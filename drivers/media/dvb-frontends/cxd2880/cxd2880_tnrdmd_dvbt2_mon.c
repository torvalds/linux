// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd_dvbt2_mon.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * DVB-T2 monitor functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_tnrdmd_mon.h"
#include "cxd2880_tnrdmd_dvbt2.h"
#include "cxd2880_tnrdmd_dvbt2_mon.h"

#include <media/dvb_math.h>

static const int ref_dbm_1000[4][8] = {
	{-96000, -95000, -94000, -93000, -92000, -92000, -98000, -97000},
	{-91000, -89000, -88000, -87000, -86000, -86000, -93000, -92000},
	{-86000, -85000, -83000, -82000, -81000, -80000, -89000, -88000},
	{-82000, -80000, -78000, -76000, -75000, -74000, -86000, -84000},
};

int cxd2880_tnrdmd_dvbt2_mon_sync_stat(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *sync_stat,
				       u8 *ts_lock_stat,
				       u8 *unlock_detected)
{
	u8 data;
	int ret;

	if (!tnr_dmd || !sync_stat || !ts_lock_stat || !unlock_detected)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, &data, sizeof(data));
	if (ret)
		return ret;

	*sync_stat = data & 0x07;
	*ts_lock_stat = ((data & 0x20) ? 1 : 0);
	*unlock_detected = ((data & 0x10) ? 1 : 0);

	if (*sync_stat == 0x07)
		return -EAGAIN;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub(struct cxd2880_tnrdmd
					   *tnr_dmd,
					   u8 *sync_stat,
					   u8 *unlock_detected)
{
	u8 ts_lock_stat = 0;

	if (!tnr_dmd || !sync_stat || !unlock_detected)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd->diver_sub,
						  sync_stat,
						  &ts_lock_stat,
						  unlock_detected);
}

int cxd2880_tnrdmd_dvbt2_mon_carrier_offset(struct cxd2880_tnrdmd
					    *tnr_dmd, int *offset)
{
	u8 data[4];
	u32 ctl_val = 0;
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	int ret;

	if (!tnr_dmd || !offset)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state != 6) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x30, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	ctl_val =
	    ((data[0] & 0x0f) << 24) | (data[1] << 16) | (data[2] << 8)
	    | (data[3]);
	*offset = cxd2880_convert2s_complement(ctl_val, 28);

	switch (tnr_dmd->bandwidth) {
	case CXD2880_DTV_BW_1_7_MHZ:
		*offset = -1 * ((*offset) / 582);
		break;
	case CXD2880_DTV_BW_5_MHZ:
	case CXD2880_DTV_BW_6_MHZ:
	case CXD2880_DTV_BW_7_MHZ:
	case CXD2880_DTV_BW_8_MHZ:
		*offset = -1 * ((*offset) * tnr_dmd->bandwidth / 940);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_carrier_offset_sub(struct
						cxd2880_tnrdmd
						*tnr_dmd,
						int *offset)
{
	if (!tnr_dmd || !offset)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt2_mon_carrier_offset(tnr_dmd->diver_sub,
						       offset);
}

int cxd2880_tnrdmd_dvbt2_mon_l1_pre(struct cxd2880_tnrdmd *tnr_dmd,
				    struct cxd2880_dvbt2_l1pre
				    *l1_pre)
{
	u8 data[37];
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 version = 0;
	enum cxd2880_dvbt2_profile profile;
	int ret;

	if (!tnr_dmd || !l1_pre)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state < 5) {
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN) {
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub
			    (tnr_dmd, &sync_state, &unlock_detected);
			if (ret) {
				slvt_unfreeze_reg(tnr_dmd);
				return ret;
			}

			if (sync_state < 5) {
				slvt_unfreeze_reg(tnr_dmd);
				return -EAGAIN;
			}
		} else {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}
	}

	ret = cxd2880_tnrdmd_dvbt2_mon_profile(tnr_dmd, &profile);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x61, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}
	slvt_unfreeze_reg(tnr_dmd);

	l1_pre->type = (enum cxd2880_dvbt2_l1pre_type)data[0];
	l1_pre->bw_ext = data[1] & 0x01;
	l1_pre->s1 = (enum cxd2880_dvbt2_s1)(data[2] & 0x07);
	l1_pre->s2 = data[3] & 0x0f;
	l1_pre->l1_rep = data[4] & 0x01;
	l1_pre->gi = (enum cxd2880_dvbt2_guard)(data[5] & 0x07);
	l1_pre->papr = (enum cxd2880_dvbt2_papr)(data[6] & 0x0f);
	l1_pre->mod =
	    (enum cxd2880_dvbt2_l1post_constell)(data[7] & 0x0f);
	l1_pre->cr = (enum cxd2880_dvbt2_l1post_cr)(data[8] & 0x03);
	l1_pre->fec =
	    (enum cxd2880_dvbt2_l1post_fec_type)(data[9] & 0x03);
	l1_pre->l1_post_size = (data[10] & 0x03) << 16;
	l1_pre->l1_post_size |= (data[11]) << 8;
	l1_pre->l1_post_size |= (data[12]);
	l1_pre->l1_post_info_size = (data[13] & 0x03) << 16;
	l1_pre->l1_post_info_size |= (data[14]) << 8;
	l1_pre->l1_post_info_size |= (data[15]);
	l1_pre->pp = (enum cxd2880_dvbt2_pp)(data[16] & 0x0f);
	l1_pre->tx_id_availability = data[17];
	l1_pre->cell_id = (data[18] << 8);
	l1_pre->cell_id |= (data[19]);
	l1_pre->network_id = (data[20] << 8);
	l1_pre->network_id |= (data[21]);
	l1_pre->sys_id = (data[22] << 8);
	l1_pre->sys_id |= (data[23]);
	l1_pre->num_frames = data[24];
	l1_pre->num_symbols = (data[25] & 0x0f) << 8;
	l1_pre->num_symbols |= data[26];
	l1_pre->regen = data[27] & 0x07;
	l1_pre->post_ext = data[28] & 0x01;
	l1_pre->num_rf_freqs = data[29] & 0x07;
	l1_pre->rf_idx = data[30] & 0x07;
	version = (data[31] & 0x03) << 2;
	version |= (data[32] & 0xc0) >> 6;
	l1_pre->t2_version = (enum cxd2880_dvbt2_version)version;
	l1_pre->l1_post_scrambled = (data[32] & 0x20) >> 5;
	l1_pre->t2_base_lite = (data[32] & 0x10) >> 4;
	l1_pre->crc32 = (data[33] << 24);
	l1_pre->crc32 |= (data[34] << 16);
	l1_pre->crc32 |= (data[35] << 8);
	l1_pre->crc32 |= data[36];

	if (profile == CXD2880_DVBT2_PROFILE_BASE) {
		switch ((l1_pre->s2 >> 1)) {
		case CXD2880_DVBT2_BASE_S2_M1K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M1K;
			break;
		case CXD2880_DVBT2_BASE_S2_M2K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M2K;
			break;
		case CXD2880_DVBT2_BASE_S2_M4K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M4K;
			break;
		case CXD2880_DVBT2_BASE_S2_M8K_G_DVBT:
		case CXD2880_DVBT2_BASE_S2_M8K_G_DVBT2:
			l1_pre->fft_mode = CXD2880_DVBT2_M8K;
			break;
		case CXD2880_DVBT2_BASE_S2_M16K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M16K;
			break;
		case CXD2880_DVBT2_BASE_S2_M32K_G_DVBT:
		case CXD2880_DVBT2_BASE_S2_M32K_G_DVBT2:
			l1_pre->fft_mode = CXD2880_DVBT2_M32K;
			break;
		default:
			return -EAGAIN;
		}
	} else if (profile == CXD2880_DVBT2_PROFILE_LITE) {
		switch ((l1_pre->s2 >> 1)) {
		case CXD2880_DVBT2_LITE_S2_M2K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M2K;
			break;
		case CXD2880_DVBT2_LITE_S2_M4K_G_ANY:
			l1_pre->fft_mode = CXD2880_DVBT2_M4K;
			break;
		case CXD2880_DVBT2_LITE_S2_M8K_G_DVBT:
		case CXD2880_DVBT2_LITE_S2_M8K_G_DVBT2:
			l1_pre->fft_mode = CXD2880_DVBT2_M8K;
			break;
		case CXD2880_DVBT2_LITE_S2_M16K_G_DVBT:
		case CXD2880_DVBT2_LITE_S2_M16K_G_DVBT2:
			l1_pre->fft_mode = CXD2880_DVBT2_M16K;
			break;
		default:
			return -EAGAIN;
		}
	} else {
		return -EAGAIN;
	}

	l1_pre->mixed = l1_pre->s2 & 0x01;

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_version(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     enum cxd2880_dvbt2_version
				     *ver)
{
	u8 data[2];
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 version = 0;
	int ret;

	if (!tnr_dmd || !ver)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state < 5) {
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN) {
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub
			    (tnr_dmd, &sync_state, &unlock_detected);
			if (ret) {
				slvt_unfreeze_reg(tnr_dmd);
				return ret;
			}

			if (sync_state < 5) {
				slvt_unfreeze_reg(tnr_dmd);
				return -EAGAIN;
			}
		} else {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x80, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	version = ((data[0] & 0x03) << 2);
	version |= ((data[1] & 0xc0) >> 6);
	*ver = (enum cxd2880_dvbt2_version)version;

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_ofdm(struct cxd2880_tnrdmd *tnr_dmd,
				  struct cxd2880_dvbt2_ofdm *ofdm)
{
	u8 data[5];
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	int ret;

	if (!tnr_dmd || !ofdm)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state != 6) {
		slvt_unfreeze_reg(tnr_dmd);

		ret = -EAGAIN;

		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_ofdm(tnr_dmd->diver_sub,
							  ofdm);

		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x1d, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	ofdm->mixed = ((data[0] & 0x20) ? 1 : 0);
	ofdm->is_miso = ((data[0] & 0x10) >> 4);
	ofdm->mode = (enum cxd2880_dvbt2_mode)(data[0] & 0x07);
	ofdm->gi = (enum cxd2880_dvbt2_guard)((data[1] & 0x70) >> 4);
	ofdm->pp = (enum cxd2880_dvbt2_pp)(data[1] & 0x07);
	ofdm->bw_ext = (data[2] & 0x10) >> 4;
	ofdm->papr = (enum cxd2880_dvbt2_papr)(data[2] & 0x0f);
	ofdm->num_symbols = (data[3] << 8) | data[4];

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_data_plps(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *plp_ids,
				       u8 *num_plps)
{
	u8 l1_post_ok = 0;
	int ret;

	if (!tnr_dmd || !num_plps)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &l1_post_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!(l1_post_ok & 0x01)) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xc1, num_plps, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (*num_plps == 0) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EINVAL;
	}

	if (!plp_ids) {
		slvt_unfreeze_reg(tnr_dmd);
		return 0;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xc2,
				     plp_ids,
				     ((*num_plps > 62) ?
				     62 : *num_plps));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (*num_plps > 62) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x0c);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x10, plp_ids + 62,
					     *num_plps - 62);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	}

	slvt_unfreeze_reg(tnr_dmd);

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_active_plp(struct cxd2880_tnrdmd
					*tnr_dmd,
					enum
					cxd2880_dvbt2_plp_btype
					type,
					struct cxd2880_dvbt2_plp
					*plp_info)
{
	u8 data[20];
	u8 addr = 0;
	u8 index = 0;
	u8 l1_post_ok = 0;
	int ret;

	if (!tnr_dmd || !plp_info)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &l1_post_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!l1_post_ok) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON)
		addr = 0xa9;
	else
		addr = 0x96;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     addr, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	if (type == CXD2880_DVBT2_PLP_COMMON && !data[13])
		return -EAGAIN;

	plp_info->id = data[index++];
	plp_info->type =
	    (enum cxd2880_dvbt2_plp_type)(data[index++] & 0x07);
	plp_info->payload =
	    (enum cxd2880_dvbt2_plp_payload)(data[index++] & 0x1f);
	plp_info->ff = data[index++] & 0x01;
	plp_info->first_rf_idx = data[index++] & 0x07;
	plp_info->first_frm_idx = data[index++];
	plp_info->group_id = data[index++];
	plp_info->plp_cr =
	    (enum cxd2880_dvbt2_plp_code_rate)(data[index++] & 0x07);
	plp_info->constell =
	    (enum cxd2880_dvbt2_plp_constell)(data[index++] & 0x07);
	plp_info->rot = data[index++] & 0x01;
	plp_info->fec =
	    (enum cxd2880_dvbt2_plp_fec)(data[index++] & 0x03);
	plp_info->num_blocks_max = (data[index++] & 0x03) << 8;
	plp_info->num_blocks_max |= data[index++];
	plp_info->frm_int = data[index++];
	plp_info->til_len = data[index++];
	plp_info->til_type = data[index++] & 0x01;

	plp_info->in_band_a_flag = data[index++] & 0x01;
	plp_info->rsvd = data[index++] << 8;
	plp_info->rsvd |= data[index++];

	plp_info->in_band_b_flag =
	    (plp_info->rsvd & 0x8000) >> 15;
	plp_info->plp_mode =
	    (enum cxd2880_dvbt2_plp_mode)((plp_info->rsvd & 0x000c) >> 2);
	plp_info->static_flag = (plp_info->rsvd & 0x0002) >> 1;
	plp_info->static_padding_flag = plp_info->rsvd & 0x0001;
	plp_info->rsvd = (plp_info->rsvd & 0x7ff0) >> 4;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_data_plp_error(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    u8 *plp_error)
{
	u8 data;
	int ret;

	if (!tnr_dmd || !plp_error)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &data, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if ((data & 0x01) == 0x00) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xc0, &data, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*plp_error = data & 0x01;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_l1_change(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *l1_change)
{
	u8 data;
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	int ret;

	if (!tnr_dmd || !l1_change)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state < 5) {
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN) {
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub
			    (tnr_dmd, &sync_state, &unlock_detected);
			if (ret) {
				slvt_unfreeze_reg(tnr_dmd);
				return ret;
			}

			if (sync_state < 5) {
				slvt_unfreeze_reg(tnr_dmd);
				return -EAGAIN;
			}
		} else {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x5f, &data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	*l1_change = data & 0x01;
	if (*l1_change) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x22);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x16, 0x01);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	}
	slvt_unfreeze_reg(tnr_dmd);

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_l1_post(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     struct cxd2880_dvbt2_l1post
				     *l1_post)
{
	u8 data[16];
	int ret;

	if (!tnr_dmd || !l1_post)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, data, sizeof(data));
	if (ret)
		return ret;

	if (!(data[0] & 0x01))
		return -EAGAIN;

	l1_post->sub_slices_per_frame = (data[1] & 0x7f) << 8;
	l1_post->sub_slices_per_frame |= data[2];
	l1_post->num_plps = data[3];
	l1_post->num_aux = data[4] & 0x0f;
	l1_post->aux_cfg_rfu = data[5];
	l1_post->rf_idx = data[6] & 0x07;
	l1_post->freq = data[7] << 24;
	l1_post->freq |= data[8] << 16;
	l1_post->freq |= data[9] << 8;
	l1_post->freq |= data[10];
	l1_post->fef_type = data[11] & 0x0f;
	l1_post->fef_length = data[12] << 16;
	l1_post->fef_length |= data[13] << 8;
	l1_post->fef_length |= data[14];
	l1_post->fef_intvl = data[15];

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_bbheader(struct cxd2880_tnrdmd
				      *tnr_dmd,
				      enum cxd2880_dvbt2_plp_btype
				      type,
				      struct cxd2880_dvbt2_bbheader
				      *bbheader)
{
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 data[14];
	u8 addr = 0;
	int ret;

	if (!tnr_dmd || !bbheader)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
						       &ts_lock,
						       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!ts_lock) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON) {
		u8 l1_post_ok;
		u8 data;

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x86, &l1_post_ok, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		if (!(l1_post_ok & 0x01)) {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xb6, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		if (data == 0) {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}
	}

	if (type == CXD2880_DVBT2_PLP_COMMON)
		addr = 0x51;
	else
		addr = 0x42;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     addr, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	bbheader->stream_input =
	    (enum cxd2880_dvbt2_stream)((data[0] >> 6) & 0x03);
	bbheader->is_single_input_stream = (data[0] >> 5) & 0x01;
	bbheader->is_constant_coding_modulation =
	    (data[0] >> 4) & 0x01;
	bbheader->issy_indicator = (data[0] >> 3) & 0x01;
	bbheader->null_packet_deletion = (data[0] >> 2) & 0x01;
	bbheader->ext = data[0] & 0x03;

	bbheader->input_stream_identifier = data[1];
	bbheader->plp_mode =
	    (data[3] & 0x01) ? CXD2880_DVBT2_PLP_MODE_HEM :
	    CXD2880_DVBT2_PLP_MODE_NM;
	bbheader->data_field_length = (data[4] << 8) | data[5];

	if (bbheader->plp_mode == CXD2880_DVBT2_PLP_MODE_NM) {
		bbheader->user_packet_length =
		    (data[6] << 8) | data[7];
		bbheader->sync_byte = data[8];
		bbheader->issy = 0;
	} else {
		bbheader->user_packet_length = 0;
		bbheader->sync_byte = 0;
		bbheader->issy =
		    (data[11] << 16) | (data[12] << 8) | data[13];
	}

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_in_bandb_ts_rate(struct cxd2880_tnrdmd
					      *tnr_dmd,
					      enum
					      cxd2880_dvbt2_plp_btype
					      type,
					      u32 *ts_rate_bps)
{
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 l1_post_ok = 0;
	u8 data[4];
	u8 addr = 0;

	int ret;

	if (!tnr_dmd || !ts_rate_bps)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!ts_lock) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &l1_post_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!(l1_post_ok & 0x01)) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON)
		addr = 0xba;
	else
		addr = 0xa7;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     addr, &data[0], 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if ((data[0] & 0x80) == 0x00) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x25);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON)
		addr = 0xa6;
	else
		addr = 0xaa;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     addr, &data[0], 4);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	*ts_rate_bps = ((data[0] & 0x07) << 24) | (data[1] << 16) |
		       (data[2] << 8) | data[3];

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_spectrum_sense(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    enum
					    cxd2880_tnrdmd_spectrum_sense
					    *sense)
{
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 early_unlock = 0;
	u8 data = 0;
	int ret;

	if (!tnr_dmd || !sense)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state, &ts_lock,
					       &early_unlock);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state != 6) {
		slvt_unfreeze_reg(tnr_dmd);

		ret = -EAGAIN;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_spectrum_sense(tnr_dmd->diver_sub,
								    sense);

		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x2f, &data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*sense =
	    (data & 0x01) ? CXD2880_TNRDMD_SPECTRUM_INV :
	    CXD2880_TNRDMD_SPECTRUM_NORMAL;

	return 0;
}

static int dvbt2_read_snr_reg(struct cxd2880_tnrdmd *tnr_dmd,
			      u16 *reg_value)
{
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 data[2];
	int ret;

	if (!tnr_dmd || !reg_value)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state != 6) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x13, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*reg_value = (data[0] << 8) | data[1];

	return ret;
}

static int dvbt2_calc_snr(struct cxd2880_tnrdmd *tnr_dmd,
			  u32 reg_value, int *snr)
{
	if (!tnr_dmd || !snr)
		return -EINVAL;

	if (reg_value == 0)
		return -EAGAIN;

	if (reg_value > 10876)
		reg_value = 10876;

	*snr = intlog10(reg_value) - intlog10(12600 - reg_value);
	*snr = (*snr + 839) / 1678 + 32000;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_snr(struct cxd2880_tnrdmd *tnr_dmd,
				 int *snr)
{
	u16 reg_value = 0;
	int ret;

	if (!tnr_dmd || !snr)
		return -EINVAL;

	*snr = -1000 * 1000;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE) {
		ret = dvbt2_read_snr_reg(tnr_dmd, &reg_value);
		if (ret)
			return ret;

		ret = dvbt2_calc_snr(tnr_dmd, reg_value, snr);
	} else {
		int snr_main = 0;
		int snr_sub = 0;

		ret =
		    cxd2880_tnrdmd_dvbt2_mon_snr_diver(tnr_dmd, snr, &snr_main,
						       &snr_sub);
	}

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_snr_diver(struct cxd2880_tnrdmd
				       *tnr_dmd, int *snr,
				       int *snr_main, int *snr_sub)
{
	u16 reg_value = 0;
	u32 reg_value_sum = 0;
	int ret;

	if (!tnr_dmd || !snr || !snr_main || !snr_sub)
		return -EINVAL;

	*snr = -1000 * 1000;
	*snr_main = -1000 * 1000;
	*snr_sub = -1000 * 1000;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = dvbt2_read_snr_reg(tnr_dmd, &reg_value);
	if (!ret) {
		ret = dvbt2_calc_snr(tnr_dmd, reg_value, snr_main);
		if (ret)
			reg_value = 0;
	} else if (ret == -EAGAIN) {
		reg_value = 0;
	} else {
		return ret;
	}

	reg_value_sum += reg_value;

	ret = dvbt2_read_snr_reg(tnr_dmd->diver_sub, &reg_value);
	if (!ret) {
		ret = dvbt2_calc_snr(tnr_dmd->diver_sub, reg_value, snr_sub);
		if (ret)
			reg_value = 0;
	} else if (ret == -EAGAIN) {
		reg_value = 0;
	} else {
		return ret;
	}

	reg_value_sum += reg_value;

	return dvbt2_calc_snr(tnr_dmd, reg_value_sum, snr);
}

int cxd2880_tnrdmd_dvbt2_mon_packet_error_number(struct
						 cxd2880_tnrdmd
						 *tnr_dmd,
						 u32 *pen)
{
	int ret;
	u8 data[3];

	if (!tnr_dmd || !pen)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x39, data, sizeof(data));
	if (ret)
		return ret;

	if (!(data[0] & 0x01))
		return -EAGAIN;

	*pen = ((data[1] << 8) | data[2]);

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_sampling_offset(struct cxd2880_tnrdmd
					     *tnr_dmd, int *ppm)
{
	u8 ctl_val_reg[5];
	u8 nominal_rate_reg[5];
	u32 trl_ctl_val = 0;
	u32 trcg_nominal_rate = 0;
	int num;
	int den;
	int ret;
	u8 sync_state = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	s8 diff_upper = 0;

	if (!tnr_dmd || !ppm)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_state,
					       &ts_lock,
					       &unlock_detected);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (sync_state != 6) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x34, ctl_val_reg,
				     sizeof(ctl_val_reg));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x04);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, nominal_rate_reg,
				     sizeof(nominal_rate_reg));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	diff_upper =
	    (ctl_val_reg[0] & 0x7f) - (nominal_rate_reg[0] & 0x7f);

	if (diff_upper < -1 || diff_upper > 1)
		return -EAGAIN;

	trl_ctl_val = ctl_val_reg[1] << 24;
	trl_ctl_val |= ctl_val_reg[2] << 16;
	trl_ctl_val |= ctl_val_reg[3] << 8;
	trl_ctl_val |= ctl_val_reg[4];

	trcg_nominal_rate = nominal_rate_reg[1] << 24;
	trcg_nominal_rate |= nominal_rate_reg[2] << 16;
	trcg_nominal_rate |= nominal_rate_reg[3] << 8;
	trcg_nominal_rate |= nominal_rate_reg[4];

	trl_ctl_val >>= 1;
	trcg_nominal_rate >>= 1;

	if (diff_upper == 1)
		num =
		    (int)((trl_ctl_val + 0x80000000u) -
			  trcg_nominal_rate);
	else if (diff_upper == -1)
		num =
		    -(int)((trcg_nominal_rate + 0x80000000u) -
			   trl_ctl_val);
	else
		num = (int)(trl_ctl_val - trcg_nominal_rate);

	den = (nominal_rate_reg[0] & 0x7f) << 24;
	den |= nominal_rate_reg[1] << 16;
	den |= nominal_rate_reg[2] << 8;
	den |= nominal_rate_reg[3];
	den = (den + (390625 / 2)) / 390625;

	den >>= 1;

	if (num >= 0)
		*ppm = (num + (den / 2)) / den;
	else
		*ppm = (num - (den / 2)) / den;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_mon_sampling_offset_sub(struct
						 cxd2880_tnrdmd
						 *tnr_dmd,
						 int *ppm)
{
	if (!tnr_dmd || !ppm)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt2_mon_sampling_offset(tnr_dmd->diver_sub,
							ppm);
}

int cxd2880_tnrdmd_dvbt2_mon_qam(struct cxd2880_tnrdmd *tnr_dmd,
				 enum cxd2880_dvbt2_plp_btype type,
				 enum cxd2880_dvbt2_plp_constell *qam)
{
	u8 data;
	u8 l1_post_ok = 0;
	int ret;

	if (!tnr_dmd || !qam)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &l1_post_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!(l1_post_ok & 0x01)) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON) {
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xb6, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		if (data == 0) {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xb1, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	} else {
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x9e, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	}

	slvt_unfreeze_reg(tnr_dmd);

	*qam = (enum cxd2880_dvbt2_plp_constell)(data & 0x07);

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_code_rate(struct cxd2880_tnrdmd
				       *tnr_dmd,
				       enum cxd2880_dvbt2_plp_btype
				       type,
				       enum
				       cxd2880_dvbt2_plp_code_rate
				       *code_rate)
{
	u8 data;
	u8 l1_post_ok = 0;
	int ret;

	if (!tnr_dmd || !code_rate)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &l1_post_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	if (!(l1_post_ok & 0x01)) {
		slvt_unfreeze_reg(tnr_dmd);
		return -EAGAIN;
	}

	if (type == CXD2880_DVBT2_PLP_COMMON) {
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xb6, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}

		if (data == 0) {
			slvt_unfreeze_reg(tnr_dmd);
			return -EAGAIN;
		}

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xb0, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	} else {
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x9d, &data, 1);
		if (ret) {
			slvt_unfreeze_reg(tnr_dmd);
			return ret;
		}
	}

	slvt_unfreeze_reg(tnr_dmd);

	*code_rate = (enum cxd2880_dvbt2_plp_code_rate)(data & 0x07);

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_profile(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     enum cxd2880_dvbt2_profile
				     *profile)
{
	u8 data;
	int ret;

	if (!tnr_dmd || !profile)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x22, &data, sizeof(data));
	if (ret)
		return ret;

	if (data & 0x02) {
		if (data & 0x01)
			*profile = CXD2880_DVBT2_PROFILE_LITE;
		else
			*profile = CXD2880_DVBT2_PROFILE_BASE;
	} else {
		ret = -EAGAIN;
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret =
			    cxd2880_tnrdmd_dvbt2_mon_profile(tnr_dmd->diver_sub,
							     profile);

		return ret;
	}

	return 0;
}

static int dvbt2_calc_ssi(struct cxd2880_tnrdmd *tnr_dmd,
			  int rf_lvl, u8 *ssi)
{
	enum cxd2880_dvbt2_plp_constell qam;
	enum cxd2880_dvbt2_plp_code_rate code_rate;
	int prel;
	int temp_ssi = 0;
	int ret;

	if (!tnr_dmd || !ssi)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_qam(tnr_dmd, CXD2880_DVBT2_PLP_DATA, &qam);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_dvbt2_mon_code_rate(tnr_dmd, CXD2880_DVBT2_PLP_DATA,
					       &code_rate);
	if (ret)
		return ret;

	if (code_rate > CXD2880_DVBT2_R2_5 || qam > CXD2880_DVBT2_QAM256)
		return -EINVAL;

	prel = rf_lvl - ref_dbm_1000[qam][code_rate];

	if (prel < -15000)
		temp_ssi = 0;
	else if (prel < 0)
		temp_ssi = ((2 * (prel + 15000)) + 1500) / 3000;
	else if (prel < 20000)
		temp_ssi = (((4 * prel) + 500) / 1000) + 10;
	else if (prel < 35000)
		temp_ssi = (((2 * (prel - 20000)) + 1500) / 3000) + 90;
	else
		temp_ssi = 100;

	*ssi = (temp_ssi > 100) ? 100 : (u8)temp_ssi;

	return ret;
}

int cxd2880_tnrdmd_dvbt2_mon_ssi(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 *ssi)
{
	int rf_lvl = 0;
	int ret;

	if (!tnr_dmd || !ssi)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = cxd2880_tnrdmd_mon_rf_lvl(tnr_dmd, &rf_lvl);
	if (ret)
		return ret;

	return dvbt2_calc_ssi(tnr_dmd, rf_lvl, ssi);
}

int cxd2880_tnrdmd_dvbt2_mon_ssi_sub(struct cxd2880_tnrdmd
				     *tnr_dmd, u8 *ssi)
{
	int rf_lvl = 0;
	int ret;

	if (!tnr_dmd || !ssi)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = cxd2880_tnrdmd_mon_rf_lvl(tnr_dmd->diver_sub, &rf_lvl);
	if (ret)
		return ret;

	return dvbt2_calc_ssi(tnr_dmd, rf_lvl, ssi);
}
