/*
 *  mxl111sf-tuner.c - driver for the MaxLinear MXL111SF CMOS tuner
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
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

#include "mxl111sf-tuner.h"
#include "mxl111sf-phy.h"
#include "mxl111sf-reg.h"

/* debug */
static int mxl111sf_tuner_debug;
module_param_named(debug, mxl111sf_tuner_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able)).");

#define mxl_dbg(fmt, arg...) \
	if (mxl111sf_tuner_debug) \
		mxl_printk(KERN_DEBUG, fmt, ##arg)

/* ------------------------------------------------------------------------ */

struct mxl111sf_tuner_state {
	struct mxl111sf_state *mxl_state;

	const struct mxl111sf_tuner_config *cfg;

	enum mxl_if_freq if_freq;

	u32 frequency;
	u32 bandwidth;
};

static int mxl111sf_tuner_read_reg(struct mxl111sf_tuner_state *state,
				   u8 addr, u8 *data)
{
	return (state->cfg->read_reg) ?
		state->cfg->read_reg(state->mxl_state, addr, data) :
		-EINVAL;
}

static int mxl111sf_tuner_write_reg(struct mxl111sf_tuner_state *state,
				    u8 addr, u8 data)
{
	return (state->cfg->write_reg) ?
		state->cfg->write_reg(state->mxl_state, addr, data) :
		-EINVAL;
}

static int mxl111sf_tuner_program_regs(struct mxl111sf_tuner_state *state,
			       struct mxl111sf_reg_ctrl_info *ctrl_reg_info)
{
	return (state->cfg->program_regs) ?
		state->cfg->program_regs(state->mxl_state, ctrl_reg_info) :
		-EINVAL;
}

static int mxl1x1sf_tuner_top_master_ctrl(struct mxl111sf_tuner_state *state,
					  int onoff)
{
	return (state->cfg->top_master_ctrl) ?
		state->cfg->top_master_ctrl(state->mxl_state, onoff) :
		-EINVAL;
}

/* ------------------------------------------------------------------------ */

static struct mxl111sf_reg_ctrl_info mxl_phy_tune_rf[] = {
	{0x1d, 0x7f, 0x00}, /* channel bandwidth section 1/2/3,
			       DIG_MODEINDEX, _A, _CSF, */
	{0x1e, 0xff, 0x00}, /* channel frequency (lo and fractional) */
	{0x1f, 0xff, 0x00}, /* channel frequency (hi for integer portion) */
	{0,    0,    0}
};

/* ------------------------------------------------------------------------ */

static struct mxl111sf_reg_ctrl_info *mxl111sf_calc_phy_tune_regs(u32 freq,
								  u8 bw)
{
	u8 filt_bw;

	/* set channel bandwidth */
	switch (bw) {
	case 0: /* ATSC */
		filt_bw = 25;
		break;
	case 1: /* QAM */
		filt_bw = 69;
		break;
	case 6:
		filt_bw = 21;
		break;
	case 7:
		filt_bw = 42;
		break;
	case 8:
		filt_bw = 63;
		break;
	default:
		pr_err("%s: invalid bandwidth setting!", __func__);
		return NULL;
	}

	/* calculate RF channel */
	freq /= 1000000;

	freq *= 64;
#if 0
	/* do round */
	freq += 0.5;
#endif
	/* set bandwidth */
	mxl_phy_tune_rf[0].data = filt_bw;

	/* set RF */
	mxl_phy_tune_rf[1].data = (freq & 0xff);
	mxl_phy_tune_rf[2].data = (freq >> 8) & 0xff;

	/* start tune */
	return mxl_phy_tune_rf;
}

static int mxl1x1sf_tuner_set_if_output_freq(struct mxl111sf_tuner_state *state)
{
	int ret;
	u8 ctrl;
#if 0
	u16 iffcw;
	u32 if_freq;
#endif
	mxl_dbg("(IF polarity = %d, IF freq = 0x%02x)",
		state->cfg->invert_spectrum, state->cfg->if_freq);

	/* set IF polarity */
	ctrl = state->cfg->invert_spectrum;

	ctrl |= state->cfg->if_freq;

	ret = mxl111sf_tuner_write_reg(state, V6_TUNER_IF_SEL_REG, ctrl);
	if (mxl_fail(ret))
		goto fail;

#if 0
	if_freq /= 1000000;

	/* do round */
	if_freq += 0.5;

	if (MXL_IF_LO == state->cfg->if_freq) {
		ctrl = 0x08;
		iffcw = (u16)(if_freq / (108 * 4096));
	} else if (MXL_IF_HI == state->cfg->if_freq) {
		ctrl = 0x08;
		iffcw = (u16)(if_freq / (216 * 4096));
	} else {
		ctrl = 0;
		iffcw = 0;
	}

	ctrl |= (iffcw >> 8);
#endif
	ret = mxl111sf_tuner_read_reg(state, V6_TUNER_IF_FCW_BYP_REG, &ctrl);
	if (mxl_fail(ret))
		goto fail;

	ctrl &= 0xf0;
	ctrl |= 0x90;

	ret = mxl111sf_tuner_write_reg(state, V6_TUNER_IF_FCW_BYP_REG, ctrl);
	if (mxl_fail(ret))
		goto fail;

#if 0
	ctrl = iffcw & 0x00ff;
#endif
	ret = mxl111sf_tuner_write_reg(state, V6_TUNER_IF_FCW_REG, ctrl);
	if (mxl_fail(ret))
		goto fail;

	state->if_freq = state->cfg->if_freq;
fail:
	return ret;
}

static int mxl1x1sf_tune_rf(struct dvb_frontend *fe, u32 freq, u8 bw)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	static struct mxl111sf_reg_ctrl_info *reg_ctrl_array;
	int ret;
	u8 mxl_mode;

	mxl_dbg("(freq = %d, bw = 0x%x)", freq, bw);

	/* stop tune */
	ret = mxl111sf_tuner_write_reg(state, START_TUNE_REG, 0);
	if (mxl_fail(ret))
		goto fail;

	/* check device mode */
	ret = mxl111sf_tuner_read_reg(state, MXL_MODE_REG, &mxl_mode);
	if (mxl_fail(ret))
		goto fail;

	/* Fill out registers for channel tune */
	reg_ctrl_array = mxl111sf_calc_phy_tune_regs(freq, bw);
	if (!reg_ctrl_array)
		return -EINVAL;

	ret = mxl111sf_tuner_program_regs(state, reg_ctrl_array);
	if (mxl_fail(ret))
		goto fail;

	if ((mxl_mode & MXL_DEV_MODE_MASK) == MXL_TUNER_MODE) {
		/* IF tuner mode only */
		mxl1x1sf_tuner_top_master_ctrl(state, 0);
		mxl1x1sf_tuner_top_master_ctrl(state, 1);
		mxl1x1sf_tuner_set_if_output_freq(state);
	}

	ret = mxl111sf_tuner_write_reg(state, START_TUNE_REG, 1);
	if (mxl_fail(ret))
		goto fail;

	if (state->cfg->ant_hunt)
		state->cfg->ant_hunt(fe);
fail:
	return ret;
}

static int mxl1x1sf_tuner_get_lock_status(struct mxl111sf_tuner_state *state,
					  int *rf_synth_lock,
					  int *ref_synth_lock)
{
	int ret;
	u8 data;

	*rf_synth_lock = 0;
	*ref_synth_lock = 0;

	ret = mxl111sf_tuner_read_reg(state, V6_RF_LOCK_STATUS_REG, &data);
	if (mxl_fail(ret))
		goto fail;

	*ref_synth_lock = ((data & 0x03) == 0x03) ? 1 : 0;
	*rf_synth_lock  = ((data & 0x0c) == 0x0c) ? 1 : 0;
fail:
	return ret;
}

#if 0
static int mxl1x1sf_tuner_loop_thru_ctrl(struct mxl111sf_tuner_state *state,
					 int onoff)
{
	return mxl111sf_tuner_write_reg(state, V6_TUNER_LOOP_THRU_CTRL_REG,
					onoff ? 1 : 0);
}
#endif

/* ------------------------------------------------------------------------ */

static int mxl111sf_tuner_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 delsys  = c->delivery_system;
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	int ret;
	u8 bw;

	mxl_dbg("()");

	switch (delsys) {
	case SYS_ATSC:
	case SYS_ATSCMH:
		bw = 0; /* ATSC */
		break;
	case SYS_DVBC_ANNEX_B:
		bw = 1; /* US CABLE */
		break;
	case SYS_DVBT:
		switch (c->bandwidth_hz) {
		case 6000000:
			bw = 6;
			break;
		case 7000000:
			bw = 7;
			break;
		case 8000000:
			bw = 8;
			break;
		default:
			pr_err("%s: bandwidth not set!", __func__);
			return -EINVAL;
		}
		break;
	default:
		pr_err("%s: modulation type not supported!", __func__);
		return -EINVAL;
	}
	ret = mxl1x1sf_tune_rf(fe, c->frequency, bw);
	if (mxl_fail(ret))
		goto fail;

	state->frequency = c->frequency;
	state->bandwidth = c->bandwidth_hz;
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

#if 0
static int mxl111sf_tuner_init(struct dvb_frontend *fe)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	int ret;

	/* wake from standby handled by usb driver */

	return ret;
}

static int mxl111sf_tuner_sleep(struct dvb_frontend *fe)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	int ret;

	/* enter standby mode handled by usb driver */

	return ret;
}
#endif

/* ------------------------------------------------------------------------ */

static int mxl111sf_tuner_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	int rf_locked, ref_locked, ret;

	*status = 0;

	ret = mxl1x1sf_tuner_get_lock_status(state, &rf_locked, &ref_locked);
	if (mxl_fail(ret))
		goto fail;
	mxl_info("%s%s", rf_locked ? "rf locked " : "",
		 ref_locked ? "ref locked" : "");

	if ((rf_locked) || (ref_locked))
		*status |= TUNER_STATUS_LOCKED;
fail:
	return ret;
}

static int mxl111sf_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	u8 val1, val2;
	int ret;

	*strength = 0;

	ret = mxl111sf_tuner_write_reg(state, 0x00, 0x02);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_tuner_read_reg(state, V6_DIG_RF_PWR_LSB_REG, &val1);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_tuner_read_reg(state, V6_DIG_RF_PWR_MSB_REG, &val2);
	if (mxl_fail(ret))
		goto fail;

	*strength = val1 | ((val2 & 0x07) << 8);
fail:
	ret = mxl111sf_tuner_write_reg(state, 0x00, 0x00);
	mxl_fail(ret);

	return ret;
}

/* ------------------------------------------------------------------------ */

static int mxl111sf_tuner_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	*frequency = state->frequency;
	return 0;
}

static int mxl111sf_tuner_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	*bandwidth = state->bandwidth;
	return 0;
}

static int mxl111sf_tuner_get_if_frequency(struct dvb_frontend *fe,
					   u32 *frequency)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;

	*frequency = 0;

	switch (state->if_freq) {
	case MXL_IF_4_0:   /* 4.0   MHz */
		*frequency = 4000000;
		break;
	case MXL_IF_4_5:   /* 4.5   MHz */
		*frequency = 4500000;
		break;
	case MXL_IF_4_57:  /* 4.57  MHz */
		*frequency = 4570000;
		break;
	case MXL_IF_5_0:   /* 5.0   MHz */
		*frequency = 5000000;
		break;
	case MXL_IF_5_38:  /* 5.38  MHz */
		*frequency = 5380000;
		break;
	case MXL_IF_6_0:   /* 6.0   MHz */
		*frequency = 6000000;
		break;
	case MXL_IF_6_28:  /* 6.28  MHz */
		*frequency = 6280000;
		break;
	case MXL_IF_7_2:   /* 7.2   MHz */
		*frequency = 7200000;
		break;
	case MXL_IF_35_25: /* 35.25 MHz */
		*frequency = 35250000;
		break;
	case MXL_IF_36:    /* 36    MHz */
		*frequency = 36000000;
		break;
	case MXL_IF_36_15: /* 36.15 MHz */
		*frequency = 36150000;
		break;
	case MXL_IF_44:    /* 44    MHz */
		*frequency = 44000000;
		break;
	}
	return 0;
}

static int mxl111sf_tuner_release(struct dvb_frontend *fe)
{
	struct mxl111sf_tuner_state *state = fe->tuner_priv;
	mxl_dbg("()");
	kfree(state);
	fe->tuner_priv = NULL;
	return 0;
}

/* ------------------------------------------------------------------------- */

static struct dvb_tuner_ops mxl111sf_tuner_tuner_ops = {
	.info = {
		.name = "MaxLinear MxL111SF",
#if 0
		.frequency_min  = ,
		.frequency_max  = ,
		.frequency_step = ,
#endif
	},
#if 0
	.init              = mxl111sf_tuner_init,
	.sleep             = mxl111sf_tuner_sleep,
#endif
	.set_params        = mxl111sf_tuner_set_params,
	.get_status        = mxl111sf_tuner_get_status,
	.get_rf_strength   = mxl111sf_get_rf_strength,
	.get_frequency     = mxl111sf_tuner_get_frequency,
	.get_bandwidth     = mxl111sf_tuner_get_bandwidth,
	.get_if_frequency  = mxl111sf_tuner_get_if_frequency,
	.release           = mxl111sf_tuner_release,
};

struct dvb_frontend *mxl111sf_tuner_attach(struct dvb_frontend *fe,
				struct mxl111sf_state *mxl_state,
				const struct mxl111sf_tuner_config *cfg)
{
	struct mxl111sf_tuner_state *state = NULL;

	mxl_dbg("()");

	state = kzalloc(sizeof(struct mxl111sf_tuner_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->mxl_state = mxl_state;
	state->cfg = cfg;

	memcpy(&fe->ops.tuner_ops, &mxl111sf_tuner_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = state;
	return fe;
}
EXPORT_SYMBOL_GPL(mxl111sf_tuner_attach);

MODULE_DESCRIPTION("MaxLinear MxL111SF CMOS tuner driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
