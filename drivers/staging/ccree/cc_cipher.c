// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <crypto/des.h>
#include <crypto/xts.h>
#include <crypto/scatterwalk.h>

#include "cc_driver.h"
#include "cc_lli_defs.h"
#include "cc_buffer_mgr.h"
#include "cc_cipher.h"
#include "cc_request_mgr.h"

#define MAX_ABLKCIPHER_SEQ_LEN 6

#define template_ablkcipher	template_u.ablkcipher

#define CC_MIN_AES_XTS_SIZE 0x10
#define CC_MAX_AES_XTS_SIZE 0x2000
struct cc_cipher_handle {
	struct list_head blkcipher_alg_list;
};

struct cc_user_key_info {
	u8 *key;
	dma_addr_t key_dma_addr;
};

struct cc_hw_key_info {
	enum cc_hw_crypto_key key1_slot;
	enum cc_hw_crypto_key key2_slot;
};

struct cc_cipher_ctx {
	struct cc_drvdata *drvdata;
	int keylen;
	int key_round_number;
	int cipher_mode;
	int flow_mode;
	unsigned int flags;
	struct blkcipher_req_ctx *sync_ctx;
	struct cc_user_key_info user;
	struct cc_hw_key_info hw;
	struct crypto_shash *shash_tfm;
};

static void cc_cipher_complete(struct device *dev, void *cc_req, int err);

static int validate_keys_sizes(struct cc_cipher_ctx *ctx_p, u32 size)
{
	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		switch (size) {
		case CC_AES_128_BIT_KEY_SIZE:
		case CC_AES_192_BIT_KEY_SIZE:
			if (ctx_p->cipher_mode != DRV_CIPHER_XTS &&
			    ctx_p->cipher_mode != DRV_CIPHER_ESSIV &&
			    ctx_p->cipher_mode != DRV_CIPHER_BITLOCKER)
				return 0;
			break;
		case CC_AES_256_BIT_KEY_SIZE:
			return 0;
		case (CC_AES_192_BIT_KEY_SIZE * 2):
		case (CC_AES_256_BIT_KEY_SIZE * 2):
			if (ctx_p->cipher_mode == DRV_CIPHER_XTS ||
			    ctx_p->cipher_mode == DRV_CIPHER_ESSIV ||
			    ctx_p->cipher_mode == DRV_CIPHER_BITLOCKER)
				return 0;
			break;
		default:
			break;
		}
	case S_DIN_to_DES:
		if (size == DES3_EDE_KEY_SIZE || size == DES_KEY_SIZE)
			return 0;
		break;
	default:
		break;
	}
	return -EINVAL;
}

static int validate_data_size(struct cc_cipher_ctx *ctx_p,
			      unsigned int size)
{
	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		switch (ctx_p->cipher_mode) {
		case DRV_CIPHER_XTS:
			if (size >= CC_MIN_AES_XTS_SIZE &&
			    size <= CC_MAX_AES_XTS_SIZE &&
			    IS_ALIGNED(size, AES_BLOCK_SIZE))
				return 0;
			break;
		case DRV_CIPHER_CBC_CTS:
			if (size >= AES_BLOCK_SIZE)
				return 0;
			break;
		case DRV_CIPHER_OFB:
		case DRV_CIPHER_CTR:
				return 0;
		case DRV_CIPHER_ECB:
		case DRV_CIPHER_CBC:
		case DRV_CIPHER_ESSIV:
		case DRV_CIPHER_BITLOCKER:
			if (IS_ALIGNED(size, AES_BLOCK_SIZE))
				return 0;
			break;
		default:
			break;
		}
		break;
	case S_DIN_to_DES:
		if (IS_ALIGNED(size, DES_BLOCK_SIZE))
			return 0;
		break;
	default:
		break;
	}
	return -EINVAL;
}

static unsigned int get_max_keysize(struct crypto_tfm *tfm)
{
	struct cc_crypto_alg *cc_alg =
		container_of(tfm->__crt_alg, struct cc_crypto_alg, crypto_alg);

	if ((cc_alg->crypto_alg.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_ABLKCIPHER)
		return cc_alg->crypto_alg.cra_ablkcipher.max_keysize;

	if ((cc_alg->crypto_alg.cra_flags & CRYPTO_ALG_TYPE_MASK) ==
	    CRYPTO_ALG_TYPE_BLKCIPHER)
		return cc_alg->crypto_alg.cra_blkcipher.max_keysize;

	return 0;
}

static int cc_cipher_init(struct crypto_tfm *tfm)
{
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct cc_crypto_alg *cc_alg =
			container_of(alg, struct cc_crypto_alg, crypto_alg);
	struct device *dev = drvdata_to_dev(cc_alg->drvdata);
	int rc = 0;
	unsigned int max_key_buf_size = get_max_keysize(tfm);
	struct ablkcipher_tfm *ablktfm = &tfm->crt_ablkcipher;

	dev_dbg(dev, "Initializing context @%p for %s\n", ctx_p,
		crypto_tfm_alg_name(tfm));

	ablktfm->reqsize = sizeof(struct blkcipher_req_ctx);

	ctx_p->cipher_mode = cc_alg->cipher_mode;
	ctx_p->flow_mode = cc_alg->flow_mode;
	ctx_p->drvdata = cc_alg->drvdata;

	/* Allocate key buffer, cache line aligned */
	ctx_p->user.key = kmalloc(max_key_buf_size, GFP_KERNEL);
	if (!ctx_p->user.key)
		return -ENOMEM;

	dev_dbg(dev, "Allocated key buffer in context. key=@%p\n",
		ctx_p->user.key);

	/* Map key buffer */
	ctx_p->user.key_dma_addr = dma_map_single(dev, (void *)ctx_p->user.key,
						  max_key_buf_size,
						  DMA_TO_DEVICE);
	if (dma_mapping_error(dev, ctx_p->user.key_dma_addr)) {
		dev_err(dev, "Mapping Key %u B at va=%pK for DMA failed\n",
			max_key_buf_size, ctx_p->user.key);
		return -ENOMEM;
	}
	dev_dbg(dev, "Mapped key %u B at va=%pK to dma=%pad\n",
		max_key_buf_size, ctx_p->user.key, &ctx_p->user.key_dma_addr);

	if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
		/* Alloc hash tfm for essiv */
		ctx_p->shash_tfm = crypto_alloc_shash("sha256-generic", 0, 0);
		if (IS_ERR(ctx_p->shash_tfm)) {
			dev_err(dev, "Error allocating hash tfm for ESSIV.\n");
			return PTR_ERR(ctx_p->shash_tfm);
		}
	}

	return rc;
}

static void cc_cipher_exit(struct crypto_tfm *tfm)
{
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx_p->drvdata);
	unsigned int max_key_buf_size = get_max_keysize(tfm);

	dev_dbg(dev, "Clearing context @%p for %s\n",
		crypto_tfm_ctx(tfm), crypto_tfm_alg_name(tfm));

	if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
		/* Free hash tfm for essiv */
		crypto_free_shash(ctx_p->shash_tfm);
		ctx_p->shash_tfm = NULL;
	}

	/* Unmap key buffer */
	dma_unmap_single(dev, ctx_p->user.key_dma_addr, max_key_buf_size,
			 DMA_TO_DEVICE);
	dev_dbg(dev, "Unmapped key buffer key_dma_addr=%pad\n",
		&ctx_p->user.key_dma_addr);

	/* Free key buffer in context */
	kfree(ctx_p->user.key);
	dev_dbg(dev, "Free key buffer in context. key=@%p\n", ctx_p->user.key);
}

struct tdes_keys {
	u8	key1[DES_KEY_SIZE];
	u8	key2[DES_KEY_SIZE];
	u8	key3[DES_KEY_SIZE];
};

static const u8 zero_buff[] = {	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

/* The function verifies that tdes keys are not weak.*/
static int cc_verify_3des_keys(const u8 *key, unsigned int keylen)
{
	struct tdes_keys *tdes_key = (struct tdes_keys *)key;

	/* verify key1 != key2 and key3 != key2*/
	if ((memcmp((u8 *)tdes_key->key1, (u8 *)tdes_key->key2,
		    sizeof(tdes_key->key1)) == 0) ||
	    (memcmp((u8 *)tdes_key->key3, (u8 *)tdes_key->key2,
		    sizeof(tdes_key->key3)) == 0)) {
		return -ENOEXEC;
	}

	return 0;
}

static enum cc_hw_crypto_key hw_key_to_cc_hw_key(int slot_num)
{
	switch (slot_num) {
	case 0:
		return KFDE0_KEY;
	case 1:
		return KFDE1_KEY;
	case 2:
		return KFDE2_KEY;
	case 3:
		return KFDE3_KEY;
	}
	return END_OF_KEYS;
}

static int cc_cipher_setkey(struct crypto_ablkcipher *atfm, const u8 *key,
			    unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(atfm);
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx_p->drvdata);
	u32 tmp[DES_EXPKEY_WORDS];
	unsigned int max_key_buf_size = get_max_keysize(tfm);

	dev_dbg(dev, "Setting key in context @%p for %s. keylen=%u\n",
		ctx_p, crypto_tfm_alg_name(tfm), keylen);
	dump_byte_array("key", (u8 *)key, keylen);

	/* STAT_PHASE_0: Init and sanity checks */

	if (validate_keys_sizes(ctx_p, keylen)) {
		dev_err(dev, "Unsupported key size %d.\n", keylen);
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (cc_is_hw_key(tfm)) {
		/* setting HW key slots */
		struct arm_hw_key_info *hki = (struct arm_hw_key_info *)key;

		if (ctx_p->flow_mode != S_DIN_to_AES) {
			dev_err(dev, "HW key not supported for non-AES flows\n");
			return -EINVAL;
		}

		ctx_p->hw.key1_slot = hw_key_to_cc_hw_key(hki->hw_key1);
		if (ctx_p->hw.key1_slot == END_OF_KEYS) {
			dev_err(dev, "Unsupported hw key1 number (%d)\n",
				hki->hw_key1);
			return -EINVAL;
		}

		if (ctx_p->cipher_mode == DRV_CIPHER_XTS ||
		    ctx_p->cipher_mode == DRV_CIPHER_ESSIV ||
		    ctx_p->cipher_mode == DRV_CIPHER_BITLOCKER) {
			if (hki->hw_key1 == hki->hw_key2) {
				dev_err(dev, "Illegal hw key numbers (%d,%d)\n",
					hki->hw_key1, hki->hw_key2);
				return -EINVAL;
			}
			ctx_p->hw.key2_slot =
				hw_key_to_cc_hw_key(hki->hw_key2);
			if (ctx_p->hw.key2_slot == END_OF_KEYS) {
				dev_err(dev, "Unsupported hw key2 number (%d)\n",
					hki->hw_key2);
				return -EINVAL;
			}
		}

		ctx_p->keylen = keylen;
		dev_dbg(dev, "cc_is_hw_key ret 0");

		return 0;
	}

	// verify weak keys
	if (ctx_p->flow_mode == S_DIN_to_DES) {
		if (!des_ekey(tmp, key) &&
		    (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			dev_dbg(dev, "weak DES key");
			return -EINVAL;
		}
	}
	if (ctx_p->cipher_mode == DRV_CIPHER_XTS &&
	    xts_check_key(tfm, key, keylen)) {
		dev_dbg(dev, "weak XTS key");
		return -EINVAL;
	}
	if (ctx_p->flow_mode == S_DIN_to_DES &&
	    keylen == DES3_EDE_KEY_SIZE &&
	    cc_verify_3des_keys(key, keylen)) {
		dev_dbg(dev, "weak 3DES key");
		return -EINVAL;
	}

	/* STAT_PHASE_1: Copy key to ctx */
	dma_sync_single_for_cpu(dev, ctx_p->user.key_dma_addr,
				max_key_buf_size, DMA_TO_DEVICE);

	memcpy(ctx_p->user.key, key, keylen);
	if (keylen == 24)
		memset(ctx_p->user.key + 24, 0, CC_AES_KEY_SIZE_MAX - 24);

	if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
		/* sha256 for key2 - use sw implementation */
		int key_len = keylen >> 1;
		int err;

		SHASH_DESC_ON_STACK(desc, ctx_p->shash_tfm);

		desc->tfm = ctx_p->shash_tfm;

		err = crypto_shash_digest(desc, ctx_p->user.key, key_len,
					  ctx_p->user.key + key_len);
		if (err) {
			dev_err(dev, "Failed to hash ESSIV key.\n");
			return err;
		}
	}
	dma_sync_single_for_device(dev, ctx_p->user.key_dma_addr,
				   max_key_buf_size, DMA_TO_DEVICE);
	ctx_p->keylen = keylen;

	 dev_dbg(dev, "return safely");
	return 0;
}

static void cc_setup_cipher_desc(struct crypto_tfm *tfm,
				 struct blkcipher_req_ctx *req_ctx,
				 unsigned int ivsize, unsigned int nbytes,
				 struct cc_hw_desc desc[],
				 unsigned int *seq_size)
{
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx_p->drvdata);
	int cipher_mode = ctx_p->cipher_mode;
	int flow_mode = ctx_p->flow_mode;
	int direction = req_ctx->gen_ctx.op_type;
	dma_addr_t key_dma_addr = ctx_p->user.key_dma_addr;
	unsigned int key_len = ctx_p->keylen;
	dma_addr_t iv_dma_addr = req_ctx->gen_ctx.iv_dma_addr;
	unsigned int du_size = nbytes;

	struct cc_crypto_alg *cc_alg =
		container_of(tfm->__crt_alg, struct cc_crypto_alg, crypto_alg);

	if ((cc_alg->crypto_alg.cra_flags & CRYPTO_ALG_BULK_MASK) ==
	    CRYPTO_ALG_BULK_DU_512)
		du_size = 512;
	if ((cc_alg->crypto_alg.cra_flags & CRYPTO_ALG_BULK_MASK) ==
	    CRYPTO_ALG_BULK_DU_4096)
		du_size = 4096;

	switch (cipher_mode) {
	case DRV_CIPHER_CBC:
	case DRV_CIPHER_CBC_CTS:
	case DRV_CIPHER_CTR:
	case DRV_CIPHER_OFB:
		/* Load cipher state */
		hw_desc_init(&desc[*seq_size]);
		set_din_type(&desc[*seq_size], DMA_DLLI, iv_dma_addr, ivsize,
			     NS_BIT);
		set_cipher_config0(&desc[*seq_size], direction);
		set_flow_mode(&desc[*seq_size], flow_mode);
		set_cipher_mode(&desc[*seq_size], cipher_mode);
		if (cipher_mode == DRV_CIPHER_CTR ||
		    cipher_mode == DRV_CIPHER_OFB) {
			set_setup_mode(&desc[*seq_size], SETUP_LOAD_STATE1);
		} else {
			set_setup_mode(&desc[*seq_size], SETUP_LOAD_STATE0);
		}
		(*seq_size)++;
		/*FALLTHROUGH*/
	case DRV_CIPHER_ECB:
		/* Load key */
		hw_desc_init(&desc[*seq_size]);
		set_cipher_mode(&desc[*seq_size], cipher_mode);
		set_cipher_config0(&desc[*seq_size], direction);
		if (flow_mode == S_DIN_to_AES) {
			if (cc_is_hw_key(tfm)) {
				set_hw_crypto_key(&desc[*seq_size],
						  ctx_p->hw.key1_slot);
			} else {
				set_din_type(&desc[*seq_size], DMA_DLLI,
					     key_dma_addr, ((key_len == 24) ?
							    AES_MAX_KEY_SIZE :
							    key_len), NS_BIT);
			}
			set_key_size_aes(&desc[*seq_size], key_len);
		} else {
			/*des*/
			set_din_type(&desc[*seq_size], DMA_DLLI, key_dma_addr,
				     key_len, NS_BIT);
			set_key_size_des(&desc[*seq_size], key_len);
		}
		set_flow_mode(&desc[*seq_size], flow_mode);
		set_setup_mode(&desc[*seq_size], SETUP_LOAD_KEY0);
		(*seq_size)++;
		break;
	case DRV_CIPHER_XTS:
	case DRV_CIPHER_ESSIV:
	case DRV_CIPHER_BITLOCKER:
		/* Load AES key */
		hw_desc_init(&desc[*seq_size]);
		set_cipher_mode(&desc[*seq_size], cipher_mode);
		set_cipher_config0(&desc[*seq_size], direction);
		if (cc_is_hw_key(tfm)) {
			set_hw_crypto_key(&desc[*seq_size],
					  ctx_p->hw.key1_slot);
		} else {
			set_din_type(&desc[*seq_size], DMA_DLLI, key_dma_addr,
				     (key_len / 2), NS_BIT);
		}
		set_key_size_aes(&desc[*seq_size], (key_len / 2));
		set_flow_mode(&desc[*seq_size], flow_mode);
		set_setup_mode(&desc[*seq_size], SETUP_LOAD_KEY0);
		(*seq_size)++;

		/* load XEX key */
		hw_desc_init(&desc[*seq_size]);
		set_cipher_mode(&desc[*seq_size], cipher_mode);
		set_cipher_config0(&desc[*seq_size], direction);
		if (cc_is_hw_key(tfm)) {
			set_hw_crypto_key(&desc[*seq_size],
					  ctx_p->hw.key2_slot);
		} else {
			set_din_type(&desc[*seq_size], DMA_DLLI,
				     (key_dma_addr + (key_len / 2)),
				     (key_len / 2), NS_BIT);
		}
		set_xex_data_unit_size(&desc[*seq_size], du_size);
		set_flow_mode(&desc[*seq_size], S_DIN_to_AES2);
		set_key_size_aes(&desc[*seq_size], (key_len / 2));
		set_setup_mode(&desc[*seq_size], SETUP_LOAD_XEX_KEY);
		(*seq_size)++;

		/* Set state */
		hw_desc_init(&desc[*seq_size]);
		set_setup_mode(&desc[*seq_size], SETUP_LOAD_STATE1);
		set_cipher_mode(&desc[*seq_size], cipher_mode);
		set_cipher_config0(&desc[*seq_size], direction);
		set_key_size_aes(&desc[*seq_size], (key_len / 2));
		set_flow_mode(&desc[*seq_size], flow_mode);
		set_din_type(&desc[*seq_size], DMA_DLLI, iv_dma_addr,
			     CC_AES_BLOCK_SIZE, NS_BIT);
		(*seq_size)++;
		break;
	default:
		dev_err(dev, "Unsupported cipher mode (%d)\n", cipher_mode);
	}
}

static void cc_setup_cipher_data(struct crypto_tfm *tfm,
				 struct blkcipher_req_ctx *req_ctx,
				 struct scatterlist *dst,
				 struct scatterlist *src, unsigned int nbytes,
				 void *areq, struct cc_hw_desc desc[],
				 unsigned int *seq_size)
{
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx_p->drvdata);
	unsigned int flow_mode = ctx_p->flow_mode;

	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		flow_mode = DIN_AES_DOUT;
		break;
	case S_DIN_to_DES:
		flow_mode = DIN_DES_DOUT;
		break;
	default:
		dev_err(dev, "invalid flow mode, flow_mode = %d\n", flow_mode);
		return;
	}
	/* Process */
	if (req_ctx->dma_buf_type == CC_DMA_BUF_DLLI) {
		dev_dbg(dev, " data params addr %pad length 0x%X\n",
			&sg_dma_address(src), nbytes);
		dev_dbg(dev, " data params addr %pad length 0x%X\n",
			&sg_dma_address(dst), nbytes);
		hw_desc_init(&desc[*seq_size]);
		set_din_type(&desc[*seq_size], DMA_DLLI, sg_dma_address(src),
			     nbytes, NS_BIT);
		set_dout_dlli(&desc[*seq_size], sg_dma_address(dst),
			      nbytes, NS_BIT, (!areq ? 0 : 1));
		if (areq)
			set_queue_last_ind(&desc[*seq_size]);

		set_flow_mode(&desc[*seq_size], flow_mode);
		(*seq_size)++;
	} else {
		/* bypass */
		dev_dbg(dev, " bypass params addr %pad length 0x%X addr 0x%08X\n",
			&req_ctx->mlli_params.mlli_dma_addr,
			req_ctx->mlli_params.mlli_len,
			(unsigned int)ctx_p->drvdata->mlli_sram_addr);
		hw_desc_init(&desc[*seq_size]);
		set_din_type(&desc[*seq_size], DMA_DLLI,
			     req_ctx->mlli_params.mlli_dma_addr,
			     req_ctx->mlli_params.mlli_len, NS_BIT);
		set_dout_sram(&desc[*seq_size],
			      ctx_p->drvdata->mlli_sram_addr,
			      req_ctx->mlli_params.mlli_len);
		set_flow_mode(&desc[*seq_size], BYPASS);
		(*seq_size)++;

		hw_desc_init(&desc[*seq_size]);
		set_din_type(&desc[*seq_size], DMA_MLLI,
			     ctx_p->drvdata->mlli_sram_addr,
			     req_ctx->in_mlli_nents, NS_BIT);
		if (req_ctx->out_nents == 0) {
			dev_dbg(dev, " din/dout params addr 0x%08X addr 0x%08X\n",
				(unsigned int)ctx_p->drvdata->mlli_sram_addr,
				(unsigned int)ctx_p->drvdata->mlli_sram_addr);
			set_dout_mlli(&desc[*seq_size],
				      ctx_p->drvdata->mlli_sram_addr,
				      req_ctx->in_mlli_nents, NS_BIT,
				      (!areq ? 0 : 1));
		} else {
			dev_dbg(dev, " din/dout params addr 0x%08X addr 0x%08X\n",
				(unsigned int)ctx_p->drvdata->mlli_sram_addr,
				(unsigned int)ctx_p->drvdata->mlli_sram_addr +
				(u32)LLI_ENTRY_BYTE_SIZE * req_ctx->in_nents);
			set_dout_mlli(&desc[*seq_size],
				      (ctx_p->drvdata->mlli_sram_addr +
				       (LLI_ENTRY_BYTE_SIZE *
					req_ctx->in_mlli_nents)),
				      req_ctx->out_mlli_nents, NS_BIT,
				      (!areq ? 0 : 1));
		}
		if (areq)
			set_queue_last_ind(&desc[*seq_size]);

		set_flow_mode(&desc[*seq_size], flow_mode);
		(*seq_size)++;
	}
}

static void cc_cipher_complete(struct device *dev, void *cc_req, int err)
{
	struct ablkcipher_request *areq = (struct ablkcipher_request *)cc_req;
	struct scatterlist *dst = areq->dst;
	struct scatterlist *src = areq->src;
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(areq);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(areq);
	unsigned int ivsize = crypto_ablkcipher_ivsize(tfm);
	struct ablkcipher_request *req = (struct ablkcipher_request *)areq;

	cc_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);
	kfree(req_ctx->iv);

	/*
	 * The crypto API expects us to set the req->info to the last
	 * ciphertext block. For encrypt, simply copy from the result.
	 * For decrypt, we must copy from a saved buffer since this
	 * could be an in-place decryption operation and the src is
	 * lost by this point.
	 */
	if (req_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT)  {
		memcpy(req->info, req_ctx->backup_info, ivsize);
		kfree(req_ctx->backup_info);
	} else if (!err) {
		scatterwalk_map_and_copy(req->info, req->dst,
					 (req->nbytes - ivsize), ivsize, 0);
	}

	ablkcipher_request_complete(areq, err);
}

static int cc_cipher_process(struct ablkcipher_request *req,
			     enum drv_crypto_direction direction)
{
	struct crypto_ablkcipher *ablk_tfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablk_tfm);
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablk_tfm);
	struct scatterlist *dst = req->dst;
	struct scatterlist *src = req->src;
	unsigned int nbytes = req->nbytes;
	void *info = req->info;
	struct cc_cipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx_p->drvdata);
	struct cc_hw_desc desc[MAX_ABLKCIPHER_SEQ_LEN];
	struct cc_crypto_req cc_req = {};
	int rc, seq_len = 0, cts_restore_flag = 0;
	gfp_t flags = cc_gfp_flags(&req->base);

	dev_dbg(dev, "%s req=%p info=%p nbytes=%d\n",
		((direction == DRV_CRYPTO_DIRECTION_ENCRYPT) ?
		"Encrypt" : "Decrypt"), req, info, nbytes);

	/* STAT_PHASE_0: Init and sanity checks */

	/* TODO: check data length according to mode */
	if (validate_data_size(ctx_p, nbytes)) {
		dev_err(dev, "Unsupported data size %d.\n", nbytes);
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_BLOCK_LEN);
		rc = -EINVAL;
		goto exit_process;
	}
	if (nbytes == 0) {
		/* No data to process is valid */
		rc = 0;
		goto exit_process;
	}

	/* The IV we are handed may be allocted from the stack so
	 * we must copy it to a DMAable buffer before use.
	 */
	req_ctx->iv = kmalloc(ivsize, flags);
	if (!req_ctx->iv) {
		rc = -ENOMEM;
		goto exit_process;
	}
	memcpy(req_ctx->iv, info, ivsize);

	/*For CTS in case of data size aligned to 16 use CBC mode*/
	if (((nbytes % AES_BLOCK_SIZE) == 0) &&
	    ctx_p->cipher_mode == DRV_CIPHER_CBC_CTS) {
		ctx_p->cipher_mode = DRV_CIPHER_CBC;
		cts_restore_flag = 1;
	}

	/* Setup DX request structure */
	cc_req.user_cb = (void *)cc_cipher_complete;
	cc_req.user_arg = (void *)req;

#ifdef ENABLE_CYCLE_COUNT
	cc_req.op_type = (direction == DRV_CRYPTO_DIRECTION_DECRYPT) ?
		STAT_OP_TYPE_DECODE : STAT_OP_TYPE_ENCODE;

#endif

	/* Setup request context */
	req_ctx->gen_ctx.op_type = direction;

	/* STAT_PHASE_1: Map buffers */

	rc = cc_map_blkcipher_request(ctx_p->drvdata, req_ctx, ivsize, nbytes,
				      req_ctx->iv, src, dst, flags);
	if (rc) {
		dev_err(dev, "map_request() failed\n");
		goto exit_process;
	}

	/* STAT_PHASE_2: Create sequence */

	/* Setup processing */
	cc_setup_cipher_desc(tfm, req_ctx, ivsize, nbytes, desc, &seq_len);
	/* Data processing */
	cc_setup_cipher_data(tfm, req_ctx, dst, src, nbytes, req, desc,
			     &seq_len);

	/* do we need to generate IV? */
	if (req_ctx->is_giv) {
		cc_req.ivgen_dma_addr[0] = req_ctx->gen_ctx.iv_dma_addr;
		cc_req.ivgen_dma_addr_len = 1;
		/* set the IV size (8/16 B long)*/
		cc_req.ivgen_size = ivsize;
	}

	/* STAT_PHASE_3: Lock HW and push sequence */

	rc = cc_send_request(ctx_p->drvdata, &cc_req, desc, seq_len,
			     &req->base);
	if (rc != -EINPROGRESS && rc != -EBUSY) {
		/* Failed to send the request or request completed
		 * synchronously
		 */
		cc_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);
	}

exit_process:
	if (cts_restore_flag)
		ctx_p->cipher_mode = DRV_CIPHER_CBC_CTS;

	if (rc != -EINPROGRESS && rc != -EBUSY) {
		kfree(req_ctx->backup_info);
		kfree(req_ctx->iv);
	}

	return rc;
}

static int cc_cipher_encrypt(struct ablkcipher_request *req)
{
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->is_giv = false;
	req_ctx->backup_info = NULL;

	return cc_cipher_process(req, DRV_CRYPTO_DIRECTION_ENCRYPT);
}

static int cc_cipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *ablk_tfm = crypto_ablkcipher_reqtfm(req);
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablk_tfm);
	gfp_t flags = cc_gfp_flags(&req->base);

	/*
	 * Allocate and save the last IV sized bytes of the source, which will
	 * be lost in case of in-place decryption and might be needed for CTS.
	 */
	req_ctx->backup_info = kmalloc(ivsize, flags);
	if (!req_ctx->backup_info)
		return -ENOMEM;

	scatterwalk_map_and_copy(req_ctx->backup_info, req->src,
				 (req->nbytes - ivsize), ivsize, 0);
	req_ctx->is_giv = false;

	return cc_cipher_process(req, DRV_CRYPTO_DIRECTION_DECRYPT);
}

/* DX Block cipher alg */
static struct cc_alg_template blkcipher_algs[] = {
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			.geniv = "eseqiv",
			},
		.cipher_mode = DRV_CIPHER_XTS,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-du512-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_512,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_XTS,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-du4096-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_4096,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_XTS,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "essiv(aes)",
		.driver_name = "essiv-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_ESSIV,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "essiv(aes)",
		.driver_name = "essiv-aes-du512-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_512,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_ESSIV,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "essiv(aes)",
		.driver_name = "essiv-aes-du4096-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_4096,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_ESSIV,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "bitlocker(aes)",
		.driver_name = "bitlocker-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_BITLOCKER,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "bitlocker(aes)",
		.driver_name = "bitlocker-aes-du512-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_512,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_BITLOCKER,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "bitlocker(aes)",
		.driver_name = "bitlocker-aes-du4096-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_BULK_DU_4096,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_BITLOCKER,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "ecb(aes)",
		.driver_name = "ecb-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = 0,
			},
		.cipher_mode = DRV_CIPHER_ECB,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "cbc(aes)",
		.driver_name = "cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "ofb(aes)",
		.driver_name = "ofb-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_OFB,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "cts1(cbc(aes))",
		.driver_name = "cts1-cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_CBC_CTS,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "ctr(aes)",
		.driver_name = "ctr-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_CTR,
		.flow_mode = S_DIN_to_AES,
	},
	{
		.name = "cbc(des3_ede)",
		.driver_name = "cbc-3des-dx",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_DES,
	},
	{
		.name = "ecb(des3_ede)",
		.driver_name = "ecb-3des-dx",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = 0,
			},
		.cipher_mode = DRV_CIPHER_ECB,
		.flow_mode = S_DIN_to_DES,
	},
	{
		.name = "cbc(des)",
		.driver_name = "cbc-des-dx",
		.blocksize = DES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_DES,
	},
	{
		.name = "ecb(des)",
		.driver_name = "ecb-des-dx",
		.blocksize = DES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = cc_cipher_setkey,
			.encrypt = cc_cipher_encrypt,
			.decrypt = cc_cipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = 0,
			},
		.cipher_mode = DRV_CIPHER_ECB,
		.flow_mode = S_DIN_to_DES,
	},
};

static
struct cc_crypto_alg *cc_cipher_create_alg(struct cc_alg_template *template,
					   struct device *dev)
{
	struct cc_crypto_alg *t_alg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg)
		return ERR_PTR(-ENOMEM);

	alg = &t_alg->crypto_alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", template->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 template->driver_name);
	alg->cra_module = THIS_MODULE;
	alg->cra_priority = CC_CRA_PRIO;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_ctxsize = sizeof(struct cc_cipher_ctx);

	alg->cra_init = cc_cipher_init;
	alg->cra_exit = cc_cipher_exit;
	alg->cra_type = &crypto_ablkcipher_type;
	alg->cra_ablkcipher = template->template_ablkcipher;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
				template->type;

	t_alg->cipher_mode = template->cipher_mode;
	t_alg->flow_mode = template->flow_mode;

	return t_alg;
}

int cc_cipher_free(struct cc_drvdata *drvdata)
{
	struct cc_crypto_alg *t_alg, *n;
	struct cc_cipher_handle *blkcipher_handle = drvdata->blkcipher_handle;

	if (blkcipher_handle) {
		/* Remove registered algs */
		list_for_each_entry_safe(t_alg, n,
					 &blkcipher_handle->blkcipher_alg_list,
					 entry) {
			crypto_unregister_alg(&t_alg->crypto_alg);
			list_del(&t_alg->entry);
			kfree(t_alg);
		}
		kfree(blkcipher_handle);
		drvdata->blkcipher_handle = NULL;
	}
	return 0;
}

int cc_cipher_alloc(struct cc_drvdata *drvdata)
{
	struct cc_cipher_handle *ablkcipher_handle;
	struct cc_crypto_alg *t_alg;
	struct device *dev = drvdata_to_dev(drvdata);
	int rc = -ENOMEM;
	int alg;

	ablkcipher_handle = kmalloc(sizeof(*ablkcipher_handle), GFP_KERNEL);
	if (!ablkcipher_handle)
		return -ENOMEM;

	INIT_LIST_HEAD(&ablkcipher_handle->blkcipher_alg_list);
	drvdata->blkcipher_handle = ablkcipher_handle;

	/* Linux crypto */
	dev_dbg(dev, "Number of algorithms = %zu\n",
		ARRAY_SIZE(blkcipher_algs));
	for (alg = 0; alg < ARRAY_SIZE(blkcipher_algs); alg++) {
		dev_dbg(dev, "creating %s\n", blkcipher_algs[alg].driver_name);
		t_alg = cc_cipher_create_alg(&blkcipher_algs[alg], dev);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			dev_err(dev, "%s alg allocation failed\n",
				blkcipher_algs[alg].driver_name);
			goto fail0;
		}
		t_alg->drvdata = drvdata;

		dev_dbg(dev, "registering %s\n",
			blkcipher_algs[alg].driver_name);
		rc = crypto_register_alg(&t_alg->crypto_alg);
		dev_dbg(dev, "%s alg registration rc = %x\n",
			t_alg->crypto_alg.cra_driver_name, rc);
		if (rc) {
			dev_err(dev, "%s alg registration failed\n",
				t_alg->crypto_alg.cra_driver_name);
			kfree(t_alg);
			goto fail0;
		} else {
			list_add_tail(&t_alg->entry,
				      &ablkcipher_handle->blkcipher_alg_list);
			dev_dbg(dev, "Registered %s\n",
				t_alg->crypto_alg.cra_driver_name);
		}
	}
	return 0;

fail0:
	cc_cipher_free(drvdata);
	return rc;
}
