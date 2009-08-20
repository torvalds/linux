/*
 * Driver for Zarlink DVB-T ZL10353 demodulator
 *
 * Copyright (C) 2006, 2007 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "zl10353_priv.h"
#include "zl10353.h"

struct zl10353_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend frontend;

	struct zl10353_config config;

	enum fe_bandwidth bandwidth;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "zl10353: " args); \
	} while (0)

static int debug_regs;

static int zl10353_single_write(struct dvb_frontend *fe, u8 reg, u8 val)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = state->config.demod_address, .flags = 0,
			       .buf = buf, .len = 2 };
	int err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk("zl10353: write to reg %x failed (err = %d)!\n", reg, err);
		return err;
	}
	return 0;
}

static int zl10353_write(struct dvb_frontend *fe, u8 *ibuf, int ilen)
{
	int err, i;
	for (i = 0; i < ilen - 1; i++)
		if ((err = zl10353_single_write(fe, ibuf[0] + i, ibuf[i + 1])))
			return err;

	return 0;
}

static int zl10353_read_register(struct zl10353_state *state, u8 reg)
{
	int ret;
	u8 b0[1] = { reg };
	u8 b1[1] = { 0 };
	struct i2c_msg msg[2] = { { .addr = state->config.demod_address,
				    .flags = 0,
				    .buf = b0, .len = 1 },
				  { .addr = state->config.demod_address,
				    .flags = I2C_M_RD,
				    .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk("%s: readreg error (reg=%d, ret==%i)\n",
		       __func__, reg, ret);
		return ret;
	}

	return b1[0];
}

static void zl10353_dump_regs(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;
	int ret;
	u8 reg;

	/* Dump all registers. */
	for (reg = 0; ; reg++) {
		if (reg % 16 == 0) {
			if (reg)
				printk(KERN_CONT "\n");
			printk(KERN_DEBUG "%02x:", reg);
		}
		ret = zl10353_read_register(state, reg);
		if (ret >= 0)
			printk(KERN_CONT " %02x", (u8)ret);
		else
			printk(KERN_CONT " --");
		if (reg == 0xff)
			break;
	}
	printk(KERN_CONT "\n");
}

static void zl10353_calc_nominal_rate(struct dvb_frontend *fe,
				      enum fe_bandwidth bandwidth,
				      u16 *nominal_rate)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u32 adc_clock = 450560; /* 45.056 MHz */
	u64 value;
	u8 bw;

	if (state->config.adc_clock)
		adc_clock = state->config.adc_clock;

	switch (bandwidth) {
	case BANDWIDTH_6_MHZ:
		bw = 6;
		break;
	case BANDWIDTH_7_MHZ:
		bw = 7;
		break;
	case BANDWIDTH_8_MHZ:
	default:
		bw = 8;
		break;
	}

	value = (u64)10 * (1 << 23) / 7 * 125;
	value = (bw * value) + adc_clock / 2;
	do_div(value, adc_clock);
	*nominal_rate = value;

	dprintk("%s: bw %d, adc_clock %d => 0x%x\n",
		__func__, bw, adc_clock, *nominal_rate);
}

static void zl10353_calc_input_freq(struct dvb_frontend *fe,
				    u16 *input_freq)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u32 adc_clock = 450560;	/* 45.056  MHz */
	int if2 = 361667;	/* 36.1667 MHz */
	int ife;
	u64 value;

	if (state->config.adc_clock)
		adc_clock = state->config.adc_clock;
	if (state->config.if2)
		if2 = state->config.if2;

	if (adc_clock >= if2 * 2)
		ife = if2;
	else {
		ife = adc_clock - (if2 % adc_clock);
		if (ife > adc_clock / 2)
			ife = adc_clock - ife;
	}
	value = (u64)65536 * ife + adc_clock / 2;
	do_div(value, adc_clock);
	*input_freq = -value;

	dprintk("%s: if2 %d, ife %d, adc_clock %d => %d / 0x%x\n",
		__func__, if2, ife, adc_clock, -(int)value, *input_freq);
}

static int zl10353_sleep(struct dvb_frontend *fe)
{
	static u8 zl10353_softdown[] = { 0x50, 0x0C, 0x44 };

	zl10353_write(fe, zl10353_softdown, sizeof(zl10353_softdown));
	return 0;
}

static int zl10353_set_parameters(struct dvb_frontend *fe,
				  struct dvb_frontend_parameters *param)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u16 nominal_rate, input_freq;
	u8 pllbuf[6] = { 0x67 }, acq_ctl = 0;
	u16 tps = 0;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;

	zl10353_single_write(fe, RESET, 0x80);
	udelay(200);
	zl10353_single_write(fe, 0xEA, 0x01);
	udelay(200);
	zl10353_single_write(fe, 0xEA, 0x00);

	zl10353_single_write(fe, AGC_TARGET, 0x28);

	if (op->transmission_mode != TRANSMISSION_MODE_AUTO)
		acq_ctl |= (1 << 0);
	if (op->guard_interval != GUARD_INTERVAL_AUTO)
		acq_ctl |= (1 << 1);
	zl10353_single_write(fe, ACQ_CTL, acq_ctl);

	switch (op->bandwidth) {
	case BANDWIDTH_6_MHZ:
		/* These are extrapolated from the 7 and 8MHz values */
		zl10353_single_write(fe, MCLK_RATIO, 0x97);
		zl10353_single_write(fe, 0x64, 0x34);
		zl10353_single_write(fe, 0xcc, 0xdd);
		break;
	case BANDWIDTH_7_MHZ:
		zl10353_single_write(fe, MCLK_RATIO, 0x86);
		zl10353_single_write(fe, 0x64, 0x35);
		zl10353_single_write(fe, 0xcc, 0x73);
		break;
	case BANDWIDTH_8_MHZ:
	default:
		zl10353_single_write(fe, MCLK_RATIO, 0x75);
		zl10353_single_write(fe, 0x64, 0x36);
		zl10353_single_write(fe, 0xcc, 0x73);
	}

	zl10353_calc_nominal_rate(fe, op->bandwidth, &nominal_rate);
	zl10353_single_write(fe, TRL_NOMINAL_RATE_1, msb(nominal_rate));
	zl10353_single_write(fe, TRL_NOMINAL_RATE_0, lsb(nominal_rate));
	state->bandwidth = op->bandwidth;

	zl10353_calc_input_freq(fe, &input_freq);
	zl10353_single_write(fe, INPUT_FREQ_1, msb(input_freq));
	zl10353_single_write(fe, INPUT_FREQ_0, lsb(input_freq));

	/* Hint at TPS settings */
	switch (op->code_rate_HP) {
	case FEC_2_3:
		tps |= (1 << 7);
		break;
	case FEC_3_4:
		tps |= (2 << 7);
		break;
	case FEC_5_6:
		tps |= (3 << 7);
		break;
	case FEC_7_8:
		tps |= (4 << 7);
		break;
	case FEC_1_2:
	case FEC_AUTO:
		break;
	default:
		return -EINVAL;
	}

	switch (op->code_rate_LP) {
	case FEC_2_3:
		tps |= (1 << 4);
		break;
	case FEC_3_4:
		tps |= (2 << 4);
		break;
	case FEC_5_6:
		tps |= (3 << 4);
		break;
	case FEC_7_8:
		tps |= (4 << 4);
		break;
	case FEC_1_2:
	case FEC_AUTO:
		break;
	case FEC_NONE:
		if (op->hierarchy_information == HIERARCHY_AUTO ||
		    op->hierarchy_information == HIERARCHY_NONE)
			break;
	default:
		return -EINVAL;
	}

	switch (op->constellation) {
	case QPSK:
		break;
	case QAM_AUTO:
	case QAM_16:
		tps |= (1 << 13);
		break;
	case QAM_64:
		tps |= (2 << 13);
		break;
	default:
		return -EINVAL;
	}

	switch (op->transmission_mode) {
	case TRANSMISSION_MODE_2K:
	case TRANSMISSION_MODE_AUTO:
		break;
	case TRANSMISSION_MODE_8K:
		tps |= (1 << 0);
		break;
	default:
		return -EINVAL;
	}

	switch (op->guard_interval) {
	case GUARD_INTERVAL_1_32:
	case GUARD_INTERVAL_AUTO:
		break;
	case GUARD_INTERVAL_1_16:
		tps |= (1 << 2);
		break;
	case GUARD_INTERVAL_1_8:
		tps |= (2 << 2);
		break;
	case GUARD_INTERVAL_1_4:
		tps |= (3 << 2);
		break;
	default:
		return -EINVAL;
	}

	switch (op->hierarchy_information) {
	case HIERARCHY_AUTO:
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		tps |= (1 << 10);
		break;
	case HIERARCHY_2:
		tps |= (2 << 10);
		break;
	case HIERARCHY_4:
		tps |= (3 << 10);
		break;
	default:
		return -EINVAL;
	}

	zl10353_single_write(fe, TPS_GIVEN_1, msb(tps));
	zl10353_single_write(fe, TPS_GIVEN_0, lsb(tps));

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	/*
	 * If there is no tuner attached to the secondary I2C bus, we call
	 * set_params to program a potential tuner attached somewhere else.
	 * Otherwise, we update the PLL registers via calc_regs.
	 */
	if (state->config.no_tuner) {
		if (fe->ops.tuner_ops.set_params) {
			fe->ops.tuner_ops.set_params(fe, param);
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}
	} else if (fe->ops.tuner_ops.calc_regs) {
		fe->ops.tuner_ops.calc_regs(fe, param, pllbuf + 1, 5);
		pllbuf[1] <<= 1;
		zl10353_write(fe, pllbuf, sizeof(pllbuf));
	}

	zl10353_single_write(fe, 0x5F, 0x13);

	/* If no attached tuner or invalid PLL registers, just start the FSM. */
	if (state->config.no_tuner || fe->ops.tuner_ops.calc_regs == NULL)
		zl10353_single_write(fe, FSM_GO, 0x01);
	else
		zl10353_single_write(fe, TUNER_GO, 0x01);

	return 0;
}

static int zl10353_get_parameters(struct dvb_frontend *fe,
				  struct dvb_frontend_parameters *param)
{
	struct zl10353_state *state = fe->demodulator_priv;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;
	int s6, s9;
	u16 tps;
	static const u8 tps_fec_to_api[8] = {
		FEC_1_2,
		FEC_2_3,
		FEC_3_4,
		FEC_5_6,
		FEC_7_8,
		FEC_AUTO,
		FEC_AUTO,
		FEC_AUTO
	};

	s6 = zl10353_read_register(state, STATUS_6);
	s9 = zl10353_read_register(state, STATUS_9);
	if (s6 < 0 || s9 < 0)
		return -EREMOTEIO;
	if ((s6 & (1 << 5)) == 0 || (s9 & (1 << 4)) == 0)
		return -EINVAL;	/* no FE or TPS lock */

	tps = zl10353_read_register(state, TPS_RECEIVED_1) << 8 |
	      zl10353_read_register(state, TPS_RECEIVED_0);

	op->code_rate_HP = tps_fec_to_api[(tps >> 7) & 7];
	op->code_rate_LP = tps_fec_to_api[(tps >> 4) & 7];

	switch ((tps >> 13) & 3) {
	case 0:
		op->constellation = QPSK;
		break;
	case 1:
		op->constellation = QAM_16;
		break;
	case 2:
		op->constellation = QAM_64;
		break;
	default:
		op->constellation = QAM_AUTO;
		break;
	}

	op->transmission_mode = (tps & 0x01) ? TRANSMISSION_MODE_8K :
					       TRANSMISSION_MODE_2K;

	switch ((tps >> 2) & 3) {
	case 0:
		op->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		op->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		op->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		op->guard_interval = GUARD_INTERVAL_1_4;
		break;
	default:
		op->guard_interval = GUARD_INTERVAL_AUTO;
		break;
	}

	switch ((tps >> 10) & 7) {
	case 0:
		op->hierarchy_information = HIERARCHY_NONE;
		break;
	case 1:
		op->hierarchy_information = HIERARCHY_1;
		break;
	case 2:
		op->hierarchy_information = HIERARCHY_2;
		break;
	case 3:
		op->hierarchy_information = HIERARCHY_4;
		break;
	default:
		op->hierarchy_information = HIERARCHY_AUTO;
		break;
	}

	param->frequency = 0;
	op->bandwidth = state->bandwidth;
	param->inversion = INVERSION_AUTO;

	return 0;
}

static int zl10353_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct zl10353_state *state = fe->demodulator_priv;
	int s6, s7, s8;

	if ((s6 = zl10353_read_register(state, STATUS_6)) < 0)
		return -EREMOTEIO;
	if ((s7 = zl10353_read_register(state, STATUS_7)) < 0)
		return -EREMOTEIO;
	if ((s8 = zl10353_read_register(state, STATUS_8)) < 0)
		return -EREMOTEIO;

	*status = 0;
	if (s6 & (1 << 2))
		*status |= FE_HAS_CARRIER;
	if (s6 & (1 << 1))
		*status |= FE_HAS_VITERBI;
	if (s6 & (1 << 5))
		*status |= FE_HAS_LOCK;
	if (s7 & (1 << 4))
		*status |= FE_HAS_SYNC;
	if (s8 & (1 << 6))
		*status |= FE_HAS_SIGNAL;

	if ((*status & (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC)) !=
	    (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC))
		*status &= ~FE_HAS_LOCK;

	return 0;
}

static int zl10353_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct zl10353_state *state = fe->demodulator_priv;

	*ber = zl10353_read_register(state, RS_ERR_CNT_2) << 16 |
	       zl10353_read_register(state, RS_ERR_CNT_1) << 8 |
	       zl10353_read_register(state, RS_ERR_CNT_0);

	return 0;
}

static int zl10353_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct zl10353_state *state = fe->demodulator_priv;

	u16 signal = zl10353_read_register(state, AGC_GAIN_1) << 10 |
		     zl10353_read_register(state, AGC_GAIN_0) << 2 | 3;

	*strength = ~signal;

	return 0;
}

static int zl10353_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 _snr;

	if (debug_regs)
		zl10353_dump_regs(fe);

	_snr = zl10353_read_register(state, SNR);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int zl10353_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct zl10353_state *state = fe->demodulator_priv;

	*ucblocks = zl10353_read_register(state, RS_UBC_1) << 8 |
		    zl10353_read_register(state, RS_UBC_0);

	return 0;
}

static int zl10353_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings
					 *fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 1000;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static int zl10353_init(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 zl10353_reset_attach[6] = { 0x50, 0x03, 0x64, 0x46, 0x15, 0x0F };
	int rc = 0;

	if (debug_regs)
		zl10353_dump_regs(fe);
	if (state->config.parallel_ts)
		zl10353_reset_attach[2] &= ~0x20;
	if (state->config.clock_ctl_1)
		zl10353_reset_attach[3] = state->config.clock_ctl_1;
	if (state->config.pll_0)
		zl10353_reset_attach[4] = state->config.pll_0;

	/* Do a "hard" reset if not already done */
	if (zl10353_read_register(state, 0x50) != zl10353_reset_attach[1] ||
	    zl10353_read_register(state, 0x51) != zl10353_reset_attach[2]) {
		rc = zl10353_write(fe, zl10353_reset_attach,
				   sizeof(zl10353_reset_attach));
		if (debug_regs)
			zl10353_dump_regs(fe);
	}

	return 0;
}

static int zl10353_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 val = 0x0a;

	if (state->config.disable_i2c_gate_ctrl) {
		/* No tuner attached to the internal I2C bus */
		/* If set enable I2C bridge, the main I2C bus stopped hardly */
		return 0;
	}

	if (enable)
		val |= 0x10;

	return zl10353_single_write(fe, 0x62, val);
}

static void zl10353_release(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops zl10353_ops;

struct dvb_frontend *zl10353_attach(const struct zl10353_config *config,
				    struct i2c_adapter *i2c)
{
	struct zl10353_state *state = NULL;
	int id;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct zl10353_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config, config, sizeof(struct zl10353_config));

	/* check if the demod is there */
	id = zl10353_read_register(state, CHIP_ID);
	if ((id != ID_ZL10353) && (id != ID_CE6230) && (id != ID_CE6231))
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &zl10353_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	return &state->frontend;
error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops zl10353_ops = {

	.info = {
		.name			= "Zarlink ZL10353 DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 174000000,
		.frequency_max		= 862000000,
		.frequency_stepsize	= 166667,
		.frequency_tolerance	= 0,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	},

	.release = zl10353_release,

	.init = zl10353_init,
	.sleep = zl10353_sleep,
	.i2c_gate_ctrl = zl10353_i2c_gate_ctrl,
	.write = zl10353_write,

	.set_frontend = zl10353_set_parameters,
	.get_frontend = zl10353_get_parameters,
	.get_tune_settings = zl10353_get_tune_settings,

	.read_status = zl10353_read_status,
	.read_ber = zl10353_read_ber,
	.read_signal_strength = zl10353_read_signal_strength,
	.read_snr = zl10353_read_snr,
	.read_ucblocks = zl10353_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

module_param(debug_regs, int, 0644);
MODULE_PARM_DESC(debug_regs, "Turn on/off frontend register dumps (default:off).");

MODULE_DESCRIPTION("Zarlink ZL10353 DVB-T demodulator driver");
MODULE_AUTHOR("Chris Pascoe");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(zl10353_attach);
