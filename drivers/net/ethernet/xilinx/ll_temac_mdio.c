// SPDX-License-Identifier: GPL-2.0
/*
 * MDIO bus driver for the Xilinx TEMAC device
 *
 * Copyright (c) 2009 Secret Lab Technologies, Ltd.
 */

#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_mdio.h>
#include <linux/platform_data/xilinx-ll-temac.h>

#include "ll_temac.h"

/* ---------------------------------------------------------------------
 * MDIO Bus functions
 */
static int temac_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct temac_local *lp = bus->priv;
	u32 rc;
	unsigned long flags;

	/* Write the PHY address to the MIIM Access Initiator register.
	 * When the transfer completes, the PHY register value will appear
	 * in the LSW0 register
	 */
	spin_lock_irqsave(lp->indirect_lock, flags);
	temac_iow(lp, XTE_LSW0_OFFSET, (phy_id << 5) | reg);
	rc = temac_indirect_in32_locked(lp, XTE_MIIMAI_OFFSET);
	spin_unlock_irqrestore(lp->indirect_lock, flags);

	dev_dbg(lp->dev, "temac_mdio_read(phy_id=%i, reg=%x) == %x\n",
		phy_id, reg, rc);

	return rc;
}

static int temac_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 val)
{
	struct temac_local *lp = bus->priv;
	unsigned long flags;

	dev_dbg(lp->dev, "temac_mdio_write(phy_id=%i, reg=%x, val=%x)\n",
		phy_id, reg, val);

	/* First write the desired value into the write data register
	 * and then write the address into the access initiator register
	 */
	spin_lock_irqsave(lp->indirect_lock, flags);
	temac_indirect_out32_locked(lp, XTE_MGTDR_OFFSET, val);
	temac_indirect_out32_locked(lp, XTE_MIIMAI_OFFSET, (phy_id << 5) | reg);
	spin_unlock_irqrestore(lp->indirect_lock, flags);

	return 0;
}

int temac_mdio_setup(struct temac_local *lp, struct platform_device *pdev)
{
	struct ll_temac_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *np = dev_of_node(&pdev->dev);
	struct mii_bus *bus;
	u32 bus_hz;
	int clk_div;
	int rc;
	struct resource res;

	/* Get MDIO bus frequency (if specified) */
	bus_hz = 0;
	if (np)
		of_property_read_u32(np, "clock-frequency", &bus_hz);
	else if (pdata)
		bus_hz = pdata->mdio_clk_freq;

	/* Calculate a reasonable divisor for the clock rate */
	clk_div = 0x3f; /* worst-case default setting */
	if (bus_hz != 0) {
		clk_div = bus_hz / (2500 * 1000 * 2) - 1;
		if (clk_div < 1)
			clk_div = 1;
		if (clk_div > 0x3f)
			clk_div = 0x3f;
	}

	/* Enable the MDIO bus by asserting the enable bit and writing
	 * in the clock config
	 */
	temac_indirect_out32(lp, XTE_MC_OFFSET, 1 << 6 | clk_div);

	bus = devm_mdiobus_alloc(&pdev->dev);
	if (!bus)
		return -ENOMEM;

	if (np) {
		of_address_to_resource(np, 0, &res);
		snprintf(bus->id, MII_BUS_ID_SIZE, "%.8llx",
			 (unsigned long long)res.start);
	} else if (pdata) {
		snprintf(bus->id, MII_BUS_ID_SIZE, "%.8llx",
			 pdata->mdio_bus_id);
	}

	bus->priv = lp;
	bus->name = "Xilinx TEMAC MDIO";
	bus->read = temac_mdio_read;
	bus->write = temac_mdio_write;
	bus->parent = lp->dev;

	lp->mii_bus = bus;

	rc = of_mdiobus_register(bus, np);
	if (rc)
		return rc;

	dev_dbg(lp->dev, "MDIO bus registered;  MC:%x\n",
		temac_indirect_in32(lp, XTE_MC_OFFSET));
	return 0;
}

void temac_mdio_teardown(struct temac_local *lp)
{
	mdiobus_unregister(lp->mii_bus);
}
