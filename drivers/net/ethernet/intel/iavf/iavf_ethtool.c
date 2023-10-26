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
	cmd->base.duplex = DUPLEX_FULL;

	if (ADV_LINK_SUPPORT(adapter)) {
		if (adapter->link_speed_mbps &&
		    adapter->link_speed_mbps < U32_MAX)
			cmd->base.speed = adapter->link_speed_mbps;
		else
			cmd->base.speed = SPEED_UNKNOWN;

		return 0;
	}

	switch (adapter->link_speed) {
	case VIRTCHNL_LINK_SPEED_40GB:
		cmd->base.speed = SPEED_40000;
		break;
	case VIRTCHNL_LINK_SPEED_25GB:
		cmd->base.speed = SPEED_25000;
		break;
	case VIRTCHNL_LINK_SPEED_20GB:
		cmd->base.speed = SPEED_20000;
		break;
	case VIRTCHNL_LINK_SPEED_10GB:
		cmd->base.speed = SPEED_10000;
		break;
	case VIRTCHNL_LINK_SPEED_5GB:
		cmd->base.speed = SPEED_5000;
		break;
	case VIRTCHNL_LINK_SPEED_2_5GB:
		cmd->base.speed = SPEED_2500;
		break;
	case VIRTCHNL_LINK_SPEED_1GB:
		cmd->base.speed = SPEED_1000;
		break;
	case VIRTCHNL_LINK_SPEED_100MB:
		cmd->base.speed = SPEED_100;
		break;
	default:
		break;
	}

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
	/* Report the maximum number queues, even if not every queue is
	 * currently configured. Since allocation of queues is in pairs,
	 * use netdev->real_num_tx_queues * 2. The real_num_tx_queues is set
	 * at device creation and never changes.
	 */

	if (sset == ETH_SS_STATS)
		return IAVF_STATS_LEN +
			(IAVF_QUEUE_STATS_LEN * 2 *
			 netdev->real_num_tx_queues);
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

	/* Explicitly request stats refresh */
	iavf_schedule_request_stats(adapter);

	iavf_add_ethtool_stats(&data, adapter, iavf_gstrings_stats);

	rcu_read_lock();
	/* As num_active_queues describe both tx and rx queues, we can use
	 * it to iterate over rings' stats.
	 */
	for (i = 0; i < adapter->num_active_queues; i++) {
		struct iavf_ring *ring;

		/* Tx rings stats */
		ring = &adapter->tx_rings[i];
		iavf_add_queue_stats(&data, ring);

		/* Rx rings stats */
		ring = &adapter->rx_rings[i];
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

	/* Queues are always allocated in pairs, so we just use
	 * real_num_tx_queues for both Tx and Rx queues.
	 */
	for (i = 0; i < netdev->real_num_tx_queues; i++) {
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
	int ret = 0;
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
			iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);
			ret = iavf_wait_for_reset(adapter);
			if (ret)
				netdev_warn(netdev, "Changing private flags timeout or interrupted waiting for reset");
		}
	}

	return ret;
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

	strscpy(drvinfo->driver, iavf_driver_name, 32);
	strscpy(drvinfo->fw_version, "N/A", 4);
	strscpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->n_priv_flags = IAVF_PRIV_FLAGS_STR_LEN;
}

/**
 * iavf_get_ringparam - Get ring parameters
 * @netdev: network interface device structure
 * @ring: ethtool ringparam structure
 * @kernel_ring: ethtool extenal ringparam structure
 * @extack: netlink extended ACK report struct
 *
 * Returns current ring parameters. TX and RX rings are reported separately,
 * but the number of rings is not reported.
 **/
static void iavf_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
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
 * @kernel_ring: ethtool external ringparam structure
 * @extack: netlink extended ACK report struct
 *
 * Sets ring parameters. TX and RX rings are controlled separately, but the
 * number of rings is not specified, so all rings get the same settings.
 **/
static int iavf_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring,
			      struct kernel_ethtool_ringparam *kernel_ring,
			      struct netlink_ext_ack *extack)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u32 new_rx_count, new_tx_count;
	int ret = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	if (ring->tx_pending > IAVF_MAX_TXD ||
	    ring->tx_pending < IAVF_MIN_TXD ||
	    ring->rx_pending > IAVF_MAX_RXD ||
	    ring->rx_pending < IAVF_MIN_RXD) {
		netdev_err(netdev, "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d] (increment %d)\n",
			   ring->tx_pending, ring->rx_pending, IAVF_MIN_TXD,
			   IAVF_MAX_RXD, IAVF_REQ_DESCRIPTOR_MULTIPLE);
		return -EINVAL;
	}

	new_tx_count = ALIGN(ring->tx_pending, IAVF_REQ_DESCRIPTOR_MULTIPLE);
	if (new_tx_count != ring->tx_pending)
		netdev_info(netdev, "Requested Tx descriptor count rounded up to %d\n",
			    new_tx_count);

	new_rx_count = ALIGN(ring->rx_pending, IAVF_REQ_DESCRIPTOR_MULTIPLE);
	if (new_rx_count != ring->rx_pending)
		netdev_info(netdev, "Requested Rx descriptor count rounded up to %d\n",
			    new_rx_count);

	/* if nothing to do return success */
	if ((new_tx_count == adapter->tx_desc_count) &&
	    (new_rx_count == adapter->rx_desc_count)) {
		netdev_dbg(netdev, "Nothing to change, descriptor count is same as requested\n");
		return 0;
	}

	if (new_tx_count != adapter->tx_desc_count) {
		netdev_dbg(netdev, "Changing Tx descriptor count from %d to %d\n",
			   adapter->tx_desc_count, new_tx_count);
		adapter->tx_desc_count = new_tx_count;
	}

	if (new_rx_count != adapter->rx_desc_count) {
		netdev_dbg(netdev, "Changing Rx descriptor count from %d to %d\n",
			   adapter->rx_desc_count, new_rx_count);
		adapter->rx_desc_count = new_rx_count;
	}

	if (netif_running(netdev)) {
		iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);
		ret = iavf_wait_for_reset(adapter);
		if (ret)
			netdev_warn(netdev, "Changing ring parameters timeout or interrupted waiting for reset");
	}

	return ret;
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
	struct iavf_ring *rx_ring, *tx_ring;

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
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack: extack for reporting error messages
 *
 * Returns current coalescing settings. This is referred to elsewhere in the
 * driver as Interrupt Throttle Rate, as this is how the hardware describes
 * this functionality. Note that if per-queue settings have been modified this
 * only represents the settings of queue 0.
 **/
static int iavf_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
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
static int iavf_set_itr_per_queue(struct iavf_adapter *adapter,
				  struct ethtool_coalesce *ec, int queue)
{
	struct iavf_ring *rx_ring = &adapter->rx_rings[queue];
	struct iavf_ring *tx_ring = &adapter->tx_rings[queue];
	struct iavf_q_vector *q_vector;
	u16 itr_setting;

	itr_setting = rx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	if (ec->rx_coalesce_usecs != itr_setting &&
	    ec->use_adaptive_rx_coalesce) {
		netif_info(adapter, drv, adapter->netdev,
			   "Rx interrupt throttling cannot be changed if adaptive-rx is enabled\n");
		return -EINVAL;
	}

	itr_setting = tx_ring->itr_setting & ~IAVF_ITR_DYNAMIC;

	if (ec->tx_coalesce_usecs != itr_setting &&
	    ec->use_adaptive_tx_coalesce) {
		netif_info(adapter, drv, adapter->netdev,
			   "Tx interrupt throttling cannot be changed if adaptive-tx is enabled\n");
		return -EINVAL;
	}

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
	return 0;
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
	int i;

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
			if (iavf_set_itr_per_queue(adapter, ec, i))
				return -EINVAL;
	} else if (queue < adapter->num_active_queues) {
		if (iavf_set_itr_per_queue(adapter, ec, queue))
			return -EINVAL;
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
 * @kernel_coal: ethtool CQE mode setting structure
 * @extack: extack for reporting error messages
 *
 * Change current coalescing settings for every queue.
 **/
static int iavf_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec,
			     struct kernel_ethtool_coalesce *kernel_coal,
			     struct netlink_ext_ack *extack)
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
 * iavf_fltr_to_ethtool_flow - convert filter type values to ethtool
 * flow type values
 * @flow: filter type to be converted
 *
 * Returns the corresponding ethtool flow type.
 */
static int iavf_fltr_to_ethtool_flow(enum iavf_fdir_flow_type flow)
{
	switch (flow) {
	case IAVF_FDIR_FLOW_IPV4_TCP:
		return TCP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_UDP:
		return UDP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_SCTP:
		return SCTP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_AH:
		return AH_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_ESP:
		return ESP_V4_FLOW;
	case IAVF_FDIR_FLOW_IPV4_OTHER:
		return IPV4_USER_FLOW;
	case IAVF_FDIR_FLOW_IPV6_TCP:
		return TCP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_UDP:
		return UDP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_SCTP:
		return SCTP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_AH:
		return AH_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_ESP:
		return ESP_V6_FLOW;
	case IAVF_FDIR_FLOW_IPV6_OTHER:
		return IPV6_USER_FLOW;
	case IAVF_FDIR_FLOW_NON_IP_L2:
		return ETHER_FLOW;
	default:
		/* 0 is undefined ethtool flow */
		return 0;
	}
}

/**
 * iavf_ethtool_flow_to_fltr - convert ethtool flow type to filter enum
 * @eth: Ethtool flow type to be converted
 *
 * Returns flow enum
 */
static enum iavf_fdir_flow_type iavf_ethtool_flow_to_fltr(int eth)
{
	switch (eth) {
	case TCP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_TCP;
	case UDP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_UDP;
	case SCTP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_SCTP;
	case AH_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_AH;
	case ESP_V4_FLOW:
		return IAVF_FDIR_FLOW_IPV4_ESP;
	case IPV4_USER_FLOW:
		return IAVF_FDIR_FLOW_IPV4_OTHER;
	case TCP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_TCP;
	case UDP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_UDP;
	case SCTP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_SCTP;
	case AH_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_AH;
	case ESP_V6_FLOW:
		return IAVF_FDIR_FLOW_IPV6_ESP;
	case IPV6_USER_FLOW:
		return IAVF_FDIR_FLOW_IPV6_OTHER;
	case ETHER_FLOW:
		return IAVF_FDIR_FLOW_NON_IP_L2;
	default:
		return IAVF_FDIR_FLOW_NONE;
	}
}

/**
 * iavf_is_mask_valid - check mask field set
 * @mask: full mask to check
 * @field: field for which mask should be valid
 *
 * If the mask is fully set return true. If it is not valid for field return
 * false.
 */
static bool iavf_is_mask_valid(u64 mask, u64 field)
{
	return (mask & field) == field;
}

/**
 * iavf_parse_rx_flow_user_data - deconstruct user-defined data
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: pointer to Flow Director filter for userdef data storage
 *
 * Returns 0 on success, negative error value on failure
 */
static int
iavf_parse_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
			     struct iavf_fdir_fltr *fltr)
{
	struct iavf_flex_word *flex;
	int i, cnt = 0;

	if (!(fsp->flow_type & FLOW_EXT))
		return 0;

	for (i = 0; i < IAVF_FLEX_WORD_NUM; i++) {
#define IAVF_USERDEF_FLEX_WORD_M	GENMASK(15, 0)
#define IAVF_USERDEF_FLEX_OFFS_S	16
#define IAVF_USERDEF_FLEX_OFFS_M	GENMASK(31, IAVF_USERDEF_FLEX_OFFS_S)
#define IAVF_USERDEF_FLEX_FLTR_M	GENMASK(31, 0)
		u32 value = be32_to_cpu(fsp->h_ext.data[i]);
		u32 mask = be32_to_cpu(fsp->m_ext.data[i]);

		if (!value || !mask)
			continue;

		if (!iavf_is_mask_valid(mask, IAVF_USERDEF_FLEX_FLTR_M))
			return -EINVAL;

		/* 504 is the maximum value for offsets, and offset is measured
		 * from the start of the MAC address.
		 */
#define IAVF_USERDEF_FLEX_MAX_OFFS_VAL 504
		flex = &fltr->flex_words[cnt++];
		flex->word = value & IAVF_USERDEF_FLEX_WORD_M;
		flex->offset = (value & IAVF_USERDEF_FLEX_OFFS_M) >>
			     IAVF_USERDEF_FLEX_OFFS_S;
		if (flex->offset > IAVF_USERDEF_FLEX_MAX_OFFS_VAL)
			return -EINVAL;
	}

	fltr->flex_cnt = cnt;

	return 0;
}

/**
 * iavf_fill_rx_flow_ext_data - fill the additional data
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: pointer to Flow Director filter to get additional data
 */
static void
iavf_fill_rx_flow_ext_data(struct ethtool_rx_flow_spec *fsp,
			   struct iavf_fdir_fltr *fltr)
{
	if (!fltr->ext_mask.usr_def[0] && !fltr->ext_mask.usr_def[1])
		return;

	fsp->flow_type |= FLOW_EXT;

	memcpy(fsp->h_ext.data, fltr->ext_data.usr_def, sizeof(fsp->h_ext.data));
	memcpy(fsp->m_ext.data, fltr->ext_mask.usr_def, sizeof(fsp->m_ext.data));
}

/**
 * iavf_get_ethtool_fdir_entry - fill ethtool structure with Flow Director filter data
 * @adapter: the VF adapter structure that contains filter list
 * @cmd: ethtool command data structure to receive the filter data
 *
 * Returns 0 as expected for success by ethtool
 */
static int
iavf_get_ethtool_fdir_entry(struct iavf_adapter *adapter,
			    struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;
	struct iavf_fdir_fltr *rule = NULL;
	int ret = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->fdir_fltr_lock);

	rule = iavf_find_fdir_fltr_by_loc(adapter, fsp->location);
	if (!rule) {
		ret = -EINVAL;
		goto release_lock;
	}

	fsp->flow_type = iavf_fltr_to_ethtool_flow(rule->flow_type);

	memset(&fsp->m_u, 0, sizeof(fsp->m_u));
	memset(&fsp->m_ext, 0, sizeof(fsp->m_ext));

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.tcp_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.tcp_ip4_spec.psrc = rule->ip_data.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = rule->ip_data.dst_port;
		fsp->h_u.tcp_ip4_spec.tos = rule->ip_data.tos;
		fsp->m_u.tcp_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.tcp_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.tcp_ip4_spec.psrc = rule->ip_mask.src_port;
		fsp->m_u.tcp_ip4_spec.pdst = rule->ip_mask.dst_port;
		fsp->m_u.tcp_ip4_spec.tos = rule->ip_mask.tos;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fsp->h_u.ah_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.ah_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.ah_ip4_spec.spi = rule->ip_data.spi;
		fsp->h_u.ah_ip4_spec.tos = rule->ip_data.tos;
		fsp->m_u.ah_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.ah_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.ah_ip4_spec.spi = rule->ip_mask.spi;
		fsp->m_u.ah_ip4_spec.tos = rule->ip_mask.tos;
		break;
	case IPV4_USER_FLOW:
		fsp->h_u.usr_ip4_spec.ip4src = rule->ip_data.v4_addrs.src_ip;
		fsp->h_u.usr_ip4_spec.ip4dst = rule->ip_data.v4_addrs.dst_ip;
		fsp->h_u.usr_ip4_spec.l4_4_bytes = rule->ip_data.l4_header;
		fsp->h_u.usr_ip4_spec.tos = rule->ip_data.tos;
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = rule->ip_data.proto;
		fsp->m_u.usr_ip4_spec.ip4src = rule->ip_mask.v4_addrs.src_ip;
		fsp->m_u.usr_ip4_spec.ip4dst = rule->ip_mask.v4_addrs.dst_ip;
		fsp->m_u.usr_ip4_spec.l4_4_bytes = rule->ip_mask.l4_header;
		fsp->m_u.usr_ip4_spec.tos = rule->ip_mask.tos;
		fsp->m_u.usr_ip4_spec.ip_ver = 0xFF;
		fsp->m_u.usr_ip4_spec.proto = rule->ip_mask.proto;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(fsp->h_u.usr_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.usr_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.tcp_ip6_spec.psrc = rule->ip_data.src_port;
		fsp->h_u.tcp_ip6_spec.pdst = rule->ip_data.dst_port;
		fsp->h_u.tcp_ip6_spec.tclass = rule->ip_data.tclass;
		memcpy(fsp->m_u.usr_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.usr_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.tcp_ip6_spec.psrc = rule->ip_mask.src_port;
		fsp->m_u.tcp_ip6_spec.pdst = rule->ip_mask.dst_port;
		fsp->m_u.tcp_ip6_spec.tclass = rule->ip_mask.tclass;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(fsp->h_u.ah_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.ah_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.ah_ip6_spec.spi = rule->ip_data.spi;
		fsp->h_u.ah_ip6_spec.tclass = rule->ip_data.tclass;
		memcpy(fsp->m_u.ah_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.ah_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.ah_ip6_spec.spi = rule->ip_mask.spi;
		fsp->m_u.ah_ip6_spec.tclass = rule->ip_mask.tclass;
		break;
	case IPV6_USER_FLOW:
		memcpy(fsp->h_u.usr_ip6_spec.ip6src, &rule->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.usr_ip6_spec.ip6dst, &rule->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.usr_ip6_spec.l4_4_bytes = rule->ip_data.l4_header;
		fsp->h_u.usr_ip6_spec.tclass = rule->ip_data.tclass;
		fsp->h_u.usr_ip6_spec.l4_proto = rule->ip_data.proto;
		memcpy(fsp->m_u.usr_ip6_spec.ip6src, &rule->ip_mask.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.usr_ip6_spec.ip6dst, &rule->ip_mask.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.usr_ip6_spec.l4_4_bytes = rule->ip_mask.l4_header;
		fsp->m_u.usr_ip6_spec.tclass = rule->ip_mask.tclass;
		fsp->m_u.usr_ip6_spec.l4_proto = rule->ip_mask.proto;
		break;
	case ETHER_FLOW:
		fsp->h_u.ether_spec.h_proto = rule->eth_data.etype;
		fsp->m_u.ether_spec.h_proto = rule->eth_mask.etype;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	iavf_fill_rx_flow_ext_data(fsp, rule);

	if (rule->action == VIRTCHNL_ACTION_DROP)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->q_index;

release_lock:
	spin_unlock_bh(&adapter->fdir_fltr_lock);
	return ret;
}

/**
 * iavf_get_fdir_fltr_ids - fill buffer with filter IDs of active filters
 * @adapter: the VF adapter structure containing the filter list
 * @cmd: ethtool command data structure
 * @rule_locs: ethtool array passed in from OS to receive filter IDs
 *
 * Returns 0 as expected for success by ethtool
 */
static int
iavf_get_fdir_fltr_ids(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd,
		       u32 *rule_locs)
{
	struct iavf_fdir_fltr *fltr;
	unsigned int cnt = 0;
	int val = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	cmd->data = IAVF_MAX_FDIR_FILTERS;

	spin_lock_bh(&adapter->fdir_fltr_lock);

	list_for_each_entry(fltr, &adapter->fdir_list_head, list) {
		if (cnt == cmd->rule_cnt) {
			val = -EMSGSIZE;
			goto release_lock;
		}
		rule_locs[cnt] = fltr->loc;
		cnt++;
	}

release_lock:
	spin_unlock_bh(&adapter->fdir_fltr_lock);
	if (!val)
		cmd->rule_cnt = cnt;

	return val;
}

/**
 * iavf_add_fdir_fltr_info - Set the input set for Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @fsp: pointer to ethtool Rx flow specification
 * @fltr: filter structure
 */
static int
iavf_add_fdir_fltr_info(struct iavf_adapter *adapter, struct ethtool_rx_flow_spec *fsp,
			struct iavf_fdir_fltr *fltr)
{
	u32 flow_type, q_index = 0;
	enum virtchnl_action act;
	int err;

	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		act = VIRTCHNL_ACTION_DROP;
	} else {
		q_index = fsp->ring_cookie;
		if (q_index >= adapter->num_active_queues)
			return -EINVAL;

		act = VIRTCHNL_ACTION_QUEUE;
	}

	fltr->action = act;
	fltr->loc = fsp->location;
	fltr->q_index = q_index;

	if (fsp->flow_type & FLOW_EXT) {
		memcpy(fltr->ext_data.usr_def, fsp->h_ext.data,
		       sizeof(fltr->ext_data.usr_def));
		memcpy(fltr->ext_mask.usr_def, fsp->m_ext.data,
		       sizeof(fltr->ext_mask.usr_def));
	}

	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS);
	fltr->flow_type = iavf_ethtool_flow_to_fltr(flow_type);

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.tcp_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
		fltr->ip_data.src_port = fsp->h_u.tcp_ip4_spec.psrc;
		fltr->ip_data.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
		fltr->ip_data.tos = fsp->h_u.tcp_ip4_spec.tos;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.tcp_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.tcp_ip4_spec.ip4dst;
		fltr->ip_mask.src_port = fsp->m_u.tcp_ip4_spec.psrc;
		fltr->ip_mask.dst_port = fsp->m_u.tcp_ip4_spec.pdst;
		fltr->ip_mask.tos = fsp->m_u.tcp_ip4_spec.tos;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.ah_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.ah_ip4_spec.ip4dst;
		fltr->ip_data.spi = fsp->h_u.ah_ip4_spec.spi;
		fltr->ip_data.tos = fsp->h_u.ah_ip4_spec.tos;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.ah_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.ah_ip4_spec.ip4dst;
		fltr->ip_mask.spi = fsp->m_u.ah_ip4_spec.spi;
		fltr->ip_mask.tos = fsp->m_u.ah_ip4_spec.tos;
		break;
	case IPV4_USER_FLOW:
		fltr->ip_data.v4_addrs.src_ip = fsp->h_u.usr_ip4_spec.ip4src;
		fltr->ip_data.v4_addrs.dst_ip = fsp->h_u.usr_ip4_spec.ip4dst;
		fltr->ip_data.l4_header = fsp->h_u.usr_ip4_spec.l4_4_bytes;
		fltr->ip_data.tos = fsp->h_u.usr_ip4_spec.tos;
		fltr->ip_data.proto = fsp->h_u.usr_ip4_spec.proto;
		fltr->ip_mask.v4_addrs.src_ip = fsp->m_u.usr_ip4_spec.ip4src;
		fltr->ip_mask.v4_addrs.dst_ip = fsp->m_u.usr_ip4_spec.ip4dst;
		fltr->ip_mask.l4_header = fsp->m_u.usr_ip4_spec.l4_4_bytes;
		fltr->ip_mask.tos = fsp->m_u.usr_ip4_spec.tos;
		fltr->ip_mask.proto = fsp->m_u.usr_ip4_spec.proto;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.src_port = fsp->h_u.tcp_ip6_spec.psrc;
		fltr->ip_data.dst_port = fsp->h_u.tcp_ip6_spec.pdst;
		fltr->ip_data.tclass = fsp->h_u.tcp_ip6_spec.tclass;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.src_port = fsp->m_u.tcp_ip6_spec.psrc;
		fltr->ip_mask.dst_port = fsp->m_u.tcp_ip6_spec.pdst;
		fltr->ip_mask.tclass = fsp->m_u.tcp_ip6_spec.tclass;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.ah_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.ah_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.spi = fsp->h_u.ah_ip6_spec.spi;
		fltr->ip_data.tclass = fsp->h_u.ah_ip6_spec.tclass;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.ah_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.ah_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.spi = fsp->m_u.ah_ip6_spec.spi;
		fltr->ip_mask.tclass = fsp->m_u.ah_ip6_spec.tclass;
		break;
	case IPV6_USER_FLOW:
		memcpy(&fltr->ip_data.v6_addrs.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_data.v6_addrs.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_data.l4_header = fsp->h_u.usr_ip6_spec.l4_4_bytes;
		fltr->ip_data.tclass = fsp->h_u.usr_ip6_spec.tclass;
		fltr->ip_data.proto = fsp->h_u.usr_ip6_spec.l4_proto;
		memcpy(&fltr->ip_mask.v6_addrs.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&fltr->ip_mask.v6_addrs.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		fltr->ip_mask.l4_header = fsp->m_u.usr_ip6_spec.l4_4_bytes;
		fltr->ip_mask.tclass = fsp->m_u.usr_ip6_spec.tclass;
		fltr->ip_mask.proto = fsp->m_u.usr_ip6_spec.l4_proto;
		break;
	case ETHER_FLOW:
		fltr->eth_data.etype = fsp->h_u.ether_spec.h_proto;
		fltr->eth_mask.etype = fsp->m_u.ether_spec.h_proto;
		break;
	default:
		/* not doing un-parsed flow types */
		return -EINVAL;
	}

	if (iavf_fdir_is_dup_fltr(adapter, fltr))
		return -EEXIST;

	err = iavf_parse_rx_flow_user_data(fsp, fltr);
	if (err)
		return err;

	return iavf_fill_fdir_add_msg(adapter, fltr);
}

/**
 * iavf_add_fdir_ethtool - add Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @cmd: command to add Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
static int iavf_add_fdir_ethtool(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct iavf_fdir_fltr *fltr;
	int count = 50;
	int err;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	if (adapter->fdir_active_fltr >= IAVF_MAX_FDIR_FILTERS) {
		dev_err(&adapter->pdev->dev,
			"Unable to add Flow Director filter because VF reached the limit of max allowed filters (%u)\n",
			IAVF_MAX_FDIR_FILTERS);
		return -ENOSPC;
	}

	spin_lock_bh(&adapter->fdir_fltr_lock);
	if (iavf_find_fdir_fltr_by_loc(adapter, fsp->location)) {
		dev_err(&adapter->pdev->dev, "Failed to add Flow Director filter, it already exists\n");
		spin_unlock_bh(&adapter->fdir_fltr_lock);
		return -EEXIST;
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	fltr = kzalloc(sizeof(*fltr), GFP_KERNEL);
	if (!fltr)
		return -ENOMEM;

	while (!mutex_trylock(&adapter->crit_lock)) {
		if (--count == 0) {
			kfree(fltr);
			return -EINVAL;
		}
		udelay(1);
	}

	err = iavf_add_fdir_fltr_info(adapter, fsp, fltr);
	if (err)
		goto ret;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	iavf_fdir_list_add_fltr(adapter, fltr);
	adapter->fdir_active_fltr++;
	fltr->state = IAVF_FDIR_FLTR_ADD_REQUEST;
	adapter->aq_required |= IAVF_FLAG_AQ_ADD_FDIR_FILTER;
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

ret:
	if (err && fltr)
		kfree(fltr);

	mutex_unlock(&adapter->crit_lock);
	return err;
}

/**
 * iavf_del_fdir_ethtool - delete Flow Director filter
 * @adapter: pointer to the VF adapter structure
 * @cmd: command to delete Flow Director filter
 *
 * Returns 0 on success and negative values for failure
 */
static int iavf_del_fdir_ethtool(struct iavf_adapter *adapter, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;
	struct iavf_fdir_fltr *fltr = NULL;
	int err = 0;

	if (!FDIR_FLTR_SUPPORT(adapter))
		return -EOPNOTSUPP;

	spin_lock_bh(&adapter->fdir_fltr_lock);
	fltr = iavf_find_fdir_fltr_by_loc(adapter, fsp->location);
	if (fltr) {
		if (fltr->state == IAVF_FDIR_FLTR_ACTIVE) {
			fltr->state = IAVF_FDIR_FLTR_DEL_REQUEST;
			adapter->aq_required |= IAVF_FLAG_AQ_DEL_FDIR_FILTER;
		} else {
			err = -EBUSY;
		}
	} else if (adapter->fdir_active_fltr) {
		err = -EINVAL;
	}
	spin_unlock_bh(&adapter->fdir_fltr_lock);

	if (fltr && fltr->state == IAVF_FDIR_FLTR_DEL_REQUEST)
		mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

	return err;
}

/**
 * iavf_adv_rss_parse_hdrs - parses headers from RSS hash input
 * @cmd: ethtool rxnfc command
 *
 * This function parses the rxnfc command and returns intended
 * header types for RSS configuration
 */
static u32 iavf_adv_rss_parse_hdrs(struct ethtool_rxnfc *cmd)
{
	u32 hdrs = IAVF_ADV_RSS_FLOW_SEG_HDR_NONE;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_TCP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case UDP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_UDP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case SCTP_V4_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4;
		break;
	case TCP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_TCP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	case UDP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_UDP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	case SCTP_V6_FLOW:
		hdrs |= IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP |
			IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6;
		break;
	default:
		break;
	}

	return hdrs;
}

/**
 * iavf_adv_rss_parse_hash_flds - parses hash fields from RSS hash input
 * @cmd: ethtool rxnfc command
 *
 * This function parses the rxnfc command and returns intended hash fields for
 * RSS configuration
 */
static u64 iavf_adv_rss_parse_hash_flds(struct ethtool_rxnfc *cmd)
{
	u64 hfld = IAVF_ADV_RSS_HASH_INVALID;

	if (cmd->data & RXH_IP_SRC || cmd->data & RXH_IP_DST) {
		switch (cmd->flow_type) {
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
			if (cmd->data & RXH_IP_SRC)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV4_SA;
			if (cmd->data & RXH_IP_DST)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV4_DA;
			break;
		case TCP_V6_FLOW:
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
			if (cmd->data & RXH_IP_SRC)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV6_SA;
			if (cmd->data & RXH_IP_DST)
				hfld |= IAVF_ADV_RSS_HASH_FLD_IPV6_DA;
			break;
		default:
			break;
		}
	}

	if (cmd->data & RXH_L4_B_0_1 || cmd->data & RXH_L4_B_2_3) {
		switch (cmd->flow_type) {
		case TCP_V4_FLOW:
		case TCP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT;
			break;
		case UDP_V4_FLOW:
		case UDP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT;
			break;
		case SCTP_V4_FLOW:
		case SCTP_V6_FLOW:
			if (cmd->data & RXH_L4_B_0_1)
				hfld |= IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT;
			if (cmd->data & RXH_L4_B_2_3)
				hfld |= IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT;
			break;
		default:
			break;
		}
	}

	return hfld;
}

/**
 * iavf_set_adv_rss_hash_opt - Enable/Disable flow types for RSS hash
 * @adapter: pointer to the VF adapter structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 */
static int
iavf_set_adv_rss_hash_opt(struct iavf_adapter *adapter,
			  struct ethtool_rxnfc *cmd)
{
	struct iavf_adv_rss *rss_old, *rss_new;
	bool rss_new_add = false;
	int count = 50, err = 0;
	u64 hash_flds;
	u32 hdrs;

	if (!ADV_RSS_SUPPORT(adapter))
		return -EOPNOTSUPP;

	hdrs = iavf_adv_rss_parse_hdrs(cmd);
	if (hdrs == IAVF_ADV_RSS_FLOW_SEG_HDR_NONE)
		return -EINVAL;

	hash_flds = iavf_adv_rss_parse_hash_flds(cmd);
	if (hash_flds == IAVF_ADV_RSS_HASH_INVALID)
		return -EINVAL;

	rss_new = kzalloc(sizeof(*rss_new), GFP_KERNEL);
	if (!rss_new)
		return -ENOMEM;

	if (iavf_fill_adv_rss_cfg_msg(&rss_new->cfg_msg, hdrs, hash_flds)) {
		kfree(rss_new);
		return -EINVAL;
	}

	while (!mutex_trylock(&adapter->crit_lock)) {
		if (--count == 0) {
			kfree(rss_new);
			return -EINVAL;
		}

		udelay(1);
	}

	spin_lock_bh(&adapter->adv_rss_lock);
	rss_old = iavf_find_adv_rss_cfg_by_hdrs(adapter, hdrs);
	if (rss_old) {
		if (rss_old->state != IAVF_ADV_RSS_ACTIVE) {
			err = -EBUSY;
		} else if (rss_old->hash_flds != hash_flds) {
			rss_old->state = IAVF_ADV_RSS_ADD_REQUEST;
			rss_old->hash_flds = hash_flds;
			memcpy(&rss_old->cfg_msg, &rss_new->cfg_msg,
			       sizeof(rss_new->cfg_msg));
			adapter->aq_required |= IAVF_FLAG_AQ_ADD_ADV_RSS_CFG;
		} else {
			err = -EEXIST;
		}
	} else {
		rss_new_add = true;
		rss_new->state = IAVF_ADV_RSS_ADD_REQUEST;
		rss_new->packet_hdrs = hdrs;
		rss_new->hash_flds = hash_flds;
		list_add_tail(&rss_new->list, &adapter->adv_rss_list_head);
		adapter->aq_required |= IAVF_FLAG_AQ_ADD_ADV_RSS_CFG;
	}
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (!err)
		mod_delayed_work(adapter->wq, &adapter->watchdog_task, 0);

	mutex_unlock(&adapter->crit_lock);

	if (!rss_new_add)
		kfree(rss_new);

	return err;
}

/**
 * iavf_get_adv_rss_hash_opt - Retrieve hash fields for a given flow-type
 * @adapter: pointer to the VF adapter structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 */
static int
iavf_get_adv_rss_hash_opt(struct iavf_adapter *adapter,
			  struct ethtool_rxnfc *cmd)
{
	struct iavf_adv_rss *rss;
	u64 hash_flds;
	u32 hdrs;

	if (!ADV_RSS_SUPPORT(adapter))
		return -EOPNOTSUPP;

	cmd->data = 0;

	hdrs = iavf_adv_rss_parse_hdrs(cmd);
	if (hdrs == IAVF_ADV_RSS_FLOW_SEG_HDR_NONE)
		return -EINVAL;

	spin_lock_bh(&adapter->adv_rss_lock);
	rss = iavf_find_adv_rss_cfg_by_hdrs(adapter, hdrs);
	if (rss)
		hash_flds = rss->hash_flds;
	else
		hash_flds = IAVF_ADV_RSS_HASH_INVALID;
	spin_unlock_bh(&adapter->adv_rss_lock);

	if (hash_flds == IAVF_ADV_RSS_HASH_INVALID)
		return -EINVAL;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_SA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_SA))
		cmd->data |= (u64)RXH_IP_SRC;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_DA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_DA))
		cmd->data |= (u64)RXH_IP_DST;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT))
		cmd->data |= (u64)RXH_L4_B_0_1;

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT))
		cmd->data |= (u64)RXH_L4_B_2_3;

	return 0;
}

/**
 * iavf_set_rxnfc - command to set Rx flow rules.
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns 0 for success and negative values for errors
 */
static int iavf_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = iavf_add_fdir_ethtool(adapter, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = iavf_del_fdir_ethtool(adapter, cmd);
		break;
	case ETHTOOL_SRXFH:
		ret = iavf_set_adv_rss_hash_opt(adapter, cmd);
		break;
	default:
		break;
	}

	return ret;
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
	case ETHTOOL_GRXCLSRLCNT:
		if (!FDIR_FLTR_SUPPORT(adapter))
			break;
		cmd->rule_cnt = adapter->fdir_active_fltr;
		cmd->data = IAVF_MAX_FDIR_FILTERS;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = iavf_get_ethtool_fdir_entry(adapter, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = iavf_get_fdir_fltr_ids(adapter, cmd, (u32 *)rule_locs);
		break;
	case ETHTOOL_GRXFH:
		ret = iavf_get_adv_rss_hash_opt(adapter, cmd);
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
	ch->max_combined = adapter->vsi_res->num_queue_pairs;

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
	u32 num_req = ch->combined_count;
	int ret = 0;

	if ((adapter->vf_res->vf_cap_flags & VIRTCHNL_VF_OFFLOAD_ADQ) &&
	    adapter->num_tc) {
		dev_info(&adapter->pdev->dev, "Cannot set channels since ADq is enabled.\n");
		return -EINVAL;
	}

	/* All of these should have already been checked by ethtool before this
	 * even gets to us, but just to be sure.
	 */
	if (num_req == 0 || num_req > adapter->vsi_res->num_queue_pairs)
		return -EINVAL;

	if (num_req == adapter->num_active_queues)
		return 0;

	if (ch->rx_count || ch->tx_count || ch->other_count != NONQ_VECS)
		return -EINVAL;

	adapter->num_req_queues = num_req;
	adapter->flags |= IAVF_FLAG_REINIT_ITR_NEEDED;
	iavf_schedule_reset(adapter, IAVF_FLAG_RESET_NEEDED);

	ret = iavf_wait_for_reset(adapter);
	if (ret)
		netdev_warn(netdev, "Changing channel count timeout or interrupted waiting for reset");

	return ret;
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
	if (key)
		memcpy(key, adapter->rss_key, adapter->rss_key_size);

	if (indir)
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
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int iavf_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct iavf_adapter *adapter = netdev_priv(netdev);
	u16 i;

	/* Only support toeplitz hash function */
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (!key && !indir)
		return 0;

	if (key)
		memcpy(adapter->rss_key, key, adapter->rss_key_size);

	if (indir) {
		/* Each 32 bits pointed by 'indir' is stored with a lut entry */
		for (i = 0; i < adapter->rss_lut_size; i++)
			adapter->rss_lut[i] = (u8)(indir[i]);
	}

	return iavf_config_rss(adapter);
}

static const struct ethtool_ops iavf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
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
	.set_rxnfc		= iavf_set_rxnfc,
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
