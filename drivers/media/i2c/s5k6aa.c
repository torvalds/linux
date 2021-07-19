// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Samsung S5K6AAFX SXGA 1/6" 1.3M CMOS Image Sensor
 * with embedded SoC ISP.
 *
 * Copyright (C) 2011, Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Based on a driver authored by Dongsoo Nathaniel Kim.
 * Copyright (C) 2009, Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>
#include <media/i2c/s5k6aa.h>

static int debug;
module_param(debug, int, 0644);

#define DRIVER_NAME			"S5K6AA"

/* The token to indicate array termination */
#define S5K6AA_TERM			0xffff
#define S5K6AA_OUT_WIDTH_DEF		640
#define S5K6AA_OUT_HEIGHT_DEF		480
#define S5K6AA_WIN_WIDTH_MAX		1280
#define S5K6AA_WIN_HEIGHT_MAX		1024
#define S5K6AA_WIN_WIDTH_MIN		8
#define S5K6AA_WIN_HEIGHT_MIN		8

/*
 * H/W register Interface (0xD0000000 - 0xD0000FFF)
 */
#define AHB_MSB_ADDR_PTR		0xfcfc
#define GEN_REG_OFFSH			0xd000
#define REG_CMDWR_ADDRH			0x0028
#define REG_CMDWR_ADDRL			0x002a
#define REG_CMDRD_ADDRH			0x002c
#define REG_CMDRD_ADDRL			0x002e
#define REG_CMDBUF0_ADDR		0x0f12
#define REG_CMDBUF1_ADDR		0x0f10

/*
 * Host S/W Register interface (0x70000000 - 0x70002000)
 * The value of the two most significant address bytes is 0x7000,
 * (HOST_SWIF_OFFS_H). The register addresses below specify 2 LSBs.
 */
#define HOST_SWIF_OFFSH			0x7000

/* Initialization parameters */
/* Master clock frequency in KHz */
#define REG_I_INCLK_FREQ_L		0x01b8
#define REG_I_INCLK_FREQ_H		0x01ba
#define  MIN_MCLK_FREQ_KHZ		6000U
#define  MAX_MCLK_FREQ_KHZ		27000U
#define REG_I_USE_NPVI_CLOCKS		0x01c6
#define REG_I_USE_NMIPI_CLOCKS		0x01c8

/* Clock configurations, n = 0..2. REG_I_* frequency unit is 4 kHz. */
#define REG_I_OPCLK_4KHZ(n)		((n) * 6 + 0x01cc)
#define REG_I_MIN_OUTRATE_4KHZ(n)	((n) * 6 + 0x01ce)
#define REG_I_MAX_OUTRATE_4KHZ(n)	((n) * 6 + 0x01d0)
#define  SYS_PLL_OUT_FREQ		(48000000 / 4000)
#define  PCLK_FREQ_MIN			(24000000 / 4000)
#define  PCLK_FREQ_MAX			(48000000 / 4000)
#define REG_I_INIT_PARAMS_UPDATED	0x01e0
#define REG_I_ERROR_INFO		0x01e2

/* General purpose parameters */
#define REG_USER_BRIGHTNESS		0x01e4
#define REG_USER_CONTRAST		0x01e6
#define REG_USER_SATURATION		0x01e8
#define REG_USER_SHARPBLUR		0x01ea

#define REG_G_SPEC_EFFECTS		0x01ee
#define REG_G_ENABLE_PREV		0x01f0
#define REG_G_ENABLE_PREV_CHG		0x01f2
#define REG_G_NEW_CFG_SYNC		0x01f8
#define REG_G_PREVZOOM_IN_WIDTH		0x020a
#define REG_G_PREVZOOM_IN_HEIGHT	0x020c
#define REG_G_PREVZOOM_IN_XOFFS		0x020e
#define REG_G_PREVZOOM_IN_YOFFS		0x0210
#define REG_G_INPUTS_CHANGE_REQ		0x021a
#define REG_G_ACTIVE_PREV_CFG		0x021c
#define REG_G_PREV_CFG_CHG		0x021e
#define REG_G_PREV_OPEN_AFTER_CH	0x0220
#define REG_G_PREV_CFG_ERROR		0x0222

/* Preview control section. n = 0...4. */
#define PREG(n, x)			((n) * 0x26 + x)
#define REG_P_OUT_WIDTH(n)		PREG(n, 0x0242)
#define REG_P_OUT_HEIGHT(n)		PREG(n, 0x0244)
#define REG_P_FMT(n)			PREG(n, 0x0246)
#define REG_P_MAX_OUT_RATE(n)		PREG(n, 0x0248)
#define REG_P_MIN_OUT_RATE(n)		PREG(n, 0x024a)
#define REG_P_PVI_MASK(n)		PREG(n, 0x024c)
#define REG_P_CLK_INDEX(n)		PREG(n, 0x024e)
#define REG_P_FR_RATE_TYPE(n)		PREG(n, 0x0250)
#define  FR_RATE_DYNAMIC		0
#define  FR_RATE_FIXED			1
#define  FR_RATE_FIXED_ACCURATE		2
#define REG_P_FR_RATE_Q_TYPE(n)		PREG(n, 0x0252)
#define  FR_RATE_Q_BEST_FRRATE		1 /* Binning enabled */
#define  FR_RATE_Q_BEST_QUALITY		2 /* Binning disabled */
/* Frame period in 0.1 ms units */
#define REG_P_MAX_FR_TIME(n)		PREG(n, 0x0254)
#define REG_P_MIN_FR_TIME(n)		PREG(n, 0x0256)
/* Conversion to REG_P_[MAX/MIN]_FR_TIME value; __t: time in us */
#define  US_TO_FR_TIME(__t)		((__t) / 100)
#define  S5K6AA_MIN_FR_TIME		33300  /* us */
#define  S5K6AA_MAX_FR_TIME		650000 /* us */
#define  S5K6AA_MAX_HIGHRES_FR_TIME	666    /* x100 us */
/* The below 5 registers are for "device correction" values */
#define REG_P_COLORTEMP(n)		PREG(n, 0x025e)
#define REG_P_PREV_MIRROR(n)		PREG(n, 0x0262)

/* Extended image property controls */
/* Exposure time in 10 us units */
#define REG_SF_USR_EXPOSURE_L		0x03c6
#define REG_SF_USR_EXPOSURE_H		0x03c8
#define REG_SF_USR_EXPOSURE_CHG		0x03ca
#define REG_SF_USR_TOT_GAIN		0x03cc
#define REG_SF_USR_TOT_GAIN_CHG		0x03ce
#define REG_SF_RGAIN			0x03d0
#define REG_SF_RGAIN_CHG		0x03d2
#define REG_SF_GGAIN			0x03d4
#define REG_SF_GGAIN_CHG		0x03d6
#define REG_SF_BGAIN			0x03d8
#define REG_SF_BGAIN_CHG		0x03da
#define REG_SF_FLICKER_QUANT		0x03dc
#define REG_SF_FLICKER_QUANT_CHG	0x03de

/* Output interface (parallel/MIPI) setup */
#define REG_OIF_EN_MIPI_LANES		0x03fa
#define REG_OIF_EN_PACKETS		0x03fc
#define REG_OIF_CFG_CHG			0x03fe

/* Auto-algorithms enable mask */
#define REG_DBG_AUTOALG_EN		0x0400
#define  AALG_ALL_EN_MASK		(1 << 0)
#define  AALG_AE_EN_MASK		(1 << 1)
#define  AALG_DIVLEI_EN_MASK		(1 << 2)
#define  AALG_WB_EN_MASK		(1 << 3)
#define  AALG_FLICKER_EN_MASK		(1 << 5)
#define  AALG_FIT_EN_MASK		(1 << 6)
#define  AALG_WRHW_EN_MASK		(1 << 7)

/* Firmware revision information */
#define REG_FW_APIVER			0x012e
#define  S5K6AAFX_FW_APIVER		0x0001
#define REG_FW_REVISION			0x0130

/* For now we use only one user configuration register set */
#define S5K6AA_MAX_PRESETS		1

static const char * const s5k6aa_supply_names[] = {
	"vdd_core",	/* Digital core supply 1.5V (1.4V to 1.6V) */
	"vdda",		/* Analog power supply 2.8V (2.6V to 3.0V) */
	"vdd_reg",	/* Regulator input power 1.8V (1.7V to 1.9V)
			   or 2.8V (2.6V to 3.0) */
	"vddio",	/* I/O supply 1.8V (1.65V to 1.95V)
			   or 2.8V (2.5V to 3.1V) */
};
#define S5K6AA_NUM_SUPPLIES ARRAY_SIZE(s5k6aa_supply_names)

enum s5k6aa_gpio_id {
	STBY,
	RST,
	GPIO_NUM,
};

struct s5k6aa_regval {
	u16 addr;
	u16 val;
};

struct s5k6aa_pixfmt {
	u32 code;
	u32 colorspace;
	/* REG_P_FMT(x) register value */
	u16 reg_p_fmt;
};

struct s5k6aa_preset {
	/* output pixel format and resolution */
	struct v4l2_mbus_framefmt mbus_fmt;
	u8 clk_id;
	u8 index;
};

struct s5k6aa_ctrls {
	struct v4l2_ctrl_handler handler;
	/* Auto / manual white balance cluster */
	struct v4l2_ctrl *awb;
	struct v4l2_ctrl *gain_red;
	struct v4l2_ctrl *gain_blue;
	struct v4l2_ctrl *gain_green;
	/* Mirror cluster */
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	/* Auto exposure / manual exposure and gain cluster */
	struct v4l2_ctrl *auto_exp;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
};

struct s5k6aa_interval {
	u16 reg_fr_time;
	struct v4l2_fract interval;
	/* Maximum rectangle for the interval */
	struct v4l2_frmsize_discrete size;
};

struct s5k6aa {
	struct v4l2_subdev sd;
	struct media_pad pad;

	enum v4l2_mbus_type bus_type;
	u8 mipi_lanes;

	int (*s_power)(int enable);
	struct regulator_bulk_data supplies[S5K6AA_NUM_SUPPLIES];
	struct s5k6aa_gpio gpio[GPIO_NUM];

	/* external master clock frequency */
	unsigned long mclk_frequency;
	/* ISP internal master clock frequency */
	u16 clk_fop;
	/* output pixel clock frequency range */
	u16 pclk_fmin;
	u16 pclk_fmax;

	unsigned int inv_hflip:1;
	unsigned int inv_vflip:1;

	/* protects the struct members below */
	struct mutex lock;

	/* sensor matrix scan window */
	struct v4l2_rect ccd_rect;

	struct s5k6aa_ctrls ctrls;
	struct s5k6aa_preset presets[S5K6AA_MAX_PRESETS];
	struct s5k6aa_preset *preset;
	const struct s5k6aa_interval *fiv;

	unsigned int streaming:1;
	unsigned int apply_cfg:1;
	unsigned int apply_crop:1;
	unsigned int power;
};

static struct s5k6aa_regval s5k6aa_analog_config[] = {
	/* Analog settings */
	{ 0x112a, 0x0000 }, { 0x1132, 0x0000 },
	{ 0x113e, 0x0000 }, { 0x115c, 0x0000 },
	{ 0x1164, 0x0000 }, { 0x1174, 0x0000 },
	{ 0x1178, 0x0000 }, { 0x077a, 0x0000 },
	{ 0x077c, 0x0000 }, { 0x077e, 0x0000 },
	{ 0x0780, 0x0000 }, { 0x0782, 0x0000 },
	{ 0x0784, 0x0000 }, { 0x0786, 0x0000 },
	{ 0x0788, 0x0000 }, { 0x07a2, 0x0000 },
	{ 0x07a4, 0x0000 }, { 0x07a6, 0x0000 },
	{ 0x07a8, 0x0000 }, { 0x07b6, 0x0000 },
	{ 0x07b8, 0x0002 }, { 0x07ba, 0x0004 },
	{ 0x07bc, 0x0004 }, { 0x07be, 0x0005 },
	{ 0x07c0, 0x0005 }, { S5K6AA_TERM, 0 },
};

/* TODO: Add RGB888 and Bayer format */
static const struct s5k6aa_pixfmt s5k6aa_formats[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8,	V4L2_COLORSPACE_JPEG,	5 },
	/* range 16-240 */
	{ MEDIA_BUS_FMT_YUYV8_2X8,	V4L2_COLORSPACE_REC709,	6 },
	{ MEDIA_BUS_FMT_RGB565_2X8_BE,	V4L2_COLORSPACE_JPEG,	0 },
};

static const struct s5k6aa_interval s5k6aa_intervals[] = {
	{ 1000, {10000, 1000000}, {1280, 1024} }, /* 10 fps */
	{ 666,  {15000, 1000000}, {1280, 1024} }, /* 15 fps */
	{ 500,  {20000, 1000000}, {1280, 720} },  /* 20 fps */
	{ 400,  {25000, 1000000}, {640, 480} },   /* 25 fps */
	{ 333,  {33300, 1000000}, {640, 480} },   /* 30 fps */
};

#define S5K6AA_INTERVAL_DEF_INDEX 1 /* 15 fps */

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct s5k6aa, ctrls.handler)->sd;
}

static inline struct s5k6aa *to_s5k6aa(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k6aa, sd);
}

/* Set initial values for all preview presets */
static void s5k6aa_presets_data_init(struct s5k6aa *s5k6aa)
{
	struct s5k6aa_preset *preset = &s5k6aa->presets[0];
	int i;

	for (i = 0; i < S5K6AA_MAX_PRESETS; i++) {
		preset->mbus_fmt.width	= S5K6AA_OUT_WIDTH_DEF;
		preset->mbus_fmt.height	= S5K6AA_OUT_HEIGHT_DEF;
		preset->mbus_fmt.code	= s5k6aa_formats[0].code;
		preset->index		= i;
		preset->clk_id		= 0;
		preset++;
	}

	s5k6aa->fiv = &s5k6aa_intervals[S5K6AA_INTERVAL_DEF_INDEX];
	s5k6aa->preset = &s5k6aa->presets[0];
}

static int s5k6aa_i2c_read(struct i2c_client *client, u16 addr, u16 *val)
{
	u8 wbuf[2] = {addr >> 8, addr & 0xFF};
	struct i2c_msg msg[2];
	u8 rbuf[2];
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = wbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	*val = be16_to_cpu(*((__be16 *)rbuf));

	v4l2_dbg(3, debug, client, "i2c_read: 0x%04X : 0x%04x\n", addr, *val);

	return ret == 2 ? 0 : ret;
}

static int s5k6aa_i2c_write(struct i2c_client *client, u16 addr, u16 val)
{
	u8 buf[4] = {addr >> 8, addr & 0xFF, val >> 8, val & 0xFF};

	int ret = i2c_master_send(client, buf, 4);
	v4l2_dbg(3, debug, client, "i2c_write: 0x%04X : 0x%04x\n", addr, val);

	return ret == 4 ? 0 : ret;
}

/* The command register write, assumes Command_Wr_addH = 0x7000. */
static int s5k6aa_write(struct i2c_client *c, u16 addr, u16 val)
{
	int ret = s5k6aa_i2c_write(c, REG_CMDWR_ADDRL, addr);
	if (ret)
		return ret;
	return s5k6aa_i2c_write(c, REG_CMDBUF0_ADDR, val);
}

/* The command register read, assumes Command_Rd_addH = 0x7000. */
static int s5k6aa_read(struct i2c_client *client, u16 addr, u16 *val)
{
	int ret = s5k6aa_i2c_write(client, REG_CMDRD_ADDRL, addr);
	if (ret)
		return ret;
	return s5k6aa_i2c_read(client, REG_CMDBUF0_ADDR, val);
}

static int s5k6aa_write_array(struct v4l2_subdev *sd,
			      const struct s5k6aa_regval *msg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 addr_incr = 0;
	int ret = 0;

	while (msg->addr != S5K6AA_TERM) {
		if (addr_incr != 2)
			ret = s5k6aa_i2c_write(client, REG_CMDWR_ADDRL,
					       msg->addr);
		if (ret)
			break;
		ret = s5k6aa_i2c_write(client, REG_CMDBUF0_ADDR, msg->val);
		if (ret)
			break;
		/* Assume that msg->addr is always less than 0xfffc */
		addr_incr = (msg + 1)->addr - msg->addr;
		msg++;
	}

	return ret;
}

/* Configure the AHB high address bytes for GTG registers access */
static int s5k6aa_set_ahb_address(struct i2c_client *client)
{
	int ret = s5k6aa_i2c_write(client, AHB_MSB_ADDR_PTR, GEN_REG_OFFSH);
	if (ret)
		return ret;
	ret = s5k6aa_i2c_write(client, REG_CMDRD_ADDRH, HOST_SWIF_OFFSH);
	if (ret)
		return ret;
	return s5k6aa_i2c_write(client, REG_CMDWR_ADDRH, HOST_SWIF_OFFSH);
}

/**
 * s5k6aa_configure_pixel_clocks - apply ISP main clock/PLL configuration
 * @s5k6aa: pointer to &struct s5k6aa describing the device
 *
 * Configure the internal ISP PLL for the required output frequency.
 * Locking: called with s5k6aa.lock mutex held.
 */
static int s5k6aa_configure_pixel_clocks(struct s5k6aa *s5k6aa)
{
	struct i2c_client *c = v4l2_get_subdevdata(&s5k6aa->sd);
	unsigned long fmclk = s5k6aa->mclk_frequency / 1000;
	u16 status;
	int ret;

	if (WARN(fmclk < MIN_MCLK_FREQ_KHZ || fmclk > MAX_MCLK_FREQ_KHZ,
		 "Invalid clock frequency: %ld\n", fmclk))
		return -EINVAL;

	s5k6aa->pclk_fmin = PCLK_FREQ_MIN;
	s5k6aa->pclk_fmax = PCLK_FREQ_MAX;
	s5k6aa->clk_fop = SYS_PLL_OUT_FREQ;

	/* External input clock frequency in kHz */
	ret = s5k6aa_write(c, REG_I_INCLK_FREQ_H, fmclk >> 16);
	if (!ret)
		ret = s5k6aa_write(c, REG_I_INCLK_FREQ_L, fmclk & 0xFFFF);
	if (!ret)
		ret = s5k6aa_write(c, REG_I_USE_NPVI_CLOCKS, 1);
	/* Internal PLL frequency */
	if (!ret)
		ret = s5k6aa_write(c, REG_I_OPCLK_4KHZ(0), s5k6aa->clk_fop);
	if (!ret)
		ret = s5k6aa_write(c, REG_I_MIN_OUTRATE_4KHZ(0),
				   s5k6aa->pclk_fmin);
	if (!ret)
		ret = s5k6aa_write(c, REG_I_MAX_OUTRATE_4KHZ(0),
				   s5k6aa->pclk_fmax);
	if (!ret)
		ret = s5k6aa_write(c, REG_I_INIT_PARAMS_UPDATED, 1);
	if (!ret)
		ret = s5k6aa_read(c, REG_I_ERROR_INFO, &status);

	return ret ? ret : (status ? -EINVAL : 0);
}

/* Set horizontal and vertical image flipping */
static int s5k6aa_set_mirror(struct s5k6aa *s5k6aa, int horiz_flip)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	int index = s5k6aa->preset->index;

	unsigned int vflip = s5k6aa->ctrls.vflip->val ^ s5k6aa->inv_vflip;
	unsigned int flip = (horiz_flip ^ s5k6aa->inv_hflip) | (vflip << 1);

	return s5k6aa_write(client, REG_P_PREV_MIRROR(index), flip);
}

/* Configure auto/manual white balance and R/G/B gains */
static int s5k6aa_set_awb(struct s5k6aa *s5k6aa, int awb)
{
	struct i2c_client *c = v4l2_get_subdevdata(&s5k6aa->sd);
	struct s5k6aa_ctrls *ctrls = &s5k6aa->ctrls;
	u16 reg;

	int ret = s5k6aa_read(c, REG_DBG_AUTOALG_EN, &reg);

	if (!ret && !awb) {
		ret = s5k6aa_write(c, REG_SF_RGAIN, ctrls->gain_red->val);
		if (!ret)
			ret = s5k6aa_write(c, REG_SF_RGAIN_CHG, 1);
		if (ret)
			return ret;

		ret = s5k6aa_write(c, REG_SF_GGAIN, ctrls->gain_green->val);
		if (!ret)
			ret = s5k6aa_write(c, REG_SF_GGAIN_CHG, 1);
		if (ret)
			return ret;

		ret = s5k6aa_write(c, REG_SF_BGAIN, ctrls->gain_blue->val);
		if (!ret)
			ret = s5k6aa_write(c, REG_SF_BGAIN_CHG, 1);
	}
	if (!ret) {
		reg = awb ? reg | AALG_WB_EN_MASK : reg & ~AALG_WB_EN_MASK;
		ret = s5k6aa_write(c, REG_DBG_AUTOALG_EN, reg);
	}

	return ret;
}

/* Program FW with exposure time, 'exposure' in us units */
static int s5k6aa_set_user_exposure(struct i2c_client *client, int exposure)
{
	unsigned int time = exposure / 10;

	int ret = s5k6aa_write(client, REG_SF_USR_EXPOSURE_L, time & 0xffff);
	if (!ret)
		ret = s5k6aa_write(client, REG_SF_USR_EXPOSURE_H, time >> 16);
	if (ret)
		return ret;
	return s5k6aa_write(client, REG_SF_USR_EXPOSURE_CHG, 1);
}

static int s5k6aa_set_user_gain(struct i2c_client *client, int gain)
{
	int ret = s5k6aa_write(client, REG_SF_USR_TOT_GAIN, gain);
	if (ret)
		return ret;
	return s5k6aa_write(client, REG_SF_USR_TOT_GAIN_CHG, 1);
}

/* Set auto/manual exposure and total gain */
static int s5k6aa_set_auto_exposure(struct s5k6aa *s5k6aa, int value)
{
	struct i2c_client *c = v4l2_get_subdevdata(&s5k6aa->sd);
	unsigned int exp_time = s5k6aa->ctrls.exposure->val;
	u16 auto_alg;

	int ret = s5k6aa_read(c, REG_DBG_AUTOALG_EN, &auto_alg);
	if (ret)
		return ret;

	v4l2_dbg(1, debug, c, "man_exp: %d, auto_exp: %d, a_alg: 0x%x\n",
		 exp_time, value, auto_alg);

	if (value == V4L2_EXPOSURE_AUTO) {
		auto_alg |= AALG_AE_EN_MASK | AALG_DIVLEI_EN_MASK;
	} else {
		ret = s5k6aa_set_user_exposure(c, exp_time);
		if (ret)
			return ret;
		ret = s5k6aa_set_user_gain(c, s5k6aa->ctrls.gain->val);
		if (ret)
			return ret;
		auto_alg &= ~(AALG_AE_EN_MASK | AALG_DIVLEI_EN_MASK);
	}

	return s5k6aa_write(c, REG_DBG_AUTOALG_EN, auto_alg);
}

static int s5k6aa_set_anti_flicker(struct s5k6aa *s5k6aa, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	u16 auto_alg;
	int ret;

	ret = s5k6aa_read(client, REG_DBG_AUTOALG_EN, &auto_alg);
	if (ret)
		return ret;

	if (value == V4L2_CID_POWER_LINE_FREQUENCY_AUTO) {
		auto_alg |= AALG_FLICKER_EN_MASK;
	} else {
		auto_alg &= ~AALG_FLICKER_EN_MASK;
		/* The V4L2_CID_LINE_FREQUENCY control values match
		 * the register values */
		ret = s5k6aa_write(client, REG_SF_FLICKER_QUANT, value);
		if (ret)
			return ret;
		ret = s5k6aa_write(client, REG_SF_FLICKER_QUANT_CHG, 1);
		if (ret)
			return ret;
	}

	return s5k6aa_write(client, REG_DBG_AUTOALG_EN, auto_alg);
}

static int s5k6aa_set_colorfx(struct s5k6aa *s5k6aa, int val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	static const struct v4l2_control colorfx[] = {
		{ V4L2_COLORFX_NONE,	 0 },
		{ V4L2_COLORFX_BW,	 1 },
		{ V4L2_COLORFX_NEGATIVE, 2 },
		{ V4L2_COLORFX_SEPIA,	 3 },
		{ V4L2_COLORFX_SKY_BLUE, 4 },
		{ V4L2_COLORFX_SKETCH,	 5 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(colorfx); i++) {
		if (colorfx[i].id == val)
			return s5k6aa_write(client, REG_G_SPEC_EFFECTS,
					    colorfx[i].value);
	}
	return -EINVAL;
}

static int s5k6aa_preview_config_status(struct i2c_client *client)
{
	u16 error = 0;
	int ret = s5k6aa_read(client, REG_G_PREV_CFG_ERROR, &error);

	v4l2_dbg(1, debug, client, "error: 0x%x (%d)\n", error, ret);
	return ret ? ret : (error ? -EINVAL : 0);
}

static int s5k6aa_get_pixfmt_index(struct s5k6aa *s5k6aa,
				   struct v4l2_mbus_framefmt *mf)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(s5k6aa_formats); i++)
		if (mf->colorspace == s5k6aa_formats[i].colorspace &&
		    mf->code == s5k6aa_formats[i].code)
			return i;
	return 0;
}

static int s5k6aa_set_output_framefmt(struct s5k6aa *s5k6aa,
				      struct s5k6aa_preset *preset)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	int fmt_index = s5k6aa_get_pixfmt_index(s5k6aa, &preset->mbus_fmt);
	int ret;

	ret = s5k6aa_write(client, REG_P_OUT_WIDTH(preset->index),
			   preset->mbus_fmt.width);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_OUT_HEIGHT(preset->index),
				   preset->mbus_fmt.height);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_FMT(preset->index),
				   s5k6aa_formats[fmt_index].reg_p_fmt);
	return ret;
}

static int s5k6aa_set_input_params(struct s5k6aa *s5k6aa)
{
	struct i2c_client *c = v4l2_get_subdevdata(&s5k6aa->sd);
	struct v4l2_rect *r = &s5k6aa->ccd_rect;
	int ret;

	ret = s5k6aa_write(c, REG_G_PREVZOOM_IN_WIDTH, r->width);
	if (!ret)
		ret = s5k6aa_write(c, REG_G_PREVZOOM_IN_HEIGHT, r->height);
	if (!ret)
		ret = s5k6aa_write(c, REG_G_PREVZOOM_IN_XOFFS, r->left);
	if (!ret)
		ret = s5k6aa_write(c, REG_G_PREVZOOM_IN_YOFFS, r->top);
	if (!ret)
		ret = s5k6aa_write(c, REG_G_INPUTS_CHANGE_REQ, 1);
	if (!ret)
		s5k6aa->apply_crop = 0;

	return ret;
}

/**
 * s5k6aa_configure_video_bus - configure the video output interface
 * @s5k6aa: pointer to &struct s5k6aa describing the device
 * @bus_type: video bus type: parallel or MIPI-CSI
 * @nlanes: number of MIPI lanes to be used (MIPI-CSI only)
 *
 * Note: Only parallel bus operation has been tested.
 */
static int s5k6aa_configure_video_bus(struct s5k6aa *s5k6aa,
				      enum v4l2_mbus_type bus_type, int nlanes)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	u16 cfg = 0;
	int ret;

	/*
	 * TODO: The sensor is supposed to support BT.601 and BT.656
	 * but there is nothing indicating how to switch between both
	 * in the datasheet. For now default BT.601 interface is assumed.
	 */
	if (bus_type == V4L2_MBUS_CSI2_DPHY)
		cfg = nlanes;
	else if (bus_type != V4L2_MBUS_PARALLEL)
		return -EINVAL;

	ret = s5k6aa_write(client, REG_OIF_EN_MIPI_LANES, cfg);
	if (ret)
		return ret;
	return s5k6aa_write(client, REG_OIF_CFG_CHG, 1);
}

/* This function should be called when switching to new user configuration set*/
static int s5k6aa_new_config_sync(struct i2c_client *client, int timeout,
				  int cid)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout);
	u16 reg = 1;
	int ret;

	ret = s5k6aa_write(client, REG_G_ACTIVE_PREV_CFG, cid);
	if (!ret)
		ret = s5k6aa_write(client, REG_G_PREV_CFG_CHG, 1);
	if (!ret)
		ret = s5k6aa_write(client, REG_G_NEW_CFG_SYNC, 1);
	if (timeout == 0)
		return ret;

	while (ret >= 0 && time_is_after_jiffies(end)) {
		ret = s5k6aa_read(client, REG_G_NEW_CFG_SYNC, &reg);
		if (!reg)
			return 0;
		usleep_range(1000, 5000);
	}
	return ret ? ret : -ETIMEDOUT;
}

/**
 * s5k6aa_set_prev_config - write user preview register set
 * @s5k6aa: pointer to &struct s5k6aa describing the device
 * @preset: s5kaa preset to be applied
 *
 * Configure output resolution and color format, pixel clock
 * frequency range, device frame rate type and frame period range.
 */
static int s5k6aa_set_prev_config(struct s5k6aa *s5k6aa,
				  struct s5k6aa_preset *preset)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	int idx = preset->index;
	u16 frame_rate_q;
	int ret;

	if (s5k6aa->fiv->reg_fr_time >= S5K6AA_MAX_HIGHRES_FR_TIME)
		frame_rate_q = FR_RATE_Q_BEST_FRRATE;
	else
		frame_rate_q = FR_RATE_Q_BEST_QUALITY;

	ret = s5k6aa_set_output_framefmt(s5k6aa, preset);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_MAX_OUT_RATE(idx),
				   s5k6aa->pclk_fmax);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_MIN_OUT_RATE(idx),
				   s5k6aa->pclk_fmin);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_CLK_INDEX(idx),
				   preset->clk_id);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_FR_RATE_TYPE(idx),
				   FR_RATE_DYNAMIC);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_FR_RATE_Q_TYPE(idx),
				   frame_rate_q);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_MAX_FR_TIME(idx),
				   s5k6aa->fiv->reg_fr_time + 33);
	if (!ret)
		ret = s5k6aa_write(client, REG_P_MIN_FR_TIME(idx),
				   s5k6aa->fiv->reg_fr_time - 33);
	if (!ret)
		ret = s5k6aa_new_config_sync(client, 250, idx);
	if (!ret)
		ret = s5k6aa_preview_config_status(client);
	if (!ret)
		s5k6aa->apply_cfg = 0;

	v4l2_dbg(1, debug, client, "Frame interval: %d +/- 3.3ms. (%d)\n",
		 s5k6aa->fiv->reg_fr_time, ret);
	return ret;
}

/**
 * s5k6aa_initialize_isp - basic ISP MCU initialization
 * @sd: pointer to V4L2 sub-device descriptor
 *
 * Configure AHB addresses for registers read/write; configure PLLs for
 * required output pixel clock. The ISP power supply needs to be already
 * enabled, with an optional H/W reset.
 * Locking: called with s5k6aa.lock mutex held.
 */
static int s5k6aa_initialize_isp(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int ret;

	s5k6aa->apply_crop = 1;
	s5k6aa->apply_cfg = 1;
	msleep(100);

	ret = s5k6aa_set_ahb_address(client);
	if (ret)
		return ret;
	ret = s5k6aa_configure_video_bus(s5k6aa, s5k6aa->bus_type,
					 s5k6aa->mipi_lanes);
	if (ret)
		return ret;
	ret = s5k6aa_write_array(sd, s5k6aa_analog_config);
	if (ret)
		return ret;
	msleep(20);

	return s5k6aa_configure_pixel_clocks(s5k6aa);
}

static int s5k6aa_gpio_set_value(struct s5k6aa *priv, int id, u32 val)
{
	if (!gpio_is_valid(priv->gpio[id].gpio))
		return 0;
	gpio_set_value(priv->gpio[id].gpio, !!val);
	return 1;
}

static int s5k6aa_gpio_assert(struct s5k6aa *priv, int id)
{
	return s5k6aa_gpio_set_value(priv, id, priv->gpio[id].level);
}

static int s5k6aa_gpio_deassert(struct s5k6aa *priv, int id)
{
	return s5k6aa_gpio_set_value(priv, id, !priv->gpio[id].level);
}

static int __s5k6aa_power_on(struct s5k6aa *s5k6aa)
{
	int ret;

	ret = regulator_bulk_enable(S5K6AA_NUM_SUPPLIES, s5k6aa->supplies);
	if (ret)
		return ret;
	if (s5k6aa_gpio_deassert(s5k6aa, STBY))
		usleep_range(150, 200);

	if (s5k6aa->s_power)
		ret = s5k6aa->s_power(1);
	usleep_range(4000, 5000);

	if (s5k6aa_gpio_deassert(s5k6aa, RST))
		msleep(20);

	return ret;
}

static int __s5k6aa_power_off(struct s5k6aa *s5k6aa)
{
	int ret;

	if (s5k6aa_gpio_assert(s5k6aa, RST))
		usleep_range(100, 150);

	if (s5k6aa->s_power) {
		ret = s5k6aa->s_power(0);
		if (ret)
			return ret;
	}
	if (s5k6aa_gpio_assert(s5k6aa, STBY))
		usleep_range(50, 100);
	s5k6aa->streaming = 0;

	return regulator_bulk_disable(S5K6AA_NUM_SUPPLIES, s5k6aa->supplies);
}

/*
 * V4L2 subdev core and video operations
 */
static int s5k6aa_set_power(struct v4l2_subdev *sd, int on)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int ret = 0;

	mutex_lock(&s5k6aa->lock);

	if (s5k6aa->power == !on) {
		if (on) {
			ret = __s5k6aa_power_on(s5k6aa);
			if (!ret)
				ret = s5k6aa_initialize_isp(sd);
		} else {
			ret = __s5k6aa_power_off(s5k6aa);
		}

		if (!ret)
			s5k6aa->power += on ? 1 : -1;
	}

	mutex_unlock(&s5k6aa->lock);

	if (!on || ret || s5k6aa->power != 1)
		return ret;

	return v4l2_ctrl_handler_setup(sd->ctrl_handler);
}

static int __s5k6aa_stream(struct s5k6aa *s5k6aa, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	int ret = 0;

	ret = s5k6aa_write(client, REG_G_ENABLE_PREV, enable);
	if (!ret)
		ret = s5k6aa_write(client, REG_G_ENABLE_PREV_CHG, 1);
	if (!ret)
		s5k6aa->streaming = enable;

	return ret;
}

static int s5k6aa_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int ret = 0;

	mutex_lock(&s5k6aa->lock);

	if (s5k6aa->streaming == !on) {
		if (!ret && s5k6aa->apply_cfg)
			ret = s5k6aa_set_prev_config(s5k6aa, s5k6aa->preset);
		if (s5k6aa->apply_crop)
			ret = s5k6aa_set_input_params(s5k6aa);
		if (!ret)
			ret = __s5k6aa_stream(s5k6aa, !!on);
	}
	mutex_unlock(&s5k6aa->lock);

	return ret;
}

static int s5k6aa_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);

	mutex_lock(&s5k6aa->lock);
	fi->interval = s5k6aa->fiv->interval;
	mutex_unlock(&s5k6aa->lock);

	return 0;
}

static int __s5k6aa_set_frame_interval(struct s5k6aa *s5k6aa,
				       struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_mbus_framefmt *mbus_fmt = &s5k6aa->preset->mbus_fmt;
	const struct s5k6aa_interval *fiv = &s5k6aa_intervals[0];
	unsigned int err, min_err = UINT_MAX;
	unsigned int i, fr_time;

	if (fi->interval.denominator == 0)
		return -EINVAL;

	fr_time = fi->interval.numerator * 10000 / fi->interval.denominator;

	for (i = 0; i < ARRAY_SIZE(s5k6aa_intervals); i++) {
		const struct s5k6aa_interval *iv = &s5k6aa_intervals[i];

		if (mbus_fmt->width > iv->size.width ||
		    mbus_fmt->height > iv->size.height)
			continue;

		err = abs(iv->reg_fr_time - fr_time);
		if (err < min_err) {
			fiv = iv;
			min_err = err;
		}
	}
	s5k6aa->fiv = fiv;

	v4l2_dbg(1, debug, &s5k6aa->sd, "Changed frame interval to %d us\n",
		 fiv->reg_fr_time * 100);
	return 0;
}

static int s5k6aa_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int ret;

	v4l2_dbg(1, debug, sd, "Setting %d/%d frame interval\n",
		 fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&s5k6aa->lock);
	ret = __s5k6aa_set_frame_interval(s5k6aa, fi);
	s5k6aa->apply_cfg = 1;

	mutex_unlock(&s5k6aa->lock);
	return ret;
}

/*
 * V4L2 subdev pad level and video operations
 */
static int s5k6aa_enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	const struct s5k6aa_interval *fi;
	int ret = 0;

	if (fie->index >= ARRAY_SIZE(s5k6aa_intervals))
		return -EINVAL;

	v4l_bound_align_image(&fie->width, S5K6AA_WIN_WIDTH_MIN,
			      S5K6AA_WIN_WIDTH_MAX, 1,
			      &fie->height, S5K6AA_WIN_HEIGHT_MIN,
			      S5K6AA_WIN_HEIGHT_MAX, 1, 0);

	mutex_lock(&s5k6aa->lock);
	fi = &s5k6aa_intervals[fie->index];
	if (fie->width > fi->size.width || fie->height > fi->size.height)
		ret = -EINVAL;
	else
		fie->interval = fi->interval;
	mutex_unlock(&s5k6aa->lock);

	return ret;
}

static int s5k6aa_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(s5k6aa_formats))
		return -EINVAL;

	code->code = s5k6aa_formats[code->index].code;
	return 0;
}

static int s5k6aa_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int i = ARRAY_SIZE(s5k6aa_formats);

	if (fse->index > 0)
		return -EINVAL;

	while (--i)
		if (fse->code == s5k6aa_formats[i].code)
			break;

	fse->code = s5k6aa_formats[i].code;
	fse->min_width  = S5K6AA_WIN_WIDTH_MIN;
	fse->max_width  = S5K6AA_WIN_WIDTH_MAX;
	fse->max_height = S5K6AA_WIN_HEIGHT_MIN;
	fse->min_height = S5K6AA_WIN_HEIGHT_MAX;

	return 0;
}

static struct v4l2_rect *
__s5k6aa_get_crop_rect(struct s5k6aa *s5k6aa, struct v4l2_subdev_pad_config *cfg,
		       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return &s5k6aa->ccd_rect;

	WARN_ON(which != V4L2_SUBDEV_FORMAT_TRY);
	return v4l2_subdev_get_try_crop(&s5k6aa->sd, cfg, 0);
}

static void s5k6aa_try_format(struct s5k6aa *s5k6aa,
			      struct v4l2_mbus_framefmt *mf)
{
	unsigned int index;

	v4l_bound_align_image(&mf->width, S5K6AA_WIN_WIDTH_MIN,
			      S5K6AA_WIN_WIDTH_MAX, 1,
			      &mf->height, S5K6AA_WIN_HEIGHT_MIN,
			      S5K6AA_WIN_HEIGHT_MAX, 1, 0);

	if (mf->colorspace != V4L2_COLORSPACE_JPEG &&
	    mf->colorspace != V4L2_COLORSPACE_REC709)
		mf->colorspace = V4L2_COLORSPACE_JPEG;

	index = s5k6aa_get_pixfmt_index(s5k6aa, mf);

	mf->colorspace	= s5k6aa_formats[index].colorspace;
	mf->code	= s5k6aa_formats[index].code;
	mf->field	= V4L2_FIELD_NONE;
}

static int s5k6aa_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	struct v4l2_mbus_framefmt *mf;

	memset(fmt->reserved, 0, sizeof(fmt->reserved));

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		fmt->format = *mf;
		return 0;
	}

	mutex_lock(&s5k6aa->lock);
	fmt->format = s5k6aa->preset->mbus_fmt;
	mutex_unlock(&s5k6aa->lock);

	return 0;
}

static int s5k6aa_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	struct s5k6aa_preset *preset = s5k6aa->preset;
	struct v4l2_mbus_framefmt *mf;
	struct v4l2_rect *crop;
	int ret = 0;

	mutex_lock(&s5k6aa->lock);
	s5k6aa_try_format(s5k6aa, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		crop = v4l2_subdev_get_try_crop(sd, cfg, 0);
	} else {
		if (s5k6aa->streaming) {
			ret = -EBUSY;
		} else {
			mf = &preset->mbus_fmt;
			crop = &s5k6aa->ccd_rect;
			s5k6aa->apply_cfg = 1;
		}
	}

	if (ret == 0) {
		struct v4l2_subdev_frame_interval fiv = {
			.interval = {0, 1}
		};

		*mf = fmt->format;
		/*
		 * Make sure the crop window is valid, i.e. its size is
		 * greater than the output window, as the ISP supports
		 * only down-scaling.
		 */
		crop->width = clamp_t(unsigned int, crop->width, mf->width,
				      S5K6AA_WIN_WIDTH_MAX);
		crop->height = clamp_t(unsigned int, crop->height, mf->height,
				       S5K6AA_WIN_HEIGHT_MAX);
		crop->left = clamp_t(unsigned int, crop->left, 0,
				     S5K6AA_WIN_WIDTH_MAX - crop->width);
		crop->top  = clamp_t(unsigned int, crop->top, 0,
				     S5K6AA_WIN_HEIGHT_MAX - crop->height);

		/* Reset to minimum possible frame interval */
		ret = __s5k6aa_set_frame_interval(s5k6aa, &fiv);
	}
	mutex_unlock(&s5k6aa->lock);

	return ret;
}

static int s5k6aa_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	struct v4l2_rect *rect;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	memset(sel->reserved, 0, sizeof(sel->reserved));

	mutex_lock(&s5k6aa->lock);
	rect = __s5k6aa_get_crop_rect(s5k6aa, cfg, sel->which);
	sel->r = *rect;
	mutex_unlock(&s5k6aa->lock);

	v4l2_dbg(1, debug, sd, "Current crop rectangle: (%d,%d)/%dx%d\n",
		 rect->left, rect->top, rect->width, rect->height);

	return 0;
}

static int s5k6aa_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	struct v4l2_mbus_framefmt *mf;
	unsigned int max_x, max_y;
	struct v4l2_rect *crop_r;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	mutex_lock(&s5k6aa->lock);
	crop_r = __s5k6aa_get_crop_rect(s5k6aa, cfg, sel->which);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		mf = &s5k6aa->preset->mbus_fmt;
		s5k6aa->apply_crop = 1;
	} else {
		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
	}
	v4l_bound_align_image(&sel->r.width, mf->width,
			      S5K6AA_WIN_WIDTH_MAX, 1,
			      &sel->r.height, mf->height,
			      S5K6AA_WIN_HEIGHT_MAX, 1, 0);

	max_x = (S5K6AA_WIN_WIDTH_MAX - sel->r.width) & ~1;
	max_y = (S5K6AA_WIN_HEIGHT_MAX - sel->r.height) & ~1;

	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, max_x);
	sel->r.top  = clamp_t(unsigned int, sel->r.top, 0, max_y);

	*crop_r = sel->r;

	mutex_unlock(&s5k6aa->lock);

	v4l2_dbg(1, debug, sd, "Set crop rectangle: (%d,%d)/%dx%d\n",
		 crop_r->left, crop_r->top, crop_r->width, crop_r->height);

	return 0;
}

static const struct v4l2_subdev_pad_ops s5k6aa_pad_ops = {
	.enum_mbus_code		= s5k6aa_enum_mbus_code,
	.enum_frame_size	= s5k6aa_enum_frame_size,
	.enum_frame_interval	= s5k6aa_enum_frame_interval,
	.get_fmt		= s5k6aa_get_fmt,
	.set_fmt		= s5k6aa_set_fmt,
	.get_selection		= s5k6aa_get_selection,
	.set_selection		= s5k6aa_set_selection,
};

static const struct v4l2_subdev_video_ops s5k6aa_video_ops = {
	.g_frame_interval	= s5k6aa_g_frame_interval,
	.s_frame_interval	= s5k6aa_s_frame_interval,
	.s_stream		= s5k6aa_s_stream,
};

/*
 * V4L2 subdev controls
 */

static int s5k6aa_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int idx, err = 0;

	v4l2_dbg(1, debug, sd, "ctrl: 0x%x, value: %d\n", ctrl->id, ctrl->val);

	mutex_lock(&s5k6aa->lock);
	/*
	 * If the device is not powered up by the host driver do
	 * not apply any controls to H/W at this time. Instead
	 * the controls will be restored right after power-up.
	 */
	if (s5k6aa->power == 0)
		goto unlock;
	idx = s5k6aa->preset->index;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		err = s5k6aa_set_awb(s5k6aa, ctrl->val);
		break;

	case V4L2_CID_BRIGHTNESS:
		err = s5k6aa_write(client, REG_USER_BRIGHTNESS, ctrl->val);
		break;

	case V4L2_CID_COLORFX:
		err = s5k6aa_set_colorfx(s5k6aa, ctrl->val);
		break;

	case V4L2_CID_CONTRAST:
		err = s5k6aa_write(client, REG_USER_CONTRAST, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		err = s5k6aa_set_auto_exposure(s5k6aa, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		err = s5k6aa_set_mirror(s5k6aa, ctrl->val);
		if (err)
			break;
		err = s5k6aa_write(client, REG_G_PREV_CFG_CHG, 1);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		err = s5k6aa_set_anti_flicker(s5k6aa, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		err = s5k6aa_write(client, REG_USER_SATURATION, ctrl->val);
		break;

	case V4L2_CID_SHARPNESS:
		err = s5k6aa_write(client, REG_USER_SHARPBLUR, ctrl->val);
		break;

	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		err = s5k6aa_write(client, REG_P_COLORTEMP(idx), ctrl->val);
		if (err)
			break;
		err = s5k6aa_write(client, REG_G_PREV_CFG_CHG, 1);
		break;
	}
unlock:
	mutex_unlock(&s5k6aa->lock);
	return err;
}

static const struct v4l2_ctrl_ops s5k6aa_ctrl_ops = {
	.s_ctrl	= s5k6aa_s_ctrl,
};

static int s5k6aa_log_status(struct v4l2_subdev *sd)
{
	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);
	return 0;
}

#define V4L2_CID_RED_GAIN	(V4L2_CTRL_CLASS_CAMERA | 0x1001)
#define V4L2_CID_GREEN_GAIN	(V4L2_CTRL_CLASS_CAMERA | 0x1002)
#define V4L2_CID_BLUE_GAIN	(V4L2_CTRL_CLASS_CAMERA | 0x1003)

static const struct v4l2_ctrl_config s5k6aa_ctrls[] = {
	{
		.ops	= &s5k6aa_ctrl_ops,
		.id	= V4L2_CID_RED_GAIN,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Gain, Red",
		.min	= 0,
		.max	= 256,
		.def	= 127,
		.step	= 1,
	}, {
		.ops	= &s5k6aa_ctrl_ops,
		.id	= V4L2_CID_GREEN_GAIN,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Gain, Green",
		.min	= 0,
		.max	= 256,
		.def	= 127,
		.step	= 1,
	}, {
		.ops	= &s5k6aa_ctrl_ops,
		.id	= V4L2_CID_BLUE_GAIN,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Gain, Blue",
		.min	= 0,
		.max	= 256,
		.def	= 127,
		.step	= 1,
	},
};

static int s5k6aa_initialize_ctrls(struct s5k6aa *s5k6aa)
{
	const struct v4l2_ctrl_ops *ops = &s5k6aa_ctrl_ops;
	struct s5k6aa_ctrls *ctrls = &s5k6aa->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;

	int ret = v4l2_ctrl_handler_init(hdl, 16);
	if (ret)
		return ret;
	/* Auto white balance cluster */
	ctrls->awb = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTO_WHITE_BALANCE,
				       0, 1, 1, 1);
	ctrls->gain_red = v4l2_ctrl_new_custom(hdl, &s5k6aa_ctrls[0], NULL);
	ctrls->gain_green = v4l2_ctrl_new_custom(hdl, &s5k6aa_ctrls[1], NULL);
	ctrls->gain_blue = v4l2_ctrl_new_custom(hdl, &s5k6aa_ctrls[2], NULL);
	v4l2_ctrl_auto_cluster(4, &ctrls->awb, 0, false);

	ctrls->hflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_cluster(2, &ctrls->hflip);

	ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
				V4L2_CID_EXPOSURE_AUTO,
				V4L2_EXPOSURE_MANUAL, 0, V4L2_EXPOSURE_AUTO);
	/* Exposure time: x 1 us */
	ctrls->exposure = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_EXPOSURE,
					    0, 6000000U, 1, 100000U);
	/* Total gain: 256 <=> 1x */
	ctrls->gain = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAIN,
					0, 256, 1, 256);
	v4l2_ctrl_auto_cluster(3, &ctrls->auto_exp, 0, false);

	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO);

	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_COLORFX,
			       V4L2_COLORFX_SKY_BLUE, ~0x6f, V4L2_COLORFX_NONE);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_WHITE_BALANCE_TEMPERATURE,
			  0, 256, 1, 0);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION, -127, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS, -127, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -127, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SHARPNESS, -127, 127, 1, 0);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	s5k6aa->sd.ctrl_handler = hdl;
	return 0;
}

/*
 * V4L2 subdev internal operations
 */
static int s5k6aa_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	struct v4l2_rect *crop = v4l2_subdev_get_try_crop(sd, fh->pad, 0);

	format->colorspace = s5k6aa_formats[0].colorspace;
	format->code = s5k6aa_formats[0].code;
	format->width = S5K6AA_OUT_WIDTH_DEF;
	format->height = S5K6AA_OUT_HEIGHT_DEF;
	format->field = V4L2_FIELD_NONE;

	crop->width = S5K6AA_WIN_WIDTH_MAX;
	crop->height = S5K6AA_WIN_HEIGHT_MAX;
	crop->left = 0;
	crop->top = 0;

	return 0;
}

static int s5k6aa_check_fw_revision(struct s5k6aa *s5k6aa)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	u16 api_ver = 0, fw_rev = 0;

	int ret = s5k6aa_set_ahb_address(client);

	if (!ret)
		ret = s5k6aa_read(client, REG_FW_APIVER, &api_ver);
	if (!ret)
		ret = s5k6aa_read(client, REG_FW_REVISION, &fw_rev);
	if (ret) {
		v4l2_err(&s5k6aa->sd, "FW revision check failed!\n");
		return ret;
	}

	v4l2_info(&s5k6aa->sd, "FW API ver.: 0x%X, FW rev.: 0x%X\n",
		  api_ver, fw_rev);

	return api_ver == S5K6AAFX_FW_APIVER ? 0 : -ENODEV;
}

static int s5k6aa_registered(struct v4l2_subdev *sd)
{
	struct s5k6aa *s5k6aa = to_s5k6aa(sd);
	int ret;

	mutex_lock(&s5k6aa->lock);
	ret = __s5k6aa_power_on(s5k6aa);
	if (!ret) {
		msleep(100);
		ret = s5k6aa_check_fw_revision(s5k6aa);
		__s5k6aa_power_off(s5k6aa);
	}
	mutex_unlock(&s5k6aa->lock);

	return ret;
}

static const struct v4l2_subdev_internal_ops s5k6aa_subdev_internal_ops = {
	.registered = s5k6aa_registered,
	.open = s5k6aa_open,
};

static const struct v4l2_subdev_core_ops s5k6aa_core_ops = {
	.s_power = s5k6aa_set_power,
	.log_status = s5k6aa_log_status,
};

static const struct v4l2_subdev_ops s5k6aa_subdev_ops = {
	.core = &s5k6aa_core_ops,
	.pad = &s5k6aa_pad_ops,
	.video = &s5k6aa_video_ops,
};

/*
 * GPIO setup
 */

static int s5k6aa_configure_gpios(struct s5k6aa *s5k6aa,
				  const struct s5k6aa_platform_data *pdata)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k6aa->sd);
	const struct s5k6aa_gpio *gpio;
	unsigned long flags;
	int ret;

	s5k6aa->gpio[STBY].gpio = -EINVAL;
	s5k6aa->gpio[RST].gpio  = -EINVAL;

	gpio = &pdata->gpio_stby;
	if (gpio_is_valid(gpio->gpio)) {
		flags = (gpio->level ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW)
		      | GPIOF_EXPORT;
		ret = devm_gpio_request_one(&client->dev, gpio->gpio, flags,
					    "S5K6AA_STBY");
		if (ret < 0)
			return ret;

		s5k6aa->gpio[STBY] = *gpio;
	}

	gpio = &pdata->gpio_reset;
	if (gpio_is_valid(gpio->gpio)) {
		flags = (gpio->level ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW)
		      | GPIOF_EXPORT;
		ret = devm_gpio_request_one(&client->dev, gpio->gpio, flags,
					    "S5K6AA_RST");
		if (ret < 0)
			return ret;

		s5k6aa->gpio[RST] = *gpio;
	}

	return 0;
}

static int s5k6aa_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	const struct s5k6aa_platform_data *pdata = client->dev.platform_data;
	struct v4l2_subdev *sd;
	struct s5k6aa *s5k6aa;
	int i, ret;

	if (pdata == NULL) {
		dev_err(&client->dev, "Platform data not specified\n");
		return -EINVAL;
	}

	if (pdata->mclk_frequency == 0) {
		dev_err(&client->dev, "MCLK frequency not specified\n");
		return -EINVAL;
	}

	s5k6aa = devm_kzalloc(&client->dev, sizeof(*s5k6aa), GFP_KERNEL);
	if (!s5k6aa)
		return -ENOMEM;

	mutex_init(&s5k6aa->lock);

	s5k6aa->mclk_frequency = pdata->mclk_frequency;
	s5k6aa->bus_type = pdata->bus_type;
	s5k6aa->mipi_lanes = pdata->nlanes;
	s5k6aa->s_power	= pdata->set_power;
	s5k6aa->inv_hflip = pdata->horiz_flip;
	s5k6aa->inv_vflip = pdata->vert_flip;

	sd = &s5k6aa->sd;
	v4l2_i2c_subdev_init(sd, client, &s5k6aa_subdev_ops);
	/* Static name; NEVER use in new drivers! */
	strscpy(sd->name, DRIVER_NAME, sizeof(sd->name));

	sd->internal_ops = &s5k6aa_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	s5k6aa->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &s5k6aa->pad);
	if (ret)
		return ret;

	ret = s5k6aa_configure_gpios(s5k6aa, pdata);
	if (ret)
		goto out_err;

	for (i = 0; i < S5K6AA_NUM_SUPPLIES; i++)
		s5k6aa->supplies[i].supply = s5k6aa_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev, S5K6AA_NUM_SUPPLIES,
				 s5k6aa->supplies);
	if (ret) {
		dev_err(&client->dev, "Failed to get regulators\n");
		goto out_err;
	}

	ret = s5k6aa_initialize_ctrls(s5k6aa);
	if (ret)
		goto out_err;

	s5k6aa_presets_data_init(s5k6aa);

	s5k6aa->ccd_rect.width = S5K6AA_WIN_WIDTH_MAX;
	s5k6aa->ccd_rect.height	= S5K6AA_WIN_HEIGHT_MAX;
	s5k6aa->ccd_rect.left = 0;
	s5k6aa->ccd_rect.top = 0;

	return 0;

out_err:
	media_entity_cleanup(&s5k6aa->sd.entity);
	return ret;
}

static int s5k6aa_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct i2c_device_id s5k6aa_id[] = {
	{ DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, s5k6aa_id);


static struct i2c_driver s5k6aa_i2c_driver = {
	.driver = {
		.name = DRIVER_NAME
	},
	.probe		= s5k6aa_probe,
	.remove		= s5k6aa_remove,
	.id_table	= s5k6aa_id,
};

module_i2c_driver(s5k6aa_i2c_driver);

MODULE_DESCRIPTION("Samsung S5K6AA(FX) SXGA camera driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");
