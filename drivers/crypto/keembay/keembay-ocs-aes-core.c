// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay OCS AES Crypto Driver.
 *
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>

#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>

#include "ocs-aes.h"

#define KMB_OCS_PRIORITY	350
#define DRV_NAME		"keembay-ocs-aes"

#define OCS_AES_MIN_KEY_SIZE	16
#define OCS_AES_MAX_KEY_SIZE	32
#define OCS_AES_KEYSIZE_128	16
#define OCS_AES_KEYSIZE_192	24
#define OCS_AES_KEYSIZE_256	32
#define OCS_SM4_KEY_SIZE	16

/**
 * struct ocs_aes_tctx - OCS AES Transform context
 * @engine_ctx:		Engine context.
 * @aes_dev:		The OCS AES device.
 * @key:		AES/SM4 key.
 * @key_len:		The length (in bytes) of @key.
 * @cipher:		OCS cipher to use (either AES or SM4).
 * @sw_cipher:		The cipher to use as fallback.
 * @use_fallback:	Whether or not fallback cipher should be used.
 */
struct ocs_aes_tctx {
	struct crypto_engine_ctx engine_ctx;
	struct ocs_aes_dev *aes_dev;
	u8 key[OCS_AES_KEYSIZE_256];
	unsigned int key_len;
	enum ocs_cipher cipher;
	union {
		struct crypto_sync_skcipher *sk;
		struct crypto_aead *aead;
	} sw_cipher;
	bool use_fallback;
};

/**
 * struct ocs_aes_rctx - OCS AES Request context.
 * @instruction:	Instruction to be executed (encrypt / decrypt).
 * @mode:		Mode to use (ECB, CBC, CTR, CCm, GCM, CTS)
 * @src_nents:		Number of source SG entries.
 * @dst_nents:		Number of destination SG entries.
 * @src_dma_count:	The number of DMA-mapped entries of the source SG.
 * @dst_dma_count:	The number of DMA-mapped entries of the destination SG.
 * @in_place:		Whether or not this is an in place request, i.e.,
 *			src_sg == dst_sg.
 * @src_dll:		OCS DMA linked list for input data.
 * @dst_dll:		OCS DMA linked list for output data.
 * @last_ct_blk:	Buffer to hold last cipher text block (only used in CBC
 *			mode).
 * @cts_swap:		Whether or not CTS swap must be performed.
 * @aad_src_dll:	OCS DMA linked list for input AAD data.
 * @aad_dst_dll:	OCS DMA linked list for output AAD data.
 * @in_tag:		Buffer to hold input encrypted tag (only used for
 *			CCM/GCM decrypt).
 * @out_tag:		Buffer to hold output encrypted / decrypted tag (only
 *			used for GCM encrypt / decrypt).
 */
struct ocs_aes_rctx {
	/* Fields common across all modes. */
	enum ocs_instruction	instruction;
	enum ocs_mode		mode;
	int			src_nents;
	int			dst_nents;
	int			src_dma_count;
	int			dst_dma_count;
	bool			in_place;
	struct ocs_dll_desc	src_dll;
	struct ocs_dll_desc	dst_dll;

	/* CBC specific */
	u8			last_ct_blk[AES_BLOCK_SIZE];

	/* CTS specific */
	int			cts_swap;

	/* CCM/GCM specific */
	struct ocs_dll_desc	aad_src_dll;
	struct ocs_dll_desc	aad_dst_dll;
	u8			in_tag[AES_BLOCK_SIZE];

	/* GCM specific */
	u8			out_tag[AES_BLOCK_SIZE];
};

/* Driver data. */
struct ocs_aes_drv {
	struct list_head dev_list;
	spinlock_t lock;	/* Protects dev_list. */
};

static struct ocs_aes_drv ocs_aes = {
	.dev_list = LIST_HEAD_INIT(ocs_aes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(ocs_aes.lock),
};

static struct ocs_aes_dev *kmb_ocs_aes_find_dev(struct ocs_aes_tctx *tctx)
{
	struct ocs_aes_dev *aes_dev;

	spin_lock(&ocs_aes.lock);

	if (tctx->aes_dev) {
		aes_dev = tctx->aes_dev;
		goto exit;
	}

	/* Only a single OCS device available */
	aes_dev = list_first_entry(&ocs_aes.dev_list, struct ocs_aes_dev, list);
	tctx->aes_dev = aes_dev;

exit:
	spin_unlock(&ocs_aes.lock);

	return aes_dev;
}

/*
 * Ensure key is 128-bit or 256-bit for AES or 128-bit for SM4 and an actual
 * key is being passed in.
 *
 * Return: 0 if key is valid, -EINVAL otherwise.
 */
static int check_key(const u8 *in_key, size_t key_len, enum ocs_cipher cipher)
{
	if (!in_key)
		return -EINVAL;

	/* For AES, only 128-byte or 256-byte keys are supported. */
	if (cipher == OCS_AES && (key_len == OCS_AES_KEYSIZE_128 ||
				  key_len == OCS_AES_KEYSIZE_256))
		return 0;

	/* For SM4, only 128-byte keys are supported. */
	if (cipher == OCS_SM4 && key_len == OCS_AES_KEYSIZE_128)
		return 0;

	/* Everything else is unsupported. */
	return -EINVAL;
}

/* Save key into transformation context. */
static int save_key(struct ocs_aes_tctx *tctx, const u8 *in_key, size_t key_len,
		    enum ocs_cipher cipher)
{
	int ret;

	ret = check_key(in_key, key_len, cipher);
	if (ret)
		return ret;

	memcpy(tctx->key, in_key, key_len);
	tctx->key_len = key_len;
	tctx->cipher = cipher;

	return 0;
}

/* Set key for symmetric cypher. */
static int kmb_ocs_sk_set_key(struct crypto_skcipher *tfm, const u8 *in_key,
			      size_t key_len, enum ocs_cipher cipher)
{
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);

	/* Fallback is used for AES with 192-bit key. */
	tctx->use_fallback = (cipher == OCS_AES &&
			      key_len == OCS_AES_KEYSIZE_192);

	if (!tctx->use_fallback)
		return save_key(tctx, in_key, key_len, cipher);

	crypto_sync_skcipher_clear_flags(tctx->sw_cipher.sk,
					 CRYPTO_TFM_REQ_MASK);
	crypto_sync_skcipher_set_flags(tctx->sw_cipher.sk,
				       tfm->base.crt_flags &
				       CRYPTO_TFM_REQ_MASK);

	return crypto_sync_skcipher_setkey(tctx->sw_cipher.sk, in_key, key_len);
}

/* Set key for AEAD cipher. */
static int kmb_ocs_aead_set_key(struct crypto_aead *tfm, const u8 *in_key,
				size_t key_len, enum ocs_cipher cipher)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(tfm);

	/* Fallback is used for AES with 192-bit key. */
	tctx->use_fallback = (cipher == OCS_AES &&
			      key_len == OCS_AES_KEYSIZE_192);

	if (!tctx->use_fallback)
		return save_key(tctx, in_key, key_len, cipher);

	crypto_aead_clear_flags(tctx->sw_cipher.aead, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(tctx->sw_cipher.aead,
			      crypto_aead_get_flags(tfm) & CRYPTO_TFM_REQ_MASK);

	return crypto_aead_setkey(tctx->sw_cipher.aead, in_key, key_len);
}

/* Swap two AES blocks in SG lists. */
static void sg_swap_blocks(struct scatterlist *sgl, unsigned int nents,
			   off_t blk1_offset, off_t blk2_offset)
{
	u8 tmp_buf1[AES_BLOCK_SIZE], tmp_buf2[AES_BLOCK_SIZE];

	/*
	 * No easy way to copy within sg list, so copy both blocks to temporary
	 * buffers first.
	 */
	sg_pcopy_to_buffer(sgl, nents, tmp_buf1, AES_BLOCK_SIZE, blk1_offset);
	sg_pcopy_to_buffer(sgl, nents, tmp_buf2, AES_BLOCK_SIZE, blk2_offset);
	sg_pcopy_from_buffer(sgl, nents, tmp_buf1, AES_BLOCK_SIZE, blk2_offset);
	sg_pcopy_from_buffer(sgl, nents, tmp_buf2, AES_BLOCK_SIZE, blk1_offset);
}

/* Initialize request context to default values. */
static void ocs_aes_init_rctx(struct ocs_aes_rctx *rctx)
{
	/* Zero everything. */
	memset(rctx, 0, sizeof(*rctx));

	/* Set initial value for DMA addresses. */
	rctx->src_dll.dma_addr = DMA_MAPPING_ERROR;
	rctx->dst_dll.dma_addr = DMA_MAPPING_ERROR;
	rctx->aad_src_dll.dma_addr = DMA_MAPPING_ERROR;
	rctx->aad_dst_dll.dma_addr = DMA_MAPPING_ERROR;
}

static int kmb_ocs_sk_validate_input(struct skcipher_request *req,
				     enum ocs_mode mode)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	int iv_size = crypto_skcipher_ivsize(tfm);

	switch (mode) {
	case OCS_MODE_ECB:
		/* Ensure input length is multiple of block size */
		if (req->cryptlen % AES_BLOCK_SIZE != 0)
			return -EINVAL;

		return 0;

	case OCS_MODE_CBC:
		/* Ensure input length is multiple of block size */
		if (req->cryptlen % AES_BLOCK_SIZE != 0)
			return -EINVAL;

		/* Ensure IV is present and block size in length */
		if (!req->iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;
		/*
		 * NOTE: Since req->cryptlen == 0 case was already handled in
		 * kmb_ocs_sk_common(), the above two conditions also guarantee
		 * that: cryptlen >= iv_size
		 */
		return 0;

	case OCS_MODE_CTR:
		/* Ensure IV is present and block size in length */
		if (!req->iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;
		return 0;

	case OCS_MODE_CTS:
		/* Ensure input length >= block size */
		if (req->cryptlen < AES_BLOCK_SIZE)
			return -EINVAL;

		/* Ensure IV is present and block size in length */
		if (!req->iv || iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * Called by encrypt() / decrypt() skcipher functions.
 *
 * Use fallback if needed, otherwise initialize context and enqueue request
 * into engine.
 */
static int kmb_ocs_sk_common(struct skcipher_request *req,
			     enum ocs_cipher cipher,
			     enum ocs_instruction instruction,
			     enum ocs_mode mode)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_rctx *rctx = skcipher_request_ctx(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	struct ocs_aes_dev *aes_dev;
	int rc;

	if (tctx->use_fallback) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(subreq, tctx->sw_cipher.sk);

		skcipher_request_set_sync_tfm(subreq, tctx->sw_cipher.sk);
		skcipher_request_set_callback(subreq, req->base.flags, NULL,
					      NULL);
		skcipher_request_set_crypt(subreq, req->src, req->dst,
					   req->cryptlen, req->iv);

		if (instruction == OCS_ENCRYPT)
			rc = crypto_skcipher_encrypt(subreq);
		else
			rc = crypto_skcipher_decrypt(subreq);

		skcipher_request_zero(subreq);

		return rc;
	}

	/*
	 * If cryptlen == 0, no processing needed for ECB, CBC and CTR.
	 *
	 * For CTS continue: kmb_ocs_sk_validate_input() will return -EINVAL.
	 */
	if (!req->cryptlen && mode != OCS_MODE_CTS)
		return 0;

	rc = kmb_ocs_sk_validate_input(req, mode);
	if (rc)
		return rc;

	aes_dev = kmb_ocs_aes_find_dev(tctx);
	if (!aes_dev)
		return -ENODEV;

	if (cipher != tctx->cipher)
		return -EINVAL;

	ocs_aes_init_rctx(rctx);
	rctx->instruction = instruction;
	rctx->mode = mode;

	return crypto_transfer_skcipher_request_to_engine(aes_dev->engine, req);
}

static void cleanup_ocs_dma_linked_list(struct device *dev,
					struct ocs_dll_desc *dll)
{
	if (dll->vaddr)
		dma_free_coherent(dev, dll->size, dll->vaddr, dll->dma_addr);
	dll->vaddr = NULL;
	dll->size = 0;
	dll->dma_addr = DMA_MAPPING_ERROR;
}

static void kmb_ocs_sk_dma_cleanup(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_rctx *rctx = skcipher_request_ctx(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	struct device *dev = tctx->aes_dev->dev;

	if (rctx->src_dma_count) {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		rctx->src_dma_count = 0;
	}

	if (rctx->dst_dma_count) {
		dma_unmap_sg(dev, req->dst, rctx->dst_nents, rctx->in_place ?
							     DMA_BIDIRECTIONAL :
							     DMA_FROM_DEVICE);
		rctx->dst_dma_count = 0;
	}

	/* Clean up OCS DMA linked lists */
	cleanup_ocs_dma_linked_list(dev, &rctx->src_dll);
	cleanup_ocs_dma_linked_list(dev, &rctx->dst_dll);
}

static int kmb_ocs_sk_prepare_inplace(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_rctx *rctx = skcipher_request_ctx(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	int iv_size = crypto_skcipher_ivsize(tfm);
	int rc;

	/*
	 * For CBC decrypt, save last block (iv) to last_ct_blk buffer.
	 *
	 * Note: if we are here, we already checked that cryptlen >= iv_size
	 * and iv_size == AES_BLOCK_SIZE (i.e., the size of last_ct_blk); see
	 * kmb_ocs_sk_validate_input().
	 */
	if (rctx->mode == OCS_MODE_CBC && rctx->instruction == OCS_DECRYPT)
		scatterwalk_map_and_copy(rctx->last_ct_blk, req->src,
					 req->cryptlen - iv_size, iv_size, 0);

	/* For CTS decrypt, swap last two blocks, if needed. */
	if (rctx->cts_swap && rctx->instruction == OCS_DECRYPT)
		sg_swap_blocks(req->dst, rctx->dst_nents,
			       req->cryptlen - AES_BLOCK_SIZE,
			       req->cryptlen - (2 * AES_BLOCK_SIZE));

	/* src and dst buffers are the same, use bidirectional DMA mapping. */
	rctx->dst_dma_count = dma_map_sg(tctx->aes_dev->dev, req->dst,
					 rctx->dst_nents, DMA_BIDIRECTIONAL);
	if (rctx->dst_dma_count == 0) {
		dev_err(tctx->aes_dev->dev, "Failed to map destination sg\n");
		return -ENOMEM;
	}

	/* Create DST linked list */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
					    rctx->dst_dma_count, &rctx->dst_dll,
					    req->cryptlen, 0);
	if (rc)
		return rc;
	/*
	 * If descriptor creation was successful, set the src_dll.dma_addr to
	 * the value of dst_dll.dma_addr, as we do in-place AES operation on
	 * the src.
	 */
	rctx->src_dll.dma_addr = rctx->dst_dll.dma_addr;

	return 0;
}

static int kmb_ocs_sk_prepare_notinplace(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_rctx *rctx = skcipher_request_ctx(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	int rc;

	rctx->src_nents =  sg_nents_for_len(req->src, req->cryptlen);
	if (rctx->src_nents < 0)
		return -EBADMSG;

	/* Map SRC SG. */
	rctx->src_dma_count = dma_map_sg(tctx->aes_dev->dev, req->src,
					 rctx->src_nents, DMA_TO_DEVICE);
	if (rctx->src_dma_count == 0) {
		dev_err(tctx->aes_dev->dev, "Failed to map source sg\n");
		return -ENOMEM;
	}

	/* Create SRC linked list */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->src,
					    rctx->src_dma_count, &rctx->src_dll,
					    req->cryptlen, 0);
	if (rc)
		return rc;

	/* Map DST SG. */
	rctx->dst_dma_count = dma_map_sg(tctx->aes_dev->dev, req->dst,
					 rctx->dst_nents, DMA_FROM_DEVICE);
	if (rctx->dst_dma_count == 0) {
		dev_err(tctx->aes_dev->dev, "Failed to map destination sg\n");
		return -ENOMEM;
	}

	/* Create DST linked list */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
					    rctx->dst_dma_count, &rctx->dst_dll,
					    req->cryptlen, 0);
	if (rc)
		return rc;

	/* If this is not a CTS decrypt operation with swapping, we are done. */
	if (!(rctx->cts_swap && rctx->instruction == OCS_DECRYPT))
		return 0;

	/*
	 * Otherwise, we have to copy src to dst (as we cannot modify src).
	 * Use OCS AES bypass mode to copy src to dst via DMA.
	 *
	 * NOTE: for anything other than small data sizes this is rather
	 * inefficient.
	 */
	rc = ocs_aes_bypass_op(tctx->aes_dev, rctx->dst_dll.dma_addr,
			       rctx->src_dll.dma_addr, req->cryptlen);
	if (rc)
		return rc;

	/*
	 * Now dst == src, so clean up what we did so far and use in_place
	 * logic.
	 */
	kmb_ocs_sk_dma_cleanup(req);
	rctx->in_place = true;

	return kmb_ocs_sk_prepare_inplace(req);
}

static int kmb_ocs_sk_run(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_rctx *rctx = skcipher_request_ctx(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	struct ocs_aes_dev *aes_dev = tctx->aes_dev;
	int iv_size = crypto_skcipher_ivsize(tfm);
	int rc;

	rctx->dst_nents = sg_nents_for_len(req->dst, req->cryptlen);
	if (rctx->dst_nents < 0)
		return -EBADMSG;

	/*
	 * If 2 blocks or greater, and multiple of block size swap last two
	 * blocks to be compatible with other crypto API CTS implementations:
	 * OCS mode uses CBC-CS2, whereas other crypto API implementations use
	 * CBC-CS3.
	 * CBC-CS2 and CBC-CS3 defined by:
	 * https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a-add.pdf
	 */
	rctx->cts_swap = (rctx->mode == OCS_MODE_CTS &&
			  req->cryptlen > AES_BLOCK_SIZE &&
			  req->cryptlen % AES_BLOCK_SIZE == 0);

	rctx->in_place = (req->src == req->dst);

	if (rctx->in_place)
		rc = kmb_ocs_sk_prepare_inplace(req);
	else
		rc = kmb_ocs_sk_prepare_notinplace(req);

	if (rc)
		goto error;

	rc = ocs_aes_op(aes_dev, rctx->mode, tctx->cipher, rctx->instruction,
			rctx->dst_dll.dma_addr, rctx->src_dll.dma_addr,
			req->cryptlen, req->iv, iv_size);
	if (rc)
		goto error;

	/* Clean-up DMA before further processing output. */
	kmb_ocs_sk_dma_cleanup(req);

	/* For CTS Encrypt, swap last 2 blocks, if needed. */
	if (rctx->cts_swap && rctx->instruction == OCS_ENCRYPT) {
		sg_swap_blocks(req->dst, rctx->dst_nents,
			       req->cryptlen - AES_BLOCK_SIZE,
			       req->cryptlen - (2 * AES_BLOCK_SIZE));
		return 0;
	}

	/* For CBC copy IV to req->IV. */
	if (rctx->mode == OCS_MODE_CBC) {
		/* CBC encrypt case. */
		if (rctx->instruction == OCS_ENCRYPT) {
			scatterwalk_map_and_copy(req->iv, req->dst,
						 req->cryptlen - iv_size,
						 iv_size, 0);
			return 0;
		}
		/* CBC decrypt case. */
		if (rctx->in_place)
			memcpy(req->iv, rctx->last_ct_blk, iv_size);
		else
			scatterwalk_map_and_copy(req->iv, req->src,
						 req->cryptlen - iv_size,
						 iv_size, 0);
		return 0;
	}
	/* For all other modes there's nothing to do. */

	return 0;

error:
	kmb_ocs_sk_dma_cleanup(req);

	return rc;
}

static int kmb_ocs_aead_validate_input(struct aead_request *req,
				       enum ocs_instruction instruction,
				       enum ocs_mode mode)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	int tag_size = crypto_aead_authsize(tfm);
	int iv_size = crypto_aead_ivsize(tfm);

	/* For decrypt crytplen == len(PT) + len(tag). */
	if (instruction == OCS_DECRYPT && req->cryptlen < tag_size)
		return -EINVAL;

	/* IV is mandatory. */
	if (!req->iv)
		return -EINVAL;

	switch (mode) {
	case OCS_MODE_GCM:
		if (iv_size != GCM_AES_IV_SIZE)
			return -EINVAL;

		return 0;

	case OCS_MODE_CCM:
		/* Ensure IV is present and block size in length */
		if (iv_size != AES_BLOCK_SIZE)
			return -EINVAL;

		return 0;

	default:
		return -EINVAL;
	}
}

/*
 * Called by encrypt() / decrypt() aead functions.
 *
 * Use fallback if needed, otherwise initialize context and enqueue request
 * into engine.
 */
static int kmb_ocs_aead_common(struct aead_request *req,
			       enum ocs_cipher cipher,
			       enum ocs_instruction instruction,
			       enum ocs_mode mode)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct ocs_aes_rctx *rctx = aead_request_ctx(req);
	struct ocs_aes_dev *dd;
	int rc;

	if (tctx->use_fallback) {
		struct aead_request *subreq = aead_request_ctx(req);

		aead_request_set_tfm(subreq, tctx->sw_cipher.aead);
		aead_request_set_callback(subreq, req->base.flags,
					  req->base.complete, req->base.data);
		aead_request_set_crypt(subreq, req->src, req->dst,
				       req->cryptlen, req->iv);
		aead_request_set_ad(subreq, req->assoclen);
		rc = crypto_aead_setauthsize(tctx->sw_cipher.aead,
					     crypto_aead_authsize(crypto_aead_reqtfm(req)));
		if (rc)
			return rc;

		return (instruction == OCS_ENCRYPT) ?
		       crypto_aead_encrypt(subreq) :
		       crypto_aead_decrypt(subreq);
	}

	rc = kmb_ocs_aead_validate_input(req, instruction, mode);
	if (rc)
		return rc;

	dd = kmb_ocs_aes_find_dev(tctx);
	if (!dd)
		return -ENODEV;

	if (cipher != tctx->cipher)
		return -EINVAL;

	ocs_aes_init_rctx(rctx);
	rctx->instruction = instruction;
	rctx->mode = mode;

	return crypto_transfer_aead_request_to_engine(dd->engine, req);
}

static void kmb_ocs_aead_dma_cleanup(struct aead_request *req)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct ocs_aes_rctx *rctx = aead_request_ctx(req);
	struct device *dev = tctx->aes_dev->dev;

	if (rctx->src_dma_count) {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		rctx->src_dma_count = 0;
	}

	if (rctx->dst_dma_count) {
		dma_unmap_sg(dev, req->dst, rctx->dst_nents, rctx->in_place ?
							     DMA_BIDIRECTIONAL :
							     DMA_FROM_DEVICE);
		rctx->dst_dma_count = 0;
	}
	/* Clean up OCS DMA linked lists */
	cleanup_ocs_dma_linked_list(dev, &rctx->src_dll);
	cleanup_ocs_dma_linked_list(dev, &rctx->dst_dll);
	cleanup_ocs_dma_linked_list(dev, &rctx->aad_src_dll);
	cleanup_ocs_dma_linked_list(dev, &rctx->aad_dst_dll);
}

/**
 * kmb_ocs_aead_dma_prepare() - Do DMA mapping for AEAD processing.
 * @req:		The AEAD request being processed.
 * @src_dll_size:	Where to store the length of the data mapped into the
 *			src_dll OCS DMA list.
 *
 * Do the following:
 * - DMA map req->src and req->dst
 * - Initialize the following OCS DMA linked lists: rctx->src_dll,
 *   rctx->dst_dll, rctx->aad_src_dll and rxtc->aad_dst_dll.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int kmb_ocs_aead_dma_prepare(struct aead_request *req, u32 *src_dll_size)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	const int tag_size = crypto_aead_authsize(crypto_aead_reqtfm(req));
	struct ocs_aes_rctx *rctx = aead_request_ctx(req);
	u32 in_size;	/* The length of the data to be mapped by src_dll. */
	u32 out_size;	/* The length of the data to be mapped by dst_dll. */
	u32 dst_size;	/* The length of the data in dst_sg. */
	int rc;

	/* Get number of entries in input data SG list. */
	rctx->src_nents = sg_nents_for_len(req->src,
					   req->assoclen + req->cryptlen);
	if (rctx->src_nents < 0)
		return -EBADMSG;

	if (rctx->instruction == OCS_DECRYPT) {
		/*
		 * For decrypt:
		 * - src sg list is:		AAD|CT|tag
		 * - dst sg list expects:	AAD|PT
		 *
		 * in_size == len(CT); out_size == len(PT)
		 */

		/* req->cryptlen includes both CT and tag. */
		in_size = req->cryptlen - tag_size;

		/* out_size = PT size == CT size */
		out_size = in_size;

		/* len(dst_sg) == len(AAD) + len(PT) */
		dst_size = req->assoclen + out_size;

		/*
		 * Copy tag from source SG list to 'in_tag' buffer.
		 *
		 * Note: this needs to be done here, before DMA mapping src_sg.
		 */
		sg_pcopy_to_buffer(req->src, rctx->src_nents, rctx->in_tag,
				   tag_size, req->assoclen + in_size);

	} else { /* OCS_ENCRYPT */
		/*
		 * For encrypt:
		 *	src sg list is:		AAD|PT
		 *	dst sg list expects:	AAD|CT|tag
		 */
		/* in_size == len(PT) */
		in_size = req->cryptlen;

		/*
		 * In CCM mode the OCS engine appends the tag to the ciphertext,
		 * but in GCM mode the tag must be read from the tag registers
		 * and appended manually below
		 */
		out_size = (rctx->mode == OCS_MODE_CCM) ? in_size + tag_size :
							  in_size;
		/* len(dst_sg) == len(AAD) + len(CT) + len(tag) */
		dst_size = req->assoclen + in_size + tag_size;
	}
	*src_dll_size = in_size;

	/* Get number of entries in output data SG list. */
	rctx->dst_nents = sg_nents_for_len(req->dst, dst_size);
	if (rctx->dst_nents < 0)
		return -EBADMSG;

	rctx->in_place = (req->src == req->dst) ? 1 : 0;

	/* Map destination; use bidirectional mapping for in-place case. */
	rctx->dst_dma_count = dma_map_sg(tctx->aes_dev->dev, req->dst,
					 rctx->dst_nents,
					 rctx->in_place ? DMA_BIDIRECTIONAL :
							  DMA_FROM_DEVICE);
	if (rctx->dst_dma_count == 0 && rctx->dst_nents != 0) {
		dev_err(tctx->aes_dev->dev, "Failed to map destination sg\n");
		return -ENOMEM;
	}

	/* Create AAD DST list: maps dst[0:AAD_SIZE-1]. */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
					    rctx->dst_dma_count,
					    &rctx->aad_dst_dll, req->assoclen,
					    0);
	if (rc)
		return rc;

	/* Create DST list: maps dst[AAD_SIZE:out_size] */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
					    rctx->dst_dma_count, &rctx->dst_dll,
					    out_size, req->assoclen);
	if (rc)
		return rc;

	if (rctx->in_place) {
		/* If this is not CCM encrypt, we are done. */
		if (!(rctx->mode == OCS_MODE_CCM &&
		      rctx->instruction == OCS_ENCRYPT)) {
			/*
			 * SRC and DST are the same, so re-use the same DMA
			 * addresses (to avoid allocating new DMA lists
			 * identical to the dst ones).
			 */
			rctx->src_dll.dma_addr = rctx->dst_dll.dma_addr;
			rctx->aad_src_dll.dma_addr = rctx->aad_dst_dll.dma_addr;

			return 0;
		}
		/*
		 * For CCM encrypt the input and output linked lists contain
		 * different amounts of data, so, we need to create different
		 * SRC and AAD SRC lists, even for the in-place case.
		 */
		rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
						    rctx->dst_dma_count,
						    &rctx->aad_src_dll,
						    req->assoclen, 0);
		if (rc)
			return rc;
		rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->dst,
						    rctx->dst_dma_count,
						    &rctx->src_dll, in_size,
						    req->assoclen);
		if (rc)
			return rc;

		return 0;
	}
	/* Not in-place case. */

	/* Map source SG. */
	rctx->src_dma_count = dma_map_sg(tctx->aes_dev->dev, req->src,
					 rctx->src_nents, DMA_TO_DEVICE);
	if (rctx->src_dma_count == 0 && rctx->src_nents != 0) {
		dev_err(tctx->aes_dev->dev, "Failed to map source sg\n");
		return -ENOMEM;
	}

	/* Create AAD SRC list. */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->src,
					    rctx->src_dma_count,
					    &rctx->aad_src_dll,
					    req->assoclen, 0);
	if (rc)
		return rc;

	/* Create SRC list. */
	rc = ocs_create_linked_list_from_sg(tctx->aes_dev, req->src,
					    rctx->src_dma_count,
					    &rctx->src_dll, in_size,
					    req->assoclen);
	if (rc)
		return rc;

	if (req->assoclen == 0)
		return 0;

	/* Copy AAD from src sg to dst sg using OCS DMA. */
	rc = ocs_aes_bypass_op(tctx->aes_dev, rctx->aad_dst_dll.dma_addr,
			       rctx->aad_src_dll.dma_addr, req->cryptlen);
	if (rc)
		dev_err(tctx->aes_dev->dev,
			"Failed to copy source AAD to destination AAD\n");

	return rc;
}

static int kmb_ocs_aead_run(struct aead_request *req)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	const int tag_size = crypto_aead_authsize(crypto_aead_reqtfm(req));
	struct ocs_aes_rctx *rctx = aead_request_ctx(req);
	u32 in_size;	/* The length of the data mapped by src_dll. */
	int rc;

	rc = kmb_ocs_aead_dma_prepare(req, &in_size);
	if (rc)
		goto exit;

	/* For CCM, we just call the OCS processing and we are done. */
	if (rctx->mode == OCS_MODE_CCM) {
		rc = ocs_aes_ccm_op(tctx->aes_dev, tctx->cipher,
				    rctx->instruction, rctx->dst_dll.dma_addr,
				    rctx->src_dll.dma_addr, in_size,
				    req->iv,
				    rctx->aad_src_dll.dma_addr, req->assoclen,
				    rctx->in_tag, tag_size);
		goto exit;
	}
	/* GCM case; invoke OCS processing. */
	rc = ocs_aes_gcm_op(tctx->aes_dev, tctx->cipher,
			    rctx->instruction,
			    rctx->dst_dll.dma_addr,
			    rctx->src_dll.dma_addr, in_size,
			    req->iv,
			    rctx->aad_src_dll.dma_addr, req->assoclen,
			    rctx->out_tag, tag_size);
	if (rc)
		goto exit;

	/* For GCM decrypt, we have to compare in_tag with out_tag. */
	if (rctx->instruction == OCS_DECRYPT) {
		rc = memcmp(rctx->in_tag, rctx->out_tag, tag_size) ?
		     -EBADMSG : 0;
		goto exit;
	}

	/* For GCM encrypt, we must manually copy out_tag to DST sg. */

	/* Clean-up must be called before the sg_pcopy_from_buffer() below. */
	kmb_ocs_aead_dma_cleanup(req);

	/* Copy tag to destination sg after AAD and CT. */
	sg_pcopy_from_buffer(req->dst, rctx->dst_nents, rctx->out_tag,
			     tag_size, req->assoclen + req->cryptlen);

	/* Return directly as DMA cleanup already done. */
	return 0;

exit:
	kmb_ocs_aead_dma_cleanup(req);

	return rc;
}

static int kmb_ocs_aes_sk_do_one_request(struct crypto_engine *engine,
					 void *areq)
{
	struct skcipher_request *req =
			container_of(areq, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	int err;

	if (!tctx->aes_dev) {
		err = -ENODEV;
		goto exit;
	}

	err = ocs_aes_set_key(tctx->aes_dev, tctx->key_len, tctx->key,
			      tctx->cipher);
	if (err)
		goto exit;

	err = kmb_ocs_sk_run(req);

exit:
	crypto_finalize_skcipher_request(engine, req, err);

	return 0;
}

static int kmb_ocs_aes_aead_do_one_request(struct crypto_engine *engine,
					   void *areq)
{
	struct aead_request *req = container_of(areq,
						struct aead_request, base);
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	int err;

	if (!tctx->aes_dev)
		return -ENODEV;

	err = ocs_aes_set_key(tctx->aes_dev, tctx->key_len, tctx->key,
			      tctx->cipher);
	if (err)
		goto exit;

	err = kmb_ocs_aead_run(req);

exit:
	crypto_finalize_aead_request(tctx->aes_dev->engine, req, err);

	return 0;
}

static int kmb_ocs_aes_set_key(struct crypto_skcipher *tfm, const u8 *in_key,
			       unsigned int key_len)
{
	return kmb_ocs_sk_set_key(tfm, in_key, key_len, OCS_AES);
}

static int kmb_ocs_aes_aead_set_key(struct crypto_aead *tfm, const u8 *in_key,
				    unsigned int key_len)
{
	return kmb_ocs_aead_set_key(tfm, in_key, key_len, OCS_AES);
}

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB
static int kmb_ocs_aes_ecb_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_ECB);
}

static int kmb_ocs_aes_ecb_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_ECB);
}
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB */

static int kmb_ocs_aes_cbc_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_CBC);
}

static int kmb_ocs_aes_cbc_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_CBC);
}

static int kmb_ocs_aes_ctr_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_CTR);
}

static int kmb_ocs_aes_ctr_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_CTR);
}

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS
static int kmb_ocs_aes_cts_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_CTS);
}

static int kmb_ocs_aes_cts_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_CTS);
}
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS */

static int kmb_ocs_aes_gcm_encrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_GCM);
}

static int kmb_ocs_aes_gcm_decrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_GCM);
}

static int kmb_ocs_aes_ccm_encrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_AES, OCS_ENCRYPT, OCS_MODE_CCM);
}

static int kmb_ocs_aes_ccm_decrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_AES, OCS_DECRYPT, OCS_MODE_CCM);
}

static int kmb_ocs_sm4_set_key(struct crypto_skcipher *tfm, const u8 *in_key,
			       unsigned int key_len)
{
	return kmb_ocs_sk_set_key(tfm, in_key, key_len, OCS_SM4);
}

static int kmb_ocs_sm4_aead_set_key(struct crypto_aead *tfm, const u8 *in_key,
				    unsigned int key_len)
{
	return kmb_ocs_aead_set_key(tfm, in_key, key_len, OCS_SM4);
}

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB
static int kmb_ocs_sm4_ecb_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_ECB);
}

static int kmb_ocs_sm4_ecb_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_ECB);
}
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB */

static int kmb_ocs_sm4_cbc_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_CBC);
}

static int kmb_ocs_sm4_cbc_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_CBC);
}

static int kmb_ocs_sm4_ctr_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_CTR);
}

static int kmb_ocs_sm4_ctr_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_CTR);
}

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS
static int kmb_ocs_sm4_cts_encrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_CTS);
}

static int kmb_ocs_sm4_cts_decrypt(struct skcipher_request *req)
{
	return kmb_ocs_sk_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_CTS);
}
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS */

static int kmb_ocs_sm4_gcm_encrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_GCM);
}

static int kmb_ocs_sm4_gcm_decrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_GCM);
}

static int kmb_ocs_sm4_ccm_encrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_SM4, OCS_ENCRYPT, OCS_MODE_CCM);
}

static int kmb_ocs_sm4_ccm_decrypt(struct aead_request *req)
{
	return kmb_ocs_aead_common(req, OCS_SM4, OCS_DECRYPT, OCS_MODE_CCM);
}

static inline int ocs_common_init(struct ocs_aes_tctx *tctx)
{
	tctx->engine_ctx.op.prepare_request = NULL;
	tctx->engine_ctx.op.do_one_request = kmb_ocs_aes_sk_do_one_request;
	tctx->engine_ctx.op.unprepare_request = NULL;

	return 0;
}

static int ocs_aes_init_tfm(struct crypto_skcipher *tfm)
{
	const char *alg_name = crypto_tfm_alg_name(&tfm->base);
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);
	struct crypto_sync_skcipher *blk;

	/* set fallback cipher in case it will be needed */
	blk = crypto_alloc_sync_skcipher(alg_name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(blk))
		return PTR_ERR(blk);

	tctx->sw_cipher.sk = blk;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct ocs_aes_rctx));

	return ocs_common_init(tctx);
}

static int ocs_sm4_init_tfm(struct crypto_skcipher *tfm)
{
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct ocs_aes_rctx));

	return ocs_common_init(tctx);
}

static inline void clear_key(struct ocs_aes_tctx *tctx)
{
	memzero_explicit(tctx->key, OCS_AES_KEYSIZE_256);

	/* Zero key registers if set */
	if (tctx->aes_dev)
		ocs_aes_set_key(tctx->aes_dev, OCS_AES_KEYSIZE_256,
				tctx->key, OCS_AES);
}

static void ocs_exit_tfm(struct crypto_skcipher *tfm)
{
	struct ocs_aes_tctx *tctx = crypto_skcipher_ctx(tfm);

	clear_key(tctx);

	if (tctx->sw_cipher.sk) {
		crypto_free_sync_skcipher(tctx->sw_cipher.sk);
		tctx->sw_cipher.sk = NULL;
	}
}

static inline int ocs_common_aead_init(struct ocs_aes_tctx *tctx)
{
	tctx->engine_ctx.op.prepare_request = NULL;
	tctx->engine_ctx.op.do_one_request = kmb_ocs_aes_aead_do_one_request;
	tctx->engine_ctx.op.unprepare_request = NULL;

	return 0;
}

static int ocs_aes_aead_cra_init(struct crypto_aead *tfm)
{
	const char *alg_name = crypto_tfm_alg_name(&tfm->base);
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(tfm);
	struct crypto_aead *blk;

	/* Set fallback cipher in case it will be needed */
	blk = crypto_alloc_aead(alg_name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(blk))
		return PTR_ERR(blk);

	tctx->sw_cipher.aead = blk;

	crypto_aead_set_reqsize(tfm,
				max(sizeof(struct ocs_aes_rctx),
				    (sizeof(struct aead_request) +
				     crypto_aead_reqsize(tctx->sw_cipher.aead))));

	return ocs_common_aead_init(tctx);
}

static int kmb_ocs_aead_ccm_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		return 0;
	default:
		return -EINVAL;
	}
}

static int kmb_ocs_aead_gcm_setauthsize(struct crypto_aead *tfm,
					unsigned int authsize)
{
	return crypto_gcm_check_authsize(authsize);
}

static int ocs_sm4_aead_cra_init(struct crypto_aead *tfm)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(tfm);

	crypto_aead_set_reqsize(tfm, sizeof(struct ocs_aes_rctx));

	return ocs_common_aead_init(tctx);
}

static void ocs_aead_cra_exit(struct crypto_aead *tfm)
{
	struct ocs_aes_tctx *tctx = crypto_aead_ctx(tfm);

	clear_key(tctx);

	if (tctx->sw_cipher.aead) {
		crypto_free_aead(tctx->sw_cipher.aead);
		tctx->sw_cipher.aead = NULL;
	}
}

static struct skcipher_alg algs[] = {
#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB
	{
		.base.cra_name = "ecb(aes)",
		.base.cra_driver_name = "ecb-aes-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_AES_MIN_KEY_SIZE,
		.max_keysize = OCS_AES_MAX_KEY_SIZE,
		.setkey = kmb_ocs_aes_set_key,
		.encrypt = kmb_ocs_aes_ecb_encrypt,
		.decrypt = kmb_ocs_aes_ecb_decrypt,
		.init = ocs_aes_init_tfm,
		.exit = ocs_exit_tfm,
	},
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB */
	{
		.base.cra_name = "cbc(aes)",
		.base.cra_driver_name = "cbc-aes-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_AES_MIN_KEY_SIZE,
		.max_keysize = OCS_AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_aes_set_key,
		.encrypt = kmb_ocs_aes_cbc_encrypt,
		.decrypt = kmb_ocs_aes_cbc_decrypt,
		.init = ocs_aes_init_tfm,
		.exit = ocs_exit_tfm,
	},
	{
		.base.cra_name = "ctr(aes)",
		.base.cra_driver_name = "ctr-aes-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_AES_MIN_KEY_SIZE,
		.max_keysize = OCS_AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_aes_set_key,
		.encrypt = kmb_ocs_aes_ctr_encrypt,
		.decrypt = kmb_ocs_aes_ctr_decrypt,
		.init = ocs_aes_init_tfm,
		.exit = ocs_exit_tfm,
	},
#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS
	{
		.base.cra_name = "cts(cbc(aes))",
		.base.cra_driver_name = "cts-aes-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_AES_MIN_KEY_SIZE,
		.max_keysize = OCS_AES_MAX_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_aes_set_key,
		.encrypt = kmb_ocs_aes_cts_encrypt,
		.decrypt = kmb_ocs_aes_cts_decrypt,
		.init = ocs_aes_init_tfm,
		.exit = ocs_exit_tfm,
	},
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS */
#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB
	{
		.base.cra_name = "ecb(sm4)",
		.base.cra_driver_name = "ecb-sm4-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_SM4_KEY_SIZE,
		.max_keysize = OCS_SM4_KEY_SIZE,
		.setkey = kmb_ocs_sm4_set_key,
		.encrypt = kmb_ocs_sm4_ecb_encrypt,
		.decrypt = kmb_ocs_sm4_ecb_decrypt,
		.init = ocs_sm4_init_tfm,
		.exit = ocs_exit_tfm,
	},
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB */
	{
		.base.cra_name = "cbc(sm4)",
		.base.cra_driver_name = "cbc-sm4-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_SM4_KEY_SIZE,
		.max_keysize = OCS_SM4_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_sm4_set_key,
		.encrypt = kmb_ocs_sm4_cbc_encrypt,
		.decrypt = kmb_ocs_sm4_cbc_decrypt,
		.init = ocs_sm4_init_tfm,
		.exit = ocs_exit_tfm,
	},
	{
		.base.cra_name = "ctr(sm4)",
		.base.cra_driver_name = "ctr-sm4-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize = 1,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_SM4_KEY_SIZE,
		.max_keysize = OCS_SM4_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_sm4_set_key,
		.encrypt = kmb_ocs_sm4_ctr_encrypt,
		.decrypt = kmb_ocs_sm4_ctr_decrypt,
		.init = ocs_sm4_init_tfm,
		.exit = ocs_exit_tfm,
	},
#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS
	{
		.base.cra_name = "cts(cbc(sm4))",
		.base.cra_driver_name = "cts-sm4-keembay-ocs",
		.base.cra_priority = KMB_OCS_PRIORITY,
		.base.cra_flags = CRYPTO_ALG_ASYNC |
				  CRYPTO_ALG_KERN_DRIVER_ONLY,
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.base.cra_ctxsize = sizeof(struct ocs_aes_tctx),
		.base.cra_module = THIS_MODULE,
		.base.cra_alignmask = 0,

		.min_keysize = OCS_SM4_KEY_SIZE,
		.max_keysize = OCS_SM4_KEY_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.setkey = kmb_ocs_sm4_set_key,
		.encrypt = kmb_ocs_sm4_cts_encrypt,
		.decrypt = kmb_ocs_sm4_cts_decrypt,
		.init = ocs_sm4_init_tfm,
		.exit = ocs_exit_tfm,
	}
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS */
};

static struct aead_alg algs_aead[] = {
	{
		.base = {
			.cra_name = "gcm(aes)",
			.cra_driver_name = "gcm-aes-keembay-ocs",
			.cra_priority = KMB_OCS_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct ocs_aes_tctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = ocs_aes_aead_cra_init,
		.exit = ocs_aead_cra_exit,
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.setauthsize = kmb_ocs_aead_gcm_setauthsize,
		.setkey = kmb_ocs_aes_aead_set_key,
		.encrypt = kmb_ocs_aes_gcm_encrypt,
		.decrypt = kmb_ocs_aes_gcm_decrypt,
	},
	{
		.base = {
			.cra_name = "ccm(aes)",
			.cra_driver_name = "ccm-aes-keembay-ocs",
			.cra_priority = KMB_OCS_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct ocs_aes_tctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = ocs_aes_aead_cra_init,
		.exit = ocs_aead_cra_exit,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.setauthsize = kmb_ocs_aead_ccm_setauthsize,
		.setkey = kmb_ocs_aes_aead_set_key,
		.encrypt = kmb_ocs_aes_ccm_encrypt,
		.decrypt = kmb_ocs_aes_ccm_decrypt,
	},
	{
		.base = {
			.cra_name = "gcm(sm4)",
			.cra_driver_name = "gcm-sm4-keembay-ocs",
			.cra_priority = KMB_OCS_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct ocs_aes_tctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = ocs_sm4_aead_cra_init,
		.exit = ocs_aead_cra_exit,
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.setauthsize = kmb_ocs_aead_gcm_setauthsize,
		.setkey = kmb_ocs_sm4_aead_set_key,
		.encrypt = kmb_ocs_sm4_gcm_encrypt,
		.decrypt = kmb_ocs_sm4_gcm_decrypt,
	},
	{
		.base = {
			.cra_name = "ccm(sm4)",
			.cra_driver_name = "ccm-sm4-keembay-ocs",
			.cra_priority = KMB_OCS_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct ocs_aes_tctx),
			.cra_alignmask = 0,
			.cra_module = THIS_MODULE,
		},
		.init = ocs_sm4_aead_cra_init,
		.exit = ocs_aead_cra_exit,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
		.setauthsize = kmb_ocs_aead_ccm_setauthsize,
		.setkey = kmb_ocs_sm4_aead_set_key,
		.encrypt = kmb_ocs_sm4_ccm_encrypt,
		.decrypt = kmb_ocs_sm4_ccm_decrypt,
	}
};

static void unregister_aes_algs(struct ocs_aes_dev *aes_dev)
{
	crypto_unregister_aeads(algs_aead, ARRAY_SIZE(algs_aead));
	crypto_unregister_skciphers(algs, ARRAY_SIZE(algs));
}

static int register_aes_algs(struct ocs_aes_dev *aes_dev)
{
	int ret;

	/*
	 * If any algorithm fails to register, all preceding algorithms that
	 * were successfully registered will be automatically unregistered.
	 */
	ret = crypto_register_aeads(algs_aead, ARRAY_SIZE(algs_aead));
	if (ret)
		return ret;

	ret = crypto_register_skciphers(algs, ARRAY_SIZE(algs));
	if (ret)
		crypto_unregister_aeads(algs_aead, ARRAY_SIZE(algs));

	return ret;
}

/* Device tree driver match. */
static const struct of_device_id kmb_ocs_aes_of_match[] = {
	{
		.compatible = "intel,keembay-ocs-aes",
	},
	{}
};

static int kmb_ocs_aes_remove(struct platform_device *pdev)
{
	struct ocs_aes_dev *aes_dev;

	aes_dev = platform_get_drvdata(pdev);
	if (!aes_dev)
		return -ENODEV;

	unregister_aes_algs(aes_dev);

	spin_lock(&ocs_aes.lock);
	list_del(&aes_dev->list);
	spin_unlock(&ocs_aes.lock);

	crypto_engine_exit(aes_dev->engine);

	return 0;
}

static int kmb_ocs_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocs_aes_dev *aes_dev;
	struct resource *aes_mem;
	int rc;

	aes_dev = devm_kzalloc(dev, sizeof(*aes_dev), GFP_KERNEL);
	if (!aes_dev)
		return -ENOMEM;

	aes_dev->dev = dev;

	platform_set_drvdata(pdev, aes_dev);

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "Failed to set 32 bit dma mask %d\n", rc);
		return rc;
	}

	/* Get base register address. */
	aes_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!aes_mem) {
		dev_err(dev, "Could not retrieve io mem resource\n");
		return -ENODEV;
	}

	aes_dev->base_reg = devm_ioremap_resource(&pdev->dev, aes_mem);
	if (IS_ERR(aes_dev->base_reg)) {
		dev_err(dev, "Failed to get base address\n");
		return PTR_ERR(aes_dev->base_reg);
	}

	/* Get and request IRQ */
	aes_dev->irq = platform_get_irq(pdev, 0);
	if (aes_dev->irq < 0)
		return aes_dev->irq;

	rc = devm_request_threaded_irq(dev, aes_dev->irq, ocs_aes_irq_handler,
				       NULL, 0, "keembay-ocs-aes", aes_dev);
	if (rc < 0) {
		dev_err(dev, "Could not request IRQ\n");
		return rc;
	}

	INIT_LIST_HEAD(&aes_dev->list);
	spin_lock(&ocs_aes.lock);
	list_add_tail(&aes_dev->list, &ocs_aes.dev_list);
	spin_unlock(&ocs_aes.lock);

	init_completion(&aes_dev->irq_completion);

	/* Initialize crypto engine */
	aes_dev->engine = crypto_engine_alloc_init(dev, true);
	if (!aes_dev->engine)
		goto list_del;

	rc = crypto_engine_start(aes_dev->engine);
	if (rc) {
		dev_err(dev, "Could not start crypto engine\n");
		goto cleanup;
	}

	rc = register_aes_algs(aes_dev);
	if (rc) {
		dev_err(dev,
			"Could not register OCS algorithms with Crypto API\n");
		goto cleanup;
	}

	return 0;

cleanup:
	crypto_engine_exit(aes_dev->engine);
list_del:
	spin_lock(&ocs_aes.lock);
	list_del(&aes_dev->list);
	spin_unlock(&ocs_aes.lock);

	return rc;
}

/* The OCS driver is a platform device. */
static struct platform_driver kmb_ocs_aes_driver = {
	.probe = kmb_ocs_aes_probe,
	.remove = kmb_ocs_aes_remove,
	.driver = {
			.name = DRV_NAME,
			.of_match_table = kmb_ocs_aes_of_match,
		},
};

module_platform_driver(kmb_ocs_aes_driver);

MODULE_DESCRIPTION("Intel Keem Bay Offload and Crypto Subsystem (OCS) AES/SM4 Driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS_CRYPTO("cbc-aes-keembay-ocs");
MODULE_ALIAS_CRYPTO("ctr-aes-keembay-ocs");
MODULE_ALIAS_CRYPTO("gcm-aes-keembay-ocs");
MODULE_ALIAS_CRYPTO("ccm-aes-keembay-ocs");

MODULE_ALIAS_CRYPTO("cbc-sm4-keembay-ocs");
MODULE_ALIAS_CRYPTO("ctr-sm4-keembay-ocs");
MODULE_ALIAS_CRYPTO("gcm-sm4-keembay-ocs");
MODULE_ALIAS_CRYPTO("ccm-sm4-keembay-ocs");

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB
MODULE_ALIAS_CRYPTO("ecb-aes-keembay-ocs");
MODULE_ALIAS_CRYPTO("ecb-sm4-keembay-ocs");
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_ECB */

#ifdef CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS
MODULE_ALIAS_CRYPTO("cts-aes-keembay-ocs");
MODULE_ALIAS_CRYPTO("cts-sm4-keembay-ocs");
#endif /* CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4_CTS */
