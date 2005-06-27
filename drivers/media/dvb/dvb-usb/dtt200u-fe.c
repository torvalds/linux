/* Frontend part of the Linux driver for the Yakumo/Hama/Typhoon DVB-T
 * USB2.0 receiver.
 *
 * Copyright (C) 2005 Patrick Boettcher <patrick.boettcher@desy.de>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dtt200u.h"

struct dtt200u_fe_state {
	struct dvb_usb_device *d;

	struct dvb_frontend_parameters fep;
	struct dvb_frontend frontend;
};

#define moan(which,what) info("unexpected value in '%s' for cmd '%02x' - please report to linux-dvb@linuxtv.org",which,what)

static int dtt200u_fe_read_status(struct dvb_frontend* fe, fe_status_t *stat)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw = GET_TUNE_STAT;
	u8 br[3] = { 0 };
//	u8 bdeb[5] = { 0 };

	dvb_usb_generic_rw(state->d,&bw,1,br,3,0);
	switch (br[0]) {
		case 0x01:
			*stat = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
			break;
		case 0x00:
			*stat = 0;
			break;
		default:
			moan("br[0]",GET_TUNE_STAT);
			break;
	}

//	bw[0] = 0x88;
//	dvb_usb_generic_rw(state->d,bw,1,bdeb,5,0);

//	deb_info("%02x: %02x %02x %02x %02x %02x\n",bw[0],bdeb[0],bdeb[1],bdeb[2],bdeb[3],bdeb[4]);

	return 0;
}
static int dtt200u_fe_read_ber(struct dvb_frontend* fe, u32 *ber)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw = GET_BER;
	*ber = 0;
	dvb_usb_generic_rw(state->d,&bw,1,(u8*) ber,3,0);
	return 0;
}

static int dtt200u_fe_read_unc_blocks(struct dvb_frontend* fe, u32 *unc)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw = GET_UNK;
	*unc = 0;
	dvb_usb_generic_rw(state->d,&bw,1,(u8*) unc,3,0);
	return 0;
}

static int dtt200u_fe_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw = GET_SIG_STRENGTH, b;
	dvb_usb_generic_rw(state->d,&bw,1,&b,1,0);
	*strength = (b << 8) | b;
	return 0;
}

static int dtt200u_fe_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw = GET_SNR,br;
	dvb_usb_generic_rw(state->d,&bw,1,&br,1,0);
	*snr = ~((br << 8) | br);
	return 0;
}

static int dtt200u_fe_init(struct dvb_frontend* fe)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 b = RESET_DEMOD;
	return dvb_usb_generic_write(state->d,&b,1);
}

static int dtt200u_fe_sleep(struct dvb_frontend* fe)
{
	return dtt200u_fe_init(fe);
}

static int dtt200u_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1500;
	tune->step_size = 166667;
	tune->max_drift = 166667 * 2;
	return 0;
}

static int dtt200u_fe_set_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u16 freq = fep->frequency / 250000;
	u8 bw,bwbuf[2] = { SET_BANDWIDTH, 0 }, freqbuf[3] = { SET_FREQUENCY, 0, 0 };

	switch (fep->u.ofdm.bandwidth) {
		case BANDWIDTH_8_MHZ: bw = 8; break;
		case BANDWIDTH_7_MHZ: bw = 7; break;
		case BANDWIDTH_6_MHZ: bw = 6; break;
		case BANDWIDTH_AUTO: return -EOPNOTSUPP;
		default:
			return -EINVAL;
	}
	deb_info("set_frontend\n");

	bwbuf[1] = bw;
	dvb_usb_generic_write(state->d,bwbuf,2);

	freqbuf[1] = freq & 0xff;
	freqbuf[2] = (freq >> 8) & 0xff;
	dvb_usb_generic_write(state->d,freqbuf,3);

	memcpy(&state->fep,fep,sizeof(struct dvb_frontend_parameters));

	return 0;
}

static int dtt200u_fe_get_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	memcpy(fep,&state->fep,sizeof(struct dvb_frontend_parameters));
	return 0;
}

static void dtt200u_fe_release(struct dvb_frontend* fe)
{
	struct dtt200u_fe_state *state = (struct dtt200u_fe_state*) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops dtt200u_fe_ops;

struct dvb_frontend* dtt200u_fe_attach(struct dvb_usb_device *d)
{
	struct dtt200u_fe_state* state = NULL;

	/* allocate memory for the internal state */
	state = (struct dtt200u_fe_state*) kmalloc(sizeof(struct dtt200u_fe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(struct dtt200u_fe_state));

	deb_info("attaching frontend dtt200u\n");

	state->d = d;

	state->frontend.ops = &dtt200u_fe_ops;
	state->frontend.demodulator_priv = state;

	goto success;
error:
	return NULL;
success:
	return &state->frontend;
}

static struct dvb_frontend_ops dtt200u_fe_ops = {
	.info = {
		.name			= "DTT200U (Yakumo/Typhoon/Hama) DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 44250000,
		.frequency_max		= 867250000,
		.frequency_stepsize	= 250000,
		.caps = FE_CAN_INVERSION_AUTO |
				FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
				FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
				FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_RECOVER |
				FE_CAN_HIERARCHY_AUTO,
	},

	.release = dtt200u_fe_release,

	.init = dtt200u_fe_init,
	.sleep = dtt200u_fe_sleep,

	.set_frontend = dtt200u_fe_set_frontend,
	.get_frontend = dtt200u_fe_get_frontend,
	.get_tune_settings = dtt200u_fe_get_tune_settings,

	.read_status = dtt200u_fe_read_status,
	.read_ber = dtt200u_fe_read_ber,
	.read_signal_strength = dtt200u_fe_read_signal_strength,
	.read_snr = dtt200u_fe_read_snr,
	.read_ucblocks = dtt200u_fe_read_unc_blocks,
};
