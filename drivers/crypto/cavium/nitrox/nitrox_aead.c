// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/crypto.h>
#include <linux/rtnetlink.h>

#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/sha.h>
#include <crypto/internal/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/gcm.h>

#include "nitrox_dev.h"
#include "nitrox_common.h"
#include "nitrox_req.h"

#define GCM_AES_SALT_SIZE	4

/**
 * struct nitrox_crypt_params - Params to set nitrox crypto request.
 * @cryptlen: Encryption/Decryption data length
 * @authlen: Assoc data length + Cryptlen
 * @srclen: Input buffer length
 * @dstlen: Output buffer length
 * @iv: IV data
 * @ivsize: IV data length
 * @ctrl_arg: Identifies the request type (ENCRYPT/DECRYPT)
 */
struct nitrox_crypt_params {
	unsigned int cryptlen;
	unsigned int authlen;
	unsigned int srclen;
	unsigned int dstlen;
	u8 *iv;
	int ivsize;
	u8 ctrl_arg;
};

union gph_p3 {
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u16 iv_offset : 8;
		u16 auth_offset	: 8;
#else
		u16 auth_offset	: 8;
		u16 iv_offset : 8;
#endif
	};
	u16 param;
};

static int nitrox_aes_gcm_setkey(struct crypto_aead *aead, const u8 *key,
				 unsigned int keylen)
{
	int aes_keylen;
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct flexi_crypto_context *fctx;
	union fc_ctx_flags flags;

	aes_keylen = flexi_aes_keylen(keylen);
	if (aes_keylen < 0) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	/* fill crypto context */
	fctx = nctx->u.fctx;
	flags.f = be64_to_cpu(fctx->flags.f);
	flags.w0.aes_keylen = aes_keylen;
	fctx->flags.f = cpu_to_be64(flags.f);

	/* copy enc key to context */
	memset(&fctx->crypto, 0, sizeof(fctx->crypto));
	memcpy(fctx->crypto.u.key, key, keylen);

	return 0;
}

static int nitrox_aead_setauthsize(struct crypto_aead *aead,
				   unsigned int authsize)
{
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	union fc_ctx_flags flags;

	flags.f = be64_to_cpu(fctx->flags.f);
	flags.w0.mac_len = authsize;
	fctx->flags.f = cpu_to_be64(flags.f);

	aead->authsize = authsize;

	return 0;
}

static int alloc_src_sglist(struct aead_request *areq, char *iv, int ivsize,
			    int buflen)
{
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);
	int nents = sg_nents_for_len(areq->src, buflen) + 1;
	int ret;

	if (nents < 0)
		return nents;

	/* Allocate buffer to hold IV and input scatterlist array */
	ret = alloc_src_req_buf(nkreq, nents, ivsize);
	if (ret)
		return ret;

	nitrox_creq_copy_iv(nkreq->src, iv, ivsize);
	nitrox_creq_set_src_sg(nkreq, nents, ivsize, areq->src, buflen);

	return 0;
}

static int alloc_dst_sglist(struct aead_request *areq, int ivsize, int buflen)
{
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);
	int nents = sg_nents_for_len(areq->dst, buflen) + 3;
	int ret;

	if (nents < 0)
		return nents;

	/* Allocate buffer to hold ORH, COMPLETION and output scatterlist
	 * array
	 */
	ret = alloc_dst_req_buf(nkreq, nents);
	if (ret)
		return ret;

	nitrox_creq_set_orh(nkreq);
	nitrox_creq_set_comp(nkreq);
	nitrox_creq_set_dst_sg(nkreq, nents, ivsize, areq->dst, buflen);

	return 0;
}

static void free_src_sglist(struct aead_request *areq)
{
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);

	kfree(nkreq->src);
}

static void free_dst_sglist(struct aead_request *areq)
{
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);

	kfree(nkreq->dst);
}

static int nitrox_set_creq(struct aead_request *areq,
			   struct nitrox_crypt_params *params)
{
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);
	struct se_crypto_request *creq = &nkreq->creq;
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	union gph_p3 param3;
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	int ret;

	creq->flags = areq->base.flags;
	creq->gfp = (areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		GFP_KERNEL : GFP_ATOMIC;

	creq->ctrl.value = 0;
	creq->opcode = FLEXI_CRYPTO_ENCRYPT_HMAC;
	creq->ctrl.s.arg = params->ctrl_arg;

	creq->gph.param0 = cpu_to_be16(params->cryptlen);
	creq->gph.param1 = cpu_to_be16(params->authlen);
	creq->gph.param2 = cpu_to_be16(params->ivsize + areq->assoclen);
	param3.iv_offset = 0;
	param3.auth_offset = params->ivsize;
	creq->gph.param3 = cpu_to_be16(param3.param);

	creq->ctx_handle = nctx->u.ctx_handle;
	creq->ctrl.s.ctxl = sizeof(struct flexi_crypto_context);

	ret = alloc_src_sglist(areq, params->iv, params->ivsize,
			       params->srclen);
	if (ret)
		return ret;

	ret = alloc_dst_sglist(areq, params->ivsize, params->dstlen);
	if (ret) {
		free_src_sglist(areq);
		return ret;
	}

	return 0;
}

static void nitrox_aead_callback(void *arg, int err)
{
	struct aead_request *areq = arg;

	free_src_sglist(areq);
	free_dst_sglist(areq);
	if (err) {
		pr_err_ratelimited("request failed status 0x%0x\n", err);
		err = -EINVAL;
	}

	areq->base.complete(&areq->base, err);
}

static int nitrox_aes_gcm_enc(struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);
	struct se_crypto_request *creq = &nkreq->creq;
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	struct nitrox_crypt_params params;
	int ret;

	memcpy(fctx->crypto.iv, areq->iv, GCM_AES_SALT_SIZE);

	memset(&params, 0, sizeof(params));
	params.cryptlen = areq->cryptlen;
	params.authlen = areq->assoclen + params.cryptlen;
	params.srclen = params.authlen;
	params.dstlen = params.srclen + aead->authsize;
	params.iv = &areq->iv[GCM_AES_SALT_SIZE];
	params.ivsize = GCM_AES_IV_SIZE - GCM_AES_SALT_SIZE;
	params.ctrl_arg = ENCRYPT;
	ret = nitrox_set_creq(areq, &params);
	if (ret)
		return ret;

	/* send the crypto request */
	return nitrox_process_se_request(nctx->ndev, creq, nitrox_aead_callback,
					 areq);
}

static int nitrox_aes_gcm_dec(struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct nitrox_kcrypt_request *nkreq = aead_request_ctx(areq);
	struct se_crypto_request *creq = &nkreq->creq;
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	struct nitrox_crypt_params params;
	int ret;

	memcpy(fctx->crypto.iv, areq->iv, GCM_AES_SALT_SIZE);

	memset(&params, 0, sizeof(params));
	params.cryptlen = areq->cryptlen - aead->authsize;
	params.authlen = areq->assoclen + params.cryptlen;
	params.srclen = areq->cryptlen + areq->assoclen;
	params.dstlen = params.srclen - aead->authsize;
	params.iv = &areq->iv[GCM_AES_SALT_SIZE];
	params.ivsize = GCM_AES_IV_SIZE - GCM_AES_SALT_SIZE;
	params.ctrl_arg = DECRYPT;
	ret = nitrox_set_creq(areq, &params);
	if (ret)
		return ret;

	/* send the crypto request */
	return nitrox_process_se_request(nctx->ndev, creq, nitrox_aead_callback,
					 areq);
}

static int nitrox_aead_init(struct crypto_aead *aead)
{
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
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
	nctx->u.fctx->flags.f = 0;

	return 0;
}

static int nitrox_aes_gcm_init(struct crypto_aead *aead)
{
	int ret;
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	union fc_ctx_flags *flags;

	ret = nitrox_aead_init(aead);
	if (ret)
		return ret;

	flags = &nctx->u.fctx->flags;
	flags->w0.cipher_type = CIPHER_AES_GCM;
	flags->w0.hash_type = AUTH_NULL;
	flags->w0.iv_source = IV_FROM_DPTR;
	/* ask microcode to calculate ipad/opad */
	flags->w0.auth_input_type = 1;
	flags->f = be64_to_cpu(flags->f);

	crypto_aead_set_reqsize(aead, sizeof(struct aead_request) +
				sizeof(struct nitrox_kcrypt_request));

	return 0;
}

static void nitrox_aead_exit(struct crypto_aead *aead)
{
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);

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

static struct aead_alg nitrox_aeads[] = { {
	.base = {
		.cra_name = "gcm(aes)",
		.cra_driver_name = "n5_aes_gcm",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.setkey = nitrox_aes_gcm_setkey,
	.setauthsize = nitrox_aead_setauthsize,
	.encrypt = nitrox_aes_gcm_enc,
	.decrypt = nitrox_aes_gcm_dec,
	.init = nitrox_aes_gcm_init,
	.exit = nitrox_aead_exit,
	.ivsize = GCM_AES_IV_SIZE,
	.maxauthsize = AES_BLOCK_SIZE,
} };

int nitrox_register_aeads(void)
{
	return crypto_register_aeads(nitrox_aeads, ARRAY_SIZE(nitrox_aeads));
}

void nitrox_unregister_aeads(void)
{
	crypto_unregister_aeads(nitrox_aeads, ARRAY_SIZE(nitrox_aeads));
}
