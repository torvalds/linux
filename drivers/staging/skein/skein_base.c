/***********************************************************************
 **
 ** Implementation of the Skein hash function.
 **
 ** Source code author: Doug Whiting, 2008.
 **
 ** This algorithm and source code is released to the public domain.
 **
 ************************************************************************/

#include <linux/string.h>       /* get the memcpy/memset functions */
#include <linux/export.h>
#include "skein_base.h" /* get the Skein API definitions   */
#include "skein_iv.h"    /* get precomputed IVs */
#include "skein_block.h"

/*****************************************************************/
/*     256-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_256_init(struct skein_256_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN_256_STATE_BYTES];
		u64 w[SKEIN_256_STATE_WORDS];
	} cfg;                              /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  256:
		memcpy(ctx->x, SKEIN_256_IV_256, sizeof(ctx->x));
		break;
	case  224:
		memcpy(ctx->x, SKEIN_256_IV_224, sizeof(ctx->x));
		break;
	case  160:
		memcpy(ctx->x, SKEIN_256_IV_160, sizeof(ctx->x));
		break;
	case  128:
		memcpy(ctx->x, SKEIN_256_IV_128, sizeof(ctx->x));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		skein_start_new_type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = skein_swap64(hash_bit_len);
		cfg.w[2] = skein_swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3 * sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		skein_256_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}
	/* The chaining vars ctx->x are now initialized for hash_bit_len. */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to skein_256_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int skein_256_init_ext(struct skein_256_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8  b[SKEIN_256_STATE_BYTES];
		u64 w[SKEIN_256_STATE_WORDS];
	} cfg; /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	skein_assert_ret(key_bytes == 0 || key, SKEIN_FAIL);

	/* compute the initial chaining values ctx->x[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->x, 0, sizeof(ctx->x));
	} else { /* here to pre-process a key */
		skein_assert(sizeof(cfg.b) >= sizeof(ctx->x));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8 * sizeof(ctx->x);
		/* set tweaks: T0 = 0; T1 = KEY type */
		skein_start_new_type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		/* hash the key */
		skein_256_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_256_final_pad(ctx, cfg.b);
		/* copy over into ctx->x[] */
		memcpy(ctx->x, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hash_bit_len = hash_bit_len;
	skein_start_new_type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = skein_swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = skein_swap64(tree_info);

	/* compute the initial chaining values from config block */
	skein_256_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->x are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_256_update(struct skein_256_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN_256_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			skein_assert(ctx->h.b_cnt == SKEIN_256_BLOCK_BYTES);
			skein_256_process_block(ctx, ctx->b, 1,
						SKEIN_256_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN_256_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt - 1) / SKEIN_256_BLOCK_BYTES;
			skein_256_process_block(ctx, msg, n,
						SKEIN_256_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN_256_BLOCK_BYTES;
			msg        += n * SKEIN_256_BLOCK_BYTES;
		}
		skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN_256_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_256_final(struct skein_256_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_256_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_256_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_256_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_256_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*     512-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_512_init(struct skein_512_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN_512_STATE_BYTES];
		u64 w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->x, SKEIN_512_IV_512, sizeof(ctx->x));
		break;
	case  384:
		memcpy(ctx->x, SKEIN_512_IV_384, sizeof(ctx->x));
		break;
	case  256:
		memcpy(ctx->x, SKEIN_512_IV_256, sizeof(ctx->x));
		break;
	case  224:
		memcpy(ctx->x, SKEIN_512_IV_224, sizeof(ctx->x));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		skein_start_new_type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = skein_swap64(hash_bit_len);
		cfg.w[2] = skein_swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3 * sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		skein_512_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/*
	 * The chaining vars ctx->x are now initialized for the given
	 * hash_bit_len.
	 */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to skein_512_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int skein_512_init_ext(struct skein_512_ctx *ctx, size_t hash_bit_len,
		       u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8 b[SKEIN_512_STATE_BYTES];
		u64 w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	skein_assert_ret(key_bytes == 0 || key, SKEIN_FAIL);

	/* compute the initial chaining values ctx->x[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->x, 0, sizeof(ctx->x));
	} else { /* here to pre-process a key */
		skein_assert(sizeof(cfg.b) >= sizeof(ctx->x));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8 * sizeof(ctx->x);
		/* set tweaks: T0 = 0; T1 = KEY type */
		skein_start_new_type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		/* hash the key */
		skein_512_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_512_final_pad(ctx, cfg.b);
		/* copy over into ctx->x[] */
		memcpy(ctx->x, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hash_bit_len = hash_bit_len;          /* output hash bit count */
	skein_start_new_type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = skein_swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = skein_swap64(tree_info);

	/* compute the initial chaining values from config block */
	skein_512_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->x are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_512_update(struct skein_512_ctx *ctx, const u8 *msg,
		     size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN_512_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			skein_assert(ctx->h.b_cnt == SKEIN_512_BLOCK_BYTES);
			skein_512_process_block(ctx, ctx->b, 1,
						SKEIN_512_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN_512_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt - 1) / SKEIN_512_BLOCK_BYTES;
			skein_512_process_block(ctx, msg, n,
						SKEIN_512_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN_512_BLOCK_BYTES;
			msg        += n * SKEIN_512_BLOCK_BYTES;
		}
		skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN_512_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_512_final(struct skein_512_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_512_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_512_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_512_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_512_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*    1024-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int skein_1024_init(struct skein_1024_ctx *ctx, size_t hash_bit_len)
{
	union {
		u8 b[SKEIN_1024_STATE_BYTES];
		u64 w[SKEIN_1024_STATE_WORDS];
	} cfg;                              /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hash_bit_len = hash_bit_len;         /* output hash bit count */

	switch (hash_bit_len) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->x, SKEIN_1024_IV_512, sizeof(ctx->x));
		break;
	case  384:
		memcpy(ctx->x, SKEIN_1024_IV_384, sizeof(ctx->x));
		break;
	case 1024:
		memcpy(ctx->x, SKEIN_1024_IV_1024, sizeof(ctx->x));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG
		 * (could be precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		skein_start_new_type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = skein_swap64(hash_bit_len);
		cfg.w[2] = skein_swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3 * sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		skein_1024_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/* The chaining vars ctx->x are now initialized for the hash_bit_len. */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to skein_1024_init() when key_bytes == 0 && \
 *	tree_info == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int skein_1024_init_ext(struct skein_1024_ctx *ctx, size_t hash_bit_len,
			u64 tree_info, const u8 *key, size_t key_bytes)
{
	union {
		u8 b[SKEIN_1024_STATE_BYTES];
		u64 w[SKEIN_1024_STATE_WORDS];
	} cfg;                              /* config block */

	skein_assert_ret(hash_bit_len > 0, SKEIN_BAD_HASHLEN);
	skein_assert_ret(key_bytes == 0 || key, SKEIN_FAIL);

	/* compute the initial chaining values ctx->x[], based on key */
	if (key_bytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->x, 0, sizeof(ctx->x));
	} else { /* here to pre-process a key */
		skein_assert(sizeof(cfg.b) >= sizeof(ctx->x));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hash_bit_len = 8 * sizeof(ctx->x);
		/* set tweaks: T0 = 0; T1 = KEY type */
		skein_start_new_type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->x, 0, sizeof(ctx->x));
		/* hash the key */
		skein_1024_update(ctx, key, key_bytes);
		/* put result into cfg.b[] */
		skein_1024_final_pad(ctx, cfg.b);
		/* copy over into ctx->x[] */
		memcpy(ctx->x, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hash_bit_len = hash_bit_len;
	skein_start_new_type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = skein_swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = skein_swap64(hash_bit_len);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = skein_swap64(tree_info);

	/* compute the initial chaining values from config block */
	skein_1024_process_block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->x are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	skein_start_new_type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int skein_1024_update(struct skein_1024_ctx *ctx, const u8 *msg,
		      size_t msg_byte_cnt)
{
	size_t n;

	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_1024_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msg_byte_cnt + ctx->h.b_cnt > SKEIN_1024_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.b_cnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_1024_BLOCK_BYTES - ctx->h.b_cnt;
			if (n) {
				/* check on our logic here */
				skein_assert(n < msg_byte_cnt);
				memcpy(&ctx->b[ctx->h.b_cnt], msg, n);
				msg_byte_cnt  -= n;
				msg         += n;
				ctx->h.b_cnt += n;
			}
			skein_assert(ctx->h.b_cnt == SKEIN_1024_BLOCK_BYTES);
			skein_1024_process_block(ctx, ctx->b, 1,
						 SKEIN_1024_BLOCK_BYTES);
			ctx->h.b_cnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msg_byte_cnt > SKEIN_1024_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msg_byte_cnt - 1) / SKEIN_1024_BLOCK_BYTES;
			skein_1024_process_block(ctx, msg, n,
						 SKEIN_1024_BLOCK_BYTES);
			msg_byte_cnt -= n * SKEIN_1024_BLOCK_BYTES;
			msg        += n * SKEIN_1024_BLOCK_BYTES;
		}
		skein_assert(ctx->h.b_cnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msg_byte_cnt) {
		skein_assert(msg_byte_cnt + ctx->h.b_cnt <=
			     SKEIN_1024_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.b_cnt], msg, msg_byte_cnt);
		ctx->h.b_cnt += msg_byte_cnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int skein_1024_final(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_1024_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_1024_BLOCK_BYTES - ctx->h.b_cnt);

	/* process the final block */
	skein_1024_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_1024_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_1024_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_1024_BLOCK_BYTES;
		if (n >= SKEIN_1024_BLOCK_BYTES)
			n  = SKEIN_1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_1024_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}

/**************** Functions to support MAC/tree hashing ***************/
/*   (this code is identical for Optimized and Reference versions)    */

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_256_final_pad(struct skein_256_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_256_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_256_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	skein_put64_lsb_first(hash_val, ctx->x, SKEIN_256_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_512_final_pad(struct skein_512_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_512_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_512_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	skein_put64_lsb_first(hash_val, ctx->x, SKEIN_512_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int skein_1024_final_pad(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.tweak[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.b_cnt < SKEIN_1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.b_cnt], 0,
		       SKEIN_1024_BLOCK_BYTES - ctx->h.b_cnt);
	/* process the final block */
	skein_1024_process_block(ctx, ctx->b, 1, ctx->h.b_cnt);

	/* "output" the state bytes */
	skein_put64_lsb_first(hash_val, ctx->x, SKEIN_1024_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

#if SKEIN_TREE_HASH
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_256_output(struct skein_256_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_256_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_256_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_256_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_512_output(struct skein_512_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_512_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_512_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_512_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int skein_1024_output(struct skein_1024_ctx *ctx, u8 *hash_val)
{
	size_t i, n, byte_cnt;
	u64 x[SKEIN_1024_STATE_WORDS];
	/* catch uninitialized context */
	skein_assert_ret(ctx->h.b_cnt <= SKEIN_1024_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byte_cnt = (ctx->h.hash_bit_len + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(x, ctx->x, sizeof(x));
	for (i = 0; i * SKEIN_1024_BLOCK_BYTES < byte_cnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = skein_swap64((u64)i);
		skein_start_new_type(ctx, OUT_FINAL);
		/* run "counter mode" */
		skein_1024_process_block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byte_cnt - i * SKEIN_1024_BLOCK_BYTES;
		if (n >= SKEIN_1024_BLOCK_BYTES)
			n  = SKEIN_1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		skein_put64_lsb_first(hash_val + (i * SKEIN_1024_BLOCK_BYTES),
				      ctx->x, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->x, x, sizeof(x));
	}
	return SKEIN_SUCCESS;
}
#endif
