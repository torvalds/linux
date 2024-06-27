/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef DEBUG_HTT_STATS_H
#define DEBUG_HTT_STATS_H

#define ATH12K_HTT_STATS_BUF_SIZE		(1024 * 512)
#define ATH12K_HTT_STATS_COOKIE_LSB		GENMASK_ULL(31, 0)
#define ATH12K_HTT_STATS_COOKIE_MSB		GENMASK_ULL(63, 32)
#define ATH12K_HTT_STATS_MAGIC_VALUE		0xF0F0F0F0

#define ATH12K_HTT_STATS_RESET_BITMAP32_OFFSET(_idx)	((_idx) & 0x1f)
#define ATH12K_HTT_STATS_RESET_BITMAP64_OFFSET(_idx)	((_idx) & 0x3f)
#define ATH12K_HTT_STATS_RESET_BITMAP32_BIT(_idx)	(1 << \
		ATH12K_HTT_STATS_RESET_BITMAP32_OFFSET(_idx))
#define ATH12K_HTT_STATS_RESET_BITMAP64_BIT(_idx)	(1 << \
		ATH12K_HTT_STATS_RESET_BITMAP64_OFFSET(_idx))

void ath12k_debugfs_htt_stats_register(struct ath12k *ar);

/* htt_dbg_ext_stats_type */
enum ath12k_dbg_htt_ext_stats_type {
	ATH12K_DBG_HTT_EXT_STATS_RESET		= 0,

	/* keep this last */
	ATH12K_DBG_HTT_NUM_EXT_STATS,
};

enum ath12k_htt_stats_reset_cfg_param_alloc_pos {
	ATH12K_HTT_STATS_RESET_PARAM_CFG_32_BYTES = 1,
	ATH12K_HTT_STATS_RESET_PARAM_CFG_64_BYTES,
	ATH12K_HTT_STATS_RESET_PARAM_CFG_128_BYTES,
};

struct debug_htt_stats_req {
	bool done;
	bool override_cfg_param;
	u8 pdev_id;
	enum ath12k_dbg_htt_ext_stats_type type;
	u32 cfg_param[4];
	u8 peer_addr[ETH_ALEN];
	struct completion htt_stats_rcvd;
	u32 buf_len;
	u8 buf[];
};

#endif
