/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARIA Cipher 32-way parallel algorithm (AVX2)
 *
 * Copyright (c) 2022 Taehee Yoo <ap420073@gmail.com>
 *
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include <asm/asm-offsets.h>
#include <linux/cfi_types.h>

/* register macros */
#define CTX %rdi

#define ymm0_x xmm0
#define ymm1_x xmm1
#define ymm2_x xmm2
#define ymm3_x xmm3
#define ymm4_x xmm4
#define ymm5_x xmm5
#define ymm6_x xmm6
#define ymm7_x xmm7
#define ymm8_x xmm8
#define ymm9_x xmm9
#define ymm10_x xmm10
#define ymm11_x xmm11
#define ymm12_x xmm12
#define ymm13_x xmm13
#define ymm14_x xmm14
#define ymm15_x xmm15

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
	vbroadcasti128 .Lshufb_16x16b, a0;		\
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
	vbroadcasti128 .Lshufb_16x16b, a0;		\
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
	vmovdqu (0 * 32)(rio), x0;			\
	vmovdqu (1 * 32)(rio), x1;			\
	vmovdqu (2 * 32)(rio), x2;			\
	vmovdqu (3 * 32)(rio), x3;			\
	vmovdqu (4 * 32)(rio), x4;			\
	vmovdqu (5 * 32)(rio), x5;			\
	vmovdqu (6 * 32)(rio), x6;			\
	vmovdqu (7 * 32)(rio), x7;			\
	vmovdqu (8 * 32)(rio), y0;			\
	vmovdqu (9 * 32)(rio), y1;			\
	vmovdqu (10 * 32)(rio), y2;			\
	vmovdqu (11 * 32)(rio), y3;			\
	vmovdqu (12 * 32)(rio), y4;			\
	vmovdqu (13 * 32)(rio), y5;			\
	vmovdqu (14 * 32)(rio), y6;			\
	vmovdqu (15 * 32)(rio), y7;

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
	vmovdqu x0, 0 * 32(mem_ab);			\
	vmovdqu x1, 1 * 32(mem_ab);			\
	vmovdqu x2, 2 * 32(mem_ab);			\
	vmovdqu x3, 3 * 32(mem_ab);			\
	vmovdqu x4, 4 * 32(mem_ab);			\
	vmovdqu x5, 5 * 32(mem_ab);			\
	vmovdqu x6, 6 * 32(mem_ab);			\
	vmovdqu x7, 7 * 32(mem_ab);			\
	vmovdqu y0, 0 * 32(mem_cd);			\
	vmovdqu y1, 1 * 32(mem_cd);			\
	vmovdqu y2, 2 * 32(mem_cd);			\
	vmovdqu y3, 3 * 32(mem_cd);			\
	vmovdqu y4, 4 * 32(mem_cd);			\
	vmovdqu y5, 5 * 32(mem_cd);			\
	vmovdqu y6, 6 * 32(mem_cd);			\
	vmovdqu y7, 7 * 32(mem_cd);

#define write_output(x0, x1, x2, x3,			\
		     x4, x5, x6, x7,			\
		     y0, y1, y2, y3,			\
		     y4, y5, y6, y7,			\
		     mem)				\
	vmovdqu x0, 0 * 32(mem);			\
	vmovdqu x1, 1 * 32(mem);			\
	vmovdqu x2, 2 * 32(mem);			\
	vmovdqu x3, 3 * 32(mem);			\
	vmovdqu x4, 4 * 32(mem);			\
	vmovdqu x5, 5 * 32(mem);			\
	vmovdqu x6, 6 * 32(mem);			\
	vmovdqu x7, 7 * 32(mem);			\
	vmovdqu y0, 8 * 32(mem);			\
	vmovdqu y1, 9 * 32(mem);			\
	vmovdqu y2, 10 * 32(mem);			\
	vmovdqu y3, 11 * 32(mem);			\
	vmovdqu y4, 12 * 32(mem);			\
	vmovdqu y5, 13 * 32(mem);			\
	vmovdqu y6, 14 * 32(mem);			\
	vmovdqu y7, 15 * 32(mem);			\

#define aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, idx)		\
	vmovdqu x0, ((idx + 0) * 32)(mem_tmp);		\
	vmovdqu x1, ((idx + 1) * 32)(mem_tmp);		\
	vmovdqu x2, ((idx + 2) * 32)(mem_tmp);		\
	vmovdqu x3, ((idx + 3) * 32)(mem_tmp);		\
	vmovdqu x4, ((idx + 4) * 32)(mem_tmp);		\
	vmovdqu x5, ((idx + 5) * 32)(mem_tmp);		\
	vmovdqu x6, ((idx + 6) * 32)(mem_tmp);		\
	vmovdqu x7, ((idx + 7) * 32)(mem_tmp);

#define aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, idx)		\
	vmovdqu ((idx + 0) * 32)(mem_tmp), x0;		\
	vmovdqu ((idx + 1) * 32)(mem_tmp), x1;		\
	vmovdqu ((idx + 2) * 32)(mem_tmp), x2;		\
	vmovdqu ((idx + 3) * 32)(mem_tmp), x3;		\
	vmovdqu ((idx + 4) * 32)(mem_tmp), x4;		\
	vmovdqu ((idx + 5) * 32)(mem_tmp), x5;		\
	vmovdqu ((idx + 6) * 32)(mem_tmp), x6;		\
	vmovdqu ((idx + 7) * 32)(mem_tmp), x7;

#define aria_ark_8way(x0, x1, x2, x3,			\
		      x4, x5, x6, x7,			\
		      t0, rk, idx, round)		\
	/* AddRoundKey */                               \
	vpbroadcastb ((round * 16) + idx + 3)(rk), t0;	\
	vpxor t0, x0, x0;				\
	vpbroadcastb ((round * 16) + idx + 2)(rk), t0;	\
	vpxor t0, x1, x1;				\
	vpbroadcastb ((round * 16) + idx + 1)(rk), t0;	\
	vpxor t0, x2, x2;				\
	vpbroadcastb ((round * 16) + idx + 0)(rk), t0;	\
	vpxor t0, x3, x3;				\
	vpbroadcastb ((round * 16) + idx + 7)(rk), t0;	\
	vpxor t0, x4, x4;				\
	vpbroadcastb ((round * 16) + idx + 6)(rk), t0;	\
	vpxor t0, x5, x5;				\
	vpbroadcastb ((round * 16) + idx + 5)(rk), t0;	\
	vpxor t0, x6, x6;				\
	vpbroadcastb ((round * 16) + idx + 4)(rk), t0;	\
	vpxor t0, x7, x7;

#ifdef CONFIG_AS_GFNI
#define aria_sbox_8way_gfni(x0, x1, x2, x3,		\
			    x4, x5, x6, x7,		\
			    t0, t1, t2, t3,		\
			    t4, t5, t6, t7)		\
	vpbroadcastq .Ltf_s2_bitmatrix, t0;		\
	vpbroadcastq .Ltf_inv_bitmatrix, t1;		\
	vpbroadcastq .Ltf_id_bitmatrix, t2;		\
	vpbroadcastq .Ltf_aff_bitmatrix, t3;		\
	vpbroadcastq .Ltf_x2_bitmatrix, t4;		\
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
#define aria_sbox_8way(x0, x1, x2, x3,			\
		       x4, x5, x6, x7,			\
		       t0, t1, t2, t3,			\
		       t4, t5, t6, t7)			\
	vpxor t7, t7, t7;				\
	vpxor t6, t6, t6;				\
	vbroadcasti128 .Linv_shift_row, t0;		\
	vbroadcasti128 .Lshift_row, t1;			\
	vbroadcasti128 .Ltf_lo__inv_aff__and__s2, t2;	\
	vbroadcasti128 .Ltf_hi__inv_aff__and__s2, t3;	\
	vbroadcasti128 .Ltf_lo__x2__and__fwd_aff, t4;	\
	vbroadcasti128 .Ltf_hi__x2__and__fwd_aff, t5;	\
							\
	vextracti128 $1, x0, t6##_x;			\
	vaesenclast t7##_x, x0##_x, x0##_x;		\
	vaesenclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x0, x0;			\
							\
	vextracti128 $1, x4, t6##_x;			\
	vaesenclast t7##_x, x4##_x, x4##_x;		\
	vaesenclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x4, x4;			\
							\
	vextracti128 $1, x1, t6##_x;			\
	vaesenclast t7##_x, x1##_x, x1##_x;		\
	vaesenclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x1, x1;			\
							\
	vextracti128 $1, x5, t6##_x;			\
	vaesenclast t7##_x, x5##_x, x5##_x;		\
	vaesenclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x5, x5;			\
							\
	vextracti128 $1, x2, t6##_x;			\
	vaesdeclast t7##_x, x2##_x, x2##_x;		\
	vaesdeclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x2, x2;			\
							\
	vextracti128 $1, x6, t6##_x;			\
	vaesdeclast t7##_x, x6##_x, x6##_x;		\
	vaesdeclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x6, x6;			\
							\
	vpbroadcastd .L0f0f0f0f, t6;			\
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
							\
	vpxor t6, t6, t6;				\
	vextracti128 $1, x3, t6##_x;			\
	vaesdeclast t7##_x, x3##_x, x3##_x;		\
	vaesdeclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x3, x3;			\
							\
	vextracti128 $1, x7, t6##_x;			\
	vaesdeclast t7##_x, x7##_x, x7##_x;		\
	vaesdeclast t7##_x, t6##_x, t6##_x;		\
	vinserti128 $1, t6##_x, x7, x7;			\

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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
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
		      y0, rk, 0, round);		\
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
	/* aria_diff_byte()				\
	 * T3 = ABCD -> BADC				\
	 * T3 = y4, y5, y6, y7 -> y5, y4, y7, y6	\
	 * T0 = ABCD -> CDAB				\
	 * T0 = x0, x1, x2, x3 -> x2, x3, x0, x1	\
	 * T1 = ABCD -> DCBA				\
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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
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
		      y0, rk, 0, round);		\
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
	/* aria_diff_byte()				\
	 * T1 = ABCD -> BADC				\
	 * T1 = x4, x5, x6, x7 -> x5, x4, x7, x6	\
	 * T2 = ABCD -> CDAB				\
	 * T2 = y0, y1, y2, y3, -> y2, y3, y0, y1	\
	 * T3 = ABCD -> DCBA				\
	 * T3 = y4, y5, y6, y7 -> y7, y6, y5, y4	\
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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, last_round);		\
							\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 0, round);		\
							\
	aria_sbox_8way(x2, x3, x0, x1, x6, x7, x4, x5,	\
		       y0, y1, y2, y3, y4, y5, y6, y7);	\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 0, last_round);		\
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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1,		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3,		\
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
		      y0, rk, 0, round);		\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1,		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3,		\
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
	/* aria_diff_byte()				\
	 * T3 = ABCD -> BADC				\
	 * T3 = y4, y5, y6, y7 -> y5, y4, y7, y6	\
	 * T0 = ABCD -> CDAB				\
	 * T0 = x0, x1, x2, x3 -> x2, x3, x0, x1	\
	 * T1 = ABCD -> DCBA				\
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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
							\
	aria_sbox_8way_gfni(x0, x1, x2, x3,		\
			    x4, x5, x6, x7,		\
			    y0, y1, y2, y3,		\
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
		      y0, rk, 0, round);		\
							\
	aria_sbox_8way_gfni(x0, x1, x2, x3,		\
			    x4, x5, x6, x7,		\
			    y0, y1, y2, y3,		\
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
	/* aria_diff_byte()				\
	 * T1 = ABCD -> BADC				\
	 * T1 = x4, x5, x6, x7 -> x5, x4, x7, x6	\
	 * T2 = ABCD -> CDAB				\
	 * T2 = y0, y1, y2, y3, -> y2, y3, y0, y1	\
	 * T3 = ABCD -> DCBA				\
	 * T3 = y4, y5, y6, y7 -> y7, y6, y5, y4	\
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
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, round);		\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1,		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3,		\
			    y4, y5, y6, y7);		\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 8, last_round);		\
							\
	aria_store_state_8way(x0, x1, x2, x3,		\
			      x4, x5, x6, x7,		\
			      mem_tmp, 8);		\
							\
	aria_load_state_8way(x0, x1, x2, x3,		\
			     x4, x5, x6, x7,		\
			     mem_tmp, 0);		\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 0, round);		\
							\
	aria_sbox_8way_gfni(x2, x3, x0, x1,		\
			    x6, x7, x4, x5,		\
			    y0, y1, y2, y3,		\
			    y4, y5, y6, y7);		\
							\
	aria_ark_8way(x0, x1, x2, x3, x4, x5, x6, x7,	\
		      y0, rk, 0, last_round);		\
							\
	aria_load_state_8way(y0, y1, y2, y3,		\
			     y4, y5, y6, y7,		\
			     mem_tmp, 8);
#endif /* CONFIG_AS_GFNI */

.section        .rodata.cst32.shufb_16x16b, "aM", @progbits, 32
.align 32
#define SHUFB_BYTES(idx) \
	0 + (idx), 4 + (idx), 8 + (idx), 12 + (idx)
.Lshufb_16x16b:
	.byte SHUFB_BYTES(0), SHUFB_BYTES(1), SHUFB_BYTES(2), SHUFB_BYTES(3)
	.byte SHUFB_BYTES(0), SHUFB_BYTES(1), SHUFB_BYTES(2), SHUFB_BYTES(3)

.section	.rodata.cst16, "aM", @progbits, 16
.align 16
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

#endif /* CONFIG_AS_GFNI */

/* 4-bit mask */
.section	.rodata.cst4.L0f0f0f0f, "aM", @progbits, 4
.align 4
.L0f0f0f0f:
	.long 0x0f0f0f0f

.text

SYM_FUNC_START_LOCAL(__aria_aesni_avx2_crypt_32way)
	/* input:
	 *      %r9: rk
	 *      %rsi: dst
	 *      %rdx: src
	 *      %ymm0..%ymm15: byte-sliced blocks
	 */

	FRAME_BEGIN

	movq %rsi, %rax;
	leaq 8 * 32(%rax), %r8;

	inpack16_post(%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		      %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		      %ymm15, %rax, %r8);
	aria_fo(%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 0);
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 1);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 2);
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 3);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 4);
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 5);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 6);
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 7);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 8);
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 9);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 10);
	cmpl $12, ARIA_CTX_rounds(CTX);
	jne .Laria_192;
	aria_ff(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 11, 12);
	jmp .Laria_end;
.Laria_192:
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 11);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 12);
	cmpl $14, ARIA_CTX_rounds(CTX);
	jne .Laria_256;
	aria_ff(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 13, 14);
	jmp .Laria_end;
.Laria_256:
	aria_fe(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 13);
	aria_fo(%ymm9, %ymm8, %ymm11, %ymm10, %ymm12, %ymm13, %ymm14, %ymm15,
		%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		%rax, %r9, 14);
	aria_ff(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 15, 16);
.Laria_end:
	debyteslice_16x16b(%ymm8, %ymm12, %ymm1, %ymm4,
			   %ymm9, %ymm13, %ymm0, %ymm5,
			   %ymm10, %ymm14, %ymm3, %ymm6,
			   %ymm11, %ymm15, %ymm2, %ymm7,
			   (%rax), (%r8));

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx2_crypt_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_encrypt_32way)
	/* input:
	 *      %rdi: ctx, CTX
	 *      %rsi: dst
	 *      %rdx: src
	 */

	FRAME_BEGIN

	leaq ARIA_CTX_enc_key(CTX), %r9;

	inpack16_pre(%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rdx);

	call __aria_aesni_avx2_crypt_32way;

	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_encrypt_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_decrypt_32way)
	/* input:
	 *      %rdi: ctx, CTX
	 *      %rsi: dst
	 *      %rdx: src
	 */

	FRAME_BEGIN

	leaq ARIA_CTX_dec_key(CTX), %r9;

	inpack16_pre(%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rdx);

	call __aria_aesni_avx2_crypt_32way;

	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_decrypt_32way)

SYM_FUNC_START_LOCAL(__aria_aesni_avx2_ctr_gen_keystream_32way)
	/* input:
	 *      %rdi: ctx
	 *      %rsi: dst
	 *      %rdx: src
	 *      %rcx: keystream
	 *      %r8: iv (big endian, 128bit)
	 */

	FRAME_BEGIN
	movq 8(%r8), %r11;
	bswapq %r11;

	vbroadcasti128 .Lbswap128_mask (%rip), %ymm6;
	vpcmpeqd %ymm0, %ymm0, %ymm0;
	vpsrldq $8, %ymm0, %ymm0;   /* ab: -1:0 ; cd: -1:0 */
	vpaddq %ymm0, %ymm0, %ymm5; /* ab: -2:0 ; cd: -2:0 */

	/* load IV and byteswap */
	vmovdqu (%r8), %xmm7;
	vpshufb %xmm6, %xmm7, %xmm7;
	vmovdqa %xmm7, %xmm3;
	inc_le128(%xmm7, %xmm0, %xmm4);
	vinserti128 $1, %xmm7, %ymm3, %ymm3;
	vpshufb %ymm6, %ymm3, %ymm8; /* +1 ; +0 */

	/* check need for handling 64-bit overflow and carry */
	cmpq $(0xffffffffffffffff - 32), %r11;
	ja .Lhandle_ctr_carry;

	/* construct IVs */
	vpsubq %ymm5, %ymm3, %ymm3; /* +3 ; +2 */
	vpshufb %ymm6, %ymm3, %ymm9;
	vpsubq %ymm5, %ymm3, %ymm3; /* +5 ; +4 */
	vpshufb %ymm6, %ymm3, %ymm10;
	vpsubq %ymm5, %ymm3, %ymm3; /* +7 ; +6 */
	vpshufb %ymm6, %ymm3, %ymm11;
	vpsubq %ymm5, %ymm3, %ymm3; /* +9 ; +8 */
	vpshufb %ymm6, %ymm3, %ymm12;
	vpsubq %ymm5, %ymm3, %ymm3; /* +11 ; +10 */
	vpshufb %ymm6, %ymm3, %ymm13;
	vpsubq %ymm5, %ymm3, %ymm3; /* +13 ; +12 */
	vpshufb %ymm6, %ymm3, %ymm14;
	vpsubq %ymm5, %ymm3, %ymm3; /* +15 ; +14 */
	vpshufb %ymm6, %ymm3, %ymm15;
	vmovdqu %ymm8, (0 * 32)(%rcx);
	vmovdqu %ymm9, (1 * 32)(%rcx);
	vmovdqu %ymm10, (2 * 32)(%rcx);
	vmovdqu %ymm11, (3 * 32)(%rcx);
	vmovdqu %ymm12, (4 * 32)(%rcx);
	vmovdqu %ymm13, (5 * 32)(%rcx);
	vmovdqu %ymm14, (6 * 32)(%rcx);
	vmovdqu %ymm15, (7 * 32)(%rcx);

	vpsubq %ymm5, %ymm3, %ymm3; /* +17 ; +16 */
	vpshufb %ymm6, %ymm3, %ymm8;
	vpsubq %ymm5, %ymm3, %ymm3; /* +19 ; +18 */
	vpshufb %ymm6, %ymm3, %ymm9;
	vpsubq %ymm5, %ymm3, %ymm3; /* +21 ; +20 */
	vpshufb %ymm6, %ymm3, %ymm10;
	vpsubq %ymm5, %ymm3, %ymm3; /* +23 ; +22 */
	vpshufb %ymm6, %ymm3, %ymm11;
	vpsubq %ymm5, %ymm3, %ymm3; /* +25 ; +24 */
	vpshufb %ymm6, %ymm3, %ymm12;
	vpsubq %ymm5, %ymm3, %ymm3; /* +27 ; +26 */
	vpshufb %ymm6, %ymm3, %ymm13;
	vpsubq %ymm5, %ymm3, %ymm3; /* +29 ; +28 */
	vpshufb %ymm6, %ymm3, %ymm14;
	vpsubq %ymm5, %ymm3, %ymm3; /* +31 ; +30 */
	vpshufb %ymm6, %ymm3, %ymm15;
	vpsubq %ymm5, %ymm3, %ymm3; /* +32 */
	vpshufb %xmm6, %xmm3, %xmm3;
	vmovdqu %xmm3, (%r8);
	vmovdqu (0 * 32)(%rcx), %ymm0;
	vmovdqu (1 * 32)(%rcx), %ymm1;
	vmovdqu (2 * 32)(%rcx), %ymm2;
	vmovdqu (3 * 32)(%rcx), %ymm3;
	vmovdqu (4 * 32)(%rcx), %ymm4;
	vmovdqu (5 * 32)(%rcx), %ymm5;
	vmovdqu (6 * 32)(%rcx), %ymm6;
	vmovdqu (7 * 32)(%rcx), %ymm7;
	jmp .Lctr_carry_done;

	.Lhandle_ctr_carry:
	/* construct IVs */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm9; /* +3 ; +2 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm10; /* +5 ; +4 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm11; /* +7 ; +6 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm12; /* +9 ; +8 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm13; /* +11 ; +10 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm14; /* +13 ; +12 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm15; /* +15 ; +14 */
	vmovdqu %ymm8, (0 * 32)(%rcx);
	vmovdqu %ymm9, (1 * 32)(%rcx);
	vmovdqu %ymm10, (2 * 32)(%rcx);
	vmovdqu %ymm11, (3 * 32)(%rcx);
	vmovdqu %ymm12, (4 * 32)(%rcx);
	vmovdqu %ymm13, (5 * 32)(%rcx);
	vmovdqu %ymm14, (6 * 32)(%rcx);
	vmovdqu %ymm15, (7 * 32)(%rcx);

	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm8; /* +17 ; +16 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm9; /* +19 ; +18 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm10; /* +21 ; +20 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm11; /* +23 ; +22 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm12; /* +25 ; +24 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm13; /* +27 ; +26 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm14; /* +29 ; +28 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	inc_le128(%ymm3, %ymm0, %ymm4);
	vpshufb %ymm6, %ymm3, %ymm15; /* +31 ; +30 */
	inc_le128(%ymm3, %ymm0, %ymm4);
	vextracti128 $1, %ymm3, %xmm3;
	vpshufb %xmm6, %xmm3, %xmm3; /* +32 */
	vmovdqu %xmm3, (%r8);
	vmovdqu (0 * 32)(%rcx), %ymm0;
	vmovdqu (1 * 32)(%rcx), %ymm1;
	vmovdqu (2 * 32)(%rcx), %ymm2;
	vmovdqu (3 * 32)(%rcx), %ymm3;
	vmovdqu (4 * 32)(%rcx), %ymm4;
	vmovdqu (5 * 32)(%rcx), %ymm5;
	vmovdqu (6 * 32)(%rcx), %ymm6;
	vmovdqu (7 * 32)(%rcx), %ymm7;

	.Lctr_carry_done:

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx2_ctr_gen_keystream_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_ctr_crypt_32way)
	/* input:
	 *      %rdi: ctx
	 *      %rsi: dst
	 *      %rdx: src
	 *      %rcx: keystream
	 *      %r8: iv (big endian, 128bit)
	 */
	FRAME_BEGIN

	call __aria_aesni_avx2_ctr_gen_keystream_32way;

	leaq (%rsi), %r10;
	leaq (%rdx), %r11;
	leaq (%rcx), %rsi;
	leaq (%rcx), %rdx;
	leaq ARIA_CTX_enc_key(CTX), %r9;

	call __aria_aesni_avx2_crypt_32way;

	vpxor (0 * 32)(%r11), %ymm1, %ymm1;
	vpxor (1 * 32)(%r11), %ymm0, %ymm0;
	vpxor (2 * 32)(%r11), %ymm3, %ymm3;
	vpxor (3 * 32)(%r11), %ymm2, %ymm2;
	vpxor (4 * 32)(%r11), %ymm4, %ymm4;
	vpxor (5 * 32)(%r11), %ymm5, %ymm5;
	vpxor (6 * 32)(%r11), %ymm6, %ymm6;
	vpxor (7 * 32)(%r11), %ymm7, %ymm7;
	vpxor (8 * 32)(%r11), %ymm8, %ymm8;
	vpxor (9 * 32)(%r11), %ymm9, %ymm9;
	vpxor (10 * 32)(%r11), %ymm10, %ymm10;
	vpxor (11 * 32)(%r11), %ymm11, %ymm11;
	vpxor (12 * 32)(%r11), %ymm12, %ymm12;
	vpxor (13 * 32)(%r11), %ymm13, %ymm13;
	vpxor (14 * 32)(%r11), %ymm14, %ymm14;
	vpxor (15 * 32)(%r11), %ymm15, %ymm15;
	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %r10);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_ctr_crypt_32way)

#ifdef CONFIG_AS_GFNI
SYM_FUNC_START_LOCAL(__aria_aesni_avx2_gfni_crypt_32way)
	/* input:
	 *      %r9: rk
	 *      %rsi: dst
	 *      %rdx: src
	 *      %ymm0..%ymm15: 16 byte-sliced blocks
	 */

	FRAME_BEGIN

	movq %rsi, %rax;
	leaq 8 * 32(%rax), %r8;

	inpack16_post(%ymm0, %ymm1, %ymm2, %ymm3,
		      %ymm4, %ymm5, %ymm6, %ymm7,
		      %ymm8, %ymm9, %ymm10, %ymm11,
		      %ymm12, %ymm13, %ymm14,
		      %ymm15, %rax, %r8);
	aria_fo_gfni(%ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 0);
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 1);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 2);
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 3);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 4);
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 5);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 6);
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 7);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 8);
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 9);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 10);
	cmpl $12, ARIA_CTX_rounds(CTX);
	jne .Laria_gfni_192;
	aria_ff_gfni(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		%ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		%ymm15, %rax, %r9, 11, 12);
	jmp .Laria_gfni_end;
.Laria_gfni_192:
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 11);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 12);
	cmpl $14, ARIA_CTX_rounds(CTX);
	jne .Laria_gfni_256;
	aria_ff_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 13, 14);
	jmp .Laria_gfni_end;
.Laria_gfni_256:
	aria_fe_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 13);
	aria_fo_gfni(%ymm9, %ymm8, %ymm11, %ymm10,
		     %ymm12, %ymm13, %ymm14, %ymm15,
		     %ymm0, %ymm1, %ymm2, %ymm3,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %rax, %r9, 14);
	aria_ff_gfni(%ymm1, %ymm0, %ymm3, %ymm2,
		     %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11,
		     %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax, %r9, 15, 16);
.Laria_gfni_end:
	debyteslice_16x16b(%ymm8, %ymm12, %ymm1, %ymm4,
			   %ymm9, %ymm13, %ymm0, %ymm5,
			   %ymm10, %ymm14, %ymm3, %ymm6,
			   %ymm11, %ymm15, %ymm2, %ymm7,
			   (%rax), (%r8));

	FRAME_END
	RET;
SYM_FUNC_END(__aria_aesni_avx2_gfni_crypt_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_gfni_encrypt_32way)
	/* input:
	 *      %rdi: ctx, CTX
	 *      %rsi: dst
	 *      %rdx: src
	 */

	FRAME_BEGIN

	leaq ARIA_CTX_enc_key(CTX), %r9;

	inpack16_pre(%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rdx);

	call __aria_aesni_avx2_gfni_crypt_32way;

	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_gfni_encrypt_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_gfni_decrypt_32way)
	/* input:
	 *      %rdi: ctx, CTX
	 *      %rsi: dst
	 *      %rdx: src
	 */

	FRAME_BEGIN

	leaq ARIA_CTX_dec_key(CTX), %r9;

	inpack16_pre(%ymm0, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rdx);

	call __aria_aesni_avx2_gfni_crypt_32way;

	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %rax);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_gfni_decrypt_32way)

SYM_TYPED_FUNC_START(aria_aesni_avx2_gfni_ctr_crypt_32way)
	/* input:
	 *      %rdi: ctx
	 *      %rsi: dst
	 *      %rdx: src
	 *      %rcx: keystream
	 *      %r8: iv (big endian, 128bit)
	 */
	FRAME_BEGIN

	call __aria_aesni_avx2_ctr_gen_keystream_32way

	leaq (%rsi), %r10;
	leaq (%rdx), %r11;
	leaq (%rcx), %rsi;
	leaq (%rcx), %rdx;
	leaq ARIA_CTX_enc_key(CTX), %r9;

	call __aria_aesni_avx2_gfni_crypt_32way;

	vpxor (0 * 32)(%r11), %ymm1, %ymm1;
	vpxor (1 * 32)(%r11), %ymm0, %ymm0;
	vpxor (2 * 32)(%r11), %ymm3, %ymm3;
	vpxor (3 * 32)(%r11), %ymm2, %ymm2;
	vpxor (4 * 32)(%r11), %ymm4, %ymm4;
	vpxor (5 * 32)(%r11), %ymm5, %ymm5;
	vpxor (6 * 32)(%r11), %ymm6, %ymm6;
	vpxor (7 * 32)(%r11), %ymm7, %ymm7;
	vpxor (8 * 32)(%r11), %ymm8, %ymm8;
	vpxor (9 * 32)(%r11), %ymm9, %ymm9;
	vpxor (10 * 32)(%r11), %ymm10, %ymm10;
	vpxor (11 * 32)(%r11), %ymm11, %ymm11;
	vpxor (12 * 32)(%r11), %ymm12, %ymm12;
	vpxor (13 * 32)(%r11), %ymm13, %ymm13;
	vpxor (14 * 32)(%r11), %ymm14, %ymm14;
	vpxor (15 * 32)(%r11), %ymm15, %ymm15;
	write_output(%ymm1, %ymm0, %ymm3, %ymm2, %ymm4, %ymm5, %ymm6, %ymm7,
		     %ymm8, %ymm9, %ymm10, %ymm11, %ymm12, %ymm13, %ymm14,
		     %ymm15, %r10);

	FRAME_END
	RET;
SYM_FUNC_END(aria_aesni_avx2_gfni_ctr_crypt_32way)
#endif /* CONFIG_AS_GFNI */
