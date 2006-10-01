/*
 *  Driver for Zarlink DVB-T MT352 demodulator
 *
 *  Written by Holger Waechtler <holger@qanu.de>
 *	 and Daniel Mack <daniel@qanu.de>
 *
 *  AVerMedia AVerTV DVB-T 771 support by
 *       Wolfram Joost <dbox2@frokaschwei.de>
 *
 *  Support for Samsung TDTC9251DH01C(M) tuner
 *  Copyright (C) 2004 Antonio Mancuso <antonio.mancuso@digitaltelevision.it>
 *                     Amauri  Celani  <acelani@essegi.net>
 *
 *  DVICO FusionHDTV DVB-T1 and DVICO FusionHDTV DVB-T Lite support by
 *       Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "mt352_priv.h"
#include "mt352.h"

struct mt352_state {
	struct i2c_adapter* i2c;
	struct dvb_frontend frontend;

	/* configuration settings */
	struct mt352_config config;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "mt352: " args); \
	} while (0)

static int mt352_single_write(struct dvb_frontend *fe, u8 reg, u8 val)
{
	struct mt352_state* state = fe->demodulator_priv;
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = state->config.demod_address, .flags = 0,
			       .buf = buf, .len = 2 };
	int err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk("mt352_write() to reg %x failed (err = %d)!\n", reg, err);
		return err;
	}
	return 0;
}

static int _mt352_write(struct dvb_frontend* fe, u8* ibuf, int ilen)
{
	int err,i;
	for (i=0; i < ilen-1; i++)
		if ((err = mt352_single_write(fe,ibuf[0]+i,ibuf[i+1])))
			return err;

	return 0;
}

static int mt352_read_register(struct mt352_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config.demod_address,
				    .flags = 0,
				    .buf = b0, .len = 1 },
				  { .addr = state->config.demod_address,
				    .flags = I2C_M_RD,
				    .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk("%s: readreg error (reg=%d, ret==%i)\n",
		       __FUNCTION__, reg, ret);
		return ret;
	}

	return b1[0];
}

static int mt352_sleep(struct dvb_frontend* fe)
{
	static u8 mt352_softdown[] = { CLOCK_CTL, 0x20, 0x08 };

	_mt352_write(fe, mt352_softdown, sizeof(mt352_softdown));
	return 0;
}

static void mt352_calc_nominal_rate(struct mt352_state* state,
				    enum fe_bandwidth bandwidth,
				    unsigned char *buf)
{
	u32 adc_clock = 20480; /* 20.340 MHz */
	u32 bw,value;

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
	if (state->config.adc_clock)
		adc_clock = state->config.adc_clock;

	value = 64 * bw * (1<<16) / (7 * 8);
	value = value * 1000 / adc_clock;
	dprintk("%s: bw %d, adc_clock %d => 0x%x\n",
		__FUNCTION__, bw, adc_clock, value);
	buf[0] = msb(value);
	buf[1] = lsb(value);
}

static void mt352_calc_input_freq(struct mt352_state* state,
				  unsigned char *buf)
{
	int adc_clock = 20480; /* 20.480000 MHz */
	int if2       = 36167; /* 36.166667 MHz */
	int ife,value;

	if (state->config.adc_clock)
		adc_clock = state->config.adc_clock;
	if (state->config.if2)
		if2 = state->config.if2;

	ife = (2*adc_clock - if2);
	value = -16374 * ife / adc_clock;
	dprintk("%s: if2 %d, ife %d, adc_clock %d => %d / 0x%x\n",
		__FUNCTION__, if2, ife, adc_clock, value, value & 0x3fff);
	buf[0] = msb(value);
	buf[1] = lsb(value);
}

static int mt352_set_parameters(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *param)
{
	struct mt352_state* state = fe->demodulator_priv;
	unsigned char buf[13];
	static unsigned char tuner_go[] = { 0x5d, 0x01 };
	static unsigned char fsm_go[]   = { 0x5e, 0x01 };
	unsigned int tps = 0;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;

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


	buf[0] = TPS_GIVEN_1; /* TPS_GIVEN_1 and following registers */

	buf[1] = msb(tps);      /* TPS_GIVEN_(1|0) */
	buf[2] = lsb(tps);

	buf[3] = 0x50;  // old
//	buf[3] = 0xf4;  // pinnacle

	mt352_calc_nominal_rate(state, op->bandwidth, buf+4);
	mt352_calc_input_freq(state, buf+6);

	if (state->config.no_tuner) {
		if (fe->ops.tuner_ops.set_params) {
			fe->ops.tuner_ops.set_params(fe, param);
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}

		_mt352_write(fe, buf, 8);
		_mt352_write(fe, fsm_go, 2);
	} else {
		if (fe->ops.tuner_ops.calc_regs) {
			fe->ops.tuner_ops.calc_regs(fe, param, buf+8, 5);
			buf[8] <<= 1;
			_mt352_write(fe, buf, sizeof(buf));
			_mt352_write(fe, tuner_go, 2);
		}
	}

	return 0;
}

static int mt352_get_parameters(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *param)
{
	struct mt352_state* state = fe->demodulator_priv;
	u16 tps;
	u16 div;
	u8 trl;
	struct dvb_ofdm_parameters *op = &param->u.ofdm;
	static const u8 tps_fec_to_api[8] =
	{
		FEC_1_2,
		FEC_2_3,
		FEC_3_4,
		FEC_5_6,
		FEC_7_8,
		FEC_AUTO,
		FEC_AUTO,
		FEC_AUTO
	};

	if ( (mt352_read_register(state,0x00) & 0xC0) != 0xC0 )
		return -EINVAL;

	/* Use TPS_RECEIVED-registers, not the TPS_CURRENT-registers because
	 * the mt352 sometimes works with the wrong parameters
	 */
	tps = (mt352_read_register(state, TPS_RECEIVED_1) << 8) | mt352_read_register(state, TPS_RECEIVED_0);
	div = (mt352_read_register(state, CHAN_START_1) << 8) | mt352_read_register(state, CHAN_START_0);
	trl = mt352_read_register(state, TRL_NOMINAL_RATE_1);

	op->code_rate_HP = tps_fec_to_api[(tps >> 7) & 7];
	op->code_rate_LP = tps_fec_to_api[(tps >> 4) & 7];

	switch ( (tps >> 13) & 3)
	{
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

	op->transmission_mode = (tps & 0x01) ? TRANSMISSION_MODE_8K : TRANSMISSION_MODE_2K;

	switch ( (tps >> 2) & 3)
	{
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

	switch ( (tps >> 10) & 7)
	{
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

	param->frequency = ( 500 * (div - IF_FREQUENCYx6) ) / 3 * 1000;

	if (trl == 0x72)
		op->bandwidth = BANDWIDTH_8_MHZ;
	else if (trl == 0x64)
		op->bandwidth = BANDWIDTH_7_MHZ;
	else
		op->bandwidth = BANDWIDTH_6_MHZ;


	if (mt352_read_register(state, STATUS_2) & 0x02)
		param->inversion = INVERSION_OFF;
	else
		param->inversion = INVERSION_ON;

	return 0;
}

static int mt352_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct mt352_state* state = fe->demodulator_priv;
	int s0, s1, s3;

	/* FIXME:
	 *
	 * The MT352 design manual from Zarlink states (page 46-47):
	 *
	 * Notes about the TUNER_GO register:
	 *
	 * If the Read_Tuner_Byte (bit-1) is activated, then the tuner status
	 * byte is copied from the tuner to the STATUS_3 register and
	 * completion of the read operation is indicated by bit-5 of the
	 * INTERRUPT_3 register.
	 */

	if ((s0 = mt352_read_register(state, STATUS_0)) < 0)
		return -EREMOTEIO;
	if ((s1 = mt352_read_register(state, STATUS_1)) < 0)
		return -EREMOTEIO;
	if ((s3 = mt352_read_register(state, STATUS_3)) < 0)
		return -EREMOTEIO;

	*status = 0;
	if (s0 & (1 << 4))
		*status |= FE_HAS_CARRIER;
	if (s0 & (1 << 1))
		*status |= FE_HAS_VITERBI;
	if (s0 & (1 << 5))
		*status |= FE_HAS_LOCK;
	if (s1 & (1 << 1))
		*status |= FE_HAS_SYNC;
	if (s3 & (1 << 6))
		*status |= FE_HAS_SIGNAL;

	if ((*status & (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC)) !=
		      (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC))
		*status &= ~FE_HAS_LOCK;

	return 0;
}

static int mt352_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct mt352_state* state = fe->demodulator_priv;

	*ber = (mt352_read_register (state, RS_ERR_CNT_2) << 16) |
	       (mt352_read_register (state, RS_ERR_CNT_1) << 8) |
	       (mt352_read_register (state, RS_ERR_CNT_0));

	return 0;
}

static int mt352_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct mt352_state* state = fe->demodulator_priv;

	/* align the 12 bit AGC gain with the most significant bits */
	u16 signal = ((mt352_read_register(state, AGC_GAIN_1) & 0x0f) << 12) |
		(mt352_read_register(state, AGC_GAIN_0) << 4);

	/* inverse of gain is signal strength */
	*strength = ~signal;
	return 0;
}

static int mt352_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct mt352_state* state = fe->demodulator_priv;

	u8 _snr = mt352_read_register (state, SNR);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int mt352_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct mt352_state* state = fe->demodulator_priv;

	*ucblocks = (mt352_read_register (state,  RS_UBC_1) << 8) |
		    (mt352_read_register (state,  RS_UBC_0));

	return 0;
}

static int mt352_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 800;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static int mt352_init(struct dvb_frontend* fe)
{
	struct mt352_state* state = fe->demodulator_priv;

	static u8 mt352_reset_attach [] = { RESET, 0xC0 };

	dprintk("%s: hello\n",__FUNCTION__);

	if ((mt352_read_register(state, CLOCK_CTL) & 0x10) == 0 ||
	    (mt352_read_register(state, CONFIG) & 0x20) == 0) {

		/* Do a "hard" reset */
		_mt352_write(fe, mt352_reset_attach, sizeof(mt352_reset_attach));
		return state->config.demod_init(fe);
	}

	return 0;
}

static void mt352_release(struct dvb_frontend* fe)
{
	struct mt352_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops mt352_ops;

struct dvb_frontend* mt352_attach(const struct mt352_config* config,
				  struct i2c_adapter* i2c)
{
	struct mt352_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct mt352_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config,config,sizeof(struct mt352_config));

	/* check if the demod is there */
	if (mt352_read_register(state, CHIP_ID) != ID_MT352) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &mt352_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops mt352_ops = {

	.info = {
		.name			= "Zarlink MT352 DVB-T",
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

	.release = mt352_release,

	.init = mt352_init,
	.sleep = mt352_sleep,
	.write = _mt352_write,

	.set_frontend = mt352_set_parameters,
	.get_frontend = mt352_get_parameters,
	.get_tune_settings = mt352_get_tune_settings,

	.read_status = mt352_read_status,
	.read_ber = mt352_read_ber,
	.read_signal_strength = mt352_read_signal_strength,
	.read_snr = mt352_read_snr,
	.read_ucblocks = mt352_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Zarlink MT352 DVB-T Demodulator driver");
MODULE_AUTHOR("Holger Waechtler, Daniel Mack, Antonio Mancuso");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(mt352_attach);
