/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_WIFI7_H
#define ATH12K_DP_WIFI7_H

#include "hw.h"

int ath12k_wifi7_dp_service_srng(struct ath12k_base *ab,
				 struct ath12k_ext_irq_grp *irq_grp, int budget);
#endif
