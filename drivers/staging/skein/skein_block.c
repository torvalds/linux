/*
 ***********************************************************************
 *
 * Implementation of the Skein block functions.
 *
 * Source code author: Doug Whiting, 2008.
 *
 * This algorithm and source code is released to the public domain.
 *
 * Compile-time switches:
 *
 *  SKEIN_USE_ASM  -- set bits (256/512/1024) to select which
 *                    versions use ASM code for block processing
 *                    [default: use C for all block sizes]
 *
 ***********************************************************************
 */

#include <linux/string.h>
#include <linux/bitops.h>
#include "skein_base.h"
#include "skein_block.h"

/*****************************  SKEIN_256 ******************************/
#if !(SKEIN_USE_ASM & 256)
void skein_256_process_block(struct skein_256_ctx *ctx, const u8 *blk_ptr,
			     size_t blk_cnt, size_t byte_cnt_add)
{ /* do it in C */
	enum {
		WCNT = SKEIN_256_STATE_WORDS
	};
	size_t r;
#if SKEIN_UNROLL_256
	/* key schedule: chaining vars + tweak + "rot"*/
	u64  kw[WCNT + 4 + (RCNT * 2)];
#else
	/* key schedule words : chaining vars + tweak */
	u64  kw[WCNT + 4];
#endif
	u64  X0, X1, X2, X3; /* local copy of context vars, for speed */
	u64  w[WCNT]; /* local copy of input block */
#ifdef SKEIN_DEBUG
	const u64 *X_ptr[4]; /* use for debugging (help cc put Xn in regs) */

	X_ptr[0] = &X0;
	X_ptr[1] = &X1;
	X_ptr[2] = &X2;
	X_ptr[3] = &X3;
#endif
	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	ts[0] = ctx->h.tweak[0];
	ts[1] = ctx->h.tweak[1];
	do  {
		/*
		 * this implementation only supports 2**64 input bytes
		 * (no carry out here)
		 */
		ts[0] += byte_cnt_add; /* update processed length */

		/* precompute the key schedule for this block */
		ks[0] = ctx->x[0];
		ks[1] = ctx->x[1];
		ks[2] = ctx->x[2];
		ks[3] = ctx->x[3];
		ks[4] = ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^ SKEIN_KS_PARITY;

		ts[2] = ts[0] ^ ts[1];

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, WCNT);
		debug_save_tweak(ctx);

		/* do the first full key injection */
		X0 = w[0] + ks[0];
		X1 = w[1] + ks[1] + ts[0];
		X2 = w[2] + ks[2] + ts[1];
		X3 = w[3] + ks[3];

		blk_ptr += SKEIN_256_BLOCK_BYTES;

		/* run the rounds */
		for (r = 1;
			r < (SKEIN_UNROLL_256 ? 2 * RCNT : 2);
			r += (SKEIN_UNROLL_256 ? 2 * SKEIN_UNROLL_256 : 1)) {
			R256_8_ROUNDS(0);
#if   R256_UNROLL_R(1)
			R256_8_ROUNDS(1);
#endif
#if   R256_UNROLL_R(2)
			R256_8_ROUNDS(2);
#endif
#if   R256_UNROLL_R(3)
			R256_8_ROUNDS(3);
#endif
#if   R256_UNROLL_R(4)
			R256_8_ROUNDS(4);
#endif
#if   R256_UNROLL_R(5)
			R256_8_ROUNDS(5);
#endif
#if   R256_UNROLL_R(6)
			R256_8_ROUNDS(6);
#endif
#if   R256_UNROLL_R(7)
			R256_8_ROUNDS(7);
#endif
#if   R256_UNROLL_R(8)
			R256_8_ROUNDS(8);
#endif
#if   R256_UNROLL_R(9)
			R256_8_ROUNDS(9);
#endif
#if   R256_UNROLL_R(10)
			R256_8_ROUNDS(10);
#endif
#if   R256_UNROLL_R(11)
			R256_8_ROUNDS(11);
#endif
#if   R256_UNROLL_R(12)
			R256_8_ROUNDS(12);
#endif
#if   R256_UNROLL_R(13)
			R256_8_ROUNDS(13);
#endif
#if   R256_UNROLL_R(14)
			R256_8_ROUNDS(14);
#endif
		}
		/* do the final "feedforward" xor, update context chaining */
		ctx->x[0] = X0 ^ w[0];
		ctx->x[1] = X1 ^ w[1];
		ctx->x[2] = X2 ^ w[2];
		ctx->x[3] = X3 ^ w[3];

		ts[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blk_cnt);
	ctx->h.tweak[0] = ts[0];
	ctx->h.tweak[1] = ts[1];
}

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t skein_256_process_block_code_size(void)
{
	return ((u8 *)skein_256_process_block_code_size) -
		((u8 *)skein_256_process_block);
}

unsigned int skein_256_unroll_cnt(void)
{
	return SKEIN_UNROLL_256;
}
#endif
#endif

/*****************************  SKEIN_512 ******************************/
#if !(SKEIN_USE_ASM & 512)
void skein_512_process_block(struct skein_512_ctx *ctx, const u8 *blk_ptr,
			     size_t blk_cnt, size_t byte_cnt_add)
{ /* do it in C */
	enum {
		WCNT = SKEIN_512_STATE_WORDS
	};
	size_t  r;
#if SKEIN_UNROLL_512
	/* key sched: chaining vars + tweak + "rot"*/
	u64  kw[WCNT + 4 + RCNT * 2];
#else
	/* key schedule words : chaining vars + tweak */
	u64  kw[WCNT + 4];
#endif
	u64  X0, X1, X2, X3, X4, X5, X6, X7; /* local copies, for speed */
	u64  w[WCNT]; /* local copy of input block */
#ifdef SKEIN_DEBUG
	const u64 *X_ptr[8]; /* use for debugging (help cc put Xn in regs) */

	X_ptr[0] = &X0;
	X_ptr[1] = &X1;
	X_ptr[2] = &X2;
	X_ptr[3] = &X3;
	X_ptr[4] = &X4;
	X_ptr[5] = &X5;
	X_ptr[6] = &X6;
	X_ptr[7] = &X7;
#endif

	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	ts[0] = ctx->h.tweak[0];
	ts[1] = ctx->h.tweak[1];
	do  {
		/*
		 * this implementation only supports 2**64 input bytes
		 * (no carry out here)
		 */
		ts[0] += byte_cnt_add; /* update processed length */

		/* precompute the key schedule for this block */
		ks[0] = ctx->x[0];
		ks[1] = ctx->x[1];
		ks[2] = ctx->x[2];
		ks[3] = ctx->x[3];
		ks[4] = ctx->x[4];
		ks[5] = ctx->x[5];
		ks[6] = ctx->x[6];
		ks[7] = ctx->x[7];
		ks[8] = ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^
			ks[4] ^ ks[5] ^ ks[6] ^ ks[7] ^ SKEIN_KS_PARITY;

		ts[2] = ts[0] ^ ts[1];

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, WCNT);
		debug_save_tweak(ctx);

		/* do the first full key injection */
		X0 = w[0] + ks[0];
		X1 = w[1] + ks[1];
		X2 = w[2] + ks[2];
		X3 = w[3] + ks[3];
		X4 = w[4] + ks[4];
		X5 = w[5] + ks[5] + ts[0];
		X6 = w[6] + ks[6] + ts[1];
		X7 = w[7] + ks[7];

		blk_ptr += SKEIN_512_BLOCK_BYTES;

		/* run the rounds */
		for (r = 1;
			r < (SKEIN_UNROLL_512 ? 2 * RCNT : 2);
			r += (SKEIN_UNROLL_512 ? 2 * SKEIN_UNROLL_512 : 1)) {
			R512_8_ROUNDS(0);

#if   R512_UNROLL_R(1)
			R512_8_ROUNDS(1);
#endif
#if   R512_UNROLL_R(2)
			R512_8_ROUNDS(2);
#endif
#if   R512_UNROLL_R(3)
			R512_8_ROUNDS(3);
#endif
#if   R512_UNROLL_R(4)
			R512_8_ROUNDS(4);
#endif
#if   R512_UNROLL_R(5)
			R512_8_ROUNDS(5);
#endif
#if   R512_UNROLL_R(6)
			R512_8_ROUNDS(6);
#endif
#if   R512_UNROLL_R(7)
			R512_8_ROUNDS(7);
#endif
#if   R512_UNROLL_R(8)
			R512_8_ROUNDS(8);
#endif
#if   R512_UNROLL_R(9)
			R512_8_ROUNDS(9);
#endif
#if   R512_UNROLL_R(10)
			R512_8_ROUNDS(10);
#endif
#if   R512_UNROLL_R(11)
			R512_8_ROUNDS(11);
#endif
#if   R512_UNROLL_R(12)
			R512_8_ROUNDS(12);
#endif
#if   R512_UNROLL_R(13)
			R512_8_ROUNDS(13);
#endif
#if   R512_UNROLL_R(14)
			R512_8_ROUNDS(14);
#endif
		}

		/* do the final "feedforward" xor, update context chaining */
		ctx->x[0] = X0 ^ w[0];
		ctx->x[1] = X1 ^ w[1];
		ctx->x[2] = X2 ^ w[2];
		ctx->x[3] = X3 ^ w[3];
		ctx->x[4] = X4 ^ w[4];
		ctx->x[5] = X5 ^ w[5];
		ctx->x[6] = X6 ^ w[6];
		ctx->x[7] = X7 ^ w[7];

		ts[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blk_cnt);
	ctx->h.tweak[0] = ts[0];
	ctx->h.tweak[1] = ts[1];
}

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t skein_512_process_block_code_size(void)
{
	return ((u8 *)skein_512_process_block_code_size) -
		((u8 *)skein_512_process_block);
}

unsigned int skein_512_unroll_cnt(void)
{
	return SKEIN_UNROLL_512;
}
#endif
#endif

/*****************************  SKEIN_1024 ******************************/
#if !(SKEIN_USE_ASM & 1024)
void skein_1024_process_block(struct skein_1024_ctx *ctx, const u8 *blk_ptr,
			      size_t blk_cnt, size_t byte_cnt_add)
{ /* do it in C, always looping (unrolled is bigger AND slower!) */
	enum {
		WCNT = SKEIN_1024_STATE_WORDS
	};
	size_t  r;
#if (SKEIN_UNROLL_1024 != 0)
	/* key sched: chaining vars + tweak + "rot" */
	u64  kw[WCNT + 4 + (RCNT * 2)];
#else
	/* key schedule words : chaining vars + tweak */
	u64  kw[WCNT + 4];
#endif

	/* local copy of vars, for speed */
	u64  X00, X01, X02, X03, X04, X05, X06, X07,
	     X08, X09, X10, X11, X12, X13, X14, X15;
	u64  w[WCNT]; /* local copy of input block */

	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	ts[0] = ctx->h.tweak[0];
	ts[1] = ctx->h.tweak[1];
	do  {
		/*
		 * this implementation only supports 2**64 input bytes
		 * (no carry out here)
		 */
		ts[0] += byte_cnt_add; /* update processed length */

		/* precompute the key schedule for this block */
		ks[0]  = ctx->x[0];
		ks[1]  = ctx->x[1];
		ks[2]  = ctx->x[2];
		ks[3]  = ctx->x[3];
		ks[4]  = ctx->x[4];
		ks[5]  = ctx->x[5];
		ks[6]  = ctx->x[6];
		ks[7]  = ctx->x[7];
		ks[8]  = ctx->x[8];
		ks[9]  = ctx->x[9];
		ks[10] = ctx->x[10];
		ks[11] = ctx->x[11];
		ks[12] = ctx->x[12];
		ks[13] = ctx->x[13];
		ks[14] = ctx->x[14];
		ks[15] = ctx->x[15];
		ks[16] =  ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^
			  ks[4] ^ ks[5] ^ ks[6] ^ ks[7] ^
			  ks[8] ^ ks[9] ^ ks[10] ^ ks[11] ^
			  ks[12] ^ ks[13] ^ ks[14] ^ ks[15] ^ SKEIN_KS_PARITY;

		ts[2] = ts[0] ^ ts[1];

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, WCNT);
		debug_save_tweak(ctx);

		/* do the first full key injection */
		X00 = w[0] + ks[0];
		X01 = w[1] + ks[1];
		X02 = w[2] + ks[2];
		X03 = w[3] + ks[3];
		X04 = w[4] + ks[4];
		X05 = w[5] + ks[5];
		X06 = w[6] + ks[6];
		X07 = w[7] + ks[7];
		X08 = w[8] + ks[8];
		X09 = w[9] + ks[9];
		X10 = w[10] + ks[10];
		X11 = w[11] + ks[11];
		X12 = w[12] + ks[12];
		X13 = w[13] + ks[13] + ts[0];
		X14 = w[14] + ks[14] + ts[1];
		X15 = w[15] + ks[15];

		for (r = 1;
			r < (SKEIN_UNROLL_1024 ? 2 * RCNT : 2);
			r += (SKEIN_UNROLL_1024 ? 2 * SKEIN_UNROLL_1024 : 1)) {
			R1024_8_ROUNDS(0);
#if   R1024_UNROLL_R(1)
			R1024_8_ROUNDS(1);
#endif
#if   R1024_UNROLL_R(2)
			R1024_8_ROUNDS(2);
#endif
#if   R1024_UNROLL_R(3)
			R1024_8_ROUNDS(3);
#endif
#if   R1024_UNROLL_R(4)
			R1024_8_ROUNDS(4);
#endif
#if   R1024_UNROLL_R(5)
			R1024_8_ROUNDS(5);
#endif
#if   R1024_UNROLL_R(6)
			R1024_8_ROUNDS(6);
#endif
#if   R1024_UNROLL_R(7)
			R1024_8_ROUNDS(7);
#endif
#if   R1024_UNROLL_R(8)
			R1024_8_ROUNDS(8);
#endif
#if   R1024_UNROLL_R(9)
			R1024_8_ROUNDS(9);
#endif
#if   R1024_UNROLL_R(10)
			R1024_8_ROUNDS(10);
#endif
#if   R1024_UNROLL_R(11)
			R1024_8_ROUNDS(11);
#endif
#if   R1024_UNROLL_R(12)
			R1024_8_ROUNDS(12);
#endif
#if   R1024_UNROLL_R(13)
			R1024_8_ROUNDS(13);
#endif
#if   R1024_UNROLL_R(14)
			R1024_8_ROUNDS(14);
#endif
		}
		/* do the final "feedforward" xor, update context chaining */

		ctx->x[0] = X00 ^ w[0];
		ctx->x[1] = X01 ^ w[1];
		ctx->x[2] = X02 ^ w[2];
		ctx->x[3] = X03 ^ w[3];
		ctx->x[4] = X04 ^ w[4];
		ctx->x[5] = X05 ^ w[5];
		ctx->x[6] = X06 ^ w[6];
		ctx->x[7] = X07 ^ w[7];
		ctx->x[8] = X08 ^ w[8];
		ctx->x[9] = X09 ^ w[9];
		ctx->x[10] = X10 ^ w[10];
		ctx->x[11] = X11 ^ w[11];
		ctx->x[12] = X12 ^ w[12];
		ctx->x[13] = X13 ^ w[13];
		ctx->x[14] = X14 ^ w[14];
		ctx->x[15] = X15 ^ w[15];

		ts[1] &= ~SKEIN_T1_FLAG_FIRST;
		blk_ptr += SKEIN_1024_BLOCK_BYTES;
	} while (--blk_cnt);
	ctx->h.tweak[0] = ts[0];
	ctx->h.tweak[1] = ts[1];
}

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t skein_1024_process_block_code_size(void)
{
	return ((u8 *)skein_1024_process_block_code_size) -
		((u8 *)skein_1024_process_block);
}

unsigned int skein_1024_unroll_cnt(void)
{
	return SKEIN_UNROLL_1024;
}
#endif
#endif
