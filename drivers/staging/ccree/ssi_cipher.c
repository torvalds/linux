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
#include <linux/semaphore.h>
#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/des.h>
#include <crypto/xts.h>

#include "ssi_config.h"
#include "ssi_driver.h"
#include "cc_lli_defs.h"
#include "ssi_buffer_mgr.h"
#include "ssi_cipher.h"
#include "ssi_request_mgr.h"
#include "ssi_sysfs.h"

#define MAX_ABLKCIPHER_SEQ_LEN 6

#define template_ablkcipher	template_u.ablkcipher

#define SSI_MIN_AES_XTS_SIZE 0x10
#define SSI_MAX_AES_XTS_SIZE 0x2000
struct ssi_blkcipher_handle {
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

struct ssi_ablkcipher_ctx {
	struct ssi_drvdata *drvdata;
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

static void ssi_ablkcipher_complete(struct device *dev, void *ssi_req, void __iomem *cc_base);

static int validate_keys_sizes(struct ssi_ablkcipher_ctx *ctx_p, u32 size)
{
	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		switch (size) {
		case CC_AES_128_BIT_KEY_SIZE:
		case CC_AES_192_BIT_KEY_SIZE:
			if (likely((ctx_p->cipher_mode != DRV_CIPHER_XTS) &&
				   (ctx_p->cipher_mode != DRV_CIPHER_ESSIV) &&
				   (ctx_p->cipher_mode != DRV_CIPHER_BITLOCKER)))
				return 0;
			break;
		case CC_AES_256_BIT_KEY_SIZE:
			return 0;
		case (CC_AES_192_BIT_KEY_SIZE * 2):
		case (CC_AES_256_BIT_KEY_SIZE * 2):
			if (likely((ctx_p->cipher_mode == DRV_CIPHER_XTS) ||
				   (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) ||
				   (ctx_p->cipher_mode == DRV_CIPHER_BITLOCKER)))
				return 0;
			break;
		default:
			break;
		}
	case S_DIN_to_DES:
		if (likely(size == DES3_EDE_KEY_SIZE ||
		    size == DES_KEY_SIZE))
			return 0;
		break;
#if SSI_CC_HAS_MULTI2
	case S_DIN_to_MULTI2:
		if (likely(size == CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE))
			return 0;
		break;
#endif
	default:
		break;
	}
	return -EINVAL;
}

static int validate_data_size(struct ssi_ablkcipher_ctx *ctx_p, unsigned int size)
{
	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		switch (ctx_p->cipher_mode) {
		case DRV_CIPHER_XTS:
			if ((size >= SSI_MIN_AES_XTS_SIZE) &&
			    (size <= SSI_MAX_AES_XTS_SIZE) &&
			    IS_ALIGNED(size, AES_BLOCK_SIZE))
				return 0;
			break;
		case DRV_CIPHER_CBC_CTS:
			if (likely(size >= AES_BLOCK_SIZE))
				return 0;
			break;
		case DRV_CIPHER_OFB:
		case DRV_CIPHER_CTR:
				return 0;
		case DRV_CIPHER_ECB:
		case DRV_CIPHER_CBC:
		case DRV_CIPHER_ESSIV:
		case DRV_CIPHER_BITLOCKER:
			if (likely(IS_ALIGNED(size, AES_BLOCK_SIZE)))
				return 0;
			break;
		default:
			break;
		}
		break;
	case S_DIN_to_DES:
		if (likely(IS_ALIGNED(size, DES_BLOCK_SIZE)))
				return 0;
		break;
#if SSI_CC_HAS_MULTI2
	case S_DIN_to_MULTI2:
		switch (ctx_p->cipher_mode) {
		case DRV_MULTI2_CBC:
			if (likely(IS_ALIGNED(size, CC_MULTI2_BLOCK_SIZE)))
				return 0;
			break;
		case DRV_MULTI2_OFB:
			return 0;
		default:
			break;
		}
		break;
#endif /*SSI_CC_HAS_MULTI2*/
	default:
		break;
	}
	return -EINVAL;
}

static unsigned int get_max_keysize(struct crypto_tfm *tfm)
{
	struct ssi_crypto_alg *ssi_alg = container_of(tfm->__crt_alg, struct ssi_crypto_alg, crypto_alg);

	if ((ssi_alg->crypto_alg.cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_ABLKCIPHER)
		return ssi_alg->crypto_alg.cra_ablkcipher.max_keysize;

	if ((ssi_alg->crypto_alg.cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_BLKCIPHER)
		return ssi_alg->crypto_alg.cra_blkcipher.max_keysize;

	return 0;
}

static int ssi_blkcipher_init(struct crypto_tfm *tfm)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct ssi_crypto_alg *ssi_alg =
			container_of(alg, struct ssi_crypto_alg, crypto_alg);
	struct device *dev;
	int rc = 0;
	unsigned int max_key_buf_size = get_max_keysize(tfm);

	SSI_LOG_DEBUG("Initializing context @%p for %s\n", ctx_p,
						crypto_tfm_alg_name(tfm));

	ctx_p->cipher_mode = ssi_alg->cipher_mode;
	ctx_p->flow_mode = ssi_alg->flow_mode;
	ctx_p->drvdata = ssi_alg->drvdata;
	dev = &ctx_p->drvdata->plat_dev->dev;

	/* Allocate key buffer, cache line aligned */
	ctx_p->user.key = kmalloc(max_key_buf_size, GFP_KERNEL | GFP_DMA);
	if (!ctx_p->user.key) {
		SSI_LOG_ERR("Allocating key buffer in context failed\n");
		rc = -ENOMEM;
	}
	SSI_LOG_DEBUG("Allocated key buffer in context. key=@%p\n",
		      ctx_p->user.key);

	/* Map key buffer */
	ctx_p->user.key_dma_addr = dma_map_single(dev, (void *)ctx_p->user.key,
					     max_key_buf_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, ctx_p->user.key_dma_addr)) {
		SSI_LOG_ERR("Mapping Key %u B at va=%pK for DMA failed\n",
			max_key_buf_size, ctx_p->user.key);
		return -ENOMEM;
	}
	SSI_LOG_DEBUG("Mapped key %u B at va=%pK to dma=0x%llX\n",
		max_key_buf_size, ctx_p->user.key,
		(unsigned long long)ctx_p->user.key_dma_addr);

	if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
		/* Alloc hash tfm for essiv */
		ctx_p->shash_tfm = crypto_alloc_shash("sha256-generic", 0, 0);
		if (IS_ERR(ctx_p->shash_tfm)) {
			SSI_LOG_ERR("Error allocating hash tfm for ESSIV.\n");
			return PTR_ERR(ctx_p->shash_tfm);
		}
	}

	return rc;
}

static void ssi_blkcipher_exit(struct crypto_tfm *tfm)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = &ctx_p->drvdata->plat_dev->dev;
	unsigned int max_key_buf_size = get_max_keysize(tfm);

	SSI_LOG_DEBUG("Clearing context @%p for %s\n",
		crypto_tfm_ctx(tfm), crypto_tfm_alg_name(tfm));

	if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
		/* Free hash tfm for essiv */
		crypto_free_shash(ctx_p->shash_tfm);
		ctx_p->shash_tfm = NULL;
	}

	/* Unmap key buffer */
	dma_unmap_single(dev, ctx_p->user.key_dma_addr, max_key_buf_size,
								DMA_TO_DEVICE);
	SSI_LOG_DEBUG("Unmapped key buffer key_dma_addr=0x%llX\n",
		(unsigned long long)ctx_p->user.key_dma_addr);

	/* Free key buffer in context */
	kfree(ctx_p->user.key);
	SSI_LOG_DEBUG("Free key buffer in context. key=@%p\n", ctx_p->user.key);
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
static int ssi_verify_3des_keys(const u8 *key, unsigned int keylen)
{
	struct tdes_keys *tdes_key = (struct tdes_keys *)key;

	/* verify key1 != key2 and key3 != key2*/
	if (unlikely((memcmp((u8 *)tdes_key->key1, (u8 *)tdes_key->key2, sizeof(tdes_key->key1)) == 0) ||
		      (memcmp((u8 *)tdes_key->key3, (u8 *)tdes_key->key2, sizeof(tdes_key->key3)) == 0))) {
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

static int ssi_blkcipher_setkey(struct crypto_tfm *tfm,
				const u8 *key,
				unsigned int keylen)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = &ctx_p->drvdata->plat_dev->dev;
	u32 tmp[DES_EXPKEY_WORDS];
	unsigned int max_key_buf_size = get_max_keysize(tfm);

	SSI_LOG_DEBUG("Setting key in context @%p for %s. keylen=%u\n",
		ctx_p, crypto_tfm_alg_name(tfm), keylen);
	dump_byte_array("key", (u8 *)key, keylen);

	SSI_LOG_DEBUG("ssi_blkcipher_setkey: after FIPS check");

	/* STAT_PHASE_0: Init and sanity checks */

#if SSI_CC_HAS_MULTI2
	/*last byte of key buffer is round number and should not be a part of key size*/
	if (ctx_p->flow_mode == S_DIN_to_MULTI2)
		keylen -= 1;
#endif /*SSI_CC_HAS_MULTI2*/

	if (unlikely(validate_keys_sizes(ctx_p, keylen) != 0)) {
		SSI_LOG_ERR("Unsupported key size %d.\n", keylen);
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (ssi_is_hw_key(tfm)) {
		/* setting HW key slots */
		struct arm_hw_key_info *hki = (struct arm_hw_key_info *)key;

		if (unlikely(ctx_p->flow_mode != S_DIN_to_AES)) {
			SSI_LOG_ERR("HW key not supported for non-AES flows\n");
			return -EINVAL;
		}

		ctx_p->hw.key1_slot = hw_key_to_cc_hw_key(hki->hw_key1);
		if (unlikely(ctx_p->hw.key1_slot == END_OF_KEYS)) {
			SSI_LOG_ERR("Unsupported hw key1 number (%d)\n", hki->hw_key1);
			return -EINVAL;
		}

		if ((ctx_p->cipher_mode == DRV_CIPHER_XTS) ||
		    (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) ||
		    (ctx_p->cipher_mode == DRV_CIPHER_BITLOCKER)) {
			if (unlikely(hki->hw_key1 == hki->hw_key2)) {
				SSI_LOG_ERR("Illegal hw key numbers (%d,%d)\n", hki->hw_key1, hki->hw_key2);
				return -EINVAL;
			}
			ctx_p->hw.key2_slot = hw_key_to_cc_hw_key(hki->hw_key2);
			if (unlikely(ctx_p->hw.key2_slot == END_OF_KEYS)) {
				SSI_LOG_ERR("Unsupported hw key2 number (%d)\n", hki->hw_key2);
				return -EINVAL;
			}
		}

		ctx_p->keylen = keylen;
		SSI_LOG_DEBUG("ssi_blkcipher_setkey: ssi_is_hw_key ret 0");

		return 0;
	}

	// verify weak keys
	if (ctx_p->flow_mode == S_DIN_to_DES) {
		if (unlikely(!des_ekey(tmp, key)) &&
		    (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			SSI_LOG_DEBUG("ssi_blkcipher_setkey:  weak DES key");
			return -EINVAL;
		}
	}
	if ((ctx_p->cipher_mode == DRV_CIPHER_XTS) &&
	    xts_check_key(tfm, key, keylen) != 0) {
		SSI_LOG_DEBUG("ssi_blkcipher_setkey: weak XTS key");
		return -EINVAL;
	}
	if ((ctx_p->flow_mode == S_DIN_to_DES) &&
	    (keylen == DES3_EDE_KEY_SIZE) &&
	    ssi_verify_3des_keys(key, keylen) != 0) {
		SSI_LOG_DEBUG("ssi_blkcipher_setkey: weak 3DES key");
		return -EINVAL;
	}

	/* STAT_PHASE_1: Copy key to ctx */
	dma_sync_single_for_cpu(dev, ctx_p->user.key_dma_addr,
					max_key_buf_size, DMA_TO_DEVICE);

	if (ctx_p->flow_mode == S_DIN_to_MULTI2) {
#if SSI_CC_HAS_MULTI2
		memcpy(ctx_p->user.key, key, CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE);
		ctx_p->key_round_number = key[CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE];
		if (ctx_p->key_round_number < CC_MULTI2_MIN_NUM_ROUNDS ||
		    ctx_p->key_round_number > CC_MULTI2_MAX_NUM_ROUNDS) {
			crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
			SSI_LOG_DEBUG("ssi_blkcipher_setkey: SSI_CC_HAS_MULTI2 einval");
			return -EINVAL;
#endif /*SSI_CC_HAS_MULTI2*/
	} else {
		memcpy(ctx_p->user.key, key, keylen);
		if (keylen == 24)
			memset(ctx_p->user.key + 24, 0, CC_AES_KEY_SIZE_MAX - 24);

		if (ctx_p->cipher_mode == DRV_CIPHER_ESSIV) {
			/* sha256 for key2 - use sw implementation */
			int key_len = keylen >> 1;
			int err;
			SHASH_DESC_ON_STACK(desc, ctx_p->shash_tfm);

			desc->tfm = ctx_p->shash_tfm;

			err = crypto_shash_digest(desc, ctx_p->user.key, key_len, ctx_p->user.key + key_len);
			if (err) {
				SSI_LOG_ERR("Failed to hash ESSIV key.\n");
				return err;
			}
		}
	}
	dma_sync_single_for_device(dev, ctx_p->user.key_dma_addr,
					max_key_buf_size, DMA_TO_DEVICE);
	ctx_p->keylen = keylen;

	 SSI_LOG_DEBUG("ssi_blkcipher_setkey: return safely");
	return 0;
}

static inline void
ssi_blkcipher_create_setup_desc(
	struct crypto_tfm *tfm,
	struct blkcipher_req_ctx *req_ctx,
	unsigned int ivsize,
	unsigned int nbytes,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	int cipher_mode = ctx_p->cipher_mode;
	int flow_mode = ctx_p->flow_mode;
	int direction = req_ctx->gen_ctx.op_type;
	dma_addr_t key_dma_addr = ctx_p->user.key_dma_addr;
	unsigned int key_len = ctx_p->keylen;
	dma_addr_t iv_dma_addr = req_ctx->gen_ctx.iv_dma_addr;
	unsigned int du_size = nbytes;

	struct ssi_crypto_alg *ssi_alg = container_of(tfm->__crt_alg, struct ssi_crypto_alg, crypto_alg);

	if ((ssi_alg->crypto_alg.cra_flags & CRYPTO_ALG_BULK_MASK) == CRYPTO_ALG_BULK_DU_512)
		du_size = 512;
	if ((ssi_alg->crypto_alg.cra_flags & CRYPTO_ALG_BULK_MASK) == CRYPTO_ALG_BULK_DU_4096)
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
		if ((cipher_mode == DRV_CIPHER_CTR) ||
		    (cipher_mode == DRV_CIPHER_OFB)) {
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
			if (ssi_is_hw_key(tfm)) {
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
		if (ssi_is_hw_key(tfm)) {
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
		if (ssi_is_hw_key(tfm)) {
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
		SSI_LOG_ERR("Unsupported cipher mode (%d)\n", cipher_mode);
		BUG();
	}
}

#if SSI_CC_HAS_MULTI2
static inline void ssi_blkcipher_create_multi2_setup_desc(
	struct crypto_tfm *tfm,
	struct blkcipher_req_ctx *req_ctx,
	unsigned int ivsize,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);

	int direction = req_ctx->gen_ctx.op_type;
	/* Load system key */
	hw_desc_init(&desc[*seq_size]);
	set_cipher_mode(&desc[*seq_size], ctx_p->cipher_mode);
	set_cipher_config0(&desc[*seq_size], direction);
	set_din_type(&desc[*seq_size], DMA_DLLI, ctx_p->user.key_dma_addr,
		     CC_MULTI2_SYSTEM_KEY_SIZE, NS_BIT);
	set_flow_mode(&desc[*seq_size], ctx_p->flow_mode);
	set_setup_mode(&desc[*seq_size], SETUP_LOAD_KEY0);
	(*seq_size)++;

	/* load data key */
	hw_desc_init(&desc[*seq_size]);
	set_din_type(&desc[*seq_size], DMA_DLLI,
		     (ctx_p->user.key_dma_addr + CC_MULTI2_SYSTEM_KEY_SIZE),
		     CC_MULTI2_DATA_KEY_SIZE, NS_BIT);
	set_multi2_num_rounds(&desc[*seq_size], ctx_p->key_round_number);
	set_flow_mode(&desc[*seq_size], ctx_p->flow_mode);
	set_cipher_mode(&desc[*seq_size], ctx_p->cipher_mode);
	set_cipher_config0(&desc[*seq_size], direction);
	set_setup_mode(&desc[*seq_size], SETUP_LOAD_STATE0);
	(*seq_size)++;

	/* Set state */
	hw_desc_init(&desc[*seq_size]);
	set_din_type(&desc[*seq_size], DMA_DLLI, req_ctx->gen_ctx.iv_dma_addr,
		     ivsize, NS_BIT);
	set_cipher_config0(&desc[*seq_size], direction);
	set_flow_mode(&desc[*seq_size], ctx_p->flow_mode);
	set_cipher_mode(&desc[*seq_size], ctx_p->cipher_mode);
	set_setup_mode(&desc[*seq_size], SETUP_LOAD_STATE1);
	(*seq_size)++;
}
#endif /*SSI_CC_HAS_MULTI2*/

static inline void
ssi_blkcipher_create_data_desc(
	struct crypto_tfm *tfm,
	struct blkcipher_req_ctx *req_ctx,
	struct scatterlist *dst, struct scatterlist *src,
	unsigned int nbytes,
	void *areq,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	unsigned int flow_mode = ctx_p->flow_mode;

	switch (ctx_p->flow_mode) {
	case S_DIN_to_AES:
		flow_mode = DIN_AES_DOUT;
		break;
	case S_DIN_to_DES:
		flow_mode = DIN_DES_DOUT;
		break;
#if SSI_CC_HAS_MULTI2
	case S_DIN_to_MULTI2:
		flow_mode = DIN_MULTI2_DOUT;
		break;
#endif /*SSI_CC_HAS_MULTI2*/
	default:
		SSI_LOG_ERR("invalid flow mode, flow_mode = %d \n", flow_mode);
		return;
	}
	/* Process */
	if (likely(req_ctx->dma_buf_type == SSI_DMA_BUF_DLLI)) {
		SSI_LOG_DEBUG(" data params addr 0x%llX length 0x%X \n",
			     (unsigned long long)sg_dma_address(src),
			     nbytes);
		SSI_LOG_DEBUG(" data params addr 0x%llX length 0x%X \n",
			     (unsigned long long)sg_dma_address(dst),
			     nbytes);
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
		SSI_LOG_DEBUG(" bypass params addr 0x%llX "
			     "length 0x%X addr 0x%08X\n",
			(unsigned long long)req_ctx->mlli_params.mlli_dma_addr,
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
			SSI_LOG_DEBUG(" din/dout params addr 0x%08X "
				     "addr 0x%08X\n",
			(unsigned int)ctx_p->drvdata->mlli_sram_addr,
			(unsigned int)ctx_p->drvdata->mlli_sram_addr);
			set_dout_mlli(&desc[*seq_size],
				      ctx_p->drvdata->mlli_sram_addr,
				      req_ctx->in_mlli_nents, NS_BIT,
				      (!areq ? 0 : 1));
		} else {
			SSI_LOG_DEBUG(" din/dout params "
				     "addr 0x%08X addr 0x%08X\n",
				(unsigned int)ctx_p->drvdata->mlli_sram_addr,
				(unsigned int)ctx_p->drvdata->mlli_sram_addr +
				(u32)LLI_ENTRY_BYTE_SIZE *
							req_ctx->in_nents);
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

static int ssi_blkcipher_complete(struct device *dev,
				struct ssi_ablkcipher_ctx *ctx_p,
				struct blkcipher_req_ctx *req_ctx,
				struct scatterlist *dst,
				struct scatterlist *src,
				unsigned int ivsize,
				void *areq,
				void __iomem *cc_base)
{
	int completion_error = 0;
	u32 inflight_counter;

	ssi_buffer_mgr_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);

	/*Set the inflight couter value to local variable*/
	inflight_counter =  ctx_p->drvdata->inflight_counter;
	/*Decrease the inflight counter*/
	if (ctx_p->flow_mode == BYPASS && ctx_p->drvdata->inflight_counter > 0)
		ctx_p->drvdata->inflight_counter--;

	if (areq) {
		ablkcipher_request_complete(areq, completion_error);
		return 0;
	}
	return completion_error;
}

static int ssi_blkcipher_process(
	struct crypto_tfm *tfm,
	struct blkcipher_req_ctx *req_ctx,
	struct scatterlist *dst, struct scatterlist *src,
	unsigned int nbytes,
	void *info, //req info
	unsigned int ivsize,
	void *areq,
	enum drv_crypto_direction direction)
{
	struct ssi_ablkcipher_ctx *ctx_p = crypto_tfm_ctx(tfm);
	struct device *dev = &ctx_p->drvdata->plat_dev->dev;
	struct cc_hw_desc desc[MAX_ABLKCIPHER_SEQ_LEN];
	struct ssi_crypto_req ssi_req = {};
	int rc, seq_len = 0, cts_restore_flag = 0;

	SSI_LOG_DEBUG("%s areq=%p info=%p nbytes=%d\n",
		((direction == DRV_CRYPTO_DIRECTION_ENCRYPT) ? "Encrypt" : "Decrypt"),
		     areq, info, nbytes);

	/* STAT_PHASE_0: Init and sanity checks */

	/* TODO: check data length according to mode */
	if (unlikely(validate_data_size(ctx_p, nbytes))) {
		SSI_LOG_ERR("Unsupported data size %d.\n", nbytes);
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_BLOCK_LEN);
		return -EINVAL;
	}
	if (nbytes == 0) {
		/* No data to process is valid */
		return 0;
	}
	/*For CTS in case of data size aligned to 16 use CBC mode*/
	if (((nbytes % AES_BLOCK_SIZE) == 0) && (ctx_p->cipher_mode == DRV_CIPHER_CBC_CTS)) {
		ctx_p->cipher_mode = DRV_CIPHER_CBC;
		cts_restore_flag = 1;
	}

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_ablkcipher_complete;
	ssi_req.user_arg = (void *)areq;

#ifdef ENABLE_CYCLE_COUNT
	ssi_req.op_type = (direction == DRV_CRYPTO_DIRECTION_DECRYPT) ?
		STAT_OP_TYPE_DECODE : STAT_OP_TYPE_ENCODE;

#endif

	/* Setup request context */
	req_ctx->gen_ctx.op_type = direction;

	/* STAT_PHASE_1: Map buffers */

	rc = ssi_buffer_mgr_map_blkcipher_request(ctx_p->drvdata, req_ctx, ivsize, nbytes, info, src, dst);
	if (unlikely(rc != 0)) {
		SSI_LOG_ERR("map_request() failed\n");
		goto exit_process;
	}

	/* STAT_PHASE_2: Create sequence */

	/* Setup processing */
#if SSI_CC_HAS_MULTI2
	if (ctx_p->flow_mode == S_DIN_to_MULTI2)
		ssi_blkcipher_create_multi2_setup_desc(tfm, req_ctx, ivsize,
						       desc, &seq_len);
	else
#endif /*SSI_CC_HAS_MULTI2*/
		ssi_blkcipher_create_setup_desc(tfm, req_ctx, ivsize, nbytes,
						desc, &seq_len);
	/* Data processing */
	ssi_blkcipher_create_data_desc(tfm,
			      req_ctx,
			      dst, src,
			      nbytes,
			      areq,
			      desc, &seq_len);

	/* do we need to generate IV? */
	if (req_ctx->is_giv) {
		ssi_req.ivgen_dma_addr[0] = req_ctx->gen_ctx.iv_dma_addr;
		ssi_req.ivgen_dma_addr_len = 1;
		/* set the IV size (8/16 B long)*/
		ssi_req.ivgen_size = ivsize;
	}

	/* STAT_PHASE_3: Lock HW and push sequence */

	rc = send_request(ctx_p->drvdata, &ssi_req, desc, seq_len, (!areq) ? 0 : 1);
	if (areq) {
		if (unlikely(rc != -EINPROGRESS)) {
			/* Failed to send the request or request completed synchronously */
			ssi_buffer_mgr_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);
		}

	} else {
		if (rc != 0) {
			ssi_buffer_mgr_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);
		} else {
			rc = ssi_blkcipher_complete(dev, ctx_p, req_ctx, dst,
						    src, ivsize, NULL,
						    ctx_p->drvdata->cc_base);
		}
	}

exit_process:
	if (cts_restore_flag != 0)
		ctx_p->cipher_mode = DRV_CIPHER_CBC_CTS;

	return rc;
}

static void ssi_ablkcipher_complete(struct device *dev, void *ssi_req, void __iomem *cc_base)
{
	struct ablkcipher_request *areq = (struct ablkcipher_request *)ssi_req;
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(areq);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(areq);
	struct ssi_ablkcipher_ctx *ctx_p = crypto_ablkcipher_ctx(tfm);
	unsigned int ivsize = crypto_ablkcipher_ivsize(tfm);

	ssi_blkcipher_complete(dev, ctx_p, req_ctx, areq->dst, areq->src,
			       ivsize, areq, cc_base);
}

/* Async wrap functions */

static int ssi_ablkcipher_init(struct crypto_tfm *tfm)
{
	struct ablkcipher_tfm *ablktfm = &tfm->crt_ablkcipher;

	ablktfm->reqsize = sizeof(struct blkcipher_req_ctx);

	return ssi_blkcipher_init(tfm);
}

static int ssi_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
				const u8 *key,
				unsigned int keylen)
{
	return ssi_blkcipher_setkey(crypto_ablkcipher_tfm(tfm), key, keylen);
}

static int ssi_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *ablk_tfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablk_tfm);
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablk_tfm);

	req_ctx->backup_info = req->info;
	req_ctx->is_giv = false;

	return ssi_blkcipher_process(tfm, req_ctx, req->dst, req->src, req->nbytes, req->info, ivsize, (void *)req, DRV_CRYPTO_DIRECTION_ENCRYPT);
}

static int ssi_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *ablk_tfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(ablk_tfm);
	struct blkcipher_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	unsigned int ivsize = crypto_ablkcipher_ivsize(ablk_tfm);

	req_ctx->backup_info = req->info;
	req_ctx->is_giv = false;
	return ssi_blkcipher_process(tfm, req_ctx, req->dst, req->src, req->nbytes, req->info, ivsize, (void *)req, DRV_CRYPTO_DIRECTION_DECRYPT);
}

/* DX Block cipher alg */
static struct ssi_alg_template blkcipher_algs[] = {
/* Async template */
#if SSI_CC_HAS_AES_XTS
	{
		.name = "xts(aes)",
		.driver_name = "xts-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_XTS,
		.flow_mode = S_DIN_to_AES,
	},
#endif /*SSI_CC_HAS_AES_XTS*/
#if SSI_CC_HAS_AES_ESSIV
	{
		.name = "essiv(aes)",
		.driver_name = "essiv-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_ESSIV,
		.flow_mode = S_DIN_to_AES,
	},
#endif /*SSI_CC_HAS_AES_ESSIV*/
#if SSI_CC_HAS_AES_BITLOCKER
	{
		.name = "bitlocker(aes)",
		.driver_name = "bitlocker-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE * 2,
			.max_keysize = AES_MAX_KEY_SIZE * 2,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_BITLOCKER,
		.flow_mode = S_DIN_to_AES,
	},
#endif /*SSI_CC_HAS_AES_BITLOCKER*/
	{
		.name = "ecb(aes)",
		.driver_name = "ecb-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_OFB,
		.flow_mode = S_DIN_to_AES,
	},
#if SSI_CC_HAS_AES_CTS
	{
		.name = "cts1(cbc(aes))",
		.driver_name = "cts1-cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
		.cipher_mode = DRV_CIPHER_CBC_CTS,
		.flow_mode = S_DIN_to_AES,
	},
#endif
	{
		.name = "ctr(aes)",
		.driver_name = "ctr-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
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
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = 0,
			},
		.cipher_mode = DRV_CIPHER_ECB,
		.flow_mode = S_DIN_to_DES,
	},
#if SSI_CC_HAS_MULTI2
	{
		.name = "cbc(multi2)",
		.driver_name = "cbc-multi2-dx",
		.blocksize = CC_MULTI2_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_decrypt,
			.min_keysize = CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE + 1,
			.max_keysize = CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE + 1,
			.ivsize = CC_MULTI2_IV_SIZE,
			},
		.cipher_mode = DRV_MULTI2_CBC,
		.flow_mode = S_DIN_to_MULTI2,
	},
	{
		.name = "ofb(multi2)",
		.driver_name = "ofb-multi2-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_ABLKCIPHER,
		.template_ablkcipher = {
			.setkey = ssi_ablkcipher_setkey,
			.encrypt = ssi_ablkcipher_encrypt,
			.decrypt = ssi_ablkcipher_encrypt,
			.min_keysize = CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE + 1,
			.max_keysize = CC_MULTI2_SYSTEM_N_DATA_KEY_SIZE + 1,
			.ivsize = CC_MULTI2_IV_SIZE,
			},
		.cipher_mode = DRV_MULTI2_OFB,
		.flow_mode = S_DIN_to_MULTI2,
	},
#endif /*SSI_CC_HAS_MULTI2*/
};

static
struct ssi_crypto_alg *ssi_ablkcipher_create_alg(struct ssi_alg_template *template)
{
	struct ssi_crypto_alg *t_alg;
	struct crypto_alg *alg;

	t_alg = kzalloc(sizeof(struct ssi_crypto_alg), GFP_KERNEL);
	if (!t_alg) {
		SSI_LOG_ERR("failed to allocate t_alg\n");
		return ERR_PTR(-ENOMEM);
	}

	alg = &t_alg->crypto_alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", template->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 template->driver_name);
	alg->cra_module = THIS_MODULE;
	alg->cra_priority = SSI_CRA_PRIO;
	alg->cra_blocksize = template->blocksize;
	alg->cra_alignmask = 0;
	alg->cra_ctxsize = sizeof(struct ssi_ablkcipher_ctx);

	alg->cra_init = ssi_ablkcipher_init;
	alg->cra_exit = ssi_blkcipher_exit;
	alg->cra_type = &crypto_ablkcipher_type;
	alg->cra_ablkcipher = template->template_ablkcipher;
	alg->cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
				template->type;

	t_alg->cipher_mode = template->cipher_mode;
	t_alg->flow_mode = template->flow_mode;

	return t_alg;
}

int ssi_ablkcipher_free(struct ssi_drvdata *drvdata)
{
	struct ssi_crypto_alg *t_alg, *n;
	struct ssi_blkcipher_handle *blkcipher_handle =
						drvdata->blkcipher_handle;
	struct device *dev;

	dev = &drvdata->plat_dev->dev;

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

int ssi_ablkcipher_alloc(struct ssi_drvdata *drvdata)
{
	struct ssi_blkcipher_handle *ablkcipher_handle;
	struct ssi_crypto_alg *t_alg;
	int rc = -ENOMEM;
	int alg;

	ablkcipher_handle = kmalloc(sizeof(struct ssi_blkcipher_handle),
		GFP_KERNEL);
	if (!ablkcipher_handle)
		return -ENOMEM;

	drvdata->blkcipher_handle = ablkcipher_handle;

	INIT_LIST_HEAD(&ablkcipher_handle->blkcipher_alg_list);

	/* Linux crypto */
	SSI_LOG_DEBUG("Number of algorithms = %zu\n", ARRAY_SIZE(blkcipher_algs));
	for (alg = 0; alg < ARRAY_SIZE(blkcipher_algs); alg++) {
		SSI_LOG_DEBUG("creating %s\n", blkcipher_algs[alg].driver_name);
		t_alg = ssi_ablkcipher_create_alg(&blkcipher_algs[alg]);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			SSI_LOG_ERR("%s alg allocation failed\n",
				 blkcipher_algs[alg].driver_name);
			goto fail0;
		}
		t_alg->drvdata = drvdata;

		SSI_LOG_DEBUG("registering %s\n", blkcipher_algs[alg].driver_name);
		rc = crypto_register_alg(&t_alg->crypto_alg);
		SSI_LOG_DEBUG("%s alg registration rc = %x\n",
			t_alg->crypto_alg.cra_driver_name, rc);
		if (unlikely(rc != 0)) {
			SSI_LOG_ERR("%s alg registration failed\n",
				t_alg->crypto_alg.cra_driver_name);
			kfree(t_alg);
			goto fail0;
		} else {
			list_add_tail(&t_alg->entry,
				      &ablkcipher_handle->blkcipher_alg_list);
			SSI_LOG_DEBUG("Registered %s\n",
					t_alg->crypto_alg.cra_driver_name);
		}
	}
	return 0;

fail0:
	ssi_ablkcipher_free(drvdata);
	return rc;
}
