/*
 * This file provides autodetection for ISA PnP IDE interfaces.
 * It was tested with "ESS ES1868 Plug and Play AudioDrive" IDE interface.
 *
 * Copyright (C) 2000 Andrey Panin <pazke@donpac.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
 */

#include <linux/init.h>
#include <linux/pnp.h>
#include <linux/ide.h>

/* Add your devices here :)) */
static struct pnp_device_id idepnp_devices[] = {
  	/* Generic ESDI/IDE/ATA compatible hard disk controller */
	{.id = "PNP0600", .driver_data = 0},
	{.id = ""}
};

static int idepnp_probe(struct pnp_dev * dev, const struct pnp_device_id *dev_id)
{
	hw_regs_t hw;
	ide_hwif_t *hwif;

	if (!(pnp_port_valid(dev, 0) && pnp_port_valid(dev, 1) && pnp_irq_valid(dev, 0)))
		return -1;

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, pnp_port_start(dev, 0),
				pnp_port_start(dev, 1));
	hw.irq = pnp_irq(dev, 0);

	hwif = ide_find_port(hw.io_ports[IDE_DATA_OFFSET]);
	if (hwif) {
		u8 index = hwif->index;
		u8 idx[4] = { index, 0xff, 0xff, 0xff };

		ide_init_port_data(hwif, index);
		ide_init_port_hw(hwif, &hw);

		printk(KERN_INFO "ide%d: generic PnP IDE interface\n", index);
		pnp_set_drvdata(dev,hwif);

		ide_device_add(idx);

		return 0;
	}

	return -1;
}

static void idepnp_remove(struct pnp_dev * dev)
{
	ide_hwif_t *hwif = pnp_get_drvdata(dev);
	if (hwif) {
		ide_unregister(hwif->index);
	} else
		printk(KERN_ERR "idepnp: Unable to remove device, please report.\n");
}

static struct pnp_driver idepnp_driver = {
	.name		= "ide",
	.id_table	= idepnp_devices,
	.probe		= idepnp_probe,
	.remove		= idepnp_remove,
};

static int __init pnpide_init(void)
{
	return pnp_register_driver(&idepnp_driver);
}

static void __exit pnpide_exit(void)
{
	pnp_unregister_driver(&idepnp_driver);
}

module_init(pnpide_init);
module_exit(pnpide_exit);
