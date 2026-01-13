/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_TX_WIFI7_H
#define ATH12K_DP_TX_WIFI7_H

int ath12k_wifi7_dp_tx(struct ath12k_pdev_dp *dp_pdev, struct ath12k_link_vif *arvif,
		       struct sk_buff *skb, bool gsn_valid, int mcbc_gsn,
		       bool is_mcast);
void ath12k_wifi7_dp_tx_completion_handler(struct ath12k_dp *dp, int ring_id);
u32 ath12k_wifi7_dp_tx_get_vdev_bank_config(struct ath12k_base *ab,
					    struct ath12k_link_vif *arvif);
#endif
