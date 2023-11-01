// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/phy.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "ngbe_type.h"
#include "ngbe_mdio.h"

static int ngbe_phy_read_reg_internal(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct wx *wx = bus->priv;

	if (phy_addr != 0)
		return 0xffff;
	return (u16)rd32(wx, NGBE_PHY_CONFIG(regnum));
}

static int ngbe_phy_write_reg_internal(struct mii_bus *bus, int phy_addr, int regnum, u16 value)
{
	struct wx *wx = bus->priv;

	if (phy_addr == 0)
		wr32(wx, NGBE_PHY_CONFIG(regnum), value);
	return 0;
}

static int ngbe_phy_read_reg_c22(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct wx *wx = bus->priv;
	u16 phy_data;

	if (wx->mac_type == em_mac_type_mdi)
		phy_data = ngbe_phy_read_reg_internal(bus, phy_addr, regnum);
	else
		phy_data = wx_phy_read_reg_mdi_c22(bus, phy_addr, regnum);

	return phy_data;
}

static int ngbe_phy_write_reg_c22(struct mii_bus *bus, int phy_addr,
				  int regnum, u16 value)
{
	struct wx *wx = bus->priv;
	int ret;

	if (wx->mac_type == em_mac_type_mdi)
		ret = ngbe_phy_write_reg_internal(bus, phy_addr, regnum, value);
	else
		ret = wx_phy_write_reg_mdi_c22(bus, phy_addr, regnum, value);

	return ret;
}

static void ngbe_handle_link_change(struct net_device *dev)
{
	struct wx *wx = netdev_priv(dev);
	struct phy_device *phydev;
	u32 lan_speed, reg;

	phydev = wx->phydev;
	if (!(wx->link != phydev->link ||
	      wx->speed != phydev->speed ||
	      wx->duplex != phydev->duplex))
		return;

	wx->link = phydev->link;
	wx->speed = phydev->speed;
	wx->duplex = phydev->duplex;
	switch (phydev->speed) {
	case SPEED_10:
		lan_speed = 0;
		break;
	case SPEED_100:
		lan_speed = 1;
		break;
	case SPEED_1000:
	default:
		lan_speed = 2;
		break;
	}
	wr32m(wx, NGBE_CFG_LAN_SPEED, 0x3, lan_speed);

	if (phydev->link) {
		reg = rd32(wx, WX_MAC_TX_CFG);
		reg &= ~WX_MAC_TX_CFG_SPEED_MASK;
		reg |= WX_MAC_TX_CFG_SPEED_1G | WX_MAC_TX_CFG_TE;
		wr32(wx, WX_MAC_TX_CFG, reg);
		/* Re configure MAC RX */
		reg = rd32(wx, WX_MAC_RX_CFG);
		wr32(wx, WX_MAC_RX_CFG, reg);
		wr32(wx, WX_MAC_PKT_FLT, WX_MAC_PKT_FLT_PR);
		reg = rd32(wx, WX_MAC_WDG_TIMEOUT);
		wr32(wx, WX_MAC_WDG_TIMEOUT, reg);
	}
	phy_print_status(phydev);
}

int ngbe_phy_connect(struct wx *wx)
{
	int ret;

	ret = phy_connect_direct(wx->netdev,
				 wx->phydev,
				 ngbe_handle_link_change,
				 PHY_INTERFACE_MODE_RGMII_ID);
	if (ret) {
		wx_err(wx, "PHY connect failed.\n");
		return ret;
	}

	return 0;
}

static void ngbe_phy_fixup(struct wx *wx)
{
	struct phy_device *phydev = wx->phydev;
	struct ethtool_eee eee;

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	phydev->mac_managed_pm = true;
	if (wx->mac_type != em_mac_type_mdi)
		return;
	/* disable EEE, internal phy does not support eee */
	memset(&eee, 0, sizeof(eee));
	phy_ethtool_set_eee(phydev, &eee);
}

int ngbe_mdio_init(struct wx *wx)
{
	struct pci_dev *pdev = wx->pdev;
	struct mii_bus *mii_bus;
	int ret;

	mii_bus = devm_mdiobus_alloc(&pdev->dev);
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "ngbe_mii_bus";
	mii_bus->read = ngbe_phy_read_reg_c22;
	mii_bus->write = ngbe_phy_write_reg_c22;
	mii_bus->phy_mask = GENMASK(31, 4);
	mii_bus->parent = &pdev->dev;
	mii_bus->priv = wx;

	if (wx->mac_type == em_mac_type_rgmii) {
		mii_bus->read_c45 = wx_phy_read_reg_mdi_c45;
		mii_bus->write_c45 = wx_phy_write_reg_mdi_c45;
	}

	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "ngbe-%x", pci_dev_id(pdev));
	ret = devm_mdiobus_register(&pdev->dev, mii_bus);
	if (ret)
		return ret;

	wx->phydev = phy_find_first(mii_bus);
	if (!wx->phydev)
		return -ENODEV;

	phy_attached_info(wx->phydev);
	ngbe_phy_fixup(wx);

	wx->link = 0;
	wx->speed = 0;
	wx->duplex = 0;

	return 0;
}
