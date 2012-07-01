/*
 * Cryptographic API.
 *
 * Support for ATMEL SHA1/SHA256 HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Some ideas are from omap-sham.c drivers.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include "atmel-sha-regs.h"

/* SHA flags */
#define SHA_FLAGS_BUSY			BIT(0)
#define	SHA_FLAGS_FINAL			BIT(1)
#define SHA_FLAGS_DMA_ACTIVE	BIT(2)
#define SHA_FLAGS_OUTPUT_READY	BIT(3)
#define SHA_FLAGS_INIT			BIT(4)
#define SHA_FLAGS_CPU			BIT(5)
#define SHA_FLAGS_DMA_READY		BIT(6)

#define SHA_FLAGS_FINUP		BIT(16)
#define SHA_FLAGS_SG		BIT(17)
#define SHA_FLAGS_SHA1		BIT(18)
#define SHA_FLAGS_SHA256	BIT(19)
#define SHA_FLAGS_ERROR		BIT(20)
#define SHA_FLAGS_PAD		BIT(21)

#define SHA_FLAGS_DUALBUFF	BIT(24)

#define SHA_OP_UPDATE	1
#define SHA_OP_FINAL	2

#define SHA_BUFFER_LEN		PAGE_SIZE

#define ATMEL_SHA_DMA_THRESHOLD		56


struct atmel_sha_dev;

struct atmel_sha_reqctx {
	struct atmel_sha_dev	*dd;
	unsigned long	flags;
	unsigned long	op;

	u8	digest[SHA256_DIGEST_SIZE] __aligned(sizeof(u32));
	size_t	digcnt;
	size_t	bufcnt;
	size_t	buflen;
	dma_addr_t	dma_addr;

	/* walk state */
	struct scatterlist	*sg;
	unsigned int	offset;	/* offset in current sg */
	unsigned int	total;	/* total request */

	u8	buffer[0] __aligned(sizeof(u32));
};

struct atmel_sha_ctx {
	struct atmel_sha_dev	*dd;

	unsigned long		flags;

	/* fallback stuff */
	struct crypto_shash	*fallback;

};

#define ATMEL_SHA_QUEUE_LENGTH	1

struct atmel_sha_dev {
	struct list_head	list;
	unsigned long		phys_base;
	struct device		*dev;
	struct clk			*iclk;
	int					irq;
	void __iomem		*io_base;

	spinlock_t		lock;
	int			err;
	struct tasklet_struct	done_task;

	unsigned long		flags;
	struct crypto_queue	queue;
	struct ahash_request	*req;
};

struct atmel_sha_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
};

static struct atmel_sha_drv atmel_sha = {
	.dev_list = LIST_HEAD_INIT(atmel_sha.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(atmel_sha.lock),
};

static inline u32 atmel_sha_read(struct atmel_sha_dev *dd, u32 offset)
{
	return readl_relaxed(dd->io_base + offset);
}

static inline void atmel_sha_write(struct atmel_sha_dev *dd,
					u32 offset, u32 value)
{
	writel_relaxed(value, dd->io_base + offset);
}

static void atmel_sha_dualbuff_test(struct atmel_sha_dev *dd)
{
	atmel_sha_write(dd, SHA_MR, SHA_MR_DUALBUFF);

	if (atmel_sha_read(dd, SHA_MR) & SHA_MR_DUALBUFF)
		dd->flags |= SHA_FLAGS_DUALBUFF;
}

static size_t atmel_sha_append_sg(struct atmel_sha_reqctx *ctx)
{
	size_t count;

	while ((ctx->bufcnt < ctx->buflen) && ctx->total) {
		count = min(ctx->sg->length - ctx->offset, ctx->total);
		count = min(count, ctx->buflen - ctx->bufcnt);

		if (count <= 0)
			break;

		scatterwalk_map_and_copy(ctx->buffer + ctx->bufcnt, ctx->sg,
			ctx->offset, count, 0);

		ctx->bufcnt += count;
		ctx->offset += count;
		ctx->total -= count;

		if (ctx->offset == ctx->sg->length) {
			ctx->sg = sg_next(ctx->sg);
			if (ctx->sg)
				ctx->offset = 0;
			else
				ctx->total = 0;
		}
	}

	return 0;
}

/*
 * The purpose of this padding is to ensure that the padded message
 * is a multiple of 512 bits. The bit "1" is appended at the end of
 * the message followed by "padlen-1" zero bits. Then a 64 bits block
 * equals to the message length in bits is appended.
 *
 * padlen is calculated as followed:
 *  - if message length < 56 bytes then padlen = 56 - message length
 *  - else padlen = 64 + 56 - message length
 */
static void atmel_sha_fill_padding(struct atmel_sha_reqctx *ctx, int length)
{
	unsigned int index, padlen;
	u64 bits;
	u64 size;

	bits = (ctx->bufcnt + ctx->digcnt + length) << 3;
	size = cpu_to_be64(bits);

	index = ctx->bufcnt & 0x3f;
	padlen = (index < 56) ? (56 - index) : ((64+56) - index);
	*(ctx->buffer + ctx->bufcnt) = 0x80;
	memset(ctx->buffer + ctx->bufcnt + 1, 0, padlen-1);
	memcpy(ctx->buffer + ctx->bufcnt + padlen, &size, 8);
	ctx->bufcnt += padlen + 8;
	ctx->flags |= SHA_FLAGS_PAD;
}

static int atmel_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct atmel_sha_ctx *tctx = crypto_ahash_ctx(tfm);
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = NULL;
	struct atmel_sha_dev *tmp;

	spin_lock_bh(&atmel_sha.lock);
	if (!tctx->dd) {
		list_for_each_entry(tmp, &atmel_sha.dev_list, list) {
			dd = tmp;
			break;
		}
		tctx->dd = dd;
	} else {
		dd = tctx->dd;
	}

	spin_unlock_bh(&atmel_sha.lock);

	ctx->dd = dd;

	ctx->flags = 0;

	dev_dbg(dd->dev, "init: digest size: %d\n",
		crypto_ahash_digestsize(tfm));

	if (crypto_ahash_digestsize(tfm) == SHA1_DIGEST_SIZE)
		ctx->flags |= SHA_FLAGS_SHA1;
	else if (crypto_ahash_digestsize(tfm) == SHA256_DIGEST_SIZE)
		ctx->flags |= SHA_FLAGS_SHA256;

	ctx->bufcnt = 0;
	ctx->digcnt = 0;
	ctx->buflen = SHA_BUFFER_LEN;

	return 0;
}

static void atmel_sha_write_ctrl(struct atmel_sha_dev *dd, int dma)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	u32 valcr = 0, valmr = SHA_MR_MODE_AUTO;

	if (likely(dma)) {
		atmel_sha_write(dd, SHA_IER, SHA_INT_TXBUFE);
		valmr = SHA_MR_MODE_PDC;
		if (dd->flags & SHA_FLAGS_DUALBUFF)
			valmr = SHA_MR_DUALBUFF;
	} else {
		atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
	}

	if (ctx->flags & SHA_FLAGS_SHA256)
		valmr |= SHA_MR_ALGO_SHA256;

	/* Setting CR_FIRST only for the first iteration */
	if (!ctx->digcnt)
		valcr = SHA_CR_FIRST;

	atmel_sha_write(dd, SHA_CR, valcr);
	atmel_sha_write(dd, SHA_MR, valmr);
}

static int atmel_sha_xmit_cpu(struct atmel_sha_dev *dd, const u8 *buf,
			      size_t length, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int count, len32;
	const u32 *buffer = (const u32 *)buf;

	dev_dbg(dd->dev, "xmit_cpu: digcnt: %d, length: %d, final: %d\n",
						ctx->digcnt, length, final);

	atmel_sha_write_ctrl(dd, 0);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt += length;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	len32 = DIV_ROUND_UP(length, sizeof(u32));

	dd->flags |= SHA_FLAGS_CPU;

	for (count = 0; count < len32; count++)
		atmel_sha_write(dd, SHA_REG_DIN(count), buffer[count]);

	return -EINPROGRESS;
}

static int atmel_sha_xmit_pdc(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int len32;

	dev_dbg(dd->dev, "xmit_pdc: digcnt: %d, length: %d, final: %d\n",
						ctx->digcnt, length1, final);

	len32 = DIV_ROUND_UP(length1, sizeof(u32));
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTDIS);
	atmel_sha_write(dd, SHA_TPR, dma_addr1);
	atmel_sha_write(dd, SHA_TCR, len32);

	len32 = DIV_ROUND_UP(length2, sizeof(u32));
	atmel_sha_write(dd, SHA_TNPR, dma_addr2);
	atmel_sha_write(dd, SHA_TNCR, len32);

	atmel_sha_write_ctrl(dd, 1);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt += length1;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	dd->flags |=  SHA_FLAGS_DMA_ACTIVE;

	/* Start DMA transfer */
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTEN);

	return -EINPROGRESS;
}

static int atmel_sha_update_cpu(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	int bufcnt;

	atmel_sha_append_sg(ctx);
	atmel_sha_fill_padding(ctx, 0);

	bufcnt = ctx->bufcnt;
	ctx->bufcnt = 0;

	return atmel_sha_xmit_cpu(dd, ctx->buffer, bufcnt, 1);
}

static int atmel_sha_xmit_dma_map(struct atmel_sha_dev *dd,
					struct atmel_sha_reqctx *ctx,
					size_t length, int final)
{
	ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer,
				ctx->buflen + SHA1_BLOCK_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
		dev_err(dd->dev, "dma %u bytes error\n", ctx->buflen +
				SHA1_BLOCK_SIZE);
		return -EINVAL;
	}

	ctx->flags &= ~SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_pdc(dd, ctx->dma_addr, length, 0, 0, final);
}

static int atmel_sha_update_dma_slow(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int final;
	size_t count;

	atmel_sha_append_sg(ctx);

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	dev_dbg(dd->dev, "slow: bufcnt: %u, digcnt: %d, final: %d\n",
					 ctx->bufcnt, ctx->digcnt, final);

	if (final)
		atmel_sha_fill_padding(ctx, 0);

	if (final || (ctx->bufcnt == ctx->buflen && ctx->total)) {
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		return atmel_sha_xmit_dma_map(dd, ctx, count, final);
	}

	return 0;
}

static int atmel_sha_update_dma_start(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int length, final, tail;
	struct scatterlist *sg;
	unsigned int count;

	if (!ctx->total)
		return 0;

	if (ctx->bufcnt || ctx->offset)
		return atmel_sha_update_dma_slow(dd);

	dev_dbg(dd->dev, "fast: digcnt: %d, bufcnt: %u, total: %u\n",
			ctx->digcnt, ctx->bufcnt, ctx->total);

	sg = ctx->sg;

	if (!IS_ALIGNED(sg->offset, sizeof(u32)))
		return atmel_sha_update_dma_slow(dd);

	if (!sg_is_last(sg) && !IS_ALIGNED(sg->length, SHA1_BLOCK_SIZE))
		/* size is not SHA1_BLOCK_SIZE aligned */
		return atmel_sha_update_dma_slow(dd);

	length = min(ctx->total, sg->length);

	if (sg_is_last(sg)) {
		if (!(ctx->flags & SHA_FLAGS_FINUP)) {
			/* not last sg must be SHA1_BLOCK_SIZE aligned */
			tail = length & (SHA1_BLOCK_SIZE - 1);
			length -= tail;
			if (length == 0) {
				/* offset where to start slow */
				ctx->offset = length;
				return atmel_sha_update_dma_slow(dd);
			}
		}
	}

	ctx->total -= length;
	ctx->offset = length; /* offset where to start slow */

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	/* Add padding */
	if (final) {
		tail = length & (SHA1_BLOCK_SIZE - 1);
		length -= tail;
		ctx->total += tail;
		ctx->offset = length; /* offset where to start slow */

		sg = ctx->sg;
		atmel_sha_append_sg(ctx);

		atmel_sha_fill_padding(ctx, length);

		ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer,
			ctx->buflen + SHA1_BLOCK_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
			dev_err(dd->dev, "dma %u bytes error\n",
				ctx->buflen + SHA1_BLOCK_SIZE);
			return -EINVAL;
		}

		if (length == 0) {
			ctx->flags &= ~SHA_FLAGS_SG;
			count = ctx->bufcnt;
			ctx->bufcnt = 0;
			return atmel_sha_xmit_pdc(dd, ctx->dma_addr, count, 0,
					0, final);
		} else {
			ctx->sg = sg;
			if (!dma_map_sg(dd->dev, ctx->sg, 1,
				DMA_TO_DEVICE)) {
					dev_err(dd->dev, "dma_map_sg  error\n");
					return -EINVAL;
			}

			ctx->flags |= SHA_FLAGS_SG;

			count = ctx->bufcnt;
			ctx->bufcnt = 0;
			return atmel_sha_xmit_pdc(dd, sg_dma_address(ctx->sg),
					length, ctx->dma_addr, count, final);
		}
	}

	if (!dma_map_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE)) {
		dev_err(dd->dev, "dma_map_sg  error\n");
		return -EINVAL;
	}

	ctx->flags |= SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_pdc(dd, sg_dma_address(ctx->sg), length, 0,
								0, final);
}

static int atmel_sha_update_dma_stop(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);

	if (ctx->flags & SHA_FLAGS_SG) {
		dma_unmap_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE);
		if (ctx->sg->length == ctx->offset) {
			ctx->sg = sg_next(ctx->sg);
			if (ctx->sg)
				ctx->offset = 0;
		}
		if (ctx->flags & SHA_FLAGS_PAD)
			dma_unmap_single(dd->dev, ctx->dma_addr,
				ctx->buflen + SHA1_BLOCK_SIZE, DMA_TO_DEVICE);
	} else {
		dma_unmap_single(dd->dev, ctx->dma_addr, ctx->buflen +
						SHA1_BLOCK_SIZE, DMA_TO_DEVICE);
	}

	return 0;
}

static int atmel_sha_update_req(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err;

	dev_dbg(dd->dev, "update_req: total: %u, digcnt: %d, finup: %d\n",
		 ctx->total, ctx->digcnt, (ctx->flags & SHA_FLAGS_FINUP) != 0);

	if (ctx->flags & SHA_FLAGS_CPU)
		err = atmel_sha_update_cpu(dd);
	else
		err = atmel_sha_update_dma_start(dd);

	/* wait for dma completion before can take more data */
	dev_dbg(dd->dev, "update: err: %d, digcnt: %d\n",
			err, ctx->digcnt);

	return err;
}

static int atmel_sha_final_req(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err = 0;
	int count;

	if (ctx->bufcnt >= ATMEL_SHA_DMA_THRESHOLD) {
		atmel_sha_fill_padding(ctx, 0);
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		err = atmel_sha_xmit_dma_map(dd, ctx, count, 1);
	}
	/* faster to handle last block with cpu */
	else {
		atmel_sha_fill_padding(ctx, 0);
		count = ctx->bufcnt;
		ctx->bufcnt = 0;
		err = atmel_sha_xmit_cpu(dd, ctx->buffer, count, 1);
	}

	dev_dbg(dd->dev, "final_req: err: %d\n", err);

	return err;
}

static void atmel_sha_copy_hash(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	u32 *hash = (u32 *)ctx->digest;
	int i;

	if (likely(ctx->flags & SHA_FLAGS_SHA1))
		for (i = 0; i < SHA1_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	else
		for (i = 0; i < SHA256_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
}

static void atmel_sha_copy_ready_hash(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!req->result)
		return;

	if (likely(ctx->flags & SHA_FLAGS_SHA1))
		memcpy(req->result, ctx->digest, SHA1_DIGEST_SIZE);
	else
		memcpy(req->result, ctx->digest, SHA256_DIGEST_SIZE);
}

static int atmel_sha_finish(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = ctx->dd;
	int err = 0;

	if (ctx->digcnt)
		atmel_sha_copy_ready_hash(req);

	dev_dbg(dd->dev, "digcnt: %d, bufcnt: %d\n", ctx->digcnt,
		ctx->bufcnt);

	return err;
}

static void atmel_sha_finish_req(struct ahash_request *req, int err)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = ctx->dd;

	if (!err) {
		atmel_sha_copy_hash(req);
		if (SHA_FLAGS_FINAL & dd->flags)
			err = atmel_sha_finish(req);
	} else {
		ctx->flags |= SHA_FLAGS_ERROR;
	}

	/* atomic operation is not needed here */
	dd->flags &= ~(SHA_FLAGS_BUSY | SHA_FLAGS_FINAL | SHA_FLAGS_CPU |
			SHA_FLAGS_DMA_READY | SHA_FLAGS_OUTPUT_READY);

	clk_disable_unprepare(dd->iclk);

	if (req->base.complete)
		req->base.complete(&req->base, err);

	/* handle new request */
	tasklet_schedule(&dd->done_task);
}

static int atmel_sha_hw_init(struct atmel_sha_dev *dd)
{
	clk_prepare_enable(dd->iclk);

	if (SHA_FLAGS_INIT & dd->flags) {
		atmel_sha_write(dd, SHA_CR, SHA_CR_SWRST);
		atmel_sha_dualbuff_test(dd);
		dd->flags |= SHA_FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static int atmel_sha_handle_queue(struct atmel_sha_dev *dd,
				  struct ahash_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct atmel_sha_reqctx *ctx;
	unsigned long flags;
	int err = 0, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ahash_enqueue_request(&dd->queue, req);

	if (SHA_FLAGS_BUSY & dd->flags) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}

	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		dd->flags |= SHA_FLAGS_BUSY;

	spin_unlock_irqrestore(&dd->lock, flags);

	if (!async_req)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ahash_request_cast(async_req);
	dd->req = req;
	ctx = ahash_request_ctx(req);

	dev_dbg(dd->dev, "handling new req, op: %lu, nbytes: %d\n",
						ctx->op, req->nbytes);

	err = atmel_sha_hw_init(dd);

	if (err)
		goto err1;

	if (ctx->op == SHA_OP_UPDATE) {
		err = atmel_sha_update_req(dd);
		if (err != -EINPROGRESS && (ctx->flags & SHA_FLAGS_FINUP)) {
			/* no final() after finup() */
			err = atmel_sha_final_req(dd);
		}
	} else if (ctx->op == SHA_OP_FINAL) {
		err = atmel_sha_final_req(dd);
	}

err1:
	if (err != -EINPROGRESS)
		/* done_task will not finish it, so do it here */
		atmel_sha_finish_req(req, err);

	dev_dbg(dd->dev, "exit, err: %d\n", err);

	return ret;
}

static int atmel_sha_enqueue(struct ahash_request *req, unsigned int op)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct atmel_sha_dev *dd = tctx->dd;

	ctx->op = op;

	return atmel_sha_handle_queue(dd, req);
}

static int atmel_sha_update(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!req->nbytes)
		return 0;

	ctx->total = req->nbytes;
	ctx->sg = req->src;
	ctx->offset = 0;

	if (ctx->flags & SHA_FLAGS_FINUP) {
		if (ctx->bufcnt + ctx->total < ATMEL_SHA_DMA_THRESHOLD)
			/* faster to use CPU for short transfers */
			ctx->flags |= SHA_FLAGS_CPU;
	} else if (ctx->bufcnt + ctx->total < ctx->buflen) {
		atmel_sha_append_sg(ctx);
		return 0;
	}
	return atmel_sha_enqueue(req, SHA_OP_UPDATE);
}

static int atmel_sha_final(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct atmel_sha_dev *dd = tctx->dd;

	int err = 0;

	ctx->flags |= SHA_FLAGS_FINUP;

	if (ctx->flags & SHA_FLAGS_ERROR)
		return 0; /* uncompleted hash is not needed */

	if (ctx->bufcnt) {
		return atmel_sha_enqueue(req, SHA_OP_FINAL);
	} else if (!(ctx->flags & SHA_FLAGS_PAD)) { /* add padding */
		err = atmel_sha_hw_init(dd);
		if (err)
			goto err1;

		dd->flags |= SHA_FLAGS_BUSY;
		err = atmel_sha_final_req(dd);
	} else {
		/* copy ready hash (+ finalize hmac) */
		return atmel_sha_finish(req);
	}

err1:
	if (err != -EINPROGRESS)
		/* done_task will not finish it, so do it here */
		atmel_sha_finish_req(req, err);

	return err;
}

static int atmel_sha_finup(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err1, err2;

	ctx->flags |= SHA_FLAGS_FINUP;

	err1 = atmel_sha_update(req);
	if (err1 == -EINPROGRESS || err1 == -EBUSY)
		return err1;

	/*
	 * final() has to be always called to cleanup resources
	 * even if udpate() failed, except EINPROGRESS
	 */
	err2 = atmel_sha_final(req);

	return err1 ?: err2;
}

static int atmel_sha_digest(struct ahash_request *req)
{
	return atmel_sha_init(req) ?: atmel_sha_finup(req);
}

static int atmel_sha_cra_init_alg(struct crypto_tfm *tfm, const char *alg_base)
{
	struct atmel_sha_ctx *tctx = crypto_tfm_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(tfm);

	/* Allocate a fallback and abort if it failed. */
	tctx->fallback = crypto_alloc_shash(alg_name, 0,
					    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback)) {
		pr_err("atmel-sha: fallback driver '%s' could not be loaded.\n",
				alg_name);
		return PTR_ERR(tctx->fallback);
	}
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct atmel_sha_reqctx) +
				 SHA_BUFFER_LEN + SHA256_BLOCK_SIZE);

	return 0;
}

static int atmel_sha_cra_init(struct crypto_tfm *tfm)
{
	return atmel_sha_cra_init_alg(tfm, NULL);
}

static void atmel_sha_cra_exit(struct crypto_tfm *tfm)
{
	struct atmel_sha_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(tctx->fallback);
	tctx->fallback = NULL;
}

static struct ahash_alg sha_algs[] = {
{
	.init		= atmel_sha_init,
	.update		= atmel_sha_update,
	.final		= atmel_sha_final,
	.finup		= atmel_sha_finup,
	.digest		= atmel_sha_digest,
	.halg = {
		.digestsize	= SHA1_DIGEST_SIZE,
		.base	= {
			.cra_name		= "sha1",
			.cra_driver_name	= "atmel-sha1",
			.cra_priority		= 100,
			.cra_flags		= CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize		= SHA1_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
			.cra_exit		= atmel_sha_cra_exit,
		}
	}
},
{
	.init		= atmel_sha_init,
	.update		= atmel_sha_update,
	.final		= atmel_sha_final,
	.finup		= atmel_sha_finup,
	.digest		= atmel_sha_digest,
	.halg = {
		.digestsize	= SHA256_DIGEST_SIZE,
		.base	= {
			.cra_name		= "sha256",
			.cra_driver_name	= "atmel-sha256",
			.cra_priority		= 100,
			.cra_flags		= CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize		= SHA256_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
			.cra_exit		= atmel_sha_cra_exit,
		}
	}
},
};

static void atmel_sha_done_task(unsigned long data)
{
	struct atmel_sha_dev *dd = (struct atmel_sha_dev *)data;
	int err = 0;

	if (!(SHA_FLAGS_BUSY & dd->flags)) {
		atmel_sha_handle_queue(dd, NULL);
		return;
	}

	if (SHA_FLAGS_CPU & dd->flags) {
		if (SHA_FLAGS_OUTPUT_READY & dd->flags) {
			dd->flags &= ~SHA_FLAGS_OUTPUT_READY;
			goto finish;
		}
	} else if (SHA_FLAGS_DMA_READY & dd->flags) {
		if (SHA_FLAGS_DMA_ACTIVE & dd->flags) {
			dd->flags &= ~SHA_FLAGS_DMA_ACTIVE;
			atmel_sha_update_dma_stop(dd);
			if (dd->err) {
				err = dd->err;
				goto finish;
			}
		}
		if (SHA_FLAGS_OUTPUT_READY & dd->flags) {
			/* hash or semi-hash ready */
			dd->flags &= ~(SHA_FLAGS_DMA_READY |
						SHA_FLAGS_OUTPUT_READY);
			err = atmel_sha_update_dma_start(dd);
			if (err != -EINPROGRESS)
				goto finish;
		}
	}
	return;

finish:
	/* finish curent request */
	atmel_sha_finish_req(dd->req, err);
}

static irqreturn_t atmel_sha_irq(int irq, void *dev_id)
{
	struct atmel_sha_dev *sha_dd = dev_id;
	u32 reg;

	reg = atmel_sha_read(sha_dd, SHA_ISR);
	if (reg & atmel_sha_read(sha_dd, SHA_IMR)) {
		atmel_sha_write(sha_dd, SHA_IDR, reg);
		if (SHA_FLAGS_BUSY & sha_dd->flags) {
			sha_dd->flags |= SHA_FLAGS_OUTPUT_READY;
			if (!(SHA_FLAGS_CPU & sha_dd->flags))
				sha_dd->flags |= SHA_FLAGS_DMA_READY;
			tasklet_schedule(&sha_dd->done_task);
		} else {
			dev_warn(sha_dd->dev, "SHA interrupt when no active requests.\n");
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void atmel_sha_unregister_algs(struct atmel_sha_dev *dd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sha_algs); i++)
		crypto_unregister_ahash(&sha_algs[i]);
}

static int atmel_sha_register_algs(struct atmel_sha_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(sha_algs); i++) {
		err = crypto_register_ahash(&sha_algs[i]);
		if (err)
			goto err_sha_algs;
	}

	return 0;

err_sha_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_algs[j]);

	return err;
}

static int __devinit atmel_sha_probe(struct platform_device *pdev)
{
	struct atmel_sha_dev *sha_dd;
	struct device *dev = &pdev->dev;
	struct resource *sha_res;
	unsigned long sha_phys_size;
	int err;

	sha_dd = kzalloc(sizeof(struct atmel_sha_dev), GFP_KERNEL);
	if (sha_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto sha_dd_err;
	}

	sha_dd->dev = dev;

	platform_set_drvdata(pdev, sha_dd);

	INIT_LIST_HEAD(&sha_dd->list);

	tasklet_init(&sha_dd->done_task, atmel_sha_done_task,
					(unsigned long)sha_dd);

	crypto_init_queue(&sha_dd->queue, ATMEL_SHA_QUEUE_LENGTH);

	sha_dd->irq = -1;

	/* Get the base address */
	sha_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!sha_res) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto res_err;
	}
	sha_dd->phys_base = sha_res->start;
	sha_phys_size = resource_size(sha_res);

	/* Get the IRQ */
	sha_dd->irq = platform_get_irq(pdev,  0);
	if (sha_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = sha_dd->irq;
		goto res_err;
	}

	err = request_irq(sha_dd->irq, atmel_sha_irq, IRQF_SHARED, "atmel-sha",
						sha_dd);
	if (err) {
		dev_err(dev, "unable to request sha irq.\n");
		goto res_err;
	}

	/* Initializing the clock */
	sha_dd->iclk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sha_dd->iclk)) {
		dev_err(dev, "clock intialization failed.\n");
		err = PTR_ERR(sha_dd->iclk);
		goto clk_err;
	}

	sha_dd->io_base = ioremap(sha_dd->phys_base, sha_phys_size);
	if (!sha_dd->io_base) {
		dev_err(dev, "can't ioremap\n");
		err = -ENOMEM;
		goto sha_io_err;
	}

	spin_lock(&atmel_sha.lock);
	list_add_tail(&sha_dd->list, &atmel_sha.dev_list);
	spin_unlock(&atmel_sha.lock);

	err = atmel_sha_register_algs(sha_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel SHA1/SHA256\n");

	return 0;

err_algs:
	spin_lock(&atmel_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&atmel_sha.lock);
	iounmap(sha_dd->io_base);
sha_io_err:
	clk_put(sha_dd->iclk);
clk_err:
	free_irq(sha_dd->irq, sha_dd);
res_err:
	tasklet_kill(&sha_dd->done_task);
	kfree(sha_dd);
	sha_dd = NULL;
sha_dd_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int __devexit atmel_sha_remove(struct platform_device *pdev)
{
	static struct atmel_sha_dev *sha_dd;

	sha_dd = platform_get_drvdata(pdev);
	if (!sha_dd)
		return -ENODEV;
	spin_lock(&atmel_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&atmel_sha.lock);

	atmel_sha_unregister_algs(sha_dd);

	tasklet_kill(&sha_dd->done_task);

	iounmap(sha_dd->io_base);

	clk_put(sha_dd->iclk);

	if (sha_dd->irq >= 0)
		free_irq(sha_dd->irq, sha_dd);

	kfree(sha_dd);
	sha_dd = NULL;

	return 0;
}

static struct platform_driver atmel_sha_driver = {
	.probe		= atmel_sha_probe,
	.remove		= __devexit_p(atmel_sha_remove),
	.driver		= {
		.name	= "atmel_sha",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(atmel_sha_driver);

MODULE_DESCRIPTION("Atmel SHA1/SHA256 hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
