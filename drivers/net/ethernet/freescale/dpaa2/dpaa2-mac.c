// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019, 2024-2026 NXP */

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

#define DPMAC_STATS_BUNDLE_VER_MAJOR		4
#define DPMAC_STATS_BUNDLE_VER_MINOR		10

#define DPMAC_STANDARD_STATS_VER_MAJOR		4
#define DPMAC_STANDARD_STATS_VER_MINOR		11

#define DPAA2_MAC_FEATURE_PROTOCOL_CHANGE	BIT(0)
#define DPAA2_MAC_FEATURE_STATS_BUNDLE		BIT(1)
#define DPAA2_MAC_FEATURE_STANDARD_STATS	BIT(2)

struct dpmac_counter {
	enum dpmac_counter_id id;
	size_t offset;
	const char *name;
};

#define DPMAC_COUNTER(counter_id, struct_name, struct_offset)	\
	{							\
		.id = counter_id,				\
		.offset = offsetof(struct_name, struct_offset),	\
	}

#define DPMAC_UNSTRUCTURED_COUNTER(counter_id, counter_name)	\
	{							\
		.id = counter_id,				\
		.name = counter_name,				\
	}

#define DPMAC_RMON_COUNTER(counter_id, struct_offset)		\
	DPMAC_COUNTER(counter_id, struct ethtool_rmon_stats, struct_offset)

#define DPMAC_PAUSE_COUNTER(counter_id, struct_offset)		\
	DPMAC_COUNTER(counter_id, struct ethtool_pause_stats, struct_offset)

#define DPMAC_CTRL_COUNTER(counter_id, struct_offset)		\
	DPMAC_COUNTER(counter_id, struct ethtool_eth_ctrl_stats, struct_offset)

#define DPMAC_MAC_COUNTER(counter_id, struct_offset)		\
	DPMAC_COUNTER(counter_id, struct ethtool_eth_mac_stats, struct_offset)

static const struct dpmac_counter dpaa2_mac_ethtool_stats[] = {
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_ALL_FRAME,  "[mac] rx all frames"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_GOOD_FRAME,  "[mac] rx frames ok"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_ERR_FRAME, "[mac] rx frame errors"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_DISCARD, "[mac] rx frame discards"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_UCAST_FRAME, "[mac] rx u-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_BCAST_FRAME, "[mac] rx b-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_MCAST_FRAME, "[mac] rx m-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_64, "[mac] rx 64 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_127, "[mac] rx 65-127 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_255, "[mac] rx 128-255 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_511, "[mac] rx 256-511 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_1023, "[mac] rx 512-1023 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_1518, "[mac] rx 1024-1518 bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAME_1519_MAX, "[mac] rx 1519-max bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_FRAG, "[mac] rx frags"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_JABBER, "[mac] rx jabber"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_ALIGN_ERR, "[mac] rx align errors"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_OVERSIZED, "[mac] rx oversized"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_VALID_PAUSE_FRAME, "[mac] rx pause"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_ING_BYTE, "[mac] rx bytes"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_GOOD_FRAME, "[mac] tx frames ok"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_UCAST_FRAME, "[mac] tx u-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_MCAST_FRAME, "[mac] tx m-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_BCAST_FRAME, "[mac] tx b-cast"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_ERR_FRAME, "[mac] tx frame errors"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_UNDERSIZED, "[mac] tx undersized"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_VALID_PAUSE_FRAME, "[mac] tx b-pause"),
	DPMAC_UNSTRUCTURED_COUNTER(DPMAC_CNT_EGR_BYTE, "[mac] tx bytes"),
};

#define DPAA2_MAC_NUM_ETHTOOL_STATS	ARRAY_SIZE(dpaa2_mac_ethtool_stats)

static const struct dpmac_counter dpaa2_mac_rmon_stats[] = {
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_64, hist[0]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_127, hist[1]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_255, hist[2]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_511, hist[3]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_1023, hist[4]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_1518, hist[5]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAME_1519_MAX, hist[6]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_64, hist_tx[0]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_127, hist_tx[1]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_255, hist_tx[2]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_511, hist_tx[3]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_1023, hist_tx[4]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_1518, hist_tx[5]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_EGR_FRAME_1519_MAX, hist_tx[6]),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_UNDERSIZED, undersize_pkts),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_OVERSIZED, oversize_pkts),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_FRAG, fragments),
	DPMAC_RMON_COUNTER(DPMAC_CNT_ING_JABBER, jabbers),
};

#define DPAA2_MAC_NUM_RMON_STATS	ARRAY_SIZE(dpaa2_mac_rmon_stats)

static const struct dpmac_counter dpaa2_mac_pause_stats[] = {
	DPMAC_PAUSE_COUNTER(DPMAC_CNT_ING_VALID_PAUSE_FRAME, rx_pause_frames),
	DPMAC_PAUSE_COUNTER(DPMAC_CNT_EGR_VALID_PAUSE_FRAME, tx_pause_frames),
};

#define DPAA2_MAC_NUM_PAUSE_STATS	ARRAY_SIZE(dpaa2_mac_pause_stats)

static const struct dpmac_counter dpaa2_mac_eth_ctrl_stats[] = {
	DPMAC_CTRL_COUNTER(DPMAC_CNT_ING_CONTROL_FRAME, MACControlFramesReceived),
	DPMAC_CTRL_COUNTER(DPMAC_CNT_EGR_CONTROL_FRAME, MACControlFramesTransmitted),
};

#define DPAA2_MAC_NUM_ETH_CTRL_STATS	ARRAY_SIZE(dpaa2_mac_eth_ctrl_stats)

static const struct dpmac_counter dpaa2_mac_eth_mac_stats[] = {
	DPMAC_MAC_COUNTER(DPMAC_CNT_EGR_GOOD_FRAME, FramesTransmittedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_GOOD_FRAME, FramesReceivedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_FCS_ERR, FrameCheckSequenceErrors),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_ALIGN_ERR, AlignmentErrors),
	DPMAC_MAC_COUNTER(DPMAC_CNT_EGR_ALL_BYTE, OctetsTransmittedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_EGR_ERR_FRAME, FramesLostDueToIntMACXmitError),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_ALL_BYTE, OctetsReceivedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_FRAME_DISCARD_NOT_TRUNC, FramesLostDueToIntMACRcvError),
	DPMAC_MAC_COUNTER(DPMAC_CNT_EGR_MCAST_FRAME, MulticastFramesXmittedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_EGR_BCAST_FRAME, BroadcastFramesXmittedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_MCAST_FRAME, MulticastFramesReceivedOK),
	DPMAC_MAC_COUNTER(DPMAC_CNT_ING_BCAST_FRAME, BroadcastFramesReceivedOK),
};

#define DPAA2_MAC_NUM_ETH_MAC_STATS	ARRAY_SIZE(dpaa2_mac_eth_mac_stats)

static void dpaa2_mac_setup_stats(struct dpaa2_mac *mac,
				  struct dpaa2_mac_stats *stats,
				  size_t num_stats,
				  const struct dpmac_counter *counters)
{
	struct device *dev = mac->net_dev->dev.parent;
	size_t size_idx, size_values;
	__le32 *cnt_idx;

	size_idx = num_stats * sizeof(u32);
	stats->idx_dma_mem = dma_alloc_noncoherent(dev, size_idx,
						   &stats->idx_iova,
						   DMA_TO_DEVICE,
						   GFP_KERNEL);
	if (!stats->idx_dma_mem)
		goto out;

	size_values = num_stats * sizeof(u64);
	stats->values_dma_mem = dma_alloc_noncoherent(dev, size_values,
						      &stats->values_iova,
						      DMA_FROM_DEVICE,
						      GFP_KERNEL);
	if (!stats->values_dma_mem)
		goto err_alloc_values;

	cnt_idx = stats->idx_dma_mem;
	for (size_t i = 0; i < num_stats; i++)
		*cnt_idx++ = cpu_to_le32((u32)(counters[i].id));

	dma_sync_single_for_device(dev, stats->idx_iova, size_idx,
				   DMA_TO_DEVICE);

	return;

err_alloc_values:
	dma_free_noncoherent(dev, num_stats * sizeof(u32), stats->idx_dma_mem,
			     stats->idx_iova, DMA_TO_DEVICE);
out:
	stats->idx_dma_mem = NULL;
	stats->values_dma_mem = NULL;
}

static void dpaa2_mac_clear_stats(struct dpaa2_mac *mac,
				  struct dpaa2_mac_stats *stats,
				  size_t num_stats)
{
	struct device *dev = mac->net_dev->dev.parent;

	if (stats->idx_dma_mem) {
		dma_free_noncoherent(dev, num_stats * sizeof(u32),
				     stats->idx_dma_mem,
				     stats->idx_iova, DMA_TO_DEVICE);
		stats->idx_dma_mem = NULL;
	}

	if (stats->values_dma_mem) {
		dma_free_noncoherent(dev, num_stats * sizeof(u64),
				     stats->values_dma_mem,
				     stats->values_iova, DMA_FROM_DEVICE);
		stats->values_dma_mem = NULL;
	}
}

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

	if (dpaa2_mac_cmp_ver(mac, DPMAC_STATS_BUNDLE_VER_MAJOR,
			      DPMAC_STATS_BUNDLE_VER_MINOR) >= 0)
		mac->features |= DPAA2_MAC_FEATURE_STATS_BUNDLE;

	if (dpaa2_mac_cmp_ver(mac, DPMAC_STANDARD_STATS_VER_MAJOR,
			      DPMAC_STANDARD_STATS_VER_MINOR) >= 0)
		mac->features |= DPAA2_MAC_FEATURE_STANDARD_STATS;
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
	case DPMAC_ETH_IF_CAUI:
		*if_mode = PHY_INTERFACE_MODE_25GBASER;
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
	case PHY_INTERFACE_MODE_25GBASER:
		return DPMAC_ETH_IF_CAUI;
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
		dev_dbg(dev, "dprc not finished probing\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

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

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			      state->advertising))
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
	.mac_select_pcs = dpaa2_mac_select_pcs,
	.mac_config = dpaa2_mac_config,
	.mac_link_up = dpaa2_mac_link_up,
	.mac_link_down = dpaa2_mac_link_down,
};

static int dpaa2_pcs_create(struct dpaa2_mac *mac,
			    struct fwnode_handle *dpmac_node,
			    int id)
{
	struct fwnode_handle *node;
	struct phylink_pcs *pcs;

	node = fwnode_find_reference(dpmac_node, "pcs-handle", 0);
	if (IS_ERR(node)) {
		/* do not error out on old DTS files */
		netdev_warn(mac->net_dev, "pcs-handle node not found\n");
		return 0;
	}

	pcs = lynx_pcs_create_fwnode(node);
	fwnode_handle_put(node);

	if (pcs == ERR_PTR(-EPROBE_DEFER)) {
		netdev_dbg(mac->net_dev, "missing PCS device\n");
		return -EPROBE_DEFER;
	}

	if (pcs == ERR_PTR(-ENODEV)) {
		netdev_err(mac->net_dev, "pcs-handle node not available\n");
		return PTR_ERR(pcs);
	}

	if (IS_ERR(pcs)) {
		netdev_err(mac->net_dev,
			   "lynx_pcs_create_fwnode() failed: %pe\n", pcs);
		return PTR_ERR(pcs);
	}

	mac->pcs = pcs;

	return 0;
}

static void dpaa2_pcs_destroy(struct dpaa2_mac *mac)
{
	struct phylink_pcs *phylink_pcs = mac->pcs;

	if (phylink_pcs) {
		lynx_pcs_destroy(phylink_pcs);
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
	ASSERT_RTNL();

	if (mac->serdes_phy)
		phy_power_on(mac->serdes_phy);

	phylink_start(mac->phylink);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_start);

void dpaa2_mac_stop(struct dpaa2_mac *mac)
{
	ASSERT_RTNL();

	phylink_stop(mac->phylink);

	if (mac->serdes_phy)
		phy_power_off(mac->serdes_phy);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_stop);

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
		MAC_10000FD | MAC_25000FD;

	dpaa2_mac_set_supported_interfaces(mac);

	phylink = phylink_create(&mac->phylink_config,
				 dpmac_node, mac->if_mode,
				 &dpaa2_mac_phylink_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		goto err_pcs_destroy;
	}
	mac->phylink = phylink;

	rtnl_lock();
	err = phylink_fwnode_phy_connect(mac->phylink, dpmac_node, 0);
	rtnl_unlock();
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
EXPORT_SYMBOL_GPL(dpaa2_mac_connect);

void dpaa2_mac_disconnect(struct dpaa2_mac *mac)
{
	rtnl_lock();
	phylink_disconnect_phy(mac->phylink);
	rtnl_unlock();

	phylink_destroy(mac->phylink);
	dpaa2_pcs_destroy(mac);
	of_phy_put(mac->serdes_phy);
	mac->serdes_phy = NULL;
}
EXPORT_SYMBOL_GPL(dpaa2_mac_disconnect);

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

	if (mac->features & DPAA2_MAC_FEATURE_STATS_BUNDLE)
		dpaa2_mac_setup_stats(mac, &mac->ethtool_stats,
				      DPAA2_MAC_NUM_ETHTOOL_STATS,
				      dpaa2_mac_ethtool_stats);

	if (mac->features & DPAA2_MAC_FEATURE_STANDARD_STATS) {
		dpaa2_mac_setup_stats(mac, &mac->rmon_stats,
				      DPAA2_MAC_NUM_RMON_STATS,
				      dpaa2_mac_rmon_stats);

		dpaa2_mac_setup_stats(mac, &mac->pause_stats,
				      DPAA2_MAC_NUM_PAUSE_STATS,
				      dpaa2_mac_pause_stats);

		dpaa2_mac_setup_stats(mac, &mac->eth_ctrl_stats,
				      DPAA2_MAC_NUM_ETH_CTRL_STATS,
				      dpaa2_mac_eth_ctrl_stats);

		dpaa2_mac_setup_stats(mac, &mac->eth_mac_stats,
				      DPAA2_MAC_NUM_ETH_MAC_STATS,
				      dpaa2_mac_eth_mac_stats);
	}

	return 0;

err_close_dpmac:
	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
	return err;
}
EXPORT_SYMBOL_GPL(dpaa2_mac_open);

void dpaa2_mac_close(struct dpaa2_mac *mac)
{
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;

	if (mac->features & DPAA2_MAC_FEATURE_STATS_BUNDLE)
		dpaa2_mac_clear_stats(mac, &mac->ethtool_stats,
				      DPAA2_MAC_NUM_ETHTOOL_STATS);

	if (mac->features & DPAA2_MAC_FEATURE_STANDARD_STATS) {
		dpaa2_mac_clear_stats(mac, &mac->rmon_stats,
				      DPAA2_MAC_NUM_RMON_STATS);
		dpaa2_mac_clear_stats(mac, &mac->pause_stats,
				      DPAA2_MAC_NUM_PAUSE_STATS);
		dpaa2_mac_clear_stats(mac, &mac->eth_ctrl_stats,
				      DPAA2_MAC_NUM_ETH_CTRL_STATS);
		dpaa2_mac_clear_stats(mac, &mac->eth_mac_stats,
				      DPAA2_MAC_NUM_ETH_MAC_STATS);
	}

	dpmac_close(mac->mc_io, 0, dpmac_dev->mc_handle);
	if (mac->fw_node)
		fwnode_handle_put(mac->fw_node);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_close);

static void dpaa2_mac_transfer_stats(const struct dpmac_counter *counters,
				     size_t num_counters, void *s,
				     __le64 *cnt_values)
{
	for (size_t i = 0; i < num_counters; i++) {
		u64 *p = s + counters[i].offset;

		*p = le64_to_cpu(cnt_values[i]);
	}
}

static const struct ethtool_rmon_hist_range dpaa2_mac_rmon_ranges[] = {
	{   64,   64 },
	{   65,  127 },
	{  128,  255 },
	{  256,  511 },
	{  512, 1023 },
	{ 1024, 1518 },
	{ 1519, DPAA2_ETH_MFL },
	{},
};

static void dpaa2_mac_get_standard_stats(struct dpaa2_mac *mac,
					 struct dpaa2_mac_stats *stats,
					 size_t num_cnt,
					 const struct dpmac_counter *counters,
					 void *s)
{
	struct device *dev = mac->net_dev->dev.parent;
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;
	size_t values_size = num_cnt * sizeof(u64);
	int err;

	if (!(mac->features & DPAA2_MAC_FEATURE_STANDARD_STATS))
		return;

	if (!stats->idx_dma_mem || !stats->values_dma_mem)
		return;

	dma_sync_single_for_device(dev, stats->values_iova, values_size,
				   DMA_FROM_DEVICE);

	err = dpmac_get_statistics(mac->mc_io, 0, dpmac_dev->mc_handle,
				   stats->idx_iova, stats->values_iova,
				   num_cnt);
	if (err) {
		netdev_err(mac->net_dev, "%s: dpmac_get_statistics() = %d\n",
			   __func__, err);
		return;
	}

	dma_sync_single_for_cpu(dev, stats->values_iova, values_size,
				DMA_FROM_DEVICE);

	dpaa2_mac_transfer_stats(counters, num_cnt, s, stats->values_dma_mem);
}

void dpaa2_mac_get_rmon_stats(struct dpaa2_mac *mac,
			      struct ethtool_rmon_stats *s,
			      const struct ethtool_rmon_hist_range **ranges)
{
	if (s->src != ETHTOOL_MAC_STATS_SRC_AGGREGATE)
		return;

	dpaa2_mac_get_standard_stats(mac, &mac->rmon_stats,
				     DPAA2_MAC_NUM_RMON_STATS,
				     dpaa2_mac_rmon_stats, s);

	*ranges = dpaa2_mac_rmon_ranges;
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_rmon_stats);

void dpaa2_mac_get_pause_stats(struct dpaa2_mac *mac,
			       struct ethtool_pause_stats *s)
{
	if (s->src != ETHTOOL_MAC_STATS_SRC_AGGREGATE)
		return;

	dpaa2_mac_get_standard_stats(mac, &mac->pause_stats,
				     DPAA2_MAC_NUM_PAUSE_STATS,
				     dpaa2_mac_pause_stats, s);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_pause_stats);

void dpaa2_mac_get_ctrl_stats(struct dpaa2_mac *mac,
			      struct ethtool_eth_ctrl_stats *s)
{
	if (s->src != ETHTOOL_MAC_STATS_SRC_AGGREGATE)
		return;

	dpaa2_mac_get_standard_stats(mac, &mac->eth_ctrl_stats,
				     DPAA2_MAC_NUM_ETH_CTRL_STATS,
				     dpaa2_mac_eth_ctrl_stats, s);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_ctrl_stats);

void dpaa2_mac_get_eth_mac_stats(struct dpaa2_mac *mac,
				 struct ethtool_eth_mac_stats *s)
{
	if (s->src != ETHTOOL_MAC_STATS_SRC_AGGREGATE)
		return;

	dpaa2_mac_get_standard_stats(mac, &mac->eth_mac_stats,
				     DPAA2_MAC_NUM_ETH_MAC_STATS,
				     dpaa2_mac_eth_mac_stats, s);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_eth_mac_stats);

int dpaa2_mac_get_sset_count(void)
{
	return DPAA2_MAC_NUM_ETHTOOL_STATS;
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_sset_count);

void dpaa2_mac_get_strings(u8 **data)
{
	int i;

	for (i = 0; i < DPAA2_MAC_NUM_ETHTOOL_STATS; i++)
		ethtool_puts(data, dpaa2_mac_ethtool_stats[i].name);
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_strings);

void dpaa2_mac_get_ethtool_stats(struct dpaa2_mac *mac, u64 *data)
{
	size_t values_size = DPAA2_MAC_NUM_ETHTOOL_STATS * sizeof(u64);
	struct device *dev = mac->net_dev->dev.parent;
	struct fsl_mc_device *dpmac_dev = mac->mc_dev;
	__le64 *cnt_values;
	int i, err;
	u64 value;

	if (!(mac->features & DPAA2_MAC_FEATURE_STATS_BUNDLE))
		goto fallback;

	if (!mac->ethtool_stats.idx_dma_mem ||
	    !mac->ethtool_stats.values_dma_mem)
		goto fallback;

	dma_sync_single_for_device(dev, mac->ethtool_stats.values_iova,
				   values_size, DMA_FROM_DEVICE);

	err = dpmac_get_statistics(mac->mc_io, 0, dpmac_dev->mc_handle,
				   mac->ethtool_stats.idx_iova,
				   mac->ethtool_stats.values_iova,
				   DPAA2_MAC_NUM_ETHTOOL_STATS);
	if (err)
		goto fallback;

	dma_sync_single_for_cpu(dev, mac->ethtool_stats.values_iova,
				values_size, DMA_FROM_DEVICE);

	cnt_values = mac->ethtool_stats.values_dma_mem;
	for (i = 0; i < DPAA2_MAC_NUM_ETHTOOL_STATS; i++)
		*(data + i) = le64_to_cpu(*cnt_values++);

	return;

fallback:

	/* Fallback and retrieve each counter one by one */
	for (i = 0; i < DPAA2_MAC_NUM_ETHTOOL_STATS; i++) {
		err = dpmac_get_counter(mac->mc_io, 0, dpmac_dev->mc_handle,
					dpaa2_mac_ethtool_stats[i].id, &value);
		if (err) {
			netdev_err_once(mac->net_dev,
					"dpmac_get_counter error %d\n", err);
			*(data + i) = U64_MAX;
			continue;
		}
		*(data + i) = value;
	}
}
EXPORT_SYMBOL_GPL(dpaa2_mac_get_ethtool_stats);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DPAA2 Ethernet MAC library");
