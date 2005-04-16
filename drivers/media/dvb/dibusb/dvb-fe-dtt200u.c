/*
 * dvb-dtt200u-fe.c is a driver which implements the frontend-part of the
 * Yakumo/Typhoon/Hama USB2.0 boxes. It is hard-wired to the dibusb-driver as
 * it uses the usb-transfer functions directly (maybe creating a
 * generic-dvb-usb-lib for all usb-drivers will be reduce some more code.)
 *
 * Copyright (C) 2005 Patrick Boettcher <patrick.boettcher@desy.de>
 *
 * see dvb-dibusb-core.c for copyright details.
 */

/* guessed protocol description (reverse engineered):
 * read
 *  00 - USB type 0x02 for usb2.0, 0x01 for usb1.1
 *  81 - <TS_LOCK> <current frequency divided by 250000>
 *  82 - crash - do not touch
 *  83 - crash - do not touch
 *  84 - remote control
 *  85 - crash - do not touch (OK, stop testing here)
 *  88 - locking 2 bytes (0x80 0x40 == no signal, 0x89 0x20 == nice signal)
 *  89 - noise-to-signal
 *	8a - unkown 1 byte - signal_strength
 *  8c - ber ???
 *  8d - ber
 *  8e - unc
 *
 * write
 *  02 - bandwidth
 *  03 - frequency (divided by 250000)
 *  04 - pid table (index pid(7:0) pid(12:8))
 *  05 - reset the pid table
 *  08 - demod transfer enabled or not (FX2 transfer is enabled by default)
 */

#include "dvb-dibusb.h"
#include "dvb_frontend.h"

struct dtt200u_fe_state {
	struct usb_dibusb *dib;

	struct dvb_frontend_parameters fep;
	struct dvb_frontend frontend;
};

#define moan(which,what) info("unexpected value in '%s' for cmd '%02x' - please report to linux-dvb@linuxtv.org",which,what)

static int dtt200u_fe_read_status(struct dvb_frontend* fe, fe_status_t *stat)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw[1] = { 0x81 };
	u8 br[3] = { 0 };
//	u8 bdeb[5] = { 0 };

	dibusb_readwrite_usb(state->dib,bw,1,br,3);
	switch (br[0]) {
		case 0x01:
			*stat = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
			break;
		case 0x00:
			*stat = 0;
			break;
		default:
			moan("br[0]",0x81);
			break;
	}

//	bw[0] = 0x88;
//	dibusb_readwrite_usb(state->dib,bw,1,bdeb,5);

//	deb_info("%02x: %02x %02x %02x %02x %02x\n",bw[0],bdeb[0],bdeb[1],bdeb[2],bdeb[3],bdeb[4]);

	return 0;
}
static int dtt200u_fe_read_ber(struct dvb_frontend* fe, u32 *ber)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw[1] = { 0x8d };
	*ber = 0;
	dibusb_readwrite_usb(state->dib,bw,1,(u8*) ber, 3);
	return 0;
}

static int dtt200u_fe_read_unc_blocks(struct dvb_frontend* fe, u32 *unc)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw[1] = { 0x8c };
	*unc = 0;
	dibusb_readwrite_usb(state->dib,bw,1,(u8*) unc, 3);
	return 0;
}

static int dtt200u_fe_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw[1] = { 0x8a };
	u8 b;
	dibusb_readwrite_usb(state->dib,bw,1,&b, 1);
	*strength = (b << 8) | b;
	return 0;
}

static int dtt200u_fe_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 bw[1] = { 0x89 };
	u8 br[1] = { 0 };
	dibusb_readwrite_usb(state->dib,bw,1,br,1);
	*snr = ((0xff - br[0]) << 8) | (0xff - br[0]);
	return 0;
}

static int dtt200u_fe_init(struct dvb_frontend* fe)
{
	struct dtt200u_fe_state *state = fe->demodulator_priv;
	u8 b[] = { 0x01 };
	return dibusb_write_usb(state->dib,b,1);
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
	u8 bw,bwbuf[2] = { 0x03, 0 }, freqbuf[3] = { 0x02, 0, 0 };

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
	dibusb_write_usb(state->dib,bwbuf,2);

	freqbuf[1] = freq & 0xff;
	freqbuf[2] = (freq >> 8) & 0xff;
	dibusb_write_usb(state->dib,freqbuf,3);

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

static int dtt200u_pid_control(struct dvb_frontend *fe,int index, int pid,int onoff)
{
	struct dtt200u_fe_state *state = (struct dtt200u_fe_state*) fe->demodulator_priv;
	u8 b_pid[4];
	pid = onoff ? pid : 0;

	b_pid[0] = 0x04;
	b_pid[1] = index;
	b_pid[2] = pid & 0xff;
	b_pid[3] = (pid >> 8) & 0xff;

	dibusb_write_usb(state->dib,b_pid,4);
	return 0;
}

static int dtt200u_fifo_control(struct dvb_frontend *fe, int onoff)
{
	struct dtt200u_fe_state *state = (struct dtt200u_fe_state*) fe->demodulator_priv;
	u8 b_streaming[2] = { 0x08, onoff };
	u8 b_rst_pid[1] = { 0x05 };

	dibusb_write_usb(state->dib,b_streaming,2);

	if (!onoff)
		dibusb_write_usb(state->dib,b_rst_pid,1);
	return 0;
}

static struct dvb_frontend_ops dtt200u_fe_ops;

struct dvb_frontend* dtt200u_fe_attach(struct usb_dibusb *dib, struct dib_fe_xfer_ops *xfer_ops)
{
	struct dtt200u_fe_state* state = NULL;

	/* allocate memory for the internal state */
	state = (struct dtt200u_fe_state*) kmalloc(sizeof(struct dtt200u_fe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(struct dtt200u_fe_state));

	deb_info("attaching frontend dtt200u\n");

	state->dib = dib;

	state->frontend.ops = &dtt200u_fe_ops;
	state->frontend.demodulator_priv = state;

	xfer_ops->fifo_ctrl = dtt200u_fifo_control;
	xfer_ops->pid_ctrl = dtt200u_pid_control;

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
