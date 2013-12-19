/*
 * Freescale PowerQUICC Ethernet Driver -- MIIM bus implementation
 * Provides Bus interface for MIIM regs
 *
 * Author: Andy Fleming <afleming@freescale.com>
 * Modifier: Sandeep Gopalpet <sandeep.kumar@freescale.com>
 *
 * Copyright 2002-2004, 2008-2009 Freescale Semiconductor, Inc.
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/of_device.h>

#include <asm/io.h>
#include <asm/ucc.h>	/* for ucc_set_qe_mux_mii_mng() */

#include "gianfar.h"

#define MIIMIND_BUSY		0x00000001
#define MIIMIND_NOTVALID	0x00000004
#define MIIMCFG_INIT_VALUE	0x00000007
#define MIIMCFG_RESET		0x80000000

#define MII_READ_COMMAND	0x00000001

struct fsl_pq_mii {
	u32 miimcfg;	/* MII management configuration reg */
	u32 miimcom;	/* MII management command reg */
	u32 miimadd;	/* MII management address reg */
	u32 miimcon;	/* MII management control reg */
	u32 miimstat;	/* MII management status reg */
	u32 miimind;	/* MII management indication reg */
};

struct fsl_pq_mdio {
	u8 res1[16];
	u32 ieventm;	/* MDIO Interrupt event register (for etsec2)*/
	u32 imaskm;	/* MDIO Interrupt mask register (for etsec2)*/
	u8 res2[4];
	u32 emapm;	/* MDIO Event mapping register (for etsec2)*/
	u8 res3[1280];
	struct fsl_pq_mii mii;
	u8 res4[28];
	u32 utbipar;	/* TBI phy address reg (only on UCC) */
	u8 res5[2728];
} __packed;

/* Number of microseconds to wait for an MII register to respond */
#define MII_TIMEOUT	1000

struct fsl_pq_mdio_priv {
	void __iomem *map;
	struct fsl_pq_mii __iomem *regs;
	int irqs[PHY_MAX_ADDR];
};

/*
 * Per-device-type data.  Each type of device tree node that we support gets
 * one of these.
 *
 * @mii_offset: the offset of the MII registers within the memory map of the
 * node.  Some nodes define only the MII registers, and some define the whole
 * MAC (which includes the MII registers).
 *
 * @get_tbipa: determines the address of the TBIPA register
 *
 * @ucc_configure: a special function for extra QE configuration
 */
struct fsl_pq_mdio_data {
	unsigned int mii_offset;	/* offset of the MII registers */
	uint32_t __iomem * (*get_tbipa)(void __iomem *p);
	void (*ucc_configure)(phys_addr_t start, phys_addr_t end);
};

/*
 * Write value to the PHY at mii_id at register regnum, on the bus attached
 * to the local interface, which may be different from the generic mdio bus
 * (tied to a single interface), waiting until the write is done before
 * returning. This is helpful in programming interfaces like the TBI which
 * control interfaces like onchip SERDES and are always tied to the local
 * mdio pins, which may not be the same as system mdio bus, used for
 * controlling the external PHYs, for example.
 */
static int fsl_pq_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
		u16 value)
{
	struct fsl_pq_mdio_priv *priv = bus->priv;
	struct fsl_pq_mii __iomem *regs = priv->regs;
	u32 status;

	/* Set the PHY address and the register address we want to write */
	out_be32(&regs->miimadd, (mii_id << 8) | regnum);

	/* Write out the value we want */
	out_be32(&regs->miimcon, value);

	/* Wait for the transaction to finish */
	status = spin_event_timeout(!(in_be32(&regs->miimind) &	MIIMIND_BUSY),
				    MII_TIMEOUT, 0);

	return status ? 0 : -ETIMEDOUT;
}

/*
 * Read the bus for PHY at addr mii_id, register regnum, and return the value.
 * Clears miimcom first.
 *
 * All PHY operation done on the bus attached to the local interface, which
 * may be different from the generic mdio bus.  This is helpful in programming
 * interfaces like the TBI which, in turn, control interfaces like on-chip
 * SERDES and are always tied to the local mdio pins, which may not be the
 * same as system mdio bus, used for controlling the external PHYs, for eg.
 */
static int fsl_pq_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fsl_pq_mdio_priv *priv = bus->priv;
	struct fsl_pq_mii __iomem *regs = priv->regs;
	u32 status;
	u16 value;

	/* Set the PHY address and the register address we want to read */
	out_be32(&regs->miimadd, (mii_id << 8) | regnum);

	/* Clear miimcom, and then initiate a read */
	out_be32(&regs->miimcom, 0);
	out_be32(&regs->miimcom, MII_READ_COMMAND);

	/* Wait for the transaction to finish, normally less than 100us */
	status = spin_event_timeout(!(in_be32(&regs->miimind) &
				    (MIIMIND_NOTVALID | MIIMIND_BUSY)),
				    MII_TIMEOUT, 0);
	if (!status)
		return -ETIMEDOUT;

	/* Grab the value of the register from miimstat */
	value = in_be32(&regs->miimstat);

	dev_dbg(&bus->dev, "read %04x from address %x/%x\n", value, mii_id, regnum);
	return value;
}

/* Reset the MIIM registers, and wait for the bus to free */
static int fsl_pq_mdio_reset(struct mii_bus *bus)
{
	struct fsl_pq_mdio_priv *priv = bus->priv;
	struct fsl_pq_mii __iomem *regs = priv->regs;
	u32 status;

	mutex_lock(&bus->mdio_lock);

	/* Reset the management interface */
	out_be32(&regs->miimcfg, MIIMCFG_RESET);

	/* Setup the MII Mgmt clock speed */
	out_be32(&regs->miimcfg, MIIMCFG_INIT_VALUE);

	/* Wait until the bus is free */
	status = spin_event_timeout(!(in_be32(&regs->miimind) &	MIIMIND_BUSY),
				    MII_TIMEOUT, 0);

	mutex_unlock(&bus->mdio_lock);

	if (!status) {
		dev_err(&bus->dev, "timeout waiting for MII bus\n");
		return -EBUSY;
	}

	return 0;
}

#if defined(CONFIG_GIANFAR) || defined(CONFIG_GIANFAR_MODULE)
/*
 * This is mildly evil, but so is our hardware for doing this.
 * Also, we have to cast back to struct gfar because of
 * definition weirdness done in gianfar.h.
 */
static uint32_t __iomem *get_gfar_tbipa(void __iomem *p)
{
	struct gfar __iomem *enet_regs = p;

	return &enet_regs->tbipa;
}

/*
 * Return the TBIPAR address for an eTSEC2 node
 */
static uint32_t __iomem *get_etsec_tbipa(void __iomem *p)
{
	return p;
}
#endif

#if defined(CONFIG_UCC_GETH) || defined(CONFIG_UCC_GETH_MODULE)
/*
 * Return the TBIPAR address for a QE MDIO node
 */
static uint32_t __iomem *get_ucc_tbipa(void __iomem *p)
{
	struct fsl_pq_mdio __iomem *mdio = p;

	return &mdio->utbipar;
}

/*
 * Find the UCC node that controls the given MDIO node
 *
 * For some reason, the QE MDIO nodes are not children of the UCC devices
 * that control them.  Therefore, we need to scan all UCC nodes looking for
 * the one that encompases the given MDIO node.  We do this by comparing
 * physical addresses.  The 'start' and 'end' addresses of the MDIO node are
 * passed, and the correct UCC node will cover the entire address range.
 *
 * This assumes that there is only one QE MDIO node in the entire device tree.
 */
static void ucc_configure(phys_addr_t start, phys_addr_t end)
{
	static bool found_mii_master;
	struct device_node *np = NULL;

	if (found_mii_master)
		return;

	for_each_compatible_node(np, NULL, "ucc_geth") {
		struct resource res;
		const uint32_t *iprop;
		uint32_t id;
		int ret;

		ret = of_address_to_resource(np, 0, &res);
		if (ret < 0) {
			pr_debug("fsl-pq-mdio: no address range in node %s\n",
				 np->full_name);
			continue;
		}

		/* if our mdio regs fall within this UCC regs range */
		if ((start < res.start) || (end > res.end))
			continue;

		iprop = of_get_property(np, "cell-index", NULL);
		if (!iprop) {
			iprop = of_get_property(np, "device-id", NULL);
			if (!iprop) {
				pr_debug("fsl-pq-mdio: no UCC ID in node %s\n",
					 np->full_name);
				continue;
			}
		}

		id = be32_to_cpup(iprop);

		/*
		 * cell-index and device-id for QE nodes are
		 * numbered from 1, not 0.
		 */
		if (ucc_set_qe_mux_mii_mng(id - 1) < 0) {
			pr_debug("fsl-pq-mdio: invalid UCC ID in node %s\n",
				 np->full_name);
			continue;
		}

		pr_debug("fsl-pq-mdio: setting node UCC%u to MII master\n", id);
		found_mii_master = true;
	}
}

#endif

static struct of_device_id fsl_pq_mdio_match[] = {
#if defined(CONFIG_GIANFAR) || defined(CONFIG_GIANFAR_MODULE)
	{
		.compatible = "fsl,gianfar-tbi",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = 0,
			.get_tbipa = get_gfar_tbipa,
		},
	},
	{
		.compatible = "fsl,gianfar-mdio",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = 0,
			.get_tbipa = get_gfar_tbipa,
		},
	},
	{
		.type = "mdio",
		.compatible = "gianfar",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = offsetof(struct fsl_pq_mdio, mii),
			.get_tbipa = get_gfar_tbipa,
		},
	},
	{
		.compatible = "fsl,etsec2-tbi",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = offsetof(struct fsl_pq_mdio, mii),
			.get_tbipa = get_etsec_tbipa,
		},
	},
	{
		.compatible = "fsl,etsec2-mdio",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = offsetof(struct fsl_pq_mdio, mii),
			.get_tbipa = get_etsec_tbipa,
		},
	},
#endif
#if defined(CONFIG_UCC_GETH) || defined(CONFIG_UCC_GETH_MODULE)
	{
		.compatible = "fsl,ucc-mdio",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = 0,
			.get_tbipa = get_ucc_tbipa,
			.ucc_configure = ucc_configure,
		},
	},
	{
		/* Legacy UCC MDIO node */
		.type = "mdio",
		.compatible = "ucc_geth_phy",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = 0,
			.get_tbipa = get_ucc_tbipa,
			.ucc_configure = ucc_configure,
		},
	},
#endif
	/* No Kconfig option for Fman support yet */
	{
		.compatible = "fsl,fman-mdio",
		.data = &(struct fsl_pq_mdio_data) {
			.mii_offset = 0,
			/* Fman TBI operations are handled elsewhere */
		},
	},

	{},
};
MODULE_DEVICE_TABLE(of, fsl_pq_mdio_match);

static int fsl_pq_mdio_probe(struct platform_device *pdev)
{
	const struct of_device_id *id =
		of_match_device(fsl_pq_mdio_match, &pdev->dev);
	const struct fsl_pq_mdio_data *data = id->data;
	struct device_node *np = pdev->dev.of_node;
	struct resource res;
	struct device_node *tbi;
	struct fsl_pq_mdio_priv *priv;
	struct mii_bus *new_bus;
	int err;

	dev_dbg(&pdev->dev, "found %s compatible node\n", id->compatible);

	new_bus = mdiobus_alloc_size(sizeof(*priv));
	if (!new_bus)
		return -ENOMEM;

	priv = new_bus->priv;
	new_bus->name = "Freescale PowerQUICC MII Bus",
	new_bus->read = &fsl_pq_mdio_read;
	new_bus->write = &fsl_pq_mdio_write;
	new_bus->reset = &fsl_pq_mdio_reset;
	new_bus->irq = priv->irqs;

	err = of_address_to_resource(np, 0, &res);
	if (err < 0) {
		dev_err(&pdev->dev, "could not obtain address information\n");
		goto error;
	}

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s@%llx", np->name,
		(unsigned long long)res.start);

	priv->map = of_iomap(np, 0);
	if (!priv->map) {
		err = -ENOMEM;
		goto error;
	}

	/*
	 * Some device tree nodes represent only the MII registers, and
	 * others represent the MAC and MII registers.  The 'mii_offset' field
	 * contains the offset of the MII registers inside the mapped register
	 * space.
	 */
	if (data->mii_offset > resource_size(&res)) {
		dev_err(&pdev->dev, "invalid register map\n");
		err = -EINVAL;
		goto error;
	}
	priv->regs = priv->map + data->mii_offset;

	new_bus->parent = &pdev->dev;
	platform_set_drvdata(pdev, new_bus);

	if (data->get_tbipa) {
		for_each_child_of_node(np, tbi) {
			if (strcmp(tbi->type, "tbi-phy") == 0) {
				dev_dbg(&pdev->dev, "found TBI PHY node %s\n",
					strrchr(tbi->full_name, '/') + 1);
				break;
			}
		}

		if (tbi) {
			const u32 *prop = of_get_property(tbi, "reg", NULL);
			uint32_t __iomem *tbipa;

			if (!prop) {
				dev_err(&pdev->dev,
					"missing 'reg' property in node %s\n",
					tbi->full_name);
				err = -EBUSY;
				goto error;
			}

			tbipa = data->get_tbipa(priv->map);

			out_be32(tbipa, be32_to_cpup(prop));
		}
	}

	if (data->ucc_configure)
		data->ucc_configure(res.start, res.end);

	err = of_mdiobus_register(new_bus, np);
	if (err) {
		dev_err(&pdev->dev, "cannot register %s as MDIO bus\n",
			new_bus->name);
		goto error;
	}

	return 0;

error:
	if (priv->map)
		iounmap(priv->map);

	kfree(new_bus);

	return err;
}


static int fsl_pq_mdio_remove(struct platform_device *pdev)
{
	struct device *device = &pdev->dev;
	struct mii_bus *bus = dev_get_drvdata(device);
	struct fsl_pq_mdio_priv *priv = bus->priv;

	mdiobus_unregister(bus);

	iounmap(priv->map);
	mdiobus_free(bus);

	return 0;
}

static struct platform_driver fsl_pq_mdio_driver = {
	.driver = {
		.name = "fsl-pq_mdio",
		.owner = THIS_MODULE,
		.of_match_table = fsl_pq_mdio_match,
	},
	.probe = fsl_pq_mdio_probe,
	.remove = fsl_pq_mdio_remove,
};

module_platform_driver(fsl_pq_mdio_driver);

MODULE_LICENSE("GPL");
