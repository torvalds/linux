// SPDX-License-Identifier: GPL-2.0-only
/*
 * K3 DTHE V2 crypto accelerator driver
 *
 * Copyright (C) Texas Instruments 2025 - https://www.ti.com
 * Author: T Pratham <t-pratham@ti.com>
 */

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/engine.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>

#include "dthev2-common.h"

#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/scatterlist.h>

/* Registers */

// AES Engine
#define DTHE_P_AES_BASE		0x7000
#define DTHE_P_AES_KEY1_0	0x0038
#define DTHE_P_AES_KEY1_1	0x003C
#define DTHE_P_AES_KEY1_2	0x0030
#define DTHE_P_AES_KEY1_3	0x0034
#define DTHE_P_AES_KEY1_4	0x0028
#define DTHE_P_AES_KEY1_5	0x002C
#define DTHE_P_AES_KEY1_6	0x0020
#define DTHE_P_AES_KEY1_7	0x0024
#define DTHE_P_AES_IV_IN_0	0x0040
#define DTHE_P_AES_IV_IN_1	0x0044
#define DTHE_P_AES_IV_IN_2	0x0048
#define DTHE_P_AES_IV_IN_3	0x004C
#define DTHE_P_AES_CTRL		0x0050
#define DTHE_P_AES_C_LENGTH_0	0x0054
#define DTHE_P_AES_C_LENGTH_1	0x0058
#define DTHE_P_AES_AUTH_LENGTH	0x005C
#define DTHE_P_AES_DATA_IN_OUT	0x0060

#define DTHE_P_AES_SYSCONFIG	0x0084
#define DTHE_P_AES_IRQSTATUS	0x008C
#define DTHE_P_AES_IRQENABLE	0x0090

/* Register write values and macros */

enum aes_ctrl_mode_masks {
	AES_CTRL_ECB_MASK = 0x00,
	AES_CTRL_CBC_MASK = BIT(5),
};

#define DTHE_AES_CTRL_MODE_CLEAR_MASK		~GENMASK(28, 5)

#define DTHE_AES_CTRL_DIR_ENC			BIT(2)

#define DTHE_AES_CTRL_KEYSIZE_16B		BIT(3)
#define DTHE_AES_CTRL_KEYSIZE_24B		BIT(4)
#define DTHE_AES_CTRL_KEYSIZE_32B		(BIT(3) | BIT(4))

#define DTHE_AES_CTRL_SAVE_CTX_SET		BIT(29)

#define DTHE_AES_CTRL_OUTPUT_READY		BIT_MASK(0)
#define DTHE_AES_CTRL_INPUT_READY		BIT_MASK(1)
#define DTHE_AES_CTRL_SAVED_CTX_READY		BIT_MASK(30)
#define DTHE_AES_CTRL_CTX_READY			BIT_MASK(31)

#define DTHE_AES_SYSCONFIG_DMA_DATA_IN_OUT_EN	GENMASK(6, 5)
#define DTHE_AES_IRQENABLE_EN_ALL		GENMASK(3, 0)

/* Misc */
#define AES_IV_SIZE				AES_BLOCK_SIZE
#define AES_BLOCK_WORDS				(AES_BLOCK_SIZE / sizeof(u32))
#define AES_IV_WORDS				AES_BLOCK_WORDS

static int dthe_cipher_init_tfm(struct crypto_skcipher *tfm)
{
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct dthe_data *dev_data = dthe_get_dev(ctx);

	ctx->dev_data = dev_data;
	ctx->keylen = 0;

	return 0;
}

static int dthe_aes_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen)
{
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 && keylen != AES_KEYSIZE_256)
		return -EINVAL;

	ctx->keylen = keylen;
	memcpy(ctx->key, key, keylen);

	return 0;
}

static int dthe_aes_ecb_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen)
{
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->aes_mode = DTHE_AES_ECB;

	return dthe_aes_setkey(tfm, key, keylen);
}

static int dthe_aes_cbc_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen)
{
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->aes_mode = DTHE_AES_CBC;

	return dthe_aes_setkey(tfm, key, keylen);
}

static void dthe_aes_set_ctrl_key(struct dthe_tfm_ctx *ctx,
				  struct dthe_aes_req_ctx *rctx,
				  u32 *iv_in)
{
	struct dthe_data *dev_data = dthe_get_dev(ctx);
	void __iomem *aes_base_reg = dev_data->regs + DTHE_P_AES_BASE;
	u32 ctrl_val = 0;

	writel_relaxed(ctx->key[0], aes_base_reg + DTHE_P_AES_KEY1_0);
	writel_relaxed(ctx->key[1], aes_base_reg + DTHE_P_AES_KEY1_1);
	writel_relaxed(ctx->key[2], aes_base_reg + DTHE_P_AES_KEY1_2);
	writel_relaxed(ctx->key[3], aes_base_reg + DTHE_P_AES_KEY1_3);

	if (ctx->keylen > AES_KEYSIZE_128) {
		writel_relaxed(ctx->key[4], aes_base_reg + DTHE_P_AES_KEY1_4);
		writel_relaxed(ctx->key[5], aes_base_reg + DTHE_P_AES_KEY1_5);
	}
	if (ctx->keylen == AES_KEYSIZE_256) {
		writel_relaxed(ctx->key[6], aes_base_reg + DTHE_P_AES_KEY1_6);
		writel_relaxed(ctx->key[7], aes_base_reg + DTHE_P_AES_KEY1_7);
	}

	if (rctx->enc)
		ctrl_val |= DTHE_AES_CTRL_DIR_ENC;

	if (ctx->keylen == AES_KEYSIZE_128)
		ctrl_val |= DTHE_AES_CTRL_KEYSIZE_16B;
	else if (ctx->keylen == AES_KEYSIZE_192)
		ctrl_val |= DTHE_AES_CTRL_KEYSIZE_24B;
	else
		ctrl_val |= DTHE_AES_CTRL_KEYSIZE_32B;

	// Write AES mode
	ctrl_val &= DTHE_AES_CTRL_MODE_CLEAR_MASK;
	switch (ctx->aes_mode) {
	case DTHE_AES_ECB:
		ctrl_val |= AES_CTRL_ECB_MASK;
		break;
	case DTHE_AES_CBC:
		ctrl_val |= AES_CTRL_CBC_MASK;
		break;
	}

	if (iv_in) {
		ctrl_val |= DTHE_AES_CTRL_SAVE_CTX_SET;
		for (int i = 0; i < AES_IV_WORDS; ++i)
			writel_relaxed(iv_in[i],
				       aes_base_reg + DTHE_P_AES_IV_IN_0 + (DTHE_REG_SIZE * i));
	}

	writel_relaxed(ctrl_val, aes_base_reg + DTHE_P_AES_CTRL);
}

static void dthe_aes_dma_in_callback(void *data)
{
	struct skcipher_request *req = (struct skcipher_request *)data;
	struct dthe_aes_req_ctx *rctx = skcipher_request_ctx(req);

	complete(&rctx->aes_compl);
}

static int dthe_aes_run(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = container_of(areq, struct skcipher_request, base);
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct dthe_data *dev_data = dthe_get_dev(ctx);
	struct dthe_aes_req_ctx *rctx = skcipher_request_ctx(req);

	unsigned int len = req->cryptlen;
	struct scatterlist *src = req->src;
	struct scatterlist *dst = req->dst;

	int src_nents = sg_nents_for_len(src, len);
	int dst_nents;

	int src_mapped_nents;
	int dst_mapped_nents;

	bool diff_dst;
	enum dma_data_direction src_dir, dst_dir;

	struct device *tx_dev, *rx_dev;
	struct dma_async_tx_descriptor *desc_in, *desc_out;

	int ret;

	void __iomem *aes_base_reg = dev_data->regs + DTHE_P_AES_BASE;

	u32 aes_irqenable_val = readl_relaxed(aes_base_reg + DTHE_P_AES_IRQENABLE);
	u32 aes_sysconfig_val = readl_relaxed(aes_base_reg + DTHE_P_AES_SYSCONFIG);

	aes_sysconfig_val |= DTHE_AES_SYSCONFIG_DMA_DATA_IN_OUT_EN;
	writel_relaxed(aes_sysconfig_val, aes_base_reg + DTHE_P_AES_SYSCONFIG);

	aes_irqenable_val |= DTHE_AES_IRQENABLE_EN_ALL;
	writel_relaxed(aes_irqenable_val, aes_base_reg + DTHE_P_AES_IRQENABLE);

	if (src == dst) {
		diff_dst = false;
		src_dir = DMA_BIDIRECTIONAL;
		dst_dir = DMA_BIDIRECTIONAL;
	} else {
		diff_dst = true;
		src_dir = DMA_TO_DEVICE;
		dst_dir  = DMA_FROM_DEVICE;
	}

	tx_dev = dmaengine_get_dma_device(dev_data->dma_aes_tx);
	rx_dev = dmaengine_get_dma_device(dev_data->dma_aes_rx);

	src_mapped_nents = dma_map_sg(tx_dev, src, src_nents, src_dir);
	if (src_mapped_nents == 0) {
		ret = -EINVAL;
		goto aes_err;
	}

	if (!diff_dst) {
		dst_nents = src_nents;
		dst_mapped_nents = src_mapped_nents;
	} else {
		dst_nents = sg_nents_for_len(dst, len);
		dst_mapped_nents = dma_map_sg(rx_dev, dst, dst_nents, dst_dir);
		if (dst_mapped_nents == 0) {
			dma_unmap_sg(tx_dev, src, src_nents, src_dir);
			ret = -EINVAL;
			goto aes_err;
		}
	}

	desc_in = dmaengine_prep_slave_sg(dev_data->dma_aes_rx, dst, dst_mapped_nents,
					  DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_in) {
		dev_err(dev_data->dev, "IN prep_slave_sg() failed\n");
		ret = -EINVAL;
		goto aes_prep_err;
	}

	desc_out = dmaengine_prep_slave_sg(dev_data->dma_aes_tx, src, src_mapped_nents,
					   DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_out) {
		dev_err(dev_data->dev, "OUT prep_slave_sg() failed\n");
		ret = -EINVAL;
		goto aes_prep_err;
	}

	desc_in->callback = dthe_aes_dma_in_callback;
	desc_in->callback_param = req;

	init_completion(&rctx->aes_compl);

	if (ctx->aes_mode == DTHE_AES_ECB)
		dthe_aes_set_ctrl_key(ctx, rctx, NULL);
	else
		dthe_aes_set_ctrl_key(ctx, rctx, (u32 *)req->iv);

	writel_relaxed(lower_32_bits(req->cryptlen), aes_base_reg + DTHE_P_AES_C_LENGTH_0);
	writel_relaxed(upper_32_bits(req->cryptlen), aes_base_reg + DTHE_P_AES_C_LENGTH_1);

	dmaengine_submit(desc_in);
	dmaengine_submit(desc_out);

	dma_async_issue_pending(dev_data->dma_aes_rx);
	dma_async_issue_pending(dev_data->dma_aes_tx);

	// Need to do a timeout to ensure finalise gets called if DMA callback fails for any reason
	ret = wait_for_completion_timeout(&rctx->aes_compl, msecs_to_jiffies(DTHE_DMA_TIMEOUT_MS));
	if (!ret) {
		ret = -ETIMEDOUT;
		dmaengine_terminate_sync(dev_data->dma_aes_rx);
		dmaengine_terminate_sync(dev_data->dma_aes_tx);

		for (int i = 0; i < AES_BLOCK_WORDS; ++i)
			readl_relaxed(aes_base_reg + DTHE_P_AES_DATA_IN_OUT + (DTHE_REG_SIZE * i));
	} else {
		ret = 0;
	}

	// For modes other than ECB, read IV_OUT
	if (ctx->aes_mode != DTHE_AES_ECB) {
		u32 *iv_out = (u32 *)req->iv;

		for (int i = 0; i < AES_IV_WORDS; ++i)
			iv_out[i] = readl_relaxed(aes_base_reg +
						  DTHE_P_AES_IV_IN_0 +
						  (DTHE_REG_SIZE * i));
	}

aes_prep_err:
	dma_unmap_sg(tx_dev, src, src_nents, src_dir);
	if (dst_dir != DMA_BIDIRECTIONAL)
		dma_unmap_sg(rx_dev, dst, dst_nents, dst_dir);

aes_err:
	local_bh_disable();
	crypto_finalize_skcipher_request(dev_data->engine, req, ret);
	local_bh_enable();
	return ret;
}

static int dthe_aes_crypt(struct skcipher_request *req)
{
	struct dthe_tfm_ctx *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct dthe_data *dev_data = dthe_get_dev(ctx);
	struct crypto_engine *engine;

	/*
	 * If data is not a multiple of AES_BLOCK_SIZE, need to return -EINVAL
	 * If data length input is zero, no need to do any operation.
	 */
	if (req->cryptlen % AES_BLOCK_SIZE)
		return -EINVAL;

	if (req->cryptlen == 0)
		return 0;

	engine = dev_data->engine;
	return crypto_transfer_skcipher_request_to_engine(engine, req);
}

static int dthe_aes_encrypt(struct skcipher_request *req)
{
	struct dthe_aes_req_ctx *rctx = skcipher_request_ctx(req);

	rctx->enc = 1;
	return dthe_aes_crypt(req);
}

static int dthe_aes_decrypt(struct skcipher_request *req)
{
	struct dthe_aes_req_ctx *rctx = skcipher_request_ctx(req);

	rctx->enc = 0;
	return dthe_aes_crypt(req);
}

static struct skcipher_engine_alg cipher_algs[] = {
	{
		.base.init			= dthe_cipher_init_tfm,
		.base.setkey			= dthe_aes_ecb_setkey,
		.base.encrypt			= dthe_aes_encrypt,
		.base.decrypt			= dthe_aes_decrypt,
		.base.min_keysize		= AES_MIN_KEY_SIZE,
		.base.max_keysize		= AES_MAX_KEY_SIZE,
		.base.base = {
			.cra_name		= "ecb(aes)",
			.cra_driver_name	= "ecb-aes-dthev2",
			.cra_priority		= 299,
			.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_alignmask		= AES_BLOCK_SIZE - 1,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct dthe_tfm_ctx),
			.cra_reqsize		= sizeof(struct dthe_aes_req_ctx),
			.cra_module		= THIS_MODULE,
		},
		.op.do_one_request = dthe_aes_run,
	}, /* ECB AES */
	{
		.base.init			= dthe_cipher_init_tfm,
		.base.setkey			= dthe_aes_cbc_setkey,
		.base.encrypt			= dthe_aes_encrypt,
		.base.decrypt			= dthe_aes_decrypt,
		.base.min_keysize		= AES_MIN_KEY_SIZE,
		.base.max_keysize		= AES_MAX_KEY_SIZE,
		.base.ivsize			= AES_IV_SIZE,
		.base.base = {
			.cra_name		= "cbc(aes)",
			.cra_driver_name	= "cbc-aes-dthev2",
			.cra_priority		= 299,
			.cra_flags		= CRYPTO_ALG_TYPE_SKCIPHER |
						  CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_alignmask		= AES_BLOCK_SIZE - 1,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct dthe_tfm_ctx),
			.cra_reqsize		= sizeof(struct dthe_aes_req_ctx),
			.cra_module		= THIS_MODULE,
		},
		.op.do_one_request = dthe_aes_run,
	} /* CBC AES */
};

int dthe_register_aes_algs(void)
{
	return crypto_engine_register_skciphers(cipher_algs, ARRAY_SIZE(cipher_algs));
}

void dthe_unregister_aes_algs(void)
{
	crypto_engine_unregister_skciphers(cipher_algs, ARRAY_SIZE(cipher_algs));
}
