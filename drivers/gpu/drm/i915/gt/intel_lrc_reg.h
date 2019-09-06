/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _INTEL_LRC_REG_H_
#define _INTEL_LRC_REG_H_

#include <linux/types.h>

/* GEN8 to GEN11 Reg State Context */
#define CTX_LRI_HEADER_0		0x01
#define CTX_CONTEXT_CONTROL		0x02
#define CTX_RING_HEAD			0x04
#define CTX_RING_TAIL			0x06
#define CTX_RING_BUFFER_START		0x08
#define CTX_RING_BUFFER_CONTROL		0x0a
#define CTX_BB_HEAD_U			0x0c
#define CTX_BB_HEAD_L			0x0e
#define CTX_BB_STATE			0x10
#define CTX_SECOND_BB_HEAD_U		0x12
#define CTX_SECOND_BB_HEAD_L		0x14
#define CTX_SECOND_BB_STATE		0x16
#define CTX_BB_PER_CTX_PTR		0x18
#define CTX_RCS_INDIRECT_CTX		0x1a
#define CTX_RCS_INDIRECT_CTX_OFFSET	0x1c
#define CTX_LRI_HEADER_1		0x21
#define CTX_CTX_TIMESTAMP		0x22
#define CTX_PDP3_UDW			0x24
#define CTX_PDP3_LDW			0x26
#define CTX_PDP2_UDW			0x28
#define CTX_PDP2_LDW			0x2a
#define CTX_PDP1_UDW			0x2c
#define CTX_PDP1_LDW			0x2e
#define CTX_PDP0_UDW			0x30
#define CTX_PDP0_LDW			0x32
#define CTX_LRI_HEADER_2		0x41
#define CTX_R_PWR_CLK_STATE		0x42
#define CTX_END				0x44

/* GEN12+ Reg State Context */
#define GEN12_CTX_BB_PER_CTX_PTR		0x12
#define GEN12_CTX_LRI_HEADER_3			0x41

#define CTX_REG(reg_state, pos, reg, val) do { \
	u32 *reg_state__ = (reg_state); \
	const u32 pos__ = (pos); \
	(reg_state__)[(pos__) + 0] = i915_mmio_reg_offset(reg); \
	(reg_state__)[(pos__) + 1] = (val); \
} while (0)

#define ASSIGN_CTX_PDP(ppgtt, reg_state, n) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = i915_page_dir_dma_addr((ppgtt), (n)); \
	(reg_state__)[CTX_PDP ## n ## _UDW + 1] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP ## n ## _LDW + 1] = lower_32_bits(addr__); \
} while (0)

#define ASSIGN_CTX_PML4(ppgtt, reg_state) do { \
	u32 *reg_state__ = (reg_state); \
	const u64 addr__ = px_dma(ppgtt->pd); \
	(reg_state__)[CTX_PDP0_UDW + 1] = upper_32_bits(addr__); \
	(reg_state__)[CTX_PDP0_LDW + 1] = lower_32_bits(addr__); \
} while (0)

#define GEN8_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x17
#define GEN9_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x26
#define GEN10_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x19
#define GEN11_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0x1A
#define GEN12_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0xD

#endif /* _INTEL_LRC_REG_H_ */
