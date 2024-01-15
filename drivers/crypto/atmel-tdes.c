// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * Support for ATMEL DES/TDES HW acceleration.
 *
 * Copyright (c) 2012 Eukréa Electromatique - ATMEL
 * Author: Nicolas Royer <nicolas@eukrea.com>
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
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>
#include "atmel-tdes-regs.h"

#define ATMEL_TDES_PRIORITY	300

/* TDES flags  */
/* Reserve bits [17:16], [13:12], [2:0] for AES Mode Register */
#define TDES_FLAGS_ENCRYPT	TDES_MR_CYPHER_ENC
#define TDES_FLAGS_OPMODE_MASK	(TDES_MR_OPMOD_MASK | TDES_MR_CFBS_MASK)
#define TDES_FLAGS_ECB		TDES_MR_OPMOD_ECB
#define TDES_FLAGS_CBC		TDES_MR_OPMOD_CBC
#define TDES_FLAGS_OFB		TDES_MR_OPMOD_OFB
#define TDES_FLAGS_CFB64	(TDES_MR_OPMOD_CFB | TDES_MR_CFBS_64b)
#define TDES_FLAGS_CFB32	(TDES_MR_OPMOD_CFB | TDES_MR_CFBS_32b)
#define TDES_FLAGS_CFB16	(TDES_MR_OPMOD_CFB | TDES_MR_CFBS_16b)
#define TDES_FLAGS_CFB8		(TDES_MR_OPMOD_CFB | TDES_MR_CFBS_8b)

#define TDES_FLAGS_MODE_MASK	(TDES_FLAGS_OPMODE_MASK | TDES_FLAGS_ENCRYPT)

#define TDES_FLAGS_INIT		BIT(3)
#define TDES_FLAGS_FAST		BIT(4)
#define TDES_FLAGS_BUSY		BIT(5)
#define TDES_FLAGS_DMA		BIT(6)

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
	u32		key[DES3_EDE_KEY_SIZE / sizeof(u32)];
	unsigned long	flags;

	u16		block_size;
};

struct atmel_tdes_reqctx {
	unsigned long mode;
	u8 lastc[DES_BLOCK_SIZE];
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

	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct skcipher_request	*req;
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
			       const u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		atmel_tdes_write(dd, offset, *value);
}

static struct atmel_tdes_dev *atmel_tdes_dev_alloc(void)
{
	struct atmel_tdes_dev *tdes_dd;

	spin_lock_bh(&atmel_tdes.lock);
	/* One TDES IP per SoC. */
	tdes_dd = list_first_entry_or_null(&atmel_tdes.dev_list,
					   struct atmel_tdes_dev, list);
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
	}

	return 0;
}

static inline unsigned int atmel_tdes_get_version(struct atmel_tdes_dev *dd)
{
	return atmel_tdes_read(dd, TDES_HW_VERSION) & 0x00000fff;
}

static int atmel_tdes_hw_version_init(struct atmel_tdes_dev *dd)
{
	int err;

	err = atmel_tdes_hw_init(dd);
	if (err)
		return err;

	dd->hw_version = atmel_tdes_get_version(dd);

	dev_info(dd->dev,
			"version: 0x%x\n", dd->hw_version);

	clk_disable_unprepare(dd->iclk);

	return 0;
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
	u32 valmr = TDES_MR_SMOD_PDC;

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

	valmr |= dd->flags & TDES_FLAGS_MODE_MASK;

	atmel_tdes_write(dd, TDES_MR, valmr);

	atmel_tdes_write_n(dd, TDES_KEY1W1R, dd->ctx->key,
						dd->ctx->keylen >> 2);

	if (dd->req->iv && (valmr & TDES_MR_OPMOD_MASK) != TDES_MR_OPMOD_ECB)
		atmel_tdes_write_n(dd, TDES_IV1R, (void *)dd->req->iv, 2);

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
			dev_dbg(dd->dev, "not all data converted: %zu\n", count);
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
		dev_dbg(dd->dev, "unable to alloc pages.\n");
		goto err_alloc;
	}

	/* MAP here */
	dd->dma_addr_in = dma_map_single(dd->dev, dd->buf_in,
					dd->buflen, DMA_TO_DEVICE);
	err = dma_mapping_error(dd->dev, dd->dma_addr_in);
	if (err) {
		dev_dbg(dd->dev, "dma %zd bytes error\n", dd->buflen);
		goto err_map_in;
	}

	dd->dma_addr_out = dma_map_single(dd->dev, dd->buf_out,
					dd->buflen, DMA_FROM_DEVICE);
	err = dma_mapping_error(dd->dev, dd->dma_addr_out);
	if (err) {
		dev_dbg(dd->dev, "dma %zd bytes error\n", dd->buflen);
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

static int atmel_tdes_crypt_pdc(struct atmel_tdes_dev *dd,
				dma_addr_t dma_addr_in,
				dma_addr_t dma_addr_out, int length)
{
	struct atmel_tdes_reqctx *rctx = skcipher_request_ctx(dd->req);
	int len32;

	dd->dma_size = length;

	if (!(dd->flags & TDES_FLAGS_FAST)) {
		dma_sync_single_for_device(dd->dev, dma_addr_in, length,
					   DMA_TO_DEVICE);
	}

	switch (rctx->mode & TDES_FLAGS_OPMODE_MASK) {
	case TDES_FLAGS_CFB8:
		len32 = DIV_ROUND_UP(length, sizeof(u8));
		break;

	case TDES_FLAGS_CFB16:
		len32 = DIV_ROUND_UP(length, sizeof(u16));
		break;

	default:
		len32 = DIV_ROUND_UP(length, sizeof(u32));
		break;
	}

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

static int atmel_tdes_crypt_dma(struct atmel_tdes_dev *dd,
				dma_addr_t dma_addr_in,
				dma_addr_t dma_addr_out, int length)
{
	struct atmel_tdes_reqctx *rctx = skcipher_request_ctx(dd->req);
	struct scatterlist sg[2];
	struct dma_async_tx_descriptor	*in_desc, *out_desc;
	enum dma_slave_buswidth addr_width;

	dd->dma_size = length;

	if (!(dd->flags & TDES_FLAGS_FAST)) {
		dma_sync_single_for_device(dd->dev, dma_addr_in, length,
					   DMA_TO_DEVICE);
	}

	switch (rctx->mode & TDES_FLAGS_OPMODE_MASK) {
	case TDES_FLAGS_CFB8:
		addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;

	case TDES_FLAGS_CFB16:
		addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;

	default:
		addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	}

	dd->dma_lch_in.dma_conf.dst_addr_width = addr_width;
	dd->dma_lch_out.dma_conf.src_addr_width = addr_width;

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
			dev_dbg(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}

		err = dma_map_sg(dd->dev, dd->out_sg, 1,
				DMA_FROM_DEVICE);
		if (!err) {
			dev_dbg(dd->dev, "dma_map_sg() error\n");
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
		err = atmel_tdes_crypt_dma(dd, addr_in, addr_out, count);
	else
		err = atmel_tdes_crypt_pdc(dd, addr_in, addr_out, count);

	if (err && (dd->flags & TDES_FLAGS_FAST)) {
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_TO_DEVICE);
	}

	return err;
}

static void
atmel_tdes_set_iv_as_last_ciphertext_block(struct atmel_tdes_dev *dd)
{
	struct skcipher_request *req = dd->req;
	struct atmel_tdes_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	unsigned int ivsize = crypto_skcipher_ivsize(skcipher);

	if (req->cryptlen < ivsize)
		return;

	if (rctx->mode & TDES_FLAGS_ENCRYPT)
		scatterwalk_map_and_copy(req->iv, req->dst,
					 req->cryptlen - ivsize, ivsize, 0);
	else
		memcpy(req->iv, rctx->lastc, ivsize);

}

static void atmel_tdes_finish_req(struct atmel_tdes_dev *dd, int err)
{
	struct skcipher_request *req = dd->req;
	struct atmel_tdes_reqctx *rctx = skcipher_request_ctx(req);

	clk_disable_unprepare(dd->iclk);

	dd->flags &= ~TDES_FLAGS_BUSY;

	if (!err && (rctx->mode & TDES_FLAGS_OPMODE_MASK) != TDES_FLAGS_ECB)
		atmel_tdes_set_iv_as_last_ciphertext_block(dd);

	skcipher_request_complete(req, err);
}

static int atmel_tdes_handle_queue(struct atmel_tdes_dev *dd,
			       struct skcipher_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct atmel_tdes_ctx *ctx;
	struct atmel_tdes_reqctx *rctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = crypto_enqueue_request(&dd->queue, &req->base);
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
		crypto_request_complete(backlog, -EINPROGRESS);

	req = skcipher_request_cast(async_req);

	/* assign new request to device */
	dd->req = req;
	dd->total = req->cryptlen;
	dd->in_offset = 0;
	dd->in_sg = req->src;
	dd->out_offset = 0;
	dd->out_sg = req->dst;

	rctx = skcipher_request_ctx(req);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	rctx->mode &= TDES_FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~TDES_FLAGS_MODE_MASK) | rctx->mode;
	dd->ctx = ctx;

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
				dev_dbg(dd->dev, "not all data converted: %zu\n", count);
			}
		}
	}
	return err;
}

static int atmel_tdes_crypt(struct skcipher_request *req, unsigned long mode)
{
	struct crypto_skcipher *skcipher = crypto_skcipher_reqtfm(req);
	struct atmel_tdes_ctx *ctx = crypto_skcipher_ctx(skcipher);
	struct atmel_tdes_reqctx *rctx = skcipher_request_ctx(req);
	struct device *dev = ctx->dd->dev;

	if (!req->cryptlen)
		return 0;

	switch (mode & TDES_FLAGS_OPMODE_MASK) {
	case TDES_FLAGS_CFB8:
		if (!IS_ALIGNED(req->cryptlen, CFB8_BLOCK_SIZE)) {
			dev_dbg(dev, "request size is not exact amount of CFB8 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB8_BLOCK_SIZE;
		break;

	case TDES_FLAGS_CFB16:
		if (!IS_ALIGNED(req->cryptlen, CFB16_BLOCK_SIZE)) {
			dev_dbg(dev, "request size is not exact amount of CFB16 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB16_BLOCK_SIZE;
		break;

	case TDES_FLAGS_CFB32:
		if (!IS_ALIGNED(req->cryptlen, CFB32_BLOCK_SIZE)) {
			dev_dbg(dev, "request size is not exact amount of CFB32 blocks\n");
			return -EINVAL;
		}
		ctx->block_size = CFB32_BLOCK_SIZE;
		break;

	default:
		if (!IS_ALIGNED(req->cryptlen, DES_BLOCK_SIZE)) {
			dev_dbg(dev, "request size is not exact amount of DES blocks\n");
			return -EINVAL;
		}
		ctx->block_size = DES_BLOCK_SIZE;
		break;
	}

	rctx->mode = mode;

	if ((mode & TDES_FLAGS_OPMODE_MASK) != TDES_FLAGS_ECB &&
	    !(mode & TDES_FLAGS_ENCRYPT)) {
		unsigned int ivsize = crypto_skcipher_ivsize(skcipher);

		if (req->cryptlen >= ivsize)
			scatterwalk_map_and_copy(rctx->lastc, req->src,
						 req->cryptlen - ivsize,
						 ivsize, 0);
	}

	return atmel_tdes_handle_queue(ctx->dd, req);
}

static int atmel_tdes_dma_init(struct atmel_tdes_dev *dd)
{
	int ret;

	/* Try to grab 2 DMA channels */
	dd->dma_lch_in.chan = dma_request_chan(dd->dev, "tx");
	if (IS_ERR(dd->dma_lch_in.chan)) {
		ret = PTR_ERR(dd->dma_lch_in.chan);
		goto err_dma_in;
	}

	dd->dma_lch_in.dma_conf.dst_addr = dd->phys_base +
		TDES_IDATA1R;
	dd->dma_lch_in.dma_conf.src_maxburst = 1;
	dd->dma_lch_in.dma_conf.src_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.dst_maxburst = 1;
	dd->dma_lch_in.dma_conf.dst_addr_width =
		DMA_SLAVE_BUSWIDTH_4_BYTES;
	dd->dma_lch_in.dma_conf.device_fc = false;

	dd->dma_lch_out.chan = dma_request_chan(dd->dev, "rx");
	if (IS_ERR(dd->dma_lch_out.chan)) {
		ret = PTR_ERR(dd->dma_lch_out.chan);
		goto err_dma_out;
	}

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
	dev_err(dd->dev, "no DMA channel available\n");
	return ret;
}

static void atmel_tdes_dma_cleanup(struct atmel_tdes_dev *dd)
{
	dma_release_channel(dd->dma_lch_in.chan);
	dma_release_channel(dd->dma_lch_out.chan);
}

static int atmel_des_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct atmel_tdes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = verify_skcipher_des_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_tdes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct atmel_tdes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = verify_skcipher_des3_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int atmel_tdes_ecb_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ECB | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_ecb_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_ECB);
}

static int atmel_tdes_cbc_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CBC | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_cbc_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CBC);
}
static int atmel_tdes_cfb_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB64 | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_cfb_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB64);
}

static int atmel_tdes_cfb8_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB8 | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_cfb8_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB8);
}

static int atmel_tdes_cfb16_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB16 | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_cfb16_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB16);
}

static int atmel_tdes_cfb32_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB32 | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_cfb32_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_CFB32);
}

static int atmel_tdes_ofb_encrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_OFB | TDES_FLAGS_ENCRYPT);
}

static int atmel_tdes_ofb_decrypt(struct skcipher_request *req)
{
	return atmel_tdes_crypt(req, TDES_FLAGS_OFB);
}

static int atmel_tdes_init_tfm(struct crypto_skcipher *tfm)
{
	struct atmel_tdes_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->dd = atmel_tdes_dev_alloc();
	if (!ctx->dd)
		return -ENODEV;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct atmel_tdes_reqctx));

	return 0;
}

static void atmel_tdes_skcipher_alg_init(struct skcipher_alg *alg)
{
	alg->base.cra_priority = ATMEL_TDES_PRIORITY;
	alg->base.cra_flags = CRYPTO_ALG_ASYNC;
	alg->base.cra_ctxsize = sizeof(struct atmel_tdes_ctx);
	alg->base.cra_module = THIS_MODULE;

	alg->init = atmel_tdes_init_tfm;
}

static struct skcipher_alg tdes_algs[] = {
{
	.base.cra_name		= "ecb(des)",
	.base.cra_driver_name	= "atmel-ecb-des",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_ecb_encrypt,
	.decrypt		= atmel_tdes_ecb_decrypt,
},
{
	.base.cra_name		= "cbc(des)",
	.base.cra_driver_name	= "atmel-cbc-des",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_cbc_encrypt,
	.decrypt		= atmel_tdes_cbc_decrypt,
},
{
	.base.cra_name		= "cfb(des)",
	.base.cra_driver_name	= "atmel-cfb-des",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_cfb_encrypt,
	.decrypt		= atmel_tdes_cfb_decrypt,
},
{
	.base.cra_name		= "cfb8(des)",
	.base.cra_driver_name	= "atmel-cfb8-des",
	.base.cra_blocksize	= CFB8_BLOCK_SIZE,
	.base.cra_alignmask	= 0,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_cfb8_encrypt,
	.decrypt		= atmel_tdes_cfb8_decrypt,
},
{
	.base.cra_name		= "cfb16(des)",
	.base.cra_driver_name	= "atmel-cfb16-des",
	.base.cra_blocksize	= CFB16_BLOCK_SIZE,
	.base.cra_alignmask	= 0x1,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_cfb16_encrypt,
	.decrypt		= atmel_tdes_cfb16_decrypt,
},
{
	.base.cra_name		= "cfb32(des)",
	.base.cra_driver_name	= "atmel-cfb32-des",
	.base.cra_blocksize	= CFB32_BLOCK_SIZE,
	.base.cra_alignmask	= 0x3,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_cfb32_encrypt,
	.decrypt		= atmel_tdes_cfb32_decrypt,
},
{
	.base.cra_name		= "ofb(des)",
	.base.cra_driver_name	= "atmel-ofb-des",
	.base.cra_blocksize	= 1,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES_KEY_SIZE,
	.max_keysize		= DES_KEY_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= atmel_des_setkey,
	.encrypt		= atmel_tdes_ofb_encrypt,
	.decrypt		= atmel_tdes_ofb_decrypt,
},
{
	.base.cra_name		= "ecb(des3_ede)",
	.base.cra_driver_name	= "atmel-ecb-tdes",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES3_EDE_KEY_SIZE,
	.max_keysize		= DES3_EDE_KEY_SIZE,
	.setkey			= atmel_tdes_setkey,
	.encrypt		= atmel_tdes_ecb_encrypt,
	.decrypt		= atmel_tdes_ecb_decrypt,
},
{
	.base.cra_name		= "cbc(des3_ede)",
	.base.cra_driver_name	= "atmel-cbc-tdes",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES3_EDE_KEY_SIZE,
	.max_keysize		= DES3_EDE_KEY_SIZE,
	.setkey			= atmel_tdes_setkey,
	.encrypt		= atmel_tdes_cbc_encrypt,
	.decrypt		= atmel_tdes_cbc_decrypt,
	.ivsize			= DES_BLOCK_SIZE,
},
{
	.base.cra_name		= "ofb(des3_ede)",
	.base.cra_driver_name	= "atmel-ofb-tdes",
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_alignmask	= 0x7,

	.min_keysize		= DES3_EDE_KEY_SIZE,
	.max_keysize		= DES3_EDE_KEY_SIZE,
	.setkey			= atmel_tdes_setkey,
	.encrypt		= atmel_tdes_ofb_encrypt,
	.decrypt		= atmel_tdes_ofb_decrypt,
	.ivsize			= DES_BLOCK_SIZE,
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
		crypto_unregister_skcipher(&tdes_algs[i]);
}

static int atmel_tdes_register_algs(struct atmel_tdes_dev *dd)
{
	int err, i, j;

	for (i = 0; i < ARRAY_SIZE(tdes_algs); i++) {
		atmel_tdes_skcipher_alg_init(&tdes_algs[i]);

		err = crypto_register_skcipher(&tdes_algs[i]);
		if (err)
			goto err_tdes_algs;
	}

	return 0;

err_tdes_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_skcipher(&tdes_algs[j]);

	return err;
}

static void atmel_tdes_get_cap(struct atmel_tdes_dev *dd)
{

	dd->caps.has_dma = 0;
	dd->caps.has_cfb_3keys = 0;

	/* keep only major version number */
	switch (dd->hw_version & 0xf00) {
	case 0x800:
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

static const struct of_device_id atmel_tdes_dt_ids[] = {
	{ .compatible = "atmel,at91sam9g46-tdes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atmel_tdes_dt_ids);

static int atmel_tdes_probe(struct platform_device *pdev)
{
	struct atmel_tdes_dev *tdes_dd;
	struct device *dev = &pdev->dev;
	struct resource *tdes_res;
	int err;

	tdes_dd = devm_kmalloc(&pdev->dev, sizeof(*tdes_dd), GFP_KERNEL);
	if (!tdes_dd)
		return -ENOMEM;

	tdes_dd->dev = dev;

	platform_set_drvdata(pdev, tdes_dd);

	INIT_LIST_HEAD(&tdes_dd->list);
	spin_lock_init(&tdes_dd->lock);

	tasklet_init(&tdes_dd->done_task, atmel_tdes_done_task,
					(unsigned long)tdes_dd);
	tasklet_init(&tdes_dd->queue_task, atmel_tdes_queue_task,
					(unsigned long)tdes_dd);

	crypto_init_queue(&tdes_dd->queue, ATMEL_TDES_QUEUE_LENGTH);

	tdes_dd->io_base = devm_platform_get_and_ioremap_resource(pdev, 0, &tdes_res);
	if (IS_ERR(tdes_dd->io_base)) {
		err = PTR_ERR(tdes_dd->io_base);
		goto err_tasklet_kill;
	}
	tdes_dd->phys_base = tdes_res->start;

	/* Get the IRQ */
	tdes_dd->irq = platform_get_irq(pdev,  0);
	if (tdes_dd->irq < 0) {
		err = tdes_dd->irq;
		goto err_tasklet_kill;
	}

	err = devm_request_irq(&pdev->dev, tdes_dd->irq, atmel_tdes_irq,
			       IRQF_SHARED, "atmel-tdes", tdes_dd);
	if (err) {
		dev_err(dev, "unable to request tdes irq.\n");
		goto err_tasklet_kill;
	}

	/* Initializing the clock */
	tdes_dd->iclk = devm_clk_get(&pdev->dev, "tdes_clk");
	if (IS_ERR(tdes_dd->iclk)) {
		dev_err(dev, "clock initialization failed.\n");
		err = PTR_ERR(tdes_dd->iclk);
		goto err_tasklet_kill;
	}

	err = atmel_tdes_hw_version_init(tdes_dd);
	if (err)
		goto err_tasklet_kill;

	atmel_tdes_get_cap(tdes_dd);

	err = atmel_tdes_buff_init(tdes_dd);
	if (err)
		goto err_tasklet_kill;

	if (tdes_dd->caps.has_dma) {
		err = atmel_tdes_dma_init(tdes_dd);
		if (err)
			goto err_buff_cleanup;

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
err_buff_cleanup:
	atmel_tdes_buff_cleanup(tdes_dd);
err_tasklet_kill:
	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);

	return err;
}

static void atmel_tdes_remove(struct platform_device *pdev)
{
	struct atmel_tdes_dev *tdes_dd = platform_get_drvdata(pdev);

	spin_lock(&atmel_tdes.lock);
	list_del(&tdes_dd->list);
	spin_unlock(&atmel_tdes.lock);

	atmel_tdes_unregister_algs(tdes_dd);

	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);

	if (tdes_dd->caps.has_dma)
		atmel_tdes_dma_cleanup(tdes_dd);

	atmel_tdes_buff_cleanup(tdes_dd);
}

static struct platform_driver atmel_tdes_driver = {
	.probe		= atmel_tdes_probe,
	.remove_new	= atmel_tdes_remove,
	.driver		= {
		.name	= "atmel_tdes",
		.of_match_table = atmel_tdes_dt_ids,
	},
};

module_platform_driver(atmel_tdes_driver);

MODULE_DESCRIPTION("Atmel DES/TDES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
