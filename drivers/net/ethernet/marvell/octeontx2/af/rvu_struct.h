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

/* RVU Block Type Enumeration */
enum rvu_block_type_e {
	BLKTYPE_RVUM = 0x0,
	BLKTYPE_MSIX = 0x1,
	BLKTYPE_LMT  = 0x2,
	BLKTYPE_NIX  = 0x3,
	BLKTYPE_NPA  = 0x4,
	BLKTYPE_NPC  = 0x5,
	BLKTYPE_SSO  = 0x6,
	BLKTYPE_SSOW = 0x7,
	BLKTYPE_TIM  = 0x8,
	BLKTYPE_CPT  = 0x9,
	BLKTYPE_NDC  = 0xa,
	BLKTYPE_MAX  = 0xa,
};

/* RVU Admin function Interrupt Vector Enumeration */
enum rvu_af_int_vec_e {
	RVU_AF_INT_VEC_POISON = 0x0,
	RVU_AF_INT_VEC_PFFLR  = 0x1,
	RVU_AF_INT_VEC_PFME   = 0x2,
	RVU_AF_INT_VEC_GEN    = 0x3,
	RVU_AF_INT_VEC_MBOX   = 0x4,
	RVU_AF_INT_VEC_CNT    = 0x5,
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
	RVU_PF_INT_VEC_CNT	  = 0x7,
};

/* NPA admin queue completion enumeration */
enum npa_aq_comp {
	NPA_AQ_COMP_NOTDONE    = 0x0,
	NPA_AQ_COMP_GOOD       = 0x1,
	NPA_AQ_COMP_SWERR      = 0x2,
	NPA_AQ_COMP_CTX_POISON = 0x3,
	NPA_AQ_COMP_CTX_FAULT  = 0x4,
	NPA_AQ_COMP_LOCKERR    = 0x5,
};

/* NPA admin queue context types */
enum npa_aq_ctype {
	NPA_AQ_CTYPE_AURA = 0x0,
	NPA_AQ_CTYPE_POOL = 0x1,
};

/* NPA admin queue instruction opcodes */
enum npa_aq_instop {
	NPA_AQ_INSTOP_NOP    = 0x0,
	NPA_AQ_INSTOP_INIT   = 0x1,
	NPA_AQ_INSTOP_WRITE  = 0x2,
	NPA_AQ_INSTOP_READ   = 0x3,
	NPA_AQ_INSTOP_LOCK   = 0x4,
	NPA_AQ_INSTOP_UNLOCK = 0x5,
};

/* NPA admin queue instruction structure */
struct npa_aq_inst_s {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 doneint               : 1;	/* W0 */
	u64 reserved_44_62        : 19;
	u64 cindex                : 20;
	u64 reserved_17_23        : 7;
	u64 lf                    : 9;
	u64 ctype                 : 4;
	u64 op                    : 4;
#else
	u64 op                    : 4;
	u64 ctype                 : 4;
	u64 lf                    : 9;
	u64 reserved_17_23        : 7;
	u64 cindex                : 20;
	u64 reserved_44_62        : 19;
	u64 doneint               : 1;
#endif
	u64 res_addr;			/* W1 */
};

/* NPA admin queue result structure */
struct npa_aq_res_s {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_17_63        : 47; /* W0 */
	u64 doneint               : 1;
	u64 compcode              : 8;
	u64 ctype                 : 4;
	u64 op                    : 4;
#else
	u64 op                    : 4;
	u64 ctype                 : 4;
	u64 compcode              : 8;
	u64 doneint               : 1;
	u64 reserved_17_63        : 47;
#endif
	u64 reserved_64_127;		/* W1 */
};
#endif /* RVU_STRUCT_H */
