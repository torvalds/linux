/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd_dvbt2_mon.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * DVB-T2 monitor interface
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_DVBT2_MON_H
#define CXD2880_TNRDMD_DVBT2_MON_H

#include "cxd2880_tnrdmd.h"
#include "cxd2880_dvbt2.h"

int cxd2880_tnrdmd_dvbt2_mon_sync_stat(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *sync_stat,
				       u8 *ts_lock_stat,
				       u8 *unlock_detected);

int cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub(struct cxd2880_tnrdmd
					   *tnr_dmd,
					   u8 *sync_stat,
					   u8 *unlock_detected);

int cxd2880_tnrdmd_dvbt2_mon_carrier_offset(struct cxd2880_tnrdmd
					    *tnr_dmd, int *offset);

int cxd2880_tnrdmd_dvbt2_mon_carrier_offset_sub(struct
						cxd2880_tnrdmd
						*tnr_dmd,
						int *offset);

int cxd2880_tnrdmd_dvbt2_mon_l1_pre(struct cxd2880_tnrdmd *tnr_dmd,
				    struct cxd2880_dvbt2_l1pre
				    *l1_pre);

int cxd2880_tnrdmd_dvbt2_mon_version(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     enum cxd2880_dvbt2_version
				     *ver);

int cxd2880_tnrdmd_dvbt2_mon_ofdm(struct cxd2880_tnrdmd *tnr_dmd,
				  struct cxd2880_dvbt2_ofdm *ofdm);

int cxd2880_tnrdmd_dvbt2_mon_data_plps(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *plp_ids,
				       u8 *num_plps);

int cxd2880_tnrdmd_dvbt2_mon_active_plp(struct cxd2880_tnrdmd
					*tnr_dmd,
					enum
					cxd2880_dvbt2_plp_btype
					type,
					struct cxd2880_dvbt2_plp
					*plp_info);

int cxd2880_tnrdmd_dvbt2_mon_data_plp_error(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    u8 *plp_error);

int cxd2880_tnrdmd_dvbt2_mon_l1_change(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 *l1_change);

int cxd2880_tnrdmd_dvbt2_mon_l1_post(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     struct cxd2880_dvbt2_l1post
				     *l1_post);

int cxd2880_tnrdmd_dvbt2_mon_bbheader(struct cxd2880_tnrdmd
				      *tnr_dmd,
				      enum cxd2880_dvbt2_plp_btype
				      type,
				      struct cxd2880_dvbt2_bbheader
				      *bbheader);

int cxd2880_tnrdmd_dvbt2_mon_in_bandb_ts_rate(struct cxd2880_tnrdmd
					      *tnr_dmd,
					      enum
					      cxd2880_dvbt2_plp_btype
					      type,
					      u32 *ts_rate_bps);

int cxd2880_tnrdmd_dvbt2_mon_spectrum_sense(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    enum
					    cxd2880_tnrdmd_spectrum_sense
					    *sense);

int cxd2880_tnrdmd_dvbt2_mon_snr(struct cxd2880_tnrdmd *tnr_dmd,
				 int *snr);

int cxd2880_tnrdmd_dvbt2_mon_snr_diver(struct cxd2880_tnrdmd
				       *tnr_dmd, int *snr,
				       int *snr_main,
				       int *snr_sub);

int cxd2880_tnrdmd_dvbt2_mon_packet_error_number(struct
						 cxd2880_tnrdmd
						 *tnr_dmd,
						 u32 *pen);

int cxd2880_tnrdmd_dvbt2_mon_sampling_offset(struct cxd2880_tnrdmd
					     *tnr_dmd, int *ppm);

int cxd2880_tnrdmd_dvbt2_mon_sampling_offset_sub(struct
						 cxd2880_tnrdmd
						 *tnr_dmd,
						 int *ppm);

int cxd2880_tnrdmd_dvbt2_mon_qam(struct cxd2880_tnrdmd *tnr_dmd,
				 enum cxd2880_dvbt2_plp_btype type,
				 enum cxd2880_dvbt2_plp_constell
				 *qam);

int cxd2880_tnrdmd_dvbt2_mon_code_rate(struct cxd2880_tnrdmd
				       *tnr_dmd,
				       enum cxd2880_dvbt2_plp_btype
				       type,
				       enum
				       cxd2880_dvbt2_plp_code_rate
				       *code_rate);

int cxd2880_tnrdmd_dvbt2_mon_profile(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     enum cxd2880_dvbt2_profile
				     *profile);

int cxd2880_tnrdmd_dvbt2_mon_ssi(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 *ssi);

int cxd2880_tnrdmd_dvbt2_mon_ssi_sub(struct cxd2880_tnrdmd
				     *tnr_dmd, u8 *ssi);

#endif
