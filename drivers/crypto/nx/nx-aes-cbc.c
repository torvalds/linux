// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES CBC routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2011-2012 International Business Machines Inc.
 *
 * Author: Kent Yoder <yoder1@us.ibm.com>
 */

#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


static int cbc_aes_nx_set_key(struct crypto_skcipher *tfm,
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

	csbcpb->cpb.hdr.mode = NX_MODE_AES_CBC;
	memcpy(csbcpb->cpb.aes_cbc.key, in_key, key_len);

	return 0;
}

static int cbc_aes_nx_crypt(struct skcipher_request *req,
			    int                      enc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct nx_crypto_ctx *nx_ctx = crypto_skcipher_ctx(tfm);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	unsigned long irq_flags;
	unsigned int processed = 0, to_process;
	int rc;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	if (enc)
		NX_CPB_FDM(csbcpb) |= NX_FDM_ENDE_ENCRYPT;
	else
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_ENDE_ENCRYPT;

	do {
		to_process = req->cryptlen - processed;

		rc = nx_build_sg_lists(nx_ctx, req->iv, req->dst, req->src,
				       &to_process, processed,
				       csbcpb->cpb.aes_cbc.iv);
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

		memcpy(req->iv, csbcpb->cpb.aes_cbc.cv, AES_BLOCK_SIZE);
		atomic_inc(&(nx_ctx->stats->aes_ops));
		atomic64_add(be32_to_cpu(csbcpb->csb.processed_byte_count),
			     &(nx_ctx->stats->aes_bytes));

		processed += to_process;
	} while (processed < req->cryptlen);
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int cbc_aes_nx_encrypt(struct skcipher_request *req)
{
	return cbc_aes_nx_crypt(req, 1);
}

static int cbc_aes_nx_decrypt(struct skcipher_request *req)
{
	return cbc_aes_nx_crypt(req, 0);
}

struct skcipher_alg nx_cbc_aes_alg = {
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "cbc-aes-nx",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct nx_crypto_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,
	.init			= nx_crypto_ctx_aes_cbc_init,
	.exit			= nx_crypto_ctx_skcipher_exit,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= cbc_aes_nx_set_key,
	.encrypt		= cbc_aes_nx_encrypt,
	.decrypt		= cbc_aes_nx_decrypt,
};
