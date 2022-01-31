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
	ATH11K_PKTLOG_TYPE_INVALID      = 0,
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

enum fw_dbglog_wlan_module_id {
	WLAN_MODULE_ID_MIN = 0,
	WLAN_MODULE_INF = WLAN_MODULE_ID_MIN,
	WLAN_MODULE_WMI,
	WLAN_MODULE_STA_PWRSAVE,
	WLAN_MODULE_WHAL,
	WLAN_MODULE_COEX,
	WLAN_MODULE_ROAM,
	WLAN_MODULE_RESMGR_CHAN_MANAGER,
	WLAN_MODULE_RESMGR,
	WLAN_MODULE_VDEV_MGR,
	WLAN_MODULE_SCAN,
	WLAN_MODULE_RATECTRL,
	WLAN_MODULE_AP_PWRSAVE,
	WLAN_MODULE_BLOCKACK,
	WLAN_MODULE_MGMT_TXRX,
	WLAN_MODULE_DATA_TXRX,
	WLAN_MODULE_HTT,
	WLAN_MODULE_HOST,
	WLAN_MODULE_BEACON,
	WLAN_MODULE_OFFLOAD,
	WLAN_MODULE_WAL,
	WLAN_WAL_MODULE_DE,
	WLAN_MODULE_PCIELP,
	WLAN_MODULE_RTT,
	WLAN_MODULE_RESOURCE,
	WLAN_MODULE_DCS,
	WLAN_MODULE_CACHEMGR,
	WLAN_MODULE_ANI,
	WLAN_MODULE_P2P,
	WLAN_MODULE_CSA,
	WLAN_MODULE_NLO,
	WLAN_MODULE_CHATTER,
	WLAN_MODULE_WOW,
	WLAN_MODULE_WAL_VDEV,
	WLAN_MODULE_WAL_PDEV,
	WLAN_MODULE_TEST,
	WLAN_MODULE_STA_SMPS,
	WLAN_MODULE_SWBMISS,
	WLAN_MODULE_WMMAC,
	WLAN_MODULE_TDLS,
	WLAN_MODULE_HB,
	WLAN_MODULE_TXBF,
	WLAN_MODULE_BATCH_SCAN,
	WLAN_MODULE_THERMAL_MGR,
	WLAN_MODULE_PHYERR_DFS,
	WLAN_MODULE_RMC,
	WLAN_MODULE_STATS,
	WLAN_MODULE_NAN,
	WLAN_MODULE_IBSS_PWRSAVE,
	WLAN_MODULE_HIF_UART,
	WLAN_MODULE_LPI,
	WLAN_MODULE_EXTSCAN,
	WLAN_MODULE_UNIT_TEST,
	WLAN_MODULE_MLME,
	WLAN_MODULE_SUPPL,
	WLAN_MODULE_ERE,
	WLAN_MODULE_OCB,
	WLAN_MODULE_RSSI_MONITOR,
	WLAN_MODULE_WPM,
	WLAN_MODULE_CSS,
	WLAN_MODULE_PPS,
	WLAN_MODULE_SCAN_CH_PREDICT,
	WLAN_MODULE_MAWC,
	WLAN_MODULE_CMC_QMIC,
	WLAN_MODULE_EGAP,
	WLAN_MODULE_NAN20,
	WLAN_MODULE_QBOOST,
	WLAN_MODULE_P2P_LISTEN_OFFLOAD,
	WLAN_MODULE_HALPHY,
	WLAN_WAL_MODULE_ENQ,
	WLAN_MODULE_GNSS,
	WLAN_MODULE_WAL_MEM,
	WLAN_MODULE_SCHED_ALGO,
	WLAN_MODULE_TX,
	WLAN_MODULE_RX,
	WLAN_MODULE_WLM,
	WLAN_MODULE_RU_ALLOCATOR,
	WLAN_MODULE_11K_OFFLOAD,
	WLAN_MODULE_STA_TWT,
	WLAN_MODULE_AP_TWT,
	WLAN_MODULE_UL_OFDMA,
	WLAN_MODULE_HPCS_PULSE,
	WLAN_MODULE_DTF,
	WLAN_MODULE_QUIET_IE,
	WLAN_MODULE_SHMEM_MGR,
	WLAN_MODULE_CFIR,
	WLAN_MODULE_CODE_COVER,
	WLAN_MODULE_SHO,
	WLAN_MODULE_MLO_MGR,
	WLAN_MODULE_PEER_INIT,
	WLAN_MODULE_STA_MLO_PS,

	WLAN_MODULE_ID_MAX,
	WLAN_MODULE_ID_INVALID = WLAN_MODULE_ID_MAX,
};

enum fw_dbglog_log_level {
	ATH11K_FW_DBGLOG_ML = 0,
	ATH11K_FW_DBGLOG_VERBOSE = 0,
	ATH11K_FW_DBGLOG_INFO,
	ATH11K_FW_DBGLOG_INFO_LVL_1,
	ATH11K_FW_DBGLOG_INFO_LVL_2,
	ATH11K_FW_DBGLOG_WARN,
	ATH11K_FW_DBGLOG_ERR,
	ATH11K_FW_DBGLOG_LVL_MAX
};

struct ath11k_fw_dbglog {
	enum wmi_debug_log_param param;
	union {
		struct {
			/* log_level values are given in enum fw_dbglog_log_level */
			u16 log_level;
			/* module_id values are given in  enum fw_dbglog_wlan_module_id */
			u16 module_id;
		};
		/* value is either log_level&module_id/vdev_id/vdev_id_bitmap/log_level
		 * according to param
		 */
		u32 value;
	};
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

int ath11k_debugfs_add_interface(struct ath11k_vif *arvif);
void ath11k_debugfs_remove_interface(struct ath11k_vif *arvif);

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

static inline int ath11k_debugfs_add_interface(struct ath11k_vif *arvif)
{
	return 0;
}

static inline void ath11k_debugfs_remove_interface(struct ath11k_vif *arvif)
{
}

#endif /* CONFIG_ATH11K_DEBUGFS*/

#endif /* _ATH11K_DEBUGFS_H_ */
