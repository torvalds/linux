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
#include <crypto/internal/skcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/sha.h>
#include <crypto/ctr.h>
#include <crypto/authenc.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <linux/rtnetlink.h>
#include <linux/version.h>
#include "ssi_config.h"
#include "ssi_driver.h"
#include "ssi_buffer_mgr.h"
#include "ssi_aead.h"
#include "ssi_request_mgr.h"
#include "ssi_hash.h"
#include "ssi_sysfs.h"
#include "ssi_sram_mgr.h"

#define template_aead	template_u.aead

#define MAX_AEAD_SETKEY_SEQ 12
#define MAX_AEAD_PROCESS_SEQ 23

#define MAX_HMAC_DIGEST_SIZE (SHA256_DIGEST_SIZE)
#define MAX_HMAC_BLOCK_SIZE (SHA256_BLOCK_SIZE)

#define AES_CCM_RFC4309_NONCE_SIZE 3
#define MAX_NONCE_SIZE CTR_RFC3686_NONCE_SIZE

/* Value of each ICV_CMP byte (of 8) in case of success */
#define ICV_VERIF_OK 0x01

struct ssi_aead_handle {
	ssi_sram_addr_t sram_workspace_addr;
	struct list_head aead_list;
};

struct cc_hmac_s {
	u8 *padded_authkey;
	u8 *ipad_opad; /* IPAD, OPAD*/
	dma_addr_t padded_authkey_dma_addr;
	dma_addr_t ipad_opad_dma_addr;
};

struct cc_xcbc_s {
	u8 *xcbc_keys; /* K1,K2,K3 */
	dma_addr_t xcbc_keys_dma_addr;
};

struct ssi_aead_ctx {
	struct ssi_drvdata *drvdata;
	u8 ctr_nonce[MAX_NONCE_SIZE]; /* used for ctr3686 iv and aes ccm */
	u8 *enckey;
	dma_addr_t enckey_dma_addr;
	union {
		struct cc_hmac_s hmac;
		struct cc_xcbc_s xcbc;
	} auth_state;
	unsigned int enc_keylen;
	unsigned int auth_keylen;
	unsigned int authsize; /* Actual (reduced?) size of the MAC/ICv */
	enum drv_cipher_mode cipher_mode;
	enum cc_flow_mode flow_mode;
	enum drv_hash_mode auth_mode;
};

static inline bool valid_assoclen(struct aead_request *req)
{
	return ((req->assoclen == 16) || (req->assoclen == 20));
}

static void ssi_aead_exit(struct crypto_aead *tfm)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "Clearing context @%p for %s\n", crypto_aead_ctx(tfm),
		crypto_tfm_alg_name(&tfm->base));

	/* Unmap enckey buffer */
	if (ctx->enckey) {
		dma_free_coherent(dev, AES_MAX_KEY_SIZE, ctx->enckey, ctx->enckey_dma_addr);
		dev_dbg(dev, "Freed enckey DMA buffer enckey_dma_addr=%pad\n",
			&ctx->enckey_dma_addr);
		ctx->enckey_dma_addr = 0;
		ctx->enckey = NULL;
	}

	if (ctx->auth_mode == DRV_HASH_XCBC_MAC) { /* XCBC authetication */
		struct cc_xcbc_s *xcbc = &ctx->auth_state.xcbc;

		if (xcbc->xcbc_keys) {
			dma_free_coherent(dev, CC_AES_128_BIT_KEY_SIZE * 3,
					  xcbc->xcbc_keys,
					  xcbc->xcbc_keys_dma_addr);
		}
		dev_dbg(dev, "Freed xcbc_keys DMA buffer xcbc_keys_dma_addr=%pad\n",
			&xcbc->xcbc_keys_dma_addr);
		xcbc->xcbc_keys_dma_addr = 0;
		xcbc->xcbc_keys = NULL;
	} else if (ctx->auth_mode != DRV_HASH_NULL) { /* HMAC auth. */
		struct cc_hmac_s *hmac = &ctx->auth_state.hmac;

		if (hmac->ipad_opad) {
			dma_free_coherent(dev, 2 * MAX_HMAC_DIGEST_SIZE,
					  hmac->ipad_opad,
					  hmac->ipad_opad_dma_addr);
			dev_dbg(dev, "Freed ipad_opad DMA buffer ipad_opad_dma_addr=%pad\n",
				&hmac->ipad_opad_dma_addr);
			hmac->ipad_opad_dma_addr = 0;
			hmac->ipad_opad = NULL;
		}
		if (hmac->padded_authkey) {
			dma_free_coherent(dev, MAX_HMAC_BLOCK_SIZE,
					  hmac->padded_authkey,
					  hmac->padded_authkey_dma_addr);
			dev_dbg(dev, "Freed padded_authkey DMA buffer padded_authkey_dma_addr=%pad\n",
				&hmac->padded_authkey_dma_addr);
			hmac->padded_authkey_dma_addr = 0;
			hmac->padded_authkey = NULL;
		}
	}
}

static int ssi_aead_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct ssi_crypto_alg *ssi_alg =
			container_of(alg, struct ssi_crypto_alg, aead_alg);
	struct device *dev = drvdata_to_dev(ssi_alg->drvdata);

	dev_dbg(dev, "Initializing context @%p for %s\n", ctx,
		crypto_tfm_alg_name(&tfm->base));

	/* Initialize modes in instance */
	ctx->cipher_mode = ssi_alg->cipher_mode;
	ctx->flow_mode = ssi_alg->flow_mode;
	ctx->auth_mode = ssi_alg->auth_mode;
	ctx->drvdata = ssi_alg->drvdata;
	crypto_aead_set_reqsize(tfm, sizeof(struct aead_req_ctx));

	/* Allocate key buffer, cache line aligned */
	ctx->enckey = dma_alloc_coherent(dev, AES_MAX_KEY_SIZE,
					 &ctx->enckey_dma_addr, GFP_KERNEL);
	if (!ctx->enckey) {
		dev_err(dev, "Failed allocating key buffer\n");
		goto init_failed;
	}
	dev_dbg(dev, "Allocated enckey buffer in context ctx->enckey=@%p\n",
		ctx->enckey);

	/* Set default authlen value */

	if (ctx->auth_mode == DRV_HASH_XCBC_MAC) { /* XCBC authetication */
		struct cc_xcbc_s *xcbc = &ctx->auth_state.xcbc;
		const unsigned int key_size = CC_AES_128_BIT_KEY_SIZE * 3;

		/* Allocate dma-coherent buffer for XCBC's K1+K2+K3 */
		/* (and temporary for user key - up to 256b) */
		xcbc->xcbc_keys = dma_alloc_coherent(dev, key_size,
						     &xcbc->xcbc_keys_dma_addr,
						     GFP_KERNEL);
		if (!xcbc->xcbc_keys) {
			dev_err(dev, "Failed allocating buffer for XCBC keys\n");
			goto init_failed;
		}
	} else if (ctx->auth_mode != DRV_HASH_NULL) { /* HMAC authentication */
		struct cc_hmac_s *hmac = &ctx->auth_state.hmac;
		const unsigned int digest_size = 2 * MAX_HMAC_DIGEST_SIZE;
		dma_addr_t *pkey_dma = &hmac->padded_authkey_dma_addr;

		/* Allocate dma-coherent buffer for IPAD + OPAD */
		hmac->ipad_opad = dma_alloc_coherent(dev, digest_size,
						     &hmac->ipad_opad_dma_addr,
						     GFP_KERNEL);

		if (!hmac->ipad_opad) {
			dev_err(dev, "Failed allocating IPAD/OPAD buffer\n");
			goto init_failed;
		}

		dev_dbg(dev, "Allocated authkey buffer in context ctx->authkey=@%p\n",
			hmac->ipad_opad);

		hmac->padded_authkey = dma_alloc_coherent(dev,
							  MAX_HMAC_BLOCK_SIZE,
							  pkey_dma,
							  GFP_KERNEL);

		if (!hmac->padded_authkey) {
			dev_err(dev, "failed to allocate padded_authkey\n");
			goto init_failed;
		}
	} else {
		ctx->auth_state.hmac.ipad_opad = NULL;
		ctx->auth_state.hmac.padded_authkey = NULL;
	}

	return 0;

init_failed:
	ssi_aead_exit(tfm);
	return -ENOMEM;
}

static void ssi_aead_complete(struct device *dev, void *ssi_req, void __iomem *cc_base)
{
	struct aead_request *areq = (struct aead_request *)ssi_req;
	struct aead_req_ctx *areq_ctx = aead_request_ctx(areq);
	struct crypto_aead *tfm = crypto_aead_reqtfm(ssi_req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	int err = 0;

	ssi_buffer_mgr_unmap_aead_request(dev, areq);

	/* Restore ordinary iv pointer */
	areq->iv = areq_ctx->backup_iv;

	if (areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) {
		if (memcmp(areq_ctx->mac_buf, areq_ctx->icv_virt_addr,
			   ctx->authsize) != 0) {
			dev_dbg(dev, "Payload authentication failure, (auth-size=%d, cipher=%d)\n",
				ctx->authsize, ctx->cipher_mode);
			/* In case of payload authentication failure, MUST NOT
			 * revealed the decrypted message --> zero its memory.
			 */
			ssi_buffer_mgr_zero_sgl(areq->dst, areq_ctx->cryptlen);
			err = -EBADMSG;
		}
	} else { /*ENCRYPT*/
		if (unlikely(areq_ctx->is_icv_fragmented))
			ssi_buffer_mgr_copy_scatterlist_portion(
				dev, areq_ctx->mac_buf, areq_ctx->dst_sgl,
				areq->cryptlen + areq_ctx->dst_offset,
				(areq->cryptlen + areq_ctx->dst_offset +
				 ctx->authsize),
				SSI_SG_FROM_BUF);

		/* If an IV was generated, copy it back to the user provided buffer. */
		if (areq_ctx->backup_giv) {
			if (ctx->cipher_mode == DRV_CIPHER_CTR)
				memcpy(areq_ctx->backup_giv, areq_ctx->ctr_iv + CTR_RFC3686_NONCE_SIZE, CTR_RFC3686_IV_SIZE);
			else if (ctx->cipher_mode == DRV_CIPHER_CCM)
				memcpy(areq_ctx->backup_giv, areq_ctx->ctr_iv + CCM_BLOCK_IV_OFFSET, CCM_BLOCK_IV_SIZE);
		}
	}

	aead_request_complete(areq, err);
}

static int xcbc_setkey(struct cc_hw_desc *desc, struct ssi_aead_ctx *ctx)
{
	/* Load the AES key */
	hw_desc_init(&desc[0]);
	/* We are using for the source/user key the same buffer as for the output keys,
	 * because after this key loading it is not needed anymore
	 */
	set_din_type(&desc[0], DMA_DLLI,
		     ctx->auth_state.xcbc.xcbc_keys_dma_addr, ctx->auth_keylen,
		     NS_BIT);
	set_cipher_mode(&desc[0], DRV_CIPHER_ECB);
	set_cipher_config0(&desc[0], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_key_size_aes(&desc[0], ctx->auth_keylen);
	set_flow_mode(&desc[0], S_DIN_to_AES);
	set_setup_mode(&desc[0], SETUP_LOAD_KEY0);

	hw_desc_init(&desc[1]);
	set_din_const(&desc[1], 0x01010101, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[1], DIN_AES_DOUT);
	set_dout_dlli(&desc[1], ctx->auth_state.xcbc.xcbc_keys_dma_addr,
		      AES_KEYSIZE_128, NS_BIT, 0);

	hw_desc_init(&desc[2]);
	set_din_const(&desc[2], 0x02020202, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[2], DIN_AES_DOUT);
	set_dout_dlli(&desc[2], (ctx->auth_state.xcbc.xcbc_keys_dma_addr
					 + AES_KEYSIZE_128),
			      AES_KEYSIZE_128, NS_BIT, 0);

	hw_desc_init(&desc[3]);
	set_din_const(&desc[3], 0x03030303, CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[3], DIN_AES_DOUT);
	set_dout_dlli(&desc[3], (ctx->auth_state.xcbc.xcbc_keys_dma_addr
					  + 2 * AES_KEYSIZE_128),
			      AES_KEYSIZE_128, NS_BIT, 0);

	return 4;
}

static int hmac_setkey(struct cc_hw_desc *desc, struct ssi_aead_ctx *ctx)
{
	unsigned int hmac_pad_const[2] = { HMAC_IPAD_CONST, HMAC_OPAD_CONST };
	unsigned int digest_ofs = 0;
	unsigned int hash_mode = (ctx->auth_mode == DRV_HASH_SHA1) ?
			DRV_HASH_HW_SHA1 : DRV_HASH_HW_SHA256;
	unsigned int digest_size = (ctx->auth_mode == DRV_HASH_SHA1) ?
			CC_SHA1_DIGEST_SIZE : CC_SHA256_DIGEST_SIZE;
	struct cc_hmac_s *hmac = &ctx->auth_state.hmac;

	int idx = 0;
	int i;

	/* calc derived HMAC key */
	for (i = 0; i < 2; i++) {
		/* Load hash initial state */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], hash_mode);
		set_din_sram(&desc[idx],
			     ssi_ahash_get_larval_digest_sram_addr(
				ctx->drvdata, ctx->auth_mode),
			     digest_size);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
		idx++;

		/* Load the hash current length*/
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], hash_mode);
		set_din_const(&desc[idx], 0, HASH_LEN_SIZE);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
		idx++;

		/* Prepare ipad key */
		hw_desc_init(&desc[idx]);
		set_xor_val(&desc[idx], hmac_pad_const[i]);
		set_cipher_mode(&desc[idx], hash_mode);
		set_flow_mode(&desc[idx], S_DIN_to_HASH);
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
		idx++;

		/* Perform HASH update */
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     hmac->padded_authkey_dma_addr,
			     SHA256_BLOCK_SIZE, NS_BIT);
		set_cipher_mode(&desc[idx], hash_mode);
		set_xor_active(&desc[idx]);
		set_flow_mode(&desc[idx], DIN_HASH);
		idx++;

		/* Get the digset */
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], hash_mode);
		set_dout_dlli(&desc[idx],
			      (hmac->ipad_opad_dma_addr + digest_ofs),
			      digest_size, NS_BIT, 0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
		idx++;

		digest_ofs += digest_size;
	}

	return idx;
}

static int validate_keys_sizes(struct ssi_aead_ctx *ctx)
{
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "enc_keylen=%u  authkeylen=%u\n",
		ctx->enc_keylen, ctx->auth_keylen);

	switch (ctx->auth_mode) {
	case DRV_HASH_SHA1:
	case DRV_HASH_SHA256:
		break;
	case DRV_HASH_XCBC_MAC:
		if ((ctx->auth_keylen != AES_KEYSIZE_128) &&
		    (ctx->auth_keylen != AES_KEYSIZE_192) &&
		    (ctx->auth_keylen != AES_KEYSIZE_256))
			return -ENOTSUPP;
		break;
	case DRV_HASH_NULL: /* Not authenc (e.g., CCM) - no auth_key) */
		if (ctx->auth_keylen > 0)
			return -EINVAL;
		break;
	default:
		dev_err(dev, "Invalid auth_mode=%d\n", ctx->auth_mode);
		return -EINVAL;
	}
	/* Check cipher key size */
	if (unlikely(ctx->flow_mode == S_DIN_to_DES)) {
		if (ctx->enc_keylen != DES3_EDE_KEY_SIZE) {
			dev_err(dev, "Invalid cipher(3DES) key size: %u\n",
				ctx->enc_keylen);
			return -EINVAL;
		}
	} else { /* Default assumed to be AES ciphers */
		if ((ctx->enc_keylen != AES_KEYSIZE_128) &&
		    (ctx->enc_keylen != AES_KEYSIZE_192) &&
		    (ctx->enc_keylen != AES_KEYSIZE_256)) {
			dev_err(dev, "Invalid cipher(AES) key size: %u\n",
				ctx->enc_keylen);
			return -EINVAL;
		}
	}

	return 0; /* All tests of keys sizes passed */
}

/* This function prepers the user key so it can pass to the hmac processing
 * (copy to intenral buffer or hash in case of key longer than block
 */
static int
ssi_get_plain_hmac_key(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	dma_addr_t key_dma_addr = 0;
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	u32 larval_addr = ssi_ahash_get_larval_digest_sram_addr(
					ctx->drvdata, ctx->auth_mode);
	struct ssi_crypto_req ssi_req = {};
	unsigned int blocksize;
	unsigned int digestsize;
	unsigned int hashmode;
	unsigned int idx = 0;
	int rc = 0;
	struct cc_hw_desc desc[MAX_AEAD_SETKEY_SEQ];
	dma_addr_t padded_authkey_dma_addr =
		ctx->auth_state.hmac.padded_authkey_dma_addr;

	switch (ctx->auth_mode) { /* auth_key required and >0 */
	case DRV_HASH_SHA1:
		blocksize = SHA1_BLOCK_SIZE;
		digestsize = SHA1_DIGEST_SIZE;
		hashmode = DRV_HASH_HW_SHA1;
		break;
	case DRV_HASH_SHA256:
	default:
		blocksize = SHA256_BLOCK_SIZE;
		digestsize = SHA256_DIGEST_SIZE;
		hashmode = DRV_HASH_HW_SHA256;
	}

	if (likely(keylen != 0)) {
		key_dma_addr = dma_map_single(dev, (void *)key, keylen, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, key_dma_addr))) {
			dev_err(dev, "Mapping key va=0x%p len=%u for DMA failed\n",
				key, keylen);
			return -ENOMEM;
		}
		if (keylen > blocksize) {
			/* Load hash initial state */
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], hashmode);
			set_din_sram(&desc[idx], larval_addr, digestsize);
			set_flow_mode(&desc[idx], S_DIN_to_HASH);
			set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
			idx++;

			/* Load the hash current length*/
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], hashmode);
			set_din_const(&desc[idx], 0, HASH_LEN_SIZE);
			set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
			set_flow_mode(&desc[idx], S_DIN_to_HASH);
			set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
			idx++;

			hw_desc_init(&desc[idx]);
			set_din_type(&desc[idx], DMA_DLLI,
				     key_dma_addr, keylen, NS_BIT);
			set_flow_mode(&desc[idx], DIN_HASH);
			idx++;

			/* Get hashed key */
			hw_desc_init(&desc[idx]);
			set_cipher_mode(&desc[idx], hashmode);
			set_dout_dlli(&desc[idx], padded_authkey_dma_addr,
				      digestsize, NS_BIT, 0);
			set_flow_mode(&desc[idx], S_HASH_to_DOUT);
			set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
			set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
			set_cipher_config0(&desc[idx],
					   HASH_DIGEST_RESULT_LITTLE_ENDIAN);
			idx++;

			hw_desc_init(&desc[idx]);
			set_din_const(&desc[idx], 0, (blocksize - digestsize));
			set_flow_mode(&desc[idx], BYPASS);
			set_dout_dlli(&desc[idx], (padded_authkey_dma_addr +
				      digestsize), (blocksize - digestsize),
				      NS_BIT, 0);
			idx++;
		} else {
			hw_desc_init(&desc[idx]);
			set_din_type(&desc[idx], DMA_DLLI, key_dma_addr,
				     keylen, NS_BIT);
			set_flow_mode(&desc[idx], BYPASS);
			set_dout_dlli(&desc[idx], padded_authkey_dma_addr,
				      keylen, NS_BIT, 0);
			idx++;

			if ((blocksize - keylen) != 0) {
				hw_desc_init(&desc[idx]);
				set_din_const(&desc[idx], 0,
					      (blocksize - keylen));
				set_flow_mode(&desc[idx], BYPASS);
				set_dout_dlli(&desc[idx],
					      (padded_authkey_dma_addr +
					       keylen),
					      (blocksize - keylen), NS_BIT, 0);
				idx++;
			}
		}
	} else {
		hw_desc_init(&desc[idx]);
		set_din_const(&desc[idx], 0, (blocksize - keylen));
		set_flow_mode(&desc[idx], BYPASS);
		set_dout_dlli(&desc[idx], padded_authkey_dma_addr,
			      blocksize, NS_BIT, 0);
		idx++;
	}

	rc = send_request(ctx->drvdata, &ssi_req, desc, idx, 0);
	if (unlikely(rc != 0))
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);

	if (likely(key_dma_addr != 0))
		dma_unmap_single(dev, key_dma_addr, keylen, DMA_TO_DEVICE);

	return rc;
}

static int
ssi_aead_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct rtattr *rta = (struct rtattr *)key;
	struct ssi_crypto_req ssi_req = {};
	struct crypto_authenc_key_param *param;
	struct cc_hw_desc desc[MAX_AEAD_SETKEY_SEQ];
	int seq_len = 0, rc = -EINVAL;
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "Setting key in context @%p for %s. key=%p keylen=%u\n",
		ctx, crypto_tfm_alg_name(crypto_aead_tfm(tfm)), key, keylen);

	/* STAT_PHASE_0: Init and sanity checks */

	if (ctx->auth_mode != DRV_HASH_NULL) { /* authenc() alg. */
		if (!RTA_OK(rta, keylen))
			goto badkey;
		if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
			goto badkey;
		if (RTA_PAYLOAD(rta) < sizeof(*param))
			goto badkey;
		param = RTA_DATA(rta);
		ctx->enc_keylen = be32_to_cpu(param->enckeylen);
		key += RTA_ALIGN(rta->rta_len);
		keylen -= RTA_ALIGN(rta->rta_len);
		if (keylen < ctx->enc_keylen)
			goto badkey;
		ctx->auth_keylen = keylen - ctx->enc_keylen;

		if (ctx->cipher_mode == DRV_CIPHER_CTR) {
			/* the nonce is stored in bytes at end of key */
			if (ctx->enc_keylen <
			    (AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE))
				goto badkey;
			/* Copy nonce from last 4 bytes in CTR key to
			 *  first 4 bytes in CTR IV
			 */
			memcpy(ctx->ctr_nonce, key + ctx->auth_keylen + ctx->enc_keylen -
				CTR_RFC3686_NONCE_SIZE, CTR_RFC3686_NONCE_SIZE);
			/* Set CTR key size */
			ctx->enc_keylen -= CTR_RFC3686_NONCE_SIZE;
		}
	} else { /* non-authenc - has just one key */
		ctx->enc_keylen = keylen;
		ctx->auth_keylen = 0;
	}

	rc = validate_keys_sizes(ctx);
	if (unlikely(rc != 0))
		goto badkey;

	/* STAT_PHASE_1: Copy key to ctx */

	/* Get key material */
	memcpy(ctx->enckey, key + ctx->auth_keylen, ctx->enc_keylen);
	if (ctx->enc_keylen == 24)
		memset(ctx->enckey + 24, 0, CC_AES_KEY_SIZE_MAX - 24);
	if (ctx->auth_mode == DRV_HASH_XCBC_MAC) {
		memcpy(ctx->auth_state.xcbc.xcbc_keys, key, ctx->auth_keylen);
	} else if (ctx->auth_mode != DRV_HASH_NULL) { /* HMAC */
		rc = ssi_get_plain_hmac_key(tfm, key, ctx->auth_keylen);
		if (rc != 0)
			goto badkey;
	}

	/* STAT_PHASE_2: Create sequence */

	switch (ctx->auth_mode) {
	case DRV_HASH_SHA1:
	case DRV_HASH_SHA256:
		seq_len = hmac_setkey(desc, ctx);
		break;
	case DRV_HASH_XCBC_MAC:
		seq_len = xcbc_setkey(desc, ctx);
		break;
	case DRV_HASH_NULL: /* non-authenc modes, e.g., CCM */
		break; /* No auth. key setup */
	default:
		dev_err(dev, "Unsupported authenc (%d)\n", ctx->auth_mode);
		rc = -ENOTSUPP;
		goto badkey;
	}

	/* STAT_PHASE_3: Submit sequence to HW */

	if (seq_len > 0) { /* For CCM there is no sequence to setup the key */
		rc = send_request(ctx->drvdata, &ssi_req, desc, seq_len, 0);
		if (unlikely(rc != 0)) {
			dev_err(dev, "send_request() failed (rc=%d)\n", rc);
			goto setkey_error;
		}
	}

	/* Update STAT_PHASE_3 */
	return rc;

badkey:
	crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);

setkey_error:
	return rc;
}

#if SSI_CC_HAS_AES_CCM
static int ssi_rfc4309_ccm_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);

	if (keylen < 3)
		return -EINVAL;

	keylen -= 3;
	memcpy(ctx->ctr_nonce, key + keylen, 3);

	return ssi_aead_setkey(tfm, key, keylen);
}
#endif /*SSI_CC_HAS_AES_CCM*/

static int ssi_aead_setauthsize(
	struct crypto_aead *authenc,
	unsigned int authsize)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(authenc);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	/* Unsupported auth. sizes */
	if ((authsize == 0) ||
	    (authsize > crypto_aead_maxauthsize(authenc))) {
		return -ENOTSUPP;
	}

	ctx->authsize = authsize;
	dev_dbg(dev, "authlen=%d\n", ctx->authsize);

	return 0;
}

#if SSI_CC_HAS_AES_CCM
static int ssi_rfc4309_ccm_setauthsize(struct crypto_aead *authenc,
				       unsigned int authsize)
{
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return ssi_aead_setauthsize(authenc, authsize);
}

static int ssi_ccm_setauthsize(struct crypto_aead *authenc,
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
		break;
	default:
		return -EINVAL;
	}

	return ssi_aead_setauthsize(authenc, authsize);
}
#endif /*SSI_CC_HAS_AES_CCM*/

static inline void
ssi_aead_create_assoc_desc(
	struct aead_request *areq,
	unsigned int flow_mode,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(areq);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(areq);
	enum ssi_req_dma_buf_type assoc_dma_type = areq_ctx->assoc_buff_type;
	unsigned int idx = *seq_size;
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	switch (assoc_dma_type) {
	case SSI_DMA_BUF_DLLI:
		dev_dbg(dev, "ASSOC buffer type DLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI, sg_dma_address(areq->src),
			     areq->assoclen, NS_BIT); set_flow_mode(&desc[idx],
			     flow_mode);
		if ((ctx->auth_mode == DRV_HASH_XCBC_MAC) &&
		    (areq_ctx->cryptlen > 0))
			set_din_not_last_indication(&desc[idx]);
		break;
	case SSI_DMA_BUF_MLLI:
		dev_dbg(dev, "ASSOC buffer type MLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_MLLI, areq_ctx->assoc.sram_addr,
			     areq_ctx->assoc.mlli_nents, NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		if ((ctx->auth_mode == DRV_HASH_XCBC_MAC) &&
		    (areq_ctx->cryptlen > 0))
			set_din_not_last_indication(&desc[idx]);
		break;
	case SSI_DMA_BUF_NULL:
	default:
		dev_err(dev, "Invalid ASSOC buffer type\n");
	}

	*seq_size = (++idx);
}

static inline void
ssi_aead_process_authenc_data_desc(
	struct aead_request *areq,
	unsigned int flow_mode,
	struct cc_hw_desc desc[],
	unsigned int *seq_size,
	int direct)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(areq);
	enum ssi_req_dma_buf_type data_dma_type = areq_ctx->data_buff_type;
	unsigned int idx = *seq_size;
	struct crypto_aead *tfm = crypto_aead_reqtfm(areq);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	switch (data_dma_type) {
	case SSI_DMA_BUF_DLLI:
	{
		struct scatterlist *cipher =
			(direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ?
			areq_ctx->dst_sgl : areq_ctx->src_sgl;

		unsigned int offset =
			(direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ?
			areq_ctx->dst_offset : areq_ctx->src_offset;
		dev_dbg(dev, "AUTHENC: SRC/DST buffer type DLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     (sg_dma_address(cipher) + offset),
			     areq_ctx->cryptlen, NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		break;
	}
	case SSI_DMA_BUF_MLLI:
	{
		/* DOUBLE-PASS flow (as default)
		 * assoc. + iv + data -compact in one table
		 * if assoclen is ZERO only IV perform
		 */
		ssi_sram_addr_t mlli_addr = areq_ctx->assoc.sram_addr;
		u32 mlli_nents = areq_ctx->assoc.mlli_nents;

		if (likely(areq_ctx->is_single_pass)) {
			if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
				mlli_addr = areq_ctx->dst.sram_addr;
				mlli_nents = areq_ctx->dst.mlli_nents;
			} else {
				mlli_addr = areq_ctx->src.sram_addr;
				mlli_nents = areq_ctx->src.mlli_nents;
			}
		}

		dev_dbg(dev, "AUTHENC: SRC/DST buffer type MLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_MLLI, mlli_addr, mlli_nents,
			     NS_BIT);
		set_flow_mode(&desc[idx], flow_mode);
		break;
	}
	case SSI_DMA_BUF_NULL:
	default:
		dev_err(dev, "AUTHENC: Invalid SRC/DST buffer type\n");
	}

	*seq_size = (++idx);
}

static inline void
ssi_aead_process_cipher_data_desc(
	struct aead_request *areq,
	unsigned int flow_mode,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	struct aead_req_ctx *areq_ctx = aead_request_ctx(areq);
	enum ssi_req_dma_buf_type data_dma_type = areq_ctx->data_buff_type;
	struct crypto_aead *tfm = crypto_aead_reqtfm(areq);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	if (areq_ctx->cryptlen == 0)
		return; /*null processing*/

	switch (data_dma_type) {
	case SSI_DMA_BUF_DLLI:
		dev_dbg(dev, "CIPHER: SRC/DST buffer type DLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     (sg_dma_address(areq_ctx->src_sgl) +
			      areq_ctx->src_offset), areq_ctx->cryptlen, NS_BIT);
		set_dout_dlli(&desc[idx],
			      (sg_dma_address(areq_ctx->dst_sgl) +
			       areq_ctx->dst_offset),
			      areq_ctx->cryptlen, NS_BIT, 0);
		set_flow_mode(&desc[idx], flow_mode);
		break;
	case SSI_DMA_BUF_MLLI:
		dev_dbg(dev, "CIPHER: SRC/DST buffer type MLLI\n");
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_MLLI, areq_ctx->src.sram_addr,
			     areq_ctx->src.mlli_nents, NS_BIT);
		set_dout_mlli(&desc[idx], areq_ctx->dst.sram_addr,
			      areq_ctx->dst.mlli_nents, NS_BIT, 0);
		set_flow_mode(&desc[idx], flow_mode);
		break;
	case SSI_DMA_BUF_NULL:
	default:
		dev_err(dev, "CIPHER: Invalid SRC/DST buffer type\n");
	}

	*seq_size = (++idx);
}

static inline void ssi_aead_process_digest_result_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int idx = *seq_size;
	unsigned int hash_mode = (ctx->auth_mode == DRV_HASH_SHA1) ?
				DRV_HASH_HW_SHA1 : DRV_HASH_HW_SHA256;
	int direct = req_ctx->gen_ctx.op_type;

	/* Get final ICV result */
	if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
		hw_desc_init(&desc[idx]);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		set_dout_dlli(&desc[idx], req_ctx->icv_dma_addr, ctx->authsize,
			      NS_BIT, 1);
		set_queue_last_ind(&desc[idx]);
		if (ctx->auth_mode == DRV_HASH_XCBC_MAC) {
			set_aes_not_hash_mode(&desc[idx]);
			set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
		} else {
			set_cipher_config0(&desc[idx],
					   HASH_DIGEST_RESULT_LITTLE_ENDIAN);
			set_cipher_mode(&desc[idx], hash_mode);
		}
	} else { /*Decrypt*/
		/* Get ICV out from hardware */
		hw_desc_init(&desc[idx]);
		set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
		set_flow_mode(&desc[idx], S_HASH_to_DOUT);
		set_dout_dlli(&desc[idx], req_ctx->mac_buf_dma_addr,
			      ctx->authsize, NS_BIT, 1);
		set_queue_last_ind(&desc[idx]);
		set_cipher_config0(&desc[idx],
				   HASH_DIGEST_RESULT_LITTLE_ENDIAN);
		set_cipher_config1(&desc[idx], HASH_PADDING_DISABLED);
		if (ctx->auth_mode == DRV_HASH_XCBC_MAC) {
			set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
			set_aes_not_hash_mode(&desc[idx]);
		} else {
			set_cipher_mode(&desc[idx], hash_mode);
		}
	}

	*seq_size = (++idx);
}

static inline void ssi_aead_setup_cipher_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int hw_iv_size = req_ctx->hw_iv_size;
	unsigned int idx = *seq_size;
	int direct = req_ctx->gen_ctx.op_type;

	/* Setup cipher state */
	hw_desc_init(&desc[idx]);
	set_cipher_config0(&desc[idx], direct);
	set_flow_mode(&desc[idx], ctx->flow_mode);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->gen_ctx.iv_dma_addr,
		     hw_iv_size, NS_BIT);
	if (ctx->cipher_mode == DRV_CIPHER_CTR)
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	else
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	set_cipher_mode(&desc[idx], ctx->cipher_mode);
	idx++;

	/* Setup enc. key */
	hw_desc_init(&desc[idx]);
	set_cipher_config0(&desc[idx], direct);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_flow_mode(&desc[idx], ctx->flow_mode);
	if (ctx->flow_mode == S_DIN_to_AES) {
		set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
			     ((ctx->enc_keylen == 24) ? CC_AES_KEY_SIZE_MAX :
			      ctx->enc_keylen), NS_BIT);
		set_key_size_aes(&desc[idx], ctx->enc_keylen);
	} else {
		set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
			     ctx->enc_keylen, NS_BIT);
		set_key_size_des(&desc[idx], ctx->enc_keylen);
	}
	set_cipher_mode(&desc[idx], ctx->cipher_mode);
	idx++;

	*seq_size = idx;
}

static inline void ssi_aead_process_cipher(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size,
	unsigned int data_flow_mode)
{
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	int direct = req_ctx->gen_ctx.op_type;
	unsigned int idx = *seq_size;

	if (req_ctx->cryptlen == 0)
		return; /*null processing*/

	ssi_aead_setup_cipher_desc(req, desc, &idx);
	ssi_aead_process_cipher_data_desc(req, data_flow_mode, desc, &idx);
	if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
		/* We must wait for DMA to write all cipher */
		hw_desc_init(&desc[idx]);
		set_din_no_dma(&desc[idx], 0, 0xfffff0);
		set_dout_no_dma(&desc[idx], 0, 0, 1);
		idx++;
	}

	*seq_size = idx;
}

static inline void ssi_aead_hmac_setup_digest_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned int hash_mode = (ctx->auth_mode == DRV_HASH_SHA1) ?
				DRV_HASH_HW_SHA1 : DRV_HASH_HW_SHA256;
	unsigned int digest_size = (ctx->auth_mode == DRV_HASH_SHA1) ?
				CC_SHA1_DIGEST_SIZE : CC_SHA256_DIGEST_SIZE;
	unsigned int idx = *seq_size;

	/* Loading hash ipad xor key state */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], hash_mode);
	set_din_type(&desc[idx], DMA_DLLI,
		     ctx->auth_state.hmac.ipad_opad_dma_addr, digest_size,
		     NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load init. digest len (64 bytes) */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], hash_mode);
	set_din_sram(&desc[idx],
		     ssi_ahash_get_initial_digest_len_sram_addr(ctx->drvdata,
								hash_mode),
								HASH_LEN_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	*seq_size = idx;
}

static inline void ssi_aead_xcbc_setup_digest_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned int idx = *seq_size;

	/* Loading MAC state */
	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0, CC_AES_BLOCK_SIZE);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* Setup XCBC MAC K1 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI,
		     ctx->auth_state.xcbc.xcbc_keys_dma_addr,
		     AES_KEYSIZE_128, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* Setup XCBC MAC K2 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI,
		     (ctx->auth_state.xcbc.xcbc_keys_dma_addr +
		      AES_KEYSIZE_128), AES_KEYSIZE_128, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* Setup XCBC MAC K3 */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI,
		     (ctx->auth_state.xcbc.xcbc_keys_dma_addr +
		      2 * AES_KEYSIZE_128), AES_KEYSIZE_128, NS_BIT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE2);
	set_cipher_mode(&desc[idx], DRV_CIPHER_XCBC_MAC);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_key_size_aes(&desc[idx], CC_AES_128_BIT_KEY_SIZE);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	*seq_size = idx;
}

static inline void ssi_aead_process_digest_header_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	unsigned int idx = *seq_size;
	/* Hash associated data */
	if (req->assoclen > 0)
		ssi_aead_create_assoc_desc(req, DIN_HASH, desc, &idx);

	/* Hash IV */
	*seq_size = idx;
}

static inline void ssi_aead_process_digest_scheme_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct ssi_aead_handle *aead_handle = ctx->drvdata->aead_handle;
	unsigned int hash_mode = (ctx->auth_mode == DRV_HASH_SHA1) ?
				DRV_HASH_HW_SHA1 : DRV_HASH_HW_SHA256;
	unsigned int digest_size = (ctx->auth_mode == DRV_HASH_SHA1) ?
				CC_SHA1_DIGEST_SIZE : CC_SHA256_DIGEST_SIZE;
	unsigned int idx = *seq_size;

	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], hash_mode);
	set_dout_sram(&desc[idx], aead_handle->sram_workspace_addr,
		      HASH_LEN_SIZE);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE1);
	set_cipher_do(&desc[idx], DO_PAD);
	idx++;

	/* Get final ICV result */
	hw_desc_init(&desc[idx]);
	set_dout_sram(&desc[idx], aead_handle->sram_workspace_addr,
		      digest_size);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	set_cipher_mode(&desc[idx], hash_mode);
	idx++;

	/* Loading hash opad xor key state */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], hash_mode);
	set_din_type(&desc[idx], DMA_DLLI,
		     (ctx->auth_state.hmac.ipad_opad_dma_addr + digest_size),
		     digest_size, NS_BIT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	/* Load init. digest len (64 bytes) */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], hash_mode);
	set_din_sram(&desc[idx],
		     ssi_ahash_get_initial_digest_len_sram_addr(ctx->drvdata,
								hash_mode),
		     HASH_LEN_SIZE);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Perform HASH update */
	hw_desc_init(&desc[idx]);
	set_din_sram(&desc[idx], aead_handle->sram_workspace_addr,
		     digest_size);
	set_flow_mode(&desc[idx], DIN_HASH);
	idx++;

	*seq_size = idx;
}

static inline void ssi_aead_load_mlli_to_sram(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	if (unlikely(
		(req_ctx->assoc_buff_type == SSI_DMA_BUF_MLLI) ||
		(req_ctx->data_buff_type == SSI_DMA_BUF_MLLI) ||
		!req_ctx->is_single_pass)) {
		dev_dbg(dev, "Copy-to-sram: mlli_dma=%08x, mlli_size=%u\n",
			(unsigned int)ctx->drvdata->mlli_sram_addr,
			req_ctx->mlli_params.mlli_len);
		/* Copy MLLI table host-to-sram */
		hw_desc_init(&desc[*seq_size]);
		set_din_type(&desc[*seq_size], DMA_DLLI,
			     req_ctx->mlli_params.mlli_dma_addr,
			     req_ctx->mlli_params.mlli_len, NS_BIT);
		set_dout_sram(&desc[*seq_size],
			      ctx->drvdata->mlli_sram_addr,
			      req_ctx->mlli_params.mlli_len);
		set_flow_mode(&desc[*seq_size], BYPASS);
		(*seq_size)++;
	}
}

static inline enum cc_flow_mode ssi_aead_get_data_flow_mode(
	enum drv_crypto_direction direct,
	enum cc_flow_mode setup_flow_mode,
	bool is_single_pass)
{
	enum cc_flow_mode data_flow_mode;

	if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
		if (setup_flow_mode == S_DIN_to_AES)
			data_flow_mode = likely(is_single_pass) ?
				AES_to_HASH_and_DOUT : DIN_AES_DOUT;
		else
			data_flow_mode = likely(is_single_pass) ?
				DES_to_HASH_and_DOUT : DIN_DES_DOUT;
	} else { /* Decrypt */
		if (setup_flow_mode == S_DIN_to_AES)
			data_flow_mode = likely(is_single_pass) ?
					AES_and_HASH : DIN_AES_DOUT;
		else
			data_flow_mode = likely(is_single_pass) ?
					DES_and_HASH : DIN_DES_DOUT;
	}

	return data_flow_mode;
}

static inline void ssi_aead_hmac_authenc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	int direct = req_ctx->gen_ctx.op_type;
	unsigned int data_flow_mode = ssi_aead_get_data_flow_mode(
		direct, ctx->flow_mode, req_ctx->is_single_pass);

	if (req_ctx->is_single_pass) {
		/**
		 * Single-pass flow
		 */
		ssi_aead_hmac_setup_digest_desc(req, desc, seq_size);
		ssi_aead_setup_cipher_desc(req, desc, seq_size);
		ssi_aead_process_digest_header_desc(req, desc, seq_size);
		ssi_aead_process_cipher_data_desc(req, data_flow_mode, desc, seq_size);
		ssi_aead_process_digest_scheme_desc(req, desc, seq_size);
		ssi_aead_process_digest_result_desc(req, desc, seq_size);
		return;
	}

	/**
	 * Double-pass flow
	 * Fallback for unsupported single-pass modes,
	 * i.e. using assoc. data of non-word-multiple
	 */
	if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
		/* encrypt first.. */
		ssi_aead_process_cipher(req, desc, seq_size, data_flow_mode);
		/* authenc after..*/
		ssi_aead_hmac_setup_digest_desc(req, desc, seq_size);
		ssi_aead_process_authenc_data_desc(req, DIN_HASH, desc, seq_size, direct);
		ssi_aead_process_digest_scheme_desc(req, desc, seq_size);
		ssi_aead_process_digest_result_desc(req, desc, seq_size);

	} else { /*DECRYPT*/
		/* authenc first..*/
		ssi_aead_hmac_setup_digest_desc(req, desc, seq_size);
		ssi_aead_process_authenc_data_desc(req, DIN_HASH, desc, seq_size, direct);
		ssi_aead_process_digest_scheme_desc(req, desc, seq_size);
		/* decrypt after.. */
		ssi_aead_process_cipher(req, desc, seq_size, data_flow_mode);
		/* read the digest result with setting the completion bit
		 * must be after the cipher operation
		 */
		ssi_aead_process_digest_result_desc(req, desc, seq_size);
	}
}

static inline void
ssi_aead_xcbc_authenc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	int direct = req_ctx->gen_ctx.op_type;
	unsigned int data_flow_mode = ssi_aead_get_data_flow_mode(
		direct, ctx->flow_mode, req_ctx->is_single_pass);

	if (req_ctx->is_single_pass) {
		/**
		 * Single-pass flow
		 */
		ssi_aead_xcbc_setup_digest_desc(req, desc, seq_size);
		ssi_aead_setup_cipher_desc(req, desc, seq_size);
		ssi_aead_process_digest_header_desc(req, desc, seq_size);
		ssi_aead_process_cipher_data_desc(req, data_flow_mode, desc, seq_size);
		ssi_aead_process_digest_result_desc(req, desc, seq_size);
		return;
	}

	/**
	 * Double-pass flow
	 * Fallback for unsupported single-pass modes,
	 * i.e. using assoc. data of non-word-multiple
	 */
	if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) {
		/* encrypt first.. */
		ssi_aead_process_cipher(req, desc, seq_size, data_flow_mode);
		/* authenc after.. */
		ssi_aead_xcbc_setup_digest_desc(req, desc, seq_size);
		ssi_aead_process_authenc_data_desc(req, DIN_HASH, desc, seq_size, direct);
		ssi_aead_process_digest_result_desc(req, desc, seq_size);
	} else { /*DECRYPT*/
		/* authenc first.. */
		ssi_aead_xcbc_setup_digest_desc(req, desc, seq_size);
		ssi_aead_process_authenc_data_desc(req, DIN_HASH, desc, seq_size, direct);
		/* decrypt after..*/
		ssi_aead_process_cipher(req, desc, seq_size, data_flow_mode);
		/* read the digest result with setting the completion bit
		 * must be after the cipher operation
		 */
		ssi_aead_process_digest_result_desc(req, desc, seq_size);
	}
}

static int validate_data_size(struct ssi_aead_ctx *ctx,
			      enum drv_crypto_direction direct,
			      struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	unsigned int assoclen = req->assoclen;
	unsigned int cipherlen = (direct == DRV_CRYPTO_DIRECTION_DECRYPT) ?
			(req->cryptlen - ctx->authsize) : req->cryptlen;

	if (unlikely((direct == DRV_CRYPTO_DIRECTION_DECRYPT) &&
		     (req->cryptlen < ctx->authsize)))
		goto data_size_err;

	areq_ctx->is_single_pass = true; /*defaulted to fast flow*/

	switch (ctx->flow_mode) {
	case S_DIN_to_AES:
		if (unlikely((ctx->cipher_mode == DRV_CIPHER_CBC) &&
			     !IS_ALIGNED(cipherlen, AES_BLOCK_SIZE)))
			goto data_size_err;
		if (ctx->cipher_mode == DRV_CIPHER_CCM)
			break;
		if (ctx->cipher_mode == DRV_CIPHER_GCTR) {
			if (areq_ctx->plaintext_authenticate_only)
				areq_ctx->is_single_pass = false;
			break;
		}

		if (!IS_ALIGNED(assoclen, sizeof(u32)))
			areq_ctx->is_single_pass = false;

		if ((ctx->cipher_mode == DRV_CIPHER_CTR) &&
		    !IS_ALIGNED(cipherlen, sizeof(u32)))
			areq_ctx->is_single_pass = false;

		break;
	case S_DIN_to_DES:
		if (unlikely(!IS_ALIGNED(cipherlen, DES_BLOCK_SIZE)))
			goto data_size_err;
		if (unlikely(!IS_ALIGNED(assoclen, DES_BLOCK_SIZE)))
			areq_ctx->is_single_pass = false;
		break;
	default:
		dev_err(dev, "Unexpected flow mode (%d)\n", ctx->flow_mode);
		goto data_size_err;
	}

	return 0;

data_size_err:
	return -EINVAL;
}

#if SSI_CC_HAS_AES_CCM
static unsigned int format_ccm_a0(u8 *pa0_buff, u32 header_size)
{
	unsigned int len = 0;

	if (header_size == 0)
		return 0;

	if (header_size < ((1UL << 16) - (1UL << 8))) {
		len = 2;

		pa0_buff[0] = (header_size >> 8) & 0xFF;
		pa0_buff[1] = header_size & 0xFF;
	} else {
		len = 6;

		pa0_buff[0] = 0xFF;
		pa0_buff[1] = 0xFE;
		pa0_buff[2] = (header_size >> 24) & 0xFF;
		pa0_buff[3] = (header_size >> 16) & 0xFF;
		pa0_buff[4] = (header_size >> 8) & 0xFF;
		pa0_buff[5] = header_size & 0xFF;
	}

	return len;
}

static int set_msg_len(u8 *block, unsigned int msglen, unsigned int csize)
{
	__be32 data;

	memset(block, 0, csize);
	block += csize;

	if (csize >= 4)
		csize = 4;
	else if (msglen > (1 << (8 * csize)))
		return -EOVERFLOW;

	data = cpu_to_be32(msglen);
	memcpy(block - csize, (u8 *)&data + 4 - csize, csize);

	return 0;
}

static inline int ssi_aead_ccm(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int idx = *seq_size;
	unsigned int cipher_flow_mode;
	dma_addr_t mac_result;

	if (req_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) {
		cipher_flow_mode = AES_to_HASH_and_DOUT;
		mac_result = req_ctx->mac_buf_dma_addr;
	} else { /* Encrypt */
		cipher_flow_mode = AES_and_HASH;
		mac_result = req_ctx->icv_dma_addr;
	}

	/* load key */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CTR);
	set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
		     ((ctx->enc_keylen == 24) ?  CC_AES_KEY_SIZE_MAX :
		      ctx->enc_keylen), NS_BIT);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* load ctr state */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CTR);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_din_type(&desc[idx], DMA_DLLI,
		     req_ctx->gen_ctx.iv_dma_addr, AES_BLOCK_SIZE, NS_BIT);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* load MAC key */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CBC_MAC);
	set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
		     ((ctx->enc_keylen == 24) ?  CC_AES_KEY_SIZE_MAX :
		      ctx->enc_keylen), NS_BIT);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* load MAC state */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CBC_MAC);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->mac_buf_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_cipher_config0(&desc[idx], DESC_DIRECTION_ENCRYPT_ENCRYPT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* process assoc data */
	if (req->assoclen > 0) {
		ssi_aead_create_assoc_desc(req, DIN_HASH, desc, &idx);
	} else {
		hw_desc_init(&desc[idx]);
		set_din_type(&desc[idx], DMA_DLLI,
			     sg_dma_address(&req_ctx->ccm_adata_sg),
			     AES_BLOCK_SIZE + req_ctx->ccm_hdr_size, NS_BIT);
		set_flow_mode(&desc[idx], DIN_HASH);
		idx++;
	}

	/* process the cipher */
	if (req_ctx->cryptlen != 0)
		ssi_aead_process_cipher_data_desc(req, cipher_flow_mode, desc, &idx);

	/* Read temporal MAC */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CBC_MAC);
	set_dout_dlli(&desc[idx], req_ctx->mac_buf_dma_addr, ctx->authsize,
		      NS_BIT, 0);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_cipher_config0(&desc[idx], HASH_DIGEST_RESULT_LITTLE_ENDIAN);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_aes_not_hash_mode(&desc[idx]);
	idx++;

	/* load AES-CTR state (for last MAC calculation)*/
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_CTR);
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->ccm_iv0_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	hw_desc_init(&desc[idx]);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	idx++;

	/* encrypt the "T" value and store MAC in mac_state */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->mac_buf_dma_addr,
		     ctx->authsize, NS_BIT);
	set_dout_dlli(&desc[idx], mac_result, ctx->authsize, NS_BIT, 1);
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	idx++;

	*seq_size = idx;
	return 0;
}

static int config_ccm_adata(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	//unsigned int size_of_a = 0, rem_a_size = 0;
	unsigned int lp = req->iv[0];
	/* Note: The code assume that req->iv[0] already contains the value of L' of RFC3610 */
	unsigned int l = lp + 1;  /* This is L' of RFC 3610. */
	unsigned int m = ctx->authsize;  /* This is M' of RFC 3610. */
	u8 *b0 = req_ctx->ccm_config + CCM_B0_OFFSET;
	u8 *a0 = req_ctx->ccm_config + CCM_A0_OFFSET;
	u8 *ctr_count_0 = req_ctx->ccm_config + CCM_CTR_COUNT_0_OFFSET;
	unsigned int cryptlen = (req_ctx->gen_ctx.op_type ==
				 DRV_CRYPTO_DIRECTION_ENCRYPT) ?
				req->cryptlen :
				(req->cryptlen - ctx->authsize);
	int rc;

	memset(req_ctx->mac_buf, 0, AES_BLOCK_SIZE);
	memset(req_ctx->ccm_config, 0, AES_BLOCK_SIZE * 3);

	/* taken from crypto/ccm.c */
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (l < 2 || l > 8) {
		dev_err(dev, "illegal iv value %X\n", req->iv[0]);
		return -EINVAL;
	}
	memcpy(b0, req->iv, AES_BLOCK_SIZE);

	/* format control info per RFC 3610 and
	 * NIST Special Publication 800-38C
	 */
	*b0 |= (8 * ((m - 2) / 2));
	if (req->assoclen > 0)
		*b0 |= 64;  /* Enable bit 6 if Adata exists. */

	rc = set_msg_len(b0 + 16 - l, cryptlen, l);  /* Write L'. */
	if (rc != 0) {
		dev_err(dev, "message len overflow detected");
		return rc;
	}
	 /* END of "taken from crypto/ccm.c" */

	/* l(a) - size of associated data. */
	req_ctx->ccm_hdr_size = format_ccm_a0(a0, req->assoclen);

	memset(req->iv + 15 - req->iv[0], 0, req->iv[0] + 1);
	req->iv[15] = 1;

	memcpy(ctr_count_0, req->iv, AES_BLOCK_SIZE);
	ctr_count_0[15] = 0;

	return 0;
}

static void ssi_rfc4309_ccm_process(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);

	/* L' */
	memset(areq_ctx->ctr_iv, 0, AES_BLOCK_SIZE);
	areq_ctx->ctr_iv[0] = 3;  /* For RFC 4309, always use 4 bytes for message length (at most 2^32-1 bytes). */

	/* In RFC 4309 there is an 11-bytes nonce+IV part, that we build here. */
	memcpy(areq_ctx->ctr_iv + CCM_BLOCK_NONCE_OFFSET, ctx->ctr_nonce, CCM_BLOCK_NONCE_SIZE);
	memcpy(areq_ctx->ctr_iv + CCM_BLOCK_IV_OFFSET,    req->iv,        CCM_BLOCK_IV_SIZE);
	req->iv = areq_ctx->ctr_iv;
	req->assoclen -= CCM_BLOCK_IV_SIZE;
}
#endif /*SSI_CC_HAS_AES_CCM*/

#if SSI_CC_HAS_AES_GCM

static inline void ssi_aead_gcm_setup_ghash_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int idx = *seq_size;

	/* load key to AES*/
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_ECB);
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
		     ctx->enc_keylen, NS_BIT);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* process one zero block to generate hkey */
	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x0, AES_BLOCK_SIZE);
	set_dout_dlli(&desc[idx], req_ctx->hkey_dma_addr, AES_BLOCK_SIZE,
		      NS_BIT, 0);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	idx++;

	/* Memory Barrier */
	hw_desc_init(&desc[idx]);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	idx++;

	/* Load GHASH subkey */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->hkey_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_HASH_HW_GHASH);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Configure Hash Engine to work with GHASH.
	 * Since it was not possible to extend HASH submodes to add GHASH,
	 * The following command is necessary in order to
	 * select GHASH (according to HW designers)
	 */
	hw_desc_init(&desc[idx]);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_HASH_HW_GHASH);
	set_cipher_do(&desc[idx], 1); //1=AES_SK RKEK
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	idx++;

	/* Load GHASH initial STATE (which is 0). (for any hash there is an initial state) */
	hw_desc_init(&desc[idx]);
	set_din_const(&desc[idx], 0x0, AES_BLOCK_SIZE);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	set_flow_mode(&desc[idx], S_DIN_to_HASH);
	set_aes_not_hash_mode(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_HASH_HW_GHASH);
	set_cipher_config1(&desc[idx], HASH_PADDING_ENABLED);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE0);
	idx++;

	*seq_size = idx;
}

static inline void ssi_aead_gcm_setup_gctr_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int idx = *seq_size;

	/* load key to AES*/
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_GCTR);
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_din_type(&desc[idx], DMA_DLLI, ctx->enckey_dma_addr,
		     ctx->enc_keylen, NS_BIT);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_setup_mode(&desc[idx], SETUP_LOAD_KEY0);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	if ((req_ctx->cryptlen != 0) && (!req_ctx->plaintext_authenticate_only)) {
		/* load AES/CTR initial CTR value inc by 2*/
		hw_desc_init(&desc[idx]);
		set_cipher_mode(&desc[idx], DRV_CIPHER_GCTR);
		set_key_size_aes(&desc[idx], ctx->enc_keylen);
		set_din_type(&desc[idx], DMA_DLLI,
			     req_ctx->gcm_iv_inc2_dma_addr, AES_BLOCK_SIZE,
			     NS_BIT);
		set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
		set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
		set_flow_mode(&desc[idx], S_DIN_to_AES);
		idx++;
	}

	*seq_size = idx;
}

static inline void ssi_aead_process_gcm_result_desc(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	dma_addr_t mac_result;
	unsigned int idx = *seq_size;

	if (req_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) {
		mac_result = req_ctx->mac_buf_dma_addr;
	} else { /* Encrypt */
		mac_result = req_ctx->icv_dma_addr;
	}

	/* process(ghash) gcm_block_len */
	hw_desc_init(&desc[idx]);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->gcm_block_len_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_flow_mode(&desc[idx], DIN_HASH);
	idx++;

	/* Store GHASH state after GHASH(Associated Data + Cipher +LenBlock) */
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_HASH_HW_GHASH);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_dlli(&desc[idx], req_ctx->mac_buf_dma_addr, AES_BLOCK_SIZE,
		      NS_BIT, 0);
	set_setup_mode(&desc[idx], SETUP_WRITE_STATE0);
	set_flow_mode(&desc[idx], S_HASH_to_DOUT);
	set_aes_not_hash_mode(&desc[idx]);

	idx++;

	/* load AES/CTR initial CTR value inc by 1*/
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_GCTR);
	set_key_size_aes(&desc[idx], ctx->enc_keylen);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->gcm_iv_inc1_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_cipher_config0(&desc[idx], DRV_CRYPTO_DIRECTION_ENCRYPT);
	set_setup_mode(&desc[idx], SETUP_LOAD_STATE1);
	set_flow_mode(&desc[idx], S_DIN_to_AES);
	idx++;

	/* Memory Barrier */
	hw_desc_init(&desc[idx]);
	set_din_no_dma(&desc[idx], 0, 0xfffff0);
	set_dout_no_dma(&desc[idx], 0, 0, 1);
	idx++;

	/* process GCTR on stored GHASH and store MAC in mac_state*/
	hw_desc_init(&desc[idx]);
	set_cipher_mode(&desc[idx], DRV_CIPHER_GCTR);
	set_din_type(&desc[idx], DMA_DLLI, req_ctx->mac_buf_dma_addr,
		     AES_BLOCK_SIZE, NS_BIT);
	set_dout_dlli(&desc[idx], mac_result, ctx->authsize, NS_BIT, 1);
	set_queue_last_ind(&desc[idx]);
	set_flow_mode(&desc[idx], DIN_AES_DOUT);
	idx++;

	*seq_size = idx;
}

static inline int ssi_aead_gcm(
	struct aead_request *req,
	struct cc_hw_desc desc[],
	unsigned int *seq_size)
{
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	unsigned int cipher_flow_mode;

	if (req_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) {
		cipher_flow_mode = AES_and_HASH;
	} else { /* Encrypt */
		cipher_flow_mode = AES_to_HASH_and_DOUT;
	}

	//in RFC4543 no data to encrypt. just copy data from src to dest.
	if (req_ctx->plaintext_authenticate_only) {
		ssi_aead_process_cipher_data_desc(req, BYPASS, desc, seq_size);
		ssi_aead_gcm_setup_ghash_desc(req, desc, seq_size);
		/* process(ghash) assoc data */
		ssi_aead_create_assoc_desc(req, DIN_HASH, desc, seq_size);
		ssi_aead_gcm_setup_gctr_desc(req, desc, seq_size);
		ssi_aead_process_gcm_result_desc(req, desc, seq_size);
		return 0;
	}

	// for gcm and rfc4106.
	ssi_aead_gcm_setup_ghash_desc(req, desc, seq_size);
	/* process(ghash) assoc data */
	if (req->assoclen > 0)
		ssi_aead_create_assoc_desc(req, DIN_HASH, desc, seq_size);
	ssi_aead_gcm_setup_gctr_desc(req, desc, seq_size);
	/* process(gctr+ghash) */
	if (req_ctx->cryptlen != 0)
		ssi_aead_process_cipher_data_desc(req, cipher_flow_mode, desc, seq_size);
	ssi_aead_process_gcm_result_desc(req, desc, seq_size);

	return 0;
}

#ifdef CC_DEBUG
static inline void ssi_aead_dump_gcm(
	const char *title,
	struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);

	if (ctx->cipher_mode != DRV_CIPHER_GCTR)
		return;

	if (title) {
		dev_dbg(dev, "----------------------------------------------------------------------------------");
		dev_dbg(dev, "%s\n", title);
	}

	dev_dbg(dev, "cipher_mode %d, authsize %d, enc_keylen %d, assoclen %d, cryptlen %d\n",
		ctx->cipher_mode, ctx->authsize, ctx->enc_keylen,
		req->assoclen, req_ctx->cryptlen);

	if (ctx->enckey)
		dump_byte_array("mac key", ctx->enckey, 16);

	dump_byte_array("req->iv", req->iv, AES_BLOCK_SIZE);

	dump_byte_array("gcm_iv_inc1", req_ctx->gcm_iv_inc1, AES_BLOCK_SIZE);

	dump_byte_array("gcm_iv_inc2", req_ctx->gcm_iv_inc2, AES_BLOCK_SIZE);

	dump_byte_array("hkey", req_ctx->hkey, AES_BLOCK_SIZE);

	dump_byte_array("mac_buf", req_ctx->mac_buf, AES_BLOCK_SIZE);

	dump_byte_array("gcm_len_block", req_ctx->gcm_len_block.len_a, AES_BLOCK_SIZE);

	if (req->src && req->cryptlen)
		dump_byte_array("req->src", sg_virt(req->src), req->cryptlen + req->assoclen);

	if (req->dst)
		dump_byte_array("req->dst", sg_virt(req->dst), req->cryptlen + ctx->authsize + req->assoclen);
}
#endif

static int config_gcm_context(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *req_ctx = aead_request_ctx(req);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	unsigned int cryptlen = (req_ctx->gen_ctx.op_type ==
				 DRV_CRYPTO_DIRECTION_ENCRYPT) ?
				req->cryptlen :
				(req->cryptlen - ctx->authsize);
	__be32 counter = cpu_to_be32(2);

	dev_dbg(dev, "%s() cryptlen = %d, req->assoclen = %d ctx->authsize = %d\n",
		__func__, cryptlen, req->assoclen, ctx->authsize);

	memset(req_ctx->hkey, 0, AES_BLOCK_SIZE);

	memset(req_ctx->mac_buf, 0, AES_BLOCK_SIZE);

	memcpy(req->iv + 12, &counter, 4);
	memcpy(req_ctx->gcm_iv_inc2, req->iv, 16);

	counter = cpu_to_be32(1);
	memcpy(req->iv + 12, &counter, 4);
	memcpy(req_ctx->gcm_iv_inc1, req->iv, 16);

	if (!req_ctx->plaintext_authenticate_only) {
		__be64 temp64;

		temp64 = cpu_to_be64(req->assoclen * 8);
		memcpy(&req_ctx->gcm_len_block.len_a, &temp64, sizeof(temp64));
		temp64 = cpu_to_be64(cryptlen * 8);
		memcpy(&req_ctx->gcm_len_block.len_c, &temp64, 8);
	} else { //rfc4543=>  all data(AAD,IV,Plain) are considered additional data that is nothing is encrypted.
		__be64 temp64;

		temp64 = cpu_to_be64((req->assoclen + GCM_BLOCK_RFC4_IV_SIZE + cryptlen) * 8);
		memcpy(&req_ctx->gcm_len_block.len_a, &temp64, sizeof(temp64));
		temp64 = 0;
		memcpy(&req_ctx->gcm_len_block.len_c, &temp64, 8);
	}

	return 0;
}

static void ssi_rfc4_gcm_process(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);

	memcpy(areq_ctx->ctr_iv + GCM_BLOCK_RFC4_NONCE_OFFSET, ctx->ctr_nonce, GCM_BLOCK_RFC4_NONCE_SIZE);
	memcpy(areq_ctx->ctr_iv + GCM_BLOCK_RFC4_IV_OFFSET,    req->iv, GCM_BLOCK_RFC4_IV_SIZE);
	req->iv = areq_ctx->ctr_iv;
	req->assoclen -= GCM_BLOCK_RFC4_IV_SIZE;
}

#endif /*SSI_CC_HAS_AES_GCM*/

static int ssi_aead_process(struct aead_request *req, enum drv_crypto_direction direct)
{
	int rc = 0;
	int seq_len = 0;
	struct cc_hw_desc desc[MAX_AEAD_PROCESS_SEQ];
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct ssi_crypto_req ssi_req = {};

	dev_dbg(dev, "%s context=%p req=%p iv=%p src=%p src_ofs=%d dst=%p dst_ofs=%d cryptolen=%d\n",
		((direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ? "Enc" : "Dec"),
		ctx, req, req->iv, sg_virt(req->src), req->src->offset,
		sg_virt(req->dst), req->dst->offset, req->cryptlen);

	/* STAT_PHASE_0: Init and sanity checks */

	/* Check data length according to mode */
	if (unlikely(validate_data_size(ctx, direct, req) != 0)) {
		dev_err(dev, "Unsupported crypt/assoc len %d/%d.\n",
			req->cryptlen, req->assoclen);
		crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_BLOCK_LEN);
		return -EINVAL;
	}

	/* Setup DX request structure */
	ssi_req.user_cb = (void *)ssi_aead_complete;
	ssi_req.user_arg = (void *)req;

	/* Setup request context */
	areq_ctx->gen_ctx.op_type = direct;
	areq_ctx->req_authsize = ctx->authsize;
	areq_ctx->cipher_mode = ctx->cipher_mode;

	/* STAT_PHASE_1: Map buffers */

	if (ctx->cipher_mode == DRV_CIPHER_CTR) {
		/* Build CTR IV - Copy nonce from last 4 bytes in
		 * CTR key to first 4 bytes in CTR IV
		 */
		memcpy(areq_ctx->ctr_iv, ctx->ctr_nonce, CTR_RFC3686_NONCE_SIZE);
		if (!areq_ctx->backup_giv) /*User none-generated IV*/
			memcpy(areq_ctx->ctr_iv + CTR_RFC3686_NONCE_SIZE,
			       req->iv, CTR_RFC3686_IV_SIZE);
		/* Initialize counter portion of counter block */
		*(__be32 *)(areq_ctx->ctr_iv + CTR_RFC3686_NONCE_SIZE +
			    CTR_RFC3686_IV_SIZE) = cpu_to_be32(1);

		/* Replace with counter iv */
		req->iv = areq_ctx->ctr_iv;
		areq_ctx->hw_iv_size = CTR_RFC3686_BLOCK_SIZE;
	} else if ((ctx->cipher_mode == DRV_CIPHER_CCM) ||
		   (ctx->cipher_mode == DRV_CIPHER_GCTR)) {
		areq_ctx->hw_iv_size = AES_BLOCK_SIZE;
		if (areq_ctx->ctr_iv != req->iv) {
			memcpy(areq_ctx->ctr_iv, req->iv, crypto_aead_ivsize(tfm));
			req->iv = areq_ctx->ctr_iv;
		}
	}  else {
		areq_ctx->hw_iv_size = crypto_aead_ivsize(tfm);
	}

#if SSI_CC_HAS_AES_CCM
	if (ctx->cipher_mode == DRV_CIPHER_CCM) {
		rc = config_ccm_adata(req);
		if (unlikely(rc != 0)) {
			dev_dbg(dev, "config_ccm_adata() returned with a failure %d!",
				rc);
			goto exit;
		}
	} else {
		areq_ctx->ccm_hdr_size = ccm_header_size_null;
	}
#else
	areq_ctx->ccm_hdr_size = ccm_header_size_null;
#endif /*SSI_CC_HAS_AES_CCM*/

#if SSI_CC_HAS_AES_GCM
	if (ctx->cipher_mode == DRV_CIPHER_GCTR) {
		rc = config_gcm_context(req);
		if (unlikely(rc != 0)) {
			dev_dbg(dev, "config_gcm_context() returned with a failure %d!",
				rc);
			goto exit;
		}
	}
#endif /*SSI_CC_HAS_AES_GCM*/

	rc = ssi_buffer_mgr_map_aead_request(ctx->drvdata, req);
	if (unlikely(rc != 0)) {
		dev_err(dev, "map_request() failed\n");
		goto exit;
	}

	/* do we need to generate IV? */
	if (areq_ctx->backup_giv) {
		/* set the DMA mapped IV address*/
		if (ctx->cipher_mode == DRV_CIPHER_CTR) {
			ssi_req.ivgen_dma_addr[0] = areq_ctx->gen_ctx.iv_dma_addr + CTR_RFC3686_NONCE_SIZE;
			ssi_req.ivgen_dma_addr_len = 1;
		} else if (ctx->cipher_mode == DRV_CIPHER_CCM) {
			/* In ccm, the IV needs to exist both inside B0 and inside the counter.
			 * It is also copied to iv_dma_addr for other reasons (like returning
			 * it to the user).
			 * So, using 3 (identical) IV outputs.
			 */
			ssi_req.ivgen_dma_addr[0] = areq_ctx->gen_ctx.iv_dma_addr + CCM_BLOCK_IV_OFFSET;
			ssi_req.ivgen_dma_addr[1] = sg_dma_address(&areq_ctx->ccm_adata_sg) + CCM_B0_OFFSET          + CCM_BLOCK_IV_OFFSET;
			ssi_req.ivgen_dma_addr[2] = sg_dma_address(&areq_ctx->ccm_adata_sg) + CCM_CTR_COUNT_0_OFFSET + CCM_BLOCK_IV_OFFSET;
			ssi_req.ivgen_dma_addr_len = 3;
		} else {
			ssi_req.ivgen_dma_addr[0] = areq_ctx->gen_ctx.iv_dma_addr;
			ssi_req.ivgen_dma_addr_len = 1;
		}

		/* set the IV size (8/16 B long)*/
		ssi_req.ivgen_size = crypto_aead_ivsize(tfm);
	}

	/* STAT_PHASE_2: Create sequence */

	/* Load MLLI tables to SRAM if necessary */
	ssi_aead_load_mlli_to_sram(req, desc, &seq_len);

	/*TODO: move seq len by reference */
	switch (ctx->auth_mode) {
	case DRV_HASH_SHA1:
	case DRV_HASH_SHA256:
		ssi_aead_hmac_authenc(req, desc, &seq_len);
		break;
	case DRV_HASH_XCBC_MAC:
		ssi_aead_xcbc_authenc(req, desc, &seq_len);
		break;
#if (SSI_CC_HAS_AES_CCM || SSI_CC_HAS_AES_GCM)
	case DRV_HASH_NULL:
#if SSI_CC_HAS_AES_CCM
		if (ctx->cipher_mode == DRV_CIPHER_CCM)
			ssi_aead_ccm(req, desc, &seq_len);
#endif /*SSI_CC_HAS_AES_CCM*/
#if SSI_CC_HAS_AES_GCM
		if (ctx->cipher_mode == DRV_CIPHER_GCTR)
			ssi_aead_gcm(req, desc, &seq_len);
#endif /*SSI_CC_HAS_AES_GCM*/
			break;
#endif
	default:
		dev_err(dev, "Unsupported authenc (%d)\n", ctx->auth_mode);
		ssi_buffer_mgr_unmap_aead_request(dev, req);
		rc = -ENOTSUPP;
		goto exit;
	}

	/* STAT_PHASE_3: Lock HW and push sequence */

	rc = send_request(ctx->drvdata, &ssi_req, desc, seq_len, 1);

	if (unlikely(rc != -EINPROGRESS)) {
		dev_err(dev, "send_request() failed (rc=%d)\n", rc);
		ssi_buffer_mgr_unmap_aead_request(dev, req);
	}

exit:
	return rc;
}

static int ssi_aead_encrypt(struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc;

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;
	areq_ctx->is_gcm4543 = false;

	areq_ctx->plaintext_authenticate_only = false;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_ENCRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;

	return rc;
}

#if SSI_CC_HAS_AES_CCM
static int ssi_rfc4309_ccm_encrypt(struct aead_request *req)
{
	/* Very similar to ssi_aead_encrypt() above. */

	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	int rc = -EINVAL;

	if (!valid_assoclen(req)) {
		dev_err(dev, "invalid Assoclen:%u\n", req->assoclen);
		goto out;
	}

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;
	areq_ctx->is_gcm4543 = true;

	ssi_rfc4309_ccm_process(req);

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_ENCRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;
out:
	return rc;
}
#endif /* SSI_CC_HAS_AES_CCM */

static int ssi_aead_decrypt(struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc;

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;
	areq_ctx->is_gcm4543 = false;

	areq_ctx->plaintext_authenticate_only = false;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_DECRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;

	return rc;
}

#if SSI_CC_HAS_AES_CCM
static int ssi_rfc4309_ccm_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc = -EINVAL;

	if (!valid_assoclen(req)) {
		dev_err(dev, "invalid Assoclen:%u\n", req->assoclen);
		goto out;
	}

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;

	areq_ctx->is_gcm4543 = true;
	ssi_rfc4309_ccm_process(req);

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_DECRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;

out:
	return rc;
}
#endif /* SSI_CC_HAS_AES_CCM */

#if SSI_CC_HAS_AES_GCM

static int ssi_rfc4106_gcm_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "%s()  keylen %d, key %p\n", __func__, keylen, key);

	if (keylen < 4)
		return -EINVAL;

	keylen -= 4;
	memcpy(ctx->ctr_nonce, key + keylen, 4);

	return ssi_aead_setkey(tfm, key, keylen);
}

static int ssi_rfc4543_gcm_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "%s()  keylen %d, key %p\n", __func__, keylen, key);

	if (keylen < 4)
		return -EINVAL;

	keylen -= 4;
	memcpy(ctx->ctr_nonce, key + keylen, 4);

	return ssi_aead_setkey(tfm, key, keylen);
}

static int ssi_gcm_setauthsize(struct crypto_aead *authenc,
			       unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return ssi_aead_setauthsize(authenc, authsize);
}

static int ssi_rfc4106_gcm_setauthsize(struct crypto_aead *authenc,
				       unsigned int authsize)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(authenc);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "authsize %d\n", authsize);

	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return ssi_aead_setauthsize(authenc, authsize);
}

static int ssi_rfc4543_gcm_setauthsize(struct crypto_aead *authenc,
				       unsigned int authsize)
{
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(authenc);
	struct device *dev = drvdata_to_dev(ctx->drvdata);

	dev_dbg(dev, "authsize %d\n", authsize);

	if (authsize != 16)
		return -EINVAL;

	return ssi_aead_setauthsize(authenc, authsize);
}

static int ssi_rfc4106_gcm_encrypt(struct aead_request *req)
{
	/* Very similar to ssi_aead_encrypt() above. */

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc = -EINVAL;

	if (!valid_assoclen(req)) {
		dev_err(dev, "invalid Assoclen:%u\n", req->assoclen);
		goto out;
	}

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;

	areq_ctx->plaintext_authenticate_only = false;

	ssi_rfc4_gcm_process(req);
	areq_ctx->is_gcm4543 = true;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_ENCRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;
out:
	return rc;
}

static int ssi_rfc4543_gcm_encrypt(struct aead_request *req)
{
	/* Very similar to ssi_aead_encrypt() above. */

	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc;

	//plaintext is not encryped with rfc4543
	areq_ctx->plaintext_authenticate_only = true;

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;

	ssi_rfc4_gcm_process(req);
	areq_ctx->is_gcm4543 = true;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_ENCRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;

	return rc;
}

static int ssi_rfc4106_gcm_decrypt(struct aead_request *req)
{
	/* Very similar to ssi_aead_decrypt() above. */

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct device *dev = drvdata_to_dev(ctx->drvdata);
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc = -EINVAL;

	if (!valid_assoclen(req)) {
		dev_err(dev, "invalid Assoclen:%u\n", req->assoclen);
		goto out;
	}

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;

	areq_ctx->plaintext_authenticate_only = false;

	ssi_rfc4_gcm_process(req);
	areq_ctx->is_gcm4543 = true;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_DECRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;
out:
	return rc;
}

static int ssi_rfc4543_gcm_decrypt(struct aead_request *req)
{
	/* Very similar to ssi_aead_decrypt() above. */

	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc;

	//plaintext is not decryped with rfc4543
	areq_ctx->plaintext_authenticate_only = true;

	/* No generated IV required */
	areq_ctx->backup_iv = req->iv;
	areq_ctx->backup_giv = NULL;

	ssi_rfc4_gcm_process(req);
	areq_ctx->is_gcm4543 = true;

	rc = ssi_aead_process(req, DRV_CRYPTO_DIRECTION_DECRYPT);
	if (rc != -EINPROGRESS)
		req->iv = areq_ctx->backup_iv;

	return rc;
}
#endif /* SSI_CC_HAS_AES_GCM */

/* DX Block aead alg */
static struct ssi_alg_template aead_algs[] = {
	{
		.name = "authenc(hmac(sha1),cbc(aes))",
		.driver_name = "authenc-hmac-sha1-cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_SHA1,
	},
	{
		.name = "authenc(hmac(sha1),cbc(des3_ede))",
		.driver_name = "authenc-hmac-sha1-cbc-des3-dx",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_DES,
		.auth_mode = DRV_HASH_SHA1,
	},
	{
		.name = "authenc(hmac(sha256),cbc(aes))",
		.driver_name = "authenc-hmac-sha256-cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_SHA256,
	},
	{
		.name = "authenc(hmac(sha256),cbc(des3_ede))",
		.driver_name = "authenc-hmac-sha256-cbc-des3-dx",
		.blocksize = DES3_EDE_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_DES,
		.auth_mode = DRV_HASH_SHA256,
	},
	{
		.name = "authenc(xcbc(aes),cbc(aes))",
		.driver_name = "authenc-xcbc-aes-cbc-aes-dx",
		.blocksize = AES_BLOCK_SIZE,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CBC,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_XCBC_MAC,
	},
	{
		.name = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
		.driver_name = "authenc-hmac-sha1-rfc3686-ctr-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_SHA1,
	},
	{
		.name = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
		.driver_name = "authenc-hmac-sha256-rfc3686-ctr-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_SHA256,
	},
	{
		.name = "authenc(xcbc(aes),rfc3686(ctr(aes)))",
		.driver_name = "authenc-xcbc-aes-rfc3686-ctr-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_aead_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = CTR_RFC3686_IV_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_XCBC_MAC,
	},
#if SSI_CC_HAS_AES_CCM
	{
		.name = "ccm(aes)",
		.driver_name = "ccm-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_ccm_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CCM,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_NULL,
	},
	{
		.name = "rfc4309(ccm(aes))",
		.driver_name = "rfc4309-ccm-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_rfc4309_ccm_setkey,
			.setauthsize = ssi_rfc4309_ccm_setauthsize,
			.encrypt = ssi_rfc4309_ccm_encrypt,
			.decrypt = ssi_rfc4309_ccm_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = CCM_BLOCK_IV_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_CCM,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_NULL,
	},
#endif /*SSI_CC_HAS_AES_CCM*/
#if SSI_CC_HAS_AES_GCM
	{
		.name = "gcm(aes)",
		.driver_name = "gcm-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_aead_setkey,
			.setauthsize = ssi_gcm_setauthsize,
			.encrypt = ssi_aead_encrypt,
			.decrypt = ssi_aead_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = 12,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_GCTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_NULL,
	},
	{
		.name = "rfc4106(gcm(aes))",
		.driver_name = "rfc4106-gcm-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_rfc4106_gcm_setkey,
			.setauthsize = ssi_rfc4106_gcm_setauthsize,
			.encrypt = ssi_rfc4106_gcm_encrypt,
			.decrypt = ssi_rfc4106_gcm_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = GCM_BLOCK_RFC4_IV_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_GCTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_NULL,
	},
	{
		.name = "rfc4543(gcm(aes))",
		.driver_name = "rfc4543-gcm-aes-dx",
		.blocksize = 1,
		.type = CRYPTO_ALG_TYPE_AEAD,
		.template_aead = {
			.setkey = ssi_rfc4543_gcm_setkey,
			.setauthsize = ssi_rfc4543_gcm_setauthsize,
			.encrypt = ssi_rfc4543_gcm_encrypt,
			.decrypt = ssi_rfc4543_gcm_decrypt,
			.init = ssi_aead_init,
			.exit = ssi_aead_exit,
			.ivsize = GCM_BLOCK_RFC4_IV_SIZE,
			.maxauthsize = AES_BLOCK_SIZE,
		},
		.cipher_mode = DRV_CIPHER_GCTR,
		.flow_mode = S_DIN_to_AES,
		.auth_mode = DRV_HASH_NULL,
	},
#endif /*SSI_CC_HAS_AES_GCM*/
};

static struct ssi_crypto_alg *ssi_aead_create_alg(
			struct ssi_alg_template *template,
			struct device *dev)
{
	struct ssi_crypto_alg *t_alg;
	struct aead_alg *alg;

	t_alg = kzalloc(sizeof(*t_alg), GFP_KERNEL);
	if (!t_alg)
		return ERR_PTR(-ENOMEM);

	alg = &template->template_aead;

	snprintf(alg->base.cra_name, CRYPTO_MAX_ALG_NAME, "%s", template->name);
	snprintf(alg->base.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s",
		 template->driver_name);
	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = SSI_CRA_PRIO;

	alg->base.cra_ctxsize = sizeof(struct ssi_aead_ctx);
	alg->base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_KERN_DRIVER_ONLY |
			 template->type;
	alg->init = ssi_aead_init;
	alg->exit = ssi_aead_exit;

	t_alg->aead_alg = *alg;

	t_alg->cipher_mode = template->cipher_mode;
	t_alg->flow_mode = template->flow_mode;
	t_alg->auth_mode = template->auth_mode;

	return t_alg;
}

int ssi_aead_free(struct ssi_drvdata *drvdata)
{
	struct ssi_crypto_alg *t_alg, *n;
	struct ssi_aead_handle *aead_handle =
		(struct ssi_aead_handle *)drvdata->aead_handle;

	if (aead_handle) {
		/* Remove registered algs */
		list_for_each_entry_safe(t_alg, n, &aead_handle->aead_list, entry) {
			crypto_unregister_aead(&t_alg->aead_alg);
			list_del(&t_alg->entry);
			kfree(t_alg);
		}
		kfree(aead_handle);
		drvdata->aead_handle = NULL;
	}

	return 0;
}

int ssi_aead_alloc(struct ssi_drvdata *drvdata)
{
	struct ssi_aead_handle *aead_handle;
	struct ssi_crypto_alg *t_alg;
	int rc = -ENOMEM;
	int alg;
	struct device *dev = drvdata_to_dev(drvdata);

	aead_handle = kmalloc(sizeof(*aead_handle), GFP_KERNEL);
	if (!aead_handle) {
		rc = -ENOMEM;
		goto fail0;
	}

	INIT_LIST_HEAD(&aead_handle->aead_list);
	drvdata->aead_handle = aead_handle;

	aead_handle->sram_workspace_addr = ssi_sram_mgr_alloc(
		drvdata, MAX_HMAC_DIGEST_SIZE);
	if (aead_handle->sram_workspace_addr == NULL_SRAM_ADDR) {
		dev_err(dev, "SRAM pool exhausted\n");
		rc = -ENOMEM;
		goto fail1;
	}

	/* Linux crypto */
	for (alg = 0; alg < ARRAY_SIZE(aead_algs); alg++) {
		t_alg = ssi_aead_create_alg(&aead_algs[alg], dev);
		if (IS_ERR(t_alg)) {
			rc = PTR_ERR(t_alg);
			dev_err(dev, "%s alg allocation failed\n",
				aead_algs[alg].driver_name);
			goto fail1;
		}
		t_alg->drvdata = drvdata;
		rc = crypto_register_aead(&t_alg->aead_alg);
		if (unlikely(rc != 0)) {
			dev_err(dev, "%s alg registration failed\n",
				t_alg->aead_alg.base.cra_driver_name);
			goto fail2;
		} else {
			list_add_tail(&t_alg->entry, &aead_handle->aead_list);
			dev_dbg(dev, "Registered %s\n",
				t_alg->aead_alg.base.cra_driver_name);
		}
	}

	return 0;

fail2:
	kfree(t_alg);
fail1:
	ssi_aead_free(drvdata);
fail0:
	return rc;
}
