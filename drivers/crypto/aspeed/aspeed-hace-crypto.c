// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto driver for the Aspeed SoC
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "aspeed-hace.h"

#define ASPEED_SEC_PROTECTION		0x0
#define SEC_UNLOCK_PASSWORD			0x349fe38a
#define ASPEED_VAULT_KEY_CTRL		0x80C
#define SEC_VK_CTRL_VK_SELECTION	BIT(0)

// #define ASPEED_CIPHER_DEBUG

#ifdef ASPEED_CIPHER_DEBUG
#define CIPHER_DBG(fmt, args...) pr_notice("%s() " fmt, __func__, ## args)
#else
#define CIPHER_DBG(fmt, args...)
#endif

int aspeed_hace_crypto_handle_queue(struct aspeed_hace_dev *hace_dev,
				    struct crypto_async_request *new_areq)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct crypto_async_request *areq, *backlog;
	struct aspeed_cipher_ctx *ctx;
	unsigned long flags;
	int err, ret = 0;

	CIPHER_DBG("\n");
	spin_lock_irqsave(&crypto_engine->lock, flags);
	if (new_areq)
		ret = crypto_enqueue_request(&crypto_engine->queue, new_areq);
	if (crypto_engine->flags & CRYPTO_FLAGS_BUSY) {
		spin_unlock_irqrestore(&crypto_engine->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&crypto_engine->queue);
	areq = crypto_dequeue_request(&crypto_engine->queue);
	if (areq)
		crypto_engine->flags |= CRYPTO_FLAGS_BUSY;
	spin_unlock_irqrestore(&crypto_engine->lock, flags);

	if (!areq)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	ctx = crypto_tfm_ctx(areq->tfm);
	crypto_engine->is_async = (areq != new_areq);
	crypto_engine->areq = areq;

	err = ctx->start(hace_dev);

	// crypto_engine->sk_req = skcipher_request_cast(areq);
	// err = aspeed_hace_skcipher_trigger(hace_dev);

	return (crypto_engine->is_async) ? ret : err;
}

static inline int aspeed_crypto_wait_for_data_ready(struct aspeed_hace_dev *hace_dev,
		aspeed_hace_fn_t resume)
{
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	CIPHER_DBG("\n");

	return -EINPROGRESS;
#else
	u32 sts;

	CIPHER_DBG("\n");
	do {
		sts = aspeed_hace_read(hace_dev, ASPEED_HACE_STS);
	} while (sts & HACE_CRYPTO_BUSY);

	return resume(hace_dev);
#endif
}

static int aspeed_sk_complete(struct aspeed_hace_dev *hace_dev, int err)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);

	CIPHER_DBG("\n");
	if (rctx->enc_cmd & HACE_CMD_IV_REQUIRE) {
		if (rctx->enc_cmd & HACE_CMD_DES_SELECT)
			memcpy(req->iv, crypto_engine->cipher_ctx + 8, 8);
		else
			memcpy(req->iv, crypto_engine->cipher_ctx, 16);
	}
	crypto_engine->flags &= ~CRYPTO_FLAGS_BUSY;
	if (crypto_engine->is_async)
		req->base.complete(&req->base, err);

	tasklet_schedule(&crypto_engine->queue_task);

	return err;
}

static int aspeed_sk_sg_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct device *dev = hace_dev->dev;

	CIPHER_DBG("\n");
	if (req->src == req->dst) {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
	}

	return aspeed_sk_complete(hace_dev, 0);
}

static int aspeed_sk_cpu_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct device *dev = hace_dev->dev;
	struct scatterlist *out_sg = req->dst;
	int nbytes = 0;
	int err = 0;

	CIPHER_DBG("\n");
	nbytes = sg_copy_from_buffer(out_sg, rctx->dst_nents, crypto_engine->cipher_addr, req->cryptlen);
	if (!nbytes) {
		dev_err(dev, "nbytes %d req->cryptlen %d\n", nbytes, req->cryptlen);
		err = -EINVAL;
	}
	return aspeed_sk_complete(hace_dev, err);
}

# if 0
static int aspeed_sk_dma_start(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);

	CIPHER_DBG("\n");
	CIPHER_DBG("req->cryptlen %d , nb_in_sg %d, nb_out_sg %d\n", req->cryptlen, ctx->src_nents, ctx->dst_nents);
	if (req->dst == req->src) {
		if (!dma_map_sg(hace_dev->dev, req->src, 1, DMA_BIDIRECTIONAL)) {
			dev_err(hace_dev->dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	} else {
		if (!dma_map_sg(hace_dev->dev, req->src, 1, DMA_TO_DEVICE)) {
			dev_err(hace_dev->dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
		if (!dma_map_sg(hace_dev->dev, req->dst, 1, DMA_FROM_DEVICE)) {
			dma_unmap_sg(hace_dev->dev, req->dst, 1, DMA_FROM_DEVICE);
			dev_err(hace_dev->dev, "[%s:%d] dma_map_sg(dst) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	}
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	crypto_engine->resume = aspeed_sk_sg_transfer;
#endif
	aspeed_hace_write(hace_dev, sg_dma_address(req->src), ASPEED_HACE_SRC);
	aspeed_hace_write(hace_dev, sg_dma_address(req->dst), ASPEED_HACE_DEST);

	aspeed_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	aspeed_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);
	return aspeed_crypto_wait_for_data_ready(hace_dev, aspeed_sk_sg_transfer);
}
#endif

static int aspeed_sk_cpu_start(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct device *dev = hace_dev->dev;
	struct scatterlist *in_sg = req->src;
	int nbytes = 0;

	CIPHER_DBG("\n");
	nbytes = sg_copy_to_buffer(in_sg, rctx->src_nents, crypto_engine->cipher_addr, req->cryptlen);
	CIPHER_DBG("copy nbytes %d, req->cryptlen %d , nb_in_sg %d, nb_out_sg %d\n", nbytes, req->cryptlen, rctx->src_nents, rctx->dst_nents);
	if (!nbytes) {
		dev_err(dev, "nbytes error\n");
		return -EINVAL;
	}
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	crypto_engine->resume = aspeed_sk_cpu_transfer;
#endif
	aspeed_hace_write(hace_dev, crypto_engine->cipher_dma_addr, ASPEED_HACE_SRC);
	aspeed_hace_write(hace_dev, crypto_engine->cipher_dma_addr, ASPEED_HACE_DEST);
	aspeed_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	aspeed_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);

	return aspeed_crypto_wait_for_data_ready(hace_dev, aspeed_sk_cpu_transfer);
}

static int aspeed_sk_g6_start(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct device *dev = hace_dev->dev;
	struct aspeed_sg_list *src_list, *dst_list;
	dma_addr_t src_dma_addr, dst_dma_addr;
	struct scatterlist *s;
	int total, i;
	int src_sg_len;
	int dst_sg_len;
	unsigned int val;

	CIPHER_DBG("\n");

	rctx->enc_cmd |= HACE_CMD_DES_SG_CTRL | HACE_CMD_SRC_SG_CTRL |
			 HACE_CMD_AES_KEY_HW_EXP | HACE_CMD_MBUS_REQ_SYNC_EN;

	if (ctx->dummy_key == 1 || ctx->dummy_key == 2) {
		rctx->enc_cmd |= HACE_CMD_AES_KEY_FROM_OTP;
		writel(SEC_UNLOCK_PASSWORD, hace_dev->sec_regs + ASPEED_SEC_PROTECTION);
		CIPHER_DBG("unlock SB, SEC000=0x%x\n", readl(hace_dev->sec_regs + ASPEED_SEC_PROTECTION));
		val = readl(hace_dev->sec_regs + ASPEED_VAULT_KEY_CTRL);
		if (ctx->dummy_key == 1) {
			val &= ~SEC_VK_CTRL_VK_SELECTION;
			writel(val, hace_dev->sec_regs + ASPEED_VAULT_KEY_CTRL);
			CIPHER_DBG("Vault key 1:\n");
		} else {
			val |= SEC_VK_CTRL_VK_SELECTION;
			writel(val, hace_dev->sec_regs + ASPEED_VAULT_KEY_CTRL);
			CIPHER_DBG("Vault key 2:\n");
		}
		writel(0x0, hace_dev->sec_regs + ASPEED_SEC_PROTECTION);
		CIPHER_DBG("lock SB, SEC000=0x%x\n", readl(hace_dev->sec_regs + ASPEED_SEC_PROTECTION));
	} else {
		rctx->enc_cmd &= ~HACE_CMD_AES_KEY_FROM_OTP;
	}

	if (req->dst == req->src) {
		src_sg_len = dma_map_sg(dev, req->src, rctx->src_nents, DMA_BIDIRECTIONAL);
		dst_sg_len = src_sg_len;
		if (!src_sg_len) {
			dev_err(dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	} else {
		src_sg_len = dma_map_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		if (!src_sg_len) {
			dev_err(dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
		dst_sg_len = dma_map_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
		if (!dst_sg_len) {
			dev_err(dev, "[%s:%d] dma_map_sg(dst) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	}

	src_list = (struct aspeed_sg_list *)crypto_engine->cipher_addr;
	src_dma_addr = crypto_engine->cipher_dma_addr;
	total = req->cryptlen;
	for_each_sg(req->src, s, src_sg_len, i) {
		src_list[i].phy_addr = sg_dma_address(s);
		if (sg_dma_len(s) >= total) {
			src_list[i].len = total;
			src_list[i].len |= BIT(31);
			total = 0;
			break;
		}
		src_list[i].len = sg_dma_len(s);
		total -= src_list[i].len;
	}
	if (total != 0)
		return -EINVAL;

	if (req->dst == req->src) {
		dst_list = src_list;
		dst_dma_addr = src_dma_addr;
		// dummy read for a1
		READ_ONCE(src_list[src_sg_len]);
	} else {
		dst_list = (struct aspeed_sg_list *)crypto_engine->dst_sg_addr;
		dst_dma_addr = crypto_engine->dst_sg_dma_addr;
		total = req->cryptlen;
		for_each_sg(req->dst, s, dst_sg_len, i) {
			dst_list[i].phy_addr = sg_dma_address(s);
			if (sg_dma_len(s) >= total) {
				dst_list[i].len = total;
				dst_list[i].len |= BIT(31);
				total = 0;
				break;
			}
			dst_list[i].len = sg_dma_len(s);
			total -= dst_list[i].len;
		}
		dst_list[dst_sg_len].phy_addr = 0;
		dst_list[dst_sg_len].len = 0;
		// dummy read for a1
		READ_ONCE(src_list[src_sg_len]);
		READ_ONCE(dst_list[dst_sg_len]);
	}
	if (total != 0)
		return -EINVAL;

	// i = 0;
	// printk("src_list\n");
	// while (1) {
	//	printk("addr: %x\n", src_list[i].phy_addr);
	//	if (src_list[i].len & BIT(31)) {
	//		printk("len: %lu\n", src_list[i].len & ~BIT(31));
	//		break;
	//	} else {
	//		printk("len: %lu\n", src_list[i].len);
	//		i++;
	//	}
	// }
	// if (req->dst == req->src) {
	//	printk("dst_list\n");
	//	while (1) {
	//		printk("addr: %x\n", src_list[i].phy_addr);
	//		if (src_list[i].len & BIT(31)) {
	//			printk("len: %lu\n", src_list[i].len & ~BIT(31));
	//			break;
	//		} else {
	//			printk("len: %lu\n", src_list[i].len);
	//			i++;
	//		}
	//	}
	// } else {
	//	i = 0;
	//	printk("dst_list\n");
	//	while (1) {
	//		printk("addr: %x\n", dst_list[i].phy_addr);
	//		if (dst_list[i].len & BIT(31)) {
	//			printk("len: %lu\n", dst_list[i].len & ~BIT(31));
	//			break;
	//		} else {
	//			printk("len: %lu\n", dst_list[i].len);
	//			i++;
	//		}
	//	}
	// }
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	crypto_engine->resume = aspeed_sk_sg_transfer;
#endif
	aspeed_hace_write(hace_dev, src_dma_addr, ASPEED_HACE_SRC);
	aspeed_hace_write(hace_dev, dst_dma_addr, ASPEED_HACE_DEST);
	aspeed_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	aspeed_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);

	return aspeed_crypto_wait_for_data_ready(hace_dev, aspeed_sk_sg_transfer);
}

int aspeed_hace_skcipher_trigger(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct skcipher_request *req = skcipher_request_cast(crypto_engine->areq);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);

	CIPHER_DBG("\n");
	//for enable interrupt
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	rctx->enc_cmd |= HACE_CMD_ISR_EN;
#endif
	rctx->dst_nents = sg_nents(req->dst);
	rctx->src_nents = sg_nents(req->src);
	// if ((ctx->dst_nents == 1) && (ctx->src_nents == 1))
	//	return aspeed_sk_dma_start(hace_dev);

	aspeed_hace_write(hace_dev, crypto_engine->cipher_ctx_dma, ASPEED_HACE_CONTEXT);
	if (rctx->enc_cmd & HACE_CMD_IV_REQUIRE) {
		if (rctx->enc_cmd & HACE_CMD_DES_SELECT)
			memcpy(crypto_engine->cipher_ctx + 8, req->iv, 8);
		else
			memcpy(crypto_engine->cipher_ctx, req->iv, 16);
	}

	if (hace_dev->version == 6) {
		memcpy(crypto_engine->cipher_ctx + 16, ctx->key, ctx->key_len);
		return aspeed_sk_g6_start(hace_dev);
	}

	memcpy(crypto_engine->cipher_ctx + 16, ctx->key, AES_MAX_KEYLENGTH);
	return aspeed_sk_cpu_start(hace_dev);
}

#if 0
static int aspeed_rc4_crypt(struct skcipher_request *req, u32 cmd)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;

	CIPHER_DBG("\n");

	cmd |= HACE_CMD_RI_WO_DATA_ENABLE |
	       HACE_CMD_CONTEXT_LOAD_ENABLE | HACE_CMD_CONTEXT_SAVE_ENABLE;

	rctx->enc_cmd = cmd;

	return aspeed_hace_crypto_handle_queue(hace_dev, &req->base);
}

static int aspeed_rc4_setkey(struct crypto_skcipher *cipher, const u8 *in_key,
			     unsigned int key_len)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	int i, j = 0, k = 0;
	u8 *rc4_key = ctx->cipher_key;

	CIPHER_DBG("keylen : %d : %s\n", key_len, in_key);

	*(u32 *)(ctx->cipher_key + 0) = 0x0;
	*(u32 *)(ctx->cipher_key + 4) = 0x0;
	*(u32 *)(ctx->cipher_key + 8) = 0x0001;

	for (i = 0; i < 256; i++)
		rc4_key[16 + i] = i;

	for (i = 0; i < 256; i++) {
		u32 a = rc4_key[16 + i];

		j = (j + in_key[k] + a) & 0xff;
		rc4_key[16 + i] = rc4_key[16 + j];
		rc4_key[16 + j] = a;
		if (++k >= key_len)
			k = 0;
	}

	ctx->key_len = 256;

	return 0;
}

static int aspeed_rc4_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_rc4_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_RC4);
}

static int aspeed_rc4_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_rc4_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_RC4);
}
#endif

static int aspeed_des_crypt(struct skcipher_request *req, u32 cmd)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	u32 crypto_alg = cmd & (7 << 4);

	CIPHER_DBG("\n");

	if (crypto_alg == HACE_CMD_CBC || crypto_alg == HACE_CMD_ECB) {
		if (!IS_ALIGNED(req->cryptlen, DES_BLOCK_SIZE))
			return -EINVAL;
	}

	rctx->enc_cmd = cmd | HACE_CMD_DES_SELECT | HACE_CMD_RI_WO_DATA_ENABLE |
			HACE_CMD_DES | HACE_CMD_CONTEXT_LOAD_ENABLE |
			HACE_CMD_CONTEXT_SAVE_ENABLE;

	return aspeed_hace_crypto_handle_queue(hace_dev, &req->base);
}

static int aspeed_des_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct device *dev = ctx->hace_dev->dev;
	int err;

	CIPHER_DBG("bits : %d :\n", keylen);

	if (keylen != DES_KEY_SIZE && keylen != 2 * DES_KEY_SIZE && keylen != 3 * DES_KEY_SIZE) {
		dev_err(dev, "keylen fail %d bits\n", keylen);
		return -EINVAL;
	}

	if (keylen == DES_KEY_SIZE) {
		err = crypto_des_verify_key(tfm, key);
		if (err)
			return err;
	}

	memcpy(ctx->key, key, keylen);
	ctx->key_len = keylen;

	return 0;
}

static int aspeed_tdes_ctr_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ctr_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ofb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_OFB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ofb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_OFB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cfb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CFB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cfb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CFB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cbc_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cbc_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ecb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ecb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB | HACE_CMD_TRIPLE_DES);
}

static int aspeed_des_ctr_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ctr_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ofb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_OFB | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ofb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_OFB | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cfb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CFB | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cfb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CFB | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cbc_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cbc_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ecb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB | HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ecb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB | HACE_CMD_SINGLE_DES);
}

static int aspeed_aes_crypt(struct skcipher_request *req, u32 cmd)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	u32 crypto_alg = cmd & (7 << 4);

	if (crypto_alg == HACE_CMD_CBC || crypto_alg == HACE_CMD_ECB) {
		if (!IS_ALIGNED(req->cryptlen, AES_BLOCK_SIZE))
			return -EINVAL;
	}

	cmd |= HACE_CMD_AES_SELECT | HACE_CMD_RI_WO_DATA_ENABLE |
	       HACE_CMD_CONTEXT_LOAD_ENABLE | HACE_CMD_CONTEXT_SAVE_ENABLE;

	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		cmd |= HACE_CMD_AES128;
		break;
	case AES_KEYSIZE_192:
		cmd |= HACE_CMD_AES192;
		break;
	case AES_KEYSIZE_256:
		cmd |= HACE_CMD_AES256;
		break;
	default:
		return -EINVAL;
	}

	rctx->enc_cmd = cmd;

	return aspeed_hace_crypto_handle_queue(hace_dev, &req->base);
}

static int aspeed_aes_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct crypto_aes_ctx gen_aes_key;

	CIPHER_DBG("bits : %d\n", (keylen * 8));

	ctx->dummy_key = find_dummy_key(key, keylen);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	if (ctx->hace_dev->version == 5) {
		aes_expandkey(&gen_aes_key, key, keylen);
		memcpy(ctx->key, gen_aes_key.key_enc, AES_MAX_KEYLENGTH);
	} else {
		memcpy(ctx->key, key, keylen);
	}
	ctx->key_len = keylen;

	return 0;
}

static int aspeed_aes_ctr_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR);
}

static int aspeed_aes_ctr_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR);
}

static int aspeed_aes_ofb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_OFB);
}

static int aspeed_aes_ofb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_OFB);
}

static int aspeed_aes_cfb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CFB);
}

static int aspeed_aes_cfb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CFB);
}

static int aspeed_aes_ecb_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB);
}

static int aspeed_aes_ecb_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB);
}

static int aspeed_aes_cbc_decrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC);
}

static int aspeed_aes_cbc_encrypt(struct skcipher_request *req)
{
	CIPHER_DBG("\n");
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC);
}

static int aspeed_crypto_cra_init(struct crypto_skcipher *tfm)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct aspeed_hace_alg *crypto_alg;

	CIPHER_DBG("\n");
	crypto_alg = container_of(alg, struct aspeed_hace_alg, alg.skcipher);

	ctx->hace_dev = crypto_alg->hace_dev;
	ctx->start = aspeed_hace_skcipher_trigger;
	crypto_skcipher_set_reqsize(tfm, sizeof(struct aspeed_cipher_reqctx));
	return 0;
}

static void aspeed_crypto_cra_exit(struct crypto_skcipher *tfm)
{
	CIPHER_DBG("\n");
}

static int aspeed_aead_complete(struct aspeed_hace_dev *hace_dev, int err)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aead_request *req = aead_request_cast(crypto_engine->areq);

	CIPHER_DBG("\n");
	memcpy(req->iv, crypto_engine->cipher_ctx, 16);
	crypto_engine->flags &= ~CRYPTO_FLAGS_BUSY;
	if (crypto_engine->is_async)
		req->base.complete(&req->base, err);

	aspeed_hace_crypto_handle_queue(hace_dev, NULL);

	return err;
}

static int aspeed_aead_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aead_request *req = aead_request_cast(crypto_engine->areq);
	struct crypto_aead *cipher = crypto_aead_reqtfm(req);
	struct aspeed_cipher_reqctx *rctx = aead_request_ctx(req);
	struct device *dev = hace_dev->dev;
	int err = 0;
	int enc = (rctx->enc_cmd & HACE_CMD_ENCRYPT) ? 1 : 0;
	u32 offset, authsize, tag[4];

	CIPHER_DBG("\n");
	if (req->src == req->dst) {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
	}
	authsize = crypto_aead_authsize(cipher);
	if (!enc) {
		offset = req->assoclen + req->cryptlen - authsize;
		scatterwalk_map_and_copy(tag, req->src, offset, authsize, 0);
		err = crypto_memneq(crypto_engine->dst_sg_addr + ASPEED_CRYPTO_GCM_TAG_OFFSET,
				    tag, authsize) ? -EBADMSG : 0;
	} else {
		offset = req->assoclen + req->cryptlen;
		scatterwalk_map_and_copy(crypto_engine->dst_sg_addr + ASPEED_CRYPTO_GCM_TAG_OFFSET, req->dst, offset, authsize, 1);
	}

	return aspeed_aead_complete(hace_dev, err);
}

static int  aspeed_aead_start(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aead_request *req = aead_request_cast(crypto_engine->areq);
	struct crypto_aead *cipher = crypto_aead_reqtfm(req);
	struct aspeed_cipher_reqctx *rctx = aead_request_ctx(req);
	struct device *dev = hace_dev->dev;
	struct aspeed_sg_list *src_list, *dst_list;
	int src_sg_len;
	int dst_sg_len;
	dma_addr_t src_dma_addr, dst_dma_addr;
	dma_addr_t tag_dma_addr;
	struct scatterlist *s;
	u32 total, offset, authsize;
	int i, j;
	int enc = (rctx->enc_cmd & HACE_CMD_ENCRYPT) ? 1 : 0;

	CIPHER_DBG("\n");

	authsize = crypto_aead_authsize(cipher);
	rctx->enc_cmd |= HACE_CMD_DES_SG_CTRL | HACE_CMD_SRC_SG_CTRL |
			 HACE_CMD_AES_KEY_HW_EXP | HACE_CMD_MBUS_REQ_SYNC_EN |
			 HACE_CMD_GCM_TAG_ADDR_SEL;

	if (req->dst == req->src) {
		src_sg_len = dma_map_sg(dev, req->src, rctx->src_nents, DMA_BIDIRECTIONAL);
		dst_sg_len = src_sg_len;
		if (!src_sg_len) {
			dev_err(dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	} else {
		src_sg_len = dma_map_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		if (!src_sg_len) {
			dev_err(dev, "[%s:%d] dma_map_sg(src) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
		dst_sg_len = dma_map_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
		if (!dst_sg_len) {
			dma_unmap_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
			dev_err(dev, "[%s:%d] dma_map_sg(dst) error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
	}

	src_list = (struct aspeed_sg_list *)crypto_engine->cipher_addr;
	dst_list = (struct aspeed_sg_list *)crypto_engine->dst_sg_addr;
	src_dma_addr = crypto_engine->cipher_dma_addr;
	dst_dma_addr = crypto_engine->dst_sg_dma_addr;
	tag_dma_addr = crypto_engine->dst_sg_dma_addr + ASPEED_CRYPTO_GCM_TAG_OFFSET;
	if (enc)
		total = req->assoclen + req->cryptlen;
	else
		total = req->assoclen + req->cryptlen - authsize;
	for_each_sg(req->src, s, src_sg_len, i) {
		src_list[i].phy_addr = sg_dma_address(s);
		if (sg_dma_len(s) >= total) {
			src_list[i].len = total;
			src_list[i].len |= BIT(31);
			total = 0;
			break;
		}
		src_list[i].len = sg_dma_len(s);
		total -= src_list[i].len;
	}
	src_list[src_sg_len].phy_addr = 0;
	src_list[src_sg_len].len = 0;
	if (total != 0)
		return -EINVAL;

	offset = req->assoclen;
	if (enc)
		total = req->cryptlen;
	else
		total = req->cryptlen - authsize;
	j = 0;
	for_each_sg(req->dst, s, dst_sg_len, i) {
		if (offset != 0) {
			if (sg_dma_len(s) == offset) {
				offset = 0;
				continue;
			} else if (sg_dma_len(s) > offset) {
				dst_list[j].phy_addr = sg_dma_address(s) + offset;
				if (sg_dma_len(s) - offset >= total) {
					dst_list[j].len = total;
					dst_list[j].len |= BIT(31);
					total = 0;
					offset = 0;
					break;
				}
				dst_list[j].len = sg_dma_len(s) - offset;
				total -= dst_list[j].len;
				offset = 0;
				j++;
				continue;
			} else {
				offset -= sg_dma_len(s);
			}
		} else {
			if (sg_dma_len(s) >= total) {
				dst_list[j].phy_addr = sg_dma_address(s);
				dst_list[j].len = total;
				dst_list[j].len |= BIT(31);
				j++;
				total = 0;
				break;
			}
			dst_list[j].phy_addr = sg_dma_address(s);
			dst_list[j].len = sg_dma_len(s);
			total -= dst_list[j].len;
			j++;
		}
	}
	if (total != 0 || offset != 0)
		return -EINVAL;

	// i = 0;
	// printk("src_list\n");
	// while (1) {
	//	printk("addr: %x\n", src_list[i].phy_addr);
	//	if (src_list[i].len & BIT(31)) {
	//		printk("len: %lu\n", src_list[i].len & ~BIT(31));
	//		break;
	//	} else {
	//		printk("len: %lu\n", src_list[i].len);
	//		i++;
	//	}
	// }

	// i = 0;
	// printk("dst_list\n");
	// while (1) {
	//	printk("addr: %x\n", dst_list[i].phy_addr);
	//	if (dst_list[i].len & BIT(31)) {
	//		printk("len: %lu\n", dst_list[i].len & ~BIT(31));
	//		break;
	//	} else {
	//		printk("len: %lu\n", dst_list[i].len);
	//		i++;
	//	}
	// }
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	crypto_engine->resume = aspeed_aead_transfer;
#endif
	aspeed_hace_write(hace_dev, src_dma_addr, ASPEED_HACE_SRC);
	aspeed_hace_write(hace_dev, dst_dma_addr, ASPEED_HACE_DEST);
	if (enc)
		aspeed_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	else
		aspeed_hace_write(hace_dev, req->cryptlen - authsize, ASPEED_HACE_DATA_LEN);
	aspeed_hace_write(hace_dev, tag_dma_addr, ASPEED_HACE_GCM_TAG_BASE_ADDR);
	aspeed_hace_write(hace_dev, req->assoclen, ASPEED_HACE_GCM_ADD_LEN);
	aspeed_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);

	return aspeed_crypto_wait_for_data_ready(hace_dev, aspeed_aead_transfer);
}

int aspeed_hace_aead_trigger(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aead_request *req = aead_request_cast(crypto_engine->areq);
	struct crypto_aead *cipher = crypto_aead_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_aead_ctx(cipher);
	struct aspeed_cipher_reqctx *rctx = aead_request_ctx(req);

	CIPHER_DBG("\n");
	//for enable interrupt
#ifdef CONFIG_CRYPTO_DEV_ASPEED_SK_INT
	rctx->enc_cmd |= HACE_CMD_ISR_EN;
#endif
	rctx->dst_nents = sg_nents(req->dst);
	rctx->src_nents = sg_nents(req->src);

	// printk("length: %d\n", req->src->length);
	// printk("assoclen: %d\n", req->assoclen);
	// printk("cryptlen: %d\n", req->cryptlen);

	memcpy(crypto_engine->cipher_ctx, req->iv, 12);
	memset(crypto_engine->cipher_ctx + 12, 0, 3);
	memset(crypto_engine->cipher_ctx + 15, 1, 1);

	memcpy(crypto_engine->cipher_ctx + 16, ctx->key, ctx->key_len);
	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		memcpy(crypto_engine->cipher_ctx + 32, ctx->sub_key, 16);
		break;
	case AES_KEYSIZE_192:
		memcpy(crypto_engine->cipher_ctx + 48, ctx->sub_key, 16);
		break;
	case AES_KEYSIZE_256:
		memcpy(crypto_engine->cipher_ctx + 48, ctx->sub_key, 16);
		break;
	}

	aspeed_hace_write(hace_dev, crypto_engine->cipher_ctx_dma, ASPEED_HACE_CONTEXT);
	return aspeed_aead_start(hace_dev);
}

static int aspeed_gcm_crypt(struct aead_request *req, u32 cmd)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_aead_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	struct aspeed_cipher_reqctx *rctx = aead_request_ctx(req);

	CIPHER_DBG("\n");
	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		cmd |= HACE_CMD_AES128;
		break;
	case AES_KEYSIZE_192:
		cmd |= HACE_CMD_AES192;
		break;
	case AES_KEYSIZE_256:
		cmd |= HACE_CMD_AES256;
		break;
	}

	rctx->enc_cmd = cmd;

	return aspeed_hace_crypto_handle_queue(hace_dev, &req->base);
}

static void aspeed_gcm_subkey_done(struct crypto_async_request *req, int err)
{
	struct aspeed_gcm_subkey_result *res = req->data;

	CIPHER_DBG("\n");
	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static int aspeed_gcm_subkey(struct crypto_skcipher *tfm, u8 *data,
			     unsigned int len, const u8 *key, unsigned int keylen)
{
	struct aspeed_gcm_subkey_result result;
	struct skcipher_request *req;
	struct scatterlist sg[1];
	int ret;

	CIPHER_DBG("\n");

	init_completion(&result.completion);
	req = skcipher_request_alloc(tfm, GFP_KERNEL);

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      aspeed_gcm_subkey_done, &result);
	crypto_skcipher_clear_flags(tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_setkey(tfm, key, keylen);
	sg_init_one(sg, data, len);
	skcipher_request_set_crypt(req, sg, sg, len, NULL);
	ret = crypto_skcipher_encrypt(req);

	switch (ret) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		wait_for_completion(&result.completion);
		ret = result.err;
		break;
	default:
		pr_err("ecb(aes) enc error");
	}

	skcipher_request_free(req);
	return ret;
}

static int aspeed_gcm_setkey(struct crypto_aead *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct aspeed_cipher_ctx *ctx = crypto_aead_ctx(tfm);
	u8 data[16];

	CIPHER_DBG("bits : %d\n", (keylen * 8));

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;
	memcpy(ctx->key, key, keylen);
	aspeed_gcm_subkey(ctx->aes, data, 16, key, keylen);
	memcpy(ctx->sub_key, data, 16);
	ctx->key_len = keylen;

	return 0;
}

static int aspeed_gcm_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	/* Same as gcm_authsize() from crypto/gcm.c */
	CIPHER_DBG("\n");
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int aspeed_gcm_encrypt(struct aead_request *req)
{
	return aspeed_gcm_crypt(req, HACE_CMD_GCM | HACE_CMD_ENCRYPT);
}

static int aspeed_gcm_decrypt(struct aead_request *req)
{
	return aspeed_gcm_crypt(req, HACE_CMD_GCM | HACE_CMD_DECRYPT);
}

static int aspeed_gcm_init(struct crypto_aead *tfm)
{
	struct aspeed_cipher_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct aspeed_hace_alg *crypto_alg;

	CIPHER_DBG("\n");
	crypto_alg = container_of(alg, struct aspeed_hace_alg, alg.aead);

	ctx->hace_dev = crypto_alg->hace_dev;
	ctx->start = aspeed_hace_aead_trigger;
	ctx->aes = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	if (IS_ERR(ctx->aes)) {
		dev_err(ctx->hace_dev->dev, "aspeed-gcm: base driver 'ecb(aes)' could not be loaded.\n");
		return PTR_ERR(ctx->aes);
	}
	return 0;
}

static void aspeed_gcm_exit(struct crypto_aead *tfm)
{
	struct aspeed_cipher_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_skcipher(ctx->aes);
}

struct aspeed_hace_alg aspeed_crypto_algs[] = {
	{
		.alg.skcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_ecb_encrypt,
			.decrypt	= aspeed_aes_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(aes)",
				.cra_driver_name	= "aspeed-ecb-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= AES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_cbc_encrypt,
			.decrypt	= aspeed_aes_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(aes)",
				.cra_driver_name	= "aspeed-cbc-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= AES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_cfb_encrypt,
			.decrypt	= aspeed_aes_cfb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cfb(aes)",
				.cra_driver_name	= "aspeed-cfb-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_ofb_encrypt,
			.decrypt	= aspeed_aes_ofb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ofb(aes)",
				.cra_driver_name	= "aspeed-ofb-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_ecb_encrypt,
			.decrypt	= aspeed_des_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(des)",
				.cra_driver_name	= "aspeed-ecb-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_cbc_encrypt,
			.decrypt	= aspeed_des_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(des)",
				.cra_driver_name	= "aspeed-cbc-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_cfb_encrypt,
			.decrypt	= aspeed_des_cfb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cfb(des)",
				.cra_driver_name	= "aspeed-cfb-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_ofb_encrypt,
			.decrypt	= aspeed_des_ofb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ofb(des)",
				.cra_driver_name	= "aspeed-ofb-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_ecb_encrypt,
			.decrypt	= aspeed_tdes_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(des3_ede)",
				.cra_driver_name	= "aspeed-ecb-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_cbc_encrypt,
			.decrypt	= aspeed_tdes_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(des3_ede)",
				.cra_driver_name	= "aspeed-cbc-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_cfb_encrypt,
			.decrypt	= aspeed_tdes_cfb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cfb(des3_ede)",
				.cra_driver_name	= "aspeed-cfb-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_ofb_encrypt,
			.decrypt	= aspeed_tdes_ofb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ofb(des3_ede)",
				.cra_driver_name	= "aspeed-ofb-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
#if 0
	{
		.alg.skcipher = {
			.min_keysize	= 1,
			.max_keysize	= 256,
			.setkey		= aspeed_rc4_setkey,
			.encrypt	= aspeed_rc4_encrypt,
			.decrypt	= aspeed_rc4_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(arc4)",
				.cra_driver_name	= "aspeed-arc4",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	}
#endif
};

struct aspeed_hace_alg aspeed_crypto_algs_g6[] = {
	{
		.alg.skcipher = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_ctr_encrypt,
			.decrypt	= aspeed_aes_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(aes)",
				.cra_driver_name	= "aspeed-ctr-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_ctr_encrypt,
			.decrypt	= aspeed_des_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(des)",
				.cra_driver_name	= "aspeed-ctr-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},
	{
		.alg.skcipher = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_ctr_encrypt,
			.decrypt	= aspeed_tdes_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(des3_ede)",
				.cra_driver_name	= "aspeed-ctr-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		}
	},

};

struct aspeed_hace_alg aspeed_aead_algs_g6[] = {
	{
		.alg.aead = {
			.setkey		= aspeed_gcm_setkey,
			.setauthsize	= aspeed_gcm_setauthsize,
			.encrypt	= aspeed_gcm_encrypt,
			.decrypt	= aspeed_gcm_decrypt,
			.init		= aspeed_gcm_init,
			.exit		= aspeed_gcm_exit,
			.ivsize		= 12,
			.maxauthsize	= AES_BLOCK_SIZE,

			.base = {
				.cra_name		= "gcm(aes)",
				.cra_driver_name	= "aspeed-gcm-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			},
		}
	}
};

int aspeed_register_hace_crypto_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs); i++) {
		aspeed_crypto_algs[i].hace_dev = hace_dev;
		err = crypto_register_skcipher(&aspeed_crypto_algs[i].alg.skcipher);
		if (err)
			return err;
	}
	if (hace_dev->version == 6) {
		for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs_g6); i++) {
			aspeed_crypto_algs_g6[i].hace_dev = hace_dev;
			err = crypto_register_skcipher(&aspeed_crypto_algs_g6[i].alg.skcipher);
			if (err)
				return err;
		}
#if 0
		for (i = 0; i < ARRAY_SIZE(aspeed_aead_algs_g6); i++) {
			aspeed_aead_algs_g6[i].hace_dev = hace_dev;
			err = crypto_register_aead(&aspeed_aead_algs_g6[i].alg.aead);
			if (err)
				return err;
		}
#endif
	}
	return 0;
}
