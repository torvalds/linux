/*
 * drivers/net/ucc_geth_mii.c
 *
 * QE UCC Gigabit Ethernet Driver -- MII Management Bus Implementation
 * Provides Bus interface for MII Management regs in the UCC register space
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
 *
 * Authors: Li Yang <leoli@freescale.com>
 *	    Kim Phillips <kim.phillips@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

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
#include <linux/platform_device.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/fsl_devices.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/ucc.h>

#include "ucc_geth_mii.h"
#include "ucc_geth.h"

#define DEBUG
#ifdef DEBUG
#define vdbg(format, arg...) printk(KERN_DEBUG , format "\n" , ## arg)
#else
#define vdbg(format, arg...) do {} while(0)
#endif

#define MII_DRV_DESC "QE UCC Ethernet Controller MII Bus"
#define MII_DRV_NAME "fsl-uec_mdio"

/* Write value to the PHY for this device to the register at regnum, */
/* waiting until the write is done before it returns.  All PHY */
/* configuration has to be done through the master UEC MIIM regs */
int uec_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value)
{
	struct ucc_mii_mng __iomem *regs = (void __iomem *)bus->priv;

	/* Setting up the MII Mangement Address Register */
	out_be32(&regs->miimadd,
		 (mii_id << MIIMADD_PHY_ADDRESS_SHIFT) | regnum);

	/* Setting up the MII Mangement Control Register with the value */
	out_be32(&regs->miimcon, value);

	/* Wait till MII management write is complete */
	while ((in_be32(&regs->miimind)) & MIIMIND_BUSY)
		cpu_relax();

	return 0;
}

/* Reads from register regnum in the PHY for device dev, */
/* returning the value.  Clears miimcom first.  All PHY */
/* configuration has to be done through the TSEC1 MIIM regs */
int uec_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct ucc_mii_mng __iomem *regs = (void __iomem *)bus->priv;
	u16 value;

	/* Setting up the MII Mangement Address Register */
	out_be32(&regs->miimadd,
		 (mii_id << MIIMADD_PHY_ADDRESS_SHIFT) | regnum);

	/* Clear miimcom, perform an MII management read cycle */
	out_be32(&regs->miimcom, 0);
	out_be32(&regs->miimcom, MIIMCOM_READ_CYCLE);

	/* Wait till MII management write is complete */
	while ((in_be32(&regs->miimind)) & (MIIMIND_BUSY | MIIMIND_NOT_VALID))
		cpu_relax();

	/* Read MII management status  */
	value = in_be32(&regs->miimstat);

	return value;
}

/* Reset the MIIM registers, and wait for the bus to free */
static int uec_mdio_reset(struct mii_bus *bus)
{
	struct ucc_mii_mng __iomem *regs = (void __iomem *)bus->priv;
	unsigned int timeout = PHY_INIT_TIMEOUT;

	mutex_lock(&bus->mdio_lock);

	/* Reset the management interface */
	out_be32(&regs->miimcfg, MIIMCFG_RESET_MANAGEMENT);

	/* Setup the MII Mgmt clock speed */
	out_be32(&regs->miimcfg, MIIMCFG_MANAGEMENT_CLOCK_DIVIDE_BY_112);

	/* Wait until the bus is free */
	while ((in_be32(&regs->miimind) & MIIMIND_BUSY) && timeout--)
		cpu_relax();

	mutex_unlock(&bus->mdio_lock);

	if (timeout <= 0) {
		printk(KERN_ERR "%s: The MII Bus is stuck!\n", bus->name);
		return -EBUSY;
	}

	return 0;
}

static int uec_mdio_probe(struct of_device *ofdev, const struct of_device_id *match)
{
	struct device *device = &ofdev->dev;
	struct device_node *np = ofdev->node, *tempnp = NULL;
	struct device_node *child = NULL;
	struct ucc_mii_mng __iomem *regs;
	struct mii_bus *new_bus;
	struct resource res;
	int k, err = 0;

	new_bus = mdiobus_alloc();
	if (NULL == new_bus)
		return -ENOMEM;

	new_bus->name = "UCC Ethernet Controller MII Bus";
	new_bus->read = &uec_mdio_read;
	new_bus->write = &uec_mdio_write;
	new_bus->reset = &uec_mdio_reset;

	memset(&res, 0, sizeof(res));

	err = of_address_to_resource(np, 0, &res);
	if (err)
		goto reg_map_fail;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", res.start);

	new_bus->irq = kmalloc(32 * sizeof(int), GFP_KERNEL);

	if (NULL == new_bus->irq) {
		err = -ENOMEM;
		goto reg_map_fail;
	}

	for (k = 0; k < 32; k++)
		new_bus->irq[k] = PHY_POLL;

	while ((child = of_get_next_child(np, child)) != NULL) {
		int irq = irq_of_parse_and_map(child, 0);
		if (irq != NO_IRQ) {
			const u32 *id = of_get_property(child, "reg", NULL);
			new_bus->irq[*id] = irq;
		}
	}

	/* Set the base address */
	regs = ioremap(res.start, sizeof(struct ucc_mii_mng));

	if (NULL == regs) {
		err = -ENOMEM;
		goto ioremap_fail;
	}

	new_bus->priv = (void __force *)regs;

	new_bus->parent = device;
	dev_set_drvdata(device, new_bus);

	/* Read MII management master from device tree */
	while ((tempnp = of_find_compatible_node(tempnp, "network", "ucc_geth"))
	       != NULL) {
		struct resource tempres;

		err = of_address_to_resource(tempnp, 0, &tempres);
		if (err)
			goto bus_register_fail;

		/* if our mdio regs fall within this UCC regs range */
		if ((res.start >= tempres.start) &&
		    (res.end <= tempres.end)) {
			/* set this UCC to be the MII master */
			const u32 *id;

			id = of_get_property(tempnp, "cell-index", NULL);
			if (!id) {
				id = of_get_property(tempnp, "device-id", NULL);
				if (!id)
					goto bus_register_fail;
			}

			ucc_set_qe_mux_mii_mng(*id - 1);

			/* assign the TBI an address which won't
			 * conflict with the PHYs */
			out_be32(&regs->utbipar, UTBIPAR_INIT_TBIPA);
			break;
		}
	}

	err = mdiobus_register(new_bus);
	if (0 != err) {
		printk(KERN_ERR "%s: Cannot register as MDIO bus\n",
		       new_bus->name);
		goto bus_register_fail;
	}

	return 0;

bus_register_fail:
	iounmap(regs);
ioremap_fail:
	kfree(new_bus->irq);
reg_map_fail:
	mdiobus_free(new_bus);

	return err;
}

static int uec_mdio_remove(struct of_device *ofdev)
{
	struct device *device = &ofdev->dev;
	struct mii_bus *bus = dev_get_drvdata(device);

	mdiobus_unregister(bus);

	dev_set_drvdata(device, NULL);

	iounmap((void __iomem *)bus->priv);
	bus->priv = NULL;
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id uec_mdio_match[] = {
	{
		.type = "mdio",
		.compatible = "ucc_geth_phy",
	},
	{
		.compatible = "fsl,ucc-mdio",
	},
	{},
};

static struct of_platform_driver uec_mdio_driver = {
	.name	= MII_DRV_NAME,
	.probe	= uec_mdio_probe,
	.remove	= uec_mdio_remove,
	.match_table	= uec_mdio_match,
};

int __init uec_mdio_init(void)
{
	return of_register_platform_driver(&uec_mdio_driver);
}

/* called from __init ucc_geth_init, therefore can not be __exit */
void uec_mdio_exit(void)
{
	of_unregister_platform_driver(&uec_mdio_driver);
}
