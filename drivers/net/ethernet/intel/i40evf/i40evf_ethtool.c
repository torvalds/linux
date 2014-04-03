/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

/* ethtool support for i40evf */
#include "i40evf.h"

#include <linux/uaccess.h>


struct i40evf_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_offset;
};

#define I40EVF_STAT(_name, _stat) { \
	.stat_string = _name, \
	.stat_offset = offsetof(struct i40evf_adapter, _stat) \
}

/* All stats are u64, so we don't need to track the size of the field. */
static const struct i40evf_stats i40evf_gstrings_stats[] = {
	I40EVF_STAT("rx_bytes", current_stats.rx_bytes),
	I40EVF_STAT("rx_unicast", current_stats.rx_unicast),
	I40EVF_STAT("rx_multicast", current_stats.rx_multicast),
	I40EVF_STAT("rx_broadcast", current_stats.rx_broadcast),
	I40EVF_STAT("rx_discards", current_stats.rx_discards),
	I40EVF_STAT("rx_errors", current_stats.rx_errors),
	I40EVF_STAT("rx_missed", current_stats.rx_missed),
	I40EVF_STAT("rx_unknown_protocol", current_stats.rx_unknown_protocol),
	I40EVF_STAT("tx_bytes", current_stats.tx_bytes),
	I40EVF_STAT("tx_unicast", current_stats.tx_unicast),
	I40EVF_STAT("tx_multicast", current_stats.tx_multicast),
	I40EVF_STAT("tx_broadcast", current_stats.tx_broadcast),
	I40EVF_STAT("tx_discards", current_stats.tx_discards),
	I40EVF_STAT("tx_errors", current_stats.tx_errors),
};

#define I40EVF_GLOBAL_STATS_LEN ARRAY_SIZE(i40evf_gstrings_stats)
#define I40EVF_QUEUE_STATS_LEN \
	(((struct i40evf_adapter *) \
		netdev_priv(netdev))->vsi_res->num_queue_pairs * 4)
#define I40EVF_STATS_LEN (I40EVF_GLOBAL_STATS_LEN + I40EVF_QUEUE_STATS_LEN)

/**
 * i40evf_get_settings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @ecmd: ethtool command
 *
 * Reports speed/duplex settings. Because this is a VF, we don't know what
 * kind of link we really have, so we fake it.
 **/
static int i40evf_get_settings(struct net_device *netdev,
			       struct ethtool_cmd *ecmd)
{
	/* In the future the VF will be able to query the PF for
	 * some information - for now use a dummy value
	 */
	ecmd->supported = SUPPORTED_10000baseT_Full;
	ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->transceiver = XCVR_DUMMY1;
	ecmd->port = PORT_NONE;

	return 0;
}

/**
 * i40evf_get_sset_count - Get length of string set
 * @netdev: network interface device structure
 * @sset: id of string set
 *
 * Reports size of string table. This driver only supports
 * strings for statistics.
 **/
static int i40evf_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset == ETH_SS_STATS)
		return I40EVF_STATS_LEN;
	else
		return -ENOTSUPP;
}

/**
 * i40evf_get_ethtool_stats - report device statistics
 * @netdev: network interface device structure
 * @stats: ethtool statistics structure
 * @data: pointer to data buffer
 *
 * All statistics are added to the data buffer as an array of u64.
 **/
static void i40evf_get_ethtool_stats(struct net_device *netdev,
				     struct ethtool_stats *stats, u64 *data)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int i, j;
	char *p;

	for (i = 0; i < I40EVF_GLOBAL_STATS_LEN; i++) {
		p = (char *)adapter + i40evf_gstrings_stats[i].stat_offset;
		data[i] =  *(u64 *)p;
	}
	for (j = 0; j < adapter->vsi_res->num_queue_pairs; j++) {
		data[i++] = adapter->tx_rings[j]->stats.packets;
		data[i++] = adapter->tx_rings[j]->stats.bytes;
	}
	for (j = 0; j < adapter->vsi_res->num_queue_pairs; j++) {
		data[i++] = adapter->rx_rings[j]->stats.packets;
		data[i++] = adapter->rx_rings[j]->stats.bytes;
	}
}

/**
 * i40evf_get_strings - Get string set
 * @netdev: network interface device structure
 * @sset: id of string set
 * @data: buffer for string data
 *
 * Builds stats string table.
 **/
static void i40evf_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	u8 *p = data;
	int i;

	if (sset == ETH_SS_STATS) {
		for (i = 0; i < I40EVF_GLOBAL_STATS_LEN; i++) {
			memcpy(p, i40evf_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->vsi_res->num_queue_pairs; i++) {
			snprintf(p, ETH_GSTRING_LEN, "tx-%u.packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "tx-%u.bytes", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->vsi_res->num_queue_pairs; i++) {
			snprintf(p, ETH_GSTRING_LEN, "rx-%u.packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "rx-%u.bytes", i);
			p += ETH_GSTRING_LEN;
		}
	}
}

/**
 * i40evf_get_msglevel - Get debug message level
 * @netdev: network interface device structure
 *
 * Returns current debug message level.
 **/
static u32 i40evf_get_msglevel(struct net_device *netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

/**
 * i40evf_get_msglevel - Set debug message level
 * @netdev: network interface device structure
 * @data: message level
 *
 * Set current debug message level. Higher values cause the driver to
 * be noisier.
 **/
static void i40evf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

/**
 * i40evf_get_drvinto - Get driver info
 * @netdev: network interface device structure
 * @drvinfo: ethool driver info structure
 *
 * Returns information about the driver and device for display to the user.
 **/
static void i40evf_get_drvinfo(struct net_device *netdev,
			       struct ethtool_drvinfo *drvinfo)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, i40evf_driver_name, 32);
	strlcpy(drvinfo->version, i40evf_driver_version, 32);

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
}

/**
 * i40evf_get_ringparam - Get ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 *
 * Returns current ring parameters. TX and RX rings are reported separately,
 * but the number of rings is not reported.
 **/
static void i40evf_get_ringparam(struct net_device *netdev,
				  struct ethtool_ringparam *ring)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_ring *tx_ring = adapter->tx_rings[0];
	struct i40e_ring *rx_ring = adapter->rx_rings[0];

	ring->rx_max_pending = I40EVF_MAX_RXD;
	ring->tx_max_pending = I40EVF_MAX_TXD;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
}

/**
 * i40evf_set_ringparam - Set ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 *
 * Sets ring parameters. TX and RX rings are controlled separately, but the
 * number of rings is not specified, so all rings get the same settings.
 **/
static int i40evf_set_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;
	int i;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_tx_count = clamp_t(u32, ring->tx_pending,
			       I40EVF_MIN_TXD,
			       I40EVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, I40EVF_REQ_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending,
			       I40EVF_MIN_RXD,
			       I40EVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, I40EVF_REQ_DESCRIPTOR_MULTIPLE);

	/* if nothing to do return success */
	if ((new_tx_count == adapter->tx_rings[0]->count) &&
	    (new_rx_count == adapter->rx_rings[0]->count))
		return 0;

	for (i = 0; i < adapter->vsi_res->num_queue_pairs; i++) {
		adapter->tx_rings[0]->count = new_tx_count;
		adapter->rx_rings[0]->count = new_rx_count;
	}

	if (netif_running(netdev))
		i40evf_reinit_locked(adapter);
	return 0;
}

/**
 * i40evf_get_coalesce - Get interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Returns current coalescing settings. This is referred to elsewhere in the
 * driver as Interrupt Throttle Rate, as this is how the hardware describes
 * this functionality.
 **/
static int i40evf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_vsi *vsi = &adapter->vsi;

	ec->tx_max_coalesced_frames = vsi->work_limit;
	ec->rx_max_coalesced_frames = vsi->work_limit;

	if (ITR_IS_DYNAMIC(vsi->rx_itr_setting))
		ec->rx_coalesce_usecs = 1;
	else
		ec->rx_coalesce_usecs = vsi->rx_itr_setting;

	if (ITR_IS_DYNAMIC(vsi->tx_itr_setting))
		ec->tx_coalesce_usecs = 1;
	else
		ec->tx_coalesce_usecs = vsi->tx_itr_setting;

	return 0;
}

/**
 * i40evf_set_coalesce - Set interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Change current coalescing settings.
 **/
static int i40evf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_hw *hw = &adapter->hw;
	struct i40e_vsi *vsi = &adapter->vsi;
	struct i40e_q_vector *q_vector;
	int i;

	if (ec->tx_max_coalesced_frames || ec->rx_max_coalesced_frames)
		vsi->work_limit = ec->tx_max_coalesced_frames;

	switch (ec->rx_coalesce_usecs) {
	case 0:
		vsi->rx_itr_setting = 0;
		break;
	case 1:
		vsi->rx_itr_setting = (I40E_ITR_DYNAMIC
				       | ITR_REG_TO_USEC(I40E_ITR_RX_DEF));
		break;
	default:
		if ((ec->rx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		    (ec->rx_coalesce_usecs > (I40E_MAX_ITR << 1)))
			return -EINVAL;
		vsi->rx_itr_setting = ec->rx_coalesce_usecs;
		break;
	}

	switch (ec->tx_coalesce_usecs) {
	case 0:
		vsi->tx_itr_setting = 0;
		break;
	case 1:
		vsi->tx_itr_setting = (I40E_ITR_DYNAMIC
				       | ITR_REG_TO_USEC(I40E_ITR_TX_DEF));
		break;
	default:
		if ((ec->tx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		    (ec->tx_coalesce_usecs > (I40E_MAX_ITR << 1)))
			return -EINVAL;
		vsi->tx_itr_setting = ec->tx_coalesce_usecs;
		break;
	}

	for (i = 0; i < adapter->num_msix_vectors - NONQ_VECS; i++) {
		q_vector = adapter->q_vector[i];
		q_vector->rx.itr = ITR_TO_REG(vsi->rx_itr_setting);
		wr32(hw, I40E_VFINT_ITRN1(0, i), q_vector->rx.itr);
		q_vector->tx.itr = ITR_TO_REG(vsi->tx_itr_setting);
		wr32(hw, I40E_VFINT_ITRN1(1, i), q_vector->tx.itr);
		i40e_flush(hw);
	}

	return 0;
}

static struct ethtool_ops i40evf_ethtool_ops = {
	.get_settings		= i40evf_get_settings,
	.get_drvinfo		= i40evf_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= i40evf_get_ringparam,
	.set_ringparam		= i40evf_set_ringparam,
	.get_strings		= i40evf_get_strings,
	.get_ethtool_stats	= i40evf_get_ethtool_stats,
	.get_sset_count		= i40evf_get_sset_count,
	.get_msglevel		= i40evf_get_msglevel,
	.set_msglevel		= i40evf_set_msglevel,
	.get_coalesce		= i40evf_get_coalesce,
	.set_coalesce		= i40evf_set_coalesce,
};

/**
 * i40evf_set_ethtool_ops - Initialize ethtool ops struct
 * @netdev: network interface device structure
 *
 * Sets ethtool ops struct in our netdev so that ethtool can call
 * our functions.
 **/
void i40evf_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &i40evf_ethtool_ops);
}
