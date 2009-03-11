/*
 * drivers/net/gianfar_mii.c
 *
 * Gianfar Ethernet Driver -- MIIM bus implementation
 * Provides Bus interface for MIIM regs
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
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
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "gianfar.h"
#include "gianfar_mii.h"

/*
 * Write value to the PHY at mii_id at register regnum,
 * on the bus attached to the local interface, which may be different from the
 * generic mdio bus (tied to a single interface), waiting until the write is
 * done before returning. This is helpful in programming interfaces like
 * the TBI which control interfaces like onchip SERDES and are always tied to
 * the local mdio pins, which may not be the same as system mdio bus, used for
 * controlling the external PHYs, for example.
 */
int gfar_local_mdio_write(struct gfar_mii __iomem *regs, int mii_id,
			  int regnum, u16 value)
{
	/* Set the PHY address and the register address we want to write */
	gfar_write(&regs->miimadd, (mii_id << 8) | regnum);

	/* Write out the value we want */
	gfar_write(&regs->miimcon, value);

	/* Wait for the transaction to finish */
	while (gfar_read(&regs->miimind) & MIIMIND_BUSY)
		cpu_relax();

	return 0;
}

/*
 * Read the bus for PHY at addr mii_id, register regnum, and
 * return the value.  Clears miimcom first.  All PHY operation
 * done on the bus attached to the local interface,
 * which may be different from the generic mdio bus
 * This is helpful in programming interfaces like
 * the TBI which, inturn, control interfaces like onchip SERDES
 * and are always tied to the local mdio pins, which may not be the
 * same as system mdio bus, used for controlling the external PHYs, for eg.
 */
int gfar_local_mdio_read(struct gfar_mii __iomem *regs, int mii_id, int regnum)
{
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

/* Write value to the PHY at mii_id at register regnum,
 * on the bus, waiting until the write is done before returning.
 * All PHY configuration is done through the TSEC1 MIIM regs */
int gfar_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value)
{
	struct gfar_mii __iomem *regs = (void __iomem *)bus->priv;

	/* Write to the local MII regs */
	return(gfar_local_mdio_write(regs, mii_id, regnum, value));
}

/* Read the bus for PHY at addr mii_id, register regnum, and
 * return the value.  Clears miimcom first.  All PHY
 * configuration has to be done through the TSEC1 MIIM regs */
int gfar_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct gfar_mii __iomem *regs = (void __iomem *)bus->priv;

	/* Read the local MII regs */
	return(gfar_local_mdio_read(regs, mii_id, regnum));
}

/* Reset the MIIM registers, and wait for the bus to free */
static int gfar_mdio_reset(struct mii_bus *bus)
{
	struct gfar_mii __iomem *regs = (void __iomem *)bus->priv;
	unsigned int timeout = PHY_INIT_TIMEOUT;

	mutex_lock(&bus->mdio_lock);

	/* Reset the management interface */
	gfar_write(&regs->miimcfg, MIIMCFG_RESET);

	/* Setup the MII Mgmt clock speed */
	gfar_write(&regs->miimcfg, MIIMCFG_INIT_VALUE);

	/* Wait until the bus is free */
	while ((gfar_read(&regs->miimind) & MIIMIND_BUSY) &&
			--timeout)
		cpu_relax();

	mutex_unlock(&bus->mdio_lock);

	if(timeout == 0) {
		printk(KERN_ERR "%s: The MII Bus is stuck!\n",
				bus->name);
		return -EBUSY;
	}

	return 0;
}

/* Allocate an array which provides irq #s for each PHY on the given bus */
static int *create_irq_map(struct device_node *np)
{
	int *irqs;
	int i;
	struct device_node *child = NULL;

	irqs = kcalloc(PHY_MAX_ADDR, sizeof(int), GFP_KERNEL);

	if (!irqs)
		return NULL;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		irqs[i] = PHY_POLL;

	while ((child = of_get_next_child(np, child)) != NULL) {
		int irq = irq_of_parse_and_map(child, 0);
		const u32 *id;

		if (irq == NO_IRQ)
			continue;

		id = of_get_property(child, "reg", NULL);

		if (!id)
			continue;

		if (*id < PHY_MAX_ADDR && *id >= 0)
			irqs[*id] = irq;
		else
			printk(KERN_WARNING "%s: "
					"%d is not a valid PHY address\n",
					np->full_name, *id);
	}

	return irqs;
}


void gfar_mdio_bus_name(char *name, struct device_node *np)
{
	const u32 *reg;

	reg = of_get_property(np, "reg", NULL);

	snprintf(name, MII_BUS_ID_SIZE, "%s@%x", np->name, reg ? *reg : 0);
}

/* Scan the bus in reverse, looking for an empty spot */
static int gfar_mdio_find_free(struct mii_bus *new_bus)
{
	int i;

	for (i = PHY_MAX_ADDR; i > 0; i--) {
		u32 phy_id;

		if (get_phy_id(new_bus, i, &phy_id))
			return -1;

		if (phy_id == 0xffffffff)
			break;
	}

	return i;
}

static int gfar_mdio_probe(struct of_device *ofdev,
		const struct of_device_id *match)
{
	struct gfar_mii __iomem *regs;
	struct gfar __iomem *enet_regs;
	struct mii_bus *new_bus;
	int err = 0;
	u64 addr, size;
	struct device_node *np = ofdev->node;
	struct device_node *tbi;
	int tbiaddr = -1;

	new_bus = mdiobus_alloc();
	if (NULL == new_bus)
		return -ENOMEM;

	device_init_wakeup(&ofdev->dev, 1);

	new_bus->name = "Gianfar MII Bus",
	new_bus->read = &gfar_mdio_read,
	new_bus->write = &gfar_mdio_write,
	new_bus->reset = &gfar_mdio_reset,
	gfar_mdio_bus_name(new_bus->id, np);

	/* Set the PHY base address */
	addr = of_translate_address(np, of_get_address(np, 0, &size, NULL));
	regs = ioremap(addr, size);

	if (NULL == regs) {
		err = -ENOMEM;
		goto err_free_bus;
	}

	new_bus->priv = (void __force *)regs;

	new_bus->irq = create_irq_map(np);

	if (new_bus->irq == NULL) {
		err = -ENOMEM;
		goto err_unmap_regs;
	}

	new_bus->parent = &ofdev->dev;
	dev_set_drvdata(&ofdev->dev, new_bus);

	/*
	 * This is mildly evil, but so is our hardware for doing this.
	 * Also, we have to cast back to struct gfar_mii because of
	 * definition weirdness done in gianfar.h.
	 */
	enet_regs = (struct gfar __iomem *)
		((char *)regs - offsetof(struct gfar, gfar_mii_regs));

	for_each_child_of_node(np, tbi) {
		if (!strncmp(tbi->type, "tbi-phy", 8))
			break;
	}

	if (tbi) {
		const u32 *prop = of_get_property(tbi, "reg", NULL);

		if (prop)
			tbiaddr = *prop;
	}

	if (tbiaddr == -1) {
		gfar_write(&enet_regs->tbipa, 0);

		tbiaddr = gfar_mdio_find_free(new_bus);
	}

	/*
	 * We define TBIPA at 0 to be illegal, opting to fail for boards that
	 * have PHYs at 1-31, rather than change tbipa and rescan.
	 */
	if (tbiaddr == 0) {
		err = -EBUSY;

		goto err_free_irqs;
	}

	gfar_write(&enet_regs->tbipa, tbiaddr);

	/*
	 * The TBIPHY-only buses will find PHYs at every address,
	 * so we mask them all but the TBI
	 */
	if (!of_device_is_compatible(np, "fsl,gianfar-mdio"))
		new_bus->phy_mask = ~(1 << tbiaddr);

	err = mdiobus_register(new_bus);

	if (err != 0) {
		printk (KERN_ERR "%s: Cannot register as MDIO bus\n",
				new_bus->name);
		goto err_free_irqs;
	}

	return 0;

err_free_irqs:
	kfree(new_bus->irq);
err_unmap_regs:
	iounmap(regs);
err_free_bus:
	mdiobus_free(new_bus);

	return err;
}


static int gfar_mdio_remove(struct of_device *ofdev)
{
	struct mii_bus *bus = dev_get_drvdata(&ofdev->dev);

	mdiobus_unregister(bus);

	dev_set_drvdata(&ofdev->dev, NULL);

	iounmap((void __iomem *)bus->priv);
	bus->priv = NULL;
	kfree(bus->irq);
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id gfar_mdio_match[] =
{
	{
		.compatible = "fsl,gianfar-mdio",
	},
	{
		.compatible = "fsl,gianfar-tbi",
	},
	{
		.type = "mdio",
		.compatible = "gianfar",
	},
	{},
};

static struct of_platform_driver gianfar_mdio_driver = {
	.name = "fsl-gianfar_mdio",
	.match_table = gfar_mdio_match,

	.probe = gfar_mdio_probe,
	.remove = gfar_mdio_remove,
};

int __init gfar_mdio_init(void)
{
	return of_register_platform_driver(&gianfar_mdio_driver);
}

void gfar_mdio_exit(void)
{
	of_unregister_platform_driver(&gianfar_mdio_driver);
}
