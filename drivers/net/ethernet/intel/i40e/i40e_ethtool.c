// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

/* ethtool support for i40e */

#include "i40e.h"
#include "i40e_diag.h"
#include "i40e_txrx_common.h"

/* ethtool statistics helpers */

/**
 * struct i40e_stats - definition for an ethtool statistic
 * @stat_string: statistic name to display in ethtool -S output
 * @sizeof_stat: the sizeof() the stat, must be no greater than sizeof(u64)
 * @stat_offset: offsetof() the stat from a base pointer
 *
 * This structure defines a statistic to be added to the ethtool stats buffer.
 * It defines a statistic as offset from a common base pointer. Stats should
 * be defined in constant arrays using the I40E_STAT macro, with every element
 * of the array using the same _type for calculating the sizeof_stat and
 * stat_offset.
 *
 * The @sizeof_stat is expected to be sizeof(u8), sizeof(u16), sizeof(u32) or
 * sizeof(u64). Other sizes are not expected and will produce a WARN_ONCE from
 * the i40e_add_ethtool_stat() helper function.
 *
 * The @stat_string is interpreted as a format string, allowing formatted
 * values to be inserted while looping over multiple structures for a given
 * statistics array. Thus, every statistic string in an array should have the
 * same type and number of format specifiers, to be formatted by variadic
 * arguments to the i40e_add_stat_string() helper function.
 **/
struct i40e_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

/* Helper macro to define an i40e_stat structure with proper size and type.
 * Use this when defining constant statistics arrays. Note that @_type expects
 * only a type name and is used multiple times.
 */
#define I40E_STAT(_type, _name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = sizeof_field(_type, _stat), \
	.stat_offset = offsetof(_type, _stat) \
}

/* Helper macro for defining some statistics directly copied from the netdev
 * stats structure.
 */
#define I40E_NETDEV_STAT(_net_stat) \
	I40E_STAT(struct rtnl_link_stats64, #_net_stat, _net_stat)

/* Helper macro for defining some statistics related to queues */
#define I40E_QUEUE_STAT(_name, _stat) \
	I40E_STAT(struct i40e_ring, _name, _stat)

/* Stats associated with a Tx or Rx ring */
static const struct i40e_stats i40e_gstrings_queue_stats[] = {
	I40E_QUEUE_STAT("%s-%u.packets", stats.packets),
	I40E_QUEUE_STAT("%s-%u.bytes", stats.bytes),
};

/**
 * i40e_add_one_ethtool_stat - copy the stat into the supplied buffer
 * @data: location to store the stat value
 * @pointer: basis for where to copy from
 * @stat: the stat definition
 *
 * Copies the stat data defined by the pointer and stat structure pair into
 * the memory supplied as data. Used to implement i40e_add_ethtool_stats and
 * i40e_add_queue_stats. If the pointer is null, data will be zero'd.
 */
static void
i40e_add_one_ethtool_stat(u64 *data, void *pointer,
			  const struct i40e_stats *stat)
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
 * __i40e_add_ethtool_stats - copy stats into the ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location to copy stats from
 * @stats: array of stats to copy
 * @size: the size of the stats definition
 *
 * Copy the stats defined by the stats array using the pointer as a base into
 * the data buffer supplied by ethtool. Updates the data pointer to point to
 * the next empty location for successive calls to __i40e_add_ethtool_stats.
 * If pointer is null, set the data values to zero and update the pointer to
 * skip these stats.
 **/
static void
__i40e_add_ethtool_stats(u64 **data, void *pointer,
			 const struct i40e_stats stats[],
			 const unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		i40e_add_one_ethtool_stat((*data)++, pointer, &stats[i]);
}

/**
 * i40e_add_ethtool_stats - copy stats into ethtool supplied buffer
 * @data: ethtool stats buffer
 * @pointer: location where stats are stored
 * @stats: static const array of stat definitions
 *
 * Macro to ease the use of __i40e_add_ethtool_stats by taking a static
 * constant stats array and passing the ARRAY_SIZE(). This avoids typos by
 * ensuring that we pass the size associated with the given stats array.
 *
 * The parameter @stats is evaluated twice, so parameters with side effects
 * should be avoided.
 **/
#define i40e_add_ethtool_stats(data, pointer, stats) \
	__i40e_add_ethtool_stats(data, pointer, stats, ARRAY_SIZE(stats))

/**
 * i40e_add_queue_stats - copy queue statistics into supplied buffer
 * @data: ethtool stats buffer
 * @ring: the ring to copy
 *
 * Queue statistics must be copied while protected by
 * u64_stats_fetch_begin_irq, so we can't directly use i40e_add_ethtool_stats.
 * Assumes that queue stats are defined in i40e_gstrings_queue_stats. If the
 * ring pointer is null, zero out the queue stat values and update the data
 * pointer. Otherwise safely copy the stats from the ring into the supplied
 * buffer and update the data pointer when finished.
 *
 * This function expects to be called while under rcu_read_lock().
 **/
static void
i40e_add_queue_stats(u64 **data, struct i40e_ring *ring)
{
	const unsigned int size = ARRAY_SIZE(i40e_gstrings_queue_stats);
	const struct i40e_stats *stats = i40e_gstrings_queue_stats;
	unsigned int start;
	unsigned int i;

	/* To avoid invalid statistics values, ensure that we keep retrying
	 * the copy until we get a consistent value according to
	 * u64_stats_fetch_retry_irq. But first, make sure our ring is
	 * non-null before attempting to access its syncp.
	 */
	do {
		start = !ring ? 0 : u64_stats_fetch_begin_irq(&ring->syncp);
		for (i = 0; i < size; i++) {
			i40e_add_one_ethtool_stat(&(*data)[i], ring,
						  &stats[i]);
		}
	} while (ring && u64_stats_fetch_retry_irq(&ring->syncp, start));

	/* Once we successfully copy the stats in, update the data pointer */
	*data += size;
}

/**
 * __i40e_add_stat_strings - copy stat strings into ethtool buffer
 * @p: ethtool supplied buffer
 * @stats: stat definitions array
 * @size: size of the stats array
 *
 * Format and copy the strings described by stats into the buffer pointed at
 * by p.
 **/
static void __i40e_add_stat_strings(u8 **p, const struct i40e_stats stats[],
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
 * 40e_add_stat_strings - copy stat strings into ethtool buffer
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
#define i40e_add_stat_strings(p, stats, ...) \
	__i40e_add_stat_strings(p, stats, ARRAY_SIZE(stats), ## __VA_ARGS__)

#define I40E_PF_STAT(_name, _stat) \
	I40E_STAT(struct i40e_pf, _name, _stat)
#define I40E_VSI_STAT(_name, _stat) \
	I40E_STAT(struct i40e_vsi, _name, _stat)
#define I40E_VEB_STAT(_name, _stat) \
	I40E_STAT(struct i40e_veb, _name, _stat)
#define I40E_PFC_STAT(_name, _stat) \
	I40E_STAT(struct i40e_pfc_stats, _name, _stat)
#define I40E_QUEUE_STAT(_name, _stat) \
	I40E_STAT(struct i40e_ring, _name, _stat)

static const struct i40e_stats i40e_gstrings_net_stats[] = {
	I40E_NETDEV_STAT(rx_packets),
	I40E_NETDEV_STAT(tx_packets),
	I40E_NETDEV_STAT(rx_bytes),
	I40E_NETDEV_STAT(tx_bytes),
	I40E_NETDEV_STAT(rx_errors),
	I40E_NETDEV_STAT(tx_errors),
	I40E_NETDEV_STAT(rx_dropped),
	I40E_NETDEV_STAT(tx_dropped),
	I40E_NETDEV_STAT(collisions),
	I40E_NETDEV_STAT(rx_length_errors),
	I40E_NETDEV_STAT(rx_crc_errors),
};

static const struct i40e_stats i40e_gstrings_veb_stats[] = {
	I40E_VEB_STAT("veb.rx_bytes", stats.rx_bytes),
	I40E_VEB_STAT("veb.tx_bytes", stats.tx_bytes),
	I40E_VEB_STAT("veb.rx_unicast", stats.rx_unicast),
	I40E_VEB_STAT("veb.tx_unicast", stats.tx_unicast),
	I40E_VEB_STAT("veb.rx_multicast", stats.rx_multicast),
	I40E_VEB_STAT("veb.tx_multicast", stats.tx_multicast),
	I40E_VEB_STAT("veb.rx_broadcast", stats.rx_broadcast),
	I40E_VEB_STAT("veb.tx_broadcast", stats.tx_broadcast),
	I40E_VEB_STAT("veb.rx_discards", stats.rx_discards),
	I40E_VEB_STAT("veb.tx_discards", stats.tx_discards),
	I40E_VEB_STAT("veb.tx_errors", stats.tx_errors),
	I40E_VEB_STAT("veb.rx_unknown_protocol", stats.rx_unknown_protocol),
};

static const struct i40e_stats i40e_gstrings_veb_tc_stats[] = {
	I40E_VEB_STAT("veb.tc_%u_tx_packets", tc_stats.tc_tx_packets),
	I40E_VEB_STAT("veb.tc_%u_tx_bytes", tc_stats.tc_tx_bytes),
	I40E_VEB_STAT("veb.tc_%u_rx_packets", tc_stats.tc_rx_packets),
	I40E_VEB_STAT("veb.tc_%u_rx_bytes", tc_stats.tc_rx_bytes),
};

static const struct i40e_stats i40e_gstrings_misc_stats[] = {
	I40E_VSI_STAT("rx_unicast", eth_stats.rx_unicast),
	I40E_VSI_STAT("tx_unicast", eth_stats.tx_unicast),
	I40E_VSI_STAT("rx_multicast", eth_stats.rx_multicast),
	I40E_VSI_STAT("tx_multicast", eth_stats.tx_multicast),
	I40E_VSI_STAT("rx_broadcast", eth_stats.rx_broadcast),
	I40E_VSI_STAT("tx_broadcast", eth_stats.tx_broadcast),
	I40E_VSI_STAT("rx_unknown_protocol", eth_stats.rx_unknown_protocol),
	I40E_VSI_STAT("tx_linearize", tx_linearize),
	I40E_VSI_STAT("tx_force_wb", tx_force_wb),
	I40E_VSI_STAT("tx_busy", tx_busy),
	I40E_VSI_STAT("rx_alloc_fail", rx_buf_failed),
	I40E_VSI_STAT("rx_pg_alloc_fail", rx_page_failed),
};

/* These PF_STATs might look like duplicates of some NETDEV_STATs,
 * but they are separate.  This device supports Virtualization, and
 * as such might have several netdevs supporting VMDq and FCoE going
 * through a single port.  The NETDEV_STATs are for individual netdevs
 * seen at the top of the stack, and the PF_STATs are for the physical
 * function at the bottom of the stack hosting those netdevs.
 *
 * The PF_STATs are appended to the netdev stats only when ethtool -S
 * is queried on the base PF netdev, not on the VMDq or FCoE netdev.
 */
static const struct i40e_stats i40e_gstrings_stats[] = {
	I40E_PF_STAT("port.rx_bytes", stats.eth.rx_bytes),
	I40E_PF_STAT("port.tx_bytes", stats.eth.tx_bytes),
	I40E_PF_STAT("port.rx_unicast", stats.eth.rx_unicast),
	I40E_PF_STAT("port.tx_unicast", stats.eth.tx_unicast),
	I40E_PF_STAT("port.rx_multicast", stats.eth.rx_multicast),
	I40E_PF_STAT("port.tx_multicast", stats.eth.tx_multicast),
	I40E_PF_STAT("port.rx_broadcast", stats.eth.rx_broadcast),
	I40E_PF_STAT("port.tx_broadcast", stats.eth.tx_broadcast),
	I40E_PF_STAT("port.tx_errors", stats.eth.tx_errors),
	I40E_PF_STAT("port.rx_dropped", stats.eth.rx_discards),
	I40E_PF_STAT("port.tx_dropped_link_down", stats.tx_dropped_link_down),
	I40E_PF_STAT("port.rx_crc_errors", stats.crc_errors),
	I40E_PF_STAT("port.illegal_bytes", stats.illegal_bytes),
	I40E_PF_STAT("port.mac_local_faults", stats.mac_local_faults),
	I40E_PF_STAT("port.mac_remote_faults", stats.mac_remote_faults),
	I40E_PF_STAT("port.tx_timeout", tx_timeout_count),
	I40E_PF_STAT("port.rx_csum_bad", hw_csum_rx_error),
	I40E_PF_STAT("port.rx_length_errors", stats.rx_length_errors),
	I40E_PF_STAT("port.link_xon_rx", stats.link_xon_rx),
	I40E_PF_STAT("port.link_xoff_rx", stats.link_xoff_rx),
	I40E_PF_STAT("port.link_xon_tx", stats.link_xon_tx),
	I40E_PF_STAT("port.link_xoff_tx", stats.link_xoff_tx),
	I40E_PF_STAT("port.rx_size_64", stats.rx_size_64),
	I40E_PF_STAT("port.rx_size_127", stats.rx_size_127),
	I40E_PF_STAT("port.rx_size_255", stats.rx_size_255),
	I40E_PF_STAT("port.rx_size_511", stats.rx_size_511),
	I40E_PF_STAT("port.rx_size_1023", stats.rx_size_1023),
	I40E_PF_STAT("port.rx_size_1522", stats.rx_size_1522),
	I40E_PF_STAT("port.rx_size_big", stats.rx_size_big),
	I40E_PF_STAT("port.tx_size_64", stats.tx_size_64),
	I40E_PF_STAT("port.tx_size_127", stats.tx_size_127),
	I40E_PF_STAT("port.tx_size_255", stats.tx_size_255),
	I40E_PF_STAT("port.tx_size_511", stats.tx_size_511),
	I40E_PF_STAT("port.tx_size_1023", stats.tx_size_1023),
	I40E_PF_STAT("port.tx_size_1522", stats.tx_size_1522),
	I40E_PF_STAT("port.tx_size_big", stats.tx_size_big),
	I40E_PF_STAT("port.rx_undersize", stats.rx_undersize),
	I40E_PF_STAT("port.rx_fragments", stats.rx_fragments),
	I40E_PF_STAT("port.rx_oversize", stats.rx_oversize),
	I40E_PF_STAT("port.rx_jabber", stats.rx_jabber),
	I40E_PF_STAT("port.VF_admin_queue_requests", vf_aq_requests),
	I40E_PF_STAT("port.arq_overflows", arq_overflows),
	I40E_PF_STAT("port.tx_hwtstamp_timeouts", tx_hwtstamp_timeouts),
	I40E_PF_STAT("port.rx_hwtstamp_cleared", rx_hwtstamp_cleared),
	I40E_PF_STAT("port.tx_hwtstamp_skipped", tx_hwtstamp_skipped),
	I40E_PF_STAT("port.fdir_flush_cnt", fd_flush_cnt),
	I40E_PF_STAT("port.fdir_atr_match", stats.fd_atr_match),
	I40E_PF_STAT("port.fdir_atr_tunnel_match", stats.fd_atr_tunnel_match),
	I40E_PF_STAT("port.fdir_atr_status", stats.fd_atr_status),
	I40E_PF_STAT("port.fdir_sb_match", stats.fd_sb_match),
	I40E_PF_STAT("port.fdir_sb_status", stats.fd_sb_status),

	/* LPI stats */
	I40E_PF_STAT("port.tx_lpi_status", stats.tx_lpi_status),
	I40E_PF_STAT("port.rx_lpi_status", stats.rx_lpi_status),
	I40E_PF_STAT("port.tx_lpi_count", stats.tx_lpi_count),
	I40E_PF_STAT("port.rx_lpi_count", stats.rx_lpi_count),
};

struct i40e_pfc_stats {
	u64 priority_xon_rx;
	u64 priority_xoff_rx;
	u64 priority_xon_tx;
	u64 priority_xoff_tx;
	u64 priority_xon_2_xoff;
};

static const struct i40e_stats i40e_gstrings_pfc_stats[] = {
	I40E_PFC_STAT("port.tx_priority_%u_xon_tx", priority_xon_tx),
	I40E_PFC_STAT("port.tx_priority_%u_xoff_tx", priority_xoff_tx),
	I40E_PFC_STAT("port.rx_priority_%u_xon_rx", priority_xon_rx),
	I40E_PFC_STAT("port.rx_priority_%u_xoff_rx", priority_xoff_rx),
	I40E_PFC_STAT("port.rx_priority_%u_xon_2_xoff", priority_xon_2_xoff),
};

#define I40E_NETDEV_STATS_LEN	ARRAY_SIZE(i40e_gstrings_net_stats)

#define I40E_MISC_STATS_LEN	ARRAY_SIZE(i40e_gstrings_misc_stats)

#define I40E_VSI_STATS_LEN	(I40E_NETDEV_STATS_LEN + I40E_MISC_STATS_LEN)

#define I40E_PFC_STATS_LEN	(ARRAY_SIZE(i40e_gstrings_pfc_stats) * \
				 I40E_MAX_USER_PRIORITY)

#define I40E_VEB_STATS_LEN	(ARRAY_SIZE(i40e_gstrings_veb_stats) + \
				 (ARRAY_SIZE(i40e_gstrings_veb_tc_stats) * \
				  I40E_MAX_TRAFFIC_CLASS))

#define I40E_GLOBAL_STATS_LEN	ARRAY_SIZE(i40e_gstrings_stats)

#define I40E_PF_STATS_LEN	(I40E_GLOBAL_STATS_LEN + \
				 I40E_PFC_STATS_LEN + \
				 I40E_VEB_STATS_LEN + \
				 I40E_VSI_STATS_LEN)

/* Length of stats for a single queue */
#define I40E_QUEUE_STATS_LEN	ARRAY_SIZE(i40e_gstrings_queue_stats)

enum i40e_ethtool_test_id {
	I40E_ETH_TEST_REG = 0,
	I40E_ETH_TEST_EEPROM,
	I40E_ETH_TEST_INTR,
	I40E_ETH_TEST_LINK,
};

static const char i40e_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)",
	"Eeprom test    (offline)",
	"Interrupt test (offline)",
	"Link test   (on/offline)"
};

#define I40E_TEST_LEN (sizeof(i40e_gstrings_test) / ETH_GSTRING_LEN)

struct i40e_priv_flags {
	char flag_string[ETH_GSTRING_LEN];
	u64 flag;
	bool read_only;
};

#define I40E_PRIV_FLAG(_name, _flag, _read_only) { \
	.flag_string = _name, \
	.flag = _flag, \
	.read_only = _read_only, \
}

static const struct i40e_priv_flags i40e_gstrings_priv_flags[] = {
	/* NOTE: MFP setting cannot be changed */
	I40E_PRIV_FLAG("MFP", I40E_FLAG_MFP_ENABLED, 1),
	I40E_PRIV_FLAG("total-port-shutdown",
		       I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED, 1),
	I40E_PRIV_FLAG("LinkPolling", I40E_FLAG_LINK_POLLING_ENABLED, 0),
	I40E_PRIV_FLAG("flow-director-atr", I40E_FLAG_FD_ATR_ENABLED, 0),
	I40E_PRIV_FLAG("veb-stats", I40E_FLAG_VEB_STATS_ENABLED, 0),
	I40E_PRIV_FLAG("hw-atr-eviction", I40E_FLAG_HW_ATR_EVICT_ENABLED, 0),
	I40E_PRIV_FLAG("link-down-on-close",
		       I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED, 0),
	I40E_PRIV_FLAG("legacy-rx", I40E_FLAG_LEGACY_RX, 0),
	I40E_PRIV_FLAG("disable-source-pruning",
		       I40E_FLAG_SOURCE_PRUNING_DISABLED, 0),
	I40E_PRIV_FLAG("disable-fw-lldp", I40E_FLAG_DISABLE_FW_LLDP, 0),
	I40E_PRIV_FLAG("rs-fec", I40E_FLAG_RS_FEC, 0),
	I40E_PRIV_FLAG("base-r-fec", I40E_FLAG_BASE_R_FEC, 0),
};

#define I40E_PRIV_FLAGS_STR_LEN ARRAY_SIZE(i40e_gstrings_priv_flags)

/* Private flags with a global effect, restricted to PF 0 */
static const struct i40e_priv_flags i40e_gl_gstrings_priv_flags[] = {
	I40E_PRIV_FLAG("vf-true-promisc-support",
		       I40E_FLAG_TRUE_PROMISC_SUPPORT, 0),
};

#define I40E_GL_PRIV_FLAGS_STR_LEN ARRAY_SIZE(i40e_gl_gstrings_priv_flags)

/**
 * i40e_partition_setting_complaint - generic complaint for MFP restriction
 * @pf: the PF struct
 **/
static void i40e_partition_setting_complaint(struct i40e_pf *pf)
{
	dev_info(&pf->pdev->dev,
		 "The link settings are allowed to be changed only from the first partition of a given port. Please switch to the first partition in order to change the setting.\n");
}

/**
 * i40e_phy_type_to_ethtool - convert the phy_types to ethtool link modes
 * @pf: PF struct with phy_types
 * @ks: ethtool link ksettings struct to fill out
 *
 **/
static void i40e_phy_type_to_ethtool(struct i40e_pf *pf,
				     struct ethtool_link_ksettings *ks)
{
	struct i40e_link_status *hw_link_info = &pf->hw.phy.link_info;
	u64 phy_types = pf->hw.phy.phy_types;

	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_zero_link_mode(ks, advertising);

	if (phy_types & I40E_CAP_PHY_TYPE_SGMII) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseT_Full);
		if (pf->hw_features & I40E_HW_100M_SGMII_CAPABLE) {
			ethtool_link_ksettings_add_link_mode(ks, supported,
							     100baseT_Full);
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     100baseT_Full);
		}
	}
	if (phy_types & I40E_CAP_PHY_TYPE_XAUI ||
	    phy_types & I40E_CAP_PHY_TYPE_XFI ||
	    phy_types & I40E_CAP_PHY_TYPE_SFI ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_SFPP_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_AOC) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_T) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_2_5GBASE_T) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_2_5GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     2500baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_5GBASE_T) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     5000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_5GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     5000baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_XLAUI ||
	    phy_types & I40E_CAP_PHY_TYPE_XLPPI ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_AOC)
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_40GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     40000baseCR4_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_100BASE_TX) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_100MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     100baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_1000BASE_T) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseT_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_SR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseSR4_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_LR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseLR4_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_40GBASE_KR4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseKR4_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_20GBASE_KR2) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     20000baseKR2_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_20GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     20000baseKR2_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_KX4) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKX4_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseKX4_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_KR &&
	    !(pf->hw_features & I40E_HW_HAVE_CRT_RETIMER)) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseKR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_1000BASE_KX &&
	    !(pf->hw_features & I40E_HW_HAVE_CRT_RETIMER)) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseKX_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseKX_Full);
	}
	/* need to add 25G PHY types */
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_KR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseKR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseKR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_CR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseCR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_LR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseSR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_AOC ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_ACC) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_25GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     25000baseCR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_25GBASE_KR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_CR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_LR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_AOC ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_ACC) {
		ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
		ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_25GB) {
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     FEC_NONE);
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     FEC_RS);
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     FEC_BASER);
		}
	}
	/* need to add new 10G PHY types */
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1 ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1_CU) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseCR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseCR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_SR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseSR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_10GBASE_LR) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseLR_Full);
	}
	if (phy_types & I40E_CAP_PHY_TYPE_1000BASE_SX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_LX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_T_OPTICAL) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseX_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseX_Full);
	}
	/* Autoneg PHY types */
	if (phy_types & I40E_CAP_PHY_TYPE_SGMII ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_KR4 ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_40GBASE_CR4 ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_LR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_KR ||
	    phy_types & I40E_CAP_PHY_TYPE_25GBASE_CR ||
	    phy_types & I40E_CAP_PHY_TYPE_20GBASE_KR2 ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_SR ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_LR ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_KX4 ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_KR ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1_CU ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_CR1 ||
	    phy_types & I40E_CAP_PHY_TYPE_10GBASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_5GBASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_2_5GBASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_T_OPTICAL ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_T ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_SX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_LX ||
	    phy_types & I40E_CAP_PHY_TYPE_1000BASE_KX ||
	    phy_types & I40E_CAP_PHY_TYPE_100BASE_TX) {
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Autoneg);
	}
}

/**
 * i40e_get_settings_link_up_fec - Get the FEC mode encoding from mask
 * @req_fec_info: mask request FEC info
 * @ks: ethtool ksettings to fill in
 **/
static void i40e_get_settings_link_up_fec(u8 req_fec_info,
					  struct ethtool_link_ksettings *ks)
{
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);

	if ((I40E_AQ_SET_FEC_REQUEST_RS & req_fec_info) &&
	    (I40E_AQ_SET_FEC_REQUEST_KR & req_fec_info)) {
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_NONE);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_RS);
	} else if (I40E_AQ_SET_FEC_REQUEST_RS & req_fec_info) {
		ethtool_link_ksettings_add_link_mode(ks, advertising, FEC_RS);
	} else if (I40E_AQ_SET_FEC_REQUEST_KR & req_fec_info) {
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_BASER);
	} else {
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     FEC_NONE);
	}
}

/**
 * i40e_get_settings_link_up - Get the Link settings for when link is up
 * @hw: hw structure
 * @ks: ethtool ksettings to fill in
 * @netdev: network interface device structure
 * @pf: pointer to physical function struct
 **/
static void i40e_get_settings_link_up(struct i40e_hw *hw,
				      struct ethtool_link_ksettings *ks,
				      struct net_device *netdev,
				      struct i40e_pf *pf)
{
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	struct ethtool_link_ksettings cap_ksettings;
	u32 link_speed = hw_link_info->link_speed;

	/* Initialize supported and advertised settings based on phy settings */
	switch (hw_link_info->phy_type) {
	case I40E_PHY_TYPE_40GBASE_CR4:
	case I40E_PHY_TYPE_40GBASE_CR4_CU:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseCR4_Full);
		break;
	case I40E_PHY_TYPE_XLAUI:
	case I40E_PHY_TYPE_XLPPI:
	case I40E_PHY_TYPE_40GBASE_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseCR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseCR4_Full);
		break;
	case I40E_PHY_TYPE_40GBASE_SR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseSR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseSR4_Full);
		break;
	case I40E_PHY_TYPE_40GBASE_LR4:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseLR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseLR4_Full);
		break;
	case I40E_PHY_TYPE_25GBASE_SR:
	case I40E_PHY_TYPE_25GBASE_LR:
	case I40E_PHY_TYPE_10GBASE_SR:
	case I40E_PHY_TYPE_10GBASE_LR:
	case I40E_PHY_TYPE_1000BASE_SX:
	case I40E_PHY_TYPE_1000BASE_LX:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseSR_Full);
		i40e_get_settings_link_up_fec(hw_link_info->req_fec_info, ks);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseSR_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseLR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseLR_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseX_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     1000baseX_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		if (hw_link_info->module_type[2] &
		    I40E_MODULE_TYPE_1000BASE_SX ||
		    hw_link_info->module_type[2] &
		    I40E_MODULE_TYPE_1000BASE_LX) {
			ethtool_link_ksettings_add_link_mode(ks, supported,
							     1000baseT_Full);
			if (hw_link_info->requested_speeds &
			    I40E_LINK_SPEED_1GB)
				ethtool_link_ksettings_add_link_mode(
				     ks, advertising, 1000baseT_Full);
		}
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
		break;
	case I40E_PHY_TYPE_10GBASE_T:
	case I40E_PHY_TYPE_5GBASE_T:
	case I40E_PHY_TYPE_2_5GBASE_T:
	case I40E_PHY_TYPE_1000BASE_T:
	case I40E_PHY_TYPE_100BASE_TX:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     5000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     2500baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     100baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_5GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     5000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_2_5GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     2500baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_100MB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     100baseT_Full);
		break;
	case I40E_PHY_TYPE_1000BASE_T_OPTICAL:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     1000baseT_Full);
		break;
	case I40E_PHY_TYPE_10GBASE_CR1_CU:
	case I40E_PHY_TYPE_10GBASE_CR1:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseT_Full);
		break;
	case I40E_PHY_TYPE_XAUI:
	case I40E_PHY_TYPE_XFI:
	case I40E_PHY_TYPE_SFI:
	case I40E_PHY_TYPE_10GBASE_SFPP_CU:
	case I40E_PHY_TYPE_10GBASE_AOC:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_10GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     10000baseT_Full);
		i40e_get_settings_link_up_fec(hw_link_info->req_fec_info, ks);
		break;
	case I40E_PHY_TYPE_SGMII:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseT_Full);
		if (hw_link_info->requested_speeds & I40E_LINK_SPEED_1GB)
			ethtool_link_ksettings_add_link_mode(ks, advertising,
							     1000baseT_Full);
		if (pf->hw_features & I40E_HW_100M_SGMII_CAPABLE) {
			ethtool_link_ksettings_add_link_mode(ks, supported,
							     100baseT_Full);
			if (hw_link_info->requested_speeds &
			    I40E_LINK_SPEED_100MB)
				ethtool_link_ksettings_add_link_mode(
				      ks, advertising, 100baseT_Full);
		}
		break;
	case I40E_PHY_TYPE_40GBASE_KR4:
	case I40E_PHY_TYPE_25GBASE_KR:
	case I40E_PHY_TYPE_20GBASE_KR2:
	case I40E_PHY_TYPE_10GBASE_KR:
	case I40E_PHY_TYPE_10GBASE_KX4:
	case I40E_PHY_TYPE_1000BASE_KX:
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     40000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     20000baseKR2_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseKX4_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     1000baseKX_Full);
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     40000baseKR4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseKR_Full);
		i40e_get_settings_link_up_fec(hw_link_info->req_fec_info, ks);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     20000baseKR2_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseKR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseKX4_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     1000baseKX_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		break;
	case I40E_PHY_TYPE_25GBASE_CR:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseCR_Full);
		i40e_get_settings_link_up_fec(hw_link_info->req_fec_info, ks);

		break;
	case I40E_PHY_TYPE_25GBASE_AOC:
	case I40E_PHY_TYPE_25GBASE_ACC:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported,
						     25000baseCR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     25000baseCR_Full);
		i40e_get_settings_link_up_fec(hw_link_info->req_fec_info, ks);

		ethtool_link_ksettings_add_link_mode(ks, supported,
						     10000baseCR_Full);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     10000baseCR_Full);
		break;
	default:
		/* if we got here and link is up something bad is afoot */
		netdev_info(netdev,
			    "WARNING: Link is up but PHY type 0x%x is not recognized.\n",
			    hw_link_info->phy_type);
	}

	/* Now that we've worked out everything that could be supported by the
	 * current PHY type, get what is supported by the NVM and intersect
	 * them to get what is truly supported
	 */
	memset(&cap_ksettings, 0, sizeof(struct ethtool_link_ksettings));
	i40e_phy_type_to_ethtool(pf, &cap_ksettings);
	ethtool_intersect_link_masks(ks, &cap_ksettings);

	/* Set speed and duplex */
	switch (link_speed) {
	case I40E_LINK_SPEED_40GB:
		ks->base.speed = SPEED_40000;
		break;
	case I40E_LINK_SPEED_25GB:
		ks->base.speed = SPEED_25000;
		break;
	case I40E_LINK_SPEED_20GB:
		ks->base.speed = SPEED_20000;
		break;
	case I40E_LINK_SPEED_10GB:
		ks->base.speed = SPEED_10000;
		break;
	case I40E_LINK_SPEED_5GB:
		ks->base.speed = SPEED_5000;
		break;
	case I40E_LINK_SPEED_2_5GB:
		ks->base.speed = SPEED_2500;
		break;
	case I40E_LINK_SPEED_1GB:
		ks->base.speed = SPEED_1000;
		break;
	case I40E_LINK_SPEED_100MB:
		ks->base.speed = SPEED_100;
		break;
	default:
		ks->base.speed = SPEED_UNKNOWN;
		break;
	}
	ks->base.duplex = DUPLEX_FULL;
}

/**
 * i40e_get_settings_link_down - Get the Link settings for when link is down
 * @hw: hw structure
 * @ks: ethtool ksettings to fill in
 * @pf: pointer to physical function struct
 *
 * Reports link settings that can be determined when link is down
 **/
static void i40e_get_settings_link_down(struct i40e_hw *hw,
					struct ethtool_link_ksettings *ks,
					struct i40e_pf *pf)
{
	/* link is down and the driver needs to fall back on
	 * supported phy types to figure out what info to display
	 */
	i40e_phy_type_to_ethtool(pf, ks);

	/* With no link speed and duplex are unknown */
	ks->base.speed = SPEED_UNKNOWN;
	ks->base.duplex = DUPLEX_UNKNOWN;
}

/**
 * i40e_get_link_ksettings - Get Link Speed and Duplex settings
 * @netdev: network interface device structure
 * @ks: ethtool ksettings
 *
 * Reports speed/duplex settings based on media_type
 **/
static int i40e_get_link_ksettings(struct net_device *netdev,
				   struct ethtool_link_ksettings *ks)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	bool link_up = hw_link_info->link_info & I40E_AQ_LINK_UP;

	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_zero_link_mode(ks, advertising);

	if (link_up)
		i40e_get_settings_link_up(hw, ks, netdev, pf);
	else
		i40e_get_settings_link_down(hw, ks, pf);

	/* Now set the settings that don't rely on link being up/down */
	/* Set autoneg settings */
	ks->base.autoneg = ((hw_link_info->an_info & I40E_AQ_AN_COMPLETED) ?
			    AUTONEG_ENABLE : AUTONEG_DISABLE);

	/* Set media type settings */
	switch (hw->phy.media_type) {
	case I40E_MEDIA_TYPE_BACKPLANE:
		ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, supported, Backplane);
		ethtool_link_ksettings_add_link_mode(ks, advertising, Autoneg);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Backplane);
		ks->base.port = PORT_NONE;
		break;
	case I40E_MEDIA_TYPE_BASET:
		ethtool_link_ksettings_add_link_mode(ks, supported, TP);
		ethtool_link_ksettings_add_link_mode(ks, advertising, TP);
		ks->base.port = PORT_TP;
		break;
	case I40E_MEDIA_TYPE_DA:
	case I40E_MEDIA_TYPE_CX4:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ks, advertising, FIBRE);
		ks->base.port = PORT_DA;
		break;
	case I40E_MEDIA_TYPE_FIBER:
		ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(ks, advertising, FIBRE);
		ks->base.port = PORT_FIBRE;
		break;
	case I40E_MEDIA_TYPE_UNKNOWN:
	default:
		ks->base.port = PORT_OTHER;
		break;
	}

	/* Set flow control settings */
	ethtool_link_ksettings_add_link_mode(ks, supported, Pause);

	switch (hw->fc.requested_mode) {
	case I40E_FC_FULL:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		break;
	case I40E_FC_TX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	case I40E_FC_RX_PAUSE:
		ethtool_link_ksettings_add_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_add_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	default:
		ethtool_link_ksettings_del_link_mode(ks, advertising, Pause);
		ethtool_link_ksettings_del_link_mode(ks, advertising,
						     Asym_Pause);
		break;
	}

	return 0;
}

/**
 * i40e_set_link_ksettings - Set Speed and Duplex
 * @netdev: network interface device structure
 * @ks: ethtool ksettings
 *
 * Set speed/duplex per media_types advertised/forced
 **/
static int i40e_set_link_ksettings(struct net_device *netdev,
				   const struct ethtool_link_ksettings *ks)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct ethtool_link_ksettings safe_ks;
	struct ethtool_link_ksettings copy_ks;
	struct i40e_aq_set_phy_config config;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	bool autoneg_changed = false;
	i40e_status status = 0;
	int timeout = 50;
	int err = 0;
	u8 autoneg;

	/* Changing port settings is not supported if this isn't the
	 * port's controlling PF
	 */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}
	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;
	if (hw->phy.media_type != I40E_MEDIA_TYPE_BASET &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_FIBER &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_BACKPLANE &&
	    hw->phy.media_type != I40E_MEDIA_TYPE_DA &&
	    hw->phy.link_info.link_info & I40E_AQ_LINK_UP)
		return -EOPNOTSUPP;
	if (hw->device_id == I40E_DEV_ID_KX_B ||
	    hw->device_id == I40E_DEV_ID_KX_C ||
	    hw->device_id == I40E_DEV_ID_20G_KR2 ||
	    hw->device_id == I40E_DEV_ID_20G_KR2_A ||
	    hw->device_id == I40E_DEV_ID_25G_B ||
	    hw->device_id == I40E_DEV_ID_KX_X722) {
		netdev_info(netdev, "Changing settings is not supported on backplane.\n");
		return -EOPNOTSUPP;
	}

	/* copy the ksettings to copy_ks to avoid modifying the origin */
	memcpy(&copy_ks, ks, sizeof(struct ethtool_link_ksettings));

	/* save autoneg out of ksettings */
	autoneg = copy_ks.base.autoneg;

	/* get our own copy of the bits to check against */
	memset(&safe_ks, 0, sizeof(struct ethtool_link_ksettings));
	safe_ks.base.cmd = copy_ks.base.cmd;
	safe_ks.base.link_mode_masks_nwords =
		copy_ks.base.link_mode_masks_nwords;
	i40e_get_link_ksettings(netdev, &safe_ks);

	/* Get link modes supported by hardware and check against modes
	 * requested by the user.  Return an error if unsupported mode was set.
	 */
	if (!bitmap_subset(copy_ks.link_modes.advertising,
			   safe_ks.link_modes.supported,
			   __ETHTOOL_LINK_MODE_MASK_NBITS))
		return -EINVAL;

	/* set autoneg back to what it currently is */
	copy_ks.base.autoneg = safe_ks.base.autoneg;

	/* If copy_ks.base and safe_ks.base are not the same now, then they are
	 * trying to set something that we do not support.
	 */
	if (memcmp(&copy_ks.base, &safe_ks.base,
		   sizeof(struct ethtool_link_settings)))
		return -EOPNOTSUPP;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	/* Get the current phy config */
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	/* Copy abilities to config in case autoneg is not
	 * set below
	 */
	memset(&config, 0, sizeof(struct i40e_aq_set_phy_config));
	config.abilities = abilities.abilities;

	/* Check autoneg */
	if (autoneg == AUTONEG_ENABLE) {
		/* If autoneg was not already enabled */
		if (!(hw->phy.link_info.an_info & I40E_AQ_AN_COMPLETED)) {
			/* If autoneg is not supported, return error */
			if (!ethtool_link_ksettings_test_link_mode(&safe_ks,
								   supported,
								   Autoneg)) {
				netdev_info(netdev, "Autoneg not supported on this phy\n");
				err = -EINVAL;
				goto done;
			}
			/* Autoneg is allowed to change */
			config.abilities = abilities.abilities |
					   I40E_AQ_PHY_ENABLE_AN;
			autoneg_changed = true;
		}
	} else {
		/* If autoneg is currently enabled */
		if (hw->phy.link_info.an_info & I40E_AQ_AN_COMPLETED) {
			/* If autoneg is supported 10GBASE_T is the only PHY
			 * that can disable it, so otherwise return error
			 */
			if (ethtool_link_ksettings_test_link_mode(&safe_ks,
								  supported,
								  Autoneg) &&
			    hw->phy.link_info.phy_type !=
			    I40E_PHY_TYPE_10GBASE_T) {
				netdev_info(netdev, "Autoneg cannot be disabled on this phy\n");
				err = -EINVAL;
				goto done;
			}
			/* Autoneg is allowed to change */
			config.abilities = abilities.abilities &
					   ~I40E_AQ_PHY_ENABLE_AN;
			autoneg_changed = true;
		}
	}

	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  100baseT_Full))
		config.link_speed |= I40E_LINK_SPEED_100MB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseT_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseX_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  1000baseKX_Full))
		config.link_speed |= I40E_LINK_SPEED_1GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseT_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseKX4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseKR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseCR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseSR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  10000baseLR_Full))
		config.link_speed |= I40E_LINK_SPEED_10GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  2500baseT_Full))
		config.link_speed |= I40E_LINK_SPEED_2_5GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  5000baseT_Full))
		config.link_speed |= I40E_LINK_SPEED_5GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  20000baseKR2_Full))
		config.link_speed |= I40E_LINK_SPEED_20GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseCR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseKR_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  25000baseSR_Full))
		config.link_speed |= I40E_LINK_SPEED_25GB;
	if (ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseKR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseCR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseSR4_Full) ||
	    ethtool_link_ksettings_test_link_mode(ks, advertising,
						  40000baseLR4_Full))
		config.link_speed |= I40E_LINK_SPEED_40GB;

	/* If speed didn't get set, set it to what it currently is.
	 * This is needed because if advertise is 0 (as it is when autoneg
	 * is disabled) then speed won't get set.
	 */
	if (!config.link_speed)
		config.link_speed = abilities.link_speed;
	if (autoneg_changed || abilities.link_speed != config.link_speed) {
		/* copy over the rest of the abilities */
		config.phy_type = abilities.phy_type;
		config.phy_type_ext = abilities.phy_type_ext;
		config.eee_capability = abilities.eee_capability;
		config.eeer = abilities.eeer_val;
		config.low_power_ctrl = abilities.d3_lpan;
		config.fec_config = abilities.fec_cfg_curr_mod_ext_info &
				    I40E_AQ_PHY_FEC_CONFIG_MASK;

		/* save the requested speeds */
		hw->phy.link_info.requested_speeds = config.link_speed;
		/* set link and auto negotiation so changes take effect */
		config.abilities |= I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
		/* If link is up put link down */
		if (hw->phy.link_info.link_info & I40E_AQ_LINK_UP) {
			/* Tell the OS link is going down, the link will go
			 * back up when fw says it is ready asynchronously
			 */
			i40e_print_link_message(vsi, false);
			netif_carrier_off(netdev);
			netif_tx_stop_all_queues(netdev);
		}

		/* make the aq call */
		status = i40e_aq_set_phy_config(hw, &config, NULL);
		if (status) {
			netdev_info(netdev,
				    "Set phy config failed, err %s aq_err %s\n",
				    i40e_stat_str(hw, status),
				    i40e_aq_str(hw, hw->aq.asq_last_status));
			err = -EAGAIN;
			goto done;
		}

		status = i40e_update_link_info(hw);
		if (status)
			netdev_dbg(netdev,
				   "Updating link info failed with err %s aq_err %s\n",
				   i40e_stat_str(hw, status),
				   i40e_aq_str(hw, hw->aq.asq_last_status));

	} else {
		netdev_info(netdev, "Nothing changed, exiting without setting anything.\n");
	}

done:
	clear_bit(__I40E_CONFIG_BUSY, pf->state);

	return err;
}

static int i40e_set_fec_cfg(struct net_device *netdev, u8 fec_cfg)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	i40e_status status = 0;
	u32 flags = 0;
	int err = 0;

	flags = READ_ONCE(pf->flags);
	i40e_set_fec_in_flags(fec_cfg, &flags);

	/* Get the current phy config */
	memset(&abilities, 0, sizeof(abilities));
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	if (abilities.fec_cfg_curr_mod_ext_info != fec_cfg) {
		struct i40e_aq_set_phy_config config;

		memset(&config, 0, sizeof(config));
		config.phy_type = abilities.phy_type;
		config.abilities = abilities.abilities;
		config.phy_type_ext = abilities.phy_type_ext;
		config.link_speed = abilities.link_speed;
		config.eee_capability = abilities.eee_capability;
		config.eeer = abilities.eeer_val;
		config.low_power_ctrl = abilities.d3_lpan;
		config.fec_config = fec_cfg & I40E_AQ_PHY_FEC_CONFIG_MASK;
		status = i40e_aq_set_phy_config(hw, &config, NULL);
		if (status) {
			netdev_info(netdev,
				    "Set phy config failed, err %s aq_err %s\n",
				    i40e_stat_str(hw, status),
				    i40e_aq_str(hw, hw->aq.asq_last_status));
			err = -EAGAIN;
			goto done;
		}
		pf->flags = flags;
		status = i40e_update_link_info(hw);
		if (status)
			/* debug level message only due to relation to the link
			 * itself rather than to the FEC settings
			 * (e.g. no physical connection etc.)
			 */
			netdev_dbg(netdev,
				   "Updating link info failed with err %s aq_err %s\n",
				   i40e_stat_str(hw, status),
				   i40e_aq_str(hw, hw->aq.asq_last_status));
	}

done:
	return err;
}

static int i40e_get_fec_param(struct net_device *netdev,
			      struct ethtool_fecparam *fecparam)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp abilities;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	i40e_status status = 0;
	int err = 0;
	u8 fec_cfg;

	/* Get the current phy config */
	memset(&abilities, 0, sizeof(abilities));
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status) {
		err = -EAGAIN;
		goto done;
	}

	fecparam->fec = 0;
	fec_cfg = abilities.fec_cfg_curr_mod_ext_info;
	if (fec_cfg & I40E_AQ_SET_FEC_AUTO)
		fecparam->fec |= ETHTOOL_FEC_AUTO;
	else if (fec_cfg & (I40E_AQ_SET_FEC_REQUEST_RS |
		 I40E_AQ_SET_FEC_ABILITY_RS))
		fecparam->fec |= ETHTOOL_FEC_RS;
	else if (fec_cfg & (I40E_AQ_SET_FEC_REQUEST_KR |
		 I40E_AQ_SET_FEC_ABILITY_KR))
		fecparam->fec |= ETHTOOL_FEC_BASER;
	if (fec_cfg == 0)
		fecparam->fec |= ETHTOOL_FEC_OFF;

	if (hw->phy.link_info.fec_info & I40E_AQ_CONFIG_FEC_KR_ENA)
		fecparam->active_fec = ETHTOOL_FEC_BASER;
	else if (hw->phy.link_info.fec_info & I40E_AQ_CONFIG_FEC_RS_ENA)
		fecparam->active_fec = ETHTOOL_FEC_RS;
	else
		fecparam->active_fec = ETHTOOL_FEC_OFF;
done:
	return err;
}

static int i40e_set_fec_param(struct net_device *netdev,
			      struct ethtool_fecparam *fecparam)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u8 fec_cfg = 0;

	if (hw->device_id != I40E_DEV_ID_25G_SFP28 &&
	    hw->device_id != I40E_DEV_ID_25G_B &&
	    hw->device_id != I40E_DEV_ID_KX_X722)
		return -EPERM;

	if (hw->mac.type == I40E_MAC_X722 &&
	    !(hw->flags & I40E_HW_FLAG_X722_FEC_REQUEST_CAPABLE)) {
		netdev_err(netdev, "Setting FEC encoding not supported by firmware. Please update the NVM image.\n");
		return -EOPNOTSUPP;
	}

	switch (fecparam->fec) {
	case ETHTOOL_FEC_AUTO:
		fec_cfg = I40E_AQ_SET_FEC_AUTO;
		break;
	case ETHTOOL_FEC_RS:
		fec_cfg = (I40E_AQ_SET_FEC_REQUEST_RS |
			     I40E_AQ_SET_FEC_ABILITY_RS);
		break;
	case ETHTOOL_FEC_BASER:
		fec_cfg = (I40E_AQ_SET_FEC_REQUEST_KR |
			     I40E_AQ_SET_FEC_ABILITY_KR);
		break;
	case ETHTOOL_FEC_OFF:
	case ETHTOOL_FEC_NONE:
		fec_cfg = 0;
		break;
	default:
		dev_warn(&pf->pdev->dev, "Unsupported FEC mode: %d",
			 fecparam->fec);
		return -EINVAL;
	}

	return i40e_set_fec_cfg(netdev, fec_cfg);
}

static int i40e_nway_reset(struct net_device *netdev)
{
	/* restart autonegotiation */
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	bool link_up = hw->phy.link_info.link_info & I40E_AQ_LINK_UP;
	i40e_status ret = 0;

	ret = i40e_aq_set_link_restart_an(hw, link_up, NULL);
	if (ret) {
		netdev_info(netdev, "link restart failed, err %s aq_err %s\n",
			    i40e_stat_str(hw, ret),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		return -EIO;
	}

	return 0;
}

/**
 * i40e_get_pauseparam -  Get Flow Control status
 * @netdev: netdevice structure
 * @pause: buffer to return pause parameters
 *
 * Return tx/rx-pause status
 **/
static void i40e_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	struct i40e_dcbx_config *dcbx_cfg = &hw->local_dcbx_config;

	pause->autoneg =
		((hw_link_info->an_info & I40E_AQ_AN_COMPLETED) ?
		  AUTONEG_ENABLE : AUTONEG_DISABLE);

	/* PFC enabled so report LFC as off */
	if (dcbx_cfg->pfc.pfcenable) {
		pause->rx_pause = 0;
		pause->tx_pause = 0;
		return;
	}

	if (hw->fc.current_mode == I40E_FC_RX_PAUSE) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == I40E_FC_TX_PAUSE) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == I40E_FC_FULL) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

/**
 * i40e_set_pauseparam - Set Flow Control parameter
 * @netdev: network interface device structure
 * @pause: return tx/rx flow control status
 **/
static int i40e_set_pauseparam(struct net_device *netdev,
			       struct ethtool_pauseparam *pause)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;
	struct i40e_dcbx_config *dcbx_cfg = &hw->local_dcbx_config;
	bool link_up = hw_link_info->link_info & I40E_AQ_LINK_UP;
	i40e_status status;
	u8 aq_failures;
	int err = 0;
	u32 is_an;

	/* Changing the port's flow control is not supported if this isn't the
	 * port's controlling PF
	 */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}

	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;

	is_an = hw_link_info->an_info & I40E_AQ_AN_COMPLETED;
	if (pause->autoneg != is_an) {
		netdev_info(netdev, "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	/* If we have link and don't have autoneg */
	if (!test_bit(__I40E_DOWN, pf->state) && !is_an) {
		/* Send message that it might not necessarily work*/
		netdev_info(netdev, "Autoneg did not complete so changing settings may not result in an actual change.\n");
	}

	if (dcbx_cfg->pfc.pfcenable) {
		netdev_info(netdev,
			    "Priority flow control enabled. Cannot set link flow control.\n");
		return -EOPNOTSUPP;
	}

	if (pause->rx_pause && pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_FULL;
	else if (pause->rx_pause && !pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_RX_PAUSE;
	else if (!pause->rx_pause && pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_TX_PAUSE;
	else if (!pause->rx_pause && !pause->tx_pause)
		hw->fc.requested_mode = I40E_FC_NONE;
	else
		return -EINVAL;

	/* Tell the OS link is going down, the link will go back up when fw
	 * says it is ready asynchronously
	 */
	i40e_print_link_message(vsi, false);
	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	/* Set the fc mode and only restart an if link is up*/
	status = i40e_set_fc(hw, &aq_failures, link_up);

	if (aq_failures & I40E_SET_FC_AQ_FAIL_GET) {
		netdev_info(netdev, "Set fc failed on the get_phy_capabilities call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}
	if (aq_failures & I40E_SET_FC_AQ_FAIL_SET) {
		netdev_info(netdev, "Set fc failed on the set_phy_config call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}
	if (aq_failures & I40E_SET_FC_AQ_FAIL_UPDATE) {
		netdev_info(netdev, "Set fc failed on the get_link_info call with err %s aq_err %s\n",
			    i40e_stat_str(hw, status),
			    i40e_aq_str(hw, hw->aq.asq_last_status));
		err = -EAGAIN;
	}

	if (!test_bit(__I40E_DOWN, pf->state) && is_an) {
		/* Give it a little more time to try to come back */
		msleep(75);
		if (!test_bit(__I40E_DOWN, pf->state))
			return i40e_nway_reset(netdev);
	}

	return err;
}

static u32 i40e_get_msglevel(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	u32 debug_mask = pf->hw.debug_mask;

	if (debug_mask)
		netdev_info(netdev, "i40e debug_mask: 0x%08X\n", debug_mask);

	return pf->msg_enable;
}

static void i40e_set_msglevel(struct net_device *netdev, u32 data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	if (I40E_DEBUG_USER & data)
		pf->hw.debug_mask = data;
	else
		pf->msg_enable = data;
}

static int i40e_get_regs_len(struct net_device *netdev)
{
	int reg_count = 0;
	int i;

	for (i = 0; i40e_reg_list[i].offset != 0; i++)
		reg_count += i40e_reg_list[i].elements;

	return reg_count * sizeof(u32);
}

static void i40e_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			  void *p)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 *reg_buf = p;
	unsigned int i, j, ri;
	u32 reg;

	/* Tell ethtool which driver-version-specific regs output we have.
	 *
	 * At some point, if we have ethtool doing special formatting of
	 * this data, it will rely on this version number to know how to
	 * interpret things.  Hence, this needs to be updated if/when the
	 * diags register table is changed.
	 */
	regs->version = 1;

	/* loop through the diags reg table for what to print */
	ri = 0;
	for (i = 0; i40e_reg_list[i].offset != 0; i++) {
		for (j = 0; j < i40e_reg_list[i].elements; j++) {
			reg = i40e_reg_list[i].offset
				+ (j * i40e_reg_list[i].stride);
			reg_buf[ri++] = rd32(hw, reg);
		}
	}

}

static int i40e_get_eeprom(struct net_device *netdev,
			   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_pf *pf = np->vsi->back;
	int ret_val = 0, len, offset;
	u8 *eeprom_buff;
	u16 i, sectors;
	bool last;
	u32 magic;

#define I40E_NVM_SECTOR_SIZE  4096
	if (eeprom->len == 0)
		return -EINVAL;

	/* check for NVMUpdate access method */
	magic = hw->vendor_id | (hw->device_id << 16);
	if (eeprom->magic && eeprom->magic != magic) {
		struct i40e_nvm_access *cmd = (struct i40e_nvm_access *)eeprom;
		int errno = 0;

		/* make sure it is the right magic for NVMUpdate */
		if ((eeprom->magic >> 16) != hw->device_id)
			errno = -EINVAL;
		else if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
			 test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
			errno = -EBUSY;
		else
			ret_val = i40e_nvmupd_command(hw, cmd, bytes, &errno);

		if ((errno || ret_val) && (hw->debug_mask & I40E_DEBUG_NVM))
			dev_info(&pf->pdev->dev,
				 "NVMUpdate read failed err=%d status=0x%x errno=%d module=%d offset=0x%x size=%d\n",
				 ret_val, hw->aq.asq_last_status, errno,
				 (u8)(cmd->config & I40E_NVM_MOD_PNT_MASK),
				 cmd->offset, cmd->data_size);

		return errno;
	}

	/* normal ethtool get_eeprom support */
	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	eeprom_buff = kzalloc(eeprom->len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ret_val = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret_val) {
		dev_info(&pf->pdev->dev,
			 "Failed Acquiring NVM resource for read err=%d status=0x%x\n",
			 ret_val, hw->aq.asq_last_status);
		goto free_buff;
	}

	sectors = eeprom->len / I40E_NVM_SECTOR_SIZE;
	sectors += (eeprom->len % I40E_NVM_SECTOR_SIZE) ? 1 : 0;
	len = I40E_NVM_SECTOR_SIZE;
	last = false;
	for (i = 0; i < sectors; i++) {
		if (i == (sectors - 1)) {
			len = eeprom->len - (I40E_NVM_SECTOR_SIZE * i);
			last = true;
		}
		offset = eeprom->offset + (I40E_NVM_SECTOR_SIZE * i),
		ret_val = i40e_aq_read_nvm(hw, 0x0, offset, len,
				(u8 *)eeprom_buff + (I40E_NVM_SECTOR_SIZE * i),
				last, NULL);
		if (ret_val && hw->aq.asq_last_status == I40E_AQ_RC_EPERM) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed, invalid offset 0x%x\n",
				 offset);
			break;
		} else if (ret_val &&
			   hw->aq.asq_last_status == I40E_AQ_RC_EACCES) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed, access, offset 0x%x\n",
				 offset);
			break;
		} else if (ret_val) {
			dev_info(&pf->pdev->dev,
				 "read NVM failed offset %d err=%d status=0x%x\n",
				 offset, ret_val, hw->aq.asq_last_status);
			break;
		}
	}

	i40e_release_nvm(hw);
	memcpy(bytes, (u8 *)eeprom_buff, eeprom->len);
free_buff:
	kfree(eeprom_buff);
	return ret_val;
}

static int i40e_get_eeprom_len(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	u32 val;

#define X722_EEPROM_SCOPE_LIMIT 0x5B9FFF
	if (hw->mac.type == I40E_MAC_X722) {
		val = X722_EEPROM_SCOPE_LIMIT + 1;
		return val;
	}
	val = (rd32(hw, I40E_GLPCI_LBARCTRL)
		& I40E_GLPCI_LBARCTRL_FL_SIZE_MASK)
		>> I40E_GLPCI_LBARCTRL_FL_SIZE_SHIFT;
	/* register returns value in power of 2, 64Kbyte chunks. */
	val = (64 * 1024) * BIT(val);
	return val;
}

static int i40e_set_eeprom(struct net_device *netdev,
			   struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_nvm_access *cmd = (struct i40e_nvm_access *)eeprom;
	int ret_val = 0;
	int errno = 0;
	u32 magic;

	/* normal ethtool set_eeprom is not supported */
	magic = hw->vendor_id | (hw->device_id << 16);
	if (eeprom->magic == magic)
		errno = -EOPNOTSUPP;
	/* check for NVMUpdate access method */
	else if (!eeprom->magic || (eeprom->magic >> 16) != hw->device_id)
		errno = -EINVAL;
	else if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
		 test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		errno = -EBUSY;
	else
		ret_val = i40e_nvmupd_command(hw, cmd, bytes, &errno);

	if ((errno || ret_val) && (hw->debug_mask & I40E_DEBUG_NVM))
		dev_info(&pf->pdev->dev,
			 "NVMUpdate write failed err=%d status=0x%x errno=%d module=%d offset=0x%x size=%d\n",
			 ret_val, hw->aq.asq_last_status, errno,
			 (u8)(cmd->config & I40E_NVM_MOD_PNT_MASK),
			 cmd->offset, cmd->data_size);

	return errno;
}

static void i40e_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *drvinfo)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	strlcpy(drvinfo->driver, i40e_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->fw_version, i40e_nvm_version_str(&pf->hw),
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(pf->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_priv_flags = I40E_PRIV_FLAGS_STR_LEN;
	if (pf->hw.pf_id == 0)
		drvinfo->n_priv_flags += I40E_GL_PRIV_FLAGS_STR_LEN;
}

static void i40e_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = pf->vsi[pf->lan_vsi];

	ring->rx_max_pending = I40E_MAX_NUM_DESCRIPTORS;
	ring->tx_max_pending = I40E_MAX_NUM_DESCRIPTORS;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = vsi->rx_rings[0]->count;
	ring->tx_pending = vsi->tx_rings[0]->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static bool i40e_active_tx_ring_index(struct i40e_vsi *vsi, u16 index)
{
	if (i40e_enabled_xdp_vsi(vsi)) {
		return index < vsi->num_queue_pairs ||
			(index >= vsi->alloc_queue_pairs &&
			 index < vsi->alloc_queue_pairs + vsi->num_queue_pairs);
	}

	return index < vsi->num_queue_pairs;
}

static int i40e_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct i40e_ring *tx_rings = NULL, *rx_rings = NULL;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_hw *hw = &np->vsi->back->hw;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 new_rx_count, new_tx_count;
	u16 tx_alloc_queue_pairs;
	int timeout = 50;
	int i, err = 0;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	if (ring->tx_pending > I40E_MAX_NUM_DESCRIPTORS ||
	    ring->tx_pending < I40E_MIN_NUM_DESCRIPTORS ||
	    ring->rx_pending > I40E_MAX_NUM_DESCRIPTORS ||
	    ring->rx_pending < I40E_MIN_NUM_DESCRIPTORS) {
		netdev_info(netdev,
			    "Descriptors requested (Tx: %d / Rx: %d) out of range [%d-%d]\n",
			    ring->tx_pending, ring->rx_pending,
			    I40E_MIN_NUM_DESCRIPTORS, I40E_MAX_NUM_DESCRIPTORS);
		return -EINVAL;
	}

	new_tx_count = ALIGN(ring->tx_pending, I40E_REQ_DESCRIPTOR_MULTIPLE);
	new_rx_count = ALIGN(ring->rx_pending, I40E_REQ_DESCRIPTOR_MULTIPLE);

	/* if nothing to do return success */
	if ((new_tx_count == vsi->tx_rings[0]->count) &&
	    (new_rx_count == vsi->rx_rings[0]->count))
		return 0;

	/* If there is a AF_XDP page pool attached to any of Rx rings,
	 * disallow changing the number of descriptors -- regardless
	 * if the netdev is running or not.
	 */
	if (i40e_xsk_any_rx_ring_enabled(vsi))
		return -EBUSY;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, pf->state)) {
		timeout--;
		if (!timeout)
			return -EBUSY;
		usleep_range(1000, 2000);
	}

	if (!netif_running(vsi->netdev)) {
		/* simple case - set for the next time the netdev is started */
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			vsi->tx_rings[i]->count = new_tx_count;
			vsi->rx_rings[i]->count = new_rx_count;
			if (i40e_enabled_xdp_vsi(vsi))
				vsi->xdp_rings[i]->count = new_tx_count;
		}
		vsi->num_tx_desc = new_tx_count;
		vsi->num_rx_desc = new_rx_count;
		goto done;
	}

	/* We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the Tx and Rx ring structs.
	 */

	/* alloc updated Tx and XDP Tx resources */
	tx_alloc_queue_pairs = vsi->alloc_queue_pairs *
			       (i40e_enabled_xdp_vsi(vsi) ? 2 : 1);
	if (new_tx_count != vsi->tx_rings[0]->count) {
		netdev_info(netdev,
			    "Changing Tx descriptor count from %d to %d.\n",
			    vsi->tx_rings[0]->count, new_tx_count);
		tx_rings = kcalloc(tx_alloc_queue_pairs,
				   sizeof(struct i40e_ring), GFP_KERNEL);
		if (!tx_rings) {
			err = -ENOMEM;
			goto done;
		}

		for (i = 0; i < tx_alloc_queue_pairs; i++) {
			if (!i40e_active_tx_ring_index(vsi, i))
				continue;

			tx_rings[i] = *vsi->tx_rings[i];
			tx_rings[i].count = new_tx_count;
			/* the desc and bi pointers will be reallocated in the
			 * setup call
			 */
			tx_rings[i].desc = NULL;
			tx_rings[i].rx_bi = NULL;
			err = i40e_setup_tx_descriptors(&tx_rings[i]);
			if (err) {
				while (i) {
					i--;
					if (!i40e_active_tx_ring_index(vsi, i))
						continue;
					i40e_free_tx_resources(&tx_rings[i]);
				}
				kfree(tx_rings);
				tx_rings = NULL;

				goto done;
			}
		}
	}

	/* alloc updated Rx resources */
	if (new_rx_count != vsi->rx_rings[0]->count) {
		netdev_info(netdev,
			    "Changing Rx descriptor count from %d to %d\n",
			    vsi->rx_rings[0]->count, new_rx_count);
		rx_rings = kcalloc(vsi->alloc_queue_pairs,
				   sizeof(struct i40e_ring), GFP_KERNEL);
		if (!rx_rings) {
			err = -ENOMEM;
			goto free_tx;
		}

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			u16 unused;

			/* clone ring and setup updated count */
			rx_rings[i] = *vsi->rx_rings[i];
			rx_rings[i].count = new_rx_count;
			/* the desc and bi pointers will be reallocated in the
			 * setup call
			 */
			rx_rings[i].desc = NULL;
			rx_rings[i].rx_bi = NULL;
			/* Clear cloned XDP RX-queue info before setup call */
			memset(&rx_rings[i].xdp_rxq, 0, sizeof(rx_rings[i].xdp_rxq));
			/* this is to allow wr32 to have something to write to
			 * during early allocation of Rx buffers
			 */
			rx_rings[i].tail = hw->hw_addr + I40E_PRTGEN_STATUS;
			err = i40e_setup_rx_descriptors(&rx_rings[i]);
			if (err)
				goto rx_unwind;
			err = i40e_alloc_rx_bi(&rx_rings[i]);
			if (err)
				goto rx_unwind;

			/* now allocate the Rx buffers to make sure the OS
			 * has enough memory, any failure here means abort
			 */
			unused = I40E_DESC_UNUSED(&rx_rings[i]);
			err = i40e_alloc_rx_buffers(&rx_rings[i], unused);
rx_unwind:
			if (err) {
				do {
					i40e_free_rx_resources(&rx_rings[i]);
				} while (i--);
				kfree(rx_rings);
				rx_rings = NULL;

				goto free_tx;
			}
		}
	}

	/* Bring interface down, copy in the new ring info,
	 * then restore the interface
	 */
	i40e_down(vsi);

	if (tx_rings) {
		for (i = 0; i < tx_alloc_queue_pairs; i++) {
			if (i40e_active_tx_ring_index(vsi, i)) {
				i40e_free_tx_resources(vsi->tx_rings[i]);
				*vsi->tx_rings[i] = tx_rings[i];
			}
		}
		kfree(tx_rings);
		tx_rings = NULL;
	}

	if (rx_rings) {
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			i40e_free_rx_resources(vsi->rx_rings[i]);
			/* get the real tail offset */
			rx_rings[i].tail = vsi->rx_rings[i]->tail;
			/* this is to fake out the allocation routine
			 * into thinking it has to realloc everything
			 * but the recycling logic will let us re-use
			 * the buffers allocated above
			 */
			rx_rings[i].next_to_use = 0;
			rx_rings[i].next_to_clean = 0;
			rx_rings[i].next_to_alloc = 0;
			/* do a struct copy */
			*vsi->rx_rings[i] = rx_rings[i];
		}
		kfree(rx_rings);
		rx_rings = NULL;
	}

	vsi->num_tx_desc = new_tx_count;
	vsi->num_rx_desc = new_rx_count;
	i40e_up(vsi);

free_tx:
	/* error cleanup if the Rx allocations failed after getting Tx */
	if (tx_rings) {
		for (i = 0; i < tx_alloc_queue_pairs; i++) {
			if (i40e_active_tx_ring_index(vsi, i))
				i40e_free_tx_resources(vsi->tx_rings[i]);
		}
		kfree(tx_rings);
		tx_rings = NULL;
	}

done:
	clear_bit(__I40E_CONFIG_BUSY, pf->state);

	return err;
}

/**
 * i40e_get_stats_count - return the stats count for a device
 * @netdev: the netdev to return the count for
 *
 * Returns the total number of statistics for this netdev. Note that even
 * though this is a function, it is required that the count for a specific
 * netdev must never change. Basing the count on static values such as the
 * maximum number of queues or the device type is ok. However, the API for
 * obtaining stats is *not* safe against changes based on non-static
 * values such as the *current* number of queues, or runtime flags.
 *
 * If a statistic is not always enabled, return it as part of the count
 * anyways, always return its string, and report its value as zero.
 **/
static int i40e_get_stats_count(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int stats_len;

	if (vsi == pf->vsi[pf->lan_vsi] && pf->hw.partition_id == 1)
		stats_len = I40E_PF_STATS_LEN;
	else
		stats_len = I40E_VSI_STATS_LEN;

	/* The number of stats reported for a given net_device must remain
	 * constant throughout the life of that device.
	 *
	 * This is because the API for obtaining the size, strings, and stats
	 * is spread out over three separate ethtool ioctls. There is no safe
	 * way to lock the number of stats across these calls, so we must
	 * assume that they will never change.
	 *
	 * Due to this, we report the maximum number of queues, even if not
	 * every queue is currently configured. Since we always allocate
	 * queues in pairs, we'll just use netdev->num_tx_queues * 2. This
	 * works because the num_tx_queues is set at device creation and never
	 * changes.
	 */
	stats_len += I40E_QUEUE_STATS_LEN * 2 * netdev->num_tx_queues;

	return stats_len;
}

static int i40e_get_sset_count(struct net_device *netdev, int sset)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	switch (sset) {
	case ETH_SS_TEST:
		return I40E_TEST_LEN;
	case ETH_SS_STATS:
		return i40e_get_stats_count(netdev);
	case ETH_SS_PRIV_FLAGS:
		return I40E_PRIV_FLAGS_STR_LEN +
			(pf->hw.pf_id == 0 ? I40E_GL_PRIV_FLAGS_STR_LEN : 0);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * i40e_get_pfc_stats - copy HW PFC statistics to formatted structure
 * @pf: the PF device structure
 * @i: the priority value to copy
 *
 * The PFC stats are found as arrays in pf->stats, which is not easy to pass
 * into i40e_add_ethtool_stats. Produce a formatted i40e_pfc_stats structure
 * of the PFC stats for the given priority.
 **/
static inline struct i40e_pfc_stats
i40e_get_pfc_stats(struct i40e_pf *pf, unsigned int i)
{
#define I40E_GET_PFC_STAT(stat, priority) \
	.stat = pf->stats.stat[priority]

	struct i40e_pfc_stats pfc = {
		I40E_GET_PFC_STAT(priority_xon_rx, i),
		I40E_GET_PFC_STAT(priority_xoff_rx, i),
		I40E_GET_PFC_STAT(priority_xon_tx, i),
		I40E_GET_PFC_STAT(priority_xoff_tx, i),
		I40E_GET_PFC_STAT(priority_xon_2_xoff, i),
	};
	return pfc;
}

/**
 * i40e_get_ethtool_stats - copy stat values into supplied buffer
 * @netdev: the netdev to collect stats for
 * @stats: ethtool stats command structure
 * @data: ethtool supplied buffer
 *
 * Copy the stats values for this netdev into the buffer. Expects data to be
 * pre-allocated to the size returned by i40e_get_stats_count.. Note that all
 * statistics must be copied in a static order, and the count must not change
 * for a given netdev. See i40e_get_stats_count for more details.
 *
 * If a statistic is not currently valid (such as a disabled queue), this
 * function reports its value as zero.
 **/
static void i40e_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_veb *veb = NULL;
	unsigned int i;
	bool veb_stats;
	u64 *p = data;

	i40e_update_stats(vsi);

	i40e_add_ethtool_stats(&data, i40e_get_vsi_stats_struct(vsi),
			       i40e_gstrings_net_stats);

	i40e_add_ethtool_stats(&data, vsi, i40e_gstrings_misc_stats);

	rcu_read_lock();
	for (i = 0; i < netdev->num_tx_queues; i++) {
		i40e_add_queue_stats(&data, READ_ONCE(vsi->tx_rings[i]));
		i40e_add_queue_stats(&data, READ_ONCE(vsi->rx_rings[i]));
	}
	rcu_read_unlock();

	if (vsi != pf->vsi[pf->lan_vsi] || pf->hw.partition_id != 1)
		goto check_data_pointer;

	veb_stats = ((pf->lan_veb != I40E_NO_VEB) &&
		     (pf->lan_veb < I40E_MAX_VEB) &&
		     (pf->flags & I40E_FLAG_VEB_STATS_ENABLED));

	if (veb_stats) {
		veb = pf->veb[pf->lan_veb];
		i40e_update_veb_stats(veb);
	}

	/* If veb stats aren't enabled, pass NULL instead of the veb so that
	 * we initialize stats to zero and update the data pointer
	 * intelligently
	 */
	i40e_add_ethtool_stats(&data, veb_stats ? veb : NULL,
			       i40e_gstrings_veb_stats);

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		i40e_add_ethtool_stats(&data, veb_stats ? veb : NULL,
				       i40e_gstrings_veb_tc_stats);

	i40e_add_ethtool_stats(&data, pf, i40e_gstrings_stats);

	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		struct i40e_pfc_stats pfc = i40e_get_pfc_stats(pf, i);

		i40e_add_ethtool_stats(&data, &pfc, i40e_gstrings_pfc_stats);
	}

check_data_pointer:
	WARN_ONCE(data - p != i40e_get_stats_count(netdev),
		  "ethtool stats count mismatch!");
}

/**
 * i40e_get_stat_strings - copy stat strings into supplied buffer
 * @netdev: the netdev to collect strings for
 * @data: supplied buffer to copy strings into
 *
 * Copy the strings related to stats for this netdev. Expects data to be
 * pre-allocated with the size reported by i40e_get_stats_count. Note that the
 * strings must be copied in a static order and the total count must not
 * change for a given netdev. See i40e_get_stats_count for more details.
 **/
static void i40e_get_stat_strings(struct net_device *netdev, u8 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	unsigned int i;
	u8 *p = data;

	i40e_add_stat_strings(&data, i40e_gstrings_net_stats);

	i40e_add_stat_strings(&data, i40e_gstrings_misc_stats);

	for (i = 0; i < netdev->num_tx_queues; i++) {
		i40e_add_stat_strings(&data, i40e_gstrings_queue_stats,
				      "tx", i);
		i40e_add_stat_strings(&data, i40e_gstrings_queue_stats,
				      "rx", i);
	}

	if (vsi != pf->vsi[pf->lan_vsi] || pf->hw.partition_id != 1)
		goto check_data_pointer;

	i40e_add_stat_strings(&data, i40e_gstrings_veb_stats);

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		i40e_add_stat_strings(&data, i40e_gstrings_veb_tc_stats, i);

	i40e_add_stat_strings(&data, i40e_gstrings_stats);

	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++)
		i40e_add_stat_strings(&data, i40e_gstrings_pfc_stats, i);

check_data_pointer:
	WARN_ONCE(data - p != i40e_get_stats_count(netdev) * ETH_GSTRING_LEN,
		  "stat strings count mismatch!");
}

static void i40e_get_priv_flag_strings(struct net_device *netdev, u8 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	char *p = (char *)data;
	unsigned int i;

	for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
		snprintf(p, ETH_GSTRING_LEN, "%s",
			 i40e_gstrings_priv_flags[i].flag_string);
		p += ETH_GSTRING_LEN;
	}
	if (pf->hw.pf_id != 0)
		return;
	for (i = 0; i < I40E_GL_PRIV_FLAGS_STR_LEN; i++) {
		snprintf(p, ETH_GSTRING_LEN, "%s",
			 i40e_gl_gstrings_priv_flags[i].flag_string);
		p += ETH_GSTRING_LEN;
	}
}

static void i40e_get_strings(struct net_device *netdev, u32 stringset,
			     u8 *data)
{
	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, i40e_gstrings_test,
		       I40E_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		i40e_get_stat_strings(netdev, data);
		break;
	case ETH_SS_PRIV_FLAGS:
		i40e_get_priv_flag_strings(netdev, data);
		break;
	default:
		break;
	}
}

static int i40e_get_ts_info(struct net_device *dev,
			    struct ethtool_ts_info *info)
{
	struct i40e_pf *pf = i40e_netdev_to_pf(dev);

	/* only report HW timestamping if PTP is enabled */
	if (!(pf->flags & I40E_FLAG_PTP))
		return ethtool_op_get_ts_info(dev, info);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	if (pf->ptp_clock)
		info->phc_index = ptp_clock_index(pf->ptp_clock);
	else
		info->phc_index = -1;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ);

	if (pf->hw_features & I40E_HW_PTP_L4_CAPABLE)
		info->rx_filters |= BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
				    BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	return 0;
}

static u64 i40e_link_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	i40e_status status;
	bool link_up = false;

	netif_info(pf, hw, netdev, "link test\n");
	status = i40e_get_link_status(&pf->hw, &link_up);
	if (status) {
		netif_err(pf, drv, netdev, "link query timed out, please retry test\n");
		*data = 1;
		return *data;
	}

	if (link_up)
		*data = 0;
	else
		*data = 1;

	return *data;
}

static u64 i40e_reg_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	netif_info(pf, hw, netdev, "register test\n");
	*data = i40e_diag_reg_test(&pf->hw);

	return *data;
}

static u64 i40e_eeprom_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	netif_info(pf, hw, netdev, "eeprom test\n");
	*data = i40e_diag_eeprom_test(&pf->hw);

	/* forcebly clear the NVM Update state machine */
	pf->hw.nvmupd_state = I40E_NVMUPD_STATE_INIT;

	return *data;
}

static u64 i40e_intr_test(struct net_device *netdev, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	u16 swc_old = pf->sw_int_count;

	netif_info(pf, hw, netdev, "interrupt test\n");
	wr32(&pf->hw, I40E_PFINT_DYN_CTL0,
	     (I40E_PFINT_DYN_CTL0_INTENA_MASK |
	      I40E_PFINT_DYN_CTL0_SWINT_TRIG_MASK |
	      I40E_PFINT_DYN_CTL0_ITR_INDX_MASK |
	      I40E_PFINT_DYN_CTL0_SW_ITR_INDX_ENA_MASK |
	      I40E_PFINT_DYN_CTL0_SW_ITR_INDX_MASK));
	usleep_range(1000, 2000);
	*data = (swc_old == pf->sw_int_count);

	return *data;
}

static inline bool i40e_active_vfs(struct i40e_pf *pf)
{
	struct i40e_vf *vfs = pf->vf;
	int i;

	for (i = 0; i < pf->num_alloc_vfs; i++)
		if (test_bit(I40E_VF_STATE_ACTIVE, &vfs[i].vf_states))
			return true;
	return false;
}

static inline bool i40e_active_vmdqs(struct i40e_pf *pf)
{
	return !!i40e_find_vsi_by_type(pf, I40E_VSI_VMDQ2);
}

static void i40e_diag_test(struct net_device *netdev,
			   struct ethtool_test *eth_test, u64 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	bool if_running = netif_running(netdev);
	struct i40e_pf *pf = np->vsi->back;

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */
		netif_info(pf, drv, netdev, "offline testing starting\n");

		set_bit(__I40E_TESTING, pf->state);

		if (i40e_active_vfs(pf) || i40e_active_vmdqs(pf)) {
			dev_warn(&pf->pdev->dev,
				 "Please take active VFs and Netqueues offline and restart the adapter before running NIC diagnostics\n");
			data[I40E_ETH_TEST_REG]		= 1;
			data[I40E_ETH_TEST_EEPROM]	= 1;
			data[I40E_ETH_TEST_INTR]	= 1;
			data[I40E_ETH_TEST_LINK]	= 1;
			eth_test->flags |= ETH_TEST_FL_FAILED;
			clear_bit(__I40E_TESTING, pf->state);
			goto skip_ol_tests;
		}

		/* If the device is online then take it offline */
		if (if_running)
			/* indicate we're in test mode */
			i40e_close(netdev);
		else
			/* This reset does not affect link - if it is
			 * changed to a type of reset that does affect
			 * link then the following link test would have
			 * to be moved to before the reset
			 */
			i40e_do_reset(pf, BIT(__I40E_PF_RESET_REQUESTED), true);

		if (i40e_link_test(netdev, &data[I40E_ETH_TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (i40e_eeprom_test(netdev, &data[I40E_ETH_TEST_EEPROM]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (i40e_intr_test(netdev, &data[I40E_ETH_TEST_INTR]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* run reg test last, a reset is required after it */
		if (i40e_reg_test(netdev, &data[I40E_ETH_TEST_REG]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		clear_bit(__I40E_TESTING, pf->state);
		i40e_do_reset(pf, BIT(__I40E_PF_RESET_REQUESTED), true);

		if (if_running)
			i40e_open(netdev);
	} else {
		/* Online tests */
		netif_info(pf, drv, netdev, "online testing starting\n");

		if (i40e_link_test(netdev, &data[I40E_ETH_TEST_LINK]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Offline only tests, not run in online; pass by default */
		data[I40E_ETH_TEST_REG] = 0;
		data[I40E_ETH_TEST_EEPROM] = 0;
		data[I40E_ETH_TEST_INTR] = 0;
	}

skip_ol_tests:

	netif_info(pf, drv, netdev, "testing finished\n");
}

static void i40e_get_wol(struct net_device *netdev,
			 struct ethtool_wolinfo *wol)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u16 wol_nvm_bits;

	/* NVM bit on means WoL disabled for the port */
	i40e_read_nvm_word(hw, I40E_SR_NVM_WAKE_ON_LAN, &wol_nvm_bits);
	if ((BIT(hw->port) & wol_nvm_bits) || (hw->partition_id != 1)) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		wol->supported = WAKE_MAGIC;
		wol->wolopts = (pf->wol_en ? WAKE_MAGIC : 0);
	}
}

/**
 * i40e_set_wol - set the WakeOnLAN configuration
 * @netdev: the netdev in question
 * @wol: the ethtool WoL setting data
 **/
static int i40e_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_hw *hw = &pf->hw;
	u16 wol_nvm_bits;

	/* WoL not supported if this isn't the controlling PF on the port */
	if (hw->partition_id != 1) {
		i40e_partition_setting_complaint(pf);
		return -EOPNOTSUPP;
	}

	if (vsi != pf->vsi[pf->lan_vsi])
		return -EOPNOTSUPP;

	/* NVM bit on means WoL disabled for the port */
	i40e_read_nvm_word(hw, I40E_SR_NVM_WAKE_ON_LAN, &wol_nvm_bits);
	if (BIT(hw->port) & wol_nvm_bits)
		return -EOPNOTSUPP;

	/* only magic packet is supported */
	if (wol->wolopts & ~WAKE_MAGIC)
		return -EOPNOTSUPP;

	/* is this a new value? */
	if (pf->wol_en != !!wol->wolopts) {
		pf->wol_en = !!wol->wolopts;
		device_set_wakeup_enable(&pf->pdev->dev, pf->wol_en);
	}

	return 0;
}

static int i40e_set_phys_id(struct net_device *netdev,
			    enum ethtool_phys_id_state state)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	i40e_status ret = 0;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int blink_freq = 2;
	u16 temp_status;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		if (!(pf->hw_features & I40E_HW_PHY_CONTROLS_LEDS)) {
			pf->led_status = i40e_led_get(hw);
		} else {
			if (!(hw->flags & I40E_HW_FLAG_AQ_PHY_ACCESS_CAPABLE))
				i40e_aq_set_phy_debug(hw, I40E_PHY_DEBUG_ALL,
						      NULL);
			ret = i40e_led_get_phy(hw, &temp_status,
					       &pf->phy_led_val);
			pf->led_status = temp_status;
		}
		return blink_freq;
	case ETHTOOL_ID_ON:
		if (!(pf->hw_features & I40E_HW_PHY_CONTROLS_LEDS))
			i40e_led_set(hw, 0xf, false);
		else
			ret = i40e_led_set_phy(hw, true, pf->led_status, 0);
		break;
	case ETHTOOL_ID_OFF:
		if (!(pf->hw_features & I40E_HW_PHY_CONTROLS_LEDS))
			i40e_led_set(hw, 0x0, false);
		else
			ret = i40e_led_set_phy(hw, false, pf->led_status, 0);
		break;
	case ETHTOOL_ID_INACTIVE:
		if (!(pf->hw_features & I40E_HW_PHY_CONTROLS_LEDS)) {
			i40e_led_set(hw, pf->led_status, false);
		} else {
			ret = i40e_led_set_phy(hw, false, pf->led_status,
					       (pf->phy_led_val |
					       I40E_PHY_LED_MODE_ORIG));
			if (!(hw->flags & I40E_HW_FLAG_AQ_PHY_ACCESS_CAPABLE))
				i40e_aq_set_phy_debug(hw, 0, NULL);
		}
		break;
	default:
		break;
	}
	if (ret)
		return -ENOENT;
	else
		return 0;
}

/* NOTE: i40e hardware uses a conversion factor of 2 for Interrupt
 * Throttle Rate (ITR) ie. ITR(1) = 2us ITR(10) = 20 us, and also
 * 125us (8000 interrupts per second) == ITR(62)
 */

/**
 * __i40e_get_coalesce - get per-queue coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 * @queue: which queue to pick
 *
 * Gets the per-queue settings for coalescence. Specifically Rx and Tx usecs
 * are per queue. If queue is <0 then we default to queue 0 as the
 * representative value.
 **/
static int __i40e_get_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec,
			       int queue)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_ring *rx_ring, *tx_ring;
	struct i40e_vsi *vsi = np->vsi;

	ec->tx_max_coalesced_frames_irq = vsi->work_limit;
	ec->rx_max_coalesced_frames_irq = vsi->work_limit;

	/* rx and tx usecs has per queue value. If user doesn't specify the
	 * queue, return queue 0's value to represent.
	 */
	if (queue < 0)
		queue = 0;
	else if (queue >= vsi->num_queue_pairs)
		return -EINVAL;

	rx_ring = vsi->rx_rings[queue];
	tx_ring = vsi->tx_rings[queue];

	if (ITR_IS_DYNAMIC(rx_ring->itr_setting))
		ec->use_adaptive_rx_coalesce = 1;

	if (ITR_IS_DYNAMIC(tx_ring->itr_setting))
		ec->use_adaptive_tx_coalesce = 1;

	ec->rx_coalesce_usecs = rx_ring->itr_setting & ~I40E_ITR_DYNAMIC;
	ec->tx_coalesce_usecs = tx_ring->itr_setting & ~I40E_ITR_DYNAMIC;

	/* we use the _usecs_high to store/set the interrupt rate limit
	 * that the hardware supports, that almost but not quite
	 * fits the original intent of the ethtool variable,
	 * the rx_coalesce_usecs_high limits total interrupts
	 * per second from both tx/rx sources.
	 */
	ec->rx_coalesce_usecs_high = vsi->int_rate_limit;
	ec->tx_coalesce_usecs_high = vsi->int_rate_limit;

	return 0;
}

/**
 * i40e_get_coalesce - get a netdev's coalesce settings
 * @netdev: the netdev to check
 * @ec: ethtool coalesce data structure
 *
 * Gets the coalesce settings for a particular netdev. Note that if user has
 * modified per-queue settings, this only guarantees to represent queue 0. See
 * __i40e_get_coalesce for more details.
 **/
static int i40e_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __i40e_get_coalesce(netdev, ec, -1);
}

/**
 * i40e_get_per_queue_coalesce - gets coalesce settings for particular queue
 * @netdev: netdev structure
 * @ec: ethtool's coalesce settings
 * @queue: the particular queue to read
 *
 * Will read a specific queue's coalesce settings
 **/
static int i40e_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __i40e_get_coalesce(netdev, ec, queue);
}

/**
 * i40e_set_itr_per_queue - set ITR values for specific queue
 * @vsi: the VSI to set values for
 * @ec: coalesce settings from ethtool
 * @queue: the queue to modify
 *
 * Change the ITR settings for a specific queue.
 **/
static void i40e_set_itr_per_queue(struct i40e_vsi *vsi,
				   struct ethtool_coalesce *ec,
				   int queue)
{
	struct i40e_ring *rx_ring = vsi->rx_rings[queue];
	struct i40e_ring *tx_ring = vsi->tx_rings[queue];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_q_vector *q_vector;
	u16 intrl;

	intrl = i40e_intrl_usec_to_reg(vsi->int_rate_limit);

	rx_ring->itr_setting = ITR_REG_ALIGN(ec->rx_coalesce_usecs);
	tx_ring->itr_setting = ITR_REG_ALIGN(ec->tx_coalesce_usecs);

	if (ec->use_adaptive_rx_coalesce)
		rx_ring->itr_setting |= I40E_ITR_DYNAMIC;
	else
		rx_ring->itr_setting &= ~I40E_ITR_DYNAMIC;

	if (ec->use_adaptive_tx_coalesce)
		tx_ring->itr_setting |= I40E_ITR_DYNAMIC;
	else
		tx_ring->itr_setting &= ~I40E_ITR_DYNAMIC;

	q_vector = rx_ring->q_vector;
	q_vector->rx.target_itr = ITR_TO_REG(rx_ring->itr_setting);

	q_vector = tx_ring->q_vector;
	q_vector->tx.target_itr = ITR_TO_REG(tx_ring->itr_setting);

	/* The interrupt handler itself will take care of programming
	 * the Tx and Rx ITR values based on the values we have entered
	 * into the q_vector, no need to write the values now.
	 */

	wr32(hw, I40E_PFINT_RATEN(q_vector->reg_idx), intrl);
	i40e_flush(hw);
}

/**
 * __i40e_set_coalesce - set coalesce settings for particular queue
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 * @queue: the queue to change
 *
 * Sets the coalesce settings for a particular queue.
 **/
static int __i40e_set_coalesce(struct net_device *netdev,
			       struct ethtool_coalesce *ec,
			       int queue)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	u16 intrl_reg, cur_rx_itr, cur_tx_itr;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int i;

	if (ec->tx_max_coalesced_frames_irq || ec->rx_max_coalesced_frames_irq)
		vsi->work_limit = ec->tx_max_coalesced_frames_irq;

	if (queue < 0) {
		cur_rx_itr = vsi->rx_rings[0]->itr_setting;
		cur_tx_itr = vsi->tx_rings[0]->itr_setting;
	} else if (queue < vsi->num_queue_pairs) {
		cur_rx_itr = vsi->rx_rings[queue]->itr_setting;
		cur_tx_itr = vsi->tx_rings[queue]->itr_setting;
	} else {
		netif_info(pf, drv, netdev, "Invalid queue value, queue range is 0 - %d\n",
			   vsi->num_queue_pairs - 1);
		return -EINVAL;
	}

	cur_tx_itr &= ~I40E_ITR_DYNAMIC;
	cur_rx_itr &= ~I40E_ITR_DYNAMIC;

	/* tx_coalesce_usecs_high is ignored, use rx-usecs-high instead */
	if (ec->tx_coalesce_usecs_high != vsi->int_rate_limit) {
		netif_info(pf, drv, netdev, "tx-usecs-high is not used, please program rx-usecs-high\n");
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs_high > INTRL_REG_TO_USEC(I40E_MAX_INTRL)) {
		netif_info(pf, drv, netdev, "Invalid value, rx-usecs-high range is 0-%lu\n",
			   INTRL_REG_TO_USEC(I40E_MAX_INTRL));
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs != cur_rx_itr &&
	    ec->use_adaptive_rx_coalesce) {
		netif_info(pf, drv, netdev, "RX interrupt moderation cannot be changed if adaptive-rx is enabled.\n");
		return -EINVAL;
	}

	if (ec->rx_coalesce_usecs > I40E_MAX_ITR) {
		netif_info(pf, drv, netdev, "Invalid value, rx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	if (ec->tx_coalesce_usecs != cur_tx_itr &&
	    ec->use_adaptive_tx_coalesce) {
		netif_info(pf, drv, netdev, "TX interrupt moderation cannot be changed if adaptive-tx is enabled.\n");
		return -EINVAL;
	}

	if (ec->tx_coalesce_usecs > I40E_MAX_ITR) {
		netif_info(pf, drv, netdev, "Invalid value, tx-usecs range is 0-8160\n");
		return -EINVAL;
	}

	if (ec->use_adaptive_rx_coalesce && !cur_rx_itr)
		ec->rx_coalesce_usecs = I40E_MIN_ITR;

	if (ec->use_adaptive_tx_coalesce && !cur_tx_itr)
		ec->tx_coalesce_usecs = I40E_MIN_ITR;

	intrl_reg = i40e_intrl_usec_to_reg(ec->rx_coalesce_usecs_high);
	vsi->int_rate_limit = INTRL_REG_TO_USEC(intrl_reg);
	if (vsi->int_rate_limit != ec->rx_coalesce_usecs_high) {
		netif_info(pf, drv, netdev, "Interrupt rate limit rounded down to %d\n",
			   vsi->int_rate_limit);
	}

	/* rx and tx usecs has per queue value. If user doesn't specify the
	 * queue, apply to all queues.
	 */
	if (queue < 0) {
		for (i = 0; i < vsi->num_queue_pairs; i++)
			i40e_set_itr_per_queue(vsi, ec, i);
	} else {
		i40e_set_itr_per_queue(vsi, ec, queue);
	}

	return 0;
}

/**
 * i40e_set_coalesce - set coalesce settings for every queue on the netdev
 * @netdev: the netdev to change
 * @ec: ethtool coalesce settings
 *
 * This will set each queue to the same coalesce settings.
 **/
static int i40e_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	return __i40e_set_coalesce(netdev, ec, -1);
}

/**
 * i40e_set_per_queue_coalesce - set specific queue's coalesce settings
 * @netdev: the netdev to change
 * @ec: ethtool's coalesce settings
 * @queue: the queue to change
 *
 * Sets the specified queue's coalesce settings.
 **/
static int i40e_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
				       struct ethtool_coalesce *ec)
{
	return __i40e_set_coalesce(netdev, ec, queue);
}

/**
 * i40e_get_rss_hash_opts - Get RSS hash Input Set for each flow type
 * @pf: pointer to the physical function struct
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the flow is supported, else Invalid Input.
 **/
static int i40e_get_rss_hash_opts(struct i40e_pf *pf, struct ethtool_rxnfc *cmd)
{
	struct i40e_hw *hw = &pf->hw;
	u8 flow_pctype = 0;
	u64 i_set = 0;

	cmd->data = 0;

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		break;
	case UDP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		break;
	case TCP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		break;
	case UDP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		break;
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		/* Default is src/dest for IP, no matter the L4 hashing */
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	/* Read flow based hash input set register */
	if (flow_pctype) {
		i_set = (u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(0,
					      flow_pctype)) |
			((u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(1,
					       flow_pctype)) << 32);
	}

	/* Process bits of hash input set */
	if (i_set) {
		if (i_set & I40E_L4_SRC_MASK)
			cmd->data |= RXH_L4_B_0_1;
		if (i_set & I40E_L4_DST_MASK)
			cmd->data |= RXH_L4_B_2_3;

		if (cmd->flow_type == TCP_V4_FLOW ||
		    cmd->flow_type == UDP_V4_FLOW) {
			if (i_set & I40E_L3_SRC_MASK)
				cmd->data |= RXH_IP_SRC;
			if (i_set & I40E_L3_DST_MASK)
				cmd->data |= RXH_IP_DST;
		} else if (cmd->flow_type == TCP_V6_FLOW ||
			  cmd->flow_type == UDP_V6_FLOW) {
			if (i_set & I40E_L3_V6_SRC_MASK)
				cmd->data |= RXH_IP_SRC;
			if (i_set & I40E_L3_V6_DST_MASK)
				cmd->data |= RXH_IP_DST;
		}
	}

	return 0;
}

/**
 * i40e_check_mask - Check whether a mask field is set
 * @mask: the full mask value
 * @field: mask of the field to check
 *
 * If the given mask is fully set, return positive value. If the mask for the
 * field is fully unset, return zero. Otherwise return a negative error code.
 **/
static int i40e_check_mask(u64 mask, u64 field)
{
	u64 value = mask & field;

	if (value == field)
		return 1;
	else if (!value)
		return 0;
	else
		return -1;
}

/**
 * i40e_parse_rx_flow_user_data - Deconstruct user-defined data
 * @fsp: pointer to rx flow specification
 * @data: pointer to userdef data structure for storage
 *
 * Read the user-defined data and deconstruct the value into a structure. No
 * other code should read the user-defined data, so as to ensure that every
 * place consistently reads the value correctly.
 *
 * The user-defined field is a 64bit Big Endian format value, which we
 * deconstruct by reading bits or bit fields from it. Single bit flags shall
 * be defined starting from the highest bits, while small bit field values
 * shall be defined starting from the lowest bits.
 *
 * Returns 0 if the data is valid, and non-zero if the userdef data is invalid
 * and the filter should be rejected. The data structure will always be
 * modified even if FLOW_EXT is not set.
 *
 **/
static int i40e_parse_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
					struct i40e_rx_flow_userdef *data)
{
	u64 value, mask;
	int valid;

	/* Zero memory first so it's always consistent. */
	memset(data, 0, sizeof(*data));

	if (!(fsp->flow_type & FLOW_EXT))
		return 0;

	value = be64_to_cpu(*((__be64 *)fsp->h_ext.data));
	mask = be64_to_cpu(*((__be64 *)fsp->m_ext.data));

#define I40E_USERDEF_FLEX_WORD		GENMASK_ULL(15, 0)
#define I40E_USERDEF_FLEX_OFFSET	GENMASK_ULL(31, 16)
#define I40E_USERDEF_FLEX_FILTER	GENMASK_ULL(31, 0)

	valid = i40e_check_mask(mask, I40E_USERDEF_FLEX_FILTER);
	if (valid < 0) {
		return -EINVAL;
	} else if (valid) {
		data->flex_word = value & I40E_USERDEF_FLEX_WORD;
		data->flex_offset =
			(value & I40E_USERDEF_FLEX_OFFSET) >> 16;
		data->flex_filter = true;
	}

	return 0;
}

/**
 * i40e_fill_rx_flow_user_data - Fill in user-defined data field
 * @fsp: pointer to rx_flow specification
 * @data: pointer to return userdef data
 *
 * Reads the userdef data structure and properly fills in the user defined
 * fields of the rx_flow_spec.
 **/
static void i40e_fill_rx_flow_user_data(struct ethtool_rx_flow_spec *fsp,
					struct i40e_rx_flow_userdef *data)
{
	u64 value = 0, mask = 0;

	if (data->flex_filter) {
		value |= data->flex_word;
		value |= (u64)data->flex_offset << 16;
		mask |= I40E_USERDEF_FLEX_FILTER;
	}

	if (value || mask)
		fsp->flow_type |= FLOW_EXT;

	*((__be64 *)fsp->h_ext.data) = cpu_to_be64(value);
	*((__be64 *)fsp->m_ext.data) = cpu_to_be64(mask);
}

/**
 * i40e_get_ethtool_fdir_all - Populates the rule count of a command
 * @pf: Pointer to the physical function struct
 * @cmd: The command to get or set Rx flow classification rules
 * @rule_locs: Array of used rule locations
 *
 * This function populates both the total and actual rule count of
 * the ethtool flow classification command
 *
 * Returns 0 on success or -EMSGSIZE if entry not found
 **/
static int i40e_get_ethtool_fdir_all(struct i40e_pf *pf,
				     struct ethtool_rxnfc *cmd,
				     u32 *rule_locs)
{
	struct i40e_fdir_filter *rule;
	struct hlist_node *node2;
	int cnt = 0;

	/* report total rule count */
	cmd->data = i40e_get_fd_cnt_all(pf);

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (cnt == cmd->rule_cnt)
			return -EMSGSIZE;

		rule_locs[cnt] = rule->fd_id;
		cnt++;
	}

	cmd->rule_cnt = cnt;

	return 0;
}

/**
 * i40e_get_ethtool_fdir_entry - Look up a filter based on Rx flow
 * @pf: Pointer to the physical function struct
 * @cmd: The command to get or set Rx flow classification rules
 *
 * This function looks up a filter based on the Rx flow classification
 * command and fills the flow spec info for it if found
 *
 * Returns 0 on success or -EINVAL if filter not found
 **/
static int i40e_get_ethtool_fdir_entry(struct i40e_pf *pf,
				       struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
			(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct i40e_rx_flow_userdef userdef = {0};
	struct i40e_fdir_filter *rule = NULL;
	struct hlist_node *node2;
	u64 input_set;
	u16 index;

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (fsp->location <= rule->fd_id)
			break;
	}

	if (!rule || fsp->location != rule->fd_id)
		return -EINVAL;

	fsp->flow_type = rule->flow_type;
	if (fsp->flow_type == IP_USER_FLOW) {
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.proto = 0;
	}

	if (fsp->flow_type == IPV6_USER_FLOW ||
	    fsp->flow_type == UDP_V6_FLOW ||
	    fsp->flow_type == TCP_V6_FLOW ||
	    fsp->flow_type == SCTP_V6_FLOW) {
		/* Reverse the src and dest notion, since the HW views them
		 * from Tx perspective where as the user expects it from
		 * Rx filter view.
		 */
		fsp->h_u.tcp_ip6_spec.psrc = rule->dst_port;
		fsp->h_u.tcp_ip6_spec.pdst = rule->src_port;
		memcpy(fsp->h_u.tcp_ip6_spec.ip6dst, rule->src_ip6,
		       sizeof(__be32) * 4);
		memcpy(fsp->h_u.tcp_ip6_spec.ip6src, rule->dst_ip6,
		       sizeof(__be32) * 4);
	} else {
		/* Reverse the src and dest notion, since the HW views them
		 * from Tx perspective where as the user expects it from
		 * Rx filter view.
		 */
		fsp->h_u.tcp_ip4_spec.psrc = rule->dst_port;
		fsp->h_u.tcp_ip4_spec.pdst = rule->src_port;
		fsp->h_u.tcp_ip4_spec.ip4src = rule->dst_ip;
		fsp->h_u.tcp_ip4_spec.ip4dst = rule->src_ip;
	}

	switch (rule->flow_type) {
	case SCTP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_SCTP;
		break;
	case TCP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		break;
	case UDP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		break;
	case SCTP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_SCTP;
		break;
	case TCP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		break;
	case UDP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		break;
	case IP_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_OTHER;
		break;
	case IPV6_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_OTHER;
		break;
	default:
		/* If we have stored a filter with a flow type not listed here
		 * it is almost certainly a driver bug. WARN(), and then
		 * assign the input_set as if all fields are enabled to avoid
		 * reading unassigned memory.
		 */
		WARN(1, "Missing input set index for flow_type %d\n",
		     rule->flow_type);
		input_set = 0xFFFFFFFFFFFFFFFFULL;
		goto no_input_set;
	}

	input_set = i40e_read_fd_input_set(pf, index);

no_input_set:
	if (input_set & I40E_L3_V6_SRC_MASK) {
		fsp->m_u.tcp_ip6_spec.ip6src[0] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6src[1] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6src[2] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6src[3] = htonl(0xFFFFFFFF);
	}

	if (input_set & I40E_L3_V6_DST_MASK) {
		fsp->m_u.tcp_ip6_spec.ip6dst[0] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6dst[1] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6dst[2] = htonl(0xFFFFFFFF);
		fsp->m_u.tcp_ip6_spec.ip6dst[3] = htonl(0xFFFFFFFF);
	}

	if (input_set & I40E_L3_SRC_MASK)
		fsp->m_u.tcp_ip4_spec.ip4src = htonl(0xFFFFFFFF);

	if (input_set & I40E_L3_DST_MASK)
		fsp->m_u.tcp_ip4_spec.ip4dst = htonl(0xFFFFFFFF);

	if (input_set & I40E_L4_SRC_MASK)
		fsp->m_u.tcp_ip4_spec.psrc = htons(0xFFFF);

	if (input_set & I40E_L4_DST_MASK)
		fsp->m_u.tcp_ip4_spec.pdst = htons(0xFFFF);

	if (rule->dest_ctl == I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->q_index;

	if (rule->vlan_tag) {
		fsp->h_ext.vlan_etype = rule->vlan_etype;
		fsp->m_ext.vlan_etype = htons(0xFFFF);
		fsp->h_ext.vlan_tci = rule->vlan_tag;
		fsp->m_ext.vlan_tci = htons(0xFFFF);
		fsp->flow_type |= FLOW_EXT;
	}

	if (rule->dest_vsi != pf->vsi[pf->lan_vsi]->id) {
		struct i40e_vsi *vsi;

		vsi = i40e_find_vsi_from_id(pf, rule->dest_vsi);
		if (vsi && vsi->type == I40E_VSI_SRIOV) {
			/* VFs are zero-indexed by the driver, but ethtool
			 * expects them to be one-indexed, so add one here
			 */
			u64 ring_vf = vsi->vf_id + 1;

			ring_vf <<= ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
			fsp->ring_cookie |= ring_vf;
		}
	}

	if (rule->flex_filter) {
		userdef.flex_filter = true;
		userdef.flex_word = be16_to_cpu(rule->flex_word);
		userdef.flex_offset = rule->flex_offset;
	}

	i40e_fill_rx_flow_user_data(fsp, &userdef);

	return 0;
}

/**
 * i40e_get_rxnfc - command to get RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 * @rule_locs: pointer to store rule data
 *
 * Returns Success if the command is supported.
 **/
static int i40e_get_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd,
			  u32 *rule_locs)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = vsi->rss_size;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		ret = i40e_get_rss_hash_opts(pf, cmd);
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = pf->fdir_pf_active_filters;
		/* report total rule count */
		cmd->data = i40e_get_fd_cnt_all(pf);
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = i40e_get_ethtool_fdir_entry(pf, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = i40e_get_ethtool_fdir_all(pf, cmd, rule_locs);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * i40e_get_rss_hash_bits - Read RSS Hash bits from register
 * @nfc: pointer to user request
 * @i_setc: bits currently set
 *
 * Returns value of bits to be set per user request
 **/
static u64 i40e_get_rss_hash_bits(struct ethtool_rxnfc *nfc, u64 i_setc)
{
	u64 i_set = i_setc;
	u64 src_l3 = 0, dst_l3 = 0;

	if (nfc->data & RXH_L4_B_0_1)
		i_set |= I40E_L4_SRC_MASK;
	else
		i_set &= ~I40E_L4_SRC_MASK;
	if (nfc->data & RXH_L4_B_2_3)
		i_set |= I40E_L4_DST_MASK;
	else
		i_set &= ~I40E_L4_DST_MASK;

	if (nfc->flow_type == TCP_V6_FLOW || nfc->flow_type == UDP_V6_FLOW) {
		src_l3 = I40E_L3_V6_SRC_MASK;
		dst_l3 = I40E_L3_V6_DST_MASK;
	} else if (nfc->flow_type == TCP_V4_FLOW ||
		  nfc->flow_type == UDP_V4_FLOW) {
		src_l3 = I40E_L3_SRC_MASK;
		dst_l3 = I40E_L3_DST_MASK;
	} else {
		/* Any other flow type are not supported here */
		return i_set;
	}

	if (nfc->data & RXH_IP_SRC)
		i_set |= src_l3;
	else
		i_set &= ~src_l3;
	if (nfc->data & RXH_IP_DST)
		i_set |= dst_l3;
	else
		i_set &= ~dst_l3;

	return i_set;
}

/**
 * i40e_set_rss_hash_opt - Enable/Disable flow types for RSS hash
 * @pf: pointer to the physical function struct
 * @nfc: ethtool rxnfc command
 *
 * Returns Success if the flow input set is supported.
 **/
static int i40e_set_rss_hash_opt(struct i40e_pf *pf, struct ethtool_rxnfc *nfc)
{
	struct i40e_hw *hw = &pf->hw;
	u64 hena = (u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(0)) |
		   ((u64)i40e_read_rx_ctl(hw, I40E_PFQF_HENA(1)) << 32);
	u8 flow_pctype = 0;
	u64 i_set, i_setc;

	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		dev_err(&pf->pdev->dev,
			"Change of RSS hash input set is not supported when MFP mode is enabled\n");
		return -EOPNOTSUPP;
	}

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		if (pf->hw_features & I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK);
		break;
	case TCP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		if (pf->hw_features & I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK);
		if (pf->hw_features & I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK);
		break;
	case UDP_V4_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		if (pf->hw_features & I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) |
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP);

		hena |= BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4);
		break;
	case UDP_V6_FLOW:
		flow_pctype = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		if (pf->hw_features & I40E_HW_MULTIPLE_TCP_UDP_RSS_PCTYPE)
			hena |=
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) |
			  BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP);

		hena |= BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6);
		break;
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V4_FLOW:
		if ((nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER);
		break;
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
		if ((nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER);
		break;
	case IPV4_FLOW:
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER) |
			BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4);
		break;
	case IPV6_FLOW:
		hena |= BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER) |
			BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6);
		break;
	default:
		return -EINVAL;
	}

	if (flow_pctype) {
		i_setc = (u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(0,
					       flow_pctype)) |
			((u64)i40e_read_rx_ctl(hw, I40E_GLQF_HASH_INSET(1,
					       flow_pctype)) << 32);
		i_set = i40e_get_rss_hash_bits(nfc, i_setc);
		i40e_write_rx_ctl(hw, I40E_GLQF_HASH_INSET(0, flow_pctype),
				  (u32)i_set);
		i40e_write_rx_ctl(hw, I40E_GLQF_HASH_INSET(1, flow_pctype),
				  (u32)(i_set >> 32));
		hena |= BIT_ULL(flow_pctype);
	}

	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(0), (u32)hena);
	i40e_write_rx_ctl(hw, I40E_PFQF_HENA(1), (u32)(hena >> 32));
	i40e_flush(hw);

	return 0;
}

/**
 * i40e_update_ethtool_fdir_entry - Updates the fdir filter entry
 * @vsi: Pointer to the targeted VSI
 * @input: The filter to update or NULL to indicate deletion
 * @sw_idx: Software index to the filter
 * @cmd: The command to get or set Rx flow classification rules
 *
 * This function updates (or deletes) a Flow Director entry from
 * the hlist of the corresponding PF
 *
 * Returns 0 on success
 **/
static int i40e_update_ethtool_fdir_entry(struct i40e_vsi *vsi,
					  struct i40e_fdir_filter *input,
					  u16 sw_idx,
					  struct ethtool_rxnfc *cmd)
{
	struct i40e_fdir_filter *rule, *parent;
	struct i40e_pf *pf = vsi->back;
	struct hlist_node *node2;
	int err = -EINVAL;

	parent = NULL;
	rule = NULL;

	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		/* hash found, or no matching entry */
		if (rule->fd_id >= sw_idx)
			break;
		parent = rule;
	}

	/* if there is an old rule occupying our place remove it */
	if (rule && (rule->fd_id == sw_idx)) {
		/* Remove this rule, since we're either deleting it, or
		 * replacing it.
		 */
		err = i40e_add_del_fdir(vsi, rule, false);
		hlist_del(&rule->fdir_node);
		kfree(rule);
		pf->fdir_pf_active_filters--;
	}

	/* If we weren't given an input, this is a delete, so just return the
	 * error code indicating if there was an entry at the requested slot
	 */
	if (!input)
		return err;

	/* Otherwise, install the new rule as requested */
	INIT_HLIST_NODE(&input->fdir_node);

	/* add filter to the list */
	if (parent)
		hlist_add_behind(&input->fdir_node, &parent->fdir_node);
	else
		hlist_add_head(&input->fdir_node,
			       &pf->fdir_filter_list);

	/* update counts */
	pf->fdir_pf_active_filters++;

	return 0;
}

/**
 * i40e_prune_flex_pit_list - Cleanup unused entries in FLX_PIT table
 * @pf: pointer to PF structure
 *
 * This function searches the list of filters and determines which FLX_PIT
 * entries are still required. It will prune any entries which are no longer
 * in use after the deletion.
 **/
static void i40e_prune_flex_pit_list(struct i40e_pf *pf)
{
	struct i40e_flex_pit *entry, *tmp;
	struct i40e_fdir_filter *rule;

	/* First, we'll check the l3 table */
	list_for_each_entry_safe(entry, tmp, &pf->l3_flex_pit_list, list) {
		bool found = false;

		hlist_for_each_entry(rule, &pf->fdir_filter_list, fdir_node) {
			if (rule->flow_type != IP_USER_FLOW)
				continue;
			if (rule->flex_filter &&
			    rule->flex_offset == entry->src_offset) {
				found = true;
				break;
			}
		}

		/* If we didn't find the filter, then we can prune this entry
		 * from the list.
		 */
		if (!found) {
			list_del(&entry->list);
			kfree(entry);
		}
	}

	/* Followed by the L4 table */
	list_for_each_entry_safe(entry, tmp, &pf->l4_flex_pit_list, list) {
		bool found = false;

		hlist_for_each_entry(rule, &pf->fdir_filter_list, fdir_node) {
			/* Skip this filter if it's L3, since we already
			 * checked those in the above loop
			 */
			if (rule->flow_type == IP_USER_FLOW)
				continue;
			if (rule->flex_filter &&
			    rule->flex_offset == entry->src_offset) {
				found = true;
				break;
			}
		}

		/* If we didn't find the filter, then we can prune this entry
		 * from the list.
		 */
		if (!found) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
}

/**
 * i40e_del_fdir_entry - Deletes a Flow Director filter entry
 * @vsi: Pointer to the targeted VSI
 * @cmd: The command to get or set Rx flow classification rules
 *
 * The function removes a Flow Director filter entry from the
 * hlist of the corresponding PF
 *
 * Returns 0 on success
 */
static int i40e_del_fdir_entry(struct i40e_vsi *vsi,
			       struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct i40e_pf *pf = vsi->back;
	int ret = 0;

	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		return -EBUSY;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		return -EBUSY;

	ret = i40e_update_ethtool_fdir_entry(vsi, NULL, fsp->location, cmd);

	i40e_prune_flex_pit_list(pf);

	i40e_fdir_check_and_reenable(pf);
	return ret;
}

/**
 * i40e_unused_pit_index - Find an unused PIT index for given list
 * @pf: the PF data structure
 *
 * Find the first unused flexible PIT index entry. We search both the L3 and
 * L4 flexible PIT lists so that the returned index is unique and unused by
 * either currently programmed L3 or L4 filters. We use a bit field as storage
 * to track which indexes are already used.
 **/
static u8 i40e_unused_pit_index(struct i40e_pf *pf)
{
	unsigned long available_index = 0xFF;
	struct i40e_flex_pit *entry;

	/* We need to make sure that the new index isn't in use by either L3
	 * or L4 filters so that IP_USER_FLOW filters can program both L3 and
	 * L4 to use the same index.
	 */

	list_for_each_entry(entry, &pf->l4_flex_pit_list, list)
		clear_bit(entry->pit_index, &available_index);

	list_for_each_entry(entry, &pf->l3_flex_pit_list, list)
		clear_bit(entry->pit_index, &available_index);

	return find_first_bit(&available_index, 8);
}

/**
 * i40e_find_flex_offset - Find an existing flex src_offset
 * @flex_pit_list: L3 or L4 flex PIT list
 * @src_offset: new src_offset to find
 *
 * Searches the flex_pit_list for an existing offset. If no offset is
 * currently programmed, then this will return an ERR_PTR if there is no space
 * to add a new offset, otherwise it returns NULL.
 **/
static
struct i40e_flex_pit *i40e_find_flex_offset(struct list_head *flex_pit_list,
					    u16 src_offset)
{
	struct i40e_flex_pit *entry;
	int size = 0;

	/* Search for the src_offset first. If we find a matching entry
	 * already programmed, we can simply re-use it.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		size++;
		if (entry->src_offset == src_offset)
			return entry;
	}

	/* If we haven't found an entry yet, then the provided src offset has
	 * not yet been programmed. We will program the src offset later on,
	 * but we need to indicate whether there is enough space to do so
	 * here. We'll make use of ERR_PTR for this purpose.
	 */
	if (size >= I40E_FLEX_PIT_TABLE_SIZE)
		return ERR_PTR(-ENOSPC);

	return NULL;
}

/**
 * i40e_add_flex_offset - Add src_offset to flex PIT table list
 * @flex_pit_list: L3 or L4 flex PIT list
 * @src_offset: new src_offset to add
 * @pit_index: the PIT index to program
 *
 * This function programs the new src_offset to the list. It is expected that
 * i40e_find_flex_offset has already been tried and returned NULL, indicating
 * that this offset is not programmed, and that the list has enough space to
 * store another offset.
 *
 * Returns 0 on success, and negative value on error.
 **/
static int i40e_add_flex_offset(struct list_head *flex_pit_list,
				u16 src_offset,
				u8 pit_index)
{
	struct i40e_flex_pit *new_pit, *entry;

	new_pit = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!new_pit)
		return -ENOMEM;

	new_pit->src_offset = src_offset;
	new_pit->pit_index = pit_index;

	/* We need to insert this item such that the list is sorted by
	 * src_offset in ascending order.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		if (new_pit->src_offset < entry->src_offset) {
			list_add_tail(&new_pit->list, &entry->list);
			return 0;
		}

		/* If we found an entry with our offset already programmed we
		 * can simply return here, after freeing the memory. However,
		 * if the pit_index does not match we need to report an error.
		 */
		if (new_pit->src_offset == entry->src_offset) {
			int err = 0;

			/* If the PIT index is not the same we can't re-use
			 * the entry, so we must report an error.
			 */
			if (new_pit->pit_index != entry->pit_index)
				err = -EINVAL;

			kfree(new_pit);
			return err;
		}
	}

	/* If we reached here, then we haven't yet added the item. This means
	 * that we should add the item at the end of the list.
	 */
	list_add_tail(&new_pit->list, flex_pit_list);
	return 0;
}

/**
 * __i40e_reprogram_flex_pit - Re-program specific FLX_PIT table
 * @pf: Pointer to the PF structure
 * @flex_pit_list: list of flexible src offsets in use
 * @flex_pit_start: index to first entry for this section of the table
 *
 * In order to handle flexible data, the hardware uses a table of values
 * called the FLX_PIT table. This table is used to indicate which sections of
 * the input correspond to what PIT index values. Unfortunately, hardware is
 * very restrictive about programming this table. Entries must be ordered by
 * src_offset in ascending order, without duplicates. Additionally, unused
 * entries must be set to the unused index value, and must have valid size and
 * length according to the src_offset ordering.
 *
 * This function will reprogram the FLX_PIT register from a book-keeping
 * structure that we guarantee is already ordered correctly, and has no more
 * than 3 entries.
 *
 * To make things easier, we only support flexible values of one word length,
 * rather than allowing variable length flexible values.
 **/
static void __i40e_reprogram_flex_pit(struct i40e_pf *pf,
				      struct list_head *flex_pit_list,
				      int flex_pit_start)
{
	struct i40e_flex_pit *entry = NULL;
	u16 last_offset = 0;
	int i = 0, j = 0;

	/* First, loop over the list of flex PIT entries, and reprogram the
	 * registers.
	 */
	list_for_each_entry(entry, flex_pit_list, list) {
		/* We have to be careful when programming values for the
		 * largest SRC_OFFSET value. It is possible that adding
		 * additional empty values at the end would overflow the space
		 * for the SRC_OFFSET in the FLX_PIT register. To avoid this,
		 * we check here and add the empty values prior to adding the
		 * largest value.
		 *
		 * To determine this, we will use a loop from i+1 to 3, which
		 * will determine whether the unused entries would have valid
		 * SRC_OFFSET. Note that there cannot be extra entries past
		 * this value, because the only valid values would have been
		 * larger than I40E_MAX_FLEX_SRC_OFFSET, and thus would not
		 * have been added to the list in the first place.
		 */
		for (j = i + 1; j < 3; j++) {
			u16 offset = entry->src_offset + j;
			int index = flex_pit_start + i;
			u32 value = I40E_FLEX_PREP_VAL(I40E_FLEX_DEST_UNUSED,
						       1,
						       offset - 3);

			if (offset > I40E_MAX_FLEX_SRC_OFFSET) {
				i40e_write_rx_ctl(&pf->hw,
						  I40E_PRTQF_FLX_PIT(index),
						  value);
				i++;
			}
		}

		/* Now, we can program the actual value into the table */
		i40e_write_rx_ctl(&pf->hw,
				  I40E_PRTQF_FLX_PIT(flex_pit_start + i),
				  I40E_FLEX_PREP_VAL(entry->pit_index + 50,
						     1,
						     entry->src_offset));
		i++;
	}

	/* In order to program the last entries in the table, we need to
	 * determine the valid offset. If the list is empty, we'll just start
	 * with 0. Otherwise, we'll start with the last item offset and add 1.
	 * This ensures that all entries have valid sizes. If we don't do this
	 * correctly, the hardware will disable flexible field parsing.
	 */
	if (!list_empty(flex_pit_list))
		last_offset = list_prev_entry(entry, list)->src_offset + 1;

	for (; i < 3; i++, last_offset++) {
		i40e_write_rx_ctl(&pf->hw,
				  I40E_PRTQF_FLX_PIT(flex_pit_start + i),
				  I40E_FLEX_PREP_VAL(I40E_FLEX_DEST_UNUSED,
						     1,
						     last_offset));
	}
}

/**
 * i40e_reprogram_flex_pit - Reprogram all FLX_PIT tables after input set change
 * @pf: pointer to the PF structure
 *
 * This function reprograms both the L3 and L4 FLX_PIT tables. See the
 * internal helper function for implementation details.
 **/
static void i40e_reprogram_flex_pit(struct i40e_pf *pf)
{
	__i40e_reprogram_flex_pit(pf, &pf->l3_flex_pit_list,
				  I40E_FLEX_PIT_IDX_START_L3);

	__i40e_reprogram_flex_pit(pf, &pf->l4_flex_pit_list,
				  I40E_FLEX_PIT_IDX_START_L4);

	/* We also need to program the L3 and L4 GLQF ORT register */
	i40e_write_rx_ctl(&pf->hw,
			  I40E_GLQF_ORT(I40E_L3_GLQF_ORT_IDX),
			  I40E_ORT_PREP_VAL(I40E_FLEX_PIT_IDX_START_L3,
					    3, 1));

	i40e_write_rx_ctl(&pf->hw,
			  I40E_GLQF_ORT(I40E_L4_GLQF_ORT_IDX),
			  I40E_ORT_PREP_VAL(I40E_FLEX_PIT_IDX_START_L4,
					    3, 1));
}

/**
 * i40e_flow_str - Converts a flow_type into a human readable string
 * @fsp: the flow specification
 *
 * Currently only flow types we support are included here, and the string
 * value attempts to match what ethtool would use to configure this flow type.
 **/
static const char *i40e_flow_str(struct ethtool_rx_flow_spec *fsp)
{
	switch (fsp->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		return "tcp4";
	case UDP_V4_FLOW:
		return "udp4";
	case SCTP_V4_FLOW:
		return "sctp4";
	case IP_USER_FLOW:
		return "ip4";
	case TCP_V6_FLOW:
		return "tcp6";
	case UDP_V6_FLOW:
		return "udp6";
	case SCTP_V6_FLOW:
		return "sctp6";
	case IPV6_USER_FLOW:
		return "ip6";
	default:
		return "unknown";
	}
}

/**
 * i40e_pit_index_to_mask - Return the FLEX mask for a given PIT index
 * @pit_index: PIT index to convert
 *
 * Returns the mask for a given PIT index. Will return 0 if the pit_index is
 * of range.
 **/
static u64 i40e_pit_index_to_mask(int pit_index)
{
	switch (pit_index) {
	case 0:
		return I40E_FLEX_50_MASK;
	case 1:
		return I40E_FLEX_51_MASK;
	case 2:
		return I40E_FLEX_52_MASK;
	case 3:
		return I40E_FLEX_53_MASK;
	case 4:
		return I40E_FLEX_54_MASK;
	case 5:
		return I40E_FLEX_55_MASK;
	case 6:
		return I40E_FLEX_56_MASK;
	case 7:
		return I40E_FLEX_57_MASK;
	default:
		return 0;
	}
}

/**
 * i40e_print_input_set - Show changes between two input sets
 * @vsi: the vsi being configured
 * @old: the old input set
 * @new: the new input set
 *
 * Print the difference between old and new input sets by showing which series
 * of words are toggled on or off. Only displays the bits we actually support
 * changing.
 **/
static void i40e_print_input_set(struct i40e_vsi *vsi, u64 old, u64 new)
{
	struct i40e_pf *pf = vsi->back;
	bool old_value, new_value;
	int i;

	old_value = !!(old & I40E_L3_SRC_MASK);
	new_value = !!(new & I40E_L3_SRC_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L3 source address: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L3_DST_MASK);
	new_value = !!(new & I40E_L3_DST_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L3 destination address: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L4_SRC_MASK);
	new_value = !!(new & I40E_L4_SRC_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L4 source port: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_L4_DST_MASK);
	new_value = !!(new & I40E_L4_DST_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "L4 destination port: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	old_value = !!(old & I40E_VERIFY_TAG_MASK);
	new_value = !!(new & I40E_VERIFY_TAG_MASK);
	if (old_value != new_value)
		netif_info(pf, drv, vsi->netdev, "SCTP verification tag: %s -> %s\n",
			   old_value ? "ON" : "OFF",
			   new_value ? "ON" : "OFF");

	/* Show change of flexible filter entries */
	for (i = 0; i < I40E_FLEX_INDEX_ENTRIES; i++) {
		u64 flex_mask = i40e_pit_index_to_mask(i);

		old_value = !!(old & flex_mask);
		new_value = !!(new & flex_mask);
		if (old_value != new_value)
			netif_info(pf, drv, vsi->netdev, "FLEX index %d: %s -> %s\n",
				   i,
				   old_value ? "ON" : "OFF",
				   new_value ? "ON" : "OFF");
	}

	netif_info(pf, drv, vsi->netdev, "  Current input set: %0llx\n",
		   old);
	netif_info(pf, drv, vsi->netdev, "Requested input set: %0llx\n",
		   new);
}

/**
 * i40e_check_fdir_input_set - Check that a given rx_flow_spec mask is valid
 * @vsi: pointer to the targeted VSI
 * @fsp: pointer to Rx flow specification
 * @userdef: userdefined data from flow specification
 *
 * Ensures that a given ethtool_rx_flow_spec has a valid mask. Some support
 * for partial matches exists with a few limitations. First, hardware only
 * supports masking by word boundary (2 bytes) and not per individual bit.
 * Second, hardware is limited to using one mask for a flow type and cannot
 * use a separate mask for each filter.
 *
 * To support these limitations, if we already have a configured filter for
 * the specified type, this function enforces that new filters of the type
 * match the configured input set. Otherwise, if we do not have a filter of
 * the specified type, we allow the input set to be updated to match the
 * desired filter.
 *
 * To help ensure that administrators understand why filters weren't displayed
 * as supported, we print a diagnostic message displaying how the input set
 * would change and warning to delete the preexisting filters if required.
 *
 * Returns 0 on successful input set match, and a negative return code on
 * failure.
 **/
static int i40e_check_fdir_input_set(struct i40e_vsi *vsi,
				     struct ethtool_rx_flow_spec *fsp,
				     struct i40e_rx_flow_userdef *userdef)
{
	static const __be32 ipv6_full_mask[4] = {cpu_to_be32(0xffffffff),
		cpu_to_be32(0xffffffff), cpu_to_be32(0xffffffff),
		cpu_to_be32(0xffffffff)};
	struct ethtool_tcpip6_spec *tcp_ip6_spec;
	struct ethtool_usrip6_spec *usr_ip6_spec;
	struct ethtool_tcpip4_spec *tcp_ip4_spec;
	struct ethtool_usrip4_spec *usr_ip4_spec;
	struct i40e_pf *pf = vsi->back;
	u64 current_mask, new_mask;
	bool new_flex_offset = false;
	bool flex_l3 = false;
	u16 *fdir_filter_count;
	u16 index, src_offset = 0;
	u8 pit_index = 0;
	int err;

	switch (fsp->flow_type & ~FLOW_EXT) {
	case SCTP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_SCTP;
		fdir_filter_count = &pf->fd_sctp4_filter_cnt;
		break;
	case TCP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
		fdir_filter_count = &pf->fd_tcp4_filter_cnt;
		break;
	case UDP_V4_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
		fdir_filter_count = &pf->fd_udp4_filter_cnt;
		break;
	case SCTP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_SCTP;
		fdir_filter_count = &pf->fd_sctp6_filter_cnt;
		break;
	case TCP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_TCP;
		fdir_filter_count = &pf->fd_tcp6_filter_cnt;
		break;
	case UDP_V6_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_UDP;
		fdir_filter_count = &pf->fd_udp6_filter_cnt;
		break;
	case IP_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV4_OTHER;
		fdir_filter_count = &pf->fd_ip4_filter_cnt;
		flex_l3 = true;
		break;
	case IPV6_USER_FLOW:
		index = I40E_FILTER_PCTYPE_NONF_IPV6_OTHER;
		fdir_filter_count = &pf->fd_ip6_filter_cnt;
		flex_l3 = true;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Read the current input set from register memory. */
	current_mask = i40e_read_fd_input_set(pf, index);
	new_mask = current_mask;

	/* Determine, if any, the required changes to the input set in order
	 * to support the provided mask.
	 *
	 * Hardware only supports masking at word (2 byte) granularity and does
	 * not support full bitwise masking. This implementation simplifies
	 * even further and only supports fully enabled or fully disabled
	 * masks for each field, even though we could split the ip4src and
	 * ip4dst fields.
	 */
	switch (fsp->flow_type & ~FLOW_EXT) {
	case SCTP_V4_FLOW:
		new_mask &= ~I40E_VERIFY_TAG_MASK;
		fallthrough;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		tcp_ip4_spec = &fsp->m_u.tcp_ip4_spec;

		/* IPv4 source address */
		if (tcp_ip4_spec->ip4src == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_SRC_MASK;
		else if (!tcp_ip4_spec->ip4src)
			new_mask &= ~I40E_L3_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* IPv4 destination address */
		if (tcp_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_DST_MASK;
		else if (!tcp_ip4_spec->ip4dst)
			new_mask &= ~I40E_L3_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 source port */
		if (tcp_ip4_spec->psrc == htons(0xFFFF))
			new_mask |= I40E_L4_SRC_MASK;
		else if (!tcp_ip4_spec->psrc)
			new_mask &= ~I40E_L4_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 destination port */
		if (tcp_ip4_spec->pdst == htons(0xFFFF))
			new_mask |= I40E_L4_DST_MASK;
		else if (!tcp_ip4_spec->pdst)
			new_mask &= ~I40E_L4_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* Filtering on Type of Service is not supported. */
		if (tcp_ip4_spec->tos)
			return -EOPNOTSUPP;

		break;
	case SCTP_V6_FLOW:
		new_mask &= ~I40E_VERIFY_TAG_MASK;
		fallthrough;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		tcp_ip6_spec = &fsp->m_u.tcp_ip6_spec;

		/* Check if user provided IPv6 source address. */
		if (ipv6_addr_equal((struct in6_addr *)&tcp_ip6_spec->ip6src,
				    (struct in6_addr *)&ipv6_full_mask))
			new_mask |= I40E_L3_V6_SRC_MASK;
		else if (ipv6_addr_any((struct in6_addr *)
				       &tcp_ip6_spec->ip6src))
			new_mask &= ~I40E_L3_V6_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* Check if user provided destination address. */
		if (ipv6_addr_equal((struct in6_addr *)&tcp_ip6_spec->ip6dst,
				    (struct in6_addr *)&ipv6_full_mask))
			new_mask |= I40E_L3_V6_DST_MASK;
		else if (ipv6_addr_any((struct in6_addr *)
				       &tcp_ip6_spec->ip6dst))
			new_mask &= ~I40E_L3_V6_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 source port */
		if (tcp_ip6_spec->psrc == htons(0xFFFF))
			new_mask |= I40E_L4_SRC_MASK;
		else if (!tcp_ip6_spec->psrc)
			new_mask &= ~I40E_L4_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* L4 destination port */
		if (tcp_ip6_spec->pdst == htons(0xFFFF))
			new_mask |= I40E_L4_DST_MASK;
		else if (!tcp_ip6_spec->pdst)
			new_mask &= ~I40E_L4_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* Filtering on Traffic Classes is not supported. */
		if (tcp_ip6_spec->tclass)
			return -EOPNOTSUPP;
		break;
	case IP_USER_FLOW:
		usr_ip4_spec = &fsp->m_u.usr_ip4_spec;

		/* IPv4 source address */
		if (usr_ip4_spec->ip4src == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_SRC_MASK;
		else if (!usr_ip4_spec->ip4src)
			new_mask &= ~I40E_L3_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* IPv4 destination address */
		if (usr_ip4_spec->ip4dst == htonl(0xFFFFFFFF))
			new_mask |= I40E_L3_DST_MASK;
		else if (!usr_ip4_spec->ip4dst)
			new_mask &= ~I40E_L3_DST_MASK;
		else
			return -EOPNOTSUPP;

		/* First 4 bytes of L4 header */
		if (usr_ip4_spec->l4_4_bytes == htonl(0xFFFFFFFF))
			new_mask |= I40E_L4_SRC_MASK | I40E_L4_DST_MASK;
		else if (!usr_ip4_spec->l4_4_bytes)
			new_mask &= ~(I40E_L4_SRC_MASK | I40E_L4_DST_MASK);
		else
			return -EOPNOTSUPP;

		/* Filtering on Type of Service is not supported. */
		if (usr_ip4_spec->tos)
			return -EOPNOTSUPP;

		/* Filtering on IP version is not supported */
		if (usr_ip4_spec->ip_ver)
			return -EINVAL;

		/* Filtering on L4 protocol is not supported */
		if (usr_ip4_spec->proto)
			return -EINVAL;

		break;
	case IPV6_USER_FLOW:
		usr_ip6_spec = &fsp->m_u.usr_ip6_spec;

		/* Check if user provided IPv6 source address. */
		if (ipv6_addr_equal((struct in6_addr *)&usr_ip6_spec->ip6src,
				    (struct in6_addr *)&ipv6_full_mask))
			new_mask |= I40E_L3_V6_SRC_MASK;
		else if (ipv6_addr_any((struct in6_addr *)
				       &usr_ip6_spec->ip6src))
			new_mask &= ~I40E_L3_V6_SRC_MASK;
		else
			return -EOPNOTSUPP;

		/* Check if user provided destination address. */
		if (ipv6_addr_equal((struct in6_addr *)&usr_ip6_spec->ip6dst,
				    (struct in6_addr *)&ipv6_full_mask))
			new_mask |= I40E_L3_V6_DST_MASK;
		else if (ipv6_addr_any((struct in6_addr *)
				       &usr_ip6_spec->ip6src))
			new_mask &= ~I40E_L3_V6_DST_MASK;
		else
			return -EOPNOTSUPP;

		if (usr_ip6_spec->l4_4_bytes == htonl(0xFFFFFFFF))
			new_mask |= I40E_L4_SRC_MASK | I40E_L4_DST_MASK;
		else if (!usr_ip6_spec->l4_4_bytes)
			new_mask &= ~(I40E_L4_SRC_MASK | I40E_L4_DST_MASK);
		else
			return -EOPNOTSUPP;

		/* Filtering on Traffic class is not supported. */
		if (usr_ip6_spec->tclass)
			return -EOPNOTSUPP;

		/* Filtering on L4 protocol is not supported */
		if (usr_ip6_spec->l4_proto)
			return -EINVAL;

		break;
	default:
		return -EOPNOTSUPP;
	}

	if (fsp->flow_type & FLOW_EXT) {
		/* Allow only 802.1Q and no etype defined, as
		 * later it's modified to 0x8100
		 */
		if (fsp->h_ext.vlan_etype != htons(ETH_P_8021Q) &&
		    fsp->h_ext.vlan_etype != 0)
			return -EOPNOTSUPP;
		if (fsp->m_ext.vlan_tci == htons(0xFFFF))
			new_mask |= I40E_VLAN_SRC_MASK;
		else
			new_mask &= ~I40E_VLAN_SRC_MASK;
	}

	/* First, clear all flexible filter entries */
	new_mask &= ~I40E_FLEX_INPUT_MASK;

	/* If we have a flexible filter, try to add this offset to the correct
	 * flexible filter PIT list. Once finished, we can update the mask.
	 * If the src_offset changed, we will get a new mask value which will
	 * trigger an input set change.
	 */
	if (userdef->flex_filter) {
		struct i40e_flex_pit *l3_flex_pit = NULL, *flex_pit = NULL;

		/* Flexible offset must be even, since the flexible payload
		 * must be aligned on 2-byte boundary.
		 */
		if (userdef->flex_offset & 0x1) {
			dev_warn(&pf->pdev->dev,
				 "Flexible data offset must be 2-byte aligned\n");
			return -EINVAL;
		}

		src_offset = userdef->flex_offset >> 1;

		/* FLX_PIT source offset value is only so large */
		if (src_offset > I40E_MAX_FLEX_SRC_OFFSET) {
			dev_warn(&pf->pdev->dev,
				 "Flexible data must reside within first 64 bytes of the packet payload\n");
			return -EINVAL;
		}

		/* See if this offset has already been programmed. If we get
		 * an ERR_PTR, then the filter is not safe to add. Otherwise,
		 * if we get a NULL pointer, this means we will need to add
		 * the offset.
		 */
		flex_pit = i40e_find_flex_offset(&pf->l4_flex_pit_list,
						 src_offset);
		if (IS_ERR(flex_pit))
			return PTR_ERR(flex_pit);

		/* IP_USER_FLOW filters match both L4 (ICMP) and L3 (unknown)
		 * packet types, and thus we need to program both L3 and L4
		 * flexible values. These must have identical flexible index,
		 * as otherwise we can't correctly program the input set. So
		 * we'll find both an L3 and L4 index and make sure they are
		 * the same.
		 */
		if (flex_l3) {
			l3_flex_pit =
				i40e_find_flex_offset(&pf->l3_flex_pit_list,
						      src_offset);
			if (IS_ERR(l3_flex_pit))
				return PTR_ERR(l3_flex_pit);

			if (flex_pit) {
				/* If we already had a matching L4 entry, we
				 * need to make sure that the L3 entry we
				 * obtained uses the same index.
				 */
				if (l3_flex_pit) {
					if (l3_flex_pit->pit_index !=
					    flex_pit->pit_index) {
						return -EINVAL;
					}
				} else {
					new_flex_offset = true;
				}
			} else {
				flex_pit = l3_flex_pit;
			}
		}

		/* If we didn't find an existing flex offset, we need to
		 * program a new one. However, we don't immediately program it
		 * here because we will wait to program until after we check
		 * that it is safe to change the input set.
		 */
		if (!flex_pit) {
			new_flex_offset = true;
			pit_index = i40e_unused_pit_index(pf);
		} else {
			pit_index = flex_pit->pit_index;
		}

		/* Update the mask with the new offset */
		new_mask |= i40e_pit_index_to_mask(pit_index);
	}

	/* If the mask and flexible filter offsets for this filter match the
	 * currently programmed values we don't need any input set change, so
	 * this filter is safe to install.
	 */
	if (new_mask == current_mask && !new_flex_offset)
		return 0;

	netif_info(pf, drv, vsi->netdev, "Input set change requested for %s flows:\n",
		   i40e_flow_str(fsp));
	i40e_print_input_set(vsi, current_mask, new_mask);
	if (new_flex_offset) {
		netif_info(pf, drv, vsi->netdev, "FLEX index %d: Offset -> %d",
			   pit_index, src_offset);
	}

	/* Hardware input sets are global across multiple ports, so even the
	 * main port cannot change them when in MFP mode as this would impact
	 * any filters on the other ports.
	 */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		netif_err(pf, drv, vsi->netdev, "Cannot change Flow Director input sets while MFP is enabled\n");
		return -EOPNOTSUPP;
	}

	/* This filter requires us to update the input set. However, hardware
	 * only supports one input set per flow type, and does not support
	 * separate masks for each filter. This means that we can only support
	 * a single mask for all filters of a specific type.
	 *
	 * If we have preexisting filters, they obviously depend on the
	 * current programmed input set. Display a diagnostic message in this
	 * case explaining why the filter could not be accepted.
	 */
	if (*fdir_filter_count) {
		netif_err(pf, drv, vsi->netdev, "Cannot change input set for %s flows until %d preexisting filters are removed\n",
			  i40e_flow_str(fsp),
			  *fdir_filter_count);
		return -EOPNOTSUPP;
	}

	i40e_write_fd_input_set(pf, index, new_mask);

	/* IP_USER_FLOW filters match both IPv4/Other and IPv4/Fragmented
	 * frames. If we're programming the input set for IPv4/Other, we also
	 * need to program the IPv4/Fragmented input set. Since we don't have
	 * separate support, we'll always assume and enforce that the two flow
	 * types must have matching input sets.
	 */
	if (index == I40E_FILTER_PCTYPE_NONF_IPV4_OTHER)
		i40e_write_fd_input_set(pf, I40E_FILTER_PCTYPE_FRAG_IPV4,
					new_mask);

	/* Add the new offset and update table, if necessary */
	if (new_flex_offset) {
		err = i40e_add_flex_offset(&pf->l4_flex_pit_list, src_offset,
					   pit_index);
		if (err)
			return err;

		if (flex_l3) {
			err = i40e_add_flex_offset(&pf->l3_flex_pit_list,
						   src_offset,
						   pit_index);
			if (err)
				return err;
		}

		i40e_reprogram_flex_pit(pf);
	}

	return 0;
}

/**
 * i40e_match_fdir_filter - Return true of two filters match
 * @a: pointer to filter struct
 * @b: pointer to filter struct
 *
 * Returns true if the two filters match exactly the same criteria. I.e. they
 * match the same flow type and have the same parameters. We don't need to
 * check any input-set since all filters of the same flow type must use the
 * same input set.
 **/
static bool i40e_match_fdir_filter(struct i40e_fdir_filter *a,
				   struct i40e_fdir_filter *b)
{
	/* The filters do not much if any of these criteria differ. */
	if (a->dst_ip != b->dst_ip ||
	    a->src_ip != b->src_ip ||
	    a->dst_port != b->dst_port ||
	    a->src_port != b->src_port ||
	    a->flow_type != b->flow_type ||
	    a->ipl4_proto != b->ipl4_proto ||
	    a->vlan_tag != b->vlan_tag ||
	    a->vlan_etype != b->vlan_etype)
		return false;

	return true;
}

/**
 * i40e_disallow_matching_filters - Check that new filters differ
 * @vsi: pointer to the targeted VSI
 * @input: new filter to check
 *
 * Due to hardware limitations, it is not possible for two filters that match
 * similar criteria to be programmed at the same time. This is true for a few
 * reasons:
 *
 * (a) all filters matching a particular flow type must use the same input
 * set, that is they must match the same criteria.
 * (b) different flow types will never match the same packet, as the flow type
 * is decided by hardware before checking which rules apply.
 * (c) hardware has no way to distinguish which order filters apply in.
 *
 * Due to this, we can't really support using the location data to order
 * filters in the hardware parsing. It is technically possible for the user to
 * request two filters matching the same criteria but which select different
 * queues. In this case, rather than keep both filters in the list, we reject
 * the 2nd filter when the user requests adding it.
 *
 * This avoids needing to track location for programming the filter to
 * hardware, and ensures that we avoid some strange scenarios involving
 * deleting filters which match the same criteria.
 **/
static int i40e_disallow_matching_filters(struct i40e_vsi *vsi,
					  struct i40e_fdir_filter *input)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_fdir_filter *rule;
	struct hlist_node *node2;

	/* Loop through every filter, and check that it doesn't match */
	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		/* Don't check the filters match if they share the same fd_id,
		 * since the new filter is actually just updating the target
		 * of the old filter.
		 */
		if (rule->fd_id == input->fd_id)
			continue;

		/* If any filters match, then print a warning message to the
		 * kernel message buffer and bail out.
		 */
		if (i40e_match_fdir_filter(rule, input)) {
			dev_warn(&pf->pdev->dev,
				 "Existing user defined filter %d already matches this flow.\n",
				 rule->fd_id);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * i40e_add_fdir_ethtool - Add/Remove Flow Director filters
 * @vsi: pointer to the targeted VSI
 * @cmd: command to get or set RX flow classification rules
 *
 * Add Flow Director filters for a specific flow spec based on their
 * protocol.  Returns 0 if the filters were successfully added.
 **/
static int i40e_add_fdir_ethtool(struct i40e_vsi *vsi,
				 struct ethtool_rxnfc *cmd)
{
	struct i40e_rx_flow_userdef userdef;
	struct ethtool_rx_flow_spec *fsp;
	struct i40e_fdir_filter *input;
	u16 dest_vsi = 0, q_index = 0;
	struct i40e_pf *pf;
	int ret = -EINVAL;
	u8 dest_ctl;

	if (!vsi)
		return -EINVAL;
	pf = vsi->back;

	if (!(pf->flags & I40E_FLAG_FD_SB_ENABLED))
		return -EOPNOTSUPP;

	if (test_bit(__I40E_FD_SB_AUTO_DISABLED, pf->state))
		return -ENOSPC;

	if (test_bit(__I40E_RESET_RECOVERY_PENDING, pf->state) ||
	    test_bit(__I40E_RESET_INTR_RECEIVED, pf->state))
		return -EBUSY;

	if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
		return -EBUSY;

	fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	/* Parse the user-defined field */
	if (i40e_parse_rx_flow_user_data(fsp, &userdef))
		return -EINVAL;

	/* Extended MAC field is not supported */
	if (fsp->flow_type & FLOW_MAC_EXT)
		return -EINVAL;

	ret = i40e_check_fdir_input_set(vsi, fsp, &userdef);
	if (ret)
		return ret;

	if (fsp->location >= (pf->hw.func_caps.fd_filters_best_effort +
			      pf->hw.func_caps.fd_filters_guaranteed)) {
		return -EINVAL;
	}

	/* ring_cookie is either the drop index, or is a mask of the queue
	 * index and VF id we wish to target.
	 */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC) {
		dest_ctl = I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET;
	} else {
		u32 ring = ethtool_get_flow_spec_ring(fsp->ring_cookie);
		u8 vf = ethtool_get_flow_spec_ring_vf(fsp->ring_cookie);

		if (!vf) {
			if (ring >= vsi->num_queue_pairs)
				return -EINVAL;
			dest_vsi = vsi->id;
		} else {
			/* VFs are zero-indexed, so we subtract one here */
			vf--;

			if (vf >= pf->num_alloc_vfs)
				return -EINVAL;
			if (ring >= pf->vf[vf].num_queue_pairs)
				return -EINVAL;
			dest_vsi = pf->vf[vf].lan_vsi_id;
		}
		dest_ctl = I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_QINDEX;
		q_index = ring;
	}

	input = kzalloc(sizeof(*input), GFP_KERNEL);

	if (!input)
		return -ENOMEM;

	input->fd_id = fsp->location;
	input->q_index = q_index;
	input->dest_vsi = dest_vsi;
	input->dest_ctl = dest_ctl;
	input->fd_status = I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID;
	input->cnt_index  = I40E_FD_SB_STAT_IDX(pf->hw.pf_id);
	input->dst_ip = fsp->h_u.tcp_ip4_spec.ip4src;
	input->src_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
	input->flow_type = fsp->flow_type & ~FLOW_EXT;

	input->vlan_etype = fsp->h_ext.vlan_etype;
	if (!fsp->m_ext.vlan_etype && fsp->h_ext.vlan_tci)
		input->vlan_etype = cpu_to_be16(ETH_P_8021Q);
	if (fsp->m_ext.vlan_tci && input->vlan_etype)
		input->vlan_tag = fsp->h_ext.vlan_tci;
	if (input->flow_type == IPV6_USER_FLOW ||
	    input->flow_type == UDP_V6_FLOW ||
	    input->flow_type == TCP_V6_FLOW ||
	    input->flow_type == SCTP_V6_FLOW) {
		/* Reverse the src and dest notion, since the HW expects them
		 * to be from Tx perspective where as the input from user is
		 * from Rx filter view.
		 */
		input->ipl4_proto = fsp->h_u.usr_ip6_spec.l4_proto;
		input->dst_port = fsp->h_u.tcp_ip6_spec.psrc;
		input->src_port = fsp->h_u.tcp_ip6_spec.pdst;
		memcpy(input->dst_ip6, fsp->h_u.ah_ip6_spec.ip6src,
		       sizeof(__be32) * 4);
		memcpy(input->src_ip6, fsp->h_u.ah_ip6_spec.ip6dst,
		       sizeof(__be32) * 4);
	} else {
		/* Reverse the src and dest notion, since the HW expects them
		 * to be from Tx perspective where as the input from user is
		 * from Rx filter view.
		 */
		input->ipl4_proto = fsp->h_u.usr_ip4_spec.proto;
		input->dst_port = fsp->h_u.tcp_ip4_spec.psrc;
		input->src_port = fsp->h_u.tcp_ip4_spec.pdst;
		input->dst_ip = fsp->h_u.tcp_ip4_spec.ip4src;
		input->src_ip = fsp->h_u.tcp_ip4_spec.ip4dst;
	}

	if (userdef.flex_filter) {
		input->flex_filter = true;
		input->flex_word = cpu_to_be16(userdef.flex_word);
		input->flex_offset = userdef.flex_offset;
	}

	/* Avoid programming two filters with identical match criteria. */
	ret = i40e_disallow_matching_filters(vsi, input);
	if (ret)
		goto free_filter_memory;

	/* Add the input filter to the fdir_input_list, possibly replacing
	 * a previous filter. Do not free the input structure after adding it
	 * to the list as this would cause a use-after-free bug.
	 */
	i40e_update_ethtool_fdir_entry(vsi, input, fsp->location, NULL);
	ret = i40e_add_del_fdir(vsi, input, true);
	if (ret)
		goto remove_sw_rule;
	return 0;

remove_sw_rule:
	hlist_del(&input->fdir_node);
	pf->fdir_pf_active_filters--;
free_filter_memory:
	kfree(input);
	return ret;
}

/**
 * i40e_set_rxnfc - command to set RX flow classification rules
 * @netdev: network interface device structure
 * @cmd: ethtool rxnfc command
 *
 * Returns Success if the command is supported.
 **/
static int i40e_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = i40e_set_rss_hash_opt(pf, cmd);
		break;
	case ETHTOOL_SRXCLSRLINS:
		ret = i40e_add_fdir_ethtool(vsi, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = i40e_del_fdir_entry(vsi, cmd);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * i40e_max_channels - get Max number of combined channels supported
 * @vsi: vsi pointer
 **/
static unsigned int i40e_max_channels(struct i40e_vsi *vsi)
{
	/* TODO: This code assumes DCB and FD is disabled for now. */
	return vsi->alloc_queue_pairs;
}

/**
 * i40e_get_channels - Get the current channels enabled and max supported etc.
 * @dev: network interface device structure
 * @ch: ethtool channels structure
 *
 * We don't support separate tx and rx queues as channels. The other count
 * represents how many queues are being used for control. max_combined counts
 * how many queue pairs we can support. They may not be mapped 1 to 1 with
 * q_vectors since we support a lot more queue pairs than q_vectors.
 **/
static void i40e_get_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	/* report maximum channels */
	ch->max_combined = i40e_max_channels(vsi);

	/* report info for other vector */
	ch->other_count = (pf->flags & I40E_FLAG_FD_SB_ENABLED) ? 1 : 0;
	ch->max_other = ch->other_count;

	/* Note: This code assumes DCB is disabled for now. */
	ch->combined_count = vsi->num_queue_pairs;
}

/**
 * i40e_set_channels - Set the new channels count.
 * @dev: network interface device structure
 * @ch: ethtool channels structure
 *
 * The new channels count may not be the same as requested by the user
 * since it gets rounded down to a power of 2 value.
 **/
static int i40e_set_channels(struct net_device *dev,
			     struct ethtool_channels *ch)
{
	const u8 drop = I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET;
	struct i40e_netdev_priv *np = netdev_priv(dev);
	unsigned int count = ch->combined_count;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_fdir_filter *rule;
	struct hlist_node *node2;
	int new_count;
	int err = 0;

	/* We do not support setting channels for any other VSI at present */
	if (vsi->type != I40E_VSI_MAIN)
		return -EINVAL;

	/* We do not support setting channels via ethtool when TCs are
	 * configured through mqprio
	 */
	if (pf->flags & I40E_FLAG_TC_MQPRIO)
		return -EINVAL;

	/* verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* verify other_count has not changed */
	if (ch->other_count != ((pf->flags & I40E_FLAG_FD_SB_ENABLED) ? 1 : 0))
		return -EINVAL;

	/* verify the number of channels does not exceed hardware limits */
	if (count > i40e_max_channels(vsi))
		return -EINVAL;

	/* verify that the number of channels does not invalidate any current
	 * flow director rules
	 */
	hlist_for_each_entry_safe(rule, node2,
				  &pf->fdir_filter_list, fdir_node) {
		if (rule->dest_ctl != drop && count <= rule->q_index) {
			dev_warn(&pf->pdev->dev,
				 "Existing user defined filter %d assigns flow to queue %d\n",
				 rule->fd_id, rule->q_index);
			err = -EINVAL;
		}
	}

	if (err) {
		dev_err(&pf->pdev->dev,
			"Existing filter rules must be deleted to reduce combined channel count to %d\n",
			count);
		return err;
	}

	/* update feature limits from largest to smallest supported values */
	/* TODO: Flow director limit, DCB etc */

	/* use rss_reconfig to rebuild with new queue count and update traffic
	 * class queue mapping
	 */
	new_count = i40e_reconfig_rss_queues(pf, count);
	if (new_count > 0)
		return 0;
	else
		return -EINVAL;
}

/**
 * i40e_get_rxfh_key_size - get the RSS hash key size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40e_get_rxfh_key_size(struct net_device *netdev)
{
	return I40E_HKEY_ARRAY_SIZE;
}

/**
 * i40e_get_rxfh_indir_size - get the rx flow hash indirection table size
 * @netdev: network interface device structure
 *
 * Returns the table size.
 **/
static u32 i40e_get_rxfh_indir_size(struct net_device *netdev)
{
	return I40E_HLUT_ARRAY_SIZE;
}

/**
 * i40e_get_rxfh - get the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function
 *
 * Reads the indirection table directly from the hardware. Returns 0 on
 * success.
 **/
static int i40e_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
			 u8 *hfunc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	u8 *lut, *seed = NULL;
	int ret;
	u16 i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	if (!indir)
		return 0;

	seed = key;
	lut = kzalloc(I40E_HLUT_ARRAY_SIZE, GFP_KERNEL);
	if (!lut)
		return -ENOMEM;
	ret = i40e_get_rss(vsi, seed, lut, I40E_HLUT_ARRAY_SIZE);
	if (ret)
		goto out;
	for (i = 0; i < I40E_HLUT_ARRAY_SIZE; i++)
		indir[i] = (u32)(lut[i]);

out:
	kfree(lut);

	return ret;
}

/**
 * i40e_set_rxfh - set the rx flow hash indirection table
 * @netdev: network interface device structure
 * @indir: indirection table
 * @key: hash key
 * @hfunc: hash function to use
 *
 * Returns -EINVAL if the table specifies an invalid queue id, otherwise
 * returns 0 after programming the table.
 **/
static int i40e_set_rxfh(struct net_device *netdev, const u32 *indir,
			 const u8 *key, const u8 hfunc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u8 *seed = NULL;
	u16 i;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (key) {
		if (!vsi->rss_hkey_user) {
			vsi->rss_hkey_user = kzalloc(I40E_HKEY_ARRAY_SIZE,
						     GFP_KERNEL);
			if (!vsi->rss_hkey_user)
				return -ENOMEM;
		}
		memcpy(vsi->rss_hkey_user, key, I40E_HKEY_ARRAY_SIZE);
		seed = vsi->rss_hkey_user;
	}
	if (!vsi->rss_lut_user) {
		vsi->rss_lut_user = kzalloc(I40E_HLUT_ARRAY_SIZE, GFP_KERNEL);
		if (!vsi->rss_lut_user)
			return -ENOMEM;
	}

	/* Each 32 bits pointed by 'indir' is stored with a lut entry */
	if (indir)
		for (i = 0; i < I40E_HLUT_ARRAY_SIZE; i++)
			vsi->rss_lut_user[i] = (u8)(indir[i]);
	else
		i40e_fill_rss_lut(pf, vsi->rss_lut_user, I40E_HLUT_ARRAY_SIZE,
				  vsi->rss_size);

	return i40e_config_rss(vsi, seed, vsi->rss_lut_user,
			       I40E_HLUT_ARRAY_SIZE);
}

/**
 * i40e_get_priv_flags - report device private flags
 * @dev: network interface device structure
 *
 * The get string set count and the string set should be matched for each
 * flag returned.  Add new strings for each flag to the i40e_gstrings_priv_flags
 * array.
 *
 * Returns a u32 bitmap of flags.
 **/
static u32 i40e_get_priv_flags(struct net_device *dev)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 i, j, ret_flags = 0;

	for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gstrings_priv_flags[i];

		if (priv_flags->flag & pf->flags)
			ret_flags |= BIT(i);
	}

	if (pf->hw.pf_id != 0)
		return ret_flags;

	for (j = 0; j < I40E_GL_PRIV_FLAGS_STR_LEN; j++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gl_gstrings_priv_flags[j];

		if (priv_flags->flag & pf->flags)
			ret_flags |= BIT(i + j);
	}

	return ret_flags;
}

/**
 * i40e_set_priv_flags - set private flags
 * @dev: network interface device structure
 * @flags: bit flags to be set
 **/
static int i40e_set_priv_flags(struct net_device *dev, u32 flags)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	u64 orig_flags, new_flags, changed_flags;
	enum i40e_admin_queue_err adq_err;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u32 reset_needed = 0;
	i40e_status status;
	u32 i, j;

	orig_flags = READ_ONCE(pf->flags);
	new_flags = orig_flags;

	for (i = 0; i < I40E_PRIV_FLAGS_STR_LEN; i++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gstrings_priv_flags[i];

		if (flags & BIT(i))
			new_flags |= priv_flags->flag;
		else
			new_flags &= ~(priv_flags->flag);

		/* If this is a read-only flag, it can't be changed */
		if (priv_flags->read_only &&
		    ((orig_flags ^ new_flags) & ~BIT(i)))
			return -EOPNOTSUPP;
	}

	if (pf->hw.pf_id != 0)
		goto flags_complete;

	for (j = 0; j < I40E_GL_PRIV_FLAGS_STR_LEN; j++) {
		const struct i40e_priv_flags *priv_flags;

		priv_flags = &i40e_gl_gstrings_priv_flags[j];

		if (flags & BIT(i + j))
			new_flags |= priv_flags->flag;
		else
			new_flags &= ~(priv_flags->flag);

		/* If this is a read-only flag, it can't be changed */
		if (priv_flags->read_only &&
		    ((orig_flags ^ new_flags) & ~BIT(i)))
			return -EOPNOTSUPP;
	}

flags_complete:
	changed_flags = orig_flags ^ new_flags;

	if (changed_flags & I40E_FLAG_DISABLE_FW_LLDP)
		reset_needed = I40E_PF_RESET_AND_REBUILD_FLAG;
	if (changed_flags & (I40E_FLAG_VEB_STATS_ENABLED |
	    I40E_FLAG_LEGACY_RX | I40E_FLAG_SOURCE_PRUNING_DISABLED))
		reset_needed = BIT(__I40E_PF_RESET_REQUESTED);

	/* Before we finalize any flag changes, we need to perform some
	 * checks to ensure that the changes are supported and safe.
	 */

	/* ATR eviction is not supported on all devices */
	if ((new_flags & I40E_FLAG_HW_ATR_EVICT_ENABLED) &&
	    !(pf->hw_features & I40E_HW_ATR_EVICT_CAPABLE))
		return -EOPNOTSUPP;

	/* If the driver detected FW LLDP was disabled on init, this flag could
	 * be set, however we do not support _changing_ the flag:
	 * - on XL710 if NPAR is enabled or FW API version < 1.7
	 * - on X722 with FW API version < 1.6
	 * There are situations where older FW versions/NPAR enabled PFs could
	 * disable LLDP, however we _must_ not allow the user to enable/disable
	 * LLDP with this flag on unsupported FW versions.
	 */
	if (changed_flags & I40E_FLAG_DISABLE_FW_LLDP) {
		if (!(pf->hw.flags & I40E_HW_FLAG_FW_LLDP_STOPPABLE)) {
			dev_warn(&pf->pdev->dev,
				 "Device does not support changing FW LLDP\n");
			return -EOPNOTSUPP;
		}
	}

	if (changed_flags & I40E_FLAG_RS_FEC &&
	    pf->hw.device_id != I40E_DEV_ID_25G_SFP28 &&
	    pf->hw.device_id != I40E_DEV_ID_25G_B) {
		dev_warn(&pf->pdev->dev,
			 "Device does not support changing FEC configuration\n");
		return -EOPNOTSUPP;
	}

	if (changed_flags & I40E_FLAG_BASE_R_FEC &&
	    pf->hw.device_id != I40E_DEV_ID_25G_SFP28 &&
	    pf->hw.device_id != I40E_DEV_ID_25G_B &&
	    pf->hw.device_id != I40E_DEV_ID_KX_X722) {
		dev_warn(&pf->pdev->dev,
			 "Device does not support changing FEC configuration\n");
		return -EOPNOTSUPP;
	}

	/* Process any additional changes needed as a result of flag changes.
	 * The changed_flags value reflects the list of bits that were
	 * changed in the code above.
	 */

	/* Flush current ATR settings if ATR was disabled */
	if ((changed_flags & I40E_FLAG_FD_ATR_ENABLED) &&
	    !(new_flags & I40E_FLAG_FD_ATR_ENABLED)) {
		set_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state);
		set_bit(__I40E_FD_FLUSH_REQUESTED, pf->state);
	}

	if (changed_flags & I40E_FLAG_TRUE_PROMISC_SUPPORT) {
		u16 sw_flags = 0, valid_flags = 0;
		int ret;

		if (!(new_flags & I40E_FLAG_TRUE_PROMISC_SUPPORT))
			sw_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		valid_flags = I40E_AQ_SET_SWITCH_CFG_PROMISC;
		ret = i40e_aq_set_switch_config(&pf->hw, sw_flags, valid_flags,
						0, NULL);
		if (ret && pf->hw.aq.asq_last_status != I40E_AQ_RC_ESRCH) {
			dev_info(&pf->pdev->dev,
				 "couldn't set switch config bits, err %s aq_err %s\n",
				 i40e_stat_str(&pf->hw, ret),
				 i40e_aq_str(&pf->hw,
					     pf->hw.aq.asq_last_status));
			/* not a fatal problem, just keep going */
		}
	}

	if ((changed_flags & I40E_FLAG_RS_FEC) ||
	    (changed_flags & I40E_FLAG_BASE_R_FEC)) {
		u8 fec_cfg = 0;

		if (new_flags & I40E_FLAG_RS_FEC &&
		    new_flags & I40E_FLAG_BASE_R_FEC) {
			fec_cfg = I40E_AQ_SET_FEC_AUTO;
		} else if (new_flags & I40E_FLAG_RS_FEC) {
			fec_cfg = (I40E_AQ_SET_FEC_REQUEST_RS |
				   I40E_AQ_SET_FEC_ABILITY_RS);
		} else if (new_flags & I40E_FLAG_BASE_R_FEC) {
			fec_cfg = (I40E_AQ_SET_FEC_REQUEST_KR |
				   I40E_AQ_SET_FEC_ABILITY_KR);
		}
		if (i40e_set_fec_cfg(dev, fec_cfg))
			dev_warn(&pf->pdev->dev, "Cannot change FEC config\n");
	}

	if ((changed_flags & I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED) &&
	    (orig_flags & I40E_FLAG_TOTAL_PORT_SHUTDOWN_ENABLED)) {
		dev_err(&pf->pdev->dev,
			"Setting link-down-on-close not supported on this port (because total-port-shutdown is enabled)\n");
		return -EOPNOTSUPP;
	}

	if ((changed_flags & new_flags &
	     I40E_FLAG_LINK_DOWN_ON_CLOSE_ENABLED) &&
	    (new_flags & I40E_FLAG_MFP_ENABLED))
		dev_warn(&pf->pdev->dev,
			 "Turning on link-down-on-close flag may affect other partitions\n");

	if (changed_flags & I40E_FLAG_DISABLE_FW_LLDP) {
		if (new_flags & I40E_FLAG_DISABLE_FW_LLDP) {
#ifdef CONFIG_I40E_DCB
			i40e_dcb_sw_default_config(pf);
#endif /* CONFIG_I40E_DCB */
			i40e_aq_cfg_lldp_mib_change_event(&pf->hw, false, NULL);
			i40e_aq_stop_lldp(&pf->hw, true, false, NULL);
		} else {
			i40e_set_lldp_forwarding(pf, false);
			status = i40e_aq_start_lldp(&pf->hw, false, NULL);
			if (status) {
				adq_err = pf->hw.aq.asq_last_status;
				switch (adq_err) {
				case I40E_AQ_RC_EEXIST:
					dev_warn(&pf->pdev->dev,
						 "FW LLDP agent is already running\n");
					reset_needed = 0;
					break;
				case I40E_AQ_RC_EPERM:
					dev_warn(&pf->pdev->dev,
						 "Device configuration forbids SW from starting the LLDP agent.\n");
					return -EINVAL;
				default:
					dev_warn(&pf->pdev->dev,
						 "Starting FW LLDP agent failed: error: %s, %s\n",
						 i40e_stat_str(&pf->hw,
							       status),
						 i40e_aq_str(&pf->hw,
							     adq_err));
					return -EINVAL;
				}
			}
		}
	}

	/* Now that we've checked to ensure that the new flags are valid, load
	 * them into place. Since we only modify flags either (a) during
	 * initialization or (b) while holding the RTNL lock, we don't need
	 * anything fancy here.
	 */
	pf->flags = new_flags;

	/* Issue reset to cause things to take effect, as additional bits
	 * are added we will need to create a mask of bits requiring reset
	 */
	if (reset_needed)
		i40e_do_reset(pf, reset_needed, true);

	return 0;
}

/**
 * i40e_get_module_info - get (Q)SFP+ module type info
 * @netdev: network interface device structure
 * @modinfo: module EEPROM size and layout information structure
 **/
static int i40e_get_module_info(struct net_device *netdev,
				struct ethtool_modinfo *modinfo)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 sff8472_comp = 0;
	u32 sff8472_swap = 0;
	u32 sff8636_rev = 0;
	i40e_status status;
	u32 type = 0;

	/* Check if firmware supports reading module EEPROM. */
	if (!(hw->flags & I40E_HW_FLAG_AQ_PHY_ACCESS_CAPABLE)) {
		netdev_err(vsi->netdev, "Module EEPROM memory read not supported. Please update the NVM image.\n");
		return -EINVAL;
	}

	status = i40e_update_link_info(hw);
	if (status)
		return -EIO;

	if (hw->phy.link_info.phy_type == I40E_PHY_TYPE_EMPTY) {
		netdev_err(vsi->netdev, "Cannot read module EEPROM memory. No module connected.\n");
		return -EINVAL;
	}

	type = hw->phy.link_info.module_type[0];

	switch (type) {
	case I40E_MODULE_TYPE_SFP:
		status = i40e_aq_get_phy_register(hw,
				I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
				I40E_I2C_EEPROM_DEV_ADDR, true,
				I40E_MODULE_SFF_8472_COMP,
				&sff8472_comp, NULL);
		if (status)
			return -EIO;

		status = i40e_aq_get_phy_register(hw,
				I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
				I40E_I2C_EEPROM_DEV_ADDR, true,
				I40E_MODULE_SFF_8472_SWAP,
				&sff8472_swap, NULL);
		if (status)
			return -EIO;

		/* Check if the module requires address swap to access
		 * the other EEPROM memory page.
		 */
		if (sff8472_swap & I40E_MODULE_SFF_ADDR_MODE) {
			netdev_warn(vsi->netdev, "Module address swap to access page 0xA2 is not supported.\n");
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else if (sff8472_comp == 0x00) {
			/* Module is not SFF-8472 compliant */
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else if (!(sff8472_swap & I40E_MODULE_SFF_DDM_IMPLEMENTED)) {
			/* Module is SFF-8472 compliant but doesn't implement
			 * Digital Diagnostic Monitoring (DDM).
			 */
			modinfo->type = ETH_MODULE_SFF_8079;
			modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8472;
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		}
		break;
	case I40E_MODULE_TYPE_QSFP_PLUS:
		/* Read from memory page 0. */
		status = i40e_aq_get_phy_register(hw,
				I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
				0, true,
				I40E_MODULE_REVISION_ADDR,
				&sff8636_rev, NULL);
		if (status)
			return -EIO;
		/* Determine revision compliance byte */
		if (sff8636_rev > 0x02) {
			/* Module is SFF-8636 compliant */
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = I40E_MODULE_QSFP_MAX_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = I40E_MODULE_QSFP_MAX_LEN;
		}
		break;
	case I40E_MODULE_TYPE_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = I40E_MODULE_QSFP_MAX_LEN;
		break;
	default:
		netdev_err(vsi->netdev, "Module type unrecognized\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * i40e_get_module_eeprom - fills buffer with (Q)SFP+ module memory contents
 * @netdev: network interface device structure
 * @ee: EEPROM dump request structure
 * @data: buffer to be filled with EEPROM contents
 **/
static int i40e_get_module_eeprom(struct net_device *netdev,
				  struct ethtool_eeprom *ee,
				  u8 *data)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	bool is_sfp = false;
	i40e_status status;
	u32 value = 0;
	int i;

	if (!ee || !ee->len || !data)
		return -EINVAL;

	if (hw->phy.link_info.module_type[0] == I40E_MODULE_TYPE_SFP)
		is_sfp = true;

	for (i = 0; i < ee->len; i++) {
		u32 offset = i + ee->offset;
		u32 addr = is_sfp ? I40E_I2C_EEPROM_DEV_ADDR : 0;

		/* Check if we need to access the other memory page */
		if (is_sfp) {
			if (offset >= ETH_MODULE_SFF_8079_LEN) {
				offset -= ETH_MODULE_SFF_8079_LEN;
				addr = I40E_I2C_EEPROM_DEV_ADDR2;
			}
		} else {
			while (offset >= ETH_MODULE_SFF_8436_LEN) {
				/* Compute memory page number and offset. */
				offset -= ETH_MODULE_SFF_8436_LEN / 2;
				addr++;
			}
		}

		status = i40e_aq_get_phy_register(hw,
				I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
				true, addr, offset, &value, NULL);
		if (status)
			return -EIO;
		data[i] = value;
	}
	return 0;
}

static int i40e_get_eee(struct net_device *netdev, struct ethtool_eee *edata)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp phy_cfg;
	enum i40e_status_code status = 0;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;

	/* Get initial PHY capabilities */
	status = i40e_aq_get_phy_capabilities(hw, false, true, &phy_cfg, NULL);
	if (status)
		return -EAGAIN;

	/* Check whether NIC configuration is compatible with Energy Efficient
	 * Ethernet (EEE) mode.
	 */
	if (phy_cfg.eee_capability == 0)
		return -EOPNOTSUPP;

	edata->supported = SUPPORTED_Autoneg;
	edata->lp_advertised = edata->supported;

	/* Get current configuration */
	status = i40e_aq_get_phy_capabilities(hw, false, false, &phy_cfg, NULL);
	if (status)
		return -EAGAIN;

	edata->advertised = phy_cfg.eee_capability ? SUPPORTED_Autoneg : 0U;
	edata->eee_enabled = !!edata->advertised;
	edata->tx_lpi_enabled = pf->stats.tx_lpi_status;

	edata->eee_active = pf->stats.tx_lpi_status && pf->stats.rx_lpi_status;

	return 0;
}

static int i40e_is_eee_param_supported(struct net_device *netdev,
				       struct ethtool_eee *edata)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_ethtool_not_used {
		u32 value;
		const char *name;
	} param[] = {
		{edata->advertised & ~SUPPORTED_Autoneg, "advertise"},
		{edata->tx_lpi_timer, "tx-timer"},
		{edata->tx_lpi_enabled != pf->stats.tx_lpi_status, "tx-lpi"}
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(param); i++) {
		if (param[i].value) {
			netdev_info(netdev,
				    "EEE setting %s not supported\n",
				    param[i].name);
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int i40e_set_eee(struct net_device *netdev, struct ethtool_eee *edata)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_aq_get_phy_abilities_resp abilities;
	enum i40e_status_code status = I40E_SUCCESS;
	struct i40e_aq_set_phy_config config;
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	__le16 eee_capability;

	/* Deny parameters we don't support */
	if (i40e_is_eee_param_supported(netdev, edata))
		return -EOPNOTSUPP;

	/* Get initial PHY capabilities */
	status = i40e_aq_get_phy_capabilities(hw, false, true, &abilities,
					      NULL);
	if (status)
		return -EAGAIN;

	/* Check whether NIC configuration is compatible with Energy Efficient
	 * Ethernet (EEE) mode.
	 */
	if (abilities.eee_capability == 0)
		return -EOPNOTSUPP;

	/* Cache initial EEE capability */
	eee_capability = abilities.eee_capability;

	/* Get current PHY configuration */
	status = i40e_aq_get_phy_capabilities(hw, false, false, &abilities,
					      NULL);
	if (status)
		return -EAGAIN;

	/* Cache current PHY configuration */
	config.phy_type = abilities.phy_type;
	config.phy_type_ext = abilities.phy_type_ext;
	config.link_speed = abilities.link_speed;
	config.abilities = abilities.abilities |
			   I40E_AQ_PHY_ENABLE_ATOMIC_LINK;
	config.eeer = abilities.eeer_val;
	config.low_power_ctrl = abilities.d3_lpan;
	config.fec_config = abilities.fec_cfg_curr_mod_ext_info &
			    I40E_AQ_PHY_FEC_CONFIG_MASK;

	/* Set desired EEE state */
	if (edata->eee_enabled) {
		config.eee_capability = eee_capability;
		config.eeer |= cpu_to_le32(I40E_PRTPM_EEER_TX_LPI_EN_MASK);
	} else {
		config.eee_capability = 0;
		config.eeer &= cpu_to_le32(~I40E_PRTPM_EEER_TX_LPI_EN_MASK);
	}

	/* Apply modified PHY configuration */
	status = i40e_aq_set_phy_config(hw, &config, NULL);
	if (status)
		return -EAGAIN;

	return 0;
}

static const struct ethtool_ops i40e_ethtool_recovery_mode_ops = {
	.get_drvinfo		= i40e_get_drvinfo,
	.set_eeprom		= i40e_set_eeprom,
	.get_eeprom_len		= i40e_get_eeprom_len,
	.get_eeprom		= i40e_get_eeprom,
};

static const struct ethtool_ops i40e_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES_IRQ |
				     ETHTOOL_COALESCE_USE_ADAPTIVE |
				     ETHTOOL_COALESCE_RX_USECS_HIGH |
				     ETHTOOL_COALESCE_TX_USECS_HIGH,
	.get_drvinfo		= i40e_get_drvinfo,
	.get_regs_len		= i40e_get_regs_len,
	.get_regs		= i40e_get_regs,
	.nway_reset		= i40e_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_wol		= i40e_get_wol,
	.set_wol		= i40e_set_wol,
	.set_eeprom		= i40e_set_eeprom,
	.get_eeprom_len		= i40e_get_eeprom_len,
	.get_eeprom		= i40e_get_eeprom,
	.get_ringparam		= i40e_get_ringparam,
	.set_ringparam		= i40e_set_ringparam,
	.get_pauseparam		= i40e_get_pauseparam,
	.set_pauseparam		= i40e_set_pauseparam,
	.get_msglevel		= i40e_get_msglevel,
	.set_msglevel		= i40e_set_msglevel,
	.get_rxnfc		= i40e_get_rxnfc,
	.set_rxnfc		= i40e_set_rxnfc,
	.self_test		= i40e_diag_test,
	.get_strings		= i40e_get_strings,
	.get_eee		= i40e_get_eee,
	.set_eee		= i40e_set_eee,
	.set_phys_id		= i40e_set_phys_id,
	.get_sset_count		= i40e_get_sset_count,
	.get_ethtool_stats	= i40e_get_ethtool_stats,
	.get_coalesce		= i40e_get_coalesce,
	.set_coalesce		= i40e_set_coalesce,
	.get_rxfh_key_size	= i40e_get_rxfh_key_size,
	.get_rxfh_indir_size	= i40e_get_rxfh_indir_size,
	.get_rxfh		= i40e_get_rxfh,
	.set_rxfh		= i40e_set_rxfh,
	.get_channels		= i40e_get_channels,
	.set_channels		= i40e_set_channels,
	.get_module_info	= i40e_get_module_info,
	.get_module_eeprom	= i40e_get_module_eeprom,
	.get_ts_info		= i40e_get_ts_info,
	.get_priv_flags		= i40e_get_priv_flags,
	.set_priv_flags		= i40e_set_priv_flags,
	.get_per_queue_coalesce	= i40e_get_per_queue_coalesce,
	.set_per_queue_coalesce	= i40e_set_per_queue_coalesce,
	.get_link_ksettings	= i40e_get_link_ksettings,
	.set_link_ksettings	= i40e_set_link_ksettings,
	.get_fecparam = i40e_get_fec_param,
	.set_fecparam = i40e_set_fec_param,
	.flash_device = i40e_ddp_flash,
};

void i40e_set_ethtool_ops(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf		*pf = np->vsi->back;

	if (!test_bit(__I40E_RECOVERY_MODE, pf->state))
		netdev->ethtool_ops = &i40e_ethtool_ops;
	else
		netdev->ethtool_ops = &i40e_ethtool_recovery_mode_ops;
}
