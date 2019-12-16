// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

/* ethtool support for iavf */
#include "iavf.h"

#include <linux/uaccess.h>

/* ethtool statistics helpers */

/**
 * struct iavf_stats - definition for an ethtool statistic
 * @stat_string: statistic name to display in ethtool -S output
 * @sizeof_stat: the sizeof() the stat, must be no greater than sizeof(u64)
 * @stat_offset: offsetof() the stat from a base pointer
 *
 * This structure defines a statistic to be added to the ethtool stats buffer.
 * It defines a statistic as offset from a common base pointer. Stats should
 * be defined in constant arrays using the IAVF_STAT macro, with every element
 * of the array using the same _type for calculating the sizeof_stat and
 * stat_offset.
 *
 * The @sizeof_stat is expected to be sizeof(u8), sizeof(u16), sizeof(u32) or
 * sizeof(u64). Other sizes are not expected and will produce a WARN_ONCE from
 * the iavf_add_ethtool_stat() helper function.
 *
 * The @stat_string is interpreted as a format string, allowing formatted
 * values to be inserted while looping over multiple structures for a given
 * statistics array. Thus, every statistic string in an array should have the
 * same type and number of format specifiers, to be formatted by variadic
 * arguments to the iavf_add_stat_string() helper function.
 **/
struct iavf_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

/* Helper macro to define an iavf_stat structure with proper size and type.
 * Use this when defining constant statistics arrays. Note that @_type expects
 * only a type name and is used multiple times.
 */
#define IAVF_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = sizeof_field(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

/* Helper macro for defining some statistics related to queues */
#define IAVF_QUEUE_STAT(_name, _stat) \
	IAVF_STAT(struct iavf_ring, _name, _stat)

/* Stats associated with a Tx or Rx ring */
static const struct iavf_stats iavf_gstrings_queue_stats[] = {
	IAVF_QUEUE_STAT("%s-%u.packets", stats.packets),
	IAVF_QUEUE_STAT("%s-%u.bytes", stats.bytes),
};

/**
 * iavf_add_one_ethtool_stat - copy the stat into the supplied buffer
 * @data: location to store the stat value
 * @pointer: basis for where to copy from
 * @stat: the stat definition
 *
 * Copies the stat data defined by the pointer and stat structure pair into
 * the memory supplied as data. Used to implement iavf_add_ethtool_stats and
 * iavf_add_queue_stats. If the pointer is null, data will be zero'd.
 */
static void
iavf_add_one_ethtool_stat(u64 *data, void *pointer,
			  const struct iavf_stats *stat)
{
	char *p;

	if (!pointer) {
		/* ensure that the ethtool data buffer is zero'd for any stats
		 * which don't have a valid pointer.
		 */
		*data = 0;
		return;
	}

	p = (char *)pointer + stat->stat_offset;
	switch (stat->sizeof_stat) {
	case sizeof(u64):
		*data = *((u64 *)p);
		break;
	case sizeof(u32):
		*data = *((u32 *)p);
		break;
	case sizeof(u16):
		*data = *((u16 *)p);
		break;
	case sizeof(u8):
		*data = *((u8 *)p);
		break;
	default:
		WARN_ONCE(1, "unexpected stat size for %s",
			  stat->stat_string);
		*data = 0;
	}
}

/**
 * __iavf_add_ethtool_stats - copy stats into the ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location to copy stats from
 * @stats: array of stats to copy
 * @size: the size of the stats definition
 *
 * Copy the stats defined by the stats array using the pointer as a base into
 * the data buffer supplied by ethtool. Updates the data pointer to point to
 * the next empty location for successive calls to __iavf_add_ethtool_stats.
 * If pointer is null, set the data values to zero and update the pointer to
 * skip these stats.
 **/
static void
__iavf_add_ethtool_stats(u64 **data, void *pointer,
			 const struct iavf_stats stats[],
			 const unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		iavf_add_one_ethtool_stat((*data)++, pointer, &stats[i]);
}

/**
 * iavf_add_ethtool_stats - copy stats into ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location where stats are stored
 * @stats: static const array of stat definitions
 *
 * Macro to ease the use of __iavf_add_ethtool_stats by taking a static
 * constant stats array and passing the ARRAY_SIZE(). This avoids typos by
 * ensuring that we pass the size associated with the given stats array.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided.
 **/
#define iavf_add_ethtool_stats(data, pointer, stats) \
	__iavf_add_ethtool_stats(data, pointer, stats, ARRAY_SIZE(stats))

/**
 * iavf_add_queue_stats - copy queue statistics into supplied buffer
 * @data: ethtool stats buffer
 * @ring: the ring to copy
 *
 * Queue statistics must be copied while protected by
 * u64_stats_fetch_begin_irq, so we can't directly use iavf_add_ethtool_stats.
 * Assumes that queue stats are defined in iavf_gstrings_queue_stats. If the
 * ring pointer is null, zero out the queue stat values and update the data
 * pointer. Otherwise safely copy the stats from the ring into the supplied
 * buffer and update the data pointer when finished.
 *
 * This function expects to be called while under rcu_read_lock().
 **/
static void
iavf_add_queue_stats(u64 **data, struct iavf_ring *ring)
{
	const unsigned int size = ARRAY_SIZE(iavf_gstrings_queue_stats);
	const struct iavf_stats *stats = iavf_gstrings_queue_stats;
	unsigned int start;
	unsigned int i;

	/* To avoid invalid statistics values, ensure that we keep retrying
	 * the copy until we get a consistent value according to
	 * u64_stats_fetch_retry_irq. But first, make sure our ring is
	 * non-null before attempting to access its syncp.
	 */
	do {
		start = !ring ? 0 : u64_stats_fetch_begin_irq(&ring->syncp);
		for (i = 0; i < size; i++)
			iavf_add_one_ethtool_stat(&(*data)[i], ring, &stats[i]);
	} while (ring && u64_stats_fetch_retry_irq(&ring->syncp, start));

	/* Once we successfully copy the stats in, update the data pointer */
	*data += size;
}

/**
 * __iavf_add_stat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 * @size: size of the stats array
 *
 * Format and copy the strings described by stats into the buffer pointed at
 * by p.
 **/
static void __iavf_add_stat_strings(u8 **p, const struct iavf_stats stats[],
				    const unsigned int size, ...)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		va_list args;

		va_start(args, size);
		vsnprintf(*p, ETH_GSTRING_LEN, stats[i].stat_string, args);
		*p += ETH_GSTRING_LEN;
		va_end(args);
	}
}

/**
 * iavf_add_stat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 *
 * Format and copy the strings described by the const static stats value into
 * the buffer pointed at by p.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided. Additionally, stats must be an array such that
 * ARRAY_SIZE can be called on it.
 **/
#define iavf_add_stat_strings(p, stats, ...) \
	__iavf_add_stat_strings(p, stats, ARRAY_SIZE(stats), ## __VA_ARGS__)

#define VF_STAT(_name, _stat) \
	IAVF_STAT(struct iavf_adapter, _name, _stat)

static const struct iavf_stats iavf_gstrings_stats[] = {
	VF_STAT("rx_bytes", current_stats.rx_bytes),
	VF_STAT("rx_unicast", current_stats.rx_unicast),
	VF_STAT("rx_multicast", current_stats.rx_multicast),
	VF_STAT("rx_broadcast", current_stats.rx_broadcast),
	VF_STAT("rx_discards", current_stats.rx_discards),
	VF_STAT("rx_unknown_protocol", current_stats.rx_unknown_protocol),
	VF_STAT("tx_bytes", current_stats.tx_bytes),
	VF_STAT("tx_unicast", current_stats.tx_unicast),
	VF_STAT("tx_multicast", current_stats.tx_multicast),
	VF_STAT("tx_broadcast", current_stats.tx_broadcast),
	VF_STAT("tx_discards", current_stats.tx_discards),
	VF_STAT("tx_errors", current_stats.tx_errors),
};

#define IAVF_STATS_LEN	ARRAY_SIZE(iavf_gstrings_stats)

#define IAVF_QUEUE_STATS_LEN	ARRAY_SIZE(iavf_gstrings_queue_stats)

/* For now we have one and only one private flag and it is only defined
 * when we have support for the SKIP_CPU_SYNC DMA attribute.  Instead
 * of leaving all this code sitting around empty we will strip it unless
 * our one private flag is actually available.
 */
struct iavf_priv_flags {
	char flag_string[ETH_GSTRING_LEN];
	u32 flag;
	bool read_only;
};

#define IAVF_PRIV_FLAG(_name, _flag, _read_only) { \
	.flag_string = _name, \
	.flag = _flag, \
	.read_only = _read_only, \
}

static const struct iavf_priv_flags iavf_gstrings_priv_flags[] = {
	IAVF_PRIV_FLAG("legacy-rx", IAVF_FLAG_LEGACY_RX, 0),
};

#define IAVF_PRIV_FLAGS_STR_LEN ARRAY_SIZE(iavf_gstrings_priv_flags)

/**
 * iavf_get_link_ksettings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @cmd: ethtool command
 *
 * Reports speed/duplex settings. Because this is a VF, we don't know what
 * kind of link we really have, so we fake it.
 **/
static int iavf_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *cmd)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = PORT_NONE;
	/* Set speed and duplex */
	switch (adapter->link_speed) {
	case IAVF_LINK_SPEED_40GB:
		cmd->base.speed = SPEED_40000;
		break;
	case IAVF_LINK_SPEED_25GB:
#ifdef SPEED_25000
		cmd->base.speed = SPEED_25000;
#else
		netdev_info(netdev,
			    "Speed is 25G, display not supported by this version of ethtool.\n");
#endif
		break;
	case IAVF_LINK_SPEED_20GB:
		cmd->base.speed = SPEED_20000;
		break;
	case IAVF_LINK_SPEED_10GB:
		cmd->base.speed = SPEED_10000;
		break;
	case IAVF_LINK_SPEED_1GB:
		cmd->base.speed = SPEED_1000;
		break;
	case IAVF_LINK_SPEED_100MB:
		cmd->base.speed = SPEED_100;
		break;
	default:
		break;
	}
	cmd->base.duplex = DUPLEX_FULL;

	return 0;
}

/**
 * iavf_get_sset_count - Get length of string set
 * @netdev: network interface device structure
 * @sset: id of string set
 *
 * Reports size of various string tables.
 **/
static int iavf_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset == ETH_SS_STATS)
		return IAVF_STATS_LEN +
			(IAVF_QUEUE_STATS_LEN * 2 * IAVF_MAX_REQ_QUEUES);
	else if (sset == ETH_SS_PRIV_FLAGS)
		return IAVF_PRIV_FLAGS_STR_LEN;
	else
		return -EINVAL;
}

/**
 * iavf_get_ethtool_stats - report device statistics
 * @netdev: network interface device structure
 * @stats: ethtool statistics structure
 * @data: pointer to data buffer
 *
 * All statistics are added to the data buffer as an array of u64.
 **/
static void iavf_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	unsigned int i;

	iavf_add_ethtool_stats(&data, adapter, iavf_gstrings_stats);

	rcu_read_lock();
	for (i = 0; i < IAVF_MAX_REQ_QUEUES; i++) {
		struct iavf_ring *ring;

		/* Avoid accessing un-allocated queues */
		ring = (i < adapter->num_active_queues ?
			&adapter->tx_rings[i] : NULL);
		iavf_add_queue_stats(&data, ring);

		/* Avoid accessing un-allocated queues */
		ring = (i < adapter->num_active_queues ?
			&adapter->rx_rings[i] : NULL);
		iavf_add_queue_stats(&data, ring);
	}
	rcu_read_unlock();
}

/**
 * iavf_get_priv_flag_strings - Get private flag strings
 * @netdev: network interface device structure
 * @data: buffer for string data
 *
 * Builds the private flags string table
 **/
static void iavf_get_priv_flag_strings(struct net_device *netdev, u8 *data)
{
	unsigned int i;

	for (i = 0; i < IAVF_PRIV_FLAGS_STR_LEN; i++) {
		snprintf(data, ETH_GSTRING_LEN, "%s",
			 iavf_gstrings_priv_flags[i].flag_string);
		data += ETH_GSTRING_LEN;
	}
}

/**
 * iavf_get_stat_strings - Get stat strings
 * @netdev: network interface device structure
 * @data: buffer for string data
 *
 * Builds the statistics string table
 **/
static void iavf_get_stat_strings(struct net_device *netdev, u8 *data)
{
	unsigned int i;

	iavf_add_stat_strings(&data, iavf_gstrings_stats);

	/* Queues are always allocated in pairs, so we just use num_tx_queues
	 * for both Tx and Rx queues.
	 */
	for (i = 0; i < netdev->num_tx_queues; i++) {
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats,
				      "tx", i);
		iavf_add_stat_strings(&data, iavf_gstrings_queue_stats,
				      "rx", i);
	}
}

/**
 * iavf_get_strings - Get string set
 * @netdev: network interface device structure
 * @sset: id of string set
 * @data: buffer for string data
 *
 * Builds string tables for various string sets
 **/
static void iavf_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	switch (sset) {
	case ETH_SS_STATS:
		iavf_get_stat_strings(netdev, data);
		break;
	case ETH_SS_PRIV_FLAGS:
		iavf_get_priv_flag_strings(netdev, data);
		break;
	default:
		break;
	}
}

/**
 * iavf_get_priv_flags - report device private flags
 * @netdev: network interface device structure
 *
 * The get string set count and the string set should be matched for each
 * flag returned.  Add new strings for each flag to the iavf_gstrings_priv_flags
 * array.
 *
 * Returns a u32 bitmap of flags.
 **/
static u32 iavf_get_priv_flags(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 i, ret_flags = 0;

	for (i = 0; i < IAVF_PRIV_FLAGS_STR_LEN; i++) {
		const struct iavf_priv_flags *priv_flags;

		priv_flags = &iavf_gstrings_priv_flags[i];

		if (priv_flags->flag & adapter->flags)
			ret_flags |= BIT(i);
	}

	return ret_flags;
}

/**
 * iavf_set_priv_flags - set private flags
 * @netdev: network interface device structure
 * @flags: bit flags to be set
 **/
static int iavf_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 orig_flags, new_flags, changed_flags;
	u32 i;

	orig_flags = READ_ONCE(adapter->flags);
	new_flags = orig_flags;

	for (i = 0; i < IAVF_PRIV_FLAGS_STR_LEN; i++) {
		const struct iavf_priv_flags *priv_flags;

		priv_flags = &iavf_gstrings_priv_flags[i];

		if (flags & BIT(i))
			new_flags |= priv_flags->flag;
		else
			new_flags &= ~(priv_flags->flag);

		if (priv_flags->read_only &&
		    ((orig_flags ^ new_flags) & ~BIT(i)))
			return -EOPNOTSUPP;
	}

	/* Before we finalize any flag changes, any checks which we need to
	 * perform to determine if the new flags will be supported should go
	 * here...
	 */

	/* Compare and exchange the new flags into place. If we failed, that
	 * is if cmpxchg returns anything but the old value, this means
	 * something else must have modified the flags variable since we
	 * copied it. We'll just punt with an error and log something in the
	 * message buffer.
	 */
	if (cmpxchg(&adapter->flags, orig_flags, new_flags) != orig_flags) {
		dev_warn(&adapter->pdev->dev,
			 "Unable to update adapter->flags as it was modified by another thread...\n");
		return -EAGAIN;
	}

	changed_flags = orig_flags ^ new_flags;

	/* Process any additional changes needed as a result of flag changes.
	 * The changed_flags value reflects the list of bits that were changed
	 * in the code above.
	 */

	/* issue a reset to force legacy-rx change to take effect */
	if (changed_flags & IAVF_FLAG_LEGACY_RX) {
		if (netif_running(netdev)) {
			adapter->flags |= IAVF_FLAG_RESET_NEEDED;
			queue_work(iavf_wq, &adapter->reset_task);
		}
	}

	return 0;
}

/**
 * iavf_get_msglevel - Get debug message level
 * @netdev: network interface device structure
 *
 * Returns current debug message level.
 **/
static u32 iavf_get_msglevel(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

/**
 * iavf_set_msglevel - Set debug message level
 * @netdev: network interface device structure
 * @data: message level
 *
 * Set current debug message level. Higher values cause the driver to
 * be noisier.
 **/
static void iavf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	if (IAVF_DEBUG_USER & data)
		adapter->hw.debug_mask = data;
	adapter->msg_enable = data;
}

/**
 * iavf_get_drvinfo - Get driver info
 * @netdev: network interface device structure
 * @drvinfo: ethool driver info structure
 *
 * Returns information about the driver and device for display to the user.
 **/
static void iavf_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, iavf_driver_name, 32);
	strlcpy(drvinfo->version, iavf_driver_version, 32);
	strlcpy(drvinfo->fw_version, "N/A", 4);
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->n_priv_flags = IAVF_PRIV_FLAGS_STR_LEN;
}

/**
 * iavf_get_ringparam - Get ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 *
 * Returns current ring parameters. TX and RX rings are reported separately,
 * but the number of rings is not reported.
 **/
static void iavf_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = IAVF_MAX_RXD;
	ring->tx_max_pending = IAVF_MAX_TXD;
	ring->rx_pending = adapter->rx_desc_count;
	ring->tx_pending = adapter->tx_desc_count;
}

/**
 * iavf_set_ringparam - Set ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 *
 * Sets ring parameters. TX and RX rings are controlled separately, but the
 * number of rings is not specified, so all rings get the same settings.
 **/
static int iavf_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_tx_count = clamp_t(u32, ring->tx_pending,
			       IAVF_MIN_TXD,
			       IAVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, IAVF_REQ_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending,
			       IAVF_MIN_RXD,
			       IAVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, IAVF_REQ_DESCRIPTOR_MULTIPLE);

	/* if nothing to do return success */
	if ((new_tx_count == adapter->tx_desc_count) &&
	    (new_rx_count == adapter->rx_desc_count))
		return 0;

	adapter->tx_desc_count = new_tx_count;
	adapter->rx_desc_count = new_rx_count;

	if (netif_running(netdev)) {
		adapter->flags |= IAVF_FLAG_RESET_NEEDED;
		queue_work(iavf_wq, &adapter->reset_task);
	}

	return 0;
}

/**
 * __iavf_get_coalesce - get per-queue coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 * @queue: which queue to pick
 *
 * Gets the per-queue settings for coalescence. Specifically Rx and Tx usecs
 * are per queue. If queue is <0 then we default to queue 0 as the
 * representative value.
 **/
static int __iavf_get_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec, int queue)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	struct iavf_vsi *vsi = &adapter->vsi;
	struct iavf_ring *rx_ring, *tx_ring;

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

	if (ITR_IS_DYNAMIC(rx_ring->itr_setting))
		ec->use_adaptive_rx_coalesce = 1;

	if (ITR_IS_DYNAMIC(tx_ring->itr_setting))
		ec->use_adaptive_tx_coalesce = 1;

	ec->rx_coalesce_usecs = rx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;
	ec->tx_coalesce_usecs = tx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	return 0;
}

/**
 * iavf_get_coalesce - Get interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Returns current coalescing settings. This is referred to elsewhere in the
 * driver as Interrupt Throttle Rate, as this is how the hardware describes
 * this functionality. Note that if per-queue settings have been modified this
 * only represents the settings of queue 0.
 **/
static int iavf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __iavf_get_coalesce(netdev, ec, -1);
}

/**
 * iavf_get_per_queue_coalesce - get coalesce values for specific queue
 * @netdev: netdev to read
 * @ec: coalesce settings from ethtool
 * @queue: the queue to read
 *
 * Read specific queue's coalesce settings.
 **/
static int iavf_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __iavf_get_coalesce(netdev, ec, queue);
}

/**
 * iavf_set_itr_per_queue - set ITR values for specific queue
 * @adapter: the VF adapter struct to set values for
 * @ec: coalesce settings from ethtool
 * @queue: the queue to modify
 *
 * Change the ITR settings for a specific queue.
 **/
static void iavf_set_itr_per_queue(struct iavf_adapter *adapter,
				   struct ethtool_coalesce *ec, int queue)
{
	struct iavf_ring *rx_ring = &adapter->rx_rings[queue];
	struct iavf_ring *tx_ring = &adapter->tx_rings[queue];
	struct iavf_q_vector *q_vector;

	rx_ring->itr_setting = ITR_REG_ALIGN(ec->rx_coalesce_usecs);
	tx_ring->itr_setting = ITR_REG_ALIGN(ec->tx_coalesce_usecs);

	rx_ring->itr_setting |= IAVF_ITR_DYNAMIC;
	if (!ec->use_adaptive_rx_coalesce)
		rx_ring->itr_setting ^= IAVF_ITR_DYNAMIC;

	tx_ring->itr_setting |= IAVF_ITR_DYNAMIC;
	if (!ec->use_adaptive_tx_coalesce)
		tx_ring->itr_setting ^= IAVF_ITR_DYNAMIC;

	q_vector = rx_ring->q_vector;
	q_vector->rx.target_itr = ITR_TO_REG(rx_ring->itr_setting);

	q_vector = tx_ring->q_vector;
	q_vector->tx.target_itr = ITR_TO_REG(tx_ring->itr_setting);

	/* The interrupt handler itself will take care of programming
	 * the Tx and Rx ITR values based on the values we have entered
	 * into the q_vector, no need to write the values now.
	 */
}

/**
 * __iavf_set_coalesce - set coalesce settings for particular queue
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 * @queue: the queue to change
 *
 * Sets the coalesce settings for a particular queue.
 **/
static int __iavf_set_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec, int queue)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	struct iavf_vsi *vsi = &adapter->vsi;
	int i;

	if (ec->tx_max_coalesced_frames_irq || ec->rx_max_coalesced_frames_irq)
		vsi->work_limit = ec->tx_max_coalesced_frames_irq;

	if (ec->rx_coalesce_usecs == 0) {
		if (ec->use_adaptive_rx_coalesce)
			netif_info(adapter, drv, netdev, "rx-usecs=0, need to disable adaptive-rx for a complete disable\n");
	} else if ((ec->rx_coalesce_usecs < IAVF_MIN_ITR) ||
		   (ec->rx_coalesce_usecs > IAVF_MAX_ITR)) {
		netif_info(adapter, drv, netdev, "Invalid value, rx-usecs range is 0-8160\n");
		return -EINVAL;
	} else if (ec->tx_coalesce_usecs == 0) {
		if (ec->use_adaptive_tx_coalesce)
			netif_info(adapter, drv, netdev, "tx-usecs=0, need to disable adaptive-tx for a complete disable\n");
	} else if ((ec->tx_coalesce_usecs < IAVF_MIN_ITR) ||
		   (ec->tx_coalesce_usecs > IAVF_MAX_ITR)) {
		netif_info(adapter, drv, netdev, "Invalid value, tx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	/* Rx and Tx usecs has per queue value. If user doesn't specify the
	 * queue, apply to all queues.
	 */
	if (queue < 0) {
		for (i = 0; i < adapter->num_active_queues; i++)
			iavf_set_itr_per_queue(adapter, ec, i);
	} else if (queue < adapter->num_active_queues) {
		iavf_set_itr_per_queue(adapter, ec, queue);
	} else {
		netif_info(adapter, drv, netdev, "Invalid queue value, queue range is 0 - %d\n",
			   adapter->num_active_queues - 1);
		return -EINVAL;
	}

	return 0;
}

/**
 * iavf_set_coalesce - Set interrupt coalescing settings
 * @netdev: network interface device structure
 * @ec: ethtool coalesce structure
 *
 * Change current coalescing settings for every queue.
 **/
static int iavf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __iavf_set_coalesce(netdev, ec, -1);
}

/**
 * iavf_set_per_queue_coalesce - set specific queue's coalesce settings
 * @netdev: the netdev to change
 * @ec: ethtool's coalesce settings
 * @queue: the queue to modify
 *
 * Modifies a specific queue's coalesce settings.
 */
static int iavf_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __iavf_set_coalesce(netdev, ec, queue);
}

/**
 * iavf_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: pointer to store rule locations
 *
 * Returns Success if the command is supported.
 **/
static int iavf_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
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
 * iavf_get_channels: get the number of channels supported by the device
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * For the purposes of our device, we only use combined channels, i.e. a tx/rx
 * queue pair. Report one extra channel to match our "other" MSI-X vector.
 **/
static void iavf_get_channels(struct net_device *netdev,
			      struct ethtool_channels *ch)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	/* Report maximum channels */
	ch->max_combined = IAVF_MAX_REQ_QUEUES;

	ch->max_other = NONQ_VECS;
	ch->other_count = NONQ_VECS;

	ch->combined_count = adapter->num_active_queues;
}

/**
 * iavf_set_channels: set the new channel count
 * @netdev: network interface device structure
 * @ch: channel information structure
 *
 * Negotiate a new number of channels with the PF then do a reset.  During
 * reset we'll realloc queues and fix the RSS table.  Returns 0 on success,
 * negative on failure.
 **/
static int iavf_set_channels(struct net_device *netdev,
			     struct ethtool_channels *ch)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int num_req = ch->combined_count;

	if (num_req != adapter->num_active_queues &&
	    !(adapter->vf_res->vf_cap_flags &
	      VIRTCHNL_VF_OFFLOAD_REQ_QUEUES)) {
		dev_info(&adapter->pdev->dev, "PF is not capable of queue negotiation.\n");
		return -EINVAL;
	}

	if ((adapter->vf_res->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_ADQ) &&
	    adapter->num_tc) {
		dev_info(&adapter->pdev->dev, "Cannot set channels since ADq is enabled.\n");
		return -EINVAL;
	}

	/* All of these should have already been checked by ethtool before this
	 * even gets to us, but just to be sure.
	 */
	if (num_req <= 0 || num_req > IAVF_MAX_REQ_QUEUES)
		return -EINVAL;

	if (ch->rx_count || ch->tx_count || ch->other_count != NONQ_VECS)
		return -EINVAL;

	adapter->num_req_queues = num_req;
	return iavf_request_queues(adapter, num_req);
}

/**
 * iavf_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 iavf_get_rxfh_key_size(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_key_size;
}

/**
 * iavf_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 iavf_get_rxfh_indir_size(struct net_device *netdev)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);

	return adapter->rss_lut_size;
}

/**
 * iavf_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function in use
 *
 * Reads the indirection table directly from the hardware. Always returns 0.
 **/
static int iavf_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
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
 * iavf_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function to use
 *
 * Returns -EINVAL if the table specifies an inavlid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int iavf_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

	/* We do not allow change in unsupported parameters */
	if (key ||
	    (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP))
		return -EOPNOTSUPP;
	if (!indir)
		return 0;

	if (key)
		memcpy(adapter->rss_key, key, adapter->rss_key_size);

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	for (i = 0; i < adapter->rss_lut_size; i++)
		adapter->rss_lut[i] = (u8)(indir[i]);

	return iavf_config_rss(adapter);
}

static const struct ethtool_ops iavf_ethtool_ops = {
	.get_drvinfo		= iavf_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= iavf_get_ringparam,
	.set_ringparam		= iavf_set_ringparam,
	.get_strings		= iavf_get_strings,
	.get_ethtool_stats	= iavf_get_ethtool_stats,
	.get_sset_count		= iavf_get_sset_count,
	.get_priv_flags		= iavf_get_priv_flags,
	.set_priv_flags		= iavf_set_priv_flags,
	.get_msglevel		= iavf_get_msglevel,
	.set_msglevel		= iavf_set_msglevel,
	.get_coalesce		= iavf_get_coalesce,
	.set_coalesce		= iavf_set_coalesce,
	.get_per_queue_coalesce = iavf_get_per_queue_coalesce,
	.set_per_queue_coalesce = iavf_set_per_queue_coalesce,
	.get_rxnfc		= iavf_get_rxnfc,
	.get_rxfh_indir_size	= iavf_get_rxfh_indir_size,
	.get_rxfh		= iavf_get_rxfh,
	.set_rxfh		= iavf_set_rxfh,
	.get_channels		= iavf_get_channels,
	.set_channels		= iavf_set_channels,
	.get_rxfh_key_size	= iavf_get_rxfh_key_size,
	.get_link_ksettings	= iavf_get_link_ksettings,
};

/**
 * iavf_set_ethtool_ops - Initialize ethtool ops struct
 * @netdev: network interface device structure
 *
 * Sets ethtool ops struct in our netdev so that ethtool can call
 * our functions.
 **/
void iavf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &iavf_ethtool_ops;
}
