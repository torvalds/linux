// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/dma-mapping.h>
#include "hal_tx.h"
#include "debug.h"
#include "hal_desc.h"
#include "hif.h"

static const struct hal_srng_config hw_srng_config_template[] = {
	/* TODO: max_rings can populated by querying HW capabilities */
	{ /* REO_DST */
		.start_ring_id = HAL_SRNG_RING_ID_REO2SW1,
		.max_rings = 4,
		.entry_size = sizeof(struct hal_reo_dest_ring) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_REO2SW1_RING_BASE_MSB_RING_SIZE,
	},
	{ /* REO_EXCEPTION */
		/* Designating REO2TCL ring as exception ring. This ring is
		 * similar to other REO2SW rings though it is named as REO2TCL.
		 * Any of theREO2SW rings can be used as exception ring.
		 */
		.start_ring_id = HAL_SRNG_RING_ID_REO2TCL,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_reo_dest_ring) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_REO2TCL_RING_BASE_MSB_RING_SIZE,
	},
	{ /* REO_REINJECT */
		.start_ring_id = HAL_SRNG_RING_ID_SW2REO,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_reo_entrance_ring) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_REO_SW2REO_RING_BASE_MSB_RING_SIZE,
	},
	{ /* REO_CMD */
		.start_ring_id = HAL_SRNG_RING_ID_REO_CMD,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			sizeof(struct hal_reo_get_queue_stats)) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_REO_CMD_RING_BASE_MSB_RING_SIZE,
	},
	{ /* REO_STATUS */
		.start_ring_id = HAL_SRNG_RING_ID_REO_STATUS,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			sizeof(struct hal_reo_get_queue_stats_status)) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_REO_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	{ /* TCL_DATA */
		.start_ring_id = HAL_SRNG_RING_ID_SW2TCL1,
		.max_rings = 3,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			     sizeof(struct hal_tcl_data_cmd)) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2TCL1_RING_BASE_MSB_RING_SIZE,
	},
	{ /* TCL_CMD */
		.start_ring_id = HAL_SRNG_RING_ID_SW2TCL_CMD,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			     sizeof(struct hal_tcl_gse_cmd)) >> 2,
		.lmac_ring =  false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2TCL1_CMD_RING_BASE_MSB_RING_SIZE,
	},
	{ /* TCL_STATUS */
		.start_ring_id = HAL_SRNG_RING_ID_TCL_STATUS,
		.max_rings = 1,
		.entry_size = (sizeof(struct hal_tlv_hdr) +
			     sizeof(struct hal_tcl_status_ring)) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_TCL_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	{ /* CE_SRC */
		.start_ring_id = HAL_SRNG_RING_ID_CE0_SRC,
		.max_rings = 12,
		.entry_size = sizeof(struct hal_ce_srng_src_desc) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_CE_SRC_RING_BASE_MSB_RING_SIZE,
	},
	{ /* CE_DST */
		.start_ring_id = HAL_SRNG_RING_ID_CE0_DST,
		.max_rings = 12,
		.entry_size = sizeof(struct hal_ce_srng_dest_desc) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_CE_DST_RING_BASE_MSB_RING_SIZE,
	},
	{ /* CE_DST_STATUS */
		.start_ring_id = HAL_SRNG_RING_ID_CE0_DST_STATUS,
		.max_rings = 12,
		.entry_size = sizeof(struct hal_ce_srng_dst_status_desc) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_CE_DST_STATUS_RING_BASE_MSB_RING_SIZE,
	},
	{ /* WBM_IDLE_LINK */
		.start_ring_id = HAL_SRNG_RING_ID_WBM_IDLE_LINK,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_link_desc) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_WBM_IDLE_LINK_RING_BASE_MSB_RING_SIZE,
	},
	{ /* SW2WBM_RELEASE */
		.start_ring_id = HAL_SRNG_RING_ID_WBM_SW_RELEASE,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_release_ring) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_SW2WBM_RELEASE_RING_BASE_MSB_RING_SIZE,
	},
	{ /* WBM2SW_RELEASE */
		.start_ring_id = HAL_SRNG_RING_ID_WBM2SW0_RELEASE,
		.max_rings = 5,
		.entry_size = sizeof(struct hal_wbm_release_ring) >> 2,
		.lmac_ring = false,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_WBM2SW_RELEASE_RING_BASE_MSB_RING_SIZE,
	},
	{ /* RXDMA_BUF */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXDMA0_BUF,
		.max_rings = 2,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA_DST */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_RXDMA2SW0,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_reo_entrance_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA_MONITOR_BUF */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXDMA2_BUF,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA_MONITOR_STATUS */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_STATBUF,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA_MONITOR_DST */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_RXDMA2SW1,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_reo_entrance_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_DST,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA_MONITOR_DESC */
		.start_ring_id = HAL_SRNG_RING_ID_WMAC1_SW2RXDMA1_DESC,
		.max_rings = 1,
		.entry_size = sizeof(struct hal_wbm_buffer_ring) >> 2,
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
	{ /* RXDMA DIR BUF */
		.start_ring_id = HAL_SRNG_RING_ID_RXDMA_DIR_BUF,
		.max_rings = 1,
		.entry_size = 8 >> 2, /* TODO: Define the struct */
		.lmac_ring = true,
		.ring_dir = HAL_SRNG_DIR_SRC,
		.max_size = HAL_RXDMA_RING_MAX_SIZE,
	},
};

static int ath11k_hal_alloc_cont_rdp(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	size_t size;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	hal->rdp.vaddr = dma_alloc_coherent(ab->dev, size, &hal->rdp.paddr,
					    GFP_KERNEL);
	if (!hal->rdp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath11k_hal_free_cont_rdp(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	size_t size;

	if (!hal->rdp.vaddr)
		return;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	dma_free_coherent(ab->dev, size,
			  hal->rdp.vaddr, hal->rdp.paddr);
	hal->rdp.vaddr = NULL;
}

static int ath11k_hal_alloc_cont_wrp(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	size_t size;

	size = sizeof(u32) * HAL_SRNG_NUM_LMAC_RINGS;
	hal->wrp.vaddr = dma_alloc_coherent(ab->dev, size, &hal->wrp.paddr,
					    GFP_KERNEL);
	if (!hal->wrp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath11k_hal_free_cont_wrp(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	size_t size;

	if (!hal->wrp.vaddr)
		return;

	size = sizeof(u32) * HAL_SRNG_NUM_LMAC_RINGS;
	dma_free_coherent(ab->dev, size,
			  hal->wrp.vaddr, hal->wrp.paddr);
	hal->wrp.vaddr = NULL;
}

static void ath11k_hal_ce_dst_setup(struct ath11k_base *ab,
				    struct hal_srng *srng, int ring_num)
{
	struct hal_srng_config *srng_config = &ab->hal.srng_config[HAL_CE_DST];
	u32 addr;
	u32 val;

	addr = HAL_CE_DST_RING_CTRL +
	       srng_config->reg_start[HAL_SRNG_REG_GRP_R0] +
	       ring_num * srng_config->reg_size[HAL_SRNG_REG_GRP_R0];

	val = ath11k_hif_read32(ab, addr);
	val &= ~HAL_CE_DST_R0_DEST_CTRL_MAX_LEN;
	val |= FIELD_PREP(HAL_CE_DST_R0_DEST_CTRL_MAX_LEN,
			  srng->u.dst_ring.max_buffer_length);
	ath11k_hif_write32(ab, addr, val);
}

static void ath11k_hal_srng_dst_hw_init(struct ath11k_base *ab,
					struct hal_srng *srng)
{
	struct ath11k_hal *hal = &ab->hal;
	u32 val;
	u64 hp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath11k_hif_write32(ab, reg_base +
				   HAL_REO1_RING_MSI1_BASE_LSB_OFFSET(ab),
				   srng->msi_addr);

		val = FIELD_PREP(HAL_REO1_RING_MSI1_BASE_MSB_ADDR,
				 ((u64)srng->msi_addr >>
				  HAL_ADDR_MSB_REG_SHIFT)) |
		      HAL_REO1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath11k_hif_write32(ab, reg_base +
				       HAL_REO1_RING_MSI1_BASE_MSB_OFFSET(ab), val);

		ath11k_hif_write32(ab,
				   reg_base + HAL_REO1_RING_MSI1_DATA_OFFSET(ab),
				   srng->msi_data);
	}

	ath11k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = FIELD_PREP(HAL_REO1_RING_BASE_MSB_RING_BASE_ADDR_MSB,
			 ((u64)srng->ring_base_paddr >>
			  HAL_ADDR_MSB_REG_SHIFT)) |
	      FIELD_PREP(HAL_REO1_RING_BASE_MSB_RING_SIZE,
			 (srng->entry_size * srng->num_entries));
	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_BASE_MSB_OFFSET(ab), val);

	val = FIELD_PREP(HAL_REO1_RING_ID_RING_ID, srng->ring_id) |
	      FIELD_PREP(HAL_REO1_RING_ID_ENTRY_SIZE, srng->entry_size);
	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_ID_OFFSET(ab), val);

	/* interrupt setup */
	val = FIELD_PREP(HAL_REO1_RING_PRDR_INT_SETUP_INTR_TMR_THOLD,
			 (srng->intr_timer_thres_us >> 3));

	val |= FIELD_PREP(HAL_REO1_RING_PRDR_INT_SETUP_BATCH_COUNTER_THOLD,
			  (srng->intr_batch_cntr_thres_entries *
			   srng->entry_size));

	ath11k_hif_write32(ab,
			   reg_base + HAL_REO1_RING_PRODUCER_INT_SETUP_OFFSET(ab),
			   val);

	hp_addr = hal->rdp.paddr +
		  ((unsigned long)srng->u.dst_ring.hp_addr -
		   (unsigned long)hal->rdp.vaddr);
	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_HP_ADDR_LSB_OFFSET(ab),
			   hp_addr & HAL_ADDR_LSB_REG_MASK);
	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_HP_ADDR_MSB_OFFSET(ab),
			   hp_addr >> HAL_ADDR_MSB_REG_SHIFT);

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath11k_hif_write32(ab, reg_base, 0);
	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_TP_OFFSET(ab), 0);
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

	ath11k_hif_write32(ab, reg_base + HAL_REO1_RING_MISC_OFFSET(ab), val);
}

static void ath11k_hal_srng_src_hw_init(struct ath11k_base *ab,
					struct hal_srng *srng)
{
	struct ath11k_hal *hal = &ab->hal;
	u32 val;
	u64 tp_addr;
	u32 reg_base;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R0];

	if (srng->flags & HAL_SRNG_FLAGS_MSI_INTR) {
		ath11k_hif_write32(ab, reg_base +
				   HAL_TCL1_RING_MSI1_BASE_LSB_OFFSET(ab),
				   srng->msi_addr);

		val = FIELD_PREP(HAL_TCL1_RING_MSI1_BASE_MSB_ADDR,
				 ((u64)srng->msi_addr >>
				  HAL_ADDR_MSB_REG_SHIFT)) |
		      HAL_TCL1_RING_MSI1_BASE_MSB_MSI1_ENABLE;
		ath11k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_BASE_MSB_OFFSET(ab),
				   val);

		ath11k_hif_write32(ab, reg_base +
				       HAL_TCL1_RING_MSI1_DATA_OFFSET(ab),
				   srng->msi_data);
	}

	ath11k_hif_write32(ab, reg_base, srng->ring_base_paddr);

	val = FIELD_PREP(HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB,
			 ((u64)srng->ring_base_paddr >>
			  HAL_ADDR_MSB_REG_SHIFT)) |
	      FIELD_PREP(HAL_TCL1_RING_BASE_MSB_RING_SIZE,
			 (srng->entry_size * srng->num_entries));
	ath11k_hif_write32(ab, reg_base + HAL_TCL1_RING_BASE_MSB_OFFSET(ab), val);

	val = FIELD_PREP(HAL_REO1_RING_ID_ENTRY_SIZE, srng->entry_size);
	ath11k_hif_write32(ab, reg_base + HAL_TCL1_RING_ID_OFFSET(ab), val);

	if (srng->ring_id == HAL_SRNG_RING_ID_WBM_IDLE_LINK) {
		ath11k_hif_write32(ab, reg_base, (u32)srng->ring_base_paddr);
		val = FIELD_PREP(HAL_TCL1_RING_BASE_MSB_RING_BASE_ADDR_MSB,
				 ((u64)srng->ring_base_paddr >>
				 HAL_ADDR_MSB_REG_SHIFT)) |
			FIELD_PREP(HAL_TCL1_RING_BASE_MSB_RING_SIZE,
				   (srng->entry_size * srng->num_entries));
		ath11k_hif_write32(ab, reg_base + HAL_TCL1_RING_BASE_MSB_OFFSET(ab), val);
	}

	/* interrupt setup */
	/* NOTE: IPQ8074 v2 requires the interrupt timer threshold in the
	 * unit of 8 usecs instead of 1 usec (as required by v1).
	 */
	val = FIELD_PREP(HAL_TCL1_RING_CONSR_INT_SETUP_IX0_INTR_TMR_THOLD,
			 srng->intr_timer_thres_us);

	val |= FIELD_PREP(HAL_TCL1_RING_CONSR_INT_SETUP_IX0_BATCH_COUNTER_THOLD,
			  (srng->intr_batch_cntr_thres_entries *
			   srng->entry_size));

	ath11k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX0_OFFSET(ab),
			   val);

	val = 0;
	if (srng->flags & HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN) {
		val |= FIELD_PREP(HAL_TCL1_RING_CONSR_INT_SETUP_IX1_LOW_THOLD,
				  srng->u.src_ring.low_threshold);
	}
	ath11k_hif_write32(ab,
			   reg_base + HAL_TCL1_RING_CONSR_INT_SETUP_IX1_OFFSET(ab),
			   val);

	if (srng->ring_id != HAL_SRNG_RING_ID_WBM_IDLE_LINK) {
		tp_addr = hal->rdp.paddr +
			  ((unsigned long)srng->u.src_ring.tp_addr -
			   (unsigned long)hal->rdp.vaddr);
		ath11k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_LSB_OFFSET(ab),
				   tp_addr & HAL_ADDR_LSB_REG_MASK);
		ath11k_hif_write32(ab,
				   reg_base + HAL_TCL1_RING_TP_ADDR_MSB_OFFSET(ab),
				   tp_addr >> HAL_ADDR_MSB_REG_SHIFT);
	}

	/* Initialize head and tail pointers to indicate ring is empty */
	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];
	ath11k_hif_write32(ab, reg_base, 0);
	ath11k_hif_write32(ab, reg_base + HAL_TCL1_RING_TP_OFFSET, 0);
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

	ath11k_hif_write32(ab, reg_base + HAL_TCL1_RING_MISC_OFFSET(ab), val);
}

static void ath11k_hal_srng_hw_init(struct ath11k_base *ab,
				    struct hal_srng *srng)
{
	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		ath11k_hal_srng_src_hw_init(ab, srng);
	else
		ath11k_hal_srng_dst_hw_init(ab, srng);
}

static int ath11k_hal_srng_get_ring_id(struct ath11k_base *ab,
				       enum hal_ring_type type,
				       int ring_num, int mac_id)
{
	struct hal_srng_config *srng_config = &ab->hal.srng_config[type];
	int ring_id;

	if (ring_num >= srng_config->max_rings) {
		ath11k_warn(ab, "invalid ring number :%d\n", ring_num);
		return -EINVAL;
	}

	ring_id = srng_config->start_ring_id + ring_num;
	if (srng_config->lmac_ring)
		ring_id += mac_id * HAL_SRNG_RINGS_PER_LMAC;

	if (WARN_ON(ring_id >= HAL_SRNG_RING_ID_MAX))
		return -EINVAL;

	return ring_id;
}

int ath11k_hal_srng_get_entrysize(struct ath11k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->entry_size << 2);
}

int ath11k_hal_srng_get_max_entries(struct ath11k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->max_size / srng_config->entry_size);
}

void ath11k_hal_srng_get_params(struct ath11k_base *ab, struct hal_srng *srng,
				struct hal_srng_params *params)
{
	params->ring_base_paddr = srng->ring_base_paddr;
	params->ring_base_vaddr = srng->ring_base_vaddr;
	params->num_entries = srng->num_entries;
	params->intr_timer_thres_us = srng->intr_timer_thres_us;
	params->intr_batch_cntr_thres_entries =
		srng->intr_batch_cntr_thres_entries;
	params->low_threshold = srng->u.src_ring.low_threshold;
	params->msi_addr = srng->msi_addr;
	params->msi_data = srng->msi_data;
	params->flags = srng->flags;
}

dma_addr_t ath11k_hal_srng_get_hp_addr(struct ath11k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.src_ring.hp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
	else
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.dst_ring.hp_addr -
			 (unsigned long)ab->hal.rdp.vaddr);
}

dma_addr_t ath11k_hal_srng_get_tp_addr(struct ath11k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.src_ring.tp_addr -
			(unsigned long)ab->hal.rdp.vaddr);
	else
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.dst_ring.tp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
}

u32 ath11k_hal_ce_get_desc_size(enum hal_ce_desc type)
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

void ath11k_hal_ce_src_set_desc(void *buf, dma_addr_t paddr, u32 len, u32 id,
				u8 byte_swap_data)
{
	struct hal_ce_srng_src_desc *desc = buf;

	desc->buffer_addr_low = paddr & HAL_ADDR_LSB_REG_MASK;
	desc->buffer_addr_info =
		FIELD_PREP(HAL_CE_SRC_DESC_ADDR_INFO_ADDR_HI,
			   ((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT)) |
		FIELD_PREP(HAL_CE_SRC_DESC_ADDR_INFO_BYTE_SWAP,
			   byte_swap_data) |
		FIELD_PREP(HAL_CE_SRC_DESC_ADDR_INFO_GATHER, 0) |
		FIELD_PREP(HAL_CE_SRC_DESC_ADDR_INFO_LEN, len);
	desc->meta_info = FIELD_PREP(HAL_CE_SRC_DESC_META_INFO_DATA, id);
}

void ath11k_hal_ce_dst_set_desc(void *buf, dma_addr_t paddr)
{
	struct hal_ce_srng_dest_desc *desc = buf;

	desc->buffer_addr_low = paddr & HAL_ADDR_LSB_REG_MASK;
	desc->buffer_addr_info =
		FIELD_PREP(HAL_CE_DEST_DESC_ADDR_INFO_ADDR_HI,
			   ((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT));
}

u32 ath11k_hal_ce_dst_status_get_length(void *buf)
{
	struct hal_ce_srng_dst_status_desc *desc = buf;
	u32 len;

	len = FIELD_GET(HAL_CE_DST_STATUS_DESC_FLAGS_LEN, desc->flags);
	desc->flags &= ~HAL_CE_DST_STATUS_DESC_FLAGS_LEN;

	return len;
}

void ath11k_hal_set_link_desc_addr(struct hal_wbm_link_desc *desc, u32 cookie,
				   dma_addr_t paddr)
{
	desc->buf_addr_info.info0 = FIELD_PREP(BUFFER_ADDR_INFO0_ADDR,
					       (paddr & HAL_ADDR_LSB_REG_MASK));
	desc->buf_addr_info.info1 = FIELD_PREP(BUFFER_ADDR_INFO1_ADDR,
					       ((u64)paddr >> HAL_ADDR_MSB_REG_SHIFT)) |
				    FIELD_PREP(BUFFER_ADDR_INFO1_RET_BUF_MGR, 1) |
				    FIELD_PREP(BUFFER_ADDR_INFO1_SW_COOKIE, cookie);
}

u32 *ath11k_hal_srng_dst_peek(struct ath11k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp != srng->u.dst_ring.cached_hp)
		return (srng->ring_base_vaddr + srng->u.dst_ring.tp);

	return NULL;
}

static void ath11k_hal_srng_prefetch_desc(struct ath11k_base *ab,
					  struct hal_srng *srng)
{
	u32 *desc;

	/* prefetch only if desc is available */
	desc = ath11k_hal_srng_dst_peek(ab, srng);
	if (likely(desc)) {
		dma_sync_single_for_cpu(ab->dev, virt_to_phys(desc),
					(srng->entry_size * sizeof(u32)),
					DMA_FROM_DEVICE);
		prefetch(desc);
	}
}

u32 *ath11k_hal_srng_dst_get_next_entry(struct ath11k_base *ab,
					struct hal_srng *srng)
{
	u32 *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp == srng->u.dst_ring.cached_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.dst_ring.tp;

	srng->u.dst_ring.tp += srng->entry_size;

	/* wrap around to start of ring*/
	if (srng->u.dst_ring.tp == srng->ring_size)
		srng->u.dst_ring.tp = 0;

	/* Try to prefetch the next descriptor in the ring */
	if (srng->flags & HAL_SRNG_FLAGS_CACHED)
		ath11k_hal_srng_prefetch_desc(ab, srng);

	return desc;
}

int ath11k_hal_srng_dst_num_free(struct ath11k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	tp = srng->u.dst_ring.tp;

	if (sync_hw_ptr) {
		hp = *srng->u.dst_ring.hp_addr;
		srng->u.dst_ring.cached_hp = hp;
	} else {
		hp = srng->u.dst_ring.cached_hp;
	}

	if (hp >= tp)
		return (hp - tp) / srng->entry_size;
	else
		return (srng->ring_size - tp + hp) / srng->entry_size;
}

/* Returns number of available entries in src ring */
int ath11k_hal_srng_src_num_free(struct ath11k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	hp = srng->u.src_ring.hp;

	if (sync_hw_ptr) {
		tp = *srng->u.src_ring.tp_addr;
		srng->u.src_ring.cached_tp = tp;
	} else {
		tp = srng->u.src_ring.cached_tp;
	}

	if (tp > hp)
		return ((tp - hp) / srng->entry_size) - 1;
	else
		return ((srng->ring_size - hp + tp) / srng->entry_size) - 1;
}

u32 *ath11k_hal_srng_src_get_next_entry(struct ath11k_base *ab,
					struct hal_srng *srng)
{
	u32 *desc;
	u32 next_hp;

	lockdep_assert_held(&srng->lock);

	/* TODO: Using % is expensive, but we have to do this since size of some
	 * SRNG rings is not power of 2 (due to descriptor sizes). Need to see
	 * if separate function is defined for rings having power of 2 ring size
	 * (TCL2SW, REO2SW, SW2RXDMA and CE rings) so that we can avoid the
	 * overhead of % by using mask (with &).
	 */
	next_hp = (srng->u.src_ring.hp + srng->entry_size) % srng->ring_size;

	if (next_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = next_hp;

	/* TODO: Reap functionality is not used by all rings. If particular
	 * ring does not use reap functionality, we need not update reap_hp
	 * with next_hp pointer. Need to make sure a separate function is used
	 * before doing any optimization by removing below code updating
	 * reap_hp.
	 */
	srng->u.src_ring.reap_hp = next_hp;

	return desc;
}

u32 *ath11k_hal_srng_src_reap_next(struct ath11k_base *ab,
				   struct hal_srng *srng)
{
	u32 *desc;
	u32 next_reap_hp;

	lockdep_assert_held(&srng->lock);

	next_reap_hp = (srng->u.src_ring.reap_hp + srng->entry_size) %
		       srng->ring_size;

	if (next_reap_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + next_reap_hp;
	srng->u.src_ring.reap_hp = next_reap_hp;

	return desc;
}

u32 *ath11k_hal_srng_src_get_next_reaped(struct ath11k_base *ab,
					 struct hal_srng *srng)
{
	u32 *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.src_ring.hp == srng->u.src_ring.reap_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = (srng->u.src_ring.hp + srng->entry_size) %
			      srng->ring_size;

	return desc;
}

u32 *ath11k_hal_srng_src_peek(struct ath11k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (((srng->u.src_ring.hp + srng->entry_size) % srng->ring_size) ==
	    srng->u.src_ring.cached_tp)
		return NULL;

	return srng->ring_base_vaddr + srng->u.src_ring.hp;
}

void ath11k_hal_srng_access_begin(struct ath11k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		srng->u.src_ring.cached_tp =
			*(volatile u32 *)srng->u.src_ring.tp_addr;
	} else {
		srng->u.dst_ring.cached_hp = *srng->u.dst_ring.hp_addr;

		/* Try to prefetch the next descriptor in the ring */
		if (srng->flags & HAL_SRNG_FLAGS_CACHED)
			ath11k_hal_srng_prefetch_desc(ab, srng);
	}
}

/* Update cached ring head/tail pointers to HW. ath11k_hal_srng_access_begin()
 * should have been called before this.
 */
void ath11k_hal_srng_access_end(struct ath11k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	/* TODO: See if we need a write memory barrier here */
	if (srng->flags & HAL_SRNG_FLAGS_LMAC_RING) {
		/* For LMAC rings, ring pointer updates are done through FW and
		 * hence written to a shared memory location that is read by FW
		 */
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			*srng->u.src_ring.hp_addr = srng->u.src_ring.hp;
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			*srng->u.dst_ring.tp_addr = srng->u.dst_ring.tp;
		}
	} else {
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			ath11k_hif_write32(ab,
					   (unsigned long)srng->u.src_ring.hp_addr -
					   (unsigned long)ab->mem,
					   srng->u.src_ring.hp);
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			ath11k_hif_write32(ab,
					   (unsigned long)srng->u.dst_ring.tp_addr -
					   (unsigned long)ab->mem,
					   srng->u.dst_ring.tp);
		}
	}

	srng->timestamp = jiffies;
}

void ath11k_hal_setup_link_idle_list(struct ath11k_base *ab,
				     struct hal_wbm_idle_scatter_list *sbuf,
				     u32 nsbufs, u32 tot_link_desc,
				     u32 end_offset)
{
	struct ath11k_buffer_addr *link_addr;
	int i;
	u32 reg_scatter_buf_sz = HAL_WBM_IDLE_SCATTER_BUF_SIZE / 64;

	link_addr = (void *)sbuf[0].vaddr + HAL_WBM_IDLE_SCATTER_BUF_SIZE;

	for (i = 1; i < nsbufs; i++) {
		link_addr->info0 = sbuf[i].paddr & HAL_ADDR_LSB_REG_MASK;
		link_addr->info1 = FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32,
				(u64)sbuf[i].paddr >> HAL_ADDR_MSB_REG_SHIFT) |
				FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG,
				BASE_ADDR_MATCH_TAG_VAL);

		link_addr = (void *)sbuf[i].vaddr +
			     HAL_WBM_IDLE_SCATTER_BUF_SIZE;
	}

	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_R0_IDLE_LIST_CONTROL_ADDR,
			   FIELD_PREP(HAL_WBM_SCATTER_BUFFER_SIZE, reg_scatter_buf_sz) |
			   FIELD_PREP(HAL_WBM_LINK_DESC_IDLE_LIST_MODE, 0x1));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_R0_IDLE_LIST_SIZE_ADDR,
			   FIELD_PREP(HAL_WBM_SCATTER_RING_SIZE_OF_IDLE_LINK_DESC_LIST,
				      reg_scatter_buf_sz * nsbufs));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_LSB,
			   FIELD_PREP(BUFFER_ADDR_INFO0_ADDR,
				      sbuf[0].paddr & HAL_ADDR_LSB_REG_MASK));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_RING_BASE_MSB,
			   FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32,
				(u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT) |
				FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_MATCH_TAG,
				BASE_ADDR_MATCH_TAG_VAL));

	/* Setup head and tail pointers for the idle list */
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0,
			   FIELD_PREP(BUFFER_ADDR_INFO0_ADDR,
				      sbuf[nsbufs - 1].paddr));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX1,
			   FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32,
				((u64)sbuf[nsbufs - 1].paddr >>
				 HAL_ADDR_MSB_REG_SHIFT)) |
			   FIELD_PREP(HAL_WBM_SCATTERED_DESC_HEAD_P_OFFSET_IX1,
				      (end_offset >> 2)));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HEAD_INFO_IX0,
			   FIELD_PREP(BUFFER_ADDR_INFO0_ADDR,
				      sbuf[0].paddr));

	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX0,
			   FIELD_PREP(BUFFER_ADDR_INFO0_ADDR,
				      sbuf[0].paddr));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_TAIL_INFO_IX1,
			   FIELD_PREP(
				HAL_WBM_SCATTERED_DESC_MSB_BASE_ADDR_39_32,
				((u64)sbuf[0].paddr >> HAL_ADDR_MSB_REG_SHIFT)) |
			   FIELD_PREP(HAL_WBM_SCATTERED_DESC_TAIL_P_OFFSET_IX1,
				      0));
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_SCATTERED_DESC_PTR_HP_ADDR,
			   2 * tot_link_desc);

	/* Enable the SRNG */
	ath11k_hif_write32(ab,
			   HAL_SEQ_WCSS_UMAC_WBM_REG +
			   HAL_WBM_IDLE_LINK_RING_MISC_ADDR(ab), 0x40);
}

int ath11k_hal_srng_setup(struct ath11k_base *ab, enum hal_ring_type type,
			  int ring_num, int mac_id,
			  struct hal_srng_params *params)
{
	struct ath11k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &ab->hal.srng_config[type];
	struct hal_srng *srng;
	int ring_id;
	u32 lmac_idx;
	int i;
	u32 reg_base;

	ring_id = ath11k_hal_srng_get_ring_id(ab, type, ring_num, mac_id);
	if (ring_id < 0)
		return ring_id;

	srng = &hal->srng_list[ring_id];

	srng->ring_id = ring_id;
	srng->ring_dir = srng_config->ring_dir;
	srng->ring_base_paddr = params->ring_base_paddr;
	srng->ring_base_vaddr = params->ring_base_vaddr;
	srng->entry_size = srng_config->entry_size;
	srng->num_entries = params->num_entries;
	srng->ring_size = srng->entry_size * srng->num_entries;
	srng->intr_batch_cntr_thres_entries =
				params->intr_batch_cntr_thres_entries;
	srng->intr_timer_thres_us = params->intr_timer_thres_us;
	srng->flags = params->flags;
	srng->msi_addr = params->msi_addr;
	srng->msi_data = params->msi_data;
	srng->initialized = 1;
	spin_lock_init(&srng->lock);
	lockdep_set_class(&srng->lock, hal->srng_key + ring_id);

	for (i = 0; i < HAL_SRNG_NUM_REG_GRP; i++) {
		srng->hwreg_base[i] = srng_config->reg_start[i] +
				      (ring_num * srng_config->reg_size[i]);
	}

	memset(srng->ring_base_vaddr, 0,
	       (srng->entry_size * srng->num_entries) << 2);

	/* TODO: Add comments on these swap configurations */
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		srng->flags |= HAL_SRNG_FLAGS_MSI_SWAP | HAL_SRNG_FLAGS_DATA_TLV_SWAP |
			       HAL_SRNG_FLAGS_RING_PTR_SWAP;

	reg_base = srng->hwreg_base[HAL_SRNG_REG_GRP_R2];

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		srng->u.src_ring.hp = 0;
		srng->u.src_ring.cached_tp = 0;
		srng->u.src_ring.reap_hp = srng->ring_size - srng->entry_size;
		srng->u.src_ring.tp_addr = (void *)(hal->rdp.vaddr + ring_id);
		srng->u.src_ring.low_threshold = params->low_threshold *
						 srng->entry_size;
		if (srng_config->lmac_ring) {
			lmac_idx = ring_id - HAL_SRNG_RING_ID_LMAC1_ID_START;
			srng->u.src_ring.hp_addr = (void *)(hal->wrp.vaddr +
						   lmac_idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		} else {
			if (!ab->hw_params.supports_shadow_regs)
				srng->u.src_ring.hp_addr =
				(u32 *)((unsigned long)ab->mem + reg_base);
			else
				ath11k_dbg(ab, ATH11K_DBG_HAL,
					   "type %d ring_num %d reg_base 0x%x shadow 0x%lx\n",
					   type, ring_num,
					   reg_base,
					   (unsigned long)srng->u.src_ring.hp_addr -
					   (unsigned long)ab->mem);
		}
	} else {
		/* During initialization loop count in all the descriptors
		 * will be set to zero, and HW will set it to 1 on completing
		 * descriptor update in first loop, and increments it by 1 on
		 * subsequent loops (loop count wraps around after reaching
		 * 0xffff). The 'loop_cnt' in SW ring state is the expected
		 * loop count in descriptors updated by HW (to be processed
		 * by SW).
		 */
		srng->u.dst_ring.loop_cnt = 1;
		srng->u.dst_ring.tp = 0;
		srng->u.dst_ring.cached_hp = 0;
		srng->u.dst_ring.hp_addr = (void *)(hal->rdp.vaddr + ring_id);
		if (srng_config->lmac_ring) {
			/* For LMAC rings, tail pointer updates will be done
			 * through FW by writing to a shared memory location
			 */
			lmac_idx = ring_id - HAL_SRNG_RING_ID_LMAC1_ID_START;
			srng->u.dst_ring.tp_addr = (void *)(hal->wrp.vaddr +
						   lmac_idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		} else {
			if (!ab->hw_params.supports_shadow_regs)
				srng->u.dst_ring.tp_addr =
				(u32 *)((unsigned long)ab->mem + reg_base +
					(HAL_REO1_RING_TP(ab) - HAL_REO1_RING_HP(ab)));
			else
				ath11k_dbg(ab, ATH11K_DBG_HAL,
					   "type %d ring_num %d target_reg 0x%x shadow 0x%lx\n",
					   type, ring_num,
					   reg_base + (HAL_REO1_RING_TP(ab) -
						       HAL_REO1_RING_HP(ab)),
					   (unsigned long)srng->u.dst_ring.tp_addr -
					   (unsigned long)ab->mem);
		}
	}

	if (srng_config->lmac_ring)
		return ring_id;

	ath11k_hal_srng_hw_init(ab, srng);

	if (type == HAL_CE_DST) {
		srng->u.dst_ring.max_buffer_length = params->max_buffer_len;
		ath11k_hal_ce_dst_setup(ab, srng, ring_num);
	}

	return ring_id;
}

static void ath11k_hal_srng_update_hp_tp_addr(struct ath11k_base *ab,
					      int shadow_cfg_idx,
					  enum hal_ring_type ring_type,
					  int ring_num)
{
	struct hal_srng *srng;
	struct ath11k_hal *hal = &ab->hal;
	int ring_id;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

	ring_id = ath11k_hal_srng_get_ring_id(ab, ring_type, ring_num, 0);
	if (ring_id < 0)
		return;

	srng = &hal->srng_list[ring_id];

	if (srng_config->ring_dir == HAL_SRNG_DIR_DST)
		srng->u.dst_ring.tp_addr = (u32 *)(HAL_SHADOW_REG(ab, shadow_cfg_idx) +
						   (unsigned long)ab->mem);
	else
		srng->u.src_ring.hp_addr = (u32 *)(HAL_SHADOW_REG(ab, shadow_cfg_idx) +
						   (unsigned long)ab->mem);
}

int ath11k_hal_srng_update_shadow_config(struct ath11k_base *ab,
					 enum hal_ring_type ring_type,
					 int ring_num)
{
	struct ath11k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &hal->srng_config[ring_type];
	int shadow_cfg_idx = hal->num_shadow_reg_configured;
	u32 target_reg;

	if (shadow_cfg_idx >= HAL_SHADOW_NUM_REGS)
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
	ath11k_hal_srng_update_hp_tp_addr(ab, shadow_cfg_idx, ring_type,
					  ring_num);

	ath11k_dbg(ab, ATH11K_DBG_HAL,
		   "update shadow config target_reg %x shadow reg 0x%x shadow_idx 0x%x ring_type %d ring num %d",
		  target_reg,
		  HAL_SHADOW_REG(ab, shadow_cfg_idx),
		  shadow_cfg_idx,
		  ring_type, ring_num);

	return 0;
}

void ath11k_hal_srng_shadow_config(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	int ring_type, ring_num;

	/* update all the non-CE srngs. */
	for (ring_type = 0; ring_type < HAL_MAX_RING_TYPES; ring_type++) {
		struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

		if (ring_type == HAL_CE_SRC ||
		    ring_type == HAL_CE_DST ||
			ring_type == HAL_CE_DST_STATUS)
			continue;

		if (srng_config->lmac_ring)
			continue;

		for (ring_num = 0; ring_num < srng_config->max_rings; ring_num++)
			ath11k_hal_srng_update_shadow_config(ab, ring_type, ring_num);
	}
}

void ath11k_hal_srng_get_shadow_config(struct ath11k_base *ab,
				       u32 **cfg, u32 *len)
{
	struct ath11k_hal *hal = &ab->hal;

	*len = hal->num_shadow_reg_configured;
	*cfg = hal->shadow_reg_addr;
}

void ath11k_hal_srng_shadow_update_hp_tp(struct ath11k_base *ab,
					 struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	/* check whether the ring is empty. Update the shadow
	 * HP only when then ring isn't empty.
	 */
	if (srng->ring_dir == HAL_SRNG_DIR_SRC &&
	    *srng->u.src_ring.tp_addr != srng->u.src_ring.hp)
		ath11k_hal_srng_access_end(ab, srng);
}

static int ath11k_hal_srng_create_config(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	struct hal_srng_config *s;

	hal->srng_config = kmemdup(hw_srng_config_template,
				   sizeof(hw_srng_config_template),
				   GFP_KERNEL);
	if (!hal->srng_config)
		return -ENOMEM;

	s = &hal->srng_config[HAL_REO_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO1_RING_HP(ab);
	s->reg_size[0] = HAL_REO2_RING_BASE_LSB(ab) - HAL_REO1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_REO2_RING_HP(ab) - HAL_REO1_RING_HP(ab);

	s = &hal->srng_config[HAL_REO_EXCEPTION];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_TCL_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_TCL_RING_HP(ab);

	s = &hal->srng_config[HAL_REO_REINJECT];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_SW2REO_RING_HP(ab);

	s = &hal->srng_config[HAL_REO_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_CMD_HP(ab);

	s = &hal->srng_config[HAL_REO_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_REO_REG + HAL_REO_STATUS_HP(ab);

	s = &hal->srng_config[HAL_TCL_DATA];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL1_RING_HP;
	s->reg_size[0] = HAL_TCL2_RING_BASE_LSB(ab) - HAL_TCL1_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_TCL2_RING_HP - HAL_TCL1_RING_HP;

	s = &hal->srng_config[HAL_TCL_CMD];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_RING_HP;

	s = &hal->srng_config[HAL_TCL_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_TCL_REG + HAL_TCL_STATUS_RING_HP;

	s = &hal->srng_config[HAL_CE_SRC];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_BASE_LSB +
		ATH11K_CE_OFFSET(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab) + HAL_CE_DST_RING_HP +
		ATH11K_CE_OFFSET(ab);
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_SRC_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_SRC_REG(ab);

	s = &hal->srng_config[HAL_CE_DST];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_BASE_LSB +
		ATH11K_CE_OFFSET(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_RING_HP +
		ATH11K_CE_OFFSET(ab);
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_CE_DST_STATUS];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) +
		HAL_CE_DST_STATUS_RING_BASE_LSB + ATH11K_CE_OFFSET(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab) + HAL_CE_DST_STATUS_RING_HP +
		ATH11K_CE_OFFSET(ab);
	s->reg_size[0] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);
	s->reg_size[1] = HAL_SEQ_WCSS_UMAC_CE1_DST_REG(ab) -
		HAL_SEQ_WCSS_UMAC_CE0_DST_REG(ab);

	s = &hal->srng_config[HAL_WBM_IDLE_LINK];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_IDLE_LINK_RING_HP;

	s = &hal->srng_config[HAL_SW2WBM_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM_RELEASE_RING_HP;

	s = &hal->srng_config[HAL_WBM2SW_RELEASE];
	s->reg_start[0] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
	s->reg_start[1] = HAL_SEQ_WCSS_UMAC_WBM_REG + HAL_WBM0_RELEASE_RING_HP;
	s->reg_size[0] = HAL_WBM1_RELEASE_RING_BASE_LSB(ab) -
		HAL_WBM0_RELEASE_RING_BASE_LSB(ab);
	s->reg_size[1] = HAL_WBM1_RELEASE_RING_HP - HAL_WBM0_RELEASE_RING_HP;

	return 0;
}

static void ath11k_hal_register_srng_key(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_register_key(hal->srng_key + ring_id);
}

static void ath11k_hal_unregister_srng_key(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_unregister_key(hal->srng_key + ring_id);
}

int ath11k_hal_srng_init(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;
	int ret;

	memset(hal, 0, sizeof(*hal));

	ret = ath11k_hal_srng_create_config(ab);
	if (ret)
		goto err_hal;

	ret = ath11k_hal_alloc_cont_rdp(ab);
	if (ret)
		goto err_hal;

	ret = ath11k_hal_alloc_cont_wrp(ab);
	if (ret)
		goto err_free_cont_rdp;

	ath11k_hal_register_srng_key(ab);

	return 0;

err_free_cont_rdp:
	ath11k_hal_free_cont_rdp(ab);

err_hal:
	return ret;
}
EXPORT_SYMBOL(ath11k_hal_srng_init);

void ath11k_hal_srng_deinit(struct ath11k_base *ab)
{
	struct ath11k_hal *hal = &ab->hal;

	ath11k_hal_unregister_srng_key(ab);
	ath11k_hal_free_cont_rdp(ab);
	ath11k_hal_free_cont_wrp(ab);
	kfree(hal->srng_config);
}
EXPORT_SYMBOL(ath11k_hal_srng_deinit);

void ath11k_hal_dump_srng_stats(struct ath11k_base *ab)
{
	struct hal_srng *srng;
	struct ath11k_ext_irq_grp *irq_grp;
	struct ath11k_ce_pipe *ce_pipe;
	int i;

	ath11k_err(ab, "Last interrupt received for each CE:\n");
	for (i = 0; i < ab->hw_params.ce_count; i++) {
		ce_pipe = &ab->ce.ce_pipe[i];

		if (ath11k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		ath11k_err(ab, "CE_id %d pipe_num %d %ums before\n",
			   i, ce_pipe->pipe_num,
			   jiffies_to_msecs(jiffies - ce_pipe->timestamp));
	}

	ath11k_err(ab, "\nLast interrupt received for each group:\n");
	for (i = 0; i < ATH11K_EXT_IRQ_GRP_NUM_MAX; i++) {
		irq_grp = &ab->ext_irq_grp[i];
		ath11k_err(ab, "group_id %d %ums before\n",
			   irq_grp->grp_id,
			   jiffies_to_msecs(jiffies - irq_grp->timestamp));
	}

	for (i = 0; i < HAL_SRNG_RING_ID_MAX; i++) {
		srng = &ab->hal.srng_list[i];

		if (!srng->initialized)
			continue;

		if (srng->ring_dir == HAL_SRNG_DIR_SRC)
			ath11k_err(ab,
				   "src srng id %u hp %u, reap_hp %u, cur tp %u, cached tp %u last tp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.src_ring.hp,
				   srng->u.src_ring.reap_hp,
				   *srng->u.src_ring.tp_addr, srng->u.src_ring.cached_tp,
				   srng->u.src_ring.last_tp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
		else if (srng->ring_dir == HAL_SRNG_DIR_DST)
			ath11k_err(ab,
				   "dst srng id %u tp %u, cur hp %u, cached hp %u last hp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.dst_ring.tp,
				   *srng->u.dst_ring.hp_addr,
				   srng->u.dst_ring.cached_hp,
				   srng->u.dst_ring.last_hp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
	}
}
