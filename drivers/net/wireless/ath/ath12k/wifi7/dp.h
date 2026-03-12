/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_WIFI7_H
#define ATH12K_DP_WIFI7_H

#include "../dp_cmn.h"
#include "hw.h"

struct ath12k_base;
struct ath12k_dp;
enum dp_monitor_mode;

struct ath12k_dp *ath12k_wifi7_dp_device_alloc(struct ath12k_base *ab);
void ath12k_wifi7_dp_device_free(struct ath12k_dp *dp);

#endif
