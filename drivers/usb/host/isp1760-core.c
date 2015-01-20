/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "isp1760-core.h"
#include "isp1760-hcd.h"

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags)
{
	struct isp1760_device *isp;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	/* prevent usb-core allocating DMA pages */
	dev->dma_mask = NULL;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(isp->regs))
		return PTR_ERR(isp->regs);

	ret = isp1760_hcd_register(&isp->hcd, isp->regs, mem, irq,
				   irqflags | IRQF_SHARED, dev, devflags);
	if (ret < 0)
		return ret;

	dev_set_drvdata(dev, isp);

	return 0;
}

void isp1760_unregister(struct device *dev)
{
	struct isp1760_device *isp = dev_get_drvdata(dev);

	isp1760_hcd_unregister(&isp->hcd);
}

MODULE_DESCRIPTION("Driver for the ISP1760 USB-controller from NXP");
MODULE_AUTHOR("Sebastian Siewior <bigeasy@linuxtronix.de>");
MODULE_LICENSE("GPL v2");
