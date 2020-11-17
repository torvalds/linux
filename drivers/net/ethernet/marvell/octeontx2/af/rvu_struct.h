/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_STRUCT_H
#define RVU_STRUCT_H

/* RVU Block revision IDs */
#define RVU_BLK_RVUM_REVID		0x01

#define RVU_MULTI_BLK_VER		0x7ULL

/* RVU Block Address Enumeration */
enum rvu_block_addr_e {
	BLKADDR_RVUM		= 0x0ULL,
	BLKADDR_LMT		= 0x1ULL,
	BLKADDR_MSIX		= 0x2ULL,
	BLKADDR_NPA		= 0x3ULL,
	BLKADDR_NIX0		= 0x4ULL,
	BLKADDR_NIX1		= 0x5ULL,
	BLKADDR_NPC		= 0x6ULL,
	BLKADDR_SSO		= 0x7ULL,
	BLKADDR_SSOW		= 0x8ULL,
	BLKADDR_TIM		= 0x9ULL,
	BLKADDR_CPT0		= 0xaULL,
	BLKADDR_CPT1		= 0xbULL,
	BLKADDR_NDC_NIX0_RX	= 0xcULL,
	BLKADDR_NDC_NIX0_TX	= 0xdULL,
	BLKADDR_NDC_NPA0	= 0xeULL,
	BLKADDR_NDC_NIX1_RX	= 0x10ULL,
	BLKADDR_NDC_NIX1_TX	= 0x11ULL,
	BLK_COUNT		= 0x12ULL,
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

struct npa_aura_s {
	u64 pool_addr;			/* W0 */
#if defined(__BIG_ENDIAN_BITFIELD)	/* W1 */
	u64 avg_level             : 8;
	u64 reserved_118_119      : 2;
	u64 shift                 : 6;
	u64 aura_drop             : 8;
	u64 reserved_98_103       : 6;
	u64 bp_ena                : 2;
	u64 aura_drop_ena         : 1;
	u64 pool_drop_ena         : 1;
	u64 reserved_93           : 1;
	u64 avg_con               : 9;
	u64 pool_way_mask         : 16;
	u64 pool_caching          : 1;
	u64 reserved_65           : 2;
	u64 ena                   : 1;
#else
	u64 ena                   : 1;
	u64 reserved_65           : 2;
	u64 pool_caching          : 1;
	u64 pool_way_mask         : 16;
	u64 avg_con               : 9;
	u64 reserved_93           : 1;
	u64 pool_drop_ena         : 1;
	u64 aura_drop_ena         : 1;
	u64 bp_ena                : 2;
	u64 reserved_98_103       : 6;
	u64 aura_drop             : 8;
	u64 shift                 : 6;
	u64 reserved_118_119      : 2;
	u64 avg_level             : 8;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W2 */
	u64 reserved_189_191      : 3;
	u64 nix1_bpid             : 9;
	u64 reserved_177_179      : 3;
	u64 nix0_bpid             : 9;
	u64 reserved_164_167      : 4;
	u64 count                 : 36;
#else
	u64 count                 : 36;
	u64 reserved_164_167      : 4;
	u64 nix0_bpid             : 9;
	u64 reserved_177_179      : 3;
	u64 nix1_bpid             : 9;
	u64 reserved_189_191      : 3;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W3 */
	u64 reserved_252_255      : 4;
	u64 fc_hyst_bits          : 4;
	u64 fc_stype              : 2;
	u64 fc_up_crossing        : 1;
	u64 fc_ena                : 1;
	u64 reserved_240_243      : 4;
	u64 bp                    : 8;
	u64 reserved_228_231      : 4;
	u64 limit                 : 36;
#else
	u64 limit                 : 36;
	u64 reserved_228_231      : 4;
	u64 bp                    : 8;
	u64 reserved_240_243      : 4;
	u64 fc_ena                : 1;
	u64 fc_up_crossing        : 1;
	u64 fc_stype              : 2;
	u64 fc_hyst_bits          : 4;
	u64 reserved_252_255      : 4;
#endif
	u64 fc_addr;			/* W4 */
#if defined(__BIG_ENDIAN_BITFIELD)	/* W5 */
	u64 reserved_379_383      : 5;
	u64 err_qint_idx          : 7;
	u64 reserved_371          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_363          : 1;
	u64 thresh_up             : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_int            : 1;
	u64 err_int_ena           : 8;
	u64 err_int               : 8;
	u64 update_time           : 16;
	u64 pool_drop             : 8;
#else
	u64 pool_drop             : 8;
	u64 update_time           : 16;
	u64 err_int               : 8;
	u64 err_int_ena           : 8;
	u64 thresh_int            : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_up             : 1;
	u64 reserved_363          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_371          : 1;
	u64 err_qint_idx          : 7;
	u64 reserved_379_383      : 5;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W6 */
	u64 reserved_420_447      : 28;
	u64 thresh                : 36;
#else
	u64 thresh                : 36;
	u64 reserved_420_447      : 28;
#endif
	u64 reserved_448_511;		/* W7 */
};

struct npa_pool_s {
	u64 stack_base;			/* W0 */
#if defined(__BIG_ENDIAN_BITFIELD)	/* W1 */
	u64 reserved_115_127      : 13;
	u64 buf_size              : 11;
	u64 reserved_100_103      : 4;
	u64 buf_offset            : 12;
	u64 stack_way_mask        : 16;
	u64 reserved_70_71        : 3;
	u64 stack_caching         : 1;
	u64 reserved_66_67        : 2;
	u64 nat_align             : 1;
	u64 ena                   : 1;
#else
	u64 ena                   : 1;
	u64 nat_align             : 1;
	u64 reserved_66_67        : 2;
	u64 stack_caching         : 1;
	u64 reserved_70_71        : 3;
	u64 stack_way_mask        : 16;
	u64 buf_offset            : 12;
	u64 reserved_100_103      : 4;
	u64 buf_size              : 11;
	u64 reserved_115_127      : 13;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W2 */
	u64 stack_pages           : 32;
	u64 stack_max_pages       : 32;
#else
	u64 stack_max_pages       : 32;
	u64 stack_pages           : 32;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W3 */
	u64 reserved_240_255      : 16;
	u64 op_pc                 : 48;
#else
	u64 op_pc                 : 48;
	u64 reserved_240_255      : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W4 */
	u64 reserved_316_319      : 4;
	u64 update_time           : 16;
	u64 reserved_297_299      : 3;
	u64 fc_up_crossing        : 1;
	u64 fc_hyst_bits          : 4;
	u64 fc_stype              : 2;
	u64 fc_ena                : 1;
	u64 avg_con               : 9;
	u64 avg_level             : 8;
	u64 reserved_270_271      : 2;
	u64 shift                 : 6;
	u64 reserved_260_263      : 4;
	u64 stack_offset          : 4;
#else
	u64 stack_offset          : 4;
	u64 reserved_260_263      : 4;
	u64 shift                 : 6;
	u64 reserved_270_271      : 2;
	u64 avg_level             : 8;
	u64 avg_con               : 9;
	u64 fc_ena                : 1;
	u64 fc_stype              : 2;
	u64 fc_hyst_bits          : 4;
	u64 fc_up_crossing        : 1;
	u64 reserved_297_299      : 3;
	u64 update_time           : 16;
	u64 reserved_316_319      : 4;
#endif
	u64 fc_addr;			/* W5 */
	u64 ptr_start;			/* W6 */
	u64 ptr_end;			/* W7 */
#if defined(__BIG_ENDIAN_BITFIELD)	/* W8 */
	u64 reserved_571_575      : 5;
	u64 err_qint_idx          : 7;
	u64 reserved_563          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_555          : 1;
	u64 thresh_up             : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_int            : 1;
	u64 err_int_ena           : 8;
	u64 err_int               : 8;
	u64 reserved_512_535      : 24;
#else
	u64 reserved_512_535      : 24;
	u64 err_int               : 8;
	u64 err_int_ena           : 8;
	u64 thresh_int            : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_up             : 1;
	u64 reserved_555          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_563          : 1;
	u64 err_qint_idx          : 7;
	u64 reserved_571_575      : 5;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W9 */
	u64 reserved_612_639      : 28;
	u64 thresh                : 36;
#else
	u64 thresh                : 36;
	u64 reserved_612_639      : 28;
#endif
	u64 reserved_640_703;		/* W10 */
	u64 reserved_704_767;		/* W11 */
	u64 reserved_768_831;		/* W12 */
	u64 reserved_832_895;		/* W13 */
	u64 reserved_896_959;		/* W14 */
	u64 reserved_960_1023;		/* W15 */
};

/* NIX admin queue completion status */
enum nix_aq_comp {
	NIX_AQ_COMP_NOTDONE        = 0x0,
	NIX_AQ_COMP_GOOD           = 0x1,
	NIX_AQ_COMP_SWERR          = 0x2,
	NIX_AQ_COMP_CTX_POISON     = 0x3,
	NIX_AQ_COMP_CTX_FAULT      = 0x4,
	NIX_AQ_COMP_LOCKERR        = 0x5,
	NIX_AQ_COMP_SQB_ALLOC_FAIL = 0x6,
};

/* NIX admin queue context types */
enum nix_aq_ctype {
	NIX_AQ_CTYPE_RQ   = 0x0,
	NIX_AQ_CTYPE_SQ   = 0x1,
	NIX_AQ_CTYPE_CQ   = 0x2,
	NIX_AQ_CTYPE_MCE  = 0x3,
	NIX_AQ_CTYPE_RSS  = 0x4,
	NIX_AQ_CTYPE_DYNO = 0x5,
};

/* NIX admin queue instruction opcodes */
enum nix_aq_instop {
	NIX_AQ_INSTOP_NOP    = 0x0,
	NIX_AQ_INSTOP_INIT   = 0x1,
	NIX_AQ_INSTOP_WRITE  = 0x2,
	NIX_AQ_INSTOP_READ   = 0x3,
	NIX_AQ_INSTOP_LOCK   = 0x4,
	NIX_AQ_INSTOP_UNLOCK = 0x5,
};

/* NIX admin queue instruction structure */
struct nix_aq_inst_s {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 doneint		: 1;	/* W0 */
	u64 reserved_44_62	: 19;
	u64 cindex		: 20;
	u64 reserved_15_23	: 9;
	u64 lf			: 7;
	u64 ctype		: 4;
	u64 op			: 4;
#else
	u64 op			: 4;
	u64 ctype		: 4;
	u64 lf			: 7;
	u64 reserved_15_23	: 9;
	u64 cindex		: 20;
	u64 reserved_44_62	: 19;
	u64 doneint		: 1;
#endif
	u64 res_addr;			/* W1 */
};

/* NIX admin queue result structure */
struct nix_aq_res_s {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_17_63	: 47;	/* W0 */
	u64 doneint		: 1;
	u64 compcode		: 8;
	u64 ctype		: 4;
	u64 op			: 4;
#else
	u64 op			: 4;
	u64 ctype		: 4;
	u64 compcode		: 8;
	u64 doneint		: 1;
	u64 reserved_17_63	: 47;
#endif
	u64 reserved_64_127;		/* W1 */
};

/* NIX Completion queue context structure */
struct nix_cq_ctx_s {
	u64 base;
#if defined(__BIG_ENDIAN_BITFIELD)	/* W1 */
	u64 wrptr		: 20;
	u64 avg_con		: 9;
	u64 cint_idx		: 7;
	u64 cq_err		: 1;
	u64 qint_idx		: 7;
	u64 rsvd_81_83		: 3;
	u64 bpid		: 9;
	u64 rsvd_69_71		: 3;
	u64 bp_ena		: 1;
	u64 rsvd_64_67		: 4;
#else
	u64 rsvd_64_67		: 4;
	u64 bp_ena		: 1;
	u64 rsvd_69_71		: 3;
	u64 bpid		: 9;
	u64 rsvd_81_83		: 3;
	u64 qint_idx		: 7;
	u64 cq_err		: 1;
	u64 cint_idx		: 7;
	u64 avg_con		: 9;
	u64 wrptr		: 20;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W2 */
	u64 update_time		: 16;
	u64 avg_level		: 8;
	u64 head		: 20;
	u64 tail		: 20;
#else
	u64 tail		: 20;
	u64 head		: 20;
	u64 avg_level		: 8;
	u64 update_time		: 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W3 */
	u64 cq_err_int_ena	: 8;
	u64 cq_err_int		: 8;
	u64 qsize		: 4;
	u64 rsvd_233_235	: 3;
	u64 caching		: 1;
	u64 substream		: 20;
	u64 rsvd_210_211	: 2;
	u64 ena			: 1;
	u64 drop_ena		: 1;
	u64 drop		: 8;
	u64 bp			: 8;
#else
	u64 bp			: 8;
	u64 drop		: 8;
	u64 drop_ena		: 1;
	u64 ena			: 1;
	u64 rsvd_210_211	: 2;
	u64 substream		: 20;
	u64 caching		: 1;
	u64 rsvd_233_235	: 3;
	u64 qsize		: 4;
	u64 cq_err_int		: 8;
	u64 cq_err_int_ena	: 8;
#endif
};

/* NIX Receive queue context structure */
struct nix_rq_ctx_s {
#if defined(__BIG_ENDIAN_BITFIELD)  /* W0 */
	u64 wqe_aura      : 20;
	u64 substream     : 20;
	u64 cq            : 20;
	u64 ena_wqwd      : 1;
	u64 ipsech_ena    : 1;
	u64 sso_ena       : 1;
	u64 ena           : 1;
#else
	u64 ena           : 1;
	u64 sso_ena       : 1;
	u64 ipsech_ena    : 1;
	u64 ena_wqwd      : 1;
	u64 cq            : 20;
	u64 substream     : 20;
	u64 wqe_aura      : 20;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W1 */
	u64 rsvd_127_122  : 6;
	u64 lpb_drop_ena  : 1;
	u64 spb_drop_ena  : 1;
	u64 xqe_drop_ena  : 1;
	u64 wqe_caching   : 1;
	u64 pb_caching    : 2;
	u64 sso_tt        : 2;
	u64 sso_grp       : 10;
	u64 lpb_aura      : 20;
	u64 spb_aura      : 20;
#else
	u64 spb_aura      : 20;
	u64 lpb_aura      : 20;
	u64 sso_grp       : 10;
	u64 sso_tt        : 2;
	u64 pb_caching    : 2;
	u64 wqe_caching   : 1;
	u64 xqe_drop_ena  : 1;
	u64 spb_drop_ena  : 1;
	u64 lpb_drop_ena  : 1;
	u64 rsvd_127_122  : 6;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W2 */
	u64 xqe_hdr_split : 1;
	u64 xqe_imm_copy  : 1;
	u64 rsvd_189_184  : 6;
	u64 xqe_imm_size  : 6;
	u64 later_skip    : 6;
	u64 rsvd_171      : 1;
	u64 first_skip    : 7;
	u64 lpb_sizem1    : 12;
	u64 spb_ena       : 1;
	u64 rsvd_150_148  : 3;
	u64 wqe_skip      : 2;
	u64 spb_sizem1    : 6;
	u64 rsvd_139_128  : 12;
#else
	u64 rsvd_139_128  : 12;
	u64 spb_sizem1    : 6;
	u64 wqe_skip      : 2;
	u64 rsvd_150_148  : 3;
	u64 spb_ena       : 1;
	u64 lpb_sizem1    : 12;
	u64 first_skip    : 7;
	u64 rsvd_171      : 1;
	u64 later_skip    : 6;
	u64 xqe_imm_size  : 6;
	u64 rsvd_189_184  : 6;
	u64 xqe_imm_copy  : 1;
	u64 xqe_hdr_split : 1;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W3 */
	u64 spb_pool_pass : 8;
	u64 spb_pool_drop : 8;
	u64 spb_aura_pass : 8;
	u64 spb_aura_drop : 8;
	u64 wqe_pool_pass : 8;
	u64 wqe_pool_drop : 8;
	u64 xqe_pass      : 8;
	u64 xqe_drop      : 8;
#else
	u64 xqe_drop      : 8;
	u64 xqe_pass      : 8;
	u64 wqe_pool_drop : 8;
	u64 wqe_pool_pass : 8;
	u64 spb_aura_drop : 8;
	u64 spb_aura_pass : 8;
	u64 spb_pool_drop : 8;
	u64 spb_pool_pass : 8;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W4 */
	u64 rsvd_319_315  : 5;
	u64 qint_idx      : 7;
	u64 rq_int_ena    : 8;
	u64 rq_int        : 8;
	u64 rsvd_291_288  : 4;
	u64 lpb_pool_pass : 8;
	u64 lpb_pool_drop : 8;
	u64 lpb_aura_pass : 8;
	u64 lpb_aura_drop : 8;
#else
	u64 lpb_aura_drop : 8;
	u64 lpb_aura_pass : 8;
	u64 lpb_pool_drop : 8;
	u64 lpb_pool_pass : 8;
	u64 rsvd_291_288  : 4;
	u64 rq_int        : 8;
	u64 rq_int_ena    : 8;
	u64 qint_idx      : 7;
	u64 rsvd_319_315  : 5;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W5 */
	u64 rsvd_383_366  : 18;
	u64 flow_tagw     : 6;
	u64 bad_utag      : 8;
	u64 good_utag     : 8;
	u64 ltag          : 24;
#else
	u64 ltag          : 24;
	u64 good_utag     : 8;
	u64 bad_utag      : 8;
	u64 flow_tagw     : 6;
	u64 rsvd_383_366  : 18;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W6 */
	u64 rsvd_447_432  : 16;
	u64 octs          : 48;
#else
	u64 octs          : 48;
	u64 rsvd_447_432  : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W7 */
	u64 rsvd_511_496  : 16;
	u64 pkts          : 48;
#else
	u64 pkts          : 48;
	u64 rsvd_511_496  : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W8 */
	u64 rsvd_575_560  : 16;
	u64 drop_octs     : 48;
#else
	u64 drop_octs     : 48;
	u64 rsvd_575_560  : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W9 */
	u64 rsvd_639_624  : 16;
	u64 drop_pkts     : 48;
#else
	u64 drop_pkts     : 48;
	u64 rsvd_639_624  : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)	/* W10 */
	u64 rsvd_703_688  : 16;
	u64 re_pkts       : 48;
#else
	u64 re_pkts       : 48;
	u64 rsvd_703_688  : 16;
#endif
	u64 rsvd_767_704;		/* W11 */
	u64 rsvd_831_768;		/* W12 */
	u64 rsvd_895_832;		/* W13 */
	u64 rsvd_959_896;		/* W14 */
	u64 rsvd_1023_960;		/* W15 */
};

/* NIX sqe sizes */
enum nix_maxsqesz {
	NIX_MAXSQESZ_W16 = 0x0,
	NIX_MAXSQESZ_W8  = 0x1,
};

/* NIX SQB caching type */
enum nix_stype {
	NIX_STYPE_STF = 0x0,
	NIX_STYPE_STT = 0x1,
	NIX_STYPE_STP = 0x2,
};

/* NIX Send queue context structure */
struct nix_sq_ctx_s {
#if defined(__BIG_ENDIAN_BITFIELD)  /* W0 */
	u64 sqe_way_mask          : 16;
	u64 cq                    : 20;
	u64 sdp_mcast             : 1;
	u64 substream             : 20;
	u64 qint_idx              : 6;
	u64 ena                   : 1;
#else
	u64 ena                   : 1;
	u64 qint_idx              : 6;
	u64 substream             : 20;
	u64 sdp_mcast             : 1;
	u64 cq                    : 20;
	u64 sqe_way_mask          : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W1 */
	u64 sqb_count             : 16;
	u64 default_chan          : 12;
	u64 smq_rr_quantum        : 24;
	u64 sso_ena               : 1;
	u64 xoff                  : 1;
	u64 cq_ena                : 1;
	u64 smq                   : 9;
#else
	u64 smq                   : 9;
	u64 cq_ena                : 1;
	u64 xoff                  : 1;
	u64 sso_ena               : 1;
	u64 smq_rr_quantum        : 24;
	u64 default_chan          : 12;
	u64 sqb_count             : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W2 */
	u64 rsvd_191              : 1;
	u64 sqe_stype             : 2;
	u64 sq_int_ena            : 8;
	u64 sq_int                : 8;
	u64 sqb_aura              : 20;
	u64 smq_rr_count          : 25;
#else
	u64 smq_rr_count          : 25;
	u64 sqb_aura              : 20;
	u64 sq_int                : 8;
	u64 sq_int_ena            : 8;
	u64 sqe_stype             : 2;
	u64 rsvd_191              : 1;
#endif
#if defined(__BIG_ENDIAN_BITFIELD)  /* W3 */
	u64 rsvd_255_253          : 3;
	u64 smq_next_sq_vld       : 1;
	u64 smq_pend              : 1;
	u64 smenq_next_sqb_vld    : 1;
	u64 head_offset           : 6;
	u64 smenq_offset          : 6;
	u64 tail_offset           : 6;
	u64 smq_lso_segnum        : 8;
	u64 smq_next_sq           : 20;
	u64 mnq_dis               : 1;
	u64 lmt_dis               : 1;
	u64 cq_limit              : 8;
	u64 max_sqe_size          : 2;
#else
	u64 max_sqe_size          : 2;
	u64 cq_limit              : 8;
	u64 lmt_dis               : 1;
	u64 mnq_dis               : 1;
	u64 smq_next_sq           : 20;
	u64 smq_lso_segnum        : 8;
	u64 tail_offset           : 6;
	u64 smenq_offset          : 6;
	u64 head_offset           : 6;
	u64 smenq_next_sqb_vld    : 1;
	u64 smq_pend              : 1;
	u64 smq_next_sq_vld       : 1;
	u64 rsvd_255_253          : 3;
#endif
	u64 next_sqb              : 64;/* W4 */
	u64 tail_sqb              : 64;/* W5 */
	u64 smenq_sqb             : 64;/* W6 */
	u64 smenq_next_sqb        : 64;/* W7 */
	u64 head_sqb              : 64;/* W8 */
#if defined(__BIG_ENDIAN_BITFIELD)  /* W9 */
	u64 rsvd_639_630          : 10;
	u64 vfi_lso_vld           : 1;
	u64 vfi_lso_vlan1_ins_ena : 1;
	u64 vfi_lso_vlan0_ins_ena : 1;
	u64 vfi_lso_mps           : 14;
	u64 vfi_lso_sb            : 8;
	u64 vfi_lso_sizem1        : 3;
	u64 vfi_lso_total         : 18;
	u64 rsvd_583_576          : 8;
#else
	u64 rsvd_583_576          : 8;
	u64 vfi_lso_total         : 18;
	u64 vfi_lso_sizem1        : 3;
	u64 vfi_lso_sb            : 8;
	u64 vfi_lso_mps           : 14;
	u64 vfi_lso_vlan0_ins_ena : 1;
	u64 vfi_lso_vlan1_ins_ena : 1;
	u64 vfi_lso_vld           : 1;
	u64 rsvd_639_630          : 10;
#endif
#if defined(__BIG_ENDIAN_BITFIELD) /* W10 */
	u64 rsvd_703_658          : 46;
	u64 scm_lso_rem           : 18;
#else
	u64 scm_lso_rem           : 18;
	u64 rsvd_703_658          : 46;
#endif
#if defined(__BIG_ENDIAN_BITFIELD) /* W11 */
	u64 rsvd_767_752          : 16;
	u64 octs                  : 48;
#else
	u64 octs                  : 48;
	u64 rsvd_767_752          : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD) /* W12 */
	u64 rsvd_831_816          : 16;
	u64 pkts                  : 48;
#else
	u64 pkts                  : 48;
	u64 rsvd_831_816          : 16;
#endif
	u64 rsvd_895_832          : 64;/* W13 */
#if defined(__BIG_ENDIAN_BITFIELD) /* W14 */
	u64 rsvd_959_944          : 16;
	u64 dropped_octs          : 48;
#else
	u64 dropped_octs          : 48;
	u64 rsvd_959_944          : 16;
#endif
#if defined(__BIG_ENDIAN_BITFIELD) /* W15 */
	u64 rsvd_1023_1008        : 16;
	u64 dropped_pkts          : 48;
#else
	u64 dropped_pkts          : 48;
	u64 rsvd_1023_1008        : 16;
#endif
};

/* NIX Receive side scaling entry structure*/
struct nix_rsse_s {
#if defined(__BIG_ENDIAN_BITFIELD)
	uint32_t reserved_20_31		: 12;
	uint32_t rq			: 20;
#else
	uint32_t rq			: 20;
	uint32_t reserved_20_31		: 12;

#endif
};

/* NIX receive multicast/mirror entry structure */
struct nix_rx_mce_s {
#if defined(__BIG_ENDIAN_BITFIELD)  /* W0 */
	uint64_t next       : 16;
	uint64_t pf_func    : 16;
	uint64_t rsvd_31_24 : 8;
	uint64_t index      : 20;
	uint64_t eol        : 1;
	uint64_t rsvd_2     : 1;
	uint64_t op         : 2;
#else
	uint64_t op         : 2;
	uint64_t rsvd_2     : 1;
	uint64_t eol        : 1;
	uint64_t index      : 20;
	uint64_t rsvd_31_24 : 8;
	uint64_t pf_func    : 16;
	uint64_t next       : 16;
#endif
};

enum nix_lsoalg {
	NIX_LSOALG_NOP,
	NIX_LSOALG_ADD_SEGNUM,
	NIX_LSOALG_ADD_PAYLEN,
	NIX_LSOALG_ADD_OFFSET,
	NIX_LSOALG_TCP_FLAGS,
};

enum nix_txlayer {
	NIX_TXLAYER_OL3,
	NIX_TXLAYER_OL4,
	NIX_TXLAYER_IL3,
	NIX_TXLAYER_IL4,
};

struct nix_lso_format {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_19_63		: 45;
	u64 alg			: 3;
	u64 rsvd_14_15		: 2;
	u64 sizem1		: 2;
	u64 rsvd_10_11		: 2;
	u64 layer		: 2;
	u64 offset		: 8;
#else
	u64 offset		: 8;
	u64 layer		: 2;
	u64 rsvd_10_11		: 2;
	u64 sizem1		: 2;
	u64 rsvd_14_15		: 2;
	u64 alg			: 3;
	u64 rsvd_19_63		: 45;
#endif
};

struct nix_rx_flowkey_alg {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 reserved_35_63	:29;
	u64 ltype_match		:4;
	u64 ltype_mask		:4;
	u64 sel_chan		:1;
	u64 ena			:1;
	u64 reserved_24_24	:1;
	u64 lid			:3;
	u64 bytesm1		:5;
	u64 hdr_offset		:8;
	u64 fn_mask		:1;
	u64 ln_mask		:1;
	u64 key_offset		:6;
#else
	u64 key_offset		:6;
	u64 ln_mask		:1;
	u64 fn_mask		:1;
	u64 hdr_offset		:8;
	u64 bytesm1		:5;
	u64 lid			:3;
	u64 reserved_24_24	:1;
	u64 ena			:1;
	u64 sel_chan		:1;
	u64 ltype_mask		:4;
	u64 ltype_match		:4;
	u64 reserved_35_63	:29;
#endif
};

/* NIX VTAG size */
enum nix_vtag_size {
	VTAGSIZE_T4   = 0x0,
	VTAGSIZE_T8   = 0x1,
};

enum nix_tx_vtag_op {
	NOP		= 0x0,
	VTAG_INSERT	= 0x1,
	VTAG_REPLACE	= 0x2,
};

/* NIX RX VTAG actions */
#define VTAG_STRIP	BIT_ULL(4)
#define VTAG_CAPTURE	BIT_ULL(5)

#endif /* RVU_STRUCT_H */
