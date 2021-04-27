// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */
#include "rk_crypto_core.h"
#include "rk_crypto_v1.h"
#include "rk_crypto_v1_reg.h"

#define RK_CRYPTO_DEC			BIT(0)

static struct rk_alg_ctx *rk_alg_ctx_cast(
	struct rk_crypto_dev *rk_dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	return &ctx->algs_ctx;
}

static int rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;

	interrupt_status = CRYPTO_READ(rk_dev, RK_CRYPTO_INTSTS);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_INTSTS, interrupt_status);

	if (interrupt_status & 0x0a) {
		dev_warn(rk_dev->dev, "DMA Error\n");
		rk_dev->err = -EFAULT;
	}

	return 0;
}

static void rk_crypto_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static int rk_handle_req(struct rk_crypto_dev *rk_dev,
			 struct ablkcipher_request *req)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	if (!IS_ALIGNED(req->nbytes, ctx->algs_ctx.align_size))
		return -EINVAL;
	else
		return rk_dev->enqueue(rk_dev, &req->base);
}

static int rk_get_bc(u32 algo, u32 mode, u32 *bc_val)
{
	/* default DES ECB mode */
	*bc_val = 0;

	switch (algo) {
	case CIPHER_ALGO_DES3_EDE:
		*bc_val |= RK_CRYPTO_TDES_SELECT;
		/* fall through */
	case CIPHER_ALGO_DES:
		if (mode == CIPHER_MODE_ECB)
			*bc_val = 0;
		else if (mode == CIPHER_MODE_CBC)
			*bc_val = RK_CRYPTO_TDES_CHAINMODE_CBC;
		else
			goto error;
		break;
	case CIPHER_ALGO_AES:
		if (mode == CIPHER_MODE_ECB)
			*bc_val = RK_CRYPTO_AES_ECB_MODE;
		else if (mode == CIPHER_MODE_CBC)
			*bc_val = RK_CRYPTO_AES_CBC_MODE;
		else
			goto error;
		break;
	default:
		goto error;
	}

	return 0;
error:
	return -EINVAL;
}

static int rk_cipher_setkey(struct crypto_ablkcipher *cipher,
			    const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct rk_crypto_algt *algt;
	u32 tmp[DES_EXPKEY_WORDS];

	algt = container_of(alg, struct rk_crypto_algt, alg.crypto);

	CRYPTO_MSG("algo = %x, mode = %x, key_len = %d\n",
		   algt->algo, algt->mode, keylen);

	switch (algt->algo) {
	case CIPHER_ALGO_DES:
		if (keylen != DES_KEY_SIZE)
			goto error;

		if (!des_ekey(tmp, key) &&
		    (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			return -EINVAL;
		}
		break;
	case CIPHER_ALGO_DES3_EDE:
		if (keylen != DES3_EDE_KEY_SIZE)
			goto error;
		break;
	case CIPHER_ALGO_AES:
		if (keylen != AES_KEYSIZE_128 &&
		    keylen != AES_KEYSIZE_192 &&
		    keylen != AES_KEYSIZE_256)
			goto error;
		break;
	default:
		goto error;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;

error:
	crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}


static int rk_cipher_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;
	struct rk_crypto_algt *algt;
	int ret;

	algt = container_of(alg, struct rk_crypto_algt, alg.crypto);

	ret = rk_get_bc(algt->algo, algt->mode, &ctx->mode);
	if (ret)
		return ret;

	CRYPTO_MSG("ctx->mode = %x\n", ctx->mode);

	return rk_handle_req(rk_dev, req);
}

static int rk_cipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_alg *alg = tfm->base.__crt_alg;
	struct rk_crypto_dev *rk_dev = ctx->rk_dev;
	struct rk_crypto_algt *algt;
	int ret;

	algt = container_of(alg, struct rk_crypto_algt, alg.crypto);

	ret = rk_get_bc(algt->algo, algt->mode, &ctx->mode);
	if (ret)
		return ret;

	ctx->mode |= RK_CRYPTO_DEC;

	CRYPTO_MSG("ctx->mode = %x\n", ctx->mode);

	return rk_handle_req(rk_dev, req);
}

static void rk_ablk_hw_init(struct rk_crypto_dev *rk_dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	u32 ivsize, block, conf_reg = 0;

	block = crypto_tfm_alg_blocksize(tfm);
	ivsize = crypto_ablkcipher_ivsize(cipher);

	if (block == DES_BLOCK_SIZE) {
		memcpy_toio(ctx->rk_dev->reg + RK_CRYPTO_TDES_KEY1_0,
			    ctx->key, ctx->keylen);
		ctx->mode |= RK_CRYPTO_TDES_FIFO_MODE |
			     RK_CRYPTO_TDES_BYTESWAP_KEY |
			     RK_CRYPTO_TDES_BYTESWAP_IV;
		CRYPTO_WRITE(rk_dev, RK_CRYPTO_TDES_CTRL, ctx->mode);
		memcpy_toio(rk_dev->reg + RK_CRYPTO_TDES_IV_0,
			    req->info, ivsize);
		conf_reg = RK_CRYPTO_DESSEL;
	} else {
		memcpy_toio(ctx->rk_dev->reg + RK_CRYPTO_AES_KEY_0,
			    ctx->key, ctx->keylen);
		ctx->mode |= RK_CRYPTO_AES_FIFO_MODE |
			     RK_CRYPTO_AES_KEY_CHANGE |
			     RK_CRYPTO_AES_BYTESWAP_KEY |
			     RK_CRYPTO_AES_BYTESWAP_IV;
		if (ctx->keylen == AES_KEYSIZE_192)
			ctx->mode |= RK_CRYPTO_AES_192BIT_key;
		else if (ctx->keylen == AES_KEYSIZE_256)
			ctx->mode |= RK_CRYPTO_AES_256BIT_key;
		CRYPTO_WRITE(rk_dev, RK_CRYPTO_AES_CTRL, ctx->mode);
		memcpy_toio(rk_dev->reg + RK_CRYPTO_AES_IV_0,
			    req->info, ivsize);
	}
	conf_reg |= RK_CRYPTO_BYTESWAP_BTFIFO |
		    RK_CRYPTO_BYTESWAP_BRFIFO;
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CONF, conf_reg);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_INTENA,
		     RK_CRYPTO_BCDMA_ERR_ENA | RK_CRYPTO_BCDMA_DONE_ENA);
}

static void crypto_dma_start(struct rk_crypto_dev *rk_dev)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);

	CRYPTO_WRITE(rk_dev, RK_CRYPTO_BRDMAS, alg_ctx->addr_in);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_BRDMAL, alg_ctx->count / 4);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_BTDMAS, alg_ctx->addr_out);
	CRYPTO_WRITE(rk_dev, RK_CRYPTO_CTRL, RK_CRYPTO_BLOCK_START |
		     _SBF(RK_CRYPTO_BLOCK_START, 16));
}

static int rk_set_data_start(struct rk_crypto_dev *rk_dev)
{
	int err;
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 ivsize = crypto_ablkcipher_ivsize(tfm);
	u8 *src_last_blk = page_address(sg_page(alg_ctx->sg_src)) +
		alg_ctx->sg_src->offset + alg_ctx->sg_src->length - ivsize;

	/* Store the iv that need to be updated in chain mode.
	 * And update the IV buffer to contain the next IV for decryption mode.
	 */
	if (ctx->mode & RK_CRYPTO_DEC) {
		memcpy(ctx->iv, src_last_blk, ivsize);
		sg_pcopy_to_buffer(alg_ctx->req_src, alg_ctx->src_nents,
				   req->info, ivsize, alg_ctx->total - ivsize);
	}

	err = rk_dev->load_data(rk_dev, alg_ctx->sg_src, alg_ctx->sg_dst);
	if (!err)
		crypto_dma_start(rk_dev);
	return err;
}

static int rk_ablk_start(struct rk_crypto_dev *rk_dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	unsigned long flags;
	int err = 0;

	alg_ctx->left_bytes = req->nbytes;
	alg_ctx->total      = req->nbytes;
	alg_ctx->sg_src     = req->src;
	alg_ctx->req_src    = req->src;
	alg_ctx->src_nents  = sg_nents_for_len(req->src, req->nbytes);
	alg_ctx->sg_dst     = req->dst;
	alg_ctx->req_dst    = req->dst;
	alg_ctx->dst_nents  = sg_nents_for_len(req->dst, req->nbytes);

	spin_lock_irqsave(&rk_dev->lock, flags);
	rk_ablk_hw_init(rk_dev);
	err = rk_set_data_start(rk_dev);
	spin_unlock_irqrestore(&rk_dev->lock, flags);
	return err;
}

static void rk_iv_copyback(struct rk_crypto_dev *rk_dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 ivsize = crypto_ablkcipher_ivsize(tfm);

	/* Update the IV buffer to contain the next IV for encryption mode. */
	if (!(ctx->mode & RK_CRYPTO_DEC) && req->info) {
		if (alg_ctx->aligned) {
			memcpy(req->info, sg_virt(alg_ctx->sg_dst) +
				alg_ctx->sg_dst->length - ivsize, ivsize);
		} else {
			memcpy(req->info, rk_dev->addr_vir +
				alg_ctx->count - ivsize, ivsize);
		}
	}
}

static void rk_update_iv(struct rk_crypto_dev *rk_dev)
{
	struct ablkcipher_request *req =
		ablkcipher_request_cast(rk_dev->async_req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev);
	u32 ivsize = crypto_ablkcipher_ivsize(tfm);
	u8 *new_iv = NULL;

	if (ctx->mode & RK_CRYPTO_DEC) {
		new_iv = ctx->iv;
	} else {
		new_iv = page_address(sg_page(alg_ctx->sg_dst)) +
			 alg_ctx->sg_dst->offset +
			 alg_ctx->sg_dst->length - ivsize;
	}

	if (ivsize == DES_BLOCK_SIZE)
		memcpy_toio(rk_dev->reg + RK_CRYPTO_TDES_IV_0, new_iv, ivsize);
	else if (ivsize == AES_BLOCK_SIZE)
		memcpy_toio(rk_dev->reg + RK_CRYPTO_AES_IV_0, new_iv, ivsize);
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
		rk_update_iv(rk_dev);
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
		/* here show the calculation is over without any err */
		alg_ctx->ops.complete(rk_dev->async_req, 0);
		tasklet_schedule(&rk_dev->queue_task);
	}
out_rx:
	return err;
}

static int rk_ablk_cra_init(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;
	struct rk_crypto_algt *algt;
	struct rk_crypto_dev *rk_dev;

	algt = container_of(alg, struct rk_crypto_algt, alg.crypto);
	rk_dev = algt->rk_dev;

	memset(ctx, 0x00, sizeof(*ctx));

	if (!rk_dev->request_crypto)
		return -EFAULT;

	rk_dev->request_crypto(rk_dev, crypto_tfm_alg_name(tfm));

	alg_ctx->align_size     = crypto_tfm_alg_alignmask(tfm) + 1;

	alg_ctx->ops.start      = rk_ablk_start;
	alg_ctx->ops.update     = rk_ablk_rx;
	alg_ctx->ops.complete   = rk_crypto_complete;
	alg_ctx->ops.irq_handle = rk_crypto_irq_handle;

	ctx->rk_dev = rk_dev;

	return 0;
}

static void rk_ablk_cra_exit(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rk_dev->release_crypto(ctx->rk_dev, crypto_tfm_alg_name(tfm));
}

int rk_hw_crypto_v1_init(struct device *dev, void *hw_info)
{
	return 0;
}

void rk_hw_crypto_v1_deinit(struct device *dev, void *hw_info)
{

}

struct rk_crypto_algt rk_v1_ecb_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, ECB, ecb(aes), ecb-aes-rk);

struct rk_crypto_algt rk_v1_cbc_aes_alg =
	RK_CIPHER_ALGO_INIT(AES, CBC, cbc(aes), cbc-aes-rk);

struct rk_crypto_algt rk_v1_ecb_des_alg =
	RK_CIPHER_ALGO_INIT(DES, ECB, ecb(des), ecb-des-rk);

struct rk_crypto_algt rk_v1_cbc_des_alg =
	RK_CIPHER_ALGO_INIT(DES, CBC, cbc(des), cbc-des-rk);

struct rk_crypto_algt rk_v1_ecb_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, ECB, ecb(des3_ede), ecb-des3_ede-rk);

struct rk_crypto_algt rk_v1_cbc_des3_ede_alg =
	RK_CIPHER_ALGO_INIT(DES3_EDE, CBC, cbc(des3_ede), cbc-des3_ede-rk);
