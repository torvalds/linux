/*
 * Support for mt9m114 Camera Sensor.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#ifndef __A1040_H__
#define __A1040_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include "../include/linux/atomisp_platform.h"
#include "../include/linux/atomisp.h"

#define V4L2_IDENT_MT9M114 8245

#define MT9P111_REV3
#define FULLINISUPPORT

/* #defines for register writes and register array processing */
#define MISENSOR_8BIT		1
#define MISENSOR_16BIT		2
#define MISENSOR_32BIT		4

#define MISENSOR_FWBURST0	0x80
#define MISENSOR_FWBURST1	0x81
#define MISENSOR_FWBURST4	0x84
#define MISENSOR_FWBURST	0x88

#define MISENSOR_TOK_TERM	0xf000	/* terminating token for reg list */
#define MISENSOR_TOK_DELAY	0xfe00	/* delay token for reg list */
#define MISENSOR_TOK_FWLOAD	0xfd00	/* token indicating load FW */
#define MISENSOR_TOK_POLL	0xfc00	/* token indicating poll instruction */
#define MISENSOR_TOK_RMW	0x0010  /* RMW operation */
#define MISENSOR_TOK_MASK	0xfff0
#define MISENSOR_AWB_STEADY	(1<<0)	/* awb steady */
#define MISENSOR_AE_READY	(1<<3)	/* ae status ready */

/* mask to set sensor read_mode via misensor_rmw_reg */
#define MISENSOR_R_MODE_MASK	0x0330
/* mask to set sensor vert_flip and horz_mirror */
#define MISENSOR_VFLIP_MASK	0x0002
#define MISENSOR_HFLIP_MASK	0x0001
#define MISENSOR_FLIP_EN	1
#define MISENSOR_FLIP_DIS	0

/* bits set to set sensor read_mode via misensor_rmw_reg */
#define MISENSOR_SKIPPING_SET	0x0011
#define MISENSOR_SUMMING_SET	0x0033
#define MISENSOR_NORMAL_SET	0x0000

/* sensor register that control sensor read-mode and mirror */
#define MISENSOR_READ_MODE	0xC834
/* sensor ae-track status register */
#define MISENSOR_AE_TRACK_STATUS	0xA800
/* sensor awb status register */
#define MISENSOR_AWB_STATUS	0xAC00
/* sensor coarse integration time register */
#define MISENSOR_COARSE_INTEGRATION_TIME 0xC83C

/* registers */
#define REG_SW_RESET                    0x301A
#define REG_SW_STREAM                   0xDC00
#define REG_SCCB_CTRL                   0x3100
#define REG_SC_CMMN_CHIP_ID             0x0000
#define REG_V_START                     0xc800 /* 16bits */
#define REG_H_START                     0xc802 /* 16bits */
#define REG_V_END                       0xc804 /* 16bits */
#define REG_H_END                       0xc806 /* 16bits */
#define REG_PIXEL_CLK                   0xc808 /* 32bits */
#define REG_TIMING_VTS                  0xc812 /* 16bits */
#define REG_TIMING_HTS                  0xc814 /* 16bits */
#define REG_WIDTH                       0xC868 /* 16bits */
#define REG_HEIGHT                      0xC86A /* 16bits */
#define REG_EXPO_COARSE                 0x3012 /* 16bits */
#define REG_EXPO_FINE                   0x3014 /* 16bits */
#define REG_GAIN                        0x305E
#define REG_ANALOGGAIN                  0x305F
#define REG_ADDR_ACESSS                 0x098E /* logical_address_access */
#define REG_COMM_Register               0x0080 /* command_register */

#define SENSOR_DETECTED		1
#define SENSOR_NOT_DETECTED	0

#define I2C_RETRY_COUNT		5
#define MSG_LEN_OFFSET		2

#ifndef MIPI_CONTROL
#define MIPI_CONTROL		0x3400	/* MIPI_Control */
#endif

/* GPIO pin on Moorestown */
#define GPIO_SCLK_25		44
#define GPIO_STB_PIN		47

#define GPIO_STDBY_PIN		49   /* ab:new */
#define GPIO_RESET_PIN		50

/* System control register for Aptina A-1040SOC*/
#define MT9M114_PID		0x0

/* MT9P111_DEVICE_ID */
#define MT9M114_MOD_ID		0x2481

#define MT9M114_FINE_INTG_TIME_MIN 0
#define MT9M114_FINE_INTG_TIME_MAX_MARGIN 0
#define MT9M114_COARSE_INTG_TIME_MIN 1
#define MT9M114_COARSE_INTG_TIME_MAX_MARGIN 6


/* ulBPat; */

#define MT9M114_BPAT_RGRGGBGB	(1 << 0)
#define MT9M114_BPAT_GRGRBGBG	(1 << 1)
#define MT9M114_BPAT_GBGBRGRG	(1 << 2)
#define MT9M114_BPAT_BGBGGRGR	(1 << 3)

#define MT9M114_FOCAL_LENGTH_NUM	208	/*2.08mm*/
#define MT9M114_FOCAL_LENGTH_DEM	100
#define MT9M114_F_NUMBER_DEFAULT_NUM	24
#define MT9M114_F_NUMBER_DEM	10
#define MT9M114_WAIT_STAT_TIMEOUT	100
#define MT9M114_FLICKER_MODE_50HZ	1
#define MT9M114_FLICKER_MODE_60HZ	2
/*
 * focal length bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define MT9M114_FOCAL_LENGTH_DEFAULT 0xD00064

/*
 * current f-number bits definition:
 * bits 31-16: numerator, bits 15-0: denominator
 */
#define MT9M114_F_NUMBER_DEFAULT 0x18000a

/*
 * f-number range bits definition:
 * bits 31-24: max f-number numerator
 * bits 23-16: max f-number denominator
 * bits 15-8: min f-number numerator
 * bits 7-0: min f-number denominator
 */
#define MT9M114_F_NUMBER_RANGE 0x180a180a

/* Supported resolutions */
enum {
	MT9M114_RES_736P,
	MT9M114_RES_864P,
	MT9M114_RES_960P,
};
#define MT9M114_RES_960P_SIZE_H		1296
#define MT9M114_RES_960P_SIZE_V		976
#define MT9M114_RES_720P_SIZE_H		1280
#define MT9M114_RES_720P_SIZE_V		720
#define MT9M114_RES_576P_SIZE_H		1024
#define MT9M114_RES_576P_SIZE_V		576
#define MT9M114_RES_480P_SIZE_H		768
#define MT9M114_RES_480P_SIZE_V		480
#define MT9M114_RES_VGA_SIZE_H		640
#define MT9M114_RES_VGA_SIZE_V		480
#define MT9M114_RES_QVGA_SIZE_H		320
#define MT9M114_RES_QVGA_SIZE_V		240
#define MT9M114_RES_QCIF_SIZE_H		176
#define MT9M114_RES_QCIF_SIZE_V		144

#define MT9M114_RES_720_480p_768_SIZE_H 736
#define MT9M114_RES_720_480p_768_SIZE_V 496
#define MT9M114_RES_736P_SIZE_H 1296
#define MT9M114_RES_736P_SIZE_V 736
#define MT9M114_RES_864P_SIZE_H 1296
#define MT9M114_RES_864P_SIZE_V 864
#define MT9M114_RES_976P_SIZE_H 1296
#define MT9M114_RES_976P_SIZE_V 976

#define MT9M114_BIN_FACTOR_MAX			3

#define MT9M114_DEFAULT_FIRST_EXP 0x10
#define MT9M114_MAX_FIRST_EXP 0x302

/* completion status polling requirements, usage based on Aptina .INI Rev2 */
enum poll_reg {
	NO_POLLING,
	PRE_POLLING,
	POST_POLLING,
};
/*
 * struct misensor_reg - MI sensor  register format
 * @length: length of the register
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 * Define a structure for sensor register initialization values
 */
struct misensor_reg {
	u32 length;
	u32 reg;
	u32 val;	/* value or for read/mod/write, AND mask */
	u32 val2;	/* optional; for rmw, OR mask */
};

/*
 * struct misensor_fwreg - Firmware burst command
 * @type: FW burst or 8/16 bit register
 * @addr: 16-bit offset to register or other values depending on type
 * @valx: data value for burst (or other commands)
 *
 * Define a structure for sensor register initialization values
 */
struct misensor_fwreg {
	u32	type;	/* type of value, register or FW burst string */
	u32	addr;	/* target address */
	u32	val0;
	u32	val1;
	u32	val2;
	u32	val3;
	u32	val4;
	u32	val5;
	u32	val6;
	u32	val7;
};

struct regval_list {
	u16 reg_num;
	u8 value;
};

struct mt9m114_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;

	struct camera_sensor_platform_data *platform_data;
	struct mutex input_lock;	/* serialize sensor's ioctl */
	struct v4l2_ctrl_handler ctrl_handler;
	int real_model_id;
	int nctx;
	int power;

	unsigned int bus_width;
	unsigned int mode;
	unsigned int field_inv;
	unsigned int field_sel;
	unsigned int ycseq;
	unsigned int conv422;
	unsigned int bpat;
	unsigned int hpol;
	unsigned int vpol;
	unsigned int edge;
	unsigned int bls;
	unsigned int gamma;
	unsigned int cconv;
	unsigned int res;
	unsigned int dwn_sz;
	unsigned int blc;
	unsigned int agc;
	unsigned int awb;
	unsigned int aec;
	/* extention SENSOR version 2 */
	unsigned int cie_profile;

	/* extention SENSOR version 3 */
	unsigned int flicker_freq;

	/* extension SENSOR version 4 */
	unsigned int smia_mode;
	unsigned int mipi_mode;

	/* Add name here to load shared library */
	unsigned int type;

	/*Number of MIPI lanes*/
	unsigned int mipi_lanes;
	/*WA for low light AE*/
	unsigned int first_exp;
	unsigned int first_gain;
	unsigned int first_diggain;
	char name[32];

	u8 lightfreq;
	u8 streamon;
};

struct mt9m114_format_struct {
	u8 *desc;
	u32 pixelformat;
	struct regval_list *regs;
};

struct mt9m114_res_struct {
	u8 *desc;
	int res;
	int width;
	int height;
	int fps;
	int skip_frames;
	bool used;
	struct regval_list *regs;
	u16 pixels_per_line;
	u16 lines_per_frame;
	u8 bin_factor_x;
	u8 bin_factor_y;
	u8 bin_mode;
};

/* 2 bytes used for address: 256 bytes total */
#define MT9M114_MAX_WRITE_BUF_SIZE	254
struct mt9m114_write_buffer {
	u16 addr;
	u8 data[MT9M114_MAX_WRITE_BUF_SIZE];
};

struct mt9m114_write_ctrl {
	int index;
	struct mt9m114_write_buffer buffer;
};

/*
 * Modes supported by the mt9m114 driver.
 * Please, keep them in ascending order.
 */
static struct mt9m114_res_struct mt9m114_res[] = {
	{
	.desc	= "720P",
	.res	= MT9M114_RES_736P,
	.width	= 1296,
	.height = 736,
	.fps	= 30,
	.used	= false,
	.regs	= NULL,
	.skip_frames = 1,

	.pixels_per_line = 0x0640,
	.lines_per_frame = 0x0307,
	.bin_factor_x = 1,
	.bin_factor_y = 1,
	.bin_mode = 0,
	},
	{
	.desc	= "848P",
	.res	= MT9M114_RES_864P,
	.width	= 1296,
	.height = 864,
	.fps	= 30,
	.used	= false,
	.regs	= NULL,
	.skip_frames = 1,

	.pixels_per_line = 0x0640,
	.lines_per_frame = 0x03E8,
	.bin_factor_x = 1,
	.bin_factor_y = 1,
	.bin_mode = 0,
	},
	{
	.desc	= "960P",
	.res	= MT9M114_RES_960P,
	.width	= 1296,
	.height	= 976,
	.fps	= 30,
	.used	= false,
	.regs	= NULL,
	.skip_frames = 1,

	.pixels_per_line = 0x0644, /* consistent with regs arrays */
	.lines_per_frame = 0x03E5, /* consistent with regs arrays */
	.bin_factor_x = 1,
	.bin_factor_y = 1,
	.bin_mode = 0,
	},
};
#define N_RES (ARRAY_SIZE(mt9m114_res))

static struct misensor_reg const mt9m114_exitstandby[] = {
	{MISENSOR_16BIT,  0x098E, 0xDC00},
	/* exit-standby */
	{MISENSOR_8BIT,  0xDC00, 0x54},
	{MISENSOR_16BIT,  0x0080, 0x8002},
	{MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_exp_win[5][5] = {
	{
		{MISENSOR_8BIT,  0xA407, 0x64},
		{MISENSOR_8BIT,  0xA408, 0x64},
		{MISENSOR_8BIT,  0xA409, 0x64},
		{MISENSOR_8BIT,  0xA40A, 0x64},
		{MISENSOR_8BIT,  0xA40B, 0x64},
	},
	{
		{MISENSOR_8BIT,  0xA40C, 0x64},
		{MISENSOR_8BIT,  0xA40D, 0x64},
		{MISENSOR_8BIT,  0xA40E, 0x64},
		{MISENSOR_8BIT,  0xA40F, 0x64},
		{MISENSOR_8BIT,  0xA410, 0x64},
	},
	{
		{MISENSOR_8BIT,  0xA411, 0x64},
		{MISENSOR_8BIT,  0xA412, 0x64},
		{MISENSOR_8BIT,  0xA413, 0x64},
		{MISENSOR_8BIT,  0xA414, 0x64},
		{MISENSOR_8BIT,  0xA415, 0x64},
	},
	{
		{MISENSOR_8BIT,  0xA416, 0x64},
		{MISENSOR_8BIT,  0xA417, 0x64},
		{MISENSOR_8BIT,  0xA418, 0x64},
		{MISENSOR_8BIT,  0xA419, 0x64},
		{MISENSOR_8BIT,  0xA41A, 0x64},
	},
	{
		{MISENSOR_8BIT,  0xA41B, 0x64},
		{MISENSOR_8BIT,  0xA41C, 0x64},
		{MISENSOR_8BIT,  0xA41D, 0x64},
		{MISENSOR_8BIT,  0xA41E, 0x64},
		{MISENSOR_8BIT,  0xA41F, 0x64},
	},
};

static struct misensor_reg const mt9m114_exp_average[] = {
	{MISENSOR_8BIT,  0xA407, 0x00},
	{MISENSOR_8BIT,  0xA408, 0x00},
	{MISENSOR_8BIT,  0xA409, 0x00},
	{MISENSOR_8BIT,  0xA40A, 0x00},
	{MISENSOR_8BIT,  0xA40B, 0x00},
	{MISENSOR_8BIT,  0xA40C, 0x00},
	{MISENSOR_8BIT,  0xA40D, 0x00},
	{MISENSOR_8BIT,  0xA40E, 0x00},
	{MISENSOR_8BIT,  0xA40F, 0x00},
	{MISENSOR_8BIT,  0xA410, 0x00},
	{MISENSOR_8BIT,  0xA411, 0x00},
	{MISENSOR_8BIT,  0xA412, 0x00},
	{MISENSOR_8BIT,  0xA413, 0x00},
	{MISENSOR_8BIT,  0xA414, 0x00},
	{MISENSOR_8BIT,  0xA415, 0x00},
	{MISENSOR_8BIT,  0xA416, 0x00},
	{MISENSOR_8BIT,  0xA417, 0x00},
	{MISENSOR_8BIT,  0xA418, 0x00},
	{MISENSOR_8BIT,  0xA419, 0x00},
	{MISENSOR_8BIT,  0xA41A, 0x00},
	{MISENSOR_8BIT,  0xA41B, 0x00},
	{MISENSOR_8BIT,  0xA41C, 0x00},
	{MISENSOR_8BIT,  0xA41D, 0x00},
	{MISENSOR_8BIT,  0xA41E, 0x00},
	{MISENSOR_8BIT,  0xA41F, 0x00},
	{MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_exp_center[] = {
	{MISENSOR_8BIT,  0xA407, 0x19},
	{MISENSOR_8BIT,  0xA408, 0x19},
	{MISENSOR_8BIT,  0xA409, 0x19},
	{MISENSOR_8BIT,  0xA40A, 0x19},
	{MISENSOR_8BIT,  0xA40B, 0x19},
	{MISENSOR_8BIT,  0xA40C, 0x19},
	{MISENSOR_8BIT,  0xA40D, 0x4B},
	{MISENSOR_8BIT,  0xA40E, 0x4B},
	{MISENSOR_8BIT,  0xA40F, 0x4B},
	{MISENSOR_8BIT,  0xA410, 0x19},
	{MISENSOR_8BIT,  0xA411, 0x19},
	{MISENSOR_8BIT,  0xA412, 0x4B},
	{MISENSOR_8BIT,  0xA413, 0x64},
	{MISENSOR_8BIT,  0xA414, 0x4B},
	{MISENSOR_8BIT,  0xA415, 0x19},
	{MISENSOR_8BIT,  0xA416, 0x19},
	{MISENSOR_8BIT,  0xA417, 0x4B},
	{MISENSOR_8BIT,  0xA418, 0x4B},
	{MISENSOR_8BIT,  0xA419, 0x4B},
	{MISENSOR_8BIT,  0xA41A, 0x19},
	{MISENSOR_8BIT,  0xA41B, 0x19},
	{MISENSOR_8BIT,  0xA41C, 0x19},
	{MISENSOR_8BIT,  0xA41D, 0x19},
	{MISENSOR_8BIT,  0xA41E, 0x19},
	{MISENSOR_8BIT,  0xA41F, 0x19},
	{MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_suspend[] = {
	 {MISENSOR_16BIT,  0x098E, 0xDC00},
	 {MISENSOR_8BIT,  0xDC00, 0x40},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_streaming[] = {
	 {MISENSOR_16BIT,  0x098E, 0xDC00},
	 {MISENSOR_8BIT,  0xDC00, 0x34},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_standby_reg[] = {
	 {MISENSOR_16BIT,  0x098E, 0xDC00},
	 {MISENSOR_8BIT,  0xDC00, 0x50},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_wakeup_reg[] = {
	 {MISENSOR_16BIT,  0x098E, 0xDC00},
	 {MISENSOR_8BIT,  0xDC00, 0x54},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_chgstat_reg[] = {
	{MISENSOR_16BIT,  0x098E, 0xDC00},
	{MISENSOR_8BIT,  0xDC00, 0x28},
	{MISENSOR_16BIT,  0x0080, 0x8002},
	{MISENSOR_TOK_TERM, 0, 0}
};

/* [1296x976_30fps] - Intel */
static struct misensor_reg const mt9m114_960P_init[] = {
	{MISENSOR_16BIT, 0x098E, 0x1000},
	{MISENSOR_8BIT, 0xC97E, 0x01},	  /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x0128}, /* cam_sysctl_pll_divider_m_n = 276 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x0000}, /* cam_sensor_cfg_y_addr_start = 0 */
	{MISENSOR_16BIT, 0xC802, 0x0000}, /* cam_sensor_cfg_x_addr_start = 0 */
	{MISENSOR_16BIT, 0xC804, 0x03CF}, /* cam_sensor_cfg_y_addr_end = 971 */
	{MISENSOR_16BIT, 0xC806, 0x050F}, /* cam_sensor_cfg_x_addr_end = 1291 */
	{MISENSOR_16BIT, 0xC808, 0x02DC}, /* cam_sensor_cfg_pixclk = 48000000 */
	{MISENSOR_16BIT, 0xC80A, 0x6C00},
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x00DB},
	/* cam_sensor_cfg_fine_integ_time_max = 1459 */
	{MISENSOR_16BIT, 0xC810, 0x05B3},
	/* cam_sensor_cfg_frame_length_lines = 1006 */
	{MISENSOR_16BIT, 0xC812, 0x03F6},
	/* cam_sensor_cfg_line_length_pck = 1590 */
	{MISENSOR_16BIT, 0xC814, 0x063E},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x0060},
	/* cam_sensor_cfg_cpipe_last_row = 963 */
	{MISENSOR_16BIT, 0xC818, 0x03C3},
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0000}, /* cam_sensor_control_read_mode = 0 */
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x0508}, /* cam_crop_window_width = 1280 */
	{MISENSOR_16BIT, 0xC85A, 0x03C8}, /* cam_crop_window_height = 960 */
	{MISENSOR_8BIT,  0xC85C, 0x03},   /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x0508}, /* cam_output_width = 1280 */
	{MISENSOR_16BIT, 0xC86A, 0x03C8}, /* cam_output_height = 960 */
	{MISENSOR_TOK_TERM, 0, 0},
};

/* [1296x976_30fps_768Mbps] */
static struct misensor_reg const mt9m114_976P_init[] = {
	{MISENSOR_16BIT, 0x98E, 0x1000},
	{MISENSOR_8BIT, 0xC97E, 0x01},	  /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x0128}, /* cam_sysctl_pll_divider_m_n = 276 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x0000}, /* cam_sensor_cfg_y_addr_start = 0 */
	{MISENSOR_16BIT, 0xC802, 0x0000}, /* cam_sensor_cfg_x_addr_start = 0 */
	{MISENSOR_16BIT, 0xC804, 0x03CF}, /* cam_sensor_cfg_y_addr_end = 975 */
	{MISENSOR_16BIT, 0xC806, 0x050F}, /* cam_sensor_cfg_x_addr_end = 1295 */
	{MISENSOR_32BIT, 0xC808, 0x2DC6C00},/* cam_sensor_cfg_pixclk = 480000*/
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x00DB},
	 /* 0x062E //cam_sensor_cfg_fine_integ_time_max = 1459 */
	{MISENSOR_16BIT, 0xC810, 0x05B3},
	/* 0x074C //cam_sensor_cfg_frame_length_lines = 1006 */
	{MISENSOR_16BIT, 0xC812, 0x03E5},
	/* 0x06B1 /cam_sensor_cfg_line_length_pck = 1590 */
	{MISENSOR_16BIT, 0xC814, 0x0644},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x0060},
	/* cam_sensor_cfg_cpipe_last_row = 963 */
	{MISENSOR_16BIT, 0xC818, 0x03C3},
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0000}, /* cam_sensor_control_read_mode = 0 */
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x0508}, /* cam_crop_window_width = 1288 */
	{MISENSOR_16BIT, 0xC85A, 0x03C8}, /* cam_crop_window_height = 968 */
	{MISENSOR_8BIT, 0xC85C, 0x03}, /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x0508}, /* cam_output_width = 1288 */
	{MISENSOR_16BIT, 0xC86A, 0x03C8}, /* cam_output_height = 968 */
	{MISENSOR_8BIT, 0xC878, 0x00}, /* 0x0E //cam_aet_aemode = 0 */
	{MISENSOR_TOK_TERM, 0, 0}
};

/* [1296x864_30fps] */
static struct misensor_reg const mt9m114_864P_init[] = {
	{MISENSOR_16BIT, 0x98E, 0x1000},
	{MISENSOR_8BIT, 0xC97E, 0x01},	  /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x0128}, /* cam_sysctl_pll_divider_m_n = 276 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x0038}, /* cam_sensor_cfg_y_addr_start = 56 */
	{MISENSOR_16BIT, 0xC802, 0x0000}, /* cam_sensor_cfg_x_addr_start = 0 */
	{MISENSOR_16BIT, 0xC804, 0x0397}, /* cam_sensor_cfg_y_addr_end = 919 */
	{MISENSOR_16BIT, 0xC806, 0x050F}, /* cam_sensor_cfg_x_addr_end = 1295 */
	/* cam_sensor_cfg_pixclk = 48000000 */
	{MISENSOR_32BIT, 0xC808, 0x2DC6C00},
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x00DB},
	/* cam_sensor_cfg_fine_integ_time_max = 1469 */
	{MISENSOR_16BIT, 0xC810, 0x05BD},
	/* cam_sensor_cfg_frame_length_lines = 1000 */
	{MISENSOR_16BIT, 0xC812, 0x03E8},
	/* cam_sensor_cfg_line_length_pck = 1600 */
	{MISENSOR_16BIT, 0xC814, 0x0640},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x0060},
	/* cam_sensor_cfg_cpipe_last_row = 859 */
	{MISENSOR_16BIT, 0xC818, 0x035B},
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0000}, /* cam_sensor_control_read_mode = 0 */
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x0508}, /* cam_crop_window_width = 1288 */
	{MISENSOR_16BIT, 0xC85A, 0x0358}, /* cam_crop_window_height = 856 */
	{MISENSOR_8BIT, 0xC85C, 0x03}, /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x0508}, /* cam_output_width = 1288 */
	{MISENSOR_16BIT, 0xC86A, 0x0358}, /* cam_output_height = 856 */
	{MISENSOR_8BIT, 0xC878, 0x00}, /* 0x0E //cam_aet_aemode = 0 */
	{MISENSOR_TOK_TERM, 0, 0}
};

/* [1296x736_30fps] */
static struct misensor_reg const mt9m114_736P_init[] = {
	{MISENSOR_16BIT, 0x98E, 0x1000},
	{MISENSOR_8BIT, 0xC97E, 0x01},	  /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x011F}, /* cam_sysctl_pll_divider_m_n = 287 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x0078}, /* cam_sensor_cfg_y_addr_start = 120*/
	{MISENSOR_16BIT, 0xC802, 0x0000}, /* cam_sensor_cfg_x_addr_start = 0 */
	{MISENSOR_16BIT, 0xC804, 0x0357}, /* cam_sensor_cfg_y_addr_end = 855 */
	{MISENSOR_16BIT, 0xC806, 0x050F}, /* cam_sensor_cfg_x_addr_end = 1295 */
	{MISENSOR_32BIT, 0xC808, 0x237A07F}, /* cam_sensor_cfg_pixclk=37199999*/
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x00DB},
	/* 0x062E //cam_sensor_cfg_fine_integ_time_max = 1469 */
	{MISENSOR_16BIT, 0xC810, 0x05BD},
	/* 0x074C //cam_sensor_cfg_frame_length_lines = 775 */
	{MISENSOR_16BIT, 0xC812, 0x0307},
	/* 0x06B1 /cam_sensor_cfg_line_length_pck = 1600 */
	{MISENSOR_16BIT, 0xC814, 0x0640},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x0060},
	/* cam_sensor_cfg_cpipe_last_row = 731 */
	{MISENSOR_16BIT, 0xC818, 0x02DB},
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0000}, /* cam_sensor_control_read_mode = 0 */
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x0508}, /* cam_crop_window_width = 1288 */
	{MISENSOR_16BIT, 0xC85A, 0x02D8}, /* cam_crop_window_height = 728 */
	{MISENSOR_8BIT, 0xC85C, 0x03}, /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x0508}, /* cam_output_width = 1288 */
	{MISENSOR_16BIT, 0xC86A, 0x02D8}, /* cam_output_height = 728 */
	{MISENSOR_8BIT, 0xC878, 0x00}, /* 0x0E //cam_aet_aemode = 0 */
	{MISENSOR_TOK_TERM, 0, 0}
};

/* [736x496_30fps_768Mbps] */
static struct misensor_reg const mt9m114_720_480P_init[] = {
	{MISENSOR_16BIT, 0x98E, 0x1000},
	{MISENSOR_8BIT, 0xC97E, 0x01},	  /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x0128}, /* cam_sysctl_pll_divider_m_n = 276 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x00F0}, /* cam_sensor_cfg_y_addr_start = 240*/
	{MISENSOR_16BIT, 0xC802, 0x0118}, /* cam_sensor_cfg_x_addr_start = 280*/
	{MISENSOR_16BIT, 0xC804, 0x02DF}, /* cam_sensor_cfg_y_addr_end = 735 */
	{MISENSOR_16BIT, 0xC806, 0x03F7}, /* cam_sensor_cfg_x_addr_end = 1015 */
	/* cam_sensor_cfg_pixclk = 48000000 */
	{MISENSOR_32BIT, 0xC808, 0x2DC6C00},
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x00DB},
	/* 0x062E //cam_sensor_cfg_fine_integ_time_max = 1459 */
	{MISENSOR_16BIT, 0xC810, 0x05B3},
	/* 0x074C //cam_sensor_cfg_frame_length_lines = 997 */
	{MISENSOR_16BIT, 0xC812, 0x03E5},
	/* 0x06B1 /cam_sensor_cfg_line_length_pck = 1604 */
	{MISENSOR_16BIT, 0xC814, 0x0644},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x0060},
	{MISENSOR_16BIT, 0xC818, 0x03C3}, /* cam_sensor_cfg_cpipe_last_row=963*/
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0000}, /* cam_sensor_control_read_mode = 0*/
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x02D8}, /* cam_crop_window_width = 728 */
	{MISENSOR_16BIT, 0xC85A, 0x01E8}, /* cam_crop_window_height = 488 */
	{MISENSOR_8BIT, 0xC85C, 0x03}, /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x02D8}, /* cam_output_width = 728 */
	{MISENSOR_16BIT, 0xC86A, 0x01E8}, /* cam_output_height = 488 */
	{MISENSOR_8BIT, 0xC878, 0x00}, /* 0x0E //cam_aet_aemode = 0 */
	{MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_common[] = {
	/* reset */
	{MISENSOR_16BIT,  0x301A, 0x0234},
	/* LOAD = Step2-PLL_Timing      //PLL and Timing */
	{MISENSOR_16BIT, 0x098E, 0x1000}, /* LOGICAL_ADDRESS_ACCESS */
	{MISENSOR_8BIT, 0xC97E, 0x01},    /* cam_sysctl_pll_enable = 1 */
	{MISENSOR_16BIT, 0xC980, 0x0128}, /* cam_sysctl_pll_divider_m_n = 276 */
	{MISENSOR_16BIT, 0xC982, 0x0700}, /* cam_sysctl_pll_divider_p = 1792 */
	{MISENSOR_16BIT, 0xC800, 0x0000}, /* cam_sensor_cfg_y_addr_start = 216*/
	{MISENSOR_16BIT, 0xC802, 0x0000}, /* cam_sensor_cfg_x_addr_start = 168*/
	{MISENSOR_16BIT, 0xC804, 0x03CD}, /* cam_sensor_cfg_y_addr_end = 761 */
	{MISENSOR_16BIT, 0xC806, 0x050D}, /* cam_sensor_cfg_x_addr_end = 1127 */
	{MISENSOR_16BIT, 0xC808, 0x02DC}, /* cam_sensor_cfg_pixclk = 24000000 */
	{MISENSOR_16BIT, 0xC80A, 0x6C00},
	{MISENSOR_16BIT, 0xC80C, 0x0001}, /* cam_sensor_cfg_row_speed = 1 */
	/* cam_sensor_cfg_fine_integ_time_min = 219 */
	{MISENSOR_16BIT, 0xC80E, 0x01C3},
	/* cam_sensor_cfg_fine_integ_time_max = 1149 */
	{MISENSOR_16BIT, 0xC810, 0x03F7},
	/* cam_sensor_cfg_frame_length_lines = 625 */
	{MISENSOR_16BIT, 0xC812, 0x0500},
	/* cam_sensor_cfg_line_length_pck = 1280 */
	{MISENSOR_16BIT, 0xC814, 0x04E2},
	/* cam_sensor_cfg_fine_correction = 96 */
	{MISENSOR_16BIT, 0xC816, 0x00E0},
	/* cam_sensor_cfg_cpipe_last_row = 541 */
	{MISENSOR_16BIT, 0xC818, 0x01E3},
	{MISENSOR_16BIT, 0xC826, 0x0020}, /* cam_sensor_cfg_reg_0_data = 32 */
	{MISENSOR_16BIT, 0xC834, 0x0330}, /* cam_sensor_control_read_mode = 0 */
	{MISENSOR_16BIT, 0xC854, 0x0000}, /* cam_crop_window_xoffset = 0 */
	{MISENSOR_16BIT, 0xC856, 0x0000}, /* cam_crop_window_yoffset = 0 */
	{MISENSOR_16BIT, 0xC858, 0x0280}, /* cam_crop_window_width = 952 */
	{MISENSOR_16BIT, 0xC85A, 0x01E0}, /* cam_crop_window_height = 538 */
	{MISENSOR_8BIT, 0xC85C, 0x03},    /* cam_crop_cropmode = 3 */
	{MISENSOR_16BIT, 0xC868, 0x0280}, /* cam_output_width = 952 */
	{MISENSOR_16BIT, 0xC86A, 0x01E0}, /* cam_output_height = 538 */
	/* LOAD = Step3-Recommended
	 * Patch,Errata and Sensor optimization Setting */
	{MISENSOR_16BIT, 0x316A, 0x8270}, /* DAC_TXLO_ROW */
	{MISENSOR_16BIT, 0x316C, 0x8270}, /* DAC_TXLO */
	{MISENSOR_16BIT, 0x3ED0, 0x2305}, /* DAC_LD_4_5 */
	{MISENSOR_16BIT, 0x3ED2, 0x77CF}, /* DAC_LD_6_7 */
	{MISENSOR_16BIT, 0x316E, 0x8202}, /* DAC_ECL */
	{MISENSOR_16BIT, 0x3180, 0x87FF}, /* DELTA_DK_CONTROL */
	{MISENSOR_16BIT, 0x30D4, 0x6080}, /* COLUMN_CORRECTION */
	{MISENSOR_16BIT, 0xA802, 0x0008}, /* AE_TRACK_MODE */
	{MISENSOR_16BIT, 0x3E14, 0xFF39}, /* SAMP_COL_PUP2 */
	{MISENSOR_16BIT, 0x31E0, 0x0003}, /* PIX_DEF_ID */
	/* LOAD = Step8-Features	//Ports, special features, etc. */
	{MISENSOR_16BIT, 0x098E, 0x0000}, /* LOGICAL_ADDRESS_ACCESS */
	{MISENSOR_16BIT, 0x001E, 0x0777}, /* PAD_SLEW */
	{MISENSOR_16BIT, 0x098E, 0x0000}, /* LOGICAL_ADDRESS_ACCESS */
	{MISENSOR_16BIT, 0xC984, 0x8001}, /* CAM_PORT_OUTPUT_CONTROL */
	{MISENSOR_16BIT, 0xC988, 0x0F00}, /* CAM_PORT_MIPI_TIMING_T_HS_ZERO */
	/* CAM_PORT_MIPI_TIMING_T_HS_EXIT_HS_TRAIL */
	{MISENSOR_16BIT, 0xC98A, 0x0B07},
	/* CAM_PORT_MIPI_TIMING_T_CLK_POST_CLK_PRE */
	{MISENSOR_16BIT, 0xC98C, 0x0D01},
	/* CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_CLK_ZERO */
	{MISENSOR_16BIT, 0xC98E, 0x071D},
	{MISENSOR_16BIT, 0xC990, 0x0006}, /* CAM_PORT_MIPI_TIMING_T_LPX */
	{MISENSOR_16BIT, 0xC992, 0x0A0C}, /* CAM_PORT_MIPI_TIMING_INIT_TIMING */
	{MISENSOR_16BIT, 0x3C5A, 0x0009}, /* MIPI_DELAY_TRIM */
	{MISENSOR_16BIT, 0xC86C, 0x0210}, /* CAM_OUTPUT_FORMAT */
	{MISENSOR_16BIT, 0xA804, 0x0000}, /* AE_TRACK_ALGO */
	/* default exposure */
	{MISENSOR_16BIT, 0x3012, 0x0110}, /* COMMAND_REGISTER */
	{MISENSOR_TOK_TERM, 0, 0},

};

static struct misensor_reg const mt9m114_antiflicker_50hz[] = {
	 {MISENSOR_16BIT,  0x098E, 0xC88B},
	 {MISENSOR_8BIT,  0xC88B, 0x32},
	 {MISENSOR_8BIT,  0xDC00, 0x28},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_antiflicker_60hz[] = {
	 {MISENSOR_16BIT,  0x098E, 0xC88B},
	 {MISENSOR_8BIT,  0xC88B, 0x3C},
	 {MISENSOR_8BIT,  0xDC00, 0x28},
	 {MISENSOR_16BIT,  0x0080, 0x8002},
	 {MISENSOR_TOK_TERM, 0, 0}
};

static struct misensor_reg const mt9m114_iq[] = {
	/* [Step3-Recommended] [Sensor optimization] */
	{MISENSOR_16BIT,	0x316A, 0x8270},
	{MISENSOR_16BIT,	0x316C, 0x8270},
	{MISENSOR_16BIT,	0x3ED0, 0x2305},
	{MISENSOR_16BIT,	0x3ED2, 0x77CF},
	{MISENSOR_16BIT,	0x316E, 0x8202},
	{MISENSOR_16BIT,	0x3180, 0x87FF},
	{MISENSOR_16BIT,	0x30D4, 0x6080},
	{MISENSOR_16BIT,	0xA802, 0x0008},

	/* This register is from vender to avoid low light color noise */
	{MISENSOR_16BIT,	0x31E0, 0x0001},

	/* LOAD=Errata item 1 */
	{MISENSOR_16BIT,	0x3E14, 0xFF39},

	/* LOAD=Errata item 2 */
	{MISENSOR_16BIT,	0x301A, 0x8234},

	/*
	 * LOAD=Errata item 3
	 * LOAD=Patch 0202;
	 * Feature Recommended; Black level correction fix
	 */
	{MISENSOR_16BIT,	0x0982, 0x0001},
	{MISENSOR_16BIT,	0x098A, 0x5000},
	{MISENSOR_16BIT,	0xD000, 0x70CF},
	{MISENSOR_16BIT,	0xD002, 0xFFFF},
	{MISENSOR_16BIT,	0xD004, 0xC5D4},
	{MISENSOR_16BIT,	0xD006, 0x903A},
	{MISENSOR_16BIT,	0xD008, 0x2144},
	{MISENSOR_16BIT,	0xD00A, 0x0C00},
	{MISENSOR_16BIT,	0xD00C, 0x2186},
	{MISENSOR_16BIT,	0xD00E, 0x0FF3},
	{MISENSOR_16BIT,	0xD010, 0xB844},
	{MISENSOR_16BIT,	0xD012, 0xB948},
	{MISENSOR_16BIT,	0xD014, 0xE082},
	{MISENSOR_16BIT,	0xD016, 0x20CC},
	{MISENSOR_16BIT,	0xD018, 0x80E2},
	{MISENSOR_16BIT,	0xD01A, 0x21CC},
	{MISENSOR_16BIT,	0xD01C, 0x80A2},
	{MISENSOR_16BIT,	0xD01E, 0x21CC},
	{MISENSOR_16BIT,	0xD020, 0x80E2},
	{MISENSOR_16BIT,	0xD022, 0xF404},
	{MISENSOR_16BIT,	0xD024, 0xD801},
	{MISENSOR_16BIT,	0xD026, 0xF003},
	{MISENSOR_16BIT,	0xD028, 0xD800},
	{MISENSOR_16BIT,	0xD02A, 0x7EE0},
	{MISENSOR_16BIT,	0xD02C, 0xC0F1},
	{MISENSOR_16BIT,	0xD02E, 0x08BA},

	{MISENSOR_16BIT,	0xD030, 0x0600},
	{MISENSOR_16BIT,	0xD032, 0xC1A1},
	{MISENSOR_16BIT,	0xD034, 0x76CF},
	{MISENSOR_16BIT,	0xD036, 0xFFFF},
	{MISENSOR_16BIT,	0xD038, 0xC130},
	{MISENSOR_16BIT,	0xD03A, 0x6E04},
	{MISENSOR_16BIT,	0xD03C, 0xC040},
	{MISENSOR_16BIT,	0xD03E, 0x71CF},
	{MISENSOR_16BIT,	0xD040, 0xFFFF},
	{MISENSOR_16BIT,	0xD042, 0xC790},
	{MISENSOR_16BIT,	0xD044, 0x8103},
	{MISENSOR_16BIT,	0xD046, 0x77CF},
	{MISENSOR_16BIT,	0xD048, 0xFFFF},
	{MISENSOR_16BIT,	0xD04A, 0xC7C0},
	{MISENSOR_16BIT,	0xD04C, 0xE001},
	{MISENSOR_16BIT,	0xD04E, 0xA103},
	{MISENSOR_16BIT,	0xD050, 0xD800},
	{MISENSOR_16BIT,	0xD052, 0x0C6A},
	{MISENSOR_16BIT,	0xD054, 0x04E0},
	{MISENSOR_16BIT,	0xD056, 0xB89E},
	{MISENSOR_16BIT,	0xD058, 0x7508},
	{MISENSOR_16BIT,	0xD05A, 0x8E1C},
	{MISENSOR_16BIT,	0xD05C, 0x0809},
	{MISENSOR_16BIT,	0xD05E, 0x0191},

	{MISENSOR_16BIT,	0xD060, 0xD801},
	{MISENSOR_16BIT,	0xD062, 0xAE1D},
	{MISENSOR_16BIT,	0xD064, 0xE580},
	{MISENSOR_16BIT,	0xD066, 0x20CA},
	{MISENSOR_16BIT,	0xD068, 0x0022},
	{MISENSOR_16BIT,	0xD06A, 0x20CF},
	{MISENSOR_16BIT,	0xD06C, 0x0522},
	{MISENSOR_16BIT,	0xD06E, 0x0C5C},
	{MISENSOR_16BIT,	0xD070, 0x04E2},
	{MISENSOR_16BIT,	0xD072, 0x21CA},
	{MISENSOR_16BIT,	0xD074, 0x0062},
	{MISENSOR_16BIT,	0xD076, 0xE580},
	{MISENSOR_16BIT,	0xD078, 0xD901},
	{MISENSOR_16BIT,	0xD07A, 0x79C0},
	{MISENSOR_16BIT,	0xD07C, 0xD800},
	{MISENSOR_16BIT,	0xD07E, 0x0BE6},
	{MISENSOR_16BIT,	0xD080, 0x04E0},
	{MISENSOR_16BIT,	0xD082, 0xB89E},
	{MISENSOR_16BIT,	0xD084, 0x70CF},
	{MISENSOR_16BIT,	0xD086, 0xFFFF},
	{MISENSOR_16BIT,	0xD088, 0xC8D4},
	{MISENSOR_16BIT,	0xD08A, 0x9002},
	{MISENSOR_16BIT,	0xD08C, 0x0857},
	{MISENSOR_16BIT,	0xD08E, 0x025E},

	{MISENSOR_16BIT,	0xD090, 0xFFDC},
	{MISENSOR_16BIT,	0xD092, 0xE080},
	{MISENSOR_16BIT,	0xD094, 0x25CC},
	{MISENSOR_16BIT,	0xD096, 0x9022},
	{MISENSOR_16BIT,	0xD098, 0xF225},
	{MISENSOR_16BIT,	0xD09A, 0x1700},
	{MISENSOR_16BIT,	0xD09C, 0x108A},
	{MISENSOR_16BIT,	0xD09E, 0x73CF},
	{MISENSOR_16BIT,	0xD0A0, 0xFF00},
	{MISENSOR_16BIT,	0xD0A2, 0x3174},
	{MISENSOR_16BIT,	0xD0A4, 0x9307},
	{MISENSOR_16BIT,	0xD0A6, 0x2A04},
	{MISENSOR_16BIT,	0xD0A8, 0x103E},
	{MISENSOR_16BIT,	0xD0AA, 0x9328},
	{MISENSOR_16BIT,	0xD0AC, 0x2942},
	{MISENSOR_16BIT,	0xD0AE, 0x7140},
	{MISENSOR_16BIT,	0xD0B0, 0x2A04},
	{MISENSOR_16BIT,	0xD0B2, 0x107E},
	{MISENSOR_16BIT,	0xD0B4, 0x9349},
	{MISENSOR_16BIT,	0xD0B6, 0x2942},
	{MISENSOR_16BIT,	0xD0B8, 0x7141},
	{MISENSOR_16BIT,	0xD0BA, 0x2A04},
	{MISENSOR_16BIT,	0xD0BC, 0x10BE},
	{MISENSOR_16BIT,	0xD0BE, 0x934A},

	{MISENSOR_16BIT,	0xD0C0, 0x2942},
	{MISENSOR_16BIT,	0xD0C2, 0x714B},
	{MISENSOR_16BIT,	0xD0C4, 0x2A04},
	{MISENSOR_16BIT,	0xD0C6, 0x10BE},
	{MISENSOR_16BIT,	0xD0C8, 0x130C},
	{MISENSOR_16BIT,	0xD0CA, 0x010A},
	{MISENSOR_16BIT,	0xD0CC, 0x2942},
	{MISENSOR_16BIT,	0xD0CE, 0x7142},
	{MISENSOR_16BIT,	0xD0D0, 0x2250},
	{MISENSOR_16BIT,	0xD0D2, 0x13CA},
	{MISENSOR_16BIT,	0xD0D4, 0x1B0C},
	{MISENSOR_16BIT,	0xD0D6, 0x0284},
	{MISENSOR_16BIT,	0xD0D8, 0xB307},
	{MISENSOR_16BIT,	0xD0DA, 0xB328},
	{MISENSOR_16BIT,	0xD0DC, 0x1B12},
	{MISENSOR_16BIT,	0xD0DE, 0x02C4},
	{MISENSOR_16BIT,	0xD0E0, 0xB34A},
	{MISENSOR_16BIT,	0xD0E2, 0xED88},
	{MISENSOR_16BIT,	0xD0E4, 0x71CF},
	{MISENSOR_16BIT,	0xD0E6, 0xFF00},
	{MISENSOR_16BIT,	0xD0E8, 0x3174},
	{MISENSOR_16BIT,	0xD0EA, 0x9106},
	{MISENSOR_16BIT,	0xD0EC, 0xB88F},
	{MISENSOR_16BIT,	0xD0EE, 0xB106},

	{MISENSOR_16BIT,	0xD0F0, 0x210A},
	{MISENSOR_16BIT,	0xD0F2, 0x8340},
	{MISENSOR_16BIT,	0xD0F4, 0xC000},
	{MISENSOR_16BIT,	0xD0F6, 0x21CA},
	{MISENSOR_16BIT,	0xD0F8, 0x0062},
	{MISENSOR_16BIT,	0xD0FA, 0x20F0},
	{MISENSOR_16BIT,	0xD0FC, 0x0040},
	{MISENSOR_16BIT,	0xD0FE, 0x0B02},
	{MISENSOR_16BIT,	0xD100, 0x0320},
	{MISENSOR_16BIT,	0xD102, 0xD901},
	{MISENSOR_16BIT,	0xD104, 0x07F1},
	{MISENSOR_16BIT,	0xD106, 0x05E0},
	{MISENSOR_16BIT,	0xD108, 0xC0A1},
	{MISENSOR_16BIT,	0xD10A, 0x78E0},
	{MISENSOR_16BIT,	0xD10C, 0xC0F1},
	{MISENSOR_16BIT,	0xD10E, 0x71CF},
	{MISENSOR_16BIT,	0xD110, 0xFFFF},
	{MISENSOR_16BIT,	0xD112, 0xC7C0},
	{MISENSOR_16BIT,	0xD114, 0xD840},
	{MISENSOR_16BIT,	0xD116, 0xA900},
	{MISENSOR_16BIT,	0xD118, 0x71CF},
	{MISENSOR_16BIT,	0xD11A, 0xFFFF},
	{MISENSOR_16BIT,	0xD11C, 0xD02C},
	{MISENSOR_16BIT,	0xD11E, 0xD81E},

	{MISENSOR_16BIT,	0xD120, 0x0A5A},
	{MISENSOR_16BIT,	0xD122, 0x04E0},
	{MISENSOR_16BIT,	0xD124, 0xDA00},
	{MISENSOR_16BIT,	0xD126, 0xD800},
	{MISENSOR_16BIT,	0xD128, 0xC0D1},
	{MISENSOR_16BIT,	0xD12A, 0x7EE0},

	{MISENSOR_16BIT,	0x098E, 0x0000},
	{MISENSOR_16BIT,	0xE000, 0x010C},
	{MISENSOR_16BIT,	0xE002, 0x0202},
	{MISENSOR_16BIT,	0xE004, 0x4103},
	{MISENSOR_16BIT,	0xE006, 0x0202},
	{MISENSOR_16BIT,	0x0080, 0xFFF0},
	{MISENSOR_16BIT,	0x0080, 0xFFF1},

	/* LOAD=Patch 0302; Feature Recommended; Adaptive Sensitivity */
	{MISENSOR_16BIT,	0x0982, 0x0001},
	{MISENSOR_16BIT,	0x098A, 0x512C},
	{MISENSOR_16BIT,	0xD12C, 0x70CF},
	{MISENSOR_16BIT,	0xD12E, 0xFFFF},
	{MISENSOR_16BIT,	0xD130, 0xC5D4},
	{MISENSOR_16BIT,	0xD132, 0x903A},
	{MISENSOR_16BIT,	0xD134, 0x2144},
	{MISENSOR_16BIT,	0xD136, 0x0C00},
	{MISENSOR_16BIT,	0xD138, 0x2186},
	{MISENSOR_16BIT,	0xD13A, 0x0FF3},
	{MISENSOR_16BIT,	0xD13C, 0xB844},
	{MISENSOR_16BIT,	0xD13E, 0x262F},
	{MISENSOR_16BIT,	0xD140, 0xF008},
	{MISENSOR_16BIT,	0xD142, 0xB948},
	{MISENSOR_16BIT,	0xD144, 0x21CC},
	{MISENSOR_16BIT,	0xD146, 0x8021},
	{MISENSOR_16BIT,	0xD148, 0xD801},
	{MISENSOR_16BIT,	0xD14A, 0xF203},
	{MISENSOR_16BIT,	0xD14C, 0xD800},
	{MISENSOR_16BIT,	0xD14E, 0x7EE0},
	{MISENSOR_16BIT,	0xD150, 0xC0F1},
	{MISENSOR_16BIT,	0xD152, 0x71CF},
	{MISENSOR_16BIT,	0xD154, 0xFFFF},
	{MISENSOR_16BIT,	0xD156, 0xC610},
	{MISENSOR_16BIT,	0xD158, 0x910E},
	{MISENSOR_16BIT,	0xD15A, 0x208C},
	{MISENSOR_16BIT,	0xD15C, 0x8014},
	{MISENSOR_16BIT,	0xD15E, 0xF418},
	{MISENSOR_16BIT,	0xD160, 0x910F},
	{MISENSOR_16BIT,	0xD162, 0x208C},
	{MISENSOR_16BIT,	0xD164, 0x800F},
	{MISENSOR_16BIT,	0xD166, 0xF414},
	{MISENSOR_16BIT,	0xD168, 0x9116},
	{MISENSOR_16BIT,	0xD16A, 0x208C},
	{MISENSOR_16BIT,	0xD16C, 0x800A},
	{MISENSOR_16BIT,	0xD16E, 0xF410},
	{MISENSOR_16BIT,	0xD170, 0x9117},
	{MISENSOR_16BIT,	0xD172, 0x208C},
	{MISENSOR_16BIT,	0xD174, 0x8807},
	{MISENSOR_16BIT,	0xD176, 0xF40C},
	{MISENSOR_16BIT,	0xD178, 0x9118},
	{MISENSOR_16BIT,	0xD17A, 0x2086},
	{MISENSOR_16BIT,	0xD17C, 0x0FF3},
	{MISENSOR_16BIT,	0xD17E, 0xB848},
	{MISENSOR_16BIT,	0xD180, 0x080D},
	{MISENSOR_16BIT,	0xD182, 0x0090},
	{MISENSOR_16BIT,	0xD184, 0xFFEA},
	{MISENSOR_16BIT,	0xD186, 0xE081},
	{MISENSOR_16BIT,	0xD188, 0xD801},
	{MISENSOR_16BIT,	0xD18A, 0xF203},
	{MISENSOR_16BIT,	0xD18C, 0xD800},
	{MISENSOR_16BIT,	0xD18E, 0xC0D1},
	{MISENSOR_16BIT,	0xD190, 0x7EE0},
	{MISENSOR_16BIT,	0xD192, 0x78E0},
	{MISENSOR_16BIT,	0xD194, 0xC0F1},
	{MISENSOR_16BIT,	0xD196, 0x71CF},
	{MISENSOR_16BIT,	0xD198, 0xFFFF},
	{MISENSOR_16BIT,	0xD19A, 0xC610},
	{MISENSOR_16BIT,	0xD19C, 0x910E},
	{MISENSOR_16BIT,	0xD19E, 0x208C},
	{MISENSOR_16BIT,	0xD1A0, 0x800A},
	{MISENSOR_16BIT,	0xD1A2, 0xF418},
	{MISENSOR_16BIT,	0xD1A4, 0x910F},
	{MISENSOR_16BIT,	0xD1A6, 0x208C},
	{MISENSOR_16BIT,	0xD1A8, 0x8807},
	{MISENSOR_16BIT,	0xD1AA, 0xF414},
	{MISENSOR_16BIT,	0xD1AC, 0x9116},
	{MISENSOR_16BIT,	0xD1AE, 0x208C},
	{MISENSOR_16BIT,	0xD1B0, 0x800A},
	{MISENSOR_16BIT,	0xD1B2, 0xF410},
	{MISENSOR_16BIT,	0xD1B4, 0x9117},
	{MISENSOR_16BIT,	0xD1B6, 0x208C},
	{MISENSOR_16BIT,	0xD1B8, 0x8807},
	{MISENSOR_16BIT,	0xD1BA, 0xF40C},
	{MISENSOR_16BIT,	0xD1BC, 0x9118},
	{MISENSOR_16BIT,	0xD1BE, 0x2086},
	{MISENSOR_16BIT,	0xD1C0, 0x0FF3},
	{MISENSOR_16BIT,	0xD1C2, 0xB848},
	{MISENSOR_16BIT,	0xD1C4, 0x080D},
	{MISENSOR_16BIT,	0xD1C6, 0x0090},
	{MISENSOR_16BIT,	0xD1C8, 0xFFD9},
	{MISENSOR_16BIT,	0xD1CA, 0xE080},
	{MISENSOR_16BIT,	0xD1CC, 0xD801},
	{MISENSOR_16BIT,	0xD1CE, 0xF203},
	{MISENSOR_16BIT,	0xD1D0, 0xD800},
	{MISENSOR_16BIT,	0xD1D2, 0xF1DF},
	{MISENSOR_16BIT,	0xD1D4, 0x9040},
	{MISENSOR_16BIT,	0xD1D6, 0x71CF},
	{MISENSOR_16BIT,	0xD1D8, 0xFFFF},
	{MISENSOR_16BIT,	0xD1DA, 0xC5D4},
	{MISENSOR_16BIT,	0xD1DC, 0xB15A},
	{MISENSOR_16BIT,	0xD1DE, 0x9041},
	{MISENSOR_16BIT,	0xD1E0, 0x73CF},
	{MISENSOR_16BIT,	0xD1E2, 0xFFFF},
	{MISENSOR_16BIT,	0xD1E4, 0xC7D0},
	{MISENSOR_16BIT,	0xD1E6, 0xB140},
	{MISENSOR_16BIT,	0xD1E8, 0x9042},
	{MISENSOR_16BIT,	0xD1EA, 0xB141},
	{MISENSOR_16BIT,	0xD1EC, 0x9043},
	{MISENSOR_16BIT,	0xD1EE, 0xB142},
	{MISENSOR_16BIT,	0xD1F0, 0x9044},
	{MISENSOR_16BIT,	0xD1F2, 0xB143},
	{MISENSOR_16BIT,	0xD1F4, 0x9045},
	{MISENSOR_16BIT,	0xD1F6, 0xB147},
	{MISENSOR_16BIT,	0xD1F8, 0x9046},
	{MISENSOR_16BIT,	0xD1FA, 0xB148},
	{MISENSOR_16BIT,	0xD1FC, 0x9047},
	{MISENSOR_16BIT,	0xD1FE, 0xB14B},
	{MISENSOR_16BIT,	0xD200, 0x9048},
	{MISENSOR_16BIT,	0xD202, 0xB14C},
	{MISENSOR_16BIT,	0xD204, 0x9049},
	{MISENSOR_16BIT,	0xD206, 0x1958},
	{MISENSOR_16BIT,	0xD208, 0x0084},
	{MISENSOR_16BIT,	0xD20A, 0x904A},
	{MISENSOR_16BIT,	0xD20C, 0x195A},
	{MISENSOR_16BIT,	0xD20E, 0x0084},
	{MISENSOR_16BIT,	0xD210, 0x8856},
	{MISENSOR_16BIT,	0xD212, 0x1B36},
	{MISENSOR_16BIT,	0xD214, 0x8082},
	{MISENSOR_16BIT,	0xD216, 0x8857},
	{MISENSOR_16BIT,	0xD218, 0x1B37},
	{MISENSOR_16BIT,	0xD21A, 0x8082},
	{MISENSOR_16BIT,	0xD21C, 0x904C},
	{MISENSOR_16BIT,	0xD21E, 0x19A7},
	{MISENSOR_16BIT,	0xD220, 0x009C},
	{MISENSOR_16BIT,	0xD222, 0x881A},
	{MISENSOR_16BIT,	0xD224, 0x7FE0},
	{MISENSOR_16BIT,	0xD226, 0x1B54},
	{MISENSOR_16BIT,	0xD228, 0x8002},
	{MISENSOR_16BIT,	0xD22A, 0x78E0},
	{MISENSOR_16BIT,	0xD22C, 0x71CF},
	{MISENSOR_16BIT,	0xD22E, 0xFFFF},
	{MISENSOR_16BIT,	0xD230, 0xC350},
	{MISENSOR_16BIT,	0xD232, 0xD828},
	{MISENSOR_16BIT,	0xD234, 0xA90B},
	{MISENSOR_16BIT,	0xD236, 0x8100},
	{MISENSOR_16BIT,	0xD238, 0x01C5},
	{MISENSOR_16BIT,	0xD23A, 0x0320},
	{MISENSOR_16BIT,	0xD23C, 0xD900},
	{MISENSOR_16BIT,	0xD23E, 0x78E0},
	{MISENSOR_16BIT,	0xD240, 0x220A},
	{MISENSOR_16BIT,	0xD242, 0x1F80},
	{MISENSOR_16BIT,	0xD244, 0xFFFF},
	{MISENSOR_16BIT,	0xD246, 0xD4E0},
	{MISENSOR_16BIT,	0xD248, 0xC0F1},
	{MISENSOR_16BIT,	0xD24A, 0x0811},
	{MISENSOR_16BIT,	0xD24C, 0x0051},
	{MISENSOR_16BIT,	0xD24E, 0x2240},
	{MISENSOR_16BIT,	0xD250, 0x1200},
	{MISENSOR_16BIT,	0xD252, 0xFFE1},
	{MISENSOR_16BIT,	0xD254, 0xD801},
	{MISENSOR_16BIT,	0xD256, 0xF006},
	{MISENSOR_16BIT,	0xD258, 0x2240},
	{MISENSOR_16BIT,	0xD25A, 0x1900},
	{MISENSOR_16BIT,	0xD25C, 0xFFDE},
	{MISENSOR_16BIT,	0xD25E, 0xD802},
	{MISENSOR_16BIT,	0xD260, 0x1A05},
	{MISENSOR_16BIT,	0xD262, 0x1002},
	{MISENSOR_16BIT,	0xD264, 0xFFF2},
	{MISENSOR_16BIT,	0xD266, 0xF195},
	{MISENSOR_16BIT,	0xD268, 0xC0F1},
	{MISENSOR_16BIT,	0xD26A, 0x0E7E},
	{MISENSOR_16BIT,	0xD26C, 0x05C0},
	{MISENSOR_16BIT,	0xD26E, 0x75CF},
	{MISENSOR_16BIT,	0xD270, 0xFFFF},
	{MISENSOR_16BIT,	0xD272, 0xC84C},
	{MISENSOR_16BIT,	0xD274, 0x9502},
	{MISENSOR_16BIT,	0xD276, 0x77CF},
	{MISENSOR_16BIT,	0xD278, 0xFFFF},
	{MISENSOR_16BIT,	0xD27A, 0xC344},
	{MISENSOR_16BIT,	0xD27C, 0x2044},
	{MISENSOR_16BIT,	0xD27E, 0x008E},
	{MISENSOR_16BIT,	0xD280, 0xB8A1},
	{MISENSOR_16BIT,	0xD282, 0x0926},
	{MISENSOR_16BIT,	0xD284, 0x03E0},
	{MISENSOR_16BIT,	0xD286, 0xB502},
	{MISENSOR_16BIT,	0xD288, 0x9502},
	{MISENSOR_16BIT,	0xD28A, 0x952E},
	{MISENSOR_16BIT,	0xD28C, 0x7E05},
	{MISENSOR_16BIT,	0xD28E, 0xB5C2},
	{MISENSOR_16BIT,	0xD290, 0x70CF},
	{MISENSOR_16BIT,	0xD292, 0xFFFF},
	{MISENSOR_16BIT,	0xD294, 0xC610},
	{MISENSOR_16BIT,	0xD296, 0x099A},
	{MISENSOR_16BIT,	0xD298, 0x04A0},
	{MISENSOR_16BIT,	0xD29A, 0xB026},
	{MISENSOR_16BIT,	0xD29C, 0x0E02},
	{MISENSOR_16BIT,	0xD29E, 0x0560},
	{MISENSOR_16BIT,	0xD2A0, 0xDE00},
	{MISENSOR_16BIT,	0xD2A2, 0x0A12},
	{MISENSOR_16BIT,	0xD2A4, 0x0320},
	{MISENSOR_16BIT,	0xD2A6, 0xB7C4},
	{MISENSOR_16BIT,	0xD2A8, 0x0B36},
	{MISENSOR_16BIT,	0xD2AA, 0x03A0},
	{MISENSOR_16BIT,	0xD2AC, 0x70C9},
	{MISENSOR_16BIT,	0xD2AE, 0x9502},
	{MISENSOR_16BIT,	0xD2B0, 0x7608},
	{MISENSOR_16BIT,	0xD2B2, 0xB8A8},
	{MISENSOR_16BIT,	0xD2B4, 0xB502},
	{MISENSOR_16BIT,	0xD2B6, 0x70CF},
	{MISENSOR_16BIT,	0xD2B8, 0x0000},
	{MISENSOR_16BIT,	0xD2BA, 0x5536},
	{MISENSOR_16BIT,	0xD2BC, 0x7860},
	{MISENSOR_16BIT,	0xD2BE, 0x2686},
	{MISENSOR_16BIT,	0xD2C0, 0x1FFB},
	{MISENSOR_16BIT,	0xD2C2, 0x9502},
	{MISENSOR_16BIT,	0xD2C4, 0x78C5},
	{MISENSOR_16BIT,	0xD2C6, 0x0631},
	{MISENSOR_16BIT,	0xD2C8, 0x05E0},
	{MISENSOR_16BIT,	0xD2CA, 0xB502},
	{MISENSOR_16BIT,	0xD2CC, 0x72CF},
	{MISENSOR_16BIT,	0xD2CE, 0xFFFF},
	{MISENSOR_16BIT,	0xD2D0, 0xC5D4},
	{MISENSOR_16BIT,	0xD2D2, 0x923A},
	{MISENSOR_16BIT,	0xD2D4, 0x73CF},
	{MISENSOR_16BIT,	0xD2D6, 0xFFFF},
	{MISENSOR_16BIT,	0xD2D8, 0xC7D0},
	{MISENSOR_16BIT,	0xD2DA, 0xB020},
	{MISENSOR_16BIT,	0xD2DC, 0x9220},
	{MISENSOR_16BIT,	0xD2DE, 0xB021},
	{MISENSOR_16BIT,	0xD2E0, 0x9221},
	{MISENSOR_16BIT,	0xD2E2, 0xB022},
	{MISENSOR_16BIT,	0xD2E4, 0x9222},
	{MISENSOR_16BIT,	0xD2E6, 0xB023},
	{MISENSOR_16BIT,	0xD2E8, 0x9223},
	{MISENSOR_16BIT,	0xD2EA, 0xB024},
	{MISENSOR_16BIT,	0xD2EC, 0x9227},
	{MISENSOR_16BIT,	0xD2EE, 0xB025},
	{MISENSOR_16BIT,	0xD2F0, 0x9228},
	{MISENSOR_16BIT,	0xD2F2, 0xB026},
	{MISENSOR_16BIT,	0xD2F4, 0x922B},
	{MISENSOR_16BIT,	0xD2F6, 0xB027},
	{MISENSOR_16BIT,	0xD2F8, 0x922C},
	{MISENSOR_16BIT,	0xD2FA, 0xB028},
	{MISENSOR_16BIT,	0xD2FC, 0x1258},
	{MISENSOR_16BIT,	0xD2FE, 0x0101},
	{MISENSOR_16BIT,	0xD300, 0xB029},
	{MISENSOR_16BIT,	0xD302, 0x125A},
	{MISENSOR_16BIT,	0xD304, 0x0101},
	{MISENSOR_16BIT,	0xD306, 0xB02A},
	{MISENSOR_16BIT,	0xD308, 0x1336},
	{MISENSOR_16BIT,	0xD30A, 0x8081},
	{MISENSOR_16BIT,	0xD30C, 0xA836},
	{MISENSOR_16BIT,	0xD30E, 0x1337},
	{MISENSOR_16BIT,	0xD310, 0x8081},
	{MISENSOR_16BIT,	0xD312, 0xA837},
	{MISENSOR_16BIT,	0xD314, 0x12A7},
	{MISENSOR_16BIT,	0xD316, 0x0701},
	{MISENSOR_16BIT,	0xD318, 0xB02C},
	{MISENSOR_16BIT,	0xD31A, 0x1354},
	{MISENSOR_16BIT,	0xD31C, 0x8081},
	{MISENSOR_16BIT,	0xD31E, 0x7FE0},
	{MISENSOR_16BIT,	0xD320, 0xA83A},
	{MISENSOR_16BIT,	0xD322, 0x78E0},
	{MISENSOR_16BIT,	0xD324, 0xC0F1},
	{MISENSOR_16BIT,	0xD326, 0x0DC2},
	{MISENSOR_16BIT,	0xD328, 0x05C0},
	{MISENSOR_16BIT,	0xD32A, 0x7608},
	{MISENSOR_16BIT,	0xD32C, 0x09BB},
	{MISENSOR_16BIT,	0xD32E, 0x0010},
	{MISENSOR_16BIT,	0xD330, 0x75CF},
	{MISENSOR_16BIT,	0xD332, 0xFFFF},
	{MISENSOR_16BIT,	0xD334, 0xD4E0},
	{MISENSOR_16BIT,	0xD336, 0x8D21},
	{MISENSOR_16BIT,	0xD338, 0x8D00},
	{MISENSOR_16BIT,	0xD33A, 0x2153},
	{MISENSOR_16BIT,	0xD33C, 0x0003},
	{MISENSOR_16BIT,	0xD33E, 0xB8C0},
	{MISENSOR_16BIT,	0xD340, 0x8D45},
	{MISENSOR_16BIT,	0xD342, 0x0B23},
	{MISENSOR_16BIT,	0xD344, 0x0000},
	{MISENSOR_16BIT,	0xD346, 0xEA8F},
	{MISENSOR_16BIT,	0xD348, 0x0915},
	{MISENSOR_16BIT,	0xD34A, 0x001E},
	{MISENSOR_16BIT,	0xD34C, 0xFF81},
	{MISENSOR_16BIT,	0xD34E, 0xE808},
	{MISENSOR_16BIT,	0xD350, 0x2540},
	{MISENSOR_16BIT,	0xD352, 0x1900},
	{MISENSOR_16BIT,	0xD354, 0xFFDE},
	{MISENSOR_16BIT,	0xD356, 0x8D00},
	{MISENSOR_16BIT,	0xD358, 0xB880},
	{MISENSOR_16BIT,	0xD35A, 0xF004},
	{MISENSOR_16BIT,	0xD35C, 0x8D00},
	{MISENSOR_16BIT,	0xD35E, 0xB8A0},
	{MISENSOR_16BIT,	0xD360, 0xAD00},
	{MISENSOR_16BIT,	0xD362, 0x8D05},
	{MISENSOR_16BIT,	0xD364, 0xE081},
	{MISENSOR_16BIT,	0xD366, 0x20CC},
	{MISENSOR_16BIT,	0xD368, 0x80A2},
	{MISENSOR_16BIT,	0xD36A, 0xDF00},
	{MISENSOR_16BIT,	0xD36C, 0xF40A},
	{MISENSOR_16BIT,	0xD36E, 0x71CF},
	{MISENSOR_16BIT,	0xD370, 0xFFFF},
	{MISENSOR_16BIT,	0xD372, 0xC84C},
	{MISENSOR_16BIT,	0xD374, 0x9102},
	{MISENSOR_16BIT,	0xD376, 0x7708},
	{MISENSOR_16BIT,	0xD378, 0xB8A6},
	{MISENSOR_16BIT,	0xD37A, 0x2786},
	{MISENSOR_16BIT,	0xD37C, 0x1FFE},
	{MISENSOR_16BIT,	0xD37E, 0xB102},
	{MISENSOR_16BIT,	0xD380, 0x0B42},
	{MISENSOR_16BIT,	0xD382, 0x0180},
	{MISENSOR_16BIT,	0xD384, 0x0E3E},
	{MISENSOR_16BIT,	0xD386, 0x0180},
	{MISENSOR_16BIT,	0xD388, 0x0F4A},
	{MISENSOR_16BIT,	0xD38A, 0x0160},
	{MISENSOR_16BIT,	0xD38C, 0x70C9},
	{MISENSOR_16BIT,	0xD38E, 0x8D05},
	{MISENSOR_16BIT,	0xD390, 0xE081},
	{MISENSOR_16BIT,	0xD392, 0x20CC},
	{MISENSOR_16BIT,	0xD394, 0x80A2},
	{MISENSOR_16BIT,	0xD396, 0xF429},
	{MISENSOR_16BIT,	0xD398, 0x76CF},
	{MISENSOR_16BIT,	0xD39A, 0xFFFF},
	{MISENSOR_16BIT,	0xD39C, 0xC84C},
	{MISENSOR_16BIT,	0xD39E, 0x082D},
	{MISENSOR_16BIT,	0xD3A0, 0x0051},
	{MISENSOR_16BIT,	0xD3A2, 0x70CF},
	{MISENSOR_16BIT,	0xD3A4, 0xFFFF},
	{MISENSOR_16BIT,	0xD3A6, 0xC90C},
	{MISENSOR_16BIT,	0xD3A8, 0x8805},
	{MISENSOR_16BIT,	0xD3AA, 0x09B6},
	{MISENSOR_16BIT,	0xD3AC, 0x0360},
	{MISENSOR_16BIT,	0xD3AE, 0xD908},
	{MISENSOR_16BIT,	0xD3B0, 0x2099},
	{MISENSOR_16BIT,	0xD3B2, 0x0802},
	{MISENSOR_16BIT,	0xD3B4, 0x9634},
	{MISENSOR_16BIT,	0xD3B6, 0xB503},
	{MISENSOR_16BIT,	0xD3B8, 0x7902},
	{MISENSOR_16BIT,	0xD3BA, 0x1523},
	{MISENSOR_16BIT,	0xD3BC, 0x1080},
	{MISENSOR_16BIT,	0xD3BE, 0xB634},
	{MISENSOR_16BIT,	0xD3C0, 0xE001},
	{MISENSOR_16BIT,	0xD3C2, 0x1D23},
	{MISENSOR_16BIT,	0xD3C4, 0x1002},
	{MISENSOR_16BIT,	0xD3C6, 0xF00B},
	{MISENSOR_16BIT,	0xD3C8, 0x9634},
	{MISENSOR_16BIT,	0xD3CA, 0x9503},
	{MISENSOR_16BIT,	0xD3CC, 0x6038},
	{MISENSOR_16BIT,	0xD3CE, 0xB614},
	{MISENSOR_16BIT,	0xD3D0, 0x153F},
	{MISENSOR_16BIT,	0xD3D2, 0x1080},
	{MISENSOR_16BIT,	0xD3D4, 0xE001},
	{MISENSOR_16BIT,	0xD3D6, 0x1D3F},
	{MISENSOR_16BIT,	0xD3D8, 0x1002},
	{MISENSOR_16BIT,	0xD3DA, 0xFFA4},
	{MISENSOR_16BIT,	0xD3DC, 0x9602},
	{MISENSOR_16BIT,	0xD3DE, 0x7F05},
	{MISENSOR_16BIT,	0xD3E0, 0xD800},
	{MISENSOR_16BIT,	0xD3E2, 0xB6E2},
	{MISENSOR_16BIT,	0xD3E4, 0xAD05},
	{MISENSOR_16BIT,	0xD3E6, 0x0511},
	{MISENSOR_16BIT,	0xD3E8, 0x05E0},
	{MISENSOR_16BIT,	0xD3EA, 0xD800},
	{MISENSOR_16BIT,	0xD3EC, 0xC0F1},
	{MISENSOR_16BIT,	0xD3EE, 0x0CFE},
	{MISENSOR_16BIT,	0xD3F0, 0x05C0},
	{MISENSOR_16BIT,	0xD3F2, 0x0A96},
	{MISENSOR_16BIT,	0xD3F4, 0x05A0},
	{MISENSOR_16BIT,	0xD3F6, 0x7608},
	{MISENSOR_16BIT,	0xD3F8, 0x0C22},
	{MISENSOR_16BIT,	0xD3FA, 0x0240},
	{MISENSOR_16BIT,	0xD3FC, 0xE080},
	{MISENSOR_16BIT,	0xD3FE, 0x20CA},
	{MISENSOR_16BIT,	0xD400, 0x0F82},
	{MISENSOR_16BIT,	0xD402, 0x0000},
	{MISENSOR_16BIT,	0xD404, 0x190B},
	{MISENSOR_16BIT,	0xD406, 0x0C60},
	{MISENSOR_16BIT,	0xD408, 0x05A2},
	{MISENSOR_16BIT,	0xD40A, 0x21CA},
	{MISENSOR_16BIT,	0xD40C, 0x0022},
	{MISENSOR_16BIT,	0xD40E, 0x0C56},
	{MISENSOR_16BIT,	0xD410, 0x0240},
	{MISENSOR_16BIT,	0xD412, 0xE806},
	{MISENSOR_16BIT,	0xD414, 0x0E0E},
	{MISENSOR_16BIT,	0xD416, 0x0220},
	{MISENSOR_16BIT,	0xD418, 0x70C9},
	{MISENSOR_16BIT,	0xD41A, 0xF048},
	{MISENSOR_16BIT,	0xD41C, 0x0896},
	{MISENSOR_16BIT,	0xD41E, 0x0440},
	{MISENSOR_16BIT,	0xD420, 0x0E96},
	{MISENSOR_16BIT,	0xD422, 0x0400},
	{MISENSOR_16BIT,	0xD424, 0x0966},
	{MISENSOR_16BIT,	0xD426, 0x0380},
	{MISENSOR_16BIT,	0xD428, 0x75CF},
	{MISENSOR_16BIT,	0xD42A, 0xFFFF},
	{MISENSOR_16BIT,	0xD42C, 0xD4E0},
	{MISENSOR_16BIT,	0xD42E, 0x8D00},
	{MISENSOR_16BIT,	0xD430, 0x084D},
	{MISENSOR_16BIT,	0xD432, 0x001E},
	{MISENSOR_16BIT,	0xD434, 0xFF47},
	{MISENSOR_16BIT,	0xD436, 0x080D},
	{MISENSOR_16BIT,	0xD438, 0x0050},
	{MISENSOR_16BIT,	0xD43A, 0xFF57},
	{MISENSOR_16BIT,	0xD43C, 0x0841},
	{MISENSOR_16BIT,	0xD43E, 0x0051},
	{MISENSOR_16BIT,	0xD440, 0x8D04},
	{MISENSOR_16BIT,	0xD442, 0x9521},
	{MISENSOR_16BIT,	0xD444, 0xE064},
	{MISENSOR_16BIT,	0xD446, 0x790C},
	{MISENSOR_16BIT,	0xD448, 0x702F},
	{MISENSOR_16BIT,	0xD44A, 0x0CE2},
	{MISENSOR_16BIT,	0xD44C, 0x05E0},
	{MISENSOR_16BIT,	0xD44E, 0xD964},
	{MISENSOR_16BIT,	0xD450, 0x72CF},
	{MISENSOR_16BIT,	0xD452, 0xFFFF},
	{MISENSOR_16BIT,	0xD454, 0xC700},
	{MISENSOR_16BIT,	0xD456, 0x9235},
	{MISENSOR_16BIT,	0xD458, 0x0811},
	{MISENSOR_16BIT,	0xD45A, 0x0043},
	{MISENSOR_16BIT,	0xD45C, 0xFF3D},
	{MISENSOR_16BIT,	0xD45E, 0x080D},
	{MISENSOR_16BIT,	0xD460, 0x0051},
	{MISENSOR_16BIT,	0xD462, 0xD801},
	{MISENSOR_16BIT,	0xD464, 0xFF77},
	{MISENSOR_16BIT,	0xD466, 0xF025},
	{MISENSOR_16BIT,	0xD468, 0x9501},
	{MISENSOR_16BIT,	0xD46A, 0x9235},
	{MISENSOR_16BIT,	0xD46C, 0x0911},
	{MISENSOR_16BIT,	0xD46E, 0x0003},
	{MISENSOR_16BIT,	0xD470, 0xFF49},
	{MISENSOR_16BIT,	0xD472, 0x080D},
	{MISENSOR_16BIT,	0xD474, 0x0051},
	{MISENSOR_16BIT,	0xD476, 0xD800},
	{MISENSOR_16BIT,	0xD478, 0xFF72},
	{MISENSOR_16BIT,	0xD47A, 0xF01B},
	{MISENSOR_16BIT,	0xD47C, 0x0886},
	{MISENSOR_16BIT,	0xD47E, 0x03E0},
	{MISENSOR_16BIT,	0xD480, 0xD801},
	{MISENSOR_16BIT,	0xD482, 0x0EF6},
	{MISENSOR_16BIT,	0xD484, 0x03C0},
	{MISENSOR_16BIT,	0xD486, 0x0F52},
	{MISENSOR_16BIT,	0xD488, 0x0340},
	{MISENSOR_16BIT,	0xD48A, 0x0DBA},
	{MISENSOR_16BIT,	0xD48C, 0x0200},
	{MISENSOR_16BIT,	0xD48E, 0x0AF6},
	{MISENSOR_16BIT,	0xD490, 0x0440},
	{MISENSOR_16BIT,	0xD492, 0x0C22},
	{MISENSOR_16BIT,	0xD494, 0x0400},
	{MISENSOR_16BIT,	0xD496, 0x0D72},
	{MISENSOR_16BIT,	0xD498, 0x0440},
	{MISENSOR_16BIT,	0xD49A, 0x0DC2},
	{MISENSOR_16BIT,	0xD49C, 0x0200},
	{MISENSOR_16BIT,	0xD49E, 0x0972},
	{MISENSOR_16BIT,	0xD4A0, 0x0440},
	{MISENSOR_16BIT,	0xD4A2, 0x0D3A},
	{MISENSOR_16BIT,	0xD4A4, 0x0220},
	{MISENSOR_16BIT,	0xD4A6, 0xD820},
	{MISENSOR_16BIT,	0xD4A8, 0x0BFA},
	{MISENSOR_16BIT,	0xD4AA, 0x0260},
	{MISENSOR_16BIT,	0xD4AC, 0x70C9},
	{MISENSOR_16BIT,	0xD4AE, 0x0451},
	{MISENSOR_16BIT,	0xD4B0, 0x05C0},
	{MISENSOR_16BIT,	0xD4B2, 0x78E0},
	{MISENSOR_16BIT,	0xD4B4, 0xD900},
	{MISENSOR_16BIT,	0xD4B6, 0xF00A},
	{MISENSOR_16BIT,	0xD4B8, 0x70CF},
	{MISENSOR_16BIT,	0xD4BA, 0xFFFF},
	{MISENSOR_16BIT,	0xD4BC, 0xD520},
	{MISENSOR_16BIT,	0xD4BE, 0x7835},
	{MISENSOR_16BIT,	0xD4C0, 0x8041},
	{MISENSOR_16BIT,	0xD4C2, 0x8000},
	{MISENSOR_16BIT,	0xD4C4, 0xE102},
	{MISENSOR_16BIT,	0xD4C6, 0xA040},
	{MISENSOR_16BIT,	0xD4C8, 0x09F1},
	{MISENSOR_16BIT,	0xD4CA, 0x8114},
	{MISENSOR_16BIT,	0xD4CC, 0x71CF},
	{MISENSOR_16BIT,	0xD4CE, 0xFFFF},
	{MISENSOR_16BIT,	0xD4D0, 0xD4E0},
	{MISENSOR_16BIT,	0xD4D2, 0x70CF},
	{MISENSOR_16BIT,	0xD4D4, 0xFFFF},
	{MISENSOR_16BIT,	0xD4D6, 0xC594},
	{MISENSOR_16BIT,	0xD4D8, 0xB03A},
	{MISENSOR_16BIT,	0xD4DA, 0x7FE0},
	{MISENSOR_16BIT,	0xD4DC, 0xD800},
	{MISENSOR_16BIT,	0xD4DE, 0x0000},
	{MISENSOR_16BIT,	0xD4E0, 0x0000},
	{MISENSOR_16BIT,	0xD4E2, 0x0500},
	{MISENSOR_16BIT,	0xD4E4, 0x0500},
	{MISENSOR_16BIT,	0xD4E6, 0x0200},
	{MISENSOR_16BIT,	0xD4E8, 0x0330},
	{MISENSOR_16BIT,	0xD4EA, 0x0000},
	{MISENSOR_16BIT,	0xD4EC, 0x0000},
	{MISENSOR_16BIT,	0xD4EE, 0x03CD},
	{MISENSOR_16BIT,	0xD4F0, 0x050D},
	{MISENSOR_16BIT,	0xD4F2, 0x01C5},
	{MISENSOR_16BIT,	0xD4F4, 0x03B3},
	{MISENSOR_16BIT,	0xD4F6, 0x00E0},
	{MISENSOR_16BIT,	0xD4F8, 0x01E3},
	{MISENSOR_16BIT,	0xD4FA, 0x0280},
	{MISENSOR_16BIT,	0xD4FC, 0x01E0},
	{MISENSOR_16BIT,	0xD4FE, 0x0109},
	{MISENSOR_16BIT,	0xD500, 0x0080},
	{MISENSOR_16BIT,	0xD502, 0x0500},
	{MISENSOR_16BIT,	0xD504, 0x0000},
	{MISENSOR_16BIT,	0xD506, 0x0000},
	{MISENSOR_16BIT,	0xD508, 0x0000},
	{MISENSOR_16BIT,	0xD50A, 0x0000},
	{MISENSOR_16BIT,	0xD50C, 0x0000},
	{MISENSOR_16BIT,	0xD50E, 0x0000},
	{MISENSOR_16BIT,	0xD510, 0x0000},
	{MISENSOR_16BIT,	0xD512, 0x0000},
	{MISENSOR_16BIT,	0xD514, 0x0000},
	{MISENSOR_16BIT,	0xD516, 0x0000},
	{MISENSOR_16BIT,	0xD518, 0x0000},
	{MISENSOR_16BIT,	0xD51A, 0x0000},
	{MISENSOR_16BIT,	0xD51C, 0x0000},
	{MISENSOR_16BIT,	0xD51E, 0x0000},
	{MISENSOR_16BIT,	0xD520, 0xFFFF},
	{MISENSOR_16BIT,	0xD522, 0xC9B4},
	{MISENSOR_16BIT,	0xD524, 0xFFFF},
	{MISENSOR_16BIT,	0xD526, 0xD324},
	{MISENSOR_16BIT,	0xD528, 0xFFFF},
	{MISENSOR_16BIT,	0xD52A, 0xCA34},
	{MISENSOR_16BIT,	0xD52C, 0xFFFF},
	{MISENSOR_16BIT,	0xD52E, 0xD3EC},
	{MISENSOR_16BIT,	0x098E, 0x0000},
	{MISENSOR_16BIT,	0xE000, 0x04B4},
	{MISENSOR_16BIT,	0xE002, 0x0302},
	{MISENSOR_16BIT,	0xE004, 0x4103},
	{MISENSOR_16BIT,	0xE006, 0x0202},
	{MISENSOR_16BIT,	0x0080, 0xFFF0},
	{MISENSOR_16BIT,	0x0080, 0xFFF1},

	/* PGA parameter and APGA
	 * [Step4-APGA] [TP101_MT9M114_APGA]
	 */
	{MISENSOR_16BIT,	0x098E, 0x495E},
	{MISENSOR_16BIT,	0xC95E, 0x0000},
	{MISENSOR_16BIT,	0x3640, 0x02B0},
	{MISENSOR_16BIT,	0x3642, 0x8063},
	{MISENSOR_16BIT,	0x3644, 0x78D0},
	{MISENSOR_16BIT,	0x3646, 0x50CC},
	{MISENSOR_16BIT,	0x3648, 0x3511},
	{MISENSOR_16BIT,	0x364A, 0x0110},
	{MISENSOR_16BIT,	0x364C, 0xBD8A},
	{MISENSOR_16BIT,	0x364E, 0x0CD1},
	{MISENSOR_16BIT,	0x3650, 0x24ED},
	{MISENSOR_16BIT,	0x3652, 0x7C11},
	{MISENSOR_16BIT,	0x3654, 0x0150},
	{MISENSOR_16BIT,	0x3656, 0x124C},
	{MISENSOR_16BIT,	0x3658, 0x3130},
	{MISENSOR_16BIT,	0x365A, 0x508C},
	{MISENSOR_16BIT,	0x365C, 0x21F1},
	{MISENSOR_16BIT,	0x365E, 0x0090},
	{MISENSOR_16BIT,	0x3660, 0xBFCA},
	{MISENSOR_16BIT,	0x3662, 0x0A11},
	{MISENSOR_16BIT,	0x3664, 0x4F4B},
	{MISENSOR_16BIT,	0x3666, 0x28B1},
	{MISENSOR_16BIT,	0x3680, 0x50A9},
	{MISENSOR_16BIT,	0x3682, 0xA04B},
	{MISENSOR_16BIT,	0x3684, 0x0E2D},
	{MISENSOR_16BIT,	0x3686, 0x73EC},
	{MISENSOR_16BIT,	0x3688, 0x164F},
	{MISENSOR_16BIT,	0x368A, 0xF829},
	{MISENSOR_16BIT,	0x368C, 0xC1A8},
	{MISENSOR_16BIT,	0x368E, 0xB0EC},
	{MISENSOR_16BIT,	0x3690, 0xE76A},
	{MISENSOR_16BIT,	0x3692, 0x69AF},
	{MISENSOR_16BIT,	0x3694, 0x378C},
	{MISENSOR_16BIT,	0x3696, 0xA70D},
	{MISENSOR_16BIT,	0x3698, 0x884F},
	{MISENSOR_16BIT,	0x369A, 0xEE8B},
	{MISENSOR_16BIT,	0x369C, 0x5DEF},
	{MISENSOR_16BIT,	0x369E, 0x27CC},
	{MISENSOR_16BIT,	0x36A0, 0xCAAC},
	{MISENSOR_16BIT,	0x36A2, 0x840E},
	{MISENSOR_16BIT,	0x36A4, 0xDAA9},
	{MISENSOR_16BIT,	0x36A6, 0xF00C},
	{MISENSOR_16BIT,	0x36C0, 0x1371},
	{MISENSOR_16BIT,	0x36C2, 0x272F},
	{MISENSOR_16BIT,	0x36C4, 0x2293},
	{MISENSOR_16BIT,	0x36C6, 0xE6D0},
	{MISENSOR_16BIT,	0x36C8, 0xEC32},
	{MISENSOR_16BIT,	0x36CA, 0x11B1},
	{MISENSOR_16BIT,	0x36CC, 0x7BAF},
	{MISENSOR_16BIT,	0x36CE, 0x5813},
	{MISENSOR_16BIT,	0x36D0, 0xB871},
	{MISENSOR_16BIT,	0x36D2, 0x8913},
	{MISENSOR_16BIT,	0x36D4, 0x4610},
	{MISENSOR_16BIT,	0x36D6, 0x7EEE},
	{MISENSOR_16BIT,	0x36D8, 0x0DF3},
	{MISENSOR_16BIT,	0x36DA, 0xB84F},
	{MISENSOR_16BIT,	0x36DC, 0xB532},
	{MISENSOR_16BIT,	0x36DE, 0x1171},
	{MISENSOR_16BIT,	0x36E0, 0x13CF},
	{MISENSOR_16BIT,	0x36E2, 0x22F3},
	{MISENSOR_16BIT,	0x36E4, 0xE090},
	{MISENSOR_16BIT,	0x36E6, 0x8133},
	{MISENSOR_16BIT,	0x3700, 0x88AE},
	{MISENSOR_16BIT,	0x3702, 0x00EA},
	{MISENSOR_16BIT,	0x3704, 0x344F},
	{MISENSOR_16BIT,	0x3706, 0xEC88},
	{MISENSOR_16BIT,	0x3708, 0x3E91},
	{MISENSOR_16BIT,	0x370A, 0xF12D},
	{MISENSOR_16BIT,	0x370C, 0xB0EF},
	{MISENSOR_16BIT,	0x370E, 0x77CD},
	{MISENSOR_16BIT,	0x3710, 0x7930},
	{MISENSOR_16BIT,	0x3712, 0x5C12},
	{MISENSOR_16BIT,	0x3714, 0x500C},
	{MISENSOR_16BIT,	0x3716, 0x22CE},
	{MISENSOR_16BIT,	0x3718, 0x2370},
	{MISENSOR_16BIT,	0x371A, 0x258F},
	{MISENSOR_16BIT,	0x371C, 0x3D30},
	{MISENSOR_16BIT,	0x371E, 0x370C},
	{MISENSOR_16BIT,	0x3720, 0x03ED},
	{MISENSOR_16BIT,	0x3722, 0x9AD0},
	{MISENSOR_16BIT,	0x3724, 0x7ECF},
	{MISENSOR_16BIT,	0x3726, 0x1093},
	{MISENSOR_16BIT,	0x3740, 0x2391},
	{MISENSOR_16BIT,	0x3742, 0xAAD0},
	{MISENSOR_16BIT,	0x3744, 0x28F2},
	{MISENSOR_16BIT,	0x3746, 0xBA4F},
	{MISENSOR_16BIT,	0x3748, 0xC536},
	{MISENSOR_16BIT,	0x374A, 0x1472},
	{MISENSOR_16BIT,	0x374C, 0xD110},
	{MISENSOR_16BIT,	0x374E, 0x2933},
	{MISENSOR_16BIT,	0x3750, 0xD0D1},
	{MISENSOR_16BIT,	0x3752, 0x9F37},
	{MISENSOR_16BIT,	0x3754, 0x34D1},
	{MISENSOR_16BIT,	0x3756, 0x1C6C},
	{MISENSOR_16BIT,	0x3758, 0x3FD2},
	{MISENSOR_16BIT,	0x375A, 0xCB72},
	{MISENSOR_16BIT,	0x375C, 0xBA96},
	{MISENSOR_16BIT,	0x375E, 0x1551},
	{MISENSOR_16BIT,	0x3760, 0xB74F},
	{MISENSOR_16BIT,	0x3762, 0x1672},
	{MISENSOR_16BIT,	0x3764, 0x84F1},
	{MISENSOR_16BIT,	0x3766, 0xC2D6},
	{MISENSOR_16BIT,	0x3782, 0x01E0},
	{MISENSOR_16BIT,	0x3784, 0x0280},
	{MISENSOR_16BIT,	0x37C0, 0xA6EA},
	{MISENSOR_16BIT,	0x37C2, 0x874B},
	{MISENSOR_16BIT,	0x37C4, 0x85CB},
	{MISENSOR_16BIT,	0x37C6, 0x968A},
	{MISENSOR_16BIT,	0x098E, 0x0000},
	{MISENSOR_16BIT,	0xC960, 0x0AF0},
	{MISENSOR_16BIT,	0xC962, 0x79E2},
	{MISENSOR_16BIT,	0xC964, 0x5EC8},
	{MISENSOR_16BIT,	0xC966, 0x791F},
	{MISENSOR_16BIT,	0xC968, 0x76EE},
	{MISENSOR_16BIT,	0xC96A, 0x0FA0},
	{MISENSOR_16BIT,	0xC96C, 0x7DFA},
	{MISENSOR_16BIT,	0xC96E, 0x7DAF},
	{MISENSOR_16BIT,	0xC970, 0x7E02},
	{MISENSOR_16BIT,	0xC972, 0x7E0A},
	{MISENSOR_16BIT,	0xC974, 0x1964},
	{MISENSOR_16BIT,	0xC976, 0x7CDC},
	{MISENSOR_16BIT,	0xC978, 0x7838},
	{MISENSOR_16BIT,	0xC97A, 0x7C2F},
	{MISENSOR_16BIT,	0xC97C, 0x7792},
	{MISENSOR_16BIT,	0xC95E, 0x0003},

	/* [Step4-APGA] */
	{MISENSOR_16BIT,	0x098E, 0x0000},
	{MISENSOR_16BIT,	0xC95E, 0x0003},

	/* [Step5-AWB_CCM]1: LOAD=CCM */
	{MISENSOR_16BIT,	0xC892, 0x0267},
	{MISENSOR_16BIT,	0xC894, 0xFF1A},
	{MISENSOR_16BIT,	0xC896, 0xFFB3},
	{MISENSOR_16BIT,	0xC898, 0xFF80},
	{MISENSOR_16BIT,	0xC89A, 0x0166},
	{MISENSOR_16BIT,	0xC89C, 0x0003},
	{MISENSOR_16BIT,	0xC89E, 0xFF9A},
	{MISENSOR_16BIT,	0xC8A0, 0xFEB4},
	{MISENSOR_16BIT,	0xC8A2, 0x024D},
	{MISENSOR_16BIT,	0xC8A4, 0x01BF},
	{MISENSOR_16BIT,	0xC8A6, 0xFF01},
	{MISENSOR_16BIT,	0xC8A8, 0xFFF3},
	{MISENSOR_16BIT,	0xC8AA, 0xFF75},
	{MISENSOR_16BIT,	0xC8AC, 0x0198},
	{MISENSOR_16BIT,	0xC8AE, 0xFFFD},
	{MISENSOR_16BIT,	0xC8B0, 0xFF9A},
	{MISENSOR_16BIT,	0xC8B2, 0xFEE7},
	{MISENSOR_16BIT,	0xC8B4, 0x02A8},
	{MISENSOR_16BIT,	0xC8B6, 0x01D9},
	{MISENSOR_16BIT,	0xC8B8, 0xFF26},
	{MISENSOR_16BIT,	0xC8BA, 0xFFF3},
	{MISENSOR_16BIT,	0xC8BC, 0xFFB3},
	{MISENSOR_16BIT,	0xC8BE, 0x0132},
	{MISENSOR_16BIT,	0xC8C0, 0xFFE8},
	{MISENSOR_16BIT,	0xC8C2, 0xFFDA},
	{MISENSOR_16BIT,	0xC8C4, 0xFECD},
	{MISENSOR_16BIT,	0xC8C6, 0x02C2},
	{MISENSOR_16BIT,	0xC8C8, 0x0075},
	{MISENSOR_16BIT,	0xC8CA, 0x011C},
	{MISENSOR_16BIT,	0xC8CC, 0x009A},
	{MISENSOR_16BIT,	0xC8CE, 0x0105},
	{MISENSOR_16BIT,	0xC8D0, 0x00A4},
	{MISENSOR_16BIT,	0xC8D2, 0x00AC},
	{MISENSOR_16BIT,	0xC8D4, 0x0A8C},
	{MISENSOR_16BIT,	0xC8D6, 0x0F0A},
	{MISENSOR_16BIT,	0xC8D8, 0x1964},

	/* LOAD=AWB */
	{MISENSOR_16BIT,	0xC914, 0x0000},
	{MISENSOR_16BIT,	0xC916, 0x0000},
	{MISENSOR_16BIT,	0xC918, 0x04FF},
	{MISENSOR_16BIT,	0xC91A, 0x02CF},
	{MISENSOR_16BIT,	0xC904, 0x0033},
	{MISENSOR_16BIT,	0xC906, 0x0040},
	{MISENSOR_8BIT,   0xC8F2, 0x03},
	{MISENSOR_8BIT,   0xC8F3, 0x02},
	{MISENSOR_16BIT,	0xC906, 0x003C},
	{MISENSOR_16BIT,	0xC8F4, 0x0000},
	{MISENSOR_16BIT,	0xC8F6, 0x0000},
	{MISENSOR_16BIT,	0xC8F8, 0x0000},
	{MISENSOR_16BIT,	0xC8FA, 0xE724},
	{MISENSOR_16BIT,	0xC8FC, 0x1583},
	{MISENSOR_16BIT,	0xC8FE, 0x2045},
	{MISENSOR_16BIT,	0xC900, 0x05DC},
	{MISENSOR_16BIT,	0xC902, 0x007C},
	{MISENSOR_8BIT,   0xC90C, 0x80},
	{MISENSOR_8BIT,   0xC90D, 0x80},
	{MISENSOR_8BIT,   0xC90E, 0x80},
	{MISENSOR_8BIT,   0xC90F, 0x88},
	{MISENSOR_8BIT,   0xC910, 0x80},
	{MISENSOR_8BIT,   0xC911, 0x80},

	/* LOAD=Step7-CPIPE_Preference */
	{MISENSOR_16BIT,	0xC926, 0x0020},
	{MISENSOR_16BIT,	0xC928, 0x009A},
	{MISENSOR_16BIT,	0xC946, 0x0070},
	{MISENSOR_16BIT,	0xC948, 0x00F3},
	{MISENSOR_16BIT,	0xC952, 0x0020},
	{MISENSOR_16BIT,	0xC954, 0x009A},
	{MISENSOR_8BIT,   0xC92A, 0x80},
	{MISENSOR_8BIT,   0xC92B, 0x4B},
	{MISENSOR_8BIT,   0xC92C, 0x00},
	{MISENSOR_8BIT,   0xC92D, 0xFF},
	{MISENSOR_8BIT,   0xC92E, 0x3C},
	{MISENSOR_8BIT,   0xC92F, 0x02},
	{MISENSOR_8BIT,   0xC930, 0x06},
	{MISENSOR_8BIT,   0xC931, 0x64},
	{MISENSOR_8BIT,   0xC932, 0x01},
	{MISENSOR_8BIT,   0xC933, 0x0C},
	{MISENSOR_8BIT,   0xC934, 0x3C},
	{MISENSOR_8BIT,   0xC935, 0x3C},
	{MISENSOR_8BIT,   0xC936, 0x3C},
	{MISENSOR_8BIT,   0xC937, 0x0F},
	{MISENSOR_8BIT,   0xC938, 0x64},
	{MISENSOR_8BIT,   0xC939, 0x64},
	{MISENSOR_8BIT,   0xC93A, 0x64},
	{MISENSOR_8BIT,   0xC93B, 0x32},
	{MISENSOR_16BIT,	0xC93C, 0x0020},
	{MISENSOR_16BIT,	0xC93E, 0x009A},
	{MISENSOR_16BIT,	0xC940, 0x00DC},
	{MISENSOR_8BIT,   0xC942, 0x38},
	{MISENSOR_8BIT,   0xC943, 0x30},
	{MISENSOR_8BIT,   0xC944, 0x50},
	{MISENSOR_8BIT,   0xC945, 0x19},
	{MISENSOR_16BIT,	0xC94A, 0x0230},
	{MISENSOR_16BIT,	0xC94C, 0x0010},
	{MISENSOR_16BIT,	0xC94E, 0x01CD},
	{MISENSOR_8BIT,   0xC950, 0x05},
	{MISENSOR_8BIT,   0xC951, 0x40},
	{MISENSOR_8BIT,   0xC87B, 0x1B},
	{MISENSOR_8BIT,   0xC878, 0x0E},
	{MISENSOR_16BIT,	0xC890, 0x0080},
	{MISENSOR_16BIT,	0xC886, 0x0100},
	{MISENSOR_16BIT,	0xC87C, 0x005A},
	{MISENSOR_8BIT,   0xB42A, 0x05},
	{MISENSOR_8BIT,   0xA80A, 0x20},

	/* Speed up AE/AWB */
	{MISENSOR_16BIT,	0x098E, 0x2802},
	{MISENSOR_16BIT,	0xA802, 0x0008},
	{MISENSOR_8BIT,   0xC908, 0x01},
	{MISENSOR_8BIT,   0xC879, 0x01},
	{MISENSOR_8BIT,   0xC909, 0x02},
	{MISENSOR_8BIT,   0xA80A, 0x18},
	{MISENSOR_8BIT,   0xA80B, 0x18},
	{MISENSOR_8BIT,   0xAC16, 0x18},
	{MISENSOR_8BIT,   0xC878, 0x0E},

	{MISENSOR_TOK_TERM, 0, 0}
};

#endif
