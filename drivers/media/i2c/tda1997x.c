// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Gateworks Corporation
 */
#include <linux/delay.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/i2c/tda1997x.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <dt-bindings/media/tda1997x.h>

#include "tda1997x_regs.h"

#define TDA1997X_MBUS_CODES	5

/* debug level */
static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

/* Audio formats */
static const char * const audtype_names[] = {
	"PCM",			/* PCM Samples */
	"HBR",			/* High Bit Rate Audio */
	"OBA",			/* One-Bit Audio */
	"DST"			/* Direct Stream Transfer */
};

/* Audio output port formats */
enum audfmt_types {
	AUDFMT_TYPE_DISABLED = 0,
	AUDFMT_TYPE_I2S,
	AUDFMT_TYPE_SPDIF,
};
static const char * const audfmt_names[] = {
	"Disabled",
	"I2S",
	"SPDIF",
};

/* Video input formats */
static const char * const hdmi_colorspace_names[] = {
	"RGB", "YUV422", "YUV444", "YUV420", "", "", "", "",
};
static const char * const hdmi_colorimetry_names[] = {
	"", "ITU601", "ITU709", "Extended",
};
static const char * const v4l2_quantization_names[] = {
	"Default",
	"Full Range (0-255)",
	"Limited Range (16-235)",
};

/* Video output port formats */
static const char * const vidfmt_names[] = {
	"RGB444/YUV444",	/* RGB/YUV444 16bit data bus, 8bpp */
	"YUV422 semi-planar",	/* YUV422 16bit data base, 8bpp */
	"YUV422 CCIR656",	/* BT656 (YUV 8bpp 2 clock per pixel) */
	"Invalid",
};

/*
 * Colorspace conversion matrices
 */
struct color_matrix_coefs {
	const char *name;
	/* Input offsets */
	s16 offint1;
	s16 offint2;
	s16 offint3;
	/* Coeficients */
	s16 p11coef;
	s16 p12coef;
	s16 p13coef;
	s16 p21coef;
	s16 p22coef;
	s16 p23coef;
	s16 p31coef;
	s16 p32coef;
	s16 p33coef;
	/* Output offsets */
	s16 offout1;
	s16 offout2;
	s16 offout3;
};

enum {
	ITU709_RGBFULL,
	ITU601_RGBFULL,
	RGBLIMITED_RGBFULL,
	RGBLIMITED_ITU601,
	RGBLIMITED_ITU709,
	RGBFULL_ITU601,
	RGBFULL_ITU709,
};

/* NB: 4096 is 1.0 using fixed point numbers */
static const struct color_matrix_coefs conv_matrix[] = {
	{
		"YUV709 -> RGB full",
		 -256, -2048,  -2048,
		 4769, -2183,   -873,
		 4769,  7343,      0,
		 4769,     0,   8652,
		    0,     0,      0,
	},
	{
		"YUV601 -> RGB full",
		 -256, -2048,  -2048,
		 4769, -3330,  -1602,
		 4769,  6538,      0,
		 4769,     0,   8264,
		  256,   256,    256,
	},
	{
		"RGB limited -> RGB full",
		 -256,  -256,   -256,
		    0,  4769,      0,
		    0,     0,   4769,
		 4769,     0,      0,
		    0,     0,      0,
	},
	{
		"RGB limited -> ITU601",
		 -256,  -256,   -256,
		 2404,  1225,    467,
		-1754,  2095,   -341,
		-1388,  -707,   2095,
		  256,  2048,   2048,
	},
	{
		"RGB limited -> ITU709",
		 -256,  -256,   -256,
		 2918,   867,    295,
		-1894,  2087,   -190,
		-1607,  -477,   2087,
		  256,  2048,   2048,
	},
	{
		"RGB full -> ITU601",
		    0,     0,      0,
		 2065,  1052,    401,
		-1506,  1799,   -293,
		-1192,  -607,   1799,
		  256,  2048,   2048,
	},
	{
		"RGB full -> ITU709",
		    0,     0,      0,
		 2506,   745,    253,
		-1627,  1792,   -163,
		-1380,  -410,   1792,
		  256,  2048,   2048,
	},
};

static const struct v4l2_dv_timings_cap tda1997x_dv_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },

	V4L2_INIT_BT_TIMINGS(
		640, 1920,			/* min/max width */
		350, 1200,			/* min/max height */
		13000000, 165000000,		/* min/max pixelclock */
		/* standards */
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		/* capabilities */
		V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM
	)
};

/* regulator supplies */
static const char * const tda1997x_supply_name[] = {
	"DOVDD", /* Digital I/O supply */
	"DVDD",  /* Digital Core supply */
	"AVDD",  /* Analog supply */
};

#define TDA1997X_NUM_SUPPLIES ARRAY_SIZE(tda1997x_supply_name)

enum tda1997x_type {
	TDA19971,
	TDA19973,
};

enum tda1997x_hdmi_pads {
	TDA1997X_PAD_SOURCE,
	TDA1997X_NUM_PADS,
};

struct tda1997x_chip_info {
	enum tda1997x_type type;
	const char *name;
};

struct tda1997x_state {
	const struct tda1997x_chip_info *info;
	struct tda1997x_platform_data pdata;
	struct i2c_client *client;
	struct i2c_client *client_cec;
	struct v4l2_subdev sd;
	struct regulator_bulk_data supplies[TDA1997X_NUM_SUPPLIES];
	struct media_pad pads[TDA1997X_NUM_PADS];
	struct mutex lock;
	struct mutex page_lock;
	char page;

	/* detected info from chip */
	int chip_revision;
	char port_30bit;
	char output_2p5;
	char tmdsb_clk;
	char tmdsb_soc;

	/* status info */
	char hdmi_status;
	char mptrw_in_progress;
	char activity_status;
	char input_detect[2];

	/* video */
	struct hdmi_avi_infoframe avi_infoframe;
	struct v4l2_hdmi_colorimetry colorimetry;
	u32 rgb_quantization_range;
	struct v4l2_dv_timings timings;
	int fps;
	const struct color_matrix_coefs *conv;
	u32 mbus_codes[TDA1997X_MBUS_CODES];	/* available modes */
	u32 mbus_code;		/* current mode */
	u8 vid_fmt;

	/* controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *rgb_quantization_range_ctrl;

	/* audio */
	u8  audio_ch_alloc;
	int audio_samplerate;
	int audio_channels;
	int audio_samplesize;
	int audio_type;
	struct mutex audio_lock;
	struct snd_pcm_substream *audio_stream;

	/* EDID */
	struct {
		u8 edid[256];
		u32 present;
		unsigned int blocks;
	} edid;
	struct delayed_work delayed_work_enable_hpd;
};

static const struct v4l2_event tda1997x_ev_fmt = {
	.type = V4L2_EVENT_SOURCE_CHANGE,
	.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
};

static const struct tda1997x_chip_info tda1997x_chip_info[] = {
	[TDA19971] = {
		.type = TDA19971,
		.name = "tda19971",
	},
	[TDA19973] = {
		.type = TDA19973,
		.name = "tda19973",
	},
};

static inline struct tda1997x_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tda1997x_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct tda1997x_state, hdl)->sd;
}

static int tda1997x_cec_read(struct v4l2_subdev *sd, u8 reg)
{
	struct tda1997x_state *state = to_state(sd);
	int val;

	val = i2c_smbus_read_byte_data(state->client_cec, reg);
	if (val < 0) {
		v4l_err(state->client, "read reg error: reg=%2x\n", reg);
		val = -1;
	}

	return val;
}

static int tda1997x_cec_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct tda1997x_state *state = to_state(sd);
	int ret = 0;

	ret = i2c_smbus_write_byte_data(state->client_cec, reg, val);
	if (ret < 0) {
		v4l_err(state->client, "write reg error:reg=%2x,val=%2x\n",
			reg, val);
		ret = -1;
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * I2C transfer
 */

static int tda1997x_setpage(struct v4l2_subdev *sd, u8 page)
{
	struct tda1997x_state *state = to_state(sd);
	int ret;

	if (state->page != page) {
		ret = i2c_smbus_write_byte_data(state->client,
			REG_CURPAGE_00H, page);
		if (ret < 0) {
			v4l_err(state->client,
				"write reg error:reg=%2x,val=%2x\n",
				REG_CURPAGE_00H, page);
			return ret;
		}
		state->page = page;
	}
	return 0;
}

static inline int io_read(struct v4l2_subdev *sd, u16 reg)
{
	struct tda1997x_state *state = to_state(sd);
	int val;

	mutex_lock(&state->page_lock);
	if (tda1997x_setpage(sd, reg >> 8)) {
		val = -1;
		goto out;
	}

	val = i2c_smbus_read_byte_data(state->client, reg&0xff);
	if (val < 0) {
		v4l_err(state->client, "read reg error: reg=%2x\n", reg & 0xff);
		val = -1;
		goto out;
	}

out:
	mutex_unlock(&state->page_lock);
	return val;
}

static inline long io_read16(struct v4l2_subdev *sd, u16 reg)
{
	int val;
	long lval = 0;

	val = io_read(sd, reg);
	if (val < 0)
		return val;
	lval |= (val << 8);
	val = io_read(sd, reg + 1);
	if (val < 0)
		return val;
	lval |= val;

	return lval;
}

static inline long io_read24(struct v4l2_subdev *sd, u16 reg)
{
	int val;
	long lval = 0;

	val = io_read(sd, reg);
	if (val < 0)
		return val;
	lval |= (val << 16);
	val = io_read(sd, reg + 1);
	if (val < 0)
		return val;
	lval |= (val << 8);
	val = io_read(sd, reg + 2);
	if (val < 0)
		return val;
	lval |= val;

	return lval;
}

static unsigned int io_readn(struct v4l2_subdev *sd, u16 reg, u8 len, u8 *data)
{
	int i;
	int sz = 0;
	int val;

	for (i = 0; i < len; i++) {
		val = io_read(sd, reg + i);
		if (val < 0)
			break;
		data[i] = val;
		sz++;
	}

	return sz;
}

static int io_write(struct v4l2_subdev *sd, u16 reg, u8 val)
{
	struct tda1997x_state *state = to_state(sd);
	s32 ret = 0;

	mutex_lock(&state->page_lock);
	if (tda1997x_setpage(sd, reg >> 8)) {
		ret = -1;
		goto out;
	}

	ret = i2c_smbus_write_byte_data(state->client, reg & 0xff, val);
	if (ret < 0) {
		v4l_err(state->client, "write reg error:reg=%2x,val=%2x\n",
			reg&0xff, val);
		ret = -1;
		goto out;
	}

out:
	mutex_unlock(&state->page_lock);
	return ret;
}

static int io_write16(struct v4l2_subdev *sd, u16 reg, u16 val)
{
	int ret;

	ret = io_write(sd, reg, (val >> 8) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 1, val & 0xff);
	if (ret < 0)
		return ret;
	return 0;
}

static int io_write24(struct v4l2_subdev *sd, u16 reg, u32 val)
{
	int ret;

	ret = io_write(sd, reg, (val >> 16) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 1, (val >> 8) & 0xff);
	if (ret < 0)
		return ret;
	ret = io_write(sd, reg + 2, val & 0xff);
	if (ret < 0)
		return ret;
	return 0;
}

/* -----------------------------------------------------------------------------
 * Hotplug
 */

enum hpd_mode {
	HPD_LOW_BP,	/* HPD low and pulse of at least 100ms */
	HPD_LOW_OTHER,	/* HPD low and pulse of at least 100ms */
	HPD_HIGH_BP,	/* HIGH */
	HPD_HIGH_OTHER,
	HPD_PULSE,	/* HPD low pulse */
};

/* manual HPD (Hot Plug Detect) control */
static int tda1997x_manual_hpd(struct v4l2_subdev *sd, enum hpd_mode mode)
{
	u8 hpd_auto, hpd_pwr, hpd_man;

	hpd_auto = io_read(sd, REG_HPD_AUTO_CTRL);
	hpd_pwr = io_read(sd, REG_HPD_POWER);
	hpd_man = io_read(sd, REG_HPD_MAN_CTRL);

	/* mask out unused bits */
	hpd_man &= (HPD_MAN_CTRL_HPD_PULSE |
		    HPD_MAN_CTRL_5VEN |
		    HPD_MAN_CTRL_HPD_B |
		    HPD_MAN_CTRL_HPD_A);

	switch (mode) {
	/* HPD low and pulse of at least 100ms */
	case HPD_LOW_BP:
		/* hpd_bp=0 */
		hpd_pwr &= ~HPD_POWER_BP_MASK;
		/* disable HPD_A and HPD_B */
		hpd_man &= ~(HPD_MAN_CTRL_HPD_A | HPD_MAN_CTRL_HPD_B);
		io_write(sd, REG_HPD_POWER, hpd_pwr);
		io_write(sd, REG_HPD_MAN_CTRL, hpd_man);
		break;
	/* HPD high */
	case HPD_HIGH_BP:
		/* hpd_bp=1 */
		hpd_pwr &= ~HPD_POWER_BP_MASK;
		hpd_pwr |= 1 << HPD_POWER_BP_SHIFT;
		io_write(sd, REG_HPD_POWER, hpd_pwr);
		break;
	/* HPD low and pulse of at least 100ms */
	case HPD_LOW_OTHER:
		/* disable HPD_A and HPD_B */
		hpd_man &= ~(HPD_MAN_CTRL_HPD_A | HPD_MAN_CTRL_HPD_B);
		/* hp_other=0 */
		hpd_auto &= ~HPD_AUTO_HP_OTHER;
		io_write(sd, REG_HPD_AUTO_CTRL, hpd_auto);
		io_write(sd, REG_HPD_MAN_CTRL, hpd_man);
		break;
	/* HPD high */
	case HPD_HIGH_OTHER:
		hpd_auto |= HPD_AUTO_HP_OTHER;
		io_write(sd, REG_HPD_AUTO_CTRL, hpd_auto);
		break;
	/* HPD low pulse */
	case HPD_PULSE:
		/* disable HPD_A and HPD_B */
		hpd_man &= ~(HPD_MAN_CTRL_HPD_A | HPD_MAN_CTRL_HPD_B);
		io_write(sd, REG_HPD_MAN_CTRL, hpd_man);
		break;
	}

	return 0;
}

static void tda1997x_delayed_work_enable_hpd(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct tda1997x_state *state = container_of(dwork,
						    struct tda1997x_state,
						    delayed_work_enable_hpd);
	struct v4l2_subdev *sd = &state->sd;

	v4l2_dbg(2, debug, sd, "%s:\n", __func__);

	/* Set HPD high */
	tda1997x_manual_hpd(sd, HPD_HIGH_OTHER);
	tda1997x_manual_hpd(sd, HPD_HIGH_BP);

	state->edid.present = 1;
}

static void tda1997x_disable_edid(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s\n", __func__);
	cancel_delayed_work_sync(&state->delayed_work_enable_hpd);

	/* Set HPD low */
	tda1997x_manual_hpd(sd, HPD_LOW_BP);
}

static void tda1997x_enable_edid(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);

	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	/* Enable hotplug after 100ms */
	schedule_delayed_work(&state->delayed_work_enable_hpd, HZ / 10);
}

/* -----------------------------------------------------------------------------
 * Signal Control
 */

/*
 * configure vid_fmt based on mbus_code
 */
static int
tda1997x_setup_format(struct tda1997x_state *state, u32 code)
{
	v4l_dbg(1, debug, state->client, "%s code=0x%x\n", __func__, code);
	switch (code) {
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV8_1X24:
		state->vid_fmt = OF_FMT_444;
		break;
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYVY8_1X16:
		state->vid_fmt = OF_FMT_422_SMPT;
		break;
	case MEDIA_BUS_FMT_UYVY12_2X12:
	case MEDIA_BUS_FMT_UYVY10_2X10:
	case MEDIA_BUS_FMT_UYVY8_2X8:
		state->vid_fmt = OF_FMT_422_CCIR;
		break;
	default:
		v4l_err(state->client, "incompatible format (0x%x)\n", code);
		return -EINVAL;
	}
	v4l_dbg(1, debug, state->client, "%s code=0x%x fmt=%s\n", __func__,
		code, vidfmt_names[state->vid_fmt]);
	state->mbus_code = code;

	return 0;
}

/*
 * The color conversion matrix will convert between the colorimetry of the
 * HDMI input to the desired output format RGB|YUV. RGB output is to be
 * full-range and YUV is to be limited range.
 *
 * RGB full-range uses values from 0 to 255 which is recommended on a monitor
 * and RGB Limited uses values from 16 to 236 (16=black, 235=white) which is
 * typically recommended on a TV.
 */
static void
tda1997x_configure_csc(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);
	struct hdmi_avi_infoframe *avi = &state->avi_infoframe;
	struct v4l2_hdmi_colorimetry *c = &state->colorimetry;
	/* Blanking code values depend on output colorspace (RGB or YUV) */
	struct blanking_codes {
		s16 code_gy;
		s16 code_bu;
		s16 code_rv;
	};
	static const struct blanking_codes rgb_blanking = { 64, 64, 64 };
	static const struct blanking_codes yuv_blanking = { 64, 512, 512 };
	const struct blanking_codes *blanking_codes = NULL;
	u8 reg;

	v4l_dbg(1, debug, state->client, "input:%s quant:%s output:%s\n",
		hdmi_colorspace_names[avi->colorspace],
		v4l2_quantization_names[c->quantization],
		vidfmt_names[state->vid_fmt]);
	state->conv = NULL;
	switch (state->vid_fmt) {
	/* RGB output */
	case OF_FMT_444:
		blanking_codes = &rgb_blanking;
		if (c->colorspace == V4L2_COLORSPACE_SRGB) {
			if (c->quantization == V4L2_QUANTIZATION_LIM_RANGE)
				state->conv = &conv_matrix[RGBLIMITED_RGBFULL];
		} else {
			if (c->colorspace == V4L2_COLORSPACE_REC709)
				state->conv = &conv_matrix[ITU709_RGBFULL];
			else if (c->colorspace == V4L2_COLORSPACE_SMPTE170M)
				state->conv = &conv_matrix[ITU601_RGBFULL];
		}
		break;

	/* YUV output */
	case OF_FMT_422_SMPT: /* semi-planar */
	case OF_FMT_422_CCIR: /* CCIR656 */
		blanking_codes = &yuv_blanking;
		if ((c->colorspace == V4L2_COLORSPACE_SRGB) &&
		    (c->quantization == V4L2_QUANTIZATION_FULL_RANGE)) {
			if (state->timings.bt.height <= 576)
				state->conv = &conv_matrix[RGBFULL_ITU601];
			else
				state->conv = &conv_matrix[RGBFULL_ITU709];
		} else if ((c->colorspace == V4L2_COLORSPACE_SRGB) &&
			   (c->quantization == V4L2_QUANTIZATION_LIM_RANGE)) {
			if (state->timings.bt.height <= 576)
				state->conv = &conv_matrix[RGBLIMITED_ITU601];
			else
				state->conv = &conv_matrix[RGBLIMITED_ITU709];
		}
		break;
	}

	if (state->conv) {
		v4l_dbg(1, debug, state->client, "%s\n",
			state->conv->name);
		/* enable matrix conversion */
		reg = io_read(sd, REG_VDP_CTRL);
		reg &= ~VDP_CTRL_MATRIX_BP;
		io_write(sd, REG_VDP_CTRL, reg);
		/* offset inputs */
		io_write16(sd, REG_VDP_MATRIX + 0, state->conv->offint1);
		io_write16(sd, REG_VDP_MATRIX + 2, state->conv->offint2);
		io_write16(sd, REG_VDP_MATRIX + 4, state->conv->offint3);
		/* coefficients */
		io_write16(sd, REG_VDP_MATRIX + 6, state->conv->p11coef);
		io_write16(sd, REG_VDP_MATRIX + 8, state->conv->p12coef);
		io_write16(sd, REG_VDP_MATRIX + 10, state->conv->p13coef);
		io_write16(sd, REG_VDP_MATRIX + 12, state->conv->p21coef);
		io_write16(sd, REG_VDP_MATRIX + 14, state->conv->p22coef);
		io_write16(sd, REG_VDP_MATRIX + 16, state->conv->p23coef);
		io_write16(sd, REG_VDP_MATRIX + 18, state->conv->p31coef);
		io_write16(sd, REG_VDP_MATRIX + 20, state->conv->p32coef);
		io_write16(sd, REG_VDP_MATRIX + 22, state->conv->p33coef);
		/* offset outputs */
		io_write16(sd, REG_VDP_MATRIX + 24, state->conv->offout1);
		io_write16(sd, REG_VDP_MATRIX + 26, state->conv->offout2);
		io_write16(sd, REG_VDP_MATRIX + 28, state->conv->offout3);
	} else {
		/* disable matrix conversion */
		reg = io_read(sd, REG_VDP_CTRL);
		reg |= VDP_CTRL_MATRIX_BP;
		io_write(sd, REG_VDP_CTRL, reg);
	}

	/* SetBlankingCodes */
	if (blanking_codes) {
		io_write16(sd, REG_BLK_GY, blanking_codes->code_gy);
		io_write16(sd, REG_BLK_BU, blanking_codes->code_bu);
		io_write16(sd, REG_BLK_RV, blanking_codes->code_rv);
	}
}

/* Configure frame detection window and VHREF timing generator */
static void
tda1997x_configure_vhref(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);
	const struct v4l2_bt_timings *bt = &state->timings.bt;
	int width, lines;
	u16 href_start, href_end;
	u16 vref_f1_start, vref_f2_start;
	u8 vref_f1_width, vref_f2_width;
	u8 field_polarity;
	u16 fieldref_f1_start, fieldref_f2_start;
	u8 reg;

	href_start = bt->hbackporch + bt->hsync + 1;
	href_end = href_start + bt->width;
	vref_f1_start = bt->height + bt->vbackporch + bt->vsync +
			bt->il_vbackporch + bt->il_vsync +
			bt->il_vfrontporch;
	vref_f1_width = bt->vbackporch + bt->vsync + bt->vfrontporch;
	vref_f2_start = 0;
	vref_f2_width = 0;
	fieldref_f1_start = 0;
	fieldref_f2_start = 0;
	if (bt->interlaced) {
		vref_f2_start = (bt->height / 2) +
				(bt->il_vbackporch + bt->il_vsync - 1);
		vref_f2_width = bt->il_vbackporch + bt->il_vsync +
				bt->il_vfrontporch;
		fieldref_f2_start = vref_f2_start + bt->il_vfrontporch +
				    fieldref_f1_start;
	}
	field_polarity = 0;

	width = V4L2_DV_BT_FRAME_WIDTH(bt);
	lines = V4L2_DV_BT_FRAME_HEIGHT(bt);

	/*
	 * Configure Frame Detection Window:
	 *  horiz area where the VHREF module consider a VSYNC a new frame
	 */
	io_write16(sd, REG_FDW_S, 0x2ef); /* start position */
	io_write16(sd, REG_FDW_E, 0x141); /* end position */

	/* Set Pixel And Line Counters */
	if (state->chip_revision == 0)
		io_write16(sd, REG_PXCNT_PR, 4);
	else
		io_write16(sd, REG_PXCNT_PR, 1);
	io_write16(sd, REG_PXCNT_NPIX, width & MASK_VHREF);
	io_write16(sd, REG_LCNT_PR, 1);
	io_write16(sd, REG_LCNT_NLIN, lines & MASK_VHREF);

	/*
	 * Configure the VHRef timing generator responsible for rebuilding all
	 * horiz and vert synch and ref signals from its input allowing auto
	 * detection algorithms and forcing predefined modes (480i & 576i)
	 */
	reg = VHREF_STD_DET_OFF << VHREF_STD_DET_SHIFT;
	io_write(sd, REG_VHREF_CTRL, reg);

	/*
	 * Configure the VHRef timing values. In case the VHREF generator has
	 * been configured in manual mode, this will allow to manually set all
	 * horiz and vert ref values (non-active pixel areas) of the generator
	 * and allows setting the frame reference params.
	 */
	/* horizontal reference start/end */
	io_write16(sd, REG_HREF_S, href_start & MASK_VHREF);
	io_write16(sd, REG_HREF_E, href_end & MASK_VHREF);
	/* vertical reference f1 start/end */
	io_write16(sd, REG_VREF_F1_S, vref_f1_start & MASK_VHREF);
	io_write(sd, REG_VREF_F1_WIDTH, vref_f1_width);
	/* vertical reference f2 start/end */
	io_write16(sd, REG_VREF_F2_S, vref_f2_start & MASK_VHREF);
	io_write(sd, REG_VREF_F2_WIDTH, vref_f2_width);

	/* F1/F2 FREF, field polarity */
	reg = fieldref_f1_start & MASK_VHREF;
	reg |= field_polarity << 8;
	io_write16(sd, REG_FREF_F1_S, reg);
	reg = fieldref_f2_start & MASK_VHREF;
	io_write16(sd, REG_FREF_F2_S, reg);
}

/* Configure Video Output port signals */
static int
tda1997x_configure_vidout(struct tda1997x_state *state)
{
	struct v4l2_subdev *sd = &state->sd;
	struct tda1997x_platform_data *pdata = &state->pdata;
	u8 prefilter;
	u8 reg;

	/* Configure pixel clock generator: delay, polarity, rate */
	reg = (state->vid_fmt == OF_FMT_422_CCIR) ?
	       PCLK_SEL_X2 : PCLK_SEL_X1;
	reg |= pdata->vidout_delay_pclk << PCLK_DELAY_SHIFT;
	reg |= pdata->vidout_inv_pclk << PCLK_INV_SHIFT;
	io_write(sd, REG_PCLK, reg);

	/* Configure pre-filter */
	prefilter = 0; /* filters off */
	/* YUV422 mode requires conversion */
	if ((state->vid_fmt == OF_FMT_422_SMPT) ||
	    (state->vid_fmt == OF_FMT_422_CCIR)) {
		/* 2/7 taps for Rv and Bu */
		prefilter = FILTERS_CTRL_2_7TAP << FILTERS_CTRL_BU_SHIFT |
			    FILTERS_CTRL_2_7TAP << FILTERS_CTRL_RV_SHIFT;
	}
	io_write(sd, REG_FILTERS_CTRL, prefilter);

	/* Configure video port */
	reg = state->vid_fmt & OF_FMT_MASK;
	if (state->vid_fmt == OF_FMT_422_CCIR)
		reg |= (OF_BLK | OF_TRC);
	reg |= OF_VP_ENABLE;
	io_write(sd, REG_OF, reg);

	/* Configure formatter and conversions */
	reg = io_read(sd, REG_VDP_CTRL);
	/* pre-filter is needed unless (REG_FILTERS_CTRL == 0) */
	if (!prefilter)
		reg |= VDP_CTRL_PREFILTER_BP;
	else
		reg &= ~VDP_CTRL_PREFILTER_BP;
	/* formatter is needed for YUV422 and for trc/blc codes */
	if (state->vid_fmt == OF_FMT_444)
		reg |= VDP_CTRL_FORMATTER_BP;
	/* formatter and compdel needed for timing/blanking codes */
	else
		reg &= ~(VDP_CTRL_FORMATTER_BP | VDP_CTRL_COMPDEL_BP);
	/* activate compdel for small sync delays */
	if ((pdata->vidout_delay_vs < 4) || (pdata->vidout_delay_hs < 4))
		reg &= ~VDP_CTRL_COMPDEL_BP;
	io_write(sd, REG_VDP_CTRL, reg);

	/* Configure DE output signal: delay, polarity, and source */
	reg = pdata->vidout_delay_de << DE_FREF_DELAY_SHIFT |
	      pdata->vidout_inv_de << DE_FREF_INV_SHIFT |
	      pdata->vidout_sel_de << DE_FREF_SEL_SHIFT;
	io_write(sd, REG_DE_FREF, reg);

	/* Configure HS/HREF output signal: delay, polarity, and source */
	if (state->vid_fmt != OF_FMT_422_CCIR) {
		reg = pdata->vidout_delay_hs << HS_HREF_DELAY_SHIFT |
		      pdata->vidout_inv_hs << HS_HREF_INV_SHIFT |
		      pdata->vidout_sel_hs << HS_HREF_SEL_SHIFT;
	} else
		reg = HS_HREF_SEL_NONE << HS_HREF_SEL_SHIFT;
	io_write(sd, REG_HS_HREF, reg);

	/* Configure VS/VREF output signal: delay, polarity, and source */
	if (state->vid_fmt != OF_FMT_422_CCIR) {
		reg = pdata->vidout_delay_vs << VS_VREF_DELAY_SHIFT |
		      pdata->vidout_inv_vs << VS_VREF_INV_SHIFT |
		      pdata->vidout_sel_vs << VS_VREF_SEL_SHIFT;
	} else
		reg = VS_VREF_SEL_NONE << VS_VREF_SEL_SHIFT;
	io_write(sd, REG_VS_VREF, reg);

	return 0;
}

/* Configure Audio output port signals */
static int
tda1997x_configure_audout(struct v4l2_subdev *sd, u8 channel_assignment)
{
	struct tda1997x_state *state = to_state(sd);
	struct tda1997x_platform_data *pdata = &state->pdata;
	bool sp_used_by_fifo = true;
	u8 reg;

	if (!pdata->audout_format)
		return 0;

	/* channel assignment (CEA-861-D Table 20) */
	io_write(sd, REG_AUDIO_PATH, channel_assignment);

	/* Audio output configuration */
	reg = 0;
	switch (pdata->audout_format) {
	case AUDFMT_TYPE_I2S:
		reg |= AUDCFG_BUS_I2S << AUDCFG_BUS_SHIFT;
		break;
	case AUDFMT_TYPE_SPDIF:
		reg |= AUDCFG_BUS_SPDIF << AUDCFG_BUS_SHIFT;
		break;
	}
	switch (state->audio_type) {
	case AUDCFG_TYPE_PCM:
		reg |= AUDCFG_TYPE_PCM << AUDCFG_TYPE_SHIFT;
		break;
	case AUDCFG_TYPE_OBA:
		reg |= AUDCFG_TYPE_OBA << AUDCFG_TYPE_SHIFT;
		break;
	case AUDCFG_TYPE_DST:
		reg |= AUDCFG_TYPE_DST << AUDCFG_TYPE_SHIFT;
		sp_used_by_fifo = false;
		break;
	case AUDCFG_TYPE_HBR:
		reg |= AUDCFG_TYPE_HBR << AUDCFG_TYPE_SHIFT;
		if (pdata->audout_layout == 1) {
			/* demuxed via AP0:AP3 */
			reg |= AUDCFG_HBR_DEMUX << AUDCFG_HBR_SHIFT;
			if (pdata->audout_format == AUDFMT_TYPE_SPDIF)
				sp_used_by_fifo = false;
		} else {
			/* straight via AP0 */
			reg |= AUDCFG_HBR_STRAIGHT << AUDCFG_HBR_SHIFT;
		}
		break;
	}
	if (pdata->audout_width == 32)
		reg |= AUDCFG_I2SW_32 << AUDCFG_I2SW_SHIFT;
	else
		reg |= AUDCFG_I2SW_16 << AUDCFG_I2SW_SHIFT;

	/* automatic hardware mute */
	if (pdata->audio_auto_mute)
		reg |= AUDCFG_AUTO_MUTE_EN;
	/* clock polarity */
	if (pdata->audout_invert_clk)
		reg |= AUDCFG_CLK_INVERT;
	io_write(sd, REG_AUDCFG, reg);

	/* audio layout */
	reg = (pdata->audout_layout) ? AUDIO_LAYOUT_LAYOUT1 : 0;
	if (!pdata->audout_layoutauto)
		reg |= AUDIO_LAYOUT_MANUAL;
	if (sp_used_by_fifo)
		reg |= AUDIO_LAYOUT_SP_FLAG;
	io_write(sd, REG_AUDIO_LAYOUT, reg);

	/* FIFO Latency value */
	io_write(sd, REG_FIFO_LATENCY_VAL, 0x80);

	/* Audio output port config */
	if (sp_used_by_fifo) {
		reg = AUDIO_OUT_ENABLE_AP0;
		if (channel_assignment >= 0x01)
			reg |= AUDIO_OUT_ENABLE_AP1;
		if (channel_assignment >= 0x04)
			reg |= AUDIO_OUT_ENABLE_AP2;
		if (channel_assignment >= 0x0c)
			reg |= AUDIO_OUT_ENABLE_AP3;
		/* specific cases where AP1 is not used */
		if ((channel_assignment == 0x04)
		 || (channel_assignment == 0x08)
		 || (channel_assignment == 0x0c)
		 || (channel_assignment == 0x10)
		 || (channel_assignment == 0x14)
		 || (channel_assignment == 0x18)
		 || (channel_assignment == 0x1c))
			reg &= ~AUDIO_OUT_ENABLE_AP1;
		/* specific cases where AP2 is not used */
		if ((channel_assignment >= 0x14)
		 && (channel_assignment <= 0x17))
			reg &= ~AUDIO_OUT_ENABLE_AP2;
	} else {
		reg = AUDIO_OUT_ENABLE_AP3 |
		      AUDIO_OUT_ENABLE_AP2 |
		      AUDIO_OUT_ENABLE_AP1 |
		      AUDIO_OUT_ENABLE_AP0;
	}
	if (pdata->audout_format == AUDFMT_TYPE_I2S)
		reg |= (AUDIO_OUT_ENABLE_ACLK | AUDIO_OUT_ENABLE_WS);
	io_write(sd, REG_AUDIO_OUT_ENABLE, reg);

	/* reset test mode to normal audio freq auto selection */
	io_write(sd, REG_TEST_MODE, 0x00);

	return 0;
}

/* Soft Reset of specific hdmi info */
static int
tda1997x_hdmi_info_reset(struct v4l2_subdev *sd, u8 info_rst, bool reset_sus)
{
	u8 reg;

	/* reset infoframe engine packets */
	reg = io_read(sd, REG_HDMI_INFO_RST);
	io_write(sd, REG_HDMI_INFO_RST, info_rst);

	/* if infoframe engine has been reset clear INT_FLG_MODE */
	if (reg & RESET_IF) {
		reg = io_read(sd, REG_INT_FLG_CLR_MODE);
		io_write(sd, REG_INT_FLG_CLR_MODE, reg);
	}

	/* Disable REFTIM to restart start-up-sequencer (SUS) */
	reg = io_read(sd, REG_RATE_CTRL);
	reg &= ~RATE_REFTIM_ENABLE;
	if (!reset_sus)
		reg |= RATE_REFTIM_ENABLE;
	reg = io_write(sd, REG_RATE_CTRL, reg);

	return 0;
}

static void
tda1997x_power_mode(struct tda1997x_state *state, bool enable)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg;

	if (enable) {
		/* Automatic control of TMDS */
		io_write(sd, REG_PON_OVR_EN, PON_DIS);
		/* Enable current bias unit */
		io_write(sd, REG_CFG1, PON_EN);
		/* Enable deep color PLL */
		io_write(sd, REG_DEEP_PLL7_BYP, PON_DIS);
		/* Output buffers active */
		reg = io_read(sd, REG_OF);
		reg &= ~OF_VP_ENABLE;
		io_write(sd, REG_OF, reg);
	} else {
		/* Power down EDID mode sequence */
		/* Output buffers in HiZ */
		reg = io_read(sd, REG_OF);
		reg |= OF_VP_ENABLE;
		io_write(sd, REG_OF, reg);
		/* Disable deep color PLL */
		io_write(sd, REG_DEEP_PLL7_BYP, PON_EN);
		/* Disable current bias unit */
		io_write(sd, REG_CFG1, PON_DIS);
		/* Manual control of TMDS */
		io_write(sd, REG_PON_OVR_EN, PON_EN);
	}
}

static bool
tda1997x_detect_tx_5v(struct v4l2_subdev *sd)
{
	u8 reg = io_read(sd, REG_DETECT_5V);

	return ((reg & DETECT_5V_SEL) ? 1 : 0);
}

static bool
tda1997x_detect_tx_hpd(struct v4l2_subdev *sd)
{
	u8 reg = io_read(sd, REG_DETECT_5V);

	return ((reg & DETECT_HPD) ? 1 : 0);
}

static int
tda1997x_detect_std(struct tda1997x_state *state,
		    struct v4l2_dv_timings *timings)
{
	struct v4l2_subdev *sd = &state->sd;
	u32 vper;
	u16 hper;
	u16 hsper;
	int i;

	/*
	 * Read the FMT registers
	 *   REG_V_PER: Period of a frame (or two fields) in MCLK(27MHz) cycles
	 *   REG_H_PER: Period of a line in MCLK(27MHz) cycles
	 *   REG_HS_WIDTH: Period of horiz sync pulse in MCLK(27MHz) cycles
	 */
	vper = io_read24(sd, REG_V_PER) & MASK_VPER;
	hper = io_read16(sd, REG_H_PER) & MASK_HPER;
	hsper = io_read16(sd, REG_HS_WIDTH) & MASK_HSWIDTH;
	v4l2_dbg(1, debug, sd, "Signal Timings: %u/%u/%u\n", vper, hper, hsper);
	if (!vper || !hper || !hsper)
		return -ENOLINK;

	for (i = 0; v4l2_dv_timings_presets[i].bt.width; i++) {
		const struct v4l2_bt_timings *bt;
		u32 lines, width, _hper, _hsper;
		u32 vmin, vmax, hmin, hmax, hsmin, hsmax;
		bool vmatch, hmatch, hsmatch;

		bt = &v4l2_dv_timings_presets[i].bt;
		width = V4L2_DV_BT_FRAME_WIDTH(bt);
		lines = V4L2_DV_BT_FRAME_HEIGHT(bt);
		_hper = (u32)bt->pixelclock / width;
		if (bt->interlaced)
			lines /= 2;
		/* vper +/- 0.7% */
		vmin = ((27000000 / 1000) * 993) / _hper * lines;
		vmax = ((27000000 / 1000) * 1007) / _hper * lines;
		/* hper +/- 1.0% */
		hmin = ((27000000 / 100) * 99) / _hper;
		hmax = ((27000000 / 100) * 101) / _hper;
		/* hsper +/- 2 (take care to avoid 32bit overflow) */
		_hsper = 27000 * bt->hsync / ((u32)bt->pixelclock/1000);
		hsmin = _hsper - 2;
		hsmax = _hsper + 2;

		/* vmatch matches the framerate */
		vmatch = ((vper <= vmax) && (vper >= vmin)) ? 1 : 0;
		/* hmatch matches the width */
		hmatch = ((hper <= hmax) && (hper >= hmin)) ? 1 : 0;
		/* hsmatch matches the hswidth */
		hsmatch = ((hsper <= hsmax) && (hsper >= hsmin)) ? 1 : 0;
		if (hmatch && vmatch && hsmatch) {
			v4l2_print_dv_timings(sd->name, "Detected format: ",
					      &v4l2_dv_timings_presets[i],
					      false);
			if (timings)
				*timings = v4l2_dv_timings_presets[i];
			return 0;
		}
	}

	v4l_err(state->client, "no resolution match for timings: %d/%d/%d\n",
		vper, hper, hsper);
	return -ERANGE;
}

/* some sort of errata workaround for chip revision 0 (N1) */
static void tda1997x_reset_n1(struct tda1997x_state *state)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg;

	/* clear HDMI mode flag in BCAPS */
	io_write(sd, REG_CLK_CFG, CLK_CFG_SEL_ACLK_EN | CLK_CFG_SEL_ACLK);
	io_write(sd, REG_PON_OVR_EN, PON_EN);
	io_write(sd, REG_PON_CBIAS, PON_EN);
	io_write(sd, REG_PON_PLL, PON_EN);

	reg = io_read(sd, REG_MODE_REC_CFG1);
	reg &= ~0x06;
	reg |= 0x02;
	io_write(sd, REG_MODE_REC_CFG1, reg);
	io_write(sd, REG_CLK_CFG, CLK_CFG_DIS);
	io_write(sd, REG_PON_OVR_EN, PON_DIS);
	reg = io_read(sd, REG_MODE_REC_CFG1);
	reg &= ~0x06;
	io_write(sd, REG_MODE_REC_CFG1, reg);
}

/*
 * Activity detection must only be notified when stable_clk_x AND active_x
 * bits are set to 1. If only stable_clk_x bit is set to 1 but not
 * active_x, it means that the TMDS clock is not in the defined range
 * and activity detection must not be notified.
 */
static u8
tda1997x_read_activity_status_regs(struct v4l2_subdev *sd)
{
	u8 reg, status = 0;

	/* Read CLK_A_STATUS register */
	reg = io_read(sd, REG_CLK_A_STATUS);
	/* ignore if not active */
	if ((reg & MASK_CLK_STABLE) && !(reg & MASK_CLK_ACTIVE))
		reg &= ~MASK_CLK_STABLE;
	status |= ((reg & MASK_CLK_STABLE) >> 2);

	/* Read CLK_B_STATUS register */
	reg = io_read(sd, REG_CLK_B_STATUS);
	/* ignore if not active */
	if ((reg & MASK_CLK_STABLE) && !(reg & MASK_CLK_ACTIVE))
		reg &= ~MASK_CLK_STABLE;
	status |= ((reg & MASK_CLK_STABLE) >> 1);

	/* Read the SUS_STATUS register */
	reg = io_read(sd, REG_SUS_STATUS);

	/* If state = 5 => TMDS is locked */
	if ((reg & MASK_SUS_STATUS) == LAST_STATE_REACHED)
		status |= MASK_SUS_STATE;
	else
		status &= ~MASK_SUS_STATE;

	return status;
}

static void
set_rgb_quantization_range(struct tda1997x_state *state)
{
	struct v4l2_hdmi_colorimetry *c = &state->colorimetry;

	state->colorimetry = v4l2_hdmi_rx_colorimetry(&state->avi_infoframe,
						      NULL,
						      state->timings.bt.height);
	/* If ycbcr_enc is V4L2_YCBCR_ENC_DEFAULT, we receive RGB */
	if (c->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT) {
		switch (state->rgb_quantization_range) {
		case V4L2_DV_RGB_RANGE_LIMITED:
			c->quantization = V4L2_QUANTIZATION_FULL_RANGE;
			break;
		case V4L2_DV_RGB_RANGE_FULL:
			c->quantization = V4L2_QUANTIZATION_LIM_RANGE;
			break;
		}
	}
	v4l_dbg(1, debug, state->client,
		"colorspace=%d/%d colorimetry=%d range=%s content=%d\n",
		state->avi_infoframe.colorspace, c->colorspace,
		state->avi_infoframe.colorimetry,
		v4l2_quantization_names[c->quantization],
		state->avi_infoframe.content_type);
}

/* parse an infoframe and do some sanity checks on it */
static unsigned int
tda1997x_parse_infoframe(struct tda1997x_state *state, u16 addr)
{
	struct v4l2_subdev *sd = &state->sd;
	union hdmi_infoframe frame;
	u8 buffer[40];
	u8 reg;
	int len, err;

	/* read data */
	len = io_readn(sd, addr, sizeof(buffer), buffer);
	err = hdmi_infoframe_unpack(&frame, buffer, sizeof(buffer));
	if (err) {
		v4l_err(state->client,
			"failed parsing %d byte infoframe: 0x%04x/0x%02x\n",
			len, addr, buffer[0]);
		return err;
	}
	hdmi_infoframe_log(KERN_INFO, &state->client->dev, &frame);
	switch (frame.any.type) {
	/* Audio InfoFrame: see HDMI spec 8.2.2 */
	case HDMI_INFOFRAME_TYPE_AUDIO:
		/* sample rate */
		switch (frame.audio.sample_frequency) {
		case HDMI_AUDIO_SAMPLE_FREQUENCY_32000:
			state->audio_samplerate = 32000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_44100:
			state->audio_samplerate = 44100;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_48000:
			state->audio_samplerate = 48000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_88200:
			state->audio_samplerate = 88200;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_96000:
			state->audio_samplerate = 96000;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_176400:
			state->audio_samplerate = 176400;
			break;
		case HDMI_AUDIO_SAMPLE_FREQUENCY_192000:
			state->audio_samplerate = 192000;
			break;
		default:
		case HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM:
			break;
		}

		/* sample size */
		switch (frame.audio.sample_size) {
		case HDMI_AUDIO_SAMPLE_SIZE_16:
			state->audio_samplesize = 16;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_20:
			state->audio_samplesize = 20;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_24:
			state->audio_samplesize = 24;
			break;
		case HDMI_AUDIO_SAMPLE_SIZE_STREAM:
		default:
			break;
		}

		/* Channel Count */
		state->audio_channels = frame.audio.channels;
		if (frame.audio.channel_allocation &&
		    frame.audio.channel_allocation != state->audio_ch_alloc) {
			/* use the channel assignment from the infoframe */
			state->audio_ch_alloc = frame.audio.channel_allocation;
			tda1997x_configure_audout(sd, state->audio_ch_alloc);
			/* reset the audio FIFO */
			tda1997x_hdmi_info_reset(sd, RESET_AUDIO, false);
		}
		break;

	/* Auxiliary Video information (AVI) InfoFrame: see HDMI spec 8.2.1 */
	case HDMI_INFOFRAME_TYPE_AVI:
		state->avi_infoframe = frame.avi;
		set_rgb_quantization_range(state);

		/* configure upsampler: 0=bypass 1=repeatchroma 2=interpolate */
		reg = io_read(sd, REG_PIX_REPEAT);
		reg &= ~PIX_REPEAT_MASK_UP_SEL;
		if (frame.avi.colorspace == HDMI_COLORSPACE_YUV422)
			reg |= (PIX_REPEAT_CHROMA << PIX_REPEAT_SHIFT);
		io_write(sd, REG_PIX_REPEAT, reg);

		/* ConfigurePixelRepeater: repeat n-times each pixel */
		reg = io_read(sd, REG_PIX_REPEAT);
		reg &= ~PIX_REPEAT_MASK_REP;
		reg |= frame.avi.pixel_repeat;
		io_write(sd, REG_PIX_REPEAT, reg);

		/* configure the receiver with the new colorspace */
		tda1997x_configure_csc(sd);
		break;
	default:
		break;
	}
	return 0;
}

static void tda1997x_irq_sus(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg, source;

	source = io_read(sd, REG_INT_FLG_CLR_SUS);
	io_write(sd, REG_INT_FLG_CLR_SUS, source);

	if (source & MASK_MPT) {
		/* reset MTP in use flag if set */
		if (state->mptrw_in_progress)
			state->mptrw_in_progress = 0;
	}

	if (source & MASK_SUS_END) {
		/* reset audio FIFO */
		reg = io_read(sd, REG_HDMI_INFO_RST);
		reg |= MASK_SR_FIFO_FIFO_CTRL;
		io_write(sd, REG_HDMI_INFO_RST, reg);
		reg &= ~MASK_SR_FIFO_FIFO_CTRL;
		io_write(sd, REG_HDMI_INFO_RST, reg);

		/* reset HDMI flags */
		state->hdmi_status = 0;
	}

	/* filter FMT interrupt based on SUS state */
	reg = io_read(sd, REG_SUS_STATUS);
	if (((reg & MASK_SUS_STATUS) != LAST_STATE_REACHED)
	   || (source & MASK_MPT)) {
		source &= ~MASK_FMT;
	}

	if (source & (MASK_FMT | MASK_SUS_END)) {
		reg = io_read(sd, REG_SUS_STATUS);
		if ((reg & MASK_SUS_STATUS) != LAST_STATE_REACHED) {
			v4l_err(state->client, "BAD SUS STATUS\n");
			return;
		}
		if (debug)
			tda1997x_detect_std(state, NULL);
		/* notify user of change in resolution */
		v4l2_subdev_notify_event(&state->sd, &tda1997x_ev_fmt);
	}
}

static void tda1997x_irq_ddc(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 source;

	source = io_read(sd, REG_INT_FLG_CLR_DDC);
	io_write(sd, REG_INT_FLG_CLR_DDC, source);
	if (source & MASK_EDID_MTP) {
		/* reset MTP in use flag if set */
		if (state->mptrw_in_progress)
			state->mptrw_in_progress = 0;
	}

	/* Detection of +5V */
	if (source & MASK_DET_5V) {
		v4l2_ctrl_s_ctrl(state->detect_tx_5v_ctrl,
				 tda1997x_detect_tx_5v(sd));
	}
}

static void tda1997x_irq_rate(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg, source;

	u8 irq_status;

	source = io_read(sd, REG_INT_FLG_CLR_RATE);
	io_write(sd, REG_INT_FLG_CLR_RATE, source);

	/* read status regs */
	irq_status = tda1997x_read_activity_status_regs(sd);

	/*
	 * read clock status reg until INT_FLG_CLR_RATE is still 0
	 * after the read to make sure its the last one
	 */
	reg = source;
	while (reg != 0) {
		irq_status = tda1997x_read_activity_status_regs(sd);
		reg = io_read(sd, REG_INT_FLG_CLR_RATE);
		io_write(sd, REG_INT_FLG_CLR_RATE, reg);
		source |= reg;
	}

	/* we only pay attention to stability change events */
	if (source & (MASK_RATE_A_ST | MASK_RATE_B_ST)) {
		int input = (source & MASK_RATE_A_ST)?0:1;
		u8 mask = 1<<input;

		/* state change */
		if ((irq_status & mask) != (state->activity_status & mask)) {
			/* activity lost */
			if ((irq_status & mask) == 0) {
				v4l_info(state->client,
					 "HDMI-%c: Digital Activity Lost\n",
					 input+'A');

				/* bypass up/down sampler and pixel repeater */
				reg = io_read(sd, REG_PIX_REPEAT);
				reg &= ~PIX_REPEAT_MASK_UP_SEL;
				reg &= ~PIX_REPEAT_MASK_REP;
				io_write(sd, REG_PIX_REPEAT, reg);

				if (state->chip_revision == 0)
					tda1997x_reset_n1(state);

				state->input_detect[input] = 0;
				v4l2_subdev_notify_event(sd, &tda1997x_ev_fmt);
			}

			/* activity detected */
			else {
				v4l_info(state->client,
					 "HDMI-%c: Digital Activity Detected\n",
					 input+'A');
				state->input_detect[input] = 1;
			}

			/* hold onto current state */
			state->activity_status = (irq_status & mask);
		}
	}
}

static void tda1997x_irq_info(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 source;

	source = io_read(sd, REG_INT_FLG_CLR_INFO);
	io_write(sd, REG_INT_FLG_CLR_INFO, source);

	/* Audio infoframe */
	if (source & MASK_AUD_IF) {
		tda1997x_parse_infoframe(state, AUD_IF);
		source &= ~MASK_AUD_IF;
	}

	/* Source Product Descriptor infoframe change */
	if (source & MASK_SPD_IF) {
		tda1997x_parse_infoframe(state, SPD_IF);
		source &= ~MASK_SPD_IF;
	}

	/* Auxiliary Video Information infoframe */
	if (source & MASK_AVI_IF) {
		tda1997x_parse_infoframe(state, AVI_IF);
		source &= ~MASK_AVI_IF;
	}
}

static void tda1997x_irq_audio(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg, source;

	source = io_read(sd, REG_INT_FLG_CLR_AUDIO);
	io_write(sd, REG_INT_FLG_CLR_AUDIO, source);

	/* reset audio FIFO on FIFO pointer error or audio mute */
	if (source & MASK_ERROR_FIFO_PT ||
	    source & MASK_MUTE_FLG) {
		/* audio reset audio FIFO */
		reg = io_read(sd, REG_SUS_STATUS);
		if ((reg & MASK_SUS_STATUS) == LAST_STATE_REACHED) {
			reg = io_read(sd, REG_HDMI_INFO_RST);
			reg |= MASK_SR_FIFO_FIFO_CTRL;
			io_write(sd, REG_HDMI_INFO_RST, reg);
			reg &= ~MASK_SR_FIFO_FIFO_CTRL;
			io_write(sd, REG_HDMI_INFO_RST, reg);
			/* reset channel status IT if present */
			source &= ~(MASK_CH_STATE);
		}
	}
	if (source & MASK_AUDIO_FREQ_FLG) {
		static const int freq[] = {
			0, 32000, 44100, 48000, 88200, 96000, 176400, 192000
		};

		reg = io_read(sd, REG_AUDIO_FREQ);
		state->audio_samplerate = freq[reg & 7];
		v4l_info(state->client, "Audio Frequency Change: %dHz\n",
			 state->audio_samplerate);
	}
	if (source & MASK_AUDIO_FLG) {
		reg = io_read(sd, REG_AUDIO_FLAGS);
		if (reg & BIT(AUDCFG_TYPE_DST))
			state->audio_type = AUDCFG_TYPE_DST;
		if (reg & BIT(AUDCFG_TYPE_OBA))
			state->audio_type = AUDCFG_TYPE_OBA;
		if (reg & BIT(AUDCFG_TYPE_HBR))
			state->audio_type = AUDCFG_TYPE_HBR;
		if (reg & BIT(AUDCFG_TYPE_PCM))
			state->audio_type = AUDCFG_TYPE_PCM;
		v4l_info(state->client, "Audio Type: %s\n",
			 audtype_names[state->audio_type]);
	}
}

static void tda1997x_irq_hdcp(struct tda1997x_state *state, u8 *flags)
{
	struct v4l2_subdev *sd = &state->sd;
	u8 reg, source;

	source = io_read(sd, REG_INT_FLG_CLR_HDCP);
	io_write(sd, REG_INT_FLG_CLR_HDCP, source);

	/* reset MTP in use flag if set */
	if (source & MASK_HDCP_MTP)
		state->mptrw_in_progress = 0;
	if (source & MASK_STATE_C5) {
		/* REPEATER: mask AUDIO and IF irqs to avoid IF during auth */
		reg = io_read(sd, REG_INT_MASK_TOP);
		reg &= ~(INTERRUPT_AUDIO | INTERRUPT_INFO);
		io_write(sd, REG_INT_MASK_TOP, reg);
		*flags &= (INTERRUPT_AUDIO | INTERRUPT_INFO);
	}
}

static irqreturn_t tda1997x_isr_thread(int irq, void *d)
{
	struct tda1997x_state *state = d;
	struct v4l2_subdev *sd = &state->sd;
	u8 flags;

	mutex_lock(&state->lock);
	do {
		/* read interrupt flags */
		flags = io_read(sd, REG_INT_FLG_CLR_TOP);
		if (flags == 0)
			break;

		/* SUS interrupt source (Input activity events) */
		if (flags & INTERRUPT_SUS)
			tda1997x_irq_sus(state, &flags);
		/* DDC interrupt source (Display Data Channel) */
		else if (flags & INTERRUPT_DDC)
			tda1997x_irq_ddc(state, &flags);
		/* RATE interrupt source (Digital Input activity) */
		else if (flags & INTERRUPT_RATE)
			tda1997x_irq_rate(state, &flags);
		/* Infoframe change interrupt */
		else if (flags & INTERRUPT_INFO)
			tda1997x_irq_info(state, &flags);
		/* Audio interrupt source:
		 *   freq change, DST,OBA,HBR,ASP flags, mute, FIFO err
		 */
		else if (flags & INTERRUPT_AUDIO)
			tda1997x_irq_audio(state, &flags);
		/* HDCP interrupt source (content protection) */
		if (flags & INTERRUPT_HDCP)
			tda1997x_irq_hdcp(state, &flags);
	} while (flags != 0);
	mutex_unlock(&state->lock);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * v4l2_subdev_video_ops
 */

static int
tda1997x_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct tda1997x_state *state = to_state(sd);
	u32 vper;
	u16 hper;
	u16 hsper;

	mutex_lock(&state->lock);
	vper = io_read24(sd, REG_V_PER) & MASK_VPER;
	hper = io_read16(sd, REG_H_PER) & MASK_HPER;
	hsper = io_read16(sd, REG_HS_WIDTH) & MASK_HSWIDTH;
	/*
	 * The tda1997x supports A/B inputs but only a single output.
	 * The irq handler monitors for timing changes on both inputs and
	 * sets the input_detect array to 0|1 depending on signal presence.
	 * I believe selection of A vs B is automatic.
	 *
	 * The vper/hper/hsper registers provide the frame period, line period
	 * and horiz sync period (units of MCLK clock cycles (27MHz)) and
	 * testing shows these values to be random if no signal is present
	 * or locked.
	 */
	v4l2_dbg(1, debug, sd, "inputs:%d/%d timings:%d/%d/%d\n",
		 state->input_detect[0], state->input_detect[1],
		 vper, hper, hsper);
	if (!state->input_detect[0] && !state->input_detect[1])
		*status = V4L2_IN_ST_NO_SIGNAL;
	else if (!vper || !hper || !hsper)
		*status = V4L2_IN_ST_NO_SYNC;
	else
		*status = 0;
	mutex_unlock(&state->lock);

	return 0;
};

static int tda1997x_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s\n", __func__);

	if (v4l2_match_dv_timings(&state->timings, timings, 0, false))
		return 0; /* no changes */

	if (!v4l2_valid_dv_timings(timings, &tda1997x_dv_timings_cap,
				   NULL, NULL))
		return -ERANGE;

	mutex_lock(&state->lock);
	state->timings = *timings;
	/* setup frame detection window and VHREF timing generator */
	tda1997x_configure_vhref(sd);
	/* configure colorspace conversion */
	tda1997x_configure_csc(sd);
	mutex_unlock(&state->lock);

	return 0;
}

static int tda1997x_g_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s\n", __func__);
	mutex_lock(&state->lock);
	*timings = state->timings;
	mutex_unlock(&state->lock);

	return 0;
}

static int tda1997x_query_dv_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s\n", __func__);
	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	mutex_lock(&state->lock);
	tda1997x_detect_std(state, timings);
	mutex_unlock(&state->lock);

	return 0;
}

static const struct v4l2_subdev_video_ops tda1997x_video_ops = {
	.g_input_status = tda1997x_g_input_status,
	.s_dv_timings = tda1997x_s_dv_timings,
	.g_dv_timings = tda1997x_g_dv_timings,
	.query_dv_timings = tda1997x_query_dv_timings,
};


/* -----------------------------------------------------------------------------
 * v4l2_subdev_pad_ops
 */

static int tda1997x_init_cfg(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg)
{
	struct tda1997x_state *state = to_state(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, cfg, 0);
	mf->code = state->mbus_codes[0];

	return 0;
}

static int tda1997x_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s %d\n", __func__, code->index);
	if (code->index >= ARRAY_SIZE(state->mbus_codes))
		return -EINVAL;

	if (!state->mbus_codes[code->index])
		return -EINVAL;

	code->code = state->mbus_codes[code->index];

	return 0;
}

static void tda1997x_fill_format(struct tda1997x_state *state,
				 struct v4l2_mbus_framefmt *format)
{
	const struct v4l2_bt_timings *bt;

	memset(format, 0, sizeof(*format));
	bt = &state->timings.bt;
	format->width = bt->width;
	format->height = bt->height;
	format->colorspace = state->colorimetry.colorspace;
	format->field = (bt->interlaced) ?
		V4L2_FIELD_SEQ_TB : V4L2_FIELD_NONE;
}

static int tda1997x_get_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *format)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s pad=%d which=%d\n",
		__func__, format->pad, format->which);

	tda1997x_fill_format(state, &format->format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		format->format.code = fmt->code;
	} else
		format->format.code = state->mbus_code;

	return 0;
}

static int tda1997x_set_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *format)
{
	struct tda1997x_state *state = to_state(sd);
	u32 code = 0;
	int i;

	v4l_dbg(1, debug, state->client, "%s pad=%d which=%d fmt=0x%x\n",
		__func__, format->pad, format->which, format->format.code);

	for (i = 0; i < ARRAY_SIZE(state->mbus_codes); i++) {
		if (format->format.code == state->mbus_codes[i]) {
			code = state->mbus_codes[i];
			break;
		}
	}
	if (!code)
		code = state->mbus_codes[0];

	tda1997x_fill_format(state, &format->format);
	format->format.code = code;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *fmt;

		fmt = v4l2_subdev_get_try_format(sd, cfg, format->pad);
		*fmt = format->format;
	} else {
		int ret = tda1997x_setup_format(state, format->format.code);

		if (ret)
			return ret;
		/* mbus_code has changed - re-configure csc/vidout */
		tda1997x_configure_csc(sd);
		tda1997x_configure_vidout(state);
	}

	return 0;
}

static int tda1997x_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct tda1997x_state *state = to_state(sd);

	v4l_dbg(1, debug, state->client, "%s pad=%d\n", __func__, edid->pad);
	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = state->edid.blocks;
		return 0;
	}

	if (!state->edid.present)
		return -ENODATA;

	if (edid->start_block >= state->edid.blocks)
		return -EINVAL;

	if (edid->start_block + edid->blocks > state->edid.blocks)
		edid->blocks = state->edid.blocks - edid->start_block;

	memcpy(edid->edid, state->edid.edid + edid->start_block * 128,
	       edid->blocks * 128);

	return 0;
}

static int tda1997x_set_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct tda1997x_state *state = to_state(sd);
	int i;

	v4l_dbg(1, debug, state->client, "%s pad=%d\n", __func__, edid->pad);
	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->start_block != 0)
		return -EINVAL;

	if (edid->blocks == 0) {
		state->edid.blocks = 0;
		state->edid.present = 0;
		tda1997x_disable_edid(sd);
		return 0;
	}

	if (edid->blocks > 2) {
		edid->blocks = 2;
		return -E2BIG;
	}

	tda1997x_disable_edid(sd);

	/* write base EDID */
	for (i = 0; i < 128; i++)
		io_write(sd, REG_EDID_IN_BYTE0 + i, edid->edid[i]);

	/* write CEA Extension */
	for (i = 0; i < 128; i++)
		io_write(sd, REG_EDID_IN_BYTE128 + i, edid->edid[i+128]);

	/* store state */
	memcpy(state->edid.edid, edid->edid, 256);
	state->edid.blocks = edid->blocks;

	tda1997x_enable_edid(sd);

	return 0;
}

static int tda1997x_get_dv_timings_cap(struct v4l2_subdev *sd,
				       struct v4l2_dv_timings_cap *cap)
{
	*cap = tda1997x_dv_timings_cap;
	return 0;
}

static int tda1997x_enum_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &tda1997x_dv_timings_cap,
					NULL, NULL);
}

static const struct v4l2_subdev_pad_ops tda1997x_pad_ops = {
	.init_cfg = tda1997x_init_cfg,
	.enum_mbus_code = tda1997x_enum_mbus_code,
	.get_fmt = tda1997x_get_format,
	.set_fmt = tda1997x_set_format,
	.get_edid = tda1997x_get_edid,
	.set_edid = tda1997x_set_edid,
	.dv_timings_cap = tda1997x_get_dv_timings_cap,
	.enum_dv_timings = tda1997x_enum_dv_timings,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_core_ops
 */

static int tda1997x_log_infoframe(struct v4l2_subdev *sd, int addr)
{
	struct tda1997x_state *state = to_state(sd);
	union hdmi_infoframe frame;
	u8 buffer[40];
	int len, err;

	/* read data */
	len = io_readn(sd, addr, sizeof(buffer), buffer);
	v4l2_dbg(1, debug, sd, "infoframe: addr=%d len=%d\n", addr, len);
	err = hdmi_infoframe_unpack(&frame, buffer, sizeof(buffer));
	if (err) {
		v4l_err(state->client,
			"failed parsing %d byte infoframe: 0x%04x/0x%02x\n",
			len, addr, buffer[0]);
		return err;
	}
	hdmi_infoframe_log(KERN_INFO, &state->client->dev, &frame);

	return 0;
}

static int tda1997x_log_status(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);
	struct v4l2_dv_timings timings;
	struct hdmi_avi_infoframe *avi = &state->avi_infoframe;

	v4l2_info(sd, "-----Chip status-----\n");
	v4l2_info(sd, "Chip: %s N%d\n", state->info->name,
		  state->chip_revision + 1);
	v4l2_info(sd, "EDID Enabled: %s\n", state->edid.present ? "yes" : "no");

	v4l2_info(sd, "-----Signal status-----\n");
	v4l2_info(sd, "Cable detected (+5V power): %s\n",
		  tda1997x_detect_tx_5v(sd) ? "yes" : "no");
	v4l2_info(sd, "HPD detected: %s\n",
		  tda1997x_detect_tx_hpd(sd) ? "yes" : "no");

	v4l2_info(sd, "-----Video Timings-----\n");
	switch (tda1997x_detect_std(state, &timings)) {
	case -ENOLINK:
		v4l2_info(sd, "No video detected\n");
		break;
	case -ERANGE:
		v4l2_info(sd, "Invalid signal detected\n");
		break;
	}
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			      &state->timings, true);

	v4l2_info(sd, "-----Color space-----\n");
	v4l2_info(sd, "Input color space: %s %s %s",
		  hdmi_colorspace_names[avi->colorspace],
		  (avi->colorspace == HDMI_COLORSPACE_RGB) ? "" :
			hdmi_colorimetry_names[avi->colorimetry],
		  v4l2_quantization_names[state->colorimetry.quantization]);
	v4l2_info(sd, "Output color space: %s",
		  vidfmt_names[state->vid_fmt]);
	v4l2_info(sd, "Color space conversion: %s", state->conv ?
		  state->conv->name : "None");

	v4l2_info(sd, "-----Audio-----\n");
	if (state->audio_channels) {
		v4l2_info(sd, "audio: %dch %dHz\n", state->audio_channels,
			  state->audio_samplerate);
	} else {
		v4l2_info(sd, "audio: none\n");
	}

	v4l2_info(sd, "-----Infoframes-----\n");
	tda1997x_log_infoframe(sd, AUD_IF);
	tda1997x_log_infoframe(sd, SPD_IF);
	tda1997x_log_infoframe(sd, AVI_IF);

	return 0;
}

static int tda1997x_subscribe_event(struct v4l2_subdev *sd,
				    struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops tda1997x_core_ops = {
	.log_status = tda1997x_log_status,
	.subscribe_event = tda1997x_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * v4l2_subdev_ops
 */

static const struct v4l2_subdev_ops tda1997x_subdev_ops = {
	.core = &tda1997x_core_ops,
	.video = &tda1997x_video_ops,
	.pad = &tda1997x_pad_ops,
};

/* -----------------------------------------------------------------------------
 * v4l2_controls
 */

static int tda1997x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tda1997x_state *state = to_state(sd);

	switch (ctrl->id) {
	/* allow overriding the default RGB quantization range */
	case V4L2_CID_DV_RX_RGB_RANGE:
		state->rgb_quantization_range = ctrl->val;
		set_rgb_quantization_range(state);
		tda1997x_configure_csc(sd);
		return 0;
	}

	return -EINVAL;
};

static int tda1997x_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tda1997x_state *state = to_state(sd);

	if (ctrl->id == V4L2_CID_DV_RX_IT_CONTENT_TYPE) {
		ctrl->val = state->avi_infoframe.content_type;
		return 0;
	}
	return -EINVAL;
};

static const struct v4l2_ctrl_ops tda1997x_ctrl_ops = {
	.s_ctrl = tda1997x_s_ctrl,
	.g_volatile_ctrl = tda1997x_g_volatile_ctrl,
};

static int tda1997x_core_init(struct v4l2_subdev *sd)
{
	struct tda1997x_state *state = to_state(sd);
	struct tda1997x_platform_data *pdata = &state->pdata;
	u8 reg;
	int i;

	/* disable HPD */
	io_write(sd, REG_HPD_AUTO_CTRL, HPD_AUTO_HPD_UNSEL);
	if (state->chip_revision == 0) {
		io_write(sd, REG_MAN_SUS_HDMI_SEL, MAN_DIS_HDCP | MAN_RST_HDCP);
		io_write(sd, REG_CGU_DBG_SEL, 1 << CGU_DBG_CLK_SEL_SHIFT);
	}

	/* reset infoframe at end of start-up-sequencer */
	io_write(sd, REG_SUS_SET_RGB2, 0x06);
	io_write(sd, REG_SUS_SET_RGB3, 0x06);

	/* Enable TMDS pull-ups */
	io_write(sd, REG_RT_MAN_CTRL, RT_MAN_CTRL_RT |
		 RT_MAN_CTRL_RT_B | RT_MAN_CTRL_RT_A);

	/* enable sync measurement timing */
	tda1997x_cec_write(sd, REG_PWR_CONTROL & 0xff, 0x04);
	/* adjust CEC clock divider */
	tda1997x_cec_write(sd, REG_OSC_DIVIDER & 0xff, 0x03);
	tda1997x_cec_write(sd, REG_EN_OSC_PERIOD_LSB & 0xff, 0xa0);
	io_write(sd, REG_TIMER_D, 0x54);
	/* enable power switch */
	reg = tda1997x_cec_read(sd, REG_CONTROL & 0xff);
	reg |= 0x20;
	tda1997x_cec_write(sd, REG_CONTROL & 0xff, reg);
	mdelay(50);

	/* read the chip version */
	reg = io_read(sd, REG_VERSION);
	/* get the chip configuration */
	reg = io_read(sd, REG_CMTP_REG10);

	/* enable interrupts we care about */
	io_write(sd, REG_INT_MASK_TOP,
		 INTERRUPT_HDCP | INTERRUPT_AUDIO | INTERRUPT_INFO |
		 INTERRUPT_RATE | INTERRUPT_SUS);
	/* config_mtp,fmt,sus_end,sus_st */
	io_write(sd, REG_INT_MASK_SUS, MASK_MPT | MASK_FMT | MASK_SUS_END);
	/* rate stability change for inputs A/B */
	io_write(sd, REG_INT_MASK_RATE, MASK_RATE_B_ST | MASK_RATE_A_ST);
	/* aud,spd,avi*/
	io_write(sd, REG_INT_MASK_INFO,
		 MASK_AUD_IF | MASK_SPD_IF | MASK_AVI_IF);
	/* audio_freq,audio_flg,mute_flg,fifo_err */
	io_write(sd, REG_INT_MASK_AUDIO,
		 MASK_AUDIO_FREQ_FLG | MASK_AUDIO_FLG | MASK_MUTE_FLG |
		 MASK_ERROR_FIFO_PT);
	/* HDCP C5 state reached */
	io_write(sd, REG_INT_MASK_HDCP, MASK_STATE_C5);
	/* 5V detect and HDP pulse end */
	io_write(sd, REG_INT_MASK_DDC, MASK_DET_5V);
	/* don't care about AFE/MODE */
	io_write(sd, REG_INT_MASK_AFE, 0);
	io_write(sd, REG_INT_MASK_MODE, 0);

	/* clear all interrupts */
	io_write(sd, REG_INT_FLG_CLR_TOP, 0xff);
	io_write(sd, REG_INT_FLG_CLR_SUS, 0xff);
	io_write(sd, REG_INT_FLG_CLR_DDC, 0xff);
	io_write(sd, REG_INT_FLG_CLR_RATE, 0xff);
	io_write(sd, REG_INT_FLG_CLR_MODE, 0xff);
	io_write(sd, REG_INT_FLG_CLR_INFO, 0xff);
	io_write(sd, REG_INT_FLG_CLR_AUDIO, 0xff);
	io_write(sd, REG_INT_FLG_CLR_HDCP, 0xff);
	io_write(sd, REG_INT_FLG_CLR_AFE, 0xff);

	/* init TMDS equalizer */
	if (state->chip_revision == 0)
		io_write(sd, REG_CGU_DBG_SEL, 1 << CGU_DBG_CLK_SEL_SHIFT);
	io_write24(sd, REG_CLK_MIN_RATE, CLK_MIN_RATE);
	io_write24(sd, REG_CLK_MAX_RATE, CLK_MAX_RATE);
	if (state->chip_revision == 0)
		io_write(sd, REG_WDL_CFG, WDL_CFG_VAL);
	/* DC filter */
	io_write(sd, REG_DEEP_COLOR_CTRL, DC_FILTER_VAL);
	/* disable test pattern */
	io_write(sd, REG_SVC_MODE, 0x00);
	/* update HDMI INFO CTRL */
	io_write(sd, REG_INFO_CTRL, 0xff);
	/* write HDMI INFO EXCEED value */
	io_write(sd, REG_INFO_EXCEED, 3);

	if (state->chip_revision == 0)
		tda1997x_reset_n1(state);

	/*
	 * No HDCP acknowledge when HDCP is disabled
	 * and reset SUS to force format detection
	 */
	tda1997x_hdmi_info_reset(sd, NACK_HDCP, true);

	/* Set HPD low */
	tda1997x_manual_hpd(sd, HPD_LOW_BP);

	/* Configure receiver capabilities */
	io_write(sd, REG_HDCP_BCAPS, HDCP_HDMI | HDCP_FAST_REAUTH);

	/* Configure HDMI: Auto HDCP mode, packet controlled mute */
	reg = HDMI_CTRL_MUTE_AUTO << HDMI_CTRL_MUTE_SHIFT;
	reg |= HDMI_CTRL_HDCP_AUTO << HDMI_CTRL_HDCP_SHIFT;
	io_write(sd, REG_HDMI_CTRL, reg);

	/* reset start-up-sequencer to force format detection */
	tda1997x_hdmi_info_reset(sd, 0, true);

	/* disable matrix conversion */
	reg = io_read(sd, REG_VDP_CTRL);
	reg |= VDP_CTRL_MATRIX_BP;
	io_write(sd, REG_VDP_CTRL, reg);

	/* set video output mode */
	tda1997x_configure_vidout(state);

	/* configure video output port */
	for (i = 0; i < 9; i++) {
		v4l_dbg(1, debug, state->client, "vidout_cfg[%d]=0x%02x\n", i,
			pdata->vidout_port_cfg[i]);
		io_write(sd, REG_VP35_32_CTRL + i, pdata->vidout_port_cfg[i]);
	}

	/* configure audio output port */
	tda1997x_configure_audout(sd, 0);

	/* configure audio clock freq */
	switch (pdata->audout_mclk_fs) {
	case 512:
		reg = AUDIO_CLOCK_SEL_512FS;
		break;
	case 256:
		reg = AUDIO_CLOCK_SEL_256FS;
		break;
	case 128:
		reg = AUDIO_CLOCK_SEL_128FS;
		break;
	case 64:
		reg = AUDIO_CLOCK_SEL_64FS;
		break;
	case 32:
		reg = AUDIO_CLOCK_SEL_32FS;
		break;
	default:
		reg = AUDIO_CLOCK_SEL_16FS;
		break;
	}
	io_write(sd, REG_AUDIO_CLOCK, reg);

	/* reset advanced infoframes (ISRC1/ISRC2/ACP) */
	tda1997x_hdmi_info_reset(sd, RESET_AI, false);
	/* reset infoframe */
	tda1997x_hdmi_info_reset(sd, RESET_IF, false);
	/* reset audio infoframes */
	tda1997x_hdmi_info_reset(sd, RESET_AUDIO, false);
	/* reset gamut */
	tda1997x_hdmi_info_reset(sd, RESET_GAMUT, false);

	/* get initial HDMI status */
	state->hdmi_status = io_read(sd, REG_HDMI_FLAGS);

	io_write(sd, REG_EDID_ENABLE, EDID_ENABLE_A_EN | EDID_ENABLE_B_EN);
	return 0;
}

static int tda1997x_set_power(struct tda1997x_state *state, bool on)
{
	int ret = 0;

	if (on) {
		ret = regulator_bulk_enable(TDA1997X_NUM_SUPPLIES,
					     state->supplies);
		msleep(300);
	} else {
		ret = regulator_bulk_disable(TDA1997X_NUM_SUPPLIES,
					     state->supplies);
	}

	return ret;
}

static const struct i2c_device_id tda1997x_i2c_id[] = {
	{"tda19971", (kernel_ulong_t)&tda1997x_chip_info[TDA19971]},
	{"tda19973", (kernel_ulong_t)&tda1997x_chip_info[TDA19973]},
	{ },
};
MODULE_DEVICE_TABLE(i2c, tda1997x_i2c_id);

static const struct of_device_id tda1997x_of_id[] __maybe_unused = {
	{ .compatible = "nxp,tda19971", .data = &tda1997x_chip_info[TDA19971] },
	{ .compatible = "nxp,tda19973", .data = &tda1997x_chip_info[TDA19973] },
	{ },
};
MODULE_DEVICE_TABLE(of, tda1997x_of_id);

static int tda1997x_parse_dt(struct tda1997x_state *state)
{
	struct tda1997x_platform_data *pdata = &state->pdata;
	struct v4l2_fwnode_endpoint bus_cfg = { .bus_type = 0 };
	struct device_node *ep;
	struct device_node *np;
	unsigned int flags;
	const char *str;
	int ret;
	u32 v;

	/*
	 * setup default values:
	 * - HREF: active high from start to end of row
	 * - VS: Vertical Sync active high at beginning of frame
	 * - DE: Active high when data valid
	 * - A_CLK: 128*Fs
	 */
	pdata->vidout_sel_hs = HS_HREF_SEL_HREF_VHREF;
	pdata->vidout_sel_vs = VS_VREF_SEL_VREF_HDMI;
	pdata->vidout_sel_de = DE_FREF_SEL_DE_VHREF;

	np = state->client->dev.of_node;
	ep = of_graph_get_next_endpoint(np, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &bus_cfg);
	if (ret) {
		of_node_put(ep);
		return ret;
	}
	of_node_put(ep);
	pdata->vidout_bus_type = bus_cfg.bus_type;

	/* polarity of HS/VS/DE */
	flags = bus_cfg.bus.parallel.flags;
	if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
		pdata->vidout_inv_hs = 1;
	if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
		pdata->vidout_inv_vs = 1;
	if (flags & V4L2_MBUS_DATA_ACTIVE_LOW)
		pdata->vidout_inv_de = 1;
	pdata->vidout_bus_width = bus_cfg.bus.parallel.bus_width;

	/* video output port config */
	ret = of_property_count_u32_elems(np, "nxp,vidout-portcfg");
	if (ret > 0) {
		u32 reg, val, i;

		for (i = 0; i < ret / 2 && i < 9; i++) {
			of_property_read_u32_index(np, "nxp,vidout-portcfg",
						   i * 2, &reg);
			of_property_read_u32_index(np, "nxp,vidout-portcfg",
						   i * 2 + 1, &val);
			if (reg < 9)
				pdata->vidout_port_cfg[reg] = val;
		}
	} else {
		v4l_err(state->client, "nxp,vidout-portcfg missing\n");
		return -EINVAL;
	}

	/* default to channel layout dictated by packet header */
	pdata->audout_layoutauto = true;

	pdata->audout_format = AUDFMT_TYPE_DISABLED;
	if (!of_property_read_string(np, "nxp,audout-format", &str)) {
		if (strcmp(str, "i2s") == 0)
			pdata->audout_format = AUDFMT_TYPE_I2S;
		else if (strcmp(str, "spdif") == 0)
			pdata->audout_format = AUDFMT_TYPE_SPDIF;
		else {
			v4l_err(state->client, "nxp,audout-format invalid\n");
			return -EINVAL;
		}
		if (!of_property_read_u32(np, "nxp,audout-layout", &v)) {
			switch (v) {
			case 0:
			case 1:
				break;
			default:
				v4l_err(state->client,
					"nxp,audout-layout invalid\n");
				return -EINVAL;
			}
			pdata->audout_layout = v;
		}
		if (!of_property_read_u32(np, "nxp,audout-width", &v)) {
			switch (v) {
			case 16:
			case 32:
				break;
			default:
				v4l_err(state->client,
					"nxp,audout-width invalid\n");
				return -EINVAL;
			}
			pdata->audout_width = v;
		}
		if (!of_property_read_u32(np, "nxp,audout-mclk-fs", &v)) {
			switch (v) {
			case 512:
			case 256:
			case 128:
			case 64:
			case 32:
			case 16:
				break;
			default:
				v4l_err(state->client,
					"nxp,audout-mclk-fs invalid\n");
				return -EINVAL;
			}
			pdata->audout_mclk_fs = v;
		}
	}

	return 0;
}

static int tda1997x_get_regulators(struct tda1997x_state *state)
{
	int i;

	for (i = 0; i < TDA1997X_NUM_SUPPLIES; i++)
		state->supplies[i].supply = tda1997x_supply_name[i];

	return devm_regulator_bulk_get(&state->client->dev,
				       TDA1997X_NUM_SUPPLIES,
				       state->supplies);
}

static int tda1997x_identify_module(struct tda1997x_state *state)
{
	struct v4l2_subdev *sd = &state->sd;
	enum tda1997x_type type;
	u8 reg;

	/* Read chip configuration*/
	reg = io_read(sd, REG_CMTP_REG10);
	state->tmdsb_clk = (reg >> 6) & 0x01; /* use tmds clock B_inv for B */
	state->tmdsb_soc = (reg >> 5) & 0x01; /* tmds of input B */
	state->port_30bit = (reg >> 2) & 0x03; /* 30bit vs 24bit */
	state->output_2p5 = (reg >> 1) & 0x01; /* output supply 2.5v */
	switch ((reg >> 4) & 0x03) {
	case 0x00:
		type = TDA19971;
		break;
	case 0x02:
	case 0x03:
		type = TDA19973;
		break;
	default:
		dev_err(&state->client->dev, "unsupported chip ID\n");
		return -EIO;
	}
	if (state->info->type != type) {
		dev_err(&state->client->dev, "chip id mismatch\n");
		return -EIO;
	}

	/* read chip revision */
	state->chip_revision = io_read(sd, REG_CMTP_REG11);

	return 0;
}

static const struct media_entity_operations tda1997x_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};


/* -----------------------------------------------------------------------------
 * HDMI Audio Codec
 */

/* refine sample-rate based on HDMI source */
static int tda1997x_pcm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tda1997x_state *state = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_component *component = dai->component;
	struct snd_pcm_runtime *rtd = substream->runtime;
	int rate, err;

	rate = state->audio_samplerate;
	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_RATE,
					   rate, rate);
	if (err < 0) {
		dev_err(component->dev, "failed to constrain samplerate to %dHz\n",
			rate);
		return err;
	}
	dev_info(component->dev, "set samplerate constraint to %dHz\n", rate);

	return 0;
}

static const struct snd_soc_dai_ops tda1997x_dai_ops = {
	.startup = tda1997x_pcm_startup,
};

static struct snd_soc_dai_driver tda1997x_audio_dai = {
	.name = "tda1997x",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000,
	},
	.ops = &tda1997x_dai_ops,
};

static int tda1997x_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

static void tda1997x_codec_remove(struct snd_soc_component *component)
{
}

static struct snd_soc_component_driver tda1997x_codec_driver = {
	.probe			= tda1997x_codec_probe,
	.remove			= tda1997x_codec_remove,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int tda1997x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct tda1997x_state *state;
	struct tda1997x_platform_data *pdata;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_ctrl *ctrl;
	static const struct v4l2_dv_timings cea1920x1080 =
		V4L2_DV_BT_CEA_1920X1080P60;
	u32 *mbus_codes;
	int i, ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = kzalloc(sizeof(struct tda1997x_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->client = client;
	pdata = &state->pdata;
	if (IS_ENABLED(CONFIG_OF) && client->dev.of_node) {
		const struct of_device_id *oid;

		oid = of_match_node(tda1997x_of_id, client->dev.of_node);
		state->info = oid->data;

		ret = tda1997x_parse_dt(state);
		if (ret < 0) {
			v4l_err(client, "DT parsing error\n");
			goto err_free_state;
		}
	} else if (client->dev.platform_data) {
		struct tda1997x_platform_data *pdata =
			client->dev.platform_data;
		state->info =
			(const struct tda1997x_chip_info *)id->driver_data;
		state->pdata = *pdata;
	} else {
		v4l_err(client, "No platform data\n");
		ret = -ENODEV;
		goto err_free_state;
	}

	ret = tda1997x_get_regulators(state);
	if (ret)
		goto err_free_state;

	ret = tda1997x_set_power(state, 1);
	if (ret)
		goto err_free_state;

	mutex_init(&state->page_lock);
	mutex_init(&state->lock);
	state->page = 0xff;

	INIT_DELAYED_WORK(&state->delayed_work_enable_hpd,
			  tda1997x_delayed_work_enable_hpd);

	/* set video format based on chip and bus width */
	ret = tda1997x_identify_module(state);
	if (ret)
		goto err_free_mutex;

	/* initialize subdev */
	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &tda1997x_subdev_ops);
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		 id->name, i2c_adapter_id(client->adapter),
		 client->addr);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.function = MEDIA_ENT_F_DV_DECODER;
	sd->entity.ops = &tda1997x_media_ops;

	/* set allowed mbus modes based on chip, bus-type, and bus-width */
	i = 0;
	mbus_codes = state->mbus_codes;
	switch (state->info->type) {
	case TDA19973:
		switch (pdata->vidout_bus_type) {
		case V4L2_MBUS_PARALLEL:
			switch (pdata->vidout_bus_width) {
			case 36:
				mbus_codes[i++] = MEDIA_BUS_FMT_RGB121212_1X36;
				mbus_codes[i++] = MEDIA_BUS_FMT_YUV12_1X36;
				fallthrough;
			case 24:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY12_1X24;
				break;
			}
			break;
		case V4L2_MBUS_BT656:
			switch (pdata->vidout_bus_width) {
			case 36:
			case 24:
			case 12:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY12_2X12;
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY10_2X10;
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY8_2X8;
				break;
			}
			break;
		default:
			break;
		}
		break;
	case TDA19971:
		switch (pdata->vidout_bus_type) {
		case V4L2_MBUS_PARALLEL:
			switch (pdata->vidout_bus_width) {
			case 24:
				mbus_codes[i++] = MEDIA_BUS_FMT_RGB888_1X24;
				mbus_codes[i++] = MEDIA_BUS_FMT_YUV8_1X24;
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY12_1X24;
				fallthrough;
			case 20:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY10_1X20;
				fallthrough;
			case 16:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY8_1X16;
				break;
			}
			break;
		case V4L2_MBUS_BT656:
			switch (pdata->vidout_bus_width) {
			case 24:
			case 20:
			case 16:
			case 12:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY12_2X12;
				fallthrough;
			case 10:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY10_2X10;
				fallthrough;
			case 8:
				mbus_codes[i++] = MEDIA_BUS_FMT_UYVY8_2X8;
				break;
			}
			break;
		default:
			break;
		}
		break;
	}
	if (WARN_ON(i > ARRAY_SIZE(state->mbus_codes))) {
		ret = -EINVAL;
		goto err_free_mutex;
	}

	/* default format */
	tda1997x_setup_format(state, state->mbus_codes[0]);
	state->timings = cea1920x1080;

	/*
	 * default to SRGB full range quantization
	 * (in case we don't get an infoframe such as DVI signal
	 */
	state->colorimetry.colorspace = V4L2_COLORSPACE_SRGB;
	state->colorimetry.quantization = V4L2_QUANTIZATION_FULL_RANGE;

	/* disable/reset HDCP to get correct I2C access to Rx HDMI */
	io_write(sd, REG_MAN_SUS_HDMI_SEL, MAN_RST_HDCP | MAN_DIS_HDCP);

	/*
	 * if N2 version, reset compdel_bp as it may generate some small pixel
	 * shifts in case of embedded sync/or delay lower than 4
	 */
	if (state->chip_revision != 0) {
		io_write(sd, REG_MAN_SUS_HDMI_SEL, 0x00);
		io_write(sd, REG_VDP_CTRL, 0x1f);
	}

	v4l_info(client, "NXP %s N%d detected\n", state->info->name,
		 state->chip_revision + 1);
	v4l_info(client, "video: %dbit %s %d formats available\n",
		pdata->vidout_bus_width,
		(pdata->vidout_bus_type == V4L2_MBUS_PARALLEL) ?
			"parallel" : "BT656",
		i);
	if (pdata->audout_format) {
		v4l_info(client, "audio: %dch %s layout%d sysclk=%d*fs\n",
			 pdata->audout_layout ? 2 : 8,
			 audfmt_names[pdata->audout_format],
			 pdata->audout_layout,
			 pdata->audout_mclk_fs);
	}

	ret = 0x34 + ((io_read(sd, REG_SLAVE_ADDR)>>4) & 0x03);
	state->client_cec = devm_i2c_new_dummy_device(&client->dev,
						      client->adapter, ret);
	if (IS_ERR(state->client_cec)) {
		ret = PTR_ERR(state->client_cec);
		goto err_free_mutex;
	}

	v4l_info(client, "CEC slave address 0x%02x\n", ret);

	ret = tda1997x_core_init(sd);
	if (ret)
		goto err_free_mutex;

	/* control handlers */
	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 3);
	ctrl = v4l2_ctrl_new_std_menu(hdl, &tda1997x_ctrl_ops,
			V4L2_CID_DV_RX_IT_CONTENT_TYPE,
			V4L2_DV_IT_CONTENT_TYPE_NO_ITC, 0,
			V4L2_DV_IT_CONTENT_TYPE_NO_ITC);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	/* custom controls */
	state->detect_tx_5v_ctrl = v4l2_ctrl_new_std(hdl, NULL,
			V4L2_CID_DV_RX_POWER_PRESENT, 0, 1, 0, 0);
	state->rgb_quantization_range_ctrl = v4l2_ctrl_new_std_menu(hdl,
			&tda1997x_ctrl_ops,
			V4L2_CID_DV_RX_RGB_RANGE, V4L2_DV_RGB_RANGE_FULL, 0,
			V4L2_DV_RGB_RANGE_AUTO);
	state->sd.ctrl_handler = hdl;
	if (hdl->error) {
		ret = hdl->error;
		goto err_free_handler;
	}
	v4l2_ctrl_handler_setup(hdl);

	/* initialize source pads */
	state->pads[TDA1997X_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, TDA1997X_NUM_PADS,
		state->pads);
	if (ret) {
		v4l_err(client, "failed entity_init: %d", ret);
		goto err_free_handler;
	}

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_free_media;

	/* register audio DAI */
	if (pdata->audout_format) {
		u64 formats;

		if (pdata->audout_width == 32)
			formats = SNDRV_PCM_FMTBIT_S32_LE;
		else
			formats = SNDRV_PCM_FMTBIT_S16_LE;
		tda1997x_audio_dai.capture.formats = formats;
		ret = devm_snd_soc_register_component(&state->client->dev,
					     &tda1997x_codec_driver,
					     &tda1997x_audio_dai, 1);
		if (ret) {
			dev_err(&client->dev, "register audio codec failed\n");
			goto err_free_media;
		}
		dev_set_drvdata(&state->client->dev, state);
		v4l_info(state->client, "registered audio codec\n");
	}

	/* request irq */
	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, tda1997x_isr_thread,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					KBUILD_MODNAME, state);
	if (ret) {
		v4l_err(client, "irq%d reg failed: %d\n", client->irq, ret);
		goto err_free_media;
	}

	return 0;

err_free_media:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&state->hdl);
err_free_mutex:
	cancel_delayed_work(&state->delayed_work_enable_hpd);
	mutex_destroy(&state->page_lock);
	mutex_destroy(&state->lock);
err_free_state:
	kfree(state);
	dev_err(&client->dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int tda1997x_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tda1997x_state *state = to_state(sd);
	struct tda1997x_platform_data *pdata = &state->pdata;

	if (pdata->audout_format) {
		mutex_destroy(&state->audio_lock);
	}

	disable_irq(state->client->irq);
	tda1997x_power_mode(state, 0);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&state->hdl);
	regulator_bulk_disable(TDA1997X_NUM_SUPPLIES, state->supplies);
	cancel_delayed_work_sync(&state->delayed_work_enable_hpd);
	mutex_destroy(&state->page_lock);
	mutex_destroy(&state->lock);

	kfree(state);

	return 0;
}

static struct i2c_driver tda1997x_i2c_driver = {
	.driver = {
		.name = "tda1997x",
		.of_match_table = of_match_ptr(tda1997x_of_id),
	},
	.probe = tda1997x_probe,
	.remove = tda1997x_remove,
	.id_table = tda1997x_i2c_id,
};

module_i2c_driver(tda1997x_i2c_driver);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("TDA1997X HDMI Receiver driver");
MODULE_LICENSE("GPL v2");
