/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/dvb_frontend.h>

#include "firedtv.h"

static int fdtv_dvb_init(struct dvb_frontend *fe)
{
	struct firedtv *fdtv = fe->sec_priv;
	int err;

	/* FIXME - allocate free channel at IRM */
	fdtv->isochannel = fdtv->adapter.num;

	err = cmp_establish_pp_connection(fdtv, fdtv->subunit,
					  fdtv->isochannel);
	if (err) {
		dev_err(fdtv->device,
			"could not establish point to point connection\n");
		return err;
	}

	return fdtv_start_iso(fdtv);
}

static int fdtv_sleep(struct dvb_frontend *fe)
{
	struct firedtv *fdtv = fe->sec_priv;

	fdtv_stop_iso(fdtv);
	cmp_break_pp_connection(fdtv, fdtv->subunit, fdtv->isochannel);
	fdtv->isochannel = -1;
	return 0;
}

#define LNBCONTROL_DONTCARE 0xff

static int fdtv_diseqc_send_master_cmd(struct dvb_frontend *fe,
				       struct dvb_diseqc_master_cmd *cmd)
{
	struct firedtv *fdtv = fe->sec_priv;

	return avc_lnb_control(fdtv, LNBCONTROL_DONTCARE, LNBCONTROL_DONTCARE,
			       LNBCONTROL_DONTCARE, 1, cmd);
}

static int fdtv_diseqc_send_burst(struct dvb_frontend *fe,
				  enum fe_sec_mini_cmd minicmd)
{
	return 0;
}

static int fdtv_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct firedtv *fdtv = fe->sec_priv;

	fdtv->tone = tone;
	return 0;
}

static int fdtv_set_voltage(struct dvb_frontend *fe,
			    enum fe_sec_voltage voltage)
{
	struct firedtv *fdtv = fe->sec_priv;

	fdtv->voltage = voltage;
	return 0;
}

static int fdtv_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct firedtv *fdtv = fe->sec_priv;
	struct firedtv_tuner_status stat;

	if (avc_tuner_status(fdtv, &stat))
		return -EINVAL;

	if (stat.no_rf)
		*status = 0;
	else
		*status = FE_HAS_SIGNAL | FE_HAS_VITERBI | FE_HAS_SYNC |
			  FE_HAS_CARRIER | FE_HAS_LOCK;
	return 0;
}

static int fdtv_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct firedtv *fdtv = fe->sec_priv;
	struct firedtv_tuner_status stat;

	if (avc_tuner_status(fdtv, &stat))
		return -EINVAL;

	*ber = stat.ber;
	return 0;
}

static int fdtv_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct firedtv *fdtv = fe->sec_priv;
	struct firedtv_tuner_status stat;

	if (avc_tuner_status(fdtv, &stat))
		return -EINVAL;

	*strength = stat.signal_strength << 8;
	return 0;
}

static int fdtv_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct firedtv *fdtv = fe->sec_priv;
	struct firedtv_tuner_status stat;

	if (avc_tuner_status(fdtv, &stat))
		return -EINVAL;

	/* C/N[dB] = -10 * log10(snr / 65535) */
	*snr = stat.carrier_noise_ratio * 257;
	return 0;
}

static int fdtv_read_uncorrected_blocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	return -EOPNOTSUPP;
}

static int fdtv_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct firedtv *fdtv = fe->sec_priv;

	return avc_tuner_dsd(fdtv, p);
}

void fdtv_frontend_init(struct firedtv *fdtv, const char *name)
{
	struct dvb_frontend_ops *ops = &fdtv->fe.ops;
	struct dvb_frontend_internal_info *fi = &ops->info;

	ops->init			= fdtv_dvb_init;
	ops->sleep			= fdtv_sleep;

	ops->set_frontend		= fdtv_set_frontend;

	ops->read_status		= fdtv_read_status;
	ops->read_ber			= fdtv_read_ber;
	ops->read_signal_strength	= fdtv_read_signal_strength;
	ops->read_snr			= fdtv_read_snr;
	ops->read_ucblocks		= fdtv_read_uncorrected_blocks;

	ops->diseqc_send_master_cmd	= fdtv_diseqc_send_master_cmd;
	ops->diseqc_send_burst		= fdtv_diseqc_send_burst;
	ops->set_tone			= fdtv_set_tone;
	ops->set_voltage		= fdtv_set_voltage;

	switch (fdtv->type) {
	case FIREDTV_DVB_S:
		ops->delsys[0]		= SYS_DVBS;

		fi->frequency_min_hz	=   950 * MHz;
		fi->frequency_max_hz	=  2150 * MHz;
		fi->frequency_stepsize_hz = 125 * kHz;
		fi->symbol_rate_min	= 1000000;
		fi->symbol_rate_max	= 40000000;

		fi->caps		= FE_CAN_INVERSION_AUTO |
					  FE_CAN_FEC_1_2	|
					  FE_CAN_FEC_2_3	|
					  FE_CAN_FEC_3_4	|
					  FE_CAN_FEC_5_6	|
					  FE_CAN_FEC_7_8	|
					  FE_CAN_FEC_AUTO	|
					  FE_CAN_QPSK;
		break;

	case FIREDTV_DVB_S2:
		ops->delsys[0]		= SYS_DVBS;
		ops->delsys[1]		= SYS_DVBS2;

		fi->frequency_min_hz	=   950 * MHz;
		fi->frequency_max_hz	=  2150 * MHz;
		fi->frequency_stepsize_hz = 125 * kHz;
		fi->symbol_rate_min	= 1000000;
		fi->symbol_rate_max	= 40000000;

		fi->caps		= FE_CAN_INVERSION_AUTO |
					  FE_CAN_FEC_1_2        |
					  FE_CAN_FEC_2_3        |
					  FE_CAN_FEC_3_4        |
					  FE_CAN_FEC_5_6        |
					  FE_CAN_FEC_7_8        |
					  FE_CAN_FEC_AUTO       |
					  FE_CAN_QPSK           |
					  FE_CAN_2G_MODULATION;
		break;

	case FIREDTV_DVB_C:
		ops->delsys[0]		= SYS_DVBC_ANNEX_A;

		fi->frequency_min_hz	=      47 * MHz;
		fi->frequency_max_hz	=     866 * MHz;
		fi->frequency_stepsize_hz = 62500;
		fi->symbol_rate_min	= 870000;
		fi->symbol_rate_max	= 6900000;

		fi->caps		= FE_CAN_INVERSION_AUTO |
					  FE_CAN_QAM_16		|
					  FE_CAN_QAM_32		|
					  FE_CAN_QAM_64		|
					  FE_CAN_QAM_128	|
					  FE_CAN_QAM_256	|
					  FE_CAN_QAM_AUTO;
		break;

	case FIREDTV_DVB_T:
		ops->delsys[0]		= SYS_DVBT;

		fi->frequency_min_hz	=  49 * MHz;
		fi->frequency_max_hz	= 861 * MHz;
		fi->frequency_stepsize_hz = 62500;

		fi->caps		= FE_CAN_INVERSION_AUTO		|
					  FE_CAN_FEC_2_3		|
					  FE_CAN_TRANSMISSION_MODE_AUTO |
					  FE_CAN_GUARD_INTERVAL_AUTO	|
					  FE_CAN_HIERARCHY_AUTO;
		break;

	default:
		dev_err(fdtv->device, "no frontend for model type %d\n",
			fdtv->type);
	}
	strcpy(fi->name, name);

	fdtv->fe.dvb = &fdtv->adapter;
	fdtv->fe.sec_priv = fdtv;
}
