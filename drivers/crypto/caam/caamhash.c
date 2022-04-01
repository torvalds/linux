// SPDX-License-Identifier: GPL-2.0+
/*
 * caam - Freescale FSL CAAM support for ahash functions of crypto API
 *
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2018-2019 NXP
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
#include "caamhash_desc.h"
#include <crypto/engine.h>

#define CAAM_CRA_PRIORITY		3000

/* max hash key is max split key size */
#define CAAM_MAX_HASH_KEY_SIZE		(SHA512_DIGEST_SIZE * 2)

#define CAAM_MAX_HASH_BLOCK_SIZE	SHA512_BLOCK_SIZE
#define CAAM_MAX_HASH_DIGEST_SIZE	SHA512_DIGEST_SIZE

#define DESC_HASH_MAX_USED_BYTES	(DESC_AHASH_FINAL_LEN + \
					 CAAM_MAX_HASH_KEY_SIZE)
#define DESC_HASH_MAX_USED_LEN		(DESC_HASH_MAX_USED_BYTES / CAAM_CMD_SZ)

/* caam context sizes for hashes: running digest + 8 */
#define HASH_MSG_LEN			8
#define MAX_CTX_LEN			(HASH_MSG_LEN + SHA512_DIGEST_SIZE)

static struct list_head hash_list;

/* ahash per-session context */
struct caam_hash_ctx {
	struct crypto_engine_ctx enginectx;
	u32 sh_desc_update[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_update_first[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_fin[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u32 sh_desc_digest[DESC_HASH_MAX_USED_LEN] ____cacheline_aligned;
	u8 key[CAAM_MAX_HASH_KEY_SIZE] ____cacheline_aligned;
	dma_addr_t sh_desc_update_dma ____cacheline_aligned;
	dma_addr_t sh_desc_update_first_dma;
	dma_addr_t sh_desc_fin_dma;
	dma_addr_t sh_desc_digest_dma;
	enum dma_data_direction dir;
	enum dma_data_direction key_dir;
	struct device *jrdev;
	int ctx_len;
	struct alginfo adata;
};

/* ahash state */
struct caam_hash_state {
	dma_addr_t buf_dma;
	dma_addr_t ctx_dma;
	int ctx_dma_len;
	u8 buf[CAAM_MAX_HASH_BLOCK_SIZE] ____cacheline_aligned;
	int buflen;
	int next_buflen;
	u8 caam_ctx[MAX_CTX_LEN] ____cacheline_aligned;
	int (*update)(struct ahash_request *req) ____cacheline_aligned;
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
	struct ahash_edesc *edesc;
	void (*ahash_op_done)(struct device *jrdev, u32 *desc, u32 err,
			      void *context);
};

struct caam_export_state {
	u8 buf[CAAM_MAX_HASH_BLOCK_SIZE];
	u8 caam_ctx[MAX_CTX_LEN];
	int buflen;
	int (*update)(struct ahash_request *req);
	int (*final)(struct ahash_request *req);
	int (*finup)(struct ahash_request *req);
};

static inline bool is_cmac_aes(u32 algtype)
{
	return (algtype & (OP_ALG_ALGSEL_MASK | OP_ALG_AAI_MASK)) ==
	       (OP_ALG_ALGSEL_AES | OP_ALG_AAI_CMAC);
}
/* Common job descriptor seq in/out ptr routines */

/* Map state->caam_ctx, and append seq_out_ptr command that points to it */
static inline int map_seq_out_ptr_ctx(u32 *desc, struct device *jrdev,
				      struct caam_hash_state *state,
				      int ctx_len)
{
	state->ctx_dma_len = ctx_len;
	state->ctx_dma = dma_map_single(jrdev, state->caam_ctx,
					ctx_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, state->ctx_dma)) {
		dev_err(jrdev, "unable to map ctx\n");
		state->ctx_dma = 0;
		return -ENOMEM;
	}

	append_seq_out_ptr(desc, state->ctx_dma, ctx_len, 0);

	return 0;
}

/* Map current buffer in state (if length > 0) and put it in link table */
static inline int buf_map_to_sec4_sg(struct device *jrdev,
				     struct sec4_sg_entry *sec4_sg,
				     struct caam_hash_state *state)
{
	int buflen = state->buflen;

	if (!buflen)
		return 0;

	state->buf_dma = dma_map_single(jrdev, state->buf, buflen,
					DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, state->buf_dma)) {
		dev_err(jrdev, "unable to map buf\n");
		state->buf_dma = 0;
		return -ENOMEM;
	}

	dma_to_sec4_sg_one(sec4_sg, state->buf_dma, buflen, 0);

	return 0;
}

/* Map state->caam_ctx, and add it to link table */
static inline int ctx_map_to_sec4_sg(struct device *jrdev,
				     struct caam_hash_state *state, int ctx_len,
				     struct sec4_sg_entry *sec4_sg, u32 flag)
{
	state->ctx_dma_len = ctx_len;
	state->ctx_dma = dma_map_single(jrdev, state->caam_ctx, ctx_len, flag);
	if (dma_mapping_error(jrdev, state->ctx_dma)) {
		dev_err(jrdev, "unable to map ctx\n");
		state->ctx_dma = 0;
		return -ENOMEM;
	}

	dma_to_sec4_sg_one(sec4_sg, state->ctx_dma, ctx_len, 0);

	return 0;
}

static int ahash_set_sh_desc(struct crypto_ahash *ahash)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct device *jrdev = ctx->jrdev;
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(jrdev->parent);
	u32 *desc;

	ctx->adata.key_virt = ctx->key;

	/* ahash_update shared descriptor */
	desc = ctx->sh_desc_update;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_UPDATE, ctx->ctx_len,
			  ctx->ctx_len, true, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_dma,
				   desc_bytes(desc), ctx->dir);

	print_hex_dump_debug("ahash update shdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* ahash_update_first shared descriptor */
	desc = ctx->sh_desc_update_first;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_INIT, ctx->ctx_len,
			  ctx->ctx_len, false, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_first_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("ahash update first shdesc@"__stringify(__LINE__)
			     ": ", DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* ahash_final shared descriptor */
	desc = ctx->sh_desc_fin;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_FINALIZE, digestsize,
			  ctx->ctx_len, true, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_fin_dma,
				   desc_bytes(desc), ctx->dir);

	print_hex_dump_debug("ahash final shdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* ahash_digest shared descriptor */
	desc = ctx->sh_desc_digest;
	cnstr_shdsc_ahash(desc, &ctx->adata, OP_ALG_AS_INITFINAL, digestsize,
			  ctx->ctx_len, false, ctrlpriv->era);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_digest_dma,
				   desc_bytes(desc), ctx->dir);

	print_hex_dump_debug("ahash digest shdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	return 0;
}

static int axcbc_set_sh_desc(struct crypto_ahash *ahash)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;

	/* shared descriptor for ahash_update */
	desc = ctx->sh_desc_update;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_UPDATE,
			    ctx->ctx_len, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("axcbc update shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* shared descriptor for ahash_{final,finup} */
	desc = ctx->sh_desc_fin;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_FINALIZE,
			    digestsize, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_fin_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("axcbc finup shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	/* key is immediate data for INIT and INITFINAL states */
	ctx->adata.key_virt = ctx->key;

	/* shared descriptor for first invocation of ahash_update */
	desc = ctx->sh_desc_update_first;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_INIT, ctx->ctx_len,
			    ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_first_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("axcbc update first shdesc@" __stringify(__LINE__)
			     " : ", DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* shared descriptor for ahash_digest */
	desc = ctx->sh_desc_digest;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_INITFINAL,
			    digestsize, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_digest_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("axcbc digest shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);
	return 0;
}

static int acmac_set_sh_desc(struct crypto_ahash *ahash)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;

	/* shared descriptor for ahash_update */
	desc = ctx->sh_desc_update;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_UPDATE,
			    ctx->ctx_len, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("acmac update shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* shared descriptor for ahash_{final,finup} */
	desc = ctx->sh_desc_fin;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_FINALIZE,
			    digestsize, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_fin_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("acmac finup shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* shared descriptor for first invocation of ahash_update */
	desc = ctx->sh_desc_update_first;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_INIT, ctx->ctx_len,
			    ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_update_first_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("acmac update first shdesc@" __stringify(__LINE__)
			     " : ", DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	/* shared descriptor for ahash_digest */
	desc = ctx->sh_desc_digest;
	cnstr_shdsc_sk_hash(desc, &ctx->adata, OP_ALG_AS_INITFINAL,
			    digestsize, ctx->ctx_len);
	dma_sync_single_for_device(jrdev, ctx->sh_desc_digest_dma,
				   desc_bytes(desc), ctx->dir);
	print_hex_dump_debug("acmac digest shdesc@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc,
			     desc_bytes(desc), 1);

	return 0;
}

/* Digest hash size if it is too large */
static int hash_digest_key(struct caam_hash_ctx *ctx, u32 *keylen, u8 *key,
			   u32 digestsize)
{
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	struct split_key_result result;
	dma_addr_t key_dma;
	int ret;

	desc = kmalloc(CAAM_CMD_SZ * 8 + CAAM_PTR_SZ * 2, GFP_KERNEL | GFP_DMA);
	if (!desc) {
		dev_err(jrdev, "unable to allocate key input memory\n");
		return -ENOMEM;
	}

	init_job_desc(desc, 0);

	key_dma = dma_map_single(jrdev, key, *keylen, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(jrdev, key_dma)) {
		dev_err(jrdev, "unable to map key memory\n");
		kfree(desc);
		return -ENOMEM;
	}

	/* Job descriptor to perform unkeyed hash on key_in */
	append_operation(desc, ctx->adata.algtype | OP_ALG_ENCRYPT |
			 OP_ALG_AS_INITFINAL);
	append_seq_in_ptr(desc, key_dma, *keylen, 0);
	append_seq_fifo_load(desc, *keylen, FIFOLD_CLASS_CLASS2 |
			     FIFOLD_TYPE_LAST2 | FIFOLD_TYPE_MSG);
	append_seq_out_ptr(desc, key_dma, digestsize, 0);
	append_seq_store(desc, digestsize, LDST_CLASS_2_CCB |
			 LDST_SRCDST_BYTE_CONTEXT);

	print_hex_dump_debug("key_in@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, *keylen, 1);
	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	result.err = 0;
	init_completion(&result.completion);

	ret = caam_jr_enqueue(jrdev, desc, split_key_done, &result);
	if (ret == -EINPROGRESS) {
		/* in progress */
		wait_for_completion(&result.completion);
		ret = result.err;

		print_hex_dump_debug("digested key@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, key,
				     digestsize, 1);
	}
	dma_unmap_single(jrdev, key_dma, *keylen, DMA_BIDIRECTIONAL);

	*keylen = digestsize;

	kfree(desc);

	return ret;
}

static int ahash_setkey(struct crypto_ahash *ahash,
			const u8 *key, unsigned int keylen)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *jrdev = ctx->jrdev;
	int blocksize = crypto_tfm_alg_blocksize(&ahash->base);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct caam_drv_private *ctrlpriv = dev_get_drvdata(ctx->jrdev->parent);
	int ret;
	u8 *hashed_key = NULL;

	dev_dbg(jrdev, "keylen %d\n", keylen);

	if (keylen > blocksize) {
		hashed_key = kmemdup(key, keylen, GFP_KERNEL | GFP_DMA);
		if (!hashed_key)
			return -ENOMEM;
		ret = hash_digest_key(ctx, &keylen, hashed_key, digestsize);
		if (ret)
			goto bad_free_key;
		key = hashed_key;
	}

	/*
	 * If DKP is supported, use it in the shared descriptor to generate
	 * the split key.
	 */
	if (ctrlpriv->era >= 6) {
		ctx->adata.key_inline = true;
		ctx->adata.keylen = keylen;
		ctx->adata.keylen_pad = split_key_len(ctx->adata.algtype &
						      OP_ALG_ALGSEL_MASK);

		if (ctx->adata.keylen_pad > CAAM_MAX_HASH_KEY_SIZE)
			goto bad_free_key;

		memcpy(ctx->key, key, keylen);

		/*
		 * In case |user key| > |derived key|, using DKP<imm,imm>
		 * would result in invalid opcodes (last bytes of user key) in
		 * the resulting descriptor. Use DKP<ptr,imm> instead => both
		 * virtual and dma key addresses are needed.
		 */
		if (keylen > ctx->adata.keylen_pad)
			dma_sync_single_for_device(ctx->jrdev,
						   ctx->adata.key_dma,
						   ctx->adata.keylen_pad,
						   DMA_TO_DEVICE);
	} else {
		ret = gen_split_key(ctx->jrdev, ctx->key, &ctx->adata, key,
				    keylen, CAAM_MAX_HASH_KEY_SIZE);
		if (ret)
			goto bad_free_key;
	}

	kfree(hashed_key);
	return ahash_set_sh_desc(ahash);
 bad_free_key:
	kfree(hashed_key);
	return -EINVAL;
}

static int axcbc_setkey(struct crypto_ahash *ahash, const u8 *key,
			unsigned int keylen)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *jrdev = ctx->jrdev;

	if (keylen != AES_KEYSIZE_128)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	dma_sync_single_for_device(jrdev, ctx->adata.key_dma, keylen,
				   DMA_TO_DEVICE);
	ctx->adata.keylen = keylen;

	print_hex_dump_debug("axcbc ctx.key@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, ctx->key, keylen, 1);

	return axcbc_set_sh_desc(ahash);
}

static int acmac_setkey(struct crypto_ahash *ahash, const u8 *key,
			unsigned int keylen)
{
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int err;

	err = aes_check_keylen(keylen);
	if (err)
		return err;

	/* key is immediate data for all cmac shared descriptors */
	ctx->adata.key_virt = key;
	ctx->adata.keylen = keylen;

	print_hex_dump_debug("acmac ctx.key@" __stringify(__LINE__)" : ",
			     DUMP_PREFIX_ADDRESS, 16, 4, key, keylen, 1);

	return acmac_set_sh_desc(ahash);
}

/*
 * ahash_edesc - s/w-extended ahash descriptor
 * @sec4_sg_dma: physical mapped address of h/w link table
 * @src_nents: number of segments in input scatterlist
 * @sec4_sg_bytes: length of dma mapped sec4_sg space
 * @bklog: stored to determine if the request needs backlog
 * @hw_desc: the h/w job descriptor followed by any referenced link tables
 * @sec4_sg: h/w link table
 */
struct ahash_edesc {
	dma_addr_t sec4_sg_dma;
	int src_nents;
	int sec4_sg_bytes;
	bool bklog;
	u32 hw_desc[DESC_JOB_IO_LEN_MAX / sizeof(u32)] ____cacheline_aligned;
	struct sec4_sg_entry sec4_sg[];
};

static inline void ahash_unmap(struct device *dev,
			struct ahash_edesc *edesc,
			struct ahash_request *req, int dst_len)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	if (edesc->src_nents)
		dma_unmap_sg(dev, req->src, edesc->src_nents, DMA_TO_DEVICE);

	if (edesc->sec4_sg_bytes)
		dma_unmap_single(dev, edesc->sec4_sg_dma,
				 edesc->sec4_sg_bytes, DMA_TO_DEVICE);

	if (state->buf_dma) {
		dma_unmap_single(dev, state->buf_dma, state->buflen,
				 DMA_TO_DEVICE);
		state->buf_dma = 0;
	}
}

static inline void ahash_unmap_ctx(struct device *dev,
			struct ahash_edesc *edesc,
			struct ahash_request *req, int dst_len, u32 flag)
{
	struct caam_hash_state *state = ahash_request_ctx(req);

	if (state->ctx_dma) {
		dma_unmap_single(dev, state->ctx_dma, state->ctx_dma_len, flag);
		state->ctx_dma = 0;
	}
	ahash_unmap(dev, edesc, req, dst_len);
}

static inline void ahash_done_cpy(struct device *jrdev, u32 *desc, u32 err,
				  void *context, enum dma_data_direction dir)
{
	struct ahash_request *req = context;
	struct caam_drv_private_jr *jrp = dev_get_drvdata(jrdev);
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int ecode = 0;
	bool has_bklog;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = state->edesc;
	has_bklog = edesc->bklog;

	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	ahash_unmap_ctx(jrdev, edesc, req, digestsize, dir);
	memcpy(req->result, state->caam_ctx, digestsize);
	kfree(edesc);

	print_hex_dump_debug("ctx@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);

	/*
	 * If no backlog flag, the completion of the request is done
	 * by CAAM, not crypto engine.
	 */
	if (!has_bklog)
		req->base.complete(&req->base, ecode);
	else
		crypto_finalize_hash_request(jrp->engine, req, ecode);
}

static void ahash_done(struct device *jrdev, u32 *desc, u32 err,
		       void *context)
{
	ahash_done_cpy(jrdev, desc, err, context, DMA_FROM_DEVICE);
}

static void ahash_done_ctx_src(struct device *jrdev, u32 *desc, u32 err,
			       void *context)
{
	ahash_done_cpy(jrdev, desc, err, context, DMA_BIDIRECTIONAL);
}

static inline void ahash_done_switch(struct device *jrdev, u32 *desc, u32 err,
				     void *context, enum dma_data_direction dir)
{
	struct ahash_request *req = context;
	struct caam_drv_private_jr *jrp = dev_get_drvdata(jrdev);
	struct ahash_edesc *edesc;
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	int digestsize = crypto_ahash_digestsize(ahash);
	int ecode = 0;
	bool has_bklog;

	dev_dbg(jrdev, "%s %d: err 0x%x\n", __func__, __LINE__, err);

	edesc = state->edesc;
	has_bklog = edesc->bklog;
	if (err)
		ecode = caam_jr_strstatus(jrdev, err);

	ahash_unmap_ctx(jrdev, edesc, req, ctx->ctx_len, dir);
	kfree(edesc);

	scatterwalk_map_and_copy(state->buf, req->src,
				 req->nbytes - state->next_buflen,
				 state->next_buflen, 0);
	state->buflen = state->next_buflen;

	print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->buf,
			     state->buflen, 1);

	print_hex_dump_debug("ctx@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, state->caam_ctx,
			     ctx->ctx_len, 1);
	if (req->result)
		print_hex_dump_debug("result@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, req->result,
				     digestsize, 1);

	/*
	 * If no backlog flag, the completion of the request is done
	 * by CAAM, not crypto engine.
	 */
	if (!has_bklog)
		req->base.complete(&req->base, ecode);
	else
		crypto_finalize_hash_request(jrp->engine, req, ecode);

}

static void ahash_done_bi(struct device *jrdev, u32 *desc, u32 err,
			  void *context)
{
	ahash_done_switch(jrdev, desc, err, context, DMA_BIDIRECTIONAL);
}

static void ahash_done_ctx_dst(struct device *jrdev, u32 *desc, u32 err,
			       void *context)
{
	ahash_done_switch(jrdev, desc, err, context, DMA_FROM_DEVICE);
}

/*
 * Allocate an enhanced descriptor, which contains the hardware descriptor
 * and space for hardware scatter table containing sg_num entries.
 */
static struct ahash_edesc *ahash_edesc_alloc(struct ahash_request *req,
					     int sg_num, u32 *sh_desc,
					     dma_addr_t sh_desc_dma)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	struct ahash_edesc *edesc;
	unsigned int sg_size = sg_num * sizeof(struct sec4_sg_entry);

	edesc = kzalloc(sizeof(*edesc) + sg_size, GFP_DMA | flags);
	if (!edesc) {
		dev_err(ctx->jrdev, "could not allocate extended descriptor\n");
		return NULL;
	}

	state->edesc = edesc;

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
		unsigned int sgsize = sizeof(*sg) *
				      pad_sg_nents(first_sg + nents);

		sg_to_sec4_sg_last(req->src, to_hash, sg + first_sg, 0);

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

static int ahash_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	u32 *desc = state->edesc->hw_desc;
	int ret;

	state->edesc->bklog = true;

	ret = caam_jr_enqueue(jrdev, desc, state->ahash_op_done, req);

	if (ret == -ENOSPC && engine->retry_support)
		return ret;

	if (ret != -EINPROGRESS) {
		ahash_unmap(jrdev, state->edesc, req, 0);
		kfree(state->edesc);
	} else {
		ret = 0;
	}

	return ret;
}

static int ahash_enqueue_req(struct device *jrdev,
			     void (*cbk)(struct device *jrdev, u32 *desc,
					 u32 err, void *context),
			     struct ahash_request *req,
			     int dst_len, enum dma_data_direction dir)
{
	struct caam_drv_private_jr *jrpriv = dev_get_drvdata(jrdev);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct ahash_edesc *edesc = state->edesc;
	u32 *desc = edesc->hw_desc;
	int ret;

	state->ahash_op_done = cbk;

	/*
	 * Only the backlog request are sent to crypto-engine since the others
	 * can be handled by CAAM, if free, especially since JR has up to 1024
	 * entries (more than the 10 entries from crypto-engine).
	 */
	if (req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)
		ret = crypto_transfer_hash_request_to_engine(jrpriv->engine,
							     req);
	else
		ret = caam_jr_enqueue(jrdev, desc, cbk, req);

	if ((ret != -EINPROGRESS) && (ret != -EBUSY)) {
		ahash_unmap_ctx(jrdev, edesc, req, dst_len, dir);
		kfree(edesc);
	}

	return ret;
}

/* submit update job descriptor */
static int ahash_update_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int blocksize = crypto_ahash_blocksize(ahash);
	int in_len = *buflen + req->nbytes, to_hash;
	u32 *desc;
	int src_nents, mapped_nents, sec4_sg_bytes, sec4_sg_src_index;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = in_len & (blocksize - 1);
	to_hash = in_len - *next_buflen;

	/*
	 * For XCBC and CMAC, if to_hash is multiple of block size,
	 * keep last block in internal buffer
	 */
	if ((is_xcbc_aes(ctx->adata.algtype) ||
	     is_cmac_aes(ctx->adata.algtype)) && to_hash >= blocksize &&
	     (*next_buflen == 0)) {
		*next_buflen = blocksize;
		to_hash -= blocksize;
	}

	if (to_hash) {
		int pad_nents;
		int src_len = req->nbytes - *next_buflen;

		src_nents = sg_nents_for_len(req->src, src_len);
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
		pad_nents = pad_sg_nents(sec4_sg_src_index + mapped_nents);
		sec4_sg_bytes = pad_nents * sizeof(struct sec4_sg_entry);

		/*
		 * allocate space for base edesc and hw desc commands,
		 * link tables
		 */
		edesc = ahash_edesc_alloc(req, pad_nents, ctx->sh_desc_update,
					  ctx->sh_desc_update_dma);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		edesc->sec4_sg_bytes = sec4_sg_bytes;

		ret = ctx_map_to_sec4_sg(jrdev, state, ctx->ctx_len,
					 edesc->sec4_sg, DMA_BIDIRECTIONAL);
		if (ret)
			goto unmap_ctx;

		ret = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg + 1, state);
		if (ret)
			goto unmap_ctx;

		if (mapped_nents)
			sg_to_sec4_sg_last(req->src, src_len,
					   edesc->sec4_sg + sec4_sg_src_index,
					   0);
		else
			sg_to_sec4_set_last(edesc->sec4_sg + sec4_sg_src_index -
					    1);

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

		print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, desc,
				     desc_bytes(desc), 1);

		ret = ahash_enqueue_req(jrdev, ahash_done_bi, req,
					ctx->ctx_len, DMA_BIDIRECTIONAL);
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

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
	int buflen = state->buflen;
	u32 *desc;
	int sec4_sg_bytes;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	sec4_sg_bytes = pad_sg_nents(1 + (buflen ? 1 : 0)) *
			sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(req, 4, ctx->sh_desc_fin,
				  ctx->sh_desc_fin_dma);
	if (!edesc)
		return -ENOMEM;

	desc = edesc->hw_desc;

	edesc->sec4_sg_bytes = sec4_sg_bytes;

	ret = ctx_map_to_sec4_sg(jrdev, state, ctx->ctx_len,
				 edesc->sec4_sg, DMA_BIDIRECTIONAL);
	if (ret)
		goto unmap_ctx;

	ret = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg + 1, state);
	if (ret)
		goto unmap_ctx;

	sg_to_sec4_set_last(edesc->sec4_sg + (buflen ? 1 : 0));

	edesc->sec4_sg_dma = dma_map_single(jrdev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(jrdev, edesc->sec4_sg_dma)) {
		dev_err(jrdev, "unable to map S/G table\n");
		ret = -ENOMEM;
		goto unmap_ctx;
	}

	append_seq_in_ptr(desc, edesc->sec4_sg_dma, ctx->ctx_len + buflen,
			  LDST_SGF);
	append_seq_out_ptr(desc, state->ctx_dma, digestsize, 0);

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return ahash_enqueue_req(jrdev, ahash_done_ctx_src, req,
				 digestsize, DMA_BIDIRECTIONAL);
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, digestsize, DMA_BIDIRECTIONAL);
	kfree(edesc);
	return ret;
}

static int ahash_finup_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	int buflen = state->buflen;
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
	edesc = ahash_edesc_alloc(req, sec4_sg_src_index + mapped_nents,
				  ctx->sh_desc_fin, ctx->sh_desc_fin_dma);
	if (!edesc) {
		dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	desc = edesc->hw_desc;

	edesc->src_nents = src_nents;

	ret = ctx_map_to_sec4_sg(jrdev, state, ctx->ctx_len,
				 edesc->sec4_sg, DMA_BIDIRECTIONAL);
	if (ret)
		goto unmap_ctx;

	ret = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg + 1, state);
	if (ret)
		goto unmap_ctx;

	ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents,
				  sec4_sg_src_index, ctx->ctx_len + buflen,
				  req->nbytes);
	if (ret)
		goto unmap_ctx;

	append_seq_out_ptr(desc, state->ctx_dma, digestsize, 0);

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return ahash_enqueue_req(jrdev, ahash_done_ctx_src, req,
				 digestsize, DMA_BIDIRECTIONAL);
 unmap_ctx:
	ahash_unmap_ctx(jrdev, edesc, req, digestsize, DMA_BIDIRECTIONAL);
	kfree(edesc);
	return ret;
}

static int ahash_digest(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	u32 *desc;
	int digestsize = crypto_ahash_digestsize(ahash);
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret;

	state->buf_dma = 0;

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
	edesc = ahash_edesc_alloc(req, mapped_nents > 1 ? mapped_nents : 0,
				  ctx->sh_desc_digest, ctx->sh_desc_digest_dma);
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

	ret = map_seq_out_ptr_ctx(desc, jrdev, state, digestsize);
	if (ret) {
		ahash_unmap(jrdev, edesc, req, digestsize);
		kfree(edesc);
		return -ENOMEM;
	}

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return ahash_enqueue_req(jrdev, ahash_done, req, digestsize,
				 DMA_FROM_DEVICE);
}

/* submit ahash final if it the first job descriptor */
static int ahash_final_no_ctx(struct ahash_request *req)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct caam_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct caam_hash_state *state = ahash_request_ctx(req);
	struct device *jrdev = ctx->jrdev;
	u8 *buf = state->buf;
	int buflen = state->buflen;
	u32 *desc;
	int digestsize = crypto_ahash_digestsize(ahash);
	struct ahash_edesc *edesc;
	int ret;

	/* allocate space for base edesc and hw desc commands, link tables */
	edesc = ahash_edesc_alloc(req, 0, ctx->sh_desc_digest,
				  ctx->sh_desc_digest_dma);
	if (!edesc)
		return -ENOMEM;

	desc = edesc->hw_desc;

	if (buflen) {
		state->buf_dma = dma_map_single(jrdev, buf, buflen,
						DMA_TO_DEVICE);
		if (dma_mapping_error(jrdev, state->buf_dma)) {
			dev_err(jrdev, "unable to map src\n");
			goto unmap;
		}

		append_seq_in_ptr(desc, state->buf_dma, buflen, 0);
	}

	ret = map_seq_out_ptr_ctx(desc, jrdev, state, digestsize);
	if (ret)
		goto unmap;

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return ahash_enqueue_req(jrdev, ahash_done, req,
				 digestsize, DMA_FROM_DEVICE);
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
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int blocksize = crypto_ahash_blocksize(ahash);
	int in_len = *buflen + req->nbytes, to_hash;
	int sec4_sg_bytes, src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	u32 *desc;
	int ret = 0;

	*next_buflen = in_len & (blocksize - 1);
	to_hash = in_len - *next_buflen;

	/*
	 * For XCBC and CMAC, if to_hash is multiple of block size,
	 * keep last block in internal buffer
	 */
	if ((is_xcbc_aes(ctx->adata.algtype) ||
	     is_cmac_aes(ctx->adata.algtype)) && to_hash >= blocksize &&
	     (*next_buflen == 0)) {
		*next_buflen = blocksize;
		to_hash -= blocksize;
	}

	if (to_hash) {
		int pad_nents;
		int src_len = req->nbytes - *next_buflen;

		src_nents = sg_nents_for_len(req->src, src_len);
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

		pad_nents = pad_sg_nents(1 + mapped_nents);
		sec4_sg_bytes = pad_nents * sizeof(struct sec4_sg_entry);

		/*
		 * allocate space for base edesc and hw desc commands,
		 * link tables
		 */
		edesc = ahash_edesc_alloc(req, pad_nents,
					  ctx->sh_desc_update_first,
					  ctx->sh_desc_update_first_dma);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;
		edesc->sec4_sg_bytes = sec4_sg_bytes;

		ret = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg, state);
		if (ret)
			goto unmap_ctx;

		sg_to_sec4_sg_last(req->src, src_len, edesc->sec4_sg + 1, 0);

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

		print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, desc,
				     desc_bytes(desc), 1);

		ret = ahash_enqueue_req(jrdev, ahash_done_ctx_dst, req,
					ctx->ctx_len, DMA_TO_DEVICE);
		if ((ret != -EINPROGRESS) && (ret != -EBUSY))
			return ret;
		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		scatterwalk_map_and_copy(buf + *buflen, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

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
	int buflen = state->buflen;
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
	edesc = ahash_edesc_alloc(req, sec4_sg_src_index + mapped_nents,
				  ctx->sh_desc_digest, ctx->sh_desc_digest_dma);
	if (!edesc) {
		dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	desc = edesc->hw_desc;

	edesc->src_nents = src_nents;
	edesc->sec4_sg_bytes = sec4_sg_bytes;

	ret = buf_map_to_sec4_sg(jrdev, edesc->sec4_sg, state);
	if (ret)
		goto unmap;

	ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents, 1, buflen,
				  req->nbytes);
	if (ret) {
		dev_err(jrdev, "unable to map S/G table\n");
		goto unmap;
	}

	ret = map_seq_out_ptr_ctx(desc, jrdev, state, digestsize);
	if (ret)
		goto unmap;

	print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);

	return ahash_enqueue_req(jrdev, ahash_done, req,
				 digestsize, DMA_FROM_DEVICE);
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
	u8 *buf = state->buf;
	int *buflen = &state->buflen;
	int *next_buflen = &state->next_buflen;
	int to_hash;
	int blocksize = crypto_ahash_blocksize(ahash);
	u32 *desc;
	int src_nents, mapped_nents;
	struct ahash_edesc *edesc;
	int ret = 0;

	*next_buflen = req->nbytes & (blocksize - 1);
	to_hash = req->nbytes - *next_buflen;

	/*
	 * For XCBC and CMAC, if to_hash is multiple of block size,
	 * keep last block in internal buffer
	 */
	if ((is_xcbc_aes(ctx->adata.algtype) ||
	     is_cmac_aes(ctx->adata.algtype)) && to_hash >= blocksize &&
	     (*next_buflen == 0)) {
		*next_buflen = blocksize;
		to_hash -= blocksize;
	}

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
		edesc = ahash_edesc_alloc(req, mapped_nents > 1 ?
					  mapped_nents : 0,
					  ctx->sh_desc_update_first,
					  ctx->sh_desc_update_first_dma);
		if (!edesc) {
			dma_unmap_sg(jrdev, req->src, src_nents, DMA_TO_DEVICE);
			return -ENOMEM;
		}

		edesc->src_nents = src_nents;

		ret = ahash_edesc_add_src(ctx, edesc, req, mapped_nents, 0, 0,
					  to_hash);
		if (ret)
			goto unmap_ctx;

		desc = edesc->hw_desc;

		ret = map_seq_out_ptr_ctx(desc, jrdev, state, ctx->ctx_len);
		if (ret)
			goto unmap_ctx;

		print_hex_dump_debug("jobdesc@"__stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, desc,
				     desc_bytes(desc), 1);

		ret = ahash_enqueue_req(jrdev, ahash_done_ctx_dst, req,
					ctx->ctx_len, DMA_TO_DEVICE);
		if ((ret != -EINPROGRESS) && (ret != -EBUSY))
			return ret;
		state->update = ahash_update_ctx;
		state->finup = ahash_finup_ctx;
		state->final = ahash_final_ctx;
	} else if (*next_buflen) {
		state->update = ahash_update_no_ctx;
		state->finup = ahash_finup_no_ctx;
		state->final = ahash_final_no_ctx;
		scatterwalk_map_and_copy(buf, req->src, 0,
					 req->nbytes, 0);
		*buflen = *next_buflen;

		print_hex_dump_debug("buf@" __stringify(__LINE__)": ",
				     DUMP_PREFIX_ADDRESS, 16, 4, buf,
				     *buflen, 1);
	}

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

	state->ctx_dma = 0;
	state->ctx_dma_len = 0;
	state->buf_dma = 0;
	state->buflen = 0;
	state->next_buflen = 0;

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
	u8 *buf = state->buf;
	int len = state->buflen;

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
	memcpy(state->buf, export->buf, export->buflen);
	memcpy(state->caam_ctx, export->caam_ctx, sizeof(state->caam_ctx));
	state->buflen = export->buflen;
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
	}, {
		.hmac_name = "xcbc(aes)",
		.hmac_driver_name = "xcbc-aes-caam",
		.blocksize = AES_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = axcbc_setkey,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		 },
		.alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_XCBC_MAC,
	}, {
		.hmac_name = "cmac(aes)",
		.hmac_driver_name = "cmac-aes-caam",
		.blocksize = AES_BLOCK_SIZE,
		.template_ahash = {
			.init = ahash_init,
			.update = ahash_update,
			.final = ahash_final,
			.finup = ahash_finup,
			.digest = ahash_digest,
			.export = ahash_export,
			.import = ahash_import,
			.setkey = acmac_setkey,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = sizeof(struct caam_export_state),
			},
		 },
		.alg_type = OP_ALG_ALGSEL_AES | OP_ALG_AAI_CMAC,
	},
};

struct caam_hash_alg {
	struct list_head entry;
	int alg_type;
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
	const size_t sh_desc_update_offset = offsetof(struct caam_hash_ctx,
						      sh_desc_update);
	dma_addr_t dma_addr;
	struct caam_drv_private *priv;

	/*
	 * Get a Job ring from Job Ring driver to ensure in-order
	 * crypto request processing per tfm
	 */
	ctx->jrdev = caam_jr_alloc();
	if (IS_ERR(ctx->jrdev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->jrdev);
	}

	priv = dev_get_drvdata(ctx->jrdev->parent);

	if (is_xcbc_aes(caam_hash->alg_type)) {
		ctx->dir = DMA_TO_DEVICE;
		ctx->key_dir = DMA_BIDIRECTIONAL;
		ctx->adata.algtype = OP_TYPE_CLASS1_ALG | caam_hash->alg_type;
		ctx->ctx_len = 48;
	} else if (is_cmac_aes(caam_hash->alg_type)) {
		ctx->dir = DMA_TO_DEVICE;
		ctx->key_dir = DMA_NONE;
		ctx->adata.algtype = OP_TYPE_CLASS1_ALG | caam_hash->alg_type;
		ctx->ctx_len = 32;
	} else {
		if (priv->era >= 6) {
			ctx->dir = DMA_BIDIRECTIONAL;
			ctx->key_dir = alg->setkey ? DMA_TO_DEVICE : DMA_NONE;
		} else {
			ctx->dir = DMA_TO_DEVICE;
			ctx->key_dir = DMA_NONE;
		}
		ctx->adata.algtype = OP_TYPE_CLASS2_ALG | caam_hash->alg_type;
		ctx->ctx_len = runninglen[(ctx->adata.algtype &
					   OP_ALG_ALGSEL_SUBMASK) >>
					  OP_ALG_ALGSEL_SHIFT];
	}

	if (ctx->key_dir != DMA_NONE) {
		ctx->adata.key_dma = dma_map_single_attrs(ctx->jrdev, ctx->key,
							  ARRAY_SIZE(ctx->key),
							  ctx->key_dir,
							  DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(ctx->jrdev, ctx->adata.key_dma)) {
			dev_err(ctx->jrdev, "unable to map key\n");
			caam_jr_free(ctx->jrdev);
			return -ENOMEM;
		}
	}

	dma_addr = dma_map_single_attrs(ctx->jrdev, ctx->sh_desc_update,
					offsetof(struct caam_hash_ctx, key) -
					sh_desc_update_offset,
					ctx->dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(ctx->jrdev, dma_addr)) {
		dev_err(ctx->jrdev, "unable to map shared descriptors\n");

		if (ctx->key_dir != DMA_NONE)
			dma_unmap_single_attrs(ctx->jrdev, ctx->adata.key_dma,
					       ARRAY_SIZE(ctx->key),
					       ctx->key_dir,
					       DMA_ATTR_SKIP_CPU_SYNC);

		caam_jr_free(ctx->jrdev);
		return -ENOMEM;
	}

	ctx->sh_desc_update_dma = dma_addr;
	ctx->sh_desc_update_first_dma = dma_addr +
					offsetof(struct caam_hash_ctx,
						 sh_desc_update_first) -
					sh_desc_update_offset;
	ctx->sh_desc_fin_dma = dma_addr + offsetof(struct caam_hash_ctx,
						   sh_desc_fin) -
					sh_desc_update_offset;
	ctx->sh_desc_digest_dma = dma_addr + offsetof(struct caam_hash_ctx,
						      sh_desc_digest) -
					sh_desc_update_offset;

	ctx->enginectx.op.do_one_request = ahash_do_one_req;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct caam_hash_state));

	/*
	 * For keyed hash algorithms shared descriptors
	 * will be created later in setkey() callback
	 */
	return alg->setkey ? 0 : ahash_set_sh_desc(ahash);
}

static void caam_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct caam_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	dma_unmap_single_attrs(ctx->jrdev, ctx->sh_desc_update_dma,
			       offsetof(struct caam_hash_ctx, key) -
			       offsetof(struct caam_hash_ctx, sh_desc_update),
			       ctx->dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (ctx->key_dir != DMA_NONE)
		dma_unmap_single_attrs(ctx->jrdev, ctx->adata.key_dma,
				       ARRAY_SIZE(ctx->key), ctx->key_dir,
				       DMA_ATTR_SKIP_CPU_SYNC);
	caam_jr_free(ctx->jrdev);
}

void caam_algapi_hash_exit(void)
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
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY;

	t_alg->alg_type = template->alg_type;

	return t_alg;
}

int caam_algapi_hash_init(struct device *ctrldev)
{
	int i = 0, err = 0;
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);
	unsigned int md_limit = SHA512_DIGEST_SIZE;
	u32 md_inst, md_vid;

	/*
	 * Register crypto algorithms the device supports.  First, identify
	 * presence and attributes of MD block.
	 */
	if (priv->era < 10) {
		md_vid = (rd_reg32(&priv->ctrl->perfmon.cha_id_ls) &
			  CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;
		md_inst = (rd_reg32(&priv->ctrl->perfmon.cha_num_ls) &
			   CHA_ID_LS_MD_MASK) >> CHA_ID_LS_MD_SHIFT;
	} else {
		u32 mdha = rd_reg32(&priv->ctrl->vreg.mdha);

		md_vid = (mdha & CHA_VER_VID_MASK) >> CHA_VER_VID_SHIFT;
		md_inst = mdha & CHA_VER_NUM_MASK;
	}

	/*
	 * Skip registration of any hashing algorithms if MD block
	 * is not present.
	 */
	if (!md_inst)
		return 0;

	/* Limit digest size based on LP256 */
	if (md_vid == CHA_VER_VID_MD_LP256)
		md_limit = SHA256_DIGEST_SIZE;

	INIT_LIST_HEAD(&hash_list);

	/* register crypto algorithms the device supports */
	for (i = 0; i < ARRAY_SIZE(driver_hash); i++) {
		struct caam_hash_alg *t_alg;
		struct caam_hash_template *alg = driver_hash + i;

		/* If MD size is not supported by device, skip registration */
		if (is_mdha(alg->alg_type) &&
		    alg->template_ahash.halg.digestsize > md_limit)
			continue;

		/* register hmac version */
		t_alg = caam_hash_alloc(alg, true);
		if (IS_ERR(t_alg)) {
			err = PTR_ERR(t_alg);
			pr_warn("%s alg allocation failed\n",
				alg->hmac_driver_name);
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

		if ((alg->alg_type & OP_ALG_ALGSEL_MASK) == OP_ALG_ALGSEL_AES)
			continue;

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
