// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 NovaTech LLC
 * George McCollister <george.mccollister@gmail.com>
 */

#include <net/dsa.h>
#include <linux/if_bridge.h>
#include <linux/of_device.h>
#include <linux/netdev_features.h>
#include <linux/if_hsr.h>
#include "xrs700x.h"
#include "xrs700x_reg.h"

#define XRS700X_MIB_INTERVAL msecs_to_jiffies(3000)

#define XRS7000X_SUPPORTED_HSR_FEATURES \
	(NETIF_F_HW_HSR_TAG_INS | NETIF_F_HW_HSR_TAG_RM | \
	 NETIF_F_HW_HSR_FWD | NETIF_F_HW_HSR_DUP)

#define XRS7003E_ID	0x100
#define XRS7003F_ID	0x101
#define XRS7004E_ID	0x200
#define XRS7004F_ID	0x201

const struct xrs700x_info xrs7003e_info = {XRS7003E_ID, "XRS7003E", 3};
EXPORT_SYMBOL(xrs7003e_info);

const struct xrs700x_info xrs7003f_info = {XRS7003F_ID, "XRS7003F", 3};
EXPORT_SYMBOL(xrs7003f_info);

const struct xrs700x_info xrs7004e_info = {XRS7004E_ID, "XRS7004E", 4};
EXPORT_SYMBOL(xrs7004e_info);

const struct xrs700x_info xrs7004f_info = {XRS7004F_ID, "XRS7004F", 4};
EXPORT_SYMBOL(xrs7004f_info);

struct xrs700x_regfield {
	struct reg_field rf;
	struct regmap_field **rmf;
};

struct xrs700x_mib {
	unsigned int offset;
	const char *name;
	int stats64_offset;
};

#define XRS700X_MIB_ETHTOOL_ONLY(o, n) {o, n, -1}
#define XRS700X_MIB(o, n, m) {o, n, offsetof(struct rtnl_link_stats64, m)}

static const struct xrs700x_mib xrs700x_mibs[] = {
	XRS700X_MIB(XRS_RX_GOOD_OCTETS_L, "rx_good_octets", rx_bytes),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_BAD_OCTETS_L, "rx_bad_octets"),
	XRS700X_MIB(XRS_RX_UNICAST_L, "rx_unicast", rx_packets),
	XRS700X_MIB(XRS_RX_BROADCAST_L, "rx_broadcast", rx_packets),
	XRS700X_MIB(XRS_RX_MULTICAST_L, "rx_multicast", multicast),
	XRS700X_MIB(XRS_RX_UNDERSIZE_L, "rx_undersize", rx_length_errors),
	XRS700X_MIB(XRS_RX_FRAGMENTS_L, "rx_fragments", rx_length_errors),
	XRS700X_MIB(XRS_RX_OVERSIZE_L, "rx_oversize", rx_length_errors),
	XRS700X_MIB(XRS_RX_JABBER_L, "rx_jabber", rx_length_errors),
	XRS700X_MIB(XRS_RX_ERR_L, "rx_err", rx_errors),
	XRS700X_MIB(XRS_RX_CRC_L, "rx_crc", rx_crc_errors),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_64_L, "rx_64"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_65_127_L, "rx_65_127"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_128_255_L, "rx_128_255"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_256_511_L, "rx_256_511"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_512_1023_L, "rx_512_1023"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_1024_1536_L, "rx_1024_1536"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_HSR_PRP_L, "rx_hsr_prp"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_WRONGLAN_L, "rx_wronglan"),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_RX_DUPLICATE_L, "rx_duplicate"),
	XRS700X_MIB(XRS_TX_OCTETS_L, "tx_octets", tx_bytes),
	XRS700X_MIB(XRS_TX_UNICAST_L, "tx_unicast", tx_packets),
	XRS700X_MIB(XRS_TX_BROADCAST_L, "tx_broadcast", tx_packets),
	XRS700X_MIB(XRS_TX_MULTICAST_L, "tx_multicast", tx_packets),
	XRS700X_MIB_ETHTOOL_ONLY(XRS_TX_HSR_PRP_L, "tx_hsr_prp"),
	XRS700X_MIB(XRS_PRIQ_DROP_L, "priq_drop", tx_dropped),
	XRS700X_MIB(XRS_EARLY_DROP_L, "early_drop", tx_dropped),
};

static const u8 eth_hsrsup_addr[ETH_ALEN] = {
	0x01, 0x15, 0x4e, 0x00, 0x01, 0x00};

static void xrs700x_get_strings(struct dsa_switch *ds, int port,
				u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(xrs700x_mibs); i++) {
		strscpy(data, xrs700x_mibs[i].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}
}

static int xrs700x_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return ARRAY_SIZE(xrs700x_mibs);
}

static void xrs700x_read_port_counters(struct xrs700x *priv, int port)
{
	struct xrs700x_port *p = &priv->ports[port];
	struct rtnl_link_stats64 stats;
	int i;

	memset(&stats, 0, sizeof(stats));

	mutex_lock(&p->mib_mutex);

	/* Capture counter values */
	regmap_write(priv->regmap, XRS_CNT_CTRL(port), 1);

	for (i = 0; i < ARRAY_SIZE(xrs700x_mibs); i++) {
		unsigned int high = 0, low = 0, reg;

		reg = xrs700x_mibs[i].offset + XRS_PORT_OFFSET * port;
		regmap_read(priv->regmap, reg, &low);
		regmap_read(priv->regmap, reg + 2, &high);

		p->mib_data[i] += (high << 16) | low;

		if (xrs700x_mibs[i].stats64_offset >= 0) {
			u8 *s = (u8 *)&stats + xrs700x_mibs[i].stats64_offset;
			*(u64 *)s += p->mib_data[i];
		}
	}

	/* multicast must be added to rx_packets (which already includes
	 * unicast and broadcast)
	 */
	stats.rx_packets += stats.multicast;

	u64_stats_update_begin(&p->syncp);
	p->stats64 = stats;
	u64_stats_update_end(&p->syncp);

	mutex_unlock(&p->mib_mutex);
}

static void xrs700x_mib_work(struct work_struct *work)
{
	struct xrs700x *priv = container_of(work, struct xrs700x,
					    mib_work.work);
	int i;

	for (i = 0; i < priv->ds->num_ports; i++)
		xrs700x_read_port_counters(priv, i);

	schedule_delayed_work(&priv->mib_work, XRS700X_MIB_INTERVAL);
}

static void xrs700x_get_ethtool_stats(struct dsa_switch *ds, int port,
				      u64 *data)
{
	struct xrs700x *priv = ds->priv;
	struct xrs700x_port *p = &priv->ports[port];

	xrs700x_read_port_counters(priv, port);

	mutex_lock(&p->mib_mutex);
	memcpy(data, p->mib_data, sizeof(*data) * ARRAY_SIZE(xrs700x_mibs));
	mutex_unlock(&p->mib_mutex);
}

static void xrs700x_get_stats64(struct dsa_switch *ds, int port,
				struct rtnl_link_stats64 *s)
{
	struct xrs700x *priv = ds->priv;
	struct xrs700x_port *p = &priv->ports[port];
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&p->syncp);
		*s = p->stats64;
	} while (u64_stats_fetch_retry(&p->syncp, start));
}

static int xrs700x_setup_regmap_range(struct xrs700x *priv)
{
	struct xrs700x_regfield regfields[] = {
		{
			.rf = REG_FIELD_ID(XRS_PORT_STATE(0), 0, 1,
					   priv->ds->num_ports,
					   XRS_PORT_OFFSET),
			.rmf = &priv->ps_forward
		},
		{
			.rf = REG_FIELD_ID(XRS_PORT_STATE(0), 2, 3,
					   priv->ds->num_ports,
					   XRS_PORT_OFFSET),
			.rmf = &priv->ps_management
		},
		{
			.rf = REG_FIELD_ID(XRS_PORT_STATE(0), 4, 9,
					   priv->ds->num_ports,
					   XRS_PORT_OFFSET),
			.rmf = &priv->ps_sel_speed
		},
		{
			.rf = REG_FIELD_ID(XRS_PORT_STATE(0), 10, 11,
					   priv->ds->num_ports,
					   XRS_PORT_OFFSET),
			.rmf = &priv->ps_cur_speed
		}
	};
	int i = 0;

	for (; i < ARRAY_SIZE(regfields); i++) {
		*regfields[i].rmf = devm_regmap_field_alloc(priv->dev,
							    priv->regmap,
							    regfields[i].rf);
		if (IS_ERR(*regfields[i].rmf))
			return PTR_ERR(*regfields[i].rmf);
	}

	return 0;
}

static enum dsa_tag_protocol xrs700x_get_tag_protocol(struct dsa_switch *ds,
						      int port,
						      enum dsa_tag_protocol m)
{
	return DSA_TAG_PROTO_XRS700X;
}

static int xrs700x_reset(struct dsa_switch *ds)
{
	struct xrs700x *priv = ds->priv;
	unsigned int val;
	int ret;

	ret = regmap_write(priv->regmap, XRS_GENERAL, XRS_GENERAL_RESET);
	if (ret)
		goto error;

	ret = regmap_read_poll_timeout(priv->regmap, XRS_GENERAL,
				       val, !(val & XRS_GENERAL_RESET),
				       10, 1000);
error:
	if (ret) {
		dev_err_ratelimited(priv->dev, "error resetting switch: %d\n",
				    ret);
	}

	return ret;
}

static void xrs700x_port_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	struct xrs700x *priv = ds->priv;
	unsigned int bpdus = 1;
	unsigned int val;

	switch (state) {
	case BR_STATE_DISABLED:
		bpdus = 0;
		fallthrough;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		val = XRS_PORT_DISABLED;
		break;
	case BR_STATE_LEARNING:
		val = XRS_PORT_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		val = XRS_PORT_FORWARDING;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	regmap_fields_write(priv->ps_forward, port, val);

	/* Enable/disable inbound policy added by xrs700x_port_add_bpdu_ipf()
	 * which allows BPDU forwarding to the CPU port when the front facing
	 * port is in disabled/learning state.
	 */
	regmap_update_bits(priv->regmap, XRS_ETH_ADDR_CFG(port, 0), 1, bpdus);

	dev_dbg_ratelimited(priv->dev, "%s - port: %d, state: %u, val: 0x%x\n",
			    __func__, port, state, val);
}

/* Add an inbound policy filter which matches the BPDU destination MAC
 * and forwards to the CPU port. Leave the policy disabled, it will be
 * enabled as needed.
 */
static int xrs700x_port_add_bpdu_ipf(struct dsa_switch *ds, int port)
{
	struct xrs700x *priv = ds->priv;
	unsigned int val = 0;
	int i = 0;
	int ret;

	/* Compare all 48 bits of the destination MAC address. */
	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_CFG(port, 0), 48 << 2);
	if (ret)
		return ret;

	/* match BPDU destination 01:80:c2:00:00:00 */
	for (i = 0; i < sizeof(eth_stp_addr); i += 2) {
		ret = regmap_write(priv->regmap, XRS_ETH_ADDR_0(port, 0) + i,
				   eth_stp_addr[i] |
				   (eth_stp_addr[i + 1] << 8));
		if (ret)
			return ret;
	}

	/* Mirror BPDU to CPU port */
	for (i = 0; i < ds->num_ports; i++) {
		if (dsa_is_cpu_port(ds, i))
			val |= BIT(i);
	}

	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_FWD_MIRROR(port, 0), val);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_FWD_ALLOW(port, 0), 0);
	if (ret)
		return ret;

	return 0;
}

/* Add an inbound policy filter which matches the HSR/PRP supervision MAC
 * range and forwards to the CPU port without discarding duplicates.
 * This is required to correctly populate the HSR/PRP node_table.
 * Leave the policy disabled, it will be enabled as needed.
 */
static int xrs700x_port_add_hsrsup_ipf(struct dsa_switch *ds, int port,
				       int fwdport)
{
	struct xrs700x *priv = ds->priv;
	unsigned int val = 0;
	int i = 0;
	int ret;

	/* Compare 40 bits of the destination MAC address. */
	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_CFG(port, 1), 40 << 2);
	if (ret)
		return ret;

	/* match HSR/PRP supervision destination 01:15:4e:00:01:XX */
	for (i = 0; i < sizeof(eth_hsrsup_addr); i += 2) {
		ret = regmap_write(priv->regmap, XRS_ETH_ADDR_0(port, 1) + i,
				   eth_hsrsup_addr[i] |
				   (eth_hsrsup_addr[i + 1] << 8));
		if (ret)
			return ret;
	}

	/* Mirror HSR/PRP supervision to CPU port */
	for (i = 0; i < ds->num_ports; i++) {
		if (dsa_is_cpu_port(ds, i))
			val |= BIT(i);
	}

	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_FWD_MIRROR(port, 1), val);
	if (ret)
		return ret;

	if (fwdport >= 0)
		val |= BIT(fwdport);

	/* Allow must be set prevent duplicate discard */
	ret = regmap_write(priv->regmap, XRS_ETH_ADDR_FWD_ALLOW(port, 1), val);
	if (ret)
		return ret;

	return 0;
}

static int xrs700x_port_setup(struct dsa_switch *ds, int port)
{
	bool cpu_port = dsa_is_cpu_port(ds, port);
	struct xrs700x *priv = ds->priv;
	unsigned int val = 0;
	int ret, i;

	xrs700x_port_stp_state_set(ds, port, BR_STATE_DISABLED);

	/* Disable forwarding to non-CPU ports */
	for (i = 0; i < ds->num_ports; i++) {
		if (!dsa_is_cpu_port(ds, i))
			val |= BIT(i);
	}

	/* 1 = Disable forwarding to the port */
	ret = regmap_write(priv->regmap, XRS_PORT_FWD_MASK(port), val);
	if (ret)
		return ret;

	val = cpu_port ? XRS_PORT_MODE_MANAGEMENT : XRS_PORT_MODE_NORMAL;
	ret = regmap_fields_write(priv->ps_management, port, val);
	if (ret)
		return ret;

	if (!cpu_port) {
		ret = xrs700x_port_add_bpdu_ipf(ds, port);
		if (ret)
			return ret;
	}

	return 0;
}

static int xrs700x_setup(struct dsa_switch *ds)
{
	struct xrs700x *priv = ds->priv;
	int ret, i;

	ret = xrs700x_reset(ds);
	if (ret)
		return ret;

	for (i = 0; i < ds->num_ports; i++) {
		ret = xrs700x_port_setup(ds, i);
		if (ret)
			return ret;
	}

	schedule_delayed_work(&priv->mib_work, XRS700X_MIB_INTERVAL);

	return 0;
}

static void xrs700x_teardown(struct dsa_switch *ds)
{
	struct xrs700x *priv = ds->priv;

	cancel_delayed_work_sync(&priv->mib_work);
}

static void xrs700x_phylink_validate(struct dsa_switch *ds, int port,
				     unsigned long *supported,
				     struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (port) {
	case 0:
		break;
	case 1:
	case 2:
	case 3:
		phylink_set(mask, 1000baseT_Full);
		break;
	default:
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		dev_err(ds->dev, "Unsupported port: %i\n", port);
		return;
	}

	phylink_set_port_modes(mask);

	/* The switch only supports full duplex. */
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Full);

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void xrs700x_mac_link_up(struct dsa_switch *ds, int port,
				unsigned int mode, phy_interface_t interface,
				struct phy_device *phydev,
				int speed, int duplex,
				bool tx_pause, bool rx_pause)
{
	struct xrs700x *priv = ds->priv;
	unsigned int val;

	switch (speed) {
	case SPEED_1000:
		val = XRS_PORT_SPEED_1000;
		break;
	case SPEED_100:
		val = XRS_PORT_SPEED_100;
		break;
	case SPEED_10:
		val = XRS_PORT_SPEED_10;
		break;
	default:
		return;
	}

	regmap_fields_write(priv->ps_sel_speed, port, val);

	dev_dbg_ratelimited(priv->dev, "%s: port: %d mode: %u speed: %u\n",
			    __func__, port, mode, speed);
}

static int xrs700x_bridge_common(struct dsa_switch *ds, int port,
				 struct net_device *bridge, bool join)
{
	unsigned int i, cpu_mask = 0, mask = 0;
	struct xrs700x *priv = ds->priv;
	int ret;

	for (i = 0; i < ds->num_ports; i++) {
		if (dsa_is_cpu_port(ds, i))
			continue;

		cpu_mask |= BIT(i);

		if (dsa_to_port(ds, i)->bridge_dev == bridge)
			continue;

		mask |= BIT(i);
	}

	for (i = 0; i < ds->num_ports; i++) {
		if (dsa_to_port(ds, i)->bridge_dev != bridge)
			continue;

		/* 1 = Disable forwarding to the port */
		ret = regmap_write(priv->regmap, XRS_PORT_FWD_MASK(i), mask);
		if (ret)
			return ret;
	}

	if (!join) {
		ret = regmap_write(priv->regmap, XRS_PORT_FWD_MASK(port),
				   cpu_mask);
		if (ret)
			return ret;
	}

	return 0;
}

static int xrs700x_bridge_join(struct dsa_switch *ds, int port,
			       struct net_device *bridge)
{
	return xrs700x_bridge_common(ds, port, bridge, true);
}

static void xrs700x_bridge_leave(struct dsa_switch *ds, int port,
				 struct net_device *bridge)
{
	xrs700x_bridge_common(ds, port, bridge, false);
}

static int xrs700x_hsr_join(struct dsa_switch *ds, int port,
			    struct net_device *hsr)
{
	unsigned int val = XRS_HSR_CFG_HSR_PRP;
	struct dsa_port *partner = NULL, *dp;
	struct xrs700x *priv = ds->priv;
	struct net_device *slave;
	int ret, i, hsr_pair[2];
	enum hsr_version ver;
	bool fwd = false;

	ret = hsr_get_version(hsr, &ver);
	if (ret)
		return ret;

	/* Only ports 1 and 2 can be HSR/PRP redundant ports. */
	if (port != 1 && port != 2)
		return -EOPNOTSUPP;

	if (ver == HSR_V1)
		val |= XRS_HSR_CFG_HSR;
	else if (ver == PRP_V1)
		val |= XRS_HSR_CFG_PRP;
	else
		return -EOPNOTSUPP;

	dsa_hsr_foreach_port(dp, ds, hsr) {
		if (dp->index != port) {
			partner = dp;
			break;
		}
	}

	/* We can't enable redundancy on the switch until both
	 * redundant ports have signed up.
	 */
	if (!partner)
		return 0;

	regmap_fields_write(priv->ps_forward, partner->index,
			    XRS_PORT_DISABLED);
	regmap_fields_write(priv->ps_forward, port, XRS_PORT_DISABLED);

	regmap_write(priv->regmap, XRS_HSR_CFG(partner->index),
		     val | XRS_HSR_CFG_LANID_A);
	regmap_write(priv->regmap, XRS_HSR_CFG(port),
		     val | XRS_HSR_CFG_LANID_B);

	/* Clear bits for both redundant ports (HSR only) and the CPU port to
	 * enable forwarding.
	 */
	val = GENMASK(ds->num_ports - 1, 0);
	if (ver == HSR_V1) {
		val &= ~BIT(partner->index);
		val &= ~BIT(port);
		fwd = true;
	}
	val &= ~BIT(dsa_upstream_port(ds, port));
	regmap_write(priv->regmap, XRS_PORT_FWD_MASK(partner->index), val);
	regmap_write(priv->regmap, XRS_PORT_FWD_MASK(port), val);

	regmap_fields_write(priv->ps_forward, partner->index,
			    XRS_PORT_FORWARDING);
	regmap_fields_write(priv->ps_forward, port, XRS_PORT_FORWARDING);

	/* Enable inbound policy which allows HSR/PRP supervision forwarding
	 * to the CPU port without discarding duplicates. Continue to
	 * forward to redundant ports when in HSR mode while discarding
	 * duplicates.
	 */
	ret = xrs700x_port_add_hsrsup_ipf(ds, partner->index, fwd ? port : -1);
	if (ret)
		return ret;

	ret = xrs700x_port_add_hsrsup_ipf(ds, port, fwd ? partner->index : -1);
	if (ret)
		return ret;

	regmap_update_bits(priv->regmap,
			   XRS_ETH_ADDR_CFG(partner->index, 1), 1, 1);
	regmap_update_bits(priv->regmap, XRS_ETH_ADDR_CFG(port, 1), 1, 1);

	hsr_pair[0] = port;
	hsr_pair[1] = partner->index;
	for (i = 0; i < ARRAY_SIZE(hsr_pair); i++) {
		slave = dsa_to_port(ds, hsr_pair[i])->slave;
		slave->features |= XRS7000X_SUPPORTED_HSR_FEATURES;
	}

	return 0;
}

static int xrs700x_hsr_leave(struct dsa_switch *ds, int port,
			     struct net_device *hsr)
{
	struct dsa_port *partner = NULL, *dp;
	struct xrs700x *priv = ds->priv;
	struct net_device *slave;
	int i, hsr_pair[2];
	unsigned int val;

	dsa_hsr_foreach_port(dp, ds, hsr) {
		if (dp->index != port) {
			partner = dp;
			break;
		}
	}

	if (!partner)
		return 0;

	regmap_fields_write(priv->ps_forward, partner->index,
			    XRS_PORT_DISABLED);
	regmap_fields_write(priv->ps_forward, port, XRS_PORT_DISABLED);

	regmap_write(priv->regmap, XRS_HSR_CFG(partner->index), 0);
	regmap_write(priv->regmap, XRS_HSR_CFG(port), 0);

	/* Clear bit for the CPU port to enable forwarding. */
	val = GENMASK(ds->num_ports - 1, 0);
	val &= ~BIT(dsa_upstream_port(ds, port));
	regmap_write(priv->regmap, XRS_PORT_FWD_MASK(partner->index), val);
	regmap_write(priv->regmap, XRS_PORT_FWD_MASK(port), val);

	regmap_fields_write(priv->ps_forward, partner->index,
			    XRS_PORT_FORWARDING);
	regmap_fields_write(priv->ps_forward, port, XRS_PORT_FORWARDING);

	/* Disable inbound policy added by xrs700x_port_add_hsrsup_ipf()
	 * which allows HSR/PRP supervision forwarding to the CPU port without
	 * discarding duplicates.
	 */
	regmap_update_bits(priv->regmap,
			   XRS_ETH_ADDR_CFG(partner->index, 1), 1, 0);
	regmap_update_bits(priv->regmap, XRS_ETH_ADDR_CFG(port, 1), 1, 0);

	hsr_pair[0] = port;
	hsr_pair[1] = partner->index;
	for (i = 0; i < ARRAY_SIZE(hsr_pair); i++) {
		slave = dsa_to_port(ds, hsr_pair[i])->slave;
		slave->features &= ~XRS7000X_SUPPORTED_HSR_FEATURES;
	}

	return 0;
}

static const struct dsa_switch_ops xrs700x_ops = {
	.get_tag_protocol	= xrs700x_get_tag_protocol,
	.setup			= xrs700x_setup,
	.teardown		= xrs700x_teardown,
	.port_stp_state_set	= xrs700x_port_stp_state_set,
	.phylink_validate	= xrs700x_phylink_validate,
	.phylink_mac_link_up	= xrs700x_mac_link_up,
	.get_strings		= xrs700x_get_strings,
	.get_sset_count		= xrs700x_get_sset_count,
	.get_ethtool_stats	= xrs700x_get_ethtool_stats,
	.get_stats64		= xrs700x_get_stats64,
	.port_bridge_join	= xrs700x_bridge_join,
	.port_bridge_leave	= xrs700x_bridge_leave,
	.port_hsr_join		= xrs700x_hsr_join,
	.port_hsr_leave		= xrs700x_hsr_leave,
};

static int xrs700x_detect(struct xrs700x *priv)
{
	const struct xrs700x_info *info;
	unsigned int id;
	int ret;

	ret = regmap_read(priv->regmap, XRS_DEV_ID0, &id);
	if (ret) {
		dev_err(priv->dev, "error %d while reading switch id.\n",
			ret);
		return ret;
	}

	info = of_device_get_match_data(priv->dev);
	if (!info)
		return -EINVAL;

	if (info->id == id) {
		priv->ds->num_ports = info->num_ports;
		dev_info(priv->dev, "%s detected.\n", info->name);
		return 0;
	}

	dev_err(priv->dev, "expected switch id 0x%x but found 0x%x.\n",
		info->id, id);

	return -ENODEV;
}

struct xrs700x *xrs700x_switch_alloc(struct device *base, void *devpriv)
{
	struct dsa_switch *ds;
	struct xrs700x *priv;

	ds = devm_kzalloc(base, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = base;

	priv = devm_kzalloc(base, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	INIT_DELAYED_WORK(&priv->mib_work, xrs700x_mib_work);

	ds->ops = &xrs700x_ops;
	ds->priv = priv;
	priv->dev = base;

	priv->ds = ds;
	priv->priv = devpriv;

	return priv;
}
EXPORT_SYMBOL(xrs700x_switch_alloc);

static int xrs700x_alloc_port_mib(struct xrs700x *priv, int port)
{
	struct xrs700x_port *p = &priv->ports[port];

	p->mib_data = devm_kcalloc(priv->dev, ARRAY_SIZE(xrs700x_mibs),
				   sizeof(*p->mib_data), GFP_KERNEL);
	if (!p->mib_data)
		return -ENOMEM;

	mutex_init(&p->mib_mutex);
	u64_stats_init(&p->syncp);

	return 0;
}

int xrs700x_switch_register(struct xrs700x *priv)
{
	int ret;
	int i;

	ret = xrs700x_detect(priv);
	if (ret)
		return ret;

	ret = xrs700x_setup_regmap_range(priv);
	if (ret)
		return ret;

	priv->ports = devm_kcalloc(priv->dev, priv->ds->num_ports,
				   sizeof(*priv->ports), GFP_KERNEL);
	if (!priv->ports)
		return -ENOMEM;

	for (i = 0; i < priv->ds->num_ports; i++) {
		ret = xrs700x_alloc_port_mib(priv, i);
		if (ret)
			return ret;
	}

	return dsa_register_switch(priv->ds);
}
EXPORT_SYMBOL(xrs700x_switch_register);

void xrs700x_switch_remove(struct xrs700x *priv)
{
	dsa_unregister_switch(priv->ds);
}
EXPORT_SYMBOL(xrs700x_switch_remove);

void xrs700x_switch_shutdown(struct xrs700x *priv)
{
	dsa_switch_shutdown(priv->ds);
}
EXPORT_SYMBOL(xrs700x_switch_shutdown);

MODULE_AUTHOR("George McCollister <george.mccollister@gmail.com>");
MODULE_DESCRIPTION("Arrow SpeedChips XRS700x DSA driver");
MODULE_LICENSE("GPL v2");
