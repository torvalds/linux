/*
 * linux/drivers/ide/ide-pnp.c
 *
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
	int index;

	if (!(pnp_port_valid(dev, 0) && pnp_port_valid(dev, 1) && pnp_irq_valid(dev, 0)))
		return -1;

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, pnp_port_start(dev, 0),
				pnp_port_start(dev, 1));
	hw.irq = pnp_irq(dev, 0);
	hw.dma = NO_DMA;

	index = ide_register_hw(&hw, &hwif);

	if (index != -1) {
	    	printk(KERN_INFO "ide%d: generic PnP IDE interface\n", index);
		pnp_set_drvdata(dev,hwif);
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

void __init pnpide_init(void)
{
	pnp_register_driver(&idepnp_driver);
}

void __exit pnpide_exit(void)
{
	pnp_unregister_driver(&idepnp_driver);
}
