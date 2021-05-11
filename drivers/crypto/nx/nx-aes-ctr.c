// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES CTR routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2011-2012 International Business Machines Inc.
 *
 * Author: Kent Yoder <yoder1@us.ibm.com>
 */

#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


static int ctr_aes_nx_set_key(struct crypto_skcipher *tfm,
			      const u8               *in_key,
			      unsigned int            key_len)
{
	struct nx_crypto_ctx *nx_ctx = crypto_skcipher_ctx(tfm);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;

	nx_ctx_init(nx_ctx, HCOP_FC_AES);

	switch (key_len) {
	case AES_KEYSIZE_128:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_128);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_128];
		break;
	case AES_KEYSIZE_192:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_192);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_192];
		break;
	case AES_KEYSIZE_256:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_256);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_256];
		break;
	default:
		return -EINVAL;
	}

	csbcpb->cpb.hdr.mode = NX_MODE_AES_CTR;
	memcpy(csbcpb->cpb.aes_ctr.key, in_key, key_len);

	return 0;
}

static int ctr3686_aes_nx_set_key(struct crypto_skcipher *tfm,
				  const u8               *in_key,
				  unsigned int            key_len)
{
	struct nx_crypto_ctx *nx_ctx = crypto_skcipher_ctx(tfm);

	if (key_len < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	memcpy(nx_ctx->priv.ctr.nonce,
	       in_key + key_len - CTR_RFC3686_NONCE_SIZE,
	       CTR_RFC3686_NONCE_SIZE);

	key_len -= CTR_RFC3686_NONCE_SIZE;

	return ctr_aes_nx_set_key(tfm, in_key, key_len);
}

static int ctr_aes_nx_crypt(struct skcipher_request *req, u8 *iv)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct nx_crypto_ctx *nx_ctx = crypto_skcipher_ctx(tfm);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	unsigned long irq_flags;
	unsigned int processed = 0, to_process;
	int rc;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	do {
		to_process = req->cryptlen - processed;

		rc = nx_build_sg_lists(nx_ctx, iv, req->dst, req->src,
				       &to_process, processed,
				       csbcpb->cpb.aes_ctr.iv);
		if (rc)
			goto out;

		if (!nx_ctx->op.inlen || !nx_ctx->op.outlen) {
			rc = -EINVAL;
			goto out;
		}

		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
				   req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			goto out;

		memcpy(iv, csbcpb->cpb.aes_cbc.cv, AES_BLOCK_SIZE);

		atomic_inc(&(nx_ctx->stats->aes_ops));
		atomic64_add(csbcpb->csb.processed_byte_count,
			     &(nx_ctx->stats->aes_bytes));

		processed += to_process;
	} while (processed < req->cryptlen);
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int ctr3686_aes_nx_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct nx_crypto_ctx *nx_ctx = crypto_skcipher_ctx(tfm);
	u8 iv[16];

	memcpy(iv, nx_ctx->priv.ctr.nonce, CTR_RFC3686_IV_SIZE);
	memcpy(iv + CTR_RFC3686_NONCE_SIZE, req->iv, CTR_RFC3686_IV_SIZE);
	iv[12] = iv[13] = iv[14] = 0;
	iv[15] = 1;

	return ctr_aes_nx_crypt(req, iv);
}

struct skcipher_alg nx_ctr3686_aes_alg = {
	.base.cra_name		= "rfc3686(ctr(aes))",
	.base.cra_driver_name	= "rfc3686-ctr-aes-nx",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct nx_crypto_ctx),
	.base.cra_module	= THIS_MODULE,
	.init			= nx_crypto_ctx_aes_ctr_init,
	.exit			= nx_crypto_ctx_skcipher_exit,
	.min_keysize		= AES_MIN_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE + CTR_RFC3686_NONCE_SIZE,
	.ivsize			= CTR_RFC3686_IV_SIZE,
	.setkey			= ctr3686_aes_nx_set_key,
	.encrypt		= ctr3686_aes_nx_crypt,
	.decrypt		= ctr3686_aes_nx_crypt,
	.chunksize		= AES_BLOCK_SIZE,
};
