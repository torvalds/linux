/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef _ATH11K_DEBUGFS_H_
#define _ATH11K_DEBUGFS_H_

#include "hal_tx.h"

#define ATH11K_TX_POWER_MAX_VAL	70
#define ATH11K_TX_POWER_MIN_VAL	0

/* htt_dbg_ext_stats_type */
enum ath11k_dbg_htt_ext_stats_type {
	ATH11K_DBG_HTT_EXT_STATS_RESET                      =  0,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX                    =  1,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_RX                    =  2,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_HWQ                =  3,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_SCHED              =  4,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_ERROR                 =  5,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TQM                   =  6,
	ATH11K_DBG_HTT_EXT_STATS_TQM_CMDQ                   =  7,
	ATH11K_DBG_HTT_EXT_STATS_TX_DE_INFO                 =  8,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_RATE               =  9,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_RX_RATE               =  10,
	ATH11K_DBG_HTT_EXT_STATS_PEER_INFO                  =  11,
	ATH11K_DBG_HTT_EXT_STATS_TX_SELFGEN_INFO            =  12,
	ATH11K_DBG_HTT_EXT_STATS_TX_MU_HWQ                  =  13,
	ATH11K_DBG_HTT_EXT_STATS_RING_IF_INFO               =  14,
	ATH11K_DBG_HTT_EXT_STATS_SRNG_INFO                  =  15,
	ATH11K_DBG_HTT_EXT_STATS_SFM_INFO                   =  16,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_MU                 =  17,
	ATH11K_DBG_HTT_EXT_STATS_ACTIVE_PEERS_LIST          =  18,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_CCA_STATS             =  19,
	ATH11K_DBG_HTT_EXT_STATS_TWT_SESSIONS               =  20,
	ATH11K_DBG_HTT_EXT_STATS_REO_RESOURCE_STATS         =  21,
	ATH11K_DBG_HTT_EXT_STATS_TX_SOUNDING_INFO           =  22,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_OBSS_PD_STATS	    =  23,
	ATH11K_DBG_HTT_EXT_STATS_RING_BACKPRESSURE_STATS    =  24,
	ATH11K_DBG_HTT_EXT_STATS_PEER_CTRL_PATH_TXRX_STATS  =  29,
	ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_RATE_TXBF_STATS    =  31,
	ATH11K_DBG_HTT_EXT_STATS_TXBF_OFDMA		    =  32,
	ATH11K_DBG_HTT_EXT_PHY_COUNTERS_AND_PHY_STATS	    =  37,

	/* keep this last */
	ATH11K_DBG_HTT_NUM_EXT_STATS,
};

struct debug_htt_stats_req {
	bool done;
	u8 pdev_id;
	u8 type;
	u8 peer_addr[ETH_ALEN];
	struct completion cmpln;
	u32 buf_len;
	u8 buf[];
};

struct ath_pktlog_hdr {
	u16 flags;
	u16 missed_cnt;
	u16 log_type;
	u16 size;
	u32 timestamp;
	u32 type_specific_data;
	u8 payload[];
};

#define ATH11K_HTT_PEER_STATS_RESET BIT(16)

#define ATH11K_HTT_STATS_BUF_SIZE (1024 * 512)
#define ATH11K_FW_STATS_BUF_SIZE (1024 * 1024)

enum ath11k_pktlog_filter {
	ATH11K_PKTLOG_RX		= 0x000000001,
	ATH11K_PKTLOG_TX		= 0x000000002,
	ATH11K_PKTLOG_RCFIND		= 0x000000004,
	ATH11K_PKTLOG_RCUPDATE		= 0x000000008,
	ATH11K_PKTLOG_EVENT_SMART_ANT	= 0x000000020,
	ATH11K_PKTLOG_EVENT_SW		= 0x000000040,
	ATH11K_PKTLOG_ANY		= 0x00000006f,
};

enum ath11k_pktlog_mode {
	ATH11K_PKTLOG_MODE_LITE = 1,
	ATH11K_PKTLOG_MODE_FULL = 2,
};

enum ath11k_pktlog_enum {
	ATH11K_PKTLOG_TYPE_TX_CTRL      = 1,
	ATH11K_PKTLOG_TYPE_TX_STAT      = 2,
	ATH11K_PKTLOG_TYPE_TX_MSDU_ID   = 3,
	ATH11K_PKTLOG_TYPE_RX_STAT      = 5,
	ATH11K_PKTLOG_TYPE_RC_FIND      = 6,
	ATH11K_PKTLOG_TYPE_RC_UPDATE    = 7,
	ATH11K_PKTLOG_TYPE_TX_VIRT_ADDR = 8,
	ATH11K_PKTLOG_TYPE_RX_CBF       = 10,
	ATH11K_PKTLOG_TYPE_RX_STATBUF   = 22,
	ATH11K_PKTLOG_TYPE_PPDU_STATS   = 23,
	ATH11K_PKTLOG_TYPE_LITE_RX      = 24,
};

enum ath11k_dbg_aggr_mode {
	ATH11K_DBG_AGGR_MODE_AUTO,
	ATH11K_DBG_AGGR_MODE_MANUAL,
	ATH11K_DBG_AGGR_MODE_MAX,
};

#ifdef CONFIG_ATH11K_DEBUGFS
int ath11k_debugfs_soc_create(struct ath11k_base *ab);
void ath11k_debugfs_soc_destroy(struct ath11k_base *ab);
int ath11k_debugfs_pdev_create(struct ath11k_base *ab);
void ath11k_debugfs_pdev_destroy(struct ath11k_base *ab);
int ath11k_debugfs_register(struct ath11k *ar);
void ath11k_debugfs_unregister(struct ath11k *ar);
void ath11k_debugfs_fw_stats_process(struct ath11k_base *ab, struct sk_buff *skb);

void ath11k_debugfs_fw_stats_init(struct ath11k *ar);
int ath11k_debugfs_get_fw_stats(struct ath11k *ar, u32 pdev_id,
				u32 vdev_id, u32 stats_id);

static inline bool ath11k_debugfs_is_pktlog_lite_mode_enabled(struct ath11k *ar)
{
	return (ar->debug.pktlog_mode == ATH11K_PKTLOG_MODE_LITE);
}

static inline bool ath11k_debugfs_is_pktlog_rx_stats_enabled(struct ath11k *ar)
{
	return (!ar->debug.pktlog_peer_valid && ar->debug.pktlog_mode);
}

static inline bool ath11k_debugfs_is_pktlog_peer_valid(struct ath11k *ar, u8 *addr)
{
	return (ar->debug.pktlog_peer_valid && ar->debug.pktlog_mode &&
		ether_addr_equal(addr, ar->debug.pktlog_peer_addr));
}

static inline int ath11k_debugfs_is_extd_tx_stats_enabled(struct ath11k *ar)
{
	return ar->debug.extd_tx_stats;
}

static inline int ath11k_debugfs_is_extd_rx_stats_enabled(struct ath11k *ar)
{
	return ar->debug.extd_rx_stats;
}

static inline int ath11k_debugfs_rx_filter(struct ath11k *ar)
{
	return ar->debug.rx_filter;
}

#else
static inline int ath11k_debugfs_soc_create(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_debugfs_soc_destroy(struct ath11k_base *ab)
{
}

static inline int ath11k_debugfs_pdev_create(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_debugfs_pdev_destroy(struct ath11k_base *ab)
{
}

static inline int ath11k_debugfs_register(struct ath11k *ar)
{
	return 0;
}

static inline void ath11k_debugfs_unregister(struct ath11k *ar)
{
}

static inline void ath11k_debugfs_fw_stats_process(struct ath11k_base *ab,
						   struct sk_buff *skb)
{
}

static inline void ath11k_debugfs_fw_stats_init(struct ath11k *ar)
{
}

static inline int ath11k_debugfs_is_extd_tx_stats_enabled(struct ath11k *ar)
{
	return 0;
}

static inline int ath11k_debugfs_is_extd_rx_stats_enabled(struct ath11k *ar)
{
	return 0;
}

static inline bool ath11k_debugfs_is_pktlog_lite_mode_enabled(struct ath11k *ar)
{
	return false;
}

static inline bool ath11k_debugfs_is_pktlog_rx_stats_enabled(struct ath11k *ar)
{
	return false;
}

static inline bool ath11k_debugfs_is_pktlog_peer_valid(struct ath11k *ar, u8 *addr)
{
	return false;
}

static inline int ath11k_debugfs_rx_filter(struct ath11k *ar)
{
	return 0;
}

static inline int ath11k_debugfs_get_fw_stats(struct ath11k *ar,
					      u32 pdev_id, u32 vdev_id, u32 stats_id)
{
	return 0;
}

#endif /* CONFIG_MAC80211_DEBUGFS*/

#endif /* _ATH11K_DEBUGFS_H_ */
