// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <linux/kernel.h>
#include <linux/module.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/sm3.h>
#include <crypto/internal/hash.h>

#include "cc_driver.h"
#include "cc_request_mgr.h"
#include "cc_buffer_mgr.h"
#include "cc_hash.h"
#include "cc_sram_mgr.h"

#define CC_MAX_HASH_SEQ_LEN 12
#define CC_MAX_OPAD_KEYS_SIZE CC_MAX_HASH_BLCK_SIZE
#define CC_SM3_HASH_LEN_SIZE 8

struct cc_hash_handle {
	u32 digest_len_sram_addr;	/* const value in SRAM*/
	u32 larval_digest_sram_addr;   /* const value in SRAM */
	struct list_head hash_list;
};

static const u32 cc_digest_len_init[] = {
	0x00000040, 0x00000000, 0x00000000, 0x00000000 };
static const u32 cc_md5_init[] = {
	SHA1_H3, SHA1_H2, SHA1_H1, SHA1_H0 };
static const u32 cc_sha1_init[] = {
	SHA1_H4, SHA1_H3, SHA1_H2, SHA1_H1, SHA1_H0 };
static const u32 cc_sha224_init[] = {
	SHA224_H7, SHA224_H6, SHA224_H5, SHA224_H4,
	SHA224_H3, SHA224_H2, SHA224_H1, SHA224_H0 };
static const u32 cc_sha256_init[] = {
	SHA256_H7, SHA256_H6, SHA256_H5, SHA256_H4,
	SHA256_H3, SHA256_H2, SHA256_H1, SHA256_H0 };
static const u32 cc_digest_len_sha512_init[] = {
	0x00000080, 0x00000000, 0x00000000, 0x00000000 };

/*
 * Due to the way the HW works, every double word in the SHA384 and SHA512
 * larval hashes must be stored in hi/lo order
 */
#define hilo(x)	upper_32_bits(x), lower_32_bits(x)
static const u32 cc_sha384_init[] = {
	hilo(SHA384_H7), hilo(SHA384_H6), hilo(SHA384_H5), hilo(SHA384_H4),
	hilo(SHA384_H3), hilo(SHA384_H2), hilo(SHA384_H1), hilo(SHA384_H0) };
static const u32 cc_sha512_init[] = {
	hilo(SHA512_H7), hilo(SHA512_H6), hilo(SHA512_H5), hilo(SHA512_H4),
	hilo(SHA512_H3), hilo(SHA512_H2), hilo(SHA512_H1), hilo(SHA512_H0) };

static const u32 cc_sm3_init[] = {
	SM3_IVH, SM3_IVG, SM3_IVF, SM3_IVE,
	SM3_IVD, SM3_IVC, SM3_IVB, SM3_IVA };

static void cc_setup_xcbc(struct ahash_request *areq, struct cc_hw_desc desc[],
			  unsigned int *seq_size);

static void cc_setup_cmac(struct ahash_request *areq, struct cc_hw_desc desc[],
			  unsigned int *seq_size);

static const void *cc_larval_digest(struct device *dev, u32 mode);

struct cc_hash_alg {
	struct list_head entry;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	struct cc_drvdata *drvdata;
	struct ahash_alg ahash_alg;
};

struct hash_key_req_ctx {
	u32 keylen;
	dma_addr_t key_dma_addr;
	u8 *key;
};

/* hash per-session context */
struct cc_hash_ctx {
	struct cc_drvdata *drvdata;
	/* holds the origin digest; the digest after "setkey" if HMAC,*
	 * the initial digest if HASH.
	 */
	u8 digest_buff[CC_MAX_HASH_DIGEST_SIZE]  ____cacheline_aligned;
	u8 opad_tmp_keys_buff[CC_MAX_OPAD_KEYS_SIZE]  ____cacheline_aligned;

	dma_addr_t opad_tmp_keys_dma_addr  ____cacheline_aligned;
	dma_addr_t digest_buff_dma_addr;
	/* use for hmac with key large then mode block size */
	struct hash_key_req_ctx key_params;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	unsigned int hash_len;
	struct completion setkey_comp;
	bool is_hmac;
};

static void cc_set_desc(struct ahash_req_ctx *areq_ctx, struct cc_hash_ctx *ctx,
			unsigned int flow_mode, struct cc_hw_desc desc[],
			bool is_not_last_data, unsigned int *seq_size);

static void cc_set_endianity(u32 mode, struct cc_hw_desc *desc)
{
	if (mode == DRV_HASH_MD5 || mode == DRV_HASH_SHA384 ||
	    mode == DRV_HASH_SHA512) {
		set_bytes_swap(desc, 1);
	} else {
		set_cipher_config0(desc, HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	}
}

static int cc_map_result(struct device *dev, struct ahash_req_ctx *state,
			 unsigned int digestsize)
{
	state->digest_result_dma_addr =
		dma_map_single(dev, state->digest_result_buff,
			       digestsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, state->digest_result_dma_addr)) {
		dev_err(dev, "Mapping digest result buffer %u B for DMA failed\n",
			digestsize);
		return -ENOMEM;
	}
	dev_dbg(dev, "Mapped digest result buffer %u B at va=%pK to dma=%pad\n",
		digestsize, state->digest_result_buff,
		&state->digest_result_dma_addr);

	return 0;
}

static void cc_init_req(struct device *dev, struct ahash_req_ctx *state,
			struct cc_hash_ctx *ctx)
{
	bool is_hmac = ctx->is_hmac;

	memset(state, 0, sizeof(*state));

	if (is_hmac) {
		if (ctx->hw_mode != DRV_CIPHER_XCBC_MAC &&
		    ctx->hw_mode != DRV_CIPHER_CMAC) {
			dma_sync_single_for_cpu(dev, ctx->digest_buff_dma_addr,
						ctx->inter_digestsize,
						DMA_BIDIRECTIONAL);

			memcpy(state->digest_buff, ctx->digest_buff,
			       ctx->inter_digestsize);
			if (ctx->hash_mode == DRV_HASH_SHA512 ||
			    ctx->hash_mode == DRV_HASH_SHA384)
				memcpy(state->digest_bytes_len,
				       cc_digest_len_sha512_init,
				       ctx->hash_len);
			else
				memcpy(state->digest_bytes_len,
				       cc_digest_len_init,
				       ctx->hash_len);
		}

		if (ctx->hash_mode != DRV_HASH_NULL) {
			dma_sync_single_for_cpu(dev,
						ctx->opad_tmp_keys_dma_addr,
						ctx->inter_digestsize,
						DMA_BIDIRECTIONAL);
			memcpy(state->opad_digest_buff,
			       ctx->opad_tmp_keys_buff, ctx->inter_digestsize);
		}
	} else { /*hash*/
		/* Copy the initial digests if hash flow. */
		const void *larval = cc_larval_digest(dev, ctx->hash_mode);

		memcpy(state->digest_buff, larval, ctx->inter_digestsize);
	}
}

static int cc_map_req(struct device *dev, struct ahash_req_ctx *state,
		      struct cc_hash_ctx *ctx)
{
	bool is_hmac = ctx->is_hmac;

	state->digest_buff_dma_addr =
		dma_map_single(dev, state->digest_buff,
			       ctx->inter_digestsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, state->digest_buff_dma_addr)) {
		dev_err(dev, "Mapping digest len %d B at va=%pK for DMA failed\n",
			ctx->inter_digestsize, state->digest_buff);
		return -EINVAL;
	}
	dev_dbg(dev, "Mapped digest %d B at va=%pK to dma=%pad\n",
		ctx->inter_digestsize, state->digest_buff,
		&state->digest_buff_dma_addr);

	if (ctx->hw_mode != DRV_CIPHER_XCBC_MAC) {
		state->digest_bytes_len_dma_addr =
			dma_map_single(dev, state->digest_bytes_len,
				       HASH_MAX_LEN_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, state->digest_bytes_len_dma_addr)) {
			dev_err(dev, "Mapping digest len %u B at va=%pK for DMA failed\n",
				HASH_MAX_LEN_SIZE, state->digest_bytes_len);
			goto unmap_digest_buf;
		}
		dev_dbg(dev, "Mapped digest len %u B at va=%pK to dma=%pad\n",
			HASH_MAX_LEN_SIZE, state->digest_bytes_len,
			&state->digest_bytes_len_dma_addr);
	}

	if (is_hmac && ctx->hash_mode != DRV_HASH_NULL) {
		state->opad_digest_dma_addr =
			dma_map_single(dev, state->opad_digest_buff,
				       ctx->inter_digestsize,
				       DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, state->opad_digest_dma_addr)) {
			dev_err(dev, "Mapping opad digest %d B at va=%pK for DMA failed\n",
				ctx->inter_digestsize,
				state->opad_digest_buff);
			goto unmap_digest_len;
		}
		dev_dbg(dev, "Mapped opad digest %d B at va=%pK to dma=%pad\n",
			ctx->inter_digestsize, state->opad_digest_buff,
			&state->opad_digest_dma_addr);
	}

	return 0;

unmap_digest_len:
	if (state->digest_bytes_len_dma_addr) {
		dma_unmap_single(dev, state->digest_bytes_len_dma_addr,
				 HASH_MAX_LEN_SIZE, DMA_BIDIRECTIONAL);
		state->digest_bytes_len_dma_addr = 0;
	}
unmap_digest_buf:
	if (state->digest_buff_dma_addr) {
		dma_unmap_single(dev, state->digest_buff_dma_addr,
				 ctx->inter_digestsize, DMA_BIDIRECTIONAL);
		state->digest_buff_dma_addr = 0;
	}

	return -EINVAL;
}

static void cc_unmap_req(struct device *dev, struct ahash_req_ctx *state,
			 struct cc_hash_ctx *ctx)
{
	if (state->digest_buff_dma_addr) {
		dma_unmap_single(dev, state->digest_buff_dma_addr,
				 ctx->inter_digestsize, DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped digest-buffer: digest_buff_dma_addr=%pad\n",
			&state->digest_buff_dma_addr);
		state->digest_buff_dma_addr = 0;
	}
	if (state->digest_bytes_len_dma_addr) {
		dma_unmap_single(dev, state->digest_bytes_len_dma_addr,
				 HASH_MAX_LEN_SIZE, DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped digest-bytes-len buffer: digest_bytes_len_dma_addr=%pad\n",
			&state->digest_bytes_len_dma_addr);
		state->digest_bytes_len_dma_addr = 0;
	}
	if (state->opad_digest_dma_addr) {
		dma_unmap_single(dev, state->opad_digest_dma_addr,
				 ctx->inter_digestsize, DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped opad-digest: opad_digest_dma_addr=%pad\n",
			&state->opad_digest_dma_addr);
		state->opad_digest_dma_addr = 0;
	}
}

static void cc_unmap_result(struct device *dev, struct ahash_req_ctx *state,
			    unsigned int digestsize, u8 *result)
{
	if (state->digest_result_dma_addr) {
		dma_unmap_single(dev, state->digest_result_dma_addr, digestsize,
				 DMA_BIDIRECTIONAL);
		dev_dbg(dev, "unmpa digest result buffer va (%pK) pa (%pad) len %u\n",
			state->digest_result_buff,
			&state->digest_result_dma_addr, digestsize);
		memcpy(result, state->digest_result_buff, digestsize);
	}
	state->digest_result_dma_addr = 0;
}

static void cc_update_complete(struct device *dev, void *cc_req, int err)
{
	struct ahash_request *req = (struct ahash_request *)cc_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	dev_dbg(dev, "req=%pK\n", req);

	if (err != -EINPROGRESS) {
		/* Not a BACKLOG notification */
		cc_unmap_hash_request(dev, state, req->src, false);
		cc_unmap_req(dev, state, ctx);
	}

	ahash_request_complete(req, err);
}

static void cc_digest_complete(struct device *dev, void *cc_req, int err)
{
	struct ahash_request *req = (struct ahash_request *)cc_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	dev_dbg(dev, "req=%pK\n", req);

	if (err != -EINPROGRESS) {
		/* Not a BACKLOG notification */
		cc_unmap_hash_request(dev, state, req->src, false);
		cc_unmap_result(dev, state, digestsize, req->result);
		cc_unmap_req(dev, state, ctx);
	}

	ahash_request_complete(req, err);
}

static void cc_hash_complete(struct device *dev, void *cc_req, int err)
{
	struct ahash_request *req = (struct ahash_request *)cc_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	dev_dbg(dev, "req=%pK\n", req);

	if (err != -EINPROGRESS) {
		/* Not a BACKLOG notification */
		cc_unmap_hash_request(dev, state, req->src, false);
		cc_unmap_result(dev, state, digestsize, req->result);
		cc_unmap_req(dev, state, ctx);
	}

	ahash_request_complete(req, err);
}

static int cc_fin_result(struct cc_hw_desc *desc, struct ahash_request *req,
			 int idx)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr, digestsize,
		      NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	cc_set_endianity(ctx->hash_mode, &desc[idx]);
	idx++;

	return idx;
}

static int cc_fin_hmac(struct cc_hw_desc *desc, struct ahash_request *req,
		       int idx)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	/* store the hash digest result in the context */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_buff_dma_addr, digestsize,
		      NS_BIT, 0);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	cc_set_endianity(ctx->hash_mode, &desc[idx]);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* Loading hash opad xor key state */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->opad_digest_dma_addr,
		     ctx->inter_digestsize, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load the hash current length */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_sram(&desc[idx],
		     cc_digest_len_addr(ctx->drvdata, ctx->hash_mode),
		     ctx->hash_len);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Memory Barrier: wait for IPAD/OPAD axi write to complete */
	hw_desc_init(&desc[idx]);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	idx++;

	/* Perform HASH update */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     digestsize, NS_BIT);
	set_flow_mode(&desc[idx], DIN_HASH);
	idx++;

	return idx;
}

static int cc_hash_digest(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);
	struct scatterlist *src = req->src;
	unsigned int nbytes = req->nbytes;
	u8 *result = req->result;
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	bool is_hmac = ctx->is_hmac;
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	u32 larval_digest_addr;
	int idx = 0;
	int rc = 0;
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "===== %s-digest (%d) ====\n", is_hmac ? "hmac" : "hash",
		nbytes);

	cc_init_req(dev, state, ctx);

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -ENOMEM;
	}

	if (cc_map_result(dev, state, digestsize)) {
		dev_err(dev, "map_ahash_digest() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	if (cc_map_hash_request_final(ctx->drvdata, state, src, nbytes, 1,
				      flags)) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		cc_unmap_result(dev, state, digestsize, result);
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_digest_complete;
	cc_req.user_arg = req;

	/* If HMAC then load hash IPAD xor key, if HASH then load initial
	 * digest
	 */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	if (is_hmac) {
		set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
			     ctx->inter_digestsize, NS_BIT);
	} else {
		larval_digest_addr = cc_larval_digest_addr(ctx->drvdata,
							   ctx->hash_mode);
		set_din_sram(&desc[idx], larval_digest_addr,
			     ctx->inter_digestsize);
	}
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load the hash current length */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);

	if (is_hmac) {
		set_din_type(&desc[idx], DMA_DLLI,
			     state->digest_bytes_len_dma_addr,
			     ctx->hash_len, NS_BIT);
	} else {
		set_din_const(&desc[idx], 0, ctx->hash_len);
		if (nbytes)
			set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
		else
			set_cipher_do(&desc[idx], DO_PAD);
	}
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	cc_set_desc(state, ctx, DIN_HASH, desc, false, &idx);

	if (is_hmac) {
		/* HW last hash block padding (aka. "DO_PAD") */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      ctx->hash_len, NS_BIT, 0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
		set_cipher_do(&desc[idx], DO_PAD);
		idx++;

		idx = cc_fin_hmac(desc, req, idx);
	}

	idx = cc_fin_result(desc, req, idx);

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, src, true);
		cc_unmap_result(dev, state, digestsize, result);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_restore_hash(struct cc_hw_desc *desc, struct cc_hash_ctx *ctx,
			   struct ahash_req_ctx *state, unsigned int idx)
{
	/* Restore hash digest */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     ctx->inter_digestsize, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Restore hash current length */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_bytes_len_dma_addr,
		     ctx->hash_len, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	cc_set_desc(state, ctx, DIN_HASH, desc, false, &idx);

	return idx;
}

static int cc_hash_update(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int block_size = crypto_tfm_alg_blocksize(&tfm->base);
	struct scatterlist *src = req->src;
	unsigned int nbytes = req->nbytes;
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	u32 idx = 0;
	int rc;
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "===== %s-update (%d) ====\n", ctx->is_hmac ?
		"hmac" : "hash", nbytes);

	if (nbytes == 0) {
		/* no real updates required */
		return 0;
	}

	rc = cc_map_hash_request_update(ctx->drvdata, state, src, nbytes,
					block_size, flags);
	if (rc) {
		if (rc == 1) {
			dev_dbg(dev, " data size not require HW update %x\n",
				nbytes);
			/* No hardware updates are required */
			return 0;
		}
		dev_err(dev, "map_ahash_request_update() failed\n");
		return -ENOMEM;
	}

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		cc_unmap_hash_request(dev, state, src, true);
		return -EINVAL;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_update_complete;
	cc_req.user_arg = req;

	idx = cc_restore_hash(desc, ctx, state, idx);

	/* store the hash digest result in context */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
		      ctx->inter_digestsize, NS_BIT, 0);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* store current hash length in context */
	hw_desc_init(&desc[idx]);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	set_dout_dlli(&desc[idx], state->digest_bytes_len_dma_addr,
		      ctx->hash_len, NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
	idx++;

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, src, true);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_do_finup(struct ahash_request *req, bool update)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);
	struct scatterlist *src = req->src;
	unsigned int nbytes = req->nbytes;
	u8 *result = req->result;
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	bool is_hmac = ctx->is_hmac;
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	unsigned int idx = 0;
	int rc;
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "===== %s-%s (%d) ====\n", is_hmac ? "hmac" : "hash",
		update ? "finup" : "final", nbytes);

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -EINVAL;
	}

	if (cc_map_hash_request_final(ctx->drvdata, state, src, nbytes, update,
				      flags)) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}
	if (cc_map_result(dev, state, digestsize)) {
		dev_err(dev, "map_ahash_digest() failed\n");
		cc_unmap_hash_request(dev, state, src, true);
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_hash_complete;
	cc_req.user_arg = req;

	idx = cc_restore_hash(desc, ctx, state, idx);

	/* Pad the hash */
	hw_desc_init(&desc[idx]);
	set_cipher_do(&desc[idx], DO_PAD);
	set_hash_cipher_mode(&desc[idx], ctx->hw_mode, ctx->hash_mode);
	set_dout_dlli(&desc[idx], state->digest_bytes_len_dma_addr,
		      ctx->hash_len, NS_BIT, 0);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	idx++;

	if (is_hmac)
		idx = cc_fin_hmac(desc, req, idx);

	idx = cc_fin_result(desc, req, idx);

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, src, true);
		cc_unmap_result(dev, state, digestsize, result);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_hash_finup(struct ahash_request *req)
{
	return cc_do_finup(req, true);
}


static int cc_hash_final(struct ahash_request *req)
{
	return cc_do_finup(req, false);
}

static int cc_hash_init(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "===== init (%d) ====\n", req->nbytes);

	cc_init_req(dev, state, ctx);

	return 0;
}

static int cc_hash_setkey(struct crypto_ahash *ahash, const u8 *key,
			  unsigned int keylen)
{
	unsigned int hmac_pad_const[2] = { HMAC_IPAD_CONST, HMAC_OPAD_CONST };
	struct cc_crypto_req cc_req = {};
	struct cc_hash_ctx *ctx = NULL;
	int blocksize = 0;
	int digestsize = 0;
	int i, idx = 0, rc = 0;
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	u32 larval_addr;
	struct device *dev;

	ctx = crypto_ahash_ctx(ahash);
	dev = drvdata_to_dev(ctx->drvdata);
	dev_dbg(dev, "start keylen: %d", keylen);

	blocksize = crypto_tfm_alg_blocksize(&ahash->base);
	digestsize = crypto_ahash_digestsize(ahash);

	larval_addr = cc_larval_digest_addr(ctx->drvdata, ctx->hash_mode);

	/* The keylen value distinguishes HASH in case keylen is ZERO bytes,
	 * any NON-ZERO value utilizes HMAC flow
	 */
	ctx->key_params.keylen = keylen;
	ctx->key_params.key_dma_addr = 0;
	ctx->is_hmac = true;
	ctx->key_params.key = NULL;

	if (keylen) {
		ctx->key_params.key = kmemdup(key, keylen, GFP_KERNEL);
		if (!ctx->key_params.key)
			return -ENOMEM;

		ctx->key_params.key_dma_addr =
			dma_map_single(dev, ctx->key_params.key, keylen,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(dev, ctx->key_params.key_dma_addr)) {
			dev_err(dev, "Mapping key va=0x%p len=%u for DMA failed\n",
				ctx->key_params.key, keylen);
			kzfree(ctx->key_params.key);
			return -ENOMEM;
		}
		dev_dbg(dev, "mapping key-buffer: key_dma_addr=%pad keylen=%u\n",
			&ctx->key_params.key_dma_addr, ctx->key_params.keylen);

		if (keylen > blocksize) {
			/* Load hash initial state */
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], ctx->hw_mode);
			set_din_sram(&desc[idx], larval_addr,
				     ctx->inter_digestsize);
			set_flow_mode(&desc[idx], S_DIN_to_HASH);
			set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
			idx++;

			/* Load the hash current length*/
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], ctx->hw_mode);
			set_din_const(&desc[idx], 0, ctx->hash_len);
			set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
			set_flow_mode(&desc[idx], S_DIN_to_HASH);
			set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
			idx++;

			hw_desc_init(&desc[idx]);
			set_din_type(&desc[idx], DMA_DLLI,
				     ctx->key_params.key_dma_addr, keylen,
				     NS_BIT);
			set_flow_mode(&desc[idx], DIN_HASH);
			idx++;

			/* Get hashed key */
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], ctx->hw_mode);
			set_dout_dlli(&desc[idx], ctx->opad_tmp_keys_dma_addr,
				      digestsize, NS_BIT, 0);
			set_flow_mode(&desc[idx], S_HASH_to_DOUT);
			set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
			set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
			cc_set_endianity(ctx->hash_mode, &desc[idx]);
			idx++;

			hw_desc_init(&desc[idx]);
			set_din_const(&desc[idx], 0, (blocksize - digestsize));
			set_flow_mode(&desc[idx], BYPASS);
			set_dout_dlli(&desc[idx],
				      (ctx->opad_tmp_keys_dma_addr +
				       digestsize),
				      (blocksize - digestsize), NS_BIT, 0);
			idx++;
		} else {
			hw_desc_init(&desc[idx]);
			set_din_type(&desc[idx], DMA_DLLI,
				     ctx->key_params.key_dma_addr, keylen,
				     NS_BIT);
			set_flow_mode(&desc[idx], BYPASS);
			set_dout_dlli(&desc[idx], ctx->opad_tmp_keys_dma_addr,
				      keylen, NS_BIT, 0);
			idx++;

			if ((blocksize - keylen)) {
				hw_desc_init(&desc[idx]);
				set_din_const(&desc[idx], 0,
					      (blocksize - keylen));
				set_flow_mode(&desc[idx], BYPASS);
				set_dout_dlli(&desc[idx],
					      (ctx->opad_tmp_keys_dma_addr +
					       keylen), (blocksize - keylen),
					      NS_BIT, 0);
				idx++;
			}
		}
	} else {
		hw_desc_init(&desc[idx]);
		set_din_const(&desc[idx], 0, blocksize);
		set_flow_mode(&desc[idx], BYPASS);
		set_dout_dlli(&desc[idx], (ctx->opad_tmp_keys_dma_addr),
			      blocksize, NS_BIT, 0);
		idx++;
	}

	rc = cc_send_sync_request(ctx->drvdata, &cc_req, desc, idx);
	if (rc) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		goto out;
	}

	/* calc derived HMAC key */
	for (idx = 0, i = 0; i < 2; i++) {
		/* Load hash initial state */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_din_sram(&desc[idx], larval_addr, ctx->inter_digestsize);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
		idx++;

		/* Load the hash current length*/
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_din_const(&desc[idx], 0, ctx->hash_len);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Prepare ipad key */
		hw_desc_init(&desc[idx]);
		set_xor_val(&desc[idx], hmac_pad_const[i]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
		idx++;

		/* Perform HASH update */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI, ctx->opad_tmp_keys_dma_addr,
			     blocksize, NS_BIT);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_xor_active(&desc[idx]);
		set_flow_mode(&desc[idx], DIN_HASH);
		idx++;

		/* Get the IPAD/OPAD xor key (Note, IPAD is the initial digest
		 * of the first HASH "update" state)
		 */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		if (i > 0) /* Not first iteration */
			set_dout_dlli(&desc[idx], ctx->opad_tmp_keys_dma_addr,
				      ctx->inter_digestsize, NS_BIT, 0);
		else /* First iteration */
			set_dout_dlli(&desc[idx], ctx->digest_buff_dma_addr,
				      ctx->inter_digestsize, NS_BIT, 0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		idx++;
	}

	rc = cc_send_sync_request(ctx->drvdata, &cc_req, desc, idx);

out:
	if (ctx->key_params.key_dma_addr) {
		dma_unmap_single(dev, ctx->key_params.key_dma_addr,
				 ctx->key_params.keylen, DMA_TO_DEVICE);
		dev_dbg(dev, "Unmapped key-buffer: key_dma_addr=%pad keylen=%u\n",
			&ctx->key_params.key_dma_addr, ctx->key_params.keylen);
	}

	kzfree(ctx->key_params.key);

	return rc;
}

static int cc_xcbc_setkey(struct crypto_ahash *ahash,
			  const u8 *key, unsigned int keylen)
{
	struct cc_crypto_req cc_req = {};
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	int rc = 0;
	unsigned int idx = 0;
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];

	dev_dbg(dev, "===== setkey (%d) ====\n", keylen);

	switch (keylen) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		break;
	default:
		return -EINVAL;
	}

	ctx->key_params.keylen = keylen;

	ctx->key_params.key = kmemdup(key, keylen, GFP_KERNEL);
	if (!ctx->key_params.key)
		return -ENOMEM;

	ctx->key_params.key_dma_addr =
		dma_map_single(dev, ctx->key_params.key, keylen, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, ctx->key_params.key_dma_addr)) {
		dev_err(dev, "Mapping key va=0x%p len=%u for DMA failed\n",
			key, keylen);
		kzfree(ctx->key_params.key);
		return -ENOMEM;
	}
	dev_dbg(dev, "mapping key-buffer: key_dma_addr=%pad keylen=%u\n",
		&ctx->key_params.key_dma_addr, ctx->key_params.keylen);

	ctx->is_hmac = true;
	/* 1. Load the AES key */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, ctx->key_params.key_dma_addr,
		     keylen, NS_BIT);
	set_cipher_mode(&desc[idx], DRV_CIPHER_ECB);
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_key_size_aes(&desc[idx], keylen);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x01010101, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	set_dout_dlli(&desc[idx],
		      (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K1_OFFSET),
		      CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x02020202, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	set_dout_dlli(&desc[idx],
		      (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K2_OFFSET),
		      CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x03030303, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	set_dout_dlli(&desc[idx],
		      (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K3_OFFSET),
		      CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	rc = cc_send_sync_request(ctx->drvdata, &cc_req, desc, idx);

	dma_unmap_single(dev, ctx->key_params.key_dma_addr,
			 ctx->key_params.keylen, DMA_TO_DEVICE);
	dev_dbg(dev, "Unmapped key-buffer: key_dma_addr=%pad keylen=%u\n",
		&ctx->key_params.key_dma_addr, ctx->key_params.keylen);

	kzfree(ctx->key_params.key);

	return rc;
}

static int cc_cmac_setkey(struct crypto_ahash *ahash,
			  const u8 *key, unsigned int keylen)
{
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "===== setkey (%d) ====\n", keylen);

	ctx->is_hmac = true;

	switch (keylen) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		break;
	default:
		return -EINVAL;
	}

	ctx->key_params.keylen = keylen;

	/* STAT_PHASE_1: Copy key to ctx */

	dma_sync_single_for_cpu(dev, ctx->opad_tmp_keys_dma_addr,
				keylen, DMA_TO_DEVICE);

	memcpy(ctx->opad_tmp_keys_buff, key, keylen);
	if (keylen == 24) {
		memset(ctx->opad_tmp_keys_buff + 24, 0,
		       CC_AES_KEY_SIZE_MAX - 24);
	}

	dma_sync_single_for_device(dev, ctx->opad_tmp_keys_dma_addr,
				   keylen, DMA_TO_DEVICE);

	ctx->key_params.keylen = keylen;

	return 0;
}

static void cc_free_ctx(struct cc_hash_ctx *ctx)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	if (ctx->digest_buff_dma_addr) {
		dma_unmap_single(dev, ctx->digest_buff_dma_addr,
				 sizeof(ctx->digest_buff), DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped digest-buffer: digest_buff_dma_addr=%pad\n",
			&ctx->digest_buff_dma_addr);
		ctx->digest_buff_dma_addr = 0;
	}
	if (ctx->opad_tmp_keys_dma_addr) {
		dma_unmap_single(dev, ctx->opad_tmp_keys_dma_addr,
				 sizeof(ctx->opad_tmp_keys_buff),
				 DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped opad-digest: opad_tmp_keys_dma_addr=%pad\n",
			&ctx->opad_tmp_keys_dma_addr);
		ctx->opad_tmp_keys_dma_addr = 0;
	}

	ctx->key_params.keylen = 0;
}

static int cc_alloc_ctx(struct cc_hash_ctx *ctx)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	ctx->key_params.keylen = 0;

	ctx->digest_buff_dma_addr =
		dma_map_single(dev, ctx->digest_buff, sizeof(ctx->digest_buff),
			       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, ctx->digest_buff_dma_addr)) {
		dev_err(dev, "Mapping digest len %zu B at va=%pK for DMA failed\n",
			sizeof(ctx->digest_buff), ctx->digest_buff);
		goto fail;
	}
	dev_dbg(dev, "Mapped digest %zu B at va=%pK to dma=%pad\n",
		sizeof(ctx->digest_buff), ctx->digest_buff,
		&ctx->digest_buff_dma_addr);

	ctx->opad_tmp_keys_dma_addr =
		dma_map_single(dev, ctx->opad_tmp_keys_buff,
			       sizeof(ctx->opad_tmp_keys_buff),
			       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, ctx->opad_tmp_keys_dma_addr)) {
		dev_err(dev, "Mapping opad digest %zu B at va=%pK for DMA failed\n",
			sizeof(ctx->opad_tmp_keys_buff),
			ctx->opad_tmp_keys_buff);
		goto fail;
	}
	dev_dbg(dev, "Mapped opad_tmp_keys %zu B at va=%pK to dma=%pad\n",
		sizeof(ctx->opad_tmp_keys_buff), ctx->opad_tmp_keys_buff,
		&ctx->opad_tmp_keys_dma_addr);

	ctx->is_hmac = false;
	return 0;

fail:
	cc_free_ctx(ctx);
	return -ENOMEM;
}

static int cc_get_hash_len(struct crypto_tfm *tfm)
{
	struct cc_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->hash_mode == DRV_HASH_SM3)
		return CC_SM3_HASH_LEN_SIZE;
	else
		return cc_get_default_hash_len(ctx->drvdata);
}

static int cc_cra_init(struct crypto_tfm *tfm)
{
	struct cc_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct hash_alg_common *hash_alg_common =
		container_of(tfm->__crt_alg, struct hash_alg_common, base);
	struct ahash_alg *ahash_alg =
		container_of(hash_alg_common, struct ahash_alg, halg);
	struct cc_hash_alg *cc_alg =
			container_of(ahash_alg, struct cc_hash_alg, ahash_alg);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct ahash_req_ctx));

	ctx->hash_mode = cc_alg->hash_mode;
	ctx->hw_mode = cc_alg->hw_mode;
	ctx->inter_digestsize = cc_alg->inter_digestsize;
	ctx->drvdata = cc_alg->drvdata;
	ctx->hash_len = cc_get_hash_len(tfm);
	return cc_alloc_ctx(ctx);
}

static void cc_cra_exit(struct crypto_tfm *tfm)
{
	struct cc_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "cc_cra_exit");
	cc_free_ctx(ctx);
}

static int cc_mac_update(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	unsigned int block_size = crypto_tfm_alg_blocksize(&tfm->base);
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	int rc;
	u32 idx = 0;
	gfp_t flags = cc_gfp_flags(&req->base);

	if (req->nbytes == 0) {
		/* no real updates required */
		return 0;
	}

	state->xcbc_count++;

	rc = cc_map_hash_request_update(ctx->drvdata, state, req->src,
					req->nbytes, block_size, flags);
	if (rc) {
		if (rc == 1) {
			dev_dbg(dev, " data size not require HW update %x\n",
				req->nbytes);
			/* No hardware updates are required */
			return 0;
		}
		dev_err(dev, "map_ahash_request_update() failed\n");
		return -ENOMEM;
	}

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -EINVAL;
	}

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC)
		cc_setup_xcbc(req, desc, &idx);
	else
		cc_setup_cmac(req, desc, &idx);

	cc_set_desc(state, ctx, DIN_AES_DOUT, desc, true, &idx);

	/* store the hash digest result in context */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
		      ctx->inter_digestsize, NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* Setup request structure */
	cc_req.user_cb = cc_update_complete;
	cc_req.user_arg = req;

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_mac_final(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	int idx = 0;
	int rc = 0;
	u32 key_size, key_len;
	u32 digestsize = crypto_ahash_digestsize(tfm);
	gfp_t flags = cc_gfp_flags(&req->base);
	u32 rem_cnt = *cc_hash_buf_cnt(state);

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_size = CC_AES_128_BIT_KEY_SIZE;
		key_len  = CC_AES_128_BIT_KEY_SIZE;
	} else {
		key_size = (ctx->key_params.keylen == 24) ? AES_MAX_KEY_SIZE :
			ctx->key_params.keylen;
		key_len =  ctx->key_params.keylen;
	}

	dev_dbg(dev, "===== final  xcbc reminder (%d) ====\n", rem_cnt);

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -EINVAL;
	}

	if (cc_map_hash_request_final(ctx->drvdata, state, req->src,
				      req->nbytes, 0, flags)) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	if (cc_map_result(dev, state, digestsize)) {
		dev_err(dev, "map_ahash_digest() failed\n");
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_hash_complete;
	cc_req.user_arg = req;

	if (state->xcbc_count && rem_cnt == 0) {
		/* Load key for ECB decryption */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], DRV_CIPHER_ECB);
		set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_DECRYPT);
		set_din_type(&desc[idx], DMA_DLLI,
			     (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K1_OFFSET),
			     key_size, NS_BIT);
		set_key_size_aes(&desc[idx], key_len);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Initiate decryption of block state to previous
		 * block_state-XOR-M[n]
		 */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
			     CC_AES_BLOCK_SIZE, NS_BIT);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      CC_AES_BLOCK_SIZE, NS_BIT, 0);
		set_flow_mode(&desc[idx], DIN_AES_DOUT);
		idx++;

		/* Memory Barrier: wait for axi write to complete */
		hw_desc_init(&desc[idx]);
		set_din_no_dma(&desc[idx], 0, 0xfffff0);
		set_dout_no_dma(&desc[idx], 0, 0, 1);
		idx++;
	}

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC)
		cc_setup_xcbc(req, desc, &idx);
	else
		cc_setup_cmac(req, desc, &idx);

	if (state->xcbc_count == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else if (rem_cnt > 0) {
		cc_set_desc(state, ctx, DIN_AES_DOUT, desc, false, &idx);
	} else {
		hw_desc_init(&desc[idx]);
		set_din_const(&desc[idx], 0x00, CC_AES_BLOCK_SIZE);
		set_flow_mode(&desc[idx], DIN_AES_DOUT);
		idx++;
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr,
		      digestsize, NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_result(dev, state, digestsize, req->result);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_mac_finup(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	int idx = 0;
	int rc = 0;
	u32 key_len = 0;
	u32 digestsize = crypto_ahash_digestsize(tfm);
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "===== finup xcbc(%d) ====\n", req->nbytes);
	if (state->xcbc_count > 0 && req->nbytes == 0) {
		dev_dbg(dev, "No data to update. Call to fdx_mac_final\n");
		return cc_mac_final(req);
	}

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -EINVAL;
	}

	if (cc_map_hash_request_final(ctx->drvdata, state, req->src,
				      req->nbytes, 1, flags)) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}
	if (cc_map_result(dev, state, digestsize)) {
		dev_err(dev, "map_ahash_digest() failed\n");
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_hash_complete;
	cc_req.user_arg = req;

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_len = CC_AES_128_BIT_KEY_SIZE;
		cc_setup_xcbc(req, desc, &idx);
	} else {
		key_len = ctx->key_params.keylen;
		cc_setup_cmac(req, desc, &idx);
	}

	if (req->nbytes == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else {
		cc_set_desc(state, ctx, DIN_AES_DOUT, desc, false, &idx);
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr,
		      digestsize, NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_result(dev, state, digestsize, req->result);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_mac_digest(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	u32 digestsize = crypto_ahash_digestsize(tfm);
	struct cc_crypto_req cc_req = {};
	struct cc_hw_desc desc[CC_MAX_HASH_SEQ_LEN];
	u32 key_len;
	unsigned int idx = 0;
	int rc;
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "===== -digest mac (%d) ====\n",  req->nbytes);

	cc_init_req(dev, state, ctx);

	if (cc_map_req(dev, state, ctx)) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -ENOMEM;
	}
	if (cc_map_result(dev, state, digestsize)) {
		dev_err(dev, "map_ahash_digest() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	if (cc_map_hash_request_final(ctx->drvdata, state, req->src,
				      req->nbytes, 1, flags)) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		cc_unmap_req(dev, state, ctx);
		return -ENOMEM;
	}

	/* Setup request structure */
	cc_req.user_cb = cc_digest_complete;
	cc_req.user_arg = req;

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_len = CC_AES_128_BIT_KEY_SIZE;
		cc_setup_xcbc(req, desc, &idx);
	} else {
		key_len = ctx->key_params.keylen;
		cc_setup_cmac(req, desc, &idx);
	}

	if (req->nbytes == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else {
		cc_set_desc(state, ctx, DIN_AES_DOUT, desc, false, &idx);
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr,
		      CC_AES_BLOCK_SIZE, NS_BIT, 1);
	set_queue_last_ind(ctx->drvdata, &desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = cc_send_request(ctx->drvdata, &cc_req, desc, idx, &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		cc_unmap_result(dev, state, digestsize, req->result);
		cc_unmap_req(dev, state, ctx);
	}
	return rc;
}

static int cc_hash_export(struct ahash_request *req, void *out)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	u8 *curr_buff = cc_hash_buf(state);
	u32 curr_buff_cnt = *cc_hash_buf_cnt(state);
	const u32 tmp = CC_EXPORT_MAGIC;

	memcpy(out, &tmp, sizeof(u32));
	out += sizeof(u32);

	memcpy(out, state->digest_buff, ctx->inter_digestsize);
	out += ctx->inter_digestsize;

	memcpy(out, state->digest_bytes_len, ctx->hash_len);
	out += ctx->hash_len;

	memcpy(out, &curr_buff_cnt, sizeof(u32));
	out += sizeof(u32);

	memcpy(out, curr_buff, curr_buff_cnt);

	return 0;
}

static int cc_hash_import(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	u32 tmp;

	memcpy(&tmp, in, sizeof(u32));
	if (tmp != CC_EXPORT_MAGIC)
		return -EINVAL;
	in += sizeof(u32);

	cc_init_req(dev, state, ctx);

	memcpy(state->digest_buff, in, ctx->inter_digestsize);
	in += ctx->inter_digestsize;

	memcpy(state->digest_bytes_len, in, ctx->hash_len);
	in += ctx->hash_len;

	/* Sanity check the data as much as possible */
	memcpy(&tmp, in, sizeof(u32));
	if (tmp > CC_MAX_HASH_BLCK_SIZE)
		return -EINVAL;
	in += sizeof(u32);

	state->buf_cnt[0] = tmp;
	memcpy(state->buffers[0], in, tmp);

	return 0;
}

struct cc_hash_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	char mac_name[CRYPTO_MAX_ALG_NAME];
	char mac_driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	bool is_mac;
	bool synchronize;
	struct ahash_alg template_ahash;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	struct cc_drvdata *drvdata;
	u32 min_hw_rev;
	enum cc_std_body std_body;
};

#define CC_STATE_SIZE(_x) \
	((_x) + HASH_MAX_LEN_SIZE + CC_MAX_HASH_BLCK_SIZE + (2 * sizeof(u32)))

/* hash descriptors */
static struct cc_hash_template driver_hash[] = {
	//Asynchronize hash template
	{
		.name = "sha1",
		.driver_name = "sha1-ccree",
		.mac_name = "hmac(sha1)",
		.mac_driver_name = "hmac-sha1-ccree",
		.blocksize = SHA1_BLOCK_SIZE,
		.is_mac = true,
		.synchronize = false,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA1_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA1,
		.hw_mode = DRV_HASH_HW_SHA1,
		.inter_digestsize = SHA1_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "sha256",
		.driver_name = "sha256-ccree",
		.mac_name = "hmac(sha256)",
		.mac_driver_name = "hmac-sha256-ccree",
		.blocksize = SHA256_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA256_DIGEST_SIZE)
			},
		},
		.hash_mode = DRV_HASH_SHA256,
		.hw_mode = DRV_HASH_HW_SHA256,
		.inter_digestsize = SHA256_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "sha224",
		.driver_name = "sha224-ccree",
		.mac_name = "hmac(sha224)",
		.mac_driver_name = "hmac-sha224-ccree",
		.blocksize = SHA224_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA256_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA224,
		.hw_mode = DRV_HASH_HW_SHA256,
		.inter_digestsize = SHA256_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "sha384",
		.driver_name = "sha384-ccree",
		.mac_name = "hmac(sha384)",
		.mac_driver_name = "hmac-sha384-ccree",
		.blocksize = SHA384_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA512_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA384,
		.hw_mode = DRV_HASH_HW_SHA512,
		.inter_digestsize = SHA512_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_712,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "sha512",
		.driver_name = "sha512-ccree",
		.mac_name = "hmac(sha512)",
		.mac_driver_name = "hmac-sha512-ccree",
		.blocksize = SHA512_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA512_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA512,
		.hw_mode = DRV_HASH_HW_SHA512,
		.inter_digestsize = SHA512_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_712,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "md5",
		.driver_name = "md5-ccree",
		.mac_name = "hmac(md5)",
		.mac_driver_name = "hmac-md5-ccree",
		.blocksize = MD5_HMAC_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = MD5_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(MD5_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_MD5,
		.hw_mode = DRV_HASH_HW_MD5,
		.inter_digestsize = MD5_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
	{
		.name = "sm3",
		.driver_name = "sm3-ccree",
		.blocksize = SM3_BLOCK_SIZE,
		.is_mac = false,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_hash_update,
			.final = cc_hash_final,
			.finup = cc_hash_finup,
			.digest = cc_hash_digest,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.setkey = cc_hash_setkey,
			.halg = {
				.digestsize = SM3_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SM3_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SM3,
		.hw_mode = DRV_HASH_HW_SM3,
		.inter_digestsize = SM3_DIGEST_SIZE,
		.min_hw_rev = CC_HW_REV_713,
		.std_body = CC_STD_OSCCA,
	},
	{
		.mac_name = "xcbc(aes)",
		.mac_driver_name = "xcbc-aes-ccree",
		.blocksize = AES_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_mac_update,
			.final = cc_mac_final,
			.finup = cc_mac_finup,
			.digest = cc_mac_digest,
			.setkey = cc_xcbc_setkey,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = CC_STATE_SIZE(AES_BLOCK_SIZE),
			},
		},
		.hash_mode = DRV_HASH_NULL,
		.hw_mode = DRV_CIPHER_XCBC_MAC,
		.inter_digestsize = AES_BLOCK_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
	{
		.mac_name = "cmac(aes)",
		.mac_driver_name = "cmac-aes-ccree",
		.blocksize = AES_BLOCK_SIZE,
		.is_mac = true,
		.template_ahash = {
			.init = cc_hash_init,
			.update = cc_mac_update,
			.final = cc_mac_final,
			.finup = cc_mac_finup,
			.digest = cc_mac_digest,
			.setkey = cc_cmac_setkey,
			.export = cc_hash_export,
			.import = cc_hash_import,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = CC_STATE_SIZE(AES_BLOCK_SIZE),
			},
		},
		.hash_mode = DRV_HASH_NULL,
		.hw_mode = DRV_CIPHER_CMAC,
		.inter_digestsize = AES_BLOCK_SIZE,
		.min_hw_rev = CC_HW_REV_630,
		.std_body = CC_STD_NIST,
	},
};

static struct cc_hash_alg *cc_alloc_hash_alg(struct cc_hash_template *template,
					     struct device *dev, bool keyed)
{
	struct cc_hash_alg *t_crypto_alg;
	struct crypto_alg *alg;
	struct ahash_alg *halg;

	t_crypto_alg = devm_kzalloc(dev, sizeof(*t_crypto_alg), GFP_KERNEL);
	if (!t_crypto_alg)
		return ERR_PTR(-ENOMEM);

	t_crypto_alg->ahash_alg = template->template_ahash;
	halg = &t_crypto_alg->ahash_alg;
	alg = &halg->halg.base;

	if (keyed) {
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->mac_name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->mac_driver_name);
	} else {
		halg->setkey = NULL;
		snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->name);
		snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
			 template->driver_name);
	}
	alg->cra_module = THIS_MODULE;
	alg->cra_ctxsize = sizeof(struct cc_hash_ctx);
	alg->cra_priority = CC_CRA_PRIO;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_exit = cc_cra_exit;

	alg->cra_init = cc_cra_init;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY;

	t_crypto_alg->hash_mode = template->hash_mode;
	t_crypto_alg->hw_mode = template->hw_mode;
	t_crypto_alg->inter_digestsize = template->inter_digestsize;

	return t_crypto_alg;
}

static int cc_init_copy_sram(struct cc_drvdata *drvdata, const u32 *data,
			     unsigned int size, u32 *sram_buff_ofs)
{
	struct cc_hw_desc larval_seq[CC_DIGEST_SIZE_MAX / sizeof(u32)];
	unsigned int larval_seq_len = 0;
	int rc;

	cc_set_sram_desc(data, *sram_buff_ofs, size / sizeof(*data),
			 larval_seq, &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (rc)
		return rc;

	*sram_buff_ofs += size;
	return 0;
}

int cc_init_hash_sram(struct cc_drvdata *drvdata)
{
	struct cc_hash_handle *hash_handle = drvdata->hash_handle;
	u32 sram_buff_ofs = hash_handle->digest_len_sram_addr;
	bool large_sha_supported = (drvdata->hw_rev >= CC_HW_REV_712);
	bool sm3_supported = (drvdata->hw_rev >= CC_HW_REV_713);
	int rc = 0;

	/* Copy-to-sram digest-len */
	rc = cc_init_copy_sram(drvdata, cc_digest_len_init,
			       sizeof(cc_digest_len_init), &sram_buff_ofs);
	if (rc)
		goto init_digest_const_err;

	if (large_sha_supported) {
		/* Copy-to-sram digest-len for sha384/512 */
		rc = cc_init_copy_sram(drvdata, cc_digest_len_sha512_init,
				       sizeof(cc_digest_len_sha512_init),
				       &sram_buff_ofs);
		if (rc)
			goto init_digest_const_err;
	}

	/* The initial digests offset */
	hash_handle->larval_digest_sram_addr = sram_buff_ofs;

	/* Copy-to-sram initial SHA* digests */
	rc = cc_init_copy_sram(drvdata, cc_md5_init, sizeof(cc_md5_init),
			       &sram_buff_ofs);
	if (rc)
		goto init_digest_const_err;

	rc = cc_init_copy_sram(drvdata, cc_sha1_init, sizeof(cc_sha1_init),
			       &sram_buff_ofs);
	if (rc)
		goto init_digest_const_err;

	rc = cc_init_copy_sram(drvdata, cc_sha224_init, sizeof(cc_sha224_init),
			       &sram_buff_ofs);
	if (rc)
		goto init_digest_const_err;

	rc = cc_init_copy_sram(drvdata, cc_sha256_init, sizeof(cc_sha256_init),
			       &sram_buff_ofs);
	if (rc)
		goto init_digest_const_err;

	if (sm3_supported) {
		rc = cc_init_copy_sram(drvdata, cc_sm3_init,
				       sizeof(cc_sm3_init), &sram_buff_ofs);
		if (rc)
			goto init_digest_const_err;
	}

	if (large_sha_supported) {
		rc = cc_init_copy_sram(drvdata, cc_sha384_init,
				       sizeof(cc_sha384_init), &sram_buff_ofs);
		if (rc)
			goto init_digest_const_err;

		rc = cc_init_copy_sram(drvdata, cc_sha512_init,
				       sizeof(cc_sha512_init), &sram_buff_ofs);
		if (rc)
			goto init_digest_const_err;
	}

init_digest_const_err:
	return rc;
}

int cc_hash_alloc(struct cc_drvdata *drvdata)
{
	struct cc_hash_handle *hash_handle;
	u32 sram_buff;
	u32 sram_size_to_alloc;
	struct device *dev = drvdata_to_dev(drvdata);
	int rc = 0;
	int alg;

	hash_handle = devm_kzalloc(dev, sizeof(*hash_handle), GFP_KERNEL);
	if (!hash_handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&hash_handle->hash_list);
	drvdata->hash_handle = hash_handle;

	sram_size_to_alloc = sizeof(cc_digest_len_init) +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init) +
			sizeof(cc_sha224_init) +
			sizeof(cc_sha256_init);

	if (drvdata->hw_rev >= CC_HW_REV_713)
		sram_size_to_alloc += sizeof(cc_sm3_init);

	if (drvdata->hw_rev >= CC_HW_REV_712)
		sram_size_to_alloc += sizeof(cc_digest_len_sha512_init) +
			sizeof(cc_sha384_init) + sizeof(cc_sha512_init);

	sram_buff = cc_sram_alloc(drvdata, sram_size_to_alloc);
	if (sram_buff == NULL_SRAM_ADDR) {
		rc = -ENOMEM;
		goto fail;
	}

	/* The initial digest-len offset */
	hash_handle->digest_len_sram_addr = sram_buff;

	/*must be set before the alg registration as it is being used there*/
	rc = cc_init_hash_sram(drvdata);
	if (rc) {
		dev_err(dev, "Init digest CONST failed (rc=%d)\n", rc);
		goto fail;
	}

	/* ahash registration */
	for (alg = 0; alg < ARRAY_SIZE(driver_hash); alg++) {
		struct cc_hash_alg *t_alg;
		int hw_mode = driver_hash[alg].hw_mode;

		/* Check that the HW revision and variants are suitable */
		if ((driver_hash[alg].min_hw_rev > drvdata->hw_rev) ||
		    !(drvdata->std_bodies & driver_hash[alg].std_body))
			continue;

		if (driver_hash[alg].is_mac) {
			/* register hmac version */
			t_alg = cc_alloc_hash_alg(&driver_hash[alg], dev, true);
			if (IS_ERR(t_alg)) {
				rc = PTR_ERR(t_alg);
				dev_err(dev, "%s alg allocation failed\n",
					driver_hash[alg].driver_name);
				goto fail;
			}
			t_alg->drvdata = drvdata;

			rc = crypto_register_ahash(&t_alg->ahash_alg);
			if (rc) {
				dev_err(dev, "%s alg registration failed\n",
					driver_hash[alg].driver_name);
				goto fail;
			}

			list_add_tail(&t_alg->entry, &hash_handle->hash_list);
		}
		if (hw_mode == DRV_CIPHER_XCBC_MAC ||
		    hw_mode == DRV_CIPHER_CMAC)
			continue;

		/* register hash version */
		t_alg = cc_alloc_hash_alg(&driver_hash[alg], dev, false);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			dev_err(dev, "%s alg allocation failed\n",
				driver_hash[alg].driver_name);
			goto fail;
		}
		t_alg->drvdata = drvdata;

		rc = crypto_register_ahash(&t_alg->ahash_alg);
		if (rc) {
			dev_err(dev, "%s alg registration failed\n",
				driver_hash[alg].driver_name);
			goto fail;
		}

		list_add_tail(&t_alg->entry, &hash_handle->hash_list);
	}

	return 0;

fail:
	cc_hash_free(drvdata);
	return rc;
}

int cc_hash_free(struct cc_drvdata *drvdata)
{
	struct cc_hash_alg *t_hash_alg, *hash_n;
	struct cc_hash_handle *hash_handle = drvdata->hash_handle;

	list_for_each_entry_safe(t_hash_alg, hash_n, &hash_handle->hash_list,
				 entry) {
		crypto_unregister_ahash(&t_hash_alg->ahash_alg);
		list_del(&t_hash_alg->entry);
	}

	return 0;
}

static void cc_setup_xcbc(struct ahash_request *areq, struct cc_hw_desc desc[],
			  unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct ahash_req_ctx *state = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	/* Setup XCBC MAC K1 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, (ctx->opad_tmp_keys_dma_addr +
					    XCBC_MAC_K1_OFFSET),
		     CC_AES_128_BIT_KEY_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_hash_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC, ctx->hash_mode);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Setup XCBC MAC K2 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI,
		     (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K2_OFFSET),
		     CC_AES_128_BIT_KEY_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Setup XCBC MAC K3 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI,
		     (ctx->opad_tmp_keys_dma_addr + XCBC_MAC_K3_OFFSET),
		     CC_AES_128_BIT_KEY_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE2);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Loading MAC state */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     CC_AES_BLOCK_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;
	*seq_size = idx;
}

static void cc_setup_cmac(struct ahash_request *areq, struct cc_hw_desc desc[],
			  unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct ahash_req_ctx *state = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct cc_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	/* Setup CMAC Key */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, ctx->opad_tmp_keys_dma_addr,
		     ((ctx->key_params.keylen == 24) ? AES_MAX_KEY_SIZE :
		      ctx->key_params.keylen), NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CMAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], ctx->key_params.keylen);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Load MAC state */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     CC_AES_BLOCK_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CMAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], ctx->key_params.keylen);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;
	*seq_size = idx;
}

static void cc_set_desc(struct ahash_req_ctx *areq_ctx,
			struct cc_hash_ctx *ctx, unsigned int flow_mode,
			struct cc_hw_desc desc[], bool is_not_last_data,
			unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	if (areq_ctx->data_dma_buf_type == CC_DMA_BUF_DLLI) {
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     sg_dma_address(areq_ctx->curr_sg),
			     areq_ctx->curr_sg->length, NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		idx++;
	} else {
		if (areq_ctx->data_dma_buf_type == CC_DMA_BUF_NULL) {
			dev_dbg(dev, " NULL mode\n");
			/* nothing to build */
			return;
		}
		/* bypass */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     areq_ctx->mlli_params.mlli_dma_addr,
			     areq_ctx->mlli_params.mlli_len, NS_BIT);
		set_dout_sram(&desc[idx], ctx->drvdata->mlli_sram_addr,
			      areq_ctx->mlli_params.mlli_len);
		set_flow_mode(&desc[idx], BYPASS);
		idx++;
		/* process */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_MLLI,
			     ctx->drvdata->mlli_sram_addr,
			     areq_ctx->mlli_nents, NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		idx++;
	}
	if (is_not_last_data)
		set_din_not_last_indication(&desc[(idx - 1)]);
	/* return updated desc sequence size */
	*seq_size = idx;
}

static const void *cc_larval_digest(struct device *dev, u32 mode)
{
	switch (mode) {
	case DRV_HASH_MD5:
		return cc_md5_init;
	case DRV_HASH_SHA1:
		return cc_sha1_init;
	case DRV_HASH_SHA224:
		return cc_sha224_init;
	case DRV_HASH_SHA256:
		return cc_sha256_init;
	case DRV_HASH_SHA384:
		return cc_sha384_init;
	case DRV_HASH_SHA512:
		return cc_sha512_init;
	case DRV_HASH_SM3:
		return cc_sm3_init;
	default:
		dev_err(dev, "Invalid hash mode (%d)\n", mode);
		return cc_md5_init;
	}
}

/**
 * cc_larval_digest_addr() - Get the address of the initial digest in SRAM
 * according to the given hash mode
 *
 * @drvdata: Associated device driver context
 * @mode: The Hash mode. Supported modes: MD5/SHA1/SHA224/SHA256
 *
 * Return:
 * The address of the initial digest in SRAM
 */
u32 cc_larval_digest_addr(void *drvdata, u32 mode)
{
	struct cc_drvdata *_drvdata = (struct cc_drvdata *)drvdata;
	struct cc_hash_handle *hash_handle = _drvdata->hash_handle;
	struct device *dev = drvdata_to_dev(_drvdata);
	bool sm3_supported = (_drvdata->hw_rev >= CC_HW_REV_713);
	u32 addr;

	switch (mode) {
	case DRV_HASH_NULL:
		break; /*Ignore*/
	case DRV_HASH_MD5:
		return (hash_handle->larval_digest_sram_addr);
	case DRV_HASH_SHA1:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init));
	case DRV_HASH_SHA224:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init));
	case DRV_HASH_SHA256:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init) +
			sizeof(cc_sha224_init));
	case DRV_HASH_SM3:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init) +
			sizeof(cc_sha224_init) +
			sizeof(cc_sha256_init));
	case DRV_HASH_SHA384:
		addr = (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init) +
			sizeof(cc_sha224_init) +
			sizeof(cc_sha256_init));
		if (sm3_supported)
			addr += sizeof(cc_sm3_init);
		return addr;
	case DRV_HASH_SHA512:
		addr = (hash_handle->larval_digest_sram_addr +
			sizeof(cc_md5_init) +
			sizeof(cc_sha1_init) +
			sizeof(cc_sha224_init) +
			sizeof(cc_sha256_init) +
			sizeof(cc_sha384_init));
		if (sm3_supported)
			addr += sizeof(cc_sm3_init);
		return addr;
	default:
		dev_err(dev, "Invalid hash mode (%d)\n", mode);
	}

	/*This is valid wrong value to avoid kernel crash*/
	return hash_handle->larval_digest_sram_addr;
}

u32 cc_digest_len_addr(void *drvdata, u32 mode)
{
	struct cc_drvdata *_drvdata = (struct cc_drvdata *)drvdata;
	struct cc_hash_handle *hash_handle = _drvdata->hash_handle;
	u32 digest_len_addr = hash_handle->digest_len_sram_addr;

	switch (mode) {
	case DRV_HASH_SHA1:
	case DRV_HASH_SHA224:
	case DRV_HASH_SHA256:
	case DRV_HASH_MD5:
		return digest_len_addr;
	case DRV_HASH_SHA384:
	case DRV_HASH_SHA512:
		return  digest_len_addr + sizeof(cc_digest_len_init);
	default:
		return digest_len_addr; /*to avoid kernel crash*/
	}
}
