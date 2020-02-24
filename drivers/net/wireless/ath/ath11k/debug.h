/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef _ATH11K_DEBUG_H_
#define _ATH11K_DEBUG_H_

#include "hal_tx.h"
#include "trace.h"

#define ATH11K_TX_POWER_MAX_VAL	70
#define ATH11K_TX_POWER_MIN_VAL	0

enum ath11k_debug_mask {
	ATH11K_DBG_AHB		= 0x00000001,
	ATH11K_DBG_WMI		= 0x00000002,
	ATH11K_DBG_HTC		= 0x00000004,
	ATH11K_DBG_DP_HTT	= 0x00000008,
	ATH11K_DBG_MAC		= 0x00000010,
	ATH11K_DBG_BOOT		= 0x00000020,
	ATH11K_DBG_QMI		= 0x00000040,
	ATH11K_DBG_DATA		= 0x00000080,
	ATH11K_DBG_MGMT		= 0x00000100,
	ATH11K_DBG_REG		= 0x00000200,
	ATH11K_DBG_TESTMODE	= 0x00000400,
	ATH11k_DBG_HAL		= 0x00000800,
	ATH11K_DBG_ANY		= 0xffffffff,
};

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
	u8 buf[0];
};

#define ATH11K_HTT_STATS_BUF_SIZE (1024 * 512)

#define ATH11K_FW_STATS_BUF_SIZE (1024 * 1024)

#define ATH11K_HTT_PKTLOG_MAX_SIZE 2048

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

__printf(2, 3) void ath11k_info(struct ath11k_base *ab, const char *fmt, ...);
__printf(2, 3) void ath11k_err(struct ath11k_base *ab, const char *fmt, ...);
__printf(2, 3) void ath11k_warn(struct ath11k_base *ab, const char *fmt, ...);

extern unsigned int ath11k_debug_mask;

#ifdef CONFIG_ATH11K_DEBUG
__printf(3, 4) void __ath11k_dbg(struct ath11k_base *ab,
				 enum ath11k_debug_mask mask,
				 const char *fmt, ...);
void ath11k_dbg_dump(struct ath11k_base *ab,
		     enum ath11k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);
#else /* CONFIG_ATH11K_DEBUG */
static inline int __ath11k_dbg(struct ath11k_base *ab,
			       enum ath11k_debug_mask dbg_mask,
			       const char *fmt, ...)
{
	return 0;
}

static inline void ath11k_dbg_dump(struct ath11k_base *ab,
				   enum ath11k_debug_mask mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH11K_DEBUG */

#ifdef CONFIG_ATH11K_DEBUGFS
int ath11k_debug_soc_create(struct ath11k_base *ab);
void ath11k_debug_soc_destroy(struct ath11k_base *ab);
int ath11k_debug_pdev_create(struct ath11k_base *ab);
void ath11k_debug_pdev_destroy(struct ath11k_base *ab);
int ath11k_debug_register(struct ath11k *ar);
void ath11k_debug_unregister(struct ath11k *ar);
void ath11k_dbg_htt_ext_stats_handler(struct ath11k_base *ab,
				      struct sk_buff *skb);
void ath11k_debug_fw_stats_process(struct ath11k_base *ab, struct sk_buff *skb);

void ath11k_debug_fw_stats_init(struct ath11k *ar);
int ath11k_dbg_htt_stats_req(struct ath11k *ar);

static inline bool ath11k_debug_is_pktlog_lite_mode_enabled(struct ath11k *ar)
{
	return (ar->debug.pktlog_mode == ATH11K_PKTLOG_MODE_LITE);
}

static inline bool ath11k_debug_is_pktlog_rx_stats_enabled(struct ath11k *ar)
{
	return (!ar->debug.pktlog_peer_valid && ar->debug.pktlog_mode);
}

static inline bool ath11k_debug_is_pktlog_peer_valid(struct ath11k *ar, u8 *addr)
{
	return (ar->debug.pktlog_peer_valid && ar->debug.pktlog_mode &&
		ether_addr_equal(addr, ar->debug.pktlog_peer_addr));
}

static inline int ath11k_debug_is_extd_tx_stats_enabled(struct ath11k *ar)
{
	return ar->debug.extd_tx_stats;
}

static inline int ath11k_debug_is_extd_rx_stats_enabled(struct ath11k *ar)
{
	return ar->debug.extd_rx_stats;
}

void ath11k_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
void
ath11k_accumulate_per_peer_tx_stats(struct ath11k_sta *arsta,
				    struct ath11k_per_peer_tx_stats *peer_stats,
				    u8 legacy_rate_idx);
void ath11k_update_per_peer_stats_from_txcompl(struct ath11k *ar,
					       struct sk_buff *msdu,
					       struct hal_tx_status *ts);
#else
static inline int ath11k_debug_soc_create(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_debug_soc_destroy(struct ath11k_base *ab)
{
}

static inline int ath11k_debug_pdev_create(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_debug_pdev_destroy(struct ath11k_base *ab)
{
}

static inline int ath11k_debug_register(struct ath11k *ar)
{
	return 0;
}

static inline void ath11k_debug_unregister(struct ath11k *ar)
{
}

static inline void ath11k_dbg_htt_ext_stats_handler(struct ath11k_base *ab,
						    struct sk_buff *skb)
{
}

static inline void ath11k_debug_fw_stats_process(struct ath11k_base *ab,
						 struct sk_buff *skb)
{
}

static inline void ath11k_debug_fw_stats_init(struct ath11k *ar)
{
}

static inline int ath11k_debug_is_extd_tx_stats_enabled(struct ath11k *ar)
{
	return 0;
}

static inline int ath11k_debug_is_extd_rx_stats_enabled(struct ath11k *ar)
{
	return 0;
}

static inline int ath11k_dbg_htt_stats_req(struct ath11k *ar)
{
	return 0;
}

static inline bool ath11k_debug_is_pktlog_lite_mode_enabled(struct ath11k *ar)
{
	return false;
}

static inline bool ath11k_debug_is_pktlog_rx_stats_enabled(struct ath11k *ar)
{
	return false;
}

static inline bool ath11k_debug_is_pktlog_peer_valid(struct ath11k *ar, u8 *addr)
{
	return false;
}

static inline void
ath11k_accumulate_per_peer_tx_stats(struct ath11k_sta *arsta,
				    struct ath11k_per_peer_tx_stats *peer_stats,
				    u8 legacy_rate_idx)
{
}

static inline void
ath11k_update_per_peer_stats_from_txcompl(struct ath11k *ar,
					  struct sk_buff *msdu,
					  struct hal_tx_status *ts)
{
}

#endif /* CONFIG_MAC80211_DEBUGFS*/

#define ath11k_dbg(ar, dbg_mask, fmt, ...)			\
do {								\
	if (ath11k_debug_mask & dbg_mask)			\
		__ath11k_dbg(ar, dbg_mask, fmt, ##__VA_ARGS__);	\
} while (0)

#endif /* _ATH11K_DEBUG_H_ */
