/*
 * Multi buffer SHA1 algorithm Glue Code
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2014 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Contact Information:
 *	Tim Chen <tim.c.chen@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2014 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <linux/list.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha.h>
#include <crypto/mcryptd.h>
#include <crypto/crypto_wq.h>
#include <asm/byteorder.h>
#include <asm/i387.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <linux/hardirq.h>
#include <asm/fpu-internal.h>
#include "sha_mb_ctx.h"

#define FLUSH_INTERVAL 1000 /* in usec */

static struct mcryptd_alg_state sha1_mb_alg_state;

struct sha1_mb_ctx {
	struct mcryptd_ahash *mcryptd_tfm;
};

static inline struct mcryptd_hash_request_ctx *cast_hash_to_mcryptd_ctx(struct sha1_hash_ctx *hash_ctx)
{
	struct shash_desc *desc;

	desc = container_of((void *) hash_ctx, struct shash_desc, __ctx);
	return container_of(desc, struct mcryptd_hash_request_ctx, desc);
}

static inline struct ahash_request *cast_mcryptd_ctx_to_req(struct mcryptd_hash_request_ctx *ctx)
{
	return container_of((void *) ctx, struct ahash_request, __ctx);
}

static void req_ctx_init(struct mcryptd_hash_request_ctx *rctx,
				struct shash_desc *desc)
{
	rctx->flag = HASH_UPDATE;
}

static asmlinkage void (*sha1_job_mgr_init)(struct sha1_mb_mgr *state);
static asmlinkage struct job_sha1* (*sha1_job_mgr_submit)(struct sha1_mb_mgr *state,
							  struct job_sha1 *job);
static asmlinkage struct job_sha1* (*sha1_job_mgr_flush)(struct sha1_mb_mgr *state);
static asmlinkage struct job_sha1* (*sha1_job_mgr_get_comp_job)(struct sha1_mb_mgr *state);

inline void sha1_init_digest(uint32_t *digest)
{
	static const uint32_t initial_digest[SHA1_DIGEST_LENGTH] = {SHA1_H0,
					SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 };
	memcpy(digest, initial_digest, sizeof(initial_digest));
}

inline uint32_t sha1_pad(uint8_t padblock[SHA1_BLOCK_SIZE * 2],
			 uint32_t total_len)
{
	uint32_t i = total_len & (SHA1_BLOCK_SIZE - 1);

	memset(&padblock[i], 0, SHA1_BLOCK_SIZE);
	padblock[i] = 0x80;

	i += ((SHA1_BLOCK_SIZE - 1) &
	      (0 - (total_len + SHA1_PADLENGTHFIELD_SIZE + 1)))
	     + 1 + SHA1_PADLENGTHFIELD_SIZE;

#if SHA1_PADLENGTHFIELD_SIZE == 16
	*((uint64_t *) &padblock[i - 16]) = 0;
#endif

	*((uint64_t *) &padblock[i - 8]) = cpu_to_be64(total_len << 3);

	/* Number of extra blocks to hash */
	return i >> SHA1_LOG2_BLOCK_SIZE;
}

static struct sha1_hash_ctx *sha1_ctx_mgr_resubmit(struct sha1_ctx_mgr *mgr, struct sha1_hash_ctx *ctx)
{
	while (ctx) {
		if (ctx->status & HASH_CTX_STS_COMPLETE) {
			/* Clear PROCESSING bit */
			ctx->status = HASH_CTX_STS_COMPLETE;
			return ctx;
		}

		/*
		 * If the extra blocks are empty, begin hashing what remains
		 * in the user's buffer.
		 */
		if (ctx->partial_block_buffer_length == 0 &&
		    ctx->incoming_buffer_length) {

			const void *buffer = ctx->incoming_buffer;
			uint32_t len = ctx->incoming_buffer_length;
			uint32_t copy_len;

			/*
			 * Only entire blocks can be hashed.
			 * Copy remainder to extra blocks buffer.
			 */
			copy_len = len & (SHA1_BLOCK_SIZE-1);

			if (copy_len) {
				len -= copy_len;
				memcpy(ctx->partial_block_buffer,
				       ((const char *) buffer + len),
				       copy_len);
				ctx->partial_block_buffer_length = copy_len;
			}

			ctx->incoming_buffer_length = 0;

			/* len should be a multiple of the block size now */
			assert((len % SHA1_BLOCK_SIZE) == 0);

			/* Set len to the number of blocks to be hashed */
			len >>= SHA1_LOG2_BLOCK_SIZE;

			if (len) {

				ctx->job.buffer = (uint8_t *) buffer;
				ctx->job.len = len;
				ctx = (struct sha1_hash_ctx *) sha1_job_mgr_submit(&mgr->mgr,
										  &ctx->job);
				continue;
			}
		}

		/*
		 * If the extra blocks are not empty, then we are
		 * either on the last block(s) or we need more
		 * user input before continuing.
		 */
		if (ctx->status & HASH_CTX_STS_LAST) {

			uint8_t *buf = ctx->partial_block_buffer;
			uint32_t n_extra_blocks = sha1_pad(buf, ctx->total_length);

			ctx->status = (HASH_CTX_STS_PROCESSING |
				       HASH_CTX_STS_COMPLETE);
			ctx->job.buffer = buf;
			ctx->job.len = (uint32_t) n_extra_blocks;
			ctx = (struct sha1_hash_ctx *) sha1_job_mgr_submit(&mgr->mgr, &ctx->job);
			continue;
		}

		ctx->status = HASH_CTX_STS_IDLE;
		return ctx;
	}

	return NULL;
}

static struct sha1_hash_ctx *sha1_ctx_mgr_get_comp_ctx(struct sha1_ctx_mgr *mgr)
{
	/*
	 * If get_comp_job returns NULL, there are no jobs complete.
	 * If get_comp_job returns a job, verify that it is safe to return to the user.
	 * If it is not ready, resubmit the job to finish processing.
	 * If sha1_ctx_mgr_resubmit returned a job, it is ready to be returned.
	 * Otherwise, all jobs currently being managed by the hash_ctx_mgr still need processing.
	 */
	struct sha1_hash_ctx *ctx;

	ctx = (struct sha1_hash_ctx *) sha1_job_mgr_get_comp_job(&mgr->mgr);
	return sha1_ctx_mgr_resubmit(mgr, ctx);
}

static void sha1_ctx_mgr_init(struct sha1_ctx_mgr *mgr)
{
	sha1_job_mgr_init(&mgr->mgr);
}

static struct sha1_hash_ctx *sha1_ctx_mgr_submit(struct sha1_ctx_mgr *mgr,
					  struct sha1_hash_ctx *ctx,
					  const void *buffer,
					  uint32_t len,
					  int flags)
{
	if (flags & (~HASH_ENTIRE)) {
		/* User should not pass anything other than FIRST, UPDATE, or LAST */
		ctx->error = HASH_CTX_ERROR_INVALID_FLAGS;
		return ctx;
	}

	if (ctx->status & HASH_CTX_STS_PROCESSING) {
		/* Cannot submit to a currently processing job. */
		ctx->error = HASH_CTX_ERROR_ALREADY_PROCESSING;
		return ctx;
	}

	if ((ctx->status & HASH_CTX_STS_COMPLETE) && !(flags & HASH_FIRST)) {
		/* Cannot update a finished job. */
		ctx->error = HASH_CTX_ERROR_ALREADY_COMPLETED;
		return ctx;
	}


	if (flags & HASH_FIRST) {
		/* Init digest */
		sha1_init_digest(ctx->job.result_digest);

		/* Reset byte counter */
		ctx->total_length = 0;

		/* Clear extra blocks */
		ctx->partial_block_buffer_length = 0;
	}

	/* If we made it here, there were no errors during this call to submit */
	ctx->error = HASH_CTX_ERROR_NONE;

	/* Store buffer ptr info from user */
	ctx->incoming_buffer = buffer;
	ctx->incoming_buffer_length = len;

	/* Store the user's request flags and mark this ctx as currently being processed. */
	ctx->status = (flags & HASH_LAST) ?
			(HASH_CTX_STS_PROCESSING | HASH_CTX_STS_LAST) :
			HASH_CTX_STS_PROCESSING;

	/* Advance byte counter */
	ctx->total_length += len;

	/*
	 * If there is anything currently buffered in the extra blocks,
	 * append to it until it contains a whole block.
	 * Or if the user's buffer contains less than a whole block,
	 * append as much as possible to the extra block.
	 */
	if ((ctx->partial_block_buffer_length) | (len < SHA1_BLOCK_SIZE)) {
		/* Compute how many bytes to copy from user buffer into extra block */
		uint32_t copy_len = SHA1_BLOCK_SIZE - ctx->partial_block_buffer_length;
		if (len < copy_len)
			copy_len = len;

		if (copy_len) {
			/* Copy and update relevant pointers and counters */
			memcpy(&ctx->partial_block_buffer[ctx->partial_block_buffer_length],
				buffer, copy_len);

			ctx->partial_block_buffer_length += copy_len;
			ctx->incoming_buffer = (const void *)((const char *)buffer + copy_len);
			ctx->incoming_buffer_length = len - copy_len;
		}

		/* The extra block should never contain more than 1 block here */
		assert(ctx->partial_block_buffer_length <= SHA1_BLOCK_SIZE);

		/* If the extra block buffer contains exactly 1 block, it can be hashed. */
		if (ctx->partial_block_buffer_length >= SHA1_BLOCK_SIZE) {
			ctx->partial_block_buffer_length = 0;

			ctx->job.buffer = ctx->partial_block_buffer;
			ctx->job.len = 1;
			ctx = (struct sha1_hash_ctx *) sha1_job_mgr_submit(&mgr->mgr, &ctx->job);
		}
	}

	return sha1_ctx_mgr_resubmit(mgr, ctx);
}

static struct sha1_hash_ctx *sha1_ctx_mgr_flush(struct sha1_ctx_mgr *mgr)
{
	struct sha1_hash_ctx *ctx;

	while (1) {
		ctx = (struct sha1_hash_ctx *) sha1_job_mgr_flush(&mgr->mgr);

		/* If flush returned 0, there are no more jobs in flight. */
		if (!ctx)
			return NULL;

		/*
		 * If flush returned a job, resubmit the job to finish processing.
		 */
		ctx = sha1_ctx_mgr_resubmit(mgr, ctx);

		/*
		 * If sha1_ctx_mgr_resubmit returned a job, it is ready to be returned.
		 * Otherwise, all jobs currently being managed by the sha1_ctx_mgr
		 * still need processing. Loop.
		 */
		if (ctx)
			return ctx;
	}
}

static int sha1_mb_init(struct shash_desc *desc)
{
	struct sha1_hash_ctx *sctx = shash_desc_ctx(desc);

	hash_ctx_init(sctx);
	sctx->job.result_digest[0] = SHA1_H0;
	sctx->job.result_digest[1] = SHA1_H1;
	sctx->job.result_digest[2] = SHA1_H2;
	sctx->job.result_digest[3] = SHA1_H3;
	sctx->job.result_digest[4] = SHA1_H4;
	sctx->total_length = 0;
	sctx->partial_block_buffer_length = 0;
	sctx->status = HASH_CTX_STS_IDLE;

	return 0;
}

static int sha1_mb_set_results(struct mcryptd_hash_request_ctx *rctx)
{
	int	i;
	struct	sha1_hash_ctx *sctx = shash_desc_ctx(&rctx->desc);
	__be32	*dst = (__be32 *) rctx->out;

	for (i = 0; i < 5; ++i)
		dst[i] = cpu_to_be32(sctx->job.result_digest[i]);

	return 0;
}

static int sha_finish_walk(struct mcryptd_hash_request_ctx **ret_rctx,
			struct mcryptd_alg_cstate *cstate, bool flush)
{
	int	flag = HASH_UPDATE;
	int	nbytes, err = 0;
	struct mcryptd_hash_request_ctx *rctx = *ret_rctx;
	struct sha1_hash_ctx *sha_ctx;

	/* more work ? */
	while (!(rctx->flag & HASH_DONE)) {
		nbytes = crypto_ahash_walk_done(&rctx->walk, 0);
		if (nbytes < 0) {
			err = nbytes;
			goto out;
		}
		/* check if the walk is done */
		if (crypto_ahash_walk_last(&rctx->walk)) {
			rctx->flag |= HASH_DONE;
			if (rctx->flag & HASH_FINAL)
				flag |= HASH_LAST;

		}
		sha_ctx = (struct sha1_hash_ctx *) shash_desc_ctx(&rctx->desc);
		kernel_fpu_begin();
		sha_ctx = sha1_ctx_mgr_submit(cstate->mgr, sha_ctx, rctx->walk.data, nbytes, flag);
		if (!sha_ctx) {
			if (flush)
				sha_ctx = sha1_ctx_mgr_flush(cstate->mgr);
		}
		kernel_fpu_end();
		if (sha_ctx)
			rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
		else {
			rctx = NULL;
			goto out;
		}
	}

	/* copy the results */
	if (rctx->flag & HASH_FINAL)
		sha1_mb_set_results(rctx);

out:
	*ret_rctx = rctx;
	return err;
}

static int sha_complete_job(struct mcryptd_hash_request_ctx *rctx,
			    struct mcryptd_alg_cstate *cstate,
			    int err)
{
	struct ahash_request *req = cast_mcryptd_ctx_to_req(rctx);
	struct sha1_hash_ctx *sha_ctx;
	struct mcryptd_hash_request_ctx *req_ctx;
	int ret;

	/* remove from work list */
	spin_lock(&cstate->work_lock);
	list_del(&rctx->waiter);
	spin_unlock(&cstate->work_lock);

	if (irqs_disabled())
		rctx->complete(&req->base, err);
	else {
		local_bh_disable();
		rctx->complete(&req->base, err);
		local_bh_enable();
	}

	/* check to see if there are other jobs that are done */
	sha_ctx = sha1_ctx_mgr_get_comp_ctx(cstate->mgr);
	while (sha_ctx) {
		req_ctx = cast_hash_to_mcryptd_ctx(sha_ctx);
		ret = sha_finish_walk(&req_ctx, cstate, false);
		if (req_ctx) {
			spin_lock(&cstate->work_lock);
			list_del(&req_ctx->waiter);
			spin_unlock(&cstate->work_lock);

			req = cast_mcryptd_ctx_to_req(req_ctx);
			if (irqs_disabled())
				rctx->complete(&req->base, ret);
			else {
				local_bh_disable();
				rctx->complete(&req->base, ret);
				local_bh_enable();
			}
		}
		sha_ctx = sha1_ctx_mgr_get_comp_ctx(cstate->mgr);
	}

	return 0;
}

static void sha1_mb_add_list(struct mcryptd_hash_request_ctx *rctx,
			     struct mcryptd_alg_cstate *cstate)
{
	unsigned long next_flush;
	unsigned long delay = usecs_to_jiffies(FLUSH_INTERVAL);

	/* initialize tag */
	rctx->tag.arrival = jiffies;    /* tag the arrival time */
	rctx->tag.seq_num = cstate->next_seq_num++;
	next_flush = rctx->tag.arrival + delay;
	rctx->tag.expire = next_flush;

	spin_lock(&cstate->work_lock);
	list_add_tail(&rctx->waiter, &cstate->work_list);
	spin_unlock(&cstate->work_lock);

	mcryptd_arm_flusher(cstate, delay);
}

static int sha1_mb_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct mcryptd_hash_request_ctx *rctx =
			container_of(desc, struct mcryptd_hash_request_ctx, desc);
	struct mcryptd_alg_cstate *cstate =
				this_cpu_ptr(sha1_mb_alg_state.alg_cstate);

	struct ahash_request *req = cast_mcryptd_ctx_to_req(rctx);
	struct sha1_hash_ctx *sha_ctx;
	int ret = 0, nbytes;


	/* sanity check */
	if (rctx->tag.cpu != smp_processor_id()) {
		pr_err("mcryptd error: cpu clash\n");
		goto done;
	}

	/* need to init context */
	req_ctx_init(rctx, desc);

	nbytes = crypto_ahash_walk_first(req, &rctx->walk);

	if (nbytes < 0) {
		ret = nbytes;
		goto done;
	}

	if (crypto_ahash_walk_last(&rctx->walk))
		rctx->flag |= HASH_DONE;

	/* submit */
	sha_ctx = (struct sha1_hash_ctx *) shash_desc_ctx(desc);
	sha1_mb_add_list(rctx, cstate);
	kernel_fpu_begin();
	sha_ctx = sha1_ctx_mgr_submit(cstate->mgr, sha_ctx, rctx->walk.data, nbytes, HASH_UPDATE);
	kernel_fpu_end();

	/* check if anything is returned */
	if (!sha_ctx)
		return -EINPROGRESS;

	if (sha_ctx->error) {
		ret = sha_ctx->error;
		rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
		goto done;
	}

	rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
	ret = sha_finish_walk(&rctx, cstate, false);

	if (!rctx)
		return -EINPROGRESS;
done:
	sha_complete_job(rctx, cstate, ret);
	return ret;
}

static int sha1_mb_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	struct mcryptd_hash_request_ctx *rctx =
			container_of(desc, struct mcryptd_hash_request_ctx, desc);
	struct mcryptd_alg_cstate *cstate =
				this_cpu_ptr(sha1_mb_alg_state.alg_cstate);

	struct ahash_request *req = cast_mcryptd_ctx_to_req(rctx);
	struct sha1_hash_ctx *sha_ctx;
	int ret = 0, flag = HASH_UPDATE, nbytes;

	/* sanity check */
	if (rctx->tag.cpu != smp_processor_id()) {
		pr_err("mcryptd error: cpu clash\n");
		goto done;
	}

	/* need to init context */
	req_ctx_init(rctx, desc);

	nbytes = crypto_ahash_walk_first(req, &rctx->walk);

	if (nbytes < 0) {
		ret = nbytes;
		goto done;
	}

	if (crypto_ahash_walk_last(&rctx->walk)) {
		rctx->flag |= HASH_DONE;
		flag = HASH_LAST;
	}
	rctx->out = out;

	/* submit */
	rctx->flag |= HASH_FINAL;
	sha_ctx = (struct sha1_hash_ctx *) shash_desc_ctx(desc);
	sha1_mb_add_list(rctx, cstate);

	kernel_fpu_begin();
	sha_ctx = sha1_ctx_mgr_submit(cstate->mgr, sha_ctx, rctx->walk.data, nbytes, flag);
	kernel_fpu_end();

	/* check if anything is returned */
	if (!sha_ctx)
		return -EINPROGRESS;

	if (sha_ctx->error) {
		ret = sha_ctx->error;
		goto done;
	}

	rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
	ret = sha_finish_walk(&rctx, cstate, false);
	if (!rctx)
		return -EINPROGRESS;
done:
	sha_complete_job(rctx, cstate, ret);
	return ret;
}

static int sha1_mb_final(struct shash_desc *desc, u8 *out)
{
	struct mcryptd_hash_request_ctx *rctx =
			container_of(desc, struct mcryptd_hash_request_ctx, desc);
	struct mcryptd_alg_cstate *cstate =
				this_cpu_ptr(sha1_mb_alg_state.alg_cstate);

	struct sha1_hash_ctx *sha_ctx;
	int ret = 0;
	u8 data;

	/* sanity check */
	if (rctx->tag.cpu != smp_processor_id()) {
		pr_err("mcryptd error: cpu clash\n");
		goto done;
	}

	/* need to init context */
	req_ctx_init(rctx, desc);

	rctx->out = out;
	rctx->flag |= HASH_DONE | HASH_FINAL;

	sha_ctx = (struct sha1_hash_ctx *) shash_desc_ctx(desc);
	/* flag HASH_FINAL and 0 data size */
	sha1_mb_add_list(rctx, cstate);
	kernel_fpu_begin();
	sha_ctx = sha1_ctx_mgr_submit(cstate->mgr, sha_ctx, &data, 0, HASH_LAST);
	kernel_fpu_end();

	/* check if anything is returned */
	if (!sha_ctx)
		return -EINPROGRESS;

	if (sha_ctx->error) {
		ret = sha_ctx->error;
		rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
		goto done;
	}

	rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
	ret = sha_finish_walk(&rctx, cstate, false);
	if (!rctx)
		return -EINPROGRESS;
done:
	sha_complete_job(rctx, cstate, ret);
	return ret;
}

static int sha1_mb_export(struct shash_desc *desc, void *out)
{
	struct sha1_hash_ctx *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int sha1_mb_import(struct shash_desc *desc, const void *in)
{
	struct sha1_hash_ctx *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}


static struct shash_alg sha1_mb_shash_alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_mb_init,
	.update		=	sha1_mb_update,
	.final		=	sha1_mb_final,
	.finup		=	sha1_mb_finup,
	.export		=	sha1_mb_export,
	.import		=	sha1_mb_import,
	.descsize	=	sizeof(struct sha1_hash_ctx),
	.statesize	=	sizeof(struct sha1_hash_ctx),
	.base		=	{
		.cra_name	 = "__sha1-mb",
		.cra_driver_name = "__intel_sha1-mb",
		.cra_priority	 = 100,
		/*
		 * use ASYNC flag as some buffers in multi-buffer
		 * algo may not have completed before hashing thread sleep
		 */
		.cra_flags	 = CRYPTO_ALG_TYPE_SHASH | CRYPTO_ALG_ASYNC,
		.cra_blocksize	 = SHA1_BLOCK_SIZE,
		.cra_module	 = THIS_MODULE,
		.cra_list	 = LIST_HEAD_INIT(sha1_mb_shash_alg.base.cra_list),
	}
};

static int sha1_mb_async_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sha1_mb_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ahash_request *mcryptd_req = ahash_request_ctx(req);
	struct mcryptd_ahash *mcryptd_tfm = ctx->mcryptd_tfm;

	memcpy(mcryptd_req, req, sizeof(*req));
	ahash_request_set_tfm(mcryptd_req, &mcryptd_tfm->base);
	return crypto_ahash_init(mcryptd_req);
}

static int sha1_mb_async_update(struct ahash_request *req)
{
	struct ahash_request *mcryptd_req = ahash_request_ctx(req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sha1_mb_ctx *ctx = crypto_ahash_ctx(tfm);
	struct mcryptd_ahash *mcryptd_tfm = ctx->mcryptd_tfm;

	memcpy(mcryptd_req, req, sizeof(*req));
	ahash_request_set_tfm(mcryptd_req, &mcryptd_tfm->base);
	return crypto_ahash_update(mcryptd_req);
}

static int sha1_mb_async_finup(struct ahash_request *req)
{
	struct ahash_request *mcryptd_req = ahash_request_ctx(req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sha1_mb_ctx *ctx = crypto_ahash_ctx(tfm);
	struct mcryptd_ahash *mcryptd_tfm = ctx->mcryptd_tfm;

	memcpy(mcryptd_req, req, sizeof(*req));
	ahash_request_set_tfm(mcryptd_req, &mcryptd_tfm->base);
	return crypto_ahash_finup(mcryptd_req);
}

static int sha1_mb_async_final(struct ahash_request *req)
{
	struct ahash_request *mcryptd_req = ahash_request_ctx(req);

	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sha1_mb_ctx *ctx = crypto_ahash_ctx(tfm);
	struct mcryptd_ahash *mcryptd_tfm = ctx->mcryptd_tfm;

	memcpy(mcryptd_req, req, sizeof(*req));
	ahash_request_set_tfm(mcryptd_req, &mcryptd_tfm->base);
	return crypto_ahash_final(mcryptd_req);
}

static int sha1_mb_async_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct sha1_mb_ctx *ctx = crypto_ahash_ctx(tfm);
	struct ahash_request *mcryptd_req = ahash_request_ctx(req);
	struct mcryptd_ahash *mcryptd_tfm = ctx->mcryptd_tfm;

	memcpy(mcryptd_req, req, sizeof(*req));
	ahash_request_set_tfm(mcryptd_req, &mcryptd_tfm->base);
	return crypto_ahash_digest(mcryptd_req);
}

static int sha1_mb_async_init_tfm(struct crypto_tfm *tfm)
{
	struct mcryptd_ahash *mcryptd_tfm;
	struct sha1_mb_ctx *ctx = crypto_tfm_ctx(tfm);
	struct mcryptd_hash_ctx *mctx;

	mcryptd_tfm = mcryptd_alloc_ahash("__intel_sha1-mb", 0, 0);
	if (IS_ERR(mcryptd_tfm))
		return PTR_ERR(mcryptd_tfm);
	mctx = crypto_ahash_ctx(&mcryptd_tfm->base);
	mctx->alg_state = &sha1_mb_alg_state;
	ctx->mcryptd_tfm = mcryptd_tfm;
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct ahash_request) +
				 crypto_ahash_reqsize(&mcryptd_tfm->base));

	return 0;
}

static void sha1_mb_async_exit_tfm(struct crypto_tfm *tfm)
{
	struct sha1_mb_ctx *ctx = crypto_tfm_ctx(tfm);

	mcryptd_free_ahash(ctx->mcryptd_tfm);
}

static struct ahash_alg sha1_mb_async_alg = {
	.init           = sha1_mb_async_init,
	.update         = sha1_mb_async_update,
	.final          = sha1_mb_async_final,
	.finup          = sha1_mb_async_finup,
	.digest         = sha1_mb_async_digest,
	.halg = {
		.digestsize     = SHA1_DIGEST_SIZE,
		.base = {
			.cra_name               = "sha1",
			.cra_driver_name        = "sha1_mb",
			.cra_priority           = 200,
			.cra_flags              = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
			.cra_blocksize          = SHA1_BLOCK_SIZE,
			.cra_type               = &crypto_ahash_type,
			.cra_module             = THIS_MODULE,
			.cra_list               = LIST_HEAD_INIT(sha1_mb_async_alg.halg.base.cra_list),
			.cra_init               = sha1_mb_async_init_tfm,
			.cra_exit               = sha1_mb_async_exit_tfm,
			.cra_ctxsize		= sizeof(struct sha1_mb_ctx),
			.cra_alignmask		= 0,
		},
	},
};

static unsigned long sha1_mb_flusher(struct mcryptd_alg_cstate *cstate)
{
	struct mcryptd_hash_request_ctx *rctx;
	unsigned long cur_time;
	unsigned long next_flush = 0;
	struct sha1_hash_ctx *sha_ctx;


	cur_time = jiffies;

	while (!list_empty(&cstate->work_list)) {
		rctx = list_entry(cstate->work_list.next,
				struct mcryptd_hash_request_ctx, waiter);
		if (time_before(cur_time, rctx->tag.expire))
			break;
		kernel_fpu_begin();
		sha_ctx = (struct sha1_hash_ctx *) sha1_ctx_mgr_flush(cstate->mgr);
		kernel_fpu_end();
		if (!sha_ctx) {
			pr_err("sha1_mb error: nothing got flushed for non-empty list\n");
			break;
		}
		rctx = cast_hash_to_mcryptd_ctx(sha_ctx);
		sha_finish_walk(&rctx, cstate, true);
		sha_complete_job(rctx, cstate, 0);
	}

	if (!list_empty(&cstate->work_list)) {
		rctx = list_entry(cstate->work_list.next,
				struct mcryptd_hash_request_ctx, waiter);
		/* get the hash context and then flush time */
		next_flush = rctx->tag.expire;
		mcryptd_arm_flusher(cstate, get_delay(next_flush));
	}
	return next_flush;
}

static int __init sha1_mb_mod_init(void)
{

	int cpu;
	int err;
	struct mcryptd_alg_cstate *cpu_state;

	/* check for dependent cpu features */
	if (!boot_cpu_has(X86_FEATURE_AVX2) ||
	    !boot_cpu_has(X86_FEATURE_BMI2))
		return -ENODEV;

	/* initialize multibuffer structures */
	sha1_mb_alg_state.alg_cstate = alloc_percpu(struct mcryptd_alg_cstate);

	sha1_job_mgr_init = sha1_mb_mgr_init_avx2;
	sha1_job_mgr_submit = sha1_mb_mgr_submit_avx2;
	sha1_job_mgr_flush = sha1_mb_mgr_flush_avx2;
	sha1_job_mgr_get_comp_job = sha1_mb_mgr_get_comp_job_avx2;

	if (!sha1_mb_alg_state.alg_cstate)
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		cpu_state = per_cpu_ptr(sha1_mb_alg_state.alg_cstate, cpu);
		cpu_state->next_flush = 0;
		cpu_state->next_seq_num = 0;
		cpu_state->flusher_engaged = false;
		INIT_DELAYED_WORK(&cpu_state->flush, mcryptd_flusher);
		cpu_state->cpu = cpu;
		cpu_state->alg_state = &sha1_mb_alg_state;
		cpu_state->mgr = (struct sha1_ctx_mgr *) kzalloc(sizeof(struct sha1_ctx_mgr), GFP_KERNEL);
		if (!cpu_state->mgr)
			goto err2;
		sha1_ctx_mgr_init(cpu_state->mgr);
		INIT_LIST_HEAD(&cpu_state->work_list);
		spin_lock_init(&cpu_state->work_lock);
	}
	sha1_mb_alg_state.flusher = &sha1_mb_flusher;

	err = crypto_register_shash(&sha1_mb_shash_alg);
	if (err)
		goto err2;
	err = crypto_register_ahash(&sha1_mb_async_alg);
	if (err)
		goto err1;


	return 0;
err1:
	crypto_unregister_shash(&sha1_mb_shash_alg);
err2:
	for_each_possible_cpu(cpu) {
		cpu_state = per_cpu_ptr(sha1_mb_alg_state.alg_cstate, cpu);
		kfree(cpu_state->mgr);
	}
	free_percpu(sha1_mb_alg_state.alg_cstate);
	return -ENODEV;
}

static void __exit sha1_mb_mod_fini(void)
{
	int cpu;
	struct mcryptd_alg_cstate *cpu_state;

	crypto_unregister_ahash(&sha1_mb_async_alg);
	crypto_unregister_shash(&sha1_mb_shash_alg);
	for_each_possible_cpu(cpu) {
		cpu_state = per_cpu_ptr(sha1_mb_alg_state.alg_cstate, cpu);
		kfree(cpu_state->mgr);
	}
	free_percpu(sha1_mb_alg_state.alg_cstate);
}

module_init(sha1_mb_mod_init);
module_exit(sha1_mb_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, multi buffer accelerated");

MODULE_ALIAS_CRYPTO("sha1");
