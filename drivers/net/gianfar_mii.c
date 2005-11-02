/* 
 * drivers/net/gianfar_mii.c
 *
 * Gianfar Ethernet Driver -- MIIM bus implementation
 * Provides Bus interface for MIIM regs
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <asm/ocp.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "gianfar.h"
#include "gianfar_mii.h"

/* Write value to the PHY at mii_id at register regnum,
 * on the bus, waiting until the write is done before returning.
 * All PHY configuration is done through the TSEC1 MIIM regs */
int gfar_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value)
{
	struct gfar_mii *regs = bus->priv;

	/* Set the PHY address and the register address we want to write */
	gfar_write(&regs->miimadd, (mii_id << 8) | regnum);

	/* Write out the value we want */
	gfar_write(&regs->miimcon, value);

	/* Wait for the transaction to finish */
	while (gfar_read(&regs->miimind) & MIIMIND_BUSY)
		cpu_relax();

	return 0;
}

/* Read the bus for PHY at addr mii_id, register regnum, and
 * return the value.  Clears miimcom first.  All PHY
 * configuration has to be done through the TSEC1 MIIM regs */
int gfar_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct gfar_mii *regs = bus->priv;
	u16 value;

	/* Set the PHY address and the register address we want to read */
	gfar_write(&regs->miimadd, (mii_id << 8) | regnum);

	/* Clear miimcom, and then initiate a read */
	gfar_write(&regs->miimcom, 0);
	gfar_write(&regs->miimcom, MII_READ_COMMAND);

	/* Wait for the transaction to finish */
	while (gfar_read(&regs->miimind) & (MIIMIND_NOTVALID | MIIMIND_BUSY))
		cpu_relax();

	/* Grab the value of the register from miimstat */
	value = gfar_read(&regs->miimstat);

	return value;
}


/* Reset the MIIM registers, and wait for the bus to free */
int gfar_mdio_reset(struct mii_bus *bus)
{
	struct gfar_mii *regs = bus->priv;
	unsigned int timeout = PHY_INIT_TIMEOUT;

	spin_lock_bh(&bus->mdio_lock);

	/* Reset the management interface */
	gfar_write(&regs->miimcfg, MIIMCFG_RESET);

	/* Setup the MII Mgmt clock speed */
	gfar_write(&regs->miimcfg, MIIMCFG_INIT_VALUE);

	/* Wait until the bus is free */
	while ((gfar_read(&regs->miimind) & MIIMIND_BUSY) &&
			timeout--)
		cpu_relax();

	spin_unlock_bh(&bus->mdio_lock);

	if(timeout <= 0) {
		printk(KERN_ERR "%s: The MII Bus is stuck!\n",
				bus->name);
		return -EBUSY;
	}

	return 0;
}


int gfar_mdio_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gianfar_mdio_data *pdata;
	struct gfar_mii *regs;
	struct mii_bus *new_bus;
	int err = 0;

	if (NULL == dev)
		return -EINVAL;

	new_bus = kmalloc(sizeof(struct mii_bus), GFP_KERNEL);

	if (NULL == new_bus)
		return -ENOMEM;

	new_bus->name = "Gianfar MII Bus",
	new_bus->read = &gfar_mdio_read,
	new_bus->write = &gfar_mdio_write,
	new_bus->reset = &gfar_mdio_reset,
	new_bus->id = pdev->id;

	pdata = (struct gianfar_mdio_data *)pdev->dev.platform_data;

	if (NULL == pdata) {
		printk(KERN_ERR "gfar mdio %d: Missing platform data!\n", pdev->id);
		return -ENODEV;
	}

	/* Set the PHY base address */
	regs = (struct gfar_mii *) ioremap(pdata->paddr, 
			sizeof (struct gfar_mii));

	if (NULL == regs) {
		err = -ENOMEM;
		goto reg_map_fail;
	}

	new_bus->priv = regs;

	new_bus->irq = pdata->irq;

	new_bus->dev = dev;
	dev_set_drvdata(dev, new_bus);

	err = mdiobus_register(new_bus);

	if (0 != err) {
		printk (KERN_ERR "%s: Cannot register as MDIO bus\n", 
				new_bus->name);
		goto bus_register_fail;
	}

	return 0;

bus_register_fail:
	iounmap((void *) regs);
reg_map_fail:
	kfree(new_bus);

	return err;
}


int gfar_mdio_remove(struct device *dev)
{
	struct mii_bus *bus = dev_get_drvdata(dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(dev, NULL);

	iounmap((void *) (&bus->priv));
	bus->priv = NULL;
	kfree(bus);

	return 0;
}

static struct device_driver gianfar_mdio_driver = {
	.name = "fsl-gianfar_mdio",
	.bus = &platform_bus_type,
	.probe = gfar_mdio_probe,
	.remove = gfar_mdio_remove,
};

int __init gfar_mdio_init(void)
{
	return driver_register(&gianfar_mdio_driver);
}

void __exit gfar_mdio_exit(void)
{
	driver_unregister(&gianfar_mdio_driver);
}
