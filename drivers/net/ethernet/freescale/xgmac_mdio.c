/*
 * QorIQ 10G MDIO Controller
 *
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2021 NXP
 *
 * Authors: Andy Fleming <afleming@freescale.com>
 *          Timur Tabi <timur@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/acpi_mdio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/slab.h>

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

struct mdio_fsl_priv {
	struct	tgec_mdio_controller __iomem *mdio_base;
	bool	is_little_endian;
	bool	has_a009885;
	bool	has_a011043;
};

static u32 xgmac_read32(void __iomem *regs,
			bool is_little_endian)
{
	if (is_little_endian)
		return ioread32(regs);
	else
		return ioread32be(regs);
}

static void xgmac_write32(u32 value,
			  void __iomem *regs,
			  bool is_little_endian)
{
	if (is_little_endian)
		iowrite32(value, regs);
	else
		iowrite32be(value, regs);
}

/*
 * Wait until the MDIO bus is free
 */
static int xgmac_wait_until_free(struct device *dev,
				 struct tgec_mdio_controller __iomem *regs,
				 bool is_little_endian)
{
	unsigned int timeout;

	/* Wait till the bus is free */
	timeout = TIMEOUT;
	while ((xgmac_read32(&regs->mdio_stat, is_little_endian) &
		MDIO_STAT_BSY) && timeout) {
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
				 struct tgec_mdio_controller __iomem *regs,
				 bool is_little_endian)
{
	unsigned int timeout;

	/* Wait till the MDIO write is complete */
	timeout = TIMEOUT;
	while ((xgmac_read32(&regs->mdio_stat, is_little_endian) &
		MDIO_STAT_BSY) && timeout) {
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
	struct mdio_fsl_priv *priv = (struct mdio_fsl_priv *)bus->priv;
	struct tgec_mdio_controller __iomem *regs = priv->mdio_base;
	uint16_t dev_addr;
	u32 mdio_ctl, mdio_stat;
	int ret;
	bool endian = priv->is_little_endian;

	mdio_stat = xgmac_read32(&regs->mdio_stat, endian);
	if (regnum & MII_ADDR_C45) {
		/* Clause 45 (ie 10G) */
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_stat |= MDIO_STAT_ENC;
	} else {
		/* Clause 22 (ie 1G) */
		dev_addr = regnum & 0x1f;
		mdio_stat &= ~MDIO_STAT_ENC;
	}

	xgmac_write32(mdio_stat, &regs->mdio_stat, endian);

	ret = xgmac_wait_until_free(&bus->dev, regs, endian);
	if (ret)
		return ret;

	/* Set the port and dev addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	xgmac_write32(mdio_ctl, &regs->mdio_ctl, endian);

	/* Set the register address */
	if (regnum & MII_ADDR_C45) {
		xgmac_write32(regnum & 0xffff, &regs->mdio_addr, endian);

		ret = xgmac_wait_until_free(&bus->dev, regs, endian);
		if (ret)
			return ret;
	}

	/* Write the value to the register */
	xgmac_write32(MDIO_DATA(value), &regs->mdio_data, endian);

	ret = xgmac_wait_until_done(&bus->dev, regs, endian);
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
	struct mdio_fsl_priv *priv = (struct mdio_fsl_priv *)bus->priv;
	struct tgec_mdio_controller __iomem *regs = priv->mdio_base;
	unsigned long flags;
	uint16_t dev_addr;
	uint32_t mdio_stat;
	uint32_t mdio_ctl;
	int ret;
	bool endian = priv->is_little_endian;

	mdio_stat = xgmac_read32(&regs->mdio_stat, endian);
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_stat |= MDIO_STAT_ENC;
	} else {
		dev_addr = regnum & 0x1f;
		mdio_stat &= ~MDIO_STAT_ENC;
	}

	xgmac_write32(mdio_stat, &regs->mdio_stat, endian);

	ret = xgmac_wait_until_free(&bus->dev, regs, endian);
	if (ret)
		return ret;

	/* Set the Port and Device Addrs */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	xgmac_write32(mdio_ctl, &regs->mdio_ctl, endian);

	/* Set the register address */
	if (regnum & MII_ADDR_C45) {
		xgmac_write32(regnum & 0xffff, &regs->mdio_addr, endian);

		ret = xgmac_wait_until_free(&bus->dev, regs, endian);
		if (ret)
			return ret;
	}

	if (priv->has_a009885)
		/* Once the operation completes, i.e. MDIO_STAT_BSY clears, we
		 * must read back the data register within 16 MDC cycles.
		 */
		local_irq_save(flags);

	/* Initiate the read */
	xgmac_write32(mdio_ctl | MDIO_CTL_READ, &regs->mdio_ctl, endian);

	ret = xgmac_wait_until_done(&bus->dev, regs, endian);
	if (ret)
		goto irq_restore;

	/* Return all Fs if nothing was there */
	if ((xgmac_read32(&regs->mdio_stat, endian) & MDIO_STAT_RD_ER) &&
	    !priv->has_a011043) {
		dev_dbg(&bus->dev,
			"Error while reading PHY%d reg at %d.%hhu\n",
			phy_id, dev_addr, regnum);
		ret = 0xffff;
	} else {
		ret = xgmac_read32(&regs->mdio_data, endian) & 0xffff;
		dev_dbg(&bus->dev, "read %04x\n", ret);
	}

irq_restore:
	if (priv->has_a009885)
		local_irq_restore(flags);

	return ret;
}

static int xgmac_mdio_probe(struct platform_device *pdev)
{
	struct fwnode_handle *fwnode;
	struct mdio_fsl_priv *priv;
	struct resource *res;
	struct mii_bus *bus;
	int ret;

	/* In DPAA-1, MDIO is one of the many FMan sub-devices. The FMan
	 * defines a register space that spans a large area, covering all the
	 * subdevice areas. Therefore, MDIO cannot claim exclusive access to
	 * this register area.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "could not obtain address\n");
		return -EINVAL;
	}

	bus = mdiobus_alloc_size(sizeof(struct mdio_fsl_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale XGMAC MDIO Bus";
	bus->read = xgmac_mdio_read;
	bus->write = xgmac_mdio_write;
	bus->parent = &pdev->dev;
	bus->probe_capabilities = MDIOBUS_C22_C45;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%pa", &res->start);

	/* Set the PHY base address */
	priv = bus->priv;
	priv->mdio_base = ioremap(res->start, resource_size(res));
	if (!priv->mdio_base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	/* For both ACPI and DT cases, endianness of MDIO controller
	 * needs to be specified using "little-endian" property.
	 */
	priv->is_little_endian = device_property_read_bool(&pdev->dev,
							   "little-endian");

	priv->has_a009885 = device_property_read_bool(&pdev->dev,
						      "fsl,erratum-a009885");
	priv->has_a011043 = device_property_read_bool(&pdev->dev,
						      "fsl,erratum-a011043");

	fwnode = pdev->dev.fwnode;
	if (is_of_node(fwnode))
		ret = of_mdiobus_register(bus, to_of_node(fwnode));
	else if (is_acpi_node(fwnode))
		ret = acpi_mdiobus_register(bus, fwnode);
	else
		ret = -EINVAL;
	if (ret) {
		dev_err(&pdev->dev, "cannot register MDIO bus\n");
		goto err_registration;
	}

	platform_set_drvdata(pdev, bus);

	return 0;

err_registration:
	iounmap(priv->mdio_base);

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

static const struct of_device_id xgmac_mdio_match[] = {
	{
		.compatible = "fsl,fman-xmdio",
	},
	{
		.compatible = "fsl,fman-memac-mdio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, xgmac_mdio_match);

static const struct acpi_device_id xgmac_acpi_match[] = {
	{ "NXP0006" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, xgmac_acpi_match);

static struct platform_driver xgmac_mdio_driver = {
	.driver = {
		.name = "fsl-fman_xmdio",
		.of_match_table = xgmac_mdio_match,
		.acpi_match_table = xgmac_acpi_match,
	},
	.probe = xgmac_mdio_probe,
	.remove = xgmac_mdio_remove,
};

module_platform_driver(xgmac_mdio_driver);

MODULE_DESCRIPTION("Freescale QorIQ 10G MDIO Controller");
MODULE_LICENSE("GPL v2");
