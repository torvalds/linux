// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <crypto/aes.h>
#include <crypto/authenc.h>
#include <crypto/cryptd.h>
#include <crypto/des.h>
#include <crypto/internal/aead.h>
#include <crypto/sha.h>
#include <crypto/xts.h>
#include <crypto/scatterwalk.h>
#include <linux/rtnetlink.h>
#include <linux/sort.h>
#include <linux/module.h>
#include "otx_cptvf.h"
#include "otx_cptvf_algs.h"
#include "otx_cptvf_reqmgr.h"

#define CPT_MAX_VF_NUM	64
/* Size of salt in AES GCM mode */
#define AES_GCM_SALT_SIZE	4
/* Size of IV in AES GCM mode */
#define AES_GCM_IV_SIZE		8
/* Size of ICV (Integrity Check Value) in AES GCM mode */
#define AES_GCM_ICV_SIZE	16
/* Offset of IV in AES GCM mode */
#define AES_GCM_IV_OFFSET	8
#define CONTROL_WORD_LEN	8
#define KEY2_OFFSET		48
#define DMA_MODE_FLAG(dma_mode) \
	(((dma_mode) == OTX_CPT_DMA_GATHER_SCATTER) ? (1 << 7) : 0)

/* Truncated SHA digest size */
#define SHA1_TRUNC_DIGEST_SIZE		12
#define SHA256_TRUNC_DIGEST_SIZE	16
#define SHA384_TRUNC_DIGEST_SIZE	24
#define SHA512_TRUNC_DIGEST_SIZE	32

static DEFINE_MUTEX(mutex);
static int is_crypto_registered;

struct cpt_device_desc {
	enum otx_cptpf_type pf_type;
	struct pci_dev *dev;
	int num_queues;
};

struct cpt_device_table {
	atomic_t count;
	struct cpt_device_desc desc[CPT_MAX_VF_NUM];
};

static struct cpt_device_table se_devices = {
	.count = ATOMIC_INIT(0)
};

static struct cpt_device_table ae_devices = {
	.count = ATOMIC_INIT(0)
};

static inline int get_se_device(struct pci_dev **pdev, int *cpu_num)
{
	int count, ret = 0;

	count = atomic_read(&se_devices.count);
	if (count < 1)
		return -ENODEV;

	*cpu_num = get_cpu();

	if (se_devices.desc[0].pf_type == OTX_CPT_SE) {
		/*
		 * On OcteonTX platform there is one CPT instruction queue bound
		 * to each VF. We get maximum performance if one CPT queue
		 * is available for each cpu otherwise CPT queues need to be
		 * shared between cpus.
		 */
		if (*cpu_num >= count)
			*cpu_num %= count;
		*pdev = se_devices.desc[*cpu_num].dev;
	} else {
		pr_err("Unknown PF type %d\n", se_devices.desc[0].pf_type);
		ret = -EINVAL;
	}
	put_cpu();

	return ret;
}

static inline int validate_hmac_cipher_null(struct otx_cpt_req_info *cpt_req)
{
	struct otx_cpt_req_ctx *rctx;
	struct aead_request *req;
	struct crypto_aead *tfm;

	req = container_of(cpt_req->areq, struct aead_request, base);
	tfm = crypto_aead_reqtfm(req);
	rctx = aead_request_ctx(req);
	if (memcmp(rctx->fctx.hmac.s.hmac_calc,
		   rctx->fctx.hmac.s.hmac_recv,
		   crypto_aead_authsize(tfm)) != 0)
		return -EBADMSG;

	return 0;
}

static void otx_cpt_aead_callback(int status, void *arg1, void *arg2)
{
	struct otx_cpt_info_buffer *cpt_info = arg2;
	struct crypto_async_request *areq = arg1;
	struct otx_cpt_req_info *cpt_req;
	struct pci_dev *pdev;

	if (!cpt_info)
		goto complete;

	cpt_req = cpt_info->req;
	if (!status) {
		/*
		 * When selected cipher is NULL we need to manually
		 * verify whether calculated hmac value matches
		 * received hmac value
		 */
		if (cpt_req->req_type == OTX_CPT_AEAD_ENC_DEC_NULL_REQ &&
		    !cpt_req->is_enc)
			status = validate_hmac_cipher_null(cpt_req);
	}
	pdev = cpt_info->pdev;
	do_request_cleanup(pdev, cpt_info);

complete:
	if (areq)
		areq->complete(areq, status);
}

static void output_iv_copyback(struct crypto_async_request *areq)
{
	struct otx_cpt_req_info *req_info;
	struct skcipher_request *sreq;
	struct crypto_skcipher *stfm;
	struct otx_cpt_req_ctx *rctx;
	struct otx_cpt_enc_ctx *ctx;
	u32 start, ivsize;

	sreq = container_of(areq, struct skcipher_request, base);
	stfm = crypto_skcipher_reqtfm(sreq);
	ctx = crypto_skcipher_ctx(stfm);
	if (ctx->cipher_type == OTX_CPT_AES_CBC ||
	    ctx->cipher_type == OTX_CPT_DES3_CBC) {
		rctx = skcipher_request_ctx(sreq);
		req_info = &rctx->cpt_req;
		ivsize = crypto_skcipher_ivsize(stfm);
		start = sreq->cryptlen - ivsize;

		if (req_info->is_enc) {
			scatterwalk_map_and_copy(sreq->iv, sreq->dst, start,
						 ivsize, 0);
		} else {
			if (sreq->src != sreq->dst) {
				scatterwalk_map_and_copy(sreq->iv, sreq->src,
							 start, ivsize, 0);
			} else {
				memcpy(sreq->iv, req_info->iv_out, ivsize);
				kfree(req_info->iv_out);
			}
		}
	}
}

static void otx_cpt_skcipher_callback(int status, void *arg1, void *arg2)
{
	struct otx_cpt_info_buffer *cpt_info = arg2;
	struct crypto_async_request *areq = arg1;
	struct pci_dev *pdev;

	if (areq) {
		if (!status)
			output_iv_copyback(areq);
		if (cpt_info) {
			pdev = cpt_info->pdev;
			do_request_cleanup(pdev, cpt_info);
		}
		areq->complete(areq, status);
	}
}

static inline void update_input_data(struct otx_cpt_req_info *req_info,
				     struct scatterlist *inp_sg,
				     u32 nbytes, u32 *argcnt)
{
	req_info->req.dlen += nbytes;

	while (nbytes) {
		u32 len = min(nbytes, inp_sg->length);
		u8 *ptr = sg_virt(inp_sg);

		req_info->in[*argcnt].vptr = (void *)ptr;
		req_info->in[*argcnt].size = len;
		nbytes -= len;
		++(*argcnt);
		inp_sg = sg_next(inp_sg);
	}
}

static inline void update_output_data(struct otx_cpt_req_info *req_info,
				      struct scatterlist *outp_sg,
				      u32 offset, u32 nbytes, u32 *argcnt)
{
	req_info->rlen += nbytes;

	while (nbytes) {
		u32 len = min(nbytes, outp_sg->length - offset);
		u8 *ptr = sg_virt(outp_sg);

		req_info->out[*argcnt].vptr = (void *) (ptr + offset);
		req_info->out[*argcnt].size = len;
		nbytes -= len;
		++(*argcnt);
		offset = 0;
		outp_sg = sg_next(outp_sg);
	}
}

static inline u32 create_ctx_hdr(struct skcipher_request *req, u32 enc,
				 u32 *argcnt)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct otx_cpt_req_ctx *rctx = skcipher_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	struct crypto_tfm *tfm = crypto_skcipher_tfm(stfm);
	struct otx_cpt_enc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct otx_cpt_fc_ctx *fctx = &rctx->fctx;
	int ivsize = crypto_skcipher_ivsize(stfm);
	u32 start = req->cryptlen - ivsize;
	gfp_t flags;

	flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
			GFP_KERNEL : GFP_ATOMIC;
	req_info->ctrl.s.dma_mode = OTX_CPT_DMA_GATHER_SCATTER;
	req_info->ctrl.s.se_req = OTX_CPT_SE_CORE_REQ;

	req_info->req.opcode.s.major = OTX_CPT_MAJOR_OP_FC |
				DMA_MODE_FLAG(OTX_CPT_DMA_GATHER_SCATTER);
	if (enc) {
		req_info->req.opcode.s.minor = 2;
	} else {
		req_info->req.opcode.s.minor = 3;
		if ((ctx->cipher_type == OTX_CPT_AES_CBC ||
		    ctx->cipher_type == OTX_CPT_DES3_CBC) &&
		    req->src == req->dst) {
			req_info->iv_out = kmalloc(ivsize, flags);
			if (!req_info->iv_out)
				return -ENOMEM;

			scatterwalk_map_and_copy(req_info->iv_out, req->src,
						 start, ivsize, 0);
		}
	}
	/* Encryption data length */
	req_info->req.param1 = req->cryptlen;
	/* Authentication data length */
	req_info->req.param2 = 0;

	fctx->enc.enc_ctrl.e.enc_cipher = ctx->cipher_type;
	fctx->enc.enc_ctrl.e.aes_key = ctx->key_type;
	fctx->enc.enc_ctrl.e.iv_source = OTX_CPT_FROM_CPTR;

	if (ctx->cipher_type == OTX_CPT_AES_XTS)
		memcpy(fctx->enc.encr_key, ctx->enc_key, ctx->key_len * 2);
	else
		memcpy(fctx->enc.encr_key, ctx->enc_key, ctx->key_len);

	memcpy(fctx->enc.encr_iv, req->iv, crypto_skcipher_ivsize(stfm));

	fctx->enc.enc_ctrl.flags = cpu_to_be64(fctx->enc.enc_ctrl.cflags);

	/*
	 * Storing  Packet Data Information in offset
	 * Control Word First 8 bytes
	 */
	req_info->in[*argcnt].vptr = (u8 *)&rctx->ctrl_word;
	req_info->in[*argcnt].size = CONTROL_WORD_LEN;
	req_info->req.dlen += CONTROL_WORD_LEN;
	++(*argcnt);

	req_info->in[*argcnt].vptr = (u8 *)fctx;
	req_info->in[*argcnt].size = sizeof(struct otx_cpt_fc_ctx);
	req_info->req.dlen += sizeof(struct otx_cpt_fc_ctx);

	++(*argcnt);

	return 0;
}

static inline u32 create_input_list(struct skcipher_request *req, u32 enc,
				    u32 enc_iv_len)
{
	struct otx_cpt_req_ctx *rctx = skcipher_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	u32 argcnt =  0;
	int ret;

	ret = create_ctx_hdr(req, enc, &argcnt);
	if (ret)
		return ret;

	update_input_data(req_info, req->src, req->cryptlen, &argcnt);
	req_info->incnt = argcnt;

	return 0;
}

static inline void create_output_list(struct skcipher_request *req,
				      u32 enc_iv_len)
{
	struct otx_cpt_req_ctx *rctx = skcipher_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	u32 argcnt = 0;

	/*
	 * OUTPUT Buffer Processing
	 * AES encryption/decryption output would be
	 * received in the following format
	 *
	 * ------IV--------|------ENCRYPTED/DECRYPTED DATA-----|
	 * [ 16 Bytes/     [   Request Enc/Dec/ DATA Len AES CBC ]
	 */
	update_output_data(req_info, req->dst, 0, req->cryptlen, &argcnt);
	req_info->outcnt = argcnt;
}

static inline int cpt_enc_dec(struct skcipher_request *req, u32 enc)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct otx_cpt_req_ctx *rctx = skcipher_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	u32 enc_iv_len = crypto_skcipher_ivsize(stfm);
	struct pci_dev *pdev;
	int status, cpu_num;

	/* Validate that request doesn't exceed maximum CPT supported size */
	if (req->cryptlen > OTX_CPT_MAX_REQ_SIZE)
		return -E2BIG;

	/* Clear control words */
	rctx->ctrl_word.flags = 0;
	rctx->fctx.enc.enc_ctrl.flags = 0;

	status = create_input_list(req, enc, enc_iv_len);
	if (status)
		return status;
	create_output_list(req, enc_iv_len);

	status = get_se_device(&pdev, &cpu_num);
	if (status)
		return status;

	req_info->callback = (void *)otx_cpt_skcipher_callback;
	req_info->areq = &req->base;
	req_info->req_type = OTX_CPT_ENC_DEC_REQ;
	req_info->is_enc = enc;
	req_info->is_trunc_hmac = false;
	req_info->ctrl.s.grp = 0;

	/*
	 * We perform an asynchronous send and once
	 * the request is completed the driver would
	 * intimate through registered call back functions
	 */
	status = otx_cpt_do_request(pdev, req_info, cpu_num);

	return status;
}

static int otx_cpt_skcipher_encrypt(struct skcipher_request *req)
{
	return cpt_enc_dec(req, true);
}

static int otx_cpt_skcipher_decrypt(struct skcipher_request *req)
{
	return cpt_enc_dec(req, false);
}

static int otx_cpt_skcipher_xts_setkey(struct crypto_skcipher *tfm,
				       const u8 *key, u32 keylen)
{
	struct otx_cpt_enc_ctx *ctx = crypto_skcipher_ctx(tfm);
	const u8 *key2 = key + (keylen / 2);
	const u8 *key1 = key;
	int ret;

	ret = xts_check_key(crypto_skcipher_tfm(tfm), key, keylen);
	if (ret)
		return ret;
	ctx->key_len = keylen;
	memcpy(ctx->enc_key, key1, keylen / 2);
	memcpy(ctx->enc_key + KEY2_OFFSET, key2, keylen / 2);
	ctx->cipher_type = OTX_CPT_AES_XTS;
	switch (ctx->key_len) {
	case 2 * AES_KEYSIZE_128:
		ctx->key_type = OTX_CPT_AES_128_BIT;
		break;
	case 2 * AES_KEYSIZE_256:
		ctx->key_type = OTX_CPT_AES_256_BIT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpt_des_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  u32 keylen, u8 cipher_type)
{
	struct otx_cpt_enc_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (keylen != DES3_EDE_KEY_SIZE)
		return -EINVAL;

	ctx->key_len = keylen;
	ctx->cipher_type = cipher_type;

	memcpy(ctx->enc_key, key, keylen);

	return 0;
}

static int cpt_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  u32 keylen, u8 cipher_type)
{
	struct otx_cpt_enc_ctx *ctx = crypto_skcipher_ctx(tfm);

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->key_type = OTX_CPT_AES_128_BIT;
		break;
	case AES_KEYSIZE_192:
		ctx->key_type = OTX_CPT_AES_192_BIT;
		break;
	case AES_KEYSIZE_256:
		ctx->key_type = OTX_CPT_AES_256_BIT;
		break;
	default:
		return -EINVAL;
	}
	ctx->key_len = keylen;
	ctx->cipher_type = cipher_type;

	memcpy(ctx->enc_key, key, keylen);

	return 0;
}

static int otx_cpt_skcipher_cbc_aes_setkey(struct crypto_skcipher *tfm,
					   const u8 *key, u32 keylen)
{
	return cpt_aes_setkey(tfm, key, keylen, OTX_CPT_AES_CBC);
}

static int otx_cpt_skcipher_ecb_aes_setkey(struct crypto_skcipher *tfm,
					   const u8 *key, u32 keylen)
{
	return cpt_aes_setkey(tfm, key, keylen, OTX_CPT_AES_ECB);
}

static int otx_cpt_skcipher_cfb_aes_setkey(struct crypto_skcipher *tfm,
					   const u8 *key, u32 keylen)
{
	return cpt_aes_setkey(tfm, key, keylen, OTX_CPT_AES_CFB);
}

static int otx_cpt_skcipher_cbc_des3_setkey(struct crypto_skcipher *tfm,
					    const u8 *key, u32 keylen)
{
	return cpt_des_setkey(tfm, key, keylen, OTX_CPT_DES3_CBC);
}

static int otx_cpt_skcipher_ecb_des3_setkey(struct crypto_skcipher *tfm,
					    const u8 *key, u32 keylen)
{
	return cpt_des_setkey(tfm, key, keylen, OTX_CPT_DES3_ECB);
}

static int otx_cpt_enc_dec_init(struct crypto_skcipher *tfm)
{
	struct otx_cpt_enc_ctx *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	/*
	 * Additional memory for skcipher_request is
	 * allocated since the cryptd daemon uses
	 * this memory for request_ctx information
	 */
	crypto_skcipher_set_reqsize(tfm, sizeof(struct otx_cpt_req_ctx) +
					sizeof(struct skcipher_request));

	return 0;
}

static int cpt_aead_init(struct crypto_aead *tfm, u8 cipher_type, u8 mac_type)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(tfm);

	ctx->cipher_type = cipher_type;
	ctx->mac_type = mac_type;

	/*
	 * When selected cipher is NULL we use HMAC opcode instead of
	 * FLEXICRYPTO opcode therefore we don't need to use HASH algorithms
	 * for calculating ipad and opad
	 */
	if (ctx->cipher_type != OTX_CPT_CIPHER_NULL) {
		switch (ctx->mac_type) {
		case OTX_CPT_SHA1:
			ctx->hashalg = crypto_alloc_shash("sha1", 0,
							  CRYPTO_ALG_ASYNC);
			if (IS_ERR(ctx->hashalg))
				return PTR_ERR(ctx->hashalg);
			break;

		case OTX_CPT_SHA256:
			ctx->hashalg = crypto_alloc_shash("sha256", 0,
							  CRYPTO_ALG_ASYNC);
			if (IS_ERR(ctx->hashalg))
				return PTR_ERR(ctx->hashalg);
			break;

		case OTX_CPT_SHA384:
			ctx->hashalg = crypto_alloc_shash("sha384", 0,
							  CRYPTO_ALG_ASYNC);
			if (IS_ERR(ctx->hashalg))
				return PTR_ERR(ctx->hashalg);
			break;

		case OTX_CPT_SHA512:
			ctx->hashalg = crypto_alloc_shash("sha512", 0,
							  CRYPTO_ALG_ASYNC);
			if (IS_ERR(ctx->hashalg))
				return PTR_ERR(ctx->hashalg);
			break;
		}
	}

	crypto_aead_set_reqsize(tfm, sizeof(struct otx_cpt_req_ctx));

	return 0;
}

static int otx_cpt_aead_cbc_aes_sha1_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_AES_CBC, OTX_CPT_SHA1);
}

static int otx_cpt_aead_cbc_aes_sha256_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_AES_CBC, OTX_CPT_SHA256);
}

static int otx_cpt_aead_cbc_aes_sha384_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_AES_CBC, OTX_CPT_SHA384);
}

static int otx_cpt_aead_cbc_aes_sha512_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_AES_CBC, OTX_CPT_SHA512);
}

static int otx_cpt_aead_ecb_null_sha1_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_CIPHER_NULL, OTX_CPT_SHA1);
}

static int otx_cpt_aead_ecb_null_sha256_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_CIPHER_NULL, OTX_CPT_SHA256);
}

static int otx_cpt_aead_ecb_null_sha384_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_CIPHER_NULL, OTX_CPT_SHA384);
}

static int otx_cpt_aead_ecb_null_sha512_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_CIPHER_NULL, OTX_CPT_SHA512);
}

static int otx_cpt_aead_gcm_aes_init(struct crypto_aead *tfm)
{
	return cpt_aead_init(tfm, OTX_CPT_AES_GCM, OTX_CPT_MAC_NULL);
}

static void otx_cpt_aead_exit(struct crypto_aead *tfm)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(tfm);

	kfree(ctx->ipad);
	kfree(ctx->opad);
	if (ctx->hashalg)
		crypto_free_shash(ctx->hashalg);
	kfree(ctx->sdesc);
}

/*
 * This is the Integrity Check Value validation (aka the authentication tag
 * length)
 */
static int otx_cpt_aead_set_authsize(struct crypto_aead *tfm,
				     unsigned int authsize)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(tfm);

	switch (ctx->mac_type) {
	case OTX_CPT_SHA1:
		if (authsize != SHA1_DIGEST_SIZE &&
		    authsize != SHA1_TRUNC_DIGEST_SIZE)
			return -EINVAL;

		if (authsize == SHA1_TRUNC_DIGEST_SIZE)
			ctx->is_trunc_hmac = true;
		break;

	case OTX_CPT_SHA256:
		if (authsize != SHA256_DIGEST_SIZE &&
		    authsize != SHA256_TRUNC_DIGEST_SIZE)
			return -EINVAL;

		if (authsize == SHA256_TRUNC_DIGEST_SIZE)
			ctx->is_trunc_hmac = true;
		break;

	case OTX_CPT_SHA384:
		if (authsize != SHA384_DIGEST_SIZE &&
		    authsize != SHA384_TRUNC_DIGEST_SIZE)
			return -EINVAL;

		if (authsize == SHA384_TRUNC_DIGEST_SIZE)
			ctx->is_trunc_hmac = true;
		break;

	case OTX_CPT_SHA512:
		if (authsize != SHA512_DIGEST_SIZE &&
		    authsize != SHA512_TRUNC_DIGEST_SIZE)
			return -EINVAL;

		if (authsize == SHA512_TRUNC_DIGEST_SIZE)
			ctx->is_trunc_hmac = true;
		break;

	case OTX_CPT_MAC_NULL:
		if (ctx->cipher_type == OTX_CPT_AES_GCM) {
			if (authsize != AES_GCM_ICV_SIZE)
				return -EINVAL;
		} else
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	tfm->authsize = authsize;
	return 0;
}

static struct otx_cpt_sdesc *alloc_sdesc(struct crypto_shash *alg)
{
	struct otx_cpt_sdesc *sdesc;
	int size;

	size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return NULL;

	sdesc->shash.tfm = alg;

	return sdesc;
}

static inline void swap_data32(void *buf, u32 len)
{
	cpu_to_be32_array(buf, buf, len / 4);
}

static inline void swap_data64(void *buf, u32 len)
{
	__be64 *dst = buf;
	u64 *src = buf;
	int i = 0;

	for (i = 0 ; i < len / 8; i++, src++, dst++)
		*dst = cpu_to_be64p(src);
}

static int copy_pad(u8 mac_type, u8 *out_pad, u8 *in_pad)
{
	struct sha512_state *sha512;
	struct sha256_state *sha256;
	struct sha1_state *sha1;

	switch (mac_type) {
	case OTX_CPT_SHA1:
		sha1 = (struct sha1_state *) in_pad;
		swap_data32(sha1->state, SHA1_DIGEST_SIZE);
		memcpy(out_pad, &sha1->state, SHA1_DIGEST_SIZE);
		break;

	case OTX_CPT_SHA256:
		sha256 = (struct sha256_state *) in_pad;
		swap_data32(sha256->state, SHA256_DIGEST_SIZE);
		memcpy(out_pad, &sha256->state, SHA256_DIGEST_SIZE);
		break;

	case OTX_CPT_SHA384:
	case OTX_CPT_SHA512:
		sha512 = (struct sha512_state *) in_pad;
		swap_data64(sha512->state, SHA512_DIGEST_SIZE);
		memcpy(out_pad, &sha512->state, SHA512_DIGEST_SIZE);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int aead_hmac_init(struct crypto_aead *cipher)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(cipher);
	int state_size = crypto_shash_statesize(ctx->hashalg);
	int ds = crypto_shash_digestsize(ctx->hashalg);
	int bs = crypto_shash_blocksize(ctx->hashalg);
	int authkeylen = ctx->auth_key_len;
	u8 *ipad = NULL, *opad = NULL;
	int ret = 0, icount = 0;

	ctx->sdesc = alloc_sdesc(ctx->hashalg);
	if (!ctx->sdesc)
		return -ENOMEM;

	ctx->ipad = kzalloc(bs, GFP_KERNEL);
	if (!ctx->ipad) {
		ret = -ENOMEM;
		goto calc_fail;
	}

	ctx->opad = kzalloc(bs, GFP_KERNEL);
	if (!ctx->opad) {
		ret = -ENOMEM;
		goto calc_fail;
	}

	ipad = kzalloc(state_size, GFP_KERNEL);
	if (!ipad) {
		ret = -ENOMEM;
		goto calc_fail;
	}

	opad = kzalloc(state_size, GFP_KERNEL);
	if (!opad) {
		ret = -ENOMEM;
		goto calc_fail;
	}

	if (authkeylen > bs) {
		ret = crypto_shash_digest(&ctx->sdesc->shash, ctx->key,
					  authkeylen, ipad);
		if (ret)
			goto calc_fail;

		authkeylen = ds;
	} else {
		memcpy(ipad, ctx->key, authkeylen);
	}

	memset(ipad + authkeylen, 0, bs - authkeylen);
	memcpy(opad, ipad, bs);

	for (icount = 0; icount < bs; icount++) {
		ipad[icount] ^= 0x36;
		opad[icount] ^= 0x5c;
	}

	/*
	 * Partial Hash calculated from the software
	 * algorithm is retrieved for IPAD & OPAD
	 */

	/* IPAD Calculation */
	crypto_shash_init(&ctx->sdesc->shash);
	crypto_shash_update(&ctx->sdesc->shash, ipad, bs);
	crypto_shash_export(&ctx->sdesc->shash, ipad);
	ret = copy_pad(ctx->mac_type, ctx->ipad, ipad);
	if (ret)
		goto calc_fail;

	/* OPAD Calculation */
	crypto_shash_init(&ctx->sdesc->shash);
	crypto_shash_update(&ctx->sdesc->shash, opad, bs);
	crypto_shash_export(&ctx->sdesc->shash, opad);
	ret = copy_pad(ctx->mac_type, ctx->opad, opad);
	if (ret)
		goto calc_fail;

	kfree(ipad);
	kfree(opad);

	return 0;

calc_fail:
	kfree(ctx->ipad);
	ctx->ipad = NULL;
	kfree(ctx->opad);
	ctx->opad = NULL;
	kfree(ipad);
	kfree(opad);
	kfree(ctx->sdesc);
	ctx->sdesc = NULL;

	return ret;
}

static int otx_cpt_aead_cbc_aes_sha_setkey(struct crypto_aead *cipher,
					   const unsigned char *key,
					   unsigned int keylen)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(cipher);
	struct crypto_authenc_key_param *param;
	int enckeylen = 0, authkeylen = 0;
	struct rtattr *rta = (void *)key;
	int status = -EINVAL;

	if (!RTA_OK(rta, keylen))
		goto badkey;

	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		goto badkey;

	if (RTA_PAYLOAD(rta) < sizeof(*param))
		goto badkey;

	param = RTA_DATA(rta);
	enckeylen = be32_to_cpu(param->enckeylen);
	key += RTA_ALIGN(rta->rta_len);
	keylen -= RTA_ALIGN(rta->rta_len);
	if (keylen < enckeylen)
		goto badkey;

	if (keylen > OTX_CPT_MAX_KEY_SIZE)
		goto badkey;

	authkeylen = keylen - enckeylen;
	memcpy(ctx->key, key, keylen);

	switch (enckeylen) {
	case AES_KEYSIZE_128:
		ctx->key_type = OTX_CPT_AES_128_BIT;
		break;
	case AES_KEYSIZE_192:
		ctx->key_type = OTX_CPT_AES_192_BIT;
		break;
	case AES_KEYSIZE_256:
		ctx->key_type = OTX_CPT_AES_256_BIT;
		break;
	default:
		/* Invalid key length */
		goto badkey;
	}

	ctx->enc_key_len = enckeylen;
	ctx->auth_key_len = authkeylen;

	status = aead_hmac_init(cipher);
	if (status)
		goto badkey;

	return 0;
badkey:
	return status;
}

static int otx_cpt_aead_ecb_null_sha_setkey(struct crypto_aead *cipher,
					    const unsigned char *key,
					    unsigned int keylen)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(cipher);
	struct crypto_authenc_key_param *param;
	struct rtattr *rta = (void *)key;
	int enckeylen = 0;

	if (!RTA_OK(rta, keylen))
		goto badkey;

	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		goto badkey;

	if (RTA_PAYLOAD(rta) < sizeof(*param))
		goto badkey;

	param = RTA_DATA(rta);
	enckeylen = be32_to_cpu(param->enckeylen);
	key += RTA_ALIGN(rta->rta_len);
	keylen -= RTA_ALIGN(rta->rta_len);
	if (enckeylen != 0)
		goto badkey;

	if (keylen > OTX_CPT_MAX_KEY_SIZE)
		goto badkey;

	memcpy(ctx->key, key, keylen);
	ctx->enc_key_len = enckeylen;
	ctx->auth_key_len = keylen;
	return 0;
badkey:
	return -EINVAL;
}

static int otx_cpt_aead_gcm_aes_setkey(struct crypto_aead *cipher,
				       const unsigned char *key,
				       unsigned int keylen)
{
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(cipher);

	/*
	 * For aes gcm we expect to get encryption key (16, 24, 32 bytes)
	 * and salt (4 bytes)
	 */
	switch (keylen) {
	case AES_KEYSIZE_128 + AES_GCM_SALT_SIZE:
		ctx->key_type = OTX_CPT_AES_128_BIT;
		ctx->enc_key_len = AES_KEYSIZE_128;
		break;
	case AES_KEYSIZE_192 + AES_GCM_SALT_SIZE:
		ctx->key_type = OTX_CPT_AES_192_BIT;
		ctx->enc_key_len = AES_KEYSIZE_192;
		break;
	case AES_KEYSIZE_256 + AES_GCM_SALT_SIZE:
		ctx->key_type = OTX_CPT_AES_256_BIT;
		ctx->enc_key_len = AES_KEYSIZE_256;
		break;
	default:
		/* Invalid key and salt length */
		return -EINVAL;
	}

	/* Store encryption key and salt */
	memcpy(ctx->key, key, keylen);

	return 0;
}

static inline u32 create_aead_ctx_hdr(struct aead_request *req, u32 enc,
				      u32 *argcnt)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	struct otx_cpt_fc_ctx *fctx = &rctx->fctx;
	int mac_len = crypto_aead_authsize(tfm);
	int ds;

	rctx->ctrl_word.e.enc_data_offset = req->assoclen;

	switch (ctx->cipher_type) {
	case OTX_CPT_AES_CBC:
		fctx->enc.enc_ctrl.e.iv_source = OTX_CPT_FROM_CPTR;
		/* Copy encryption key to context */
		memcpy(fctx->enc.encr_key, ctx->key + ctx->auth_key_len,
		       ctx->enc_key_len);
		/* Copy IV to context */
		memcpy(fctx->enc.encr_iv, req->iv, crypto_aead_ivsize(tfm));

		ds = crypto_shash_digestsize(ctx->hashalg);
		if (ctx->mac_type == OTX_CPT_SHA384)
			ds = SHA512_DIGEST_SIZE;
		if (ctx->ipad)
			memcpy(fctx->hmac.e.ipad, ctx->ipad, ds);
		if (ctx->opad)
			memcpy(fctx->hmac.e.opad, ctx->opad, ds);
		break;

	case OTX_CPT_AES_GCM:
		fctx->enc.enc_ctrl.e.iv_source = OTX_CPT_FROM_DPTR;
		/* Copy encryption key to context */
		memcpy(fctx->enc.encr_key, ctx->key, ctx->enc_key_len);
		/* Copy salt to context */
		memcpy(fctx->enc.encr_iv, ctx->key + ctx->enc_key_len,
		       AES_GCM_SALT_SIZE);

		rctx->ctrl_word.e.iv_offset = req->assoclen - AES_GCM_IV_OFFSET;
		break;

	default:
		/* Unknown cipher type */
		return -EINVAL;
	}
	rctx->ctrl_word.flags = cpu_to_be64(rctx->ctrl_word.cflags);

	req_info->ctrl.s.dma_mode = OTX_CPT_DMA_GATHER_SCATTER;
	req_info->ctrl.s.se_req = OTX_CPT_SE_CORE_REQ;
	req_info->req.opcode.s.major = OTX_CPT_MAJOR_OP_FC |
				 DMA_MODE_FLAG(OTX_CPT_DMA_GATHER_SCATTER);
	if (enc) {
		req_info->req.opcode.s.minor = 2;
		req_info->req.param1 = req->cryptlen;
		req_info->req.param2 = req->cryptlen + req->assoclen;
	} else {
		req_info->req.opcode.s.minor = 3;
		req_info->req.param1 = req->cryptlen - mac_len;
		req_info->req.param2 = req->cryptlen + req->assoclen - mac_len;
	}

	fctx->enc.enc_ctrl.e.enc_cipher = ctx->cipher_type;
	fctx->enc.enc_ctrl.e.aes_key = ctx->key_type;
	fctx->enc.enc_ctrl.e.mac_type = ctx->mac_type;
	fctx->enc.enc_ctrl.e.mac_len = mac_len;
	fctx->enc.enc_ctrl.flags = cpu_to_be64(fctx->enc.enc_ctrl.cflags);

	/*
	 * Storing Packet Data Information in offset
	 * Control Word First 8 bytes
	 */
	req_info->in[*argcnt].vptr = (u8 *)&rctx->ctrl_word;
	req_info->in[*argcnt].size = CONTROL_WORD_LEN;
	req_info->req.dlen += CONTROL_WORD_LEN;
	++(*argcnt);

	req_info->in[*argcnt].vptr = (u8 *)fctx;
	req_info->in[*argcnt].size = sizeof(struct otx_cpt_fc_ctx);
	req_info->req.dlen += sizeof(struct otx_cpt_fc_ctx);
	++(*argcnt);

	return 0;
}

static inline u32 create_hmac_ctx_hdr(struct aead_request *req, u32 *argcnt,
				      u32 enc)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct otx_cpt_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;

	req_info->ctrl.s.dma_mode = OTX_CPT_DMA_GATHER_SCATTER;
	req_info->ctrl.s.se_req = OTX_CPT_SE_CORE_REQ;
	req_info->req.opcode.s.major = OTX_CPT_MAJOR_OP_HMAC |
				 DMA_MODE_FLAG(OTX_CPT_DMA_GATHER_SCATTER);
	req_info->is_trunc_hmac = ctx->is_trunc_hmac;

	req_info->req.opcode.s.minor = 0;
	req_info->req.param1 = ctx->auth_key_len;
	req_info->req.param2 = ctx->mac_type << 8;

	/* Add authentication key */
	req_info->in[*argcnt].vptr = ctx->key;
	req_info->in[*argcnt].size = round_up(ctx->auth_key_len, 8);
	req_info->req.dlen += round_up(ctx->auth_key_len, 8);
	++(*argcnt);

	return 0;
}

static inline u32 create_aead_input_list(struct aead_request *req, u32 enc)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	u32 inputlen =  req->cryptlen + req->assoclen;
	u32 status, argcnt = 0;

	status = create_aead_ctx_hdr(req, enc, &argcnt);
	if (status)
		return status;
	update_input_data(req_info, req->src, inputlen, &argcnt);
	req_info->incnt = argcnt;

	return 0;
}

static inline u32 create_aead_output_list(struct aead_request *req, u32 enc,
					  u32 mac_len)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct otx_cpt_req_info *req_info =  &rctx->cpt_req;
	u32 argcnt = 0, outputlen = 0;

	if (enc)
		outputlen = req->cryptlen +  req->assoclen + mac_len;
	else
		outputlen = req->cryptlen + req->assoclen - mac_len;

	update_output_data(req_info, req->dst, 0, outputlen, &argcnt);
	req_info->outcnt = argcnt;

	return 0;
}

static inline u32 create_aead_null_input_list(struct aead_request *req,
					      u32 enc, u32 mac_len)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	u32 inputlen, argcnt = 0;

	if (enc)
		inputlen =  req->cryptlen + req->assoclen;
	else
		inputlen =  req->cryptlen + req->assoclen - mac_len;

	create_hmac_ctx_hdr(req, &argcnt, enc);
	update_input_data(req_info, req->src, inputlen, &argcnt);
	req_info->incnt = argcnt;

	return 0;
}

static inline u32 create_aead_null_output_list(struct aead_request *req,
					       u32 enc, u32 mac_len)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct otx_cpt_req_info *req_info =  &rctx->cpt_req;
	struct scatterlist *dst;
	u8 *ptr = NULL;
	int argcnt = 0, status, offset;
	u32 inputlen;

	if (enc)
		inputlen =  req->cryptlen + req->assoclen;
	else
		inputlen =  req->cryptlen + req->assoclen - mac_len;

	/*
	 * If source and destination are different
	 * then copy payload to destination
	 */
	if (req->src != req->dst) {

		ptr = kmalloc(inputlen, (req_info->areq->flags &
					 CRYPTO_TFM_REQ_MAY_SLEEP) ?
					 GFP_KERNEL : GFP_ATOMIC);
		if (!ptr) {
			status = -ENOMEM;
			goto error;
		}

		status = sg_copy_to_buffer(req->src, sg_nents(req->src), ptr,
					   inputlen);
		if (status != inputlen) {
			status = -EINVAL;
			goto error_free;
		}
		status = sg_copy_from_buffer(req->dst, sg_nents(req->dst), ptr,
					     inputlen);
		if (status != inputlen) {
			status = -EINVAL;
			goto error_free;
		}
		kfree(ptr);
	}

	if (enc) {
		/*
		 * In an encryption scenario hmac needs
		 * to be appended after payload
		 */
		dst = req->dst;
		offset = inputlen;
		while (offset >= dst->length) {
			offset -= dst->length;
			dst = sg_next(dst);
			if (!dst) {
				status = -ENOENT;
				goto error;
			}
		}

		update_output_data(req_info, dst, offset, mac_len, &argcnt);
	} else {
		/*
		 * In a decryption scenario calculated hmac for received
		 * payload needs to be compare with hmac received
		 */
		status = sg_copy_buffer(req->src, sg_nents(req->src),
					rctx->fctx.hmac.s.hmac_recv, mac_len,
					inputlen, true);
		if (status != mac_len) {
			status = -EINVAL;
			goto error;
		}

		req_info->out[argcnt].vptr = rctx->fctx.hmac.s.hmac_calc;
		req_info->out[argcnt].size = mac_len;
		argcnt++;
	}

	req_info->outcnt = argcnt;
	return 0;

error_free:
	kfree(ptr);
error:
	return status;
}

static u32 cpt_aead_enc_dec(struct aead_request *req, u8 reg_type, u8 enc)
{
	struct otx_cpt_req_ctx *rctx = aead_request_ctx(req);
	struct otx_cpt_req_info *req_info = &rctx->cpt_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct pci_dev *pdev;
	u32 status, cpu_num;

	/* Clear control words */
	rctx->ctrl_word.flags = 0;
	rctx->fctx.enc.enc_ctrl.flags = 0;

	req_info->callback = otx_cpt_aead_callback;
	req_info->areq = &req->base;
	req_info->req_type = reg_type;
	req_info->is_enc = enc;
	req_info->is_trunc_hmac = false;

	switch (reg_type) {
	case OTX_CPT_AEAD_ENC_DEC_REQ:
		status = create_aead_input_list(req, enc);
		if (status)
			return status;
		status = create_aead_output_list(req, enc,
						 crypto_aead_authsize(tfm));
		if (status)
			return status;
		break;

	case OTX_CPT_AEAD_ENC_DEC_NULL_REQ:
		status = create_aead_null_input_list(req, enc,
						     crypto_aead_authsize(tfm));
		if (status)
			return status;
		status = create_aead_null_output_list(req, enc,
						crypto_aead_authsize(tfm));
		if (status)
			return status;
		break;

	default:
		return -EINVAL;
	}

	/* Validate that request doesn't exceed maximum CPT supported size */
	if (req_info->req.param1 > OTX_CPT_MAX_REQ_SIZE ||
	    req_info->req.param2 > OTX_CPT_MAX_REQ_SIZE)
		return -E2BIG;

	status = get_se_device(&pdev, &cpu_num);
	if (status)
		return status;

	req_info->ctrl.s.grp = 0;

	status = otx_cpt_do_request(pdev, req_info, cpu_num);
	/*
	 * We perform an asynchronous send and once
	 * the request is completed the driver would
	 * intimate through registered call back functions
	 */
	return status;
}

static int otx_cpt_aead_encrypt(struct aead_request *req)
{
	return cpt_aead_enc_dec(req, OTX_CPT_AEAD_ENC_DEC_REQ, true);
}

static int otx_cpt_aead_decrypt(struct aead_request *req)
{
	return cpt_aead_enc_dec(req, OTX_CPT_AEAD_ENC_DEC_REQ, false);
}

static int otx_cpt_aead_null_encrypt(struct aead_request *req)
{
	return cpt_aead_enc_dec(req, OTX_CPT_AEAD_ENC_DEC_NULL_REQ, true);
}

static int otx_cpt_aead_null_decrypt(struct aead_request *req)
{
	return cpt_aead_enc_dec(req, OTX_CPT_AEAD_ENC_DEC_NULL_REQ, false);
}

static struct skcipher_alg otx_cpt_skciphers[] = { {
	.base.cra_name = "xts(aes)",
	.base.cra_driver_name = "cpt_xts_aes",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_enc_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.ivsize = AES_BLOCK_SIZE,
	.min_keysize = 2 * AES_MIN_KEY_SIZE,
	.max_keysize = 2 * AES_MAX_KEY_SIZE,
	.setkey = otx_cpt_skcipher_xts_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
}, {
	.base.cra_name = "cbc(aes)",
	.base.cra_driver_name = "cpt_cbc_aes",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_enc_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.ivsize = AES_BLOCK_SIZE,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.setkey = otx_cpt_skcipher_cbc_aes_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
}, {
	.base.cra_name = "ecb(aes)",
	.base.cra_driver_name = "cpt_ecb_aes",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_enc_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.ivsize = 0,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.setkey = otx_cpt_skcipher_ecb_aes_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
}, {
	.base.cra_name = "cfb(aes)",
	.base.cra_driver_name = "cpt_cfb_aes",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_enc_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.ivsize = AES_BLOCK_SIZE,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.setkey = otx_cpt_skcipher_cfb_aes_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
}, {
	.base.cra_name = "cbc(des3_ede)",
	.base.cra_driver_name = "cpt_cbc_des3_ede",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_des3_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = DES_BLOCK_SIZE,
	.setkey = otx_cpt_skcipher_cbc_des3_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
}, {
	.base.cra_name = "ecb(des3_ede)",
	.base.cra_driver_name = "cpt_ecb_des3_ede",
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
	.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct otx_cpt_des3_ctx),
	.base.cra_alignmask = 7,
	.base.cra_priority = 4001,
	.base.cra_module = THIS_MODULE,

	.init = otx_cpt_enc_dec_init,
	.min_keysize = DES3_EDE_KEY_SIZE,
	.max_keysize = DES3_EDE_KEY_SIZE,
	.ivsize = 0,
	.setkey = otx_cpt_skcipher_ecb_des3_setkey,
	.encrypt = otx_cpt_skcipher_encrypt,
	.decrypt = otx_cpt_skcipher_decrypt,
} };

static struct aead_alg otx_cpt_aeads[] = { {
	.base = {
		.cra_name = "authenc(hmac(sha1),cbc(aes))",
		.cra_driver_name = "cpt_hmac_sha1_cbc_aes",
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_cbc_aes_sha1_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_cbc_aes_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_encrypt,
	.decrypt = otx_cpt_aead_decrypt,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA1_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha256),cbc(aes))",
		.cra_driver_name = "cpt_hmac_sha256_cbc_aes",
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_cbc_aes_sha256_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_cbc_aes_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_encrypt,
	.decrypt = otx_cpt_aead_decrypt,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA256_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha384),cbc(aes))",
		.cra_driver_name = "cpt_hmac_sha384_cbc_aes",
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_cbc_aes_sha384_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_cbc_aes_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_encrypt,
	.decrypt = otx_cpt_aead_decrypt,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA384_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha512),cbc(aes))",
		.cra_driver_name = "cpt_hmac_sha512_cbc_aes",
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_cbc_aes_sha512_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_cbc_aes_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_encrypt,
	.decrypt = otx_cpt_aead_decrypt,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA512_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha1),ecb(cipher_null))",
		.cra_driver_name = "cpt_hmac_sha1_ecb_null",
		.cra_blocksize = 1,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_ecb_null_sha1_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_ecb_null_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_null_encrypt,
	.decrypt = otx_cpt_aead_null_decrypt,
	.ivsize = 0,
	.maxauthsize = SHA1_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha256),ecb(cipher_null))",
		.cra_driver_name = "cpt_hmac_sha256_ecb_null",
		.cra_blocksize = 1,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_ecb_null_sha256_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_ecb_null_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_null_encrypt,
	.decrypt = otx_cpt_aead_null_decrypt,
	.ivsize = 0,
	.maxauthsize = SHA256_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha384),ecb(cipher_null))",
		.cra_driver_name = "cpt_hmac_sha384_ecb_null",
		.cra_blocksize = 1,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_ecb_null_sha384_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_ecb_null_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_null_encrypt,
	.decrypt = otx_cpt_aead_null_decrypt,
	.ivsize = 0,
	.maxauthsize = SHA384_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha512),ecb(cipher_null))",
		.cra_driver_name = "cpt_hmac_sha512_ecb_null",
		.cra_blocksize = 1,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_ecb_null_sha512_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_ecb_null_sha_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_null_encrypt,
	.decrypt = otx_cpt_aead_null_decrypt,
	.ivsize = 0,
	.maxauthsize = SHA512_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "rfc4106(gcm(aes))",
		.cra_driver_name = "cpt_rfc4106_gcm_aes",
		.cra_blocksize = 1,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct otx_cpt_aead_ctx),
		.cra_priority = 4001,
		.cra_alignmask = 0,
		.cra_module = THIS_MODULE,
	},
	.init = otx_cpt_aead_gcm_aes_init,
	.exit = otx_cpt_aead_exit,
	.setkey = otx_cpt_aead_gcm_aes_setkey,
	.setauthsize = otx_cpt_aead_set_authsize,
	.encrypt = otx_cpt_aead_encrypt,
	.decrypt = otx_cpt_aead_decrypt,
	.ivsize = AES_GCM_IV_SIZE,
	.maxauthsize = AES_GCM_ICV_SIZE,
} };

static inline int is_any_alg_used(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(otx_cpt_skciphers); i++)
		if (refcount_read(&otx_cpt_skciphers[i].base.cra_refcnt) != 1)
			return true;
	for (i = 0; i < ARRAY_SIZE(otx_cpt_aeads); i++)
		if (refcount_read(&otx_cpt_aeads[i].base.cra_refcnt) != 1)
			return true;
	return false;
}

static inline int cpt_register_algs(void)
{
	int i, err = 0;

	if (!IS_ENABLED(CONFIG_DM_CRYPT)) {
		for (i = 0; i < ARRAY_SIZE(otx_cpt_skciphers); i++)
			otx_cpt_skciphers[i].base.cra_flags &= ~CRYPTO_ALG_DEAD;

		err = crypto_register_skciphers(otx_cpt_skciphers,
						ARRAY_SIZE(otx_cpt_skciphers));
		if (err)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(otx_cpt_aeads); i++)
		otx_cpt_aeads[i].base.cra_flags &= ~CRYPTO_ALG_DEAD;

	err = crypto_register_aeads(otx_cpt_aeads, ARRAY_SIZE(otx_cpt_aeads));
	if (err) {
		crypto_unregister_skciphers(otx_cpt_skciphers,
					    ARRAY_SIZE(otx_cpt_skciphers));
		return err;
	}

	return 0;
}

static inline void cpt_unregister_algs(void)
{
	crypto_unregister_skciphers(otx_cpt_skciphers,
				    ARRAY_SIZE(otx_cpt_skciphers));
	crypto_unregister_aeads(otx_cpt_aeads, ARRAY_SIZE(otx_cpt_aeads));
}

static int compare_func(const void *lptr, const void *rptr)
{
	struct cpt_device_desc *ldesc = (struct cpt_device_desc *) lptr;
	struct cpt_device_desc *rdesc = (struct cpt_device_desc *) rptr;

	if (ldesc->dev->devfn < rdesc->dev->devfn)
		return -1;
	if (ldesc->dev->devfn > rdesc->dev->devfn)
		return 1;
	return 0;
}

static void swap_func(void *lptr, void *rptr, int size)
{
	struct cpt_device_desc *ldesc = (struct cpt_device_desc *) lptr;
	struct cpt_device_desc *rdesc = (struct cpt_device_desc *) rptr;
	struct cpt_device_desc desc;

	desc = *ldesc;
	*ldesc = *rdesc;
	*rdesc = desc;
}

int otx_cpt_crypto_init(struct pci_dev *pdev, struct module *mod,
			enum otx_cptpf_type pf_type,
			enum otx_cptvf_type engine_type,
			int num_queues, int num_devices)
{
	int ret = 0;
	int count;

	mutex_lock(&mutex);
	switch (engine_type) {
	case OTX_CPT_SE_TYPES:
		count = atomic_read(&se_devices.count);
		if (count >= CPT_MAX_VF_NUM) {
			dev_err(&pdev->dev, "No space to add a new device\n");
			ret = -ENOSPC;
			goto err;
		}
		se_devices.desc[count].pf_type = pf_type;
		se_devices.desc[count].num_queues = num_queues;
		se_devices.desc[count++].dev = pdev;
		atomic_inc(&se_devices.count);

		if (atomic_read(&se_devices.count) == num_devices &&
		    is_crypto_registered == false) {
			if (cpt_register_algs()) {
				dev_err(&pdev->dev,
				   "Error in registering crypto algorithms\n");
				ret =  -EINVAL;
				goto err;
			}
			try_module_get(mod);
			is_crypto_registered = true;
		}
		sort(se_devices.desc, count, sizeof(struct cpt_device_desc),
		     compare_func, swap_func);
		break;

	case OTX_CPT_AE_TYPES:
		count = atomic_read(&ae_devices.count);
		if (count >= CPT_MAX_VF_NUM) {
			dev_err(&pdev->dev, "No space to a add new device\n");
			ret = -ENOSPC;
			goto err;
		}
		ae_devices.desc[count].pf_type = pf_type;
		ae_devices.desc[count].num_queues = num_queues;
		ae_devices.desc[count++].dev = pdev;
		atomic_inc(&ae_devices.count);
		sort(ae_devices.desc, count, sizeof(struct cpt_device_desc),
		     compare_func, swap_func);
		break;

	default:
		dev_err(&pdev->dev, "Unknown VF type %d\n", engine_type);
		ret = BAD_OTX_CPTVF_TYPE;
	}
err:
	mutex_unlock(&mutex);
	return ret;
}

void otx_cpt_crypto_exit(struct pci_dev *pdev, struct module *mod,
			 enum otx_cptvf_type engine_type)
{
	struct cpt_device_table *dev_tbl;
	bool dev_found = false;
	int i, j, count;

	mutex_lock(&mutex);

	dev_tbl = (engine_type == OTX_CPT_AE_TYPES) ? &ae_devices : &se_devices;
	count = atomic_read(&dev_tbl->count);
	for (i = 0; i < count; i++)
		if (pdev == dev_tbl->desc[i].dev) {
			for (j = i; j < count-1; j++)
				dev_tbl->desc[j] = dev_tbl->desc[j+1];
			dev_found = true;
			break;
		}

	if (!dev_found) {
		dev_err(&pdev->dev, "%s device not found\n", __func__);
		goto exit;
	}

	if (engine_type != OTX_CPT_AE_TYPES) {
		if (atomic_dec_and_test(&se_devices.count) &&
		    !is_any_alg_used()) {
			cpt_unregister_algs();
			module_put(mod);
			is_crypto_registered = false;
		}
	} else
		atomic_dec(&ae_devices.count);
exit:
	mutex_unlock(&mutex);
}
