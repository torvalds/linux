// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * msi-ec: MSI laptops' embedded controller driver.
 *
 * This driver allows various MSI laptops' functionalities to be
 * controlled from userspace.
 *
 * It contains EC memory configurations for different firmware versions
 * and exports battery charge thresholds to userspace.
 *
 * Copyright (C) 2023 Jose Angel Pastrana <japp0005@red.ujaen.es>
 * Copyright (C) 2023 Aakash Singh <mail@singhaakash.dev>
 * Copyright (C) 2023 Nikita Kravets <teackot@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "msi-ec.h"

#include <acpi/battery.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define SM_ECO_NAME		"eco"
#define SM_COMFORT_NAME		"comfort"
#define SM_SPORT_NAME		"sport"
#define SM_TURBO_NAME		"turbo"

#define FM_AUTO_NAME		"auto"
#define FM_SILENT_NAME		"silent"
#define FM_BASIC_NAME		"basic"
#define FM_ADVANCED_NAME	"advanced"

static const char * const ALLOWED_FW_0[] __initconst = {
	"14C1EMS1.012",
	"14C1EMS1.101",
	"14C1EMS1.102",
	NULL
};

static struct msi_ec_conf CONF0 __initdata = {
	.allowed_fw = ALLOWED_FW_0,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xbf,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNKNOWN, // 0xd5 needs testing
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_BASIC_NAME,    0x4d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = 0x89,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2b,
		.mute_led_address    = 0x2c,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xf3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_1[] __initconst = {
	"17F2EMS1.103",
	"17F2EMS1.104",
	"17F2EMS1.106",
	"17F2EMS1.107",
	NULL
};

static struct msi_ec_conf CONF1 __initdata = {
	.allowed_fw = ALLOWED_FW_1,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xbf,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNKNOWN,
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_BASIC_NAME,    0x4d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = 0x89,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2b,
		.mute_led_address    = 0x2c,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xf3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_2[] __initconst = {
	"1552EMS1.118",
	NULL
};

static struct msi_ec_conf CONF2 __initdata = {
	.allowed_fw = ALLOWED_FW_2,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xeb,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_BASIC_NAME,    0x4d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = 0x89,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2c,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_3[] __initconst = {
	"1592EMS1.111",
	NULL
};

static struct msi_ec_conf CONF3 __initdata = {
	.allowed_fw = ALLOWED_FW_3,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xeb,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_BASIC_NAME,    0x4d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0xc9,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = 0x89, // ?
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2b,
		.mute_led_address    = 0x2c,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_4[] __initconst = {
	"16V4EMS1.114",
	NULL
};

static struct msi_ec_conf CONF4 __initdata = {
	.allowed_fw = ALLOWED_FW_4,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = MSI_EC_ADDR_UNKNOWN, // supported, but unknown
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = { // may be supported, but address is unknown
		.address = MSI_EC_ADDR_UNKNOWN,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68, // needs testing
		.rt_fan_speed_address  = 0x71, // needs testing
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNKNOWN,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = MSI_EC_ADDR_UNKNOWN,
	},
	.leds = {
		.micmute_led_address = MSI_EC_ADDR_UNKNOWN,
		.mute_led_address    = MSI_EC_ADDR_UNKNOWN,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = MSI_EC_ADDR_UNSUPP, // 0xd3, not functional
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_5[] __initconst = {
	"158LEMS1.103",
	"158LEMS1.105",
	"158LEMS1.106",
	NULL
};

static struct msi_ec_conf CONF5 __initdata = {
	.allowed_fw = ALLOWED_FW_5,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = { // todo: reverse
		.address = 0xbf,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = { // unsupported?
		.address = MSI_EC_ADDR_UNKNOWN,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68, // needs testing
		.rt_fan_speed_address  = 0x71, // needs testing
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNKNOWN,
		.rt_fan_speed_address = MSI_EC_ADDR_UNKNOWN,
	},
	.leds = {
		.micmute_led_address = 0x2b,
		.mute_led_address    = 0x2c,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = MSI_EC_ADDR_UNSUPP, // 0xf3, not functional
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_6[] __initconst = {
	"1542EMS1.102",
	"1542EMS1.104",
	NULL
};

static struct msi_ec_conf CONF6 __initdata = {
	.allowed_fw = ALLOWED_FW_6,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = MSI_EC_ADDR_UNSUPP,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xbf, // todo: reverse
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xd5,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0xc9,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = MSI_EC_ADDR_UNKNOWN,
	},
	.leds = {
		.micmute_led_address = MSI_EC_ADDR_UNSUPP,
		.mute_led_address    = MSI_EC_ADDR_UNSUPP,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = MSI_EC_ADDR_UNSUPP, // 0xf3, not functional
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_7[] __initconst = {
	"17FKEMS1.108",
	"17FKEMS1.109",
	"17FKEMS1.10A",
	NULL
};

static struct msi_ec_conf CONF7 __initdata = {
	.allowed_fw = ALLOWED_FW_7,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = MSI_EC_ADDR_UNSUPP,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xbf, // needs testing
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNKNOWN, // 0xd5 but has its own wet of modes
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d }, // d may not be relevant
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0xc9, // needs testing
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNKNOWN,
		.rt_fan_speed_address = MSI_EC_ADDR_UNKNOWN,
	},
	.leds = {
		.micmute_led_address = MSI_EC_ADDR_UNSUPP,
		.mute_led_address    = 0x2c,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xf3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_8[] __initconst = {
	"14F1EMS1.115",
	NULL
};

static struct msi_ec_conf CONF8 __initdata = {
	.allowed_fw = ALLOWED_FW_8,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = MSI_EC_ADDR_UNSUPP,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xeb,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_BASIC_NAME,    0x4d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNKNOWN,
		.rt_fan_speed_address = MSI_EC_ADDR_UNKNOWN,
	},
	.leds = {
		.micmute_led_address = MSI_EC_ADDR_UNSUPP,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bl_modes         = { 0x00, 0x08 }, // ?
		.max_mode         = 1, // ?
		.bl_state_address = MSI_EC_ADDR_UNSUPP, // not functional
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_9[] __initconst = {
	"14JKEMS1.104",
	NULL
};

static struct msi_ec_conf CONF9 __initdata = {
	.allowed_fw = ALLOWED_FW_9,
	.charge_control = {
		.address      = 0xef,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xbf,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xf2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNSUPP, // unsupported or enabled by ECO shift
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xf4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x00,
		.rt_fan_speed_base_max = 0x96,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNSUPP,
		.rt_fan_speed_address = MSI_EC_ADDR_UNSUPP,
	},
	.leds = {
		.micmute_led_address = 0x2b,
		.mute_led_address    = 0x2c,
		.bit                 = 2,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNSUPP, // not presented in MSI app
		.bl_modes         = { 0x00, 0x08 },
		.max_mode         = 1,
		.bl_state_address = 0xf3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_10[] __initconst = {
	"1582EMS1.107", // GF66 11UC
	NULL
};

static struct msi_ec_conf CONF10 __initdata = {
	.allowed_fw = ALLOWED_FW_10,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = MSI_EC_ADDR_UNSUPP,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xe5,
		.mask    = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71, // ?
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNKNOWN, // ?
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2c,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c,
		.bl_modes         = { 0x00, 0x08 },
		.max_mode         = 1,
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_11[] __initconst = {
	"16S6EMS1.111", // Prestige 15 a11scx
	"1552EMS1.115", // Modern 15 a11m
	NULL
};

static struct msi_ec_conf CONF11 __initdata = {
	.allowed_fw = ALLOWED_FW_11,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = MSI_EC_ADDR_UNKNOWN,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = 0xeb,
		.mask = 0x0f,
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x4d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNSUPP,
		.rt_fan_speed_address = MSI_EC_ADDR_UNSUPP,
	},
	.leds = {
		.micmute_led_address = 0x2c,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN,
		.bl_modes         = {}, // ?
		.max_mode         = 1, // ?
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_12[] __initconst = {
	"16R6EMS1.104", // GF63 Thin 11UC
	NULL
};

static struct msi_ec_conf CONF12 __initdata = {
	.allowed_fw = ALLOWED_FW_12,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4,
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 },
			{ SM_COMFORT_NAME, 0xc1 },
			{ SM_SPORT_NAME,   0xc0 },
			{ SM_TURBO_NAME,   0xc4 },
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNSUPP, // 0xeb
		.mask    = 0x0f, // 00, 0f
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71,
		.rt_fan_speed_base_min = 0x19,
		.rt_fan_speed_base_max = 0x37,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = MSI_EC_ADDR_UNSUPP,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = MSI_EC_ADDR_UNSUPP,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = MSI_EC_ADDR_UNKNOWN,
		.bl_modes         = { 0x00, 0x08 },
		.max_mode         = 1,
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static const char * const ALLOWED_FW_13[] __initconst = {
	"1594EMS1.109", // MSI Prestige 16 Studio A13VE
	NULL
};

static struct msi_ec_conf CONF13 __initdata = {
	.allowed_fw = ALLOWED_FW_13,
	.charge_control = {
		.address      = 0xd7,
		.offset_start = 0x8a,
		.offset_end   = 0x80,
		.range_min    = 0x8a,
		.range_max    = 0xe4,
	},
	.webcam = {
		.address       = 0x2e,
		.block_address = 0x2f,
		.bit           = 1,
	},
	.fn_win_swap = {
		.address = 0xe8,
		.bit     = 4, // 0x00-0x10
	},
	.cooler_boost = {
		.address = 0x98,
		.bit     = 7,
	},
	.shift_mode = {
		.address = 0xd2,
		.modes = {
			{ SM_ECO_NAME,     0xc2 }, // super battery
			{ SM_COMFORT_NAME, 0xc1 }, // balanced
			{ SM_TURBO_NAME,   0xc4 }, // extreme
			MSI_EC_MODE_NULL
		},
	},
	.super_battery = {
		.address = MSI_EC_ADDR_UNSUPP,
		.mask    = 0x0f, // 00, 0f
	},
	.fan_mode = {
		.address = 0xd4,
		.modes = {
			{ FM_AUTO_NAME,     0x0d },
			{ FM_SILENT_NAME,   0x1d },
			{ FM_ADVANCED_NAME, 0x8d },
			MSI_EC_MODE_NULL
		},
	},
	.cpu = {
		.rt_temp_address       = 0x68,
		.rt_fan_speed_address  = 0x71, // 0x0-0x96
		.rt_fan_speed_base_min = 0x00,
		.rt_fan_speed_base_max = 0x96,
		.bs_fan_speed_address  = MSI_EC_ADDR_UNSUPP,
		.bs_fan_speed_base_min = 0x00,
		.bs_fan_speed_base_max = 0x0f,
	},
	.gpu = {
		.rt_temp_address      = 0x80,
		.rt_fan_speed_address = 0x89,
	},
	.leds = {
		.micmute_led_address = 0x2c,
		.mute_led_address    = 0x2d,
		.bit                 = 1,
	},
	.kbd_bl = {
		.bl_mode_address  = 0x2c, // KB auto turn off
		.bl_modes         = { 0x00, 0x08 }, // always on; off after 10 sec
		.max_mode         = 1,
		.bl_state_address = 0xd3,
		.state_base_value = 0x80,
		.max_state        = 3,
	},
};

static struct msi_ec_conf *CONFIGS[] __initdata = {
	&CONF0,
	&CONF1,
	&CONF2,
	&CONF3,
	&CONF4,
	&CONF5,
	&CONF6,
	&CONF7,
	&CONF8,
	&CONF9,
	&CONF10,
	&CONF11,
	&CONF12,
	&CONF13,
	NULL
};

static struct msi_ec_conf conf; // current configuration

/*
 * Helper functions
 */

static int ec_read_seq(u8 addr, u8 *buf, u8 len)
{
	int result;

	for (u8 i = 0; i < len; i++) {
		result = ec_read(addr + i, buf + i);
		if (result < 0)
			return result;
	}

	return 0;
}

static int ec_get_firmware_version(u8 buf[MSI_EC_FW_VERSION_LENGTH + 1])
{
	int result;

	memset(buf, 0, MSI_EC_FW_VERSION_LENGTH + 1);
	result = ec_read_seq(MSI_EC_FW_VERSION_ADDRESS,
			     buf,
			     MSI_EC_FW_VERSION_LENGTH);
	if (result < 0)
		return result;

	return MSI_EC_FW_VERSION_LENGTH + 1;
}

/*
 * Sysfs power_supply subsystem
 */

static ssize_t charge_control_threshold_show(u8 offset,
					     struct device *device,
					     struct device_attribute *attr,
					     char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(conf.charge_control.address, &rdata);
	if (result < 0)
		return result;

	return sysfs_emit(buf, "%i\n", rdata - offset);
}

static ssize_t charge_control_threshold_store(u8 offset,
					      struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	u8 wdata;
	int result;

	result = kstrtou8(buf, 10, &wdata);
	if (result < 0)
		return result;

	wdata += offset;
	if (wdata < conf.charge_control.range_min ||
	    wdata > conf.charge_control.range_max)
		return -EINVAL;

	result = ec_write(conf.charge_control.address, wdata);
	if (result < 0)
		return result;

	return count;
}

static ssize_t charge_control_start_threshold_show(struct device *device,
						   struct device_attribute *attr,
						   char *buf)
{
	return charge_control_threshold_show(conf.charge_control.offset_start,
					     device, attr, buf);
}

static ssize_t charge_control_start_threshold_store(struct device *dev,
						    struct device_attribute *attr,
						    const char *buf, size_t count)
{
	return charge_control_threshold_store(conf.charge_control.offset_start,
					      dev, attr, buf, count);
}

static ssize_t charge_control_end_threshold_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	return charge_control_threshold_show(conf.charge_control.offset_end,
					     device, attr, buf);
}

static ssize_t charge_control_end_threshold_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	return charge_control_threshold_store(conf.charge_control.offset_end,
					      dev, attr, buf, count);
}

static DEVICE_ATTR_RW(charge_control_start_threshold);
static DEVICE_ATTR_RW(charge_control_end_threshold);

static struct attribute *msi_battery_attrs[] = {
	&dev_attr_charge_control_start_threshold.attr,
	&dev_attr_charge_control_end_threshold.attr,
	NULL
};

ATTRIBUTE_GROUPS(msi_battery);

static int msi_battery_add(struct power_supply *battery,
			   struct acpi_battery_hook *hook)
{
	return device_add_groups(&battery->dev, msi_battery_groups);
}

static int msi_battery_remove(struct power_supply *battery,
			      struct acpi_battery_hook *hook)
{
	device_remove_groups(&battery->dev, msi_battery_groups);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery = msi_battery_add,
	.remove_battery = msi_battery_remove,
	.name = MSI_EC_DRIVER_NAME,
};

/*
 * Module load/unload
 */

static const struct dmi_system_id msi_dmi_table[] __initconst __maybe_unused = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MICRO-STAR INT"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International"),
		},
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, msi_dmi_table);

static int __init load_configuration(void)
{
	int result;

	u8 fw_version[MSI_EC_FW_VERSION_LENGTH + 1];

	/* get firmware version */
	result = ec_get_firmware_version(fw_version);
	if (result < 0)
		return result;

	/* load the suitable configuration, if exists */
	for (int i = 0; CONFIGS[i]; i++) {
		if (match_string(CONFIGS[i]->allowed_fw, -1, fw_version) != -EINVAL) {
			conf = *CONFIGS[i];
			conf.allowed_fw = NULL;
			return 0;
		}
	}

	/* config not found */

	for (int i = 0; i < MSI_EC_FW_VERSION_LENGTH; i++) {
		if (!isgraph(fw_version[i])) {
			pr_warn("Unable to find a valid firmware version!\n");
			return -EOPNOTSUPP;
		}
	}

	pr_warn("Firmware version is not supported: '%s'\n", fw_version);
	return -EOPNOTSUPP;
}

static int __init msi_ec_init(void)
{
	int result;

	result = load_configuration();
	if (result < 0)
		return result;

	battery_hook_register(&battery_hook);
	return 0;
}

static void __exit msi_ec_exit(void)
{
	battery_hook_unregister(&battery_hook);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Angel Pastrana <japp0005@red.ujaen.es>");
MODULE_AUTHOR("Aakash Singh <mail@singhaakash.dev>");
MODULE_AUTHOR("Nikita Kravets <teackot@gmail.com>");
MODULE_DESCRIPTION("MSI Embedded Controller");

module_init(msi_ec_init);
module_exit(msi_ec_exit);
