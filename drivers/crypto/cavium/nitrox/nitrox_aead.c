// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/crypto.h>
#include <linux/rtnetlink.h>

#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/internal/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/gcm.h>

#include "nitrox_dev.h"
#include "nitrox_common.h"
#include "nitrox_req.h"

#define GCM_AES_SALT_SIZE	4

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
	if (aes_keylen < 0)
		return -EINVAL;

	/* fill crypto context */
	fctx = nctx->u.fctx;
	flags.fu = be64_to_cpu(fctx->flags.f);
	flags.w0.aes_keylen = aes_keylen;
	fctx->flags.f = cpu_to_be64(flags.fu);

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

	flags.fu = be64_to_cpu(fctx->flags.f);
	flags.w0.mac_len = authsize;
	fctx->flags.f = cpu_to_be64(flags.fu);

	aead->authsize = authsize;

	return 0;
}

static int nitrox_aes_gcm_setauthsize(struct crypto_aead *aead,
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

	return nitrox_aead_setauthsize(aead, authsize);
}

static int alloc_src_sglist(struct nitrox_kcrypt_request *nkreq,
			    struct scatterlist *src, char *iv, int ivsize,
			    int buflen)
{
	int nents = sg_nents_for_len(src, buflen);
	int ret;

	if (nents < 0)
		return nents;

	/* IV entry */
	nents += 1;
	/* Allocate buffer to hold IV and input scatterlist array */
	ret = alloc_src_req_buf(nkreq, nents, ivsize);
	if (ret)
		return ret;

	nitrox_creq_copy_iv(nkreq->src, iv, ivsize);
	nitrox_creq_set_src_sg(nkreq, nents, ivsize, src, buflen);

	return 0;
}

static int alloc_dst_sglist(struct nitrox_kcrypt_request *nkreq,
			    struct scatterlist *dst, int ivsize, int buflen)
{
	int nents = sg_nents_for_len(dst, buflen);
	int ret;

	if (nents < 0)
		return nents;

	/* IV, ORH, COMPLETION entries */
	nents += 3;
	/* Allocate buffer to hold ORH, COMPLETION and output scatterlist
	 * array
	 */
	ret = alloc_dst_req_buf(nkreq, nents);
	if (ret)
		return ret;

	nitrox_creq_set_orh(nkreq);
	nitrox_creq_set_comp(nkreq);
	nitrox_creq_set_dst_sg(nkreq, nents, ivsize, dst, buflen);

	return 0;
}

static void free_src_sglist(struct nitrox_kcrypt_request *nkreq)
{
	kfree(nkreq->src);
}

static void free_dst_sglist(struct nitrox_kcrypt_request *nkreq)
{
	kfree(nkreq->dst);
}

static int nitrox_set_creq(struct nitrox_aead_rctx *rctx)
{
	struct se_crypto_request *creq = &rctx->nkreq.creq;
	union gph_p3 param3;
	int ret;

	creq->flags = rctx->flags;
	creq->gfp = (rctx->flags & CRYPTO_TFM_REQ_MAY_SLEEP) ? GFP_KERNEL :
							       GFP_ATOMIC;

	creq->ctrl.value = 0;
	creq->opcode = FLEXI_CRYPTO_ENCRYPT_HMAC;
	creq->ctrl.s.arg = rctx->ctrl_arg;

	creq->gph.param0 = cpu_to_be16(rctx->cryptlen);
	creq->gph.param1 = cpu_to_be16(rctx->cryptlen + rctx->assoclen);
	creq->gph.param2 = cpu_to_be16(rctx->ivsize + rctx->assoclen);
	param3.iv_offset = 0;
	param3.auth_offset = rctx->ivsize;
	creq->gph.param3 = cpu_to_be16(param3.param);

	creq->ctx_handle = rctx->ctx_handle;
	creq->ctrl.s.ctxl = sizeof(struct flexi_crypto_context);

	ret = alloc_src_sglist(&rctx->nkreq, rctx->src, rctx->iv, rctx->ivsize,
			       rctx->srclen);
	if (ret)
		return ret;

	ret = alloc_dst_sglist(&rctx->nkreq, rctx->dst, rctx->ivsize,
			       rctx->dstlen);
	if (ret) {
		free_src_sglist(&rctx->nkreq);
		return ret;
	}

	return 0;
}

static void nitrox_aead_callback(void *arg, int err)
{
	struct aead_request *areq = arg;
	struct nitrox_aead_rctx *rctx = aead_request_ctx(areq);

	free_src_sglist(&rctx->nkreq);
	free_dst_sglist(&rctx->nkreq);
	if (err) {
		pr_err_ratelimited("request failed status 0x%0x\n", err);
		err = -EINVAL;
	}

	areq->base.complete(&areq->base, err);
}

static inline bool nitrox_aes_gcm_assoclen_supported(unsigned int assoclen)
{
	if (assoclen <= 512)
		return true;

	return false;
}

static int nitrox_aes_gcm_enc(struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct nitrox_aead_rctx *rctx = aead_request_ctx(areq);
	struct se_crypto_request *creq = &rctx->nkreq.creq;
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	int ret;

	if (!nitrox_aes_gcm_assoclen_supported(areq->assoclen))
		return -EINVAL;

	memcpy(fctx->crypto.iv, areq->iv, GCM_AES_SALT_SIZE);

	rctx->cryptlen = areq->cryptlen;
	rctx->assoclen = areq->assoclen;
	rctx->srclen = areq->assoclen + areq->cryptlen;
	rctx->dstlen = rctx->srclen + aead->authsize;
	rctx->iv = &areq->iv[GCM_AES_SALT_SIZE];
	rctx->ivsize = GCM_AES_IV_SIZE - GCM_AES_SALT_SIZE;
	rctx->flags = areq->base.flags;
	rctx->ctx_handle = nctx->u.ctx_handle;
	rctx->src = areq->src;
	rctx->dst = areq->dst;
	rctx->ctrl_arg = ENCRYPT;
	ret = nitrox_set_creq(rctx);
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
	struct nitrox_aead_rctx *rctx = aead_request_ctx(areq);
	struct se_crypto_request *creq = &rctx->nkreq.creq;
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	int ret;

	if (!nitrox_aes_gcm_assoclen_supported(areq->assoclen))
		return -EINVAL;

	memcpy(fctx->crypto.iv, areq->iv, GCM_AES_SALT_SIZE);

	rctx->cryptlen = areq->cryptlen - aead->authsize;
	rctx->assoclen = areq->assoclen;
	rctx->srclen = areq->cryptlen + areq->assoclen;
	rctx->dstlen = rctx->srclen - aead->authsize;
	rctx->iv = &areq->iv[GCM_AES_SALT_SIZE];
	rctx->ivsize = GCM_AES_IV_SIZE - GCM_AES_SALT_SIZE;
	rctx->flags = areq->base.flags;
	rctx->ctx_handle = nctx->u.ctx_handle;
	rctx->src = areq->src;
	rctx->dst = areq->dst;
	rctx->ctrl_arg = DECRYPT;
	ret = nitrox_set_creq(rctx);
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

static int nitrox_gcm_common_init(struct crypto_aead *aead)
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
	flags->f = cpu_to_be64(flags->fu);

	return 0;
}

static int nitrox_aes_gcm_init(struct crypto_aead *aead)
{
	int ret;

	ret = nitrox_gcm_common_init(aead);
	if (ret)
		return ret;

	crypto_aead_set_reqsize(aead,
				sizeof(struct aead_request) +
					sizeof(struct nitrox_aead_rctx));

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

static int nitrox_rfc4106_setkey(struct crypto_aead *aead, const u8 *key,
				 unsigned int keylen)
{
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct flexi_crypto_context *fctx = nctx->u.fctx;
	int ret;

	if (keylen < GCM_AES_SALT_SIZE)
		return -EINVAL;

	keylen -= GCM_AES_SALT_SIZE;
	ret = nitrox_aes_gcm_setkey(aead, key, keylen);
	if (ret)
		return ret;

	memcpy(fctx->crypto.iv, key + keylen, GCM_AES_SALT_SIZE);
	return 0;
}

static int nitrox_rfc4106_setauthsize(struct crypto_aead *aead,
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

	return nitrox_aead_setauthsize(aead, authsize);
}

static int nitrox_rfc4106_set_aead_rctx_sglist(struct aead_request *areq)
{
	struct nitrox_rfc4106_rctx *rctx = aead_request_ctx(areq);
	struct nitrox_aead_rctx *aead_rctx = &rctx->base;
	unsigned int assoclen = areq->assoclen - GCM_RFC4106_IV_SIZE;
	struct scatterlist *sg;

	if (areq->assoclen != 16 && areq->assoclen != 20)
		return -EINVAL;

	scatterwalk_map_and_copy(rctx->assoc, areq->src, 0, assoclen, 0);
	sg_init_table(rctx->src, 3);
	sg_set_buf(rctx->src, rctx->assoc, assoclen);
	sg = scatterwalk_ffwd(rctx->src + 1, areq->src, areq->assoclen);
	if (sg != rctx->src + 1)
		sg_chain(rctx->src, 2, sg);

	if (areq->src != areq->dst) {
		sg_init_table(rctx->dst, 3);
		sg_set_buf(rctx->dst, rctx->assoc, assoclen);
		sg = scatterwalk_ffwd(rctx->dst + 1, areq->dst, areq->assoclen);
		if (sg != rctx->dst + 1)
			sg_chain(rctx->dst, 2, sg);
	}

	aead_rctx->src = rctx->src;
	aead_rctx->dst = (areq->src == areq->dst) ? rctx->src : rctx->dst;

	return 0;
}

static void nitrox_rfc4106_callback(void *arg, int err)
{
	struct aead_request *areq = arg;
	struct nitrox_rfc4106_rctx *rctx = aead_request_ctx(areq);
	struct nitrox_kcrypt_request *nkreq = &rctx->base.nkreq;

	free_src_sglist(nkreq);
	free_dst_sglist(nkreq);
	if (err) {
		pr_err_ratelimited("request failed status 0x%0x\n", err);
		err = -EINVAL;
	}

	areq->base.complete(&areq->base, err);
}

static int nitrox_rfc4106_enc(struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct nitrox_rfc4106_rctx *rctx = aead_request_ctx(areq);
	struct nitrox_aead_rctx *aead_rctx = &rctx->base;
	struct se_crypto_request *creq = &aead_rctx->nkreq.creq;
	int ret;

	aead_rctx->cryptlen = areq->cryptlen;
	aead_rctx->assoclen = areq->assoclen - GCM_RFC4106_IV_SIZE;
	aead_rctx->srclen = aead_rctx->assoclen + aead_rctx->cryptlen;
	aead_rctx->dstlen = aead_rctx->srclen + aead->authsize;
	aead_rctx->iv = areq->iv;
	aead_rctx->ivsize = GCM_RFC4106_IV_SIZE;
	aead_rctx->flags = areq->base.flags;
	aead_rctx->ctx_handle = nctx->u.ctx_handle;
	aead_rctx->ctrl_arg = ENCRYPT;

	ret = nitrox_rfc4106_set_aead_rctx_sglist(areq);
	if (ret)
		return ret;

	ret = nitrox_set_creq(aead_rctx);
	if (ret)
		return ret;

	/* send the crypto request */
	return nitrox_process_se_request(nctx->ndev, creq,
					 nitrox_rfc4106_callback, areq);
}

static int nitrox_rfc4106_dec(struct aead_request *areq)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(areq);
	struct nitrox_crypto_ctx *nctx = crypto_aead_ctx(aead);
	struct nitrox_rfc4106_rctx *rctx = aead_request_ctx(areq);
	struct nitrox_aead_rctx *aead_rctx = &rctx->base;
	struct se_crypto_request *creq = &aead_rctx->nkreq.creq;
	int ret;

	aead_rctx->cryptlen = areq->cryptlen - aead->authsize;
	aead_rctx->assoclen = areq->assoclen - GCM_RFC4106_IV_SIZE;
	aead_rctx->srclen =
		areq->cryptlen - GCM_RFC4106_IV_SIZE + areq->assoclen;
	aead_rctx->dstlen = aead_rctx->srclen - aead->authsize;
	aead_rctx->iv = areq->iv;
	aead_rctx->ivsize = GCM_RFC4106_IV_SIZE;
	aead_rctx->flags = areq->base.flags;
	aead_rctx->ctx_handle = nctx->u.ctx_handle;
	aead_rctx->ctrl_arg = DECRYPT;

	ret = nitrox_rfc4106_set_aead_rctx_sglist(areq);
	if (ret)
		return ret;

	ret = nitrox_set_creq(aead_rctx);
	if (ret)
		return ret;

	/* send the crypto request */
	return nitrox_process_se_request(nctx->ndev, creq,
					 nitrox_rfc4106_callback, areq);
}

static int nitrox_rfc4106_init(struct crypto_aead *aead)
{
	int ret;

	ret = nitrox_gcm_common_init(aead);
	if (ret)
		return ret;

	crypto_aead_set_reqsize(aead, sizeof(struct aead_request) +
				sizeof(struct nitrox_rfc4106_rctx));

	return 0;
}

static struct aead_alg nitrox_aeads[] = { {
	.base = {
		.cra_name = "gcm(aes)",
		.cra_driver_name = "n5_aes_gcm",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.setkey = nitrox_aes_gcm_setkey,
	.setauthsize = nitrox_aes_gcm_setauthsize,
	.encrypt = nitrox_aes_gcm_enc,
	.decrypt = nitrox_aes_gcm_dec,
	.init = nitrox_aes_gcm_init,
	.exit = nitrox_aead_exit,
	.ivsize = GCM_AES_IV_SIZE,
	.maxauthsize = AES_BLOCK_SIZE,
}, {
	.base = {
		.cra_name = "rfc4106(gcm(aes))",
		.cra_driver_name = "n5_rfc4106",
		.cra_priority = PRIO,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_blocksize = 1,
		.cra_ctxsize = sizeof(struct nitrox_crypto_ctx),
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.setkey = nitrox_rfc4106_setkey,
	.setauthsize = nitrox_rfc4106_setauthsize,
	.encrypt = nitrox_rfc4106_enc,
	.decrypt = nitrox_rfc4106_dec,
	.init = nitrox_rfc4106_init,
	.exit = nitrox_aead_exit,
	.ivsize = GCM_RFC4106_IV_SIZE,
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
