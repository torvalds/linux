/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <crypto/algapi.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/md5.h>
#include <crypto/internal/hash.h>

#include "ssi_config.h"
#include "ssi_driver.h"
#include "ssi_request_mgr.h"
#include "ssi_buffer_mgr.h"
#include "ssi_sysfs.h"
#include "ssi_hash.h"
#include "ssi_sram_mgr.h"

#define SSI_MAX_AHASH_SEQ_LEN 12
#define SSI_MAX_HASH_OPAD_TMP_KEYS_SIZE SSI_MAX_HASH_BLCK_SIZE

struct ssi_hash_handle {
	ssi_sram_addr_t digest_len_sram_addr; /* const value in SRAM*/
	ssi_sram_addr_t larval_digest_sram_addr;   /* const value in SRAM */
	struct list_head hash_list;
	struct completion init_comp;
};

static const u32 digest_len_init[] = {
	0x00000040, 0x00000000, 0x00000000, 0x00000000 };
static const u32 md5_init[] = {
	SHA1_H3, SHA1_H2, SHA1_H1, SHA1_H0 };
static const u32 sha1_init[] = {
	SHA1_H4, SHA1_H3, SHA1_H2, SHA1_H1, SHA1_H0 };
static const u32 sha224_init[] = {
	SHA224_H7, SHA224_H6, SHA224_H5, SHA224_H4,
	SHA224_H3, SHA224_H2, SHA224_H1, SHA224_H0 };
static const u32 sha256_init[] = {
	SHA256_H7, SHA256_H6, SHA256_H5, SHA256_H4,
	SHA256_H3, SHA256_H2, SHA256_H1, SHA256_H0 };
#if (DX_DEV_SHA_MAX > 256)
static const u32 digest_len_sha512_init[] = {
	0x00000080, 0x00000000, 0x00000000, 0x00000000 };
static const u64 sha384_init[] = {
	SHA384_H7, SHA384_H6, SHA384_H5, SHA384_H4,
	SHA384_H3, SHA384_H2, SHA384_H1, SHA384_H0 };
static const u64 sha512_init[] = {
	SHA512_H7, SHA512_H6, SHA512_H5, SHA512_H4,
	SHA512_H3, SHA512_H2, SHA512_H1, SHA512_H0 };
#endif

static void ssi_hash_create_xcbc_setup(
	struct ahash_request *areq,
	struct cc_hw_desc desc[],
	unsigned int *seq_size);

static void ssi_hash_create_cmac_setup(struct ahash_request *areq,
				       struct cc_hw_desc desc[],
				       unsigned int *seq_size);

struct ssi_hash_alg {
	struct list_head entry;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	struct ssi_drvdata *drvdata;
	struct ahash_alg ahash_alg;
};

struct hash_key_req_ctx {
	u32 keylen;
	dma_addr_t key_dma_addr;
};

/* hash per-session context */
struct ssi_hash_ctx {
	struct ssi_drvdata *drvdata;
	/* holds the origin digest; the digest after "setkey" if HMAC,*
	 * the initial digest if HASH.
	 */
	u8 digest_buff[SSI_MAX_HASH_DIGEST_SIZE]  ____cacheline_aligned;
	u8 opad_tmp_keys_buff[SSI_MAX_HASH_OPAD_TMP_KEYS_SIZE]  ____cacheline_aligned;

	dma_addr_t opad_tmp_keys_dma_addr  ____cacheline_aligned;
	dma_addr_t digest_buff_dma_addr;
	/* use for hmac with key large then mode block size */
	struct hash_key_req_ctx key_params;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	struct completion setkey_comp;
	bool is_hmac;
};

static void ssi_hash_create_data_desc(
	struct ahash_req_ctx *areq_ctx,
	struct ssi_hash_ctx *ctx,
	unsigned int flow_mode, struct cc_hw_desc desc[],
	bool is_not_last_data,
	unsigned int *seq_size);

static inline void ssi_set_hash_endianity(u32 mode, struct cc_hw_desc *desc)
{
	if (unlikely(mode == DRV_HASH_MD5 ||
		     mode == DRV_HASH_SHA384 ||
		     mode == DRV_HASH_SHA512)) {
		set_bytes_swap(desc, 1);
	} else {
		set_cipher_config0(desc, HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	}
}

static int ssi_hash_map_result(struct device *dev,
			       struct ahash_req_ctx *state,
			       unsigned int digestsize)
{
	state->digest_result_dma_addr =
		dma_map_single(dev, (void *)state->digest_result_buff,
			       digestsize,
			       DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, state->digest_result_dma_addr))) {
		dev_err(dev, "Mapping digest result buffer %u B for DMA failed\n",
			digestsize);
		return -ENOMEM;
	}
	dev_dbg(dev, "Mapped digest result buffer %u B at va=%pK to dma=%pad\n",
		digestsize, state->digest_result_buff,
		&state->digest_result_dma_addr);

	return 0;
}

static int ssi_hash_map_request(struct device *dev,
				struct ahash_req_ctx *state,
				struct ssi_hash_ctx *ctx)
{
	bool is_hmac = ctx->is_hmac;
	ssi_sram_addr_t larval_digest_addr =
		cc_larval_digest_addr(ctx->drvdata, ctx->hash_mode);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc;
	int rc = -ENOMEM;

	state->buff0 = kzalloc(SSI_MAX_HASH_BLCK_SIZE, GFP_KERNEL | GFP_DMA);
	if (!state->buff0)
		goto fail0;

	state->buff1 = kzalloc(SSI_MAX_HASH_BLCK_SIZE, GFP_KERNEL | GFP_DMA);
	if (!state->buff1)
		goto fail_buff0;

	state->digest_result_buff = kzalloc(SSI_MAX_HASH_DIGEST_SIZE,
					    GFP_KERNEL | GFP_DMA);
	if (!state->digest_result_buff)
		goto fail_buff1;

	state->digest_buff = kzalloc(ctx->inter_digestsize,
				     GFP_KERNEL | GFP_DMA);
	if (!state->digest_buff)
		goto fail_digest_result_buff;

	dev_dbg(dev, "Allocated digest-buffer in context ctx->digest_buff=@%p\n",
		state->digest_buff);
	if (ctx->hw_mode != DRV_CIPHER_XCBC_MAC) {
		state->digest_bytes_len = kzalloc(HASH_LEN_SIZE,
						  GFP_KERNEL | GFP_DMA);
		if (!state->digest_bytes_len)
			goto fail1;

		dev_dbg(dev, "Allocated digest-bytes-len in context state->>digest_bytes_len=@%p\n",
			state->digest_bytes_len);
	} else {
		state->digest_bytes_len = NULL;
	}

	state->opad_digest_buff = kzalloc(ctx->inter_digestsize,
					  GFP_KERNEL | GFP_DMA);
	if (!state->opad_digest_buff)
		goto fail2;

	dev_dbg(dev, "Allocated opad-digest-buffer in context state->digest_bytes_len=@%p\n",
		state->opad_digest_buff);

	state->digest_buff_dma_addr =
		dma_map_single(dev, (void *)state->digest_buff,
			       ctx->inter_digestsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, state->digest_buff_dma_addr)) {
		dev_err(dev, "Mapping digest len %d B at va=%pK for DMA failed\n",
			ctx->inter_digestsize, state->digest_buff);
		goto fail3;
	}
	dev_dbg(dev, "Mapped digest %d B at va=%pK to dma=%pad\n",
		ctx->inter_digestsize, state->digest_buff,
		&state->digest_buff_dma_addr);

	if (is_hmac) {
		dma_sync_single_for_cpu(dev, ctx->digest_buff_dma_addr,
					ctx->inter_digestsize,
					DMA_BIDIRECTIONAL);
		if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC ||
		    ctx->hw_mode == DRV_CIPHER_CMAC) {
			memset(state->digest_buff, 0, ctx->inter_digestsize);
		} else { /*sha*/
			memcpy(state->digest_buff, ctx->digest_buff,
			       ctx->inter_digestsize);
#if (DX_DEV_SHA_MAX > 256)
			if (unlikely(ctx->hash_mode == DRV_HASH_SHA512 ||
				     ctx->hash_mode == DRV_HASH_SHA384))
				memcpy(state->digest_bytes_len,
				       digest_len_sha512_init, HASH_LEN_SIZE);
			else
				memcpy(state->digest_bytes_len,
				       digest_len_init, HASH_LEN_SIZE);
#else
			memcpy(state->digest_bytes_len, digest_len_init,
			       HASH_LEN_SIZE);
#endif
		}
		dma_sync_single_for_device(dev, state->digest_buff_dma_addr,
					   ctx->inter_digestsize,
					   DMA_BIDIRECTIONAL);

		if (ctx->hash_mode != DRV_HASH_NULL) {
			dma_sync_single_for_cpu(dev,
						ctx->opad_tmp_keys_dma_addr,
						ctx->inter_digestsize,
						DMA_BIDIRECTIONAL);
			memcpy(state->opad_digest_buff,
			       ctx->opad_tmp_keys_buff, ctx->inter_digestsize);
		}
	} else { /*hash*/
		/* Copy the initial digests if hash flow. The SRAM contains the
		 * initial digests in the expected order for all SHA*
		 */
		hw_desc_init(&desc);
		set_din_sram(&desc, larval_digest_addr, ctx->inter_digestsize);
		set_dout_dlli(&desc, state->digest_buff_dma_addr,
			      ctx->inter_digestsize, NS_BIT, 0);
		set_flow_mode(&desc, BYPASS);

		rc = send_request(ctx->drvdata, &ssi_req, &desc, 1, 0);
		if (unlikely(rc)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			goto fail4;
		}
	}

	if (ctx->hw_mode != DRV_CIPHER_XCBC_MAC) {
		state->digest_bytes_len_dma_addr =
			dma_map_single(dev, (void *)state->digest_bytes_len,
				       HASH_LEN_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, state->digest_bytes_len_dma_addr)) {
			dev_err(dev, "Mapping digest len %u B at va=%pK for DMA failed\n",
				HASH_LEN_SIZE, state->digest_bytes_len);
			goto fail4;
		}
		dev_dbg(dev, "Mapped digest len %u B at va=%pK to dma=%pad\n",
			HASH_LEN_SIZE, state->digest_bytes_len,
			&state->digest_bytes_len_dma_addr);
	} else {
		state->digest_bytes_len_dma_addr = 0;
	}

	if (is_hmac && ctx->hash_mode != DRV_HASH_NULL) {
		state->opad_digest_dma_addr =
			dma_map_single(dev, (void *)state->opad_digest_buff,
				       ctx->inter_digestsize,
				       DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, state->opad_digest_dma_addr)) {
			dev_err(dev, "Mapping opad digest %d B at va=%pK for DMA failed\n",
				ctx->inter_digestsize,
				state->opad_digest_buff);
			goto fail5;
		}
		dev_dbg(dev, "Mapped opad digest %d B at va=%pK to dma=%pad\n",
			ctx->inter_digestsize, state->opad_digest_buff,
			&state->opad_digest_dma_addr);
	} else {
		state->opad_digest_dma_addr = 0;
	}
	state->buff0_cnt = 0;
	state->buff1_cnt = 0;
	state->buff_index = 0;
	state->mlli_params.curr_pool = NULL;

	return 0;

fail5:
	if (state->digest_bytes_len_dma_addr) {
		dma_unmap_single(dev, state->digest_bytes_len_dma_addr,
				 HASH_LEN_SIZE, DMA_BIDIRECTIONAL);
		state->digest_bytes_len_dma_addr = 0;
	}
fail4:
	if (state->digest_buff_dma_addr) {
		dma_unmap_single(dev, state->digest_buff_dma_addr,
				 ctx->inter_digestsize, DMA_BIDIRECTIONAL);
		state->digest_buff_dma_addr = 0;
	}
fail3:
	kfree(state->opad_digest_buff);
fail2:
	kfree(state->digest_bytes_len);
fail1:
	 kfree(state->digest_buff);
fail_digest_result_buff:
	kfree(state->digest_result_buff);
	state->digest_result_buff = NULL;
fail_buff1:
	kfree(state->buff1);
	state->buff1 = NULL;
fail_buff0:
	kfree(state->buff0);
	state->buff0 = NULL;
fail0:
	return rc;
}

static void ssi_hash_unmap_request(struct device *dev,
				   struct ahash_req_ctx *state,
				   struct ssi_hash_ctx *ctx)
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
				 HASH_LEN_SIZE, DMA_BIDIRECTIONAL);
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

	kfree(state->opad_digest_buff);
	kfree(state->digest_bytes_len);
	kfree(state->digest_buff);
	kfree(state->digest_result_buff);
	kfree(state->buff1);
	kfree(state->buff0);
}

static void ssi_hash_unmap_result(struct device *dev,
				  struct ahash_req_ctx *state,
				  unsigned int digestsize, u8 *result)
{
	if (state->digest_result_dma_addr) {
		dma_unmap_single(dev,
				 state->digest_result_dma_addr,
				 digestsize,
				  DMA_BIDIRECTIONAL);
		dev_dbg(dev, "unmpa digest result buffer va (%pK) pa (%pad) len %u\n",
			state->digest_result_buff,
			&state->digest_result_dma_addr, digestsize);
		memcpy(result,
		       state->digest_result_buff,
		       digestsize);
	}
	state->digest_result_dma_addr = 0;
}

static void ssi_hash_update_complete(struct device *dev, void *ssi_req)
{
	struct ahash_request *req = (struct ahash_request *)ssi_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);

	dev_dbg(dev, "req=%pK\n", req);

	cc_unmap_hash_request(dev, state, req->src, false);
	req->base.complete(&req->base, 0);
}

static void ssi_hash_digest_complete(struct device *dev, void *ssi_req)
{
	struct ahash_request *req = (struct ahash_request *)ssi_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	dev_dbg(dev, "req=%pK\n", req);

	cc_unmap_hash_request(dev, state, req->src, false);
	ssi_hash_unmap_result(dev, state, digestsize, req->result);
	ssi_hash_unmap_request(dev, state, ctx);
	req->base.complete(&req->base, 0);
}

static void ssi_hash_complete(struct device *dev, void *ssi_req)
{
	struct ahash_request *req = (struct ahash_request *)ssi_req;
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	dev_dbg(dev, "req=%pK\n", req);

	cc_unmap_hash_request(dev, state, req->src, false);
	ssi_hash_unmap_result(dev, state, digestsize, req->result);
	ssi_hash_unmap_request(dev, state, ctx);
	req->base.complete(&req->base, 0);
}

static int ssi_hash_digest(struct ahash_req_ctx *state,
			   struct ssi_hash_ctx *ctx,
			   unsigned int digestsize,
			   struct scatterlist *src,
			   unsigned int nbytes, u8 *result,
			   void *async_req)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	bool is_hmac = ctx->is_hmac;
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	ssi_sram_addr_t larval_digest_addr =
		cc_larval_digest_addr(ctx->drvdata, ctx->hash_mode);
	int idx = 0;
	int rc = 0;

	dev_dbg(dev, "===== %s-digest (%d) ====\n", is_hmac ? "hmac" : "hash",
		nbytes);

	if (unlikely(ssi_hash_map_request(dev, state, ctx))) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -ENOMEM;
	}

	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state,
					       src, nbytes, 1))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}

	if (async_req) {
		/* Setup DX request structure */
		ssi_req.user_cb = (void *)ssi_hash_digest_complete;
		ssi_req.user_arg = (void *)async_req;
	}

	/* If HMAC then load hash IPAD xor key, if HASH then load initial
	 * digest
	 */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	if (is_hmac) {
		set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
			     ctx->inter_digestsize, NS_BIT);
	} else {
		set_din_sram(&desc[idx], larval_digest_addr,
			     ctx->inter_digestsize);
	}
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load the hash current length */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);

	if (is_hmac) {
		set_din_type(&desc[idx], DMA_DLLI,
			     state->digest_bytes_len_dma_addr, HASH_LEN_SIZE,
			     NS_BIT);
	} else {
		set_din_const(&desc[idx], 0, HASH_LEN_SIZE);
		if (likely(nbytes))
			set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
		else
			set_cipher_do(&desc[idx], DO_PAD);
	}
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	ssi_hash_create_data_desc(state, ctx, DIN_HASH, desc, false, &idx);

	if (is_hmac) {
		/* HW last hash block padding (aka. "DO_PAD") */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      HASH_LEN_SIZE, NS_BIT, 0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
		set_cipher_do(&desc[idx], DO_PAD);
		idx++;

		/* store the hash digest result in the context */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      digestsize, NS_BIT, 0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
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
			     ssi_ahash_get_initial_digest_len_sram_addr(
ctx->drvdata, ctx->hash_mode), HASH_LEN_SIZE);
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
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr, digestsize,
		      NS_BIT, (async_req ? 1 : 0));
	if (async_req)
		set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
	idx++;

	if (async_req) {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
		if (unlikely(rc != -EINPROGRESS)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
			ssi_hash_unmap_result(dev, state, digestsize, result);
			ssi_hash_unmap_request(dev, state, ctx);
		}
	} else {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
		if (rc) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
		} else {
			cc_unmap_hash_request(dev, state, src, false);
		}
		ssi_hash_unmap_result(dev, state, digestsize, result);
		ssi_hash_unmap_request(dev, state, ctx);
	}
	return rc;
}

static int ssi_hash_update(struct ahash_req_ctx *state,
			   struct ssi_hash_ctx *ctx,
			   unsigned int block_size,
			   struct scatterlist *src,
			   unsigned int nbytes,
			   void *async_req)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	u32 idx = 0;
	int rc;

	dev_dbg(dev, "===== %s-update (%d) ====\n", ctx->is_hmac ?
		"hmac" : "hash", nbytes);

	if (nbytes == 0) {
		/* no real updates required */
		return 0;
	}

	rc = cc_map_hash_request_update(ctx->drvdata, state, src, nbytes,
					block_size);
	if (unlikely(rc)) {
		if (rc == 1) {
			dev_dbg(dev, " data size not require HW update %x\n",
				nbytes);
			/* No hardware updates are required */
			return 0;
		}
		dev_err(dev, "map_ahash_request_update() failed\n");
		return -ENOMEM;
	}

	if (async_req) {
		/* Setup DX request structure */
		ssi_req.user_cb = (void *)ssi_hash_update_complete;
		ssi_req.user_arg = async_req;
	}

	/* Restore hash digest */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     ctx->inter_digestsize, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;
	/* Restore hash current length */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_bytes_len_dma_addr,
		     HASH_LEN_SIZE, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	ssi_hash_create_data_desc(state, ctx, DIN_HASH, desc, false, &idx);

	/* store the hash digest result in context */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
		      ctx->inter_digestsize, NS_BIT, 0);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* store current hash length in context */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_bytes_len_dma_addr,
		      HASH_LEN_SIZE, NS_BIT, (async_req ? 1 : 0));
	if (async_req)
		set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
	idx++;

	if (async_req) {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
		if (unlikely(rc != -EINPROGRESS)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
		}
	} else {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
		if (rc) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
		} else {
			cc_unmap_hash_request(dev, state, src, false);
		}
	}
	return rc;
}

static int ssi_hash_finup(struct ahash_req_ctx *state,
			  struct ssi_hash_ctx *ctx,
			  unsigned int digestsize,
			  struct scatterlist *src,
			  unsigned int nbytes,
			  u8 *result,
			  void *async_req)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	bool is_hmac = ctx->is_hmac;
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	int idx = 0;
	int rc;

	dev_dbg(dev, "===== %s-finup (%d) ====\n", is_hmac ? "hmac" : "hash",
		nbytes);

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state, src,
					       nbytes, 1))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}
	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	if (async_req) {
		/* Setup DX request structure */
		ssi_req.user_cb = (void *)ssi_hash_complete;
		ssi_req.user_arg = async_req;
	}

	/* Restore hash digest */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     ctx->inter_digestsize, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Restore hash current length */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_bytes_len_dma_addr,
		     HASH_LEN_SIZE, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	ssi_hash_create_data_desc(state, ctx, DIN_HASH, desc, false, &idx);

	if (is_hmac) {
		/* Store the hash digest result in the context */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      digestsize, NS_BIT, 0);
		ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		idx++;

		/* Loading hash OPAD xor key state */
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
			     ssi_ahash_get_initial_digest_len_sram_addr(
ctx->drvdata, ctx->hash_mode), HASH_LEN_SIZE);
		set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Memory Barrier: wait for IPAD/OPAD axi write to complete */
		hw_desc_init(&desc[idx]);
		set_din_no_dma(&desc[idx], 0, 0xfffff0);
		set_dout_no_dma(&desc[idx], 0, 0, 1);
		idx++;

		/* Perform HASH update on last digest */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
			     digestsize, NS_BIT);
		set_flow_mode(&desc[idx], DIN_HASH);
		idx++;
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr, digestsize,
		      NS_BIT, (async_req ? 1 : 0));
	if (async_req)
		set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	if (async_req) {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
		if (unlikely(rc != -EINPROGRESS)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
			ssi_hash_unmap_result(dev, state, digestsize, result);
		}
	} else {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
		if (rc) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
			ssi_hash_unmap_result(dev, state, digestsize, result);
		} else {
			cc_unmap_hash_request(dev, state, src, false);
			ssi_hash_unmap_result(dev, state, digestsize, result);
			ssi_hash_unmap_request(dev, state, ctx);
		}
	}
	return rc;
}

static int ssi_hash_final(struct ahash_req_ctx *state,
			  struct ssi_hash_ctx *ctx,
			  unsigned int digestsize,
			  struct scatterlist *src,
			  unsigned int nbytes,
			  u8 *result,
			  void *async_req)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	bool is_hmac = ctx->is_hmac;
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	int idx = 0;
	int rc;

	dev_dbg(dev, "===== %s-final (%d) ====\n", is_hmac ? "hmac" : "hash",
		nbytes);

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state, src,
					       nbytes, 0))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}

	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	if (async_req) {
		/* Setup DX request structure */
		ssi_req.user_cb = (void *)ssi_hash_complete;
		ssi_req.user_arg = async_req;
	}

	/* Restore hash digest */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
		     ctx->inter_digestsize, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Restore hash current length */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	set_din_type(&desc[idx], DMA_DLLI, state->digest_bytes_len_dma_addr,
		     HASH_LEN_SIZE, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	ssi_hash_create_data_desc(state, ctx, DIN_HASH, desc, false, &idx);

	/* "DO-PAD" must be enabled only when writing current length to HW */
	hw_desc_init(&desc[idx]);
	set_cipher_do(&desc[idx], DO_PAD);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_bytes_len_dma_addr,
		      HASH_LEN_SIZE, NS_BIT, 0);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	idx++;

	if (is_hmac) {
		/* Store the hash digest result in the context */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
			      digestsize, NS_BIT, 0);
		ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		idx++;

		/* Loading hash OPAD xor key state */
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
			     ssi_ahash_get_initial_digest_len_sram_addr(
ctx->drvdata, ctx->hash_mode), HASH_LEN_SIZE);
		set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Memory Barrier: wait for IPAD/OPAD axi write to complete */
		hw_desc_init(&desc[idx]);
		set_din_no_dma(&desc[idx], 0, 0xfffff0);
		set_dout_no_dma(&desc[idx], 0, 0, 1);
		idx++;

		/* Perform HASH update on last digest */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI, state->digest_buff_dma_addr,
			     digestsize, NS_BIT);
		set_flow_mode(&desc[idx], DIN_HASH);
		idx++;
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr, digestsize,
		      NS_BIT, (async_req ? 1 : 0));
	if (async_req)
		set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	if (async_req) {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
		if (unlikely(rc != -EINPROGRESS)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
			ssi_hash_unmap_result(dev, state, digestsize, result);
		}
	} else {
		rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
		if (rc) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			cc_unmap_hash_request(dev, state, src, true);
			ssi_hash_unmap_result(dev, state, digestsize, result);
		} else {
			cc_unmap_hash_request(dev, state, src, false);
			ssi_hash_unmap_result(dev, state, digestsize, result);
			ssi_hash_unmap_request(dev, state, ctx);
		}
	}
	return rc;
}

static int ssi_hash_init(struct ahash_req_ctx *state, struct ssi_hash_ctx *ctx)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	state->xcbc_count = 0;

	ssi_hash_map_request(dev, state, ctx);

	return 0;
}

static int ssi_hash_setkey(void *hash,
			   const u8 *key,
			   unsigned int keylen,
			   bool synchronize)
{
	unsigned int hmac_pad_const[2] = { HMAC_IPAD_CONST, HMAC_OPAD_CONST };
	struct ssi_crypto_req ssi_req = {};
	struct ssi_hash_ctx *ctx = NULL;
	int blocksize = 0;
	int digestsize = 0;
	int i, idx = 0, rc = 0;
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	ssi_sram_addr_t larval_addr;
	struct device *dev;

	ctx = crypto_ahash_ctx(((struct crypto_ahash *)hash));
	dev = drvdata_to_dev(ctx->drvdata);
	dev_dbg(dev, "start keylen: %d", keylen);

	blocksize = crypto_tfm_alg_blocksize(&((struct crypto_ahash *)hash)->base);
	digestsize = crypto_ahash_digestsize(((struct crypto_ahash *)hash));

	larval_addr = cc_larval_digest_addr(ctx->drvdata, ctx->hash_mode);

	/* The keylen value distinguishes HASH in case keylen is ZERO bytes,
	 * any NON-ZERO value utilizes HMAC flow
	 */
	ctx->key_params.keylen = keylen;
	ctx->key_params.key_dma_addr = 0;
	ctx->is_hmac = true;

	if (keylen) {
		ctx->key_params.key_dma_addr = dma_map_single(
						dev, (void *)key,
						keylen, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev,
					       ctx->key_params.key_dma_addr))) {
			dev_err(dev, "Mapping key va=0x%p len=%u for DMA failed\n",
				key, keylen);
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
			set_din_const(&desc[idx], 0, HASH_LEN_SIZE);
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
			ssi_set_hash_endianity(ctx->hash_mode, &desc[idx]);
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

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
	if (unlikely(rc)) {
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
		set_din_const(&desc[idx], 0, HASH_LEN_SIZE);
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

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);

out:
	if (rc)
		crypto_ahash_set_flags((struct crypto_ahash *)hash,
				       CRYPTO_TFM_RES_BAD_KEY_LEN);

	if (ctx->key_params.key_dma_addr) {
		dma_unmap_single(dev, ctx->key_params.key_dma_addr,
				 ctx->key_params.keylen, DMA_TO_DEVICE);
		dev_dbg(dev, "Unmapped key-buffer: key_dma_addr=%pad keylen=%u\n",
			&ctx->key_params.key_dma_addr, ctx->key_params.keylen);
	}
	return rc;
}

static int ssi_xcbc_setkey(struct crypto_ahash *ahash,
			   const u8 *key, unsigned int keylen)
{
	struct ssi_crypto_req ssi_req = {};
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	int idx = 0, rc = 0;
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];

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

	ctx->key_params.key_dma_addr = dma_map_single(
					dev, (void *)key,
					keylen, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, ctx->key_params.key_dma_addr))) {
		dev_err(dev, "Mapping key va=0x%p len=%u for DMA failed\n",
			key, keylen);
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
	set_dout_dlli(&desc[idx], (ctx->opad_tmp_keys_dma_addr +
					   XCBC_MAC_K1_OFFSET),
			      CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x02020202, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	set_dout_dlli(&desc[idx], (ctx->opad_tmp_keys_dma_addr +
					   XCBC_MAC_K2_OFFSET),
			      CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x03030303, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	set_dout_dlli(&desc[idx], (ctx->opad_tmp_keys_dma_addr +
					   XCBC_MAC_K3_OFFSET),
			       CC_AES_128_BIT_KEY_SIZE, NS_BIT, 0);
	idx++;

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);

	if (rc)
		crypto_ahash_set_flags(ahash, CRYPTO_TFM_RES_BAD_KEY_LEN);

	dma_unmap_single(dev, ctx->key_params.key_dma_addr,
			 ctx->key_params.keylen, DMA_TO_DEVICE);
	dev_dbg(dev, "Unmapped key-buffer: key_dma_addr=%pad keylen=%u\n",
		&ctx->key_params.key_dma_addr, ctx->key_params.keylen);

	return rc;
}

#if SSI_CC_HAS_CMAC
static int ssi_cmac_setkey(struct crypto_ahash *ahash,
			   const u8 *key, unsigned int keylen)
{
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(ahash);
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
#endif

static void ssi_hash_free_ctx(struct ssi_hash_ctx *ctx)
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

static int ssi_hash_alloc_ctx(struct ssi_hash_ctx *ctx)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	ctx->key_params.keylen = 0;

	ctx->digest_buff_dma_addr =
		dma_map_single(dev, (void *)ctx->digest_buff,
			       sizeof(ctx->digest_buff), DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, ctx->digest_buff_dma_addr)) {
		dev_err(dev, "Mapping digest len %zu B at va=%pK for DMA failed\n",
			sizeof(ctx->digest_buff), ctx->digest_buff);
		goto fail;
	}
	dev_dbg(dev, "Mapped digest %zu B at va=%pK to dma=%pad\n",
		sizeof(ctx->digest_buff), ctx->digest_buff,
		&ctx->digest_buff_dma_addr);

	ctx->opad_tmp_keys_dma_addr =
		dma_map_single(dev, (void *)ctx->opad_tmp_keys_buff,
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
	ssi_hash_free_ctx(ctx);
	return -ENOMEM;
}

static int ssi_ahash_cra_init(struct crypto_tfm *tfm)
{
	struct ssi_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct hash_alg_common *hash_alg_common =
		container_of(tfm->__crt_alg, struct hash_alg_common, base);
	struct ahash_alg *ahash_alg =
		container_of(hash_alg_common, struct ahash_alg, halg);
	struct ssi_hash_alg *ssi_alg =
			container_of(ahash_alg, struct ssi_hash_alg,
				     ahash_alg);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct ahash_req_ctx));

	ctx->hash_mode = ssi_alg->hash_mode;
	ctx->hw_mode = ssi_alg->hw_mode;
	ctx->inter_digestsize = ssi_alg->inter_digestsize;
	ctx->drvdata = ssi_alg->drvdata;

	return ssi_hash_alloc_ctx(ctx);
}

static void ssi_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct ssi_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "ssi_hash_cra_exit");
	ssi_hash_free_ctx(ctx);
}

static int ssi_mac_update(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	unsigned int block_size = crypto_tfm_alg_blocksize(&tfm->base);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	int rc;
	u32 idx = 0;

	if (req->nbytes == 0) {
		/* no real updates required */
		return 0;
	}

	state->xcbc_count++;

	rc = cc_map_hash_request_update(ctx->drvdata, state, req->src,
					req->nbytes, block_size);
	if (unlikely(rc)) {
		if (rc == 1) {
			dev_dbg(dev, " data size not require HW update %x\n",
				req->nbytes);
			/* No hardware updates are required */
			return 0;
		}
		dev_err(dev, "map_ahash_request_update() failed\n");
		return -ENOMEM;
	}

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC)
		ssi_hash_create_xcbc_setup(req, desc, &idx);
	else
		ssi_hash_create_cmac_setup(req, desc, &idx);

	ssi_hash_create_data_desc(state, ctx, DIN_AES_DOUT, desc, true, &idx);

	/* store the hash digest result in context */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	set_dout_dlli(&desc[idx], state->digest_buff_dma_addr,
		      ctx->inter_digestsize, NS_BIT, 1);
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	idx++;

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_hash_update_complete;
	ssi_req.user_arg = (void *)req;

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
	if (unlikely(rc != -EINPROGRESS)) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
	}
	return rc;
}

static int ssi_mac_final(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	int idx = 0;
	int rc = 0;
	u32 key_size, key_len;
	u32 digestsize = crypto_ahash_digestsize(tfm);

	u32 rem_cnt = state->buff_index ? state->buff1_cnt :
			state->buff0_cnt;

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_size = CC_AES_128_BIT_KEY_SIZE;
		key_len  = CC_AES_128_BIT_KEY_SIZE;
	} else {
		key_size = (ctx->key_params.keylen == 24) ? AES_MAX_KEY_SIZE :
			ctx->key_params.keylen;
		key_len =  ctx->key_params.keylen;
	}

	dev_dbg(dev, "===== final  xcbc reminder (%d) ====\n", rem_cnt);

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state, req->src,
					       req->nbytes, 0))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}

	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_hash_complete;
	ssi_req.user_arg = (void *)req;

	if (state->xcbc_count && rem_cnt == 0) {
		/* Load key for ECB decryption */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], DRV_CIPHER_ECB);
		set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_DECRYPT);
		set_din_type(&desc[idx], DMA_DLLI,
			     (ctx->opad_tmp_keys_dma_addr +
			      XCBC_MAC_K1_OFFSET), key_size, NS_BIT);
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
		ssi_hash_create_xcbc_setup(req, desc, &idx);
	else
		ssi_hash_create_cmac_setup(req, desc, &idx);

	if (state->xcbc_count == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else if (rem_cnt > 0) {
		ssi_hash_create_data_desc(state, ctx, DIN_AES_DOUT, desc,
					  false, &idx);
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
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
	if (unlikely(rc != -EINPROGRESS)) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		ssi_hash_unmap_result(dev, state, digestsize, req->result);
	}
	return rc;
}

static int ssi_mac_finup(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	int idx = 0;
	int rc = 0;
	u32 key_len = 0;
	u32 digestsize = crypto_ahash_digestsize(tfm);

	dev_dbg(dev, "===== finup xcbc(%d) ====\n", req->nbytes);
	if (state->xcbc_count > 0 && req->nbytes == 0) {
		dev_dbg(dev, "No data to update. Call to fdx_mac_final\n");
		return ssi_mac_final(req);
	}

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state, req->src,
					       req->nbytes, 1))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}
	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_hash_complete;
	ssi_req.user_arg = (void *)req;

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_len = CC_AES_128_BIT_KEY_SIZE;
		ssi_hash_create_xcbc_setup(req, desc, &idx);
	} else {
		key_len = ctx->key_params.keylen;
		ssi_hash_create_cmac_setup(req, desc, &idx);
	}

	if (req->nbytes == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else {
		ssi_hash_create_data_desc(state, ctx, DIN_AES_DOUT, desc,
					  false, &idx);
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	/* TODO */
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr,
		      digestsize, NS_BIT, 1);
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
	if (unlikely(rc != -EINPROGRESS)) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		ssi_hash_unmap_result(dev, state, digestsize, req->result);
	}
	return rc;
}

static int ssi_mac_digest(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	u32 digestsize = crypto_ahash_digestsize(tfm);
	struct ssi_crypto_req ssi_req = {};
	struct cc_hw_desc desc[SSI_MAX_AHASH_SEQ_LEN];
	u32 key_len;
	int idx = 0;
	int rc;

	dev_dbg(dev, "===== -digest mac (%d) ====\n",  req->nbytes);

	if (unlikely(ssi_hash_map_request(dev, state, ctx))) {
		dev_err(dev, "map_ahash_source() failed\n");
		return -ENOMEM;
	}
	if (unlikely(ssi_hash_map_result(dev, state, digestsize))) {
		dev_err(dev, "map_ahash_digest() failed\n");
		return -ENOMEM;
	}

	if (unlikely(cc_map_hash_request_final(ctx->drvdata, state, req->src,
					       req->nbytes, 1))) {
		dev_err(dev, "map_ahash_request_final() failed\n");
		return -ENOMEM;
	}

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_hash_digest_complete;
	ssi_req.user_arg = (void *)req;

	if (ctx->hw_mode == DRV_CIPHER_XCBC_MAC) {
		key_len = CC_AES_128_BIT_KEY_SIZE;
		ssi_hash_create_xcbc_setup(req, desc, &idx);
	} else {
		key_len = ctx->key_params.keylen;
		ssi_hash_create_cmac_setup(req, desc, &idx);
	}

	if (req->nbytes == 0) {
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], ctx->hw_mode);
		set_key_size_aes(&desc[idx], key_len);
		set_cmac_size0_mode(&desc[idx]);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	} else {
		ssi_hash_create_data_desc(state, ctx, DIN_AES_DOUT, desc,
					  false, &idx);
	}

	/* Get final MAC result */
	hw_desc_init(&desc[idx]);
	set_dout_dlli(&desc[idx], state->digest_result_dma_addr,
		      CC_AES_BLOCK_SIZE, NS_BIT, 1);
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], S_AES_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_cipher_mode(&desc[idx], ctx->hw_mode);
	idx++;

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 1);
	if (unlikely(rc != -EINPROGRESS)) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		cc_unmap_hash_request(dev, state, req->src, true);
		ssi_hash_unmap_result(dev, state, digestsize, req->result);
		ssi_hash_unmap_request(dev, state, ctx);
	}
	return rc;
}

//ahash wrap functions
static int ssi_ahash_digest(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	return ssi_hash_digest(state, ctx, digestsize, req->src, req->nbytes,
			       req->result, (void *)req);
}

static int ssi_ahash_update(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int block_size = crypto_tfm_alg_blocksize(&tfm->base);

	return ssi_hash_update(state, ctx, block_size, req->src, req->nbytes,
			       (void *)req);
}

static int ssi_ahash_finup(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	return ssi_hash_finup(state, ctx, digestsize, req->src, req->nbytes,
			      req->result, (void *)req);
}

static int ssi_ahash_final(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	u32 digestsize = crypto_ahash_digestsize(tfm);

	return ssi_hash_final(state, ctx, digestsize, req->src, req->nbytes,
			      req->result, (void *)req);
}

static int ssi_ahash_init(struct ahash_request *req)
{
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "===== init (%d) ====\n", req->nbytes);

	return ssi_hash_init(state, ctx);
}

static int ssi_ahash_export(struct ahash_request *req, void *out)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	u8 *curr_buff = state->buff_index ? state->buff1 : state->buff0;
	u32 curr_buff_cnt = state->buff_index ? state->buff1_cnt :
				state->buff0_cnt;
	const u32 tmp = CC_EXPORT_MAGIC;

	memcpy(out, &tmp, sizeof(u32));
	out += sizeof(u32);

	dma_sync_single_for_cpu(dev, state->digest_buff_dma_addr,
				ctx->inter_digestsize, DMA_BIDIRECTIONAL);
	memcpy(out, state->digest_buff, ctx->inter_digestsize);
	out += ctx->inter_digestsize;

	if (state->digest_bytes_len_dma_addr) {
		dma_sync_single_for_cpu(dev, state->digest_bytes_len_dma_addr,
					HASH_LEN_SIZE, DMA_BIDIRECTIONAL);
		memcpy(out, state->digest_bytes_len, HASH_LEN_SIZE);
	} else {
		/* Poison the unused exported digest len field. */
		memset(out, 0x5F, HASH_LEN_SIZE);
	}
	out += HASH_LEN_SIZE;

	memcpy(out, &curr_buff_cnt, sizeof(u32));
	out += sizeof(u32);

	memcpy(out, curr_buff, curr_buff_cnt);

	/* No sync for device ineeded since we did not change the data,
	 * we only copy it
	 */

	return 0;
}

static int ssi_ahash_import(struct ahash_request *req, const void *in)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ahash_req_ctx *state = ahash_request_ctx(req);
	u32 tmp;
	int rc;

	memcpy(&tmp, in, sizeof(u32));
	if (tmp != CC_EXPORT_MAGIC) {
		rc = -EINVAL;
		goto out;
	}
	in += sizeof(u32);

	rc = ssi_hash_init(state, ctx);
	if (rc)
		goto out;

	dma_sync_single_for_cpu(dev, state->digest_buff_dma_addr,
				ctx->inter_digestsize, DMA_BIDIRECTIONAL);
	memcpy(state->digest_buff, in, ctx->inter_digestsize);
	in += ctx->inter_digestsize;

	if (state->digest_bytes_len_dma_addr) {
		dma_sync_single_for_cpu(dev, state->digest_bytes_len_dma_addr,
					HASH_LEN_SIZE, DMA_BIDIRECTIONAL);
		memcpy(state->digest_bytes_len, in, HASH_LEN_SIZE);
	}
	in += HASH_LEN_SIZE;

	dma_sync_single_for_device(dev, state->digest_buff_dma_addr,
				   ctx->inter_digestsize, DMA_BIDIRECTIONAL);

	if (state->digest_bytes_len_dma_addr)
		dma_sync_single_for_device(dev,
					   state->digest_bytes_len_dma_addr,
					   HASH_LEN_SIZE, DMA_BIDIRECTIONAL);

	state->buff_index = 0;

	/* Sanity check the data as much as possible */
	memcpy(&tmp, in, sizeof(u32));
	if (tmp > SSI_MAX_HASH_BLCK_SIZE) {
		rc = -EINVAL;
		goto out;
	}
	in += sizeof(u32);

	state->buff0_cnt = tmp;
	memcpy(state->buff0, in, state->buff0_cnt);

out:
	return rc;
}

static int ssi_ahash_setkey(struct crypto_ahash *ahash,
			    const u8 *key, unsigned int keylen)
{
	return ssi_hash_setkey((void *)ahash, key, keylen, false);
}

struct ssi_hash_template {
	char name[CRYPTO_MAX_ALG_NAME];
	char driver_name[CRYPTO_MAX_ALG_NAME];
	char mac_name[CRYPTO_MAX_ALG_NAME];
	char mac_driver_name[CRYPTO_MAX_ALG_NAME];
	unsigned int blocksize;
	bool synchronize;
	struct ahash_alg template_ahash;
	int hash_mode;
	int hw_mode;
	int inter_digestsize;
	struct ssi_drvdata *drvdata;
};

#define CC_STATE_SIZE(_x) \
	((_x) + HASH_LEN_SIZE + SSI_MAX_HASH_BLCK_SIZE + (2 * sizeof(u32)))

/* hash descriptors */
static struct ssi_hash_template driver_hash[] = {
	//Asynchronize hash template
	{
		.name = "sha1",
		.driver_name = "sha1-dx",
		.mac_name = "hmac(sha1)",
		.mac_driver_name = "hmac-sha1-dx",
		.blocksize = SHA1_BLOCK_SIZE,
		.synchronize = false,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = SHA1_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA1_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA1,
		.hw_mode = DRV_HASH_HW_SHA1,
		.inter_digestsize = SHA1_DIGEST_SIZE,
	},
	{
		.name = "sha256",
		.driver_name = "sha256-dx",
		.mac_name = "hmac(sha256)",
		.mac_driver_name = "hmac-sha256-dx",
		.blocksize = SHA256_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = SHA256_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA256_DIGEST_SIZE)
			},
		},
		.hash_mode = DRV_HASH_SHA256,
		.hw_mode = DRV_HASH_HW_SHA256,
		.inter_digestsize = SHA256_DIGEST_SIZE,
	},
	{
		.name = "sha224",
		.driver_name = "sha224-dx",
		.mac_name = "hmac(sha224)",
		.mac_driver_name = "hmac-sha224-dx",
		.blocksize = SHA224_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = SHA224_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA224_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA224,
		.hw_mode = DRV_HASH_HW_SHA256,
		.inter_digestsize = SHA256_DIGEST_SIZE,
	},
#if (DX_DEV_SHA_MAX > 256)
	{
		.name = "sha384",
		.driver_name = "sha384-dx",
		.mac_name = "hmac(sha384)",
		.mac_driver_name = "hmac-sha384-dx",
		.blocksize = SHA384_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = SHA384_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA384_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA384,
		.hw_mode = DRV_HASH_HW_SHA512,
		.inter_digestsize = SHA512_DIGEST_SIZE,
	},
	{
		.name = "sha512",
		.driver_name = "sha512-dx",
		.mac_name = "hmac(sha512)",
		.mac_driver_name = "hmac-sha512-dx",
		.blocksize = SHA512_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = SHA512_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(SHA512_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_SHA512,
		.hw_mode = DRV_HASH_HW_SHA512,
		.inter_digestsize = SHA512_DIGEST_SIZE,
	},
#endif
	{
		.name = "md5",
		.driver_name = "md5-dx",
		.mac_name = "hmac(md5)",
		.mac_driver_name = "hmac-md5-dx",
		.blocksize = MD5_HMAC_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_ahash_update,
			.final = ssi_ahash_final,
			.finup = ssi_ahash_finup,
			.digest = ssi_ahash_digest,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.setkey = ssi_ahash_setkey,
			.halg = {
				.digestsize = MD5_DIGEST_SIZE,
				.statesize = CC_STATE_SIZE(MD5_DIGEST_SIZE),
			},
		},
		.hash_mode = DRV_HASH_MD5,
		.hw_mode = DRV_HASH_HW_MD5,
		.inter_digestsize = MD5_DIGEST_SIZE,
	},
	{
		.mac_name = "xcbc(aes)",
		.mac_driver_name = "xcbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_mac_update,
			.final = ssi_mac_final,
			.finup = ssi_mac_finup,
			.digest = ssi_mac_digest,
			.setkey = ssi_xcbc_setkey,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = CC_STATE_SIZE(AES_BLOCK_SIZE),
			},
		},
		.hash_mode = DRV_HASH_NULL,
		.hw_mode = DRV_CIPHER_XCBC_MAC,
		.inter_digestsize = AES_BLOCK_SIZE,
	},
#if SSI_CC_HAS_CMAC
	{
		.mac_name = "cmac(aes)",
		.mac_driver_name = "cmac-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.template_ahash = {
			.init = ssi_ahash_init,
			.update = ssi_mac_update,
			.final = ssi_mac_final,
			.finup = ssi_mac_finup,
			.digest = ssi_mac_digest,
			.setkey = ssi_cmac_setkey,
			.export = ssi_ahash_export,
			.import = ssi_ahash_import,
			.halg = {
				.digestsize = AES_BLOCK_SIZE,
				.statesize = CC_STATE_SIZE(AES_BLOCK_SIZE),
			},
		},
		.hash_mode = DRV_HASH_NULL,
		.hw_mode = DRV_CIPHER_CMAC,
		.inter_digestsize = AES_BLOCK_SIZE,
	},
#endif

};

static struct ssi_hash_alg *
ssi_hash_create_alg(struct ssi_hash_template *template, struct device *dev,
		    bool keyed)
{
	struct ssi_hash_alg *t_crypto_alg;
	struct crypto_alg *alg;
	struct ahash_alg *halg;

	t_crypto_alg = kzalloc(sizeof(*t_crypto_alg), GFP_KERNEL);
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
	alg->cra_ctxsize = sizeof(struct ssi_hash_ctx);
	alg->cra_priority = SSI_CRA_PRIO;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_exit = ssi_hash_cra_exit;

	alg->cra_init = ssi_ahash_cra_init;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_TYPE_AHASH |
			CRYPTO_ALG_KERN_DRIVER_ONLY;
	alg->cra_type = &crypto_ahash_type;

	t_crypto_alg->hash_mode = template->hash_mode;
	t_crypto_alg->hw_mode = template->hw_mode;
	t_crypto_alg->inter_digestsize = template->inter_digestsize;

	return t_crypto_alg;
}

int ssi_hash_init_sram_digest_consts(struct ssi_drvdata *drvdata)
{
	struct ssi_hash_handle *hash_handle = drvdata->hash_handle;
	ssi_sram_addr_t sram_buff_ofs = hash_handle->digest_len_sram_addr;
	unsigned int larval_seq_len = 0;
	struct cc_hw_desc larval_seq[CC_DIGEST_SIZE_MAX / sizeof(u32)];
	struct device *dev = drvdata_to_dev(drvdata);
	int rc = 0;
#if (DX_DEV_SHA_MAX > 256)
	int i;
#endif

	/* Copy-to-sram digest-len */
	cc_set_sram_desc(digest_len_init, sram_buff_ofs,
			 ARRAY_SIZE(digest_len_init), larval_seq,
			 &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;

	sram_buff_ofs += sizeof(digest_len_init);
	larval_seq_len = 0;

#if (DX_DEV_SHA_MAX > 256)
	/* Copy-to-sram digest-len for sha384/512 */
	cc_set_sram_desc(digest_len_sha512_init, sram_buff_ofs,
			 ARRAY_SIZE(digest_len_sha512_init),
			 larval_seq, &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;

	sram_buff_ofs += sizeof(digest_len_sha512_init);
	larval_seq_len = 0;
#endif

	/* The initial digests offset */
	hash_handle->larval_digest_sram_addr = sram_buff_ofs;

	/* Copy-to-sram initial SHA* digests */
	cc_set_sram_desc(md5_init, sram_buff_ofs,
			 ARRAY_SIZE(md5_init), larval_seq,
			 &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;
	sram_buff_ofs += sizeof(md5_init);
	larval_seq_len = 0;

	cc_set_sram_desc(sha1_init, sram_buff_ofs,
			 ARRAY_SIZE(sha1_init), larval_seq,
			 &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;
	sram_buff_ofs += sizeof(sha1_init);
	larval_seq_len = 0;

	cc_set_sram_desc(sha224_init, sram_buff_ofs,
			 ARRAY_SIZE(sha224_init), larval_seq,
			 &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;
	sram_buff_ofs += sizeof(sha224_init);
	larval_seq_len = 0;

	cc_set_sram_desc(sha256_init, sram_buff_ofs,
			 ARRAY_SIZE(sha256_init), larval_seq,
			 &larval_seq_len);
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc))
		goto init_digest_const_err;
	sram_buff_ofs += sizeof(sha256_init);
	larval_seq_len = 0;

#if (DX_DEV_SHA_MAX > 256)
	/* We are forced to swap each double-word larval before copying to
	 * sram
	 */
	for (i = 0; i < ARRAY_SIZE(sha384_init); i++) {
		const u32 const0 = ((u32 *)((u64 *)&sha384_init[i]))[1];
		const u32 const1 = ((u32 *)((u64 *)&sha384_init[i]))[0];

		cc_set_sram_desc(&const0, sram_buff_ofs, 1, larval_seq,
				 &larval_seq_len);
		sram_buff_ofs += sizeof(u32);
		cc_set_sram_desc(&const1, sram_buff_ofs, 1, larval_seq,
				 &larval_seq_len);
		sram_buff_ofs += sizeof(u32);
	}
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc)) {
		dev_err(dev, "send_request() failed (rc = %d)\n", rc);
		goto init_digest_const_err;
	}
	larval_seq_len = 0;

	for (i = 0; i < ARRAY_SIZE(sha512_init); i++) {
		const u32 const0 = ((u32 *)((u64 *)&sha512_init[i]))[1];
		const u32 const1 = ((u32 *)((u64 *)&sha512_init[i]))[0];

		cc_set_sram_desc(&const0, sram_buff_ofs, 1, larval_seq,
				 &larval_seq_len);
		sram_buff_ofs += sizeof(u32);
		cc_set_sram_desc(&const1, sram_buff_ofs, 1, larval_seq,
				 &larval_seq_len);
		sram_buff_ofs += sizeof(u32);
	}
	rc = send_request_init(drvdata, larval_seq, larval_seq_len);
	if (unlikely(rc)) {
		dev_err(dev, "send_request() failed (rc = %d)\n", rc);
		goto init_digest_const_err;
	}
#endif

init_digest_const_err:
	return rc;
}

int ssi_hash_alloc(struct ssi_drvdata *drvdata)
{
	struct ssi_hash_handle *hash_handle;
	ssi_sram_addr_t sram_buff;
	u32 sram_size_to_alloc;
	struct device *dev = drvdata_to_dev(drvdata);
	int rc = 0;
	int alg;

	hash_handle = kzalloc(sizeof(*hash_handle), GFP_KERNEL);
	if (!hash_handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&hash_handle->hash_list);
	drvdata->hash_handle = hash_handle;

	sram_size_to_alloc = sizeof(digest_len_init) +
#if (DX_DEV_SHA_MAX > 256)
			sizeof(digest_len_sha512_init) +
			sizeof(sha384_init) +
			sizeof(sha512_init) +
#endif
			sizeof(md5_init) +
			sizeof(sha1_init) +
			sizeof(sha224_init) +
			sizeof(sha256_init);

	sram_buff = cc_sram_alloc(drvdata, sram_size_to_alloc);
	if (sram_buff == NULL_SRAM_ADDR) {
		dev_err(dev, "SRAM pool exhausted\n");
		rc = -ENOMEM;
		goto fail;
	}

	/* The initial digest-len offset */
	hash_handle->digest_len_sram_addr = sram_buff;

	/*must be set before the alg registration as it is being used there*/
	rc = ssi_hash_init_sram_digest_consts(drvdata);
	if (unlikely(rc)) {
		dev_err(dev, "Init digest CONST failed (rc=%d)\n", rc);
		goto fail;
	}

	/* ahash registration */
	for (alg = 0; alg < ARRAY_SIZE(driver_hash); alg++) {
		struct ssi_hash_alg *t_alg;
		int hw_mode = driver_hash[alg].hw_mode;

		/* register hmac version */
		t_alg = ssi_hash_create_alg(&driver_hash[alg], dev, true);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			dev_err(dev, "%s alg allocation failed\n",
				driver_hash[alg].driver_name);
			goto fail;
		}
		t_alg->drvdata = drvdata;

		rc = crypto_register_ahash(&t_alg->ahash_alg);
		if (unlikely(rc)) {
			dev_err(dev, "%s alg registration failed\n",
				driver_hash[alg].driver_name);
			kfree(t_alg);
			goto fail;
		} else {
			list_add_tail(&t_alg->entry,
				      &hash_handle->hash_list);
		}

		if (hw_mode == DRV_CIPHER_XCBC_MAC ||
		    hw_mode == DRV_CIPHER_CMAC)
			continue;

		/* register hash version */
		t_alg = ssi_hash_create_alg(&driver_hash[alg], dev, false);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			dev_err(dev, "%s alg allocation failed\n",
				driver_hash[alg].driver_name);
			goto fail;
		}
		t_alg->drvdata = drvdata;

		rc = crypto_register_ahash(&t_alg->ahash_alg);
		if (unlikely(rc)) {
			dev_err(dev, "%s alg registration failed\n",
				driver_hash[alg].driver_name);
			kfree(t_alg);
			goto fail;
		} else {
			list_add_tail(&t_alg->entry, &hash_handle->hash_list);
		}
	}

	return 0;

fail:
	kfree(drvdata->hash_handle);
	drvdata->hash_handle = NULL;
	return rc;
}

int ssi_hash_free(struct ssi_drvdata *drvdata)
{
	struct ssi_hash_alg *t_hash_alg, *hash_n;
	struct ssi_hash_handle *hash_handle = drvdata->hash_handle;

	if (hash_handle) {
		list_for_each_entry_safe(t_hash_alg, hash_n,
					 &hash_handle->hash_list, entry) {
			crypto_unregister_ahash(&t_hash_alg->ahash_alg);
			list_del(&t_hash_alg->entry);
			kfree(t_hash_alg);
		}

		kfree(hash_handle);
		drvdata->hash_handle = NULL;
	}
	return 0;
}

static void ssi_hash_create_xcbc_setup(struct ahash_request *areq,
				       struct cc_hw_desc desc[],
				       unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct ahash_req_ctx *state = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	/* Setup XCBC MAC K1 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, (ctx->opad_tmp_keys_dma_addr +
					    XCBC_MAC_K1_OFFSET),
		     CC_AES_128_BIT_KEY_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Setup XCBC MAC K2 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, (ctx->opad_tmp_keys_dma_addr +
					    XCBC_MAC_K2_OFFSET),
		     CC_AES_128_BIT_KEY_SIZE, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Setup XCBC MAC K3 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, (ctx->opad_tmp_keys_dma_addr +
					    XCBC_MAC_K3_OFFSET),
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

static void ssi_hash_create_cmac_setup(struct ahash_request *areq,
				       struct cc_hw_desc desc[],
				       unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct ahash_req_ctx *state = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ssi_hash_ctx *ctx = crypto_ahash_ctx(tfm);

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

static void ssi_hash_create_data_desc(struct ahash_req_ctx *areq_ctx,
				      struct ssi_hash_ctx *ctx,
				      unsigned int flow_mode,
				      struct cc_hw_desc desc[],
				      bool is_not_last_data,
				      unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	if (likely(areq_ctx->data_dma_buf_type == SSI_DMA_BUF_DLLI)) {
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     sg_dma_address(areq_ctx->curr_sg),
			     areq_ctx->curr_sg->length, NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		idx++;
	} else {
		if (areq_ctx->data_dma_buf_type == SSI_DMA_BUF_NULL) {
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

/*!
 * Gets the address of the initial digest in SRAM
 * according to the given hash mode
 *
 * \param drvdata
 * \param mode The Hash mode. Supported modes: MD5/SHA1/SHA224/SHA256
 *
 * \return u32 The address of the initial digest in SRAM
 */
ssi_sram_addr_t cc_larval_digest_addr(void *drvdata, u32 mode)
{
	struct ssi_drvdata *_drvdata = (struct ssi_drvdata *)drvdata;
	struct ssi_hash_handle *hash_handle = _drvdata->hash_handle;
	struct device *dev = drvdata_to_dev(_drvdata);

	switch (mode) {
	case DRV_HASH_NULL:
		break; /*Ignore*/
	case DRV_HASH_MD5:
		return (hash_handle->larval_digest_sram_addr);
	case DRV_HASH_SHA1:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(md5_init));
	case DRV_HASH_SHA224:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(md5_init) +
			sizeof(sha1_init));
	case DRV_HASH_SHA256:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(md5_init) +
			sizeof(sha1_init) +
			sizeof(sha224_init));
#if (DX_DEV_SHA_MAX > 256)
	case DRV_HASH_SHA384:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(md5_init) +
			sizeof(sha1_init) +
			sizeof(sha224_init) +
			sizeof(sha256_init));
	case DRV_HASH_SHA512:
		return (hash_handle->larval_digest_sram_addr +
			sizeof(md5_init) +
			sizeof(sha1_init) +
			sizeof(sha224_init) +
			sizeof(sha256_init) +
			sizeof(sha384_init));
#endif
	default:
		dev_err(dev, "Invalid hash mode (%d)\n", mode);
	}

	/*This is valid wrong value to avoid kernel crash*/
	return hash_handle->larval_digest_sram_addr;
}

ssi_sram_addr_t
ssi_ahash_get_initial_digest_len_sram_addr(void *drvdata, u32 mode)
{
	struct ssi_drvdata *_drvdata = (struct ssi_drvdata *)drvdata;
	struct ssi_hash_handle *hash_handle = _drvdata->hash_handle;
	ssi_sram_addr_t digest_len_addr = hash_handle->digest_len_sram_addr;

	switch (mode) {
	case DRV_HASH_SHA1:
	case DRV_HASH_SHA224:
	case DRV_HASH_SHA256:
	case DRV_HASH_MD5:
		return digest_len_addr;
#if (DX_DEV_SHA_MAX > 256)
	case DRV_HASH_SHA384:
	case DRV_HASH_SHA512:
		return  digest_len_addr + sizeof(digest_len_init);
#endif
	default:
		return digest_len_addr; /*to avoid kernel crash*/
	}
}

