/*
 * NT99230_0 sensor driver
 *
 * Copyright (C) 2012-2014 Intel Mobile Communications GmbH
 *
 * Copyright (C) 2008 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * Note:
 *    09/25/2014: new implementation using v4l2-subdev
 *                        instead of v4l2-int-device.
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf-core.h>
#include <linux/slab.h>
#include <media/v4l2-controls_rockchip.h>
#include "ov_camera_module.h"

#define NT_DRIVER_NAME "nt99230"

#define NT99230_AEC_GAIN_REG                           0x32CF
#define NT99230_AEC_EXPO_2ND_REG                       0x3012	/* Exposure Bits 8-15 */
#define NT99230_AEC_EXPO_1ST_REG                       0x3013	/* Exposure Bits 0-7 */
#define NT99230_AEC_UPDATE_ADDRESS                     0x3060
#define NT99230_AEC_UPDATE_DATA                        0x02

#define NT99230_FETCH_2ND_BYTE_EXP(VAL)                ((VAL >> 8) & 0xFF)	/* 8 Bits */
#define NT99230_FETCH_1ST_BYTE_EXP(VAL)                ((VAL & 0xFF))	/* 8 Bits */

#define NT99230_PIDH_ADDR                              0x3000
#define NT99230_PIDL_ADDR                              0x3001
#define NT99230_PIDH_MAGIC                             0x23  /* High byte of product ID */
#define NT99230_PIDL_MAGIC                             0x00  /* Low byte of product ID  */

#define NT99230_EXT_CLK                                24000000
#define NT99230_PCLK                                   90000000

#define NT99230_LINE_LENGTH_VALUE_HIGH                 0x09
#define NT99230_LINE_LENGTH_VALUE_LOW                  0x6C
#define NT99230_FRAME_LENGTH_VALUE_HIGH                0x04
#define NT99230_FRAME_LENGTH_VALUE_LOW                 0xDB
#define NT99230_LINE_LENGTH_REG_HIGH                   0x300A
#define NT99230_LINE_LENGTH_REG_LOW                    0x300B
#define NT99230_FRAME_LENGTH_REG_HIGH                  0x300C
#define NT99230_FRAME_LENGTH_REG_LOW                   0x300D

#define NT99230_HORIZONTAL_START_HIGH_REG              0x3002
#define NT99230_HORIZONTAL_START_LOW_REG               0x3003
#define NT99230_VERTICAL_START_HIGH_REG                0x3004
#define NT99230_VERTICAL_START_LOW_REG                 0x3005
#define NT99230_HORIZONTAL_END_HIGH_REG                0x3006
#define NT99230_HORIZONTAL_END_LOW_REG                 0x3007
#define NT99230_VERTICAL_END_HIGH_REG                  0x3008
#define NT99230_VERTICAL_END_LOW_REG                   0x3009
#define NT99230_HORIZONTAL_OUTPUT_SIZE_HIGH_REG        0x300E
#define NT99230_HORIZONTAL_OUTPUT_SIZE_LOW_REG         0x300F
#define NT99230_VERTICAL_OUTPUT_SIZE_HIGH_REG          0x3010
#define NT99230_VERTICAL_OUTPUT_SIZE_LOW_REG           0x3011
#define NT99230_H_WIN_OFF_HIGH_REG                     0x300E
#define NT99230_H_WIN_OFF_LOW_REG                      0x300F
#define NT99230_V_WIN_OFF_HIGH_REG                     0x3010
#define NT99230_V_WIN_OFF_LOW_REG                      0x3011
#define NT99230_COARSE_INTG_TIME_MIN 16
#define NT99230_COARSE_INTG_TIME_MAX_MARGIN 4

/* ======================================================================== */
/* Base sensor configs */
/* ======================================================================== */
/* MCLK:24MHz  1920x1080  30fps   mipi 2lane   640Mbps/lane */
static const struct ov_camera_module_reg NT99230_init_tab_1920_1080_30fps_yuyv[] = {
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3069, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x306A, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3100, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3101, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3102, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3103, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3104, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3105, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3106, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3107, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3108, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3109, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310A, 0x75},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310B, 0xC0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310C, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310D, 0x43},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310E, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3110, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3111, 0xCE},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3112, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3113, 0x66},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3114, 0x33},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3115, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3116, 0x86},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3118, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3119, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x311A, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303F, 0x32},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3055, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3051, 0x3B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3053, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3056, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308A, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308B, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308C, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308D, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308E, 0x37},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308F, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30A0, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30A4, 0x1F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350B, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3514, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3515, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3518, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3519, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351A, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3530, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3534, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3512, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3511, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3513, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3532, 0x35},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3533, 0x20},

{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F14, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F03, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F04, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F05, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F06, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F07, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F09, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0A, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0B, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0C, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0D, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0E, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F0F, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F10, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F11, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F12, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F13, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3F00, 0x20},

{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3210, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3211, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3212, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3213, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3214, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3215, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3216, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3217, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3218, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3219, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321A, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321B, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321C, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321D, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321E, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x321F, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3230, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3231, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3232, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3233, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3234, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3235, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3236, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3237, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3238, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3239, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x323A, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3243, 0xC3},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3244, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3245, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3241, 0x45},

{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3270, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3271, 0x0B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3272, 0x16},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3273, 0x2B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3274, 0x3F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3275, 0x51},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3276, 0x72},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3277, 0x8F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3278, 0xA7},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3279, 0xBC},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x327A, 0xDC},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x327B, 0xF0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x327C, 0xFA},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x327D, 0xFE},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x327E, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3700, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3701, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3702, 0x1A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3703, 0x32},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3704, 0x42},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3705, 0x51},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3706, 0x6B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3707, 0x81},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3708, 0x94},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3709, 0xA6},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370A, 0xC2},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370B, 0xD4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370C, 0xE0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370D, 0xE7},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x370E, 0xEF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3710, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3714, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3800, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3302, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3303, 0x4F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3304, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3305, 0xA1},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3306, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3307, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3308, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3309, 0xCD},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330A, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330B, 0xCD},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330C, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330D, 0x67},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330E, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x330F, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3310, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3311, 0xEA},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3312, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3313, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3250, 0x37},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3251, 0x1F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3252, 0x39},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3253, 0x21},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3254, 0x3B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3255, 0x23},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3256, 0x29},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3257, 0x16},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3258, 0x4F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3259, 0x35},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x325A, 0x29},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x325B, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x325C, 0x70},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x325D, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3290, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3292, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3297, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B8, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B9, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B0, 0x57},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B1, 0xAC},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B2, 0x11},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B3, 0xA0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32B4, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BC, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BD, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BE, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32CB, 0x18},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32CC, 0x70},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32CD, 0xA0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3326, 0x12},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3327, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3332, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x334A, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x334B, 0x7F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x334C, 0x1F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x335B, 0xD0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3360, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3361, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3362, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3365, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3366, 0x0C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3367, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3368, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3369, 0x38},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x336B, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x336D, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x336E, 0x16},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3370, 0x0A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3371, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3372, 0x1C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3374, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3375, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3376, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3378, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3379, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x337A, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x337C, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A0, 0x40},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A1, 0x7A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A2, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A3, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A4, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A5, 0x8C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A6, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A7, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A9, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AA, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AC, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AD, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AE, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33B0, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33B1, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33B4, 0x48},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33B5, 0xA4},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33BA, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33BB, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33C0, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33C6, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33C7, 0x43},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33C8, 0x33},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33C9, 0x0A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3363, 0x31},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3364, 0x0B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x333F, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3012, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3013, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3290, 0x82},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3292, 0x7A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x307D, 0x02},
/*
 * MCLK:      24.00MHz
 * Pixel Clk: 80.00MHz
 * Data Rate: 640.00bps
 * Size:      1920x1080
 * FPS:       25.00~30.00
 * Line:      2412
 * Frame:     1105
 * Flicker:   50Hz
*/
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BB, 0x67},  /* AE Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BF, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C0, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C1, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C2, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C3, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C4, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C5, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C6, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D3, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D4, 0x4C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D5, 0x84},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D6, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D7, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D8, 0x81},  /* AE End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32F0, 0x01},  /* Output Format */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3200, 0x7E},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3201, 0x3F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302A, 0x80},  /* PLL Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302B, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302C, 0x4F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302D, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302E, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302F, 0x07},  /* PLL End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x24},  /* Timing Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3023, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3003, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3004, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3005, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3006, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3007, 0x87},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3009, 0x39},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300A, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300B, 0x6C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300C, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300D, 0x51},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300E, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300F, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3010, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x38},  /* Timing End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3508, 0x01},  /* MIPI Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x7C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0xE4},  /* MIPI End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x320A, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3021, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3060, 0x01},
};

#ifdef RAW_RGB
static const struct ov_camera_module_reg NT99230_init_tab_1920_1080_30fps_raw[] = {
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3069, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x306A, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3100, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3101, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3102, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3103, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3104, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3105, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3106, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3107, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3108, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3109, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310A, 0x75},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310B, 0xC0},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310C, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310D, 0x43},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x310E, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3110, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3111, 0xCE},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3112, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3113, 0x66},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3114, 0x33},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3115, 0x88},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3116, 0x86},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3118, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3119, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x311A, 0xAF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x301E, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x303F, 0x32},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3052, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3055, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3056, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30A0, 0x03},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x30A4, 0x1F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308B, 0x5F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308C, 0x3F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308D, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x308F, 0x10},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3262, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x333F, 0x0F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x334B, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3360, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3361, 0x28},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3362, 0x30},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3363, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3364, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x336D, 0x20},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x336E, 0x16},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3370, 0x0B},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3371, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3372, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3374, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3379, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x337A, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x337C, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33A9, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AA, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AC, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AD, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33AE, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x33B0, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3810, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x350B, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3514, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3515, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3518, 0x06},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3519, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x351A, 0xFF},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3530, 0x50},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3534, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3512, 0x05},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3511, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3513, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3532, 0x35},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3533, 0x20},
/*
 * MCLK:			24.00MHz
 * Pixel Clk:	90.00MHz
 * Data Rate:	450.00bps
 * Size:			1920x1080
 * FPS:			30.00~30.02
 * Line:			2412
 * Frame:		1243
 * Flicker:		50Hz
*/
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BB, 0x67},  /* AE Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32BF, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C0, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C1, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C2, 0x60},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C3, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C4, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C5, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32C6, 0x2F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D3, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D4, 0x4C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D5, 0x84},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D6, 0x01},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D7, 0x14},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32D8, 0x81},  /* AE End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x32F0, 0x90},  /* Output Format */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3200, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3201, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302A, 0x80},  /* PLL Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302B, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302C, 0x4A},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302D, 0x0C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302E, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x302F, 0x04},  /* PLL End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3022, 0x24},  /* Timing Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3023, 0x24},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3002, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3003, 0x08},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3004, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3005, 0x02},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3006, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3007, 0x87},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3008, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3009, 0x39},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300A, 0x09},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300B, 0x6C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300C, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300D, 0xDB},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300E, 0x07},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x300F, 0x80},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3010, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3011, 0x38},  /* Timing End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3012, 0x04},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3013, 0x5F},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3508, 0x02},  /* MIPI Start */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3509, 0x1C},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3500, 0xE4},  /* MIPI End */
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x320A, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3021, 0x00},
{OV_CAMERA_MODULE_REG_TYPE_DATA, 0x3060, 0x01},
};
#endif

/* ======================================================================== */
static struct ov_camera_module_config NT99230_configs[] = {
	{
		.name = "1920x1080_30fps_YUYV",
		.frm_fmt = {
			.width = 1920,
			.height = 1080,
			.code = MEDIA_BUS_FMT_YUYV8_2X8
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 30
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)NT99230_init_tab_1920_1080_30fps_yuyv,
		.reg_table_num_entries =
			sizeof(NT99230_init_tab_1920_1080_30fps_yuyv) /
			sizeof(NT99230_init_tab_1920_1080_30fps_yuyv[0]),
		.v_blanking_time_us = 4052,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 675, 24000000)
	}
#ifdef RAW_RGB
	{
		.name = "1920x1080_30fps_RG10",
		.frm_fmt = {
			.width = 1920,
			.height = 1080,
			.code = MEDIA_BUS_FMT_SRGGB10_1X10
		},
		.frm_intrvl = {
			.interval = {
				.numerator = 1,
				.denominator = 30
			}
		},
		.auto_exp_enabled = false,
		.auto_gain_enabled = false,
		.auto_wb_enabled = false,
		.reg_table = (void *)NT99230_init_tab_1920_1080_30fps_raw,
		.reg_table_num_entries =
			sizeof(NT99230_init_tab_1920_1080_30fps_raw) /
			sizeof(NT99230_init_tab_1920_1080_30fps_raw[0]),
		.v_blanking_time_us = 4052,
		.max_exp_gain_h = 16,
		.max_exp_gain_l = 0,
		.ignore_measurement_check = 1,
		PLTFRM_CAM_ITF_MIPI_CFG(0, 2, 450, 24000000)
	}
#endif
};

/*--------------------------------------------------------------------------*/
static int NT99230_g_VTS(struct ov_camera_module *cam_mod, u32 *vts)
{
	u32 msb, lsb;
	int ret;

	ret = ov_camera_module_read_reg_table(cam_mod, NT99230_FRAME_LENGTH_REG_HIGH, &msb);
	if (IS_ERR_VALUE(ret))
		goto err;

	ret = ov_camera_module_read_reg_table(cam_mod, NT99230_FRAME_LENGTH_REG_LOW, &lsb);
	if (IS_ERR_VALUE(ret))
		goto err;

	*vts = (msb << 8) | lsb;

	return 0;
err:
	ov_camera_module_pr_err(cam_mod,
							"failed with error (%d)\n", ret);
	return ret;
}

static int NT99230_auto_adjust_fps(struct ov_camera_module *cam_mod, u32 exp_time)
{
	int ret;

	ret = 0;

	return ret;
}

/**************************************************/
/* calcul_agc_ratio: 100 = 1x, 150 = 1.5x, 200 = 2x, etc...*/
/**************************************************/
#define AGC_ISO_MIN          0
#define AGC_GAP             16
#define GAIN_STEP           625

static u32 gain_transfer(u32 a_gain)
{
	u32 gain_factor, calcul_agc_ratio, shf;

	calcul_agc_ratio = a_gain * 100;
	shf = 0;

	while (calcul_agc_ratio / ((1 << (shf + 1)) * 100))
		shf++;

	gain_factor = ((calcul_agc_ratio - ((1 << shf) * 100)) * 100) /
					(GAIN_STEP << shf) + AGC_ISO_MIN + AGC_GAP * shf;

	return gain_factor;
}

static int NT99230_write_aec(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod,
								"exp_time = %d lines, gain = %d, flash_mode = %d\n",
								cam_mod->exp_config.exp_time,
								cam_mod->exp_config.gain,
								cam_mod->exp_config.flash_mode);

	/*
	if the sensor is already streaming, write to shadow registers,
	if the sensor is in SW standby, write to active registers,
	if the sensor is off/registers are not writeable, do nothing
	*/
	if (cam_mod->state == OV_CAMERA_MODULE_SW_STANDBY || cam_mod->state == OV_CAMERA_MODULE_STREAMING) {
		u32 a_gain = cam_mod->exp_config.gain;
		u32 exp_time = cam_mod->exp_config.exp_time;

		mutex_lock(&cam_mod->lock);
		if (!IS_ERR_VALUE(ret) && cam_mod->auto_adjust_fps)
			ret = NT99230_auto_adjust_fps(cam_mod, cam_mod->exp_config.exp_time);

		ret |= ov_camera_module_write_reg(cam_mod, NT99230_AEC_GAIN_REG, gain_transfer(a_gain));
		ret |= ov_camera_module_write_reg(cam_mod, NT99230_AEC_EXPO_2ND_REG, NT99230_FETCH_2ND_BYTE_EXP(exp_time));
		ret |= ov_camera_module_write_reg(cam_mod, NT99230_AEC_EXPO_1ST_REG, NT99230_FETCH_1ST_BYTE_EXP(exp_time));

		if (cam_mod->state == OV_CAMERA_MODULE_STREAMING)
			ret = ov_camera_module_write_reg(cam_mod, NT99230_AEC_UPDATE_ADDRESS, NT99230_AEC_UPDATE_DATA);
		mutex_unlock(&cam_mod->lock);
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);

	return ret;
}

static int NT99230_g_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod, "failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_g_timings(struct ov_camera_module *cam_mod, struct ov_camera_module_timings *timings)
{
	int ret = 0;
	u32 reg_val;
	u32 win_off;

	if (IS_ERR_OR_NULL(cam_mod->active_config))
		goto err;

	timings->frame_length_lines = (NT99230_FRAME_LENGTH_VALUE_HIGH << 8) + NT99230_FRAME_LENGTH_VALUE_LOW;

	timings->line_length_pck |= (NT99230_LINE_LENGTH_VALUE_HIGH << 8) + NT99230_LINE_LENGTH_VALUE_LOW;

    /* Novatek Sensor do not use coarse integration time. */
	timings->coarse_integration_time_min = NT99230_COARSE_INTG_TIME_MIN;
	timings->coarse_integration_time_max_margin = NT99230_COARSE_INTG_TIME_MAX_MARGIN;

	/* Novatek Sensor do not use fine integration time. */
	timings->fine_integration_time_min = 0;
	timings->fine_integration_time_max_margin = 0;

	timings->binning_factor_x = 0;
	timings->binning_factor_y = 0;

	/* Get the cropping and output resolution to ISP for this mode. */
	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_START_HIGH_REG, &reg_val)))
		goto err;

	timings->crop_horizontal_start = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_START_LOW_REG, &reg_val)))
		goto err;

	timings->crop_horizontal_start |= reg_val;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_START_HIGH_REG, &reg_val)))
		goto err;

	timings->crop_vertical_start = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_START_LOW_REG, &reg_val)))
		goto err;

	timings->crop_vertical_start |= reg_val;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_END_HIGH_REG, &reg_val)))
		goto err;

	timings->crop_horizontal_end = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_END_LOW_REG, &reg_val)))
		goto err;

	timings->crop_horizontal_end |= reg_val;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_END_HIGH_REG, &reg_val)))
		goto err;

	timings->crop_vertical_end = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_END_LOW_REG, &reg_val)))
		goto err;

	timings->crop_vertical_end |= reg_val;

	/*
	The sensor can do windowing within the cropped array.
	Take this into the cropping size reported.
	*/
	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_H_WIN_OFF_HIGH_REG, &reg_val)))
		goto err;

	win_off = (reg_val & 0xF) << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_H_WIN_OFF_LOW_REG, &reg_val)))
		goto err;

	win_off |= (reg_val & 0xFF);

	timings->crop_horizontal_start += win_off;
	timings->crop_horizontal_end -= win_off;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_V_WIN_OFF_HIGH_REG, &reg_val)))
		goto err;

	win_off = (reg_val & 0xF) << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_V_WIN_OFF_LOW_REG, &reg_val)))
		goto err;

	win_off |= (reg_val & 0xFF);

	timings->crop_vertical_start += win_off;
	timings->crop_vertical_end -= win_off;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_OUTPUT_SIZE_HIGH_REG, &reg_val)))
		goto err;

	timings->sensor_output_width = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_HORIZONTAL_OUTPUT_SIZE_LOW_REG, &reg_val)))
		goto err;

	timings->sensor_output_width |= reg_val;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_OUTPUT_SIZE_HIGH_REG, &reg_val)))
		goto err;

	timings->sensor_output_height = reg_val << 8;

	if (IS_ERR_VALUE(ov_camera_module_read_reg_table(cam_mod, NT99230_VERTICAL_OUTPUT_SIZE_LOW_REG, &reg_val)))
		goto err;

	timings->sensor_output_height |= reg_val;

	timings->vt_pix_clk_freq_hz = cam_mod->frm_intrvl.interval.denominator
					* timings->frame_length_lines
					* timings->line_length_pck;

	ov_camera_module_pr_debug(cam_mod,
								"vt_pix_clk_freq_hz: %d, frame_length_lines: 0x%x, line_length_pck: 0x%x\n",
								timings->vt_pix_clk_freq_hz, timings->frame_length_lines,
								timings->line_length_pck);

	return ret;
err:
	ov_camera_module_pr_err(cam_mod,
							"failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_s_ctrl(struct ov_camera_module *cam_mod, u32 ctrl_id)
{
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	switch (ctrl_id) {
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		ret = NT99230_write_aec(cam_mod);
		break;
	case V4L2_CID_FLASH_LED_MODE:
		/* nothing to be done here */
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		/* todo*/
		break;
	/*
	 * case RK_V4L2_CID_FPS_CTRL:
	 * 	if (cam_mod->auto_adjust_fps) {
	 * 		ret = NT99230_auto_adjust_fps(cam_mod, cam_mod->exp_config.exp_time);
	 * 	}
	 * 	break;
	*/
	default:
		ret = -EINVAL;
		break;
	}

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_s_ext_ctrls(struct ov_camera_module *cam_mod,
				struct ov_camera_module_ext_ctrls *ctrls)
{
	int ret = 0;

	if ((ctrls->ctrls[0].id == V4L2_CID_GAIN ||
		ctrls->ctrls[0].id == V4L2_CID_EXPOSURE))
		ret = NT99230_write_aec(cam_mod);
	else
		ret = -EINVAL;

	if (IS_ERR_VALUE(ret))
		ov_camera_module_pr_debug(cam_mod, "failed with error (%d)\n", ret);

	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_start_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "active config = %s\n", cam_mod->active_config->name);

	ret = NT99230_g_VTS(cam_mod, &cam_mod->vts_min);

	if (IS_ERR_VALUE(ret))
		goto err;

	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x3021, 0x02);
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;
	msleep(25);

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_stop_streaming(struct ov_camera_module *cam_mod)
{
	int ret = 0;

	ov_camera_module_pr_info(cam_mod, "\n");

	mutex_lock(&cam_mod->lock);
	ret = ov_camera_module_write_reg(cam_mod, 0x3021, 0x00);;
	mutex_unlock(&cam_mod->lock);
	if (IS_ERR_VALUE(ret))
		goto err;

	msleep(25);
	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/*--------------------------------------------------------------------------*/

static int NT99230_check_camera_id(struct ov_camera_module *cam_mod)
{
	u32 pidh, pidl;
	int ret = 0;

	ov_camera_module_pr_debug(cam_mod, "\n");

	ret |= ov_camera_module_read_reg(cam_mod, 1, NT99230_PIDH_ADDR, &pidh);
	ret |= ov_camera_module_read_reg(cam_mod, 1, NT99230_PIDL_ADDR, &pidl);
	if (IS_ERR_VALUE(ret)) {
		ov_camera_module_pr_err(cam_mod,
			"register read failed, camera module powered off, while(1)\n");
		goto err;
	}

	if (pidh == NT99230_PIDH_MAGIC && pidl == NT99230_PIDL_MAGIC) {
		ov_camera_module_pr_info(cam_mod,
									"successfully detected camera ID 0x%02x%02x\n",
									pidh, pidl);
	} else {
		ov_camera_module_pr_err(cam_mod,
								"wrong camera ID, expected 0x%02x%02x, detected 0x%02x%02x\n",
								NT99230_PIDH_MAGIC, NT99230_PIDL_MAGIC, pidh, pidl);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	ov_camera_module_pr_err(cam_mod, "failed with error (%d)\n", ret);
	return ret;
}

/* ======================================================================== */
/* This part is platform dependent */
/* ======================================================================== */

static struct v4l2_subdev_core_ops NT99230_camera_module_core_ops = {
	.g_ctrl = ov_camera_module_g_ctrl,
	.s_ctrl = ov_camera_module_s_ctrl,
	.s_ext_ctrls = ov_camera_module_s_ext_ctrls,
	.s_power = ov_camera_module_s_power,
	.ioctl = ov_camera_module_ioctl
};

static struct v4l2_subdev_video_ops NT99230_camera_module_video_ops = {
	.s_frame_interval = ov_camera_module_s_frame_interval,
	.g_frame_interval = ov_camera_module_g_frame_interval,
	.s_stream = ov_camera_module_s_stream
};

static struct v4l2_subdev_pad_ops NT99230_camera_module_pad_ops = {
	.enum_frame_interval = ov_camera_module_enum_frameintervals,
	.get_fmt = ov_camera_module_g_fmt,
	.set_fmt = ov_camera_module_s_fmt,
};

static struct v4l2_subdev_ops NT99230_camera_module_ops = {
	.core = &NT99230_camera_module_core_ops,
	.video = &NT99230_camera_module_video_ops,
	.pad = &NT99230_camera_module_pad_ops
};

static int num_cameras;

static struct ov_camera_module_custom_config NT99230_custom_config = {
	.start_streaming = NT99230_start_streaming,
	.stop_streaming = NT99230_stop_streaming,
	.s_ctrl = NT99230_s_ctrl,
	.g_ctrl = NT99230_g_ctrl,
	.s_ext_ctrls = NT99230_s_ext_ctrls,
	.g_timings = NT99230_g_timings,
	.check_camera_id = NT99230_check_camera_id,
	.configs = NT99230_configs,
	.num_configs = sizeof(NT99230_configs) / sizeof(NT99230_configs[0]),
	.power_up_delays_ms = {5, 30, 0},
	/*
	*0: Exposure time valid fileds;
	*1: Exposure gain valid fileds;
	*(2 fileds == 1 frames)
	*/
	.exposure_valid_frame = {4, 4}
};

static int NT99230_probe(
	struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct ov_camera_module *tmp_NT99230 = NULL;

	dev_info(&client->dev, "probing...\n");

	tmp_NT99230 = devm_kzalloc(&client->dev, sizeof(*tmp_NT99230), GFP_KERNEL);
	dev_info(&client->dev, "tmp_NT99230 = %p\n", tmp_NT99230);
	if (!tmp_NT99230) {
		dev_info(&client->dev, "devm_kzalloc ov_camera_module buffer error!\n");
		return -1;
	}

	v4l2_i2c_subdev_init(&tmp_NT99230->sd, client, &NT99230_camera_module_ops);

	tmp_NT99230->custom = NT99230_custom_config;
	num_cameras++;
	mutex_init(&tmp_NT99230->lock);
	dev_info(&client->dev, "probing successful\n");
	return 0;
}

/* ======================================================================== */

static int NT99230_remove(
	struct i2c_client *client)
{
	struct ov_camera_module *cam_mod = i2c_get_clientdata(client);

	dev_info(&client->dev, "removing device...\n");

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	mutex_destroy(&cam_mod->lock);
	ov_camera_module_release(cam_mod);

	dev_info(&client->dev, "removed\n");
	return 0;
}

static const struct i2c_device_id NT99230_id[] = {
	{ NT_DRIVER_NAME, 0 },
	{ }
};

static const struct of_device_id NT99230_of_match[] = {
	{.compatible = "novatek," NT_DRIVER_NAME "-v4l2-i2c-subdev",},
	{},
};

MODULE_DEVICE_TABLE(i2c, NT99230_id);

static struct i2c_driver NT99230_i2c_driver = {
	.driver = {
		.name = NT_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = NT99230_of_match
	},
	.probe = NT99230_probe,
	.remove = NT99230_remove,
	.id_table = NT99230_id,
};

module_i2c_driver(NT99230_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for NT99230_0");
MODULE_AUTHOR("Eike Grimpe");
MODULE_LICENSE("GPL");

