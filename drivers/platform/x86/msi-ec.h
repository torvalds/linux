/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * msi-ec: MSI laptops' embedded controller driver.
 *
 * Copyright (C) 2023 Jose Angel Pastrana <japp0005@red.ujaen.es>
 * Copyright (C) 2023 Aakash Singh <mail@singhaakash.dev>
 * Copyright (C) 2023 Nikita Kravets <teackot@gmail.com>
 */

#ifndef _MSI_EC_H_
#define _MSI_EC_H_

#include <linux/types.h>

#define MSI_EC_DRIVER_NAME "msi-ec"

#define MSI_EC_ADDR_UNKNOWN 0xff01 // unknown address
#define MSI_EC_ADDR_UNSUPP  0xff01 // unsupported parameter

// Firmware info addresses are universal
#define MSI_EC_FW_VERSION_ADDRESS 0xa0
#define MSI_EC_FW_DATE_ADDRESS    0xac
#define MSI_EC_FW_TIME_ADDRESS    0xb4
#define MSI_EC_FW_VERSION_LENGTH  12
#define MSI_EC_FW_DATE_LENGTH     8
#define MSI_EC_FW_TIME_LENGTH     8

struct msi_ec_charge_control_conf {
	int address;
	int offset_start;
	int offset_end;
	int range_min;
	int range_max;
};

struct msi_ec_webcam_conf {
	int address;
	int block_address;
	int bit;
};

struct msi_ec_fn_super_swap_conf {
	int address;
	int bit;
};

struct msi_ec_cooler_boost_conf {
	int address;
	int bit;
};

#define MSI_EC_MODE_NULL { NULL, 0 }
struct msi_ec_mode {
	const char *name;
	int value;
};

struct msi_ec_shift_mode_conf {
	int address;
	struct msi_ec_mode modes[5]; // fixed size for easier hard coding
};

struct msi_ec_super_battery_conf {
	int address;
	int mask;
};

struct msi_ec_fan_mode_conf {
	int address;
	struct msi_ec_mode modes[5]; // fixed size for easier hard coding
};

struct msi_ec_cpu_conf {
	int rt_temp_address;
	int rt_fan_speed_address; // realtime
	int rt_fan_speed_base_min;
	int rt_fan_speed_base_max;
	int bs_fan_speed_address; // basic
	int bs_fan_speed_base_min;
	int bs_fan_speed_base_max;
};

struct msi_ec_gpu_conf {
	int rt_temp_address;
	int rt_fan_speed_address; // realtime
};

struct msi_ec_led_conf {
	int micmute_led_address;
	int mute_led_address;
	int bit;
};

#define MSI_EC_KBD_BL_STATE_MASK 0x3
struct msi_ec_kbd_bl_conf {
	int bl_mode_address;
	int bl_modes[2];
	int max_mode;

	int bl_state_address;
	int state_base_value;
	int max_state;
};

struct msi_ec_conf {
	const char * const *allowed_fw;

	struct msi_ec_charge_control_conf charge_control;
	struct msi_ec_webcam_conf         webcam;
	struct msi_ec_fn_super_swap_conf  fn_super_swap;
	struct msi_ec_cooler_boost_conf   cooler_boost;
	struct msi_ec_shift_mode_conf     shift_mode;
	struct msi_ec_super_battery_conf  super_battery;
	struct msi_ec_fan_mode_conf       fan_mode;
	struct msi_ec_cpu_conf            cpu;
	struct msi_ec_gpu_conf            gpu;
	struct msi_ec_led_conf            leds;
	struct msi_ec_kbd_bl_conf         kbd_bl;
};

#endif // _MSI_EC_H_
