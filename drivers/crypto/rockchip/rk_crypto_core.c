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

static void dump_alg_ctx(struct crypto_async_request *async_req)
{
	struct rk_alg_ctx *alg_ctx = crypto_tfm_ctx(async_req->tfm);
	struct scatterlist *cur_sg = NULL;
	unsigned int i;

	CRYPTO_TRACE("\n");

	CRYPTO_TRACE("------ req_src addr = %llx, nents = %zu ------",
		     (long long)alg_ctx->req_src, alg_ctx->src_nents);

	for_each_sg(alg_ctx->req_src, cur_sg, alg_ctx->src_nents, i)
		CRYPTO_TRACE("sg %llx: virt = %llx, off = %u, len = %u",
			     (long long)cur_sg, (long long)sg_virt(cur_sg),
			     cur_sg->offset, cur_sg->length);

	CRYPTO_TRACE("\n");

	if (!alg_ctx->req_dst)
		return;

	CRYPTO_TRACE("------ req_dst addr = %llx, nents = %zu ------",
		     (long long)alg_ctx->req_dst, alg_ctx->dst_nents);

	for_each_sg(alg_ctx->req_dst, cur_sg, alg_ctx->dst_nents, i)
		CRYPTO_TRACE("sg %llx: virt = %llx, off = %u, len = %u\n",
			     (long long)cur_sg, (long long)sg_virt(cur_sg),
			     cur_sg->offset, cur_sg->length);
	CRYPTO_TRACE("\n");
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

static int check_alignment(struct scatterlist *sg_src,
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

static int rk_load_data(struct rk_crypto_dev *rk_dev,
			struct scatterlist *sg_src,
			struct scatterlist *sg_dst)
{
	int ret = -EINVAL;
	unsigned int count;
	struct device *dev = rk_dev->dev;
	struct rk_alg_ctx *alg_ctx = crypto_tfm_ctx(rk_dev->async_req->tfm);

	mutex_lock(&rk_dev->mutex);

	alg_ctx->aligned = check_alignment(sg_src, sg_dst, alg_ctx->align_size);

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
			if (!dma_map_sg(dev, sg_dst, 1, DMA_FROM_DEVICE)) {
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
		count = (alg_ctx->left_bytes > PAGE_SIZE) ?
			PAGE_SIZE : alg_ctx->left_bytes;

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
	mutex_unlock(&rk_dev->mutex);
	return ret;
}

static int rk_unload_data(struct rk_crypto_dev *rk_dev)
{
	int ret = 0;
	struct scatterlist *sg_in, *sg_out;
	struct rk_alg_ctx *alg_ctx = crypto_tfm_ctx(rk_dev->async_req->tfm);

	CRYPTO_TRACE("aligned = %d, total = %u, left_bytes = %u\n",
		     alg_ctx->aligned, alg_ctx->total, alg_ctx->left_bytes);

	sg_in = alg_ctx->aligned ? alg_ctx->sg_src : &alg_ctx->sg_tmp;
	dma_unmap_sg(rk_dev->dev, sg_in, 1, DMA_TO_DEVICE);

	if (alg_ctx->sg_dst) {
		sg_out = alg_ctx->aligned ? alg_ctx->sg_dst : &alg_ctx->sg_tmp;
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
	mutex_unlock(&rk_dev->mutex);
	return ret;
}

static irqreturn_t rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_dev *rk_dev  = platform_get_drvdata(dev_id);
	struct rk_alg_ctx *alg_ctx = crypto_tfm_ctx(rk_dev->async_req->tfm);

	spin_lock(&rk_dev->lock);

	if (alg_ctx->ops.irq_handle)
		alg_ctx->ops.irq_handle(irq, dev_id);

	tasklet_schedule(&rk_dev->done_task);

	spin_unlock(&rk_dev->lock);
	return IRQ_HANDLED;
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
	struct rk_alg_ctx *alg_ctx;
	unsigned long flags;
	int err = 0;

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

	alg_ctx = crypto_tfm_ctx(async_req->tfm);

	rk_dev->async_req = async_req;
	err = alg_ctx->ops.start(rk_dev);
	if (err)
		alg_ctx->ops.complete(rk_dev->async_req, err);

	dump_alg_ctx(async_req);
}

static void rk_crypto_done_task_cb(unsigned long data)
{
	struct rk_crypto_dev *rk_dev = (struct rk_crypto_dev *)data;
	struct rk_alg_ctx *alg_ctx = crypto_tfm_ctx(rk_dev->async_req->tfm);

	if (rk_dev->err) {
		alg_ctx->ops.complete(rk_dev->async_req, rk_dev->err);
		return;
	}

	rk_dev->err = alg_ctx->ops.update(rk_dev);
	if (rk_dev->err)
		alg_ctx->ops.complete(rk_dev->async_req, rk_dev->err);
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
			if (tmp_algs->algo == CIPHER_ALGO_AES &&
			    tmp_algs->mode != CIPHER_MODE_XTS &&
			    soc_data->use_soft_aes192)
				tmp_algs->alg.crypto.cra_flags |= CRYPTO_ALG_NEED_FALLBACK;

			err = crypto_register_alg(&tmp_algs->alg.crypto);
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
			crypto_unregister_alg(&tmp_algs->alg.crypto);
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
			crypto_unregister_alg(&tmp_algs->alg.crypto);
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

static char *rv1126_algs_name[] = {
	"ecb(sm4)", "cbc(sm4)", "cfb(sm4)", "ofb(sm4)", "ctr(sm4)", "xts(sm4)",
	"ecb(aes)", "cbc(aes)", "cfb(aes)", "ctr(aes)", "xts(aes)",
	"ecb(des)", "cbc(des)", "cfb(des)", "ofb(des)",
	"ecb(des3_ede)", "cbc(des3_ede)", "cfb(des3_ede)", "ofb(des3_ede)",
	"sha1", "sha256", "sha512", "md5", "sm3",
	"hmac(sha1)", "hmac(sha256)", "hmac(sha512)", "hmac(md5)", "hmac(sm3)",
	"rsa"
};

static char *rk3568_algs_name[] = {
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
	RK_CRYPTO_V2_SOC_DATA_INIT(rv1126_algs_name, true);

static const struct rk_crypto_soc_data rk3568_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(rk3568_algs_name, false);

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
		.data = (void *)&rk3568_soc_data,
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

	rk_dev->addr_vir = (char *)__get_free_page(GFP_KERNEL);
	if (!rk_dev->addr_vir) {
		err = -ENOMEM;
		dev_err(dev, "__get_free_page failed.\n");
		goto err_crypto;
	}

	platform_set_drvdata(pdev, rk_dev);

	tasklet_init(&rk_dev->queue_task,
		     rk_crypto_queue_task_cb, (unsigned long)rk_dev);
	tasklet_init(&rk_dev->done_task,
		     rk_crypto_done_task_cb, (unsigned long)rk_dev);
	crypto_init_queue(&rk_dev->queue, 50);

	mutex_init(&rk_dev->mutex);

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

	dev_info(dev, "Crypto Accelerator successfully registered\n");
	return 0;

err_register_alg:
	mutex_destroy(&rk_dev->mutex);
	tasklet_kill(&rk_dev->queue_task);
	tasklet_kill(&rk_dev->done_task);
err_crypto:
	return err;
}

static int rk_crypto_remove(struct platform_device *pdev)
{
	struct rk_crypto_dev *rk_dev = platform_get_drvdata(pdev);

	rk_crypto_unregister(rk_dev);
	tasklet_kill(&rk_dev->done_task);
	tasklet_kill(&rk_dev->queue_task);

	if (rk_dev->addr_vir)
		free_page((unsigned long)rk_dev->addr_vir);

	rk_dev->soc_data->hw_deinit(&pdev->dev, rk_dev->hw_info);

	mutex_destroy(&rk_dev->mutex);

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
