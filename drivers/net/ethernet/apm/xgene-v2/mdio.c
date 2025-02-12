// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#include "main.h"

static int xge_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 data)
{
	struct xge_pdata *pdata = bus->priv;
	u32 done, val = 0;
	u8 wait = 10;

	SET_REG_BITS(&val, PHY_ADDR, phy_id);
	SET_REG_BITS(&val, REG_ADDR, reg);
	xge_wr_csr(pdata, MII_MGMT_ADDRESS, val);

	xge_wr_csr(pdata, MII_MGMT_CONTROL, data);
	do {
		usleep_range(5, 10);
		done = xge_rd_csr(pdata, MII_MGMT_INDICATORS);
	} while ((done & MII_MGMT_BUSY) && wait--);

	if (done & MII_MGMT_BUSY) {
		dev_err(&bus->dev, "MII_MGMT write failed\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int xge_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct xge_pdata *pdata = bus->priv;
	u32 data, done, val = 0;
	u8 wait = 10;

	SET_REG_BITS(&val, PHY_ADDR, phy_id);
	SET_REG_BITS(&val, REG_ADDR, reg);
	xge_wr_csr(pdata, MII_MGMT_ADDRESS, val);

	xge_wr_csr(pdata, MII_MGMT_COMMAND, MII_READ_CYCLE);
	do {
		usleep_range(5, 10);
		done = xge_rd_csr(pdata, MII_MGMT_INDICATORS);
	} while ((done & MII_MGMT_BUSY) && wait--);

	if (done & MII_MGMT_BUSY) {
		dev_err(&bus->dev, "MII_MGMT read failed\n");
		return -ETIMEDOUT;
	}

	data = xge_rd_csr(pdata, MII_MGMT_STATUS);
	xge_wr_csr(pdata, MII_MGMT_COMMAND, 0);

	return data;
}

static void xge_adjust_link(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (phydev->link) {
		if (pdata->phy_speed != phydev->speed) {
			pdata->phy_speed = phydev->speed;
			xge_mac_set_speed(pdata);
			xge_mac_enable(pdata);
			phy_print_status(phydev);
		}
	} else {
		if (pdata->phy_speed != SPEED_UNKNOWN) {
			pdata->phy_speed = SPEED_UNKNOWN;
			xge_mac_disable(pdata);
			phy_print_status(phydev);
		}
	}
}

void xge_mdio_remove(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct mii_bus *mdio_bus = pdata->mdio_bus;

	if (ndev->phydev)
		phy_disconnect(ndev->phydev);

	if (mdio_bus->state == MDIOBUS_REGISTERED)
		mdiobus_unregister(mdio_bus);

	mdiobus_free(mdio_bus);
}

int xge_mdio_config(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct mii_bus *mdio_bus;
	struct phy_device *phydev;
	int ret;

	mdio_bus = mdiobus_alloc();
	if (!mdio_bus)
		return -ENOMEM;

	mdio_bus->name = "APM X-Gene Ethernet (v2) MDIO Bus";
	mdio_bus->read = xge_mdio_read;
	mdio_bus->write = xge_mdio_write;
	mdio_bus->priv = pdata;
	mdio_bus->parent = dev;
	snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	pdata->mdio_bus = mdio_bus;

	mdio_bus->phy_mask = 0x1;
	ret = mdiobus_register(mdio_bus);
	if (ret)
		goto err;

	phydev = phy_find_first(mdio_bus);
	if (!phydev) {
		dev_err(dev, "no PHY found\n");
		ret = -ENODEV;
		goto err;
	}
	phydev = phy_connect(ndev, phydev_name(phydev),
			     &xge_adjust_link,
			     pdata->resources.phy_mode);

	if (IS_ERR(phydev)) {
		netdev_err(ndev, "Could not attach to PHY\n");
		ret = PTR_ERR(phydev);
		goto err;
	}

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	pdata->phy_speed = SPEED_UNKNOWN;

	return 0;
err:
	xge_mdio_remove(ndev);

	return ret;
}
