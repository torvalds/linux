/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GTT_DEFS_H_
#define _XE_GTT_DEFS_H_

#define XELPG_GGTT_PTE_PAT0	BIT_ULL(52)
#define XELPG_GGTT_PTE_PAT1	BIT_ULL(53)

#define GGTT_PTE_VFID		GENMASK_ULL(11, 2)

#define GUC_GGTT_TOP		0xFEE00000

#define XELPG_PPGTT_PTE_PAT3		BIT_ULL(62)
#define XE2_PPGTT_PTE_PAT4		BIT_ULL(61)
#define XE_PPGTT_PDE_PDPE_PAT2		BIT_ULL(12)
#define XE_PPGTT_PTE_PAT2		BIT_ULL(7)
#define XE_PPGTT_PTE_PAT1		BIT_ULL(4)
#define XE_PPGTT_PTE_PAT0		BIT_ULL(3)

#define XE_PDE_PS_2M			BIT_ULL(7)
#define XE_PDPE_PS_1G			BIT_ULL(7)
#define XE_PDE_IPS_64K			BIT_ULL(11)

#define XE_GGTT_PTE_DM			BIT_ULL(1)
#define XE_USM_PPGTT_PTE_AE		BIT_ULL(10)
#define XE_PPGTT_PTE_DM			BIT_ULL(11)
#define XE_PDE_64K			BIT_ULL(6)
#define XE_PTE_PS64			BIT_ULL(8)
#define XE_PTE_NULL			BIT_ULL(9)

#define XE_PAGE_PRESENT			BIT_ULL(0)
#define XE_PAGE_RW			BIT_ULL(1)

#endif
