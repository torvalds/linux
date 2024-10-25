// SPDX-License-Identifier: GPL-2.0
/*
 * DPAA2 Ethernet Switch ethtool support
 *
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
 *
 */

#include <linux/ethtool.h>

#include "dpaa2-switch.h"

static struct {
	enum dpsw_counter id;
	char name[ETH_GSTRING_LEN];
} dpaa2_switch_ethtool_counters[] =  {
	{DPSW_CNT_ING_FRAME,		"[hw] rx frames"},
	{DPSW_CNT_ING_BYTE,		"[hw] rx bytes"},
	{DPSW_CNT_ING_FLTR_FRAME,	"[hw] rx filtered frames"},
	{DPSW_CNT_ING_FRAME_DISCARD,	"[hw] rx discarded frames"},
	{DPSW_CNT_ING_BCAST_FRAME,	"[hw] rx bcast frames"},
	{DPSW_CNT_ING_BCAST_BYTES,	"[hw] rx bcast bytes"},
	{DPSW_CNT_ING_MCAST_FRAME,	"[hw] rx mcast frames"},
	{DPSW_CNT_ING_MCAST_BYTE,	"[hw] rx mcast bytes"},
	{DPSW_CNT_EGR_FRAME,		"[hw] tx frames"},
	{DPSW_CNT_EGR_BYTE,		"[hw] tx bytes"},
	{DPSW_CNT_EGR_FRAME_DISCARD,	"[hw] tx discarded frames"},
	{DPSW_CNT_ING_NO_BUFF_DISCARD,	"[hw] rx nobuffer discards"},
};

#define DPAA2_SWITCH_NUM_COUNTERS	ARRAY_SIZE(dpaa2_switch_ethtool_counters)

static void dpaa2_switch_get_drvinfo(struct net_device *netdev,
				     struct ethtool_drvinfo *drvinfo)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	u16 version_major, version_minor;
	int err;

	strscpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));

	err = dpsw_get_api_version(port_priv->ethsw_data->mc_io, 0,
				   &version_major,
				   &version_minor);
	if (err)
		strscpy(drvinfo->fw_version, "N/A",
			sizeof(drvinfo->fw_version));
	else
		snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
			 "%u.%u", version_major, version_minor);

	strscpy(drvinfo->bus_info, dev_name(netdev->dev.parent->parent),
		sizeof(drvinfo->bus_info));
}

static int
dpaa2_switch_get_link_ksettings(struct net_device *netdev,
				struct ethtool_link_ksettings *link_ksettings)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct dpsw_link_state state = {0};
	int err;

	mutex_lock(&port_priv->mac_lock);

	if (dpaa2_switch_port_is_type_phy(port_priv)) {
		err = phylink_ethtool_ksettings_get(port_priv->mac->phylink,
						    link_ksettings);
		mutex_unlock(&port_priv->mac_lock);
		return err;
	}

	mutex_unlock(&port_priv->mac_lock);

	err = dpsw_if_get_link_state(port_priv->ethsw_data->mc_io, 0,
				     port_priv->ethsw_data->dpsw_handle,
				     port_priv->idx,
				     &state);
	if (err) {
		netdev_err(netdev, "ERROR %d getting link state\n", err);
		goto out;
	}

	/* At the moment, we have no way of interrogating the DPMAC
	 * from the DPSW side or there may not exist a DPMAC at all.
	 * Report only autoneg state, duplexity and speed.
	 */
	if (state.options & DPSW_LINK_OPT_AUTONEG)
		link_ksettings->base.autoneg = AUTONEG_ENABLE;
	if (!(state.options & DPSW_LINK_OPT_HALF_DUPLEX))
		link_ksettings->base.duplex = DUPLEX_FULL;
	link_ksettings->base.speed = state.rate;

out:
	return err;
}

static int
dpaa2_switch_set_link_ksettings(struct net_device *netdev,
				const struct ethtool_link_ksettings *link_ksettings)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpsw_link_cfg cfg = {0};
	bool if_running;
	int err = 0, ret;

	mutex_lock(&port_priv->mac_lock);

	if (dpaa2_switch_port_is_type_phy(port_priv)) {
		err = phylink_ethtool_ksettings_set(port_priv->mac->phylink,
						    link_ksettings);
		mutex_unlock(&port_priv->mac_lock);
		return err;
	}

	mutex_unlock(&port_priv->mac_lock);

	/* Interface needs to be down to change link settings */
	if_running = netif_running(netdev);
	if (if_running) {
		err = dpsw_if_disable(ethsw->mc_io, 0,
				      ethsw->dpsw_handle,
				      port_priv->idx);
		if (err) {
			netdev_err(netdev, "dpsw_if_disable err %d\n", err);
			return err;
		}
	}

	cfg.rate = link_ksettings->base.speed;
	if (link_ksettings->base.autoneg == AUTONEG_ENABLE)
		cfg.options |= DPSW_LINK_OPT_AUTONEG;
	else
		cfg.options &= ~DPSW_LINK_OPT_AUTONEG;
	if (link_ksettings->base.duplex  == DUPLEX_HALF)
		cfg.options |= DPSW_LINK_OPT_HALF_DUPLEX;
	else
		cfg.options &= ~DPSW_LINK_OPT_HALF_DUPLEX;

	err = dpsw_if_set_link_cfg(port_priv->ethsw_data->mc_io, 0,
				   port_priv->ethsw_data->dpsw_handle,
				   port_priv->idx,
				   &cfg);

	if (if_running) {
		ret = dpsw_if_enable(ethsw->mc_io, 0,
				     ethsw->dpsw_handle,
				     port_priv->idx);
		if (ret) {
			netdev_err(netdev, "dpsw_if_enable err %d\n", ret);
			return ret;
		}
	}
	return err;
}

static int
dpaa2_switch_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return DPAA2_SWITCH_NUM_COUNTERS + dpaa2_mac_get_sset_count();
	default:
		return -EOPNOTSUPP;
	}
}

static void dpaa2_switch_ethtool_get_strings(struct net_device *netdev,
					     u32 stringset, u8 *data)
{
	const char *str;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < DPAA2_SWITCH_NUM_COUNTERS; i++) {
			str = dpaa2_switch_ethtool_counters[i].name;
			ethtool_puts(&data, str);
		}
		dpaa2_mac_get_strings(&data);
		break;
	}
}

static void dpaa2_switch_ethtool_get_stats(struct net_device *netdev,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct ethsw_port_priv *port_priv = netdev_priv(netdev);
	int i, err;

	for (i = 0; i < DPAA2_SWITCH_NUM_COUNTERS; i++) {
		err = dpsw_if_get_counter(port_priv->ethsw_data->mc_io, 0,
					  port_priv->ethsw_data->dpsw_handle,
					  port_priv->idx,
					  dpaa2_switch_ethtool_counters[i].id,
					  &data[i]);
		if (err)
			netdev_err(netdev, "dpsw_if_get_counter[%s] err %d\n",
				   dpaa2_switch_ethtool_counters[i].name, err);
	}

	mutex_lock(&port_priv->mac_lock);

	if (dpaa2_switch_port_has_mac(port_priv))
		dpaa2_mac_get_ethtool_stats(port_priv->mac, data + i);

	mutex_unlock(&port_priv->mac_lock);
}

const struct ethtool_ops dpaa2_switch_port_ethtool_ops = {
	.get_drvinfo		= dpaa2_switch_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= dpaa2_switch_get_link_ksettings,
	.set_link_ksettings	= dpaa2_switch_set_link_ksettings,
	.get_strings		= dpaa2_switch_ethtool_get_strings,
	.get_ethtool_stats	= dpaa2_switch_ethtool_get_stats,
	.get_sset_count		= dpaa2_switch_ethtool_get_sset_count,
};
