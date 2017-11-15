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
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <linux/platform_data/crypto-atmel.h>
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
#define SHA_FLAGS_SHA224	BIT(19)
#define SHA_FLAGS_SHA256	BIT(20)
#define SHA_FLAGS_SHA384	BIT(21)
#define SHA_FLAGS_SHA512	BIT(22)
#define SHA_FLAGS_ERROR		BIT(23)
#define SHA_FLAGS_PAD		BIT(24)

#define SHA_OP_UPDATE	1
#define SHA_OP_FINAL	2

#define SHA_BUFFER_LEN		PAGE_SIZE

#define ATMEL_SHA_DMA_THRESHOLD		56

struct atmel_sha_caps {
	bool	has_dma;
	bool	has_dualbuff;
	bool	has_sha224;
	bool	has_sha_384_512;
};

struct atmel_sha_dev;

struct atmel_sha_reqctx {
	struct atmel_sha_dev	*dd;
	unsigned long	flags;
	unsigned long	op;

	u8	digest[SHA512_DIGEST_SIZE] __aligned(sizeof(u32));
	u64	digcnt[2];
	size_t	bufcnt;
	size_t	buflen;
	dma_addr_t	dma_addr;

	/* walk state */
	struct scatterlist	*sg;
	unsigned int	offset;	/* offset in current sg */
	unsigned int	total;	/* total request */

	size_t block_size;

	u8	buffer[0] __aligned(sizeof(u32));
};

struct atmel_sha_ctx {
	struct atmel_sha_dev	*dd;

	unsigned long		flags;
};

#define ATMEL_SHA_QUEUE_LENGTH	50

struct atmel_sha_dma {
	struct dma_chan			*chan;
	struct dma_slave_config dma_conf;
};

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

	struct atmel_sha_dma	dma_lch_in;

	struct atmel_sha_caps	caps;

	u32	hw_version;
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

static size_t atmel_sha_append_sg(struct atmel_sha_reqctx *ctx)
{
	size_t count;

	while ((ctx->bufcnt < ctx->buflen) && ctx->total) {
		count = min(ctx->sg->length - ctx->offset, ctx->total);
		count = min(count, ctx->buflen - ctx->bufcnt);

		if (count <= 0) {
			/*
			* Check if count <= 0 because the buffer is full or
			* because the sg length is 0. In the latest case,
			* check if there is another sg in the list, a 0 length
			* sg doesn't necessarily mean the end of the sg list.
			*/
			if ((ctx->sg->length == 0) && !sg_is_last(ctx->sg)) {
				ctx->sg = sg_next(ctx->sg);
				continue;
			} else {
				break;
			}
		}

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
 * The purpose of this padding is to ensure that the padded message is a
 * multiple of 512 bits (SHA1/SHA224/SHA256) or 1024 bits (SHA384/SHA512).
 * The bit "1" is appended at the end of the message followed by
 * "padlen-1" zero bits. Then a 64 bits block (SHA1/SHA224/SHA256) or
 * 128 bits block (SHA384/SHA512) equals to the message length in bits
 * is appended.
 *
 * For SHA1/SHA224/SHA256, padlen is calculated as followed:
 *  - if message length < 56 bytes then padlen = 56 - message length
 *  - else padlen = 64 + 56 - message length
 *
 * For SHA384/SHA512, padlen is calculated as followed:
 *  - if message length < 112 bytes then padlen = 112 - message length
 *  - else padlen = 128 + 112 - message length
 */
static void atmel_sha_fill_padding(struct atmel_sha_reqctx *ctx, int length)
{
	unsigned int index, padlen;
	u64 bits[2];
	u64 size[2];

	size[0] = ctx->digcnt[0];
	size[1] = ctx->digcnt[1];

	size[0] += ctx->bufcnt;
	if (size[0] < ctx->bufcnt)
		size[1]++;

	size[0] += length;
	if (size[0]  < length)
		size[1]++;

	bits[1] = cpu_to_be64(size[0] << 3);
	bits[0] = cpu_to_be64(size[1] << 3 | size[0] >> 61);

	if (ctx->flags & (SHA_FLAGS_SHA384 | SHA_FLAGS_SHA512)) {
		index = ctx->bufcnt & 0x7f;
		padlen = (index < 112) ? (112 - index) : ((128+112) - index);
		*(ctx->buffer + ctx->bufcnt) = 0x80;
		memset(ctx->buffer + ctx->bufcnt + 1, 0, padlen-1);
		memcpy(ctx->buffer + ctx->bufcnt + padlen, bits, 16);
		ctx->bufcnt += padlen + 16;
		ctx->flags |= SHA_FLAGS_PAD;
	} else {
		index = ctx->bufcnt & 0x3f;
		padlen = (index < 56) ? (56 - index) : ((64+56) - index);
		*(ctx->buffer + ctx->bufcnt) = 0x80;
		memset(ctx->buffer + ctx->bufcnt + 1, 0, padlen-1);
		memcpy(ctx->buffer + ctx->bufcnt + padlen, &bits[1], 8);
		ctx->bufcnt += padlen + 8;
		ctx->flags |= SHA_FLAGS_PAD;
	}
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

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA1_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA1;
		ctx->block_size = SHA1_BLOCK_SIZE;
		break;
	case SHA224_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA224;
		ctx->block_size = SHA224_BLOCK_SIZE;
		break;
	case SHA256_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA256;
		ctx->block_size = SHA256_BLOCK_SIZE;
		break;
	case SHA384_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA384;
		ctx->block_size = SHA384_BLOCK_SIZE;
		break;
	case SHA512_DIGEST_SIZE:
		ctx->flags |= SHA_FLAGS_SHA512;
		ctx->block_size = SHA512_BLOCK_SIZE;
		break;
	default:
		return -EINVAL;
		break;
	}

	ctx->bufcnt = 0;
	ctx->digcnt[0] = 0;
	ctx->digcnt[1] = 0;
	ctx->buflen = SHA_BUFFER_LEN;

	return 0;
}

static void atmel_sha_write_ctrl(struct atmel_sha_dev *dd, int dma)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	u32 valcr = 0, valmr = SHA_MR_MODE_AUTO;

	if (likely(dma)) {
		if (!dd->caps.has_dma)
			atmel_sha_write(dd, SHA_IER, SHA_INT_TXBUFE);
		valmr = SHA_MR_MODE_PDC;
		if (dd->caps.has_dualbuff)
			valmr |= SHA_MR_DUALBUFF;
	} else {
		atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
	}

	if (ctx->flags & SHA_FLAGS_SHA1)
		valmr |= SHA_MR_ALGO_SHA1;
	else if (ctx->flags & SHA_FLAGS_SHA224)
		valmr |= SHA_MR_ALGO_SHA224;
	else if (ctx->flags & SHA_FLAGS_SHA256)
		valmr |= SHA_MR_ALGO_SHA256;
	else if (ctx->flags & SHA_FLAGS_SHA384)
		valmr |= SHA_MR_ALGO_SHA384;
	else if (ctx->flags & SHA_FLAGS_SHA512)
		valmr |= SHA_MR_ALGO_SHA512;

	/* Setting CR_FIRST only for the first iteration */
	if (!(ctx->digcnt[0] || ctx->digcnt[1]))
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

	dev_dbg(dd->dev, "xmit_cpu: digcnt: 0x%llx 0x%llx, length: %d, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length, final);

	atmel_sha_write_ctrl(dd, 0);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length;
	if (ctx->digcnt[0] < length)
		ctx->digcnt[1]++;

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

	dev_dbg(dd->dev, "xmit_pdc: digcnt: 0x%llx 0x%llx, length: %d, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length1, final);

	len32 = DIV_ROUND_UP(length1, sizeof(u32));
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTDIS);
	atmel_sha_write(dd, SHA_TPR, dma_addr1);
	atmel_sha_write(dd, SHA_TCR, len32);

	len32 = DIV_ROUND_UP(length2, sizeof(u32));
	atmel_sha_write(dd, SHA_TNPR, dma_addr2);
	atmel_sha_write(dd, SHA_TNCR, len32);

	atmel_sha_write_ctrl(dd, 1);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length1;
	if (ctx->digcnt[0] < length1)
		ctx->digcnt[1]++;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	dd->flags |=  SHA_FLAGS_DMA_ACTIVE;

	/* Start DMA transfer */
	atmel_sha_write(dd, SHA_PTCR, SHA_PTCR_TXTEN);

	return -EINPROGRESS;
}

static void atmel_sha_dma_callback(void *data)
{
	struct atmel_sha_dev *dd = data;

	/* dma_lch_in - completed - wait DATRDY */
	atmel_sha_write(dd, SHA_IER, SHA_INT_DATARDY);
}

static int atmel_sha_xmit_dma(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	struct dma_async_tx_descriptor	*in_desc;
	struct scatterlist sg[2];

	dev_dbg(dd->dev, "xmit_dma: digcnt: 0x%llx 0x%llx, length: %d, final: %d\n",
		ctx->digcnt[1], ctx->digcnt[0], length1, final);

	dd->dma_lch_in.dma_conf.src_maxburst = 16;
	dd->dma_lch_in.dma_conf.dst_maxburst = 16;

	dmaengine_slave_config(dd->dma_lch_in.chan, &dd->dma_lch_in.dma_conf);

	if (length2) {
		sg_init_table(sg, 2);
		sg_dma_address(&sg[0]) = dma_addr1;
		sg_dma_len(&sg[0]) = length1;
		sg_dma_address(&sg[1]) = dma_addr2;
		sg_dma_len(&sg[1]) = length2;
		in_desc = dmaengine_prep_slave_sg(dd->dma_lch_in.chan, sg, 2,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	} else {
		sg_init_table(sg, 1);
		sg_dma_address(&sg[0]) = dma_addr1;
		sg_dma_len(&sg[0]) = length1;
		in_desc = dmaengine_prep_slave_sg(dd->dma_lch_in.chan, sg, 1,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}
	if (!in_desc)
		return -EINVAL;

	in_desc->callback = atmel_sha_dma_callback;
	in_desc->callback_param = dd;

	atmel_sha_write_ctrl(dd, 1);

	/* should be non-zero before next lines to disable clocks later */
	ctx->digcnt[0] += length1;
	if (ctx->digcnt[0] < length1)
		ctx->digcnt[1]++;

	if (final)
		dd->flags |= SHA_FLAGS_FINAL; /* catch last interrupt */

	dd->flags |=  SHA_FLAGS_DMA_ACTIVE;

	/* Start DMA transfer */
	dmaengine_submit(in_desc);
	dma_async_issue_pending(dd->dma_lch_in.chan);

	return -EINPROGRESS;
}

static int atmel_sha_xmit_start(struct atmel_sha_dev *dd, dma_addr_t dma_addr1,
		size_t length1, dma_addr_t dma_addr2, size_t length2, int final)
{
	if (dd->caps.has_dma)
		return atmel_sha_xmit_dma(dd, dma_addr1, length1,
				dma_addr2, length2, final);
	else
		return atmel_sha_xmit_pdc(dd, dma_addr1, length1,
				dma_addr2, length2, final);
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
				ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
		dev_err(dd->dev, "dma %u bytes error\n", ctx->buflen +
				ctx->block_size);
		return -EINVAL;
	}

	ctx->flags &= ~SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_start(dd, ctx->dma_addr, length, 0, 0, final);
}

static int atmel_sha_update_dma_slow(struct atmel_sha_dev *dd)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(dd->req);
	unsigned int final;
	size_t count;

	atmel_sha_append_sg(ctx);

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	dev_dbg(dd->dev, "slow: bufcnt: %u, digcnt: 0x%llx 0x%llx, final: %d\n",
		 ctx->bufcnt, ctx->digcnt[1], ctx->digcnt[0], final);

	if (final)
		atmel_sha_fill_padding(ctx, 0);

	if (final || (ctx->bufcnt == ctx->buflen)) {
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

	dev_dbg(dd->dev, "fast: digcnt: 0x%llx 0x%llx, bufcnt: %u, total: %u\n",
		ctx->digcnt[1], ctx->digcnt[0], ctx->bufcnt, ctx->total);

	sg = ctx->sg;

	if (!IS_ALIGNED(sg->offset, sizeof(u32)))
		return atmel_sha_update_dma_slow(dd);

	if (!sg_is_last(sg) && !IS_ALIGNED(sg->length, ctx->block_size))
		/* size is not ctx->block_size aligned */
		return atmel_sha_update_dma_slow(dd);

	length = min(ctx->total, sg->length);

	if (sg_is_last(sg)) {
		if (!(ctx->flags & SHA_FLAGS_FINUP)) {
			/* not last sg must be ctx->block_size aligned */
			tail = length & (ctx->block_size - 1);
			length -= tail;
		}
	}

	ctx->total -= length;
	ctx->offset = length; /* offset where to start slow */

	final = (ctx->flags & SHA_FLAGS_FINUP) && !ctx->total;

	/* Add padding */
	if (final) {
		tail = length & (ctx->block_size - 1);
		length -= tail;
		ctx->total += tail;
		ctx->offset = length; /* offset where to start slow */

		sg = ctx->sg;
		atmel_sha_append_sg(ctx);

		atmel_sha_fill_padding(ctx, length);

		ctx->dma_addr = dma_map_single(dd->dev, ctx->buffer,
			ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
		if (dma_mapping_error(dd->dev, ctx->dma_addr)) {
			dev_err(dd->dev, "dma %u bytes error\n",
				ctx->buflen + ctx->block_size);
			return -EINVAL;
		}

		if (length == 0) {
			ctx->flags &= ~SHA_FLAGS_SG;
			count = ctx->bufcnt;
			ctx->bufcnt = 0;
			return atmel_sha_xmit_start(dd, ctx->dma_addr, count, 0,
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
			return atmel_sha_xmit_start(dd, sg_dma_address(ctx->sg),
					length, ctx->dma_addr, count, final);
		}
	}

	if (!dma_map_sg(dd->dev, ctx->sg, 1, DMA_TO_DEVICE)) {
		dev_err(dd->dev, "dma_map_sg  error\n");
		return -EINVAL;
	}

	ctx->flags |= SHA_FLAGS_SG;

	/* next call does not fail... so no unmap in the case of error */
	return atmel_sha_xmit_start(dd, sg_dma_address(ctx->sg), length, 0,
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
		if (ctx->flags & SHA_FLAGS_PAD) {
			dma_unmap_single(dd->dev, ctx->dma_addr,
				ctx->buflen + ctx->block_size, DMA_TO_DEVICE);
		}
	} else {
		dma_unmap_single(dd->dev, ctx->dma_addr, ctx->buflen +
						ctx->block_size, DMA_TO_DEVICE);
	}

	return 0;
}

static int atmel_sha_update_req(struct atmel_sha_dev *dd)
{
	struct ahash_request *req = dd->req;
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	int err;

	dev_dbg(dd->dev, "update_req: total: %u, digcnt: 0x%llx 0x%llx\n",
		ctx->total, ctx->digcnt[1], ctx->digcnt[0]);

	if (ctx->flags & SHA_FLAGS_CPU)
		err = atmel_sha_update_cpu(dd);
	else
		err = atmel_sha_update_dma_start(dd);

	/* wait for dma completion before can take more data */
	dev_dbg(dd->dev, "update: err: %d, digcnt: 0x%llx 0%llx\n",
			err, ctx->digcnt[1], ctx->digcnt[0]);

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

	if (ctx->flags & SHA_FLAGS_SHA1)
		for (i = 0; i < SHA1_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	else if (ctx->flags & SHA_FLAGS_SHA224)
		for (i = 0; i < SHA224_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	else if (ctx->flags & SHA_FLAGS_SHA256)
		for (i = 0; i < SHA256_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	else if (ctx->flags & SHA_FLAGS_SHA384)
		for (i = 0; i < SHA384_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
	else
		for (i = 0; i < SHA512_DIGEST_SIZE / sizeof(u32); i++)
			hash[i] = atmel_sha_read(ctx->dd, SHA_REG_DIGEST(i));
}

static void atmel_sha_copy_ready_hash(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);

	if (!req->result)
		return;

	if (ctx->flags & SHA_FLAGS_SHA1)
		memcpy(req->result, ctx->digest, SHA1_DIGEST_SIZE);
	else if (ctx->flags & SHA_FLAGS_SHA224)
		memcpy(req->result, ctx->digest, SHA224_DIGEST_SIZE);
	else if (ctx->flags & SHA_FLAGS_SHA256)
		memcpy(req->result, ctx->digest, SHA256_DIGEST_SIZE);
	else if (ctx->flags & SHA_FLAGS_SHA384)
		memcpy(req->result, ctx->digest, SHA384_DIGEST_SIZE);
	else
		memcpy(req->result, ctx->digest, SHA512_DIGEST_SIZE);
}

static int atmel_sha_finish(struct ahash_request *req)
{
	struct atmel_sha_reqctx *ctx = ahash_request_ctx(req);
	struct atmel_sha_dev *dd = ctx->dd;
	int err = 0;

	if (ctx->digcnt[0] || ctx->digcnt[1])
		atmel_sha_copy_ready_hash(req);

	dev_dbg(dd->dev, "digcnt: 0x%llx 0x%llx, bufcnt: %d\n", ctx->digcnt[1],
		ctx->digcnt[0], ctx->bufcnt);

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

	clk_disable(dd->iclk);

	if (req->base.complete)
		req->base.complete(&req->base, err);

	/* handle new request */
	tasklet_schedule(&dd->done_task);
}

static int atmel_sha_hw_init(struct atmel_sha_dev *dd)
{
	int err;

	err = clk_enable(dd->iclk);
	if (err)
		return err;

	if (!(SHA_FLAGS_INIT & dd->flags)) {
		atmel_sha_write(dd, SHA_CR, SHA_CR_SWRST);
		dd->flags |= SHA_FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static inline unsigned int atmel_sha_get_version(struct atmel_sha_dev *dd)
{
	return atmel_sha_read(dd, SHA_HW_VERSION) & 0x00000fff;
}

static void atmel_sha_hw_version_init(struct atmel_sha_dev *dd)
{
	atmel_sha_hw_init(dd);

	dd->hw_version = atmel_sha_get_version(dd);

	dev_info(dd->dev,
			"version: 0x%x\n", dd->hw_version);

	clk_disable(dd->iclk);
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
		if (err != -EINPROGRESS && (ctx->flags & SHA_FLAGS_FINUP))
			/* no final() after finup() */
			err = atmel_sha_final_req(dd);
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
	if (err1 == -EINPROGRESS ||
	    (err1 == -EBUSY && (ahash_request_flags(req) &
				CRYPTO_TFM_REQ_MAY_BACKLOG)))
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

static int atmel_sha_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct atmel_sha_reqctx) +
				 SHA_BUFFER_LEN + SHA512_BLOCK_SIZE);

	return 0;
}

static struct ahash_alg sha_1_256_algs[] = {
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
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA1_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
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
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA256_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
		}
	}
},
};

static struct ahash_alg sha_224_alg = {
	.init		= atmel_sha_init,
	.update		= atmel_sha_update,
	.final		= atmel_sha_final,
	.finup		= atmel_sha_finup,
	.digest		= atmel_sha_digest,
	.halg = {
		.digestsize	= SHA224_DIGEST_SIZE,
		.base	= {
			.cra_name		= "sha224",
			.cra_driver_name	= "atmel-sha224",
			.cra_priority		= 100,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA224_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
		}
	}
};

static struct ahash_alg sha_384_512_algs[] = {
{
	.init		= atmel_sha_init,
	.update		= atmel_sha_update,
	.final		= atmel_sha_final,
	.finup		= atmel_sha_finup,
	.digest		= atmel_sha_digest,
	.halg = {
		.digestsize	= SHA384_DIGEST_SIZE,
		.base	= {
			.cra_name		= "sha384",
			.cra_driver_name	= "atmel-sha384",
			.cra_priority		= 100,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA384_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0x3,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
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
		.digestsize	= SHA512_DIGEST_SIZE,
		.base	= {
			.cra_name		= "sha512",
			.cra_driver_name	= "atmel-sha512",
			.cra_priority		= 100,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA512_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct atmel_sha_ctx),
			.cra_alignmask		= 0x3,
			.cra_module		= THIS_MODULE,
			.cra_init		= atmel_sha_cra_init,
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

	for (i = 0; i < ARRAY_SIZE(sha_1_256_algs); i++)
		crypto_unregister_ahash(&sha_1_256_algs[i]);

	if (dd->caps.has_sha224)
		crypto_unregister_ahash(&sha_224_alg);

	if (dd->caps.has_sha_384_512) {
		for (i = 0; i < ARRAY_SIZE(sha_384_512_algs); i++)
			crypto_unregister_ahash(&sha_384_512_algs[i]);
	}
}

static int atmel_sha_register_algs(struct atmel_sha_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(sha_1_256_algs); i++) {
		err = crypto_register_ahash(&sha_1_256_algs[i]);
		if (err)
			goto err_sha_1_256_algs;
	}

	if (dd->caps.has_sha224) {
		err = crypto_register_ahash(&sha_224_alg);
		if (err)
			goto err_sha_224_algs;
	}

	if (dd->caps.has_sha_384_512) {
		for (i = 0; i < ARRAY_SIZE(sha_384_512_algs); i++) {
			err = crypto_register_ahash(&sha_384_512_algs[i]);
			if (err)
				goto err_sha_384_512_algs;
		}
	}

	return 0;

err_sha_384_512_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_384_512_algs[j]);
	crypto_unregister_ahash(&sha_224_alg);
err_sha_224_algs:
	i = ARRAY_SIZE(sha_1_256_algs);
err_sha_1_256_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_ahash(&sha_1_256_algs[j]);

	return err;
}

static bool atmel_sha_filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave	*sl = slave;

	if (sl && sl->dma_dev == chan->device->dev) {
		chan->private = sl;
		return true;
	} else {
		return false;
	}
}

static int atmel_sha_dma_init(struct atmel_sha_dev *dd,
				struct crypto_platform_data *pdata)
{
	int err = -ENOMEM;
	dma_cap_mask_t mask_in;

	/* Try to grab DMA channel */
	dma_cap_zero(mask_in);
	dma_cap_set(DMA_SLAVE, mask_in);

	dd->dma_lch_in.chan = dma_request_slave_channel_compat(mask_in,
			atmel_sha_filter, &pdata->dma_slave->rxdata, dd->dev, "tx");
	if (!dd->dma_lch_in.chan) {
		dev_warn(dd->dev, "no DMA channel available\n");
		return err;
	}

	dd->dma_lch_in.dma_conf.direction = DMA_MEM_TO_DEV;
	dd->dma_lch_in.dma_conf.dst_addr = dd->phys_base +
		SHA_REG_DIN(0);
	dd->dma_lch_in.dma_conf.src_maxburst = 1;
	dd->dma_lch_in.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.dst_maxburst = 1;
	dd->dma_lch_in.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.device_fc = false;

	return 0;
}

static void atmel_sha_dma_cleanup(struct atmel_sha_dev *dd)
{
	dma_release_channel(dd->dma_lch_in.chan);
}

static void atmel_sha_get_cap(struct atmel_sha_dev *dd)
{

	dd->caps.has_dma = 0;
	dd->caps.has_dualbuff = 0;
	dd->caps.has_sha224 = 0;
	dd->caps.has_sha_384_512 = 0;

	/* keep only major version number */
	switch (dd->hw_version & 0xff0) {
	case 0x420:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		dd->caps.has_sha_384_512 = 1;
		break;
	case 0x410:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		dd->caps.has_sha_384_512 = 1;
		break;
	case 0x400:
		dd->caps.has_dma = 1;
		dd->caps.has_dualbuff = 1;
		dd->caps.has_sha224 = 1;
		break;
	case 0x320:
		break;
	default:
		dev_warn(dd->dev,
				"Unmanaged sha version, set minimum capabilities\n");
		break;
	}
}

#if defined(CONFIG_OF)
static const struct of_device_id atmel_sha_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-sha" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_sha_dt_ids);

static struct crypto_platform_data *atmel_sha_of_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct crypto_platform_data *pdata;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	pdata->dma_slave = devm_kzalloc(&pdev->dev,
					sizeof(*(pdata->dma_slave)),
					GFP_KERNEL);
	if (!pdata->dma_slave) {
		dev_err(&pdev->dev, "could not allocate memory for dma_slave\n");
		return ERR_PTR(-ENOMEM);
	}

	return pdata;
}
#else /* CONFIG_OF */
static inline struct crypto_platform_data *atmel_sha_of_init(struct platform_device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int atmel_sha_probe(struct platform_device *pdev)
{
	struct atmel_sha_dev *sha_dd;
	struct crypto_platform_data	*pdata;
	struct device *dev = &pdev->dev;
	struct resource *sha_res;
	int err;

	sha_dd = devm_kzalloc(&pdev->dev, sizeof(*sha_dd), GFP_KERNEL);
	if (sha_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto sha_dd_err;
	}

	sha_dd->dev = dev;

	platform_set_drvdata(pdev, sha_dd);

	INIT_LIST_HEAD(&sha_dd->list);
	spin_lock_init(&sha_dd->lock);

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

	/* Get the IRQ */
	sha_dd->irq = platform_get_irq(pdev,  0);
	if (sha_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = sha_dd->irq;
		goto res_err;
	}

	err = devm_request_irq(&pdev->dev, sha_dd->irq, atmel_sha_irq,
			       IRQF_SHARED, "atmel-sha", sha_dd);
	if (err) {
		dev_err(dev, "unable to request sha irq.\n");
		goto res_err;
	}

	/* Initializing the clock */
	sha_dd->iclk = devm_clk_get(&pdev->dev, "sha_clk");
	if (IS_ERR(sha_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(sha_dd->iclk);
		goto res_err;
	}

	sha_dd->io_base = devm_ioremap_resource(&pdev->dev, sha_res);
	if (IS_ERR(sha_dd->io_base)) {
		dev_err(dev, "can't ioremap\n");
		err = PTR_ERR(sha_dd->io_base);
		goto res_err;
	}

	err = clk_prepare(sha_dd->iclk);
	if (err)
		goto res_err;

	atmel_sha_hw_version_init(sha_dd);

	atmel_sha_get_cap(sha_dd);

	if (sha_dd->caps.has_dma) {
		pdata = pdev->dev.platform_data;
		if (!pdata) {
			pdata = atmel_sha_of_init(pdev);
			if (IS_ERR(pdata)) {
				dev_err(&pdev->dev, "platform data not available\n");
				err = PTR_ERR(pdata);
				goto iclk_unprepare;
			}
		}
		if (!pdata->dma_slave) {
			err = -ENXIO;
			goto iclk_unprepare;
		}
		err = atmel_sha_dma_init(sha_dd, pdata);
		if (err)
			goto err_sha_dma;

		dev_info(dev, "using %s for DMA transfers\n",
				dma_chan_name(sha_dd->dma_lch_in.chan));
	}

	spin_lock(&atmel_sha.lock);
	list_add_tail(&sha_dd->list, &atmel_sha.dev_list);
	spin_unlock(&atmel_sha.lock);

	err = atmel_sha_register_algs(sha_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel SHA1/SHA256%s%s\n",
			sha_dd->caps.has_sha224 ? "/SHA224" : "",
			sha_dd->caps.has_sha_384_512 ? "/SHA384/SHA512" : "");

	return 0;

err_algs:
	spin_lock(&atmel_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&atmel_sha.lock);
	if (sha_dd->caps.has_dma)
		atmel_sha_dma_cleanup(sha_dd);
err_sha_dma:
iclk_unprepare:
	clk_unprepare(sha_dd->iclk);
res_err:
	tasklet_kill(&sha_dd->done_task);
sha_dd_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int atmel_sha_remove(struct platform_device *pdev)
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

	if (sha_dd->caps.has_dma)
		atmel_sha_dma_cleanup(sha_dd);

	clk_unprepare(sha_dd->iclk);

	return 0;
}

static struct platform_driver atmel_sha_driver = {
	.probe		= atmel_sha_probe,
	.remove		= atmel_sha_remove,
	.driver		= {
		.name	= "atmel_sha",
		.of_match_table	= of_match_ptr(atmel_sha_dt_ids),
	},
};

module_platform_driver(atmel_sha_driver);

MODULE_DESCRIPTION("Atmel SHA (1/256/224/384/512) hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
