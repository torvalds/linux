/*
 * Cryptographic API.
 *
 * Support for OMAP AES HW acceleration.
 *
 * Copyright (c) 2010 Nokia Corporation
 * Author: Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <crypto/scatterwalk.h>
#include <crypto/aes.h>

#include <linux/omap-dma.h>

/* OMAP TRM gives bitfields as start:end, where start is the higher bit
   number. For example 7:0 */
#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))

#define AES_REG_KEY(x)			(0x1C - ((x ^ 0x01) * 0x04))
#define AES_REG_IV(x)			(0x20 + ((x) * 0x04))

#define AES_REG_CTRL			0x30
#define AES_REG_CTRL_CTR_WIDTH		(1 << 7)
#define AES_REG_CTRL_CTR		(1 << 6)
#define AES_REG_CTRL_CBC		(1 << 5)
#define AES_REG_CTRL_KEY_SIZE		(3 << 3)
#define AES_REG_CTRL_DIRECTION		(1 << 2)
#define AES_REG_CTRL_INPUT_READY	(1 << 1)
#define AES_REG_CTRL_OUTPUT_READY	(1 << 0)

#define AES_REG_DATA			0x34
#define AES_REG_DATA_N(x)		(0x34 + ((x) * 0x04))

#define AES_REG_REV			0x44
#define AES_REG_REV_MAJOR		0xF0
#define AES_REG_REV_MINOR		0x0F

#define AES_REG_MASK			0x48
#define AES_REG_MASK_SIDLE		(1 << 6)
#define AES_REG_MASK_START		(1 << 5)
#define AES_REG_MASK_DMA_OUT_EN		(1 << 3)
#define AES_REG_MASK_DMA_IN_EN		(1 << 2)
#define AES_REG_MASK_SOFTRESET		(1 << 1)
#define AES_REG_AUTOIDLE		(1 << 0)

#define AES_REG_SYSSTATUS		0x4C
#define AES_REG_SYSSTATUS_RESETDONE	(1 << 0)

#define DEFAULT_TIMEOUT		(5*HZ)

#define FLAGS_MODE_MASK		0x000f
#define FLAGS_ENCRYPT		BIT(0)
#define FLAGS_CBC		BIT(1)
#define FLAGS_GIV		BIT(2)

#define FLAGS_INIT		BIT(4)
#define FLAGS_FAST		BIT(5)
#define FLAGS_BUSY		BIT(6)

struct omap_aes_ctx {
	struct omap_aes_dev *dd;

	int		keylen;
	u32		key[AES_KEYSIZE_256 / sizeof(u32)];
	unsigned long	flags;
};

struct omap_aes_reqctx {
	unsigned long mode;
};

#define OMAP_AES_QUEUE_LENGTH	1
#define OMAP_AES_CACHE_SIZE	0

struct omap_aes_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;
	struct clk		*iclk;
	struct omap_aes_ctx	*ctx;
	struct device		*dev;
	unsigned long		flags;
	int			err;

	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct ablkcipher_request	*req;
	size_t				total;
	struct scatterlist		*in_sg;
	size_t				in_offset;
	struct scatterlist		*out_sg;
	size_t				out_offset;

	size_t			buflen;
	void			*buf_in;
	size_t			dma_size;
	int			dma_in;
	int			dma_lch_in;
	dma_addr_t		dma_addr_in;
	void			*buf_out;
	int			dma_out;
	int			dma_lch_out;
	dma_addr_t		dma_addr_out;
};

/* keep registered devices data here */
static LIST_HEAD(dev_list);
static DEFINE_SPINLOCK(list_lock);

static inline u32 omap_aes_read(struct omap_aes_dev *dd, u32 offset)
{
	return __raw_readl(dd->io_base + offset);
}

static inline void omap_aes_write(struct omap_aes_dev *dd, u32 offset,
				  u32 value)
{
	__raw_writel(value, dd->io_base + offset);
}

static inline void omap_aes_write_mask(struct omap_aes_dev *dd, u32 offset,
					u32 value, u32 mask)
{
	u32 val;

	val = omap_aes_read(dd, offset);
	val &= ~mask;
	val |= value;
	omap_aes_write(dd, offset, val);
}

static void omap_aes_write_n(struct omap_aes_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		omap_aes_write(dd, offset, *value);
}

static int omap_aes_hw_init(struct omap_aes_dev *dd)
{
	/*
	 * clocks are enabled when request starts and disabled when finished.
	 * It may be long delays between requests.
	 * Device might go to off mode to save power.
	 */
	clk_enable(dd->iclk);

	if (!(dd->flags & FLAGS_INIT)) {
		dd->flags |= FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static int omap_aes_write_ctrl(struct omap_aes_dev *dd)
{
	unsigned int key32;
	int i, err;
	u32 val, mask;

	err = omap_aes_hw_init(dd);
	if (err)
		return err;

	val = 0;
	if (dd->dma_lch_out >= 0)
		val |= AES_REG_MASK_DMA_OUT_EN;
	if (dd->dma_lch_in >= 0)
		val |= AES_REG_MASK_DMA_IN_EN;

	mask = AES_REG_MASK_DMA_IN_EN | AES_REG_MASK_DMA_OUT_EN;

	omap_aes_write_mask(dd, AES_REG_MASK, val, mask);

	key32 = dd->ctx->keylen / sizeof(u32);

	/* it seems a key should always be set even if it has not changed */
	for (i = 0; i < key32; i++) {
		omap_aes_write(dd, AES_REG_KEY(i),
			__le32_to_cpu(dd->ctx->key[i]));
	}

	if ((dd->flags & FLAGS_CBC) && dd->req->info)
		omap_aes_write_n(dd, AES_REG_IV(0), dd->req->info, 4);

	val = FLD_VAL(((dd->ctx->keylen >> 3) - 1), 4, 3);
	if (dd->flags & FLAGS_CBC)
		val |= AES_REG_CTRL_CBC;
	if (dd->flags & FLAGS_ENCRYPT)
		val |= AES_REG_CTRL_DIRECTION;

	mask = AES_REG_CTRL_CBC | AES_REG_CTRL_DIRECTION |
			AES_REG_CTRL_KEY_SIZE;

	omap_aes_write_mask(dd, AES_REG_CTRL, val, mask);

	/* IN */
	omap_set_dma_dest_params(dd->dma_lch_in, 0, OMAP_DMA_AMODE_CONSTANT,
				 dd->phys_base + AES_REG_DATA, 0, 4);

	omap_set_dma_dest_burst_mode(dd->dma_lch_in, OMAP_DMA_DATA_BURST_4);
	omap_set_dma_src_burst_mode(dd->dma_lch_in, OMAP_DMA_DATA_BURST_4);

	/* OUT */
	omap_set_dma_src_params(dd->dma_lch_out, 0, OMAP_DMA_AMODE_CONSTANT,
				dd->phys_base + AES_REG_DATA, 0, 4);

	omap_set_dma_src_burst_mode(dd->dma_lch_out, OMAP_DMA_DATA_BURST_4);
	omap_set_dma_dest_burst_mode(dd->dma_lch_out, OMAP_DMA_DATA_BURST_4);

	return 0;
}

static struct omap_aes_dev *omap_aes_find_dev(struct omap_aes_ctx *ctx)
{
	struct omap_aes_dev *dd = NULL, *tmp;

	spin_lock_bh(&list_lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &dev_list, list) {
			/* FIXME: take fist available aes core */
			dd = tmp;
			break;
		}
		ctx->dd = dd;
	} else {
		/* already found before */
		dd = ctx->dd;
	}
	spin_unlock_bh(&list_lock);

	return dd;
}

static void omap_aes_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_aes_dev *dd = data;

	if (ch_status != OMAP_DMA_BLOCK_IRQ) {
		pr_err("omap-aes DMA error status: 0x%hx\n", ch_status);
		dd->err = -EIO;
		dd->flags &= ~FLAGS_INIT; /* request to re-initialize */
	} else if (lch == dd->dma_lch_in) {
		return;
	}

	/* dma_lch_out - completed */
	tasklet_schedule(&dd->done_task);
}

static int omap_aes_dma_init(struct omap_aes_dev *dd)
{
	int err = -ENOMEM;

	dd->dma_lch_out = -1;
	dd->dma_lch_in = -1;

	dd->buf_in = (void *)__get_free_pages(GFP_KERNEL, OMAP_AES_CACHE_SIZE);
	dd->buf_out = (void *)__get_free_pages(GFP_KERNEL, OMAP_AES_CACHE_SIZE);
	dd->buflen = PAGE_SIZE << OMAP_AES_CACHE_SIZE;
	dd->buflen &= ~(AES_BLOCK_SIZE - 1);

	if (!dd->buf_in || !dd->buf_out) {
		dev_err(dd->dev, "unable to alloc pages.\n");
		goto err_alloc;
	}

	/* MAP here */
	dd->dma_addr_in = dma_map_single(dd->dev, dd->buf_in, dd->buflen,
					 DMA_TO_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_in)) {
		dev_err(dd->dev, "dma %d bytes error\n", dd->buflen);
		err = -EINVAL;
		goto err_map_in;
	}

	dd->dma_addr_out = dma_map_single(dd->dev, dd->buf_out, dd->buflen,
					  DMA_FROM_DEVICE);
	if (dma_mapping_error(dd->dev, dd->dma_addr_out)) {
		dev_err(dd->dev, "dma %d bytes error\n", dd->buflen);
		err = -EINVAL;
		goto err_map_out;
	}

	err = omap_request_dma(dd->dma_in, "omap-aes-rx",
			       omap_aes_dma_callback, dd, &dd->dma_lch_in);
	if (err) {
		dev_err(dd->dev, "Unable to request DMA channel\n");
		goto err_dma_in;
	}
	err = omap_request_dma(dd->dma_out, "omap-aes-tx",
			       omap_aes_dma_callback, dd, &dd->dma_lch_out);
	if (err) {
		dev_err(dd->dev, "Unable to request DMA channel\n");
		goto err_dma_out;
	}

	return 0;

err_dma_out:
	omap_free_dma(dd->dma_lch_in);
err_dma_in:
	dma_unmap_single(dd->dev, dd->dma_addr_out, dd->buflen,
			 DMA_FROM_DEVICE);
err_map_out:
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen, DMA_TO_DEVICE);
err_map_in:
	free_pages((unsigned long)dd->buf_out, OMAP_AES_CACHE_SIZE);
	free_pages((unsigned long)dd->buf_in, OMAP_AES_CACHE_SIZE);
err_alloc:
	if (err)
		pr_err("error: %d\n", err);
	return err;
}

static void omap_aes_dma_cleanup(struct omap_aes_dev *dd)
{
	omap_free_dma(dd->dma_lch_out);
	omap_free_dma(dd->dma_lch_in);
	dma_unmap_single(dd->dev, dd->dma_addr_out, dd->buflen,
			 DMA_FROM_DEVICE);
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen, DMA_TO_DEVICE);
	free_pages((unsigned long)dd->buf_out, OMAP_AES_CACHE_SIZE);
	free_pages((unsigned long)dd->buf_in, OMAP_AES_CACHE_SIZE);
}

static void sg_copy_buf(void *buf, struct scatterlist *sg,
			      unsigned int start, unsigned int nbytes, int out)
{
	struct scatter_walk walk;

	if (!nbytes)
		return;

	scatterwalk_start(&walk, sg);
	scatterwalk_advance(&walk, start);
	scatterwalk_copychunks(buf, &walk, nbytes, out);
	scatterwalk_done(&walk, out, 0);
}

static int sg_copy(struct scatterlist **sg, size_t *offset, void *buf,
		   size_t buflen, size_t total, int out)
{
	unsigned int count, off = 0;

	while (buflen && total) {
		count = min((*sg)->length - *offset, total);
		count = min(count, buflen);

		if (!count)
			return off;

		/*
		 * buflen and total are AES_BLOCK_SIZE size aligned,
		 * so count should be also aligned
		 */

		sg_copy_buf(buf + off, *sg, *offset, count, out);

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

static int omap_aes_crypt_dma(struct crypto_tfm *tfm, dma_addr_t dma_addr_in,
			       dma_addr_t dma_addr_out, int length)
{
	struct omap_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	struct omap_aes_dev *dd = ctx->dd;
	int len32;

	pr_debug("len: %d\n", length);

	dd->dma_size = length;

	if (!(dd->flags & FLAGS_FAST))
		dma_sync_single_for_device(dd->dev, dma_addr_in, length,
					   DMA_TO_DEVICE);

	len32 = DIV_ROUND_UP(length, sizeof(u32));

	/* IN */
	omap_set_dma_transfer_params(dd->dma_lch_in, OMAP_DMA_DATA_TYPE_S32,
				     len32, 1, OMAP_DMA_SYNC_PACKET, dd->dma_in,
					OMAP_DMA_DST_SYNC);

	omap_set_dma_src_params(dd->dma_lch_in, 0, OMAP_DMA_AMODE_POST_INC,
				dma_addr_in, 0, 0);

	/* OUT */
	omap_set_dma_transfer_params(dd->dma_lch_out, OMAP_DMA_DATA_TYPE_S32,
				     len32, 1, OMAP_DMA_SYNC_PACKET,
					dd->dma_out, OMAP_DMA_SRC_SYNC);

	omap_set_dma_dest_params(dd->dma_lch_out, 0, OMAP_DMA_AMODE_POST_INC,
				 dma_addr_out, 0, 0);

	omap_start_dma(dd->dma_lch_in);
	omap_start_dma(dd->dma_lch_out);

	/* start DMA or disable idle mode */
	omap_aes_write_mask(dd, AES_REG_MASK, AES_REG_MASK_START,
			    AES_REG_MASK_START);

	return 0;
}

static int omap_aes_crypt_dma_start(struct omap_aes_dev *dd)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(
					crypto_ablkcipher_reqtfm(dd->req));
	int err, fast = 0, in, out;
	size_t count;
	dma_addr_t addr_in, addr_out;

	pr_debug("total: %d\n", dd->total);

	if (sg_is_last(dd->in_sg) && sg_is_last(dd->out_sg)) {
		/* check for alignment */
		in = IS_ALIGNED((u32)dd->in_sg->offset, sizeof(u32));
		out = IS_ALIGNED((u32)dd->out_sg->offset, sizeof(u32));

		fast = in && out;
	}

	if (fast)  {
		count = min(dd->total, sg_dma_len(dd->in_sg));
		count = min(count, sg_dma_len(dd->out_sg));

		if (count != dd->total) {
			pr_err("request length != buffer length\n");
			return -EINVAL;
		}

		pr_debug("fast\n");

		err = dma_map_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}

		err = dma_map_sg(dd->dev, dd->out_sg, 1, DMA_FROM_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
			return -EINVAL;
		}

		addr_in = sg_dma_address(dd->in_sg);
		addr_out = sg_dma_address(dd->out_sg);

		dd->flags |= FLAGS_FAST;

	} else {
		/* use cache buffers */
		count = sg_copy(&dd->in_sg, &dd->in_offset, dd->buf_in,
				 dd->buflen, dd->total, 0);

		addr_in = dd->dma_addr_in;
		addr_out = dd->dma_addr_out;

		dd->flags &= ~FLAGS_FAST;

	}

	dd->total -= count;

	err = omap_aes_crypt_dma(tfm, addr_in, addr_out, count);
	if (err) {
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_TO_DEVICE);
	}

	return err;
}

static void omap_aes_finish_req(struct omap_aes_dev *dd, int err)
{
	struct ablkcipher_request *req = dd->req;

	pr_debug("err: %d\n", err);

	clk_disable(dd->iclk);
	dd->flags &= ~FLAGS_BUSY;

	req->base.complete(&req->base, err);
}

static int omap_aes_crypt_dma_stop(struct omap_aes_dev *dd)
{
	int err = 0;
	size_t count;

	pr_debug("total: %d\n", dd->total);

	omap_aes_write_mask(dd, AES_REG_MASK, 0, AES_REG_MASK_START);

	omap_stop_dma(dd->dma_lch_in);
	omap_stop_dma(dd->dma_lch_out);

	if (dd->flags & FLAGS_FAST) {
		dma_unmap_sg(dd->dev, dd->out_sg, 1, DMA_FROM_DEVICE);
		dma_unmap_sg(dd->dev, dd->in_sg, 1, DMA_TO_DEVICE);
	} else {
		dma_sync_single_for_device(dd->dev, dd->dma_addr_out,
					   dd->dma_size, DMA_FROM_DEVICE);

		/* copy data */
		count = sg_copy(&dd->out_sg, &dd->out_offset, dd->buf_out,
				 dd->buflen, dd->dma_size, 1);
		if (count != dd->dma_size) {
			err = -EINVAL;
			pr_err("not all data converted: %u\n", count);
		}
	}

	return err;
}

static int omap_aes_handle_queue(struct omap_aes_dev *dd,
			       struct ablkcipher_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct omap_aes_ctx *ctx;
	struct omap_aes_reqctx *rctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = ablkcipher_enqueue_request(&dd->queue, req);
	if (dd->flags & FLAGS_BUSY) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (async_req)
		dd->flags |= FLAGS_BUSY;
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
	rctx->mode &= FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	dd->ctx = ctx;
	ctx->dd = dd;

	err = omap_aes_write_ctrl(dd);
	if (!err)
		err = omap_aes_crypt_dma_start(dd);
	if (err) {
		/* aes_task will not finish it, so do it here */
		omap_aes_finish_req(dd, err);
		tasklet_schedule(&dd->queue_task);
	}

	return ret; /* return ret, which is enqueue return value */
}

static void omap_aes_done_task(unsigned long data)
{
	struct omap_aes_dev *dd = (struct omap_aes_dev *)data;
	int err;

	pr_debug("enter\n");

	err = omap_aes_crypt_dma_stop(dd);

	err = dd->err ? : err;

	if (dd->total && !err) {
		err = omap_aes_crypt_dma_start(dd);
		if (!err)
			return; /* DMA started. Not fininishing. */
	}

	omap_aes_finish_req(dd, err);
	omap_aes_handle_queue(dd, NULL);

	pr_debug("exit\n");
}

static void omap_aes_queue_task(unsigned long data)
{
	struct omap_aes_dev *dd = (struct omap_aes_dev *)data;

	omap_aes_handle_queue(dd, NULL);
}

static int omap_aes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct omap_aes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct omap_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	struct omap_aes_dev *dd;

	pr_debug("nbytes: %d, enc: %d, cbc: %d\n", req->nbytes,
		  !!(mode & FLAGS_ENCRYPT),
		  !!(mode & FLAGS_CBC));

	if (!IS_ALIGNED(req->nbytes, AES_BLOCK_SIZE)) {
		pr_err("request size is not exact amount of AES blocks\n");
		return -EINVAL;
	}

	dd = omap_aes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx->mode = mode;

	return omap_aes_handle_queue(dd, req);
}

/* ********************** ALG API ************************************ */

static int omap_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct omap_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
		   keylen != AES_KEYSIZE_256)
		return -EINVAL;

	pr_debug("enter, keylen: %d\n", keylen);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int omap_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_ENCRYPT);
}

static int omap_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return omap_aes_crypt(req, 0);
}

static int omap_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_ENCRYPT | FLAGS_CBC);
}

static int omap_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_CBC);
}

static int omap_aes_cra_init(struct crypto_tfm *tfm)
{
	pr_debug("enter\n");

	tfm->crt_ablkcipher.reqsize = sizeof(struct omap_aes_reqctx);

	return 0;
}

static void omap_aes_cra_exit(struct crypto_tfm *tfm)
{
	pr_debug("enter\n");
}

/* ********************** ALGS ************************************ */

static struct crypto_alg algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "ecb-aes-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_aes_cra_init,
	.cra_exit		= omap_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= omap_aes_setkey,
		.encrypt	= omap_aes_ecb_encrypt,
		.decrypt	= omap_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "cbc-aes-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_aes_cra_init,
	.cra_exit		= omap_aes_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= omap_aes_setkey,
		.encrypt	= omap_aes_cbc_encrypt,
		.decrypt	= omap_aes_cbc_decrypt,
	}
}
};

static int omap_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct omap_aes_dev *dd;
	struct resource *res;
	int err = -ENOMEM, i, j;
	u32 reg;

	dd = kzalloc(sizeof(struct omap_aes_dev), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		goto err_data;
	}
	dd->dev = dev;
	platform_set_drvdata(pdev, dd);

	spin_lock_init(&dd->lock);
	crypto_init_queue(&dd->queue, OMAP_AES_QUEUE_LENGTH);

	/* Get the base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "invalid resource type\n");
		err = -ENODEV;
		goto err_res;
	}
	dd->phys_base = res->start;

	/* Get the DMA */
	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res)
		dev_info(dev, "no DMA info\n");
	else
		dd->dma_out = res->start;

	/* Get the DMA */
	res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!res)
		dev_info(dev, "no DMA info\n");
	else
		dd->dma_in = res->start;

	/* Initializing the clock */
	dd->iclk = clk_get(dev, "ick");
	if (IS_ERR(dd->iclk)) {
		dev_err(dev, "clock intialization failed.\n");
		err = PTR_ERR(dd->iclk);
		goto err_res;
	}

	dd->io_base = ioremap(dd->phys_base, SZ_4K);
	if (!dd->io_base) {
		dev_err(dev, "can't ioremap\n");
		err = -ENOMEM;
		goto err_io;
	}

	clk_enable(dd->iclk);
	reg = omap_aes_read(dd, AES_REG_REV);
	dev_info(dev, "OMAP AES hw accel rev: %u.%u\n",
		 (reg & AES_REG_REV_MAJOR) >> 4, reg & AES_REG_REV_MINOR);
	clk_disable(dd->iclk);

	tasklet_init(&dd->done_task, omap_aes_done_task, (unsigned long)dd);
	tasklet_init(&dd->queue_task, omap_aes_queue_task, (unsigned long)dd);

	err = omap_aes_dma_init(dd);
	if (err)
		goto err_dma;

	INIT_LIST_HEAD(&dd->list);
	spin_lock(&list_lock);
	list_add_tail(&dd->list, &dev_list);
	spin_unlock(&list_lock);

	for (i = 0; i < ARRAY_SIZE(algs); i++) {
		pr_debug("i: %d\n", i);
		err = crypto_register_alg(&algs[i]);
		if (err)
			goto err_algs;
	}

	return 0;
err_algs:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&algs[j]);
	omap_aes_dma_cleanup(dd);
err_dma:
	tasklet_kill(&dd->done_task);
	tasklet_kill(&dd->queue_task);
	iounmap(dd->io_base);
err_io:
	clk_put(dd->iclk);
err_res:
	kfree(dd);
	dd = NULL;
err_data:
	dev_err(dev, "initialization failed.\n");
	return err;
}

static int omap_aes_remove(struct platform_device *pdev)
{
	struct omap_aes_dev *dd = platform_get_drvdata(pdev);
	int i;

	if (!dd)
		return -ENODEV;

	spin_lock(&list_lock);
	list_del(&dd->list);
	spin_unlock(&list_lock);

	for (i = 0; i < ARRAY_SIZE(algs); i++)
		crypto_unregister_alg(&algs[i]);

	tasklet_kill(&dd->done_task);
	tasklet_kill(&dd->queue_task);
	omap_aes_dma_cleanup(dd);
	iounmap(dd->io_base);
	clk_put(dd->iclk);
	kfree(dd);
	dd = NULL;

	return 0;
}

static struct platform_driver omap_aes_driver = {
	.probe	= omap_aes_probe,
	.remove	= omap_aes_remove,
	.driver	= {
		.name	= "omap-aes",
		.owner	= THIS_MODULE,
	},
};

static int __init omap_aes_mod_init(void)
{
	return  platform_driver_register(&omap_aes_driver);
}

static void __exit omap_aes_mod_exit(void)
{
	platform_driver_unregister(&omap_aes_driver);
}

module_init(omap_aes_mod_init);
module_exit(omap_aes_mod_exit);

MODULE_DESCRIPTION("OMAP AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dmitry Kasatkin");

