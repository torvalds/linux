/*
 * Cryptographic API.
 *
 * Support for ATMEL DES/TDES HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Some ideas are from omap-aes.c drivers.
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
#include <crypto/des.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <linux/platform_data/crypto-atmel.h>
#include "atmel-tdes-regs.h"

/* TDES flags  */
#define TDES_FLAGS_MODE_MASK		0x00ff
#define TDES_FLAGS_ENCRYPT	BIT(0)
#define TDES_FLAGS_CBC		BIT(1)
#define TDES_FLAGS_CFB		BIT(2)
#define TDES_FLAGS_CFB8		BIT(3)
#define TDES_FLAGS_CFB16	BIT(4)
#define TDES_FLAGS_CFB32	BIT(5)
#define TDES_FLAGS_CFB64	BIT(6)
#define TDES_FLAGS_OFB		BIT(7)

#define TDES_FLAGS_INIT		BIT(16)
#define TDES_FLAGS_FAST		BIT(17)
#define TDES_FLAGS_BUSY		BIT(18)
#define TDES_FLAGS_DMA		BIT(19)

#define ATMEL_TDES_QUEUE_LENGTH	50

#define CFB8_BLOCK_SIZE		1
#define CFB16_BLOCK_SIZE	2
#define CFB32_BLOCK_SIZE	4

struct atmel_tdes_caps {
	bool	has_dma;
	u32		has_cfb_3keys;
};

struct atmel_tdes_dev;

struct atmel_tdes_ctx {
	struct atmel_tdes_dev *dd;

	int		keylen;
	u32		key[3*DES_KEY_SIZE / sizeof(u32)];
	unsigned long	flags;

	u16		block_size;
};

struct atmel_tdes_reqctx {
	unsigned long mode;
};

struct atmel_tdes_dma {
	struct dma_chan			*chan;
	struct dma_slave_config dma_conf;
};

struct atmel_tdes_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;

	struct atmel_tdes_ctx	*ctx;
	struct device		*dev;
	struct clk			*iclk;
	int					irq;

	unsigned long		flags;
	int			err;

	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct ablkcipher_request	*req;
	size_t				total;

	struct scatterlist	*in_sg;
	unsigned int		nb_in_sg;
	size_t				in_offset;
	struct scatterlist	*out_sg;
	unsigned int		nb_out_sg;
	size_t				out_offset;

	size_t	buflen;
	size_t	dma_size;

	void	*buf_in;
	int		dma_in;
	dma_addr_t	dma_addr_in;
	struct atmel_tdes_dma	dma_lch_in;

	void	*buf_out;
	int		dma_out;
	dma_addr_t	dma_addr_out;
	struct atmel_tdes_dma	dma_lch_out;

	struct atmel_tdes_caps	caps;

	u32	hw_version;
};

struct atmel_tdes_drv {
	struct list_head	dev_list;
	spinlock_t		lock;
};

static struct atmel_tdes_drv atmel_tdes = {
	.dev_list = LIST_HEAD_INIT(atmel_tdes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(atmel_tdes.lock),
};

static int atmel_tdes_sg_copy(struct scatterlist **sg, size_t *offset,
			void *buf, size_t buflen, size_t total, int out)
{
	size_t count, off = 0;

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

static inline u32 atmel_tdes_read(struct atmel_tdes_dev *dd, u32 offset)
{
	return readl_relaxed(dd->io_base + offset);
}

static inline void atmel_tdes_write(struct atmel_tdes_dev *dd,
					u32 offset, u32 value)
{
	writel_relaxed(value, dd->io_base + offset);
}

static void atmel_tdes_write_n(struct atmel_tdes_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		atmel_tdes_write(dd, offset, *value);
}

static struct atmel_tdes_dev *atmel_tdes_find_dev(struct atmel_tdes_ctx *ctx)
{
	struct atmel_tdes_dev *tdes_dd = NULL;
	struct atmel_tdes_dev *tmp;

	spin_lock_bh(&atmel_tdes.lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &atmel_tdes.dev_list, list) {
			tdes_dd = tmp;
			break;
		}
		ctx->dd = tdes_dd;
	} else {
		tdes_dd = ctx->dd;
	}
	spin_unlock_bh(&atmel_tdes.lock);

	return tdes_dd;
}

static int atmel_tdes_hw_init(struct atmel_tdes_dev *dd)
{
	int err;

	err = clk_prepare_enable(dd->iclk);
	if (err)
		return err;

	if (!(dd->flags & TDES_FLAGS_INIT)) {
		atmel_tdes_write(dd, TDES_CR, TDES_CR_SWRST);
		dd->flags |= TDES_FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static inline unsigned int atmel_tdes_get_version(struct atmel_tdes_dev *dd)
{
	return atmel_tdes_read(dd, TDES_HW_VERSION) & 0x00000fff;
}

static void atmel_tdes_hw_version_init(struct atmel_tdes_dev *dd)
{
	atmel_tdes_hw_init(dd);

	dd->hw_version = atmel_tdes_get_version(dd);

	dev_info(dd->dev,
			"version: 0x%x\n", dd->hw_version);

	clk_disable_unprepare(dd->iclk);
}

static void atmel_tdes_dma_callback(void *data)
{
	struct atmel_tdes_dev *dd = data;

	/* dma_lch_out - completed */
	tasklet_schedule(&dd->done_task);
}

static int atmel_tdes_write_ctrl(struct atmel_tdes_dev *dd)
{
	int err;
	u32 valcr = 0, valmr = TDES_MR_SMOD_PDC;

	err = atmel_tdes_hw_init(dd);

	if (err)
		return err;

	if (!dd->caps.has_dma)
		atmel_tdes_write(dd, TDES_PTCR,
			TDES_PTCR_TXTDIS | TDES_PTCR_RXTDIS);

	/* MR register must be set before IV registers */
	if (dd->ctx->keylen > (DES_KEY_SIZE << 1)) {
		valmr |= TDES_MR_KEYMOD_3KEY;
		valmr |= TDES_MR_TDESMOD_TDES;
	} else if (dd->ctx->keylen > DES_KEY_SIZE) {
		valmr |= TDES_MR_KEYMOD_2KEY;
		valmr |= TDES_MR_TDESMOD_TDES;
	} else {
		valmr |= TDES_MR_TDESMOD_DES;
	}

	if (dd->flags & TDES_FLAGS_CBC) {
		valmr |= TDES_MR_OPMOD_CBC;
	} else if (dd->flags & TDES_FLAGS_CFB) {
		valmr |= TDES_MR_OPMOD_CFB;

		if (dd->flags & TDES_FLAGS_CFB8)
			valmr |= TDES_MR_CFBS_8b;
		else if (dd->flags & TDES_FLAGS_CFB16)
			valmr |= TDES_MR_CFBS_16b;
		else if (dd->flags & TDES_FLAGS_CFB32)
			valmr |= TDES_MR_CFBS_32b;
		else if (dd->flags & TDES_FLAGS_CFB64)
			valmr |= TDES_MR_CFBS_64b;
	} else if (dd->flags & TDES_FLAGS_OFB) {
		valmr |= TDES_MR_OPMOD_OFB;
	}

	if ((dd->flags & TDES_FLAGS_ENCRYPT) || (dd->flags & TDES_FLAGS_OFB))
		valmr |= TDES_MR_CYPHER_ENC;

	atmel_tdes_write(dd, TDES_CR, valcr);
	atmel_tdes_write(dd, TDES_MR, valmr);

	atmel_tdes_write_n(dd, TDES_KEY1W1R, dd->ctx->key,
						dd->ctx->keylen >> 2);

	if (((dd->flags & TDES_FLAGS_CBC) || (dd->flags & TDES_FLAGS_CFB) ||
		(dd->flags & TDES_FLAGS_OFB)) && dd->req->info) {
		atmel_tdes_write_n(dd, TDES_IV1R, dd->req->info, 2);
	}

	return 0;
}

static int atmel_tdes_crypt_pdc_stop(struct atmel_tdes_dev *dd)
{
	int err = 0;
	size_t count;

	atmel_tdes_write(dd, TDES_PTCR, TDES_PTCR_TXTDIS|TDES_PTCR_RXTDIS);

	if (dd->flags & TDES_FLAGS_FAST) {
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_FROM_DEVICE);
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
	} else {
		dma_sync_single_for_device(dd->dev, dd->dma_addr_out,
					   dd->dma_size, DMA_FROM_DEVICE);

		/* copy data */
		count = atmel_tdes_sg_copy(&dd->out_sg, &dd->out_offset,
				dd->buf_out, dd->buflen, dd->dma_size, 1);
		if (count != dd->dma_size) {
			err = -EINVAL;
			pr_err("not all data converted: %zu\n", count);
		}
	}

	return err;
}

static int atmel_tdes_buff_init(struct atmel_tdes_dev *dd)
{
	int err = -ENOMEM;

	dd->buf_in = (void *)__get_free_pages(GFP_KERNEL, 0);
	dd->buf_out = (void *)__get_free_pages(GFP_KERNEL, 0);
	dd->buflen = PAGE_SIZE;
	dd->buflen &= ~(DES_BLOCK_SIZE - 1);

	if (!dd->buf_in || !dd->buf_out) {
		dev_err(dd->dev, "unable to alloc pages.\n");
		goto err_alloc;
	}

	/* MAP here */
	dd->dma_addr_in = dma_map_single(dd->dev, dd->buf_in,
					dd->buflen, DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_in)) {
		dev_err(dd->dev, "dma %zd bytes error\n", dd->buflen);
		err = -EINVAL;
		goto err_map_in;
	}

	dd->dma_addr_out = dma_map_single(dd->dev, dd->buf_out,
					dd->buflen, DMA_FROM_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_out)) {
		dev_err(dd->dev, "dma %zd bytes error\n", dd->buflen);
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

static void atmel_tdes_buff_cleanup(struct atmel_tdes_dev *dd)
{
	dma_unmap_single(dd->dev, dd->dma_addr_out, dd->buflen,
			 DMA_FROM_DEVICE);
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen,
		DMA_TO_DEVICE);
	free_page((unsigned long)dd->buf_out);
	free_page((unsigned long)dd->buf_in);
}

static int atmel_tdes_crypt_pdc(struct crypto_tfm *tfm, dma_addr_t dma_addr_in,
			       dma_addr_t dma_addr_out, int length)
{
	struct atmel_tdes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct atmel_tdes_dev *dd = ctx->dd;
	int len32;

	dd->dma_size = length;

	if (!(dd->flags & TDES_FLAGS_FAST)) {
		dma_sync_single_for_device(dd->dev, dma_addr_in, length,
					   DMA_TO_DEVICE);
	}

	if ((dd->flags & TDES_FLAGS_CFB) && (dd->flags & TDES_FLAGS_CFB8))
		len32 = DIV_ROUND_UP(length, sizeof(u8));
	else if ((dd->flags & TDES_FLAGS_CFB) && (dd->flags & TDES_FLAGS_CFB16))
		len32 = DIV_ROUND_UP(length, sizeof(u16));
	else
		len32 = DIV_ROUND_UP(length, sizeof(u32));

	atmel_tdes_write(dd, TDES_PTCR, TDES_PTCR_TXTDIS|TDES_PTCR_RXTDIS);
	atmel_tdes_write(dd, TDES_TPR, dma_addr_in);
	atmel_tdes_write(dd, TDES_TCR, len32);
	atmel_tdes_write(dd, TDES_RPR, dma_addr_out);
	atmel_tdes_write(dd, TDES_RCR, len32);

	/* Enable Interrupt */
	atmel_tdes_write(dd, TDES_IER, TDES_INT_ENDRX);

	/* Start DMA transfer */
	atmel_tdes_write(dd, TDES_PTCR, TDES_PTCR_TXTEN | TDES_PTCR_RXTEN);

	return 0;
}

static int atmel_tdes_crypt_dma(struct crypto_tfm *tfm, dma_addr_t dma_addr_in,
			       dma_addr_t dma_addr_out, int length)
{
	struct atmel_tdes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct atmel_tdes_dev *dd = ctx->dd;
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor	*in_desc, *out_desc;

	dd->dma_size = length;

	if (!(dd->flags & TDES_FLAGS_FAST)) {
		dma_sync_single_for_device(dd->dev, dma_addr_in, length,
					   DMA_TO_DEVICE);
	}

	if (dd->flags & TDES_FLAGS_CFB8) {
		dd->dma_lch_in.dma_conf.dst_addr_width =
			DMA_SLAVE_BUSWIDTH_1_BYTE;
		dd->dma_lch_out.dma_conf.src_addr_width =
			DMA_SLAVE_BUSWIDTH_1_BYTE;
	} else if (dd->flags & TDES_FLAGS_CFB16) {
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

	dmaengine_slave_config(dd->dma_lch_in.chan, &dd->dma_lch_in.dma_conf);
	dmaengine_slave_config(dd->dma_lch_out.chan, &dd->dma_lch_out.dma_conf);

	dd->flags |= TDES_FLAGS_DMA;

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

	out_desc->callback = atmel_tdes_dma_callback;
	out_desc->callback_param = dd;

	dmaengine_submit(out_desc);
	dma_async_issue_pending(dd->dma_lch_out.chan);

	dmaengine_submit(in_desc);
	dma_async_issue_pending(dd->dma_lch_in.chan);

	return 0;
}

static int atmel_tdes_crypt_start(struct atmel_tdes_dev *dd)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(
					crypto_ablkcipher_reqtfm(dd->req));
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
		count = min_t(size_t, dd->total, sg_dma_len(dd->in_sg));
		count = min_t(size_t, count, sg_dma_len(dd->out_sg));

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

		dd->flags |= TDES_FLAGS_FAST;

	} else {
		/* use cache buffers */
		count = atmel_tdes_sg_copy(&dd->in_sg, &dd->in_offset,
				dd->buf_in, dd->buflen, dd->total, 0);

		addr_in = dd->dma_addr_in;
		addr_out = dd->dma_addr_out;

		dd->flags &= ~TDES_FLAGS_FAST;
	}

	dd->total -= count;

	if (dd->caps.has_dma)
		err = atmel_tdes_crypt_dma(tfm, addr_in, addr_out, count);
	else
		err = atmel_tdes_crypt_pdc(tfm, addr_in, addr_out, count);

	if (err && (dd->flags & TDES_FLAGS_FAST)) {
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_TO_DEVICE);
	}

	return err;
}

static void atmel_tdes_finish_req(struct atmel_tdes_dev *dd, int err)
{
	struct ablkcipher_request *req = dd->req;

	clk_disable_unprepare(dd->iclk);

	dd->flags &= ~TDES_FLAGS_BUSY;

	req->base.complete(&req->base, err);
}

static int atmel_tdes_handle_queue(struct atmel_tdes_dev *dd,
			       struct ablkcipher_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct atmel_tdes_ctx *ctx;
	struct atmel_tdes_reqctx *rctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ablkcipher_enqueue_request(&dd->queue, req);
	if (dd->flags & TDES_FLAGS_BUSY) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		dd->flags |= TDES_FLAGS_BUSY;
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
	rctx->mode &= TDES_FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~TDES_FLAGS_MODE_MASK) | rctx->mode;
	dd->ctx = ctx;
	ctx->dd = dd;

	err = atmel_tdes_write_ctrl(dd);
	if (!err)
		err = atmel_tdes_crypt_start(dd);
	if (err) {
		/* des_task will not finish it, so do it here */
		atmel_tdes_finish_req(dd, err);
		tasklet_schedule(&dd->queue_task);
	}

	return ret;
}

static int atmel_tdes_crypt_dma_stop(struct atmel_tdes_dev *dd)
{
	int err = -EINVAL;
	size_t count;

	if (dd->flags & TDES_FLAGS_DMA) {
		err = 0;
		if  (dd->flags & TDES_FLAGS_FAST) {
			dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_FROM_DEVICE);
			dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		} else {
			dma_sync_single_for_device(dd->dev, dd->dma_addr_out,
				dd->dma_size, DMA_FROM_DEVICE);

			/* copy data */
			count = atmel_tdes_sg_copy(&dd->out_sg, &dd->out_offset,
				dd->buf_out, dd->buflen, dd->dma_size, 1);
			if (count != dd->dma_size) {
				err = -EINVAL;
				pr_err("not all data converted: %zu\n", count);
			}
		}
	}
	return err;
}

static int atmel_tdes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct atmel_tdes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct atmel_tdes_reqctx *rctx = ablkcipher_request_ctx(req);

	if (mode & TDES_FLAGS_CFB8) {
		if (!IS_ALIGNED(req->nbytes, CFB8_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB8 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB8_BLOCK_SIZE;
	} else if (mode & TDES_FLAGS_CFB16) {
		if (!IS_ALIGNED(req->nbytes, CFB16_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB16 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB16_BLOCK_SIZE;
	} else if (mode & TDES_FLAGS_CFB32) {
		if (!IS_ALIGNED(req->nbytes, CFB32_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB32 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB32_BLOCK_SIZE;
	} else {
		if (!IS_ALIGNED(req->nbytes, DES_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of DES blocks\n");
			return -EINVAL;
		}
		ctx->block_size = DES_BLOCK_SIZE;
	}

	rctx->mode = mode;

	return atmel_tdes_handle_queue(ctx->dd, req);
}

static bool atmel_tdes_filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave	*sl = slave;

	if (sl && sl->dma_dev == chan->device->dev) {
		chan->private = sl;
		return true;
	} else {
		return false;
	}
}

static int atmel_tdes_dma_init(struct atmel_tdes_dev *dd,
			struct crypto_platform_data *pdata)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Try to grab 2 DMA channels */
	dd->dma_lch_in.chan = dma_request_slave_channel_compat(mask,
			atmel_tdes_filter, &pdata->dma_slave->rxdata, dd->dev, "tx");
	if (!dd->dma_lch_in.chan)
		goto err_dma_in;

	dd->dma_lch_in.dma_conf.direction = DMA_MEM_TO_DEV;
	dd->dma_lch_in.dma_conf.dst_addr = dd->phys_base +
		TDES_IDATA1R;
	dd->dma_lch_in.dma_conf.src_maxburst = 1;
	dd->dma_lch_in.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.dst_maxburst = 1;
	dd->dma_lch_in.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.device_fc = false;

	dd->dma_lch_out.chan = dma_request_slave_channel_compat(mask,
			atmel_tdes_filter, &pdata->dma_slave->txdata, dd->dev, "rx");
	if (!dd->dma_lch_out.chan)
		goto err_dma_out;

	dd->dma_lch_out.dma_conf.direction = DMA_DEV_TO_MEM;
	dd->dma_lch_out.dma_conf.src_addr = dd->phys_base +
		TDES_ODATA1R;
	dd->dma_lch_out.dma_conf.src_maxburst = 1;
	dd->dma_lch_out.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_out.dma_conf.dst_maxburst = 1;
	dd->dma_lch_out.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_out.dma_conf.device_fc = false;

	return 0;

err_dma_out:
	dma_release_channel(dd->dma_lch_in.chan);
err_dma_in:
	dev_warn(dd->dev, "no DMA channel available\n");
	return -ENODEV;
}

static void atmel_tdes_dma_cleanup(struct atmel_tdes_dev *dd)
{
	dma_release_channel(dd->dma_lch_in.chan);
	dma_release_channel(dd->dma_lch_out.chan);
}

static int atmel_des_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	u32 tmp[DES_EXPKEY_WORDS];
	int err;
	struct crypto_tfm *ctfm = crypto_ablkcipher_tfm(tfm);

	struct atmel_tdes_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != DES_KEY_SIZE) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	err = des_ekey(tmp, key);
	if (err == 0 && (ctfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		ctfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_tdes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct atmel_tdes_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	const char *alg_name;

	alg_name = crypto_tfm_alg_name(crypto_ablkcipher_tfm(tfm));

	/*
	 * HW bug in cfb 3-keys mode.
	 */
	if (!ctx->dd->caps.has_cfb_3keys && strstr(alg_name, "cfb")
			&& (keylen != 2*DES_KEY_SIZE)) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	} else if ((keylen != 2*DES_KEY_SIZE) && (keylen != 3*DES_KEY_SIZE)) {
		crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_tdes_ecb_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_ecb_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, 0);
}

static int atmel_tdes_cbc_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_CBC);
}

static int atmel_tdes_cbc_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CBC);
}
static int atmel_tdes_cfb_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_CFB);
}

static int atmel_tdes_cfb_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB);
}

static int atmel_tdes_cfb8_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_CFB |
						TDES_FLAGS_CFB8);
}

static int atmel_tdes_cfb8_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB | TDES_FLAGS_CFB8);
}

static int atmel_tdes_cfb16_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_CFB |
						TDES_FLAGS_CFB16);
}

static int atmel_tdes_cfb16_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB | TDES_FLAGS_CFB16);
}

static int atmel_tdes_cfb32_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_CFB |
						TDES_FLAGS_CFB32);
}

static int atmel_tdes_cfb32_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB | TDES_FLAGS_CFB32);
}

static int atmel_tdes_ofb_encrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ENCRYPT | TDES_FLAGS_OFB);
}

static int atmel_tdes_ofb_decrypt(struct ablkcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_OFB);
}

static int atmel_tdes_cra_init(struct crypto_tfm *tfm)
{
	struct atmel_tdes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct atmel_tdes_dev *dd;

	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_tdes_reqctx);

	dd = atmel_tdes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	return 0;
}

static struct crypto_alg tdes_algs[] = {
{
	.cra_name		= "ecb(des)",
	.cra_driver_name	= "atmel-ecb-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_ecb_encrypt,
		.decrypt	= atmel_tdes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des)",
	.cra_driver_name	= "atmel-cbc-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_cbc_encrypt,
		.decrypt	= atmel_tdes_cbc_decrypt,
	}
},
{
	.cra_name		= "cfb(des)",
	.cra_driver_name	= "atmel-cfb-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_cfb_encrypt,
		.decrypt	= atmel_tdes_cfb_decrypt,
	}
},
{
	.cra_name		= "cfb8(des)",
	.cra_driver_name	= "atmel-cfb8-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB8_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_cfb8_encrypt,
		.decrypt	= atmel_tdes_cfb8_decrypt,
	}
},
{
	.cra_name		= "cfb16(des)",
	.cra_driver_name	= "atmel-cfb16-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB16_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x1,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_cfb16_encrypt,
		.decrypt	= atmel_tdes_cfb16_decrypt,
	}
},
{
	.cra_name		= "cfb32(des)",
	.cra_driver_name	= "atmel-cfb32-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB32_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x3,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_cfb32_encrypt,
		.decrypt	= atmel_tdes_cfb32_decrypt,
	}
},
{
	.cra_name		= "ofb(des)",
	.cra_driver_name	= "atmel-ofb-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_des_setkey,
		.encrypt	= atmel_tdes_ofb_encrypt,
		.decrypt	= atmel_tdes_ofb_decrypt,
	}
},
{
	.cra_name		= "ecb(des3_ede)",
	.cra_driver_name	= "atmel-ecb-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2 * DES_KEY_SIZE,
		.max_keysize	= 3 * DES_KEY_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_ecb_encrypt,
		.decrypt	= atmel_tdes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des3_ede)",
	.cra_driver_name	= "atmel-cbc-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 3*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_cbc_encrypt,
		.decrypt	= atmel_tdes_cbc_decrypt,
	}
},
{
	.cra_name		= "cfb(des3_ede)",
	.cra_driver_name	= "atmel-cfb-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 2*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_cfb_encrypt,
		.decrypt	= atmel_tdes_cfb_decrypt,
	}
},
{
	.cra_name		= "cfb8(des3_ede)",
	.cra_driver_name	= "atmel-cfb8-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB8_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 2*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_cfb8_encrypt,
		.decrypt	= atmel_tdes_cfb8_decrypt,
	}
},
{
	.cra_name		= "cfb16(des3_ede)",
	.cra_driver_name	= "atmel-cfb16-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB16_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x1,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 2*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_cfb16_encrypt,
		.decrypt	= atmel_tdes_cfb16_decrypt,
	}
},
{
	.cra_name		= "cfb32(des3_ede)",
	.cra_driver_name	= "atmel-cfb32-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CFB32_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x3,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 2*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_cfb32_encrypt,
		.decrypt	= atmel_tdes_cfb32_decrypt,
	}
},
{
	.cra_name		= "ofb(des3_ede)",
	.cra_driver_name	= "atmel-ofb-tdes",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0x7,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_u.ablkcipher = {
		.min_keysize	= 2*DES_KEY_SIZE,
		.max_keysize	= 3*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= atmel_tdes_setkey,
		.encrypt	= atmel_tdes_ofb_encrypt,
		.decrypt	= atmel_tdes_ofb_decrypt,
	}
},
};

static void atmel_tdes_queue_task(unsigned long data)
{
	struct atmel_tdes_dev *dd = (struct atmel_tdes_dev *)data;

	atmel_tdes_handle_queue(dd, NULL);
}

static void atmel_tdes_done_task(unsigned long data)
{
	struct atmel_tdes_dev *dd = (struct atmel_tdes_dev *) data;
	int err;

	if (!(dd->flags & TDES_FLAGS_DMA))
		err = atmel_tdes_crypt_pdc_stop(dd);
	else
		err = atmel_tdes_crypt_dma_stop(dd);

	err = dd->err ? : err;

	if (dd->total && !err) {
		if (dd->flags & TDES_FLAGS_FAST) {
			dd->in_sg = sg_next(dd->in_sg);
			dd->out_sg = sg_next(dd->out_sg);
			if (!dd->in_sg || !dd->out_sg)
				err = -EINVAL;
		}
		if (!err)
			err = atmel_tdes_crypt_start(dd);
		if (!err)
			return; /* DMA started. Not fininishing. */
	}

	atmel_tdes_finish_req(dd, err);
	atmel_tdes_handle_queue(dd, NULL);
}

static irqreturn_t atmel_tdes_irq(int irq, void *dev_id)
{
	struct atmel_tdes_dev *tdes_dd = dev_id;
	u32 reg;

	reg = atmel_tdes_read(tdes_dd, TDES_ISR);
	if (reg & atmel_tdes_read(tdes_dd, TDES_IMR)) {
		atmel_tdes_write(tdes_dd, TDES_IDR, reg);
		if (TDES_FLAGS_BUSY & tdes_dd->flags)
			tasklet_schedule(&tdes_dd->done_task);
		else
			dev_warn(tdes_dd->dev, "TDES interrupt when no active requests.\n");
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void atmel_tdes_unregister_algs(struct atmel_tdes_dev *dd)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tdes_algs); i++)
		crypto_unregister_alg(&tdes_algs[i]);
}

static int atmel_tdes_register_algs(struct atmel_tdes_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(tdes_algs); i++) {
		err = crypto_register_alg(&tdes_algs[i]);
		if (err)
			goto err_tdes_algs;
	}

	return 0;

err_tdes_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&tdes_algs[j]);

	return err;
}

static void atmel_tdes_get_cap(struct atmel_tdes_dev *dd)
{

	dd->caps.has_dma = 0;
	dd->caps.has_cfb_3keys = 0;

	/* keep only major version number */
	switch (dd->hw_version & 0xf00) {
	case 0x700:
		dd->caps.has_dma = 1;
		dd->caps.has_cfb_3keys = 1;
		break;
	case 0x600:
		break;
	default:
		dev_warn(dd->dev,
				"Unmanaged tdes version, set minimum capabilities\n");
		break;
	}
}

#if defined(CONFIG_OF)
static const struct of_device_id atmel_tdes_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-tdes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_tdes_dt_ids);

static struct crypto_platform_data *atmel_tdes_of_init(struct platform_device *pdev)
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
static inline struct crypto_platform_data *atmel_tdes_of_init(struct platform_device *pdev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int atmel_tdes_probe(struct platform_device *pdev)
{
	struct atmel_tdes_dev *tdes_dd;
	struct crypto_platform_data	*pdata;
	struct device *dev = &pdev->dev;
	struct resource *tdes_res;
	int err;

	tdes_dd = devm_kmalloc(&pdev->dev, sizeof(*tdes_dd), GFP_KERNEL);
	if (tdes_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto tdes_dd_err;
	}

	tdes_dd->dev = dev;

	platform_set_drvdata(pdev, tdes_dd);

	INIT_LIST_HEAD(&tdes_dd->list);
	spin_lock_init(&tdes_dd->lock);

	tasklet_init(&tdes_dd->done_task, atmel_tdes_done_task,
					(unsigned long)tdes_dd);
	tasklet_init(&tdes_dd->queue_task, atmel_tdes_queue_task,
					(unsigned long)tdes_dd);

	crypto_init_queue(&tdes_dd->queue, ATMEL_TDES_QUEUE_LENGTH);

	/* Get the base address */
	tdes_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!tdes_res) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto res_err;
	}
	tdes_dd->phys_base = tdes_res->start;

	/* Get the IRQ */
	tdes_dd->irq = platform_get_irq(pdev,  0);
	if (tdes_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = tdes_dd->irq;
		goto res_err;
	}

	err = devm_request_irq(&pdev->dev, tdes_dd->irq, atmel_tdes_irq,
			       IRQF_SHARED, "atmel-tdes", tdes_dd);
	if (err) {
		dev_err(dev, "unable to request tdes irq.\n");
		goto res_err;
	}

	/* Initializing the clock */
	tdes_dd->iclk = devm_clk_get(&pdev->dev, "tdes_clk");
	if (IS_ERR(tdes_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(tdes_dd->iclk);
		goto res_err;
	}

	tdes_dd->io_base = devm_ioremap_resource(&pdev->dev, tdes_res);
	if (IS_ERR(tdes_dd->io_base)) {
		dev_err(dev, "can't ioremap\n");
		err = PTR_ERR(tdes_dd->io_base);
		goto res_err;
	}

	atmel_tdes_hw_version_init(tdes_dd);

	atmel_tdes_get_cap(tdes_dd);

	err = atmel_tdes_buff_init(tdes_dd);
	if (err)
		goto err_tdes_buff;

	if (tdes_dd->caps.has_dma) {
		pdata = pdev->dev.platform_data;
		if (!pdata) {
			pdata = atmel_tdes_of_init(pdev);
			if (IS_ERR(pdata)) {
				dev_err(&pdev->dev, "platform data not available\n");
				err = PTR_ERR(pdata);
				goto err_pdata;
			}
		}
		if (!pdata->dma_slave) {
			err = -ENXIO;
			goto err_pdata;
		}
		err = atmel_tdes_dma_init(tdes_dd, pdata);
		if (err)
			goto err_tdes_dma;

		dev_info(dev, "using %s, %s for DMA transfers\n",
				dma_chan_name(tdes_dd->dma_lch_in.chan),
				dma_chan_name(tdes_dd->dma_lch_out.chan));
	}

	spin_lock(&atmel_tdes.lock);
	list_add_tail(&tdes_dd->list, &atmel_tdes.dev_list);
	spin_unlock(&atmel_tdes.lock);

	err = atmel_tdes_register_algs(tdes_dd);
	if (err)
		goto err_algs;

	dev_info(dev, "Atmel DES/TDES\n");

	return 0;

err_algs:
	spin_lock(&atmel_tdes.lock);
	list_del(&tdes_dd->list);
	spin_unlock(&atmel_tdes.lock);
	if (tdes_dd->caps.has_dma)
		atmel_tdes_dma_cleanup(tdes_dd);
err_tdes_dma:
err_pdata:
	atmel_tdes_buff_cleanup(tdes_dd);
err_tdes_buff:
res_err:
	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);
tdes_dd_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int atmel_tdes_remove(struct platform_device *pdev)
{
	struct atmel_tdes_dev *tdes_dd;

	tdes_dd = platform_get_drvdata(pdev);
	if (!tdes_dd)
		return -ENODEV;
	spin_lock(&atmel_tdes.lock);
	list_del(&tdes_dd->list);
	spin_unlock(&atmel_tdes.lock);

	atmel_tdes_unregister_algs(tdes_dd);

	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);

	if (tdes_dd->caps.has_dma)
		atmel_tdes_dma_cleanup(tdes_dd);

	atmel_tdes_buff_cleanup(tdes_dd);

	return 0;
}

static struct platform_driver atmel_tdes_driver = {
	.probe		= atmel_tdes_probe,
	.remove		= atmel_tdes_remove,
	.driver		= {
		.name	= "atmel_tdes",
		.of_match_table = of_match_ptr(atmel_tdes_dt_ids),
	},
};

module_platform_driver(atmel_tdes_driver);

MODULE_DESCRIPTION("Atmel DES/TDES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
