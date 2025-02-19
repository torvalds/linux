// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Support for OMAP AES HW acceleration.
 *
 * Copyright (c) 2010 Nokia Corporation
 * Author: Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 * Copyright (c) 2011 Texas Instruments Incorporated
 */

#define pr_fmt(fmt) "%20s: " fmt, __func__
#define prn(num) pr_debug(#num "=%d\n", num)
#define prx(num) pr_debug(#num "=%x\n", num)

#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/engine.h>
#include <crypto/internal/skcipher.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

#include "omap-crypto.h"
#include "omap-aes.h"

/* keep registered devices data here */
static LIST_HEAD(dev_list);
static DEFINE_SPINLOCK(list_lock);

static int aes_fallback_sz = 200;

#ifdef DEBUG
#define omap_aes_read(dd, offset)				\
({								\
	int _read_ret;						\
	_read_ret = __raw_readl(dd->io_base + offset);		\
	pr_debug("omap_aes_read(" #offset "=%#x)= %#x\n",	\
		 offset, _read_ret);				\
	_read_ret;						\
})
#else
inline u32 omap_aes_read(struct omap_aes_dev *dd, u32 offset)
{
	return __raw_readl(dd->io_base + offset);
}
#endif

#ifdef DEBUG
#define omap_aes_write(dd, offset, value)				\
	do {								\
		pr_debug("omap_aes_write(" #offset "=%#x) value=%#x\n",	\
			 offset, value);				\
		__raw_writel(value, dd->io_base + offset);		\
	} while (0)
#else
inline void omap_aes_write(struct omap_aes_dev *dd, u32 offset,
				  u32 value)
{
	__raw_writel(value, dd->io_base + offset);
}
#endif

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
	int err;

	if (!(dd->flags & FLAGS_INIT)) {
		dd->flags |= FLAGS_INIT;
		dd->err = 0;
	}

	err = pm_runtime_resume_and_get(dd->dev);
	if (err < 0) {
		dev_err(dd->dev, "failed to get sync: %d\n", err);
		return err;
	}

	return 0;
}

void omap_aes_clear_copy_flags(struct omap_aes_dev *dd)
{
	dd->flags &= ~(OMAP_CRYPTO_COPY_MASK << FLAGS_IN_DATA_ST_SHIFT);
	dd->flags &= ~(OMAP_CRYPTO_COPY_MASK << FLAGS_OUT_DATA_ST_SHIFT);
	dd->flags &= ~(OMAP_CRYPTO_COPY_MASK << FLAGS_ASSOC_DATA_ST_SHIFT);
}

int omap_aes_write_ctrl(struct omap_aes_dev *dd)
{
	struct omap_aes_reqctx *rctx;
	unsigned int key32;
	int i, err;
	u32 val;

	err = omap_aes_hw_init(dd);
	if (err)
		return err;

	key32 = dd->ctx->keylen / sizeof(u32);

	/* RESET the key as previous HASH keys should not get affected*/
	if (dd->flags & FLAGS_GCM)
		for (i = 0; i < 0x40; i = i + 4)
			omap_aes_write(dd, i, 0x0);

	for (i = 0; i < key32; i++) {
		omap_aes_write(dd, AES_REG_KEY(dd, i),
			       (__force u32)cpu_to_le32(dd->ctx->key[i]));
	}

	if ((dd->flags & (FLAGS_CBC | FLAGS_CTR)) && dd->req->iv)
		omap_aes_write_n(dd, AES_REG_IV(dd, 0), (void *)dd->req->iv, 4);

	if ((dd->flags & (FLAGS_GCM)) && dd->aead_req->iv) {
		rctx = aead_request_ctx(dd->aead_req);
		omap_aes_write_n(dd, AES_REG_IV(dd, 0), (u32 *)rctx->iv, 4);
	}

	val = FLD_VAL(((dd->ctx->keylen >> 3) - 1), 4, 3);
	if (dd->flags & FLAGS_CBC)
		val |= AES_REG_CTRL_CBC;

	if (dd->flags & (FLAGS_CTR | FLAGS_GCM))
		val |= AES_REG_CTRL_CTR | AES_REG_CTRL_CTR_WIDTH_128;

	if (dd->flags & FLAGS_GCM)
		val |= AES_REG_CTRL_GCM;

	if (dd->flags & FLAGS_ENCRYPT)
		val |= AES_REG_CTRL_DIRECTION;

	omap_aes_write_mask(dd, AES_REG_CTRL(dd), val, AES_REG_CTRL_MASK);

	return 0;
}

static void omap_aes_dma_trigger_omap2(struct omap_aes_dev *dd, int length)
{
	u32 mask, val;

	val = dd->pdata->dma_start;

	if (dd->dma_lch_out != NULL)
		val |= dd->pdata->dma_enable_out;
	if (dd->dma_lch_in != NULL)
		val |= dd->pdata->dma_enable_in;

	mask = dd->pdata->dma_enable_out | dd->pdata->dma_enable_in |
	       dd->pdata->dma_start;

	omap_aes_write_mask(dd, AES_REG_MASK(dd), val, mask);

}

static void omap_aes_dma_trigger_omap4(struct omap_aes_dev *dd, int length)
{
	omap_aes_write(dd, AES_REG_LENGTH_N(0), length);
	omap_aes_write(dd, AES_REG_LENGTH_N(1), 0);
	if (dd->flags & FLAGS_GCM)
		omap_aes_write(dd, AES_REG_A_LEN, dd->assoc_len);

	omap_aes_dma_trigger_omap2(dd, length);
}

static void omap_aes_dma_stop(struct omap_aes_dev *dd)
{
	u32 mask;

	mask = dd->pdata->dma_enable_out | dd->pdata->dma_enable_in |
	       dd->pdata->dma_start;

	omap_aes_write_mask(dd, AES_REG_MASK(dd), 0, mask);
}

struct omap_aes_dev *omap_aes_find_dev(struct omap_aes_reqctx *rctx)
{
	struct omap_aes_dev *dd;

	spin_lock_bh(&list_lock);
	dd = list_first_entry(&dev_list, struct omap_aes_dev, list);
	list_move_tail(&dd->list, &dev_list);
	rctx->dd = dd;
	spin_unlock_bh(&list_lock);

	return dd;
}

static void omap_aes_dma_out_callback(void *data)
{
	struct omap_aes_dev *dd = data;

	/* dma_lch_out - completed */
	tasklet_schedule(&dd->done_task);
}

static int omap_aes_dma_init(struct omap_aes_dev *dd)
{
	int err;

	dd->dma_lch_out = NULL;
	dd->dma_lch_in = NULL;

	dd->dma_lch_in = dma_request_chan(dd->dev, "rx");
	if (IS_ERR(dd->dma_lch_in)) {
		dev_err(dd->dev, "Unable to request in DMA channel\n");
		return PTR_ERR(dd->dma_lch_in);
	}

	dd->dma_lch_out = dma_request_chan(dd->dev, "tx");
	if (IS_ERR(dd->dma_lch_out)) {
		dev_err(dd->dev, "Unable to request out DMA channel\n");
		err = PTR_ERR(dd->dma_lch_out);
		goto err_dma_out;
	}

	return 0;

err_dma_out:
	dma_release_channel(dd->dma_lch_in);

	return err;
}

static void omap_aes_dma_cleanup(struct omap_aes_dev *dd)
{
	if (dd->pio_only)
		return;

	dma_release_channel(dd->dma_lch_out);
	dma_release_channel(dd->dma_lch_in);
}

static int omap_aes_crypt_dma(struct omap_aes_dev *dd,
			      struct scatterlist *in_sg,
			      struct scatterlist *out_sg,
			      int in_sg_len, int out_sg_len)
{
	struct dma_async_tx_descriptor *tx_in, *tx_out = NULL, *cb_desc;
	struct dma_slave_config cfg;
	int ret;

	if (dd->pio_only) {
		dd->in_sg_offset = 0;
		if (out_sg_len)
			dd->out_sg_offset = 0;

		/* Enable DATAIN interrupt and let it take
		   care of the rest */
		omap_aes_write(dd, AES_REG_IRQ_ENABLE(dd), 0x2);
		return 0;
	}

	dma_sync_sg_for_device(dd->dev, dd->in_sg, in_sg_len, DMA_TO_DEVICE);

	memset(&cfg, 0, sizeof(cfg));

	cfg.src_addr = dd->phys_base + AES_REG_DATA_N(dd, 0);
	cfg.dst_addr = dd->phys_base + AES_REG_DATA_N(dd, 0);
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
	tx_in->callback = NULL;

	/* OUT */
	if (out_sg_len) {
		ret = dmaengine_slave_config(dd->dma_lch_out, &cfg);
		if (ret) {
			dev_err(dd->dev, "can't configure OUT dmaengine slave: %d\n",
				ret);
			return ret;
		}

		tx_out = dmaengine_prep_slave_sg(dd->dma_lch_out, out_sg,
						 out_sg_len,
						 DMA_DEV_TO_MEM,
						 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!tx_out) {
			dev_err(dd->dev, "OUT prep_slave_sg() failed\n");
			return -EINVAL;
		}

		cb_desc = tx_out;
	} else {
		cb_desc = tx_in;
	}

	if (dd->flags & FLAGS_GCM)
		cb_desc->callback = omap_aes_gcm_dma_out_callback;
	else
		cb_desc->callback = omap_aes_dma_out_callback;
	cb_desc->callback_param = dd;


	dmaengine_submit(tx_in);
	if (tx_out)
		dmaengine_submit(tx_out);

	dma_async_issue_pending(dd->dma_lch_in);
	if (out_sg_len)
		dma_async_issue_pending(dd->dma_lch_out);

	/* start DMA */
	dd->pdata->trigger(dd, dd->total);

	return 0;
}

int omap_aes_crypt_dma_start(struct omap_aes_dev *dd)
{
	int err;

	pr_debug("total: %zu\n", dd->total);

	if (!dd->pio_only) {
		err = dma_map_sg(dd->dev, dd->in_sg, dd->in_sg_len,
				 DMA_TO_DEVICE);
		if (!err) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			return -EINVAL;
		}

		if (dd->out_sg_len) {
			err = dma_map_sg(dd->dev, dd->out_sg, dd->out_sg_len,
					 DMA_FROM_DEVICE);
			if (!err) {
				dev_err(dd->dev, "dma_map_sg() error\n");
				return -EINVAL;
			}
		}
	}

	err = omap_aes_crypt_dma(dd, dd->in_sg, dd->out_sg, dd->in_sg_len,
				 dd->out_sg_len);
	if (err && !dd->pio_only) {
		dma_unmap_sg(dd->dev, dd->in_sg, dd->in_sg_len, DMA_TO_DEVICE);
		if (dd->out_sg_len)
			dma_unmap_sg(dd->dev, dd->out_sg, dd->out_sg_len,
				     DMA_FROM_DEVICE);
	}

	return err;
}

static void omap_aes_finish_req(struct omap_aes_dev *dd, int err)
{
	struct skcipher_request *req = dd->req;

	pr_debug("err: %d\n", err);

	crypto_finalize_skcipher_request(dd->engine, req, err);

	pm_runtime_mark_last_busy(dd->dev);
	pm_runtime_put_autosuspend(dd->dev);
}

int omap_aes_crypt_dma_stop(struct omap_aes_dev *dd)
{
	pr_debug("total: %zu\n", dd->total);

	omap_aes_dma_stop(dd);


	return 0;
}

static int omap_aes_handle_queue(struct omap_aes_dev *dd,
				 struct skcipher_request *req)
{
	if (req)
		return crypto_transfer_skcipher_request_to_engine(dd->engine, req);

	return 0;
}

static int omap_aes_prepare_req(struct skcipher_request *req,
				struct omap_aes_dev *dd)
{
	struct omap_aes_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct omap_aes_reqctx *rctx = skcipher_request_ctx(req);
	int ret;
	u16 flags;

	/* assign new request to device */
	dd->req = req;
	dd->total = req->cryptlen;
	dd->total_save = req->cryptlen;
	dd->in_sg = req->src;
	dd->out_sg = req->dst;
	dd->orig_out = req->dst;

	flags = OMAP_CRYPTO_COPY_DATA;
	if (req->src == req->dst)
		flags |= OMAP_CRYPTO_FORCE_COPY;

	ret = omap_crypto_align_sg(&dd->in_sg, dd->total, AES_BLOCK_SIZE,
				   dd->in_sgl, flags,
				   FLAGS_IN_DATA_ST_SHIFT, &dd->flags);
	if (ret)
		return ret;

	ret = omap_crypto_align_sg(&dd->out_sg, dd->total, AES_BLOCK_SIZE,
				   &dd->out_sgl, 0,
				   FLAGS_OUT_DATA_ST_SHIFT, &dd->flags);
	if (ret)
		return ret;

	dd->in_sg_len = sg_nents_for_len(dd->in_sg, dd->total);
	if (dd->in_sg_len < 0)
		return dd->in_sg_len;

	dd->out_sg_len = sg_nents_for_len(dd->out_sg, dd->total);
	if (dd->out_sg_len < 0)
		return dd->out_sg_len;

	rctx->mode &= FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	dd->ctx = ctx;
	rctx->dd = dd;

	return omap_aes_write_ctrl(dd);
}

static int omap_aes_crypt_req(struct crypto_engine *engine,
			      void *areq)
{
	struct skcipher_request *req = container_of(areq, struct skcipher_request, base);
	struct omap_aes_reqctx *rctx = skcipher_request_ctx(req);
	struct omap_aes_dev *dd = rctx->dd;

	if (!dd)
		return -ENODEV;

	return omap_aes_prepare_req(req, dd) ?:
	       omap_aes_crypt_dma_start(dd);
}

static void omap_aes_copy_ivout(struct omap_aes_dev *dd, u8 *ivbuf)
{
	int i;

	for (i = 0; i < 4; i++)
		((u32 *)ivbuf)[i] = omap_aes_read(dd, AES_REG_IV(dd, i));
}

static void omap_aes_done_task(unsigned long data)
{
	struct omap_aes_dev *dd = (struct omap_aes_dev *)data;

	pr_debug("enter done_task\n");

	if (!dd->pio_only) {
		dma_sync_sg_for_device(dd->dev, dd->out_sg, dd->out_sg_len,
				       DMA_FROM_DEVICE);
		dma_unmap_sg(dd->dev, dd->in_sg, dd->in_sg_len, DMA_TO_DEVICE);
		dma_unmap_sg(dd->dev, dd->out_sg, dd->out_sg_len,
			     DMA_FROM_DEVICE);
		omap_aes_crypt_dma_stop(dd);
	}

	omap_crypto_cleanup(dd->in_sg, NULL, 0, dd->total_save,
			    FLAGS_IN_DATA_ST_SHIFT, dd->flags);

	omap_crypto_cleanup(dd->out_sg, dd->orig_out, 0, dd->total_save,
			    FLAGS_OUT_DATA_ST_SHIFT, dd->flags);

	/* Update IV output */
	if (dd->flags & (FLAGS_CBC | FLAGS_CTR))
		omap_aes_copy_ivout(dd, dd->req->iv);

	omap_aes_finish_req(dd, 0);

	pr_debug("exit\n");
}

static int omap_aes_crypt(struct skcipher_request *req, unsigned long mode)
{
	struct omap_aes_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct omap_aes_reqctx *rctx = skcipher_request_ctx(req);
	struct omap_aes_dev *dd;
	int ret;

	if ((req->cryptlen % AES_BLOCK_SIZE) && !(mode & FLAGS_CTR))
		return -EINVAL;

	pr_debug("nbytes: %d, enc: %d, cbc: %d\n", req->cryptlen,
		  !!(mode & FLAGS_ENCRYPT),
		  !!(mode & FLAGS_CBC));

	if (req->cryptlen < aes_fallback_sz) {
		skcipher_request_set_tfm(&rctx->fallback_req, ctx->fallback);
		skcipher_request_set_callback(&rctx->fallback_req,
					      req->base.flags,
					      req->base.complete,
					      req->base.data);
		skcipher_request_set_crypt(&rctx->fallback_req, req->src,
					   req->dst, req->cryptlen, req->iv);

		if (mode & FLAGS_ENCRYPT)
			ret = crypto_skcipher_encrypt(&rctx->fallback_req);
		else
			ret = crypto_skcipher_decrypt(&rctx->fallback_req);
		return ret;
	}
	dd = omap_aes_find_dev(rctx);
	if (!dd)
		return -ENODEV;

	rctx->mode = mode;

	return omap_aes_handle_queue(dd, req);
}

/* ********************** ALG API ************************************ */

static int omap_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct omap_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
		   keylen != AES_KEYSIZE_256)
		return -EINVAL;

	pr_debug("enter, keylen: %d\n", keylen);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	crypto_skcipher_clear_flags(ctx->fallback, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctx->fallback, tfm->base.crt_flags &
						 CRYPTO_TFM_REQ_MASK);

	ret = crypto_skcipher_setkey(ctx->fallback, key, keylen);
	if (!ret)
		return 0;

	return 0;
}

static int omap_aes_ecb_encrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_ENCRYPT);
}

static int omap_aes_ecb_decrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, 0);
}

static int omap_aes_cbc_encrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_ENCRYPT | FLAGS_CBC);
}

static int omap_aes_cbc_decrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_CBC);
}

static int omap_aes_ctr_encrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_ENCRYPT | FLAGS_CTR);
}

static int omap_aes_ctr_decrypt(struct skcipher_request *req)
{
	return omap_aes_crypt(req, FLAGS_CTR);
}

static int omap_aes_init_tfm(struct crypto_skcipher *tfm)
{
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct omap_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *blk;

	blk = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(blk))
		return PTR_ERR(blk);

	ctx->fallback = blk;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct omap_aes_reqctx) +
					 crypto_skcipher_reqsize(blk));

	return 0;
}

static void omap_aes_exit_tfm(struct crypto_skcipher *tfm)
{
	struct omap_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (ctx->fallback)
		crypto_free_skcipher(ctx->fallback);

	ctx->fallback = NULL;
}

/* ********************** ALGS ************************************ */

static struct skcipher_engine_alg algs_ecb_cbc[] = {
{
	.base = {
		.base.cra_name		= "ecb(aes)",
		.base.cra_driver_name	= "ecb-aes-omap",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct omap_aes_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.setkey			= omap_aes_setkey,
		.encrypt		= omap_aes_ecb_encrypt,
		.decrypt		= omap_aes_ecb_decrypt,
		.init			= omap_aes_init_tfm,
		.exit			= omap_aes_exit_tfm,
	},
	.op.do_one_request = omap_aes_crypt_req,
},
{
	.base = {
		.base.cra_name		= "cbc(aes)",
		.base.cra_driver_name	= "cbc-aes-omap",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct omap_aes_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
		.setkey			= omap_aes_setkey,
		.encrypt		= omap_aes_cbc_encrypt,
		.decrypt		= omap_aes_cbc_decrypt,
		.init			= omap_aes_init_tfm,
		.exit			= omap_aes_exit_tfm,
	},
	.op.do_one_request = omap_aes_crypt_req,
}
};

static struct skcipher_engine_alg algs_ctr[] = {
{
	.base = {
		.base.cra_name		= "ctr(aes)",
		.base.cra_driver_name	= "ctr-aes-omap",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct omap_aes_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
		.setkey			= omap_aes_setkey,
		.encrypt		= omap_aes_ctr_encrypt,
		.decrypt		= omap_aes_ctr_decrypt,
		.init			= omap_aes_init_tfm,
		.exit			= omap_aes_exit_tfm,
	},
	.op.do_one_request = omap_aes_crypt_req,
}
};

static struct omap_aes_algs_info omap_aes_algs_info_ecb_cbc[] = {
	{
		.algs_list	= algs_ecb_cbc,
		.size		= ARRAY_SIZE(algs_ecb_cbc),
	},
};

static struct aead_engine_alg algs_aead_gcm[] = {
{
	.base = {
		.base = {
			.cra_name		= "gcm(aes)",
			.cra_driver_name	= "gcm-aes-omap",
			.cra_priority		= 300,
			.cra_flags		= CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct omap_aes_gcm_ctx),
			.cra_alignmask		= 0xf,
			.cra_module		= THIS_MODULE,
		},
		.init		= omap_aes_gcm_cra_init,
		.ivsize		= GCM_AES_IV_SIZE,
		.maxauthsize	= AES_BLOCK_SIZE,
		.setkey		= omap_aes_gcm_setkey,
		.setauthsize	= omap_aes_gcm_setauthsize,
		.encrypt	= omap_aes_gcm_encrypt,
		.decrypt	= omap_aes_gcm_decrypt,
	},
	.op.do_one_request = omap_aes_gcm_crypt_req,
},
{
	.base = {
		.base = {
			.cra_name		= "rfc4106(gcm(aes))",
			.cra_driver_name	= "rfc4106-gcm-aes-omap",
			.cra_priority		= 300,
			.cra_flags		= CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct omap_aes_gcm_ctx),
			.cra_alignmask		= 0xf,
			.cra_module		= THIS_MODULE,
		},
		.init		= omap_aes_gcm_cra_init,
		.maxauthsize	= AES_BLOCK_SIZE,
		.ivsize		= GCM_RFC4106_IV_SIZE,
		.setkey		= omap_aes_4106gcm_setkey,
		.setauthsize	= omap_aes_4106gcm_setauthsize,
		.encrypt	= omap_aes_4106gcm_encrypt,
		.decrypt	= omap_aes_4106gcm_decrypt,
	},
	.op.do_one_request = omap_aes_gcm_crypt_req,
},
};

static struct omap_aes_aead_algs omap_aes_aead_info = {
	.algs_list	=	algs_aead_gcm,
	.size		=	ARRAY_SIZE(algs_aead_gcm),
};

static const struct omap_aes_pdata omap_aes_pdata_omap2 = {
	.algs_info	= omap_aes_algs_info_ecb_cbc,
	.algs_info_size	= ARRAY_SIZE(omap_aes_algs_info_ecb_cbc),
	.trigger	= omap_aes_dma_trigger_omap2,
	.key_ofs	= 0x1c,
	.iv_ofs		= 0x20,
	.ctrl_ofs	= 0x30,
	.data_ofs	= 0x34,
	.rev_ofs	= 0x44,
	.mask_ofs	= 0x48,
	.dma_enable_in	= BIT(2),
	.dma_enable_out	= BIT(3),
	.dma_start	= BIT(5),
	.major_mask	= 0xf0,
	.major_shift	= 4,
	.minor_mask	= 0x0f,
	.minor_shift	= 0,
};

#ifdef CONFIG_OF
static struct omap_aes_algs_info omap_aes_algs_info_ecb_cbc_ctr[] = {
	{
		.algs_list	= algs_ecb_cbc,
		.size		= ARRAY_SIZE(algs_ecb_cbc),
	},
	{
		.algs_list	= algs_ctr,
		.size		= ARRAY_SIZE(algs_ctr),
	},
};

static const struct omap_aes_pdata omap_aes_pdata_omap3 = {
	.algs_info	= omap_aes_algs_info_ecb_cbc_ctr,
	.algs_info_size	= ARRAY_SIZE(omap_aes_algs_info_ecb_cbc_ctr),
	.trigger	= omap_aes_dma_trigger_omap2,
	.key_ofs	= 0x1c,
	.iv_ofs		= 0x20,
	.ctrl_ofs	= 0x30,
	.data_ofs	= 0x34,
	.rev_ofs	= 0x44,
	.mask_ofs	= 0x48,
	.dma_enable_in	= BIT(2),
	.dma_enable_out	= BIT(3),
	.dma_start	= BIT(5),
	.major_mask	= 0xf0,
	.major_shift	= 4,
	.minor_mask	= 0x0f,
	.minor_shift	= 0,
};

static const struct omap_aes_pdata omap_aes_pdata_omap4 = {
	.algs_info	= omap_aes_algs_info_ecb_cbc_ctr,
	.algs_info_size	= ARRAY_SIZE(omap_aes_algs_info_ecb_cbc_ctr),
	.aead_algs_info	= &omap_aes_aead_info,
	.trigger	= omap_aes_dma_trigger_omap4,
	.key_ofs	= 0x3c,
	.iv_ofs		= 0x40,
	.ctrl_ofs	= 0x50,
	.data_ofs	= 0x60,
	.rev_ofs	= 0x80,
	.mask_ofs	= 0x84,
	.irq_status_ofs = 0x8c,
	.irq_enable_ofs = 0x90,
	.dma_enable_in	= BIT(5),
	.dma_enable_out	= BIT(6),
	.major_mask	= 0x0700,
	.major_shift	= 8,
	.minor_mask	= 0x003f,
	.minor_shift	= 0,
};

static irqreturn_t omap_aes_irq(int irq, void *dev_id)
{
	struct omap_aes_dev *dd = dev_id;
	u32 status, i;
	u32 *src, *dst;

	status = omap_aes_read(dd, AES_REG_IRQ_STATUS(dd));
	if (status & AES_REG_IRQ_DATA_IN) {
		omap_aes_write(dd, AES_REG_IRQ_ENABLE(dd), 0x0);

		BUG_ON(!dd->in_sg);

		BUG_ON(dd->in_sg_offset > dd->in_sg->length);

		src = sg_virt(dd->in_sg) + dd->in_sg_offset;

		for (i = 0; i < AES_BLOCK_WORDS; i++) {
			omap_aes_write(dd, AES_REG_DATA_N(dd, i), *src);
			dd->in_sg_offset += 4;
			if (dd->in_sg_offset == dd->in_sg->length) {
				dd->in_sg = sg_next(dd->in_sg);
				if (dd->in_sg) {
					dd->in_sg_offset = 0;
					src = sg_virt(dd->in_sg);
				}
			} else {
				src++;
			}
		}

		/* Clear IRQ status */
		status &= ~AES_REG_IRQ_DATA_IN;
		omap_aes_write(dd, AES_REG_IRQ_STATUS(dd), status);

		/* Enable DATA_OUT interrupt */
		omap_aes_write(dd, AES_REG_IRQ_ENABLE(dd), 0x4);

	} else if (status & AES_REG_IRQ_DATA_OUT) {
		omap_aes_write(dd, AES_REG_IRQ_ENABLE(dd), 0x0);

		BUG_ON(!dd->out_sg);

		BUG_ON(dd->out_sg_offset > dd->out_sg->length);

		dst = sg_virt(dd->out_sg) + dd->out_sg_offset;

		for (i = 0; i < AES_BLOCK_WORDS; i++) {
			*dst = omap_aes_read(dd, AES_REG_DATA_N(dd, i));
			dd->out_sg_offset += 4;
			if (dd->out_sg_offset == dd->out_sg->length) {
				dd->out_sg = sg_next(dd->out_sg);
				if (dd->out_sg) {
					dd->out_sg_offset = 0;
					dst = sg_virt(dd->out_sg);
				}
			} else {
				dst++;
			}
		}

		dd->total -= min_t(size_t, AES_BLOCK_SIZE, dd->total);

		/* Clear IRQ status */
		status &= ~AES_REG_IRQ_DATA_OUT;
		omap_aes_write(dd, AES_REG_IRQ_STATUS(dd), status);

		if (!dd->total)
			/* All bytes read! */
			tasklet_schedule(&dd->done_task);
		else
			/* Enable DATA_IN interrupt for next block */
			omap_aes_write(dd, AES_REG_IRQ_ENABLE(dd), 0x2);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id omap_aes_of_match[] = {
	{
		.compatible	= "ti,omap2-aes",
		.data		= &omap_aes_pdata_omap2,
	},
	{
		.compatible	= "ti,omap3-aes",
		.data		= &omap_aes_pdata_omap3,
	},
	{
		.compatible	= "ti,omap4-aes",
		.data		= &omap_aes_pdata_omap4,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_aes_of_match);

static int omap_aes_get_res_of(struct omap_aes_dev *dd,
		struct device *dev, struct resource *res)
{
	struct device_node *node = dev->of_node;
	int err = 0;

	dd->pdata = of_device_get_match_data(dev);
	if (!dd->pdata) {
		dev_err(dev, "no compatible OF match\n");
		err = -EINVAL;
		goto err;
	}

	err = of_address_to_resource(node, 0, res);
	if (err < 0) {
		dev_err(dev, "can't translate OF node address\n");
		err = -EINVAL;
		goto err;
	}

err:
	return err;
}
#else
static const struct of_device_id omap_aes_of_match[] = {
	{},
};

static int omap_aes_get_res_of(struct omap_aes_dev *dd,
		struct device *dev, struct resource *res)
{
	return -EINVAL;
}
#endif

static int omap_aes_get_res_pdev(struct omap_aes_dev *dd,
		struct platform_device *pdev, struct resource *res)
{
	struct device *dev = &pdev->dev;
	struct resource *r;
	int err = 0;

	/* Get the base address */
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(dev, "no MEM resource info\n");
		err = -ENODEV;
		goto err;
	}
	memcpy(res, r, sizeof(*res));

	/* Only OMAP2/3 can be non-DT */
	dd->pdata = &omap_aes_pdata_omap2;

err:
	return err;
}

static ssize_t fallback_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", aes_fallback_sz);
}

static ssize_t fallback_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t size)
{
	ssize_t status;
	long value;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	/* HW accelerator only works with buffers > 9 */
	if (value < 9) {
		dev_err(dev, "minimum fallback size 9\n");
		return -EINVAL;
	}

	aes_fallback_sz = value;

	return size;
}

static ssize_t queue_len_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct omap_aes_dev *dd = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dd->engine->queue.max_qlen);
}

static ssize_t queue_len_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t size)
{
	struct omap_aes_dev *dd;
	ssize_t status;
	long value;
	unsigned long flags;

	status = kstrtol(buf, 0, &value);
	if (status)
		return status;

	if (value < 1)
		return -EINVAL;

	/*
	 * Changing the queue size in fly is safe, if size becomes smaller
	 * than current size, it will just not accept new entries until
	 * it has shrank enough.
	 */
	spin_lock_bh(&list_lock);
	list_for_each_entry(dd, &dev_list, list) {
		spin_lock_irqsave(&dd->lock, flags);
		dd->engine->queue.max_qlen = value;
		dd->aead_queue.base.max_qlen = value;
		spin_unlock_irqrestore(&dd->lock, flags);
	}
	spin_unlock_bh(&list_lock);

	return size;
}

static DEVICE_ATTR_RW(queue_len);
static DEVICE_ATTR_RW(fallback);

static struct attribute *omap_aes_attrs[] = {
	&dev_attr_queue_len.attr,
	&dev_attr_fallback.attr,
	NULL,
};

static const struct attribute_group omap_aes_attr_group = {
	.attrs = omap_aes_attrs,
};

static int omap_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct omap_aes_dev *dd;
	struct skcipher_engine_alg *algp;
	struct aead_engine_alg *aalg;
	struct resource res;
	int err = -ENOMEM, i, j, irq = -1;
	u32 reg;

	dd = devm_kzalloc(dev, sizeof(struct omap_aes_dev), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		goto err_data;
	}
	dd->dev = dev;
	platform_set_drvdata(pdev, dd);

	aead_init_queue(&dd->aead_queue, OMAP_AES_QUEUE_LENGTH);

	err = (dev->of_node) ? omap_aes_get_res_of(dd, dev, &res) :
			       omap_aes_get_res_pdev(dd, pdev, &res);
	if (err)
		goto err_res;

	dd->io_base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(dd->io_base)) {
		err = PTR_ERR(dd->io_base);
		goto err_res;
	}
	dd->phys_base = res.start;

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, DEFAULT_AUTOSUSPEND_DELAY);

	pm_runtime_enable(dev);
	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "%s: failed to get_sync(%d)\n",
			__func__, err);
		goto err_pm_disable;
	}

	omap_aes_dma_stop(dd);

	reg = omap_aes_read(dd, AES_REG_REV(dd));

	pm_runtime_put_sync(dev);

	dev_info(dev, "OMAP AES hw accel rev: %u.%u\n",
		 (reg & dd->pdata->major_mask) >> dd->pdata->major_shift,
		 (reg & dd->pdata->minor_mask) >> dd->pdata->minor_shift);

	tasklet_init(&dd->done_task, omap_aes_done_task, (unsigned long)dd);

	err = omap_aes_dma_init(dd);
	if (err == -EPROBE_DEFER) {
		goto err_irq;
	} else if (err && AES_REG_IRQ_STATUS(dd) && AES_REG_IRQ_ENABLE(dd)) {
		dd->pio_only = 1;

		irq = platform_get_irq(pdev, 0);
		if (irq < 0) {
			err = irq;
			goto err_irq;
		}

		err = devm_request_irq(dev, irq, omap_aes_irq, 0,
				dev_name(dev), dd);
		if (err) {
			dev_err(dev, "Unable to grab omap-aes IRQ\n");
			goto err_irq;
		}
	}

	spin_lock_init(&dd->lock);

	INIT_LIST_HEAD(&dd->list);
	spin_lock_bh(&list_lock);
	list_add_tail(&dd->list, &dev_list);
	spin_unlock_bh(&list_lock);

	/* Initialize crypto engine */
	dd->engine = crypto_engine_alloc_init(dev, 1);
	if (!dd->engine) {
		err = -ENOMEM;
		goto err_engine;
	}

	err = crypto_engine_start(dd->engine);
	if (err)
		goto err_engine;

	for (i = 0; i < dd->pdata->algs_info_size; i++) {
		if (!dd->pdata->algs_info[i].registered) {
			for (j = 0; j < dd->pdata->algs_info[i].size; j++) {
				algp = &dd->pdata->algs_info[i].algs_list[j];

				pr_debug("reg alg: %s\n", algp->base.base.cra_name);

				err = crypto_engine_register_skcipher(algp);
				if (err)
					goto err_algs;

				dd->pdata->algs_info[i].registered++;
			}
		}
	}

	if (dd->pdata->aead_algs_info &&
	    !dd->pdata->aead_algs_info->registered) {
		for (i = 0; i < dd->pdata->aead_algs_info->size; i++) {
			aalg = &dd->pdata->aead_algs_info->algs_list[i];

			pr_debug("reg alg: %s\n", aalg->base.base.cra_name);

			err = crypto_engine_register_aead(aalg);
			if (err)
				goto err_aead_algs;

			dd->pdata->aead_algs_info->registered++;
		}
	}

	err = sysfs_create_group(&dev->kobj, &omap_aes_attr_group);
	if (err) {
		dev_err(dev, "could not create sysfs device attrs\n");
		goto err_aead_algs;
	}

	return 0;
err_aead_algs:
	for (i = dd->pdata->aead_algs_info->registered - 1; i >= 0; i--) {
		aalg = &dd->pdata->aead_algs_info->algs_list[i];
		crypto_engine_unregister_aead(aalg);
	}
err_algs:
	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--)
			crypto_engine_unregister_skcipher(
					&dd->pdata->algs_info[i].algs_list[j]);

err_engine:
	if (dd->engine)
		crypto_engine_exit(dd->engine);

	omap_aes_dma_cleanup(dd);
err_irq:
	tasklet_kill(&dd->done_task);
err_pm_disable:
	pm_runtime_disable(dev);
err_res:
	dd = NULL;
err_data:
	dev_err(dev, "initialization failed.\n");
	return err;
}

static void omap_aes_remove(struct platform_device *pdev)
{
	struct omap_aes_dev *dd = platform_get_drvdata(pdev);
	struct aead_engine_alg *aalg;
	int i, j;

	spin_lock_bh(&list_lock);
	list_del(&dd->list);
	spin_unlock_bh(&list_lock);

	for (i = dd->pdata->algs_info_size - 1; i >= 0; i--)
		for (j = dd->pdata->algs_info[i].registered - 1; j >= 0; j--) {
			crypto_engine_unregister_skcipher(
					&dd->pdata->algs_info[i].algs_list[j]);
			dd->pdata->algs_info[i].registered--;
		}

	for (i = dd->pdata->aead_algs_info->registered - 1; i >= 0; i--) {
		aalg = &dd->pdata->aead_algs_info->algs_list[i];
		crypto_engine_unregister_aead(aalg);
		dd->pdata->aead_algs_info->registered--;
	}

	crypto_engine_exit(dd->engine);

	tasklet_kill(&dd->done_task);
	omap_aes_dma_cleanup(dd);
	pm_runtime_disable(dd->dev);

	sysfs_remove_group(&dd->dev->kobj, &omap_aes_attr_group);
}

#ifdef CONFIG_PM_SLEEP
static int omap_aes_suspend(struct device *dev)
{
	pm_runtime_put_sync(dev);
	return 0;
}

static int omap_aes_resume(struct device *dev)
{
	pm_runtime_get_sync(dev);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(omap_aes_pm_ops, omap_aes_suspend, omap_aes_resume);

static struct platform_driver omap_aes_driver = {
	.probe	= omap_aes_probe,
	.remove = omap_aes_remove,
	.driver	= {
		.name	= "omap-aes",
		.pm	= &omap_aes_pm_ops,
		.of_match_table	= omap_aes_of_match,
	},
};

module_platform_driver(omap_aes_driver);

MODULE_DESCRIPTION("OMAP AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dmitry Kasatkin");

