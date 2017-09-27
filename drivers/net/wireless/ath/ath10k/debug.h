/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/types.h>
#include "trace.h"

enum ath10k_debug_mask {
	ATH10K_DBG_PCI		= 0x00000001,
	ATH10K_DBG_WMI		= 0x00000002,
	ATH10K_DBG_HTC		= 0x00000004,
	ATH10K_DBG_HTT		= 0x00000008,
	ATH10K_DBG_MAC		= 0x00000010,
	ATH10K_DBG_BOOT		= 0x00000020,
	ATH10K_DBG_PCI_DUMP	= 0x00000040,
	ATH10K_DBG_HTT_DUMP	= 0x00000080,
	ATH10K_DBG_MGMT		= 0x00000100,
	ATH10K_DBG_DATA		= 0x00000200,
	ATH10K_DBG_BMI		= 0x00000400,
	ATH10K_DBG_REGULATORY	= 0x00000800,
	ATH10K_DBG_TESTMODE	= 0x00001000,
	ATH10K_DBG_WMI_PRINT	= 0x00002000,
	ATH10K_DBG_PCI_PS	= 0x00004000,
	ATH10K_DBG_AHB		= 0x00008000,
	ATH10K_DBG_SDIO		= 0x00010000,
	ATH10K_DBG_SDIO_DUMP	= 0x00020000,
	ATH10K_DBG_USB		= 0x00040000,
	ATH10K_DBG_USB_BULK	= 0x00080000,
	ATH10K_DBG_ANY		= 0xffffffff,
};

enum ath10k_pktlog_filter {
	ATH10K_PKTLOG_RX         = 0x000000001,
	ATH10K_PKTLOG_TX         = 0x000000002,
	ATH10K_PKTLOG_RCFIND     = 0x000000004,
	ATH10K_PKTLOG_RCUPDATE   = 0x000000008,
	ATH10K_PKTLOG_DBG_PRINT  = 0x000000010,
	ATH10K_PKTLOG_ANY        = 0x00000001f,
};

enum ath10k_dbg_aggr_mode {
	ATH10K_DBG_AGGR_MODE_AUTO,
	ATH10K_DBG_AGGR_MODE_MANUAL,
	ATH10K_DBG_AGGR_MODE_MAX,
};

/* FIXME: How to calculate the buffer size sanely? */
#define ATH10K_FW_STATS_BUF_SIZE (1024 * 1024)

extern unsigned int ath10k_debug_mask;

__printf(2, 3) void ath10k_info(struct ath10k *ar, const char *fmt, ...);
__printf(2, 3) void ath10k_err(struct ath10k *ar, const char *fmt, ...);
__printf(2, 3) void ath10k_warn(struct ath10k *ar, const char *fmt, ...);

void ath10k_debug_print_hwfw_info(struct ath10k *ar);
void ath10k_debug_print_board_info(struct ath10k *ar);
void ath10k_debug_print_boot_info(struct ath10k *ar);
void ath10k_print_driver_info(struct ath10k *ar);

#ifdef CONFIG_ATH10K_DEBUGFS
int ath10k_debug_start(struct ath10k *ar);
void ath10k_debug_stop(struct ath10k *ar);
int ath10k_debug_create(struct ath10k *ar);
void ath10k_debug_destroy(struct ath10k *ar);
int ath10k_debug_register(struct ath10k *ar);
void ath10k_debug_unregister(struct ath10k *ar);
void ath10k_debug_fw_stats_process(struct ath10k *ar, struct sk_buff *skb);
void ath10k_debug_tpc_stats_process(struct ath10k *ar,
				    struct ath10k_tpc_stats *tpc_stats);
struct ath10k_fw_crash_data *
ath10k_debug_get_new_fw_crash_data(struct ath10k *ar);

void ath10k_debug_dbglog_add(struct ath10k *ar, u8 *buffer, int len);

int ath10k_debug_fw_devcoredump(struct ath10k *ar);

#define ATH10K_DFS_STAT_INC(ar, c) (ar->debug.dfs_stats.c++)

void ath10k_debug_get_et_strings(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 u32 sset, u8 *data);
int ath10k_debug_get_et_sset_count(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif, int sset);
void ath10k_debug_get_et_stats(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ethtool_stats *stats, u64 *data);

static inline u64 ath10k_debug_get_fw_dbglog_mask(struct ath10k *ar)
{
	return ar->debug.fw_dbglog_mask;
}

static inline u32 ath10k_debug_get_fw_dbglog_level(struct ath10k *ar)
{
	return ar->debug.fw_dbglog_level;
}

#else

static inline int ath10k_debug_start(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_debug_stop(struct ath10k *ar)
{
}

static inline int ath10k_debug_create(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_debug_destroy(struct ath10k *ar)
{
}

static inline int ath10k_debug_register(struct ath10k *ar)
{
	return 0;
}

static inline void ath10k_debug_unregister(struct ath10k *ar)
{
}

static inline void ath10k_debug_fw_stats_process(struct ath10k *ar,
						 struct sk_buff *skb)
{
}

static inline void ath10k_debug_tpc_stats_process(struct ath10k *ar,
						  struct ath10k_tpc_stats *tpc_stats)
{
	kfree(tpc_stats);
}

static inline void ath10k_debug_dbglog_add(struct ath10k *ar, u8 *buffer,
					   int len)
{
}

static inline struct ath10k_fw_crash_data *
ath10k_debug_get_new_fw_crash_data(struct ath10k *ar)
{
	return NULL;
}

static inline u64 ath10k_debug_get_fw_dbglog_mask(struct ath10k *ar)
{
	return 0;
}

static inline u32 ath10k_debug_get_fw_dbglog_level(struct ath10k *ar)
{
	return 0;
}

static inline int ath10k_debug_fw_devcoredump(struct ath10k *ar)
{
	return 0;
}

#define ATH10K_DFS_STAT_INC(ar, c) do { } while (0)

#define ath10k_debug_get_et_strings NULL
#define ath10k_debug_get_et_sset_count NULL
#define ath10k_debug_get_et_stats NULL

#endif /* CONFIG_ATH10K_DEBUGFS */
#ifdef CONFIG_MAC80211_DEBUGFS
void ath10k_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
void ath10k_sta_update_rx_duration(struct ath10k *ar,
				   struct ath10k_fw_stats *stats);
void ath10k_sta_statistics(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct station_info *sinfo);
#else
static inline
void ath10k_sta_update_rx_duration(struct ath10k *ar,
				   struct ath10k_fw_stats *stats)
{
}
#endif /* CONFIG_MAC80211_DEBUGFS */

#ifdef CONFIG_ATH10K_DEBUG
__printf(3, 4) void ath10k_dbg(struct ath10k *ar,
			       enum ath10k_debug_mask mask,
			       const char *fmt, ...);
void ath10k_dbg_dump(struct ath10k *ar,
		     enum ath10k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len);
#else /* CONFIG_ATH10K_DEBUG */

static inline int ath10k_dbg(struct ath10k *ar,
			     enum ath10k_debug_mask dbg_mask,
			     const char *fmt, ...)
{
	return 0;
}

static inline void ath10k_dbg_dump(struct ath10k *ar,
				   enum ath10k_debug_mask mask,
				   const char *msg, const char *prefix,
				   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH10K_DEBUG */
#endif /* _DEBUG_H_ */
