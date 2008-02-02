/*
 * Copyright (c) 1996-2002 Russell King.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/ecard.h>

static void rapide_setup_ports(hw_regs_t *hw, void __iomem *base,
			       void __iomem *ctrl, unsigned int sz, int irq)
{
	unsigned long port = (unsigned long)base;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = port;
		port += sz;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = (unsigned long)ctrl;
	hw->irq = irq;
}

static int __devinit
rapide_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	ide_hwif_t *hwif;
	void __iomem *base;
	int ret;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };
	hw_regs_t hw;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	base = ecardm_iomap(ec, ECARD_RES_MEMC, 0, 0);
	if (!base) {
		ret = -ENOMEM;
		goto release;
	}

	hwif = ide_find_port((unsigned long)base);
	if (hwif) {
		memset(&hw, 0, sizeof(hw));
		rapide_setup_ports(&hw, base, base + 0x818, 1 << 6, ec->irq);
		hw.chipset = ide_generic;
		hw.dev = &ec->dev;

		ide_init_port_hw(hwif, &hw);

		hwif->mmio = 1;
		default_hwif_mmiops(hwif);

		idx[0] = hwif->index;

		ide_device_add(idx, NULL);

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

	ide_unregister(hwif->index);

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
