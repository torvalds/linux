// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd_dvbt_mon.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * DVB-T monitor functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_tnrdmd_mon.h"
#include "cxd2880_tnrdmd_dvbt.h"
#include "cxd2880_tnrdmd_dvbt_mon.h"

#include <linux/int_log.h>

static const int ref_dbm_1000[3][5] = {
	{-93000, -91000, -90000, -89000, -88000},
	{-87000, -85000, -84000, -83000, -82000},
	{-82000, -80000, -78000, -77000, -76000},
};

static int is_tps_locked(struct cxd2880_tnrdmd *tnr_dmd);

int cxd2880_tnrdmd_dvbt_mon_sync_stat(struct cxd2880_tnrdmd
				      *tnr_dmd, u8 *sync_stat,
				      u8 *ts_lock_stat,
				      u8 *unlock_detected)
{
	u8 rdata = 0x00;
	int ret;

	if (!tnr_dmd || !sync_stat || !ts_lock_stat || !unlock_detected)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;
	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, &rdata, 1);
	if (ret)
		return ret;

	*unlock_detected = (rdata & 0x10) ? 1 : 0;
	*sync_stat = rdata & 0x07;
	*ts_lock_stat = (rdata & 0x20) ? 1 : 0;

	if (*sync_stat == 0x07)
		return -EAGAIN;

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_sync_stat_sub(struct cxd2880_tnrdmd
					  *tnr_dmd, u8 *sync_stat,
					  u8 *unlock_detected)
{
	u8 ts_lock_stat = 0;

	if (!tnr_dmd || !sync_stat || !unlock_detected)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt_mon_sync_stat(tnr_dmd->diver_sub,
						 sync_stat,
						 &ts_lock_stat,
						 unlock_detected);
}

int cxd2880_tnrdmd_dvbt_mon_mode_guard(struct cxd2880_tnrdmd
				       *tnr_dmd,
				       enum cxd2880_dvbt_mode
				       *mode,
				       enum cxd2880_dvbt_guard
				       *guard)
{
	u8 rdata = 0x00;
	int ret;

	if (!tnr_dmd || !mode || !guard)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret =
			    cxd2880_tnrdmd_dvbt_mon_mode_guard(tnr_dmd->diver_sub,
							       mode, guard);

		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x1b, &rdata, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*mode = (enum cxd2880_dvbt_mode)((rdata >> 2) & 0x03);
	*guard = (enum cxd2880_dvbt_guard)(rdata & 0x03);

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_carrier_offset(struct cxd2880_tnrdmd
					   *tnr_dmd, int *offset)
{
	u8 rdata[4];
	u32 ctl_val = 0;
	int ret;

	if (!tnr_dmd || !offset)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x1d, rdata, 4);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	ctl_val =
	    ((rdata[0] & 0x1f) << 24) | (rdata[1] << 16) | (rdata[2] << 8) |
	    (rdata[3]);
	*offset = cxd2880_convert2s_complement(ctl_val, 29);
	*offset = -1 * ((*offset) * tnr_dmd->bandwidth / 235);

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_carrier_offset_sub(struct
					       cxd2880_tnrdmd
					       *tnr_dmd,
					       int *offset)
{
	if (!tnr_dmd || !offset)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt_mon_carrier_offset(tnr_dmd->diver_sub,
						      offset);
}

int cxd2880_tnrdmd_dvbt_mon_tps_info(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     struct cxd2880_dvbt_tpsinfo
				     *info)
{
	u8 rdata[7];
	u8 cell_id_ok = 0;
	int ret;

	if (!tnr_dmd || !info)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret =
			    cxd2880_tnrdmd_dvbt_mon_tps_info(tnr_dmd->diver_sub,
							     info);

		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x29, rdata, 7);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x11);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xd5, &cell_id_ok, 1);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	info->constellation =
	    (enum cxd2880_dvbt_constellation)((rdata[0] >> 6) & 0x03);
	info->hierarchy = (enum cxd2880_dvbt_hierarchy)((rdata[0] >> 3) & 0x07);
	info->rate_hp = (enum cxd2880_dvbt_coderate)(rdata[0] & 0x07);
	info->rate_lp = (enum cxd2880_dvbt_coderate)((rdata[1] >> 5) & 0x07);
	info->guard = (enum cxd2880_dvbt_guard)((rdata[1] >> 3) & 0x03);
	info->mode = (enum cxd2880_dvbt_mode)((rdata[1] >> 1) & 0x03);
	info->fnum = (rdata[2] >> 6) & 0x03;
	info->length_indicator = rdata[2] & 0x3f;
	info->cell_id = (rdata[3] << 8) | rdata[4];
	info->reserved_even = rdata[5] & 0x3f;
	info->reserved_odd = rdata[6] & 0x3f;

	info->cell_id_ok = cell_id_ok & 0x01;

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_packet_error_number(struct
						cxd2880_tnrdmd
						*tnr_dmd,
						u32 *pen)
{
	u8 rdata[3];
	int ret;

	if (!tnr_dmd || !pen)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x26, rdata, 3);
	if (ret)
		return ret;

	if (!(rdata[0] & 0x01))
		return -EAGAIN;

	*pen = (rdata[1] << 8) | rdata[2];

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_spectrum_sense(struct cxd2880_tnrdmd
					   *tnr_dmd,
					    enum
					    cxd2880_tnrdmd_spectrum_sense
					    *sense)
{
	u8 data = 0;
	int ret;

	if (!tnr_dmd || !sense)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
			ret = cxd2880_tnrdmd_dvbt_mon_spectrum_sense(tnr_dmd->diver_sub,
								     sense);

		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x1c, &data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*sense =
	    (data & 0x01) ? CXD2880_TNRDMD_SPECTRUM_INV :
	    CXD2880_TNRDMD_SPECTRUM_NORMAL;

	return ret;
}

static int dvbt_read_snr_reg(struct cxd2880_tnrdmd *tnr_dmd,
			     u16 *reg_value)
{
	u8 rdata[2];
	int ret;

	if (!tnr_dmd || !reg_value)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x13, rdata, 2);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	slvt_unfreeze_reg(tnr_dmd);

	*reg_value = (rdata[0] << 8) | rdata[1];

	return ret;
}

static int dvbt_calc_snr(struct cxd2880_tnrdmd *tnr_dmd,
			 u32 reg_value, int *snr)
{
	if (!tnr_dmd || !snr)
		return -EINVAL;

	if (reg_value == 0)
		return -EAGAIN;

	if (reg_value > 4996)
		reg_value = 4996;

	*snr = intlog10(reg_value) - intlog10(5350 - reg_value);
	*snr = (*snr + 839) / 1678 + 28500;

	return 0;
}

int cxd2880_tnrdmd_dvbt_mon_snr(struct cxd2880_tnrdmd *tnr_dmd,
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

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE) {
		ret = dvbt_read_snr_reg(tnr_dmd, &reg_value);
		if (ret)
			return ret;

		ret = dvbt_calc_snr(tnr_dmd, reg_value, snr);
	} else {
		int snr_main = 0;
		int snr_sub = 0;

		ret =
		    cxd2880_tnrdmd_dvbt_mon_snr_diver(tnr_dmd, snr, &snr_main,
						      &snr_sub);
	}

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_snr_diver(struct cxd2880_tnrdmd
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

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = dvbt_read_snr_reg(tnr_dmd, &reg_value);
	if (!ret) {
		ret = dvbt_calc_snr(tnr_dmd, reg_value, snr_main);
		if (ret)
			reg_value = 0;
	} else if (ret == -EAGAIN) {
		reg_value = 0;
	} else {
		return ret;
	}

	reg_value_sum += reg_value;

	ret = dvbt_read_snr_reg(tnr_dmd->diver_sub, &reg_value);
	if (!ret) {
		ret = dvbt_calc_snr(tnr_dmd->diver_sub, reg_value, snr_sub);
		if (ret)
			reg_value = 0;
	} else if (ret == -EAGAIN) {
		reg_value = 0;
	} else {
		return ret;
	}

	reg_value_sum += reg_value;

	return dvbt_calc_snr(tnr_dmd, reg_value_sum, snr);
}

int cxd2880_tnrdmd_dvbt_mon_sampling_offset(struct cxd2880_tnrdmd
					    *tnr_dmd, int *ppm)
{
	u8 ctl_val_reg[5];
	u8 nominal_rate_reg[5];
	u32 trl_ctl_val = 0;
	u32 trcg_nominal_rate = 0;
	int num;
	int den;
	s8 diff_upper = 0;
	int ret;

	if (!tnr_dmd || !ppm)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnr_dmd);
	if (ret)
		return ret;

	ret = is_tps_locked(tnr_dmd);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0d);
	if (ret) {
		slvt_unfreeze_reg(tnr_dmd);
		return ret;
	}

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x21, ctl_val_reg,
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
				     0x60, nominal_rate_reg,
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

	return ret;
}

int cxd2880_tnrdmd_dvbt_mon_sampling_offset_sub(struct
						cxd2880_tnrdmd
						*tnr_dmd, int *ppm)
{
	if (!tnr_dmd || !ppm)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_dvbt_mon_sampling_offset(tnr_dmd->diver_sub, ppm);
}

static int dvbt_calc_ssi(struct cxd2880_tnrdmd *tnr_dmd,
			 int rf_lvl, u8 *ssi)
{
	struct cxd2880_dvbt_tpsinfo tps;
	int prel;
	int temp_ssi = 0;
	int ret;

	if (!tnr_dmd || !ssi)
		return -EINVAL;

	ret = cxd2880_tnrdmd_dvbt_mon_tps_info(tnr_dmd, &tps);
	if (ret)
		return ret;

	if (tps.constellation >= CXD2880_DVBT_CONSTELLATION_RESERVED_3 ||
	    tps.rate_hp >= CXD2880_DVBT_CODERATE_RESERVED_5)
		return -EINVAL;

	prel = rf_lvl - ref_dbm_1000[tps.constellation][tps.rate_hp];

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

int cxd2880_tnrdmd_dvbt_mon_ssi(struct cxd2880_tnrdmd *tnr_dmd,
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

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = cxd2880_tnrdmd_mon_rf_lvl(tnr_dmd, &rf_lvl);
	if (ret)
		return ret;

	return dvbt_calc_ssi(tnr_dmd, rf_lvl, ssi);
}

int cxd2880_tnrdmd_dvbt_mon_ssi_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    u8 *ssi)
{
	int rf_lvl = 0;
	int ret;

	if (!tnr_dmd || !ssi)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = cxd2880_tnrdmd_mon_rf_lvl(tnr_dmd->diver_sub, &rf_lvl);
	if (ret)
		return ret;

	return dvbt_calc_ssi(tnr_dmd, rf_lvl, ssi);
}

static int is_tps_locked(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 sync = 0;
	u8 tslock = 0;
	u8 early_unlock = 0;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_dvbt_mon_sync_stat(tnr_dmd, &sync, &tslock,
					      &early_unlock);
	if (ret)
		return ret;

	if (sync != 6)
		return -EAGAIN;

	return 0;
}
