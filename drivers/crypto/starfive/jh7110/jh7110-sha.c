// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 StarFive, Inc <huan.feng@starfivetech.com>
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING
 * CUSTOMERS WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER
 * FOR THEM TO SAVE TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE
 * FOR ANY DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY
 * CLAIMS ARISING FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE
 * BY CUSTOMERS OF THE CODING INFORMATION CONTAINED HEREIN IN CONNECTION
 * WITH THEIR PRODUCTS.
 */
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <linux/dma-direct.h>
#include <crypto/hash.h>
#include <crypto/sm3.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>

#include "jh7110-pl080.h"
#include "jh7110-str.h"

#define HASH_OP_UPDATE			1
#define HASH_OP_FINAL			2

#define HASH_FLAGS_INIT			BIT(0)
#define HASH_FLAGS_FINAL		BIT(1)
#define HASH_FLAGS_FINUP		BIT(2)

#define JH7110_MAX_ALIGN_SIZE	SHA512_BLOCK_SIZE

#define JH7110_HASH_BUFLEN		8192

static inline int jh7110_hash_wait_hmac_done(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret = -1;

	if (sdev->done_flags & JH7110_SHA_HMAC_DONE)
		ret = 0;

	return ret;
}

static inline int jh7110_hash_wait_busy(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_SHA_SHACSR, status,
			!(status & JH7110_SHA_BUSY), 10, 100000);
}

static inline int jh7110_hash_wait_key_done(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_SHA_SHACSR, status,
			(status & JH7110_SHA_KEY_DONE), 10, 100000);
}

static int jh7110_get_hash_size(struct jh7110_sec_ctx *ctx)
{
	unsigned int hashsize;

	switch (ctx->sha_mode & JH7110_SHA_MODE_MASK) {
	case JH7110_SHA_SHA1:
		hashsize = SHA1_DIGEST_SIZE;
		break;
	case JH7110_SHA_SHA224:
		hashsize = SHA224_DIGEST_SIZE;
		break;
	case JH7110_SHA_SHA256:
		hashsize = SHA256_DIGEST_SIZE;
		break;
	case JH7110_SHA_SHA384:
		hashsize = SHA384_DIGEST_SIZE;
		break;
	case JH7110_SHA_SHA512:
		hashsize = SHA512_DIGEST_SIZE;
		break;
	case JH7110_SHA_SM3:
		hashsize = SM3_DIGEST_SIZE;
		break;
	default:
		return 0;
	}
	return hashsize;
}

static void jh7110_hash_start(struct jh7110_sec_ctx *ctx, int flags)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;

	rctx->csr.sha_csr.v = jh7110_sec_read(sdev, JH7110_SHA_SHACSR);
	rctx->csr.sha_csr.firstb = 0;

	if (flags)
		rctx->csr.sha_csr.final = 1;

	jh7110_sec_write(sdev, JH7110_SHA_SHACSR, rctx->csr.sha_csr.v);
}

static int jh7110_sha_hmac_key(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int klen = ctx->keylen, loop;
	unsigned int *key_tmp;

	jh7110_sec_write(sdev, JH7110_SHA_SHAWKLEN, ctx->keylen);

	rctx->csr.sha_csr.hmac = !!(ctx->sha_mode & JH7110_SHA_HMAC_FLAGS);
	rctx->csr.sha_csr.key_flag = 1;

	jh7110_sec_write(sdev, JH7110_SHA_SHACSR, rctx->csr.sha_csr.v);

	key_tmp = (unsigned int *)ctx->key;

	for (loop = 0; loop < klen / sizeof(unsigned int); loop++)
		jh7110_sec_write(sdev, JH7110_SHA_SHAWKR, key_tmp[loop]);

	if (jh7110_hash_wait_key_done(ctx)) {
		dev_err(sdev->dev, " jh7110_hash_wait_key_done error\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static void jh7110_sha_dma_callback(void *param)
{
	struct jh7110_sec_dev *sdev = param;

	complete(&sdev->sec_comp_m);
}

static int jh7110_hash_xmit_dma(struct jh7110_sec_ctx *ctx, int flags)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct dma_async_tx_descriptor	*in_desc;
	dma_cookie_t cookie;
	union  jh7110_alg_cr alg_cr;
	int total_len;
	int ret;

	if (!rctx->bufcnt)
		return 0;

	ctx->sha_len_total += rctx->bufcnt;

	total_len = rctx->bufcnt;

	jh7110_sec_write(sdev, JH7110_DMA_IN_LEN_OFFSET, rctx->bufcnt);

	total_len = (total_len & 0x3) ? (((total_len >> 2) + 1) << 2) : total_len;

	memset(sdev->sha_data + rctx->bufcnt, 0, total_len - rctx->bufcnt);

	alg_cr.v = 0;
	alg_cr.start = 1;
	alg_cr.sha_dma_en = 1;
	jh7110_sec_write(sdev, JH7110_ALG_CR_OFFSET, alg_cr.v);

	sg_init_table(&ctx->sg[0], 1);
	sg_set_buf(&ctx->sg[0], sdev->sha_data, total_len);
	sg_dma_address(&ctx->sg[0]) = phys_to_dma(sdev->dev, (unsigned long long)(sdev->sha_data));
	sg_dma_len(&ctx->sg[0]) = total_len;

	ret = dma_map_sg(sdev->dev, &ctx->sg[0], 1, DMA_TO_DEVICE);
	if (!ret) {
		dev_err(sdev->dev, "dma_map_sg() error\n");
		return -EINVAL;
	}

	sdev->cfg_in.direction = DMA_MEM_TO_DEV;
	sdev->cfg_in.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_in.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sdev->cfg_in.src_maxburst = sdev->dma_maxburst;
	sdev->cfg_in.dst_maxburst = sdev->dma_maxburst;
	sdev->cfg_in.dst_addr = sdev->io_phys_base + JH7110_ALG_FIFO_OFFSET;

	dmaengine_slave_config(sdev->sec_xm_m, &sdev->cfg_in);

	in_desc = dmaengine_prep_slave_sg(sdev->sec_xm_m, &ctx->sg[0],
				1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT  |  DMA_CTRL_ACK);
	if (!in_desc)
		return -EINVAL;

	reinit_completion(&sdev->sec_comp_m);

	in_desc->callback = jh7110_sha_dma_callback;
	in_desc->callback_param = sdev;

	cookie = dmaengine_submit(in_desc);
	dma_async_issue_pending(sdev->sec_xm_m);

	if (!wait_for_completion_timeout(&sdev->sec_comp_m,
					msecs_to_jiffies(10000))) {
		dev_dbg(sdev->dev, "this is debug for lophyel status = %x err = %x control0 = %x control1 = %x  %s %s %d\n",
		       readl_relaxed(sdev->dma_base + PL080_TC_STATUS), readl_relaxed(sdev->dma_base + PL080_ERR_STATUS),
		       readl_relaxed(sdev->dma_base + 0x10c), readl_relaxed(sdev->dma_base + 0x12c),
		       __FILE__, __func__, __LINE__);
		dev_err(sdev->dev, "wait_for_completion_timeout out error cookie = %x\n",
			dma_async_is_tx_complete(sdev->sec_xm_p, cookie,
				     NULL, NULL));
	}

	dma_unmap_sg(sdev->dev, &ctx->sg[0], 1, DMA_TO_DEVICE);

	alg_cr.v = 0;
	alg_cr.clear = 1;
	jh7110_sec_write(sdev, JH7110_ALG_CR_OFFSET, alg_cr.v);

	return 0;
}

static int jh7110_hash_xmit_cpu(struct jh7110_sec_ctx *ctx, int flags)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int total_len, mlen, loop;
	unsigned int  *buffer;
	unsigned char *cl;

	if (!rctx->bufcnt)
		return 0;

	ctx->sha_len_total += rctx->bufcnt;

	total_len = rctx->bufcnt;
	mlen = total_len / sizeof(u32);// DIV_ROUND_UP(total_len, sizeof(u32));
	buffer = (unsigned int *)ctx->buffer;

	for (loop = 0; loop < mlen; loop++, buffer++)
		jh7110_sec_write(sdev, JH7110_SHA_SHAWDR, *buffer);

	if (total_len & 0x3) {
		cl = (unsigned char *)buffer;
		for (loop = 0; loop < (total_len & 0x3); loop++, cl++)
			jh7110_sec_writeb(sdev, JH7110_SHA_SHAWDR, *cl);
	}

	return 0;
}

static void jh7110_hash_append_sg(struct jh7110_sec_request_ctx *rctx)
{
	struct jh7110_sec_ctx *ctx = rctx->ctx;
	size_t count;

	while ((rctx->bufcnt < rctx->buflen) && rctx->total) {
		count = min(rctx->in_sg->length - rctx->offset, rctx->total);
		count = min(count, rctx->buflen - rctx->bufcnt);

		if (count <= 0) {
			if ((rctx->in_sg->length == 0) && !sg_is_last(rctx->in_sg)) {
				rctx->in_sg = sg_next(rctx->in_sg);
				continue;
			} else {
				break;
			}
		}

		scatterwalk_map_and_copy(ctx->buffer + rctx->bufcnt, rctx->in_sg,
					rctx->offset, count, 0);

		rctx->bufcnt += count;
		rctx->offset += count;
		rctx->total -= count;

		if (rctx->offset == rctx->in_sg->length) {
			rctx->in_sg = sg_next(rctx->in_sg);
			if (rctx->in_sg)
				rctx->offset = 0;
			else
				rctx->total = 0;
		}
	}
}

static int jh7110_hash_xmit(struct jh7110_sec_ctx *ctx, int flags)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret;

	sdev->cry_type = JH7110_SHA_TYPE;

	rctx->csr.sha_csr.v = 0;
	rctx->csr.sha_csr.reset = 1;
	jh7110_sec_write(sdev, JH7110_SHA_SHACSR, rctx->csr.sha_csr.v);

	if (jh7110_hash_wait_busy(ctx)) {
		dev_err(sdev->dev, "jh7110_hash_wait_busy error\n");
		return -ETIMEDOUT;
	}

	rctx->csr.sha_csr.v = 0;
	rctx->csr.sha_csr.mode = ctx->sha_mode & JH7110_SHA_MODE_MASK;
	if (ctx->sdev->use_dma)
		rctx->csr.sha_csr.ie = 1;

	if (ctx->sha_mode & JH7110_SHA_HMAC_FLAGS)
		ret = jh7110_sha_hmac_key(ctx);

	if (ret)
		return ret;

	if (ctx->sec_init && !rctx->csr.sha_csr.hmac) {
		rctx->csr.sha_csr.start = 1;
		rctx->csr.sha_csr.firstb = 1;
		ctx->sec_init = 0;
		jh7110_sec_write(sdev, JH7110_SHA_SHACSR, rctx->csr.sha_csr.v);
	}

	if (ctx->sdev->use_dma) {
		ret = jh7110_hash_xmit_dma(ctx, flags);
		if (flags)
			rctx->flags |= HASH_FLAGS_FINAL;
	} else {
		ret = jh7110_hash_xmit_cpu(ctx, flags);
		if (flags)
			rctx->flags |= HASH_FLAGS_FINAL;
	}

	if (ret)
		return ret;

	jh7110_hash_start(ctx, flags);

	if (jh7110_hash_wait_busy(ctx)) {
		dev_err(sdev->dev, "jh7110_hash_wait_busy error\n");
		return -ETIMEDOUT;
	}

	if (ctx->sha_mode & JH7110_SHA_HMAC_FLAGS)
		if (jh7110_hash_wait_hmac_done(ctx)) {
			dev_err(sdev->dev, "jh7110_hash_wait_hmac_done error\n");
			return -ETIMEDOUT;
		}
	return 0;
}

static int jh7110_hash_update_req(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	int err = 0, final;

	final = (rctx->flags & HASH_FLAGS_FINUP);

	while ((rctx->total >= rctx->buflen) ||
			(rctx->bufcnt + rctx->total >= rctx->buflen)) {
		jh7110_hash_append_sg(rctx);
		err = jh7110_hash_xmit(ctx, 0);
		rctx->bufcnt = 0;
	}

	jh7110_hash_append_sg(rctx);

	if (final) {
		err = jh7110_hash_xmit(ctx,
					(rctx->flags & HASH_FLAGS_FINUP));
		rctx->bufcnt = 0;
	}

	return err;
}

static int jh7110_hash_final_req(struct jh7110_sec_ctx *ctx)
{
	struct ahash_request *req = ctx->rctx->req.hreq;
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	int err = 0;

	err = jh7110_hash_xmit(ctx, 1);
	rctx->bufcnt = 0;

	return err;
}


static int jh7110_hash_out_cpu(struct ahash_request *req)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	struct jh7110_sec_ctx *ctx = rctx->ctx;
	int count, *data;
	int mlen;

	if (!req->result)
		return 0;

	mlen = jh7110_get_hash_size(ctx) / sizeof(u32);

	data = (u32 *)req->result;
	for (count = 0; count < mlen; count++)
		data[count] = jh7110_sec_read(ctx->sdev, JH7110_SHA_SHARDR);

	return 0;
}

static int jh7110_hash_copy_hash(struct ahash_request *req)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	struct jh7110_sec_ctx *ctx = rctx->ctx;
	int hashsize;
	int ret;

	hashsize = jh7110_get_hash_size(ctx);

	ret = jh7110_hash_out_cpu(req);

	if (ret)
		return ret;

	memcpy(rctx->sha_digest_mid, req->result, hashsize);
	rctx->sha_digest_len = hashsize;

	return ret;
}

static void jh7110_hash_finish_req(struct ahash_request *req, int err)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	struct jh7110_sec_dev *sdev = rctx->sdev;

	if (!err && (HASH_FLAGS_FINAL & rctx->flags)) {
		err = jh7110_hash_copy_hash(req);
		rctx->flags &= ~(HASH_FLAGS_FINAL |
				 HASH_FLAGS_INIT);
	}

	crypto_finalize_hash_request(sdev->engine, req, err);
}

static int jh7110_hash_prepare_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx;

	if (!sdev)
		return -ENODEV;

	mutex_lock(&ctx->sdev->lock);

	rctx = ahash_request_ctx(req);

	rctx->req.hreq = req;

	return 0;
}

static int jh7110_hash_one_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = container_of(areq, struct ahash_request,
						 base);
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx;
	int err = 0;

	if (!sdev)
		return -ENODEV;

	rctx = ahash_request_ctx(req);

	if (rctx->op == HASH_OP_UPDATE)
		err = jh7110_hash_update_req(ctx);
	else if (rctx->op == HASH_OP_FINAL)
		err = jh7110_hash_final_req(ctx);

	if (err != -EINPROGRESS)
	/* done task will not finish it, so do it here */
		jh7110_hash_finish_req(req, err);

	mutex_unlock(&ctx->sdev->lock);

	return 0;
}

static int jh7110_hash_handle_queue(struct jh7110_sec_dev *sdev,
					struct ahash_request *req)
{
	return crypto_transfer_hash_request_to_engine(sdev->engine, req);
}

static int jh7110_hash_enqueue(struct ahash_request *req, unsigned int op)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	struct jh7110_sec_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct jh7110_sec_dev *sdev = ctx->sdev;

	rctx->op = op;

	return jh7110_hash_handle_queue(sdev, req);
}

static int jh7110_hash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	struct jh7110_sec_dev *sdev = ctx->sdev;

	memset(rctx, 0, sizeof(struct jh7110_sec_request_ctx));

	rctx->sdev = sdev;
	rctx->ctx = ctx;
	rctx->req.hreq = req;
	rctx->bufcnt = 0;

	rctx->total = 0;
	rctx->offset = 0;
	rctx->bufcnt = 0;
	rctx->buflen = JH7110_HASH_BUFLEN;

	memset(ctx->buffer, 0, JH7110_HASH_BUFLEN);

	ctx->rctx = rctx;

	dev_dbg(sdev->dev, "%s Flags %lx\n", __func__, rctx->flags);

	return 0;
}

static int jh7110_hash_update(struct ahash_request *req)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return 0;

	rctx->total = req->nbytes;
	rctx->in_sg = req->src;
	rctx->offset = 0;

	if ((rctx->bufcnt + rctx->total < rctx->buflen)) {
		jh7110_hash_append_sg(rctx);
		return 0;
	}

	return jh7110_hash_enqueue(req, HASH_OP_UPDATE);
}

static int jh7110_hash_final(struct ahash_request *req)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);

	rctx->flags |= HASH_FLAGS_FINUP;

	return jh7110_hash_enqueue(req, HASH_OP_FINAL);
}

static int jh7110_hash_finup(struct ahash_request *req)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);
	int err1, err2;
	int nents;

	nents = sg_nents_for_len(req->src, req->nbytes);

	rctx->flags |= HASH_FLAGS_FINUP;

	err1 = jh7110_hash_update(req);

	if (err1 == -EINPROGRESS || err1 == -EBUSY)
		return err1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if update() failed, except EINPROGRESS
	 */
	err2 = jh7110_hash_final(req);

	return err1 ?: err2;
}

static int jh7110_hash_digest(struct ahash_request *req)
{
	return jh7110_hash_init(req) ?: jh7110_hash_finup(req);
}

static int jh7110_hash_export(struct ahash_request *req, void *out)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int jh7110_hash_import(struct ahash_request *req, const void *in)
{
	struct jh7110_sec_request_ctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static int jh7110_hash_cra_init_algs(struct crypto_tfm *tfm,
					const char *algs_hmac_name,
					unsigned int mode)
{
	struct jh7110_sec_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->sdev = jh7110_sec_find_dev(ctx);

	if (!ctx->sdev)
		return -ENODEV;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				sizeof(struct jh7110_sec_request_ctx));

	ctx->sec_init = 1;
	ctx->keylen   = 0;
	ctx->sha_mode = mode;
	ctx->sha_len_total = 0;
	ctx->buffer = ctx->sdev->sha_data;

	if (algs_hmac_name)
		ctx->sha_mode |= JH7110_SHA_HMAC_FLAGS;

	ctx->enginectx.op.do_one_request = jh7110_hash_one_request;
	ctx->enginectx.op.prepare_request = jh7110_hash_prepare_req;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void jh7110_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->enginectx.op.do_one_request = NULL;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
}

static int jh7110_hash_long_setkey(struct jh7110_sec_ctx *ctx,
					const u8 *key, unsigned int keylen,
					const char *alg_name)
{
	struct crypto_wait wait;
	struct ahash_request *req;
	struct scatterlist sg;
	struct crypto_ahash *ahash_tfm;
	u8 *buf;
	int ret;

	ahash_tfm = crypto_alloc_ahash(alg_name, 0, 0);
	if (IS_ERR(ahash_tfm))
		return PTR_ERR(ahash_tfm);

	req = ahash_request_alloc(ahash_tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto err_free_ahash;
	}

	crypto_init_wait(&wait);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &wait);
	crypto_ahash_clear_flags(ahash_tfm, ~0);

	buf = kzalloc(keylen + JH7110_MAX_ALIGN_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_req;
	}

	memcpy(buf, key, keylen);
	sg_init_one(&sg, buf, keylen);
	ahash_request_set_crypt(req, &sg, ctx->key, keylen);

	ret = crypto_wait_req(crypto_ahash_digest(req), &wait);

err_free_req:
	ahash_request_free(req);
err_free_ahash:
	crypto_free_ahash(ahash_tfm);
	return ret;
}

static int jh7110_hash1_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));
	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sha1");
	}

	return ret;
}

static int jh7110_hash224_setkey(struct crypto_ahash *tfm,
				const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sha224");
	}

	return ret;
}

static int jh7110_hash256_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sha256");
	}

	return ret;
}

static int jh7110_hash384_setkey(struct crypto_ahash *tfm,
				const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sha384");
	}

	return ret;
}

static int jh7110_hash512_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sha512");
	}

	return ret;
}

static int jh7110_sm3_setkey(struct crypto_ahash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_ahash_ctx(tfm);
	unsigned int digestsize = crypto_ahash_digestsize(tfm);
	unsigned int blocksize;
	int ret = 0;

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	if (keylen <= blocksize) {
		memcpy(ctx->key, key, keylen);
		ctx->keylen = keylen;
	} else {
		ctx->keylen = digestsize;
		ret = jh7110_hash_long_setkey(ctx, key, keylen, "jh7110-sm3");
	}

	return ret;
}

static int jh7110_hash_cra_sha1_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SHA1);
}

static int jh7110_hash_cra_sha224_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SHA224);
}

static int jh7110_hash_cra_sha256_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SHA256);
}

static int jh7110_hash_cra_sha384_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SHA384);
}

static int jh7110_hash_cra_sha512_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SHA512);
}

static int jh7110_hash_cra_sm3_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, NULL, JH7110_SHA_SM3);
}

static int jh7110_hash_cra_hmac_sha1_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sha1", JH7110_SHA_SHA1);
}

static int jh7110_hash_cra_hmac_sha224_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sha224", JH7110_SHA_SHA224);
}

static int jh7110_hash_cra_hmac_sha256_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sha256", JH7110_SHA_SHA256);
}

static int jh7110_hash_cra_hmac_sha384_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sha384", JH7110_SHA_SHA384);
}

static int jh7110_hash_cra_hmac_sha512_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sha512", JH7110_SHA_SHA512);
}

static int jh7110_hash_cra_hmac_sm3_init(struct crypto_tfm *tfm)
{
	return jh7110_hash_cra_init_algs(tfm, "sm3", JH7110_SHA_SM3);
}

static struct ahash_alg algs_sha0_sha512_sm3[] = {
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sha1",
				.cra_driver_name	= "jh7110-sha1",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA1_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sha1_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.setkey   = jh7110_hash1_setkey,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sha1)",
				.cra_driver_name	= "jh7110-hmac-sha1",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA1_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sha1_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},

	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sha224",
				.cra_driver_name	= "jh7110-sha224",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA224_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sha224_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.setkey   = jh7110_hash224_setkey,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sha224)",
				.cra_driver_name	= "jh7110-hmac-sha224",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA224_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sha224_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sha256",
				.cra_driver_name	= "jh7110-sha256",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA256_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sha256_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.setkey   = jh7110_hash256_setkey,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sha256)",
				.cra_driver_name	= "jh7110-hmac-sha256",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA256_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sha256_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sha384",
				.cra_driver_name	= "jh7110-sha384",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA384_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sha384_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.setkey   = jh7110_hash384_setkey,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sha384)",
				.cra_driver_name	= "jh7110-hmac-sha384",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA384_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sha384_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sha512",
				.cra_driver_name	= "jh7110-sha512",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA512_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sha512_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.setkey   = jh7110_hash512_setkey,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sha512)",
				.cra_driver_name	= "jh7110-hmac-sha512",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA512_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sha512_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init     = jh7110_hash_init,
		.update   = jh7110_hash_update,
		.final    = jh7110_hash_final,
		.finup    = jh7110_hash_finup,
		.digest   = jh7110_hash_digest,
		.export   = jh7110_hash_export,
		.import   = jh7110_hash_import,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "sm3",
				.cra_driver_name	= "jh7110-sm3",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA512_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_sm3_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
	{
		.init		= jh7110_hash_init,
		.update		= jh7110_hash_update,
		.final		= jh7110_hash_final,
		.finup		= jh7110_hash_finup,
		.digest		= jh7110_hash_digest,
		.setkey		= jh7110_sm3_setkey,
		.export		= jh7110_hash_export,
		.import		= jh7110_hash_import,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize  = sizeof(struct jh7110_sec_request_ctx),
			.base = {
				.cra_name			= "hmac(sm3)",
				.cra_driver_name	= "jh7110-hmac-sm3",
				.cra_priority		= 200,
				.cra_flags			= CRYPTO_ALG_ASYNC |
										CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize		= SHA512_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct jh7110_sec_ctx),
				.cra_alignmask		= 3,
				.cra_init			= jh7110_hash_cra_hmac_sm3_init,
				.cra_exit			= jh7110_hash_cra_exit,
				.cra_module			= THIS_MODULE,
			}
		}
	},
};

int jh7110_hash_register_algs(void)
{
	int ret = 0;

	ret = crypto_register_ahashes(algs_sha0_sha512_sm3, ARRAY_SIZE(algs_sha0_sha512_sm3));

	return ret;
}

void jh7110_hash_unregister_algs(void)
{
	crypto_unregister_ahashes(algs_sha0_sha512_sm3, ARRAY_SIZE(algs_sha0_sha512_sm3));
}
