// SPDX-License-Identifier: GPL-2.0
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>

#include <crypto/aes.h>
#include <crypto/skcipher.h>
#include <crypto/ctr.h>
#include <crypto/internal/des.h>
#include <crypto/xts.h>

#include "nitrox_dev.h"
#include "nitrox_common.h"
#include "nitrox_req.h"

struct nitrox_cipher {
	const char *name;
	enum flexi_cipher value;
};

/**
 * supported cipher list
 */
static const struct nitrox_cipher flexi_cipher_table[] = {
	{ "null",		CIPHER_NULL },
	{ "cbc(des3_ede)",	CIPHER_3DES_CBC },
	{ "ecb(des3_ede)",	CIPHER_3DES_ECB },
	{ "cbc(aes)",		CIPHER_AES_CBC },
	{ "ecb(aes)",		CIPHER_AES_ECB },
	{ "cfb(aes)",		CIPHER_AES_CFB },
	{ "rfc3686(ctr(aes))",	CIPHER_AES_CTR },
	{ "xts(aes)",		CIPHER_AES_XTS },
	{ "cts(cbc(aes))",	CIPHER_AES_CBC_CTS },
	{ NULL,			CIPHER_INVALID }
};

static enum flexi_cipher flexi_cipher_type(const char *name)
{
	const struct nitrox_cipher *cipher = flexi_cipher_table;

	while (cipher->name) {
		if (!strcmp(cipher->name, name))
			break;
		cipher++;
	}
	return cipher->value;
}

static int nitrox_skcipher_init(struct crypto_skcipher *tfm)
{
	struct nitrox_crypto_ctx *nctx = crypto_skcipher_ctx(tfm);
	struct crypto_ctx_hdr *chdr;

	/* get the first device */
	nctx->ndev = nitrox_get_first_device();
	if (!nctx->ndev)
		return -ENODEV;

	/* allocate nitrox crypto context */
	chdr = crypto_alloc_context(nctx->ndev);
	if (!chdr) {
		nitrox_put_device(nctx->ndev);
		return -ENOMEM;
	}
	nctx->chdr = chdr;
	nctx->u.ctx_handle = (uintptr_t)((u8 *)chdr->vaddr +
					 sizeof(struct ctx_hdr));
	crypto_skcipher_set_reqsize(tfm, crypto_skcipher_reqsize(tfm) +
				    sizeof(struct nitrox_kcrypt_request));
	return 0;
}

static void nitrox_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct nitrox_crypto_ctx *nctx = crypto_skcipher_ctx(tfm);

	/* free the nitrox crypto context */
	if (nctx->u.ctx_handle) {
		struct flexi_crypto_context *fctx = nctx->u.fctx;

		memzero_explicit(&fctx->crypto, sizeof(struct crypto_keys));
		memzero_explicit(&fctx->auth, sizeof(struct auth_keys));
		crypto_free_context((void *)nctx->chdr);
	}
	nitrox_put_device(nctx->ndev);

	nctx->u.ctx_handle = 0;
	nctx->ndev = NULL;
}

static inline int nitrox_skcipher_setkey(struct crypto_skcipher *cipher,
					 int aes_keylen, const u8 *key,
					 unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct nitrox_crypto_ctx *nctx = crypto_tfm_ctx(tfm);
	struct flexi_crypto_context *fctx;
	union fc_ctx_flags *flags;
	enum flexi_cipher cipher_type;
	const char *name;

	name = crypto_tfm_alg_name(tfm);
	cipher_type = flexi_cipher_type(name);
	if (unlikely(cipher_type == CIPHER_INVALID)) {
		pr_err("unsupported cipher: %s\n", name);
		return -EINVAL;
	}

	/* fill crypto context */
	fctx = nctx->u.fctx;
	flags = &fctx->flags;
	flags->f = 0;
	flags->w0.cipher_type = cipher_type;
	flags->w0.aes_keylen = aes_keylen;
	flags->w0.iv_source = IV_FROM_DPTR;
	flags->f = cpu_to_be64(*(u64 *)&flags->w0);
	/* copy the key to context */
	memcpy(fctx->crypto.u.key, key, keylen);

	return 0;
}

static int nitrox_aes_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	int aes_keylen;

	aes_keylen = flexi_aes_keylen(keylen);
	if (aes_keylen < 0) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return nitrox_skcipher_setkey(cipher, aes_keylen, key, keylen);
}

static int alloc_src_sglist(struct skcipher_request *skreq, int ivsize)
{
	struct nitrox_kcrypt_request *nkreq = skcipher_request_ctx(skreq);
	int nents = sg_nents(skreq->src) + 1;
	int ret;

	/* Allocate buffer to hold IV and input scatterlist array */
	ret = alloc_src_req_buf(nkreq, nents, ivsize);
	if (ret)
		return ret;

	nitrox_creq_copy_iv(nkreq->src, skreq->iv, ivsize);
	nitrox_creq_set_src_sg(nkreq, nents, ivsize, skreq->src,
			       skreq->cryptlen);

	return 0;
}

static int alloc_dst_sglist(struct skcipher_request *skreq, int ivsize)
{
	struct nitrox_kcrypt_request *nkreq = skcipher_request_ctx(skreq);
	int nents = sg_nents(skreq->dst) + 3;
	int ret;

	/* Allocate buffer to hold ORH, COMPLETION and output scatterlist
	 * array
	 */
	ret = alloc_dst_req_buf(nkreq, nents);
	if (ret)
		return ret;

	nitrox_creq_set_orh(nkreq);
	nitrox_creq_set_comp(nkreq);
	nitrox_creq_set_dst_sg(nkreq, nents, ivsize, skreq->dst,
			       skreq->cryptlen);

	return 0;
}

static void free_src_sglist(struct skcipher_request *skreq)
{
	struct nitrox_kcrypt_request *nkreq = skcipher_request_ctx(skreq);

	kfree(nkreq->src);
}

static void free_dst_sglist(struct skcipher_request *skreq)
{
	struct nitrox_kcrypt_request *nkreq = skcipher_request_ctx(skreq);

	kfree(nkreq->dst);
}

static void nitrox_skcipher_callback(void *arg, int err)
{
	struct skcipher_request *skreq = arg;

	free_src_sglist(skreq);
	free_dst_sglist(skreq);
	if (err) {
		pr_err_ratelimited("request failed status 0x%0x\n", err);
		err = -EINVAL;
	}

	skcipher_request_complete(skreq, err);
}

static int nitrox_skcipher_crypt(struct skcipher_request *skreq, bool enc)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(skreq);
	struct nitrox_crypto_ctx *nctx = crypto_skcipher_ctx(cipher);
	struct nitrox_kcrypt_request *nkreq = skcipher_request_ctx(skreq);
	int ivsize = crypto_skcipher_ivsize(cipher);
	struct se_crypto_request *creq;
	int ret;

	creq = &nkreq->creq;
	creq->flags = skreq->base.flags;
	creq->gfp = (skreq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		     GFP_KERNEL : GFP_ATOMIC;

	/* fill the request */
	creq->ctrl.value = 0;
	creq->opcode = FLEXI_CRYPTO_ENCRYPT_HMAC;
	creq->ctrl.s.arg = (enc ? ENCRYPT : DECRYPT);
	/* param0: length of the data to be encrypted */
	creq->gph.param0 = cpu_to_be16(skreq->cryptlen);
	creq->gph.param1 = 0;
	/* param2: encryption data offset */
	creq->gph.param2 = cpu_to_be16(ivsize);
	creq->gph.param3 = 0;

	creq->ctx_handle = nctx->u.ctx_handle;
	creq->ctrl.s.ctxl = sizeof(struct flexi_crypto_context);

	ret = alloc_src_sglist(skreq, ivsize);
	if (ret)
		return ret;

	ret = alloc_dst_sglist(skreq, ivsize);
	if (ret) {
		free_src_sglist(skreq);
		return ret;
	}

	/* send the crypto request */
	return nitrox_process_se_request(nctx->ndev, creq,
					 nitrox_skcipher_callback, skreq);
}

static int nitrox_aes_encrypt(struct skcipher_request *skreq)
{
	return nitrox_skcipher_crypt(skreq, true);
}

static int nitrox_aes_decrypt(struct skcipher_request *skreq)
{
	return nitrox_skcipher_crypt(skreq, false);
}

static int nitrox_3des_setkey(struct crypto_skcipher *cipher,
			      const u8 *key, unsigned int keylen)
{
	return verify_skcipher_des3_key(cipher, key) ?:
	       nitrox_skcipher_setkey(cipher, 0, key, keylen);
}

static int nitrox_3des_encrypt(struct skcipher_request *skreq)
{
	return nitrox_skcipher_crypt(skreq, true);
}

static int nitrox_3des_decrypt(struct skcipher_request *skreq)
{
	return nitrox_skcipher_crypt(skreq, false);
}

static int nitrox_aes_xts_setkey(struct crypto_skcipher *cipher,
				 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct nitrox_crypto_ctx *nctx = crypto_tfm_ctx(tfm);
	struct flexi_crypto_context *fctx;
	int aes_keylen, ret;

	ret = xts_check_key(tfm, key, keylen);
	if (ret)
		return ret;

	keylen /= 2;

	aes_keylen = flexi_aes_keylen(keylen);
	if (aes_keylen < 0) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	fctx = nctx->u.fctx;
	/* copy KEY2 */
	memcpy(fctx->auth.u.key2, (key + keylen), keylen);

	return nitrox_skcipher_setkey(cipher, aes_keylen, key, keylen);
}

static int nitrox_aes_ctr_rfc3686_setkey(struct crypto_skcipher *cipher,
					 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct nitrox_crypto_ctx *nctx = crypto_tfm_ctx(tfm);
	struct flexi_crypto_context *fctx;
	int aes_keylen;

	if (keylen < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	fctx = nctx->u.fctx;

	memcpy(fctx->crypto.iv, key + (keylen - CTR_RFC3686_NONCE_SIZE),
	       CTR_RFC3686_NONCE_SIZE);

	keylen -= CTR_RFC3686_NONCE_SIZE;

	aes_keylen = flexi_aes_keylen(keylen);
	if (aes_keylen < 0) {
		crypto_skcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	return nitrox_skcipher_setkey(cipher, aes_keylen, key, keylen);
}

static struct skcipher_alg nitrox_skciphers[] = { {
	.base = {
		.cra_name = "cbc(aes)",
		.cra_driver_name = "n5_cbc(aes)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.setkey = nitrox_aes_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "ecb(aes)",
		.cra_driver_name = "n5_ecb(aes)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.setkey = nitrox_aes_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "cfb(aes)",
		.cra_driver_name = "n5_cfb(aes)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.setkey = nitrox_aes_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "xts(aes)",
		.cra_driver_name = "n5_xts(aes)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = 2 * AES_MIN_KEY_SIZE,
	.max_keysize = 2 * AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.setkey = nitrox_aes_xts_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "rfc3686(ctr(aes))",
		.cra_driver_name = "n5_rfc3686(ctr(aes))",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.ivsize = CTR_RFC3686_IV_SIZE,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
	.setkey = nitrox_aes_ctr_rfc3686_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
}, {
	.base = {
		.cra_name = "cts(cbc(aes))",
		.cra_driver_name = "n5_cts(cbc(aes))",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
	.setkey = nitrox_aes_setkey,
	.encrypt = nitrox_aes_encrypt,
	.decrypt = nitrox_aes_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "cbc(des3_ede)",
		.cra_driver_name = "n5_cbc(des3_ede)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = DES3_EDE_BLOCK_SIZE,
	.setkey = nitrox_3des_setkey,
	.encrypt = nitrox_3des_encrypt,
	.decrypt = nitrox_3des_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}, {
	.base = {
		.cra_name = "ecb(des3_ede)",
		.cra_driver_name = "n5_ecb(des3_ede)",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = DES3_EDE_BLOCK_SIZE,
	.setkey = nitrox_3des_setkey,
	.encrypt = nitrox_3des_encrypt,
	.decrypt = nitrox_3des_decrypt,
	.init = nitrox_skcipher_init,
	.exit = nitrox_skcipher_exit,
}

};

int nitrox_register_skciphers(void)
{
	return crypto_register_skciphers(nitrox_skciphers,
					 ARRAY_SIZE(nitrox_skciphers));
}

void nitrox_unregister_skciphers(void)
{
	crypto_unregister_skciphers(nitrox_skciphers,
				    ARRAY_SIZE(nitrox_skciphers));
}
