/*
Copyright (C) 2014 Mark Reed

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "pebble.h"

GColor background_color = GColorBlack;
GColor text_color = GColorWhite;

static AppSync sync;
static uint8_t sync_buffer[64];

static int invert;
static int bluetoothvibe;
static int hourlyvibe;

static bool appStarted = false;

enum {
  INVERT_COLOR_KEY = 0x0,
  BLUETOOTHVIBE_KEY = 0x1,
  HOURLYVIBE_KEY = 0x2
};

Window *window;
static Layer *window_layer;

TextLayer *layer_date_text;
TextLayer *layer_time_text;
TextLayer *layer_bt_text;
TextLayer *layer_wday_text;

int cur_day = -1;

int charge_percent = 0;

TextLayer *battery_text_layer;

InverterLayer *inverter_layer = NULL;

static Layer *path_layer;

static GPath *batt10;
static GPath *batt20;
static GPath *batt30;
static GPath *batt40;
static GPath *batt50;
static GPath *batt60;
static GPath *batt70;
static GPath *batt80;
static GPath *batt90;
static GPath *batt100;

#define NUM_GRAPHIC_PATHS 10

static GPath *graphic_paths[NUM_GRAPHIC_PATHS];

static GPath *current_path = NULL;

static int current_path_index = 0;

static bool outline_mode = false;

// This defines graphics path information to be loaded as a path later
static const GPathInfo BATT10 = {
  4,
  (GPoint []) {
    {0, 1},
    {72, 1},
    {72, 3},
    {0, 3}
  }
};

static const GPathInfo BATT20 = {
  4,
  (GPoint []) {
    {0, 1},
    {143, 1},
    {143, 3},
    {0, 3}
  }
};

static const GPathInfo BATT30 = {
  6,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 56},
	{140, 56},
    {140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT40 = {
  6,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 112},
	{140, 112},
    {140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT50 = {
  6,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{140, 166},
    {140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT60 = {
  8,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{72, 166},
	{72,164},
	{141, 164},
    {141, 3},
    {0, 3}
  }
};

static const GPathInfo BATT70 = {
  8,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{0, 166},
	{0, 164},
	{140, 164},
	{140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT80 = {
  10,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{0, 166},
	{0, 112},
	{3, 112},
	{3, 164},
	{140, 164},
	{140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT90 = {
  10,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{0, 166},
	{0, 56},
	{3, 56},	
	{3, 164},
	{140, 164},
	{140, 3},
    {0, 3}
  }
};

static const GPathInfo BATT100 = {
  10,
  (GPoint []) {
    {0, 1},
    {143, 1},
	{143, 166},
	{0, 166},
	{0, 3},
	{3,3},
	{3, 164},
	{140, 164},
	{140, 3},
    {0, 3}
  }
};


static void path_layer_update_callback(Layer *me, GContext *ctx) {
  (void)me;
	if (outline_mode) {
    // draw outline uses the stroke color
    graphics_context_set_stroke_color(ctx, GColorWhite);
    gpath_draw_outline(ctx, current_path);
  } else {
    // draw filled uses the fill color
    graphics_context_set_fill_color(ctx, GColorWhite);
    gpath_draw_filled(ctx, current_path);
  }
}

void change_background(bool invert) {
  if (invert && inverter_layer == NULL) {
    // Add inverter layer
    Layer *window_layer = window_get_root_layer(window);

    inverter_layer = inverter_layer_create(GRect(0, 0, 144, 168));
    layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));

  } else if (!invert && inverter_layer != NULL) {
    // Remove Inverter layer
    layer_remove_from_parent(inverter_layer_get_layer(inverter_layer));
    inverter_layer_destroy(inverter_layer);
    inverter_layer = NULL;
  }
  // No action required
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed);

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
  switch (key) {
    case INVERT_COLOR_KEY:
      invert = new_tuple->value->uint8 != 0;
	  persist_write_bool(INVERT_COLOR_KEY, invert);
      change_background(invert);
      break;
	  
    case BLUETOOTHVIBE_KEY:
      bluetoothvibe = new_tuple->value->uint8 != 0;
	  persist_write_bool(BLUETOOTHVIBE_KEY, bluetoothvibe);
      break;      
	  
    case HOURLYVIBE_KEY:
      hourlyvibe = new_tuple->value->uint8 != 0;
	  persist_write_bool(HOURLYVIBE_KEY, hourlyvibe);	  
      break;	  
  }
}

void update_battery_state(BatteryChargeState charge_state) {
	

    static char battery_text[] = "x100%";

	  if (charge_state.is_charging) {
		graphic_paths[9] = batt100;
		current_path = graphic_paths[9];	
        snprintf(battery_text, sizeof(battery_text), "+%d%%", charge_state.charge_percent);
		
    } else {
        snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);
        
		  if (charge_state.charge_percent <= 10) {  
				graphic_paths[0] = batt10;
			  	current_path = graphic_paths[0];
          } else if (charge_state.charge_percent <= 20) {
			  graphic_paths[1] = batt20;
			  current_path = graphic_paths[1];
		  } else if (charge_state.charge_percent <= 30) {
			  graphic_paths[2] = batt30;
			  current_path = graphic_paths[2];
	 	  } else if (charge_state.charge_percent <= 40) {
			  graphic_paths[3] = batt40;
			  current_path = graphic_paths[3];
		  } else if (charge_state.charge_percent <= 50) {
			  graphic_paths[4] = batt50;
			  current_path = graphic_paths[4];
		  } else if (charge_state.charge_percent <= 60) {
			  graphic_paths[5] = batt60;
			  current_path = graphic_paths[5];
		  } else if (charge_state.charge_percent <= 70) {
			  graphic_paths[6] = batt70;
			  current_path = graphic_paths[6];
		  } else if (charge_state.charge_percent <= 80) {
			  graphic_paths[7] = batt80;
			  current_path = graphic_paths[7];
		  } else if (charge_state.charge_percent <= 90) {
			  graphic_paths[8] = batt90;
			  current_path = graphic_paths[8];
		  } else if (charge_state.charge_percent <= 98) {
			  graphic_paths[9] = batt100;
			  current_path = graphic_paths[9];
		  
		  } else {
			  graphic_paths[9] = batt100;
			  current_path = graphic_paths[9];
		  }
		
    } 
    charge_percent = charge_state.charge_percent;
    
    text_layer_set_text(battery_text_layer, battery_text);
	
	
	if (charge_state.is_charging) {
        layer_set_hidden(text_layer_get_layer(battery_text_layer), false);
	}      	
} 


static void toggle_bluetooth(bool connected) {
    if (appStarted && !connected && bluetoothvibe) {
	  
	static char bt_text[] = "xxx xxxxxxxxx";
	  
	    snprintf(bt_text, sizeof(bt_text), "NOT Connected");
	    text_layer_set_text(layer_bt_text, bt_text);
	
    //vibe!
    vibes_long_pulse();
	
    } else {
			static char bt_text[] = "xxx xxxxxxxxx";

        snprintf(bt_text, sizeof(bt_text), "Connected");
        text_layer_set_text(layer_bt_text, bt_text);
   }
}

void bluetooth_connection_callback(bool connected) {
  toggle_bluetooth(connected);
}

void update_time(struct tm *tick_time) {

	static char time_text[] = "00:00";
    static char date_text[] = "00xx xx XXXXXXXXX";
    static char wday_text[] = "xxxxxxxxx";

    char *time_format;

    int new_cur_day = tick_time->tm_year*1000 + tick_time->tm_yday;
    if (new_cur_day != cur_day) {
        cur_day = new_cur_day;

			switch(tick_time->tm_mday)
  {
    case 1 :
    case 21 :
    case 31 :
      strftime(date_text, sizeof(date_text), "%est of %b", tick_time);
      break;
    case 2 :
    case 22 :
      strftime(date_text, sizeof(date_text), "%end of %b", tick_time);
      break;
    case 3 :
    case 23 :
      strftime(date_text, sizeof(date_text), "%erd of %b", tick_time);
      break;
    default :
      strftime(date_text, sizeof(date_text), "%eth of %B", tick_time);
      break;
  }
	        strftime(wday_text, sizeof(wday_text), "%A", tick_time);
            text_layer_set_text(layer_wday_text, wday_text);
		
        	text_layer_set_text(layer_date_text, date_text);
		
		
    }

    if (clock_is_24h_style()) {
        time_format = "%R";
		
    } else {
        time_format = "%I:%M";
		
    }

    strftime(time_text, sizeof(time_text), time_format, tick_time);

    if (!clock_is_24h_style() && (time_text[0] == '0')) {
        memmove(time_text, &time_text[1], sizeof(time_text) - 1);
    }

    text_layer_set_text(layer_time_text, time_text);
}

void set_style(void) {
    
    background_color  = GColorBlack;
    text_color = GColorWhite;
	
	// set-up layer colours
    window_set_background_color(window, background_color);
    text_layer_set_text_color(layer_time_text, text_color);
    text_layer_set_text_color(layer_date_text, text_color);
    text_layer_set_text_color(battery_text_layer, text_color);
    text_layer_set_text_color(layer_bt_text, text_color);
	text_layer_set_text_color(layer_wday_text, text_color);

}

void force_update(void) {
    toggle_bluetooth(bluetooth_connection_service_peek());
    time_t now = time(NULL);
    update_time(localtime(&now));
}

void hourvibe (struct tm *tick_time) {
  if (appStarted && hourlyvibe) {
    //vibe!
    vibes_short_pulse();
  }
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
    update_time(tick_time);
   if (units_changed & HOUR_UNIT) {
    hourvibe(tick_time);
   }
}

void handle_init(void) {
	
	const int inbound_size = 64;
    const int outbound_size = 64;
    app_message_open(inbound_size, outbound_size);  
	
    window = window_create();
    window_stack_push(window, true);
 
    window_layer = window_get_root_layer(window);

  GRect bounds = layer_get_frame(window_layer);
  path_layer = layer_create(bounds);
  layer_set_update_proc(path_layer, path_layer_update_callback);
  layer_add_child(window_layer, path_layer);

  // Pass the corresponding GPathInfo to initialize a GPath
  batt10 = gpath_create(&BATT10);
  batt20 = gpath_create(&BATT20);
  batt30 = gpath_create(&BATT30);
  batt40 = gpath_create(&BATT40);
  batt50 = gpath_create(&BATT50);
  batt60 = gpath_create(&BATT60);
  batt70 = gpath_create(&BATT70);
  batt80 = gpath_create(&BATT80);
  batt90 = gpath_create(&BATT90);
  batt100 = gpath_create(&BATT100);
  
	// resources

    // layer position and alignment
	
    layer_time_text = text_layer_create(GRect(0, 55, 144, 50));
    layer_date_text = text_layer_create(GRect(0, 140, 144, 20));
    battery_text_layer = text_layer_create(GRect(0, 20, 144, 22));
    layer_bt_text  = text_layer_create(GRect(0, 5, 144, 20));
    layer_wday_text = text_layer_create(GRect(0, 125, 144, 20));

    text_layer_set_background_color(layer_date_text, GColorClear);
    text_layer_set_font(layer_date_text, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_background_color(layer_time_text, GColorClear);
    text_layer_set_font(layer_time_text, fonts_get_system_font(FONT_KEY_BITHAM_42_MEDIUM_NUMBERS));
//    text_layer_set_font(layer_time_text, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
    text_layer_set_background_color(battery_text_layer, GColorClear);
    text_layer_set_font(battery_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	text_layer_set_background_color(layer_bt_text, GColorClear);
    text_layer_set_font(layer_bt_text, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_background_color(layer_wday_text, GColorClear);
    text_layer_set_font(layer_wday_text, fonts_get_system_font(FONT_KEY_GOTHIC_14));
	
    text_layer_set_text_alignment(layer_date_text, GTextAlignmentCenter);
    text_layer_set_text_alignment(layer_time_text, GTextAlignmentCenter);
    text_layer_set_text_alignment(battery_text_layer, GTextAlignmentCenter);
    text_layer_set_text_alignment(layer_bt_text, GTextAlignmentCenter);
	text_layer_set_text_alignment(layer_wday_text, GTextAlignmentCenter);

    // composing layers
    layer_add_child(window_layer, text_layer_get_layer(layer_date_text));
    layer_add_child(window_layer, text_layer_get_layer(layer_time_text));
    layer_add_child(window_layer, text_layer_get_layer(battery_text_layer));
    layer_add_child(window_layer, text_layer_get_layer(layer_bt_text));
    layer_add_child(window_layer, text_layer_get_layer(layer_wday_text));

    set_style();
	
    // handlers
    battery_state_service_subscribe(&update_battery_state);
    bluetooth_connection_service_subscribe(&toggle_bluetooth);
    tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

	Tuplet initial_values[] = {
    	TupletInteger(INVERT_COLOR_KEY, persist_read_bool(INVERT_COLOR_KEY)),
    	TupletInteger(BLUETOOTHVIBE_KEY, persist_read_bool(BLUETOOTHVIBE_KEY)),
    	TupletInteger(HOURLYVIBE_KEY, persist_read_bool(HOURLYVIBE_KEY)),
  };
  
    app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values),
      sync_tuple_changed_callback, NULL, NULL);
   
    appStarted = true;
	
	// update the battery on launch
    update_battery_state(battery_state_service_peek());
	
    // draw first frame
    force_update();
}

void handle_deinit(void) {
  app_sync_deinit(&sync);

  tick_timer_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
	
  text_layer_destroy( layer_time_text );
  text_layer_destroy( layer_date_text );
  text_layer_destroy( layer_wday_text );
  text_layer_destroy( battery_text_layer );
  text_layer_destroy( layer_bt_text );
		
	  gpath_destroy(batt10);
	  gpath_destroy(batt20);
	  gpath_destroy(batt30);
	  gpath_destroy(batt40);
	  gpath_destroy(batt50);
	  gpath_destroy(batt60);
	  gpath_destroy(batt70);
	  gpath_destroy(batt80);
	  gpath_destroy(batt90);
	  gpath_destroy(batt100);

  layer_destroy(path_layer);

  window_destroy(window);

}

int main(void) {
    handle_init();
    app_event_loop();
    handle_deinit();
}
