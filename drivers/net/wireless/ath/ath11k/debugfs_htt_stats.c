// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "debug.h"
#include "debugfs_htt_stats.h"

#define HTT_MAX_PRINT_CHAR_PER_ELEM 15

#define HTT_TLV_HDR_LEN 4

#define PRINT_ARRAY_TO_BUF(out, buflen, arr, str, len, newline)				\
	do {										\
		int index = 0; u8 i; const char *str_val = str;				\
		const char *new_line = newline;						\
		if (str_val) {								\
			index += scnprintf((out + buflen),				\
				 (ATH11K_HTT_STATS_BUF_SIZE - buflen),			\
				 "%s = ", str_val);					\
		}									\
		for (i = 0; i < len; i++) {						\
			index += scnprintf((out + buflen) + index,			\
				 (ATH11K_HTT_STATS_BUF_SIZE - buflen) - index,		\
				 " %u:%u,", i, arr[i]);					\
		}									\
		index += scnprintf((out + buflen) + index,				\
			 (ATH11K_HTT_STATS_BUF_SIZE - buflen) - index,			\
			  "%s", new_line);						\
		buflen += index;							\
	} while (0)

static inline void htt_print_stats_string_tlv(const void *tag_buf,
					      u16 tag_len,
					      struct debug_htt_stats_req *stats_req)
{
	const struct htt_stats_string_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8  i;

	tag_len = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_STRING_TLV:\n");

	len += scnprintf(buf + len, buf_len - len,
			 "data = ");
	for (i = 0; i < tag_len; i++) {
		len += scnprintf(buf + len,
				 buf_len - len,
				 "%.*s", 4, (char *)&(htt_stats_buf->data[i]));
	}
	/* New lines are added for better display */
	len += scnprintf(buf + len, buf_len - len, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_pdev_stats_cmn_tlv(const void *tag_buf,
						   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "hw_queued = %u\n",
			 htt_stats_buf->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "hw_reaped = %u\n",
			 htt_stats_buf->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "underrun = %u\n",
			 htt_stats_buf->underrun);
	len += scnprintf(buf + len, buf_len - len, "hw_paused = %u\n",
			 htt_stats_buf->hw_paused);
	len += scnprintf(buf + len, buf_len - len, "hw_flush = %u\n",
			 htt_stats_buf->hw_flush);
	len += scnprintf(buf + len, buf_len - len, "hw_filt = %u\n",
			 htt_stats_buf->hw_filt);
	len += scnprintf(buf + len, buf_len - len, "tx_abort = %u\n",
			 htt_stats_buf->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "mpdu_requeued = %u\n",
			 htt_stats_buf->mpdu_requeued);
	len += scnprintf(buf + len, buf_len - len, "tx_xretry = %u\n",
			 htt_stats_buf->tx_xretry);
	len += scnprintf(buf + len, buf_len - len, "data_rc = %u\n",
			 htt_stats_buf->data_rc);
	len += scnprintf(buf + len, buf_len - len, "mpdu_dropped_xretry = %u\n",
			 htt_stats_buf->mpdu_dropped_xretry);
	len += scnprintf(buf + len, buf_len - len, "illegal_rate_phy_err = %u\n",
			 htt_stats_buf->illgl_rate_phy_err);
	len += scnprintf(buf + len, buf_len - len, "cont_xretry = %u\n",
			 htt_stats_buf->cont_xretry);
	len += scnprintf(buf + len, buf_len - len, "tx_timeout = %u\n",
			 htt_stats_buf->tx_timeout);
	len += scnprintf(buf + len, buf_len - len, "pdev_resets = %u\n",
			 htt_stats_buf->pdev_resets);
	len += scnprintf(buf + len, buf_len - len, "phy_underrun = %u\n",
			 htt_stats_buf->phy_underrun);
	len += scnprintf(buf + len, buf_len - len, "txop_ovf = %u\n",
			 htt_stats_buf->txop_ovf);
	len += scnprintf(buf + len, buf_len - len, "seq_posted = %u\n",
			 htt_stats_buf->seq_posted);
	len += scnprintf(buf + len, buf_len - len, "seq_failed_queueing = %u\n",
			 htt_stats_buf->seq_failed_queueing);
	len += scnprintf(buf + len, buf_len - len, "seq_completed = %u\n",
			 htt_stats_buf->seq_completed);
	len += scnprintf(buf + len, buf_len - len, "seq_restarted = %u\n",
			 htt_stats_buf->seq_restarted);
	len += scnprintf(buf + len, buf_len - len, "mu_seq_posted = %u\n",
			 htt_stats_buf->mu_seq_posted);
	len += scnprintf(buf + len, buf_len - len, "seq_switch_hw_paused = %u\n",
			 htt_stats_buf->seq_switch_hw_paused);
	len += scnprintf(buf + len, buf_len - len, "next_seq_posted_dsr = %u\n",
			 htt_stats_buf->next_seq_posted_dsr);
	len += scnprintf(buf + len, buf_len - len, "seq_posted_isr = %u\n",
			 htt_stats_buf->seq_posted_isr);
	len += scnprintf(buf + len, buf_len - len, "seq_ctrl_cached = %u\n",
			 htt_stats_buf->seq_ctrl_cached);
	len += scnprintf(buf + len, buf_len - len, "mpdu_count_tqm = %u\n",
			 htt_stats_buf->mpdu_count_tqm);
	len += scnprintf(buf + len, buf_len - len, "msdu_count_tqm = %u\n",
			 htt_stats_buf->msdu_count_tqm);
	len += scnprintf(buf + len, buf_len - len, "mpdu_removed_tqm = %u\n",
			 htt_stats_buf->mpdu_removed_tqm);
	len += scnprintf(buf + len, buf_len - len, "msdu_removed_tqm = %u\n",
			 htt_stats_buf->msdu_removed_tqm);
	len += scnprintf(buf + len, buf_len - len, "mpdus_sw_flush = %u\n",
			 htt_stats_buf->mpdus_sw_flush);
	len += scnprintf(buf + len, buf_len - len, "mpdus_hw_filter = %u\n",
			 htt_stats_buf->mpdus_hw_filter);
	len += scnprintf(buf + len, buf_len - len, "mpdus_truncated = %u\n",
			 htt_stats_buf->mpdus_truncated);
	len += scnprintf(buf + len, buf_len - len, "mpdus_ack_failed = %u\n",
			 htt_stats_buf->mpdus_ack_failed);
	len += scnprintf(buf + len, buf_len - len, "mpdus_expired = %u\n",
			 htt_stats_buf->mpdus_expired);
	len += scnprintf(buf + len, buf_len - len, "mpdus_seq_hw_retry = %u\n",
			 htt_stats_buf->mpdus_seq_hw_retry);
	len += scnprintf(buf + len, buf_len - len, "ack_tlv_proc = %u\n",
			 htt_stats_buf->ack_tlv_proc);
	len += scnprintf(buf + len, buf_len - len, "coex_abort_mpdu_cnt_valid = %u\n",
			 htt_stats_buf->coex_abort_mpdu_cnt_valid);
	len += scnprintf(buf + len, buf_len - len, "coex_abort_mpdu_cnt = %u\n",
			 htt_stats_buf->coex_abort_mpdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "num_total_ppdus_tried_ota = %u\n",
			 htt_stats_buf->num_total_ppdus_tried_ota);
	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_tried_ota = %u\n",
			 htt_stats_buf->num_data_ppdus_tried_ota);
	len += scnprintf(buf + len, buf_len - len, "local_ctrl_mgmt_enqued = %u\n",
			 htt_stats_buf->local_ctrl_mgmt_enqued);
	len += scnprintf(buf + len, buf_len - len, "local_ctrl_mgmt_freed = %u\n",
			 htt_stats_buf->local_ctrl_mgmt_freed);
	len += scnprintf(buf + len, buf_len - len, "local_data_enqued = %u\n",
			 htt_stats_buf->local_data_enqued);
	len += scnprintf(buf + len, buf_len - len, "local_data_freed = %u\n",
			 htt_stats_buf->local_data_freed);
	len += scnprintf(buf + len, buf_len - len, "mpdu_tried = %u\n",
			 htt_stats_buf->mpdu_tried);
	len += scnprintf(buf + len, buf_len - len, "isr_wait_seq_posted = %u\n",
			 htt_stats_buf->isr_wait_seq_posted);
	len += scnprintf(buf + len, buf_len - len, "tx_active_dur_us_low = %u\n",
			 htt_stats_buf->tx_active_dur_us_low);
	len += scnprintf(buf + len, buf_len - len, "tx_active_dur_us_high = %u\n\n",
			 htt_stats_buf->tx_active_dur_us_high);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_urrn_tlv_v(const void *tag_buf,
				   u16 tag_len,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_urrn_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_URRN_STATS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_URRN_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->urrn_stats, "urrn_stats",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_flush_tlv_v(const void *tag_buf,
				    u16 tag_len,
				    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_flush_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_FLUSH_REASON_STATS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_FLUSH_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->flush_errs, "flush_errs",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_sifs_tlv_v(const void *tag_buf,
				   u16 tag_len,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_sifs_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_SIFS_BURST_STATS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_SIFS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sifs_status, "sifs_status",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_phy_err_tlv_v(const void *tag_buf,
				      u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_phy_err_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_PHY_ERR_STATS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_PHY_ERR_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->phy_errs, "phy_errs",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_sifs_hist_tlv_v(const void *tag_buf,
					u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_sifs_hist_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_SIFS_BURST_HIST_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_SIFS_HIST_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sifs_hist_status,
			   "sifs_hist_status", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_tx_ppdu_stats_tlv_v(const void *tag_buf,
					    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_tx_ppdu_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_TX_PPDU_STATS_TLV_V:\n");

	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_legacy_su = %u\n",
			 htt_stats_buf->num_data_ppdus_legacy_su);

	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_ac_su = %u\n",
			 htt_stats_buf->num_data_ppdus_ac_su);

	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_ax_su = %u\n",
			 htt_stats_buf->num_data_ppdus_ax_su);

	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_ac_su_txbf = %u\n",
			 htt_stats_buf->num_data_ppdus_ac_su_txbf);

	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_ax_su_txbf = %u\n\n",
			 htt_stats_buf->num_data_ppdus_ax_su_txbf);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_tried_mpdu_cnt_hist_tlv_v(const void *tag_buf,
						  u16 tag_len,
						  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_tried_mpdu_cnt_hist_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u32  num_elements = ((tag_len - sizeof(htt_stats_buf->hist_bin_size)) >> 2);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_TRIED_MPDU_CNT_HIST_TLV_V:\n");
	len += scnprintf(buf + len, buf_len - len, "TRIED_MPDU_CNT_HIST_BIN_SIZE : %u\n",
			 htt_stats_buf->hist_bin_size);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tried_mpdu_cnt_hist,
			   "tried_mpdu_cnt_hist", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_hw_stats_intr_misc_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_hw_stats_intr_misc_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	char hw_intr_name[HTT_STATS_MAX_HW_INTR_NAME_LEN + 1] = {0};

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_INTR_MISC_TLV:\n");
	memcpy(hw_intr_name, &(htt_stats_buf->hw_intr_name[0]),
	       HTT_STATS_MAX_HW_INTR_NAME_LEN);
	len += scnprintf(buf + len, buf_len - len, "hw_intr_name = %s\n", hw_intr_name);
	len += scnprintf(buf + len, buf_len - len, "mask = %u\n",
			 htt_stats_buf->mask);
	len += scnprintf(buf + len, buf_len - len, "count = %u\n\n",
			 htt_stats_buf->count);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_hw_stats_wd_timeout_tlv(const void *tag_buf,
				  struct debug_htt_stats_req *stats_req)
{
	const struct htt_hw_stats_wd_timeout_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	char hw_module_name[HTT_STATS_MAX_HW_MODULE_NAME_LEN + 1] = {0};

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_WD_TIMEOUT_TLV:\n");
	memcpy(hw_module_name, &(htt_stats_buf->hw_module_name[0]),
	       HTT_STATS_MAX_HW_MODULE_NAME_LEN);
	len += scnprintf(buf + len, buf_len - len, "hw_module_name = %s\n",
			 hw_module_name);
	len += scnprintf(buf + len, buf_len - len, "count = %u\n",
			 htt_stats_buf->count);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_hw_stats_pdev_errs_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_hw_stats_pdev_errs_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_PDEV_ERRS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "tx_abort = %u\n",
			 htt_stats_buf->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "tx_abort_fail_count = %u\n",
			 htt_stats_buf->tx_abort_fail_count);
	len += scnprintf(buf + len, buf_len - len, "rx_abort = %u\n",
			 htt_stats_buf->rx_abort);
	len += scnprintf(buf + len, buf_len - len, "rx_abort_fail_count = %u\n",
			 htt_stats_buf->rx_abort_fail_count);
	len += scnprintf(buf + len, buf_len - len, "warm_reset = %u\n",
			 htt_stats_buf->warm_reset);
	len += scnprintf(buf + len, buf_len - len, "cold_reset = %u\n",
			 htt_stats_buf->cold_reset);
	len += scnprintf(buf + len, buf_len - len, "tx_flush = %u\n",
			 htt_stats_buf->tx_flush);
	len += scnprintf(buf + len, buf_len - len, "tx_glb_reset = %u\n",
			 htt_stats_buf->tx_glb_reset);
	len += scnprintf(buf + len, buf_len - len, "tx_txq_reset = %u\n",
			 htt_stats_buf->tx_txq_reset);
	len += scnprintf(buf + len, buf_len - len, "rx_timeout_reset = %u\n\n",
			 htt_stats_buf->rx_timeout_reset);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_msdu_flow_stats_tlv(const void *tag_buf,
						 struct debug_htt_stats_req *stats_req)
{
	const struct htt_msdu_flow_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_MSDU_FLOW_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "last_update_timestamp = %u\n",
			 htt_stats_buf->last_update_timestamp);
	len += scnprintf(buf + len, buf_len - len, "last_add_timestamp = %u\n",
			 htt_stats_buf->last_add_timestamp);
	len += scnprintf(buf + len, buf_len - len, "last_remove_timestamp = %u\n",
			 htt_stats_buf->last_remove_timestamp);
	len += scnprintf(buf + len, buf_len - len, "total_processed_msdu_count = %u\n",
			 htt_stats_buf->total_processed_msdu_count);
	len += scnprintf(buf + len, buf_len - len, "cur_msdu_count_in_flowq = %u\n",
			 htt_stats_buf->cur_msdu_count_in_flowq);
	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %u\n",
			 htt_stats_buf->sw_peer_id);
	len += scnprintf(buf + len, buf_len - len, "tx_flow_no = %lu\n",
			 FIELD_GET(HTT_MSDU_FLOW_STATS_TX_FLOW_NO,
				   htt_stats_buf->tx_flow_no__tid_num__drop_rule));
	len += scnprintf(buf + len, buf_len - len, "tid_num = %lu\n",
			 FIELD_GET(HTT_MSDU_FLOW_STATS_TID_NUM,
				   htt_stats_buf->tx_flow_no__tid_num__drop_rule));
	len += scnprintf(buf + len, buf_len - len, "drop_rule = %lu\n",
			 FIELD_GET(HTT_MSDU_FLOW_STATS_DROP_RULE,
				   htt_stats_buf->tx_flow_no__tid_num__drop_rule));
	len += scnprintf(buf + len, buf_len - len, "last_cycle_enqueue_count = %u\n",
			 htt_stats_buf->last_cycle_enqueue_count);
	len += scnprintf(buf + len, buf_len - len, "last_cycle_dequeue_count = %u\n",
			 htt_stats_buf->last_cycle_dequeue_count);
	len += scnprintf(buf + len, buf_len - len, "last_cycle_drop_count = %u\n",
			 htt_stats_buf->last_cycle_drop_count);
	len += scnprintf(buf + len, buf_len - len, "current_drop_th = %u\n\n",
			 htt_stats_buf->current_drop_th);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_tid_stats_tlv(const void *tag_buf,
					      struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tid_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	char tid_name[MAX_HTT_TID_NAME + 1] = {0};

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TID_STATS_TLV:\n");
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += scnprintf(buf + len, buf_len - len, "tid_name = %s\n", tid_name);
	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_SW_PEER_ID,
				   htt_stats_buf->sw_peer_id__tid_num));
	len += scnprintf(buf + len, buf_len - len, "tid_num = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_TID_NUM,
				   htt_stats_buf->sw_peer_id__tid_num));
	len += scnprintf(buf + len, buf_len - len, "num_sched_pending = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_NUM_SCHED_PENDING,
				   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq));
	len += scnprintf(buf + len, buf_len - len, "num_ppdu_in_hwq = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_NUM_PPDU_IN_HWQ,
				   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq));
	len += scnprintf(buf + len, buf_len - len, "tid_flags = 0x%x\n",
			 htt_stats_buf->tid_flags);
	len += scnprintf(buf + len, buf_len - len, "hw_queued = %u\n",
			 htt_stats_buf->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "hw_reaped = %u\n",
			 htt_stats_buf->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "mpdus_hw_filter = %u\n",
			 htt_stats_buf->mpdus_hw_filter);
	len += scnprintf(buf + len, buf_len - len, "qdepth_bytes = %u\n",
			 htt_stats_buf->qdepth_bytes);
	len += scnprintf(buf + len, buf_len - len, "qdepth_num_msdu = %u\n",
			 htt_stats_buf->qdepth_num_msdu);
	len += scnprintf(buf + len, buf_len - len, "qdepth_num_mpdu = %u\n",
			 htt_stats_buf->qdepth_num_mpdu);
	len += scnprintf(buf + len, buf_len - len, "last_scheduled_tsmp = %u\n",
			 htt_stats_buf->last_scheduled_tsmp);
	len += scnprintf(buf + len, buf_len - len, "pause_module_id = %u\n",
			 htt_stats_buf->pause_module_id);
	len += scnprintf(buf + len, buf_len - len, "block_module_id = %u\n\n",
			 htt_stats_buf->block_module_id);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_tid_stats_v1_tlv(const void *tag_buf,
						 struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tid_stats_v1_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	char tid_name[MAX_HTT_TID_NAME + 1] = {0};

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TID_STATS_V1_TLV:\n");
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += scnprintf(buf + len, buf_len - len, "tid_name = %s\n", tid_name);
	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_V1_SW_PEER_ID,
				   htt_stats_buf->sw_peer_id__tid_num));
	len += scnprintf(buf + len, buf_len - len, "tid_num = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_V1_TID_NUM,
				   htt_stats_buf->sw_peer_id__tid_num));
	len += scnprintf(buf + len, buf_len - len, "num_sched_pending = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_V1_NUM_SCHED_PENDING,
				   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq));
	len += scnprintf(buf + len, buf_len - len, "num_ppdu_in_hwq = %lu\n",
			 FIELD_GET(HTT_TX_TID_STATS_V1_NUM_PPDU_IN_HWQ,
				   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq));
	len += scnprintf(buf + len, buf_len - len, "tid_flags = 0x%x\n",
			 htt_stats_buf->tid_flags);
	len += scnprintf(buf + len, buf_len - len, "max_qdepth_bytes = %u\n",
			 htt_stats_buf->max_qdepth_bytes);
	len += scnprintf(buf + len, buf_len - len, "max_qdepth_n_msdus = %u\n",
			 htt_stats_buf->max_qdepth_n_msdus);
	len += scnprintf(buf + len, buf_len - len, "rsvd = %u\n",
			 htt_stats_buf->rsvd);
	len += scnprintf(buf + len, buf_len - len, "qdepth_bytes = %u\n",
			 htt_stats_buf->qdepth_bytes);
	len += scnprintf(buf + len, buf_len - len, "qdepth_num_msdu = %u\n",
			 htt_stats_buf->qdepth_num_msdu);
	len += scnprintf(buf + len, buf_len - len, "qdepth_num_mpdu = %u\n",
			 htt_stats_buf->qdepth_num_mpdu);
	len += scnprintf(buf + len, buf_len - len, "last_scheduled_tsmp = %u\n",
			 htt_stats_buf->last_scheduled_tsmp);
	len += scnprintf(buf + len, buf_len - len, "pause_module_id = %u\n",
			 htt_stats_buf->pause_module_id);
	len += scnprintf(buf + len, buf_len - len, "block_module_id = %u\n",
			 htt_stats_buf->block_module_id);
	len += scnprintf(buf + len, buf_len - len, "allow_n_flags = 0x%x\n",
			 htt_stats_buf->allow_n_flags);
	len += scnprintf(buf + len, buf_len - len, "sendn_frms_allowed = %u\n\n",
			 htt_stats_buf->sendn_frms_allowed);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_rx_tid_stats_tlv(const void *tag_buf,
					      struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_tid_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	char tid_name[MAX_HTT_TID_NAME + 1] = {0};

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_TID_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %lu\n",
			 FIELD_GET(HTT_RX_TID_STATS_SW_PEER_ID,
				   htt_stats_buf->sw_peer_id__tid_num));
	len += scnprintf(buf + len, buf_len - len, "tid_num = %lu\n",
			 FIELD_GET(HTT_RX_TID_STATS_TID_NUM,
				   htt_stats_buf->sw_peer_id__tid_num));
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += scnprintf(buf + len, buf_len - len, "tid_name = %s\n", tid_name);
	len += scnprintf(buf + len, buf_len - len, "dup_in_reorder = %u\n",
			 htt_stats_buf->dup_in_reorder);
	len += scnprintf(buf + len, buf_len - len, "dup_past_outside_window = %u\n",
			 htt_stats_buf->dup_past_outside_window);
	len += scnprintf(buf + len, buf_len - len, "dup_past_within_window = %u\n",
			 htt_stats_buf->dup_past_within_window);
	len += scnprintf(buf + len, buf_len - len, "rxdesc_err_decrypt = %u\n\n",
			 htt_stats_buf->rxdesc_err_decrypt);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_counter_tlv(const void *tag_buf,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_counter_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_COUNTER_TLV:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->counter_name,
			   "counter_name",
			   HTT_MAX_COUNTER_NAME, "\n");
	len += scnprintf(buf + len, buf_len - len, "count = %u\n\n",
			 htt_stats_buf->count);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_peer_stats_cmn_tlv(const void *tag_buf,
						struct debug_htt_stats_req *stats_req)
{
	const struct htt_peer_stats_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_PEER_STATS_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ppdu_cnt = %u\n",
			 htt_stats_buf->ppdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "mpdu_cnt = %u\n",
			 htt_stats_buf->mpdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "msdu_cnt = %u\n",
			 htt_stats_buf->msdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "pause_bitmap = %u\n",
			 htt_stats_buf->pause_bitmap);
	len += scnprintf(buf + len, buf_len - len, "block_bitmap = %u\n",
			 htt_stats_buf->block_bitmap);
	len += scnprintf(buf + len, buf_len - len, "last_rssi = %d\n",
			 htt_stats_buf->rssi);
	len += scnprintf(buf + len, buf_len - len, "enqueued_count = %llu\n",
			 htt_stats_buf->peer_enqueued_count_low |
			 ((u64)htt_stats_buf->peer_enqueued_count_high << 32));
	len += scnprintf(buf + len, buf_len - len, "dequeued_count = %llu\n",
			 htt_stats_buf->peer_dequeued_count_low |
			 ((u64)htt_stats_buf->peer_dequeued_count_high << 32));
	len += scnprintf(buf + len, buf_len - len, "dropped_count = %llu\n",
			 htt_stats_buf->peer_dropped_count_low |
			 ((u64)htt_stats_buf->peer_dropped_count_high << 32));
	len += scnprintf(buf + len, buf_len - len, "transmitted_ppdu_bytes = %llu\n",
			 htt_stats_buf->ppdu_transmitted_bytes_low |
			 ((u64)htt_stats_buf->ppdu_transmitted_bytes_high << 32));
	len += scnprintf(buf + len, buf_len - len, "ttl_removed_count = %u\n",
			 htt_stats_buf->peer_ttl_removed_count);
	len += scnprintf(buf + len, buf_len - len, "inactive_time = %u\n\n",
			 htt_stats_buf->inactive_time);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_peer_details_tlv(const void *tag_buf,
					      struct debug_htt_stats_req *stats_req)
{
	const struct htt_peer_details_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_PEER_DETAILS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "peer_type = %u\n",
			 htt_stats_buf->peer_type);
	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %u\n",
			 htt_stats_buf->sw_peer_id);
	len += scnprintf(buf + len, buf_len - len, "vdev_id = %lu\n",
			 FIELD_GET(HTT_PEER_DETAILS_VDEV_ID,
				   htt_stats_buf->vdev_pdev_ast_idx));
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %lu\n",
			 FIELD_GET(HTT_PEER_DETAILS_PDEV_ID,
				   htt_stats_buf->vdev_pdev_ast_idx));
	len += scnprintf(buf + len, buf_len - len, "ast_idx = %lu\n",
			 FIELD_GET(HTT_PEER_DETAILS_AST_IDX,
				   htt_stats_buf->vdev_pdev_ast_idx));
	len += scnprintf(buf + len, buf_len - len,
			 "mac_addr = %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
			 FIELD_GET(HTT_MAC_ADDR_L32_0,
				   htt_stats_buf->mac_addr.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_1,
				   htt_stats_buf->mac_addr.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_2,
				   htt_stats_buf->mac_addr.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_3,
				   htt_stats_buf->mac_addr.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_H16_0,
				   htt_stats_buf->mac_addr.mac_addr_h16),
			 FIELD_GET(HTT_MAC_ADDR_H16_1,
				   htt_stats_buf->mac_addr.mac_addr_h16));
	len += scnprintf(buf + len, buf_len - len, "peer_flags = 0x%x\n",
			 htt_stats_buf->peer_flags);
	len += scnprintf(buf + len, buf_len - len, "qpeer_flags = 0x%x\n\n",
			 htt_stats_buf->qpeer_flags);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_peer_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_peer_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 j;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PEER_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "tx_ldpc = %u\n",
			 htt_stats_buf->tx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 htt_stats_buf->rts_cnt);
	len += scnprintf(buf + len, buf_len - len, "ack_rssi = %u\n",
			 htt_stats_buf->ack_rssi);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_mcs, "tx_mcs",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_su_mcs, "tx_su_mcs",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_mu_mcs, "tx_mu_mcs",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_nss, "tx_nss",
			   HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_bw, "tx_bw",
			   HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_stbc, "tx_stbc",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_pream, "tx_pream",
			   HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");

	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "tx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_gi[j], NULL,
				   HTT_TX_PEER_STATS_NUM_MCS_COUNTERS, "\n");
	}

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_dcm, "tx_dcm",
			   HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_rx_peer_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_peer_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 j;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PEER_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "nsts = %u\n",
			 htt_stats_buf->nsts);
	len += scnprintf(buf + len, buf_len - len, "rx_ldpc = %u\n",
			 htt_stats_buf->rx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 htt_stats_buf->rts_cnt);
	len += scnprintf(buf + len, buf_len - len, "rssi_mgmt = %u\n",
			 htt_stats_buf->rssi_mgmt);
	len += scnprintf(buf + len, buf_len - len, "rssi_data = %u\n",
			 htt_stats_buf->rssi_data);
	len += scnprintf(buf + len, buf_len - len, "rssi_comb = %u\n",
			 htt_stats_buf->rssi_comb);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_mcs, "rx_mcs",
			   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_nss, "rx_nss",
			   HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_dcm, "rx_dcm",
			   HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_stbc, "rx_stbc",
			   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_bw, "rx_bw",
			   HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "rssi_chain[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rssi_chain[j], NULL,
				   HTT_RX_PEER_STATS_NUM_BW_COUNTERS, "\n");
	}

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "rx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_gi[j], NULL,
				   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_pream, "rx_pream",
			   HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_mu_mimo_sch_stats_tlv(const void *tag_buf,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_mu_mimo_sch_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_HWQ_MU_MIMO_SCH_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_posted = %u\n",
			 htt_stats_buf->mu_mimo_sch_posted);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_failed = %u\n",
			 htt_stats_buf->mu_mimo_sch_failed);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_ppdu_posted = %u\n\n",
			 htt_stats_buf->mu_mimo_ppdu_posted);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_mu_mimo_mpdu_stats_tlv(const void *tag_buf,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_mu_mimo_mpdu_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_MU_MIMO_MPDU_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_mpdus_queued_usr = %u\n",
			 htt_stats_buf->mu_mimo_mpdus_queued_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_mpdus_tried_usr = %u\n",
			 htt_stats_buf->mu_mimo_mpdus_tried_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_mpdus_failed_usr = %u\n",
			 htt_stats_buf->mu_mimo_mpdus_failed_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_mpdus_requeued_usr = %u\n",
			 htt_stats_buf->mu_mimo_mpdus_requeued_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_err_no_ba_usr = %u\n",
			 htt_stats_buf->mu_mimo_err_no_ba_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_mpdu_underrun_usr = %u\n",
			 htt_stats_buf->mu_mimo_mpdu_underrun_usr);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_ampdu_underrun_usr = %u\n\n",
			 htt_stats_buf->mu_mimo_ampdu_underrun_usr);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_mu_mimo_cmn_stats_tlv(const void *tag_buf,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_mu_mimo_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_HWQ_MU_MIMO_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_TX_HWQ_STATS_MAC_ID,
				   htt_stats_buf->mac_id__hwq_id__word));
	len += scnprintf(buf + len, buf_len - len, "hwq_id = %lu\n\n",
			 FIELD_GET(HTT_TX_HWQ_STATS_HWQ_ID,
				   htt_stats_buf->mac_id__hwq_id__word));

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_stats_cmn_tlv(const void *tag_buf, struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_stats_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	/* TODO: HKDBG */
	len += scnprintf(buf + len, buf_len - len, "HTT_TX_HWQ_STATS_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_TX_HWQ_STATS_MAC_ID,
				   htt_stats_buf->mac_id__hwq_id__word));
	len += scnprintf(buf + len, buf_len - len, "hwq_id = %lu\n",
			 FIELD_GET(HTT_TX_HWQ_STATS_HWQ_ID,
				   htt_stats_buf->mac_id__hwq_id__word));
	len += scnprintf(buf + len, buf_len - len, "xretry = %u\n",
			 htt_stats_buf->xretry);
	len += scnprintf(buf + len, buf_len - len, "underrun_cnt = %u\n",
			 htt_stats_buf->underrun_cnt);
	len += scnprintf(buf + len, buf_len - len, "flush_cnt = %u\n",
			 htt_stats_buf->flush_cnt);
	len += scnprintf(buf + len, buf_len - len, "filt_cnt = %u\n",
			 htt_stats_buf->filt_cnt);
	len += scnprintf(buf + len, buf_len - len, "null_mpdu_bmap = %u\n",
			 htt_stats_buf->null_mpdu_bmap);
	len += scnprintf(buf + len, buf_len - len, "user_ack_failure = %u\n",
			 htt_stats_buf->user_ack_failure);
	len += scnprintf(buf + len, buf_len - len, "ack_tlv_proc = %u\n",
			 htt_stats_buf->ack_tlv_proc);
	len += scnprintf(buf + len, buf_len - len, "sched_id_proc = %u\n",
			 htt_stats_buf->sched_id_proc);
	len += scnprintf(buf + len, buf_len - len, "null_mpdu_tx_count = %u\n",
			 htt_stats_buf->null_mpdu_tx_count);
	len += scnprintf(buf + len, buf_len - len, "mpdu_bmap_not_recvd = %u\n",
			 htt_stats_buf->mpdu_bmap_not_recvd);
	len += scnprintf(buf + len, buf_len - len, "num_bar = %u\n",
			 htt_stats_buf->num_bar);
	len += scnprintf(buf + len, buf_len - len, "rts = %u\n",
			 htt_stats_buf->rts);
	len += scnprintf(buf + len, buf_len - len, "cts2self = %u\n",
			 htt_stats_buf->cts2self);
	len += scnprintf(buf + len, buf_len - len, "qos_null = %u\n",
			 htt_stats_buf->qos_null);
	len += scnprintf(buf + len, buf_len - len, "mpdu_tried_cnt = %u\n",
			 htt_stats_buf->mpdu_tried_cnt);
	len += scnprintf(buf + len, buf_len - len, "mpdu_queued_cnt = %u\n",
			 htt_stats_buf->mpdu_queued_cnt);
	len += scnprintf(buf + len, buf_len - len, "mpdu_ack_fail_cnt = %u\n",
			 htt_stats_buf->mpdu_ack_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "mpdu_filt_cnt = %u\n",
			 htt_stats_buf->mpdu_filt_cnt);
	len += scnprintf(buf + len, buf_len - len, "false_mpdu_ack_count = %u\n",
			 htt_stats_buf->false_mpdu_ack_count);
	len += scnprintf(buf + len, buf_len - len, "txq_timeout = %u\n\n",
			 htt_stats_buf->txq_timeout);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_difs_latency_stats_tlv_v(const void *tag_buf,
					  u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_difs_latency_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 data_len = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_DIFS_LATENCY_BINS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_DIFS_LATENCY_STATS_TLV_V:\n");
	len += scnprintf(buf + len, buf_len - len, "hist_intvl = %u\n",
			 htt_stats_buf->hist_intvl);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->difs_latency_hist,
			   "difs_latency_hist", data_len, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_cmd_result_stats_tlv_v(const void *tag_buf,
					u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_cmd_result_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 data_len;

	data_len = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_CMD_RESULT_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_CMD_RESULT_STATS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->cmd_result, "cmd_result",
			   data_len, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_cmd_stall_stats_tlv_v(const void *tag_buf,
				       u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_cmd_stall_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems;

	num_elems = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_CMD_STALL_STATS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_HWQ_CMD_STALL_STATS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->cmd_stall_status,
			   "cmd_stall_status", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_fes_result_stats_tlv_v(const void *tag_buf,
					u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_fes_result_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems;

	num_elems = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_FES_RESULT_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_FES_RESULT_STATS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fes_result, "fes_result",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_tried_mpdu_cnt_hist_tlv_v(const void *tag_buf,
					   u16 tag_len,
					   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_tried_mpdu_cnt_hist_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u32  num_elements = ((tag_len -
			    sizeof(htt_stats_buf->hist_bin_size)) >> 2);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_TRIED_MPDU_CNT_HIST_TLV_V:\n");
	len += scnprintf(buf + len, buf_len - len, "TRIED_MPDU_CNT_HIST_BIN_SIZE : %u\n",
			 htt_stats_buf->hist_bin_size);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tried_mpdu_cnt_hist,
			   "tried_mpdu_cnt_hist", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_hwq_txop_used_cnt_hist_tlv_v(const void *tag_buf,
					  u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_txop_used_cnt_hist_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u32 num_elements = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_HWQ_TXOP_USED_CNT_HIST_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->txop_used_cnt_hist,
			   "txop_used_cnt_hist", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_sounding_stats_tlv(const void *tag_buf,
						   struct debug_htt_stats_req *stats_req)
{
	s32 i;
	const struct htt_tx_sounding_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	const u32 *cbf_20 = htt_stats_buf->cbf_20;
	const u32 *cbf_40 = htt_stats_buf->cbf_40;
	const u32 *cbf_80 = htt_stats_buf->cbf_80;
	const u32 *cbf_160 = htt_stats_buf->cbf_160;

	if (htt_stats_buf->tx_sounding_mode == HTT_TX_AC_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "\nHTT_TX_AC_SOUNDING_STATS_TLV:\n\n");
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_20 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_20[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_40 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_40[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_80 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_80[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_160 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_160[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);

		for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS; i++) {
			len += scnprintf(buf + len, buf_len - len,
					 "Sounding User %u = 20MHz: %u, 40MHz : %u, 80MHz: %u, 160MHz: %u\n",
					 i,
					 htt_stats_buf->sounding[0],
					 htt_stats_buf->sounding[1],
					 htt_stats_buf->sounding[2],
					 htt_stats_buf->sounding[3]);
		}
	} else if (htt_stats_buf->tx_sounding_mode == HTT_TX_AX_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "\nHTT_TX_AX_SOUNDING_STATS_TLV:\n");
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_20 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_20[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_20[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_40 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_40[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_40[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_80 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_80[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_80[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_160 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u\n",
				 cbf_160[HTT_IMPLICIT_TXBF_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				 cbf_160[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);

		for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS; i++) {
			len += scnprintf(buf + len, buf_len - len,
					 "Sounding User %u = 20MHz: %u, 40MHz : %u, 80MHz: %u, 160MHz: %u\n",
					 i,
					 htt_stats_buf->sounding[0],
					 htt_stats_buf->sounding[1],
					 htt_stats_buf->sounding[2],
					 htt_stats_buf->sounding[3]);
		}
	}

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_selfgen_cmn_stats_tlv(const void *tag_buf,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_selfgen_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "su_bar = %u\n",
			 htt_stats_buf->su_bar);
	len += scnprintf(buf + len, buf_len - len, "rts = %u\n",
			 htt_stats_buf->rts);
	len += scnprintf(buf + len, buf_len - len, "cts2self = %u\n",
			 htt_stats_buf->cts2self);
	len += scnprintf(buf + len, buf_len - len, "qos_null = %u\n",
			 htt_stats_buf->qos_null);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_1 = %u\n",
			 htt_stats_buf->delayed_bar_1);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_2 = %u\n",
			 htt_stats_buf->delayed_bar_2);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_3 = %u\n",
			 htt_stats_buf->delayed_bar_3);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_4 = %u\n",
			 htt_stats_buf->delayed_bar_4);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_5 = %u\n",
			 htt_stats_buf->delayed_bar_5);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_6 = %u\n",
			 htt_stats_buf->delayed_bar_6);
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_7 = %u\n\n",
			 htt_stats_buf->delayed_bar_7);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_selfgen_ac_stats_tlv(const void *tag_buf,
				  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_selfgen_ac_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndpa = %u\n",
			 htt_stats_buf->ac_su_ndpa);
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndp = %u\n",
			 htt_stats_buf->ac_su_ndp);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndpa = %u\n",
			 htt_stats_buf->ac_mu_mimo_ndpa);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndp = %u\n",
			 htt_stats_buf->ac_mu_mimo_ndp);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brpoll_1 = %u\n",
			 htt_stats_buf->ac_mu_mimo_brpoll_1);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brpoll_2 = %u\n",
			 htt_stats_buf->ac_mu_mimo_brpoll_2);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brpoll_3 = %u\n\n",
			 htt_stats_buf->ac_mu_mimo_brpoll_3);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_selfgen_ax_stats_tlv(const void *tag_buf,
				  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_selfgen_ax_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndpa = %u\n",
			 htt_stats_buf->ax_su_ndpa);
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndp = %u\n",
			 htt_stats_buf->ax_su_ndp);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndpa = %u\n",
			 htt_stats_buf->ax_mu_mimo_ndpa);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndp = %u\n",
			 htt_stats_buf->ax_mu_mimo_ndp);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_1 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_1);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_2 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_2);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_3 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_3);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_4 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_4);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_5 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_5);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_6 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_6);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brpoll_7 = %u\n",
			 htt_stats_buf->ax_mu_mimo_brpoll_7);
	len += scnprintf(buf + len, buf_len - len, "ax_basic_trigger = %u\n",
			 htt_stats_buf->ax_basic_trigger);
	len += scnprintf(buf + len, buf_len - len, "ax_bsr_trigger = %u\n",
			 htt_stats_buf->ax_bsr_trigger);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_bar_trigger = %u\n",
			 htt_stats_buf->ax_mu_bar_trigger);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_rts_trigger = %u\n\n",
			 htt_stats_buf->ax_mu_rts_trigger);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_selfgen_ac_err_stats_tlv(const void *tag_buf,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_selfgen_ac_err_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_ERR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndp_err = %u\n",
			 htt_stats_buf->ac_su_ndp_err);
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndpa_err = %u\n",
			 htt_stats_buf->ac_su_ndpa_err);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndpa_err = %u\n",
			 htt_stats_buf->ac_mu_mimo_ndpa_err);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndp_err = %u\n",
			 htt_stats_buf->ac_mu_mimo_ndp_err);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp1_err = %u\n",
			 htt_stats_buf->ac_mu_mimo_brp1_err);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp2_err = %u\n",
			 htt_stats_buf->ac_mu_mimo_brp2_err);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp3_err = %u\n\n",
			 htt_stats_buf->ac_mu_mimo_brp3_err);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_selfgen_ax_err_stats_tlv(const void *tag_buf,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_selfgen_ax_err_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_ERR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndp_err = %u\n",
			 htt_stats_buf->ax_su_ndp_err);
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndpa_err = %u\n",
			 htt_stats_buf->ax_su_ndpa_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndpa_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_ndpa_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndp_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_ndp_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp1_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp1_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp2_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp2_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp3_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp3_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp4_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp4_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp5_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp5_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp6_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp6_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_brp7_err = %u\n",
			 htt_stats_buf->ax_mu_mimo_brp7_err);
	len += scnprintf(buf + len, buf_len - len, "ax_basic_trigger_err = %u\n",
			 htt_stats_buf->ax_basic_trigger_err);
	len += scnprintf(buf + len, buf_len - len, "ax_bsr_trigger_err = %u\n",
			 htt_stats_buf->ax_bsr_trigger_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_bar_trigger_err = %u\n",
			 htt_stats_buf->ax_mu_bar_trigger_err);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_rts_trigger_err = %u\n\n",
			 htt_stats_buf->ax_mu_rts_trigger_err);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_mu_mimo_sch_stats_tlv(const void *tag_buf,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_mu_mimo_sch_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_MU_MIMO_SCH_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_posted = %u\n",
			 htt_stats_buf->mu_mimo_sch_posted);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_failed = %u\n",
			 htt_stats_buf->mu_mimo_sch_failed);
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_ppdu_posted = %u\n\n",
			 htt_stats_buf->mu_mimo_ppdu_posted);

	len += scnprintf(buf + len, buf_len - len, "11ac MU_MIMO SCH STATS:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "ac_mu_mimo_sch_nusers_%u = %u\n",
				 i, htt_stats_buf->ac_mu_mimo_sch_nusers[i]);

	len += scnprintf(buf + len, buf_len - len, "\n11ax MU_MIMO SCH STATS:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "ax_mu_mimo_sch_nusers_%u = %u\n",
				 i, htt_stats_buf->ax_mu_mimo_sch_nusers[i]);

	len += scnprintf(buf + len, buf_len - len, "\n11ax OFDMA SCH STATS:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_sch_nusers_%u = %u\n",
				 i, htt_stats_buf->ax_ofdma_sch_nusers[i]);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_mu_mimo_mpdu_stats_tlv(const void *tag_buf,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_mpdu_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	if (htt_stats_buf->tx_sched_mode == HTT_STATS_TX_SCHED_MODE_MU_MIMO_AC) {
		if (!htt_stats_buf->user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_MU_MIMO_AC_MPDU_STATS:\n");

		if (htt_stats_buf->user_index <
		    HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_queued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_queued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_tried_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_tried_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_failed_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_failed_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_requeued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_requeued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_err_no_ba_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->err_no_ba_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdu_underrun_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdu_underrun_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_ampdu_underrun_usr_%u = %u\n\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->ampdu_underrun_usr);
		}
	}

	if (htt_stats_buf->tx_sched_mode == HTT_STATS_TX_SCHED_MODE_MU_MIMO_AX) {
		if (!htt_stats_buf->user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_MU_MIMO_AX_MPDU_STATS:\n");

		if (htt_stats_buf->user_index <
		    HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_queued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_queued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_tried_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_tried_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_failed_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_failed_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_requeued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_requeued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_err_no_ba_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->err_no_ba_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdu_underrun_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdu_underrun_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_ampdu_underrun_usr_%u = %u\n\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->ampdu_underrun_usr);
		}
	}

	if (htt_stats_buf->tx_sched_mode == HTT_STATS_TX_SCHED_MODE_MU_OFDMA_AX) {
		if (!htt_stats_buf->user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_AX_MU_OFDMA_MPDU_STATS:\n");

		if (htt_stats_buf->user_index < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_queued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_queued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_tried_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_tried_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_failed_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_failed_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_requeued_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdus_requeued_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_err_no_ba_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->err_no_ba_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdu_underrun_usr_%u = %u\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->mpdu_underrun_usr);
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_ampdu_underrun_usr_%u = %u\n\n",
					 htt_stats_buf->user_index,
					 htt_stats_buf->ampdu_underrun_usr);
		}
	}

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_sched_txq_cmd_posted_tlv_v(const void *tag_buf,
				     u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct htt_sched_txq_cmd_posted_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elements = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_SCHED_TX_MODE_MAX);

	len += scnprintf(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_POSTED_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sched_cmd_posted,
			   "sched_cmd_posted", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_sched_txq_cmd_reaped_tlv_v(const void *tag_buf,
				     u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct htt_sched_txq_cmd_reaped_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elements = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_SCHED_TX_MODE_MAX);

	len += scnprintf(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_REAPED_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sched_cmd_reaped,
			   "sched_cmd_reaped", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_sched_txq_sched_order_su_tlv_v(const void *tag_buf,
					 u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_sched_txq_sched_order_su_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	/* each entry is u32, i.e. 4 bytes */
	u32 sched_order_su_num_entries =
		min_t(u32, (tag_len >> 2), HTT_TX_PDEV_NUM_SCHED_ORDER_LOG);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_SCHED_TXQ_SCHED_ORDER_SU_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sched_order_su, "sched_order_su",
			   sched_order_su_num_entries, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_sched_txq_sched_ineligibility_tlv_v(const void *tag_buf,
					      u16 tag_len,
					      struct debug_htt_stats_req *stats_req)
{
	const struct htt_sched_txq_sched_ineligibility_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	/* each entry is u32, i.e. 4 bytes */
	u32 sched_ineligibility_num_entries = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_SCHED_TXQ_SCHED_INELIGIBILITY_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->sched_ineligibility,
			   "sched_ineligibility", sched_ineligibility_num_entries,
			   "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_pdev_stats_sched_per_txq_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_stats_sched_per_txq_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_SCHED_PER_TXQ_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_TX_PDEV_STATS_SCHED_PER_TXQ_MAC_ID,
				   htt_stats_buf->mac_id__txq_id__word));
	len += scnprintf(buf + len, buf_len - len, "txq_id = %lu\n",
			 FIELD_GET(HTT_TX_PDEV_STATS_SCHED_PER_TXQ_ID,
				   htt_stats_buf->mac_id__txq_id__word));
	len += scnprintf(buf + len, buf_len - len, "sched_policy = %u\n",
			 htt_stats_buf->sched_policy);
	len += scnprintf(buf + len, buf_len - len,
			 "last_sched_cmd_posted_timestamp = %u\n",
			 htt_stats_buf->last_sched_cmd_posted_timestamp);
	len += scnprintf(buf + len, buf_len - len,
			 "last_sched_cmd_compl_timestamp = %u\n",
			 htt_stats_buf->last_sched_cmd_compl_timestamp);
	len += scnprintf(buf + len, buf_len - len, "sched_2_tac_lwm_count = %u\n",
			 htt_stats_buf->sched_2_tac_lwm_count);
	len += scnprintf(buf + len, buf_len - len, "sched_2_tac_ring_full = %u\n",
			 htt_stats_buf->sched_2_tac_ring_full);
	len += scnprintf(buf + len, buf_len - len, "sched_cmd_post_failure = %u\n",
			 htt_stats_buf->sched_cmd_post_failure);
	len += scnprintf(buf + len, buf_len - len, "num_active_tids = %u\n",
			 htt_stats_buf->num_active_tids);
	len += scnprintf(buf + len, buf_len - len, "num_ps_schedules = %u\n",
			 htt_stats_buf->num_ps_schedules);
	len += scnprintf(buf + len, buf_len - len, "sched_cmds_pending = %u\n",
			 htt_stats_buf->sched_cmds_pending);
	len += scnprintf(buf + len, buf_len - len, "num_tid_register = %u\n",
			 htt_stats_buf->num_tid_register);
	len += scnprintf(buf + len, buf_len - len, "num_tid_unregister = %u\n",
			 htt_stats_buf->num_tid_unregister);
	len += scnprintf(buf + len, buf_len - len, "num_qstats_queried = %u\n",
			 htt_stats_buf->num_qstats_queried);
	len += scnprintf(buf + len, buf_len - len, "qstats_update_pending = %u\n",
			 htt_stats_buf->qstats_update_pending);
	len += scnprintf(buf + len, buf_len - len, "last_qstats_query_timestamp = %u\n",
			 htt_stats_buf->last_qstats_query_timestamp);
	len += scnprintf(buf + len, buf_len - len, "num_tqm_cmdq_full = %u\n",
			 htt_stats_buf->num_tqm_cmdq_full);
	len += scnprintf(buf + len, buf_len - len, "num_de_sched_algo_trigger = %u\n",
			 htt_stats_buf->num_de_sched_algo_trigger);
	len += scnprintf(buf + len, buf_len - len, "num_rt_sched_algo_trigger = %u\n",
			 htt_stats_buf->num_rt_sched_algo_trigger);
	len += scnprintf(buf + len, buf_len - len, "num_tqm_sched_algo_trigger = %u\n",
			 htt_stats_buf->num_tqm_sched_algo_trigger);
	len += scnprintf(buf + len, buf_len - len, "notify_sched = %u\n\n",
			 htt_stats_buf->notify_sched);
	len += scnprintf(buf + len, buf_len - len, "dur_based_sendn_term = %u\n\n",
			 htt_stats_buf->dur_based_sendn_term);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_stats_tx_sched_cmn_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_stats_tx_sched_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_TX_SCHED_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "current_timestamp = %u\n\n",
			 htt_stats_buf->current_timestamp);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_tqm_gen_mpdu_stats_tlv_v(const void *tag_buf,
				      u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_gen_mpdu_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elements = min_t(u16, (tag_len >> 2),
				 HTT_TX_TQM_MAX_LIST_MPDU_END_REASON);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_GEN_MPDU_STATS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->gen_mpdu_end_reason,
			   "gen_mpdu_end_reason", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_tqm_list_mpdu_stats_tlv_v(const void *tag_buf,
				       u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_list_mpdu_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_TQM_MAX_LIST_MPDU_END_REASON);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_TQM_LIST_MPDU_STATS_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->list_mpdu_end_reason,
			   "list_mpdu_end_reason", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_tqm_list_mpdu_cnt_tlv_v(const void *tag_buf,
				     u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_list_mpdu_cnt_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      HTT_TX_TQM_MAX_LIST_MPDU_CNT_HISTOGRAM_BINS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_LIST_MPDU_CNT_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->list_mpdu_cnt_hist,
			   "list_mpdu_cnt_hist", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_tqm_pdev_stats_tlv_v(const void *tag_buf,
				  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_pdev_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_PDEV_STATS_TLV_V:\n");
	len += scnprintf(buf + len, buf_len - len, "msdu_count = %u\n",
			 htt_stats_buf->msdu_count);
	len += scnprintf(buf + len, buf_len - len, "mpdu_count = %u\n",
			 htt_stats_buf->mpdu_count);
	len += scnprintf(buf + len, buf_len - len, "remove_msdu = %u\n",
			 htt_stats_buf->remove_msdu);
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu = %u\n",
			 htt_stats_buf->remove_mpdu);
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_ttl = %u\n",
			 htt_stats_buf->remove_msdu_ttl);
	len += scnprintf(buf + len, buf_len - len, "send_bar = %u\n",
			 htt_stats_buf->send_bar);
	len += scnprintf(buf + len, buf_len - len, "bar_sync = %u\n",
			 htt_stats_buf->bar_sync);
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu = %u\n",
			 htt_stats_buf->notify_mpdu);
	len += scnprintf(buf + len, buf_len - len, "sync_cmd = %u\n",
			 htt_stats_buf->sync_cmd);
	len += scnprintf(buf + len, buf_len - len, "write_cmd = %u\n",
			 htt_stats_buf->write_cmd);
	len += scnprintf(buf + len, buf_len - len, "hwsch_trigger = %u\n",
			 htt_stats_buf->hwsch_trigger);
	len += scnprintf(buf + len, buf_len - len, "ack_tlv_proc = %u\n",
			 htt_stats_buf->ack_tlv_proc);
	len += scnprintf(buf + len, buf_len - len, "gen_mpdu_cmd = %u\n",
			 htt_stats_buf->gen_mpdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "gen_list_cmd = %u\n",
			 htt_stats_buf->gen_list_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu_cmd = %u\n",
			 htt_stats_buf->remove_mpdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu_tried_cmd = %u\n",
			 htt_stats_buf->remove_mpdu_tried_cmd);
	len += scnprintf(buf + len, buf_len - len, "mpdu_queue_stats_cmd = %u\n",
			 htt_stats_buf->mpdu_queue_stats_cmd);
	len += scnprintf(buf + len, buf_len - len, "mpdu_head_info_cmd = %u\n",
			 htt_stats_buf->mpdu_head_info_cmd);
	len += scnprintf(buf + len, buf_len - len, "msdu_flow_stats_cmd = %u\n",
			 htt_stats_buf->msdu_flow_stats_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_cmd = %u\n",
			 htt_stats_buf->remove_msdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_ttl_cmd = %u\n",
			 htt_stats_buf->remove_msdu_ttl_cmd);
	len += scnprintf(buf + len, buf_len - len, "flush_cache_cmd = %u\n",
			 htt_stats_buf->flush_cache_cmd);
	len += scnprintf(buf + len, buf_len - len, "update_mpduq_cmd = %u\n",
			 htt_stats_buf->update_mpduq_cmd);
	len += scnprintf(buf + len, buf_len - len, "enqueue = %u\n",
			 htt_stats_buf->enqueue);
	len += scnprintf(buf + len, buf_len - len, "enqueue_notify = %u\n",
			 htt_stats_buf->enqueue_notify);
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu_at_head = %u\n",
			 htt_stats_buf->notify_mpdu_at_head);
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu_state_valid = %u\n",
			 htt_stats_buf->notify_mpdu_state_valid);
	len += scnprintf(buf + len, buf_len - len, "sched_udp_notify1 = %u\n",
			 htt_stats_buf->sched_udp_notify1);
	len += scnprintf(buf + len, buf_len - len, "sched_udp_notify2 = %u\n",
			 htt_stats_buf->sched_udp_notify2);
	len += scnprintf(buf + len, buf_len - len, "sched_nonudp_notify1 = %u\n",
			 htt_stats_buf->sched_nonudp_notify1);
	len += scnprintf(buf + len, buf_len - len, "sched_nonudp_notify2 = %u\n\n",
			 htt_stats_buf->sched_nonudp_notify2);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_tqm_cmn_stats_tlv(const void *tag_buf,
						  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "max_cmdq_id = %u\n",
			 htt_stats_buf->max_cmdq_id);
	len += scnprintf(buf + len, buf_len - len, "list_mpdu_cnt_hist_intvl = %u\n",
			 htt_stats_buf->list_mpdu_cnt_hist_intvl);
	len += scnprintf(buf + len, buf_len - len, "add_msdu = %u\n",
			 htt_stats_buf->add_msdu);
	len += scnprintf(buf + len, buf_len - len, "q_empty = %u\n",
			 htt_stats_buf->q_empty);
	len += scnprintf(buf + len, buf_len - len, "q_not_empty = %u\n",
			 htt_stats_buf->q_not_empty);
	len += scnprintf(buf + len, buf_len - len, "drop_notification = %u\n",
			 htt_stats_buf->drop_notification);
	len += scnprintf(buf + len, buf_len - len, "desc_threshold = %u\n\n",
			 htt_stats_buf->desc_threshold);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_tqm_error_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_error_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_ERROR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "q_empty_failure = %u\n",
			 htt_stats_buf->q_empty_failure);
	len += scnprintf(buf + len, buf_len - len, "q_not_empty_failure = %u\n",
			 htt_stats_buf->q_not_empty_failure);
	len += scnprintf(buf + len, buf_len - len, "add_msdu_failure = %u\n\n",
			 htt_stats_buf->add_msdu_failure);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_tqm_cmdq_status_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_tqm_cmdq_status_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_CMDQ_STATUS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_TX_TQM_CMDQ_STATUS_MAC_ID,
				   htt_stats_buf->mac_id__cmdq_id__word));
	len += scnprintf(buf + len, buf_len - len, "cmdq_id = %lu\n\n",
			 FIELD_GET(HTT_TX_TQM_CMDQ_STATUS_CMDQ_ID,
				   htt_stats_buf->mac_id__cmdq_id__word));
	len += scnprintf(buf + len, buf_len - len, "sync_cmd = %u\n",
			 htt_stats_buf->sync_cmd);
	len += scnprintf(buf + len, buf_len - len, "write_cmd = %u\n",
			 htt_stats_buf->write_cmd);
	len += scnprintf(buf + len, buf_len - len, "gen_mpdu_cmd = %u\n",
			 htt_stats_buf->gen_mpdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "mpdu_queue_stats_cmd = %u\n",
			 htt_stats_buf->mpdu_queue_stats_cmd);
	len += scnprintf(buf + len, buf_len - len, "mpdu_head_info_cmd = %u\n",
			 htt_stats_buf->mpdu_head_info_cmd);
	len += scnprintf(buf + len, buf_len - len, "msdu_flow_stats_cmd = %u\n",
			 htt_stats_buf->msdu_flow_stats_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu_cmd = %u\n",
			 htt_stats_buf->remove_mpdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_cmd = %u\n",
			 htt_stats_buf->remove_msdu_cmd);
	len += scnprintf(buf + len, buf_len - len, "flush_cache_cmd = %u\n",
			 htt_stats_buf->flush_cache_cmd);
	len += scnprintf(buf + len, buf_len - len, "update_mpduq_cmd = %u\n",
			 htt_stats_buf->update_mpduq_cmd);
	len += scnprintf(buf + len, buf_len - len, "update_msduq_cmd = %u\n\n",
			 htt_stats_buf->update_msduq_cmd);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_eapol_packets_stats_tlv(const void *tag_buf,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_eapol_packets_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			   "HTT_TX_DE_EAPOL_PACKETS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "m1_packets = %u\n",
			 htt_stats_buf->m1_packets);
	len += scnprintf(buf + len, buf_len - len, "m2_packets = %u\n",
			 htt_stats_buf->m2_packets);
	len += scnprintf(buf + len, buf_len - len, "m3_packets = %u\n",
			 htt_stats_buf->m3_packets);
	len += scnprintf(buf + len, buf_len - len, "m4_packets = %u\n",
			 htt_stats_buf->m4_packets);
	len += scnprintf(buf + len, buf_len - len, "g1_packets = %u\n",
			 htt_stats_buf->g1_packets);
	len += scnprintf(buf + len, buf_len - len, "g2_packets = %u\n\n",
			 htt_stats_buf->g2_packets);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_classify_failed_stats_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_classify_failed_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			   "HTT_TX_DE_CLASSIFY_FAILED_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ap_bss_peer_not_found = %u\n",
			 htt_stats_buf->ap_bss_peer_not_found);
	len += scnprintf(buf + len, buf_len - len, "ap_bcast_mcast_no_peer = %u\n",
			 htt_stats_buf->ap_bcast_mcast_no_peer);
	len += scnprintf(buf + len, buf_len - len, "sta_delete_in_progress = %u\n",
			 htt_stats_buf->sta_delete_in_progress);
	len += scnprintf(buf + len, buf_len - len, "ibss_no_bss_peer = %u\n",
			 htt_stats_buf->ibss_no_bss_peer);
	len += scnprintf(buf + len, buf_len - len, "invalid_vdev_type = %u\n",
			 htt_stats_buf->invalid_vdev_type);
	len += scnprintf(buf + len, buf_len - len, "invalid_ast_peer_entry = %u\n",
			 htt_stats_buf->invalid_ast_peer_entry);
	len += scnprintf(buf + len, buf_len - len, "peer_entry_invalid = %u\n",
			 htt_stats_buf->peer_entry_invalid);
	len += scnprintf(buf + len, buf_len - len, "ethertype_not_ip = %u\n",
			 htt_stats_buf->ethertype_not_ip);
	len += scnprintf(buf + len, buf_len - len, "eapol_lookup_failed = %u\n",
			 htt_stats_buf->eapol_lookup_failed);
	len += scnprintf(buf + len, buf_len - len, "qpeer_not_allow_data = %u\n",
			 htt_stats_buf->qpeer_not_allow_data);
	len += scnprintf(buf + len, buf_len - len, "fse_tid_override = %u\n",
			 htt_stats_buf->fse_tid_override);
	len += scnprintf(buf + len, buf_len - len, "ipv6_jumbogram_zero_length = %u\n",
			 htt_stats_buf->ipv6_jumbogram_zero_length);
	len += scnprintf(buf + len, buf_len - len, "qos_to_non_qos_in_prog = %u\n\n",
			 htt_stats_buf->qos_to_non_qos_in_prog);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_classify_stats_tlv(const void *tag_buf,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_classify_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_CLASSIFY_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "arp_packets = %u\n",
			 htt_stats_buf->arp_packets);
	len += scnprintf(buf + len, buf_len - len, "igmp_packets = %u\n",
			 htt_stats_buf->igmp_packets);
	len += scnprintf(buf + len, buf_len - len, "dhcp_packets = %u\n",
			 htt_stats_buf->dhcp_packets);
	len += scnprintf(buf + len, buf_len - len, "host_inspected = %u\n",
			 htt_stats_buf->host_inspected);
	len += scnprintf(buf + len, buf_len - len, "htt_included = %u\n",
			 htt_stats_buf->htt_included);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_mcs = %u\n",
			 htt_stats_buf->htt_valid_mcs);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_nss = %u\n",
			 htt_stats_buf->htt_valid_nss);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_preamble_type = %u\n",
			 htt_stats_buf->htt_valid_preamble_type);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_chainmask = %u\n",
			 htt_stats_buf->htt_valid_chainmask);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_guard_interval = %u\n",
			 htt_stats_buf->htt_valid_guard_interval);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_retries = %u\n",
			 htt_stats_buf->htt_valid_retries);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_bw_info = %u\n",
			 htt_stats_buf->htt_valid_bw_info);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_power = %u\n",
			 htt_stats_buf->htt_valid_power);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_key_flags = 0x%x\n",
			 htt_stats_buf->htt_valid_key_flags);
	len += scnprintf(buf + len, buf_len - len, "htt_valid_no_encryption = %u\n",
			 htt_stats_buf->htt_valid_no_encryption);
	len += scnprintf(buf + len, buf_len - len, "fse_entry_count = %u\n",
			 htt_stats_buf->fse_entry_count);
	len += scnprintf(buf + len, buf_len - len, "fse_priority_be = %u\n",
			 htt_stats_buf->fse_priority_be);
	len += scnprintf(buf + len, buf_len - len, "fse_priority_high = %u\n",
			 htt_stats_buf->fse_priority_high);
	len += scnprintf(buf + len, buf_len - len, "fse_priority_low = %u\n",
			 htt_stats_buf->fse_priority_low);
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_be = %u\n",
			 htt_stats_buf->fse_traffic_ptrn_be);
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_over_sub = %u\n",
			 htt_stats_buf->fse_traffic_ptrn_over_sub);
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_bursty = %u\n",
			 htt_stats_buf->fse_traffic_ptrn_bursty);
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_interactive = %u\n",
			 htt_stats_buf->fse_traffic_ptrn_interactive);
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_periodic = %u\n",
			 htt_stats_buf->fse_traffic_ptrn_periodic);
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_alloc = %u\n",
			 htt_stats_buf->fse_hwqueue_alloc);
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_created = %u\n",
			 htt_stats_buf->fse_hwqueue_created);
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_send_to_host = %u\n",
			 htt_stats_buf->fse_hwqueue_send_to_host);
	len += scnprintf(buf + len, buf_len - len, "mcast_entry = %u\n",
			 htt_stats_buf->mcast_entry);
	len += scnprintf(buf + len, buf_len - len, "bcast_entry = %u\n",
			 htt_stats_buf->bcast_entry);
	len += scnprintf(buf + len, buf_len - len, "htt_update_peer_cache = %u\n",
			 htt_stats_buf->htt_update_peer_cache);
	len += scnprintf(buf + len, buf_len - len, "htt_learning_frame = %u\n",
			 htt_stats_buf->htt_learning_frame);
	len += scnprintf(buf + len, buf_len - len, "fse_invalid_peer = %u\n",
			 htt_stats_buf->fse_invalid_peer);
	len += scnprintf(buf + len, buf_len - len, "mec_notify = %u\n\n",
			 htt_stats_buf->mec_notify);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_classify_status_stats_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_classify_status_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			   "HTT_TX_DE_CLASSIFY_STATUS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "eok = %u\n",
			 htt_stats_buf->eok);
	len += scnprintf(buf + len, buf_len - len, "classify_done = %u\n",
			 htt_stats_buf->classify_done);
	len += scnprintf(buf + len, buf_len - len, "lookup_failed = %u\n",
			 htt_stats_buf->lookup_failed);
	len += scnprintf(buf + len, buf_len - len, "send_host_dhcp = %u\n",
			 htt_stats_buf->send_host_dhcp);
	len += scnprintf(buf + len, buf_len - len, "send_host_mcast = %u\n",
			 htt_stats_buf->send_host_mcast);
	len += scnprintf(buf + len, buf_len - len, "send_host_unknown_dest = %u\n",
			 htt_stats_buf->send_host_unknown_dest);
	len += scnprintf(buf + len, buf_len - len, "send_host = %u\n",
			 htt_stats_buf->send_host);
	len += scnprintf(buf + len, buf_len - len, "status_invalid = %u\n\n",
			 htt_stats_buf->status_invalid);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_enqueue_packets_stats_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_enqueue_packets_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_ENQUEUE_PACKETS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "enqueued_pkts = %u\n",
			 htt_stats_buf->enqueued_pkts);
	len += scnprintf(buf + len, buf_len - len, "to_tqm = %u\n",
			 htt_stats_buf->to_tqm);
	len += scnprintf(buf + len, buf_len - len, "to_tqm_bypass = %u\n\n",
			 htt_stats_buf->to_tqm_bypass);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_enqueue_discard_stats_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_enqueue_discard_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_ENQUEUE_DISCARD_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "discarded_pkts = %u\n",
			 htt_stats_buf->discarded_pkts);
	len += scnprintf(buf + len, buf_len - len, "local_frames = %u\n",
			 htt_stats_buf->local_frames);
	len += scnprintf(buf + len, buf_len - len, "is_ext_msdu = %u\n\n",
			 htt_stats_buf->is_ext_msdu);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_de_compl_stats_tlv(const void *tag_buf,
						   struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_compl_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_COMPL_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "tcl_dummy_frame = %u\n",
			 htt_stats_buf->tcl_dummy_frame);
	len += scnprintf(buf + len, buf_len - len, "tqm_dummy_frame = %u\n",
			 htt_stats_buf->tqm_dummy_frame);
	len += scnprintf(buf + len, buf_len - len, "tqm_notify_frame = %u\n",
			 htt_stats_buf->tqm_notify_frame);
	len += scnprintf(buf + len, buf_len - len, "fw2wbm_enq = %u\n",
			 htt_stats_buf->fw2wbm_enq);
	len += scnprintf(buf + len, buf_len - len, "tqm_bypass_frame = %u\n\n",
			 htt_stats_buf->tqm_bypass_frame);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_fw2wbm_ring_full_hist_tlv(const void *tag_buf,
					  u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_fw2wbm_ring_full_hist_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16  num_elements = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_FW2WBM_RING_FULL_HIST_TLV");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fw2wbm_ring_full_hist,
			   "fw2wbm_ring_full_hist", num_elements, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_tx_de_cmn_stats_tlv(const void *tag_buf, struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_de_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "tcl2fw_entry_count = %u\n",
			 htt_stats_buf->tcl2fw_entry_count);
	len += scnprintf(buf + len, buf_len - len, "not_to_fw = %u\n",
			 htt_stats_buf->not_to_fw);
	len += scnprintf(buf + len, buf_len - len, "invalid_pdev_vdev_peer = %u\n",
			 htt_stats_buf->invalid_pdev_vdev_peer);
	len += scnprintf(buf + len, buf_len - len, "tcl_res_invalid_addrx = %u\n",
			 htt_stats_buf->tcl_res_invalid_addrx);
	len += scnprintf(buf + len, buf_len - len, "wbm2fw_entry_count = %u\n",
			 htt_stats_buf->wbm2fw_entry_count);
	len += scnprintf(buf + len, buf_len - len, "invalid_pdev = %u\n\n",
			 htt_stats_buf->invalid_pdev);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_ring_if_stats_tlv(const void *tag_buf,
					       struct debug_htt_stats_req *stats_req)
{
	const struct htt_ring_if_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RING_IF_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "base_addr = %u\n",
			 htt_stats_buf->base_addr);
	len += scnprintf(buf + len, buf_len - len, "elem_size = %u\n",
			 htt_stats_buf->elem_size);
	len += scnprintf(buf + len, buf_len - len, "num_elems = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_NUM_ELEMS,
				   htt_stats_buf->num_elems__prefetch_tail_idx));
	len += scnprintf(buf + len, buf_len - len, "prefetch_tail_idx = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_PREFETCH_TAIL_INDEX,
				   htt_stats_buf->num_elems__prefetch_tail_idx));
	len += scnprintf(buf + len, buf_len - len, "head_idx = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_HEAD_IDX,
				   htt_stats_buf->head_idx__tail_idx));
	len += scnprintf(buf + len, buf_len - len, "tail_idx = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_TAIL_IDX,
				   htt_stats_buf->head_idx__tail_idx));
	len += scnprintf(buf + len, buf_len - len, "shadow_head_idx = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_SHADOW_HEAD_IDX,
				   htt_stats_buf->shadow_head_idx__shadow_tail_idx));
	len += scnprintf(buf + len, buf_len - len, "shadow_tail_idx = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_SHADOW_TAIL_IDX,
				   htt_stats_buf->shadow_head_idx__shadow_tail_idx));
	len += scnprintf(buf + len, buf_len - len, "num_tail_incr = %u\n",
			 htt_stats_buf->num_tail_incr);
	len += scnprintf(buf + len, buf_len - len, "lwm_thresh = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_LWM_THRESH,
				   htt_stats_buf->lwm_thresh__hwm_thresh));
	len += scnprintf(buf + len, buf_len - len, "hwm_thresh = %lu\n",
			 FIELD_GET(HTT_RING_IF_STATS_HWM_THRESH,
				   htt_stats_buf->lwm_thresh__hwm_thresh));
	len += scnprintf(buf + len, buf_len - len, "overrun_hit_count = %u\n",
			 htt_stats_buf->overrun_hit_count);
	len += scnprintf(buf + len, buf_len - len, "underrun_hit_count = %u\n",
			 htt_stats_buf->underrun_hit_count);
	len += scnprintf(buf + len, buf_len - len, "prod_blockwait_count = %u\n",
			 htt_stats_buf->prod_blockwait_count);
	len += scnprintf(buf + len, buf_len - len, "cons_blockwait_count = %u\n",
			 htt_stats_buf->cons_blockwait_count);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->low_wm_hit_count,
			   "low_wm_hit_count", HTT_STATS_LOW_WM_BINS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->high_wm_hit_count,
			   "high_wm_hit_count", HTT_STATS_HIGH_WM_BINS, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_ring_if_cmn_tlv(const void *tag_buf,
					     struct debug_htt_stats_req *stats_req)
{
	const struct htt_ring_if_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RING_IF_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n\n",
			 htt_stats_buf->num_records);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_sfm_client_user_tlv_v(const void *tag_buf,
						   u16 tag_len,
						   struct debug_htt_stats_req *stats_req)
{
	const struct htt_sfm_client_user_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CLIENT_USER_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->dwords_used_by_user_n,
			   "dwords_used_by_user_n", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_sfm_client_tlv(const void *tag_buf,
					    struct debug_htt_stats_req *stats_req)
{
	const struct htt_sfm_client_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CLIENT_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "client_id = %u\n",
			 htt_stats_buf->client_id);
	len += scnprintf(buf + len, buf_len - len, "buf_min = %u\n",
			 htt_stats_buf->buf_min);
	len += scnprintf(buf + len, buf_len - len, "buf_max = %u\n",
			 htt_stats_buf->buf_max);
	len += scnprintf(buf + len, buf_len - len, "buf_busy = %u\n",
			 htt_stats_buf->buf_busy);
	len += scnprintf(buf + len, buf_len - len, "buf_alloc = %u\n",
			 htt_stats_buf->buf_alloc);
	len += scnprintf(buf + len, buf_len - len, "buf_avail = %u\n",
			 htt_stats_buf->buf_avail);
	len += scnprintf(buf + len, buf_len - len, "num_users = %u\n\n",
			 htt_stats_buf->num_users);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_sfm_cmn_tlv(const void *tag_buf,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_sfm_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "buf_total = %u\n",
			 htt_stats_buf->buf_total);
	len += scnprintf(buf + len, buf_len - len, "mem_empty = %u\n",
			 htt_stats_buf->mem_empty);
	len += scnprintf(buf + len, buf_len - len, "deallocate_bufs = %u\n",
			 htt_stats_buf->deallocate_bufs);
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n\n",
			 htt_stats_buf->num_records);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_sring_stats_tlv(const void *tag_buf,
					     struct debug_htt_stats_req *stats_req)
{
	const struct htt_sring_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_SRING_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_MAC_ID,
				   htt_stats_buf->mac_id__ring_id__arena__ep));
	len += scnprintf(buf + len, buf_len - len, "ring_id = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_RING_ID,
				   htt_stats_buf->mac_id__ring_id__arena__ep));
	len += scnprintf(buf + len, buf_len - len, "arena = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_ARENA,
				   htt_stats_buf->mac_id__ring_id__arena__ep));
	len += scnprintf(buf + len, buf_len - len, "ep = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_EP,
				   htt_stats_buf->mac_id__ring_id__arena__ep));
	len += scnprintf(buf + len, buf_len - len, "base_addr_lsb = 0x%x\n",
			 htt_stats_buf->base_addr_lsb);
	len += scnprintf(buf + len, buf_len - len, "base_addr_msb = 0x%x\n",
			 htt_stats_buf->base_addr_msb);
	len += scnprintf(buf + len, buf_len - len, "ring_size = %u\n",
			 htt_stats_buf->ring_size);
	len += scnprintf(buf + len, buf_len - len, "elem_size = %u\n",
			 htt_stats_buf->elem_size);
	len += scnprintf(buf + len, buf_len - len, "num_avail_words = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_NUM_AVAIL_WORDS,
				   htt_stats_buf->num_avail_words__num_valid_words));
	len += scnprintf(buf + len, buf_len - len, "num_valid_words = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_NUM_VALID_WORDS,
				   htt_stats_buf->num_avail_words__num_valid_words));
	len += scnprintf(buf + len, buf_len - len, "head_ptr = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_HEAD_PTR,
				   htt_stats_buf->head_ptr__tail_ptr));
	len += scnprintf(buf + len, buf_len - len, "tail_ptr = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_TAIL_PTR,
				   htt_stats_buf->head_ptr__tail_ptr));
	len += scnprintf(buf + len, buf_len - len, "consumer_empty = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_CONSUMER_EMPTY,
				   htt_stats_buf->consumer_empty__producer_full));
	len += scnprintf(buf + len, buf_len - len, "producer_full = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_PRODUCER_FULL,
				   htt_stats_buf->consumer_empty__producer_full));
	len += scnprintf(buf + len, buf_len - len, "prefetch_count = %lu\n",
			 FIELD_GET(HTT_SRING_STATS_PREFETCH_COUNT,
				   htt_stats_buf->prefetch_count__internal_tail_ptr));
	len += scnprintf(buf + len, buf_len - len, "internal_tail_ptr = %lu\n\n",
			 FIELD_GET(HTT_SRING_STATS_INTERNAL_TAIL_PTR,
				   htt_stats_buf->prefetch_count__internal_tail_ptr));

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_sring_cmn_tlv(const void *tag_buf,
					   struct debug_htt_stats_req *stats_req)
{
	const struct htt_sring_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_SRING_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n\n",
			 htt_stats_buf->num_records);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_tx_pdev_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_pdev_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 j;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "tx_ldpc = %u\n",
			 htt_stats_buf->tx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_tx_ldpc = %u\n",
			 htt_stats_buf->ac_mu_mimo_tx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_tx_ldpc = %u\n",
			 htt_stats_buf->ax_mu_mimo_tx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "ofdma_tx_ldpc = %u\n",
			 htt_stats_buf->ofdma_tx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 htt_stats_buf->rts_cnt);
	len += scnprintf(buf + len, buf_len - len, "rts_success = %u\n",
			 htt_stats_buf->rts_success);
	len += scnprintf(buf + len, buf_len - len, "ack_rssi = %u\n",
			 htt_stats_buf->ack_rssi);

	len += scnprintf(buf + len, buf_len - len,
			 "Legacy CCK Rates: 1 Mbps: %u, 2 Mbps: %u, 5.5 Mbps: %u, 11 Mbps: %u\n",
			 htt_stats_buf->tx_legacy_cck_rate[0],
			 htt_stats_buf->tx_legacy_cck_rate[1],
			 htt_stats_buf->tx_legacy_cck_rate[2],
			 htt_stats_buf->tx_legacy_cck_rate[3]);

	len += scnprintf(buf + len, buf_len - len,
			 "Legacy OFDM Rates: 6 Mbps: %u, 9 Mbps: %u, 12 Mbps: %u, 18 Mbps: %u\n"
			 "                   24 Mbps: %u, 36 Mbps: %u, 48 Mbps: %u, 54 Mbps: %u\n",
			 htt_stats_buf->tx_legacy_ofdm_rate[0],
			 htt_stats_buf->tx_legacy_ofdm_rate[1],
			 htt_stats_buf->tx_legacy_ofdm_rate[2],
			 htt_stats_buf->tx_legacy_ofdm_rate[3],
			 htt_stats_buf->tx_legacy_ofdm_rate[4],
			 htt_stats_buf->tx_legacy_ofdm_rate[5],
			 htt_stats_buf->tx_legacy_ofdm_rate[6],
			 htt_stats_buf->tx_legacy_ofdm_rate[7]);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_mcs, "tx_mcs",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ac_mu_mimo_tx_mcs,
			   "ac_mu_mimo_tx_mcs", HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ax_mu_mimo_tx_mcs,
			   "ax_mu_mimo_tx_mcs", HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ofdma_tx_mcs, "ofdma_tx_mcs",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_nss, "tx_nss",
			   HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ac_mu_mimo_tx_nss,
			   "ac_mu_mimo_tx_nss",
			   HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ax_mu_mimo_tx_nss,
			   "ax_mu_mimo_tx_nss",
			   HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ofdma_tx_nss, "ofdma_tx_nss",
			   HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_bw, "tx_bw",
			   HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ac_mu_mimo_tx_bw,
			   "ac_mu_mimo_tx_bw", HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ax_mu_mimo_tx_bw,
			   "ax_mu_mimo_tx_bw",
			   HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ofdma_tx_bw, "ofdma_tx_bw",
			   HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_stbc, "tx_stbc",
			   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_pream, "tx_pream",
			   HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");

	len += scnprintf(buf + len, buf_len - len, "HE LTF: 1x: %u, 2x: %u, 4x: %u\n",
			 htt_stats_buf->tx_he_ltf[1],
			 htt_stats_buf->tx_he_ltf[2],
			 htt_stats_buf->tx_he_ltf[3]);

	/* SU GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "tx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_gi[j], NULL,
				   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	/* AC MU-MIMO GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ac_mu_mimo_tx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ac_mu_mimo_tx_gi[j],
				   NULL, HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	/* AX MU-MIMO GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ax_mu_mimo_tx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ax_mu_mimo_tx_gi[j],
				   NULL, HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	/* DL OFDMA GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ofdma_tx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ofdma_tx_gi[j], NULL,
				   HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->tx_dcm, "tx_dcm",
			   HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_rx_pdev_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 i, j;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "nsts = %u\n",
			 htt_stats_buf->nsts);
	len += scnprintf(buf + len, buf_len - len, "rx_ldpc = %u\n",
			 htt_stats_buf->rx_ldpc);
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 htt_stats_buf->rts_cnt);
	len += scnprintf(buf + len, buf_len - len, "rssi_mgmt = %u\n",
			 htt_stats_buf->rssi_mgmt);
	len += scnprintf(buf + len, buf_len - len, "rssi_data = %u\n",
			 htt_stats_buf->rssi_data);
	len += scnprintf(buf + len, buf_len - len, "rssi_comb = %u\n",
			 htt_stats_buf->rssi_comb);
	len += scnprintf(buf + len, buf_len - len, "rssi_in_dbm = %d\n",
			 htt_stats_buf->rssi_in_dbm);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_mcs, "rx_mcs",
			   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_nss, "rx_nss",
			   HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_dcm, "rx_dcm",
			   HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_stbc, "rx_stbc",
			   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_bw, "rx_bw",
			   HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");

	len += scnprintf(buf + len, buf_len - len, "rx_evm_nss_count = %u\n",
			 htt_stats_buf->nss_count);

	len += scnprintf(buf + len, buf_len - len, "rx_evm_pilot_count = %u\n",
			 htt_stats_buf->pilot_count);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "pilot_evm_db[%u] = ", j);
		for (i = 0; i < HTT_RX_PDEV_STATS_RXEVM_MAX_PILOTS_PER_NSS; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u:%d,",
					 i,
					 htt_stats_buf->rx_pilot_evm_db[j][i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	len += scnprintf(buf + len, buf_len - len,
			 "pilot_evm_db_mean = ");
	for (i = 0; i < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len,
				 buf_len - len,
				 " %u:%d,", i,
				 htt_stats_buf->rx_pilot_evm_db_mean[i]);
	len += scnprintf(buf + len, buf_len - len, "\n");

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rssi_chain[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rssi_chain[j], NULL,
				   HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	}

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_gi[j], NULL,
				   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_pream, "rx_pream",
			   HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");

	len += scnprintf(buf + len, buf_len - len, "rx_11ax_su_ext = %u\n",
			 htt_stats_buf->rx_11ax_su_ext);
	len += scnprintf(buf + len, buf_len - len, "rx_11ac_mumimo = %u\n",
			 htt_stats_buf->rx_11ac_mumimo);
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_mumimo = %u\n",
			 htt_stats_buf->rx_11ax_mumimo);
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ofdma = %u\n",
			 htt_stats_buf->rx_11ax_ofdma);
	len += scnprintf(buf + len, buf_len - len, "txbf = %u\n",
			 htt_stats_buf->txbf);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_legacy_cck_rate,
			   "rx_legacy_cck_rate",
			   HTT_RX_PDEV_STATS_NUM_LEGACY_CCK_STATS, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_legacy_ofdm_rate,
			   "rx_legacy_ofdm_rate",
			   HTT_RX_PDEV_STATS_NUM_LEGACY_OFDM_STATS, "\n");

	len += scnprintf(buf + len, buf_len - len, "rx_active_dur_us_low = %u\n",
			 htt_stats_buf->rx_active_dur_us_low);
	len += scnprintf(buf + len, buf_len - len, "rx_active_dur_us_high = %u\n",
			 htt_stats_buf->rx_active_dur_us_high);
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ul_ofdma = %u\n",
			 htt_stats_buf->rx_11ax_ul_ofdma);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ul_ofdma_rx_mcs,
			   "ul_ofdma_rx_mcs",
			   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ul_ofdma_rx_gi[%u] = ", j);
		PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ul_ofdma_rx_gi[j], NULL,
				   HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	}

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ul_ofdma_rx_nss,
			   "ul_ofdma_rx_nss",
			   HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->ul_ofdma_rx_bw, "ul_ofdma_rx_bw",
			   HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");

	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_stbc = %u\n",
			htt_stats_buf->ul_ofdma_rx_stbc);
	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_ldpc = %u\n",
			htt_stats_buf->ul_ofdma_rx_ldpc);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_ulofdma_non_data_ppdu,
			   "rx_ulofdma_non_data_ppdu",
			   HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_ulofdma_data_ppdu,
			   "rx_ulofdma_data_ppdu", HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_ulofdma_mpdu_ok,
			   "rx_ulofdma_mpdu_ok", HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rx_ulofdma_mpdu_fail,
			   "rx_ulofdma_mpdu_fail", HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ul_fd_rssi: nss[%u] = ", j);
		for (i = 0; i < HTT_RX_PDEV_MAX_OFDMA_NUM_USER; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u:%d,",
					 i, htt_stats_buf->rx_ul_fd_rssi[j][i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	len += scnprintf(buf + len, buf_len - len, "per_chain_rssi_pkt_type = %#x\n",
			 htt_stats_buf->per_chain_rssi_pkt_type);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_per_chain_rssi_in_dbm[%u] = ", j);
		for (i = 0; i < HTT_RX_PDEV_STATS_NUM_BW_COUNTERS; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u:%d,",
					 i,
					 htt_stats_buf->rx_per_chain_rssi_in_dbm[j][i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}
	len += scnprintf(buf + len, buf_len - len, "\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_rx_soc_fw_stats_tlv(const void *tag_buf,
						 struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_SOC_FW_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "fw_reo_ring_data_msdu = %u\n",
			 htt_stats_buf->fw_reo_ring_data_msdu);
	len += scnprintf(buf + len, buf_len - len, "fw_to_host_data_msdu_bcmc = %u\n",
			 htt_stats_buf->fw_to_host_data_msdu_bcmc);
	len += scnprintf(buf + len, buf_len - len, "fw_to_host_data_msdu_uc = %u\n",
			 htt_stats_buf->fw_to_host_data_msdu_uc);
	len += scnprintf(buf + len, buf_len - len,
			 "ofld_remote_data_buf_recycle_cnt = %u\n",
			 htt_stats_buf->ofld_remote_data_buf_recycle_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "ofld_remote_free_buf_indication_cnt = %u\n",
			 htt_stats_buf->ofld_remote_free_buf_indication_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "ofld_buf_to_host_data_msdu_uc = %u\n",
			 htt_stats_buf->ofld_buf_to_host_data_msdu_uc);
	len += scnprintf(buf + len, buf_len - len,
			 "reo_fw_ring_to_host_data_msdu_uc = %u\n",
			 htt_stats_buf->reo_fw_ring_to_host_data_msdu_uc);
	len += scnprintf(buf + len, buf_len - len, "wbm_sw_ring_reap = %u\n",
			 htt_stats_buf->wbm_sw_ring_reap);
	len += scnprintf(buf + len, buf_len - len, "wbm_forward_to_host_cnt = %u\n",
			 htt_stats_buf->wbm_forward_to_host_cnt);
	len += scnprintf(buf + len, buf_len - len, "wbm_target_recycle_cnt = %u\n",
			 htt_stats_buf->wbm_target_recycle_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "target_refill_ring_recycle_cnt = %u\n",
			 htt_stats_buf->target_refill_ring_recycle_cnt);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_soc_fw_refill_ring_empty_tlv_v(const void *tag_buf,
					    u16 tag_len,
					    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_refill_ring_empty_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_REFILL_MAX_RING);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_SOC_FW_REFILL_RING_EMPTY_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->refill_ring_empty_cnt,
			   "refill_ring_empty_cnt", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_soc_fw_refill_ring_num_rxdma_err_tlv_v(const void *tag_buf,
						    u16 tag_len,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_refill_ring_num_rxdma_err_tlv_v *htt_stats_buf =
		tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_RXDMA_MAX_ERR_CODE);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_SOC_FW_REFILL_RING_NUM_RXDMA_ERR_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->rxdma_err, "rxdma_err",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_soc_fw_refill_ring_num_reo_err_tlv_v(const void *tag_buf,
						  u16 tag_len,
						  struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_refill_ring_num_reo_err_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_REO_MAX_ERR_CODE);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_SOC_FW_REFILL_RING_NUM_REO_ERR_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->reo_err, "reo_err",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_reo_debug_stats_tlv_v(const void *tag_buf,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_reo_resource_stats_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_REO_RESOURCE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "sample_id = %u\n",
			 htt_stats_buf->sample_id);
	len += scnprintf(buf + len, buf_len - len, "total_max = %u\n",
			 htt_stats_buf->total_max);
	len += scnprintf(buf + len, buf_len - len, "total_avg = %u\n",
			 htt_stats_buf->total_avg);
	len += scnprintf(buf + len, buf_len - len, "total_sample = %u\n",
			 htt_stats_buf->total_sample);
	len += scnprintf(buf + len, buf_len - len, "non_zeros_avg = %u\n",
			 htt_stats_buf->non_zeros_avg);
	len += scnprintf(buf + len, buf_len - len, "non_zeros_sample = %u\n",
			 htt_stats_buf->non_zeros_sample);
	len += scnprintf(buf + len, buf_len - len, "last_non_zeros_max = %u\n",
			 htt_stats_buf->last_non_zeros_max);
	len += scnprintf(buf + len, buf_len - len, "last_non_zeros_min %u\n",
			 htt_stats_buf->last_non_zeros_min);
	len += scnprintf(buf + len, buf_len - len, "last_non_zeros_avg %u\n",
			 htt_stats_buf->last_non_zeros_avg);
	len += scnprintf(buf + len, buf_len - len, "last_non_zeros_sample %u\n\n",
			 htt_stats_buf->last_non_zeros_sample);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_soc_fw_refill_ring_num_refill_tlv_v(const void *tag_buf,
						 u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_refill_ring_num_refill_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_REFILL_MAX_RING);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_SOC_FW_REFILL_RING_NUM_REFILL_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->refill_ring_num_refill,
			   "refill_ring_num_refill", num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_rx_pdev_fw_stats_tlv(const void *tag_buf,
						  struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_fw_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_FW_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "ppdu_recvd = %u\n",
			 htt_stats_buf->ppdu_recvd);
	len += scnprintf(buf + len, buf_len - len, "mpdu_cnt_fcs_ok = %u\n",
			 htt_stats_buf->mpdu_cnt_fcs_ok);
	len += scnprintf(buf + len, buf_len - len, "mpdu_cnt_fcs_err = %u\n",
			 htt_stats_buf->mpdu_cnt_fcs_err);
	len += scnprintf(buf + len, buf_len - len, "tcp_msdu_cnt = %u\n",
			 htt_stats_buf->tcp_msdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "tcp_ack_msdu_cnt = %u\n",
			 htt_stats_buf->tcp_ack_msdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "udp_msdu_cnt = %u\n",
			 htt_stats_buf->udp_msdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "other_msdu_cnt = %u\n",
			 htt_stats_buf->other_msdu_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_ring_mpdu_ind = %u\n",
			 htt_stats_buf->fw_ring_mpdu_ind);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fw_ring_mgmt_subtype,
			   "fw_ring_mgmt_subtype", HTT_STATS_SUBTYPE_MAX, "\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fw_ring_ctrl_subtype,
			   "fw_ring_ctrl_subtype", HTT_STATS_SUBTYPE_MAX, "\n");

	len += scnprintf(buf + len, buf_len - len, "fw_ring_mcast_data_msdu = %u\n",
			 htt_stats_buf->fw_ring_mcast_data_msdu);
	len += scnprintf(buf + len, buf_len - len, "fw_ring_bcast_data_msdu = %u\n",
			 htt_stats_buf->fw_ring_bcast_data_msdu);
	len += scnprintf(buf + len, buf_len - len, "fw_ring_ucast_data_msdu = %u\n",
			 htt_stats_buf->fw_ring_ucast_data_msdu);
	len += scnprintf(buf + len, buf_len - len, "fw_ring_null_data_msdu = %u\n",
			 htt_stats_buf->fw_ring_null_data_msdu);
	len += scnprintf(buf + len, buf_len - len, "fw_ring_mpdu_drop = %u\n",
			 htt_stats_buf->fw_ring_mpdu_drop);
	len += scnprintf(buf + len, buf_len - len, "ofld_local_data_ind_cnt = %u\n",
			 htt_stats_buf->ofld_local_data_ind_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "ofld_local_data_buf_recycle_cnt = %u\n",
			 htt_stats_buf->ofld_local_data_buf_recycle_cnt);
	len += scnprintf(buf + len, buf_len - len, "drx_local_data_ind_cnt = %u\n",
			 htt_stats_buf->drx_local_data_ind_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "drx_local_data_buf_recycle_cnt = %u\n",
			 htt_stats_buf->drx_local_data_buf_recycle_cnt);
	len += scnprintf(buf + len, buf_len - len, "local_nondata_ind_cnt = %u\n",
			 htt_stats_buf->local_nondata_ind_cnt);
	len += scnprintf(buf + len, buf_len - len, "local_nondata_buf_recycle_cnt = %u\n",
			 htt_stats_buf->local_nondata_buf_recycle_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_status_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->fw_status_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_status_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->fw_status_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_pkt_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->fw_pkt_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_pkt_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->fw_pkt_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_link_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->fw_link_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "fw_link_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->fw_link_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "host_pkt_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->host_pkt_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "host_pkt_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->host_pkt_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_pkt_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->mon_pkt_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_pkt_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->mon_pkt_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len,
			 "mon_status_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->mon_status_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_status_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->mon_status_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_desc_buf_ring_refill_cnt = %u\n",
			 htt_stats_buf->mon_desc_buf_ring_refill_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_desc_buf_ring_empty_cnt = %u\n",
			 htt_stats_buf->mon_desc_buf_ring_empty_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_dest_ring_update_cnt = %u\n",
			 htt_stats_buf->mon_dest_ring_update_cnt);
	len += scnprintf(buf + len, buf_len - len, "mon_dest_ring_full_cnt = %u\n",
			 htt_stats_buf->mon_dest_ring_full_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_suspend_cnt = %u\n",
			 htt_stats_buf->rx_suspend_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_suspend_fail_cnt = %u\n",
			 htt_stats_buf->rx_suspend_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_resume_cnt = %u\n",
			 htt_stats_buf->rx_resume_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_resume_fail_cnt = %u\n",
			 htt_stats_buf->rx_resume_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_ring_switch_cnt = %u\n",
			 htt_stats_buf->rx_ring_switch_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_ring_restore_cnt = %u\n",
			 htt_stats_buf->rx_ring_restore_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_flush_cnt = %u\n",
			 htt_stats_buf->rx_flush_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_recovery_reset_cnt = %u\n\n",
			 htt_stats_buf->rx_recovery_reset_cnt);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_pdev_fw_ring_mpdu_err_tlv_v(const void *tag_buf,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_fw_ring_mpdu_err_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_PDEV_FW_RING_MPDU_ERR_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fw_ring_mpdu_err,
			   "fw_ring_mpdu_err", HTT_RX_STATS_RXDMA_MAX_ERR, "\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_pdev_fw_mpdu_drop_tlv_v(const void *tag_buf,
				     u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_fw_mpdu_drop_tlv_v *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_FW_DROP_REASON_MAX);

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_FW_MPDU_DROP_TLV_V:\n");

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->fw_mpdu_drop, "fw_mpdu_drop",
			   num_elems, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_rx_pdev_fw_stats_phy_err_tlv(const void *tag_buf,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_fw_stats_phy_err_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_FW_STATS_PHY_ERR_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id__word = %u\n",
			 htt_stats_buf->mac_id__word);
	len += scnprintf(buf + len, buf_len - len, "total_phy_err_nct = %u\n",
			 htt_stats_buf->total_phy_err_cnt);

	PRINT_ARRAY_TO_BUF(buf, len, htt_stats_buf->phy_err, "phy_errs",
			   HTT_STATS_PHY_ERR_MAX, "\n\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_pdev_cca_stats_hist_tlv(const void *tag_buf,
				  struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_cca_stats_hist_v1_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "\nHTT_PDEV_CCA_STATS_HIST_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "chan_num = %u\n",
			 htt_stats_buf->chan_num);
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n",
			 htt_stats_buf->num_records);
	len += scnprintf(buf + len, buf_len - len, "valid_cca_counters_bitmap = 0x%x\n",
			 htt_stats_buf->valid_cca_counters_bitmap);
	len += scnprintf(buf + len, buf_len - len, "collection_interval = %u\n\n",
			 htt_stats_buf->collection_interval);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_PDEV_STATS_CCA_COUNTERS_TLV:(in usec)\n");
	len += scnprintf(buf + len, buf_len - len,
			 "|  tx_frame|   rx_frame|   rx_clear| my_rx_frame|        cnt| med_rx_idle| med_tx_idle_global|   cca_obss|\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_pdev_stats_cca_counters_tlv(const void *tag_buf,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_stats_cca_counters_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len,
			 "|%10u| %10u| %10u| %11u| %10u| %11u| %18u| %10u|\n",
			 htt_stats_buf->tx_frame_usec,
			 htt_stats_buf->rx_frame_usec,
			 htt_stats_buf->rx_clear_usec,
			 htt_stats_buf->my_rx_frame_usec,
			 htt_stats_buf->usec_cnt,
			 htt_stats_buf->med_rx_idle_usec,
			 htt_stats_buf->med_tx_idle_global_usec,
			 htt_stats_buf->cca_obss_usec);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_hw_stats_whal_tx_tlv(const void *tag_buf,
						  struct debug_htt_stats_req *stats_req)
{
	const struct htt_hw_stats_whal_tx_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_WHAL_TX_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %lu\n",
			 FIELD_GET(HTT_STATS_MAC_ID, htt_stats_buf->mac_id__word));
	len += scnprintf(buf + len, buf_len - len, "last_unpause_ppdu_id = %u\n",
			 htt_stats_buf->last_unpause_ppdu_id);
	len += scnprintf(buf + len, buf_len - len, "hwsch_unpause_wait_tqm_write = %u\n",
			 htt_stats_buf->hwsch_unpause_wait_tqm_write);
	len += scnprintf(buf + len, buf_len - len, "hwsch_dummy_tlv_skipped = %u\n",
			 htt_stats_buf->hwsch_dummy_tlv_skipped);
	len += scnprintf(buf + len, buf_len - len,
			 "hwsch_misaligned_offset_received = %u\n",
			 htt_stats_buf->hwsch_misaligned_offset_received);
	len += scnprintf(buf + len, buf_len - len, "hwsch_reset_count = %u\n",
			 htt_stats_buf->hwsch_reset_count);
	len += scnprintf(buf + len, buf_len - len, "hwsch_dev_reset_war = %u\n",
			 htt_stats_buf->hwsch_dev_reset_war);
	len += scnprintf(buf + len, buf_len - len, "hwsch_delayed_pause = %u\n",
			 htt_stats_buf->hwsch_delayed_pause);
	len += scnprintf(buf + len, buf_len - len, "hwsch_long_delayed_pause = %u\n",
			 htt_stats_buf->hwsch_long_delayed_pause);
	len += scnprintf(buf + len, buf_len - len, "sch_rx_ppdu_no_response = %u\n",
			 htt_stats_buf->sch_rx_ppdu_no_response);
	len += scnprintf(buf + len, buf_len - len, "sch_selfgen_response = %u\n",
			 htt_stats_buf->sch_selfgen_response);
	len += scnprintf(buf + len, buf_len - len, "sch_rx_sifs_resp_trigger= %u\n\n",
			 htt_stats_buf->sch_rx_sifs_resp_trigger);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_pdev_stats_twt_sessions_tlv(const void *tag_buf,
				      struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_stats_twt_sessions_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_STATS_TWT_SESSIONS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 htt_stats_buf->pdev_id);
	len += scnprintf(buf + len, buf_len - len, "num_sessions = %u\n\n",
			 htt_stats_buf->num_sessions);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_pdev_stats_twt_session_tlv(const void *tag_buf,
				     struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_stats_twt_session_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_STATS_TWT_SESSION_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "vdev_id = %u\n",
			 htt_stats_buf->vdev_id);
	len += scnprintf(buf + len, buf_len - len,
			 "peer_mac = %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
			 FIELD_GET(HTT_MAC_ADDR_L32_0,
				   htt_stats_buf->peer_mac.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_1,
				   htt_stats_buf->peer_mac.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_2,
				   htt_stats_buf->peer_mac.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_L32_3,
				   htt_stats_buf->peer_mac.mac_addr_l32),
			 FIELD_GET(HTT_MAC_ADDR_H16_0,
				   htt_stats_buf->peer_mac.mac_addr_h16),
			 FIELD_GET(HTT_MAC_ADDR_H16_1,
				   htt_stats_buf->peer_mac.mac_addr_h16));
	len += scnprintf(buf + len, buf_len - len, "flow_id_flags = %u\n",
			 htt_stats_buf->flow_id_flags);
	len += scnprintf(buf + len, buf_len - len, "dialog_id = %u\n",
			 htt_stats_buf->dialog_id);
	len += scnprintf(buf + len, buf_len - len, "wake_dura_us = %u\n",
			 htt_stats_buf->wake_dura_us);
	len += scnprintf(buf + len, buf_len - len, "wake_intvl_us = %u\n",
			 htt_stats_buf->wake_intvl_us);
	len += scnprintf(buf + len, buf_len - len, "sp_offset_us = %u\n\n",
			 htt_stats_buf->sp_offset_us);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void
htt_print_pdev_obss_pd_stats_tlv_v(const void *tag_buf,
				   struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_obss_pd_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "OBSS Tx success PPDU = %u\n",
			   htt_stats_buf->num_obss_tx_ppdu_success);
	len += scnprintf(buf + len, buf_len - len, "OBSS Tx failures PPDU = %u\n",
			   htt_stats_buf->num_obss_tx_ppdu_failure);
	len += scnprintf(buf + len, buf_len - len, "Non-SRG Opportunities = %u\n",
			   htt_stats_buf->num_non_srg_opportunities);
	len += scnprintf(buf + len, buf_len - len, "Non-SRG tried PPDU = %u\n",
			   htt_stats_buf->num_non_srg_ppdu_tried);
	len += scnprintf(buf + len, buf_len - len, "Non-SRG success PPDU = %u\n",
			   htt_stats_buf->num_non_srg_ppdu_success);
	len += scnprintf(buf + len, buf_len - len, "SRG Opportunities = %u\n",
			   htt_stats_buf->num_srg_opportunities);
	len += scnprintf(buf + len, buf_len - len, "SRG tried PPDU = %u\n",
			   htt_stats_buf->num_srg_ppdu_tried);
	len += scnprintf(buf + len, buf_len - len, "SRG success PPDU = %u\n\n",
			   htt_stats_buf->num_srg_ppdu_success);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_print_backpressure_stats_tlv_v(const u32 *tag_buf,
						      u8 *data)
{
	struct debug_htt_stats_req *stats_req =
			(struct debug_htt_stats_req *)data;
	struct htt_ring_backpressure_stats_tlv *htt_stats_buf =
			(struct htt_ring_backpressure_stats_tlv *)tag_buf;
	int i;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 htt_stats_buf->pdev_id);
	len += scnprintf(buf + len, buf_len - len, "current_head_idx = %u\n",
			 htt_stats_buf->current_head_idx);
	len += scnprintf(buf + len, buf_len - len, "current_tail_idx = %u\n",
			 htt_stats_buf->current_tail_idx);
	len += scnprintf(buf + len, buf_len - len, "num_htt_msgs_sent = %u\n",
			 htt_stats_buf->num_htt_msgs_sent);
	len += scnprintf(buf + len, buf_len - len,
			 "backpressure_time_ms = %u\n",
			 htt_stats_buf->backpressure_time_ms);

	for (i = 0; i < 5; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "backpressure_hist_%u = %u\n",
				 i + 1, htt_stats_buf->backpressure_hist[i]);

	len += scnprintf(buf + len, buf_len - len,
			 "============================\n");

	if (len >= buf_len) {
		buf[buf_len - 1] = 0;
		stats_req->buf_len = buf_len - 1;
	} else {
		buf[len] = 0;
		stats_req->buf_len = len;
	}
}

static inline
void htt_print_pdev_tx_rate_txbf_stats_tlv(const void *tag_buf,
					   struct debug_htt_stats_req *stats_req)
{
	const struct htt_pdev_txrate_txbf_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_STATS_PDEV_TX_RATE_TXBF_STATS:\n");

	len += scnprintf(buf + len, buf_len - len, "tx_ol_mcs = ");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_MCS_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ol_mcs[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_ibf_mcs = ");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_MCS_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ibf_mcs[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_txbf_mcs =");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_MCS_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_txbf_mcs[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_ol_nss = ");
	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ol_nss[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_ibf_nss = ");
	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ibf_nss[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_txbf_nss = ");
	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_txbf_nss[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_ol_bw = ");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_BW_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ol_bw[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_ibf_bw = ");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_BW_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_ibf_bw[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\ntx_txbf_bw = ");
	for (i = 0; i < HTT_TX_TXBF_RATE_STATS_NUM_BW_COUNTERS; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "%d:%u,", i, htt_stats_buf->tx_su_txbf_bw[i]);
	len--;

	len += scnprintf(buf + len, buf_len - len, "\n");

	stats_req->buf_len = len;
}

static inline
void htt_print_txbf_ofdma_ndpa_stats_tlv(const void *tag_buf,
					 struct debug_htt_stats_req *stats_req)
{
	const struct htt_txbf_ofdma_ndpa_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_NDPA_STATS_TLV:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndpa_queued_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndpa_queued[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndpa_tried_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndpa_tried[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndpa_flushed_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndpa_flushed[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndpa_err_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndpa_err[i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	stats_req->buf_len = len;
}

static inline
void htt_print_txbf_ofdma_ndp_stats_tlv(const void *tag_buf,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_txbf_ofdma_ndp_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_NDP_STATS_TLV:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndp_queued_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndp_queued[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndp_tried_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndp_tried[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndp_flushed_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndp_flushed[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_ndp_err_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_ndp_err[i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	stats_req->buf_len = len;
}

static inline
void htt_print_txbf_ofdma_brp_stats_tlv(const void *tag_buf,
					struct debug_htt_stats_req *stats_req)
{
	const struct htt_txbf_ofdma_brp_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_BRP_STATS_TLV:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_brpoll_queued_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_brpoll_queued[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_brpoll_tried_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_brpoll_tried[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_brpoll_flushed_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_brpoll_flushed[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_brp_err_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_brp_err[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_brp_err_num_cbf_rcvd_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_brp_err_num_cbf_rcvd[i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	stats_req->buf_len = len;
}

static inline
void htt_print_txbf_ofdma_steer_stats_tlv(const void *tag_buf,
					  struct debug_htt_stats_req *stats_req)
{
	const struct htt_txbf_ofdma_steer_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_STEER_STATS_TLV:\n");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_num_ppdu_steer_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_num_ppdu_steer[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_num_ppdu_ol_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_num_ppdu_ol[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_num_usrs_prefetch_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_num_usrs_prefetch[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_num_usrs_sound_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_num_usrs_sound[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_num_usrs_force_sound_user%d = %u\n",
				 i, htt_stats_buf->ax_ofdma_num_usrs_force_sound[i]);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	stats_req->buf_len = len;
}

static inline
void htt_print_phy_counters_tlv(const void *tag_buf,
				struct debug_htt_stats_req *stats_req)
{
	const struct htt_phy_counters_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_COUNTERS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "rx_ofdma_timing_err_cnt = %u\n",
			 htt_stats_buf->rx_ofdma_timing_err_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_cck_fail_cnt = %u\n",
			 htt_stats_buf->rx_cck_fail_cnt);
	len += scnprintf(buf + len, buf_len - len, "mactx_abort_cnt = %u\n",
			 htt_stats_buf->mactx_abort_cnt);
	len += scnprintf(buf + len, buf_len - len, "macrx_abort_cnt = %u\n",
			 htt_stats_buf->macrx_abort_cnt);
	len += scnprintf(buf + len, buf_len - len, "phytx_abort_cnt = %u\n",
			 htt_stats_buf->phytx_abort_cnt);
	len += scnprintf(buf + len, buf_len - len, "phyrx_abort_cnt = %u\n",
			 htt_stats_buf->phyrx_abort_cnt);
	len += scnprintf(buf + len, buf_len - len, "phyrx_defer_abort_cnt = %u\n",
			 htt_stats_buf->phyrx_defer_abort_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_gain_adj_lstf_event_cnt = %u\n",
			 htt_stats_buf->rx_gain_adj_lstf_event_cnt);
	len += scnprintf(buf + len, buf_len - len, "rx_gain_adj_non_legacy_cnt = %u\n",
			 htt_stats_buf->rx_gain_adj_non_legacy_cnt);

	for (i = 0; i < HTT_MAX_RX_PKT_CNT; i++)
		len += scnprintf(buf + len, buf_len - len, "rx_pkt_cnt[%d] = %u\n",
				 i, htt_stats_buf->rx_pkt_cnt[i]);

	for (i = 0; i < HTT_MAX_RX_PKT_CRC_PASS_CNT; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "rx_pkt_crc_pass_cnt[%d] = %u\n",
				 i, htt_stats_buf->rx_pkt_crc_pass_cnt[i]);

	for (i = 0; i < HTT_MAX_PER_BLK_ERR_CNT; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "per_blk_err_cnt[%d] = %u\n",
				 i, htt_stats_buf->per_blk_err_cnt[i]);

	for (i = 0; i < HTT_MAX_RX_OTA_ERR_CNT; i++)
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ota_err_cnt[%d] = %u\n",
				 i, htt_stats_buf->rx_ota_err_cnt[i]);

	stats_req->buf_len = len;
}

static inline
void htt_print_phy_stats_tlv(const void *tag_buf,
			     struct debug_htt_stats_req *stats_req)
{
	const struct htt_phy_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_STATS_TLV:\n");

	for (i = 0; i < HTT_STATS_MAX_CHAINS; i++)
		len += scnprintf(buf + len, buf_len - len, "nf_chain[%d] = %d\n",
				 i, htt_stats_buf->nf_chain[i]);

	len += scnprintf(buf + len, buf_len - len, "false_radar_cnt = %u\n",
			 htt_stats_buf->false_radar_cnt);
	len += scnprintf(buf + len, buf_len - len, "radar_cs_cnt = %u\n",
			 htt_stats_buf->radar_cs_cnt);
	len += scnprintf(buf + len, buf_len - len, "ani_level = %d\n",
			 htt_stats_buf->ani_level);
	len += scnprintf(buf + len, buf_len - len, "fw_run_time = %u\n",
			 htt_stats_buf->fw_run_time);

	stats_req->buf_len = len;
}

static inline
void htt_print_peer_ctrl_path_txrx_stats_tlv(const void *tag_buf,
					     struct debug_htt_stats_req *stats_req)
{
	const struct htt_peer_ctrl_path_txrx_stats_tlv *htt_stat_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	int i;
	const char *mgmt_frm_type[ATH11K_STATS_MGMT_FRM_TYPE_MAX - 1] = {
		"assoc_req", "assoc_resp",
		"reassoc_req", "reassoc_resp",
		"probe_req", "probe_resp",
		"timing_advertisement", "reserved",
		"beacon", "atim", "disassoc",
		"auth", "deauth", "action", "action_no_ack"};

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_STATS_PEER_CTRL_PATH_TXRX_STATS_TAG:\n");
	len += scnprintf(buf + len, buf_len - len,
			 "peer_mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
			 htt_stat_buf->peer_mac_addr[0], htt_stat_buf->peer_mac_addr[1],
			 htt_stat_buf->peer_mac_addr[2], htt_stat_buf->peer_mac_addr[3],
			 htt_stat_buf->peer_mac_addr[4], htt_stat_buf->peer_mac_addr[5]);

	len += scnprintf(buf + len, buf_len - len, "peer_tx_mgmt_subtype:\n");
	for (i = 0; i < ATH11K_STATS_MGMT_FRM_TYPE_MAX - 1; i++)
		len += scnprintf(buf + len, buf_len - len, "%s:%u\n",
				 mgmt_frm_type[i],
				 htt_stat_buf->peer_rx_mgmt_subtype[i]);

	len += scnprintf(buf + len, buf_len - len, "peer_rx_mgmt_subtype:\n");
	for (i = 0; i < ATH11K_STATS_MGMT_FRM_TYPE_MAX - 1; i++)
		len += scnprintf(buf + len, buf_len - len, "%s:%u\n",
				 mgmt_frm_type[i],
				 htt_stat_buf->peer_rx_mgmt_subtype[i]);

	len += scnprintf(buf + len, buf_len - len, "\n");

	stats_req->buf_len = len;
}

static int ath11k_dbg_htt_ext_stats_parse(struct ath11k_base *ab,
					  u16 tag, u16 len, const void *tag_buf,
					  void *user_data)
{
	struct debug_htt_stats_req *stats_req = user_data;

	switch (tag) {
	case HTT_STATS_TX_PDEV_CMN_TAG:
		htt_print_tx_pdev_stats_cmn_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TX_PDEV_UNDERRUN_TAG:
		htt_print_tx_pdev_stats_urrn_tlv_v(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_SIFS_TAG:
		htt_print_tx_pdev_stats_sifs_tlv_v(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_FLUSH_TAG:
		htt_print_tx_pdev_stats_flush_tlv_v(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_PHY_ERR_TAG:
		htt_print_tx_pdev_stats_phy_err_tlv_v(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_SIFS_HIST_TAG:
		htt_print_tx_pdev_stats_sifs_hist_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_PDEV_TX_PPDU_STATS_TAG:
		htt_print_tx_pdev_stats_tx_ppdu_stats_tlv_v(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_PDEV_TRIED_MPDU_CNT_HIST_TAG:
		htt_print_tx_pdev_stats_tried_mpdu_cnt_hist_tlv_v(tag_buf, len,
								  stats_req);
		break;

	case HTT_STATS_STRING_TAG:
		htt_print_stats_string_tlv(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_CMN_TAG:
		htt_print_tx_hwq_stats_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_HWQ_DIFS_LATENCY_TAG:
		htt_print_tx_hwq_difs_latency_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_CMD_RESULT_TAG:
		htt_print_tx_hwq_cmd_result_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_CMD_STALL_TAG:
		htt_print_tx_hwq_cmd_stall_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_FES_STATUS_TAG:
		htt_print_tx_hwq_fes_result_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_TRIED_MPDU_CNT_HIST_TAG:
		htt_print_tx_hwq_tried_mpdu_cnt_hist_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_HWQ_TXOP_USED_CNT_HIST_TAG:
		htt_print_tx_hwq_txop_used_cnt_hist_tlv_v(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_GEN_MPDU_TAG:
		htt_print_tx_tqm_gen_mpdu_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_TQM_LIST_MPDU_TAG:
		htt_print_tx_tqm_list_mpdu_stats_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_TQM_LIST_MPDU_CNT_TAG:
		htt_print_tx_tqm_list_mpdu_cnt_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_TQM_CMN_TAG:
		htt_print_tx_tqm_cmn_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_TQM_PDEV_TAG:
		htt_print_tx_tqm_pdev_stats_tlv_v(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_TQM_CMDQ_STATUS_TAG:
		htt_print_tx_tqm_cmdq_status_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_EAPOL_PACKETS_TAG:
		htt_print_tx_de_eapol_packets_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_CLASSIFY_FAILED_TAG:
		htt_print_tx_de_classify_failed_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_CLASSIFY_STATS_TAG:
		htt_print_tx_de_classify_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_CLASSIFY_STATUS_TAG:
		htt_print_tx_de_classify_status_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_ENQUEUE_PACKETS_TAG:
		htt_print_tx_de_enqueue_packets_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_ENQUEUE_DISCARD_TAG:
		htt_print_tx_de_enqueue_discard_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_FW2WBM_RING_FULL_HIST_TAG:
		htt_print_tx_de_fw2wbm_ring_full_hist_tlv(tag_buf, len, stats_req);
		break;

	case HTT_STATS_TX_DE_CMN_TAG:
		htt_print_tx_de_cmn_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RING_IF_TAG:
		htt_print_ring_if_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_PDEV_MU_MIMO_STATS_TAG:
		htt_print_tx_pdev_mu_mimo_sch_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SFM_CMN_TAG:
		htt_print_sfm_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SRING_STATS_TAG:
		htt_print_sring_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_PDEV_FW_STATS_TAG:
		htt_print_rx_pdev_fw_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_PDEV_FW_RING_MPDU_ERR_TAG:
		htt_print_rx_pdev_fw_ring_mpdu_err_tlv_v(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_PDEV_FW_MPDU_DROP_TAG:
		htt_print_rx_pdev_fw_mpdu_drop_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_RX_SOC_FW_STATS_TAG:
		htt_print_rx_soc_fw_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_SOC_FW_REFILL_RING_EMPTY_TAG:
		htt_print_rx_soc_fw_refill_ring_empty_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_RX_SOC_FW_REFILL_RING_NUM_REFILL_TAG:
		htt_print_rx_soc_fw_refill_ring_num_refill_tlv_v(
				tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_REFILL_RXDMA_ERR_TAG:
		htt_print_rx_soc_fw_refill_ring_num_rxdma_err_tlv_v(
				tag_buf, len, stats_req);
		break;

	case HTT_STATS_RX_REFILL_REO_ERR_TAG:
		htt_print_rx_soc_fw_refill_ring_num_reo_err_tlv_v(
				tag_buf, len, stats_req);
		break;

	case HTT_STATS_RX_REO_RESOURCE_STATS_TAG:
		htt_print_rx_reo_debug_stats_tlv_v(
				tag_buf, stats_req);
		break;
	case HTT_STATS_RX_PDEV_FW_STATS_PHY_ERR_TAG:
		htt_print_rx_pdev_fw_stats_phy_err_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_PDEV_RATE_STATS_TAG:
		htt_print_tx_pdev_rate_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_PDEV_RATE_STATS_TAG:
		htt_print_rx_pdev_rate_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_PDEV_SCHEDULER_TXQ_STATS_TAG:
		htt_print_tx_pdev_stats_sched_per_txq_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TX_SCHED_CMN_TAG:
		htt_print_stats_tx_sched_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_PDEV_MPDU_STATS_TAG:
		htt_print_tx_pdev_mu_mimo_mpdu_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SCHED_TXQ_CMD_POSTED_TAG:
		htt_print_sched_txq_cmd_posted_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_RING_IF_CMN_TAG:
		htt_print_ring_if_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SFM_CLIENT_USER_TAG:
		htt_print_sfm_client_user_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_SFM_CLIENT_TAG:
		htt_print_sfm_client_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_TQM_ERROR_STATS_TAG:
		htt_print_tx_tqm_error_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SCHED_TXQ_CMD_REAPED_TAG:
		htt_print_sched_txq_cmd_reaped_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_SRING_CMN_TAG:
		htt_print_sring_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SOUNDING_STATS_TAG:
		htt_print_tx_sounding_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SELFGEN_AC_ERR_STATS_TAG:
		htt_print_tx_selfgen_ac_err_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SELFGEN_CMN_STATS_TAG:
		htt_print_tx_selfgen_cmn_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SELFGEN_AC_STATS_TAG:
		htt_print_tx_selfgen_ac_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SELFGEN_AX_STATS_TAG:
		htt_print_tx_selfgen_ax_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_SELFGEN_AX_ERR_STATS_TAG:
		htt_print_tx_selfgen_ax_err_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_HWQ_MUMIMO_SCH_STATS_TAG:
		htt_print_tx_hwq_mu_mimo_sch_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_HWQ_MUMIMO_MPDU_STATS_TAG:
		htt_print_tx_hwq_mu_mimo_mpdu_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_HWQ_MUMIMO_CMN_STATS_TAG:
		htt_print_tx_hwq_mu_mimo_cmn_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_HW_INTR_MISC_TAG:
		htt_print_hw_stats_intr_misc_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_HW_WD_TIMEOUT_TAG:
		htt_print_hw_stats_wd_timeout_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_HW_PDEV_ERRS_TAG:
		htt_print_hw_stats_pdev_errs_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_COUNTER_NAME_TAG:
		htt_print_counter_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_TID_DETAILS_TAG:
		htt_print_tx_tid_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_TID_DETAILS_V1_TAG:
		htt_print_tx_tid_stats_v1_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_RX_TID_DETAILS_TAG:
		htt_print_rx_tid_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PEER_STATS_CMN_TAG:
		htt_print_peer_stats_cmn_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PEER_DETAILS_TAG:
		htt_print_peer_details_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PEER_MSDU_FLOWQ_TAG:
		htt_print_msdu_flow_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PEER_TX_RATE_STATS_TAG:
		htt_print_tx_peer_rate_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PEER_RX_RATE_STATS_TAG:
		htt_print_rx_peer_rate_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_TX_DE_COMPL_STATS_TAG:
		htt_print_tx_de_compl_stats_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PDEV_CCA_1SEC_HIST_TAG:
	case HTT_STATS_PDEV_CCA_100MSEC_HIST_TAG:
	case HTT_STATS_PDEV_CCA_STAT_CUMULATIVE_TAG:
		htt_print_pdev_cca_stats_hist_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PDEV_CCA_COUNTERS_TAG:
		htt_print_pdev_stats_cca_counters_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_WHAL_TX_TAG:
		htt_print_hw_stats_whal_tx_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PDEV_TWT_SESSIONS_TAG:
		htt_print_pdev_stats_twt_sessions_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_PDEV_TWT_SESSION_TAG:
		htt_print_pdev_stats_twt_session_tlv(tag_buf, stats_req);
		break;

	case HTT_STATS_SCHED_TXQ_SCHED_ORDER_SU_TAG:
		htt_print_sched_txq_sched_order_su_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_SCHED_TXQ_SCHED_INELIGIBILITY_TAG:
		htt_print_sched_txq_sched_ineligibility_tlv_v(tag_buf, len, stats_req);
		break;

	case HTT_STATS_PDEV_OBSS_PD_TAG:
		htt_print_pdev_obss_pd_stats_tlv_v(tag_buf, stats_req);
		break;
	case HTT_STATS_RING_BACKPRESSURE_STATS_TAG:
		htt_print_backpressure_stats_tlv_v(tag_buf, user_data);
		break;
	case HTT_STATS_PDEV_TX_RATE_TXBF_STATS_TAG:
		htt_print_pdev_tx_rate_txbf_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_NDPA_STATS_TAG:
		htt_print_txbf_ofdma_ndpa_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_NDP_STATS_TAG:
		htt_print_txbf_ofdma_ndp_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_BRP_STATS_TAG:
		htt_print_txbf_ofdma_brp_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_STEER_STATS_TAG:
		htt_print_txbf_ofdma_steer_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_PHY_COUNTERS_TAG:
		htt_print_phy_counters_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_PHY_STATS_TAG:
		htt_print_phy_stats_tlv(tag_buf, stats_req);
		break;
	case HTT_STATS_PEER_CTRL_PATH_TXRX_STATS_TAG:
		htt_print_peer_ctrl_path_txrx_stats_tlv(tag_buf, stats_req);
		break;
	default:
		break;
	}

	return 0;
}

void ath11k_debugfs_htt_ext_stats_handler(struct ath11k_base *ab,
					  struct sk_buff *skb)
{
	struct ath11k_htt_extd_stats_msg *msg;
	struct debug_htt_stats_req *stats_req;
	struct ath11k *ar;
	u32 len;
	u64 cookie;
	int ret;
	bool send_completion = false;
	u8 pdev_id;

	msg = (struct ath11k_htt_extd_stats_msg *)skb->data;
	cookie = msg->cookie;

	if (FIELD_GET(HTT_STATS_COOKIE_MSB, cookie) != HTT_STATS_MAGIC_VALUE) {
		ath11k_warn(ab, "received invalid htt ext stats event\n");
		return;
	}

	pdev_id = FIELD_GET(HTT_STATS_COOKIE_LSB, cookie);
	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_pdev_id(ab, pdev_id);
	rcu_read_unlock();
	if (!ar) {
		ath11k_warn(ab, "failed to get ar for pdev_id %d\n", pdev_id);
		return;
	}

	stats_req = ar->debug.htt_stats.stats_req;
	if (!stats_req)
		return;

	spin_lock_bh(&ar->debug.htt_stats.lock);

	stats_req->done = FIELD_GET(HTT_T2H_EXT_STATS_INFO1_DONE, msg->info1);
	if (stats_req->done)
		send_completion = true;

	spin_unlock_bh(&ar->debug.htt_stats.lock);

	len = FIELD_GET(HTT_T2H_EXT_STATS_INFO1_LENGTH, msg->info1);
	ret = ath11k_dp_htt_tlv_iter(ab, msg->data, len,
				     ath11k_dbg_htt_ext_stats_parse,
				     stats_req);
	if (ret)
		ath11k_warn(ab, "Failed to parse tlv %d\n", ret);

	if (send_completion)
		complete(&stats_req->cmpln);
}

static ssize_t ath11k_read_htt_stats_type(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	size_t len;

	len = scnprintf(buf, sizeof(buf), "%u\n", ar->debug.htt_stats.type);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath11k_write_htt_stats_type(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	u8 type;
	int ret;

	ret = kstrtou8_from_user(user_buf, count, 0, &type);
	if (ret)
		return ret;

	if (type >= ATH11K_DBG_HTT_NUM_EXT_STATS)
		return -E2BIG;

	if (type == ATH11K_DBG_HTT_EXT_STATS_RESET)
		return -EPERM;

	ar->debug.htt_stats.type = type;

	ret = count;

	return ret;
}

static const struct file_operations fops_htt_stats_type = {
	.read = ath11k_read_htt_stats_type,
	.write = ath11k_write_htt_stats_type,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath11k_prep_htt_stats_cfg_params(struct ath11k *ar, u8 type,
					    const u8 *mac_addr,
					    struct htt_ext_stats_cfg_params *cfg_params)
{
	if (!cfg_params)
		return -EINVAL;

	switch (type) {
	case ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_HWQ:
	case ATH11K_DBG_HTT_EXT_STATS_TX_MU_HWQ:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ALL_HWQS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_PDEV_TX_SCHED:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ALL_TXQS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_TQM_CMDQ:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ALL_CMDQS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_PEER_INFO:
		cfg_params->cfg0 = HTT_STAT_PEER_INFO_MAC_ADDR;
		cfg_params->cfg0 |= FIELD_PREP(GENMASK(15, 1),
					HTT_PEER_STATS_REQ_MODE_FLUSH_TQM);
		cfg_params->cfg1 = HTT_STAT_DEFAULT_PEER_REQ_TYPE;
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(7, 0), mac_addr[0]);
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(15, 8), mac_addr[1]);
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(23, 16), mac_addr[2]);
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(31, 24), mac_addr[3]);
		cfg_params->cfg3 |= FIELD_PREP(GENMASK(7, 0), mac_addr[4]);
		cfg_params->cfg3 |= FIELD_PREP(GENMASK(15, 8), mac_addr[5]);
		break;
	case ATH11K_DBG_HTT_EXT_STATS_RING_IF_INFO:
	case ATH11K_DBG_HTT_EXT_STATS_SRNG_INFO:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ALL_RINGS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_ACTIVE_PEERS_LIST:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ACTIVE_PEERS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_PDEV_CCA_STATS:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_CCA_CUMULATIVE;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_TX_SOUNDING_INFO:
		cfg_params->cfg0 = HTT_STAT_DEFAULT_CFG0_ACTIVE_VDEVS;
		break;
	case ATH11K_DBG_HTT_EXT_STATS_PEER_CTRL_PATH_TXRX_STATS:
		cfg_params->cfg0 = HTT_STAT_PEER_INFO_MAC_ADDR;
		cfg_params->cfg1 |= FIELD_PREP(GENMASK(7, 0), mac_addr[0]);
		cfg_params->cfg1 |= FIELD_PREP(GENMASK(15, 8), mac_addr[1]);
		cfg_params->cfg1 |= FIELD_PREP(GENMASK(23, 16), mac_addr[2]);
		cfg_params->cfg1 |= FIELD_PREP(GENMASK(31, 24), mac_addr[3]);
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(7, 0), mac_addr[4]);
		cfg_params->cfg2 |= FIELD_PREP(GENMASK(15, 8), mac_addr[5]);
		break;
	default:
		break;
	}

	return 0;
}

int ath11k_debugfs_htt_stats_req(struct ath11k *ar)
{
	struct debug_htt_stats_req *stats_req = ar->debug.htt_stats.stats_req;
	u8 type = stats_req->type;
	u64 cookie = 0;
	int ret, pdev_id = ar->pdev->pdev_id;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };

	init_completion(&stats_req->cmpln);

	stats_req->done = false;
	stats_req->pdev_id = pdev_id;

	cookie = FIELD_PREP(HTT_STATS_COOKIE_MSB, HTT_STATS_MAGIC_VALUE) |
		 FIELD_PREP(HTT_STATS_COOKIE_LSB, pdev_id);

	ret = ath11k_prep_htt_stats_cfg_params(ar, type, stats_req->peer_addr,
					       &cfg_params);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set htt stats cfg params: %d\n", ret);
		return ret;
	}

	ret = ath11k_dp_tx_htt_h2t_ext_stats_req(ar, type, &cfg_params, cookie);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		return ret;
	}

	while (!wait_for_completion_timeout(&stats_req->cmpln, 3 * HZ)) {
		spin_lock_bh(&ar->debug.htt_stats.lock);
		if (!stats_req->done) {
			stats_req->done = true;
			spin_unlock_bh(&ar->debug.htt_stats.lock);
			ath11k_warn(ar->ab, "stats request timed out\n");
			return -ETIMEDOUT;
		}
		spin_unlock_bh(&ar->debug.htt_stats.lock);
	}

	return 0;
}

static int ath11k_open_htt_stats(struct inode *inode, struct file *file)
{
	struct ath11k *ar = inode->i_private;
	struct debug_htt_stats_req *stats_req;
	u8 type = ar->debug.htt_stats.type;
	int ret;

	if (type == ATH11K_DBG_HTT_EXT_STATS_RESET ||
	    type == ATH11K_DBG_HTT_EXT_STATS_PEER_INFO ||
	    type == ATH11K_DBG_HTT_EXT_STATS_PEER_CTRL_PATH_TXRX_STATS)
		return -EPERM;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH11K_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	if (ar->debug.htt_stats.stats_req) {
		ret = -EAGAIN;
		goto err_unlock;
	}

	stats_req = vzalloc(sizeof(*stats_req) + ATH11K_HTT_STATS_BUF_SIZE);
	if (!stats_req) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	ar->debug.htt_stats.stats_req = stats_req;
	stats_req->type = type;

	ret = ath11k_debugfs_htt_stats_req(ar);
	if (ret < 0)
		goto out;

	file->private_data = stats_req;

	mutex_unlock(&ar->conf_mutex);

	return 0;
out:
	vfree(stats_req);
	ar->debug.htt_stats.stats_req = NULL;
err_unlock:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static int ath11k_release_htt_stats(struct inode *inode, struct file *file)
{
	struct ath11k *ar = inode->i_private;

	mutex_lock(&ar->conf_mutex);
	vfree(file->private_data);
	ar->debug.htt_stats.stats_req = NULL;
	mutex_unlock(&ar->conf_mutex);

	return 0;
}

static ssize_t ath11k_read_htt_stats(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct debug_htt_stats_req *stats_req = file->private_data;
	char *buf;
	u32 length = 0;

	buf = stats_req->buf;
	length = min_t(u32, stats_req->buf_len, ATH11K_HTT_STATS_BUF_SIZE);
	return simple_read_from_buffer(user_buf, count, ppos, buf, length);
}

static const struct file_operations fops_dump_htt_stats = {
	.open = ath11k_open_htt_stats,
	.release = ath11k_release_htt_stats,
	.read = ath11k_read_htt_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath11k_read_htt_stats_reset(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	char buf[32];
	size_t len;

	len = scnprintf(buf, sizeof(buf), "%u\n", ar->debug.htt_stats.reset);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath11k_write_htt_stats_reset(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath11k *ar = file->private_data;
	u8 type;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };
	int ret;

	ret = kstrtou8_from_user(user_buf, count, 0, &type);
	if (ret)
		return ret;

	if (type >= ATH11K_DBG_HTT_NUM_EXT_STATS ||
	    type == ATH11K_DBG_HTT_EXT_STATS_RESET)
		return -E2BIG;

	mutex_lock(&ar->conf_mutex);
	cfg_params.cfg0 = HTT_STAT_DEFAULT_RESET_START_OFFSET;
	cfg_params.cfg1 = 1 << (cfg_params.cfg0 + type);
	ret = ath11k_dp_tx_htt_h2t_ext_stats_req(ar,
						 ATH11K_DBG_HTT_EXT_STATS_RESET,
						 &cfg_params,
						 0ULL);
	if (ret) {
		ath11k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		mutex_unlock(&ar->conf_mutex);
		return ret;
	}

	ar->debug.htt_stats.reset = type;
	mutex_unlock(&ar->conf_mutex);

	ret = count;

	return ret;
}

static const struct file_operations fops_htt_stats_reset = {
	.read = ath11k_read_htt_stats_reset,
	.write = ath11k_write_htt_stats_reset,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath11k_debugfs_htt_stats_init(struct ath11k *ar)
{
	spin_lock_init(&ar->debug.htt_stats.lock);
	debugfs_create_file("htt_stats_type", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_type);
	debugfs_create_file("htt_stats", 0400, ar->debug.debugfs_pdev,
			    ar, &fops_dump_htt_stats);
	debugfs_create_file("htt_stats_reset", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_reset);
}
