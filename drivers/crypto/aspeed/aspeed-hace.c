// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "aspeed-hace.h"

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
#define HACE_DBG(d, fmt, ...)	\
	dev_info((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define HACE_DBG(d, fmt, ...)	\
	dev_dbg((d)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#endif

/* HACE interrupt service routine */
static irqreturn_t aspeed_hace_irq(int irq, void *dev)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)dev;
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	u32 sts;

	sts = ast_hace_read(hace_dev, ASPEED_HACE_STS);
	ast_hace_write(hace_dev, sts, ASPEED_HACE_STS);

	HACE_DBG(hace_dev, "irq status: 0x%x\n", sts);

	if (sts & HACE_HASH_ISR) {
		if (hash_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&hash_engine->done_task);
		else
			dev_warn(hace_dev->dev, "HASH no active requests.\n");
	}

	if (sts & HACE_CRYPTO_ISR) {
		if (crypto_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&crypto_engine->done_task);
		else
			dev_warn(hace_dev->dev, "CRYPTO no active requests.\n");
	}

	return IRQ_HANDLED;
}

static void aspeed_hace_crypto_done_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;

	crypto_engine->resume(hace_dev);
}

static void aspeed_hace_hash_done_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;

	hash_engine->resume(hace_dev);
}

static void aspeed_hace_register(struct aspeed_hace_dev *hace_dev)
{
#ifdef CONFIG_CRYPTO_DEV_ASPEED_HACE_HASH
	aspeed_register_hace_hash_algs(hace_dev);
#endif
#ifdef CONFIG_CRYPTO_DEV_ASPEED_HACE_CRYPTO
	aspeed_register_hace_crypto_algs(hace_dev);
#endif
}

static void aspeed_hace_unregister(struct aspeed_hace_dev *hace_dev)
{
#ifdef CONFIG_CRYPTO_DEV_ASPEED_HACE_HASH
	aspeed_unregister_hace_hash_algs(hace_dev);
#endif
#ifdef CONFIG_CRYPTO_DEV_ASPEED_HACE_CRYPTO
	aspeed_unregister_hace_crypto_algs(hace_dev);
#endif
}

static const struct of_device_id aspeed_hace_of_matches[] = {
	{ .compatible = "aspeed,ast2500-hace", .data = (void *)5, },
	{ .compatible = "aspeed,ast2600-hace", .data = (void *)6, },
	{},
};

static int aspeed_hace_probe(struct platform_device *pdev)
{
	struct aspeed_engine_crypto *crypto_engine;
	const struct of_device_id *hace_dev_id;
	struct aspeed_engine_hash *hash_engine;
	struct aspeed_hace_dev *hace_dev;
	struct resource *res;
	int rc;

	hace_dev = devm_kzalloc(&pdev->dev, sizeof(struct aspeed_hace_dev),
				GFP_KERNEL);
	if (!hace_dev)
		return -ENOMEM;

	hace_dev_id = of_match_device(aspeed_hace_of_matches, &pdev->dev);
	if (!hace_dev_id) {
		dev_err(&pdev->dev, "Failed to match hace dev id\n");
		return -EINVAL;
	}

	hace_dev->dev = &pdev->dev;
	hace_dev->version = (unsigned long)hace_dev_id->data;
	hash_engine = &hace_dev->hash_engine;
	crypto_engine = &hace_dev->crypto_engine;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	platform_set_drvdata(pdev, hace_dev);

	hace_dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hace_dev->regs))
		return PTR_ERR(hace_dev->regs);

	/* Get irq number and register it */
	hace_dev->irq = platform_get_irq(pdev, 0);
	if (hace_dev->irq < 0)
		return -ENXIO;

	rc = devm_request_irq(&pdev->dev, hace_dev->irq, aspeed_hace_irq, 0,
			      dev_name(&pdev->dev), hace_dev);
	if (rc) {
		dev_err(&pdev->dev, "Failed to request interrupt\n");
		return rc;
	}

	/* Get clk and enable it */
	hace_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hace_dev->clk)) {
		dev_err(&pdev->dev, "Failed to get clk\n");
		return -ENODEV;
	}

	rc = clk_prepare_enable(hace_dev->clk);
	if (rc) {
		dev_err(&pdev->dev, "Failed to enable clock 0x%x\n", rc);
		return rc;
	}

	/* Initialize crypto hardware engine structure for hash */
	hace_dev->crypt_engine_hash = crypto_engine_alloc_init(hace_dev->dev,
							       true);
	if (!hace_dev->crypt_engine_hash) {
		rc = -ENOMEM;
		goto clk_exit;
	}

	rc = crypto_engine_start(hace_dev->crypt_engine_hash);
	if (rc)
		goto err_engine_hash_start;

	tasklet_init(&hash_engine->done_task, aspeed_hace_hash_done_task,
		     (unsigned long)hace_dev);

	/* Initialize crypto hardware engine structure for crypto */
	hace_dev->crypt_engine_crypto = crypto_engine_alloc_init(hace_dev->dev,
								 true);
	if (!hace_dev->crypt_engine_crypto) {
		rc = -ENOMEM;
		goto err_engine_hash_start;
	}

	rc = crypto_engine_start(hace_dev->crypt_engine_crypto);
	if (rc)
		goto err_engine_crypto_start;

	tasklet_init(&crypto_engine->done_task, aspeed_hace_crypto_done_task,
		     (unsigned long)hace_dev);

	/* Allocate DMA buffer for hash engine input used */
	hash_engine->ahash_src_addr =
		dmam_alloc_coherent(&pdev->dev,
				    ASPEED_HASH_SRC_DMA_BUF_LEN,
				    &hash_engine->ahash_src_dma_addr,
				    GFP_KERNEL);
	if (!hash_engine->ahash_src_addr) {
		dev_err(&pdev->dev, "Failed to allocate dma buffer\n");
		rc = -ENOMEM;
		goto err_engine_crypto_start;
	}

	/* Allocate DMA buffer for crypto engine context used */
	crypto_engine->cipher_ctx =
		dmam_alloc_coherent(&pdev->dev,
				    PAGE_SIZE,
				    &crypto_engine->cipher_ctx_dma,
				    GFP_KERNEL);
	if (!crypto_engine->cipher_ctx) {
		dev_err(&pdev->dev, "Failed to allocate cipher ctx dma\n");
		rc = -ENOMEM;
		goto err_engine_crypto_start;
	}

	/* Allocate DMA buffer for crypto engine input used */
	crypto_engine->cipher_addr =
		dmam_alloc_coherent(&pdev->dev,
				    ASPEED_CRYPTO_SRC_DMA_BUF_LEN,
				    &crypto_engine->cipher_dma_addr,
				    GFP_KERNEL);
	if (!crypto_engine->cipher_addr) {
		dev_err(&pdev->dev, "Failed to allocate cipher addr dma\n");
		rc = -ENOMEM;
		goto err_engine_crypto_start;
	}

	/* Allocate DMA buffer for crypto engine output used */
	if (hace_dev->version == AST2600_VERSION) {
		crypto_engine->dst_sg_addr =
			dmam_alloc_coherent(&pdev->dev,
					    ASPEED_CRYPTO_DST_DMA_BUF_LEN,
					    &crypto_engine->dst_sg_dma_addr,
					    GFP_KERNEL);
		if (!crypto_engine->dst_sg_addr) {
			dev_err(&pdev->dev, "Failed to allocate dst_sg dma\n");
			rc = -ENOMEM;
			goto err_engine_crypto_start;
		}
	}

	aspeed_hace_register(hace_dev);

	dev_info(&pdev->dev, "Aspeed Crypto Accelerator successfully registered\n");

	return 0;

err_engine_crypto_start:
	crypto_engine_exit(hace_dev->crypt_engine_crypto);
err_engine_hash_start:
	crypto_engine_exit(hace_dev->crypt_engine_hash);
clk_exit:
	clk_disable_unprepare(hace_dev->clk);

	return rc;
}

static int aspeed_hace_remove(struct platform_device *pdev)
{
	struct aspeed_hace_dev *hace_dev = platform_get_drvdata(pdev);
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;

	aspeed_hace_unregister(hace_dev);

	crypto_engine_exit(hace_dev->crypt_engine_hash);
	crypto_engine_exit(hace_dev->crypt_engine_crypto);

	tasklet_kill(&hash_engine->done_task);
	tasklet_kill(&crypto_engine->done_task);

	clk_disable_unprepare(hace_dev->clk);

	return 0;
}

MODULE_DEVICE_TABLE(of, aspeed_hace_of_matches);

static struct platform_driver aspeed_hace_driver = {
	.probe		= aspeed_hace_probe,
	.remove		= aspeed_hace_remove,
	.driver         = {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_hace_of_matches,
	},
};

module_platform_driver(aspeed_hace_driver);

MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed HACE driver Crypto Accelerator");
MODULE_LICENSE("GPL");
