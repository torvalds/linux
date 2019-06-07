// SPDX-License-Identifier: GPL-2.0
/*
 * MDIO bus driver for the Xilinx Axi Ethernet device
 *
 * Copyright (c) 2009 Secret Lab Technologies, Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2010 - 2012 Xilinx, Inc. All rights reserved.
 */

#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/jiffies.h>
#include <linux/iopoll.h>

#include "xilinx_axienet.h"

#define MAX_MDIO_FREQ		2500000 /* 2.5 MHz */
#define DEFAULT_CLOCK_DIVISOR	XAE_MDIO_DIV_DFT

/* Wait till MDIO interface is ready to accept a new transaction.*/
int axienet_mdio_wait_until_ready(struct axienet_local *lp)
{
	u32 val;

	return readx_poll_timeout(axinet_ior_read_mcr, lp,
				  val, val & XAE_MDIO_MCR_READY_MASK,
				  1, 20000);
}

/**
 * axienet_mdio_read - MDIO interface read function
 * @bus:	Pointer to mii bus structure
 * @phy_id:	Address of the PHY device
 * @reg:	PHY register to read
 *
 * Return:	The register contents on success, -ETIMEDOUT on a timeout
 *
 * Reads the contents of the requested register from the requested PHY
 * address by first writing the details into MCR register. After a while
 * the register MRD is read to obtain the PHY register content.
 */
static int axienet_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	u32 rc;
	int ret;
	struct axienet_local *lp = bus->priv;

	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	axienet_iow(lp, XAE_MDIO_MCR_OFFSET,
		    (((phy_id << XAE_MDIO_MCR_PHYAD_SHIFT) &
		      XAE_MDIO_MCR_PHYAD_MASK) |
		     ((reg << XAE_MDIO_MCR_REGAD_SHIFT) &
		      XAE_MDIO_MCR_REGAD_MASK) |
		     XAE_MDIO_MCR_INITIATE_MASK |
		     XAE_MDIO_MCR_OP_READ_MASK));

	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	rc = axienet_ior(lp, XAE_MDIO_MRD_OFFSET) & 0x0000FFFF;

	dev_dbg(lp->dev, "axienet_mdio_read(phy_id=%i, reg=%x) == %x\n",
		phy_id, reg, rc);

	return rc;
}

/**
 * axienet_mdio_write - MDIO interface write function
 * @bus:	Pointer to mii bus structure
 * @phy_id:	Address of the PHY device
 * @reg:	PHY register to write to
 * @val:	Value to be written into the register
 *
 * Return:	0 on success, -ETIMEDOUT on a timeout
 *
 * Writes the value to the requested register by first writing the value
 * into MWD register. The the MCR register is then appropriately setup
 * to finish the write operation.
 */
static int axienet_mdio_write(struct mii_bus *bus, int phy_id, int reg,
			      u16 val)
{
	int ret;
	struct axienet_local *lp = bus->priv;

	dev_dbg(lp->dev, "axienet_mdio_write(phy_id=%i, reg=%x, val=%x)\n",
		phy_id, reg, val);

	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	axienet_iow(lp, XAE_MDIO_MWD_OFFSET, (u32) val);
	axienet_iow(lp, XAE_MDIO_MCR_OFFSET,
		    (((phy_id << XAE_MDIO_MCR_PHYAD_SHIFT) &
		      XAE_MDIO_MCR_PHYAD_MASK) |
		     ((reg << XAE_MDIO_MCR_REGAD_SHIFT) &
		      XAE_MDIO_MCR_REGAD_MASK) |
		     XAE_MDIO_MCR_INITIATE_MASK |
		     XAE_MDIO_MCR_OP_WRITE_MASK));

	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * axienet_mdio_setup - MDIO setup function
 * @lp:		Pointer to axienet local data structure.
 * @np:		Pointer to device node
 *
 * Return:	0 on success, -ETIMEDOUT on a timeout, -ENOMEM when
 *		mdiobus_alloc (to allocate memory for mii bus structure) fails.
 *
 * Sets up the MDIO interface by initializing the MDIO clock and enabling the
 * MDIO interface in hardware. Register the MDIO interface.
 **/
int axienet_mdio_setup(struct axienet_local *lp, struct device_node *np)
{
	int ret;
	u32 clk_div, host_clock;
	struct mii_bus *bus;
	struct resource res;
	struct device_node *np1;

	/* clk_div can be calculated by deriving it from the equation:
	 * fMDIO = fHOST / ((1 + clk_div) * 2)
	 *
	 * Where fMDIO <= 2500000, so we get:
	 * fHOST / ((1 + clk_div) * 2) <= 2500000
	 *
	 * Then we get:
	 * 1 / ((1 + clk_div) * 2) <= (2500000 / fHOST)
	 *
	 * Then we get:
	 * 1 / (1 + clk_div) <= ((2500000 * 2) / fHOST)
	 *
	 * Then we get:
	 * 1 / (1 + clk_div) <= (5000000 / fHOST)
	 *
	 * So:
	 * (1 + clk_div) >= (fHOST / 5000000)
	 *
	 * And finally:
	 * clk_div >= (fHOST / 5000000) - 1
	 *
	 * fHOST can be read from the flattened device tree as property
	 * "clock-frequency" from the CPU
	 */

	np1 = of_find_node_by_name(NULL, "cpu");
	if (!np1) {
		netdev_warn(lp->ndev, "Could not find CPU device node.\n");
		netdev_warn(lp->ndev,
			    "Setting MDIO clock divisor to default %d\n",
			    DEFAULT_CLOCK_DIVISOR);
		clk_div = DEFAULT_CLOCK_DIVISOR;
		goto issue;
	}
	if (of_property_read_u32(np1, "clock-frequency", &host_clock)) {
		netdev_warn(lp->ndev, "clock-frequency property not found.\n");
		netdev_warn(lp->ndev,
			    "Setting MDIO clock divisor to default %d\n",
			    DEFAULT_CLOCK_DIVISOR);
		clk_div = DEFAULT_CLOCK_DIVISOR;
		of_node_put(np1);
		goto issue;
	}

	clk_div = (host_clock / (MAX_MDIO_FREQ * 2)) - 1;
	/* If there is any remainder from the division of
	 * fHOST / (MAX_MDIO_FREQ * 2), then we need to add
	 * 1 to the clock divisor or we will surely be above 2.5 MHz
	 */
	if (host_clock % (MAX_MDIO_FREQ * 2))
		clk_div++;

	netdev_dbg(lp->ndev,
		   "Setting MDIO clock divisor to %u/%u Hz host clock.\n",
		   clk_div, host_clock);

	of_node_put(np1);
issue:
	axienet_iow(lp, XAE_MDIO_MC_OFFSET,
		    (((u32) clk_div) | XAE_MDIO_MC_MDIOEN_MASK));

	ret = axienet_mdio_wait_until_ready(lp);
	if (ret < 0)
		return ret;

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	np1 = of_get_parent(lp->phy_node);
	of_address_to_resource(np1, 0, &res);
	snprintf(bus->id, MII_BUS_ID_SIZE, "%.8llx",
		 (unsigned long long) res.start);

	bus->priv = lp;
	bus->name = "Xilinx Axi Ethernet MDIO";
	bus->read = axienet_mdio_read;
	bus->write = axienet_mdio_write;
	bus->parent = lp->dev;
	lp->mii_bus = bus;

	ret = of_mdiobus_register(bus, np1);
	if (ret) {
		mdiobus_free(bus);
		lp->mii_bus = NULL;
		return ret;
	}
	return 0;
}

/**
 * axienet_mdio_teardown - MDIO remove function
 * @lp:		Pointer to axienet local data structure.
 *
 * Unregisters the MDIO and frees any associate memory for mii bus.
 */
void axienet_mdio_teardown(struct axienet_local *lp)
{
	mdiobus_unregister(lp->mii_bus);
	mdiobus_free(lp->mii_bus);
	lp->mii_bus = NULL;
}
