// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Schneider-Electric
 *
 * Clément Léger <clement.leger@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <net/dsa.h>

#include "rzn1_a5psw.h"

struct a5psw_stats {
	u16 offset;
	const char name[ETH_GSTRING_LEN];
};

#define STAT_DESC(_offset) {	\
	.offset = A5PSW_##_offset,	\
	.name = __stringify(_offset),	\
}

static const struct a5psw_stats a5psw_stats[] = {
	STAT_DESC(aFramesTransmittedOK),
	STAT_DESC(aFramesReceivedOK),
	STAT_DESC(aFrameCheckSequenceErrors),
	STAT_DESC(aAlignmentErrors),
	STAT_DESC(aOctetsTransmittedOK),
	STAT_DESC(aOctetsReceivedOK),
	STAT_DESC(aTxPAUSEMACCtrlFrames),
	STAT_DESC(aRxPAUSEMACCtrlFrames),
	STAT_DESC(ifInErrors),
	STAT_DESC(ifOutErrors),
	STAT_DESC(ifInUcastPkts),
	STAT_DESC(ifInMulticastPkts),
	STAT_DESC(ifInBroadcastPkts),
	STAT_DESC(ifOutDiscards),
	STAT_DESC(ifOutUcastPkts),
	STAT_DESC(ifOutMulticastPkts),
	STAT_DESC(ifOutBroadcastPkts),
	STAT_DESC(etherStatsDropEvents),
	STAT_DESC(etherStatsOctets),
	STAT_DESC(etherStatsPkts),
	STAT_DESC(etherStatsUndersizePkts),
	STAT_DESC(etherStatsOversizePkts),
	STAT_DESC(etherStatsPkts64Octets),
	STAT_DESC(etherStatsPkts65to127Octets),
	STAT_DESC(etherStatsPkts128to255Octets),
	STAT_DESC(etherStatsPkts256to511Octets),
	STAT_DESC(etherStatsPkts1024to1518Octets),
	STAT_DESC(etherStatsPkts1519toXOctets),
	STAT_DESC(etherStatsJabbers),
	STAT_DESC(etherStatsFragments),
	STAT_DESC(VLANReceived),
	STAT_DESC(VLANTransmitted),
	STAT_DESC(aDeferred),
	STAT_DESC(aMultipleCollisions),
	STAT_DESC(aSingleCollisions),
	STAT_DESC(aLateCollisions),
	STAT_DESC(aExcessiveCollisions),
	STAT_DESC(aCarrierSenseErrors),
};

static void a5psw_reg_writel(struct a5psw *a5psw, int offset, u32 value)
{
	writel(value, a5psw->base + offset);
}

static u32 a5psw_reg_readl(struct a5psw *a5psw, int offset)
{
	return readl(a5psw->base + offset);
}

static void a5psw_reg_rmw(struct a5psw *a5psw, int offset, u32 mask, u32 val)
{
	u32 reg;

	spin_lock(&a5psw->reg_lock);

	reg = a5psw_reg_readl(a5psw, offset);
	reg &= ~mask;
	reg |= val;
	a5psw_reg_writel(a5psw, offset, reg);

	spin_unlock(&a5psw->reg_lock);
}

static enum dsa_tag_protocol a5psw_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_RZN1_A5PSW;
}

static void a5psw_port_pattern_set(struct a5psw *a5psw, int port, int pattern,
				   bool enable)
{
	u32 rx_match = 0;

	if (enable)
		rx_match |= A5PSW_RXMATCH_CONFIG_PATTERN(pattern);

	a5psw_reg_rmw(a5psw, A5PSW_RXMATCH_CONFIG(port),
		      A5PSW_RXMATCH_CONFIG_PATTERN(pattern), rx_match);
}

static void a5psw_port_mgmtfwd_set(struct a5psw *a5psw, int port, bool enable)
{
	/* Enable "management forward" pattern matching, this will forward
	 * packets from this port only towards the management port and thus
	 * isolate the port.
	 */
	a5psw_port_pattern_set(a5psw, port, A5PSW_PATTERN_MGMTFWD, enable);
}

static void a5psw_port_enable_set(struct a5psw *a5psw, int port, bool enable)
{
	u32 port_ena = 0;

	if (enable)
		port_ena |= A5PSW_PORT_ENA_TX_RX(port);

	a5psw_reg_rmw(a5psw, A5PSW_PORT_ENA, A5PSW_PORT_ENA_TX_RX(port),
		      port_ena);
}

static int a5psw_lk_execute_ctrl(struct a5psw *a5psw, u32 *ctrl)
{
	int ret;

	a5psw_reg_writel(a5psw, A5PSW_LK_ADDR_CTRL, *ctrl);

	ret = readl_poll_timeout(a5psw->base + A5PSW_LK_ADDR_CTRL, *ctrl,
				 !(*ctrl & A5PSW_LK_ADDR_CTRL_BUSY),
				 A5PSW_LK_BUSY_USEC_POLL, A5PSW_CTRL_TIMEOUT);
	if (ret)
		dev_err(a5psw->dev, "LK_CTRL timeout waiting for BUSY bit\n");

	return ret;
}

static void a5psw_port_fdb_flush(struct a5psw *a5psw, int port)
{
	u32 ctrl = A5PSW_LK_ADDR_CTRL_DELETE_PORT | BIT(port);

	mutex_lock(&a5psw->lk_lock);
	a5psw_lk_execute_ctrl(a5psw, &ctrl);
	mutex_unlock(&a5psw->lk_lock);
}

static void a5psw_port_authorize_set(struct a5psw *a5psw, int port,
				     bool authorize)
{
	u32 reg = a5psw_reg_readl(a5psw, A5PSW_AUTH_PORT(port));

	if (authorize)
		reg |= A5PSW_AUTH_PORT_AUTHORIZED;
	else
		reg &= ~A5PSW_AUTH_PORT_AUTHORIZED;

	a5psw_reg_writel(a5psw, A5PSW_AUTH_PORT(port), reg);
}

static void a5psw_port_disable(struct dsa_switch *ds, int port)
{
	struct a5psw *a5psw = ds->priv;

	a5psw_port_authorize_set(a5psw, port, false);
	a5psw_port_enable_set(a5psw, port, false);
}

static int a5psw_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct a5psw *a5psw = ds->priv;

	a5psw_port_authorize_set(a5psw, port, true);
	a5psw_port_enable_set(a5psw, port, true);

	return 0;
}

static int a5psw_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct a5psw *a5psw = ds->priv;

	new_mtu += ETH_HLEN + A5PSW_EXTRA_MTU_LEN + ETH_FCS_LEN;
	a5psw_reg_writel(a5psw, A5PSW_FRM_LENGTH(port), new_mtu);

	return 0;
}

static int a5psw_port_max_mtu(struct dsa_switch *ds, int port)
{
	return A5PSW_MAX_MTU;
}

static void a5psw_phylink_get_caps(struct dsa_switch *ds, int port,
				   struct phylink_config *config)
{
	unsigned long *intf = config->supported_interfaces;

	config->mac_capabilities = MAC_1000FD;

	if (dsa_is_cpu_port(ds, port)) {
		/* GMII is used internally and GMAC2 is connected to the switch
		 * using 1000Mbps Full-Duplex mode only (cf ethernet manual)
		 */
		__set_bit(PHY_INTERFACE_MODE_GMII, intf);
	} else {
		config->mac_capabilities |= MAC_100 | MAC_10;
		phy_interface_set_rgmii(intf);
		__set_bit(PHY_INTERFACE_MODE_RMII, intf);
		__set_bit(PHY_INTERFACE_MODE_MII, intf);
	}
}

static struct phylink_pcs *
a5psw_phylink_mac_select_pcs(struct dsa_switch *ds, int port,
			     phy_interface_t interface)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct a5psw *a5psw = ds->priv;

	if (!dsa_port_is_cpu(dp) && a5psw->pcs[port])
		return a5psw->pcs[port];

	return NULL;
}

static void a5psw_phylink_mac_link_down(struct dsa_switch *ds, int port,
					unsigned int mode,
					phy_interface_t interface)
{
	struct a5psw *a5psw = ds->priv;
	u32 cmd_cfg;

	cmd_cfg = a5psw_reg_readl(a5psw, A5PSW_CMD_CFG(port));
	cmd_cfg &= ~(A5PSW_CMD_CFG_RX_ENA | A5PSW_CMD_CFG_TX_ENA);
	a5psw_reg_writel(a5psw, A5PSW_CMD_CFG(port), cmd_cfg);
}

static void a5psw_phylink_mac_link_up(struct dsa_switch *ds, int port,
				      unsigned int mode,
				      phy_interface_t interface,
				      struct phy_device *phydev, int speed,
				      int duplex, bool tx_pause, bool rx_pause)
{
	u32 cmd_cfg = A5PSW_CMD_CFG_RX_ENA | A5PSW_CMD_CFG_TX_ENA |
		      A5PSW_CMD_CFG_TX_CRC_APPEND;
	struct a5psw *a5psw = ds->priv;

	if (speed == SPEED_1000)
		cmd_cfg |= A5PSW_CMD_CFG_ETH_SPEED;

	if (duplex == DUPLEX_HALF)
		cmd_cfg |= A5PSW_CMD_CFG_HD_ENA;

	cmd_cfg |= A5PSW_CMD_CFG_CNTL_FRM_ENA;

	if (!rx_pause)
		cmd_cfg &= ~A5PSW_CMD_CFG_PAUSE_IGNORE;

	a5psw_reg_writel(a5psw, A5PSW_CMD_CFG(port), cmd_cfg);
}

static int a5psw_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct a5psw *a5psw = ds->priv;
	unsigned long rate;
	u64 max, tmp;
	u32 agetime;

	rate = clk_get_rate(a5psw->clk);
	max = div64_ul(((u64)A5PSW_LK_AGETIME_MASK * A5PSW_TABLE_ENTRIES * 1024),
		       rate) * 1000;
	if (msecs > max)
		return -EINVAL;

	tmp = div_u64(rate, MSEC_PER_SEC);
	agetime = div_u64(msecs * tmp, 1024 * A5PSW_TABLE_ENTRIES);

	a5psw_reg_writel(a5psw, A5PSW_LK_AGETIME, agetime);

	return 0;
}

static void a5psw_flooding_set_resolution(struct a5psw *a5psw, int port,
					  bool set)
{
	u8 offsets[] = {A5PSW_UCAST_DEF_MASK, A5PSW_BCAST_DEF_MASK,
			A5PSW_MCAST_DEF_MASK};
	int i;

	if (set)
		a5psw->bridged_ports |= BIT(port);
	else
		a5psw->bridged_ports &= ~BIT(port);

	for (i = 0; i < ARRAY_SIZE(offsets); i++)
		a5psw_reg_writel(a5psw, offsets[i], a5psw->bridged_ports);
}

static int a5psw_port_bridge_join(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge,
				  bool *tx_fwd_offload,
				  struct netlink_ext_ack *extack)
{
	struct a5psw *a5psw = ds->priv;

	/* We only support 1 bridge device */
	if (a5psw->br_dev && bridge.dev != a5psw->br_dev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Forwarding offload supported for a single bridge");
		return -EOPNOTSUPP;
	}

	a5psw->br_dev = bridge.dev;
	a5psw_flooding_set_resolution(a5psw, port, true);
	a5psw_port_mgmtfwd_set(a5psw, port, false);

	return 0;
}

static void a5psw_port_bridge_leave(struct dsa_switch *ds, int port,
				    struct dsa_bridge bridge)
{
	struct a5psw *a5psw = ds->priv;

	a5psw_flooding_set_resolution(a5psw, port, false);
	a5psw_port_mgmtfwd_set(a5psw, port, true);

	/* No more ports bridged */
	if (a5psw->bridged_ports == BIT(A5PSW_CPU_PORT))
		a5psw->br_dev = NULL;
}

static void a5psw_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	u32 mask = A5PSW_INPUT_LEARN_DIS(port) | A5PSW_INPUT_LEARN_BLOCK(port);
	struct a5psw *a5psw = ds->priv;
	u32 reg = 0;

	switch (state) {
	case BR_STATE_DISABLED:
	case BR_STATE_BLOCKING:
		reg |= A5PSW_INPUT_LEARN_DIS(port);
		reg |= A5PSW_INPUT_LEARN_BLOCK(port);
		break;
	case BR_STATE_LISTENING:
		reg |= A5PSW_INPUT_LEARN_DIS(port);
		break;
	case BR_STATE_LEARNING:
		reg |= A5PSW_INPUT_LEARN_BLOCK(port);
		break;
	case BR_STATE_FORWARDING:
	default:
		break;
	}

	a5psw_reg_rmw(a5psw, A5PSW_INPUT_LEARN, mask, reg);
}

static void a5psw_port_fast_age(struct dsa_switch *ds, int port)
{
	struct a5psw *a5psw = ds->priv;

	a5psw_port_fdb_flush(a5psw, port);
}

static int a5psw_lk_execute_lookup(struct a5psw *a5psw, union lk_data *lk_data,
				   u16 *entry)
{
	u32 ctrl;
	int ret;

	a5psw_reg_writel(a5psw, A5PSW_LK_DATA_LO, lk_data->lo);
	a5psw_reg_writel(a5psw, A5PSW_LK_DATA_HI, lk_data->hi);

	ctrl = A5PSW_LK_ADDR_CTRL_LOOKUP;
	ret = a5psw_lk_execute_ctrl(a5psw, &ctrl);
	if (ret)
		return ret;

	*entry = ctrl & A5PSW_LK_ADDR_CTRL_ADDRESS;

	return 0;
}

static int a5psw_port_fdb_add(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	struct a5psw *a5psw = ds->priv;
	union lk_data lk_data = {0};
	bool inc_learncount = false;
	int ret = 0;
	u16 entry;
	u32 reg;

	ether_addr_copy(lk_data.entry.mac, addr);
	lk_data.entry.port_mask = BIT(port);

	mutex_lock(&a5psw->lk_lock);

	/* Set the value to be written in the lookup table */
	ret = a5psw_lk_execute_lookup(a5psw, &lk_data, &entry);
	if (ret)
		goto lk_unlock;

	lk_data.hi = a5psw_reg_readl(a5psw, A5PSW_LK_DATA_HI);
	if (!lk_data.entry.valid) {
		inc_learncount = true;
		/* port_mask set to 0x1f when entry is not valid, clear it */
		lk_data.entry.port_mask = 0;
		lk_data.entry.prio = 0;
	}

	lk_data.entry.port_mask |= BIT(port);
	lk_data.entry.is_static = 1;
	lk_data.entry.valid = 1;

	a5psw_reg_writel(a5psw, A5PSW_LK_DATA_HI, lk_data.hi);

	reg = A5PSW_LK_ADDR_CTRL_WRITE | entry;
	ret = a5psw_lk_execute_ctrl(a5psw, &reg);
	if (ret)
		goto lk_unlock;

	if (inc_learncount) {
		reg = A5PSW_LK_LEARNCOUNT_MODE_INC;
		a5psw_reg_writel(a5psw, A5PSW_LK_LEARNCOUNT, reg);
	}

lk_unlock:
	mutex_unlock(&a5psw->lk_lock);

	return ret;
}

static int a5psw_port_fdb_del(struct dsa_switch *ds, int port,
			      const unsigned char *addr, u16 vid,
			      struct dsa_db db)
{
	struct a5psw *a5psw = ds->priv;
	union lk_data lk_data = {0};
	bool clear = false;
	u16 entry;
	u32 reg;
	int ret;

	ether_addr_copy(lk_data.entry.mac, addr);

	mutex_lock(&a5psw->lk_lock);

	ret = a5psw_lk_execute_lookup(a5psw, &lk_data, &entry);
	if (ret)
		goto lk_unlock;

	lk_data.hi = a5psw_reg_readl(a5psw, A5PSW_LK_DATA_HI);

	/* Our hardware does not associate any VID to the FDB entries so this
	 * means that if two entries were added for the same mac but for
	 * different VID, then, on the deletion of the first one, we would also
	 * delete the second one. Since there is unfortunately nothing we can do
	 * about that, do not return an error...
	 */
	if (!lk_data.entry.valid)
		goto lk_unlock;

	lk_data.entry.port_mask &= ~BIT(port);
	/* If there is no more port in the mask, clear the entry */
	if (lk_data.entry.port_mask == 0)
		clear = true;

	a5psw_reg_writel(a5psw, A5PSW_LK_DATA_HI, lk_data.hi);

	reg = entry;
	if (clear)
		reg |= A5PSW_LK_ADDR_CTRL_CLEAR;
	else
		reg |= A5PSW_LK_ADDR_CTRL_WRITE;

	ret = a5psw_lk_execute_ctrl(a5psw, &reg);
	if (ret)
		goto lk_unlock;

	/* Decrement LEARNCOUNT */
	if (clear) {
		reg = A5PSW_LK_LEARNCOUNT_MODE_DEC;
		a5psw_reg_writel(a5psw, A5PSW_LK_LEARNCOUNT, reg);
	}

lk_unlock:
	mutex_unlock(&a5psw->lk_lock);

	return ret;
}

static int a5psw_port_fdb_dump(struct dsa_switch *ds, int port,
			       dsa_fdb_dump_cb_t *cb, void *data)
{
	struct a5psw *a5psw = ds->priv;
	union lk_data lk_data;
	int i = 0, ret = 0;
	u32 reg;

	mutex_lock(&a5psw->lk_lock);

	for (i = 0; i < A5PSW_TABLE_ENTRIES; i++) {
		reg = A5PSW_LK_ADDR_CTRL_READ | A5PSW_LK_ADDR_CTRL_WAIT | i;

		ret = a5psw_lk_execute_ctrl(a5psw, &reg);
		if (ret)
			goto out_unlock;

		lk_data.hi = a5psw_reg_readl(a5psw, A5PSW_LK_DATA_HI);
		/* If entry is not valid or does not contain the port, skip */
		if (!lk_data.entry.valid ||
		    !(lk_data.entry.port_mask & BIT(port)))
			continue;

		lk_data.lo = a5psw_reg_readl(a5psw, A5PSW_LK_DATA_LO);

		ret = cb(lk_data.entry.mac, 0, lk_data.entry.is_static, data);
		if (ret)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(&a5psw->lk_lock);

	return ret;
}

static u64 a5psw_read_stat(struct a5psw *a5psw, u32 offset, int port)
{
	u32 reg_lo, reg_hi;

	reg_lo = a5psw_reg_readl(a5psw, offset + A5PSW_PORT_OFFSET(port));
	/* A5PSW_STATS_HIWORD is latched on stat read */
	reg_hi = a5psw_reg_readl(a5psw, A5PSW_STATS_HIWORD);

	return ((u64)reg_hi << 32) | reg_lo;
}

static void a5psw_get_strings(struct dsa_switch *ds, int port, u32 stringset,
			      uint8_t *data)
{
	unsigned int u;

	if (stringset != ETH_SS_STATS)
		return;

	for (u = 0; u < ARRAY_SIZE(a5psw_stats); u++) {
		memcpy(data + u * ETH_GSTRING_LEN, a5psw_stats[u].name,
		       ETH_GSTRING_LEN);
	}
}

static void a5psw_get_ethtool_stats(struct dsa_switch *ds, int port,
				    uint64_t *data)
{
	struct a5psw *a5psw = ds->priv;
	unsigned int u;

	for (u = 0; u < ARRAY_SIZE(a5psw_stats); u++)
		data[u] = a5psw_read_stat(a5psw, a5psw_stats[u].offset, port);
}

static int a5psw_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(a5psw_stats);
}

static void a5psw_get_eth_mac_stats(struct dsa_switch *ds, int port,
				    struct ethtool_eth_mac_stats *mac_stats)
{
	struct a5psw *a5psw = ds->priv;

#define RD(name) a5psw_read_stat(a5psw, A5PSW_##name, port)
	mac_stats->FramesTransmittedOK = RD(aFramesTransmittedOK);
	mac_stats->SingleCollisionFrames = RD(aSingleCollisions);
	mac_stats->MultipleCollisionFrames = RD(aMultipleCollisions);
	mac_stats->FramesReceivedOK = RD(aFramesReceivedOK);
	mac_stats->FrameCheckSequenceErrors = RD(aFrameCheckSequenceErrors);
	mac_stats->AlignmentErrors = RD(aAlignmentErrors);
	mac_stats->OctetsTransmittedOK = RD(aOctetsTransmittedOK);
	mac_stats->FramesWithDeferredXmissions = RD(aDeferred);
	mac_stats->LateCollisions = RD(aLateCollisions);
	mac_stats->FramesAbortedDueToXSColls = RD(aExcessiveCollisions);
	mac_stats->FramesLostDueToIntMACXmitError = RD(ifOutErrors);
	mac_stats->CarrierSenseErrors = RD(aCarrierSenseErrors);
	mac_stats->OctetsReceivedOK = RD(aOctetsReceivedOK);
	mac_stats->FramesLostDueToIntMACRcvError = RD(ifInErrors);
	mac_stats->MulticastFramesXmittedOK = RD(ifOutMulticastPkts);
	mac_stats->BroadcastFramesXmittedOK = RD(ifOutBroadcastPkts);
	mac_stats->FramesWithExcessiveDeferral = RD(aDeferred);
	mac_stats->MulticastFramesReceivedOK = RD(ifInMulticastPkts);
	mac_stats->BroadcastFramesReceivedOK = RD(ifInBroadcastPkts);
#undef RD
}

static const struct ethtool_rmon_hist_range a5psw_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 1518 },
	{ 1519, A5PSW_MAX_MTU },
	{}
};

static void a5psw_get_rmon_stats(struct dsa_switch *ds, int port,
				 struct ethtool_rmon_stats *rmon_stats,
				 const struct ethtool_rmon_hist_range **ranges)
{
	struct a5psw *a5psw = ds->priv;

#define RD(name) a5psw_read_stat(a5psw, A5PSW_##name, port)
	rmon_stats->undersize_pkts = RD(etherStatsUndersizePkts);
	rmon_stats->oversize_pkts = RD(etherStatsOversizePkts);
	rmon_stats->fragments = RD(etherStatsFragments);
	rmon_stats->jabbers = RD(etherStatsJabbers);
	rmon_stats->hist[0] = RD(etherStatsPkts64Octets);
	rmon_stats->hist[1] = RD(etherStatsPkts65to127Octets);
	rmon_stats->hist[2] = RD(etherStatsPkts128to255Octets);
	rmon_stats->hist[3] = RD(etherStatsPkts256to511Octets);
	rmon_stats->hist[4] = RD(etherStatsPkts512to1023Octets);
	rmon_stats->hist[5] = RD(etherStatsPkts1024to1518Octets);
	rmon_stats->hist[6] = RD(etherStatsPkts1519toXOctets);
#undef RD

	*ranges = a5psw_rmon_ranges;
}

static void a5psw_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct a5psw *a5psw = ds->priv;
	u64 stat;

	stat = a5psw_read_stat(a5psw, A5PSW_aTxPAUSEMACCtrlFrames, port);
	ctrl_stats->MACControlFramesTransmitted = stat;
	stat = a5psw_read_stat(a5psw, A5PSW_aRxPAUSEMACCtrlFrames, port);
	ctrl_stats->MACControlFramesReceived = stat;
}

static int a5psw_setup(struct dsa_switch *ds)
{
	struct a5psw *a5psw = ds->priv;
	int port, vlan, ret;
	struct dsa_port *dp;
	u32 reg;

	/* Validate that there is only 1 CPU port with index A5PSW_CPU_PORT */
	dsa_switch_for_each_cpu_port(dp, ds) {
		if (dp->index != A5PSW_CPU_PORT) {
			dev_err(a5psw->dev, "Invalid CPU port\n");
			return -EINVAL;
		}
	}

	/* Configure management port */
	reg = A5PSW_CPU_PORT | A5PSW_MGMT_CFG_DISCARD;
	a5psw_reg_writel(a5psw, A5PSW_MGMT_CFG, reg);

	/* Set pattern 0 to forward all frame to mgmt port */
	a5psw_reg_writel(a5psw, A5PSW_PATTERN_CTRL(A5PSW_PATTERN_MGMTFWD),
			 A5PSW_PATTERN_CTRL_MGMTFWD);

	/* Enable port tagging */
	reg = FIELD_PREP(A5PSW_MGMT_TAG_CFG_TAGFIELD, ETH_P_DSA_A5PSW);
	reg |= A5PSW_MGMT_TAG_CFG_ENABLE | A5PSW_MGMT_TAG_CFG_ALL_FRAMES;
	a5psw_reg_writel(a5psw, A5PSW_MGMT_TAG_CFG, reg);

	/* Enable normal switch operation */
	reg = A5PSW_LK_ADDR_CTRL_BLOCKING | A5PSW_LK_ADDR_CTRL_LEARNING |
	      A5PSW_LK_ADDR_CTRL_AGEING | A5PSW_LK_ADDR_CTRL_ALLOW_MIGR |
	      A5PSW_LK_ADDR_CTRL_CLEAR_TABLE;
	a5psw_reg_writel(a5psw, A5PSW_LK_CTRL, reg);

	ret = readl_poll_timeout(a5psw->base + A5PSW_LK_CTRL, reg,
				 !(reg & A5PSW_LK_ADDR_CTRL_CLEAR_TABLE),
				 A5PSW_LK_BUSY_USEC_POLL, A5PSW_CTRL_TIMEOUT);
	if (ret) {
		dev_err(a5psw->dev, "Failed to clear lookup table\n");
		return ret;
	}

	/* Reset learn count to 0 */
	reg = A5PSW_LK_LEARNCOUNT_MODE_SET;
	a5psw_reg_writel(a5psw, A5PSW_LK_LEARNCOUNT, reg);

	/* Clear VLAN resource table */
	reg = A5PSW_VLAN_RES_WR_PORTMASK | A5PSW_VLAN_RES_WR_TAGMASK;
	for (vlan = 0; vlan < A5PSW_VLAN_COUNT; vlan++)
		a5psw_reg_writel(a5psw, A5PSW_VLAN_RES(vlan), reg);

	/* Reset all ports */
	dsa_switch_for_each_port(dp, ds) {
		port = dp->index;

		/* Reset the port */
		a5psw_reg_writel(a5psw, A5PSW_CMD_CFG(port),
				 A5PSW_CMD_CFG_SW_RESET);

		/* Enable only CPU port */
		a5psw_port_enable_set(a5psw, port, dsa_port_is_cpu(dp));

		if (dsa_port_is_unused(dp))
			continue;

		/* Enable egress flooding for CPU port */
		if (dsa_port_is_cpu(dp))
			a5psw_flooding_set_resolution(a5psw, port, true);

		/* Enable management forward only for user ports */
		if (dsa_port_is_user(dp))
			a5psw_port_mgmtfwd_set(a5psw, port, true);
	}

	return 0;
}

static const struct dsa_switch_ops a5psw_switch_ops = {
	.get_tag_protocol = a5psw_get_tag_protocol,
	.setup = a5psw_setup,
	.port_disable = a5psw_port_disable,
	.port_enable = a5psw_port_enable,
	.phylink_get_caps = a5psw_phylink_get_caps,
	.phylink_mac_select_pcs = a5psw_phylink_mac_select_pcs,
	.phylink_mac_link_down = a5psw_phylink_mac_link_down,
	.phylink_mac_link_up = a5psw_phylink_mac_link_up,
	.port_change_mtu = a5psw_port_change_mtu,
	.port_max_mtu = a5psw_port_max_mtu,
	.get_sset_count = a5psw_get_sset_count,
	.get_strings = a5psw_get_strings,
	.get_ethtool_stats = a5psw_get_ethtool_stats,
	.get_eth_mac_stats = a5psw_get_eth_mac_stats,
	.get_eth_ctrl_stats = a5psw_get_eth_ctrl_stats,
	.get_rmon_stats = a5psw_get_rmon_stats,
	.set_ageing_time = a5psw_set_ageing_time,
	.port_bridge_join = a5psw_port_bridge_join,
	.port_bridge_leave = a5psw_port_bridge_leave,
	.port_stp_state_set = a5psw_port_stp_state_set,
	.port_fast_age = a5psw_port_fast_age,
	.port_fdb_add = a5psw_port_fdb_add,
	.port_fdb_del = a5psw_port_fdb_del,
	.port_fdb_dump = a5psw_port_fdb_dump,
};

static int a5psw_mdio_wait_busy(struct a5psw *a5psw)
{
	u32 status;
	int err;

	err = readl_poll_timeout(a5psw->base + A5PSW_MDIO_CFG_STATUS, status,
				 !(status & A5PSW_MDIO_CFG_STATUS_BUSY), 10,
				 1000 * USEC_PER_MSEC);
	if (err)
		dev_err(a5psw->dev, "MDIO command timeout\n");

	return err;
}

static int a5psw_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	struct a5psw *a5psw = bus->priv;
	u32 cmd, status;
	int ret;

	if (phy_reg & MII_ADDR_C45)
		return -EOPNOTSUPP;

	cmd = A5PSW_MDIO_COMMAND_READ;
	cmd |= FIELD_PREP(A5PSW_MDIO_COMMAND_REG_ADDR, phy_reg);
	cmd |= FIELD_PREP(A5PSW_MDIO_COMMAND_PHY_ADDR, phy_id);

	a5psw_reg_writel(a5psw, A5PSW_MDIO_COMMAND, cmd);

	ret = a5psw_mdio_wait_busy(a5psw);
	if (ret)
		return ret;

	ret = a5psw_reg_readl(a5psw, A5PSW_MDIO_DATA) & A5PSW_MDIO_DATA_MASK;

	status = a5psw_reg_readl(a5psw, A5PSW_MDIO_CFG_STATUS);
	if (status & A5PSW_MDIO_CFG_STATUS_READERR)
		return -EIO;

	return ret;
}

static int a5psw_mdio_write(struct mii_bus *bus, int phy_id, int phy_reg,
			    u16 phy_data)
{
	struct a5psw *a5psw = bus->priv;
	u32 cmd;

	if (phy_reg & MII_ADDR_C45)
		return -EOPNOTSUPP;

	cmd = FIELD_PREP(A5PSW_MDIO_COMMAND_REG_ADDR, phy_reg);
	cmd |= FIELD_PREP(A5PSW_MDIO_COMMAND_PHY_ADDR, phy_id);

	a5psw_reg_writel(a5psw, A5PSW_MDIO_COMMAND, cmd);
	a5psw_reg_writel(a5psw, A5PSW_MDIO_DATA, phy_data);

	return a5psw_mdio_wait_busy(a5psw);
}

static int a5psw_mdio_config(struct a5psw *a5psw, u32 mdio_freq)
{
	unsigned long rate;
	unsigned long div;
	u32 cfgstatus;

	rate = clk_get_rate(a5psw->hclk);
	div = ((rate / mdio_freq) / 2);
	if (div > FIELD_MAX(A5PSW_MDIO_CFG_STATUS_CLKDIV) ||
	    div < A5PSW_MDIO_CLK_DIV_MIN) {
		dev_err(a5psw->dev, "MDIO clock div %ld out of range\n", div);
		return -ERANGE;
	}

	cfgstatus = FIELD_PREP(A5PSW_MDIO_CFG_STATUS_CLKDIV, div);

	a5psw_reg_writel(a5psw, A5PSW_MDIO_CFG_STATUS, cfgstatus);

	return 0;
}

static int a5psw_probe_mdio(struct a5psw *a5psw, struct device_node *node)
{
	struct device *dev = a5psw->dev;
	struct mii_bus *bus;
	u32 mdio_freq;
	int ret;

	if (of_property_read_u32(node, "clock-frequency", &mdio_freq))
		mdio_freq = A5PSW_MDIO_DEF_FREQ;

	ret = a5psw_mdio_config(a5psw, mdio_freq);
	if (ret)
		return ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "a5psw_mdio";
	bus->read = a5psw_mdio_read;
	bus->write = a5psw_mdio_write;
	bus->priv = a5psw;
	bus->parent = dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	a5psw->mii_bus = bus;

	return devm_of_mdiobus_register(dev, bus, node);
}

static void a5psw_pcs_free(struct a5psw *a5psw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(a5psw->pcs); i++) {
		if (a5psw->pcs[i])
			miic_destroy(a5psw->pcs[i]);
	}
}

static int a5psw_pcs_get(struct a5psw *a5psw)
{
	struct device_node *ports, *port, *pcs_node;
	struct phylink_pcs *pcs;
	int ret;
	u32 reg;

	ports = of_get_child_by_name(a5psw->dev->of_node, "ethernet-ports");
	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		pcs_node = of_parse_phandle(port, "pcs-handle", 0);
		if (!pcs_node)
			continue;

		if (of_property_read_u32(port, "reg", &reg)) {
			ret = -EINVAL;
			goto free_pcs;
		}

		if (reg >= ARRAY_SIZE(a5psw->pcs)) {
			ret = -ENODEV;
			goto free_pcs;
		}

		pcs = miic_create(a5psw->dev, pcs_node);
		if (IS_ERR(pcs)) {
			dev_err(a5psw->dev, "Failed to create PCS for port %d\n",
				reg);
			ret = PTR_ERR(pcs);
			goto free_pcs;
		}

		a5psw->pcs[reg] = pcs;
		of_node_put(pcs_node);
	}
	of_node_put(ports);

	return 0;

free_pcs:
	of_node_put(pcs_node);
	of_node_put(port);
	of_node_put(ports);
	a5psw_pcs_free(a5psw);

	return ret;
}

static int a5psw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *mdio;
	struct dsa_switch *ds;
	struct a5psw *a5psw;
	int ret;

	a5psw = devm_kzalloc(dev, sizeof(*a5psw), GFP_KERNEL);
	if (!a5psw)
		return -ENOMEM;

	a5psw->dev = dev;
	mutex_init(&a5psw->lk_lock);
	spin_lock_init(&a5psw->reg_lock);
	a5psw->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(a5psw->base))
		return PTR_ERR(a5psw->base);

	ret = a5psw_pcs_get(a5psw);
	if (ret)
		return ret;

	a5psw->hclk = devm_clk_get(dev, "hclk");
	if (IS_ERR(a5psw->hclk)) {
		dev_err(dev, "failed get hclk clock\n");
		ret = PTR_ERR(a5psw->hclk);
		goto free_pcs;
	}

	a5psw->clk = devm_clk_get(dev, "clk");
	if (IS_ERR(a5psw->clk)) {
		dev_err(dev, "failed get clk_switch clock\n");
		ret = PTR_ERR(a5psw->clk);
		goto free_pcs;
	}

	ret = clk_prepare_enable(a5psw->clk);
	if (ret)
		goto free_pcs;

	ret = clk_prepare_enable(a5psw->hclk);
	if (ret)
		goto clk_disable;

	mdio = of_get_child_by_name(dev->of_node, "mdio");
	if (of_device_is_available(mdio)) {
		ret = a5psw_probe_mdio(a5psw, mdio);
		if (ret) {
			of_node_put(mdio);
			dev_err(dev, "Failed to register MDIO: %d\n", ret);
			goto hclk_disable;
		}
	}

	of_node_put(mdio);

	ds = &a5psw->ds;
	ds->dev = dev;
	ds->num_ports = A5PSW_PORTS_NUM;
	ds->ops = &a5psw_switch_ops;
	ds->priv = a5psw;

	ret = dsa_register_switch(ds);
	if (ret) {
		dev_err(dev, "Failed to register DSA switch: %d\n", ret);
		goto hclk_disable;
	}

	return 0;

hclk_disable:
	clk_disable_unprepare(a5psw->hclk);
clk_disable:
	clk_disable_unprepare(a5psw->clk);
free_pcs:
	a5psw_pcs_free(a5psw);

	return ret;
}

static int a5psw_remove(struct platform_device *pdev)
{
	struct a5psw *a5psw = platform_get_drvdata(pdev);

	if (!a5psw)
		return 0;

	dsa_unregister_switch(&a5psw->ds);
	a5psw_pcs_free(a5psw);
	clk_disable_unprepare(a5psw->hclk);
	clk_disable_unprepare(a5psw->clk);

	return 0;
}

static void a5psw_shutdown(struct platform_device *pdev)
{
	struct a5psw *a5psw = platform_get_drvdata(pdev);

	if (!a5psw)
		return;

	dsa_switch_shutdown(&a5psw->ds);

	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id a5psw_of_mtable[] = {
	{ .compatible = "renesas,rzn1-a5psw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, a5psw_of_mtable);

static struct platform_driver a5psw_driver = {
	.driver = {
		.name	 = "rzn1_a5psw",
		.of_match_table = of_match_ptr(a5psw_of_mtable),
	},
	.probe = a5psw_probe,
	.remove = a5psw_remove,
	.shutdown = a5psw_shutdown,
};
module_platform_driver(a5psw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ/N1 Advanced 5-port Switch driver");
MODULE_AUTHOR("Clément Léger <clement.leger@bootlin.com>");
