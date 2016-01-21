/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2014 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ccp.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/acpi.h>

#include "ccp-dev.h"

struct ccp_platform {
	int coherent;
};

static int ccp_get_irq(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	int ret;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ccp->irq = ret;
	ret = request_irq(ccp->irq, ccp_irq_handler, 0, "ccp", dev);
	if (ret) {
		dev_notice(dev, "unable to allocate IRQ (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int ccp_get_irqs(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	int ret;

	ret = ccp_get_irq(ccp);
	if (!ret)
		return 0;

	/* Couldn't get an interrupt */
	dev_notice(dev, "could not enable interrupts (%d)\n", ret);

	return ret;
}

static void ccp_free_irqs(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;

	free_irq(ccp->irq, dev);
}

static struct resource *ccp_find_mmio_area(struct ccp_device *ccp)
{
	struct device *dev = ccp->dev;
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	struct resource *ior;

	ior = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (ior && (resource_size(ior) >= 0x800))
		return ior;

	return NULL;
}

static int ccp_platform_probe(struct platform_device *pdev)
{
	struct ccp_device *ccp;
	struct ccp_platform *ccp_platform;
	struct device *dev = &pdev->dev;
	enum dev_dma_attr attr;
	struct resource *ior;
	int ret;

	ret = -ENOMEM;
	ccp = ccp_alloc_struct(dev);
	if (!ccp)
		goto e_err;

	ccp_platform = devm_kzalloc(dev, sizeof(*ccp_platform), GFP_KERNEL);
	if (!ccp_platform)
		goto e_err;

	ccp->dev_specific = ccp_platform;
	ccp->get_irq = ccp_get_irqs;
	ccp->free_irq = ccp_free_irqs;

	ior = ccp_find_mmio_area(ccp);
	ccp->io_map = devm_ioremap_resource(dev, ior);
	if (IS_ERR(ccp->io_map)) {
		ret = PTR_ERR(ccp->io_map);
		goto e_err;
	}
	ccp->io_regs = ccp->io_map;

	attr = device_get_dma_attr(dev);
	if (attr == DEV_DMA_NOT_SUPPORTED) {
		dev_err(dev, "DMA is not supported");
		goto e_err;
	}

	ccp_platform->coherent = (attr == DEV_DMA_COHERENT);
	if (ccp_platform->coherent)
		ccp->axcache = CACHE_WB_NO_ALLOC;
	else
		ccp->axcache = CACHE_NONE;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		dev_err(dev, "dma_set_mask_and_coherent failed (%d)\n", ret);
		goto e_err;
	}

	dev_set_drvdata(dev, ccp);

	ret = ccp_init(ccp);
	if (ret)
		goto e_err;

	dev_notice(dev, "enabled\n");

	return 0;

e_err:
	dev_notice(dev, "initialization failed\n");
	return ret;
}

static int ccp_platform_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ccp_device *ccp = dev_get_drvdata(dev);

	ccp_destroy(ccp);

	dev_notice(dev, "disabled\n");

	return 0;
}

#ifdef CONFIG_PM
static int ccp_platform_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	struct device *dev = &pdev->dev;
	struct ccp_device *ccp = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	ccp->suspending = 1;

	/* Wake all the queue kthreads to prepare for suspend */
	for (i = 0; i < ccp->cmd_q_count; i++)
		wake_up_process(ccp->cmd_q[i].kthread);

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	/* Wait for all queue kthreads to say they're done */
	while (!ccp_queues_suspended(ccp))
		wait_event_interruptible(ccp->suspend_queue,
					 ccp_queues_suspended(ccp));

	return 0;
}

static int ccp_platform_resume(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ccp_device *ccp = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&ccp->cmd_lock, flags);

	ccp->suspending = 0;

	/* Wake up all the kthreads */
	for (i = 0; i < ccp->cmd_q_count; i++) {
		ccp->cmd_q[i].suspended = 0;
		wake_up_process(ccp->cmd_q[i].kthread);
	}

	spin_unlock_irqrestore(&ccp->cmd_lock, flags);

	return 0;
}
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id ccp_acpi_match[] = {
	{ "AMDI0C00", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ccp_acpi_match);
#endif

#ifdef CONFIG_OF
static const struct of_device_id ccp_of_match[] = {
	{ .compatible = "amd,ccp-seattle-v1a" },
	{ },
};
MODULE_DEVICE_TABLE(of, ccp_of_match);
#endif

static struct platform_driver ccp_platform_driver = {
	.driver = {
		.name = "ccp",
#ifdef CONFIG_ACPI
		.acpi_match_table = ccp_acpi_match,
#endif
#ifdef CONFIG_OF
		.of_match_table = ccp_of_match,
#endif
	},
	.probe = ccp_platform_probe,
	.remove = ccp_platform_remove,
#ifdef CONFIG_PM
	.suspend = ccp_platform_suspend,
	.resume = ccp_platform_resume,
#endif
};

int ccp_platform_init(void)
{
	return platform_driver_register(&ccp_platform_driver);
}

void ccp_platform_exit(void)
{
	platform_driver_unregister(&ccp_platform_driver);
}
