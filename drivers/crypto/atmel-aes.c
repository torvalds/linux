/*
 * Cryptographic API.
 *
 * Support for ATMEL AES HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Some ideas are from omap-aes.c driver.
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
#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <linux/platform_data/crypto-atmel.h>
#include <dt-bindings/dma/at91.h>
#include "atmel-aes-regs.h"

#define CFB8_BLOCK_SIZE		1
#define CFB16_BLOCK_SIZE	2
#define CFB32_BLOCK_SIZE	4
#define CFB64_BLOCK_SIZE	8

/* AES flags */
#define AES_FLAGS_MODE_MASK	0x03ff
#define AES_FLAGS_ENCRYPT	BIT(0)
#define AES_FLAGS_CBC		BIT(1)
#define AES_FLAGS_CFB		BIT(2)
#define AES_FLAGS_CFB8		BIT(3)
#define AES_FLAGS_CFB16		BIT(4)
#define AES_FLAGS_CFB32		BIT(5)
#define AES_FLAGS_CFB64		BIT(6)
#define AES_FLAGS_CFB128	BIT(7)
#define AES_FLAGS_OFB		BIT(8)
#define AES_FLAGS_CTR		BIT(9)

#define AES_FLAGS_INIT		BIT(16)
#define AES_FLAGS_DMA		BIT(17)
#define AES_FLAGS_BUSY		BIT(18)
#define AES_FLAGS_FAST		BIT(19)

#define ATMEL_AES_QUEUE_LENGTH	50

#define ATMEL_AES_DMA_THRESHOLD		16


struct atmel_aes_caps {
	bool	has_dualbuff;
	bool	has_cfb64;
	u32		max_burst_size;
};

struct atmel_aes_dev;

struct atmel_aes_ctx {
	struct atmel_aes_dev *dd;

	int		keylen;
	u32		key[AES_KEYSIZE_256 / sizeof(u32)];

	u16		block_size;
};

struct atmel_aes_reqctx {
	unsigned long mode;
};

struct atmel_aes_dma {
	struct dma_chan			*chan;
	struct dma_slave_config dma_conf;
};

struct atmel_aes_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;

	struct atmel_aes_ctx	*ctx;
	struct device		*dev;
	struct clk		*iclk;
	int	irq;

	unsigned long		flags;
	int	err;

	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct ablkcipher_request	*req;
	size_t	total;

	struct scatterlist	*in_sg;
	unsigned int		nb_in_sg;
	size_t				in_offset;
	struct scatterlist	*out_sg;
	unsigned int		nb_out_sg;
	size_t				out_offset;

	size_t	bufcnt;
	size_t	buflen;
	size_t	dma_size;

	void	*buf_in;
	int		dma_in;
	dma_addr_t	dma_addr_in;
	struct atmel_aes_dma	dma_lch_in;

	void	*buf_out;
	int		dma_out;
	dma_addr_t	dma_addr_out;
	struct atmel_aes_dma	dma_lch_out;

	struct atmel_aes_caps	caps;

	u32	hw_version;
};

struct atmel_aes_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
};

static struct atmel_aes_drv atmel_aes = {
	.dev_list = LIST_HEAD_INIT(atmel_aes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(atmel_aes.lock),
};

static int atmel_aes_sg_length(struct ablkcipher_request *req,
			struct scatterlist *sg)
{
	unsigned int total = req->nbytes;
	int sg_nb;
	unsigned int len;
	struct scatterlist *sg_list;

	sg_nb = 0;
	sg_list = sg;
	total = req->nbytes;

	while (total) {
		len = min(sg_list->length, total);

		sg_nb++;
		total -= len;

		sg_list = sg_next(sg_list);
		if (!sg_list)
			total = 0;
	}

	return sg_nb;
}

static int atmel_aes_sg_copy(struct scatterlist **sg, size_t *offset,
			void *buf, size_t buflen, size_t total, int out)
{
	unsigned int count, off = 0;

	while (buflen && total) {
		count = min((*sg)->length - *offset, total);
		count = min(count, buflen);

		if (!count)
			return off;

		scatterwalk_map_and_copy(buf + off, *sg, *offset, count, out);

		off += count;
		buflen -= count;
		*offset += count;
		total -= count;

		if (*offset == (*sg)->length) {
			*sg = sg_next(*sg);
			if (*sg)
				*offset = 0;
			else
				total = 0;
		}
	}

	return off;
}

static inline u32 atmel_aes_read(struct atmel_aes_dev *dd, u32 offset)
{
	return readl_relaxed(dd->io_base + offset);
}

static inline void atmel_aes_write(struct atmel_aes_dev *dd,
					u32 offset, u32 value)
{
	writel_relaxed(value, dd->io_base + offset);
}

static void atmel_aes_read_n(struct atmel_aes_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		*value = atmel_aes_read(dd, offset);
}

static void atmel_aes_write_n(struct atmel_aes_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		atmel_aes_write(dd, offset, *value);
}

static struct atmel_aes_dev *atmel_aes_find_dev(struct atmel_aes_ctx *ctx)
{
	struct atmel_aes_dev *aes_dd = NULL;
	struct atmel_aes_dev *tmp;

	spin_lock_bh(&atmel_aes.lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &atmel_aes.dev_list, list) {
			aes_dd = tmp;
			break;
		}
		ctx->dd = aes_dd;
	} else {
		aes_dd = ctx->dd;
	}

	spin_unlock_bh(&atmel_aes.lock);

	return aes_dd;
}

static int atmel_aes_hw_init(struct atmel_aes_dev *dd)
{
	int err;

	err = clk_prepare_enable(dd->iclk);
	if (err)
		return err;

	if (!(dd->flags & AES_FLAGS_INIT)) {
		atmel_aes_write(dd, AES_CR, AES_CR_SWRST);
		atmel_aes_write(dd, AES_MR, 0xE << AES_MR_CKEY_OFFSET);
		dd->flags |= AES_FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static inline unsigned int atmel_aes_get_version(struct atmel_aes_dev *dd)
{
	return atmel_aes_read(dd, AES_HW_VERSION) & 0x00000fff;
}

static void atmel_aes_hw_version_init(struct atmel_aes_dev *dd)
{
	atmel_aes_hw_init(dd);

	dd->hw_version = atmel_aes_get_version(dd);

	dev_info(dd->dev,
			"version: 0x%x\n", dd->hw_version);

	clk_disable_unprepare(dd->iclk);
}

static void atmel_aes_finish_req(struct atmel_aes_dev *dd, int err)
{
	struct ablkcipher_request *req = dd->req;

	clk_disable_unprepare(dd->iclk);
	dd->flags &= ~AES_FLAGS_BUSY;

	req->base.complete(&req->base, err);
}

static void atmel_aes_dma_callback(void *data)
{
	struct atmel_aes_dev *dd = data;

	/* dma_lch_out - completed */
	tasklet_schedule(&dd->done_task);
}

static int atmel_aes_crypt_dma(struct atmel_aes_dev *dd,
		dma_addr_t dma_addr_in, dma_addr_t dma_addr_out, int length)
{
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor	*in_desc, *out_desc;

	dd->dma_size = length;

	dma_sync_single_for_device(dd->dev, dma_addr_in, length,
				   DMA_TO_DEVICE);
	dma_sync_single_for_device(dd->dev, dma_addr_out, length,
				   DMA_FROM_DEVICE);

	if (dd->flags & AES_FLAGS_CFB8) {
		dd->dma_lch_in.dma_conf.dst_addr_width =
			DMA_SLAVE_BUSWIDTH_1_BYTE;
		dd->dma_lch_out.dma_conf.src_addr_width =
			DMA_SLAVE_BUSWIDTH_1_BYTE;
	} else if (dd->flags & AES_FLAGS_CFB16) {
		dd->dma_lch_in.dma_conf.dst_addr_width =
			DMA_SLAVE_BUSWIDTH_2_BYTES;
		dd->dma_lch_out.dma_conf.src_addr_width =
			DMA_SLAVE_BUSWIDTH_2_BYTES;
	} else {
		dd->dma_lch_in.dma_conf.dst_addr_width =
			DMA_SLAVE_BUSWIDTH_4_BYTES;
		dd->dma_lch_out.dma_conf.src_addr_width =
			DMA_SLAVE_BUSWIDTH_4_BYTES;
	}

	if (dd->flags & (AES_FLAGS_CFB8 | AES_FLAGS_CFB16 |
			AES_FLAGS_CFB32 | AES_FLAGS_CFB64)) {
		dd->dma_lch_in.dma_conf.src_maxburst = 1;
		dd->dma_lch_in.dma_conf.dst_maxburst = 1;
		dd->dma_lch_out.dma_conf.src_maxburst = 1;
		dd->dma_lch_out.dma_conf.dst_maxburst = 1;
	} else {
		dd->dma_lch_in.dma_conf.src_maxburst = dd->caps.max_burst_size;
		dd->dma_lch_in.dma_conf.dst_maxburst = dd->caps.max_burst_size;
		dd->dma_lch_out.dma_conf.src_maxburst = dd->caps.max_burst_size;
		dd->dma_lch_out.dma_conf.dst_maxburst = dd->caps.max_burst_size;
	}

	dmaengine_slave_config(dd->dma_lch_in.chan, &dd->dma_lch_in.dma_conf);
	dmaengine_slave_config(dd->dma_lch_out.chan, &dd->dma_lch_out.dma_conf);

	dd->flags |= AES_FLAGS_DMA;

	sg_init_table(&sg[0], 1);
	sg_dma_address(&sg[0]) = dma_addr_in;
	sg_dma_len(&sg[0]) = length;

	sg_init_table(&sg[1], 1);
	sg_dma_address(&sg[1]) = dma_addr_out;
	sg_dma_len(&sg[1]) = length;

	in_desc = dmaengine_prep_slave_sg(dd->dma_lch_in.chan, &sg[0],
				1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT  |  DMA_CTRL_ACK);
	if (!in_desc)
		return -EINVAL;

	out_desc = dmaengine_prep_slave_sg(dd->dma_lch_out.chan, &sg[1],
				1, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!out_desc)
		return -EINVAL;

	out_desc->callback = atmel_aes_dma_callback;
	out_desc->callback_param = dd;

	dmaengine_submit(out_desc);
	dma_async_issue_pending(dd->dma_lch_out.chan);

	dmaengine_submit(in_desc);
	dma_async_issue_pending(dd->dma_lch_in.chan);

	return 0;
}

static int atmel_aes_crypt_cpu_start(struct atmel_aes_dev *dd)
{
	dd->flags &= ~AES_FLAGS_DMA;

	dma_sync_single_for_cpu(dd->dev, dd->dma_addr_in,
				dd->dma_size, DMA_TO_DEVICE);
	dma_sync_single_for_cpu(dd->dev, dd->dma_addr_out,
				dd->dma_size, DMA_FROM_DEVICE);

	/* use cache buffers */
	dd->nb_in_sg = atmel_aes_sg_length(dd->req, dd->in_sg);
	if (!dd->nb_in_sg)
		return -EINVAL;

	dd->nb_out_sg = atmel_aes_sg_length(dd->req, dd->out_sg);
	if (!dd->nb_out_sg)
		return -EINVAL;

	dd->bufcnt = sg_copy_to_buffer(dd->in_sg, dd->nb_in_sg,
					dd->buf_in, dd->total);

	if (!dd->bufcnt)
		return -EINVAL;

	dd->total -= dd->bufcnt;

	atmel_aes_write(dd, AES_IER, AES_INT_DATARDY);
	atmel_aes_write_n(dd, AES_IDATAR(0), (u32 *) dd->buf_in,
				dd->bufcnt >> 2);

	return 0;
}

static int atmel_aes_crypt_dma_start(struct atmel_aes_dev *dd)
{
	int err, fast = 0, in, out;
	size_t count;
	dma_addr_t addr_in, addr_out;

	if ((!dd->in_offset) && (!dd->out_offset)) {
		/* check for alignment */
		in = IS_ALIGNED((u32)dd->in_sg->offset, sizeof(u32)) &&
			IS_ALIGNED(dd->in_sg->length, dd->ctx->block_size);
		out = IS_ALIGNED((u32)dd->out_sg->offset, sizeof(u32)) &&
			IS_ALIGNED(dd->out_sg->length, dd->ctx->block_size);
		fast = in && out;

		if (sg_dma_len(dd->in_sg) != sg_dma_len(dd->out_sg))
			fast = 0;
	}


	if (fast)  {
		count = min(dd->total, sg_dma_len(dd->in_sg));
		count = min(count, sg_dma_len(dd->out_sg));

		err = dma_map_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}

		err = dma_map_sg(dd->dev, dd->out_sg, 1,
				DMA_FROM_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			dma_unmap_sg(dd->dev, dd->in_sg, 1,
				DMA_TO_DEVICE);
			return -EINVAL;
		}

		addr_in = sg_dma_address(dd->in_sg);
		addr_out = sg_dma_address(dd->out_sg);

		dd->flags |= AES_FLAGS_FAST;

	} else {
		dma_sync_single_for_cpu(dd->dev, dd->dma_addr_in,
					dd->dma_size, DMA_TO_DEVICE);

		/* use cache buffers */
		count = atmel_aes_sg_copy(&dd->in_sg, &dd->in_offset,
				dd->buf_in, dd->buflen, dd->total, 0);

		addr_in = dd->dma_addr_in;
		addr_out = dd->dma_addr_out;

		dd->flags &= ~AES_FLAGS_FAST;
	}

	dd->total -= count;

	err = atmel_aes_crypt_dma(dd, addr_in, addr_out, count);

	if (err && (dd->flags & AES_FLAGS_FAST)) {
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_TO_DEVICE);
	}

	return err;
}

static int atmel_aes_write_ctrl(struct atmel_aes_dev *dd)
{
	int err;
	u32 valcr = 0, valmr = 0;

	err = atmel_aes_hw_init(dd);

	if (err)
		return err;

	/* MR register must be set before IV registers */
	if (dd->ctx->keylen == AES_KEYSIZE_128)
		valmr |= AES_MR_KEYSIZE_128;
	else if (dd->ctx->keylen == AES_KEYSIZE_192)
		valmr |= AES_MR_KEYSIZE_192;
	else
		valmr |= AES_MR_KEYSIZE_256;

	if (dd->flags & AES_FLAGS_CBC) {
		valmr |= AES_MR_OPMOD_CBC;
	} else if (dd->flags & AES_FLAGS_CFB) {
		valmr |= AES_MR_OPMOD_CFB;
		if (dd->flags & AES_FLAGS_CFB8)
			valmr |= AES_MR_CFBS_8b;
		else if (dd->flags & AES_FLAGS_CFB16)
			valmr |= AES_MR_CFBS_16b;
		else if (dd->flags & AES_FLAGS_CFB32)
			valmr |= AES_MR_CFBS_32b;
		else if (dd->flags & AES_FLAGS_CFB64)
			valmr |= AES_MR_CFBS_64b;
		else if (dd->flags & AES_FLAGS_CFB128)
			valmr |= AES_MR_CFBS_128b;
	} else if (dd->flags & AES_FLAGS_OFB) {
		valmr |= AES_MR_OPMOD_OFB;
	} else if (dd->flags & AES_FLAGS_CTR) {
		valmr |= AES_MR_OPMOD_CTR;
	} else {
		valmr |= AES_MR_OPMOD_ECB;
	}

	if (dd->flags & AES_FLAGS_ENCRYPT)
		valmr |= AES_MR_CYPHER_ENC;

	if (dd->total > ATMEL_AES_DMA_THRESHOLD) {
		valmr |= AES_MR_SMOD_IDATAR0;
		if (dd->caps.has_dualbuff)
			valmr |= AES_MR_DUALBUFF;
	} else {
		valmr |= AES_MR_SMOD_AUTO;
	}

	atmel_aes_write(dd, AES_CR, valcr);
	atmel_aes_write(dd, AES_MR, valmr);

	atmel_aes_write_n(dd, AES_KEYWR(0), dd->ctx->key,
						dd->ctx->keylen >> 2);

	if (((dd->flags & AES_FLAGS_CBC) || (dd->flags & AES_FLAGS_CFB) ||
	   (dd->flags & AES_FLAGS_OFB) || (dd->flags & AES_FLAGS_CTR)) &&
	   dd->req->info) {
		atmel_aes_write_n(dd, AES_IVR(0), dd->req->info, 4);
	}

	return 0;
}

static int atmel_aes_handle_queue(struct atmel_aes_dev *dd,
			       struct ablkcipher_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct atmel_aes_ctx *ctx;
	struct atmel_aes_reqctx *rctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ablkcipher_enqueue_request(&dd->queue, req);
	if (dd->flags & AES_FLAGS_BUSY) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		dd->flags |= AES_FLAGS_BUSY;
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!async_req)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ablkcipher_request_cast(async_req);

	/* assign new request to device */
	dd->req = req;
	dd->total = req->nbytes;
	dd->in_offset = 0;
	dd->in_sg = req->src;
	dd->out_offset = 0;
	dd->out_sg = req->dst;

	rctx = ablkcipher_request_ctx(req);
	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx->mode &= AES_FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~AES_FLAGS_MODE_MASK) | rctx->mode;
	dd->ctx = ctx;
	ctx->dd = dd;

	err = atmel_aes_write_ctrl(dd);
	if (!err) {
		if (dd->total > ATMEL_AES_DMA_THRESHOLD)
			err = atmel_aes_crypt_dma_start(dd);
		else
			err = atmel_aes_crypt_cpu_start(dd);
	}
	if (err) {
		/* aes_task will not finish it, so do it here */
		atmel_aes_finish_req(dd, err);
		tasklet_schedule(&dd->queue_task);
	}

	return ret;
}

static int atmel_aes_crypt_dma_stop(struct atmel_aes_dev *dd)
{
	int err = -EINVAL;
	size_t count;

	if (dd->flags & AES_FLAGS_DMA) {
		err = 0;
		if  (dd->flags & AES_FLAGS_FAST) {
			dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_FROM_DEVICE);
			dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		} else {
			dma_sync_single_for_cpu(dd->dev, dd->dma_addr_out,
				dd->dma_size, DMA_FROM_DEVICE);

			/* copy data */
			count = atmel_aes_sg_copy(&dd->out_sg, &dd->out_offset,
				dd->buf_out, dd->buflen, dd->dma_size, 1);
			if (count != dd->dma_size) {
				err = -EINVAL;
				pr_err("not all data converted: %u\n", count);
			}
		}
	}

	return err;
}


static int atmel_aes_buff_init(struct atmel_aes_dev *dd)
{
	int err = -ENOMEM;

	dd->buf_in = (void *)__get_free_pages(GFP_KERNEL, 0);
	dd->buf_out = (void *)__get_free_pages(GFP_KERNEL, 0);
	dd->buflen = PAGE_SIZE;
	dd->buflen &= ~(AES_BLOCK_SIZE - 1);

	if (!dd->buf_in || !dd->buf_out) {
		dev_err(dd->dev, "unable to alloc pages.\n");
		goto err_alloc;
	}

	/* MAP here */
	dd->dma_addr_in = dma_map_single(dd->dev, dd->buf_in,
					dd->buflen, DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_in)) {
		dev_err(dd->dev, "dma %d bytes error\n", dd->buflen);
		err = -EINVAL;
		goto err_map_in;
	}

	dd->dma_addr_out = dma_map_single(dd->dev, dd->buf_out,
					dd->buflen, DMA_FROM_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_out)) {
		dev_err(dd->dev, "dma %d bytes error\n", dd->buflen);
		err = -EINVAL;
		goto err_map_out;
	}

	return 0;

err_map_out:
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen,
		DMA_TO_DEVICE);
err_map_in:
err_alloc:
	free_page((unsigned long)dd->buf_out);
	free_page((unsigned long)dd->buf_in);
	if (err)
		pr_err("error: %d\n", err);
	return err;
}

static void atmel_aes_buff_cleanup(struct atmel_aes_dev *dd)
{
	dma_unmap_single(dd->dev, dd->dma_addr_out, dd->buflen,
			 DMA_FROM_DEVICE);
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen,
		DMA_TO_DEVICE);
	free_page((unsigned long)dd->buf_out);
	free_page((unsigned long)dd->buf_in);
}

static int atmel_aes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct atmel_aes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct atmel_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	struct atmel_aes_dev *dd;

	if (mode & AES_FLAGS_CFB8) {
		if (!IS_ALIGNED(req->nbytes, CFB8_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB8 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB8_BLOCK_SIZE;
	} else if (mode & AES_FLAGS_CFB16) {
		if (!IS_ALIGNED(req->nbytes, CFB16_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB16 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB16_BLOCK_SIZE;
	} else if (mode & AES_FLAGS_CFB32) {
		if (!IS_ALIGNED(req->nbytes, CFB32_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB32 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB32_BLOCK_SIZE;
	} else if (mode & AES_FLAGS_CFB64) {
		if (!IS_ALIGNED(req->nbytes, CFB64_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB64 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB64_BLOCK_SIZE;
	} else {
		if (!IS_ALIGNED(req->nbytes, AES_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of AES blocks\n");
			return -EINVAL;
		}
		ctx->block_size = AES_BLOCK_SIZE;
	}

	dd = atmel_aes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx->mode = mode;

	return atmel_aes_handle_queue(dd, req);
}

static bool atmel_aes_filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave	*sl = slave;

	if (sl && sl->dma_dev == chan->device->dev) {
		chan->private = sl;
		return true;
	} else {
		return false;
	}
}

static int atmel_aes_dma_init(struct atmel_aes_dev *dd,
	struct crypto_platform_data *pdata)
{
	int err = -ENOMEM;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Try to grab 2 DMA channels */
	dd->dma_lch_in.chan = dma_request_slave_channel_compat(mask,
			atmel_aes_filter, &pdata->dma_slave->rxdata, dd->dev, "tx");
	if (!dd->dma_lch_in.chan)
		goto err_dma_in;

	dd->dma_lch_in.dma_conf.direction = DMA_MEM_TO_DEV;
	dd->dma_lch_in.dma_conf.dst_addr = dd->phys_base +
		AES_IDATAR(0);
	dd->dma_lch_in.dma_conf.src_maxburst = dd->caps.max_burst_size;
	dd->dma_lch_in.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.dst_maxburst = dd->caps.max_burst_size;
	dd->dma_lch_in.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.device_fc = false;

	dd->dma_lch_out.chan = dma_request_slave_channel_compat(mask,
			atmel_aes_filter, &pdata->dma_slave->txdata, dd->dev, "rx");
	if (!dd->dma_lch_out.chan)
		goto err_dma_out;

	dd->dma_lch_out.dma_conf.direction = DMA_DEV_TO_MEM;
	dd->dma_lch_out.dma_conf.src_addr = dd->phys_base +
		AES_ODATAR(0);
	dd->dma_lch_out.dma_conf.src_maxburst = dd->caps.max_burst_size;
	dd->dma_lch_out.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_out.dma_conf.dst_maxburst = dd->caps.max_burst_size;
	dd->dma_lch_out.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_out.dma_conf.device_fc = false;

	return 0;

err_dma_out:
	dma_release_channel(dd->dma_lch_in.chan);
err_dma_in:
	dev_warn(dd->dev, "no DMA channel available\n");
	return err;
}

static void atmel_aes_dma_cleanup(struct atmel_aes_dev *dd)
{
	dma_release_channel(dd->dma_lch_in.chan);
	dma_release_channel(dd->dma_lch_out.chan);
}

static int atmel_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct atmel_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
		   keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT);
}

static int atmel_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		0);
}

static int atmel_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CBC);
}

static int atmel_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CBC);
}

static int atmel_aes_ofb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_OFB);
}

static int atmel_aes_ofb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_OFB);
}

static int atmel_aes_cfb_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CFB | AES_FLAGS_CFB128);
}

static int atmel_aes_cfb_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CFB | AES_FLAGS_CFB128);
}

static int atmel_aes_cfb64_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CFB | AES_FLAGS_CFB64);
}

static int atmel_aes_cfb64_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CFB | AES_FLAGS_CFB64);
}

static int atmel_aes_cfb32_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CFB | AES_FLAGS_CFB32);
}

static int atmel_aes_cfb32_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CFB | AES_FLAGS_CFB32);
}

static int atmel_aes_cfb16_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CFB | AES_FLAGS_CFB16);
}

static int atmel_aes_cfb16_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CFB | AES_FLAGS_CFB16);
}

static int atmel_aes_cfb8_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT |	AES_FLAGS_CFB | AES_FLAGS_CFB8);
}

static int atmel_aes_cfb8_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CFB | AES_FLAGS_CFB8);
}

static int atmel_aes_ctr_encrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_ENCRYPT | AES_FLAGS_CTR);
}

static int atmel_aes_ctr_decrypt(struct ablkcipher_request *req)
{
	return atmel_aes_crypt(req,
		AES_FLAGS_CTR);
}

static int atmel_aes_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_aes_reqctx);

	return 0;
}

static void atmel_aes_cra_exit(struct crypto_tfm *tfm)
{
}

static struct crypto_alg aes_algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "atmel-ecb-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ecb_encrypt,
		.decrypt	= atmel_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "atmel-cbc-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cbc_encrypt,
		.decrypt	= atmel_aes_cbc_decrypt,
	}
},
{
	.cra_name		= "ofb(aes)",
	.cra_driver_name	= "atmel-ofb-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ofb_encrypt,
		.decrypt	= atmel_aes_ofb_decrypt,
	}
},
{
	.cra_name		= "cfb(aes)",
	.cra_driver_name	= "atmel-cfb-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb_encrypt,
		.decrypt	= atmel_aes_cfb_decrypt,
	}
},
{
	.cra_name		= "cfb32(aes)",
	.cra_driver_name	= "atmel-cfb32-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB32_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x3,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb32_encrypt,
		.decrypt	= atmel_aes_cfb32_decrypt,
	}
},
{
	.cra_name		= "cfb16(aes)",
	.cra_driver_name	= "atmel-cfb16-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB16_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x1,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb16_encrypt,
		.decrypt	= atmel_aes_cfb16_decrypt,
	}
},
{
	.cra_name		= "cfb8(aes)",
	.cra_driver_name	= "atmel-cfb8-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB8_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb8_encrypt,
		.decrypt	= atmel_aes_cfb8_decrypt,
	}
},
{
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "atmel-ctr-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_ctr_encrypt,
		.decrypt	= atmel_aes_ctr_decrypt,
	}
},
};

static struct crypto_alg aes_cfb64_alg = {
	.cra_name		= "cfb64(aes)",
	.cra_driver_name	= "atmel-cfb64-aes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB64_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_aes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_aes_cra_init,
	.cra_exit		= atmel_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= atmel_aes_setkey,
		.encrypt	= atmel_aes_cfb64_encrypt,
		.decrypt	= atmel_aes_cfb64_decrypt,
	}
};

static void atmel_aes_queue_task(unsigned long data)
{
	struct atmel_aes_dev *dd = (struct atmel_aes_dev *)data;

	atmel_aes_handle_queue(dd, NULL);
}

static void atmel_aes_done_task(unsigned long data)
{
	struct atmel_aes_dev *dd = (struct atmel_aes_dev *) data;
	int err;

	if (!(dd->flags & AES_FLAGS_DMA)) {
		atmel_aes_read_n(dd, AES_ODATAR(0), (u32 *) dd->buf_out,
				dd->bufcnt >> 2);

		if (sg_copy_from_buffer(dd->out_sg, dd->nb_out_sg,
			dd->buf_out, dd->bufcnt))
			err = 0;
		else
			err = -EINVAL;

		goto cpu_end;
	}

	err = atmel_aes_crypt_dma_stop(dd);

	err = dd->err ? : err;

	if (dd->total && !err) {
		if (dd->flags & AES_FLAGS_FAST) {
			dd->in_sg = sg_next(dd->in_sg);
			dd->out_sg = sg_next(dd->out_sg);
			if (!dd->in_sg || !dd->out_sg)
				err = -EINVAL;
		}
		if (!err)
			err = atmel_aes_crypt_dma_start(dd);
		if (!err)
			return; /* DMA started. Not fininishing. */
	}

cpu_end:
	atmel_aes_finish_req(dd, err);
	atmel_aes_handle_queue(dd, NULL);
}

static irqreturn_t atmel_aes_irq(int irq, void *dev_id)
{
	struct atmel_aes_dev *aes_dd = dev_id;
	u32 reg;

	reg = atmel_aes_read(aes_dd, AES_ISR);
	if (reg & atmel_aes_read(aes_dd, AES_IMR)) {
		atmel_aes_write(aes_dd, AES_IDR, reg);
		if (AES_FLAGS_BUSY & aes_dd->flags)
			tasklet_schedule(&aes_dd->done_task);
		else
			dev_warn(aes_dd->dev, "AES interrupt when no active requests.\n");
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void atmel_aes_unregister_algs(struct atmel_aes_dev *dd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++)
		crypto_unregister_alg(&aes_algs[i]);
	if (dd->caps.has_cfb64)
		crypto_unregister_alg(&aes_cfb64_alg);
}

static int atmel_aes_register_algs(struct atmel_aes_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		err = crypto_register_alg(&aes_algs[i]);
		if (err)
			goto err_aes_algs;
	}

	if (dd->caps.has_cfb64) {
		err = crypto_register_alg(&aes_cfb64_alg);
		if (err)
			goto err_aes_cfb64_alg;
	}

	return 0;

err_aes_cfb64_alg:
	i = ARRAY_SIZE(aes_algs);
err_aes_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&aes_algs[j]);

	return err;
}

static void atmel_aes_get_cap(struct atmel_aes_dev *dd)
{
	dd->caps.has_dualbuff = 0;
	dd->caps.has_cfb64 = 0;
	dd->caps.max_burst_size = 1;

	/* keep only major version number */
	switch (dd->hw_version & 0xff0) {
	case 0x200:
		dd->caps.has_dualbuff = 1;
		dd->caps.has_cfb64 = 1;
		dd->caps.max_burst_size = 4;
		break;
	case 0x130:
		dd->caps.has_dualbuff = 1;
		dd->caps.has_cfb64 = 1;
		dd->caps.max_burst_size = 4;
		break;
	case 0x120:
		break;
	default:
		dev_warn(dd->dev,
				"Unmanaged aes version, set minimum capabilities\n");
		break;
	}
}

#if defined(CONFIG_OF)
static const struct of_device_id atmel_aes_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-aes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_aes_dt_ids);

static struct crypto_platform_data *atmel_aes_of_init(struct platform_device *pdev)
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
		devm_kfree(&pdev->dev, pdata);
		return ERR_PTR(-ENOMEM);
	}

	return pdata;
}
#else
static inline struct crypto_platform_data *atmel_aes_of_init(struct platform_device *pdev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int atmel_aes_probe(struct platform_device *pdev)
{
	struct atmel_aes_dev *aes_dd;
	struct crypto_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct resource *aes_res;
	int err;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		pdata = atmel_aes_of_init(pdev);
		if (IS_ERR(pdata)) {
			err = PTR_ERR(pdata);
			goto aes_dd_err;
		}
	}

	if (!pdata->dma_slave) {
		err = -ENXIO;
		goto aes_dd_err;
	}

	aes_dd = devm_kzalloc(&pdev->dev, sizeof(*aes_dd), GFP_KERNEL);
	if (aes_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto aes_dd_err;
	}

	aes_dd->dev = dev;

	platform_set_drvdata(pdev, aes_dd);

	INIT_LIST_HEAD(&aes_dd->list);
	spin_lock_init(&aes_dd->lock);

	tasklet_init(&aes_dd->done_task, atmel_aes_done_task,
					(unsigned long)aes_dd);
	tasklet_init(&aes_dd->queue_task, atmel_aes_queue_task,
					(unsigned long)aes_dd);

	crypto_init_queue(&aes_dd->queue, ATMEL_AES_QUEUE_LENGTH);

	aes_dd->irq = -1;

	/* Get the base address */
	aes_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!aes_res) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto res_err;
	}
	aes_dd->phys_base = aes_res->start;

	/* Get the IRQ */
	aes_dd->irq = platform_get_irq(pdev,  0);
	if (aes_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = aes_dd->irq;
		goto res_err;
	}

	err = devm_request_irq(&pdev->dev, aes_dd->irq, atmel_aes_irq,
			       IRQF_SHARED, "atmel-aes", aes_dd);
	if (err) {
		dev_err(dev, "unable to request aes irq.\n");
		goto res_err;
	}

	/* Initializing the clock */
	aes_dd->iclk = devm_clk_get(&pdev->dev, "aes_clk");
	if (IS_ERR(aes_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(aes_dd->iclk);
		goto res_err;
	}

	aes_dd->io_base = devm_ioremap_resource(&pdev->dev, aes_res);
	if (IS_ERR(aes_dd->io_base)) {
		dev_err(dev, "can't ioremap\n");
		err = PTR_ERR(aes_dd->io_base);
		goto res_err;
	}

	atmel_aes_hw_version_init(aes_dd);

	atmel_aes_get_cap(aes_dd);

	err = atmel_aes_buff_init(aes_dd);
	if (err)
		goto err_aes_buff;

	err = atmel_aes_dma_init(aes_dd, pdata);
	if (err)
		goto err_aes_dma;

	spin_lock(&atmel_aes.lock);
	list_add_tail(&aes_dd->list, &atmel_aes.dev_list);
	spin_unlock(&atmel_aes.lock);

	err = atmel_aes_register_algs(aes_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel AES - Using %s, %s for DMA transfers\n",
			dma_chan_name(aes_dd->dma_lch_in.chan),
			dma_chan_name(aes_dd->dma_lch_out.chan));

	return 0;

err_algs:
	spin_lock(&atmel_aes.lock);
	list_del(&aes_dd->list);
	spin_unlock(&atmel_aes.lock);
	atmel_aes_dma_cleanup(aes_dd);
err_aes_dma:
	atmel_aes_buff_cleanup(aes_dd);
err_aes_buff:
res_err:
	tasklet_kill(&aes_dd->done_task);
	tasklet_kill(&aes_dd->queue_task);
aes_dd_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int atmel_aes_remove(struct platform_device *pdev)
{
	static struct atmel_aes_dev *aes_dd;

	aes_dd = platform_get_drvdata(pdev);
	if (!aes_dd)
		return -ENODEV;
	spin_lock(&atmel_aes.lock);
	list_del(&aes_dd->list);
	spin_unlock(&atmel_aes.lock);

	atmel_aes_unregister_algs(aes_dd);

	tasklet_kill(&aes_dd->done_task);
	tasklet_kill(&aes_dd->queue_task);

	atmel_aes_dma_cleanup(aes_dd);

	return 0;
}

static struct platform_driver atmel_aes_driver = {
	.probe		= atmel_aes_probe,
	.remove		= atmel_aes_remove,
	.driver		= {
		.name	= "atmel_aes",
		.of_match_table = of_match_ptr(atmel_aes_dt_ids),
	},
};

module_platform_driver(atmel_aes_driver);

MODULE_DESCRIPTION("Atmel AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
