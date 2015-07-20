/**
 * SHA-512 routines supporting the Power 7+ Nest Accelerators driver
 *
 * Copyright (C) 2011-2012 International Business Machines Inc.
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

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <linux/module.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


static int nx_crypto_ctx_sha512_init(struct crypto_tfm *tfm)
{
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(tfm);
	int err;

	err = nx_crypto_ctx_sha_init(tfm);
	if (err)
		return err;

	nx_ctx_init(nx_ctx, HCOP_FC_SHA);

	nx_ctx->ap = &nx_ctx->props[NX_PROPS_SHA512];

	NX_CPB_SET_DIGEST_SIZE(nx_ctx->csbcpb, NX_DS_SHA512);

	return 0;
}

static int nx_sha512_init(struct shash_desc *desc)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	memset(sctx, 0, sizeof *sctx);

	sctx->state[0] = __cpu_to_be64(SHA512_H0);
	sctx->state[1] = __cpu_to_be64(SHA512_H1);
	sctx->state[2] = __cpu_to_be64(SHA512_H2);
	sctx->state[3] = __cpu_to_be64(SHA512_H3);
	sctx->state[4] = __cpu_to_be64(SHA512_H4);
	sctx->state[5] = __cpu_to_be64(SHA512_H5);
	sctx->state[6] = __cpu_to_be64(SHA512_H6);
	sctx->state[7] = __cpu_to_be64(SHA512_H7);
	sctx->count[0] = 0;

	return 0;
}

static int nx_sha512_update(struct shash_desc *desc, const u8 *data,
			    unsigned int len)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(&desc->tfm->base);
	struct nx_csbcpb *csbcpb = (struct nx_csbcpb *)nx_ctx->csbcpb;
	struct nx_sg *in_sg;
	struct nx_sg *out_sg;
	u64 to_process, leftover = 0, total;
	unsigned long irq_flags;
	int rc = 0;
	int data_len;
	u32 max_sg_len;
	u64 buf_len = (sctx->count[0] % SHA512_BLOCK_SIZE);

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	/* 2 cases for total data len:
	 *  1: < SHA512_BLOCK_SIZE: copy into state, return 0
	 *  2: >= SHA512_BLOCK_SIZE: process X blocks, copy in leftover
	 */
	total = (sctx->count[0] % SHA512_BLOCK_SIZE) + len;
	if (total < SHA512_BLOCK_SIZE) {
		memcpy(sctx->buf + buf_len, data, len);
		sctx->count[0] += len;
		goto out;
	}

	memcpy(csbcpb->cpb.sha512.message_digest, sctx->state, SHA512_DIGEST_SIZE);
	NX_CPB_FDM(csbcpb) |= NX_FDM_INTERMEDIATE;
	NX_CPB_FDM(csbcpb) |= NX_FDM_CONTINUATION;

	in_sg = nx_ctx->in_sg;
	max_sg_len = min_t(u64, nx_ctx->ap->sglen,
			nx_driver.of.max_sg_len/sizeof(struct nx_sg));
	max_sg_len = min_t(u64, max_sg_len,
			nx_ctx->ap->databytelen/NX_PAGE_SIZE);

	data_len = SHA512_DIGEST_SIZE;
	out_sg = nx_build_sg_list(nx_ctx->out_sg, (u8 *)sctx->state,
				  &data_len, max_sg_len);
	nx_ctx->op.outlen = (nx_ctx->out_sg - out_sg) * sizeof(struct nx_sg);

	if (data_len != SHA512_DIGEST_SIZE) {
		rc = -EINVAL;
		goto out;
	}

	do {
		/*
		 * to_process: the SHA512_BLOCK_SIZE data chunk to process in
		 * this update. This value is also restricted by the sg list
		 * limits.
		 */
		to_process = total - leftover;
		to_process = to_process & ~(SHA512_BLOCK_SIZE - 1);
		leftover = total - to_process;

		if (buf_len) {
			data_len = buf_len;
			in_sg = nx_build_sg_list(nx_ctx->in_sg,
						 (u8 *) sctx->buf,
						 &data_len, max_sg_len);

			if (data_len != buf_len) {
				rc = -EINVAL;
				goto out;
			}
		}

		data_len = to_process - buf_len;
		in_sg = nx_build_sg_list(in_sg, (u8 *) data,
					 &data_len, max_sg_len);

		nx_ctx->op.inlen = (nx_ctx->in_sg - in_sg) * sizeof(struct nx_sg);

		if (data_len != (to_process - buf_len)) {
			rc = -EINVAL;
			goto out;
		}

		to_process = (data_len + buf_len);
		leftover = total - to_process;

		/*
		 * we've hit the nx chip previously and we're updating
		 * again, so copy over the partial digest.
		 */
		memcpy(csbcpb->cpb.sha512.input_partial_digest,
			       csbcpb->cpb.sha512.message_digest,
			       SHA512_DIGEST_SIZE);

		if (!nx_ctx->op.inlen || !nx_ctx->op.outlen) {
			rc = -EINVAL;
			goto out;
		}

		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
				   desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			goto out;

		atomic_inc(&(nx_ctx->stats->sha512_ops));

		total -= to_process;
		data += to_process - buf_len;
		buf_len = 0;

	} while (leftover >= SHA512_BLOCK_SIZE);

	/* copy the leftover back into the state struct */
	if (leftover)
		memcpy(sctx->buf, data, leftover);
	sctx->count[0] += len;
	memcpy(sctx->state, csbcpb->cpb.sha512.message_digest, SHA512_DIGEST_SIZE);
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int nx_sha512_final(struct shash_desc *desc, u8 *out)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(&desc->tfm->base);
	struct nx_csbcpb *csbcpb = (struct nx_csbcpb *)nx_ctx->csbcpb;
	struct nx_sg *in_sg, *out_sg;
	u32 max_sg_len;
	u64 count0;
	unsigned long irq_flags;
	int rc = 0;
	int len;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	max_sg_len = min_t(u64, nx_ctx->ap->sglen,
			nx_driver.of.max_sg_len/sizeof(struct nx_sg));
	max_sg_len = min_t(u64, max_sg_len,
			nx_ctx->ap->databytelen/NX_PAGE_SIZE);

	/* final is represented by continuing the operation and indicating that
	 * this is not an intermediate operation */
	if (sctx->count[0] >= SHA512_BLOCK_SIZE) {
		/* we've hit the nx chip previously, now we're finalizing,
		 * so copy over the partial digest */
		memcpy(csbcpb->cpb.sha512.input_partial_digest, sctx->state,
							SHA512_DIGEST_SIZE);
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;
		NX_CPB_FDM(csbcpb) |= NX_FDM_CONTINUATION;
	} else {
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;
		NX_CPB_FDM(csbcpb) &= ~NX_FDM_CONTINUATION;
	}

	NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;

	count0 = sctx->count[0] * 8;

	csbcpb->cpb.sha512.message_bit_length_lo = count0;

	len = sctx->count[0] & (SHA512_BLOCK_SIZE - 1);
	in_sg = nx_build_sg_list(nx_ctx->in_sg, sctx->buf, &len,
				 max_sg_len);

	if (len != (sctx->count[0] & (SHA512_BLOCK_SIZE - 1))) {
		rc = -EINVAL;
		goto out;
	}

	len = SHA512_DIGEST_SIZE;
	out_sg = nx_build_sg_list(nx_ctx->out_sg, out, &len,
				 max_sg_len);

	nx_ctx->op.inlen = (nx_ctx->in_sg - in_sg) * sizeof(struct nx_sg);
	nx_ctx->op.outlen = (nx_ctx->out_sg - out_sg) * sizeof(struct nx_sg);

	if (!nx_ctx->op.outlen) {
		rc = -EINVAL;
		goto out;
	}

	rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
			   desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP);
	if (rc)
		goto out;

	atomic_inc(&(nx_ctx->stats->sha512_ops));
	atomic64_add(sctx->count[0], &(nx_ctx->stats->sha512_bytes));

	memcpy(out, csbcpb->cpb.sha512.message_digest, SHA512_DIGEST_SIZE);
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int nx_sha512_export(struct shash_desc *desc, void *out)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int nx_sha512_import(struct shash_desc *desc, const void *in)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

struct shash_alg nx_shash_sha512_alg = {
	.digestsize = SHA512_DIGEST_SIZE,
	.init       = nx_sha512_init,
	.update     = nx_sha512_update,
	.final      = nx_sha512_final,
	.export     = nx_sha512_export,
	.import     = nx_sha512_import,
	.descsize   = sizeof(struct sha512_state),
	.statesize  = sizeof(struct sha512_state),
	.base       = {
		.cra_name        = "sha512",
		.cra_driver_name = "sha512-nx",
		.cra_priority    = 300,
		.cra_flags       = CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize   = SHA512_BLOCK_SIZE,
		.cra_module      = THIS_MODULE,
		.cra_ctxsize     = sizeof(struct nx_crypto_ctx),
		.cra_init        = nx_crypto_ctx_sha512_init,
		.cra_exit        = nx_crypto_ctx_exit,
	}
};
