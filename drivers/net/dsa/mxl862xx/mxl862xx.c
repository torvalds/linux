// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-host.h"

#define MXL862XX_API_WRITE(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), false, false)
#define MXL862XX_API_READ(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, false)
#define MXL862XX_API_READ_QUIET(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, true)

#define MXL862XX_SDMA_PCTRLP(p)		(0xbc0 + ((p) * 0x6))
#define MXL862XX_SDMA_PCTRL_EN		BIT(0)

#define MXL862XX_FDMA_PCTRLP(p)		(0xa80 + ((p) * 0x6))
#define MXL862XX_FDMA_PCTRL_EN		BIT(0)

#define MXL862XX_READY_TIMEOUT_MS	10000
#define MXL862XX_READY_POLL_MS		100

static enum dsa_tag_protocol mxl862xx_get_tag_protocol(struct dsa_switch *ds,
						       int port,
						       enum dsa_tag_protocol m)
{
	return DSA_TAG_PROTO_MXL862;
}

/* PHY access via firmware relay */
static int mxl862xx_phy_read_mmd(struct mxl862xx_priv *priv, int port,
				 int devadd, int reg)
{
	struct mdio_relay_data param = {
		.phy = port,
		.mmd = devadd,
		.reg = cpu_to_le16(reg),
	};
	int ret;

	ret = MXL862XX_API_READ(priv, INT_GPHY_READ, param);
	if (ret)
		return ret;

	return le16_to_cpu(param.data);
}

static int mxl862xx_phy_write_mmd(struct mxl862xx_priv *priv, int port,
				  int devadd, int reg, u16 data)
{
	struct mdio_relay_data param = {
		.phy = port,
		.mmd = devadd,
		.reg = cpu_to_le16(reg),
		.data = cpu_to_le16(data),
	};

	return MXL862XX_API_WRITE(priv, INT_GPHY_WRITE, param);
}

static int mxl862xx_phy_read_mii_bus(struct mii_bus *bus, int port, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, port, 0, regnum);
}

static int mxl862xx_phy_write_mii_bus(struct mii_bus *bus, int port,
				      int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, port, 0, regnum, val);
}

static int mxl862xx_phy_read_c45_mii_bus(struct mii_bus *bus, int port,
					 int devadd, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, port, devadd, regnum);
}

static int mxl862xx_phy_write_c45_mii_bus(struct mii_bus *bus, int port,
					  int devadd, int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, port, devadd, regnum, val);
}

static int mxl862xx_wait_ready(struct dsa_switch *ds)
{
	struct mxl862xx_sys_fw_image_version ver = {};
	unsigned long start = jiffies, timeout;
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_cfg cfg = {};
	int ret;

	timeout = start + msecs_to_jiffies(MXL862XX_READY_TIMEOUT_MS);
	msleep(2000); /* it always takes at least 2 seconds */
	do {
		ret = MXL862XX_API_READ_QUIET(priv, SYS_MISC_FW_VERSION, ver);
		if (ret || !ver.iv_major)
			goto not_ready_yet;

		/* being able to perform CFGGET indicates that
		 * the firmware is ready
		 */
		ret = MXL862XX_API_READ_QUIET(priv,
					      MXL862XX_COMMON_CFGGET,
					      cfg);
		if (ret)
			goto not_ready_yet;

		dev_info(ds->dev, "switch ready after %ums, firmware %u.%u.%u (build %u)\n",
			 jiffies_to_msecs(jiffies - start),
			 ver.iv_major, ver.iv_minor,
			 le16_to_cpu(ver.iv_revision),
			 le32_to_cpu(ver.iv_build_num));
		return 0;

not_ready_yet:
		msleep(MXL862XX_READY_POLL_MS);
	} while (time_before(jiffies, timeout));

	dev_err(ds->dev, "switch not responding after reset\n");
	return -ETIMEDOUT;
}

static int mxl862xx_setup_mdio(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct device *dev = ds->dev;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = priv;
	ds->user_mii_bus = bus;
	bus->name = KBUILD_MODNAME "-mii";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	bus->read_c45 = mxl862xx_phy_read_c45_mii_bus;
	bus->write_c45 = mxl862xx_phy_write_c45_mii_bus;
	bus->read = mxl862xx_phy_read_mii_bus;
	bus->write = mxl862xx_phy_write_mii_bus;
	bus->parent = dev;
	bus->phy_mask = ~ds->phys_mii_mask;

	mdio_np = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_np)
		return -ENODEV;

	ret = devm_of_mdiobus_register(dev, bus, mdio_np);
	of_node_put(mdio_np);

	return ret;
}

static int mxl862xx_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	ret = mxl862xx_reset(priv);
	if (ret)
		return ret;

	ret = mxl862xx_wait_ready(ds);
	if (ret)
		return ret;

	return mxl862xx_setup_mdio(ds);
}

static int mxl862xx_port_state(struct dsa_switch *ds, int port, bool enable)
{
	struct mxl862xx_register_mod sdma = {
		.addr = cpu_to_le16(MXL862XX_SDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_SDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_SDMA_PCTRL_EN),
	};
	struct mxl862xx_register_mod fdma = {
		.addr = cpu_to_le16(MXL862XX_FDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_FDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_FDMA_PCTRL_EN),
	};
	int ret;

	ret = MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, sdma);
	if (ret)
		return ret;

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, fdma);
}

static int mxl862xx_port_enable(struct dsa_switch *ds, int port,
				struct phy_device *phydev)
{
	return mxl862xx_port_state(ds, port, true);
}

static void mxl862xx_port_disable(struct dsa_switch *ds, int port)
{
	if (mxl862xx_port_state(ds, port, false))
		dev_err(ds->dev, "failed to disable port %d\n", port);
}

static void mxl862xx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct mxl862xx_mac_table_clear param = {
		.type = MXL862XX_MAC_CLEAR_PHY_PORT,
		.port_id = port,
	};

	if (MXL862XX_API_WRITE(ds->priv, MXL862XX_MAC_TABLECLEARCOND, param))
		dev_err(ds->dev, "failed to clear fdb on port %d\n", port);
}

static int mxl862xx_configure_ctp_port(struct dsa_switch *ds, int port,
				       u16 first_ctp_port_id,
				       u16 number_of_ctp_ports)
{
	struct mxl862xx_ctp_port_assignment ctp_assign = {
		.logical_port_id = port,
		.first_ctp_port_id = cpu_to_le16(first_ctp_port_id),
		.number_of_ctp_port = cpu_to_le16(number_of_ctp_ports),
		.mode = cpu_to_le32(MXL862XX_LOGICAL_PORT_ETHERNET),
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_CTP_PORTASSIGNMENTSET,
				  ctp_assign);
}

static int mxl862xx_configure_sp_tag_proto(struct dsa_switch *ds, int port,
					   bool enable)
{
	struct mxl862xx_ss_sp_tag tag = {
		.pid = port,
		.mask = MXL862XX_SS_SP_TAG_MASK_RX | MXL862XX_SS_SP_TAG_MASK_TX,
		.rx = enable ? MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT :
			       MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT,
		.tx = enable ? MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE :
			       MXL862XX_SS_SP_TAG_TX_TAG_REMOVE,
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_SS_SPTAG_SET, tag);
}

static int mxl862xx_setup_cpu_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;
	u16 bridge_port_map = 0;
	struct dsa_port *dp;

	/* CPU port bridge setup */
	br_port_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING);

	br_port_cfg.bridge_port_id = cpu_to_le16(port);
	br_port_cfg.src_mac_learning_disable = false;
	br_port_cfg.vlan_src_mac_vid_enable = true;
	br_port_cfg.vlan_dst_mac_vid_enable = true;

	/* include all assigned user ports in the CPU portmap */
	dsa_switch_for_each_user_port(dp, ds) {
		/* it's safe to rely on cpu_dp being valid for user ports */
		if (dp->cpu_dp->index != port)
			continue;

		bridge_port_map |= BIT(dp->index);
	}
	br_port_cfg.bridge_port_map[0] |= cpu_to_le16(bridge_port_map);

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, br_port_cfg);
}

static int mxl862xx_add_single_port_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_bridge_alloc br_alloc = {};
	int ret;

	ret = MXL862XX_API_READ(ds->priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret) {
		dev_err(ds->dev, "failed to allocate a bridge for port %d\n", port);
		return ret;
	}

	br_port_cfg.bridge_id = br_alloc.bridge_id;
	br_port_cfg.bridge_port_id = cpu_to_le16(port);
	br_port_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING);
	br_port_cfg.src_mac_learning_disable = true;
	br_port_cfg.vlan_src_mac_vid_enable = false;
	br_port_cfg.vlan_dst_mac_vid_enable = false;
	/* As this function is only called for user ports it is safe to rely on
	 * cpu_dp being valid
	 */
	br_port_cfg.bridge_port_map[0] = cpu_to_le16(BIT(dp->cpu_dp->index));

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_BRIDGEPORT_CONFIGSET, br_port_cfg);
}

static int mxl862xx_port_setup(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	bool is_cpu_port = dsa_port_is_cpu(dp);
	int ret;

	/* disable port and flush MAC entries */
	ret = mxl862xx_port_state(ds, port, false);
	if (ret)
		return ret;

	mxl862xx_port_fast_age(ds, port);

	/* skip setup for unused and DSA ports */
	if (dsa_port_is_unused(dp) ||
	    dsa_port_is_dsa(dp))
		return 0;

	/* configure tag protocol */
	ret = mxl862xx_configure_sp_tag_proto(ds, port, is_cpu_port);
	if (ret)
		return ret;

	/* assign CTP port IDs */
	ret = mxl862xx_configure_ctp_port(ds, port, port,
					  is_cpu_port ? 32 - port : 1);
	if (ret)
		return ret;

	if (is_cpu_port)
		/* assign user ports to CPU port bridge */
		return mxl862xx_setup_cpu_bridge(ds, port);

	/* setup single-port bridge for user ports */
	return mxl862xx_add_single_port_bridge(ds, port);
}

static void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
				      struct phylink_config *config)
{
	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_INTERNAL,
		  config->supported_interfaces);
}

static const struct dsa_switch_ops mxl862xx_switch_ops = {
	.get_tag_protocol = mxl862xx_get_tag_protocol,
	.setup = mxl862xx_setup,
	.port_setup = mxl862xx_port_setup,
	.phylink_get_caps = mxl862xx_phylink_get_caps,
	.port_enable = mxl862xx_port_enable,
	.port_disable = mxl862xx_port_disable,
	.port_fast_age = mxl862xx_port_fast_age,
};

static void mxl862xx_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
}

static void mxl862xx_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
}

static void mxl862xx_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
}

static const struct phylink_mac_ops mxl862xx_phylink_mac_ops = {
	.mac_config = mxl862xx_phylink_mac_config,
	.mac_link_down = mxl862xx_phylink_mac_link_down,
	.mac_link_up = mxl862xx_phylink_mac_link_up,
};

static int mxl862xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct mxl862xx_priv *priv;
	struct dsa_switch *ds;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mdiodev = mdiodev;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	priv->ds = ds;
	ds->dev = dev;
	ds->priv = priv;
	ds->ops = &mxl862xx_switch_ops;
	ds->phylink_mac_ops = &mxl862xx_phylink_mac_ops;
	ds->num_ports = MXL862XX_MAX_PORTS;

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds);
}

static void mxl862xx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_unregister_switch(ds);
}

static void mxl862xx_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_switch_shutdown(ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id mxl862xx_of_match[] = {
	{ .compatible = "maxlinear,mxl86282" },
	{ .compatible = "maxlinear,mxl86252" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxl862xx_of_match);

static struct mdio_driver mxl862xx_driver = {
	.probe  = mxl862xx_probe,
	.remove = mxl862xx_remove,
	.shutdown = mxl862xx_shutdown,
	.mdiodrv.driver = {
		.name = "mxl862xx",
		.of_match_table = mxl862xx_of_match,
	},
};

mdio_module_driver(mxl862xx_driver);

MODULE_DESCRIPTION("Driver for MaxLinear MxL862xx switch family");
MODULE_LICENSE("GPL");
