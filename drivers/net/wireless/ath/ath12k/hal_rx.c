// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "debug.h"
#include "hal.h"
#include "hal_tx.h"
#include "hal_rx.h"
#include "hal_desc.h"
#include "hif.h"

static void ath12k_hal_reo_set_desc_hdr(struct hal_desc_header *hdr,
					u8 owner, u8 buffer_type, u32 magic)
{
	hdr->info0 = le32_encode_bits(owner, HAL_DESC_HDR_INFO0_OWNER) |
		     le32_encode_bits(buffer_type, HAL_DESC_HDR_INFO0_BUF_TYPE);

	/* Magic pattern in reserved bits for debugging */
	hdr->info0 |= le32_encode_bits(magic, HAL_DESC_HDR_INFO0_DBG_RESERVED);
}

static int ath12k_hal_reo_cmd_queue_stats(struct hal_tlv_64_hdr *tlv,
					  struct ath12k_hal_reo_cmd *cmd)
{
	struct hal_reo_get_queue_stats *desc;

	tlv->tl = le64_encode_bits(HAL_REO_GET_QUEUE_STATS, HAL_TLV_HDR_TAG) |
		  le64_encode_bits(sizeof(*desc), HAL_TLV_HDR_LEN);

	desc = (struct hal_reo_get_queue_stats *)tlv->value;
	memset_startat(desc, 0, queue_addr_lo);

	desc->cmd.info0 &= ~cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);

	desc->queue_addr_lo = cpu_to_le32(cmd->addr_lo);
	desc->info0 = le32_encode_bits(cmd->addr_hi,
				       HAL_REO_GET_QUEUE_STATS_INFO0_QUEUE_ADDR_HI);
	if (cmd->flag & HAL_REO_CMD_FLG_STATS_CLEAR)
		desc->info0 |= cpu_to_le32(HAL_REO_GET_QUEUE_STATS_INFO0_CLEAR_STATS);

	return le32_get_bits(desc->cmd.info0, HAL_REO_CMD_HDR_INFO0_CMD_NUMBER);
}

static int ath12k_hal_reo_cmd_flush_cache(struct ath12k_hal *hal,
					  struct hal_tlv_64_hdr *tlv,
					  struct ath12k_hal_reo_cmd *cmd)
{
	struct hal_reo_flush_cache *desc;
	u8 avail_slot = ffz(hal->avail_blk_resource);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_BLOCK_LATER) {
		if (avail_slot >= HAL_MAX_AVAIL_BLK_RES)
			return -ENOSPC;

		hal->current_blk_index = avail_slot;
	}

	tlv->tl = le64_encode_bits(HAL_REO_FLUSH_CACHE, HAL_TLV_HDR_TAG) |
		  le64_encode_bits(sizeof(*desc), HAL_TLV_HDR_LEN);

	desc = (struct hal_reo_flush_cache *)tlv->value;
	memset_startat(desc, 0, cache_addr_lo);

	desc->cmd.info0 &= ~cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);

	desc->cache_addr_lo = cpu_to_le32(cmd->addr_lo);
	desc->info0 = le32_encode_bits(cmd->addr_hi,
				       HAL_REO_FLUSH_CACHE_INFO0_CACHE_ADDR_HI);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_FWD_ALL_MPDUS)
		desc->info0 |= cpu_to_le32(HAL_REO_FLUSH_CACHE_INFO0_FWD_ALL_MPDUS);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_BLOCK_LATER) {
		desc->info0 |= cpu_to_le32(HAL_REO_FLUSH_CACHE_INFO0_BLOCK_CACHE_USAGE);
		desc->info0 |=
			le32_encode_bits(avail_slot,
					 HAL_REO_FLUSH_CACHE_INFO0_BLOCK_RESRC_IDX);
	}

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_NO_INVAL)
		desc->info0 |= cpu_to_le32(HAL_REO_FLUSH_CACHE_INFO0_FLUSH_WO_INVALIDATE);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_ALL)
		desc->info0 |= cpu_to_le32(HAL_REO_FLUSH_CACHE_INFO0_FLUSH_ALL);

	return le32_get_bits(desc->cmd.info0, HAL_REO_CMD_HDR_INFO0_CMD_NUMBER);
}

static int ath12k_hal_reo_cmd_update_rx_queue(struct hal_tlv_64_hdr *tlv,
					      struct ath12k_hal_reo_cmd *cmd)
{
	struct hal_reo_update_rx_queue *desc;

	tlv->tl = le64_encode_bits(HAL_REO_UPDATE_RX_REO_QUEUE, HAL_TLV_HDR_TAG) |
		  le64_encode_bits(sizeof(*desc), HAL_TLV_HDR_LEN);

	desc = (struct hal_reo_update_rx_queue *)tlv->value;
	memset_startat(desc, 0, queue_addr_lo);

	desc->cmd.info0 &= ~cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= cpu_to_le32(HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED);

	desc->queue_addr_lo = cpu_to_le32(cmd->addr_lo);
	desc->info0 =
		le32_encode_bits(cmd->addr_hi,
				 HAL_REO_UPD_RX_QUEUE_INFO0_QUEUE_ADDR_HI) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_RX_QUEUE_NUM),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RX_QUEUE_NUM) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_VLD),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_VLD) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_ALDC),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_ASSOC_LNK_DESC_CNT) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_DIS_DUP_DETECTION),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_DIS_DUP_DETECTION) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_SOFT_REORDER_EN),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SOFT_REORDER_EN) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_AC),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_AC) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_BAR),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BAR) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_RETRY),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RETRY) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_CHECK_2K_MODE),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_CHECK_2K_MODE) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_OOR_MODE),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_OOR_MODE) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_BA_WINDOW_SIZE),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BA_WINDOW_SIZE) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_CHECK),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_CHECK) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_EVEN_PN),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_EVEN_PN) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_UNEVEN_PN),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_UNEVEN_PN) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_HANDLE_ENABLE),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_HANDLE_ENABLE) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_SIZE),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_SIZE) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_IGNORE_AMPDU_FLG),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_IGNORE_AMPDU_FLG) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_SVLD),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SVLD) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_SSN),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SSN) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_SEQ_2K_ERR),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SEQ_2K_ERR) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_VALID),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_VALID) |
		le32_encode_bits(!!(cmd->upd0 & HAL_REO_CMD_UPD0_PN),
				 HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN);

	desc->info1 =
		le32_encode_bits(cmd->rx_queue_num,
				 HAL_REO_UPD_RX_QUEUE_INFO1_RX_QUEUE_NUMBER) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_VLD),
				 HAL_REO_UPD_RX_QUEUE_INFO1_VLD) |
		le32_encode_bits(u32_get_bits(cmd->upd1, HAL_REO_CMD_UPD1_ALDC),
				 HAL_REO_UPD_RX_QUEUE_INFO1_ASSOC_LNK_DESC_COUNTER) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_DIS_DUP_DETECTION),
				 HAL_REO_UPD_RX_QUEUE_INFO1_DIS_DUP_DETECTION) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_SOFT_REORDER_EN),
				 HAL_REO_UPD_RX_QUEUE_INFO1_SOFT_REORDER_EN) |
		le32_encode_bits(u32_get_bits(cmd->upd1, HAL_REO_CMD_UPD1_AC),
				 HAL_REO_UPD_RX_QUEUE_INFO1_AC) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_BAR),
				 HAL_REO_UPD_RX_QUEUE_INFO1_BAR) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_CHECK_2K_MODE),
				 HAL_REO_UPD_RX_QUEUE_INFO1_CHECK_2K_MODE) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_RETRY),
				 HAL_REO_UPD_RX_QUEUE_INFO1_RETRY) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_OOR_MODE),
				 HAL_REO_UPD_RX_QUEUE_INFO1_OOR_MODE) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_PN_CHECK),
				 HAL_REO_UPD_RX_QUEUE_INFO1_PN_CHECK) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_EVEN_PN),
				 HAL_REO_UPD_RX_QUEUE_INFO1_EVEN_PN) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_UNEVEN_PN),
				 HAL_REO_UPD_RX_QUEUE_INFO1_UNEVEN_PN) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_PN_HANDLE_ENABLE),
				 HAL_REO_UPD_RX_QUEUE_INFO1_PN_HANDLE_ENABLE) |
		le32_encode_bits(!!(cmd->upd1 & HAL_REO_CMD_UPD1_IGNORE_AMPDU_FLG),
				 HAL_REO_UPD_RX_QUEUE_INFO1_IGNORE_AMPDU_FLG);

	if (cmd->pn_size == 24)
		cmd->pn_size = HAL_RX_REO_QUEUE_PN_SIZE_24;
	else if (cmd->pn_size == 48)
		cmd->pn_size = HAL_RX_REO_QUEUE_PN_SIZE_48;
	else if (cmd->pn_size == 128)
		cmd->pn_size = HAL_RX_REO_QUEUE_PN_SIZE_128;

	if (cmd->ba_window_size < 1)
		cmd->ba_window_size = 1;

	if (cmd->ba_window_size == 1)
		cmd->ba_window_size++;

	desc->info2 =
		le32_encode_bits(cmd->ba_window_size - 1,
				 HAL_REO_UPD_RX_QUEUE_INFO2_BA_WINDOW_SIZE) |
		le32_encode_bits(cmd->pn_size, HAL_REO_UPD_RX_QUEUE_INFO2_PN_SIZE) |
		le32_encode_bits(!!(cmd->upd2 & HAL_REO_CMD_UPD2_SVLD),
				 HAL_REO_UPD_RX_QUEUE_INFO2_SVLD) |
		le32_encode_bits(u32_get_bits(cmd->upd2, HAL_REO_CMD_UPD2_SSN),
				 HAL_REO_UPD_RX_QUEUE_INFO2_SSN) |
		le32_encode_bits(!!(cmd->upd2 & HAL_REO_CMD_UPD2_SEQ_2K_ERR),
				 HAL_REO_UPD_RX_QUEUE_INFO2_SEQ_2K_ERR) |
		le32_encode_bits(!!(cmd->upd2 & HAL_REO_CMD_UPD2_PN_ERR),
				 HAL_REO_UPD_RX_QUEUE_INFO2_PN_ERR);

	return le32_get_bits(desc->cmd.info0, HAL_REO_CMD_HDR_INFO0_CMD_NUMBER);
}

int ath12k_hal_reo_cmd_send(struct ath12k_base *ab, struct hal_srng *srng,
			    enum hal_reo_cmd_type type,
			    struct ath12k_hal_reo_cmd *cmd)
{
	struct hal_tlv_64_hdr *reo_desc;
	int ret;

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);
	reo_desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!reo_desc) {
		ret = -ENOBUFS;
		goto out;
	}

	switch (type) {
	case HAL_REO_CMD_GET_QUEUE_STATS:
		ret = ath12k_hal_reo_cmd_queue_stats(reo_desc, cmd);
		break;
	case HAL_REO_CMD_FLUSH_CACHE:
		ret = ath12k_hal_reo_cmd_flush_cache(&ab->hal, reo_desc, cmd);
		break;
	case HAL_REO_CMD_UPDATE_RX_QUEUE:
		ret = ath12k_hal_reo_cmd_update_rx_queue(reo_desc, cmd);
		break;
	case HAL_REO_CMD_FLUSH_QUEUE:
	case HAL_REO_CMD_UNBLOCK_CACHE:
	case HAL_REO_CMD_FLUSH_TIMEOUT_LIST:
		ath12k_warn(ab, "Unsupported reo command %d\n", type);
		ret = -EOPNOTSUPP;
		break;
	default:
		ath12k_warn(ab, "Unknown reo command %d\n", type);
		ret = -EINVAL;
		break;
	}

out:
	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return ret;
}

void ath12k_hal_rx_buf_addr_info_set(struct ath12k_buffer_addr *binfo,
				     dma_addr_t paddr, u32 cookie, u8 manager)
{
	u32 paddr_lo, paddr_hi;

	paddr_lo = lower_32_bits(paddr);
	paddr_hi = upper_32_bits(paddr);
	binfo->info0 = le32_encode_bits(paddr_lo, BUFFER_ADDR_INFO0_ADDR);
	binfo->info1 = le32_encode_bits(paddr_hi, BUFFER_ADDR_INFO1_ADDR) |
		       le32_encode_bits(cookie, BUFFER_ADDR_INFO1_SW_COOKIE) |
		       le32_encode_bits(manager, BUFFER_ADDR_INFO1_RET_BUF_MGR);
}

void ath12k_hal_rx_buf_addr_info_get(struct ath12k_buffer_addr *binfo,
				     dma_addr_t *paddr,
				     u32 *cookie, u8 *rbm)
{
	*paddr = (((u64)le32_get_bits(binfo->info1, BUFFER_ADDR_INFO1_ADDR)) << 32) |
		le32_get_bits(binfo->info0, BUFFER_ADDR_INFO0_ADDR);
	*cookie = le32_get_bits(binfo->info1, BUFFER_ADDR_INFO1_SW_COOKIE);
	*rbm = le32_get_bits(binfo->info1, BUFFER_ADDR_INFO1_RET_BUF_MGR);
}

void ath12k_hal_rx_msdu_link_info_get(struct hal_rx_msdu_link *link, u32 *num_msdus,
				      u32 *msdu_cookies,
				      enum hal_rx_buf_return_buf_manager *rbm)
{
	struct hal_rx_msdu_details *msdu;
	u32 val;
	int i;

	*num_msdus = HAL_NUM_RX_MSDUS_PER_LINK_DESC;

	msdu = &link->msdu_link[0];
	*rbm = le32_get_bits(msdu->buf_addr_info.info1,
			     BUFFER_ADDR_INFO1_RET_BUF_MGR);

	for (i = 0; i < *num_msdus; i++) {
		msdu = &link->msdu_link[i];

		val = le32_get_bits(msdu->buf_addr_info.info0,
				    BUFFER_ADDR_INFO0_ADDR);
		if (val == 0) {
			*num_msdus = i;
			break;
		}
		*msdu_cookies = le32_get_bits(msdu->buf_addr_info.info1,
					      BUFFER_ADDR_INFO1_SW_COOKIE);
		msdu_cookies++;
	}
}

int ath12k_hal_desc_reo_parse_err(struct ath12k_base *ab,
				  struct hal_reo_dest_ring *desc,
				  dma_addr_t *paddr, u32 *desc_bank)
{
	enum hal_reo_dest_ring_push_reason push_reason;
	enum hal_reo_dest_ring_error_code err_code;
	u32 cookie, val;

	push_reason = le32_get_bits(desc->info0,
				    HAL_REO_DEST_RING_INFO0_PUSH_REASON);
	err_code = le32_get_bits(desc->info0,
				 HAL_REO_DEST_RING_INFO0_ERROR_CODE);
	ab->soc_stats.reo_error[err_code]++;

	if (push_reason != HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED &&
	    push_reason != HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION) {
		ath12k_warn(ab, "expected error push reason code, received %d\n",
			    push_reason);
		return -EINVAL;
	}

	val = le32_get_bits(desc->info0, HAL_REO_DEST_RING_INFO0_BUFFER_TYPE);
	if (val != HAL_REO_DEST_RING_BUFFER_TYPE_LINK_DESC) {
		ath12k_warn(ab, "expected buffer type link_desc");
		return -EINVAL;
	}

	ath12k_hal_rx_reo_ent_paddr_get(ab, &desc->buf_addr_info, paddr, &cookie);
	*desc_bank = u32_get_bits(cookie, DP_LINK_DESC_BANK_MASK);

	return 0;
}

int ath12k_hal_wbm_desc_parse_err(struct ath12k_base *ab, void *desc,
				  struct hal_rx_wbm_rel_info *rel_info)
{
	struct hal_wbm_release_ring *wbm_desc = desc;
	struct hal_wbm_release_ring_cc_rx *wbm_cc_desc = desc;
	enum hal_wbm_rel_desc_type type;
	enum hal_wbm_rel_src_module rel_src;
	bool hw_cc_done;
	u64 desc_va;
	u32 val;

	type = le32_get_bits(wbm_desc->info0, HAL_WBM_RELEASE_INFO0_DESC_TYPE);
	/* We expect only WBM_REL buffer type */
	if (type != HAL_WBM_REL_DESC_TYPE_REL_MSDU) {
		WARN_ON(1);
		return -EINVAL;
	}

	rel_src = le32_get_bits(wbm_desc->info0,
				HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE);
	if (rel_src != HAL_WBM_REL_SRC_MODULE_RXDMA &&
	    rel_src != HAL_WBM_REL_SRC_MODULE_REO)
		return -EINVAL;

	/* The format of wbm rel ring desc changes based on the
	 * hw cookie conversion status
	 */
	hw_cc_done = le32_get_bits(wbm_desc->info0,
				   HAL_WBM_RELEASE_RX_INFO0_CC_STATUS);

	if (!hw_cc_done) {
		val = le32_get_bits(wbm_desc->buf_addr_info.info1,
				    BUFFER_ADDR_INFO1_RET_BUF_MGR);
		if (val != HAL_RX_BUF_RBM_SW3_BM) {
			ab->soc_stats.invalid_rbm++;
			return -EINVAL;
		}

		rel_info->cookie = le32_get_bits(wbm_desc->buf_addr_info.info1,
						 BUFFER_ADDR_INFO1_SW_COOKIE);

		rel_info->rx_desc = NULL;
	} else {
		val = le32_get_bits(wbm_cc_desc->info0,
				    HAL_WBM_RELEASE_RX_CC_INFO0_RBM);
		if (val != HAL_RX_BUF_RBM_SW3_BM) {
			ab->soc_stats.invalid_rbm++;
			return -EINVAL;
		}

		rel_info->cookie = le32_get_bits(wbm_cc_desc->info1,
						 HAL_WBM_RELEASE_RX_CC_INFO1_COOKIE);

		desc_va = ((u64)le32_to_cpu(wbm_cc_desc->buf_va_hi) << 32 |
			   le32_to_cpu(wbm_cc_desc->buf_va_lo));
		rel_info->rx_desc =
			(struct ath12k_rx_desc_info *)((unsigned long)desc_va);
	}

	rel_info->err_rel_src = rel_src;
	rel_info->hw_cc_done = hw_cc_done;

	rel_info->first_msdu = le32_get_bits(wbm_desc->info3,
					     HAL_WBM_RELEASE_INFO3_FIRST_MSDU);
	rel_info->last_msdu = le32_get_bits(wbm_desc->info3,
					    HAL_WBM_RELEASE_INFO3_LAST_MSDU);
	rel_info->continuation = le32_get_bits(wbm_desc->info3,
					       HAL_WBM_RELEASE_INFO3_CONTINUATION);

	if (rel_info->err_rel_src == HAL_WBM_REL_SRC_MODULE_REO) {
		rel_info->push_reason =
			le32_get_bits(wbm_desc->info0,
				      HAL_WBM_RELEASE_INFO0_REO_PUSH_REASON);
		rel_info->err_code =
			le32_get_bits(wbm_desc->info0,
				      HAL_WBM_RELEASE_INFO0_REO_ERROR_CODE);
	} else {
		rel_info->push_reason =
			le32_get_bits(wbm_desc->info0,
				      HAL_WBM_RELEASE_INFO0_RXDMA_PUSH_REASON);
		rel_info->err_code =
			le32_get_bits(wbm_desc->info0,
				      HAL_WBM_RELEASE_INFO0_RXDMA_ERROR_CODE);
	}

	return 0;
}

void ath12k_hal_rx_reo_ent_paddr_get(struct ath12k_base *ab,
				     struct ath12k_buffer_addr *buff_addr,
				     dma_addr_t *paddr, u32 *cookie)
{
	*paddr = ((u64)(le32_get_bits(buff_addr->info1,
				      BUFFER_ADDR_INFO1_ADDR)) << 32) |
		le32_get_bits(buff_addr->info0, BUFFER_ADDR_INFO0_ADDR);

	*cookie = le32_get_bits(buff_addr->info1, BUFFER_ADDR_INFO1_SW_COOKIE);
}

void ath12k_hal_rx_msdu_link_desc_set(struct ath12k_base *ab,
				      struct hal_wbm_release_ring *dst_desc,
				      struct hal_wbm_release_ring *src_desc,
				      enum hal_wbm_rel_bm_act action)
{
	dst_desc->buf_addr_info = src_desc->buf_addr_info;
	dst_desc->info0 |= le32_encode_bits(HAL_WBM_REL_SRC_MODULE_SW,
					    HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE) |
			   le32_encode_bits(action, HAL_WBM_RELEASE_INFO0_BM_ACTION) |
			   le32_encode_bits(HAL_WBM_REL_DESC_TYPE_MSDU_LINK,
					    HAL_WBM_RELEASE_INFO0_DESC_TYPE);
}

void ath12k_hal_reo_status_queue_stats(struct ath12k_base *ab, struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status)
{
	struct hal_reo_get_queue_stats_status *desc =
		(struct hal_reo_get_queue_stats_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				le32_get_bits(desc->hdr.info0,
					      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
				le32_get_bits(desc->hdr.info0,
					      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);

	ath12k_dbg(ab, ATH12K_DBG_HAL, "Queue stats status:\n");
	ath12k_dbg(ab, ATH12K_DBG_HAL, "header: cmd_num %d status %d\n",
		   status->uniform_hdr.cmd_num,
		   status->uniform_hdr.cmd_status);
	ath12k_dbg(ab, ATH12K_DBG_HAL, "ssn %u cur_idx %u\n",
		   le32_get_bits(desc->info0,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_SSN),
		   le32_get_bits(desc->info0,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_CUR_IDX));
	ath12k_dbg(ab, ATH12K_DBG_HAL, "pn = [%08x, %08x, %08x, %08x]\n",
		   desc->pn[0], desc->pn[1], desc->pn[2], desc->pn[3]);
	ath12k_dbg(ab, ATH12K_DBG_HAL, "last_rx: enqueue_tstamp %08x dequeue_tstamp %08x\n",
		   desc->last_rx_enqueue_timestamp,
		   desc->last_rx_dequeue_timestamp);
	ath12k_dbg(ab, ATH12K_DBG_HAL, "rx_bitmap [%08x %08x %08x %08x %08x %08x %08x %08x]\n",
		   desc->rx_bitmap[0], desc->rx_bitmap[1], desc->rx_bitmap[2],
		   desc->rx_bitmap[3], desc->rx_bitmap[4], desc->rx_bitmap[5],
		   desc->rx_bitmap[6], desc->rx_bitmap[7]);
	ath12k_dbg(ab, ATH12K_DBG_HAL, "count: cur_mpdu %u cur_msdu %u\n",
		   le32_get_bits(desc->info1,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MPDU_COUNT),
		   le32_get_bits(desc->info1,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MSDU_COUNT));
	ath12k_dbg(ab, ATH12K_DBG_HAL, "fwd_timeout %u fwd_bar %u dup_count %u\n",
		   le32_get_bits(desc->info2,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_TIMEOUT_COUNT),
		   le32_get_bits(desc->info2,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_FDTB_COUNT),
		   le32_get_bits(desc->info2,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_DUPLICATE_COUNT));
	ath12k_dbg(ab, ATH12K_DBG_HAL, "frames_in_order %u bar_rcvd %u\n",
		   le32_get_bits(desc->info3,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_FIO_COUNT),
		   le32_get_bits(desc->info3,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_BAR_RCVD_CNT));
	ath12k_dbg(ab, ATH12K_DBG_HAL, "num_mpdus %d num_msdus %d total_bytes %d\n",
		   desc->num_mpdu_frames, desc->num_msdu_frames,
		   desc->total_bytes);
	ath12k_dbg(ab, ATH12K_DBG_HAL, "late_rcvd %u win_jump_2k %u hole_cnt %u\n",
		   le32_get_bits(desc->info4,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_LATE_RX_MPDU),
		   le32_get_bits(desc->info2,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_WINDOW_JMP2K),
		   le32_get_bits(desc->info4,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_HOLE_COUNT));
	ath12k_dbg(ab, ATH12K_DBG_HAL, "looping count %u\n",
		   le32_get_bits(desc->info5,
				 HAL_REO_GET_QUEUE_STATS_STATUS_INFO5_LOOPING_CNT));
}

void ath12k_hal_reo_flush_queue_status(struct ath12k_base *ab, struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status)
{
	struct hal_reo_flush_queue_status *desc =
		(struct hal_reo_flush_queue_status *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);
	status->u.flush_queue.err_detected =
			le32_get_bits(desc->info0,
				      HAL_REO_FLUSH_QUEUE_INFO0_ERR_DETECTED);
}

void ath12k_hal_reo_flush_cache_status(struct ath12k_base *ab, struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_reo_flush_cache_status *desc =
		(struct hal_reo_flush_cache_status *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);

	status->u.flush_cache.err_detected =
			le32_get_bits(desc->info0,
				      HAL_REO_FLUSH_CACHE_STATUS_INFO0_IS_ERR);
	status->u.flush_cache.err_code =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_BLOCK_ERR_CODE);
	if (!status->u.flush_cache.err_code)
		hal->avail_blk_resource |= BIT(hal->current_blk_index);

	status->u.flush_cache.cache_controller_flush_status_hit =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_STATUS_HIT);

	status->u.flush_cache.cache_controller_flush_status_desc_type =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_DESC_TYPE);
	status->u.flush_cache.cache_controller_flush_status_client_id =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_CLIENT_ID);
	status->u.flush_cache.cache_controller_flush_status_err =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_ERR);
	status->u.flush_cache.cache_controller_flush_status_cnt =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_COUNT);
}

void ath12k_hal_reo_unblk_cache_status(struct ath12k_base *ab, struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_reo_unblock_cache_status *desc =
		(struct hal_reo_unblock_cache_status *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);

	status->u.unblock_cache.err_detected =
			le32_get_bits(desc->info0,
				      HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_IS_ERR);
	status->u.unblock_cache.unblock_type =
			le32_get_bits(desc->info0,
				      HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_TYPE);

	if (!status->u.unblock_cache.err_detected &&
	    status->u.unblock_cache.unblock_type ==
	    HAL_REO_STATUS_UNBLOCK_BLOCKING_RESOURCE)
		hal->avail_blk_resource &= ~BIT(hal->current_blk_index);
}

void ath12k_hal_reo_flush_timeout_list_status(struct ath12k_base *ab,
					      struct hal_tlv_64_hdr *tlv,
					      struct hal_reo_status *status)
{
	struct hal_reo_flush_timeout_list_status *desc =
		(struct hal_reo_flush_timeout_list_status *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);

	status->u.timeout_list.err_detected =
			le32_get_bits(desc->info0,
				      HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_IS_ERR);
	status->u.timeout_list.list_empty =
			le32_get_bits(desc->info0,
				      HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_LIST_EMPTY);

	status->u.timeout_list.release_desc_cnt =
		le32_get_bits(desc->info1,
			      HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_REL_DESC_COUNT);
	status->u.timeout_list.fwd_buf_cnt =
		le32_get_bits(desc->info0,
			      HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_FWD_BUF_COUNT);
}

void ath12k_hal_reo_desc_thresh_reached_status(struct ath12k_base *ab,
					       struct hal_tlv_64_hdr *tlv,
					       struct hal_reo_status *status)
{
	struct hal_reo_desc_thresh_reached_status *desc =
		(struct hal_reo_desc_thresh_reached_status *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->hdr.info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);

	status->u.desc_thresh_reached.threshold_idx =
		le32_get_bits(desc->info0,
			      HAL_REO_DESC_THRESH_STATUS_INFO0_THRESH_INDEX);

	status->u.desc_thresh_reached.link_desc_counter0 =
		le32_get_bits(desc->info1,
			      HAL_REO_DESC_THRESH_STATUS_INFO1_LINK_DESC_COUNTER0);

	status->u.desc_thresh_reached.link_desc_counter1 =
		le32_get_bits(desc->info2,
			      HAL_REO_DESC_THRESH_STATUS_INFO2_LINK_DESC_COUNTER1);

	status->u.desc_thresh_reached.link_desc_counter2 =
		le32_get_bits(desc->info3,
			      HAL_REO_DESC_THRESH_STATUS_INFO3_LINK_DESC_COUNTER2);

	status->u.desc_thresh_reached.link_desc_counter_sum =
		le32_get_bits(desc->info4,
			      HAL_REO_DESC_THRESH_STATUS_INFO4_LINK_DESC_COUNTER_SUM);
}

void ath12k_hal_reo_update_rx_reo_queue_status(struct ath12k_base *ab,
					       struct hal_tlv_64_hdr *tlv,
					       struct hal_reo_status *status)
{
	struct hal_reo_status_hdr *desc =
		(struct hal_reo_status_hdr *)tlv->value;

	status->uniform_hdr.cmd_num =
			le32_get_bits(desc->info0,
				      HAL_REO_STATUS_HDR_INFO0_STATUS_NUM);
	status->uniform_hdr.cmd_status =
			le32_get_bits(desc->info0,
				      HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS);
}

u32 ath12k_hal_reo_qdesc_size(u32 ba_window_size, u8 tid)
{
	u32 num_ext_desc, num_1k_desc = 0;

	if (ba_window_size <= 1) {
		if (tid != HAL_DESC_REO_NON_QOS_TID)
			num_ext_desc = 1;
		else
			num_ext_desc = 0;

	} else if (ba_window_size <= 105) {
		num_ext_desc = 1;
	} else if (ba_window_size <= 210) {
		num_ext_desc = 2;
	} else if (ba_window_size <= 256) {
		num_ext_desc = 3;
	} else {
		num_ext_desc = 10;
		num_1k_desc = 1;
	}

	return sizeof(struct hal_rx_reo_queue) +
		(num_ext_desc * sizeof(struct hal_rx_reo_queue_ext)) +
		(num_1k_desc * sizeof(struct hal_rx_reo_queue_1k));
}

void ath12k_hal_reo_qdesc_setup(struct hal_rx_reo_queue *qdesc,
				int tid, u32 ba_window_size,
				u32 start_seq, enum hal_pn_type type)
{
	struct hal_rx_reo_queue_ext *ext_desc;

	ath12k_hal_reo_set_desc_hdr(&qdesc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_0);

	qdesc->rx_queue_num = le32_encode_bits(tid, HAL_RX_REO_QUEUE_RX_QUEUE_NUMBER);

	qdesc->info0 =
		le32_encode_bits(1, HAL_RX_REO_QUEUE_INFO0_VLD) |
		le32_encode_bits(1, HAL_RX_REO_QUEUE_INFO0_ASSOC_LNK_DESC_COUNTER) |
		le32_encode_bits(ath12k_tid_to_ac(tid), HAL_RX_REO_QUEUE_INFO0_AC);

	if (ba_window_size < 1)
		ba_window_size = 1;

	if (ba_window_size == 1 && tid != HAL_DESC_REO_NON_QOS_TID)
		ba_window_size++;

	if (ba_window_size == 1)
		qdesc->info0 |= le32_encode_bits(1, HAL_RX_REO_QUEUE_INFO0_RETRY);

	qdesc->info0 |= le32_encode_bits(ba_window_size - 1,
					 HAL_RX_REO_QUEUE_INFO0_BA_WINDOW_SIZE);
	switch (type) {
	case HAL_PN_TYPE_NONE:
	case HAL_PN_TYPE_WAPI_EVEN:
	case HAL_PN_TYPE_WAPI_UNEVEN:
		break;
	case HAL_PN_TYPE_WPA:
		qdesc->info0 |=
			le32_encode_bits(1, HAL_RX_REO_QUEUE_INFO0_PN_CHECK) |
			le32_encode_bits(HAL_RX_REO_QUEUE_PN_SIZE_48,
					 HAL_RX_REO_QUEUE_INFO0_PN_SIZE);
		break;
	}

	/* TODO: Set Ignore ampdu flags based on BA window size and/or
	 * AMPDU capabilities
	 */
	qdesc->info0 |= le32_encode_bits(1, HAL_RX_REO_QUEUE_INFO0_IGNORE_AMPDU_FLG);

	qdesc->info1 |= le32_encode_bits(0, HAL_RX_REO_QUEUE_INFO1_SVLD);

	if (start_seq <= 0xfff)
		qdesc->info1 = le32_encode_bits(start_seq,
						HAL_RX_REO_QUEUE_INFO1_SSN);

	if (tid == HAL_DESC_REO_NON_QOS_TID)
		return;

	ext_desc = qdesc->ext_desc;

	/* TODO: HW queue descriptors are currently allocated for max BA
	 * window size for all QOS TIDs so that same descriptor can be used
	 * later when ADDBA request is received. This should be changed to
	 * allocate HW queue descriptors based on BA window size being
	 * negotiated (0 for non BA cases), and reallocate when BA window
	 * size changes and also send WMI message to FW to change the REO
	 * queue descriptor in Rx peer entry as part of dp_rx_tid_update.
	 */
	memset(ext_desc, 0, 3 * sizeof(*ext_desc));
	ath12k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_1);
	ext_desc++;
	ath12k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_2);
	ext_desc++;
	ath12k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_3);
}

void ath12k_hal_reo_init_cmd_ring(struct ath12k_base *ab,
				  struct hal_srng *srng)
{
	struct hal_srng_params params;
	struct hal_tlv_64_hdr *tlv;
	struct hal_reo_get_queue_stats *desc;
	int i, cmd_num = 1;
	int entry_size;
	u8 *entry;

	memset(&params, 0, sizeof(params));

	entry_size = ath12k_hal_srng_get_entrysize(ab, HAL_REO_CMD);
	ath12k_hal_srng_get_params(ab, srng, &params);
	entry = (u8 *)params.ring_base_vaddr;

	for (i = 0; i < params.num_entries; i++) {
		tlv = (struct hal_tlv_64_hdr *)entry;
		desc = (struct hal_reo_get_queue_stats *)tlv->value;
		desc->cmd.info0 = le32_encode_bits(cmd_num++,
						   HAL_REO_CMD_HDR_INFO0_CMD_NUMBER);
		entry += entry_size;
	}
}

void ath12k_hal_reo_hw_setup(struct ath12k_base *ab, u32 ring_hash_map)
{
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 val;

	val = ath12k_hif_read32(ab, reo_base + HAL_REO1_GEN_ENABLE);

	val |= u32_encode_bits(1, HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE) |
	       u32_encode_bits(1, HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_GEN_ENABLE, val);

	val = ath12k_hif_read32(ab, reo_base + HAL_REO1_MISC_CTRL_ADDR(ab));

	val &= ~(HAL_REO1_MISC_CTL_FRAG_DST_RING |
		 HAL_REO1_MISC_CTL_BAR_DST_RING);
	val |= u32_encode_bits(HAL_SRNG_RING_ID_REO2SW0,
			       HAL_REO1_MISC_CTL_FRAG_DST_RING);
	val |= u32_encode_bits(HAL_SRNG_RING_ID_REO2SW0,
			       HAL_REO1_MISC_CTL_BAR_DST_RING);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_MISC_CTRL_ADDR(ab), val);

	ath12k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_0(ab),
			   HAL_DEFAULT_BE_BK_VI_REO_TIMEOUT_USEC);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_1(ab),
			   HAL_DEFAULT_BE_BK_VI_REO_TIMEOUT_USEC);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_2(ab),
			   HAL_DEFAULT_BE_BK_VI_REO_TIMEOUT_USEC);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_3(ab),
			   HAL_DEFAULT_VO_REO_TIMEOUT_USEC);

	ath12k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_2,
			   ring_hash_map);
	ath12k_hif_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_3,
			   ring_hash_map);
}

void ath12k_hal_reo_shared_qaddr_cache_clear(struct ath12k_base *ab)
{
	u32 val;

	lockdep_assert_held(&ab->base_lock);
	val = ath12k_hif_read32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
				HAL_REO1_QDESC_ADDR(ab));

	val |= u32_encode_bits(1, HAL_REO_QDESC_ADDR_READ_CLEAR_QDESC_ARRAY);
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO1_QDESC_ADDR(ab), val);

	val &= ~HAL_REO_QDESC_ADDR_READ_CLEAR_QDESC_ARRAY;
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO1_QDESC_ADDR(ab), val);
}
