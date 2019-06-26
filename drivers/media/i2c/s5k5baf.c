// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Samsung S5K5BAF UXGA 1/5" 2M CMOS Image Sensor
 * with embedded SoC ISP.
 *
 * Copyright (C) 2013, Samsung Electronics Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * Based on S5K6AA driver authored by Sylwester Nawrocki
 * Copyright (C) 2013, Samsung Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-fwnode.h>

static int debug;
module_param(debug, int, 0644);

#define S5K5BAF_DRIVER_NAME		"s5k5baf"
#define S5K5BAF_DEFAULT_MCLK_FREQ	24000000U
#define S5K5BAF_CLK_NAME		"mclk"

#define S5K5BAF_FW_FILENAME		"s5k5baf-cfg.bin"
#define S5K5BAF_FW_TAG			"SF00"
#define S5K5BAG_FW_TAG_LEN		2
#define S5K5BAG_FW_MAX_COUNT		16

#define S5K5BAF_CIS_WIDTH		1600
#define S5K5BAF_CIS_HEIGHT		1200
#define S5K5BAF_WIN_WIDTH_MIN		8
#define S5K5BAF_WIN_HEIGHT_MIN		8
#define S5K5BAF_GAIN_RED_DEF		127
#define S5K5BAF_GAIN_GREEN_DEF		95
#define S5K5BAF_GAIN_BLUE_DEF		180
/* Default number of MIPI CSI-2 data lanes used */
#define S5K5BAF_DEF_NUM_LANES		1

#define AHB_MSB_ADDR_PTR		0xfcfc

/*
 * Register interface pages (the most significant word of the address)
 */
#define PAGE_IF_HW			0xd000
#define PAGE_IF_SW			0x7000

/*
 * H/W register Interface (PAGE_IF_HW)
 */
#define REG_SW_LOAD_COMPLETE		0x0014
#define REG_CMDWR_PAGE			0x0028
#define REG_CMDWR_ADDR			0x002a
#define REG_CMDRD_PAGE			0x002c
#define REG_CMDRD_ADDR			0x002e
#define REG_CMD_BUF			0x0f12
#define REG_SET_HOST_INT		0x1000
#define REG_CLEAR_HOST_INT		0x1030
#define REG_PATTERN_SET			0x3100
#define REG_PATTERN_WIDTH		0x3118
#define REG_PATTERN_HEIGHT		0x311a
#define REG_PATTERN_PARAM		0x311c

/*
 * S/W register interface (PAGE_IF_SW)
 */

/* Firmware revision information */
#define REG_FW_APIVER			0x012e
#define  S5K5BAF_FW_APIVER		0x0001
#define REG_FW_REVISION			0x0130
#define REG_FW_SENSOR_ID		0x0152

/* Initialization parameters */
/* Master clock frequency in KHz */
#define REG_I_INCLK_FREQ_L		0x01b8
#define REG_I_INCLK_FREQ_H		0x01ba
#define  MIN_MCLK_FREQ_KHZ		6000U
#define  MAX_MCLK_FREQ_KHZ		48000U
#define REG_I_USE_NPVI_CLOCKS		0x01c6
#define  NPVI_CLOCKS			1
#define REG_I_USE_NMIPI_CLOCKS		0x01c8
#define  NMIPI_CLOCKS			1
#define REG_I_BLOCK_INTERNAL_PLL_CALC	0x01ca

/* Clock configurations, n = 0..2. REG_I_* frequency unit is 4 kHz. */
#define REG_I_OPCLK_4KHZ(n)		((n) * 6 + 0x01cc)
#define REG_I_MIN_OUTRATE_4KHZ(n)	((n) * 6 + 0x01ce)
#define REG_I_MAX_OUTRATE_4KHZ(n)	((n) * 6 + 0x01d0)
#define  SCLK_PVI_FREQ			24000
#define  SCLK_MIPI_FREQ			48000
#define  PCLK_MIN_FREQ			6000
#define  PCLK_MAX_FREQ			48000
#define REG_I_USE_REGS_API		0x01de
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
#define REG_G_PREVREQ_IN_WIDTH		0x01fa
#define REG_G_PREVREQ_IN_HEIGHT		0x01fc
#define REG_G_PREVREQ_IN_XOFFS		0x01fe
#define REG_G_PREVREQ_IN_YOFFS		0x0200
#define REG_G_PREVZOOM_IN_WIDTH		0x020a
#define REG_G_PREVZOOM_IN_HEIGHT	0x020c
#define REG_G_PREVZOOM_IN_XOFFS		0x020e
#define REG_G_PREVZOOM_IN_YOFFS		0x0210
#define REG_G_INPUTS_CHANGE_REQ		0x021a
#define REG_G_ACTIVE_PREV_CFG		0x021c
#define REG_G_PREV_CFG_CHG		0x021e
#define REG_G_PREV_OPEN_AFTER_CH	0x0220
#define REG_G_PREV_CFG_ERROR		0x0222
#define  CFG_ERROR_RANGE		0x0b
#define REG_G_PREV_CFG_BYPASS_CHANGED	0x022a
#define REG_G_ACTUAL_P_FR_TIME		0x023a
#define REG_G_ACTUAL_P_OUT_RATE		0x023c
#define REG_G_ACTUAL_C_FR_TIME		0x023e
#define REG_G_ACTUAL_C_OUT_RATE		0x0240

/* Preview control section. n = 0...4. */
#define PREG(n, x)			((n) * 0x26 + x)
#define REG_P_OUT_WIDTH(n)		PREG(n, 0x0242)
#define REG_P_OUT_HEIGHT(n)		PREG(n, 0x0244)
#define REG_P_FMT(n)			PREG(n, 0x0246)
#define REG_P_MAX_OUT_RATE(n)		PREG(n, 0x0248)
#define REG_P_MIN_OUT_RATE(n)		PREG(n, 0x024a)
#define REG_P_PVI_MASK(n)		PREG(n, 0x024c)
#define  PVI_MASK_MIPI			0x52
#define REG_P_CLK_INDEX(n)		PREG(n, 0x024e)
#define  CLK_PVI_INDEX			0
#define  CLK_MIPI_INDEX			NPVI_CLOCKS
#define REG_P_FR_RATE_TYPE(n)		PREG(n, 0x0250)
#define  FR_RATE_DYNAMIC		0
#define  FR_RATE_FIXED			1
#define  FR_RATE_FIXED_ACCURATE		2
#define REG_P_FR_RATE_Q_TYPE(n)		PREG(n, 0x0252)
#define  FR_RATE_Q_DYNAMIC		0
#define  FR_RATE_Q_BEST_FRRATE		1 /* Binning enabled */
#define  FR_RATE_Q_BEST_QUALITY		2 /* Binning disabled */
/* Frame period in 0.1 ms units */
#define REG_P_MAX_FR_TIME(n)		PREG(n, 0x0254)
#define REG_P_MIN_FR_TIME(n)		PREG(n, 0x0256)
#define  S5K5BAF_MIN_FR_TIME		333  /* x100 us */
#define  S5K5BAF_MAX_FR_TIME		6500 /* x100 us */
/* The below 5 registers are for "device correction" values */
#define REG_P_SATURATION(n)		PREG(n, 0x0258)
#define REG_P_SHARP_BLUR(n)		PREG(n, 0x025a)
#define REG_P_GLAMOUR(n)		PREG(n, 0x025c)
#define REG_P_COLORTEMP(n)		PREG(n, 0x025e)
#define REG_P_GAMMA_INDEX(n)		PREG(n, 0x0260)
#define REG_P_PREV_MIRROR(n)		PREG(n, 0x0262)
#define REG_P_CAP_MIRROR(n)		PREG(n, 0x0264)
#define REG_P_CAP_ROTATION(n)		PREG(n, 0x0266)

/* Extended image property controls */
/* Exposure time in 10 us units */
#define REG_SF_USR_EXPOSURE_L		0x03bc
#define REG_SF_USR_EXPOSURE_H		0x03be
#define REG_SF_USR_EXPOSURE_CHG		0x03c0
#define REG_SF_USR_TOT_GAIN		0x03c2
#define REG_SF_USR_TOT_GAIN_CHG		0x03c4
#define REG_SF_RGAIN			0x03c6
#define REG_SF_RGAIN_CHG		0x03c8
#define REG_SF_GGAIN			0x03ca
#define REG_SF_GGAIN_CHG		0x03cc
#define REG_SF_BGAIN			0x03ce
#define REG_SF_BGAIN_CHG		0x03d0
#define REG_SF_WBGAIN_CHG		0x03d2
#define REG_SF_FLICKER_QUANT		0x03d4
#define REG_SF_FLICKER_QUANT_CHG	0x03d6

/* Output interface (parallel/MIPI) setup */
#define REG_OIF_EN_MIPI_LANES		0x03f2
#define REG_OIF_EN_PACKETS		0x03f4
#define  EN_PACKETS_CSI2		0xc3
#define REG_OIF_CFG_CHG			0x03f6

/* Auto-algorithms enable mask */
#define REG_DBG_AUTOALG_EN		0x03f8
#define  AALG_ALL_EN			BIT(0)
#define  AALG_AE_EN			BIT(1)
#define  AALG_DIVLEI_EN			BIT(2)
#define  AALG_WB_EN			BIT(3)
#define  AALG_USE_WB_FOR_ISP		BIT(4)
#define  AALG_FLICKER_EN		BIT(5)
#define  AALG_FIT_EN			BIT(6)
#define  AALG_WRHW_EN			BIT(7)

/* Pointers to color correction matrices */
#define REG_PTR_CCM_HORIZON		0x06d0
#define REG_PTR_CCM_INCANDESCENT	0x06d4
#define REG_PTR_CCM_WARM_WHITE		0x06d8
#define REG_PTR_CCM_COOL_WHITE		0x06dc
#define REG_PTR_CCM_DL50		0x06e0
#define REG_PTR_CCM_DL65		0x06e4
#define REG_PTR_CCM_OUTDOOR		0x06ec

#define REG_ARR_CCM(n)			(0x2800 + 36 * (n))

static const char * const s5k5baf_supply_names[] = {
	"vdda",		/* Analog power supply 2.8V (2.6V to 3.0V) */
	"vddreg",	/* Regulator input power supply 1.8V (1.7V to 1.9V)
			   or 2.8V (2.6V to 3.0) */
	"vddio",	/* I/O power supply 1.8V (1.65V to 1.95V)
			   or 2.8V (2.5V to 3.1V) */
};
#define S5K5BAF_NUM_SUPPLIES ARRAY_SIZE(s5k5baf_supply_names)

struct s5k5baf_gpio {
	int gpio;
	int level;
};

enum s5k5baf_gpio_id {
	STBY,
	RST,
	NUM_GPIOS,
};

#define PAD_CIS 0
#define PAD_OUT 1
#define NUM_CIS_PADS 1
#define NUM_ISP_PADS 2

struct s5k5baf_pixfmt {
	u32 code;
	u32 colorspace;
	/* REG_P_FMT(x) register value */
	u16 reg_p_fmt;
};

struct s5k5baf_ctrls {
	struct v4l2_ctrl_handler handler;
	struct { /* Auto / manual white balance cluster */
		struct v4l2_ctrl *awb;
		struct v4l2_ctrl *gain_red;
		struct v4l2_ctrl *gain_blue;
	};
	struct { /* Mirror cluster */
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
	struct { /* Auto exposure / manual exposure and gain cluster */
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	};
};

enum {
	S5K5BAF_FW_ID_PATCH,
	S5K5BAF_FW_ID_CCM,
	S5K5BAF_FW_ID_CIS,
};

struct s5k5baf_fw {
	u16 count;
	struct {
		u16 id;
		u16 offset;
	} seq[0];
	u16 data[0];
};

struct s5k5baf {
	struct s5k5baf_gpio gpios[NUM_GPIOS];
	enum v4l2_mbus_type bus_type;
	u8 nlanes;
	struct regulator_bulk_data supplies[S5K5BAF_NUM_SUPPLIES];

	struct clk *clock;
	u32 mclk_frequency;

	struct s5k5baf_fw *fw;

	struct v4l2_subdev cis_sd;
	struct media_pad cis_pad;

	struct v4l2_subdev sd;
	struct media_pad pads[NUM_ISP_PADS];

	/* protects the struct members below */
	struct mutex lock;

	int error;

	struct v4l2_rect crop_sink;
	struct v4l2_rect compose;
	struct v4l2_rect crop_source;
	/* index to s5k5baf_formats array */
	int pixfmt;
	/* actual frame interval in 100us */
	u16 fiv;
	/* requested frame interval in 100us */
	u16 req_fiv;
	/* cache for REG_DBG_AUTOALG_EN register */
	u16 auto_alg;

	struct s5k5baf_ctrls ctrls;

	unsigned int streaming:1;
	unsigned int apply_cfg:1;
	unsigned int apply_crop:1;
	unsigned int valid_auto_alg:1;
	unsigned int power;
};

static const struct s5k5baf_pixfmt s5k5baf_formats[] = {
	{ MEDIA_BUS_FMT_VYUY8_2X8,	V4L2_COLORSPACE_JPEG,	5 },
	/* range 16-240 */
	{ MEDIA_BUS_FMT_VYUY8_2X8,	V4L2_COLORSPACE_REC709,	6 },
	{ MEDIA_BUS_FMT_RGB565_2X8_BE,	V4L2_COLORSPACE_JPEG,	0 },
};

static struct v4l2_rect s5k5baf_cis_rect = {
	0, 0, S5K5BAF_CIS_WIDTH, S5K5BAF_CIS_HEIGHT
};

/* Setfile contains set of I2C command sequences. Each sequence has its ID.
 * setfile format:
 *	u8 magic[4];
 *	u16 count;		number of sequences
 *	struct {
 *		u16 id;		sequence id
 *		u16 offset;	sequence offset in data array
 *	} seq[count];
 *	u16 data[*];		array containing sequences
 *
 */
static int s5k5baf_fw_parse(struct device *dev, struct s5k5baf_fw **fw,
			    size_t count, const __le16 *data)
{
	struct s5k5baf_fw *f;
	u16 *d, i, *end;
	int ret;

	if (count < S5K5BAG_FW_TAG_LEN + 1) {
		dev_err(dev, "firmware file too short (%zu)\n", count);
		return -EINVAL;
	}

	ret = memcmp(data, S5K5BAF_FW_TAG, S5K5BAG_FW_TAG_LEN * sizeof(u16));
	if (ret != 0) {
		dev_err(dev, "invalid firmware magic number\n");
		return -EINVAL;
	}

	data += S5K5BAG_FW_TAG_LEN;
	count -= S5K5BAG_FW_TAG_LEN;

	d = devm_kcalloc(dev, count, sizeof(u16), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	for (i = 0; i < count; ++i)
		d[i] = le16_to_cpu(data[i]);

	f = (struct s5k5baf_fw *)d;
	if (count < 1 + 2 * f->count) {
		dev_err(dev, "invalid firmware header (count=%d size=%zu)\n",
			f->count, 2 * (count + S5K5BAG_FW_TAG_LEN));
		return -EINVAL;
	}
	end = d + count;
	d += 1 + 2 * f->count;

	for (i = 0; i < f->count; ++i) {
		if (f->seq[i].offset + d <= end)
			continue;
		dev_err(dev, "invalid firmware header (seq=%d)\n", i);
		return -EINVAL;
	}

	*fw = f;

	return 0;
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct s5k5baf, ctrls.handler)->sd;
}

static inline bool s5k5baf_is_cis_subdev(struct v4l2_subdev *sd)
{
	return sd->entity.function == MEDIA_ENT_F_CAM_SENSOR;
}

static inline struct s5k5baf *to_s5k5baf(struct v4l2_subdev *sd)
{
	if (s5k5baf_is_cis_subdev(sd))
		return container_of(sd, struct s5k5baf, cis_sd);
	else
		return container_of(sd, struct s5k5baf, sd);
}

static u16 s5k5baf_i2c_read(struct s5k5baf *state, u16 addr)
{
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	__be16 w, r;
	u16 res;
	struct i2c_msg msg[] = {
		{ .addr = c->addr, .flags = 0,
		  .len = 2, .buf = (u8 *)&w },
		{ .addr = c->addr, .flags = I2C_M_RD,
		  .len = 2, .buf = (u8 *)&r },
	};
	int ret;

	if (state->error)
		return 0;

	w = cpu_to_be16(addr);
	ret = i2c_transfer(c->adapter, msg, 2);
	res = be16_to_cpu(r);

	v4l2_dbg(3, debug, c, "i2c_read: 0x%04x : 0x%04x\n", addr, res);

	if (ret != 2) {
		v4l2_err(c, "i2c_read: error during transfer (%d)\n", ret);
		state->error = ret;
	}
	return res;
}

static void s5k5baf_i2c_write(struct s5k5baf *state, u16 addr, u16 val)
{
	u8 buf[4] = { addr >> 8, addr & 0xFF, val >> 8, val & 0xFF };
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	int ret;

	if (state->error)
		return;

	ret = i2c_master_send(c, buf, 4);
	v4l2_dbg(3, debug, c, "i2c_write: 0x%04x : 0x%04x\n", addr, val);

	if (ret != 4) {
		v4l2_err(c, "i2c_write: error during transfer (%d)\n", ret);
		state->error = ret;
	}
}

static u16 s5k5baf_read(struct s5k5baf *state, u16 addr)
{
	s5k5baf_i2c_write(state, REG_CMDRD_ADDR, addr);
	return s5k5baf_i2c_read(state, REG_CMD_BUF);
}

static void s5k5baf_write(struct s5k5baf *state, u16 addr, u16 val)
{
	s5k5baf_i2c_write(state, REG_CMDWR_ADDR, addr);
	s5k5baf_i2c_write(state, REG_CMD_BUF, val);
}

static void s5k5baf_write_arr_seq(struct s5k5baf *state, u16 addr,
				  u16 count, const u16 *seq)
{
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	__be16 buf[65];

	s5k5baf_i2c_write(state, REG_CMDWR_ADDR, addr);
	if (state->error)
		return;

	v4l2_dbg(3, debug, c, "i2c_write_seq(count=%d): %*ph\n", count,
		 min(2 * count, 64), seq);

	buf[0] = cpu_to_be16(REG_CMD_BUF);

	while (count > 0) {
		int n = min_t(int, count, ARRAY_SIZE(buf) - 1);
		int ret, i;

		for (i = 1; i <= n; ++i)
			buf[i] = cpu_to_be16(*seq++);

		i *= 2;
		ret = i2c_master_send(c, (char *)buf, i);
		if (ret != i) {
			v4l2_err(c, "i2c_write_seq: error during transfer (%d)\n", ret);
			state->error = ret;
			break;
		}

		count -= n;
	}
}

#define s5k5baf_write_seq(state, addr, seq...) \
	s5k5baf_write_arr_seq(state, addr, sizeof((char[]){ seq }), \
			      (const u16 []){ seq });

/* add items count at the beginning of the list */
#define NSEQ(seq...) sizeof((char[]){ seq }), seq

/*
 * s5k5baf_write_nseq() - Writes sequences of values to sensor memory via i2c
 * @nseq: sequence of u16 words in format:
 *	(N, address, value[1]...value[N-1])*,0
 * Ex.:
 *	u16 seq[] = { NSEQ(0x4000, 1, 1), NSEQ(0x4010, 640, 480), 0 };
 *	ret = s5k5baf_write_nseq(c, seq);
 */
static void s5k5baf_write_nseq(struct s5k5baf *state, const u16 *nseq)
{
	int count;

	while ((count = *nseq++)) {
		u16 addr = *nseq++;
		--count;

		s5k5baf_write_arr_seq(state, addr, count, nseq);
		nseq += count;
	}
}

static void s5k5baf_synchronize(struct s5k5baf *state, int timeout, u16 addr)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout);
	u16 reg;

	s5k5baf_write(state, addr, 1);
	do {
		reg = s5k5baf_read(state, addr);
		if (state->error || !reg)
			return;
		usleep_range(5000, 10000);
	} while (time_is_after_jiffies(end));

	v4l2_err(&state->sd, "timeout on register synchronize (%#x)\n", addr);
	state->error = -ETIMEDOUT;
}

static u16 *s5k5baf_fw_get_seq(struct s5k5baf *state, u16 seq_id)
{
	struct s5k5baf_fw *fw = state->fw;
	u16 *data;
	int i;

	if (fw == NULL)
		return NULL;

	data = fw->data + 2 * fw->count;

	for (i = 0; i < fw->count; ++i) {
		if (fw->seq[i].id == seq_id)
			return data + fw->seq[i].offset;
	}

	return NULL;
}

static void s5k5baf_hw_patch(struct s5k5baf *state)
{
	u16 *seq = s5k5baf_fw_get_seq(state, S5K5BAF_FW_ID_PATCH);

	if (seq)
		s5k5baf_write_nseq(state, seq);
}

static void s5k5baf_hw_set_clocks(struct s5k5baf *state)
{
	unsigned long mclk = state->mclk_frequency / 1000;
	u16 status;
	static const u16 nseq_clk_cfg[] = {
		NSEQ(REG_I_USE_NPVI_CLOCKS,
		  NPVI_CLOCKS, NMIPI_CLOCKS, 0,
		  SCLK_PVI_FREQ / 4, PCLK_MIN_FREQ / 4, PCLK_MAX_FREQ / 4,
		  SCLK_MIPI_FREQ / 4, PCLK_MIN_FREQ / 4, PCLK_MAX_FREQ / 4),
		NSEQ(REG_I_USE_REGS_API, 1),
		0
	};

	s5k5baf_write_seq(state, REG_I_INCLK_FREQ_L, mclk & 0xffff, mclk >> 16);
	s5k5baf_write_nseq(state, nseq_clk_cfg);

	s5k5baf_synchronize(state, 250, REG_I_INIT_PARAMS_UPDATED);
	status = s5k5baf_read(state, REG_I_ERROR_INFO);
	if (!state->error && status) {
		v4l2_err(&state->sd, "error configuring PLL (%d)\n", status);
		state->error = -EINVAL;
	}
}

/* set custom color correction matrices for various illuminations */
static void s5k5baf_hw_set_ccm(struct s5k5baf *state)
{
	u16 *seq = s5k5baf_fw_get_seq(state, S5K5BAF_FW_ID_CCM);

	if (seq)
		s5k5baf_write_nseq(state, seq);
}

/* CIS sensor tuning, based on undocumented android driver code */
static void s5k5baf_hw_set_cis(struct s5k5baf *state)
{
	u16 *seq = s5k5baf_fw_get_seq(state, S5K5BAF_FW_ID_CIS);

	if (!seq)
		return;

	s5k5baf_i2c_write(state, REG_CMDWR_PAGE, PAGE_IF_HW);
	s5k5baf_write_nseq(state, seq);
	s5k5baf_i2c_write(state, REG_CMDWR_PAGE, PAGE_IF_SW);
}

static void s5k5baf_hw_sync_cfg(struct s5k5baf *state)
{
	s5k5baf_write(state, REG_G_PREV_CFG_CHG, 1);
	if (state->apply_crop) {
		s5k5baf_write(state, REG_G_INPUTS_CHANGE_REQ, 1);
		s5k5baf_write(state, REG_G_PREV_CFG_BYPASS_CHANGED, 1);
	}
	s5k5baf_synchronize(state, 500, REG_G_NEW_CFG_SYNC);
}
/* Set horizontal and vertical image flipping */
static void s5k5baf_hw_set_mirror(struct s5k5baf *state)
{
	u16 flip = state->ctrls.vflip->val | (state->ctrls.vflip->val << 1);

	s5k5baf_write(state, REG_P_PREV_MIRROR(0), flip);
	if (state->streaming)
		s5k5baf_hw_sync_cfg(state);
}

static void s5k5baf_hw_set_alg(struct s5k5baf *state, u16 alg, bool enable)
{
	u16 cur_alg, new_alg;

	if (!state->valid_auto_alg)
		cur_alg = s5k5baf_read(state, REG_DBG_AUTOALG_EN);
	else
		cur_alg = state->auto_alg;

	new_alg = enable ? (cur_alg | alg) : (cur_alg & ~alg);

	if (new_alg != cur_alg)
		s5k5baf_write(state, REG_DBG_AUTOALG_EN, new_alg);

	if (state->error)
		return;

	state->valid_auto_alg = 1;
	state->auto_alg = new_alg;
}

/* Configure auto/manual white balance and R/G/B gains */
static void s5k5baf_hw_set_awb(struct s5k5baf *state, int awb)
{
	struct s5k5baf_ctrls *ctrls = &state->ctrls;

	if (!awb)
		s5k5baf_write_seq(state, REG_SF_RGAIN,
				  ctrls->gain_red->val, 1,
				  S5K5BAF_GAIN_GREEN_DEF, 1,
				  ctrls->gain_blue->val, 1,
				  1);

	s5k5baf_hw_set_alg(state, AALG_WB_EN, awb);
}

/* Program FW with exposure time, 'exposure' in us units */
static void s5k5baf_hw_set_user_exposure(struct s5k5baf *state, int exposure)
{
	unsigned int time = exposure / 10;

	s5k5baf_write_seq(state, REG_SF_USR_EXPOSURE_L,
			  time & 0xffff, time >> 16, 1);
}

static void s5k5baf_hw_set_user_gain(struct s5k5baf *state, int gain)
{
	s5k5baf_write_seq(state, REG_SF_USR_TOT_GAIN, gain, 1);
}

/* Set auto/manual exposure and total gain */
static void s5k5baf_hw_set_auto_exposure(struct s5k5baf *state, int value)
{
	if (value == V4L2_EXPOSURE_AUTO) {
		s5k5baf_hw_set_alg(state, AALG_AE_EN | AALG_DIVLEI_EN, true);
	} else {
		unsigned int exp_time = state->ctrls.exposure->val;

		s5k5baf_hw_set_user_exposure(state, exp_time);
		s5k5baf_hw_set_user_gain(state, state->ctrls.gain->val);
		s5k5baf_hw_set_alg(state, AALG_AE_EN | AALG_DIVLEI_EN, false);
	}
}

static void s5k5baf_hw_set_anti_flicker(struct s5k5baf *state, int v)
{
	if (v == V4L2_CID_POWER_LINE_FREQUENCY_AUTO) {
		s5k5baf_hw_set_alg(state, AALG_FLICKER_EN, true);
	} else {
		/* The V4L2_CID_LINE_FREQUENCY control values match
		 * the register values */
		s5k5baf_write_seq(state, REG_SF_FLICKER_QUANT, v, 1);
		s5k5baf_hw_set_alg(state, AALG_FLICKER_EN, false);
	}
}

static void s5k5baf_hw_set_colorfx(struct s5k5baf *state, int val)
{
	static const u16 colorfx[] = {
		[V4L2_COLORFX_NONE] = 0,
		[V4L2_COLORFX_BW] = 1,
		[V4L2_COLORFX_NEGATIVE] = 2,
		[V4L2_COLORFX_SEPIA] = 3,
		[V4L2_COLORFX_SKY_BLUE] = 4,
		[V4L2_COLORFX_SKETCH] = 5,
	};

	s5k5baf_write(state, REG_G_SPEC_EFFECTS, colorfx[val]);
}

static int s5k5baf_find_pixfmt(struct v4l2_mbus_framefmt *mf)
{
	int i, c = -1;

	for (i = 0; i < ARRAY_SIZE(s5k5baf_formats); i++) {
		if (mf->colorspace != s5k5baf_formats[i].colorspace)
			continue;
		if (mf->code == s5k5baf_formats[i].code)
			return i;
		if (c < 0)
			c = i;
	}
	return (c < 0) ? 0 : c;
}

static int s5k5baf_clear_error(struct s5k5baf *state)
{
	int ret = state->error;

	state->error = 0;
	return ret;
}

static int s5k5baf_hw_set_video_bus(struct s5k5baf *state)
{
	u16 en_pkts;

	if (state->bus_type == V4L2_MBUS_CSI2_DPHY)
		en_pkts = EN_PACKETS_CSI2;
	else
		en_pkts = 0;

	s5k5baf_write_seq(state, REG_OIF_EN_MIPI_LANES,
			  state->nlanes, en_pkts, 1);

	return s5k5baf_clear_error(state);
}

static u16 s5k5baf_get_cfg_error(struct s5k5baf *state)
{
	u16 err = s5k5baf_read(state, REG_G_PREV_CFG_ERROR);
	if (err)
		s5k5baf_write(state, REG_G_PREV_CFG_ERROR, 0);
	return err;
}

static void s5k5baf_hw_set_fiv(struct s5k5baf *state, u16 fiv)
{
	s5k5baf_write(state, REG_P_MAX_FR_TIME(0), fiv);
	s5k5baf_hw_sync_cfg(state);
}

static void s5k5baf_hw_find_min_fiv(struct s5k5baf *state)
{
	u16 err, fiv;
	int n;

	fiv = s5k5baf_read(state,  REG_G_ACTUAL_P_FR_TIME);
	if (state->error)
		return;

	for (n = 5; n > 0; --n) {
		s5k5baf_hw_set_fiv(state, fiv);
		err = s5k5baf_get_cfg_error(state);
		if (state->error)
			return;
		switch (err) {
		case CFG_ERROR_RANGE:
			++fiv;
			break;
		case 0:
			state->fiv = fiv;
			v4l2_info(&state->sd,
				  "found valid frame interval: %d00us\n", fiv);
			return;
		default:
			v4l2_err(&state->sd,
				 "error setting frame interval: %d\n", err);
			state->error = -EINVAL;
		}
	}
	v4l2_err(&state->sd, "cannot find correct frame interval\n");
	state->error = -ERANGE;
}

static void s5k5baf_hw_validate_cfg(struct s5k5baf *state)
{
	u16 err;

	err = s5k5baf_get_cfg_error(state);
	if (state->error)
		return;

	switch (err) {
	case 0:
		state->apply_cfg = 1;
		return;
	case CFG_ERROR_RANGE:
		s5k5baf_hw_find_min_fiv(state);
		if (!state->error)
			state->apply_cfg = 1;
		return;
	default:
		v4l2_err(&state->sd,
			 "error setting format: %d\n", err);
		state->error = -EINVAL;
	}
}

static void s5k5baf_rescale(struct v4l2_rect *r, const struct v4l2_rect *v,
			    const struct v4l2_rect *n,
			    const struct v4l2_rect *d)
{
	r->left = v->left * n->width / d->width;
	r->top = v->top * n->height / d->height;
	r->width = v->width * n->width / d->width;
	r->height = v->height * n->height / d->height;
}

static int s5k5baf_hw_set_crop_rects(struct s5k5baf *state)
{
	struct v4l2_rect *p, r;
	u16 err;
	int ret;

	p = &state->crop_sink;
	s5k5baf_write_seq(state, REG_G_PREVREQ_IN_WIDTH, p->width, p->height,
			  p->left, p->top);

	s5k5baf_rescale(&r, &state->crop_source, &state->crop_sink,
			&state->compose);
	s5k5baf_write_seq(state, REG_G_PREVZOOM_IN_WIDTH, r.width, r.height,
			  r.left, r.top);

	s5k5baf_synchronize(state, 500, REG_G_INPUTS_CHANGE_REQ);
	s5k5baf_synchronize(state, 500, REG_G_PREV_CFG_BYPASS_CHANGED);
	err = s5k5baf_get_cfg_error(state);
	ret = s5k5baf_clear_error(state);
	if (ret < 0)
		return ret;

	switch (err) {
	case 0:
		break;
	case CFG_ERROR_RANGE:
		/* retry crop with frame interval set to max */
		s5k5baf_hw_set_fiv(state, S5K5BAF_MAX_FR_TIME);
		err = s5k5baf_get_cfg_error(state);
		ret = s5k5baf_clear_error(state);
		if (ret < 0)
			return ret;
		if (err) {
			v4l2_err(&state->sd,
				 "crop error on max frame interval: %d\n", err);
			state->error = -EINVAL;
		}
		s5k5baf_hw_set_fiv(state, state->req_fiv);
		s5k5baf_hw_validate_cfg(state);
		break;
	default:
		v4l2_err(&state->sd, "crop error: %d\n", err);
		return -EINVAL;
	}

	if (!state->apply_cfg)
		return 0;

	p = &state->crop_source;
	s5k5baf_write_seq(state, REG_P_OUT_WIDTH(0), p->width, p->height);
	s5k5baf_hw_set_fiv(state, state->req_fiv);
	s5k5baf_hw_validate_cfg(state);

	return s5k5baf_clear_error(state);
}

static void s5k5baf_hw_set_config(struct s5k5baf *state)
{
	u16 reg_fmt = s5k5baf_formats[state->pixfmt].reg_p_fmt;
	struct v4l2_rect *r = &state->crop_source;

	s5k5baf_write_seq(state, REG_P_OUT_WIDTH(0),
			  r->width, r->height, reg_fmt,
			  PCLK_MAX_FREQ >> 2, PCLK_MIN_FREQ >> 2,
			  PVI_MASK_MIPI, CLK_MIPI_INDEX,
			  FR_RATE_FIXED, FR_RATE_Q_DYNAMIC,
			  state->req_fiv, S5K5BAF_MIN_FR_TIME);
	s5k5baf_hw_sync_cfg(state);
	s5k5baf_hw_validate_cfg(state);
}


static void s5k5baf_hw_set_test_pattern(struct s5k5baf *state, int id)
{
	s5k5baf_i2c_write(state, REG_PATTERN_WIDTH, 800);
	s5k5baf_i2c_write(state, REG_PATTERN_HEIGHT, 511);
	s5k5baf_i2c_write(state, REG_PATTERN_PARAM, 0);
	s5k5baf_i2c_write(state, REG_PATTERN_SET, id);
}

static void s5k5baf_gpio_assert(struct s5k5baf *state, int id)
{
	struct s5k5baf_gpio *gpio = &state->gpios[id];

	gpio_set_value(gpio->gpio, gpio->level);
}

static void s5k5baf_gpio_deassert(struct s5k5baf *state, int id)
{
	struct s5k5baf_gpio *gpio = &state->gpios[id];

	gpio_set_value(gpio->gpio, !gpio->level);
}

static int s5k5baf_power_on(struct s5k5baf *state)
{
	int ret;

	ret = regulator_bulk_enable(S5K5BAF_NUM_SUPPLIES, state->supplies);
	if (ret < 0)
		goto err;

	ret = clk_set_rate(state->clock, state->mclk_frequency);
	if (ret < 0)
		goto err_reg_dis;

	ret = clk_prepare_enable(state->clock);
	if (ret < 0)
		goto err_reg_dis;

	v4l2_dbg(1, debug, &state->sd, "clock frequency: %ld\n",
		 clk_get_rate(state->clock));

	s5k5baf_gpio_deassert(state, STBY);
	usleep_range(50, 100);
	s5k5baf_gpio_deassert(state, RST);
	return 0;

err_reg_dis:
	regulator_bulk_disable(S5K5BAF_NUM_SUPPLIES, state->supplies);
err:
	v4l2_err(&state->sd, "%s() failed (%d)\n", __func__, ret);
	return ret;
}

static int s5k5baf_power_off(struct s5k5baf *state)
{
	int ret;

	state->streaming = 0;
	state->apply_cfg = 0;
	state->apply_crop = 0;

	s5k5baf_gpio_assert(state, RST);
	s5k5baf_gpio_assert(state, STBY);

	if (!IS_ERR(state->clock))
		clk_disable_unprepare(state->clock);

	ret = regulator_bulk_disable(S5K5BAF_NUM_SUPPLIES,
					state->supplies);
	if (ret < 0)
		v4l2_err(&state->sd, "failed to disable regulators\n");

	return 0;
}

static void s5k5baf_hw_init(struct s5k5baf *state)
{
	s5k5baf_i2c_write(state, AHB_MSB_ADDR_PTR, PAGE_IF_HW);
	s5k5baf_i2c_write(state, REG_CLEAR_HOST_INT, 0);
	s5k5baf_i2c_write(state, REG_SW_LOAD_COMPLETE, 1);
	s5k5baf_i2c_write(state, REG_CMDRD_PAGE, PAGE_IF_SW);
	s5k5baf_i2c_write(state, REG_CMDWR_PAGE, PAGE_IF_SW);
}

/*
 * V4L2 subdev core and video operations
 */

static void s5k5baf_initialize_data(struct s5k5baf *state)
{
	state->pixfmt = 0;
	state->req_fiv = 10000 / 15;
	state->fiv = state->req_fiv;
	state->valid_auto_alg = 0;
}

static int s5k5baf_load_setfile(struct s5k5baf *state)
{
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, S5K5BAF_FW_FILENAME, &c->dev);
	if (ret < 0) {
		dev_warn(&c->dev, "firmware file (%s) not loaded\n",
			 S5K5BAF_FW_FILENAME);
		return ret;
	}

	ret = s5k5baf_fw_parse(&c->dev, &state->fw, fw->size / 2,
			       (__le16 *)fw->data);

	release_firmware(fw);

	return ret;
}

static int s5k5baf_set_power(struct v4l2_subdev *sd, int on)
{
	struct s5k5baf *state = to_s5k5baf(sd);
	int ret = 0;

	mutex_lock(&state->lock);

	if (state->power != !on)
		goto out;

	if (on) {
		if (state->fw == NULL)
			s5k5baf_load_setfile(state);

		s5k5baf_initialize_data(state);
		ret = s5k5baf_power_on(state);
		if (ret < 0)
			goto out;

		s5k5baf_hw_init(state);
		s5k5baf_hw_patch(state);
		s5k5baf_i2c_write(state, REG_SET_HOST_INT, 1);
		s5k5baf_hw_set_clocks(state);

		ret = s5k5baf_hw_set_video_bus(state);
		if (ret < 0)
			goto out;

		s5k5baf_hw_set_cis(state);
		s5k5baf_hw_set_ccm(state);

		ret = s5k5baf_clear_error(state);
		if (!ret)
			state->power++;
	} else {
		s5k5baf_power_off(state);
		state->power--;
	}

out:
	mutex_unlock(&state->lock);

	if (!ret && on)
		ret = v4l2_ctrl_handler_setup(&state->ctrls.handler);

	return ret;
}

static void s5k5baf_hw_set_stream(struct s5k5baf *state, int enable)
{
	s5k5baf_write_seq(state, REG_G_ENABLE_PREV, enable, 1);
}

static int s5k5baf_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5k5baf *state = to_s5k5baf(sd);
	int ret;

	mutex_lock(&state->lock);

	if (state->streaming == !!on) {
		ret = 0;
		goto out;
	}

	if (on) {
		s5k5baf_hw_set_config(state);
		ret = s5k5baf_hw_set_crop_rects(state);
		if (ret < 0)
			goto out;
		s5k5baf_hw_set_stream(state, 1);
		s5k5baf_i2c_write(state, 0xb0cc, 0x000b);
	} else {
		s5k5baf_hw_set_stream(state, 0);
	}
	ret = s5k5baf_clear_error(state);
	if (!ret)
		state->streaming = !state->streaming;

out:
	mutex_unlock(&state->lock);

	return ret;
}

static int s5k5baf_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5k5baf *state = to_s5k5baf(sd);

	mutex_lock(&state->lock);
	fi->interval.numerator = state->fiv;
	fi->interval.denominator = 10000;
	mutex_unlock(&state->lock);

	return 0;
}

static void s5k5baf_set_frame_interval(struct s5k5baf *state,
				       struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_fract *i = &fi->interval;

	if (fi->interval.denominator == 0)
		state->req_fiv = S5K5BAF_MAX_FR_TIME;
	else
		state->req_fiv = clamp_t(u32,
					 i->numerator * 10000 / i->denominator,
					 S5K5BAF_MIN_FR_TIME,
					 S5K5BAF_MAX_FR_TIME);

	state->fiv = state->req_fiv;
	if (state->apply_cfg) {
		s5k5baf_hw_set_fiv(state, state->req_fiv);
		s5k5baf_hw_validate_cfg(state);
	}
	*i = (struct v4l2_fract){ state->fiv, 10000 };
	if (state->fiv == state->req_fiv)
		v4l2_info(&state->sd, "frame interval changed to %d00us\n",
			  state->fiv);
}

static int s5k5baf_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5k5baf *state = to_s5k5baf(sd);

	mutex_lock(&state->lock);
	s5k5baf_set_frame_interval(state, fi);
	mutex_unlock(&state->lock);
	return 0;
}

/*
 * V4L2 subdev pad level and video operations
 */
static int s5k5baf_enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index > S5K5BAF_MAX_FR_TIME - S5K5BAF_MIN_FR_TIME ||
	    fie->pad != PAD_CIS)
		return -EINVAL;

	v4l_bound_align_image(&fie->width, S5K5BAF_WIN_WIDTH_MIN,
			      S5K5BAF_CIS_WIDTH, 1,
			      &fie->height, S5K5BAF_WIN_HEIGHT_MIN,
			      S5K5BAF_CIS_HEIGHT, 1, 0);

	fie->interval.numerator = S5K5BAF_MIN_FR_TIME + fie->index;
	fie->interval.denominator = 10000;

	return 0;
}

static int s5k5baf_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad == PAD_CIS) {
		if (code->index > 0)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_FIXED;
		return 0;
	}

	if (code->index >= ARRAY_SIZE(s5k5baf_formats))
		return -EINVAL;

	code->code = s5k5baf_formats[code->index].code;
	return 0;
}

static int s5k5baf_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	int i;

	if (fse->index > 0)
		return -EINVAL;

	if (fse->pad == PAD_CIS) {
		fse->code = MEDIA_BUS_FMT_FIXED;
		fse->min_width = S5K5BAF_CIS_WIDTH;
		fse->max_width = S5K5BAF_CIS_WIDTH;
		fse->min_height = S5K5BAF_CIS_HEIGHT;
		fse->max_height = S5K5BAF_CIS_HEIGHT;
		return 0;
	}

	i = ARRAY_SIZE(s5k5baf_formats);
	while (--i)
		if (fse->code == s5k5baf_formats[i].code)
			break;
	fse->code = s5k5baf_formats[i].code;
	fse->min_width = S5K5BAF_WIN_WIDTH_MIN;
	fse->max_width = S5K5BAF_CIS_WIDTH;
	fse->max_height = S5K5BAF_WIN_HEIGHT_MIN;
	fse->min_height = S5K5BAF_CIS_HEIGHT;

	return 0;
}

static void s5k5baf_try_cis_format(struct v4l2_mbus_framefmt *mf)
{
	mf->width = S5K5BAF_CIS_WIDTH;
	mf->height = S5K5BAF_CIS_HEIGHT;
	mf->code = MEDIA_BUS_FMT_FIXED;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field = V4L2_FIELD_NONE;
}

static int s5k5baf_try_isp_format(struct v4l2_mbus_framefmt *mf)
{
	int pixfmt;

	v4l_bound_align_image(&mf->width, S5K5BAF_WIN_WIDTH_MIN,
			      S5K5BAF_CIS_WIDTH, 1,
			      &mf->height, S5K5BAF_WIN_HEIGHT_MIN,
			      S5K5BAF_CIS_HEIGHT, 1, 0);

	pixfmt = s5k5baf_find_pixfmt(mf);

	mf->colorspace = s5k5baf_formats[pixfmt].colorspace;
	mf->code = s5k5baf_formats[pixfmt].code;
	mf->field = V4L2_FIELD_NONE;

	return pixfmt;
}

static int s5k5baf_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k5baf *state = to_s5k5baf(sd);
	const struct s5k5baf_pixfmt *pixfmt;
	struct v4l2_mbus_framefmt *mf;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		fmt->format = *mf;
		return 0;
	}

	mf = &fmt->format;
	if (fmt->pad == PAD_CIS) {
		s5k5baf_try_cis_format(mf);
		return 0;
	}
	mf->field = V4L2_FIELD_NONE;
	mutex_lock(&state->lock);
	pixfmt = &s5k5baf_formats[state->pixfmt];
	mf->width = state->crop_source.width;
	mf->height = state->crop_source.height;
	mf->code = pixfmt->code;
	mf->colorspace = pixfmt->colorspace;
	mutex_unlock(&state->lock);

	return 0;
}

static int s5k5baf_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct s5k5baf *state = to_s5k5baf(sd);
	const struct s5k5baf_pixfmt *pixfmt;
	int ret = 0;

	mf->field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = *mf;
		return 0;
	}

	if (fmt->pad == PAD_CIS) {
		s5k5baf_try_cis_format(mf);
		return 0;
	}

	mutex_lock(&state->lock);

	if (state->streaming) {
		mutex_unlock(&state->lock);
		return -EBUSY;
	}

	state->pixfmt = s5k5baf_try_isp_format(mf);
	pixfmt = &s5k5baf_formats[state->pixfmt];
	mf->code = pixfmt->code;
	mf->colorspace = pixfmt->colorspace;
	mf->width = state->crop_source.width;
	mf->height = state->crop_source.height;

	mutex_unlock(&state->lock);
	return ret;
}

enum selection_rect { R_CIS, R_CROP_SINK, R_COMPOSE, R_CROP_SOURCE, R_INVALID };

static enum selection_rect s5k5baf_get_sel_rect(u32 pad, u32 target)
{
	switch (target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		return pad ? R_COMPOSE : R_CIS;
	case V4L2_SEL_TGT_CROP:
		return pad ? R_CROP_SOURCE : R_CROP_SINK;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		return pad ? R_INVALID : R_CROP_SINK;
	case V4L2_SEL_TGT_COMPOSE:
		return pad ? R_INVALID : R_COMPOSE;
	default:
		return R_INVALID;
	}
}

static int s5k5baf_is_bound_target(u32 target)
{
	return target == V4L2_SEL_TGT_CROP_BOUNDS ||
		target == V4L2_SEL_TGT_COMPOSE_BOUNDS;
}

static int s5k5baf_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	enum selection_rect rtype;
	struct s5k5baf *state = to_s5k5baf(sd);

	rtype = s5k5baf_get_sel_rect(sel->pad, sel->target);

	switch (rtype) {
	case R_INVALID:
		return -EINVAL;
	case R_CIS:
		sel->r = s5k5baf_cis_rect;
		return 0;
	default:
		break;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (rtype == R_COMPOSE)
			sel->r = *v4l2_subdev_get_try_compose(sd, cfg, sel->pad);
		else
			sel->r = *v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
		return 0;
	}

	mutex_lock(&state->lock);
	switch (rtype) {
	case R_CROP_SINK:
		sel->r = state->crop_sink;
		break;
	case R_COMPOSE:
		sel->r = state->compose;
		break;
	case R_CROP_SOURCE:
		sel->r = state->crop_source;
		break;
	default:
		break;
	}
	if (s5k5baf_is_bound_target(sel->target)) {
		sel->r.left = 0;
		sel->r.top = 0;
	}
	mutex_unlock(&state->lock);

	return 0;
}

/* bounds range [start, start+len) to [0, max) and aligns to 2 */
static void s5k5baf_bound_range(u32 *start, u32 *len, u32 max)
{
	if (*len > max)
		*len = max;
	if (*start + *len > max)
		*start = max - *len;
	*start &= ~1;
	*len &= ~1;
	if (*len < S5K5BAF_WIN_WIDTH_MIN)
		*len = S5K5BAF_WIN_WIDTH_MIN;
}

static void s5k5baf_bound_rect(struct v4l2_rect *r, u32 width, u32 height)
{
	s5k5baf_bound_range(&r->left, &r->width, width);
	s5k5baf_bound_range(&r->top, &r->height, height);
}

static void s5k5baf_set_rect_and_adjust(struct v4l2_rect **rects,
					enum selection_rect first,
					struct v4l2_rect *v)
{
	struct v4l2_rect *r, *br;
	enum selection_rect i = first;

	*rects[first] = *v;
	do {
		r = rects[i];
		br = rects[i - 1];
		s5k5baf_bound_rect(r, br->width, br->height);
	} while (++i != R_INVALID);
	*v = *rects[first];
}

static bool s5k5baf_cmp_rect(const struct v4l2_rect *r1,
			     const struct v4l2_rect *r2)
{
	return !memcmp(r1, r2, sizeof(*r1));
}

static int s5k5baf_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	static enum selection_rect rtype;
	struct s5k5baf *state = to_s5k5baf(sd);
	struct v4l2_rect **rects;
	int ret = 0;

	rtype = s5k5baf_get_sel_rect(sel->pad, sel->target);
	if (rtype == R_INVALID || s5k5baf_is_bound_target(sel->target))
		return -EINVAL;

	/* allow only scaling on compose */
	if (rtype == R_COMPOSE) {
		sel->r.left = 0;
		sel->r.top = 0;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		rects = (struct v4l2_rect * []) {
				&s5k5baf_cis_rect,
				v4l2_subdev_get_try_crop(sd, cfg, PAD_CIS),
				v4l2_subdev_get_try_compose(sd, cfg, PAD_CIS),
				v4l2_subdev_get_try_crop(sd, cfg, PAD_OUT)
			};
		s5k5baf_set_rect_and_adjust(rects, rtype, &sel->r);
		return 0;
	}

	rects = (struct v4l2_rect * []) {
			&s5k5baf_cis_rect,
			&state->crop_sink,
			&state->compose,
			&state->crop_source
		};
	mutex_lock(&state->lock);
	if (state->streaming) {
		/* adjust sel->r to avoid output resolution change */
		if (rtype < R_CROP_SOURCE) {
			if (sel->r.width < state->crop_source.width)
				sel->r.width = state->crop_source.width;
			if (sel->r.height < state->crop_source.height)
				sel->r.height = state->crop_source.height;
		} else {
			sel->r.width = state->crop_source.width;
			sel->r.height = state->crop_source.height;
		}
	}
	s5k5baf_set_rect_and_adjust(rects, rtype, &sel->r);
	if (!s5k5baf_cmp_rect(&state->crop_sink, &s5k5baf_cis_rect) ||
	    !s5k5baf_cmp_rect(&state->compose, &s5k5baf_cis_rect))
		state->apply_crop = 1;
	if (state->streaming)
		ret = s5k5baf_hw_set_crop_rects(state);
	mutex_unlock(&state->lock);

	return ret;
}

static const struct v4l2_subdev_pad_ops s5k5baf_cis_pad_ops = {
	.enum_mbus_code		= s5k5baf_enum_mbus_code,
	.enum_frame_size	= s5k5baf_enum_frame_size,
	.get_fmt		= s5k5baf_get_fmt,
	.set_fmt		= s5k5baf_set_fmt,
};

static const struct v4l2_subdev_pad_ops s5k5baf_pad_ops = {
	.enum_mbus_code		= s5k5baf_enum_mbus_code,
	.enum_frame_size	= s5k5baf_enum_frame_size,
	.enum_frame_interval	= s5k5baf_enum_frame_interval,
	.get_fmt		= s5k5baf_get_fmt,
	.set_fmt		= s5k5baf_set_fmt,
	.get_selection		= s5k5baf_get_selection,
	.set_selection		= s5k5baf_set_selection,
};

static const struct v4l2_subdev_video_ops s5k5baf_video_ops = {
	.g_frame_interval	= s5k5baf_g_frame_interval,
	.s_frame_interval	= s5k5baf_s_frame_interval,
	.s_stream		= s5k5baf_s_stream,
};

/*
 * V4L2 subdev controls
 */

static int s5k5baf_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct s5k5baf *state = to_s5k5baf(sd);
	int ret;

	v4l2_dbg(1, debug, sd, "ctrl: %s, value: %d\n", ctrl->name, ctrl->val);

	mutex_lock(&state->lock);

	if (state->power == 0)
		goto unlock;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		s5k5baf_hw_set_awb(state, ctrl->val);
		break;

	case V4L2_CID_BRIGHTNESS:
		s5k5baf_write(state, REG_USER_BRIGHTNESS, ctrl->val);
		break;

	case V4L2_CID_COLORFX:
		s5k5baf_hw_set_colorfx(state, ctrl->val);
		break;

	case V4L2_CID_CONTRAST:
		s5k5baf_write(state, REG_USER_CONTRAST, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		s5k5baf_hw_set_auto_exposure(state, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		s5k5baf_hw_set_mirror(state);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		s5k5baf_hw_set_anti_flicker(state, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		s5k5baf_write(state, REG_USER_SATURATION, ctrl->val);
		break;

	case V4L2_CID_SHARPNESS:
		s5k5baf_write(state, REG_USER_SHARPBLUR, ctrl->val);
		break;

	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		s5k5baf_write(state, REG_P_COLORTEMP(0), ctrl->val);
		if (state->apply_cfg)
			s5k5baf_hw_sync_cfg(state);
		break;

	case V4L2_CID_TEST_PATTERN:
		s5k5baf_hw_set_test_pattern(state, ctrl->val);
		break;
	}
unlock:
	ret = s5k5baf_clear_error(state);
	mutex_unlock(&state->lock);
	return ret;
}

static const struct v4l2_ctrl_ops s5k5baf_ctrl_ops = {
	.s_ctrl	= s5k5baf_s_ctrl,
};

static const char * const s5k5baf_test_pattern_menu[] = {
	"Disabled",
	"Blank",
	"Bars",
	"Gradients",
	"Textile",
	"Textile2",
	"Squares"
};

static int s5k5baf_initialize_ctrls(struct s5k5baf *state)
{
	const struct v4l2_ctrl_ops *ops = &s5k5baf_ctrl_ops;
	struct s5k5baf_ctrls *ctrls = &state->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 16);
	if (ret < 0) {
		v4l2_err(&state->sd, "cannot init ctrl handler (%d)\n", ret);
		return ret;
	}

	/* Auto white balance cluster */
	ctrls->awb = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_AUTO_WHITE_BALANCE,
				       0, 1, 1, 1);
	ctrls->gain_red = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_RED_BALANCE,
					    0, 255, 1, S5K5BAF_GAIN_RED_DEF);
	ctrls->gain_blue = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BLUE_BALANCE,
					     0, 255, 1, S5K5BAF_GAIN_BLUE_DEF);
	v4l2_ctrl_auto_cluster(3, &ctrls->awb, 0, false);

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

	v4l2_ctrl_new_std_menu_items(hdl, ops, V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(s5k5baf_test_pattern_menu) - 1,
				     0, 0, s5k5baf_test_pattern_menu);

	if (hdl->error) {
		v4l2_err(&state->sd, "error creating controls (%d)\n",
			 hdl->error);
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	state->sd.ctrl_handler = hdl;
	return 0;
}

/*
 * V4L2 subdev internal operations
 */
static int s5k5baf_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, fh->pad, PAD_CIS);
	s5k5baf_try_cis_format(mf);

	if (s5k5baf_is_cis_subdev(sd))
		return 0;

	mf = v4l2_subdev_get_try_format(sd, fh->pad, PAD_OUT);
	mf->colorspace = s5k5baf_formats[0].colorspace;
	mf->code = s5k5baf_formats[0].code;
	mf->width = s5k5baf_cis_rect.width;
	mf->height = s5k5baf_cis_rect.height;
	mf->field = V4L2_FIELD_NONE;

	*v4l2_subdev_get_try_crop(sd, fh->pad, PAD_CIS) = s5k5baf_cis_rect;
	*v4l2_subdev_get_try_compose(sd, fh->pad, PAD_CIS) = s5k5baf_cis_rect;
	*v4l2_subdev_get_try_crop(sd, fh->pad, PAD_OUT) = s5k5baf_cis_rect;

	return 0;
}

static int s5k5baf_check_fw_revision(struct s5k5baf *state)
{
	u16 api_ver = 0, fw_rev = 0, s_id = 0;
	int ret;

	api_ver = s5k5baf_read(state, REG_FW_APIVER);
	fw_rev = s5k5baf_read(state, REG_FW_REVISION) & 0xff;
	s_id = s5k5baf_read(state, REG_FW_SENSOR_ID);
	ret = s5k5baf_clear_error(state);
	if (ret < 0)
		return ret;

	v4l2_info(&state->sd, "FW API=%#x, revision=%#x sensor_id=%#x\n",
		  api_ver, fw_rev, s_id);

	if (api_ver != S5K5BAF_FW_APIVER) {
		v4l2_err(&state->sd, "FW API version not supported\n");
		return -ENODEV;
	}

	return 0;
}

static int s5k5baf_registered(struct v4l2_subdev *sd)
{
	struct s5k5baf *state = to_s5k5baf(sd);
	int ret;

	ret = v4l2_device_register_subdev(sd->v4l2_dev, &state->cis_sd);
	if (ret < 0)
		v4l2_err(sd, "failed to register subdev %s\n",
			 state->cis_sd.name);
	else
		ret = media_create_pad_link(&state->cis_sd.entity, PAD_CIS,
					       &state->sd.entity, PAD_CIS,
					       MEDIA_LNK_FL_IMMUTABLE |
					       MEDIA_LNK_FL_ENABLED);
	return ret;
}

static void s5k5baf_unregistered(struct v4l2_subdev *sd)
{
	struct s5k5baf *state = to_s5k5baf(sd);
	v4l2_device_unregister_subdev(&state->cis_sd);
}

static const struct v4l2_subdev_ops s5k5baf_cis_subdev_ops = {
	.pad	= &s5k5baf_cis_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k5baf_cis_subdev_internal_ops = {
	.open = s5k5baf_open,
};

static const struct v4l2_subdev_internal_ops s5k5baf_subdev_internal_ops = {
	.registered = s5k5baf_registered,
	.unregistered = s5k5baf_unregistered,
	.open = s5k5baf_open,
};

static const struct v4l2_subdev_core_ops s5k5baf_core_ops = {
	.s_power = s5k5baf_set_power,
	.log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_ops s5k5baf_subdev_ops = {
	.core = &s5k5baf_core_ops,
	.pad = &s5k5baf_pad_ops,
	.video = &s5k5baf_video_ops,
};

static int s5k5baf_configure_gpios(struct s5k5baf *state)
{
	static const char * const name[] = { "S5K5BAF_STBY", "S5K5BAF_RST" };
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	struct s5k5baf_gpio *g = state->gpios;
	int ret, i;

	for (i = 0; i < NUM_GPIOS; ++i) {
		int flags = GPIOF_DIR_OUT;
		if (g[i].level)
			flags |= GPIOF_INIT_HIGH;
		ret = devm_gpio_request_one(&c->dev, g[i].gpio, flags, name[i]);
		if (ret < 0) {
			v4l2_err(c, "failed to request gpio %s\n", name[i]);
			return ret;
		}
	}
	return 0;
}

static int s5k5baf_parse_gpios(struct s5k5baf_gpio *gpios, struct device *dev)
{
	static const char * const names[] = {
		"stbyn-gpios",
		"rstn-gpios",
	};
	struct device_node *node = dev->of_node;
	enum of_gpio_flags flags;
	int ret, i;

	for (i = 0; i < NUM_GPIOS; ++i) {
		ret = of_get_named_gpio_flags(node, names[i], 0, &flags);
		if (ret < 0) {
			dev_err(dev, "no %s GPIO pin provided\n", names[i]);
			return ret;
		}
		gpios[i].gpio = ret;
		gpios[i].level = !(flags & OF_GPIO_ACTIVE_LOW);
	}

	return 0;
}

static int s5k5baf_parse_device_node(struct s5k5baf *state, struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct device_node *node_ep;
	struct v4l2_fwnode_endpoint ep = { .bus_type = 0 };
	int ret;

	if (!node) {
		dev_err(dev, "no device-tree node provided\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "clock-frequency",
				   &state->mclk_frequency);
	if (ret < 0) {
		state->mclk_frequency = S5K5BAF_DEFAULT_MCLK_FREQ;
		dev_info(dev, "using default %u Hz clock frequency\n",
			 state->mclk_frequency);
	}

	ret = s5k5baf_parse_gpios(state->gpios, dev);
	if (ret < 0)
		return ret;

	node_ep = of_graph_get_next_endpoint(node, NULL);
	if (!node_ep) {
		dev_err(dev, "no endpoint defined at node %pOF\n", node);
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(node_ep), &ep);
	of_node_put(node_ep);
	if (ret)
		return ret;

	state->bus_type = ep.bus_type;

	switch (state->bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
		state->nlanes = ep.bus.mipi_csi2.num_data_lanes;
		break;
	case V4L2_MBUS_PARALLEL:
		break;
	default:
		dev_err(dev, "unsupported bus in endpoint defined at node %pOF\n",
			node);
		return -EINVAL;
	}

	return 0;
}

static int s5k5baf_configure_subdevs(struct s5k5baf *state,
				     struct i2c_client *c)
{
	struct v4l2_subdev *sd;
	int ret;

	sd = &state->cis_sd;
	v4l2_subdev_init(sd, &s5k5baf_cis_subdev_ops);
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, state);
	snprintf(sd->name, sizeof(sd->name), "S5K5BAF-CIS %d-%04x",
		 i2c_adapter_id(c->adapter), c->addr);

	sd->internal_ops = &s5k5baf_cis_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->cis_pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, NUM_CIS_PADS, &state->cis_pad);
	if (ret < 0)
		goto err;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, c, &s5k5baf_subdev_ops);
	snprintf(sd->name, sizeof(sd->name), "S5K5BAF-ISP %d-%04x",
		 i2c_adapter_id(c->adapter), c->addr);

	sd->internal_ops = &s5k5baf_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->pads[PAD_CIS].flags = MEDIA_PAD_FL_SINK;
	state->pads[PAD_OUT].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
	ret = media_entity_pads_init(&sd->entity, NUM_ISP_PADS, state->pads);

	if (!ret)
		return 0;

	media_entity_cleanup(&state->cis_sd.entity);
err:
	dev_err(&c->dev, "cannot init media entity %s\n", sd->name);
	return ret;
}

static int s5k5baf_configure_regulators(struct s5k5baf *state)
{
	struct i2c_client *c = v4l2_get_subdevdata(&state->sd);
	int ret;
	int i;

	for (i = 0; i < S5K5BAF_NUM_SUPPLIES; i++)
		state->supplies[i].supply = s5k5baf_supply_names[i];

	ret = devm_regulator_bulk_get(&c->dev, S5K5BAF_NUM_SUPPLIES,
				      state->supplies);
	if (ret < 0)
		v4l2_err(c, "failed to get regulators\n");
	return ret;
}

static int s5k5baf_probe(struct i2c_client *c,
			const struct i2c_device_id *id)
{
	struct s5k5baf *state;
	int ret;

	state = devm_kzalloc(&c->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);
	state->crop_sink = s5k5baf_cis_rect;
	state->compose = s5k5baf_cis_rect;
	state->crop_source = s5k5baf_cis_rect;

	ret = s5k5baf_parse_device_node(state, &c->dev);
	if (ret < 0)
		return ret;

	ret = s5k5baf_configure_subdevs(state, c);
	if (ret < 0)
		return ret;

	ret = s5k5baf_configure_gpios(state);
	if (ret < 0)
		goto err_me;

	ret = s5k5baf_configure_regulators(state);
	if (ret < 0)
		goto err_me;

	state->clock = devm_clk_get(state->sd.dev, S5K5BAF_CLK_NAME);
	if (IS_ERR(state->clock)) {
		ret = -EPROBE_DEFER;
		goto err_me;
	}

	ret = s5k5baf_power_on(state);
	if (ret < 0) {
		ret = -EPROBE_DEFER;
		goto err_me;
	}
	s5k5baf_hw_init(state);
	ret = s5k5baf_check_fw_revision(state);

	s5k5baf_power_off(state);
	if (ret < 0)
		goto err_me;

	ret = s5k5baf_initialize_ctrls(state);
	if (ret < 0)
		goto err_me;

	ret = v4l2_async_register_subdev(&state->sd);
	if (ret < 0)
		goto err_ctrl;

	return 0;

err_ctrl:
	v4l2_ctrl_handler_free(state->sd.ctrl_handler);
err_me:
	media_entity_cleanup(&state->sd.entity);
	media_entity_cleanup(&state->cis_sd.entity);
	return ret;
}

static int s5k5baf_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);
	struct s5k5baf *state = to_s5k5baf(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	media_entity_cleanup(&sd->entity);

	sd = &state->cis_sd;
	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct i2c_device_id s5k5baf_id[] = {
	{ S5K5BAF_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, s5k5baf_id);

static const struct of_device_id s5k5baf_of_match[] = {
	{ .compatible = "samsung,s5k5baf" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5k5baf_of_match);

static struct i2c_driver s5k5baf_i2c_driver = {
	.driver = {
		.of_match_table = s5k5baf_of_match,
		.name = S5K5BAF_DRIVER_NAME
	},
	.probe		= s5k5baf_probe,
	.remove		= s5k5baf_remove,
	.id_table	= s5k5baf_id,
};

module_i2c_driver(s5k5baf_i2c_driver);

MODULE_DESCRIPTION("Samsung S5K5BAF(X) UXGA camera driver");
MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_LICENSE("GPL v2");
