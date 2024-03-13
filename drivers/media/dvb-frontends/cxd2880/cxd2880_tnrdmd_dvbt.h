/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd_dvbt.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * control interface for DVB-T
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_DVBT_H
#define CXD2880_TNRDMD_DVBT_H

#include "cxd2880_common.h"
#include "cxd2880_tnrdmd.h"

struct cxd2880_dvbt_tune_param {
	u32 center_freq_khz;
	enum cxd2880_dtv_bandwidth bandwidth;
	enum cxd2880_dvbt_profile profile;
};

int cxd2880_tnrdmd_dvbt_tune1(struct cxd2880_tnrdmd *tnr_dmd,
			      struct cxd2880_dvbt_tune_param
			      *tune_param);

int cxd2880_tnrdmd_dvbt_tune2(struct cxd2880_tnrdmd *tnr_dmd,
			      struct cxd2880_dvbt_tune_param
			      *tune_param);

int cxd2880_tnrdmd_dvbt_sleep_setting(struct cxd2880_tnrdmd
				      *tnr_dmd);

int cxd2880_tnrdmd_dvbt_check_demod_lock(struct cxd2880_tnrdmd
					 *tnr_dmd,
					 enum
					 cxd2880_tnrdmd_lock_result
					 *lock);

int cxd2880_tnrdmd_dvbt_check_ts_lock(struct cxd2880_tnrdmd
				      *tnr_dmd,
				      enum
				      cxd2880_tnrdmd_lock_result
				      *lock);

#endif
