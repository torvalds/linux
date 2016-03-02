/*
 * Support for OMAP DES and Triple DES HW acceleration.
 *
 * Copyright (c) 2013 Texas Instruments Incorporated
 * Author: Joel Fernandes <joelf@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#ifdef DEBUG
#define prn(num) printk(#num "=%d\n", num)
#define prx(num) printk(#num "=%x\n", num)
#else
#define prn(num) do { } while (0)
#define prx(num)  do { } while (0)
#endif

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/omap-dma.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <crypto/scatterwalk.h>
#include <crypto/des.h>

#define DST_MAXBURST			2

#define DES_BLOCK_WORDS		(DES_BLOCK_SIZE >> 2)

#define _calc_walked(inout) (dd->inout##_walk.offset - dd->inout##_sg->offset)

#define DES_REG_KEY(dd, x)		((dd)->pdata->key_ofs - \
						((x ^ 0x01) * 0x04))

#define DES_REG_IV(dd, x)		((dd)->pdata->iv_ofs + ((x) * 0x04))

#define DES_REG_CTRL(dd)		((dd)->pdata->ctrl_ofs)
#define DES_REG_CTRL_CBC		BIT(4)
#define DES_REG_CTRL_TDES		BIT(3)
#define DES_REG_CTRL_DIRECTION		BIT(2)
#define DES_REG_CTRL_INPUT_READY	BIT(1)
#define DES_REG_CTRL_OUTPUT_READY	BIT(0)

#define DES_REG_DATA_N(dd, x)		((dd)->pdata->data_ofs + ((x) * 0x04))

#define DES_REG_REV(dd)			((dd)->pdata->rev_ofs)

#define DES_REG_MASK(dd)		((dd)->pdata->mask_ofs)

#define DES_REG_LENGTH_N(x)		(0x24 + ((x) * 0x04))

#define DES_REG_IRQ_STATUS(dd)         ((dd)->pdata->irq_status_ofs)
#define DES_REG_IRQ_ENABLE(dd)         ((dd)->pdata->irq_enable_ofs)
#define DES_REG_IRQ_DATA_IN            BIT(1)
#define DES_REG_IRQ_DATA_OUT           BIT(2)

#define FLAGS_MODE_MASK		0x000f
#define FLAGS_ENCRYPT		BIT(0)
#define FLAGS_CBC		BIT(1)
#define FLAGS_INIT		BIT(4)
#define FLAGS_BUSY		BIT(6)

struct omap_des_ctx {
	struct omap_des_dev *dd;

	int		keylen;
	u32		key[(3 * DES_KEY_SIZE) / sizeof(u32)];
	unsigned long	flags;
};

struct omap_des_reqctx {
	unsigned long mode;
};

#define OMAP_DES_QUEUE_LENGTH	1
#define OMAP_DES_CACHE_SIZE	0

struct omap_des_algs_info {
	struct crypto_alg	*algs_list;
	unsigned int		size;
	unsigned int		registered;
};

struct omap_des_pdata {
	struct omap_des_algs_info	*algs_info;
	unsigned int	algs_info_size;

	void		(*trigger)(struct omap_des_dev *dd, int length);

	u32		key_ofs;
	u32		iv_ofs;
	u32		ctrl_ofs;
	u32		data_ofs;
	u32		rev_ofs;
	u32		mask_ofs;
	u32             irq_enable_ofs;
	u32             irq_status_ofs;

	u32		dma_enable_in;
	u32		dma_enable_out;
	u32		dma_start;

	u32		major_mask;
	u32		major_shift;
	u32		minor_mask;
	u32		minor_shift;
};

struct omap_des_dev {
	struct list_head	list;
	unsigned long		phys_base;
	void __iomem		*io_base;
	struct omap_des_ctx	*ctx;
	struct device		*dev;
	unsigned long		flags;
	int			err;

	/* spinlock used for queues */
	spinlock_t		lock;
	struct crypto_queue	queue;

	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;

	struct ablkcipher_request	*req;
	/*
	 * total is used by PIO mode for book keeping so introduce
	 * variable total_save as need it to calc page_order
	 */
	size_t                          total;
	size_t                          total_save;

	struct scatterlist		*in_sg;
	struct scatterlist		*out_sg;

	/* Buffers for copying for unaligned cases */
	struct scatterlist		in_sgl;
	struct scatterlist		out_sgl;
	struct scatterlist		*orig_out;
	int				sgs_copied;

	struct scatter_walk		in_walk;
	struct scatter_walk		out_walk;
	int			dma_in;
	struct dma_chan		*dma_lch_in;
	int			dma_out;
	struct dma_chan		*dma_lch_out;
	int			in_sg_len;
	int			out_sg_len;
	int			pio_only;
	const struct omap_des_pdata	*pdata;
};

/* keep registered devices data here */
static LIST_HEAD(dev_list);
static DEFINE_SPINLOCK(list_lock);

#ifdef DEBUG
#define omap_des_read(dd, offset)                               \
	({                                                              \
	 int _read_ret;                                          \
	 _read_ret = __raw_readl(dd->io_base + offset);          \
	 pr_err("omap_des_read(" #offset "=%#x)= %#x\n",       \
		 offset, _read_ret);                            \
	 _read_ret;                                              \
	 })
#else
static inline u32 omap_des_read(struct omap_des_dev *dd, u32 offset)
{
	return __raw_readl(dd->io_base + offset);
}
#endif

#ifdef DEBUG
#define omap_des_write(dd, offset, value)                               \
	do {                                                            \
		pr_err("omap_des_write(" #offset "=%#x) value=%#x\n", \
				offset, value);                                \
		__raw_writel(value, dd->io_base + offset);              \
	} while (0)
#else
static inline void omap_des_write(struct omap_des_dev *dd, u32 offset,
		u32 value)
{
	__raw_writel(value, dd->io_base + offset);
}
#endif

static inline void omap_des_write_mask(struct omap_des_dev *dd, u32 offset,
					u32 value, u32 mask)
{
	u32 val;

	val = omap_des_read(dd, offset);
	val &= ~mask;
	val |= value;
	omap_des_write(dd, offset, val);
}

static void omap_des_write_n(struct omap_des_dev *dd, u32 offset,
					u32 *value, int count)
{
	for (; count--; value++, offset += 4)
		omap_des_write(dd, offset, *value);
}

static int omap_des_hw_init(struct omap_des_dev *dd)
{
	int err;

	/*
	 * clocks are enabled when request starts and disabled when finished.
	 * It may be long delays between requests.
	 * Device might go to off mode to save power.
	 */
	err = pm_runtime_get_sync(dd->dev);
	if (err < 0) {
		pm_runtime_put_noidle(dd->dev);
		dev_err(dd->dev, "%s: failed to get_sync(%d)\n", __func__, err);
		return err;
	}

	if (!(dd->flags & FLAGS_INIT)) {
		dd->flags |= FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static int omap_des_write_ctrl(struct omap_des_dev *dd)
{
	unsigned int key32;
	int i, err;
	u32 val = 0, mask = 0;

	err = omap_des_hw_init(dd);
	if (err)
		return err;

	key32 = dd->ctx->keylen / sizeof(u32);

	/* it seems a key should always be set even if it has not changed */
	for (i = 0; i < key32; i++) {
		omap_des_write(dd, DES_REG_KEY(dd, i),
			       __le32_to_cpu(dd->ctx->key[i]));
	}

	if ((dd->flags & FLAGS_CBC) && dd->req->info)
		omap_des_write_n(dd, DES_REG_IV(dd, 0), dd->req->info, 2);

	if (dd->flags & FLAGS_CBC)
		val |= DES_REG_CTRL_CBC;
	if (dd->flags & FLAGS_ENCRYPT)
		val |= DES_REG_CTRL_DIRECTION;
	if (key32 == 6)
		val |= DES_REG_CTRL_TDES;

	mask |= DES_REG_CTRL_CBC | DES_REG_CTRL_DIRECTION | DES_REG_CTRL_TDES;

	omap_des_write_mask(dd, DES_REG_CTRL(dd), val, mask);

	return 0;
}

static void omap_des_dma_trigger_omap4(struct omap_des_dev *dd, int length)
{
	u32 mask, val;

	omap_des_write(dd, DES_REG_LENGTH_N(0), length);

	val = dd->pdata->dma_start;

	if (dd->dma_lch_out != NULL)
		val |= dd->pdata->dma_enable_out;
	if (dd->dma_lch_in != NULL)
		val |= dd->pdata->dma_enable_in;

	mask = dd->pdata->dma_enable_out | dd->pdata->dma_enable_in |
	       dd->pdata->dma_start;

	omap_des_write_mask(dd, DES_REG_MASK(dd), val, mask);
}

static void omap_des_dma_stop(struct omap_des_dev *dd)
{
	u32 mask;

	mask = dd->pdata->dma_enable_out | dd->pdata->dma_enable_in |
	       dd->pdata->dma_start;

	omap_des_write_mask(dd, DES_REG_MASK(dd), 0, mask);
}

static struct omap_des_dev *omap_des_find_dev(struct omap_des_ctx *ctx)
{
	struct omap_des_dev *dd = NULL, *tmp;

	spin_lock_bh(&list_lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &dev_list, list) {
			/* FIXME: take fist available des core */
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

static void omap_des_dma_out_callback(void *data)
{
	struct omap_des_dev *dd = data;

	/* dma_lch_out - completed */
	tasklet_schedule(&dd->done_task);
}

static int omap_des_dma_init(struct omap_des_dev *dd)
{
	int err = -ENOMEM;
	dma_cap_mask_t mask;

	dd->dma_lch_out = NULL;
	dd->dma_lch_in = NULL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	dd->dma_lch_in = dma_request_slave_channel_compat(mask,
							  omap_dma_filter_fn,
							  &dd->dma_in,
							  dd->dev, "rx");
	if (!dd->dma_lch_in) {
		dev_err(dd->dev, "Unable to request in DMA channel\n");
		goto err_dma_in;
	}

	dd->dma_lch_out = dma_request_slave_channel_compat(mask,
							   omap_dma_filter_fn,
							   &dd->dma_out,
							   dd->dev, "tx");
	if (!dd->dma_lch_out) {
		dev_err(dd->dev, "Unable to request out DMA channel\n");
		goto err_dma_out;
	}

	return 0;

err_dma_out:
	dma_release_channel(dd->dma_lch_in);
err_dma_in:
	if (err)
		pr_err("error: %d\n", err);
	return err;
}

static void omap_des_dma_cleanup(struct omap_des_dev *dd)
{
	dma_release_channel(dd->dma_lch_out);
	dma_release_channel(dd->dma_lch_in);
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

static int omap_des_crypt_dma(struct crypto_tfm *tfm,
		struct scatterlist *in_sg, struct scatterlist *out_sg,
		int in_sg_len, int out_sg_len)
{
	struct omap_des_ctx *ctx = crypto_tfm_ctx(tfm);
	struct omap_des_dev *dd = ctx->dd;
	struct dma_async_tx_descriptor *tx_in, *tx_out;
	struct dma_slave_config cfg;
	int ret;

	if (dd->pio_only) {
		scatterwalk_start(&dd->in_walk, dd->in_sg);
		scatterwalk_start(&dd->out_walk, dd->out_sg);

		/* Enable DATAIN interrupt and let it take
		   care of the rest */
		omap_des_write(dd, DES_REG_IRQ_ENABLE(dd), 0x2);
		return 0;
	}

	dma_sync_sg_for_device(dd->dev, dd->in_sg, in_sg_len, DMA_TO_DEVICE);

	memset(&cfg, 0, sizeof(cfg));

	cfg.src_addr = dd->phys_base + DES_REG_DATA_N(dd, 0);
	cfg.dst_addr = dd->phys_base + DES_REG_DATA_N(dd, 0);
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst = DST_MAXBURST;
	cfg.dst_maxburst = DST_MAXBURST;

	/* IN */
	ret = dmaengine_slave_config(dd->dma_lch_in, &cfg);
	if (ret) {
		dev_err(dd->dev, "can't configure IN dmaengine slave: %d\n",
			ret);
		return ret;
	}

	tx_in = dmaengine_prep_slave_sg(dd->dma_lch_in, in_sg, in_sg_len,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx_in) {
		dev_err(dd->dev, "IN prep_slave_sg() failed\n");
		return -EINVAL;
	}

	/* No callback necessary */
	tx_in->callback_param = dd;

	/* OUT */
	ret = dmaengine_slave_config(dd->dma_lch_out, &cfg);
	if (ret) {
		dev_err(dd->dev, "can't configure OUT dmaengine slave: %d\n",
			ret);
		return ret;
	}

	tx_out = dmaengine_prep_slave_sg(dd->dma_lch_out, out_sg, out_sg_len,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx_out) {
		dev_err(dd->dev, "OUT prep_slave_sg() failed\n");
		return -EINVAL;
	}

	tx_out->callback = omap_des_dma_out_callback;
	tx_out->callback_param = dd;

	dmaengine_submit(tx_in);
	dmaengine_submit(tx_out);

	dma_async_issue_pending(dd->dma_lch_in);
	dma_async_issue_pending(dd->dma_lch_out);

	/* start DMA */
	dd->pdata->trigger(dd, dd->total);

	return 0;
}

static int omap_des_crypt_dma_start(struct omap_des_dev *dd)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(
					crypto_ablkcipher_reqtfm(dd->req));
	int err;

	pr_debug("total: %d\n", dd->total);

	if (!dd->pio_only) {
		err = dma_map_sg(dd->dev, dd->in_sg, dd->in_sg_len,
				 DMA_TO_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}

		err = dma_map_sg(dd->dev, dd->out_sg, dd->out_sg_len,
				 DMA_FROM_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}
	}

	err = omap_des_crypt_dma(tfm, dd->in_sg, dd->out_sg, dd->in_sg_len,
				 dd->out_sg_len);
	if (err && !dd->pio_only) {
		dma_unmap_sg(dd->dev, dd->in_sg, dd->in_sg_len, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, dd->out_sg_len,
			     DMA_FROM_DEVICE);
	}

	return err;
}

static void omap_des_finish_req(struct omap_des_dev *dd, int err)
{
	struct ablkcipher_request *req = dd->req;

	pr_debug("err: %d\n", err);

	pm_runtime_put(dd->dev);
	dd->flags &= ~FLAGS_BUSY;

	req->base.complete(&req->base, err);
}

static int omap_des_crypt_dma_stop(struct omap_des_dev *dd)
{
	pr_debug("total: %d\n", dd->total);

	omap_des_dma_stop(dd);

	dmaengine_terminate_all(dd->dma_lch_in);
	dmaengine_terminate_all(dd->dma_lch_out);

	return 0;
}

static int omap_des_copy_needed(struct scatterlist *sg)
{
	while (sg) {
		if (!IS_ALIGNED(sg->offset, 4))
			return -1;
		if (!IS_ALIGNED(sg->length, DES_BLOCK_SIZE))
			return -1;
		sg = sg_next(sg);
	}
	return 0;
}

static int omap_des_copy_sgs(struct omap_des_dev *dd)
{
	void *buf_in, *buf_out;
	int pages;

	pages = dd->total >> PAGE_SHIFT;

	if (dd->total & (PAGE_SIZE-1))
		pages++;

	BUG_ON(!pages);

	buf_in = (void *)__get_free_pages(GFP_ATOMIC, pages);
	buf_out = (void *)__get_free_pages(GFP_ATOMIC, pages);

	if (!buf_in || !buf_out) {
		pr_err("Couldn't allocated pages for unaligned cases.\n");
		return -1;
	}

	dd->orig_out = dd->out_sg;

	sg_copy_buf(buf_in, dd->in_sg, 0, dd->total, 0);

	sg_init_table(&dd->in_sgl, 1);
	sg_set_buf(&dd->in_sgl, buf_in, dd->total);
	dd->in_sg = &dd->in_sgl;

	sg_init_table(&dd->out_sgl, 1);
	sg_set_buf(&dd->out_sgl, buf_out, dd->total);
	dd->out_sg = &dd->out_sgl;

	return 0;
}

static int omap_des_handle_queue(struct omap_des_dev *dd,
			       struct ablkcipher_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	struct omap_des_ctx *ctx;
	struct omap_des_reqctx *rctx;
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
	dd->total_save = req->nbytes;
	dd->in_sg = req->src;
	dd->out_sg = req->dst;

	if (omap_des_copy_needed(dd->in_sg) ||
	    omap_des_copy_needed(dd->out_sg)) {
		if (omap_des_copy_sgs(dd))
			pr_err("Failed to copy SGs for unaligned cases\n");
		dd->sgs_copied = 1;
	} else {
		dd->sgs_copied = 0;
	}

	dd->in_sg_len = scatterwalk_bytes_sglen(dd->in_sg, dd->total);
	dd->out_sg_len = scatterwalk_bytes_sglen(dd->out_sg, dd->total);
	BUG_ON(dd->in_sg_len < 0 || dd->out_sg_len < 0);

	rctx = ablkcipher_request_ctx(req);
	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx->mode &= FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	dd->ctx = ctx;
	ctx->dd = dd;

	err = omap_des_write_ctrl(dd);
	if (!err)
		err = omap_des_crypt_dma_start(dd);
	if (err) {
		/* des_task will not finish it, so do it here */
		omap_des_finish_req(dd, err);
		tasklet_schedule(&dd->queue_task);
	}

	return ret; /* return ret, which is enqueue return value */
}

static void omap_des_done_task(unsigned long data)
{
	struct omap_des_dev *dd = (struct omap_des_dev *)data;
	void *buf_in, *buf_out;
	int pages;

	pr_debug("enter done_task\n");

	if (!dd->pio_only) {
		dma_sync_sg_for_device(dd->dev, dd->out_sg, dd->out_sg_len,
				       DMA_FROM_DEVICE);
		dma_unmap_sg(dd->dev, dd->in_sg, dd->in_sg_len, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, dd->out_sg_len,
			     DMA_FROM_DEVICE);
		omap_des_crypt_dma_stop(dd);
	}

	if (dd->sgs_copied) {
		buf_in = sg_virt(&dd->in_sgl);
		buf_out = sg_virt(&dd->out_sgl);

		sg_copy_buf(buf_out, dd->orig_out, 0, dd->total_save, 1);

		pages = get_order(dd->total_save);
		free_pages((unsigned long)buf_in, pages);
		free_pages((unsigned long)buf_out, pages);
	}

	omap_des_finish_req(dd, 0);
	omap_des_handle_queue(dd, NULL);

	pr_debug("exit\n");
}

static void omap_des_queue_task(unsigned long data)
{
	struct omap_des_dev *dd = (struct omap_des_dev *)data;

	omap_des_handle_queue(dd, NULL);
}

static int omap_des_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct omap_des_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct omap_des_reqctx *rctx = ablkcipher_request_ctx(req);
	struct omap_des_dev *dd;

	pr_debug("nbytes: %d, enc: %d, cbc: %d\n", req->nbytes,
		 !!(mode & FLAGS_ENCRYPT),
		 !!(mode & FLAGS_CBC));

	if (!IS_ALIGNED(req->nbytes, DES_BLOCK_SIZE)) {
		pr_err("request size is not exact amount of DES blocks\n");
		return -EINVAL;
	}

	dd = omap_des_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx->mode = mode;

	return omap_des_handle_queue(dd, req);
}

/* ********************** ALG API ************************************ */

static int omap_des_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct omap_des_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (keylen != DES_KEY_SIZE && keylen != (3*DES_KEY_SIZE))
		return -EINVAL;

	pr_debug("enter, keylen: %d\n", keylen);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int omap_des_ecb_encrypt(struct ablkcipher_request *req)
{
	return omap_des_crypt(req, FLAGS_ENCRYPT);
}

static int omap_des_ecb_decrypt(struct ablkcipher_request *req)
{
	return omap_des_crypt(req, 0);
}

static int omap_des_cbc_encrypt(struct ablkcipher_request *req)
{
	return omap_des_crypt(req, FLAGS_ENCRYPT | FLAGS_CBC);
}

static int omap_des_cbc_decrypt(struct ablkcipher_request *req)
{
	return omap_des_crypt(req, FLAGS_CBC);
}

static int omap_des_cra_init(struct crypto_tfm *tfm)
{
	pr_debug("enter\n");

	tfm->crt_ablkcipher.reqsize = sizeof(struct omap_des_reqctx);

	return 0;
}

static void omap_des_cra_exit(struct crypto_tfm *tfm)
{
	pr_debug("enter\n");
}

/* ********************** ALGS ************************************ */

static struct crypto_alg algs_ecb_cbc[] = {
{
	.cra_name		= "ecb(des)",
	.cra_driver_name	= "ecb-des-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_des_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_des_cra_init,
	.cra_exit		= omap_des_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.setkey		= omap_des_setkey,
		.encrypt	= omap_des_ecb_encrypt,
		.decrypt	= omap_des_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des)",
	.cra_driver_name	= "cbc-des-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_des_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_des_cra_init,
	.cra_exit		= omap_des_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= DES_KEY_SIZE,
		.max_keysize	= DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= omap_des_setkey,
		.encrypt	= omap_des_cbc_encrypt,
		.decrypt	= omap_des_cbc_decrypt,
	}
},
{
	.cra_name		= "ecb(des3_ede)",
	.cra_driver_name	= "ecb-des3-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_des_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_des_cra_init,
	.cra_exit		= omap_des_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= 3*DES_KEY_SIZE,
		.max_keysize	= 3*DES_KEY_SIZE,
		.setkey		= omap_des_setkey,
		.encrypt	= omap_des_ecb_encrypt,
		.decrypt	= omap_des_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des3_ede)",
	.cra_driver_name	= "cbc-des3-omap",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_KERN_DRIVER_ONLY |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct omap_des_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= omap_des_cra_init,
	.cra_exit		= omap_des_cra_exit,
	.cra_u.ablkcipher = {
		.min_keysize	= 3*DES_KEY_SIZE,
		.max_keysize	= 3*DES_KEY_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= omap_des_setkey,
		.encrypt	= omap_des_cbc_encrypt,
		.decrypt	= omap_des_cbc_decrypt,
	}
}
};

static struct omap_des_algs_info omap_des_algs_info_ecb_cbc[] = {
	{
		.algs_list	= algs_ecb_cbc,
		.size		= ARRAY_SIZE(algs_ecb_cbc),
	},
};

#ifdef CONFIG_OF
static const struct omap_des_pdata omap_des_pdata_omap4 = {
	.algs_info	= omap_des_algs_info_ecb_cbc,
	.algs_info_size	= ARRAY_SIZE(omap_des_algs_info_ecb_cbc),
	.trigger	= omap_des_dma_trigger_omap4,
	.key_ofs	= 0x14,
	.iv_ofs		= 0x18,
	.ctrl_ofs	= 0x20,
	.data_ofs	= 0x28,
	.rev_ofs	= 0x30,
	.mask_ofs	= 0x34,
	.irq_status_ofs = 0x3c,
	.irq_enable_ofs = 0x40,
	.dma_enable_in	= BIT(5),
	.dma_enable_out	= BIT(6),
	.major_mask	= 0x0700,
	.major_shift	= 8,
	.minor_mask	= 0x003f,
	.minor_shift	= 0,
};

static irqreturn_t omap_des_irq(int irq, void *dev_id)
{
	struct omap_des_dev *dd = dev_id;
	u32 status, i;
	u32 *src, *dst;

	status = omap_des_read(dd, DES_REG_IRQ_STATUS(dd));
	if (status & DES_REG_IRQ_DATA_IN) {
		omap_des_write(dd, DES_REG_IRQ_ENABLE(dd), 0x0);

		BUG_ON(!dd->in_sg);

		BUG_ON(_calc_walked(in) > dd->in_sg->length);

		src = sg_virt(dd->in_sg) + _calc_walked(in);

		for (i = 0; i < DES_BLOCK_WORDS; i++) {
			omap_des_write(dd, DES_REG_DATA_N(dd, i), *src);

			scatterwalk_advance(&dd->in_walk, 4);
			if (dd->in_sg->length == _calc_walked(in)) {
				dd->in_sg = sg_next(dd->in_sg);
				if (dd->in_sg) {
					scatterwalk_start(&dd->in_walk,
							  dd->in_sg);
					src = sg_virt(dd->in_sg) +
					      _calc_walked(in);
				}
			} else {
				src++;
			}
		}

		/* Clear IRQ status */
		status &= ~DES_REG_IRQ_DATA_IN;
		omap_des_write(dd, DES_REG_IRQ_STATUS(dd), status);

		/* Enable DATA_OUT interrupt */
		omap_des_write(dd, DES_REG_IRQ_ENABLE(dd), 0x4);

	} else if (status & DES_REG_IRQ_DATA_OUT) {
		omap_des_write(dd, DES_REG_IRQ_ENABLE(dd), 0x0);

		BUG_ON(!dd->out_sg);

		BUG_ON(_calc_walked(out) > dd->out_sg->length);

		dst = sg_virt(dd->out_sg) + _calc_walked(out);

		for (i = 0; i < DES_BLOCK_WORDS; i++) {
			*dst = omap_des_read(dd, DES_REG_DATA_N(dd, i));
			scatterwalk_advance(&dd->out_walk, 4);
			if (dd->out_sg->length == _calc_walked(out)) {
				dd->out_sg = sg_next(dd->out_sg);
				if (dd->out_sg) {
					scatterwalk_start(&dd->out_walk,
							  dd->out_sg);
					dst = sg_virt(dd->out_sg) +
					      _calc_walked(out);
				}
			} else {
				dst++;
			}
		}

		BUG_ON(dd->total < DES_BLOCK_SIZE);

		dd->total -= DES_BLOCK_SIZE;

		/* Clear IRQ status */
		status &= ~DES_REG_IRQ_DATA_OUT;
		omap_des_write(dd, DES_REG_IRQ_STATUS(dd), status);

		if (!dd->total)
			/* All bytes read! */
			tasklet_schedule(&dd->done_task);
		else
			/* Enable DATA_IN interrupt for next block */
			omap_des_write(dd, DES_REG_IRQ_ENABLE(dd), 0x2);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id omap_des_of_match[] = {
	{
		.compatible	= "ti,omap4-des",
		.data		= &omap_des_pdata_omap4,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_des_of_match);

static int omap_des_get_of(struct omap_des_dev *dd,
		struct platform_device *pdev)
{
	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(omap_des_of_match), &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "no compatible OF match\n");
		return -EINVAL;
	}

	dd->dma_out = -1; /* Dummy value that's unused */
	dd->dma_in = -1; /* Dummy value that's unused */
	dd->pdata = match->data;

	return 0;
}
#else
static int omap_des_get_of(struct omap_des_dev *dd,
		struct device *dev)
{
	return -EINVAL;
}
#endif

static int omap_des_get_pdev(struct omap_des_dev *dd,
		struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	int err = 0;

	/* Get the DMA out channel */
	r = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!r) {
		dev_err(dev, "no DMA out resource info\n");
		err = -ENODEV;
		goto err;
	}
	dd->dma_out = r->start;

	/* Get the DMA in channel */
	r = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!r) {
		dev_err(dev, "no DMA in resource info\n");
		err = -ENODEV;
		goto err;
	}
	dd->dma_in = r->start;

	/* non-DT devices get pdata from pdev */
	dd->pdata = pdev->dev.platform_data;

err:
	return err;
}

static int omap_des_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct omap_des_dev *dd;
	struct crypto_alg *algp;
	struct resource *res;
	int err = -ENOMEM, i, j, irq = -1;
	u32 reg;

	dd = devm_kzalloc(dev, sizeof(struct omap_des_dev), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		goto err_data;
	}
	dd->dev = dev;
	platform_set_drvdata(pdev, dd);

	spin_lock_init(&dd->lock);
	crypto_init_queue(&dd->queue, OMAP_DES_QUEUE_LENGTH);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no MEM resource info\n");
		goto err_res;
	}

	err = (dev->of_node) ? omap_des_get_of(dd, pdev) :
			       omap_des_get_pdev(dd, pdev);
	if (err)
		goto err_res;

	dd->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(dd->io_base)) {
		err = PTR_ERR(dd->io_base);
		goto err_res;
	}
	dd->phys_base = res->start;

	pm_runtime_enable(dev);
	pm_runtime_irq_safe(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dd->dev, "%s: failed to get_sync(%d)\n", __func__, err);
		goto err_get;
	}

	omap_des_dma_stop(dd);

	reg = omap_des_read(dd, DES_REG_REV(dd));

	pm_runtime_put_sync(dev);

	dev_info(dev, "OMAP DES hw accel rev: %u.%u\n",
		 (reg & dd->pdata->major_mask) >> dd->pdata->major_shift,
		 (reg & dd->pdata->minor_mask) >> dd->pdata->minor_shift);

	tasklet_init(&dd->done_task, omap_des_done_task, (unsigned long)dd);
	tasklet_init(&dd->queue_task, omap_des_queue_task, (unsigned long)dd);

	err = omap_des_dma_init(dd);
	if (err && DES_REG_IRQ_STATUS(dd) && DES_REG_IRQ_ENABLE(dd)) {
		dd->pio_only = 1;

		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			dev_err(dev, "can't get IRQ resource\n");
			goto err_irq;
		}

		err = devm_request_irq(dev, irq, omap_des_irq, 0,
				dev_name(dev), dd);
		if (err) {
			dev_err(dev, "Unable to grab omap-des IRQ\n");
			goto err_irq;
		}
	}


	INIT_LIST_HEAD(&dd->list);
	spin_lock(&list_lock);
	list_add_tail(&dd->list, &dev_list);
	spin_unlock(&list_lock);

	for (i = 0; i < dd->pdata->algs_info_size; i++) {
		for (j = 0; j < dd->pdata->algs_info[i].size; j++) {
			algp = &dd->pdata->algs_info[i].algs_list[j];

			pr_debug("reg alg: %s\n", algp->cra_name);
			INIT_LIST_HEAD(&algp->cra_list);

			err = crypto_register_alg(algp);
			if (err)
				goto err_algs;

			dd->pdata->algs_info[i].registered++;
		}
	}

	return 0;
err_algs:
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_unregister_alg(
					&dd->pdata->algs_info[i].algs_list[j]);
	if (!dd->pio_only)
		omap_des_dma_cleanup(dd);
err_irq:
	tasklet_kill(&dd->done_task);
	tasklet_kill(&dd->queue_task);
err_get:
	pm_runtime_disable(dev);
err_res:
	dd = NULL;
err_data:
	dev_err(dev, "initialization failed.\n");
	return err;
}

static int omap_des_remove(struct platform_device *pdev)
{
	struct omap_des_dev *dd = platform_get_drvdata(pdev);
	int i, j;

	if (!dd)
		return -ENODEV;

	spin_lock(&list_lock);
	list_del(&dd->list);
	spin_unlock(&list_lock);

	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_unregister_alg(
					&dd->pdata->algs_info[i].algs_list[j]);

	tasklet_kill(&dd->done_task);
	tasklet_kill(&dd->queue_task);
	omap_des_dma_cleanup(dd);
	pm_runtime_disable(dd->dev);
	dd = NULL;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int omap_des_suspend(struct device *dev)
{
	pm_runtime_put_sync(dev);
	return 0;
}

static int omap_des_resume(struct device *dev)
{
	int err;

	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dev, "%s: failed to get_sync(%d)\n", __func__, err);
		return err;
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(omap_des_pm_ops, omap_des_suspend, omap_des_resume);

static struct platform_driver omap_des_driver = {
	.probe	= omap_des_probe,
	.remove	= omap_des_remove,
	.driver	= {
		.name	= "omap-des",
		.pm	= &omap_des_pm_ops,
		.of_match_table	= of_match_ptr(omap_des_of_match),
	},
};

module_platform_driver(omap_des_driver);

MODULE_DESCRIPTION("OMAP DES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joel Fernandes <joelf@ti.com>");
