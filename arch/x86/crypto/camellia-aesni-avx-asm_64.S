/*
 * x86_64/AVX/AES-NI assembler implementation of Camellia
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/*
 * Version licensed under 2-clause BSD License is available at:
 *	http://koti.mbnet.fi/axh/crypto/camellia-BSD-1.2.0-aesni1.tar.xz
 */

#include <linux/linkage.h>
#include <asm/nospec-branch.h>

#define CAMELLIA_TABLE_BYTE_LEN 272

/* struct camellia_ctx: */
#define key_table 0
#define key_length CAMELLIA_TABLE_BYTE_LEN

/* register macros */
#define CTX %rdi

/**********************************************************************
  16-way camellia
 **********************************************************************/
#define filter_8bit(x, lo_t, hi_t, mask4bit, tmp0) \
	vpand x, mask4bit, tmp0; \
	vpandn x, mask4bit, x; \
	vpsrld $4, x, x; \
	\
	vpshufb tmp0, lo_t, tmp0; \
	vpshufb x, hi_t, x; \
	vpxor tmp0, x, x;

/*
 * IN:
 *   x0..x7: byte-sliced AB state
 *   mem_cd: register pointer storing CD state
 *   key: index for key material
 * OUT:
 *   x0..x7: new byte-sliced CD state
 */
#define roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, t0, t1, t2, t3, t4, t5, t6, \
		  t7, mem_cd, key) \
	/* \
	 * S-function with AES subbytes \
	 */ \
	vmovdqa .Linv_shift_row, t4; \
	vbroadcastss .L0f0f0f0f, t7; \
	vmovdqa .Lpre_tf_lo_s1, t0; \
	vmovdqa .Lpre_tf_hi_s1, t1; \
	\
	/* AES inverse shift rows */ \
	vpshufb t4, x0, x0; \
	vpshufb t4, x7, x7; \
	vpshufb t4, x1, x1; \
	vpshufb t4, x4, x4; \
	vpshufb t4, x2, x2; \
	vpshufb t4, x5, x5; \
	vpshufb t4, x3, x3; \
	vpshufb t4, x6, x6; \
	\
	/* prefilter sboxes 1, 2 and 3 */ \
	vmovdqa .Lpre_tf_lo_s4, t2; \
	vmovdqa .Lpre_tf_hi_s4, t3; \
	filter_8bit(x0, t0, t1, t7, t6); \
	filter_8bit(x7, t0, t1, t7, t6); \
	filter_8bit(x1, t0, t1, t7, t6); \
	filter_8bit(x4, t0, t1, t7, t6); \
	filter_8bit(x2, t0, t1, t7, t6); \
	filter_8bit(x5, t0, t1, t7, t6); \
	\
	/* prefilter sbox 4 */ \
	vpxor t4, t4, t4; \
	filter_8bit(x3, t2, t3, t7, t6); \
	filter_8bit(x6, t2, t3, t7, t6); \
	\
	/* AES subbytes + AES shift rows */ \
	vmovdqa .Lpost_tf_lo_s1, t0; \
	vmovdqa .Lpost_tf_hi_s1, t1; \
	vaesenclast t4, x0, x0; \
	vaesenclast t4, x7, x7; \
	vaesenclast t4, x1, x1; \
	vaesenclast t4, x4, x4; \
	vaesenclast t4, x2, x2; \
	vaesenclast t4, x5, x5; \
	vaesenclast t4, x3, x3; \
	vaesenclast t4, x6, x6; \
	\
	/* postfilter sboxes 1 and 4 */ \
	vmovdqa .Lpost_tf_lo_s3, t2; \
	vmovdqa .Lpost_tf_hi_s3, t3; \
	filter_8bit(x0, t0, t1, t7, t6); \
	filter_8bit(x7, t0, t1, t7, t6); \
	filter_8bit(x3, t0, t1, t7, t6); \
	filter_8bit(x6, t0, t1, t7, t6); \
	\
	/* postfilter sbox 3 */ \
	vmovdqa .Lpost_tf_lo_s2, t4; \
	vmovdqa .Lpost_tf_hi_s2, t5; \
	filter_8bit(x2, t2, t3, t7, t6); \
	filter_8bit(x5, t2, t3, t7, t6); \
	\
	vpxor t6, t6, t6; \
	vmovq key, t0; \
	\
	/* postfilter sbox 2 */ \
	filter_8bit(x1, t4, t5, t7, t2); \
	filter_8bit(x4, t4, t5, t7, t2); \
	\
	vpsrldq $5, t0, t5; \
	vpsrldq $1, t0, t1; \
	vpsrldq $2, t0, t2; \
	vpsrldq $3, t0, t3; \
	vpsrldq $4, t0, t4; \
	vpshufb t6, t0, t0; \
	vpshufb t6, t1, t1; \
	vpshufb t6, t2, t2; \
	vpshufb t6, t3, t3; \
	vpshufb t6, t4, t4; \
	vpsrldq $2, t5, t7; \
	vpshufb t6, t7, t7; \
	\
	/* \
	 * P-function \
	 */ \
	vpxor x5, x0, x0; \
	vpxor x6, x1, x1; \
	vpxor x7, x2, x2; \
	vpxor x4, x3, x3; \
	\
	vpxor x2, x4, x4; \
	vpxor x3, x5, x5; \
	vpxor x0, x6, x6; \
	vpxor x1, x7, x7; \
	\
	vpxor x7, x0, x0; \
	vpxor x4, x1, x1; \
	vpxor x5, x2, x2; \
	vpxor x6, x3, x3; \
	\
	vpxor x3, x4, x4; \
	vpxor x0, x5, x5; \
	vpxor x1, x6, x6; \
	vpxor x2, x7, x7; /* note: high and low parts swapped */ \
	\
	/* \
	 * Add key material and result to CD (x becomes new CD) \
	 */ \
	\
	vpxor t3, x4, x4; \
	vpxor 0 * 16(mem_cd), x4, x4; \
	\
	vpxor t2, x5, x5; \
	vpxor 1 * 16(mem_cd), x5, x5; \
	\
	vpsrldq $1, t5, t3; \
	vpshufb t6, t5, t5; \
	vpshufb t6, t3, t6; \
	\
	vpxor t1, x6, x6; \
	vpxor 2 * 16(mem_cd), x6, x6; \
	\
	vpxor t0, x7, x7; \
	vpxor 3 * 16(mem_cd), x7, x7; \
	\
	vpxor t7, x0, x0; \
	vpxor 4 * 16(mem_cd), x0, x0; \
	\
	vpxor t6, x1, x1; \
	vpxor 5 * 16(mem_cd), x1, x1; \
	\
	vpxor t5, x2, x2; \
	vpxor 6 * 16(mem_cd), x2, x2; \
	\
	vpxor t4, x3, x3; \
	vpxor 7 * 16(mem_cd), x3, x3;

/*
 * Size optimization... with inlined roundsm16, binary would be over 5 times
 * larger and would only be 0.5% faster (on sandy-bridge).
 */
.align 8
roundsm16_x0_x1_x2_x3_x4_x5_x6_x7_y0_y1_y2_y3_y4_y5_y6_y7_cd:
	roundsm16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		  %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15,
		  %rcx, (%r9));
	ret;
ENDPROC(roundsm16_x0_x1_x2_x3_x4_x5_x6_x7_y0_y1_y2_y3_y4_y5_y6_y7_cd)

.align 8
roundsm16_x4_x5_x6_x7_x0_x1_x2_x3_y4_y5_y6_y7_y0_y1_y2_y3_ab:
	roundsm16(%xmm4, %xmm5, %xmm6, %xmm7, %xmm0, %xmm1, %xmm2, %xmm3,
		  %xmm12, %xmm13, %xmm14, %xmm15, %xmm8, %xmm9, %xmm10, %xmm11,
		  %rax, (%r9));
	ret;
ENDPROC(roundsm16_x4_x5_x6_x7_x0_x1_x2_x3_y4_y5_y6_y7_y0_y1_y2_y3_ab)

/*
 * IN/OUT:
 *  x0..x7: byte-sliced AB state preloaded
 *  mem_ab: byte-sliced AB state in memory
 *  mem_cb: byte-sliced CD state in memory
 */
#define two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, i, dir, store_ab) \
	leaq (key_table + (i) * 8)(CTX), %r9; \
	call roundsm16_x0_x1_x2_x3_x4_x5_x6_x7_y0_y1_y2_y3_y4_y5_y6_y7_cd; \
	\
	vmovdqu x4, 0 * 16(mem_cd); \
	vmovdqu x5, 1 * 16(mem_cd); \
	vmovdqu x6, 2 * 16(mem_cd); \
	vmovdqu x7, 3 * 16(mem_cd); \
	vmovdqu x0, 4 * 16(mem_cd); \
	vmovdqu x1, 5 * 16(mem_cd); \
	vmovdqu x2, 6 * 16(mem_cd); \
	vmovdqu x3, 7 * 16(mem_cd); \
	\
	leaq (key_table + ((i) + (dir)) * 8)(CTX), %r9; \
	call roundsm16_x4_x5_x6_x7_x0_x1_x2_x3_y4_y5_y6_y7_y0_y1_y2_y3_ab; \
	\
	store_ab(x0, x1, x2, x3, x4, x5, x6, x7, mem_ab);

#define dummy_store(x0, x1, x2, x3, x4, x5, x6, x7, mem_ab) /* do nothing */

#define store_ab_state(x0, x1, x2, x3, x4, x5, x6, x7, mem_ab) \
	/* Store new AB state */ \
	vmovdqu x0, 0 * 16(mem_ab); \
	vmovdqu x1, 1 * 16(mem_ab); \
	vmovdqu x2, 2 * 16(mem_ab); \
	vmovdqu x3, 3 * 16(mem_ab); \
	vmovdqu x4, 4 * 16(mem_ab); \
	vmovdqu x5, 5 * 16(mem_ab); \
	vmovdqu x6, 6 * 16(mem_ab); \
	vmovdqu x7, 7 * 16(mem_ab);

#define enc_rounds16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, i) \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 2, 1, store_ab_state); \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 4, 1, store_ab_state); \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 6, 1, dummy_store);

#define dec_rounds16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, i) \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 7, -1, store_ab_state); \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 5, -1, store_ab_state); \
	two_roundsm16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd, (i) + 3, -1, dummy_store);

/*
 * IN:
 *  v0..3: byte-sliced 32-bit integers
 * OUT:
 *  v0..3: (IN <<< 1)
 */
#define rol32_1_16(v0, v1, v2, v3, t0, t1, t2, zero) \
	vpcmpgtb v0, zero, t0; \
	vpaddb v0, v0, v0; \
	vpabsb t0, t0; \
	\
	vpcmpgtb v1, zero, t1; \
	vpaddb v1, v1, v1; \
	vpabsb t1, t1; \
	\
	vpcmpgtb v2, zero, t2; \
	vpaddb v2, v2, v2; \
	vpabsb t2, t2; \
	\
	vpor t0, v1, v1; \
	\
	vpcmpgtb v3, zero, t0; \
	vpaddb v3, v3, v3; \
	vpabsb t0, t0; \
	\
	vpor t1, v2, v2; \
	vpor t2, v3, v3; \
	vpor t0, v0, v0;

/*
 * IN:
 *   r: byte-sliced AB state in memory
 *   l: byte-sliced CD state in memory
 * OUT:
 *   x0..x7: new byte-sliced CD state
 */
#define fls16(l, l0, l1, l2, l3, l4, l5, l6, l7, r, t0, t1, t2, t3, tt0, \
	      tt1, tt2, tt3, kll, klr, krl, krr) \
	/* \
	 * t0 = kll; \
	 * t0 &= ll; \
	 * lr ^= rol32(t0, 1); \
	 */ \
	vpxor tt0, tt0, tt0; \
	vmovd kll, t0; \
	vpshufb tt0, t0, t3; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t2; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t1; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t0; \
	\
	vpand l0, t0, t0; \
	vpand l1, t1, t1; \
	vpand l2, t2, t2; \
	vpand l3, t3, t3; \
	\
	rol32_1_16(t3, t2, t1, t0, tt1, tt2, tt3, tt0); \
	\
	vpxor l4, t0, l4; \
	vmovdqu l4, 4 * 16(l); \
	vpxor l5, t1, l5; \
	vmovdqu l5, 5 * 16(l); \
	vpxor l6, t2, l6; \
	vmovdqu l6, 6 * 16(l); \
	vpxor l7, t3, l7; \
	vmovdqu l7, 7 * 16(l); \
	\
	/* \
	 * t2 = krr; \
	 * t2 |= rr; \
	 * rl ^= t2; \
	 */ \
	\
	vmovd krr, t0; \
	vpshufb tt0, t0, t3; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t2; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t1; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t0; \
	\
	vpor 4 * 16(r), t0, t0; \
	vpor 5 * 16(r), t1, t1; \
	vpor 6 * 16(r), t2, t2; \
	vpor 7 * 16(r), t3, t3; \
	\
	vpxor 0 * 16(r), t0, t0; \
	vpxor 1 * 16(r), t1, t1; \
	vpxor 2 * 16(r), t2, t2; \
	vpxor 3 * 16(r), t3, t3; \
	vmovdqu t0, 0 * 16(r); \
	vmovdqu t1, 1 * 16(r); \
	vmovdqu t2, 2 * 16(r); \
	vmovdqu t3, 3 * 16(r); \
	\
	/* \
	 * t2 = krl; \
	 * t2 &= rl; \
	 * rr ^= rol32(t2, 1); \
	 */ \
	vmovd krl, t0; \
	vpshufb tt0, t0, t3; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t2; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t1; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t0; \
	\
	vpand 0 * 16(r), t0, t0; \
	vpand 1 * 16(r), t1, t1; \
	vpand 2 * 16(r), t2, t2; \
	vpand 3 * 16(r), t3, t3; \
	\
	rol32_1_16(t3, t2, t1, t0, tt1, tt2, tt3, tt0); \
	\
	vpxor 4 * 16(r), t0, t0; \
	vpxor 5 * 16(r), t1, t1; \
	vpxor 6 * 16(r), t2, t2; \
	vpxor 7 * 16(r), t3, t3; \
	vmovdqu t0, 4 * 16(r); \
	vmovdqu t1, 5 * 16(r); \
	vmovdqu t2, 6 * 16(r); \
	vmovdqu t3, 7 * 16(r); \
	\
	/* \
	 * t0 = klr; \
	 * t0 |= lr; \
	 * ll ^= t0; \
	 */ \
	\
	vmovd klr, t0; \
	vpshufb tt0, t0, t3; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t2; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t1; \
	vpsrldq $1, t0, t0; \
	vpshufb tt0, t0, t0; \
	\
	vpor l4, t0, t0; \
	vpor l5, t1, t1; \
	vpor l6, t2, t2; \
	vpor l7, t3, t3; \
	\
	vpxor l0, t0, l0; \
	vmovdqu l0, 0 * 16(l); \
	vpxor l1, t1, l1; \
	vmovdqu l1, 1 * 16(l); \
	vpxor l2, t2, l2; \
	vmovdqu l2, 2 * 16(l); \
	vpxor l3, t3, l3; \
	vmovdqu l3, 3 * 16(l);

#define transpose_4x4(x0, x1, x2, x3, t1, t2) \
	vpunpckhdq x1, x0, t2; \
	vpunpckldq x1, x0, x0; \
	\
	vpunpckldq x3, x2, t1; \
	vpunpckhdq x3, x2, x2; \
	\
	vpunpckhqdq t1, x0, x1; \
	vpunpcklqdq t1, x0, x0; \
	\
	vpunpckhqdq x2, t2, x3; \
	vpunpcklqdq x2, t2, x2;

#define byteslice_16x16b(a0, b0, c0, d0, a1, b1, c1, d1, a2, b2, c2, d2, a3, \
			 b3, c3, d3, st0, st1) \
	vmovdqu d2, st0; \
	vmovdqu d3, st1; \
	transpose_4x4(a0, a1, a2, a3, d2, d3); \
	transpose_4x4(b0, b1, b2, b3, d2, d3); \
	vmovdqu st0, d2; \
	vmovdqu st1, d3; \
	\
	vmovdqu a0, st0; \
	vmovdqu a1, st1; \
	transpose_4x4(c0, c1, c2, c3, a0, a1); \
	transpose_4x4(d0, d1, d2, d3, a0, a1); \
	\
	vmovdqu .Lshufb_16x16b, a0; \
	vmovdqu st1, a1; \
	vpshufb a0, a2, a2; \
	vpshufb a0, a3, a3; \
	vpshufb a0, b0, b0; \
	vpshufb a0, b1, b1; \
	vpshufb a0, b2, b2; \
	vpshufb a0, b3, b3; \
	vpshufb a0, a1, a1; \
	vpshufb a0, c0, c0; \
	vpshufb a0, c1, c1; \
	vpshufb a0, c2, c2; \
	vpshufb a0, c3, c3; \
	vpshufb a0, d0, d0; \
	vpshufb a0, d1, d1; \
	vpshufb a0, d2, d2; \
	vpshufb a0, d3, d3; \
	vmovdqu d3, st1; \
	vmovdqu st0, d3; \
	vpshufb a0, d3, a0; \
	vmovdqu d2, st0; \
	\
	transpose_4x4(a0, b0, c0, d0, d2, d3); \
	transpose_4x4(a1, b1, c1, d1, d2, d3); \
	vmovdqu st0, d2; \
	vmovdqu st1, d3; \
	\
	vmovdqu b0, st0; \
	vmovdqu b1, st1; \
	transpose_4x4(a2, b2, c2, d2, b0, b1); \
	transpose_4x4(a3, b3, c3, d3, b0, b1); \
	vmovdqu st0, b0; \
	vmovdqu st1, b1; \
	/* does not adjust output bytes inside vectors */

/* load blocks to registers and apply pre-whitening */
#define inpack16_pre(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		     y6, y7, rio, key) \
	vmovq key, x0; \
	vpshufb .Lpack_bswap, x0, x0; \
	\
	vpxor 0 * 16(rio), x0, y7; \
	vpxor 1 * 16(rio), x0, y6; \
	vpxor 2 * 16(rio), x0, y5; \
	vpxor 3 * 16(rio), x0, y4; \
	vpxor 4 * 16(rio), x0, y3; \
	vpxor 5 * 16(rio), x0, y2; \
	vpxor 6 * 16(rio), x0, y1; \
	vpxor 7 * 16(rio), x0, y0; \
	vpxor 8 * 16(rio), x0, x7; \
	vpxor 9 * 16(rio), x0, x6; \
	vpxor 10 * 16(rio), x0, x5; \
	vpxor 11 * 16(rio), x0, x4; \
	vpxor 12 * 16(rio), x0, x3; \
	vpxor 13 * 16(rio), x0, x2; \
	vpxor 14 * 16(rio), x0, x1; \
	vpxor 15 * 16(rio), x0, x0;

/* byteslice pre-whitened blocks and store to temporary memory */
#define inpack16_post(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		      y6, y7, mem_ab, mem_cd) \
	byteslice_16x16b(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, \
			 y5, y6, y7, (mem_ab), (mem_cd)); \
	\
	vmovdqu x0, 0 * 16(mem_ab); \
	vmovdqu x1, 1 * 16(mem_ab); \
	vmovdqu x2, 2 * 16(mem_ab); \
	vmovdqu x3, 3 * 16(mem_ab); \
	vmovdqu x4, 4 * 16(mem_ab); \
	vmovdqu x5, 5 * 16(mem_ab); \
	vmovdqu x6, 6 * 16(mem_ab); \
	vmovdqu x7, 7 * 16(mem_ab); \
	vmovdqu y0, 0 * 16(mem_cd); \
	vmovdqu y1, 1 * 16(mem_cd); \
	vmovdqu y2, 2 * 16(mem_cd); \
	vmovdqu y3, 3 * 16(mem_cd); \
	vmovdqu y4, 4 * 16(mem_cd); \
	vmovdqu y5, 5 * 16(mem_cd); \
	vmovdqu y6, 6 * 16(mem_cd); \
	vmovdqu y7, 7 * 16(mem_cd);

/* de-byteslice, apply post-whitening and store blocks */
#define outunpack16(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, \
		    y5, y6, y7, key, stack_tmp0, stack_tmp1) \
	byteslice_16x16b(y0, y4, x0, x4, y1, y5, x1, x5, y2, y6, x2, x6, y3, \
			 y7, x3, x7, stack_tmp0, stack_tmp1); \
	\
	vmovdqu x0, stack_tmp0; \
	\
	vmovq key, x0; \
	vpshufb .Lpack_bswap, x0, x0; \
	\
	vpxor x0, y7, y7; \
	vpxor x0, y6, y6; \
	vpxor x0, y5, y5; \
	vpxor x0, y4, y4; \
	vpxor x0, y3, y3; \
	vpxor x0, y2, y2; \
	vpxor x0, y1, y1; \
	vpxor x0, y0, y0; \
	vpxor x0, x7, x7; \
	vpxor x0, x6, x6; \
	vpxor x0, x5, x5; \
	vpxor x0, x4, x4; \
	vpxor x0, x3, x3; \
	vpxor x0, x2, x2; \
	vpxor x0, x1, x1; \
	vpxor stack_tmp0, x0, x0;

#define write_output(x0, x1, x2, x3, x4, x5, x6, x7, y0, y1, y2, y3, y4, y5, \
		     y6, y7, rio) \
	vmovdqu x0, 0 * 16(rio); \
	vmovdqu x1, 1 * 16(rio); \
	vmovdqu x2, 2 * 16(rio); \
	vmovdqu x3, 3 * 16(rio); \
	vmovdqu x4, 4 * 16(rio); \
	vmovdqu x5, 5 * 16(rio); \
	vmovdqu x6, 6 * 16(rio); \
	vmovdqu x7, 7 * 16(rio); \
	vmovdqu y0, 8 * 16(rio); \
	vmovdqu y1, 9 * 16(rio); \
	vmovdqu y2, 10 * 16(rio); \
	vmovdqu y3, 11 * 16(rio); \
	vmovdqu y4, 12 * 16(rio); \
	vmovdqu y5, 13 * 16(rio); \
	vmovdqu y6, 14 * 16(rio); \
	vmovdqu y7, 15 * 16(rio);

.data
.align 16

#define SHUFB_BYTES(idx) \
	0 + (idx), 4 + (idx), 8 + (idx), 12 + (idx)

.Lshufb_16x16b:
	.byte SHUFB_BYTES(0), SHUFB_BYTES(1), SHUFB_BYTES(2), SHUFB_BYTES(3);

.Lpack_bswap:
	.long 0x00010203
	.long 0x04050607
	.long 0x80808080
	.long 0x80808080

/* For CTR-mode IV byteswap */
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

/* For XTS mode IV generation */
.Lxts_gf128mul_and_shl1_mask:
	.byte 0x87, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0

/*
 * pre-SubByte transform
 *
 * pre-lookup for sbox1, sbox2, sbox3:
 *   swap_bitendianness(
 *       isom_map_camellia_to_aes(
 *           camellia_f(
 *               swap_bitendianess(in)
 *           )
 *       )
 *   )
 *
 * (note: '⊕ 0xc5' inside camellia_f())
 */
.Lpre_tf_lo_s1:
	.byte 0x45, 0xe8, 0x40, 0xed, 0x2e, 0x83, 0x2b, 0x86
	.byte 0x4b, 0xe6, 0x4e, 0xe3, 0x20, 0x8d, 0x25, 0x88
.Lpre_tf_hi_s1:
	.byte 0x00, 0x51, 0xf1, 0xa0, 0x8a, 0xdb, 0x7b, 0x2a
	.byte 0x09, 0x58, 0xf8, 0xa9, 0x83, 0xd2, 0x72, 0x23

/*
 * pre-SubByte transform
 *
 * pre-lookup for sbox4:
 *   swap_bitendianness(
 *       isom_map_camellia_to_aes(
 *           camellia_f(
 *               swap_bitendianess(in <<< 1)
 *           )
 *       )
 *   )
 *
 * (note: '⊕ 0xc5' inside camellia_f())
 */
.Lpre_tf_lo_s4:
	.byte 0x45, 0x40, 0x2e, 0x2b, 0x4b, 0x4e, 0x20, 0x25
	.byte 0x14, 0x11, 0x7f, 0x7a, 0x1a, 0x1f, 0x71, 0x74
.Lpre_tf_hi_s4:
	.byte 0x00, 0xf1, 0x8a, 0x7b, 0x09, 0xf8, 0x83, 0x72
	.byte 0xad, 0x5c, 0x27, 0xd6, 0xa4, 0x55, 0x2e, 0xdf

/*
 * post-SubByte transform
 *
 * post-lookup for sbox1, sbox4:
 *  swap_bitendianness(
 *      camellia_h(
 *          isom_map_aes_to_camellia(
 *              swap_bitendianness(
 *                  aes_inverse_affine_transform(in)
 *              )
 *          )
 *      )
 *  )
 *
 * (note: '⊕ 0x6e' inside camellia_h())
 */
.Lpost_tf_lo_s1:
	.byte 0x3c, 0xcc, 0xcf, 0x3f, 0x32, 0xc2, 0xc1, 0x31
	.byte 0xdc, 0x2c, 0x2f, 0xdf, 0xd2, 0x22, 0x21, 0xd1
.Lpost_tf_hi_s1:
	.byte 0x00, 0xf9, 0x86, 0x7f, 0xd7, 0x2e, 0x51, 0xa8
	.byte 0xa4, 0x5d, 0x22, 0xdb, 0x73, 0x8a, 0xf5, 0x0c

/*
 * post-SubByte transform
 *
 * post-lookup for sbox2:
 *  swap_bitendianness(
 *      camellia_h(
 *          isom_map_aes_to_camellia(
 *              swap_bitendianness(
 *                  aes_inverse_affine_transform(in)
 *              )
 *          )
 *      )
 *  ) <<< 1
 *
 * (note: '⊕ 0x6e' inside camellia_h())
 */
.Lpost_tf_lo_s2:
	.byte 0x78, 0x99, 0x9f, 0x7e, 0x64, 0x85, 0x83, 0x62
	.byte 0xb9, 0x58, 0x5e, 0xbf, 0xa5, 0x44, 0x42, 0xa3
.Lpost_tf_hi_s2:
	.byte 0x00, 0xf3, 0x0d, 0xfe, 0xaf, 0x5c, 0xa2, 0x51
	.byte 0x49, 0xba, 0x44, 0xb7, 0xe6, 0x15, 0xeb, 0x18

/*
 * post-SubByte transform
 *
 * post-lookup for sbox3:
 *  swap_bitendianness(
 *      camellia_h(
 *          isom_map_aes_to_camellia(
 *              swap_bitendianness(
 *                  aes_inverse_affine_transform(in)
 *              )
 *          )
 *      )
 *  ) >>> 1
 *
 * (note: '⊕ 0x6e' inside camellia_h())
 */
.Lpost_tf_lo_s3:
	.byte 0x1e, 0x66, 0xe7, 0x9f, 0x19, 0x61, 0xe0, 0x98
	.byte 0x6e, 0x16, 0x97, 0xef, 0x69, 0x11, 0x90, 0xe8
.Lpost_tf_hi_s3:
	.byte 0x00, 0xfc, 0x43, 0xbf, 0xeb, 0x17, 0xa8, 0x54
	.byte 0x52, 0xae, 0x11, 0xed, 0xb9, 0x45, 0xfa, 0x06

/* For isolating SubBytes from AESENCLAST, inverse shift row */
.Linv_shift_row:
	.byte 0x00, 0x0d, 0x0a, 0x07, 0x04, 0x01, 0x0e, 0x0b
	.byte 0x08, 0x05, 0x02, 0x0f, 0x0c, 0x09, 0x06, 0x03

/* 4-bit mask */
.align 4
.L0f0f0f0f:
	.long 0x0f0f0f0f

.text

.align 8
__camellia_enc_blk16:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rax: temporary storage, 256 bytes
	 *	%xmm0..%xmm15: 16 plaintext blocks
	 * output:
	 *	%xmm0..%xmm15: 16 encrypted blocks, order swapped:
	 *       7, 8, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8
	 */

	leaq 8 * 16(%rax), %rcx;

	inpack16_post(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		      %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		      %xmm15, %rax, %rcx);

	enc_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 0);

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (8) * 8) + 0)(CTX),
	      ((key_table + (8) * 8) + 4)(CTX),
	      ((key_table + (8) * 8) + 8)(CTX),
	      ((key_table + (8) * 8) + 12)(CTX));

	enc_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 8);

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (16) * 8) + 0)(CTX),
	      ((key_table + (16) * 8) + 4)(CTX),
	      ((key_table + (16) * 8) + 8)(CTX),
	      ((key_table + (16) * 8) + 12)(CTX));

	enc_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 16);

	movl $24, %r8d;
	cmpl $16, key_length(CTX);
	jne .Lenc_max32;

.Lenc_done:
	/* load CD for output */
	vmovdqu 0 * 16(%rcx), %xmm8;
	vmovdqu 1 * 16(%rcx), %xmm9;
	vmovdqu 2 * 16(%rcx), %xmm10;
	vmovdqu 3 * 16(%rcx), %xmm11;
	vmovdqu 4 * 16(%rcx), %xmm12;
	vmovdqu 5 * 16(%rcx), %xmm13;
	vmovdqu 6 * 16(%rcx), %xmm14;
	vmovdqu 7 * 16(%rcx), %xmm15;

	outunpack16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		    %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		    %xmm15, (key_table)(CTX, %r8, 8), (%rax), 1 * 16(%rax));

	ret;

.align 8
.Lenc_max32:
	movl $32, %r8d;

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (24) * 8) + 0)(CTX),
	      ((key_table + (24) * 8) + 4)(CTX),
	      ((key_table + (24) * 8) + 8)(CTX),
	      ((key_table + (24) * 8) + 12)(CTX));

	enc_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 24);

	jmp .Lenc_done;
ENDPROC(__camellia_enc_blk16)

.align 8
__camellia_dec_blk16:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rax: temporary storage, 256 bytes
	 *	%r8d: 24 for 16 byte key, 32 for larger
	 *	%xmm0..%xmm15: 16 encrypted blocks
	 * output:
	 *	%xmm0..%xmm15: 16 plaintext blocks, order swapped:
	 *       7, 8, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8
	 */

	leaq 8 * 16(%rax), %rcx;

	inpack16_post(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		      %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		      %xmm15, %rax, %rcx);

	cmpl $32, %r8d;
	je .Ldec_max32;

.Ldec_max24:
	dec_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 16);

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (16) * 8) + 8)(CTX),
	      ((key_table + (16) * 8) + 12)(CTX),
	      ((key_table + (16) * 8) + 0)(CTX),
	      ((key_table + (16) * 8) + 4)(CTX));

	dec_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 8);

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (8) * 8) + 8)(CTX),
	      ((key_table + (8) * 8) + 12)(CTX),
	      ((key_table + (8) * 8) + 0)(CTX),
	      ((key_table + (8) * 8) + 4)(CTX));

	dec_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 0);

	/* load CD for output */
	vmovdqu 0 * 16(%rcx), %xmm8;
	vmovdqu 1 * 16(%rcx), %xmm9;
	vmovdqu 2 * 16(%rcx), %xmm10;
	vmovdqu 3 * 16(%rcx), %xmm11;
	vmovdqu 4 * 16(%rcx), %xmm12;
	vmovdqu 5 * 16(%rcx), %xmm13;
	vmovdqu 6 * 16(%rcx), %xmm14;
	vmovdqu 7 * 16(%rcx), %xmm15;

	outunpack16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		    %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		    %xmm15, (key_table)(CTX), (%rax), 1 * 16(%rax));

	ret;

.align 8
.Ldec_max32:
	dec_rounds16(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rax, %rcx, 24);

	fls16(%rax, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
	      %rcx, %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
	      %xmm15,
	      ((key_table + (24) * 8) + 8)(CTX),
	      ((key_table + (24) * 8) + 12)(CTX),
	      ((key_table + (24) * 8) + 0)(CTX),
	      ((key_table + (24) * 8) + 4)(CTX));

	jmp .Ldec_max24;
ENDPROC(__camellia_dec_blk16)

ENTRY(camellia_ecb_enc_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 */

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx, (key_table)(CTX));

	/* now dst can be used as temporary buffer (even in src == dst case) */
	movq	%rsi, %rax;

	call __camellia_enc_blk16;

	write_output(%xmm7, %xmm6, %xmm5, %xmm4, %xmm3, %xmm2, %xmm1, %xmm0,
		     %xmm15, %xmm14, %xmm13, %xmm12, %xmm11, %xmm10, %xmm9,
		     %xmm8, %rsi);

	ret;
ENDPROC(camellia_ecb_enc_16way)

ENTRY(camellia_ecb_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 */

	cmpl $16, key_length(CTX);
	movl $32, %r8d;
	movl $24, %eax;
	cmovel %eax, %r8d; /* max */

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx, (key_table)(CTX, %r8, 8));

	/* now dst can be used as temporary buffer (even in src == dst case) */
	movq	%rsi, %rax;

	call __camellia_dec_blk16;

	write_output(%xmm7, %xmm6, %xmm5, %xmm4, %xmm3, %xmm2, %xmm1, %xmm0,
		     %xmm15, %xmm14, %xmm13, %xmm12, %xmm11, %xmm10, %xmm9,
		     %xmm8, %rsi);

	ret;
ENDPROC(camellia_ecb_dec_16way)

ENTRY(camellia_cbc_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 */

	cmpl $16, key_length(CTX);
	movl $32, %r8d;
	movl $24, %eax;
	cmovel %eax, %r8d; /* max */

	inpack16_pre(%xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7,
		     %xmm8, %xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14,
		     %xmm15, %rdx, (key_table)(CTX, %r8, 8));

	/*
	 * dst might still be in-use (in case dst == src), so use stack for
	 * temporary storage.
	 */
	subq $(16 * 16), %rsp;
	movq %rsp, %rax;

	call __camellia_dec_blk16;

	addq $(16 * 16), %rsp;

	vpxor (0 * 16)(%rdx), %xmm6, %xmm6;
	vpxor (1 * 16)(%rdx), %xmm5, %xmm5;
	vpxor (2 * 16)(%rdx), %xmm4, %xmm4;
	vpxor (3 * 16)(%rdx), %xmm3, %xmm3;
	vpxor (4 * 16)(%rdx), %xmm2, %xmm2;
	vpxor (5 * 16)(%rdx), %xmm1, %xmm1;
	vpxor (6 * 16)(%rdx), %xmm0, %xmm0;
	vpxor (7 * 16)(%rdx), %xmm15, %xmm15;
	vpxor (8 * 16)(%rdx), %xmm14, %xmm14;
	vpxor (9 * 16)(%rdx), %xmm13, %xmm13;
	vpxor (10 * 16)(%rdx), %xmm12, %xmm12;
	vpxor (11 * 16)(%rdx), %xmm11, %xmm11;
	vpxor (12 * 16)(%rdx), %xmm10, %xmm10;
	vpxor (13 * 16)(%rdx), %xmm9, %xmm9;
	vpxor (14 * 16)(%rdx), %xmm8, %xmm8;
	write_output(%xmm7, %xmm6, %xmm5, %xmm4, %xmm3, %xmm2, %xmm1, %xmm0,
		     %xmm15, %xmm14, %xmm13, %xmm12, %xmm11, %xmm10, %xmm9,
		     %xmm8, %rsi);

	ret;
ENDPROC(camellia_cbc_dec_16way)

#define inc_le128(x, minus_one, tmp) \
	vpcmpeqq minus_one, x, tmp; \
	vpsubq minus_one, x, x; \
	vpslldq $8, tmp, tmp; \
	vpsubq tmp, x, x;

ENTRY(camellia_ctr_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (little endian, 128bit)
	 */

	subq $(16 * 16), %rsp;
	movq %rsp, %rax;

	vmovdqa .Lbswap128_mask, %xmm14;

	/* load IV and byteswap */
	vmovdqu (%rcx), %xmm0;
	vpshufb %xmm14, %xmm0, %xmm15;
	vmovdqu %xmm15, 15 * 16(%rax);

	vpcmpeqd %xmm15, %xmm15, %xmm15;
	vpsrldq $8, %xmm15, %xmm15; /* low: -1, high: 0 */

	/* construct IVs */
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm13;
	vmovdqu %xmm13, 14 * 16(%rax);
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm13;
	vmovdqu %xmm13, 13 * 16(%rax);
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm12;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm11;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm10;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm9;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm8;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm7;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm6;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm5;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm4;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm3;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm2;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vpshufb %xmm14, %xmm0, %xmm1;
	inc_le128(%xmm0, %xmm15, %xmm13);
	vmovdqa %xmm0, %xmm13;
	vpshufb %xmm14, %xmm0, %xmm0;
	inc_le128(%xmm13, %xmm15, %xmm14);
	vmovdqu %xmm13, (%rcx);

	/* inpack16_pre: */
	vmovq (key_table)(CTX), %xmm15;
	vpshufb .Lpack_bswap, %xmm15, %xmm15;
	vpxor %xmm0, %xmm15, %xmm0;
	vpxor %xmm1, %xmm15, %xmm1;
	vpxor %xmm2, %xmm15, %xmm2;
	vpxor %xmm3, %xmm15, %xmm3;
	vpxor %xmm4, %xmm15, %xmm4;
	vpxor %xmm5, %xmm15, %xmm5;
	vpxor %xmm6, %xmm15, %xmm6;
	vpxor %xmm7, %xmm15, %xmm7;
	vpxor %xmm8, %xmm15, %xmm8;
	vpxor %xmm9, %xmm15, %xmm9;
	vpxor %xmm10, %xmm15, %xmm10;
	vpxor %xmm11, %xmm15, %xmm11;
	vpxor %xmm12, %xmm15, %xmm12;
	vpxor 13 * 16(%rax), %xmm15, %xmm13;
	vpxor 14 * 16(%rax), %xmm15, %xmm14;
	vpxor 15 * 16(%rax), %xmm15, %xmm15;

	call __camellia_enc_blk16;

	addq $(16 * 16), %rsp;

	vpxor 0 * 16(%rdx), %xmm7, %xmm7;
	vpxor 1 * 16(%rdx), %xmm6, %xmm6;
	vpxor 2 * 16(%rdx), %xmm5, %xmm5;
	vpxor 3 * 16(%rdx), %xmm4, %xmm4;
	vpxor 4 * 16(%rdx), %xmm3, %xmm3;
	vpxor 5 * 16(%rdx), %xmm2, %xmm2;
	vpxor 6 * 16(%rdx), %xmm1, %xmm1;
	vpxor 7 * 16(%rdx), %xmm0, %xmm0;
	vpxor 8 * 16(%rdx), %xmm15, %xmm15;
	vpxor 9 * 16(%rdx), %xmm14, %xmm14;
	vpxor 10 * 16(%rdx), %xmm13, %xmm13;
	vpxor 11 * 16(%rdx), %xmm12, %xmm12;
	vpxor 12 * 16(%rdx), %xmm11, %xmm11;
	vpxor 13 * 16(%rdx), %xmm10, %xmm10;
	vpxor 14 * 16(%rdx), %xmm9, %xmm9;
	vpxor 15 * 16(%rdx), %xmm8, %xmm8;
	write_output(%xmm7, %xmm6, %xmm5, %xmm4, %xmm3, %xmm2, %xmm1, %xmm0,
		     %xmm15, %xmm14, %xmm13, %xmm12, %xmm11, %xmm10, %xmm9,
		     %xmm8, %rsi);

	ret;
ENDPROC(camellia_ctr_16way)

#define gf128mul_x_ble(iv, mask, tmp) \
	vpsrad $31, iv, tmp; \
	vpaddq iv, iv, iv; \
	vpshufd $0x13, tmp, tmp; \
	vpand mask, tmp, tmp; \
	vpxor tmp, iv, iv;

.align 8
camellia_xts_crypt_16way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 *	%r8: index for input whitening key
	 *	%r9: pointer to  __camellia_enc_blk16 or __camellia_dec_blk16
	 */

	subq $(16 * 16), %rsp;
	movq %rsp, %rax;

	vmovdqa .Lxts_gf128mul_and_shl1_mask, %xmm14;

	/* load IV */
	vmovdqu (%rcx), %xmm0;
	vpxor 0 * 16(%rdx), %xmm0, %xmm15;
	vmovdqu %xmm15, 15 * 16(%rax);
	vmovdqu %xmm0, 0 * 16(%rsi);

	/* construct IVs */
	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 1 * 16(%rdx), %xmm0, %xmm15;
	vmovdqu %xmm15, 14 * 16(%rax);
	vmovdqu %xmm0, 1 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 2 * 16(%rdx), %xmm0, %xmm13;
	vmovdqu %xmm0, 2 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 3 * 16(%rdx), %xmm0, %xmm12;
	vmovdqu %xmm0, 3 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 4 * 16(%rdx), %xmm0, %xmm11;
	vmovdqu %xmm0, 4 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 5 * 16(%rdx), %xmm0, %xmm10;
	vmovdqu %xmm0, 5 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 6 * 16(%rdx), %xmm0, %xmm9;
	vmovdqu %xmm0, 6 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 7 * 16(%rdx), %xmm0, %xmm8;
	vmovdqu %xmm0, 7 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 8 * 16(%rdx), %xmm0, %xmm7;
	vmovdqu %xmm0, 8 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 9 * 16(%rdx), %xmm0, %xmm6;
	vmovdqu %xmm0, 9 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 10 * 16(%rdx), %xmm0, %xmm5;
	vmovdqu %xmm0, 10 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 11 * 16(%rdx), %xmm0, %xmm4;
	vmovdqu %xmm0, 11 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 12 * 16(%rdx), %xmm0, %xmm3;
	vmovdqu %xmm0, 12 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 13 * 16(%rdx), %xmm0, %xmm2;
	vmovdqu %xmm0, 13 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 14 * 16(%rdx), %xmm0, %xmm1;
	vmovdqu %xmm0, 14 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vpxor 15 * 16(%rdx), %xmm0, %xmm15;
	vmovdqu %xmm15, 0 * 16(%rax);
	vmovdqu %xmm0, 15 * 16(%rsi);

	gf128mul_x_ble(%xmm0, %xmm14, %xmm15);
	vmovdqu %xmm0, (%rcx);

	/* inpack16_pre: */
	vmovq (key_table)(CTX, %r8, 8), %xmm15;
	vpshufb .Lpack_bswap, %xmm15, %xmm15;
	vpxor 0 * 16(%rax), %xmm15, %xmm0;
	vpxor %xmm1, %xmm15, %xmm1;
	vpxor %xmm2, %xmm15, %xmm2;
	vpxor %xmm3, %xmm15, %xmm3;
	vpxor %xmm4, %xmm15, %xmm4;
	vpxor %xmm5, %xmm15, %xmm5;
	vpxor %xmm6, %xmm15, %xmm6;
	vpxor %xmm7, %xmm15, %xmm7;
	vpxor %xmm8, %xmm15, %xmm8;
	vpxor %xmm9, %xmm15, %xmm9;
	vpxor %xmm10, %xmm15, %xmm10;
	vpxor %xmm11, %xmm15, %xmm11;
	vpxor %xmm12, %xmm15, %xmm12;
	vpxor %xmm13, %xmm15, %xmm13;
	vpxor 14 * 16(%rax), %xmm15, %xmm14;
	vpxor 15 * 16(%rax), %xmm15, %xmm15;

	CALL_NOSPEC %r9;

	addq $(16 * 16), %rsp;

	vpxor 0 * 16(%rsi), %xmm7, %xmm7;
	vpxor 1 * 16(%rsi), %xmm6, %xmm6;
	vpxor 2 * 16(%rsi), %xmm5, %xmm5;
	vpxor 3 * 16(%rsi), %xmm4, %xmm4;
	vpxor 4 * 16(%rsi), %xmm3, %xmm3;
	vpxor 5 * 16(%rsi), %xmm2, %xmm2;
	vpxor 6 * 16(%rsi), %xmm1, %xmm1;
	vpxor 7 * 16(%rsi), %xmm0, %xmm0;
	vpxor 8 * 16(%rsi), %xmm15, %xmm15;
	vpxor 9 * 16(%rsi), %xmm14, %xmm14;
	vpxor 10 * 16(%rsi), %xmm13, %xmm13;
	vpxor 11 * 16(%rsi), %xmm12, %xmm12;
	vpxor 12 * 16(%rsi), %xmm11, %xmm11;
	vpxor 13 * 16(%rsi), %xmm10, %xmm10;
	vpxor 14 * 16(%rsi), %xmm9, %xmm9;
	vpxor 15 * 16(%rsi), %xmm8, %xmm8;
	write_output(%xmm7, %xmm6, %xmm5, %xmm4, %xmm3, %xmm2, %xmm1, %xmm0,
		     %xmm15, %xmm14, %xmm13, %xmm12, %xmm11, %xmm10, %xmm9,
		     %xmm8, %rsi);

	ret;
ENDPROC(camellia_xts_crypt_16way)

ENTRY(camellia_xts_enc_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	xorl %r8d, %r8d; /* input whitening key, 0 for enc */

	leaq __camellia_enc_blk16, %r9;

	jmp camellia_xts_crypt_16way;
ENDPROC(camellia_xts_enc_16way)

ENTRY(camellia_xts_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */

	cmpl $16, key_length(CTX);
	movl $32, %r8d;
	movl $24, %eax;
	cmovel %eax, %r8d;  /* input whitening key, last for dec */

	leaq __camellia_dec_blk16, %r9;

	jmp camellia_xts_crypt_16way;
ENDPROC(camellia_xts_dec_16way)
