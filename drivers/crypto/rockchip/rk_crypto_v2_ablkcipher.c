// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip Crypto V2
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include "rk_crypto_core.h"
#include "rk_crypto_v2.h"
#include "rk_crypto_v2_reg.h"

#define RK_CRYPTO_DEC		BIT(0)
#define IS_AES_XTS(mode)	(((mode) & RK_CRYPTO_AES_XTS_MODE) \
					== RK_CRYPTO_AES_XTS_MODE)

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_info *dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)dev->hw_info;

	interrupt_status = CRYPTO_READ(dev, CRYPTO_DMA_INT_ST);
	CRYPTO_WRITE(dev, CRYPTO_DMA_INT_ST, interrupt_status);

	if (interrupt_status != CRYPTO_DST_ITEM_DONE_INT_ST) {
		dev_err(dev->dev, "DMA desc = %p\n", hw_info->desc);
		dev_err(dev->dev, "DMA addr_in = %08x\n",
			(u32)dev->addr_in);
		dev_err(dev->dev, "DMA addr_out = %08x\n",
			(u32)dev->addr_out);
		dev_err(dev->dev, "DMA count = %08x\n", dev->count);
		dev_err(dev->dev, "DMA desc_dma = %08x\n",
			(u32)hw_info->desc_dma);
		dev_err(dev->dev, "DMA Error status = %08x\n",
			interrupt_status);
		dev_err(dev->dev, "DMA CRYPTO_DMA_LLI_ADDR status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_LLI_ADDR));
		dev_err(dev->dev, "DMA CRYPTO_DMA_ST status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_ST));
		dev_err(dev->dev, "DMA CRYPTO_DMA_STATE status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_STATE));
		dev_err(dev->dev, "DMA CRYPTO_DMA_LLI_RADDR status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_LLI_RADDR));
		dev_err(dev->dev, "DMA CRYPTO_DMA_SRC_RADDR status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_SRC_RADDR));
		dev_err(dev->dev, "DMA CRYPTO_DMA_DST_RADDR status = %08x\n",
			CRYPTO_READ(dev, CRYPTO_DMA_DST_RADDR));
		dev->err = -EFAULT;
	}

	return 0;
}

static u32 byte2word(const u8 *ch, u32 endian)
{
	u32 w = 0;

	/* 0: Big-Endian 1: Little-Endian */
	if (endian == BIG_ENDIAN)
		w = (*ch << 24) + (*(ch + 1) << 16) +
		    (*(ch + 2) << 8) + *(ch + 3);
	else if (endian == LITTLE_ENDIAN)
		w = (*(ch + 3) << 24) + (*(ch + 2) << 16) +
		    (*(ch + 1) << 8) + *ch;

	return w;
}

static void word2byte(u32 word, u8 *ch, u32 endian)
{
	/* 0: Big-Endian 1: Little-Endian */
	if (endian == BIG_ENDIAN) {
		ch[0] = (word >> 24) & 0xff;
		ch[1] = (word >> 16) & 0xff;
		ch[2] = (word >> 8) & 0xff;
		ch[3] = (word >> 0) & 0xff;
	} else if (endian == LITTLE_ENDIAN) {
		ch[0] = (word >> 0) & 0xff;
		ch[1] = (word >> 8) & 0xff;
		ch[2] = (word >> 16) & 0xff;
		ch[3] = (word >> 24) & 0xff;
	} else {
		ch[0] = 0;
		ch[1] = 0;
		ch[2] = 0;
		ch[3] = 0;
	}
}

static void set_iv_reg(struct rk_crypto_info *dev, const u8 *iv, u32 iv_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 base_iv;

	base_iv = CRYPTO_CH0_IV_0;
	/* write iv data to reg */
	for (i = 0; i < iv_len / 4; i++, base_iv += 4)
		CRYPTO_WRITE(dev, base_iv, byte2word(iv + i * 4, BIG_ENDIAN));

	if (iv_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, iv + (iv_len / 4) * 4, iv_len % 4);
		CRYPTO_WRITE(dev, base_iv, byte2word(tmp_buf, BIG_ENDIAN));
	}

	CRYPTO_WRITE(dev, CRYPTO_CH0_IV_LEN_0, iv_len);
}

static void read_iv_reg(struct rk_crypto_info *dev, u8 *iv, u32 iv_len)
{
	u32 i;
	u32 base;

	base = CRYPTO_CH0_IV_0;
	/* read iv data from reg */
	for (i = 0; i < iv_len / 4; i++, base += 4)
		word2byte(CRYPTO_READ(dev, base), iv + 4 * i, BIG_ENDIAN);
}

static void write_key_reg(struct rk_crypto_info *dev, const u8 *key,
			  u32 key_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 chn_base_addr;

	chn_base_addr = CRYPTO_CH0_KEY_0;

	for (i = 0; i < key_len / 4; i++, chn_base_addr += 4)
		CRYPTO_WRITE(dev, chn_base_addr,
			     byte2word(key + i * 4, BIG_ENDIAN));

	if (key_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, key + i * 4, key_len % 4);
		CRYPTO_WRITE(dev, chn_base_addr,
			     byte2word(tmp_buf, BIG_ENDIAN));
	}
}

static void write_tkey_reg(struct rk_crypto_info *dev, const u8 *key,
			   u32 key_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 chn_base_addr;

	chn_base_addr = CRYPTO_CH4_KEY_0;

	for (i = 0; i < key_len / 4; i++, chn_base_addr += 4)
		CRYPTO_WRITE(dev, chn_base_addr,
			     byte2word(key + i * 4, BIG_ENDIAN));

	if (key_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, key + i * 4, key_len % 4);
		CRYPTO_WRITE(dev, chn_base_addr,
			     byte2word(tmp_buf, BIG_ENDIAN));
	}
}

static void rk_crypto_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static int rk_handle_req(struct rk_crypto_info *dev,
			 struct ablkcipher_request *req)
{
	if (!IS_ALIGNED(req->nbytes, dev->align_size))
		return -EINVAL;
	else
		return dev->enqueue(dev, &req->base);
}

static int rk_aes_setkey(struct crypto_ablkcipher *cipher,
			 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	return 0;
}

static int rk_aes_xts_setkey(struct crypto_ablkcipher *cipher,
			     const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_256 && keylen != (AES_KEYSIZE_256 * 2)) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen / 2;

	return 0;
}

static int rk_tdes_setkey(struct crypto_ablkcipher *cipher,
			  const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];

	if (keylen != DES_KEY_SIZE && keylen != DES3_EDE_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (keylen == DES_KEY_SIZE) {
		if (!des_ekey(tmp, key) &&
		    (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			return -EINVAL;
		}
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	return 0;
}

static int rk_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_AES | CRYPTO_BC_ECB;

	return rk_handle_req(dev, req);
}

static int rk_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_AES | CRYPTO_BC_ECB | CRYPTO_BC_DECRYPT;

	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_AES | CRYPTO_BC_CBC;

	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_AES | CRYPTO_BC_CBC | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static int rk_aes_xts_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_AES | CRYPTO_BC_XTS;
	return rk_handle_req(dev, req);
}

static int rk_aes_xts_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode =  CRYPTO_BC_AES | CRYPTO_BC_XTS | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_DES | CRYPTO_BC_ECB;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_DES | CRYPTO_BC_ECB | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_DES | CRYPTO_BC_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_DES | CRYPTO_BC_CBC | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_TDES | CRYPTO_BC_ECB;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_TDES | CRYPTO_BC_ECB | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_TDES | CRYPTO_BC_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	ctx->mode = CRYPTO_BC_TDES | CRYPTO_BC_CBC | CRYPTO_BC_DECRYPT;
	return rk_handle_req(dev, req);
}

static void rk_ablk_hw_init(struct rk_crypto_info *dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(dev->async_req);
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	u32 ivsize, block;

	CRYPTO_WRITE(dev, CRYPTO_BC_CTL, 0x00010000);

	block = crypto_tfm_alg_blocksize(tfm);
	ivsize = crypto_ablkcipher_ivsize(cipher);

	write_key_reg(ctx->dev, ctx->key, ctx->keylen);
	if ((ctx->mode & CRYPTO_BC_XTS) == CRYPTO_BC_XTS)
		write_tkey_reg(ctx->dev, ctx->key + ctx->keylen, ctx->keylen);

	set_iv_reg(dev, req->info, ivsize);

	if (block != DES_BLOCK_SIZE) {
		if (ctx->keylen == AES_KEYSIZE_128)
			ctx->mode |= CRYPTO_BC_128_bit_key;
		else if (ctx->keylen == AES_KEYSIZE_192)
			ctx->mode |= CRYPTO_BC_192_bit_key;
		else if (ctx->keylen == AES_KEYSIZE_256)
			ctx->mode |= CRYPTO_BC_256_bit_key;
	}

	ctx->mode |= CRYPTO_BC_ENABLE;

	CRYPTO_WRITE(dev, CRYPTO_FIFO_CTL, 0x00030003);

	CRYPTO_WRITE(dev, CRYPTO_DMA_INT_EN, 0x7f);

	CRYPTO_WRITE(dev, CRYPTO_BC_CTL, ctx->mode | CRYPTO_WRITE_MASK_ALL);
}

static void crypto_dma_start(struct rk_crypto_info *dev)
{
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)dev->hw_info;

	memset(hw_info->desc, 0x00, sizeof(*hw_info->desc));

	hw_info->desc->src_addr = dev->addr_in;
	hw_info->desc->src_len  = dev->count;
	hw_info->desc->dst_addr = dev->addr_out;
	hw_info->desc->dst_len  = dev->count;
	hw_info->desc->next_addr = 0;
	hw_info->desc->dma_ctrl = 0x00000201;
	hw_info->desc->user_define = 0x7;

	dma_sync_single_for_device(dev->dev, hw_info->desc_dma,
				   sizeof(*hw_info->desc), DMA_TO_DEVICE);
	CRYPTO_WRITE(dev, CRYPTO_DMA_LLI_ADDR, hw_info->desc_dma);
	CRYPTO_WRITE(dev, CRYPTO_DMA_CTL, 0x00010001);/* start */
}

static int rk_set_data_start(struct rk_crypto_info *dev)
{
	int err;

	err = dev->load_data(dev, dev->sg_src, dev->sg_dst);
	if (!err)
		crypto_dma_start(dev);
	return err;
}

static int rk_ablk_start(struct rk_crypto_info *dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(dev->async_req);
	unsigned long flags;
	int err = 0;

	dev->left_bytes = req->nbytes;
	dev->total = req->nbytes;
	dev->sg_src = req->src;
	dev->first = req->src;
	dev->src_nents = sg_nents(req->src);
	dev->sg_dst = req->dst;
	dev->dst_nents = sg_nents(req->dst);
	dev->aligned = 1;

	spin_lock_irqsave(&dev->lock, flags);
	rk_ablk_hw_init(dev);
	err = rk_set_data_start(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	return err;
}

static void rk_iv_copyback(struct rk_crypto_info *dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(dev->async_req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);

	u32 ivsize = crypto_ablkcipher_ivsize(tfm);

	read_iv_reg(dev, req->info, ivsize);
}

/* return:
 *	true	some err was occurred
 *	fault	no err, continue
 */
static int rk_ablk_rx(struct rk_crypto_info *dev)
{
	int err = 0;
	struct ablkcipher_request *req =
		ablkcipher_request_cast(dev->async_req);

	dev->unload_data(dev);
	if (!dev->aligned) {
		if (!sg_pcopy_from_buffer(req->dst, dev->dst_nents,
					  dev->addr_vir, dev->count,
					  dev->total - dev->left_bytes -
					  dev->count)) {
			err = -EINVAL;
			goto out_rx;
		}
	}
	if (dev->left_bytes) {
		if (dev->aligned) {
			if (sg_is_last(dev->sg_src)) {
				dev_err(dev->dev, "[%s:%d] Lack of data\n",
					__func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			dev->sg_src = sg_next(dev->sg_src);
			dev->sg_dst = sg_next(dev->sg_dst);
		}
		err = rk_set_data_start(dev);
	} else {
		rk_iv_copyback(dev);
		/* here show the calculation is over without any err */
		dev->complete(dev->async_req, 0);
		tasklet_schedule(&dev->queue_task);
	}
out_rx:
	return err;
}

static int rk_ablk_cra_init(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct rk_crypto_tmp *algt;
	struct rk_hw_crypto_v2_info *hw_info;

	algt = container_of(alg, struct rk_crypto_tmp, alg.crypto);

	ctx->dev = algt->dev;
	ctx->dev->align_size = crypto_tfm_alg_alignmask(tfm) + 1;
	ctx->dev->start = rk_ablk_start;
	ctx->dev->update = rk_ablk_rx;
	ctx->dev->complete = rk_crypto_complete;
	ctx->dev->irq_handle = rk_crypto_irq_handle;

	hw_info = (struct rk_hw_crypto_v2_info *)ctx->dev->hw_info;

	if (hw_info->clk_enable == 0)
		ctx->dev->enable_clk(ctx->dev);

	hw_info->clk_enable++;

	return 0;
}

static void rk_ablk_cra_exit(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)ctx->dev->hw_info;

	hw_info->clk_enable--;

	if (hw_info->clk_enable == 0)
		ctx->dev->disable_clk(ctx->dev);
}

int rk_hw_crypto_v2_init(struct device *dev, void *hw_info)
{
	int err = 0;
	struct rk_hw_crypto_v2_info *info =
		(struct rk_hw_crypto_v2_info *)hw_info;

	info->desc = dma_alloc_coherent(dev,
					sizeof(struct crypto_lli_desc),
					&info->desc_dma,
					GFP_KERNEL);
	if (!info->desc) {
		err = -ENOMEM;
		goto end;
	}

end:
	return err;
}

void rk_hw_crypto_v2_deinit(struct device *dev, void *hw_info)
{
	struct rk_hw_crypto_v2_info *info =
		(struct rk_hw_crypto_v2_info *)hw_info;

	dma_free_coherent(dev, sizeof(struct crypto_lli_desc),
			  info->desc, info->desc_dma);
}

struct rk_crypto_tmp rk_v2_ecb_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(aes)",
		.cra_driver_name	= "ecb-aes-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x0f,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= rk_aes_setkey,
			.encrypt	= rk_aes_ecb_encrypt,
			.decrypt	= rk_aes_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_cbc_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(aes)",
		.cra_driver_name	= "cbc-aes-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x0f,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= rk_aes_setkey,
			.encrypt	= rk_aes_cbc_encrypt,
			.decrypt	= rk_aes_cbc_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_xts_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "xts(aes)",
		.cra_driver_name	= "xts-aes-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x0f,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= AES_MAX_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE * 2,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= rk_aes_xts_setkey,
			.encrypt	= rk_aes_xts_encrypt,
			.decrypt	= rk_aes_xts_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_ecb_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(des)",
		.cra_driver_name	= "ecb-des-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des_ecb_encrypt,
			.decrypt	= rk_des_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_cbc_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(des)",
		.cra_driver_name	= "cbc-des-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des_cbc_encrypt,
			.decrypt	= rk_des_cbc_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_ecb_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(des3_ede)",
		.cra_driver_name	= "ecb-des3-ede-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des3_ede_ecb_encrypt,
			.decrypt	= rk_des3_ede_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_v2_cbc_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(des3_ede)",
		.cra_driver_name	= "cbc-des3-ede-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des3_ede_cbc_encrypt,
			.decrypt	= rk_des3_ede_cbc_decrypt,
		}
	}
};
