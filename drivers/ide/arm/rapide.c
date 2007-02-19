/*
 * linux/drivers/ide/arm/rapide.c
 *
 * Copyright (c) 1996-2002 Russell King.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/ecard.h>

/*
 * Something like this really should be in generic code, but isn't.
 */
static ide_hwif_t *
rapide_locate_hwif(void __iomem *base, void __iomem *ctrl, unsigned int sz, int irq)
{
	unsigned long port = (unsigned long)base;
	ide_hwif_t *hwif;
	int index, i;

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = ide_hwifs + index;
		if (hwif->io_ports[IDE_DATA_OFFSET] == port)
			goto found;
	}

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = ide_hwifs + index;
		if (hwif->io_ports[IDE_DATA_OFFSET] == 0)
			goto found;
	}

	return NULL;

 found:
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hwif->hw.io_ports[i] = port;
		hwif->io_ports[i] = port;
		port += sz;
	}
	hwif->hw.io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;
	hwif->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;
	hwif->hw.irq = hwif->irq = irq;
	hwif->mmio = 1;
	default_hwif_mmiops(hwif);

	return hwif;
}

static int __devinit
rapide_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	ide_hwif_t *hwif;
	void __iomem *base;
	int ret;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	base = ioremap(ecard_resource_start(ec, ECARD_RES_MEMC),
		       ecard_resource_len(ec, ECARD_RES_MEMC));
	if (!base) {
		ret = -ENOMEM;
		goto release;
	}

	hwif = rapide_locate_hwif(base, base + 0x818, 1 << 6, ec->irq);
	if (hwif) {
		hwif->hwif_data = base;
		hwif->gendev.parent = &ec->dev;
		hwif->noprobe = 0;
		probe_hwif_init(hwif);
		create_proc_ide_interfaces();
		ecard_set_drvdata(ec, hwif);
		goto out;
	}

	iounmap(base);
 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit rapide_remove(struct expansion_card *ec)
{
	ide_hwif_t *hwif = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);

	/* there must be a better way */
	ide_unregister(hwif - ide_hwifs);
	iounmap(hwif->hwif_data);
	ecard_release_resources(ec);
}

static struct ecard_id rapide_ids[] = {
	{ MANU_YELLOWSTONE, PROD_YELLOWSTONE_RAPIDE32 },
	{ 0xffff, 0xffff }
};

static struct ecard_driver rapide_driver = {
	.probe		= rapide_probe,
	.remove		= __devexit_p(rapide_remove),
	.id_table	= rapide_ids,
	.drv = {
		.name	= "rapide",
	},
};

static int __init rapide_init(void)
{
	return ecard_register_driver(&rapide_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Yellowstone RAPIDE driver");

module_init(rapide_init);
