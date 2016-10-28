/*
 * caam - Freescale FSL CAAM support for ahash functions of crypto API
 *
 * Copyright 2011 Freescale Semiconductor, Inc.
 *
 * Based on caamalg.c crypto API driver.
 *
 * relationship of digest job descriptor or first job descriptor after init to
 * shared descriptors:
 *
 * ---------------                     ---------------
 * | JobDesc #1  |-------------------->|  ShareDesc  |
 * | *(packet 1) |                     |  (hashKey)  |
 * ---------------                     | (operation) |
 *                                     ---------------
 *
 * relationship of subsequent job descriptors to shared descriptors:
 *
 * ---------------                     ---------------
 * | JobDesc #2  |-------------------->|  ShareDesc  |
 * | *(packet 2) |      |------------->|  (hashKey)  |
 * ---------------      |    |-------->| (operation) |
 *       .              |    |         | (load ctx2) |
 *       .              |    |         ---------------
 * ---------------      |    |
 * | JobDesc #3  |------|    |
 * | *(packet 3) |           |
 * ---------------           |
 *       .                   |
 *       .                   |
 * ---------------           |
 * | JobDesc #4  |------------
 * | *(packet 4) |
 * ---------------
 *
 * The SharedDesc never changes for a connection unless rekeyed, but
 * each packet will likely be in a different place. So all we need
 * to know to process the packet is where the input is, where the
 * output goes, and what context we want to process with. Context is
 * in the SharedDesc, packet references in the JobDesc.
 *
 * So, a job desc looks like:
 *
 * ---------------------
 * | Header            |
 * | ShareDesc Pointer |
 * | SEQ_OUT_PTR       |
 * | (output buffer)   |
 * | (output length)   |
 * | SEQ_IN_PTR        |
 * | (input buffer)    |
 * | (input length)    |
 * ---------------------
 */

#include "compat.h"

#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "jr.h"
#include "error.h"
#include "sg_sw_sec4.h"
#include "key_gen.h"

#define CAAM_CRA_PRIORITY		3000

/* max hash key is max split key size */
#define CAAM_MAX_HASH_KEY_SIZE		(SHA512_DIGEST_SIZE * 2)

#define CAAM_MAX_HASH_BLOCK_SIZE	SHA512_BLOCK_SIZE
#define CAAM_MAX_HASH_DIGEST_SIZE	SHA512_DIGEST_SIZE

/* length of descriptors text */
#define DESC_AHASH_BASE			(4 * CAAM_CMD_SZ)
#define DESC_AHASH_UPDATE_LEN		(6 * CAAM_CMD_SZ)
#define DESC_AHASH_UPDATE_FIRST_LEN	(DESC_AHASH_BASE + 4 * CAAM_CMD_SZ)
#define DESC_AHASH_FINAL_LEN		(DESC_AHASH_BASE + 5 * CAAM_CMD_SZ)
#define DESC_AHASH_FINUP_LEN		(DESC_AHASH_BASE + 5 * CAAM_CMD_SZ)
#define DESC_AHASH_DIGEST_LEN		(DESC_AHASH_BASE + 4 * CAAM_CMD_SZ)

#define DESC_HASH_MAX_USED_BYTES	(DESC_AHASH_FINAL_LEN + \
					 CAAM_MAX_HASH_KEY_SIZE)
#define DESC_HASH_MAX_USED_LEN		(DESC_HASH_MAX_USED_BYTES / CAAM_CMD_SZ)

/* caam context sizes for hashes: running digest + 8 */
#define HASH_MSG_LEN			8
#define MAX_CTX_LEN			(HASH_MSG_LEN + SHA512_DIGEST_SIZE)

#ifdef DEBUG
/* for print_hex_dumps with line references */
#define debug(format, arg...) printk(format, arg)
#else
#define debug(format, arg...)
#endif


static struct list_head hash_list;

/* ahash per-session context */
struct caam_hash_ctx {
	u32 sh_desc_update[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_update_first[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_fin[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_digest[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_finup[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	dma_addr_t sh_desc_update_dma ____cacheline_aligned;
	dma_addr_t sh_desc_update_first_dma;
	dma_addr_t sh_desc_fin_dma;
	dma_addr_t sh_desc_digest_dma;
	dma_addr_t sh_desc_finup_dma;
	struct device *jrdev;
	u32 alg_type;
	u32 alg_op;
	u8 key[CAAM_MAX_HASH_KEY_SIZE];
	dma_addr_t key_dma;
	int ctx_len;
	unsigned int split_key_len;
	unsigned int split_key_pad_len;
};

/* ahash state */
struct caam_hash_state {
	dma_addr_t buf_dma;
	dma_addr_t ctx_dma;
	u8 buf_0[CAAM_MAX_HASH_BLOCK_SIZE] ____cacheline_aligned;
	int buflen_0;
	u8 buf_1[CAAM_MAX_HASH_BLOCK_SIZE] ____cacheline_aligned;
	int buflen_1;
	u8 caam_ctx[MAX_CTX_LEN] ____cacheline_aligned;
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	int current_buf;
};

struct caam_export_state {
	u8 buf[CAAM_MAX_HASH_BLOCK_SIZE];
	u8 caam_ctx[MAX_CTX_LEN];
	int buflen;
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
};

/* Common job descriptor seq in/out ptr routines */

/* Map state->caam_ctx, and append seq_out_ptr command that points to it */
static inline int map_seq_out_ptr_ctx(u32 *desc, struct device *jrdev,
				      struct caam_hash_state *state,
				      int ctx_len)
{
	state->ctx_dma = dma_map_single(jrdev, state->caam_ctx,
					ctx_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, state->ctx_dma)) {
		dev_err(jrdev, "unable to map ctx\n");
		return -ENOMEM;
	}

	append_seq_out_ptr(desc, state->ctx_dma, ctx_len, 0);

	return 0;
}

/* Map req->result, and append seq_out_ptr command that points to it */
static inline dma_addr_t map_seq_out_ptr_result(u32 *desc, struct device *jrdev,
						u8 *result, int digestsize)
{
	dma_addr_t dst_dma;

	dst_dma = dma_map_single(jrdev, result, digestsize, DMA_FROM_DEVICE);
	append_seq_out_ptr(desc, dst_dma, digestsize, 0);

	return dst_dma;
}

/* Map current buffer in state and put it in link table */
static inline dma_addr_t buf_map_to_sec4_sg(struct device *jrdev,
					    struct sec4_sg_entry *sec4_sg,
					    u8 *buf, int buflen)
{
	dma_addr_t buf_dma;

	buf_dma = dma_map_single(jrdev, buf, buflen, DMA_TO_DEVICE);
	dma_to_sec4_sg_one(sec4_sg, buf_dma, buflen, 0);

	return buf_dma;
}

/*
 * Only put buffer in link table if it contains data, which is possible,
 * since a buffer has previously been used, and needs to be unmapped,
 */
static inline dma_addr_t
try_buf_map_to_sec4_sg(struct device *jrdev, struct sec4_sg_entry *sec4_sg,
		       u8 *buf, dma_addr_t buf_dma, int buflen,
		       int last_buflen)
{
	if (buf_dma && !dma_mapping_error(jrdev, buf_dma))
		dma_unmap_single(jrdev, buf_dma, last_buflen, DMA_TO_DEVICE);
	if (buflen)
		buf_dma = buf_map_to_sec4_sg(jrdev, sec4_sg, buf, buflen);
	else
		buf_dma = 0;

	return buf_dma;
}

/* Map state->caam_ctx, and add it to link table */
static inline int ctx_map_to_sec4_sg(u32 *desc, struct device *jrdev,
				     struct caam_hash_state *state, int ctx_len,
				     struct sec4_sg_entry *sec4_sg, u32 flag)
{
	state->ctx_dma = dma_map_single(jrdev, state->caam_ctx, ctx_len, flag);
	if (dma_mapping_error(jrdev, state->ctx_dma)) {
		dev_err(jrdev, "unable to map ctx\n");
		return -ENOMEM;
	}

	dma_to_sec4_sg_one(sec4_sg, state->ctx_dma, ctx_len, 0);

	return 0;
}

/* Common shared descriptor commands */
static inline void append_key_ahash(u32 *desc, struct caam_hash_ctx *ctx)
{
	append_key_as_imm(desc, ctx->key, ctx->split_key_pad_len,
			  ctx->split_key_len, CLASS_2 |
			  KEY_DEST_MDHA_SPLIT | KEY_ENC);
}

/* Append key if it has been set */
static inline void init_sh_desc_key_ahash(u32 *desc, struct caam_hash_ctx *ctx)
{
	u32 *key_jump_cmd;

	init_sh_desc(desc, HDR_SHARE_SERIAL);

	if (ctx->split_key_len) {
		/* Skip if already shared */
		key_jump_cmd = append_jump(desc, JUMP_JSL | JUMP_TEST_ALL |
					   JUMP_COND_SHRD);

		append_key_ahash(desc, ctx);

		set_jump_tgt_here(desc, key_jump_cmd);
	}

	/* Propagate errors from shared to job descriptor */
	append_cmd(desc, SET_OK_NO_PROP_ERRORS | CMD_LOAD);
}

/*
 * For ahash read data from seqin following state->caam_ctx,
 * and write resulting class2 context to seqout, which may be state->caam_ctx
 * or req->result
 */
static inline void ahash_append_load_str(u32 *desc, int digestsize)
{
	/* Calculate remaining bytes to read */
	append_math_add(desc, VARSEQINLEN, SEQINLEN, REG0, CAAM_CMD_SZ);

	/* Read remaining bytes */
	append_seq_fifo_load(desc, 0, FIFOLD_CLASS_CLASS2 | FIFOLD_TYPE_LAST2 |
			     FIFOLD_TYPE_MSG | KEY_VLF);

	/* Store class2 context bytes */
	append_seq_store(desc, digestsize, LDST_CLASS_2_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);
}

/*
 * For ahash update, final and finup, import context, read and write to seqout
 */
static inline void ahash_ctx_data_to_out(u32 *desc, u32 op, u32 state,
					 int digestsize,
					 struct caam_hash_ctx *ctx)
{
	init_sh_desc_key_ahash(desc, ctx);

	/* Import context from software */
	append_cmd(desc, CMD_SEQ_LOAD | LDST_SRCDST_BYTE_CONTEXT |
		   LDST_CLASS_2_CCB | ctx->ctx_len);

	/* Class 2 operation */
	append_operation(desc, op | state | OP_ALG_ENCRYPT);

	/*
	 * Load from buf and/or src and write to req->result or state->context
	 */
	ahash_append_load_str(desc, digestsize);
}

/* For ahash firsts and digest, read and write to seqout */
static inline void ahash_data_to_out(u32 *desc, u32 op, u32 state,
				     int digestsize, struct caam_hash_ctx *ctx)
{
	init_sh_desc_key_ahash(desc, ctx);

	/* Class 2 operation */
	append_operation(desc, op | state | OP_ALG_ENCRYPT);

	/*
	 * Load from buf and/or src and write to req->result or state->context
	 */
	ahash_append_load_str(desc, digestsize);
}

static int ahash_set_sh_desc(struct crypto_ahash *ahash)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct device *jrdev = ctx->jrdev;
	u32 have_key = 0;
	u32 *desc;

	if (ctx->split_key_len)
		have_key = OP_ALG_AAI_HMAC_PRECOMP;

	/* ahash_update shared descriptor */
	desc = ctx->sh_desc_update;

	init_sh_desc(desc, HDR_SHARE_SERIAL);

	/* Import context from software */
	append_cmd(desc, CMD_SEQ_LOAD | LDST_SRCDST_BYTE_CONTEXT |
		   LDST_CLASS_2_CCB | ctx->ctx_len);

	/* Class 2 operation */
	append_operation(desc, ctx->alg_type | OP_ALG_AS_UPDATE |
			 OP_ALG_ENCRYPT);

	/* Load data and write to result or context */
	ahash_append_load_str(desc, ctx->ctx_len);

	ctx->sh_desc_update_dma = dma_map_single(jrdev, desc, desc_bytes(desc),
						 DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_update_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "ahash update shdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	/* ahash_update_first shared descriptor */
	desc = ctx->sh_desc_update_first;

	ahash_data_to_out(desc, have_key | ctx->alg_type, OP_ALG_AS_INIT,
			  ctx->ctx_len, ctx);

	ctx->sh_desc_update_first_dma = dma_map_single(jrdev, desc,
						       desc_bytes(desc),
						       DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_update_first_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "ahash update first shdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	/* ahash_final shared descriptor */
	desc = ctx->sh_desc_fin;

	ahash_ctx_data_to_out(desc, have_key | ctx->alg_type,
			      OP_ALG_AS_FINALIZE, digestsize, ctx);

	ctx->sh_desc_fin_dma = dma_map_single(jrdev, desc, desc_bytes(desc),
					      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_fin_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ahash final shdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc,
		       desc_bytes(desc), 1);
#endif

	/* ahash_finup shared descriptor */
	desc = ctx->sh_desc_finup;

	ahash_ctx_data_to_out(desc, have_key | ctx->alg_type,
			      OP_ALG_AS_FINALIZE, digestsize, ctx);

	ctx->sh_desc_finup_dma = dma_map_single(jrdev, desc, desc_bytes(desc),
						DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_finup_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ahash finup shdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc,
		       desc_bytes(desc), 1);
#endif

	/* ahash_digest shared descriptor */
	desc = ctx->sh_desc_digest;

	ahash_data_to_out(desc, have_key | ctx->alg_type, OP_ALG_AS_INITFINAL,
			  digestsize, ctx);

	ctx->sh_desc_digest_dma = dma_map_single(jrdev, desc,
						 desc_bytes(desc),
						 DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->sh_desc_digest_dma)) {
		dev_err(jrdev, "unable to map shared descriptor\n");
		return -ENOMEM;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "ahash digest shdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc,
		       desc_bytes(desc), 1);
#endif

	return 0;
}

static int gen_split_hash_key(struct caam_hash_ctx *ctx, const u8 *key_in,
			      u32 keylen)
{
	return gen_split_key(ctx->jrdev, ctx->key, ctx->split_key_len,
			       ctx->split_key_pad_len, key_in, keylen,
			       ctx->alg_op);
}

/* Digest hash size if it is too large */
static int hash_digest_key(struct caam_hash_ctx *ctx, const u8 *key_in,
			   u32 *keylen, u8 *key_out, u32 digestsize)
{
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	struct split_key_result result;
	dma_addr_t src_dma, dst_dma;
	int ret;

	desc = kmalloc(CAAM_CMD_SZ * 8 + CAAM_PTR_SZ * 2, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "unable to allocate key input memory\n");
		return -ENOMEM;
	}

	init_job_desc(desc, 0);

	src_dma = dma_map_single(jrdev, (void *)key_in, *keylen,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, src_dma)) {
		dev_err(jrdev, "unable to map key input memory\n");
		kfree(desc);
		return -ENOMEM;
	}
	dst_dma = dma_map_single(jrdev, (void *)key_out, digestsize,
				 DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dst_dma)) {
		dev_err(jrdev, "unable to map key output memory\n");
		dma_unmap_single(jrdev, src_dma, *keylen, DMA_TO_DEVICE);
		kfree(desc);
		return -ENOMEM;
	}

	/* Job descriptor to perform unkeyed hash on key_in */
	append_operation(desc, ctx->alg_type | OP_ALG_ENCRYPT |
			 OP_ALG_AS_INITFINAL);
	append_seq_in_ptr(desc, src_dma, *keylen, 0);
	append_seq_fifo_load(desc, *keylen, FIFOLD_CLASS_CLASS2 |
			     FIFOLD_TYPE_LAST2 | FIFOLD_TYPE_MSG);
	append_seq_out_ptr(desc, dst_dma, digestsize, 0);
	append_seq_store(desc, digestsize, LDST_CLASS_2_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "key_in@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key_in, *keylen, 1);
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	result.err = 0;
	init_completion(&result.completion);

	ret = caam_jr_enqueue(jrdev, desc, split_key_done, &result);
	if (!ret) {
		/* in progress */
		wait_for_completion_interruptible(&result.completion);
		ret = result.err;
#ifdef DEBUG
		print_hex_dump(KERN_ERR,
			       "digested key@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, key_in,
			       digestsize, 1);
#endif
	}
	dma_unmap_single(jrdev, src_dma, *keylen, DMA_TO_DEVICE);
	dma_unmap_single(jrdev, dst_dma, digestsize, DMA_FROM_DEVICE);

	*keylen = digestsize;

	kfree(desc);

	return ret;
}

static int ahash_setkey(struct crypto_ahash *ahash,
			const u8 *key, unsigned int keylen)
{
	/* Sizes for MDHA pads (*not* keys): MD5, SHA1, 224, 256, 384, 512 */
	static const u8 mdpadlen[] = { 16, 20, 32, 32, 64, 64 };
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *jrdev = ctx->jrdev;
	int blocksize = crypto_tfm_alg_blocksize(&ahash->base);
	int digestsize = crypto_ahash_digestsize(ahash);
	int ret;
	u8 *hashed_key = NULL;

#ifdef DEBUG
	printk(KERN_ERR "keylen %d\n", keylen);
#endif

	if (keylen > blocksize) {
		hashed_key = kmalloc_array(digestsize,
					   sizeof(*hashed_key),
					   GFP_KERNEL | GFP_DMA);
		if (!hashed_key)
			return -ENOMEM;
		ret = hash_digest_key(ctx, key, &keylen, hashed_key,
				      digestsize);
		if (ret)
			goto bad_free_key;
		key = hashed_key;
	}

	/* Pick class 2 key length from algorithm submask */
	ctx->split_key_len = mdpadlen[(ctx->alg_op & OP_ALG_ALGSEL_SUBMASK) >>
				      OP_ALG_ALGSEL_SHIFT] * 2;
	ctx->split_key_pad_len = ALIGN(ctx->split_key_len, 16);

#ifdef DEBUG
	printk(KERN_ERR "split_key_len %d split_key_pad_len %d\n",
	       ctx->split_key_len, ctx->split_key_pad_len);
	print_hex_dump(KERN_ERR, "key in @"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);
#endif

	ret = gen_split_hash_key(ctx, key, keylen);
	if (ret)
		goto bad_free_key;

	ctx->key_dma = dma_map_single(jrdev, ctx->key, ctx->split_key_pad_len,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, ctx->key_dma)) {
		dev_err(jrdev, "unable to map key i/o memory\n");
		ret = -ENOMEM;
		goto error_free_key;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx.key@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, ctx->key,
		       ctx->split_key_pad_len, 1);
#endif

	ret = ahash_set_sh_desc(ahash);
	if (ret) {
		dma_unmap_single(jrdev, ctx->key_dma, ctx->split_key_pad_len,
				 DMA_TO_DEVICE);
	}
 error_free_key:
	kfree(hashed_key);
	return ret;
 bad_free_key:
	kfree(hashed_key);
	crypto_ahash_set_flags(ahash, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

/*
 * ahash_edesc - s/w-extended ahash descriptor
 * @dst_dma: physical mapped address of req->result
 * @sec4_sg_dma: physical mapped address of h/w link table
 * @src_nents: number of segments in input scatterlist
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 * @sec4_sg: h/w link table
 */
struct ahash_edesc {
	dma_addr_t dst_dma;
	dma_addr_t sec4_sg_dma;
	int src_nents;
	int sec4_sg_bytes;
	u32 hw_desc[DESC_JOB_IO_LEN / sizeof(u32)] ____cacheline_aligned;
	struct sec4_sg_entry sec4_sg[0];
};

static inline void ahash_unmap(struct device *dev,
			struct ahash_edesc *edesc,
			struct ahash_request *req, int dst_len)
{
	if (edesc->src_nents)
		dma_unmap_sg(dev, req->src, edesc->src_nents, DMA_TO_DEVICE);
	if (edesc->dst_dma)
		dma_unmap_single(dev, edesc->dst_dma, dst_len, DMA_FROM_DEVICE);

	if (edesc->sec4_sg_bytes)
		dma_unmap_single(dev, edesc->sec4_sg_dma,
				 edesc->sec4_sg_bytes, DMA_TO_DEVICE);
}

static inline void ahash_unmap_ctx(struct device *dev,
			struct ahash_edesc *edesc,
			struct ahash_request *req, int dst_len, u32 flag)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);

	if (state->ctx_dma)
		dma_unmap_single(dev, state->ctx_dma, ctx->ctx_len, flag);
	ahash_unmap(dev, edesc, req, dst_len);
}

static void ahash_done(struct device *jrdev, u32 *desc, u32 err,
		       void *context)
{
	struct ahash_request *req = context;
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	int digestsize = crypto_ahash_digestsize(ahash);
#ifdef DEBUG
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = (struct ahash_edesc *)((char *)desc -
		 offsetof(struct ahash_edesc, hw_desc));
	if (err)
		caam_jr_strstatus(jrdev, err);

	ahash_unmap(jrdev, edesc, req, digestsize);
	kfree(edesc);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
		       ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump(KERN_ERR, "result@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, req->result,
			       digestsize, 1);
#endif

	req->base.complete(&req->base, err);
}

static void ahash_done_bi(struct device *jrdev, u32 *desc, u32 err,
			    void *context)
{
	struct ahash_request *req = context;
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
#ifdef DEBUG
	struct caam_hash_state *state = ahash_request_ctx(req);
	int digestsize = crypto_ahash_digestsize(ahash);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = (struct ahash_edesc *)((char *)desc -
		 offsetof(struct ahash_edesc, hw_desc));
	if (err)
		caam_jr_strstatus(jrdev, err);

	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, DMA_BIDIRECTIONAL);
	kfree(edesc);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
		       ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump(KERN_ERR, "result@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, req->result,
			       digestsize, 1);
#endif

	req->base.complete(&req->base, err);
}

static void ahash_done_ctx_src(struct device *jrdev, u32 *desc, u32 err,
			       void *context)
{
	struct ahash_request *req = context;
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	int digestsize = crypto_ahash_digestsize(ahash);
#ifdef DEBUG
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = (struct ahash_edesc *)((char *)desc -
		 offsetof(struct ahash_edesc, hw_desc));
	if (err)
		caam_jr_strstatus(jrdev, err);

	ahash_unmap_ctx(jrdev, edesc, req, digestsize, DMA_TO_DEVICE);
	kfree(edesc);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
		       ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump(KERN_ERR, "result@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, req->result,
			       digestsize, 1);
#endif

	req->base.complete(&req->base, err);
}

static void ahash_done_ctx_dst(struct device *jrdev, u32 *desc, u32 err,
			       void *context)
{
	struct ahash_request *req = context;
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
#ifdef DEBUG
	struct caam_hash_state *state = ahash_request_ctx(req);
	int digestsize = crypto_ahash_digestsize(ahash);

	dev_err(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);
#endif

	edesc = (struct ahash_edesc *)((char *)desc -
		 offsetof(struct ahash_edesc, hw_desc));
	if (err)
		caam_jr_strstatus(jrdev, err);

	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, DMA_FROM_DEVICE);
	kfree(edesc);

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "ctx@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
		       ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump(KERN_ERR, "result@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, req->result,
			       digestsize, 1);
#endif

	req->base.complete(&req->base, err);
}

/*
 * Allocate an enhanced descriptor, which contains the hardware descriptor
 * and space for hardware scatter table containing sg_num entries.
 */
static struct ahash_edesc *ahash_edesc_alloc(struct caam_hash_ctx *ctx,
					     int sg_num, u32 *sh_desc,
					     dma_addr_t sh_desc_dma,
					     gfp_t flags)
{
	struct ahash_edesc *edesc;
	unsigned int sg_size = sg_num * sizeof(struct sec4_sg_entry);

	edesc = kzalloc(sizeof(*edesc) + sg_size, GFP_DMA | flags);
	if (!edesc) {
		dev_err(ctx->jrdev, "could not allocate extended descriptor\n");
		return NULL;
	}

	init_job_desc_shared(edesc->hw_desc, sh_desc_dma, desc_len(sh_desc),
			     HDR_SHARE_DEFER | HDR_REVERSE);

	return edesc;
}

static int ahash_edesc_add_src(struct caam_hash_ctx *ctx,
			       struct ahash_edesc *edesc,
			       struct ahash_request *req, int nents,
			       unsigned int first_sg,
			       unsigned int first_bytes, size_t to_hash)
{
	dma_addr_t src_dma;
	u32 options;

	if (nents > 1 || first_sg) {
		struct sec4_sg_entry *sg = edesc->sec4_sg;
		unsigned int sgsize = sizeof(*sg) * (first_sg + nents);

		sg_to_sec4_sg_last(req->src, nents, sg + first_sg, 0);

		src_dma = dma_map_single(ctx->jrdev, sg, sgsize, DMA_TO_DEVICE);
		if (dma_mapping_error(ctx->jrdev, src_dma)) {
			dev_err(ctx->jrdev, "unable to map S/G table\n");
			return -ENOMEM;
		}

		edesc->sec4_sg_bytes = sgsize;
		edesc->sec4_sg_dma = src_dma;
		options = LDST_SGF;
	} else {
		src_dma = sg_dma_address(req->src);
		options = 0;
	}

	append_seq_in_ptr(edesc->hw_desc, src_dma, first_bytes + to_hash,
			  options);

	return 0;
}

/* submit update job descriptor */
static int ahash_update_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int *buflen = state->current_buf ? &state->buflen_1 : &state->buflen_0;
	u8 *next_buf = state->current_buf ? state->buf_0 : state->buf_1;
	int *next_buflen = state->current_buf ? &state->buflen_0 :
			   &state->buflen_1, last_buflen;
	int in_len = *buflen + req->nbytes, to_hash;
	u32 *desc;
	int src_nents, mapped_nents, sec4_sg_bytes, sec4_sg_src_index;
	struct ahash_edesc *edesc;
	int ret = 0;

	last_buflen = *next_buflen;
	*next_buflen = in_len & (crypto_tfm_alg_blocksize(&ahash->base) - 1);
	to_hash = in_len - *next_buflen;

	if (to_hash) {
		src_nents = sg_nents_for_len(req->src,
					     req->nbytes - (*next_buflen));
		if (src_nents < 0) {
			dev_err(jrdev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(jrdev, "unable to DMA map source\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		sec4_sg_src_index = 1 + (*buflen ? 1 : 0);
		sec4_sg_bytes = (sec4_sg_src_index + mapped_nents) *
				 sizeof(struct sec4_sg_entry);

		/*
		 * allocate space for base edesc and hw desc commands,
		 * link tables
		 */
		edesc = ahash_edesc_alloc(ctx, sec4_sg_src_index + mapped_nents,
					  ctx->sh_desc_update,
					  ctx->sh_desc_update_dma, flags);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		edesc->sec4_sg_bytes = sec4_sg_bytes;

		ret = ctx_map_to_sec4_sg(desc, jrdev, state, ctx->ctx_len,
					 edesc->sec4_sg, DMA_BIDIRECTIONAL);
		if (ret)
			goto unmap_ctx;

		state->buf_dma = try_buf_map_to_sec4_sg(jrdev,
							edesc->sec4_sg + 1,
							buf, state->buf_dma,
							*buflen, last_buflen);

		if (mapped_nents) {
			sg_to_sec4_sg_last(req->src, mapped_nents,
					   edesc->sec4_sg + sec4_sg_src_index,
					   0);
			if (*next_buflen)
				scatterwalk_map_and_copy(next_buf, req->src,
							 to_hash - *buflen,
							 *next_buflen, 0);
		} else {
			(edesc->sec4_sg + sec4_sg_src_index - 1)->len |=
				cpu_to_caam32(SEC4_SG_LEN_FIN);
		}

		state->current_buf = !state->current_buf;

		desc = edesc->hw_desc;

		edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
						     sec4_sg_bytes,
						     DMA_TO_DEVICE);
		if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
			dev_err(jrdev, "unable to map S/G table\n");
			ret = -ENOMEM;
			goto unmap_ctx;
		}

		append_seq_in_ptr(desc, edesc->sec4_sg_dma, ctx->ctx_len +
				       to_hash, LDST_SGF);

		append_seq_out_ptr(desc, state->ctx_dma, ctx->ctx_len, 0);

#ifdef DEBUG
		print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, desc,
			       desc_bytes(desc), 1);
#endif

		ret = caam_jr_enqueue(jrdev, desc, ahash_done_bi, req);
		if (ret)
			goto unmap_ctx;

		ret = -EINPROGRESS;
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;
		*next_buflen = last_buflen;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "buf@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, buf, *buflen, 1);
	print_hex_dump(KERN_ERR, "next buf@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, next_buf,
		       *next_buflen, 1);
#endif

	return ret;
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, DMA_BIDIRECTIONAL);
	kfree(edesc);
	return ret;
}

static int ahash_final_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int buflen = state->current_buf ? state->buflen_1 : state->buflen_0;
	int last_buflen = state->current_buf ? state->buflen_0 :
			  state->buflen_1;
	u32 *desc;
	int sec4_sg_bytes, sec4_sg_src_index;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	sec4_sg_src_index = 1 + (buflen ? 1 : 0);
	sec4_sg_bytes = sec4_sg_src_index * sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(ctx, sec4_sg_src_index,
				  ctx->sh_desc_fin, ctx->sh_desc_fin_dma,
				  flags);
	if (!edesc)
		return -ENOMEM;

	desc = edesc->hw_desc;

	edesc->sec4_sg_bytes = sec4_sg_bytes;
	edesc->src_nents = 0;

	ret = ctx_map_to_sec4_sg(desc, jrdev, state, ctx->ctx_len,
				 edesc->sec4_sg, DMA_TO_DEVICE);
	if (ret)
		goto unmap_ctx;

	state->buf_dma = try_buf_map_to_sec4_sg(jrdev, edesc->sec4_sg + 1,
						buf, state->buf_dma, buflen,
						last_buflen);
	(edesc->sec4_sg + sec4_sg_src_index - 1)->len |=
		cpu_to_caam32(SEC4_SG_LEN_FIN);

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}

	append_seq_in_ptr(desc, edesc->sec4_sg_dma, ctx->ctx_len + buflen,
			  LDST_SGF);

	edesc->dst_dma = map_seq_out_ptr_result(desc, jrdev, req->result,
						digestsize);
	if (dma_mapping_error(jrdev, edesc->dst_dma)) {
		dev_err(jrdev, "unable to map dst\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ahash_done_ctx_src, req);
	if (ret)
		goto unmap_ctx;

	return -EINPROGRESS;
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, digestsize, DMA_FROM_DEVICE);
	kfree(edesc);
	return ret;
}

static int ahash_finup_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int buflen = state->current_buf ? state->buflen_1 : state->buflen_0;
	int last_buflen = state->current_buf ? state->buflen_0 :
			  state->buflen_1;
	u32 *desc;
	int sec4_sg_src_index;
	int src_nents, mapped_nents;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(jrdev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(jrdev, "unable to DMA map source\n");
			return -ENOMEM;
		}
	} else {
		mapped_nents = 0;
	}

	sec4_sg_src_index = 1 + (buflen ? 1 : 0);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(ctx, sec4_sg_src_index + mapped_nents,
				  ctx->sh_desc_finup, ctx->sh_desc_finup_dma,
				  flags);
	if (!edesc) {
		dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	desc = edesc->hw_desc;

	edesc->src_nents = src_nents;

	ret = ctx_map_to_sec4_sg(desc, jrdev, state, ctx->ctx_len,
				 edesc->sec4_sg, DMA_TO_DEVICE);
	if (ret)
		goto unmap_ctx;

	state->buf_dma = try_buf_map_to_sec4_sg(jrdev, edesc->sec4_sg + 1,
						buf, state->buf_dma, buflen,
						last_buflen);

	ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents,
				  sec4_sg_src_index, ctx->ctx_len + buflen,
				  req->nbytes);
	if (ret)
		goto unmap_ctx;

	edesc->dst_dma = map_seq_out_ptr_result(desc, jrdev, req->result,
						digestsize);
	if (dma_mapping_error(jrdev, edesc->dst_dma)) {
		dev_err(jrdev, "unable to map dst\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ahash_done_ctx_src, req);
	if (ret)
		goto unmap_ctx;

	return -EINPROGRESS;
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, digestsize, DMA_FROM_DEVICE);
	kfree(edesc);
	return ret;
}

static int ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u32 *desc;
	int digestsize = crypto_ahash_digestsize(ahash);
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(jrdev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(jrdev, "unable to map source for DMA\n");
			return -ENOMEM;
		}
	} else {
		mapped_nents = 0;
	}

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(ctx, mapped_nents > 1 ? mapped_nents : 0,
				  ctx->sh_desc_digest, ctx->sh_desc_digest_dma,
				  flags);
	if (!edesc) {
		dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	edesc->src_nents = src_nents;

	ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents, 0, 0,
				  req->nbytes);
	if (ret) {
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
		return ret;
	}

	desc = edesc->hw_desc;

	edesc->dst_dma = map_seq_out_ptr_result(desc, jrdev, req->result,
						digestsize);
	if (dma_mapping_error(jrdev, edesc->dst_dma)) {
		dev_err(jrdev, "unable to map dst\n");
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
		return -ENOMEM;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ahash_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
	}

	return ret;
}

/* submit ahash final if it the first job descriptor */
static int ahash_final_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int buflen = state->current_buf ? state->buflen_1 : state->buflen_0;
	u32 *desc;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(ctx, 0, ctx->sh_desc_digest,
				  ctx->sh_desc_digest_dma, flags);
	if (!edesc)
		return -ENOMEM;

	desc = edesc->hw_desc;

	state->buf_dma = dma_map_single(jrdev, buf, buflen, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, state->buf_dma)) {
		dev_err(jrdev, "unable to map src\n");
		goto unmap;
	}

	append_seq_in_ptr(desc, state->buf_dma, buflen, 0);

	edesc->dst_dma = map_seq_out_ptr_result(desc, jrdev, req->result,
						digestsize);
	if (dma_mapping_error(jrdev, edesc->dst_dma)) {
		dev_err(jrdev, "unable to map dst\n");
		goto unmap;
	}
	edesc->src_nents = 0;

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ahash_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
	}

	return ret;
 unmap:
	ahash_unmap(jrdev, edesc, req, digestsize);
	kfree(edesc);
	return -ENOMEM;

}

/* submit ahash update if it the first job descriptor after update */
static int ahash_update_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int *buflen = state->current_buf ? &state->buflen_1 : &state->buflen_0;
	u8 *next_buf = state->current_buf ? state->buf_0 : state->buf_1;
	int *next_buflen = state->current_buf ? &state->buflen_0 :
			   &state->buflen_1;
	int in_len = *buflen + req->nbytes, to_hash;
	int sec4_sg_bytes, src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	u32 *desc;
	int ret = 0;

	*next_buflen = in_len & (crypto_tfm_alg_blocksize(&ahash->base) - 1);
	to_hash = in_len - *next_buflen;

	if (to_hash) {
		src_nents = sg_nents_for_len(req->src,
					     req->nbytes - *next_buflen);
		if (src_nents < 0) {
			dev_err(jrdev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(jrdev, "unable to DMA map source\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		sec4_sg_bytes = (1 + mapped_nents) *
				sizeof(struct sec4_sg_entry);

		/*
		 * allocate space for base edesc and hw desc commands,
		 * link tables
		 */
		edesc = ahash_edesc_alloc(ctx, 1 + mapped_nents,
					  ctx->sh_desc_update_first,
					  ctx->sh_desc_update_first_dma,
					  flags);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		edesc->sec4_sg_bytes = sec4_sg_bytes;
		edesc->dst_dma = 0;

		state->buf_dma = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg,
						    buf, *buflen);
		sg_to_sec4_sg_last(req->src, mapped_nents,
				   edesc->sec4_sg + 1, 0);

		if (*next_buflen) {
			scatterwalk_map_and_copy(next_buf, req->src,
						 to_hash - *buflen,
						 *next_buflen, 0);
		}

		state->current_buf = !state->current_buf;

		desc = edesc->hw_desc;

		edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
						    sec4_sg_bytes,
						    DMA_TO_DEVICE);
		if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
			dev_err(jrdev, "unable to map S/G table\n");
			ret = -ENOMEM;
			goto unmap_ctx;
		}

		append_seq_in_ptr(desc, edesc->sec4_sg_dma, to_hash, LDST_SGF);

		ret = map_seq_out_ptr_ctx(desc, jrdev, state, ctx->ctx_len);
		if (ret)
			goto unmap_ctx;

#ifdef DEBUG
		print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, desc,
			       desc_bytes(desc), 1);
#endif

		ret = caam_jr_enqueue(jrdev, desc, ahash_done_ctx_dst, req);
		if (ret)
			goto unmap_ctx;

		ret = -EINPROGRESS;
		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;
		*next_buflen = 0;
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "buf@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, buf, *buflen, 1);
	print_hex_dump(KERN_ERR, "next buf@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, next_buf,
		       *next_buflen, 1);
#endif

	return ret;
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, DMA_TO_DEVICE);
	kfree(edesc);
	return ret;
}

/* submit ahash finup if it the first job descriptor after update */
static int ahash_finup_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *buf = state->current_buf ? state->buf_1 : state->buf_0;
	int buflen = state->current_buf ? state->buflen_1 : state->buflen_0;
	int last_buflen = state->current_buf ? state->buflen_0 :
			  state->buflen_1;
	u32 *desc;
	int sec4_sg_bytes, sec4_sg_src_index, src_nents, mapped_nents;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (src_nents < 0) {
		dev_err(jrdev, "Invalid number of src SG.\n");
		return src_nents;
	}

	if (src_nents) {
		mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
					  DMA_TO_DEVICE);
		if (!mapped_nents) {
			dev_err(jrdev, "unable to DMA map source\n");
			return -ENOMEM;
		}
	} else {
		mapped_nents = 0;
	}

	sec4_sg_src_index = 2;
	sec4_sg_bytes = (sec4_sg_src_index + mapped_nents) *
			 sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(ctx, sec4_sg_src_index + mapped_nents,
				  ctx->sh_desc_digest, ctx->sh_desc_digest_dma,
				  flags);
	if (!edesc) {
		dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	desc = edesc->hw_desc;

	edesc->src_nents = src_nents;
	edesc->sec4_sg_bytes = sec4_sg_bytes;

	state->buf_dma = try_buf_map_to_sec4_sg(jrdev, edesc->sec4_sg, buf,
						state->buf_dma, buflen,
						last_buflen);

	ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents, 1, buflen,
				  req->nbytes);
	if (ret) {
		dev_err(jrdev, "unable to map S/G table\n");
		goto unmap;
	}

	edesc->dst_dma = map_seq_out_ptr_result(desc, jrdev, req->result,
						digestsize);
	if (dma_mapping_error(jrdev, edesc->dst_dma)) {
		dev_err(jrdev, "unable to map dst\n");
		goto unmap;
	}

#ifdef DEBUG
	print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc), 1);
#endif

	ret = caam_jr_enqueue(jrdev, desc, ahash_done, req);
	if (!ret) {
		ret = -EINPROGRESS;
	} else {
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
	}

	return ret;
 unmap:
	ahash_unmap(jrdev, edesc, req, digestsize);
	kfree(edesc);
	return -ENOMEM;

}

/* submit first update job descriptor after init */
static int ahash_update_first(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	gfp_t flags = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	u8 *next_buf = state->current_buf ? state->buf_1 : state->buf_0;
	int *next_buflen = state->current_buf ?
		&state->buflen_1 : &state->buflen_0;
	int to_hash;
	u32 *desc;
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = req->nbytes & (crypto_tfm_alg_blocksize(&ahash->base) -
				      1);
	to_hash = req->nbytes - *next_buflen;

	if (to_hash) {
		src_nents = sg_nents_for_len(req->src,
					     req->nbytes - *next_buflen);
		if (src_nents < 0) {
			dev_err(jrdev, "Invalid number of src SG.\n");
			return src_nents;
		}

		if (src_nents) {
			mapped_nents = dma_map_sg(jrdev, req->src, src_nents,
						  DMA_TO_DEVICE);
			if (!mapped_nents) {
				dev_err(jrdev, "unable to map source for DMA\n");
				return -ENOMEM;
			}
		} else {
			mapped_nents = 0;
		}

		/*
		 * allocate space for base edesc and hw desc commands,
		 * link tables
		 */
		edesc = ahash_edesc_alloc(ctx, mapped_nents > 1 ?
					  mapped_nents : 0,
					  ctx->sh_desc_update_first,
					  ctx->sh_desc_update_first_dma,
					  flags);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		edesc->dst_dma = 0;

		ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents, 0, 0,
					  to_hash);
		if (ret)
			goto unmap_ctx;

		if (*next_buflen)
			scatterwalk_map_and_copy(next_buf, req->src, to_hash,
						 *next_buflen, 0);

		desc = edesc->hw_desc;

		ret = map_seq_out_ptr_ctx(desc, jrdev, state, ctx->ctx_len);
		if (ret)
			goto unmap_ctx;

#ifdef DEBUG
		print_hex_dump(KERN_ERR, "jobdesc@"__stringify(__LINE__)": ",
			       DUMP_PREFIX_ADDRESS, 16, 4, desc,
			       desc_bytes(desc), 1);
#endif

		ret = caam_jr_enqueue(jrdev, desc, ahash_done_ctx_dst, req);
		if (ret)
			goto unmap_ctx;

		ret = -EINPROGRESS;
		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		state->update = ahash_update_no_ctx;
		state->finup = ahash_finup_no_ctx;
		state->final = ahash_final_no_ctx;
		scatterwalk_map_and_copy(next_buf, req->src, 0,
					 req->nbytes, 0);
	}
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "next buf@"__stringify(__LINE__)": ",
		       DUMP_PREFIX_ADDRESS, 16, 4, next_buf,
		       *next_buflen, 1);
#endif

	return ret;
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, DMA_TO_DEVICE);
	kfree(edesc);
	return ret;
}

static int ahash_finup_first(struct ahash_request *req)
{
	return ahash_digest(req);
}

static int ahash_init(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	state->update = ahash_update_first;
	state->finup = ahash_finup_first;
	state->final = ahash_final_no_ctx;

	state->current_buf = 0;
	state->buf_dma = 0;
	state->buflen_0 = 0;
	state->buflen_1 = 0;

	return 0;
}

static int ahash_update(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	return state->update(req);
}

static int ahash_finup(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	return state->finup(req);
}

static int ahash_final(struct ahash_request *req)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	return state->final(req);
}

static int ahash_export(struct ahash_request *req, void *out)
{
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct caam_export_state *export = out;
	int len;
	u8 *buf;

	if (state->current_buf) {
		buf = state->buf_1;
		len = state->buflen_1;
	} else {
		buf = state->buf_0;
		len = state->buflen_0;
	}

	memcpy(export->buf, buf, len);
	memcpy(export->caam_ctx, state->caam_ctx, sizeof(export->caam_ctx));
	export->buflen = len;
	export->update = state->update;
	export->final = state->final;
	export->finup = state->finup;

	return 0;
}

static int ahash_import(struct ahash_request *req, const void *in)
{
	struct caam_hash_state *state = ahash_request_ctx(req);
	const struct caam_export_state *export = in;

	memset(state, 0, sizeof(*state));
	memcpy(state->buf_0, export->buf, export->buflen);
	memcpy(state->caam_ctx, export->caam_ctx, sizeof(state->caam_ctx));
	state->buflen_0 = export->buflen;
	state->update = export->update;
	state->final = export->final;
	state->finup = export->finup;

	return 0;
}

struct caam_hash_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	char hmac_name[CRYPTO_MAX_ALG_NAME];
	char hmac_driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	struct ahash_alg template_ahash;
	u32 alg_type;
	u32 alg_op;
};

/* ahash descriptors */
static struct caam_hash_template driver_hash[] = {
	{
		.name = "sha1",
		.driver_name = "sha1-caam",
		.hmac_name = "hmac(sha1)",
		.hmac_driver_name = "hmac-sha1-caam",
		.blocksize = SHA1_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA1,
		.alg_op = OP_ALG_ALGSEL_SHA1 | OP_ALG_AAI_HMAC,
	}, {
		.name = "sha224",
		.driver_name = "sha224-caam",
		.hmac_name = "hmac(sha224)",
		.hmac_driver_name = "hmac-sha224-caam",
		.blocksize = SHA224_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA224,
		.alg_op = OP_ALG_ALGSEL_SHA224 | OP_ALG_AAI_HMAC,
	}, {
		.name = "sha256",
		.driver_name = "sha256-caam",
		.hmac_name = "hmac(sha256)",
		.hmac_driver_name = "hmac-sha256-caam",
		.blocksize = SHA256_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA256,
		.alg_op = OP_ALG_ALGSEL_SHA256 | OP_ALG_AAI_HMAC,
	}, {
		.name = "sha384",
		.driver_name = "sha384-caam",
		.hmac_name = "hmac(sha384)",
		.hmac_driver_name = "hmac-sha384-caam",
		.blocksize = SHA384_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA384,
		.alg_op = OP_ALG_ALGSEL_SHA384 | OP_ALG_AAI_HMAC,
	}, {
		.name = "sha512",
		.driver_name = "sha512-caam",
		.hmac_name = "hmac(sha512)",
		.hmac_driver_name = "hmac-sha512-caam",
		.blocksize = SHA512_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_SHA512,
		.alg_op = OP_ALG_ALGSEL_SHA512 | OP_ALG_AAI_HMAC,
	}, {
		.name = "md5",
		.driver_name = "md5-caam",
		.hmac_name = "hmac(md5)",
		.hmac_driver_name = "hmac-md5-caam",
		.blocksize = MD5_BLOCK_WORDS * 4,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = ahash_setkey,
			.halg = {
				.digestsize = MD5_DIGEST_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		},
		.alg_type = OP_ALG_ALGSEL_MD5,
		.alg_op = OP_ALG_ALGSEL_MD5 | OP_ALG_AAI_HMAC,
	},
};

struct caam_hash_alg {
	struct list_head entry;
	int alg_type;
	int alg_op;
	struct ahash_alg ahash_alg;
};

static int caam_hash_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct crypto_alg *base = tfm->__crt_alg;
	struct hash_alg_common *halg =
		 container_of(base, struct hash_alg_common, base);
	struct ahash_alg *alg =
		 container_of(halg, struct ahash_alg, halg);
	struct caam_hash_alg *caam_hash =
		 container_of(alg, struct caam_hash_alg, ahash_alg);
	struct caam_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	/* Sizes for MDHA running digests: MD5, SHA1, 224, 256, 384, 512 */
	static const u8 runninglen[] = { HASH_MSG_LEN + MD5_DIGEST_SIZE,
					 HASH_MSG_LEN + SHA1_DIGEST_SIZE,
					 HASH_MSG_LEN + 32,
					 HASH_MSG_LEN + SHA256_DIGEST_SIZE,
					 HASH_MSG_LEN + 64,
					 HASH_MSG_LEN + SHA512_DIGEST_SIZE };

	/*
	 * Get a Job ring from Job Ring driver to ensure in-order
	 * crypto request processing per tfm
	 */
	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}
	/* copy descriptor header template value */
	ctx->alg_type = OP_TYPE_CLASS2_ALG | caam_hash->alg_type;
	ctx->alg_op = OP_TYPE_CLASS2_ALG | caam_hash->alg_op;

	ctx->ctx_len = runninglen[(ctx->alg_op & OP_ALG_ALGSEL_SUBMASK) >>
				  OP_ALG_ALGSEL_SHIFT];

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct caam_hash_state));
	return ahash_set_sh_desc(ahash);
}

static void caam_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct caam_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->sh_desc_update_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_update_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_update_dma,
				 desc_bytes(ctx->sh_desc_update),
				 DMA_TO_DEVICE);
	if (ctx->sh_desc_update_first_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_update_first_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_update_first_dma,
				 desc_bytes(ctx->sh_desc_update_first),
				 DMA_TO_DEVICE);
	if (ctx->sh_desc_fin_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_fin_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_fin_dma,
				 desc_bytes(ctx->sh_desc_fin), DMA_TO_DEVICE);
	if (ctx->sh_desc_digest_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_digest_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_digest_dma,
				 desc_bytes(ctx->sh_desc_digest),
				 DMA_TO_DEVICE);
	if (ctx->sh_desc_finup_dma &&
	    !dma_mapping_error(ctx->jrdev, ctx->sh_desc_finup_dma))
		dma_unmap_single(ctx->jrdev, ctx->sh_desc_finup_dma,
				 desc_bytes(ctx->sh_desc_finup), DMA_TO_DEVICE);

	caam_jr_free(ctx->jrdev);
}

static void __exit caam_algapi_hash_exit(void)
{
	struct caam_hash_alg *t_alg, *n;

	if (!hash_list.next)
		return;

	list_for_each_entry_safe(t_alg, n, &hash_list, entry) {
		crypto_unregister_ahash(&t_alg->ahash_alg);
		list_del(&t_alg->entry);
		kfree(t_alg);
	}
}

static struct caam_hash_alg *
caam_hash_alloc(struct caam_hash_template *template,
		bool keyed)
{
	struct caam_hash_alg *t_alg;
	struct ahash_alg *halg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg) {
		pr_err("failed to allocate t_alg\n");
		return ERR_PTR(-ENOMEM);
	}

	t_alg->ahash_alg = template->template_ahash;
	halg = &t_alg->ahash_alg;
	alg = &halg->halg.base;

	if (keyed) {
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->hmac_name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->hmac_driver_name);
	} else {
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->driver_name);
		t_alg->ahash_alg.setkey = NULL;
	}
	alg->cra_module = THIS_MODULE;
	alg->cra_init = caam_hash_cra_init;
	alg->cra_exit = caam_hash_cra_exit;
	alg->cra_ctxsize = sizeof(struct caam_hash_ctx);
	alg->cra_priority = CAAM_CRA_PRIORITY;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_TYPE_AHASH;
	alg->cra_type = &crypto_ahash_type;

	t_alg->alg_type = template->alg_type;
	t_alg->alg_op = template->alg_op;

	return t_alg;
}

static int __init caam_algapi_hash_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;
	struct device *ctrldev;
	int i = 0, err = 0;
	struct caam_drv_private *priv;
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	u32 cha_inst, cha_vid;

	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev) {
		of_node_put(dev_node);
		return -ENODEV;
	}

	ctrldev = &pdev->dev;
	priv = dev_get_drvdata(ctrldev);
	of_node_put(dev_node);

	/*
	 * If priv is NULL, it's probably because the caam driver wasn't
	 * properly initialized (e.g. RNG4 init failed). Thus, bail out here.
	 */
	if (!priv)
		return -ENODEV;

	/*
	 * Register crypto algorithms the device supports.  First, identify
	 * presence and attributes of MD block.
	 */
	cha_vid = rd_reg32(&priv->ctrl->perfmon.cha_id_ls);
	cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);

	/*
	 * Skip registration of any hashing algorithms if MD block
	 * is not present.
	 */
	if (!((cha_inst & CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT))
		return -ENODEV;

	/* Limit digest size based on LP256 */
	if ((cha_vid & CHA_ID_LS_MD_MASK) == CHA_ID_LS_MD_LP256)
		md_limit = SHA256_DIGEST_SIZE;

	INIT_LIST_HEAD(&hash_list);

	/* register crypto algorithms the device supports */
	for (i = 0; i < ARRAY_SIZE(driver_hash); i++) {
		struct caam_hash_alg *t_alg;
		struct caam_hash_template *alg = driver_hash + i;

		/* If MD size is not supported by device, skip registration */
		if (alg->template_ahash.halg.digestsize > md_limit)
			continue;

		/* register hmac version */
		t_alg = caam_hash_alloc(alg, true);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			pr_warn("%s alg allocation failed\n", alg->driver_name);
			continue;
		}

		err = crypto_register_ahash(&t_alg->ahash_alg);
		if (err) {
			pr_warn("%s alg registration failed: %d\n",
				t_alg->ahash_alg.halg.base.cra_driver_name,
				err);
			kfree(t_alg);
		} else
			list_add_tail(&t_alg->entry, &hash_list);

		/* register unkeyed version */
		t_alg = caam_hash_alloc(alg, false);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			pr_warn("%s alg allocation failed\n", alg->driver_name);
			continue;
		}

		err = crypto_register_ahash(&t_alg->ahash_alg);
		if (err) {
			pr_warn("%s alg registration failed: %d\n",
				t_alg->ahash_alg.halg.base.cra_driver_name,
				err);
			kfree(t_alg);
		} else
			list_add_tail(&t_alg->entry, &hash_list);
	}

	return err;
}

module_init(caam_algapi_hash_init);
module_exit(caam_algapi_hash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM support for ahash functions of crypto API");
MODULE_AUTHOR("Freescale Semiconductor - NMG");
