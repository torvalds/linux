// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 - 2021
 *
 * Richard van Schagen <vschagen@icloud.com>
 * Christian Marangi <ansuelsmth@gmail.com
 */

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/authenc.h>
#include <crypto/ctr.h>
#include <crypto/hmac.h>
#include <crypto/internal/aead.h>
#include <crypto/md5.h>
#include <crypto/null.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>

#include <crypto/internal/des.h>

#include <linux/crypto.h>
#include <linux/dma-mapping.h>

#include "eip93-aead.h"
#include "eip93-cipher.h"
#include "eip93-common.h"
#include "eip93-regs.h"

void eip93_aead_handle_result(struct crypto_async_request *async, int err)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(async->tfm);
	struct eip93_device *eip93 = ctx->eip93;
	struct aead_request *req = aead_request_cast(async);
	struct eip93_cipher_reqctx *rctx = aead_request_ctx(req);

	eip93_unmap_dma(eip93, rctx, req->src, req->dst);
	eip93_handle_result(eip93, rctx, req->iv);

	aead_request_complete(req, err);
}

static int eip93_aead_send_req(struct crypto_async_request *async)
{
	struct aead_request *req = aead_request_cast(async);
	struct eip93_cipher_reqctx *rctx = aead_request_ctx(req);
	int err;

	err = check_valid_request(rctx);
	if (err) {
		aead_request_complete(req, err);
		return err;
	}

	return eip93_send_req(async, req->iv, rctx);
}

/* Crypto aead API functions */
static int eip93_aead_cra_init(struct crypto_tfm *tfm)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);
	struct eip93_alg_template *tmpl = container_of(tfm->__crt_alg,
				struct eip93_alg_template, alg.aead.base);

	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				sizeof(struct eip93_cipher_reqctx));

	ctx->eip93 = tmpl->eip93;
	ctx->flags = tmpl->flags;
	ctx->type = tmpl->type;
	ctx->set_assoc = true;

	ctx->sa_record = kzalloc(sizeof(*ctx->sa_record), GFP_KERNEL);
	if (!ctx->sa_record)
		return -ENOMEM;

	return 0;
}

static void eip93_aead_cra_exit(struct crypto_tfm *tfm)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);

	dma_unmap_single(ctx->eip93->dev, ctx->sa_record_base,
			 sizeof(*ctx->sa_record), DMA_TO_DEVICE);
	kfree(ctx->sa_record);
}

static int eip93_aead_setkey(struct crypto_aead *ctfm, const u8 *key,
			     unsigned int len)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_authenc_keys keys;
	struct crypto_aes_ctx aes;
	struct sa_record *sa_record = ctx->sa_record;
	u32 nonce = 0;
	int ret;

	if (crypto_authenc_extractkeys(&keys, key, len))
		return -EINVAL;

	if (IS_RFC3686(ctx->flags)) {
		if (keys.enckeylen < CTR_RFC3686_NONCE_SIZE)
			return -EINVAL;

		keys.enckeylen -= CTR_RFC3686_NONCE_SIZE;
		memcpy(&nonce, keys.enckey + keys.enckeylen,
		       CTR_RFC3686_NONCE_SIZE);
	}

	switch ((ctx->flags & EIP93_ALG_MASK)) {
	case EIP93_ALG_DES:
		ret = verify_aead_des_key(ctfm, keys.enckey, keys.enckeylen);
		if (ret)
			return ret;

		break;
	case EIP93_ALG_3DES:
		if (keys.enckeylen != DES3_EDE_KEY_SIZE)
			return -EINVAL;

		ret = verify_aead_des3_key(ctfm, keys.enckey, keys.enckeylen);
		if (ret)
			return ret;

		break;
	case EIP93_ALG_AES:
		ret = aes_expandkey(&aes, keys.enckey, keys.enckeylen);
		if (ret)
			return ret;

		break;
	}

	ctx->blksize = crypto_aead_blocksize(ctfm);
	/* Encryption key */
	eip93_set_sa_record(sa_record, keys.enckeylen, ctx->flags);
	sa_record->sa_cmd0_word &= ~EIP93_SA_CMD_OPCODE;
	sa_record->sa_cmd0_word |= FIELD_PREP(EIP93_SA_CMD_OPCODE,
					      EIP93_SA_CMD_OPCODE_BASIC_OUT_ENC_HASH);
	sa_record->sa_cmd0_word &= ~EIP93_SA_CMD_DIGEST_LENGTH;
	sa_record->sa_cmd0_word |= FIELD_PREP(EIP93_SA_CMD_DIGEST_LENGTH,
					      ctx->authsize / sizeof(u32));

	memcpy(sa_record->sa_key, keys.enckey, keys.enckeylen);
	ctx->sa_nonce = nonce;
	sa_record->sa_nonce = nonce;

	/* authentication key */
	ret = eip93_hmac_setkey(ctx->flags, keys.authkey, keys.authkeylen,
				ctx->authsize, sa_record->sa_i_digest,
				sa_record->sa_o_digest, false);

	ctx->set_assoc = true;

	return ret;
}

static int eip93_aead_setauthsize(struct crypto_aead *ctfm,
				  unsigned int authsize)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(ctfm);
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->authsize = authsize;
	ctx->sa_record->sa_cmd0_word &= ~EIP93_SA_CMD_DIGEST_LENGTH;
	ctx->sa_record->sa_cmd0_word |= FIELD_PREP(EIP93_SA_CMD_DIGEST_LENGTH,
						   ctx->authsize / sizeof(u32));

	return 0;
}

static void eip93_aead_setassoc(struct eip93_crypto_ctx *ctx,
				struct aead_request *req)
{
	struct sa_record *sa_record = ctx->sa_record;

	sa_record->sa_cmd1_word &= ~EIP93_SA_CMD_HASH_CRYPT_OFFSET;
	sa_record->sa_cmd1_word |= FIELD_PREP(EIP93_SA_CMD_HASH_CRYPT_OFFSET,
					      req->assoclen / sizeof(u32));

	ctx->assoclen = req->assoclen;
}

static int eip93_aead_crypt(struct aead_request *req)
{
	struct eip93_cipher_reqctx *rctx = aead_request_ctx(req);
	struct crypto_async_request *async = &req->base;
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	int ret;

	ctx->sa_record_base = dma_map_single(ctx->eip93->dev, ctx->sa_record,
					     sizeof(*ctx->sa_record), DMA_TO_DEVICE);
	ret = dma_mapping_error(ctx->eip93->dev, ctx->sa_record_base);
	if (ret)
		return ret;

	rctx->textsize = req->cryptlen;
	rctx->blksize = ctx->blksize;
	rctx->assoclen = req->assoclen;
	rctx->authsize = ctx->authsize;
	rctx->sg_src = req->src;
	rctx->sg_dst = req->dst;
	rctx->ivsize = crypto_aead_ivsize(aead);
	rctx->desc_flags = EIP93_DESC_AEAD;
	rctx->sa_record_base = ctx->sa_record_base;

	if (IS_DECRYPT(rctx->flags))
		rctx->textsize -= rctx->authsize;

	return eip93_aead_send_req(async);
}

static int eip93_aead_encrypt(struct aead_request *req)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct eip93_cipher_reqctx *rctx = aead_request_ctx(req);

	rctx->flags = ctx->flags;
	rctx->flags |= EIP93_ENCRYPT;
	if (ctx->set_assoc) {
		eip93_aead_setassoc(ctx, req);
		ctx->set_assoc = false;
	}

	if (req->assoclen != ctx->assoclen) {
		dev_err(ctx->eip93->dev, "Request AAD length error\n");
		return -EINVAL;
	}

	return eip93_aead_crypt(req);
}

static int eip93_aead_decrypt(struct aead_request *req)
{
	struct eip93_crypto_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct eip93_cipher_reqctx *rctx = aead_request_ctx(req);

	ctx->sa_record->sa_cmd0_word |= EIP93_SA_CMD_DIRECTION_IN;
	ctx->sa_record->sa_cmd1_word &= ~(EIP93_SA_CMD_COPY_PAD |
					  EIP93_SA_CMD_COPY_DIGEST);

	rctx->flags = ctx->flags;
	rctx->flags |= EIP93_DECRYPT;
	if (ctx->set_assoc) {
		eip93_aead_setassoc(ctx, req);
		ctx->set_assoc = false;
	}

	if (req->assoclen != ctx->assoclen) {
		dev_err(ctx->eip93->dev, "Request AAD length error\n");
		return -EINVAL;
	}

	return eip93_aead_crypt(req);
}

/* Available authenc algorithms in this module */
struct eip93_alg_template eip93_alg_authenc_hmac_md5_cbc_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_MD5 | EIP93_MODE_CBC | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= AES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = MD5_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(md5),cbc(aes))",
			.cra_driver_name =
				"authenc(hmac(md5-eip93), cbc(aes-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha1_cbc_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA1 | EIP93_MODE_CBC | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= AES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(aes))",
			.cra_driver_name =
				"authenc(hmac(sha1-eip93),cbc(aes-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha224_cbc_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA224 | EIP93_MODE_CBC | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= AES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),cbc(aes))",
			.cra_driver_name =
				"authenc(hmac(sha224-eip93),cbc(aes-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha256_cbc_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA256 | EIP93_MODE_CBC | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= AES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),cbc(aes))",
			.cra_driver_name =
				"authenc(hmac(sha256-eip93),cbc(aes-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_md5_rfc3686_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_MD5 |
			EIP93_MODE_CTR | EIP93_MODE_RFC3686 | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= CTR_RFC3686_IV_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = MD5_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(md5),rfc3686(ctr(aes)))",
			.cra_driver_name =
			"authenc(hmac(md5-eip93),rfc3686(ctr(aes-eip93)))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha1_rfc3686_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA1 |
			EIP93_MODE_CTR | EIP93_MODE_RFC3686 | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= CTR_RFC3686_IV_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),rfc3686(ctr(aes)))",
			.cra_driver_name =
			"authenc(hmac(sha1-eip93),rfc3686(ctr(aes-eip93)))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha224_rfc3686_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA224 |
			EIP93_MODE_CTR | EIP93_MODE_RFC3686 | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= CTR_RFC3686_IV_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),rfc3686(ctr(aes)))",
			.cra_driver_name =
			"authenc(hmac(sha224-eip93),rfc3686(ctr(aes-eip93)))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha256_rfc3686_aes = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA256 |
			EIP93_MODE_CTR | EIP93_MODE_RFC3686 | EIP93_ALG_AES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= CTR_RFC3686_IV_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),rfc3686(ctr(aes)))",
			.cra_driver_name =
			"authenc(hmac(sha256-eip93),rfc3686(ctr(aes-eip93)))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_md5_cbc_des = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_MD5 | EIP93_MODE_CBC | EIP93_ALG_DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = MD5_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(md5),cbc(des))",
			.cra_driver_name =
				"authenc(hmac(md5-eip93),cbc(des-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha1_cbc_des = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA1 | EIP93_MODE_CBC | EIP93_ALG_DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(des))",
			.cra_driver_name =
				"authenc(hmac(sha1-eip93),cbc(des-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha224_cbc_des = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA224 | EIP93_MODE_CBC | EIP93_ALG_DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),cbc(des))",
			.cra_driver_name =
				"authenc(hmac(sha224-eip93),cbc(des-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha256_cbc_des = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA256 | EIP93_MODE_CBC | EIP93_ALG_DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),cbc(des))",
			.cra_driver_name =
				"authenc(hmac(sha256-eip93),cbc(des-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_md5_cbc_des3_ede = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_MD5 | EIP93_MODE_CBC | EIP93_ALG_3DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES3_EDE_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = MD5_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
			.cra_driver_name =
				"authenc(hmac(md5-eip93),cbc(des3_ede-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0x0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha1_cbc_des3_ede = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA1 | EIP93_MODE_CBC | EIP93_ALG_3DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES3_EDE_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha1),cbc(des3_ede))",
			.cra_driver_name =
				"authenc(hmac(sha1-eip93),cbc(des3_ede-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0x0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha224_cbc_des3_ede = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA224 | EIP93_MODE_CBC | EIP93_ALG_3DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES3_EDE_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha224),cbc(des3_ede))",
			.cra_driver_name =
			"authenc(hmac(sha224-eip93),cbc(des3_ede-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0x0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};

struct eip93_alg_template eip93_alg_authenc_hmac_sha256_cbc_des3_ede = {
	.type = EIP93_ALG_TYPE_AEAD,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA256 | EIP93_MODE_CBC | EIP93_ALG_3DES,
	.alg.aead = {
		.setkey = eip93_aead_setkey,
		.encrypt = eip93_aead_encrypt,
		.decrypt = eip93_aead_decrypt,
		.ivsize	= DES3_EDE_BLOCK_SIZE,
		.setauthsize = eip93_aead_setauthsize,
		.maxauthsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_name = "authenc(hmac(sha256),cbc(des3_ede))",
			.cra_driver_name =
			"authenc(hmac(sha256-eip93),cbc(des3_ede-eip93))",
			.cra_priority = EIP93_CRA_PRIORITY,
			.cra_flags = CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_KERN_DRIVER_ONLY |
					CRYPTO_ALG_ALLOCATES_MEMORY,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct eip93_crypto_ctx),
			.cra_alignmask = 0x0,
			.cra_init = eip93_aead_cra_init,
			.cra_exit = eip93_aead_cra_exit,
			.cra_module = THIS_MODULE,
		},
	},
};
