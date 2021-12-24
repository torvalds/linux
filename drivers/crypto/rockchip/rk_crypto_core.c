// SPDX-License-Identifier: GPL-2.0
/*
 * Crypto acceleration support for Rockchip crypto
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 * Mender: Lin Jinhan <troy.lin@rock-chips.com>
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "rk_crypto_core.h"
#include "rk_crypto_v1.h"
#include "rk_crypto_v2.h"
#include "cryptodev_linux/rk_cryptodev_int.h"

#define RK_CRYPTO_V1_SOC_DATA_INIT(names) {\
	.use_soft_aes192 = false,\
	.valid_algs_name = (names),\
	.valid_algs_num = ARRAY_SIZE(names),\
	.total_algs = crypto_v1_algs,\
	.total_algs_num = ARRAY_SIZE(crypto_v1_algs),\
	.rsts = crypto_v1_rsts,\
	.rsts_num = ARRAY_SIZE(crypto_v1_rsts),\
	.hw_init = rk_hw_crypto_v1_init,\
	.hw_deinit = rk_hw_crypto_v1_deinit,\
	.hw_info_size = sizeof(struct rk_hw_crypto_v1_info),\
	.default_pka_offset = 0,\
}

#define RK_CRYPTO_V2_SOC_DATA_INIT(names, soft_aes_192) {\
	.use_soft_aes192 = soft_aes_192,\
	.valid_algs_name = (names),\
	.valid_algs_num = ARRAY_SIZE(names),\
	.total_algs = crypto_v2_algs,\
	.total_algs_num = ARRAY_SIZE(crypto_v2_algs),\
	.rsts = crypto_v2_rsts,\
	.rsts_num = ARRAY_SIZE(crypto_v2_rsts),\
	.hw_init = rk_hw_crypto_v2_init,\
	.hw_deinit = rk_hw_crypto_v2_deinit,\
	.hw_info_size = sizeof(struct rk_hw_crypto_v2_info),\
	.default_pka_offset = 0x0480,\
}

static struct rk_alg_ctx *rk_alg_ctx_cast(struct crypto_async_request *async_req)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(async_req->tfm);

	return &ctx->algs_ctx;
}

static int rk_crypto_enable_clk(struct rk_crypto_dev *rk_dev)
{
	int ret;

	dev_dbg(rk_dev->dev, "clk_bulk_prepare_enable.\n");

	ret = clk_bulk_prepare_enable(rk_dev->clks_num,
				      rk_dev->clk_bulks);
	if (ret < 0)
		dev_err(rk_dev->dev, "failed to enable clks %d\n", ret);

	return ret;
}

static void rk_crypto_disable_clk(struct rk_crypto_dev *rk_dev)
{
	dev_dbg(rk_dev->dev, "clk_bulk_disable_unprepare.\n");

	clk_bulk_disable_unprepare(rk_dev->clks_num, rk_dev->clk_bulks);
}

static int check_scatter_align(struct scatterlist *sg_src,
			       struct scatterlist *sg_dst,
			       int align_mask)
{
	int in, out, align;

	in = IS_ALIGNED((u32)sg_src->offset, 4) &&
	     IS_ALIGNED((u32)sg_src->length, align_mask);
	if (!sg_dst)
		return in;
	out = IS_ALIGNED((u32)sg_dst->offset, 4) &&
	      IS_ALIGNED((u32)sg_dst->length, align_mask);
	align = in && out;

	return (align && (sg_src->length == sg_dst->length));
}

static bool check_from_dmafd(struct crypto_async_request *async_req)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(async_req);

	if (alg_ctx->src_nents == 1 &&
	    sg_virt(alg_ctx->req_src) &&
	    sg_dma_address(alg_ctx->req_src) &&
	    alg_ctx->dst_nents == 1 &&
	    sg_virt(alg_ctx->req_dst) &&
	    sg_dma_address(alg_ctx->req_dst) &&
	    sg_dma_len(alg_ctx->req_src) == sg_dma_len(alg_ctx->req_dst))
		return true;

	return false;
}

static bool check_scatterlist_align(struct crypto_async_request *async_req,
				    int align_mask)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(async_req);
	struct scatterlist *src_tmp = NULL;
	struct scatterlist *dst_tmp = NULL;
	unsigned int i;

	if (alg_ctx->src_nents != alg_ctx->dst_nents)
		return false;

	src_tmp = alg_ctx->req_src;
	dst_tmp = alg_ctx->req_dst;

	for (i = 0; i < alg_ctx->src_nents; i++) {
		if (!src_tmp || !dst_tmp)
			return false;

		if (!check_scatter_align(src_tmp, dst_tmp, align_mask))
			return false;

		src_tmp = sg_next(src_tmp);
		dst_tmp = sg_next(dst_tmp);
	}

	return true;
}

static int rk_load_data(struct rk_crypto_dev *rk_dev,
			struct scatterlist *sg_src,
			struct scatterlist *sg_dst)
{
	int ret = -EINVAL;
	unsigned int count;
	struct device *dev = rk_dev->dev;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	if (alg_ctx->left_bytes == alg_ctx->total) {
		alg_ctx->is_dma = check_from_dmafd(rk_dev->async_req);
		alg_ctx->aligned = check_scatterlist_align(rk_dev->async_req,
							   alg_ctx->align_size) | alg_ctx->is_dma;
	}

	CRYPTO_TRACE("aligned = %d, total = %u, left_bytes = %u\n",
		     alg_ctx->aligned, alg_ctx->total, alg_ctx->left_bytes);

	if (alg_ctx->aligned) {
		count = min_t(unsigned int, alg_ctx->left_bytes,
			     sg_src->length);
		alg_ctx->left_bytes -= count;

		if (!dma_map_sg(dev, sg_src, 1, DMA_TO_DEVICE)) {
			dev_err(dev, "[%s:%d] dma_map_sg(src)  error\n",
				__func__, __LINE__);
			ret = -EINVAL;
			goto error;
		}
		alg_ctx->addr_in = sg_dma_address(sg_src);

		if (sg_dst) {
			if (!alg_ctx->is_dma && !dma_map_sg(dev, sg_dst, 1, DMA_FROM_DEVICE)) {
				dev_err(dev,
					"[%s:%d] dma_map_sg(dst)  error\n",
					__func__, __LINE__);
				dma_unmap_sg(dev, sg_src, 1,
					     DMA_TO_DEVICE);
				ret = -EINVAL;
				goto error;
			}
			alg_ctx->addr_out = sg_dma_address(sg_dst);
		}
	} else {
		count = (alg_ctx->left_bytes > rk_dev->vir_max) ?
			rk_dev->vir_max : alg_ctx->left_bytes;

		if (!sg_pcopy_to_buffer(alg_ctx->req_src, alg_ctx->src_nents,
					rk_dev->addr_vir, count,
					alg_ctx->total - alg_ctx->left_bytes)) {
			dev_err(dev, "[%s:%d] pcopy err\n",
				__func__, __LINE__);
			ret = -EINVAL;
			goto error;
		}
		alg_ctx->left_bytes -= count;
		sg_init_one(&alg_ctx->sg_tmp, rk_dev->addr_vir, count);
		if (!dma_map_sg(dev, &alg_ctx->sg_tmp, 1, DMA_TO_DEVICE)) {
			dev_err(dev, "[%s:%d] dma_map_sg(sg_tmp)  error\n",
				__func__, __LINE__);
			ret = -ENOMEM;
			goto error;
		}
		alg_ctx->addr_in = sg_dma_address(&alg_ctx->sg_tmp);

		if (sg_dst) {
			if (!dma_map_sg(dev, &alg_ctx->sg_tmp, 1,
					DMA_FROM_DEVICE)) {
				dev_err(dev,
					"[%s:%d] dma_map_sg(sg_tmp)  error\n",
					__func__, __LINE__);
				dma_unmap_sg(dev, &alg_ctx->sg_tmp, 1,
					     DMA_TO_DEVICE);
				ret = -ENOMEM;
				goto error;
			}
			alg_ctx->addr_out = sg_dma_address(&alg_ctx->sg_tmp);
		}
	}

	alg_ctx->count = count;
	return 0;
error:
	return ret;
}

static int rk_unload_data(struct rk_crypto_dev *rk_dev)
{
	int ret = 0;
	struct scatterlist *sg_in, *sg_out;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	CRYPTO_TRACE("aligned = %d, total = %u, left_bytes = %u\n",
		     alg_ctx->aligned, alg_ctx->total, alg_ctx->left_bytes);

	if (alg_ctx->count == 0)
		return 0;

	sg_in = alg_ctx->aligned ? alg_ctx->sg_src : &alg_ctx->sg_tmp;
	if (!alg_ctx->is_dma)
		dma_unmap_sg(rk_dev->dev, sg_in, 1, DMA_TO_DEVICE);

	if (alg_ctx->sg_dst) {
		sg_out = alg_ctx->aligned ? alg_ctx->sg_dst : &alg_ctx->sg_tmp;
		if (!alg_ctx->is_dma)
			dma_unmap_sg(rk_dev->dev, sg_out, 1, DMA_FROM_DEVICE);
	}

	if (!alg_ctx->aligned && alg_ctx->req_dst) {
		if (!sg_pcopy_from_buffer(alg_ctx->req_dst, alg_ctx->dst_nents,
					  rk_dev->addr_vir, alg_ctx->count,
					  alg_ctx->total - alg_ctx->left_bytes -
					  alg_ctx->count)) {
			ret = -EINVAL;
			goto exit;
		}
	}

exit:
	return ret;
}

static void start_irq_timer(struct rk_crypto_dev *rk_dev)
{
	mod_timer(&rk_dev->timer, jiffies + msecs_to_jiffies(3000));
}

/* use timer to avoid crypto irq timeout */
static void rk_crypto_irq_timer_handle(struct timer_list *t)
{
	struct rk_crypto_dev *rk_dev = from_timer(rk_dev, t, timer);

	rk_dev->err = -ETIMEDOUT;
	tasklet_schedule(&rk_dev->done_task);
}

static irqreturn_t rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	spin_lock(&rk_dev->lock);

	if (alg_ctx->ops.irq_handle)
		alg_ctx->ops.irq_handle(irq, dev_id);

	tasklet_schedule(&rk_dev->done_task);

	spin_unlock(&rk_dev->lock);
	return IRQ_HANDLED;
}

static int rk_start_op(struct rk_crypto_dev *rk_dev)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);
	int ret;

	if (!alg_ctx || !alg_ctx->ops.start)
		return -EINVAL;

	alg_ctx->aligned = false;

	start_irq_timer(rk_dev);

	ret = alg_ctx->ops.start(rk_dev);
	if (ret)
		return ret;

	return 0;
}

static int rk_update_op(struct rk_crypto_dev *rk_dev)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	if (!alg_ctx || !alg_ctx->ops.update)
		return -EINVAL;

	return alg_ctx->ops.update(rk_dev);
}

static void rk_complete_op(struct rk_crypto_dev *rk_dev, int err)
{
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	del_timer(&rk_dev->timer);

	if (!alg_ctx || !alg_ctx->ops.complete)
		return;

	alg_ctx->ops.complete(rk_dev->async_req, err);

	tasklet_schedule(&rk_dev->queue_task);
}

static int rk_crypto_enqueue(struct rk_crypto_dev *rk_dev,
			     struct crypto_async_request *async_req)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rk_dev->lock, flags);
	ret = crypto_enqueue_request(&rk_dev->queue, async_req);
	if (rk_dev->busy) {
		spin_unlock_irqrestore(&rk_dev->lock, flags);
		return ret;
	}
	rk_dev->busy = true;
	spin_unlock_irqrestore(&rk_dev->lock, flags);
	tasklet_schedule(&rk_dev->queue_task);

	return ret;
}

static void rk_crypto_queue_task_cb(unsigned long data)
{
	struct rk_crypto_dev *rk_dev = (struct rk_crypto_dev *)data;
	struct crypto_async_request *async_req, *backlog;
	unsigned long flags;

	rk_dev->err = 0;
	spin_lock_irqsave(&rk_dev->lock, flags);
	backlog   = crypto_get_backlog(&rk_dev->queue);
	async_req = crypto_dequeue_request(&rk_dev->queue);

	if (!async_req) {
		rk_dev->busy = false;
		spin_unlock_irqrestore(&rk_dev->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&rk_dev->lock, flags);

	if (backlog) {
		backlog->complete(backlog, -EINPROGRESS);
		backlog = NULL;
	}

	rk_dev->async_req = async_req;
	rk_dev->err = rk_start_op(rk_dev);
	if (rk_dev->err)
		rk_complete_op(rk_dev, rk_dev->err);
}

static void rk_crypto_done_task_cb(unsigned long data)
{
	struct rk_crypto_dev *rk_dev = (struct rk_crypto_dev *)data;
	struct rk_alg_ctx *alg_ctx = rk_alg_ctx_cast(rk_dev->async_req);

	if (rk_dev->err)
		goto exit;

	rk_dev->err = rk_update_op(rk_dev);
	if (rk_dev->err)
		goto exit;

	if (alg_ctx->total && alg_ctx->left_bytes == 0)
		goto exit;

	return;
exit:
	rk_complete_op(rk_dev, rk_dev->err);
}

static struct rk_crypto_algt *rk_crypto_find_algs(struct rk_crypto_dev *rk_dev,
						  char *name)
{
	u32 i;
	struct rk_crypto_algt **algs;
	struct rk_crypto_algt *tmp_algs;

	algs = rk_dev->soc_data->total_algs;

	for (i = 0; i < rk_dev->soc_data->total_algs_num; i++, algs++) {
		tmp_algs = *algs;
		tmp_algs->rk_dev = rk_dev;

		if (strcmp(tmp_algs->name, name) == 0)
			return tmp_algs;
	}

	return NULL;
}

static int rk_crypto_register(struct rk_crypto_dev *rk_dev)
{
	unsigned int i, k;
	char **algs_name;
	struct rk_crypto_algt *tmp_algs;
	struct rk_crypto_soc_data *soc_data;
	int err = 0;

	soc_data = rk_dev->soc_data;

	algs_name = soc_data->valid_algs_name;

	for (i = 0; i < soc_data->valid_algs_num; i++, algs_name++) {
		tmp_algs = rk_crypto_find_algs(rk_dev, *algs_name);
		if (!tmp_algs) {
			CRYPTO_TRACE("%s not matched!!!\n", *algs_name);
			continue;
		}

		CRYPTO_TRACE("%s matched!!!\n", *algs_name);

		tmp_algs->rk_dev = rk_dev;

		if (tmp_algs->type == ALG_TYPE_CIPHER) {
			if (tmp_algs->mode == CIPHER_MODE_CTR ||
			    tmp_algs->mode == CIPHER_MODE_CFB ||
			    tmp_algs->mode == CIPHER_MODE_OFB)
				tmp_algs->alg.crypto.base.cra_blocksize = 1;

			if (tmp_algs->mode == CIPHER_MODE_ECB)
				tmp_algs->alg.crypto.ivsize = 0;

			/* rv1126 is not support aes192 */
			if (soc_data->use_soft_aes192 &&
			    tmp_algs->algo == CIPHER_ALGO_AES)
				tmp_algs->use_soft_aes192 = true;

			err = crypto_register_skcipher(&tmp_algs->alg.crypto);
		} else if (tmp_algs->type == ALG_TYPE_HASH || tmp_algs->type == ALG_TYPE_HMAC) {
			err = crypto_register_ahash(&tmp_algs->alg.hash);
		} else if (tmp_algs->type == ALG_TYPE_ASYM) {
			err = crypto_register_akcipher(&tmp_algs->alg.asym);
		} else {
			continue;
		}

		if (err)
			goto err_cipher_algs;

		CRYPTO_TRACE("%s register OK!!!\n", *algs_name);
	}
	return 0;

err_cipher_algs:
	algs_name = soc_data->valid_algs_name;

	for (k = 0; k < i; k++, algs_name++) {
		tmp_algs = rk_crypto_find_algs(rk_dev, *algs_name);
		if (tmp_algs->type == ALG_TYPE_CIPHER)
			crypto_unregister_skcipher(&tmp_algs->alg.crypto);
		else if (tmp_algs->type == ALG_TYPE_HASH || tmp_algs->type == ALG_TYPE_HMAC)
			crypto_unregister_ahash(&tmp_algs->alg.hash);
		else if (tmp_algs->type == ALG_TYPE_ASYM)
			crypto_unregister_akcipher(&tmp_algs->alg.asym);
	}
	return err;
}

static void rk_crypto_unregister(struct rk_crypto_dev *rk_dev)
{
	unsigned int i;
	char **algs_name;
	struct rk_crypto_algt *tmp_algs;

	algs_name = rk_dev->soc_data->valid_algs_name;

	for (i = 0; i < rk_dev->soc_data->valid_algs_num; i++, algs_name++) {
		tmp_algs = rk_crypto_find_algs(rk_dev, *algs_name);
		if (tmp_algs->type == ALG_TYPE_CIPHER)
			crypto_unregister_skcipher(&tmp_algs->alg.crypto);
		else if (tmp_algs->type == ALG_TYPE_HASH || tmp_algs->type == ALG_TYPE_HMAC)
			crypto_unregister_ahash(&tmp_algs->alg.hash);
		else if (tmp_algs->type == ALG_TYPE_ASYM)
			crypto_unregister_akcipher(&tmp_algs->alg.asym);
	}
}

static void rk_crypto_request(struct rk_crypto_dev *rk_dev, const char *name)
{
	CRYPTO_TRACE("Crypto is requested by %s\n", name);

	rk_crypto_enable_clk(rk_dev);
}

static void rk_crypto_release(struct rk_crypto_dev *rk_dev, const char *name)
{
	CRYPTO_TRACE("Crypto is released by %s\n", name);

	rk_crypto_disable_clk(rk_dev);
}

static void rk_crypto_action(void *data)
{
	struct rk_crypto_dev *rk_dev = data;

	if (rk_dev->rst)
		reset_control_assert(rk_dev->rst);
}

static const char * const crypto_v2_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_algt *crypto_v2_algs[] = {
	&rk_v2_ecb_sm4_alg,		/* ecb(sm4) */
	&rk_v2_cbc_sm4_alg,		/* cbc(sm4) */
	&rk_v2_xts_sm4_alg,		/* xts(sm4) */
	&rk_v2_cfb_sm4_alg,		/* cfb(sm4) */
	&rk_v2_ofb_sm4_alg,		/* ofb(sm4) */
	&rk_v2_ctr_sm4_alg,		/* ctr(sm4) */

	&rk_v2_ecb_aes_alg,		/* ecb(aes) */
	&rk_v2_cbc_aes_alg,		/* cbc(aes) */
	&rk_v2_xts_aes_alg,		/* xts(aes) */
	&rk_v2_cfb_aes_alg,		/* cfb(aes) */
	&rk_v2_ofb_aes_alg,		/* ofb(aes) */
	&rk_v2_ctr_aes_alg,		/* ctr(aes) */

	&rk_v2_ecb_des_alg,		/* ecb(des) */
	&rk_v2_cbc_des_alg,		/* cbc(des) */
	&rk_v2_cfb_des_alg,		/* cfb(des) */
	&rk_v2_ofb_des_alg,		/* ofb(des) */

	&rk_v2_ecb_des3_ede_alg,	/* ecb(des3_ede) */
	&rk_v2_cbc_des3_ede_alg,	/* cbc(des3_ede) */
	&rk_v2_cfb_des3_ede_alg,	/* cfb(des3_ede) */
	&rk_v2_ofb_des3_ede_alg,	/* ofb(des3_ede) */

	&rk_v2_ahash_sha1,		/* sha1 */
	&rk_v2_ahash_sha256,		/* sha256 */
	&rk_v2_ahash_sha512,		/* sha512 */
	&rk_v2_ahash_md5,		/* md5 */
	&rk_v2_ahash_sm3,		/* sm3 */

	&rk_v2_hmac_sha1,		/* hmac(sha1) */
	&rk_v2_hmac_sha256,		/* hmac(sha256) */
	&rk_v2_hmac_sha512,		/* hmac(sha512) */
	&rk_v2_hmac_md5,		/* hmac(md5) */
	&rk_v2_hmac_sm3,		/* hmac(sm3) */

	&rk_v2_asym_rsa,		/* rsa */
};

static char *px30_algs_name[] = {
	"ecb(aes)", "cbc(aes)", "xts(aes)",
	"ecb(des)", "cbc(des)",
	"ecb(des3_ede)", "cbc(des3_ede)",
	"sha1", "sha256", "sha512", "md5",
};

static char *crypto_full_algs_name[] = {
	"ecb(sm4)", "cbc(sm4)", "cfb(sm4)", "ofb(sm4)", "ctr(sm4)", "xts(sm4)",
	"ecb(aes)", "cbc(aes)", "cfb(aes)", "ctr(aes)", "xts(aes)",
	"ecb(des)", "cbc(des)", "cfb(des)", "ofb(des)",
	"ecb(des3_ede)", "cbc(des3_ede)", "cfb(des3_ede)", "ofb(des3_ede)",
	"sha1", "sha256", "sha512", "md5", "sm3",
	"hmac(sha1)", "hmac(sha256)", "hmac(sha512)", "hmac(md5)", "hmac(sm3)",
	"rsa"
};

static const struct rk_crypto_soc_data px30_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(px30_algs_name, false);

static const struct rk_crypto_soc_data rv1126_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(crypto_full_algs_name, true);

static const struct rk_crypto_soc_data full_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(crypto_full_algs_name, false);

static const char * const crypto_v1_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_algt *crypto_v1_algs[] = {
	&rk_v1_ecb_aes_alg,		/* ecb(aes) */
	&rk_v1_cbc_aes_alg,		/* cbc(aes) */

	&rk_v1_ecb_des_alg,		/* ecb(des) */
	&rk_v1_cbc_des_alg,		/* cbc(des) */

	&rk_v1_ecb_des3_ede_alg,	/* ecb(des3_ede) */
	&rk_v1_cbc_des3_ede_alg,	/* cbc(des3_ede) */

	&rk_v1_ahash_sha1,		/* sha1 */
	&rk_v1_ahash_sha256,		/* sha256 */
	&rk_v1_ahash_md5,		/* md5 */
};

static char *rk3288_cipher_algs[] = {
	"ecb(aes)", "cbc(aes)",
	"ecb(des)", "cbc(des)",
	"ecb(des3_ede)", "cbc(des3_ede)",
	"sha1", "sha256", "md5",
};

static const struct rk_crypto_soc_data rk3288_soc_data =
	RK_CRYPTO_V1_SOC_DATA_INIT(rk3288_cipher_algs);

static const struct of_device_id crypto_of_id_table[] = {
	/* crypto v2 in belows */
	{
		.compatible = "rockchip,px30-crypto",
		.data = (void *)&px30_soc_data,
	},
	{
		.compatible = "rockchip,rv1126-crypto",
		.data = (void *)&rv1126_soc_data,
	},
	{
		.compatible = "rockchip,rk3568-crypto",
		.data = (void *)&full_soc_data,
	},
	{
		.compatible = "rockchip,rk3588-crypto",
		.data = (void *)&full_soc_data,
	},
	/* crypto v1 in belows */
	{
		.compatible = "rockchip,rk3288-crypto",
		.data = (void *)&rk3288_soc_data,
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, crypto_of_id_table);

static int rk_crypto_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct rk_crypto_soc_data *soc_data;
	const struct of_device_id *match;
	struct rk_crypto_dev *rk_dev;
	int err = 0;

	rk_dev = devm_kzalloc(&pdev->dev,
				   sizeof(*rk_dev), GFP_KERNEL);
	if (!rk_dev) {
		err = -ENOMEM;
		goto err_crypto;
	}

	match = of_match_node(crypto_of_id_table, np);
	soc_data = (struct rk_crypto_soc_data *)match->data;
	rk_dev->soc_data = soc_data;

	if (soc_data->rsts[0]) {
		rk_dev->rst =
			devm_reset_control_get(dev, soc_data->rsts[0]);
		if (IS_ERR(rk_dev->rst)) {
			err = PTR_ERR(rk_dev->rst);
			goto err_crypto;
		}
		reset_control_assert(rk_dev->rst);
		usleep_range(10, 20);
		reset_control_deassert(rk_dev->rst);
	}

	err = devm_add_action_or_reset(dev, rk_crypto_action, rk_dev);
	if (err)
		goto err_crypto;

	spin_lock_init(&rk_dev->lock);

	/* get crypto base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk_dev->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(rk_dev->reg)) {
		err = PTR_ERR(rk_dev->reg);
		goto err_crypto;
	}

	/* get pka base, if pka reg not set, pka reg = crypto + pka offset */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rk_dev->pka_reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(rk_dev->pka_reg))
		rk_dev->pka_reg = rk_dev->reg + soc_data->default_pka_offset;

	rk_dev->clks_num = devm_clk_bulk_get_all(dev, &rk_dev->clk_bulks);
	if (rk_dev->clks_num < 0) {
		err = rk_dev->clks_num;
		dev_err(dev, "failed to get clks property\n");
		goto err_crypto;
	}

	rk_dev->irq = platform_get_irq(pdev, 0);
	if (rk_dev->irq < 0) {
		dev_warn(dev,
			 "control Interrupt is not available.\n");
		err = rk_dev->irq;
		goto err_crypto;
	}

	err = devm_request_irq(dev, rk_dev->irq,
			       rk_crypto_irq_handle, IRQF_SHARED,
			       "rk-crypto", pdev);
	if (err) {
		dev_err(dev, "irq request failed.\n");
		goto err_crypto;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(dev, "crypto: No suitable DMA available.\n");
		goto err_crypto;
	}

	rk_dev->dev = dev;

	rk_dev->hw_info =
		devm_kzalloc(dev, soc_data->hw_info_size, GFP_KERNEL);
	if (!rk_dev->hw_info) {
		err = -ENOMEM;
		goto err_crypto;
	}

	err = soc_data->hw_init(dev, rk_dev->hw_info);
	if (err) {
		dev_err(dev, "hw_init failed.\n");
		goto err_crypto;
	}

	rk_dev->addr_vir = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA32,
						    RK_BUFFER_ORDER);
	if (!rk_dev->addr_vir) {
		err = -ENOMEM;
		dev_err(dev, "__get_free_page failed.\n");
		goto err_crypto;
	}

	rk_dev->vir_max = RK_BUFFER_SIZE;

	platform_set_drvdata(pdev, rk_dev);

	tasklet_init(&rk_dev->queue_task,
		     rk_crypto_queue_task_cb, (unsigned long)rk_dev);
	tasklet_init(&rk_dev->done_task,
		     rk_crypto_done_task_cb, (unsigned long)rk_dev);
	crypto_init_queue(&rk_dev->queue, 50);

	timer_setup(&rk_dev->timer, rk_crypto_irq_timer_handle, 0);

	rk_dev->request_crypto = rk_crypto_request;
	rk_dev->release_crypto = rk_crypto_release;
	rk_dev->load_data = rk_load_data;
	rk_dev->unload_data = rk_unload_data;
	rk_dev->enqueue = rk_crypto_enqueue;
	rk_dev->busy = false;

	err = rk_crypto_register(rk_dev);
	if (err) {
		dev_err(dev, "err in register alg");
		goto err_register_alg;
	}

	rk_cryptodev_register_dev(rk_dev->dev, "rk_crypto");

	dev_info(dev, "Crypto Accelerator successfully registered\n");
	return 0;

err_register_alg:
	tasklet_kill(&rk_dev->queue_task);
	tasklet_kill(&rk_dev->done_task);
err_crypto:
	return err;
}

static int rk_crypto_remove(struct platform_device *pdev)
{
	struct rk_crypto_dev *rk_dev = platform_get_drvdata(pdev);

	rk_cryptodev_unregister_dev(rk_dev->dev);

	del_timer_sync(&rk_dev->timer);

	rk_crypto_unregister(rk_dev);
	tasklet_kill(&rk_dev->done_task);
	tasklet_kill(&rk_dev->queue_task);

	if (rk_dev->addr_vir)
		free_pages((unsigned long)rk_dev->addr_vir, RK_BUFFER_ORDER);

	rk_dev->soc_data->hw_deinit(&pdev->dev, rk_dev->hw_info);

	return 0;
}

static struct platform_driver crypto_driver = {
	.probe		= rk_crypto_probe,
	.remove		= rk_crypto_remove,
	.driver		= {
		.name	= "rk-crypto",
		.of_match_table	= crypto_of_id_table,
	},
};

module_platform_driver(crypto_driver);

MODULE_AUTHOR("Lin Jinhan <troy.lin@rock-chips.com>");
MODULE_DESCRIPTION("Support for Rockchip's cryptographic engine");
MODULE_LICENSE("GPL");
