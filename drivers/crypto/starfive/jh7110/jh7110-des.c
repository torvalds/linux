// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 StarFive, Inc <william.qiu@starfivetech.com>
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
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <crypto/hash.h>

#include <linux/dma-direct.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/des.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "jh7110-pl080.h"
#include "jh7110-str.h"

#define FLG_DES_MODE_MASK					GENMASK(1, 0)

#define FLAGS_DES_ENCRYPT					BIT(4)

enum {
	JH7110_CRYPTO_NOT_ALIGNED = 1,
	JH7110_CRYPTO_BAD_DATA_LENGTH,
};

#define JH7110_CRYPTO_DATA_COPIED			BIT(0)
#define JH7110_CRYPTO_SG_COPIED				BIT(1)

#define JH7110_CRYPTO_COPY_MASK				0x3

#define JH7110_CRYPTO_COPY_DATA				BIT(0)
#define JH7110_CRYPTO_FORCE_COPY			BIT(1)
#define JH7110_CRYPTO_ZERO_BUF				BIT(2)
#define JH7110_CRYPTO_FORCE_SINGLE_ENTRY	BIT(3)

#define FLAGS_IN_DATA_ST_SHIFT				8
#define FLAGS_OUT_DATA_ST_SHIFT				10


static inline int jh7110_des_wait_busy(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	u32 status;

	return readl_relaxed_poll_timeout(sdev->io_base + JH7110_DES_DAECSR_OFFSET, status,
				   !(status & JH7110_DES_BUSY), 10, 100000);
}

static inline int jh7110_des_wait_done(struct jh7110_sec_dev *sdev)
{
	int ret = -1;

	mutex_lock(&sdev->doing);

	if (sdev->done_flags & JH7110_DES_DONE)
		ret = 0;

	mutex_unlock(&sdev->doing);

	return ret;
}

static inline int is_des_ecb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_DES_MODE_MASK) == JH7110_DES_MODE_ECB;
}

static inline int is_des_cbc(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_DES_MODE_MASK) == JH7110_DES_MODE_CBC;
}

static inline int is_des_ofb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_DES_MODE_MASK) == JH7110_DES_MODE_OFB;
}

static inline int is_des_cfb(struct jh7110_sec_request_ctx *rctx)
{
	return (rctx->flags & FLG_DES_MODE_MASK) == JH7110_DES_MODE_CFB;
}

static inline int get_des_mode(struct jh7110_sec_request_ctx *rctx)
{
	return rctx->flags & FLG_DES_MODE_MASK;
}

static inline int is_des_encrypt(struct jh7110_sec_request_ctx *rctx)
{
	return !!(rctx->flags & FLAGS_DES_ENCRYPT);
}

static inline int is_des_decrypt(struct jh7110_sec_request_ctx *rctx)
{
	return !is_des_encrypt(rctx);
}

static inline void jh7110_des_reset(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;

	rctx->csr.des_csr.v = 0;
	rctx->csr.des_csr.reset = 1;
	jh7110_sec_write(ctx->sdev, JH7110_DES_DAECSR_OFFSET, rctx->csr.des_csr.v);

}

static int jh7110_des_hw_write_key(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	u64 key64, *k;
	int loop;

	key64 = ctx->keylen / sizeof(u64);

	switch (key64) {
	case 1:
		rctx->csr.des_csr.ks = 0x0;
		break;
	case 2:
		rctx->csr.des_csr.ks = 0x2;
		break;
	case 3:
		rctx->csr.des_csr.ks = 0x3;
		break;
	}

	jh7110_sec_write(ctx->sdev, JH7110_DES_DAECSR_OFFSET, rctx->csr.des_csr.v);

	k = (u64 *)ctx->key;
	for (loop = 0; loop < key64; loop++) {
		jh7110_sec_writeq(sdev, JH7110_DES_DAEKIN1R_HI_OFFSET + loop * 8, *k);
		k++;
	}

	return 0;
}



static int jh7110_des_hw_init(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret;
	u32 hw_mode;

	jh7110_des_reset(ctx);

	hw_mode = get_des_mode(ctx->rctx);

	rctx->csr.des_csr.v = 0;
	rctx->csr.des_csr.mode = rctx->flags & FLG_DES_MODE_MASK;
	rctx->csr.des_csr.encryt = !!(rctx->flags & FLAGS_DES_ENCRYPT);
	rctx->csr.des_csr.ie = 1;
	rctx->csr.des_csr.bitmode = JH7110_DES_BITMODE_64;
	rctx->csr.des_csr.en = 1;
	rctx->csr.des_csr.disturb = 1;
	rctx->csr.des_csr.vdes_en = 1;

	switch (hw_mode) {
	case JH7110_DES_MODE_ECB:
		ret = jh7110_des_hw_write_key(ctx);
		break;
	case JH7110_DES_MODE_OFB:
	case JH7110_DES_MODE_CFB:
	case JH7110_DES_MODE_CBC:
		jh7110_sec_writeq(sdev, JH7110_DES_DAEIVINR_HI_OFFSET, *(u64 *)rctx->req.sreq->iv);
		break;
	default:
		break;
	}

	return ret;
}

static void jh7110_des_dma_callback(void *param)
{
	struct jh7110_sec_dev *sdev = param;

	complete(&sdev->sec_comp_p);
}

static int jh7110_des_dma_start(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct dma_async_tx_descriptor	*in_desc, *out_desc;
	struct dma_slave_config	cfg;
	int err;
	int ret;

	rctx->total = (rctx->total & 0x3) ? 1 : 0;
	sg_init_table(&sdev->in_sg, 1);
	sg_set_buf(&sdev->in_sg, sdev->des_data, rctx->total);
	sg_dma_address(&sdev->in_sg) = phys_to_dma(sdev->dev, (unsigned long long)(sdev->des_data));
	sg_dma_len(&sdev->in_sg) = rctx->total;

	sg_init_table(&sdev->out_sg, 1);
	sg_set_buf(&sdev->out_sg, sdev->des_data, rctx->total);
	sg_dma_address(&sdev->out_sg) = phys_to_dma(sdev->dev, (unsigned long long)(sdev->des_data));
	sg_dma_len(&sdev->out_sg) = rctx->total;

	err = dma_map_sg(sdev->dev, &sdev->in_sg, 1, DMA_TO_DEVICE);
	if (!err) {
		dev_err(sdev->dev, "dma_map_sg() error\n");
		return -EINVAL;
	}

	err = dma_map_sg(sdev->dev, &sdev->out_sg, 1, DMA_FROM_DEVICE);
	if (!err) {
		dev_err(sdev->dev, "dma_map_sg() error\n");
		return -EINVAL;
	}


	dma_sync_sg_for_device(sdev->dev, &sdev->in_sg, sdev->in_sg_len, DMA_TO_DEVICE);

	cfg.src_addr = sdev->io_phys_base + JH7110_ALG_FIFO_OFFSET;
	cfg.dst_addr = sdev->io_phys_base + JH7110_ALG_FIFO_OFFSET;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst = 4;
	cfg.dst_maxburst = 4;

	ret = dmaengine_slave_config(sdev->sec_xm_m, &cfg);
	if (ret) {
		dev_err(sdev->dev, "can't configure IN dmaengine slave: %d\n",
			ret);
		return ret;
	}

	in_desc = dmaengine_prep_slave_sg(sdev->sec_xm_m, &sdev->in_sg,
				1, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT  |  DMA_CTRL_ACK);

	if (!in_desc) {
		dev_err(sdev->dev, "IN prep_slave_sg() failed\n");
		return -EINVAL;
	}

	in_desc->callback_param = sdev;

	ret = dmaengine_slave_config(sdev->sec_xm_p, &cfg);
	if (ret) {
		dev_err(sdev->dev, "can't configure OUT dmaengine slave: %d\n",
			ret);
		return ret;
	}

	out_desc = dmaengine_prep_slave_sg(sdev->sec_xm_p, &sdev->out_sg,
				1, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!out_desc) {
		dev_err(sdev->dev, "OUT prep_slave_sg() failed\n");
		return -EINVAL;
	}

	out_desc->callback = jh7110_des_dma_callback;
	out_desc->callback_param = sdev;

	dmaengine_submit(in_desc);
	dmaengine_submit(out_desc);

	dma_async_issue_pending(sdev->sec_xm_m);
	dma_async_issue_pending(sdev->sec_xm_p);

	dma_unmap_sg(sdev->dev, &sdev->in_sg, 1, DMA_TO_DEVICE);
	dma_unmap_sg(sdev->dev, &sdev->out_sg, 1, DMA_FROM_DEVICE);

	return 0;
}

static int jh7110_des_cpu_start(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	struct jh7110_sec_request_ctx *rctx = ctx->rctx;
	struct sg_mapping_iter mi;
	void *buffer;
	int total_len, mlen, bs, offset;
	u64 value;
	u8 bvalue;

	total_len = rctx->total;

	sg_miter_start(&mi, rctx->in_sg, rctx->sg_len,
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);

	bs = 8;
	mlen = 0;

	while (total_len) {
		switch (!(total_len >> 3)) {
		case 0:
			bs = 8;
			break;
		case 1:
			bs = 1;
			break;
		}
		if (!mlen) {
			sg_miter_next(&mi);
			mlen = mi.length;
			if (!mlen) {
				pr_err("sg miter failure.\n");
				return -EINVAL;
			}
			buffer = mi.addr;
		}

		bs = min(bs, mlen);

		switch (bs) {
		case 8:
			jh7110_sec_writeq(sdev, JH7110_DES_DAEDINR_HI_OFFSET,
					   *(u64 *)buffer);
			break;
		case 1:
			jh7110_sec_writeb(sdev, JH7110_DES_DAEDINR_HI_OFFSET,
					   *(u8 *)buffer);
			break;
		}
		mlen -= bs;
		total_len -= bs;
		buffer += bs;
	}

	sg_miter_stop(&mi);

	if (jh7110_des_wait_busy(ctx)) {
		dev_err(sdev->dev, "jh7110_hash_wait_busy error\n");
		return -ETIMEDOUT;
	}

	if (jh7110_des_wait_done(sdev)) {
		dev_err(sdev->dev, "jh7110_hash_wait_done error\n");
		return -ETIMEDOUT;
	}

	total_len = rctx->total;
	offset = 0;

	while (total_len) {
		switch (!(total_len >> 3)) {
		case 0:
			value = jh7110_sec_readq(sdev, JH7110_DES_DAEDINR_HI_OFFSET);
			sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), (void *)&value,
				       sizeof(u64), offset, 0);
			offset += sizeof(u64);
			total_len -= 8;
			break;
		case 1:
			bvalue = jh7110_sec_readb(sdev, JH7110_DES_DAEDINR_HI_OFFSET);
			sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg), (void *)&bvalue,
				       sizeof(u8), offset, 0);
			offset += sizeof(u8);
			total_len -= 1;
			break;
		}
	}

	return 0;
}

static int jh7110_des_crypt_start(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret;

	if (sdev->use_dma)
		ret = jh7110_des_dma_start(ctx);
	else
		ret = jh7110_des_cpu_start(ctx);

	return ret;
}

static int jh7110_des_prepare_req(struct crypto_engine *engine,
				void *areq);
static int jh7110_des_one_req(struct crypto_engine *engine, void *areq);


static int jh7110_des_cra_init(struct crypto_skcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->sdev = jh7110_sec_find_dev(ctx);
	if (!ctx->sdev)
		return -ENODEV;

	ctx->sec_init = 0;

	crypto_skcipher_set_reqsize(tfm, sizeof(struct jh7110_sec_request_ctx));

	ctx->enginectx.op.do_one_request = jh7110_des_one_req;
	ctx->enginectx.op.prepare_request = jh7110_des_prepare_req;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void jh7110_des_cra_exit(struct crypto_skcipher *tfm)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);


	ctx->enginectx.op.do_one_request = NULL;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;
}

static int jh7110_des_crypt(struct skcipher_request *req, unsigned long mode)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct jh7110_sec_request_ctx *rctx = skcipher_request_ctx(req);
	struct jh7110_sec_dev *sdev;

	if (!req->cryptlen)
		return 0;

	if (!IS_ALIGNED(req->cryptlen, DES_BLOCK_SIZE))
		return -EINVAL;

	sdev = jh7110_sec_find_dev(ctx);
	if (!sdev)
		return -ENODEV;

	rctx->flags = mode;

	return crypto_transfer_skcipher_request_to_engine(sdev->engine, req);
}

static int jh7110_des_setkey(struct crypto_skcipher *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = verify_skcipher_des_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int jh7110_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = verify_skcipher_des3_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int jh7110_des_ecb_encrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, FLAGS_DES_ENCRYPT | JH7110_DES_MODE_ECB);
}

static int jh7110_des_ecb_decrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, JH7110_DES_MODE_ECB);
}

static int jh7110_des_cbc_encrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, FLAGS_DES_ENCRYPT | JH7110_DES_MODE_CBC);
}

static int jh7110_des_cbc_decrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, JH7110_DES_MODE_CBC);
}

static int jh7110_des_cfb_encrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, FLAGS_DES_ENCRYPT | JH7110_DES_MODE_CFB);
}

static int jh7110_des_cfb_decrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, JH7110_DES_MODE_CFB);
}

static int jh7110_des_ofb_encrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, FLAGS_DES_ENCRYPT | JH7110_DES_MODE_OFB);
}

static int jh7110_des_ofb_decrypt(struct skcipher_request *req)
{
	return jh7110_des_crypt(req, JH7110_DES_MODE_OFB);
}


static int jh7110_des_prepare_req(struct crypto_engine *engine,
				  void *areq)
{
	struct skcipher_request *req = container_of(areq, struct skcipher_request, base);
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct jh7110_sec_dev *sdev = jh7110_sec_find_dev(ctx);
	struct jh7110_sec_request_ctx *rctx;

	if (!sdev)
		return -ENODEV;

	rctx = skcipher_request_ctx(req);

	/* assign new request to device */
	rctx->req.sreq = req;
	rctx->req_type = JH7110_ABLK_REQ;
	rctx->total_in = req->cryptlen;
	rctx->total_out = rctx->total_in;
	rctx->authsize = 0;
	rctx->assoclen = 0;


	rctx->in_sg = req->src;
	rctx->out_sg = req->dst;

	rctx->in_sg_len = sg_nents_for_len(rctx->in_sg, rctx->total);
	if (rctx->in_sg_len < 0)
		return rctx->in_sg_len;

	rctx->out_sg_len = sg_nents_for_len(rctx->out_sg, rctx->total);
	if (rctx->out_sg_len < 0)
		return rctx->out_sg_len;

	rctx = skcipher_request_ctx(req);
	ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));

	rctx->ctx = ctx;
	rctx->sdev = sdev;
	ctx->sdev = sdev;
	ctx->rctx = rctx;

	return jh7110_des_hw_init(ctx);
}

static int jh7110_des_one_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = container_of(areq,
						      struct skcipher_request,
						      base);
	struct jh7110_sec_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct jh7110_sec_dev *sdev = ctx->sdev;
	int ret;

	if (!sdev)
		return -ENODEV;

	ret = jh7110_des_crypt_start(ctx);

	mutex_unlock(&sdev->lock);

	return ret;
}

static struct skcipher_alg algs_des_jh7110[] = {
	{
		.base.cra_name		= "ecb(des)",
		.base.cra_driver_name	= "ecb-des-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
								CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.setkey			= jh7110_des_setkey,
		.encrypt		= jh7110_des_ecb_encrypt,
		.decrypt		= jh7110_des_ecb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "cbc(des)",
		.base.cra_driver_name	= "cbc-des-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
								CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.ivsize				= DES_BLOCK_SIZE,
		.setkey			= jh7110_des_setkey,
		.encrypt		= jh7110_des_cbc_encrypt,
		.decrypt		= jh7110_des_cbc_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "cfb(des)",
		.base.cra_driver_name	= "cfb-des-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
								CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.ivsize				= DES_BLOCK_SIZE,
		.setkey			= jh7110_des_setkey,
		.encrypt		= jh7110_des_cfb_encrypt,
		.decrypt		= jh7110_des_cfb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "ofb(des)",
		.base.cra_driver_name	= "ofb-des-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
								CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.ivsize				= DES_BLOCK_SIZE,
		.setkey			= jh7110_des_setkey,
		.encrypt		= jh7110_des_ofb_encrypt,
		.decrypt		= jh7110_des_ofb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "ecb(des3)",
		.base.cra_driver_name	= "ecb-des3-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
								CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.setkey			= jh7110_des3_setkey,
		.encrypt		= jh7110_des_ecb_encrypt,
		.decrypt		= jh7110_des_ecb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "cbc(des3)",
		.base.cra_driver_name	= "cbc-des3-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize                 = DES_BLOCK_SIZE,
		.setkey			= jh7110_des3_setkey,
		.encrypt		= jh7110_des_cbc_encrypt,
		.decrypt		= jh7110_des_cbc_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "cfb(des3)",
		.base.cra_driver_name	= "cfb-des3-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize                 = DES_BLOCK_SIZE,
		.setkey			= jh7110_des3_setkey,
		.encrypt		= jh7110_des_cfb_encrypt,
		.decrypt		= jh7110_des_cfb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
	{
		.base.cra_name		= "ofb(des3)",
		.base.cra_driver_name	= "ofb-des3-jh7110",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ASYNC,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct jh7110_sec_ctx),
		.base.cra_module	= THIS_MODULE,

		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize                 = DES_BLOCK_SIZE,
		.setkey			= jh7110_des3_setkey,
		.encrypt		= jh7110_des_ofb_encrypt,
		.decrypt		= jh7110_des_ofb_decrypt,
		.init			= jh7110_des_cra_init,
		.exit			= jh7110_des_cra_exit,
	},
};

int jh7110_des_register_algs(void)
{
	int ret;

	ret = crypto_register_skciphers(algs_des_jh7110, ARRAY_SIZE(algs_des_jh7110));
	if (ret) {
		pr_debug("Could not register algs\n");
		return ret;
	}
	return ret;
}

void jh7110_des_unregister_algs(void)
{
	crypto_unregister_skciphers(algs_des_jh7110, ARRAY_SIZE(algs_des_jh7110));
}

