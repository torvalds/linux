/*
 * Copyright (c) 1996-2002 Russell King.
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/ecard.h>

static const struct ide_port_info rapide_port_info = {
	.host_flags		= IDE_HFLAG_MMIO | IDE_HFLAG_NO_DMA,
	.chipset		= ide_generic,
};

static void rapide_setup_ports(struct ide_hw *hw, void __iomem *base,
			       void __iomem *ctrl, unsigned int sz, int irq)
{
	unsigned long port = (unsigned long)base;
	int i;

	for (i = 0; i <= 7; i++) {
		hw->io_ports_array[i] = port;
		port += sz;
	}
	hw->io_ports.ctl_addr = (unsigned long)ctrl;
	hw->irq = irq;
}

static int __devinit
rapide_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	void __iomem *base;
	struct ide_host *host;
	int ret;
	struct ide_hw hw, *hws[] = { &hw };

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	base = ecardm_iomap(ec, ECARD_RES_MEMC, 0, 0);
	if (!base) {
		ret = -ENOMEM;
		goto release;
	}

	memset(&hw, 0, sizeof(hw));
	rapide_setup_ports(&hw, base, base + 0x818, 1 << 6, ec->irq);
	hw.dev = &ec->dev;

	ret = ide_host_add(&rapide_port_info, hws, 1, &host);
	if (ret)
		goto release;

	ecard_set_drvdata(ec, host);
	goto out;

 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit rapide_remove(struct expansion_card *ec)
{
	struct ide_host *host = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);

	ide_host_remove(host);

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

static void __exit rapide_exit(void)
{
	ecard_remove_driver(&rapide_driver);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Yellowstone RAPIDE driver");

module_init(rapide_init);
module_exit(rapide_exit);
