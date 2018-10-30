/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd_dvbt2.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * control interface for DVB-T2
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_DVBT2_H
#define CXD2880_TNRDMD_DVBT2_H

#include "cxd2880_common.h"
#include "cxd2880_tnrdmd.h"

enum cxd2880_tnrdmd_dvbt2_tune_info {
	CXD2880_TNRDMD_DVBT2_TUNE_INFO_OK,
	CXD2880_TNRDMD_DVBT2_TUNE_INFO_INVALID_PLP_ID
};

struct cxd2880_dvbt2_tune_param {
	u32 center_freq_khz;
	enum cxd2880_dtv_bandwidth bandwidth;
	u16 data_plp_id;
	enum cxd2880_dvbt2_profile profile;
	enum cxd2880_tnrdmd_dvbt2_tune_info tune_info;
};

#define CXD2880_DVBT2_TUNE_PARAM_PLPID_AUTO  0xffff

int cxd2880_tnrdmd_dvbt2_tune1(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_dvbt2_tune_param
			       *tune_param);

int cxd2880_tnrdmd_dvbt2_tune2(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_dvbt2_tune_param
			       *tune_param);

int cxd2880_tnrdmd_dvbt2_sleep_setting(struct cxd2880_tnrdmd
				       *tnr_dmd);

int cxd2880_tnrdmd_dvbt2_check_demod_lock(struct cxd2880_tnrdmd
					  *tnr_dmd,
					  enum
					  cxd2880_tnrdmd_lock_result
					  *lock);

int cxd2880_tnrdmd_dvbt2_check_ts_lock(struct cxd2880_tnrdmd
				       *tnr_dmd,
				       enum
				       cxd2880_tnrdmd_lock_result
				       *lock);

int cxd2880_tnrdmd_dvbt2_set_plp_cfg(struct cxd2880_tnrdmd
				     *tnr_dmd, u8 auto_plp,
				     u8 plp_id);

int cxd2880_tnrdmd_dvbt2_diver_fef_setting(struct cxd2880_tnrdmd
					   *tnr_dmd);

int cxd2880_tnrdmd_dvbt2_check_l1post_valid(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    u8 *l1_post_valid);

#endif
