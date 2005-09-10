/*
 * Frontend driver for mobile DVB-T demodulator DiBcom 3000P/M-C
 * DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DiBCom, which has
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
 * see Documentation/dvb/README.dibusb for more information
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "dib3000-common.h"
#include "dib3000mc_priv.h"
#include "dib3000.h"

/* Version information */
#define DRIVER_VERSION "0.1"
#define DRIVER_DESC "DiBcom 3000M-C DVB-T demodulator"
#define DRIVER_AUTHOR "Patrick Boettcher, patrick.boettcher@desy.de"

#ifdef CONFIG_DVB_DIBCOM_DEBUG
static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,2=xfer,4=setfe,8=getfe,16=stat (|-able)).");
#endif
#define deb_info(args...) dprintk(0x01,args)
#define deb_xfer(args...) dprintk(0x02,args)
#define deb_setf(args...) dprintk(0x04,args)
#define deb_getf(args...) dprintk(0x08,args)
#define deb_stat(args...) dprintk(0x10,args)

static int dib3000mc_set_impulse_noise(struct dib3000_state * state, int mode,
	fe_transmit_mode_t transmission_mode, fe_bandwidth_t bandwidth)
{
	switch (transmission_mode) {
		case TRANSMISSION_MODE_2K:
			wr_foreach(dib3000mc_reg_fft,dib3000mc_fft_modes[0]);
			break;
		case TRANSMISSION_MODE_8K:
			wr_foreach(dib3000mc_reg_fft,dib3000mc_fft_modes[1]);
			break;
		default:
			break;
	}

	switch (bandwidth) {
/*		case BANDWIDTH_5_MHZ:
			wr_foreach(dib3000mc_reg_impulse_noise,dib3000mc_impluse_noise[0]);
			break; */
		case BANDWIDTH_6_MHZ:
			wr_foreach(dib3000mc_reg_impulse_noise,dib3000mc_impluse_noise[1]);
			break;
		case BANDWIDTH_7_MHZ:
			wr_foreach(dib3000mc_reg_impulse_noise,dib3000mc_impluse_noise[2]);
			break;
		case BANDWIDTH_8_MHZ:
			wr_foreach(dib3000mc_reg_impulse_noise,dib3000mc_impluse_noise[3]);
			break;
		default:
			break;
	}

	switch (mode) {
		case 0: /* no impulse */ /* fall through */
			wr_foreach(dib3000mc_reg_imp_noise_ctl,dib3000mc_imp_noise_ctl[0]);
			break;
		case 1: /* new algo */
			wr_foreach(dib3000mc_reg_imp_noise_ctl,dib3000mc_imp_noise_ctl[1]);
			set_or(DIB3000MC_REG_IMP_NOISE_55,DIB3000MC_IMP_NEW_ALGO(0)); /* gives 1<<10 */
			break;
		default: /* old algo */
			wr_foreach(dib3000mc_reg_imp_noise_ctl,dib3000mc_imp_noise_ctl[3]);
			break;
	}
	return 0;
}

static int dib3000mc_set_timing(struct dib3000_state *state, int upd_offset,
		fe_transmit_mode_t fft, fe_bandwidth_t bw)
{
	u16 timf_msb,timf_lsb;
	s32 tim_offset,tim_sgn;
	u64 comp1,comp2,comp=0;

	switch (bw) {
		case BANDWIDTH_8_MHZ: comp = DIB3000MC_CLOCK_REF*8; break;
		case BANDWIDTH_7_MHZ: comp = DIB3000MC_CLOCK_REF*7; break;
		case BANDWIDTH_6_MHZ: comp = DIB3000MC_CLOCK_REF*6; break;
		default: err("unknown bandwidth (%d)",bw); break;
	}
	timf_msb = (comp >> 16) & 0xff;
	timf_lsb = (comp & 0xffff);

	// Update the timing offset ;
	if (upd_offset > 0) {
		if (!state->timing_offset_comp_done) {
			msleep(200);
			state->timing_offset_comp_done = 1;
		}
		tim_offset = rd(DIB3000MC_REG_TIMING_OFFS_MSB);
		if ((tim_offset & 0x2000) == 0x2000)
			tim_offset |= 0xC000;
		if (fft == TRANSMISSION_MODE_2K)
			tim_offset <<= 2;
		state->timing_offset += tim_offset;
	}

	tim_offset = state->timing_offset;
	if (tim_offset < 0) {
		tim_sgn = 1;
		tim_offset = -tim_offset;
	} else
		tim_sgn = 0;

	comp1 =  (u32)tim_offset * (u32)timf_lsb ;
	comp2 =  (u32)tim_offset * (u32)timf_msb ;
	comp  = ((comp1 >> 16) + comp2) >> 7;

	if (tim_sgn == 0)
		comp = (u32)(timf_msb << 16) + (u32) timf_lsb + comp;
	else
		comp = (u32)(timf_msb << 16) + (u32) timf_lsb - comp ;

	timf_msb = (comp >> 16) & 0xff;
	timf_lsb = comp & 0xffff;

	wr(DIB3000MC_REG_TIMING_FREQ_MSB,timf_msb);
	wr(DIB3000MC_REG_TIMING_FREQ_LSB,timf_lsb);
	return 0;
}

static int dib3000mc_init_auto_scan(struct dib3000_state *state, fe_bandwidth_t bw, int boost)
{
	if (boost) {
		wr(DIB3000MC_REG_SCAN_BOOST,DIB3000MC_SCAN_BOOST_ON);
	} else {
		wr(DIB3000MC_REG_SCAN_BOOST,DIB3000MC_SCAN_BOOST_OFF);
	}
	switch (bw) {
		case BANDWIDTH_8_MHZ:
			wr_foreach(dib3000mc_reg_bandwidth,dib3000mc_bandwidth_8mhz);
			break;
		case BANDWIDTH_7_MHZ:
			wr_foreach(dib3000mc_reg_bandwidth,dib3000mc_bandwidth_7mhz);
			break;
		case BANDWIDTH_6_MHZ:
			wr_foreach(dib3000mc_reg_bandwidth,dib3000mc_bandwidth_6mhz);
			break;
/*		case BANDWIDTH_5_MHZ:
			wr_foreach(dib3000mc_reg_bandwidth,dib3000mc_bandwidth_5mhz);
			break;*/
		case BANDWIDTH_AUTO:
			return -EOPNOTSUPP;
		default:
			err("unknown bandwidth value (%d).",bw);
			return -EINVAL;
	}
	if (boost) {
		u32 timeout = (rd(DIB3000MC_REG_BW_TIMOUT_MSB) << 16) +
			rd(DIB3000MC_REG_BW_TIMOUT_LSB);
		timeout *= 85; timeout >>= 7;
		wr(DIB3000MC_REG_BW_TIMOUT_MSB,(timeout >> 16) & 0xffff);
		wr(DIB3000MC_REG_BW_TIMOUT_LSB,timeout & 0xffff);
	}
	return 0;
}

static int dib3000mc_set_adp_cfg(struct dib3000_state *state, fe_modulation_t con)
{
	switch (con) {
		case QAM_64:
			wr_foreach(dib3000mc_reg_adp_cfg,dib3000mc_adp_cfg[2]);
			break;
		case QAM_16:
			wr_foreach(dib3000mc_reg_adp_cfg,dib3000mc_adp_cfg[1]);
			break;
		case QPSK:
			wr_foreach(dib3000mc_reg_adp_cfg,dib3000mc_adp_cfg[0]);
			break;
		case QAM_AUTO:
			break;
		default:
			warn("unkown constellation.");
			break;
	}
	return 0;
}

static int dib3000mc_set_general_cfg(struct dib3000_state *state, struct dvb_frontend_parameters *fep, int *auto_val)
{
	struct dvb_ofdm_parameters *ofdm = &fep->u.ofdm;
	fe_code_rate_t fe_cr = FEC_NONE;
	u8 fft=0, guard=0, qam=0, alpha=0, sel_hp=0, cr=0, hrch=0;
	int seq;

	switch (ofdm->transmission_mode) {
		case TRANSMISSION_MODE_2K: fft = DIB3000_TRANSMISSION_MODE_2K; break;
		case TRANSMISSION_MODE_8K: fft = DIB3000_TRANSMISSION_MODE_8K; break;
		case TRANSMISSION_MODE_AUTO: break;
		default: return -EINVAL;
	}
	switch (ofdm->guard_interval) {
		case GUARD_INTERVAL_1_32: guard = DIB3000_GUARD_TIME_1_32; break;
		case GUARD_INTERVAL_1_16: guard = DIB3000_GUARD_TIME_1_16; break;
		case GUARD_INTERVAL_1_8:  guard = DIB3000_GUARD_TIME_1_8; break;
		case GUARD_INTERVAL_1_4:  guard = DIB3000_GUARD_TIME_1_4; break;
		case GUARD_INTERVAL_AUTO: break;
		default: return -EINVAL;
	}
	switch (ofdm->constellation) {
		case QPSK:   qam = DIB3000_CONSTELLATION_QPSK; break;
		case QAM_16: qam = DIB3000_CONSTELLATION_16QAM; break;
		case QAM_64: qam = DIB3000_CONSTELLATION_64QAM; break;
		case QAM_AUTO: break;
		default: return -EINVAL;
	}
	switch (ofdm->hierarchy_information) {
		case HIERARCHY_NONE: /* fall through */
		case HIERARCHY_1: alpha = DIB3000_ALPHA_1; break;
		case HIERARCHY_2: alpha = DIB3000_ALPHA_2; break;
		case HIERARCHY_4: alpha = DIB3000_ALPHA_4; break;
		case HIERARCHY_AUTO: break;
		default: return -EINVAL;
	}
	if (ofdm->hierarchy_information == HIERARCHY_NONE) {
		hrch   = DIB3000_HRCH_OFF;
		sel_hp = DIB3000_SELECT_HP;
		fe_cr  = ofdm->code_rate_HP;
	} else if (ofdm->hierarchy_information != HIERARCHY_AUTO) {
		hrch   = DIB3000_HRCH_ON;
		sel_hp = DIB3000_SELECT_LP;
		fe_cr  = ofdm->code_rate_LP;
	}
	switch (fe_cr) {
		case FEC_1_2: cr = DIB3000_FEC_1_2; break;
		case FEC_2_3: cr = DIB3000_FEC_2_3; break;
		case FEC_3_4: cr = DIB3000_FEC_3_4; break;
		case FEC_5_6: cr = DIB3000_FEC_5_6; break;
		case FEC_7_8: cr = DIB3000_FEC_7_8; break;
		case FEC_NONE: break;
		case FEC_AUTO: break;
		default: return -EINVAL;
	}

	wr(DIB3000MC_REG_DEMOD_PARM,DIB3000MC_DEMOD_PARM(alpha,qam,guard,fft));
	wr(DIB3000MC_REG_HRCH_PARM,DIB3000MC_HRCH_PARM(sel_hp,cr,hrch));

	switch (fep->inversion) {
		case INVERSION_OFF:
			wr(DIB3000MC_REG_SET_DDS_FREQ_MSB,DIB3000MC_DDS_FREQ_MSB_INV_OFF);
			break;
		case INVERSION_AUTO: /* fall through */
		case INVERSION_ON:
			wr(DIB3000MC_REG_SET_DDS_FREQ_MSB,DIB3000MC_DDS_FREQ_MSB_INV_ON);
			break;
		default:
			return -EINVAL;
	}

	seq = dib3000_seq
		[ofdm->transmission_mode == TRANSMISSION_MODE_AUTO]
		[ofdm->guard_interval == GUARD_INTERVAL_AUTO]
		[fep->inversion == INVERSION_AUTO];

	deb_setf("seq? %d\n", seq);
	wr(DIB3000MC_REG_SEQ_TPS,DIB3000MC_SEQ_TPS(seq,1));
	*auto_val = ofdm->constellation == QAM_AUTO ||
			ofdm->hierarchy_information == HIERARCHY_AUTO ||
			ofdm->guard_interval == GUARD_INTERVAL_AUTO ||
			ofdm->transmission_mode == TRANSMISSION_MODE_AUTO ||
			fe_cr == FEC_AUTO ||
			fep->inversion == INVERSION_AUTO;
	return 0;
}

static int dib3000mc_get_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep)
{
	struct dib3000_state* state = fe->demodulator_priv;
	struct dvb_ofdm_parameters *ofdm = &fep->u.ofdm;
	fe_code_rate_t *cr;
	u16 tps_val,cr_val;
	int inv_test1,inv_test2;
	u32 dds_val, threshold = 0x1000000;

	if (!(rd(DIB3000MC_REG_LOCK_507) & DIB3000MC_LOCK_507))
		return 0;

	dds_val = (rd(DIB3000MC_REG_DDS_FREQ_MSB) << 16) + rd(DIB3000MC_REG_DDS_FREQ_LSB);
	deb_getf("DDS_FREQ: %6x\n",dds_val);
	if (dds_val < threshold)
		inv_test1 = 0;
	else if (dds_val == threshold)
		inv_test1 = 1;
	else
		inv_test1 = 2;

	dds_val = (rd(DIB3000MC_REG_SET_DDS_FREQ_MSB) << 16) + rd(DIB3000MC_REG_SET_DDS_FREQ_LSB);
	deb_getf("DDS_SET_FREQ: %6x\n",dds_val);
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

	fep->frequency = state->last_tuned_freq;
	fep->u.ofdm.bandwidth= state->last_tuned_bw;

	tps_val = rd(DIB3000MC_REG_TUNING_PARM);

	switch (DIB3000MC_TP_QAM(tps_val)) {
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

	if (DIB3000MC_TP_HRCH(tps_val)) {
		deb_getf("HRCH ON ");
		cr = &ofdm->code_rate_LP;
		ofdm->code_rate_HP = FEC_NONE;
		switch (DIB3000MC_TP_ALPHA(tps_val)) {
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
		cr_val = DIB3000MC_TP_FEC_CR_LP(tps_val);
	} else {
		deb_getf("HRCH OFF ");
		cr = &ofdm->code_rate_HP;
		ofdm->code_rate_LP = FEC_NONE;
		ofdm->hierarchy_information = HIERARCHY_NONE;
		cr_val = DIB3000MC_TP_FEC_CR_HP(tps_val);
	}

	switch (cr_val) {
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

	switch (DIB3000MC_TP_GUARD(tps_val)) {
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

	switch (DIB3000MC_TP_FFT(tps_val)) {
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
	deb_getf("\n");

	return 0;
}

static int dib3000mc_set_frontend(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters *fep, int tuner)
{
	struct dib3000_state* state = fe->demodulator_priv;
	struct dvb_ofdm_parameters *ofdm = &fep->u.ofdm;
	int search_state,auto_val;
	u16 val;

	if (tuner && state->config.pll_set) { /* initial call from dvb */
		state->config.pll_set(fe,fep);

		state->last_tuned_freq = fep->frequency;
	//	if (!scanboost) {
			dib3000mc_set_timing(state,0,ofdm->transmission_mode,ofdm->bandwidth);
			dib3000mc_init_auto_scan(state, ofdm->bandwidth, 0);
			state->last_tuned_bw = ofdm->bandwidth;

			wr_foreach(dib3000mc_reg_agc_bandwidth,dib3000mc_agc_bandwidth);
			wr(DIB3000MC_REG_RESTART,DIB3000MC_RESTART_AGC);
			wr(DIB3000MC_REG_RESTART,DIB3000MC_RESTART_OFF);

			/* Default cfg isi offset adp */
			wr_foreach(dib3000mc_reg_offset,dib3000mc_offset[0]);

			wr(DIB3000MC_REG_ISI,DIB3000MC_ISI_DEFAULT | DIB3000MC_ISI_INHIBIT);
			dib3000mc_set_adp_cfg(state,ofdm->constellation);
			wr(DIB3000MC_REG_UNK_133,DIB3000MC_UNK_133);

			wr_foreach(dib3000mc_reg_bandwidth_general,dib3000mc_bandwidth_general);
			/* power smoothing */
			if (ofdm->bandwidth != BANDWIDTH_8_MHZ) {
				wr_foreach(dib3000mc_reg_bw,dib3000mc_bw[0]);
			} else {
				wr_foreach(dib3000mc_reg_bw,dib3000mc_bw[3]);
			}
			auto_val = 0;
			dib3000mc_set_general_cfg(state,fep,&auto_val);
			dib3000mc_set_impulse_noise(state,0,ofdm->constellation,ofdm->bandwidth);

			val = rd(DIB3000MC_REG_DEMOD_PARM);
			wr(DIB3000MC_REG_DEMOD_PARM,val|DIB3000MC_DEMOD_RST_DEMOD_ON);
			wr(DIB3000MC_REG_DEMOD_PARM,val);
	//	}
		msleep(70);

		/* something has to be auto searched */
		if (auto_val) {
			int as_count=0;

			deb_setf("autosearch enabled.\n");

			val = rd(DIB3000MC_REG_DEMOD_PARM);
			wr(DIB3000MC_REG_DEMOD_PARM,val | DIB3000MC_DEMOD_RST_AUTO_SRCH_ON);
			wr(DIB3000MC_REG_DEMOD_PARM,val);

			while ((search_state = dib3000_search_status(
						rd(DIB3000MC_REG_AS_IRQ),1)) < 0 && as_count++ < 100)
				msleep(10);

			deb_info("search_state after autosearch %d after %d checks\n",search_state,as_count);

			if (search_state == 1) {
				struct dvb_frontend_parameters feps;
				if (dib3000mc_get_frontend(fe, &feps) == 0) {
					deb_setf("reading tuning data from frontend succeeded.\n");
					return dib3000mc_set_frontend(fe, &feps, 0);
				}
			}
		} else {
			dib3000mc_set_impulse_noise(state,0,ofdm->transmission_mode,ofdm->bandwidth);
			wr(DIB3000MC_REG_ISI,DIB3000MC_ISI_DEFAULT|DIB3000MC_ISI_ACTIVATE);
			dib3000mc_set_adp_cfg(state,ofdm->constellation);

			/* set_offset_cfg */
			wr_foreach(dib3000mc_reg_offset,
					dib3000mc_offset[(ofdm->transmission_mode == TRANSMISSION_MODE_8K)+1]);
		}
	} else { /* second call, after autosearch (fka: set_WithKnownParams) */
//		dib3000mc_set_timing(state,1,ofdm->transmission_mode,ofdm->bandwidth);

		auto_val = 0;
		dib3000mc_set_general_cfg(state,fep,&auto_val);
		if (auto_val)
			deb_info("auto_val is true, even though an auto search was already performed.\n");

		dib3000mc_set_impulse_noise(state,0,ofdm->constellation,ofdm->bandwidth);

		val = rd(DIB3000MC_REG_DEMOD_PARM);
		wr(DIB3000MC_REG_DEMOD_PARM,val | DIB3000MC_DEMOD_RST_AUTO_SRCH_ON);
		wr(DIB3000MC_REG_DEMOD_PARM,val);

		msleep(30);

		wr(DIB3000MC_REG_ISI,DIB3000MC_ISI_DEFAULT|DIB3000MC_ISI_ACTIVATE);
			dib3000mc_set_adp_cfg(state,ofdm->constellation);
		wr_foreach(dib3000mc_reg_offset,
				dib3000mc_offset[(ofdm->transmission_mode == TRANSMISSION_MODE_8K)+1]);
	}
	return 0;
}

static int dib3000mc_fe_init(struct dvb_frontend* fe, int mobile_mode)
{
	struct dib3000_state *state = fe->demodulator_priv;
	deb_info("init start\n");

	state->timing_offset = 0;
	state->timing_offset_comp_done = 0;

	wr(DIB3000MC_REG_RESTART,DIB3000MC_RESTART_CONFIG);
	wr(DIB3000MC_REG_RESTART,DIB3000MC_RESTART_OFF);
	wr(DIB3000MC_REG_CLK_CFG_1,DIB3000MC_CLK_CFG_1_POWER_UP);
	wr(DIB3000MC_REG_CLK_CFG_2,DIB3000MC_CLK_CFG_2_PUP_MOBILE);
	wr(DIB3000MC_REG_CLK_CFG_3,DIB3000MC_CLK_CFG_3_POWER_UP);
	wr(DIB3000MC_REG_CLK_CFG_7,DIB3000MC_CLK_CFG_7_INIT);

	wr(DIB3000MC_REG_RST_UNC,DIB3000MC_RST_UNC_OFF);
	wr(DIB3000MC_REG_UNK_19,DIB3000MC_UNK_19);

	wr(33,5);
	wr(36,81);
	wr(DIB3000MC_REG_UNK_88,DIB3000MC_UNK_88);

	wr(DIB3000MC_REG_UNK_99,DIB3000MC_UNK_99);
	wr(DIB3000MC_REG_UNK_111,DIB3000MC_UNK_111_PH_N_MODE_0); /* phase noise algo off */

	/* mobile mode - portable reception */
	wr_foreach(dib3000mc_reg_mobile_mode,dib3000mc_mobile_mode[1]);

/* TUNER_PANASONIC_ENV57H12D5: */
	wr_foreach(dib3000mc_reg_agc_bandwidth,dib3000mc_agc_bandwidth);
	wr_foreach(dib3000mc_reg_agc_bandwidth_general,dib3000mc_agc_bandwidth_general);
	wr_foreach(dib3000mc_reg_agc,dib3000mc_agc_tuner[1]);

	wr(DIB3000MC_REG_UNK_110,DIB3000MC_UNK_110);
	wr(26,0x6680);
	wr(DIB3000MC_REG_UNK_1,DIB3000MC_UNK_1);
	wr(DIB3000MC_REG_UNK_2,DIB3000MC_UNK_2);
	wr(DIB3000MC_REG_UNK_3,DIB3000MC_UNK_3);
	wr(DIB3000MC_REG_SEQ_TPS,DIB3000MC_SEQ_TPS_DEFAULT);

	wr_foreach(dib3000mc_reg_bandwidth,dib3000mc_bandwidth_8mhz);
	wr_foreach(dib3000mc_reg_bandwidth_general,dib3000mc_bandwidth_general);

	wr(DIB3000MC_REG_UNK_4,DIB3000MC_UNK_4);

	wr(DIB3000MC_REG_SET_DDS_FREQ_MSB,DIB3000MC_DDS_FREQ_MSB_INV_OFF);
	wr(DIB3000MC_REG_SET_DDS_FREQ_LSB,DIB3000MC_DDS_FREQ_LSB);

	dib3000mc_set_timing(state,0,TRANSMISSION_MODE_8K,BANDWIDTH_8_MHZ);
//	wr_foreach(dib3000mc_reg_timing_freq,dib3000mc_timing_freq[3]);

	wr(DIB3000MC_REG_UNK_120,DIB3000MC_UNK_120);
	wr(DIB3000MC_REG_UNK_134,DIB3000MC_UNK_134);
	wr(DIB3000MC_REG_FEC_CFG,DIB3000MC_FEC_CFG);

	wr(DIB3000MC_REG_DIVERSITY3,DIB3000MC_DIVERSITY3_IN_OFF);

	dib3000mc_set_impulse_noise(state,0,TRANSMISSION_MODE_8K,BANDWIDTH_8_MHZ);

/* output mode control, just the MPEG2_SLAVE */
//	set_or(DIB3000MC_REG_OUTMODE,DIB3000MC_OM_SLAVE);
	wr(DIB3000MC_REG_OUTMODE,DIB3000MC_OM_SLAVE);
	wr(DIB3000MC_REG_SMO_MODE,DIB3000MC_SMO_MODE_SLAVE);
	wr(DIB3000MC_REG_FIFO_THRESHOLD,DIB3000MC_FIFO_THRESHOLD_SLAVE);
	wr(DIB3000MC_REG_ELEC_OUT,DIB3000MC_ELEC_OUT_SLAVE);

/* MPEG2_PARALLEL_CONTINUOUS_CLOCK
	wr(DIB3000MC_REG_OUTMODE,
		DIB3000MC_SET_OUTMODE(DIB3000MC_OM_PAR_CONT_CLK,
			rd(DIB3000MC_REG_OUTMODE)));

	wr(DIB3000MC_REG_SMO_MODE,
			DIB3000MC_SMO_MODE_DEFAULT |
			DIB3000MC_SMO_MODE_188);

	wr(DIB3000MC_REG_FIFO_THRESHOLD,DIB3000MC_FIFO_THRESHOLD_DEFAULT);
	wr(DIB3000MC_REG_ELEC_OUT,DIB3000MC_ELEC_OUT_DIV_OUT_ON);
*/

/* diversity */
	wr(DIB3000MC_REG_DIVERSITY1,DIB3000MC_DIVERSITY1_DEFAULT);
	wr(DIB3000MC_REG_DIVERSITY2,DIB3000MC_DIVERSITY2_DEFAULT);

	set_and(DIB3000MC_REG_DIVERSITY3,DIB3000MC_DIVERSITY3_IN_OFF);

	set_or(DIB3000MC_REG_CLK_CFG_7,DIB3000MC_CLK_CFG_7_DIV_IN_OFF);

	if (state->config.pll_init)
		state->config.pll_init(fe);

	deb_info("init end\n");
	return 0;
}
static int dib3000mc_read_status(struct dvb_frontend* fe, fe_status_t *stat)
{
	struct dib3000_state* state = fe->demodulator_priv;
	u16 lock = rd(DIB3000MC_REG_LOCKING);

	*stat = 0;
	if (DIB3000MC_AGC_LOCK(lock))
		*stat |= FE_HAS_SIGNAL;
	if (DIB3000MC_CARRIER_LOCK(lock))
		*stat |= FE_HAS_CARRIER;
	if (DIB3000MC_TPS_LOCK(lock))
		*stat |= FE_HAS_VITERBI;
	if (DIB3000MC_MPEG_SYNC_LOCK(lock))
		*stat |= (FE_HAS_SYNC | FE_HAS_LOCK);

	deb_stat("actual status is %2x fifo_level: %x,244: %x, 206: %x, 207: %x, 1040: %x\n",*stat,rd(510),rd(244),rd(206),rd(207),rd(1040));

	return 0;
}

static int dib3000mc_read_ber(struct dvb_frontend* fe, u32 *ber)
{
	struct dib3000_state* state = fe->demodulator_priv;
	*ber = ((rd(DIB3000MC_REG_BER_MSB) << 16) | rd(DIB3000MC_REG_BER_LSB));
	return 0;
}

static int dib3000mc_read_unc_blocks(struct dvb_frontend* fe, u32 *unc)
{
	struct dib3000_state* state = fe->demodulator_priv;

	*unc = rd(DIB3000MC_REG_PACKET_ERRORS);
	return 0;
}

/* see dib3000mb.c for calculation comments */
static int dib3000mc_read_signal_strength(struct dvb_frontend* fe, u16 *strength)
{
	struct dib3000_state* state = fe->demodulator_priv;
	u16 val = rd(DIB3000MC_REG_SIGNAL_NOISE_LSB);
	*strength = (((val >> 6) & 0xff) << 8) + (val & 0x3f);

	deb_stat("signal: mantisse = %d, exponent = %d\n",(*strength >> 8) & 0xff, *strength & 0xff);
	return 0;
}

/* see dib3000mb.c for calculation comments */
static int dib3000mc_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	struct dib3000_state* state = fe->demodulator_priv;
	u16 val = rd(DIB3000MC_REG_SIGNAL_NOISE_LSB),
		val2 = rd(DIB3000MC_REG_SIGNAL_NOISE_MSB);
	u16 sig,noise;

	sig =   (((val >> 6) & 0xff) << 8) + (val & 0x3f);
	noise = (((val >> 4) & 0xff) << 8) + ((val & 0xf) << 2) + ((val2 >> 14) & 0x3);
	if (noise == 0)
		*snr = 0xffff;
	else
		*snr = (u16) sig/noise;

	deb_stat("signal: mantisse = %d, exponent = %d\n",(sig >> 8) & 0xff, sig & 0xff);
	deb_stat("noise:  mantisse = %d, exponent = %d\n",(noise >> 8) & 0xff, noise & 0xff);
	deb_stat("snr: %d\n",*snr);
	return 0;
}

static int dib3000mc_sleep(struct dvb_frontend* fe)
{
	struct dib3000_state* state = fe->demodulator_priv;

	set_or(DIB3000MC_REG_CLK_CFG_7,DIB3000MC_CLK_CFG_7_PWR_DOWN);
	wr(DIB3000MC_REG_CLK_CFG_1,DIB3000MC_CLK_CFG_1_POWER_DOWN);
	wr(DIB3000MC_REG_CLK_CFG_2,DIB3000MC_CLK_CFG_2_POWER_DOWN);
	wr(DIB3000MC_REG_CLK_CFG_3,DIB3000MC_CLK_CFG_3_POWER_DOWN);
	return 0;
}

static int dib3000mc_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static int dib3000mc_fe_init_nonmobile(struct dvb_frontend* fe)
{
	return dib3000mc_fe_init(fe, 0);
}

static int dib3000mc_set_frontend_and_tuner(struct dvb_frontend* fe, struct dvb_frontend_parameters *fep)
{
	return dib3000mc_set_frontend(fe, fep, 1);
}

static void dib3000mc_release(struct dvb_frontend* fe)
{
	struct dib3000_state *state = fe->demodulator_priv;
	kfree(state);
}

/* pid filter and transfer stuff */
static int dib3000mc_pid_control(struct dvb_frontend *fe,int index, int pid,int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;
	pid = (onoff ? pid | DIB3000_ACTIVATE_PID_FILTERING : 0);
	wr(index+DIB3000MC_REG_FIRST_PID,pid);
	return 0;
}

static int dib3000mc_fifo_control(struct dvb_frontend *fe, int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;
	u16 tmp = rd(DIB3000MC_REG_SMO_MODE);

	deb_xfer("%s fifo\n",onoff ? "enabling" : "disabling");

	if (onoff) {
		deb_xfer("%d %x\n",tmp & DIB3000MC_SMO_MODE_FIFO_UNFLUSH,tmp & DIB3000MC_SMO_MODE_FIFO_UNFLUSH);
		wr(DIB3000MC_REG_SMO_MODE,tmp & DIB3000MC_SMO_MODE_FIFO_UNFLUSH);
	} else {
		deb_xfer("%d %x\n",tmp | DIB3000MC_SMO_MODE_FIFO_FLUSH,tmp | DIB3000MC_SMO_MODE_FIFO_FLUSH);
		wr(DIB3000MC_REG_SMO_MODE,tmp | DIB3000MC_SMO_MODE_FIFO_FLUSH);
	}
	return 0;
}

static int dib3000mc_pid_parse(struct dvb_frontend *fe, int onoff)
{
	struct dib3000_state *state = fe->demodulator_priv;
	u16 tmp = rd(DIB3000MC_REG_SMO_MODE);

	deb_xfer("%s pid parsing\n",onoff ? "enabling" : "disabling");

	if (onoff) {
		wr(DIB3000MC_REG_SMO_MODE,tmp | DIB3000MC_SMO_MODE_PID_PARSE);
	} else {
		wr(DIB3000MC_REG_SMO_MODE,tmp & DIB3000MC_SMO_MODE_NO_PID_PARSE);
	}
	return 0;
}

static int dib3000mc_tuner_pass_ctrl(struct dvb_frontend *fe, int onoff, u8 pll_addr)
{
	struct dib3000_state *state = fe->demodulator_priv;
	if (onoff) {
		wr(DIB3000MC_REG_TUNER, DIB3000_TUNER_WRITE_ENABLE(pll_addr));
	} else {
		wr(DIB3000MC_REG_TUNER, DIB3000_TUNER_WRITE_DISABLE(pll_addr));
	}
	return 0;
}

static int dib3000mc_demod_init(struct dib3000_state *state)
{
	u16 default_addr = 0x0a;
	/* first init */
	if (state->config.demod_address != default_addr) {
		deb_info("initializing the demod the first time. Setting demod addr to 0x%x\n",default_addr);
		wr(DIB3000MC_REG_ELEC_OUT,DIB3000MC_ELEC_OUT_DIV_OUT_ON);
		wr(DIB3000MC_REG_OUTMODE,DIB3000MC_OM_PAR_CONT_CLK);

		wr(DIB3000MC_REG_RST_I2C_ADDR,
			DIB3000MC_DEMOD_ADDR(default_addr) |
			DIB3000MC_DEMOD_ADDR_ON);

		state->config.demod_address = default_addr;

		wr(DIB3000MC_REG_RST_I2C_ADDR,
			DIB3000MC_DEMOD_ADDR(default_addr));
	} else
		deb_info("demod is already initialized. Demod addr: 0x%x\n",state->config.demod_address);
	return 0;
}


static struct dvb_frontend_ops dib3000mc_ops;

struct dvb_frontend* dib3000mc_attach(const struct dib3000_config* config,
				      struct i2c_adapter* i2c, struct dib_fe_xfer_ops *xfer_ops)
{
	struct dib3000_state* state = NULL;
	u16 devid;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct dib3000_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(struct dib3000_state));

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config,config,sizeof(struct dib3000_config));
	memcpy(&state->ops, &dib3000mc_ops, sizeof(struct dvb_frontend_ops));

	/* check for the correct demod */
	if (rd(DIB3000_REG_MANUFACTOR_ID) != DIB3000_I2C_ID_DIBCOM)
		goto error;

	devid = rd(DIB3000_REG_DEVICE_ID);
	if (devid != DIB3000MC_DEVICE_ID && devid != DIB3000P_DEVICE_ID)
		goto error;

	switch (devid) {
		case DIB3000MC_DEVICE_ID:
			info("Found a DiBcom 3000M-C, interesting...");
			break;
		case DIB3000P_DEVICE_ID:
			info("Found a DiBcom 3000P.");
			break;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;

	/* set the xfer operations */
	xfer_ops->pid_parse = dib3000mc_pid_parse;
	xfer_ops->fifo_ctrl = dib3000mc_fifo_control;
	xfer_ops->pid_ctrl = dib3000mc_pid_control;
	xfer_ops->tuner_pass_ctrl = dib3000mc_tuner_pass_ctrl;

	dib3000mc_demod_init(state);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dib3000mc_ops = {

	.info = {
		.name			= "DiBcom 3000P/M-C DVB-T",
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

	.release = dib3000mc_release,

	.init = dib3000mc_fe_init_nonmobile,
	.sleep = dib3000mc_sleep,

	.set_frontend = dib3000mc_set_frontend_and_tuner,
	.get_frontend = dib3000mc_get_frontend,
	.get_tune_settings = dib3000mc_fe_get_tune_settings,

	.read_status = dib3000mc_read_status,
	.read_ber = dib3000mc_read_ber,
	.read_signal_strength = dib3000mc_read_signal_strength,
	.read_snr = dib3000mc_read_snr,
	.read_ucblocks = dib3000mc_read_unc_blocks,
};

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(dib3000mc_attach);
