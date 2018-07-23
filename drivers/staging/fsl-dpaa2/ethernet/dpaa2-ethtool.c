/* Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
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

#include <linux/net_tstamp.h>

#include "dpni.h"	/* DPNI_LINK_OPT_* */
#include "dpaa2-eth.h"

/* To be kept in sync with DPNI statistics */
static char dpaa2_ethtool_stats[][ETH_GSTRING_LEN] = {
	"[hw] rx frames",
	"[hw] rx bytes",
	"[hw] rx mcast frames",
	"[hw] rx mcast bytes",
	"[hw] rx bcast frames",
	"[hw] rx bcast bytes",
	"[hw] tx frames",
	"[hw] tx bytes",
	"[hw] tx mcast frames",
	"[hw] tx mcast bytes",
	"[hw] tx bcast frames",
	"[hw] tx bcast bytes",
	"[hw] rx filtered frames",
	"[hw] rx discarded frames",
	"[hw] rx nobuffer discards",
	"[hw] tx discarded frames",
	"[hw] tx confirmed frames",
};

#define DPAA2_ETH_NUM_STATS	ARRAY_SIZE(dpaa2_ethtool_stats)

static char dpaa2_ethtool_extras[][ETH_GSTRING_LEN] = {
	/* per-cpu stats */
	"[drv] tx conf frames",
	"[drv] tx conf bytes",
	"[drv] tx sg frames",
	"[drv] tx sg bytes",
	"[drv] tx realloc frames",
	"[drv] rx sg frames",
	"[drv] rx sg bytes",
	"[drv] enqueue portal busy",
	/* Channel stats */
	"[drv] dequeue portal busy",
	"[drv] channel pull errors",
	"[drv] cdan",
};

#define DPAA2_ETH_NUM_EXTRA_STATS	ARRAY_SIZE(dpaa2_ethtool_extras)

static void dpaa2_eth_get_drvinfo(struct net_device *net_dev,
				  struct ethtool_drvinfo *drvinfo)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, dpaa2_eth_drv_version,
		sizeof(drvinfo->version));

	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%u.%u", priv->dpni_ver_major, priv->dpni_ver_minor);

	strlcpy(drvinfo->bus_info, dev_name(net_dev->dev.parent->parent),
		sizeof(drvinfo->bus_info));
}

static int
dpaa2_eth_get_link_ksettings(struct net_device *net_dev,
			     struct ethtool_link_ksettings *link_settings)
{
	struct dpni_link_state state = {0};
	int err = 0;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	err = dpni_get_link_state(priv->mc_io, 0, priv->mc_token, &state);
	if (err) {
		netdev_err(net_dev, "ERROR %d getting link state\n", err);
		goto out;
	}

	/* At the moment, we have no way of interrogating the DPMAC
	 * from the DPNI side - and for that matter there may exist
	 * no DPMAC at all. So for now we just don't report anything
	 * beyond the DPNI attributes.
	 */
	if (state.options & DPNI_LINK_OPT_AUTONEG)
		link_settings->base.autoneg = AUTONEG_ENABLE;
	if (!(state.options & DPNI_LINK_OPT_HALF_DUPLEX))
		link_settings->base.duplex = DUPLEX_FULL;
	link_settings->base.speed = state.rate;

out:
	return err;
}

#define DPNI_DYNAMIC_LINK_SET_VER_MAJOR		7
#define DPNI_DYNAMIC_LINK_SET_VER_MINOR		1
static int
dpaa2_eth_set_link_ksettings(struct net_device *net_dev,
			     const struct ethtool_link_ksettings *link_settings)
{
	struct dpni_link_cfg cfg = {0};
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	int err = 0;

	/* If using an older MC version, the DPNI must be down
	 * in order to be able to change link settings. Taking steps to let
	 * the user know that.
	 */
	if (dpaa2_eth_cmp_dpni_ver(priv, DPNI_DYNAMIC_LINK_SET_VER_MAJOR,
				   DPNI_DYNAMIC_LINK_SET_VER_MINOR) < 0) {
		if (netif_running(net_dev)) {
			netdev_info(net_dev, "Interface must be brought down first.\n");
			return -EACCES;
		}
	}

	cfg.rate = link_settings->base.speed;
	if (link_settings->base.autoneg == AUTONEG_ENABLE)
		cfg.options |= DPNI_LINK_OPT_AUTONEG;
	else
		cfg.options &= ~DPNI_LINK_OPT_AUTONEG;
	if (link_settings->base.duplex  == DUPLEX_HALF)
		cfg.options |= DPNI_LINK_OPT_HALF_DUPLEX;
	else
		cfg.options &= ~DPNI_LINK_OPT_HALF_DUPLEX;

	err = dpni_set_link_cfg(priv->mc_io, 0, priv->mc_token, &cfg);
	if (err)
		/* ethtool will be loud enough if we return an error; no point
		 * in putting our own error message on the console by default
		 */
		netdev_dbg(net_dev, "ERROR %d setting link cfg\n", err);

	return err;
}

static void dpaa2_eth_get_strings(struct net_device *netdev, u32 stringset,
				  u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < DPAA2_ETH_NUM_STATS; i++) {
			strlcpy(p, dpaa2_ethtool_stats[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < DPAA2_ETH_NUM_EXTRA_STATS; i++) {
			strlcpy(p, dpaa2_ethtool_extras[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int dpaa2_eth_get_sset_count(struct net_device *net_dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS: /* ethtool_get_stats(), ethtool_get_drvinfo() */
		return DPAA2_ETH_NUM_STATS + DPAA2_ETH_NUM_EXTRA_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

/** Fill in hardware counters, as returned by MC.
 */
static void dpaa2_eth_get_ethtool_stats(struct net_device *net_dev,
					struct ethtool_stats *stats,
					u64 *data)
{
	int i = 0;
	int j, k, err;
	int num_cnt;
	union dpni_statistics dpni_stats;
	u64 cdan = 0;
	u64 portal_busy = 0, pull_err = 0;
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpaa2_eth_drv_stats *extras;
	struct dpaa2_eth_ch_stats *ch_stats;

	memset(data, 0,
	       sizeof(u64) * (DPAA2_ETH_NUM_STATS + DPAA2_ETH_NUM_EXTRA_STATS));

	/* Print standard counters, from DPNI statistics */
	for (j = 0; j <= 2; j++) {
		err = dpni_get_statistics(priv->mc_io, 0, priv->mc_token,
					  j, &dpni_stats);
		if (err != 0)
			netdev_warn(net_dev, "dpni_get_stats(%d) failed\n", j);
		switch (j) {
		case 0:
			num_cnt = sizeof(dpni_stats.page_0) / sizeof(u64);
			break;
		case 1:
			num_cnt = sizeof(dpni_stats.page_1) / sizeof(u64);
			break;
		case 2:
			num_cnt = sizeof(dpni_stats.page_2) / sizeof(u64);
			break;
		}
		for (k = 0; k < num_cnt; k++)
			*(data + i++) = dpni_stats.raw.counter[k];
	}

	/* Print per-cpu extra stats */
	for_each_online_cpu(k) {
		extras = per_cpu_ptr(priv->percpu_extras, k);
		for (j = 0; j < sizeof(*extras) / sizeof(__u64); j++)
			*((__u64 *)data + i + j) += *((__u64 *)extras + j);
	}
	i += j;

	for (j = 0; j < priv->num_channels; j++) {
		ch_stats = &priv->channel[j]->stats;
		cdan += ch_stats->cdan;
		portal_busy += ch_stats->dequeue_portal_busy;
		pull_err += ch_stats->pull_err;
	}

	*(data + i++) = portal_busy;
	*(data + i++) = pull_err;
	*(data + i++) = cdan;
}

static int dpaa2_eth_get_rxnfc(struct net_device *net_dev,
			       struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	switch (rxnfc->cmd) {
	case ETHTOOL_GRXFH:
		/* we purposely ignore cmd->flow_type for now, because the
		 * classifier only supports a single set of fields for all
		 * protocols
		 */
		rxnfc->data = priv->rx_hash_fields;
		break;
	case ETHTOOL_GRXRINGS:
		rxnfc->data = dpaa2_eth_queue_count(priv);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int dpaa2_phc_index = -1;
EXPORT_SYMBOL(dpaa2_phc_index);

static int dpaa2_eth_get_ts_info(struct net_device *dev,
				 struct ethtool_ts_info *info)
{
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->phc_index = dpaa2_phc_index;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) |
			 (1 << HWTSTAMP_TX_ON);

	info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
			   (1 << HWTSTAMP_FILTER_ALL);
	return 0;
}

const struct ethtool_ops dpaa2_ethtool_ops = {
	.get_drvinfo = dpaa2_eth_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = dpaa2_eth_get_link_ksettings,
	.set_link_ksettings = dpaa2_eth_set_link_ksettings,
	.get_sset_count = dpaa2_eth_get_sset_count,
	.get_ethtool_stats = dpaa2_eth_get_ethtool_stats,
	.get_strings = dpaa2_eth_get_strings,
	.get_rxnfc = dpaa2_eth_get_rxnfc,
	.get_ts_info = dpaa2_eth_get_ts_info,
};
