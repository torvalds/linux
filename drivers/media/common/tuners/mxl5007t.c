/*
 *  mxl5007t.c - driver for the MaxLinear MxL5007T silicon tuner
 *
 *  Copyright (C) 2008 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include "tuner-i2c.h"
#include "mxl5007t.h"

static DEFINE_MUTEX(mxl5007t_list_mutex);
static LIST_HEAD(hybrid_tuner_instance_list);

static int mxl5007t_debug;
module_param_named(debug, mxl5007t_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level");

/* ------------------------------------------------------------------------- */

#define mxl_printk(kern, fmt, arg...) \
	printk(kern "%s: " fmt "\n", __func__, ##arg)

#define mxl_err(fmt, arg...) \
	mxl_printk(KERN_ERR, "%d: " fmt, __LINE__, ##arg)

#define mxl_warn(fmt, arg...) \
	mxl_printk(KERN_WARNING, fmt, ##arg)

#define mxl_info(fmt, arg...) \
	mxl_printk(KERN_INFO, fmt, ##arg)

#define mxl_debug(fmt, arg...)				\
({							\
	if (mxl5007t_debug)				\
		mxl_printk(KERN_DEBUG, fmt, ##arg);	\
})

#define mxl_fail(ret)							\
({									\
	int __ret;							\
	__ret = (ret < 0);						\
	if (__ret)							\
		mxl_printk(KERN_ERR, "error %d on line %d",		\
			   ret, __LINE__);				\
	__ret;								\
})

/* ------------------------------------------------------------------------- */

#define MHz 1000000

enum mxl5007t_mode {
	MxL_MODE_OTA_DVBT_ATSC        =    0,
	MxL_MODE_OTA_NTSC_PAL_GH      =    1,
	MxL_MODE_OTA_PAL_IB           =    2,
	MxL_MODE_OTA_PAL_D_SECAM_KL   =    3,
	MxL_MODE_OTA_ISDBT            =    4,
	MxL_MODE_CABLE_DIGITAL        = 0x10,
	MxL_MODE_CABLE_NTSC_PAL_GH    = 0x11,
	MxL_MODE_CABLE_PAL_IB         = 0x12,
	MxL_MODE_CABLE_PAL_D_SECAM_KL = 0x13,
	MxL_MODE_CABLE_SCTE40         = 0x14,
};

enum mxl5007t_chip_version {
	MxL_UNKNOWN_ID     = 0x00,
	MxL_5007_V1_F1     = 0x11,
	MxL_5007_V1_F2     = 0x12,
	MxL_5007_V2_100_F1 = 0x21,
	MxL_5007_V2_100_F2 = 0x22,
	MxL_5007_V2_200_F1 = 0x23,
	MxL_5007_V2_200_F2 = 0x24,
};

struct reg_pair_t {
	u8 reg;
	u8 val;
};

/* ------------------------------------------------------------------------- */

static struct reg_pair_t init_tab[] = {
	{ 0x0b, 0x44 }, /* XTAL */
	{ 0x0c, 0x60 }, /* IF */
	{ 0x10, 0x00 }, /* MISC */
	{ 0x12, 0xca }, /* IDAC */
	{ 0x16, 0x90 }, /* MODE */
	{ 0x32, 0x38 }, /* MODE Analog/Digital */
	{ 0xd8, 0x18 }, /* CLK_OUT_ENABLE */
	{ 0x2c, 0x34 }, /* OVERRIDE */
	{ 0x4d, 0x40 }, /* OVERRIDE */
	{ 0x7f, 0x02 }, /* OVERRIDE */
	{ 0x9a, 0x52 }, /* OVERRIDE */
	{ 0x48, 0x5a }, /* OVERRIDE */
	{ 0x76, 0x1a }, /* OVERRIDE */
	{ 0x6a, 0x48 }, /* OVERRIDE */
	{ 0x64, 0x28 }, /* OVERRIDE */
	{ 0x66, 0xe6 }, /* OVERRIDE */
	{ 0x35, 0x0e }, /* OVERRIDE */
	{ 0x7e, 0x01 }, /* OVERRIDE */
	{ 0x83, 0x00 }, /* OVERRIDE */
	{ 0x04, 0x0b }, /* OVERRIDE */
	{ 0x05, 0x01 }, /* TOP_MASTER_ENABLE */
	{ 0, 0 }
};

static struct reg_pair_t init_tab_cable[] = {
	{ 0x0b, 0x44 }, /* XTAL */
	{ 0x0c, 0x60 }, /* IF */
	{ 0x10, 0x00 }, /* MISC */
	{ 0x12, 0xca }, /* IDAC */
	{ 0x16, 0x90 }, /* MODE */
	{ 0x32, 0x38 }, /* MODE A/D */
	{ 0x71, 0x3f }, /* TOP1 */
	{ 0x72, 0x3f }, /* TOP2 */
	{ 0x74, 0x3f }, /* TOP3 */
	{ 0xd8, 0x18 }, /* CLK_OUT_ENABLE */
	{ 0x2c, 0x34 }, /* OVERRIDE */
	{ 0x4d, 0x40 }, /* OVERRIDE */
	{ 0x7f, 0x02 }, /* OVERRIDE */
	{ 0x9a, 0x52 }, /* OVERRIDE */
	{ 0x48, 0x5a }, /* OVERRIDE */
	{ 0x76, 0x1a }, /* OVERRIDE */
	{ 0x6a, 0x48 }, /* OVERRIDE */
	{ 0x64, 0x28 }, /* OVERRIDE */
	{ 0x66, 0xe6 }, /* OVERRIDE */
	{ 0x35, 0x0e }, /* OVERRIDE */
	{ 0x7e, 0x01 }, /* OVERRIDE */
	{ 0x04, 0x0b }, /* OVERRIDE */
	{ 0x68, 0xb4 }, /* OVERRIDE */
	{ 0x36, 0x00 }, /* OVERRIDE */
	{ 0x05, 0x01 }, /* TOP_MASTER_ENABLE */
	{ 0, 0 }
};

/* ------------------------------------------------------------------------- */

static struct reg_pair_t reg_pair_rftune[] = {
	{ 0x11, 0x00 }, /* abort tune */
	{ 0x13, 0x15 },
	{ 0x14, 0x40 },
	{ 0x15, 0x0e },
	{ 0x11, 0x02 }, /* start tune */
	{ 0, 0 }
};

/* ------------------------------------------------------------------------- */

struct mxl5007t_state {
	struct list_head hybrid_tuner_instance_list;
	struct tuner_i2c_props i2c_props;

	struct mutex lock;

	struct mxl5007t_config *config;

	enum mxl5007t_chip_version chip_id;

	struct reg_pair_t tab_init[ARRAY_SIZE(init_tab)];
	struct reg_pair_t tab_init_cable[ARRAY_SIZE(init_tab_cable)];
	struct reg_pair_t tab_rftune[ARRAY_SIZE(reg_pair_rftune)];

	u32 frequency;
	u32 bandwidth;
};

/* ------------------------------------------------------------------------- */

/* called by _init and _rftun to manipulate the register arrays */

static void set_reg_bits(struct reg_pair_t *reg_pair, u8 reg, u8 mask, u8 val)
{
	unsigned int i = 0;

	while (reg_pair[i].reg || reg_pair[i].val) {
		if (reg_pair[i].reg == reg) {
			reg_pair[i].val &= ~mask;
			reg_pair[i].val |= val;
		}
		i++;

	}
	return;
}

static void copy_reg_bits(struct reg_pair_t *reg_pair1,
			  struct reg_pair_t *reg_pair2)
{
	unsigned int i, j;

	i = j = 0;

	while (reg_pair1[i].reg || reg_pair1[i].val) {
		while (reg_pair2[j].reg || reg_pair2[j].reg) {
			if (reg_pair1[i].reg != reg_pair2[j].reg) {
				j++;
				continue;
			}
			reg_pair2[j].val = reg_pair1[i].val;
			break;
		}
		i++;
	}
	return;
}

/* ------------------------------------------------------------------------- */

static void mxl5007t_set_mode_bits(struct mxl5007t_state *state,
				   enum mxl5007t_mode mode,
				   s32 if_diff_out_level)
{
	switch (mode) {
	case MxL_MODE_OTA_DVBT_ATSC:
		set_reg_bits(state->tab_init, 0x32, 0x0f, 0x06);
		set_reg_bits(state->tab_init, 0x35, 0xff, 0x0e);
		break;
	case MxL_MODE_OTA_ISDBT:
		set_reg_bits(state->tab_init, 0x32, 0x0f, 0x06);
		set_reg_bits(state->tab_init, 0x35, 0xff, 0x12);
		break;
	case MxL_MODE_OTA_NTSC_PAL_GH:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x00);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		break;
	case MxL_MODE_OTA_PAL_IB:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x10);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		break;
	case MxL_MODE_OTA_PAL_D_SECAM_KL:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x20);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		break;
	case MxL_MODE_CABLE_DIGITAL:
		set_reg_bits(state->tab_init_cable, 0x71, 0xff, 0x01);
		set_reg_bits(state->tab_init_cable, 0x72, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x74, 0xff, 0x17);
		break;
	case MxL_MODE_CABLE_NTSC_PAL_GH:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x00);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		set_reg_bits(state->tab_init_cable, 0x71, 0xff, 0x01);
		set_reg_bits(state->tab_init_cable, 0x72, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x74, 0xff, 0x17);
		break;
	case MxL_MODE_CABLE_PAL_IB:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x10);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		set_reg_bits(state->tab_init_cable, 0x71, 0xff, 0x01);
		set_reg_bits(state->tab_init_cable, 0x72, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x74, 0xff, 0x17);
		break;
	case MxL_MODE_CABLE_PAL_D_SECAM_KL:
		set_reg_bits(state->tab_init, 0x16, 0x70, 0x20);
		set_reg_bits(state->tab_init, 0x32, 0xff, 0x85);
		set_reg_bits(state->tab_init_cable, 0x71, 0xff, 0x01);
		set_reg_bits(state->tab_init_cable, 0x72, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x74, 0xff, 0x17);
		break;
	case MxL_MODE_CABLE_SCTE40:
		set_reg_bits(state->tab_init_cable, 0x36, 0xff, 0x08);
		set_reg_bits(state->tab_init_cable, 0x68, 0xff, 0xbc);
		set_reg_bits(state->tab_init_cable, 0x71, 0xff, 0x01);
		set_reg_bits(state->tab_init_cable, 0x72, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x74, 0xff, 0x17);
		break;
	default:
		mxl_fail(-EINVAL);
	}
	return;
}

static void mxl5007t_set_if_freq_bits(struct mxl5007t_state *state,
				      enum mxl5007t_if_freq if_freq,
				      int invert_if)
{
	u8 val;

	switch (if_freq) {
	case MxL_IF_4_MHZ:
		val = 0x00;
		break;
	case MxL_IF_4_5_MHZ:
		val = 0x20;
		break;
	case MxL_IF_4_57_MHZ:
		val = 0x30;
		break;
	case MxL_IF_5_MHZ:
		val = 0x40;
		break;
	case MxL_IF_5_38_MHZ:
		val = 0x50;
		break;
	case MxL_IF_6_MHZ:
		val = 0x60;
		break;
	case MxL_IF_6_28_MHZ:
		val = 0x70;
		break;
	case MxL_IF_9_1915_MHZ:
		val = 0x80;
		break;
	case MxL_IF_35_25_MHZ:
		val = 0x90;
		break;
	case MxL_IF_36_15_MHZ:
		val = 0xa0;
		break;
	case MxL_IF_44_MHZ:
		val = 0xb0;
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}
	set_reg_bits(state->tab_init, 0x0c, 0xf0, val);

	/* set inverted IF or normal IF */
	set_reg_bits(state->tab_init, 0x0c, 0x08, invert_if ? 0x08 : 0x00);

	return;
}

static void mxl5007t_set_xtal_freq_bits(struct mxl5007t_state *state,
					enum mxl5007t_xtal_freq xtal_freq)
{
	u8 val;

	switch (xtal_freq) {
	case MxL_XTAL_16_MHZ:
		val = 0x00; /* select xtal freq & Ref Freq */
		break;
	case MxL_XTAL_20_MHZ:
		val = 0x11;
		break;
	case MxL_XTAL_20_25_MHZ:
		val = 0x22;
		break;
	case MxL_XTAL_20_48_MHZ:
		val = 0x33;
		break;
	case MxL_XTAL_24_MHZ:
		val = 0x44;
		break;
	case MxL_XTAL_25_MHZ:
		val = 0x55;
		break;
	case MxL_XTAL_25_14_MHZ:
		val = 0x66;
		break;
	case MxL_XTAL_27_MHZ:
		val = 0x77;
		break;
	case MxL_XTAL_28_8_MHZ:
		val = 0x88;
		break;
	case MxL_XTAL_32_MHZ:
		val = 0x99;
		break;
	case MxL_XTAL_40_MHZ:
		val = 0xaa;
		break;
	case MxL_XTAL_44_MHZ:
		val = 0xbb;
		break;
	case MxL_XTAL_48_MHZ:
		val = 0xcc;
		break;
	case MxL_XTAL_49_3811_MHZ:
		val = 0xdd;
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}
	set_reg_bits(state->tab_init, 0x0b, 0xff, val);

	return;
}

static struct reg_pair_t *mxl5007t_calc_init_regs(struct mxl5007t_state *state,
						  enum mxl5007t_mode mode)
{
	struct mxl5007t_config *cfg = state->config;

	memcpy(&state->tab_init, &init_tab, sizeof(init_tab));
	memcpy(&state->tab_init_cable, &init_tab_cable, sizeof(init_tab_cable));

	mxl5007t_set_mode_bits(state, mode, cfg->if_diff_out_level);
	mxl5007t_set_if_freq_bits(state, cfg->if_freq_hz, cfg->invert_if);
	mxl5007t_set_xtal_freq_bits(state, cfg->xtal_freq_hz);

	set_reg_bits(state->tab_init, 0x10, 0x40, cfg->loop_thru_enable << 6);

	set_reg_bits(state->tab_init, 0xd8, 0x08, cfg->clk_out_enable << 3);

	set_reg_bits(state->tab_init, 0x10, 0x07, cfg->clk_out_amp);

	/* set IDAC to automatic mode control by AGC */
	set_reg_bits(state->tab_init, 0x12, 0x80, 0x00);

	if (mode >= MxL_MODE_CABLE_DIGITAL) {
		copy_reg_bits(state->tab_init, state->tab_init_cable);
		return state->tab_init_cable;
	} else
		return state->tab_init;
}

/* ------------------------------------------------------------------------- */

enum mxl5007t_bw_mhz {
	MxL_BW_6MHz = 6,
	MxL_BW_7MHz = 7,
	MxL_BW_8MHz = 8,
};

static void mxl5007t_set_bw_bits(struct mxl5007t_state *state,
				 enum mxl5007t_bw_mhz bw)
{
	u8 val;

	switch (bw) {
	case MxL_BW_6MHz:
		val = 0x15; /* set DIG_MODEINDEX, DIG_MODEINDEX_A,
			     * and DIG_MODEINDEX_CSF */
		break;
	case MxL_BW_7MHz:
		val = 0x21;
		break;
	case MxL_BW_8MHz:
		val = 0x3f;
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}
	set_reg_bits(state->tab_rftune, 0x13, 0x3f, val);

	return;
}

static struct
reg_pair_t *mxl5007t_calc_rf_tune_regs(struct mxl5007t_state *state,
				       u32 rf_freq, enum mxl5007t_bw_mhz bw)
{
	u32 dig_rf_freq = 0;
	u32 temp;
	u32 frac_divider = 1000000;
	unsigned int i;

	memcpy(&state->tab_rftune, &reg_pair_rftune, sizeof(reg_pair_rftune));

	mxl5007t_set_bw_bits(state, bw);

	/* Convert RF frequency into 16 bits =>
	 * 10 bit integer (MHz) + 6 bit fraction */
	dig_rf_freq = rf_freq / MHz;

	temp = rf_freq % MHz;

	for (i = 0; i < 6; i++) {
		dig_rf_freq <<= 1;
		frac_divider /= 2;
		if (temp > frac_divider) {
			temp -= frac_divider;
			dig_rf_freq++;
		}
	}

	/* add to have shift center point by 7.8124 kHz */
	if (temp > 7812)
		dig_rf_freq++;

	set_reg_bits(state->tab_rftune, 0x14, 0xff, (u8)dig_rf_freq);
	set_reg_bits(state->tab_rftune, 0x15, 0xff, (u8)(dig_rf_freq >> 8));

	return state->tab_rftune;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_write_reg(struct mxl5007t_state *state, u8 reg, u8 val)
{
	u8 buf[] = { reg, val };
	struct i2c_msg msg = { .addr = state->i2c_props.addr, .flags = 0,
			       .buf = buf, .len = 2 };
	int ret;

	ret = i2c_transfer(state->i2c_props.adap, &msg, 1);
	if (ret != 1) {
		mxl_err("failed!");
		return -EREMOTEIO;
	}
	return 0;
}

static int mxl5007t_write_regs(struct mxl5007t_state *state,
			       struct reg_pair_t *reg_pair)
{
	unsigned int i = 0;
	int ret = 0;

	while ((ret == 0) && (reg_pair[i].reg || reg_pair[i].val)) {
		ret = mxl5007t_write_reg(state,
					 reg_pair[i].reg, reg_pair[i].val);
		i++;
	}
	return ret;
}

static int mxl5007t_read_reg(struct mxl5007t_state *state, u8 reg, u8 *val)
{
	struct i2c_msg msg[] = {
		{ .addr = state->i2c_props.addr, .flags = 0,
		  .buf = &reg, .len = 1 },
		{ .addr = state->i2c_props.addr, .flags = I2C_M_RD,
		  .buf = val, .len = 1 },
	};
	int ret;

	ret = i2c_transfer(state->i2c_props.adap, msg, 2);
	if (ret != 2) {
		mxl_err("failed!");
		return -EREMOTEIO;
	}
	return 0;
}

static int mxl5007t_soft_reset(struct mxl5007t_state *state)
{
	u8 d = 0xff;
	struct i2c_msg msg = { .addr = state->i2c_props.addr, .flags = 0,
			       .buf = &d, .len = 1 };

	int ret = i2c_transfer(state->i2c_props.adap, &msg, 1);

	if (ret != 1) {
		mxl_err("failed!");
		return -EREMOTEIO;
	}
	return 0;
}

static int mxl5007t_tuner_init(struct mxl5007t_state *state,
			       enum mxl5007t_mode mode)
{
	struct reg_pair_t *init_regs;
	int ret;

	ret = mxl5007t_soft_reset(state);
	if (mxl_fail(ret))
		goto fail;

	/* calculate initialization reg array */
	init_regs = mxl5007t_calc_init_regs(state, mode);

	ret = mxl5007t_write_regs(state, init_regs);
	if (mxl_fail(ret))
		goto fail;
	mdelay(1);

	ret = mxl5007t_write_reg(state, 0x2c, 0x35);
	mxl_fail(ret);
fail:
	return ret;
}

static int mxl5007t_tuner_rf_tune(struct mxl5007t_state *state, u32 rf_freq_hz,
				  enum mxl5007t_bw_mhz bw)
{
	struct reg_pair_t *rf_tune_regs;
	int ret;

	/* calculate channel change reg array */
	rf_tune_regs = mxl5007t_calc_rf_tune_regs(state, rf_freq_hz, bw);

	ret = mxl5007t_write_regs(state, rf_tune_regs);
	if (mxl_fail(ret))
		goto fail;
	msleep(3);
fail:
	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_synth_lock_status(struct mxl5007t_state *state,
				      int *rf_locked, int *ref_locked)
{
	u8 d;
	int ret;

	*rf_locked = 0;
	*ref_locked = 0;

	ret = mxl5007t_read_reg(state, 0xcf, &d);
	if (mxl_fail(ret))
		goto fail;

	if ((d & 0x0c) == 0x0c)
		*rf_locked = 1;

	if ((d & 0x03) == 0x03)
		*ref_locked = 1;
fail:
	return ret;
}

static int mxl5007t_check_rf_input_power(struct mxl5007t_state *state,
					 s32 *rf_input_level)
{
	u8 d1, d2;
	int ret;

	ret = mxl5007t_read_reg(state, 0xb7, &d1);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl5007t_read_reg(state, 0xbf, &d2);
	if (mxl_fail(ret))
		goto fail;

	d2 = d2 >> 4;
	if (d2 > 7)
		d2 += 0xf0;

	*rf_input_level = (s32)(d1 + d2 - 113);
fail:
	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	int rf_locked, ref_locked;
	s32 rf_input_level = 0;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl5007t_synth_lock_status(state, &rf_locked, &ref_locked);
	if (mxl_fail(ret))
		goto fail;
	mxl_debug("%s%s", rf_locked ? "rf locked " : "",
		  ref_locked ? "ref locked" : "");

	ret = mxl5007t_check_rf_input_power(state, &rf_input_level);
	if (mxl_fail(ret))
		goto fail;
	mxl_debug("rf input power: %d", rf_input_level);
fail:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_set_params(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *params)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	enum mxl5007t_bw_mhz bw;
	enum mxl5007t_mode mode;
	int ret;
	u32 freq = params->frequency;

	if (fe->ops.info.type == FE_ATSC) {
		switch (params->u.vsb.modulation) {
		case VSB_8:
		case VSB_16:
			mode = MxL_MODE_OTA_DVBT_ATSC;
			break;
		case QAM_64:
		case QAM_256:
			mode = MxL_MODE_CABLE_DIGITAL;
			break;
		default:
			mxl_err("modulation not set!");
			return -EINVAL;
		}
		bw = MxL_BW_6MHz;
	} else if (fe->ops.info.type == FE_OFDM) {
		switch (params->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
			bw = MxL_BW_6MHz;
			break;
		case BANDWIDTH_7_MHZ:
			bw = MxL_BW_7MHz;
			break;
		case BANDWIDTH_8_MHZ:
			bw = MxL_BW_8MHz;
			break;
		default:
			mxl_err("bandwidth not set!");
			return -EINVAL;
		}
		mode = MxL_MODE_OTA_DVBT_ATSC;
	} else {
		mxl_err("modulation type not supported!");
		return -EINVAL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	mutex_lock(&state->lock);

	ret = mxl5007t_tuner_init(state, mode);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl5007t_tuner_rf_tune(state, freq, bw);
	if (mxl_fail(ret))
		goto fail;

	state->frequency = freq;
	state->bandwidth = (fe->ops.info.type == FE_OFDM) ?
		params->u.ofdm.bandwidth : 0;
fail:
	mutex_unlock(&state->lock);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

static int mxl5007t_set_analog_params(struct dvb_frontend *fe,
				      struct analog_parameters *params)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	enum mxl5007t_bw_mhz bw = 0; /* FIXME */
	enum mxl5007t_mode cbl_mode;
	enum mxl5007t_mode ota_mode;
	char *mode_name;
	int ret;
	u32 freq = params->frequency * 62500;

#define cable 1
	if (params->std & V4L2_STD_MN) {
		cbl_mode = MxL_MODE_CABLE_NTSC_PAL_GH;
		ota_mode = MxL_MODE_OTA_NTSC_PAL_GH;
		mode_name = "MN";
	} else if (params->std & V4L2_STD_B) {
		cbl_mode = MxL_MODE_CABLE_PAL_IB;
		ota_mode = MxL_MODE_OTA_PAL_IB;
		mode_name = "B";
	} else if (params->std & V4L2_STD_GH) {
		cbl_mode = MxL_MODE_CABLE_NTSC_PAL_GH;
		ota_mode = MxL_MODE_OTA_NTSC_PAL_GH;
		mode_name = "GH";
	} else if (params->std & V4L2_STD_PAL_I) {
		cbl_mode = MxL_MODE_CABLE_PAL_IB;
		ota_mode = MxL_MODE_OTA_PAL_IB;
		mode_name = "I";
	} else if (params->std & V4L2_STD_DK) {
		cbl_mode = MxL_MODE_CABLE_PAL_D_SECAM_KL;
		ota_mode = MxL_MODE_OTA_PAL_D_SECAM_KL;
		mode_name = "DK";
	} else if (params->std & V4L2_STD_SECAM_L) {
		cbl_mode = MxL_MODE_CABLE_PAL_D_SECAM_KL;
		ota_mode = MxL_MODE_OTA_PAL_D_SECAM_KL;
		mode_name = "L";
	} else if (params->std & V4L2_STD_SECAM_LC) {
		cbl_mode = MxL_MODE_CABLE_PAL_D_SECAM_KL;
		ota_mode = MxL_MODE_OTA_PAL_D_SECAM_KL;
		mode_name = "L'";
	} else {
		mode_name = "xx";
		/* FIXME */
		cbl_mode = MxL_MODE_CABLE_NTSC_PAL_GH;
		ota_mode = MxL_MODE_OTA_NTSC_PAL_GH;
	}
	mxl_debug("setting mxl5007 to system %s", mode_name);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	mutex_lock(&state->lock);

	ret = mxl5007t_tuner_init(state, cable ? cbl_mode : ota_mode);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl5007t_tuner_rf_tune(state, freq, bw);
	if (mxl_fail(ret))
		goto fail;

	state->frequency = freq;
	state->bandwidth = 0;
fail:
	mutex_unlock(&state->lock);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_init(struct dvb_frontend *fe)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	int ret;
	u8 d;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl5007t_read_reg(state, 0x05, &d);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl5007t_write_reg(state, 0x05, d | 0x01);
	mxl_fail(ret);
fail:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

static int mxl5007t_sleep(struct dvb_frontend *fe)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	int ret;
	u8 d;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl5007t_read_reg(state, 0x05, &d);
	if (mxl_fail(ret))
		goto fail;

	ret = mxl5007t_write_reg(state, 0x05, d & ~0x01);
	mxl_fail(ret);
fail:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	*frequency = state->frequency;
	return 0;
}

static int mxl5007t_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	*bandwidth = state->bandwidth;
	return 0;
}

static int mxl5007t_release(struct dvb_frontend *fe)
{
	struct mxl5007t_state *state = fe->tuner_priv;

	mutex_lock(&mxl5007t_list_mutex);

	if (state)
		hybrid_tuner_release_state(state);

	mutex_unlock(&mxl5007t_list_mutex);

	fe->tuner_priv = NULL;

	return 0;
}

/* ------------------------------------------------------------------------- */

static struct dvb_tuner_ops mxl5007t_tuner_ops = {
	.info = {
		.name = "MaxLinear MxL5007T",
	},
	.init              = mxl5007t_init,
	.sleep             = mxl5007t_sleep,
	.set_params        = mxl5007t_set_params,
	.set_analog_params = mxl5007t_set_analog_params,
	.get_status        = mxl5007t_get_status,
	.get_frequency     = mxl5007t_get_frequency,
	.get_bandwidth     = mxl5007t_get_bandwidth,
	.release           = mxl5007t_release,
};

static int mxl5007t_get_chip_id(struct mxl5007t_state *state)
{
	char *name;
	int ret;
	u8 id;

	ret = mxl5007t_read_reg(state, 0xd3, &id);
	if (mxl_fail(ret))
		goto fail;

	switch (id) {
	case MxL_5007_V1_F1:
		name = "MxL5007.v1.f1";
		break;
	case MxL_5007_V1_F2:
		name = "MxL5007.v1.f2";
		break;
	case MxL_5007_V2_100_F1:
		name = "MxL5007.v2.100.f1";
		break;
	case MxL_5007_V2_100_F2:
		name = "MxL5007.v2.100.f2";
		break;
	case MxL_5007_V2_200_F1:
		name = "MxL5007.v2.200.f1";
		break;
	case MxL_5007_V2_200_F2:
		name = "MxL5007.v2.200.f2";
		break;
	default:
		name = "MxL5007T";
		id = MxL_UNKNOWN_ID;
	}
	state->chip_id = id;
	mxl_info("%s detected @ %d-%04x", name,
		 i2c_adapter_id(state->i2c_props.adap),
		 state->i2c_props.addr);
	return 0;
fail:
	mxl_warn("unable to identify device @ %d-%04x",
		 i2c_adapter_id(state->i2c_props.adap),
		 state->i2c_props.addr);

	state->chip_id = MxL_UNKNOWN_ID;
	return ret;
}

struct dvb_frontend *mxl5007t_attach(struct dvb_frontend *fe,
				     struct i2c_adapter *i2c, u8 addr,
				     struct mxl5007t_config *cfg)
{
	struct mxl5007t_state *state = NULL;
	int instance, ret;

	mutex_lock(&mxl5007t_list_mutex);
	instance = hybrid_tuner_request_state(struct mxl5007t_state, state,
					      hybrid_tuner_instance_list,
					      i2c, addr, "mxl5007");
	switch (instance) {
	case 0:
		goto fail;
	case 1:
		/* new tuner instance */
		state->config = cfg;

		mutex_init(&state->lock);

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = mxl5007t_get_chip_id(state);

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);

		/* check return value of mxl5007t_get_chip_id */
		if (mxl_fail(ret))
			goto fail;
		break;
	default:
		/* existing tuner instance */
		break;
	}
	fe->tuner_priv = state;
	mutex_unlock(&mxl5007t_list_mutex);

	memcpy(&fe->ops.tuner_ops, &mxl5007t_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	return fe;
fail:
	mutex_unlock(&mxl5007t_list_mutex);

	mxl5007t_release(fe);
	return NULL;
}
EXPORT_SYMBOL_GPL(mxl5007t_attach);
MODULE_DESCRIPTION("MaxLinear MxL5007T Silicon IC tuner driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
