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
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include "atmel-tdes-regs.h"

/* TDES flags  */
#define TDES_FLAGS_MODE_MASK		0x007f
#define TDES_FLAGS_ENCRYPT	BIT(0)
#define TDES_FLAGS_CBC		BIT(1)
#define TDES_FLAGS_CFB		BIT(2)
#define TDES_FLAGS_CFB8		BIT(3)
#define TDES_FLAGS_CFB16	BIT(4)
#define TDES_FLAGS_CFB32	BIT(5)
#define TDES_FLAGS_OFB		BIT(6)

#define TDES_FLAGS_INIT		BIT(16)
#define TDES_FLAGS_FAST		BIT(17)
#define TDES_FLAGS_BUSY		BIT(18)

#define ATMEL_TDES_QUEUE_LENGTH	1

#define CFB8_BLOCK_SIZE		1
#define CFB16_BLOCK_SIZE	2
#define CFB32_BLOCK_SIZE	4
#define CFB64_BLOCK_SIZE	8


struct atmel_tdes_dev;

struct atmel_tdes_ctx {
	struct atmel_tdes_dev *dd;

	int		keylen;
	u32		key[3*DES_KEY_SIZE / sizeof(u32)];
	unsigned long	flags;
};

struct atmel_tdes_reqctx {
	unsigned long mode;
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
	size_t				in_offset;
	struct scatterlist	*out_sg;
	size_t				out_offset;

	size_t	buflen;
	size_t	dma_size;

	void	*buf_in;
	int		dma_in;
	dma_addr_t	dma_addr_in;

	void	*buf_out;
	int		dma_out;
	dma_addr_t	dma_addr_out;
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
	clk_prepare_enable(dd->iclk);

	if (!(dd->flags & TDES_FLAGS_INIT)) {
		atmel_tdes_write(dd, TDES_CR, TDES_CR_SWRST);
		dd->flags |= TDES_FLAGS_INIT;
		dd->err = 0;
	}

	return 0;
}

static int atmel_tdes_write_ctrl(struct atmel_tdes_dev *dd)
{
	int err;
	u32 valcr = 0, valmr = TDES_MR_SMOD_PDC;

	err = atmel_tdes_hw_init(dd);

	if (err)
		return err;

	atmel_tdes_write(dd, TDES_PTCR, TDES_PTCR_TXTDIS|TDES_PTCR_RXTDIS);

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

static int atmel_tdes_crypt_dma_stop(struct atmel_tdes_dev *dd)
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
			pr_err("not all data converted: %u\n", count);
		}
	}

	return err;
}

static int atmel_tdes_dma_init(struct atmel_tdes_dev *dd)
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
	free_page((unsigned long)dd->buf_out);
	free_page((unsigned long)dd->buf_in);
err_alloc:
	if (err)
		pr_err("error: %d\n", err);
	return err;
}

static void atmel_tdes_dma_cleanup(struct atmel_tdes_dev *dd)
{
	dma_unmap_single(dd->dev, dd->dma_addr_out, dd->buflen,
			 DMA_FROM_DEVICE);
	dma_unmap_single(dd->dev, dd->dma_addr_in, dd->buflen,
		DMA_TO_DEVICE);
	free_page((unsigned long)dd->buf_out);
	free_page((unsigned long)dd->buf_in);
}

static int atmel_tdes_crypt_dma(struct crypto_tfm *tfm, dma_addr_t dma_addr_in,
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

static int atmel_tdes_crypt_dma_start(struct atmel_tdes_dev *dd)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(
					crypto_ablkcipher_reqtfm(dd->req));
	int err, fast = 0, in, out;
	size_t count;
	dma_addr_t addr_in, addr_out;

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

	err = atmel_tdes_crypt_dma(tfm, addr_in, addr_out, count);
	if (err) {
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
		err = atmel_tdes_crypt_dma_start(dd);
	if (err) {
		/* des_task will not finish it, so do it here */
		atmel_tdes_finish_req(dd, err);
		tasklet_schedule(&dd->queue_task);
	}

	return ret;
}


static int atmel_tdes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct atmel_tdes_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct atmel_tdes_reqctx *rctx = ablkcipher_request_ctx(req);
	struct atmel_tdes_dev *dd;

	if (mode & TDES_FLAGS_CFB8) {
		if (!IS_ALIGNED(req->nbytes, CFB8_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB8 blocks\n");
			return -EINVAL;
		}
	} else if (mode & TDES_FLAGS_CFB16) {
		if (!IS_ALIGNED(req->nbytes, CFB16_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB16 blocks\n");
			return -EINVAL;
		}
	} else if (mode & TDES_FLAGS_CFB32) {
		if (!IS_ALIGNED(req->nbytes, CFB32_BLOCK_SIZE)) {
			pr_err("request size is not exact amount of CFB32 blocks\n");
			return -EINVAL;
		}
	} else if (!IS_ALIGNED(req->nbytes, DES_BLOCK_SIZE)) {
		pr_err("request size is not exact amount of DES blocks\n");
		return -EINVAL;
	}

	dd = atmel_tdes_find_dev(ctx);
	if (!dd)
		return -ENODEV;

	rctx->mode = mode;

	return atmel_tdes_handle_queue(dd, req);
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
	if (strstr(alg_name, "cfb") && (keylen != 2*DES_KEY_SIZE)) {
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
	tfm->crt_ablkcipher.reqsize = sizeof(struct atmel_tdes_reqctx);

	return 0;
}

static void atmel_tdes_cra_exit(struct crypto_tfm *tfm)
{
}

static struct crypto_alg tdes_algs[] = {
{
	.cra_name		= "ecb(des)",
	.cra_driver_name	= "atmel-ecb-des",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct atmel_tdes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= atmel_tdes_cra_init,
	.cra_exit		= atmel_tdes_cra_exit,
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

	err = atmel_tdes_crypt_dma_stop(dd);

	err = dd->err ? : err;

	if (dd->total && !err) {
		err = atmel_tdes_crypt_dma_start(dd);
		if (!err)
			return;
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

static int __devinit atmel_tdes_probe(struct platform_device *pdev)
{
	struct atmel_tdes_dev *tdes_dd;
	struct device *dev = &pdev->dev;
	struct resource *tdes_res;
	unsigned long tdes_phys_size;
	int err;

	tdes_dd = kzalloc(sizeof(struct atmel_tdes_dev), GFP_KERNEL);
	if (tdes_dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		err = -ENOMEM;
		goto tdes_dd_err;
	}

	tdes_dd->dev = dev;

	platform_set_drvdata(pdev, tdes_dd);

	INIT_LIST_HEAD(&tdes_dd->list);

	tasklet_init(&tdes_dd->done_task, atmel_tdes_done_task,
					(unsigned long)tdes_dd);
	tasklet_init(&tdes_dd->queue_task, atmel_tdes_queue_task,
					(unsigned long)tdes_dd);

	crypto_init_queue(&tdes_dd->queue, ATMEL_TDES_QUEUE_LENGTH);

	tdes_dd->irq = -1;

	/* Get the base address */
	tdes_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!tdes_res) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto res_err;
	}
	tdes_dd->phys_base = tdes_res->start;
	tdes_phys_size = resource_size(tdes_res);

	/* Get the IRQ */
	tdes_dd->irq = platform_get_irq(pdev,  0);
	if (tdes_dd->irq < 0) {
		dev_err(dev, "no IRQ resource info\n");
		err = tdes_dd->irq;
		goto res_err;
	}

	err = request_irq(tdes_dd->irq, atmel_tdes_irq, IRQF_SHARED,
			"atmel-tdes", tdes_dd);
	if (err) {
		dev_err(dev, "unable to request tdes irq.\n");
		goto tdes_irq_err;
	}

	/* Initializing the clock */
	tdes_dd->iclk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tdes_dd->iclk)) {
		dev_err(dev, "clock intialization failed.\n");
		err = PTR_ERR(tdes_dd->iclk);
		goto clk_err;
	}

	tdes_dd->io_base = ioremap(tdes_dd->phys_base, tdes_phys_size);
	if (!tdes_dd->io_base) {
		dev_err(dev, "can't ioremap\n");
		err = -ENOMEM;
		goto tdes_io_err;
	}

	err = atmel_tdes_dma_init(tdes_dd);
	if (err)
		goto err_tdes_dma;

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
	atmel_tdes_dma_cleanup(tdes_dd);
err_tdes_dma:
	iounmap(tdes_dd->io_base);
tdes_io_err:
	clk_put(tdes_dd->iclk);
clk_err:
	free_irq(tdes_dd->irq, tdes_dd);
tdes_irq_err:
res_err:
	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);
	kfree(tdes_dd);
	tdes_dd = NULL;
tdes_dd_err:
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int __devexit atmel_tdes_remove(struct platform_device *pdev)
{
	static struct atmel_tdes_dev *tdes_dd;

	tdes_dd = platform_get_drvdata(pdev);
	if (!tdes_dd)
		return -ENODEV;
	spin_lock(&atmel_tdes.lock);
	list_del(&tdes_dd->list);
	spin_unlock(&atmel_tdes.lock);

	atmel_tdes_unregister_algs(tdes_dd);

	tasklet_kill(&tdes_dd->done_task);
	tasklet_kill(&tdes_dd->queue_task);

	atmel_tdes_dma_cleanup(tdes_dd);

	iounmap(tdes_dd->io_base);

	clk_put(tdes_dd->iclk);

	if (tdes_dd->irq >= 0)
		free_irq(tdes_dd->irq, tdes_dd);

	kfree(tdes_dd);
	tdes_dd = NULL;

	return 0;
}

static struct platform_driver atmel_tdes_driver = {
	.probe		= atmel_tdes_probe,
	.remove		= __devexit_p(atmel_tdes_remove),
	.driver		= {
		.name	= "atmel_tdes",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(atmel_tdes_driver);

MODULE_DESCRIPTION("Atmel DES/TDES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nicolas Royer - Eukréa Electromatique");
