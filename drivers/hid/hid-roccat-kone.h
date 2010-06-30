#ifndef __HID_ROCCAT_KONE_H
#define __HID_ROCCAT_KONE_H

/*
 * Copyright (c) 2010 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/types.h>

#define ROCCAT_KONE_DRIVER_VERSION "v0.3.1"

#pragma pack(push)
#pragma pack(1)

struct kone_keystroke {
	uint8_t key;
	uint8_t action;
	uint16_t period; /* in milliseconds */
};

enum kone_keystroke_buttons {
	kone_keystroke_button_1 = 0xf0, /* left mouse button */
	kone_keystroke_button_2 = 0xf1, /* right mouse button */
	kone_keystroke_button_3 = 0xf2, /* wheel */
	kone_keystroke_button_9 = 0xf3, /* side button up */
	kone_keystroke_button_8 = 0xf4 /* side button down */
};

enum kone_keystroke_actions {
	kone_keystroke_action_press = 0,
	kone_keystroke_action_release = 1
};

struct kone_button_info {
	uint8_t number; /* range 1-8 */
	uint8_t type;
	uint8_t macro_type; /* 0 = short, 1 = overlong */
	uint8_t macro_set_name[16]; /* can be max 15 chars long */
	uint8_t macro_name[16]; /* can be max 15 chars long */
	uint8_t count;
	struct kone_keystroke keystrokes[20];
};

enum kone_button_info_types {
	/* valid button types until firmware 1.32 */
	kone_button_info_type_button_1 = 0x1, /* click (left mouse button) */
	kone_button_info_type_button_2 = 0x2, /* menu (right mouse button)*/
	kone_button_info_type_button_3 = 0x3, /* scroll (wheel) */
	kone_button_info_type_double_click = 0x4,
	kone_button_info_type_key = 0x5,
	kone_button_info_type_macro = 0x6,
	kone_button_info_type_off = 0x7,
	/* TODO clarify function and rename */
	kone_button_info_type_osd_xy_prescaling = 0x8,
	kone_button_info_type_osd_dpi = 0x9,
	kone_button_info_type_osd_profile = 0xa,
	kone_button_info_type_button_9 = 0xb, /* ie forward */
	kone_button_info_type_button_8 = 0xc, /* ie backward */
	kone_button_info_type_dpi_up = 0xd, /* internal */
	kone_button_info_type_dpi_down = 0xe, /* internal */
	kone_button_info_type_button_7 = 0xf, /* tilt left */
	kone_button_info_type_button_6 = 0x10, /* tilt right */
	kone_button_info_type_profile_up = 0x11, /* internal */
	kone_button_info_type_profile_down = 0x12, /* internal */
	/* additional valid button types since firmware 1.38 */
	kone_button_info_type_multimedia_open_player = 0x20,
	kone_button_info_type_multimedia_next_track = 0x21,
	kone_button_info_type_multimedia_prev_track = 0x22,
	kone_button_info_type_multimedia_play_pause = 0x23,
	kone_button_info_type_multimedia_stop = 0x24,
	kone_button_info_type_multimedia_mute = 0x25,
	kone_button_info_type_multimedia_volume_up = 0x26,
	kone_button_info_type_multimedia_volume_down = 0x27
};

enum kone_button_info_numbers {
	kone_button_top = 1,
	kone_button_wheel_tilt_left = 2,
	kone_button_wheel_tilt_right = 3,
	kone_button_forward = 4,
	kone_button_backward = 5,
	kone_button_middle = 6,
	kone_button_plus = 7,
	kone_button_minus = 8,
};

struct kone_light_info {
	uint8_t number; /* number of light 1-5 */
	uint8_t mod;   /* 1 = on, 2 = off */
	uint8_t red;   /* range 0x00-0xff */
	uint8_t green; /* range 0x00-0xff */
	uint8_t blue;  /* range 0x00-0xff */
};

struct kone_profile {
	uint16_t size; /* always 975 */
	uint16_t unused; /* always 0 */

	/*
	 * range 1-5
	 * This number does not need to correspond with location where profile
	 * saved
	 */
	uint8_t profile; /* range 1-5 */

	uint16_t main_sensitivity; /* range 100-1000 */
	uint8_t xy_sensitivity_enabled; /* 1 = on, 2 = off */
	uint16_t x_sensitivity; /* range 100-1000 */
	uint16_t y_sensitivity; /* range 100-1000 */
	uint8_t dpi_rate; /* bit 1 = 800, ... */
	uint8_t startup_dpi; /* range 1-6 */
	uint8_t polling_rate; /* 1 = 125Hz, 2 = 500Hz, 3 = 1000Hz */
	/* kone has no dcu
	 * value is always 2 in firmwares <= 1.32 and
	 * 1 in firmwares > 1.32
	 */
	uint8_t dcu_flag;
	uint8_t light_effect_1; /* range 1-3 */
	uint8_t light_effect_2; /* range 1-5 */
	uint8_t light_effect_3; /* range 1-4 */
	uint8_t light_effect_speed; /* range 0-255 */

	struct kone_light_info light_infos[5];
	/* offset is kone_button_info_numbers - 1 */
	struct kone_button_info button_infos[8];

	uint16_t checksum; /* \brief holds checksum of struct */
};

enum kone_polling_rates {
	kone_polling_rate_125 = 1,
	kone_polling_rate_500 = 2,
	kone_polling_rate_1000 = 3
};

struct kone_settings {
	uint16_t size; /* always 36 */
	uint8_t  startup_profile; /* 1-5 */
	uint8_t	 unknown1;
	uint8_t  tcu; /* 0 = off, 1 = on */
	uint8_t  unknown2[23];
	uint8_t  calibration_data[4];
	uint8_t  unknown3[2];
	uint16_t checksum;
};

/*
 * 12 byte mouse event read by interrupt_read
 */
struct kone_mouse_event {
	uint8_t report_number; /* always 1 */
	uint8_t button;
	uint16_t x;
	uint16_t y;
	uint8_t wheel; /* up = 1, down = -1 */
	uint8_t tilt; /* right = 1, left = -1 */
	uint8_t unknown;
	uint8_t event;
	uint8_t value; /* press = 0, release = 1 */
	uint8_t macro_key; /* 0 to 8 */
};

enum kone_mouse_events {
	/* osd events are thought to be display on screen */
	kone_mouse_event_osd_dpi = 0xa0,
	kone_mouse_event_osd_profile = 0xb0,
	/* TODO clarify meaning and occurence of kone_mouse_event_calibration */
	kone_mouse_event_calibration = 0xc0,
	kone_mouse_event_call_overlong_macro = 0xe0,
	/* switch events notify if user changed values with mousebutton click */
	kone_mouse_event_switch_dpi = 0xf0,
	kone_mouse_event_switch_profile = 0xf1
};

enum kone_commands {
	kone_command_profile = 0x5a,
	kone_command_settings = 0x15a,
	kone_command_firmware_version = 0x25a,
	kone_command_weight = 0x45a,
	kone_command_calibrate = 0x55a,
	kone_command_confirm_write = 0x65a,
	kone_command_firmware = 0xe5a
};

struct kone_roccat_report {
	uint8_t event;
	uint8_t value; /* holds dpi or profile value */
	uint8_t key; /* macro key on overlong macro execution */
};

#pragma pack(pop)

struct kone_device {
	/*
	 * Storing actual values when we get informed about changes since there
	 * is no way of getting this information from the device on demand
	 */
	int actual_profile, actual_dpi;
	/* Used for neutralizing abnormal button behaviour */
	struct kone_mouse_event last_mouse_event;

	/*
	 * It's unlikely that multiple sysfs attributes are accessed at a time,
	 * so only one mutex is used to secure hardware access and profiles and
	 * settings of this struct.
	 */
	struct mutex kone_lock;

	/*
	 * Storing the data here reduces IO and ensures that data is available
	 * when its needed (E.g. interrupt handler).
	 */
	struct kone_profile profiles[5];
	struct kone_settings settings;

	/*
	 * firmware doesn't change unless firmware update is implemented,
	 * so it's read only once
	 */
	int firmware_version;

	int roccat_claimed;
	int chrdev_minor;
};

#endif
