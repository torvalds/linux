/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARIA Cipher 16-way parallel algorithm (AVX)
 *
 * Copyright (c) 2022 Taehee Yoo <ap420073@gmail.com>
 *
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/asm-offsets.h>
#include <asm/frame.h>

/* register macros */
#define CTX %rdi


#define BV8(a0, a1, a2, a3, a4, a5, a6, a7)		\
	( (((a0) & 1) << 0) |				\
	  (((a1) & 1) << 1) |				\
	  (((a2) & 1) << 2) |				\
	  (((a3) & 1) << 3) |				\
	  (((a4) & 1) << 4) |				\
	  (((a5) & 1) << 5) |				\
	  (((a6) & 1) << 6) |				\
	  (((a7) & 1) << 7) )

#define BM8X8(l0, l1, l2, l3, l4, l5, l6, l7)		\
	( ((l7) << (0 * 8)) |				\
	  ((l6) << (1 * 8)) |				\
	  ((l5) << (2 * 8)) |				\
	  ((l4) << (3 * 8)) |				\
	  ((l3) << (4 * 8)) |				\
	  ((l2) << (5 * 8)) |				\
	  ((l1) << (6 * 8)) |				\
	  ((l0) << (7 * 8)) )

#define inc_le128(x, minus_one, tmp)			\
	vpcmpeqq minus_one, x, tmp;			\
	vpsubq minus_one, x, x;				\
	vpslldq $8, tmp, tmp;				\
	vpsubq tmp, x, x;

#define filter_8bit(x, lo_t, hi_t, mask4bit, tmp0)	\
	vpand x, mask4bit, tmp0;			\
	vpandn x, mask4bit, x;				\
	vpsrld $4, x, x;				\
							\
	vpshufb tmp0, lo_t, tmp0;			\
	vpshufb x, hi_t, x;				\
	vpxor tmp0, x, x;

#define transpose_4x4(x0, x1, x2, x3, t1, t2)		\
	vpunpckhdq x1, x0, t2;				\
	vpunpckldq x1, x0, x0;				\
							\
	vpunpckldq x3, x2, t1;				\
	vpunpckhdq x3, x2, x2;				\
							\
	vpunpckhqdq t1, x0, x1;				\
	vpunpcklqdq t1, x0, x0;				\
							\
	vpunpckhqdq x2, t2, x3;				\
	vpunpcklqdq x2, t2, x2;

#define byteslice_16x16b(a0, b0, c0, d0,		\
			 a1, b1, c1, d1,		\
			 a2, b2, c2, d2,		\
			 a3, b3, c3, d3,		\
			 st0, st1)			\
	vmovdqu d2, st0;				\
	vmovdqu d3, st1;				\
	transpose_4x4(a0, a1, a2, a3, d2, d3);		\
	transpose_4x4(b0, b1, b2, b3, d2, d3);		\
	vmovdqu st0, d2;				\
	vmovdqu st1, d3;				\
							\
	vmovdqu a0, st0;				\
	vmovdqu a1, st1;				\
	transpose_4x4(c0, c1, c2, c3, a0, a1);		\
	transpose_4x4(d0, d1, d2, d3, a0, a1);		\
							\
	vmovdqu .Lshufb_16x16b, a0;			\
	vmovdqu st1, a1;				\
	vpshufb a0, a2, a2;				\
	vpshufb a0, a3, a3;				\
	vpshufb a0, b0, b0;				\
	vpshufb a0, b1, b1;				\
	vpshufb a0, b2, b2;				\
	vpshufb a0, b3, b3;				\
	vpshufb a0, a1, a1;				\
	vpshufb a0, c0, c0;				\
	vpshufb a0, c1, c1;				\
	vpshufb a0, c2, c2;				\
	vpshufb a0, c3, c3;				\
	vpshufb a0, d0, d0;				\
	vpshufb a0, d1, d1;				\
	vpshufb a0, d2, d2;				\
	vpshufb a0, d3, d3;				\
	vmovdqu d3, st1;				\
	vmovdqu st0, d3;				\
	vpshufb a0, d3, a0;				\
	vmovdqu d2, st0;				\
							\
	transpose_4x4(a0, b0, c0, d0, d2, d3);		\
	transpose_4x4(a1, b1, c1, d1, d2, d3);		\
	vmovdqu st0, d2;				\
	vmovdqu st1, d3;				\
							\
	vmovdqu b0, st0;				\
	vmovdqu b1, st1;				\
	transpose_4x4(a2, b2, c2, d2, b0, b1);		\
	transpose_4x4(a3, b3, c3, d3, b0, b1);		\
	vmovdqu st0, b0;				\
	vmovdqu st1, b1;				\
	/* does not adjust output bytes inside vectors */

#define debyteslice_16x16b(a0, b0, c0, d0,		\
			   a1, b1, c1, d1,		\
			   a2, b2, c2, d2,		\
			   a3, b3, c3, d3,		\
			   st0, st1)			\
	vmovdqu d2, st0;				\
	vmovdqu d3, st1;				\
	transpose_4x4(a0, a1, a2, a3, d2, d3);		\
	transpose_4x4(b0, b1, b2, b3, d2, d3);		\
	vmovdqu st0, d2;				\
	vmovdqu st1, d3;				\
							\
	vmovdqu a0, st0;				\
	vmovdqu a1, st1;				\
	transpose_4x4(c0, c1, c2, c3, a0, a1);		\
	transpose_4x4(d0, d1, d2, d3, a0, a1);		\
							\
	vmovdqu .Lshufb_16x16b, a0;			\
	vmovdqu st1, a1;				\
	vpshufb a0, a2, a2;				\
	vpshufb a0, a3, a3;				\
	vpshufb a0, b0, b0;				\
	vpshufb a0, b1, b1;				\
	vpshufb a0, b2, b2;				\
	vpshufb a0, b3, b3;				\
	vpshufb a0, a1, a1;				\
	vpshufb a0, c0, c0;				\
	vpshufb a0, c1, c1;				\
	vpshufb a0, c2, c2;				\
	vpshufb a0, c3, c3;				\
	vpshufb a0, d0, d0;				\
	vpshufb a0, d1, d1;				\
	vpshufb a0, d2, d2;				\
	vpshufb a0, d3, d3;				\
	vmovdqu d3, st1;				\
	vmovdqu st0, d3;				\
	vpshufb a0, d3, a0;				\
	vmovdqu d2, st0;				\
							\
	transpose_4x4(c0, d0, a0, b0, d2, d3);		\
	transpose_4x4(c1, d1, a1, b1, d2, d3);		\
	vmovdqu st0, d2;				\
	vmovdqu st1, d3;				\
							\
	vmovdqu b0, st0;				\
	vmovdqu b1, st1;				\
	transpose_4x4(c2, d2, a2, b2, b0, b1);		\
	transpose_4x4(c3, d3, a3, b3, b0, b1);		\
	vmovdqu st0, b0;				\
	vmovdqu st1, b1;				\
	/* does not adjust output bytes inside vectors */

/* load blocks to registers and apply pre-whitening */
#define inpack16_pre(x0, x1, x2, x3,			\
		     x4, x5, x6, x7,			\
		     y0, y1, y2, y3,			\
		     y4, y5, y6, y7,			\
		     rio)				\
	vmovdqu (0 * 16)(rio), x0;			\
	vmovdqu (1 * 16)(rio), x1;			\
	vmovdqu (2 * 16)(rio), x2;			\
	vmovdqu (3 * 16)(rio), x3;			\
	vmovdqu (4 * 16)(rio), x4;			\
	vmovdqu (5 * 16)(rio), x5;			\
	vmovdqu (6 * 16)(rio), x6;			\
	vmovdqu (7 * 16)(rio), x7;			\
	vmovdqu (8 * 16)(rio), y0;			\
	vmovdqu (9 * 16)(rio), y1;			\
	vmovdqu (10 * 16)(rio), y2;			\
	vmovdqu (11 * 16)(rio), y3;			\
	vmovdqu (12 * 16)(rio), y4;			\
	vmovdqu (13 * 16)(rio), y5;			\
	vmovdqu (14 * 16)(rio), y6;			\
	vmovdqu (15 * 16)(rio), y7;

/* byteslice pre-whitened blocks and store to temporary memory */
#define inpack16_post(x0, x1, x2, x3,			\
		      x4, x5, x6, x7,			\
		      y0, y1, y2, y3,			\
		      y4, y5, y6, y7,			\
		      mem_ab, mem_cd)			\
	byteslice_16x16b(x0, x1, x2, x3,		\
			 x4, x5, x6, x7,		\
			 y0, y1, y2, y3,		\
			 y4, y5, y6, y7,		\
			 (mem_ab), (mem_cd));		\
							\
	vmovdqu x0, 0 * 16(mem_ab);			\
	vmovdqu x1, 1 * 16(mem_ab);			\
	vmovdqu x2, 2 * 16(mem_ab);			\
	vmovdqu x3, 3 * 16(mem_ab);			\
	vmovdqu x4, 4 * 16(mem_ab);			\
	vmovdqu x5, 5 * 16(mem_ab);			\
	vmovdqu x6, 6 * 16(mem_ab);			\
	vmovdqu x7, 7 * 16(mem_ab);			\
	vmovdqu y0, 0 * 16(mem_cd);			\
	vmovdqu y1, 1 * 16(mem_cd);			\
	vmovdqu y2, 2 * 16(mem_cd);			\
	vmovdqu y3, 3 * 16(mem_cd);			\
	vmovdqu y4, 4 * 16(mem_cd);			\
	vmovdqu y5, 5 * 16(mem_cd);			\
	vmovdqu y6, 6 * 16(mem_cd);			\
	vmovdqu y7, 7 * 16(mem_cd);

#define write_output(x0, x1, x2, x3,			\
		     x4, x5, x6, x7,			\
		     y0, y1, y2, y3,			\
		     y4, y5, y6, y7,			\
		     mem)				\
	vmovdqu x0, 0 * 16(mem);			\
	vmovdqu x1, 1 * 16(mem);			\
	vmovdqu x2, 2 * 16(mem);			\
	vmovdqu x3, 3 * 16(mem);			\
	vmovdqu x4, 4 * 16(mem);			\
	vmovdqu x5, 5 * 16(mem);			\
	vmovdqu x6, 6 * 16(mem);			\
	vmovdqu x7, 7 * 16(mem);			\
	vmovdqu y0, 8 * 16(mem);			\
	vmovdqu y1, 9 * 16(mem);			\
	vmovdqu y2, 10 * 16(mem);			\
	vmovdqu y3, 11 * 16(mem);			\
	vmovdqu y4, 12 * 16(mem);			\
	vmovdqu y5, 13 * 16(mem);			\
	vmovdqu y6, 14 * 16(mem);			\
	vmovdqu y7, 15 * 16(mem);			\

#define aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, idx)		\
	vmovdqu x0, ((idx + 0) * 16)(mem_tmp);		\
	vmovdqu x1, ((idx + 1) * 16)(mem_tmp);		\
	vmovdqu x2, ((idx + 2) * 16)(mem_tmp);		\
	vmovdqu x3, ((idx + 3) * 16)(mem_tmp);		\
	vmovdqu x4, ((idx + 4) * 16)(mem_tmp);		\
	vmovdqu x5, ((idx + 5) * 16)(mem_tmp);		\
	vmovdqu x6, ((idx + 6) * 16)(mem_tmp);		\
	vmovdqu x7, ((idx + 7) * 16)(mem_tmp);

#define aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, idx)		\
	vmovdqu ((idx + 0) * 16)(mem_tmp), x0;		\
	vmovdqu ((idx + 1) * 16)(mem_tmp), x1;		\
	vmovdqu ((idx + 2) * 16)(mem_tmp), x2;		\
	vmovdqu ((idx + 3) * 16)(mem_tmp), x3;		\
	vmovdqu ((idx + 4) * 16)(mem_tmp), x4;		\
	vmovdqu ((idx + 5) * 16)(mem_tmp), x5;		\
	vmovdqu ((idx + 6) * 16)(mem_tmp), x6;		\
	vmovdqu ((idx + 7) * 16)(mem_tmp), x7;

#define aria_ark_8way(x0, x1, x2, x3,			\
		      x4, x5, x6, x7,			\
		      t0, t1, t2, rk,			\
		      idx, round)			\
	/* AddRoundKey */                               \
	vbroadcastss ((round * 16) + idx + 0)(rk), t0;	\
	vpsrld $24, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x0, x0;				\
	vpsrld $16, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x1, x1;				\
	vpsrld $8, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x2, x2;				\
	vpshufb t1, t0, t2;				\
	vpxor t2, x3, x3;				\
	vbroadcastss ((round * 16) + idx + 4)(rk), t0;	\
	vpsrld $24, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x4, x4;				\
	vpsrld $16, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x5, x5;				\
	vpsrld $8, t0, t2;				\
	vpshufb t1, t2, t2;				\
	vpxor t2, x6, x6;				\
	vpshufb t1, t0, t2;				\
	vpxor t2, x7, x7;

#ifdef CONFIG_AS_GFNI
#define aria_sbox_8way_gfni(x0, x1, x2, x3,		\
			    x4, x5, x6, x7,		\
			    t0, t1, t2, t3,		\
			    t4, t5, t6, t7)		\
	vmovdqa .Ltf_s2_bitmatrix, t0;			\
	vmovdqa .Ltf_inv_bitmatrix, t1;			\
	vmovdqa .Ltf_id_bitmatrix, t2;			\
	vmovdqa .Ltf_aff_bitmatrix, t3;			\
	vmovdqa .Ltf_x2_bitmatrix, t4;			\
	vgf2p8affineinvqb $(tf_s2_const), t0, x1, x1;	\
	vgf2p8affineinvqb $(tf_s2_const), t0, x5, x5;	\
	vgf2p8affineqb $(tf_inv_const), t1, x2, x2;	\
	vgf2p8affineqb $(tf_inv_const), t1, x6, x6;	\
	vgf2p8affineinvqb $0, t2, x2, x2;		\
	vgf2p8affineinvqb $0, t2, x6, x6;		\
	vgf2p8affineinvqb $(tf_aff_const), t3, x0, x0;	\
	vgf2p8affineinvqb $(tf_aff_const), t3, x4, x4;	\
	vgf2p8affineqb $(tf_x2_const), t4, x3, x3;	\
	vgf2p8affineqb $(tf_x2_const), t4, x7, x7;	\
	vgf2p8affineinvqb $0, t2, x3, x3;		\
	vgf2p8affineinvqb $0, t2, x7, x7

#endif /* CONFIG_AS_GFNI */

#define aria_sbox_8way(x0, x1, x2, x3,            	\
		       x4, x5, x6, x7,			\
		       t0, t1, t2, t3,			\
		       t4, t5, t6, t7)			\
	vmovdqa .Linv_shift_row, t0;			\
	vmovdqa .Lshift_row, t1;			\
	vbroadcastss .L0f0f0f0f, t6;			\
	vmovdqa .Ltf_lo__inv_aff__and__s2, t2;		\
	vmovdqa .Ltf_hi__inv_aff__and__s2, t3;		\
	vmovdqa .Ltf_lo__x2__and__fwd_aff, t4;		\
	vmovdqa .Ltf_hi__x2__and__fwd_aff, t5;		\
							\
	vaesenclast t7, x0, x0;				\
	vaesenclast t7, x4, x4;				\
	vaesenclast t7, x1, x1;				\
	vaesenclast t7, x5, x5;				\
	vaesdeclast t7, x2, x2;				\
	vaesdeclast t7, x6, x6;				\
							\
	/* AES inverse shift rows */			\
	vpshufb t0, x0, x0;				\
	vpshufb t0, x4, x4;				\
	vpshufb t0, x1, x1;				\
	vpshufb t0, x5, x5;				\
	vpshufb t1, x3, x3;				\
	vpshufb t1, x7, x7;				\
	vpshufb t1, x2, x2;				\
	vpshufb t1, x6, x6;				\
							\
	/* affine transformation for S2 */		\
	filter_8bit(x1, t2, t3, t6, t0);		\
	/* affine transformation for S2 */		\
	filter_8bit(x5, t2, t3, t6, t0);		\
							\
	/* affine transformation for X2 */		\
	filter_8bit(x3, t4, t5, t6, t0);		\
	/* affine transformation for X2 */		\
	filter_8bit(x7, t4, t5, t6, t0);		\
	vaesdeclast t7, x3, x3;				\
	vaesdeclast t7, x7, x7;

#define aria_diff_m(x0, x1, x2, x3,			\
		    t0, t1, t2, t3)			\
	/* T = rotr32(X, 8); */				\
	/* X ^= T */					\
	vpxor x0, x3, t0;				\
	vpxor x1, x0, t1;				\
	vpxor x2, x1, t2;				\
	vpxor x3, x2, t3;				\
	/* X = T ^ rotr(X, 16); */			\
	vpxor t2, x0, x0;				\
	vpxor x1, t3, t3;				\
	vpxor t0, x2, x2;				\
	vpxor t1, x3, x1;				\
	vmovdqu t3, x3;

#define aria_diff_word(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       y0, y1, y2, y3,			\
		       y4, y5, y6, y7)			\
	/* t1 ^= t2; */					\
	vpxor y0, x4, x4;				\
	vpxor y1, x5, x5;				\
	vpxor y2, x6, x6;				\
	vpxor y3, x7, x7;				\
							\
	/* t2 ^= t3; */					\
	vpxor y4, y0, y0;				\
	vpxor y5, y1, y1;				\
	vpxor y6, y2, y2;				\
	vpxor y7, y3, y3;				\
							\
	/* t0 ^= t1; */					\
	vpxor x4, x0, x0;				\
	vpxor x5, x1, x1;				\
	vpxor x6, x2, x2;				\
	vpxor x7, x3, x3;				\
							\
	/* t3 ^= t1; */					\
	vpxor x4, y4, y4;				\
	vpxor x5, y5, y5;				\
	vpxor x6, y6, y6;				\
	vpxor x7, y7, y7;				\
							\
	/* t2 ^= t0; */					\
	vpxor x0, y0, y0;				\
	vpxor x1, y1, y1;				\
	vpxor x2, y2, y2;				\
	vpxor x3, y3, y3;				\
							\
	/* t1 ^= t2; */					\
	vpxor y0, x4, x4;				\
	vpxor y1, x5, x5;				\
	vpxor y2, x6, x6;				\
	vpxor y3, x7, x7;

#define aria_fe(x0, x1, x2, x3,				\
		x4, x5, x6, x7,				\
		y0, y1, y2, y3,				\
		y4, y5, y6, y7,				\
		mem_tmp, rk, round)			\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 0);		\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);		\
	aria_diff_word(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       y0, y1, y2, y3,			\
		       y4, y5, y6, y7);			\
	/* aria_diff_byte() 				\
	 * T3 = ABCD -> BADC 				\
	 * T3 = y4, y5, y6, y7 -> y5, y4, y7, y6 	\
	 * T0 = ABCD -> CDAB 				\
	 * T0 = x0, x1, x2, x3 -> x2, x3, x0, x1 	\
	 * T1 = ABCD -> DCBA 				\
	 * T1 = x4, x5, x6, x7 -> x7, x6, x5, x4	\
	 */						\
	aria_diff_word(x2, x3, x0, x1,			\
		       x7, x6, x5, x4,			\
		       y0, y1, y2, y3,			\
		       y5, y4, y7, y6);			\
	aria_store_state_8way(x3, x2, x1, x0,		\
			      x6, x7, x4, x5,		\
			      mem_tmp, 0);

#define aria_fo(x0, x1, x2, x3,				\
		x4, x5, x6, x7,				\
		y0, y1, y2, y3,				\
		y4, y5, y6, y7,				\
		mem_tmp, rk, round)			\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 0);		\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);		\
	aria_diff_word(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       y0, y1, y2, y3,			\
		       y4, y5, y6, y7);			\
	/* aria_diff_byte() 				\
	 * T1 = ABCD -> BADC 				\
	 * T1 = x4, x5, x6, x7 -> x5, x4, x7, x6	\
	 * T2 = ABCD -> CDAB 				\
	 * T2 = y0, y1, y2, y3, -> y2, y3, y0, y1 	\
	 * T3 = ABCD -> DCBA 				\
	 * T3 = y4, y5, y6, y7 -> y7, y6, y5, y4 	\
	 */						\
	aria_diff_word(x0, x1, x2, x3,			\
		       x5, x4, x7, x6,			\
		       y2, y3, y0, y1,			\
		       y7, y6, y5, y4);			\
	aria_store_state_8way(x3, x2, x1, x0,		\
			      x6, x7, x4, x5,		\
			      mem_tmp, 0);

#define aria_ff(x0, x1, x2, x3,				\
		x4, x5, x6, x7,				\
		y0, y1, y2, y3,				\
		y4, y5, y6, y7,				\
		mem_tmp, rk, round, last_round)		\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, last_round);	\
							\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, last_round);	\
							\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);

#ifdef CONFIG_AS_GFNI
#define aria_fe_gfni(x0, x1, x2, x3,			\
		     x4, x5, x6, x7,			\
		     y0, y1, y2, y3,			\
		     y4, y5, y6, y7,			\
		     mem_tmp, rk, round)		\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1, 		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1, 		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 0);		\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);		\
	aria_diff_word(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       y0, y1, y2, y3,			\
		       y4, y5, y6, y7);			\
	/* aria_diff_byte() 				\
	 * T3 = ABCD -> BADC 				\
	 * T3 = y4, y5, y6, y7 -> y5, y4, y7, y6 	\
	 * T0 = ABCD -> CDAB 				\
	 * T0 = x0, x1, x2, x3 -> x2, x3, x0, x1 	\
	 * T1 = ABCD -> DCBA 				\
	 * T1 = x4, x5, x6, x7 -> x7, x6, x5, x4	\
	 */						\
	aria_diff_word(x2, x3, x0, x1,			\
		       x7, x6, x5, x4,			\
		       y0, y1, y2, y3,			\
		       y5, y4, y7, y6);			\
	aria_store_state_8way(x3, x2, x1, x0,		\
			      x6, x7, x4, x5,		\
			      mem_tmp, 0);

#define aria_fo_gfni(x0, x1, x2, x3,			\
		     x4, x5, x6, x7,			\
		     y0, y1, y2, y3,			\
		     y4, y5, y6, y7,			\
		     mem_tmp, rk, round)		\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way_gfni(x0, x1, x2, x3, 		\
			    x4, x5, x6, x7,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way_gfni(x0, x1, x2, x3, 		\
			    x4, x5, x6, x7,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_diff_m(x0, x1, x2, x3, y0, y1, y2, y3);	\
	aria_diff_m(x4, x5, x6, x7, y0, y1, y2, y3);	\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 0);		\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);		\
	aria_diff_word(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       y0, y1, y2, y3,			\
		       y4, y5, y6, y7);			\
	/* aria_diff_byte() 				\
	 * T1 = ABCD -> BADC 				\
	 * T1 = x4, x5, x6, x7 -> x5, x4, x7, x6	\
	 * T2 = ABCD -> CDAB 				\
	 * T2 = y0, y1, y2, y3, -> y2, y3, y0, y1 	\
	 * T3 = ABCD -> DCBA 				\
	 * T3 = y4, y5, y6, y7 -> y7, y6, y5, y4 	\
	 */						\
	aria_diff_word(x0, x1, x2, x3,			\
		       x5, x4, x7, x6,			\
		       y2, y3, y0, y1,			\
		       y7, y6, y5, y4);			\
	aria_store_state_8way(x3, x2, x1, x0,		\
			      x6, x7, x4, x5,		\
			      mem_tmp, 0);

#define aria_ff_gfni(x0, x1, x2, x3,			\
		x4, x5, x6, x7,				\
		y0, y1, y2, y3,				\
		y4, y5, y6, y7,				\
		mem_tmp, rk, round, last_round)		\
	vpxor y7, y7, y7;				\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, round);	\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1, 		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 8, last_round);	\
							\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, round);	\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1, 		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3, 		\
			    y4, y5, y6, y7);		\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, y7, y2, rk, 0, last_round);	\
							\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);

#endif /* CONFIG_AS_GFNI */

/* NB: section is mergeable, all elements must be aligned 16-byte blocks */
.section	.rodata.cst16, "aM", @progbits, 16
.align 16

#define SHUFB_BYTES(idx) \
	0 + (idx), 4 + (idx), 8 + (idx), 12 + (idx)

.Lshufb_16x16b:
	.byte SHUFB_BYTES(0), SHUFB_BYTES(1), SHUFB_BYTES(2), SHUFB_BYTES(3);
/* For isolating SubBytes from AESENCLAST, inverse shift row */
.Linv_shift_row:
	.byte 0x00, 0x0d, 0x0a, 0x07, 0x04, 0x01, 0x0e, 0x0b
	.byte 0x08, 0x05, 0x02, 0x0f, 0x0c, 0x09, 0x06, 0x03
.Lshift_row:
	.byte 0x00, 0x05, 0x0a, 0x0f, 0x04, 0x09, 0x0e, 0x03
	.byte 0x08, 0x0d, 0x02, 0x07, 0x0c, 0x01, 0x06, 0x0b
/* For CTR-mode IV byteswap */
.Lbswap128_mask:
	.byte 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08
	.byte 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00

/* AES inverse affine and S2 combined:
 *      1 1 0 0 0 0 0 1     x0     0
 *      0 1 0 0 1 0 0 0     x1     0
 *      1 1 0 0 1 1 1 1     x2     0
 *      0 1 1 0 1 0 0 1     x3     1
 *      0 1 0 0 1 1 0 0  *  x4  +  0
 *      0 1 0 1 1 0 0 0     x5     0
 *      0 0 0 0 0 1 0 1     x6     0
 *      1 1 1 0 0 1 1 1     x7     1
 */
.Ltf_lo__inv_aff__and__s2:
	.octa 0x92172DA81A9FA520B2370D883ABF8500
.Ltf_hi__inv_aff__and__s2:
	.octa 0x2B15FFC1AF917B45E6D8320C625CB688

/* X2 and AES forward affine combined:
 *      1 0 1 1 0 0 0 1     x0     0
 *      0 1 1 1 1 0 1 1     x1     0
 *      0 0 0 1 1 0 1 0     x2     1
 *      0 1 0 0 0 1 0 0     x3     0
 *      0 0 1 1 1 0 1 1  *  x4  +  0
 *      0 1 0 0 1 0 0 0     x5     0
 *      1 1 0 1 0 0 1 1     x6     0
 *      0 1 0 0 1 0 1 0     x7     0
 */
.Ltf_lo__x2__and__fwd_aff:
	.octa 0xEFAE0544FCBD1657B8F95213ABEA4100
.Ltf_hi__x2__and__fwd_aff:
	.octa 0x3F893781E95FE1576CDA64D2BA0CB204

#ifdef CONFIG_AS_GFNI
.section	.rodata.cst8, "aM", @progbits, 8
.align 8
/* AES affine: */
#define tf_aff_const BV8(1, 1, 0, 0, 0, 1, 1, 0)
.Ltf_aff_bitmatrix:
	.quad BM8X8(BV8(1, 0, 0, 0, 1, 1, 1, 1),
		    BV8(1, 1, 0, 0, 0, 1, 1, 1),
		    BV8(1, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 1, 0, 0, 0, 1),
		    BV8(1, 1, 1, 1, 1, 0, 0, 0),
		    BV8(0, 1, 1, 1, 1, 1, 0, 0),
		    BV8(0, 0, 1, 1, 1, 1, 1, 0),
		    BV8(0, 0, 0, 1, 1, 1, 1, 1))
	.quad BM8X8(BV8(1, 0, 0, 0, 1, 1, 1, 1),
		    BV8(1, 1, 0, 0, 0, 1, 1, 1),
		    BV8(1, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 1, 0, 0, 0, 1),
		    BV8(1, 1, 1, 1, 1, 0, 0, 0),
		    BV8(0, 1, 1, 1, 1, 1, 0, 0),
		    BV8(0, 0, 1, 1, 1, 1, 1, 0),
		    BV8(0, 0, 0, 1, 1, 1, 1, 1))

/* AES inverse affine: */
#define tf_inv_const BV8(1, 0, 1, 0, 0, 0, 0, 0)
.Ltf_inv_bitmatrix:
	.quad BM8X8(BV8(0, 0, 1, 0, 0, 1, 0, 1),
		    BV8(1, 0, 0, 1, 0, 0, 1, 0),
		    BV8(0, 1, 0, 0, 1, 0, 0, 1),
		    BV8(1, 0, 1, 0, 0, 1, 0, 0),
		    BV8(0, 1, 0, 1, 0, 0, 1, 0),
		    BV8(0, 0, 1, 0, 1, 0, 0, 1),
		    BV8(1, 0, 0, 1, 0, 1, 0, 0),
		    BV8(0, 1, 0, 0, 1, 0, 1, 0))
	.quad BM8X8(BV8(0, 0, 1, 0, 0, 1, 0, 1),
		    BV8(1, 0, 0, 1, 0, 0, 1, 0),
		    BV8(0, 1, 0, 0, 1, 0, 0, 1),
		    BV8(1, 0, 1, 0, 0, 1, 0, 0),
		    BV8(0, 1, 0, 1, 0, 0, 1, 0),
		    BV8(0, 0, 1, 0, 1, 0, 0, 1),
		    BV8(1, 0, 0, 1, 0, 1, 0, 0),
		    BV8(0, 1, 0, 0, 1, 0, 1, 0))

/* S2: */
#define tf_s2_const BV8(0, 1, 0, 0, 0, 1, 1, 1)
.Ltf_s2_bitmatrix:
	.quad BM8X8(BV8(0, 1, 0, 1, 0, 1, 1, 1),
		    BV8(0, 0, 1, 1, 1, 1, 1, 1),
		    BV8(1, 1, 1, 0, 1, 1, 0, 1),
		    BV8(1, 1, 0, 0, 0, 0, 1, 1),
		    BV8(0, 1, 0, 0, 0, 0, 1, 1),
		    BV8(1, 1, 0, 0, 1, 1, 1, 0),
		    BV8(0, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 1, 0, 1, 1, 0))
	.quad BM8X8(BV8(0, 1, 0, 1, 0, 1, 1, 1),
		    BV8(0, 0, 1, 1, 1, 1, 1, 1),
		    BV8(1, 1, 1, 0, 1, 1, 0, 1),
		    BV8(1, 1, 0, 0, 0, 0, 1, 1),
		    BV8(0, 1, 0, 0, 0, 0, 1, 1),
		    BV8(1, 1, 0, 0, 1, 1, 1, 0),
		    BV8(0, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 1, 0, 1, 1, 0))

/* X2: */
#define tf_x2_const BV8(0, 0, 1, 1, 0, 1, 0, 0)
.Ltf_x2_bitmatrix:
	.quad BM8X8(BV8(0, 0, 0, 1, 1, 0, 0, 0),
		    BV8(0, 0, 1, 0, 0, 1, 1, 0),
		    BV8(0, 0, 0, 0, 1, 0, 1, 0),
		    BV8(1, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 0, 1, 1, 0, 0),
		    BV8(0, 1, 1, 0, 1, 0, 1, 1),
		    BV8(1, 0, 1, 1, 1, 1, 0, 1),
		    BV8(1, 0, 0, 1, 0, 0, 1, 1))
	.quad BM8X8(BV8(0, 0, 0, 1, 1, 0, 0, 0),
		    BV8(0, 0, 1, 0, 0, 1, 1, 0),
		    BV8(0, 0, 0, 0, 1, 0, 1, 0),
		    BV8(1, 1, 1, 0, 0, 0, 1, 1),
		    BV8(1, 1, 1, 0, 1, 1, 0, 0),
		    BV8(0, 1, 1, 0, 1, 0, 1, 1),
		    BV8(1, 0, 1, 1, 1, 1, 0, 1),
		    BV8(1, 0, 0, 1, 0, 0, 1, 1))

/* Identity matrix: */
.Ltf_id_bitmatrix:
	.quad BM8X8(BV8(1, 0, 0, 0, 0, 0, 0, 0),
		    BV8(0, 1, 0, 0, 0, 0, 0, 0),
		    BV8(0, 0, 1, 0, 0, 0, 0, 0),
		    BV8(0, 0, 0, 1, 0, 0, 0, 0),
		    BV8(0, 0, 0, 0, 1, 0, 0, 0),
		    BV8(0, 0, 0, 0, 0, 1, 0, 0),
		    BV8(0, 0, 0, 0, 0, 0, 1, 0),
		    BV8(0, 0, 0, 0, 0, 0, 0, 1))
	.quad BM8X8(BV8(1, 0, 0, 0, 0, 0, 0, 0),
		    BV8(0, 1, 0, 0, 0, 0, 0, 0),
		    BV8(0, 0, 1, 0, 0, 0, 0, 0),
		    BV8(0, 0, 0, 1, 0, 0, 0, 0),
		    BV8(0, 0, 0, 0, 1, 0, 0, 0),
		    BV8(0, 0, 0, 0, 0, 1, 0, 0),
		    BV8(0, 0, 0, 0, 0, 0, 1, 0),
		    BV8(0, 0, 0, 0, 0, 0, 0, 1))
#endif /* CONFIG_AS_GFNI */

/* 4-bit mask */
.section	.rodata.cst4.L0f0f0f0f, "aM", @progbits, 4
.align 4
.L0f0f0f0f:
	.long 0x0f0f0f0f

.text

SYM_FUNC_START_LOCAL(__aria_aesni_avx_crypt_16way)
	/* input:
	*      %r9: rk
	*      %rsi: dst
	*      %rdx: src
	*      %xmm0..%xmm15: 16 byte-sliced blocks
	*/

	FRAME_BEGIN

	movq %rsi, %rax;
	leaq 8 * 16(%rax), %r8;

	inpack16_post(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		      %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		      %xmm15, %rax, %r8);
	aria_fo(%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 0);
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 1);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 2);
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 3);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 4);
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 5);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 6);
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 7);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 8);
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 9);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 10);
	cmpl $12, ARIA_CTX_rounds(CTX);
	jne .Laria_192;
	aria_ff(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 11, 12);
	jmp .Laria_end;
.Laria_192:
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 11);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 12);
	cmpl $14, ARIA_CTX_rounds(CTX);
	jne .Laria_256;
	aria_ff(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 13, 14);
	jmp .Laria_end;
.Laria_256:
	aria_fe(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 13);
	aria_fo(%xmm9, %xmm8, %xmm11, %xmm10, %xmm12, %xmm13, %xmm14, %xmm15,
		%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		%rax, %r9, 14);
	aria_ff(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 15, 16);
.Laria_end:
	debyteslice_16x16b(%xmm8, %xmm12, %xmm1, %xmm4,
			   %xmm9, %xmm13, %xmm0, %xmm5,
			   %xmm10, %xmm14, %xmm3, %xmm6,
			   %xmm11, %xmm15, %xmm2, %xmm7,
			   (%rax), (%r8));

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx_crypt_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_encrypt_16way)
	/* input:
	*      %rdi: ctx, CTX
	*      %rsi: dst
	*      %rdx: src
	*/

	FRAME_BEGIN

	leaq ARIA_CTX_enc_key(CTX), %r9;

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx);

	call __aria_aesni_avx_crypt_16way;

	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_encrypt_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_decrypt_16way)
	/* input:
	*      %rdi: ctx, CTX
	*      %rsi: dst
	*      %rdx: src
	*/

	FRAME_BEGIN

	leaq ARIA_CTX_dec_key(CTX), %r9;

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx);

	call __aria_aesni_avx_crypt_16way;

	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_decrypt_16way)

SYM_FUNC_START_LOCAL(__aria_aesni_avx_ctr_gen_keystream_16way)
	/* input:
	*      %rdi: ctx
	*      %rsi: dst
	*      %rdx: src
	*      %rcx: keystream
	*      %r8: iv (big endian, 128bit)
	*/

	FRAME_BEGIN
	/* load IV and byteswap */
	vmovdqu (%r8), %xmm8;

	vmovdqa .Lbswap128_mask (%rip), %xmm1;
	vpshufb %xmm1, %xmm8, %xmm3; /* be => le */

	vpcmpeqd %xmm0, %xmm0, %xmm0;
	vpsrldq $8, %xmm0, %xmm0; /* low: -1, high: 0 */

	/* construct IVs */
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm9;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm10;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm11;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm12;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm13;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm14;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm15;
	vmovdqu %xmm8, (0 * 16)(%rcx);
	vmovdqu %xmm9, (1 * 16)(%rcx);
	vmovdqu %xmm10, (2 * 16)(%rcx);
	vmovdqu %xmm11, (3 * 16)(%rcx);
	vmovdqu %xmm12, (4 * 16)(%rcx);
	vmovdqu %xmm13, (5 * 16)(%rcx);
	vmovdqu %xmm14, (6 * 16)(%rcx);
	vmovdqu %xmm15, (7 * 16)(%rcx);

	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm8;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm9;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm10;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm11;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm12;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm13;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm14;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm15;
	inc_le128(%xmm3, %xmm0, %xmm5); /* +1 */
	vpshufb %xmm1, %xmm3, %xmm4;
	vmovdqu %xmm4, (%r8);

	vmovdqu (0 * 16)(%rcx), %xmm0;
	vmovdqu (1 * 16)(%rcx), %xmm1;
	vmovdqu (2 * 16)(%rcx), %xmm2;
	vmovdqu (3 * 16)(%rcx), %xmm3;
	vmovdqu (4 * 16)(%rcx), %xmm4;
	vmovdqu (5 * 16)(%rcx), %xmm5;
	vmovdqu (6 * 16)(%rcx), %xmm6;
	vmovdqu (7 * 16)(%rcx), %xmm7;

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx_ctr_gen_keystream_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_ctr_crypt_16way)
	/* input:
	*      %rdi: ctx
	*      %rsi: dst
	*      %rdx: src
	*      %rcx: keystream
	*      %r8: iv (big endian, 128bit)
	*/
	FRAME_BEGIN

	call __aria_aesni_avx_ctr_gen_keystream_16way;

	leaq (%rsi), %r10;
	leaq (%rdx), %r11;
	leaq (%rcx), %rsi;
	leaq (%rcx), %rdx;
	leaq ARIA_CTX_enc_key(CTX), %r9;

	call __aria_aesni_avx_crypt_16way;

	vpxor (0 * 16)(%r11), %xmm1, %xmm1;
	vpxor (1 * 16)(%r11), %xmm0, %xmm0;
	vpxor (2 * 16)(%r11), %xmm3, %xmm3;
	vpxor (3 * 16)(%r11), %xmm2, %xmm2;
	vpxor (4 * 16)(%r11), %xmm4, %xmm4;
	vpxor (5 * 16)(%r11), %xmm5, %xmm5;
	vpxor (6 * 16)(%r11), %xmm6, %xmm6;
	vpxor (7 * 16)(%r11), %xmm7, %xmm7;
	vpxor (8 * 16)(%r11), %xmm8, %xmm8;
	vpxor (9 * 16)(%r11), %xmm9, %xmm9;
	vpxor (10 * 16)(%r11), %xmm10, %xmm10;
	vpxor (11 * 16)(%r11), %xmm11, %xmm11;
	vpxor (12 * 16)(%r11), %xmm12, %xmm12;
	vpxor (13 * 16)(%r11), %xmm13, %xmm13;
	vpxor (14 * 16)(%r11), %xmm14, %xmm14;
	vpxor (15 * 16)(%r11), %xmm15, %xmm15;
	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %r10);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_ctr_crypt_16way)

#ifdef CONFIG_AS_GFNI
SYM_FUNC_START_LOCAL(__aria_aesni_avx_gfni_crypt_16way)
	/* input:
	*      %r9: rk
	*      %rsi: dst
	*      %rdx: src
	*      %xmm0..%xmm15: 16 byte-sliced blocks
	*/

	FRAME_BEGIN

	movq %rsi, %rax;
	leaq 8 * 16(%rax), %r8;

	inpack16_post(%xmm0, %xmm1, %xmm2, %xmm3,
		      %xmm4, %xmm5, %xmm6, %xmm7,
		      %xmm8, %xmm9, %xmm10, %xmm11,
		      %xmm12, %xmm13, %xmm14,
		      %xmm15, %rax, %r8);
	aria_fo_gfni(%xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 0);
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 1);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 2);
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 3);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 4);
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 5);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 6);
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 7);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 8);
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 9);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 10);
	cmpl $12, ARIA_CTX_rounds(CTX);
	jne .Laria_gfni_192;
	aria_ff_gfni(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		%xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		%xmm15, %rax, %r9, 11, 12);
	jmp .Laria_gfni_end;
.Laria_gfni_192:
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 11);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 12);
	cmpl $14, ARIA_CTX_rounds(CTX);
	jne .Laria_gfni_256;
	aria_ff_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 13, 14);
	jmp .Laria_gfni_end;
.Laria_gfni_256:
	aria_fe_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 13);
	aria_fo_gfni(%xmm9, %xmm8, %xmm11, %xmm10,
		     %xmm12, %xmm13, %xmm14, %xmm15,
		     %xmm0, %xmm1, %xmm2, %xmm3,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %rax, %r9, 14);
	aria_ff_gfni(%xmm1, %xmm0, %xmm3, %xmm2,
		     %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11,
		     %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %r9, 15, 16);
.Laria_gfni_end:
	debyteslice_16x16b(%xmm8, %xmm12, %xmm1, %xmm4,
			   %xmm9, %xmm13, %xmm0, %xmm5,
			   %xmm10, %xmm14, %xmm3, %xmm6,
			   %xmm11, %xmm15, %xmm2, %xmm7,
			   (%rax), (%r8));

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx_gfni_crypt_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_gfni_encrypt_16way)
	/* input:
	*      %rdi: ctx, CTX
	*      %rsi: dst
	*      %rdx: src
	*/

	FRAME_BEGIN

	leaq ARIA_CTX_enc_key(CTX), %r9;

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx);

	call __aria_aesni_avx_gfni_crypt_16way;

	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_gfni_encrypt_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_gfni_decrypt_16way)
	/* input:
	*      %rdi: ctx, CTX
	*      %rsi: dst
	*      %rdx: src
	*/

	FRAME_BEGIN

	leaq ARIA_CTX_dec_key(CTX), %r9;

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx);

	call __aria_aesni_avx_gfni_crypt_16way;

	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_gfni_decrypt_16way)

SYM_TYPED_FUNC_START(aria_aesni_avx_gfni_ctr_crypt_16way)
	/* input:
	*      %rdi: ctx
	*      %rsi: dst
	*      %rdx: src
	*      %rcx: keystream
	*      %r8: iv (big endian, 128bit)
	*/
	FRAME_BEGIN

	call __aria_aesni_avx_ctr_gen_keystream_16way

	leaq (%rsi), %r10;
	leaq (%rdx), %r11;
	leaq (%rcx), %rsi;
	leaq (%rcx), %rdx;
	leaq ARIA_CTX_enc_key(CTX), %r9;

	call __aria_aesni_avx_gfni_crypt_16way;

	vpxor (0 * 16)(%r11), %xmm1, %xmm1;
	vpxor (1 * 16)(%r11), %xmm0, %xmm0;
	vpxor (2 * 16)(%r11), %xmm3, %xmm3;
	vpxor (3 * 16)(%r11), %xmm2, %xmm2;
	vpxor (4 * 16)(%r11), %xmm4, %xmm4;
	vpxor (5 * 16)(%r11), %xmm5, %xmm5;
	vpxor (6 * 16)(%r11), %xmm6, %xmm6;
	vpxor (7 * 16)(%r11), %xmm7, %xmm7;
	vpxor (8 * 16)(%r11), %xmm8, %xmm8;
	vpxor (9 * 16)(%r11), %xmm9, %xmm9;
	vpxor (10 * 16)(%r11), %xmm10, %xmm10;
	vpxor (11 * 16)(%r11), %xmm11, %xmm11;
	vpxor (12 * 16)(%r11), %xmm12, %xmm12;
	vpxor (13 * 16)(%r11), %xmm13, %xmm13;
	vpxor (14 * 16)(%r11), %xmm14, %xmm14;
	vpxor (15 * 16)(%r11), %xmm15, %xmm15;
	write_output(%xmm1, %xmm0, %xmm3, %xmm2, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %r10);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx_gfni_ctr_crypt_16way)
#endif /* CONFIG_AS_GFNI */
