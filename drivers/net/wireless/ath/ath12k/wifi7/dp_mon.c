// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_desc.h"
#include "../dp_mon.h"
#include "dp_mon.h"
#include "../debug.h"
#include "hal_qcn9274.h"
#include "dp_rx.h"
#include "../dp_tx.h"
#include "../peer.h"

static void
ath12k_wifi7_dp_mon_rx_memset_ppdu_info(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	memset(ppdu_info, 0, sizeof(*ppdu_info));
	ppdu_info->peer_id = HAL_INVALID_PEERID;
}

static enum hal_rx_mon_status
ath12k_wifi7_dp_mon_rx_parse_status_tlv(struct ath12k_pdev_dp *dp_pdev,
					struct ath12k_mon_data *pmon,
					const struct hal_tlv_64_hdr *tlv)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	const void *tlv_data = tlv->value;
	u32 info[7], userid;
	u16 tlv_tag, tlv_len;

	tlv_tag = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_TAG);
	tlv_len = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_LEN);
	userid = le64_get_bits(tlv->tl, HAL_TLV_64_USR_ID);

	if (ppdu_info->tlv_aggr.in_progress && ppdu_info->tlv_aggr.tlv_tag != tlv_tag) {
		ath12k_dp_mon_parse_eht_sig_hdr(ppdu_info, ppdu_info->tlv_aggr.buf);

		ppdu_info->tlv_aggr.in_progress = false;
		ppdu_info->tlv_aggr.cur_len = 0;
	}

	switch (tlv_tag) {
	case HAL_RX_PPDU_START: {
		const struct hal_rx_ppdu_start *ppdu_start = tlv_data;

		u64 ppdu_ts = ath12k_le32hilo_to_u64(ppdu_start->ppdu_start_ts_63_32,
						     ppdu_start->ppdu_start_ts_31_0);

		info[0] = __le32_to_cpu(ppdu_start->info0);

		ppdu_info->ppdu_id = u32_get_bits(info[0],
						  HAL_RX_PPDU_START_INFO0_PPDU_ID);

		info[1] = __le32_to_cpu(ppdu_start->info1);
		ppdu_info->chan_num = u32_get_bits(info[1],
						   HAL_RX_PPDU_START_INFO1_CHAN_NUM);
		ppdu_info->freq = u32_get_bits(info[1],
					       HAL_RX_PPDU_START_INFO1_CHAN_FREQ);
		ppdu_info->ppdu_ts = ppdu_ts;

		if (ppdu_info->ppdu_id != ppdu_info->last_ppdu_id) {
			ppdu_info->last_ppdu_id = ppdu_info->ppdu_id;
			ppdu_info->num_users = 0;
			memset(&ppdu_info->mpdu_fcs_ok_bitmap, 0,
			       HAL_RX_NUM_WORDS_PER_PPDU_BITMAP *
			       sizeof(ppdu_info->mpdu_fcs_ok_bitmap[0]));
		}
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS: {
		const struct hal_rx_ppdu_end_user_stats *eu_stats = tlv_data;
		u32 tid_bitmap;

		info[0] = __le32_to_cpu(eu_stats->info0);
		info[1] = __le32_to_cpu(eu_stats->info1);
		info[2] = __le32_to_cpu(eu_stats->info2);
		info[4] = __le32_to_cpu(eu_stats->info4);
		info[5] = __le32_to_cpu(eu_stats->info5);
		info[6] = __le32_to_cpu(eu_stats->info6);

		ppdu_info->ast_index =
			u32_get_bits(info[2], HAL_RX_PPDU_END_USER_STATS_INFO2_AST_INDEX);
		ppdu_info->fc_valid =
			u32_get_bits(info[1], HAL_RX_PPDU_END_USER_STATS_INFO1_FC_VALID);
		tid_bitmap = u32_get_bits(info[6],
					  HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP);
		ppdu_info->tid = ffs(tid_bitmap) - 1;
		ppdu_info->tcp_msdu_count =
			u32_get_bits(info[4],
				     HAL_RX_PPDU_END_USER_STATS_INFO4_TCP_MSDU_CNT);
		ppdu_info->udp_msdu_count =
			u32_get_bits(info[4],
				     HAL_RX_PPDU_END_USER_STATS_INFO4_UDP_MSDU_CNT);
		ppdu_info->other_msdu_count =
			u32_get_bits(info[5],
				     HAL_RX_PPDU_END_USER_STATS_INFO5_OTHER_MSDU_CNT);
		ppdu_info->tcp_ack_msdu_count =
			u32_get_bits(info[5],
				     HAL_RX_PPDU_END_USER_STATS_INFO5_TCP_ACK_MSDU_CNT);
		ppdu_info->preamble_type =
			u32_get_bits(info[1],
				     HAL_RX_PPDU_END_USER_STATS_INFO1_PKT_TYPE);
		ppdu_info->num_mpdu_fcs_ok =
			u32_get_bits(info[1],
				     HAL_RX_PPDU_END_USER_STATS_INFO1_MPDU_CNT_FCS_OK);
		ppdu_info->num_mpdu_fcs_err =
			u32_get_bits(info[0],
				     HAL_RX_PPDU_END_USER_STATS_INFO0_MPDU_CNT_FCS_ERR);
		ppdu_info->peer_id =
			u32_get_bits(info[0], HAL_RX_PPDU_END_USER_STATS_INFO0_PEER_ID);

		switch (ppdu_info->preamble_type) {
		case HAL_RX_PREAMBLE_11N:
			ppdu_info->ht_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11AC:
			ppdu_info->vht_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11AX:
			ppdu_info->he_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11BE:
			ppdu_info->is_eht = true;
			break;
		default:
			break;
		}

		if (userid < HAL_MAX_UL_MU_USERS) {
			struct hal_rx_user_status *rxuser_stats =
				&ppdu_info->userstats[userid];

			if (ppdu_info->num_mpdu_fcs_ok > 1 ||
			    ppdu_info->num_mpdu_fcs_err > 1)
				ppdu_info->userstats[userid].ampdu_present = true;

			ppdu_info->num_users += 1;

			ath12k_dp_mon_rx_handle_ofdma_info(eu_stats, rxuser_stats);
			ath12k_dp_mon_rx_populate_mu_user_info(eu_stats, ppdu_info,
							       rxuser_stats);
		}
		ppdu_info->mpdu_fcs_ok_bitmap[0] = __le32_to_cpu(eu_stats->rsvd1[0]);
		ppdu_info->mpdu_fcs_ok_bitmap[1] = __le32_to_cpu(eu_stats->rsvd1[1]);
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS_EXT: {
		const struct hal_rx_ppdu_end_user_stats_ext *eu_stats = tlv_data;

		ppdu_info->mpdu_fcs_ok_bitmap[2] = __le32_to_cpu(eu_stats->info1);
		ppdu_info->mpdu_fcs_ok_bitmap[3] = __le32_to_cpu(eu_stats->info2);
		ppdu_info->mpdu_fcs_ok_bitmap[4] = __le32_to_cpu(eu_stats->info3);
		ppdu_info->mpdu_fcs_ok_bitmap[5] = __le32_to_cpu(eu_stats->info4);
		ppdu_info->mpdu_fcs_ok_bitmap[6] = __le32_to_cpu(eu_stats->info5);
		ppdu_info->mpdu_fcs_ok_bitmap[7] = __le32_to_cpu(eu_stats->info6);
		break;
	}
	case HAL_PHYRX_HT_SIG:
		ath12k_dp_mon_parse_ht_sig(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_B:
		ath12k_dp_mon_parse_l_sig_b(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_A:
		ath12k_dp_mon_parse_l_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_VHT_SIG_A:
		ath12k_dp_mon_parse_vht_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_SU:
		ath12k_dp_mon_parse_he_sig_su(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_MU_DL:
		ath12k_dp_mon_parse_he_sig_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B1_MU:
		ath12k_dp_mon_parse_he_sig_b1_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_MU:
		ath12k_dp_mon_parse_he_sig_b2_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_OFDMA:
		ath12k_dp_mon_parse_he_sig_b2_ofdma(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_RSSI_LEGACY: {
		const struct hal_rx_phyrx_rssi_legacy_info *rssi = tlv_data;

		info[0] = __le32_to_cpu(rssi->info0);
		info[1] = __le32_to_cpu(rssi->info1);

		/* TODO: Please note that the combined rssi will not be accurate
		 * in MU case. Rssi in MU needs to be retrieved from
		 * PHYRX_OTHER_RECEIVE_INFO TLV.
		 */
		ppdu_info->rssi_comb =
			u32_get_bits(info[1],
				     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB);

		ppdu_info->bw = u32_get_bits(info[0],
					     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO0_RX_BW);
		break;
	}
	case HAL_PHYRX_OTHER_RECEIVE_INFO: {
		const struct hal_phyrx_common_user_info *cmn_usr_info = tlv_data;

		ppdu_info->gi = le32_get_bits(cmn_usr_info->info0,
					      HAL_RX_PHY_CMN_USER_INFO0_GI);
		break;
	}
	case HAL_RX_PPDU_START_USER_INFO:
		ath12k_dp_mon_hal_rx_parse_user_info(tlv_data, userid, ppdu_info);
		break;

	case HAL_RXPCU_PPDU_END_INFO: {
		const struct hal_rx_ppdu_end_duration *ppdu_rx_duration = tlv_data;

		info[0] = __le32_to_cpu(ppdu_rx_duration->info0);
		ppdu_info->rx_duration =
			u32_get_bits(info[0], HAL_RX_PPDU_END_DURATION);
		ppdu_info->tsft = __le32_to_cpu(ppdu_rx_duration->rsvd0[1]);
		ppdu_info->tsft = (ppdu_info->tsft << 32) |
				   __le32_to_cpu(ppdu_rx_duration->rsvd0[0]);
		break;
	}
	case HAL_RX_MPDU_START: {
		const struct hal_rx_mpdu_start *mpdu_start = tlv_data;
		u16 peer_id;

		info[1] = __le32_to_cpu(mpdu_start->info1);
		peer_id = u32_get_bits(info[1], HAL_RX_MPDU_START_INFO1_PEERID);
		if (peer_id)
			ppdu_info->peer_id = peer_id;

		ppdu_info->mpdu_len += u32_get_bits(info[1],
						    HAL_RX_MPDU_START_INFO2_MPDU_LEN);
		if (userid < HAL_MAX_UL_MU_USERS) {
			info[0] = __le32_to_cpu(mpdu_start->info0);
			ppdu_info->userid = userid;
			ppdu_info->userstats[userid].ampdu_id =
				u32_get_bits(info[0], HAL_RX_MPDU_START_INFO0_PPDU_ID);
		}

		return HAL_RX_MON_STATUS_MPDU_START;
	}
	case HAL_RX_MSDU_START:
		/* TODO: add msdu start parsing logic */
		break;
	case HAL_MON_BUF_ADDR:
		return HAL_RX_MON_STATUS_BUF_ADDR;
	case HAL_RX_MSDU_END:
		ath12k_dp_mon_parse_status_msdu_end(pmon, tlv_data);
		return HAL_RX_MON_STATUS_MSDU_END;
	case HAL_RX_MPDU_END:
		return HAL_RX_MON_STATUS_MPDU_END;
	case HAL_PHYRX_GENERIC_U_SIG:
		ath12k_dp_mon_hal_rx_parse_u_sig_hdr(tlv_data, ppdu_info);
		break;
	case HAL_PHYRX_GENERIC_EHT_SIG:
		/* Handle the case where aggregation is in progress
		 * or the current TLV is one of the TLVs which should be
		 * aggregated
		 */
		if (!ppdu_info->tlv_aggr.in_progress) {
			ppdu_info->tlv_aggr.in_progress = true;
			ppdu_info->tlv_aggr.tlv_tag = tlv_tag;
			ppdu_info->tlv_aggr.cur_len = 0;
		}

		ppdu_info->is_eht = true;

		ath12k_dp_mon_hal_aggr_tlv(ppdu_info, tlv_len, tlv_data);
		break;
	case HAL_DUMMY:
		return HAL_RX_MON_STATUS_BUF_DONE;
	case HAL_RX_PPDU_END_STATUS_DONE:
	case 0:
		return HAL_RX_MON_STATUS_PPDU_DONE;
	default:
		break;
	}

	return HAL_RX_MON_STATUS_PPDU_NOT_DONE;
}

static int
ath12k_wifi7_dp_mon_parse_rx_dest_tlv(struct ath12k_pdev_dp *dp_pdev,
				      struct ath12k_mon_data *pmon,
				      enum hal_rx_mon_status hal_status,
				      const void *tlv_data)
{
	switch (hal_status) {
	case HAL_RX_MON_STATUS_MPDU_START:
		if (WARN_ON_ONCE(pmon->mon_mpdu))
			break;

		pmon->mon_mpdu = kzalloc(sizeof(*pmon->mon_mpdu), GFP_ATOMIC);
		if (!pmon->mon_mpdu)
			return -ENOMEM;
		break;
	case HAL_RX_MON_STATUS_BUF_ADDR:
		return ath12k_dp_mon_parse_status_buf(dp_pdev, pmon, tlv_data);
	case HAL_RX_MON_STATUS_MPDU_END:
		/* If no MSDU then free empty MPDU */
		if (pmon->mon_mpdu->tail) {
			pmon->mon_mpdu->tail->next = NULL;
			list_add_tail(&pmon->mon_mpdu->list, &pmon->dp_rx_mon_mpdu_list);
		} else {
			kfree(pmon->mon_mpdu);
		}
		pmon->mon_mpdu = NULL;
		break;
	case HAL_RX_MON_STATUS_MSDU_END:
		pmon->mon_mpdu->decap_format = pmon->decap_format;
		pmon->mon_mpdu->err_bitmap = pmon->err_bitmap;
		break;
	default:
		break;
	}

	return 0;
}

static struct dp_mon_tx_ppdu_info *
ath12k_wifi7_dp_mon_tx_get_ppdu_info(struct ath12k_mon_data *pmon,
				     unsigned int ppdu_id,
				     enum dp_mon_tx_ppdu_info_type type)
{
	struct dp_mon_tx_ppdu_info *tx_ppdu_info;

	if (type == DP_MON_TX_PROT_PPDU_INFO) {
		tx_ppdu_info = pmon->tx_prot_ppdu_info;

		if (tx_ppdu_info && !tx_ppdu_info->is_used)
			return tx_ppdu_info;
		kfree(tx_ppdu_info);
	} else {
		tx_ppdu_info = pmon->tx_data_ppdu_info;

		if (tx_ppdu_info && !tx_ppdu_info->is_used)
			return tx_ppdu_info;
		kfree(tx_ppdu_info);
	}

	/* allocate new tx_ppdu_info */
	tx_ppdu_info = kzalloc(sizeof(*tx_ppdu_info), GFP_ATOMIC);
	if (!tx_ppdu_info)
		return NULL;

	tx_ppdu_info->is_used = 0;
	tx_ppdu_info->ppdu_id = ppdu_id;

	if (type == DP_MON_TX_PROT_PPDU_INFO)
		pmon->tx_prot_ppdu_info = tx_ppdu_info;
	else
		pmon->tx_data_ppdu_info = tx_ppdu_info;

	return tx_ppdu_info;
}

static struct dp_mon_tx_ppdu_info *
ath12k_wifi7_dp_mon_hal_tx_ppdu_info(struct ath12k_mon_data *pmon,
				     u16 tlv_tag)
{
	switch (tlv_tag) {
	case HAL_TX_FES_SETUP:
	case HAL_TX_FLUSH:
	case HAL_PCU_PPDU_SETUP_INIT:
	case HAL_TX_PEER_ENTRY:
	case HAL_TX_QUEUE_EXTENSION:
	case HAL_TX_MPDU_START:
	case HAL_TX_MSDU_START:
	case HAL_TX_DATA:
	case HAL_MON_BUF_ADDR:
	case HAL_TX_MPDU_END:
	case HAL_TX_LAST_MPDU_FETCHED:
	case HAL_TX_LAST_MPDU_END:
	case HAL_COEX_TX_REQ:
	case HAL_TX_RAW_OR_NATIVE_FRAME_SETUP:
	case HAL_SCH_CRITICAL_TLV_REFERENCE:
	case HAL_TX_FES_SETUP_COMPLETE:
	case HAL_TQM_MPDU_GLOBAL_START:
	case HAL_SCHEDULER_END:
	case HAL_TX_FES_STATUS_USER_PPDU:
		break;
	case HAL_TX_FES_STATUS_PROT: {
		if (!pmon->tx_prot_ppdu_info->is_used)
			pmon->tx_prot_ppdu_info->is_used = true;

		return pmon->tx_prot_ppdu_info;
	}
	}

	if (!pmon->tx_data_ppdu_info->is_used)
		pmon->tx_data_ppdu_info->is_used = true;

	return pmon->tx_data_ppdu_info;
}

#define MAX_MONITOR_HEADER 512
#define MAX_DUMMY_FRM_BODY 128

static struct
sk_buff *ath12k_wifi7_dp_mon_tx_alloc_skb(void)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(MAX_MONITOR_HEADER + MAX_DUMMY_FRM_BODY);
	if (!skb)
		return NULL;

	skb_reserve(skb, MAX_MONITOR_HEADER);

	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		skb_pull(skb, PTR_ALIGN(skb->data, 4) - skb->data);

	return skb;
}

static int
ath12k_wifi7_dp_mon_tx_gen_cts2self_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_cts *cts;

	skb = ath12k_wifi7_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	cts = (struct ieee80211_cts *)skb->data;
	memset(cts, 0, MAX_DUMMY_FRM_BODY);
	cts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
	cts->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(cts->ra, tx_ppdu_info->rx_status.addr1, sizeof(cts->ra));

	skb_put(skb, sizeof(*cts));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_wifi7_dp_mon_tx_gen_rts_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_rts *rts;

	skb = ath12k_wifi7_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	rts = (struct ieee80211_rts *)skb->data;
	memset(rts, 0, MAX_DUMMY_FRM_BODY);
	rts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
	rts->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(rts->ra, tx_ppdu_info->rx_status.addr1, sizeof(rts->ra));
	memcpy(rts->ta, tx_ppdu_info->rx_status.addr2, sizeof(rts->ta));

	skb_put(skb, sizeof(*rts));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_wifi7_dp_mon_tx_gen_3addr_qos_null_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_qos_hdr *qhdr;

	skb = ath12k_wifi7_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	qhdr = (struct ieee80211_qos_hdr *)skb->data;
	memset(qhdr, 0, MAX_DUMMY_FRM_BODY);
	qhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
	qhdr->duration_id = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(qhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);
	memcpy(qhdr->addr2, tx_ppdu_info->rx_status.addr2, ETH_ALEN);
	memcpy(qhdr->addr3, tx_ppdu_info->rx_status.addr3, ETH_ALEN);

	skb_put(skb, sizeof(*qhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_wifi7_dp_mon_tx_gen_4addr_qos_null_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct dp_mon_qosframe_addr4 *qhdr;

	skb = ath12k_wifi7_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	qhdr = (struct dp_mon_qosframe_addr4 *)skb->data;
	memset(qhdr, 0, MAX_DUMMY_FRM_BODY);
	qhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
	qhdr->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(qhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);
	memcpy(qhdr->addr2, tx_ppdu_info->rx_status.addr2, ETH_ALEN);
	memcpy(qhdr->addr3, tx_ppdu_info->rx_status.addr3, ETH_ALEN);
	memcpy(qhdr->addr4, tx_ppdu_info->rx_status.addr4, ETH_ALEN);

	skb_put(skb, sizeof(*qhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_wifi7_dp_mon_tx_gen_ack_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct dp_mon_frame_min_one *fbmhdr;

	skb = ath12k_wifi7_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	fbmhdr = (struct dp_mon_frame_min_one *)skb->data;
	memset(fbmhdr, 0, MAX_DUMMY_FRM_BODY);
	fbmhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_CFACK);
	memcpy(fbmhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);

	/* set duration zero for ack frame */
	fbmhdr->duration = 0;

	skb_put(skb, sizeof(*fbmhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_wifi7_dp_mon_tx_gen_prot_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	int ret = 0;

	switch (tx_ppdu_info->rx_status.medium_prot_type) {
	case DP_MON_TX_MEDIUM_RTS_LEGACY:
	case DP_MON_TX_MEDIUM_RTS_11AC_STATIC_BW:
	case DP_MON_TX_MEDIUM_RTS_11AC_DYNAMIC_BW:
		ret = ath12k_wifi7_dp_mon_tx_gen_rts_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_CTS2SELF:
		ret = ath12k_wifi7_dp_mon_tx_gen_cts2self_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_3ADDR:
		ret = ath12k_wifi7_dp_mon_tx_gen_3addr_qos_null_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_4ADDR:
		ret = ath12k_wifi7_dp_mon_tx_gen_4addr_qos_null_frame(tx_ppdu_info);
		break;
	}

	return ret;
}

static enum dp_mon_tx_tlv_status
ath12k_wifi7_dp_mon_tx_parse_status_tlv(struct ath12k_base *ab,
					struct ath12k_mon_data *pmon,
					u16 tlv_tag, const void *tlv_data,
					u32 userid)
{
	struct dp_mon_tx_ppdu_info *tx_ppdu_info;
	enum dp_mon_tx_tlv_status status = DP_MON_TX_STATUS_PPDU_NOT_DONE;
	u32 info[7];

	tx_ppdu_info = ath12k_wifi7_dp_mon_hal_tx_ppdu_info(pmon, tlv_tag);

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		const struct hal_tx_fes_setup *tx_fes_setup = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_setup->info0);
		tx_ppdu_info->ppdu_id = __le32_to_cpu(tx_fes_setup->schedule_id);
		tx_ppdu_info->num_users =
			u32_get_bits(info[0], HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		status = DP_MON_TX_FES_SETUP;
		break;
	}

	case HAL_TX_FES_STATUS_END: {
		const struct hal_tx_fes_status_end *tx_fes_status_end = tlv_data;
		u32 tst_15_0, tst_31_16;

		info[0] = __le32_to_cpu(tx_fes_status_end->info0);
		tst_15_0 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_15_0);
		tst_31_16 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_31_16);

		tx_ppdu_info->rx_status.ppdu_ts = (tst_15_0 | (tst_31_16 << 16));
		status = DP_MON_TX_FES_STATUS_END;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		const struct hal_rx_resp_req_info *rx_resp_req_info = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(rx_resp_req_info->info0);
		info[1] = __le32_to_cpu(rx_resp_req_info->info1);
		info[2] = __le32_to_cpu(rx_resp_req_info->info2);
		info[3] = __le32_to_cpu(rx_resp_req_info->info3);
		info[4] = __le32_to_cpu(rx_resp_req_info->info4);
		info[5] = __le32_to_cpu(rx_resp_req_info->info5);

		tx_ppdu_info->rx_status.ppdu_id =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_PPDU_ID);
		tx_ppdu_info->rx_status.reception_type =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_RECEPTION_TYPE);
		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_DURATION);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_RATE_MCS);
		tx_ppdu_info->rx_status.sgi =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_SGI);
		tx_ppdu_info->rx_status.is_stbc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_STBC);
		tx_ppdu_info->rx_status.ldpc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_LDPC);
		tx_ppdu_info->rx_status.is_ampdu =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_IS_AMPDU);
		tx_ppdu_info->rx_status.num_users =
			u32_get_bits(info[2], HAL_RX_RESP_REQ_INFO2_NUM_USER);

		addr_32 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO3_ADDR1_31_0);
		addr_16 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO4_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		addr_16 = u32_get_bits(info[4], HAL_RX_RESP_REQ_INFO4_ADDR1_15_0);
		addr_32 = u32_get_bits(info[5], HAL_RX_RESP_REQ_INFO5_ADDR1_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr2);

		if (tx_ppdu_info->rx_status.reception_type == 0)
			ath12k_wifi7_dp_mon_tx_gen_cts2self_frame(tx_ppdu_info);
		status = DP_MON_RX_RESPONSE_REQUIRED_INFO;
		break;
	}

	case HAL_PCU_PPDU_SETUP_INIT: {
		const struct hal_tx_pcu_ppdu_setup_init *ppdu_setup = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(ppdu_setup->info0);
		info[1] = __le32_to_cpu(ppdu_setup->info1);
		info[2] = __le32_to_cpu(ppdu_setup->info2);
		info[3] = __le32_to_cpu(ppdu_setup->info3);
		info[4] = __le32_to_cpu(ppdu_setup->info4);
		info[5] = __le32_to_cpu(ppdu_setup->info5);
		info[6] = __le32_to_cpu(ppdu_setup->info6);

		/* protection frame address 1 */
		addr_32 = u32_get_bits(info[1],
				       HAL_TX_PPDU_SETUP_INFO1_PROT_FRAME_ADDR1_31_0);
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		/* protection frame address 2 */
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR2_15_0);
		addr_32 = u32_get_bits(info[3],
				       HAL_TX_PPDU_SETUP_INFO3_PROT_FRAME_ADDR2_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr2);

		/* protection frame address 3 */
		addr_32 = u32_get_bits(info[4],
				       HAL_TX_PPDU_SETUP_INFO4_PROT_FRAME_ADDR3_31_0);
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR3_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr3);

		/* protection frame address 4 */
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR4_15_0);
		addr_32 = u32_get_bits(info[6],
				       HAL_TX_PPDU_SETUP_INFO6_PROT_FRAME_ADDR4_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr4);

		status = u32_get_bits(info[0],
				      HAL_TX_PPDU_SETUP_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_QUEUE_EXTENSION: {
		const struct hal_tx_queue_exten *tx_q_exten = tlv_data;

		info[0] = __le32_to_cpu(tx_q_exten->info0);

		tx_ppdu_info->rx_status.frame_control =
			u32_get_bits(info[0],
				     HAL_TX_Q_EXT_INFO0_FRAME_CTRL);
		tx_ppdu_info->rx_status.fc_valid = true;
		break;
	}

	case HAL_TX_FES_STATUS_START: {
		const struct hal_tx_fes_status_start *tx_fes_start = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_start->info0);

		tx_ppdu_info->rx_status.medium_prot_type =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_FES_STATUS_PROT: {
		const struct hal_tx_fes_status_prot *tx_fes_status = tlv_data;
		u32 start_timestamp;
		u32 end_timestamp;

		info[0] = __le32_to_cpu(tx_fes_status->info0);
		info[1] = __le32_to_cpu(tx_fes_status->info1);

		start_timestamp =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_15_0);
		start_timestamp |=
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_31_16) << 15;
		end_timestamp =
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_15_0);
		end_timestamp |=
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_31_16) << 15;
		tx_ppdu_info->rx_status.rx_duration = end_timestamp - start_timestamp;

		ath12k_wifi7_dp_mon_tx_gen_prot_frame(tx_ppdu_info);
		break;
	}

	case HAL_TX_FES_STATUS_START_PPDU:
	case HAL_TX_FES_STATUS_START_PROT: {
		const struct hal_tx_fes_status_start_prot *tx_fes_stat_start = tlv_data;
		u64 ppdu_ts;

		info[0] = __le32_to_cpu(tx_fes_stat_start->info0);

		tx_ppdu_info->rx_status.ppdu_ts =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_STRT_INFO0_PROT_TS_LOWER_32);
		ppdu_ts = (u32_get_bits(info[1],
					HAL_TX_FES_STAT_STRT_INFO1_PROT_TS_UPPER_32));
		tx_ppdu_info->rx_status.ppdu_ts |= ppdu_ts << 32;
		break;
	}

	case HAL_TX_FES_STATUS_USER_PPDU: {
		const struct hal_tx_fes_status_user_ppdu *tx_fes_usr_ppdu = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_usr_ppdu->info0);

		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_USR_PPDU_INFO0_DURATION);
		break;
	}

	case HAL_MACTX_HE_SIG_A_SU:
		ath12k_dp_mon_parse_he_sig_su(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_A_MU_DL:
		ath12k_dp_mon_parse_he_sig_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B1_MU:
		ath12k_dp_mon_parse_he_sig_b1_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_MU:
		ath12k_dp_mon_parse_he_sig_b2_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_OFDMA:
		ath12k_dp_mon_parse_he_sig_b2_ofdma(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_VHT_SIG_A:
		ath12k_dp_mon_parse_vht_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_A:
		ath12k_dp_mon_parse_l_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_B:
		ath12k_dp_mon_parse_l_sig_b(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_RX_FRAME_BITMAP_ACK: {
		const struct hal_rx_frame_bitmap_ack *fbm_ack = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(fbm_ack->info0);
		info[1] = __le32_to_cpu(fbm_ack->info1);

		addr_32 = u32_get_bits(info[0],
				       HAL_RX_FBM_ACK_INFO0_ADDR1_31_0);
		addr_16 = u32_get_bits(info[1],
				       HAL_RX_FBM_ACK_INFO1_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		ath12k_wifi7_dp_mon_tx_gen_ack_frame(tx_ppdu_info);
		break;
	}

	case HAL_MACTX_PHY_DESC: {
		const struct hal_tx_phy_desc *tx_phy_desc = tlv_data;

		info[0] = __le32_to_cpu(tx_phy_desc->info0);
		info[1] = __le32_to_cpu(tx_phy_desc->info1);
		info[2] = __le32_to_cpu(tx_phy_desc->info2);
		info[3] = __le32_to_cpu(tx_phy_desc->info3);

		tx_ppdu_info->rx_status.beamformed =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BF_TYPE);
		tx_ppdu_info->rx_status.preamble_type =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_PREAMBLE_11B);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1],
				     HAL_TX_PHY_DESC_INFO1_MCS);
		tx_ppdu_info->rx_status.ltf_size =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_LTF_SIZE);
		tx_ppdu_info->rx_status.nss =
			u32_get_bits(info[2],
				     HAL_TX_PHY_DESC_INFO2_NSS);
		tx_ppdu_info->rx_status.chan_num =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_ACTIVE_CHANNEL);
		tx_ppdu_info->rx_status.bw =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BANDWIDTH);
		break;
	}

	case HAL_TX_MPDU_START: {
		struct dp_mon_mpdu *mon_mpdu = tx_ppdu_info->tx_mon_mpdu;

		mon_mpdu = kzalloc(sizeof(*mon_mpdu), GFP_ATOMIC);
		if (!mon_mpdu)
			return DP_MON_TX_STATUS_PPDU_NOT_DONE;
		status = DP_MON_TX_MPDU_START;
		break;
	}

	case HAL_TX_MPDU_END:
		list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
			      &tx_ppdu_info->dp_tx_mon_mpdu_list);
		break;
	}

	return status;
}

static enum dp_mon_tx_tlv_status
ath12k_wifi7_dp_mon_tx_status_get_num_user(u16 tlv_tag,
					   struct hal_tlv_hdr *tx_tlv,
					   u8 *num_users)
{
	u32 tlv_status = DP_MON_TX_STATUS_PPDU_NOT_DONE;
	u32 info0;

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		struct hal_tx_fes_setup *tx_fes_setup =
				(struct hal_tx_fes_setup *)tx_tlv;

		info0 = __le32_to_cpu(tx_fes_setup->info0);

		*num_users = u32_get_bits(info0, HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		tlv_status = DP_MON_TX_FES_SETUP;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		/* TODO: need to update *num_users */
		tlv_status = DP_MON_RX_RESPONSE_REQUIRED_INFO;
		break;
	}
	}

	return tlv_status;
}

static void
ath12k_wifi7_dp_mon_tx_process_ppdu_info(struct ath12k_pdev_dp *dp_pdev,
					 struct napi_struct *napi,
					 struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct dp_mon_mpdu *tmp, *mon_mpdu;

	list_for_each_entry_safe(mon_mpdu, tmp,
				 &tx_ppdu_info->dp_tx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);

		if (mon_mpdu->head)
			ath12k_dp_mon_rx_deliver(dp_pdev, mon_mpdu,
						 &tx_ppdu_info->rx_status, napi);

		kfree(mon_mpdu);
	}
}

enum hal_rx_mon_status
ath12k_wifi7_dp_mon_tx_parse_mon_status(struct ath12k_pdev_dp *dp_pdev,
					struct ath12k_mon_data *pmon,
					struct sk_buff *skb,
					struct napi_struct *napi,
					u32 ppdu_id)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct dp_mon_tx_ppdu_info *tx_prot_ppdu_info, *tx_data_ppdu_info;
	struct hal_tlv_hdr *tlv;
	u8 *ptr = skb->data;
	u16 tlv_tag;
	u16 tlv_len;
	u32 tlv_userid = 0;
	u8 num_user;
	u32 tlv_status = DP_MON_TX_STATUS_PPDU_NOT_DONE;

	tx_prot_ppdu_info =
		ath12k_wifi7_dp_mon_tx_get_ppdu_info(pmon, ppdu_id,
						     DP_MON_TX_PROT_PPDU_INFO);
	if (!tx_prot_ppdu_info)
		return -ENOMEM;

	tlv = (struct hal_tlv_hdr *)ptr;
	tlv_tag = le32_get_bits(tlv->tl, HAL_TLV_HDR_TAG);

	tlv_status = ath12k_wifi7_dp_mon_tx_status_get_num_user(tlv_tag, tlv,
								&num_user);
	if (tlv_status == DP_MON_TX_STATUS_PPDU_NOT_DONE || !num_user)
		return -EINVAL;

	tx_data_ppdu_info =
		ath12k_wifi7_dp_mon_tx_get_ppdu_info(pmon, ppdu_id,
						     DP_MON_TX_DATA_PPDU_INFO);
	if (!tx_data_ppdu_info)
		return -ENOMEM;

	do {
		tlv = (struct hal_tlv_hdr *)ptr;
		tlv_tag = le32_get_bits(tlv->tl, HAL_TLV_HDR_TAG);
		tlv_len = le32_get_bits(tlv->tl, HAL_TLV_HDR_LEN);
		tlv_userid = le32_get_bits(tlv->tl, HAL_TLV_USR_ID);

		tlv_status = ath12k_wifi7_dp_mon_tx_parse_status_tlv(ab, pmon,
								     tlv_tag, ptr,
								     tlv_userid);
		ptr += tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_ALIGN);
		if ((ptr - skb->data) >= DP_TX_MONITOR_BUF_SIZE)
			break;
	} while (tlv_status != DP_MON_TX_FES_STATUS_END);

	ath12k_wifi7_dp_mon_tx_process_ppdu_info(dp_pdev, napi, tx_data_ppdu_info);
	ath12k_wifi7_dp_mon_tx_process_ppdu_info(dp_pdev, napi, tx_prot_ppdu_info);

	return tlv_status;
}

static u32
ath12k_wifi7_dp_rx_mon_mpdu_pop(struct ath12k *ar, int mac_id,
				void *ring_entry, struct sk_buff **head_msdu,
				struct sk_buff **tail_msdu,
				struct list_head *used_list,
				u32 *npackets, u32 *ppdu_id)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_buffer_addr *p_buf_addr_info, *p_last_buf_addr_info;
	u32 msdu_ppdu_id = 0, msdu_cnt = 0, total_len = 0, frag_len = 0;
	u32 rx_buf_size, rx_pkt_offset, sw_cookie;
	bool is_frag, is_first_msdu, drop_mpdu = false;
	struct hal_reo_entrance_ring *ent_desc =
		(struct hal_reo_entrance_ring *)ring_entry;
	u32 rx_bufs_used = 0, i = 0, desc_bank = 0;
	struct hal_rx_desc *rx_desc, *tail_rx_desc;
	struct hal_rx_msdu_link *msdu_link_desc;
	struct sk_buff *msdu = NULL, *last = NULL;
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_buffer_addr buf_info;
	struct hal_rx_msdu_list msdu_list;
	struct ath12k_skb_rxcb *rxcb;
	u16 num_msdus = 0;
	dma_addr_t paddr;
	u8 rbm;

	ath12k_wifi7_hal_rx_reo_ent_buf_paddr_get(ring_entry, &paddr,
						  &sw_cookie,
						  &p_last_buf_addr_info,
						  &rbm,
						  &msdu_cnt);

	spin_lock_bh(&pmon->mon_lock);

	if (le32_get_bits(ent_desc->info1,
			  HAL_REO_ENTR_RING_INFO1_RXDMA_PUSH_REASON) ==
			  HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED) {
		u8 rxdma_err = le32_get_bits(ent_desc->info1,
					     HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE);
		if (rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_FLUSH_REQUEST_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_MPDU_LEN_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_OVERFLOW_ERR) {
			drop_mpdu = true;
			pmon->rx_mon_stats.dest_mpdu_drop++;
		}
	}

	is_frag = false;
	is_first_msdu = true;
	rx_pkt_offset = sizeof(struct hal_rx_desc);

	do {
		if (pmon->mon_last_linkdesc_paddr == paddr) {
			pmon->rx_mon_stats.dup_mon_linkdesc_cnt++;
			spin_unlock_bh(&pmon->mon_lock);
			return rx_bufs_used;
		}

		desc_bank = u32_get_bits(sw_cookie, DP_LINK_DESC_BANK_MASK);
		msdu_link_desc =
			dp->link_desc_banks[desc_bank].vaddr +
			(paddr - dp->link_desc_banks[desc_bank].paddr);

		ath12k_wifi7_hal_rx_msdu_list_get(ar, msdu_link_desc, &msdu_list,
						  &num_msdus);
		desc_info = ath12k_dp_get_rx_desc(ar->ab->dp,
						  msdu_list.sw_cookie[num_msdus - 1]);
		tail_rx_desc = (struct hal_rx_desc *)(desc_info->skb)->data;

		for (i = 0; i < num_msdus; i++) {
			u32 l2_hdr_offset;

			if (pmon->mon_last_buf_cookie == msdu_list.sw_cookie[i]) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d last_cookie %d is same\n",
					   i, pmon->mon_last_buf_cookie);
				drop_mpdu = true;
				pmon->rx_mon_stats.dup_mon_buf_cnt++;
				continue;
			}

			desc_info =
				ath12k_dp_get_rx_desc(ar->ab->dp, msdu_list.sw_cookie[i]);
			msdu = desc_info->skb;

			if (!msdu) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "msdu_pop: invalid msdu (%d/%d)\n",
					   i + 1, num_msdus);
				goto next_msdu;
			}
			rxcb = ATH12K_SKB_RXCB(msdu);
			if (rxcb->paddr != msdu_list.paddr[i]) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d paddr %lx != %lx\n",
					   i, (unsigned long)rxcb->paddr,
					   (unsigned long)msdu_list.paddr[i]);
				drop_mpdu = true;
				continue;
			}
			if (!rxcb->unmapped) {
				dma_unmap_single(ar->ab->dev, rxcb->paddr,
						 msdu->len +
						 skb_tailroom(msdu),
						 DMA_FROM_DEVICE);
				rxcb->unmapped = 1;
			}
			if (drop_mpdu) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d drop msdu %p *ppdu_id %x\n",
					   i, msdu, *ppdu_id);
				dev_kfree_skb_any(msdu);
				msdu = NULL;
				goto next_msdu;
			}

			rx_desc = (struct hal_rx_desc *)msdu->data;
			l2_hdr_offset = ath12k_dp_rx_h_l3pad(ar->ab, tail_rx_desc);
			if (is_first_msdu) {
				if (!ath12k_wifi7_dp_rxdesc_mpdu_valid(ar->ab,
								       rx_desc)) {
					drop_mpdu = true;
					dev_kfree_skb_any(msdu);
					msdu = NULL;
					pmon->mon_last_linkdesc_paddr = paddr;
					goto next_msdu;
				}
				msdu_ppdu_id =
					ath12k_dp_rxdesc_get_ppduid(ar->ab, rx_desc);

				if (ath12k_dp_mon_comp_ppduid(msdu_ppdu_id,
							      ppdu_id)) {
					spin_unlock_bh(&pmon->mon_lock);
					return rx_bufs_used;
				}
				pmon->mon_last_linkdesc_paddr = paddr;
				is_first_msdu = false;
			}
			ath12k_dp_mon_get_buf_len(&msdu_list.msdu_info[i],
						  &is_frag, &total_len,
						  &frag_len, &msdu_cnt);
			rx_buf_size = rx_pkt_offset + l2_hdr_offset + frag_len;

			if (ath12k_dp_pkt_set_pktlen(msdu, rx_buf_size)) {
				dev_kfree_skb_any(msdu);
				goto next_msdu;
			}

			if (!(*head_msdu))
				*head_msdu = msdu;
			else if (last)
				last->next = msdu;

			last = msdu;
next_msdu:
			pmon->mon_last_buf_cookie = msdu_list.sw_cookie[i];
			rx_bufs_used++;
			desc_info->skb = NULL;
			list_add_tail(&desc_info->list, used_list);
		}

		ath12k_wifi7_hal_rx_buf_addr_info_set(&buf_info, paddr,
						      sw_cookie, rbm);

		ath12k_dp_mon_next_link_desc_get(ab, msdu_link_desc, &paddr,
						 &sw_cookie, &rbm,
						 &p_buf_addr_info);

		ath12k_dp_arch_rx_link_desc_return(ar->ab->dp, &buf_info,
						   HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);

		p_last_buf_addr_info = p_buf_addr_info;

	} while (paddr && msdu_cnt);

	spin_unlock_bh(&pmon->mon_lock);

	if (last)
		last->next = NULL;

	*tail_msdu = msdu;

	if (msdu_cnt == 0)
		*npackets = 1;

	return rx_bufs_used;
}

/* The destination ring processing is stuck if the destination is not
 * moving while status ring moves 16 PPDU. The destination ring processing
 * skips this destination ring PPDU as a workaround.
 */
#define MON_DEST_RING_STUCK_MAX_CNT 16

static void
ath12k_wifi7_dp_rx_mon_dest_process(struct ath12k *ar, int mac_id,
				    u32 quota, struct napi_struct *napi)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_pdev_mon_stats *rx_mon_stats;
	u32 ppdu_id, rx_bufs_used = 0, ring_id;
	u32 mpdu_rx_bufs_used, npackets = 0;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	void *ring_entry, *mon_dst_srng;
	struct dp_mon_mpdu *tmp_mpdu;
	LIST_HEAD(rx_desc_used_list);
	struct hal_srng *srng;

	ring_id = dp->rxdma_err_dst_ring[mac_id].ring_id;
	srng = &ab->hal.srng_list[ring_id];

	mon_dst_srng = &ab->hal.srng_list[ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, mon_dst_srng);

	ppdu_id = pmon->mon_ppdu_info.ppdu_id;
	rx_mon_stats = &pmon->rx_mon_stats;

	while ((ring_entry = ath12k_hal_srng_dst_peek(ar->ab, mon_dst_srng))) {
		struct sk_buff *head_msdu, *tail_msdu;

		head_msdu = NULL;
		tail_msdu = NULL;

		mpdu_rx_bufs_used = ath12k_wifi7_dp_rx_mon_mpdu_pop(ar, mac_id,
								    ring_entry,
								    &head_msdu,
								    &tail_msdu,
								    &rx_desc_used_list,
								    &npackets,
								    &ppdu_id);

		rx_bufs_used += mpdu_rx_bufs_used;

		if (mpdu_rx_bufs_used) {
			dp->mon_dest_ring_stuck_cnt = 0;
		} else {
			dp->mon_dest_ring_stuck_cnt++;
			rx_mon_stats->dest_mon_not_reaped++;
		}

		if (dp->mon_dest_ring_stuck_cnt > MON_DEST_RING_STUCK_MAX_CNT) {
			rx_mon_stats->dest_mon_stuck++;
			ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
				   "status ring ppdu_id=%d dest ring ppdu_id=%d mon_dest_ring_stuck_cnt=%d dest_mon_not_reaped=%u dest_mon_stuck=%u\n",
				   pmon->mon_ppdu_info.ppdu_id, ppdu_id,
				   dp->mon_dest_ring_stuck_cnt,
				   rx_mon_stats->dest_mon_not_reaped,
				   rx_mon_stats->dest_mon_stuck);
			spin_lock_bh(&pmon->mon_lock);
			pmon->mon_ppdu_info.ppdu_id = ppdu_id;
			spin_unlock_bh(&pmon->mon_lock);
			continue;
		}

		if (ppdu_id != pmon->mon_ppdu_info.ppdu_id) {
			spin_lock_bh(&pmon->mon_lock);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
			spin_unlock_bh(&pmon->mon_lock);
			ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
				   "dest_rx: new ppdu_id %x != status ppdu_id %x dest_mon_not_reaped = %u dest_mon_stuck = %u\n",
				   ppdu_id, pmon->mon_ppdu_info.ppdu_id,
				   rx_mon_stats->dest_mon_not_reaped,
				   rx_mon_stats->dest_mon_stuck);
			break;
		}

		if (head_msdu && tail_msdu) {
			tmp_mpdu = kzalloc(sizeof(*tmp_mpdu), GFP_ATOMIC);
			if (!tmp_mpdu)
				break;

			tmp_mpdu->head = head_msdu;
			tmp_mpdu->tail = tail_msdu;
			tmp_mpdu->err_bitmap = pmon->err_bitmap;
			tmp_mpdu->decap_format = pmon->decap_format;
			ath12k_dp_mon_rx_deliver(&ar->dp, tmp_mpdu,
						 &pmon->mon_ppdu_info, napi);
			rx_mon_stats->dest_mpdu_done++;
			kfree(tmp_mpdu);
		}

		ring_entry = ath12k_hal_srng_dst_get_next_entry(ar->ab,
								mon_dst_srng);
	}
	ath12k_hal_srng_access_end(ar->ab, mon_dst_srng);

	spin_unlock_bh(&srng->lock);

	if (rx_bufs_used) {
		rx_mon_stats->dest_ppdu_done++;
		ath12k_dp_rx_bufs_replenish(ar->ab->dp,
					    &dp->rx_refill_buf_ring,
					    &rx_desc_used_list,
					    rx_bufs_used);
	}
}

static enum dp_mon_status_buf_state
ath12k_wifi7_dp_rx_mon_buf_done(struct ath12k_base *ab, struct hal_srng *srng,
				struct dp_rxdma_mon_ring *rx_ring)
{
	struct ath12k_skb_rxcb *rxcb;
	struct hal_tlv_64_hdr *tlv;
	struct sk_buff *skb;
	void *status_desc;
	dma_addr_t paddr;
	u32 cookie;
	int buf_id;
	u8 rbm;

	status_desc = ath12k_hal_srng_src_next_peek(ab, srng);
	if (!status_desc)
		return DP_MON_STATUS_NO_DMA;

	ath12k_wifi7_hal_rx_buf_addr_info_get(status_desc, &paddr, &cookie, &rbm);

	buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

	spin_lock_bh(&rx_ring->idr_lock);
	skb = idr_find(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);

	if (!skb)
		return DP_MON_STATUS_NO_DMA;

	rxcb = ATH12K_SKB_RXCB(skb);
	dma_sync_single_for_cpu(ab->dev, rxcb->paddr,
				skb->len + skb_tailroom(skb),
				DMA_FROM_DEVICE);

	tlv = (struct hal_tlv_64_hdr *)skb->data;
	if (le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG) != HAL_RX_STATUS_BUFFER_DONE)
		return DP_MON_STATUS_NO_DMA;

	return DP_MON_STATUS_REPLINISH;
}

static enum hal_rx_mon_status
ath12k_wifi7_dp_mon_parse_rx_dest(struct ath12k_pdev_dp *dp_pdev,
				  struct ath12k_mon_data *pmon,
				  struct sk_buff *skb)
{
	struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);
	struct hal_tlv_64_hdr *tlv;
	struct ath12k_skb_rxcb *rxcb;
	enum hal_rx_mon_status hal_status;
	u16 tlv_tag, tlv_len;
	u8 *ptr = skb->data;

	do {
		tlv = (struct hal_tlv_64_hdr *)ptr;
		tlv_tag = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_TAG);

		/* The actual length of PPDU_END is the combined length of many PHY
		 * TLVs that follow. Skip the TLV header and
		 * rx_rxpcu_classification_overview that follows the header to get to
		 * next TLV.
		 */

		if (tlv_tag == HAL_RX_PPDU_END)
			tlv_len = sizeof(struct hal_rx_rxpcu_classification_overview);
		else
			tlv_len = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_LEN);

		hal_status = ath12k_wifi7_dp_mon_rx_parse_status_tlv(dp_pdev, pmon,
								     tlv);

		if (ar->monitor_started && ar->ab->hw_params->rxdma1_enable &&
		    ath12k_wifi7_dp_mon_parse_rx_dest_tlv(dp_pdev, pmon, hal_status,
							  tlv->value))
			return HAL_RX_MON_STATUS_PPDU_DONE;

		ptr += sizeof(*tlv) + tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_64_ALIGN);

		if ((ptr - skb->data) > skb->len)
			break;

	} while ((hal_status == HAL_RX_MON_STATUS_PPDU_NOT_DONE) ||
		 (hal_status == HAL_RX_MON_STATUS_BUF_ADDR) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_START) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_END) ||
		 (hal_status == HAL_RX_MON_STATUS_MSDU_END));

	rxcb = ATH12K_SKB_RXCB(skb);
	if (rxcb->is_end_of_ppdu)
		hal_status = HAL_RX_MON_STATUS_PPDU_DONE;

	return hal_status;
}

static enum hal_rx_mon_status
ath12k_wifi7_dp_mon_rx_parse_mon_status(struct ath12k_pdev_dp *dp_pdev,
					struct ath12k_mon_data *pmon,
					struct sk_buff *skb,
					struct napi_struct *napi)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct dp_mon_mpdu *tmp;
	struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
	enum hal_rx_mon_status hal_status;

	hal_status = ath12k_wifi7_dp_mon_parse_rx_dest(dp_pdev, pmon, skb);
	if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE)
		return hal_status;

	list_for_each_entry_safe(mon_mpdu, tmp, &pmon->dp_rx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);

		if (mon_mpdu->head && mon_mpdu->tail)
			ath12k_dp_mon_rx_deliver(dp_pdev, mon_mpdu, ppdu_info, napi);

		kfree(mon_mpdu);
	}

	return hal_status;
}

static int
ath12k_wifi7_dp_rx_reap_mon_status_ring(struct ath12k_base *ab, int mac_id,
					int *budget, struct sk_buff_head *skb_list)
{
	const struct ath12k_hw_hal_params *hal_params;
	int buf_id, srng_id, num_buffs_reaped = 0;
	enum dp_mon_status_buf_state reap_status;
	struct dp_rxdma_mon_ring *rx_ring;
	struct ath12k_mon_data *pmon;
	struct ath12k_skb_rxcb *rxcb;
	struct hal_tlv_64_hdr *tlv;
	void *rx_mon_status_desc;
	struct hal_srng *srng;
	struct ath12k_dp *dp;
	struct sk_buff *skb;
	struct ath12k *ar;
	dma_addr_t paddr;
	u32 cookie;
	u8 rbm;

	ar = ab->pdevs[ath12k_hw_mac_id_to_pdev_id(ab->hw_params, mac_id)].ar;
	dp = ath12k_ab_to_dp(ab);
	pmon = &ar->dp.mon_data;
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, mac_id);
	rx_ring = &dp->rx_mon_status_refill_ring[srng_id];

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (*budget) {
		*budget -= 1;
		rx_mon_status_desc = ath12k_hal_srng_src_peek(ab, srng);
		if (!rx_mon_status_desc) {
			pmon->buf_state = DP_MON_STATUS_REPLINISH;
			break;
		}
		ath12k_wifi7_hal_rx_buf_addr_info_get(rx_mon_status_desc, &paddr,
						      &cookie, &rbm);
		if (paddr) {
			buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

			spin_lock_bh(&rx_ring->idr_lock);
			skb = idr_find(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			if (!skb) {
				ath12k_warn(ab, "rx monitor status with invalid buf_id %d\n",
					    buf_id);
				pmon->buf_state = DP_MON_STATUS_REPLINISH;
				goto move_next;
			}

			rxcb = ATH12K_SKB_RXCB(skb);

			dma_sync_single_for_cpu(ab->dev, rxcb->paddr,
						skb->len + skb_tailroom(skb),
						DMA_FROM_DEVICE);

			tlv = (struct hal_tlv_64_hdr *)skb->data;
			if (le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG) !=
					HAL_RX_STATUS_BUFFER_DONE) {
				pmon->buf_state = DP_MON_STATUS_NO_DMA;
				ath12k_warn(ab,
					    "mon status DONE not set %llx, buf_id %d\n",
					    le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG),
					    buf_id);
				/* RxDMA status done bit might not be set even
				 * though tp is moved by HW.
				 */

				/* If done status is missing:
				 * 1. As per MAC team's suggestion,
				 *    when HP + 1 entry is peeked and if DMA
				 *    is not done and if HP + 2 entry's DMA done
				 *    is set. skip HP + 1 entry and
				 *    start processing in next interrupt.
				 * 2. If HP + 2 entry's DMA done is not set,
				 *    poll onto HP + 1 entry DMA done to be set.
				 *    Check status for same buffer for next time
				 *    dp_rx_mon_status_srng_process
				 */
				reap_status = ath12k_wifi7_dp_rx_mon_buf_done(ab, srng,
									      rx_ring);
				if (reap_status == DP_MON_STATUS_NO_DMA)
					continue;

				spin_lock_bh(&rx_ring->idr_lock);
				idr_remove(&rx_ring->bufs_idr, buf_id);
				spin_unlock_bh(&rx_ring->idr_lock);

				dma_unmap_single(ab->dev, rxcb->paddr,
						 skb->len + skb_tailroom(skb),
						 DMA_FROM_DEVICE);

				dev_kfree_skb_any(skb);
				pmon->buf_state = DP_MON_STATUS_REPLINISH;
				goto move_next;
			}

			spin_lock_bh(&rx_ring->idr_lock);
			idr_remove(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			dma_unmap_single(ab->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);

			if (ath12k_dp_pkt_set_pktlen(skb, RX_MON_STATUS_BUF_SIZE)) {
				dev_kfree_skb_any(skb);
				goto move_next;
			}
			__skb_queue_tail(skb_list, skb);
		} else {
			pmon->buf_state = DP_MON_STATUS_REPLINISH;
		}
move_next:
		skb = ath12k_dp_rx_alloc_mon_status_buf(ab, rx_ring,
							&buf_id);
		hal_params = ab->hal.hal_params;

		if (!skb) {
			ath12k_warn(ab, "failed to alloc buffer for status ring\n");
			ath12k_wifi7_hal_rx_buf_addr_info_set(rx_mon_status_desc,
							      0, 0,
							      hal_params->rx_buf_rbm);
			num_buffs_reaped++;
			break;
		}
		rxcb = ATH12K_SKB_RXCB(skb);

		cookie = u32_encode_bits(mac_id, DP_RXDMA_BUF_COOKIE_PDEV_ID) |
			 u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

		ath12k_wifi7_hal_rx_buf_addr_info_set(rx_mon_status_desc, rxcb->paddr,
						      cookie, hal_params->rx_buf_rbm);
		ath12k_hal_srng_src_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}
	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return num_buffs_reaped;
}

static int
__ath12k_wifi7_dp_mon_process_ring(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi, int *budget)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_pdev_mon_stats *rx_mon_stats = &pmon->rx_mon_stats;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	enum hal_rx_mon_status hal_status;
	struct sk_buff_head skb_list;
	int num_buffs_reaped;
	struct sk_buff *skb;

	__skb_queue_head_init(&skb_list);

	num_buffs_reaped = ath12k_wifi7_dp_rx_reap_mon_status_ring(ar->ab, mac_id,
								   budget, &skb_list);
	if (!num_buffs_reaped)
		goto exit;

	while ((skb = __skb_dequeue(&skb_list))) {
		memset(ppdu_info, 0, sizeof(*ppdu_info));
		ppdu_info->peer_id = HAL_INVALID_PEERID;

		hal_status = ath12k_wifi7_dp_mon_parse_rx_dest(&ar->dp, pmon, skb);

		if (ar->monitor_started &&
		    pmon->mon_ppdu_status == DP_PPDU_STATUS_START &&
		    hal_status == HAL_TLV_STATUS_PPDU_DONE) {
			rx_mon_stats->status_ppdu_done++;
			pmon->mon_ppdu_status = DP_PPDU_STATUS_DONE;
			ath12k_wifi7_dp_rx_mon_dest_process(ar, mac_id, *budget, napi);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
		}

		dev_kfree_skb_any(skb);
	}

exit:
	return num_buffs_reaped;
}

static int
ath12k_wifi7_dp_mon_srng_process(struct ath12k_pdev_dp *pdev_dp, int *budget,
				 struct napi_struct *napi)
{
	struct ath12k_dp *dp = pdev_dp->dp;
	struct ath12k_base *ab = dp->ab;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&pdev_dp->mon_data;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct hal_mon_dest_desc *mon_dst_desc;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb;
	struct dp_srng *mon_dst_ring;
	struct hal_srng *srng;
	struct dp_rxdma_mon_ring *buf_ring;
	struct ath12k_dp_link_peer *peer;
	struct sk_buff_head skb_list;
	u64 cookie;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u32 hal_status, end_offset, info0, end_reason;
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, pdev_dp->mac_id);

	__skb_queue_head_init(&skb_list);
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, pdev_idx);
	mon_dst_ring = &pdev_dp->rxdma_mon_dst_ring[srng_id];
	buf_ring = &dp->rxdma_mon_buf_ring;

	srng = &ab->hal.srng_list[mon_dst_ring->ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (likely(*budget)) {
		mon_dst_desc = ath12k_hal_srng_dst_peek(ab, srng);
		if (unlikely(!mon_dst_desc))
			break;

		/* In case of empty descriptor, the cookie in the ring descriptor
		 * is invalid. Therefore, this entry is skipped, and ring processing
		 * continues.
		 */
		info0 = le32_to_cpu(mon_dst_desc->info0);
		if (u32_get_bits(info0, HAL_MON_DEST_INFO0_EMPTY_DESC))
			goto move_next;

		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "monitor destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);

		end_reason = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_REASON);

		/* HAL_MON_FLUSH_DETECTED implies that an rx flush received at the end of
		 * rx PPDU and HAL_MON_PPDU_TRUNCATED implies that the PPDU got
		 * truncated due to a system level error. In both the cases, buffer data
		 * can be discarded
		 */
		if ((end_reason == HAL_MON_FLUSH_DETECTED) ||
		    (end_reason == HAL_MON_PPDU_TRUNCATED)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Monitor dest descriptor end reason %d", end_reason);
			dev_kfree_skb_any(skb);
			goto move_next;
		}

		/* Calculate the budget when the ring descriptor with the
		 * HAL_MON_END_OF_PPDU to ensure that one PPDU worth of data is always
		 * reaped. This helps to efficiently utilize the NAPI budget.
		 */
		if (end_reason == HAL_MON_END_OF_PPDU) {
			*budget -= 1;
			rxcb->is_end_of_ppdu = true;
		}

		end_offset = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_OFFSET);
		if (likely(end_offset <= DP_RX_BUFFER_SIZE)) {
			skb_put(skb, end_offset);
		} else {
			ath12k_warn(ab,
				    "invalid offset on mon stats destination %u\n",
				    end_offset);
			skb_put(skb, DP_RX_BUFFER_SIZE);
		}

		__skb_queue_tail(&skb_list, skb);

move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_dst_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	if (!num_buffs_reaped)
		return 0;

	/* In some cases, one PPDU worth of data can be spread across multiple NAPI
	 * schedules, To avoid losing existing parsed ppdu_info information, skip
	 * the memset of the ppdu_info structure and continue processing it.
	 */
	if (!ppdu_info->ppdu_continuation)
		ath12k_wifi7_dp_mon_rx_memset_ppdu_info(ppdu_info);

	while ((skb = __skb_dequeue(&skb_list))) {
		hal_status = ath12k_wifi7_dp_mon_rx_parse_mon_status(pdev_dp, pmon,
								     skb, napi);
		if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
			ppdu_info->ppdu_continuation = true;
			dev_kfree_skb_any(skb);
			continue;
		}

		if (ppdu_info->peer_id == HAL_INVALID_PEERID)
			goto free_skb;

		rcu_read_lock();
		peer = ath12k_dp_link_peer_find_by_peerid(pdev_dp, ppdu_info->peer_id);
		if (!peer || !peer->sta) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "failed to find the peer with monitor peer_id %d\n",
				   ppdu_info->peer_id);
			goto next_skb;
		}

		if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_SU) {
			ath12k_dp_mon_rx_update_peer_su_stats(peer, ppdu_info);
		} else if ((ppdu_info->fc_valid) &&
			   (ppdu_info->ast_index != HAL_AST_IDX_INVALID)) {
			ath12k_dp_mon_rx_process_ulofdma(ppdu_info);
			ath12k_dp_mon_rx_update_peer_mu_stats(ab, ppdu_info);
		}

next_skb:
		rcu_read_unlock();
free_skb:
		dev_kfree_skb_any(skb);
		ath12k_wifi7_dp_mon_rx_memset_ppdu_info(ppdu_info);
	}

	return num_buffs_reaped;
}

int ath12k_wifi7_dp_mon_process_ring(struct ath12k_dp *dp, int mac_id,
				     struct napi_struct *napi, int budget,
				     enum dp_monitor_mode monitor_mode)
{
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, mac_id);
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	int num_buffs_reaped = 0;

	rcu_read_lock();

	dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);
	if (!dp_pdev) {
		rcu_read_unlock();
		return 0;
	}

	if (dp->hw_params->rxdma1_enable) {
		if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE)
			num_buffs_reaped = ath12k_wifi7_dp_mon_srng_process(dp_pdev,
									    &budget,
									    napi);
	} else {
		ar = ath12k_pdev_dp_to_ar(dp_pdev);

		if (ar->monitor_started)
			num_buffs_reaped =
				__ath12k_wifi7_dp_mon_process_ring(ar, mac_id, napi,
								   &budget);
	}

	rcu_read_unlock();

	return num_buffs_reaped;
}
