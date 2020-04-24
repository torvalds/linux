// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include "ahb.h"
#include "debug.h"
#include "hal.h"
#include "hal_tx.h"
#include "hal_rx.h"
#include "hal_desc.h"

static void ath11k_hal_reo_set_desc_hdr(struct hal_desc_header *hdr,
					u8 owner, u8 buffer_type, u32 magic)
{
	hdr->info0 = FIELD_PREP(HAL_DESC_HDR_INFO0_OWNER, owner) |
		     FIELD_PREP(HAL_DESC_HDR_INFO0_BUF_TYPE, buffer_type);

	/* Magic pattern in reserved bits for debugging */
	hdr->info0 |= FIELD_PREP(HAL_DESC_HDR_INFO0_DBG_RESERVED, magic);
}

static int ath11k_hal_reo_cmd_queue_stats(struct hal_tlv_hdr *tlv,
					  struct ath11k_hal_reo_cmd *cmd)
{
	struct hal_reo_get_queue_stats *desc;

	tlv->tl = FIELD_PREP(HAL_TLV_HDR_TAG, HAL_REO_GET_QUEUE_STATS) |
		  FIELD_PREP(HAL_TLV_HDR_LEN, sizeof(*desc));

	desc = (struct hal_reo_get_queue_stats *)tlv->value;
	memset(&desc->queue_addr_lo, 0,
	       (sizeof(*desc) - sizeof(struct hal_reo_cmd_hdr)));

	desc->cmd.info0 &= ~HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;

	desc->queue_addr_lo = cmd->addr_lo;
	desc->info0 = FIELD_PREP(HAL_REO_GET_QUEUE_STATS_INFO0_QUEUE_ADDR_HI,
				 cmd->addr_hi);
	if (cmd->flag & HAL_REO_CMD_FLG_STATS_CLEAR)
		desc->info0 |= HAL_REO_GET_QUEUE_STATS_INFO0_CLEAR_STATS;

	return FIELD_GET(HAL_REO_CMD_HDR_INFO0_CMD_NUMBER, desc->cmd.info0);
}

static int ath11k_hal_reo_cmd_flush_cache(struct ath11k_hal *hal, struct hal_tlv_hdr *tlv,
					  struct ath11k_hal_reo_cmd *cmd)
{
	struct hal_reo_flush_cache *desc;
	u8 avail_slot = ffz(hal->avail_blk_resource);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_BLOCK_LATER) {
		if (avail_slot >= HAL_MAX_AVAIL_BLK_RES)
			return -ENOSPC;

		hal->current_blk_index = avail_slot;
	}

	tlv->tl = FIELD_PREP(HAL_TLV_HDR_TAG, HAL_REO_FLUSH_CACHE) |
		  FIELD_PREP(HAL_TLV_HDR_LEN, sizeof(*desc));

	desc = (struct hal_reo_flush_cache *)tlv->value;
	memset(&desc->cache_addr_lo, 0,
	       (sizeof(*desc) - sizeof(struct hal_reo_cmd_hdr)));

	desc->cmd.info0 &= ~HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;

	desc->cache_addr_lo = cmd->addr_lo;
	desc->info0 = FIELD_PREP(HAL_REO_FLUSH_CACHE_INFO0_CACHE_ADDR_HI,
				 cmd->addr_hi);

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_FWD_ALL_MPDUS)
		desc->info0 |= HAL_REO_FLUSH_CACHE_INFO0_FWD_ALL_MPDUS;

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_BLOCK_LATER) {
		desc->info0 |= HAL_REO_FLUSH_CACHE_INFO0_BLOCK_CACHE_USAGE;
		desc->info0 |=
			FIELD_PREP(HAL_REO_FLUSH_CACHE_INFO0_BLOCK_RESRC_IDX,
				   avail_slot);
	}

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_NO_INVAL)
		desc->info0 |= HAL_REO_FLUSH_CACHE_INFO0_FLUSH_WO_INVALIDATE;

	if (cmd->flag & HAL_REO_CMD_FLG_FLUSH_ALL)
		desc->info0 |= HAL_REO_FLUSH_CACHE_INFO0_FLUSH_ALL;

	return FIELD_GET(HAL_REO_CMD_HDR_INFO0_CMD_NUMBER, desc->cmd.info0);
}

static int ath11k_hal_reo_cmd_update_rx_queue(struct hal_tlv_hdr *tlv,
					      struct ath11k_hal_reo_cmd *cmd)
{
	struct hal_reo_update_rx_queue *desc;

	tlv->tl = FIELD_PREP(HAL_TLV_HDR_TAG, HAL_REO_UPDATE_RX_REO_QUEUE) |
		  FIELD_PREP(HAL_TLV_HDR_LEN, sizeof(*desc));

	desc = (struct hal_reo_update_rx_queue *)tlv->value;
	memset(&desc->queue_addr_lo, 0,
	       (sizeof(*desc) - sizeof(struct hal_reo_cmd_hdr)));

	desc->cmd.info0 &= ~HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;
	if (cmd->flag & HAL_REO_CMD_FLG_NEED_STATUS)
		desc->cmd.info0 |= HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED;

	desc->queue_addr_lo = cmd->addr_lo;
	desc->info0 =
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_QUEUE_ADDR_HI,
			   cmd->addr_hi) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RX_QUEUE_NUM,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_RX_QUEUE_NUM)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_VLD,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_VLD)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_ASSOC_LNK_DESC_CNT,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_ALDC)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_DIS_DUP_DETECTION,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_DIS_DUP_DETECTION)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SOFT_REORDER_EN,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_SOFT_REORDER_EN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_AC,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_AC)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BAR,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_BAR)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RETRY,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_RETRY)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_CHECK_2K_MODE,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_CHECK_2K_MODE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_OOR_MODE,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_OOR_MODE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BA_WINDOW_SIZE,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_BA_WINDOW_SIZE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_CHECK,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_CHECK)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_EVEN_PN,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_EVEN_PN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_UNEVEN_PN,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_UNEVEN_PN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_HANDLE_ENABLE,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_HANDLE_ENABLE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_SIZE,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_SIZE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_IGNORE_AMPDU_FLG,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_IGNORE_AMPDU_FLG)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SVLD,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_SVLD)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SSN,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_SSN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SEQ_2K_ERR,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_SEQ_2K_ERR)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_VALID,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_PN_VALID)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN,
			   !!(cmd->upd0 & HAL_REO_CMD_UPD0_PN));

	desc->info1 =
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_RX_QUEUE_NUMBER,
			   cmd->rx_queue_num) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_VLD,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_VLD)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_ASSOC_LNK_DESC_COUNTER,
			   FIELD_GET(HAL_REO_CMD_UPD1_ALDC, cmd->upd1)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_DIS_DUP_DETECTION,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_DIS_DUP_DETECTION)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_SOFT_REORDER_EN,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_SOFT_REORDER_EN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_AC,
			   FIELD_GET(HAL_REO_CMD_UPD1_AC, cmd->upd1)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_BAR,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_BAR)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_CHECK_2K_MODE,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_CHECK_2K_MODE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_RETRY,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_RETRY)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_OOR_MODE,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_OOR_MODE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_PN_CHECK,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_PN_CHECK)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_EVEN_PN,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_EVEN_PN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_UNEVEN_PN,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_UNEVEN_PN)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_PN_HANDLE_ENABLE,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_PN_HANDLE_ENABLE)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO1_IGNORE_AMPDU_FLG,
			   !!(cmd->upd1 & HAL_REO_CMD_UPD1_IGNORE_AMPDU_FLG));

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
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_BA_WINDOW_SIZE,
			   cmd->ba_window_size - 1) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_PN_SIZE, cmd->pn_size) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_SVLD,
			   !!(cmd->upd2 & HAL_REO_CMD_UPD2_SVLD)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_SSN,
			   FIELD_GET(HAL_REO_CMD_UPD2_SSN, cmd->upd2)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_SEQ_2K_ERR,
			   !!(cmd->upd2 & HAL_REO_CMD_UPD2_SEQ_2K_ERR)) |
		FIELD_PREP(HAL_REO_UPD_RX_QUEUE_INFO2_PN_ERR,
			   !!(cmd->upd2 & HAL_REO_CMD_UPD2_PN_ERR));

	return FIELD_GET(HAL_REO_CMD_HDR_INFO0_CMD_NUMBER, desc->cmd.info0);
}

int ath11k_hal_reo_cmd_send(struct ath11k_base *ab, struct hal_srng *srng,
			    enum hal_reo_cmd_type type,
			    struct ath11k_hal_reo_cmd *cmd)
{
	struct hal_tlv_hdr *reo_desc;
	int ret;

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);
	reo_desc = (struct hal_tlv_hdr *)ath11k_hal_srng_src_get_next_entry(ab, srng);
	if (!reo_desc) {
		ret = -ENOBUFS;
		goto out;
	}

	switch (type) {
	case HAL_REO_CMD_GET_QUEUE_STATS:
		ret = ath11k_hal_reo_cmd_queue_stats(reo_desc, cmd);
		break;
	case HAL_REO_CMD_FLUSH_CACHE:
		ret = ath11k_hal_reo_cmd_flush_cache(&ab->hal, reo_desc, cmd);
		break;
	case HAL_REO_CMD_UPDATE_RX_QUEUE:
		ret = ath11k_hal_reo_cmd_update_rx_queue(reo_desc, cmd);
		break;
	case HAL_REO_CMD_FLUSH_QUEUE:
	case HAL_REO_CMD_UNBLOCK_CACHE:
	case HAL_REO_CMD_FLUSH_TIMEOUT_LIST:
		ath11k_warn(ab, "Unsupported reo command %d\n", type);
		ret = -ENOTSUPP;
		break;
	default:
		ath11k_warn(ab, "Unknown reo command %d\n", type);
		ret = -EINVAL;
		break;
	}

out:
	ath11k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return ret;
}

void ath11k_hal_rx_buf_addr_info_set(void *desc, dma_addr_t paddr,
				     u32 cookie, u8 manager)
{
	struct ath11k_buffer_addr *binfo = (struct ath11k_buffer_addr *)desc;
	u32 paddr_lo, paddr_hi;

	paddr_lo = lower_32_bits(paddr);
	paddr_hi = upper_32_bits(paddr);
	binfo->info0 = FIELD_PREP(BUFFER_ADDR_INFO0_ADDR, paddr_lo);
	binfo->info1 = FIELD_PREP(BUFFER_ADDR_INFO1_ADDR, paddr_hi) |
		       FIELD_PREP(BUFFER_ADDR_INFO1_SW_COOKIE, cookie) |
		       FIELD_PREP(BUFFER_ADDR_INFO1_RET_BUF_MGR, manager);
}

void ath11k_hal_rx_buf_addr_info_get(void *desc, dma_addr_t *paddr,
				     u32 *cookie, u8 *rbm)
{
	struct ath11k_buffer_addr *binfo = (struct ath11k_buffer_addr *)desc;

	*paddr =
		(((u64)FIELD_GET(BUFFER_ADDR_INFO1_ADDR, binfo->info1)) << 32) |
		FIELD_GET(BUFFER_ADDR_INFO0_ADDR, binfo->info0);
	*cookie = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE, binfo->info1);
	*rbm = FIELD_GET(BUFFER_ADDR_INFO1_RET_BUF_MGR, binfo->info1);
}

void ath11k_hal_rx_msdu_link_info_get(void *link_desc, u32 *num_msdus,
				      u32 *msdu_cookies,
				      enum hal_rx_buf_return_buf_manager *rbm)
{
	struct hal_rx_msdu_link *link = (struct hal_rx_msdu_link *)link_desc;
	struct hal_rx_msdu_details *msdu;
	int i;

	*num_msdus = HAL_NUM_RX_MSDUS_PER_LINK_DESC;

	msdu = &link->msdu_link[0];
	*rbm = FIELD_GET(BUFFER_ADDR_INFO1_RET_BUF_MGR,
			 msdu->buf_addr_info.info1);

	for (i = 0; i < *num_msdus; i++) {
		msdu = &link->msdu_link[i];

		if (!FIELD_GET(BUFFER_ADDR_INFO0_ADDR,
			       msdu->buf_addr_info.info0)) {
			*num_msdus = i;
			break;
		}
		*msdu_cookies = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
					  msdu->buf_addr_info.info1);
		msdu_cookies++;
	}
}

int ath11k_hal_desc_reo_parse_err(struct ath11k_base *ab, u32 *rx_desc,
				  dma_addr_t *paddr, u32 *desc_bank)
{
	struct hal_reo_dest_ring *desc = (struct hal_reo_dest_ring *)rx_desc;
	enum hal_reo_dest_ring_push_reason push_reason;
	enum hal_reo_dest_ring_error_code err_code;

	push_reason = FIELD_GET(HAL_REO_DEST_RING_INFO0_PUSH_REASON,
				desc->info0);
	err_code = FIELD_GET(HAL_REO_DEST_RING_INFO0_ERROR_CODE,
			     desc->info0);
	ab->soc_stats.reo_error[err_code]++;

	if (push_reason != HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED &&
	    push_reason != HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION) {
		ath11k_warn(ab, "expected error push reason code, received %d\n",
			    push_reason);
		return -EINVAL;
	}

	if (FIELD_GET(HAL_REO_DEST_RING_INFO0_BUFFER_TYPE, desc->info0) !=
	    HAL_REO_DEST_RING_BUFFER_TYPE_LINK_DESC) {
		ath11k_warn(ab, "expected buffer type link_desc");
		return -EINVAL;
	}

	ath11k_hal_rx_reo_ent_paddr_get(ab, rx_desc, paddr, desc_bank);

	return 0;
}

int ath11k_hal_wbm_desc_parse_err(struct ath11k_base *ab, void *desc,
				  struct hal_rx_wbm_rel_info *rel_info)
{
	struct hal_wbm_release_ring *wbm_desc = desc;
	enum hal_wbm_rel_desc_type type;
	enum hal_wbm_rel_src_module rel_src;

	type = FIELD_GET(HAL_WBM_RELEASE_INFO0_DESC_TYPE,
			 wbm_desc->info0);
	/* We expect only WBM_REL buffer type */
	if (type != HAL_WBM_REL_DESC_TYPE_REL_MSDU) {
		WARN_ON(1);
		return -EINVAL;
	}

	rel_src = FIELD_GET(HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE,
			    wbm_desc->info0);
	if (rel_src != HAL_WBM_REL_SRC_MODULE_RXDMA &&
	    rel_src != HAL_WBM_REL_SRC_MODULE_REO)
		return -EINVAL;

	if (FIELD_GET(BUFFER_ADDR_INFO1_RET_BUF_MGR,
		      wbm_desc->buf_addr_info.info1) != HAL_RX_BUF_RBM_SW3_BM) {
		ab->soc_stats.invalid_rbm++;
		return -EINVAL;
	}

	rel_info->cookie = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
				     wbm_desc->buf_addr_info.info1);
	rel_info->err_rel_src = rel_src;
	if (rel_src == HAL_WBM_REL_SRC_MODULE_REO) {
		rel_info->push_reason =
			FIELD_GET(HAL_WBM_RELEASE_INFO0_REO_PUSH_REASON,
				  wbm_desc->info0);
		rel_info->err_code =
			FIELD_GET(HAL_WBM_RELEASE_INFO0_REO_ERROR_CODE,
				  wbm_desc->info0);
	} else {
		rel_info->push_reason =
			FIELD_GET(HAL_WBM_RELEASE_INFO0_RXDMA_PUSH_REASON,
				  wbm_desc->info0);
		rel_info->err_code =
			FIELD_GET(HAL_WBM_RELEASE_INFO0_RXDMA_ERROR_CODE,
				  wbm_desc->info0);
	}

	rel_info->first_msdu = FIELD_GET(HAL_WBM_RELEASE_INFO2_FIRST_MSDU,
					 wbm_desc->info2);
	rel_info->last_msdu = FIELD_GET(HAL_WBM_RELEASE_INFO2_LAST_MSDU,
					wbm_desc->info2);
	return 0;
}

void ath11k_hal_rx_reo_ent_paddr_get(struct ath11k_base *ab, void *desc,
				     dma_addr_t *paddr, u32 *desc_bank)
{
	struct ath11k_buffer_addr *buff_addr = desc;

	*paddr = ((u64)(FIELD_GET(BUFFER_ADDR_INFO1_ADDR, buff_addr->info1)) << 32) |
		  FIELD_GET(BUFFER_ADDR_INFO0_ADDR, buff_addr->info0);

	*desc_bank = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE, buff_addr->info1);
}

void ath11k_hal_rx_msdu_link_desc_set(struct ath11k_base *ab, void *desc,
				      void *link_desc,
				      enum hal_wbm_rel_bm_act action)
{
	struct hal_wbm_release_ring *dst_desc = desc;
	struct hal_wbm_release_ring *src_desc = link_desc;

	dst_desc->buf_addr_info = src_desc->buf_addr_info;
	dst_desc->info0 |= FIELD_PREP(HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE,
				      HAL_WBM_REL_SRC_MODULE_SW) |
			   FIELD_PREP(HAL_WBM_RELEASE_INFO0_BM_ACTION, action) |
			   FIELD_PREP(HAL_WBM_RELEASE_INFO0_DESC_TYPE,
				      HAL_WBM_REL_DESC_TYPE_MSDU_LINK);
}

void ath11k_hal_reo_status_queue_stats(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_get_queue_stats_status *desc =
		(struct hal_reo_get_queue_stats_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);

	ath11k_dbg(ab, ATH11k_DBG_HAL, "Queue stats status:\n");
	ath11k_dbg(ab, ATH11k_DBG_HAL, "header: cmd_num %d status %d\n",
		   status->uniform_hdr.cmd_num,
		   status->uniform_hdr.cmd_status);
	ath11k_dbg(ab, ATH11k_DBG_HAL, "ssn %ld cur_idx %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_SSN,
			     desc->info0),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_CUR_IDX,
			     desc->info0));
	ath11k_dbg(ab, ATH11k_DBG_HAL, "pn = [%08x, %08x, %08x, %08x]\n",
		   desc->pn[0], desc->pn[1], desc->pn[2], desc->pn[3]);
	ath11k_dbg(ab, ATH11k_DBG_HAL, "last_rx: enqueue_tstamp %08x dequeue_tstamp %08x\n",
		   desc->last_rx_enqueue_timestamp,
		   desc->last_rx_dequeue_timestamp);
	ath11k_dbg(ab, ATH11k_DBG_HAL, "rx_bitmap [%08x %08x %08x %08x %08x %08x %08x %08x]\n",
		   desc->rx_bitmap[0], desc->rx_bitmap[1], desc->rx_bitmap[2],
		   desc->rx_bitmap[3], desc->rx_bitmap[4], desc->rx_bitmap[5],
		   desc->rx_bitmap[6], desc->rx_bitmap[7]);
	ath11k_dbg(ab, ATH11k_DBG_HAL, "count: cur_mpdu %ld cur_msdu %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MPDU_COUNT,
			     desc->info1),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MSDU_COUNT,
			     desc->info1));
	ath11k_dbg(ab, ATH11k_DBG_HAL, "fwd_timeout %ld fwd_bar %ld dup_count %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_TIMEOUT_COUNT,
			     desc->info2),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_FDTB_COUNT,
			     desc->info2),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_DUPLICATE_COUNT,
			     desc->info2));
	ath11k_dbg(ab, ATH11k_DBG_HAL, "frames_in_order %ld bar_rcvd %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_FIO_COUNT,
			     desc->info3),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_BAR_RCVD_CNT,
			     desc->info3));
	ath11k_dbg(ab, ATH11k_DBG_HAL, "num_mpdus %d num_msdus %d total_bytes %d\n",
		   desc->num_mpdu_frames, desc->num_msdu_frames,
		   desc->total_bytes);
	ath11k_dbg(ab, ATH11k_DBG_HAL, "late_rcvd %ld win_jump_2k %ld hole_cnt %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_LATE_RX_MPDU,
			     desc->info4),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_WINDOW_JMP2K,
			     desc->info4),
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_HOLE_COUNT,
			     desc->info4));
	ath11k_dbg(ab, ATH11k_DBG_HAL, "looping count %ld\n",
		   FIELD_GET(HAL_REO_GET_QUEUE_STATS_STATUS_INFO5_LOOPING_CNT,
			     desc->info5));
}

int ath11k_hal_reo_process_status(u8 *reo_desc, u8 *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_status_hdr *hdr;

	hdr = (struct hal_reo_status_hdr *)tlv->value;
	*status = FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS, hdr->info0);

	return FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM, hdr->info0);
}

void ath11k_hal_reo_flush_queue_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_flush_queue_status *desc =
		(struct hal_reo_flush_queue_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);
	status->u.flush_queue.err_detected =
		FIELD_GET(HAL_REO_FLUSH_QUEUE_INFO0_ERR_DETECTED,
			  desc->info0);
}

void ath11k_hal_reo_flush_cache_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status)
{
	struct ath11k_hal *hal = &ab->hal;
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_flush_cache_status *desc =
		(struct hal_reo_flush_cache_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);

	status->u.flush_cache.err_detected =
			FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_IS_ERR,
				  desc->info0);
	status->u.flush_cache.err_code =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_BLOCK_ERR_CODE,
			  desc->info0);
	if (!status->u.flush_cache.err_code)
		hal->avail_blk_resource |= BIT(hal->current_blk_index);

	status->u.flush_cache.cache_controller_flush_status_hit =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_STATUS_HIT,
			  desc->info0);

	status->u.flush_cache.cache_controller_flush_status_desc_type =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_DESC_TYPE,
			  desc->info0);
	status->u.flush_cache.cache_controller_flush_status_client_id =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_CLIENT_ID,
			  desc->info0);
	status->u.flush_cache.cache_controller_flush_status_err =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_ERR,
			  desc->info0);
	status->u.flush_cache.cache_controller_flush_status_cnt =
		FIELD_GET(HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_COUNT,
			  desc->info0);
}

void ath11k_hal_reo_unblk_cache_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status)
{
	struct ath11k_hal *hal = &ab->hal;
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_unblock_cache_status *desc =
		(struct hal_reo_unblock_cache_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);

	status->u.unblock_cache.err_detected =
			FIELD_GET(HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_IS_ERR,
				  desc->info0);
	status->u.unblock_cache.unblock_type =
			FIELD_GET(HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_TYPE,
				  desc->info0);

	if (!status->u.unblock_cache.err_detected &&
	    status->u.unblock_cache.unblock_type ==
	    HAL_REO_STATUS_UNBLOCK_BLOCKING_RESOURCE)
		hal->avail_blk_resource &= ~BIT(hal->current_blk_index);
}

void ath11k_hal_reo_flush_timeout_list_status(struct ath11k_base *ab,
					      u32 *reo_desc,
					      struct hal_reo_status *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_flush_timeout_list_status *desc =
		(struct hal_reo_flush_timeout_list_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);

	status->u.timeout_list.err_detected =
			FIELD_GET(HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_IS_ERR,
				  desc->info0);
	status->u.timeout_list.list_empty =
			FIELD_GET(HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_LIST_EMPTY,
				  desc->info0);

	status->u.timeout_list.release_desc_cnt =
		FIELD_GET(HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_REL_DESC_COUNT,
			  desc->info1);
	status->u.timeout_list.fwd_buf_cnt =
		FIELD_GET(HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_FWD_BUF_COUNT,
			  desc->info1);
}

void ath11k_hal_reo_desc_thresh_reached_status(struct ath11k_base *ab,
					       u32 *reo_desc,
					       struct hal_reo_status *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_desc_thresh_reached_status *desc =
		(struct hal_reo_desc_thresh_reached_status *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->hdr.info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->hdr.info0);

	status->u.desc_thresh_reached.threshold_idx =
		FIELD_GET(HAL_REO_DESC_THRESH_STATUS_INFO0_THRESH_INDEX,
			  desc->info0);

	status->u.desc_thresh_reached.link_desc_counter0 =
		FIELD_GET(HAL_REO_DESC_THRESH_STATUS_INFO1_LINK_DESC_COUNTER0,
			  desc->info1);

	status->u.desc_thresh_reached.link_desc_counter1 =
		FIELD_GET(HAL_REO_DESC_THRESH_STATUS_INFO2_LINK_DESC_COUNTER1,
			  desc->info2);

	status->u.desc_thresh_reached.link_desc_counter2 =
		FIELD_GET(HAL_REO_DESC_THRESH_STATUS_INFO3_LINK_DESC_COUNTER2,
			  desc->info3);

	status->u.desc_thresh_reached.link_desc_counter_sum =
		FIELD_GET(HAL_REO_DESC_THRESH_STATUS_INFO4_LINK_DESC_COUNTER_SUM,
			  desc->info4);
}

void ath11k_hal_reo_update_rx_reo_queue_status(struct ath11k_base *ab,
					       u32 *reo_desc,
					       struct hal_reo_status *status)
{
	struct hal_tlv_hdr *tlv = (struct hal_tlv_hdr *)reo_desc;
	struct hal_reo_status_hdr *desc =
		(struct hal_reo_status_hdr *)tlv->value;

	status->uniform_hdr.cmd_num =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_STATUS_NUM,
					  desc->info0);
	status->uniform_hdr.cmd_status =
				FIELD_GET(HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS,
					  desc->info0);
}

u32 ath11k_hal_reo_qdesc_size(u32 ba_window_size, u8 tid)
{
	u32 num_ext_desc;

	if (ba_window_size <= 1) {
		if (tid != HAL_DESC_REO_NON_QOS_TID)
			num_ext_desc = 1;
		else
			num_ext_desc = 0;
	} else if (ba_window_size <= 105) {
		num_ext_desc = 1;
	} else if (ba_window_size <= 210) {
		num_ext_desc = 2;
	} else {
		num_ext_desc = 3;
	}

	return sizeof(struct hal_rx_reo_queue) +
		(num_ext_desc * sizeof(struct hal_rx_reo_queue_ext));
}

void ath11k_hal_reo_qdesc_setup(void *vaddr, int tid, u32 ba_window_size,
				u32 start_seq, enum hal_pn_type type)
{
	struct hal_rx_reo_queue *qdesc = (struct hal_rx_reo_queue *)vaddr;
	struct hal_rx_reo_queue_ext *ext_desc;

	memset(qdesc, 0, sizeof(*qdesc));

	ath11k_hal_reo_set_desc_hdr(&qdesc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_0);

	qdesc->rx_queue_num = FIELD_PREP(HAL_RX_REO_QUEUE_RX_QUEUE_NUMBER, tid);

	qdesc->info0 =
		FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_VLD, 1) |
		FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_ASSOC_LNK_DESC_COUNTER, 1) |
		FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_AC, ath11k_tid_to_ac(tid));

	if (ba_window_size < 1)
		ba_window_size = 1;

	if (ba_window_size == 1 && tid != HAL_DESC_REO_NON_QOS_TID)
		ba_window_size++;

	if (ba_window_size == 1)
		qdesc->info0 |= FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_RETRY, 1);

	qdesc->info0 |= FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_BA_WINDOW_SIZE,
				   ba_window_size - 1);
	switch (type) {
	case HAL_PN_TYPE_NONE:
	case HAL_PN_TYPE_WAPI_EVEN:
	case HAL_PN_TYPE_WAPI_UNEVEN:
		break;
	case HAL_PN_TYPE_WPA:
		qdesc->info0 |=
			FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_PN_CHECK, 1) |
			FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_PN_SIZE,
				   HAL_RX_REO_QUEUE_PN_SIZE_48);
		break;
	}

	/* TODO: Set Ignore ampdu flags based on BA window size and/or
	 * AMPDU capabilities
	 */
	qdesc->info0 |= FIELD_PREP(HAL_RX_REO_QUEUE_INFO0_IGNORE_AMPDU_FLG, 1);

	qdesc->info1 |= FIELD_PREP(HAL_RX_REO_QUEUE_INFO1_SVLD, 0);

	if (start_seq <= 0xfff)
		qdesc->info1 = FIELD_PREP(HAL_RX_REO_QUEUE_INFO1_SSN,
					  start_seq);

	if (tid == HAL_DESC_REO_NON_QOS_TID)
		return;

	ext_desc = qdesc->ext_desc;

	/* TODO: HW queue descriptors are currently allocated for max BA
	 * window size for all QOS TIDs so that same descriptor can be used
	 * later when ADDBA request is recevied. This should be changed to
	 * allocate HW queue descriptors based on BA window size being
	 * negotiated (0 for non BA cases), and reallocate when BA window
	 * size changes and also send WMI message to FW to change the REO
	 * queue descriptor in Rx peer entry as part of dp_rx_tid_update.
	 */
	memset(ext_desc, 0, 3 * sizeof(*ext_desc));
	ath11k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_1);
	ext_desc++;
	ath11k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_2);
	ext_desc++;
	ath11k_hal_reo_set_desc_hdr(&ext_desc->desc_hdr, HAL_DESC_REO_OWNED,
				    HAL_DESC_REO_QUEUE_EXT_DESC,
				    REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_3);
}

void ath11k_hal_reo_init_cmd_ring(struct ath11k_base *ab,
				  struct hal_srng *srng)
{
	struct hal_srng_params params;
	struct hal_tlv_hdr *tlv;
	struct hal_reo_get_queue_stats *desc;
	int i, cmd_num = 1;
	int entry_size;
	u8 *entry;

	memset(&params, 0, sizeof(params));

	entry_size = ath11k_hal_srng_get_entrysize(HAL_REO_CMD);
	ath11k_hal_srng_get_params(ab, srng, &params);
	entry = (u8 *)params.ring_base_vaddr;

	for (i = 0; i < params.num_entries; i++) {
		tlv = (struct hal_tlv_hdr *)entry;
		desc = (struct hal_reo_get_queue_stats *)tlv->value;
		desc->cmd.info0 =
			FIELD_PREP(HAL_REO_CMD_HDR_INFO0_CMD_NUMBER, cmd_num++);
		entry += entry_size;
	}
}

void ath11k_hal_reo_hw_setup(struct ath11k_base *ab, u32 ring_hash_map)
{
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 val;

	val = ath11k_ahb_read32(ab, reo_base + HAL_REO1_GEN_ENABLE);

	val &= ~HAL_REO1_GEN_ENABLE_FRAG_DST_RING;
	val |= FIELD_PREP(HAL_REO1_GEN_ENABLE_FRAG_DST_RING,
			  HAL_SRNG_RING_ID_REO2SW1) |
	       FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_LIST_ENABLE, 1) |
	       FIELD_PREP(HAL_REO1_GEN_ENABLE_AGING_FLUSH_ENABLE, 1);
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_GEN_ENABLE, val);

	ath11k_ahb_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_0,
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_1,
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_2,
			   HAL_DEFAULT_REO_TIMEOUT_USEC);
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_AGING_THRESH_IX_3,
			   HAL_DEFAULT_REO_TIMEOUT_USEC);

	ath11k_ahb_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_0,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_1,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_2,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
	ath11k_ahb_write32(ab, reo_base + HAL_REO1_DEST_RING_CTRL_IX_3,
			   FIELD_PREP(HAL_REO_DEST_RING_CTRL_HASH_RING_MAP,
				      ring_hash_map));
}

static enum hal_rx_mon_status
ath11k_hal_rx_parse_mon_status_tlv(struct ath11k_base *ab,
				   struct hal_rx_mon_ppdu_info *ppdu_info,
				   u32 tlv_tag, u8 *tlv_data)
{
	u32 info0, info1;

	switch (tlv_tag) {
	case HAL_RX_PPDU_START: {
		struct hal_rx_ppdu_start *ppdu_start =
			(struct hal_rx_ppdu_start *)tlv_data;

		ppdu_info->ppdu_id =
			FIELD_GET(HAL_RX_PPDU_START_INFO0_PPDU_ID,
				  __le32_to_cpu(ppdu_start->info0));
		ppdu_info->chan_num = __le32_to_cpu(ppdu_start->chan_num);
		ppdu_info->ppdu_ts = __le32_to_cpu(ppdu_start->ppdu_start_ts);
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS: {
		struct hal_rx_ppdu_end_user_stats *eu_stats =
			(struct hal_rx_ppdu_end_user_stats *)tlv_data;

		info0 = __le32_to_cpu(eu_stats->info0);
		info1 = __le32_to_cpu(eu_stats->info1);

		ppdu_info->tid =
			ffs(FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP,
				      __le32_to_cpu(eu_stats->info6))) - 1;
		ppdu_info->tcp_msdu_count =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO4_TCP_MSDU_CNT,
				  __le32_to_cpu(eu_stats->info4));
		ppdu_info->udp_msdu_count =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO4_UDP_MSDU_CNT,
				  __le32_to_cpu(eu_stats->info4));
		ppdu_info->other_msdu_count =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO5_OTHER_MSDU_CNT,
				  __le32_to_cpu(eu_stats->info5));
		ppdu_info->tcp_ack_msdu_count =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO5_TCP_ACK_MSDU_CNT,
				  __le32_to_cpu(eu_stats->info5));
		ppdu_info->preamble_type =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO1_PKT_TYPE, info1);
		ppdu_info->num_mpdu_fcs_ok =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO1_MPDU_CNT_FCS_OK,
				  info1);
		ppdu_info->num_mpdu_fcs_err =
			FIELD_GET(HAL_RX_PPDU_END_USER_STATS_INFO0_MPDU_CNT_FCS_ERR,
				  info0);
		break;
	}
	case HAL_PHYRX_HT_SIG: {
		struct hal_rx_ht_sig_info *ht_sig =
			(struct hal_rx_ht_sig_info *)tlv_data;

		info0 = __le32_to_cpu(ht_sig->info0);
		info1 = __le32_to_cpu(ht_sig->info1);

		ppdu_info->mcs = FIELD_GET(HAL_RX_HT_SIG_INFO_INFO0_MCS, info0);
		ppdu_info->bw = FIELD_GET(HAL_RX_HT_SIG_INFO_INFO0_BW, info0);
		ppdu_info->is_stbc = FIELD_GET(HAL_RX_HT_SIG_INFO_INFO1_STBC,
					       info1);
		ppdu_info->ldpc = FIELD_GET(HAL_RX_HT_SIG_INFO_INFO1_FEC_CODING, info1);
		ppdu_info->gi = info1 & HAL_RX_HT_SIG_INFO_INFO1_GI;

		switch (ppdu_info->mcs) {
		case 0 ... 7:
			ppdu_info->nss = 1;
			break;
		case 8 ... 15:
			ppdu_info->nss = 2;
			break;
		case 16 ... 23:
			ppdu_info->nss = 3;
			break;
		case 24 ... 31:
			ppdu_info->nss = 4;
			break;
		}

		if (ppdu_info->nss > 1)
			ppdu_info->mcs = ppdu_info->mcs % 8;

		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		break;
	}
	case HAL_PHYRX_L_SIG_B: {
		struct hal_rx_lsig_b_info *lsigb =
			(struct hal_rx_lsig_b_info *)tlv_data;

		ppdu_info->rate = FIELD_GET(HAL_RX_LSIG_B_INFO_INFO0_RATE,
					    __le32_to_cpu(lsigb->info0));
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		break;
	}
	case HAL_PHYRX_L_SIG_A: {
		struct hal_rx_lsig_a_info *lsiga =
			(struct hal_rx_lsig_a_info *)tlv_data;

		ppdu_info->rate = FIELD_GET(HAL_RX_LSIG_A_INFO_INFO0_RATE,
					    __le32_to_cpu(lsiga->info0));
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		break;
	}
	case HAL_PHYRX_VHT_SIG_A: {
		struct hal_rx_vht_sig_a_info *vht_sig =
			(struct hal_rx_vht_sig_a_info *)tlv_data;
		u32 nsts;
		u32 group_id;
		u8 gi_setting;

		info0 = __le32_to_cpu(vht_sig->info0);
		info1 = __le32_to_cpu(vht_sig->info1);

		ppdu_info->ldpc = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO1_SU_MU_CODING,
					    info0);
		ppdu_info->mcs = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO1_MCS,
					   info1);
		gi_setting = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO1_GI_SETTING,
				       info1);
		switch (gi_setting) {
		case HAL_RX_VHT_SIG_A_NORMAL_GI:
			ppdu_info->gi = HAL_RX_GI_0_8_US;
			break;
		case HAL_RX_VHT_SIG_A_SHORT_GI:
		case HAL_RX_VHT_SIG_A_SHORT_GI_AMBIGUITY:
			ppdu_info->gi = HAL_RX_GI_0_4_US;
			break;
		}

		ppdu_info->is_stbc = info0 & HAL_RX_VHT_SIG_A_INFO_INFO0_STBC;
		nsts = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO0_NSTS, info0);
		if (ppdu_info->is_stbc && nsts > 0)
			nsts = ((nsts + 1) >> 1) - 1;

		ppdu_info->nss = (nsts & VHT_SIG_SU_NSS_MASK) + 1;
		ppdu_info->bw = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO0_BW,
					  info0);
		ppdu_info->beamformed = info1 &
					HAL_RX_VHT_SIG_A_INFO_INFO1_BEAMFORMED;
		group_id = FIELD_GET(HAL_RX_VHT_SIG_A_INFO_INFO0_GROUP_ID,
				     info0);
		if (group_id == 0 || group_id == 63)
			ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		else
			ppdu_info->reception_type =
				HAL_RX_RECEPTION_TYPE_MU_MIMO;
		break;
	}
	case HAL_PHYRX_HE_SIG_A_SU: {
		struct hal_rx_he_sig_a_su_info *he_sig_a =
			(struct hal_rx_he_sig_a_su_info *)tlv_data;
		u32 nsts, cp_ltf, dcm;

		info0 = __le32_to_cpu(he_sig_a->info0);
		info1 = __le32_to_cpu(he_sig_a->info1);

		ppdu_info->mcs =
			FIELD_GET(HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_MCS,
				  info0);
		ppdu_info->bw =
			FIELD_GET(HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_BW,
				  info0);
		ppdu_info->ldpc = FIELD_GET(HAL_RX_HE_SIG_A_SU_INFO_INFO1_CODING, info0);
		ppdu_info->is_stbc = info1 &
				     HAL_RX_HE_SIG_A_SU_INFO_INFO1_STBC;
		ppdu_info->beamformed = info1 &
					HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXBF;
		dcm = info0 & HAL_RX_HE_SIG_A_SU_INFO_INFO0_DCM;
		cp_ltf = FIELD_GET(HAL_RX_HE_SIG_A_SU_INFO_INFO0_CP_LTF_SIZE,
				   info0);
		nsts = FIELD_GET(HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS, info0);

		switch (cp_ltf) {
		case 0:
		case 1:
			ppdu_info->gi = HAL_RX_GI_0_8_US;
			break;
		case 2:
			ppdu_info->gi = HAL_RX_GI_1_6_US;
			break;
		case 3:
			if (dcm && ppdu_info->is_stbc)
				ppdu_info->gi = HAL_RX_GI_0_8_US;
			else
				ppdu_info->gi = HAL_RX_GI_3_2_US;
			break;
		}

		ppdu_info->nss = nsts + 1;
		ppdu_info->dcm = dcm;
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		break;
	}
	case HAL_PHYRX_HE_SIG_A_MU_DL: {
		struct hal_rx_he_sig_a_mu_dl_info *he_sig_a_mu_dl =
			(struct hal_rx_he_sig_a_mu_dl_info *)tlv_data;

		u32 cp_ltf;

		info0 = __le32_to_cpu(he_sig_a_mu_dl->info0);
		info1 = __le32_to_cpu(he_sig_a_mu_dl->info1);

		ppdu_info->bw =
			FIELD_GET(HAL_RX_HE_SIG_A_MU_DL_INFO_INFO0_TRANSMIT_BW,
				  info0);
		cp_ltf = FIELD_GET(HAL_RX_HE_SIG_A_MU_DL_INFO_INFO0_CP_LTF_SIZE,
				   info0);

		switch (cp_ltf) {
		case 0:
		case 1:
			ppdu_info->gi = HAL_RX_GI_0_8_US;
			break;
		case 2:
			ppdu_info->gi = HAL_RX_GI_1_6_US;
			break;
		case 3:
			ppdu_info->gi = HAL_RX_GI_3_2_US;
			break;
		}

		ppdu_info->is_stbc = info1 &
				     HAL_RX_HE_SIG_A_MU_DL_INFO_INFO1_STBC;
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
		break;
	}
	case HAL_PHYRX_HE_SIG_B1_MU: {
		struct hal_rx_he_sig_b1_mu_info *he_sig_b1_mu =
			(struct hal_rx_he_sig_b1_mu_info *)tlv_data;
		u16 ru_tones;

		info0 = __le32_to_cpu(he_sig_b1_mu->info0);

		ru_tones = FIELD_GET(HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION,
				     info0);
		ppdu_info->ru_alloc = ath11k_he_ru_tones_to_nl80211_he_ru_alloc(ru_tones);
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
		break;
	}
	case HAL_PHYRX_HE_SIG_B2_MU: {
		struct hal_rx_he_sig_b2_mu_info *he_sig_b2_mu =
			(struct hal_rx_he_sig_b2_mu_info *)tlv_data;

		info0 = __le32_to_cpu(he_sig_b2_mu->info0);

		ppdu_info->mcs =
			FIELD_GET(HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_MCS,
				  info0);
		ppdu_info->nss =
			FIELD_GET(HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_NSTS,
				  info0) + 1;
		ppdu_info->ldpc = FIELD_GET(HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_CODING,
					    info0);
		break;
	}
	case HAL_PHYRX_HE_SIG_B2_OFDMA: {
		struct hal_rx_he_sig_b2_ofdma_info *he_sig_b2_ofdma =
			(struct hal_rx_he_sig_b2_ofdma_info *)tlv_data;

		info0 = __le32_to_cpu(he_sig_b2_ofdma->info0);

		ppdu_info->mcs =
			FIELD_GET(HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_MCS,
				  info0);
		ppdu_info->nss =
			FIELD_GET(HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_NSTS,
				  info0) + 1;
		ppdu_info->beamformed =
			info0 &
			HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF;
		ppdu_info->ldpc = FIELD_GET(HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_CODING,
					    info0);
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_OFDMA;
		break;
	}
	case HAL_PHYRX_RSSI_LEGACY: {
		struct hal_rx_phyrx_rssi_legacy_info *rssi =
			(struct hal_rx_phyrx_rssi_legacy_info *)tlv_data;

		/* TODO: Please note that the combined rssi will not be accurate
		 * in MU case. Rssi in MU needs to be retrieved from
		 * PHYRX_OTHER_RECEIVE_INFO TLV.
		 */
		ppdu_info->rssi_comb =
			FIELD_GET(HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB,
				  __le32_to_cpu(rssi->info0));
		break;
	}
	case HAL_RX_MPDU_START: {
		struct hal_rx_mpdu_info *mpdu_info =
			(struct hal_rx_mpdu_info *)tlv_data;
		u16 peer_id;

		peer_id = FIELD_GET(HAL_RX_MPDU_INFO_INFO0_PEERID,
				    __le32_to_cpu(mpdu_info->info0));
		if (peer_id)
			ppdu_info->peer_id = peer_id;
		break;
	}
	case HAL_RXPCU_PPDU_END_INFO: {
		struct hal_rx_ppdu_end_duration *ppdu_rx_duration =
			(struct hal_rx_ppdu_end_duration *)tlv_data;
		ppdu_info->rx_duration =
			FIELD_GET(HAL_RX_PPDU_END_DURATION,
				  __le32_to_cpu(ppdu_rx_duration->info0));
		break;
	}
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

enum hal_rx_mon_status
ath11k_hal_rx_parse_mon_status(struct ath11k_base *ab,
			       struct hal_rx_mon_ppdu_info *ppdu_info,
			       struct sk_buff *skb)
{
	struct hal_tlv_hdr *tlv;
	enum hal_rx_mon_status hal_status = HAL_RX_MON_STATUS_BUF_DONE;
	u16 tlv_tag;
	u16 tlv_len;
	u8 *ptr = skb->data;

	do {
		tlv = (struct hal_tlv_hdr *)ptr;
		tlv_tag = FIELD_GET(HAL_TLV_HDR_TAG, tlv->tl);
		tlv_len = FIELD_GET(HAL_TLV_HDR_LEN, tlv->tl);
		ptr += sizeof(*tlv);

		/* The actual length of PPDU_END is the combined length of many PHY
		 * TLVs that follow. Skip the TLV header and
		 * rx_rxpcu_classification_overview that follows the header to get to
		 * next TLV.
		 */
		if (tlv_tag == HAL_RX_PPDU_END)
			tlv_len = sizeof(struct hal_rx_rxpcu_classification_overview);

		hal_status = ath11k_hal_rx_parse_mon_status_tlv(ab, ppdu_info,
								tlv_tag, ptr);
		ptr += tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_ALIGN);

		if ((ptr - skb->data) >= DP_RX_BUFFER_SIZE)
			break;
	} while (hal_status == HAL_RX_MON_STATUS_PPDU_NOT_DONE);

	return hal_status;
}

void ath11k_hal_rx_reo_ent_buf_paddr_get(void *rx_desc, dma_addr_t *paddr,
					 u32 *sw_cookie, void **pp_buf_addr,
					 u32  *msdu_cnt)
{
	struct hal_reo_entrance_ring *reo_ent_ring =
		(struct hal_reo_entrance_ring *)rx_desc;
	struct ath11k_buffer_addr *buf_addr_info;
	struct rx_mpdu_desc *rx_mpdu_desc_info_details;

	rx_mpdu_desc_info_details =
			(struct rx_mpdu_desc *)&reo_ent_ring->rx_mpdu_info;

	*msdu_cnt = FIELD_GET(RX_MPDU_DESC_INFO0_MSDU_COUNT,
			      rx_mpdu_desc_info_details->info0);

	buf_addr_info = (struct ath11k_buffer_addr *)&reo_ent_ring->buf_addr_info;

	*paddr = (((u64)FIELD_GET(BUFFER_ADDR_INFO1_ADDR,
				  buf_addr_info->info1)) << 32) |
			FIELD_GET(BUFFER_ADDR_INFO0_ADDR,
				  buf_addr_info->info0);

	*sw_cookie = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
			       buf_addr_info->info1);

	*pp_buf_addr = (void *)buf_addr_info;
}
