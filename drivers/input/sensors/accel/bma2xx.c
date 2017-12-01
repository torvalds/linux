/*
 * drivers/input/sensors/accel/bma2xx.c
 *
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *
 * Author: Bin Yang <yangbin@rock - chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

#define BMA2X2_RANGE_SET		3  /* +/ -  2G */
#define BMA2X2_BW_SET			12 /* 125HZ  */

#define LOW_G_INTERRUPT				REL_Z
#define HIGH_G_INTERRUPT			REL_HWHEEL
#define SLOP_INTERRUPT				REL_DIAL
#define DOUBLE_TAP_INTERRUPT			REL_WHEEL
#define SINGLE_TAP_INTERRUPT			REL_MISC
#define ORIENT_INTERRUPT			ABS_PRESSURE
#define FLAT_INTERRUPT				ABS_DISTANCE
#define SLOW_NO_MOTION_INTERRUPT		REL_Y

#define HIGH_G_INTERRUPT_X_HAPPENED			1
#define HIGH_G_INTERRUPT_Y_HAPPENED			2
#define HIGH_G_INTERRUPT_Z_HAPPENED			3
#define HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED		4
#define HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED		5
#define HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED		6
#define SLOPE_INTERRUPT_X_HAPPENED			7
#define SLOPE_INTERRUPT_Y_HAPPENED			8
#define SLOPE_INTERRUPT_Z_HAPPENED			9
#define SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED		10
#define SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED		11
#define SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED		12
#define DOUBLE_TAP_INTERRUPT_HAPPENED			13
#define SINGLE_TAP_INTERRUPT_HAPPENED			14
#define UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED		15
#define UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED		16
#define UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED	17
#define UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED	18
#define DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED	19
#define DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED	20
#define DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED	21
#define DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED	22
#define FLAT_INTERRUPT_TRUE_HAPPENED			23
#define FLAT_INTERRUPT_FALSE_HAPPENED			24
#define LOW_G_INTERRUPT_HAPPENED			25
#define SLOW_NO_MOTION_INTERRUPT_HAPPENED		26

#define PAD_LOWG					0
#define PAD_HIGHG					1
#define PAD_SLOP					2
#define PAD_DOUBLE_TAP					3
#define PAD_SINGLE_TAP					4
#define PAD_ORIENT					5
#define PAD_FLAT					6
#define PAD_SLOW_NO_MOTION				7

#define BMA2X2_CHIP_ID_REG                      0x00
#define BMA2X2_VERSION_REG                      0x01
#define BMA2X2_X_AXIS_LSB_REG                   0x02
#define BMA2X2_X_AXIS_MSB_REG                   0x03
#define BMA2X2_Y_AXIS_LSB_REG                   0x04
#define BMA2X2_Y_AXIS_MSB_REG                   0x05
#define BMA2X2_Z_AXIS_LSB_REG                   0x06
#define BMA2X2_Z_AXIS_MSB_REG                   0x07
#define BMA2X2_TEMPERATURE_REG                  0x08
#define BMA2X2_STATUS1_REG                      0x09
#define BMA2X2_STATUS2_REG                      0x0A
#define BMA2X2_STATUS_TAP_SLOPE_REG             0x0B
#define BMA2X2_STATUS_ORIENT_HIGH_REG           0x0C
#define BMA2X2_STATUS_FIFO_REG                  0x0E
#define BMA2X2_RANGE_SEL_REG                    0x0F
#define BMA2X2_BW_SEL_REG                       0x10
#define BMA2X2_MODE_CTRL_REG                    0x11
#define BMA2X2_LOW_NOISE_CTRL_REG               0x12
#define BMA2X2_DATA_CTRL_REG                    0x13
#define BMA2X2_RESET_REG                        0x14
#define BMA2X2_INT_ENABLE1_REG                  0x16
#define BMA2X2_INT_ENABLE2_REG                  0x17
#define BMA2X2_INT_SLO_NO_MOT_REG               0x18
#define BMA2X2_INT1_PAD_SEL_REG                 0x19
#define BMA2X2_INT_DATA_SEL_REG                 0x1A
#define BMA2X2_INT2_PAD_SEL_REG                 0x1B
#define BMA2X2_INT_SRC_REG                      0x1E
#define BMA2X2_INT_SET_REG                      0x20
#define BMA2X2_INT_CTRL_REG                     0x21
#define BMA2X2_LOW_DURN_REG                     0x22
#define BMA2X2_LOW_THRES_REG                    0x23
#define BMA2X2_LOW_HIGH_HYST_REG                0x24
#define BMA2X2_HIGH_DURN_REG                    0x25
#define BMA2X2_HIGH_THRES_REG                   0x26
#define BMA2X2_SLOPE_DURN_REG                   0x27
#define BMA2X2_SLOPE_THRES_REG                  0x28
#define BMA2X2_SLO_NO_MOT_THRES_REG             0x29
#define BMA2X2_TAP_PARAM_REG                    0x2A
#define BMA2X2_TAP_THRES_REG                    0x2B
#define BMA2X2_ORIENT_PARAM_REG                 0x2C
#define BMA2X2_THETA_BLOCK_REG                  0x2D
#define BMA2X2_THETA_FLAT_REG                   0x2E
#define BMA2X2_FLAT_HOLD_TIME_REG               0x2F
#define BMA2X2_FIFO_WML_TRIG                    0x30
#define BMA2X2_SELF_TEST_REG                    0x32
#define BMA2X2_EEPROM_CTRL_REG                  0x33
#define BMA2X2_SERIAL_CTRL_REG                  0x34
#define BMA2X2_EXTMODE_CTRL_REG                 0x35
#define BMA2X2_OFFSET_CTRL_REG                  0x36
#define BMA2X2_OFFSET_PARAMS_REG                0x37
#define BMA2X2_OFFSET_X_AXIS_REG                0x38
#define BMA2X2_OFFSET_Y_AXIS_REG                0x39
#define BMA2X2_OFFSET_Z_AXIS_REG                0x3A
#define BMA2X2_GP0_REG                          0x3B
#define BMA2X2_GP1_REG                          0x3C
#define BMA2X2_FIFO_MODE_REG                    0x3E
#define BMA2X2_FIFO_DATA_OUTPUT_REG             0x3F

#define BMA2X2_CHIP_ID__POS             0
#define BMA2X2_CHIP_ID__MSK             0xFF
#define BMA2X2_CHIP_ID__LEN             8
#define BMA2X2_CHIP_ID__REG             BMA2X2_CHIP_ID_REG

#define BMA2X2_VERSION__POS          0
#define BMA2X2_VERSION__LEN          8
#define BMA2X2_VERSION__MSK          0xFF
#define BMA2X2_VERSION__REG          BMA2X2_VERSION_REG

#define BMA2x2_SLO_NO_MOT_DUR__POS	2
#define BMA2x2_SLO_NO_MOT_DUR__LEN	6
#define BMA2x2_SLO_NO_MOT_DUR__MSK	0xFC
#define BMA2x2_SLO_NO_MOT_DUR__REG	BMA2X2_SLOPE_DURN_REG

#define BMA2X2_NEW_DATA_X__POS          0
#define BMA2X2_NEW_DATA_X__LEN          1
#define BMA2X2_NEW_DATA_X__MSK          0x01
#define BMA2X2_NEW_DATA_X__REG          BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X14_LSB__POS           2
#define BMA2X2_ACC_X14_LSB__LEN           6
#define BMA2X2_ACC_X14_LSB__MSK           0xFC
#define BMA2X2_ACC_X14_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X12_LSB__POS           4
#define BMA2X2_ACC_X12_LSB__LEN           4
#define BMA2X2_ACC_X12_LSB__MSK           0xF0
#define BMA2X2_ACC_X12_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X10_LSB__POS           6
#define BMA2X2_ACC_X10_LSB__LEN           2
#define BMA2X2_ACC_X10_LSB__MSK           0xC0
#define BMA2X2_ACC_X10_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X8_LSB__POS           0
#define BMA2X2_ACC_X8_LSB__LEN           0
#define BMA2X2_ACC_X8_LSB__MSK           0x00
#define BMA2X2_ACC_X8_LSB__REG           BMA2X2_X_AXIS_LSB_REG

#define BMA2X2_ACC_X_MSB__POS           0
#define BMA2X2_ACC_X_MSB__LEN           8
#define BMA2X2_ACC_X_MSB__MSK           0xFF
#define BMA2X2_ACC_X_MSB__REG           BMA2X2_X_AXIS_MSB_REG

#define BMA2X2_NEW_DATA_Y__POS          0
#define BMA2X2_NEW_DATA_Y__LEN          1
#define BMA2X2_NEW_DATA_Y__MSK          0x01
#define BMA2X2_NEW_DATA_Y__REG          BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y14_LSB__POS           2
#define BMA2X2_ACC_Y14_LSB__LEN           6
#define BMA2X2_ACC_Y14_LSB__MSK           0xFC
#define BMA2X2_ACC_Y14_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y12_LSB__POS           4
#define BMA2X2_ACC_Y12_LSB__LEN           4
#define BMA2X2_ACC_Y12_LSB__MSK           0xF0
#define BMA2X2_ACC_Y12_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y10_LSB__POS           6
#define BMA2X2_ACC_Y10_LSB__LEN           2
#define BMA2X2_ACC_Y10_LSB__MSK           0xC0
#define BMA2X2_ACC_Y10_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y8_LSB__POS           0
#define BMA2X2_ACC_Y8_LSB__LEN           0
#define BMA2X2_ACC_Y8_LSB__MSK           0x00
#define BMA2X2_ACC_Y8_LSB__REG           BMA2X2_Y_AXIS_LSB_REG

#define BMA2X2_ACC_Y_MSB__POS           0
#define BMA2X2_ACC_Y_MSB__LEN           8
#define BMA2X2_ACC_Y_MSB__MSK           0xFF
#define BMA2X2_ACC_Y_MSB__REG           BMA2X2_Y_AXIS_MSB_REG

#define BMA2X2_NEW_DATA_Z__POS          0
#define BMA2X2_NEW_DATA_Z__LEN          1
#define BMA2X2_NEW_DATA_Z__MSK          0x01
#define BMA2X2_NEW_DATA_Z__REG          BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z14_LSB__POS           2
#define BMA2X2_ACC_Z14_LSB__LEN           6
#define BMA2X2_ACC_Z14_LSB__MSK           0xFC
#define BMA2X2_ACC_Z14_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z12_LSB__POS           4
#define BMA2X2_ACC_Z12_LSB__LEN           4
#define BMA2X2_ACC_Z12_LSB__MSK           0xF0
#define BMA2X2_ACC_Z12_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z10_LSB__POS           6
#define BMA2X2_ACC_Z10_LSB__LEN           2
#define BMA2X2_ACC_Z10_LSB__MSK           0xC0
#define BMA2X2_ACC_Z10_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z8_LSB__POS           0
#define BMA2X2_ACC_Z8_LSB__LEN           0
#define BMA2X2_ACC_Z8_LSB__MSK           0x00
#define BMA2X2_ACC_Z8_LSB__REG           BMA2X2_Z_AXIS_LSB_REG

#define BMA2X2_ACC_Z_MSB__POS           0
#define BMA2X2_ACC_Z_MSB__LEN           8
#define BMA2X2_ACC_Z_MSB__MSK           0xFF
#define BMA2X2_ACC_Z_MSB__REG           BMA2X2_Z_AXIS_MSB_REG

#define BMA2X2_TEMPERATURE__POS         0
#define BMA2X2_TEMPERATURE__LEN         8
#define BMA2X2_TEMPERATURE__MSK         0xFF
#define BMA2X2_TEMPERATURE__REG         BMA2X2_TEMP_RD_REG

#define BMA2X2_LOWG_INT_S__POS          0
#define BMA2X2_LOWG_INT_S__LEN          1
#define BMA2X2_LOWG_INT_S__MSK          0x01
#define BMA2X2_LOWG_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_HIGHG_INT_S__POS          1
#define BMA2X2_HIGHG_INT_S__LEN          1
#define BMA2X2_HIGHG_INT_S__MSK          0x02
#define BMA2X2_HIGHG_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_SLOPE_INT_S__POS          2
#define BMA2X2_SLOPE_INT_S__LEN          1
#define BMA2X2_SLOPE_INT_S__MSK          0x04
#define BMA2X2_SLOPE_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_SLO_NO_MOT_INT_S__POS          3
#define BMA2X2_SLO_NO_MOT_INT_S__LEN          1
#define BMA2X2_SLO_NO_MOT_INT_S__MSK          0x08
#define BMA2X2_SLO_NO_MOT_INT_S__REG          BMA2X2_STATUS1_REG

#define BMA2X2_DOUBLE_TAP_INT_S__POS     4
#define BMA2X2_DOUBLE_TAP_INT_S__LEN     1
#define BMA2X2_DOUBLE_TAP_INT_S__MSK     0x10
#define BMA2X2_DOUBLE_TAP_INT_S__REG     BMA2X2_STATUS1_REG

#define BMA2X2_SINGLE_TAP_INT_S__POS     5
#define BMA2X2_SINGLE_TAP_INT_S__LEN     1
#define BMA2X2_SINGLE_TAP_INT_S__MSK     0x20
#define BMA2X2_SINGLE_TAP_INT_S__REG     BMA2X2_STATUS1_REG

#define BMA2X2_ORIENT_INT_S__POS         6
#define BMA2X2_ORIENT_INT_S__LEN         1
#define BMA2X2_ORIENT_INT_S__MSK         0x40
#define BMA2X2_ORIENT_INT_S__REG         BMA2X2_STATUS1_REG

#define BMA2X2_FLAT_INT_S__POS           7
#define BMA2X2_FLAT_INT_S__LEN           1
#define BMA2X2_FLAT_INT_S__MSK           0x80
#define BMA2X2_FLAT_INT_S__REG           BMA2X2_STATUS1_REG

#define BMA2X2_FIFO_FULL_INT_S__POS           5
#define BMA2X2_FIFO_FULL_INT_S__LEN           1
#define BMA2X2_FIFO_FULL_INT_S__MSK           0x20
#define BMA2X2_FIFO_FULL_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_FIFO_WM_INT_S__POS           6
#define BMA2X2_FIFO_WM_INT_S__LEN           1
#define BMA2X2_FIFO_WM_INT_S__MSK           0x40
#define BMA2X2_FIFO_WM_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_DATA_INT_S__POS           7
#define BMA2X2_DATA_INT_S__LEN           1
#define BMA2X2_DATA_INT_S__MSK           0x80
#define BMA2X2_DATA_INT_S__REG           BMA2X2_STATUS2_REG

#define BMA2X2_SLOPE_FIRST_X__POS        0
#define BMA2X2_SLOPE_FIRST_X__LEN        1
#define BMA2X2_SLOPE_FIRST_X__MSK        0x01
#define BMA2X2_SLOPE_FIRST_X__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_FIRST_Y__POS        1
#define BMA2X2_SLOPE_FIRST_Y__LEN        1
#define BMA2X2_SLOPE_FIRST_Y__MSK        0x02
#define BMA2X2_SLOPE_FIRST_Y__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_FIRST_Z__POS        2
#define BMA2X2_SLOPE_FIRST_Z__LEN        1
#define BMA2X2_SLOPE_FIRST_Z__MSK        0x04
#define BMA2X2_SLOPE_FIRST_Z__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_SLOPE_SIGN_S__POS         3
#define BMA2X2_SLOPE_SIGN_S__LEN         1
#define BMA2X2_SLOPE_SIGN_S__MSK         0x08
#define BMA2X2_SLOPE_SIGN_S__REG         BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_X__POS        4
#define BMA2X2_TAP_FIRST_X__LEN        1
#define BMA2X2_TAP_FIRST_X__MSK        0x10
#define BMA2X2_TAP_FIRST_X__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_Y__POS        5
#define BMA2X2_TAP_FIRST_Y__LEN        1
#define BMA2X2_TAP_FIRST_Y__MSK        0x20
#define BMA2X2_TAP_FIRST_Y__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_FIRST_Z__POS        6
#define BMA2X2_TAP_FIRST_Z__LEN        1
#define BMA2X2_TAP_FIRST_Z__MSK        0x40
#define BMA2X2_TAP_FIRST_Z__REG        BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_TAP_SIGN_S__POS         7
#define BMA2X2_TAP_SIGN_S__LEN         1
#define BMA2X2_TAP_SIGN_S__MSK         0x80
#define BMA2X2_TAP_SIGN_S__REG         BMA2X2_STATUS_TAP_SLOPE_REG

#define BMA2X2_HIGHG_FIRST_X__POS        0
#define BMA2X2_HIGHG_FIRST_X__LEN        1
#define BMA2X2_HIGHG_FIRST_X__MSK        0x01
#define BMA2X2_HIGHG_FIRST_X__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_FIRST_Y__POS        1
#define BMA2X2_HIGHG_FIRST_Y__LEN        1
#define BMA2X2_HIGHG_FIRST_Y__MSK        0x02
#define BMA2X2_HIGHG_FIRST_Y__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_FIRST_Z__POS        2
#define BMA2X2_HIGHG_FIRST_Z__LEN        1
#define BMA2X2_HIGHG_FIRST_Z__MSK        0x04
#define BMA2X2_HIGHG_FIRST_Z__REG        BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_HIGHG_SIGN_S__POS         3
#define BMA2X2_HIGHG_SIGN_S__LEN         1
#define BMA2X2_HIGHG_SIGN_S__MSK         0x08
#define BMA2X2_HIGHG_SIGN_S__REG         BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_ORIENT_S__POS             4
#define BMA2X2_ORIENT_S__LEN             3
#define BMA2X2_ORIENT_S__MSK             0x70
#define BMA2X2_ORIENT_S__REG             BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_FLAT_S__POS               7
#define BMA2X2_FLAT_S__LEN               1
#define BMA2X2_FLAT_S__MSK               0x80
#define BMA2X2_FLAT_S__REG               BMA2X2_STATUS_ORIENT_HIGH_REG

#define BMA2X2_FIFO_FRAME_COUNTER_S__POS             0
#define BMA2X2_FIFO_FRAME_COUNTER_S__LEN             7
#define BMA2X2_FIFO_FRAME_COUNTER_S__MSK             0x7F
#define BMA2X2_FIFO_FRAME_COUNTER_S__REG             BMA2X2_STATUS_FIFO_REG

#define BMA2X2_FIFO_OVERRUN_S__POS             7
#define BMA2X2_FIFO_OVERRUN_S__LEN             1
#define BMA2X2_FIFO_OVERRUN_S__MSK             0x80
#define BMA2X2_FIFO_OVERRUN_S__REG             BMA2X2_STATUS_FIFO_REG

#define BMA2X2_RANGE_SEL__POS             0
#define BMA2X2_RANGE_SEL__LEN             4
#define BMA2X2_RANGE_SEL__MSK             0x0F
#define BMA2X2_RANGE_SEL__REG             BMA2X2_RANGE_SEL_REG

#define BMA2X2_BANDWIDTH__POS             0
#define BMA2X2_BANDWIDTH__LEN             5
#define BMA2X2_BANDWIDTH__MSK             0x1F
#define BMA2X2_BANDWIDTH__REG             BMA2X2_BW_SEL_REG

#define BMA2X2_SLEEP_DUR__POS             1
#define BMA2X2_SLEEP_DUR__LEN             4
#define BMA2X2_SLEEP_DUR__MSK             0x1E
#define BMA2X2_SLEEP_DUR__REG             BMA2X2_MODE_CTRL_REG

#define BMA2X2_MODE_CTRL__POS             5
#define BMA2X2_MODE_CTRL__LEN             3
#define BMA2X2_MODE_CTRL__MSK             0xE0
#define BMA2X2_MODE_CTRL__REG             BMA2X2_MODE_CTRL_REG

#define BMA2X2_DEEP_SUSPEND__POS          5
#define BMA2X2_DEEP_SUSPEND__LEN          1
#define BMA2X2_DEEP_SUSPEND__MSK          0x20
#define BMA2X2_DEEP_SUSPEND__REG          BMA2X2_MODE_CTRL_REG

#define BMA2X2_EN_LOW_POWER__POS          6
#define BMA2X2_EN_LOW_POWER__LEN          1
#define BMA2X2_EN_LOW_POWER__MSK          0x40
#define BMA2X2_EN_LOW_POWER__REG          BMA2X2_MODE_CTRL_REG

#define BMA2X2_EN_SUSPEND__POS            7
#define BMA2X2_EN_SUSPEND__LEN            1
#define BMA2X2_EN_SUSPEND__MSK            0x80
#define BMA2X2_EN_SUSPEND__REG            BMA2X2_MODE_CTRL_REG

#define BMA2X2_SLEEP_TIMER__POS          5
#define BMA2X2_SLEEP_TIMER__LEN          1
#define BMA2X2_SLEEP_TIMER__MSK          0x20
#define BMA2X2_SLEEP_TIMER__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_LOW_POWER_MODE__POS          6
#define BMA2X2_LOW_POWER_MODE__LEN          1
#define BMA2X2_LOW_POWER_MODE__MSK          0x40
#define BMA2X2_LOW_POWER_MODE__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_EN_LOW_NOISE__POS          7
#define BMA2X2_EN_LOW_NOISE__LEN          1
#define BMA2X2_EN_LOW_NOISE__MSK          0x80
#define BMA2X2_EN_LOW_NOISE__REG          BMA2X2_LOW_NOISE_CTRL_REG

#define BMA2X2_DIS_SHADOW_PROC__POS       6
#define BMA2X2_DIS_SHADOW_PROC__LEN       1
#define BMA2X2_DIS_SHADOW_PROC__MSK       0x40
#define BMA2X2_DIS_SHADOW_PROC__REG       BMA2X2_DATA_CTRL_REG

#define BMA2X2_EN_DATA_HIGH_BW__POS         7
#define BMA2X2_EN_DATA_HIGH_BW__LEN         1
#define BMA2X2_EN_DATA_HIGH_BW__MSK         0x80
#define BMA2X2_EN_DATA_HIGH_BW__REG         BMA2X2_DATA_CTRL_REG

#define BMA2X2_EN_SOFT_RESET__POS         0
#define BMA2X2_EN_SOFT_RESET__LEN         8
#define BMA2X2_EN_SOFT_RESET__MSK         0xFF
#define BMA2X2_EN_SOFT_RESET__REG         BMA2X2_RESET_REG

#define BMA2X2_EN_SOFT_RESET_VALUE        0xB6

#define BMA2X2_EN_SLOPE_X_INT__POS         0
#define BMA2X2_EN_SLOPE_X_INT__LEN         1
#define BMA2X2_EN_SLOPE_X_INT__MSK         0x01
#define BMA2X2_EN_SLOPE_X_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SLOPE_Y_INT__POS         1
#define BMA2X2_EN_SLOPE_Y_INT__LEN         1
#define BMA2X2_EN_SLOPE_Y_INT__MSK         0x02
#define BMA2X2_EN_SLOPE_Y_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SLOPE_Z_INT__POS         2
#define BMA2X2_EN_SLOPE_Z_INT__LEN         1
#define BMA2X2_EN_SLOPE_Z_INT__MSK         0x04
#define BMA2X2_EN_SLOPE_Z_INT__REG         BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_DOUBLE_TAP_INT__POS      4
#define BMA2X2_EN_DOUBLE_TAP_INT__LEN      1
#define BMA2X2_EN_DOUBLE_TAP_INT__MSK      0x10
#define BMA2X2_EN_DOUBLE_TAP_INT__REG      BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_SINGLE_TAP_INT__POS      5
#define BMA2X2_EN_SINGLE_TAP_INT__LEN      1
#define BMA2X2_EN_SINGLE_TAP_INT__MSK      0x20
#define BMA2X2_EN_SINGLE_TAP_INT__REG      BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_ORIENT_INT__POS          6
#define BMA2X2_EN_ORIENT_INT__LEN          1
#define BMA2X2_EN_ORIENT_INT__MSK          0x40
#define BMA2X2_EN_ORIENT_INT__REG          BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_FLAT_INT__POS            7
#define BMA2X2_EN_FLAT_INT__LEN            1
#define BMA2X2_EN_FLAT_INT__MSK            0x80
#define BMA2X2_EN_FLAT_INT__REG            BMA2X2_INT_ENABLE1_REG

#define BMA2X2_EN_HIGHG_X_INT__POS         0
#define BMA2X2_EN_HIGHG_X_INT__LEN         1
#define BMA2X2_EN_HIGHG_X_INT__MSK         0x01
#define BMA2X2_EN_HIGHG_X_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_HIGHG_Y_INT__POS         1
#define BMA2X2_EN_HIGHG_Y_INT__LEN         1
#define BMA2X2_EN_HIGHG_Y_INT__MSK         0x02
#define BMA2X2_EN_HIGHG_Y_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_HIGHG_Z_INT__POS         2
#define BMA2X2_EN_HIGHG_Z_INT__LEN         1
#define BMA2X2_EN_HIGHG_Z_INT__MSK         0x04
#define BMA2X2_EN_HIGHG_Z_INT__REG         BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_LOWG_INT__POS            3
#define BMA2X2_EN_LOWG_INT__LEN            1
#define BMA2X2_EN_LOWG_INT__MSK            0x08
#define BMA2X2_EN_LOWG_INT__REG            BMA2X2_INT_ENABLE2_REG

#define BMA2X2_EN_NEW_DATA_INT__POS        4
#define BMA2X2_EN_NEW_DATA_INT__LEN        1
#define BMA2X2_EN_NEW_DATA_INT__MSK        0x10
#define BMA2X2_EN_NEW_DATA_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_FFULL_EN_INT__POS        5
#define BMA2X2_INT_FFULL_EN_INT__LEN        1
#define BMA2X2_INT_FFULL_EN_INT__MSK        0x20
#define BMA2X2_INT_FFULL_EN_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_FWM_EN_INT__POS        6
#define BMA2X2_INT_FWM_EN_INT__LEN        1
#define BMA2X2_INT_FWM_EN_INT__MSK        0x40
#define BMA2X2_INT_FWM_EN_INT__REG        BMA2X2_INT_ENABLE2_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__POS        0
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__MSK        0x01
#define BMA2X2_INT_SLO_NO_MOT_EN_X_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__POS        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__MSK        0x02
#define BMA2X2_INT_SLO_NO_MOT_EN_Y_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__POS        2
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__MSK        0x04
#define BMA2X2_INT_SLO_NO_MOT_EN_Z_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__POS        3
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__LEN        1
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__MSK        0x08
#define BMA2X2_INT_SLO_NO_MOT_EN_SEL_INT__REG        BMA2X2_INT_SLO_NO_MOT_REG

#define BMA2X2_EN_INT1_PAD_LOWG__POS        0
#define BMA2X2_EN_INT1_PAD_LOWG__LEN        1
#define BMA2X2_EN_INT1_PAD_LOWG__MSK        0x01
#define BMA2X2_EN_INT1_PAD_LOWG__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_HIGHG__POS       1
#define BMA2X2_EN_INT1_PAD_HIGHG__LEN       1
#define BMA2X2_EN_INT1_PAD_HIGHG__MSK       0x02
#define BMA2X2_EN_INT1_PAD_HIGHG__REG       BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SLOPE__POS       2
#define BMA2X2_EN_INT1_PAD_SLOPE__LEN       1
#define BMA2X2_EN_INT1_PAD_SLOPE__MSK       0x04
#define BMA2X2_EN_INT1_PAD_SLOPE__REG       BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__POS        3
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__LEN        1
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__MSK        0x08
#define BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_DB_TAP__POS      4
#define BMA2X2_EN_INT1_PAD_DB_TAP__LEN      1
#define BMA2X2_EN_INT1_PAD_DB_TAP__MSK      0x10
#define BMA2X2_EN_INT1_PAD_DB_TAP__REG      BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_SNG_TAP__POS     5
#define BMA2X2_EN_INT1_PAD_SNG_TAP__LEN     1
#define BMA2X2_EN_INT1_PAD_SNG_TAP__MSK     0x20
#define BMA2X2_EN_INT1_PAD_SNG_TAP__REG     BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_ORIENT__POS      6
#define BMA2X2_EN_INT1_PAD_ORIENT__LEN      1
#define BMA2X2_EN_INT1_PAD_ORIENT__MSK      0x40
#define BMA2X2_EN_INT1_PAD_ORIENT__REG      BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_FLAT__POS        7
#define BMA2X2_EN_INT1_PAD_FLAT__LEN        1
#define BMA2X2_EN_INT1_PAD_FLAT__MSK        0x80
#define BMA2X2_EN_INT1_PAD_FLAT__REG        BMA2X2_INT1_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_LOWG__POS        0
#define BMA2X2_EN_INT2_PAD_LOWG__LEN        1
#define BMA2X2_EN_INT2_PAD_LOWG__MSK        0x01
#define BMA2X2_EN_INT2_PAD_LOWG__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_HIGHG__POS       1
#define BMA2X2_EN_INT2_PAD_HIGHG__LEN       1
#define BMA2X2_EN_INT2_PAD_HIGHG__MSK       0x02
#define BMA2X2_EN_INT2_PAD_HIGHG__REG       BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SLOPE__POS       2
#define BMA2X2_EN_INT2_PAD_SLOPE__LEN       1
#define BMA2X2_EN_INT2_PAD_SLOPE__MSK       0x04
#define BMA2X2_EN_INT2_PAD_SLOPE__REG       BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__POS        3
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__LEN        1
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__MSK        0x08
#define BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_DB_TAP__POS      4
#define BMA2X2_EN_INT2_PAD_DB_TAP__LEN      1
#define BMA2X2_EN_INT2_PAD_DB_TAP__MSK      0x10
#define BMA2X2_EN_INT2_PAD_DB_TAP__REG      BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_SNG_TAP__POS     5
#define BMA2X2_EN_INT2_PAD_SNG_TAP__LEN     1
#define BMA2X2_EN_INT2_PAD_SNG_TAP__MSK     0x20
#define BMA2X2_EN_INT2_PAD_SNG_TAP__REG     BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_ORIENT__POS      6
#define BMA2X2_EN_INT2_PAD_ORIENT__LEN      1
#define BMA2X2_EN_INT2_PAD_ORIENT__MSK      0x40
#define BMA2X2_EN_INT2_PAD_ORIENT__REG      BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT2_PAD_FLAT__POS        7
#define BMA2X2_EN_INT2_PAD_FLAT__LEN        1
#define BMA2X2_EN_INT2_PAD_FLAT__MSK        0x80
#define BMA2X2_EN_INT2_PAD_FLAT__REG        BMA2X2_INT2_PAD_SEL_REG

#define BMA2X2_EN_INT1_PAD_NEWDATA__POS     0
#define BMA2X2_EN_INT1_PAD_NEWDATA__LEN     1
#define BMA2X2_EN_INT1_PAD_NEWDATA__MSK     0x01
#define BMA2X2_EN_INT1_PAD_NEWDATA__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT1_PAD_FWM__POS     1
#define BMA2X2_EN_INT1_PAD_FWM__LEN     1
#define BMA2X2_EN_INT1_PAD_FWM__MSK     0x02
#define BMA2X2_EN_INT1_PAD_FWM__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT1_PAD_FFULL__POS     2
#define BMA2X2_EN_INT1_PAD_FFULL__LEN     1
#define BMA2X2_EN_INT1_PAD_FFULL__MSK     0x04
#define BMA2X2_EN_INT1_PAD_FFULL__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_FFULL__POS     5
#define BMA2X2_EN_INT2_PAD_FFULL__LEN     1
#define BMA2X2_EN_INT2_PAD_FFULL__MSK     0x20
#define BMA2X2_EN_INT2_PAD_FFULL__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_FWM__POS     6
#define BMA2X2_EN_INT2_PAD_FWM__LEN     1
#define BMA2X2_EN_INT2_PAD_FWM__MSK     0x40
#define BMA2X2_EN_INT2_PAD_FWM__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_EN_INT2_PAD_NEWDATA__POS     7
#define BMA2X2_EN_INT2_PAD_NEWDATA__LEN     1
#define BMA2X2_EN_INT2_PAD_NEWDATA__MSK     0x80
#define BMA2X2_EN_INT2_PAD_NEWDATA__REG     BMA2X2_INT_DATA_SEL_REG

#define BMA2X2_UNFILT_INT_SRC_LOWG__POS        0
#define BMA2X2_UNFILT_INT_SRC_LOWG__LEN        1
#define BMA2X2_UNFILT_INT_SRC_LOWG__MSK        0x01
#define BMA2X2_UNFILT_INT_SRC_LOWG__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_HIGHG__POS       1
#define BMA2X2_UNFILT_INT_SRC_HIGHG__LEN       1
#define BMA2X2_UNFILT_INT_SRC_HIGHG__MSK       0x02
#define BMA2X2_UNFILT_INT_SRC_HIGHG__REG       BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_SLOPE__POS       2
#define BMA2X2_UNFILT_INT_SRC_SLOPE__LEN       1
#define BMA2X2_UNFILT_INT_SRC_SLOPE__MSK       0x04
#define BMA2X2_UNFILT_INT_SRC_SLOPE__REG       BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__POS        3
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__LEN        1
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__MSK        0x08
#define BMA2X2_UNFILT_INT_SRC_SLO_NO_MOT__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_TAP__POS         4
#define BMA2X2_UNFILT_INT_SRC_TAP__LEN         1
#define BMA2X2_UNFILT_INT_SRC_TAP__MSK         0x10
#define BMA2X2_UNFILT_INT_SRC_TAP__REG         BMA2X2_INT_SRC_REG

#define BMA2X2_UNFILT_INT_SRC_DATA__POS        5
#define BMA2X2_UNFILT_INT_SRC_DATA__LEN        1
#define BMA2X2_UNFILT_INT_SRC_DATA__MSK        0x20
#define BMA2X2_UNFILT_INT_SRC_DATA__REG        BMA2X2_INT_SRC_REG

#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__POS       0
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__LEN       1
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__MSK       0x01
#define BMA2X2_INT1_PAD_ACTIVE_LEVEL__REG       BMA2X2_INT_SET_REG

#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__POS       2
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__LEN       1
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__MSK       0x04
#define BMA2X2_INT2_PAD_ACTIVE_LEVEL__REG       BMA2X2_INT_SET_REG

#define BMA2X2_INT1_PAD_OUTPUT_TYPE__POS        1
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__LEN        1
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__MSK        0x02
#define BMA2X2_INT1_PAD_OUTPUT_TYPE__REG        BMA2X2_INT_SET_REG

#define BMA2X2_INT2_PAD_OUTPUT_TYPE__POS        3
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__LEN        1
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__MSK        0x08
#define BMA2X2_INT2_PAD_OUTPUT_TYPE__REG        BMA2X2_INT_SET_REG

#define BMA2X2_INT_MODE_SEL__POS                0
#define BMA2X2_INT_MODE_SEL__LEN                4
#define BMA2X2_INT_MODE_SEL__MSK                0x0F
#define BMA2X2_INT_MODE_SEL__REG                BMA2X2_INT_CTRL_REG

#define BMA2X2_RESET_INT__POS           7
#define BMA2X2_RESET_INT__LEN           1
#define BMA2X2_RESET_INT__MSK           0x80
#define BMA2X2_RESET_INT__REG           BMA2X2_INT_CTRL_REG

#define BMA2X2_LOWG_DUR__POS                    0
#define BMA2X2_LOWG_DUR__LEN                    8
#define BMA2X2_LOWG_DUR__MSK                    0xFF
#define BMA2X2_LOWG_DUR__REG                    BMA2X2_LOW_DURN_REG

#define BMA2X2_LOWG_THRES__POS                  0
#define BMA2X2_LOWG_THRES__LEN                  8
#define BMA2X2_LOWG_THRES__MSK                  0xFF
#define BMA2X2_LOWG_THRES__REG                  BMA2X2_LOW_THRES_REG

#define BMA2X2_LOWG_HYST__POS                   0
#define BMA2X2_LOWG_HYST__LEN                   2
#define BMA2X2_LOWG_HYST__MSK                   0x03
#define BMA2X2_LOWG_HYST__REG                   BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_LOWG_INT_MODE__POS               2
#define BMA2X2_LOWG_INT_MODE__LEN               1
#define BMA2X2_LOWG_INT_MODE__MSK               0x04
#define BMA2X2_LOWG_INT_MODE__REG               BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_HIGHG_DUR__POS                    0
#define BMA2X2_HIGHG_DUR__LEN                    8
#define BMA2X2_HIGHG_DUR__MSK                    0xFF
#define BMA2X2_HIGHG_DUR__REG                    BMA2X2_HIGH_DURN_REG

#define BMA2X2_HIGHG_THRES__POS                  0
#define BMA2X2_HIGHG_THRES__LEN                  8
#define BMA2X2_HIGHG_THRES__MSK                  0xFF
#define BMA2X2_HIGHG_THRES__REG                  BMA2X2_HIGH_THRES_REG

#define BMA2X2_HIGHG_HYST__POS                  6
#define BMA2X2_HIGHG_HYST__LEN                  2
#define BMA2X2_HIGHG_HYST__MSK                  0xC0
#define BMA2X2_HIGHG_HYST__REG                  BMA2X2_LOW_HIGH_HYST_REG

#define BMA2X2_SLOPE_DUR__POS                    0
#define BMA2X2_SLOPE_DUR__LEN                    2
#define BMA2X2_SLOPE_DUR__MSK                    0x03
#define BMA2X2_SLOPE_DUR__REG                    BMA2X2_SLOPE_DURN_REG

#define BMA2X2_SLO_NO_MOT_DUR__POS                    2
#define BMA2X2_SLO_NO_MOT_DUR__LEN                    6
#define BMA2X2_SLO_NO_MOT_DUR__MSK                    0xFC
#define BMA2X2_SLO_NO_MOT_DUR__REG                    BMA2X2_SLOPE_DURN_REG

#define BMA2X2_SLOPE_THRES__POS                  0
#define BMA2X2_SLOPE_THRES__LEN                  8
#define BMA2X2_SLOPE_THRES__MSK                  0xFF
#define BMA2X2_SLOPE_THRES__REG                  BMA2X2_SLOPE_THRES_REG

#define BMA2X2_SLO_NO_MOT_THRES__POS                  0
#define BMA2X2_SLO_NO_MOT_THRES__LEN                  8
#define BMA2X2_SLO_NO_MOT_THRES__MSK                  0xFF
#define BMA2X2_SLO_NO_MOT_THRES__REG           BMA2X2_SLO_NO_MOT_THRES_REG

#define BMA2X2_TAP_DUR__POS                    0
#define BMA2X2_TAP_DUR__LEN                    3
#define BMA2X2_TAP_DUR__MSK                    0x07
#define BMA2X2_TAP_DUR__REG                    BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_SHOCK_DURN__POS             6
#define BMA2X2_TAP_SHOCK_DURN__LEN             1
#define BMA2X2_TAP_SHOCK_DURN__MSK             0x40
#define BMA2X2_TAP_SHOCK_DURN__REG             BMA2X2_TAP_PARAM_REG

#define BMA2X2_ADV_TAP_INT__POS                5
#define BMA2X2_ADV_TAP_INT__LEN                1
#define BMA2X2_ADV_TAP_INT__MSK                0x20
#define BMA2X2_ADV_TAP_INT__REG                BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_QUIET_DURN__POS             7
#define BMA2X2_TAP_QUIET_DURN__LEN             1
#define BMA2X2_TAP_QUIET_DURN__MSK             0x80
#define BMA2X2_TAP_QUIET_DURN__REG             BMA2X2_TAP_PARAM_REG

#define BMA2X2_TAP_THRES__POS                  0
#define BMA2X2_TAP_THRES__LEN                  5
#define BMA2X2_TAP_THRES__MSK                  0x1F
#define BMA2X2_TAP_THRES__REG                  BMA2X2_TAP_THRES_REG

#define BMA2X2_TAP_SAMPLES__POS                6
#define BMA2X2_TAP_SAMPLES__LEN                2
#define BMA2X2_TAP_SAMPLES__MSK                0xC0
#define BMA2X2_TAP_SAMPLES__REG                BMA2X2_TAP_THRES_REG

#define BMA2X2_ORIENT_MODE__POS                  0
#define BMA2X2_ORIENT_MODE__LEN                  2
#define BMA2X2_ORIENT_MODE__MSK                  0x03
#define BMA2X2_ORIENT_MODE__REG                  BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_BLOCK__POS                 2
#define BMA2X2_ORIENT_BLOCK__LEN                 2
#define BMA2X2_ORIENT_BLOCK__MSK                 0x0C
#define BMA2X2_ORIENT_BLOCK__REG                 BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_HYST__POS                  4
#define BMA2X2_ORIENT_HYST__LEN                  3
#define BMA2X2_ORIENT_HYST__MSK                  0x70
#define BMA2X2_ORIENT_HYST__REG                  BMA2X2_ORIENT_PARAM_REG

#define BMA2X2_ORIENT_AXIS__POS                  7
#define BMA2X2_ORIENT_AXIS__LEN                  1
#define BMA2X2_ORIENT_AXIS__MSK                  0x80
#define BMA2X2_ORIENT_AXIS__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_ORIENT_UD_EN__POS                  6
#define BMA2X2_ORIENT_UD_EN__LEN                  1
#define BMA2X2_ORIENT_UD_EN__MSK                  0x40
#define BMA2X2_ORIENT_UD_EN__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_THETA_BLOCK__POS                  0
#define BMA2X2_THETA_BLOCK__LEN                  6
#define BMA2X2_THETA_BLOCK__MSK                  0x3F
#define BMA2X2_THETA_BLOCK__REG                  BMA2X2_THETA_BLOCK_REG

#define BMA2X2_THETA_FLAT__POS                  0
#define BMA2X2_THETA_FLAT__LEN                  6
#define BMA2X2_THETA_FLAT__MSK                  0x3F
#define BMA2X2_THETA_FLAT__REG                  BMA2X2_THETA_FLAT_REG

#define BMA2X2_FLAT_HOLD_TIME__POS              4
#define BMA2X2_FLAT_HOLD_TIME__LEN              2
#define BMA2X2_FLAT_HOLD_TIME__MSK              0x30
#define BMA2X2_FLAT_HOLD_TIME__REG              BMA2X2_FLAT_HOLD_TIME_REG

#define BMA2X2_FLAT_HYS__POS                   0
#define BMA2X2_FLAT_HYS__LEN                   3
#define BMA2X2_FLAT_HYS__MSK                   0x07
#define BMA2X2_FLAT_HYS__REG                   BMA2X2_FLAT_HOLD_TIME_REG

#define BMA2X2_FIFO_WML_TRIG_RETAIN__POS                   0
#define BMA2X2_FIFO_WML_TRIG_RETAIN__LEN                   6
#define BMA2X2_FIFO_WML_TRIG_RETAIN__MSK                   0x3F
#define BMA2X2_FIFO_WML_TRIG_RETAIN__REG                   BMA2X2_FIFO_WML_TRIG

#define BMA2X2_EN_SELF_TEST__POS                0
#define BMA2X2_EN_SELF_TEST__LEN                2
#define BMA2X2_EN_SELF_TEST__MSK                0x03
#define BMA2X2_EN_SELF_TEST__REG                BMA2X2_SELF_TEST_REG

#define BMA2X2_NEG_SELF_TEST__POS               2
#define BMA2X2_NEG_SELF_TEST__LEN               1
#define BMA2X2_NEG_SELF_TEST__MSK               0x04
#define BMA2X2_NEG_SELF_TEST__REG               BMA2X2_SELF_TEST_REG

#define BMA2X2_SELF_TEST_AMP__POS               4
#define BMA2X2_SELF_TEST_AMP__LEN               1
#define BMA2X2_SELF_TEST_AMP__MSK               0x10
#define BMA2X2_SELF_TEST_AMP__REG               BMA2X2_SELF_TEST_REG

#define BMA2X2_UNLOCK_EE_PROG_MODE__POS     0
#define BMA2X2_UNLOCK_EE_PROG_MODE__LEN     1
#define BMA2X2_UNLOCK_EE_PROG_MODE__MSK     0x01
#define BMA2X2_UNLOCK_EE_PROG_MODE__REG     BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_START_EE_PROG_TRIG__POS      1
#define BMA2X2_START_EE_PROG_TRIG__LEN      1
#define BMA2X2_START_EE_PROG_TRIG__MSK      0x02
#define BMA2X2_START_EE_PROG_TRIG__REG      BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EE_PROG_READY__POS          2
#define BMA2X2_EE_PROG_READY__LEN          1
#define BMA2X2_EE_PROG_READY__MSK          0x04
#define BMA2X2_EE_PROG_READY__REG          BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_UPDATE_IMAGE__POS                3
#define BMA2X2_UPDATE_IMAGE__LEN                1
#define BMA2X2_UPDATE_IMAGE__MSK                0x08
#define BMA2X2_UPDATE_IMAGE__REG                BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EE_REMAIN__POS                4
#define BMA2X2_EE_REMAIN__LEN                4
#define BMA2X2_EE_REMAIN__MSK                0xF0
#define BMA2X2_EE_REMAIN__REG                BMA2X2_EEPROM_CTRL_REG

#define BMA2X2_EN_SPI_MODE_3__POS              0
#define BMA2X2_EN_SPI_MODE_3__LEN              1
#define BMA2X2_EN_SPI_MODE_3__MSK              0x01
#define BMA2X2_EN_SPI_MODE_3__REG              BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_I2C_WATCHDOG_PERIOD__POS        1
#define BMA2X2_I2C_WATCHDOG_PERIOD__LEN        1
#define BMA2X2_I2C_WATCHDOG_PERIOD__MSK        0x02
#define BMA2X2_I2C_WATCHDOG_PERIOD__REG        BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_EN_I2C_WATCHDOG__POS            2
#define BMA2X2_EN_I2C_WATCHDOG__LEN            1
#define BMA2X2_EN_I2C_WATCHDOG__MSK            0x04
#define BMA2X2_EN_I2C_WATCHDOG__REG            BMA2X2_SERIAL_CTRL_REG

#define BMA2X2_EXT_MODE__POS              7
#define BMA2X2_EXT_MODE__LEN              1
#define BMA2X2_EXT_MODE__MSK              0x80
#define BMA2X2_EXT_MODE__REG              BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_ALLOW_UPPER__POS        6
#define BMA2X2_ALLOW_UPPER__LEN        1
#define BMA2X2_ALLOW_UPPER__MSK        0x40
#define BMA2X2_ALLOW_UPPER__REG        BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_MAP_2_LOWER__POS            5
#define BMA2X2_MAP_2_LOWER__LEN            1
#define BMA2X2_MAP_2_LOWER__MSK            0x20
#define BMA2X2_MAP_2_LOWER__REG            BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_MAGIC_NUMBER__POS            0
#define BMA2X2_MAGIC_NUMBER__LEN            5
#define BMA2X2_MAGIC_NUMBER__MSK            0x1F
#define BMA2X2_MAGIC_NUMBER__REG            BMA2X2_EXTMODE_CTRL_REG

#define BMA2X2_UNLOCK_EE_WRITE_TRIM__POS        4
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__LEN        4
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__MSK        0xF0
#define BMA2X2_UNLOCK_EE_WRITE_TRIM__REG        BMA2X2_CTRL_UNLOCK_REG

#define BMA2X2_EN_SLOW_COMP_X__POS              0
#define BMA2X2_EN_SLOW_COMP_X__LEN              1
#define BMA2X2_EN_SLOW_COMP_X__MSK              0x01
#define BMA2X2_EN_SLOW_COMP_X__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_EN_SLOW_COMP_Y__POS              1
#define BMA2X2_EN_SLOW_COMP_Y__LEN              1
#define BMA2X2_EN_SLOW_COMP_Y__MSK              0x02
#define BMA2X2_EN_SLOW_COMP_Y__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_EN_SLOW_COMP_Z__POS              2
#define BMA2X2_EN_SLOW_COMP_Z__LEN              1
#define BMA2X2_EN_SLOW_COMP_Z__MSK              0x04
#define BMA2X2_EN_SLOW_COMP_Z__REG              BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_FAST_CAL_RDY_S__POS             4
#define BMA2X2_FAST_CAL_RDY_S__LEN             1
#define BMA2X2_FAST_CAL_RDY_S__MSK             0x10
#define BMA2X2_FAST_CAL_RDY_S__REG             BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_CAL_TRIGGER__POS                5
#define BMA2X2_CAL_TRIGGER__LEN                2
#define BMA2X2_CAL_TRIGGER__MSK                0x60
#define BMA2X2_CAL_TRIGGER__REG                BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_RESET_OFFSET_REGS__POS           7
#define BMA2X2_RESET_OFFSET_REGS__LEN           1
#define BMA2X2_RESET_OFFSET_REGS__MSK           0x80
#define BMA2X2_RESET_OFFSET_REGS__REG           BMA2X2_OFFSET_CTRL_REG

#define BMA2X2_COMP_CUTOFF__POS                 0
#define BMA2X2_COMP_CUTOFF__LEN                 1
#define BMA2X2_COMP_CUTOFF__MSK                 0x01
#define BMA2X2_COMP_CUTOFF__REG                 BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_X__POS        1
#define BMA2X2_COMP_TARGET_OFFSET_X__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_X__MSK        0x06
#define BMA2X2_COMP_TARGET_OFFSET_X__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_Y__POS        3
#define BMA2X2_COMP_TARGET_OFFSET_Y__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_Y__MSK        0x18
#define BMA2X2_COMP_TARGET_OFFSET_Y__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_COMP_TARGET_OFFSET_Z__POS        5
#define BMA2X2_COMP_TARGET_OFFSET_Z__LEN        2
#define BMA2X2_COMP_TARGET_OFFSET_Z__MSK        0x60
#define BMA2X2_COMP_TARGET_OFFSET_Z__REG        BMA2X2_OFFSET_PARAMS_REG

#define BMA2X2_FIFO_DATA_SELECT__POS                 0
#define BMA2X2_FIFO_DATA_SELECT__LEN                 2
#define BMA2X2_FIFO_DATA_SELECT__MSK                 0x03
#define BMA2X2_FIFO_DATA_SELECT__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_TRIGGER_SOURCE__POS                 2
#define BMA2X2_FIFO_TRIGGER_SOURCE__LEN                 2
#define BMA2X2_FIFO_TRIGGER_SOURCE__MSK                 0x0C
#define BMA2X2_FIFO_TRIGGER_SOURCE__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_TRIGGER_ACTION__POS                 4
#define BMA2X2_FIFO_TRIGGER_ACTION__LEN                 2
#define BMA2X2_FIFO_TRIGGER_ACTION__MSK                 0x30
#define BMA2X2_FIFO_TRIGGER_ACTION__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_FIFO_MODE__POS                 6
#define BMA2X2_FIFO_MODE__LEN                 2
#define BMA2X2_FIFO_MODE__MSK                 0xC0
#define BMA2X2_FIFO_MODE__REG                 BMA2X2_FIFO_MODE_REG

#define BMA2X2_RANGE_2G                 3
#define BMA2X2_RANGE_4G                 5
#define BMA2X2_RANGE_8G                 8
#define BMA2X2_RANGE_16G                12

#define BMA2X2_BW_7_81HZ        0x08
#define BMA2X2_BW_15_63HZ       0x09
#define BMA2X2_BW_31_25HZ       0x0A
#define BMA2X2_BW_62_50HZ       0x0B
#define BMA2X2_BW_125HZ         0x0C
#define BMA2X2_BW_250HZ         0x0D
#define BMA2X2_BW_500HZ         0x0E
#define BMA2X2_BW_1000HZ        0x0F

#define BMA2X2_SLEEP_DUR_0_5MS        0x05
#define BMA2X2_SLEEP_DUR_1MS          0x06
#define BMA2X2_SLEEP_DUR_2MS          0x07
#define BMA2X2_SLEEP_DUR_4MS          0x08
#define BMA2X2_SLEEP_DUR_6MS          0x09
#define BMA2X2_SLEEP_DUR_10MS         0x0A
#define BMA2X2_SLEEP_DUR_25MS         0x0B
#define BMA2X2_SLEEP_DUR_50MS         0x0C
#define BMA2X2_SLEEP_DUR_100MS        0x0D
#define BMA2X2_SLEEP_DUR_500MS        0x0E
#define BMA2X2_SLEEP_DUR_1S           0x0F

#define BMA2X2_LATCH_DUR_NON_LATCH    0x00
#define BMA2X2_LATCH_DUR_250MS        0x01
#define BMA2X2_LATCH_DUR_500MS        0x02
#define BMA2X2_LATCH_DUR_1S           0x03
#define BMA2X2_LATCH_DUR_2S           0x04
#define BMA2X2_LATCH_DUR_4S           0x05
#define BMA2X2_LATCH_DUR_8S           0x06
#define BMA2X2_LATCH_DUR_LATCH        0x07
#define BMA2X2_LATCH_DUR_NON_LATCH1   0x08
#define BMA2X2_LATCH_DUR_250US        0x09
#define BMA2X2_LATCH_DUR_500US        0x0A
#define BMA2X2_LATCH_DUR_1MS          0x0B
#define BMA2X2_LATCH_DUR_12_5MS       0x0C
#define BMA2X2_LATCH_DUR_25MS         0x0D
#define BMA2X2_LATCH_DUR_50MS         0x0E
#define BMA2X2_LATCH_DUR_LATCH1       0x0F

#define BMA2X2_MODE_NORMAL             0
#define BMA2X2_MODE_LOWPOWER1          1
#define BMA2X2_MODE_SUSPEND            2
#define BMA2X2_MODE_DEEP_SUSPEND       3
#define BMA2X2_MODE_LOWPOWER2          4
#define BMA2X2_MODE_STANDBY            5

#define BMA2X2_LOW_TH_IN_G(gthres, range)		((256 * gthres) / range)
#define BMA2X2_HIGH_TH_IN_G(gthres, range)		((256 * gthres) / range)
#define BMA2X2_LOW_HY_IN_G(ghyst, range)		((32 * ghyst) / range)
#define BMA2X2_HIGH_HY_IN_G(ghyst, range)		((32 * ghyst) / range)
#define BMA2X2_SLOPE_TH_IN_G(gthres, range)		((128 * gthres) / range)

#define BMA2X2_GET_BITSLICE(regvar, bitname)\
	 ((regvar & bitname##__MSK) >> bitname##__POS)
#define BMA2X2_SET_BITSLICE(regvar, bitname, val)\
((regvar & ~bitname##__MSK) | ((val << bitname##__POS) & bitname##__MSK))

#define BMA255_CHIP_ID 0XFA
#define BMA250_CHIP_ID 0XF9
#define BMA250E_CHIP_ID 0X03
#define BMA222E_CHIP_ID 0XF8
#define BMA280_CHIP_ID 0XFB

#define BAM2X2_ENABLE	0X80

#define BMA2XX_RANGE	32768

#define X_AXIS_COMPEN	0/*X_AXIS  not compensation */
/*Y_AXIS offset is caused by screws, there needs to be compensation 3.5*/
#define Y_AXIS_COMPEN	0
#define Z_AXIS_COMPEN	0/*Z_AXIS not compensation*/

static u8 slope_mode;
static u8 high_g_mode;
static u8 interrupt_dur;
static u8 interrupt_threshold;

static int bma2x2_parse_dt(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	u32 temp_val;
	int rc;

	rc = of_property_read_u32(np, "high_g_mode", &temp_val);
	if (!rc)
		high_g_mode = (u8)temp_val;

	rc = of_property_read_u32(np, "slope_mode", &temp_val);
	if (!rc)
		slope_mode = (u8)temp_val;

	rc = of_property_read_u32(np, "interrupt_dur", &temp_val);
	if (!rc)
		interrupt_dur = (u8)temp_val;

	rc = of_property_read_u32(np, "interrupt_threshold", &temp_val);
	if (!rc)
		interrupt_threshold = (u8)temp_val;

	DBG("%s: high_g_mode = %d\n", __func__, high_g_mode);
	DBG("%s: slope_mode = %d\n", __func__, slope_mode);
	DBG("%s: interrupt_dur = %d\n", __func__, interrupt_dur);
	DBG("%s: interrupt_threshold = %d\n", __func__, interrupt_threshold);

	return 0;
}

#ifdef BMA2X2_ENABLE_INT1
static int bma2x2_set_int1_pad_sel(struct i2c_client *client, unsigned char
		int1sel)
{
	int comres = 0;
	unsigned char data = 0;
	unsigned char state;

	state = 0x01;

	switch (int1sel) {
	case 0:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_LOWG__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_LOWG,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_LOWG__REG,
					  data);
		break;
	case 1:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_HIGHG__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_HIGHG,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_HIGHG__REG,
					  data);
		break;
	case 2:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_SLOPE__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SLOPE,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_SLOPE__REG,
					  data);
		break;
	case 3:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_DB_TAP__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_DB_TAP,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_DB_TAP__REG,
					  data);
		break;
	case 4:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_SNG_TAP__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SNG_TAP,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_SNG_TAP__REG,
					  data);
		break;
	case 5:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_ORIENT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_ORIENT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_ORIENT__REG,
					  data);
		break;
	case 6:
		data = sensor_read_reg(client, BMA2X2_EN_INT1_PAD_FLAT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_FLAT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_FLAT__REG,
					  data);
		break;
	case 7:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT1_PAD_SLO_NO_MOT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT1_PAD_SLO_NO_MOT__REG,
					  data);
		break;
	default:
		break;
	}

	return comres;
}
#endif /* BMA2X2_ENABLE_INT1 */

#ifdef BMA2X2_ENABLE_INT2
static int bma2x2_set_int2_pad_sel(struct i2c_client *client, unsigned char
		int2sel)
{
	int comres = 0;
	unsigned char data = 0;
	unsigned char state;

	state = 0x01;

	switch (int2sel) {
	case 0:
		comres = sensor_read_reg(client, BMA2X2_EN_INT2_PAD_LOWG__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_LOWG,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_LOWG__REG,
					  data);
		break;
	case 1:
		comres = sensor_read_reg(client, BMA2X2_EN_INT2_PAD_HIGHG__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_HIGHG,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_HIGHG__REG,
					  data);
		break;
	case 2:
		comres = sensor_read_reg(client, BMA2X2_EN_INT2_PAD_SLOPE__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SLOPE,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_SLOPE__REG,
					  data);
		break;
	case 3:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT2_PAD_DB_TAP__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_DB_TAP,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_DB_TAP__REG,
					  data);
		break;
	case 4:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT2_PAD_SNG_TAP__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SNG_TAP,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_SNG_TAP__REG,
					  data);
		break;
	case 5:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT2_PAD_ORIENT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_ORIENT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_ORIENT__REG,
					  data);
		break;
	case 6:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT2_PAD_FLAT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_FLAT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_FLAT__REG,
					  data);
		break;
	case 7:
		comres = sensor_read_reg(client,
					 BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_EN_INT2_PAD_SLO_NO_MOT,
					   state);
		comres = sensor_write_reg(client,
					  BMA2X2_EN_INT2_PAD_SLO_NO_MOT__REG,
					  data);
		break;
	default:
		break;
	}

	return comres;
}
#endif /* BMA2X2_ENABLE_INT2 */

#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
static int bma2x2_get_interruptstatus1(struct i2c_client *client, unsigned char
		*intstatus)
{
	unsigned char data;

	data = sensor_read_reg(client, BMA2X2_STATUS1_REG);
	*intstatus = data;
	return 0;
}

static int bma2x2_get_HIGH_first(struct i2c_client *client, unsigned char
						 param,
						 unsigned char *intstatus)
{
	unsigned char data;

	switch (param) {
	case 0:
		data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return 0;
}

static int bma2x2_get_HIGH_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	unsigned char data;

	data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_HIGHG_SIGN_S);
	*intstatus = data;

	return 0;
}

static int bma2x2_get_slope_first(struct i2c_client *client, unsigned char
	param, unsigned char *intstatus)
{
	unsigned char data;

	switch (param) {
	case 0:
		data = sensor_read_reg(client, BMA2X2_STATUS_TAP_SLOPE_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		data = sensor_read_reg(client, BMA2X2_STATUS_TAP_SLOPE_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		data = sensor_read_reg(client, BMA2X2_STATUS_TAP_SLOPE_REG);
		data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return 0;
}

static int bma2x2_get_slope_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	unsigned char data;

	data = sensor_read_reg(client, BMA2X2_STATUS_TAP_SLOPE_REG);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_SLOPE_SIGN_S);
	*intstatus = data;

	return 0;
}

static int bma2x2_get_orient_status(struct i2c_client *client, unsigned char
		*intstatus)
{
	unsigned char data;

	data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_ORIENT_S);
	*intstatus = data;

	return 0;
}

static int bma2x2_get_orient_flat_status(struct i2c_client *client, unsigned
		char *intstatus)
{
	unsigned char data;

	data = sensor_read_reg(client, BMA2X2_STATUS_ORIENT_HIGH_REG);
	data = BMA2X2_GET_BITSLICE(data, BMA2X2_FLAT_S);
	*intstatus = data;

	return 0;
}

static int bma2x2_set_int_mode(struct i2c_client *client, unsigned char mode)
{
	unsigned char data;
	int comres = 0;

	data = sensor_read_reg(client, BMA2X2_INT_MODE_SEL__REG);
	data = BMA2X2_SET_BITSLICE(data, BMA2X2_INT_MODE_SEL, mode);
	comres = sensor_write_reg(client,
				BMA2X2_INT_MODE_SEL__REG, data);

	return comres;
}
#endif /* defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2) */

static int bma2x2_set_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data1, data2;

	if (mode < 6) {
		data1 = sensor_read_reg(client, BMA2X2_MODE_CTRL_REG);
		data2 = sensor_read_reg(client, BMA2X2_LOW_NOISE_CTRL_REG);
		switch (mode) {
		case BMA2X2_MODE_NORMAL:
				data1	= BMA2X2_SET_BITSLICE(data1,
							      BMA2X2_MODE_CTRL,
							      0);
				data2	= BMA2X2_SET_BITSLICE
						(data2,
						BMA2X2_LOW_POWER_MODE,
						0);
				sensor_write_reg
						(client,
						BMA2X2_MODE_CTRL_REG,
						data1);
				mdelay(1);
				sensor_write_reg(client,
						  BMA2X2_LOW_NOISE_CTRL_REG,
						  data2);
				break;
		case BMA2X2_MODE_LOWPOWER1:
				data1	= BMA2X2_SET_BITSLICE
							(data1,
							BMA2X2_MODE_CTRL,
							2);
				 data2	= BMA2X2_SET_BITSLICE
							(data2,
							BMA2X2_LOW_POWER_MODE,
							0);
				 sensor_write_reg
						(client,
						BMA2X2_MODE_CTRL_REG,
						data1);
				 mdelay(1);
				 sensor_write_reg
						(client,
						BMA2X2_LOW_NOISE_CTRL_REG,
						data2);
				break;
		case BMA2X2_MODE_SUSPEND:
				 data1	= BMA2X2_SET_BITSLICE(data1,
							      BMA2X2_MODE_CTRL,
							      4);
				 data2	= BMA2X2_SET_BITSLICE
						(data2,
						BMA2X2_LOW_POWER_MODE,
						0);
				 sensor_write_reg(client,
						  BMA2X2_LOW_NOISE_CTRL_REG,
						  data2);
				 mdelay(1);
				 sensor_write_reg(client,
						  BMA2X2_MODE_CTRL_REG,
						  data1);
				break;
		case BMA2X2_MODE_DEEP_SUSPEND:
				 data1	= BMA2X2_SET_BITSLICE
						(data1,
						BMA2X2_MODE_CTRL,
						1);
				 data2	= BMA2X2_SET_BITSLICE
						(data2,
						BMA2X2_LOW_POWER_MODE,
						1);
				 sensor_write_reg
						(client,
						BMA2X2_MODE_CTRL_REG,
						data1);
				 mdelay(1);
				 sensor_write_reg(client,
						  BMA2X2_LOW_NOISE_CTRL_REG,
						  data2);
				break;
		case BMA2X2_MODE_LOWPOWER2:
				 data1	= BMA2X2_SET_BITSLICE
						(data1,
						BMA2X2_MODE_CTRL,
						2);
				 data2	= BMA2X2_SET_BITSLICE
						(data2,
						BMA2X2_LOW_POWER_MODE,
						1);
				 sensor_write_reg(client,
						  BMA2X2_MODE_CTRL_REG,
						  data1);
				 mdelay(1);
				 sensor_write_reg(client,
						  BMA2X2_LOW_NOISE_CTRL_REG,
						  data2);
				break;
		case BMA2X2_MODE_STANDBY:
				 data1	= BMA2X2_SET_BITSLICE(data1,
							      BMA2X2_MODE_CTRL,
							      4);
				 data2	= BMA2X2_SET_BITSLICE
						(data2,
						BMA2X2_LOW_POWER_MODE,
						1);
				 sensor_write_reg(client,
						  BMA2X2_LOW_NOISE_CTRL_REG,
						  data2);
				 mdelay(1);
				 sensor_write_reg(client,
						  BMA2X2_MODE_CTRL_REG,
						  data1);
		break;
		}
	} else {
		comres =  -1;
	}

	return comres;
}

static int bma2x2_set_range(struct i2c_client *client, unsigned char range)
{
	int comres = 0;
	unsigned char data1;

	if ((range == 3) || (range == 5) || (range == 8) || (range == 12)) {
		data1 = sensor_read_reg(client, BMA2X2_RANGE_SEL_REG);
		switch (range) {
		case BMA2X2_RANGE_2G:
			 data1	= BMA2X2_SET_BITSLICE(data1,
						      BMA2X2_RANGE_SEL, 3);
			break;
		case BMA2X2_RANGE_4G:
			data1	= BMA2X2_SET_BITSLICE(data1,
						      BMA2X2_RANGE_SEL, 5);
			break;
		case BMA2X2_RANGE_8G:
			data1	= BMA2X2_SET_BITSLICE(data1,
						      BMA2X2_RANGE_SEL, 8);
			break;
		case BMA2X2_RANGE_16G:
			data1	= BMA2X2_SET_BITSLICE(data1,
						      BMA2X2_RANGE_SEL, 12);
			break;
		}
		comres += sensor_write_reg(client, BMA2X2_RANGE_SEL_REG,
				 data1);
	} else {
		comres =  -1;
	}

	return comres;
}

static int bma2x2_set_bandwidth(struct i2c_client *client, unsigned char BW)
{
	int comres = 0;
	unsigned char data;
	int bandwidth = 0;

	if (BW > 7 && BW < 16) {
		switch (BW) {
		case BMA2X2_BW_7_81HZ:
			bandwidth = BMA2X2_BW_7_81HZ;

			/*  7.81 Hz	  64000 uS	 */
			break;
		case BMA2X2_BW_15_63HZ:
			 bandwidth = BMA2X2_BW_15_63HZ;

			/*  15.63 Hz	  32000 uS	 */
			break;
		case BMA2X2_BW_31_25HZ:
			bandwidth = BMA2X2_BW_31_25HZ;

			/*  31.25 Hz	  16000 uS	 */
			break;
		case BMA2X2_BW_62_50HZ:
			bandwidth = BMA2X2_BW_62_50HZ;

			/*  62.50 Hz	  8000 uS	*/
			break;
		case BMA2X2_BW_125HZ:
			bandwidth = BMA2X2_BW_125HZ;

			 /*  125 Hz	  4000 uS	*/
			break;
		case BMA2X2_BW_250HZ:
			bandwidth = BMA2X2_BW_250HZ;

			/*  250 Hz	  2000 uS	*/
			break;
		case BMA2X2_BW_500HZ:
			bandwidth = BMA2X2_BW_500HZ;

			/*  500 Hz	  1000 uS	*/
			break;
		case BMA2X2_BW_1000HZ:
			bandwidth = BMA2X2_BW_1000HZ;

			/*  1000 Hz	  500 uS   */
			break;
		}
		data = sensor_read_reg(client, BMA2X2_BANDWIDTH__REG);
		data = BMA2X2_SET_BITSLICE(data, BMA2X2_BANDWIDTH, bandwidth);
		comres += sensor_write_reg(client, BMA2X2_BANDWIDTH__REG,
				data);
	} else {
		comres =  -1;
	}

	return comres;
}

#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
	unsigned char *orient[] = {"upward looking portrait upright",
	 "upward looking portrait upside - down",
		 "upward looking landscape left",
		 "upward looking landscape right",
		 "downward looking portrait upright",
		 "downward looking portrait upside - down",
		 "downward looking landscape left",
		 "downward looking landscape right"};

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);

	unsigned char status = 0;
	unsigned char i;
	unsigned char first_value = 0;
	unsigned char sign_value = 0;

	bma2x2_get_interruptstatus1(client, &status);

	switch (status) {
	case 0x01:
		DBG("Low G interrupt happened\n");
		input_report_rel(sensor->input_dev, LOW_G_INTERRUPT,
				  LOW_G_INTERRUPT_HAPPENED);
		break;
	case 0x02:
		for (i = 0; i < 3; i++) {
			bma2x2_get_HIGH_first(client, i,
					      &first_value);
			if (first_value == 1) {
				bma2x2_get_HIGH_sign(client,
						     &sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel
					(sensor->input_dev,
					HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED);
					if (i == 1)
						input_report_rel
					(sensor->input_dev,
					HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED);
					if (i == 2)
						input_report_rel
					(sensor->input_dev,
					HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel
						(sensor->input_dev,
						HIGH_G_INTERRUPT,
						HIGH_G_INTERRUPT_X_HAPPENED);
					if (i == 1)
						input_report_rel
						(sensor->input_dev,
						HIGH_G_INTERRUPT,
						HIGH_G_INTERRUPT_Y_HAPPENED);
					if (i == 2)
						input_report_rel
						(sensor->input_dev,
						HIGH_G_INTERRUPT,
						HIGH_G_INTERRUPT_Z_HAPPENED);
				}
			}
			DBG
			("High G interrupt happened,exis is %d,first is %d,sign is %d\n",
			i, first_value, sign_value);
		}
			break;
	case 0x04:
		for (i = 0; i < 3; i++) {
			bma2x2_get_slope_first(client, i,
					&first_value);
			if (first_value == 1) {
				bma2x2_get_slope_sign(client,
						&sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel
					(sensor->input_dev,
					SLOP_INTERRUPT,
					SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED);
					else if (i == 1)
						input_report_rel
					(sensor->input_dev,
					SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED);
					else if (i == 2)
						input_report_rel
					(sensor->input_dev,
					SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel
						(sensor->input_dev,
						SLOP_INTERRUPT,
						SLOPE_INTERRUPT_X_HAPPENED);
					else if (i == 1)
						input_report_rel
						(sensor->input_dev,
						SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Y_HAPPENED);
					else if (i == 2)
						input_report_rel
						(sensor->input_dev,
						SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Z_HAPPENED);
				}
			}

			DBG("Slop interrupt happened,exis is %d,first is %d,sign is %d\n",
				i, first_value, sign_value);
		}
		break;

	case 0x08:
		DBG("slow/ no motion interrupt happened\n");
		input_report_rel(sensor->input_dev, SLOW_NO_MOTION_INTERRUPT,
				SLOW_NO_MOTION_INTERRUPT_HAPPENED);
		break;

	case 0x10:
		DBG("double tap interrupt happened\n");
		input_report_rel(sensor->input_dev, DOUBLE_TAP_INTERRUPT,
				DOUBLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x20:
		DBG("single tap interrupt happened\n");
		input_report_rel(sensor->input_dev, SINGLE_TAP_INTERRUPT,
				  SINGLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x40:
		bma2x2_get_orient_status(client,
					  &first_value);
		DBG
		("orient interrupt happened,%s\n", orient[first_value]);
		if (first_value == 0)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 1)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 2)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 3)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		else if (first_value == 4)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 5)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 6)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 7)
			input_report_abs
			(sensor->input_dev, ORIENT_INTERRUPT,
			DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		break;
	case 0x80:
		bma2x2_get_orient_flat_status(client,
					       &sign_value);
		DBG
		("flat interrupt happened,flat status is %d\n",
		sign_value);
		if (sign_value == 1) {
			input_report_abs(sensor->input_dev, FLAT_INTERRUPT,
					 FLAT_INTERRUPT_TRUE_HAPPENED);
		} else {
			input_report_abs(sensor->input_dev, FLAT_INTERRUPT,
					 FLAT_INTERRUPT_FALSE_HAPPENED);
		}
		break;
	default:
		break;
	}
	return 0;
}
#else

static void bma2x2_remap_sensor_data(struct i2c_client *client,
				     struct sensor_axis *axis)
{
#ifdef CONFIG_BMA_USE_PLATFORM_DATA
	struct bosch_sensor_data bsd;

	if (!client->bst_pd)
		return;

	bsd.x = axis->x;
	bsd.y = axis->y;
	bsd.z = axis->z;

	bst_remap_sensor_data_dft_tab(&bsd,
				       client_data->bst_pd->place);

	axis->x = bsd.x;
	axis->y = bsd.y;
	axis->z = bsd.z;
#else
	(void)axis;
	(void)client;
#endif
}

static int gsensor_report_value
(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report acceleration sensor information */
		input_report_abs(sensor->input_dev, ABS_X, axis->x);
		input_report_abs(sensor->input_dev, ABS_Y, axis->y);
		input_report_abs(sensor->input_dev, ABS_Z, axis->z);
		input_sync(sensor->input_dev);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	int comres = 0;
	unsigned char data[6];
	short x, y, z;
	unsigned int xyz_adc_rang = 0;
	char value = 0;
	struct sensor_axis axis;
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;

	/*sensor->ops->read_len = 6*/
	if (sensor->ops->read_len < 6) {
		DBG
		("%s:len is error,len=%d\n", __func__, sensor->ops->read_len);
		return  -1;
	}
	memset(data, 0, 6);

	*data = sensor->ops->read_reg;
#ifdef BMA2X2_SENSOR_IDENTIFICATION_ENABLE
	comres = sensor_rx_data(client, data, sensor->ops->read_len);
	x = (data[1] << 8) | data[0];
	y = (data[3] << 8) | data[2];
	z = (data[5] << 8) | data[4];
#else
	switch (sensor->devid) {
	case BMA255_CHIP_ID:
		comres = sensor_rx_data(client, data, sensor->ops->read_len);

		x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X12_LSB) |
			 (BMA2X2_GET_BITSLICE(data[1],
				 BMA2X2_ACC_X_MSB) <<
					(BMA2X2_ACC_X12_LSB__LEN));
		x = x  <<  (sizeof(short) * 8 - (BMA2X2_ACC_X12_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));
		x = x >> (sizeof(short) * 8 - (BMA2X2_ACC_X12_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));

		y = BMA2X2_GET_BITSLICE(data[2], BMA2X2_ACC_Y12_LSB) |
			 (BMA2X2_GET_BITSLICE(data[3],
				 BMA2X2_ACC_Y_MSB) <<
					(BMA2X2_ACC_Y12_LSB__LEN));
		y = y  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Y12_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));
		y = y >> (sizeof(short) * 8 - (BMA2X2_ACC_Y12_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));

		z = BMA2X2_GET_BITSLICE(data[4], BMA2X2_ACC_Z12_LSB) |
			 (BMA2X2_GET_BITSLICE(data[5],
				 BMA2X2_ACC_Z_MSB) <<
					(BMA2X2_ACC_Z12_LSB__LEN));
		z = z  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Z12_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		z = z >> (sizeof(short) * 8 - (BMA2X2_ACC_Z12_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		xyz_adc_rang = 0x800;
		break;
	case BMA250E_CHIP_ID:
	case BMA250_CHIP_ID:
		comres = sensor_rx_data(client, data, sensor->ops->read_len);

		x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X10_LSB) |
			 (BMA2X2_GET_BITSLICE(data[1],
				 BMA2X2_ACC_X_MSB) <<
					(BMA2X2_ACC_X10_LSB__LEN));
		x = x  <<  (sizeof(short) * 8 - (BMA2X2_ACC_X10_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));
		x = x >> (sizeof(short) * 8 - (BMA2X2_ACC_X10_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));

		y = BMA2X2_GET_BITSLICE(data[2], BMA2X2_ACC_Y10_LSB) |
			 (BMA2X2_GET_BITSLICE(data[3],
				 BMA2X2_ACC_Y_MSB) << (BMA2X2_ACC_Y10_LSB__LEN
									 ));
		y = y  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Y10_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));
		y = y >> (sizeof(short) * 8 - (BMA2X2_ACC_Y10_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));

		z = BMA2X2_GET_BITSLICE(data[4], BMA2X2_ACC_Z10_LSB) |
			 (BMA2X2_GET_BITSLICE(data[5],
				 BMA2X2_ACC_Z_MSB) <<
					(BMA2X2_ACC_Z10_LSB__LEN));
		z = z  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Z10_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		z = z >> (sizeof(short) * 8 - (BMA2X2_ACC_Z10_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		xyz_adc_rang = 0x200;
		/* compensation y axis*/
		x = x + (X_AXIS_COMPEN * xyz_adc_rang) / BMA2XX_RANGE;
		y = y + (Y_AXIS_COMPEN * xyz_adc_rang) / BMA2XX_RANGE;
		z = z + (Z_AXIS_COMPEN * xyz_adc_rang) / BMA2XX_RANGE;
		break;
	case BMA222E_CHIP_ID:
		comres = sensor_rx_data(client, data, sensor->ops->read_len);
		x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X8_LSB) |
			 (BMA2X2_GET_BITSLICE(data[1],
				 BMA2X2_ACC_X_MSB) << (BMA2X2_ACC_X8_LSB__LEN));
		x = x  <<  (sizeof(short) * 8 - (BMA2X2_ACC_X8_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));
		x = x >> (sizeof(short) * 8 - (BMA2X2_ACC_X8_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));

		y = BMA2X2_GET_BITSLICE(data[2], BMA2X2_ACC_Y8_LSB) |
			 (BMA2X2_GET_BITSLICE(data[3],
				 BMA2X2_ACC_Y_MSB) << (BMA2X2_ACC_Y8_LSB__LEN
									 ));
		y = y  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Y8_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));
		y = y >> (sizeof(short) * 8 - (BMA2X2_ACC_Y8_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));

		z = BMA2X2_GET_BITSLICE(data[4], BMA2X2_ACC_Z8_LSB) |
			 (BMA2X2_GET_BITSLICE(data[5],
				 BMA2X2_ACC_Z_MSB) << (BMA2X2_ACC_Z8_LSB__LEN));
		z = z  <<  (sizeof(short) * 8 - (BMA2X2_ACC_Z8_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		z = z >> (sizeof(short) * 8 - (BMA2X2_ACC_Z8_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		xyz_adc_rang = 0x80;
		break;
	case BMA280_CHIP_ID:
		comres = sensor_rx_data(client, data, sensor->ops->read_len);

		x = BMA2X2_GET_BITSLICE(data[0], BMA2X2_ACC_X14_LSB) |
			 (BMA2X2_GET_BITSLICE(data[1],
				BMA2X2_ACC_X_MSB) << (BMA2X2_ACC_X14_LSB__LEN));
		 x = x  <<  (sizeof(short) * 8  -  (BMA2X2_ACC_X14_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));
		 x = x >> (sizeof(short) * 8  -  (BMA2X2_ACC_X14_LSB__LEN +
					 BMA2X2_ACC_X_MSB__LEN));

		 y = BMA2X2_GET_BITSLICE(data[2], BMA2X2_ACC_Y14_LSB) |
			 (BMA2X2_GET_BITSLICE(data[3],
				BMA2X2_ACC_Y_MSB) << (BMA2X2_ACC_Y14_LSB__LEN));
		y = y  <<  (sizeof(short) * 8  -  (BMA2X2_ACC_Y14_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));
		y = y >> (sizeof(short) * 8  -  (BMA2X2_ACC_Y14_LSB__LEN +
					 BMA2X2_ACC_Y_MSB__LEN));

		z = BMA2X2_GET_BITSLICE(data[4], BMA2X2_ACC_Z14_LSB)  |
			 (BMA2X2_GET_BITSLICE(data[5],
			  BMA2X2_ACC_Z_MSB) << (BMA2X2_ACC_Z14_LSB__LEN));
		z = z  <<  (sizeof(short) * 8  -  (BMA2X2_ACC_Z14_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		z = z >> (sizeof(short) * 8  -  (BMA2X2_ACC_Z14_LSB__LEN +
					 BMA2X2_ACC_Z_MSB__LEN));
		xyz_adc_rang = 0x2000;
		break;
	default:
		return  -1;
	}
#endif

	axis.x = (pdata->orientation[0]) * x +
		(pdata->orientation[1]) * y +
		(pdata->orientation[2]) * z;
	axis.y = (pdata->orientation[3]) * x +
		(pdata->orientation[4]) * y +
		(pdata->orientation[5]) * z;
	axis.z = (pdata->orientation[6]) * x +
		(pdata->orientation[7]) * y +
		(pdata->orientation[8]) * z;

	axis.x = axis.x * (BMA2XX_RANGE / xyz_adc_rang);
	axis.y = axis.y * (BMA2XX_RANGE / xyz_adc_rang);
	axis.z = axis.z * (BMA2XX_RANGE / xyz_adc_rang);

	bma2x2_remap_sensor_data(client, &axis);

	gsensor_report_value(client, &axis);
	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	if ((sensor->pdata->irq_enable) && (sensor->ops->int_status_reg >= 0)) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		DBG("%s:sensor int status :0x%x\n", __func__, value);
	}

	return comres;
}
#endif /* defined(BMA2X2_ENABLE_INT1) |  | defined(BMA2X2_ENABLE_INT2) */

/****************operate according to sensor chip:start************/
static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	if (enable)
		bma2x2_set_mode(client, BMA2X2_MODE_NORMAL);
	else
		bma2x2_set_mode(client, BMA2X2_MODE_SUSPEND);

	return 0;
}

static int sensor_init(struct i2c_client *client)
{
	int ret = 0;
	int i = 0;
	unsigned char id_reg;
	unsigned char id_data = 0;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);

	ret = sensor->ops->active(client, 0, 0);
	if (ret) {
		DBG("%s:line=%d,error\n", __func__, __LINE__);
		return ret;
	}
	sensor->status_cur = SENSOR_OFF;
	/* read chip id */
	id_reg = sensor->ops->id_reg;
	for (i = 0; i < 3; i++) {
		ret = sensor_rx_data(client, &id_reg, 1);
		id_data = id_reg;
		if (!ret)
			break;
	}
	if (ret) {
		DBG("%s:fail to read id,ret=%d\n", __func__, ret);
		return ret;
	}
	sensor->devid = id_data;

	ret = bma2x2_set_bandwidth(client, BMA2X2_BW_SET);
	if (ret < 0)
		DBG("set bandwidth failed!\n");

	ret = bma2x2_set_range(client, BMA2X2_RANGE_SET);
	if (ret < 0)
		DBG("set g - range failed!\n");

#if defined(BMA2X2_ENABLE_INT1) || defined(BMA2X2_ENABLE_INT2)
		bma2x2_set_int_mode(client, 1);/*latch interrupt 250ms*/
#endif

#ifdef BMA2X2_ENABLE_INT1
		/* maps interrupt to INT1 pin */
		bma2x2_set_int1_pad_sel(client, PAD_LOWG);
		bma2x2_set_int1_pad_sel(client, PAD_HIGHG);
		bma2x2_set_int1_pad_sel(client, PAD_SLOP);
		bma2x2_set_int1_pad_sel(client, PAD_DOUBLE_TAP);
		bma2x2_set_int1_pad_sel(client, PAD_SINGLE_TAP);
		bma2x2_set_int1_pad_sel(client, PAD_ORIENT);
		bma2x2_set_int1_pad_sel(client, PAD_FLAT);
		bma2x2_set_int1_pad_sel(client, PAD_SLOW_NO_MOTION);
#endif

#ifdef BMA2X2_ENABLE_INT2
		/* maps interrupt to INT2 pin */
		bma2x2_set_int2_pad_sel(client, PAD_LOWG);
		bma2x2_set_int2_pad_sel(client, PAD_HIGHG);
		bma2x2_set_int2_pad_sel(client, PAD_SLOP);
		bma2x2_set_int2_pad_sel(client, PAD_DOUBLE_TAP);
		bma2x2_set_int2_pad_sel(client, PAD_SINGLE_TAP);
		bma2x2_set_int2_pad_sel(client, PAD_ORIENT);
		bma2x2_set_int2_pad_sel(client, PAD_FLAT);
		bma2x2_set_int2_pad_sel(client, PAD_SLOW_NO_MOTION);
#endif

	bma2x2_parse_dt(client);
	if (sensor->pdata->irq_enable) {
		ret = sensor_write_reg(client, BMA2X2_INT_CTRL_REG, 0x01);
		if (ret) {
			dev_err(&client->dev, "interrupt register setting failed!\n");
			return ret;
		}
		if (slope_mode) {
			ret = sensor_write_reg(client,
					       BMA2X2_INT1_PAD_SEL_REG,
					       0x04);
			if (ret) {
				dev_err(&client->dev, "interrupt map register setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_SLOPE_DURN_REG,
					       interrupt_dur);
			if (ret) {
				dev_err(&client->dev, "interrupt delay time register setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_SLOPE_THRES_REG,
					       interrupt_threshold);
			if (ret) {
				dev_err(&client->dev, "high - g interrupt threshold setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_INT_ENABLE1_REG,
					       0x07);
			if (ret) {
				dev_err(&client->dev, "interrupt engines register setting failed!\n");
				return ret;
			}
		} else if (high_g_mode) {
			ret = sensor_write_reg(client,
					       BMA2X2_INT1_PAD_SEL_REG,
					       0x02);
			if (ret) {
				dev_err(&client->dev, "interrupt map register setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_HIGH_DURN_REG,
					       interrupt_dur);
			if (ret) {
				dev_err(&client->dev, "interrupt delay time register setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_HIGH_THRES_REG,
					       interrupt_threshold);
			if (ret) {
				dev_err(&client->dev, "high - g interrupt threshold setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_INT_ENABLE2_REG,
					       0x03);
			if (ret) {
				dev_err(&client->dev, "interrupt engines register setting failed!\n");
				return ret;
			}
		} else {
			ret = sensor_write_reg(client,
					       BMA2X2_INT_DATA_SEL_REG,
					       0x01);
			if (ret) {
				dev_err(&client->dev, "interrupt map register setting failed!\n");
				return ret;
			}
			ret = sensor_write_reg(client,
					       BMA2X2_INT_ENABLE2_REG,
					       0x10);
			if (ret) {
				dev_err(&client->dev, "interrupt engines register setting failed!\n");
				return ret;
			}
		}
	}

	return ret;
}

struct sensor_operate gsensor_bma2x2_ops = {
	.name		= "bma2xx_acc",
	/*sensor type and it should be correct*/
	.type		= SENSOR_TYPE_ACCEL,
	.id_i2c		= ACCEL_ID_BMA2XX,	/*i2c id number*/
	.read_reg	= BMA2X2_X_AXIS_LSB_REG,/*read data*/
	.read_len	= 6,			/*data length*/
	/* read device id from this register */
	.id_reg		= BMA2X2_CHIP_ID_REG,
	.id_data	= BMA250_CHIP_ID,	/* device id */
	.precision	= SENSOR_UNKNOW_DATA,	/*12 bit*/
	.ctrl_reg	= BMA2X2_MODE_CTRL_REG,	/*enable or disable*/
	/*intterupt status register*/
	.int_status_reg	= BMA2X2_STATUS2_REG,
	.range		= {-BMA2XX_RANGE, BMA2XX_RANGE},/*range*/
	.trig		= IRQF_TRIGGER_HIGH  |  IRQF_ONESHOT,
	.active		= sensor_active,
	.init		= sensor_init,
	.report		= sensor_report_value,
};

/****************operate according to sensor chip:end************/
/*function name should not be changed*/
static struct sensor_operate *bma2x2_get_ops(void)
{
	return &gsensor_bma2x2_ops;
}

static int __init gsensor_bma2x2_init(void)
{
	struct sensor_operate *ops = bma2x2_get_ops();
	int result = 0;
	int type = ops->type;

	result = sensor_register_slave(type, NULL, NULL, bma2x2_get_ops);
	return result;
}

static void __exit gsensor_bma2x2_exit(void)
{
	struct sensor_operate *ops = bma2x2_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, bma2x2_get_ops);
}

module_init(gsensor_bma2x2_init);
module_exit(gsensor_bma2x2_exit);
