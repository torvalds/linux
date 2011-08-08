/*
 * Platform IDE driver
 *
 * Copyright (C) 2007 MontaVista Software
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ide.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/ata_platform.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>

static void __devinit plat_ide_setup_ports(struct ide_hw *hw,
					   void __iomem *base,
					   void __iomem *ctrl,
					   struct pata_platform_info *pdata,
					   int irq)
{
	unsigned long port = (unsigned long)base;
	int i;

	hw->io_ports.data_addr = port;

	port += (1 << pdata->ioport_shift);
	for (i = 1; i <= 7;
	     i++, port += (1 << pdata->ioport_shift))
		hw->io_ports_array[i] = port;

	hw->io_ports.ctl_addr = (unsigned long)ctrl;

	hw->irq = irq;
}

static const struct ide_port_info platform_ide_port_info = {
	.host_flags		= IDE_HFLAG_NO_DMA,
	.chipset		= ide_generic,
};

static int __devinit plat_ide_probe(struct platform_device *pdev)
{
	struct resource *res_base, *res_alt, *res_irq;
	void __iomem *base, *alt_base;
	struct pata_platform_info *pdata;
	struct ide_host *host;
	int ret = 0, mmio = 0;
	struct ide_hw hw, *hws[] = { &hw };
	struct ide_port_info d = platform_ide_port_info;

	pdata = pdev->dev.platform_data;

	/* get a pointer to the register memory */
	res_base = platform_get_resource(pdev, IORESOURCE_IO, 0);
	res_alt = platform_get_resource(pdev, IORESOURCE_IO, 1);

	if (!res_base || !res_alt) {
		res_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		res_alt = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res_base || !res_alt) {
			ret = -ENOMEM;
			goto out;
		}
		mmio = 1;
	}

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq) {
		ret = -EINVAL;
		goto out;
	}

	if (mmio) {
		base = devm_ioremap(&pdev->dev,
			res_base->start, resource_size(res_base));
		alt_base = devm_ioremap(&pdev->dev,
			res_alt->start, resource_size(res_alt));
	} else {
		base = devm_ioport_map(&pdev->dev,
			res_base->start, resource_size(res_base));
		alt_base = devm_ioport_map(&pdev->dev,
			res_alt->start, resource_size(res_alt));
	}

	memset(&hw, 0, sizeof(hw));
	plat_ide_setup_ports(&hw, base, alt_base, pdata, res_irq->start);
	hw.dev = &pdev->dev;

	d.irq_flags = res_irq->flags & IRQF_TRIGGER_MASK;
	if (res_irq->flags & IORESOURCE_IRQ_SHAREABLE)
		d.irq_flags |= IRQF_SHARED;

	if (mmio)
		d.host_flags |= IDE_HFLAG_MMIO;

	ret = ide_host_add(&d, hws, 1, &host);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, host);

	return 0;

out:
	return ret;
}

static int __devexit plat_ide_remove(struct platform_device *pdev)
{
	struct ide_host *host = dev_get_drvdata(&pdev->dev);

	ide_host_remove(host);

	return 0;
}

static struct platform_driver platform_ide_driver = {
	.driver = {
		.name = "pata_platform",
		.owner = THIS_MODULE,
	},
	.probe = plat_ide_probe,
	.remove = __devexit_p(plat_ide_remove),
};

static int __init platform_ide_init(void)
{
	return platform_driver_register(&platform_ide_driver);
}

static void __exit platform_ide_exit(void)
{
	platform_driver_unregister(&platform_ide_driver);
}

MODULE_DESCRIPTION("Platform IDE driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pata_platform");

module_init(platform_ide_init);
module_exit(platform_ide_exit);
