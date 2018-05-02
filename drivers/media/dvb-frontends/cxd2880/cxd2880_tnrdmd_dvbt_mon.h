/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd_dvbt_mon.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * DVB-T monitor interface
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_DVBT_MON_H
#define CXD2880_TNRDMD_DVBT_MON_H

#include "cxd2880_tnrdmd.h"
#include "cxd2880_dvbt.h"

int cxd2880_tnrdmd_dvbt_mon_sync_stat(struct cxd2880_tnrdmd
				      *tnr_dmd, u8 *sync_stat,
				      u8 *ts_lock_stat,
				      u8 *unlock_detected);

int cxd2880_tnrdmd_dvbt_mon_sync_stat_sub(struct cxd2880_tnrdmd
					  *tnr_dmd, u8 *sync_stat,
					  u8 *unlock_detected);

int cxd2880_tnrdmd_dvbt_mon_mode_guard(struct cxd2880_tnrdmd
				       *tnr_dmd,
				       enum cxd2880_dvbt_mode
				       *mode,
				       enum cxd2880_dvbt_guard
				       *guard);

int cxd2880_tnrdmd_dvbt_mon_carrier_offset(struct cxd2880_tnrdmd
					   *tnr_dmd, int *offset);

int cxd2880_tnrdmd_dvbt_mon_carrier_offset_sub(struct
					       cxd2880_tnrdmd
					       *tnr_dmd,
					       int *offset);

int cxd2880_tnrdmd_dvbt_mon_tps_info(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     struct cxd2880_dvbt_tpsinfo
				     *info);

int cxd2880_tnrdmd_dvbt_mon_packet_error_number(struct
						cxd2880_tnrdmd
						*tnr_dmd,
						u32 *pen);

int cxd2880_tnrdmd_dvbt_mon_spectrum_sense(struct cxd2880_tnrdmd
					   *tnr_dmd,
					   enum
					   cxd2880_tnrdmd_spectrum_sense
					   *sense);

int cxd2880_tnrdmd_dvbt_mon_snr(struct cxd2880_tnrdmd *tnr_dmd,
				int *snr);

int cxd2880_tnrdmd_dvbt_mon_snr_diver(struct cxd2880_tnrdmd
				      *tnr_dmd, int *snr,
				      int *snr_main, int *snr_sub);

int cxd2880_tnrdmd_dvbt_mon_sampling_offset(struct cxd2880_tnrdmd
					    *tnr_dmd, int *ppm);

int cxd2880_tnrdmd_dvbt_mon_sampling_offset_sub(struct
						cxd2880_tnrdmd
						*tnr_dmd,
						int *ppm);

int cxd2880_tnrdmd_dvbt_mon_ssi(struct cxd2880_tnrdmd *tnr_dmd,
				u8 *ssi);

int cxd2880_tnrdmd_dvbt_mon_ssi_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    u8 *ssi);

#endif
