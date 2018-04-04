/**
 * AES GCM routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2012 International Business Machines Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Author: Kent Yoder <yoder1@us.ibm.com>
 */

#include <crypto/internal/aead.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


static int gcm_aes_nx_set_key(struct crypto_aead *tfm,
			      const u8           *in_key,
			      unsigned int        key_len)
{
	struct nx_crypto_ctx *nx_ctx = crypto_aead_ctx(tfm);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct nx_csbcpb *csbcpb_aead = nx_ctx->csbcpb_aead;

	nx_ctx_init(nx_ctx, HCOP_FC_AES);

	switch (key_len) {
	case AES_KEYSIZE_128:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_128);
		NX_CPB_SET_KEY_SIZE(csbcpb_aead, NX_KS_AES_128);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_128];
		break;
	case AES_KEYSIZE_192:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_192);
		NX_CPB_SET_KEY_SIZE(csbcpb_aead, NX_KS_AES_192);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_192];
		break;
	case AES_KEYSIZE_256:
		NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_256);
		NX_CPB_SET_KEY_SIZE(csbcpb_aead, NX_KS_AES_256);
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_256];
		break;
	default:
		return -EINVAL;
	}

	csbcpb->cpb.hdr.mode = NX_MODE_AES_GCM;
	memcpy(csbcpb->cpb.aes_gcm.key, in_key, key_len);

	csbcpb_aead->cpb.hdr.mode = NX_MODE_AES_GCA;
	memcpy(csbcpb_aead->cpb.aes_gca.key, in_key, key_len);

	return 0;
}

static int gcm4106_aes_nx_set_key(struct crypto_aead *tfm,
				  const u8           *in_key,
				  unsigned int        key_len)
{
	struct nx_crypto_ctx *nx_ctx = crypto_aead_ctx(tfm);
	char *nonce = nx_ctx->priv.gcm.nonce;
	int rc;

	if (key_len < 4)
		return -EINVAL;

	key_len -= 4;

	rc = gcm_aes_nx_set_key(tfm, in_key, key_len);
	if (rc)
		goto out;

	memcpy(nonce, in_key + key_len, 4);
out:
	return rc;
}

static int gcm4106_aes_nx_setauthsize(struct crypto_aead *tfm,
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

	return 0;
}

static int nx_gca(struct nx_crypto_ctx  *nx_ctx,
		  struct aead_request   *req,
		  u8                    *out,
		  unsigned int assoclen)
{
	int rc;
	struct nx_csbcpb *csbcpb_aead = nx_ctx->csbcpb_aead;
	struct scatter_walk walk;
	struct nx_sg *nx_sg = nx_ctx->in_sg;
	unsigned int nbytes = assoclen;
	unsigned int processed = 0, to_process;
	unsigned int max_sg_len;

	if (nbytes <= AES_BLOCK_SIZE) {
		scatterwalk_start(&walk, req->src);
		scatterwalk_copychunks(out, &walk, nbytes, SCATTERWALK_FROM_SG);
		scatterwalk_done(&walk, SCATTERWALK_FROM_SG, 0);
		return 0;
	}

	NX_CPB_FDM(csbcpb_aead) &= ~NX_FDM_CONTINUATION;

	/* page_limit: number of sg entries that fit on one page */
	max_sg_len = min_t(u64, nx_driver.of.max_sg_len/sizeof(struct nx_sg),
			   nx_ctx->ap->sglen);
	max_sg_len = min_t(u64, max_sg_len,
			   nx_ctx->ap->databytelen/NX_PAGE_SIZE);

	do {
		/*
		 * to_process: the data chunk to process in this update.
		 * This value is bound by sg list limits.
		 */
		to_process = min_t(u64, nbytes - processed,
				   nx_ctx->ap->databytelen);
		to_process = min_t(u64, to_process,
				   NX_PAGE_SIZE * (max_sg_len - 1));

		nx_sg = nx_walk_and_build(nx_ctx->in_sg, max_sg_len,
					  req->src, processed, &to_process);

		if ((to_process + processed) < nbytes)
			NX_CPB_FDM(csbcpb_aead) |= NX_FDM_INTERMEDIATE;
		else
			NX_CPB_FDM(csbcpb_aead) &= ~NX_FDM_INTERMEDIATE;

		nx_ctx->op_aead.inlen = (nx_ctx->in_sg - nx_sg)
					* sizeof(struct nx_sg);

		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op_aead,
				req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			return rc;

		memcpy(csbcpb_aead->cpb.aes_gca.in_pat,
				csbcpb_aead->cpb.aes_gca.out_pat,
				AES_BLOCK_SIZE);
		NX_CPB_FDM(csbcpb_aead) |= NX_FDM_CONTINUATION;

		atomic_inc(&(nx_ctx->stats->aes_ops));
		atomic64_add(assoclen, &(nx_ctx->stats->aes_bytes));

		processed += to_process;
	} while (processed < nbytes);

	memcpy(out, csbcpb_aead->cpb.aes_gca.out_pat, AES_BLOCK_SIZE);

	return rc;
}

static int gmac(struct aead_request *req, struct blkcipher_desc *desc,
		unsigned int assoclen)
{
	int rc;
	struct nx_crypto_ctx *nx_ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct nx_sg *nx_sg;
	unsigned int nbytes = assoclen;
	unsigned int processed = 0, to_process;
	unsigned int max_sg_len;

	/* Set GMAC mode */
	csbcpb->cpb.hdr.mode = NX_MODE_AES_GMAC;

	NX_CPB_FDM(csbcpb) &= ~NX_FDM_CONTINUATION;

	/* page_limit: number of sg entries that fit on one page */
	max_sg_len = min_t(u64, nx_driver.of.max_sg_len/sizeof(struct nx_sg),
			   nx_ctx->ap->sglen);
	max_sg_len = min_t(u64, max_sg_len,
			   nx_ctx->ap->databytelen/NX_PAGE_SIZE);

	/* Copy IV */
	memcpy(csbcpb->cpb.aes_gcm.iv_or_cnt, desc->info, AES_BLOCK_SIZE);

	do {
		/*
		 * to_process: the data chunk to process in this update.
		 * This value is bound by sg list limits.
		 */
		to_process = min_t(u64, nbytes - processed,
				   nx_ctx->ap->databytelen);
		to_process = min_t(u64, to_process,
				   NX_PAGE_SIZE * (max_sg_len - 1));

		nx_sg = nx_walk_and_build(nx_ctx->in_sg, max_sg_len,
					  req->src, processed, &to_process);

		if ((to_process + processed) < nbytes)
			NX_CPB_FDM(csbcpb) |= NX_FDM_INTERMEDIATE;
		else
			NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;

		nx_ctx->op.inlen = (nx_ctx->in_sg - nx_sg)
					* sizeof(struct nx_sg);

		csbcpb->cpb.aes_gcm.bit_length_data = 0;
		csbcpb->cpb.aes_gcm.bit_length_aad = 8 * nbytes;

		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
				req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			goto out;

		memcpy(csbcpb->cpb.aes_gcm.in_pat_or_aad,
			csbcpb->cpb.aes_gcm.out_pat_or_mac, AES_BLOCK_SIZE);
		memcpy(csbcpb->cpb.aes_gcm.in_s0,
			csbcpb->cpb.aes_gcm.out_s0, AES_BLOCK_SIZE);

		NX_CPB_FDM(csbcpb) |= NX_FDM_CONTINUATION;

		atomic_inc(&(nx_ctx->stats->aes_ops));
		atomic64_add(assoclen, &(nx_ctx->stats->aes_bytes));

		processed += to_process;
	} while (processed < nbytes);

out:
	/* Restore GCM mode */
	csbcpb->cpb.hdr.mode = NX_MODE_AES_GCM;
	return rc;
}

static int gcm_empty(struct aead_request *req, struct blkcipher_desc *desc,
		     int enc)
{
	int rc;
	struct nx_crypto_ctx *nx_ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	char out[AES_BLOCK_SIZE];
	struct nx_sg *in_sg, *out_sg;
	int len;

	/* For scenarios where the input message is zero length, AES CTR mode
	 * may be used. Set the source data to be a single block (16B) of all
	 * zeros, and set the input IV value to be the same as the GMAC IV
	 * value. - nx_wb 4.8.1.3 */

	/* Change to ECB mode */
	csbcpb->cpb.hdr.mode = NX_MODE_AES_ECB;
	memcpy(csbcpb->cpb.aes_ecb.key, csbcpb->cpb.aes_gcm.key,
			sizeof(csbcpb->cpb.aes_ecb.key));
	if (enc)
		NX_CPB_FDM(csbcpb) |= NX_FDM_ENDE_ENCRYPT;
	else
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_ENDE_ENCRYPT;

	len = AES_BLOCK_SIZE;

	/* Encrypt the counter/IV */
	in_sg = nx_build_sg_list(nx_ctx->in_sg, (u8 *) desc->info,
				 &len, nx_ctx->ap->sglen);

	if (len != AES_BLOCK_SIZE)
		return -EINVAL;

	len = sizeof(out);
	out_sg = nx_build_sg_list(nx_ctx->out_sg, (u8 *) out, &len,
				  nx_ctx->ap->sglen);

	if (len != sizeof(out))
		return -EINVAL;

	nx_ctx->op.inlen = (nx_ctx->in_sg - in_sg) * sizeof(struct nx_sg);
	nx_ctx->op.outlen = (nx_ctx->out_sg - out_sg) * sizeof(struct nx_sg);

	rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
			   desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP);
	if (rc)
		goto out;
	atomic_inc(&(nx_ctx->stats->aes_ops));

	/* Copy out the auth tag */
	memcpy(csbcpb->cpb.aes_gcm.out_pat_or_mac, out,
			crypto_aead_authsize(crypto_aead_reqtfm(req)));
out:
	/* Restore XCBC mode */
	csbcpb->cpb.hdr.mode = NX_MODE_AES_GCM;

	/*
	 * ECB key uses the same region that GCM AAD and counter, so it's safe
	 * to just fill it with zeroes.
	 */
	memset(csbcpb->cpb.aes_ecb.key, 0, sizeof(csbcpb->cpb.aes_ecb.key));

	return rc;
}

static int gcm_aes_nx_crypt(struct aead_request *req, int enc,
			    unsigned int assoclen)
{
	struct nx_crypto_ctx *nx_ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct nx_gcm_rctx *rctx = aead_request_ctx(req);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct blkcipher_desc desc;
	unsigned int nbytes = req->cryptlen;
	unsigned int processed = 0, to_process;
	unsigned long irq_flags;
	int rc = -EINVAL;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	desc.info = rctx->iv;
	/* initialize the counter */
	*(u32 *)(desc.info + NX_GCM_CTR_OFFSET) = 1;

	if (nbytes == 0) {
		if (assoclen == 0)
			rc = gcm_empty(req, &desc, enc);
		else
			rc = gmac(req, &desc, assoclen);
		if (rc)
			goto out;
		else
			goto mac;
	}

	/* Process associated data */
	csbcpb->cpb.aes_gcm.bit_length_aad = assoclen * 8;
	if (assoclen) {
		rc = nx_gca(nx_ctx, req, csbcpb->cpb.aes_gcm.in_pat_or_aad,
			    assoclen);
		if (rc)
			goto out;
	}

	/* Set flags for encryption */
	NX_CPB_FDM(csbcpb) &= ~NX_FDM_CONTINUATION;
	if (enc) {
		NX_CPB_FDM(csbcpb) |= NX_FDM_ENDE_ENCRYPT;
	} else {
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_ENDE_ENCRYPT;
		nbytes -= crypto_aead_authsize(crypto_aead_reqtfm(req));
	}

	do {
		to_process = nbytes - processed;

		csbcpb->cpb.aes_gcm.bit_length_data = nbytes * 8;
		rc = nx_build_sg_lists(nx_ctx, &desc, req->dst,
				       req->src, &to_process,
				       processed + req->assoclen,
				       csbcpb->cpb.aes_gcm.iv_or_cnt);

		if (rc)
			goto out;

		if ((to_process + processed) < nbytes)
			NX_CPB_FDM(csbcpb) |= NX_FDM_INTERMEDIATE;
		else
			NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;


		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
				   req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			goto out;

		memcpy(desc.info, csbcpb->cpb.aes_gcm.out_cnt, AES_BLOCK_SIZE);
		memcpy(csbcpb->cpb.aes_gcm.in_pat_or_aad,
			csbcpb->cpb.aes_gcm.out_pat_or_mac, AES_BLOCK_SIZE);
		memcpy(csbcpb->cpb.aes_gcm.in_s0,
			csbcpb->cpb.aes_gcm.out_s0, AES_BLOCK_SIZE);

		NX_CPB_FDM(csbcpb) |= NX_FDM_CONTINUATION;

		atomic_inc(&(nx_ctx->stats->aes_ops));
		atomic64_add(csbcpb->csb.processed_byte_count,
			     &(nx_ctx->stats->aes_bytes));

		processed += to_process;
	} while (processed < nbytes);

mac:
	if (enc) {
		/* copy out the auth tag */
		scatterwalk_map_and_copy(
			csbcpb->cpb.aes_gcm.out_pat_or_mac,
			req->dst, req->assoclen + nbytes,
			crypto_aead_authsize(crypto_aead_reqtfm(req)),
			SCATTERWALK_TO_SG);
	} else {
		u8 *itag = nx_ctx->priv.gcm.iauth_tag;
		u8 *otag = csbcpb->cpb.aes_gcm.out_pat_or_mac;

		scatterwalk_map_and_copy(
			itag, req->src, req->assoclen + nbytes,
			crypto_aead_authsize(crypto_aead_reqtfm(req)),
			SCATTERWALK_FROM_SG);
		rc = crypto_memneq(itag, otag,
			    crypto_aead_authsize(crypto_aead_reqtfm(req))) ?
		     -EBADMSG : 0;
	}
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int gcm_aes_nx_encrypt(struct aead_request *req)
{
	struct nx_gcm_rctx *rctx = aead_request_ctx(req);
	char *iv = rctx->iv;

	memcpy(iv, req->iv, GCM_AES_IV_SIZE);

	return gcm_aes_nx_crypt(req, 1, req->assoclen);
}

static int gcm_aes_nx_decrypt(struct aead_request *req)
{
	struct nx_gcm_rctx *rctx = aead_request_ctx(req);
	char *iv = rctx->iv;

	memcpy(iv, req->iv, GCM_AES_IV_SIZE);

	return gcm_aes_nx_crypt(req, 0, req->assoclen);
}

static int gcm4106_aes_nx_encrypt(struct aead_request *req)
{
	struct nx_crypto_ctx *nx_ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct nx_gcm_rctx *rctx = aead_request_ctx(req);
	char *iv = rctx->iv;
	char *nonce = nx_ctx->priv.gcm.nonce;

	memcpy(iv, nonce, NX_GCM4106_NONCE_LEN);
	memcpy(iv + NX_GCM4106_NONCE_LEN, req->iv, 8);

	if (req->assoclen < 8)
		return -EINVAL;

	return gcm_aes_nx_crypt(req, 1, req->assoclen - 8);
}

static int gcm4106_aes_nx_decrypt(struct aead_request *req)
{
	struct nx_crypto_ctx *nx_ctx =
		crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct nx_gcm_rctx *rctx = aead_request_ctx(req);
	char *iv = rctx->iv;
	char *nonce = nx_ctx->priv.gcm.nonce;

	memcpy(iv, nonce, NX_GCM4106_NONCE_LEN);
	memcpy(iv + NX_GCM4106_NONCE_LEN, req->iv, 8);

	if (req->assoclen < 8)
		return -EINVAL;

	return gcm_aes_nx_crypt(req, 0, req->assoclen - 8);
}

/* tell the block cipher walk routines that this is a stream cipher by
 * setting cra_blocksize to 1. Even using blkcipher_walk_virt_block
 * during encrypt/decrypt doesn't solve this problem, because it calls
 * blkcipher_walk_done under the covers, which doesn't use walk->blocksize,
 * but instead uses this tfm->blocksize. */
struct aead_alg nx_gcm_aes_alg = {
	.base = {
		.cra_name        = "gcm(aes)",
		.cra_driver_name = "gcm-aes-nx",
		.cra_priority    = 300,
		.cra_blocksize   = 1,
		.cra_ctxsize     = sizeof(struct nx_crypto_ctx),
		.cra_module      = THIS_MODULE,
	},
	.init        = nx_crypto_ctx_aes_gcm_init,
	.exit        = nx_crypto_ctx_aead_exit,
	.ivsize      = GCM_AES_IV_SIZE,
	.maxauthsize = AES_BLOCK_SIZE,
	.setkey      = gcm_aes_nx_set_key,
	.encrypt     = gcm_aes_nx_encrypt,
	.decrypt     = gcm_aes_nx_decrypt,
};

struct aead_alg nx_gcm4106_aes_alg = {
	.base = {
		.cra_name        = "rfc4106(gcm(aes))",
		.cra_driver_name = "rfc4106-gcm-aes-nx",
		.cra_priority    = 300,
		.cra_blocksize   = 1,
		.cra_ctxsize     = sizeof(struct nx_crypto_ctx),
		.cra_module      = THIS_MODULE,
	},
	.init        = nx_crypto_ctx_aes_gcm_init,
	.exit        = nx_crypto_ctx_aead_exit,
	.ivsize      = GCM_RFC4106_IV_SIZE,
	.maxauthsize = AES_BLOCK_SIZE,
	.setkey      = gcm4106_aes_nx_set_key,
	.setauthsize = gcm4106_aes_nx_setauthsize,
	.encrypt     = gcm4106_aes_nx_encrypt,
	.decrypt     = gcm4106_aes_nx_decrypt,
};
