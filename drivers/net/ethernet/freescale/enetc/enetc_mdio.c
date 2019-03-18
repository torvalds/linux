// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include <linux/mdio.h>
#include <linux/of_mdio.h>
#include <linux/iopoll.h>
#include <linux/of.h>

#include "enetc_pf.h"

struct enetc_mdio_regs {
	u32	mdio_cfg;	/* MDIO configuration and status */
	u32	mdio_ctl;	/* MDIO control */
	u32	mdio_data;	/* MDIO data */
	u32	mdio_addr;	/* MDIO address */
};

#define bus_to_enetc_regs(bus)	(struct enetc_mdio_regs __iomem *)((bus)->priv)

#define ENETC_MDIO_REG_OFFSET	0x1c00
#define ENETC_MDC_DIV		258

#define MDIO_CFG_CLKDIV(x)	((((x) >> 1) & 0xff) << 8)
#define MDIO_CFG_BSY		BIT(0)
#define MDIO_CFG_RD_ER		BIT(1)
#define MDIO_CFG_ENC45		BIT(6)
 /* external MDIO only - driven on neg MDC edge */
#define MDIO_CFG_NEG		BIT(23)

#define MDIO_CTL_DEV_ADDR(x)	((x) & 0x1f)
#define MDIO_CTL_PORT_ADDR(x)	(((x) & 0x1f) << 5)
#define MDIO_CTL_READ		BIT(15)
#define MDIO_DATA(x)		((x) & 0xffff)

#define TIMEOUT	1000
static int enetc_mdio_wait_complete(struct enetc_mdio_regs __iomem *regs)
{
	u32 val;

	return readx_poll_timeout(enetc_rd_reg, &regs->mdio_cfg, val,
				  !(val & MDIO_CFG_BSY), 10, 10 * TIMEOUT);
}

static int enetc_mdio_write(struct mii_bus *bus, int phy_id, int regnum,
			    u16 value)
{
	struct enetc_mdio_regs __iomem *regs = bus_to_enetc_regs(bus);
	u32 mdio_ctl, mdio_cfg;
	u16 dev_addr;
	int ret;

	mdio_cfg = MDIO_CFG_CLKDIV(ENETC_MDC_DIV) | MDIO_CFG_NEG;
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_cfg |= MDIO_CFG_ENC45;
	} else {
		/* clause 22 (ie 1G) */
		dev_addr = regnum & 0x1f;
		mdio_cfg &= ~MDIO_CFG_ENC45;
	}

	enetc_wr_reg(&regs->mdio_cfg, mdio_cfg);

	ret = enetc_mdio_wait_complete(regs);
	if (ret)
		return ret;

	/* set port and dev addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	enetc_wr_reg(&regs->mdio_ctl, mdio_ctl);

	/* set the register address */
	if (regnum & MII_ADDR_C45) {
		enetc_wr_reg(&regs->mdio_addr, regnum & 0xffff);

		ret = enetc_mdio_wait_complete(regs);
		if (ret)
			return ret;
	}

	/* write the value */
	enetc_wr_reg(&regs->mdio_data, MDIO_DATA(value));

	ret = enetc_mdio_wait_complete(regs);
	if (ret)
		return ret;

	return 0;
}

static int enetc_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct enetc_mdio_regs __iomem *regs = bus_to_enetc_regs(bus);
	u32 mdio_ctl, mdio_cfg;
	u16 dev_addr, value;
	int ret;

	mdio_cfg = MDIO_CFG_CLKDIV(ENETC_MDC_DIV) | MDIO_CFG_NEG;
	if (regnum & MII_ADDR_C45) {
		dev_addr = (regnum >> 16) & 0x1f;
		mdio_cfg |= MDIO_CFG_ENC45;
	} else {
		dev_addr = regnum & 0x1f;
		mdio_cfg &= ~MDIO_CFG_ENC45;
	}

	enetc_wr_reg(&regs->mdio_cfg, mdio_cfg);

	ret = enetc_mdio_wait_complete(regs);
	if (ret)
		return ret;

	/* set port and device addr */
	mdio_ctl = MDIO_CTL_PORT_ADDR(phy_id) | MDIO_CTL_DEV_ADDR(dev_addr);
	enetc_wr_reg(&regs->mdio_ctl, mdio_ctl);

	/* set the register address */
	if (regnum & MII_ADDR_C45) {
		enetc_wr_reg(&regs->mdio_addr, regnum & 0xffff);

		ret = enetc_mdio_wait_complete(regs);
		if (ret)
			return ret;
	}

	/* initiate the read */
	enetc_wr_reg(&regs->mdio_ctl, mdio_ctl | MDIO_CTL_READ);

	ret = enetc_mdio_wait_complete(regs);
	if (ret)
		return ret;

	/* return all Fs if nothing was there */
	if (enetc_rd_reg(&regs->mdio_cfg) & MDIO_CFG_RD_ER) {
		dev_dbg(&bus->dev,
			"Error while reading PHY%d reg at %d.%hhu\n",
			phy_id, dev_addr, regnum);
		return 0xffff;
	}

	value = enetc_rd_reg(&regs->mdio_data) & 0xffff;

	return value;
}

int enetc_mdio_probe(struct enetc_pf *pf)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_mdio_regs __iomem *regs;
	struct device_node *np;
	struct mii_bus *bus;
	int ret;

	bus = mdiobus_alloc_size(sizeof(regs));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC MDIO Bus";
	bus->read = enetc_mdio_read;
	bus->write = enetc_mdio_write;
	bus->parent = dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	/* store the enetc mdio base address for this bus */
	regs = pf->si->hw.port + ENETC_MDIO_REG_OFFSET;
	bus->priv = regs;

	np = of_get_child_by_name(dev->of_node, "mdio");
	if (!np) {
		dev_err(dev, "MDIO node missing\n");
		ret = -EINVAL;
		goto err_registration;
	}

	ret = of_mdiobus_register(bus, np);
	if (ret) {
		of_node_put(np);
		dev_err(dev, "cannot register MDIO bus\n");
		goto err_registration;
	}

	of_node_put(np);
	pf->mdio = bus;

	return 0;

err_registration:
	mdiobus_free(bus);

	return ret;
}

void enetc_mdio_remove(struct enetc_pf *pf)
{
	if (pf->mdio) {
		mdiobus_unregister(pf->mdio);
		mdiobus_free(pf->mdio);
	}
}
