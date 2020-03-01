/* Copyright 2008-2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/string.h>
#include <linux/of_platform.h>
#include <linux/net_tstamp.h>
#include <linux/fsl/ptp_qoriq.h>

#include "dpaa_eth.h"
#include "mac.h"

static const char dpaa_stats_percpu[][ETH_GSTRING_LEN] = {
	"interrupts",
	"rx packets",
	"tx packets",
	"tx confirm",
	"tx S/G",
	"tx error",
	"rx error",
	"rx dropped",
	"tx dropped",
};

static char dpaa_stats_global[][ETH_GSTRING_LEN] = {
	/* dpa rx errors */
	"rx dma error",
	"rx frame physical error",
	"rx frame size error",
	"rx header error",

	/* demultiplexing errors */
	"qman cg_tdrop",
	"qman wred",
	"qman error cond",
	"qman early window",
	"qman late window",
	"qman fq tdrop",
	"qman fq retired",
	"qman orp disabled",

	/* congestion related stats */
	"congestion time (ms)",
	"entered congestion",
	"congested (0/1)"
};

#define DPAA_STATS_PERCPU_LEN ARRAY_SIZE(dpaa_stats_percpu)
#define DPAA_STATS_GLOBAL_LEN ARRAY_SIZE(dpaa_stats_global)

static int dpaa_get_link_ksettings(struct net_device *net_dev,
				   struct ethtool_link_ksettings *cmd)
{
	if (!net_dev->phydev)
		return 0;

	phy_ethtool_ksettings_get(net_dev->phydev, cmd);

	return 0;
}

static int dpaa_set_link_ksettings(struct net_device *net_dev,
				   const struct ethtool_link_ksettings *cmd)
{
	int err;

	if (!net_dev->phydev)
		return -ENODEV;

	err = phy_ethtool_ksettings_set(net_dev->phydev, cmd);
	if (err < 0)
		netdev_err(net_dev, "phy_ethtool_ksettings_set() = %d\n", err);

	return err;
}

static void dpaa_get_drvinfo(struct net_device *net_dev,
			     struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, KBUILD_MODNAME,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->bus_info, dev_name(net_dev->dev.parent->parent),
		sizeof(drvinfo->bus_info));
}

static u32 dpaa_get_msglevel(struct net_device *net_dev)
{
	return ((struct dpaa_priv *)netdev_priv(net_dev))->msg_enable;
}

static void dpaa_set_msglevel(struct net_device *net_dev,
			      u32 msg_enable)
{
	((struct dpaa_priv *)netdev_priv(net_dev))->msg_enable = msg_enable;
}

static int dpaa_nway_reset(struct net_device *net_dev)
{
	int err;

	if (!net_dev->phydev)
		return -ENODEV;

	err = 0;
	if (net_dev->phydev->autoneg) {
		err = phy_start_aneg(net_dev->phydev);
		if (err < 0)
			netdev_err(net_dev, "phy_start_aneg() = %d\n",
				   err);
	}

	return err;
}

static void dpaa_get_pauseparam(struct net_device *net_dev,
				struct ethtool_pauseparam *epause)
{
	struct mac_device *mac_dev;
	struct dpaa_priv *priv;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	if (!net_dev->phydev)
		return;

	epause->autoneg = mac_dev->autoneg_pause;
	epause->rx_pause = mac_dev->rx_pause_active;
	epause->tx_pause = mac_dev->tx_pause_active;
}

static int dpaa_set_pauseparam(struct net_device *net_dev,
			       struct ethtool_pauseparam *epause)
{
	struct mac_device *mac_dev;
	struct phy_device *phydev;
	bool rx_pause, tx_pause;
	struct dpaa_priv *priv;
	int err;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;

	phydev = net_dev->phydev;
	if (!phydev) {
		netdev_err(net_dev, "phy device not initialized\n");
		return -ENODEV;
	}

	if (!phy_validate_pause(phydev, epause))
		return -EINVAL;

	/* The MAC should know how to handle PAUSE frame autonegotiation before
	 * adjust_link is triggered by a forced renegotiation of sym/asym PAUSE
	 * settings.
	 */
	mac_dev->autoneg_pause = !!epause->autoneg;
	mac_dev->rx_pause_req = !!epause->rx_pause;
	mac_dev->tx_pause_req = !!epause->tx_pause;

	/* Determine the sym/asym advertised PAUSE capabilities from the desired
	 * rx/tx pause settings.
	 */

	phy_set_asym_pause(phydev, epause->rx_pause, epause->tx_pause);

	fman_get_pause_cfg(mac_dev, &rx_pause, &tx_pause);
	err = fman_set_mac_active_pause(mac_dev, rx_pause, tx_pause);
	if (err < 0)
		netdev_err(net_dev, "set_mac_active_pause() = %d\n", err);

	return err;
}

static int dpaa_get_sset_count(struct net_device *net_dev, int type)
{
	unsigned int total_stats, num_stats;

	num_stats   = num_online_cpus() + 1;
	total_stats = num_stats * (DPAA_STATS_PERCPU_LEN + 1) +
			DPAA_STATS_GLOBAL_LEN;

	switch (type) {
	case ETH_SS_STATS:
		return total_stats;
	default:
		return -EOPNOTSUPP;
	}
}

static void copy_stats(struct dpaa_percpu_priv *percpu_priv, int num_cpus,
		       int crr_cpu, u64 bp_count, u64 *data)
{
	int num_values = num_cpus + 1;
	int crr = 0;

	/* update current CPU's stats and also add them to the total values */
	data[crr * num_values + crr_cpu] = percpu_priv->in_interrupt;
	data[crr++ * num_values + num_cpus] += percpu_priv->in_interrupt;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.rx_packets;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.rx_packets;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.tx_packets;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.tx_packets;

	data[crr * num_values + crr_cpu] = percpu_priv->tx_confirm;
	data[crr++ * num_values + num_cpus] += percpu_priv->tx_confirm;

	data[crr * num_values + crr_cpu] = percpu_priv->tx_frag_skbuffs;
	data[crr++ * num_values + num_cpus] += percpu_priv->tx_frag_skbuffs;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.tx_errors;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.tx_errors;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.rx_errors;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.rx_errors;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.rx_dropped;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.rx_dropped;

	data[crr * num_values + crr_cpu] = percpu_priv->stats.tx_dropped;
	data[crr++ * num_values + num_cpus] += percpu_priv->stats.tx_dropped;

	data[crr * num_values + crr_cpu] = bp_count;
	data[crr++ * num_values + num_cpus] += bp_count;
}

static void dpaa_get_ethtool_stats(struct net_device *net_dev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct dpaa_percpu_priv *percpu_priv;
	struct dpaa_rx_errors rx_errors;
	unsigned int num_cpus, offset;
	u64 bp_count, cg_time, cg_num;
	struct dpaa_ern_cnt ern_cnt;
	struct dpaa_bp *dpaa_bp;
	struct dpaa_priv *priv;
	int total_stats, i;
	bool cg_status;

	total_stats = dpaa_get_sset_count(net_dev, ETH_SS_STATS);
	priv     = netdev_priv(net_dev);
	num_cpus = num_online_cpus();

	memset(&bp_count, 0, sizeof(bp_count));
	memset(&rx_errors, 0, sizeof(struct dpaa_rx_errors));
	memset(&ern_cnt, 0, sizeof(struct dpaa_ern_cnt));
	memset(data, 0, total_stats * sizeof(u64));

	for_each_online_cpu(i) {
		percpu_priv = per_cpu_ptr(priv->percpu_priv, i);
		dpaa_bp = priv->dpaa_bp;
		if (!dpaa_bp->percpu_count)
			continue;
		bp_count = *(per_cpu_ptr(dpaa_bp->percpu_count, i));
		rx_errors.dme += percpu_priv->rx_errors.dme;
		rx_errors.fpe += percpu_priv->rx_errors.fpe;
		rx_errors.fse += percpu_priv->rx_errors.fse;
		rx_errors.phe += percpu_priv->rx_errors.phe;

		ern_cnt.cg_tdrop     += percpu_priv->ern_cnt.cg_tdrop;
		ern_cnt.wred         += percpu_priv->ern_cnt.wred;
		ern_cnt.err_cond     += percpu_priv->ern_cnt.err_cond;
		ern_cnt.early_window += percpu_priv->ern_cnt.early_window;
		ern_cnt.late_window  += percpu_priv->ern_cnt.late_window;
		ern_cnt.fq_tdrop     += percpu_priv->ern_cnt.fq_tdrop;
		ern_cnt.fq_retired   += percpu_priv->ern_cnt.fq_retired;
		ern_cnt.orp_zero     += percpu_priv->ern_cnt.orp_zero;

		copy_stats(percpu_priv, num_cpus, i, bp_count, data);
	}

	offset = (num_cpus + 1) * (DPAA_STATS_PERCPU_LEN + 1);
	memcpy(data + offset, &rx_errors, sizeof(struct dpaa_rx_errors));

	offset += sizeof(struct dpaa_rx_errors) / sizeof(u64);
	memcpy(data + offset, &ern_cnt, sizeof(struct dpaa_ern_cnt));

	/* gather congestion related counters */
	cg_num    = 0;
	cg_status = false;
	cg_time   = jiffies_to_msecs(priv->cgr_data.congested_jiffies);
	if (qman_query_cgr_congested(&priv->cgr_data.cgr, &cg_status) == 0) {
		cg_num    = priv->cgr_data.cgr_congested_count;

		/* reset congestion stats (like QMan API does */
		priv->cgr_data.congested_jiffies   = 0;
		priv->cgr_data.cgr_congested_count = 0;
	}

	offset += sizeof(struct dpaa_ern_cnt) / sizeof(u64);
	data[offset++] = cg_time;
	data[offset++] = cg_num;
	data[offset++] = cg_status;
}

static void dpaa_get_strings(struct net_device *net_dev, u32 stringset,
			     u8 *data)
{
	unsigned int i, j, num_cpus, size;
	char string_cpu[ETH_GSTRING_LEN];
	u8 *strings;

	memset(string_cpu, 0, sizeof(string_cpu));
	strings   = data;
	num_cpus  = num_online_cpus();
	size      = DPAA_STATS_GLOBAL_LEN * ETH_GSTRING_LEN;

	for (i = 0; i < DPAA_STATS_PERCPU_LEN; i++) {
		for (j = 0; j < num_cpus; j++) {
			snprintf(string_cpu, ETH_GSTRING_LEN, "%s [CPU %d]",
				 dpaa_stats_percpu[i], j);
			memcpy(strings, string_cpu, ETH_GSTRING_LEN);
			strings += ETH_GSTRING_LEN;
		}
		snprintf(string_cpu, ETH_GSTRING_LEN, "%s [TOTAL]",
			 dpaa_stats_percpu[i]);
		memcpy(strings, string_cpu, ETH_GSTRING_LEN);
		strings += ETH_GSTRING_LEN;
	}
	for (j = 0; j < num_cpus; j++) {
		snprintf(string_cpu, ETH_GSTRING_LEN,
			 "bpool [CPU %d]", j);
		memcpy(strings, string_cpu, ETH_GSTRING_LEN);
		strings += ETH_GSTRING_LEN;
	}
	snprintf(string_cpu, ETH_GSTRING_LEN, "bpool [TOTAL]");
	memcpy(strings, string_cpu, ETH_GSTRING_LEN);
	strings += ETH_GSTRING_LEN;

	memcpy(strings, dpaa_stats_global, size);
}

static int dpaa_get_hash_opts(struct net_device *dev,
			      struct ethtool_rxnfc *cmd)
{
	struct dpaa_priv *priv = netdev_priv(dev);

	cmd->data = 0;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		if (priv->keygen_in_use)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* Fall through */
	case IPV4_FLOW:
	case IPV6_FLOW:
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V4_FLOW:
	case ESP_V6_FLOW:
		if (priv->keygen_in_use)
			cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		cmd->data = 0;
		break;
	}

	return 0;
}

static int dpaa_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			  u32 *unused)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXFH:
		ret = dpaa_get_hash_opts(dev, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static void dpaa_set_hash(struct net_device *net_dev, bool enable)
{
	struct mac_device *mac_dev;
	struct fman_port *rxport;
	struct dpaa_priv *priv;

	priv = netdev_priv(net_dev);
	mac_dev = priv->mac_dev;
	rxport = mac_dev->port[0];

	fman_port_use_kg_hash(rxport, enable);
	priv->keygen_in_use = enable;
}

static int dpaa_set_hash_opts(struct net_device *dev,
			      struct ethtool_rxnfc *nfc)
{
	int ret = -EINVAL;

	/* we support hashing on IPv4/v6 src/dest IP and L4 src/dest port */
	if (nfc->data &
	    ~(RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V4_FLOW:
	case ESP_V6_FLOW:
		dpaa_set_hash(dev, !!nfc->data);
		ret = 0;
		break;
	default:
		break;
	}

	return ret;
}

static int dpaa_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = dpaa_set_hash_opts(dev, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int dpaa_get_ts_info(struct net_device *net_dev,
			    struct ethtool_ts_info *info)
{
	struct device *dev = net_dev->dev.parent;
	struct device_node *mac_node = dev->of_node;
	struct device_node *fman_node = NULL, *ptp_node = NULL;
	struct platform_device *ptp_dev = NULL;
	struct ptp_qoriq *ptp = NULL;

	info->phc_index = -1;

	fman_node = of_get_parent(mac_node);
	if (fman_node)
		ptp_node = of_parse_phandle(fman_node, "ptimer-handle", 0);

	if (ptp_node)
		ptp_dev = of_find_device_by_node(ptp_node);

	if (ptp_dev)
		ptp = platform_get_drvdata(ptp_dev);

	if (ptp)
		info->phc_index = ptp->phc_index;

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON);
	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);

	return 0;
}

static int dpaa_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *c)
{
	struct qman_portal *portal;
	u32 period;
	u8 thresh;

	portal = qman_get_affine_portal(smp_processor_id());
	qman_portal_get_iperiod(portal, &period);
	qman_dqrr_get_ithresh(portal, &thresh);

	c->rx_coalesce_usecs = period;
	c->rx_max_coalesced_frames = thresh;
	c->use_adaptive_rx_coalesce = false;

	return 0;
}

static int dpaa_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *c)
{
	const cpumask_t *cpus = qman_affine_cpus();
	bool needs_revert[NR_CPUS] = {false};
	struct qman_portal *portal;
	u32 period, prev_period;
	u8 thresh, prev_thresh;
	int cpu, res;

	if (c->use_adaptive_rx_coalesce)
		return -EINVAL;

	period = c->rx_coalesce_usecs;
	thresh = c->rx_max_coalesced_frames;

	/* save previous values */
	portal = qman_get_affine_portal(smp_processor_id());
	qman_portal_get_iperiod(portal, &prev_period);
	qman_dqrr_get_ithresh(portal, &prev_thresh);

	/* set new values */
	for_each_cpu_and(cpu, cpus, cpu_online_mask) {
		portal = qman_get_affine_portal(cpu);
		res = qman_portal_set_iperiod(portal, period);
		if (res)
			goto revert_values;
		res = qman_dqrr_set_ithresh(portal, thresh);
		if (res) {
			qman_portal_set_iperiod(portal, prev_period);
			goto revert_values;
		}
		needs_revert[cpu] = true;
	}

	return 0;

revert_values:
	/* restore previous values */
	for_each_cpu_and(cpu, cpus, cpu_online_mask) {
		if (!needs_revert[cpu])
			continue;
		portal = qman_get_affine_portal(cpu);
		/* previous values will not fail, ignore return value */
		qman_portal_set_iperiod(portal, prev_period);
		qman_dqrr_set_ithresh(portal, prev_thresh);
	}

	return res;
}

const struct ethtool_ops dpaa_ethtool_ops = {
	.get_drvinfo = dpaa_get_drvinfo,
	.get_msglevel = dpaa_get_msglevel,
	.set_msglevel = dpaa_set_msglevel,
	.nway_reset = dpaa_nway_reset,
	.get_pauseparam = dpaa_get_pauseparam,
	.set_pauseparam = dpaa_set_pauseparam,
	.get_link = ethtool_op_get_link,
	.get_sset_count = dpaa_get_sset_count,
	.get_ethtool_stats = dpaa_get_ethtool_stats,
	.get_strings = dpaa_get_strings,
	.get_link_ksettings = dpaa_get_link_ksettings,
	.set_link_ksettings = dpaa_set_link_ksettings,
	.get_rxnfc = dpaa_get_rxnfc,
	.set_rxnfc = dpaa_set_rxnfc,
	.get_ts_info = dpaa_get_ts_info,
	.get_coalesce = dpaa_get_coalesce,
	.set_coalesce = dpaa_set_coalesce,
};
