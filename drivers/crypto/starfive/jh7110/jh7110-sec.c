// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 StarFive, Inc <william.qiu@starfivetech.com>
 * Copyright 2021 StarFive, Inc <huan.feng@starfivetech.com>
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING
 * CUSTOMERS WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER
 * FOR THEM TO SAVE TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE
 * FOR ANY DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY
 * CLAIMS ARISING FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE
 * BY CUSTOMERS OF THE CODING INFORMATION CONTAINED HEREIN IN CONNECTION
 * WITH THEIR PRODUCTS.
 */


#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>

#include "jh7110-str.h"

#define DRIVER_NAME             "jh7110-sec"

struct jh7110_dev_list {
	struct list_head        dev_list;
	spinlock_t              lock; /* protect dev_list */
};

static struct jh7110_dev_list dev_list = {
	.dev_list = LIST_HEAD_INIT(dev_list.dev_list),
	.lock     = __SPIN_LOCK_UNLOCKED(dev_list.lock),
};

struct jh7110_sec_dev *jh7110_sec_find_dev(struct jh7110_sec_ctx *ctx)
{
	struct jh7110_sec_dev *sdev = NULL, *tmp;

	spin_lock_bh(&dev_list.lock);
	if (!ctx->sdev) {
		list_for_each_entry(tmp, &dev_list.dev_list, list) {
			sdev = tmp;
			break;
		}
		ctx->sdev = sdev;
	} else {
		sdev = ctx->sdev;
	}

	spin_unlock_bh(&dev_list.lock);

	return sdev;
}

static irqreturn_t jh7110_cryp_irq_thread(int irq, void *arg)
{
	struct jh7110_sec_dev *sdev = (struct jh7110_sec_dev *) arg;

	if (sdev->use_dma)
		if (sdev->cry_type != JH7110_PKA_TYPE)
			return IRQ_HANDLED;

	mutex_unlock(&sdev->doing);

	return IRQ_HANDLED;
}

static irqreturn_t jh7110_cryp_irq(int irq, void *arg)
{
	struct jh7110_sec_dev *sdev = (struct jh7110_sec_dev *) arg;
	union jh7110_sha_shacsr sha_csr;
	union jh7110_aes_csr   aes_csr;
	union jh7110_des_daecsr   des_csr;
	union jh7110_crypto_cacr  cry_cacr;
	union jh7110_crypto_casr  cry_casr;
	irqreturn_t ret = IRQ_WAKE_THREAD;

	switch (sdev->cry_type) {
	case JH7110_SHA_TYPE:
		sha_csr.v = jh7110_sec_read(sdev, JH7110_SHA_SHACSR);
		if (sha_csr.hmac_done)
			sdev->done_flags |= JH7110_SHA_HMAC_DONE;
		if (sha_csr.shadone)
			sdev->done_flags |= JH7110_SHA_SHA_DONE;

		jh7110_sec_write(sdev, JH7110_SHA_SHACSR, sha_csr.v | BIT(15) | BIT(17));
		break;
	case JH7110_AES_TYPE:
		aes_csr.v = jh7110_sec_read(sdev, JH7110_AES_CSR);
		if (aes_csr.done) {
			sdev->done_flags |= JH7110_AES_DONE;
			jh7110_sec_write(sdev, JH7110_AES_CSR, aes_csr.v);
		}

		break;
	case JH7110_DES_TYPE:
		des_csr.v = jh7110_sec_read(sdev, JH7110_DES_DAECSR_OFFSET);
		if (des_csr.done) {
			sdev->done_flags |= JH7110_DES_DONE;
			jh7110_sec_write(sdev, JH7110_DES_DAECSR_OFFSET, des_csr.v);
		}

		break;
	case JH7110_PKA_TYPE:
		cry_casr.v = jh7110_sec_read(sdev, JH7110_CRYPTO_CASR_OFFSET);
		if (cry_casr.done)
			sdev->done_flags |= JH7110_PKA_DONE_FLAGS;
		cry_cacr.v = jh7110_sec_read(sdev, JH7110_CRYPTO_CACR_OFFSET);
		cry_cacr.cln_done = 1;
		jh7110_sec_write(sdev, JH7110_CRYPTO_CACR_OFFSET, cry_cacr.v);
		break;
	default:
		break;
	}
	return ret;
}

static const struct of_device_id jh7110_dt_ids[] = {
	{ .compatible = "starfive,jh7110-sec", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, jh7110_dt_ids);

static int jh7110_dma_init(struct jh7110_sec_dev *sdev)
{
	dma_cap_mask_t mask;
	int err;

	sdev->sec_xm_m = NULL;
	sdev->sec_xm_p = NULL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	sdev->sec_xm_m = dma_request_chan(sdev->dev, "sec_m");
	if (IS_ERR(sdev->sec_xm_m)) {
		dev_err(sdev->dev, "Unable to request sec_m dma channel in DMA channel\n");
		return PTR_ERR(sdev->sec_xm_m);
	}

	sdev->sec_xm_p = dma_request_chan(sdev->dev, "sec_p");
	if (IS_ERR(sdev->sec_xm_p)) {
		dev_err(sdev->dev, "Unable to request sec_p dma channel in DMA channel\n");
		goto err_sha_out;
	}

	init_completion(&sdev->sec_comp_m);
	init_completion(&sdev->sec_comp_p);

	return 0;

err_sha_out:
	dma_release_channel(sdev->sec_xm_m);

	return err;
}

static void jh7110_dma_cleanup(struct jh7110_sec_dev *sdev)
{
	dma_release_channel(sdev->sec_xm_p);
	dma_release_channel(sdev->sec_xm_m);
}
struct gpio_desc *gpio;

static int jh7110_cryp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jh7110_sec_dev *sdev;
	struct resource *res;
	int pages = 0;
	int ret;

	sdev = devm_kzalloc(dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->dev = dev;

	mutex_init(&sdev->lock);
	mutex_init(&sdev->doing);
	mutex_init(&sdev->pl080_doing);
	mutex_init(&sdev->sha_lock);
	mutex_init(&sdev->aes_lock);
	mutex_init(&sdev->des_lock);
	mutex_init(&sdev->rsa_lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "secreg");
	sdev->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(sdev->io_base))
		return PTR_ERR(sdev->io_base);

	sdev->io_phys_base = res->start;

	sdev->dma_base = ioremap(0x16008000, 0x4000);
	if (IS_ERR(sdev->dma_base))
		return PTR_ERR(sdev->dma_base);

	sdev->use_dma = device_property_read_bool(dev, "enable-dma");
	sdev->dma_maxburst = 32;

	sdev->secirq = platform_get_irq_byname(pdev, "secirq");
	sdev->secirq = platform_get_irq(pdev, 0);
	if (sdev->secirq < 0) {
		dev_err(dev, "Cannot get IRQ resource\n");
		return sdev->secirq;
	}

	ret = devm_request_threaded_irq(dev, sdev->secirq, jh7110_cryp_irq,
					jh7110_cryp_irq_thread, IRQF_SHARED,
					dev_name(dev), sdev);
	if (ret) {
		dev_err(&pdev->dev, "Can't get interrupt working.\n");
		return ret;
	}

	sdev->sec_hclk = devm_clk_get(dev, "sec_hclk");
	if (IS_ERR(sdev->sec_hclk)) {
		dev_err(dev, "failed to get sec clock\n");
		return PTR_ERR(sdev->sec_hclk);
	}

	sdev->sec_ahb = devm_clk_get(dev, "sec_ahb");
	if (IS_ERR(sdev->sec_ahb)) {
		dev_err(dev, "failed to get sec clock\n");
		return PTR_ERR(sdev->sec_ahb);
	}

	sdev->rst_hresetn = devm_reset_control_get_shared(sdev->dev, "sec_hre");
	if (IS_ERR(sdev->rst_hresetn)) {
		dev_err(sdev->dev, "failed to get sec reset\n");
		return PTR_ERR(sdev->rst_hresetn);
	}

	clk_prepare_enable(sdev->sec_hclk);
	clk_prepare_enable(sdev->sec_ahb);
	reset_control_deassert(sdev->rst_hresetn);

	platform_set_drvdata(pdev, sdev);

	spin_lock(&dev_list.lock);
	list_add(&sdev->list, &dev_list.dev_list);
	spin_unlock(&dev_list.lock);

	if (sdev->use_dma) {
		ret = jh7110_dma_init(sdev);
		if (ret) {
			dev_err(dev, "Cannot initial dma chan\n");
			goto err_dma_init;
		}
	}

	pages = get_order(JH7110_MSG_BUFFER_SIZE);

	sdev->sha_data = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA32, pages);
	if (!sdev->sha_data) {
		dev_err(sdev->dev, "Can't allocate aes buffer pages when unaligned\n");
		goto err_sha_data;
	}

	sdev->aes_data = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA32, pages);
	if (!sdev->aes_data) {
		dev_err(sdev->dev, "Can't allocate aes buffer pages when unaligned\n");
		goto err_aes_data;
	}

	sdev->des_data = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA32, pages);
	if (!sdev->des_data) {
		dev_err(sdev->dev, "Can't allocate des buffer pages when unaligned\n");
		goto err_des_data;
	}

	sdev->pka_data = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA32, pages);
	if (!sdev->pka_data) {
		dev_err(sdev->dev, "Can't allocate pka buffer pages when unaligned\n");
		goto err_pka_data;
	}

	sdev->pages_count = pages >> 1;
	sdev->data_buf_len = JH7110_MSG_BUFFER_SIZE >> 1;

	/* Initialize crypto engine */
	sdev->engine = crypto_engine_alloc_init(dev, 1);
	if (!sdev->engine) {
		ret = -ENOMEM;
		goto err_engine;
	}

	ret = crypto_engine_start(sdev->engine);
	if (ret)
		goto err_engine_start;

	ret = jh7110_hash_register_algs();
	if (ret)
		goto err_algs_sha;

	ret = jh7110_aes_register_algs();
	if (ret)
		goto err_algs_aes;

	ret = jh7110_des_register_algs();
	if (ret)
		goto err_algs_des;

	ret = jh7110_pka_register_algs();
	if (ret)
		goto err_algs_pka;

	dev_info(dev, "Initialized\n");

	return 0;
 err_algs_pka:
	jh7110_des_unregister_algs();
 err_algs_des:
	jh7110_aes_unregister_algs();
 err_algs_aes:
	jh7110_hash_unregister_algs();
 err_algs_sha:
	crypto_engine_stop(sdev->engine);
 err_engine_start:
	crypto_engine_exit(sdev->engine);
 err_engine:
	free_pages((unsigned long)sdev->pka_data, pages);
 err_pka_data:
	free_pages((unsigned long)sdev->des_data, pages);
 err_des_data:
	free_pages((unsigned long)sdev->aes_data, pages);
 err_aes_data:
	free_pages((unsigned long)sdev->sha_data, pages);
 err_sha_data:
	jh7110_dma_cleanup(sdev);
 err_dma_init:
	spin_lock(&dev_list.lock);
	list_del(&sdev->list);
	spin_unlock(&dev_list.lock);

	return ret;
}

static int jh7110_cryp_remove(struct platform_device *pdev)
{
	struct jh7110_sec_dev *sdev = platform_get_drvdata(pdev);

	if (!sdev)
		return -ENODEV;

	jh7110_pka_unregister_algs();
	jh7110_des_unregister_algs();
	jh7110_aes_unregister_algs();
	jh7110_hash_unregister_algs();

	crypto_engine_stop(sdev->engine);
	crypto_engine_exit(sdev->engine);

	jh7110_dma_cleanup(sdev);

	free_pages((unsigned long)sdev->pka_data, sdev->pages_count);
	free_pages((unsigned long)sdev->des_data, sdev->pages_count);
	free_pages((unsigned long)sdev->aes_data, sdev->pages_count);
	free_pages((unsigned long)sdev->sha_data, sdev->pages_count);
	sdev->pka_data = NULL;
	sdev->des_data = NULL;
	sdev->aes_data = NULL;
	sdev->sha_data = NULL;

	spin_lock(&dev_list.lock);
	list_del(&sdev->list);
	spin_unlock(&dev_list.lock);

	clk_disable_unprepare(sdev->sec_hclk);
	clk_disable_unprepare(sdev->sec_ahb);

	return 0;
}

#ifdef CONFIG_PM
static int jh7110_cryp_runtime_suspend(struct device *dev)
{
	struct jh7110_sec_dev *sdev = dev_get_drvdata(dev);

	clk_disable_unprepare(sdev->sec_ahb);
	clk_disable_unprepare(sdev->sec_hclk);

	return 0;
}

static int jh7110_cryp_runtime_resume(struct device *dev)
{
	struct jh7110_sec_dev *sdev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sdev->sec_ahb);
	if (ret) {
		dev_err(sdev->dev, "Failed to prepare_enable sec_ahb clock\n");
		return ret;
	}

	ret = clk_prepare_enable(sdev->sec_hclk);
	if (ret) {
		dev_err(sdev->dev, "Failed to prepare_enable sec_hclk clock\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops jh7110_cryp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(jh7110_cryp_runtime_suspend,
			   jh7110_cryp_runtime_resume, NULL)
};

static struct platform_driver jh7110_cryp_driver = {
	.probe  = jh7110_cryp_probe,
	.remove = jh7110_cryp_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.pm		= &jh7110_cryp_pm_ops,
		.of_match_table = jh7110_dt_ids,
	},
};

module_platform_driver(jh7110_cryp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huan Feng <huan.feng@starfivetech.com>");
MODULE_DESCRIPTION("Starfive JH7110 CRYP DES SHA and AES driver");
