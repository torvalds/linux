// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include "dpaa2-eth.h"
#include "dpaa2-mac.h"

#define phylink_to_dpaa2_mac(config) \
	container_of((config), struct dpaa2_mac, phylink_config)

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

/* Caller must call of_node_put on the returned value */
static struct device_node *dpaa2_mac_get_node(u16 dpmac_id)
{
	struct device_node *dpmacs, *dpmac = NULL;
	u32 id;
	int err;

	dpmacs = of_find_node_by_name(NULL, "dpmacs");
	if (!dpmacs)
		return NULL;

	while ((dpmac = of_get_next_child(dpmacs, dpmac)) != NULL) {
		err = of_property_read_u32(dpmac, "reg", &id);
		if (err)
			continue;
		if (id == dpmac_id)
			break;
	}

	of_node_put(dpmacs);

	return dpmac;
}

static int dpaa2_mac_get_if_mode(struct device_node *node,
				 struct dpmac_attr attr)
{
	phy_interface_t if_mode;
	int err;

	err = of_get_phy_mode(node, &if_mode);
	if (!err)
		return if_mode;

	err = phy_mode(attr.eth_if, &if_mode);
	if (!err)
		return if_mode;

	return err;
}

static bool dpaa2_mac_phy_mode_mismatch(struct dpaa2_mac *mac,
					phy_interface_t interface)
{
	switch (interface) {
	/* We can switch between SGMII and 1000BASE-X at runtime with
	 * pcs-lynx
	 */
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		if (mac->pcs &&
		    (mac->if_mode == PHY_INTERFACE_MODE_SGMII ||
		     mac->if_mode == PHY_INTERFACE_MODE_1000BASEX))
			return false;
		return interface != mac->if_mode;

	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return (interface != mac->if_mode);
	default:
		return true;
	}
}

static void dpaa2_mac_validate(struct phylink_config *config,
			       unsigned long *supported,
			       struct phylink_link_state *state)
{
	struct dpaa2_mac *mac = phylink_to_dpaa2_mac(config);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    dpaa2_mac_phy_mode_mismatch(mac, state->interface)) {
		goto empty_set;
	}

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
		phylink_set(mask, 10000baseT_Full);
		if (state->interface == PHY_INTERFACE_MODE_10GBASER)
			break;
		phylink_set(mask, 5000baseT_Full);
		phylink_set(mask, 2500baseT_Full);
		fallthrough;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		phylink_set(mask, 1000baseX_Full);
		phylink_set(mask, 1000baseT_Full);
		if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
			break;
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 10baseT_Full);
		break;
	default:
		goto empty_set;
	}

	linkmode_and(supported, supported, mask);
	linkmode_and(state->advertising, state->advertising, mask);

	return;

empty_set:
	linkmode_zero(supported);
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
	.validate = dpaa2_mac_validate,
	.mac_config = dpaa2_mac_config,
	.mac_link_up = dpaa2_mac_link_up,
	.mac_link_down = dpaa2_mac_link_down,
};

static int dpaa2_pcs_create(struct dpaa2_mac *mac,
			    struct device_node *dpmac_node, int id)
{
	struct mdio_device *mdiodev;
	struct device_node *node;

	node = of_parse_phandle(dpmac_node, "pcs-handle", 0);
	if (!node) {
		/* do not error out on old DTS files */
		netdev_warn(mac->net_dev, "pcs-handle node not found\n");
		return 0;
	}

	if (!of_device_is_available(node)) {
		netdev_err(mac->net_dev, "pcs-handle node not available\n");
		of_node_put(node);
		return -ENODEV;
	}

	mdiodev = of_mdio_find_device(node);
	of_node_put(node);
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
	struct lynx_pcs *pcs = mac->pcs;

	if (pcs) {
		struct device *dev = &pcs->mdio->dev;
		lynx_pcs_destroy(pcs);
		put_device(dev);
		mac->pcs = NULL;
	}
}

int dpaa2_mac_connect(struct dpaa2_mac *mac)
{
	struct net_device *net_dev = mac->net_dev;
	struct device_node *dpmac_node;
	struct phylink *phylink;
	int err;

	mac->if_link_type = mac->attr.link_type;

	dpmac_node = dpaa2_mac_get_node(mac->attr.id);
	if (!dpmac_node) {
		netdev_err(net_dev, "No dpmac@%d node found.\n", mac->attr.id);
		return -ENODEV;
	}

	err = dpaa2_mac_get_if_mode(dpmac_node, mac->attr);
	if (err < 0) {
		err = -EINVAL;
		goto err_put_node;
	}
	mac->if_mode = err;

	/* The MAC does not have the capability to add RGMII delays so
	 * error out if the interface mode requests them and there is no PHY
	 * to act upon them
	 */
	if (of_phy_is_fixed_link(dpmac_node) &&
	    (mac->if_mode == PHY_INTERFACE_MODE_RGMII_ID ||
	     mac->if_mode == PHY_INTERFACE_MODE_RGMII_RXID ||
	     mac->if_mode == PHY_INTERFACE_MODE_RGMII_TXID)) {
		netdev_err(net_dev, "RGMII delay not supported\n");
		err = -EINVAL;
		goto err_put_node;
	}

	if ((mac->attr.link_type == DPMAC_LINK_TYPE_PHY &&
	     mac->attr.eth_if != DPMAC_ETH_IF_RGMII) ||
	    mac->attr.link_type == DPMAC_LINK_TYPE_BACKPLANE) {
		err = dpaa2_pcs_create(mac, dpmac_node, mac->attr.id);
		if (err)
			goto err_put_node;
	}

	mac->phylink_config.dev = &net_dev->dev;
	mac->phylink_config.type = PHYLINK_NETDEV;

	phylink = phylink_create(&mac->phylink_config,
				 of_fwnode_handle(dpmac_node), mac->if_mode,
				 &dpaa2_mac_phylink_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		goto err_pcs_destroy;
	}
	mac->phylink = phylink;

	if (mac->pcs)
		phylink_set_pcs(mac->phylink, &mac->pcs->pcs);

	err = phylink_of_phy_connect(mac->phylink, dpmac_node, 0);
	if (err) {
		netdev_err(net_dev, "phylink_of_phy_connect() = %d\n", err);
		goto err_phylink_destroy;
	}

	of_node_put(dpmac_node);

	return 0;

err_phylink_destroy:
	phylink_destroy(mac->phylink);
err_pcs_destroy:
	dpaa2_pcs_destroy(mac);
err_put_node:
	of_node_put(dpmac_node);

	return err;
}

void dpaa2_mac_disconnect(struct dpaa2_mac *mac)
{
	if (!mac->phylink)
		return;

	phylink_disconnect_phy(mac->phylink);
	phylink_destroy(mac->phylink);
	dpaa2_pcs_destroy(mac);
}

int dpaa2_mac_open(struct dpaa2_mac *mac)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;
	struct net_device *net_dev = mac->net_dev;
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

	return 0;

err_close_dpmac:
	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
	return err;
}

void dpaa2_mac_close(struct dpaa2_mac *mac)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;

	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
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
