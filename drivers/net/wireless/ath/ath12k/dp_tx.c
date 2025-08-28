// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "dp_tx.h"
#include "debug.h"
#include "debugfs.h"
#include "hw.h"
#include "peer.h"
#include "mac.h"

enum hal_tcl_encap_type
ath12k_dp_tx_get_encap_type(struct ath12k_base *ab, struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags))
		return HAL_TCL_ENCAP_TYPE_RAW;

	if (tx_info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP)
		return HAL_TCL_ENCAP_TYPE_ETHERNET;

	return HAL_TCL_ENCAP_TYPE_NATIVE_WIFI;
}

void ath12k_dp_tx_encap_nwifi(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u8 *qos_ctl;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	qos_ctl = ieee80211_get_qos_ctl(hdr);
	memmove(skb->data + IEEE80211_QOS_CTL_LEN,
		skb->data, (void *)qos_ctl - (void *)skb->data);
	skb_pull(skb, IEEE80211_QOS_CTL_LEN);

	hdr = (void *)skb->data;
	hdr->frame_control &= ~__cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
}

u8 ath12k_dp_tx_get_tid(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ath12k_skb_cb *cb = ATH12K_SKB_CB(skb);

	if (cb->flags & ATH12K_SKB_HW_80211_ENCAP)
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	else if (!ieee80211_is_data_qos(hdr->frame_control))
		return HAL_DESC_REO_NON_QOS_TID;
	else
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
}

enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return HAL_ENCRYPT_TYPE_WEP_40;
	case WLAN_CIPHER_SUITE_WEP104:
		return HAL_ENCRYPT_TYPE_WEP_104;
	case WLAN_CIPHER_SUITE_TKIP:
		return HAL_ENCRYPT_TYPE_TKIP_MIC;
	case WLAN_CIPHER_SUITE_CCMP:
		return HAL_ENCRYPT_TYPE_CCMP_128;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return HAL_ENCRYPT_TYPE_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return HAL_ENCRYPT_TYPE_GCMP_128;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return HAL_ENCRYPT_TYPE_AES_GCMP_256;
	default:
		return HAL_ENCRYPT_TYPE_OPEN;
	}
}

void ath12k_dp_tx_release_txbuf(struct ath12k_dp *dp,
				struct ath12k_tx_desc_info *tx_desc,
				u8 pool_id)
{
	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	tx_desc->skb_ext_desc = NULL;
	list_move_tail(&tx_desc->list, &dp->tx_desc_free_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
}

struct ath12k_tx_desc_info *ath12k_dp_tx_assign_buffer(struct ath12k_dp *dp,
						       u8 pool_id)
{
	struct ath12k_tx_desc_info *desc;

	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	desc = list_first_entry_or_null(&dp->tx_desc_free_list[pool_id],
					struct ath12k_tx_desc_info,
					list);
	if (!desc) {
		spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
		ath12k_warn(dp->ab, "failed to allocate data Tx buffer\n");
		return NULL;
	}

	list_move_tail(&desc->list, &dp->tx_desc_used_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);

	return desc;
}

void *ath12k_dp_metadata_align_skb(struct sk_buff *skb, u8 tail_len)
{
	struct sk_buff *tail;
	void *metadata;

	if (unlikely(skb_cow_data(skb, tail_len, &tail) < 0))
		return NULL;

	metadata = pskb_put(skb, tail, tail_len);
	memset(metadata, 0, tail_len);
	return metadata;
}

static void ath12k_dp_tx_move_payload(struct sk_buff *skb,
				      unsigned long delta,
				      bool head)
{
	unsigned long len = skb->len;

	if (head) {
		skb_push(skb, delta);
		memmove(skb->data, skb->data + delta, len);
		skb_trim(skb, len);
	} else {
		skb_put(skb, delta);
		memmove(skb->data + delta, skb->data, len);
		skb_pull(skb, delta);
	}
}

int ath12k_dp_tx_align_payload(struct ath12k_base *ab,
			       struct sk_buff **pskb)
{
	u32 iova_mask = ab->hw_params->iova_mask;
	unsigned long offset, delta1, delta2;
	struct sk_buff *skb2, *skb = *pskb;
	unsigned int headroom = skb_headroom(skb);
	int tailroom = skb_tailroom(skb);
	int ret = 0;

	offset = (unsigned long)skb->data & iova_mask;
	delta1 = offset;
	delta2 = iova_mask - offset + 1;

	if (headroom >= delta1) {
		ath12k_dp_tx_move_payload(skb, delta1, true);
	} else if (tailroom >= delta2) {
		ath12k_dp_tx_move_payload(skb, delta2, false);
	} else {
		skb2 = skb_realloc_headroom(skb, iova_mask);
		if (!skb2) {
			ret = -ENOMEM;
			goto out;
		}

		dev_kfree_skb_any(skb);

		offset = (unsigned long)skb2->data & iova_mask;
		if (offset)
			ath12k_dp_tx_move_payload(skb2, offset, true);
		*pskb = skb2;
	}

out:
	return ret;
}

void ath12k_dp_tx_free_txbuf(struct ath12k_base *ab,
			     struct dp_tx_ring *tx_ring,
			     struct ath12k_tx_desc_params *desc_params)
{
	struct ath12k *ar;
	struct sk_buff *msdu = desc_params->skb;
	struct ath12k_skb_cb *skb_cb;
	u8 pdev_id = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, desc_params->mac_id);

	skb_cb = ATH12K_SKB_CB(msdu);
	ar = ab->pdevs[pdev_id].ar;

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
	}

	ieee80211_free_txskb(ar->ah->hw, msdu);

	if (atomic_dec_and_test(&ar->dp.num_tx_pending))
		wake_up(&ar->dp.tx_empty_waitq);
}

static int
ath12k_dp_tx_get_ring_id_type(struct ath12k_base *ab,
			      int mac_id, u32 ring_id,
			      enum hal_ring_type ring_type,
			      enum htt_srng_ring_type *htt_ring_type,
			      enum htt_srng_ring_id *htt_ring_id)
{
	int ret = 0;

	switch (ring_type) {
	case HAL_RXDMA_BUF:
		/* for some targets, host fills rx buffer to fw and fw fills to
		 * rxbuf ring for each rxdma
		 */
		if (!ab->hw_params->rx_mac_buf_ring) {
			if (!(ring_id == HAL_SRNG_SW2RXDMA_BUF0 ||
			      ring_id == HAL_SRNG_SW2RXDMA_BUF1)) {
				ret = -EINVAL;
			}
			*htt_ring_id = HTT_RXDMA_HOST_BUF_RING;
			*htt_ring_type = HTT_SW_TO_HW_RING;
		} else {
			if (ring_id == HAL_SRNG_SW2RXDMA_BUF0) {
				*htt_ring_id = HTT_HOST1_TO_FW_RXBUF_RING;
				*htt_ring_type = HTT_SW_TO_SW_RING;
			} else {
				*htt_ring_id = HTT_RXDMA_HOST_BUF_RING;
				*htt_ring_type = HTT_SW_TO_HW_RING;
			}
		}
		break;
	case HAL_RXDMA_DST:
		*htt_ring_id = HTT_RXDMA_NON_MONITOR_DEST_RING;
		*htt_ring_type = HTT_HW_TO_SW_RING;
		break;
	case HAL_RXDMA_MONITOR_BUF:
		*htt_ring_id = HTT_RX_MON_HOST2MON_BUF_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_RXDMA_MONITOR_STATUS:
		*htt_ring_id = HTT_RXDMA_MONITOR_STATUS_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_RXDMA_MONITOR_DST:
		*htt_ring_id = HTT_RX_MON_MON2HOST_DEST_RING;
		*htt_ring_type = HTT_HW_TO_SW_RING;
		break;
	case HAL_RXDMA_MONITOR_DESC:
		*htt_ring_id = HTT_RXDMA_MONITOR_DESC_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	default:
		ath12k_warn(ab, "Unsupported ring type in DP :%d\n", ring_type);
		ret = -EINVAL;
	}
	return ret;
}

int ath12k_dp_tx_htt_srng_setup(struct ath12k_base *ab, u32 ring_id,
				int mac_id, enum hal_ring_type ring_type)
{
	struct htt_srng_setup_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	u32 ring_entry_sz;
	int len = sizeof(*cmd);
	dma_addr_t hp_addr, tp_addr;
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	hp_addr = ath12k_hal_srng_get_hp_addr(ab, srng);
	tp_addr = ath12k_hal_srng_get_tp_addr(ab, srng);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);
	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_srng_setup_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_SRING_SETUP,
				      HTT_SRNG_SETUP_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |= le32_encode_bits(DP_SW2HW_MACID(mac_id),
					       HTT_SRNG_SETUP_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |= le32_encode_bits(mac_id,
					       HTT_SRNG_SETUP_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_type,
				       HTT_SRNG_SETUP_CMD_INFO0_RING_TYPE);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_SRNG_SETUP_CMD_INFO0_RING_ID);

	cmd->ring_base_addr_lo = cpu_to_le32(params.ring_base_paddr &
					     HAL_ADDR_LSB_REG_MASK);

	cmd->ring_base_addr_hi = cpu_to_le32((u64)params.ring_base_paddr >>
					     HAL_ADDR_MSB_REG_SHIFT);

	ret = ath12k_hal_srng_get_entrysize(ab, ring_type);
	if (ret < 0)
		goto err_free;

	ring_entry_sz = ret;

	ring_entry_sz >>= 2;
	cmd->info1 = le32_encode_bits(ring_entry_sz,
				      HTT_SRNG_SETUP_CMD_INFO1_RING_ENTRY_SIZE);
	cmd->info1 |= le32_encode_bits(params.num_entries * ring_entry_sz,
				       HTT_SRNG_SETUP_CMD_INFO1_RING_SIZE);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_MSI_SWAP);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_TLV_SWAP);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_RING_PTR_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_HOST_FW_SWAP);
	if (htt_ring_type == HTT_SW_TO_HW_RING)
		cmd->info1 |= cpu_to_le32(HTT_SRNG_SETUP_CMD_INFO1_RING_LOOP_CNT_DIS);

	cmd->ring_head_off32_remote_addr_lo = cpu_to_le32(lower_32_bits(hp_addr));
	cmd->ring_head_off32_remote_addr_hi = cpu_to_le32(upper_32_bits(hp_addr));

	cmd->ring_tail_off32_remote_addr_lo = cpu_to_le32(lower_32_bits(tp_addr));
	cmd->ring_tail_off32_remote_addr_hi = cpu_to_le32(upper_32_bits(tp_addr));

	cmd->ring_msi_addr_lo = cpu_to_le32(lower_32_bits(params.msi_addr));
	cmd->ring_msi_addr_hi = cpu_to_le32(upper_32_bits(params.msi_addr));
	cmd->msi_data = cpu_to_le32(params.msi_data);

	cmd->intr_info =
		le32_encode_bits(params.intr_batch_cntr_thres_entries * ring_entry_sz,
				 HTT_SRNG_SETUP_CMD_INTR_INFO_BATCH_COUNTER_THRESH);
	cmd->intr_info |=
		le32_encode_bits(params.intr_timer_thres_us >> 3,
				 HTT_SRNG_SETUP_CMD_INTR_INFO_INTR_TIMER_THRESH);

	cmd->info2 = 0;
	if (params.flags & HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN) {
		cmd->info2 = le32_encode_bits(params.low_threshold,
					      HTT_SRNG_SETUP_CMD_INFO2_INTR_LOW_THRESH);
	}

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "%s msi_addr_lo:0x%x, msi_addr_hi:0x%x, msi_data:0x%x\n",
		   __func__, cmd->ring_msi_addr_lo, cmd->ring_msi_addr_hi,
		   cmd->msi_data);

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "ring_id:%d, ring_type:%d, intr_info:0x%x, flags:0x%x\n",
		   ring_id, ring_type, cmd->intr_info, cmd->info2);

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);

	return ret;
}

#define HTT_TARGET_VERSION_TIMEOUT_HZ (3 * HZ)

int ath12k_dp_tx_htt_h2t_ver_req_msg(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ver_req_cmd *cmd;
	int len = sizeof(*cmd);
	u32 metadata_version;
	int ret;

	init_completion(&dp->htt_tgt_version_received);

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);
	cmd = (struct htt_ver_req_cmd *)skb->data;
	cmd->ver_reg_info = le32_encode_bits(HTT_H2T_MSG_TYPE_VERSION_REQ,
					     HTT_OPTION_TAG);
	metadata_version = ath12k_ftm_mode ? HTT_OPTION_TCL_METADATA_VER_V1 :
			   HTT_OPTION_TCL_METADATA_VER_V2;

	cmd->tcl_metadata_version = le32_encode_bits(HTT_TAG_TCL_METADATA_VERSION,
						     HTT_OPTION_TAG) |
				    le32_encode_bits(HTT_TCL_METADATA_VER_SZ,
						     HTT_OPTION_LEN) |
				    le32_encode_bits(metadata_version,
						     HTT_OPTION_VALUE);

	ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	ret = wait_for_completion_timeout(&dp->htt_tgt_version_received,
					  HTT_TARGET_VERSION_TIMEOUT_HZ);
	if (ret == 0) {
		ath12k_warn(ab, "htt target version request timed out\n");
		return -ETIMEDOUT;
	}

	if (dp->htt_tgt_ver_major != HTT_TARGET_VERSION_MAJOR) {
		ath12k_err(ab, "unsupported htt major version %d supported version is %d\n",
			   dp->htt_tgt_ver_major, HTT_TARGET_VERSION_MAJOR);
		return -EOPNOTSUPP;
	}

	return 0;
}

int ath12k_dp_tx_htt_h2t_ppdu_stats_req(struct ath12k *ar, u32 mask)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ppdu_stats_cfg_cmd *cmd;
	int len = sizeof(*cmd);
	u8 pdev_mask;
	int ret;
	int i;

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		skb = ath12k_htc_alloc_skb(ab, len);
		if (!skb)
			return -ENOMEM;

		skb_put(skb, len);
		cmd = (struct htt_ppdu_stats_cfg_cmd *)skb->data;
		cmd->msg = le32_encode_bits(HTT_H2T_MSG_TYPE_PPDU_STATS_CFG,
					    HTT_PPDU_STATS_CFG_MSG_TYPE);

		pdev_mask = 1 << (i + ar->pdev_idx);
		cmd->msg |= le32_encode_bits(pdev_mask, HTT_PPDU_STATS_CFG_PDEV_ID);
		cmd->msg |= le32_encode_bits(mask, HTT_PPDU_STATS_CFG_TLV_TYPE_BITMASK);

		ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}
	}

	return 0;
}

int ath12k_dp_tx_htt_rx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int rx_buf_size,
				     struct htt_rx_ring_tlv_filter *tlv_filter)
{
	struct htt_rx_ring_selection_cfg_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	int len = sizeof(*cmd);
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);
	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_rx_ring_selection_cfg_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_RX_RING_SELECTION_CFG,
				      HTT_RX_RING_SELECTION_CFG_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |=
			le32_encode_bits(DP_SW2HW_MACID(mac_id),
					 HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |=
			le32_encode_bits(mac_id,
					 HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_RING_ID);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_SS);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PS);
	cmd->info0 |= le32_encode_bits(tlv_filter->offset_valid,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_OFFSET_VALID);
	cmd->info0 |=
		le32_encode_bits(tlv_filter->drop_threshold_valid,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO0_DROP_THRES_VAL);
	cmd->info0 |= le32_encode_bits(!tlv_filter->rxmon_disable,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_EN_RXMON);

	cmd->info1 = le32_encode_bits(rx_buf_size,
				      HTT_RX_RING_SELECTION_CFG_CMD_INFO1_BUF_SIZE);
	cmd->info1 |= le32_encode_bits(tlv_filter->conf_len_mgmt,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_MGMT);
	cmd->info1 |= le32_encode_bits(tlv_filter->conf_len_ctrl,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_CTRL);
	cmd->info1 |= le32_encode_bits(tlv_filter->conf_len_data,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_DATA);
	cmd->pkt_type_en_flags0 = cpu_to_le32(tlv_filter->pkt_filter_flags0);
	cmd->pkt_type_en_flags1 = cpu_to_le32(tlv_filter->pkt_filter_flags1);
	cmd->pkt_type_en_flags2 = cpu_to_le32(tlv_filter->pkt_filter_flags2);
	cmd->pkt_type_en_flags3 = cpu_to_le32(tlv_filter->pkt_filter_flags3);
	cmd->rx_filter_tlv = cpu_to_le32(tlv_filter->rx_filter);

	cmd->info2 = le32_encode_bits(tlv_filter->rx_drop_threshold,
				      HTT_RX_RING_SELECTION_CFG_CMD_INFO2_DROP_THRESHOLD);
	cmd->info2 |=
		le32_encode_bits(tlv_filter->enable_log_mgmt_type,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO2_EN_LOG_MGMT_TYPE);
	cmd->info2 |=
		le32_encode_bits(tlv_filter->enable_log_ctrl_type,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO2_EN_CTRL_TYPE);
	cmd->info2 |=
		le32_encode_bits(tlv_filter->enable_log_data_type,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO2_EN_LOG_DATA_TYPE);

	cmd->info3 =
		le32_encode_bits(tlv_filter->enable_rx_tlv_offset,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO3_EN_TLV_PKT_OFFSET);
	cmd->info3 |=
		le32_encode_bits(tlv_filter->rx_tlv_offset,
				 HTT_RX_RING_SELECTION_CFG_CMD_INFO3_PKT_TLV_OFFSET);

	if (tlv_filter->offset_valid) {
		cmd->rx_packet_offset =
			le32_encode_bits(tlv_filter->rx_packet_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_PACKET_OFFSET);

		cmd->rx_packet_offset |=
			le32_encode_bits(tlv_filter->rx_header_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_HEADER_OFFSET);

		cmd->rx_mpdu_offset =
			le32_encode_bits(tlv_filter->rx_mpdu_end_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_END_OFFSET);

		cmd->rx_mpdu_offset |=
			le32_encode_bits(tlv_filter->rx_mpdu_start_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_START_OFFSET);

		cmd->rx_msdu_offset =
			le32_encode_bits(tlv_filter->rx_msdu_end_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MSDU_END_OFFSET);

		cmd->rx_msdu_offset |=
			le32_encode_bits(tlv_filter->rx_msdu_start_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MSDU_START_OFFSET);

		cmd->rx_attn_offset =
			le32_encode_bits(tlv_filter->rx_attn_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_ATTENTION_OFFSET);
	}

	if (tlv_filter->rx_mpdu_start_wmask > 0 &&
	    tlv_filter->rx_msdu_end_wmask > 0) {
		cmd->info2 |=
			le32_encode_bits(true,
					 HTT_RX_RING_SELECTION_CFG_WORD_MASK_COMPACT_SET);
		cmd->rx_mpdu_start_end_mask =
			le32_encode_bits(tlv_filter->rx_mpdu_start_wmask,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_START_MASK);
		/* mpdu_end is not used for any hardwares so far
		 * please assign it in future if any chip is
		 * using through hal ops
		 */
		cmd->rx_mpdu_start_end_mask |=
			le32_encode_bits(tlv_filter->rx_mpdu_end_wmask,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_END_MASK);
		cmd->rx_msdu_end_word_mask =
			le32_encode_bits(tlv_filter->rx_msdu_end_wmask,
					 HTT_RX_RING_SELECTION_CFG_RX_MSDU_END_MASK);
	}

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);

	return ret;
}

int
ath12k_dp_tx_htt_h2t_ext_stats_req(struct ath12k *ar, u8 type,
				   struct htt_ext_stats_cfg_params *cfg_params,
				   u64 cookie)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ext_stats_cfg_cmd *cmd;
	int len = sizeof(*cmd);
	int ret;
	u32 pdev_id;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);

	cmd = (struct htt_ext_stats_cfg_cmd *)skb->data;
	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_EXT_STATS_CFG;

	pdev_id = ath12k_mac_get_target_pdev_id(ar);
	cmd->hdr.pdev_mask = 1 << pdev_id;

	cmd->hdr.stats_type = type;
	cmd->cfg_param0 = cpu_to_le32(cfg_params->cfg0);
	cmd->cfg_param1 = cpu_to_le32(cfg_params->cfg1);
	cmd->cfg_param2 = cpu_to_le32(cfg_params->cfg2);
	cmd->cfg_param3 = cpu_to_le32(cfg_params->cfg3);
	cmd->cookie_lsb = cpu_to_le32(lower_32_bits(cookie));
	cmd->cookie_msb = cpu_to_le32(upper_32_bits(cookie));

	ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
	if (ret) {
		ath12k_warn(ab, "failed to send htt type stats request: %d",
			    ret);
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

int ath12k_dp_tx_htt_monitor_mode_ring_config(struct ath12k *ar, bool reset)
{
	struct ath12k_base *ab = ar->ab;
	int ret;

	ret = ath12k_dp_tx_htt_rx_monitor_mode_ring_config(ar, reset);
	if (ret) {
		ath12k_err(ab, "failed to setup rx monitor filter %d\n", ret);
		return ret;
	}

	return 0;
}

int ath12k_dp_tx_htt_rx_monitor_mode_ring_config(struct ath12k *ar, bool reset)
{
	struct ath12k_base *ab = ar->ab;
	struct htt_rx_ring_tlv_filter tlv_filter = {};
	int ret, ring_id, i;

	tlv_filter.offset_valid = false;

	if (!reset) {
		tlv_filter.rx_filter = HTT_RX_MON_FILTER_TLV_FLAGS_MON_DEST_RING;

		tlv_filter.drop_threshold_valid = true;
		tlv_filter.rx_drop_threshold = HTT_RX_RING_TLV_DROP_THRESHOLD_VALUE;

		tlv_filter.enable_log_mgmt_type = true;
		tlv_filter.enable_log_ctrl_type = true;
		tlv_filter.enable_log_data_type = true;

		tlv_filter.conf_len_ctrl = HTT_RX_RING_DEFAULT_DMA_LENGTH;
		tlv_filter.conf_len_mgmt = HTT_RX_RING_DEFAULT_DMA_LENGTH;
		tlv_filter.conf_len_data = HTT_RX_RING_DEFAULT_DMA_LENGTH;

		tlv_filter.enable_rx_tlv_offset = true;
		tlv_filter.rx_tlv_offset = HTT_RX_RING_PKT_TLV_OFFSET;

		tlv_filter.pkt_filter_flags0 =
					HTT_RX_MON_FP_MGMT_FILTER_FLAGS0 |
					HTT_RX_MON_MO_MGMT_FILTER_FLAGS0;
		tlv_filter.pkt_filter_flags1 =
					HTT_RX_MON_FP_MGMT_FILTER_FLAGS1 |
					HTT_RX_MON_MO_MGMT_FILTER_FLAGS1;
		tlv_filter.pkt_filter_flags2 =
					HTT_RX_MON_FP_CTRL_FILTER_FLASG2 |
					HTT_RX_MON_MO_CTRL_FILTER_FLASG2;
		tlv_filter.pkt_filter_flags3 =
					HTT_RX_MON_FP_CTRL_FILTER_FLASG3 |
					HTT_RX_MON_MO_CTRL_FILTER_FLASG3 |
					HTT_RX_MON_FP_DATA_FILTER_FLASG3 |
					HTT_RX_MON_MO_DATA_FILTER_FLASG3;
	} else {
		tlv_filter = ath12k_mac_mon_status_filter_default;

		if (ath12k_debugfs_is_extd_rx_stats_enabled(ar))
			tlv_filter.rx_filter = ath12k_debugfs_rx_filter(ar);
	}

	if (ab->hw_params->rxdma1_enable) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ring_id = ar->dp.rxdma_mon_dst_ring[i].ring_id;
			ret = ath12k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id,
							       ar->dp.mac_id + i,
							       HAL_RXDMA_MONITOR_DST,
							       DP_RXDMA_REFILL_RING_SIZE,
							       &tlv_filter);
			if (ret) {
				ath12k_err(ab,
					   "failed to setup filter for monitor buf %d\n",
					   ret);
				return ret;
			}
		}
		return 0;
	}

	if (!reset) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ring_id = ab->dp.rx_mac_buf_ring[i].ring_id;
			ret = ath12k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id,
							       i,
							       HAL_RXDMA_BUF,
							       DP_RXDMA_REFILL_RING_SIZE,
							       &tlv_filter);
			if (ret) {
				ath12k_err(ab,
					   "failed to setup filter for mon rx buf %d\n",
					   ret);
				return ret;
			}
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		ring_id = ab->dp.rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
		if (!reset) {
			tlv_filter.rx_filter =
				HTT_RX_MON_FILTER_TLV_FLAGS_MON_STATUS_RING;
		}

		ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id,
						       i,
						       HAL_RXDMA_MONITOR_STATUS,
						       RX_MON_STATUS_BUF_SIZE,
						       &tlv_filter);
		if (ret) {
			ath12k_err(ab,
				   "failed to setup filter for mon status buf %d\n",
				   ret);
			return ret;
		}
	}

	return 0;
}

int ath12k_dp_tx_htt_tx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int tx_buf_size,
				     struct htt_tx_ring_tlv_filter *htt_tlv_filter)
{
	struct htt_tx_ring_selection_cfg_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	int len = sizeof(*cmd);
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);

	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_tx_ring_selection_cfg_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_TX_MONITOR_CFG,
				      HTT_TX_RING_SELECTION_CFG_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |=
			le32_encode_bits(DP_SW2HW_MACID(mac_id),
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |=
			le32_encode_bits(mac_id,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_RING_ID);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_SS);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PS);

	cmd->info1 |=
		le32_encode_bits(tx_buf_size,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_RING_BUFF_SIZE);

	if (htt_tlv_filter->tx_mon_mgmt_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_MGMT,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_MGMT);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_MGMT,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	if (htt_tlv_filter->tx_mon_data_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_CTRL,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_CTRL);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_CTRL,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	if (htt_tlv_filter->tx_mon_ctrl_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_DATA,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_DATA);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_DATA,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	cmd->tlv_filter_mask_in0 =
		cpu_to_le32(htt_tlv_filter->tx_mon_downstream_tlv_flags);
	cmd->tlv_filter_mask_in1 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags0);
	cmd->tlv_filter_mask_in2 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags1);
	cmd->tlv_filter_mask_in3 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags2);

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);
	return ret;
}
