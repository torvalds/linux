/*
 *  mxl5007t.c - driver for the MaxLinear MxL5007T silicon tuner
 *
 *  Copyright (C) 2008, 2009 Michael Krufky <mkrufky@linuxtv.org>
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
	MxL_MODE_ISDBT     =    0,
	MxL_MODE_DVBT      =    1,
	MxL_MODE_ATSC      =    2,
	MxL_MODE_CABLE     = 0x10,
};

enum mxl5007t_chip_version {
	MxL_UNKNOWN_ID     = 0x00,
	MxL_5007_V1_F1     = 0x11,
	MxL_5007_V1_F2     = 0x12,
	MxL_5007_V4        = 0x14,
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
	{ 0x02, 0x06 },
	{ 0x03, 0x48 },
	{ 0x05, 0x04 },
	{ 0x06, 0x10 },
	{ 0x2e, 0x15 }, /* OVERRIDE */
	{ 0x30, 0x10 }, /* OVERRIDE */
	{ 0x45, 0x58 }, /* OVERRIDE */
	{ 0x48, 0x19 }, /* OVERRIDE */
	{ 0x52, 0x03 }, /* OVERRIDE */
	{ 0x53, 0x44 }, /* OVERRIDE */
	{ 0x6a, 0x4b }, /* OVERRIDE */
	{ 0x76, 0x00 }, /* OVERRIDE */
	{ 0x78, 0x18 }, /* OVERRIDE */
	{ 0x7a, 0x17 }, /* OVERRIDE */
	{ 0x85, 0x06 }, /* OVERRIDE */
	{ 0x01, 0x01 }, /* TOP_MASTER_ENABLE */
	{ 0, 0 }
};

static struct reg_pair_t init_tab_cable[] = {
	{ 0x02, 0x06 },
	{ 0x03, 0x48 },
	{ 0x05, 0x04 },
	{ 0x06, 0x10 },
	{ 0x09, 0x3f },
	{ 0x0a, 0x3f },
	{ 0x0b, 0x3f },
	{ 0x2e, 0x15 }, /* OVERRIDE */
	{ 0x30, 0x10 }, /* OVERRIDE */
	{ 0x45, 0x58 }, /* OVERRIDE */
	{ 0x48, 0x19 }, /* OVERRIDE */
	{ 0x52, 0x03 }, /* OVERRIDE */
	{ 0x53, 0x44 }, /* OVERRIDE */
	{ 0x6a, 0x4b }, /* OVERRIDE */
	{ 0x76, 0x00 }, /* OVERRIDE */
	{ 0x78, 0x18 }, /* OVERRIDE */
	{ 0x7a, 0x17 }, /* OVERRIDE */
	{ 0x85, 0x06 }, /* OVERRIDE */
	{ 0x01, 0x01 }, /* TOP_MASTER_ENABLE */
	{ 0, 0 }
};

/* ------------------------------------------------------------------------- */

static struct reg_pair_t reg_pair_rftune[] = {
	{ 0x0f, 0x00 }, /* abort tune */
	{ 0x0c, 0x15 },
	{ 0x0d, 0x40 },
	{ 0x0e, 0x0e },
	{ 0x1f, 0x87 }, /* OVERRIDE */
	{ 0x20, 0x1f }, /* OVERRIDE */
	{ 0x21, 0x87 }, /* OVERRIDE */
	{ 0x22, 0x1f }, /* OVERRIDE */
	{ 0x80, 0x01 }, /* freq dependent */
	{ 0x0f, 0x01 }, /* start tune */
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

	enum mxl5007t_if_freq if_freq;

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
		while (reg_pair2[j].reg || reg_pair2[j].val) {
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
	case MxL_MODE_ATSC:
		set_reg_bits(state->tab_init, 0x06, 0x1f, 0x12);
		break;
	case MxL_MODE_DVBT:
		set_reg_bits(state->tab_init, 0x06, 0x1f, 0x11);
		break;
	case MxL_MODE_ISDBT:
		set_reg_bits(state->tab_init, 0x06, 0x1f, 0x10);
		break;
	case MxL_MODE_CABLE:
		set_reg_bits(state->tab_init_cable, 0x09, 0xff, 0xc1);
		set_reg_bits(state->tab_init_cable, 0x0a, 0xff,
			     8 - if_diff_out_level);
		set_reg_bits(state->tab_init_cable, 0x0b, 0xff, 0x17);
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
		val = 0x02;
		break;
	case MxL_IF_4_57_MHZ:
		val = 0x03;
		break;
	case MxL_IF_5_MHZ:
		val = 0x04;
		break;
	case MxL_IF_5_38_MHZ:
		val = 0x05;
		break;
	case MxL_IF_6_MHZ:
		val = 0x06;
		break;
	case MxL_IF_6_28_MHZ:
		val = 0x07;
		break;
	case MxL_IF_9_1915_MHZ:
		val = 0x08;
		break;
	case MxL_IF_35_25_MHZ:
		val = 0x09;
		break;
	case MxL_IF_36_15_MHZ:
		val = 0x0a;
		break;
	case MxL_IF_44_MHZ:
		val = 0x0b;
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}
	set_reg_bits(state->tab_init, 0x02, 0x0f, val);

	/* set inverted IF or normal IF */
	set_reg_bits(state->tab_init, 0x02, 0x10, invert_if ? 0x10 : 0x00);

	state->if_freq = if_freq;

	return;
}

static void mxl5007t_set_xtal_freq_bits(struct mxl5007t_state *state,
					enum mxl5007t_xtal_freq xtal_freq)
{
	switch (xtal_freq) {
	case MxL_XTAL_16_MHZ:
		/* select xtal freq & ref freq */
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x00);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x00);
		break;
	case MxL_XTAL_20_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x10);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x01);
		break;
	case MxL_XTAL_20_25_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x20);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x02);
		break;
	case MxL_XTAL_20_48_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x30);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x03);
		break;
	case MxL_XTAL_24_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x40);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x04);
		break;
	case MxL_XTAL_25_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x50);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x05);
		break;
	case MxL_XTAL_25_14_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x60);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x06);
		break;
	case MxL_XTAL_27_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x70);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x07);
		break;
	case MxL_XTAL_28_8_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x80);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x08);
		break;
	case MxL_XTAL_32_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0x90);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x09);
		break;
	case MxL_XTAL_40_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0xa0);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x0a);
		break;
	case MxL_XTAL_44_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0xb0);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x0b);
		break;
	case MxL_XTAL_48_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0xc0);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x0c);
		break;
	case MxL_XTAL_49_3811_MHZ:
		set_reg_bits(state->tab_init, 0x03, 0xf0, 0xd0);
		set_reg_bits(state->tab_init, 0x05, 0x0f, 0x0d);
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}

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

	set_reg_bits(state->tab_init, 0x04, 0x01, cfg->loop_thru_enable);
	set_reg_bits(state->tab_init, 0x03, 0x08, cfg->clk_out_enable << 3);
	set_reg_bits(state->tab_init, 0x03, 0x07, cfg->clk_out_amp);

	if (mode >= MxL_MODE_CABLE) {
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
		val = 0x2a;
		break;
	case MxL_BW_8MHz:
		val = 0x3f;
		break;
	default:
		mxl_fail(-EINVAL);
		return;
	}
	set_reg_bits(state->tab_rftune, 0x0c, 0x3f, val);

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

	set_reg_bits(state->tab_rftune, 0x0d, 0xff, (u8) dig_rf_freq);
	set_reg_bits(state->tab_rftune, 0x0e, 0xff, (u8) (dig_rf_freq >> 8));

	if (rf_freq >= 333000000)
		set_reg_bits(state->tab_rftune, 0x80, 0x40, 0x40);

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
	u8 buf[2] = { 0xfb, reg };
	struct i2c_msg msg[] = {
		{ .addr = state->i2c_props.addr, .flags = 0,
		  .buf = buf, .len = 2 },
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
	struct i2c_msg msg = {
		.addr = state->i2c_props.addr, .flags = 0,
		.buf = &d, .len = 1
	};
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

	/* calculate initialization reg array */
	init_regs = mxl5007t_calc_init_regs(state, mode);

	ret = mxl5007t_write_regs(state, init_regs);
	if (mxl_fail(ret))
		goto fail;
	mdelay(1);
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

	ret = mxl5007t_read_reg(state, 0xd8, &d);
	if (mxl_fail(ret))
		goto fail;

	if ((d & 0x0c) == 0x0c)
		*rf_locked = 1;

	if ((d & 0x03) == 0x03)
		*ref_locked = 1;
fail:
	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	int rf_locked, ref_locked, ret;

	*status = 0;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl5007t_synth_lock_status(state, &rf_locked, &ref_locked);
	if (mxl_fail(ret))
		goto fail;
	mxl_debug("%s%s", rf_locked ? "rf locked " : "",
		  ref_locked ? "ref locked" : "");

	if ((rf_locked) || (ref_locked))
		*status |= TUNER_STATUS_LOCKED;
fail:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

/* ------------------------------------------------------------------------- */

static int mxl5007t_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 delsys = c->delivery_system;
	struct mxl5007t_state *state = fe->tuner_priv;
	enum mxl5007t_bw_mhz bw;
	enum mxl5007t_mode mode;
	int ret;
	u32 freq = c->frequency;

	switch (delsys) {
	case SYS_ATSC:
		mode = MxL_MODE_ATSC;
		bw = MxL_BW_6MHz;
		break;
	case SYS_DVBC_ANNEX_B:
		mode = MxL_MODE_CABLE;
		bw = MxL_BW_6MHz;
		break;
	case SYS_DVBT:
	case SYS_DVBT2:
		mode = MxL_MODE_DVBT;
		switch (c->bandwidth_hz) {
		case 6000000:
			bw = MxL_BW_6MHz;
			break;
		case 7000000:
			bw = MxL_BW_7MHz;
			break;
		case 8000000:
			bw = MxL_BW_8MHz;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
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
	state->bandwidth = c->bandwidth_hz;
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

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* wake from standby */
	ret = mxl5007t_write_reg(state, 0x01, 0x01);
	mxl_fail(ret);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

static int mxl5007t_sleep(struct dvb_frontend *fe)
{
	struct mxl5007t_state *state = fe->tuner_priv;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* enter standby mode */
	ret = mxl5007t_write_reg(state, 0x01, 0x00);
	mxl_fail(ret);
	ret = mxl5007t_write_reg(state, 0x0f, 0x00);
	mxl_fail(ret);

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

static int mxl5007t_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl5007t_state *state = fe->tuner_priv;

	*frequency = 0;

	switch (state->if_freq) {
	case MxL_IF_4_MHZ:
		*frequency = 4000000;
		break;
	case MxL_IF_4_5_MHZ:
		*frequency = 4500000;
		break;
	case MxL_IF_4_57_MHZ:
		*frequency = 4570000;
		break;
	case MxL_IF_5_MHZ:
		*frequency = 5000000;
		break;
	case MxL_IF_5_38_MHZ:
		*frequency = 5380000;
		break;
	case MxL_IF_6_MHZ:
		*frequency = 6000000;
		break;
	case MxL_IF_6_28_MHZ:
		*frequency = 6280000;
		break;
	case MxL_IF_9_1915_MHZ:
		*frequency = 9191500;
		break;
	case MxL_IF_35_25_MHZ:
		*frequency = 35250000;
		break;
	case MxL_IF_36_15_MHZ:
		*frequency = 36150000;
		break;
	case MxL_IF_44_MHZ:
		*frequency = 44000000;
		break;
	}
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
	.get_status        = mxl5007t_get_status,
	.get_frequency     = mxl5007t_get_frequency,
	.get_bandwidth     = mxl5007t_get_bandwidth,
	.release           = mxl5007t_release,
	.get_if_frequency  = mxl5007t_get_if_frequency,
};

static int mxl5007t_get_chip_id(struct mxl5007t_state *state)
{
	char *name;
	int ret;
	u8 id;

	ret = mxl5007t_read_reg(state, 0xd9, &id);
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
	case MxL_5007_V4:
		name = "MxL5007T.v4";
		break;
	default:
		name = "MxL5007T";
		printk(KERN_WARNING "%s: unknown rev (%02x)\n", __func__, id);
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
					      i2c, addr, "mxl5007t");
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

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = mxl5007t_soft_reset(state);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (mxl_fail(ret))
		goto fail;

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
MODULE_VERSION("0.2");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
