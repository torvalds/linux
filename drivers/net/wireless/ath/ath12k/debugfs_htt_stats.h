/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef DEBUG_HTT_STATS_H
#define DEBUG_HTT_STATS_H

void ath12k_debugfs_htt_stats_register(struct ath12k *ar);

/* htt_dbg_ext_stats_type */
enum ath12k_dbg_htt_ext_stats_type {
	ATH12K_DBG_HTT_EXT_STATS_RESET		= 0,

	/* keep this last */
	ATH12K_DBG_HTT_NUM_EXT_STATS,
};

#endif
