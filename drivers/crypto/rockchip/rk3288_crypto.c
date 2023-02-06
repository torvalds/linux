// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */

#include "rk3288_crypto.h"
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/reset.h>

static int rk_crypto_enable_clk(struct rk_crypto_info *dev)
{
	int err;

	err = clk_prepare_enable(dev->sclk);
	if (err) {
		dev_err(dev->dev, "[%s:%d], Couldn't enable clock sclk\n",
			__func__, __LINE__);
		goto err_return;
	}
	err = clk_prepare_enable(dev->aclk);
	if (err) {
		dev_err(dev->dev, "[%s:%d], Couldn't enable clock aclk\n",
			__func__, __LINE__);
		goto err_aclk;
	}
	err = clk_prepare_enable(dev->hclk);
	if (err) {
		dev_err(dev->dev, "[%s:%d], Couldn't enable clock hclk\n",
			__func__, __LINE__);
		goto err_hclk;
	}
	err = clk_prepare_enable(dev->dmaclk);
	if (err) {
		dev_err(dev->dev, "[%s:%d], Couldn't enable clock dmaclk\n",
			__func__, __LINE__);
		goto err_dmaclk;
	}
	return err;
err_dmaclk:
	clk_disable_unprepare(dev->hclk);
err_hclk:
	clk_disable_unprepare(dev->aclk);
err_aclk:
	clk_disable_unprepare(dev->sclk);
err_return:
	return err;
}

static void rk_crypto_disable_clk(struct rk_crypto_info *dev)
{
	clk_disable_unprepare(dev->dmaclk);
	clk_disable_unprepare(dev->hclk);
	clk_disable_unprepare(dev->aclk);
	clk_disable_unprepare(dev->sclk);
}

static irqreturn_t rk_crypto_irq_handle(int irq, void *dev_id)
{
	struct rk_crypto_info *dev  = platform_get_drvdata(dev_id);
	u32 interrupt_status;

	interrupt_status = CRYPTO_READ(dev, RK_CRYPTO_INTSTS);
	CRYPTO_WRITE(dev, RK_CRYPTO_INTSTS, interrupt_status);

	dev->status = 1;
	if (interrupt_status & 0x0a) {
		dev_warn(dev->dev, "DMA Error\n");
		dev->status = 0;
	}
	complete(&dev->complete);

	return IRQ_HANDLED;
}

static struct rk_crypto_tmp *rk_cipher_algs[] = {
	&rk_ecb_aes_alg,
	&rk_cbc_aes_alg,
	&rk_ecb_des_alg,
	&rk_cbc_des_alg,
	&rk_ecb_des3_ede_alg,
	&rk_cbc_des3_ede_alg,
	&rk_ahash_sha1,
	&rk_ahash_sha256,
	&rk_ahash_md5,
};

static int rk_crypto_register(struct rk_crypto_info *crypto_info)
{
	unsigned int i, k;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(rk_cipher_algs); i++) {
		rk_cipher_algs[i]->dev = crypto_info;
		if (rk_cipher_algs[i]->type == ALG_TYPE_CIPHER)
			err = crypto_register_skcipher(
					&rk_cipher_algs[i]->alg.skcipher);
		else
			err = crypto_register_ahash(
					&rk_cipher_algs[i]->alg.hash);
		if (err)
			goto err_cipher_algs;
	}
	return 0;

err_cipher_algs:
	for (k = 0; k < i; k++) {
		if (rk_cipher_algs[i]->type == ALG_TYPE_CIPHER)
			crypto_unregister_skcipher(&rk_cipher_algs[k]->alg.skcipher);
		else
			crypto_unregister_ahash(&rk_cipher_algs[i]->alg.hash);
	}
	return err;
}

static void rk_crypto_unregister(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rk_cipher_algs); i++) {
		if (rk_cipher_algs[i]->type == ALG_TYPE_CIPHER)
			crypto_unregister_skcipher(&rk_cipher_algs[i]->alg.skcipher);
		else
			crypto_unregister_ahash(&rk_cipher_algs[i]->alg.hash);
	}
}

static void rk_crypto_action(void *data)
{
	struct rk_crypto_info *crypto_info = data;

	reset_control_assert(crypto_info->rst);
}

static const struct of_device_id crypto_of_id_table[] = {
	{ .compatible = "rockchip,rk3288-crypto" },
	{}
};
MODULE_DEVICE_TABLE(of, crypto_of_id_table);

static int rk_crypto_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_crypto_info *crypto_info;
	int err = 0;

	crypto_info = devm_kzalloc(&pdev->dev,
				   sizeof(*crypto_info), GFP_KERNEL);
	if (!crypto_info) {
		err = -ENOMEM;
		goto err_crypto;
	}

	crypto_info->rst = devm_reset_control_get(dev, "crypto-rst");
	if (IS_ERR(crypto_info->rst)) {
		err = PTR_ERR(crypto_info->rst);
		goto err_crypto;
	}

	reset_control_assert(crypto_info->rst);
	usleep_range(10, 20);
	reset_control_deassert(crypto_info->rst);

	err = devm_add_action_or_reset(dev, rk_crypto_action, crypto_info);
	if (err)
		goto err_crypto;

	crypto_info->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(crypto_info->reg)) {
		err = PTR_ERR(crypto_info->reg);
		goto err_crypto;
	}

	crypto_info->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(crypto_info->aclk)) {
		err = PTR_ERR(crypto_info->aclk);
		goto err_crypto;
	}

	crypto_info->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(crypto_info->hclk)) {
		err = PTR_ERR(crypto_info->hclk);
		goto err_crypto;
	}

	crypto_info->sclk = devm_clk_get(&pdev->dev, "sclk");
	if (IS_ERR(crypto_info->sclk)) {
		err = PTR_ERR(crypto_info->sclk);
		goto err_crypto;
	}

	crypto_info->dmaclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(crypto_info->dmaclk)) {
		err = PTR_ERR(crypto_info->dmaclk);
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
	platform_set_drvdata(pdev, crypto_info);

	crypto_info->engine = crypto_engine_alloc_init(&pdev->dev, true);
	crypto_engine_start(crypto_info->engine);
	init_completion(&crypto_info->complete);

	rk_crypto_enable_clk(crypto_info);

	err = rk_crypto_register(crypto_info);
	if (err) {
		dev_err(dev, "err in register alg");
		goto err_register_alg;
	}

	dev_info(dev, "Crypto Accelerator successfully registered\n");
	return 0;

err_register_alg:
	crypto_engine_exit(crypto_info->engine);
err_crypto:
	dev_err(dev, "Crypto Accelerator not successfully registered\n");
	return err;
}

static int rk_crypto_remove(struct platform_device *pdev)
{
	struct rk_crypto_info *crypto_tmp = platform_get_drvdata(pdev);

	rk_crypto_unregister();
	rk_crypto_disable_clk(crypto_tmp);
	crypto_engine_exit(crypto_tmp->engine);
	return 0;
}

static struct platform_driver crypto_driver = {
	.probe		= rk_crypto_probe,
	.remove		= rk_crypto_remove,
	.driver		= {
		.name	= "rk3288-crypto",
		.of_match_table	= crypto_of_id_table,
	},
};

module_platform_driver(crypto_driver);

MODULE_AUTHOR("Zain Wang <zain.wang@rock-chips.com>");
MODULE_DESCRIPTION("Support for Rockchip's cryptographic engine");
MODULE_LICENSE("GPL");
