// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "hw.h"
#include "hal_desc.h"
#include "../hal.h"
#include "hal.h"
#include "hal_tx.h"
#include "../debug.h"
#include "../hif.h"
#include "hal_qcn9274.h"
#include "hal_wcn7850.h"
#include "hal_qcc2072.h"

static const struct ath12k_hw_version_map ath12k_wifi7_hw_ver_map[] = {
	[ATH12K_HW_QCN9274_HW10] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_qcn9274,
		.hw_regs = &qcn9274_v1_regs,
	},
	[ATH12K_HW_QCN9274_HW20] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_qcn9274,
		.hw_regs = &qcn9274_v2_regs,
	},
	[ATH12K_HW_WCN7850_HW20] = {
		.hal_ops = &hal_wcn7850_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_wcn7850),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_wcn7850,
		.hal_params = &ath12k_hw_hal_params_wcn7850,
		.hw_regs = &wcn7850_regs,
	},
	[ATH12K_HW_IPQ5332_HW10] = {
		.hal_ops = &hal_qcn9274_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9274_compact),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_qcn9274,
		.hal_params = &ath12k_hw_hal_params_ipq5332,
		.hw_regs = &ipq5332_regs,
	},
	[ATH12K_HW_QCC2072_HW10] = {
		.hal_ops = &hal_qcc2072_ops,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcc2072),
		.tcl_to_wbm_rbm_map = ath12k_hal_tcl_to_wbm_rbm_map_wcn7850,
		.hal_params = &ath12k_hw_hal_params_wcn7850,
		.hw_regs = &qcc2072_regs,
	},
};

int ath12k_wifi7_hal_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	memset(hal, 0, sizeof(*hal));

	hal->ops = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_ops;
	hal->hal_desc_sz = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_desc_sz;
	hal->tcl_to_wbm_rbm_map = ath12k_wifi7_hw_ver_map[ab->hw_rev].tcl_to_wbm_rbm_map;
	hal->regs = ath12k_wifi7_hw_ver_map[ab->hw_rev].hw_regs;
	hal->hal_params = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_params;
	hal->hal_wbm_release_ring_tx_size = sizeof(struct hal_wbm_release_ring_tx);

	return 0;
}

static unsigned int ath12k_wifi7_hal_reo1_ring_id_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_ID(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned
int ath12k_wifi7_hal_reo1_ring_msi1_base_lsb_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_MSI1_BASE_LSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned
int ath12k_wifi7_hal_reo1_ring_msi1_base_msb_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_MSI1_BASE_MSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned int ath12k_wifi7_hal_reo1_ring_msi1_data_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_MSI1_DATA(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned int ath12k_wifi7_hal_reo1_ring_base_msb_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_BASE_MSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned
int ath12k_wifi7_hal_reo1_ring_producer_int_setup_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_PRODUCER_INT_SETUP(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned int ath12k_wifi7_hal_reo1_ring_hp_addr_lsb_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_HP_ADDR_LSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned int ath12k_wifi7_hal_reo1_ring_hp_addr_msb_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_HP_ADDR_MSB(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

static unsigned int ath12k_wifi7_hal_reo1_ring_misc_offset(struct ath12k_hal *hal)
{
	return HAL_REO1_RING_MISC(hal) - HAL_REO1_RING_BASE_LSB(hal);
}

void ath12k_wifi7_hal_ce_dst_setup(struct ath12k_base *ab,
				   struct hal_srng *srng, int ring_num)
{
	struct hal_srng_config *srng_config = &ab->hal.srng_config[HAL_CE_DST];
	u32 addr;
	u32 val;

	addr = HAL_CE_DST_RING_CTRL +
	       srng_config->reg_start[HAL_SRNG_REG_GRP_R0] +
	       ring_num * srng_config->reg_size[HAL_SRNG_REG_GRP_R0];

	val = ath12k_hif_read32(ab, addr);
	val &= ~HAL_CE_DST_R0_DEST_CTRL_MAX_LEN;
	val |= u32_encode_bits(srng->u.dst_ring.max_buffer_length,
			       HAL_CE_DST_R0_DEST_CTRL_MAX_LEN);
	ath12k_hif_write32(ab, addr, val);
}

void ath12k_wifi7_hal_srng_dst_hw_init(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 val;
	u64 hp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath12k_hif_write32(ab, reg_base +
				   ath12k_wifi7_hal_reo1_ring_msi1_base_lsb_offset(hal),
				   srng->msi_addr);

		val = u32_encode_bits(((u64)srng->msi_addr >> HAL_ADDR_MSB_REG_SHIFT),
				      HAL_REO1_RING_MSI1_BASE_MSB_ADDR) |
				      HAL_REO1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath12k_hif_write32(ab, reg_base +
				   ath12k_wifi7_hal_reo1_ring_msi1_base_msb_offset(hal),
				   val);

		ath12k_hif_write32(ab,
				   reg_base +
				   ath12k_wifi7_hal_reo1_ring_msi1_data_offset(hal),
				   srng->msi_data);
	}

	ath12k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = u32_encode_bits(((u64)srng->ring_base_paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_REO1_RING_BASE_MSB_RING_BASE_ADDR_MSB) |
	      u32_encode_bits((srng->entry_size * srng->num_entries),
			      HAL_REO1_RING_BASE_MSB_RING_SIZE);
	ath12k_hif_write32(ab, reg_base + ath12k_wifi7_hal_reo1_ring_base_msb_offset(hal),
			   val);

	val = u32_encode_bits(srng->ring_id, HAL_REO1_RING_ID_RING_ID) |
	      u32_encode_bits(srng->entry_size, HAL_REO1_RING_ID_ENTRY_SIZE);
	ath12k_hif_write32(ab, reg_base + ath12k_wifi7_hal_reo1_ring_id_offset(hal), val);

	/* interrupt setup */
	val = u32_encode_bits((srng->intr_timer_thres_us >> 3),
			      HAL_REO1_RING_PRDR_INT_SETUP_INTR_TMR_THOLD);

	val |= u32_encode_bits((srng->intr_batch_cntr_thres_entries * srng->entry_size),
				HAL_REO1_RING_PRDR_INT_SETUP_BATCH_COUNTER_THOLD);

	ath12k_hif_write32(ab,
			   reg_base +
			   ath12k_wifi7_hal_reo1_ring_producer_int_setup_offset(hal),
			   val);

	hp_addr = hal->rdp.paddr +
		  ((unsigned long)srng->u.dst_ring.hp_addr -
		   (unsigned long)hal->rdp.vaddr);
	ath12k_hif_write32(ab, reg_base +
			   ath12k_wifi7_hal_reo1_ring_hp_addr_lsb_offset(hal),
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath12k_hif_write32(ab, reg_base +
			   ath12k_wifi7_hal_reo1_ring_hp_addr_msb_offset(hal),
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath12k_hif_write32(ab, reg_base, 0);
	ath12k_hif_write32(ab, reg_base + HAL_REO1_RING_TP_OFFSET, 0);
	*srng->u.dst_ring.hp_addr = 0;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];
	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP)
		val |= HAL_REO1_RING_MISC_DATA_TLV_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_RING_PTR_SWAP)
		val |= HAL_REO1_RING_MISC_HOST_FW_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_MSI_SWAP)
		val |= HAL_REO1_RING_MISC_MSI_SWAP;
	val |= HAL_REO1_RING_MISC_SRNG_ENABLE;

	ath12k_hif_write32(ab, reg_base + ath12k_wifi7_hal_reo1_ring_misc_offset(hal),
			   val);
}

void ath12k_wifi7_hal_srng_src_hw_init(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	struct ath12k_hal *hal = &ab->hal;
	u32 val;
	u64 tp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath12k_hif_write32(ab, reg_base +
				   HAL_TCL1_RING_MSI1_BASE_LSB_OFFSET(hal),
				   srng->msi_addr);

		val = u32_encode_bits(((u64)srng->msi_addr >> HAL_ADDR_MSB_REG_SHIFT),
				      HAL_TCL1_RING_MSI1_BASE_MSB_ADDR) |
				      HAL_TCL1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath12k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_BASE_MSB_OFFSET(hal),
				   val);

		ath12k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_DATA_OFFSET(hal),
				   srng->msi_data);
	}

	ath12k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = u32_encode_bits(((u64)srng->ring_base_paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB) |
	      u32_encode_bits((srng->entry_size * srng->num_entries),
			      HAL_TCL1_RING_BASE_MSB_RING_SIZE);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_BASE_MSB_OFFSET(hal), val);

	val = u32_encode_bits(srng->entry_size, HAL_REO1_RING_ID_ENTRY_SIZE);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_ID_OFFSET(hal), val);

	val = u32_encode_bits(srng->intr_timer_thres_us,
			      HAL_TCL1_RING_CONSR_INT_SETUP_IX0_INTR_TMR_THOLD);

	val |= u32_encode_bits((srng->intr_batch_cntr_thres_entries * srng->entry_size),
			       HAL_TCL1_RING_CONSR_INT_SETUP_IX0_BATCH_COUNTER_THOLD);

	ath12k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX0_OFFSET(hal),
			   val);

	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN) {
		val |= u32_encode_bits(srng->u.src_ring.low_threshold,
				       HAL_TCL1_RING_CONSR_INT_SETUP_IX1_LOW_THOLD);
	}
	ath12k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX1_OFFSET(hal),
			   val);

	if (srng->ring_id != HAL_SRNG_RING_ID_WBM_IDLE_LINK) {
		tp_addr = hal->rdp.paddr +
			  ((unsigned long)srng->u.src_ring.tp_addr -
			   (unsigned long)hal->rdp.vaddr);
		ath12k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_LSB_OFFSET(hal),
				   tp_addr & HAL_ADDR_LSB_REG_MASK);
		ath12k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_MSB_OFFSET(hal),
				   tp_addr >> HAL_ADDR_MSB_REG_SHIFT);
	}

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath12k_hif_write32(ab, reg_base, 0);
	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_TP_OFFSET, 0);
	*srng->u.src_ring.tp_addr = 0;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];
	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP)
		val |= HAL_TCL1_RING_MISC_DATA_TLV_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_RING_PTR_SWAP)
		val |= HAL_TCL1_RING_MISC_HOST_FW_SWAP;
	if (srng->flags & HAL_SRNG_FLAGS_MSI_SWAP)
		val |= HAL_TCL1_RING_MISC_MSI_SWAP;

	/* Loop count is not used for SRC rings */
	val |= HAL_TCL1_RING_MISC_MSI_LOOPCNT_DISABLE;

	val |= HAL_TCL1_RING_MISC_SRNG_ENABLE;

	if (srng->ring_id == HAL_SRNG_RING_ID_WBM_IDLE_LINK)
		val |= HAL_TCL1_RING_MISC_MSI_RING_ID_DISABLE;

	ath12k_hif_write32(ab, reg_base + HAL_TCL1_RING_MISC_OFFSET(hal), val);
}

void ath12k_wifi7_hal_set_umac_srng_ptr_addr(struct ath12k_base *ab,
					     struct hal_srng *srng)
{
	u32 reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		if (!ab->hw_params->supports_shadow_regs) {
			srng->u.src_ring.hp_addr =
				(u32 *)((unsigned long)ab->mem + reg_base);
		} else {
			ath12k_dbg(ab, ATH12K_DBG_HAL,
				   "hal reg_base 0x%x shadow 0x%lx\n",
				   reg_base,
				   (unsigned long)srng->u.src_ring.hp_addr -
				   (unsigned long)ab->mem);
		}
	} else  {
		if (!ab->hw_params->supports_shadow_regs) {
			srng->u.dst_ring.tp_addr =
				(u32 *)((unsigned long)ab->mem + reg_base +
						(HAL_REO1_RING_TP - HAL_REO1_RING_HP));
		} else {
			ath12k_dbg(ab, ATH12K_DBG_HAL,
				   "target_reg 0x%x shadow 0x%lx\n",
				   reg_base + HAL_REO1_RING_TP - HAL_REO1_RING_HP,
				   (unsigned long)srng->u.dst_ring.tp_addr -
				   (unsigned long)ab->mem);
		}
	}
}

int ath12k_wifi7_hal_srng_get_ring_id(struct ath12k_hal *hal,
				      enum hal_ring_type type,
				      int ring_num, int mac_id)
{
	struct hal_srng_config *srng_config = &hal->srng_config[type];
	int ring_id;

	if (ring_num >= srng_config->max_rings) {
		ath12k_warn(hal, "invalid ring number :%d\n", ring_num);
		return -EINVAL;
	}

	ring_id = srng_config->start_ring_id + ring_num;
	if (srng_config->mac_type == ATH12K_HAL_SRNG_PMAC)
		ring_id += mac_id * HAL_SRNG_RINGS_PER_PMAC;

	if (WARN_ON(ring_id >= HAL_SRNG_RING_ID_MAX))
		return -EINVAL;

	return ring_id;
}

static
void ath12k_wifi7_hal_srng_update_hp_tp_addr(struct ath12k_base *ab,
					     int shadow_cfg_idx,
					     enum hal_ring_type ring_type,
					     int ring_num)
{
	struct hal_srng *srng;
	struct ath12k_hal *hal = &ab->hal;
	int ring_id;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

	ring_id = ath12k_wifi7_hal_srng_get_ring_id(hal, ring_type, ring_num,
						    0);
	if (ring_id < 0)
		return;

	srng = &hal->srng_list[ring_id];

	if (srng_config->ring_dir == HAL_SRNG_DIR_DST)
		srng->u.dst_ring.tp_addr = (u32 *)(HAL_SHADOW_REG(shadow_cfg_idx) +
						   (unsigned long)ab->mem);
	else
		srng->u.src_ring.hp_addr = (u32 *)(HAL_SHADOW_REG(shadow_cfg_idx) +
						   (unsigned long)ab->mem);
}

u32 ath12k_wifi7_hal_ce_get_desc_size(enum hal_ce_desc type)
{
	switch (type) {
	case HAL_CE_DESC_SRC:
		return sizeof(struct hal_ce_srng_src_desc);
	case HAL_CE_DESC_DST:
		return sizeof(struct hal_ce_srng_dest_desc);
	case HAL_CE_DESC_DST_STATUS:
		return sizeof(struct hal_ce_srng_dst_status_desc);
	}

	return 0;
}

int ath12k_wifi7_hal_srng_update_shadow_config(struct ath12k_base *ab,
					       enum hal_ring_type ring_type,
					       int ring_num)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];
	int shadow_cfg_idx = hal->num_shadow_reg_configured;
	u32 target_reg;

	if (shadow_cfg_idx >= HAL_SHADOW_NUM_REGS_MAX)
		return -EINVAL;

	hal->num_shadow_reg_configured++;

	target_reg = srng_config->reg_start[HAL_HP_OFFSET_IN_REG_START];
	target_reg += srng_config->reg_size[HAL_HP_OFFSET_IN_REG_START] *
		ring_num;

	/* For destination ring, shadow the TP */
	if (srng_config->ring_dir == HAL_SRNG_DIR_DST)
		target_reg += HAL_OFFSET_FROM_HP_TO_TP;

	hal->shadow_reg_addr[shadow_cfg_idx] = target_reg;

	/* update hp/tp addr to hal structure*/
	ath12k_wifi7_hal_srng_update_hp_tp_addr(ab, shadow_cfg_idx, ring_type,
						ring_num);

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "target_reg %x, shadow reg 0x%x shadow_idx 0x%x, ring_type %d, ring num %d",
		  target_reg,
		  HAL_SHADOW_REG(shadow_cfg_idx),
		  shadow_cfg_idx,
		  ring_type, ring_num);

	return 0;
}

void ath12k_wifi7_hal_ce_src_set_desc(struct hal_ce_srng_src_desc *desc,
				      dma_addr_t paddr,
				      u32 len, u32 id, u8 byte_swap_data)
{
	desc->buffer_addr_low = cpu_to_le32(paddr & HAL_ADDR_LSB_REG_MASK);
	desc->buffer_addr_info =
		le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
				 HAL_CE_SRC_DESC_ADDR_INFO_ADDR_HI) |
		le32_encode_bits(byte_swap_data,
				 HAL_CE_SRC_DESC_ADDR_INFO_BYTE_SWAP) |
		le32_encode_bits(0, HAL_CE_SRC_DESC_ADDR_INFO_GATHER) |
		le32_encode_bits(len, HAL_CE_SRC_DESC_ADDR_INFO_LEN);
	desc->meta_info = le32_encode_bits(id, HAL_CE_SRC_DESC_META_INFO_DATA);
}

void ath12k_wifi7_hal_ce_dst_set_desc(struct hal_ce_srng_dest_desc *desc,
				      dma_addr_t paddr)
{
	desc->buffer_addr_low = cpu_to_le32(paddr & HAL_ADDR_LSB_REG_MASK);
	desc->buffer_addr_info =
		le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
				 HAL_CE_DEST_DESC_ADDR_INFO_ADDR_HI);
}

void ath12k_wifi7_hal_set_link_desc_addr(struct hal_wbm_link_desc *desc,
					 u32 cookie, dma_addr_t paddr,
					 enum hal_rx_buf_return_buf_manager rbm)
{
	desc->buf_addr_info.info0 = le32_encode_bits((paddr & HAL_ADDR_LSB_REG_MASK),
						     BUFFER_ADDR_INFO0_ADDR);
	desc->buf_addr_info.info1 =
			le32_encode_bits(((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT),
					 BUFFER_ADDR_INFO1_ADDR) |
			le32_encode_bits(rbm, BUFFER_ADDR_INFO1_RET_BUF_MGR) |
			le32_encode_bits(cookie, BUFFER_ADDR_INFO1_SW_COOKIE);
}

u32 ath12k_wifi7_hal_ce_dst_status_get_length(struct hal_ce_srng_dst_status_desc *desc)
{
	u32 len;

	len = le32_get_bits(READ_ONCE(desc->flags), HAL_CE_DST_STATUS_DESC_FLAGS_LEN);
	desc->flags &= ~cpu_to_le32(HAL_CE_DST_STATUS_DESC_FLAGS_LEN);

	return len;
}

void
ath12k_wifi7_hal_setup_link_idle_list(struct ath12k_base *ab,
				      struct hal_wbm_idle_scatter_list *sbuf,
				      u32 nsbufs, u32 tot_link_desc,
				      u32 end_offset)
{
	struct ath12k_hal *hal = &ab->hal;
	struct ath12k_buffer_addr *link_addr;
	int i;
	u32 reg_scatter_buf_sz = HAL_WBM_IDLE_SCATTER_BUF_SIZE / 64;
	u32 val;

	link_addr = (void *)sbuf[0].vaddr + HAL_WBM_IDLE_SCATTER_BUF_SIZE;

	for (i = 1; i < nsbufs; i++) {
		link_addr->info0 = cpu_to_le32(sbuf[i].paddr & HAL_ADDR_LSB_REG_MASK);

		link_addr->info1 =
			le32_encode_bits((u64)sbuf[i].paddr >> HAL_ADDR_MSB_REG_SHIFT,
					 HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
			le32_encode_bits(BASE_ADDR_MATCH_TAG_VAL,
					 HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG);

		link_addr = (void *)sbuf[i].vaddr +
			     HAL_WBM_IDLE_SCATTER_BUF_SIZE;
	}

	val = u32_encode_bits(reg_scatter_buf_sz, HAL_WBM_SCATTER_BUFFER_SIZE) |
	      u32_encode_bits(0x1, HAL_WBM_LINK_DESC_IDLE_LIST_MODE);

	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_R0_IDLE_LIST_CONTROL_ADDR(hal),
			   val);

	val = u32_encode_bits(reg_scatter_buf_sz * nsbufs,
			      HAL_WBM_SCATTER_RING_SIZE_OF_IDLE_LINK_DESC_LIST);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_R0_IDLE_LIST_SIZE_ADDR(hal),
			   val);

	val = u32_encode_bits(sbuf[0].paddr & HAL_ADDR_LSB_REG_MASK,
			      BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_LSB(hal),
			   val);

	val = u32_encode_bits(BASE_ADDR_MATCH_TAG_VAL,
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG) |
	      u32_encode_bits((u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT,
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_MSB(hal),
			   val);

	/* Setup head and tail pointers for the idle list */
	val = u32_encode_bits(sbuf[nsbufs - 1].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0(hal),
			   val);

	val = u32_encode_bits(((u64)sbuf[nsbufs - 1].paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
	       u32_encode_bits((end_offset >> 2),
			       HAL_WBM_SCATTERED_DESC_HEAD_P_OFFSET_IX1);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX1(hal),
			   val);

	val = u32_encode_bits(sbuf[0].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0(hal),
			   val);

	val = u32_encode_bits(sbuf[0].paddr, BUFFER_ADDR_INFO0_ADDR);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX0(hal),
			   val);

	val = u32_encode_bits(((u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT),
			      HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32) |
	      u32_encode_bits(0, HAL_WBM_SCATTERED_DESC_TAIL_P_OFFSET_IX1);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX1(hal),
			   val);

	val = 2 * tot_link_desc;
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HP_ADDR(hal),
			   val);

	/* Enable the SRNG */
	val = u32_encode_bits(1, HAL_WBM_IDLE_LINK_RING_MISC_SRNG_ENABLE) |
	      u32_encode_bits(1, HAL_WBM_IDLE_LINK_RING_MISC_RIND_ID_DISABLE);
	ath12k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_IDLE_LINK_RING_MISC_ADDR(hal),
			   val);
}

void ath12k_wifi7_hal_tx_configure_bank_register(struct ath12k_base *ab,
						 u32 bank_config,
						 u8 bank_id)
{
	ath12k_hif_write32(ab, HAL_TCL_SW_CONFIG_BANK_ADDR + 4 * bank_id,
			   bank_config);
}

void ath12k_wifi7_hal_reoq_lut_addr_read_enable(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	u32 val = ath12k_hif_read32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
				    HAL_REO1_QDESC_ADDR(hal));

	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_QDESC_ADDR(hal),
			   val | HAL_REO_QDESC_ADDR_READ_LUT_ENABLE);
}

void ath12k_wifi7_hal_reoq_lut_set_max_peerid(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_QDESC_MAX_PEERID(hal),
			   HAL_REO_QDESC_MAX_PEERID);
}

void ath12k_wifi7_hal_write_reoq_lut_addr(struct ath12k_base *ab,
					  dma_addr_t paddr)
{
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO1_QDESC_LUT_BASE0(&ab->hal), paddr);
}

void ath12k_wifi7_hal_write_ml_reoq_lut_addr(struct ath12k_base *ab,
					     dma_addr_t paddr)
{
	ath12k_hif_write32(ab, HAL_SEQ_WCSS_UMAC_REO_REG +
			   HAL_REO1_QDESC_LUT_BASE1(&ab->hal), paddr);
}

void ath12k_wifi7_hal_cc_config(struct ath12k_base *ab)
{
	u32 cmem_base = ab->qmi.dev_mem[ATH12K_QMI_DEVMEM_CMEM_INDEX].start;
	u32 reo_base = HAL_SEQ_WCSS_UMAC_REO_REG;
	u32 wbm_base = HAL_SEQ_WCSS_UMAC_WBM_REG;
	u32 val = 0;
	struct ath12k_hal *hal = &ab->hal;

	if (ath12k_ftm_mode)
		return;

	ath12k_hif_write32(ab, reo_base + HAL_REO1_SW_COOKIE_CFG0(hal), cmem_base);

	val |= u32_encode_bits(ATH12K_CMEM_ADDR_MSB,
			       HAL_REO1_SW_COOKIE_CFG_CMEM_BASE_ADDR_MSB) |
		u32_encode_bits(ATH12K_CC_PPT_MSB,
				HAL_REO1_SW_COOKIE_CFG_COOKIE_PPT_MSB) |
		u32_encode_bits(ATH12K_CC_SPT_MSB,
				HAL_REO1_SW_COOKIE_CFG_COOKIE_SPT_MSB) |
		u32_encode_bits(1, HAL_REO1_SW_COOKIE_CFG_ALIGN) |
		u32_encode_bits(1, HAL_REO1_SW_COOKIE_CFG_ENABLE) |
		u32_encode_bits(1, HAL_REO1_SW_COOKIE_CFG_GLOBAL_ENABLE);

	ath12k_hif_write32(ab, reo_base + HAL_REO1_SW_COOKIE_CFG1(hal), val);

	/* Enable HW CC for WBM */
	ath12k_hif_write32(ab, wbm_base + HAL_WBM_SW_COOKIE_CFG0, cmem_base);

	val = u32_encode_bits(ATH12K_CMEM_ADDR_MSB,
			      HAL_WBM_SW_COOKIE_CFG_CMEM_BASE_ADDR_MSB) |
		u32_encode_bits(ATH12K_CC_PPT_MSB,
				HAL_WBM_SW_COOKIE_CFG_COOKIE_PPT_MSB) |
		u32_encode_bits(ATH12K_CC_SPT_MSB,
				HAL_WBM_SW_COOKIE_CFG_COOKIE_SPT_MSB) |
		u32_encode_bits(1, HAL_WBM_SW_COOKIE_CFG_ALIGN);

	ath12k_hif_write32(ab, wbm_base + HAL_WBM_SW_COOKIE_CFG1, val);

	/* Enable conversion complete indication */
	val = ath12k_hif_read32(ab, wbm_base + HAL_WBM_SW_COOKIE_CFG2);
	val |= u32_encode_bits(1, HAL_WBM_SW_COOKIE_CFG_RELEASE_PATH_EN) |
		u32_encode_bits(1, HAL_WBM_SW_COOKIE_CFG_ERR_PATH_EN) |
		u32_encode_bits(1, HAL_WBM_SW_COOKIE_CFG_CONV_IND_EN);

	ath12k_hif_write32(ab, wbm_base + HAL_WBM_SW_COOKIE_CFG2, val);

	/* Enable Cookie conversion for WBM2SW Rings */
	val = ath12k_hif_read32(ab, wbm_base + HAL_WBM_SW_COOKIE_CONVERT_CFG);
	val |= u32_encode_bits(1, HAL_WBM_SW_COOKIE_CONV_CFG_GLOBAL_EN) |
	       hal->hal_params->wbm2sw_cc_enable;

	ath12k_hif_write32(ab, wbm_base + HAL_WBM_SW_COOKIE_CONVERT_CFG, val);
}

enum hal_rx_buf_return_buf_manager
ath12k_wifi7_hal_get_idle_link_rbm(struct ath12k_hal *hal, u8 device_id)
{
	switch (device_id) {
	case 0:
		return HAL_RX_BUF_RBM_WBM_DEV0_IDLE_DESC_LIST;
	case 1:
		return HAL_RX_BUF_RBM_WBM_DEV1_IDLE_DESC_LIST;
	case 2:
		return HAL_RX_BUF_RBM_WBM_DEV2_IDLE_DESC_LIST;
	default:
		ath12k_warn(hal,
			    "invalid %d device id, so choose default rbm\n",
			    device_id);
		WARN_ON(1);
		return HAL_RX_BUF_RBM_WBM_DEV0_IDLE_DESC_LIST;
	}
}
