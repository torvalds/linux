/***********************************************************************
**
** Implementation of the Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/

#define  SKEIN_PORT_CODE /* instantiate any code in skein_port.h */

#include <linux/string.h>       /* get the memcpy/memset functions */
#include <skein.h> /* get the Skein API definitions   */
#include <skein_iv.h>    /* get precomputed IVs */
#include <skein_block.h>

/*****************************************************************/
/*     256-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein_256_Init(struct skein_256_ctx *ctx, size_t hashBitLen)
{
	union {
		u8  b[SKEIN_256_STATE_BYTES];
		u64  w[SKEIN_256_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

	switch (hashBitLen) { /* use pre-computed values, where available */
	case  256:
		memcpy(ctx->X, SKEIN_256_IV_256, sizeof(ctx->X));
		break;
	case  224:
		memcpy(ctx->X, SKEIN_256_IV_224, sizeof(ctx->X));
		break;
	case  160:
		memcpy(ctx->X, SKEIN_256_IV_160, sizeof(ctx->X));
		break;
	case  128:
		memcpy(ctx->X, SKEIN_256_IV_128, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		Skein_256_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}
	/* The chaining vars ctx->X are now initialized for hashBitLen. */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein_256_Init() when keyBytes == 0 && \
 *	treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein_256_InitExt(struct skein_256_ctx *ctx, size_t hashBitLen,
			u64 treeInfo, const u8 *key, size_t keyBytes)
{
	union {
		u8  b[SKEIN_256_STATE_BYTES];
		u64  w[SKEIN_256_STATE_WORDS];
	} cfg; /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		Skein_256_Update(ctx, key, keyBytes);
		/* put result into cfg.b[] */
		Skein_256_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hashBitLen = hashBitLen;
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hashBitLen);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(256, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein_256_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein_256_Update(struct skein_256_ctx *ctx, const u8 *msg,
			size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN_256_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_256_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				memcpy(&ctx->b[ctx->h.bCnt], msg, n);
				msgByteCnt  -= n;
				msg         += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN_256_BLOCK_BYTES);
			Skein_256_Process_Block(ctx, ctx->b, 1,
						SKEIN_256_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN_256_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt-1) / SKEIN_256_BLOCK_BYTES;
			Skein_256_Process_Block(ctx, msg, n,
						SKEIN_256_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN_256_BLOCK_BYTES;
			msg        += n * SKEIN_256_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.bCnt], msg, msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein_256_Final(struct skein_256_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_256_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_256_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_256_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN_256_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hashVal+i*SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*     512-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein_512_Init(struct skein_512_ctx *ctx, size_t hashBitLen)
{
	union {
		u8  b[SKEIN_512_STATE_BYTES];
		u64  w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

	switch (hashBitLen) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->X, SKEIN_512_IV_512, sizeof(ctx->X));
		break;
	case  384:
		memcpy(ctx->X, SKEIN_512_IV_384, sizeof(ctx->X));
		break;
	case  256:
		memcpy(ctx->X, SKEIN_512_IV_256, sizeof(ctx->X));
		break;
	case  224:
		memcpy(ctx->X, SKEIN_512_IV_224, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		Skein_512_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hashBitLen.
	 */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein_512_Init() when keyBytes == 0 && \
 *	treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein_512_InitExt(struct skein_512_ctx *ctx, size_t hashBitLen,
			u64 treeInfo, const u8 *key, size_t keyBytes)
{
	union {
		u8  b[SKEIN_512_STATE_BYTES];
		u64  w[SKEIN_512_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		Skein_512_Update(ctx, key, keyBytes);
		/* put result into cfg.b[] */
		Skein_512_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hashBitLen = hashBitLen;             /* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hashBitLen);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(512, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein_512_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein_512_Update(struct skein_512_ctx *ctx, const u8 *msg,
			size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN_512_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_512_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				memcpy(&ctx->b[ctx->h.bCnt], msg, n);
				msgByteCnt  -= n;
				msg         += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN_512_BLOCK_BYTES);
			Skein_512_Process_Block(ctx, ctx->b, 1,
						SKEIN_512_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN_512_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt-1) / SKEIN_512_BLOCK_BYTES;
			Skein_512_Process_Block(ctx, msg, n,
						SKEIN_512_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN_512_BLOCK_BYTES;
			msg        += n * SKEIN_512_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.bCnt], msg, msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein_512_Final(struct skein_512_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_512_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_512_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN_512_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(512, &ctx->h, n,
				 hashVal+i*SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*****************************************************************/
/*    1024-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein1024_Init(struct skein1024_ctx *ctx, size_t hashBitLen)
{
	union {
		u8  b[SKEIN1024_STATE_BYTES];
		u64  w[SKEIN1024_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

	switch (hashBitLen) { /* use pre-computed values, where available */
	case  512:
		memcpy(ctx->X, SKEIN1024_IV_512, sizeof(ctx->X));
		break;
	case  384:
		memcpy(ctx->X, SKEIN1024_IV_384, sizeof(ctx->X));
		break;
	case 1024:
		memcpy(ctx->X, SKEIN1024_IV_1024, sizeof(ctx->X));
		break;
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG
		 * (could be precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		memset(&cfg.w[3], 0, sizeof(cfg) - 3*sizeof(cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		Skein1024_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/* The chaining vars ctx->X are now initialized for the hashBitLen. */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);              /* T0=0, T1= MSG type */

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein1024_Init() when keyBytes == 0 && \
 *	treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein1024_InitExt(struct skein1024_ctx *ctx, size_t hashBitLen,
			u64 treeInfo, const u8 *key, size_t keyBytes)
{
	union {
		u8  b[SKEIN1024_STATE_BYTES];
		u64  w[SKEIN1024_STATE_WORDS];
	} cfg;                              /* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) { /* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof(ctx->X));
	} else { /* here to pre-process a key */
		Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8*sizeof(ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof(ctx->X));
		/* hash the key */
		Skein1024_Update(ctx, key, keyBytes);
		/* put result into cfg.b[] */
		Skein1024_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof(cfg.b));
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	/* output hash bit count */
	ctx->h.hashBitLen = hashBitLen;
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof(cfg.w));
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hashBitLen);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(1024, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein1024_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	Skein_Start_New_Type(ctx, MSG);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein1024_Update(struct skein1024_ctx *ctx, const u8 *msg,
			size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN1024_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN1024_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				memcpy(&ctx->b[ctx->h.bCnt], msg, n);
				msgByteCnt  -= n;
				msg         += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN1024_BLOCK_BYTES);
			Skein1024_Process_Block(ctx, ctx->b, 1,
						SKEIN1024_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN1024_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt-1) / SKEIN1024_BLOCK_BYTES;
			Skein1024_Process_Block(ctx, msg, n,
						SKEIN1024_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN1024_BLOCK_BYTES;
			msg        += n * SKEIN1024_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES);
		memcpy(&ctx->b[ctx->h.bCnt], msg, msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein1024_Final(struct skein1024_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN1024_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein1024_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN1024_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein1024_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n  = SKEIN1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN1024_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(1024, &ctx->h, n,
				 hashVal+i*SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/**************** Functions to support MAC/tree hashing ***************/
/*   (this code is identical for Optimized and Reference versions)    */

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein_256_Final_Pad(struct skein_256_ctx *ctx, u8 *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein_256_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN_256_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein_512_Final_Pad(struct skein_512_ctx *ctx, u8 *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN_512_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein1024_Final_Pad(struct skein1024_ctx *ctx, u8 *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
			SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein1024_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN1024_BLOCK_BYTES);

	return SKEIN_SUCCESS;
}

#if SKEIN_TREE_HASH
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein_256_Output(struct skein_256_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN_256_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_256_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_256_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n  = SKEIN_256_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN_256_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hashVal+i*SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein_512_Output(struct skein_512_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN_512_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN_512_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_512_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n  = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN_512_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hashVal+i*SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein1024_Output(struct skein1024_ctx *ctx, u8 *hashVal)
{
	size_t i, n, byteCnt;
	u64 X[SKEIN1024_STATE_WORDS];
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof(ctx->b));
	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof(X));
	for (i = 0; i*SKEIN1024_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		((u64 *)ctx->b)[0] = Skein_Swap64((u64) i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein1024_Process_Block(ctx, ctx->b, 1, sizeof(u64));
		/* number of output bytes left to go */
		n = byteCnt - i*SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n  = SKEIN1024_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal+i*SKEIN1024_BLOCK_BYTES, ctx->X,
				      n);
		Skein_Show_Final(256, &ctx->h, n,
				 hashVal+i*SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof(X));
	}
	return SKEIN_SUCCESS;
}
#endif
