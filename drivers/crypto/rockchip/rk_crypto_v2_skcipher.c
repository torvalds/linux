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

#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "rk_crypto_core.h"
#include "rk_crypto_skcipher_utils.h"
#include "rk_crypto_v2.h"
#include "rk_crypto_v2_reg.h"

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

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev = platform_get_drvdata(dev_id);
	u32 interrupt_status;
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);

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

static void set_iv_reg(struct rk_crypto_dev *rk_dev, const u8 *iv, u32 iv_len)
{
	if (!iv || iv_len == 0)
		return;

	CRYPTO_DUMPHEX("set iv", iv, iv_len);

	rk_crypto_write_regs(rk_dev, CRYPTO_CH0_IV_0, iv, iv_len);

	CRYPTO_WRITE(rk_dev, CRYPTO_CH0_IV_LEN_0, iv_len);
}

static void write_key_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			  u32 key_len)
{
	rk_crypto_write_regs(rk_dev, CRYPTO_CH0_KEY_0, key, key_len);
}

static void write_tkey_reg(struct rk_crypto_dev *rk_dev, const u8 *key,
			   u32 key_len)
{
	rk_crypto_write_regs(rk_dev, CRYPTO_CH4_KEY_0, key, key_len);
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
	int ret;
	u32 tmp = 0, tmp_mask = 0;
	unsigned int  pool_timeout_us = 1000;

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x00);

	tmp = CRYPTO_SW_CC_RESET;
	tmp_mask = tmp << CRYPTO_WRITE_MASK_SHIFT;

	CRYPTO_WRITE(rk_dev, CRYPTO_RST_CTL, tmp | tmp_mask);

	/* This is usually done in 20 clock cycles */
	ret = readl_poll_timeout_atomic(rk_dev->reg + CRYPTO_RST_CTL,
					tmp, !tmp, 0, pool_timeout_us);
	if (ret)
		dev_err(rk_dev->dev, "cipher reset pool timeout %ums.",
			pool_timeout_us);

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
	if (algt->mode == CIPHER_MODE_XTS) {
		if (req->cryptlen < crypto_skcipher_chunksize(tfm))
			return -EINVAL;

		/* force use unalign branch */
		ctx->algs_ctx.align_size = ctx->rk_dev->vir_max;

		/*  XTS can't pause when use hardware crypto */
		if (req->cryptlen > ctx->rk_dev->vir_max)
			return rk_cipher_fallback(req, ctx, encrypt);
	}

	if (is_force_fallback(algt, ctx->keylen))
		return rk_cipher_fallback(req, ctx, encrypt);

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

	if (req->iv)
		memcpy(ctx->iv, req->iv, crypto_skcipher_ivsize(tfm));

	ctx->is_enc = encrypt;

	CRYPTO_MSG("ctx->mode = %x\n", ctx->mode);
	return rk_skcipher_handle_req(ctx->rk_dev, req);
}

static int rk_cipher_encrypt(struct skcipher_request *req)
{
	return rk_cipher_crypt(req, true);
}

static int rk_cipher_decrypt(struct skcipher_request *req)
{
	return rk_cipher_crypt(req, false);
}

static int rk_ablk_hw_init(struct rk_crypto_dev *rk_dev, u32 algo, u32 mode)
{
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	u32 ivsize;

	rk_cipher_reset(rk_dev);

	CRYPTO_WRITE(rk_dev, CRYPTO_BC_CTL, 0x00010000);

	ivsize = crypto_skcipher_ivsize(cipher);

	if (mode == CIPHER_MODE_XTS) {
		uint32_t tmp_len = ctx->keylen / 2;

		write_key_reg(ctx->rk_dev, ctx->key, tmp_len);
		write_tkey_reg(ctx->rk_dev, ctx->key + tmp_len, tmp_len);
	} else {
		write_key_reg(ctx->rk_dev, ctx->key, ctx->keylen);
	}

	if (mode != CIPHER_MODE_ECB)
		set_iv_reg(rk_dev, req->iv, ivsize);

	ctx->mode |= CRYPTO_BC_ENABLE;

	CRYPTO_WRITE(rk_dev, CRYPTO_FIFO_CTL, 0x00030003);

	CRYPTO_WRITE(rk_dev, CRYPTO_DMA_INT_EN, 0x7f);

	CRYPTO_WRITE(rk_dev, CRYPTO_BC_CTL, ctx->mode | CRYPTO_WRITE_MASK_ALL);

	return 0;
}

static int crypto_dma_start(struct rk_crypto_dev *rk_dev, uint32_t flag)
{
	struct rk_hw_crypto_v2_info *hw_info =
			(struct rk_hw_crypto_v2_info *)rk_dev->hw_info;
	struct skcipher_request *req =
		skcipher_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_cipher_alg_ctx(rk_dev);
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

	return 0;
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
	alg_ctx->align_size     = 64;
	alg_ctx->chunk_size     = crypto_skcipher_chunksize(tfm);

	alg_ctx->ops.start      = rk_ablk_start;
	alg_ctx->ops.update     = rk_ablk_rx;
	alg_ctx->ops.complete   = rk_crypto_complete;
	alg_ctx->ops.irq_handle = rk_crypto_irq_handle;

	alg_ctx->ops.hw_init      = rk_ablk_hw_init;
	alg_ctx->ops.hw_dma_start = crypto_dma_start;
	alg_ctx->ops.hw_write_iv  = set_iv_reg;

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

