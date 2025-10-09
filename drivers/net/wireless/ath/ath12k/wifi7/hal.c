// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "hw.h"
#include "hal_desc.h"
#include "../hal.h"
#include "hal.h"
#include "../debug.h"
#include "../hif.h"
#include "hal_qcn9274.h"
#include "hal_wcn7850.h"

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
};

int ath12k_wifi7_hal_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	memset(hal, 0, sizeof(*hal));

	hal->hal_ops = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_ops;
	hal->hal_desc_sz = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_desc_sz;
	hal->tcl_to_wbm_rbm_map = ath12k_wifi7_hw_ver_map[ab->hw_rev].tcl_to_wbm_rbm_map;
	hal->regs = ath12k_wifi7_hw_ver_map[ab->hw_rev].hw_regs;
	hal->hal_params = ath12k_wifi7_hw_ver_map[ab->hw_rev].hal_params;

	return 0;
}
EXPORT_SYMBOL(ath12k_wifi7_hal_init);

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
