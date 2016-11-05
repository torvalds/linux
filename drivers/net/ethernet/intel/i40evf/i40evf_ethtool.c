/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
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
	I40EVF_STAT("rx_unknown_protocol", current_stats.rx_unknown_protocol),
	I40EVF_STAT("tx_bytes", current_stats.tx_bytes),
	I40EVF_STAT("tx_unicast", current_stats.tx_unicast),
	I40EVF_STAT("tx_multicast", current_stats.tx_multicast),
	I40EVF_STAT("tx_broadcast", current_stats.tx_broadcast),
	I40EVF_STAT("tx_discards", current_stats.tx_discards),
	I40EVF_STAT("tx_errors", current_stats.tx_errors),
};

#define I40EVF_GLOBAL_STATS_LEN ARRAY_SIZE(i40evf_gstrings_stats)
#define I40EVF_QUEUE_STATS_LEN(_dev) \
	(((struct i40evf_adapter *)\
		netdev_priv(_dev))->num_active_queues \
		  * 2 * (sizeof(struct i40e_queue_stats) / sizeof(u64)))
#define I40EVF_STATS_LEN(_dev) \
	(I40EVF_GLOBAL_STATS_LEN + I40EVF_QUEUE_STATS_LEN(_dev))

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
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	ecmd->supported = 0;
	ecmd->autoneg = AUTONEG_DISABLE;
	ecmd->transceiver = XCVR_DUMMY1;
	ecmd->port = PORT_NONE;
	/* Set speed and duplex */
	switch (adapter->link_speed) {
	case I40E_LINK_SPEED_40GB:
		ethtool_cmd_speed_set(ecmd, SPEED_40000);
		break;
	case I40E_LINK_SPEED_20GB:
		ethtool_cmd_speed_set(ecmd, SPEED_20000);
		break;
	case I40E_LINK_SPEED_10GB:
		ethtool_cmd_speed_set(ecmd, SPEED_10000);
		break;
	case I40E_LINK_SPEED_1GB:
		ethtool_cmd_speed_set(ecmd, SPEED_1000);
		break;
	case I40E_LINK_SPEED_100MB:
		ethtool_cmd_speed_set(ecmd, SPEED_100);
		break;
	default:
		break;
	}
	ecmd->duplex = DUPLEX_FULL;

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
		return I40EVF_STATS_LEN(netdev);
	else
		return -EINVAL;
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
	for (j = 0; j < adapter->num_active_queues; j++) {
		data[i++] = adapter->tx_rings[j].stats.packets;
		data[i++] = adapter->tx_rings[j].stats.bytes;
	}
	for (j = 0; j < adapter->num_active_queues; j++) {
		data[i++] = adapter->rx_rings[j].stats.packets;
		data[i++] = adapter->rx_rings[j].stats.bytes;
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
		for (i = 0; i < adapter->num_active_queues; i++) {
			snprintf(p, ETH_GSTRING_LEN, "tx-%u.packets", i);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "tx-%u.bytes", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < adapter->num_active_queues; i++) {
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
 * i40evf_set_msglevel - Set debug message level
 * @netdev: network interface device structure
 * @data: message level
 *
 * Set current debug message level. Higher values cause the driver to
 * be noisier.
 **/
static void i40evf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	if (I40E_DEBUG_USER & data)
		adapter->hw.debug_mask = data;
	adapter->msg_enable = data;
}

/**
 * i40evf_get_drvinfo - Get driver info
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
	strlcpy(drvinfo->fw_version, "N/A", 4);
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

	ring->rx_max_pending = I40EVF_MAX_RXD;
	ring->tx_max_pending = I40EVF_MAX_TXD;
	ring->rx_pending = adapter->rx_desc_count;
	ring->tx_pending = adapter->tx_desc_count;
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
	if ((new_tx_count == adapter->tx_desc_count) &&
	    (new_rx_count == adapter->rx_desc_count))
		return 0;

	adapter->tx_desc_count = new_tx_count;
	adapter->rx_desc_count = new_rx_count;

	if (netif_running(netdev)) {
		adapter->flags |= I40EVF_FLAG_RESET_NEEDED;
		schedule_work(&adapter->reset_task);
	}

	return 0;
}

/**
 * __i40evf_get_coalesce - get per-queue coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 * @queue: which queue to pick
 *
 * Gets the per-queue settings for coalescence. Specifically Rx and Tx usecs
 * are per queue. If queue is <0 then we default to queue 0 as the
 * representative value.
 **/
static int __i40evf_get_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *ec,
				 int queue)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_vsi *vsi = &adapter->vsi;
	struct i40e_ring *rx_ring, *tx_ring;

	ec->tx_max_coalesced_frames = vsi->work_limit;
	ec->rx_max_coalesced_frames = vsi->work_limit;

	/* Rx and Tx usecs per queue value. If user doesn't specify the
	 * queue, return queue 0's value to represent.
	 */
	if (queue < 0)
		queue = 0;
	else if (queue >= adapter->num_active_queues)
		return -EINVAL;

	rx_ring = &adapter->rx_rings[queue];
	tx_ring = &adapter->tx_rings[queue];

	if (ITR_IS_DYNAMIC(rx_ring->rx_itr_setting))
		ec->use_adaptive_rx_coalesce = 1;

	if (ITR_IS_DYNAMIC(tx_ring->tx_itr_setting))
		ec->use_adaptive_tx_coalesce = 1;

	ec->rx_coalesce_usecs = rx_ring->rx_itr_setting & ~I40E_ITR_DYNAMIC;
	ec->tx_coalesce_usecs = tx_ring->tx_itr_setting & ~I40E_ITR_DYNAMIC;

	return 0;
}

/**
 * i40evf_get_coalesce - Get interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Returns current coalescing settings. This is referred to elsewhere in the
 * driver as Interrupt Throttle Rate, as this is how the hardware describes
 * this functionality. Note that if per-queue settings have been modified this
 * only represents the settings of queue 0.
 **/
static int i40evf_get_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec)
{
	return __i40evf_get_coalesce(netdev, ec, -1);
}

/**
 * i40evf_get_per_queue_coalesce - get coalesce values for specific queue
 * @netdev: netdev to read
 * @ec: coalesce settings from ethtool
 * @queue: the queue to read
 *
 * Read specific queue's coalesce settings.
 **/
static int i40evf_get_per_queue_coalesce(struct net_device *netdev,
					 u32 queue,
					 struct ethtool_coalesce *ec)
{
	return __i40evf_get_coalesce(netdev, ec, queue);
}

/**
 * i40evf_set_itr_per_queue - set ITR values for specific queue
 * @vsi: the VSI to set values for
 * @ec: coalesce settings from ethtool
 * @queue: the queue to modify
 *
 * Change the ITR settings for a specific queue.
 **/
static void i40evf_set_itr_per_queue(struct i40evf_adapter *adapter,
				     struct ethtool_coalesce *ec,
				     int queue)
{
	struct i40e_vsi *vsi = &adapter->vsi;
	struct i40e_hw *hw = &adapter->hw;
	struct i40e_q_vector *q_vector;
	u16 vector;

	adapter->rx_rings[queue].rx_itr_setting = ec->rx_coalesce_usecs;
	adapter->tx_rings[queue].tx_itr_setting = ec->tx_coalesce_usecs;

	if (ec->use_adaptive_rx_coalesce)
		adapter->rx_rings[queue].rx_itr_setting |= I40E_ITR_DYNAMIC;
	else
		adapter->rx_rings[queue].rx_itr_setting &= ~I40E_ITR_DYNAMIC;

	if (ec->use_adaptive_tx_coalesce)
		adapter->tx_rings[queue].tx_itr_setting |= I40E_ITR_DYNAMIC;
	else
		adapter->tx_rings[queue].tx_itr_setting &= ~I40E_ITR_DYNAMIC;

	q_vector = adapter->rx_rings[queue].q_vector;
	q_vector->rx.itr = ITR_TO_REG(adapter->rx_rings[queue].rx_itr_setting);
	vector = vsi->base_vector + q_vector->v_idx;
	wr32(hw, I40E_VFINT_ITRN1(I40E_RX_ITR, vector - 1), q_vector->rx.itr);

	q_vector = adapter->tx_rings[queue].q_vector;
	q_vector->tx.itr = ITR_TO_REG(adapter->tx_rings[queue].tx_itr_setting);
	vector = vsi->base_vector + q_vector->v_idx;
	wr32(hw, I40E_VFINT_ITRN1(I40E_TX_ITR, vector - 1), q_vector->tx.itr);

	i40e_flush(hw);
}

/**
 * __i40evf_set_coalesce - set coalesce settings for particular queue
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 * @queue: the queue to change
 *
 * Sets the coalesce settings for a particular queue.
 **/
static int __i40evf_set_coalesce(struct net_device *netdev,
				 struct ethtool_coalesce *ec,
				 int queue)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	struct i40e_vsi *vsi = &adapter->vsi;
	int i;

	if (ec->tx_max_coalesced_frames_irq || ec->rx_max_coalesced_frames_irq)
		vsi->work_limit = ec->tx_max_coalesced_frames_irq;

	if (ec->rx_coalesce_usecs == 0) {
		if (ec->use_adaptive_rx_coalesce)
			netif_info(adapter, drv, netdev, "rx-usecs=0, need to disable adaptive-rx for a complete disable\n");
	} else if ((ec->rx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		   (ec->rx_coalesce_usecs > (I40E_MAX_ITR << 1))) {
		netif_info(adapter, drv, netdev, "Invalid value, rx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	else
	if (ec->tx_coalesce_usecs == 0) {
		if (ec->use_adaptive_tx_coalesce)
			netif_info(adapter, drv, netdev, "tx-usecs=0, need to disable adaptive-tx for a complete disable\n");
	} else if ((ec->tx_coalesce_usecs < (I40E_MIN_ITR << 1)) ||
		   (ec->tx_coalesce_usecs > (I40E_MAX_ITR << 1))) {
		netif_info(adapter, drv, netdev, "Invalid value, tx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	/* Rx and Tx usecs has per queue value. If user doesn't specify the
	 * queue, apply to all queues.
	 */
	if (queue < 0) {
		for (i = 0; i < adapter->num_active_queues; i++)
			i40evf_set_itr_per_queue(adapter, ec, i);
	} else if (queue < adapter->num_active_queues) {
		i40evf_set_itr_per_queue(adapter, ec, queue);
	} else {
		netif_info(adapter, drv, netdev, "Invalid queue value, queue range is 0 - %d\n",
			   adapter->num_active_queues - 1);
		return -EINVAL;
	}

	return 0;
}

/**
 * i40evf_set_coalesce - Set interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Change current coalescing settings for every queue.
 **/
static int i40evf_set_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec)
{
	return __i40evf_set_coalesce(netdev, ec, -1);
}

/**
 * i40evf_set_per_queue_coalesce - set specific queue's coalesce settings
 * @netdev: the netdev to change
 * @ec: ethtool's coalesce settings
 * @queue: the queue to modify
 *
 * Modifies a specific queue's coalesce settings.
 */
static int i40evf_set_per_queue_coalesce(struct net_device *netdev,
					 u32 queue,
					 struct ethtool_coalesce *ec)
{
	return __i40evf_set_coalesce(netdev, ec, queue);
}

/**
 * i40evf_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the command is supported.
 **/
static int i40evf_get_rxnfc(struct net_device *netdev,
			    struct ethtool_rxnfc *cmd,
			    u32 *rule_locs)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = adapter->num_active_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		netdev_info(netdev,
			    "RSS hash info is not available to vf, use pf.\n");
		break;
	default:
		break;
	}

	return ret;
}
/**
 * i40evf_get_channels: get the number of channels supported by the device
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * For the purposes of our device, we only use combined channels, i.e. a tx/rx
 * queue pair. Report one extra channel to match our "other" MSI-X vector.
 **/
static void i40evf_get_channels(struct net_device *netdev,
				struct ethtool_channels *ch)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	/* Report maximum channels */
	ch->max_combined = adapter->num_active_queues;

	ch->max_other = NONQ_VECS;
	ch->other_count = NONQ_VECS;

	ch->combined_count = adapter->num_active_queues;
}

/**
 * i40evf_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40evf_get_rxfh_key_size(struct net_device *netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_key_size;
}

/**
 * i40evf_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40evf_get_rxfh_indir_size(struct net_device *netdev)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_lut_size;
}

/**
 * i40evf_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 *
 * Reads the indirection table directly from the hardware. Always returns 0.
 **/
static int i40evf_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			   u8 *hfunc)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	u16 i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;
	if (!indir)
		return 0;

	memcpy(key, adapter->rss_key, adapter->rss_key_size);

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	for (i = 0; i < adapter->rss_lut_size; i++)
		indir[i] = (u32)adapter->rss_lut[i];

	return 0;
}

/**
 * i40evf_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 *
 * Returns -EINVAL if the table specifies an inavlid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int i40evf_set_rxfh(struct net_device *netdev, const u32 *indir,
			   const u8 *key, const u8 hfunc)
{
	struct i40evf_adapter *adapter = netdev_priv(netdev);
	u16 i;

	/* We do not allow change in unsupported parameters */
	if (key ||
	    (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!indir)
		return 0;

	if (key) {
		memcpy(adapter->rss_key, key, adapter->rss_key_size);
	}

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	for (i = 0; i < adapter->rss_lut_size; i++)
		adapter->rss_lut[i] = (u8)(indir[i]);

	return i40evf_config_rss(adapter);
}

static const struct ethtool_ops i40evf_ethtool_ops = {
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
	.get_per_queue_coalesce = i40evf_get_per_queue_coalesce,
	.set_per_queue_coalesce = i40evf_set_per_queue_coalesce,
	.get_rxnfc		= i40evf_get_rxnfc,
	.get_rxfh_indir_size	= i40evf_get_rxfh_indir_size,
	.get_rxfh		= i40evf_get_rxfh,
	.set_rxfh		= i40evf_set_rxfh,
	.get_channels		= i40evf_get_channels,
	.get_rxfh_key_size	= i40evf_get_rxfh_key_size,
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
	netdev->ethtool_ops = &i40evf_ethtool_ops;
}
