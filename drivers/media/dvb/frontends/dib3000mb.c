/*
 * Frontend driver for mobile DVB-T demodulator DiBcom 3000M-B
 * DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DibCom, which has
 *
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Acknowledgements
 *
 *  Amaury Demol (ademol@dibcom.fr) from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dvb-dibusb) are based.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"

#include "dib3000.h"
#include "dib3000mb_priv.h"

/* Version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_DESC "DiBcom 3000M-B DVB-T demodulator"
#define DRIVER_AUTHOR "Patrick Boettcher, patrick.boettcher@desy.de"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=xfer,4=setfe,8=getfe (|-able)).");

#define deb_info(args...) dprintk(0x01,args)
#define deb_i2c(args...)  dprintk(0x02,args)
#define deb_srch(args...) dprintk(0x04,args)
#define deb_info(args...) dprintk(0x01,args)
#define deb_xfer(args...) dprintk(0x02,args)
#define deb_setf(args...) dprintk(0x04,args)
#define deb_getf(args...) dprintk(0x08,args)

static int dib3000_read_reg(struct dib3000_state *state, u16 reg)
{
	u8 wb[] = { ((reg >> 8) | 0x80) & 0xff, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[] = {
		{ .addr = state->config.demod_address, .flags = 0,        .buf = wb, .len = 2 },
		{ .addr = state->config.demod_address, .flags = I2C_M_RD, .buf = rb, .len = 2 },
	};

	if (i2c_transfer(state->i2c, msg, 2) != 2)
		deb_i2c("i2c read error\n");

	deb_i2c("reading i2c bus (reg: %5d 0x%04x, val: %5d 0x%04x)\n",reg,reg,
			(rb[0] << 8) | rb[1],(rb[0] << 8) | rb[1]);

	return (rb[0] << 8) | rb[1];
}

static int dib3000_write_reg(struct dib3000_state *state, u16 reg, u16 val)
{
	u8 b[] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg[] = {
		{ .addr = state->config.demod_address, .flags = 0, .buf = b, .len = 4 }
	};
	deb_i2c("writing i2c bus (reg: %5d 0x%04x, val: %5d 0x%04x)\n",reg,reg,val,val);

	return i2c_transfer(state->i2c,msg, 1) != 1 ? -EREMOTEIO : 0;
}

static int dib3000_search_status(u16 irq,u16 lock)
{
	if (irq & 0x02) {
		if (lock & 0x01) {
			deb_srch("auto search succeeded\n");
			return 1; // auto search succeeded
		} else {
			deb_srch("auto search not successful\n");
			return 0; // auto search failed
		}
	} else if (irq & 0x01)  {
		deb_srch("auto search failed\n");
		return 0; // auto search failed
	}
	return -1; // try again
}

/* for auto search */
static u16 dib3000_seq[2][2][2] =     /* fft,gua,   inv   */
	{ /* fft */
		{ /* gua */
			{ 0, 1 },                   /*  0   0   { 0,1 } */
			{ 3, 9 },                   /*  0   1   { 0,1 } */
		},
		{
			{ 2, 5 },                   /*  1   0   { 0,1 } */
			{ 6, 11 },                  /*  1   1   { 0,1 } */
		}
	};

static int dib3000mb_get_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep);

static int dib3000mb_set_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep, int tuner)
{
	struct dib3000_state* state = fe->demodulator_priv;
	struct dvb_ofdm_parameters *ofdm = &fep->u.ofdm;
	fe_code_rate_t fe_cr = FEC_NONE;
	int search_state, seq;

	if (tuner && fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, fep);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);

		deb_setf("bandwidth: ");
		switch (ofdm->bandwidth) {
			case BANDWIDTH_8_MHZ:
				deb_setf("8 MHz\n");
				wr_foreach(dib3000mb_reg_timing_freq, dib3000mb_timing_freq[2]);
				wr_foreach(dib3000mb_reg_bandwidth, dib3000mb_bandwidth_8mhz);
				break;
			case BANDWIDTH_7_MHZ:
				deb_setf("7 MHz\n");
				wr_foreach(dib3000mb_reg_timing_freq, dib3000mb_timing_freq[1]);
				wr_foreach(dib3000mb_reg_bandwidth, dib3000mb_bandwidth_7mhz);
				break;
			case BANDWIDTH_6_MHZ:
				deb_setf("6 MHz\n");
				wr_foreach(dib3000mb_reg_timing_freq, dib3000mb_timing_freq[0]);
				wr_foreach(dib3000mb_reg_bandwidth, dib3000mb_bandwidth_6mhz);
				break;
			case BANDWIDTH_AUTO:
				return -EOPNOTSUPP;
			default:
				err("unknown bandwidth value.");
				return -EINVAL;
		}
	}
	wr(DIB3000MB_REG_LOCK1_MASK, DIB3000MB_LOCK1_SEARCH_4);

	deb_setf("transmission mode: ");
	switch (ofdm->transmission_mode) {
		case TRANSMISSION_MODE_2K:
			deb_setf("2k\n");
			wr(DIB3000MB_REG_FFT, DIB3000_TRANSMISSION_MODE_2K);
			break;
		case TRANSMISSION_MODE_8K:
			deb_setf("8k\n");
			wr(DIB3000MB_REG_FFT, DIB3000_TRANSMISSION_MODE_8K);
			break;
		case TRANSMISSION_MODE_AUTO:
			deb_setf("auto\n");
			break;
		default:
			return -EINVAL;
	}

	deb_setf("guard: ");
	switch (ofdm->guard_interval) {
		case GUARD_INTERVAL_1_32:
			deb_setf("1_32\n");
			wr(DIB3000MB_REG_GUARD_TIME, DIB3000_GUARD_TIME_1_32);
			break;
		case GUARD_INTERVAL_1_16:
			deb_setf("1_16\n");
			wr(DIB3000MB_REG_GUARD_TIME, DIB3000_GUARD_TIME_1_16);
			break;
		case GUARD_INTERVAL_1_8:
			deb_setf("1_8\n");
			wr(DIB3000MB_REG_GUARD_TIME, DIB3000_GUARD_TIME_1_8);
			break;
		case GUARD_INTERVAL_1_4:
			deb_setf("1_4\n");
			wr(DIB3000MB_REG_GUARD_TIME, DIB3000_GUARD_TIME_1_4);
			break;
		case GUARD_INTERVAL_AUTO:
			deb_setf("auto\n");
			break;
		default:
			return -EINVAL;
	}

	deb_setf("inversion: ");
	switch (fep->inversion) {
		case INVERSION_OFF:
			deb_setf("off\n");
			wr(DIB3000MB_REG_DDS_INV, DIB3000_DDS_INVERSION_OFF);
			break;
		case INVERSION_AUTO:
			deb_setf("auto ");
			break;
		case INVERSION_ON:
			deb_setf("on\n");
			wr(DIB3000MB_REG_DDS_INV, DIB3000_DDS_INVERSION_ON);
			break;
		default:
			return -EINVAL;
	}

	deb_setf("constellation: ");
	switch (ofdm->constellation) {
		case QPSK:
			deb_setf("qpsk\n");
			wr(DIB3000MB_REG_QAM, DIB3000_CONSTELLATION_QPSK);
			break;
		case QAM_16:
			deb_setf("qam16\n");
			wr(DIB3000MB_REG_QAM, DIB3000_CONSTELLATION_16QAM);
			break;
		case QAM_64:
			deb_setf("qam64\n");
			wr(DIB3000MB_REG_QAM, DIB3000_CONSTELLATION_64QAM);
			break;
		case QAM_AUTO:
			break;
		default:
			return -EINVAL;
	}
	deb_setf("hierarchy: ");
	switch (ofdm->hierarchy_information) {
		case HIERARCHY_NONE:
			deb_setf("none ");
			/* fall through */
		case HIERARCHY_1:
			deb_setf("alpha=1\n");
			wr(DIB3000MB_REG_VIT_ALPHA, DIB3000_ALPHA_1);
			break;
		case HIERARCHY_2:
			deb_setf("alpha=2\n");
			wr(DIB3000MB_REG_VIT_ALPHA, DIB3000_ALPHA_2);
			break;
		case HIERARCHY_4:
			deb_setf("alpha=4\n");
			wr(DIB3000MB_REG_VIT_ALPHA, DIB3000_ALPHA_4);
			break;
		case HIERARCHY_AUTO:
			deb_setf("alpha=auto\n");
			break;
		default:
			return -EINVAL;
	}

	deb_setf("hierarchy: ");
	if (ofdm->hierarchy_information == HIERARCHY_NONE) {
		deb_setf("none\n");
		wr(DIB3000MB_REG_VIT_HRCH, DIB3000_HRCH_OFF);
		wr(DIB3000MB_REG_VIT_HP, DIB3000_SELECT_HP);
		fe_cr = ofdm->code_rate_HP;
	} else if (ofdm->hierarchy_information != HIERARCHY_AUTO) {
		deb_setf("on\n");
		wr(DIB3000MB_REG_VIT_HRCH, DIB3000_HRCH_ON);
		wr(DIB3000MB_REG_VIT_HP, DIB3000_SELECT_LP);
		fe_cr = ofdm->code_rate_LP;
	}
	deb_setf("fec: ");
	switch (fe_cr) {
		case FEC_1_2:
			deb_setf("1_2\n");
			wr(DIB3000MB_REG_VIT_CODE_RATE, DIB3000_FEC_1_2);
			break;
		case FEC_2_3:
			deb_setf("2_3\n");
			wr(DIB3000MB_REG_VIT_CODE_RATE, DIB3000_FEC_2_3);
			break;
		case FEC_3_4:
			deb_setf("3_4\n");
			wr(DIB3000MB_REG_VIT_CODE_RATE, DIB3000_FEC_3_4);
			break;
		case FEC_5_6:
			deb_setf("5_6\n");
			wr(DIB3000MB_REG_VIT_CODE_RATE, DIB3000_FEC_5_6);
			break;
		case FEC_7_8:
			deb_setf("7_8\n");
			wr(DIB3000MB_REG_VIT_CODE_RATE, DIB3000_FEC_7_8);
			break;
		case FEC_NONE:
			deb_setf("none ");
			break;
		case FEC_AUTO:
			deb_setf("auto\n");
			break;
		default:
			return -EINVAL;
	}

	seq = dib3000_seq
		[ofdm->transmission_mode == TRANSMISSION_MODE_AUTO]
		[ofdm->guard_interval == GUARD_INTERVAL_AUTO]
		[fep->inversion == INVERSION_AUTO];

	deb_setf("seq? %d\n", seq);

	wr(DIB3000MB_REG_SEQ, seq);

	wr(DIB3000MB_REG_ISI, seq ? DIB3000MB_ISI_INHIBIT : DIB3000MB_ISI_ACTIVATE);

	if (ofdm->transmission_mode == TRANSMISSION_MODE_2K) {
		if (ofdm->guard_interval == GUARD_INTERVAL_1_8) {
			wr(DIB3000MB_REG_SYNC_IMPROVEMENT, DIB3000MB_SYNC_IMPROVE_2K_1_8);
		} else {
			wr(DIB3000MB_REG_SYNC_IMPROVEMENT, DIB3000MB_SYNC_IMPROVE_DEFAULT);
		}

		wr(DIB3000MB_REG_UNK_121, DIB3000MB_UNK_121_2K);
	} else {
		wr(DIB3000MB_REG_UNK_121, DIB3000MB_UNK_121_DEFAULT);
	}

	wr(DIB3000MB_REG_MOBILE_ALGO, DIB3000MB_MOBILE_ALGO_OFF);
	wr(DIB3000MB_REG_MOBILE_MODE_QAM, DIB3000MB_MOBILE_MODE_QAM_OFF);
	wr(DIB3000MB_REG_MOBILE_MODE, DIB3000MB_MOBILE_MODE_OFF);

	wr_foreach(dib3000mb_reg_agc_bandwidth, dib3000mb_agc_bandwidth_high);

	wr(DIB3000MB_REG_ISI, DIB3000MB_ISI_ACTIVATE);

	wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_AGC + DIB3000MB_RESTART_CTRL);
	wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_OFF);

	/* wait for AGC lock */
	msleep(70);

	wr_foreach(dib3000mb_reg_agc_bandwidth, dib3000mb_agc_bandwidth_low);

	/* something has to be auto searched */
	if (ofdm->constellation == QAM_AUTO ||
		ofdm->hierarchy_information == HIERARCHY_AUTO ||
		fe_cr == FEC_AUTO ||
		fep->inversion == INVERSION_AUTO) {
		int as_count=0;

		deb_setf("autosearch enabled.\n");

		wr(DIB3000MB_REG_ISI, DIB3000MB_ISI_INHIBIT);

		wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_AUTO_SEARCH);
		wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_OFF);

		while ((search_state =
				dib3000_search_status(
					rd(DIB3000MB_REG_AS_IRQ_PENDING),
					rd(DIB3000MB_REG_LOCK2_VALUE))) < 0 && as_count++ < 100)
			msleep(1);

		deb_setf("search_state after autosearch %d after %d checks\n",search_state,as_count);

		if (search_state == 1) {
			struct dvb_frontend_parameters feps;
			if (dib3000mb_get_frontend(fe, &feps) == 0) {
				deb_setf("reading tuning data from frontend succeeded.\n");
				return dib3000mb_set_frontend(fe, &feps, 0);
			}
		}

	} else {
		wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_CTRL);
		wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_OFF);
	}

	return 0;
}

static int dib3000mb_fe_init(struct dvb_frontend* fe, int mobile_mode)
{
	struct dib3000_state* state = fe->demodulator_priv;

	deb_info("dib3000mb is getting up.\n");
	wr(DIB3000MB_REG_POWER_CONTROL, DIB3000MB_POWER_UP);

	wr(DIB3000MB_REG_RESTART, DIB3000MB_RESTART_AGC);

	wr(DIB3000MB_REG_RESET_DEVICE, DIB3000MB_RESET_DEVICE);
	wr(DIB3000MB_REG_RESET_DEVICE, DIB3000MB_RESET_DEVICE_RST);

	wr(DIB3000MB_REG_CLOCK, DIB3000MB_CLOCK_DEFAULT);

	wr(DIB3000MB_REG_ELECT_OUT_MODE, DIB3000MB_ELECT_OUT_MODE_ON);

	wr(DIB3000MB_REG_DDS_FREQ_MSB, DIB3000MB_DDS_FREQ_MSB);
	wr(DIB3000MB_REG_DDS_FREQ_LSB, DIB3000MB_DDS_FREQ_LSB);

	wr_foreach(dib3000mb_reg_timing_freq, dib3000mb_timing_freq[2]);

	wr_foreach(dib3000mb_reg_impulse_noise,
			dib3000mb_impulse_noise_values[DIB3000MB_IMPNOISE_OFF]);

	wr_foreach(dib3000mb_reg_agc_gain, dib3000mb_default_agc_gain);

	wr(DIB3000MB_REG_PHASE_NOISE, DIB3000MB_PHASE_NOISE_DEFAULT);

	wr_foreach(dib3000mb_reg_phase_noise, dib3000mb_default_noise_phase);

	wr_foreach(dib3000mb_reg_lock_duration, dib3000mb_default_lock_duration);

	wr_foreach(dib3000mb_reg_agc_bandwidth, dib3000mb_agc_bandwidth_low);

	wr(DIB3000MB_REG_LOCK0_MASK, DIB3000MB_LOCK0_DEFAULT);
	wr(DIB3000MB_REG_LOCK1_MASK, DIB3000MB_LOCK1_SEARCH_4);
	wr(DIB3000MB_REG_LOCK2_MASK, DIB3000MB_LOCK2_DEFAULT);
	wr(DIB3000MB_REG_SEQ, dib3000_seq[1][1][1]);

	wr_foreach(dib3000mb_reg_bandwidth, dib3000mb_bandwidth_8mhz);

	wr(DIB3000MB_REG_UNK_68, DIB3000MB_UNK_68);
	wr(DIB3000MB_REG_UNK_69, DIB3000MB_UNK_69);
	wr(DIB3000MB_REG_UNK_71, DIB3000MB_UNK_71);
	wr(DIB3000MB_REG_UNK_77, DIB3000MB_UNK_77);
	wr(DIB3000MB_REG_UNK_78, DIB3000MB_UNK_78);
	wr(DIB3000MB_REG_ISI, DIB3000MB_ISI_INHIBIT);
	wr(DIB3000MB_REG_UNK_92, DIB3000MB_UNK_92);
	wr(DIB3000MB_REG_UNK_96, DIB3000MB_UNK_96);
	wr(DIB3000MB_REG_UNK_97, DIB3000MB_UNK_97);
	wr(DIB3000MB_REG_UNK_106, DIB3000MB_UNK_106);
	wr(DIB3000MB_REG_UNK_107, DIB3000MB_UNK_107);
	wr(DIB3000MB_REG_UNK_108, DIB3000MB_UNK_108);
	wr(DIB3000MB_REG_UNK_122, DIB3000MB_UNK_122);
	wr(DIB3000MB_REG_MOBILE_MODE_QAM, DIB3000MB_MOBILE_MODE_QAM_OFF);
	wr(DIB3000MB_REG_BERLEN, DIB3000MB_BERLEN_DEFAULT);

	wr_foreach(dib3000mb_reg_filter_coeffs, dib3000mb_filter_coeffs);

	wr(DIB3000MB_REG_MOBILE_ALGO, DIB3000MB_MOBILE_ALGO_ON);
	wr(DIB3000MB_REG_MULTI_DEMOD_MSB, DIB3000MB_MULTI_DEMOD_MSB);
	wr(DIB3000MB_REG_MULTI_DEMOD_LSB, DIB3000MB_MULTI_DEMOD_LSB);

	wr(DIB3000MB_REG_OUTPUT_MODE, DIB3000MB_OUTPUT_MODE_SLAVE);

	wr(DIB3000MB_REG_FIFO_142, DIB3000MB_FIFO_142);
	wr(DIB3000MB_REG_MPEG2_OUT_MODE, DIB3000MB_MPEG2_OUT_MODE_188);
	wr(DIB3000MB_REG_PID_PARSE, DIB3000MB_PID_PARSE_ACTIVATE);
	wr(DIB3000MB_REG_FIFO, DIB3000MB_FIFO_INHIBIT);
	wr(DIB3000MB_REG_FIFO_146, DIB3000MB_FIFO_146);
	wr(DIB3000MB_REG_FIFO_147, DIB3000MB_FIFO_147);

	wr(DIB3000MB_REG_DATA_IN_DIVERSITY, DIB3000MB_DATA_DIVERSITY_IN_OFF);

	return 0;
}

static int dib3000mb_get_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep)
{
	struct dib3000_state* state = fe->demodulator_priv;
	struct dvb_ofdm_parameters *ofdm = &fep->u.ofdm;
	fe_code_rate_t *cr;
	u16 tps_val;
	int inv_test1,inv_test2;
	u32 dds_val, threshold = 0x800000;

	if (!rd(DIB3000MB_REG_TPS_LOCK))
		return 0;

	dds_val = ((rd(DIB3000MB_REG_DDS_VALUE_MSB) & 0xff) << 16) + rd(DIB3000MB_REG_DDS_VALUE_LSB);
	deb_getf("DDS_VAL: %x %x %x",dds_val, rd(DIB3000MB_REG_DDS_VALUE_MSB), rd(DIB3000MB_REG_DDS_VALUE_LSB));
	if (dds_val < threshold)
		inv_test1 = 0;
	else if (dds_val == threshold)
		inv_test1 = 1;
	else
		inv_test1 = 2;

	dds_val = ((rd(DIB3000MB_REG_DDS_FREQ_MSB) & 0xff) << 16) + rd(DIB3000MB_REG_DDS_FREQ_LSB);
	deb_getf("DDS_FREQ: %x %x %x",dds_val, rd(DIB3000MB_REG_DDS_FREQ_MSB), rd(DIB3000MB_REG_DDS_FREQ_LSB));
	if (dds_val < threshold)
		inv_test2 = 0;
	else if (dds_val == threshold)
		inv_test2 = 1;
	else
		inv_test2 = 2;

	fep->inversion =
		((inv_test2 == 2) && (inv_test1==1 || inv_test1==0)) ||
		((inv_test2 == 0) && (inv_test1==1 || inv_test1==2)) ?
		INVERSION_ON : INVERSION_OFF;

	deb_getf("inversion %d %d, %d\n", inv_test2, inv_test1, fep->inversion);

	switch ((tps_val = rd(DIB3000MB_REG_TPS_QAM))) {
		case DIB3000_CONSTELLATION_QPSK:
			deb_getf("QPSK ");
			ofdm->constellation = QPSK;
			break;
		case DIB3000_CONSTELLATION_16QAM:
			deb_getf("QAM16 ");
			ofdm->constellation = QAM_16;
			break;
		case DIB3000_CONSTELLATION_64QAM:
			deb_getf("QAM64 ");
			ofdm->constellation = QAM_64;
			break;
		default:
			err("Unexpected constellation returned by TPS (%d)", tps_val);
			break;
	}
	deb_getf("TPS: %d\n", tps_val);

	if (rd(DIB3000MB_REG_TPS_HRCH)) {
		deb_getf("HRCH ON\n");
		cr = &ofdm->code_rate_LP;
		ofdm->code_rate_HP = FEC_NONE;
		switch ((tps_val = rd(DIB3000MB_REG_TPS_VIT_ALPHA))) {
			case DIB3000_ALPHA_0:
				deb_getf("HIERARCHY_NONE ");
				ofdm->hierarchy_information = HIERARCHY_NONE;
				break;
			case DIB3000_ALPHA_1:
				deb_getf("HIERARCHY_1 ");
				ofdm->hierarchy_information = HIERARCHY_1;
				break;
			case DIB3000_ALPHA_2:
				deb_getf("HIERARCHY_2 ");
				ofdm->hierarchy_information = HIERARCHY_2;
				break;
			case DIB3000_ALPHA_4:
				deb_getf("HIERARCHY_4 ");
				ofdm->hierarchy_information = HIERARCHY_4;
				break;
			default:
				err("Unexpected ALPHA value returned by TPS (%d)", tps_val);
				break;
		}
		deb_getf("TPS: %d\n", tps_val);

		tps_val = rd(DIB3000MB_REG_TPS_CODE_RATE_LP);
	} else {
		deb_getf("HRCH OFF\n");
		cr = &ofdm->code_rate_HP;
		ofdm->code_rate_LP = FEC_NONE;
		ofdm->hierarchy_information = HIERARCHY_NONE;

		tps_val = rd(DIB3000MB_REG_TPS_CODE_RATE_HP);
	}

	switch (tps_val) {
		case DIB3000_FEC_1_2:
			deb_getf("FEC_1_2 ");
			*cr = FEC_1_2;
			break;
		case DIB3000_FEC_2_3:
			deb_getf("FEC_2_3 ");
			*cr = FEC_2_3;
			break;
		case DIB3000_FEC_3_4:
			deb_getf("FEC_3_4 ");
			*cr = FEC_3_4;
			break;
		case DIB3000_FEC_5_6:
			deb_getf("FEC_5_6 ");
			*cr = FEC_4_5;
			break;
		case DIB3000_FEC_7_8:
			deb_getf("FEC_7_8 ");
			*cr = FEC_7_8;
			break;
		default:
			err("Unexpected FEC returned by TPS (%d)", tps_val);
			break;
	}
	deb_getf("TPS: %d\n",tps_val);

	switch ((tps_val = rd(DIB3000MB_REG_TPS_GUARD_TIME))) {
		case DIB3000_GUARD_TIME_1_32:
			deb_getf("GUARD_INTERVAL_1_32 ");
			ofdm->guard_interval = GUARD_INTERVAL_1_32;
			break;
		case DIB3000_GUARD_TIME_1_16:
			deb_getf("GUARD_INTERVAL_1_16 ");
			ofdm->guard_interval = GUARD_INTERVAL_1_16;
			break;
		case DIB3000_GUARD_TIME_1_8:
			deb_getf("GUARD_INTERVAL_1_8 ");
			ofdm->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case DIB3000_GUARD_TIME_1_4:
			deb_getf("GUARD_INTERVAL_1_4 ");
			ofdm->guard_interval = GUARD_INTERVAL_1_4;
			break;
		default:
			err("Unexpected Guard Time returned by TPS (%d)", tps_val);
			break;
	}
	deb_getf("TPS: %d\n", tps_val);

	switch ((tps_val = rd(DIB3000MB_REG_TPS_FFT))) {
		case DIB3000_TRANSMISSION_MODE_2K:
			deb_getf("TRANSMISSION_MODE_2K ");
			ofdm->transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case DIB3000_TRANSMISSION_MODE_8K:
			deb_getf("TRANSMISSION_MODE_8K ");
			ofdm->transmission_mode = TRANSMISSION_MODE_8K;
			break;
		default:
			err("unexpected transmission mode return by TPS (%d)", tps_val);
			break;
	}
	deb_getf("TPS: %d\n", tps_val);

	return 0;
}

static int dib3000mb_read_status(struct dvb_frontend* fe, fe_status_t *stat)
{
	struct dib3000_state* state = fe->demodulator_priv;

	*stat = 0;

	if (rd(DIB3000MB_REG_AGC_LOCK))
		*stat |= FE_HAS_SIGNAL;
	if (rd(DIB3000MB_REG_CARRIER_LOCK))
		*stat |= FE_HAS_CARRIER;
	if (rd(DIB3000MB_REG_VIT_LCK))
		*stat |= FE_HAS_VITERBI;
	if (rd(DIB3000MB_REG_TS_SYNC_LOCK))
		*stat |= (FE_HAS_SYNC | FE_HAS_LOCK);

	deb_getf("actual status is %2x\n",*stat);

	deb_getf("autoval: tps: %d, qam: %d, hrch: %d, alpha: %d, hp: %d, lp: %d, guard: %d, fft: %d cell: %d\n",
			rd(DIB3000MB_REG_TPS_LOCK),
			rd(DIB3000MB_REG_TPS_QAM),
			rd(DIB3000MB_REG_TPS_HRCH),
			rd(DIB3000MB_REG_TPS_VIT_ALPHA),
			rd(DIB3000MB_REG_TPS_CODE_RATE_HP),
			rd(DIB3000MB_REG_TPS_CODE_RATE_LP),
			rd(DIB3000MB_REG_TPS_GUARD_TIME),
			rd(DIB3000MB_REG_TPS_FFT),
			rd(DIB3000MB_REG_TPS_CELL_ID));

	//*stat = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	return 0;
}

static int dib3000mb_read_ber(struct dvb_frontend* fe, u32 *ber)
{
	struct dib3000_state* state = fe->demodulator_priv;

	*ber = ((rd(DIB3000MB_REG_BER_MSB) << 16) | rd(DIB3000MB_REG_BER_LSB));
	return 0;
}

/* see dib3000-watch dvb-apps for exact calcuations of signal_strength and snr */
static int dib3000mb_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	struct dib3000_state* state = fe->demodulator_priv;

	*strength = rd(DIB3000MB_REG_SIGNAL_POWER) * 0xffff / 0x170;
	return 0;
}

static int dib3000mb_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	struct dib3000_state* state = fe->demodulator_priv;
	short sigpow = rd(DIB3000MB_REG_SIGNAL_POWER);
	int icipow = ((rd(DIB3000MB_REG_NOISE_POWER_MSB) & 0xff) << 16) |
		rd(DIB3000MB_REG_NOISE_POWER_LSB);
	*snr = (sigpow << 8) / ((icipow > 0) ? icipow : 1);
	return 0;
}

static int dib3000mb_read_unc_blocks(struct dvb_frontend* fe, u32 *unc)
{
	struct dib3000_state* state = fe->demodulator_priv;

	*unc = rd(DIB3000MB_REG_PACKET_ERROR_RATE);
	return 0;
}

static int dib3000mb_sleep(struct dvb_frontend* fe)
{
	struct dib3000_state* state = fe->demodulator_priv;
	deb_info("dib3000mb is going to bed.\n");
	wr(DIB3000MB_REG_POWER_CONTROL, DIB3000MB_POWER_DOWN);
	return 0;
}

static int dib3000mb_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 800;
	return 0;
}

static int dib3000mb_fe_init_nonmobile(struct dvb_frontend* fe)
{
	return dib3000mb_fe_init(fe, 0);
}

static int dib3000mb_set_frontend_and_tuner(struct dvb_frontend* fe, struct dvb_frontend_parameters *fep)
{
	return dib3000mb_set_frontend(fe, fep, 1);
}

static void dib3000mb_release(struct dvb_frontend* fe)
{
	struct dib3000_state *state = fe->demodulator_priv;
	kfree(state);
}

/* pid filter and transfer stuff */
static int dib3000mb_pid_control(struct dvb_frontend *fe,int index, int pid,int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;
	pid = (onoff ? pid | DIB3000_ACTIVATE_PID_FILTERING : 0);
	wr(index+DIB3000MB_REG_FIRST_PID,pid);
	return 0;
}

static int dib3000mb_fifo_control(struct dvb_frontend *fe, int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;

	deb_xfer("%s fifo\n",onoff ? "enabling" : "disabling");
	if (onoff) {
		wr(DIB3000MB_REG_FIFO, DIB3000MB_FIFO_ACTIVATE);
	} else {
		wr(DIB3000MB_REG_FIFO, DIB3000MB_FIFO_INHIBIT);
	}
	return 0;
}

static int dib3000mb_pid_parse(struct dvb_frontend *fe, int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;
	deb_xfer("%s pid parsing\n",onoff ? "enabling" : "disabling");
	wr(DIB3000MB_REG_PID_PARSE,onoff);
	return 0;
}

static int dib3000mb_tuner_pass_ctrl(struct dvb_frontend *fe, int onoff, u8 pll_addr)
{
	struct dib3000_state *state = fe->demodulator_priv;
	if (onoff) {
		wr(DIB3000MB_REG_TUNER, DIB3000_TUNER_WRITE_ENABLE(pll_addr));
	} else {
		wr(DIB3000MB_REG_TUNER, DIB3000_TUNER_WRITE_DISABLE(pll_addr));
	}
	return 0;
}

static struct dvb_frontend_ops dib3000mb_ops;

struct dvb_frontend* dib3000mb_attach(const struct dib3000_config* config,
				      struct i2c_adapter* i2c, struct dib_fe_xfer_ops *xfer_ops)
{
	struct dib3000_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct dib3000_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config,config,sizeof(struct dib3000_config));

	/* check for the correct demod */
	if (rd(DIB3000_REG_MANUFACTOR_ID) != DIB3000_I2C_ID_DIBCOM)
		goto error;

	if (rd(DIB3000_REG_DEVICE_ID) != DIB3000MB_DEVICE_ID)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &dib3000mb_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* set the xfer operations */
	xfer_ops->pid_parse = dib3000mb_pid_parse;
	xfer_ops->fifo_ctrl = dib3000mb_fifo_control;
	xfer_ops->pid_ctrl = dib3000mb_pid_control;
	xfer_ops->tuner_pass_ctrl = dib3000mb_tuner_pass_ctrl;

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dib3000mb_ops = {

	.info = {
		.name			= "DiBcom 3000M-B DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 44250000,
		.frequency_max		= 867250000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_INVERSION_AUTO |
				FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
				FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
				FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_RECOVER |
				FE_CAN_HIERARCHY_AUTO,
	},

	.release = dib3000mb_release,

	.init = dib3000mb_fe_init_nonmobile,
	.sleep = dib3000mb_sleep,

	.set_frontend = dib3000mb_set_frontend_and_tuner,
	.get_frontend = dib3000mb_get_frontend,
	.get_tune_settings = dib3000mb_fe_get_tune_settings,

	.read_status = dib3000mb_read_status,
	.read_ber = dib3000mb_read_ber,
	.read_signal_strength = dib3000mb_read_signal_strength,
	.read_snr = dib3000mb_read_snr,
	.read_ucblocks = dib3000mb_read_unc_blocks,
};

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(dib3000mb_attach);
