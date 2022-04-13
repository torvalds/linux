/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_REG_H_
#define _INTEL_GUC_REG_H_

#include <linux/compiler.h>
#include <linux/types.h>

#include "i915_reg_defs.h"

/* Definitions of GuC H/W registers, bits, etc */

#define GUC_STATUS			_MMIO(0xc000)
#define   GS_RESET_SHIFT		0
#define   GS_MIA_IN_RESET		  (0x01 << GS_RESET_SHIFT)
#define   GS_BOOTROM_SHIFT		1
#define   GS_BOOTROM_MASK		  (0x7F << GS_BOOTROM_SHIFT)
#define   GS_BOOTROM_RSA_FAILED		  (0x50 << GS_BOOTROM_SHIFT)
#define   GS_BOOTROM_JUMP_PASSED	  (0x76 << GS_BOOTROM_SHIFT)
#define   GS_UKERNEL_SHIFT		8
#define   GS_UKERNEL_MASK		  (0xFF << GS_UKERNEL_SHIFT)
#define   GS_MIA_SHIFT			16
#define   GS_MIA_MASK			  (0x07 << GS_MIA_SHIFT)
#define   GS_MIA_CORE_STATE		  (0x01 << GS_MIA_SHIFT)
#define   GS_MIA_HALT_REQUESTED		  (0x02 << GS_MIA_SHIFT)
#define   GS_MIA_ISR_ENTRY		  (0x04 << GS_MIA_SHIFT)
#define   GS_AUTH_STATUS_SHIFT		30
#define   GS_AUTH_STATUS_MASK		  (0x03 << GS_AUTH_STATUS_SHIFT)
#define   GS_AUTH_STATUS_BAD		  (0x01 << GS_AUTH_STATUS_SHIFT)
#define   GS_AUTH_STATUS_GOOD		  (0x02 << GS_AUTH_STATUS_SHIFT)

#define SOFT_SCRATCH(n)			_MMIO(0xc180 + (n) * 4)
#define SOFT_SCRATCH_COUNT		16

#define GEN11_SOFT_SCRATCH(n)		_MMIO(0x190240 + (n) * 4)
#define GEN11_SOFT_SCRATCH_COUNT	4

#define UOS_RSA_SCRATCH(i)		_MMIO(0xc200 + (i) * 4)
#define UOS_RSA_SCRATCH_COUNT		64

#define DMA_ADDR_0_LOW			_MMIO(0xc300)
#define DMA_ADDR_0_HIGH			_MMIO(0xc304)
#define DMA_ADDR_1_LOW			_MMIO(0xc308)
#define DMA_ADDR_1_HIGH			_MMIO(0xc30c)
#define   DMA_ADDRESS_SPACE_WOPCM	  (7 << 16)
#define   DMA_ADDRESS_SPACE_GTT		  (8 << 16)
#define DMA_COPY_SIZE			_MMIO(0xc310)
#define DMA_CTRL			_MMIO(0xc314)
#define   HUC_UKERNEL			  (1<<9)
#define   UOS_MOVE			  (1<<4)
#define   START_DMA			  (1<<0)
#define DMA_GUC_WOPCM_OFFSET		_MMIO(0xc340)
#define   GUC_WOPCM_OFFSET_VALID	  (1<<0)
#define   HUC_LOADING_AGENT_VCR		  (0<<1)
#define   HUC_LOADING_AGENT_GUC		  (1<<1)
#define   GUC_WOPCM_OFFSET_SHIFT	14
#define   GUC_WOPCM_OFFSET_MASK		  (0x3ffff << GUC_WOPCM_OFFSET_SHIFT)
#define GUC_MAX_IDLE_COUNT		_MMIO(0xC3E4)

#define HUC_STATUS2             _MMIO(0xD3B0)
#define   HUC_FW_VERIFIED       (1<<7)

#define GEN11_HUC_KERNEL_LOAD_INFO	_MMIO(0xC1DC)
#define   HUC_LOAD_SUCCESSFUL		  (1 << 0)

#define GUC_WOPCM_SIZE			_MMIO(0xc050)
#define   GUC_WOPCM_SIZE_LOCKED		  (1<<0)
#define   GUC_WOPCM_SIZE_SHIFT		12
#define   GUC_WOPCM_SIZE_MASK		  (0xfffff << GUC_WOPCM_SIZE_SHIFT)

#define GEN8_GT_PM_CONFIG		_MMIO(0x138140)
#define GEN9LP_GT_PM_CONFIG		_MMIO(0x138140)
#define GEN9_GT_PM_CONFIG		_MMIO(0x13816c)
#define   GT_DOORBELL_ENABLE		  (1<<0)

#define GEN8_GTCR			_MMIO(0x4274)
#define   GEN8_GTCR_INVALIDATE		  (1<<0)

#define GEN12_GUC_TLB_INV_CR		_MMIO(0xcee8)
#define   GEN12_GUC_TLB_INV_CR_INVALIDATE	(1 << 0)

#define GUC_ARAT_C6DIS			_MMIO(0xA178)

#define GUC_SHIM_CONTROL		_MMIO(0xc064)
#define   GUC_DISABLE_SRAM_INIT_TO_ZEROES	(1<<0)
#define   GUC_ENABLE_READ_CACHE_LOGIC		(1<<1)
#define   GUC_ENABLE_MIA_CACHING		(1<<2)
#define   GUC_GEN10_MSGCH_ENABLE		(1<<4)
#define   GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA	(1<<9)
#define   GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA	(1<<10)
#define   GUC_ENABLE_MIA_CLOCK_GATING		(1<<15)
#define   GUC_GEN10_SHIM_WC_ENABLE		(1<<21)

#define GUC_SHIM_CONTROL2		_MMIO(0xc068)
#define   GUC_IS_PRIVILEGED		(1<<29)

#define GUC_SEND_INTERRUPT		_MMIO(0xc4c8)
#define   GUC_SEND_TRIGGER		  (1<<0)
#define GEN11_GUC_HOST_INTERRUPT	_MMIO(0x1901f0)

#define GUC_NUM_DOORBELLS		256

/* format of the HW-monitored doorbell cacheline */
struct guc_doorbell_info {
	u32 db_status;
#define GUC_DOORBELL_DISABLED		0
#define GUC_DOORBELL_ENABLED		1

	u32 cookie;
	u32 reserved[14];
} __packed;

#define GEN8_DRBREGL(x)			_MMIO(0x1000 + (x) * 8)
#define   GEN8_DRB_VALID		  (1<<0)
#define GEN8_DRBREGU(x)			_MMIO(0x1000 + (x) * 8 + 4)

#define GEN12_DIST_DBS_POPULATED		_MMIO(0xd08)
#define   GEN12_DOORBELLS_PER_SQIDI_SHIFT	16
#define   GEN12_DOORBELLS_PER_SQIDI		(0xff)
#define   GEN12_SQIDIS_DOORBELL_EXIST		(0xffff)

#define DE_GUCRMR			_MMIO(0x44054)

#define GUC_BCS_RCS_IER			_MMIO(0xC550)
#define GUC_VCS2_VCS1_IER		_MMIO(0xC554)
#define GUC_WD_VECS_IER			_MMIO(0xC558)
#define GUC_PM_P24C_IER			_MMIO(0xC55C)

/* GuC Interrupt Vector */
#define GUC_INTR_GUC2HOST		BIT(15)
#define GUC_INTR_EXEC_ERROR		BIT(14)
#define GUC_INTR_DISPLAY_EVENT		BIT(13)
#define GUC_INTR_SEM_SIG		BIT(12)
#define GUC_INTR_IOMMU2GUC		BIT(11)
#define GUC_INTR_DOORBELL_RANG		BIT(10)
#define GUC_INTR_DMA_DONE		BIT(9)
#define GUC_INTR_FATAL_ERROR		BIT(8)
#define GUC_INTR_NOTIF_ERROR		BIT(7)
#define GUC_INTR_SW_INT_6		BIT(6)
#define GUC_INTR_SW_INT_5		BIT(5)
#define GUC_INTR_SW_INT_4		BIT(4)
#define GUC_INTR_SW_INT_3		BIT(3)
#define GUC_INTR_SW_INT_2		BIT(2)
#define GUC_INTR_SW_INT_1		BIT(1)
#define GUC_INTR_SW_INT_0		BIT(0)

#endif
