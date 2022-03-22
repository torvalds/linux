/*
 * Crypto driver for the Aspeed SoC
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "aspeed-hace.h"

// #define ASPEED_HACE_DEBUG

#ifdef ASPEED_HACE_DEBUG
//#define HACE_DBUG(fmt, args...) printk(KERN_DEBUG "%s() " fmt, __FUNCTION__, ## args)
#define HACE_DBUG(fmt, args...) printk("%s() " fmt, __FUNCTION__, ## args)
#else
#define HACE_DBUG(fmt, args...)
#endif

static unsigned char *dummy_key1;
static unsigned char *dummy_key2;

int find_dummy_key(const char *key, int keylen)
{
	int ret = 0;

	if (dummy_key1 && memcmp(key, dummy_key1, keylen) == 0)
		ret = 1;
	else if (dummy_key2 && memcmp(key, dummy_key2, keylen) == 0)
		ret = 2;

	return ret;
}

static irqreturn_t aspeed_hace_irq(int irq, void *dev)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)dev;
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;
	u32 sts = aspeed_hace_read(hace_dev, ASPEED_HACE_STS);
	int handle = IRQ_NONE;

	HACE_DBUG("aspeed_hace_irq sts %x \n", sts);
	aspeed_hace_write(hace_dev, sts, ASPEED_HACE_STS);

	if (sts & HACE_CRYPTO_ISR) {
		if (crypto_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&crypto_engine->done_task);
		else
			dev_warn(hace_dev->dev, "HACE CRYPTO interrupt when no active requests.\n");
		handle = IRQ_HANDLED;
	}

	if (sts & HACE_HASH_ISR) {
		if (hash_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&hash_engine->done_task);
		else
			dev_warn(hace_dev->dev, "HACE HASH interrupt when no active requests.\n");
		handle = IRQ_HANDLED;
	}
	if (sts & HACE_RSA_ISR) {
		aspeed_hace_write(hace_dev, 0, ASPEED_HACE_RSA_CMD);
		if (rsa_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&rsa_engine->done_task);
		else
			dev_warn(hace_dev->dev, "CRYPTO interrupt when no active requests.\n");
		handle = IRQ_HANDLED;
	}
	return handle;
}

static void aspeed_hace_cryptro_done_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;

	crypto_engine->is_async = true;
	(void)crypto_engine->resume(hace_dev);
}

static void aspeed_hace_hash_done_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;
	struct aspeed_engine_hash *hash_engine = &hace_dev->hash_engine;

	HACE_DBUG("\n");

	(void)hash_engine->resume(hace_dev);
}

static void aspeed_hace_rsa_done_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;

	HACE_DBUG("\n");

	rsa_engine->is_async = true;
	(void)rsa_engine->resume(hace_dev);
}

static void aspeed_hace_crypto_queue_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;

	HACE_DBUG("\n");
	aspeed_hace_crypto_handle_queue(hace_dev, NULL);
}

static void aspeed_hace_hash_queue_task(unsigned long data)
{
	struct aspeed_hace_dev *hace_dev = (struct aspeed_hace_dev *)data;

	HACE_DBUG("\n");
	aspeed_hace_hash_handle_queue(hace_dev, NULL);
}

static int aspeed_hace_register(struct aspeed_hace_dev *hace_dev)
{
	aspeed_register_hace_crypto_algs(hace_dev);
	aspeed_register_hace_hash_algs(hace_dev);
	// if (hace_dev->version != 6)
	// 	aspeed_register_hace_rsa_algs(hace_dev);

	return 0;
}

// static void aspeed_hace_unregister(void)
// {
// #if 0
// 	unsigned int i;

// 	for (i = 0; i < ARRAY_SIZE(aspeed_cipher_algs); i++) {
// 		if (aspeed_cipher_algs[i]->type == ALG_TYPE_CIPHER)
// 			crypto_unregister_alg(&aspeed_cipher_algs[i]->alg.crypto);
// 		else
// 			crypto_unregister_ahash(&aspeed_cipher_algs[i]->alg.hash);
// 	}
// #endif
// }

static const struct of_device_id aspeed_hace_of_matches[] = {
	{ .compatible = "aspeed,ast2400-hace", .data = (void *) 0,},
	{ .compatible = "aspeed,ast2500-hace", .data = (void *) 5,},
	{ .compatible = "aspeed,ast2600-hace", .data = (void *) 6,},
	{},
};

static int aspeed_hace_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_hace_dev *hace_dev;
	const struct of_device_id *hace_dev_id;
	struct aspeed_engine_crypto *crypto_engine;
	struct aspeed_engine_hash *hash_engine;
	struct aspeed_hace_engine_rsa *rsa_engine;
	int err;
	struct device_node *sec_node;


	hace_dev = devm_kzalloc(&pdev->dev, sizeof(struct aspeed_hace_dev), GFP_KERNEL);
	if (!hace_dev) {
		dev_err(dev, "unable to alloc data struct.\n");
		return -ENOMEM;
	}

	hace_dev_id = of_match_device(aspeed_hace_of_matches, &pdev->dev);
	if (!hace_dev_id)
		return -EINVAL;

	hace_dev->dev = dev;
	hace_dev->version = (unsigned long)hace_dev_id->data;
	crypto_engine = &hace_dev->crypto_engine;
	hash_engine = &hace_dev->hash_engine;
	rsa_engine = &hace_dev->rsa_engine;

	platform_set_drvdata(pdev, hace_dev);
	spin_lock_init(&crypto_engine->lock);
	tasklet_init(&crypto_engine->done_task, aspeed_hace_cryptro_done_task, (unsigned long)hace_dev);
	tasklet_init(&crypto_engine->queue_task, aspeed_hace_crypto_queue_task, (unsigned long)hace_dev);
	crypto_init_queue(&crypto_engine->queue, 50);

	spin_lock_init(&hash_engine->lock);
	tasklet_init(&hash_engine->done_task, aspeed_hace_hash_done_task, (unsigned long)hace_dev);
	tasklet_init(&hash_engine->queue_task, aspeed_hace_hash_queue_task, (unsigned long)hace_dev);
	crypto_init_queue(&hash_engine->queue, 50);

	spin_lock_init(&rsa_engine->lock);
	tasklet_init(&rsa_engine->done_task, aspeed_hace_rsa_done_task, (unsigned long)hace_dev);
	crypto_init_queue(&rsa_engine->queue, 50);

	hace_dev->regs = of_iomap(pdev->dev.of_node, 0);
	if (!(hace_dev->regs)) {
		dev_err(dev, "can't ioremap\n");
		return -ENOMEM;
	}

	hace_dev->irq = platform_get_irq(pdev, 0);
	if (!hace_dev->irq) {
		dev_err(&pdev->dev, "no memory/irq resource for hace_dev\n");
		return -ENXIO;
	}

	if (devm_request_irq(&pdev->dev, hace_dev->irq, aspeed_hace_irq, 0, dev_name(&pdev->dev), hace_dev)) {
		dev_err(dev, "unable to request aes irq.\n");
		return -EBUSY;
	}

	hace_dev->yclk = devm_clk_get(&pdev->dev, "yclk");
	if (IS_ERR(hace_dev->yclk)) {
		dev_err(&pdev->dev, "no yclk clock defined\n");
		return -ENODEV;
	}

	clk_prepare_enable(hace_dev->yclk);

	if (hace_dev->version != 6) {
		hace_dev->rsaclk = devm_clk_get(&pdev->dev, "rsaclk");
		if (IS_ERR(hace_dev->rsaclk)) {
			dev_err(&pdev->dev, "no rsaclk clock defined\n");
			return -ENODEV;
		}
		clk_prepare_enable(hace_dev->rsaclk);
	}

	// 8-byte aligned
	crypto_engine->cipher_ctx = dma_alloc_coherent(&pdev->dev,
				    PAGE_SIZE,
				    &crypto_engine->cipher_ctx_dma, GFP_KERNEL);
	crypto_engine->cipher_addr = dma_alloc_coherent(&pdev->dev, ASPEED_CRYPTO_SRC_DMA_BUF_LEN,
				     &crypto_engine->cipher_dma_addr, GFP_KERNEL);

	if (!crypto_engine->cipher_addr) {
		printk("error buff allocation\n");
		return -ENOMEM;
	}

	hash_engine->ahash_src_addr = dma_alloc_coherent(&pdev->dev,
				      ASPEED_HASH_SRC_DMA_BUF_LEN,
				      &hash_engine->ahash_src_dma_addr, GFP_KERNEL);
	if (!hash_engine->ahash_src_addr) {
		printk("error buff allocation\n");
		return -ENOMEM;
	}
	if (hace_dev->version == 6) {
		crypto_engine->dst_sg_addr = dma_alloc_coherent(&pdev->dev,
					     ASPEED_CRYPTO_DST_DMA_BUF_LEN,
					     &crypto_engine->dst_sg_dma_addr, GFP_KERNEL);
		if (!crypto_engine->dst_sg_addr) {
			printk("error buff allocation\n");
			return -ENOMEM;
		}
	} else {
		rsa_engine->rsa_buff = of_iomap(pdev->dev.of_node, 1);
		if (!(rsa_engine->rsa_buff)) {
			dev_err(dev, "can't rsa ioremap\n");
			return -ENOMEM;
		}
	}

	err = aspeed_hace_register(hace_dev);
	if (err) {
		dev_err(dev, "err in register alg");
		return err;
	}

	if (of_find_property(dev->of_node, "dummy-key1", NULL)) {
		dummy_key1 = kzalloc(DUMMY_KEY_SIZE, GFP_KERNEL);
		if (dummy_key1) {
			err = of_property_read_u8_array(dev->of_node, "dummy-key1", dummy_key1, DUMMY_KEY_SIZE);
			if (err)
				dev_err(dev, "err read dummy_key 1\n");
		} else
			dev_err(dev, "error dummy_key1 allocation\n");
	}
	if (of_find_property(dev->of_node, "dummy-key2", NULL)) {
		dummy_key2 = kzalloc(DUMMY_KEY_SIZE, GFP_KERNEL);
		if (dummy_key2) {
			err = of_property_read_u8_array(dev->of_node, "dummy-key2", dummy_key2, DUMMY_KEY_SIZE);
			if (err)
				dev_err(dev, "err read dummy_key 2\n");
		} else
			dev_err(dev, "error dummy_key2 allocation\n");
	}

	sec_node = of_find_compatible_node(NULL, NULL, "aspeed,ast2600-otp");
	if (!sec_node) {
		dev_err(dev, "[%s:%d] cannot find sec node\n", __func__, __LINE__);
	} else {
		hace_dev->sec_regs = of_iomap(sec_node, 0);
		if (!hace_dev->sec_regs)
			dev_err(dev, "[%s:%d] failed to map SEC registers\n", __func__, __LINE__);
	}

	dev_info(dev, "ASPEED Crypto Accelerator successfully registered\n");

	return 0;
}

static int aspeed_hace_remove(struct platform_device *pdev)
{
	// struct aspeed_hace_dev *hace_dev = platform_get_drvdata(pdev);

	//aspeed_hace_unregister();
	// tasklet_kill(&hace_dev->done_task);
	// tasklet_kill(&hace_dev->queue_task);
	kfree(dummy_key1);
	kfree(dummy_key2);
	return 0;
}

#ifdef CONFIG_PM
static int aspeed_hace_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct aspeed_hace_dev *hace_dev = platform_get_drvdata(pdev);

	/*
	 * We only support standby mode. All we have to do is gate the clock to
	 * the spacc. The hardware will preserve state until we turn it back
	 * on again.
	 */
	clk_disable(hace_dev->yclk);

	return 0;
}

static int aspeed_hace_resume(struct platform_device *pdev)
{
	struct aspeed_hace_dev *hace_dev = platform_get_drvdata(pdev);

	return clk_enable(hace_dev->yclk);
}

#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(of, aspeed_hace_of_matches);

static struct platform_driver aspeed_hace_driver = {
	.probe 		= aspeed_hace_probe,
	.remove		= aspeed_hace_remove,
#ifdef CONFIG_PM
	.suspend	= aspeed_hace_suspend,
	.resume 	= aspeed_hace_resume,
#endif
	.driver         = {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_hace_of_matches,
	},
};

module_platform_driver(aspeed_hace_driver);

MODULE_AUTHOR("Johnny Huang <johnny_huang@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED Crypto driver");
MODULE_LICENSE("GPL2");
