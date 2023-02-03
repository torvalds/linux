// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-pxa/ssp.c
 *
 *  based on linux/arch/arm/mach-sa1100/ssp.c by Russell King
 *
 *  Copyright (C) 2003 Russell King.
 *  Copyright (C) 2003 Wolfson Microelectronics PLC
 *
 *  PXA2xx SSP driver.  This provides the generic core for simple
 *  IO-based SSP applications and allows easy port setup for DMA access.
 *
 *  Author: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/irq.h>

static DEFINE_MUTEX(ssp_lock);
static LIST_HEAD(ssp_list);

struct ssp_device *pxa_ssp_request(int port, const char *label)
{
	struct ssp_device *ssp = NULL;

	mutex_lock(&ssp_lock);

	list_for_each_entry(ssp, &ssp_list, node) {
		if (ssp->port_id == port && ssp->use_count == 0) {
			ssp->use_count++;
			ssp->label = label;
			break;
		}
	}

	mutex_unlock(&ssp_lock);

	if (&ssp->node == &ssp_list)
		return NULL;

	return ssp;
}
EXPORT_SYMBOL(pxa_ssp_request);

struct ssp_device *pxa_ssp_request_of(const struct device_node *of_node,
				      const char *label)
{
	struct ssp_device *ssp = NULL;

	mutex_lock(&ssp_lock);

	list_for_each_entry(ssp, &ssp_list, node) {
		if (ssp->of_node == of_node && ssp->use_count == 0) {
			ssp->use_count++;
			ssp->label = label;
			break;
		}
	}

	mutex_unlock(&ssp_lock);

	if (&ssp->node == &ssp_list)
		return NULL;

	return ssp;
}
EXPORT_SYMBOL(pxa_ssp_request_of);

void pxa_ssp_free(struct ssp_device *ssp)
{
	mutex_lock(&ssp_lock);
	if (ssp->use_count) {
		ssp->use_count--;
		ssp->label = NULL;
	} else
		dev_err(ssp->dev, "device already free\n");
	mutex_unlock(&ssp_lock);
}
EXPORT_SYMBOL(pxa_ssp_free);

#ifdef CONFIG_OF
static const struct of_device_id pxa_ssp_of_ids[] = {
	{ .compatible = "mrvl,pxa25x-ssp",	.data = (void *) PXA25x_SSP },
	{ .compatible = "mvrl,pxa25x-nssp",	.data = (void *) PXA25x_NSSP },
	{ .compatible = "mrvl,pxa27x-ssp",	.data = (void *) PXA27x_SSP },
	{ .compatible = "mrvl,pxa3xx-ssp",	.data = (void *) PXA3xx_SSP },
	{ .compatible = "mvrl,pxa168-ssp",	.data = (void *) PXA168_SSP },
	{ .compatible = "mrvl,pxa910-ssp",	.data = (void *) PXA910_SSP },
	{ .compatible = "mrvl,ce4100-ssp",	.data = (void *) CE4100_SSP },
	{ },
};
MODULE_DEVICE_TABLE(of, pxa_ssp_of_ids);
#endif

static int pxa_ssp_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct ssp_device *ssp;
	struct device *dev = &pdev->dev;

	ssp = devm_kzalloc(dev, sizeof(struct ssp_device), GFP_KERNEL);
	if (ssp == NULL)
		return -ENOMEM;

	ssp->dev = dev;

	ssp->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "no memory resource defined\n");
		return -ENODEV;
	}

	res = devm_request_mem_region(dev, res->start, resource_size(res),
				      pdev->name);
	if (res == NULL) {
		dev_err(dev, "failed to request memory resource\n");
		return -EBUSY;
	}

	ssp->phys_base = res->start;

	ssp->mmio_base = devm_ioremap(dev, res->start, resource_size(res));
	if (ssp->mmio_base == NULL) {
		dev_err(dev, "failed to ioremap() registers\n");
		return -ENODEV;
	}

	ssp->irq = platform_get_irq(pdev, 0);
	if (ssp->irq < 0)
		return -ENODEV;

	if (dev->of_node) {
		const struct of_device_id *id =
			of_match_device(of_match_ptr(pxa_ssp_of_ids), dev);
		ssp->type = (int) id->data;
	} else {
		const struct platform_device_id *id =
			platform_get_device_id(pdev);
		ssp->type = (int) id->driver_data;

		/* PXA2xx/3xx SSP ports starts from 1 and the internal pdev->id
		 * starts from 0, do a translation here
		 */
		ssp->port_id = pdev->id + 1;
	}

	ssp->use_count = 0;
	ssp->of_node = dev->of_node;

	mutex_lock(&ssp_lock);
	list_add(&ssp->node, &ssp_list);
	mutex_unlock(&ssp_lock);

	platform_set_drvdata(pdev, ssp);

	return 0;
}

static int pxa_ssp_remove(struct platform_device *pdev)
{
	struct ssp_device *ssp = platform_get_drvdata(pdev);

	mutex_lock(&ssp_lock);
	list_del(&ssp->node);
	mutex_unlock(&ssp_lock);

	return 0;
}

static const struct platform_device_id ssp_id_table[] = {
	{ "pxa25x-ssp",		PXA25x_SSP },
	{ "pxa25x-nssp",	PXA25x_NSSP },
	{ "pxa27x-ssp",		PXA27x_SSP },
	{ "pxa3xx-ssp",		PXA3xx_SSP },
	{ "pxa168-ssp",		PXA168_SSP },
	{ "pxa910-ssp",		PXA910_SSP },
	{ },
};

static struct platform_driver pxa_ssp_driver = {
	.probe		= pxa_ssp_probe,
	.remove		= pxa_ssp_remove,
	.driver		= {
		.name		= "pxa2xx-ssp",
		.of_match_table	= of_match_ptr(pxa_ssp_of_ids),
	},
	.id_table	= ssp_id_table,
};

static int __init pxa_ssp_init(void)
{
	return platform_driver_register(&pxa_ssp_driver);
}

static void __exit pxa_ssp_exit(void)
{
	platform_driver_unregister(&pxa_ssp_driver);
}

arch_initcall(pxa_ssp_init);
module_exit(pxa_ssp_exit);

MODULE_DESCRIPTION("PXA SSP driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
