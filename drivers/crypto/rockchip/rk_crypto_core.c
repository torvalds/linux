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
	.clks = crypto_v1_clks,\
	.clks_num = ARRAY_SIZE(crypto_v1_clks),\
	.rsts = crypto_v1_rsts,\
	.rsts_num = ARRAY_SIZE(crypto_v1_rsts),\
	.hw_init = rk_hw_crypto_v1_init,\
	.hw_deinit = rk_hw_crypto_v1_deinit,\
	.hw_info_size = sizeof(struct rk_hw_crypto_v1_info),\
}

#define RK_CRYPTO_V2_SOC_DATA_INIT(names, soft_aes_192) {\
	.use_soft_aes192 = soft_aes_192,\
	.valid_algs_name = (names),\
	.valid_algs_num = ARRAY_SIZE(names),\
	.total_algs = crypto_v2_algs,\
	.total_algs_num = ARRAY_SIZE(crypto_v2_algs),\
	.clks = crypto_v2_clks,\
	.clks_num = ARRAY_SIZE(crypto_v2_clks),\
	.rsts = crypto_v2_rsts,\
	.rsts_num = ARRAY_SIZE(crypto_v2_rsts),\
	.hw_init = rk_hw_crypto_v2_init,\
	.hw_deinit = rk_hw_crypto_v2_deinit,\
	.hw_info_size = sizeof(struct rk_hw_crypto_v2_info),\
}

static int rk_crypto_enable_clk(struct rk_crypto_info *dev)
{
	int ret;

	dev_dbg(dev->dev, "clk_bulk_prepare_enable.\n");

	ret = clk_bulk_prepare_enable(dev->soc_data->clks_num,
				      &dev->clk_bulks[0]);
	if (ret < 0)
		dev_err(dev->dev, "failed to enable clks %d\n", ret);

	return ret;
}

static void rk_crypto_disable_clk(struct rk_crypto_info *dev)
{
	dev_dbg(dev->dev, "clk_bulk_disable_unprepare.\n");

	clk_bulk_disable_unprepare(dev->soc_data->clks_num, &dev->clk_bulks[0]);
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

static int rk_load_data(struct rk_crypto_info *dev,
			struct scatterlist *sg_src,
			struct scatterlist *sg_dst)
{
	unsigned int count;

	dev->aligned = dev->aligned ?
		check_alignment(sg_src, sg_dst, dev->align_size) :
		dev->aligned;
	if (dev->aligned) {
		count = min_t(unsigned int, dev->left_bytes, sg_src->length);
		dev->left_bytes -= count;

		if (!dma_map_sg(dev->dev, sg_src, 1, DMA_TO_DEVICE)) {
			dev_err(dev->dev, "[%s:%d] dma_map_sg(src)  error\n",
				__func__, __LINE__);
			return -EINVAL;
		}
		dev->addr_in = sg_dma_address(sg_src);

		if (sg_dst) {
			if (!dma_map_sg(dev->dev, sg_dst, 1, DMA_FROM_DEVICE)) {
				dev_err(dev->dev,
					"[%s:%d] dma_map_sg(dst)  error\n",
					__func__, __LINE__);
				dma_unmap_sg(dev->dev, sg_src, 1,
					     DMA_TO_DEVICE);
				return -EINVAL;
			}
			dev->addr_out = sg_dma_address(sg_dst);
		}
	} else {
		count = (dev->left_bytes > PAGE_SIZE) ?
			PAGE_SIZE : dev->left_bytes;

		if (!sg_pcopy_to_buffer(dev->first, dev->src_nents,
					dev->addr_vir, count,
					dev->total - dev->left_bytes)) {
			dev_err(dev->dev, "[%s:%d] pcopy err\n",
				__func__, __LINE__);
			return -EINVAL;
		}
		dev->left_bytes -= count;
		sg_init_one(&dev->sg_tmp, dev->addr_vir, count);
		if (!dma_map_sg(dev->dev, &dev->sg_tmp, 1, DMA_TO_DEVICE)) {
			dev_err(dev->dev, "[%s:%d] dma_map_sg(sg_tmp)  error\n",
				__func__, __LINE__);
			return -ENOMEM;
		}
		dev->addr_in = sg_dma_address(&dev->sg_tmp);

		if (sg_dst) {
			if (!dma_map_sg(dev->dev, &dev->sg_tmp, 1,
					DMA_FROM_DEVICE)) {
				dev_err(dev->dev,
					"[%s:%d] dma_map_sg(sg_tmp)  error\n",
					__func__, __LINE__);
				dma_unmap_sg(dev->dev, &dev->sg_tmp, 1,
					     DMA_TO_DEVICE);
				return -ENOMEM;
			}
			dev->addr_out = sg_dma_address(&dev->sg_tmp);
		}
	}
	dev->count = count;
	return 0;
}

static void rk_unload_data(struct rk_crypto_info *dev)
{
	struct scatterlist *sg_in, *sg_out;

	sg_in = dev->aligned ? dev->sg_src : &dev->sg_tmp;
	dma_unmap_sg(dev->dev, sg_in, 1, DMA_TO_DEVICE);

	if (dev->sg_dst) {
		sg_out = dev->aligned ? dev->sg_dst : &dev->sg_tmp;
		dma_unmap_sg(dev->dev, sg_out, 1, DMA_FROM_DEVICE);
	}
}

static irqreturn_t rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_info *dev  = platform_get_drvdata(dev_id);

	spin_lock(&dev->lock);

	if (dev->irq_handle)
		dev->irq_handle(irq, dev_id);

	tasklet_schedule(&dev->done_task);

	spin_unlock(&dev->lock);
	return IRQ_HANDLED;
}

static int rk_crypto_enqueue(struct rk_crypto_info *dev,
			      struct crypto_async_request *async_req)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	ret = crypto_enqueue_request(&dev->queue, async_req);
	if (dev->busy) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return ret;
	}
	dev->busy = true;
	spin_unlock_irqrestore(&dev->lock, flags);
	tasklet_schedule(&dev->queue_task);

	return ret;
}

static void rk_crypto_queue_task_cb(unsigned long data)
{
	struct rk_crypto_info *dev = (struct rk_crypto_info *)data;
	struct crypto_async_request *async_req, *backlog;
	unsigned long flags;
	int err = 0;

	dev->err = 0;
	spin_lock_irqsave(&dev->lock, flags);
	backlog   = crypto_get_backlog(&dev->queue);
	async_req = crypto_dequeue_request(&dev->queue);

	if (!async_req) {
		dev->busy = false;
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (backlog) {
		backlog->complete(backlog, -EINPROGRESS);
		backlog = NULL;
	}

	dev->async_req = async_req;
	err = dev->start(dev);
	if (err)
		dev->complete(dev->async_req, err);
}

static void rk_crypto_done_task_cb(unsigned long data)
{
	struct rk_crypto_info *dev = (struct rk_crypto_info *)data;

	if (dev->err) {
		dev->complete(dev->async_req, dev->err);
		return;
	}

	dev->err = dev->update(dev);
	if (dev->err)
		dev->complete(dev->async_req, dev->err);
}

static struct rk_crypto_tmp *rk_crypto_find_algs(struct rk_crypto_info *crypto_info, char *name)
{
	u32 i;
	struct rk_crypto_tmp **algs;
	struct rk_crypto_tmp *tmp_algs;

	algs = crypto_info->soc_data->total_algs;

	for (i = 0; i < crypto_info->soc_data->total_algs_num; i++, algs++) {
		tmp_algs = *algs;
		tmp_algs->dev = crypto_info;

		if (strcmp(tmp_algs->name, name) == 0)
			return tmp_algs;
	}

	return NULL;
}

static int rk_crypto_register(struct rk_crypto_info *crypto_info)
{
	unsigned int i, k;
	char **algs_name;
	struct rk_crypto_tmp *tmp_algs;
	struct rk_crypto_soc_data *soc_data;
	int err = 0;

	soc_data = crypto_info->soc_data;

	algs_name = soc_data->valid_algs_name;

	for (i = 0; i < soc_data->valid_algs_num; i++, algs_name++) {
		tmp_algs = rk_crypto_find_algs(crypto_info, *algs_name);
		if (!tmp_algs) {
			CRYPTO_TRACE("%s not matched!!!\n", *algs_name);
			continue;
		}

		CRYPTO_TRACE("%s matched!!!\n", *algs_name);

		tmp_algs->dev = crypto_info;

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
		tmp_algs = rk_crypto_find_algs(crypto_info, *algs_name);
		if (tmp_algs->type == ALG_TYPE_CIPHER)
			crypto_unregister_alg(&tmp_algs->alg.crypto);
		else if (tmp_algs->type == ALG_TYPE_HASH || tmp_algs->type == ALG_TYPE_HMAC)
			crypto_unregister_ahash(&tmp_algs->alg.hash);
		else if (tmp_algs->type == ALG_TYPE_ASYM)
			crypto_unregister_akcipher(&tmp_algs->alg.asym);
	}
	return err;
}

static void rk_crypto_unregister(struct rk_crypto_info *crypto_info)
{
	unsigned int i;
	char **algs_name;
	struct rk_crypto_tmp *tmp_algs;

	algs_name = crypto_info->soc_data->valid_algs_name;

	for (i = 0; i < crypto_info->soc_data->valid_algs_num; i++, algs_name++) {
		tmp_algs = rk_crypto_find_algs(crypto_info, *algs_name);
		if (tmp_algs->type == ALG_TYPE_CIPHER)
			crypto_unregister_alg(&tmp_algs->alg.crypto);
		else if (tmp_algs->type == ALG_TYPE_HASH || tmp_algs->type == ALG_TYPE_HMAC)
			crypto_unregister_ahash(&tmp_algs->alg.hash);
		else if (tmp_algs->type == ALG_TYPE_ASYM)
			crypto_unregister_akcipher(&tmp_algs->alg.asym);
	}
}

static void rk_crypto_request(struct rk_crypto_info *dev, const char *name)
{
	CRYPTO_TRACE("Crypto is requested by %s\n", name);

	mutex_lock(&dev->mutex);

	rk_crypto_enable_clk(dev);
}

static void rk_crypto_release(struct rk_crypto_info *dev, const char *name)
{
	CRYPTO_TRACE("Crypto is released by %s\n", name);

	rk_crypto_disable_clk(dev);

	mutex_unlock(&dev->mutex);
}

static void rk_crypto_action(void *data)
{
	struct rk_crypto_info *crypto_info = data;

	if (crypto_info->rst)
		reset_control_assert(crypto_info->rst);
}

static const char * const crypto_v2_clks[] = {
	"hclk",
	"aclk",
	"sclk",
	"apb_pclk",
};

static const char * const crypto_v2_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_tmp *crypto_v2_algs[] = {
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

	&rk_v2_ahash_hmac_sha1,		/* hmac(sha1) */
	&rk_v2_ahash_hmac_sha256,	/* hmac(sha256) */
	&rk_v2_ahash_hmac_sha512,	/* hmac(sha512) */
	&rk_v2_ahash_hmac_md5,		/* hmac(md5) */
	&rk_v2_ahash_hmac_sm3,		/* hmac(sm3) */

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

static const struct rk_crypto_soc_data px30_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(px30_algs_name, false);

static const struct rk_crypto_soc_data rv1126_soc_data =
	RK_CRYPTO_V2_SOC_DATA_INIT(rv1126_algs_name, true);

static const char * const crypto_v1_clks[] = {
	"hclk",
	"aclk",
	"sclk",
	"apb_pclk",
};

static const char * const crypto_v1_rsts[] = {
	"crypto-rst",
};

static struct rk_crypto_tmp *crypto_v1_algs[] = {
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
	const struct of_device_id *match;
	struct rk_crypto_info *crypto_info;
	int err = 0, i;

	crypto_info = devm_kzalloc(&pdev->dev,
				   sizeof(*crypto_info), GFP_KERNEL);
	if (!crypto_info) {
		err = -ENOMEM;
		goto err_crypto;
	}

	match = of_match_node(crypto_of_id_table, np);
	crypto_info->soc_data = (struct rk_crypto_soc_data *)match->data;

	crypto_info->clk_bulks =
		devm_kzalloc(&pdev->dev, sizeof(*crypto_info->clk_bulks) *
			     crypto_info->soc_data->clks_num, GFP_KERNEL);

	for (i = 0; i < crypto_info->soc_data->clks_num; i++)
		crypto_info->clk_bulks[i].id = crypto_info->soc_data->clks[i];

	if (crypto_info->soc_data->rsts[0]) {
		crypto_info->rst =
			devm_reset_control_get(dev,
					       crypto_info->soc_data->rsts[0]);
		if (IS_ERR(crypto_info->rst)) {
			err = PTR_ERR(crypto_info->rst);
			goto err_crypto;
		}
		reset_control_assert(crypto_info->rst);
		usleep_range(10, 20);
		reset_control_deassert(crypto_info->rst);
	}

	err = devm_add_action_or_reset(dev, rk_crypto_action, crypto_info);
	if (err)
		goto err_crypto;

	spin_lock_init(&crypto_info->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	crypto_info->reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(crypto_info->reg)) {
		err = PTR_ERR(crypto_info->reg);
		goto err_crypto;
	}

	err = devm_clk_bulk_get(dev, crypto_info->soc_data->clks_num,
				crypto_info->clk_bulks);
	if (err) {
		dev_err(&pdev->dev, "failed to get clks property\n");
		goto err_crypto;
	}

	crypto_info->irq = platform_get_irq(pdev, 0);
	if (crypto_info->irq < 0) {
		dev_warn(crypto_info->dev,
			 "control Interrupt is not available.\n");
		err = crypto_info->irq;
		goto err_crypto;
	}

	err = devm_request_irq(&pdev->dev, crypto_info->irq,
			       rk_crypto_irq_handle, IRQF_SHARED,
			       "rk-crypto", pdev);

	if (err) {
		dev_err(crypto_info->dev, "irq request failed.\n");
		goto err_crypto;
	}

	crypto_info->dev = &pdev->dev;

	crypto_info->hw_info =
		devm_kzalloc(&pdev->dev,
			     crypto_info->soc_data->hw_info_size, GFP_KERNEL);
	if (!crypto_info->hw_info) {
		err = -ENOMEM;
		goto err_crypto;
	}

	err = crypto_info->soc_data->hw_init(&pdev->dev, crypto_info->hw_info);
	if (err) {
		dev_err(crypto_info->dev, "hw_init failed.\n");
		goto err_crypto;
	}

	crypto_info->addr_vir = (char *)__get_free_page(GFP_KERNEL);
	if (!crypto_info->addr_vir) {
		err = -ENOMEM;
		dev_err(crypto_info->dev, "__get_free_page failed.\n");
		goto err_crypto;
	}

	platform_set_drvdata(pdev, crypto_info);

	tasklet_init(&crypto_info->queue_task,
		     rk_crypto_queue_task_cb, (unsigned long)crypto_info);
	tasklet_init(&crypto_info->done_task,
		     rk_crypto_done_task_cb, (unsigned long)crypto_info);
	crypto_init_queue(&crypto_info->queue, 50);

	mutex_init(&crypto_info->mutex);

	crypto_info->request_crypto = rk_crypto_request;
	crypto_info->release_crypto = rk_crypto_release;
	crypto_info->load_data = rk_load_data;
	crypto_info->unload_data = rk_unload_data;
	crypto_info->enqueue = rk_crypto_enqueue;
	crypto_info->busy = false;

	err = rk_crypto_register(crypto_info);
	if (err) {
		dev_err(dev, "err in register alg");
		goto err_register_alg;
	}

	dev_info(dev, "Crypto Accelerator successfully registered\n");
	return 0;

err_register_alg:
	mutex_destroy(&crypto_info->mutex);
	tasklet_kill(&crypto_info->queue_task);
	tasklet_kill(&crypto_info->done_task);
err_crypto:
	return err;
}

static int rk_crypto_remove(struct platform_device *pdev)
{
	struct rk_crypto_info *crypto_info = platform_get_drvdata(pdev);

	rk_crypto_unregister(crypto_info);
	tasklet_kill(&crypto_info->done_task);
	tasklet_kill(&crypto_info->queue_task);

	if (crypto_info->addr_vir)
		free_page((unsigned long)crypto_info->addr_vir);

	crypto_info->soc_data->hw_deinit(&pdev->dev, crypto_info->hw_info);

	mutex_destroy(&crypto_info->mutex);

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
