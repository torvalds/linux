/*
 * Freescale PowerQUICC Ethernet Driver -- MIIM bus implementation
 * Provides Bus interface for MIIM regs
 *
 * Author: Andy Fleming <afleming@freescale.com>
 *
 * Copyright (c) 2002-2004,2008 Freescale Semiconductor, Inc.
 *
 * Based on gianfar_mii.c and ucc_geth_mii.c (Li Yang, Kim Phillips)
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
#include <asm/ucc.h>

#include "gianfar.h"
#include "fsl_pq_mdio.h"

/*
 * Write value to the PHY at mii_id at register regnum,
 * on the bus attached to the local interface, which may be different from the
 * generic mdio bus (tied to a single interface), waiting until the write is
 * done before returning. This is helpful in programming interfaces like
 * the TBI which control interfaces like onchip SERDES and are always tied to
 * the local mdio pins, which may not be the same as system mdio bus, used for
 * controlling the external PHYs, for example.
 */
int fsl_pq_local_mdio_write(struct fsl_pq_mdio __iomem *regs, int mii_id,
		int regnum, u16 value)
{
	/* Set the PHY address and the register address we want to write */
	out_be32(&regs->miimadd, (mii_id << 8) | regnum);

	/* Write out the value we want */
	out_be32(&regs->miimcon, value);

	/* Wait for the transaction to finish */
	while (in_be32(&regs->miimind) & MIIMIND_BUSY)
		cpu_relax();

	return 0;
}

/*
 * Read the bus for PHY at addr mii_id, register regnum, and
 * return the value.  Clears miimcom first.  All PHY operation
 * done on the bus attached to the local interface,
 * which may be different from the generic mdio bus
 * This is helpful in programming interfaces like
 * the TBI which, in turn, control interfaces like onchip SERDES
 * and are always tied to the local mdio pins, which may not be the
 * same as system mdio bus, used for controlling the external PHYs, for eg.
 */
int fsl_pq_local_mdio_read(struct fsl_pq_mdio __iomem *regs,
		int mii_id, int regnum)
{
	u16 value;

	/* Set the PHY address and the register address we want to read */
	out_be32(&regs->miimadd, (mii_id << 8) | regnum);

	/* Clear miimcom, and then initiate a read */
	out_be32(&regs->miimcom, 0);
	out_be32(&regs->miimcom, MII_READ_COMMAND);

	/* Wait for the transaction to finish */
	while (in_be32(&regs->miimind) & (MIIMIND_NOTVALID | MIIMIND_BUSY))
		cpu_relax();

	/* Grab the value of the register from miimstat */
	value = in_be32(&regs->miimstat);

	return value;
}

/*
 * Write value to the PHY at mii_id at register regnum,
 * on the bus, waiting until the write is done before returning.
 */
int fsl_pq_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value)
{
	struct fsl_pq_mdio __iomem *regs = (void __iomem *)bus->priv;

	/* Write to the local MII regs */
	return(fsl_pq_local_mdio_write(regs, mii_id, regnum, value));
}

/*
 * Read the bus for PHY at addr mii_id, register regnum, and
 * return the value.  Clears miimcom first.
 */
int fsl_pq_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fsl_pq_mdio __iomem *regs = (void __iomem *)bus->priv;

	/* Read the local MII regs */
	return(fsl_pq_local_mdio_read(regs, mii_id, regnum));
}

/* Reset the MIIM registers, and wait for the bus to free */
static int fsl_pq_mdio_reset(struct mii_bus *bus)
{
	struct fsl_pq_mdio __iomem *regs = (void __iomem *)bus->priv;
	int timeout = PHY_INIT_TIMEOUT;

	mutex_lock(&bus->mdio_lock);

	/* Reset the management interface */
	out_be32(&regs->miimcfg, MIIMCFG_RESET);

	/* Setup the MII Mgmt clock speed */
	out_be32(&regs->miimcfg, MIIMCFG_INIT_VALUE);

	/* Wait until the bus is free */
	while ((in_be32(&regs->miimind) & MIIMIND_BUSY) && timeout--)
		cpu_relax();

	mutex_unlock(&bus->mdio_lock);

	if (timeout < 0) {
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

void fsl_pq_mdio_bus_name(char *name, struct device_node *np)
{
	const u32 *addr;
	u64 taddr = OF_BAD_ADDR;

	addr = of_get_address(np, 0, NULL, NULL);
	if (addr)
		taddr = of_translate_address(np, addr);

	snprintf(name, MII_BUS_ID_SIZE, "%s@%llx", np->name,
		(unsigned long long)taddr);
}
EXPORT_SYMBOL_GPL(fsl_pq_mdio_bus_name);

/* Scan the bus in reverse, looking for an empty spot */
static int fsl_pq_mdio_find_free(struct mii_bus *new_bus)
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


#ifdef CONFIG_GIANFAR
static u32 __iomem *get_gfar_tbipa(struct fsl_pq_mdio __iomem *regs)
{
	struct gfar __iomem *enet_regs;

	/*
	 * This is mildly evil, but so is our hardware for doing this.
	 * Also, we have to cast back to struct gfar because of
	 * definition weirdness done in gianfar.h.
	 */
	enet_regs = (struct gfar __iomem *)
		((char __iomem *)regs - offsetof(struct gfar, gfar_mii_regs));

	return &enet_regs->tbipa;
}
#endif


#ifdef CONFIG_UCC_GETH
static int get_ucc_id_for_range(u64 start, u64 end, u32 *ucc_id)
{
	struct device_node *np = NULL;
	int err = 0;

	for_each_compatible_node(np, NULL, "ucc_geth") {
		struct resource tempres;

		err = of_address_to_resource(np, 0, &tempres);
		if (err)
			continue;

		/* if our mdio regs fall within this UCC regs range */
		if ((start >= tempres.start) && (end <= tempres.end)) {
			/* Find the id of the UCC */
			const u32 *id;

			id = of_get_property(np, "cell-index", NULL);
			if (!id) {
				id = of_get_property(np, "device-id", NULL);
				if (!id)
					continue;
			}

			*ucc_id = *id;

			return 0;
		}
	}

	if (err)
		return err;
	else
		return -EINVAL;
}
#endif


static int fsl_pq_mdio_probe(struct of_device *ofdev,
		const struct of_device_id *match)
{
	struct device_node *np = ofdev->node;
	struct device_node *tbi;
	struct fsl_pq_mdio __iomem *regs;
	u32 __iomem *tbipa;
	struct mii_bus *new_bus;
	int tbiaddr = -1;
	u64 addr, size;
	int err = 0;

	new_bus = mdiobus_alloc();
	if (NULL == new_bus)
		return -ENOMEM;

	new_bus->name = "Freescale PowerQUICC MII Bus",
	new_bus->read = &fsl_pq_mdio_read,
	new_bus->write = &fsl_pq_mdio_write,
	new_bus->reset = &fsl_pq_mdio_reset,
	fsl_pq_mdio_bus_name(new_bus->id, np);

	/* Set the PHY base address */
	addr = of_translate_address(np, of_get_address(np, 0, &size, NULL));
	regs = ioremap(addr, size);

	if (NULL == regs) {
		err = -ENOMEM;
		goto err_free_bus;
	}

	new_bus->priv = (void __force *)regs;

	new_bus->irq = create_irq_map(np);

	if (NULL == new_bus->irq) {
		err = -ENOMEM;
		goto err_unmap_regs;
	}

	new_bus->parent = &ofdev->dev;
	dev_set_drvdata(&ofdev->dev, new_bus);

	if (of_device_is_compatible(np, "fsl,gianfar-mdio") ||
			of_device_is_compatible(np, "fsl,gianfar-tbi") ||
			of_device_is_compatible(np, "gianfar")) {
#ifdef CONFIG_GIANFAR
		tbipa = get_gfar_tbipa(regs);
#else
		err = -ENODEV;
		goto err_free_irqs;
#endif
	} else if (of_device_is_compatible(np, "fsl,ucc-mdio") ||
			of_device_is_compatible(np, "ucc_geth_phy")) {
#ifdef CONFIG_UCC_GETH
		u32 id;

		tbipa = &regs->utbipar;

		if ((err = get_ucc_id_for_range(addr, addr + size, &id)))
			goto err_free_irqs;

		ucc_set_qe_mux_mii_mng(id - 1);
#else
		err = -ENODEV;
		goto err_free_irqs;
#endif
	} else {
		err = -ENODEV;
		goto err_free_irqs;
	}

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
		out_be32(tbipa, 0);

		tbiaddr = fsl_pq_mdio_find_free(new_bus);
	}

	/*
	 * We define TBIPA at 0 to be illegal, opting to fail for boards that
	 * have PHYs at 1-31, rather than change tbipa and rescan.
	 */
	if (tbiaddr == 0) {
		err = -EBUSY;

		goto err_free_irqs;
	}

	out_be32(tbipa, tbiaddr);

	/*
	 * The TBIPHY-only buses will find PHYs at every address,
	 * so we mask them all but the TBI
	 */
	if (of_device_is_compatible(np, "fsl,gianfar-tbi"))
		new_bus->phy_mask = ~(1 << tbiaddr);

	err = mdiobus_register(new_bus);

	if (err) {
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
	kfree(new_bus);

	return err;
}


static int fsl_pq_mdio_remove(struct of_device *ofdev)
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

static struct of_device_id fsl_pq_mdio_match[] = {
	{
		.type = "mdio",
		.compatible = "ucc_geth_phy",
	},
	{
		.type = "mdio",
		.compatible = "gianfar",
	},
	{
		.compatible = "fsl,ucc-mdio",
	},
	{
		.compatible = "fsl,gianfar-tbi",
	},
	{
		.compatible = "fsl,gianfar-mdio",
	},
	{},
};

static struct of_platform_driver fsl_pq_mdio_driver = {
	.name = "fsl-pq_mdio",
	.probe = fsl_pq_mdio_probe,
	.remove = fsl_pq_mdio_remove,
	.match_table = fsl_pq_mdio_match,
};

int __init fsl_pq_mdio_init(void)
{
	return of_register_platform_driver(&fsl_pq_mdio_driver);
}

void fsl_pq_mdio_exit(void)
{
	of_unregister_platform_driver(&fsl_pq_mdio_driver);
}
subsys_initcall_sync(fsl_pq_mdio_init);
module_exit(fsl_pq_mdio_exit);
