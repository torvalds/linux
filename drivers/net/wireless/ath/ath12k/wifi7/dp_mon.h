/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_MON_WIFI7_H
#define ATH12K_DP_MON_WIFI7_H

#include "hw.h"

enum dp_monitor_mode;

int ath12k_wifi7_dp_mon_process_ring(struct ath12k_dp *dp, int mac_id,
				     struct napi_struct *napi, int budget,
				     enum dp_monitor_mode monitor_mode);
enum hal_rx_mon_status
ath12k_wifi7_dp_mon_tx_parse_mon_status(struct ath12k_pdev_dp *dp_pdev,
					struct ath12k_mon_data *pmon,
					struct sk_buff *skb,
					struct napi_struct *napi,
					u32 ppdu_id);
#endif
