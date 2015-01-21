/*
 * QorIQ 10G MDIO Controller
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * Authors: Andy Fleming <afleming@freescale.com>
 *          Timur Tabi <timur@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/mdio.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>

/* Number of microseconds to wait for a register to respond */
#define TIMEOUT	1000

struct tgec_mdio_controller {
	__be32	reserved[12];
	__be32	mdio_stat;	/* MDIO configuration and status */
	__be32	mdio_ctl;	/* MDIO control */
	__be32	mdio_data;	/* MDIO data */
	__be32	mdio_addr;	/* MDIO address */
} __packed;

#define MDIO_STAT_ENC		BIT(6)
#define MDIO_STAT_CLKDIV(x)	(((x>>1) & 0xff) << 8)
#define MDIO_STAT_BSY		BIT(0)
#define MDIO_STAT_RD_ER		BIT(1)
#define MDIO_CTL_DEV_ADDR(x) 	(x & 0x1f)
#define MDIO_CTL_PORT_ADDR(x)	((x & 0x1f) << 5)
#define MDIO_CTL_PRE_DIS	BIT(10)
#define MDIO_CTL_SCAN_EN	BIT(11)
#define MDIO_CTL_POST_INC	BIT(14)
#define MDIO_CTL_READ		BIT(15)

#define MDIO_DATA(x)		(x & 0xffff)
#define MDIO_DATA_BSY		BIT(31)

/*
 * Wait until the MDIO bus is free
 */
static int xgmac_wait_until_free(struct device *dev,
				 struct tgec_mdio_controller __iomem *regs)
{
	unsigned int timeout;

	/* Wait till the bus is free */
	timeout = TIMEOUT;
	while ((ioread32be(&regs->mdio_stat) & MDIO_STAT_BSY) && timeout) {
		cpu_relax();
		timeout--;
	}

	if (!timeout) {
		dev_err(dev, "timeout waiting for bus to be free\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Wait till the MDIO read or write operation is complete
 */
static int xgmac_wait_until_done(struct device *dev,
				 struct tgec_mdio_controller __iomem *regs)
{
	unsigned int timeout;

	/* Wait till the MDIO write is complete */
	timeout = TIMEOUT;
	while ((ioread32be(&regs->mdio_data) & MDIO_DATA_BSY) && timeout) {
		cpu_relax();
		timeout--;
	}

	if (!timeout) {
		dev_err(dev, "timeout waiting for operation to complete\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Write value to the PHY for this device to the register at regnum,waiting
 * until the write is done before it returns.  All PHY configuration has to be
 * done through the TSEC1 MIIM regs.
 */
static int xgmac_mdio_write(struct mii_bus *bus, int phy_id, int regnum, u16 value)
{
	struct tgec_mdio_controller __iomem *regs = bus->priv;
	uint16_t dev_addr;
	u32 mdio_ctl, mdio_stat;
	int ret;

	mdio_stat = ioread32be(&regs->mdio_stat);
	if (regnum & MII_ADDR_C45) {
		/* Clause 45 (ie 10G) */
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_stat |= MDIO_STAT_ENC;
	} else {
		/* Clause 22 (ie 1G) */
		dev_addr = regnum & 0x1f;
		mdio_stat &= ~MDIO_STAT_ENC;
	}

	iowrite32be(mdio_stat, &regs->mdio_stat);

	ret = xgmac_wait_until_free(&bus->dev, regs);
	if (ret)
		return ret;

	/* Set the port and dev addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	iowrite32be(mdio_ctl, &regs->mdio_ctl);

	/* Set the register address */
	if (regnum & MII_ADDR_C45) {
		iowrite32be(regnum & 0xffff, &regs->mdio_addr);

		ret = xgmac_wait_until_free(&bus->dev, regs);
		if (ret)
			return ret;
	}

	/* Write the value to the register */
	iowrite32be(MDIO_DATA(value), &regs->mdio_data);

	ret = xgmac_wait_until_done(&bus->dev, regs);
	if (ret)
		return ret;

	return 0;
}

/*
 * Reads from register regnum in the PHY for device dev, returning the value.
 * Clears miimcom first.  All PHY configuration has to be done through the
 * TSEC1 MIIM regs.
 */
static int xgmac_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct tgec_mdio_controller __iomem *regs = bus->priv;
	uint16_t dev_addr;
	uint32_t mdio_stat;
	uint32_t mdio_ctl;
	uint16_t value;
	int ret;

	mdio_stat = ioread32be(&regs->mdio_stat);
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_stat |= MDIO_STAT_ENC;
	} else {
		dev_addr = regnum & 0x1f;
		mdio_stat &= ~MDIO_STAT_ENC;
	}

	iowrite32be(mdio_stat, &regs->mdio_stat);

	ret = xgmac_wait_until_free(&bus->dev, regs);
	if (ret)
		return ret;

	/* Set the Port and Device Addrs */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	iowrite32be(mdio_ctl, &regs->mdio_ctl);

	/* Set the register address */
	if (regnum & MII_ADDR_C45) {
		iowrite32be(regnum & 0xffff, &regs->mdio_addr);

		ret = xgmac_wait_until_free(&bus->dev, regs);
		if (ret)
			return ret;
	}

	/* Initiate the read */
	iowrite32be(mdio_ctl | MDIO_CTL_READ, &regs->mdio_ctl);

	ret = xgmac_wait_until_done(&bus->dev, regs);
	if (ret)
		return ret;

	/* Return all Fs if nothing was there */
	if (ioread32be(&regs->mdio_stat) & MDIO_STAT_RD_ER) {
		dev_err(&bus->dev,
			"Error while reading PHY%d reg at %d.%hhu\n",
			phy_id, dev_addr, regnum);
		return 0xffff;
	}

	value = ioread32be(&regs->mdio_data) & 0xffff;
	dev_dbg(&bus->dev, "read %04x\n", value);

	return value;
}

static int xgmac_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus;
	struct resource res;
	int ret;

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "could not obtain address\n");
		return ret;
	}

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale XGMAC MDIO Bus";
	bus->read = xgmac_mdio_read;
	bus->write = xgmac_mdio_write;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%llx", (unsigned long long)res.start);

	/* Set the PHY base address */
	bus->priv = of_iomap(np, 0);
	if (!bus->priv) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	ret = of_mdiobus_register(bus, np);
	if (ret) {
		dev_err(&pdev->dev, "cannot register MDIO bus\n");
		goto err_registration;
	}

	platform_set_drvdata(pdev, bus);

	return 0;

err_registration:
	iounmap(bus->priv);

err_ioremap:
	mdiobus_free(bus);

	return ret;
}

static int xgmac_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	iounmap(bus->priv);
	mdiobus_free(bus);

	return 0;
}

static struct of_device_id xgmac_mdio_match[] = {
	{
		.compatible = "fsl,fman-xmdio",
	},
	{
		.compatible = "fsl,fman-memac-mdio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, xgmac_mdio_match);

static struct platform_driver xgmac_mdio_driver = {
	.driver = {
		.name = "fsl-fman_xmdio",
		.of_match_table = xgmac_mdio_match,
	},
	.probe = xgmac_mdio_probe,
	.remove = xgmac_mdio_remove,
};

module_platform_driver(xgmac_mdio_driver);

MODULE_DESCRIPTION("Freescale QorIQ 10G MDIO Controller");
MODULE_LICENSE("GPL v2");
