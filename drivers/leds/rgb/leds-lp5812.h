/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LP5812 Driver Header
 *
 * Copyright (C) 2025 Texas Instruments
 *
 * Author: Jared Zhou <jared-zhou@ti.com>
 */

#ifndef _LP5812_H_
#define _LP5812_H_

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define LP5812_REG_ENABLE				0x0000
#define LP5812_REG_RESET				0x0023
#define LP5812_DEV_CONFIG0				0x0001
#define LP5812_DEV_CONFIG1				0x0002
#define LP5812_DEV_CONFIG2				0x0003
#define LP5812_DEV_CONFIG3				0x0004
#define LP5812_DEV_CONFIG4				0x0005
#define LP5812_DEV_CONFIG5				0x0006
#define LP5812_DEV_CONFIG6				0x0007
#define LP5812_DEV_CONFIG7				0x0008
#define LP5812_DEV_CONFIG8				0x0009
#define LP5812_DEV_CONFIG9				0x000A
#define LP5812_DEV_CONFIG10				0x000B
#define LP5812_DEV_CONFIG11				0x000c
#define LP5812_DEV_CONFIG12				0x000D
#define LP5812_CMD_UPDATE				0x0010
#define LP5812_LED_EN_1					0x0020
#define LP5812_LED_EN_2					0x0021
#define LP5812_FAULT_CLEAR				0x0022
#define LP5812_MANUAL_DC_BASE				0x0030
#define LP5812_AUTO_DC_BASE				0x0050
#define LP5812_MANUAL_PWM_BASE				0x0040

#define LP5812_TSD_CONFIG_STATUS			0x0300
#define LP5812_LOD_STATUS				0x0301
#define LP5812_LSD_STATUS				0x0303

#define LP5812_ENABLE					0x01
#define LP5812_DISABLE					0x00
#define FAULT_CLEAR_ALL					0x07
#define TSD_CLEAR_VAL					0x04
#define LSD_CLEAR_VAL					0x02
#define LOD_CLEAR_VAL					0x01
#define LP5812_RESET					0x66
#define LP5812_DEV_CONFIG12_DEFAULT			0x08

#define LP5812_UPDATE_CMD_VAL				0x55
#define LP5812_REG_ADDR_HIGH_SHIFT			8
#define LP5812_REG_ADDR_BIT_8_9_MASK			0x03
#define LP5812_REG_ADDR_LOW_MASK			0xFF
#define LP5812_CHIP_ADDR_SHIFT				2
#define LP5812_DATA_LENGTH				2
#define LP5812_DATA_BYTE_0_IDX				0
#define LP5812_DATA_BYTE_1_IDX				1

#define LP5812_READ_MSG_LENGTH				2
#define LP5812_MSG_0_IDX				0
#define LP5812_MSG_1_IDX				1
#define LP5812_CFG_ERR_STATUS_MASK			0x01
#define LP5812_CFG_TSD_STATUS_SHIFT			1
#define LP5812_CFG_TSD_STATUS_MASK			0x01

#define LP5812_FAULT_CLEAR_LOD				0
#define LP5812_FAULT_CLEAR_LSD				1
#define LP5812_FAULT_CLEAR_TSD				2
#define LP5812_FAULT_CLEAR_ALL				3
#define LP5812_NUMBER_LED_IN_REG			8

#define LP5812_WAIT_DEVICE_STABLE_MIN			1000
#define LP5812_WAIT_DEVICE_STABLE_MAX			1100

#define LP5812_LSD_LOD_START_UP				0x0B
#define LP5812_MODE_NAME_MAX_LEN			20
#define LP5812_MODE_DIRECT_NAME				"direct_mode"
#define LP5812_MODE_DIRECT_VALUE			0
#define LP5812_MODE_MIX_SELECT_LED_0			0
#define LP5812_MODE_MIX_SELECT_LED_1			1
#define LP5812_MODE_MIX_SELECT_LED_2			2
#define LP5812_MODE_MIX_SELECT_LED_3			3

enum control_mode {
	LP5812_MODE_MANUAL = 0,
	LP5812_MODE_AUTONOMOUS
};

enum dimming_type {
	LP5812_DIMMING_ANALOG,
	LP5812_DIMMING_PWM
};

union lp5812_scan_order {
	struct {
		u8 order0:2;
		u8 order1:2;
		u8 order2:2;
		u8 order3:2;
	} bits;
	u8 val;
};

union lp5812_drive_mode {
	struct {
		u8 mix_sel_led_0:1;
		u8 mix_sel_led_1:1;
		u8 mix_sel_led_2:1;
		u8 mix_sel_led_3:1;
		u8 led_mode:3;
		u8 pwm_fre:1;
	} bits;
	u8 val;
};

struct lp5812_reg {
	u16 addr;
	union {
		u8 val;
		u8 mask;
		u8 shift;
	};
};

struct lp5812_mode_mapping {
	char mode_name[LP5812_MODE_NAME_MAX_LEN];
	u8 mode;
	u8 scan_order_0;
	u8 scan_order_1;
	u8 scan_order_2;
	u8 scan_order_3;
	u8 selection_led;
};

struct lp5812_led_config {
	bool is_sc_led;
	const char *name;
	u8 color_id[LED_COLOR_ID_MAX];
	u32 max_current[LED_COLOR_ID_MAX];
	int chan_nr;
	int num_colors;
	int led_id[LED_COLOR_ID_MAX];
};

struct lp5812_chip {
	u8 num_channels;
	struct i2c_client *client;
	struct mutex lock; /* Protects register access */
	struct lp5812_led_config *led_config;
	const char *label;
	const char *scan_mode;
	union lp5812_scan_order scan_order;
	union lp5812_drive_mode drive_mode;
};

struct lp5812_led {
	u8 brightness;
	int chan_nr;
	struct led_classdev cdev;
	struct led_classdev_mc mc_cdev;
	struct lp5812_chip *chip;
};

#endif /*_LP5812_H_*/
