/*
 * Frontend driver for the GENPIX 8pks/qpsk/DCII USB2.0 DVB-S module
 *
 * Copyright (C) 2006,2007 Alan Nisota (alannisota@gmail.com)
 * Copyright (C) 2006,2007 Genpix Electronics (genpix@genpix-electronics.com)
 *
 * Thanks to GENPIX for the sample code used to implement this module.
 *
 * This module is based off the vp7045 and vp702x modules
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "gp8psk-fe.h"
#include <media/dvb_frontend.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

struct gp8psk_fe_state {
	struct dvb_frontend fe;
	void *priv;
	const struct gp8psk_fe_ops *ops;
	bool is_rev1;
	u8 lock;
	u16 snr;
	unsigned long next_status_check;
	unsigned long status_check_interval;
};

static int gp8psk_tuned_to_DCII(struct dvb_frontend *fe)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	u8 status;

	st->ops->in(st->priv, GET_8PSK_CONFIG, 0, 0, &status, 1);
	return status & bmDCtuned;
}

static int gp8psk_set_tuner_mode(struct dvb_frontend *fe, int mode)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	return st->ops->out(st->priv, SET_8PSK_CONFIG, mode, 0, NULL, 0);
}

static int gp8psk_fe_update_status(struct gp8psk_fe_state *st)
{
	u8 buf[6];
	if (time_after(jiffies,st->next_status_check)) {
		st->ops->in(st->priv, GET_SIGNAL_LOCK, 0, 0, &st->lock, 1);
		st->ops->in(st->priv, GET_SIGNAL_STRENGTH, 0, 0, buf, 6);
		st->snr = (buf[1]) << 8 | buf[0];
		st->next_status_check = jiffies + (st->status_check_interval*HZ)/1000;
	}
	return 0;
}

static int gp8psk_fe_read_status(struct dvb_frontend *fe,
				 enum fe_status *status)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	gp8psk_fe_update_status(st);

	if (st->lock)
		*status = FE_HAS_LOCK | FE_HAS_SYNC | FE_HAS_VITERBI | FE_HAS_SIGNAL | FE_HAS_CARRIER;
	else
		*status = 0;

	if (*status & FE_HAS_LOCK)
		st->status_check_interval = 1000;
	else
		st->status_check_interval = 100;
	return 0;
}

/* not supported by this Frontend */
static int gp8psk_fe_read_ber(struct dvb_frontend* fe, u32 *ber)
{
	(void) fe;
	*ber = 0;
	return 0;
}

/* not supported by this Frontend */
static int gp8psk_fe_read_unc_blocks(struct dvb_frontend* fe, u32 *unc)
{
	(void) fe;
	*unc = 0;
	return 0;
}

static int gp8psk_fe_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	gp8psk_fe_update_status(st);
	/* snr is reported in dBu*256 */
	*snr = st->snr;
	return 0;
}

static int gp8psk_fe_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	gp8psk_fe_update_status(st);
	/* snr is reported in dBu*256 */
	/* snr / 38.4 ~= 100% strength */
	/* snr * 17 returns 100% strength as 65535 */
	if (st->snr > 0xf00)
		*strength = 0xffff;
	else
		*strength = (st->snr << 4) + st->snr; /* snr*17 */
	return 0;
}

static int gp8psk_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 800;
	return 0;
}

static int gp8psk_fe_set_frontend(struct dvb_frontend *fe)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 cmd[10];
	u32 freq = c->frequency * 1000;

	dprintk("%s()\n", __func__);

	cmd[4] = freq         & 0xff;
	cmd[5] = (freq >> 8)  & 0xff;
	cmd[6] = (freq >> 16) & 0xff;
	cmd[7] = (freq >> 24) & 0xff;

	/* backwards compatibility: DVB-S + 8-PSK were used for Turbo-FEC */
	if (c->delivery_system == SYS_DVBS && c->modulation == PSK_8)
		c->delivery_system = SYS_TURBO;

	switch (c->delivery_system) {
	case SYS_DVBS:
		if (c->modulation != QPSK) {
			dprintk("%s: unsupported modulation selected (%d)\n",
				__func__, c->modulation);
			return -EOPNOTSUPP;
		}
		c->fec_inner = FEC_AUTO;
		break;
	case SYS_DVBS2: /* kept for backwards compatibility */
		dprintk("%s: DVB-S2 delivery system selected\n", __func__);
		break;
	case SYS_TURBO:
		dprintk("%s: Turbo-FEC delivery system selected\n", __func__);
		break;

	default:
		dprintk("%s: unsupported delivery system selected (%d)\n",
			__func__, c->delivery_system);
		return -EOPNOTSUPP;
	}

	cmd[0] =  c->symbol_rate        & 0xff;
	cmd[1] = (c->symbol_rate >>  8) & 0xff;
	cmd[2] = (c->symbol_rate >> 16) & 0xff;
	cmd[3] = (c->symbol_rate >> 24) & 0xff;
	switch (c->modulation) {
	case QPSK:
		if (st->is_rev1)
			if (gp8psk_tuned_to_DCII(fe))
				st->ops->reload(st->priv);
		switch (c->fec_inner) {
		case FEC_1_2:
			cmd[9] = 0; break;
		case FEC_2_3:
			cmd[9] = 1; break;
		case FEC_3_4:
			cmd[9] = 2; break;
		case FEC_5_6:
			cmd[9] = 3; break;
		case FEC_7_8:
			cmd[9] = 4; break;
		case FEC_AUTO:
			cmd[9] = 5; break;
		default:
			cmd[9] = 5; break;
		}
		if (c->delivery_system == SYS_TURBO)
			cmd[8] = ADV_MOD_TURBO_QPSK;
		else
			cmd[8] = ADV_MOD_DVB_QPSK;
		break;
	case PSK_8: /* PSK_8 is for compatibility with DN */
		cmd[8] = ADV_MOD_TURBO_8PSK;
		switch (c->fec_inner) {
		case FEC_2_3:
			cmd[9] = 0; break;
		case FEC_3_4:
			cmd[9] = 1; break;
		case FEC_3_5:
			cmd[9] = 2; break;
		case FEC_5_6:
			cmd[9] = 3; break;
		case FEC_8_9:
			cmd[9] = 4; break;
		default:
			cmd[9] = 0; break;
		}
		break;
	case QAM_16: /* QAM_16 is for compatibility with DN */
		cmd[8] = ADV_MOD_TURBO_16QAM;
		cmd[9] = 0;
		break;
	default: /* Unknown modulation */
		dprintk("%s: unsupported modulation selected (%d)\n",
			__func__, c->modulation);
		return -EOPNOTSUPP;
	}

	if (st->is_rev1)
		gp8psk_set_tuner_mode(fe, 0);
	st->ops->out(st->priv, TUNE_8PSK, 0, 0, cmd, 10);

	st->lock = 0;
	st->next_status_check = jiffies;
	st->status_check_interval = 200;

	return 0;
}

static int gp8psk_fe_send_diseqc_msg (struct dvb_frontend* fe,
				    struct dvb_diseqc_master_cmd *m)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (st->ops->out(st->priv, SEND_DISEQC_COMMAND, m->msg[0], 0,
			m->msg, m->msg_len)) {
		return -EINVAL;
	}
	return 0;
}

static int gp8psk_fe_send_diseqc_burst(struct dvb_frontend *fe,
				       enum fe_sec_mini_cmd burst)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	u8 cmd;

	dprintk("%s\n", __func__);

	/* These commands are certainly wrong */
	cmd = (burst == SEC_MINI_A) ? 0x00 : 0x01;

	if (st->ops->out(st->priv, SEND_DISEQC_COMMAND, cmd, 0,
			&cmd, 0)) {
		return -EINVAL;
	}
	return 0;
}

static int gp8psk_fe_set_tone(struct dvb_frontend *fe,
			      enum fe_sec_tone_mode tone)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	if (st->ops->out(st->priv, SET_22KHZ_TONE,
			 (tone == SEC_TONE_ON), 0, NULL, 0)) {
		return -EINVAL;
	}
	return 0;
}

static int gp8psk_fe_set_voltage(struct dvb_frontend *fe,
				 enum fe_sec_voltage voltage)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	if (st->ops->out(st->priv, SET_LNB_VOLTAGE,
			 voltage == SEC_VOLTAGE_18, 0, NULL, 0)) {
		return -EINVAL;
	}
	return 0;
}

static int gp8psk_fe_enable_high_lnb_voltage(struct dvb_frontend* fe, long onoff)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	return st->ops->out(st->priv, USE_EXTRA_VOLT, onoff, 0, NULL, 0);
}

static int gp8psk_fe_send_legacy_dish_cmd (struct dvb_frontend* fe, unsigned long sw_cmd)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;
	u8 cmd = sw_cmd & 0x7f;

	if (st->ops->out(st->priv, SET_DN_SWITCH, cmd, 0, NULL, 0))
		return -EINVAL;

	if (st->ops->out(st->priv, SET_LNB_VOLTAGE, !!(sw_cmd & 0x80),
			0, NULL, 0))
		return -EINVAL;

	return 0;
}

static void gp8psk_fe_release(struct dvb_frontend* fe)
{
	struct gp8psk_fe_state *st = fe->demodulator_priv;

	kfree(st);
}

static const struct dvb_frontend_ops gp8psk_fe_ops;

struct dvb_frontend *gp8psk_fe_attach(const struct gp8psk_fe_ops *ops,
				      void *priv, bool is_rev1)
{
	struct gp8psk_fe_state *st;

	if (!ops || !ops->in || !ops->out || !ops->reload) {
		pr_err("Error! gp8psk-fe ops not defined.\n");
		return NULL;
	}

	st = kzalloc(sizeof(struct gp8psk_fe_state), GFP_KERNEL);
	if (!st)
		return NULL;

	memcpy(&st->fe.ops, &gp8psk_fe_ops, sizeof(struct dvb_frontend_ops));
	st->fe.demodulator_priv = st;
	st->ops = ops;
	st->priv = priv;
	st->is_rev1 = is_rev1;

	pr_info("Frontend %sattached\n", is_rev1 ? "revision 1 " : "");

	return &st->fe;
}
EXPORT_SYMBOL_GPL(gp8psk_fe_attach);

static const struct dvb_frontend_ops gp8psk_fe_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name			= "Genpix DVB-S",
		.frequency_min		= 800000,
		.frequency_max		= 2250000,
		.frequency_stepsize	= 100,
		.symbol_rate_min        = 1000000,
		.symbol_rate_max        = 45000000,
		.symbol_rate_tolerance  = 500,  /* ppm */
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			/*
			 * FE_CAN_QAM_16 is for compatibility
			 * (Myth incorrectly detects Turbo-QPSK as plain QAM-16)
			 */
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_TURBO_FEC
	},

	.release = gp8psk_fe_release,

	.init = NULL,
	.sleep = NULL,

	.set_frontend = gp8psk_fe_set_frontend,

	.get_tune_settings = gp8psk_fe_get_tune_settings,

	.read_status = gp8psk_fe_read_status,
	.read_ber = gp8psk_fe_read_ber,
	.read_signal_strength = gp8psk_fe_read_signal_strength,
	.read_snr = gp8psk_fe_read_snr,
	.read_ucblocks = gp8psk_fe_read_unc_blocks,

	.diseqc_send_master_cmd = gp8psk_fe_send_diseqc_msg,
	.diseqc_send_burst = gp8psk_fe_send_diseqc_burst,
	.set_tone = gp8psk_fe_set_tone,
	.set_voltage = gp8psk_fe_set_voltage,
	.dishnetwork_send_legacy_command = gp8psk_fe_send_legacy_dish_cmd,
	.enable_high_lnb_voltage = gp8psk_fe_enable_high_lnb_voltage
};

MODULE_AUTHOR("Alan Nisota <alannisota@gamil.com>");
MODULE_DESCRIPTION("Frontend Driver for Genpix DVB-S");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
