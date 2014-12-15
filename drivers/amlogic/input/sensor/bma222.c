/*  Date: 2011/8/31 17:00:00
 *  Revision: 2.2
 */

/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2011 Bosch Sensortec GmbH
 * All Rights Reserved
 */


/* file BMA222.c
   brief This file contains all function implementations for the BMA222 in linux

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
#include <linux/kernel.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/sensor/sensor_common.h>

#define SENSOR_NAME 			"bma222"
#define ABSMIN				-512
#define ABSMAX				512
#define SLOPE_THRESHOLD_VALUE 		32
#define SLOPE_DURATION_VALUE 		1
#define INTERRUPT_LATCH_MODE 		13
#define INTERRUPT_ENABLE 		1
#define INTERRUPT_DISABLE 		0
#define MAP_SLOPE_INTERRUPT 		2
#define SLOPE_X_INDEX 			5
#define SLOPE_Y_INDEX 			6
#define SLOPE_Z_INDEX 			7
#define BMA222_MAX_DELAY		200
#define BMA222_CHIP_ID			3
#define BMA222E_CHIP_ID			248
#define BMA222_RANGE_SET		0
#define BMA222_BW_SET			4

#define LOW_G_INTERRUPT				REL_Z
#define HIGH_G_INTERRUPT 			REL_HWHEEL
#define SLOP_INTERRUPT 				REL_DIAL
#define DOUBLE_TAP_INTERRUPT 			REL_WHEEL
#define SINGLE_TAP_INTERRUPT 			REL_MISC
#define ORIENT_INTERRUPT 			ABS_PRESSURE
#define FLAT_INTERRUPT 				ABS_DISTANCE


#define HIGH_G_INTERRUPT_X_HAPPENED			1
#define HIGH_G_INTERRUPT_Y_HAPPENED 			2
#define HIGH_G_INTERRUPT_Z_HAPPENED 			3
#define HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED 		4
#define HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED		5
#define HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED 		6
#define SLOPE_INTERRUPT_X_HAPPENED 			7
#define SLOPE_INTERRUPT_Y_HAPPENED 			8
#define SLOPE_INTERRUPT_Z_HAPPENED 			9
#define SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED 		10
#define SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED 		11
#define SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED 		12
#define DOUBLE_TAP_INTERRUPT_HAPPENED 			13
#define SINGLE_TAP_INTERRUPT_HAPPENED 			14
#define UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED 		15
#define UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED	 	16
#define UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED 	17
#define UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED	18
#define DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED 	19
#define DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED 	20
#define DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED 	21
#define DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED 	22
#define FLAT_INTERRUPT_TURE_HAPPENED			23
#define FLAT_INTERRUPT_FALSE_HAPPENED			24
#define LOW_G_INTERRUPT_HAPPENED			25

#define PAD_LOWG					0
#define PAD_HIGHG					1
#define PAD_SLOP					2
#define PAD_DOUBLE_TAP					3
#define PAD_SINGLE_TAP					4
#define PAD_ORIENT					5
#define PAD_FLAT					6


#define BMA222_CHIP_ID_REG                      0x00
#define BMA222_VERSION_REG                      0x01
#define BMA222_X_AXIS_LSB_REG                   0x02
#define BMA222_X_AXIS_MSB_REG                   0x03
#define BMA222_Y_AXIS_LSB_REG                   0x04
#define BMA222_Y_AXIS_MSB_REG                   0x05
#define BMA222_Z_AXIS_LSB_REG                   0x06
#define BMA222_Z_AXIS_MSB_REG                   0x07
#define BMA222_TEMP_RD_REG                      0x08
#define BMA222_STATUS1_REG                      0x09
#define BMA222_STATUS2_REG                      0x0A
#define BMA222_STATUS_TAP_SLOPE_REG             0x0B
#define BMA222_STATUS_ORIENT_HIGH_REG           0x0C
#define BMA222_RANGE_SEL_REG                    0x0F
#define BMA222_BW_SEL_REG                       0x10
#define BMA222_MODE_CTRL_REG                    0x11
#define BMA222_LOW_NOISE_CTRL_REG               0x12
#define BMA222_DATA_CTRL_REG                    0x13
#define BMA222_RESET_REG                        0x14
#define BMA222_INT_ENABLE1_REG                  0x16
#define BMA222_INT_ENABLE2_REG                  0x17
#define BMA222_INT1_PAD_SEL_REG                 0x19
#define BMA222_INT_DATA_SEL_REG                 0x1A
#define BMA222_INT2_PAD_SEL_REG                 0x1B
#define BMA222_INT_SRC_REG                      0x1E
#define BMA222_INT_SET_REG                      0x20
#define BMA222_INT_CTRL_REG                     0x21
#define BMA222_LOW_DURN_REG                     0x22
#define BMA222_LOW_THRES_REG                    0x23
#define BMA222_LOW_HIGH_HYST_REG                0x24
#define BMA222_HIGH_DURN_REG                    0x25
#define BMA222_HIGH_THRES_REG                   0x26
#define BMA222_SLOPE_DURN_REG                   0x27
#define BMA222_SLOPE_THRES_REG                  0x28
#define BMA222_TAP_PARAM_REG                    0x2A
#define BMA222_TAP_THRES_REG                    0x2B
#define BMA222_ORIENT_PARAM_REG                 0x2C
#define BMA222_THETA_BLOCK_REG                  0x2D
#define BMA222_THETA_FLAT_REG                   0x2E
#define BMA222_FLAT_HOLD_TIME_REG               0x2F
#define BMA222_STATUS_LOW_POWER_REG             0x31
#define BMA222_SELF_TEST_REG                    0x32
#define BMA222_EEPROM_CTRL_REG                  0x33
#define BMA222_SERIAL_CTRL_REG                  0x34
#define BMA222_CTRL_UNLOCK_REG                  0x35
#define BMA222_OFFSET_CTRL_REG                  0x36
#define BMA222_OFFSET_PARAMS_REG                0x37
#define BMA222_OFFSET_FILT_X_REG                0x38
#define BMA222_OFFSET_FILT_Y_REG                0x39
#define BMA222_OFFSET_FILT_Z_REG                0x3A
#define BMA222_OFFSET_UNFILT_X_REG              0x3B
#define BMA222_OFFSET_UNFILT_Y_REG              0x3C
#define BMA222_OFFSET_UNFILT_Z_REG              0x3D
#define BMA222_SPARE_0_REG                      0x3E
#define BMA222_SPARE_1_REG                      0x3F

#define BMA222_ACC_X8_LSB__POS           0
#define BMA222_ACC_X8_LSB__LEN           0
#define BMA222_ACC_X8_LSB__MSK           0x00
#define BMA222_ACC_X8_LSB__REG           BMA222_X_AXIS_LSB_REG

#define BMA222_ACC_Y8_LSB__POS           0
#define BMA222_ACC_Y8_LSB__LEN           0
#define BMA222_ACC_Y8_LSB__MSK           0x00
#define BMA222_ACC_Y8_LSB__REG           BMA222_Y_AXIS_LSB_REG

#define BMA222_ACC_Z8_LSB__POS           0
#define BMA222_ACC_Z8_LSB__LEN           0
#define BMA222_ACC_Z8_LSB__MSK           0x00
#define BMA222_ACC_Z8_LSB__REG          BMA222_Z_AXIS_LSB_REG

#define BMA222_ACC_X_LSB__POS           6
#define BMA222_ACC_X_LSB__LEN           2
#define BMA222_ACC_X_LSB__MSK           0xC0
#define BMA222_ACC_X_LSB__REG           BMA222_X_AXIS_LSB_REG

#define BMA222_ACC_X_MSB__POS           0
#define BMA222_ACC_X_MSB__LEN           8
#define BMA222_ACC_X_MSB__MSK           0xFF
#define BMA222_ACC_X_MSB__REG           BMA222_X_AXIS_MSB_REG

#define BMA222_ACC_Y_LSB__POS           6
#define BMA222_ACC_Y_LSB__LEN           2
#define BMA222_ACC_Y_LSB__MSK           0xC0
#define BMA222_ACC_Y_LSB__REG           BMA222_Y_AXIS_LSB_REG

#define BMA222_ACC_Y_MSB__POS           0
#define BMA222_ACC_Y_MSB__LEN           8
#define BMA222_ACC_Y_MSB__MSK           0xFF
#define BMA222_ACC_Y_MSB__REG           BMA222_Y_AXIS_MSB_REG

#define BMA222_ACC_Z_LSB__POS           6
#define BMA222_ACC_Z_LSB__LEN           2
#define BMA222_ACC_Z_LSB__MSK           0xC0
#define BMA222_ACC_Z_LSB__REG           BMA222_Z_AXIS_LSB_REG

#define BMA222_ACC_Z_MSB__POS           0
#define BMA222_ACC_Z_MSB__LEN           8
#define BMA222_ACC_Z_MSB__MSK           0xFF
#define BMA222_ACC_Z_MSB__REG           BMA222_Z_AXIS_MSB_REG

#define BMA222_RANGE_SEL__POS             0
#define BMA222_RANGE_SEL__LEN             4
#define BMA222_RANGE_SEL__MSK             0x0F
#define BMA222_RANGE_SEL__REG             BMA222_RANGE_SEL_REG

#define BMA222_BANDWIDTH__POS             0
#define BMA222_BANDWIDTH__LEN             5
#define BMA222_BANDWIDTH__MSK             0x1F
#define BMA222_BANDWIDTH__REG             BMA222_BW_SEL_REG

#define BMA222_EN_LOW_POWER__POS          6
#define BMA222_EN_LOW_POWER__LEN          1
#define BMA222_EN_LOW_POWER__MSK          0x40
#define BMA222_EN_LOW_POWER__REG          BMA222_MODE_CTRL_REG

#define BMA222_EN_SUSPEND__POS            7
#define BMA222_EN_SUSPEND__LEN            1
#define BMA222_EN_SUSPEND__MSK            0x80
#define BMA222_EN_SUSPEND__REG            BMA222_MODE_CTRL_REG

#define BMA222_INT_MODE_SEL__POS                0
#define BMA222_INT_MODE_SEL__LEN                4
#define BMA222_INT_MODE_SEL__MSK                0x0F
#define BMA222_INT_MODE_SEL__REG                BMA222_INT_CTRL_REG

#define BMA222_LOWG_INT_S__POS          0
#define BMA222_LOWG_INT_S__LEN          1
#define BMA222_LOWG_INT_S__MSK          0x01
#define BMA222_LOWG_INT_S__REG          BMA222_STATUS1_REG

#define BMA222_HIGHG_INT_S__POS          1
#define BMA222_HIGHG_INT_S__LEN          1
#define BMA222_HIGHG_INT_S__MSK          0x02
#define BMA222_HIGHG_INT_S__REG          BMA222_STATUS1_REG

#define BMA222_SLOPE_INT_S__POS          2
#define BMA222_SLOPE_INT_S__LEN          1
#define BMA222_SLOPE_INT_S__MSK          0x04
#define BMA222_SLOPE_INT_S__REG          BMA222_STATUS1_REG

#define BMA222_DOUBLE_TAP_INT_S__POS     4
#define BMA222_DOUBLE_TAP_INT_S__LEN     1
#define BMA222_DOUBLE_TAP_INT_S__MSK     0x10
#define BMA222_DOUBLE_TAP_INT_S__REG     BMA222_STATUS1_REG

#define BMA222_SINGLE_TAP_INT_S__POS     5
#define BMA222_SINGLE_TAP_INT_S__LEN     1
#define BMA222_SINGLE_TAP_INT_S__MSK     0x20
#define BMA222_SINGLE_TAP_INT_S__REG     BMA222_STATUS1_REG

#define BMA222_ORIENT_INT_S__POS         6
#define BMA222_ORIENT_INT_S__LEN         1
#define BMA222_ORIENT_INT_S__MSK         0x40
#define BMA222_ORIENT_INT_S__REG         BMA222_STATUS1_REG

#define BMA222_FLAT_INT_S__POS           7
#define BMA222_FLAT_INT_S__LEN           1
#define BMA222_FLAT_INT_S__MSK           0x80
#define BMA222_FLAT_INT_S__REG           BMA222_STATUS1_REG

#define BMA222_DATA_INT_S__POS           7
#define BMA222_DATA_INT_S__LEN           1
#define BMA222_DATA_INT_S__MSK           0x80
#define BMA222_DATA_INT_S__REG           BMA222_STATUS2_REG

#define BMA222_SLOPE_FIRST_X__POS        0
#define BMA222_SLOPE_FIRST_X__LEN        1
#define BMA222_SLOPE_FIRST_X__MSK        0x01
#define BMA222_SLOPE_FIRST_X__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_SLOPE_FIRST_Y__POS        1
#define BMA222_SLOPE_FIRST_Y__LEN        1
#define BMA222_SLOPE_FIRST_Y__MSK        0x02
#define BMA222_SLOPE_FIRST_Y__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_SLOPE_FIRST_Z__POS        2
#define BMA222_SLOPE_FIRST_Z__LEN        1
#define BMA222_SLOPE_FIRST_Z__MSK        0x04
#define BMA222_SLOPE_FIRST_Z__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_SLOPE_SIGN_S__POS         3
#define BMA222_SLOPE_SIGN_S__LEN         1
#define BMA222_SLOPE_SIGN_S__MSK         0x08
#define BMA222_SLOPE_SIGN_S__REG         BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_TAP_FIRST_X__POS        4
#define BMA222_TAP_FIRST_X__LEN        1
#define BMA222_TAP_FIRST_X__MSK        0x10
#define BMA222_TAP_FIRST_X__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_TAP_FIRST_Y__POS        5
#define BMA222_TAP_FIRST_Y__LEN        1
#define BMA222_TAP_FIRST_Y__MSK        0x20
#define BMA222_TAP_FIRST_Y__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_TAP_FIRST_Z__POS        6
#define BMA222_TAP_FIRST_Z__LEN        1
#define BMA222_TAP_FIRST_Z__MSK        0x40
#define BMA222_TAP_FIRST_Z__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_TAP_FIRST_XYZ__POS        4
#define BMA222_TAP_FIRST_XYZ__LEN        3
#define BMA222_TAP_FIRST_XYZ__MSK        0x70
#define BMA222_TAP_FIRST_XYZ__REG        BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_TAP_SIGN_S__POS         7
#define BMA222_TAP_SIGN_S__LEN         1
#define BMA222_TAP_SIGN_S__MSK         0x80
#define BMA222_TAP_SIGN_S__REG         BMA222_STATUS_TAP_SLOPE_REG

#define BMA222_HIGHG_FIRST_X__POS        0
#define BMA222_HIGHG_FIRST_X__LEN        1
#define BMA222_HIGHG_FIRST_X__MSK        0x01
#define BMA222_HIGHG_FIRST_X__REG        BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_HIGHG_FIRST_Y__POS        1
#define BMA222_HIGHG_FIRST_Y__LEN        1
#define BMA222_HIGHG_FIRST_Y__MSK        0x02
#define BMA222_HIGHG_FIRST_Y__REG        BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_HIGHG_FIRST_Z__POS        2
#define BMA222_HIGHG_FIRST_Z__LEN        1
#define BMA222_HIGHG_FIRST_Z__MSK        0x04
#define BMA222_HIGHG_FIRST_Z__REG        BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_HIGHG_SIGN_S__POS         3
#define BMA222_HIGHG_SIGN_S__LEN         1
#define BMA222_HIGHG_SIGN_S__MSK         0x08
#define BMA222_HIGHG_SIGN_S__REG         BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_ORIENT_S__POS             4
#define BMA222_ORIENT_S__LEN             3
#define BMA222_ORIENT_S__MSK             0x70
#define BMA222_ORIENT_S__REG             BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_FLAT_S__POS               7
#define BMA222_FLAT_S__LEN               1
#define BMA222_FLAT_S__MSK               0x80
#define BMA222_FLAT_S__REG               BMA222_STATUS_ORIENT_HIGH_REG

#define BMA222_EN_SLOPE_X_INT__POS         0
#define BMA222_EN_SLOPE_X_INT__LEN         1
#define BMA222_EN_SLOPE_X_INT__MSK         0x01
#define BMA222_EN_SLOPE_X_INT__REG         BMA222_INT_ENABLE1_REG

#define BMA222_EN_SLOPE_Y_INT__POS         1
#define BMA222_EN_SLOPE_Y_INT__LEN         1
#define BMA222_EN_SLOPE_Y_INT__MSK         0x02
#define BMA222_EN_SLOPE_Y_INT__REG         BMA222_INT_ENABLE1_REG

#define BMA222_EN_SLOPE_Z_INT__POS         2
#define BMA222_EN_SLOPE_Z_INT__LEN         1
#define BMA222_EN_SLOPE_Z_INT__MSK         0x04
#define BMA222_EN_SLOPE_Z_INT__REG         BMA222_INT_ENABLE1_REG

#define BMA222_EN_SLOPE_XYZ_INT__POS         0
#define BMA222_EN_SLOPE_XYZ_INT__LEN         3
#define BMA222_EN_SLOPE_XYZ_INT__MSK         0x07
#define BMA222_EN_SLOPE_XYZ_INT__REG         BMA222_INT_ENABLE1_REG

#define BMA222_EN_DOUBLE_TAP_INT__POS      4
#define BMA222_EN_DOUBLE_TAP_INT__LEN      1
#define BMA222_EN_DOUBLE_TAP_INT__MSK      0x10
#define BMA222_EN_DOUBLE_TAP_INT__REG      BMA222_INT_ENABLE1_REG

#define BMA222_EN_SINGLE_TAP_INT__POS      5
#define BMA222_EN_SINGLE_TAP_INT__LEN      1
#define BMA222_EN_SINGLE_TAP_INT__MSK      0x20
#define BMA222_EN_SINGLE_TAP_INT__REG      BMA222_INT_ENABLE1_REG

#define BMA222_EN_ORIENT_INT__POS          6
#define BMA222_EN_ORIENT_INT__LEN          1
#define BMA222_EN_ORIENT_INT__MSK          0x40
#define BMA222_EN_ORIENT_INT__REG          BMA222_INT_ENABLE1_REG

#define BMA222_EN_FLAT_INT__POS            7
#define BMA222_EN_FLAT_INT__LEN            1
#define BMA222_EN_FLAT_INT__MSK            0x80
#define BMA222_EN_FLAT_INT__REG            BMA222_INT_ENABLE1_REG

#define BMA222_EN_HIGHG_X_INT__POS         0
#define BMA222_EN_HIGHG_X_INT__LEN         1
#define BMA222_EN_HIGHG_X_INT__MSK         0x01
#define BMA222_EN_HIGHG_X_INT__REG         BMA222_INT_ENABLE2_REG

#define BMA222_EN_HIGHG_Y_INT__POS         1
#define BMA222_EN_HIGHG_Y_INT__LEN         1
#define BMA222_EN_HIGHG_Y_INT__MSK         0x02
#define BMA222_EN_HIGHG_Y_INT__REG         BMA222_INT_ENABLE2_REG

#define BMA222_EN_HIGHG_Z_INT__POS         2
#define BMA222_EN_HIGHG_Z_INT__LEN         1
#define BMA222_EN_HIGHG_Z_INT__MSK         0x04
#define BMA222_EN_HIGHG_Z_INT__REG         BMA222_INT_ENABLE2_REG

#define BMA222_EN_HIGHG_XYZ_INT__POS         2
#define BMA222_EN_HIGHG_XYZ_INT__LEN         1
#define BMA222_EN_HIGHG_XYZ_INT__MSK         0x04
#define BMA222_EN_HIGHG_XYZ_INT__REG         BMA222_INT_ENABLE2_REG

#define BMA222_EN_LOWG_INT__POS            3
#define BMA222_EN_LOWG_INT__LEN            1
#define BMA222_EN_LOWG_INT__MSK            0x08
#define BMA222_EN_LOWG_INT__REG            BMA222_INT_ENABLE2_REG

#define BMA222_EN_NEW_DATA_INT__POS        4
#define BMA222_EN_NEW_DATA_INT__LEN        1
#define BMA222_EN_NEW_DATA_INT__MSK        0x10
#define BMA222_EN_NEW_DATA_INT__REG        BMA222_INT_ENABLE2_REG

#define BMA222_EN_INT1_PAD_LOWG__POS        0
#define BMA222_EN_INT1_PAD_LOWG__LEN        1
#define BMA222_EN_INT1_PAD_LOWG__MSK        0x01
#define BMA222_EN_INT1_PAD_LOWG__REG        BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_HIGHG__POS       1
#define BMA222_EN_INT1_PAD_HIGHG__LEN       1
#define BMA222_EN_INT1_PAD_HIGHG__MSK       0x02
#define BMA222_EN_INT1_PAD_HIGHG__REG       BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_SLOPE__POS       2
#define BMA222_EN_INT1_PAD_SLOPE__LEN       1
#define BMA222_EN_INT1_PAD_SLOPE__MSK       0x04
#define BMA222_EN_INT1_PAD_SLOPE__REG       BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_DB_TAP__POS      4
#define BMA222_EN_INT1_PAD_DB_TAP__LEN      1
#define BMA222_EN_INT1_PAD_DB_TAP__MSK      0x10
#define BMA222_EN_INT1_PAD_DB_TAP__REG      BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_SNG_TAP__POS     5
#define BMA222_EN_INT1_PAD_SNG_TAP__LEN     1
#define BMA222_EN_INT1_PAD_SNG_TAP__MSK     0x20
#define BMA222_EN_INT1_PAD_SNG_TAP__REG     BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_ORIENT__POS      6
#define BMA222_EN_INT1_PAD_ORIENT__LEN      1
#define BMA222_EN_INT1_PAD_ORIENT__MSK      0x40
#define BMA222_EN_INT1_PAD_ORIENT__REG      BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_FLAT__POS        7
#define BMA222_EN_INT1_PAD_FLAT__LEN        1
#define BMA222_EN_INT1_PAD_FLAT__MSK        0x80
#define BMA222_EN_INT1_PAD_FLAT__REG        BMA222_INT1_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_LOWG__POS        0
#define BMA222_EN_INT2_PAD_LOWG__LEN        1
#define BMA222_EN_INT2_PAD_LOWG__MSK        0x01
#define BMA222_EN_INT2_PAD_LOWG__REG        BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_HIGHG__POS       1
#define BMA222_EN_INT2_PAD_HIGHG__LEN       1
#define BMA222_EN_INT2_PAD_HIGHG__MSK       0x02
#define BMA222_EN_INT2_PAD_HIGHG__REG       BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_SLOPE__POS       2
#define BMA222_EN_INT2_PAD_SLOPE__LEN       1
#define BMA222_EN_INT2_PAD_SLOPE__MSK       0x04
#define BMA222_EN_INT2_PAD_SLOPE__REG       BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_DB_TAP__POS      4
#define BMA222_EN_INT2_PAD_DB_TAP__LEN      1
#define BMA222_EN_INT2_PAD_DB_TAP__MSK      0x10
#define BMA222_EN_INT2_PAD_DB_TAP__REG      BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_SNG_TAP__POS     5
#define BMA222_EN_INT2_PAD_SNG_TAP__LEN     1
#define BMA222_EN_INT2_PAD_SNG_TAP__MSK     0x20
#define BMA222_EN_INT2_PAD_SNG_TAP__REG     BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_ORIENT__POS      6
#define BMA222_EN_INT2_PAD_ORIENT__LEN      1
#define BMA222_EN_INT2_PAD_ORIENT__MSK      0x40
#define BMA222_EN_INT2_PAD_ORIENT__REG      BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT2_PAD_FLAT__POS        7
#define BMA222_EN_INT2_PAD_FLAT__LEN        1
#define BMA222_EN_INT2_PAD_FLAT__MSK        0x80
#define BMA222_EN_INT2_PAD_FLAT__REG        BMA222_INT2_PAD_SEL_REG

#define BMA222_EN_INT1_PAD_NEWDATA__POS     0
#define BMA222_EN_INT1_PAD_NEWDATA__LEN     1
#define BMA222_EN_INT1_PAD_NEWDATA__MSK     0x01
#define BMA222_EN_INT1_PAD_NEWDATA__REG     BMA222_INT_DATA_SEL_REG

#define BMA222_EN_INT2_PAD_NEWDATA__POS     7
#define BMA222_EN_INT2_PAD_NEWDATA__LEN     1
#define BMA222_EN_INT2_PAD_NEWDATA__MSK     0x80
#define BMA222_EN_INT2_PAD_NEWDATA__REG     BMA222_INT_DATA_SEL_REG


#define BMA222_UNFILT_INT_SRC_LOWG__POS        0
#define BMA222_UNFILT_INT_SRC_LOWG__LEN        1
#define BMA222_UNFILT_INT_SRC_LOWG__MSK        0x01
#define BMA222_UNFILT_INT_SRC_LOWG__REG        BMA222_INT_SRC_REG

#define BMA222_UNFILT_INT_SRC_HIGHG__POS       1
#define BMA222_UNFILT_INT_SRC_HIGHG__LEN       1
#define BMA222_UNFILT_INT_SRC_HIGHG__MSK       0x02
#define BMA222_UNFILT_INT_SRC_HIGHG__REG       BMA222_INT_SRC_REG

#define BMA222_UNFILT_INT_SRC_SLOPE__POS       2
#define BMA222_UNFILT_INT_SRC_SLOPE__LEN       1
#define BMA222_UNFILT_INT_SRC_SLOPE__MSK       0x04
#define BMA222_UNFILT_INT_SRC_SLOPE__REG       BMA222_INT_SRC_REG

#define BMA222_UNFILT_INT_SRC_TAP__POS         4
#define BMA222_UNFILT_INT_SRC_TAP__LEN         1
#define BMA222_UNFILT_INT_SRC_TAP__MSK         0x10
#define BMA222_UNFILT_INT_SRC_TAP__REG         BMA222_INT_SRC_REG

#define BMA222_UNFILT_INT_SRC_DATA__POS        5
#define BMA222_UNFILT_INT_SRC_DATA__LEN        1
#define BMA222_UNFILT_INT_SRC_DATA__MSK        0x20
#define BMA222_UNFILT_INT_SRC_DATA__REG        BMA222_INT_SRC_REG

#define BMA222_INT1_PAD_ACTIVE_LEVEL__POS       0
#define BMA222_INT1_PAD_ACTIVE_LEVEL__LEN       1
#define BMA222_INT1_PAD_ACTIVE_LEVEL__MSK       0x01
#define BMA222_INT1_PAD_ACTIVE_LEVEL__REG       BMA222_INT_SET_REG

#define BMA222_INT2_PAD_ACTIVE_LEVEL__POS       2
#define BMA222_INT2_PAD_ACTIVE_LEVEL__LEN       1
#define BMA222_INT2_PAD_ACTIVE_LEVEL__MSK       0x04
#define BMA222_INT2_PAD_ACTIVE_LEVEL__REG       BMA222_INT_SET_REG

#define BMA222_INT1_PAD_OUTPUT_TYPE__POS        1
#define BMA222_INT1_PAD_OUTPUT_TYPE__LEN        1
#define BMA222_INT1_PAD_OUTPUT_TYPE__MSK        0x02
#define BMA222_INT1_PAD_OUTPUT_TYPE__REG        BMA222_INT_SET_REG

#define BMA222_INT2_PAD_OUTPUT_TYPE__POS        3
#define BMA222_INT2_PAD_OUTPUT_TYPE__LEN        1
#define BMA222_INT2_PAD_OUTPUT_TYPE__MSK        0x08
#define BMA222_INT2_PAD_OUTPUT_TYPE__REG        BMA222_INT_SET_REG


#define BMA222_INT_MODE_SEL__POS                0
#define BMA222_INT_MODE_SEL__LEN                4
#define BMA222_INT_MODE_SEL__MSK                0x0F
#define BMA222_INT_MODE_SEL__REG                BMA222_INT_CTRL_REG


#define BMA222_INT_RESET_LATCHED__POS           7
#define BMA222_INT_RESET_LATCHED__LEN           1
#define BMA222_INT_RESET_LATCHED__MSK           0x80
#define BMA222_INT_RESET_LATCHED__REG           BMA222_INT_CTRL_REG

#define BMA222_LOWG_DUR__POS                    0
#define BMA222_LOWG_DUR__LEN                    8
#define BMA222_LOWG_DUR__MSK                    0xFF
#define BMA222_LOWG_DUR__REG                    BMA222_LOW_DURN_REG

#define BMA222_LOWG_THRES__POS                  0
#define BMA222_LOWG_THRES__LEN                  8
#define BMA222_LOWG_THRES__MSK                  0xFF
#define BMA222_LOWG_THRES__REG                  BMA222_LOW_THRES_REG

#define BMA222_LOWG_HYST__POS                   0
#define BMA222_LOWG_HYST__LEN                   2
#define BMA222_LOWG_HYST__MSK                   0x03
#define BMA222_LOWG_HYST__REG                   BMA222_LOW_HIGH_HYST_REG

#define BMA222_LOWG_INT_MODE__POS               2
#define BMA222_LOWG_INT_MODE__LEN               1
#define BMA222_LOWG_INT_MODE__MSK               0x04
#define BMA222_LOWG_INT_MODE__REG               BMA222_LOW_HIGH_HYST_REG

#define BMA222_HIGHG_DUR__POS                    0
#define BMA222_HIGHG_DUR__LEN                    8
#define BMA222_HIGHG_DUR__MSK                    0xFF
#define BMA222_HIGHG_DUR__REG                    BMA222_HIGH_DURN_REG

#define BMA222_HIGHG_THRES__POS                  0
#define BMA222_HIGHG_THRES__LEN                  8
#define BMA222_HIGHG_THRES__MSK                  0xFF
#define BMA222_HIGHG_THRES__REG                  BMA222_HIGH_THRES_REG

#define BMA222_HIGHG_HYST__POS                  6
#define BMA222_HIGHG_HYST__LEN                  2
#define BMA222_HIGHG_HYST__MSK                  0xC0
#define BMA222_HIGHG_HYST__REG                  BMA222_LOW_HIGH_HYST_REG

#define BMA222_SLOPE_DUR__POS                    0
#define BMA222_SLOPE_DUR__LEN                    2
#define BMA222_SLOPE_DUR__MSK                    0x03
#define BMA222_SLOPE_DUR__REG                    BMA222_SLOPE_DURN_REG

#define BMA222_SLOPE_THRES__POS                  0
#define BMA222_SLOPE_THRES__LEN                  8
#define BMA222_SLOPE_THRES__MSK                  0xFF
#define BMA222_SLOPE_THRES__REG                  BMA222_SLOPE_THRES_REG

#define BMA222_TAP_DUR__POS                    0
#define BMA222_TAP_DUR__LEN                    3
#define BMA222_TAP_DUR__MSK                    0x07
#define BMA222_TAP_DUR__REG                    BMA222_TAP_PARAM_REG

#define BMA222_TAP_SHOCK_DURN__POS             6
#define BMA222_TAP_SHOCK_DURN__LEN             1
#define BMA222_TAP_SHOCK_DURN__MSK             0x40
#define BMA222_TAP_SHOCK_DURN__REG             BMA222_TAP_PARAM_REG

#define BMA222_TAP_QUIET_DURN__POS             7
#define BMA222_TAP_QUIET_DURN__LEN             1
#define BMA222_TAP_QUIET_DURN__MSK             0x80
#define BMA222_TAP_QUIET_DURN__REG             BMA222_TAP_PARAM_REG

#define BMA222_TAP_THRES__POS                  0
#define BMA222_TAP_THRES__LEN                  5
#define BMA222_TAP_THRES__MSK                  0x1F
#define BMA222_TAP_THRES__REG                  BMA222_TAP_THRES_REG

#define BMA222_TAP_SAMPLES__POS                6
#define BMA222_TAP_SAMPLES__LEN                2
#define BMA222_TAP_SAMPLES__MSK                0xC0
#define BMA222_TAP_SAMPLES__REG                BMA222_TAP_THRES_REG

#define BMA222_ORIENT_MODE__POS                  0
#define BMA222_ORIENT_MODE__LEN                  2
#define BMA222_ORIENT_MODE__MSK                  0x03
#define BMA222_ORIENT_MODE__REG                  BMA222_ORIENT_PARAM_REG

#define BMA222_ORIENT_BLOCK__POS                 2
#define BMA222_ORIENT_BLOCK__LEN                 2
#define BMA222_ORIENT_BLOCK__MSK                 0x0C
#define BMA222_ORIENT_BLOCK__REG                 BMA222_ORIENT_PARAM_REG

#define BMA222_ORIENT_HYST__POS                  4
#define BMA222_ORIENT_HYST__LEN                  3
#define BMA222_ORIENT_HYST__MSK                  0x70
#define BMA222_ORIENT_HYST__REG                  BMA222_ORIENT_PARAM_REG

#define BMA222_ORIENT_AXIS__POS                  7
#define BMA222_ORIENT_AXIS__LEN                  1
#define BMA222_ORIENT_AXIS__MSK                  0x80
#define BMA222_ORIENT_AXIS__REG                  BMA222_THETA_BLOCK_REG

#define BMA222_THETA_BLOCK__POS                  0
#define BMA222_THETA_BLOCK__LEN                  6
#define BMA222_THETA_BLOCK__MSK                  0x3F
#define BMA222_THETA_BLOCK__REG                  BMA222_THETA_BLOCK_REG

#define BMA222_THETA_FLAT__POS                  0
#define BMA222_THETA_FLAT__LEN                  6
#define BMA222_THETA_FLAT__MSK                  0x3F
#define BMA222_THETA_FLAT__REG                  BMA222_THETA_FLAT_REG

#define BMA222_FLAT_HOLD_TIME__POS              4
#define BMA222_FLAT_HOLD_TIME__LEN              2
#define BMA222_FLAT_HOLD_TIME__MSK              0x30
#define BMA222_FLAT_HOLD_TIME__REG              BMA222_FLAT_HOLD_TIME_REG

#define BMA222_EN_SELF_TEST__POS                0
#define BMA222_EN_SELF_TEST__LEN                2
#define BMA222_EN_SELF_TEST__MSK                0x03
#define BMA222_EN_SELF_TEST__REG                BMA222_SELF_TEST_REG



#define BMA222_NEG_SELF_TEST__POS               2
#define BMA222_NEG_SELF_TEST__LEN               1
#define BMA222_NEG_SELF_TEST__MSK               0x04
#define BMA222_NEG_SELF_TEST__REG               BMA222_SELF_TEST_REG


#define BMA222_LOW_POWER_MODE_S__POS            0
#define BMA222_LOW_POWER_MODE_S__LEN            1
#define BMA222_LOW_POWER_MODE_S__MSK            0x01
#define BMA222_LOW_POWER_MODE_S__REG            BMA222_STATUS_LOW_POWER_REG

#define BMA222_EN_FAST_COMP__POS                5
#define BMA222_EN_FAST_COMP__LEN                2
#define BMA222_EN_FAST_COMP__MSK                0x60
#define BMA222_EN_FAST_COMP__REG                BMA222_OFFSET_CTRL_REG

#define BMA222_FAST_COMP_RDY_S__POS             4
#define BMA222_FAST_COMP_RDY_S__LEN             1
#define BMA222_FAST_COMP_RDY_S__MSK             0x10
#define BMA222_FAST_COMP_RDY_S__REG             BMA222_OFFSET_CTRL_REG

#define BMA222_COMP_TARGET_OFFSET_X__POS        1
#define BMA222_COMP_TARGET_OFFSET_X__LEN        2
#define BMA222_COMP_TARGET_OFFSET_X__MSK        0x06
#define BMA222_COMP_TARGET_OFFSET_X__REG        BMA222_OFFSET_PARAMS_REG

#define BMA222_COMP_TARGET_OFFSET_Y__POS        3
#define BMA222_COMP_TARGET_OFFSET_Y__LEN        2
#define BMA222_COMP_TARGET_OFFSET_Y__MSK        0x18
#define BMA222_COMP_TARGET_OFFSET_Y__REG        BMA222_OFFSET_PARAMS_REG

#define BMA222_COMP_TARGET_OFFSET_Z__POS        5
#define BMA222_COMP_TARGET_OFFSET_Z__LEN        2
#define BMA222_COMP_TARGET_OFFSET_Z__MSK        0x60
#define BMA222_COMP_TARGET_OFFSET_Z__REG        BMA222_OFFSET_PARAMS_REG

#define BMA222_RANGE_2G                 0
#define BMA222_RANGE_4G                 1
#define BMA222_RANGE_8G                 2
#define BMA222_RANGE_16G                3

#define BMA222_BW_7_81HZ        0x08
#define BMA222_BW_15_63HZ       0x09
#define BMA222_BW_31_25HZ       0x0A
#define BMA222_BW_62_50HZ       0x0B
#define BMA222_BW_125HZ         0x0C
#define BMA222_BW_250HZ         0x0D
#define BMA222_BW_500HZ         0x0E
#define BMA222_BW_1000HZ        0x0F

#define BMA222_MODE_NORMAL      0
#define BMA222_MODE_LOWPOWER    1
#define BMA222_MODE_SUSPEND     2


#define BMA222_GET_BITSLICE(regvar, bitname)\
	((regvar & bitname##__MSK) >> bitname##__POS)


#define BMA222_SET_BITSLICE(regvar, bitname, val)\
	((regvar & ~bitname##__MSK) | ((val<<bitname##__POS)&bitname##__MSK))


struct bma222acc{
	s16	x,
		y,
		z;
} ;

struct gsen_platform_data{
	int	rotator[9];
	};

struct bma222_data {
	struct i2c_client *bma222_client;
	atomic_t delay;
	atomic_t enable;
	atomic_t selftest_result;
	unsigned char mode;
	struct input_dev *input;
	struct bma222acc value;
	struct mutex value_mutex;
	struct mutex enable_mutex;
	struct mutex mode_mutex;
	struct delayed_work work;
	struct work_struct irq_work;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int IRQ;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma222_early_suspend(struct early_suspend *h);
static void bma222_late_resume(struct early_suspend *h);
#endif

static int bma222_smbus_read_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_read_byte_data(client, reg_addr);
	if (dummy < 0)
		return -1;
	*data = dummy & 0x000000ff;

	return 0;
}

static int bma222_smbus_write_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_write_byte_data(client, reg_addr, *data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma222_smbus_read_byte_block(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;
	dummy = i2c_smbus_read_i2c_block_data(client, reg_addr, len, data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma222_set_mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data1;


	if (Mode < 3) {
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_LOW_POWER__REG, &data1);
		switch (Mode) {
		case BMA222_MODE_NORMAL:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_LOW_POWER, 0);
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_SUSPEND, 0);
			break;
		case BMA222_MODE_LOWPOWER:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_LOW_POWER, 1);
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_SUSPEND, 0);
			break;
		case BMA222_MODE_SUSPEND:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_LOW_POWER, 0);
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_EN_SUSPEND, 1);
			break;
		default:
			break;
		}

		comres += bma222_smbus_write_byte(client,
				BMA222_EN_LOW_POWER__REG, &data1);
	} else{
		comres = -1;
	}


	return comres;
}
#ifdef BMA222_ENABLE_INT1
static int bma222_set_int1_pad_sel(struct i2c_client *client, unsigned char
		int1sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state;
	state = 0x01;


	switch (int1sel) {
	case 0:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_LOWG__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_LOWG,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_HIGHG__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_HIGHG,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_SLOPE__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_SLOPE,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_DB_TAP__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_DB_TAP,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_SNG_TAP__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_SNG_TAP,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_ORIENT__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_ORIENT,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT1_PAD_FLAT__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT1_PAD_FLAT,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT1_PAD_FLAT__REG, &data);
		break;
	default:
		break;
	}

	return comres;
}
#endif /* BMA222_ENABLE_INT1 */
#ifdef BMA222_ENABLE_INT2
static int bma222_set_int2_pad_sel(struct i2c_client *client, unsigned char
		int2sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state;
	state = 0x01;


	switch (int2sel) {
	case 0:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_LOWG__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_LOWG,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_HIGHG__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_HIGHG,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_SLOPE__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_SLOPE,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_DB_TAP__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_DB_TAP,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_SNG_TAP__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_SNG_TAP,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_ORIENT__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_ORIENT,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma222_smbus_read_byte(client,
				BMA222_EN_INT2_PAD_FLAT__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_EN_INT2_PAD_FLAT,
				state);
		comres = bma222_smbus_write_byte(client,
				BMA222_EN_INT2_PAD_FLAT__REG, &data);
		break;
	default:
		break;
	}

	return comres;
}
#endif /* BMA222_ENABLE_INT2 */

static int bma222_set_Int_Enable(struct i2c_client *client, unsigned char
		InterruptType , unsigned char value)
{
	int comres = 0;
	unsigned char data1, data2;


	comres = bma222_smbus_read_byte(client, BMA222_INT_ENABLE1_REG, &data1);
	comres = bma222_smbus_read_byte(client, BMA222_INT_ENABLE2_REG, &data2);

	value = value & 1;
	switch (InterruptType) {
	case 0:
		/* Low G Interrupt  */
		data2 = BMA222_SET_BITSLICE(data2, BMA222_EN_LOWG_INT, value);
		break;
	case 1:
		/* High G X Interrupt */

		data2 = BMA222_SET_BITSLICE(data2, BMA222_EN_HIGHG_X_INT,
				value);
		break;
	case 2:
		/* High G Y Interrupt */

		data2 = BMA222_SET_BITSLICE(data2, BMA222_EN_HIGHG_Y_INT,
				value);
		break;
	case 3:
		/* High G Z Interrupt */

		data2 = BMA222_SET_BITSLICE(data2, BMA222_EN_HIGHG_Z_INT,
				value);
		break;
	case 4:
		/* New Data Interrupt  */

		data2 = BMA222_SET_BITSLICE(data2, BMA222_EN_NEW_DATA_INT,
				value);
		break;
	case 5:
		/* Slope X Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_SLOPE_X_INT,
				value);
		break;
	case 6:
		/* Slope Y Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_SLOPE_Y_INT,
				value);
		break;
	case 7:
		/* Slope Z Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_SLOPE_Z_INT,
				value);
		break;
	case 8:
		/* Single Tap Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_SINGLE_TAP_INT,
				value);
		break;
	case 9:
		/* Double Tap Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_DOUBLE_TAP_INT,
				value);
		break;
	case 10:
		/* Orient Interrupt  */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_ORIENT_INT, value);
		break;
	case 11:
		/* Flat Interrupt */

		data1 = BMA222_SET_BITSLICE(data1, BMA222_EN_FLAT_INT, value);
		break;
	default:
		break;
	}
	comres = bma222_smbus_write_byte(client, BMA222_INT_ENABLE1_REG,
			&data1);
	comres = bma222_smbus_write_byte(client, BMA222_INT_ENABLE2_REG,
			&data2);

	return comres;
}


static int bma222_get_mode(struct i2c_client *client, unsigned char *Mode)
{
	int comres = 0;


	comres = bma222_smbus_read_byte(client,
			BMA222_EN_LOW_POWER__REG, Mode);
	*Mode  = (*Mode) >> 6;


	return comres;
}

static int bma222_set_range(struct i2c_client *client, unsigned char Range)
{
	int comres = 0;
	unsigned char data1;


	if (Range < 4) {
		comres = bma222_smbus_read_byte(client,
				BMA222_RANGE_SEL_REG, &data1);
		switch (Range) {
		case 0:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_RANGE_SEL, 0);
			break;
		case 1:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_RANGE_SEL, 5);
			break;
		case 2:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_RANGE_SEL, 8);
			break;
		case 3:
			data1  = BMA222_SET_BITSLICE(data1,
					BMA222_RANGE_SEL, 12);
			break;
		default:
			break;
		}
		comres += bma222_smbus_write_byte(client,
				BMA222_RANGE_SEL_REG, &data1);
	} else{
		comres = -1;
	}


	return comres;
}

static int bma222_get_range(struct i2c_client *client, unsigned char *Range)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client, BMA222_RANGE_SEL__REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_RANGE_SEL);
	*Range = data;


	return comres;
}


static int bma222_set_bandwidth(struct i2c_client *client, unsigned char BW)
{
	int comres = 0;
	unsigned char data;
	int Bandwidth = 0;


	if (BW < 8) {
		switch (BW) {
		case 0:
			Bandwidth = BMA222_BW_7_81HZ;
			break;
		case 1:
			Bandwidth = BMA222_BW_15_63HZ;
			break;
		case 2:
			Bandwidth = BMA222_BW_31_25HZ;
			break;
		case 3:
			Bandwidth = BMA222_BW_62_50HZ;
			break;
		case 4:
			Bandwidth = BMA222_BW_125HZ;
			break;
		case 5:
			Bandwidth = BMA222_BW_250HZ;
			break;
		case 6:
			Bandwidth = BMA222_BW_500HZ;
			break;
		case 7:
			Bandwidth = BMA222_BW_1000HZ;
			break;
		default:
			break;
		}
		comres = bma222_smbus_read_byte(client,
				BMA222_BANDWIDTH__REG, &data);
		data = BMA222_SET_BITSLICE(data, BMA222_BANDWIDTH,
				Bandwidth);
		comres += bma222_smbus_write_byte(client,
				BMA222_BANDWIDTH__REG, &data);
	} else{
		comres = -1;
	}


	return comres;
}

static int bma222_get_bandwidth(struct i2c_client *client, unsigned char *BW)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client, BMA222_BANDWIDTH__REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_BANDWIDTH);
	if (data <= 8) {
		*BW = 0;
	} else{
		if (data >= 0x0F)
			*BW = 7;
		else
			*BW = data - 8;

	}


	return comres;
}

#if defined(BMA222_ENABLE_INT1) || defined(BMA222_ENABLE_INT2)
static int bma222_get_interruptstatus1(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_STATUS1_REG, &data);
	*intstatus = data;

	return comres;
}


static int bma222_get_HIGH_first(struct i2c_client *client, unsigned char
						param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma222_get_HIGH_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_SIGN_S);
	*intstatus = data;

	return comres;
}


static int bma222_get_slope_first(struct i2c_client *client, unsigned char
	param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_TAP_SLOPE_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_TAP_SLOPE_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma222_smbus_read_byte(client,
				BMA222_STATUS_TAP_SLOPE_REG, &data);
		data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma222_get_slope_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_STATUS_TAP_SLOPE_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_SIGN_S);
	*intstatus = data;

	return comres;
}

static int bma222_get_orient_status(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_ORIENT_S);
	*intstatus = data;

	return comres;
}

static int bma222_get_orient_flat_status(struct i2c_client *client, unsigned
		char *intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_FLAT_S);
	*intstatus = data;

	return comres;
}
#endif /* defined(BMA222_ENABLE_INT1)||defined(BMA222_ENABLE_INT2) */
static int bma222_set_Int_Mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_INT_MODE_SEL__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_INT_MODE_SEL, Mode);
	comres = bma222_smbus_write_byte(client,
			BMA222_INT_MODE_SEL__REG, &data);


	return comres;
}

static int bma222_get_Int_Mode(struct i2c_client *client, unsigned char *Mode)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_INT_MODE_SEL__REG, &data);
	data  = BMA222_GET_BITSLICE(data, BMA222_INT_MODE_SEL);
	*Mode = data;


	return comres;
}
static int bma222_set_slope_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_SLOPE_DUR__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_SLOPE_DUR, duration);
	comres = bma222_smbus_write_byte(client,
			BMA222_SLOPE_DUR__REG, &data);


	return comres;
}

static int bma222_get_slope_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_SLOPE_DURN_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_DUR);
	*status = data;


	return comres;
}

static int bma222_set_slope_threshold(struct i2c_client *client,
		unsigned char threshold)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_SLOPE_THRES__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_SLOPE_THRES, threshold);
	comres = bma222_smbus_write_byte(client,
			BMA222_SLOPE_THRES__REG, &data);


	return comres;
}

static int bma222_get_slope_threshold(struct i2c_client *client,
		unsigned char *status)
{
	int comres = 0;
	unsigned char data;


	comres = bma222_smbus_read_byte(client,
			BMA222_SLOPE_THRES_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_SLOPE_THRES);
	*status = data;


	return comres;
}
static int bma222_set_low_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_LOWG_DUR__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_LOWG_DUR, duration);
	comres = bma222_smbus_write_byte(client, BMA222_LOWG_DUR__REG, &data);

	return comres;
}

static int bma222_get_low_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_LOW_DURN_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_LOWG_DUR);
	*status = data;

	return comres;
}

static int bma222_set_low_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_LOWG_THRES__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_LOWG_THRES, threshold);
	comres = bma222_smbus_write_byte(client, BMA222_LOWG_THRES__REG, &data);

	return comres;
}

static int bma222_get_low_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_LOW_THRES_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_LOWG_THRES);
	*status = data;

	return comres;
}

static int bma222_set_high_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_HIGHG_DUR__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_HIGHG_DUR, duration);
	comres = bma222_smbus_write_byte(client, BMA222_HIGHG_DUR__REG, &data);

	return comres;
}

static int bma222_get_high_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_HIGH_DURN_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_DUR);
	*status = data;

	return comres;
}

static int bma222_set_high_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_HIGHG_THRES__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_HIGHG_THRES, threshold);
	comres = bma222_smbus_write_byte(client, BMA222_HIGHG_THRES__REG,
			&data);

	return comres;
}

static int bma222_get_high_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_HIGH_THRES_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_HIGHG_THRES);
	*status = data;

	return comres;
}


static int bma222_set_tap_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_DUR__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_TAP_DUR, duration);
	comres = bma222_smbus_write_byte(client, BMA222_TAP_DUR__REG, &data);

	return comres;
}

static int bma222_get_tap_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_TAP_DUR);
	*status = data;

	return comres;
}

static int bma222_set_tap_shock(struct i2c_client *client, unsigned char setval)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_SHOCK_DURN__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_TAP_SHOCK_DURN, setval);
	comres = bma222_smbus_write_byte(client, BMA222_TAP_SHOCK_DURN__REG,
			&data);

	return comres;
}

static int bma222_get_tap_shock(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_TAP_SHOCK_DURN);
	*status = data;

	return comres;
}

static int bma222_set_tap_quiet(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_QUIET_DURN__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_TAP_QUIET_DURN, duration);
	comres = bma222_smbus_write_byte(client, BMA222_TAP_QUIET_DURN__REG,
			&data);

	return comres;
}

static int bma222_get_tap_quiet(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_TAP_QUIET_DURN);
	*status = data;

	return comres;
}

static int bma222_set_tap_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_THRES__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_TAP_THRES, threshold);
	comres = bma222_smbus_write_byte(client, BMA222_TAP_THRES__REG, &data);

	return comres;
}

static int bma222_get_tap_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_THRES_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_TAP_THRES);
	*status = data;

	return comres;
}

static int bma222_set_tap_samp(struct i2c_client *client, unsigned char samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_SAMPLES__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_TAP_SAMPLES, samp);
	comres = bma222_smbus_write_byte(client, BMA222_TAP_SAMPLES__REG,
			&data);

	return comres;
}

static int bma222_get_tap_samp(struct i2c_client *client, unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_TAP_THRES_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_TAP_SAMPLES);
	*status = data;

	return comres;
}

static int bma222_set_orient_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_MODE__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_ORIENT_MODE, mode);
	comres = bma222_smbus_write_byte(client, BMA222_ORIENT_MODE__REG,
			&data);

	return comres;
}

static int bma222_get_orient_mode(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_ORIENT_MODE);
	*status = data;

	return comres;
}

static int bma222_set_orient_blocking(struct i2c_client *client, unsigned char
		samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_BLOCK__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_ORIENT_BLOCK, samp);
	comres = bma222_smbus_write_byte(client, BMA222_ORIENT_BLOCK__REG,
			&data);

	return comres;
}

static int bma222_get_orient_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_ORIENT_BLOCK);
	*status = data;

	return comres;
}

static int bma222_set_orient_hyst(struct i2c_client *client, unsigned char
		orienthyst)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_HYST__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_ORIENT_HYST, orienthyst);
	comres = bma222_smbus_write_byte(client, BMA222_ORIENT_HYST__REG,
			&data);

	return comres;
}

static int bma222_get_orient_hyst(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_ORIENT_PARAM_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_ORIENT_HYST);
	*status = data;

	return comres;
}
static int bma222_set_theta_blocking(struct i2c_client *client, unsigned char
		thetablk)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_THETA_BLOCK__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_THETA_BLOCK, thetablk);
	comres = bma222_smbus_write_byte(client, BMA222_THETA_BLOCK__REG,
			&data);

	return comres;
}

static int bma222_get_theta_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_THETA_BLOCK_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_THETA_BLOCK);
	*status = data;

	return comres;
}

static int bma222_set_theta_flat(struct i2c_client *client, unsigned char
		thetaflat)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_THETA_FLAT__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_THETA_FLAT, thetaflat);
	comres = bma222_smbus_write_byte(client, BMA222_THETA_FLAT__REG, &data);

	return comres;
}

static int bma222_get_theta_flat(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0 ;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_THETA_FLAT_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_THETA_FLAT);
	*status = data;

	return comres;
}

static int bma222_set_flat_hold_time(struct i2c_client *client, unsigned char
		holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_FLAT_HOLD_TIME__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_FLAT_HOLD_TIME, holdtime);
	comres = bma222_smbus_write_byte(client, BMA222_FLAT_HOLD_TIME__REG,
			&data);

	return comres;
}

static int bma222_get_flat_hold_time(struct i2c_client *client, unsigned char
		*holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_FLAT_HOLD_TIME_REG,
			&data);
	data  = BMA222_GET_BITSLICE(data, BMA222_FLAT_HOLD_TIME);
	*holdtime = data ;

	return comres;
}

static int bma222_write_reg(struct i2c_client *client, unsigned char addr,
		unsigned char *data)
{
	int comres = 0;
	comres = bma222_smbus_write_byte(client, addr, data);

	return comres;
}


static int bma222_set_offset_target_x(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client,
			BMA222_COMP_TARGET_OFFSET_X__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_X,
			offsettarget);
	comres = bma222_smbus_write_byte(client,
			BMA222_COMP_TARGET_OFFSET_X__REG, &data);

	return comres;
}

static int bma222_get_offset_target_x(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0 ;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_OFFSET_PARAMS_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_X);
	*offsettarget = data;

	return comres;
}

static int bma222_set_offset_target_y(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client,
			BMA222_COMP_TARGET_OFFSET_Y__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_Y,
			offsettarget);
	comres = bma222_smbus_write_byte(client,
			BMA222_COMP_TARGET_OFFSET_Y__REG, &data);

	return comres;
}

static int bma222_get_offset_target_y(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_OFFSET_PARAMS_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_Y);
	*offsettarget = data;

	return comres;
}

static int bma222_set_offset_target_z(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client,
			BMA222_COMP_TARGET_OFFSET_Z__REG, &data);
	data = BMA222_SET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_Z,
			offsettarget);
	comres = bma222_smbus_write_byte(client,
			BMA222_COMP_TARGET_OFFSET_Z__REG, &data);

	return comres;
}

static int bma222_get_offset_target_z(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_OFFSET_PARAMS_REG,
			&data);
	data = BMA222_GET_BITSLICE(data, BMA222_COMP_TARGET_OFFSET_Z);
	*offsettarget = data;

	return comres;
}

static int bma222_get_cal_ready(struct i2c_client *client, unsigned char
		*calrdy)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_OFFSET_CTRL_REG, &data);
	data = BMA222_GET_BITSLICE(data, BMA222_FAST_COMP_RDY_S);
	*calrdy = data;

	return comres;
}

static int bma222_set_cal_trigger(struct i2c_client *client, unsigned char
		caltrigger)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_EN_FAST_COMP__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_EN_FAST_COMP, caltrigger);
	comres = bma222_smbus_write_byte(client, BMA222_EN_FAST_COMP__REG,
			&data);

	return comres;
}

static int bma222_set_selftest_st(struct i2c_client *client, unsigned char
		selftest)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_EN_SELF_TEST__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_EN_SELF_TEST, selftest);
	comres = bma222_smbus_write_byte(client, BMA222_EN_SELF_TEST__REG,
			&data);

	return comres;
}

static int bma222_set_selftest_stn(struct i2c_client *client, unsigned char stn)
{
	int comres = 0;
	unsigned char data;

	comres = bma222_smbus_read_byte(client, BMA222_NEG_SELF_TEST__REG,
			&data);
	data = BMA222_SET_BITSLICE(data, BMA222_NEG_SELF_TEST, stn);
	comres = bma222_smbus_write_byte(client, BMA222_NEG_SELF_TEST__REG,
			&data);

	return comres;
}
static int bma222_read_accel_xyz(struct i2c_client *client,
		struct bma222acc *acc)
{
	int comres;
	unsigned char data[6];

	comres = bma222_smbus_read_byte_block(client, BMA222_ACC_X8_LSB__REG,
			data, 6);
	acc->x = BMA222_GET_BITSLICE(data[0], BMA222_ACC_X8_LSB)|
		(BMA222_GET_BITSLICE(data[1],
			BMA222_ACC_X_MSB)<<(BMA222_ACC_X8_LSB__LEN));
	acc->x = acc->x << (sizeof(short)*8-(BMA222_ACC_X8_LSB__LEN +
				BMA222_ACC_X_MSB__LEN));
	acc->x = acc->x >> (sizeof(short)*8-(BMA222_ACC_X8_LSB__LEN +
				BMA222_ACC_X_MSB__LEN));

	acc->y = BMA222_GET_BITSLICE(data[2], BMA222_ACC_Y8_LSB)|
		(BMA222_GET_BITSLICE(data[3],
			BMA222_ACC_Y_MSB)<<(BMA222_ACC_Y8_LSB__LEN));
	acc->y = acc->y << (sizeof(short)*8-(BMA222_ACC_Y8_LSB__LEN +
				BMA222_ACC_Y_MSB__LEN));
	acc->y = acc->y >> (sizeof(short)*8-(BMA222_ACC_Y8_LSB__LEN +
				BMA222_ACC_Y_MSB__LEN));

	acc->z = BMA222_GET_BITSLICE(data[4], BMA222_ACC_Z8_LSB)|
		(BMA222_GET_BITSLICE(data[5],
			BMA222_ACC_Z_MSB)<<(BMA222_ACC_Z8_LSB__LEN));
	acc->z = acc->z << (sizeof(short)*8-(BMA222_ACC_Z8_LSB__LEN +
				BMA222_ACC_Z_MSB__LEN));
	acc->z = acc->z >> (sizeof(short)*8-(BMA222_ACC_Z8_LSB__LEN +
				BMA222_ACC_Z_MSB__LEN));

	return comres;
}

static int bma222_read_accel_x(struct i2c_client *client, short *a_x)
{
	int comres;
	unsigned char data[2];

	comres = bma222_smbus_read_byte_block(client, BMA222_ACC_X8_LSB__REG,
			data, 2);
	*a_x = BMA222_GET_BITSLICE(data[0], BMA222_ACC_X8_LSB)|
		(BMA222_GET_BITSLICE(data[1],
			BMA222_ACC_X_MSB)<<(BMA222_ACC_X8_LSB__LEN));
	*a_x = *a_x << (sizeof(short)*8-(BMA222_ACC_X8_LSB__LEN +
				BMA222_ACC_X_MSB__LEN));
	*a_x = *a_x >> (sizeof(short)*8-(BMA222_ACC_X8_LSB__LEN +
				BMA222_ACC_X_MSB__LEN));

	return comres;
}

static int bma222_read_accel_y(struct i2c_client *client, short *a_y)
{
	int comres;
	unsigned char data[2];

	comres = bma222_smbus_read_byte_block(client, BMA222_ACC_Y8_LSB__REG,
			data, 2);
	*a_y = BMA222_GET_BITSLICE(data[0], BMA222_ACC_Y8_LSB)|
		(BMA222_GET_BITSLICE(data[1],
			BMA222_ACC_Y_MSB)<<(BMA222_ACC_Y8_LSB__LEN));
	*a_y = *a_y << (sizeof(short)*8-(BMA222_ACC_Y8_LSB__LEN +
				BMA222_ACC_Y_MSB__LEN));
	*a_y = *a_y >> (sizeof(short)*8-(BMA222_ACC_Y8_LSB__LEN +
				BMA222_ACC_Y_MSB__LEN));

	return comres;
}

static int bma222_read_accel_z(struct i2c_client *client, short *a_z)
{
	int comres;
	unsigned char data[2];

	comres = bma222_smbus_read_byte_block(client, BMA222_ACC_Z8_LSB__REG,
			data, 2);
	*a_z = BMA222_GET_BITSLICE(data[0], BMA222_ACC_Z8_LSB)|
		(BMA222_GET_BITSLICE(data[1],
			BMA222_ACC_Z_MSB)<<(BMA222_ACC_Z8_LSB__LEN));
	*a_z = *a_z << (sizeof(short)*8-(BMA222_ACC_Z8_LSB__LEN +
				BMA222_ACC_Z_MSB__LEN));
	*a_z = *a_z >> (sizeof(short)*8-(BMA222_ACC_Z8_LSB__LEN +
				BMA222_ACC_Z_MSB__LEN));

	return comres;
}

static void bma222_work_func(struct work_struct *work)
{
	struct bma222_data *bma222 = container_of((struct delayed_work *)work,
			struct bma222_data, work);
	static struct bma222acc acc;
	unsigned long delay = msecs_to_jiffies(atomic_read(&bma222->delay));
	struct gsen_platform_data *pdata =(struct gsen_platform_data *) bma222->bma222_client->dev.platform_data;
	
	bma222_read_accel_xyz(bma222->bma222_client, &acc);
	//printk("%s:::x:%d,y:%d,z:%d\n",__FUNCTION__,acc.x,acc.y,acc.z);
	if(!pdata){

        aml_sensor_report_acc(bma222->bma222_client, bma222->input, acc.x, acc.y, acc.z);

	}else{
		input_report_abs(bma222->input, ABS_X, acc.x * pdata->rotator[0] + acc.y * pdata->rotator[1]+acc.z * pdata->rotator[2]);
		input_report_abs(bma222->input, ABS_Y, acc.x * pdata->rotator[3] + acc.y * pdata->rotator[4]+acc.z * pdata->rotator[5]);
		input_report_abs(bma222->input, ABS_Z, acc.x * pdata->rotator[6] + acc.y * pdata->rotator[7]+acc.z * pdata->rotator[8]);
        input_sync(bma222->input);
	}	
	mutex_lock(&bma222->value_mutex);
	bma222->value = acc;
	mutex_unlock(&bma222->value_mutex);
	schedule_delayed_work(&bma222->work, delay);
}


static ssize_t bma222_register_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int address, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	sscanf(buf, "%d%d", &address, &value);

	if (bma222_write_reg(bma222->bma222_client, (unsigned char)address,
				(unsigned char *)&value) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	size_t count = 0;
	u8 reg[0x3d];
	int i;

	for (i = 0 ; i < 0x3d; i++) {
		bma222_smbus_read_byte(bma222->bma222_client, i, reg+i);

		count += sprintf(&buf[count], "0x%x: %d\n", i, reg[i]);
	}
	return count;


}
static ssize_t bma222_range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_range(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma222_range_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_range(bma222->bma222_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_bandwidth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_bandwidth(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_bandwidth_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_bandwidth(bma222->bma222_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_mode(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma222_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_mode(bma222->bma222_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma222_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma222_data *bma222 = input_get_drvdata(input);
	struct bma222acc acc_value;

	mutex_lock(&bma222->value_mutex);
	acc_value = bma222->value;
	mutex_unlock(&bma222->value_mutex);

	return sprintf(buf, "%d %d %d\n", acc_value.x, acc_value.y,
			acc_value.z);
}

static ssize_t bma222_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma222->delay));

}

static ssize_t bma222_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (data > BMA222_MAX_DELAY)
		data = BMA222_MAX_DELAY;
	atomic_set(&bma222->delay, (unsigned int) data);

	return count;
}


static ssize_t bma222_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma222->enable));

}

static void bma222_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&bma222->enable);

	mutex_lock(&bma222->enable_mutex);
	if (enable) {
		if (pre_enable == 0) {
			bma222_set_mode(bma222->bma222_client,
					BMA222_MODE_NORMAL);
			schedule_delayed_work(&bma222->work,
				msecs_to_jiffies(atomic_read(&bma222->delay)));
			atomic_set(&bma222->enable, 1);
		}

	} else {
		if (pre_enable == 1) {
			bma222_set_mode(bma222->bma222_client,
					BMA222_MODE_SUSPEND);
			cancel_delayed_work_sync(&bma222->work);
			atomic_set(&bma222->enable, 0);
		}
	}
	mutex_unlock(&bma222->enable_mutex);

}

static ssize_t bma222_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if ((data == 0) || (data == 1))
		bma222_set_enable(dev, data);

	return count;
}

static ssize_t bma222_enable_int_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int type, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	sscanf(buf, "%d%d", &type, &value);

	if (bma222_set_Int_Enable(bma222->bma222_client, type, value) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_int_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_Int_Mode(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma222_int_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_Int_Mode(bma222->bma222_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_slope_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_slope_duration(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_slope_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_slope_duration(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_slope_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_slope_threshold(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_slope_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_slope_threshold(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_high_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_high_g_duration(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_high_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_high_g_duration(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_high_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_high_g_threshold(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_high_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_high_g_threshold(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_low_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_low_g_duration(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_low_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_low_g_duration(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_low_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_low_g_threshold(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_low_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_low_g_threshold(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_tap_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_tap_threshold(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_tap_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma222_set_tap_threshold(bma222->bma222_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_tap_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_tap_duration(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_tap_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_tap_duration(bma222->bma222_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_tap_quiet_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_tap_quiet(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_tap_quiet_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_tap_quiet(bma222->bma222_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_tap_shock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_tap_shock(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_tap_shock_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_tap_shock(bma222->bma222_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_tap_samp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_tap_samp(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_tap_samp_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_tap_samp(bma222->bma222_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_orient_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_orient_mode(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_orient_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_orient_mode(bma222->bma222_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_orient_blocking_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_orient_blocking(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_orient_blocking_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_orient_blocking(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_orient_hyst_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_orient_hyst(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_orient_hyst_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_orient_hyst(bma222->bma222_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_orient_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_theta_blocking(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_orient_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_theta_blocking(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma222_flat_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_theta_flat(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_flat_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_theta_flat(bma222->bma222_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}
static ssize_t bma222_flat_hold_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_flat_hold_time(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_flat_hold_time_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_flat_hold_time(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma222_fast_calibration_x_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_offset_target_x(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_fast_calibration_x_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_offset_target_x(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma222_set_cal_trigger(bma222->bma222_client, 1) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma222_get_cal_ready(bma222->bma222_client, &tmp);

	/*	printk(KERN_INFO "wait 2ms cal ready flag is %d\n",tmp);*/
		timeout++;
		if (timeout == 50) {
			printk(KERN_INFO "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	printk(KERN_INFO "x axis fast calibration finished\n");
	return count;
}

static ssize_t bma222_fast_calibration_y_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_offset_target_y(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_fast_calibration_y_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_offset_target_y(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma222_set_cal_trigger(bma222->bma222_client, 2) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma222_get_cal_ready(bma222->bma222_client, &tmp);

	/*	printk(KERN_INFO "wait 2ms cal ready flag is %d\n",tmp);*/
		timeout++;
		if (timeout == 50) {
			printk(KERN_INFO "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	printk(KERN_INFO "y axis fast calibration finished\n");
	return count;
}

static ssize_t bma222_fast_calibration_z_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	if (bma222_get_offset_target_z(bma222->bma222_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma222_fast_calibration_z_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma222_set_offset_target_z(bma222->bma222_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma222_set_cal_trigger(bma222->bma222_client, 3) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma222_get_cal_ready(bma222->bma222_client, &tmp);

	/*	printk(KERN_INFO "wait 2ms cal ready flag is %d\n",tmp);*/
		timeout++;
		if (timeout == 50) {
			printk(KERN_INFO "get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	printk(KERN_INFO "z axis fast calibration finished\n");
	return count;
}

static ssize_t bma222_selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma222->selftest_result));

}

static ssize_t bma222_selftest_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	unsigned char clear_value = 0;
	int error;
	short value1 = 0;
	short value2 = 0;
	short diff = 0;
	unsigned long result = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma222_data *bma222 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;


	if (data != 1)
		return -EINVAL;
	/* set to 2 G range */
	if (bma222_set_range(bma222->bma222_client, 0) < 0)
		return -EINVAL;

	bma222_write_reg(bma222->bma222_client, 0x32, &clear_value);

	bma222_set_selftest_st(bma222->bma222_client, 1); /* 1 for x-axis*/
	bma222_set_selftest_stn(bma222->bma222_client, 0); /* positive
							      direction*/
	mdelay(10);
	bma222_read_accel_x(bma222->bma222_client, &value1);
	bma222_set_selftest_stn(bma222->bma222_client, 1); /* negative
							      direction*/
	mdelay(10);
	bma222_read_accel_x(bma222->bma222_client, &value2);
	diff = value1-value2;

	printk(KERN_INFO "diff x is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);

	if (abs(diff) < 51)
		result |= 1;

	bma222_set_selftest_st(bma222->bma222_client, 2); /* 2 for y-axis*/
	bma222_set_selftest_stn(bma222->bma222_client, 0); /* positive
							      direction*/
	mdelay(10);
	bma222_read_accel_y(bma222->bma222_client, &value1);
	bma222_set_selftest_stn(bma222->bma222_client, 1); /* negative
							      direction*/
	mdelay(10);
	bma222_read_accel_y(bma222->bma222_client, &value2);
	diff = value1-value2;
	printk(KERN_INFO "diff y is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);
	if (abs(diff) < 51)
		result |= 2;


	bma222_set_selftest_st(bma222->bma222_client, 3); /* 3 for z-axis*/
	bma222_set_selftest_stn(bma222->bma222_client, 0); /* positive
							      direction*/
	mdelay(10);
	bma222_read_accel_z(bma222->bma222_client, &value1);
	bma222_set_selftest_stn(bma222->bma222_client, 1); /* negative
							      direction*/
	mdelay(10);
	bma222_read_accel_z(bma222->bma222_client, &value2);
	diff = value1-value2;

	printk(KERN_INFO "diff z is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);
	if (abs(diff) < 25)
		result |= 4;

	atomic_set(&bma222->selftest_result, (unsigned int) result);

	printk(KERN_INFO "self test finished\n");


	return count;
}

static DEVICE_ATTR(range, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_range_show, bma222_range_store);
static DEVICE_ATTR(bandwidth, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_bandwidth_show, bma222_bandwidth_store);
static DEVICE_ATTR(mode, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_mode_show, bma222_mode_store);
static DEVICE_ATTR(value, S_IRUGO,
		bma222_value_show, NULL);
static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_delay_show, bma222_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_enable_show, bma222_enable_store);
static DEVICE_ATTR(enable_int, S_IWUSR|S_IWGRP,
		NULL, bma222_enable_int_store);
static DEVICE_ATTR(int_mode, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_int_mode_show, bma222_int_mode_store);
static DEVICE_ATTR(slope_duration, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_slope_duration_show, bma222_slope_duration_store);
static DEVICE_ATTR(slope_threshold, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_slope_threshold_show, bma222_slope_threshold_store);
static DEVICE_ATTR(high_g_duration, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_high_g_duration_show, bma222_high_g_duration_store);
static DEVICE_ATTR(high_g_threshold, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_high_g_threshold_show, bma222_high_g_threshold_store);
static DEVICE_ATTR(low_g_duration, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_low_g_duration_show, bma222_low_g_duration_store);
static DEVICE_ATTR(low_g_threshold, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_low_g_threshold_show, bma222_low_g_threshold_store);
static DEVICE_ATTR(tap_duration, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_tap_duration_show, bma222_tap_duration_store);
static DEVICE_ATTR(tap_threshold, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_tap_threshold_show, bma222_tap_threshold_store);
static DEVICE_ATTR(tap_quiet, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_tap_quiet_show, bma222_tap_quiet_store);
static DEVICE_ATTR(tap_shock, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_tap_shock_show, bma222_tap_shock_store);
static DEVICE_ATTR(tap_samp, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_tap_samp_show, bma222_tap_samp_store);
static DEVICE_ATTR(orient_mode, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_orient_mode_show, bma222_orient_mode_store);
static DEVICE_ATTR(orient_blocking, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_orient_blocking_show, bma222_orient_blocking_store);
static DEVICE_ATTR(orient_hyst, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_orient_hyst_show, bma222_orient_hyst_store);
static DEVICE_ATTR(orient_theta, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_orient_theta_show, bma222_orient_theta_store);
static DEVICE_ATTR(flat_theta, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_flat_theta_show, bma222_flat_theta_store);
static DEVICE_ATTR(flat_hold_time, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_flat_hold_time_show, bma222_flat_hold_time_store);
static DEVICE_ATTR(reg, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_register_show, bma222_register_store);
static DEVICE_ATTR(fast_calibration_x, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_fast_calibration_x_show,
		bma222_fast_calibration_x_store);
static DEVICE_ATTR(fast_calibration_y, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_fast_calibration_y_show,
		bma222_fast_calibration_y_store);
static DEVICE_ATTR(fast_calibration_z, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_fast_calibration_z_show,
		bma222_fast_calibration_z_store);
static DEVICE_ATTR(selftest, S_IRUGO|S_IWUSR|S_IWGRP,
		bma222_selftest_show, bma222_selftest_store);

static struct attribute *bma222_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_bandwidth.attr,
	&dev_attr_mode.attr,
	&dev_attr_value.attr,
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	&dev_attr_enable_int.attr,
	&dev_attr_int_mode.attr,
	&dev_attr_slope_duration.attr,
	&dev_attr_slope_threshold.attr,
	&dev_attr_high_g_duration.attr,
	&dev_attr_high_g_threshold.attr,
	&dev_attr_low_g_duration.attr,
	&dev_attr_low_g_threshold.attr,
	&dev_attr_tap_threshold.attr,
	&dev_attr_tap_duration.attr,
	&dev_attr_tap_quiet.attr,
	&dev_attr_tap_shock.attr,
	&dev_attr_tap_samp.attr,
	&dev_attr_orient_mode.attr,
	&dev_attr_orient_blocking.attr,
	&dev_attr_orient_hyst.attr,
	&dev_attr_orient_theta.attr,
	&dev_attr_flat_theta.attr,
	&dev_attr_flat_hold_time.attr,
	&dev_attr_reg.attr,
	&dev_attr_fast_calibration_x.attr,
	&dev_attr_fast_calibration_y.attr,
	&dev_attr_fast_calibration_z.attr,
	&dev_attr_selftest.attr,
	NULL
};

static struct attribute_group bma222_attribute_group = {
	.attrs = bma222_attributes
};


#if defined(BMA222_ENABLE_INT1) || defined(BMA222_ENABLE_INT2)
unsigned char *orient[] = {"upward looking portrait upright",   \
	"upward looking portrait upside-down",   \
		"upward looking landscape left",   \
		"upward looking landscape right",   \
		"downward looking portrait upright",   \
		"downward looking portrait upside-down",   \
		"downward looking landscape left",   \
		"downward looking landscape right"};

static void bma222_irq_work_func(struct work_struct *work)
{
	struct bma222_data *bma222 = container_of((struct work_struct *)work,
			struct bma222_data, irq_work);

	unsigned char status = 0;
	unsigned char i;
	unsigned char first_value = 0;
	unsigned char sign_value = 0;

	bma222_get_interruptstatus1(bma222->bma222_client, &status);

	switch (status) {

	case 0x01:
		printk(KERN_INFO "Low G interrupt happened\n");
		input_report_rel(bma222->input, LOW_G_INTERRUPT,
				LOW_G_INTERRUPT_HAPPENED);
		break;
	case 0x02:
		for (i = 0; i < 3; i++) {
			bma222_get_HIGH_first(bma222->bma222_client, i,
					   &first_value);
			if (first_value == 1) {

				bma222_get_HIGH_sign(bma222->bma222_client,
						   &sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED);
					if (i == 1)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED);
					if (i == 2)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_X_HAPPENED);
					if (i == 1)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Y_HAPPENED);
					if (i == 2)
						input_report_rel(bma222->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Z_HAPPENED);

				}
			   }

		      printk(KERN_INFO "High G interrupt happened,exis is %d,"
				      "first is %d,sign is %d\n", i,
					   first_value, sign_value);
		}
		   break;
	case 0x04:
		for (i = 0; i < 3; i++) {
			bma222_get_slope_first(bma222->bma222_client, i,
					   &first_value);
			if (first_value == 1) {

				bma222_get_slope_sign(bma222->bma222_client,
						   &sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel(bma222->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED);
					else if (i == 1)
						input_report_rel(bma222->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED);
					else if (i == 2)
						input_report_rel(bma222->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel(bma222->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_X_HAPPENED);
					else if (i == 1)
						input_report_rel(bma222->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Y_HAPPENED);
					else if (i == 2)
						input_report_rel(bma222->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Z_HAPPENED);

				}
			}

			printk(KERN_INFO "Slop interrupt happened,exis is %d,"
					"first is %d,sign is %d\n", i,
					first_value, sign_value);
		}
		break;

	case 0x10:
		printk(KERN_INFO "double tap interrupt happened\n");
		input_report_rel(bma222->input, DOUBLE_TAP_INTERRUPT,
					DOUBLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x20:
		printk(KERN_INFO "single tap interrupt happened\n");
		input_report_rel(bma222->input, SINGLE_TAP_INTERRUPT,
					SINGLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x40:
		bma222_get_orient_status(bma222->bma222_client,
				    &first_value);
		printk(KERN_INFO "orient interrupt happened,%s\n",
				orient[first_value]);
		if (first_value == 0)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 1)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 2)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 3)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		else if (first_value == 4)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 5)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 6)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 7)
			input_report_abs(bma222->input, ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		break;
	case 0x80:
		bma222_get_orient_flat_status(bma222->bma222_client,
				    &sign_value);
		printk(KERN_INFO "flat interrupt happened,flat status is %d\n",
				    sign_value);
		if (sign_value == 1) {
			input_report_abs(bma222->input, FLAT_INTERRUPT,
				FLAT_INTERRUPT_TURE_HAPPENED);
		} else {
			input_report_abs(bma222->input, FLAT_INTERRUPT,
				FLAT_INTERRUPT_FALSE_HAPPENED);
		}
		break;
	default:
		break;
	}

}

static irqreturn_t bma222_irq_handler(int irq, void *handle)
{


	struct bma222_data *data = handle;


	if (data == NULL)
		return IRQ_HANDLED;
	if (data->bma222_client == NULL)
		return IRQ_HANDLED;


	schedule_work(&data->irq_work);

	return IRQ_HANDLED;


}
#endif /* defined(BMA222_ENABLE_INT1)||defined(BMA222_ENABLE_INT2) */
static int bma222_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	unsigned char tempvalue;
	struct bma222_data *data;
	struct input_dev *dev;

//	omap_mux_init_gpio(145, OMAP_PIN_INPUT);
//	omap_mux_init_gpio(146, OMAP_PIN_INPUT);
//
//	gpio_direction_input(INT_PORT);
//	gpio_enable_edge_int(gpio_to_idx(INT_PORT), 1, client->irq - INT_GPIO_0);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_INFO "i2c_check_functionality error\n");
		goto exit;
	}
	data = kzalloc(sizeof(struct bma222_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	/* read chip id */
	tempvalue = i2c_smbus_read_byte_data(client, BMA222_CHIP_ID_REG);

	if (tempvalue == BMA222_CHIP_ID || tempvalue == BMA222E_CHIP_ID) {
		printk(KERN_INFO "Bosch Sensortec Device detected!\n"
				"BMA222 registered I2C driver!\n");
	} else{
		printk(KERN_INFO "Bosch Sensortec Device not found"
				"i2c error %d \n", tempvalue);
		err = -ENODEV;
		goto kfree_exit;
	}
	i2c_set_clientdata(client, data);
	data->bma222_client = client;
	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);
	bma222_set_bandwidth(client, BMA222_BW_SET);
	bma222_set_range(client, BMA222_RANGE_SET);

#if defined(BMA222_ENABLE_INT1) || defined(BMA222_ENABLE_INT2)
	bma222_set_Int_Mode(client, 1);/*latch interrupt 250ms*/
#endif
	/*8,single tap
	  10,orient
	  11,flat*/
/*	bma222_set_Int_Enable(client,8, 1);
	bma222_set_Int_Enable(client,10, 1);
	bma222_set_Int_Enable(client,11, 1);
*/
#ifdef BMA222_ENABLE_INT1
	/* maps interrupt to INT1 pin */
	bma222_set_int1_pad_sel(client, PAD_LOWG);
	bma222_set_int1_pad_sel(client, PAD_HIGHG);
	bma222_set_int1_pad_sel(client, PAD_SLOP);
	bma222_set_int1_pad_sel(client, PAD_DOUBLE_TAP);
	bma222_set_int1_pad_sel(client, PAD_SINGLE_TAP);
	bma222_set_int1_pad_sel(client, PAD_ORIENT);
	bma222_set_int1_pad_sel(client, PAD_FLAT);
#endif

#ifdef BMA222_ENABLE_INT2
	/* maps interrupt to INT2 pin */
	bma222_set_int2_pad_sel(client, PAD_LOWG);
	bma222_set_int2_pad_sel(client, PAD_HIGHG);
	bma222_set_int2_pad_sel(client, PAD_SLOP);
	bma222_set_int2_pad_sel(client, PAD_DOUBLE_TAP);
	bma222_set_int2_pad_sel(client, PAD_SINGLE_TAP);
	bma222_set_int2_pad_sel(client, PAD_ORIENT);
	bma222_set_int2_pad_sel(client, PAD_FLAT);
#endif

#if defined(BMA222_ENABLE_INT1) || defined(BMA222_ENABLE_INT2)
	data->IRQ = client->irq;
	err = request_irq(data->IRQ, bma222_irq_handler, IRQF_TRIGGER_RISING,
			"bma222", data);
	if (err)
		printk(KERN_ERR "could not request irq\n");

	INIT_WORK(&data->irq_work, bma222_irq_work_func);
#endif

	INIT_DELAYED_WORK(&data->work, bma222_work_func);
	atomic_set(&data->delay, BMA222_MAX_DELAY);
	atomic_set(&data->enable, 0);

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;
	dev->name = SENSOR_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, LOW_G_INTERRUPT);
	input_set_capability(dev, EV_REL, HIGH_G_INTERRUPT);
	input_set_capability(dev, EV_REL, SLOP_INTERRUPT);
	input_set_capability(dev, EV_REL, DOUBLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_REL, SINGLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_ABS, ORIENT_INTERRUPT);
	input_set_capability(dev, EV_ABS, FLAT_INTERRUPT);
	input_set_abs_params(dev, ABS_X, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Y, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Z, ABSMIN, ABSMAX, 0, 0);

	input_set_drvdata(dev, data);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		goto kfree_exit;
	}

	data->input = dev;

	err = sysfs_create_group(&data->input->dev.kobj,
			&bma222_attribute_group);
	if (err < 0)
		goto error_sysfs;

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = bma222_early_suspend;
	data->early_suspend.resume = bma222_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);

	return 0;

error_sysfs:
	input_unregister_device(data->input);

kfree_exit:
	kfree(data);
exit:
	return err;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma222_early_suspend(struct early_suspend *h)
{
	struct bma222_data *data =
		container_of(h, struct bma222_data, early_suspend);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma222_set_mode(data->bma222_client, BMA222_MODE_SUSPEND);
		cancel_delayed_work_sync(&data->work);
	}
	mutex_unlock(&data->enable_mutex);
}


static void bma222_late_resume(struct early_suspend *h)
{
	struct bma222_data *data =
		container_of(h, struct bma222_data, early_suspend);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma222_set_mode(data->bma222_client, BMA222_MODE_NORMAL);
		schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
	}
	mutex_unlock(&data->enable_mutex);
}
#endif

static int bma222_remove(struct i2c_client *client)
{
	struct bma222_data *data = i2c_get_clientdata(client);

	bma222_set_enable(&client->dev, 0);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	sysfs_remove_group(&data->input->dev.kobj, &bma222_attribute_group);
	input_unregister_device(data->input);
	kfree(data);

	return 0;
}
#ifdef CONFIG_PM

static int bma222_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bma222_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma222_set_mode(data->bma222_client, BMA222_MODE_SUSPEND);
		cancel_delayed_work_sync(&data->work);
	}
	mutex_unlock(&data->enable_mutex);

	return 0;
}

static int bma222_resume(struct i2c_client *client)
{
	struct bma222_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma222_set_mode(data->bma222_client, BMA222_MODE_NORMAL);
		schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
	}
	mutex_unlock(&data->enable_mutex);

	return 0;
}

#else

#define bma222_suspend		NULL
#define bma222_resume		NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id bma222_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma222_id);

static struct i2c_driver bma222_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SENSOR_NAME,
	},
	.suspend	= bma222_suspend,
	.resume		= bma222_resume,
	.id_table	= bma222_id,
	.probe		= bma222_probe,
	.remove		= bma222_remove,

};


static int __init BMA222_init(void)
{
	return i2c_add_driver(&bma222_driver);
}

static void __exit BMA222_exit(void)
{
	i2c_del_driver(&bma222_driver);

}

MODULE_AUTHOR("Albert Zhang <xu.zhang@bosch-sensortec.com>");
MODULE_DESCRIPTION("BMA222 accelerometer sensor driver");
MODULE_LICENSE("GPL");

module_init(BMA222_init);
module_exit(BMA222_exit);

