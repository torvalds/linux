// SPDX-License-Identifier: GPL-2.0-only
/* drivers/input/touchscreen/gt9xx.c
 *
 * 2010 - 2013 Goodix Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 2.2
 * Authors: andrew@goodix.com, meta@goodix.com
 * Release Date: 2014/01/14
 * Revision record:
 *      V1.0:
 *          first Release. By Andrew, 2012/08/31
 *      V1.2:
 *          modify gtp_reset_guitar,slot report,tracking_id & 0x0F. By Andrew, 2012/10/15
 *      V1.4:
 *          modify gt9xx_update.c. By Andrew, 2012/12/12
 *      V1.6:
 *          1. new heartbeat/esd_protect mechanism(add external watchdog)
 *          2. doze mode, sliding wakeup
 *          3. 3 more cfg_group(GT9 Sensor_ID: 0~5)
 *          3. config length verification
 *          4. names & comments
 *                  By Meta, 2013/03/11
 *      V1.8:
 *          1. pen/stylus identification
 *          2. read double check & fixed config support
 *          3. new esd & slide wakeup optimization
 *                  By Meta, 2013/06/08
 *      V2.0:
 *          1. compatible with GT9XXF
 *          2. send config after resume
 *                  By Meta, 2013/08/06
 *      V2.2:
 *          1. gt9xx_config for debug
 *          2. gesture wakeup
 *          3. pen separate input device, active-pen button support
 *          4. coordinates & keys optimization
 *                  By Meta, 2014/01/14
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/regulator/consumer.h>
#include <linux/input/mt.h>

#define CONFIG_8_9  0

/***************************PART1:ON/OFF define*******************************/
#if CONFIG_8_9
#define GTP_CHANGE_X2Y        1
#define GTP_X_REVERSE_ENABLE	0
#define GTP_Y_REVERSE_ENABLE	1
#else
#define GTP_CHANGE_X2Y        1
#define GTP_X_REVERSE_ENABLE	1
#define GTP_Y_REVERSE_ENABLE	0
#endif
#define GTP_DRIVER_SEND_CFG   1
#define GTP_HAVE_TOUCH_KEY    0
#define GTP_POWER_CTRL_SLEEP  0

#define GTP_ESD_PROTECT       0    // esd protection with a cycle of 2 seconds

#define GTP_WITH_PEN          0
#define GTP_PEN_HAVE_BUTTON   0    // active pen has buttons, function together with GTP_WITH_PEN

#define GTP_GESTURE_WAKEUP    0    // gesture wakeup

#define GTP_DEBUG_ON          1

struct goodix_ts_data {
	spinlock_t irq_lock;
	struct i2c_client *client;
	struct input_dev  *input_dev;
	struct work_struct  work;
	s32 irq_is_disable;
	u16 abs_x_max;
	u16 abs_y_max;
	u8  max_touch_num;
	u8  int_trigger_type;
	u8  gtp_is_suspend;
	u8  gtp_cfg_len;
	u8  fixed_cfg;
	u8  pnl_init_error;
	u8 cfg_file_num;

	int irq;
	int irq_pin;
	int pwr_pin;
	int rst_pin;
	unsigned long irq_flags;

#if GTP_WITH_PEN
	struct input_dev *pen_dev;
#endif

#if GTP_ESD_PROTECT
	spinlock_t esd_lock;
	u8  esd_running;
	s32 clk_tick_cnt;
#endif

	struct regulator *tp_regulator;
};

/*************************** PART2:TODO define **********************************/
// STEP_1(REQUIRED): Define Configuration Information Group(s)
// Sensor_ID Map:
/* sensor_opt1 sensor_opt2 Sensor_ID
 *    GND         GND         0
 *    VDDIO       GND         1
 *    NC          GND         2
 *    GND         NC/300K     3
 *    VDDIO       NC/300K     4
 *    NC          NC/300K     5
 **/

// TODO: define your config for Sensor_ID == 1 here, if needed
#define CTP_CFG_GROUP2 {\
	}

// TODO: define your config for Sensor_ID == 2 here, if needed
#define CTP_CFG_GROUP3 {\
	}

// TODO: define your config for Sensor_ID == 3 here, if needed
#define CTP_CFG_GROUP4 {\
	}
// TODO: define your config for Sensor_ID == 4 here, if needed
#define CTP_CFG_GROUP5 {\
	}

// TODO: define your config for Sensor_ID == 5 here, if needed
#define CTP_CFG_GROUP6 {\
	}

// STEP_2(REQUIRED): Customize your I/O ports & I/O operations
#define GTP_GPIO_OUTPUT(pin, level)      gpio_direction_output(pin, level)
#define GTP_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define GTP_GPIO_FREE(pin)              gpio_free(pin)

// STEP_3(optional): Specify your special config info if needed
#define GTP_MAX_HEIGHT   4096
#define GTP_MAX_WIDTH    4096
#define GTP_INT_TRIGGER  1	/* 0:Rising, 1:Falling */
#define GTP_MAX_TOUCH         10

// STEP_4(optional): If keys are available and reported as keys, config your key info here
#if GTP_HAVE_TOUCH_KEY
    #define GTP_KEY_TAB  {KEY_MENU, KEY_HOME, KEY_BACK}
#endif

/***************************PART3:OTHER define*********************************/
#define GTP_DRIVER_VERSION          "V2.2<2014/01/14>"
#define GTP_I2C_NAME                "Goodix-TS"
#define GTP_ADDR_LENGTH       2
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240
#define FAIL                  0
#define SUCCESS               1
#define SWITCH_OFF            0
#define SWITCH_ON             1

// Registers define
#define GTP_REG_CHIP_TYPE     0x8000
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_SLEEP         0x8040
#define GTP_REG_SENSOR_ID     0x814A
#define GTP_REG_CONFIG_DATA   0x8047
#define GTP_REG_VERSION       0x8140

#define RESOLUTION_LOC        3
#define TRIGGER_LOC           8

#define CFG_GROUP_LEN(p_cfg_grp)  ARRAY_SIZE(p_cfg_grp)

#define GTP_SWAP(x, y) \
	do {\
		typeof(x) z = x;\
		x = y;\
		y = z;\
	} while (0)

/* CFG for GT911 */
u8 gtp_dat_gt11[] = {
	/* <1200, 1920> WGJ89006B_GT911_Config */
	0x43, 0x00, 0x10, 0x00, 0x10, 0x0A, 0x3D, 0x20, 0x01, 0x08,
	0x28, 0x08, 0x50, 0x32, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x11, 0x00, 0x00, 0x18, 0x1A, 0x20, 0x14, 0x8C, 0x2E, 0x0E,
	0x3C, 0x3E, 0x0C, 0x08, 0x00, 0x00, 0x00, 0x41, 0x03, 0x1D,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x33, 0x5A, 0x94, 0xC5, 0x02, 0x08, 0x00, 0x00, 0x04,
	0xA0, 0x36, 0x00, 0x8B, 0x3C, 0x00, 0x7C, 0x43, 0x00, 0x6B,
	0x4C, 0x00, 0x5F, 0x55, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10,
	0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0F,
	0x10, 0x12, 0x13, 0x14, 0x16, 0x18, 0x1C, 0x1D, 0x1E, 0x1F,
	0x2A, 0x29, 0x28, 0x26, 0x24, 0x22, 0x21, 0x20, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0A, 0x01
};

u8 gtp_dat_gt9110[] = {
	/* <1200, 1920> GT9110P(2020)V71_Config */
	0x47, 0x80, 0x07, 0xB0, 0x04, 0x0A, 0x7C, 0x00, 0x01, 0x08,
	0x28, 0x05, 0x5A, 0x3C, 0x03, 0x04, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x00, 0x00, 0x17, 0x19, 0x1D, 0x14, 0x95, 0x35, 0xFF,
	0x3C, 0x3E, 0x0C, 0x08, 0x00, 0x00, 0x00, 0xBA, 0x02, 0x2C,
	0x17, 0x19, 0x1B, 0x90, 0x83, 0x32, 0x50, 0x3C, 0x28, 0x4A,
	0x30, 0x32, 0x96, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04,
	0x83, 0x38, 0x00, 0x79, 0x45, 0x00, 0x71, 0x57, 0x00, 0x6B,
	0x6C, 0x00, 0x68, 0x87, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x03,
	0x00, 0x48, 0x21, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
	0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
	0x1C, 0x1D, 0x2A, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23,
	0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x19, 0x18,
	0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E,
	0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
	0x03, 0x02, 0x01, 0x00, 0x53, 0x01
};

u8 gtp_dat_gt9111[] = {
	/* HLS-0102-1398V1-1060-GT911_Config */
	0x42, 0x00, 0x03, 0x00, 0x04, 0x0A, 0x45, 0x03, 0x22, 0x1F,
	0x28, 0x0F, 0x64, 0x3C, 0x03, 0x0F, 0x00, 0x00, 0x00, 0x00,
	0x11, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x29, 0x0E,
	0x71, 0x6F, 0xB2, 0x04, 0x00, 0x00, 0x00, 0x39, 0x02, 0x10,
	0x00, 0x21, 0x00, 0x00, 0x00, 0x03, 0x64, 0x32, 0x00, 0x00,
	0x00, 0x3C, 0x78, 0x94, 0xD5, 0x02, 0x07, 0x00, 0x00, 0x04,
	0xC8, 0x40, 0x00, 0xB1, 0x4A, 0x00, 0x9E, 0x55, 0x00, 0x8E,
	0x61, 0x00, 0x7F, 0x70, 0x00, 0x7F, 0x70, 0x00, 0x00, 0x00,
	0xF0, 0x90, 0x3C, 0xFF, 0xFF, 0x07, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0E,
	0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0F,
	0x10, 0x12, 0x13, 0x16, 0x18, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
	0x21, 0x22, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xF6, 0x01
};

u8 gtp_dat_gt9112[] = {
	/* <800, 1280> CJ080258_GT911_Config */
	0x62, 0x20, 0x03, 0x00, 0x05, 0x0A, 0x05, 0x00, 0x01, 0x08,
	0x28, 0x05, 0x50, 0x32, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8C, 0x2A, 0x0E,
	0x17, 0x15, 0x31, 0x0D, 0x00, 0x00, 0x01, 0x9A, 0x04, 0x1D,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x64, 0x32, 0x00, 0x00,
	0x00, 0x0F, 0x36, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04,
	0x9B, 0x11, 0x00, 0x7B, 0x16, 0x00, 0x64, 0x1C, 0x00, 0x4F,
	0x25, 0x00, 0x41, 0x2F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0E,
	0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x24, 0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1D, 0x1C,
	0x18, 0x16, 0x14, 0x13, 0x12, 0x10, 0x0F, 0x0C, 0x0A, 0x08,
	0x06, 0x04, 0x02, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x72, 0x01
};

u8 gtp_dat_8_9[] = {
	/* <1920, 1200> 8.9 WGJ10162B_GT9271_1060_Config */
	0x42, 0x80, 0x07, 0xB0, 0x04, 0x0A, 0x35, 0x00, 0x02, 0x0F,
	0x28, 0x0F, 0x5A, 0x3C, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x17, 0x19, 0x1D, 0x14, 0x8F, 0x2F, 0xAA,
	0x37, 0x39, 0xD9, 0x0B, 0x00, 0x00, 0x00, 0x83, 0x02, 0x1D,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x2A, 0x46, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04,
	0x95, 0x2C, 0x00, 0x8A, 0x31, 0x00, 0x81, 0x36, 0x00, 0x79,
	0x3C, 0x00, 0x72, 0x42, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x11, 0x10,
	0x0F, 0x0E, 0x0D, 0x0C, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0C,
	0x0D, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x19, 0x1B, 0x1C,
	0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x16, 0x01
};

u8 gtp_dat_8_9_1[] = {
	/* GT9271_Config */
	0x00, 0x80, 0x07, 0xB0, 0x04, 0x0A, 0x3C, 0x00, 0x01, 0x0A,
	0x28, 0x1F, 0x55, 0x32, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x18, 0x1A, 0x1E, 0x14, 0x90, 0x2F, 0xAA,
	0x1E, 0x20, 0x31, 0x0D, 0x00, 0x00, 0x00, 0x22, 0x03, 0x1D,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x14, 0x2D, 0x94, 0xD5, 0x02, 0x08, 0x00, 0x00, 0x04,
	0x9A, 0x15, 0x00, 0x8C, 0x19, 0x00, 0x80, 0x1E, 0x00, 0x77,
	0x23, 0x00, 0x70, 0x29, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22,
	0x21, 0x20, 0x1F, 0x1E, 0x1C, 0x1B, 0x19, 0x14, 0x13, 0x12,
	0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0A, 0x08, 0x07, 0x06,
	0x04, 0x02, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x12, 0x01
};

u8 gtp_dat_9_7[] = {
	/* <1536, 2048> 9.7 GT9110P_Config */
	0x00, 0x00, 0x06, 0x00, 0x08, 0x0A, 0x35, 0x00, 0x01, 0xC8,
	0x28, 0x08, 0x5A, 0x3C, 0x03, 0x05, 0x00, 0x00, 0xFF, 0x7F,
	0x00, 0x00, 0x04, 0x18, 0x1A, 0x1E, 0x14, 0x8F, 0x2F, 0xAA,
	0x2A, 0x2C, 0x1E, 0x14, 0x00, 0x00, 0x00, 0x21, 0x33, 0x2D,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x29, 0x19, 0x32, 0x94, 0xC5, 0x02, 0x08, 0x00, 0x00, 0x04,
	0x99, 0x1A, 0x00, 0x90, 0x1E, 0x00, 0x87, 0x23, 0x00, 0x81,
	0x28, 0x00, 0x7D, 0x2E, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x0F,
	0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19,
	0x46, 0x32, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C,
	0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
	0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1F,
	0x1E, 0x1D, 0x19, 0x18, 0x17, 0x16, 0x15, 0x12, 0x11, 0x10,
	0x0F, 0x0E, 0x09, 0x08, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x01
};

u8 gtp_dat_10_1[] = {
	/* <1200, 1920> 10.1 WGJ10187_GT9271_Config */
	0x41, 0xB0, 0x04, 0x80, 0x07, 0x0A, 0xF5, 0x00, 0x01, 0x08,
	0x28, 0x0F, 0x64, 0x32, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x16, 0x19, 0x1E, 0x14, 0x8F, 0x2F, 0x99,
	0x41, 0x43, 0x15, 0x0E, 0x00, 0x00, 0x00, 0x22, 0x03, 0x1D,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
	0x00, 0x2D, 0x62, 0x94, 0xC5, 0x02, 0x07, 0x17, 0x00, 0x04,
	0x92, 0x30, 0x00, 0x86, 0x39, 0x00, 0x7F, 0x42, 0x00, 0x79,
	0x4D, 0x00, 0x74, 0x5A, 0x00, 0x74, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x17, 0x16, 0x15, 0x14, 0x11, 0x10, 0x0F, 0x0E,
	0x0D, 0x0C, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x01, 0x00,
	0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x02, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0C,
	0x0D, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x29, 0x28, 0x27,
	0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1F, 0x1E, 0x1C,
	0x1B, 0x19, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x38, 0x01
};

u8 gtp_dat_7[] = {
	/* <1024, 600> 7.0 WGJ10187_GT910_Config */
	0x00, 0x00, 0x04, 0x58, 0x02, 0x05, 0x34, 0x20, 0x01, 0x1F,
	0x14, 0x0F, 0x5A, 0x46, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x17, 0x19, 0x1B, 0x14, 0x89, 0x08, 0x0A,
	0x3A, 0x00, 0x0F, 0x0A, 0x00, 0x00, 0x00, 0x1B, 0x02, 0x25,
	0x3C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x2A, 0x4E, 0x94, 0xC5, 0x02, 0x08, 0x00, 0x00, 0x04,
	0x92, 0x2C, 0x00, 0x88, 0x32, 0x00, 0x80, 0x39, 0x00, 0x7B,
	0x40, 0x00, 0x76, 0x49, 0x00, 0x76, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16,
	0x18, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x29, 0x28, 0x24, 0x22, 0x20, 0x1F, 0x1E, 0x1D,
	0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x05, 0x04, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99, 0x01
};

static u8 m89or101 = TRUE;
static u8 bgt911 = FALSE;
static u8 bgt9110 = FALSE;
static u8 bgt9111 = FALSE;
static u8 bgt9112 = FALSE;
static u8 bgt970 = FALSE;
static u8 bgt910 = FALSE;
static u8 gtp_change_x2y = TRUE;
static u8 gtp_x_reverse = FALSE;
static u8 gtp_y_reverse = TRUE;

static const char *goodix_ts_name = "goodix-ts";
static struct workqueue_struct *goodix_wq;
struct i2c_client *i2c_connect_client;
u8 config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH]
		= {GTP_REG_CONFIG_DATA >> 8, GTP_REG_CONFIG_DATA & 0xff};

#if GTP_HAVE_TOUCH_KEY
static const u16 touch_key_array[] = GTP_KEY_TAB;
#define GTP_MAX_KEY_NUM  ARRAY_SIZE(touch_key_array)

#if GTP_DEBUG_ON
static const int key_codes[] = {KEY_HOME, KEY_BACK, KEY_MENU, KEY_SEARCH};
static const char * const key_names[] = {"Key_Home", "Key_Back", "Key_Menu", "Key_Search"};
#endif

#endif

void gtp_reset_guitar(struct i2c_client *client, s32 ms);
s32 gtp_send_cfg(struct i2c_client *client);
void gtp_int_sync(s32 ms, struct goodix_ts_data *ts);

#if GTP_ESD_PROTECT
static struct delayed_work gtp_esd_check_work;
static struct workqueue_struct *gtp_esd_check_workqueue;
static void gtp_esd_check_func(struct work_struct *work);
static s32 gtp_init_ext_watchdog(struct i2c_client *client);
void gtp_esd_switch(struct i2c_client *client, s32 on);
#endif

#if GTP_GESTURE_WAKEUP
typedef enum {
	DOZE_DISABLED = 0,
	DOZE_ENABLED = 1,
	DOZE_WAKEUP = 2,
} DOZE_T;
static DOZE_T doze_status = DOZE_DISABLED;
static s8 gtp_enter_doze(struct goodix_ts_data *ts);
#endif

u8 grp_cfg_version;

/*******************************************************
 *Function:
 *	Read data from the i2c slave device.
 *Input:
 *	client:     i2c device.
 *	buf[0~1]:   read start address.
 *	buf[2~len-1]:   read data buffer.
 *	len:    GTP_ADDR_LENGTH + read bytes count
 *Output:
 *	numbers of i2c_msgs to transfer:
 *		2: succeed, otherwise: failed
 **********************************************************/
s32 gtp_i2c_read(struct i2c_client *client, u8 *buf, s32 len)
{
	struct i2c_msg msgs[2];
	s32 ret = -1;
	s32 retries = 0;

	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr  = client->addr;
	msgs[0].len   = GTP_ADDR_LENGTH;
	msgs[0].buf   = &buf[0];
	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = len - GTP_ADDR_LENGTH;
	msgs[1].buf   = &buf[GTP_ADDR_LENGTH];

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			break;
		retries++;
	}
	if (retries >= 5) {
	#if GTP_GESTURE_WAKEUP
		// reset chip would quit doze mode
		if (doze_status == DOZE_ENABLED)
			return ret;
	#endif
		dev_err(&client->dev, "I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);
		gtp_reset_guitar(client, 10);
	}
	return ret;
}

/*******************************************************
 *Function:
 *	Write data to the i2c slave device.
 *Input:
 *	client:     i2c device.
 *	buf[0~1]:   write start address.
 *	buf[2~len-1]:   data buffer
 *	len:    GTP_ADDR_LENGTH + write bytes count
 *Output:
 *	numbers of i2c_msgs to transfer:
 *		1: succeed, otherwise: failed
 **********************************************************/
s32 gtp_i2c_write(struct i2c_client *client, u8 *buf, s32 len)
{
	struct i2c_msg msg;
	s32 ret = -1;
	s32 retries = 0;

	msg.flags = !I2C_M_RD;
	msg.addr  = client->addr;
	msg.len   = len;
	msg.buf   = buf;

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			break;
		retries++;
	}

	if (retries >= 5) {
	#if GTP_GESTURE_WAKEUP
		if (doze_status == DOZE_ENABLED)
			return ret;
	#endif
		dev_err(&client->dev, "I2C Write: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);
		gtp_reset_guitar(client, 10);
	}
	return ret;
}

/*******************************************************
 *Function:
 *	i2c read twice, compare the results
 *Input:
 *	client:  i2c device
 *	addr:    operate address
 *	rxbuf:   read data to store, if compare successful
 *	len:     bytes to read
 *Output:
 *	0: succeed, otherwise: failed
 **********************************************************/
s32 gtp_i2c_read_dbl_check(struct i2c_client *client, u16 addr, u8 *rxbuf, int len)
{
	u8 buf[16] = {0};
	u8 confirm_buf[16] = {0};

	memset(buf, 0xAA, 16);
	buf[0] = (u8)(addr >> 8);
	buf[1] = (u8)(addr & 0xFF);
	gtp_i2c_read(client, buf, len + 2);

	memset(confirm_buf, 0xAB, 16);
	confirm_buf[0] = (u8)(addr >> 8);
	confirm_buf[1] = (u8)(addr & 0xFF);
	gtp_i2c_read(client, confirm_buf, len + 2);

	if (!memcmp(buf, confirm_buf, len + 2)) {
		memcpy(rxbuf, confirm_buf + 2, len);
		return 0;
	}

	return -EINVAL;
}

/*******************************************************
 *Function:
 *	Send config.
 *Input:
 *	client: i2c device.
 *Output:
 *	result of i2c write operation.
 *		1: succeed, otherwise: failed
 **********************************************************/

s32 gtp_send_cfg(struct i2c_client *client)
{
	s32 ret = 2;

#if GTP_DRIVER_SEND_CFG
	s32 retry = 0;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->fixed_cfg) {
		dev_info(&client->dev, "Ic fixed config, no config sent!");
		return 0;
	} else if (ts->pnl_init_error) {
		dev_info(&client->dev, "Error occured in init_panel, no config sent");
		return 0;
	}

	for (retry = 0; retry < 5; retry++) {
		ret = gtp_i2c_write(client, config, GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH);
		if (ret > 0)
			break;
	}
#endif
	return ret;
}
/*******************************************************
 *Function:
 *	Disable irq function
 *Input:
 *	ts: goodix i2c_client private data
 *Output:
 *	None.
 **********************************************************/
void gtp_irq_disable(struct goodix_ts_data *ts)
{
	unsigned long irqflags;

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (!ts->irq_is_disable) {
		ts->irq_is_disable = 1;
		disable_irq_nosync(ts->client->irq);
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}

/*******************************************************
 *Function:
 *	Enable irq function
 *Input:
 *	ts: goodix i2c_client private data
 *Output:
 *	None.
 **********************************************************/
void gtp_irq_enable(struct goodix_ts_data *ts)
{
	unsigned long irqflags = 0;

	spin_lock_irqsave(&ts->irq_lock, irqflags);
	if (ts->irq_is_disable) {
		enable_irq(ts->client->irq);
		ts->irq_is_disable = 0;
	}
	spin_unlock_irqrestore(&ts->irq_lock, irqflags);
}


/*******************************************************
 *Function:
 *	Report touch point event
 *Input:
 *	ts: goodix i2c_client private data
 *	id: trackId
 *	x:  input x coordinate
 *	y:  input y coordinate
 *	w:  input pressure
 *Output:
 *	None.
 **********************************************************/
static void gtp_touch_down(struct goodix_ts_data *ts, s32 id, s32 x, s32 y, s32 w)
{
	if (gtp_change_x2y)
		GTP_SWAP(x, y);

	if (!bgt911 && !bgt970) {
		if (gtp_x_reverse)
			x = ts->abs_x_max - x;

		if (gtp_y_reverse)
			y = ts->abs_y_max - y;
	}

	input_report_key(ts->input_dev, BTN_TOUCH, 1);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, w);
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(ts->input_dev);
}

/*******************************************************
 *Function:
 *	Report touch release event
 *Input:
 *	ts: goodix i2c_client private data
 *Output:
 *	None.
 **********************************************************/
static void gtp_touch_up(struct goodix_ts_data *ts, s32 id)
{
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
}

#if GTP_WITH_PEN

static void gtp_pen_init(struct goodix_ts_data *ts)
{
	s32 ret = 0;

	dev_info(&client->dev, ("Request input device for pen/stylus.");

	ts->pen_dev = input_allocate_device();
	if (ts->pen_dev == NULL) {
		dev_err(&client->dev, "Failed to allocate input device for pen/stylus.");
		return;
	}

	ts->pen_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	ts->pen_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	set_bit(BTN_TOOL_PEN, ts->pen_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, ts->pen_dev->propbit);
	//set_bit(INPUT_PROP_POINTER, ts->pen_dev->propbit);

#if GTP_PEN_HAVE_BUTTON
	input_set_capability(ts->pen_dev, EV_KEY, BTN_STYLUS);
	input_set_capability(ts->pen_dev, EV_KEY, BTN_STYLUS2);
#endif

	input_set_abs_params(ts->pen_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->pen_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->pen_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->pen_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->pen_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

	ts->pen_dev->name = "goodix-pen";
	ts->pen_dev->id.bustype = BUS_I2C;

	ret = input_register_device(ts->pen_dev);
	if (ret) {
		dev_err(&client->dev, "Register %s input device failed", ts->pen_dev->name);
		return;
	}
}

static void gtp_pen_down(s32 x, s32 y, s32 w, s32 id)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);

	if (gtp_change_x2y)
		GTP_SWAP(x, y);

	input_report_key(ts->pen_dev, BTN_TOOL_PEN, 1);
	input_report_key(ts->pen_dev, BTN_TOUCH, 1);
	input_report_abs(ts->pen_dev, ABS_MT_POSITION_X, x);
	input_report_abs(ts->pen_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(ts->pen_dev, ABS_MT_PRESSURE, w);
	input_report_abs(ts->pen_dev, ABS_MT_TOUCH_MAJOR, w);
	input_report_abs(ts->pen_dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(ts->pen_dev);
}

static void gtp_pen_up(s32 id)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(i2c_connect_client);

	input_report_key(ts->pen_dev, BTN_TOOL_PEN, 0);
	input_report_key(ts->pen_dev, BTN_TOUCH, 0);
}
#endif

/*******************************************************
 *Function:
 *	Goodix touchscreen work function
 *Input:
 *	work: work struct of goodix_workqueue
 *Output:
 *	None.
 **********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
	u8 end_cmd[3] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF, 0};
	u8 point_data[2 + 1 + 8 * GTP_MAX_TOUCH + 1] = {GTP_READ_COOR_ADDR >> 8, GTP_READ_COOR_ADDR & 0xFF};
	u8 touch_num = 0;
	u8 finger = 0;
	static u16 pre_touch;
	static u8 pre_key;
#if GTP_WITH_PEN
	u8 pen_active = 0;
	static u8 pre_pen;
#endif
	u8 key_value = 0;
	u8 *coor_data = NULL;
	s32 input_x = 0;
	s32 input_y = 0;
	s32 input_w = 0;
	s32 id = 0;
	s32 i = 0;
	s32 ret = -1;
	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	struct i2c_client *client = ts->client;

#if GTP_GESTURE_WAKEUP
	u8 doze_buf[3] = {0x81, 0x4B};

	if (doze_status == DOZE_ENABLED) {
		ret = gtp_i2c_read(i2c_connect_client, doze_buf, 3);
		if (ret > 0) {
			if ((doze_buf[2] == 'a') || (doze_buf[2] == 'b') || (doze_buf[2] == 'c') ||
				(doze_buf[2] == 'd') || (doze_buf[2] == 'e') || (doze_buf[2] == 'g') ||
				(doze_buf[2] == 'h') || (doze_buf[2] == 'm') || (doze_buf[2] == 'o') ||
				(doze_buf[2] == 'q') || (doze_buf[2] == 's') || (doze_buf[2] == 'v') ||
				(doze_buf[2] == 'w') || (doze_buf[2] == 'y') || (doze_buf[2] == 'z') ||
				(doze_buf[2] == 0x5E) /* ^ */
				) {

				if (doze_buf[2] != 0x5E)
					dev_info(&client->dev, "Wakeup by gesture(%c), light up the screen!", doze_buf[2]);
				else
					dev_info(&client->dev, "Wakeup by gesture(^), light up the screen!");

				doze_status = DOZE_WAKEUP;
				input_report_key(ts->input_dev, KEY_POWER, 1);
				input_sync(ts->input_dev);
				input_report_key(ts->input_dev, KEY_POWER, 0);
				input_sync(ts->input_dev);
				// clear 0x814B
				doze_buf[2] = 0x00;
				gtp_i2c_write(i2c_connect_client, doze_buf, 3);
			} else if ((doze_buf[2] == 0xAA) || (doze_buf[2] == 0xBB) ||
				(doze_buf[2] == 0xAB) || (doze_buf[2] == 0xBA)) {
				char *direction[4] = {"Right", "Down", "Up", "Left"};
				u8 type = ((doze_buf[2] & 0x0F) - 0x0A) + (((doze_buf[2] >> 4) & 0x0F) - 0x0A) * 2;

				dev_info(&client->dev, "%s slide to light up the screen!", direction[type]);
				doze_status = DOZE_WAKEUP;
				input_report_key(ts->input_dev, KEY_POWER, 1);
				input_sync(ts->input_dev);
				input_report_key(ts->input_dev, KEY_POWER, 0);
				input_sync(ts->input_dev);
				// clear 0x814B
				doze_buf[2] = 0x00;
				gtp_i2c_write(i2c_connect_client, doze_buf, 3);
			} else if (doze_buf[2] == 0xCC) {
				dev_info(&client->dev, "Double click to light up the screen!");
				doze_status = DOZE_WAKEUP;
				input_report_key(ts->input_dev, KEY_POWER, 1);
				input_sync(ts->input_dev);
				input_report_key(ts->input_dev, KEY_POWER, 0);
				input_sync(ts->input_dev);
				// clear 0x814B
				doze_buf[2] = 0x00;
				gtp_i2c_write(i2c_connect_client, doze_buf, 3);
			} else {
				// clear 0x814B
				doze_buf[2] = 0x00;
				gtp_i2c_write(i2c_connect_client, doze_buf, 3);
				gtp_enter_doze(ts);
			}
		}

		gtp_irq_enable(ts);
		goto release_wakeup_lock;
}
#endif

	ret = gtp_i2c_read(ts->client, point_data, 12);
	if (ret < 0) {
		dev_err(&client->dev, "I2C transfer error. errno:%d\n ", ret);
		gtp_irq_enable(ts);
		goto release_wakeup_lock;
	}

	finger = point_data[GTP_ADDR_LENGTH];

	if (finger == 0x00) {
		gtp_irq_enable(ts);
		goto release_wakeup_lock;
	}

	if (finger & 0x80 == 0)
		goto exit_work_func;

	touch_num = finger & 0x0f;
	if (touch_num > GTP_MAX_TOUCH)
		goto exit_work_func;

	if (touch_num > 1) {
		u8 buf[8 * GTP_MAX_TOUCH] = {(GTP_READ_COOR_ADDR + 10) >> 8, (GTP_READ_COOR_ADDR + 10) & 0xff};

		ret = gtp_i2c_read(ts->client, buf, 2 + 8 * (touch_num - 1));
		memcpy(&point_data[12], &buf[2], 8 * (touch_num - 1));
	}

#if (GTP_HAVE_TOUCH_KEY || GTP_PEN_HAVE_BUTTON)
	key_value = point_data[3 + 8 * touch_num];

	if (key_value || pre_key) {
	#if GTP_PEN_HAVE_BUTTON
		if (key_value == 0x40) {
			/* BTN_STYLUS & BTN_STYLUS2 Down. */
			input_report_key(ts->pen_dev, BTN_STYLUS, 1);
			input_report_key(ts->pen_dev, BTN_STYLUS2, 1);
			pen_active = 1;
		} else if (key_value == 0x10) {
			/* BTN_STYLUS Down, BTN_STYLUS2 Up. */
			input_report_key(ts->pen_dev, BTN_STYLUS, 1);
			input_report_key(ts->pen_dev, BTN_STYLUS2, 0);
			pen_active = 1;
		} else if (key_value == 0x20) {
			/* BTN_STYLUS Up, BTN_STYLUS2 Down. */
			input_report_key(ts->pen_dev, BTN_STYLUS, 0);
			input_report_key(ts->pen_dev, BTN_STYLUS2, 1);
			pen_active = 1;
		} else {
			/* BTN_STYLUS & BTN_STYLUS2 Up. */
			input_report_key(ts->pen_dev, BTN_STYLUS, 0);
			input_report_key(ts->pen_dev, BTN_STYLUS2, 0);
			if ((pre_key == 0x40) || (pre_key == 0x20) ||
				(pre_key == 0x10)) {
				pen_active = 1;
			}
		}

		if (pen_active)
			touch_num = 0;      // shield pen point
	#endif

	#if GTP_HAVE_TOUCH_KEY
		if (!pre_touch) {
			for (i = 0; i < GTP_MAX_KEY_NUM; i++) {
			#if GTP_DEBUG_ON
				for (ret = 0; ret < 4; ++ret) {
					if (key_codes[ret] == touch_key_array[i]) {
						dev_dbg(&client->dev, "Key: %s %s", key_names[ret], (key_value & (0x01 << i)) ? "Down" : "Up");
						break;
					}
				}
			#endif
				input_report_key(ts->input_dev, touch_key_array[i], key_value & (0x01<<i));
			}
			touch_num = 0;  // shield fingers
		}
	#endif
	}
#endif
	pre_key = key_value;

	if (touch_num) {
		for (i = 0; i < touch_num; i++) {
			coor_data = &point_data[i * 8 + 3];

			id = coor_data[0] & 0x0F;
			input_x  = coor_data[1] | (coor_data[2] << 8);
			input_y  = coor_data[3] | (coor_data[4] << 8);
			input_w  = coor_data[5] | (coor_data[6] << 8);

		#if GTP_WITH_PEN
			id = coor_data[0];
			if (id & 0x80)
			{
				/* Pen touch DOWN! */
				gtp_pen_down(input_x, input_y, input_w, 0);
				pre_pen = 1;
				pen_active = 1;
				break;
			}
			else
		#endif
			{
				gtp_touch_down(ts, id, input_x, input_y, input_w);
			}
		}
	} else if (pre_touch) {
	#if GTP_WITH_PEN
		if (pre_pen == 1)
		{
			/* Pen touch UP! */
			gtp_pen_up(0);
			pre_pen = 0;
			pen_active = 1;
		}
		else
	#endif
		{
			/* Touch Release! */
			gtp_touch_up(ts, 0);
		}
	}

	pre_touch = touch_num;

#if GTP_WITH_PEN
	if (pen_active)
	{
		pen_active = 0;
		input_sync(ts->pen_dev);
	}
	else
#endif
	{
		input_sync(ts->input_dev);
	}

exit_work_func:
	gtp_i2c_write(ts->client, end_cmd, 3);
	gtp_irq_enable(ts);

release_wakeup_lock:
	if (device_can_wakeup(&ts->client->dev))
		pm_relax(&ts->client->dev);
}

/*******************************************************
 *Function:
 *	External interrupt service routine for interrupt mode.
 *Input:
 *	irq:  interrupt number.
 *	dev_id: private data pointer
 *Output:
 *	Handle Result.
 *		IRQ_HANDLED: interrupt handled successfully
 **********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	gtp_irq_disable(ts);

	if (device_can_wakeup(&ts->client->dev))
		pm_stay_awake(&ts->client->dev);

	queue_work(goodix_wq, &ts->work);

	return IRQ_HANDLED;
}

/*******************************************************
 *Function:
 *	Synchronization.
 *Input:
 *	ms: synchronization time in millisecond.
 *Output:
 *	None.
 ********************************************************/
void gtp_int_sync(s32 ms, struct goodix_ts_data *ts)
{
	GTP_GPIO_OUTPUT(ts->irq_pin, 0);
	msleep(ms);
	gpio_direction_input(ts->irq_pin);
}


/*******************************************************
 *Function:
 *	Reset chip.
 *Input:
 *	ms: reset time in millisecond
 *Output:
 *	None.
 ********************************************************/
void gtp_reset_guitar(struct i2c_client *client, s32 ms)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	GTP_GPIO_OUTPUT(ts->rst_pin, 0);   // begin select I2C slave addr
	msleep(ms);                         // T2: > 10ms
	// HIGH: 0x28/0x29, LOW: 0xBA/0xBB
	GTP_GPIO_OUTPUT(ts->irq_pin, client->addr == 0x14);

	msleep(2);                          // T3: > 100us
	GTP_GPIO_OUTPUT(ts->rst_pin, 1);

	msleep(6);                          // T4: > 5ms

	gpio_direction_input(ts->rst_pin);

	gtp_int_sync(50, ts);
#if GTP_ESD_PROTECT
	gtp_init_ext_watchdog(client);
#endif
}

#if GTP_GESTURE_WAKEUP
/*******************************************************
 *Function:
 *	Enter doze mode for sliding wakeup.
 *Input:
 *	ts: goodix tp private data
 *Output:
 *	1: succeed, otherwise failed
 ********************************************************/
static s8 gtp_enter_doze(struct goodix_ts_data *ts)
{
	s8 ret = -1;
	s8 retry = 0;
	u8 i2c_control_buf[3] = {(u8)(GTP_REG_SLEEP >> 8), (u8)GTP_REG_SLEEP, 8};

	while (retry++ < 5) {
		i2c_control_buf[0] = 0x80;
		i2c_control_buf[1] = 0x46;
		ret = gtp_i2c_write(ts->client, i2c_control_buf, 3);
		if (ret < 0)
			continue;

		i2c_control_buf[0] = 0x80;
		i2c_control_buf[1] = 0x40;
		ret = gtp_i2c_write(ts->client, i2c_control_buf, 3);
		if (ret > 0) {
			doze_status = DOZE_ENABLED;
			return ret;
		}
		msleep(10);
	}

	return ret;
}
#endif

#if GTP_DRIVER_SEND_CFG
static s32 gtp_get_info(struct goodix_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	u8 opr_buf[6] = {0};
	s32 ret = 0;

	//ts->abs_x_max = GTP_MAX_WIDTH;
	//ts->abs_y_max = GTP_MAX_HEIGHT;

	ts->int_trigger_type = GTP_INT_TRIGGER;

	opr_buf[0] = (u8)((GTP_REG_CONFIG_DATA+1) >> 8);
	opr_buf[1] = (u8)((GTP_REG_CONFIG_DATA+1) & 0xFF);

	ret = gtp_i2c_read(ts->client, opr_buf, 6);
	if (ret < 0)
		return FAIL;

	ts->abs_x_max = (opr_buf[3] << 8) + opr_buf[2];
	ts->abs_y_max = (opr_buf[5] << 8) + opr_buf[4];

	opr_buf[0] = (u8)((GTP_REG_CONFIG_DATA+6) >> 8);
	opr_buf[1] = (u8)((GTP_REG_CONFIG_DATA+6) & 0xFF);

	ret = gtp_i2c_read(ts->client, opr_buf, 3);
	if (ret < 0)
		return FAIL;

	ts->int_trigger_type = opr_buf[2] & 0x03;

	dev_info(&client->dev, "X_MAX = %d, Y_MAX = %d, TRIGGER = 0x%02x",
			ts->abs_x_max, ts->abs_y_max, ts->int_trigger_type);

	return SUCCESS;
}
#endif

/*******************************************************
 *Function:
 *	Initialize gtp.
 *Input:
 *	ts: goodix private data
 *Output:
 *	Executive outcomes.
 *		0: succeed, otherwise: failed
 ********************************************************/
static s32 gtp_init_panel(struct goodix_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	s32 ret = -1;
#if GTP_DRIVER_SEND_CFG
	s32 i = 0;
	u8 check_sum = 0;
	u8 opr_buf[16] = {0};
	u8 sensor_id = 0;

	u8 cfg_info_group2[] = CTP_CFG_GROUP2;
	u8 cfg_info_group3[] = CTP_CFG_GROUP3;
	u8 cfg_info_group4[] = CTP_CFG_GROUP4;
	u8 cfg_info_group5[] = CTP_CFG_GROUP5;
	u8 cfg_info_group6[] = CTP_CFG_GROUP6;
	u8 *send_cfg_buf[] = { gtp_dat_10_1, cfg_info_group2, cfg_info_group3,
						cfg_info_group4, cfg_info_group5, cfg_info_group6 };
	u8 cfg_info_len[] = { CFG_GROUP_LEN(gtp_dat_10_1),
						CFG_GROUP_LEN(cfg_info_group2),
						CFG_GROUP_LEN(cfg_info_group3),
						CFG_GROUP_LEN(cfg_info_group4),
						CFG_GROUP_LEN(cfg_info_group5),
						CFG_GROUP_LEN(cfg_info_group6)};

	if (m89or101) {
		if (ts->cfg_file_num) {
			send_cfg_buf[0] = gtp_dat_8_9_1;
			cfg_info_len[0] =  CFG_GROUP_LEN(gtp_dat_8_9_1);
		} else {
			send_cfg_buf[0] = gtp_dat_8_9;
			cfg_info_len[0] =  CFG_GROUP_LEN(gtp_dat_8_9);
		}
	}

	if (bgt911) {
		send_cfg_buf[0] = gtp_dat_gt11;
		cfg_info_len[0] =  CFG_GROUP_LEN(gtp_dat_gt11);
	}

	if (bgt9110) {
		send_cfg_buf[0] = gtp_dat_gt9110;
		cfg_info_len[0] =  CFG_GROUP_LEN(gtp_dat_gt9110);
	}

	if (bgt9111) {
		send_cfg_buf[0] = gtp_dat_gt9111;
		cfg_info_len[0] =  CFG_GROUP_LEN(gtp_dat_gt9111);
	}

	if (bgt9112) {
		send_cfg_buf[0] = gtp_dat_gt9112;
		cfg_info_len[0] = CFG_GROUP_LEN(gtp_dat_gt9112);
	}

	if (bgt970) {
		send_cfg_buf[0] = gtp_dat_9_7;
		cfg_info_len[0] = CFG_GROUP_LEN(gtp_dat_9_7);
	}

	if (bgt910) {
		send_cfg_buf[0] = gtp_dat_7;
		cfg_info_len[0] = CFG_GROUP_LEN(gtp_dat_7);
	}

	if ((!cfg_info_len[1]) && (!cfg_info_len[2]) &&
		(!cfg_info_len[3]) && (!cfg_info_len[4]) &&
		(!cfg_info_len[5])) {
		sensor_id = 0;
	} else {
		msleep(50);
		ret = gtp_i2c_read_dbl_check(ts->client, GTP_REG_SENSOR_ID, &sensor_id, 1);
		if (ret < 0 || sensor_id >= 0x06) {
			ts->pnl_init_error = 1;
			return -1;
		}
	}
	ts->gtp_cfg_len = cfg_info_len[sensor_id];

	if (ts->gtp_cfg_len < GTP_CONFIG_MIN_LENGTH) {
		ts->pnl_init_error = 1;
		return -1;
	}

	ret = gtp_i2c_read_dbl_check(ts->client, GTP_REG_CONFIG_DATA, &opr_buf[0], 1);
	if (ret < 0)
		return -1;

	if (opr_buf[0] < 90) {
		grp_cfg_version = send_cfg_buf[sensor_id][0];       // backup group config version
		send_cfg_buf[sensor_id][0] = 0x00;
		ts->fixed_cfg = 0;
	} else {       // treated as fixed config, not send config
		ts->fixed_cfg = 1;
		gtp_get_info(ts);
		return 0;
	}

	memset(&config[GTP_ADDR_LENGTH], 0, GTP_CONFIG_MAX_LENGTH);
	memcpy(&config[GTP_ADDR_LENGTH], send_cfg_buf[sensor_id], ts->gtp_cfg_len);

	check_sum = 0;
	for (i = GTP_ADDR_LENGTH; i < ts->gtp_cfg_len; i++)
		check_sum += config[i];

	config[ts->gtp_cfg_len] = (~check_sum) + 1;

#else // driver not send config

	ts->gtp_cfg_len = GTP_CONFIG_MAX_LENGTH;
	ret = gtp_i2c_read(ts->client, config, ts->gtp_cfg_len + GTP_ADDR_LENGTH);
	if (ret < 0) {
		dev_err(&client->dev, "Read Config Failed, Using Default Resolution & INT Trigger!");
		//ts->abs_x_max = GTP_MAX_WIDTH;
		//ts->abs_y_max = GTP_MAX_HEIGHT;
		ts->int_trigger_type = GTP_INT_TRIGGER;
	}

#endif // GTP_DRIVER_SEND_CFG

	if ((ts->abs_x_max == 0) && (ts->abs_y_max == 0)) {
		ts->abs_x_max = (config[RESOLUTION_LOC + 1] << 8) + config[RESOLUTION_LOC];
		ts->abs_y_max = (config[RESOLUTION_LOC + 3] << 8) + config[RESOLUTION_LOC + 2];
		ts->int_trigger_type = (config[TRIGGER_LOC]) & 0x03;
	}

#if GTP_DRIVER_SEND_CFG
	ret = gtp_send_cfg(ts->client);
	if (ret < 0)
		dev_err(&client->dev, "Send config error.");

	// set config version to CTP_CFG_GROUP, for resume to send config
	config[GTP_ADDR_LENGTH] = grp_cfg_version;
	check_sum = 0;
	for (i = GTP_ADDR_LENGTH; i < ts->gtp_cfg_len; i++)
		check_sum += config[i];

	config[ts->gtp_cfg_len] = (~check_sum) + 1;
#endif
	return 0;
}

/*******************************************************
 *Function:
 *	Read chip version.
 *Input:
 *	client:  i2c device
 *	version: buffer to keep ic firmware version
 *Output:
 *	read operation return.
 *		2: succeed, otherwise: failed
 ********************************************************/
s32 gtp_read_version(struct i2c_client *client, u16 *version)
{
	s32 ret = -1;
	u8 buf[8] = {GTP_REG_VERSION >> 8, GTP_REG_VERSION & 0xff};

	ret = gtp_i2c_read(client, buf, sizeof(buf));
	if (ret < 0) {
		dev_err(&client->dev, "GTP read version failed");
		return ret;
	}

	if (version)
		*version = (buf[7] << 8) | buf[6];

	if (buf[5] == 0x00)
		dev_info(&client->dev, "IC Version: %c%c%c_%02x%02x",
				buf[2], buf[3], buf[4], buf[7], buf[6]);
	else
		dev_info(&client->dev, "IC Version: %c%c%c%c_%02x%02x",
				buf[2], buf[3], buf[4], buf[5], buf[7], buf[6]);

	return ret;
}

/*******************************************************
 *Function:
 *	Request gpio(INT & RST) ports.
 *Input:
 *	ts: private data.
 *Output:
 *	Executive outcomes.
 *		>= 0: succeed, < 0: failed
 ********************************************************/
static s8 gtp_request_io_port(struct goodix_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	s32 ret = 0;

	ret = GTP_GPIO_REQUEST(ts->rst_pin, "GTP_RST_PORT");
	if (ret < 0) {
		dev_err(&client->dev, "2Failed to request GPIO:%d, ERRNO:%d",
				(s32)ts->rst_pin, ret);
		GTP_GPIO_FREE(ts->rst_pin);
		return -ENODEV;
	}

	ret = GTP_GPIO_REQUEST(ts->irq_pin, "GTP_INT_IRQ");
	if (ret < 0) {
		dev_err(&client->dev, "3Failed to request GPIO:%d, ERRNO:%d",
				(s32)ts->irq_pin, ret);
		GTP_GPIO_FREE(ts->irq_pin);
		return -ENODEV;
	}

	gpio_direction_input(ts->irq_pin);
	gpio_direction_input(ts->rst_pin);

	gtp_reset_guitar(ts->client, 20);

	return ret;
}

/*******************************************************
 *Function:
 *	Request interrupt.
 *Input:
 *	ts: private data.
 *Output:
 *	Executive outcomes.
 *		0: succeed, -1: failed.
 ********************************************************/
static s8 gtp_request_irq(struct goodix_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	s32 ret;

	ts->irq = gpio_to_irq(ts->irq_pin);       //If not defined in client
	if (ts->irq < 0) {
		dev_err(&client->dev, "ts->irq error\n");
		ret = -EINVAL;
		goto err;
	}

	ts->client->irq = ts->irq;
	ret = devm_request_threaded_irq(&client->dev, ts->irq, NULL,
		goodix_ts_irq_handler, ts->irq_flags | IRQF_ONESHOT /*irq_table[ts->int_trigger_type]*/,
		ts->client->name, ts);
	if (ret != 0) {
		dev_err(&client->dev, "Cannot allocate ts INT! ERRNO:%d\n", ret);
		goto err;
	}

	return 0;
err:
	return ret;
}

/*******************************************************
 *Function:
 *	Request input device Function.
 *Input:
 *	ts:private data.
 *Output:
 *	Executive outcomes.
 *		0: succeed, otherwise: failed.
 ********************************************************/
static s8 gtp_request_input_dev(struct i2c_client *client,
				struct goodix_ts_data *ts)
{
	s8 ret = -1;
	s8 phys[32];
#if GTP_HAVE_TOUCH_KEY
	u8 index = 0;
#endif

	ts->input_dev = devm_input_allocate_device(&client->dev);
	if (ts->input_dev == NULL) {
		dev_err(&client->dev, "Failed to allocate input device.");
		return -ENOMEM;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);

#if GTP_HAVE_TOUCH_KEY
	for (index = 0; index < GTP_MAX_KEY_NUM; index++)
		input_set_capability(ts->input_dev, EV_KEY, touch_key_array[index]);
#endif

#if GTP_GESTURE_WAKEUP
	input_set_capability(ts->input_dev, EV_KEY, KEY_POWER);
#endif

	if (gtp_change_x2y)
		GTP_SWAP(ts->abs_x_max, ts->abs_y_max);

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);

	sprintf(phys, "input/ts");
	ts->input_dev->name = goodix_ts_name;
	ts->input_dev->phys = phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev, "Register %s input device failed", ts->input_dev->name);
		return -ENODEV;
	}

#if GTP_WITH_PEN
	gtp_pen_init(ts);
#endif

	return 0;
}

int gtp_get_chip_type(struct goodix_ts_data *ts)
{
	struct i2c_client *client = ts->client;
	u8 opr_buf[10] = {0x00};
	s32 ret = 0;

	ret = gtp_i2c_read_dbl_check(ts->client, GTP_REG_CHIP_TYPE, opr_buf, 10);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get chip-type");
		return -EINVAL;
	}

	if (memcmp(opr_buf, "GOODIX_GT9", 10))
		return -ENODEV;

	dev_info(&client->dev, "Chip Type: %s", "GOODIX_GT9");
	return 0;
}

/*******************************************************
 *Function:
 *	I2c probe.
 *Input:
 *	client: i2c device struct.
 *	id: device id.
 *Output:
 *	Executive outcomes.
 *		0: succeed.
 ********************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	s32 ret = -1;
	struct goodix_ts_data *ts;
	u16 version_info;
	struct device_node *np;
	enum of_gpio_flags rst_flags, pwr_flags;
	u32 val;

	dev_info(&client->dev, "GTP Driver Version: %s", GTP_DRIVER_VERSION);

	i2c_connect_client = client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C check functionality failed.");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL)
		return -ENOMEM;

	np = client->dev.of_node;
	if (!np) {
		dev_err(&client->dev, "No device tree node\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "tp-size", &val)) {
		dev_err(&client->dev, "no max-x defined\n");
		return -EINVAL;
	}

	if (val == 89) {
		m89or101 = TRUE;
		gtp_change_x2y = TRUE;
		gtp_x_reverse = FALSE;
		gtp_y_reverse = TRUE;
	} else if (val == 101) {
		m89or101 = FALSE;
		gtp_change_x2y = TRUE;
		gtp_x_reverse = TRUE;
		gtp_y_reverse = FALSE;
	} else if (val == 911) {
		m89or101 = FALSE;
		bgt911 = TRUE;
		gtp_change_x2y = TRUE;
		gtp_x_reverse = FALSE;
		gtp_y_reverse = TRUE;
	} else if (val == 9110) {
		m89or101 = FALSE;
		bgt9110 = TRUE;
		gtp_change_x2y = TRUE;
		gtp_x_reverse = TRUE;
		gtp_y_reverse = FALSE;
	} else if (val == 9111) {
		m89or101 = FALSE;
		bgt9111 = TRUE;
		gtp_change_x2y = FALSE;
		gtp_x_reverse = TRUE;
		gtp_y_reverse = FALSE;
	} else if (val == 9112) {
		m89or101 = FALSE;
		bgt9112 = TRUE;
		gtp_change_x2y = FALSE;
		gtp_x_reverse = FALSE;
		gtp_y_reverse = FALSE;
	} else if (val == 970) {
		m89or101 = FALSE;
		bgt911 = FALSE;
		bgt970 = TRUE;
		gtp_change_x2y = FALSE;
		gtp_x_reverse = FALSE;
		gtp_y_reverse = TRUE;
	} else if (val == 910) {
		m89or101 = FALSE;
		bgt911 = FALSE;
		bgt970 = FALSE;
		bgt910 = TRUE;
		gtp_change_x2y = TRUE;
		gtp_x_reverse = FALSE;
		gtp_y_reverse = TRUE;
	}

	if (of_property_read_u32(np, "max-x", &val)) {
		dev_err(&client->dev, "no max-x defined\n");
		return -EINVAL;
	}
	//ts->abs_x_max = val;
	if (of_property_read_u32(np, "max-y", &val)) {
		dev_err(&client->dev, "no max-y defined\n");
		return -EINVAL;
	}
	//ts->abs_y_max = val;
	if (of_property_read_u32(np, "configfile-num", &val))
		ts->cfg_file_num = 0;
	else
		ts->cfg_file_num = val;

	ts->tp_regulator = devm_regulator_get(&client->dev, "tp");
	if (IS_ERR(ts->tp_regulator)) {
		dev_err(&client->dev, "failed to get regulator, %ld\n",
			PTR_ERR(ts->tp_regulator));
		return PTR_ERR(ts->tp_regulator);
	}

	ret = regulator_enable(ts->tp_regulator);
	if (ret < 0)
		dev_err(&client->dev, "failed to enable tp regulator\n");

	msleep(20);

	ts->irq_pin = of_get_named_gpio_flags(np, "touch-gpio", 0, (enum of_gpio_flags *)(&ts->irq_flags));
	ts->rst_pin = of_get_named_gpio_flags(np, "reset-gpio", 0, &rst_flags);
	ts->pwr_pin = of_get_named_gpio_flags(np, "power-gpio", 0, &pwr_flags);

	ts->client = client;

	INIT_WORK(&ts->work, goodix_ts_work_func);
	spin_lock_init(&ts->irq_lock);          // 2.6.39 later
#if GTP_ESD_PROTECT
	ts->clk_tick_cnt = 2 * HZ;      // HZ: clock ticks in 1 second generated by system
	spin_lock_init(&ts->esd_lock);
#endif

	i2c_set_clientdata(client, ts);

	ret = gtp_request_io_port(ts);
	if (ret < 0) {
		dev_err(&client->dev, "GTP request IO port failed.");
		goto probe_init_error_requireio;
	}

	ret = gtp_get_chip_type(ts);
	if (ret < 0) {
		dev_err(&client->dev, "Get chip type failed.");
		goto probe_init_error;
	}

	ret = gtp_read_version(client, &version_info);
	if (ret < 0)
		dev_err(&client->dev, "Read version failed.");

	ret = gtp_init_panel(ts);
	if (ret < 0) {
		dev_err(&client->dev, "GTP init panel failed.");
		//ts->abs_x_max = GTP_MAX_WIDTH;
		//ts->abs_y_max = GTP_MAX_HEIGHT;
		ts->int_trigger_type = GTP_INT_TRIGGER;
	}

	ts->irq_flags = ts->int_trigger_type ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	ret = gtp_request_input_dev(client, ts);
	if (ret < 0) {
		dev_err(&client->dev, "GTP request input dev failed");
		goto probe_init_error;
	}

	ret = gtp_request_irq(ts);
	if (ret < 0)
		dev_info(&client->dev, "GTP works in polling mode.");
	else
		dev_info(&client->dev, "GTP works in interrupt mode.");

	gtp_irq_enable(ts);

	if (of_property_read_bool(np, "wakeup-source")) {
		device_init_wakeup(&client->dev, 1);
		enable_irq_wake(ts->irq);
	}

#if GTP_ESD_PROTECT
	gtp_esd_switch(client, SWITCH_ON);
#endif
	return 0;

probe_init_error:
	GTP_GPIO_FREE(ts->rst_pin);
	GTP_GPIO_FREE(ts->irq_pin);
probe_init_error_requireio:
	regulator_disable(ts->tp_regulator);
	kfree(ts);
	return ret;
}


/*******************************************************
 *Function:
 *	Goodix touchscreen driver release function.
 *Input:
 *	client: i2c device struct.
 *Output:
 *	Executive outcomes. 0---succeed.
 ********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

#if GTP_ESD_PROTECT
	destroy_workqueue(gtp_esd_check_workqueue);
#endif

	regulator_disable(ts->tp_regulator);

	if (ts) {
		gpio_direction_input(ts->irq_pin);
		GTP_GPIO_FREE(ts->irq_pin);
		free_irq(client->irq, ts);
		i2c_set_clientdata(client, NULL);
		input_unregister_device(ts->input_dev);
		kfree(ts);
	}

	return 0;
}

#if GTP_ESD_PROTECT
s32 gtp_i2c_read_no_rst(struct i2c_client *client, u8 *buf, s32 len)
{
	struct i2c_msg msgs[2];
	s32 ret = -1;
	s32 retries = 0;

	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr  = client->addr;
	msgs[0].len   = GTP_ADDR_LENGTH;
	msgs[0].buf   = &buf[0];
	msgs[1].flags = I2C_M_RD;
	msgs[1].addr  = client->addr;
	msgs[1].len   = len - GTP_ADDR_LENGTH;
	msgs[1].buf   = &buf[GTP_ADDR_LENGTH];

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			break;
		retries++;
	}
	if (retries >= 5)
		dev_err(&client->dev, "I2C Read: 0x%04X, %d bytes failed, errcode: %d!", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);

	return ret;
}

s32 gtp_i2c_write_no_rst(struct i2c_client *client, u8 *buf, s32 len)
{
	struct i2c_msg msg;
	s32 ret = -1;
	s32 retries = 0;

	msg.flags = !I2C_M_RD;
	msg.addr  = client->addr;
	msg.len   = len;
	msg.buf   = buf;

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			break;
		retries++;
	}
	if (retries >= 5)
		dev_err(&client->dev, "I2C Write: 0x%04X, %d bytes failed, errcode: %d!", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);

	return ret;
}
/*******************************************************
 *Function:
 *	switch on & off esd delayed work
 *Input:
 *	client:  i2c device
 *	on:  SWITCH_ON / SWITCH_OFF
 *Output:
 *	void
 **********************************************************/
void gtp_esd_switch(struct i2c_client *client, s32 on)
{
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(client);
	spin_lock(&ts->esd_lock);

	if (on == SWITCH_ON) {     // switch on esd
		if (!ts->esd_running) {
			ts->esd_running = 1;
			spin_unlock(&ts->esd_lock);
			queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, ts->clk_tick_cnt);
		} else {
			spin_unlock(&ts->esd_lock);
		}
	} else {   // switch off esd
		if (ts->esd_running) {
			ts->esd_running = 0;
			spin_unlock(&ts->esd_lock);
			cancel_delayed_work_sync(&gtp_esd_check_work);
		} else {
			spin_unlock(&ts->esd_lock);
		}
	}
}

/*******************************************************
 *Function:
 *	Initialize external watchdog for esd protect
 *Input:
 *	client:  i2c device.
 *Output:
 *	result of i2c write operation.
 *		1: succeed, otherwise: failed
 **********************************************************/
static s32 gtp_init_ext_watchdog(struct i2c_client *client)
{
	u8 opr_buffer[3] = {0x80, 0x41, 0xAA};

	return gtp_i2c_write_no_rst(client, opr_buffer, 3);
}

/*******************************************************
 *Function:
 *	Esd protect function.
 *	External watchdog added by meta, 2013/03/07
 *Input:
 *	work: delayed work
 *Output:
 *	None.
 ********************************************************/
static void gtp_esd_check_func(struct work_struct *work)
{
	s32 i;
	s32 ret = -1;
	struct goodix_ts_data *ts = NULL;
	u8 esd_buf[5] = {0x80, 0x40};

	ts = i2c_get_clientdata(i2c_connect_client);

	if (ts->gtp_is_suspend)
		return;

	for (i = 0; i < 3; i++) {
		ret = gtp_i2c_read_no_rst(ts->client, esd_buf, 4);
		if (ret < 0)
			continue;

		if ((esd_buf[2] == 0xAA) || (esd_buf[3] != 0xAA)) {
			// IC works abnormally..
			u8 chk_buf[4] = {0x80, 0x40};

			gtp_i2c_read_no_rst(ts->client, chk_buf, 4);
			if ((chk_buf[2] == 0xAA) || (chk_buf[3] != 0xAA)) {
				i = 3;
				break;
			}
		} else {
			// IC works normally, Write 0x8040 0xAA, feed the dog
			esd_buf[2] = 0xAA;
			gtp_i2c_write_no_rst(ts->client, esd_buf, 3);
			break;
		}
	}

	if (i >= 3) {
		/* IC working abnormally! Process reset guitar. */
		esd_buf[0] = 0x42;
		esd_buf[1] = 0x26;
		esd_buf[2] = 0x01;
		esd_buf[3] = 0x01;
		esd_buf[4] = 0x01;
		gtp_i2c_write_no_rst(ts->client, esd_buf, 5);
		msleep(50);
		gtp_reset_guitar(ts->client, 50);
		msleep(50);
		gtp_send_cfg(ts->client);
	}

	queue_delayed_work(gtp_esd_check_workqueue, &gtp_esd_check_work, ts->clk_tick_cnt);
}
#endif

static const struct i2c_device_id goodix_ts_id[] = {
	{ GTP_I2C_NAME, 0 },
	{ }
};

static const struct of_device_id goodix_ts_dt_ids[] = {
	{ .compatible = "goodix,gt9xx" },
	{ }
};

static struct i2c_driver goodix_ts_driver = {
	.probe      = goodix_ts_probe,
	.remove     = goodix_ts_remove,
	.id_table   = goodix_ts_id,
	.driver = {
		.name     = GTP_I2C_NAME,
		.of_match_table = of_match_ptr(goodix_ts_dt_ids),
	},
};

/*******************************************************
 *Function:
 *	Driver Install function.
 *Input:
 *	None.
 *Output:
 *	Executive Outcomes. 0---succeed.
 *********************************************************/
static int goodix_ts_init(void)
{
	goodix_wq = create_singlethread_workqueue("goodix_wq");
	if (!goodix_wq)
		return -ENOMEM;

#if GTP_ESD_PROTECT
	INIT_DELAYED_WORK(&gtp_esd_check_work, gtp_esd_check_func);
	gtp_esd_check_workqueue = create_workqueue("gtp_esd_check");
#endif
	return i2c_add_driver(&goodix_ts_driver);

}

/*******************************************************
 *Function:
 *	Driver uninstall function.
 *Input:
 *	None.
 *Output:
 *	Executive Outcomes. 0---succeed.
 ********************************************************/
static void goodix_ts_exit(void)
{
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);
}

module_init(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("GTP Series Driver");
MODULE_LICENSE("GPL");
