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

static struct {
	void __iomem *plat_ide_mapbase;
	void __iomem *plat_ide_alt_mapbase;
	ide_hwif_t *hwif;
	int index;
} hwif_prop;

static ide_hwif_t *__devinit plat_ide_locate_hwif(void __iomem *base,
	    void __iomem *ctrl, struct pata_platform_info *pdata, int irq,
	    int mmio)
{
	unsigned long port = (unsigned long)base;
	ide_hwif_t *hwif = ide_find_port(port);
	int i;

	if (hwif == NULL)
		goto out;

	hwif->io_ports[IDE_DATA_OFFSET] = port;

	port += (1 << pdata->ioport_shift);
	for (i = IDE_ERROR_OFFSET; i <= IDE_STATUS_OFFSET;
	     i++, port += (1 << pdata->ioport_shift))
		hwif->io_ports[i] = port;

	hwif->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;

	hwif->irq = irq;

	hwif->chipset = ide_generic;

	if (mmio) {
		hwif->mmio = 1;
		default_hwif_mmiops(hwif);
	}

	hwif_prop.hwif = hwif;
	hwif_prop.index = hwif->index;
out:
	return hwif;
}

static int __devinit plat_ide_probe(struct platform_device *pdev)
{
	struct resource *res_base, *res_alt, *res_irq;
	ide_hwif_t *hwif;
	struct pata_platform_info *pdata;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	int ret = 0;
	int mmio = 0;

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
		hwif_prop.plat_ide_mapbase = devm_ioremap(&pdev->dev,
			res_base->start, res_base->end - res_base->start + 1);
		hwif_prop.plat_ide_alt_mapbase = devm_ioremap(&pdev->dev,
			res_alt->start, res_alt->end - res_alt->start + 1);
	} else {
		hwif_prop.plat_ide_mapbase = devm_ioport_map(&pdev->dev,
			res_base->start, res_base->end - res_base->start + 1);
		hwif_prop.plat_ide_alt_mapbase = devm_ioport_map(&pdev->dev,
			res_alt->start, res_alt->end - res_alt->start + 1);
	}

	hwif = plat_ide_locate_hwif(hwif_prop.plat_ide_mapbase,
	         hwif_prop.plat_ide_alt_mapbase, pdata, res_irq->start, mmio);

	if (!hwif) {
		ret = -ENODEV;
		goto out;
	}
	hwif->gendev.parent = &pdev->dev;
	hwif->noprobe = 0;

	idx[0] = hwif->index;

	ide_device_add(idx);

	platform_set_drvdata(pdev, hwif);

	return 0;

out:
	return ret;
}

static int __devexit plat_ide_remove(struct platform_device *pdev)
{
	ide_hwif_t *hwif = pdev->dev.driver_data;

	if (hwif != hwif_prop.hwif) {
		dev_printk(KERN_DEBUG, &pdev->dev, "%s: hwif value error",
		           pdev->name);
	} else {
		ide_unregister(hwif_prop.index);
		hwif_prop.index = 0;
		hwif_prop.hwif = NULL;
	}

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
