/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef STRUCT_H
#define STRUCT_H

/*
 * CN20k RVU PF MBOX Interrupt Vector Enumeration
 *
 * Vectors 0 - 3 are compatible with pre cn20k and hence
 * existing macros are being reused.
 */
enum rvu_mbox_pf_int_vec_e {
	RVU_MBOX_PF_INT_VEC_VFPF_MBOX0	= 0x4,
	RVU_MBOX_PF_INT_VEC_VFPF_MBOX1	= 0x5,
	RVU_MBOX_PF_INT_VEC_VFPF1_MBOX0	= 0x6,
	RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1	= 0x7,
	RVU_MBOX_PF_INT_VEC_AFPF_MBOX	= 0x8,
	RVU_MBOX_PF_INT_VEC_CNT		= 0x9,
};

/* RVU Admin function Interrupt Vector Enumeration */
enum rvu_af_cn20k_int_vec_e {
	RVU_AF_CN20K_INT_VEC_POISON		= 0x0,
	RVU_AF_CN20K_INT_VEC_PFFLR0		= 0x1,
	RVU_AF_CN20K_INT_VEC_PFFLR1		= 0x2,
	RVU_AF_CN20K_INT_VEC_PFME0		= 0x3,
	RVU_AF_CN20K_INT_VEC_PFME1		= 0x4,
	RVU_AF_CN20K_INT_VEC_GEN		= 0x5,
	RVU_AF_CN20K_INT_VEC_PFAF_MBOX0		= 0x6,
	RVU_AF_CN20K_INT_VEC_PFAF_MBOX1		= 0x7,
	RVU_AF_CN20K_INT_VEC_PFAF1_MBOX0	= 0x8,
	RVU_AF_CN20K_INT_VEC_PFAF1_MBOX1	= 0x9,
	RVU_AF_CN20K_INT_VEC_CNT		= 0xa,
};
#endif
