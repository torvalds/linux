/* Frontend part of the Linux driver for the Afatech 9005
 * USB1.1 DVB-T receiver.
 *
 * Copyright (C) 2007 Luca Olivetti (luca@ventoso.org)
 *
 * Thanks to Afatech who kindly provided information.
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
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "af9005.h"
#include "af9005-script.h"
#include "mt2060.h"
#include "qt1010.h"
#include <asm/div64.h>

struct af9005_fe_state {
	struct dvb_usb_device *d;
	enum fe_status stat;

	/* retraining parameters */
	u32 original_fcw;
	u16 original_rf_top;
	u16 original_if_top;
	u16 original_if_min;
	u16 original_aci0_if_top;
	u16 original_aci1_if_top;
	u16 original_aci0_if_min;
	u8 original_if_unplug_th;
	u8 original_rf_unplug_th;
	u8 original_dtop_if_unplug_th;
	u8 original_dtop_rf_unplug_th;

	/* statistics */
	u32 pre_vit_error_count;
	u32 pre_vit_bit_count;
	u32 ber;
	u32 post_vit_error_count;
	u32 post_vit_bit_count;
	u32 unc;
	u16 abort_count;

	int opened;
	int strong;
	unsigned long next_status_check;
	struct dvb_frontend frontend;
};

static int af9005_write_word_agc(struct dvb_usb_device *d, u16 reghi,
				 u16 reglo, u8 pos, u8 len, u16 value)
{
	int ret;

	if ((ret = af9005_write_ofdm_register(d, reglo, (u8) (value & 0xff))))
		return ret;
	return af9005_write_register_bits(d, reghi, pos, len,
					  (u8) ((value & 0x300) >> 8));
}

static int af9005_read_word_agc(struct dvb_usb_device *d, u16 reghi,
				u16 reglo, u8 pos, u8 len, u16 * value)
{
	int ret;
	u8 temp0, temp1;

	if ((ret = af9005_read_ofdm_register(d, reglo, &temp0)))
		return ret;
	if ((ret = af9005_read_ofdm_register(d, reghi, &temp1)))
		return ret;
	switch (pos) {
	case 0:
		*value = ((u16) (temp1 & 0x03) << 8) + (u16) temp0;
		break;
	case 2:
		*value = ((u16) (temp1 & 0x0C) << 6) + (u16) temp0;
		break;
	case 4:
		*value = ((u16) (temp1 & 0x30) << 4) + (u16) temp0;
		break;
	case 6:
		*value = ((u16) (temp1 & 0xC0) << 2) + (u16) temp0;
		break;
	default:
		err("invalid pos in read word agc");
		return -EINVAL;
	}
	return 0;

}

static int af9005_is_fecmon_available(struct dvb_frontend *fe, int *available)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;
	u8 temp;

	*available = false;

	ret = af9005_read_register_bits(state->d, xd_p_fec_vtb_rsd_mon_en,
					fec_vtb_rsd_mon_en_pos,
					fec_vtb_rsd_mon_en_len, &temp);
	if (ret)
		return ret;
	if (temp & 1) {
		ret =
		    af9005_read_register_bits(state->d,
					      xd_p_reg_ofsm_read_rbc_en,
					      reg_ofsm_read_rbc_en_pos,
					      reg_ofsm_read_rbc_en_len, &temp);
		if (ret)
			return ret;
		if ((temp & 1) == 0)
			*available = true;

	}
	return 0;
}

static int af9005_get_post_vit_err_cw_count(struct dvb_frontend *fe,
					    u32 * post_err_count,
					    u32 * post_cw_count,
					    u16 * abort_count)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;
	u32 err_count;
	u32 cw_count;
	u8 temp, temp0, temp1, temp2;
	u16 loc_abort_count;

	*post_err_count = 0;
	*post_cw_count = 0;

	/* check if error bit count is ready */
	ret =
	    af9005_read_register_bits(state->d, xd_r_fec_rsd_ber_rdy,
				      fec_rsd_ber_rdy_pos, fec_rsd_ber_rdy_len,
				      &temp);
	if (ret)
		return ret;
	if (!temp) {
		deb_info("rsd counter not ready\n");
		return 100;
	}
	/* get abort count */
	ret =
	    af9005_read_ofdm_register(state->d,
				      xd_r_fec_rsd_abort_packet_cnt_7_0,
				      &temp0);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d,
				      xd_r_fec_rsd_abort_packet_cnt_15_8,
				      &temp1);
	if (ret)
		return ret;
	loc_abort_count = ((u16) temp1 << 8) + temp0;

	/* get error count */
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_rsd_bit_err_cnt_7_0,
				      &temp0);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_rsd_bit_err_cnt_15_8,
				      &temp1);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_rsd_bit_err_cnt_23_16,
				      &temp2);
	if (ret)
		return ret;
	err_count = ((u32) temp2 << 16) + ((u32) temp1 << 8) + temp0;
	*post_err_count = err_count - (u32) loc_abort_count *8 * 8;

	/* get RSD packet number */
	ret =
	    af9005_read_ofdm_register(state->d, xd_p_fec_rsd_packet_unit_7_0,
				      &temp0);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_p_fec_rsd_packet_unit_15_8,
				      &temp1);
	if (ret)
		return ret;
	cw_count = ((u32) temp1 << 8) + temp0;
	if (cw_count == 0) {
		err("wrong RSD packet count");
		return -EIO;
	}
	deb_info("POST abort count %d err count %d rsd packets %d\n",
		 loc_abort_count, err_count, cw_count);
	*post_cw_count = cw_count - (u32) loc_abort_count;
	*abort_count = loc_abort_count;
	return 0;

}

static int af9005_get_post_vit_ber(struct dvb_frontend *fe,
				   u32 * post_err_count, u32 * post_cw_count,
				   u16 * abort_count)
{
	u32 loc_cw_count = 0, loc_err_count;
	u16 loc_abort_count = 0;
	int ret;

	ret =
	    af9005_get_post_vit_err_cw_count(fe, &loc_err_count, &loc_cw_count,
					     &loc_abort_count);
	if (ret)
		return ret;
	*post_err_count = loc_err_count;
	*post_cw_count = loc_cw_count * 204 * 8;
	*abort_count = loc_abort_count;

	return 0;
}

static int af9005_get_pre_vit_err_bit_count(struct dvb_frontend *fe,
					    u32 * pre_err_count,
					    u32 * pre_bit_count)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	u8 temp, temp0, temp1, temp2;
	u32 super_frame_count, x, bits;
	int ret;

	ret =
	    af9005_read_register_bits(state->d, xd_r_fec_vtb_ber_rdy,
				      fec_vtb_ber_rdy_pos, fec_vtb_ber_rdy_len,
				      &temp);
	if (ret)
		return ret;
	if (!temp) {
		deb_info("viterbi counter not ready\n");
		return 101;	/* ERR_APO_VTB_COUNTER_NOT_READY; */
	}
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_vtb_err_bit_cnt_7_0,
				      &temp0);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_vtb_err_bit_cnt_15_8,
				      &temp1);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_fec_vtb_err_bit_cnt_23_16,
				      &temp2);
	if (ret)
		return ret;
	*pre_err_count = ((u32) temp2 << 16) + ((u32) temp1 << 8) + temp0;

	ret =
	    af9005_read_ofdm_register(state->d, xd_p_fec_super_frm_unit_7_0,
				      &temp0);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_p_fec_super_frm_unit_15_8,
				      &temp1);
	if (ret)
		return ret;
	super_frame_count = ((u32) temp1 << 8) + temp0;
	if (super_frame_count == 0) {
		deb_info("super frame count 0\n");
		return 102;
	}

	/* read fft mode */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_txmod,
				      reg_tpsd_txmod_pos, reg_tpsd_txmod_len,
				      &temp);
	if (ret)
		return ret;
	if (temp == 0) {
		/* 2K */
		x = 1512;
	} else if (temp == 1) {
		/* 8k */
		x = 6048;
	} else {
		err("Invalid fft mode");
		return -EINVAL;
	}

	/* read modulation mode */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_const,
				      reg_tpsd_const_pos, reg_tpsd_const_len,
				      &temp);
	if (ret)
		return ret;
	switch (temp) {
	case 0:		/* QPSK */
		bits = 2;
		break;
	case 1:		/* QAM_16 */
		bits = 4;
		break;
	case 2:		/* QAM_64 */
		bits = 6;
		break;
	default:
		err("invalid modulation mode");
		return -EINVAL;
	}
	*pre_bit_count = super_frame_count * 68 * 4 * x * bits;
	deb_info("PRE err count %d frame count %d bit count %d\n",
		 *pre_err_count, super_frame_count, *pre_bit_count);
	return 0;
}

static int af9005_reset_pre_viterbi(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;

	/* set super frame count to 1 */
	ret =
	    af9005_write_ofdm_register(state->d, xd_p_fec_super_frm_unit_7_0,
				       1 & 0xff);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, xd_p_fec_super_frm_unit_15_8,
					 1 >> 8);
	if (ret)
		return ret;
	/* reset pre viterbi error count */
	ret =
	    af9005_write_register_bits(state->d, xd_p_fec_vtb_ber_rst,
				       fec_vtb_ber_rst_pos, fec_vtb_ber_rst_len,
				       1);

	return ret;
}

static int af9005_reset_post_viterbi(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;

	/* set packet unit */
	ret =
	    af9005_write_ofdm_register(state->d, xd_p_fec_rsd_packet_unit_7_0,
				       10000 & 0xff);
	if (ret)
		return ret;
	ret =
	    af9005_write_ofdm_register(state->d, xd_p_fec_rsd_packet_unit_15_8,
				       10000 >> 8);
	if (ret)
		return ret;
	/* reset post viterbi error count */
	ret =
	    af9005_write_register_bits(state->d, xd_p_fec_rsd_ber_rst,
				       fec_rsd_ber_rst_pos, fec_rsd_ber_rst_len,
				       1);

	return ret;
}

static int af9005_get_statistic(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret, fecavailable;
	u64 numerator, denominator;

	deb_info("GET STATISTIC\n");
	ret = af9005_is_fecmon_available(fe, &fecavailable);
	if (ret)
		return ret;
	if (!fecavailable) {
		deb_info("fecmon not available\n");
		return 0;
	}

	ret = af9005_get_pre_vit_err_bit_count(fe, &state->pre_vit_error_count,
					       &state->pre_vit_bit_count);
	if (ret == 0) {
		af9005_reset_pre_viterbi(fe);
		if (state->pre_vit_bit_count > 0) {
			/* according to v 0.0.4 of the dvb api ber should be a multiple
			   of 10E-9 so we have to multiply the error count by
			   10E9=1000000000 */
			numerator =
			    (u64) state->pre_vit_error_count * (u64) 1000000000;
			denominator = (u64) state->pre_vit_bit_count;
			state->ber = do_div(numerator, denominator);
		} else {
			state->ber = 0xffffffff;
		}
	}

	ret = af9005_get_post_vit_ber(fe, &state->post_vit_error_count,
				      &state->post_vit_bit_count,
				      &state->abort_count);
	if (ret == 0) {
		ret = af9005_reset_post_viterbi(fe);
		state->unc += state->abort_count;
		if (ret)
			return ret;
	}
	return 0;
}

static int af9005_fe_refresh_state(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	if (time_after(jiffies, state->next_status_check)) {
		deb_info("REFRESH STATE\n");

		/* statistics */
		if (af9005_get_statistic(fe))
			err("get_statistic_failed");
		state->next_status_check = jiffies + 250 * HZ / 1000;
	}
	return 0;
}

static int af9005_fe_read_status(struct dvb_frontend *fe,
				 enum fe_status *stat)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	u8 temp;
	int ret;

	if (fe->ops.tuner_ops.release == NULL)
		return -ENODEV;

	*stat = 0;
	ret = af9005_read_register_bits(state->d, xd_p_agc_lock,
					agc_lock_pos, agc_lock_len, &temp);
	if (ret)
		return ret;
	if (temp)
		*stat |= FE_HAS_SIGNAL;

	ret = af9005_read_register_bits(state->d, xd_p_fd_tpsd_lock,
					fd_tpsd_lock_pos, fd_tpsd_lock_len,
					&temp);
	if (ret)
		return ret;
	if (temp)
		*stat |= FE_HAS_CARRIER;

	ret = af9005_read_register_bits(state->d,
					xd_r_mp2if_sync_byte_locked,
					mp2if_sync_byte_locked_pos,
					mp2if_sync_byte_locked_pos, &temp);
	if (ret)
		return ret;
	if (temp)
		*stat |= FE_HAS_SYNC | FE_HAS_VITERBI | FE_HAS_LOCK;
	if (state->opened)
		af9005_led_control(state->d, *stat & FE_HAS_LOCK);

	ret =
	    af9005_read_register_bits(state->d, xd_p_reg_strong_sginal_detected,
				      reg_strong_sginal_detected_pos,
				      reg_strong_sginal_detected_len, &temp);
	if (ret)
		return ret;
	if (temp != state->strong) {
		deb_info("adjust for strong signal %d\n", temp);
		state->strong = temp;
	}
	return 0;
}

static int af9005_fe_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	if (fe->ops.tuner_ops.release  == NULL)
		return -ENODEV;
	af9005_fe_refresh_state(fe);
	*ber = state->ber;
	return 0;
}

static int af9005_fe_read_unc_blocks(struct dvb_frontend *fe, u32 * unc)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	if (fe->ops.tuner_ops.release == NULL)
		return -ENODEV;
	af9005_fe_refresh_state(fe);
	*unc = state->unc;
	return 0;
}

static int af9005_fe_read_signal_strength(struct dvb_frontend *fe,
					  u16 * strength)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;
	u8 if_gain, rf_gain;

	if (fe->ops.tuner_ops.release == NULL)
		return -ENODEV;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_reg_aagc_rf_gain,
				      &rf_gain);
	if (ret)
		return ret;
	ret =
	    af9005_read_ofdm_register(state->d, xd_r_reg_aagc_if_gain,
				      &if_gain);
	if (ret)
		return ret;
	/* this value has no real meaning, but i don't have the tables that relate
	   the rf and if gain with the dbm, so I just scale the value */
	*strength = (512 - rf_gain - if_gain) << 7;
	return 0;
}

static int af9005_fe_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	/* the snr can be derived from the ber and the modulation
	   but I don't think this kind of complex calculations belong
	   in the driver. I may be wrong.... */
	return -ENOSYS;
}

static int af9005_fe_program_cfoe(struct dvb_usb_device *d, u32 bw)
{
	u8 temp0, temp1, temp2, temp3, buf[4];
	int ret;
	u32 NS_coeff1_2048Nu;
	u32 NS_coeff1_8191Nu;
	u32 NS_coeff1_8192Nu;
	u32 NS_coeff1_8193Nu;
	u32 NS_coeff2_2k;
	u32 NS_coeff2_8k;

	switch (bw) {
	case 6000000:
		NS_coeff1_2048Nu = 0x2ADB6DC;
		NS_coeff1_8191Nu = 0xAB7313;
		NS_coeff1_8192Nu = 0xAB6DB7;
		NS_coeff1_8193Nu = 0xAB685C;
		NS_coeff2_2k = 0x156DB6E;
		NS_coeff2_8k = 0x55B6DC;
		break;

	case 7000000:
		NS_coeff1_2048Nu = 0x3200001;
		NS_coeff1_8191Nu = 0xC80640;
		NS_coeff1_8192Nu = 0xC80000;
		NS_coeff1_8193Nu = 0xC7F9C0;
		NS_coeff2_2k = 0x1900000;
		NS_coeff2_8k = 0x640000;
		break;

	case 8000000:
		NS_coeff1_2048Nu = 0x3924926;
		NS_coeff1_8191Nu = 0xE4996E;
		NS_coeff1_8192Nu = 0xE49249;
		NS_coeff1_8193Nu = 0xE48B25;
		NS_coeff2_2k = 0x1C92493;
		NS_coeff2_8k = 0x724925;
		break;
	default:
		err("Invalid bandwidth %d.", bw);
		return -EINVAL;
	}

	/*
	 *  write NS_coeff1_2048Nu
	 */

	temp0 = (u8) (NS_coeff1_2048Nu & 0x000000FF);
	temp1 = (u8) ((NS_coeff1_2048Nu & 0x0000FF00) >> 8);
	temp2 = (u8) ((NS_coeff1_2048Nu & 0x00FF0000) >> 16);
	temp3 = (u8) ((NS_coeff1_2048Nu & 0x03000000) >> 24);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	/*  cfoe_NS_2k_coeff1_25_24 */
	ret = af9005_write_ofdm_register(d, 0xAE00, buf[0]);
	if (ret)
		return ret;

	/*  cfoe_NS_2k_coeff1_23_16 */
	ret = af9005_write_ofdm_register(d, 0xAE01, buf[1]);
	if (ret)
		return ret;

	/*  cfoe_NS_2k_coeff1_15_8 */
	ret = af9005_write_ofdm_register(d, 0xAE02, buf[2]);
	if (ret)
		return ret;

	/*  cfoe_NS_2k_coeff1_7_0 */
	ret = af9005_write_ofdm_register(d, 0xAE03, buf[3]);
	if (ret)
		return ret;

	/*
	 *  write NS_coeff2_2k
	 */

	temp0 = (u8) ((NS_coeff2_2k & 0x0000003F));
	temp1 = (u8) ((NS_coeff2_2k & 0x00003FC0) >> 6);
	temp2 = (u8) ((NS_coeff2_2k & 0x003FC000) >> 14);
	temp3 = (u8) ((NS_coeff2_2k & 0x01C00000) >> 22);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	ret = af9005_write_ofdm_register(d, 0xAE04, buf[0]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE05, buf[1]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE06, buf[2]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE07, buf[3]);
	if (ret)
		return ret;

	/*
	 *  write NS_coeff1_8191Nu
	 */

	temp0 = (u8) ((NS_coeff1_8191Nu & 0x000000FF));
	temp1 = (u8) ((NS_coeff1_8191Nu & 0x0000FF00) >> 8);
	temp2 = (u8) ((NS_coeff1_8191Nu & 0x00FFC000) >> 16);
	temp3 = (u8) ((NS_coeff1_8191Nu & 0x03000000) >> 24);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	ret = af9005_write_ofdm_register(d, 0xAE08, buf[0]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE09, buf[1]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE0A, buf[2]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE0B, buf[3]);
	if (ret)
		return ret;

	/*
	 *  write NS_coeff1_8192Nu
	 */

	temp0 = (u8) (NS_coeff1_8192Nu & 0x000000FF);
	temp1 = (u8) ((NS_coeff1_8192Nu & 0x0000FF00) >> 8);
	temp2 = (u8) ((NS_coeff1_8192Nu & 0x00FFC000) >> 16);
	temp3 = (u8) ((NS_coeff1_8192Nu & 0x03000000) >> 24);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	ret = af9005_write_ofdm_register(d, 0xAE0C, buf[0]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE0D, buf[1]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE0E, buf[2]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE0F, buf[3]);
	if (ret)
		return ret;

	/*
	 *  write NS_coeff1_8193Nu
	 */

	temp0 = (u8) ((NS_coeff1_8193Nu & 0x000000FF));
	temp1 = (u8) ((NS_coeff1_8193Nu & 0x0000FF00) >> 8);
	temp2 = (u8) ((NS_coeff1_8193Nu & 0x00FFC000) >> 16);
	temp3 = (u8) ((NS_coeff1_8193Nu & 0x03000000) >> 24);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	ret = af9005_write_ofdm_register(d, 0xAE10, buf[0]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE11, buf[1]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE12, buf[2]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE13, buf[3]);
	if (ret)
		return ret;

	/*
	 *  write NS_coeff2_8k
	 */

	temp0 = (u8) ((NS_coeff2_8k & 0x0000003F));
	temp1 = (u8) ((NS_coeff2_8k & 0x00003FC0) >> 6);
	temp2 = (u8) ((NS_coeff2_8k & 0x003FC000) >> 14);
	temp3 = (u8) ((NS_coeff2_8k & 0x01C00000) >> 22);

	/*  big endian to make 8051 happy */
	buf[0] = temp3;
	buf[1] = temp2;
	buf[2] = temp1;
	buf[3] = temp0;

	ret = af9005_write_ofdm_register(d, 0xAE14, buf[0]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE15, buf[1]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE16, buf[2]);
	if (ret)
		return ret;

	ret = af9005_write_ofdm_register(d, 0xAE17, buf[3]);
	return ret;

}

static int af9005_fe_select_bw(struct dvb_usb_device *d, u32 bw)
{
	u8 temp;
	switch (bw) {
	case 6000000:
		temp = 0;
		break;
	case 7000000:
		temp = 1;
		break;
	case 8000000:
		temp = 2;
		break;
	default:
		err("Invalid bandwidth %d.", bw);
		return -EINVAL;
	}
	return af9005_write_register_bits(d, xd_g_reg_bw, reg_bw_pos,
					  reg_bw_len, temp);
}

static int af9005_fe_power(struct dvb_frontend *fe, int on)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	u8 temp = on;
	int ret;
	deb_info("power %s tuner\n", on ? "on" : "off");
	ret = af9005_send_command(state->d, 0x03, &temp, 1, NULL, 0);
	return ret;
}

static struct mt2060_config af9005_mt2060_config = {
	0xC0
};

static struct qt1010_config af9005_qt1010_config = {
	0xC4
};

static int af9005_fe_init(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	int ret, i, scriptlen;
	u8 temp, temp0 = 0, temp1 = 0, temp2 = 0;
	u8 buf[2];
	u16 if1;

	deb_info("in af9005_fe_init\n");

	/* reset */
	deb_info("reset\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_I2C_reg_ofdm_rst_en,
					4, 1, 0x01)))
		return ret;
	if ((ret = af9005_write_ofdm_register(state->d, APO_REG_RESET, 0)))
		return ret;
	/* clear ofdm reset */
	deb_info("clear ofdm reset\n");
	for (i = 0; i < 150; i++) {
		if ((ret =
		     af9005_read_ofdm_register(state->d,
					       xd_I2C_reg_ofdm_rst, &temp)))
			return ret;
		if (temp & (regmask[reg_ofdm_rst_len - 1] << reg_ofdm_rst_pos))
			break;
		msleep(10);
	}
	if (i == 150)
		return -ETIMEDOUT;

	/*FIXME in the dump
	   write B200 A9
	   write xd_g_reg_ofsm_clk 7
	   read eepr c6 (2)
	   read eepr c7 (2)
	   misc ctrl 3 -> 1
	   read eepr ca (6)
	   write xd_g_reg_ofsm_clk 0
	   write B200 a1
	 */
	ret = af9005_write_ofdm_register(state->d, 0xb200, 0xa9);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, xd_g_reg_ofsm_clk, 0x07);
	if (ret)
		return ret;
	temp = 0x01;
	ret = af9005_send_command(state->d, 0x03, &temp, 1, NULL, 0);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, xd_g_reg_ofsm_clk, 0x00);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, 0xb200, 0xa1);
	if (ret)
		return ret;

	temp = regmask[reg_ofdm_rst_len - 1] << reg_ofdm_rst_pos;
	if ((ret =
	     af9005_write_register_bits(state->d, xd_I2C_reg_ofdm_rst,
					reg_ofdm_rst_pos, reg_ofdm_rst_len, 1)))
		return ret;
	ret = af9005_write_register_bits(state->d, xd_I2C_reg_ofdm_rst,
					 reg_ofdm_rst_pos, reg_ofdm_rst_len, 0);

	if (ret)
		return ret;
	/* don't know what register aefc is, but this is what the windows driver does */
	ret = af9005_write_ofdm_register(state->d, 0xaefc, 0);
	if (ret)
		return ret;

	/* set stand alone chip */
	deb_info("set stand alone chip\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_dca_stand_alone,
					reg_dca_stand_alone_pos,
					reg_dca_stand_alone_len, 1)))
		return ret;

	/* set dca upper & lower chip */
	deb_info("set dca upper & lower chip\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_dca_upper_chip,
					reg_dca_upper_chip_pos,
					reg_dca_upper_chip_len, 0)))
		return ret;
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_dca_lower_chip,
					reg_dca_lower_chip_pos,
					reg_dca_lower_chip_len, 0)))
		return ret;

	/* set 2wire master clock to 0x14 (for 60KHz) */
	deb_info("set 2wire master clock to 0x14 (for 60KHz)\n");
	if ((ret =
	     af9005_write_ofdm_register(state->d, xd_I2C_i2c_m_period, 0x14)))
		return ret;

	/* clear dca enable chip */
	deb_info("clear dca enable chip\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_dca_en,
					reg_dca_en_pos, reg_dca_en_len, 0)))
		return ret;
	/* FIXME these are register bits, but I don't know which ones */
	ret = af9005_write_ofdm_register(state->d, 0xa16c, 1);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, 0xa3c1, 0);
	if (ret)
		return ret;

	/* init other parameters: program cfoe and select bandwidth */
	deb_info("program cfoe\n");
	ret = af9005_fe_program_cfoe(state->d, 6000000);
	if (ret)
		return ret;
	/* set read-update bit for modulation */
	deb_info("set read-update bit for modulation\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_feq_read_update,
					reg_feq_read_update_pos,
					reg_feq_read_update_len, 1)))
		return ret;

	/* sample code has a set MPEG TS code here
	   but sniffing reveals that it doesn't do it */

	/* set read-update bit to 1 for DCA modulation */
	deb_info("set read-update bit 1 for DCA modulation\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_reg_dca_read_update,
					reg_dca_read_update_pos,
					reg_dca_read_update_len, 1)))
		return ret;

	/* enable fec monitor */
	deb_info("enable fec monitor\n");
	if ((ret =
	     af9005_write_register_bits(state->d, xd_p_fec_vtb_rsd_mon_en,
					fec_vtb_rsd_mon_en_pos,
					fec_vtb_rsd_mon_en_len, 1)))
		return ret;

	/* FIXME should be register bits, I don't know which ones */
	ret = af9005_write_ofdm_register(state->d, 0xa601, 0);

	/* set api_retrain_never_freeze */
	deb_info("set api_retrain_never_freeze\n");
	if ((ret = af9005_write_ofdm_register(state->d, 0xaefb, 0x01)))
		return ret;

	/* load init script */
	deb_info("load init script\n");
	scriptlen = sizeof(script) / sizeof(RegDesc);
	for (i = 0; i < scriptlen; i++) {
		if ((ret =
		     af9005_write_register_bits(state->d, script[i].reg,
						script[i].pos,
						script[i].len, script[i].val)))
			return ret;
		/* save 3 bytes of original fcw */
		if (script[i].reg == 0xae18)
			temp2 = script[i].val;
		if (script[i].reg == 0xae19)
			temp1 = script[i].val;
		if (script[i].reg == 0xae1a)
			temp0 = script[i].val;

		/* save original unplug threshold */
		if (script[i].reg == xd_p_reg_unplug_th)
			state->original_if_unplug_th = script[i].val;
		if (script[i].reg == xd_p_reg_unplug_rf_gain_th)
			state->original_rf_unplug_th = script[i].val;
		if (script[i].reg == xd_p_reg_unplug_dtop_if_gain_th)
			state->original_dtop_if_unplug_th = script[i].val;
		if (script[i].reg == xd_p_reg_unplug_dtop_rf_gain_th)
			state->original_dtop_rf_unplug_th = script[i].val;

	}
	state->original_fcw =
	    ((u32) temp2 << 16) + ((u32) temp1 << 8) + (u32) temp0;


	/* save original TOPs */
	deb_info("save original TOPs\n");

	/*  RF TOP */
	ret =
	    af9005_read_word_agc(state->d,
				 xd_p_reg_aagc_rf_top_numerator_9_8,
				 xd_p_reg_aagc_rf_top_numerator_7_0, 0, 2,
				 &state->original_rf_top);
	if (ret)
		return ret;

	/*  IF TOP */
	ret =
	    af9005_read_word_agc(state->d,
				 xd_p_reg_aagc_if_top_numerator_9_8,
				 xd_p_reg_aagc_if_top_numerator_7_0, 0, 2,
				 &state->original_if_top);
	if (ret)
		return ret;

	/*  ACI 0 IF TOP */
	ret =
	    af9005_read_word_agc(state->d, 0xA60E, 0xA60A, 4, 2,
				 &state->original_aci0_if_top);
	if (ret)
		return ret;

	/*  ACI 1 IF TOP */
	ret =
	    af9005_read_word_agc(state->d, 0xA60E, 0xA60B, 6, 2,
				 &state->original_aci1_if_top);
	if (ret)
		return ret;

	/* attach tuner and init */
	if (fe->ops.tuner_ops.release == NULL) {
		/* read tuner and board id from eeprom */
		ret = af9005_read_eeprom(adap->dev, 0xc6, buf, 2);
		if (ret) {
			err("Impossible to read EEPROM\n");
			return ret;
		}
		deb_info("Tuner id %d, board id %d\n", buf[0], buf[1]);
		switch (buf[0]) {
		case 2:	/* MT2060 */
			/* read if1 from eeprom */
			ret = af9005_read_eeprom(adap->dev, 0xc8, buf, 2);
			if (ret) {
				err("Impossible to read EEPROM\n");
				return ret;
			}
			if1 = (u16) (buf[0] << 8) + buf[1];
			if (dvb_attach(mt2060_attach, fe, &adap->dev->i2c_adap,
					 &af9005_mt2060_config, if1) == NULL) {
				deb_info("MT2060 attach failed\n");
				return -ENODEV;
			}
			break;
		case 3:	/* QT1010 */
		case 9:	/* QT1010B */
			if (dvb_attach(qt1010_attach, fe, &adap->dev->i2c_adap,
					&af9005_qt1010_config) ==NULL) {
				deb_info("QT1010 attach failed\n");
				return -ENODEV;
			}
			break;
		default:
			err("Unsupported tuner type %d", buf[0]);
			return -ENODEV;
		}
		ret = fe->ops.tuner_ops.init(fe);
		if (ret)
			return ret;
	}

	deb_info("profit!\n");
	return 0;
}

static int af9005_fe_sleep(struct dvb_frontend *fe)
{
	return af9005_fe_power(fe, 0);
}

static int af9005_ts_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct af9005_fe_state *state = fe->demodulator_priv;

	if (acquire) {
		state->opened++;
	} else {

		state->opened--;
		if (!state->opened)
			af9005_led_control(state->d, 0);
	}
	return 0;
}

static int af9005_fe_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;
	u8 temp, temp0, temp1, temp2;

	deb_info("af9005_fe_set_frontend freq %d bw %d\n", fep->frequency,
		 fep->bandwidth_hz);
	if (fe->ops.tuner_ops.release == NULL) {
		err("Tuner not attached");
		return -ENODEV;
	}

	deb_info("turn off led\n");
	/* not in the log */
	ret = af9005_led_control(state->d, 0);
	if (ret)
		return ret;
	/* not sure about the bits */
	ret = af9005_write_register_bits(state->d, XD_MP2IF_MISC, 2, 1, 0);
	if (ret)
		return ret;

	/* set FCW to default value */
	deb_info("set FCW to default value\n");
	temp0 = (u8) (state->original_fcw & 0x000000ff);
	temp1 = (u8) ((state->original_fcw & 0x0000ff00) >> 8);
	temp2 = (u8) ((state->original_fcw & 0x00ff0000) >> 16);
	ret = af9005_write_ofdm_register(state->d, 0xae1a, temp0);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, 0xae19, temp1);
	if (ret)
		return ret;
	ret = af9005_write_ofdm_register(state->d, 0xae18, temp2);
	if (ret)
		return ret;

	/* restore original TOPs */
	deb_info("restore original TOPs\n");
	ret =
	    af9005_write_word_agc(state->d,
				  xd_p_reg_aagc_rf_top_numerator_9_8,
				  xd_p_reg_aagc_rf_top_numerator_7_0, 0, 2,
				  state->original_rf_top);
	if (ret)
		return ret;
	ret =
	    af9005_write_word_agc(state->d,
				  xd_p_reg_aagc_if_top_numerator_9_8,
				  xd_p_reg_aagc_if_top_numerator_7_0, 0, 2,
				  state->original_if_top);
	if (ret)
		return ret;
	ret =
	    af9005_write_word_agc(state->d, 0xA60E, 0xA60A, 4, 2,
				  state->original_aci0_if_top);
	if (ret)
		return ret;
	ret =
	    af9005_write_word_agc(state->d, 0xA60E, 0xA60B, 6, 2,
				  state->original_aci1_if_top);
	if (ret)
		return ret;

	/* select bandwidth */
	deb_info("select bandwidth");
	ret = af9005_fe_select_bw(state->d, fep->bandwidth_hz);
	if (ret)
		return ret;
	ret = af9005_fe_program_cfoe(state->d, fep->bandwidth_hz);
	if (ret)
		return ret;

	/* clear easy mode flag */
	deb_info("clear easy mode flag\n");
	ret = af9005_write_ofdm_register(state->d, 0xaefd, 0);
	if (ret)
		return ret;

	/* set unplug threshold to original value */
	deb_info("set unplug threshold to original value\n");
	ret =
	    af9005_write_ofdm_register(state->d, xd_p_reg_unplug_th,
				       state->original_if_unplug_th);
	if (ret)
		return ret;
	/* set tuner */
	deb_info("set tuner\n");
	ret = fe->ops.tuner_ops.set_params(fe);
	if (ret)
		return ret;

	/* trigger ofsm */
	deb_info("trigger ofsm\n");
	temp = 0;
	ret = af9005_write_tuner_registers(state->d, 0xffff, &temp, 1);
	if (ret)
		return ret;

	/* clear retrain and freeze flag */
	deb_info("clear retrain and freeze flag\n");
	ret =
	    af9005_write_register_bits(state->d,
				       xd_p_reg_api_retrain_request,
				       reg_api_retrain_request_pos, 2, 0);
	if (ret)
		return ret;

	/* reset pre viterbi and post viterbi registers and statistics */
	af9005_reset_pre_viterbi(fe);
	af9005_reset_post_viterbi(fe);
	state->pre_vit_error_count = 0;
	state->pre_vit_bit_count = 0;
	state->ber = 0;
	state->post_vit_error_count = 0;
	/* state->unc = 0; commented out since it should be ever increasing */
	state->abort_count = 0;

	state->next_status_check = jiffies;
	state->strong = -1;

	return 0;
}

static int af9005_fe_get_frontend(struct dvb_frontend *fe,
				  struct dtv_frontend_properties *fep)
{
	struct af9005_fe_state *state = fe->demodulator_priv;
	int ret;
	u8 temp;

	/* mode */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_const,
				      reg_tpsd_const_pos, reg_tpsd_const_len,
				      &temp);
	if (ret)
		return ret;
	deb_info("===== fe_get_frontend_legacy = =============\n");
	deb_info("CONSTELLATION ");
	switch (temp) {
	case 0:
		fep->modulation = QPSK;
		deb_info("QPSK\n");
		break;
	case 1:
		fep->modulation = QAM_16;
		deb_info("QAM_16\n");
		break;
	case 2:
		fep->modulation = QAM_64;
		deb_info("QAM_64\n");
		break;
	}

	/* tps hierarchy and alpha value */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_hier,
				      reg_tpsd_hier_pos, reg_tpsd_hier_len,
				      &temp);
	if (ret)
		return ret;
	deb_info("HIERARCHY ");
	switch (temp) {
	case 0:
		fep->hierarchy = HIERARCHY_NONE;
		deb_info("NONE\n");
		break;
	case 1:
		fep->hierarchy = HIERARCHY_1;
		deb_info("1\n");
		break;
	case 2:
		fep->hierarchy = HIERARCHY_2;
		deb_info("2\n");
		break;
	case 3:
		fep->hierarchy = HIERARCHY_4;
		deb_info("4\n");
		break;
	}

	/*  high/low priority     */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_dec_pri,
				      reg_dec_pri_pos, reg_dec_pri_len, &temp);
	if (ret)
		return ret;
	/* if temp is set = high priority */
	deb_info("PRIORITY %s\n", temp ? "high" : "low");

	/* high coderate */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_hpcr,
				      reg_tpsd_hpcr_pos, reg_tpsd_hpcr_len,
				      &temp);
	if (ret)
		return ret;
	deb_info("CODERATE HP ");
	switch (temp) {
	case 0:
		fep->code_rate_HP = FEC_1_2;
		deb_info("FEC_1_2\n");
		break;
	case 1:
		fep->code_rate_HP = FEC_2_3;
		deb_info("FEC_2_3\n");
		break;
	case 2:
		fep->code_rate_HP = FEC_3_4;
		deb_info("FEC_3_4\n");
		break;
	case 3:
		fep->code_rate_HP = FEC_5_6;
		deb_info("FEC_5_6\n");
		break;
	case 4:
		fep->code_rate_HP = FEC_7_8;
		deb_info("FEC_7_8\n");
		break;
	}

	/* low coderate */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_lpcr,
				      reg_tpsd_lpcr_pos, reg_tpsd_lpcr_len,
				      &temp);
	if (ret)
		return ret;
	deb_info("CODERATE LP ");
	switch (temp) {
	case 0:
		fep->code_rate_LP = FEC_1_2;
		deb_info("FEC_1_2\n");
		break;
	case 1:
		fep->code_rate_LP = FEC_2_3;
		deb_info("FEC_2_3\n");
		break;
	case 2:
		fep->code_rate_LP = FEC_3_4;
		deb_info("FEC_3_4\n");
		break;
	case 3:
		fep->code_rate_LP = FEC_5_6;
		deb_info("FEC_5_6\n");
		break;
	case 4:
		fep->code_rate_LP = FEC_7_8;
		deb_info("FEC_7_8\n");
		break;
	}

	/* guard interval */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_gi,
				      reg_tpsd_gi_pos, reg_tpsd_gi_len, &temp);
	if (ret)
		return ret;
	deb_info("GUARD INTERVAL ");
	switch (temp) {
	case 0:
		fep->guard_interval = GUARD_INTERVAL_1_32;
		deb_info("1_32\n");
		break;
	case 1:
		fep->guard_interval = GUARD_INTERVAL_1_16;
		deb_info("1_16\n");
		break;
	case 2:
		fep->guard_interval = GUARD_INTERVAL_1_8;
		deb_info("1_8\n");
		break;
	case 3:
		fep->guard_interval = GUARD_INTERVAL_1_4;
		deb_info("1_4\n");
		break;
	}

	/* fft */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_tpsd_txmod,
				      reg_tpsd_txmod_pos, reg_tpsd_txmod_len,
				      &temp);
	if (ret)
		return ret;
	deb_info("TRANSMISSION MODE ");
	switch (temp) {
	case 0:
		fep->transmission_mode = TRANSMISSION_MODE_2K;
		deb_info("2K\n");
		break;
	case 1:
		fep->transmission_mode = TRANSMISSION_MODE_8K;
		deb_info("8K\n");
		break;
	}

	/* bandwidth      */
	ret =
	    af9005_read_register_bits(state->d, xd_g_reg_bw, reg_bw_pos,
				      reg_bw_len, &temp);
	deb_info("BANDWIDTH ");
	switch (temp) {
	case 0:
		fep->bandwidth_hz = 6000000;
		deb_info("6\n");
		break;
	case 1:
		fep->bandwidth_hz = 7000000;
		deb_info("7\n");
		break;
	case 2:
		fep->bandwidth_hz = 8000000;
		deb_info("8\n");
		break;
	}
	return 0;
}

static void af9005_fe_release(struct dvb_frontend *fe)
{
	struct af9005_fe_state *state =
	    (struct af9005_fe_state *)fe->demodulator_priv;
	kfree(state);
}

static const struct dvb_frontend_ops af9005_fe_ops;

struct dvb_frontend *af9005_fe_attach(struct dvb_usb_device *d)
{
	struct af9005_fe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct af9005_fe_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	deb_info("attaching frontend af9005\n");

	state->d = d;
	state->opened = 0;

	memcpy(&state->frontend.ops, &af9005_fe_ops,
	       sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	return &state->frontend;
      error:
	return NULL;
}

static const struct dvb_frontend_ops af9005_fe_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		 .name = "AF9005 USB DVB-T",
		 .frequency_min = 44250000,
		 .frequency_max = 867250000,
		 .frequency_stepsize = 250000,
		 .caps = FE_CAN_INVERSION_AUTO |
		 FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		 FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		 FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
		 FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
		 FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER |
		 FE_CAN_HIERARCHY_AUTO,
		 },

	.release = af9005_fe_release,

	.init = af9005_fe_init,
	.sleep = af9005_fe_sleep,
	.ts_bus_ctrl = af9005_ts_bus_ctrl,

	.set_frontend = af9005_fe_set_frontend,
	.get_frontend = af9005_fe_get_frontend,

	.read_status = af9005_fe_read_status,
	.read_ber = af9005_fe_read_ber,
	.read_signal_strength = af9005_fe_read_signal_strength,
	.read_snr = af9005_fe_read_snr,
	.read_ucblocks = af9005_fe_read_unc_blocks,
};
