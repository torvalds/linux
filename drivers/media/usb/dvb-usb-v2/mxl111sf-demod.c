/*
 *  mxl111sf-demod.c - driver for the MaxLinear MXL111SF DVB-T demodulator
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
 */

#include "mxl111sf-demod.h"
#include "mxl111sf-reg.h"

/* debug */
static int mxl111sf_demod_debug;
module_param_named(debug, mxl111sf_demod_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able)).");

#define mxl_dbg(fmt, arg...) \
	if (mxl111sf_demod_debug) \
		mxl_printk(KERN_DEBUG, fmt, ##arg)

/* ------------------------------------------------------------------------ */

struct mxl111sf_demod_state {
	struct mxl111sf_state *mxl_state;

	const struct mxl111sf_demod_config *cfg;

	struct dvb_frontend fe;
};

/* ------------------------------------------------------------------------ */

static int mxl111sf_demod_read_reg(struct mxl111sf_demod_state *state,
				   u8 addr, u8 *data)
{
	return (state->cfg->read_reg) ?
		state->cfg->read_reg(state->mxl_state, addr, data) :
		-EINVAL;
}

static int mxl111sf_demod_write_reg(struct mxl111sf_demod_state *state,
				    u8 addr, u8 data)
{
	return (state->cfg->write_reg) ?
		state->cfg->write_reg(state->mxl_state, addr, data) :
		-EINVAL;
}

static
int mxl111sf_demod_program_regs(struct mxl111sf_demod_state *state,
				struct mxl111sf_reg_ctrl_info *ctrl_reg_info)
{
	return (state->cfg->program_regs) ?
		state->cfg->program_regs(state->mxl_state, ctrl_reg_info) :
		-EINVAL;
}

/* ------------------------------------------------------------------------ */
/* TPS */

static
int mxl1x1sf_demod_get_tps_code_rate(struct mxl111sf_demod_state *state,
				     enum fe_code_rate *code_rate)
{
	u8 val;
	int ret = mxl111sf_demod_read_reg(state, V6_CODE_RATE_TPS_REG, &val);
	/* bit<2:0> - 000:1/2, 001:2/3, 010:3/4, 011:5/6, 100:7/8 */
	if (mxl_fail(ret))
		goto fail;

	switch (val & V6_CODE_RATE_TPS_MASK) {
	case 0:
		*code_rate = FEC_1_2;
		break;
	case 1:
		*code_rate = FEC_2_3;
		break;
	case 2:
		*code_rate = FEC_3_4;
		break;
	case 3:
		*code_rate = FEC_5_6;
		break;
	case 4:
		*code_rate = FEC_7_8;
		break;
	}
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_tps_modulation(struct mxl111sf_demod_state *state,
				      enum fe_modulation *modulation)
{
	u8 val;
	int ret = mxl111sf_demod_read_reg(state, V6_MODORDER_TPS_REG, &val);
	/* Constellation, 00 : QPSK, 01 : 16QAM, 10:64QAM */
	if (mxl_fail(ret))
		goto fail;

	switch ((val & V6_PARAM_CONSTELLATION_MASK) >> 4) {
	case 0:
		*modulation = QPSK;
		break;
	case 1:
		*modulation = QAM_16;
		break;
	case 2:
		*modulation = QAM_64;
		break;
	}
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_tps_guard_fft_mode(struct mxl111sf_demod_state *state,
					  enum fe_transmit_mode *fft_mode)
{
	u8 val;
	int ret = mxl111sf_demod_read_reg(state, V6_MODE_TPS_REG, &val);
	/* FFT Mode, 00:2K, 01:8K, 10:4K */
	if (mxl_fail(ret))
		goto fail;

	switch ((val & V6_PARAM_FFT_MODE_MASK) >> 2) {
	case 0:
		*fft_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		*fft_mode = TRANSMISSION_MODE_8K;
		break;
	case 2:
		*fft_mode = TRANSMISSION_MODE_4K;
		break;
	}
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_tps_guard_interval(struct mxl111sf_demod_state *state,
					  enum fe_guard_interval *guard)
{
	u8 val;
	int ret = mxl111sf_demod_read_reg(state, V6_CP_TPS_REG, &val);
	/* 00:1/32, 01:1/16, 10:1/8, 11:1/4 */
	if (mxl_fail(ret))
		goto fail;

	switch ((val & V6_PARAM_GI_MASK) >> 4) {
	case 0:
		*guard = GUARD_INTERVAL_1_32;
		break;
	case 1:
		*guard = GUARD_INTERVAL_1_16;
		break;
	case 2:
		*guard = GUARD_INTERVAL_1_8;
		break;
	case 3:
		*guard = GUARD_INTERVAL_1_4;
		break;
	}
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_tps_hierarchy(struct mxl111sf_demod_state *state,
				     enum fe_hierarchy *hierarchy)
{
	u8 val;
	int ret = mxl111sf_demod_read_reg(state, V6_TPS_HIERACHY_REG, &val);
	/* bit<6:4> - 000:Non hierarchy, 001:1, 010:2, 011:4 */
	if (mxl_fail(ret))
		goto fail;

	switch ((val & V6_TPS_HIERARCHY_INFO_MASK) >> 6) {
	case 0:
		*hierarchy = HIERARCHY_NONE;
		break;
	case 1:
		*hierarchy = HIERARCHY_1;
		break;
	case 2:
		*hierarchy = HIERARCHY_2;
		break;
	case 3:
		*hierarchy = HIERARCHY_4;
		break;
	}
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */
/* LOCKS */

static
int mxl1x1sf_demod_get_sync_lock_status(struct mxl111sf_demod_state *state,
					int *sync_lock)
{
	u8 val = 0;
	int ret = mxl111sf_demod_read_reg(state, V6_SYNC_LOCK_REG, &val);
	if (mxl_fail(ret))
		goto fail;
	*sync_lock = (val & SYNC_LOCK_MASK) >> 4;
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_rs_lock_status(struct mxl111sf_demod_state *state,
				      int *rs_lock)
{
	u8 val = 0;
	int ret = mxl111sf_demod_read_reg(state, V6_RS_LOCK_DET_REG, &val);
	if (mxl_fail(ret))
		goto fail;
	*rs_lock = (val & RS_LOCK_DET_MASK) >> 3;
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_tps_lock_status(struct mxl111sf_demod_state *state,
				       int *tps_lock)
{
	u8 val = 0;
	int ret = mxl111sf_demod_read_reg(state, V6_TPS_LOCK_REG, &val);
	if (mxl_fail(ret))
		goto fail;
	*tps_lock = (val & V6_PARAM_TPS_LOCK_MASK) >> 6;
fail:
	return ret;
}

static
int mxl1x1sf_demod_get_fec_lock_status(struct mxl111sf_demod_state *state,
				       int *fec_lock)
{
	u8 val = 0;
	int ret = mxl111sf_demod_read_reg(state, V6_IRQ_STATUS_REG, &val);
	if (mxl_fail(ret))
		goto fail;
	*fec_lock = (val & IRQ_MASK_FEC_LOCK) >> 4;
fail:
	return ret;
}

#if 0
static
int mxl1x1sf_demod_get_cp_lock_status(struct mxl111sf_demod_state *state,
				      int *cp_lock)
{
	u8 val = 0;
	int ret = mxl111sf_demod_read_reg(state, V6_CP_LOCK_DET_REG, &val);
	if (mxl_fail(ret))
		goto fail;
	*cp_lock = (val & V6_CP_LOCK_DET_MASK) >> 2;
fail:
	return ret;
}
#endif

static int mxl1x1sf_demod_reset_irq_status(struct mxl111sf_demod_state *state)
{
	return mxl111sf_demod_write_reg(state, 0x0e, 0xff);
}

/* ------------------------------------------------------------------------ */

static int mxl111sf_demod_set_frontend(struct dvb_frontend *fe)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	int ret = 0;

	struct mxl111sf_reg_ctrl_info phy_pll_patch[] = {
		{0x00, 0xff, 0x01}, /* change page to 1 */
		{0x40, 0xff, 0x05},
		{0x40, 0xff, 0x01},
		{0x41, 0xff, 0xca},
		{0x41, 0xff, 0xc0},
		{0x00, 0xff, 0x00}, /* change page to 0 */
		{0,    0,    0}
	};

	mxl_dbg("()");

	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (mxl_fail(ret))
			goto fail;
		msleep(50);
	}
	ret = mxl111sf_demod_program_regs(state, phy_pll_patch);
	mxl_fail(ret);
	msleep(50);
	ret = mxl1x1sf_demod_reset_irq_status(state);
	mxl_fail(ret);
	msleep(100);
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

#if 0
/* resets TS Packet error count */
/* After setting 7th bit of V5_PER_COUNT_RESET_REG, it should be reset to 0. */
static
int mxl1x1sf_demod_reset_packet_error_count(struct mxl111sf_demod_state *state)
{
	struct mxl111sf_reg_ctrl_info reset_per_count[] = {
		{0x20, 0x01, 0x01},
		{0x20, 0x01, 0x00},
		{0,    0,    0}
	};
	return mxl111sf_demod_program_regs(state, reset_per_count);
}
#endif

/* returns TS Packet error count */
/* PER Count = FEC_PER_COUNT * (2 ** (FEC_PER_SCALE * 4)) */
static int mxl111sf_demod_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	u32 fec_per_count, fec_per_scale;
	u8 val;
	int ret;

	*ucblocks = 0;

	/* FEC_PER_COUNT Register */
	ret = mxl111sf_demod_read_reg(state, V6_FEC_PER_COUNT_REG, &val);
	if (mxl_fail(ret))
		goto fail;

	fec_per_count = val;

	/* FEC_PER_SCALE Register */
	ret = mxl111sf_demod_read_reg(state, V6_FEC_PER_SCALE_REG, &val);
	if (mxl_fail(ret))
		goto fail;

	val &= V6_FEC_PER_SCALE_MASK;
	val *= 4;

	fec_per_scale = 1 << val;

	fec_per_count *= fec_per_scale;

	*ucblocks = fec_per_count;
fail:
	return ret;
}

#ifdef MXL111SF_DEMOD_ENABLE_CALCULATIONS
/* FIXME: leaving this enabled breaks the build on some architectures,
 * and we shouldn't have any floating point math in the kernel, anyway.
 *
 * These macros need to be re-written, but it's harmless to simply
 * return zero for now. */
#define CALCULATE_BER(avg_errors, count) \
	((u32)(avg_errors * 4)/(count*64*188*8))
#define CALCULATE_SNR(data) \
	((u32)((10 * (u32)data / 64) - 2.5))
#else
#define CALCULATE_BER(avg_errors, count) 0
#define CALCULATE_SNR(data) 0
#endif

static int mxl111sf_demod_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	u8 val1, val2, val3;
	int ret;

	*ber = 0;

	ret = mxl111sf_demod_read_reg(state, V6_RS_AVG_ERRORS_LSB_REG, &val1);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_demod_read_reg(state, V6_RS_AVG_ERRORS_MSB_REG, &val2);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_demod_read_reg(state, V6_N_ACCUMULATE_REG, &val3);
	if (mxl_fail(ret))
		goto fail;

	*ber = CALCULATE_BER((val1 | (val2 << 8)), val3);
fail:
	return ret;
}

static int mxl111sf_demod_calc_snr(struct mxl111sf_demod_state *state,
				   u16 *snr)
{
	u8 val1, val2;
	int ret;

	*snr = 0;

	ret = mxl111sf_demod_read_reg(state, V6_SNR_RB_LSB_REG, &val1);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl111sf_demod_read_reg(state, V6_SNR_RB_MSB_REG, &val2);
	if (mxl_fail(ret))
		goto fail;

	*snr = CALCULATE_SNR(val1 | ((val2 & 0x03) << 8));
fail:
	return ret;
}

static int mxl111sf_demod_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;

	int ret = mxl111sf_demod_calc_snr(state, snr);
	if (mxl_fail(ret))
		goto fail;

	*snr /= 10; /* 0.1 dB */
fail:
	return ret;
}

static int mxl111sf_demod_read_status(struct dvb_frontend *fe,
				      enum fe_status *status)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	int ret, locked, cr_lock, sync_lock, fec_lock;

	*status = 0;

	ret = mxl1x1sf_demod_get_rs_lock_status(state, &locked);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl1x1sf_demod_get_tps_lock_status(state, &cr_lock);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl1x1sf_demod_get_sync_lock_status(state, &sync_lock);
	if (mxl_fail(ret))
		goto fail;
	ret = mxl1x1sf_demod_get_fec_lock_status(state, &fec_lock);
	if (mxl_fail(ret))
		goto fail;

	if (locked)
		*status |= FE_HAS_SIGNAL;
	if (cr_lock)
		*status |= FE_HAS_CARRIER;
	if (sync_lock)
		*status |= FE_HAS_SYNC;
	if (fec_lock) /* false positives? */
		*status |= FE_HAS_VITERBI;

	if ((locked) && (cr_lock) && (sync_lock))
		*status |= FE_HAS_LOCK;
fail:
	return ret;
}

static int mxl111sf_demod_read_signal_strength(struct dvb_frontend *fe,
					       u16 *signal_strength)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	enum fe_modulation modulation;
	int ret;
	u16 snr;

	ret = mxl111sf_demod_calc_snr(state, &snr);
	if (ret < 0)
		return ret;
	ret = mxl1x1sf_demod_get_tps_modulation(state, &modulation);
	if (ret < 0)
		return ret;

	switch (modulation) {
	case QPSK:
		*signal_strength = (snr >= 1300) ?
			min(65535, snr * 44) : snr * 38;
		break;
	case QAM_16:
		*signal_strength = (snr >= 1500) ?
			min(65535, snr * 38) : snr * 33;
		break;
	case QAM_64:
		*signal_strength = (snr >= 2000) ?
			min(65535, snr * 29) : snr * 25;
		break;
	default:
		*signal_strength = 0;
		return -EINVAL;
	}

	return 0;
}

static int mxl111sf_demod_get_frontend(struct dvb_frontend *fe,
				       struct dtv_frontend_properties *p)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;

	mxl_dbg("()");
#if 0
	p->inversion = /* FIXME */ ? INVERSION_ON : INVERSION_OFF;
#endif
	if (fe->ops.tuner_ops.get_bandwidth)
		fe->ops.tuner_ops.get_bandwidth(fe, &p->bandwidth_hz);
	if (fe->ops.tuner_ops.get_frequency)
		fe->ops.tuner_ops.get_frequency(fe, &p->frequency);
	mxl1x1sf_demod_get_tps_code_rate(state, &p->code_rate_HP);
	mxl1x1sf_demod_get_tps_code_rate(state, &p->code_rate_LP);
	mxl1x1sf_demod_get_tps_modulation(state, &p->modulation);
	mxl1x1sf_demod_get_tps_guard_fft_mode(state,
					      &p->transmission_mode);
	mxl1x1sf_demod_get_tps_guard_interval(state,
					      &p->guard_interval);
	mxl1x1sf_demod_get_tps_hierarchy(state,
					 &p->hierarchy);

	return 0;
}

static
int mxl111sf_demod_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void mxl111sf_demod_release(struct dvb_frontend *fe)
{
	struct mxl111sf_demod_state *state = fe->demodulator_priv;
	mxl_dbg("()");
	kfree(state);
	fe->demodulator_priv = NULL;
}

static const struct dvb_frontend_ops mxl111sf_demod_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name               = "MaxLinear MxL111SF DVB-T demodulator",
		.frequency_min      = 177000000,
		.frequency_max      = 858000000,
		.frequency_stepsize = 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_HIERARCHY_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER
	},
	.release              = mxl111sf_demod_release,
#if 0
	.init                 = mxl111sf_init,
	.i2c_gate_ctrl        = mxl111sf_i2c_gate_ctrl,
#endif
	.set_frontend         = mxl111sf_demod_set_frontend,
	.get_frontend         = mxl111sf_demod_get_frontend,
	.get_tune_settings    = mxl111sf_demod_get_tune_settings,
	.read_status          = mxl111sf_demod_read_status,
	.read_signal_strength = mxl111sf_demod_read_signal_strength,
	.read_ber             = mxl111sf_demod_read_ber,
	.read_snr             = mxl111sf_demod_read_snr,
	.read_ucblocks        = mxl111sf_demod_read_ucblocks,
};

struct dvb_frontend *mxl111sf_demod_attach(struct mxl111sf_state *mxl_state,
				   const struct mxl111sf_demod_config *cfg)
{
	struct mxl111sf_demod_state *state = NULL;

	mxl_dbg("()");

	state = kzalloc(sizeof(struct mxl111sf_demod_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->mxl_state = mxl_state;
	state->cfg = cfg;

	memcpy(&state->fe.ops, &mxl111sf_demod_ops,
	       sizeof(struct dvb_frontend_ops));

	state->fe.demodulator_priv = state;
	return &state->fe;
}
EXPORT_SYMBOL_GPL(mxl111sf_demod_attach);

MODULE_DESCRIPTION("MaxLinear MxL111SF DVB-T demodulator driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
