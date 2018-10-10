/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_STRUCT_H
#define RVU_STRUCT_H

/* RVU Block Address Enumeration */
enum rvu_block_addr_e {
	BLKADDR_RVUM    = 0x0ULL,
	BLKADDR_LMT     = 0x1ULL,
	BLKADDR_MSIX    = 0x2ULL,
	BLKADDR_NPA     = 0x3ULL,
	BLKADDR_NIX0    = 0x4ULL,
	BLKADDR_NIX1    = 0x5ULL,
	BLKADDR_NPC     = 0x6ULL,
	BLKADDR_SSO     = 0x7ULL,
	BLKADDR_SSOW    = 0x8ULL,
	BLKADDR_TIM     = 0x9ULL,
	BLKADDR_CPT0    = 0xaULL,
	BLKADDR_CPT1    = 0xbULL,
	BLKADDR_NDC0    = 0xcULL,
	BLKADDR_NDC1    = 0xdULL,
	BLKADDR_NDC2    = 0xeULL,
	BLK_COUNT	= 0xfULL,
};

/* RVU Admin function Interrupt Vector Enumeration */
enum rvu_af_int_vec_e {
	RVU_AF_INT_VEC_POISON = 0x0,
	RVU_AF_INT_VEC_PFFLR  = 0x1,
	RVU_AF_INT_VEC_PFME   = 0x2,
	RVU_AF_INT_VEC_GEN    = 0x3,
	RVU_AF_INT_VEC_MBOX   = 0x4,
};

/**
 * RVU PF Interrupt Vector Enumeration
 */
enum rvu_pf_int_vec_e {
	RVU_PF_INT_VEC_VFFLR0     = 0x0,
	RVU_PF_INT_VEC_VFFLR1     = 0x1,
	RVU_PF_INT_VEC_VFME0      = 0x2,
	RVU_PF_INT_VEC_VFME1      = 0x3,
	RVU_PF_INT_VEC_VFPF_MBOX0 = 0x4,
	RVU_PF_INT_VEC_VFPF_MBOX1 = 0x5,
	RVU_PF_INT_VEC_AFPF_MBOX  = 0x6,
};

#endif /* RVU_STRUCT_H */
