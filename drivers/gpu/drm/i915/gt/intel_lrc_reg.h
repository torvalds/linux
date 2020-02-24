/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _INTEL_LRC_REG_H_
#define _INTEL_LRC_REG_H_

#include <linux/types.h>

/* GEN8 to GEN11 Reg State Context */
#define CTX_CONTEXT_CONTROL		(0x02 + 1)
#define CTX_RING_HEAD			(0x04 + 1)
#define CTX_RING_TAIL			(0x06 + 1)
#define CTX_RING_START			(0x08 + 1)
#define CTX_RING_CTL			(0x0a + 1)
#define CTX_BB_STATE			(0x10 + 1)
#define CTX_BB_PER_CTX_PTR		(0x18 + 1)
#define CTX_PDP3_UDW			(0x24 + 1)
#define CTX_PDP3_LDW			(0x26 + 1)
#define CTX_PDP2_UDW			(0x28 + 1)
#define CTX_PDP2_LDW			(0x2a + 1)
#define CTX_PDP1_UDW			(0x2c + 1)
#define CTX_PDP1_LDW			(0x2e + 1)
#define CTX_PDP0_UDW			(0x30 + 1)
#define CTX_PDP0_LDW			(0x32 + 1)
#define CTX_R_PWR_CLK_STATE		(0x42 + 1)

#define GEN9_CTX_RING_MI_MODE		0x54

/* GEN12+ Reg State Context */
#define GEN12_CTX_BB_PER_CTX_PTR		(0x12 + 1)

#define ASSIGN_CTX_PDP(ppgtt, reg_state, n) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = i915_page_dir_dma_addr((ppgtt), (n)); \
	(reg_state__)[CTX_PDP ## n ## _UDW] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP ## n ## _LDW] = lower_32_bits(addr__); \
} while (0)

#define ASSIGN_CTX_PML4(ppgtt, reg_state) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = px_dma(ppgtt->pd); \
	(reg_state__)[CTX_PDP0_UDW] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP0_LDW] = lower_32_bits(addr__); \
} while (0)

#define GEN8_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x17
#define GEN9_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x26
#define GEN10_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x19
#define GEN11_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x1A
#define GEN12_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0xD

#endif /* _INTEL_LRC_REG_H_ */
