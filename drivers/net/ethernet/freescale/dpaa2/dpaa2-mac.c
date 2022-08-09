// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include <linux/acpi.h>
#include <linux/pcs-lynx.h>
#include <linux/phy/phy.h>
#include <linux/property.h>

#include "dpaa2-eth.h"
#include "dpaa2-mac.h"

#define phylink_to_dpaa2_mac(config) \
	container_of((config), struct dpaa2_mac, phylink_config)

#define DPMAC_PROTOCOL_CHANGE_VER_MAJOR		4
#define DPMAC_PROTOCOL_CHANGE_VER_MINOR		8

#define DPAA2_MAC_FEATURE_PROTOCOL_CHANGE	BIT(0)

static int dpaa2_mac_cmp_ver(struct dpaa2_mac *mac,
			     u16 ver_major, u16 ver_minor)
{
	if (mac->ver_major == ver_major)
		return mac->ver_minor - ver_minor;
	return mac->ver_major - ver_major;
}

static void dpaa2_mac_detect_features(struct dpaa2_mac *mac)
{
	mac->features = 0;

	if (dpaa2_mac_cmp_ver(mac, DPMAC_PROTOCOL_CHANGE_VER_MAJOR,
			      DPMAC_PROTOCOL_CHANGE_VER_MINOR) >= 0)
		mac->features |= DPAA2_MAC_FEATURE_PROTOCOL_CHANGE;
}

static int phy_mode(enum dpmac_eth_if eth_if, phy_interface_t *if_mode)
{
	*if_mode = PHY_INTERFACE_MODE_NA;

	switch (eth_if) {
	case DPMAC_ETH_IF_RGMII:
		*if_mode = PHY_INTERFACE_MODE_RGMII;
		break;
	case DPMAC_ETH_IF_USXGMII:
		*if_mode = PHY_INTERFACE_MODE_USXGMII;
		break;
	case DPMAC_ETH_IF_QSGMII:
		*if_mode = PHY_INTERFACE_MODE_QSGMII;
		break;
	case DPMAC_ETH_IF_SGMII:
		*if_mode = PHY_INTERFACE_MODE_SGMII;
		break;
	case DPMAC_ETH_IF_XFI:
		*if_mode = PHY_INTERFACE_MODE_10GBASER;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum dpmac_eth_if dpmac_eth_if_mode(phy_interface_t if_mode)
{
	switch (if_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return DPMAC_ETH_IF_RGMII;
	case PHY_INTERFACE_MODE_USXGMII:
		return DPMAC_ETH_IF_USXGMII;
	case PHY_INTERFACE_MODE_QSGMII:
		return DPMAC_ETH_IF_QSGMII;
	case PHY_INTERFACE_MODE_SGMII:
		return DPMAC_ETH_IF_SGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return DPMAC_ETH_IF_XFI;
	case PHY_INTERFACE_MODE_1000BASEX:
		return DPMAC_ETH_IF_1000BASEX;
	default:
		return DPMAC_ETH_IF_MII;
	}
}

static struct fwnode_handle *dpaa2_mac_get_node(struct device *dev,
						u16 dpmac_id)
{
	struct fwnode_handle *fwnode, *parent = NULL, *child  = NULL;
	struct device_node *dpmacs = NULL;
	int err;
	u32 id;

	fwnode = dev_fwnode(dev->parent);
	if (is_of_node(fwnode)) {
		dpmacs = of_find_node_by_name(NULL, "dpmacs");
		if (!dpmacs)
			return NULL;
		parent = of_fwnode_handle(dpmacs);
	} else if (is_acpi_node(fwnode)) {
		parent = fwnode;
	} else {
		/* The root dprc device didn't yet get to finalize it's probe,
		 * thus the fwnode field is not yet set. Defer probe if we are
		 * facing this situation.
		 */
		return ERR_PTR(-EPROBE_DEFER);
	}

	if (!parent)
		return NULL;

	fwnode_for_each_child_node(parent, child) {
		err = -EINVAL;
		if (is_acpi_device_node(child))
			err = acpi_get_local_address(ACPI_HANDLE_FWNODE(child), &id);
		else if (is_of_node(child))
			err = of_property_read_u32(to_of_node(child), "reg", &id);
		if (err)
			continue;

		if (id == dpmac_id) {
			of_node_put(dpmacs);
			return child;
		}
	}
	of_node_put(dpmacs);
	return NULL;
}

static int dpaa2_mac_get_if_mode(struct fwnode_handle *dpmac_node,
				 struct dpmac_attr attr)
{
	phy_interface_t if_mode;
	int err;

	err = fwnode_get_phy_mode(dpmac_node);
	if (err > 0)
		return err;

	err = phy_mode(attr.eth_if, &if_mode);
	if (!err)
		return if_mode;

	return err;
}

static struct phylink_pcs *dpaa2_mac_select_pcs(struct phylink_config *config,
						phy_interface_t interface)
{
	struct dpaa2_mac *mac = phylink_to_dpaa2_mac(config);

	return mac->pcs;
}

static void dpaa2_mac_config(struct phylink_config *config, unsigned int mode,
			     const struct phylink_link_state *state)
{
	struct dpaa2_mac *mac = phylink_to_dpaa2_mac(config);
	struct dpmac_link_state *dpmac_state = &mac->state;
	int err;

	if (state->an_enabled)
		dpmac_state->options |= DPMAC_LINK_OPT_AUTONEG;
	else
		dpmac_state->options &= ~DPMAC_LINK_OPT_AUTONEG;

	err = dpmac_set_link_state(mac->mc_io, 0,
				   mac->mc_dev->mc_handle, dpmac_state);
	if (err)
		netdev_err(mac->net_dev, "%s: dpmac_set_link_state() = %d\n",
			   __func__, err);

	if (!mac->serdes_phy)
		return;

	/* This happens only if we support changing of protocol at runtime */
	err = dpmac_set_protocol(mac->mc_io, 0, mac->mc_dev->mc_handle,
				 dpmac_eth_if_mode(state->interface));
	if (err)
		netdev_err(mac->net_dev,  "dpmac_set_protocol() = %d\n", err);

	err = phy_set_mode_ext(mac->serdes_phy, PHY_MODE_ETHERNET, state->interface);
	if (err)
		netdev_err(mac->net_dev, "phy_set_mode_ext() = %d\n", err);
}

static void dpaa2_mac_link_up(struct phylink_config *config,
			      struct phy_device *phy,
			      unsigned int mode, phy_interface_t interface,
			      int speed, int duplex,
			      bool tx_pause, bool rx_pause)
{
	struct dpaa2_mac *mac = phylink_to_dpaa2_mac(config);
	struct dpmac_link_state *dpmac_state = &mac->state;
	int err;

	dpmac_state->up = 1;

	dpmac_state->rate = speed;

	if (duplex == DUPLEX_HALF)
		dpmac_state->options |= DPMAC_LINK_OPT_HALF_DUPLEX;
	else if (duplex == DUPLEX_FULL)
		dpmac_state->options &= ~DPMAC_LINK_OPT_HALF_DUPLEX;

	if (rx_pause)
		dpmac_state->options |= DPMAC_LINK_OPT_PAUSE;
	else
		dpmac_state->options &= ~DPMAC_LINK_OPT_PAUSE;

	if (rx_pause ^ tx_pause)
		dpmac_state->options |= DPMAC_LINK_OPT_ASYM_PAUSE;
	else
		dpmac_state->options &= ~DPMAC_LINK_OPT_ASYM_PAUSE;

	err = dpmac_set_link_state(mac->mc_io, 0,
				   mac->mc_dev->mc_handle, dpmac_state);
	if (err)
		netdev_err(mac->net_dev, "%s: dpmac_set_link_state() = %d\n",
			   __func__, err);
}

static void dpaa2_mac_link_down(struct phylink_config *config,
				unsigned int mode,
				phy_interface_t interface)
{
	struct dpaa2_mac *mac = phylink_to_dpaa2_mac(config);
	struct dpmac_link_state *dpmac_state = &mac->state;
	int err;

	dpmac_state->up = 0;
	err = dpmac_set_link_state(mac->mc_io, 0,
				   mac->mc_dev->mc_handle, dpmac_state);
	if (err)
		netdev_err(mac->net_dev, "dpmac_set_link_state() = %d\n", err);
}

static const struct phylink_mac_ops dpaa2_mac_phylink_ops = {
	.validate = phylink_generic_validate,
	.mac_select_pcs = dpaa2_mac_select_pcs,
	.mac_config = dpaa2_mac_config,
	.mac_link_up = dpaa2_mac_link_up,
	.mac_link_down = dpaa2_mac_link_down,
};

static int dpaa2_pcs_create(struct dpaa2_mac *mac,
			    struct fwnode_handle *dpmac_node,
			    int id)
{
	struct mdio_device *mdiodev;
	struct fwnode_handle *node;

	node = fwnode_find_reference(dpmac_node, "pcs-handle", 0);
	if (IS_ERR(node)) {
		/* do not error out on old DTS files */
		netdev_warn(mac->net_dev, "pcs-handle node not found\n");
		return 0;
	}

	if (!fwnode_device_is_available(node)) {
		netdev_err(mac->net_dev, "pcs-handle node not available\n");
		fwnode_handle_put(node);
		return -ENODEV;
	}

	mdiodev = fwnode_mdio_find_device(node);
	fwnode_handle_put(node);
	if (!mdiodev)
		return -EPROBE_DEFER;

	mac->pcs = lynx_pcs_create(mdiodev);
	if (!mac->pcs) {
		netdev_err(mac->net_dev, "lynx_pcs_create() failed\n");
		put_device(&mdiodev->dev);
		return -ENOMEM;
	}

	return 0;
}

static void dpaa2_pcs_destroy(struct dpaa2_mac *mac)
{
	struct phylink_pcs *phylink_pcs = mac->pcs;

	if (phylink_pcs) {
		struct mdio_device *mdio = lynx_get_mdio_device(phylink_pcs);
		struct device *dev = &mdio->dev;

		lynx_pcs_destroy(phylink_pcs);
		put_device(dev);
		mac->pcs = NULL;
	}
}

static void dpaa2_mac_set_supported_interfaces(struct dpaa2_mac *mac)
{
	int intf, err;

	/* We support the current interface mode, and if we have a PCS
	 * similar interface modes that do not require the SerDes lane to be
	 * reconfigured.
	 */
	__set_bit(mac->if_mode, mac->phylink_config.supported_interfaces);
	if (mac->pcs) {
		switch (mac->if_mode) {
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_SGMII:
			__set_bit(PHY_INTERFACE_MODE_1000BASEX,
				  mac->phylink_config.supported_interfaces);
			__set_bit(PHY_INTERFACE_MODE_SGMII,
				  mac->phylink_config.supported_interfaces);
			break;

		default:
			break;
		}
	}

	if (!mac->serdes_phy)
		return;

	/* In case we have access to the SerDes phy/lane, then ask the SerDes
	 * driver what interfaces are supported based on the current PLL
	 * configuration.
	 */
	for (intf = 0; intf < PHY_INTERFACE_MODE_MAX; intf++) {
		if (intf == PHY_INTERFACE_MODE_NA)
			continue;

		err = phy_validate(mac->serdes_phy, PHY_MODE_ETHERNET, intf, NULL);
		if (err)
			continue;

		__set_bit(intf, mac->phylink_config.supported_interfaces);
	}
}

void dpaa2_mac_start(struct dpaa2_mac *mac)
{
	if (mac->serdes_phy)
		phy_power_on(mac->serdes_phy);
}

void dpaa2_mac_stop(struct dpaa2_mac *mac)
{
	if (mac->serdes_phy)
		phy_power_off(mac->serdes_phy);
}

int dpaa2_mac_connect(struct dpaa2_mac *mac)
{
	struct net_device *net_dev = mac->net_dev;
	struct fwnode_handle *dpmac_node;
	struct phy *serdes_phy = NULL;
	struct phylink *phylink;
	int err;

	mac->if_link_type = mac->attr.link_type;

	dpmac_node = mac->fw_node;
	if (!dpmac_node) {
		netdev_err(net_dev, "No dpmac@%d node found.\n", mac->attr.id);
		return -ENODEV;
	}

	err = dpaa2_mac_get_if_mode(dpmac_node, mac->attr);
	if (err < 0)
		return -EINVAL;
	mac->if_mode = err;

	if (mac->features & DPAA2_MAC_FEATURE_PROTOCOL_CHANGE &&
	    !phy_interface_mode_is_rgmii(mac->if_mode) &&
	    is_of_node(dpmac_node)) {
		serdes_phy = of_phy_get(to_of_node(dpmac_node), NULL);

		if (serdes_phy == ERR_PTR(-ENODEV))
			serdes_phy = NULL;
		else if (IS_ERR(serdes_phy))
			return PTR_ERR(serdes_phy);
		else
			phy_init(serdes_phy);
	}
	mac->serdes_phy = serdes_phy;

	/* The MAC does not have the capability to add RGMII delays so
	 * error out if the interface mode requests them and there is no PHY
	 * to act upon them
	 */
	if (of_phy_is_fixed_link(to_of_node(dpmac_node)) &&
	    (mac->if_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	     mac->if_mode == PHY_INTERFACE_MODE_RGMII_RXID ||
	     mac->if_mode == PHY_INTERFACE_MODE_RGMII_TXID)) {
		netdev_err(net_dev, "RGMII delay not supported\n");
		return -EINVAL;
	}

	if ((mac->attr.link_type == DPMAC_LINK_TYPE_PHY &&
	     mac->attr.eth_if != DPMAC_ETH_IF_RGMII) ||
	    mac->attr.link_type == DPMAC_LINK_TYPE_BACKPLANE) {
		err = dpaa2_pcs_create(mac, dpmac_node, mac->attr.id);
		if (err)
			return err;
	}

	memset(&mac->phylink_config, 0, sizeof(mac->phylink_config));
	mac->phylink_config.dev = &net_dev->dev;
	mac->phylink_config.type = PHYLINK_NETDEV;

	mac->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
		MAC_10FD | MAC_100FD | MAC_1000FD | MAC_2500FD | MAC_5000FD |
		MAC_10000FD;

	dpaa2_mac_set_supported_interfaces(mac);

	phylink = phylink_create(&mac->phylink_config,
				 dpmac_node, mac->if_mode,
				 &dpaa2_mac_phylink_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		goto err_pcs_destroy;
	}
	mac->phylink = phylink;

	err = phylink_fwnode_phy_connect(mac->phylink, dpmac_node, 0);
	if (err) {
		netdev_err(net_dev, "phylink_fwnode_phy_connect() = %d\n", err);
		goto err_phylink_destroy;
	}

	return 0;

err_phylink_destroy:
	phylink_destroy(mac->phylink);
err_pcs_destroy:
	dpaa2_pcs_destroy(mac);

	return err;
}

void dpaa2_mac_disconnect(struct dpaa2_mac *mac)
{
	if (!mac->phylink)
		return;

	phylink_disconnect_phy(mac->phylink);
	phylink_destroy(mac->phylink);
	dpaa2_pcs_destroy(mac);
	of_phy_put(mac->serdes_phy);
	mac->serdes_phy = NULL;
}

int dpaa2_mac_open(struct dpaa2_mac *mac)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;
	struct net_device *net_dev = mac->net_dev;
	struct fwnode_handle *fw_node;
	int err;

	err = dpmac_open(mac->mc_io, 0, dpmac_dev->obj_desc.id,
			 &dpmac_dev->mc_handle);
	if (err || !dpmac_dev->mc_handle) {
		netdev_err(net_dev, "dpmac_open() = %d\n", err);
		return -ENODEV;
	}

	err = dpmac_get_attributes(mac->mc_io, 0, dpmac_dev->mc_handle,
				   &mac->attr);
	if (err) {
		netdev_err(net_dev, "dpmac_get_attributes() = %d\n", err);
		goto err_close_dpmac;
	}

	err = dpmac_get_api_version(mac->mc_io, 0, &mac->ver_major, &mac->ver_minor);
	if (err) {
		netdev_err(net_dev, "dpmac_get_api_version() = %d\n", err);
		goto err_close_dpmac;
	}

	dpaa2_mac_detect_features(mac);

	/* Find the device node representing the MAC device and link the device
	 * behind the associated netdev to it.
	 */
	fw_node = dpaa2_mac_get_node(&mac->mc_dev->dev, mac->attr.id);
	if (IS_ERR(fw_node)) {
		err = PTR_ERR(fw_node);
		goto err_close_dpmac;
	}

	mac->fw_node = fw_node;
	net_dev->dev.of_node = to_of_node(mac->fw_node);

	return 0;

err_close_dpmac:
	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
	return err;
}

void dpaa2_mac_close(struct dpaa2_mac *mac)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;

	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
	if (mac->fw_node)
		fwnode_handle_put(mac->fw_node);
}

static char dpaa2_mac_ethtool_stats[][ETH_GSTRING_LEN] = {
	[DPMAC_CNT_ING_ALL_FRAME]		= "[mac] rx all frames",
	[DPMAC_CNT_ING_GOOD_FRAME]		= "[mac] rx frames ok",
	[DPMAC_CNT_ING_ERR_FRAME]		= "[mac] rx frame errors",
	[DPMAC_CNT_ING_FRAME_DISCARD]		= "[mac] rx frame discards",
	[DPMAC_CNT_ING_UCAST_FRAME]		= "[mac] rx u-cast",
	[DPMAC_CNT_ING_BCAST_FRAME]		= "[mac] rx b-cast",
	[DPMAC_CNT_ING_MCAST_FRAME]		= "[mac] rx m-cast",
	[DPMAC_CNT_ING_FRAME_64]		= "[mac] rx 64 bytes",
	[DPMAC_CNT_ING_FRAME_127]		= "[mac] rx 65-127 bytes",
	[DPMAC_CNT_ING_FRAME_255]		= "[mac] rx 128-255 bytes",
	[DPMAC_CNT_ING_FRAME_511]		= "[mac] rx 256-511 bytes",
	[DPMAC_CNT_ING_FRAME_1023]		= "[mac] rx 512-1023 bytes",
	[DPMAC_CNT_ING_FRAME_1518]		= "[mac] rx 1024-1518 bytes",
	[DPMAC_CNT_ING_FRAME_1519_MAX]		= "[mac] rx 1519-max bytes",
	[DPMAC_CNT_ING_FRAG]			= "[mac] rx frags",
	[DPMAC_CNT_ING_JABBER]			= "[mac] rx jabber",
	[DPMAC_CNT_ING_ALIGN_ERR]		= "[mac] rx align errors",
	[DPMAC_CNT_ING_OVERSIZED]		= "[mac] rx oversized",
	[DPMAC_CNT_ING_VALID_PAUSE_FRAME]	= "[mac] rx pause",
	[DPMAC_CNT_ING_BYTE]			= "[mac] rx bytes",
	[DPMAC_CNT_EGR_GOOD_FRAME]		= "[mac] tx frames ok",
	[DPMAC_CNT_EGR_UCAST_FRAME]		= "[mac] tx u-cast",
	[DPMAC_CNT_EGR_MCAST_FRAME]		= "[mac] tx m-cast",
	[DPMAC_CNT_EGR_BCAST_FRAME]		= "[mac] tx b-cast",
	[DPMAC_CNT_EGR_ERR_FRAME]		= "[mac] tx frame errors",
	[DPMAC_CNT_EGR_UNDERSIZED]		= "[mac] tx undersized",
	[DPMAC_CNT_EGR_VALID_PAUSE_FRAME]	= "[mac] tx b-pause",
	[DPMAC_CNT_EGR_BYTE]			= "[mac] tx bytes",
};

#define DPAA2_MAC_NUM_STATS	ARRAY_SIZE(dpaa2_mac_ethtool_stats)

int dpaa2_mac_get_sset_count(void)
{
	return DPAA2_MAC_NUM_STATS;
}

void dpaa2_mac_get_strings(u8 *data)
{
	u8 *p = data;
	int i;

	for (i = 0; i < DPAA2_MAC_NUM_STATS; i++) {
		strlcpy(p, dpaa2_mac_ethtool_stats[i], ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}
}

void dpaa2_mac_get_ethtool_stats(struct dpaa2_mac *mac, u64 *data)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;
	int i, err;
	u64 value;

	for (i = 0; i < DPAA2_MAC_NUM_STATS; i++) {
		err = dpmac_get_counter(mac->mc_io, 0, dpmac_dev->mc_handle,
					i, &value);
		if (err) {
			netdev_err_once(mac->net_dev,
					"dpmac_get_counter error %d\n", err);
			*(data + i) = U64_MAX;
			continue;
		}
		*(data + i) = value;
	}
}
