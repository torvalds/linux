// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "aspeed-rsss.h"

static struct aspeed_rsss_alg *aspeed_rsss_algs[] = {
	&aspeed_rsss_algs_rsa,
	&aspeed_rsss_algs_sha3_224,
	&aspeed_rsss_algs_sha3_256,
	&aspeed_rsss_algs_sha3_384,
	&aspeed_rsss_algs_sha3_512,
};

static void aspeed_rsss_register(struct aspeed_rsss_dev *rsss_dev)
{
	char *cra_name;
	int rc;

	for (int i = 0; i < ARRAY_SIZE(aspeed_rsss_algs); i++) {
		aspeed_rsss_algs[i]->rsss_dev = rsss_dev;
		if (aspeed_rsss_algs[i]->type == ASPEED_ALGO_TYPE_AKCIPHER) {
			rc = crypto_register_akcipher(&aspeed_rsss_algs[i]->alg.akcipher);
			cra_name = aspeed_rsss_algs[i]->alg.akcipher.base.cra_name;

		} else if (aspeed_rsss_algs[i]->type == ASPEED_ALGO_TYPE_AHASH) {
			rc = crypto_register_ahash(&aspeed_rsss_algs[i]->alg.ahash);
			cra_name = aspeed_rsss_algs[i]->alg.ahash.halg.base.cra_name;
		}

		if (rc)
			RSSS_DBG(rsss_dev, "Failed to register [%d] %s\n", i, cra_name);
	}
}

static void aspeed_rsss_unregister(struct aspeed_rsss_dev *rsss_dev)
{
	for (int i = 0; i < ARRAY_SIZE(aspeed_rsss_algs); i++) {
		if (aspeed_rsss_algs[i]->type == ASPEED_ALGO_TYPE_AKCIPHER)
			crypto_unregister_akcipher(&aspeed_rsss_algs[i]->alg.akcipher);

		else if (aspeed_rsss_algs[i]->type == ASPEED_ALGO_TYPE_AHASH)
			crypto_unregister_ahash(&aspeed_rsss_algs[i]->alg.ahash);
	}
}

/* RSSS interrupt service routine. */
static irqreturn_t aspeed_rsss_irq(int irq, void *dev)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)dev;
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	u32 sts;

	sts = ast_rsss_read(rsss_dev, ASPEED_RSSS_INT_STS);
	ast_rsss_write(rsss_dev, sts, ASPEED_RSSS_INT_STS);

	RSSS_DBG(rsss_dev, "irq sts:0x%x\n", sts);

	if (sts & RSA_INT_DONE) {
		/* Stop RSA engine */
		ast_rsss_write(rsss_dev, 0, ASPEED_RSA_TRIGGER);

		if (rsa_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&rsa_engine->done_task);
		else
			dev_err(rsss_dev->dev, "RSA no active requests.\n");
	}

	if (sts & SHA3_INT_DONE) {
		if (sha3_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&sha3_engine->done_task);
		else
			dev_err(rsss_dev->dev, "SHA3 no active requests.\n");
	}

	return IRQ_HANDLED;
}

static const struct of_device_id aspeed_rsss_of_matches[] = {
	{ .compatible = "aspeed,ast2700-rsss", },
	{},
};

static int aspeed_rsss_probe(struct platform_device *pdev)
{
	struct aspeed_rsss_dev *rsss_dev;
	struct device *dev = &pdev->dev;
	int rc;

	rsss_dev = devm_kzalloc(dev, sizeof(struct aspeed_rsss_dev),
				GFP_KERNEL);
	if (!rsss_dev)
		return -ENOMEM;

	rsss_dev->dev = dev;

	platform_set_drvdata(pdev, rsss_dev);

	rsss_dev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rsss_dev->regs))
		return PTR_ERR(rsss_dev->regs);

	/* Get irq number and register it */
	rsss_dev->irq = platform_get_irq(pdev, 0);
	if (rsss_dev->irq < 0)
		return -ENXIO;

	rc = devm_request_irq(dev, rsss_dev->irq, aspeed_rsss_irq, 0,
			      dev_name(dev), rsss_dev);
	if (rc) {
		dev_err(dev, "Failed to request irq.\n");
		return rc;
	}

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_warn(&pdev->dev, "No suitable DMA available\n");
		return rc;
	}

	rsss_dev->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(rsss_dev->clk)) {
		dev_err(dev, "Failed to get rsss clk\n");
		return PTR_ERR(rsss_dev->clk);
	}

	rsss_dev->reset_rsa = devm_reset_control_get(dev, "rsa");
	if (IS_ERR(rsss_dev->reset_rsa)) {
		dev_err(dev, "Failed to get rsa reset\n");
		return PTR_ERR(rsss_dev->reset_rsa);
	}

	rsss_dev->reset_sha3 = devm_reset_control_get(dev, "sha3");
	if (IS_ERR(rsss_dev->reset_sha3)) {
		dev_err(dev, "Failed to get sha3 reset\n");
		return PTR_ERR(rsss_dev->reset_sha3);
	}

	rc = aspeed_rsss_rsa_init(rsss_dev);
	if (rc) {
		dev_err(dev, "RSA init failed\n");
		return rc;
	}

	rc = aspeed_rsss_sha3_init(rsss_dev);
	if (rc) {
		dev_err(dev, "SHA3 init failed\n");
		return rc;
	}

	aspeed_rsss_register(rsss_dev);

	dev_info(dev, "Aspeed RSSS Hardware Accelerator successfully registered\n");

	return 0;
}

static int aspeed_rsss_remove(struct platform_device *pdev)
{
	struct aspeed_rsss_dev *rsss_dev = platform_get_drvdata(pdev);

	aspeed_rsss_unregister(rsss_dev);
	aspeed_rsss_rsa_exit(rsss_dev);
	aspeed_rsss_sha3_exit(rsss_dev);

	return 0;
}

MODULE_DEVICE_TABLE(of, aspeed_rsss_of_matches);

static struct platform_driver aspeed_rsss_driver = {
	.probe		= aspeed_rsss_probe,
	.remove		= aspeed_rsss_remove,
	.driver		= {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_rsss_of_matches,
	},
};

module_platform_driver(aspeed_rsss_driver);

MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED RSSS driver for multiple cryptographic engines");
MODULE_LICENSE("GPL");
