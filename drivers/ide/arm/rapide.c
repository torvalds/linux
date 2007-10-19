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

static ide_hwif_t *
rapide_locate_hwif(void __iomem *base, void __iomem *ctrl, unsigned int sz, int irq)
{
	unsigned long port = (unsigned long)base;
	ide_hwif_t *hwif = ide_find_port(port);
	int i;

	if (hwif == NULL)
		goto out;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hwif->io_ports[i] = port;
		port += sz;
	}
	hwif->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;
	hwif->irq = irq;
	hwif->mmio = 1;
	default_hwif_mmiops(hwif);
out:
	return hwif;
}

static int __devinit
rapide_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	ide_hwif_t *hwif;
	void __iomem *base;
	int ret;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	base = ecardm_iomap(ec, ECARD_RES_MEMC, 0, 0);
	if (!base) {
		ret = -ENOMEM;
		goto release;
	}

	hwif = rapide_locate_hwif(base, base + 0x818, 1 << 6, ec->irq);
	if (hwif) {
		hwif->hwif_data = base;
		hwif->gendev.parent = &ec->dev;
		hwif->noprobe = 0;

		idx[0] = hwif->index;

		ide_device_add(idx);

		ecard_set_drvdata(ec, hwif);
		goto out;
	}

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
