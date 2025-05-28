// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/phy.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_ptp.h"
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

static void ngbe_mac_config(struct phylink_config *config, unsigned int mode,
			    const struct phylink_link_state *state)
{
}

static void ngbe_mac_link_down(struct phylink_config *config,
			       unsigned int mode, phy_interface_t interface)
{
	struct wx *wx = phylink_to_wx(config);

	wx->speed = SPEED_UNKNOWN;
	if (test_bit(WX_STATE_PTP_RUNNING, wx->state))
		wx_ptp_reset_cyclecounter(wx);
}

static void ngbe_mac_link_up(struct phylink_config *config,
			     struct phy_device *phy,
			     unsigned int mode, phy_interface_t interface,
			     int speed, int duplex,
			     bool tx_pause, bool rx_pause)
{
	struct wx *wx = phylink_to_wx(config);
	u32 lan_speed, reg;

	wx_fc_enable(wx, tx_pause, rx_pause);

	switch (speed) {
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

	reg = rd32(wx, WX_MAC_TX_CFG);
	reg &= ~WX_MAC_TX_CFG_SPEED_MASK;
	reg |= WX_MAC_TX_CFG_SPEED_1G | WX_MAC_TX_CFG_TE;
	wr32(wx, WX_MAC_TX_CFG, reg);

	/* Re configure MAC Rx */
	reg = rd32(wx, WX_MAC_RX_CFG);
	wr32(wx, WX_MAC_RX_CFG, reg);
	wr32(wx, WX_MAC_PKT_FLT, WX_MAC_PKT_FLT_PR);
	reg = rd32(wx, WX_MAC_WDG_TIMEOUT);
	wr32(wx, WX_MAC_WDG_TIMEOUT, reg);

	wx->speed = speed;
	wx->last_rx_ptp_check = jiffies;
	if (test_bit(WX_STATE_PTP_RUNNING, wx->state))
		wx_ptp_reset_cyclecounter(wx);
}

static const struct phylink_mac_ops ngbe_mac_ops = {
	.mac_config = ngbe_mac_config,
	.mac_link_down = ngbe_mac_link_down,
	.mac_link_up = ngbe_mac_link_up,
};

static int ngbe_phylink_init(struct wx *wx)
{
	struct phylink_config *config;
	phy_interface_t phy_mode;
	struct phylink *phylink;

	config = &wx->phylink_config;
	config->dev = &wx->netdev->dev;
	config->type = PHYLINK_NETDEV;
	config->mac_capabilities = MAC_1000FD | MAC_100FD | MAC_10FD |
				   MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	config->mac_managed_pm = true;

	/* The MAC only has add the Tx delay and it can not be modified.
	 * So just disable TX delay in PHY, and it is does not matter to
	 * internal phy.
	 */
	phy_mode = PHY_INTERFACE_MODE_RGMII_RXID;
	__set_bit(PHY_INTERFACE_MODE_RGMII_RXID, config->supported_interfaces);

	phylink = phylink_create(config, NULL, phy_mode, &ngbe_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	wx->phylink = phylink;

	return 0;
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

	wx->link = 0;
	wx->speed = 0;
	wx->duplex = 0;

	ret = ngbe_phylink_init(wx);
	if (ret) {
		wx_err(wx, "failed to init phylink: %d\n", ret);
		return ret;
	}

	return 0;
}
