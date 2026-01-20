// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/rtnetlink.h>
#include "hbg_common.h"
#include "hbg_hw.h"
#include "hbg_mdio.h"
#include "hbg_reg.h"

#define HBG_MAC_GET_PRIV(mac) ((struct hbg_priv *)(mac)->mdio_bus->priv)
#define HBG_MII_BUS_GET_MAC(bus) (&((struct hbg_priv *)(bus)->priv)->mac)

#define HBG_MDIO_C22_MODE		0x1
#define HBG_MDIO_C22_REG_WRITE		0x1
#define HBG_MDIO_C22_REG_READ		0x2

#define HBG_MDIO_OP_TIMEOUT_US		(1 * 1000 * 1000)
#define HBG_MDIO_OP_INTERVAL_US		(5 * 1000)

#define HBG_NP_LINK_FAIL_RETRY_TIMES	5

static void hbg_mdio_set_command(struct hbg_mac *mac, u32 cmd)
{
	hbg_reg_write(HBG_MAC_GET_PRIV(mac), HBG_REG_MDIO_COMMAND_ADDR, cmd);
}

static void hbg_mdio_get_command(struct hbg_mac *mac, u32 *cmd)
{
	*cmd = hbg_reg_read(HBG_MAC_GET_PRIV(mac), HBG_REG_MDIO_COMMAND_ADDR);
}

static void hbg_mdio_set_wdata_reg(struct hbg_mac *mac, u16 wdata_value)
{
	hbg_reg_write_field(HBG_MAC_GET_PRIV(mac), HBG_REG_MDIO_WDATA_ADDR,
			    HBG_REG_MDIO_WDATA_M, wdata_value);
}

static u32 hbg_mdio_get_rdata_reg(struct hbg_mac *mac)
{
	return hbg_reg_read_field(HBG_MAC_GET_PRIV(mac),
				  HBG_REG_MDIO_RDATA_ADDR,
				  HBG_REG_MDIO_WDATA_M);
}

static int hbg_mdio_wait_ready(struct hbg_mac *mac)
{
	struct hbg_priv *priv = HBG_MAC_GET_PRIV(mac);
	u32 cmd = 0;
	int ret;

	ret = readl_poll_timeout(priv->io_base + HBG_REG_MDIO_COMMAND_ADDR, cmd,
				 !FIELD_GET(HBG_REG_MDIO_COMMAND_START_B, cmd),
				 HBG_MDIO_OP_INTERVAL_US,
				 HBG_MDIO_OP_TIMEOUT_US);

	return ret ? -ETIMEDOUT : 0;
}

static int hbg_mdio_cmd_send(struct hbg_mac *mac, u32 prt_addr, u32 dev_addr,
			     u32 type, u32 op_code)
{
	u32 cmd = 0;

	hbg_mdio_get_command(mac, &cmd);
	hbg_field_modify(cmd, HBG_REG_MDIO_COMMAND_ST_M, type);
	hbg_field_modify(cmd, HBG_REG_MDIO_COMMAND_OP_M, op_code);
	hbg_field_modify(cmd, HBG_REG_MDIO_COMMAND_PRTAD_M, prt_addr);
	hbg_field_modify(cmd, HBG_REG_MDIO_COMMAND_DEVAD_M, dev_addr);

	/* if auto scan enabled, this value need fix to 0 */
	hbg_field_modify(cmd, HBG_REG_MDIO_COMMAND_START_B, 0x1);

	hbg_mdio_set_command(mac, cmd);

	/* wait operation complete and check the result */
	return hbg_mdio_wait_ready(mac);
}

static int hbg_mdio_read22(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct hbg_mac *mac = HBG_MII_BUS_GET_MAC(bus);
	int ret;

	ret = hbg_mdio_cmd_send(mac, phy_addr, regnum, HBG_MDIO_C22_MODE,
				HBG_MDIO_C22_REG_READ);
	if (ret)
		return ret;

	return hbg_mdio_get_rdata_reg(mac);
}

static int hbg_mdio_write22(struct mii_bus *bus, int phy_addr, int regnum,
			    u16 val)
{
	struct hbg_mac *mac = HBG_MII_BUS_GET_MAC(bus);

	hbg_mdio_set_wdata_reg(mac, val);
	return hbg_mdio_cmd_send(mac, phy_addr, regnum, HBG_MDIO_C22_MODE,
				 HBG_MDIO_C22_REG_WRITE);
}

static void hbg_mdio_init_hw(struct hbg_priv *priv)
{
	u32 freq = priv->dev_specs.mdio_frequency;
	struct hbg_mac *mac = &priv->mac;
	u32 cmd = 0;

	cmd |= FIELD_PREP(HBG_REG_MDIO_COMMAND_ST_M, HBG_MDIO_C22_MODE);
	cmd |= FIELD_PREP(HBG_REG_MDIO_COMMAND_AUTO_SCAN_B, HBG_STATUS_DISABLE);

	/* freq use two bits, which are stored in clk_sel and clk_sel_exp */
	cmd |= FIELD_PREP(HBG_REG_MDIO_COMMAND_CLK_SEL_B, freq & 0x1);
	cmd |= FIELD_PREP(HBG_REG_MDIO_COMMAND_CLK_SEL_EXP_B,
			  (freq >> 1) & 0x1);

	hbg_mdio_set_command(mac, cmd);
}

static void hbg_flowctrl_cfg(struct hbg_priv *priv)
{
	struct phy_device *phydev = priv->mac.phydev;
	bool rx_pause;
	bool tx_pause;

	if (!priv->mac.pause_autoneg)
		return;

	phy_get_pause(phydev, &tx_pause, &rx_pause);
	hbg_hw_set_pause_enable(priv, tx_pause, rx_pause);
}

void hbg_fix_np_link_fail(struct hbg_priv *priv)
{
	struct device *dev = &priv->pdev->dev;

	rtnl_lock();

	if (priv->stats.np_link_fail_cnt >= HBG_NP_LINK_FAIL_RETRY_TIMES) {
		dev_err(dev, "failed to fix the MAC link status\n");
		priv->stats.np_link_fail_cnt = 0;
		goto unlock;
	}

	if (!priv->mac.phydev->link)
		goto unlock;

	priv->stats.np_link_fail_cnt++;
	dev_err(dev, "failed to link between MAC and PHY, try to fix...\n");

	/* Replace phy_reset() with phy_stop() and phy_start(),
	 * as suggested by Andrew.
	 */
	hbg_phy_stop(priv);
	hbg_phy_start(priv);

unlock:
	rtnl_unlock();
}

static void hbg_phy_adjust_link(struct net_device *netdev)
{
	struct hbg_priv *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	u32 speed;

	if (phydev->link != priv->mac.link_status) {
		if (phydev->link) {
			switch (phydev->speed) {
			case SPEED_10:
				speed = HBG_PORT_MODE_SGMII_10M;
				break;
			case SPEED_100:
				speed = HBG_PORT_MODE_SGMII_100M;
				break;
			case SPEED_1000:
				speed = HBG_PORT_MODE_SGMII_1000M;
				break;
			default:
				return;
			}

			priv->mac.speed = speed;
			priv->mac.duplex = phydev->duplex;
			priv->mac.autoneg = phydev->autoneg;
			hbg_hw_adjust_link(priv, speed, phydev->duplex);
			hbg_flowctrl_cfg(priv);
		}

		priv->mac.link_status = phydev->link;
		phy_print_status(phydev);
	}
}

static void hbg_phy_disconnect(void *data)
{
	phy_disconnect((struct phy_device *)data);
}

static int hbg_phy_connect(struct hbg_priv *priv)
{
	struct phy_device *phydev = priv->mac.phydev;
	struct device *dev = &priv->pdev->dev;
	int ret;

	ret = phy_connect_direct(priv->netdev, phydev, hbg_phy_adjust_link,
				 PHY_INTERFACE_MODE_SGMII);
	if (ret)
		return dev_err_probe(dev, ret, "failed to connect phy\n");

	ret = devm_add_action_or_reset(dev, hbg_phy_disconnect, phydev);
	if (ret)
		return ret;

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_support_asym_pause(phydev);
	phy_attached_info(phydev);

	return 0;
}

void hbg_phy_start(struct hbg_priv *priv)
{
	phy_start(priv->mac.phydev);
}

void hbg_phy_stop(struct hbg_priv *priv)
{
	phy_stop(priv->mac.phydev);
}

static void hbg_fixed_phy_uninit(void *data)
{
	fixed_phy_unregister((struct phy_device *)data);
}

static int hbg_fixed_phy_init(struct hbg_priv *priv)
{
	struct fixed_phy_status hbg_fixed_phy_status = {
		.link = 1,
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
		.pause = 1,
		.asym_pause = 1,
	};
	struct device *dev = &priv->pdev->dev;
	struct phy_device *phydev;
	int ret;

	phydev = fixed_phy_register(&hbg_fixed_phy_status, NULL);
	if (IS_ERR(phydev)) {
		dev_err_probe(dev, PTR_ERR(phydev),
			      "failed to register fixed PHY device\n");
		return PTR_ERR(phydev);
	}

	ret = devm_add_action_or_reset(dev, hbg_fixed_phy_uninit, phydev);
	if (ret)
		return ret;

	priv->mac.phydev = phydev;
	return hbg_phy_connect(priv);
}

int hbg_mdio_init(struct hbg_priv *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct hbg_mac *mac = &priv->mac;
	struct phy_device *phydev;
	struct mii_bus *mdio_bus;
	int ret;

	mac->phy_addr = priv->dev_specs.phy_addr;
	if (mac->phy_addr == HBG_NO_PHY)
		return hbg_fixed_phy_init(priv);

	mdio_bus = devm_mdiobus_alloc(dev);
	if (!mdio_bus)
		return -ENOMEM;

	mdio_bus->parent = dev;
	mdio_bus->priv = priv;
	mdio_bus->phy_mask = ~(1 << mac->phy_addr);
	mdio_bus->name = "hibmcge mii bus";
	mac->mdio_bus = mdio_bus;

	mdio_bus->read = hbg_mdio_read22;
	mdio_bus->write = hbg_mdio_write22;
	snprintf(mdio_bus->id, MII_BUS_ID_SIZE, "%s-%s", "mii", dev_name(dev));

	ret = devm_mdiobus_register(dev, mdio_bus);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register MDIO bus\n");

	phydev = mdiobus_get_phy(mdio_bus, mac->phy_addr);
	if (!phydev)
		return dev_err_probe(dev, -ENODEV,
				     "failed to get phy device\n");

	mac->phydev = phydev;
	hbg_mdio_init_hw(priv);
	return hbg_phy_connect(priv);
}
