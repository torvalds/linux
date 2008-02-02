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
#include <linux/pata_platform.h>
#include <linux/platform_device.h>
#include <linux/io.h>

static void __devinit plat_ide_setup_ports(hw_regs_t *hw,
					   void __iomem *base,
					   void __iomem *ctrl,
					   struct pata_platform_info *pdata,
					   int irq)
{
	unsigned long port = (unsigned long)base;
	int i;

	hw->io_ports[IDE_DATA_OFFSET] = port;

	port += (1 << pdata->ioport_shift);
	for (i = IDE_ERROR_OFFSET; i <= IDE_STATUS_OFFSET;
	     i++, port += (1 << pdata->ioport_shift))
		hw->io_ports[i] = port;

	hw->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;

	hw->irq = irq;

	hw->chipset = ide_generic;
}

static int __devinit plat_ide_probe(struct platform_device *pdev)
{
	struct resource *res_base, *res_alt, *res_irq;
	void __iomem *base, *alt_base;
	ide_hwif_t *hwif;
	struct pata_platform_info *pdata;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	int ret = 0;
	int mmio = 0;
	hw_regs_t hw;

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
			res_base->start, res_base->end - res_base->start + 1);
		alt_base = devm_ioremap(&pdev->dev,
			res_alt->start, res_alt->end - res_alt->start + 1);
	} else {
		base = devm_ioport_map(&pdev->dev,
			res_base->start, res_base->end - res_base->start + 1);
		alt_base = devm_ioport_map(&pdev->dev,
			res_alt->start, res_alt->end - res_alt->start + 1);
	}

	hwif = ide_find_port((unsigned long)base);
	if (!hwif) {
		ret = -ENODEV;
		goto out;
	}

	memset(&hw, 0, sizeof(hw));
	plat_ide_setup_ports(&hw, base, alt_base, pdata, res_irq->start);
	hw.dev = &pdev->dev;

	ide_init_port_hw(hwif, &hw);

	if (mmio) {
		hwif->mmio = 1;
		default_hwif_mmiops(hwif);
	}

	idx[0] = hwif->index;

	ide_device_add(idx, NULL);

	platform_set_drvdata(pdev, hwif);

	return 0;

out:
	return ret;
}

static int __devexit plat_ide_remove(struct platform_device *pdev)
{
	ide_hwif_t *hwif = pdev->dev.driver_data;

	ide_unregister(hwif->index, 0, 0);

	return 0;
}

static struct platform_driver platform_ide_driver = {
	.driver = {
		.name = "pata_platform",
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

module_init(platform_ide_init);
module_exit(platform_ide_exit);
