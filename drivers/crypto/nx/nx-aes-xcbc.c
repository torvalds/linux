/**
 * AES XCBC routines supporting the Power 7+ Nest Accelerators driver
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
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <asm/vio.h>

#include "nx_csbcpb.h"
#include "nx.h"


struct xcbc_state {
	u8 state[AES_BLOCK_SIZE];
	unsigned int count;
	u8 buffer[AES_BLOCK_SIZE];
};

static int nx_xcbc_set_key(struct crypto_shash *desc,
			   const u8            *in_key,
			   unsigned int         key_len)
{
	struct nx_crypto_ctx *nx_ctx = crypto_shash_ctx(desc);

	switch (key_len) {
	case AES_KEYSIZE_128:
		nx_ctx->ap = &nx_ctx->props[NX_PROPS_AES_128];
		break;
	default:
		return -EINVAL;
	}

	memcpy(nx_ctx->priv.xcbc.key, in_key, key_len);

	return 0;
}

static int nx_xcbc_init(struct shash_desc *desc)
{
	struct xcbc_state *sctx = shash_desc_ctx(desc);
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(&desc->tfm->base);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct nx_sg *out_sg;

	nx_ctx_init(nx_ctx, HCOP_FC_AES);

	memset(sctx, 0, sizeof *sctx);

	NX_CPB_SET_KEY_SIZE(csbcpb, NX_KS_AES_128);
	csbcpb->cpb.hdr.mode = NX_MODE_AES_XCBC_MAC;

	memcpy(csbcpb->cpb.aes_xcbc.key, nx_ctx->priv.xcbc.key, AES_BLOCK_SIZE);
	memset(nx_ctx->priv.xcbc.key, 0, sizeof *nx_ctx->priv.xcbc.key);

	out_sg = nx_build_sg_list(nx_ctx->out_sg, (u8 *)sctx->state,
				  AES_BLOCK_SIZE, nx_ctx->ap->sglen);
	nx_ctx->op.outlen = (nx_ctx->out_sg - out_sg) * sizeof(struct nx_sg);

	return 0;
}

static int nx_xcbc_update(struct shash_desc *desc,
			  const u8          *data,
			  unsigned int       len)
{
	struct xcbc_state *sctx = shash_desc_ctx(desc);
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(&desc->tfm->base);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct nx_sg *in_sg;
	u32 to_process, leftover, total;
	u32 max_sg_len;
	unsigned long irq_flags;
	int rc = 0;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);


	total = sctx->count + len;

	/* 2 cases for total data len:
	 *  1: <= AES_BLOCK_SIZE: copy into state, return 0
	 *  2: > AES_BLOCK_SIZE: process X blocks, copy in leftover
	 */
	if (total <= AES_BLOCK_SIZE) {
		memcpy(sctx->buffer + sctx->count, data, len);
		sctx->count += len;
		goto out;
	}

	in_sg = nx_ctx->in_sg;
	max_sg_len = min_t(u32, nx_driver.of.max_sg_len/sizeof(struct nx_sg),
				nx_ctx->ap->sglen);

	do {

		/* to_process: the AES_BLOCK_SIZE data chunk to process in this
		 * update */
		to_process = min_t(u64, total, nx_ctx->ap->databytelen);
		to_process = min_t(u64, to_process,
					NX_PAGE_SIZE * (max_sg_len - 1));
		to_process = to_process & ~(AES_BLOCK_SIZE - 1);
		leftover = total - to_process;

		/* the hardware will not accept a 0 byte operation for this
		 * algorithm and the operation MUST be finalized to be correct.
		 * So if we happen to get an update that falls on a block sized
		 * boundary, we must save off the last block to finalize with
		 * later. */
		if (!leftover) {
			to_process -= AES_BLOCK_SIZE;
			leftover = AES_BLOCK_SIZE;
		}

		if (sctx->count) {
			in_sg = nx_build_sg_list(nx_ctx->in_sg,
						(u8 *) sctx->buffer,
						sctx->count,
						max_sg_len);
		}
		in_sg = nx_build_sg_list(in_sg,
					(u8 *) data,
					to_process - sctx->count,
					max_sg_len);
		nx_ctx->op.inlen = (nx_ctx->in_sg - in_sg) *
					sizeof(struct nx_sg);

		/* we've hit the nx chip previously and we're updating again,
		 * so copy over the partial digest */
		if (NX_CPB_FDM(csbcpb) & NX_FDM_CONTINUATION) {
			memcpy(csbcpb->cpb.aes_xcbc.cv,
				csbcpb->cpb.aes_xcbc.out_cv_mac,
				AES_BLOCK_SIZE);
		}

		NX_CPB_FDM(csbcpb) |= NX_FDM_INTERMEDIATE;
		if (!nx_ctx->op.inlen || !nx_ctx->op.outlen) {
			rc = -EINVAL;
			goto out;
		}

		rc = nx_hcall_sync(nx_ctx, &nx_ctx->op,
			   desc->flags & CRYPTO_TFM_REQ_MAY_SLEEP);
		if (rc)
			goto out;

		atomic_inc(&(nx_ctx->stats->aes_ops));

		/* everything after the first update is continuation */
		NX_CPB_FDM(csbcpb) |= NX_FDM_CONTINUATION;

		total -= to_process;
		data += to_process - sctx->count;
		sctx->count = 0;
		in_sg = nx_ctx->in_sg;
	} while (leftover > AES_BLOCK_SIZE);

	/* copy the leftover back into the state struct */
	memcpy(sctx->buffer, data, leftover);
	sctx->count = leftover;

out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

static int nx_xcbc_final(struct shash_desc *desc, u8 *out)
{
	struct xcbc_state *sctx = shash_desc_ctx(desc);
	struct nx_crypto_ctx *nx_ctx = crypto_tfm_ctx(&desc->tfm->base);
	struct nx_csbcpb *csbcpb = nx_ctx->csbcpb;
	struct nx_sg *in_sg, *out_sg;
	unsigned long irq_flags;
	int rc = 0;

	spin_lock_irqsave(&nx_ctx->lock, irq_flags);

	if (NX_CPB_FDM(csbcpb) & NX_FDM_CONTINUATION) {
		/* we've hit the nx chip previously, now we're finalizing,
		 * so copy over the partial digest */
		memcpy(csbcpb->cpb.aes_xcbc.cv,
		       csbcpb->cpb.aes_xcbc.out_cv_mac, AES_BLOCK_SIZE);
	} else if (sctx->count == 0) {
		/* we've never seen an update, so this is a 0 byte op. The
		 * hardware cannot handle a 0 byte op, so just copy out the
		 * known 0 byte result. This is cheaper than allocating a
		 * software context to do a 0 byte op */
		u8 data[] = { 0x75, 0xf0, 0x25, 0x1d, 0x52, 0x8a, 0xc0, 0x1c,
			      0x45, 0x73, 0xdf, 0xd5, 0x84, 0xd7, 0x9f, 0x29 };
		memcpy(out, data, sizeof(data));
		goto out;
	}

	/* final is represented by continuing the operation and indicating that
	 * this is not an intermediate operation */
	NX_CPB_FDM(csbcpb) &= ~NX_FDM_INTERMEDIATE;

	in_sg = nx_build_sg_list(nx_ctx->in_sg, (u8 *)sctx->buffer,
				 sctx->count, nx_ctx->ap->sglen);
	out_sg = nx_build_sg_list(nx_ctx->out_sg, out, AES_BLOCK_SIZE,
				  nx_ctx->ap->sglen);

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

	atomic_inc(&(nx_ctx->stats->aes_ops));

	memcpy(out, csbcpb->cpb.aes_xcbc.out_cv_mac, AES_BLOCK_SIZE);
out:
	spin_unlock_irqrestore(&nx_ctx->lock, irq_flags);
	return rc;
}

struct shash_alg nx_shash_aes_xcbc_alg = {
	.digestsize = AES_BLOCK_SIZE,
	.init       = nx_xcbc_init,
	.update     = nx_xcbc_update,
	.final      = nx_xcbc_final,
	.setkey     = nx_xcbc_set_key,
	.descsize   = sizeof(struct xcbc_state),
	.statesize  = sizeof(struct xcbc_state),
	.base       = {
		.cra_name        = "xcbc(aes)",
		.cra_driver_name = "xcbc-aes-nx",
		.cra_priority    = 300,
		.cra_flags       = CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize   = AES_BLOCK_SIZE,
		.cra_module      = THIS_MODULE,
		.cra_ctxsize     = sizeof(struct nx_crypto_ctx),
		.cra_init        = nx_crypto_ctx_aes_xcbc_init,
		.cra_exit        = nx_crypto_ctx_exit,
	}
};
