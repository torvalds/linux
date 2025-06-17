/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef DEBUG_HTT_STATS_H
#define DEBUG_HTT_STATS_H

#define ATH12K_HTT_STATS_BUF_SIZE		(1024 * 512)
#define ATH12K_HTT_STATS_COOKIE_LSB		GENMASK_ULL(31, 0)
#define ATH12K_HTT_STATS_COOKIE_MSB		GENMASK_ULL(63, 32)
#define ATH12K_HTT_STATS_MAGIC_VALUE		0xF0F0F0F0
#define ATH12K_HTT_STATS_SUBTYPE_MAX		16
#define ATH12K_HTT_MAX_STRING_LEN		256

#define ATH12K_HTT_STATS_RESET_BITMAP32_OFFSET(_idx)	((_idx) & 0x1f)
#define ATH12K_HTT_STATS_RESET_BITMAP64_OFFSET(_idx)	((_idx) & 0x3f)
#define ATH12K_HTT_STATS_RESET_BITMAP32_BIT(_idx)	(1 << \
		ATH12K_HTT_STATS_RESET_BITMAP32_OFFSET(_idx))
#define ATH12K_HTT_STATS_RESET_BITMAP64_BIT(_idx)	(1 << \
		ATH12K_HTT_STATS_RESET_BITMAP64_OFFSET(_idx))

void ath12k_debugfs_htt_stats_register(struct ath12k *ar);

#ifdef CONFIG_ATH12K_DEBUGFS
void ath12k_debugfs_htt_ext_stats_handler(struct ath12k_base *ab,
					  struct sk_buff *skb);
#else /* CONFIG_ATH12K_DEBUGFS */
static inline void ath12k_debugfs_htt_ext_stats_handler(struct ath12k_base *ab,
							struct sk_buff *skb)
{
}
#endif

/**
 * DOC: target -> host extended statistics upload
 *
 * The following field definitions describe the format of the HTT
 * target to host stats upload confirmation message.
 * The message contains a cookie echoed from the HTT host->target stats
 * upload request, which identifies which request the confirmation is
 * for, and a single stats can span over multiple HTT stats indication
 * due to the HTT message size limitation so every HTT ext stats
 * indication will have tag-length-value stats information elements.
 * The tag-length header for each HTT stats IND message also includes a
 * status field, to indicate whether the request for the stat type in
 * question was fully met, partially met, unable to be met, or invalid
 * (if the stat type in question is disabled in the target).
 * A Done bit 1's indicate the end of the of stats info elements.
 *
 *
 * |31                         16|15    12|11|10 8|7   5|4       0|
 * |--------------------------------------------------------------|
 * |                   reserved                   |    msg type   |
 * |--------------------------------------------------------------|
 * |                         cookie LSBs                          |
 * |--------------------------------------------------------------|
 * |                         cookie MSBs                          |
 * |--------------------------------------------------------------|
 * |      stats entry length     | rsvd   | D|  S |   stat type   |
 * |--------------------------------------------------------------|
 * |                   type-specific stats info                   |
 * |                      (see debugfs_htt_stats.h)               |
 * |--------------------------------------------------------------|
 * Header fields:
 *  - MSG_TYPE
 *    Bits 7:0
 *    Purpose: Identifies this is a extended statistics upload confirmation
 *             message.
 *    Value: 0x1c
 *  - COOKIE_LSBS
 *    Bits 31:0
 *    Purpose: Provide a mechanism to match a target->host stats confirmation
 *        message with its preceding host->target stats request message.
 *    Value: MSBs of the opaque cookie specified by the host-side requestor
 *  - COOKIE_MSBS
 *    Bits 31:0
 *    Purpose: Provide a mechanism to match a target->host stats confirmation
 *        message with its preceding host->target stats request message.
 *    Value: MSBs of the opaque cookie specified by the host-side requestor
 *
 * Stats Information Element tag-length header fields:
 *  - STAT_TYPE
 *    Bits 7:0
 *    Purpose: identifies the type of statistics info held in the
 *        following information element
 *    Value: ath12k_dbg_htt_ext_stats_type
 *  - STATUS
 *    Bits 10:8
 *    Purpose: indicate whether the requested stats are present
 *    Value:
 *       0 -> The requested stats have been delivered in full
 *       1 -> The requested stats have been delivered in part
 *       2 -> The requested stats could not be delivered (error case)
 *       3 -> The requested stat type is either not recognized (invalid)
 *  - DONE
 *    Bits 11
 *    Purpose:
 *        Indicates the completion of the stats entry, this will be the last
 *        stats conf HTT segment for the requested stats type.
 *    Value:
 *        0 -> the stats retrieval is ongoing
 *        1 -> the stats retrieval is complete
 *  - LENGTH
 *    Bits 31:16
 *    Purpose: indicate the stats information size
 *    Value: This field specifies the number of bytes of stats information
 *       that follows the element tag-length header.
 *       It is expected but not required that this length is a multiple of
 *       4 bytes.
 */

#define ATH12K_HTT_T2H_EXT_STATS_INFO1_DONE		BIT(11)
#define ATH12K_HTT_T2H_EXT_STATS_INFO1_LENGTH		GENMASK(31, 16)

struct ath12k_htt_extd_stats_msg {
	__le32 info0;
	__le64 cookie;
	__le32 info1;
	u8 data[];
} __packed;

/* htt_dbg_ext_stats_type */
enum ath12k_dbg_htt_ext_stats_type {
	ATH12K_DBG_HTT_EXT_STATS_RESET				= 0,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TX			= 1,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TX_SCHED			= 4,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_ERROR			= 5,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TQM			= 6,
	ATH12K_DBG_HTT_EXT_STATS_TX_DE_INFO			= 8,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TX_RATE			= 9,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_RX_RATE			= 10,
	ATH12K_DBG_HTT_EXT_STATS_TX_SELFGEN_INFO		= 12,
	ATH12K_DBG_HTT_EXT_STATS_SRNG_INFO			= 15,
	ATH12K_DBG_HTT_EXT_STATS_SFM_INFO			= 16,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TX_MU			= 17,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_CCA_STATS			= 19,
	ATH12K_DBG_HTT_EXT_STATS_TX_SOUNDING_INFO		= 22,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_OBSS_PD_STATS		= 23,
	ATH12K_DBG_HTT_EXT_STATS_LATENCY_PROF_STATS		= 25,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_UL_TRIG_STATS		= 26,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_UL_MUMIMO_TRIG_STATS	= 27,
	ATH12K_DBG_HTT_EXT_STATS_FSE_RX				= 28,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_RX_RATE_EXT		= 30,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_TX_RATE_TXBF		= 31,
	ATH12K_DBG_HTT_EXT_STATS_TXBF_OFDMA			= 32,
	ATH12K_DBG_HTT_EXT_STATS_DLPAGER_STATS			= 36,
	ATH12K_DBG_HTT_EXT_PHY_COUNTERS_AND_PHY_STATS		= 37,
	ATH12K_DBG_HTT_EXT_VDEVS_TXRX_STATS			= 38,
	ATH12K_DBG_HTT_EXT_PDEV_PER_STATS			= 40,
	ATH12K_DBG_HTT_EXT_AST_ENTRIES				= 41,
	ATH12K_DBG_HTT_EXT_STATS_SOC_ERROR			= 45,
	ATH12K_DBG_HTT_DBG_PDEV_PUNCTURE_STATS			= 46,
	ATH12K_DBG_HTT_EXT_STATS_PDEV_SCHED_ALGO		= 49,
	ATH12K_DBG_HTT_EXT_STATS_MANDATORY_MUOFDMA		= 51,
	ATH12K_DGB_HTT_EXT_STATS_PDEV_MBSSID_CTRL_FRAME		= 54,

	/* keep this last */
	ATH12K_DBG_HTT_NUM_EXT_STATS,
};

enum ath12k_dbg_htt_tlv_tag {
	HTT_STATS_TX_PDEV_CMN_TAG			= 0,
	HTT_STATS_TX_PDEV_UNDERRUN_TAG			= 1,
	HTT_STATS_TX_PDEV_SIFS_TAG			= 2,
	HTT_STATS_TX_PDEV_FLUSH_TAG			= 3,
	HTT_STATS_STRING_TAG				= 5,
	HTT_STATS_TX_TQM_GEN_MPDU_TAG			= 11,
	HTT_STATS_TX_TQM_LIST_MPDU_TAG			= 12,
	HTT_STATS_TX_TQM_LIST_MPDU_CNT_TAG		= 13,
	HTT_STATS_TX_TQM_CMN_TAG			= 14,
	HTT_STATS_TX_TQM_PDEV_TAG			= 15,
	HTT_STATS_TX_DE_EAPOL_PACKETS_TAG		= 17,
	HTT_STATS_TX_DE_CLASSIFY_FAILED_TAG		= 18,
	HTT_STATS_TX_DE_CLASSIFY_STATS_TAG		= 19,
	HTT_STATS_TX_DE_CLASSIFY_STATUS_TAG		= 20,
	HTT_STATS_TX_DE_ENQUEUE_PACKETS_TAG		= 21,
	HTT_STATS_TX_DE_ENQUEUE_DISCARD_TAG		= 22,
	HTT_STATS_TX_DE_CMN_TAG				= 23,
	HTT_STATS_TX_PDEV_MU_MIMO_STATS_TAG		= 25,
	HTT_STATS_SFM_CMN_TAG				= 26,
	HTT_STATS_SRING_STATS_TAG			= 27,
	HTT_STATS_TX_PDEV_RATE_STATS_TAG		= 34,
	HTT_STATS_RX_PDEV_RATE_STATS_TAG		= 35,
	HTT_STATS_TX_PDEV_SCHEDULER_TXQ_STATS_TAG	= 36,
	HTT_STATS_TX_SCHED_CMN_TAG			= 37,
	HTT_STATS_SCHED_TXQ_CMD_POSTED_TAG		= 39,
	HTT_STATS_SFM_CLIENT_USER_TAG			= 41,
	HTT_STATS_SFM_CLIENT_TAG			= 42,
	HTT_STATS_TX_TQM_ERROR_STATS_TAG                = 43,
	HTT_STATS_SCHED_TXQ_CMD_REAPED_TAG		= 44,
	HTT_STATS_TX_SELFGEN_AC_ERR_STATS_TAG		= 46,
	HTT_STATS_TX_SELFGEN_CMN_STATS_TAG		= 47,
	HTT_STATS_TX_SELFGEN_AC_STATS_TAG		= 48,
	HTT_STATS_TX_SELFGEN_AX_STATS_TAG		= 49,
	HTT_STATS_TX_SELFGEN_AX_ERR_STATS_TAG		= 50,
	HTT_STATS_HW_INTR_MISC_TAG			= 54,
	HTT_STATS_HW_PDEV_ERRS_TAG			= 56,
	HTT_STATS_TX_DE_COMPL_STATS_TAG			= 65,
	HTT_STATS_WHAL_TX_TAG				= 66,
	HTT_STATS_TX_PDEV_SIFS_HIST_TAG			= 67,
	HTT_STATS_PDEV_CCA_1SEC_HIST_TAG		= 70,
	HTT_STATS_PDEV_CCA_100MSEC_HIST_TAG		= 71,
	HTT_STATS_PDEV_CCA_STAT_CUMULATIVE_TAG		= 72,
	HTT_STATS_PDEV_CCA_COUNTERS_TAG			= 73,
	HTT_STATS_TX_PDEV_MPDU_STATS_TAG		= 74,
	HTT_STATS_TX_SOUNDING_STATS_TAG			= 80,
	HTT_STATS_SCHED_TXQ_SCHED_ORDER_SU_TAG		= 86,
	HTT_STATS_SCHED_TXQ_SCHED_INELIGIBILITY_TAG	= 87,
	HTT_STATS_PDEV_OBSS_PD_TAG			= 88,
	HTT_STATS_HW_WAR_TAG				= 89,
	HTT_STATS_LATENCY_PROF_STATS_TAG		= 91,
	HTT_STATS_LATENCY_CTX_TAG			= 92,
	HTT_STATS_LATENCY_CNT_TAG			= 93,
	HTT_STATS_RX_PDEV_UL_TRIG_STATS_TAG		= 94,
	HTT_STATS_RX_PDEV_UL_OFDMA_USER_STATS_TAG	= 95,
	HTT_STATS_RX_PDEV_UL_MUMIMO_TRIG_STATS_TAG	= 97,
	HTT_STATS_RX_FSE_STATS_TAG			= 98,
	HTT_STATS_SCHED_TXQ_SUPERCYCLE_TRIGGER_TAG	= 100,
	HTT_STATS_PDEV_CTRL_PATH_TX_STATS_TAG		= 102,
	HTT_STATS_RX_PDEV_RATE_EXT_STATS_TAG		= 103,
	HTT_STATS_PDEV_TX_RATE_TXBF_STATS_TAG		= 108,
	HTT_STATS_TX_SELFGEN_AC_SCHED_STATUS_STATS_TAG	= 111,
	HTT_STATS_TX_SELFGEN_AX_SCHED_STATUS_STATS_TAG	= 112,
	HTT_STATS_DLPAGER_STATS_TAG			= 120,
	HTT_STATS_PHY_COUNTERS_TAG			= 121,
	HTT_STATS_PHY_STATS_TAG				= 122,
	HTT_STATS_PHY_RESET_COUNTERS_TAG		= 123,
	HTT_STATS_PHY_RESET_STATS_TAG			= 124,
	HTT_STATS_SOC_TXRX_STATS_COMMON_TAG		= 125,
	HTT_STATS_PER_RATE_STATS_TAG			= 128,
	HTT_STATS_MU_PPDU_DIST_TAG			= 129,
	HTT_STATS_TX_PDEV_MUMIMO_GRP_STATS_TAG		= 130,
	HTT_STATS_AST_ENTRY_TAG				= 132,
	HTT_STATS_TX_PDEV_RATE_STATS_BE_OFDMA_TAG	= 135,
	HTT_STATS_TX_SELFGEN_BE_ERR_STATS_TAG		= 137,
	HTT_STATS_TX_SELFGEN_BE_STATS_TAG		= 138,
	HTT_STATS_TX_SELFGEN_BE_SCHED_STATUS_STATS_TAG	= 139,
	HTT_STATS_TX_PDEV_HISTOGRAM_STATS_TAG		= 144,
	HTT_STATS_TXBF_OFDMA_AX_NDPA_STATS_TAG		= 147,
	HTT_STATS_TXBF_OFDMA_AX_NDP_STATS_TAG		= 148,
	HTT_STATS_TXBF_OFDMA_AX_BRP_STATS_TAG		= 149,
	HTT_STATS_TXBF_OFDMA_AX_STEER_STATS_TAG		= 150,
	HTT_STATS_DMAC_RESET_STATS_TAG			= 155,
	HTT_STATS_PHY_TPC_STATS_TAG			= 157,
	HTT_STATS_PDEV_PUNCTURE_STATS_TAG		= 158,
	HTT_STATS_PDEV_SCHED_ALGO_OFDMA_STATS_TAG	= 165,
	HTT_STATS_TXBF_OFDMA_AX_STEER_MPDU_STATS_TAG	= 172,
	HTT_STATS_PDEV_MBSSID_CTRL_FRAME_STATS_TAG	= 176,

	HTT_STATS_MAX_TAG,
};

#define ATH12K_HTT_STATS_MAC_ID				GENMASK(7, 0)

#define ATH12K_HTT_TX_PDEV_MAX_SIFS_BURST_STATS		9
#define ATH12K_HTT_TX_PDEV_MAX_FLUSH_REASON_STATS	150

/* MU MIMO distribution stats is a 2-dimensional array
 * with dimension one denoting stats for nr4[0] or nr8[1]
 */
#define ATH12K_HTT_STATS_NUM_NR_BINS			2
#define ATH12K_HTT_STATS_MAX_NUM_MU_PPDU_PER_BURST	10
#define ATH12K_HTT_TX_PDEV_MAX_SIFS_BURST_HIST_STATS	10
#define ATH12K_HTT_STATS_MAX_NUM_SCHED_STATUS		9
#define ATH12K_HTT_STATS_NUM_SCHED_STATUS_WORDS		\
	(ATH12K_HTT_STATS_NUM_NR_BINS * ATH12K_HTT_STATS_MAX_NUM_SCHED_STATUS)
#define ATH12K_HTT_STATS_MU_PPDU_PER_BURST_WORDS	\
	(ATH12K_HTT_STATS_NUM_NR_BINS * ATH12K_HTT_STATS_MAX_NUM_MU_PPDU_PER_BURST)

enum ath12k_htt_tx_pdev_underrun_enum {
	HTT_STATS_TX_PDEV_NO_DATA_UNDERRUN		= 0,
	HTT_STATS_TX_PDEV_DATA_UNDERRUN_BETWEEN_MPDU	= 1,
	HTT_STATS_TX_PDEV_DATA_UNDERRUN_WITHIN_MPDU	= 2,
	HTT_TX_PDEV_MAX_URRN_STATS			= 3,
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

struct ath12k_htt_tx_pdev_stats_cmn_tlv {
	__le32 mac_id__word;
	__le32 hw_queued;
	__le32 hw_reaped;
	__le32 underrun;
	__le32 hw_paused;
	__le32 hw_flush;
	__le32 hw_filt;
	__le32 tx_abort;
	__le32 mpdu_requed;
	__le32 tx_xretry;
	__le32 data_rc;
	__le32 mpdu_dropped_xretry;
	__le32 illgl_rate_phy_err;
	__le32 cont_xretry;
	__le32 tx_timeout;
	__le32 pdev_resets;
	__le32 phy_underrun;
	__le32 txop_ovf;
	__le32 seq_posted;
	__le32 seq_failed_queueing;
	__le32 seq_completed;
	__le32 seq_restarted;
	__le32 mu_seq_posted;
	__le32 seq_switch_hw_paused;
	__le32 next_seq_posted_dsr;
	__le32 seq_posted_isr;
	__le32 seq_ctrl_cached;
	__le32 mpdu_count_tqm;
	__le32 msdu_count_tqm;
	__le32 mpdu_removed_tqm;
	__le32 msdu_removed_tqm;
	__le32 mpdus_sw_flush;
	__le32 mpdus_hw_filter;
	__le32 mpdus_truncated;
	__le32 mpdus_ack_failed;
	__le32 mpdus_expired;
	__le32 mpdus_seq_hw_retry;
	__le32 ack_tlv_proc;
	__le32 coex_abort_mpdu_cnt_valid;
	__le32 coex_abort_mpdu_cnt;
	__le32 num_total_ppdus_tried_ota;
	__le32 num_data_ppdus_tried_ota;
	__le32 local_ctrl_mgmt_enqued;
	__le32 local_ctrl_mgmt_freed;
	__le32 local_data_enqued;
	__le32 local_data_freed;
	__le32 mpdu_tried;
	__le32 isr_wait_seq_posted;

	__le32 tx_active_dur_us_low;
	__le32 tx_active_dur_us_high;
	__le32 remove_mpdus_max_retries;
	__le32 comp_delivered;
	__le32 ppdu_ok;
	__le32 self_triggers;
	__le32 tx_time_dur_data;
	__le32 seq_qdepth_repost_stop;
	__le32 mu_seq_min_msdu_repost_stop;
	__le32 seq_min_msdu_repost_stop;
	__le32 seq_txop_repost_stop;
	__le32 next_seq_cancel;
	__le32 fes_offsets_err_cnt;
	__le32 num_mu_peer_blacklisted;
	__le32 mu_ofdma_seq_posted;
	__le32 ul_mumimo_seq_posted;
	__le32 ul_ofdma_seq_posted;

	__le32 thermal_suspend_cnt;
	__le32 dfs_suspend_cnt;
	__le32 tx_abort_suspend_cnt;
	__le32 tgt_specific_opaque_txq_suspend_info;
	__le32 last_suspend_reason;
} __packed;

struct ath12k_htt_tx_pdev_stats_urrn_tlv {
	DECLARE_FLEX_ARRAY(__le32, urrn_stats);
} __packed;

struct ath12k_htt_tx_pdev_stats_flush_tlv {
	DECLARE_FLEX_ARRAY(__le32, flush_errs);
} __packed;

struct ath12k_htt_tx_pdev_stats_phy_err_tlv {
	DECLARE_FLEX_ARRAY(__le32, phy_errs);
} __packed;

struct ath12k_htt_tx_pdev_stats_sifs_tlv {
	DECLARE_FLEX_ARRAY(__le32, sifs_status);
} __packed;

struct ath12k_htt_pdev_ctrl_path_tx_stats_tlv {
	__le32 fw_tx_mgmt_subtype[ATH12K_HTT_STATS_SUBTYPE_MAX];
} __packed;

struct ath12k_htt_tx_pdev_stats_sifs_hist_tlv {
	DECLARE_FLEX_ARRAY(__le32, sifs_hist_status);
} __packed;

enum ath12k_htt_stats_hw_mode {
	ATH12K_HTT_STATS_HWMODE_AC = 0,
	ATH12K_HTT_STATS_HWMODE_AX = 1,
	ATH12K_HTT_STATS_HWMODE_BE = 2,
};

struct ath12k_htt_tx_pdev_mu_ppdu_dist_stats_tlv {
	__le32 hw_mode;
	__le32 num_seq_term_status[ATH12K_HTT_STATS_NUM_SCHED_STATUS_WORDS];
	__le32 num_ppdu_cmpl_per_burst[ATH12K_HTT_STATS_MU_PPDU_PER_BURST_WORDS];
	__le32 num_seq_posted[ATH12K_HTT_STATS_NUM_NR_BINS];
	__le32 num_ppdu_posted_per_burst[ATH12K_HTT_STATS_MU_PPDU_PER_BURST_WORDS];
} __packed;

#define ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS        12
#define ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS          4
#define ATH12K_HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS         5
#define ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS          4
#define ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS      8
#define ATH12K_HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES       7
#define ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_CCK_STATS     4
#define ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_OFDM_STATS    8
#define ATH12K_HTT_TX_PDEV_STATS_NUM_LTF                  4
#define ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS   2
#define ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS  2
#define ATH12K_HTT_TX_PDEV_STATS_NUM_11AX_TRIGGER_TYPES   6
#define ATH12K_HTT_TX_PDEV_STATS_NUM_PER_COUNTERS	  101

#define ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_DROP_COUNTERS \
	(ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS + \
	 ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS + \
	 ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS)

struct ath12k_htt_tx_pdev_rate_stats_tlv {
	__le32 mac_id_word;
	__le32 tx_ldpc;
	__le32 rts_cnt;
	__le32 ack_rssi;
	__le32 tx_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 tx_su_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 tx_mu_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 tx_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 tx_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 tx_stbc[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 tx_pream[ATH12K_HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES];
	__le32 tx_gi[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
		[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 tx_dcm[ATH12K_HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS];
	__le32 rts_success;
	__le32 tx_legacy_cck_rate[ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_CCK_STATS];
	__le32 tx_legacy_ofdm_rate[ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_OFDM_STATS];
	__le32 ac_mu_mimo_tx_ldpc;
	__le32 ax_mu_mimo_tx_ldpc;
	__le32 ofdma_tx_ldpc;
	__le32 tx_he_ltf[ATH12K_HTT_TX_PDEV_STATS_NUM_LTF];
	__le32 ac_mu_mimo_tx_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ax_mu_mimo_tx_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ofdma_tx_mcs[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ac_mu_mimo_tx_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 ax_mu_mimo_tx_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 ofdma_tx_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 ac_mu_mimo_tx_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 ax_mu_mimo_tx_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 ofdma_tx_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 ac_mu_mimo_tx_gi[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
			    [ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ax_mimo_tx_gi[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
			    [ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ofdma_tx_gi[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
		       [ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 trigger_type_11ax[ATH12K_HTT_TX_PDEV_STATS_NUM_11AX_TRIGGER_TYPES];
	__le32 tx_11ax_su_ext;
	__le32 tx_mcs_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 tx_stbc_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 tx_gi_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
		     [ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 ax_mu_mimo_tx_mcs_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 ofdma_tx_mcs_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 ax_tx_gi_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
				[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 ofd_tx_gi_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
			   [ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
	__le32 tx_mcs_ext_2[ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS];
	__le32 tx_bw_320mhz;
};

struct ath12k_htt_tx_histogram_stats_tlv {
	__le32 rate_retry_mcs_drop_cnt;
	__le32 mcs_drop_rate[ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_DROP_COUNTERS];
	__le32 per_histogram_cnt[ATH12K_HTT_TX_PDEV_STATS_NUM_PER_COUNTERS];
	__le32 low_latency_rate_cnt;
	__le32 su_burst_rate_drop_cnt;
	__le32 su_burst_rate_drop_fail_cnt;
} __packed;

#define ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_CCK_STATS		4
#define ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_OFDM_STATS		8
#define ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS		12
#define ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS		4
#define ATH12K_HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS		5
#define ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS		4
#define ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS		8
#define ATH12K_HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES		7
#define ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER			8
#define ATH12K_HTT_RX_PDEV_STATS_RXEVM_MAX_PILOTS_NSS		16
#define ATH12K_HTT_RX_PDEV_STATS_NUM_RU_SIZE_COUNTERS		6
#define ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER		8
#define ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS		2

struct ath12k_htt_rx_pdev_rate_stats_tlv {
	__le32 mac_id_word;
	__le32 nsts;
	__le32 rx_ldpc;
	__le32 rts_cnt;
	__le32 rssi_mgmt;
	__le32 rssi_data;
	__le32 rssi_comb;
	__le32 rx_mcs[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rx_nss[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 rx_dcm[ATH12K_HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS];
	__le32 rx_stbc[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rx_bw[ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 rx_pream[ATH12K_HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES];
	u8 rssi_chain_in_db[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
		     [ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 rx_gi[ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS]
		[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rssi_in_dbm;
	__le32 rx_11ax_su_ext;
	__le32 rx_11ac_mumimo;
	__le32 rx_11ax_mumimo;
	__le32 rx_11ax_ofdma;
	__le32 txbf;
	__le32 rx_legacy_cck_rate[ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_CCK_STATS];
	__le32 rx_legacy_ofdm_rate[ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_OFDM_STATS];
	__le32 rx_active_dur_us_low;
	__le32 rx_active_dur_us_high;
	__le32 rx_11ax_ul_ofdma;
	__le32 ul_ofdma_rx_mcs[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ul_ofdma_rx_gi[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
			  [ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 ul_ofdma_rx_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 ul_ofdma_rx_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 ul_ofdma_rx_stbc;
	__le32 ul_ofdma_rx_ldpc;
	__le32 rx_ulofdma_non_data_ppdu[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 rx_ulofdma_data_ppdu[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 rx_ulofdma_mpdu_ok[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 rx_ulofdma_mpdu_fail[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 nss_count;
	__le32 pilot_count;
	__le32 rx_pil_evm_db[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
			   [ATH12K_HTT_RX_PDEV_STATS_RXEVM_MAX_PILOTS_NSS];
	__le32 rx_pilot_evm_db_mean[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	s8 rx_ul_fd_rssi[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
			[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 per_chain_rssi_pkt_type;
	s8 rx_per_chain_rssi_in_dbm[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
				   [ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS];
	__le32 rx_su_ndpa;
	__le32 rx_11ax_su_txbf_mcs[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rx_mu_ndpa;
	__le32 rx_11ax_mu_txbf_mcs[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rx_br_poll;
	__le32 rx_11ax_dl_ofdma_mcs[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS];
	__le32 rx_11ax_dl_ofdma_ru[ATH12K_HTT_RX_PDEV_STATS_NUM_RU_SIZE_COUNTERS];
	__le32 rx_ulmumimo_non_data_ppdu[ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER];
	__le32 rx_ulmumimo_data_ppdu[ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER];
	__le32 rx_ulmumimo_mpdu_ok[ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER];
	__le32 rx_ulmumimo_mpdu_fail[ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER];
	__le32 rx_ulofdma_non_data_nusers[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 rx_ulofdma_data_nusers[ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER];
	__le32 rx_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS];
};

#define ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT_COUNTERS		4
#define ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT		14
#define ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS	2
#define ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT2_COUNTERS		5
#define ATH12K_HTT_RX_PDEV_STATS_NUM_PUNCTURED_MODE_COUNTERS	5

struct ath12k_htt_rx_pdev_rate_ext_stats_tlv {
	u8 rssi_chain_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
			 [ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT_COUNTERS];
	s8 rx_per_chain_rssi_ext_in_dbm[ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS]
				       [ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT_COUNTERS];
	__le32 rssi_mcast_in_dbm;
	__le32 rssi_mgmt_in_dbm;
	__le32 rx_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_stbc_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_gi_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS]
		     [ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 ul_ofdma_rx_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 ul_ofdma_rx_gi_ext[ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS]
			      [ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_11ax_su_txbf_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_11ax_mu_txbf_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_11ax_dl_ofdma_mcs_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT];
	__le32 rx_mcs_ext_2[ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS];
	__le32 rx_bw_ext[ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT2_COUNTERS];
	__le32 rx_gi_ext_2[ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS]
		[ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS];
	__le32 rx_su_punctured_mode[ATH12K_HTT_RX_PDEV_STATS_NUM_PUNCTURED_MODE_COUNTERS];
};

#define ATH12K_HTT_TX_PDEV_STATS_SCHED_PER_TXQ_MAC_ID	GENMASK(7, 0)
#define ATH12K_HTT_TX_PDEV_STATS_SCHED_PER_TXQ_ID	GENMASK(15, 8)

#define ATH12K_HTT_TX_PDEV_NUM_SCHED_ORDER_LOG	20

struct ath12k_htt_stats_tx_sched_cmn_tlv {
	__le32 mac_id__word;
	__le32 current_timestamp;
} __packed;

struct ath12k_htt_tx_pdev_stats_sched_per_txq_tlv {
	__le32 mac_id__word;
	__le32 sched_policy;
	__le32 last_sched_cmd_posted_timestamp;
	__le32 last_sched_cmd_compl_timestamp;
	__le32 sched_2_tac_lwm_count;
	__le32 sched_2_tac_ring_full;
	__le32 sched_cmd_post_failure;
	__le32 num_active_tids;
	__le32 num_ps_schedules;
	__le32 sched_cmds_pending;
	__le32 num_tid_register;
	__le32 num_tid_unregister;
	__le32 num_qstats_queried;
	__le32 qstats_update_pending;
	__le32 last_qstats_query_timestamp;
	__le32 num_tqm_cmdq_full;
	__le32 num_de_sched_algo_trigger;
	__le32 num_rt_sched_algo_trigger;
	__le32 num_tqm_sched_algo_trigger;
	__le32 notify_sched;
	__le32 dur_based_sendn_term;
	__le32 su_notify2_sched;
	__le32 su_optimal_queued_msdus_sched;
	__le32 su_delay_timeout_sched;
	__le32 su_min_txtime_sched_delay;
	__le32 su_no_delay;
	__le32 num_supercycles;
	__le32 num_subcycles_with_sort;
	__le32 num_subcycles_no_sort;
} __packed;

struct ath12k_htt_sched_txq_cmd_posted_tlv {
	DECLARE_FLEX_ARRAY(__le32, sched_cmd_posted);
} __packed;

struct ath12k_htt_sched_txq_cmd_reaped_tlv {
	DECLARE_FLEX_ARRAY(__le32, sched_cmd_reaped);
} __packed;

struct ath12k_htt_sched_txq_sched_order_su_tlv {
	DECLARE_FLEX_ARRAY(__le32, sched_order_su);
} __packed;

struct ath12k_htt_sched_txq_sched_ineligibility_tlv {
	DECLARE_FLEX_ARRAY(__le32, sched_ineligibility);
} __packed;

enum ath12k_htt_sched_txq_supercycle_triggers_tlv_enum {
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_NONE = 0,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_FORCED,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_LESS_NUM_TIDQ_ENTRIES,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_LESS_NUM_ACTIVE_TIDS,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_MAX_ITR_REACHED,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_DUR_THRESHOLD_REACHED,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_TWT_TRIGGER,
	ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_MAX,
};

struct ath12k_htt_sched_txq_supercycle_triggers_tlv {
	DECLARE_FLEX_ARRAY(__le32, supercycle_triggers);
} __packed;

struct ath12k_htt_hw_stats_pdev_errs_tlv {
	__le32 mac_id__word;
	__le32 tx_abort;
	__le32 tx_abort_fail_count;
	__le32 rx_abort;
	__le32 rx_abort_fail_count;
	__le32 warm_reset;
	__le32 cold_reset;
	__le32 tx_flush;
	__le32 tx_glb_reset;
	__le32 tx_txq_reset;
	__le32 rx_timeout_reset;
	__le32 mac_cold_reset_restore_cal;
	__le32 mac_cold_reset;
	__le32 mac_warm_reset;
	__le32 mac_only_reset;
	__le32 phy_warm_reset;
	__le32 phy_warm_reset_ucode_trig;
	__le32 mac_warm_reset_restore_cal;
	__le32 mac_sfm_reset;
	__le32 phy_warm_reset_m3_ssr;
	__le32 phy_warm_reset_reason_phy_m3;
	__le32 phy_warm_reset_reason_tx_hw_stuck;
	__le32 phy_warm_reset_reason_num_rx_frame_stuck;
	__le32 phy_warm_reset_reason_wal_rx_rec_rx_busy;
	__le32 phy_warm_reset_reason_wal_rx_rec_mac_hng;
	__le32 phy_warm_reset_reason_mac_conv_phy_reset;
	__le32 wal_rx_recovery_rst_mac_hang_cnt;
	__le32 wal_rx_recovery_rst_known_sig_cnt;
	__le32 wal_rx_recovery_rst_no_rx_cnt;
	__le32 wal_rx_recovery_rst_no_rx_consec_cnt;
	__le32 wal_rx_recovery_rst_rx_busy_cnt;
	__le32 wal_rx_recovery_rst_phy_mac_hang_cnt;
	__le32 rx_flush_cnt;
	__le32 phy_warm_reset_reason_tx_exp_cca_stuck;
	__le32 phy_warm_reset_reason_tx_consec_flsh_war;
	__le32 phy_warm_reset_reason_tx_hwsch_reset_war;
	__le32 phy_warm_reset_reason_hwsch_cca_wdog_war;
	__le32 fw_rx_rings_reset;
	__le32 rx_dest_drain_rx_descs_leak_prevented;
	__le32 rx_dest_drain_rx_descs_saved_cnt;
	__le32 rx_dest_drain_rxdma2reo_leak_detected;
	__le32 rx_dest_drain_rxdma2fw_leak_detected;
	__le32 rx_dest_drain_rxdma2wbm_leak_detected;
	__le32 rx_dest_drain_rxdma1_2sw_leak_detected;
	__le32 rx_dest_drain_rx_drain_ok_mac_idle;
	__le32 rx_dest_drain_ok_mac_not_idle;
	__le32 rx_dest_drain_prerequisite_invld;
	__le32 rx_dest_drain_skip_non_lmac_reset;
	__le32 rx_dest_drain_hw_fifo_notempty_post_wait;
} __packed;

#define ATH12K_HTT_STATS_MAX_HW_INTR_NAME_LEN 8
struct ath12k_htt_hw_stats_intr_misc_tlv {
	u8 hw_intr_name[ATH12K_HTT_STATS_MAX_HW_INTR_NAME_LEN];
	__le32 mask;
	__le32 count;
} __packed;

struct ath12k_htt_hw_stats_whal_tx_tlv {
	__le32 mac_id__word;
	__le32 last_unpause_ppdu_id;
	__le32 hwsch_unpause_wait_tqm_write;
	__le32 hwsch_dummy_tlv_skipped;
	__le32 hwsch_misaligned_offset_received;
	__le32 hwsch_reset_count;
	__le32 hwsch_dev_reset_war;
	__le32 hwsch_delayed_pause;
	__le32 hwsch_long_delayed_pause;
	__le32 sch_rx_ppdu_no_response;
	__le32 sch_selfgen_response;
	__le32 sch_rx_sifs_resp_trigger;
} __packed;

struct ath12k_htt_hw_war_stats_tlv {
	__le32 mac_id__word;
	DECLARE_FLEX_ARRAY(__le32, hw_wars);
} __packed;

struct ath12k_htt_tx_tqm_cmn_stats_tlv {
	__le32 mac_id__word;
	__le32 max_cmdq_id;
	__le32 list_mpdu_cnt_hist_intvl;
	__le32 add_msdu;
	__le32 q_empty;
	__le32 q_not_empty;
	__le32 drop_notification;
	__le32 desc_threshold;
	__le32 hwsch_tqm_invalid_status;
	__le32 missed_tqm_gen_mpdus;
	__le32 tqm_active_tids;
	__le32 tqm_inactive_tids;
	__le32 tqm_active_msduq_flows;
	__le32 msduq_timestamp_updates;
	__le32 msduq_updates_mpdu_head_info_cmd;
	__le32 msduq_updates_emp_to_nonemp_status;
	__le32 get_mpdu_head_info_cmds_by_query;
	__le32 get_mpdu_head_info_cmds_by_tac;
	__le32 gen_mpdu_cmds_by_query;
	__le32 high_prio_q_not_empty;
} __packed;

struct ath12k_htt_tx_tqm_error_stats_tlv {
	__le32 q_empty_failure;
	__le32 q_not_empty_failure;
	__le32 add_msdu_failure;
	__le32 tqm_cache_ctl_err;
	__le32 tqm_soft_reset;
	__le32 tqm_reset_num_in_use_link_descs;
	__le32 tqm_reset_num_lost_link_descs;
	__le32 tqm_reset_num_lost_host_tx_buf_cnt;
	__le32 tqm_reset_num_in_use_internal_tqm;
	__le32 tqm_reset_num_in_use_idle_link_rng;
	__le32 tqm_reset_time_to_tqm_hang_delta_ms;
	__le32 tqm_reset_recovery_time_ms;
	__le32 tqm_reset_num_peers_hdl;
	__le32 tqm_reset_cumm_dirty_hw_mpduq_cnt;
	__le32 tqm_reset_cumm_dirty_hw_msduq_proc;
	__le32 tqm_reset_flush_cache_cmd_su_cnt;
	__le32 tqm_reset_flush_cache_cmd_other_cnt;
	__le32 tqm_reset_flush_cache_cmd_trig_type;
	__le32 tqm_reset_flush_cache_cmd_trig_cfg;
	__le32 tqm_reset_flush_cmd_skp_status_null;
} __packed;

struct ath12k_htt_tx_tqm_gen_mpdu_stats_tlv {
	DECLARE_FLEX_ARRAY(__le32, gen_mpdu_end_reason);
} __packed;

#define ATH12K_HTT_TX_TQM_MAX_LIST_MPDU_END_REASON		16
#define ATH12K_HTT_TX_TQM_MAX_LIST_MPDU_CNT_HISTOGRAM_BINS	16

struct ath12k_htt_tx_tqm_list_mpdu_stats_tlv {
	DECLARE_FLEX_ARRAY(__le32, list_mpdu_end_reason);
} __packed;

struct ath12k_htt_tx_tqm_list_mpdu_cnt_tlv {
	DECLARE_FLEX_ARRAY(__le32, list_mpdu_cnt_hist);
} __packed;

struct ath12k_htt_tx_tqm_pdev_stats_tlv {
	__le32 msdu_count;
	__le32 mpdu_count;
	__le32 remove_msdu;
	__le32 remove_mpdu;
	__le32 remove_msdu_ttl;
	__le32 send_bar;
	__le32 bar_sync;
	__le32 notify_mpdu;
	__le32 sync_cmd;
	__le32 write_cmd;
	__le32 hwsch_trigger;
	__le32 ack_tlv_proc;
	__le32 gen_mpdu_cmd;
	__le32 gen_list_cmd;
	__le32 remove_mpdu_cmd;
	__le32 remove_mpdu_tried_cmd;
	__le32 mpdu_queue_stats_cmd;
	__le32 mpdu_head_info_cmd;
	__le32 msdu_flow_stats_cmd;
	__le32 remove_msdu_cmd;
	__le32 remove_msdu_ttl_cmd;
	__le32 flush_cache_cmd;
	__le32 update_mpduq_cmd;
	__le32 enqueue;
	__le32 enqueue_notify;
	__le32 notify_mpdu_at_head;
	__le32 notify_mpdu_state_valid;
	__le32 sched_udp_notify1;
	__le32 sched_udp_notify2;
	__le32 sched_nonudp_notify1;
	__le32 sched_nonudp_notify2;
} __packed;

struct ath12k_htt_tx_de_cmn_stats_tlv {
	__le32 mac_id__word;
	__le32 tcl2fw_entry_count;
	__le32 not_to_fw;
	__le32 invalid_pdev_vdev_peer;
	__le32 tcl_res_invalid_addrx;
	__le32 wbm2fw_entry_count;
	__le32 invalid_pdev;
	__le32 tcl_res_addrx_timeout;
	__le32 invalid_vdev;
	__le32 invalid_tcl_exp_frame_desc;
	__le32 vdev_id_mismatch_cnt;
} __packed;

struct ath12k_htt_tx_de_eapol_packets_stats_tlv {
	__le32 m1_packets;
	__le32 m2_packets;
	__le32 m3_packets;
	__le32 m4_packets;
	__le32 g1_packets;
	__le32 g2_packets;
	__le32 rc4_packets;
	__le32 eap_packets;
	__le32 eapol_start_packets;
	__le32 eapol_logoff_packets;
	__le32 eapol_encap_asf_packets;
} __packed;

struct ath12k_htt_tx_de_classify_stats_tlv {
	__le32 arp_packets;
	__le32 igmp_packets;
	__le32 dhcp_packets;
	__le32 host_inspected;
	__le32 htt_included;
	__le32 htt_valid_mcs;
	__le32 htt_valid_nss;
	__le32 htt_valid_preamble_type;
	__le32 htt_valid_chainmask;
	__le32 htt_valid_guard_interval;
	__le32 htt_valid_retries;
	__le32 htt_valid_bw_info;
	__le32 htt_valid_power;
	__le32 htt_valid_key_flags;
	__le32 htt_valid_no_encryption;
	__le32 fse_entry_count;
	__le32 fse_priority_be;
	__le32 fse_priority_high;
	__le32 fse_priority_low;
	__le32 fse_traffic_ptrn_be;
	__le32 fse_traffic_ptrn_over_sub;
	__le32 fse_traffic_ptrn_bursty;
	__le32 fse_traffic_ptrn_interactive;
	__le32 fse_traffic_ptrn_periodic;
	__le32 fse_hwqueue_alloc;
	__le32 fse_hwqueue_created;
	__le32 fse_hwqueue_send_to_host;
	__le32 mcast_entry;
	__le32 bcast_entry;
	__le32 htt_update_peer_cache;
	__le32 htt_learning_frame;
	__le32 fse_invalid_peer;
	__le32 mec_notify;
} __packed;

struct ath12k_htt_tx_de_classify_failed_stats_tlv {
	__le32 ap_bss_peer_not_found;
	__le32 ap_bcast_mcast_no_peer;
	__le32 sta_delete_in_progress;
	__le32 ibss_no_bss_peer;
	__le32 invalid_vdev_type;
	__le32 invalid_ast_peer_entry;
	__le32 peer_entry_invalid;
	__le32 ethertype_not_ip;
	__le32 eapol_lookup_failed;
	__le32 qpeer_not_allow_data;
	__le32 fse_tid_override;
	__le32 ipv6_jumbogram_zero_length;
	__le32 qos_to_non_qos_in_prog;
	__le32 ap_bcast_mcast_eapol;
	__le32 unicast_on_ap_bss_peer;
	__le32 ap_vdev_invalid;
	__le32 incomplete_llc;
	__le32 eapol_duplicate_m3;
	__le32 eapol_duplicate_m4;
} __packed;

struct ath12k_htt_tx_de_classify_status_stats_tlv {
	__le32 eok;
	__le32 classify_done;
	__le32 lookup_failed;
	__le32 send_host_dhcp;
	__le32 send_host_mcast;
	__le32 send_host_unknown_dest;
	__le32 send_host;
	__le32 status_invalid;
} __packed;

struct ath12k_htt_tx_de_enqueue_packets_stats_tlv {
	__le32 enqueued_pkts;
	__le32 to_tqm;
	__le32 to_tqm_bypass;
} __packed;

struct ath12k_htt_tx_de_enqueue_discard_stats_tlv {
	__le32 discarded_pkts;
	__le32 local_frames;
	__le32 is_ext_msdu;
} __packed;

struct ath12k_htt_tx_de_compl_stats_tlv {
	__le32 tcl_dummy_frame;
	__le32 tqm_dummy_frame;
	__le32 tqm_notify_frame;
	__le32 fw2wbm_enq;
	__le32 tqm_bypass_frame;
} __packed;

enum ath12k_htt_tx_mumimo_grp_invalid_reason_code_stats {
	ATH12K_HTT_TX_MUMIMO_GRP_VALID,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_NUM_MU_USERS_EXCEEDED_MU_MAX_USERS,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_SCHED_ALGO_NOT_MU_COMPATIBLE_GID,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_NON_PRIMARY_GRP,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_ZERO_CANDIDATES,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_MORE_CANDIDATES,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_GROUP_SIZE_EXCEED_NSS,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_GROUP_INELIGIBLE,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_GROUP_EFF_MU_TPUT_OMBPS,
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_MAX_REASON_CODE,
};

#define ATH12K_HTT_NUM_AC_WMM				0x4
#define ATH12K_HTT_MAX_NUM_SBT_INTR			4
#define ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS		4
#define ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS		8
#define ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS		8
#define ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS	7
#define ATH12K_HTT_TX_NUM_OFDMA_USER_STATS		74
#define ATH12K_HTT_TX_NUM_UL_MUMIMO_USER_STATS		8
#define ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ		8
#define ATH12K_HTT_STATS_MUMIMO_TPUT_NUM_BINS		10

#define ATH12K_HTT_STATS_MAX_INVALID_REASON_CODE \
	ATH12K_HTT_TX_MUMIMO_GRP_INVALID_MAX_REASON_CODE
#define ATH12K_HTT_TX_NUM_MUMIMO_GRP_INVALID_WORDS \
	(ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ * ATH12K_HTT_STATS_MAX_INVALID_REASON_CODE)

struct ath12k_htt_tx_selfgen_cmn_stats_tlv {
	__le32 mac_id__word;
	__le32 su_bar;
	__le32 rts;
	__le32 cts2self;
	__le32 qos_null;
	__le32 delayed_bar_1;
	__le32 delayed_bar_2;
	__le32 delayed_bar_3;
	__le32 delayed_bar_4;
	__le32 delayed_bar_5;
	__le32 delayed_bar_6;
	__le32 delayed_bar_7;
} __packed;

struct ath12k_htt_tx_selfgen_ac_stats_tlv {
	__le32 ac_su_ndpa;
	__le32 ac_su_ndp;
	__le32 ac_mu_mimo_ndpa;
	__le32 ac_mu_mimo_ndp;
	__le32 ac_mu_mimo_brpoll[ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS - 1];
} __packed;

struct ath12k_htt_tx_selfgen_ax_stats_tlv {
	__le32 ax_su_ndpa;
	__le32 ax_su_ndp;
	__le32 ax_mu_mimo_ndpa;
	__le32 ax_mu_mimo_ndp;
	__le32 ax_mu_mimo_brpoll[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS - 1];
	__le32 ax_basic_trigger;
	__le32 ax_bsr_trigger;
	__le32 ax_mu_bar_trigger;
	__le32 ax_mu_rts_trigger;
	__le32 ax_ulmumimo_trigger;
} __packed;

struct ath12k_htt_tx_selfgen_be_stats_tlv {
	__le32 be_su_ndpa;
	__le32 be_su_ndp;
	__le32 be_mu_mimo_ndpa;
	__le32 be_mu_mimo_ndp;
	__le32 be_mu_mimo_brpoll[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1];
	__le32 be_basic_trigger;
	__le32 be_bsr_trigger;
	__le32 be_mu_bar_trigger;
	__le32 be_mu_rts_trigger;
	__le32 be_ulmumimo_trigger;
	__le32 be_su_ndpa_queued;
	__le32 be_su_ndp_queued;
	__le32 be_mu_mimo_ndpa_queued;
	__le32 be_mu_mimo_ndp_queued;
	__le32 be_mu_mimo_brpoll_queued[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1];
	__le32 be_ul_mumimo_trigger[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
} __packed;

struct ath12k_htt_tx_selfgen_ac_err_stats_tlv {
	__le32 ac_su_ndp_err;
	__le32 ac_su_ndpa_err;
	__le32 ac_mu_mimo_ndpa_err;
	__le32 ac_mu_mimo_ndp_err;
	__le32 ac_mu_mimo_brp1_err;
	__le32 ac_mu_mimo_brp2_err;
	__le32 ac_mu_mimo_brp3_err;
} __packed;

struct ath12k_htt_tx_selfgen_ax_err_stats_tlv {
	__le32 ax_su_ndp_err;
	__le32 ax_su_ndpa_err;
	__le32 ax_mu_mimo_ndpa_err;
	__le32 ax_mu_mimo_ndp_err;
	__le32 ax_mu_mimo_brp_err[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS - 1];
	__le32 ax_basic_trigger_err;
	__le32 ax_bsr_trigger_err;
	__le32 ax_mu_bar_trigger_err;
	__le32 ax_mu_rts_trigger_err;
	__le32 ax_ulmumimo_trigger_err;
} __packed;

struct ath12k_htt_tx_selfgen_be_err_stats_tlv {
	__le32 be_su_ndp_err;
	__le32 be_su_ndpa_err;
	__le32 be_mu_mimo_ndpa_err;
	__le32 be_mu_mimo_ndp_err;
	__le32 be_mu_mimo_brp_err[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1];
	__le32 be_basic_trigger_err;
	__le32 be_bsr_trigger_err;
	__le32 be_mu_bar_trigger_err;
	__le32 be_mu_rts_trigger_err;
	__le32 be_ulmumimo_trigger_err;
	__le32 be_mu_mimo_brp_err_num_cbf_rxd[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
	__le32 be_su_ndpa_flushed;
	__le32 be_su_ndp_flushed;
	__le32 be_mu_mimo_ndpa_flushed;
	__le32 be_mu_mimo_ndp_flushed;
	__le32 be_mu_mimo_brpoll_flushed[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1];
	__le32 be_ul_mumimo_trigger_err[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
} __packed;

enum ath12k_htt_tx_selfgen_sch_tsflag_error_stats {
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_FLUSH_RCVD_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_FILT_SCHED_CMD_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_RESP_MISMATCH_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_RESP_CBF_MIMO_CTRL_MISMATCH_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_RESP_CBF_BW_MISMATCH_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_RETRY_COUNT_FAIL_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_RESP_TOO_LATE_RECEIVED_ERR,
	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_SIFS_STALL_NO_NEXT_CMD_ERR,

	ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS
};

struct ath12k_htt_tx_selfgen_ac_sched_status_stats_tlv {
	__le32 ac_su_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ac_su_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ac_su_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ac_mu_mimo_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ac_mu_mimo_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ac_mu_mimo_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ac_mu_mimo_brp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ac_mu_mimo_brp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
} __packed;

struct ath12k_htt_tx_selfgen_ax_sched_status_stats_tlv {
	__le32 ax_su_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_su_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_su_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ax_mu_mimo_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_mu_mimo_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_mu_mimo_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ax_mu_brp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_mu_brp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ax_mu_bar_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_mu_bar_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ax_basic_trig_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_basic_trig_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 ax_ulmumimo_trig_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 ax_ulmumimo_trig_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
} __packed;

struct ath12k_htt_tx_selfgen_be_sched_status_stats_tlv {
	__le32 be_su_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_su_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_su_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 be_mu_mimo_ndpa_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_mu_mimo_ndp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_mu_mimo_ndp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 be_mu_brp_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_mu_brp_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 be_mu_bar_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_mu_bar_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 be_basic_trig_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_basic_trig_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
	__le32 be_ulmumimo_trig_sch_status[ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS];
	__le32 be_ulmumimo_trig_sch_flag_err[ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS];
} __packed;

struct ath12k_htt_stats_string_tlv {
	DECLARE_FLEX_ARRAY(__le32, data);
} __packed;

#define ATH12K_HTT_SRING_STATS_MAC_ID                  GENMASK(7, 0)
#define ATH12K_HTT_SRING_STATS_RING_ID                 GENMASK(15, 8)
#define ATH12K_HTT_SRING_STATS_ARENA                   GENMASK(23, 16)
#define ATH12K_HTT_SRING_STATS_EP                      BIT(24)
#define ATH12K_HTT_SRING_STATS_NUM_AVAIL_WORDS         GENMASK(15, 0)
#define ATH12K_HTT_SRING_STATS_NUM_VALID_WORDS         GENMASK(31, 16)
#define ATH12K_HTT_SRING_STATS_HEAD_PTR                GENMASK(15, 0)
#define ATH12K_HTT_SRING_STATS_TAIL_PTR                GENMASK(31, 16)
#define ATH12K_HTT_SRING_STATS_CONSUMER_EMPTY          GENMASK(15, 0)
#define ATH12K_HTT_SRING_STATS_PRODUCER_FULL           GENMASK(31, 16)
#define ATH12K_HTT_SRING_STATS_PREFETCH_COUNT          GENMASK(15, 0)
#define ATH12K_HTT_SRING_STATS_INTERNAL_TAIL_PTR       GENMASK(31, 16)

struct ath12k_htt_sring_stats_tlv {
	__le32 mac_id__ring_id__arena__ep;
	__le32 base_addr_lsb;
	__le32 base_addr_msb;
	__le32 ring_size;
	__le32 elem_size;
	__le32 num_avail_words__num_valid_words;
	__le32 head_ptr__tail_ptr;
	__le32 consumer_empty__producer_full;
	__le32 prefetch_count__internal_tail_ptr;
} __packed;

struct ath12k_htt_sfm_cmn_tlv {
	__le32 mac_id__word;
	__le32 buf_total;
	__le32 mem_empty;
	__le32 deallocate_bufs;
	__le32 num_records;
} __packed;

struct ath12k_htt_sfm_client_tlv {
	__le32 client_id;
	__le32 buf_min;
	__le32 buf_max;
	__le32 buf_busy;
	__le32 buf_alloc;
	__le32 buf_avail;
	__le32 num_users;
} __packed;

struct ath12k_htt_sfm_client_user_tlv {
	DECLARE_FLEX_ARRAY(__le32, dwords_used_by_user_n);
} __packed;

struct ath12k_htt_tx_pdev_mu_mimo_sch_stats_tlv {
	__le32 mu_mimo_sch_posted;
	__le32 mu_mimo_sch_failed;
	__le32 mu_mimo_ppdu_posted;
	__le32 ac_mu_mimo_sch_nusers[ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS];
	__le32 ax_mu_mimo_sch_nusers[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS];
	__le32 ax_ofdma_sch_nusers[ATH12K_HTT_TX_NUM_OFDMA_USER_STATS];
	__le32 ax_ul_ofdma_nusers[ATH12K_HTT_TX_NUM_OFDMA_USER_STATS];
	__le32 ax_ul_ofdma_bsr_nusers[ATH12K_HTT_TX_NUM_OFDMA_USER_STATS];
	__le32 ax_ul_ofdma_bar_nusers[ATH12K_HTT_TX_NUM_OFDMA_USER_STATS];
	__le32 ax_ul_ofdma_brp_nusers[ATH12K_HTT_TX_NUM_OFDMA_USER_STATS];
	__le32 ax_ul_mumimo_nusers[ATH12K_HTT_TX_NUM_UL_MUMIMO_USER_STATS];
	__le32 ax_ul_mumimo_brp_nusers[ATH12K_HTT_TX_NUM_UL_MUMIMO_USER_STATS];
	__le32 ac_mu_mimo_per_grp_sz[ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS];
	__le32 ax_mu_mimo_per_grp_sz[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS];
	__le32 be_mu_mimo_sch_nusers[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
	__le32 be_mu_mimo_per_grp_sz[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
	__le32 ac_mu_mimo_grp_sz_ext[ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS];
} __packed;

struct ath12k_htt_tx_pdev_mumimo_grp_stats_tlv {
	__le32 dl_mumimo_grp_best_grp_size[ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ];
	__le32 dl_mumimo_grp_best_num_usrs[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS];
	__le32 dl_mumimo_grp_eligible[ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ];
	__le32 dl_mumimo_grp_ineligible[ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ];
	__le32 dl_mumimo_grp_invalid[ATH12K_HTT_TX_NUM_MUMIMO_GRP_INVALID_WORDS];
	__le32 dl_mumimo_grp_tputs[ATH12K_HTT_STATS_MUMIMO_TPUT_NUM_BINS];
	__le32 ul_mumimo_grp_best_grp_size[ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ];
	__le32 ul_mumimo_grp_best_usrs[ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS];
	__le32 ul_mumimo_grp_tputs[ATH12K_HTT_STATS_MUMIMO_TPUT_NUM_BINS];
} __packed;

enum ath12k_htt_stats_tx_sched_modes {
	ATH12K_HTT_STATS_TX_SCHED_MODE_MU_MIMO_AC = 0,
	ATH12K_HTT_STATS_TX_SCHED_MODE_MU_MIMO_AX,
	ATH12K_HTT_STATS_TX_SCHED_MODE_MU_OFDMA_AX,
	ATH12K_HTT_STATS_TX_SCHED_MODE_MU_OFDMA_BE,
	ATH12K_HTT_STATS_TX_SCHED_MODE_MU_MIMO_BE
};

struct ath12k_htt_tx_pdev_mpdu_stats_tlv {
	__le32 mpdus_queued_usr;
	__le32 mpdus_tried_usr;
	__le32 mpdus_failed_usr;
	__le32 mpdus_requeued_usr;
	__le32 err_no_ba_usr;
	__le32 mpdu_underrun_usr;
	__le32 ampdu_underrun_usr;
	__le32 user_index;
	__le32 tx_sched_mode;
} __packed;

struct ath12k_htt_pdev_stats_cca_counters_tlv {
	__le32 tx_frame_usec;
	__le32 rx_frame_usec;
	__le32 rx_clear_usec;
	__le32 my_rx_frame_usec;
	__le32 usec_cnt;
	__le32 med_rx_idle_usec;
	__le32 med_tx_idle_global_usec;
	__le32 cca_obss_usec;
} __packed;

struct ath12k_htt_pdev_cca_stats_hist_v1_tlv {
	__le32 chan_num;
	__le32 num_records;
	__le32 valid_cca_counters_bitmap;
	__le32 collection_interval;
} __packed;

#define ATH12K_HTT_TX_CV_CORR_MAX_NUM_COLUMNS		8
#define ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS		4
#define ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS          8
#define ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS		8
#define ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS	4
#define ATH12K_HTT_TX_NUM_MCS_CNTRS			12
#define ATH12K_HTT_TX_NUM_EXTRA_MCS_CNTRS		2

#define ATH12K_HTT_TX_NUM_OF_SOUNDING_STATS_WORDS \
	(ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS * \
	 ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS)

enum ath12k_htt_txbf_sound_steer_modes {
	ATH12K_HTT_IMPL_STEER_STATS		= 0,
	ATH12K_HTT_EXPL_SUSIFS_STEER_STATS	= 1,
	ATH12K_HTT_EXPL_SURBO_STEER_STATS	= 2,
	ATH12K_HTT_EXPL_MUSIFS_STEER_STATS	= 3,
	ATH12K_HTT_EXPL_MURBO_STEER_STATS	= 4,
	ATH12K_HTT_TXBF_MAX_NUM_OF_MODES	= 5
};

enum ath12k_htt_stats_sounding_tx_mode {
	ATH12K_HTT_TX_AC_SOUNDING_MODE		= 0,
	ATH12K_HTT_TX_AX_SOUNDING_MODE		= 1,
	ATH12K_HTT_TX_BE_SOUNDING_MODE		= 2,
	ATH12K_HTT_TX_CMN_SOUNDING_MODE		= 3,
};

struct ath12k_htt_tx_sounding_stats_tlv {
	__le32 tx_sounding_mode;
	__le32 cbf_20[ATH12K_HTT_TXBF_MAX_NUM_OF_MODES];
	__le32 cbf_40[ATH12K_HTT_TXBF_MAX_NUM_OF_MODES];
	__le32 cbf_80[ATH12K_HTT_TXBF_MAX_NUM_OF_MODES];
	__le32 cbf_160[ATH12K_HTT_TXBF_MAX_NUM_OF_MODES];
	__le32 sounding[ATH12K_HTT_TX_NUM_OF_SOUNDING_STATS_WORDS];
	__le32 cv_nc_mismatch_err;
	__le32 cv_fcs_err;
	__le32 cv_frag_idx_mismatch;
	__le32 cv_invalid_peer_id;
	__le32 cv_no_txbf_setup;
	__le32 cv_expiry_in_update;
	__le32 cv_pkt_bw_exceed;
	__le32 cv_dma_not_done_err;
	__le32 cv_update_failed;
	__le32 cv_total_query;
	__le32 cv_total_pattern_query;
	__le32 cv_total_bw_query;
	__le32 cv_invalid_bw_coding;
	__le32 cv_forced_sounding;
	__le32 cv_standalone_sounding;
	__le32 cv_nc_mismatch;
	__le32 cv_fb_type_mismatch;
	__le32 cv_ofdma_bw_mismatch;
	__le32 cv_bw_mismatch;
	__le32 cv_pattern_mismatch;
	__le32 cv_preamble_mismatch;
	__le32 cv_nr_mismatch;
	__le32 cv_in_use_cnt_exceeded;
	__le32 cv_found;
	__le32 cv_not_found;
	__le32 sounding_320[ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS];
	__le32 cbf_320[ATH12K_HTT_TXBF_MAX_NUM_OF_MODES];
	__le32 cv_ntbr_sounding;
	__le32 cv_found_upload_in_progress;
	__le32 cv_expired_during_query;
	__le32 cv_dma_timeout_error;
	__le32 cv_buf_ibf_uploads;
	__le32 cv_buf_ebf_uploads;
	__le32 cv_buf_received;
	__le32 cv_buf_fed_back;
	__le32 cv_total_query_ibf;
	__le32 cv_found_ibf;
	__le32 cv_not_found_ibf;
	__le32 cv_expired_during_query_ibf;
} __packed;

struct ath12k_htt_pdev_obss_pd_stats_tlv {
	__le32 num_obss_tx_ppdu_success;
	__le32 num_obss_tx_ppdu_failure;
	__le32 num_sr_tx_transmissions;
	__le32 num_spatial_reuse_opportunities;
	__le32 num_non_srg_opportunities;
	__le32 num_non_srg_ppdu_tried;
	__le32 num_non_srg_ppdu_success;
	__le32 num_srg_opportunities;
	__le32 num_srg_ppdu_tried;
	__le32 num_srg_ppdu_success;
	__le32 num_psr_opportunities;
	__le32 num_psr_ppdu_tried;
	__le32 num_psr_ppdu_success;
	__le32 num_non_srg_tried_per_ac[ATH12K_HTT_NUM_AC_WMM];
	__le32 num_non_srg_success_ac[ATH12K_HTT_NUM_AC_WMM];
	__le32 num_srg_tried_per_ac[ATH12K_HTT_NUM_AC_WMM];
	__le32 num_srg_success_per_ac[ATH12K_HTT_NUM_AC_WMM];
	__le32 num_obss_min_dur_check_flush_cnt;
	__le32 num_sr_ppdu_abort_flush_cnt;
} __packed;

#define ATH12K_HTT_STATS_MAX_PROF_STATS_NAME_LEN	32
#define ATH12K_HTT_LATENCY_PROFILE_NUM_MAX_HIST		3
#define ATH12K_HTT_INTERRUPTS_LATENCY_PROFILE_MAX_HIST	3

struct ath12k_htt_latency_prof_stats_tlv {
	__le32 print_header;
	s8 latency_prof_name[ATH12K_HTT_STATS_MAX_PROF_STATS_NAME_LEN];
	__le32 cnt;
	__le32 min;
	__le32 max;
	__le32 last;
	__le32 tot;
	__le32 avg;
	__le32 hist_intvl;
	__le32 hist[ATH12K_HTT_LATENCY_PROFILE_NUM_MAX_HIST];
}  __packed;

struct ath12k_htt_latency_prof_ctx_tlv {
	__le32 duration;
	__le32 tx_msdu_cnt;
	__le32 tx_mpdu_cnt;
	__le32 tx_ppdu_cnt;
	__le32 rx_msdu_cnt;
	__le32 rx_mpdu_cnt;
} __packed;

struct ath12k_htt_latency_prof_cnt_tlv {
	__le32 prof_enable_cnt;
} __packed;

#define ATH12K_HTT_RX_NUM_MCS_CNTRS		12
#define ATH12K_HTT_RX_NUM_GI_CNTRS		4
#define ATH12K_HTT_RX_NUM_SPATIAL_STREAMS	8
#define ATH12K_HTT_RX_NUM_BW_CNTRS		4
#define ATH12K_HTT_RX_NUM_RU_SIZE_CNTRS		6
#define ATH12K_HTT_RX_NUM_RU_SIZE_160MHZ_CNTRS	7
#define ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK	5
#define ATH12K_HTT_RX_NUM_REDUCED_CHAN_TYPES	2
#define ATH12K_HTT_RX_NUM_EXTRA_MCS_CNTRS	2

enum ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE {
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_26,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_52,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_106,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_242,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_484,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_996,
	ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_996x2,
	ATH12K_HTT_TX_RX_PDEV_STATS_NUM_AX_RU_SIZE_CNTRS,
};

struct ath12k_htt_rx_pdev_ul_ofdma_user_stats_tlv {
	__le32 user_index;
	__le32 rx_ulofdma_non_data_ppdu;
	__le32 rx_ulofdma_data_ppdu;
	__le32 rx_ulofdma_mpdu_ok;
	__le32 rx_ulofdma_mpdu_fail;
	__le32 rx_ulofdma_non_data_nusers;
	__le32 rx_ulofdma_data_nusers;
} __packed;

struct ath12k_htt_rx_pdev_ul_trigger_stats_tlv {
	__le32 mac_id__word;
	__le32 rx_11ax_ul_ofdma;
	__le32 ul_ofdma_rx_mcs[ATH12K_HTT_RX_NUM_MCS_CNTRS];
	__le32 ul_ofdma_rx_gi[ATH12K_HTT_RX_NUM_GI_CNTRS][ATH12K_HTT_RX_NUM_MCS_CNTRS];
	__le32 ul_ofdma_rx_nss[ATH12K_HTT_RX_NUM_SPATIAL_STREAMS];
	__le32 ul_ofdma_rx_bw[ATH12K_HTT_RX_NUM_BW_CNTRS];
	__le32 ul_ofdma_rx_stbc;
	__le32 ul_ofdma_rx_ldpc;
	__le32 data_ru_size_ppdu[ATH12K_HTT_RX_NUM_RU_SIZE_160MHZ_CNTRS];
	__le32 non_data_ru_size_ppdu[ATH12K_HTT_RX_NUM_RU_SIZE_160MHZ_CNTRS];
	__le32 uplink_sta_aid[ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK];
	__le32 uplink_sta_target_rssi[ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK];
	__le32 uplink_sta_fd_rssi[ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK];
	__le32 uplink_sta_power_headroom[ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK];
	__le32 red_bw[ATH12K_HTT_RX_NUM_REDUCED_CHAN_TYPES][ATH12K_HTT_RX_NUM_BW_CNTRS];
	__le32 ul_ofdma_bsc_trig_rx_qos_null_only;
} __packed;

#define ATH12K_HTT_TX_UL_MUMIMO_USER_STATS	8

struct ath12k_htt_rx_ul_mumimo_trig_stats_tlv {
	__le32 mac_id__word;
	__le32 rx_11ax_ul_mumimo;
	__le32 ul_mumimo_rx_mcs[ATH12K_HTT_RX_NUM_MCS_CNTRS];
	__le32 ul_rx_gi[ATH12K_HTT_RX_NUM_GI_CNTRS][ATH12K_HTT_RX_NUM_MCS_CNTRS];
	__le32 ul_mumimo_rx_nss[ATH12K_HTT_RX_NUM_SPATIAL_STREAMS];
	__le32 ul_mumimo_rx_bw[ATH12K_HTT_RX_NUM_BW_CNTRS];
	__le32 ul_mumimo_rx_stbc;
	__le32 ul_mumimo_rx_ldpc;
	__le32 ul_mumimo_rx_mcs_ext[ATH12K_HTT_RX_NUM_EXTRA_MCS_CNTRS];
	__le32 ul_gi_ext[ATH12K_HTT_RX_NUM_GI_CNTRS][ATH12K_HTT_RX_NUM_EXTRA_MCS_CNTRS];
	s8 ul_rssi[ATH12K_HTT_RX_NUM_SPATIAL_STREAMS][ATH12K_HTT_RX_NUM_BW_CNTRS];
	s8 tgt_rssi[ATH12K_HTT_TX_UL_MUMIMO_USER_STATS][ATH12K_HTT_RX_NUM_BW_CNTRS];
	s8 fd[ATH12K_HTT_TX_UL_MUMIMO_USER_STATS][ATH12K_HTT_RX_NUM_SPATIAL_STREAMS];
	s8 db[ATH12K_HTT_TX_UL_MUMIMO_USER_STATS][ATH12K_HTT_RX_NUM_SPATIAL_STREAMS];
	__le32 red_bw[ATH12K_HTT_RX_NUM_REDUCED_CHAN_TYPES][ATH12K_HTT_RX_NUM_BW_CNTRS];
	__le32 mumimo_bsc_trig_rx_qos_null_only;
} __packed;

#define ATH12K_HTT_RX_NUM_MAX_PEAK_OCCUPANCY_INDEX	10
#define ATH12K_HTT_RX_NUM_MAX_CURR_OCCUPANCY_INDEX	10
#define ATH12K_HTT_RX_NUM_SQUARE_INDEX			6
#define ATH12K_HTT_RX_NUM_MAX_PEAK_SEARCH_INDEX		4
#define ATH12K_HTT_RX_NUM_MAX_PENDING_SEARCH_INDEX	4

struct ath12k_htt_rx_fse_stats_tlv {
	__le32 fse_enable_cnt;
	__le32 fse_disable_cnt;
	__le32 fse_cache_invalidate_entry_cnt;
	__le32 fse_full_cache_invalidate_cnt;
	__le32 fse_num_cache_hits_cnt;
	__le32 fse_num_searches_cnt;
	__le32 fse_cache_occupancy_peak_cnt[ATH12K_HTT_RX_NUM_MAX_PEAK_OCCUPANCY_INDEX];
	__le32 fse_cache_occupancy_curr_cnt[ATH12K_HTT_RX_NUM_MAX_CURR_OCCUPANCY_INDEX];
	__le32 fse_search_stat_square_cnt[ATH12K_HTT_RX_NUM_SQUARE_INDEX];
	__le32 fse_search_stat_peak_cnt[ATH12K_HTT_RX_NUM_MAX_PEAK_SEARCH_INDEX];
	__le32 fse_search_stat_pending_cnt[ATH12K_HTT_RX_NUM_MAX_PENDING_SEARCH_INDEX];
} __packed;

#define ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS		14
#define ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_OFDM_STATS		8
#define ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS		8
#define ATH12K_HTT_TXBF_NUM_BW_CNTRS				5
#define ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES			2

struct ath12k_htt_pdev_txrate_txbf_stats_tlv {
	__le32 tx_su_txbf_mcs[ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS];
	__le32 tx_su_ibf_mcs[ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS];
	__le32 tx_su_ol_mcs[ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS];
	__le32 tx_su_txbf_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 tx_su_ibf_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 tx_su_ol_nss[ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS];
	__le32 tx_su_txbf_bw[ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 tx_su_ibf_bw[ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 tx_su_ol_bw[ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 tx_legacy_ofdm_rate[ATH12K_HTT_TX_PDEV_STATS_NUM_LEGACY_OFDM_STATS];
	__le32 txbf[ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES][ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 ibf[ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES][ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 ol[ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES][ATH12K_HTT_TXBF_NUM_BW_CNTRS];
	__le32 txbf_flag_set_mu_mode;
	__le32 txbf_flag_set_final_status;
	__le32 txbf_flag_not_set_verified_txbf_mode;
	__le32 txbf_flag_not_set_disable_p2p_access;
	__le32 txbf_flag_not_set_max_nss_in_he160;
	__le32 txbf_flag_not_set_disable_uldlofdma;
	__le32 txbf_flag_not_set_mcs_threshold_val;
	__le32 txbf_flag_not_set_final_status;
} __packed;

struct ath12k_htt_txbf_ofdma_ax_ndpa_stats_elem_t {
	__le32 ax_ofdma_ndpa_queued;
	__le32 ax_ofdma_ndpa_tried;
	__le32 ax_ofdma_ndpa_flush;
	__le32 ax_ofdma_ndpa_err;
} __packed;

struct ath12k_htt_txbf_ofdma_ax_ndpa_stats_tlv {
	__le32 num_elems_ax_ndpa_arr;
	__le32 arr_elem_size_ax_ndpa;
	DECLARE_FLEX_ARRAY(struct ath12k_htt_txbf_ofdma_ax_ndpa_stats_elem_t, ax_ndpa);
} __packed;

struct ath12k_htt_txbf_ofdma_ax_ndp_stats_elem_t {
	__le32 ax_ofdma_ndp_queued;
	__le32 ax_ofdma_ndp_tried;
	__le32 ax_ofdma_ndp_flush;
	__le32 ax_ofdma_ndp_err;
} __packed;

struct ath12k_htt_txbf_ofdma_ax_ndp_stats_tlv {
	__le32 num_elems_ax_ndp_arr;
	__le32 arr_elem_size_ax_ndp;
	DECLARE_FLEX_ARRAY(struct ath12k_htt_txbf_ofdma_ax_ndp_stats_elem_t, ax_ndp);
} __packed;

struct ath12k_htt_txbf_ofdma_ax_brp_stats_elem_t {
	__le32 ax_ofdma_brp_queued;
	__le32 ax_ofdma_brp_tried;
	__le32 ax_ofdma_brp_flushed;
	__le32 ax_ofdma_brp_err;
	__le32 ax_ofdma_num_cbf_rcvd;
} __packed;

struct ath12k_htt_txbf_ofdma_ax_brp_stats_tlv {
	__le32 num_elems_ax_brp_arr;
	__le32 arr_elem_size_ax_brp;
	DECLARE_FLEX_ARRAY(struct ath12k_htt_txbf_ofdma_ax_brp_stats_elem_t, ax_brp);
} __packed;

struct ath12k_htt_txbf_ofdma_ax_steer_stats_elem_t {
	__le32 num_ppdu_steer;
	__le32 num_ppdu_ol;
	__le32 num_usr_prefetch;
	__le32 num_usr_sound;
	__le32 num_usr_force_sound;
} __packed;

struct ath12k_htt_txbf_ofdma_ax_steer_stats_tlv {
	__le32 num_elems_ax_steer_arr;
	__le32 arr_elem_size_ax_steer;
	DECLARE_FLEX_ARRAY(struct ath12k_htt_txbf_ofdma_ax_steer_stats_elem_t, ax_steer);
} __packed;

struct ath12k_htt_txbf_ofdma_ax_steer_mpdu_stats_tlv {
	__le32 ax_ofdma_rbo_steer_mpdus_tried;
	__le32 ax_ofdma_rbo_steer_mpdus_failed;
	__le32 ax_ofdma_sifs_steer_mpdus_tried;
	__le32 ax_ofdma_sifs_steer_mpdus_failed;
} __packed;

enum ath12k_htt_stats_page_lock_state {
	ATH12K_HTT_STATS_PAGE_LOCKED	= 0,
	ATH12K_HTT_STATS_PAGE_UNLOCKED	= 1,
	ATH12K_NUM_PG_LOCK_STATE
};

#define ATH12K_PAGER_MAX	10

#define ATH12K_HTT_DLPAGER_ASYNC_LOCK_PG_CNT_INFO0	GENMASK(7, 0)
#define ATH12K_HTT_DLPAGER_SYNC_LOCK_PG_CNT_INFO0	GENMASK(15, 8)
#define ATH12K_HTT_DLPAGER_TOTAL_LOCK_PAGES_INFO1	GENMASK(15, 0)
#define ATH12K_HTT_DLPAGER_TOTAL_FREE_PAGES_INFO1	GENMASK(31, 16)
#define ATH12K_HTT_DLPAGER_TOTAL_LOCK_PAGES_INFO2	GENMASK(15, 0)
#define ATH12K_HTT_DLPAGER_TOTAL_FREE_PAGES_INFO2	GENMASK(31, 16)

struct ath12k_htt_pgs_info {
	__le32 page_num;
	__le32 num_pgs;
	__le32 ts_lsb;
	__le32 ts_msb;
} __packed;

struct ath12k_htt_dl_pager_stats_tlv {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	struct ath12k_htt_pgs_info pgs_info[ATH12K_NUM_PG_LOCK_STATE][ATH12K_PAGER_MAX];
} __packed;

#define ATH12K_HTT_STATS_MAX_CHAINS		8
#define ATH12K_HTT_MAX_RX_PKT_CNT		8
#define ATH12K_HTT_MAX_RX_PKT_CRC_PASS_CNT	8
#define ATH12K_HTT_MAX_PER_BLK_ERR_CNT		20
#define ATH12K_HTT_MAX_RX_OTA_ERR_CNT		14
#define ATH12K_HTT_MAX_CH_PWR_INFO_SIZE		16

struct ath12k_htt_phy_stats_tlv {
	a_sle32 nf_chain[ATH12K_HTT_STATS_MAX_CHAINS];
	__le32 false_radar_cnt;
	__le32 radar_cs_cnt;
	a_sle32 ani_level;
	__le32 fw_run_time;
	a_sle32 runtime_nf_chain[ATH12K_HTT_STATS_MAX_CHAINS];
} __packed;

struct ath12k_htt_phy_counters_tlv {
	__le32 rx_ofdma_timing_err_cnt;
	__le32 rx_cck_fail_cnt;
	__le32 mactx_abort_cnt;
	__le32 macrx_abort_cnt;
	__le32 phytx_abort_cnt;
	__le32 phyrx_abort_cnt;
	__le32 phyrx_defer_abort_cnt;
	__le32 rx_gain_adj_lstf_event_cnt;
	__le32 rx_gain_adj_non_legacy_cnt;
	__le32 rx_pkt_cnt[ATH12K_HTT_MAX_RX_PKT_CNT];
	__le32 rx_pkt_crc_pass_cnt[ATH12K_HTT_MAX_RX_PKT_CRC_PASS_CNT];
	__le32 per_blk_err_cnt[ATH12K_HTT_MAX_PER_BLK_ERR_CNT];
	__le32 rx_ota_err_cnt[ATH12K_HTT_MAX_RX_OTA_ERR_CNT];
} __packed;

struct ath12k_htt_phy_reset_stats_tlv {
	__le32 pdev_id;
	__le32 chan_mhz;
	__le32 chan_band_center_freq1;
	__le32 chan_band_center_freq2;
	__le32 chan_phy_mode;
	__le32 chan_flags;
	__le32 chan_num;
	__le32 reset_cause;
	__le32 prev_reset_cause;
	__le32 phy_warm_reset_src;
	__le32 rx_gain_tbl_mode;
	__le32 xbar_val;
	__le32 force_calibration;
	__le32 phyrf_mode;
	__le32 phy_homechan;
	__le32 phy_tx_ch_mask;
	__le32 phy_rx_ch_mask;
	__le32 phybb_ini_mask;
	__le32 phyrf_ini_mask;
	__le32 phy_dfs_en_mask;
	__le32 phy_sscan_en_mask;
	__le32 phy_synth_sel_mask;
	__le32 phy_adfs_freq;
	__le32 cck_fir_settings;
	__le32 phy_dyn_pri_chan;
	__le32 cca_thresh;
	__le32 dyn_cca_status;
	__le32 rxdesense_thresh_hw;
	__le32 rxdesense_thresh_sw;
} __packed;

struct ath12k_htt_phy_reset_counters_tlv {
	__le32 pdev_id;
	__le32 cf_active_low_fail_cnt;
	__le32 cf_active_low_pass_cnt;
	__le32 phy_off_through_vreg_cnt;
	__le32 force_calibration_cnt;
	__le32 rf_mode_switch_phy_off_cnt;
	__le32 temperature_recal_cnt;
} __packed;

struct ath12k_htt_phy_tpc_stats_tlv {
	__le32 pdev_id;
	__le32 tx_power_scale;
	__le32 tx_power_scale_db;
	__le32 min_negative_tx_power;
	__le32 reg_ctl_domain;
	__le32 max_reg_allowed_power[ATH12K_HTT_STATS_MAX_CHAINS];
	__le32 max_reg_allowed_power_6ghz[ATH12K_HTT_STATS_MAX_CHAINS];
	__le32 twice_max_rd_power;
	__le32 max_tx_power;
	__le32 home_max_tx_power;
	__le32 psd_power;
	__le32 eirp_power;
	__le32 power_type_6ghz;
	__le32 sub_band_cfreq[ATH12K_HTT_MAX_CH_PWR_INFO_SIZE];
	__le32 sub_band_txpower[ATH12K_HTT_MAX_CH_PWR_INFO_SIZE];
} __packed;

struct ath12k_htt_t2h_soc_txrx_stats_common_tlv {
	__le32 inv_peers_msdu_drop_count_hi;
	__le32 inv_peers_msdu_drop_count_lo;
} __packed;

#define ATH12K_HTT_AST_PDEV_ID_INFO		GENMASK(1, 0)
#define ATH12K_HTT_AST_VDEV_ID_INFO		GENMASK(9, 2)
#define ATH12K_HTT_AST_NEXT_HOP_INFO		BIT(10)
#define ATH12K_HTT_AST_MCAST_INFO		BIT(11)
#define ATH12K_HTT_AST_MONITOR_DIRECT_INFO	BIT(12)
#define ATH12K_HTT_AST_MESH_STA_INFO		BIT(13)
#define ATH12K_HTT_AST_MEC_INFO			BIT(14)
#define ATH12K_HTT_AST_INTRA_BSS_INFO		BIT(15)

struct ath12k_htt_ast_entry_tlv {
	__le32 sw_peer_id;
	__le32 ast_index;
	struct htt_mac_addr mac_addr;
	__le32 info;
} __packed;

enum ath12k_htt_stats_direction {
	ATH12K_HTT_STATS_DIRECTION_TX,
	ATH12K_HTT_STATS_DIRECTION_RX
};

enum ath12k_htt_stats_ppdu_type {
	ATH12K_HTT_STATS_PPDU_TYPE_MODE_SU,
	ATH12K_HTT_STATS_PPDU_TYPE_DL_MU_MIMO,
	ATH12K_HTT_STATS_PPDU_TYPE_UL_MU_MIMO,
	ATH12K_HTT_STATS_PPDU_TYPE_DL_MU_OFDMA,
	ATH12K_HTT_STATS_PPDU_TYPE_UL_MU_OFDMA
};

enum ath12k_htt_stats_param_type {
	ATH12K_HTT_STATS_PREAM_OFDM,
	ATH12K_HTT_STATS_PREAM_CCK,
	ATH12K_HTT_STATS_PREAM_HT,
	ATH12K_HTT_STATS_PREAM_VHT,
	ATH12K_HTT_STATS_PREAM_HE,
	ATH12K_HTT_STATS_PREAM_EHT,
	ATH12K_HTT_STATS_PREAM_RSVD1,
	ATH12K_HTT_STATS_PREAM_COUNT,
};

#define ATH12K_HTT_PUNCT_STATS_MAX_SUBBAND_CNT	32

struct ath12k_htt_pdev_puncture_stats_tlv {
	__le32 mac_id__word;
	__le32 direction;
	__le32 preamble;
	__le32 ppdu_type;
	__le32 subband_cnt;
	__le32 last_used_pattern_mask;
	__le32 num_subbands_used_cnt[ATH12K_HTT_PUNCT_STATS_MAX_SUBBAND_CNT];
} __packed;

struct ath12k_htt_dmac_reset_stats_tlv {
	__le32 reset_count;
	__le32 reset_time_lo_ms;
	__le32 reset_time_hi_ms;
	__le32 disengage_time_lo_ms;
	__le32 disengage_time_hi_ms;
	__le32 engage_time_lo_ms;
	__le32 engage_time_hi_ms;
	__le32 disengage_count;
	__le32 engage_count;
	__le32 drain_dest_ring_mask;
} __packed;

struct ath12k_htt_pdev_sched_algo_ofdma_stats_tlv {
	__le32 mac_id__word;
	__le32 rate_based_dlofdma_enabled_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 rate_based_dlofdma_disabled_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 rate_based_dlofdma_probing_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 rate_based_dlofdma_monitor_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 chan_acc_lat_based_dlofdma_enabled_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 chan_acc_lat_based_dlofdma_disabled_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 chan_acc_lat_based_dlofdma_monitor_cnt[ATH12K_HTT_NUM_AC_WMM];
	__le32 downgrade_to_dl_su_ru_alloc_fail[ATH12K_HTT_NUM_AC_WMM];
	__le32 candidate_list_single_user_disable_ofdma[ATH12K_HTT_NUM_AC_WMM];
	__le32 dl_cand_list_dropped_high_ul_qos_weight[ATH12K_HTT_NUM_AC_WMM];
	__le32 ax_dlofdma_disabled_due_to_pipelining[ATH12K_HTT_NUM_AC_WMM];
	__le32 dlofdma_disabled_su_only_eligible[ATH12K_HTT_NUM_AC_WMM];
	__le32 dlofdma_disabled_consec_no_mpdus_tried[ATH12K_HTT_NUM_AC_WMM];
	__le32 dlofdma_disabled_consec_no_mpdus_success[ATH12K_HTT_NUM_AC_WMM];
} __packed;

#define ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS		4
#define ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS	8
#define ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS		14

enum ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE {
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_26,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_52,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_52_26,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_106,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_106_26,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_242,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_484,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_484_242,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996_484,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996_484_242,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x2,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x2_484,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x3,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x3_484,
	ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x4,
	ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS,
};

enum ATH12K_HTT_RC_MODE {
	ATH12K_HTT_RC_MODE_SU_OL,
	ATH12K_HTT_RC_MODE_SU_BF,
	ATH12K_HTT_RC_MODE_MU1_INTF,
	ATH12K_HTT_RC_MODE_MU2_INTF,
	ATH12K_HTT_RC_MODE_MU3_INTF,
	ATH12K_HTT_RC_MODE_MU4_INTF,
	ATH12K_HTT_RC_MODE_MU5_INTF,
	ATH12K_HTT_RC_MODE_MU6_INTF,
	ATH12K_HTT_RC_MODE_MU7_INTF,
	ATH12K_HTT_RC_MODE_2D_COUNT
};

enum ath12k_htt_stats_rc_mode {
	ATH12K_HTT_STATS_RC_MODE_DLSU     = 0,
	ATH12K_HTT_STATS_RC_MODE_DLMUMIMO = 1,
	ATH12K_HTT_STATS_RC_MODE_DLOFDMA  = 2,
	ATH12K_HTT_STATS_RC_MODE_ULMUMIMO = 3,
	ATH12K_HTT_STATS_RC_MODE_ULOFDMA  = 4,
};

enum ath12k_htt_stats_ru_type {
	ATH12K_HTT_STATS_RU_TYPE_INVALID,
	ATH12K_HTT_STATS_RU_TYPE_SINGLE_RU_ONLY,
	ATH12K_HTT_STATS_RU_TYPE_SINGLE_AND_MULTI_RU,
};

struct ath12k_htt_tx_rate_stats {
	__le32 ppdus_tried;
	__le32 ppdus_ack_failed;
	__le32 mpdus_tried;
	__le32 mpdus_failed;
} __packed;

struct ath12k_htt_tx_per_rate_stats_tlv {
	__le32 rc_mode;
	__le32 last_probed_mcs;
	__le32 last_probed_nss;
	__le32 last_probed_bw;
	struct ath12k_htt_tx_rate_stats per_bw[ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS];
	struct ath12k_htt_tx_rate_stats per_nss[ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS];
	struct ath12k_htt_tx_rate_stats per_mcs[ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS];
	struct ath12k_htt_tx_rate_stats per_bw320;
	__le32 probe_cnt[ATH12K_HTT_RC_MODE_2D_COUNT];
	__le32 ru_type;
	struct ath12k_htt_tx_rate_stats ru[ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS];
} __packed;

#define ATH12K_HTT_TX_PDEV_NUM_BE_MCS_CNTRS		16
#define ATH12K_HTT_TX_PDEV_NUM_BE_BW_CNTRS		5
#define ATH12K_HTT_TX_PDEV_NUM_EHT_SIG_MCS_CNTRS	4
#define ATH12K_HTT_TX_PDEV_NUM_GI_CNTRS			4

struct ath12k_htt_tx_pdev_rate_stats_be_ofdma_tlv {
	__le32 mac_id__word;
	__le32 be_ofdma_tx_ldpc;
	__le32 be_ofdma_tx_mcs[ATH12K_HTT_TX_PDEV_NUM_BE_MCS_CNTRS];
	__le32 be_ofdma_tx_nss[ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS];
	__le32 be_ofdma_tx_bw[ATH12K_HTT_TX_PDEV_NUM_BE_BW_CNTRS];
	__le32 gi[ATH12K_HTT_TX_PDEV_NUM_GI_CNTRS][ATH12K_HTT_TX_PDEV_NUM_BE_MCS_CNTRS];
	__le32 be_ofdma_tx_ru_size[ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS];
	__le32 be_ofdma_eht_sig_mcs[ATH12K_HTT_TX_PDEV_NUM_EHT_SIG_MCS_CNTRS];
} __packed;

struct ath12k_htt_pdev_mbssid_ctrl_frame_tlv {
	__le32 mac_id__word;
	__le32 basic_trigger_across_bss;
	__le32 basic_trigger_within_bss;
	__le32 bsr_trigger_across_bss;
	__le32 bsr_trigger_within_bss;
	__le32 mu_rts_across_bss;
	__le32 mu_rts_within_bss;
	__le32 ul_mumimo_trigger_across_bss;
	__le32 ul_mumimo_trigger_within_bss;
} __packed;

#endif
