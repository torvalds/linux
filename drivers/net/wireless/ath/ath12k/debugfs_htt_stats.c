// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "core.h"
#include "debug.h"
#include "debugfs_htt_stats.h"
#include "dp_tx.h"
#include "dp_rx.h"

static u32
print_array_to_buf_index(u8 *buf, u32 offset, const char *header, u32 stats_index,
			 const __le32 *array, u32 array_len, const char *footer)
{
	int index = 0;
	u8 i;

	if (header) {
		index += scnprintf(buf + offset,
				   ATH12K_HTT_STATS_BUF_SIZE - offset,
				   "%s = ", header);
	}
	for (i = 0; i < array_len; i++) {
		index += scnprintf(buf + offset + index,
				   (ATH12K_HTT_STATS_BUF_SIZE - offset) - index,
				   " %u:%u,", stats_index++, le32_to_cpu(array[i]));
	}
	/* To overwrite the last trailing comma */
	index--;
	*(buf + offset + index) = '\0';

	if (footer) {
		index += scnprintf(buf + offset + index,
				   (ATH12K_HTT_STATS_BUF_SIZE - offset) - index,
				   "%s", footer);
	}
	return index;
}

static u32
print_array_to_buf(u8 *buf, u32 offset, const char *header,
		   const __le32 *array, u32 array_len, const char *footer)
{
	return print_array_to_buf_index(buf, offset, header, 0, array, array_len,
					footer);
}

static u32
print_array_to_buf_s8(u8 *buf, u32 offset, const char *header, u32 stats_index,
		      const s8 *array, u32 array_len, const char *footer)
{
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	int index = 0;
	u8 i;

	if (header)
		index += scnprintf(buf + offset, buf_len - offset, "%s = ", header);

	for (i = 0; i < array_len; i++) {
		index += scnprintf(buf + offset + index, (buf_len - offset) - index,
				   " %u:%d,", stats_index++, array[i]);
	}

	index--;
	if ((offset + index) < buf_len)
		buf[offset + index] = '\0';

	if (footer) {
		index += scnprintf(buf + offset + index, (buf_len - offset) - index,
				   "%s", footer);
	}

	return index;
}

static const char *ath12k_htt_ax_tx_rx_ru_size_to_str(u8 ru_size)
{
	switch (ru_size) {
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_26:
		return "26";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_52:
		return "52";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_106:
		return "106";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_242:
		return "242";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_484:
		return "484";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_996:
		return "996";
	case ATH12K_HTT_TX_RX_PDEV_STATS_AX_RU_SIZE_996x2:
		return "996x2";
	default:
		return "unknown";
	}
}

static const char *ath12k_htt_be_tx_rx_ru_size_to_str(u8 ru_size)
{
	switch (ru_size) {
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_26:
		return "26";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_52:
		return "52";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_52_26:
		return "52+26";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_106:
		return "106";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_106_26:
		return "106+26";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_242:
		return "242";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_484:
		return "484";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_484_242:
		return "484+242";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996:
		return "996";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996_484:
		return "996+484";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996_484_242:
		return "996+484+242";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x2:
		return "996x2";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x2_484:
		return "996x2+484";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x3:
		return "996x3";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x3_484:
		return "996x3+484";
	case ATH12K_HTT_TX_RX_PDEV_STATS_BE_RU_SIZE_996x4:
		return "996x4";
	default:
		return "unknown";
	}
}

static const char*
ath12k_tx_ru_size_to_str(enum ath12k_htt_stats_ru_type ru_type, u8 ru_size)
{
	if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_RU_ONLY)
		return ath12k_htt_ax_tx_rx_ru_size_to_str(ru_size);
	else if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_AND_MULTI_RU)
		return ath12k_htt_be_tx_rx_ru_size_to_str(ru_size);
	else
		return "unknown";
}

static void
htt_print_tx_pdev_stats_cmn_tlv(const void *tag_buf, u16 tag_len,
				struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_STATS_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "comp_delivered = %u\n",
			 le32_to_cpu(htt_stats_buf->comp_delivered));
	len += scnprintf(buf + len, buf_len - len, "self_triggers = %u\n",
			 le32_to_cpu(htt_stats_buf->self_triggers));
	len += scnprintf(buf + len, buf_len - len, "hw_queued = %u\n",
			 le32_to_cpu(htt_stats_buf->hw_queued));
	len += scnprintf(buf + len, buf_len - len, "hw_reaped = %u\n",
			 le32_to_cpu(htt_stats_buf->hw_reaped));
	len += scnprintf(buf + len, buf_len - len, "underrun = %u\n",
			 le32_to_cpu(htt_stats_buf->underrun));
	len += scnprintf(buf + len, buf_len - len, "hw_paused = %u\n",
			 le32_to_cpu(htt_stats_buf->hw_paused));
	len += scnprintf(buf + len, buf_len - len, "hw_flush = %u\n",
			 le32_to_cpu(htt_stats_buf->hw_flush));
	len += scnprintf(buf + len, buf_len - len, "hw_filt = %u\n",
			 le32_to_cpu(htt_stats_buf->hw_filt));
	len += scnprintf(buf + len, buf_len - len, "tx_abort = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_abort));
	len += scnprintf(buf + len, buf_len - len, "ppdu_ok = %u\n",
			 le32_to_cpu(htt_stats_buf->ppdu_ok));
	len += scnprintf(buf + len, buf_len - len, "mpdu_requeued = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_requed));
	len += scnprintf(buf + len, buf_len - len, "tx_xretry = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_xretry));
	len += scnprintf(buf + len, buf_len - len, "data_rc = %u\n",
			 le32_to_cpu(htt_stats_buf->data_rc));
	len += scnprintf(buf + len, buf_len - len, "mpdu_dropped_xretry = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_dropped_xretry));
	len += scnprintf(buf + len, buf_len - len, "illegal_rate_phy_err = %u\n",
			 le32_to_cpu(htt_stats_buf->illgl_rate_phy_err));
	len += scnprintf(buf + len, buf_len - len, "cont_xretry = %u\n",
			 le32_to_cpu(htt_stats_buf->cont_xretry));
	len += scnprintf(buf + len, buf_len - len, "tx_timeout = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_timeout));
	len += scnprintf(buf + len, buf_len - len, "tx_time_dur_data = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_time_dur_data));
	len += scnprintf(buf + len, buf_len - len, "pdev_resets = %u\n",
			 le32_to_cpu(htt_stats_buf->pdev_resets));
	len += scnprintf(buf + len, buf_len - len, "phy_underrun = %u\n",
			 le32_to_cpu(htt_stats_buf->phy_underrun));
	len += scnprintf(buf + len, buf_len - len, "txop_ovf = %u\n",
			 le32_to_cpu(htt_stats_buf->txop_ovf));
	len += scnprintf(buf + len, buf_len - len, "seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_posted));
	len += scnprintf(buf + len, buf_len - len, "seq_failed_queueing = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_failed_queueing));
	len += scnprintf(buf + len, buf_len - len, "seq_completed = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_completed));
	len += scnprintf(buf + len, buf_len - len, "seq_restarted = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_restarted));
	len += scnprintf(buf + len, buf_len - len, "seq_txop_repost_stop = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_txop_repost_stop));
	len += scnprintf(buf + len, buf_len - len, "next_seq_cancel = %u\n",
			 le32_to_cpu(htt_stats_buf->next_seq_cancel));
	len += scnprintf(buf + len, buf_len - len, "dl_mu_mimo_seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_seq_posted));
	len += scnprintf(buf + len, buf_len - len, "dl_mu_ofdma_seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_ofdma_seq_posted));
	len += scnprintf(buf + len, buf_len - len, "ul_mu_mimo_seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_mumimo_seq_posted));
	len += scnprintf(buf + len, buf_len - len, "ul_mu_ofdma_seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_seq_posted));
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_peer_blacklisted = %u\n",
			 le32_to_cpu(htt_stats_buf->num_mu_peer_blacklisted));
	len += scnprintf(buf + len, buf_len - len, "seq_qdepth_repost_stop = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_qdepth_repost_stop));
	len += scnprintf(buf + len, buf_len - len, "seq_min_msdu_repost_stop = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_min_msdu_repost_stop));
	len += scnprintf(buf + len, buf_len - len, "mu_seq_min_msdu_repost_stop = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_seq_min_msdu_repost_stop));
	len += scnprintf(buf + len, buf_len - len, "seq_switch_hw_paused = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_switch_hw_paused));
	len += scnprintf(buf + len, buf_len - len, "next_seq_posted_dsr = %u\n",
			 le32_to_cpu(htt_stats_buf->next_seq_posted_dsr));
	len += scnprintf(buf + len, buf_len - len, "seq_posted_isr = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_posted_isr));
	len += scnprintf(buf + len, buf_len - len, "seq_ctrl_cached = %u\n",
			 le32_to_cpu(htt_stats_buf->seq_ctrl_cached));
	len += scnprintf(buf + len, buf_len - len, "mpdu_count_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_count_tqm));
	len += scnprintf(buf + len, buf_len - len, "msdu_count_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->msdu_count_tqm));
	len += scnprintf(buf + len, buf_len - len, "mpdu_removed_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_removed_tqm));
	len += scnprintf(buf + len, buf_len - len, "msdu_removed_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->msdu_removed_tqm));
	len += scnprintf(buf + len, buf_len - len, "remove_mpdus_max_retries = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_mpdus_max_retries));
	len += scnprintf(buf + len, buf_len - len, "mpdus_sw_flush = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_sw_flush));
	len += scnprintf(buf + len, buf_len - len, "mpdus_hw_filter = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_hw_filter));
	len += scnprintf(buf + len, buf_len - len, "mpdus_truncated = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_truncated));
	len += scnprintf(buf + len, buf_len - len, "mpdus_ack_failed = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_ack_failed));
	len += scnprintf(buf + len, buf_len - len, "mpdus_expired = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_expired));
	len += scnprintf(buf + len, buf_len - len, "mpdus_seq_hw_retry = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdus_seq_hw_retry));
	len += scnprintf(buf + len, buf_len - len, "ack_tlv_proc = %u\n",
			 le32_to_cpu(htt_stats_buf->ack_tlv_proc));
	len += scnprintf(buf + len, buf_len - len, "coex_abort_mpdu_cnt_valid = %u\n",
			 le32_to_cpu(htt_stats_buf->coex_abort_mpdu_cnt_valid));
	len += scnprintf(buf + len, buf_len - len, "coex_abort_mpdu_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->coex_abort_mpdu_cnt));
	len += scnprintf(buf + len, buf_len - len, "num_total_ppdus_tried_ota = %u\n",
			 le32_to_cpu(htt_stats_buf->num_total_ppdus_tried_ota));
	len += scnprintf(buf + len, buf_len - len, "num_data_ppdus_tried_ota = %u\n",
			 le32_to_cpu(htt_stats_buf->num_data_ppdus_tried_ota));
	len += scnprintf(buf + len, buf_len - len, "local_ctrl_mgmt_enqued = %u\n",
			 le32_to_cpu(htt_stats_buf->local_ctrl_mgmt_enqued));
	len += scnprintf(buf + len, buf_len - len, "local_ctrl_mgmt_freed = %u\n",
			 le32_to_cpu(htt_stats_buf->local_ctrl_mgmt_freed));
	len += scnprintf(buf + len, buf_len - len, "local_data_enqued = %u\n",
			 le32_to_cpu(htt_stats_buf->local_data_enqued));
	len += scnprintf(buf + len, buf_len - len, "local_data_freed = %u\n",
			 le32_to_cpu(htt_stats_buf->local_data_freed));
	len += scnprintf(buf + len, buf_len - len, "mpdu_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_tried));
	len += scnprintf(buf + len, buf_len - len, "isr_wait_seq_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->isr_wait_seq_posted));
	len += scnprintf(buf + len, buf_len - len, "tx_active_dur_us_low = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_active_dur_us_low));
	len += scnprintf(buf + len, buf_len - len, "tx_active_dur_us_high = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_active_dur_us_high));
	len += scnprintf(buf + len, buf_len - len, "fes_offsets_err_cnt = %u\n\n",
			 le32_to_cpu(htt_stats_buf->fes_offsets_err_cnt));

	stats_req->buf_len = len;
}

static void
htt_print_tx_pdev_stats_urrn_tlv(const void *tag_buf,
				 u16 tag_len,
				 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_urrn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      HTT_TX_PDEV_MAX_URRN_STATS);

	len += scnprintf(buf + len, buf_len - len,
			"HTT_TX_PDEV_STATS_URRN_TLV:\n");

	len += print_array_to_buf(buf, len, "urrn_stats", htt_stats_buf->urrn_stats,
				  num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
htt_print_tx_pdev_stats_flush_tlv(const void *tag_buf,
				  u16 tag_len,
				  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_flush_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_TX_PDEV_MAX_FLUSH_REASON_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_FLUSH_TLV:\n");

	len += print_array_to_buf(buf, len, "flush_errs", htt_stats_buf->flush_errs,
				  num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
htt_print_tx_pdev_stats_sifs_tlv(const void *tag_buf,
				 u16 tag_len,
				 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_sifs_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_TX_PDEV_MAX_SIFS_BURST_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_SIFS_TLV:\n");

	len += print_array_to_buf(buf, len, "sifs_status", htt_stats_buf->sifs_status,
				  num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
htt_print_tx_pdev_mu_ppdu_dist_stats_tlv(const void *tag_buf, u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_mu_ppdu_dist_stats_tlv *htt_stats_buf = tag_buf;
	char *mode;
	u8 j, hw_mode, i, str_buf_len;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 stats_value;
	u8 max_ppdu = ATH12K_HTT_STATS_MAX_NUM_MU_PPDU_PER_BURST;
	u8 max_sched = ATH12K_HTT_STATS_MAX_NUM_SCHED_STATUS;
	char str_buf[ATH12K_HTT_MAX_STRING_LEN];

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	hw_mode = le32_to_cpu(htt_stats_buf->hw_mode);

	switch (hw_mode) {
	case ATH12K_HTT_STATS_HWMODE_AC:
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_TX_PDEV_AC_MU_PPDU_DISTRIBUTION_STATS:\n");
		mode = "ac";
		break;
	case ATH12K_HTT_STATS_HWMODE_AX:
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_TX_PDEV_AX_MU_PPDU_DISTRIBUTION_STATS:\n");
		mode = "ax";
		break;
	case ATH12K_HTT_STATS_HWMODE_BE:
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_TX_PDEV_BE_MU_PPDU_DISTRIBUTION_STATS:\n");
		mode = "be";
		break;
	default:
		return;
	}

	for (i = 0; i < ATH12K_HTT_STATS_NUM_NR_BINS ; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "%s_mu_mimo_num_seq_posted_nr%u = %u\n", mode,
				 ((i + 1) * 4), htt_stats_buf->num_seq_posted[i]);
		str_buf_len = 0;
		memset(str_buf, 0x0, sizeof(str_buf));
		for (j = 0; j < ATH12K_HTT_STATS_MAX_NUM_MU_PPDU_PER_BURST ; j++) {
			stats_value = le32_to_cpu(htt_stats_buf->num_ppdu_posted_per_burst
						  [i * max_ppdu + j]);
			str_buf_len += scnprintf(&str_buf[str_buf_len],
						ATH12K_HTT_MAX_STRING_LEN - str_buf_len,
						" %u:%u,", j, stats_value);
		}
		/* To overwrite the last trailing comma */
		str_buf[str_buf_len - 1] = '\0';
		len += scnprintf(buf + len, buf_len - len,
				 "%s_mu_mimo_num_ppdu_posted_per_burst_nr%u = %s\n",
				 mode, ((i + 1) * 4), str_buf);
		str_buf_len = 0;
		memset(str_buf, 0x0, sizeof(str_buf));
		for (j = 0; j < ATH12K_HTT_STATS_MAX_NUM_MU_PPDU_PER_BURST ; j++) {
			stats_value = le32_to_cpu(htt_stats_buf->num_ppdu_cmpl_per_burst
						  [i * max_ppdu + j]);
			str_buf_len += scnprintf(&str_buf[str_buf_len],
						ATH12K_HTT_MAX_STRING_LEN - str_buf_len,
						" %u:%u,", j, stats_value);
		}
		/* To overwrite the last trailing comma */
		str_buf[str_buf_len - 1] = '\0';
		len += scnprintf(buf + len, buf_len - len,
				 "%s_mu_mimo_num_ppdu_completed_per_burst_nr%u = %s\n",
				 mode, ((i + 1) * 4), str_buf);
		str_buf_len = 0;
		memset(str_buf, 0x0, sizeof(str_buf));
		for (j = 0; j < ATH12K_HTT_STATS_MAX_NUM_SCHED_STATUS ; j++) {
			stats_value = le32_to_cpu(htt_stats_buf->num_seq_term_status
						  [i * max_sched + j]);
			str_buf_len += scnprintf(&str_buf[str_buf_len],
						ATH12K_HTT_MAX_STRING_LEN - str_buf_len,
						" %u:%u,", j, stats_value);
		}
		/* To overwrite the last trailing comma */
		str_buf[str_buf_len - 1] = '\0';
		len += scnprintf(buf + len, buf_len - len,
				 "%s_mu_mimo_num_seq_term_status_nr%u = %s\n\n",
				 mode, ((i + 1) * 4), str_buf);
	}

	stats_req->buf_len = len;
}

static void
htt_print_tx_pdev_stats_sifs_hist_tlv(const void *tag_buf,
				      u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_sifs_hist_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_TX_PDEV_MAX_SIFS_BURST_HIST_STATS);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_SIFS_HIST_TLV:\n");

	len += print_array_to_buf(buf, len, "sifs_hist_status",
				  htt_stats_buf->sifs_hist_status, num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
htt_print_pdev_ctrl_path_tx_stats_tlv(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_ctrl_path_tx_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_CTRL_PATH_TX_STATS:\n");
	len += print_array_to_buf(buf, len, "fw_tx_mgmt_subtype",
				 htt_stats_buf->fw_tx_mgmt_subtype,
				 ATH12K_HTT_STATS_SUBTYPE_MAX, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_stats_tx_sched_cmn_tlv(const void *tag_buf,
					u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_stats_tx_sched_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_TX_SCHED_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "current_timestamp = %u\n\n",
			 le32_to_cpu(htt_stats_buf->current_timestamp));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_pdev_stats_sched_per_txq_tlv(const void *tag_buf,
						 u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_stats_sched_per_txq_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_STATS_SCHED_PER_TXQ_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			u32_get_bits(mac_id_word,
				     ATH12K_HTT_TX_PDEV_STATS_SCHED_PER_TXQ_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "txq_id = %u\n",
			 u32_get_bits(mac_id_word,
				      ATH12K_HTT_TX_PDEV_STATS_SCHED_PER_TXQ_ID));
	len += scnprintf(buf + len, buf_len - len, "sched_policy = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_policy));
	len += scnprintf(buf + len, buf_len - len,
			 "last_sched_cmd_posted_timestamp = %u\n",
			 le32_to_cpu(htt_stats_buf->last_sched_cmd_posted_timestamp));
	len += scnprintf(buf + len, buf_len - len,
			 "last_sched_cmd_compl_timestamp = %u\n",
			 le32_to_cpu(htt_stats_buf->last_sched_cmd_compl_timestamp));
	len += scnprintf(buf + len, buf_len - len, "sched_2_tac_lwm_count = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_2_tac_lwm_count));
	len += scnprintf(buf + len, buf_len - len, "sched_2_tac_ring_full = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_2_tac_ring_full));
	len += scnprintf(buf + len, buf_len - len, "sched_cmd_post_failure = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_cmd_post_failure));
	len += scnprintf(buf + len, buf_len - len, "num_active_tids = %u\n",
			 le32_to_cpu(htt_stats_buf->num_active_tids));
	len += scnprintf(buf + len, buf_len - len, "num_ps_schedules = %u\n",
			 le32_to_cpu(htt_stats_buf->num_ps_schedules));
	len += scnprintf(buf + len, buf_len - len, "sched_cmds_pending = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_cmds_pending));
	len += scnprintf(buf + len, buf_len - len, "num_tid_register = %u\n",
			 le32_to_cpu(htt_stats_buf->num_tid_register));
	len += scnprintf(buf + len, buf_len - len, "num_tid_unregister = %u\n",
			 le32_to_cpu(htt_stats_buf->num_tid_unregister));
	len += scnprintf(buf + len, buf_len - len, "num_qstats_queried = %u\n",
			 le32_to_cpu(htt_stats_buf->num_qstats_queried));
	len += scnprintf(buf + len, buf_len - len, "qstats_update_pending = %u\n",
			 le32_to_cpu(htt_stats_buf->qstats_update_pending));
	len += scnprintf(buf + len, buf_len - len, "last_qstats_query_timestamp = %u\n",
			 le32_to_cpu(htt_stats_buf->last_qstats_query_timestamp));
	len += scnprintf(buf + len, buf_len - len, "num_tqm_cmdq_full = %u\n",
			 le32_to_cpu(htt_stats_buf->num_tqm_cmdq_full));
	len += scnprintf(buf + len, buf_len - len, "num_de_sched_algo_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->num_de_sched_algo_trigger));
	len += scnprintf(buf + len, buf_len - len, "num_rt_sched_algo_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->num_rt_sched_algo_trigger));
	len += scnprintf(buf + len, buf_len - len, "num_tqm_sched_algo_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->num_tqm_sched_algo_trigger));
	len += scnprintf(buf + len, buf_len - len, "notify_sched = %u\n",
			 le32_to_cpu(htt_stats_buf->notify_sched));
	len += scnprintf(buf + len, buf_len - len, "dur_based_sendn_term = %u\n",
			 le32_to_cpu(htt_stats_buf->dur_based_sendn_term));
	len += scnprintf(buf + len, buf_len - len, "su_notify2_sched = %u\n",
			 le32_to_cpu(htt_stats_buf->su_notify2_sched));
	len += scnprintf(buf + len, buf_len - len, "su_optimal_queued_msdus_sched = %u\n",
			 le32_to_cpu(htt_stats_buf->su_optimal_queued_msdus_sched));
	len += scnprintf(buf + len, buf_len - len, "su_delay_timeout_sched = %u\n",
			 le32_to_cpu(htt_stats_buf->su_delay_timeout_sched));
	len += scnprintf(buf + len, buf_len - len, "su_min_txtime_sched_delay = %u\n",
			 le32_to_cpu(htt_stats_buf->su_min_txtime_sched_delay));
	len += scnprintf(buf + len, buf_len - len, "su_no_delay = %u\n",
			 le32_to_cpu(htt_stats_buf->su_no_delay));
	len += scnprintf(buf + len, buf_len - len, "num_supercycles = %u\n",
			 le32_to_cpu(htt_stats_buf->num_supercycles));
	len += scnprintf(buf + len, buf_len - len, "num_subcycles_with_sort = %u\n",
			 le32_to_cpu(htt_stats_buf->num_subcycles_with_sort));
	len += scnprintf(buf + len, buf_len - len, "num_subcycles_no_sort = %u\n\n",
			 le32_to_cpu(htt_stats_buf->num_subcycles_no_sort));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sched_txq_cmd_posted_tlv(const void *tag_buf,
					  u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sched_txq_cmd_posted_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elements = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_POSTED_TLV:\n");
	len += print_array_to_buf(buf, len, "sched_cmd_posted",
				  htt_stats_buf->sched_cmd_posted, num_elements, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sched_txq_cmd_reaped_tlv(const void *tag_buf,
					  u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sched_txq_cmd_reaped_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elements = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_SCHED_TXQ_CMD_REAPED_TLV:\n");
	len += print_array_to_buf(buf, len, "sched_cmd_reaped",
				  htt_stats_buf->sched_cmd_reaped, num_elements, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sched_txq_sched_order_su_tlv(const void *tag_buf,
					      u16 tag_len,
					      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sched_txq_sched_order_su_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 sched_order_su_num_entries = min_t(u32, (tag_len >> 2),
					       ATH12K_HTT_TX_PDEV_NUM_SCHED_ORDER_LOG);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_SCHED_TXQ_SCHED_ORDER_SU_TLV:\n");
	len += print_array_to_buf(buf, len, "sched_order_su",
				  htt_stats_buf->sched_order_su,
				  sched_order_su_num_entries, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sched_txq_sched_ineligibility_tlv(const void *tag_buf,
						   u16 tag_len,
						   struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sched_txq_sched_ineligibility_tlv *htt_stats_buf =
		     tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 sched_ineligibility_num_entries = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_SCHED_TXQ_SCHED_INELIGIBILITY:\n");
	len += print_array_to_buf(buf, len, "sched_ineligibility",
				  htt_stats_buf->sched_ineligibility,
				  sched_ineligibility_num_entries, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sched_txq_supercycle_trigger_tlv(const void *tag_buf,
						  u16 tag_len,
						  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sched_txq_supercycle_triggers_tlv *htt_stats_buf =
		     tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_SCHED_SUPERCYCLE_TRIGGER_MAX);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_SCHED_TXQ_SUPERCYCLE_TRIGGER:\n");
	len += print_array_to_buf(buf, len, "supercycle_triggers",
				  htt_stats_buf->supercycle_triggers, num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_hw_stats_pdev_errs_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_hw_stats_pdev_errs_tlv *htt_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_buf))
		return;

	mac_id_word = le32_to_cpu(htt_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_PDEV_ERRS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "tx_abort = %u\n",
			 le32_to_cpu(htt_buf->tx_abort));
	len += scnprintf(buf + len, buf_len - len, "tx_abort_fail_count = %u\n",
			 le32_to_cpu(htt_buf->tx_abort_fail_count));
	len += scnprintf(buf + len, buf_len - len, "rx_abort = %u\n",
			 le32_to_cpu(htt_buf->rx_abort));
	len += scnprintf(buf + len, buf_len - len, "rx_abort_fail_count = %u\n",
			 le32_to_cpu(htt_buf->rx_abort_fail_count));
	len += scnprintf(buf + len, buf_len - len, "rx_flush_cnt = %u\n",
			 le32_to_cpu(htt_buf->rx_flush_cnt));
	len += scnprintf(buf + len, buf_len - len, "warm_reset = %u\n",
			 le32_to_cpu(htt_buf->warm_reset));
	len += scnprintf(buf + len, buf_len - len, "cold_reset = %u\n",
			 le32_to_cpu(htt_buf->cold_reset));
	len += scnprintf(buf + len, buf_len - len, "mac_cold_reset_restore_cal = %u\n",
			 le32_to_cpu(htt_buf->mac_cold_reset_restore_cal));
	len += scnprintf(buf + len, buf_len - len, "mac_cold_reset = %u\n",
			 le32_to_cpu(htt_buf->mac_cold_reset));
	len += scnprintf(buf + len, buf_len - len, "mac_warm_reset = %u\n",
			 le32_to_cpu(htt_buf->mac_warm_reset));
	len += scnprintf(buf + len, buf_len - len, "mac_only_reset = %u\n",
			 le32_to_cpu(htt_buf->mac_only_reset));
	len += scnprintf(buf + len, buf_len - len, "phy_warm_reset = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset));
	len += scnprintf(buf + len, buf_len - len, "phy_warm_reset_ucode_trig = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_ucode_trig));
	len += scnprintf(buf + len, buf_len - len, "mac_warm_reset_restore_cal = %u\n",
			 le32_to_cpu(htt_buf->mac_warm_reset_restore_cal));
	len += scnprintf(buf + len, buf_len - len, "mac_sfm_reset = %u\n",
			 le32_to_cpu(htt_buf->mac_sfm_reset));
	len += scnprintf(buf + len, buf_len - len, "phy_warm_reset_m3_ssr = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_m3_ssr));
	len += scnprintf(buf + len, buf_len - len, "fw_rx_rings_reset = %u\n",
			 le32_to_cpu(htt_buf->fw_rx_rings_reset));
	len += scnprintf(buf + len, buf_len - len, "tx_flush = %u\n",
			 le32_to_cpu(htt_buf->tx_flush));
	len += scnprintf(buf + len, buf_len - len, "tx_glb_reset = %u\n",
			 le32_to_cpu(htt_buf->tx_glb_reset));
	len += scnprintf(buf + len, buf_len - len, "tx_txq_reset = %u\n",
			 le32_to_cpu(htt_buf->tx_txq_reset));
	len += scnprintf(buf + len, buf_len - len, "rx_timeout_reset = %u\n\n",
			 le32_to_cpu(htt_buf->rx_timeout_reset));

	len += scnprintf(buf + len, buf_len - len, "PDEV_PHY_WARM_RESET_REASONS:\n");
	len += scnprintf(buf + len, buf_len - len, "phy_warm_reset_reason_phy_m3 = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_phy_m3));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_tx_hw_stuck = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_tx_hw_stuck));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_num_cca_rx_frame_stuck = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_num_rx_frame_stuck));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_wal_rx_recovery_rst_rx_busy = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_wal_rx_rec_rx_busy));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_wal_rx_recovery_rst_mac_hang = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_wal_rx_rec_mac_hng));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_mac_reset_converted_phy_reset = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_mac_conv_phy_reset));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_tx_lifetime_expiry_cca_stuck = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_tx_exp_cca_stuck));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_tx_consecutive_flush9_war = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_tx_consec_flsh_war));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_tx_hwsch_reset_war = %u\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_tx_hwsch_reset_war));
	len += scnprintf(buf + len, buf_len - len,
			 "phy_warm_reset_reason_hwsch_wdog_or_cca_wdog_war = %u\n\n",
			 le32_to_cpu(htt_buf->phy_warm_reset_reason_hwsch_cca_wdog_war));

	len += scnprintf(buf + len, buf_len - len, "WAL_RX_RECOVERY_STATS:\n");
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_mac_hang_count = %u\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_mac_hang_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_known_sig_count = %u\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_known_sig_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_no_rx_count = %u\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_no_rx_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_no_rx_consecutive_count = %u\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_no_rx_consec_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_rx_busy_count = %u\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_rx_busy_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "wal_rx_recovery_rst_phy_mac_hang_count = %u\n\n",
			 le32_to_cpu(htt_buf->wal_rx_recovery_rst_phy_mac_hang_cnt));

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_DEST_DRAIN_STATS:\n");
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rx_descs_leak_prevention_done = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rx_descs_leak_prevented));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rx_descs_saved_cnt = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rx_descs_saved_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rxdma2reo_leak_detected = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rxdma2reo_leak_detected));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rxdma2fw_leak_detected = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rxdma2fw_leak_detected));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rxdma2wbm_leak_detected = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rxdma2wbm_leak_detected));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rxdma1_2sw_leak_detected = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rxdma1_2sw_leak_detected));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_rx_drain_ok_mac_idle = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_rx_drain_ok_mac_idle));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_ok_mac_not_idle = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_ok_mac_not_idle));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_prerequisite_invld = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_prerequisite_invld));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_skip_for_non_lmac_reset = %u\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_skip_non_lmac_reset));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_dest_drain_hw_fifo_not_empty_post_drain_wait = %u\n\n",
			 le32_to_cpu(htt_buf->rx_dest_drain_hw_fifo_notempty_post_wait));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_hw_stats_intr_misc_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_hw_stats_intr_misc_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_INTR_MISC_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "hw_intr_name = %s\n",
			 htt_stats_buf->hw_intr_name);
	len += scnprintf(buf + len, buf_len - len, "mask = %u\n",
			 le32_to_cpu(htt_stats_buf->mask));
	len += scnprintf(buf + len, buf_len - len, "count = %u\n\n",
			 le32_to_cpu(htt_stats_buf->count));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_hw_stats_whal_tx_tlv(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_hw_stats_whal_tx_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_STATS_WHAL_TX_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "last_unpause_ppdu_id = %u\n",
			 le32_to_cpu(htt_stats_buf->last_unpause_ppdu_id));
	len += scnprintf(buf + len, buf_len - len, "hwsch_unpause_wait_tqm_write = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_unpause_wait_tqm_write));
	len += scnprintf(buf + len, buf_len - len, "hwsch_dummy_tlv_skipped = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_dummy_tlv_skipped));
	len += scnprintf(buf + len, buf_len - len,
			 "hwsch_misaligned_offset_received = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_misaligned_offset_received));
	len += scnprintf(buf + len, buf_len - len, "hwsch_reset_count = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_reset_count));
	len += scnprintf(buf + len, buf_len - len, "hwsch_dev_reset_war = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_dev_reset_war));
	len += scnprintf(buf + len, buf_len - len, "hwsch_delayed_pause = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_delayed_pause));
	len += scnprintf(buf + len, buf_len - len, "hwsch_long_delayed_pause = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_long_delayed_pause));
	len += scnprintf(buf + len, buf_len - len, "sch_rx_ppdu_no_response = %u\n",
			 le32_to_cpu(htt_stats_buf->sch_rx_ppdu_no_response));
	len += scnprintf(buf + len, buf_len - len, "sch_selfgen_response = %u\n",
			 le32_to_cpu(htt_stats_buf->sch_selfgen_response));
	len += scnprintf(buf + len, buf_len - len, "sch_rx_sifs_resp_trigger= %u\n\n",
			 le32_to_cpu(htt_stats_buf->sch_rx_sifs_resp_trigger));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_hw_war_tlv(const void *tag_buf, u16 tag_len,
			    struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_hw_war_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 fixed_len, array_len;
	u8 i, array_words;
	u32 mac_id;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id = __le32_to_cpu(htt_stats_buf->mac_id__word);
	fixed_len = sizeof(*htt_stats_buf);
	array_len = tag_len - fixed_len;
	array_words = array_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_HW_WAR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id, ATH12K_HTT_STATS_MAC_ID));

	for (i = 0; i < array_words; i++) {
		len += scnprintf(buf + len, buf_len - len, "hw_war %u = %u\n\n",
				 i, le32_to_cpu(htt_stats_buf->hw_wars[i]));
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_cmn_stats_tlv(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "max_cmdq_id = %u\n",
			 le32_to_cpu(htt_stats_buf->max_cmdq_id));
	len += scnprintf(buf + len, buf_len - len, "list_mpdu_cnt_hist_intvl = %u\n",
			 le32_to_cpu(htt_stats_buf->list_mpdu_cnt_hist_intvl));
	len += scnprintf(buf + len, buf_len - len, "add_msdu = %u\n",
			 le32_to_cpu(htt_stats_buf->add_msdu));
	len += scnprintf(buf + len, buf_len - len, "q_empty = %u\n",
			 le32_to_cpu(htt_stats_buf->q_empty));
	len += scnprintf(buf + len, buf_len - len, "q_not_empty = %u\n",
			 le32_to_cpu(htt_stats_buf->q_not_empty));
	len += scnprintf(buf + len, buf_len - len, "drop_notification = %u\n",
			 le32_to_cpu(htt_stats_buf->drop_notification));
	len += scnprintf(buf + len, buf_len - len, "desc_threshold = %u\n",
			 le32_to_cpu(htt_stats_buf->desc_threshold));
	len += scnprintf(buf + len, buf_len - len, "hwsch_tqm_invalid_status = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_tqm_invalid_status));
	len += scnprintf(buf + len, buf_len - len, "missed_tqm_gen_mpdus = %u\n",
			 le32_to_cpu(htt_stats_buf->missed_tqm_gen_mpdus));
	len += scnprintf(buf + len, buf_len - len,
			 "total_msduq_timestamp_updates = %u\n",
			 le32_to_cpu(htt_stats_buf->msduq_timestamp_updates));
	len += scnprintf(buf + len, buf_len - len,
			 "total_msduq_timestamp_updates_by_get_mpdu_head_info_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->msduq_updates_mpdu_head_info_cmd));
	len += scnprintf(buf + len, buf_len - len,
			 "total_msduq_timestamp_updates_by_emp_to_nonemp_status = %u\n",
			 le32_to_cpu(htt_stats_buf->msduq_updates_emp_to_nonemp_status));
	len += scnprintf(buf + len, buf_len - len,
			 "total_get_mpdu_head_info_cmds_by_sched_algo_la_query = %u\n",
			 le32_to_cpu(htt_stats_buf->get_mpdu_head_info_cmds_by_query));
	len += scnprintf(buf + len, buf_len - len,
			 "total_get_mpdu_head_info_cmds_by_tac = %u\n",
			 le32_to_cpu(htt_stats_buf->get_mpdu_head_info_cmds_by_tac));
	len += scnprintf(buf + len, buf_len - len,
			 "total_gen_mpdu_cmds_by_sched_algo_la_query = %u\n",
			 le32_to_cpu(htt_stats_buf->gen_mpdu_cmds_by_query));
	len += scnprintf(buf + len, buf_len - len, "active_tqm_tids = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_active_tids));
	len += scnprintf(buf + len, buf_len - len, "inactive_tqm_tids = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_inactive_tids));
	len += scnprintf(buf + len, buf_len - len, "tqm_active_msduq_flows = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_active_msduq_flows));
	len += scnprintf(buf + len, buf_len - len, "hi_prio_q_not_empty = %u\n\n",
			 le32_to_cpu(htt_stats_buf->high_prio_q_not_empty));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_error_stats_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_error_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_ERROR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "q_empty_failure = %u\n",
			 le32_to_cpu(htt_stats_buf->q_empty_failure));
	len += scnprintf(buf + len, buf_len - len, "q_not_empty_failure = %u\n",
			 le32_to_cpu(htt_stats_buf->q_not_empty_failure));
	len += scnprintf(buf + len, buf_len - len, "add_msdu_failure = %u\n\n",
			 le32_to_cpu(htt_stats_buf->add_msdu_failure));

	len += scnprintf(buf + len, buf_len - len, "TQM_ERROR_RESET_STATS:\n");
	len += scnprintf(buf + len, buf_len - len, "tqm_cache_ctl_err = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_cache_ctl_err));
	len += scnprintf(buf + len, buf_len - len, "tqm_soft_reset = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_soft_reset));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_total_num_in_use_link_descs = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_in_use_link_descs));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_worst_case_num_lost_link_descs = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_lost_link_descs));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_worst_case_num_lost_host_tx_bufs_count = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_lost_host_tx_buf_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_num_in_use_link_descs_internal_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_in_use_internal_tqm));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_num_in_use_link_descs_wbm_idle_link_ring = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_in_use_idle_link_rng));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_time_to_tqm_hang_delta_ms = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_time_to_tqm_hang_delta_ms));
	len += scnprintf(buf + len, buf_len - len, "tqm_reset_recovery_time_ms = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_recovery_time_ms));
	len += scnprintf(buf + len, buf_len - len, "tqm_reset_num_peers_hdl = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_num_peers_hdl));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_cumm_dirty_hw_mpduq_proc_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_cumm_dirty_hw_mpduq_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_cumm_dirty_hw_msduq_proc = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_cumm_dirty_hw_msduq_proc));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_flush_cache_cmd_su_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_flush_cache_cmd_su_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_flush_cache_cmd_other_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_flush_cache_cmd_other_cnt));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_flush_cache_cmd_trig_type = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_flush_cache_cmd_trig_type));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_flush_cache_cmd_trig_cfg = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_flush_cache_cmd_trig_cfg));
	len += scnprintf(buf + len, buf_len - len,
			 "tqm_reset_flush_cache_cmd_skip_cmd_status_null = %u\n\n",
			 le32_to_cpu(htt_stats_buf->tqm_reset_flush_cmd_skp_status_null));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_gen_mpdu_stats_tlv(const void *tag_buf, u16 tag_len,
					   struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_gen_mpdu_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elements = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_GEN_MPDU_STATS_TLV:\n");
	len += print_array_to_buf(buf, len, "gen_mpdu_end_reason",
				  htt_stats_buf->gen_mpdu_end_reason, num_elements,
				  "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_list_mpdu_stats_tlv(const void *tag_buf, u16 tag_len,
					    struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_list_mpdu_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_TX_TQM_MAX_LIST_MPDU_END_REASON);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_LIST_MPDU_STATS_TLV:\n");
	len += print_array_to_buf(buf, len, "list_mpdu_end_reason",
				  htt_stats_buf->list_mpdu_end_reason, num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_list_mpdu_cnt_tlv(const void *tag_buf, u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_list_mpdu_cnt_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = min_t(u16, (tag_len >> 2),
			      ATH12K_HTT_TX_TQM_MAX_LIST_MPDU_CNT_HISTOGRAM_BINS);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_LIST_MPDU_CNT_TLV_V:\n");
	len += print_array_to_buf(buf, len, "list_mpdu_cnt_hist",
				  htt_stats_buf->list_mpdu_cnt_hist, num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_tqm_pdev_stats_tlv(const void *tag_buf, u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_tqm_pdev_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_TQM_PDEV_STATS_TLV_V:\n");
	len += scnprintf(buf + len, buf_len - len, "msdu_count = %u\n",
			 le32_to_cpu(htt_stats_buf->msdu_count));
	len += scnprintf(buf + len, buf_len - len, "mpdu_count = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_count));
	len += scnprintf(buf + len, buf_len - len, "remove_msdu = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_msdu));
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_mpdu));
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_ttl = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_msdu_ttl));
	len += scnprintf(buf + len, buf_len - len, "send_bar = %u\n",
			 le32_to_cpu(htt_stats_buf->send_bar));
	len += scnprintf(buf + len, buf_len - len, "bar_sync = %u\n",
			 le32_to_cpu(htt_stats_buf->bar_sync));
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu = %u\n",
			 le32_to_cpu(htt_stats_buf->notify_mpdu));
	len += scnprintf(buf + len, buf_len - len, "sync_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->sync_cmd));
	len += scnprintf(buf + len, buf_len - len, "write_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->write_cmd));
	len += scnprintf(buf + len, buf_len - len, "hwsch_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->hwsch_trigger));
	len += scnprintf(buf + len, buf_len - len, "ack_tlv_proc = %u\n",
			 le32_to_cpu(htt_stats_buf->ack_tlv_proc));
	len += scnprintf(buf + len, buf_len - len, "gen_mpdu_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->gen_mpdu_cmd));
	len += scnprintf(buf + len, buf_len - len, "gen_list_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->gen_list_cmd));
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_mpdu_cmd));
	len += scnprintf(buf + len, buf_len - len, "remove_mpdu_tried_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_mpdu_tried_cmd));
	len += scnprintf(buf + len, buf_len - len, "mpdu_queue_stats_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_queue_stats_cmd));
	len += scnprintf(buf + len, buf_len - len, "mpdu_head_info_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->mpdu_head_info_cmd));
	len += scnprintf(buf + len, buf_len - len, "msdu_flow_stats_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->msdu_flow_stats_cmd));
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_msdu_cmd));
	len += scnprintf(buf + len, buf_len - len, "remove_msdu_ttl_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->remove_msdu_ttl_cmd));
	len += scnprintf(buf + len, buf_len - len, "flush_cache_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->flush_cache_cmd));
	len += scnprintf(buf + len, buf_len - len, "update_mpduq_cmd = %u\n",
			 le32_to_cpu(htt_stats_buf->update_mpduq_cmd));
	len += scnprintf(buf + len, buf_len - len, "enqueue = %u\n",
			 le32_to_cpu(htt_stats_buf->enqueue));
	len += scnprintf(buf + len, buf_len - len, "enqueue_notify = %u\n",
			 le32_to_cpu(htt_stats_buf->enqueue_notify));
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu_at_head = %u\n",
			 le32_to_cpu(htt_stats_buf->notify_mpdu_at_head));
	len += scnprintf(buf + len, buf_len - len, "notify_mpdu_state_valid = %u\n",
			 le32_to_cpu(htt_stats_buf->notify_mpdu_state_valid));
	len += scnprintf(buf + len, buf_len - len, "sched_udp_notify1 = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_udp_notify1));
	len += scnprintf(buf + len, buf_len - len, "sched_udp_notify2 = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_udp_notify2));
	len += scnprintf(buf + len, buf_len - len, "sched_nonudp_notify1 = %u\n",
			 le32_to_cpu(htt_stats_buf->sched_nonudp_notify1));
	len += scnprintf(buf + len, buf_len - len, "sched_nonudp_notify2 = %u\n\n",
			 le32_to_cpu(htt_stats_buf->sched_nonudp_notify2));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_cmn_stats_tlv(const void *tag_buf, u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "tcl2fw_entry_count = %u\n",
			 le32_to_cpu(htt_stats_buf->tcl2fw_entry_count));
	len += scnprintf(buf + len, buf_len - len, "not_to_fw = %u\n",
			 le32_to_cpu(htt_stats_buf->not_to_fw));
	len += scnprintf(buf + len, buf_len - len, "invalid_pdev_vdev_peer = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_pdev_vdev_peer));
	len += scnprintf(buf + len, buf_len - len, "tcl_res_invalid_addrx = %u\n",
			 le32_to_cpu(htt_stats_buf->tcl_res_invalid_addrx));
	len += scnprintf(buf + len, buf_len - len, "wbm2fw_entry_count = %u\n",
			 le32_to_cpu(htt_stats_buf->wbm2fw_entry_count));
	len += scnprintf(buf + len, buf_len - len, "invalid_pdev = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_pdev));
	len += scnprintf(buf + len, buf_len - len, "tcl_res_addrx_timeout = %u\n",
			 le32_to_cpu(htt_stats_buf->tcl_res_addrx_timeout));
	len += scnprintf(buf + len, buf_len - len, "invalid_vdev = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_vdev));
	len += scnprintf(buf + len, buf_len - len, "invalid_tcl_exp_frame_desc = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_tcl_exp_frame_desc));
	len += scnprintf(buf + len, buf_len - len, "vdev_id_mismatch_count = %u\n\n",
			 le32_to_cpu(htt_stats_buf->vdev_id_mismatch_cnt));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_eapol_packets_stats_tlv(const void *tag_buf, u16 tag_len,
					       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_eapol_packets_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_EAPOL_PACKETS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "m1_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->m1_packets));
	len += scnprintf(buf + len, buf_len - len, "m2_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->m2_packets));
	len += scnprintf(buf + len, buf_len - len, "m3_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->m3_packets));
	len += scnprintf(buf + len, buf_len - len, "m4_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->m4_packets));
	len += scnprintf(buf + len, buf_len - len, "g1_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->g1_packets));
	len += scnprintf(buf + len, buf_len - len, "g2_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->g2_packets));
	len += scnprintf(buf + len, buf_len - len, "rc4_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->rc4_packets));
	len += scnprintf(buf + len, buf_len - len, "eap_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->eap_packets));
	len += scnprintf(buf + len, buf_len - len, "eapol_start_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->eapol_start_packets));
	len += scnprintf(buf + len, buf_len - len, "eapol_logoff_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->eapol_logoff_packets));
	len += scnprintf(buf + len, buf_len - len, "eapol_encap_asf_packets = %u\n\n",
			 le32_to_cpu(htt_stats_buf->eapol_encap_asf_packets));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_classify_stats_tlv(const void *tag_buf, u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_classify_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_CLASSIFY_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "arp_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->arp_packets));
	len += scnprintf(buf + len, buf_len - len, "igmp_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->igmp_packets));
	len += scnprintf(buf + len, buf_len - len, "dhcp_packets = %u\n",
			 le32_to_cpu(htt_stats_buf->dhcp_packets));
	len += scnprintf(buf + len, buf_len - len, "host_inspected = %u\n",
			 le32_to_cpu(htt_stats_buf->host_inspected));
	len += scnprintf(buf + len, buf_len - len, "htt_included = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_included));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_mcs = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_mcs));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_nss = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_nss));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_preamble_type = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_preamble_type));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_chainmask = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_chainmask));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_guard_interval = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_guard_interval));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_retries = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_retries));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_bw_info = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_bw_info));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_power = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_power));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_key_flags = 0x%x\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_key_flags));
	len += scnprintf(buf + len, buf_len - len, "htt_valid_no_encryption = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_valid_no_encryption));
	len += scnprintf(buf + len, buf_len - len, "fse_entry_count = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_entry_count));
	len += scnprintf(buf + len, buf_len - len, "fse_priority_be = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_priority_be));
	len += scnprintf(buf + len, buf_len - len, "fse_priority_high = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_priority_high));
	len += scnprintf(buf + len, buf_len - len, "fse_priority_low = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_priority_low));
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_be = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_traffic_ptrn_be));
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_over_sub = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_traffic_ptrn_over_sub));
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_bursty = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_traffic_ptrn_bursty));
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_interactive = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_traffic_ptrn_interactive));
	len += scnprintf(buf + len, buf_len - len, "fse_traffic_ptrn_periodic = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_traffic_ptrn_periodic));
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_alloc = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_hwqueue_alloc));
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_created = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_hwqueue_created));
	len += scnprintf(buf + len, buf_len - len, "fse_hwqueue_send_to_host = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_hwqueue_send_to_host));
	len += scnprintf(buf + len, buf_len - len, "mcast_entry = %u\n",
			 le32_to_cpu(htt_stats_buf->mcast_entry));
	len += scnprintf(buf + len, buf_len - len, "bcast_entry = %u\n",
			 le32_to_cpu(htt_stats_buf->bcast_entry));
	len += scnprintf(buf + len, buf_len - len, "htt_update_peer_cache = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_update_peer_cache));
	len += scnprintf(buf + len, buf_len - len, "htt_learning_frame = %u\n",
			 le32_to_cpu(htt_stats_buf->htt_learning_frame));
	len += scnprintf(buf + len, buf_len - len, "fse_invalid_peer = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_invalid_peer));
	len += scnprintf(buf + len, buf_len - len, "mec_notify = %u\n\n",
			 le32_to_cpu(htt_stats_buf->mec_notify));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_classify_failed_stats_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_classify_failed_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_CLASSIFY_FAILED_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ap_bss_peer_not_found = %u\n",
			 le32_to_cpu(htt_stats_buf->ap_bss_peer_not_found));
	len += scnprintf(buf + len, buf_len - len, "ap_bcast_mcast_no_peer = %u\n",
			 le32_to_cpu(htt_stats_buf->ap_bcast_mcast_no_peer));
	len += scnprintf(buf + len, buf_len - len, "sta_delete_in_progress = %u\n",
			 le32_to_cpu(htt_stats_buf->sta_delete_in_progress));
	len += scnprintf(buf + len, buf_len - len, "ibss_no_bss_peer = %u\n",
			 le32_to_cpu(htt_stats_buf->ibss_no_bss_peer));
	len += scnprintf(buf + len, buf_len - len, "invalid_vdev_type = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_vdev_type));
	len += scnprintf(buf + len, buf_len - len, "invalid_ast_peer_entry = %u\n",
			 le32_to_cpu(htt_stats_buf->invalid_ast_peer_entry));
	len += scnprintf(buf + len, buf_len - len, "peer_entry_invalid = %u\n",
			 le32_to_cpu(htt_stats_buf->peer_entry_invalid));
	len += scnprintf(buf + len, buf_len - len, "ethertype_not_ip = %u\n",
			 le32_to_cpu(htt_stats_buf->ethertype_not_ip));
	len += scnprintf(buf + len, buf_len - len, "eapol_lookup_failed = %u\n",
			 le32_to_cpu(htt_stats_buf->eapol_lookup_failed));
	len += scnprintf(buf + len, buf_len - len, "qpeer_not_allow_data = %u\n",
			 le32_to_cpu(htt_stats_buf->qpeer_not_allow_data));
	len += scnprintf(buf + len, buf_len - len, "fse_tid_override = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_tid_override));
	len += scnprintf(buf + len, buf_len - len, "ipv6_jumbogram_zero_length = %u\n",
			 le32_to_cpu(htt_stats_buf->ipv6_jumbogram_zero_length));
	len += scnprintf(buf + len, buf_len - len, "qos_to_non_qos_in_prog = %u\n",
			 le32_to_cpu(htt_stats_buf->qos_to_non_qos_in_prog));
	len += scnprintf(buf + len, buf_len - len, "ap_bcast_mcast_eapol = %u\n",
			 le32_to_cpu(htt_stats_buf->ap_bcast_mcast_eapol));
	len += scnprintf(buf + len, buf_len - len, "unicast_on_ap_bss_peer = %u\n",
			 le32_to_cpu(htt_stats_buf->unicast_on_ap_bss_peer));
	len += scnprintf(buf + len, buf_len - len, "ap_vdev_invalid = %u\n",
			 le32_to_cpu(htt_stats_buf->ap_vdev_invalid));
	len += scnprintf(buf + len, buf_len - len, "incomplete_llc = %u\n",
			 le32_to_cpu(htt_stats_buf->incomplete_llc));
	len += scnprintf(buf + len, buf_len - len, "eapol_duplicate_m3 = %u\n",
			 le32_to_cpu(htt_stats_buf->eapol_duplicate_m3));
	len += scnprintf(buf + len, buf_len - len, "eapol_duplicate_m4 = %u\n\n",
			 le32_to_cpu(htt_stats_buf->eapol_duplicate_m4));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_classify_status_stats_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_classify_status_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_CLASSIFY_STATUS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "eok = %u\n",
			 le32_to_cpu(htt_stats_buf->eok));
	len += scnprintf(buf + len, buf_len - len, "classify_done = %u\n",
			 le32_to_cpu(htt_stats_buf->classify_done));
	len += scnprintf(buf + len, buf_len - len, "lookup_failed = %u\n",
			 le32_to_cpu(htt_stats_buf->lookup_failed));
	len += scnprintf(buf + len, buf_len - len, "send_host_dhcp = %u\n",
			 le32_to_cpu(htt_stats_buf->send_host_dhcp));
	len += scnprintf(buf + len, buf_len - len, "send_host_mcast = %u\n",
			 le32_to_cpu(htt_stats_buf->send_host_mcast));
	len += scnprintf(buf + len, buf_len - len, "send_host_unknown_dest = %u\n",
			 le32_to_cpu(htt_stats_buf->send_host_unknown_dest));
	len += scnprintf(buf + len, buf_len - len, "send_host = %u\n",
			 le32_to_cpu(htt_stats_buf->send_host));
	len += scnprintf(buf + len, buf_len - len, "status_invalid = %u\n\n",
			 le32_to_cpu(htt_stats_buf->status_invalid));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_enqueue_packets_stats_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_enqueue_packets_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_ENQUEUE_PACKETS_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "enqueued_pkts = %u\n",
			 le32_to_cpu(htt_stats_buf->enqueued_pkts));
	len += scnprintf(buf + len, buf_len - len, "to_tqm = %u\n",
			 le32_to_cpu(htt_stats_buf->to_tqm));
	len += scnprintf(buf + len, buf_len - len, "to_tqm_bypass = %u\n\n",
			 le32_to_cpu(htt_stats_buf->to_tqm_bypass));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_enqueue_discard_stats_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_enqueue_discard_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_DE_ENQUEUE_DISCARD_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "discarded_pkts = %u\n",
			 le32_to_cpu(htt_stats_buf->discarded_pkts));
	len += scnprintf(buf + len, buf_len - len, "local_frames = %u\n",
			 le32_to_cpu(htt_stats_buf->local_frames));
	len += scnprintf(buf + len, buf_len - len, "is_ext_msdu = %u\n\n",
			 le32_to_cpu(htt_stats_buf->is_ext_msdu));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_de_compl_stats_tlv(const void *tag_buf, u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_de_compl_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_DE_COMPL_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "tcl_dummy_frame = %u\n",
			 le32_to_cpu(htt_stats_buf->tcl_dummy_frame));
	len += scnprintf(buf + len, buf_len - len, "tqm_dummy_frame = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_dummy_frame));
	len += scnprintf(buf + len, buf_len - len, "tqm_notify_frame = %u\n",
			 le32_to_cpu(htt_stats_buf->tqm_notify_frame));
	len += scnprintf(buf + len, buf_len - len, "fw2wbm_enq = %u\n",
			 le32_to_cpu(htt_stats_buf->fw2wbm_enq));
	len += scnprintf(buf + len, buf_len - len, "tqm_bypass_frame = %u\n\n",
			 le32_to_cpu(htt_stats_buf->tqm_bypass_frame));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_cmn_stats_tlv(const void *tag_buf, u16 tag_len,
					  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_cmn_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_CMN_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "su_bar = %u\n",
			 le32_to_cpu(htt_stats_buf->su_bar));
	len += scnprintf(buf + len, buf_len - len, "rts = %u\n",
			 le32_to_cpu(htt_stats_buf->rts));
	len += scnprintf(buf + len, buf_len - len, "cts2self = %u\n",
			 le32_to_cpu(htt_stats_buf->cts2self));
	len += scnprintf(buf + len, buf_len - len, "qos_null = %u\n",
			 le32_to_cpu(htt_stats_buf->qos_null));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_1 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_1));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_2 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_2));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_3 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_3));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_4 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_4));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_5 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_5));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_6 = %u\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_6));
	len += scnprintf(buf + len, buf_len - len, "delayed_bar_7 = %u\n\n",
			 le32_to_cpu(htt_stats_buf->delayed_bar_7));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ac_stats_tlv(const void *tag_buf, u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_ac_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_su_ndpa));
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_su_ndp));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_ndpa));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_ndp));
	len += print_array_to_buf_index(buf, len, "ac_mu_mimo_brpollX_tried = ", 1,
					htt_stats_buf->ac_mu_mimo_brpoll,
					ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS - 1,
					"\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ax_stats_tlv(const void *tag_buf, u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_ax_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_su_ndpa));
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_su_ndp));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_ndpa));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_ndp));
	len += print_array_to_buf_index(buf, len, "ax_mu_mimo_brpollX_tried = ", 1,
					htt_stats_buf->ax_mu_mimo_brpoll,
					ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS - 1, "\n");
	len += scnprintf(buf + len, buf_len - len, "ax_basic_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_basic_trigger));
	len += scnprintf(buf + len, buf_len - len, "ax_ulmumimo_total_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_ulmumimo_trigger));
	len += scnprintf(buf + len, buf_len - len, "ax_bsr_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_bsr_trigger));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_bar_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_bar_trigger));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_rts_trigger = %u\n\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_rts_trigger));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_be_stats_tlv(const void *tag_buf, u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_be_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_BE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "be_su_ndpa_queued = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndpa_queued));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndpa));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndp_queued = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndp_queued));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndp));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndpa_queued = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndpa_queued));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndpa_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndpa));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndp_queued = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndp_queued));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndp_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndp));
	len += print_array_to_buf_index(buf, len, "be_mu_mimo_brpollX_queued = ", 1,
					htt_stats_buf->be_mu_mimo_brpoll_queued,
					ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1,
					"\n");
	len += print_array_to_buf_index(buf, len, "be_mu_mimo_brpollX_tried = ", 1,
					htt_stats_buf->be_mu_mimo_brpoll,
					ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1,
					"\n");
	len += print_array_to_buf(buf, len, "be_ul_mumimo_trigger = ",
				  htt_stats_buf->be_ul_mumimo_trigger,
				  ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS, "\n");
	len += scnprintf(buf + len, buf_len - len, "be_basic_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->be_basic_trigger));
	len += scnprintf(buf + len, buf_len - len, "be_ulmumimo_total_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->be_ulmumimo_trigger));
	len += scnprintf(buf + len, buf_len - len, "be_bsr_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->be_bsr_trigger));
	len += scnprintf(buf + len, buf_len - len, "be_mu_bar_trigger = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_bar_trigger));
	len += scnprintf(buf + len, buf_len - len, "be_mu_rts_trigger = %u\n\n",
			 le32_to_cpu(htt_stats_buf->be_mu_rts_trigger));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ac_err_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_ac_err_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AC_ERR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_su_ndp_err));
	len += scnprintf(buf + len, buf_len - len, "ac_su_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_su_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_ndp_err));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp1_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_brp1_err));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp2_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_brp2_err));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_brp3_err = %u\n\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_brp3_err));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ax_err_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_ax_err_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_AX_ERR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_su_ndp_err));
	len += scnprintf(buf + len, buf_len - len, "ax_su_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_su_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_ndp_err));
	len += print_array_to_buf_index(buf, len, "ax_mu_mimo_brpX_err", 1,
					htt_stats_buf->ax_mu_mimo_brp_err,
					ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS - 1,
					"\n");
	len += scnprintf(buf + len, buf_len - len, "ax_basic_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_basic_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "ax_ulmumimo_total_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_ulmumimo_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "ax_bsr_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_bsr_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_bar_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_bar_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_rts_trigger_err = %u\n\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_rts_trigger_err));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_be_err_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_selfgen_be_err_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_SELFGEN_BE_ERR_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "be_su_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndp_err));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndp_flushed = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndp_flushed));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "be_su_ndpa_flushed = %u\n",
			 le32_to_cpu(htt_stats_buf->be_su_ndpa_flushed));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndpa_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndpa_err));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndpa_flushed = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndpa_flushed));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndp_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndp_err));
	len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_ndp_flushed = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_mimo_ndp_flushed));
	len += print_array_to_buf_index(buf, len, "be_mu_mimo_brpX_err", 1,
					htt_stats_buf->be_mu_mimo_brp_err,
					ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1,
					"\n");
	len += print_array_to_buf_index(buf, len, "be_mu_mimo_brpollX_flushed", 1,
					htt_stats_buf->be_mu_mimo_brpoll_flushed,
					ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS - 1,
					"\n");
	len += print_array_to_buf(buf, len, "be_mu_mimo_num_cbf_rcvd_on_brp_err",
				  htt_stats_buf->be_mu_mimo_brp_err_num_cbf_rxd,
				  ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_ul_mumimo_trigger_err",
				  htt_stats_buf->be_ul_mumimo_trigger_err,
				  ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS, "\n");
	len += scnprintf(buf + len, buf_len - len, "be_basic_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_basic_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "be_ulmumimo_total_trig_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_ulmumimo_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "be_bsr_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_bsr_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "be_mu_bar_trigger_err = %u\n",
			 le32_to_cpu(htt_stats_buf->be_mu_bar_trigger_err));
	len += scnprintf(buf + len, buf_len - len, "be_mu_rts_trigger_err = %u\n\n",
			 le32_to_cpu(htt_stats_buf->be_mu_rts_trigger_err));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ac_sched_status_stats_tlv(const void *tag_buf, u16 tag_len,
						      struct debug_htt_stats_req *stats)
{
	const struct ath12k_htt_tx_selfgen_ac_sched_status_stats_tlv *htt_stats_buf =
		     tag_buf;
	u8 *buf = stats->buf;
	u32 len = stats->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_SELFGEN_AC_SCHED_STATUS_STATS_TLV:\n");
	len += print_array_to_buf(buf, len, "ac_su_ndpa_sch_status",
				  htt_stats_buf->ac_su_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ac_su_ndp_sch_status",
				  htt_stats_buf->ac_su_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_ndpa_sch_status",
				  htt_stats_buf->ac_mu_mimo_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_ndp_sch_status",
				  htt_stats_buf->ac_mu_mimo_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_brp_sch_status",
				  htt_stats_buf->ac_mu_mimo_brp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ac_su_ndp_sch_flag_err",
				  htt_stats_buf->ac_su_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_ndp_sch_flag_err",
				  htt_stats_buf->ac_mu_mimo_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_brp_sch_flag_err",
				  htt_stats_buf->ac_mu_mimo_brp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n\n");

	stats->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_ax_sched_status_stats_tlv(const void *tag_buf, u16 tag_len,
						      struct debug_htt_stats_req *stats)
{
	const struct ath12k_htt_tx_selfgen_ax_sched_status_stats_tlv *htt_stats_buf =
		     tag_buf;
	u8 *buf = stats->buf;
	u32 len = stats->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_SELFGEN_AX_SCHED_STATUS_STATS_TLV:\n");
	len += print_array_to_buf(buf, len, "ax_su_ndpa_sch_status",
				  htt_stats_buf->ax_su_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_su_ndp_sch_status",
				  htt_stats_buf->ax_su_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_mimo_ndpa_sch_status",
				  htt_stats_buf->ax_mu_mimo_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_mimo_ndp_sch_status",
				  htt_stats_buf->ax_mu_mimo_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_brp_sch_status",
				  htt_stats_buf->ax_mu_brp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_bar_sch_status",
				  htt_stats_buf->ax_mu_bar_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_basic_trig_sch_status",
				  htt_stats_buf->ax_basic_trig_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_su_ndp_sch_flag_err",
				  htt_stats_buf->ax_su_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_mimo_ndp_sch_flag_err",
				  htt_stats_buf->ax_mu_mimo_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_brp_sch_flag_err",
				  htt_stats_buf->ax_mu_brp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_bar_sch_flag_err",
				  htt_stats_buf->ax_mu_bar_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ax_basic_trig_sch_flag_err",
				  htt_stats_buf->ax_basic_trig_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "ax_ulmumimo_trig_sch_status",
				  htt_stats_buf->ax_ulmumimo_trig_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "ax_ulmumimo_trig_sch_flag_err",
				  htt_stats_buf->ax_ulmumimo_trig_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n\n");

	stats->buf_len = len;
}

static void
ath12k_htt_print_tx_selfgen_be_sched_status_stats_tlv(const void *tag_buf, u16 tag_len,
						      struct debug_htt_stats_req *stats)
{
	const struct ath12k_htt_tx_selfgen_be_sched_status_stats_tlv *htt_stats_buf =
		     tag_buf;
	u8 *buf = stats->buf;
	u32 len = stats->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_SELFGEN_BE_SCHED_STATUS_STATS_TLV:\n");
	len += print_array_to_buf(buf, len, "be_su_ndpa_sch_status",
				  htt_stats_buf->be_su_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_su_ndp_sch_status",
				  htt_stats_buf->be_su_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_mimo_ndpa_sch_status",
				  htt_stats_buf->be_mu_mimo_ndpa_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_mimo_ndp_sch_status",
				  htt_stats_buf->be_mu_mimo_ndp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_brp_sch_status",
				  htt_stats_buf->be_mu_brp_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_bar_sch_status",
				  htt_stats_buf->be_mu_bar_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_basic_trig_sch_status",
				  htt_stats_buf->be_basic_trig_sch_status,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_su_ndp_sch_flag_err",
				  htt_stats_buf->be_su_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_mimo_ndp_sch_flag_err",
				  htt_stats_buf->be_mu_mimo_ndp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_brp_sch_flag_err",
				  htt_stats_buf->be_mu_brp_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_mu_bar_sch_flag_err",
				  htt_stats_buf->be_mu_bar_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_basic_trig_sch_flag_err",
				  htt_stats_buf->be_basic_trig_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n");
	len += print_array_to_buf(buf, len, "be_basic_trig_sch_flag_err",
				  htt_stats_buf->be_basic_trig_sch_flag_err,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_TX_ERR_STATUS, "\n");
	len += print_array_to_buf(buf, len, "be_ulmumimo_trig_sch_flag_err",
				  htt_stats_buf->be_ulmumimo_trig_sch_flag_err,
				  ATH12K_HTT_TX_SELFGEN_SCH_TSFLAG_ERR_STATS, "\n\n");

	stats->buf_len = len;
}

static void
ath12k_htt_print_stats_string_tlv(const void *tag_buf, u16 tag_len,
				  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_stats_string_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 i;
	u16 index = 0;
	u32 datum;
	char data[ATH12K_HTT_MAX_STRING_LEN] = {0};

	tag_len = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_STRING_TLV:\n");
	for (i = 0; i < tag_len; i++) {
		datum = __le32_to_cpu(htt_stats_buf->data[i]);
		index += scnprintf(&data[index], ATH12K_HTT_MAX_STRING_LEN - index,
				   "%.*s", 4, (char *)&datum);
		if (index >= ATH12K_HTT_MAX_STRING_LEN)
			break;
	}
	len += scnprintf(buf + len, buf_len - len, "data = %s\n\n", data);

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sring_stats_tlv(const void *tag_buf, u16 tag_len,
				 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sring_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;
	u32 avail_words;
	u32 head_tail_ptr;
	u32 sring_stat;
	u32 tail_ptr;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__ring_id__arena__ep);
	avail_words = __le32_to_cpu(htt_stats_buf->num_avail_words__num_valid_words);
	head_tail_ptr = __le32_to_cpu(htt_stats_buf->head_ptr__tail_ptr);
	sring_stat = __le32_to_cpu(htt_stats_buf->consumer_empty__producer_full);
	tail_ptr = __le32_to_cpu(htt_stats_buf->prefetch_count__internal_tail_ptr);

	len += scnprintf(buf + len, buf_len - len, "HTT_SRING_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_SRING_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "ring_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_SRING_STATS_RING_ID));
	len += scnprintf(buf + len, buf_len - len, "arena = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_SRING_STATS_ARENA));
	len += scnprintf(buf + len, buf_len - len, "ep = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_SRING_STATS_EP));
	len += scnprintf(buf + len, buf_len - len, "base_addr_lsb = 0x%x\n",
			 le32_to_cpu(htt_stats_buf->base_addr_lsb));
	len += scnprintf(buf + len, buf_len - len, "base_addr_msb = 0x%x\n",
			 le32_to_cpu(htt_stats_buf->base_addr_msb));
	len += scnprintf(buf + len, buf_len - len, "ring_size = %u\n",
			 le32_to_cpu(htt_stats_buf->ring_size));
	len += scnprintf(buf + len, buf_len - len, "elem_size = %u\n",
			 le32_to_cpu(htt_stats_buf->elem_size));
	len += scnprintf(buf + len, buf_len - len, "num_avail_words = %u\n",
			 u32_get_bits(avail_words,
				      ATH12K_HTT_SRING_STATS_NUM_AVAIL_WORDS));
	len += scnprintf(buf + len, buf_len - len, "num_valid_words = %u\n",
			 u32_get_bits(avail_words,
				      ATH12K_HTT_SRING_STATS_NUM_VALID_WORDS));
	len += scnprintf(buf + len, buf_len - len, "head_ptr = %u\n",
			 u32_get_bits(head_tail_ptr, ATH12K_HTT_SRING_STATS_HEAD_PTR));
	len += scnprintf(buf + len, buf_len - len, "tail_ptr = %u\n",
			 u32_get_bits(head_tail_ptr, ATH12K_HTT_SRING_STATS_TAIL_PTR));
	len += scnprintf(buf + len, buf_len - len, "consumer_empty = %u\n",
			 u32_get_bits(sring_stat,
				      ATH12K_HTT_SRING_STATS_CONSUMER_EMPTY));
	len += scnprintf(buf + len, buf_len - len, "producer_full = %u\n",
			 u32_get_bits(head_tail_ptr,
				      ATH12K_HTT_SRING_STATS_PRODUCER_FULL));
	len += scnprintf(buf + len, buf_len - len, "prefetch_count = %u\n",
			 u32_get_bits(tail_ptr, ATH12K_HTT_SRING_STATS_PREFETCH_COUNT));
	len += scnprintf(buf + len, buf_len - len, "internal_tail_ptr = %u\n\n",
			 u32_get_bits(tail_ptr,
				      ATH12K_HTT_SRING_STATS_INTERNAL_TAIL_PTR));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sfm_cmn_tlv(const void *tag_buf, u16 tag_len,
			     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sfm_cmn_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CMN_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "buf_total = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_total));
	len += scnprintf(buf + len, buf_len - len, "mem_empty = %u\n",
			 le32_to_cpu(htt_stats_buf->mem_empty));
	len += scnprintf(buf + len, buf_len - len, "deallocate_bufs = %u\n",
			 le32_to_cpu(htt_stats_buf->deallocate_bufs));
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n\n",
			 le32_to_cpu(htt_stats_buf->num_records));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sfm_client_tlv(const void *tag_buf, u16 tag_len,
				struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sfm_client_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CLIENT_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "client_id = %u\n",
			 le32_to_cpu(htt_stats_buf->client_id));
	len += scnprintf(buf + len, buf_len - len, "buf_min = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_min));
	len += scnprintf(buf + len, buf_len - len, "buf_max = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_max));
	len += scnprintf(buf + len, buf_len - len, "buf_busy = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_busy));
	len += scnprintf(buf + len, buf_len - len, "buf_alloc = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_alloc));
	len += scnprintf(buf + len, buf_len - len, "buf_avail = %u\n",
			 le32_to_cpu(htt_stats_buf->buf_avail));
	len += scnprintf(buf + len, buf_len - len, "num_users = %u\n\n",
			 le32_to_cpu(htt_stats_buf->num_users));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_sfm_client_user_tlv(const void *tag_buf, u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_sfm_client_user_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u16 num_elems = tag_len >> 2;

	len += scnprintf(buf + len, buf_len - len, "HTT_SFM_CLIENT_USER_TLV:\n");
	len += print_array_to_buf(buf, len, "dwords_used_by_user_n",
				  htt_stats_buf->dwords_used_by_user_n,
				  num_elems, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_pdev_mu_mimo_sch_stats_tlv(const void *tag_buf, u16 tag_len,
					       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_mu_mimo_sch_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 i;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_MU_MIMO_SCH_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_mimo_sch_posted));
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_sch_failed = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_mimo_sch_failed));
	len += scnprintf(buf + len, buf_len - len, "mu_mimo_ppdu_posted = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_mimo_ppdu_posted));
	len += scnprintf(buf + len, buf_len - len,
			 "\nac_mu_mimo_sch_posted_per_group_index %u (SU) = %u\n", 0,
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_per_grp_sz[0]));
	for (i = 1; i < ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ac_mu_mimo_sch_posted_per_group_index %u ", i);
		len += scnprintf(buf + len, buf_len - len,
				 "(TOTAL STREAMS = %u) = %u\n", i + 1,
				 le32_to_cpu(htt_stats_buf->ac_mu_mimo_per_grp_sz[i]));
	}

	for (i = 0; i < ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ac_mu_mimo_sch_posted_per_group_index %u ",
				 i + ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS);
		len += scnprintf(buf + len, buf_len - len,
				 "(TOTAL STREAMS = %u) = %u\n",
				 i + ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS + 1,
				 le32_to_cpu(htt_stats_buf->ac_mu_mimo_grp_sz_ext[i]));
	}

	len += scnprintf(buf + len, buf_len - len,
			 "\nax_mu_mimo_sch_posted_per_group_index %u (SU) = %u\n", 0,
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_per_grp_sz[0]));
	for (i = 1; i < ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_mu_mimo_sch_posted_per_group_index %u ", i);
		len += scnprintf(buf + len, buf_len - len,
				 "(TOTAL STREAMS = %u) = %u\n", i + 1,
				 le32_to_cpu(htt_stats_buf->ax_mu_mimo_per_grp_sz[i]));
	}

	len += scnprintf(buf + len, buf_len - len,
			"\nbe_mu_mimo_sch_posted_per_group_index %u (SU) = %u\n", 0,
			le32_to_cpu(htt_stats_buf->be_mu_mimo_per_grp_sz[0]));
	for (i = 1; i < ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "be_mu_mimo_sch_posted_per_group_index %u ", i);
		len += scnprintf(buf + len, buf_len - len,
				 "(TOTAL STREAMS = %u) = %u\n", i + 1,
				 le32_to_cpu(htt_stats_buf->be_mu_mimo_per_grp_sz[i]));
	}

	len += scnprintf(buf + len, buf_len - len, "\n11ac MU_MIMO SCH STATS:\n");
	for (i = 0; i < ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_sch_nusers_");
		len += scnprintf(buf + len, buf_len - len, "%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ac_mu_mimo_sch_nusers[i]));
	}

	len += scnprintf(buf + len, buf_len - len, "\n11ax MU_MIMO SCH STATS:\n");
	for (i = 0; i < ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_sch_nusers_");
		len += scnprintf(buf + len, buf_len - len, "%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_mu_mimo_sch_nusers[i]));
	}

	len += scnprintf(buf + len, buf_len - len, "\n11be MU_MIMO SCH STATS:\n");
	for (i = 0; i < ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len, "be_mu_mimo_sch_nusers_");
		len += scnprintf(buf + len, buf_len - len, "%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->be_mu_mimo_sch_nusers[i]));
	}

	len += scnprintf(buf + len, buf_len - len, "\n11ax OFDMA SCH STATS:\n");
	for (i = 0; i < ATH12K_HTT_TX_NUM_OFDMA_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ofdma_sch_nusers_%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ofdma_sch_nusers[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_ofdma_basic_sch_nusers_%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_ofdma_nusers[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_ofdma_bsr_sch_nusers_%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_ofdma_bsr_nusers[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_ofdma_bar_sch_nusers_%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_ofdma_bar_nusers[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_ofdma_brp_sch_nusers_%u = %u\n\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_ofdma_brp_nusers[i]));
	}

	len += scnprintf(buf + len, buf_len - len, "11ax UL MUMIMO SCH STATS:\n");
	for (i = 0; i < ATH12K_HTT_TX_NUM_UL_MUMIMO_USER_STATS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_mumimo_basic_sch_nusers_%u = %u\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_mumimo_nusers[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_ul_mumimo_brp_sch_nusers_%u = %u\n\n", i,
				 le32_to_cpu(htt_stats_buf->ax_ul_mumimo_brp_nusers[i]));
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_pdev_mumimo_grp_stats_tlv(const void *tag_buf, u16 tag_len,
					      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_mumimo_grp_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	int j;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_MUMIMO_GRP_STATS:\n");
	len += print_array_to_buf(buf, len,
				  "dl_mumimo_grp_tputs_observed (per bin = 300 mbps)",
				  htt_stats_buf->dl_mumimo_grp_tputs,
				  ATH12K_HTT_STATS_MUMIMO_TPUT_NUM_BINS, "\n");
	len += print_array_to_buf(buf, len, "dl_mumimo_grp eligible",
				  htt_stats_buf->dl_mumimo_grp_eligible,
				  ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ, "\n");
	len += print_array_to_buf(buf, len, "dl_mumimo_grp_ineligible",
				  htt_stats_buf->dl_mumimo_grp_ineligible,
				  ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ, "\n");
	len += scnprintf(buf + len, buf_len - len, "dl_mumimo_grp_invalid:\n");
	for (j = 0; j < ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ; j++) {
		len += scnprintf(buf + len, buf_len - len, "grp_id = %u", j);
		len += print_array_to_buf(buf, len, "",
					  htt_stats_buf->dl_mumimo_grp_invalid,
					  ATH12K_HTT_STATS_MAX_INVALID_REASON_CODE,
					  "\n");
	}

	len += print_array_to_buf(buf, len, "ul_mumimo_grp_best_grp_size",
				  htt_stats_buf->ul_mumimo_grp_best_grp_size,
				  ATH12K_HTT_STATS_NUM_MAX_MUMIMO_SZ, "\n");
	len += print_array_to_buf(buf, len, "ul_mumimo_grp_best_num_usrs = ",
				  htt_stats_buf->ul_mumimo_grp_best_usrs,
				  ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS, "\n");
	len += print_array_to_buf(buf, len,
				  "ul_mumimo_grp_tputs_observed (per bin = 300 mbps)",
				  htt_stats_buf->ul_mumimo_grp_tputs,
				  ATH12K_HTT_STATS_MUMIMO_TPUT_NUM_BINS, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_pdev_mu_mimo_mpdu_stats_tlv(const void *tag_buf, u16 tag_len,
						struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_mpdu_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 user_index;
	u32 tx_sched_mode;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	user_index = __le32_to_cpu(htt_stats_buf->user_index);
	tx_sched_mode = __le32_to_cpu(htt_stats_buf->tx_sched_mode);

	if (tx_sched_mode == ATH12K_HTT_STATS_TX_SCHED_MODE_MU_MIMO_AC) {
		if (!user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_MU_MIMO_AC_MPDU_STATS:\n");

		if (user_index < ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_queued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_queued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_tried_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_tried_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_failed_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_failed_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdus_requeued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_requeued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_err_no_ba_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->err_no_ba_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ac_mu_mimo_mpdu_underrun_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdu_underrun_usr));
			len += scnprintf(buf + len, buf_len - len,
					"ac_mu_mimo_ampdu_underrun_usr_%u = %u\n\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->ampdu_underrun_usr));
		}
	}

	if (tx_sched_mode == ATH12K_HTT_STATS_TX_SCHED_MODE_MU_MIMO_AX) {
		if (!user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_MU_MIMO_AX_MPDU_STATS:\n");

		if (user_index < ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_queued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_queued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_tried_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_tried_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_failed_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_failed_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdus_requeued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_requeued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_err_no_ba_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->err_no_ba_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_mpdu_underrun_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdu_underrun_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_mimo_ampdu_underrun_usr_%u = %u\n\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->ampdu_underrun_usr));
		}
	}

	if (tx_sched_mode == ATH12K_HTT_STATS_TX_SCHED_MODE_MU_OFDMA_AX) {
		if (!user_index)
			len += scnprintf(buf + len, buf_len - len,
					 "HTT_TX_PDEV_AX_MU_OFDMA_MPDU_STATS:\n");

		if (user_index < ATH12K_HTT_TX_NUM_OFDMA_USER_STATS) {
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_queued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_queued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_tried_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_tried_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_failed_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_failed_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdus_requeued_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdus_requeued_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_err_no_ba_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->err_no_ba_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_mpdu_underrun_usr_%u = %u\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->mpdu_underrun_usr));
			len += scnprintf(buf + len, buf_len - len,
					 "ax_mu_ofdma_ampdu_underrun_usr_%u = %u\n\n",
					 user_index,
					 le32_to_cpu(htt_stats_buf->ampdu_underrun_usr));
		}
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_cca_stats_hist_tlv(const void *tag_buf, u16 tag_len,
					 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_cca_stats_hist_v1_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_CCA_STATS_HIST_TLV :\n");
	len += scnprintf(buf + len, buf_len - len, "chan_num = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_num));
	len += scnprintf(buf + len, buf_len - len, "num_records = %u\n",
			 le32_to_cpu(htt_stats_buf->num_records));
	len += scnprintf(buf + len, buf_len - len, "valid_cca_counters_bitmap = 0x%x\n",
			 le32_to_cpu(htt_stats_buf->valid_cca_counters_bitmap));
	len += scnprintf(buf + len, buf_len - len, "collection_interval = %u\n\n",
			 le32_to_cpu(htt_stats_buf->collection_interval));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_stats_cca_counters_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_stats_cca_counters_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_PDEV_STATS_CCA_COUNTERS_TLV:(in usec)\n");
	len += scnprintf(buf + len, buf_len - len, "tx_frame_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_frame_usec));
	len += scnprintf(buf + len, buf_len - len, "rx_frame_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_frame_usec));
	len += scnprintf(buf + len, buf_len - len, "rx_clear_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_clear_usec));
	len += scnprintf(buf + len, buf_len - len, "my_rx_frame_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->my_rx_frame_usec));
	len += scnprintf(buf + len, buf_len - len, "usec_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->usec_cnt));
	len += scnprintf(buf + len, buf_len - len, "med_rx_idle_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->med_rx_idle_usec));
	len += scnprintf(buf + len, buf_len - len, "med_tx_idle_global_usec = %u\n",
			 le32_to_cpu(htt_stats_buf->med_tx_idle_global_usec));
	len += scnprintf(buf + len, buf_len - len, "cca_obss_usec = %u\n\n",
			 le32_to_cpu(htt_stats_buf->cca_obss_usec));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_sounding_stats_tlv(const void *tag_buf, u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_sounding_stats_tlv *htt_stats_buf = tag_buf;
	const __le32 *cbf_20, *cbf_40, *cbf_80, *cbf_160, *cbf_320;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 tx_sounding_mode;
	u8 i, u;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	cbf_20 = htt_stats_buf->cbf_20;
	cbf_40 = htt_stats_buf->cbf_40;
	cbf_80 = htt_stats_buf->cbf_80;
	cbf_160 = htt_stats_buf->cbf_160;
	cbf_320 = htt_stats_buf->cbf_320;
	tx_sounding_mode = le32_to_cpu(htt_stats_buf->tx_sounding_mode);

	if (tx_sounding_mode == ATH12K_HTT_TX_AC_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_TX_AC_SOUNDING_STATS_TLV:\n");
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_20 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_20[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_40 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_40[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_80 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_80[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ac_cbf_160 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_160[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));

		for (u = 0, i = 0; u < ATH12K_HTT_TX_NUM_AC_MUMIMO_USER_STATS; u++) {
			len += scnprintf(buf + len, buf_len - len,
					 "Sounding User_%u = 20MHz: %u, ", u,
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "40MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "80MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "160MHz: %u\n",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
		}
	} else if (tx_sounding_mode == ATH12K_HTT_TX_AX_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "\nHTT_TX_AX_SOUNDING_STATS_TLV:\n");
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_20 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_20[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_40 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_40[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_80 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_80[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "ax_cbf_160 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_160[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));

		for (u = 0, i = 0; u < ATH12K_HTT_TX_NUM_AX_MUMIMO_USER_STATS; u++) {
			len += scnprintf(buf + len, buf_len - len,
					 "Sounding User_%u = 20MHz: %u, ", u,
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "40MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "80MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "160MHz: %u\n",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
		}
	} else if (tx_sounding_mode == ATH12K_HTT_TX_BE_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "\nHTT_TX_BE_SOUNDING_STATS_TLV:\n");
		len += scnprintf(buf + len, buf_len - len,
				 "be_cbf_20 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_20[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_20[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "be_cbf_40 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_40[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_40[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "be_cbf_80 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_80[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_80[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "be_cbf_160 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_160[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_160[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len,
				 "be_cbf_320 = IBF: %u, SU_SIFS: %u, SU_RBO: %u, ",
				 le32_to_cpu(cbf_320[ATH12K_HTT_IMPL_STEER_STATS]),
				 le32_to_cpu(cbf_320[ATH12K_HTT_EXPL_SUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_320[ATH12K_HTT_EXPL_SURBO_STEER_STATS]));
		len += scnprintf(buf + len, buf_len - len, "MU_SIFS: %u, MU_RBO: %u\n",
				 le32_to_cpu(cbf_320[ATH12K_HTT_EXPL_MUSIFS_STEER_STATS]),
				 le32_to_cpu(cbf_320[ATH12K_HTT_EXPL_MURBO_STEER_STATS]));
		for (u = 0, i = 0; u < ATH12K_HTT_TX_NUM_BE_MUMIMO_USER_STATS; u++) {
			len += scnprintf(buf + len, buf_len - len,
					 "Sounding User_%u = 20MHz: %u, ", u,
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "40MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len, "80MHz: %u, ",
					 le32_to_cpu(htt_stats_buf->sounding[i++]));
			len += scnprintf(buf + len, buf_len - len,
					 "160MHz: %u, 320MHz: %u\n",
					 le32_to_cpu(htt_stats_buf->sounding[i++]),
					 le32_to_cpu(htt_stats_buf->sounding_320[u]));
		}
	} else if (tx_sounding_mode == ATH12K_HTT_TX_CMN_SOUNDING_MODE) {
		len += scnprintf(buf + len, buf_len - len,
				 "\nCV UPLOAD HANDLER STATS:\n");
		len += scnprintf(buf + len, buf_len - len, "cv_nc_mismatch_err = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_nc_mismatch_err));
		len += scnprintf(buf + len, buf_len - len, "cv_fcs_err = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_fcs_err));
		len += scnprintf(buf + len, buf_len - len, "cv_frag_idx_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_frag_idx_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_invalid_peer_id = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_invalid_peer_id));
		len += scnprintf(buf + len, buf_len - len, "cv_no_txbf_setup = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_no_txbf_setup));
		len += scnprintf(buf + len, buf_len - len, "cv_expiry_in_update = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_expiry_in_update));
		len += scnprintf(buf + len, buf_len - len, "cv_pkt_bw_exceed = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_pkt_bw_exceed));
		len += scnprintf(buf + len, buf_len - len, "cv_dma_not_done_err = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_dma_not_done_err));
		len += scnprintf(buf + len, buf_len - len, "cv_update_failed = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_update_failed));
		len += scnprintf(buf + len, buf_len - len, "cv_dma_timeout_error = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_dma_timeout_error));
		len += scnprintf(buf + len, buf_len - len, "cv_buf_ibf_uploads = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_buf_ibf_uploads));
		len += scnprintf(buf + len, buf_len - len, "cv_buf_ebf_uploads = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_buf_ebf_uploads));
		len += scnprintf(buf + len, buf_len - len, "cv_buf_received = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_buf_received));
		len += scnprintf(buf + len, buf_len - len, "cv_buf_fed_back = %u\n\n",
				 le32_to_cpu(htt_stats_buf->cv_buf_fed_back));

		len += scnprintf(buf + len, buf_len - len, "CV QUERY STATS:\n");
		len += scnprintf(buf + len, buf_len - len, "cv_total_query = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_total_query));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_total_pattern_query = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_total_pattern_query));
		len += scnprintf(buf + len, buf_len - len, "cv_total_bw_query = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_total_bw_query));
		len += scnprintf(buf + len, buf_len - len, "cv_invalid_bw_coding = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_invalid_bw_coding));
		len += scnprintf(buf + len, buf_len - len, "cv_forced_sounding = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_forced_sounding));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_standalone_sounding = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_standalone_sounding));
		len += scnprintf(buf + len, buf_len - len, "cv_nc_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_nc_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_fb_type_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_fb_type_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_ofdma_bw_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_ofdma_bw_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_bw_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_bw_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_pattern_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_pattern_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_preamble_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_preamble_mismatch));
		len += scnprintf(buf + len, buf_len - len, "cv_nr_mismatch = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_nr_mismatch));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_in_use_cnt_exceeded = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_in_use_cnt_exceeded));
		len += scnprintf(buf + len, buf_len - len, "cv_ntbr_sounding = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_ntbr_sounding));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_found_upload_in_progress = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_found_upload_in_progress));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_expired_during_query = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_expired_during_query));
		len += scnprintf(buf + len, buf_len - len, "cv_found = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_found));
		len += scnprintf(buf + len, buf_len - len, "cv_not_found = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_not_found));
		len += scnprintf(buf + len, buf_len - len, "cv_total_query_ibf = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_total_query_ibf));
		len += scnprintf(buf + len, buf_len - len, "cv_found_ibf = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_found_ibf));
		len += scnprintf(buf + len, buf_len - len, "cv_not_found_ibf = %u\n",
				 le32_to_cpu(htt_stats_buf->cv_not_found_ibf));
		len += scnprintf(buf + len, buf_len - len,
				 "cv_expired_during_query_ibf = %u\n\n",
				 le32_to_cpu(htt_stats_buf->cv_expired_during_query_ibf));
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_obss_pd_stats_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_obss_pd_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 i;
	static const char *access_cat_names[ATH12K_HTT_NUM_AC_WMM] = {"best effort",
								      "background",
								      "video", "voice"};

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_OBSS_PD_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "num_spatial_reuse_tx = %u\n",
			 le32_to_cpu(htt_stats_buf->num_sr_tx_transmissions));
	len += scnprintf(buf + len, buf_len - len,
			 "num_spatial_reuse_opportunities = %u\n",
			 le32_to_cpu(htt_stats_buf->num_spatial_reuse_opportunities));
	len += scnprintf(buf + len, buf_len - len, "num_non_srg_opportunities = %u\n",
			 le32_to_cpu(htt_stats_buf->num_non_srg_opportunities));
	len += scnprintf(buf + len, buf_len - len, "num_non_srg_ppdu_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->num_non_srg_ppdu_tried));
	len += scnprintf(buf + len, buf_len - len, "num_non_srg_ppdu_success = %u\n",
			 le32_to_cpu(htt_stats_buf->num_non_srg_ppdu_success));
	len += scnprintf(buf + len, buf_len - len, "num_srg_opportunities = %u\n",
			 le32_to_cpu(htt_stats_buf->num_srg_opportunities));
	len += scnprintf(buf + len, buf_len - len, "num_srg_ppdu_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->num_srg_ppdu_tried));
	len += scnprintf(buf + len, buf_len - len, "num_srg_ppdu_success = %u\n",
			 le32_to_cpu(htt_stats_buf->num_srg_ppdu_success));
	len += scnprintf(buf + len, buf_len - len, "num_psr_opportunities = %u\n",
			 le32_to_cpu(htt_stats_buf->num_psr_opportunities));
	len += scnprintf(buf + len, buf_len - len, "num_psr_ppdu_tried = %u\n",
			 le32_to_cpu(htt_stats_buf->num_psr_ppdu_tried));
	len += scnprintf(buf + len, buf_len - len, "num_psr_ppdu_success = %u\n",
			 le32_to_cpu(htt_stats_buf->num_psr_ppdu_success));
	len += scnprintf(buf + len, buf_len - len, "min_duration_check_flush_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->num_obss_min_dur_check_flush_cnt));
	len += scnprintf(buf + len, buf_len - len, "sr_ppdu_abort_flush_cnt = %u\n\n",
			 le32_to_cpu(htt_stats_buf->num_sr_ppdu_abort_flush_cnt));

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_OBSS_PD_PER_AC_STATS:\n");
	for (i = 0; i < ATH12K_HTT_NUM_AC_WMM; i++) {
		len += scnprintf(buf + len, buf_len - len, "Access Category %u (%s)\n",
				 i, access_cat_names[i]);
		len += scnprintf(buf + len, buf_len - len,
				 "num_non_srg_ppdu_tried = %u\n",
				 le32_to_cpu(htt_stats_buf->num_non_srg_tried_per_ac[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "num_non_srg_ppdu_success = %u\n",
				 le32_to_cpu(htt_stats_buf->num_non_srg_success_ac[i]));
		len += scnprintf(buf + len, buf_len - len, "num_srg_ppdu_tried = %u\n",
				 le32_to_cpu(htt_stats_buf->num_srg_tried_per_ac[i]));
		len += scnprintf(buf + len, buf_len - len,
				 "num_srg_ppdu_success = %u\n\n",
				 le32_to_cpu(htt_stats_buf->num_srg_success_per_ac[i]));
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_latency_prof_ctx_tlv(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_latency_prof_ctx_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_LATENCY_CTX_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "duration = %u\n",
			 le32_to_cpu(htt_stats_buf->duration));
	len += scnprintf(buf + len, buf_len - len, "tx_msdu_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_msdu_cnt));
	len += scnprintf(buf + len, buf_len - len, "tx_mpdu_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_mpdu_cnt));
	len += scnprintf(buf + len, buf_len - len, "rx_msdu_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_msdu_cnt));
	len += scnprintf(buf + len, buf_len - len, "rx_mpdu_cnt = %u\n\n",
			 le32_to_cpu(htt_stats_buf->rx_mpdu_cnt));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_latency_prof_cnt(const void *tag_buf, u16 tag_len,
				  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_latency_prof_cnt_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_LATENCY_CNT_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "prof_enable_cnt = %u\n\n",
			 le32_to_cpu(htt_stats_buf->prof_enable_cnt));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_latency_prof_stats_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_latency_prof_stats_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	if (le32_to_cpu(htt_stats_buf->print_header) == 1) {
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_STATS_LATENCY_PROF_TLV:\n");
	}

	len += scnprintf(buf + len, buf_len - len, "Latency name = %s\n",
			 htt_stats_buf->latency_prof_name);
	len += scnprintf(buf + len, buf_len - len, "count = %u\n",
			 le32_to_cpu(htt_stats_buf->cnt));
	len += scnprintf(buf + len, buf_len - len, "minimum = %u\n",
			 le32_to_cpu(htt_stats_buf->min));
	len += scnprintf(buf + len, buf_len - len, "maximum = %u\n",
			 le32_to_cpu(htt_stats_buf->max));
	len += scnprintf(buf + len, buf_len - len, "last = %u\n",
			 le32_to_cpu(htt_stats_buf->last));
	len += scnprintf(buf + len, buf_len - len, "total = %u\n",
			 le32_to_cpu(htt_stats_buf->tot));
	len += scnprintf(buf + len, buf_len - len, "average = %u\n",
			 le32_to_cpu(htt_stats_buf->avg));
	len += scnprintf(buf + len, buf_len - len, "histogram interval = %u\n",
			 le32_to_cpu(htt_stats_buf->hist_intvl));
	len += print_array_to_buf(buf, len, "histogram", htt_stats_buf->hist,
				  ATH12K_HTT_LATENCY_PROFILE_NUM_MAX_HIST, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_ul_ofdma_trigger_stats(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_pdev_ul_trigger_stats_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 mac_id;
	u8 j;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_PDEV_UL_TRIGGER_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ul_ofdma = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_ul_ofdma));
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_mcs",
				  htt_stats_buf->ul_ofdma_rx_mcs,
				  ATH12K_HTT_RX_NUM_MCS_CNTRS, "\n");
	for (j = 0; j < ATH12K_HTT_RX_NUM_GI_CNTRS; j++) {
		len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_gi[%u]", j);
		len += print_array_to_buf(buf, len, "",
					  htt_stats_buf->ul_ofdma_rx_gi[j],
					  ATH12K_HTT_RX_NUM_MCS_CNTRS, "\n");
	}

	len += print_array_to_buf_index(buf, len, "ul_ofdma_rx_nss", 1,
					htt_stats_buf->ul_ofdma_rx_nss,
					ATH12K_HTT_RX_NUM_SPATIAL_STREAMS, "\n");
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_bw",
				  htt_stats_buf->ul_ofdma_rx_bw,
				  ATH12K_HTT_RX_NUM_BW_CNTRS, "\n");

	for (j = 0; j < ATH12K_HTT_RX_NUM_REDUCED_CHAN_TYPES; j++) {
		len += scnprintf(buf + len, buf_len - len, j == 0 ?
				 "half_ul_ofdma_rx_bw" :
				 "quarter_ul_ofdma_rx_bw");
		len += print_array_to_buf(buf, len, "", htt_stats_buf->red_bw[j],
					  ATH12K_HTT_RX_NUM_BW_CNTRS, "\n");
	}
	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_stbc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_rx_stbc));
	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_rx_ldpc));

	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_data_ru_size_ppdu = ");
	for (j = 0; j < ATH12K_HTT_RX_NUM_RU_SIZE_CNTRS; j++)
		len += scnprintf(buf + len, buf_len - len, " %s:%u ",
				 ath12k_htt_ax_tx_rx_ru_size_to_str(j),
				 le32_to_cpu(htt_stats_buf->data_ru_size_ppdu[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len,
			 "rx_ulofdma_non_data_ru_size_ppdu = ");
	for (j = 0; j < ATH12K_HTT_RX_NUM_RU_SIZE_CNTRS; j++)
		len += scnprintf(buf + len, buf_len - len, " %s:%u ",
				 ath12k_htt_ax_tx_rx_ru_size_to_str(j),
				 le32_to_cpu(htt_stats_buf->non_data_ru_size_ppdu[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += print_array_to_buf(buf, len, "rx_rssi_track_sta_aid",
				  htt_stats_buf->uplink_sta_aid,
				  ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK, "\n");
	len += print_array_to_buf(buf, len, "rx_sta_target_rssi",
				  htt_stats_buf->uplink_sta_target_rssi,
				  ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK, "\n");
	len += print_array_to_buf(buf, len, "rx_sta_fd_rssi",
				  htt_stats_buf->uplink_sta_fd_rssi,
				  ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK, "\n");
	len += print_array_to_buf(buf, len, "rx_sta_power_headroom",
				  htt_stats_buf->uplink_sta_power_headroom,
				  ATH12K_HTT_RX_UL_MAX_UPLINK_RSSI_TRACK, "\n");
	len += scnprintf(buf + len, buf_len - len,
			 "ul_ofdma_basic_trigger_rx_qos_null_only = %u\n\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_bsc_trig_rx_qos_null_only));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_ul_ofdma_user_stats(const void *tag_buf, u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_pdev_ul_ofdma_user_stats_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 user_index;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	user_index = __le32_to_cpu(htt_stats_buf->user_index);

	if (!user_index)
		len += scnprintf(buf + len, buf_len - len,
				 "HTT_RX_PDEV_UL_OFDMA_USER_STAS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_non_data_ppdu_%u = %u\n",
			 user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_non_data_ppdu));
	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_data_ppdu_%u = %u\n",
			 user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_data_ppdu));
	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_mpdu_ok_%u = %u\n",
			 user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_mpdu_ok));
	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_mpdu_fail_%u = %u\n",
			 user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_mpdu_fail));
	len += scnprintf(buf + len, buf_len - len,
			 "rx_ulofdma_non_data_nusers_%u = %u\n", user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_non_data_nusers));
	len += scnprintf(buf + len, buf_len - len, "rx_ulofdma_data_nusers_%u = %u\n\n",
			 user_index,
			 le32_to_cpu(htt_stats_buf->rx_ulofdma_data_nusers));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_ul_mumimo_trig_stats(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_ul_mumimo_trig_stats_tlv *htt_stats_buf = tag_buf;
	char str_buf[ATH12K_HTT_MAX_STRING_LEN] = {0};
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 mac_id;
	u16 index;
	u8 i, j;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id = __le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_RX_PDEV_UL_MUMIMO_TRIG_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ul_mumimo = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_ul_mumimo));
	index = 0;
	memset(str_buf, 0x0, ATH12K_HTT_MAX_STRING_LEN);
	for (i = 0; i < ATH12K_HTT_RX_NUM_MCS_CNTRS; i++)
		index += scnprintf(&str_buf[index], ATH12K_HTT_MAX_STRING_LEN - index,
				  " %u:%u,", i,
				  le32_to_cpu(htt_stats_buf->ul_mumimo_rx_mcs[i]));

	for (i = 0; i < ATH12K_HTT_RX_NUM_EXTRA_MCS_CNTRS; i++)
		index += scnprintf(&str_buf[index], ATH12K_HTT_MAX_STRING_LEN - index,
				  " %u:%u,", i + ATH12K_HTT_RX_NUM_MCS_CNTRS,
				  le32_to_cpu(htt_stats_buf->ul_mumimo_rx_mcs_ext[i]));
	str_buf[--index] = '\0';
	len += scnprintf(buf + len, buf_len - len, "ul_mumimo_rx_mcs = %s\n", str_buf);

	for (j = 0; j < ATH12K_HTT_RX_NUM_GI_CNTRS; j++) {
		index = 0;
		memset(&str_buf[index], 0x0, ATH12K_HTT_MAX_STRING_LEN);
		for (i = 0; i < ATH12K_HTT_RX_NUM_MCS_CNTRS; i++)
			index += scnprintf(&str_buf[index],
					  ATH12K_HTT_MAX_STRING_LEN - index,
					  " %u:%u,", i,
					  le32_to_cpu(htt_stats_buf->ul_rx_gi[j][i]));

		for (i = 0; i < ATH12K_HTT_RX_NUM_EXTRA_MCS_CNTRS; i++)
			index += scnprintf(&str_buf[index],
					  ATH12K_HTT_MAX_STRING_LEN - index,
					  " %u:%u,", i + ATH12K_HTT_RX_NUM_MCS_CNTRS,
					  le32_to_cpu(htt_stats_buf->ul_gi_ext[j][i]));
		str_buf[--index] = '\0';
		len += scnprintf(buf + len, buf_len - len,
				 "ul_mumimo_rx_gi_%u = %s\n", j, str_buf);
	}

	index = 0;
	memset(str_buf, 0x0, ATH12K_HTT_MAX_STRING_LEN);
	len += print_array_to_buf_index(buf, len, "ul_mumimo_rx_nss", 1,
					htt_stats_buf->ul_mumimo_rx_nss,
					ATH12K_HTT_RX_NUM_SPATIAL_STREAMS, "\n");

	len += print_array_to_buf(buf, len, "ul_mumimo_rx_bw",
				  htt_stats_buf->ul_mumimo_rx_bw,
				  ATH12K_HTT_RX_NUM_BW_CNTRS, "\n");
	for (i = 0; i < ATH12K_HTT_RX_NUM_REDUCED_CHAN_TYPES; i++) {
		index = 0;
		memset(str_buf, 0x0, ATH12K_HTT_MAX_STRING_LEN);
		for (j = 0; j < ATH12K_HTT_RX_NUM_BW_CNTRS; j++)
			index += scnprintf(&str_buf[index],
					  ATH12K_HTT_MAX_STRING_LEN - index,
					  " %u:%u,", j,
					  le32_to_cpu(htt_stats_buf->red_bw[i][j]));
		str_buf[--index] = '\0';
		len += scnprintf(buf + len, buf_len - len, "%s = %s\n",
				 i == 0 ? "half_ul_mumimo_rx_bw" :
				 "quarter_ul_mumimo_rx_bw", str_buf);
	}

	len += scnprintf(buf + len, buf_len - len, "ul_mumimo_rx_stbc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_mumimo_rx_stbc));
	len += scnprintf(buf + len, buf_len - len, "ul_mumimo_rx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_mumimo_rx_ldpc));

	for (j = 0; j < ATH12K_HTT_RX_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ul_mumimo_rssi_in_dbm: chain%u ", j);
		len += print_array_to_buf_s8(buf, len, "", 0,
					     htt_stats_buf->ul_rssi[j],
					     ATH12K_HTT_RX_NUM_BW_CNTRS, "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_UL_MUMIMO_USER_STATS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ul_mumimo_target_rssi: user_%u ", j);
		len += print_array_to_buf_s8(buf, len, "", 0,
					     htt_stats_buf->tgt_rssi[j],
					     ATH12K_HTT_RX_NUM_BW_CNTRS, "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_UL_MUMIMO_USER_STATS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ul_mumimo_fd_rssi: user_%u ", j);
		len += print_array_to_buf_s8(buf, len, "", 0,
					     htt_stats_buf->fd[j],
					     ATH12K_HTT_RX_NUM_SPATIAL_STREAMS, "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_UL_MUMIMO_USER_STATS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ulmumimo_pilot_evm_db_mean: user_%u ", j);
		len += print_array_to_buf_s8(buf, len, "", 0,
					     htt_stats_buf->db[j],
					     ATH12K_HTT_RX_NUM_SPATIAL_STREAMS, "\n");
	}

	len += scnprintf(buf + len, buf_len - len,
			 "ul_mumimo_basic_trigger_rx_qos_null_only = %u\n\n",
			 le32_to_cpu(htt_stats_buf->mumimo_bsc_trig_rx_qos_null_only));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_rx_fse_stats_tlv(const void *tag_buf, u16 tag_len,
				  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_fse_stats_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_STATS_RX_FSE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "=== Software RX FSE STATS ===\n");
	len += scnprintf(buf + len, buf_len - len, "Enable count  = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_enable_cnt));
	len += scnprintf(buf + len, buf_len - len, "Disable count = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_disable_cnt));
	len += scnprintf(buf + len, buf_len - len, "Cache invalidate entry count = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_cache_invalidate_entry_cnt));
	len += scnprintf(buf + len, buf_len - len, "Full cache invalidate count = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_full_cache_invalidate_cnt));

	len += scnprintf(buf + len, buf_len - len, "\n=== Hardware RX FSE STATS ===\n");
	len += scnprintf(buf + len, buf_len - len, "Cache hits count = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_num_cache_hits_cnt));
	len += scnprintf(buf + len, buf_len - len, "Cache no. of searches = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_num_searches_cnt));
	len += scnprintf(buf + len, buf_len - len, "Cache occupancy peak count:\n");
	len += scnprintf(buf + len, buf_len - len, "[0] = %u [1-16] = %u [17-32] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[0]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[1]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[2]));
	len += scnprintf(buf + len, buf_len - len, "[33-48] = %u [49-64] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[3]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[4]));
	len += scnprintf(buf + len, buf_len - len, "[65-80] = %u [81-96] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[5]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[6]));
	len += scnprintf(buf + len, buf_len - len, "[97-112] = %u [113-127] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[7]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[8]));
	len += scnprintf(buf + len, buf_len - len, "[128] = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_peak_cnt[9]));
	len += scnprintf(buf + len, buf_len - len, "Cache occupancy current count:\n");
	len += scnprintf(buf + len, buf_len - len, "[0] = %u [1-16] = %u [17-32] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[0]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[1]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[2]));
	len += scnprintf(buf + len, buf_len - len, "[33-48] = %u [49-64] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[3]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[4]));
	len += scnprintf(buf + len, buf_len - len, "[65-80] = %u [81-96] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[5]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[6]));
	len += scnprintf(buf + len, buf_len - len, "[97-112] = %u [113-127] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[7]),
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[8]));
	len += scnprintf(buf + len, buf_len - len, "[128] = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_cache_occupancy_curr_cnt[9]));
	len += scnprintf(buf + len, buf_len - len, "Cache search square count:\n");
	len += scnprintf(buf + len, buf_len - len, "[0] = %u [1-50] = %u [51-100] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[0]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[1]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[2]));
	len += scnprintf(buf + len, buf_len - len, "[101-200] = %u [201-255] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[3]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[4]));
	len += scnprintf(buf + len, buf_len - len, "[256] = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_square_cnt[5]));
	len += scnprintf(buf + len, buf_len - len, "Cache search peak pending count:\n");
	len += scnprintf(buf + len, buf_len - len, "[0] = %u [1-2] = %u [3-4] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_peak_cnt[0]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_peak_cnt[1]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_peak_cnt[2]));
	len += scnprintf(buf + len, buf_len - len, "[Greater/Equal to 5] = %u\n",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_peak_cnt[3]));
	len += scnprintf(buf + len, buf_len - len, "Cache search tot pending count:\n");
	len += scnprintf(buf + len, buf_len - len, "[0] = %u [1-2] = %u [3-4] = %u ",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_pending_cnt[0]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_pending_cnt[1]),
			 le32_to_cpu(htt_stats_buf->fse_search_stat_pending_cnt[2]));
	len += scnprintf(buf + len, buf_len - len, "[Greater/Equal to 5] = %u\n\n",
			 le32_to_cpu(htt_stats_buf->fse_search_stat_pending_cnt[3]));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_tx_rate_txbf_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_txrate_txbf_stats_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u8 i;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_STATS_PDEV_TX_RATE_TXBF_STATS:\n");
	len += scnprintf(buf + len, buf_len - len, "Legacy OFDM Rates: 6 Mbps: %u, ",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[0]));
	len += scnprintf(buf + len, buf_len - len, "9 Mbps: %u, 12 Mbps: %u, ",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[1]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[2]));
	len += scnprintf(buf + len, buf_len - len, "18 Mbps: %u\n",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[3]));
	len += scnprintf(buf + len, buf_len - len, "24 Mbps: %u, 36 Mbps: %u, ",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[4]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[5]));
	len += scnprintf(buf + len, buf_len - len, "48 Mbps: %u, 54 Mbps: %u\n",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[6]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[7]));

	len += print_array_to_buf(buf, len, "tx_ol_mcs", htt_stats_buf->tx_su_ol_mcs,
				  ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "tx_ibf_mcs", htt_stats_buf->tx_su_ibf_mcs,
				  ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "tx_txbf_mcs", htt_stats_buf->tx_su_txbf_mcs,
				  ATH12K_HTT_TX_BF_RATE_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf_index(buf, len, "tx_ol_nss", 1,
					htt_stats_buf->tx_su_ol_nss,
					ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS,
					"\n");
	len += print_array_to_buf_index(buf, len, "tx_ibf_nss", 1,
					htt_stats_buf->tx_su_ibf_nss,
					ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS,
					"\n");
	len += print_array_to_buf_index(buf, len, "tx_txbf_nss", 1,
					htt_stats_buf->tx_su_txbf_nss,
					ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS,
					"\n");
	len += print_array_to_buf(buf, len, "tx_ol_bw", htt_stats_buf->tx_su_ol_bw,
				  ATH12K_HTT_TXBF_NUM_BW_CNTRS, "\n");
	for (i = 0; i < ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES; i++)
		len += print_array_to_buf(buf, len, i ? "quarter_tx_ol_bw" :
					  "half_tx_ol_bw",
					  htt_stats_buf->ol[i],
					  ATH12K_HTT_TXBF_NUM_BW_CNTRS,
					  "\n");

	len += print_array_to_buf(buf, len, "tx_ibf_bw", htt_stats_buf->tx_su_ibf_bw,
				  ATH12K_HTT_TXBF_NUM_BW_CNTRS, "\n");
	for (i = 0; i < ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES; i++)
		len += print_array_to_buf(buf, len, i ? "quarter_tx_ibf_bw" :
					  "half_tx_ibf_bw",
					  htt_stats_buf->ibf[i],
					  ATH12K_HTT_TXBF_NUM_BW_CNTRS,
					  "\n");

	len += print_array_to_buf(buf, len, "tx_txbf_bw", htt_stats_buf->tx_su_txbf_bw,
				  ATH12K_HTT_TXBF_NUM_BW_CNTRS, "\n");
	for (i = 0; i < ATH12K_HTT_TXBF_NUM_REDUCED_CHAN_TYPES; i++)
		len += print_array_to_buf(buf, len, i ? "quarter_tx_txbf_bw" :
					  "half_tx_txbf_bw",
					  htt_stats_buf->txbf[i],
					  ATH12K_HTT_TXBF_NUM_BW_CNTRS,
					  "\n");
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_STATS_PDEV_TXBF_FLAG_RETURN_STATS:\n");
	len += scnprintf(buf + len, buf_len - len, "TXBF_reason_code_stats: 0:%u, 1:%u,",
			 le32_to_cpu(htt_stats_buf->txbf_flag_set_mu_mode),
			 le32_to_cpu(htt_stats_buf->txbf_flag_set_final_status));
	len += scnprintf(buf + len, buf_len - len, " 2:%u, 3:%u, 4:%u, 5:%u, ",
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_verified_txbf_mode),
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_disable_p2p_access),
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_max_nss_in_he160),
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_disable_uldlofdma));
	len += scnprintf(buf + len, buf_len - len, "6:%u, 7:%u\n\n",
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_mcs_threshold_val),
			 le32_to_cpu(htt_stats_buf->txbf_flag_not_set_final_status));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_txbf_ofdma_ax_ndpa_stats_tlv(const void *tag_buf, u16 tag_len,
					      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_txbf_ofdma_ax_ndpa_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 num_elements;
	u8 i;

	if (tag_len < sizeof(*stats_buf))
		return;

	num_elements = le32_to_cpu(stats_buf->num_elems_ax_ndpa_arr);

	len += scnprintf(buf + len, buf_len - len, "HTT_TXBF_OFDMA_AX_NDPA_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_ofdma_ndpa_queued =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndpa[i].ax_ofdma_ndpa_queued));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndpa_tried =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndpa[i].ax_ofdma_ndpa_tried));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndpa_flushed =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndpa[i].ax_ofdma_ndpa_flush));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndpa_err =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndpa[i].ax_ofdma_ndpa_err));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_txbf_ofdma_ax_ndp_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_txbf_ofdma_ax_ndp_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 num_elements;
	u8 i;

	if (tag_len < sizeof(*stats_buf))
		return;

	num_elements = le32_to_cpu(stats_buf->num_elems_ax_ndp_arr);

	len += scnprintf(buf + len, buf_len - len, "HTT_TXBF_OFDMA_AX_NDP_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_ofdma_ndp_queued =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndp[i].ax_ofdma_ndp_queued));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndp_tried =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndp[i].ax_ofdma_ndp_tried));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndp_flushed =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndp[i].ax_ofdma_ndp_flush));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_ndp_err =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_ndp[i].ax_ofdma_ndp_err));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_txbf_ofdma_ax_brp_stats_tlv(const void *tag_buf, u16 tag_len,
					     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_txbf_ofdma_ax_brp_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 num_elements;
	u8 i;

	if (tag_len < sizeof(*stats_buf))
		return;

	num_elements = le32_to_cpu(stats_buf->num_elems_ax_brp_arr);

	len += scnprintf(buf + len, buf_len - len, "HTT_TXBF_OFDMA_AX_BRP_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_ofdma_brpoll_queued =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_brp[i].ax_ofdma_brp_queued));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_brpoll_tied =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_brp[i].ax_ofdma_brp_tried));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_brpoll_flushed =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_brp[i].ax_ofdma_brp_flushed));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_brp_err =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_brp[i].ax_ofdma_brp_err));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_brp_err_num_cbf_rcvd =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_brp[i].ax_ofdma_num_cbf_rcvd));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_txbf_ofdma_ax_steer_stats_tlv(const void *tag_buf, u16 tag_len,
					       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_txbf_ofdma_ax_steer_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 num_elements;
	u8 i;

	if (tag_len < sizeof(*stats_buf))
		return;

	num_elements = le32_to_cpu(stats_buf->num_elems_ax_steer_arr);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_AX_STEER_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ax_ofdma_num_ppdu_steer =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_steer[i].num_ppdu_steer));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_num_usrs_prefetch =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_steer[i].num_usr_prefetch));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_num_usrs_sound =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_steer[i].num_usr_sound));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\nax_ofdma_num_usrs_force_sound =");
	for (i = 0; i < num_elements; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,", i + 1,
				 le32_to_cpu(stats_buf->ax_steer[i].num_usr_force_sound));
	len--;
	*(buf + len) = '\0';

	len += scnprintf(buf + len, buf_len - len, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_txbf_ofdma_ax_steer_mpdu_stats_tlv(const void *tag_buf, u16 tag_len,
						    struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_txbf_ofdma_ax_steer_mpdu_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TXBF_OFDMA_AX_STEER_MPDU_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "rbo_steer_mpdus_tried = %u\n",
			 le32_to_cpu(stats_buf->ax_ofdma_rbo_steer_mpdus_tried));
	len += scnprintf(buf + len, buf_len - len, "rbo_steer_mpdus_failed = %u\n",
			 le32_to_cpu(stats_buf->ax_ofdma_rbo_steer_mpdus_failed));
	len += scnprintf(buf + len, buf_len - len, "sifs_steer_mpdus_tried = %u\n",
			 le32_to_cpu(stats_buf->ax_ofdma_sifs_steer_mpdus_tried));
	len += scnprintf(buf + len, buf_len - len, "sifs_steer_mpdus_failed = %u\n\n",
			 le32_to_cpu(stats_buf->ax_ofdma_sifs_steer_mpdus_failed));

	stats_req->buf_len = len;
}

static void ath12k_htt_print_dlpager_entry(const struct ath12k_htt_pgs_info *pg_info,
					   int idx, char *str_buf)
{
	u64 page_timestamp;
	u16 index = 0;

	page_timestamp = ath12k_le32hilo_to_u64(pg_info->ts_msb, pg_info->ts_lsb);

	index += snprintf(&str_buf[index], ATH12K_HTT_MAX_STRING_LEN - index,
			  "Index - %u ; Page Number - %u ; ",
			  idx, le32_to_cpu(pg_info->page_num));
	index += snprintf(&str_buf[index], ATH12K_HTT_MAX_STRING_LEN - index,
			  "Num of pages - %u ; Timestamp - %lluus\n",
			  le32_to_cpu(pg_info->num_pgs), page_timestamp);
}

static void
ath12k_htt_print_dlpager_stats_tlv(const void *tag_buf, u16 tag_len,
				   struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_dl_pager_stats_tlv *stat_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 dword_lock, dword_unlock;
	int i;
	u8 *buf = stats_req->buf;
	u8 pg_locked;
	u8 pg_unlock;
	char str_buf[ATH12K_HTT_MAX_STRING_LEN] = {0};

	if (tag_len < sizeof(*stat_buf))
		return;

	dword_lock = le32_get_bits(stat_buf->info2,
				   ATH12K_HTT_DLPAGER_TOTAL_LOCK_PAGES_INFO2);
	dword_unlock = le32_get_bits(stat_buf->info2,
				     ATH12K_HTT_DLPAGER_TOTAL_FREE_PAGES_INFO2);

	pg_locked = ATH12K_HTT_STATS_PAGE_LOCKED;
	pg_unlock = ATH12K_HTT_STATS_PAGE_UNLOCKED;

	len += scnprintf(buf + len, buf_len - len, "HTT_DLPAGER_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ASYNC locked pages = %u\n",
			 le32_get_bits(stat_buf->info0,
				       ATH12K_HTT_DLPAGER_ASYNC_LOCK_PG_CNT_INFO0));
	len += scnprintf(buf + len, buf_len - len, "SYNC locked pages = %u\n",
			 le32_get_bits(stat_buf->info0,
				       ATH12K_HTT_DLPAGER_SYNC_LOCK_PG_CNT_INFO0));
	len += scnprintf(buf + len, buf_len - len, "Total locked pages = %u\n",
			 le32_get_bits(stat_buf->info1,
				       ATH12K_HTT_DLPAGER_TOTAL_LOCK_PAGES_INFO1));
	len += scnprintf(buf + len, buf_len - len, "Total free pages = %u\n",
			 le32_get_bits(stat_buf->info1,
				       ATH12K_HTT_DLPAGER_TOTAL_FREE_PAGES_INFO1));

	len += scnprintf(buf + len, buf_len - len, "\nLOCKED PAGES HISTORY\n");
	len += scnprintf(buf + len, buf_len - len, "last_locked_page_idx = %u\n",
			 dword_lock ? dword_lock - 1 : (ATH12K_PAGER_MAX - 1));

	for (i = 0; i < ATH12K_PAGER_MAX; i++) {
		memset(str_buf, 0x0, ATH12K_HTT_MAX_STRING_LEN);
		ath12k_htt_print_dlpager_entry(&stat_buf->pgs_info[pg_locked][i],
					       i, str_buf);
		len += scnprintf(buf + len, buf_len - len, "%s", str_buf);
	}

	len += scnprintf(buf + len, buf_len - len, "\nUNLOCKED PAGES HISTORY\n");
	len += scnprintf(buf + len, buf_len - len, "last_unlocked_page_idx = %u\n",
			 dword_unlock ? dword_unlock - 1 : ATH12K_PAGER_MAX - 1);

	for (i = 0; i < ATH12K_PAGER_MAX; i++) {
		memset(str_buf, 0x0, ATH12K_HTT_MAX_STRING_LEN);
		ath12k_htt_print_dlpager_entry(&stat_buf->pgs_info[pg_unlock][i],
					       i, str_buf);
		len += scnprintf(buf + len, buf_len - len, "%s", str_buf);
	}

	len += scnprintf(buf + len, buf_len - len, "\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_phy_stats_tlv(const void *tag_buf, u16 tag_len,
			       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_phy_stats_tlv *htt_stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf, i;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_STATS_TLV:\n");
	for (i = 0; i < ATH12K_HTT_STATS_MAX_CHAINS; i++)
		len += scnprintf(buf + len, buf_len - len, "bdf_nf_chain[%d] = %d\n",
				 i, a_sle32_to_cpu(htt_stats_buf->nf_chain[i]));
	for (i = 0; i < ATH12K_HTT_STATS_MAX_CHAINS; i++)
		len += scnprintf(buf + len, buf_len - len, "runtime_nf_chain[%d] = %d\n",
				 i, a_sle32_to_cpu(htt_stats_buf->runtime_nf_chain[i]));
	len += scnprintf(buf + len, buf_len - len, "false_radar_cnt = %u / %u (mins)\n",
			 le32_to_cpu(htt_stats_buf->false_radar_cnt),
			 le32_to_cpu(htt_stats_buf->fw_run_time));
	len += scnprintf(buf + len, buf_len - len, "radar_cs_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->radar_cs_cnt));
	len += scnprintf(buf + len, buf_len - len, "ani_level = %d\n\n",
			 a_sle32_to_cpu(htt_stats_buf->ani_level));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_phy_counters_tlv(const void *tag_buf, u16 tag_len,
				  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_phy_counters_tlv *htt_stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_COUNTERS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "rx_ofdma_timing_err_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_ofdma_timing_err_cnt));
	len += scnprintf(buf + len, buf_len - len, "rx_cck_fail_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_cck_fail_cnt));
	len += scnprintf(buf + len, buf_len - len, "mactx_abort_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->mactx_abort_cnt));
	len += scnprintf(buf + len, buf_len - len, "macrx_abort_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->macrx_abort_cnt));
	len += scnprintf(buf + len, buf_len - len, "phytx_abort_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->phytx_abort_cnt));
	len += scnprintf(buf + len, buf_len - len, "phyrx_abort_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->phyrx_abort_cnt));
	len += scnprintf(buf + len, buf_len - len, "phyrx_defer_abort_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->phyrx_defer_abort_cnt));
	len += scnprintf(buf + len, buf_len - len, "rx_gain_adj_lstf_event_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_gain_adj_lstf_event_cnt));
	len += scnprintf(buf + len, buf_len - len, "rx_gain_adj_non_legacy_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_gain_adj_non_legacy_cnt));
	len += print_array_to_buf(buf, len, "rx_pkt_cnt", htt_stats_buf->rx_pkt_cnt,
				  ATH12K_HTT_MAX_RX_PKT_CNT, "\n");
	len += print_array_to_buf(buf, len, "rx_pkt_crc_pass_cnt",
				  htt_stats_buf->rx_pkt_crc_pass_cnt,
				  ATH12K_HTT_MAX_RX_PKT_CRC_PASS_CNT, "\n");
	len += print_array_to_buf(buf, len, "per_blk_err_cnt",
				  htt_stats_buf->per_blk_err_cnt,
				  ATH12K_HTT_MAX_PER_BLK_ERR_CNT, "\n");
	len += print_array_to_buf(buf, len, "rx_ota_err_cnt",
				  htt_stats_buf->rx_ota_err_cnt,
				  ATH12K_HTT_MAX_RX_OTA_ERR_CNT, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_phy_reset_stats_tlv(const void *tag_buf, u16 tag_len,
				     struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_phy_reset_stats_tlv *htt_stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_RESET_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 le32_to_cpu(htt_stats_buf->pdev_id));
	len += scnprintf(buf + len, buf_len - len, "chan_mhz = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_mhz));
	len += scnprintf(buf + len, buf_len - len, "chan_band_center_freq1 = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_band_center_freq1));
	len += scnprintf(buf + len, buf_len - len, "chan_band_center_freq2 = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_band_center_freq2));
	len += scnprintf(buf + len, buf_len - len, "chan_phy_mode = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_phy_mode));
	len += scnprintf(buf + len, buf_len - len, "chan_flags = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->chan_flags));
	len += scnprintf(buf + len, buf_len - len, "chan_num = %u\n",
			 le32_to_cpu(htt_stats_buf->chan_num));
	len += scnprintf(buf + len, buf_len - len, "reset_cause = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->reset_cause));
	len += scnprintf(buf + len, buf_len - len, "prev_reset_cause = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->prev_reset_cause));
	len += scnprintf(buf + len, buf_len - len, "phy_warm_reset_src = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_warm_reset_src));
	len += scnprintf(buf + len, buf_len - len, "rx_gain_tbl_mode = %d\n",
			 le32_to_cpu(htt_stats_buf->rx_gain_tbl_mode));
	len += scnprintf(buf + len, buf_len - len, "xbar_val = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->xbar_val));
	len += scnprintf(buf + len, buf_len - len, "force_calibration = %u\n",
			 le32_to_cpu(htt_stats_buf->force_calibration));
	len += scnprintf(buf + len, buf_len - len, "phyrf_mode = %u\n",
			 le32_to_cpu(htt_stats_buf->phyrf_mode));
	len += scnprintf(buf + len, buf_len - len, "phy_homechan = %u\n",
			 le32_to_cpu(htt_stats_buf->phy_homechan));
	len += scnprintf(buf + len, buf_len - len, "phy_tx_ch_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_tx_ch_mask));
	len += scnprintf(buf + len, buf_len - len, "phy_rx_ch_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_rx_ch_mask));
	len += scnprintf(buf + len, buf_len - len, "phybb_ini_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phybb_ini_mask));
	len += scnprintf(buf + len, buf_len - len, "phyrf_ini_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phyrf_ini_mask));
	len += scnprintf(buf + len, buf_len - len, "phy_dfs_en_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_dfs_en_mask));
	len += scnprintf(buf + len, buf_len - len, "phy_sscan_en_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_sscan_en_mask));
	len += scnprintf(buf + len, buf_len - len, "phy_synth_sel_mask = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->phy_synth_sel_mask));
	len += scnprintf(buf + len, buf_len - len, "phy_adfs_freq = %u\n",
			 le32_to_cpu(htt_stats_buf->phy_adfs_freq));
	len += scnprintf(buf + len, buf_len - len, "cck_fir_settings = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->cck_fir_settings));
	len += scnprintf(buf + len, buf_len - len, "phy_dyn_pri_chan = %u\n",
			 le32_to_cpu(htt_stats_buf->phy_dyn_pri_chan));
	len += scnprintf(buf + len, buf_len - len, "cca_thresh = 0x%0x\n",
			 le32_to_cpu(htt_stats_buf->cca_thresh));
	len += scnprintf(buf + len, buf_len - len, "dyn_cca_status = %u\n",
			 le32_to_cpu(htt_stats_buf->dyn_cca_status));
	len += scnprintf(buf + len, buf_len - len, "rxdesense_thresh_hw = 0x%x\n",
			 le32_to_cpu(htt_stats_buf->rxdesense_thresh_hw));
	len += scnprintf(buf + len, buf_len - len, "rxdesense_thresh_sw = 0x%x\n\n",
			 le32_to_cpu(htt_stats_buf->rxdesense_thresh_sw));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_phy_reset_counters_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_phy_reset_counters_tlv *htt_stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_RESET_COUNTERS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 le32_to_cpu(htt_stats_buf->pdev_id));
	len += scnprintf(buf + len, buf_len - len, "cf_active_low_fail_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->cf_active_low_fail_cnt));
	len += scnprintf(buf + len, buf_len - len, "cf_active_low_pass_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->cf_active_low_pass_cnt));
	len += scnprintf(buf + len, buf_len - len, "phy_off_through_vreg_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->phy_off_through_vreg_cnt));
	len += scnprintf(buf + len, buf_len - len, "force_calibration_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->force_calibration_cnt));
	len += scnprintf(buf + len, buf_len - len, "rf_mode_switch_phy_off_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rf_mode_switch_phy_off_cnt));
	len += scnprintf(buf + len, buf_len - len, "temperature_recal_cnt = %u\n\n",
			 le32_to_cpu(htt_stats_buf->temperature_recal_cnt));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_phy_tpc_stats_tlv(const void *tag_buf, u16 tag_len,
				   struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_phy_tpc_stats_tlv *htt_stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_PHY_TPC_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 le32_to_cpu(htt_stats_buf->pdev_id));
	len += scnprintf(buf + len, buf_len - len, "tx_power_scale = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_power_scale));
	len += scnprintf(buf + len, buf_len - len, "tx_power_scale_db = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_power_scale_db));
	len += scnprintf(buf + len, buf_len - len, "min_negative_tx_power = %d\n",
			 le32_to_cpu(htt_stats_buf->min_negative_tx_power));
	len += scnprintf(buf + len, buf_len - len, "reg_ctl_domain = %u\n",
			 le32_to_cpu(htt_stats_buf->reg_ctl_domain));
	len += scnprintf(buf + len, buf_len - len, "twice_max_rd_power = %u\n",
			 le32_to_cpu(htt_stats_buf->twice_max_rd_power));
	len += scnprintf(buf + len, buf_len - len, "max_tx_power = %u\n",
			 le32_to_cpu(htt_stats_buf->max_tx_power));
	len += scnprintf(buf + len, buf_len - len, "home_max_tx_power = %u\n",
			 le32_to_cpu(htt_stats_buf->home_max_tx_power));
	len += scnprintf(buf + len, buf_len - len, "psd_power = %d\n",
			 le32_to_cpu(htt_stats_buf->psd_power));
	len += scnprintf(buf + len, buf_len - len, "eirp_power = %u\n",
			 le32_to_cpu(htt_stats_buf->eirp_power));
	len += scnprintf(buf + len, buf_len - len, "power_type_6ghz = %u\n",
			 le32_to_cpu(htt_stats_buf->power_type_6ghz));
	len += print_array_to_buf(buf, len, "max_reg_allowed_power",
				  htt_stats_buf->max_reg_allowed_power,
				  ATH12K_HTT_STATS_MAX_CHAINS, "\n");
	len += print_array_to_buf(buf, len, "max_reg_allowed_power_6ghz",
				  htt_stats_buf->max_reg_allowed_power_6ghz,
				  ATH12K_HTT_STATS_MAX_CHAINS, "\n");
	len += print_array_to_buf(buf, len, "sub_band_cfreq",
				  htt_stats_buf->sub_band_cfreq,
				  ATH12K_HTT_MAX_CH_PWR_INFO_SIZE, "\n");
	len += print_array_to_buf(buf, len, "sub_band_txpower",
				  htt_stats_buf->sub_band_txpower,
				  ATH12K_HTT_MAX_CH_PWR_INFO_SIZE, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_soc_txrx_stats_common_tlv(const void *tag_buf, u16 tag_len,
					   struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_t2h_soc_txrx_stats_common_tlv *htt_stats_buf = tag_buf;
	u64 drop_count;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 *buf = stats_req->buf;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	drop_count = ath12k_le32hilo_to_u64(htt_stats_buf->inv_peers_msdu_drop_count_hi,
					    htt_stats_buf->inv_peers_msdu_drop_count_lo);

	len += scnprintf(buf + len, buf_len - len, "HTT_SOC_COMMON_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "soc_drop_count = %llu\n\n",
			 drop_count);

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_per_rate_stats_tlv(const void *tag_buf, u16 tag_len,
				       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_per_rate_stats_tlv *stats_buf = tag_buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 ru_size_cnt = 0;
	u32 rc_mode, ru_type;
	u8 *buf = stats_req->buf, i;
	const char *mode_prefix;

	if (tag_len < sizeof(*stats_buf))
		return;

	rc_mode = le32_to_cpu(stats_buf->rc_mode);
	ru_type = le32_to_cpu(stats_buf->ru_type);

	switch (rc_mode) {
	case ATH12K_HTT_STATS_RC_MODE_DLSU:
		len += scnprintf(buf + len, buf_len - len, "HTT_TX_PER_STATS:\n");
		len += scnprintf(buf + len, buf_len - len, "\nPER_STATS_SU:\n");
		mode_prefix = "su";
		break;
	case ATH12K_HTT_STATS_RC_MODE_DLMUMIMO:
		len += scnprintf(buf + len, buf_len - len, "\nPER_STATS_DL_MUMIMO:\n");
		mode_prefix = "mu";
		break;
	case ATH12K_HTT_STATS_RC_MODE_DLOFDMA:
		len += scnprintf(buf + len, buf_len - len, "\nPER_STATS_DL_OFDMA:\n");
		mode_prefix = "ofdma";
		if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_RU_ONLY)
			ru_size_cnt = ATH12K_HTT_TX_RX_PDEV_STATS_NUM_AX_RU_SIZE_CNTRS;
		else if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_AND_MULTI_RU)
			ru_size_cnt = ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS;
		break;
	case ATH12K_HTT_STATS_RC_MODE_ULMUMIMO:
		len += scnprintf(buf + len, buf_len - len, "HTT_RX_PER_STATS:\n");
		len += scnprintf(buf + len, buf_len - len, "\nPER_STATS_UL_MUMIMO:\n");
		mode_prefix = "ulmu";
		break;
	case ATH12K_HTT_STATS_RC_MODE_ULOFDMA:
		len += scnprintf(buf + len, buf_len - len, "\nPER_STATS_UL_OFDMA:\n");
		mode_prefix = "ulofdma";
		if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_RU_ONLY)
			ru_size_cnt = ATH12K_HTT_TX_RX_PDEV_STATS_NUM_AX_RU_SIZE_CNTRS;
		else if (ru_type == ATH12K_HTT_STATS_RU_TYPE_SINGLE_AND_MULTI_RU)
			ru_size_cnt = ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS;
		break;
	default:
		return;
	}

	len += scnprintf(buf + len, buf_len - len, "\nPER per BW:\n");
	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_tried_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_bw[i].ppdus_tried));
	len += scnprintf(buf + len, buf_len - len, " %u:%u\n", i,
			 le32_to_cpu(stats_buf->per_bw320.ppdus_tried));

	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "non_data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_ack_failed_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_bw[i].ppdus_ack_failed));
	len += scnprintf(buf + len, buf_len - len, " %u:%u\n", i,
			 le32_to_cpu(stats_buf->per_bw320.ppdus_ack_failed));

	len += scnprintf(buf + len, buf_len - len, "mpdus_tried_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_bw[i].mpdus_tried));
	len += scnprintf(buf + len, buf_len - len, " %u:%u\n", i,
			 le32_to_cpu(stats_buf->per_bw320.mpdus_tried));

	len += scnprintf(buf + len, buf_len - len, "mpdus_failed_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_BW_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u", i,
				 le32_to_cpu(stats_buf->per_bw[i].mpdus_failed));
	len += scnprintf(buf + len, buf_len - len, " %u:%u\n", i,
			 le32_to_cpu(stats_buf->per_bw320.mpdus_failed));

	len += scnprintf(buf + len, buf_len - len, "\nPER per NSS:\n");
	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_tried_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i + 1,
				 le32_to_cpu(stats_buf->per_nss[i].ppdus_tried));
	len += scnprintf(buf + len, buf_len - len, "\n");

	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "non_data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_ack_failed_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i + 1,
				 le32_to_cpu(stats_buf->per_nss[i].ppdus_ack_failed));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "mpdus_tried_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i + 1,
				 le32_to_cpu(stats_buf->per_nss[i].mpdus_tried));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "mpdus_failed_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i + 1,
				 le32_to_cpu(stats_buf->per_nss[i].mpdus_failed));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "\nPER per MCS:\n");
	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_tried_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_mcs[i].ppdus_tried));
	len += scnprintf(buf + len, buf_len - len, "\n");

	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA ||
	    rc_mode == ATH12K_HTT_STATS_RC_MODE_ULMUMIMO)
		len += scnprintf(buf + len, buf_len - len, "non_data_ppdus_%s = ",
				 mode_prefix);
	else
		len += scnprintf(buf + len, buf_len - len, "ppdus_ack_failed_%s = ",
				 mode_prefix);
	for (i = 0; i < ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_mcs[i].ppdus_ack_failed));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "mpdus_tried_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_mcs[i].mpdus_tried));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "mpdus_failed_%s = ", mode_prefix);
	for (i = 0; i < ATH12K_HTT_TXBF_RATE_STAT_NUM_MCS_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u ", i,
				 le32_to_cpu(stats_buf->per_mcs[i].mpdus_failed));
	len += scnprintf(buf + len, buf_len - len, "\n");

	if ((rc_mode == ATH12K_HTT_STATS_RC_MODE_DLOFDMA ||
	     rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA) &&
	     ru_type != ATH12K_HTT_STATS_RU_TYPE_INVALID) {
		len += scnprintf(buf + len, buf_len - len, "\nPER per RU:\n");

		if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA)
			len += scnprintf(buf + len, buf_len - len, "data_ppdus_%s = ",
					 mode_prefix);
		else
			len += scnprintf(buf + len, buf_len - len, "ppdus_tried_%s = ",
					 mode_prefix);
		for (i = 0; i < ru_size_cnt; i++)
			len += scnprintf(buf + len, buf_len - len, " %s:%u ",
					 ath12k_tx_ru_size_to_str(ru_type, i),
					 le32_to_cpu(stats_buf->ru[i].ppdus_tried));
		len += scnprintf(buf + len, buf_len - len, "\n");

		if (rc_mode == ATH12K_HTT_STATS_RC_MODE_ULOFDMA)
			len += scnprintf(buf + len, buf_len - len,
					 "non_data_ppdus_%s = ", mode_prefix);
		else
			len += scnprintf(buf + len, buf_len - len,
					 "ppdus_ack_failed_%s = ", mode_prefix);
		for (i = 0; i < ru_size_cnt; i++)
			len += scnprintf(buf + len, buf_len - len, " %s:%u ",
					 ath12k_tx_ru_size_to_str(ru_type, i),
					 le32_to_cpu(stats_buf->ru[i].ppdus_ack_failed));
		len += scnprintf(buf + len, buf_len - len, "\n");

		len += scnprintf(buf + len, buf_len - len, "mpdus_tried_%s = ",
				 mode_prefix);
		for (i = 0; i < ru_size_cnt; i++)
			len += scnprintf(buf + len, buf_len - len, " %s:%u ",
					 ath12k_tx_ru_size_to_str(ru_type, i),
					 le32_to_cpu(stats_buf->ru[i].mpdus_tried));
		len += scnprintf(buf + len, buf_len - len, "\n");

		len += scnprintf(buf + len, buf_len - len, "mpdus_failed_%s = ",
				 mode_prefix);
		for (i = 0; i < ru_size_cnt; i++)
			len += scnprintf(buf + len, buf_len - len, " %s:%u ",
					 ath12k_tx_ru_size_to_str(ru_type, i),
					 le32_to_cpu(stats_buf->ru[i].mpdus_failed));
		len += scnprintf(buf + len, buf_len - len, "\n\n");
	}

	if (rc_mode == ATH12K_HTT_STATS_RC_MODE_DLMUMIMO) {
		len += scnprintf(buf + len, buf_len - len, "\nlast_probed_bw  = %u\n",
				 le32_to_cpu(stats_buf->last_probed_bw));
		len += scnprintf(buf + len, buf_len - len, "last_probed_nss = %u\n",
				 le32_to_cpu(stats_buf->last_probed_nss));
		len += scnprintf(buf + len, buf_len - len, "last_probed_mcs = %u\n",
				 le32_to_cpu(stats_buf->last_probed_mcs));
		len += print_array_to_buf(buf, len, "MU Probe count per RC MODE",
					  stats_buf->probe_cnt,
					  ATH12K_HTT_RC_MODE_2D_COUNT, "\n\n");
	}

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_ast_entry_tlv(const void *tag_buf, u16 tag_len,
			       struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_ast_entry_tlv *htt_stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	u32 mac_addr_l32;
	u32 mac_addr_h16;
	u32 ast_info;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_addr_l32 = le32_to_cpu(htt_stats_buf->mac_addr.mac_addr_l32);
	mac_addr_h16 = le32_to_cpu(htt_stats_buf->mac_addr.mac_addr_h16);
	ast_info = le32_to_cpu(htt_stats_buf->info);

	len += scnprintf(buf + len, buf_len - len, "HTT_AST_ENTRY_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "ast_index = %u\n",
			 le32_to_cpu(htt_stats_buf->ast_index));
	len += scnprintf(buf + len, buf_len - len,
			 "mac_addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
			 u32_get_bits(mac_addr_l32, ATH12K_HTT_MAC_ADDR_L32_0),
			 u32_get_bits(mac_addr_l32, ATH12K_HTT_MAC_ADDR_L32_1),
			 u32_get_bits(mac_addr_l32, ATH12K_HTT_MAC_ADDR_L32_2),
			 u32_get_bits(mac_addr_l32, ATH12K_HTT_MAC_ADDR_L32_3),
			 u32_get_bits(mac_addr_h16, ATH12K_HTT_MAC_ADDR_H16_0),
			 u32_get_bits(mac_addr_h16, ATH12K_HTT_MAC_ADDR_H16_1));

	len += scnprintf(buf + len, buf_len - len, "sw_peer_id = %u\n",
			 le32_to_cpu(htt_stats_buf->sw_peer_id));
	len += scnprintf(buf + len, buf_len - len, "pdev_id = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_PDEV_ID_INFO));
	len += scnprintf(buf + len, buf_len - len, "vdev_id = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_VDEV_ID_INFO));
	len += scnprintf(buf + len, buf_len - len, "next_hop = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_NEXT_HOP_INFO));
	len += scnprintf(buf + len, buf_len - len, "mcast = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_MCAST_INFO));
	len += scnprintf(buf + len, buf_len - len, "monitor_direct = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_MONITOR_DIRECT_INFO));
	len += scnprintf(buf + len, buf_len - len, "mesh_sta = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_MESH_STA_INFO));
	len += scnprintf(buf + len, buf_len - len, "mec = %u\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_MEC_INFO));
	len += scnprintf(buf + len, buf_len - len, "intra_bss = %u\n\n",
			 u32_get_bits(ast_info, ATH12K_HTT_AST_INTRA_BSS_INFO));

	stats_req->buf_len = len;
}

static const char*
ath12k_htt_get_punct_dir_type_str(enum ath12k_htt_stats_direction direction)
{
	switch (direction) {
	case ATH12K_HTT_STATS_DIRECTION_TX:
		return "tx";
	case ATH12K_HTT_STATS_DIRECTION_RX:
		return "rx";
	default:
		return "unknown";
	}
}

static const char*
ath12k_htt_get_punct_ppdu_type_str(enum ath12k_htt_stats_ppdu_type ppdu_type)
{
	switch (ppdu_type) {
	case ATH12K_HTT_STATS_PPDU_TYPE_MODE_SU:
		return "su";
	case ATH12K_HTT_STATS_PPDU_TYPE_DL_MU_MIMO:
		return "dl_mu_mimo";
	case ATH12K_HTT_STATS_PPDU_TYPE_UL_MU_MIMO:
		return "ul_mu_mimo";
	case ATH12K_HTT_STATS_PPDU_TYPE_DL_MU_OFDMA:
		return "dl_mu_ofdma";
	case ATH12K_HTT_STATS_PPDU_TYPE_UL_MU_OFDMA:
		return "ul_mu_ofdma";
	default:
		return "unknown";
	}
}

static const char*
ath12k_htt_get_punct_pream_type_str(enum ath12k_htt_stats_param_type pream_type)
{
	switch (pream_type) {
	case ATH12K_HTT_STATS_PREAM_OFDM:
		return "ofdm";
	case ATH12K_HTT_STATS_PREAM_CCK:
		return "cck";
	case ATH12K_HTT_STATS_PREAM_HT:
		return "ht";
	case ATH12K_HTT_STATS_PREAM_VHT:
		return "ac";
	case ATH12K_HTT_STATS_PREAM_HE:
		return "ax";
	case ATH12K_HTT_STATS_PREAM_EHT:
		return "be";
	default:
		return "unknown";
	}
}

static void
ath12k_htt_print_puncture_stats_tlv(const void *tag_buf, u16 tag_len,
				    struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_puncture_stats_tlv *stats_buf = tag_buf;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 len = stats_req->buf_len;
	u8 *buf = stats_req->buf;
	const char *direction;
	const char *ppdu_type;
	const char *preamble;
	u32 mac_id__word;
	u32 subband_limit;
	u8 i;

	if (tag_len < sizeof(*stats_buf))
		return;

	mac_id__word = le32_to_cpu(stats_buf->mac_id__word);
	subband_limit = min(le32_to_cpu(stats_buf->subband_cnt),
			    ATH12K_HTT_PUNCT_STATS_MAX_SUBBAND_CNT);

	direction = ath12k_htt_get_punct_dir_type_str(le32_to_cpu(stats_buf->direction));
	ppdu_type = ath12k_htt_get_punct_ppdu_type_str(le32_to_cpu(stats_buf->ppdu_type));
	preamble = ath12k_htt_get_punct_pream_type_str(le32_to_cpu(stats_buf->preamble));

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_PUNCTURE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id__word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len,
			 "%s_%s_%s_last_used_pattern_mask = 0x%08x\n",
			 direction, preamble, ppdu_type,
			 le32_to_cpu(stats_buf->last_used_pattern_mask));

	for (i = 0; i < subband_limit; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "%s_%s_%s_num_subbands_used_cnt_%02d = %u\n",
				 direction, preamble, ppdu_type, i + 1,
				 le32_to_cpu(stats_buf->num_subbands_used_cnt[i]));
	}
	len += scnprintf(buf + len, buf_len - len, "\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_dmac_reset_stats_tlv(const void *tag_buf, u16 tag_len,
				      struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_dmac_reset_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u64 time;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_DMAC_RESET_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "reset_count = %u\n",
			 le32_to_cpu(htt_stats_buf->reset_count));
	time = ath12k_le32hilo_to_u64(htt_stats_buf->reset_time_hi_ms,
				      htt_stats_buf->reset_time_lo_ms);
	len += scnprintf(buf + len, buf_len - len, "reset_time_ms = %llu\n", time);
	time = ath12k_le32hilo_to_u64(htt_stats_buf->disengage_time_hi_ms,
				      htt_stats_buf->disengage_time_lo_ms);
	len += scnprintf(buf + len, buf_len - len, "disengage_time_ms = %llu\n", time);

	time = ath12k_le32hilo_to_u64(htt_stats_buf->engage_time_hi_ms,
				      htt_stats_buf->engage_time_lo_ms);
	len += scnprintf(buf + len, buf_len - len, "engage_time_ms = %llu\n", time);

	len += scnprintf(buf + len, buf_len - len, "disengage_count = %u\n",
			 le32_to_cpu(htt_stats_buf->disengage_count));
	len += scnprintf(buf + len, buf_len - len, "engage_count = %u\n",
			 le32_to_cpu(htt_stats_buf->engage_count));
	len += scnprintf(buf + len, buf_len - len, "drain_dest_ring_mask = 0x%x\n\n",
			 le32_to_cpu(htt_stats_buf->drain_dest_ring_mask));

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_sched_algo_ofdma_stats_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_sched_algo_ofdma_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_PDEV_SCHED_ALGO_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += print_array_to_buf(buf, len, "rate_based_dlofdma_enabled_count",
				  htt_stats_buf->rate_based_dlofdma_enabled_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "rate_based_dlofdma_disabled_count",
				  htt_stats_buf->rate_based_dlofdma_disabled_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "rate_based_dlofdma_probing_count",
				  htt_stats_buf->rate_based_dlofdma_disabled_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "rate_based_dlofdma_monitoring_count",
				  htt_stats_buf->rate_based_dlofdma_monitor_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "chan_acc_lat_based_dlofdma_enabled_count",
				  htt_stats_buf->chan_acc_lat_based_dlofdma_enabled_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "chan_acc_lat_based_dlofdma_disabled_count",
				  htt_stats_buf->chan_acc_lat_based_dlofdma_disabled_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "chan_acc_lat_based_dlofdma_monitoring_count",
				  htt_stats_buf->chan_acc_lat_based_dlofdma_monitor_cnt,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "downgrade_to_dl_su_ru_alloc_fail",
				  htt_stats_buf->downgrade_to_dl_su_ru_alloc_fail,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "candidate_list_single_user_disable_ofdma",
				  htt_stats_buf->candidate_list_single_user_disable_ofdma,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "dl_cand_list_dropped_high_ul_qos_weight",
				  htt_stats_buf->dl_cand_list_dropped_high_ul_qos_weight,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "ax_dlofdma_disabled_due_to_pipelining",
				  htt_stats_buf->ax_dlofdma_disabled_due_to_pipelining,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "dlofdma_disabled_su_only_eligible",
				  htt_stats_buf->dlofdma_disabled_su_only_eligible,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "dlofdma_disabled_consec_no_mpdus_tried",
				  htt_stats_buf->dlofdma_disabled_consec_no_mpdus_tried,
				  ATH12K_HTT_NUM_AC_WMM, "\n");
	len += print_array_to_buf(buf, len, "dlofdma_disabled_consec_no_mpdus_success",
				  htt_stats_buf->dlofdma_disabled_consec_no_mpdus_success,
				  ATH12K_HTT_NUM_AC_WMM, "\n\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_tx_pdev_rate_stats_be_ofdma_tlv(const void *tag_buf, u16 tag_len,
						 struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_rate_stats_be_ofdma_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;
	u8 i;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len,
			 "HTT_TX_PDEV_RATE_STATS_BE_OFDMA_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "be_ofdma_tx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->be_ofdma_tx_ldpc));
	len += print_array_to_buf(buf, len, "be_ofdma_tx_mcs",
				  htt_stats_buf->be_ofdma_tx_mcs,
				  ATH12K_HTT_TX_PDEV_NUM_BE_MCS_CNTRS, "\n");
	len += print_array_to_buf(buf, len, "be_ofdma_eht_sig_mcs",
				  htt_stats_buf->be_ofdma_eht_sig_mcs,
				  ATH12K_HTT_TX_PDEV_NUM_EHT_SIG_MCS_CNTRS, "\n");
	len += scnprintf(buf + len, buf_len - len, "be_ofdma_tx_ru_size = ");
	for (i = 0; i < ATH12K_HTT_TX_RX_PDEV_NUM_BE_RU_SIZE_CNTRS; i++)
		len += scnprintf(buf + len, buf_len - len, " %s:%u ",
				 ath12k_htt_be_tx_rx_ru_size_to_str(i),
				 le32_to_cpu(htt_stats_buf->be_ofdma_tx_ru_size[i]));
	len += scnprintf(buf + len, buf_len - len, "\n");
	len += print_array_to_buf_index(buf, len, "be_ofdma_tx_nss = ", 1,
					htt_stats_buf->be_ofdma_tx_nss,
					ATH12K_HTT_PDEV_STAT_NUM_SPATIAL_STREAMS,
					"\n");
	len += print_array_to_buf(buf, len, "be_ofdma_tx_bw",
				  htt_stats_buf->be_ofdma_tx_bw,
				  ATH12K_HTT_TX_PDEV_NUM_BE_BW_CNTRS, "\n");
	for (i = 0; i < ATH12K_HTT_TX_PDEV_NUM_GI_CNTRS; i++) {
		len += scnprintf(buf + len, buf_len - len,
				 "be_ofdma_tx_gi[%u]", i);
		len += print_array_to_buf(buf, len, "", htt_stats_buf->gi[i],
					  ATH12K_HTT_TX_PDEV_NUM_BE_MCS_CNTRS, "\n");
	}
	len += scnprintf(buf + len, buf_len - len, "\n");

	stats_req->buf_len = len;
}

static void
ath12k_htt_print_pdev_mbssid_ctrl_frame_stats_tlv(const void *tag_buf, u16 tag_len,
						  struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_pdev_mbssid_ctrl_frame_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id__word);

	len += scnprintf(buf + len, buf_len - len, "HTT_MBSSID_CTRL_FRAME_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "basic_trigger_across_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->basic_trigger_across_bss));
	len += scnprintf(buf + len, buf_len - len, "basic_trigger_within_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->basic_trigger_within_bss));
	len += scnprintf(buf + len, buf_len - len, "bsr_trigger_across_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->bsr_trigger_across_bss));
	len += scnprintf(buf + len, buf_len - len, "bsr_trigger_within_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->bsr_trigger_within_bss));
	len += scnprintf(buf + len, buf_len - len, "mu_rts_across_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_rts_across_bss));
	len += scnprintf(buf + len, buf_len - len, "mu_rts_within_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->mu_rts_within_bss));
	len += scnprintf(buf + len, buf_len - len, "ul_mumimo_trigger_across_bss = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_mumimo_trigger_across_bss));
	len += scnprintf(buf + len, buf_len - len,
			 "ul_mumimo_trigger_within_bss = %u\n\n",
			 le32_to_cpu(htt_stats_buf->ul_mumimo_trigger_within_bss));

	stats_req->buf_len = len;
}

static inline void
ath12k_htt_print_tx_pdev_rate_stats_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_tx_pdev_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 i, j;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id_word);

	len += scnprintf(buf + len, buf_len - len, "HTT_TX_PDEV_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "tx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->tx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_tx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ac_mu_mimo_tx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_tx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ax_mu_mimo_tx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "ofdma_tx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ofdma_tx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rts_cnt));
	len += scnprintf(buf + len, buf_len - len, "rts_success = %u\n",
			 le32_to_cpu(htt_stats_buf->rts_success));
	len += scnprintf(buf + len, buf_len - len, "ack_rssi = %u\n",
			 le32_to_cpu(htt_stats_buf->ack_rssi));
	len += scnprintf(buf + len, buf_len - len,
			 "Legacy CCK Rates: 1 Mbps: %u, 2 Mbps: %u, 5.5 Mbps: %u, 12 Mbps: %u\n",
			 le32_to_cpu(htt_stats_buf->tx_legacy_cck_rate[0]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_cck_rate[1]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_cck_rate[2]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_cck_rate[3]));
	len += scnprintf(buf + len, buf_len - len,
			 "Legacy OFDM Rates: 6 Mbps: %u, 9 Mbps: %u, 12 Mbps: %u, 18 Mbps: %u\n"
			 "                   24 Mbps: %u, 36 Mbps: %u, 48 Mbps: %u, 54 Mbps: %u\n",
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[0]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[1]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[2]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[3]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[4]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[5]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[6]),
			 le32_to_cpu(htt_stats_buf->tx_legacy_ofdm_rate[7]));
	len += scnprintf(buf + len, buf_len - len, "HE LTF: 1x: %u, 2x: %u, 4x: %u\n",
			 le32_to_cpu(htt_stats_buf->tx_he_ltf[1]),
			 le32_to_cpu(htt_stats_buf->tx_he_ltf[2]),
			 le32_to_cpu(htt_stats_buf->tx_he_ltf[3]));

	len += print_array_to_buf(buf, len, "tx_mcs", htt_stats_buf->tx_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, NULL);
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->tx_mcs_ext[j]));
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS +
				 ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->tx_mcs_ext_2[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += print_array_to_buf(buf, len, "ax_mu_mimo_tx_mcs",
				  htt_stats_buf->ax_mu_mimo_tx_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, NULL);
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->ax_mu_mimo_tx_mcs_ext[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += print_array_to_buf(buf, len, "ofdma_tx_mcs",
				  htt_stats_buf->ofdma_tx_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, NULL);
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->ofdma_tx_mcs_ext[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "tx_nss =");
	for (j = 1; j <= ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,",
				 j, le32_to_cpu(htt_stats_buf->tx_nss[j - 1]));
	len--;
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "ac_mu_mimo_tx_nss =");
	for (j = 1; j <= ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,",
				 j, le32_to_cpu(htt_stats_buf->ac_mu_mimo_tx_nss[j - 1]));
	len--;
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "ax_mu_mimo_tx_nss =");
	for (j = 1; j <= ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,",
				 j, le32_to_cpu(htt_stats_buf->ax_mu_mimo_tx_nss[j - 1]));
	len--;
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += scnprintf(buf + len, buf_len - len, "ofdma_tx_nss =");
	for (j = 1; j <= ATH12K_HTT_TX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++)
		len += scnprintf(buf + len, buf_len - len, " %u:%u,",
				 j, le32_to_cpu(htt_stats_buf->ofdma_tx_nss[j - 1]));
	len--;
	len += scnprintf(buf + len, buf_len - len, "\n");

	len += print_array_to_buf(buf, len, "tx_bw", htt_stats_buf->tx_bw,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, NULL);
	len += scnprintf(buf + len, buf_len - len, ", %u:%u\n",
			 ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS,
			 le32_to_cpu(htt_stats_buf->tx_bw_320mhz));

	len += print_array_to_buf(buf, len, "tx_stbc",
				  htt_stats_buf->tx_stbc,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, NULL);
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->tx_stbc_ext[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "tx_gi[%u] =", j);
		len += print_array_to_buf(buf, len, NULL, htt_stats_buf->tx_gi[j],
					  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					  NULL);
		for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; i++)
			len += scnprintf(buf + len, buf_len - len, ", %u:%u",
					 i + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					 le32_to_cpu(htt_stats_buf->tx_gi_ext[j][i]));
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ac_mu_mimo_tx_gi[%u] =", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->ac_mu_mimo_tx_gi[j],
					  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					  "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ax_mu_mimo_tx_gi[%u] =", j);
		len += print_array_to_buf(buf, len, NULL, htt_stats_buf->ax_mimo_tx_gi[j],
					  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					  NULL);
		for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; i++)
			len += scnprintf(buf + len, buf_len - len, ", %u:%u",
					 i + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					 le32_to_cpu(htt_stats_buf->ax_tx_gi_ext[j][i]));
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, (buf_len - len),
				 "ofdma_tx_gi[%u] = ", j);
		len += print_array_to_buf(buf, len, NULL, htt_stats_buf->ofdma_tx_gi[j],
					  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					  NULL);
		for (i = 0; i < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; i++)
			len += scnprintf(buf + len, buf_len - len, ", %u:%u",
					 i + ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS,
					 le32_to_cpu(htt_stats_buf->ofd_tx_gi_ext[j][i]));
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	len += print_array_to_buf(buf, len, "tx_su_mcs", htt_stats_buf->tx_su_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "tx_mu_mcs", htt_stats_buf->tx_mu_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_tx_mcs",
				  htt_stats_buf->ac_mu_mimo_tx_mcs,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "ac_mu_mimo_tx_bw",
				  htt_stats_buf->ac_mu_mimo_tx_bw,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "ax_mu_mimo_tx_bw",
				  htt_stats_buf->ax_mu_mimo_tx_bw,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "ofdma_tx_bw",
				  htt_stats_buf->ofdma_tx_bw,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "tx_pream", htt_stats_buf->tx_pream,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");
	len += print_array_to_buf(buf, len, "tx_dcm", htt_stats_buf->tx_dcm,
				  ATH12K_HTT_TX_PDEV_STATS_NUM_DCM_COUNTERS, "\n");

	stats_req->buf_len = len;
}

static inline void
ath12k_htt_print_rx_pdev_rate_stats_tlv(const void *tag_buf, u16 tag_len,
					struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_pdev_rate_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 i, j;
	u32 mac_id_word;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	mac_id_word = le32_to_cpu(htt_stats_buf->mac_id_word);

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_RATE_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "mac_id = %u\n",
			 u32_get_bits(mac_id_word, ATH12K_HTT_STATS_MAC_ID));
	len += scnprintf(buf + len, buf_len - len, "nsts = %u\n",
			 le32_to_cpu(htt_stats_buf->nsts));
	len += scnprintf(buf + len, buf_len - len, "rx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "rts_cnt = %u\n",
			 le32_to_cpu(htt_stats_buf->rts_cnt));
	len += scnprintf(buf + len, buf_len - len, "rssi_mgmt = %u\n",
			 le32_to_cpu(htt_stats_buf->rssi_mgmt));
	len += scnprintf(buf + len, buf_len - len, "rssi_data = %u\n",
			 le32_to_cpu(htt_stats_buf->rssi_data));
	len += scnprintf(buf + len, buf_len - len, "rssi_comb = %u\n",
			 le32_to_cpu(htt_stats_buf->rssi_comb));
	len += scnprintf(buf + len, buf_len - len, "rssi_in_dbm = %d\n",
			 le32_to_cpu(htt_stats_buf->rssi_in_dbm));
	len += scnprintf(buf + len, buf_len - len, "rx_evm_nss_count = %u\n",
			 le32_to_cpu(htt_stats_buf->nss_count));
	len += scnprintf(buf + len, buf_len - len, "rx_evm_pilot_count = %u\n",
			 le32_to_cpu(htt_stats_buf->pilot_count));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_su_ext = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_su_ext));
	len += scnprintf(buf + len, buf_len - len, "rx_11ac_mumimo = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ac_mumimo));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_mumimo = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_mumimo));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ofdma = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_ofdma));
	len += scnprintf(buf + len, buf_len - len, "txbf = %u\n",
			 le32_to_cpu(htt_stats_buf->txbf));
	len += scnprintf(buf + len, buf_len - len, "rx_su_ndpa = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_su_ndpa));
	len += scnprintf(buf + len, buf_len - len, "rx_mu_ndpa = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_mu_ndpa));
	len += scnprintf(buf + len, buf_len - len, "rx_br_poll = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_br_poll));
	len += scnprintf(buf + len, buf_len - len, "rx_active_dur_us_low = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_active_dur_us_low));
	len += scnprintf(buf + len, buf_len - len, "rx_active_dur_us_high = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_active_dur_us_high));
	len += scnprintf(buf + len, buf_len - len, "rx_11ax_ul_ofdma = %u\n",
			 le32_to_cpu(htt_stats_buf->rx_11ax_ul_ofdma));
	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_stbc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_rx_stbc));
	len += scnprintf(buf + len, buf_len - len, "ul_ofdma_rx_ldpc = %u\n",
			 le32_to_cpu(htt_stats_buf->ul_ofdma_rx_ldpc));
	len += scnprintf(buf + len, buf_len - len, "per_chain_rssi_pkt_type = %#x\n",
			 le32_to_cpu(htt_stats_buf->per_chain_rssi_pkt_type));

	len += print_array_to_buf(buf, len, "rx_nss", htt_stats_buf->rx_nss,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	len += print_array_to_buf(buf, len, "rx_dcm", htt_stats_buf->rx_dcm,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_DCM_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_stbc", htt_stats_buf->rx_stbc,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_bw", htt_stats_buf->rx_bw,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_pream", htt_stats_buf->rx_pream,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_PREAMBLE_TYPES, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_su_txbf_mcs",
				  htt_stats_buf->rx_11ax_su_txbf_mcs,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_mu_txbf_mcs",
				  htt_stats_buf->rx_11ax_mu_txbf_mcs,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_legacy_cck_rate",
				  htt_stats_buf->rx_legacy_cck_rate,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_CCK_STATS, "\n");
	len += print_array_to_buf(buf, len, "rx_legacy_ofdm_rate",
				  htt_stats_buf->rx_legacy_ofdm_rate,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_LEGACY_OFDM_STATS, "\n");
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_mcs",
				  htt_stats_buf->ul_ofdma_rx_mcs,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_nss",
				  htt_stats_buf->ul_ofdma_rx_nss,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS, "\n");
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_bw",
				  htt_stats_buf->ul_ofdma_rx_bw,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_non_data_ppdu",
				  htt_stats_buf->rx_ulofdma_non_data_ppdu,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_data_ppdu",
				  htt_stats_buf->rx_ulofdma_data_ppdu,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_mpdu_ok",
				  htt_stats_buf->rx_ulofdma_mpdu_ok,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_mpdu_fail",
				  htt_stats_buf->rx_ulofdma_mpdu_fail,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_non_data_nusers",
				  htt_stats_buf->rx_ulofdma_non_data_nusers,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulofdma_data_nusers",
				  htt_stats_buf->rx_ulofdma_data_nusers,
				  ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_dl_ofdma_mcs",
				  htt_stats_buf->rx_11ax_dl_ofdma_mcs,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_dl_ofdma_ru",
				  htt_stats_buf->rx_11ax_dl_ofdma_ru,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_RU_SIZE_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_ulmumimo_non_data_ppdu",
				  htt_stats_buf->rx_ulmumimo_non_data_ppdu,
				  ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulmumimo_data_ppdu",
				  htt_stats_buf->rx_ulmumimo_data_ppdu,
				  ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulmumimo_mpdu_ok",
				  htt_stats_buf->rx_ulmumimo_mpdu_ok,
				  ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER, "\n");
	len += print_array_to_buf(buf, len, "rx_ulmumimo_mpdu_fail",
				  htt_stats_buf->rx_ulmumimo_mpdu_fail,
				  ATH12K_HTT_RX_PDEV_MAX_ULMUMIMO_NUM_USER, "\n");

	len += print_array_to_buf(buf, len, "rx_mcs",
				  htt_stats_buf->rx_mcs,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS, NULL);
	for (j = 0; j < ATH12K_HTT_TX_PDEV_STATS_NUM_EXTRA_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS,
				 le32_to_cpu(htt_stats_buf->rx_mcs_ext[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "pilot_evm_db[%u] =", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->rx_pil_evm_db[j],
					  ATH12K_HTT_RX_PDEV_STATS_RXEVM_MAX_PILOTS_NSS,
					  "\n");
	}

	len += scnprintf(buf + len, buf_len - len, "pilot_evm_db_mean =");
	for (i = 0; i < ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; i++)
		len += scnprintf(buf + len,
				 buf_len - len,
				 " %u:%d,", i,
				 le32_to_cpu(htt_stats_buf->rx_pilot_evm_db_mean[i]));
	len--;
	len += scnprintf(buf + len, buf_len - len, "\n");

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rssi_chain_in_db[%u] = ", j);
		for (i = 0; i < ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u: %d,", i,
					 htt_stats_buf->rssi_chain_in_db[j][i]);
		len--;
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_gi[%u] = ", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->rx_gi[j],
					  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS,
					  "\n");
	}

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ul_ofdma_rx_gi[%u] = ", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->ul_ofdma_rx_gi[j],
					  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS,
					  "\n");
	}

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_ul_fd_rssi: nss[%u] = ", j);
		for (i = 0; i < ATH12K_HTT_RX_PDEV_MAX_OFDMA_NUM_USER; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u:%d,",
					 i, htt_stats_buf->rx_ul_fd_rssi[j][i]);
		len--;
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_SPATIAL_STREAMS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_per_chain_rssi_in_dbm[%u] =", j);
		for (i = 0; i < ATH12K_HTT_RX_PDEV_STATS_NUM_BW_COUNTERS; i++)
			len += scnprintf(buf + len,
					 buf_len - len,
					 " %u:%d,",
					 i,
					 htt_stats_buf->rx_per_chain_rssi_in_dbm[j][i]);
		len--;
		len += scnprintf(buf + len, buf_len - len, "\n");
	}

	stats_req->buf_len = len;
}

static inline void
ath12k_htt_print_rx_pdev_rate_ext_stats_tlv(const void *tag_buf, u16 tag_len,
					    struct debug_htt_stats_req *stats_req)
{
	const struct ath12k_htt_rx_pdev_rate_ext_stats_tlv *htt_stats_buf = tag_buf;
	u8 *buf = stats_req->buf;
	u32 len = stats_req->buf_len;
	u32 buf_len = ATH12K_HTT_STATS_BUF_SIZE;
	u8 j;

	if (tag_len < sizeof(*htt_stats_buf))
		return;

	len += scnprintf(buf + len, buf_len - len, "HTT_RX_PDEV_RATE_EXT_STATS_TLV:\n");
	len += scnprintf(buf + len, buf_len - len, "rssi_mgmt_in_dbm = %d\n",
			 le32_to_cpu(htt_stats_buf->rssi_mgmt_in_dbm));

	len += print_array_to_buf(buf, len, "rx_stbc_ext",
				  htt_stats_buf->rx_stbc_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT, "\n");
	len += print_array_to_buf(buf, len, "ul_ofdma_rx_mcs_ext",
				  htt_stats_buf->ul_ofdma_rx_mcs_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_su_txbf_mcs_ext",
				  htt_stats_buf->rx_11ax_su_txbf_mcs_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_mu_txbf_mcs_ext",
				  htt_stats_buf->rx_11ax_mu_txbf_mcs_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT, "\n");
	len += print_array_to_buf(buf, len, "rx_11ax_dl_ofdma_mcs_ext",
				  htt_stats_buf->rx_11ax_dl_ofdma_mcs_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT, "\n");
	len += print_array_to_buf(buf, len, "rx_bw_ext",
				  htt_stats_buf->rx_bw_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_BW_EXT2_COUNTERS, "\n");
	len += print_array_to_buf(buf, len, "rx_su_punctured_mode",
				  htt_stats_buf->rx_su_punctured_mode,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_PUNCTURED_MODE_COUNTERS,
				  "\n");

	len += print_array_to_buf(buf, len, "rx_mcs_ext",
				  htt_stats_buf->rx_mcs_ext,
				  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT,
				  NULL);
	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_EXTRA2_MCS_COUNTERS; j++)
		len += scnprintf(buf + len, buf_len - len, ", %u:%u",
				 j + ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT,
				 le32_to_cpu(htt_stats_buf->rx_mcs_ext_2[j]));
	len += scnprintf(buf + len, buf_len - len, "\n");

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "rx_gi_ext[%u] = ", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->rx_gi_ext[j],
					  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT,
					  "\n");
	}

	for (j = 0; j < ATH12K_HTT_RX_PDEV_STATS_NUM_GI_COUNTERS; j++) {
		len += scnprintf(buf + len, buf_len - len,
				 "ul_ofdma_rx_gi_ext[%u] = ", j);
		len += print_array_to_buf(buf, len, NULL,
					  htt_stats_buf->ul_ofdma_rx_gi_ext[j],
					  ATH12K_HTT_RX_PDEV_STATS_NUM_MCS_COUNTERS_EXT,
					  "\n");
	}

	stats_req->buf_len = len;
}

static int ath12k_dbg_htt_ext_stats_parse(struct ath12k_base *ab,
					  u16 tag, u16 len, const void *tag_buf,
					  void *user_data)
{
	struct debug_htt_stats_req *stats_req = user_data;

	switch (tag) {
	case HTT_STATS_TX_PDEV_CMN_TAG:
		htt_print_tx_pdev_stats_cmn_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_UNDERRUN_TAG:
		htt_print_tx_pdev_stats_urrn_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_SIFS_TAG:
		htt_print_tx_pdev_stats_sifs_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_FLUSH_TAG:
		htt_print_tx_pdev_stats_flush_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_SIFS_HIST_TAG:
		htt_print_tx_pdev_stats_sifs_hist_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_CTRL_PATH_TX_STATS_TAG:
		htt_print_pdev_ctrl_path_tx_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_MU_PPDU_DIST_TAG:
		htt_print_tx_pdev_mu_ppdu_dist_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SCHED_CMN_TAG:
		ath12k_htt_print_stats_tx_sched_cmn_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_SCHEDULER_TXQ_STATS_TAG:
		ath12k_htt_print_tx_pdev_stats_sched_per_txq_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SCHED_TXQ_CMD_POSTED_TAG:
		ath12k_htt_print_sched_txq_cmd_posted_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SCHED_TXQ_CMD_REAPED_TAG:
		ath12k_htt_print_sched_txq_cmd_reaped_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SCHED_TXQ_SCHED_ORDER_SU_TAG:
		ath12k_htt_print_sched_txq_sched_order_su_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SCHED_TXQ_SCHED_INELIGIBILITY_TAG:
		ath12k_htt_print_sched_txq_sched_ineligibility_tlv(tag_buf, len,
								   stats_req);
		break;
	case HTT_STATS_SCHED_TXQ_SUPERCYCLE_TRIGGER_TAG:
		ath12k_htt_print_sched_txq_supercycle_trigger_tlv(tag_buf, len,
								  stats_req);
		break;
	case HTT_STATS_HW_PDEV_ERRS_TAG:
		ath12k_htt_print_hw_stats_pdev_errs_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_HW_INTR_MISC_TAG:
		ath12k_htt_print_hw_stats_intr_misc_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_WHAL_TX_TAG:
		ath12k_htt_print_hw_stats_whal_tx_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_HW_WAR_TAG:
		ath12k_htt_print_hw_war_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_CMN_TAG:
		ath12k_htt_print_tx_tqm_cmn_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_ERROR_STATS_TAG:
		ath12k_htt_print_tx_tqm_error_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_GEN_MPDU_TAG:
		ath12k_htt_print_tx_tqm_gen_mpdu_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_LIST_MPDU_TAG:
		ath12k_htt_print_tx_tqm_list_mpdu_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_LIST_MPDU_CNT_TAG:
		ath12k_htt_print_tx_tqm_list_mpdu_cnt_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_TQM_PDEV_TAG:
		ath12k_htt_print_tx_tqm_pdev_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_CMN_TAG:
		ath12k_htt_print_tx_de_cmn_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_EAPOL_PACKETS_TAG:
		ath12k_htt_print_tx_de_eapol_packets_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_CLASSIFY_STATS_TAG:
		ath12k_htt_print_tx_de_classify_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_CLASSIFY_FAILED_TAG:
		ath12k_htt_print_tx_de_classify_failed_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_CLASSIFY_STATUS_TAG:
		ath12k_htt_print_tx_de_classify_status_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_ENQUEUE_PACKETS_TAG:
		ath12k_htt_print_tx_de_enqueue_packets_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_ENQUEUE_DISCARD_TAG:
		ath12k_htt_print_tx_de_enqueue_discard_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_DE_COMPL_STATS_TAG:
		ath12k_htt_print_tx_de_compl_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_CMN_STATS_TAG:
		ath12k_htt_print_tx_selfgen_cmn_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AC_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ac_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AX_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ax_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_BE_STATS_TAG:
		ath12k_htt_print_tx_selfgen_be_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AC_ERR_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ac_err_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AX_ERR_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ax_err_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_BE_ERR_STATS_TAG:
		ath12k_htt_print_tx_selfgen_be_err_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AC_SCHED_STATUS_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ac_sched_status_stats_tlv(tag_buf, len,
								      stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_AX_SCHED_STATUS_STATS_TAG:
		ath12k_htt_print_tx_selfgen_ax_sched_status_stats_tlv(tag_buf, len,
								      stats_req);
		break;
	case HTT_STATS_TX_SELFGEN_BE_SCHED_STATUS_STATS_TAG:
		ath12k_htt_print_tx_selfgen_be_sched_status_stats_tlv(tag_buf, len,
								      stats_req);
		break;
	case HTT_STATS_STRING_TAG:
		ath12k_htt_print_stats_string_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SRING_STATS_TAG:
		ath12k_htt_print_sring_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SFM_CMN_TAG:
		ath12k_htt_print_sfm_cmn_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SFM_CLIENT_TAG:
		ath12k_htt_print_sfm_client_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SFM_CLIENT_USER_TAG:
		ath12k_htt_print_sfm_client_user_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_MU_MIMO_STATS_TAG:
		ath12k_htt_print_tx_pdev_mu_mimo_sch_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_MUMIMO_GRP_STATS_TAG:
		ath12k_htt_print_tx_pdev_mumimo_grp_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_MPDU_STATS_TAG:
		ath12k_htt_print_tx_pdev_mu_mimo_mpdu_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_CCA_1SEC_HIST_TAG:
	case HTT_STATS_PDEV_CCA_100MSEC_HIST_TAG:
	case HTT_STATS_PDEV_CCA_STAT_CUMULATIVE_TAG:
		ath12k_htt_print_pdev_cca_stats_hist_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_CCA_COUNTERS_TAG:
		ath12k_htt_print_pdev_stats_cca_counters_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_SOUNDING_STATS_TAG:
		ath12k_htt_print_tx_sounding_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_OBSS_PD_TAG:
		ath12k_htt_print_pdev_obss_pd_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_LATENCY_CTX_TAG:
		ath12k_htt_print_latency_prof_ctx_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_LATENCY_CNT_TAG:
		ath12k_htt_print_latency_prof_cnt(tag_buf, len, stats_req);
		break;
	case HTT_STATS_LATENCY_PROF_STATS_TAG:
		ath12k_htt_print_latency_prof_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_PDEV_UL_TRIG_STATS_TAG:
		ath12k_htt_print_ul_ofdma_trigger_stats(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_PDEV_UL_OFDMA_USER_STATS_TAG:
		ath12k_htt_print_ul_ofdma_user_stats(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_PDEV_UL_MUMIMO_TRIG_STATS_TAG:
		ath12k_htt_print_ul_mumimo_trig_stats(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_FSE_STATS_TAG:
		ath12k_htt_print_rx_fse_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_TX_RATE_TXBF_STATS_TAG:
		ath12k_htt_print_pdev_tx_rate_txbf_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_AX_NDPA_STATS_TAG:
		ath12k_htt_print_txbf_ofdma_ax_ndpa_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_AX_NDP_STATS_TAG:
		ath12k_htt_print_txbf_ofdma_ax_ndp_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_AX_BRP_STATS_TAG:
		ath12k_htt_print_txbf_ofdma_ax_brp_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_AX_STEER_STATS_TAG:
		ath12k_htt_print_txbf_ofdma_ax_steer_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TXBF_OFDMA_AX_STEER_MPDU_STATS_TAG:
		ath12k_htt_print_txbf_ofdma_ax_steer_mpdu_stats_tlv(tag_buf, len,
								    stats_req);
		break;
	case HTT_STATS_DLPAGER_STATS_TAG:
		ath12k_htt_print_dlpager_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PHY_STATS_TAG:
		ath12k_htt_print_phy_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PHY_COUNTERS_TAG:
		ath12k_htt_print_phy_counters_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PHY_RESET_STATS_TAG:
		ath12k_htt_print_phy_reset_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PHY_RESET_COUNTERS_TAG:
		ath12k_htt_print_phy_reset_counters_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PHY_TPC_STATS_TAG:
		ath12k_htt_print_phy_tpc_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_SOC_TXRX_STATS_COMMON_TAG:
		ath12k_htt_print_soc_txrx_stats_common_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PER_RATE_STATS_TAG:
		ath12k_htt_print_tx_per_rate_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_AST_ENTRY_TAG:
		ath12k_htt_print_ast_entry_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_PUNCTURE_STATS_TAG:
		ath12k_htt_print_puncture_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_DMAC_RESET_STATS_TAG:
		ath12k_htt_print_dmac_reset_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_SCHED_ALGO_OFDMA_STATS_TAG:
		ath12k_htt_print_pdev_sched_algo_ofdma_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_TX_PDEV_RATE_STATS_BE_OFDMA_TAG:
		ath12k_htt_print_tx_pdev_rate_stats_be_ofdma_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_PDEV_MBSSID_CTRL_FRAME_STATS_TAG:
		ath12k_htt_print_pdev_mbssid_ctrl_frame_stats_tlv(tag_buf, len,
								  stats_req);
		break;
	case HTT_STATS_TX_PDEV_RATE_STATS_TAG:
		ath12k_htt_print_tx_pdev_rate_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_PDEV_RATE_STATS_TAG:
		ath12k_htt_print_rx_pdev_rate_stats_tlv(tag_buf, len, stats_req);
		break;
	case HTT_STATS_RX_PDEV_RATE_EXT_STATS_TAG:
		ath12k_htt_print_rx_pdev_rate_ext_stats_tlv(tag_buf, len, stats_req);
		break;
	default:
		break;
	}

	return 0;
}

void ath12k_debugfs_htt_ext_stats_handler(struct ath12k_base *ab,
					  struct sk_buff *skb)
{
	struct ath12k_htt_extd_stats_msg *msg;
	struct debug_htt_stats_req *stats_req;
	struct ath12k *ar;
	u32 len, pdev_id, stats_info;
	u64 cookie;
	int ret;
	bool send_completion = false;

	msg = (struct ath12k_htt_extd_stats_msg *)skb->data;
	cookie = le64_to_cpu(msg->cookie);

	if (u64_get_bits(cookie, ATH12K_HTT_STATS_COOKIE_MSB) !=
			 ATH12K_HTT_STATS_MAGIC_VALUE) {
		ath12k_warn(ab, "received invalid htt ext stats event\n");
		return;
	}

	pdev_id = u64_get_bits(cookie, ATH12K_HTT_STATS_COOKIE_LSB);
	rcu_read_lock();
	ar = ath12k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ath12k_warn(ab, "failed to get ar for pdev_id %d\n", pdev_id);
		goto exit;
	}

	stats_req = ar->debug.htt_stats.stats_req;
	if (!stats_req)
		goto exit;

	spin_lock_bh(&ar->data_lock);

	stats_info = le32_to_cpu(msg->info1);
	stats_req->done = u32_get_bits(stats_info, ATH12K_HTT_T2H_EXT_STATS_INFO1_DONE);
	if (stats_req->done)
		send_completion = true;

	spin_unlock_bh(&ar->data_lock);

	len = u32_get_bits(stats_info, ATH12K_HTT_T2H_EXT_STATS_INFO1_LENGTH);
	if (len > skb->len) {
		ath12k_warn(ab, "invalid length %d for HTT stats", len);
		goto exit;
	}

	ret = ath12k_dp_htt_tlv_iter(ab, msg->data, len,
				     ath12k_dbg_htt_ext_stats_parse,
				     stats_req);
	if (ret)
		ath12k_warn(ab, "Failed to parse tlv %d\n", ret);

	if (send_completion)
		complete(&stats_req->htt_stats_rcvd);
exit:
	rcu_read_unlock();
}

static ssize_t ath12k_read_htt_stats_type(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	char buf[32];
	size_t len;

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);
	type = ar->debug.htt_stats.type;
	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	len = scnprintf(buf, sizeof(buf), "%u\n", type);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath12k_write_htt_stats_type(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	unsigned int cfg_param[4] = {0};
	const int size = 32;
	int num_args;

	char *buf __free(kfree) = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	num_args = sscanf(buf, "%u %u %u %u %u\n", &type, &cfg_param[0],
			  &cfg_param[1], &cfg_param[2], &cfg_param[3]);
	if (!num_args || num_args > 5)
		return -EINVAL;

	if (type == ATH12K_DBG_HTT_EXT_STATS_RESET ||
	    type >= ATH12K_DBG_HTT_NUM_EXT_STATS)
		return -EINVAL;

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);

	ar->debug.htt_stats.type = type;
	ar->debug.htt_stats.cfg_param[0] = cfg_param[0];
	ar->debug.htt_stats.cfg_param[1] = cfg_param[1];
	ar->debug.htt_stats.cfg_param[2] = cfg_param[2];
	ar->debug.htt_stats.cfg_param[3] = cfg_param[3];

	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	return count;
}

static const struct file_operations fops_htt_stats_type = {
	.read = ath12k_read_htt_stats_type,
	.write = ath12k_write_htt_stats_type,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath12k_debugfs_htt_stats_req(struct ath12k *ar)
{
	struct debug_htt_stats_req *stats_req = ar->debug.htt_stats.stats_req;
	enum ath12k_dbg_htt_ext_stats_type type = stats_req->type;
	u64 cookie;
	int ret, pdev_id;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	init_completion(&stats_req->htt_stats_rcvd);

	pdev_id = ath12k_mac_get_target_pdev_id(ar);
	stats_req->done = false;
	stats_req->pdev_id = pdev_id;

	cookie = u64_encode_bits(ATH12K_HTT_STATS_MAGIC_VALUE,
				 ATH12K_HTT_STATS_COOKIE_MSB);
	cookie |= u64_encode_bits(pdev_id, ATH12K_HTT_STATS_COOKIE_LSB);

	if (stats_req->override_cfg_param) {
		cfg_params.cfg0 = stats_req->cfg_param[0];
		cfg_params.cfg1 = stats_req->cfg_param[1];
		cfg_params.cfg2 = stats_req->cfg_param[2];
		cfg_params.cfg3 = stats_req->cfg_param[3];
	}

	ret = ath12k_dp_tx_htt_h2t_ext_stats_req(ar, type, &cfg_params, cookie);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		return ret;
	}
	if (!wait_for_completion_timeout(&stats_req->htt_stats_rcvd, 3 * HZ)) {
		spin_lock_bh(&ar->data_lock);
		if (!stats_req->done) {
			stats_req->done = true;
			spin_unlock_bh(&ar->data_lock);
			ath12k_warn(ar->ab, "stats request timed out\n");
			return -ETIMEDOUT;
		}
		spin_unlock_bh(&ar->data_lock);
	}

	return 0;
}

static int ath12k_open_htt_stats(struct inode *inode,
				 struct file *file)
{
	struct ath12k *ar = inode->i_private;
	struct debug_htt_stats_req *stats_req;
	enum ath12k_dbg_htt_ext_stats_type type = ar->debug.htt_stats.type;
	struct ath12k_hw *ah = ath12k_ar_to_ah(ar);
	int ret;

	if (type == ATH12K_DBG_HTT_EXT_STATS_RESET)
		return -EPERM;

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);

	if (ah->state != ATH12K_HW_STATE_ON) {
		ret = -ENETDOWN;
		goto err_unlock;
	}

	if (ar->debug.htt_stats.stats_req) {
		ret = -EAGAIN;
		goto err_unlock;
	}

	stats_req = kzalloc(sizeof(*stats_req) + ATH12K_HTT_STATS_BUF_SIZE, GFP_KERNEL);
	if (!stats_req) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	ar->debug.htt_stats.stats_req = stats_req;
	stats_req->type = type;
	stats_req->cfg_param[0] = ar->debug.htt_stats.cfg_param[0];
	stats_req->cfg_param[1] = ar->debug.htt_stats.cfg_param[1];
	stats_req->cfg_param[2] = ar->debug.htt_stats.cfg_param[2];
	stats_req->cfg_param[3] = ar->debug.htt_stats.cfg_param[3];
	stats_req->override_cfg_param = !!stats_req->cfg_param[0] ||
					!!stats_req->cfg_param[1] ||
					!!stats_req->cfg_param[2] ||
					!!stats_req->cfg_param[3];

	ret = ath12k_debugfs_htt_stats_req(ar);
	if (ret < 0)
		goto out;

	file->private_data = stats_req;

	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	return 0;
out:
	kfree(stats_req);
	ar->debug.htt_stats.stats_req = NULL;
err_unlock:
	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	return ret;
}

static int ath12k_release_htt_stats(struct inode *inode,
				    struct file *file)
{
	struct ath12k *ar = inode->i_private;

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);
	kfree(file->private_data);
	ar->debug.htt_stats.stats_req = NULL;
	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	return 0;
}

static ssize_t ath12k_read_htt_stats(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct debug_htt_stats_req *stats_req = file->private_data;
	char *buf;
	u32 length;

	buf = stats_req->buf;
	length = min_t(u32, stats_req->buf_len, ATH12K_HTT_STATS_BUF_SIZE);
	return simple_read_from_buffer(user_buf, count, ppos, buf, length);
}

static const struct file_operations fops_dump_htt_stats = {
	.open = ath12k_open_htt_stats,
	.release = ath12k_release_htt_stats,
	.read = ath12k_read_htt_stats,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath12k_write_htt_stats_reset(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct ath12k *ar = file->private_data;
	enum ath12k_dbg_htt_ext_stats_type type;
	struct htt_ext_stats_cfg_params cfg_params = { 0 };
	u8 param_pos;
	int ret;

	ret = kstrtou32_from_user(user_buf, count, 0, &type);
	if (ret)
		return ret;

	if (type >= ATH12K_DBG_HTT_NUM_EXT_STATS ||
	    type == ATH12K_DBG_HTT_EXT_STATS_RESET)
		return -E2BIG;

	wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);
	cfg_params.cfg0 = HTT_STAT_DEFAULT_RESET_START_OFFSET;
	param_pos = (type >> 5) + 1;

	switch (param_pos) {
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_32_BYTES:
		cfg_params.cfg1 = 1 << (cfg_params.cfg0 + type);
		break;
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_64_BYTES:
		cfg_params.cfg2 = ATH12K_HTT_STATS_RESET_BITMAP32_BIT(cfg_params.cfg0 +
								      type);
		break;
	case ATH12K_HTT_STATS_RESET_PARAM_CFG_128_BYTES:
		cfg_params.cfg3 = ATH12K_HTT_STATS_RESET_BITMAP64_BIT(cfg_params.cfg0 +
								      type);
		break;
	default:
		break;
	}

	ret = ath12k_dp_tx_htt_h2t_ext_stats_req(ar,
						 ATH12K_DBG_HTT_EXT_STATS_RESET,
						 &cfg_params,
						 0ULL);
	if (ret) {
		ath12k_warn(ar->ab, "failed to send htt stats request: %d\n", ret);
		wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);
		return ret;
	}

	ar->debug.htt_stats.reset = type;
	wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);

	return count;
}

static const struct file_operations fops_htt_stats_reset = {
	.write = ath12k_write_htt_stats_reset,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath12k_debugfs_htt_stats_register(struct ath12k *ar)
{
	debugfs_create_file("htt_stats_type", 0600, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_type);
	debugfs_create_file("htt_stats", 0400, ar->debug.debugfs_pdev,
			    ar, &fops_dump_htt_stats);
	debugfs_create_file("htt_stats_reset", 0200, ar->debug.debugfs_pdev,
			    ar, &fops_htt_stats_reset);
}
