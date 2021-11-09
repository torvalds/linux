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

#define MASK_BC_MODE(mode)	((mode) & 0x00f0)
#define IS_BC_DECRYPT(mode)	(!!((mode) & CRYPTO_BC_DECRYPT))

static const u32 cipher_algo2bc[] = {
	[CIPHER_ALGO_DES]      = CRYPTO_BC_DES,
	[CIPHER_ALGO_DES3_EDE] = CRYPTO_BC_TDES,
	[CIPHER_ALGO_AES]      = CRYPTO_BC_AES,
	[CIPHER_ALGO_SM4]      = CRYPTO_BC_SM4,
};

static const u32 cipher_mode2bc[] = {
	[CIPHER_MODE_ECB] = CRYPTO_BC_ECB,
	[CIPHER_MODE_CBC] = CRYPTO_BC_CBC,
	[CIPHER_MODE_CFB] = CRYPTO_BC_CFB,
	[CIPHER_MODE_OFB] = CRYPTO_BC_OFB,
	[CIPHER_MODE_CTR] = CRYPTO_BC_CTR,
	[CIPHER_MODE_XTS] = CRYPTO_BC_XTS,
};

static struct rk_alg_ctx *rk_alg_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	return &ctx->algs_ctx;
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	interrupt_status = CRYPTO_READ(rk_dev, CRYPTO_DMA_INT_ST);
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_ST, interrupt_status);

	interrupt_status &= CRYPTO_LOCKSTEP_MASK;

	if (interrupt_status != CRYPTO_DST_ITEM_DONE_INT_ST) {
		dev_err(rk_dev->dev, "DMA desc = %p\n", hw_info->desc);
		dev_err(rk_dev->dev, "DMA addr_in = %08x\n",
			(u32)alg_ctx->addr_in);
		dev_err(rk_dev->dev, "DMA addr_out = %08x\n",
			(u32)alg_ctx->addr_out);
		dev_err(rk_dev->dev, "DMA count = %08x\n", alg_ctx->count);
		dev_err(rk_dev->dev, "DMA desc_dma = %08x\n",
			(u32)hw_info->desc_dma);
		dev_err(rk_dev->dev, "DMA Error status = %08x\n",
			interrupt_status);
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_LLI_ADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_LLI_ADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_ST status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_ST));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_STATE status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_STATE));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_LLI_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_LLI_RADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_SRC_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_SRC_RADDR));
		dev_err(rk_dev->dev, "DMA CRYPTO_DMA_DST_RADDR status = %08x\n",
			CRYPTO_READ(rk_dev, CRYPTO_DMA_DST_RADDR));
		rk_dev->err = -EFAULT;
	}

	return 0;
}

static inline u32 byte2word_be(const u8 *ch)
{
	return (*ch << 24) + (*(ch + 1) << 16) +
		    (*(ch + 2) << 8) + *(ch + 3);
}

static void set_iv_reg(struct rk_crypto_dev *rk_dev, const u8 *iv, u32 iv_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 base_iv;

	if (!iv || iv_len == 0)
		return;

	base_iv = CRYPTO_CH0_IV_0;
	/* write iv data to reg */
	for (i = 0; i < iv_len / 4; i++, base_iv += 4)
		CRYPTO_WRITE(rk_dev, base_iv, byte2word_be(iv + i * 4));

	if (iv_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, iv + (iv_len / 4) * 4, iv_len % 4);
		CRYPTO_WRITE(rk_dev, base_iv, byte2word_be(tmp_buf));
	}

	CRYPTO_WRITE(rk_dev, CRYPTO_CH0_IV_LEN_0, iv_len);
}

static void write_key_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			  u32 key_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 base_addr;

	base_addr = CRYPTO_CH0_KEY_0;

	for (i = 0; i < key_len / 4; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr,
			     byte2word_be(key + i * 4));

	if (key_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, key + i * 4, key_len % 4);
		CRYPTO_WRITE(rk_dev, base_addr,
			     byte2word_be(tmp_buf));
	}
}

static void write_tkey_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			   u32 key_len)
{
	u32 i;
	u8 tmp_buf[4];
	u32 base_addr;

	base_addr = CRYPTO_CH4_KEY_0;

	for (i = 0; i < key_len / 4; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr,
			     byte2word_be(key + i * 4));

	if (key_len % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, key + i * 4, key_len % 4);
		CRYPTO_WRITE(rk_dev, base_addr,
			     byte2word_be(tmp_buf));
	}
}

static struct rk_crypto_algt *rk_cipher_get_algt(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);

	return container_of(alg, struct rk_crypto_algt, alg.crypto);
}

static bool is_force_fallback(struct rk_crypto_algt *algt, uint32_t key_len)
{
	if (algt->algo != CIPHER_ALGO_AES)
		return false;

	/* crypto v2 not support xts with AES-192 */
	if (algt->mode == CIPHER_MODE_XTS && key_len == AES_KEYSIZE_192 * 2)
		return true;

	if (algt->use_soft_aes192 && key_len == AES_KEYSIZE_192)
		return true;

	return false;
}

static bool is_no_multi_blocksize(struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);

	return (algt->mode == CIPHER_MODE_CFB ||
			algt->mode == CIPHER_MODE_OFB ||
			algt->mode == CIPHER_MODE_CTR ||
			algt->mode == CIPHER_MODE_XTS) ? true : false;
}

static bool is_calc_need_round_up(struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);

	return (algt->mode == CIPHER_MODE_CFB ||
			algt->mode == CIPHER_MODE_OFB ||
			algt->mode == CIPHER_MODE_CTR) ? true : false;
}

static void rk_cipher_reset(struct rk_crypto_dev *rk_dev)
{
	u32 tmp = 0, tmp_mask = 0;

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x00);

	tmp = CRYPTO_SW_CC_RESET;
	tmp_mask = tmp << CRYPTO_WRITE_MASK_SHIFT;

	CRYPTO_WRITE(rk_dev, CRYPTO_RST_CTL, tmp | tmp_mask);
	while (CRYPTO_READ(rk_dev, CRYPTO_RST_CTL))
		nop();

	CRYPTO_WRITE(rk_dev, CRYPTO_BC_CTL, 0xffff0000);
}

static void rk_crypto_complete(struct crypto_async_request *base, int err)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(base->tfm);
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;
	struct rk_hw_crypto_v2_info *hw_info = ctx->rk_dev->hw_info;
	struct crypto_lli_desc *lli_desc = hw_info->desc;

	CRYPTO_WRITE(ctx->rk_dev, CRYPTO_BC_CTL, 0xffff0000);
	if (err) {
		rk_cipher_reset(ctx->rk_dev);
		pr_err("aligned = %u, align_size = %u\n",
		       alg_ctx->aligned, alg_ctx->align_size);
		pr_err("total = %u, left = %u, count = %u\n",
		       alg_ctx->total, alg_ctx->left_bytes, alg_ctx->count);
		pr_err("lli->src     = %08x\n", lli_desc->src_addr);
		pr_err("lli->src_len = %08x\n", lli_desc->src_len);
		pr_err("lli->dst     = %08x\n", lli_desc->dst_addr);
		pr_err("lli->dst_len = %08x\n", lli_desc->dst_len);
		pr_err("lli->dma_ctl = %08x\n", lli_desc->dma_ctrl);
		pr_err("lli->usr_def = %08x\n", lli_desc->user_define);
		pr_err("lli->next    = %08x\n\n\n", lli_desc->next_addr);

	}

	if (base->complete)
		base->complete(base, err);
}

static int rk_handle_req(struct rk_crypto_dev *rk_dev,
			 struct skcipher_request *req)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	if (!IS_ALIGNED(req->cryptlen, ctx->algs_ctx.chunk_size) &&
	    !is_no_multi_blocksize(req))
		return -EINVAL;
	else
		return rk_dev->enqueue(rk_dev, &req->base);
}

static int rk_cipher_setkey(struct crypto_skcipher *cipher,
			    const u8 *key, unsigned int keylen)
{
	struct rk_crypto_algt *algt = rk_cipher_get_algt(cipher);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	uint32_t key_factor;
	int ret = -EINVAL;

	CRYPTO_MSG("algo = %x, mode = %x, key_len = %d\n",
		   algt->algo, algt->mode, keylen);

	/* The key length of XTS is twice the normal length */
	key_factor = algt->mode == CIPHER_MODE_XTS ? 2 : 1;

	switch (algt->algo) {
	case CIPHER_ALGO_DES:
		ret = verify_skcipher_des_key(cipher, key);
		if (ret)
			goto exit;
		break;
	case CIPHER_ALGO_DES3_EDE:
		ret = verify_skcipher_des3_key(cipher, key);
		if (ret)
			goto exit;
		break;
	case CIPHER_ALGO_AES:
		if (keylen != (AES_KEYSIZE_128 * key_factor) &&
		    keylen != (AES_KEYSIZE_192 * key_factor) &&
		    keylen != (AES_KEYSIZE_256 * key_factor))
			goto exit;
		break;
	case CIPHER_ALGO_SM4:
		if (keylen != (SM4_KEY_SIZE * key_factor))
			goto exit;
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;
	ctx->fallback_key_inited = false;

	ret = 0;
exit:
	return ret;
}

static int rk_cipher_fallback(struct skcipher_request *req,
			      struct rk_cipher_ctx *ctx,
			      bool encrypt)
{
	int ret;

	CRYPTO_MSG("use fallback tfm");

	if (!ctx->fallback_tfm) {
		ret = -ENODEV;
		CRYPTO_MSG("fallback_tfm is empty!\n");
		goto exit;
	}

	if (!ctx->fallback_key_inited) {
		ret = crypto_skcipher_setkey(ctx->fallback_tfm,
					     ctx->key, ctx->keylen);
		if (ret) {
			CRYPTO_MSG("fallback crypto_skcipher_setkey err = %d\n",
				   ret);
			goto exit;
		}

		ctx->fallback_key_inited = true;
	}

	skcipher_request_set_tfm(&ctx->fallback_req, ctx->fallback_tfm);
	skcipher_request_set_callback(&ctx->fallback_req,
				      req->base.flags,
				      req->base.complete,
				      req->base.data);

	skcipher_request_set_crypt(&ctx->fallback_req, req->src,
				   req->dst, req->cryptlen, req->iv);

	ret = encrypt ? crypto_skcipher_encrypt(&ctx->fallback_req) :
			crypto_skcipher_decrypt(&ctx->fallback_req);

exit:
	return ret;
}

static int rk_cipher_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_crypto_algt *algt = rk_cipher_get_algt(tfm);

	CRYPTO_TRACE("%s total = %u",
		     encrypt ? "encrypt" : "decrypt", req->cryptlen);

	if (!req->cryptlen) {
		if (algt->mode == CIPHER_MODE_ECB ||
		    algt->mode == CIPHER_MODE_CBC ||
		    algt->mode == CIPHER_MODE_CTR ||
		    algt->mode == CIPHER_MODE_CFB ||
		    algt->mode == CIPHER_MODE_OFB)
			return 0;
		else
			return -EINVAL;
	}

	/* XTS data should >= chunksize */
	if (algt->mode == CIPHER_MODE_XTS &&
	    req->cryptlen < crypto_skcipher_chunksize(tfm))
		return -EINVAL;

	if (is_force_fallback(algt, ctx->keylen) ||
	    req->cryptlen > ctx->rk_dev->vir_max) {
		return rk_cipher_fallback(req, ctx, encrypt);
	}

	ctx->mode = cipher_algo2bc[algt->algo] |
		    cipher_mode2bc[algt->mode];
	if (!encrypt)
		ctx->mode |= CRYPTO_BC_DECRYPT;

	if (algt->algo == CIPHER_ALGO_AES) {
		uint32_t key_factor;

		/* The key length of XTS is twice the normal length */
		key_factor = algt->mode == CIPHER_MODE_XTS ? 2 : 1;

		if (ctx->keylen == AES_KEYSIZE_128 * key_factor)
			ctx->mode |= CRYPTO_BC_128_bit_key;
		else if (ctx->keylen == AES_KEYSIZE_192 * key_factor)
			ctx->mode |= CRYPTO_BC_192_bit_key;
		else if (ctx->keylen == AES_KEYSIZE_256 * key_factor)
			ctx->mode |= CRYPTO_BC_256_bit_key;
	}

	if (!encrypt && (req->src == req->dst)) {
		u32 ivsize = crypto_skcipher_ivsize(tfm);

		if (req->cryptlen >= ivsize)
			sg_pcopy_to_buffer(req->src, sg_nents(req->src),
					   ctx->lastc, ivsize,
					   req->cryptlen - ivsize);
	}

	CRYPTO_MSG("ctx->mode = %x\n", ctx->mode);
	return rk_handle_req(ctx->rk_dev, req);
}

static int rk_cipher_encrypt(struct skcipher_request *req)
{
	return rk_cipher_crypt(req, true);
}

static int rk_cipher_decrypt(struct skcipher_request *req)
{
	return rk_cipher_crypt(req, false);
}

static void rk_ablk_hw_init(struct rk_crypto_dev *rk_dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	u32 ivsize;

	CRYPTO_WRITE(rk_dev, CRYPTO_BC_CTL, 0x00010000);

	ivsize = crypto_skcipher_ivsize(cipher);

	if (MASK_BC_MODE(ctx->mode) == CRYPTO_BC_XTS) {
		uint32_t tmp_len = ctx->keylen / 2;

		write_key_reg(ctx->rk_dev, ctx->key, tmp_len);
		write_tkey_reg(ctx->rk_dev, ctx->key + tmp_len, tmp_len);
	} else {
		write_key_reg(ctx->rk_dev, ctx->key, ctx->keylen);
	}

	if (MASK_BC_MODE(ctx->mode) != CRYPTO_BC_ECB)
		set_iv_reg(rk_dev, req->iv, ivsize);

	ctx->mode |= CRYPTO_BC_ENABLE;

	CRYPTO_WRITE(rk_dev, CRYPTO_FIFO_CTL, 0x00030003);

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x7f);

	CRYPTO_WRITE(rk_dev, CRYPTO_BC_CTL, ctx->mode | CRYPTO_WRITE_MASK_ALL);
}

static void crypto_dma_start(struct rk_crypto_dev *rk_dev)
{
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 calc_len = alg_ctx->count;
	u32 start_flag = CRYPTO_DMA_START;

	memset(hw_info->desc, 0x00, sizeof(*hw_info->desc));

	/*
	 *	the data length is not aligned will use addr_vir to calculate,
	 *	so crypto v2 could round up data length to chunk_size
	 */
	if (is_calc_need_round_up(req))
		calc_len = round_up(calc_len, alg_ctx->chunk_size);

	hw_info->desc->src_addr    = alg_ctx->addr_in;
	hw_info->desc->src_len     = calc_len;
	hw_info->desc->dst_addr    = alg_ctx->addr_out;
	hw_info->desc->dst_len     = calc_len;
	hw_info->desc->next_addr   = 0;
	hw_info->desc->dma_ctrl    = LLI_DMA_CTRL_DST_DONE | LLI_DMA_CTRL_LAST;
	hw_info->desc->user_define = LLI_USER_STRING_START |
				     LLI_USER_CIPHER_START |
				     LLI_USER_STRING_LAST;

	dma_wmb();

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_LLI_ADDR, hw_info->desc_dma);
	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_CTL, start_flag | (start_flag << WRITE_MASK));
}

static int rk_set_data_start(struct rk_crypto_dev *rk_dev)
{
	int err;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err)
		crypto_dma_start(rk_dev);
	return err;
}

static int rk_ablk_start(struct rk_crypto_dev *rk_dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	unsigned long flags;
	int err = 0;

	alg_ctx->left_bytes = req->cryptlen;
	alg_ctx->total      = req->cryptlen;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, req->cryptlen);
	alg_ctx->sg_dst     = req->dst;
	alg_ctx->req_dst    = req->dst;
	alg_ctx->dst_nents  = sg_nents_for_len(req->dst, req->cryptlen);

	CRYPTO_TRACE("total = %u", alg_ctx->total);

	spin_lock_irqsave(&rk_dev->lock, flags);
	rk_ablk_hw_init(rk_dev);
	err = rk_set_data_start(rk_dev);
	spin_unlock_irqrestore(&rk_dev->lock, flags);
	return err;
}

/* increment counter (128-bit int) by 1 */
static void rk_ctr128_inc(uint8_t *counter)
{
	u32 n = 16;
	u8  c;

	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

static void rk_ctr128_calc(uint8_t *counter, uint32_t data_len)
{
	u32 i;
	u32 chunksize = AES_BLOCK_SIZE;

	for (i = 0; i < DIV_ROUND_UP(data_len, chunksize); i++)
		rk_ctr128_inc(counter);
}

static void rk_iv_copyback(struct rk_crypto_dev *rk_dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 ivsize = crypto_skcipher_ivsize(tfm);

	u32 bc_mode = MASK_BC_MODE(ctx->mode);

	if (!req->iv)
		return;

	if (bc_mode == CRYPTO_BC_CTR) {
		/* calc new counter for CTR mode */
		rk_ctr128_calc(req->iv, req->cryptlen);
	} else if (bc_mode == CRYPTO_BC_CBC) {
		if (!IS_BC_DECRYPT(ctx->mode)) {
			sg_pcopy_to_buffer(req->dst, sg_nents(req->dst),
					   req->iv, ivsize,
					   req->cryptlen - ivsize);
		} else {
			if (req->src == req->dst)
				memcpy(req->iv, ctx->lastc, ivsize);
			else
				sg_pcopy_to_buffer(req->src, sg_nents(req->src),
						   req->iv, ivsize,
						   req->cryptlen - ivsize);
		}

	} else {
		/* do nothing */
	}
}
/* return:
 *	true	some err was occurred
 *	fault	no err, continue
 */
static int rk_ablk_rx(struct rk_crypto_dev *rk_dev)
{
	int err = 0;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_TRACE("left_bytes = %u\n", alg_ctx->left_bytes);

	err = rk_dev->unload_data(rk_dev);
	if (err)
		goto out_rx;

	if (alg_ctx->left_bytes) {
		if (alg_ctx->aligned) {
			if (sg_is_last(alg_ctx->sg_src)) {
				dev_err(rk_dev->dev, "[%s:%d] Lack of data\n",
					__func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			alg_ctx->sg_src = sg_next(alg_ctx->sg_src);
			alg_ctx->sg_dst = sg_next(alg_ctx->sg_dst);
		}
		err = rk_set_data_start(rk_dev);
	} else {
		rk_iv_copyback(rk_dev);
	}
out_rx:
	return err;
}

static int rk_ablk_init_tfm(struct crypto_skcipher *tfm)
{
	struct rk_crypto_algt *algt = rk_cipher_get_algt(tfm);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(crypto_skcipher_tfm(tfm));
	struct rk_crypto_dev *rk_dev = algt->rk_dev;
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;

	CRYPTO_TRACE();

	memset(ctx, 0x00, sizeof(*ctx));

	if (!rk_dev->request_crypto)
		return -EFAULT;

	rk_dev->request_crypto(rk_dev, alg_name);

	/* always not aligned for crypto v2 cipher */
	alg_ctx->align_size     = rk_dev->vir_max;
	alg_ctx->chunk_size     = crypto_skcipher_chunksize(tfm);

	alg_ctx->ops.start      = rk_ablk_start;
	alg_ctx->ops.update     = rk_ablk_rx;
	alg_ctx->ops.complete   = rk_crypto_complete;
	alg_ctx->ops.irq_handle = rk_crypto_irq_handle;

	ctx->rk_dev = rk_dev;

	if (algt->alg.crypto.base.cra_flags & CRYPTO_ALG_NEED_FALLBACK) {
		CRYPTO_MSG("alloc fallback tfm, name = %s", alg_name);
		ctx->fallback_tfm = crypto_alloc_skcipher(alg_name, 0,
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK);
		if (IS_ERR(ctx->fallback_tfm)) {
			CRYPTO_MSG("Could not load fallback driver %s : %ld.\n",
				   alg_name, PTR_ERR(ctx->fallback_tfm));
			ctx->fallback_tfm = NULL;
		}
	}

	return 0;
}

static void rk_ablk_exit_tfm(struct crypto_skcipher *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	const char *alg_name = crypto_tfm_alg_name(crypto_skcipher_tfm(tfm));

	CRYPTO_TRACE();

	if (ctx->fallback_tfm) {
		CRYPTO_MSG("free fallback tfm");
		crypto_free_skcipher(ctx->fallback_tfm);
	}

	ctx->rk_dev->release_crypto(ctx->rk_dev, alg_name);
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

	if (info && info->desc)
		dma_free_coherent(dev, sizeof(struct crypto_lli_desc),
				  info->desc, info->desc_dma);
}

struct rk_crypto_algt rk_v2_ecb_sm4_alg =
	RK_CIPHER_ALGO_INIT(SM4, ECB, ecb(sm4), ecb-sm4-rk);

struct rk_crypto_algt rk_v2_cbc_sm4_alg =
	RK_CIPHER_ALGO_INIT(SM4, CBC, cbc(sm4), cbc-sm4-rk);

struct rk_crypto_algt rk_v2_xts_sm4_alg =
	RK_CIPHER_ALGO_XTS_INIT(SM4, xts(sm4), xts-sm4-rk);

struct rk_crypto_algt rk_v2_cfb_sm4_alg =
	RK_CIPHER_ALGO_INIT(SM4, CFB, cfb(sm4), cfb-sm4-rk);

struct rk_crypto_algt rk_v2_ofb_sm4_alg =
	RK_CIPHER_ALGO_INIT(SM4, OFB, ofb(sm4), ofb-sm4-rk);

struct rk_crypto_algt rk_v2_ctr_sm4_alg =
	RK_CIPHER_ALGO_INIT(SM4, CTR, ctr(sm4), ctr-sm4-rk);

struct rk_crypto_algt rk_v2_ecb_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, ECB, ecb(aes), ecb-aes-rk);

struct rk_crypto_algt rk_v2_cbc_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, CBC, cbc(aes), cbc-aes-rk);

struct rk_crypto_algt rk_v2_xts_aes_alg =
	RK_CIPHER_ALGO_XTS_INIT(AES, xts(aes), xts-aes-rk);

struct rk_crypto_algt rk_v2_cfb_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, CFB, cfb(aes), cfb-aes-rk);

struct rk_crypto_algt rk_v2_ofb_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, OFB, ofb(aes), ofb-aes-rk);

struct rk_crypto_algt rk_v2_ctr_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, CTR, ctr(aes), ctr-aes-rk);

struct rk_crypto_algt rk_v2_ecb_des_alg =
	RK_CIPHER_ALGO_INIT(DES, ECB, ecb(des), ecb-des-rk);

struct rk_crypto_algt rk_v2_cbc_des_alg =
	RK_CIPHER_ALGO_INIT(DES, CBC, cbc(des), cbc-des-rk);

struct rk_crypto_algt rk_v2_cfb_des_alg =
	RK_CIPHER_ALGO_INIT(DES, CFB, cfb(des), cfb-des-rk);

struct rk_crypto_algt rk_v2_ofb_des_alg =
	RK_CIPHER_ALGO_INIT(DES, OFB, ofb(des), ofb-des-rk);

struct rk_crypto_algt rk_v2_ecb_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, ECB, ecb(des3_ede), ecb-des3_ede-rk);

struct rk_crypto_algt rk_v2_cbc_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, CBC, cbc(des3_ede), cbc-des3_ede-rk);

struct rk_crypto_algt rk_v2_cfb_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, CFB, cfb(des3_ede), cfb-des3_ede-rk);

struct rk_crypto_algt rk_v2_ofb_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, OFB, ofb(des3_ede), ofb-des3_ede-rk);

