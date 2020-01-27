// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "debug.h"
#include "debug_htt_stats.h"

#define HTT_DBG_OUT(buf, len, fmt, ...) \
			scnprintf(buf, len, fmt "\n", ##__VA_ARGS__)

#define HTT_MAX_STRING_LEN 256
#define HTT_MAX_PRINT_CHAR_PER_ELEM 15

#define HTT_TLV_HDR_LEN 4

#define ARRAY_TO_STRING(out, arr, len)							\
	do {										\
		int index = 0; u8 i;							\
		for (i = 0; i < len; i++) {						\
			index += snprintf(out + index, HTT_MAX_STRING_LEN - index,	\
					  " %u:%u,", i, arr[i]);			\
			if (index < 0 || index >= HTT_MAX_STRING_LEN)			\
				break;							\
		}									\
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
	u16 index = 0;
	char data[HTT_MAX_STRING_LEN] = {0};

	tag_len = tag_len >> 2;

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_STATS_STRING_TLV:");

	for (i = 0; i < tag_len; i++) {
		index += snprintf(&data[index],
				HTT_MAX_STRING_LEN - index,
				"%.*s", 4, (char *)&(htt_stats_buf->data[i]));
		if (index >= HTT_MAX_STRING_LEN)
			break;
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "data = %s\n", data);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_queued = %u",
			   htt_stats_buf->hw_queued);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_reaped = %u",
			   htt_stats_buf->hw_reaped);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "underrun = %u",
			   htt_stats_buf->underrun);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_paused = %u",
			   htt_stats_buf->hw_paused);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_flush = %u",
			   htt_stats_buf->hw_flush);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_filt = %u",
			   htt_stats_buf->hw_filt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_abort = %u",
			   htt_stats_buf->tx_abort);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_requeued = %u",
			   htt_stats_buf->mpdu_requed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_xretry = %u",
			   htt_stats_buf->tx_xretry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "data_rc = %u",
			   htt_stats_buf->data_rc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_dropped_xretry = %u",
			   htt_stats_buf->mpdu_dropped_xretry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "illegal_rate_phy_err = %u",
			   htt_stats_buf->illgl_rate_phy_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cont_xretry = %u",
			   htt_stats_buf->cont_xretry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_timeout = %u",
			   htt_stats_buf->tx_timeout);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pdev_resets = %u",
			   htt_stats_buf->pdev_resets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "phy_underrun = %u",
			   htt_stats_buf->phy_underrun);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "txop_ovf = %u",
			   htt_stats_buf->txop_ovf);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_posted = %u",
			   htt_stats_buf->seq_posted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_failed_queueing = %u",
			   htt_stats_buf->seq_failed_queueing);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_completed = %u",
			   htt_stats_buf->seq_completed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_restarted = %u",
			   htt_stats_buf->seq_restarted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_seq_posted = %u",
			   htt_stats_buf->mu_seq_posted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_switch_hw_paused = %u",
			   htt_stats_buf->seq_switch_hw_paused);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "next_seq_posted_dsr = %u",
			   htt_stats_buf->next_seq_posted_dsr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_posted_isr = %u",
			   htt_stats_buf->seq_posted_isr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "seq_ctrl_cached = %u",
			   htt_stats_buf->seq_ctrl_cached);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_count_tqm = %u",
			   htt_stats_buf->mpdu_count_tqm);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_count_tqm = %u",
			   htt_stats_buf->msdu_count_tqm);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_removed_tqm = %u",
			   htt_stats_buf->mpdu_removed_tqm);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_removed_tqm = %u",
			   htt_stats_buf->msdu_removed_tqm);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_sw_flush = %u",
			   htt_stats_buf->mpdus_sw_flush);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_hw_filter = %u",
			   htt_stats_buf->mpdus_hw_filter);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_truncated = %u",
			   htt_stats_buf->mpdus_truncated);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_ack_failed = %u",
			   htt_stats_buf->mpdus_ack_failed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_expired = %u",
			   htt_stats_buf->mpdus_expired);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_seq_hw_retry = %u",
			   htt_stats_buf->mpdus_seq_hw_retry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ack_tlv_proc = %u",
			   htt_stats_buf->ack_tlv_proc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "coex_abort_mpdu_cnt_valid = %u",
			   htt_stats_buf->coex_abort_mpdu_cnt_valid);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "coex_abort_mpdu_cnt = %u",
			   htt_stats_buf->coex_abort_mpdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_total_ppdus_tried_ota = %u",
			   htt_stats_buf->num_total_ppdus_tried_ota);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_tried_ota = %u",
			   htt_stats_buf->num_data_ppdus_tried_ota);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_ctrl_mgmt_enqued = %u",
			   htt_stats_buf->local_ctrl_mgmt_enqued);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_ctrl_mgmt_freed = %u",
			   htt_stats_buf->local_ctrl_mgmt_freed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_data_enqued = %u",
			   htt_stats_buf->local_data_enqued);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_data_freed = %u",
			   htt_stats_buf->local_data_freed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_tried = %u",
			   htt_stats_buf->mpdu_tried);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "isr_wait_seq_posted = %u",
			   htt_stats_buf->isr_wait_seq_posted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_active_dur_us_low = %u",
			   htt_stats_buf->tx_active_dur_us_low);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_active_dur_us_high = %u\n",
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
	char urrn_stats[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_URRN_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_URRN_TLV_V:");

	ARRAY_TO_STRING(urrn_stats, htt_stats_buf->urrn_stats, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "urrn_stats = %s\n", urrn_stats);

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
	char flush_errs[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_FLUSH_REASON_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_FLUSH_TLV_V:");

	ARRAY_TO_STRING(flush_errs, htt_stats_buf->flush_errs, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "flush_errs = %s\n", flush_errs);

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
	char sifs_status[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_SIFS_BURST_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_SIFS_TLV_V:");

	ARRAY_TO_STRING(sifs_status, htt_stats_buf->sifs_status, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sifs_status = %s\n",
			   sifs_status);

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
	char phy_errs[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_PHY_ERR_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_PHY_ERR_TLV_V:");

	ARRAY_TO_STRING(phy_errs, htt_stats_buf->phy_errs, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "phy_errs = %s\n", phy_errs);

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
	char sifs_hist_status[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_MAX_SIFS_BURST_HIST_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_PDEV_STATS_SIFS_HIST_TLV_V:");

	ARRAY_TO_STRING(sifs_hist_status, htt_stats_buf->sifs_hist_status, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sifs_hist_status = %s\n",
			   sifs_hist_status);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_PDEV_STATS_TX_PPDU_STATS_TLV_V:");

	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_legacy_su = %u",
			   htt_stats_buf->num_data_ppdus_legacy_su);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_ac_su = %u",
			   htt_stats_buf->num_data_ppdus_ac_su);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_ax_su = %u",
			   htt_stats_buf->num_data_ppdus_ax_su);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_ac_su_txbf = %u",
			   htt_stats_buf->num_data_ppdus_ac_su_txbf);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_data_ppdus_ax_su_txbf = %u\n",
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
	char tried_mpdu_cnt_hist[HTT_MAX_STRING_LEN] = {0};
	u32  num_elements = ((tag_len - sizeof(htt_stats_buf->hist_bin_size)) >> 2);
	u32  required_buffer_size = HTT_MAX_PRINT_CHAR_PER_ELEM * num_elements;

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_PDEV_STATS_TRIED_MPDU_CNT_HIST_TLV_V:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "TRIED_MPDU_CNT_HIST_BIN_SIZE : %u",
			   htt_stats_buf->hist_bin_size);

	if (required_buffer_size < HTT_MAX_STRING_LEN) {
		ARRAY_TO_STRING(tried_mpdu_cnt_hist,
				htt_stats_buf->tried_mpdu_cnt_hist,
				num_elements);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "tried_mpdu_cnt_hist = %s\n",
				   tried_mpdu_cnt_hist);
	} else {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "INSUFFICIENT PRINT BUFFER\n");
	}

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_HW_STATS_INTR_MISC_TLV:");
	memcpy(hw_intr_name, &(htt_stats_buf->hw_intr_name[0]),
	       HTT_STATS_MAX_HW_INTR_NAME_LEN);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_intr_name = %s ", hw_intr_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mask = %u",
			   htt_stats_buf->mask);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "count = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_HW_STATS_WD_TIMEOUT_TLV:");
	memcpy(hw_module_name, &(htt_stats_buf->hw_module_name[0]),
	       HTT_STATS_MAX_HW_MODULE_NAME_LEN);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_module_name = %s ",
			   hw_module_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "count = %u",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_HW_STATS_PDEV_ERRS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_abort = %u",
			   htt_stats_buf->tx_abort);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_abort_fail_count = %u",
			   htt_stats_buf->tx_abort_fail_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_abort = %u",
			   htt_stats_buf->rx_abort);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_abort_fail_count = %u",
			   htt_stats_buf->rx_abort_fail_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "warm_reset = %u",
			   htt_stats_buf->warm_reset);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cold_reset = %u",
			   htt_stats_buf->cold_reset);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_flush = %u",
			   htt_stats_buf->tx_flush);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_glb_reset = %u",
			   htt_stats_buf->tx_glb_reset);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_txq_reset = %u",
			   htt_stats_buf->tx_txq_reset);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_timeout_reset = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_MSDU_FLOW_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_update_timestamp = %u",
			   htt_stats_buf->last_update_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_add_timestamp = %u",
			   htt_stats_buf->last_add_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_remove_timestamp = %u",
			   htt_stats_buf->last_remove_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "total_processed_msdu_count = %u",
			   htt_stats_buf->total_processed_msdu_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cur_msdu_count_in_flowq = %u",
			   htt_stats_buf->cur_msdu_count_in_flowq);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sw_peer_id = %u",
			   htt_stats_buf->sw_peer_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_flow_no = %u",
			   htt_stats_buf->tx_flow_no__tid_num__drop_rule & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_num = %u",
			   (htt_stats_buf->tx_flow_no__tid_num__drop_rule & 0xF0000) >>
			   16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "drop_rule = %u",
			   (htt_stats_buf->tx_flow_no__tid_num__drop_rule & 0x100000) >>
			   20);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_cycle_enqueue_count = %u",
			   htt_stats_buf->last_cycle_enqueue_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_cycle_dequeue_count = %u",
			   htt_stats_buf->last_cycle_dequeue_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_cycle_drop_count = %u",
			   htt_stats_buf->last_cycle_drop_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "current_drop_th = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TID_STATS_TLV:");
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_name = %s ", tid_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sw_peer_id = %u",
			   htt_stats_buf->sw_peer_id__tid_num & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_num = %u",
			   (htt_stats_buf->sw_peer_id__tid_num & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_sched_pending = %u",
			   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_ppdu_in_hwq = %u",
			   (htt_stats_buf->num_sched_pending__num_ppdu_in_hwq &
			   0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_flags = 0x%x",
			   htt_stats_buf->tid_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_queued = %u",
			   htt_stats_buf->hw_queued);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hw_reaped = %u",
			   htt_stats_buf->hw_reaped);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdus_hw_filter = %u",
			   htt_stats_buf->mpdus_hw_filter);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_bytes = %u",
			   htt_stats_buf->qdepth_bytes);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_num_msdu = %u",
			   htt_stats_buf->qdepth_num_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_num_mpdu = %u",
			   htt_stats_buf->qdepth_num_mpdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_scheduled_tsmp = %u",
			   htt_stats_buf->last_scheduled_tsmp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pause_module_id = %u",
			   htt_stats_buf->pause_module_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "block_module_id = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TID_STATS_V1_TLV:");
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_name = %s ", tid_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sw_peer_id = %u",
			   htt_stats_buf->sw_peer_id__tid_num & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_num = %u",
			   (htt_stats_buf->sw_peer_id__tid_num & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_sched_pending = %u",
			   htt_stats_buf->num_sched_pending__num_ppdu_in_hwq & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_ppdu_in_hwq = %u",
			   (htt_stats_buf->num_sched_pending__num_ppdu_in_hwq &
			   0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_flags = 0x%x",
			   htt_stats_buf->tid_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "max_qdepth_bytes = %u",
			   htt_stats_buf->max_qdepth_bytes);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "max_qdepth_n_msdus = %u",
			   htt_stats_buf->max_qdepth_n_msdus);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rsvd = %u",
			   htt_stats_buf->rsvd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_bytes = %u",
			   htt_stats_buf->qdepth_bytes);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_num_msdu = %u",
			   htt_stats_buf->qdepth_num_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qdepth_num_mpdu = %u",
			   htt_stats_buf->qdepth_num_mpdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_scheduled_tsmp = %u",
			   htt_stats_buf->last_scheduled_tsmp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pause_module_id = %u",
			   htt_stats_buf->pause_module_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "block_module_id = %u",
			   htt_stats_buf->block_module_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "allow_n_flags = 0x%x",
			   htt_stats_buf->allow_n_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sendn_frms_allowed = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_TID_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sw_peer_id = %u",
			   htt_stats_buf->sw_peer_id__tid_num & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_num = %u",
			   (htt_stats_buf->sw_peer_id__tid_num & 0xFFFF0000) >> 16);
	memcpy(tid_name, &(htt_stats_buf->tid_name[0]), MAX_HTT_TID_NAME);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tid_name = %s ", tid_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dup_in_reorder = %u",
			   htt_stats_buf->dup_in_reorder);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dup_past_outside_window = %u",
			   htt_stats_buf->dup_past_outside_window);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dup_past_within_window = %u",
			   htt_stats_buf->dup_past_within_window);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rxdesc_err_decrypt = %u\n",
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
	char counter_name[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_COUNTER_TLV:");

	ARRAY_TO_STRING(counter_name,
			htt_stats_buf->counter_name,
			HTT_MAX_COUNTER_NAME);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "counter_name = %s ", counter_name);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "count = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_PEER_STATS_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ppdu_cnt = %u",
			   htt_stats_buf->ppdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_cnt = %u",
			   htt_stats_buf->mpdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_cnt = %u",
			   htt_stats_buf->msdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pause_bitmap = %u",
			   htt_stats_buf->pause_bitmap);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "block_bitmap = %u",
			   htt_stats_buf->block_bitmap);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_rssi = %d",
			   htt_stats_buf->rssi);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "enqueued_count = %llu",
			   htt_stats_buf->peer_enqueued_count_low |
			   ((u64)htt_stats_buf->peer_enqueued_count_high << 32));
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dequeued_count = %llu",
			   htt_stats_buf->peer_dequeued_count_low |
			   ((u64)htt_stats_buf->peer_dequeued_count_high << 32));
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dropped_count = %llu",
			   htt_stats_buf->peer_dropped_count_low |
			   ((u64)htt_stats_buf->peer_dropped_count_high << 32));
	len += HTT_DBG_OUT(buf + len, buf_len - len, "transmitted_ppdu_bytes = %llu",
			   htt_stats_buf->ppdu_transmitted_bytes_low |
			   ((u64)htt_stats_buf->ppdu_transmitted_bytes_high << 32));
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ttl_removed_count = %u",
			   htt_stats_buf->peer_ttl_removed_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "inactive_time = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_PEER_DETAILS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "peer_type = %u",
			   htt_stats_buf->peer_type);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sw_peer_id = %u",
			   htt_stats_buf->sw_peer_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "vdev_id = %u",
			   htt_stats_buf->vdev_pdev_ast_idx & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pdev_id = %u",
			   (htt_stats_buf->vdev_pdev_ast_idx & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ast_idx = %u",
			   (htt_stats_buf->vdev_pdev_ast_idx & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "mac_addr = %02x:%02x:%02x:%02x:%02x:%02x",
			   htt_stats_buf->mac_addr.mac_addr_l32 & 0xFF,
			   (htt_stats_buf->mac_addr.mac_addr_l32 & 0xFF00) >> 8,
			   (htt_stats_buf->mac_addr.mac_addr_l32 & 0xFF0000) >> 16,
			   (htt_stats_buf->mac_addr.mac_addr_l32 & 0xFF000000) >> 24,
			   (htt_stats_buf->mac_addr.mac_addr_h16 & 0xFF),
			   (htt_stats_buf->mac_addr.mac_addr_h16 & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "peer_flags = 0x%x",
			   htt_stats_buf->peer_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qpeer_flags = 0x%x\n",
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
	char str_buf[HTT_MAX_STRING_LEN] = {0};
	char *tx_gi[HTT_TX_PEER_STATS_NUM_GI_COUNTERS] = {NULL};
	u8 j;

	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		tx_gi[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!tx_gi[j])
			goto fail;
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PEER_RATE_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_ldpc = %u",
			   htt_stats_buf->tx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts_cnt = %u",
			   htt_stats_buf->rts_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ack_rssi = %u",
			   htt_stats_buf->ack_rssi);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_su_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_su_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_mu_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_mu_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf,
			htt_stats_buf->tx_nss,
			HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf,
			htt_stats_buf->tx_bw,
			HTT_TX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_bw = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_stbc,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_stbc = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_pream,
			HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_pream = %s ", str_buf);

	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(tx_gi[j],
				htt_stats_buf->tx_gi[j],
				HTT_TX_PEER_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_gi[%u] = %s ",
				j, tx_gi[j]);
	}

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf,
			htt_stats_buf->tx_dcm,
			HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_dcm = %s\n", str_buf);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;

fail:
	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++)
		kfree(tx_gi[j]);
}

static inline void htt_print_rx_peer_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_peer_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 j;
	char *rssi_chain[HTT_RX_PEER_STATS_NUM_SPATIAL_STREAMS] = {NULL};
	char *rx_gi[HTT_RX_PEER_STATS_NUM_GI_COUNTERS] = {NULL};
	char str_buf[HTT_MAX_STRING_LEN] = {0};

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_SPATIAL_STREAMS; j++) {
		rssi_chain[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!rssi_chain[j])
			goto fail;
	}

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		rx_gi[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!rx_gi[j])
			goto fail;
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_PEER_RATE_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "nsts = %u",
			   htt_stats_buf->nsts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ldpc = %u",
			   htt_stats_buf->rx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts_cnt = %u",
			   htt_stats_buf->rts_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_mgmt = %u",
			   htt_stats_buf->rssi_mgmt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_data = %u",
			   htt_stats_buf->rssi_data);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_comb = %u",
			   htt_stats_buf->rssi_comb);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_mcs,
			HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_nss,
			HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_dcm,
			HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_dcm = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_stbc,
			HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_stbc = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_bw,
			HTT_RX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_bw = %s ", str_buf);

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_SPATIAL_STREAMS; j++) {
		ARRAY_TO_STRING(rssi_chain[j], htt_stats_buf->rssi_chain[j],
				HTT_RX_PEER_STATS_NUM_BW_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_chain[%u] = %s ",
				   j, rssi_chain[j]);
	}

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(rx_gi[j], htt_stats_buf->rx_gi[j],
				HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_gi[%u] = %s ",
				j, rx_gi[j]);
	}

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_pream,
			HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_pream = %s\n", str_buf);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;

fail:
	for (j = 0; j < HTT_RX_PEER_STATS_NUM_SPATIAL_STREAMS; j++)
		kfree(rssi_chain[j]);

	for (j = 0; j < HTT_RX_PEER_STATS_NUM_GI_COUNTERS; j++)
		kfree(rx_gi[j]);
}

static inline void
htt_print_tx_hwq_mu_mimo_sch_stats_tlv(const void *tag_buf,
				       struct debug_htt_stats_req *stats_req)
{
	const struct htt_tx_hwq_mu_mimo_sch_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_HWQ_MU_MIMO_SCH_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_sch_posted = %u",
			   htt_stats_buf->mu_mimo_sch_posted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_sch_failed = %u",
			   htt_stats_buf->mu_mimo_sch_failed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_ppdu_posted = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_MU_MIMO_MPDU_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_mpdus_queued_usr = %u",
			   htt_stats_buf->mu_mimo_mpdus_queued_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_mpdus_tried_usr = %u",
			   htt_stats_buf->mu_mimo_mpdus_tried_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_mpdus_failed_usr = %u",
			   htt_stats_buf->mu_mimo_mpdus_failed_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_mpdus_requeued_usr = %u",
			   htt_stats_buf->mu_mimo_mpdus_requeued_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_err_no_ba_usr = %u",
			   htt_stats_buf->mu_mimo_err_no_ba_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_mpdu_underrun_usr = %u",
			   htt_stats_buf->mu_mimo_mpdu_underrun_usr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_ampdu_underrun_usr = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_HWQ_MU_MIMO_CMN_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__hwq_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwq_id = %u\n",
			   (htt_stats_buf->mac_id__hwq_id__word & 0xFF00) >> 8);

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
	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_HWQ_STATS_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__hwq_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwq_id = %u",
			   (htt_stats_buf->mac_id__hwq_id__word & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "xretry = %u",
			   htt_stats_buf->xretry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "underrun_cnt = %u",
			   htt_stats_buf->underrun_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "flush_cnt = %u",
			   htt_stats_buf->flush_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "filt_cnt = %u",
			   htt_stats_buf->filt_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "null_mpdu_bmap = %u",
			   htt_stats_buf->null_mpdu_bmap);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "user_ack_failure = %u",
			   htt_stats_buf->user_ack_failure);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ack_tlv_proc = %u",
			   htt_stats_buf->ack_tlv_proc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_id_proc = %u",
			   htt_stats_buf->sched_id_proc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "null_mpdu_tx_count = %u",
			   htt_stats_buf->null_mpdu_tx_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_bmap_not_recvd = %u",
			   htt_stats_buf->mpdu_bmap_not_recvd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_bar = %u",
			   htt_stats_buf->num_bar);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts = %u",
			   htt_stats_buf->rts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cts2self = %u",
			   htt_stats_buf->cts2self);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qos_null = %u",
			   htt_stats_buf->qos_null);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_tried_cnt = %u",
			   htt_stats_buf->mpdu_tried_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_queued_cnt = %u",
			   htt_stats_buf->mpdu_queued_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_ack_fail_cnt = %u",
			   htt_stats_buf->mpdu_ack_fail_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_filt_cnt = %u",
			   htt_stats_buf->mpdu_filt_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "false_mpdu_ack_count = %u",
			   htt_stats_buf->false_mpdu_ack_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "txq_timeout = %u\n",
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
	char difs_latency_hist[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_DIFS_LATENCY_STATS_TLV_V:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hist_intvl = %u",
			htt_stats_buf->hist_intvl);

	ARRAY_TO_STRING(difs_latency_hist, htt_stats_buf->difs_latency_hist,
			data_len);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "difs_latency_hist = %s\n",
			difs_latency_hist);

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
	char cmd_result[HTT_MAX_STRING_LEN] = {0};

	data_len = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_CMD_RESULT_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_CMD_RESULT_STATS_TLV_V:");

	ARRAY_TO_STRING(cmd_result, htt_stats_buf->cmd_result, data_len);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "cmd_result = %s\n", cmd_result);

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
	char cmd_stall_status[HTT_MAX_STRING_LEN] = {0};

	num_elems = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_CMD_STALL_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_HWQ_CMD_STALL_STATS_TLV_V:");

	ARRAY_TO_STRING(cmd_stall_status, htt_stats_buf->cmd_stall_status, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cmd_stall_status = %s\n",
			   cmd_stall_status);

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
	char fes_result[HTT_MAX_STRING_LEN] = {0};

	num_elems = min_t(u16, (tag_len >> 2), HTT_TX_HWQ_MAX_FES_RESULT_STATS);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_FES_RESULT_STATS_TLV_V:");

	ARRAY_TO_STRING(fes_result, htt_stats_buf->fes_result, num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fes_result = %s\n", fes_result);

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
	char tried_mpdu_cnt_hist[HTT_MAX_STRING_LEN] = {0};
	u32  num_elements = ((tag_len -
			    sizeof(htt_stats_buf->hist_bin_size)) >> 2);
	u32  required_buffer_size = HTT_MAX_PRINT_CHAR_PER_ELEM * num_elements;

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_TRIED_MPDU_CNT_HIST_TLV_V:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "TRIED_MPDU_CNT_HIST_BIN_SIZE : %u",
			   htt_stats_buf->hist_bin_size);

	if (required_buffer_size < HTT_MAX_STRING_LEN) {
		ARRAY_TO_STRING(tried_mpdu_cnt_hist,
				htt_stats_buf->tried_mpdu_cnt_hist,
				num_elements);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "tried_mpdu_cnt_hist = %s\n",
				   tried_mpdu_cnt_hist);
	} else {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "INSUFFICIENT PRINT BUFFER ");
	}

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
	char txop_used_cnt_hist[HTT_MAX_STRING_LEN] = {0};
	u32 num_elements = tag_len >> 2;
	u32  required_buffer_size = HTT_MAX_PRINT_CHAR_PER_ELEM * num_elements;

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_HWQ_TXOP_USED_CNT_HIST_TLV_V:");

	if (required_buffer_size < HTT_MAX_STRING_LEN) {
		ARRAY_TO_STRING(txop_used_cnt_hist,
				htt_stats_buf->txop_used_cnt_hist,
				num_elements);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "txop_used_cnt_hist = %s\n",
				   txop_used_cnt_hist);
	} else {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "INSUFFICIENT PRINT BUFFER ");
	}
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
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "\nHTT_TX_AC_SOUNDING_STATS_TLV:\n");
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_cbf_20 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u ",
				   cbf_20[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_cbf_40 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_40[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_cbf_80 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_80[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_cbf_160 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_160[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);

		for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS; i++) {
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "Sounding User %u = 20MHz: %u, 40MHz : %u, 80MHz: %u, 160MHz: %u ",
					   i,
					   htt_stats_buf->sounding[0],
					   htt_stats_buf->sounding[1],
					   htt_stats_buf->sounding[2],
					   htt_stats_buf->sounding[3]);
		}
	} else if (htt_stats_buf->tx_sounding_mode == HTT_TX_AX_SOUNDING_MODE) {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "\nHTT_TX_AX_SOUNDING_STATS_TLV:\n");
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_cbf_20 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u ",
				   cbf_20[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_20[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_cbf_40 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_40[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_40[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_cbf_80 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_80[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_80[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_cbf_160 = IBF : %u, SU_SIFS : %u, SU_RBO : %u, MU_SIFS : %u, MU_RBO : %u",
				   cbf_160[HTT_IMPLICIT_TXBF_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_SU_SIFS_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_SU_RBO_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_MU_SIFS_STEER_STATS],
				   cbf_160[HTT_EXPLICIT_TXBF_MU_RBO_STEER_STATS]);

		for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS; i++) {
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "Sounding User %u = 20MHz: %u, 40MHz : %u, 80MHz: %u, 160MHz: %u ",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_SELFGEN_CMN_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "su_bar = %u",
			   htt_stats_buf->su_bar);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts = %u",
			   htt_stats_buf->rts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cts2self = %u",
			   htt_stats_buf->cts2self);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qos_null = %u",
			   htt_stats_buf->qos_null);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_1 = %u",
			   htt_stats_buf->delayed_bar_1);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_2 = %u",
			   htt_stats_buf->delayed_bar_2);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_3 = %u",
			   htt_stats_buf->delayed_bar_3);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_4 = %u",
			   htt_stats_buf->delayed_bar_4);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_5 = %u",
			   htt_stats_buf->delayed_bar_5);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_6 = %u",
			   htt_stats_buf->delayed_bar_6);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "delayed_bar_7 = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_su_ndpa = %u",
			   htt_stats_buf->ac_su_ndpa);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_su_ndp = %u",
			   htt_stats_buf->ac_su_ndp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_ndpa = %u",
			   htt_stats_buf->ac_mu_mimo_ndpa);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_ndp = %u",
			   htt_stats_buf->ac_mu_mimo_ndp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brpoll_1 = %u",
			   htt_stats_buf->ac_mu_mimo_brpoll_1);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brpoll_2 = %u",
			   htt_stats_buf->ac_mu_mimo_brpoll_2);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brpoll_3 = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_su_ndpa = %u",
			   htt_stats_buf->ax_su_ndpa);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_su_ndp = %u",
			   htt_stats_buf->ax_su_ndp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_ndpa = %u",
			   htt_stats_buf->ax_mu_mimo_ndpa);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_ndp = %u",
			   htt_stats_buf->ax_mu_mimo_ndp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_1 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_1);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_2 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_2);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_3 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_3);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_4 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_4);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_5 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_5);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_6 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_6);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brpoll_7 = %u",
			   htt_stats_buf->ax_mu_mimo_brpoll_7);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_basic_trigger = %u",
			   htt_stats_buf->ax_basic_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_bsr_trigger = %u",
			   htt_stats_buf->ax_bsr_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_bar_trigger = %u",
			   htt_stats_buf->ax_mu_bar_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_rts_trigger = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_ERR_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_su_ndp_err = %u",
			   htt_stats_buf->ac_su_ndp_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_su_ndpa_err = %u",
			   htt_stats_buf->ac_su_ndpa_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_ndpa_err = %u",
			   htt_stats_buf->ac_mu_mimo_ndpa_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_ndp_err = %u",
			   htt_stats_buf->ac_mu_mimo_ndp_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brp1_err = %u",
			   htt_stats_buf->ac_mu_mimo_brp1_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brp2_err = %u",
			   htt_stats_buf->ac_mu_mimo_brp2_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_brp3_err = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_ERR_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_su_ndp_err = %u",
			   htt_stats_buf->ax_su_ndp_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_su_ndpa_err = %u",
			   htt_stats_buf->ax_su_ndpa_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_ndpa_err = %u",
			   htt_stats_buf->ax_mu_mimo_ndpa_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_ndp_err = %u",
			   htt_stats_buf->ax_mu_mimo_ndp_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp1_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp1_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp2_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp2_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp3_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp3_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp4_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp4_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp5_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp5_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp6_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp6_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_brp7_err = %u",
			   htt_stats_buf->ax_mu_mimo_brp7_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_basic_trigger_err = %u",
			   htt_stats_buf->ax_basic_trigger_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_bsr_trigger_err = %u",
			   htt_stats_buf->ax_bsr_trigger_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_bar_trigger_err = %u",
			   htt_stats_buf->ax_mu_bar_trigger_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_rts_trigger_err = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_PDEV_MU_MIMO_SCH_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_sch_posted = %u",
			   htt_stats_buf->mu_mimo_sch_posted);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_sch_failed = %u",
			   htt_stats_buf->mu_mimo_sch_failed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mu_mimo_ppdu_posted = %u\n",
			   htt_stats_buf->mu_mimo_ppdu_posted);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "11ac MU_MIMO SCH STATS:");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS; i++)
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_mu_mimo_sch_nusers_%u = %u",
				   i, htt_stats_buf->ac_mu_mimo_sch_nusers[i]);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "\n11ax MU_MIMO SCH STATS:");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS; i++)
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_mu_mimo_sch_nusers_%u = %u",
				   i, htt_stats_buf->ax_mu_mimo_sch_nusers[i]);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "\n11ax OFDMA SCH STATS:");

	for (i = 0; i < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS; i++)
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_ofdma_sch_nusers_%u = %u",
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
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "HTT_TX_PDEV_MU_MIMO_AC_MPDU_STATS:\n");

		if (htt_stats_buf->user_index <
		    HTT_TX_PDEV_STATS_NUM_AC_MUMIMO_USER_STATS) {
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_mpdus_queued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_queued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_mpdus_tried_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_tried_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_mpdus_failed_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_failed_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_mpdus_requeued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_requeued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_err_no_ba_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->err_no_ba_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_mpdu_underrun_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdu_underrun_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ac_mu_mimo_ampdu_underrun_usr_%u = %u\n",
					   htt_stats_buf->user_index,
					   htt_stats_buf->ampdu_underrun_usr);
		}
	}

	if (htt_stats_buf->tx_sched_mode == HTT_STATS_TX_SCHED_MODE_MU_MIMO_AX) {
		if (!htt_stats_buf->user_index)
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "HTT_TX_PDEV_MU_MIMO_AX_MPDU_STATS:\n");

		if (htt_stats_buf->user_index <
		    HTT_TX_PDEV_STATS_NUM_AX_MUMIMO_USER_STATS) {
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_mpdus_queued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_queued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_mpdus_tried_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_tried_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_mpdus_failed_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_failed_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_mpdus_requeued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_requeued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_err_no_ba_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->err_no_ba_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_mpdu_underrun_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdu_underrun_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_mimo_ampdu_underrun_usr_%u = %u\n",
					   htt_stats_buf->user_index,
					   htt_stats_buf->ampdu_underrun_usr);
		}
	}

	if (htt_stats_buf->tx_sched_mode == HTT_STATS_TX_SCHED_MODE_MU_OFDMA_AX) {
		if (!htt_stats_buf->user_index)
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "HTT_TX_PDEV_AX_MU_OFDMA_MPDU_STATS:\n");

		if (htt_stats_buf->user_index < HTT_TX_PDEV_STATS_NUM_OFDMA_USER_STATS) {
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_mpdus_queued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_queued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_mpdus_tried_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_tried_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_mpdus_failed_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_failed_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_mpdus_requeued_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdus_requeued_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_err_no_ba_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->err_no_ba_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_mpdu_underrun_usr_%u = %u",
					   htt_stats_buf->user_index,
					   htt_stats_buf->mpdu_underrun_usr);
			len += HTT_DBG_OUT(buf + len, buf_len - len,
					   "ax_mu_ofdma_ampdu_underrun_usr_%u = %u\n",
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
	char sched_cmd_posted[HTT_MAX_STRING_LEN] = {0};
	u16 num_elements = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_SCHED_TX_MODE_MAX);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_POSTED_TLV_V:");

	ARRAY_TO_STRING(sched_cmd_posted, htt_stats_buf->sched_cmd_posted,
			num_elements);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_cmd_posted = %s\n",
			   sched_cmd_posted);

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
	char sched_cmd_reaped[HTT_MAX_STRING_LEN] = {0};
	u16 num_elements = min_t(u16, (tag_len >> 2), HTT_TX_PDEV_SCHED_TX_MODE_MAX);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_REAPED_TLV_V:");

	ARRAY_TO_STRING(sched_cmd_reaped, htt_stats_buf->sched_cmd_reaped,
			num_elements);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_cmd_reaped = %s\n",
			   sched_cmd_reaped);

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
	char sched_order_su[HTT_MAX_STRING_LEN] = {0};
	/* each entry is u32, i.e. 4 bytes */
	u32 sched_order_su_num_entries =
		min_t(u32, (tag_len >> 2), HTT_TX_PDEV_NUM_SCHED_ORDER_LOG);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_SCHED_TXQ_SCHED_ORDER_SU_TLV_V:");

	ARRAY_TO_STRING(sched_order_su, htt_stats_buf->sched_order_su,
			sched_order_su_num_entries);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_order_su = %s\n",
			   sched_order_su);

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
	char sched_ineligibility[HTT_MAX_STRING_LEN] = {0};
	/* each entry is u32, i.e. 4 bytes */
	u32 sched_ineligibility_num_entries = tag_len >> 2;

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_SCHED_TXQ_SCHED_INELIGIBILITY_V:");

	ARRAY_TO_STRING(sched_ineligibility, htt_stats_buf->sched_ineligibility,
			sched_ineligibility_num_entries);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_ineligibility = %s\n",
			   sched_ineligibility);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_PDEV_STATS_SCHED_PER_TXQ_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__txq_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "txq_id = %u",
			   (htt_stats_buf->mac_id__txq_id__word & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_policy = %u",
			   htt_stats_buf->sched_policy);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "last_sched_cmd_posted_timestamp = %u",
			   htt_stats_buf->last_sched_cmd_posted_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "last_sched_cmd_compl_timestamp = %u",
			   htt_stats_buf->last_sched_cmd_compl_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_2_tac_lwm_count = %u",
			   htt_stats_buf->sched_2_tac_lwm_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_2_tac_ring_full = %u",
			   htt_stats_buf->sched_2_tac_ring_full);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_cmd_post_failure = %u",
			   htt_stats_buf->sched_cmd_post_failure);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_active_tids = %u",
			   htt_stats_buf->num_active_tids);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_ps_schedules = %u",
			   htt_stats_buf->num_ps_schedules);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_cmds_pending = %u",
			   htt_stats_buf->sched_cmds_pending);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_tid_register = %u",
			   htt_stats_buf->num_tid_register);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_tid_unregister = %u",
			   htt_stats_buf->num_tid_unregister);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_qstats_queried = %u",
			   htt_stats_buf->num_qstats_queried);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qstats_update_pending = %u",
			   htt_stats_buf->qstats_update_pending);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_qstats_query_timestamp = %u",
			   htt_stats_buf->last_qstats_query_timestamp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_tqm_cmdq_full = %u",
			   htt_stats_buf->num_tqm_cmdq_full);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_de_sched_algo_trigger = %u",
			   htt_stats_buf->num_de_sched_algo_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_rt_sched_algo_trigger = %u",
			   htt_stats_buf->num_rt_sched_algo_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_tqm_sched_algo_trigger = %u",
			   htt_stats_buf->num_tqm_sched_algo_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "notify_sched = %u\n",
			   htt_stats_buf->notify_sched);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dur_based_sendn_term = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_STATS_TX_SCHED_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "current_timestamp = %u\n",
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
	char gen_mpdu_end_reason[HTT_MAX_STRING_LEN] = {0};
	u16 num_elements = min_t(u16, (tag_len >> 2),
				 HTT_TX_TQM_MAX_LIST_MPDU_END_REASON);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_GEN_MPDU_STATS_TLV_V:");

	ARRAY_TO_STRING(gen_mpdu_end_reason, htt_stats_buf->gen_mpdu_end_reason,
			num_elements);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "gen_mpdu_end_reason = %s\n",
			   gen_mpdu_end_reason);

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
	char list_mpdu_end_reason[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_TX_TQM_MAX_LIST_MPDU_END_REASON);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_TQM_LIST_MPDU_STATS_TLV_V:");

	ARRAY_TO_STRING(list_mpdu_end_reason, htt_stats_buf->list_mpdu_end_reason,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "list_mpdu_end_reason = %s\n",
			   list_mpdu_end_reason);
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
	char list_mpdu_cnt_hist[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      HTT_TX_TQM_MAX_LIST_MPDU_CNT_HISTOGRAM_BINS);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_LIST_MPDU_CNT_TLV_V:");

	ARRAY_TO_STRING(list_mpdu_cnt_hist, htt_stats_buf->list_mpdu_cnt_hist,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "list_mpdu_cnt_hist = %s\n",
			   list_mpdu_cnt_hist);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_PDEV_STATS_TLV_V:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_count = %u",
			   htt_stats_buf->msdu_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_count = %u",
			   htt_stats_buf->mpdu_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_msdu = %u",
			   htt_stats_buf->remove_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_mpdu = %u",
			   htt_stats_buf->remove_mpdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_msdu_ttl = %u",
			   htt_stats_buf->remove_msdu_ttl);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "send_bar = %u",
			   htt_stats_buf->send_bar);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "bar_sync = %u",
			   htt_stats_buf->bar_sync);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "notify_mpdu = %u",
			   htt_stats_buf->notify_mpdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sync_cmd = %u",
			   htt_stats_buf->sync_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "write_cmd = %u",
			   htt_stats_buf->write_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_trigger = %u",
			   htt_stats_buf->hwsch_trigger);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ack_tlv_proc = %u",
			   htt_stats_buf->ack_tlv_proc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "gen_mpdu_cmd = %u",
			   htt_stats_buf->gen_mpdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "gen_list_cmd = %u",
			   htt_stats_buf->gen_list_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_mpdu_cmd = %u",
			   htt_stats_buf->remove_mpdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_mpdu_tried_cmd = %u",
			   htt_stats_buf->remove_mpdu_tried_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_queue_stats_cmd = %u",
			   htt_stats_buf->mpdu_queue_stats_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_head_info_cmd = %u",
			   htt_stats_buf->mpdu_head_info_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_flow_stats_cmd = %u",
			   htt_stats_buf->msdu_flow_stats_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_msdu_cmd = %u",
			   htt_stats_buf->remove_msdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_msdu_ttl_cmd = %u",
			   htt_stats_buf->remove_msdu_ttl_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "flush_cache_cmd = %u",
			   htt_stats_buf->flush_cache_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "update_mpduq_cmd = %u",
			   htt_stats_buf->update_mpduq_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "enqueue = %u",
			   htt_stats_buf->enqueue);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "enqueue_notify = %u",
			   htt_stats_buf->enqueue_notify);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "notify_mpdu_at_head = %u",
			   htt_stats_buf->notify_mpdu_at_head);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "notify_mpdu_state_valid = %u",
			   htt_stats_buf->notify_mpdu_state_valid);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_udp_notify1 = %u",
			   htt_stats_buf->sched_udp_notify1);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_udp_notify2 = %u",
			   htt_stats_buf->sched_udp_notify2);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_nonudp_notify1 = %u",
			   htt_stats_buf->sched_nonudp_notify1);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sched_nonudp_notify2 = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_CMN_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "max_cmdq_id = %u",
			   htt_stats_buf->max_cmdq_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "list_mpdu_cnt_hist_intvl = %u",
			   htt_stats_buf->list_mpdu_cnt_hist_intvl);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "add_msdu = %u",
			   htt_stats_buf->add_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "q_empty = %u",
			   htt_stats_buf->q_empty);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "q_not_empty = %u",
			   htt_stats_buf->q_not_empty);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "drop_notification = %u",
			   htt_stats_buf->drop_notification);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "desc_threshold = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_ERROR_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "q_empty_failure = %u",
			   htt_stats_buf->q_empty_failure);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "q_not_empty_failure = %u",
			   htt_stats_buf->q_not_empty_failure);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "add_msdu_failure = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_TQM_CMDQ_STATUS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__cmdq_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cmdq_id = %u\n",
			   (htt_stats_buf->mac_id__cmdq_id__word & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sync_cmd = %u",
			   htt_stats_buf->sync_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "write_cmd = %u",
			   htt_stats_buf->write_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "gen_mpdu_cmd = %u",
			   htt_stats_buf->gen_mpdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_queue_stats_cmd = %u",
			   htt_stats_buf->mpdu_queue_stats_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_head_info_cmd = %u",
			   htt_stats_buf->mpdu_head_info_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "msdu_flow_stats_cmd = %u",
			   htt_stats_buf->msdu_flow_stats_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_mpdu_cmd = %u",
			   htt_stats_buf->remove_mpdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "remove_msdu_cmd = %u",
			   htt_stats_buf->remove_msdu_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "flush_cache_cmd = %u",
			   htt_stats_buf->flush_cache_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "update_mpduq_cmd = %u",
			   htt_stats_buf->update_mpduq_cmd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "update_msduq_cmd = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_EAPOL_PACKETS_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "m1_packets = %u",
			   htt_stats_buf->m1_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "m2_packets = %u",
			   htt_stats_buf->m2_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "m3_packets = %u",
			   htt_stats_buf->m3_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "m4_packets = %u",
			   htt_stats_buf->m4_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "g1_packets = %u",
			   htt_stats_buf->g1_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "g2_packets = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_CLASSIFY_FAILED_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ap_bss_peer_not_found = %u",
			   htt_stats_buf->ap_bss_peer_not_found);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ap_bcast_mcast_no_peer = %u",
			   htt_stats_buf->ap_bcast_mcast_no_peer);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sta_delete_in_progress = %u",
			   htt_stats_buf->sta_delete_in_progress);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ibss_no_bss_peer = %u",
			   htt_stats_buf->ibss_no_bss_peer);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "invalid_vdev_type = %u",
			   htt_stats_buf->invalid_vdev_type);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "invalid_ast_peer_entry = %u",
			   htt_stats_buf->invalid_ast_peer_entry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "peer_entry_invalid = %u",
			   htt_stats_buf->peer_entry_invalid);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ethertype_not_ip = %u",
			   htt_stats_buf->ethertype_not_ip);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "eapol_lookup_failed = %u",
			   htt_stats_buf->eapol_lookup_failed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qpeer_not_allow_data = %u",
			   htt_stats_buf->qpeer_not_allow_data);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_tid_override = %u",
			   htt_stats_buf->fse_tid_override);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ipv6_jumbogram_zero_length = %u",
			   htt_stats_buf->ipv6_jumbogram_zero_length);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "qos_to_non_qos_in_prog = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_DE_CLASSIFY_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "arp_packets = %u",
			   htt_stats_buf->arp_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "igmp_packets = %u",
			   htt_stats_buf->igmp_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dhcp_packets = %u",
			   htt_stats_buf->dhcp_packets);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "host_inspected = %u",
			   htt_stats_buf->host_inspected);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_included = %u",
			   htt_stats_buf->htt_included);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_mcs = %u",
			   htt_stats_buf->htt_valid_mcs);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_nss = %u",
			   htt_stats_buf->htt_valid_nss);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_preamble_type = %u",
			   htt_stats_buf->htt_valid_preamble_type);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_chainmask = %u",
			   htt_stats_buf->htt_valid_chainmask);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_guard_interval = %u",
			   htt_stats_buf->htt_valid_guard_interval);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_retries = %u",
			   htt_stats_buf->htt_valid_retries);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_bw_info = %u",
			   htt_stats_buf->htt_valid_bw_info);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_power = %u",
			   htt_stats_buf->htt_valid_power);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_key_flags = 0x%x",
			   htt_stats_buf->htt_valid_key_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_valid_no_encryption = %u",
			   htt_stats_buf->htt_valid_no_encryption);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_entry_count = %u",
			   htt_stats_buf->fse_entry_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_priority_be = %u",
			   htt_stats_buf->fse_priority_be);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_priority_high = %u",
			   htt_stats_buf->fse_priority_high);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_priority_low = %u",
			   htt_stats_buf->fse_priority_low);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_traffic_ptrn_be = %u",
			   htt_stats_buf->fse_traffic_ptrn_be);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_traffic_ptrn_over_sub = %u",
			   htt_stats_buf->fse_traffic_ptrn_over_sub);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_traffic_ptrn_bursty = %u",
			   htt_stats_buf->fse_traffic_ptrn_bursty);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_traffic_ptrn_interactive = %u",
			   htt_stats_buf->fse_traffic_ptrn_interactive);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_traffic_ptrn_periodic = %u",
			   htt_stats_buf->fse_traffic_ptrn_periodic);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_hwqueue_alloc = %u",
			   htt_stats_buf->fse_hwqueue_alloc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_hwqueue_created = %u",
			   htt_stats_buf->fse_hwqueue_created);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_hwqueue_send_to_host = %u",
			   htt_stats_buf->fse_hwqueue_send_to_host);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mcast_entry = %u",
			   htt_stats_buf->mcast_entry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "bcast_entry = %u",
			   htt_stats_buf->bcast_entry);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_update_peer_cache = %u",
			   htt_stats_buf->htt_update_peer_cache);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "htt_learning_frame = %u",
			   htt_stats_buf->htt_learning_frame);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fse_invalid_peer = %u",
			   htt_stats_buf->fse_invalid_peer);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mec_notify = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_CLASSIFY_STATUS_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "eok = %u",
			   htt_stats_buf->eok);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "classify_done = %u",
			   htt_stats_buf->classify_done);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "lookup_failed = %u",
			   htt_stats_buf->lookup_failed);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "send_host_dhcp = %u",
			   htt_stats_buf->send_host_dhcp);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "send_host_mcast = %u",
			   htt_stats_buf->send_host_mcast);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "send_host_unknown_dest = %u",
			   htt_stats_buf->send_host_unknown_dest);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "send_host = %u",
			   htt_stats_buf->send_host);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "status_invalid = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_ENQUEUE_PACKETS_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "enqueued_pkts = %u",
			htt_stats_buf->enqueued_pkts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "to_tqm = %u",
			htt_stats_buf->to_tqm);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "to_tqm_bypass = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_ENQUEUE_DISCARD_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "discarded_pkts = %u",
			   htt_stats_buf->discarded_pkts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_frames = %u",
			   htt_stats_buf->local_frames);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "is_ext_msdu = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_DE_COMPL_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tcl_dummy_frame = %u",
			   htt_stats_buf->tcl_dummy_frame);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tqm_dummy_frame = %u",
			   htt_stats_buf->tqm_dummy_frame);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tqm_notify_frame = %u",
			   htt_stats_buf->tqm_notify_frame);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw2wbm_enq = %u",
			   htt_stats_buf->fw2wbm_enq);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tqm_bypass_frame = %u\n",
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
	char fw2wbm_ring_full_hist[HTT_MAX_STRING_LEN] = {0};
	u16  num_elements = tag_len >> 2;
	u32  required_buffer_size = HTT_MAX_PRINT_CHAR_PER_ELEM * num_elements;

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_TX_DE_FW2WBM_RING_FULL_HIST_TLV");

	if (required_buffer_size < HTT_MAX_STRING_LEN) {
		ARRAY_TO_STRING(fw2wbm_ring_full_hist,
				htt_stats_buf->fw2wbm_ring_full_hist,
				num_elements);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "fw2wbm_ring_full_hist = %s\n",
				   fw2wbm_ring_full_hist);
	} else {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "INSUFFICIENT PRINT BUFFER ");
	}

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_DE_CMN_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tcl2fw_entry_count = %u",
			   htt_stats_buf->tcl2fw_entry_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "not_to_fw = %u",
			   htt_stats_buf->not_to_fw);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "invalid_pdev_vdev_peer = %u",
			   htt_stats_buf->invalid_pdev_vdev_peer);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tcl_res_invalid_addrx = %u",
			   htt_stats_buf->tcl_res_invalid_addrx);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wbm2fw_entry_count = %u",
			   htt_stats_buf->wbm2fw_entry_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "invalid_pdev = %u\n",
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
	char low_wm_hit_count[HTT_MAX_STRING_LEN] = {0};
	char high_wm_hit_count[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RING_IF_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "base_addr = %u",
			   htt_stats_buf->base_addr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "elem_size = %u",
			   htt_stats_buf->elem_size);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_elems = %u",
			   htt_stats_buf->num_elems__prefetch_tail_idx & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "prefetch_tail_idx = %u",
			   (htt_stats_buf->num_elems__prefetch_tail_idx &
			   0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "head_idx = %u",
			   htt_stats_buf->head_idx__tail_idx & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tail_idx = %u",
			   (htt_stats_buf->head_idx__tail_idx & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "shadow_head_idx = %u",
			   htt_stats_buf->shadow_head_idx__shadow_tail_idx & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "shadow_tail_idx = %u",
			   (htt_stats_buf->shadow_head_idx__shadow_tail_idx &
			   0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_tail_incr = %u",
			   htt_stats_buf->num_tail_incr);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "lwm_thresh = %u",
			   htt_stats_buf->lwm_thresh__hwm_thresh & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwm_thresh = %u",
			   (htt_stats_buf->lwm_thresh__hwm_thresh & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "overrun_hit_count = %u",
			   htt_stats_buf->overrun_hit_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "underrun_hit_count = %u",
			   htt_stats_buf->underrun_hit_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "prod_blockwait_count = %u",
			   htt_stats_buf->prod_blockwait_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "cons_blockwait_count = %u",
			   htt_stats_buf->cons_blockwait_count);

	ARRAY_TO_STRING(low_wm_hit_count, htt_stats_buf->low_wm_hit_count,
			HTT_STATS_LOW_WM_BINS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "low_wm_hit_count = %s ",
			   low_wm_hit_count);

	ARRAY_TO_STRING(high_wm_hit_count, htt_stats_buf->high_wm_hit_count,
			HTT_STATS_HIGH_WM_BINS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "high_wm_hit_count = %s\n",
			   high_wm_hit_count);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RING_IF_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_records = %u\n",
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
	char dwords_used_by_user_n[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = tag_len >> 2;

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SFM_CLIENT_USER_TLV_V:");

	ARRAY_TO_STRING(dwords_used_by_user_n,
			htt_stats_buf->dwords_used_by_user_n,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dwords_used_by_user_n = %s\n",
			   dwords_used_by_user_n);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SFM_CLIENT_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "client_id = %u",
			   htt_stats_buf->client_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_min = %u",
			   htt_stats_buf->buf_min);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_max = %u",
			   htt_stats_buf->buf_max);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_busy = %u",
			   htt_stats_buf->buf_busy);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_alloc = %u",
			   htt_stats_buf->buf_alloc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_avail = %u",
			   htt_stats_buf->buf_avail);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_users = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SFM_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "buf_total = %u",
			   htt_stats_buf->buf_total);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mem_empty = %u",
			   htt_stats_buf->mem_empty);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "deallocate_bufs = %u",
			   htt_stats_buf->deallocate_bufs);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_records = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SRING_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__ring_id__arena__ep & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ring_id = %u",
			   (htt_stats_buf->mac_id__ring_id__arena__ep & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "arena = %u",
			   (htt_stats_buf->mac_id__ring_id__arena__ep & 0xFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ep = %u",
			   (htt_stats_buf->mac_id__ring_id__arena__ep & 0x1000000) >> 24);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "base_addr_lsb = 0x%x",
			   htt_stats_buf->base_addr_lsb);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "base_addr_msb = 0x%x",
			   htt_stats_buf->base_addr_msb);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ring_size = %u",
			   htt_stats_buf->ring_size);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "elem_size = %u",
			   htt_stats_buf->elem_size);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_avail_words = %u",
			   htt_stats_buf->num_avail_words__num_valid_words & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_valid_words = %u",
			   (htt_stats_buf->num_avail_words__num_valid_words &
			   0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "head_ptr = %u",
			   htt_stats_buf->head_ptr__tail_ptr & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tail_ptr = %u",
			   (htt_stats_buf->head_ptr__tail_ptr & 0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "consumer_empty = %u",
			   htt_stats_buf->consumer_empty__producer_full & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "producer_full = %u",
			   (htt_stats_buf->consumer_empty__producer_full &
			   0xFFFF0000) >> 16);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "prefetch_count = %u",
			   htt_stats_buf->prefetch_count__internal_tail_ptr & 0xFFFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "internal_tail_ptr = %u\n",
			   (htt_stats_buf->prefetch_count__internal_tail_ptr &
			   0xFFFF0000) >> 16);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_SRING_CMN_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_records = %u\n",
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
	char str_buf[HTT_MAX_STRING_LEN] = {0};
	char *tx_gi[HTT_TX_PEER_STATS_NUM_GI_COUNTERS] = {NULL};

	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++) {
		tx_gi[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!tx_gi[j])
			goto fail;
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_TX_PDEV_RATE_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_ldpc = %u",
			   htt_stats_buf->tx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_tx_ldpc = %u",
			   htt_stats_buf->ac_mu_mimo_tx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_tx_ldpc = %u",
			   htt_stats_buf->ax_mu_mimo_tx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ofdma_tx_ldpc = %u",
			   htt_stats_buf->ofdma_tx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts_cnt = %u",
			   htt_stats_buf->rts_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts_success = %u",
			   htt_stats_buf->rts_success);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ack_rssi = %u",
			   htt_stats_buf->ack_rssi);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "Legacy CCK Rates: 1 Mbps: %u, 2 Mbps: %u, 5.5 Mbps: %u, 11 Mbps: %u",
			   htt_stats_buf->tx_legacy_cck_rate[0],
			   htt_stats_buf->tx_legacy_cck_rate[1],
			   htt_stats_buf->tx_legacy_cck_rate[2],
			   htt_stats_buf->tx_legacy_cck_rate[3]);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "Legacy OFDM Rates: 6 Mbps: %u, 9 Mbps: %u, 12 Mbps: %u, 18 Mbps: %u\n"
			   "                   24 Mbps: %u, 36 Mbps: %u, 48 Mbps: %u, 54 Mbps: %u",
			   htt_stats_buf->tx_legacy_ofdm_rate[0],
			   htt_stats_buf->tx_legacy_ofdm_rate[1],
			   htt_stats_buf->tx_legacy_ofdm_rate[2],
			   htt_stats_buf->tx_legacy_ofdm_rate[3],
			   htt_stats_buf->tx_legacy_ofdm_rate[4],
			   htt_stats_buf->tx_legacy_ofdm_rate[5],
			   htt_stats_buf->tx_legacy_ofdm_rate[6],
			   htt_stats_buf->tx_legacy_ofdm_rate[7]);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ac_mu_mimo_tx_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_tx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ax_mu_mimo_tx_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_tx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ofdma_tx_mcs,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ofdma_tx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_nss,
			HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ac_mu_mimo_tx_nss,
			HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_tx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ax_mu_mimo_tx_nss,
			HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_tx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ofdma_tx_nss,
			HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ofdma_tx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_bw,
			HTT_TX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_bw = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ac_mu_mimo_tx_bw,
			HTT_TX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ac_mu_mimo_tx_bw = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ax_mu_mimo_tx_bw,
			HTT_TX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ax_mu_mimo_tx_bw = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ofdma_tx_bw,
			HTT_TX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ofdma_tx_bw = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_stbc,
			HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_stbc = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_pream,
			HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_pream = %s ", str_buf);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HE LTF: 1x: %u, 2x: %u, 4x: %u",
			   htt_stats_buf->tx_he_ltf[1],
			   htt_stats_buf->tx_he_ltf[2],
			   htt_stats_buf->tx_he_ltf[3]);

	/* SU GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(tx_gi[j], htt_stats_buf->tx_gi[j],
				HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_gi[%u] = %s ",
				   j, tx_gi[j]);
	}

	/* AC MU-MIMO GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(tx_gi[j], htt_stats_buf->ac_mu_mimo_tx_gi[j],
				HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ac_mu_mimo_tx_gi[%u] = %s ",
				   j, tx_gi[j]);
	}

	/* AX MU-MIMO GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(tx_gi[j], htt_stats_buf->ax_mu_mimo_tx_gi[j],
				HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "ax_mu_mimo_tx_gi[%u] = %s ",
				   j, tx_gi[j]);
	}

	/* DL OFDMA GI Stats */
	for (j = 0; j < HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(tx_gi[j], htt_stats_buf->ofdma_tx_gi[j],
				HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "ofdma_tx_gi[%u] = %s ",
				   j, tx_gi[j]);
	}

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->tx_dcm,
			HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tx_dcm = %s\n", str_buf);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
fail:
	for (j = 0; j < HTT_TX_PEER_STATS_NUM_GI_COUNTERS; j++)
		kfree(tx_gi[j]);
}

static inline void htt_print_rx_pdev_rate_stats_tlv(const void *tag_buf,
						    struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_pdev_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u8 i, j;
	u16 index = 0;
	char *rssi_chain[HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS] = {NULL};
	char *rx_gi[HTT_RX_PDEV_STATS_NUM_GI_COUNTERS] = {NULL};
	char str_buf[HTT_MAX_STRING_LEN] = {0};
	char *rx_pilot_evm_db[HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS] = {NULL};

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		rssi_chain[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!rssi_chain[j])
			goto fail;
	}

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		rx_gi[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!rx_gi[j])
			goto fail;
	}

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		rx_pilot_evm_db[j] = kmalloc(HTT_MAX_STRING_LEN, GFP_ATOMIC);
		if (!rx_pilot_evm_db[j])
			goto fail;
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_PDEV_RATE_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "nsts = %u",
			   htt_stats_buf->nsts);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ldpc = %u",
			   htt_stats_buf->rx_ldpc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rts_cnt = %u",
			   htt_stats_buf->rts_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_mgmt = %u",
			   htt_stats_buf->rssi_mgmt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_data = %u",
			   htt_stats_buf->rssi_data);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_comb = %u",
			   htt_stats_buf->rssi_comb);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_in_dbm = %d",
			   htt_stats_buf->rssi_in_dbm);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_mcs,
			HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_mcs = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_nss,
			HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_dcm,
			HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_dcm = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_stbc,
			HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_stbc = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_bw,
			HTT_RX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_bw = %s ", str_buf);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_evm_nss_count = %u",
			htt_stats_buf->nss_count);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_evm_pilot_count = %u",
			htt_stats_buf->pilot_count);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		index = 0;

		for (i = 0; i < HTT_RX_PDEV_STATS_RXEVM_MAX_PILOTS_PER_NSS; i++)
			index += snprintf(&rx_pilot_evm_db[j][index],
					  HTT_MAX_STRING_LEN - index,
					  " %u:%d,",
					  i,
					  htt_stats_buf->rx_pilot_evm_db[j][i]);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "pilot_evm_dB[%u] = %s ",
				   j, rx_pilot_evm_db[j]);
	}

	index = 0;
	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	for (i = 0; i < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		index += snprintf(&str_buf[index],
				  HTT_MAX_STRING_LEN - index,
				  " %u:%d,", i, htt_stats_buf->rx_pilot_evm_db_mean[i]);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pilot_evm_dB_mean = %s ", str_buf);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		ARRAY_TO_STRING(rssi_chain[j], htt_stats_buf->rssi_chain[j],
				HTT_RX_PDEV_STATS_NUM_BW_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "rssi_chain[%u] = %s ",
				   j, rssi_chain[j]);
	}

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(rx_gi[j], htt_stats_buf->rx_gi[j],
				HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_gi[%u] = %s ",
				   j, rx_gi[j]);
	}

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_pream,
			HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_pream = %s", str_buf);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_11ax_su_ext = %u",
			   htt_stats_buf->rx_11ax_su_ext);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_11ac_mumimo = %u",
			   htt_stats_buf->rx_11ac_mumimo);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_11ax_mumimo = %u",
			   htt_stats_buf->rx_11ax_mumimo);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_11ax_ofdma = %u",
			   htt_stats_buf->rx_11ax_ofdma);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "txbf = %u",
			   htt_stats_buf->txbf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_legacy_cck_rate,
			HTT_RX_PDEV_STATS_NUM_LEGACY_CCK_STATS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_legacy_cck_rate = %s ",
			   str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_legacy_ofdm_rate,
			HTT_RX_PDEV_STATS_NUM_LEGACY_OFDM_STATS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_legacy_ofdm_rate = %s ",
			   str_buf);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_active_dur_us_low = %u",
			   htt_stats_buf->rx_active_dur_us_low);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_active_dur_us_high = %u",
			htt_stats_buf->rx_active_dur_us_high);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_11ax_ul_ofdma = %u",
			htt_stats_buf->rx_11ax_ul_ofdma);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ul_ofdma_rx_mcs,
			HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_mcs = %s ", str_buf);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		ARRAY_TO_STRING(rx_gi[j], htt_stats_buf->ul_ofdma_rx_gi[j],
				HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS);
		len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_gi[%u] = %s ",
				   j, rx_gi[j]);
	}

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ul_ofdma_rx_nss,
			HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_nss = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->ul_ofdma_rx_bw,
			HTT_RX_PDEV_STATS_NUM_BW_COUNTERS);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_bw = %s ", str_buf);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_stbc = %u",
			htt_stats_buf->ul_ofdma_rx_stbc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ul_ofdma_rx_ldpc = %u",
			htt_stats_buf->ul_ofdma_rx_ldpc);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_ulofdma_non_data_ppdu,
			HTT_RX_PDEV_MAX_OFDMA_NUM_USER);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ulofdma_non_data_ppdu = %s ",
			   str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_ulofdma_data_ppdu,
			HTT_RX_PDEV_MAX_OFDMA_NUM_USER);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ulofdma_data_ppdu = %s ",
			   str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_ulofdma_mpdu_ok,
			HTT_RX_PDEV_MAX_OFDMA_NUM_USER);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ulofdma_mpdu_ok = %s ", str_buf);

	memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
	ARRAY_TO_STRING(str_buf, htt_stats_buf->rx_ulofdma_mpdu_fail,
			HTT_RX_PDEV_MAX_OFDMA_NUM_USER);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ulofdma_mpdu_fail = %s",
			   str_buf);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		index = 0;
		memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
		for (i = 0; i < HTT_RX_PDEV_MAX_OFDMA_NUM_USER; i++)
			index += snprintf(&str_buf[index],
					  HTT_MAX_STRING_LEN - index,
					  " %u:%d,",
					  i, htt_stats_buf->rx_ul_fd_rssi[j][i]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "rx_ul_fd_rssi: nss[%u] = %s", j, str_buf);
	}

	len += HTT_DBG_OUT(buf + len, buf_len - len, "per_chain_rssi_pkt_type = %#x",
			   htt_stats_buf->per_chain_rssi_pkt_type);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		index = 0;
		memset(str_buf, 0x0, HTT_MAX_STRING_LEN);
		for (i = 0; i < HTT_RX_PDEV_STATS_NUM_BW_COUNTERS; i++)
			index += snprintf(&str_buf[index],
					  HTT_MAX_STRING_LEN - index,
					  " %u:%d,",
					  i,
					  htt_stats_buf->rx_per_chain_rssi_in_dbm[j][i]);
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "rx_per_chain_rssi_in_dbm[%u] = %s ", j, str_buf);
	}
	len += HTT_DBG_OUT(buf + len, buf_len - len, "\n");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;

fail:
	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		kfree(rssi_chain[j]);

	for (j = 0; j < HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		kfree(rx_pilot_evm_db[j]);

	for (i = 0; i < HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; i++)
		kfree(rx_gi[i]);
}

static inline void htt_print_rx_soc_fw_stats_tlv(const void *tag_buf,
						 struct debug_htt_stats_req *stats_req)
{
	const struct htt_rx_soc_fw_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_SOC_FW_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_reo_ring_data_msdu = %u",
			   htt_stats_buf->fw_reo_ring_data_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_to_host_data_msdu_bcmc = %u",
			   htt_stats_buf->fw_to_host_data_msdu_bcmc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_to_host_data_msdu_uc = %u",
			   htt_stats_buf->fw_to_host_data_msdu_uc);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "ofld_remote_data_buf_recycle_cnt = %u",
			   htt_stats_buf->ofld_remote_data_buf_recycle_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "ofld_remote_free_buf_indication_cnt = %u",
			   htt_stats_buf->ofld_remote_free_buf_indication_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "ofld_buf_to_host_data_msdu_uc = %u",
			   htt_stats_buf->ofld_buf_to_host_data_msdu_uc);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "reo_fw_ring_to_host_data_msdu_uc = %u",
			   htt_stats_buf->reo_fw_ring_to_host_data_msdu_uc);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wbm_sw_ring_reap = %u",
			   htt_stats_buf->wbm_sw_ring_reap);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wbm_forward_to_host_cnt = %u",
			   htt_stats_buf->wbm_forward_to_host_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wbm_target_recycle_cnt = %u",
			   htt_stats_buf->wbm_target_recycle_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "target_refill_ring_recycle_cnt = %u",
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
	char refill_ring_empty_cnt[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_REFILL_MAX_RING);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_RX_SOC_FW_REFILL_RING_EMPTY_TLV_V:");

	ARRAY_TO_STRING(refill_ring_empty_cnt,
			htt_stats_buf->refill_ring_empty_cnt,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "refill_ring_empty_cnt = %s\n",
			   refill_ring_empty_cnt);

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
	char rxdma_err_cnt[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_RXDMA_MAX_ERR_CODE);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_RX_SOC_FW_REFILL_RING_NUM_RXDMA_ERR_TLV_V:");

	ARRAY_TO_STRING(rxdma_err_cnt,
			htt_stats_buf->rxdma_err,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rxdma_err = %s\n",
			   rxdma_err_cnt);

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
	char reo_err_cnt[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_REO_MAX_ERR_CODE);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_RX_SOC_FW_REFILL_RING_NUM_REO_ERR_TLV_V:");

	ARRAY_TO_STRING(reo_err_cnt,
			htt_stats_buf->reo_err,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "reo_err = %s\n",
			   reo_err_cnt);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_REO_RESOURCE_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sample_id = %u",
			   htt_stats_buf->sample_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "total_max = %u",
			   htt_stats_buf->total_max);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "total_avg = %u",
			   htt_stats_buf->total_avg);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "total_sample = %u",
			   htt_stats_buf->total_sample);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "non_zeros_avg = %u",
			   htt_stats_buf->non_zeros_avg);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "non_zeros_sample = %u",
			   htt_stats_buf->non_zeros_sample);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_non_zeros_max = %u",
			   htt_stats_buf->last_non_zeros_max);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_non_zeros_min %u",
			   htt_stats_buf->last_non_zeros_min);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_non_zeros_avg %u",
			   htt_stats_buf->last_non_zeros_avg);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_non_zeros_sample %u\n",
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
	char refill_ring_num_refill[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_REFILL_MAX_RING);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_RX_SOC_FW_REFILL_RING_NUM_REFILL_TLV_V:");

	ARRAY_TO_STRING(refill_ring_num_refill,
			htt_stats_buf->refill_ring_num_refill,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "refill_ring_num_refill = %s\n",
			   refill_ring_num_refill);

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
	char fw_ring_mgmt_subtype[HTT_MAX_STRING_LEN] = {0};
	char fw_ring_ctrl_subtype[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_PDEV_FW_STATS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ppdu_recvd = %u",
			   htt_stats_buf->ppdu_recvd);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_cnt_fcs_ok = %u",
			   htt_stats_buf->mpdu_cnt_fcs_ok);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mpdu_cnt_fcs_err = %u",
			   htt_stats_buf->mpdu_cnt_fcs_err);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tcp_msdu_cnt = %u",
			   htt_stats_buf->tcp_msdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "tcp_ack_msdu_cnt = %u",
			   htt_stats_buf->tcp_ack_msdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "udp_msdu_cnt = %u",
			   htt_stats_buf->udp_msdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "other_msdu_cnt = %u",
			   htt_stats_buf->other_msdu_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_mpdu_ind = %u",
			   htt_stats_buf->fw_ring_mpdu_ind);

	ARRAY_TO_STRING(fw_ring_mgmt_subtype,
			htt_stats_buf->fw_ring_mgmt_subtype,
			HTT_STATS_SUBTYPE_MAX);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_mgmt_subtype = %s ",
			   fw_ring_mgmt_subtype);

	ARRAY_TO_STRING(fw_ring_ctrl_subtype,
			htt_stats_buf->fw_ring_ctrl_subtype,
			HTT_STATS_SUBTYPE_MAX);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_ctrl_subtype = %s ",
			   fw_ring_ctrl_subtype);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_mcast_data_msdu = %u",
			   htt_stats_buf->fw_ring_mcast_data_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_bcast_data_msdu = %u",
			   htt_stats_buf->fw_ring_bcast_data_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_ucast_data_msdu = %u",
			   htt_stats_buf->fw_ring_ucast_data_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_null_data_msdu = %u",
			   htt_stats_buf->fw_ring_null_data_msdu);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_mpdu_drop = %u",
			   htt_stats_buf->fw_ring_mpdu_drop);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "ofld_local_data_ind_cnt = %u",
			   htt_stats_buf->ofld_local_data_ind_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "ofld_local_data_buf_recycle_cnt = %u",
			   htt_stats_buf->ofld_local_data_buf_recycle_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "drx_local_data_ind_cnt = %u",
			   htt_stats_buf->drx_local_data_ind_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "drx_local_data_buf_recycle_cnt = %u",
			   htt_stats_buf->drx_local_data_buf_recycle_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_nondata_ind_cnt = %u",
			   htt_stats_buf->local_nondata_ind_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "local_nondata_buf_recycle_cnt = %u",
			   htt_stats_buf->local_nondata_buf_recycle_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_status_buf_ring_refill_cnt = %u",
			   htt_stats_buf->fw_status_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_status_buf_ring_empty_cnt = %u",
			   htt_stats_buf->fw_status_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_pkt_buf_ring_refill_cnt = %u",
			   htt_stats_buf->fw_pkt_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_pkt_buf_ring_empty_cnt = %u",
			   htt_stats_buf->fw_pkt_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_link_buf_ring_refill_cnt = %u",
			   htt_stats_buf->fw_link_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_link_buf_ring_empty_cnt = %u",
			   htt_stats_buf->fw_link_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "host_pkt_buf_ring_refill_cnt = %u",
			   htt_stats_buf->host_pkt_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "host_pkt_buf_ring_empty_cnt = %u",
			   htt_stats_buf->host_pkt_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_pkt_buf_ring_refill_cnt = %u",
			   htt_stats_buf->mon_pkt_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_pkt_buf_ring_empty_cnt = %u",
			   htt_stats_buf->mon_pkt_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "mon_status_buf_ring_refill_cnt = %u",
			   htt_stats_buf->mon_status_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_status_buf_ring_empty_cnt = %u",
			   htt_stats_buf->mon_status_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_desc_buf_ring_refill_cnt = %u",
			   htt_stats_buf->mon_desc_buf_ring_refill_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_desc_buf_ring_empty_cnt = %u",
			   htt_stats_buf->mon_desc_buf_ring_empty_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_dest_ring_update_cnt = %u",
			   htt_stats_buf->mon_dest_ring_update_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mon_dest_ring_full_cnt = %u",
			   htt_stats_buf->mon_dest_ring_full_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_suspend_cnt = %u",
			   htt_stats_buf->rx_suspend_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_suspend_fail_cnt = %u",
			   htt_stats_buf->rx_suspend_fail_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_resume_cnt = %u",
			   htt_stats_buf->rx_resume_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_resume_fail_cnt = %u",
			   htt_stats_buf->rx_resume_fail_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ring_switch_cnt = %u",
			   htt_stats_buf->rx_ring_switch_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_ring_restore_cnt = %u",
			   htt_stats_buf->rx_ring_restore_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_flush_cnt = %u",
			   htt_stats_buf->rx_flush_cnt);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "rx_recovery_reset_cnt = %u\n",
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
	char fw_ring_mpdu_err[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_RX_PDEV_FW_RING_MPDU_ERR_TLV_V:");

	ARRAY_TO_STRING(fw_ring_mpdu_err,
			htt_stats_buf->fw_ring_mpdu_err,
			HTT_RX_STATS_RXDMA_MAX_ERR);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_ring_mpdu_err = %s\n",
			   fw_ring_mpdu_err);

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
	char fw_mpdu_drop[HTT_MAX_STRING_LEN] = {0};
	u16 num_elems = min_t(u16, (tag_len >> 2), HTT_RX_STATS_FW_DROP_REASON_MAX);

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_PDEV_FW_MPDU_DROP_TLV_V:");

	ARRAY_TO_STRING(fw_mpdu_drop,
			htt_stats_buf->fw_mpdu_drop,
			num_elems);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "fw_mpdu_drop = %s\n", fw_mpdu_drop);

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
	char phy_errs[HTT_MAX_STRING_LEN] = {0};

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_RX_PDEV_FW_STATS_PHY_ERR_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id__word = %u",
			   htt_stats_buf->mac_id__word);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "total_phy_err_nct = %u",
			   htt_stats_buf->total_phy_err_cnt);

	ARRAY_TO_STRING(phy_errs,
			htt_stats_buf->phy_err,
			HTT_STATS_PHY_ERR_MAX);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "phy_errs = %s\n", phy_errs);

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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "\nHTT_PDEV_CCA_STATS_HIST_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "chan_num = %u",
			   htt_stats_buf->chan_num);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_records = %u",
			   htt_stats_buf->num_records);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "valid_cca_counters_bitmap = 0x%x",
			   htt_stats_buf->valid_cca_counters_bitmap);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "collection_interval = %u\n",
			   htt_stats_buf->collection_interval);

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HTT_PDEV_STATS_CCA_COUNTERS_TLV:(in usec)");
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "|  tx_frame|   rx_frame|   rx_clear| my_rx_frame|        cnt| med_rx_idle| med_tx_idle_global|   cca_obss|");

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

	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "|%10u| %10u| %10u| %11u| %10u| %11u| %18u| %10u|",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_HW_STATS_WHAL_TX_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "mac_id = %u",
			   htt_stats_buf->mac_id__word & 0xFF);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "last_unpause_ppdu_id = %u",
			   htt_stats_buf->last_unpause_ppdu_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_unpause_wait_tqm_write = %u",
			   htt_stats_buf->hwsch_unpause_wait_tqm_write);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_dummy_tlv_skipped = %u",
			   htt_stats_buf->hwsch_dummy_tlv_skipped);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "hwsch_misaligned_offset_received = %u",
			   htt_stats_buf->hwsch_misaligned_offset_received);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_reset_count = %u",
			   htt_stats_buf->hwsch_reset_count);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_dev_reset_war = %u",
			   htt_stats_buf->hwsch_dev_reset_war);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_delayed_pause = %u",
			   htt_stats_buf->hwsch_delayed_pause);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "hwsch_long_delayed_pause = %u",
			   htt_stats_buf->hwsch_long_delayed_pause);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sch_rx_ppdu_no_response = %u",
			   htt_stats_buf->sch_rx_ppdu_no_response);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sch_selfgen_response = %u",
			   htt_stats_buf->sch_selfgen_response);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sch_rx_sifs_resp_trigger= %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_PDEV_STATS_TWT_SESSIONS_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "pdev_id = %u",
			   htt_stats_buf->pdev_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "num_sessions = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "HTT_PDEV_STATS_TWT_SESSION_TLV:");
	len += HTT_DBG_OUT(buf + len, buf_len - len, "vdev_id = %u",
			   htt_stats_buf->vdev_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "peer_mac = %02x:%02x:%02x:%02x:%02x:%02x",
			   htt_stats_buf->peer_mac.mac_addr_l32 & 0xFF,
			   (htt_stats_buf->peer_mac.mac_addr_l32 & 0xFF00) >> 8,
			   (htt_stats_buf->peer_mac.mac_addr_l32 & 0xFF0000) >> 16,
			   (htt_stats_buf->peer_mac.mac_addr_l32 & 0xFF000000) >> 24,
			   (htt_stats_buf->peer_mac.mac_addr_h16 & 0xFF),
			   (htt_stats_buf->peer_mac.mac_addr_h16 & 0xFF00) >> 8);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "flow_id_flags = %u",
			   htt_stats_buf->flow_id_flags);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "dialog_id = %u",
			   htt_stats_buf->dialog_id);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wake_dura_us = %u",
			   htt_stats_buf->wake_dura_us);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "wake_intvl_us = %u",
			   htt_stats_buf->wake_intvl_us);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "sp_offset_us = %u\n",
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

	len += HTT_DBG_OUT(buf + len, buf_len - len, "OBSS Tx success PPDU = %u",
			   htt_stats_buf->num_obss_tx_ppdu_success);
	len += HTT_DBG_OUT(buf + len, buf_len - len, "OBSS Tx failures PPDU = %u\n",
			   htt_stats_buf->num_obss_tx_ppdu_failure);

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

	stats_req->buf_len = len;
}

static inline void htt_htt_stats_debug_dump(const u32 *tag_buf,
					    struct debug_htt_stats_req *stats_req)
{
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH11K_HTT_STATS_BUF_SIZE;
	u32 tlv_len = 0, i = 0, word_len = 0;

	tlv_len  = FIELD_GET(HTT_TLV_LEN, *tag_buf) + HTT_TLV_HDR_LEN;
	word_len = (tlv_len % 4) == 0 ? (tlv_len / 4) : ((tlv_len / 4) + 1);
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "============================================");
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "HKDBG TLV DUMP: (tag_len=%u bytes, words=%u)",
			   tlv_len, word_len);

	for (i = 0; i + 3 < word_len; i += 4) {
		len += HTT_DBG_OUT(buf + len, buf_len - len,
				   "0x%08x 0x%08x 0x%08x 0x%08x",
				   tag_buf[i], tag_buf[i + 1],
				   tag_buf[i + 2], tag_buf[i + 3]);
	}

	if (i + 3 == word_len) {
		len += HTT_DBG_OUT(buf + len, buf_len - len, "0x%08x 0x%08x 0x%08x ",
				tag_buf[i], tag_buf[i + 1], tag_buf[i + 2]);
	} else if (i + 2 == word_len) {
		len += HTT_DBG_OUT(buf + len, buf_len - len, "0x%08x 0x%08x ",
				tag_buf[i], tag_buf[i + 1]);
	} else if (i + 1 == word_len) {
		len += HTT_DBG_OUT(buf + len, buf_len - len, "0x%08x ",
				tag_buf[i]);
	}
	len += HTT_DBG_OUT(buf + len, buf_len - len,
			   "============================================");

	if (len >= buf_len)
		buf[buf_len - 1] = 0;
	else
		buf[len] = 0;

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
	default:
		break;
	}

	return 0;
}

void ath11k_dbg_htt_ext_stats_handler(struct ath11k_base *ab,
				      struct sk_buff *skb)
{
	struct ath11k_htt_extd_stats_msg *msg;
	struct debug_htt_stats_req *stats_req;
	struct ath11k *ar;
	u32 len;
	u64 cookie;
	int ret;
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
	if (stats_req->done) {
		spin_unlock_bh(&ar->debug.htt_stats.lock);
		return;
	}
	stats_req->done = true;
	spin_unlock_bh(&ar->debug.htt_stats.lock);

	len = FIELD_GET(HTT_T2H_EXT_STATS_INFO1_LENGTH, msg->info1);
	ret = ath11k_dp_htt_tlv_iter(ab, msg->data, len,
				     ath11k_dbg_htt_ext_stats_parse,
				     stats_req);
	if (ret)
		ath11k_warn(ab, "Failed to parse tlv %d\n", ret);

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

	if (type == ATH11K_DBG_HTT_EXT_STATS_RESET ||
	    type == ATH11K_DBG_HTT_EXT_STATS_PEER_INFO)
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
	default:
		break;
	}

	return 0;
}

int ath11k_dbg_htt_stats_req(struct ath11k *ar)
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

	if (type == ATH11K_DBG_HTT_EXT_STATS_RESET)
		return -EPERM;

	stats_req = vzalloc(sizeof(*stats_req) + ATH11K_HTT_STATS_BUF_SIZE);
	if (!stats_req)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);
	ar->debug.htt_stats.stats_req = stats_req;
	stats_req->type = type;
	ret = ath11k_dbg_htt_stats_req(ar);
	mutex_unlock(&ar->conf_mutex);
	if (ret < 0)
		goto out;

	file->private_data = stats_req;
	return 0;
out:
	vfree(stats_req);
	return ret;
}

static int ath11k_release_htt_stats(struct inode *inode, struct file *file)
{
	vfree(file->private_data);
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

void ath11k_debug_htt_stats_init(struct ath11k *ar)
{
	spin_lock_init(&ar->debug.htt_stats.lock);
	debugfs_create_file("htt_stats_type", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_type);
	debugfs_create_file("htt_stats", 0400, ar->debug.debugfs_pdev,
			    ar, &fops_dump_htt_stats);
	debugfs_create_file("htt_stats_reset", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_reset);
}
