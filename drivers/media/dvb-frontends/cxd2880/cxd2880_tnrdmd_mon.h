/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd_mon.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * common monitor interface
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_MON_H
#define CXD2880_TNRDMD_MON_H

#include "cxd2880_common.h"
#include "cxd2880_tnrdmd.h"

int cxd2880_tnrdmd_mon_rf_lvl(struct cxd2880_tnrdmd *tnr_dmd,
			      int *rf_lvl_db);

int cxd2880_tnrdmd_mon_rf_lvl_sub(struct cxd2880_tnrdmd *tnr_dmd,
				  int *rf_lvl_db);

int cxd2880_tnrdmd_mon_internal_cpu_status(struct cxd2880_tnrdmd
					   *tnr_dmd, u16 *status);

int cxd2880_tnrdmd_mon_internal_cpu_status_sub(struct
					       cxd2880_tnrdmd
					       *tnr_dmd,
					       u16 *status);
#endif
