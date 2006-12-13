/*
 * TTUSB DEC Frontend Driver
 *
 * Copyright (C) 2003-2004 Alex Woods <linux-dvb@giblets.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "dvb_frontend.h"
#include "ttusbdecfe.h"


#define LOF_HI			10600000
#define LOF_LO			9750000

struct ttusbdecfe_state {

	/* configuration settings */
	const struct ttusbdecfe_config* config;

	struct dvb_frontend frontend;

	u8 hi_band;
	u8 voltage;
};


static int ttusbdecfe_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct ttusbdecfe_state* state = fe->demodulator_priv;
	u8 b[] = { 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00 };
	u8 result[4];
	int len, ret;

	*status=0;

	ret=state->config->send_command(fe, 0x73, sizeof(b), b, &len, result);
	if(ret)
		return ret;

	if(len != 4) {
		printk(KERN_ERR "%s: unexpected reply\n", __FUNCTION__);
		return -EIO;
	}

	switch(result[3]) {
		case 1:  /* not tuned yet */
		case 2:  /* no signal/no lock*/
			break;
		case 3:	 /* signal found and locked*/
			*status = FE_HAS_SIGNAL | FE_HAS_VITERBI |
			FE_HAS_SYNC | FE_HAS_CARRIER | FE_HAS_LOCK;
			break;
		case 4:
			*status = FE_TIMEDOUT;
			break;
		default:
			pr_info("%s: returned unknown value: %d\n",
				__FUNCTION__, result[3]);
			return -EIO;
	}

	return 0;
}

static int ttusbdecfe_dvbt_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;
	u8 b[] = { 0x00, 0x00, 0x00, 0x03,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x01,
		   0x00, 0x00, 0x00, 0xff,
		   0x00, 0x00, 0x00, 0xff };

	u32 freq = htonl(p->frequency / 1000);
	memcpy(&b[4], &freq, sizeof (u32));
	state->config->send_command(fe, 0x71, sizeof(b), b, NULL, NULL);

	return 0;
}

static int ttusbdecfe_dvbt_get_tune_settings(struct dvb_frontend* fe,
					struct dvb_frontend_tune_settings* fesettings)
{
		fesettings->min_delay_ms = 1500;
		/* Drift compensation makes no sense for DVB-T */
		fesettings->step_size = 0;
		fesettings->max_drift = 0;
		return 0;
}

static int ttusbdecfe_dvbs_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;

	u8 b[] = { 0x00, 0x00, 0x00, 0x01,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x01,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00 };
	u32 freq;
	u32 sym_rate;
	u32 band;
	u32 lnb_voltage;

	freq = htonl(p->frequency +
	       (state->hi_band ? LOF_HI : LOF_LO));
	memcpy(&b[4], &freq, sizeof(u32));
	sym_rate = htonl(p->u.qam.symbol_rate);
	memcpy(&b[12], &sym_rate, sizeof(u32));
	band = htonl(state->hi_band ? LOF_HI : LOF_LO);
	memcpy(&b[24], &band, sizeof(u32));
	lnb_voltage = htonl(state->voltage);
	memcpy(&b[28], &lnb_voltage, sizeof(u32));

	state->config->send_command(fe, 0x71, sizeof(b), b, NULL, NULL);

	return 0;
}

static int ttusbdecfe_dvbs_diseqc_send_master_cmd(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;
	u8 b[] = { 0x00, 0xff, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00 };

	memcpy(&b[4], cmd->msg, cmd->msg_len);

	state->config->send_command(fe, 0x72,
				    sizeof(b) - (6 - cmd->msg_len), b,
				    NULL, NULL);

	return 0;
}


static int ttusbdecfe_dvbs_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;

	state->hi_band = (SEC_TONE_ON == tone);

	return 0;
}


static int ttusbdecfe_dvbs_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		state->voltage = 13;
		break;
	case SEC_VOLTAGE_18:
		state->voltage = 18;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void ttusbdecfe_release(struct dvb_frontend* fe)
{
	struct ttusbdecfe_state* state = (struct ttusbdecfe_state*) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops ttusbdecfe_dvbt_ops;

struct dvb_frontend* ttusbdecfe_dvbt_attach(const struct ttusbdecfe_config* config)
{
	struct ttusbdecfe_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct ttusbdecfe_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	/* setup the state */
	state->config = config;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &ttusbdecfe_dvbt_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

static struct dvb_frontend_ops ttusbdecfe_dvbs_ops;

struct dvb_frontend* ttusbdecfe_dvbs_attach(const struct ttusbdecfe_config* config)
{
	struct ttusbdecfe_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct ttusbdecfe_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	/* setup the state */
	state->config = config;
	state->voltage = 0;
	state->hi_band = 0;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &ttusbdecfe_dvbs_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;
}

static struct dvb_frontend_ops ttusbdecfe_dvbt_ops = {

	.info = {
		.name			= "TechnoTrend/Hauppauge DEC2000-t Frontend",
		.type			= FE_OFDM,
		.frequency_min		= 51000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
		.caps =	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release = ttusbdecfe_release,

	.set_frontend = ttusbdecfe_dvbt_set_frontend,

	.get_tune_settings = ttusbdecfe_dvbt_get_tune_settings,

	.read_status = ttusbdecfe_read_status,
};

static struct dvb_frontend_ops ttusbdecfe_dvbs_ops = {

	.info = {
		.name			= "TechnoTrend/Hauppauge DEC3000-s Frontend",
		.type			= FE_QPSK,
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 125,
		.symbol_rate_min        = 1000000,  /* guessed */
		.symbol_rate_max        = 45000000, /* guessed */
		.caps =	FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK
	},

	.release = ttusbdecfe_release,

	.set_frontend = ttusbdecfe_dvbs_set_frontend,

	.read_status = ttusbdecfe_read_status,

	.diseqc_send_master_cmd = ttusbdecfe_dvbs_diseqc_send_master_cmd,
	.set_voltage = ttusbdecfe_dvbs_set_voltage,
	.set_tone = ttusbdecfe_dvbs_set_tone,
};

MODULE_DESCRIPTION("TTUSB DEC DVB-T/S Demodulator driver");
MODULE_AUTHOR("Alex Woods/Andrew de Quincey");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(ttusbdecfe_dvbt_attach);
EXPORT_SYMBOL(ttusbdecfe_dvbs_attach);
