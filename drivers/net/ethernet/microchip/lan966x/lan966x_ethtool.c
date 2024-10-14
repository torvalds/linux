// SPDX-License-Identifier: GPL-2.0+

#include <linux/netdevice.h>

#include "lan966x_main.h"

/* Number of traffic classes */
#define LAN966X_NUM_TC			8
#define LAN966X_STATS_CHECK_DELAY	(2 * HZ)

static const struct lan966x_stat_layout lan966x_stats_layout[] = {
	{ .name = "rx_octets", .offset = 0x00, },
	{ .name = "rx_unicast", .offset = 0x01, },
	{ .name = "rx_multicast", .offset = 0x02 },
	{ .name = "rx_broadcast", .offset = 0x03 },
	{ .name = "rx_short", .offset = 0x04 },
	{ .name = "rx_frag", .offset = 0x05 },
	{ .name = "rx_jabber", .offset = 0x06 },
	{ .name = "rx_crc", .offset = 0x07 },
	{ .name = "rx_symbol_err", .offset = 0x08 },
	{ .name = "rx_sz_64", .offset = 0x09 },
	{ .name = "rx_sz_65_127", .offset = 0x0a},
	{ .name = "rx_sz_128_255", .offset = 0x0b},
	{ .name = "rx_sz_256_511", .offset = 0x0c },
	{ .name = "rx_sz_512_1023", .offset = 0x0d },
	{ .name = "rx_sz_1024_1526", .offset = 0x0e },
	{ .name = "rx_sz_jumbo", .offset = 0x0f },
	{ .name = "rx_pause", .offset = 0x10 },
	{ .name = "rx_control", .offset = 0x11 },
	{ .name = "rx_long", .offset = 0x12 },
	{ .name = "rx_cat_drop", .offset = 0x13 },
	{ .name = "rx_red_prio_0", .offset = 0x14 },
	{ .name = "rx_red_prio_1", .offset = 0x15 },
	{ .name = "rx_red_prio_2", .offset = 0x16 },
	{ .name = "rx_red_prio_3", .offset = 0x17 },
	{ .name = "rx_red_prio_4", .offset = 0x18 },
	{ .name = "rx_red_prio_5", .offset = 0x19 },
	{ .name = "rx_red_prio_6", .offset = 0x1a },
	{ .name = "rx_red_prio_7", .offset = 0x1b },
	{ .name = "rx_yellow_prio_0", .offset = 0x1c },
	{ .name = "rx_yellow_prio_1", .offset = 0x1d },
	{ .name = "rx_yellow_prio_2", .offset = 0x1e },
	{ .name = "rx_yellow_prio_3", .offset = 0x1f },
	{ .name = "rx_yellow_prio_4", .offset = 0x20 },
	{ .name = "rx_yellow_prio_5", .offset = 0x21 },
	{ .name = "rx_yellow_prio_6", .offset = 0x22 },
	{ .name = "rx_yellow_prio_7", .offset = 0x23 },
	{ .name = "rx_green_prio_0", .offset = 0x24 },
	{ .name = "rx_green_prio_1", .offset = 0x25 },
	{ .name = "rx_green_prio_2", .offset = 0x26 },
	{ .name = "rx_green_prio_3", .offset = 0x27 },
	{ .name = "rx_green_prio_4", .offset = 0x28 },
	{ .name = "rx_green_prio_5", .offset = 0x29 },
	{ .name = "rx_green_prio_6", .offset = 0x2a },
	{ .name = "rx_green_prio_7", .offset = 0x2b },
	{ .name = "rx_assembly_err", .offset = 0x2c },
	{ .name = "rx_smd_err", .offset = 0x2d },
	{ .name = "rx_assembly_ok", .offset = 0x2e },
	{ .name = "rx_merge_frag", .offset = 0x2f },
	{ .name = "rx_pmac_octets", .offset = 0x30, },
	{ .name = "rx_pmac_unicast", .offset = 0x31, },
	{ .name = "rx_pmac_multicast", .offset = 0x32 },
	{ .name = "rx_pmac_broadcast", .offset = 0x33 },
	{ .name = "rx_pmac_short", .offset = 0x34 },
	{ .name = "rx_pmac_frag", .offset = 0x35 },
	{ .name = "rx_pmac_jabber", .offset = 0x36 },
	{ .name = "rx_pmac_crc", .offset = 0x37 },
	{ .name = "rx_pmac_symbol_err", .offset = 0x38 },
	{ .name = "rx_pmac_sz_64", .offset = 0x39 },
	{ .name = "rx_pmac_sz_65_127", .offset = 0x3a },
	{ .name = "rx_pmac_sz_128_255", .offset = 0x3b },
	{ .name = "rx_pmac_sz_256_511", .offset = 0x3c },
	{ .name = "rx_pmac_sz_512_1023", .offset = 0x3d },
	{ .name = "rx_pmac_sz_1024_1526", .offset = 0x3e },
	{ .name = "rx_pmac_sz_jumbo", .offset = 0x3f },
	{ .name = "rx_pmac_pause", .offset = 0x40 },
	{ .name = "rx_pmac_control", .offset = 0x41 },
	{ .name = "rx_pmac_long", .offset = 0x42 },

	{ .name = "tx_octets", .offset = 0x80, },
	{ .name = "tx_unicast", .offset = 0x81, },
	{ .name = "tx_multicast", .offset = 0x82 },
	{ .name = "tx_broadcast", .offset = 0x83 },
	{ .name = "tx_col", .offset = 0x84 },
	{ .name = "tx_drop", .offset = 0x85 },
	{ .name = "tx_pause", .offset = 0x86 },
	{ .name = "tx_sz_64", .offset = 0x87 },
	{ .name = "tx_sz_65_127", .offset = 0x88 },
	{ .name = "tx_sz_128_255", .offset = 0x89 },
	{ .name = "tx_sz_256_511", .offset = 0x8a },
	{ .name = "tx_sz_512_1023", .offset = 0x8b },
	{ .name = "tx_sz_1024_1526", .offset = 0x8c },
	{ .name = "tx_sz_jumbo", .offset = 0x8d },
	{ .name = "tx_yellow_prio_0", .offset = 0x8e },
	{ .name = "tx_yellow_prio_1", .offset = 0x8f },
	{ .name = "tx_yellow_prio_2", .offset = 0x90 },
	{ .name = "tx_yellow_prio_3", .offset = 0x91 },
	{ .name = "tx_yellow_prio_4", .offset = 0x92 },
	{ .name = "tx_yellow_prio_5", .offset = 0x93 },
	{ .name = "tx_yellow_prio_6", .offset = 0x94 },
	{ .name = "tx_yellow_prio_7", .offset = 0x95 },
	{ .name = "tx_green_prio_0", .offset = 0x96 },
	{ .name = "tx_green_prio_1", .offset = 0x97 },
	{ .name = "tx_green_prio_2", .offset = 0x98 },
	{ .name = "tx_green_prio_3", .offset = 0x99 },
	{ .name = "tx_green_prio_4", .offset = 0x9a },
	{ .name = "tx_green_prio_5", .offset = 0x9b },
	{ .name = "tx_green_prio_6", .offset = 0x9c },
	{ .name = "tx_green_prio_7", .offset = 0x9d },
	{ .name = "tx_aged", .offset = 0x9e },
	{ .name = "tx_llct", .offset = 0x9f },
	{ .name = "tx_ct", .offset = 0xa0 },
	{ .name = "tx_mm_hold", .offset = 0xa1 },
	{ .name = "tx_merge_frag", .offset = 0xa2 },
	{ .name = "tx_pmac_octets", .offset = 0xa3, },
	{ .name = "tx_pmac_unicast", .offset = 0xa4, },
	{ .name = "tx_pmac_multicast", .offset = 0xa5 },
	{ .name = "tx_pmac_broadcast", .offset = 0xa6 },
	{ .name = "tx_pmac_pause", .offset = 0xa7 },
	{ .name = "tx_pmac_sz_64", .offset = 0xa8 },
	{ .name = "tx_pmac_sz_65_127", .offset = 0xa9 },
	{ .name = "tx_pmac_sz_128_255", .offset = 0xaa },
	{ .name = "tx_pmac_sz_256_511", .offset = 0xab },
	{ .name = "tx_pmac_sz_512_1023", .offset = 0xac },
	{ .name = "tx_pmac_sz_1024_1526", .offset = 0xad },
	{ .name = "tx_pmac_sz_jumbo", .offset = 0xae },

	{ .name = "dr_local", .offset = 0x100 },
	{ .name = "dr_tail", .offset = 0x101 },
	{ .name = "dr_yellow_prio_0", .offset = 0x102 },
	{ .name = "dr_yellow_prio_1", .offset = 0x103 },
	{ .name = "dr_yellow_prio_2", .offset = 0x104 },
	{ .name = "dr_yellow_prio_3", .offset = 0x105 },
	{ .name = "dr_yellow_prio_4", .offset = 0x106 },
	{ .name = "dr_yellow_prio_5", .offset = 0x107 },
	{ .name = "dr_yellow_prio_6", .offset = 0x108 },
	{ .name = "dr_yellow_prio_7", .offset = 0x109 },
	{ .name = "dr_green_prio_0", .offset = 0x10a },
	{ .name = "dr_green_prio_1", .offset = 0x10b },
	{ .name = "dr_green_prio_2", .offset = 0x10c },
	{ .name = "dr_green_prio_3", .offset = 0x10d },
	{ .name = "dr_green_prio_4", .offset = 0x10e },
	{ .name = "dr_green_prio_5", .offset = 0x10f },
	{ .name = "dr_green_prio_6", .offset = 0x110 },
	{ .name = "dr_green_prio_7", .offset = 0x111 },
};

/* The following numbers are indexes into lan966x_stats_layout[] */
#define SYS_COUNT_RX_OCT		  0
#define SYS_COUNT_RX_UC			  1
#define SYS_COUNT_RX_MC			  2
#define SYS_COUNT_RX_BC			  3
#define SYS_COUNT_RX_SHORT		  4
#define SYS_COUNT_RX_FRAG		  5
#define SYS_COUNT_RX_JABBER		  6
#define SYS_COUNT_RX_CRC		  7
#define SYS_COUNT_RX_SYMBOL_ERR		  8
#define SYS_COUNT_RX_SZ_64		  9
#define SYS_COUNT_RX_SZ_65_127		 10
#define SYS_COUNT_RX_SZ_128_255		 11
#define SYS_COUNT_RX_SZ_256_511		 12
#define SYS_COUNT_RX_SZ_512_1023	 13
#define SYS_COUNT_RX_SZ_1024_1526	 14
#define SYS_COUNT_RX_SZ_JUMBO		 15
#define SYS_COUNT_RX_PAUSE		 16
#define SYS_COUNT_RX_CONTROL		 17
#define SYS_COUNT_RX_LONG		 18
#define SYS_COUNT_RX_CAT_DROP		 19
#define SYS_COUNT_RX_RED_PRIO_0		 20
#define SYS_COUNT_RX_RED_PRIO_1		 21
#define SYS_COUNT_RX_RED_PRIO_2		 22
#define SYS_COUNT_RX_RED_PRIO_3		 23
#define SYS_COUNT_RX_RED_PRIO_4		 24
#define SYS_COUNT_RX_RED_PRIO_5		 25
#define SYS_COUNT_RX_RED_PRIO_6		 26
#define SYS_COUNT_RX_RED_PRIO_7		 27
#define SYS_COUNT_RX_YELLOW_PRIO_0	 28
#define SYS_COUNT_RX_YELLOW_PRIO_1	 29
#define SYS_COUNT_RX_YELLOW_PRIO_2	 30
#define SYS_COUNT_RX_YELLOW_PRIO_3	 31
#define SYS_COUNT_RX_YELLOW_PRIO_4	 32
#define SYS_COUNT_RX_YELLOW_PRIO_5	 33
#define SYS_COUNT_RX_YELLOW_PRIO_6	 34
#define SYS_COUNT_RX_YELLOW_PRIO_7	 35
#define SYS_COUNT_RX_GREEN_PRIO_0	 36
#define SYS_COUNT_RX_GREEN_PRIO_1	 37
#define SYS_COUNT_RX_GREEN_PRIO_2	 38
#define SYS_COUNT_RX_GREEN_PRIO_3	 39
#define SYS_COUNT_RX_GREEN_PRIO_4	 40
#define SYS_COUNT_RX_GREEN_PRIO_5	 41
#define SYS_COUNT_RX_GREEN_PRIO_6	 42
#define SYS_COUNT_RX_GREEN_PRIO_7	 43
#define SYS_COUNT_RX_ASSEMBLY_ERR	 44
#define SYS_COUNT_RX_SMD_ERR		 45
#define SYS_COUNT_RX_ASSEMBLY_OK	 46
#define SYS_COUNT_RX_MERGE_FRAG		 47
#define SYS_COUNT_RX_PMAC_OCT		 48
#define SYS_COUNT_RX_PMAC_UC		 49
#define SYS_COUNT_RX_PMAC_MC		 50
#define SYS_COUNT_RX_PMAC_BC		 51
#define SYS_COUNT_RX_PMAC_SHORT		 52
#define SYS_COUNT_RX_PMAC_FRAG		 53
#define SYS_COUNT_RX_PMAC_JABBER	 54
#define SYS_COUNT_RX_PMAC_CRC		 55
#define SYS_COUNT_RX_PMAC_SYMBOL_ERR	 56
#define SYS_COUNT_RX_PMAC_SZ_64		 57
#define SYS_COUNT_RX_PMAC_SZ_65_127	 58
#define SYS_COUNT_RX_PMAC_SZ_128_255	 59
#define SYS_COUNT_RX_PMAC_SZ_256_511	 60
#define SYS_COUNT_RX_PMAC_SZ_512_1023	 61
#define SYS_COUNT_RX_PMAC_SZ_1024_1526	 62
#define SYS_COUNT_RX_PMAC_SZ_JUMBO	 63
#define SYS_COUNT_RX_PMAC_PAUSE		 64
#define SYS_COUNT_RX_PMAC_CONTROL	 65
#define SYS_COUNT_RX_PMAC_LONG		 66

#define SYS_COUNT_TX_OCT		 67
#define SYS_COUNT_TX_UC			 68
#define SYS_COUNT_TX_MC			 69
#define SYS_COUNT_TX_BC			 70
#define SYS_COUNT_TX_COL		 71
#define SYS_COUNT_TX_DROP		 72
#define SYS_COUNT_TX_PAUSE		 73
#define SYS_COUNT_TX_SZ_64		 74
#define SYS_COUNT_TX_SZ_65_127		 75
#define SYS_COUNT_TX_SZ_128_255		 76
#define SYS_COUNT_TX_SZ_256_511		 77
#define SYS_COUNT_TX_SZ_512_1023	 78
#define SYS_COUNT_TX_SZ_1024_1526	 79
#define SYS_COUNT_TX_SZ_JUMBO		 80
#define SYS_COUNT_TX_YELLOW_PRIO_0	 81
#define SYS_COUNT_TX_YELLOW_PRIO_1	 82
#define SYS_COUNT_TX_YELLOW_PRIO_2	 83
#define SYS_COUNT_TX_YELLOW_PRIO_3	 84
#define SYS_COUNT_TX_YELLOW_PRIO_4	 85
#define SYS_COUNT_TX_YELLOW_PRIO_5	 86
#define SYS_COUNT_TX_YELLOW_PRIO_6	 87
#define SYS_COUNT_TX_YELLOW_PRIO_7	 88
#define SYS_COUNT_TX_GREEN_PRIO_0	 89
#define SYS_COUNT_TX_GREEN_PRIO_1	 90
#define SYS_COUNT_TX_GREEN_PRIO_2	 91
#define SYS_COUNT_TX_GREEN_PRIO_3	 92
#define SYS_COUNT_TX_GREEN_PRIO_4	 93
#define SYS_COUNT_TX_GREEN_PRIO_5	 94
#define SYS_COUNT_TX_GREEN_PRIO_6	 95
#define SYS_COUNT_TX_GREEN_PRIO_7	 96
#define SYS_COUNT_TX_AGED		 97
#define SYS_COUNT_TX_LLCT		 98
#define SYS_COUNT_TX_CT			 99
#define SYS_COUNT_TX_MM_HOLD		100
#define SYS_COUNT_TX_MERGE_FRAG		101
#define SYS_COUNT_TX_PMAC_OCT		102
#define SYS_COUNT_TX_PMAC_UC		103
#define SYS_COUNT_TX_PMAC_MC		104
#define SYS_COUNT_TX_PMAC_BC		105
#define SYS_COUNT_TX_PMAC_PAUSE		106
#define SYS_COUNT_TX_PMAC_SZ_64		107
#define SYS_COUNT_TX_PMAC_SZ_65_127	108
#define SYS_COUNT_TX_PMAC_SZ_128_255	109
#define SYS_COUNT_TX_PMAC_SZ_256_511	110
#define SYS_COUNT_TX_PMAC_SZ_512_1023	111
#define SYS_COUNT_TX_PMAC_SZ_1024_1526	112
#define SYS_COUNT_TX_PMAC_SZ_JUMBO	113

#define SYS_COUNT_DR_LOCAL		114
#define SYS_COUNT_DR_TAIL		115
#define SYS_COUNT_DR_YELLOW_PRIO_0	116
#define SYS_COUNT_DR_YELLOW_PRIO_1	117
#define SYS_COUNT_DR_YELLOW_PRIO_2	118
#define SYS_COUNT_DR_YELLOW_PRIO_3	119
#define SYS_COUNT_DR_YELLOW_PRIO_4	120
#define SYS_COUNT_DR_YELLOW_PRIO_5	121
#define SYS_COUNT_DR_YELLOW_PRIO_6	122
#define SYS_COUNT_DR_YELLOW_PRIO_7	123
#define SYS_COUNT_DR_GREEN_PRIO_0	124
#define SYS_COUNT_DR_GREEN_PRIO_1	125
#define SYS_COUNT_DR_GREEN_PRIO_2	126
#define SYS_COUNT_DR_GREEN_PRIO_3	127
#define SYS_COUNT_DR_GREEN_PRIO_4	128
#define SYS_COUNT_DR_GREEN_PRIO_5	129
#define SYS_COUNT_DR_GREEN_PRIO_6	130
#define SYS_COUNT_DR_GREEN_PRIO_7	131

/* Add a possibly wrapping 32 bit value to a 64 bit counter */
static void lan966x_add_cnt(u64 *cnt, u32 val)
{
	if (val < (*cnt & U32_MAX))
		*cnt += (u64)1 << 32; /* value has wrapped */

	*cnt = (*cnt & ~(u64)U32_MAX) + val;
}

static void lan966x_stats_update(struct lan966x *lan966x)
{
	int i, j;

	mutex_lock(&lan966x->stats_lock);

	for (i = 0; i < lan966x->num_phys_ports; i++) {
		uint idx = i * lan966x->num_stats;

		lan_wr(SYS_STAT_CFG_STAT_VIEW_SET(i),
		       lan966x, SYS_STAT_CFG);

		for (j = 0; j < lan966x->num_stats; j++) {
			u32 offset = lan966x->stats_layout[j].offset;

			lan966x_add_cnt(&lan966x->stats[idx++],
					lan_rd(lan966x, SYS_CNT(offset)));
		}
	}

	mutex_unlock(&lan966x->stats_lock);
}

static int lan966x_get_sset_count(struct net_device *dev, int sset)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	return lan966x->num_stats;
}

static void lan966x_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	struct lan966x_port *port = netdev_priv(netdev);
	struct lan966x *lan966x = port->lan966x;
	int i;

	if (sset != ETH_SS_STATS)
		return;

	for (i = 0; i < lan966x->num_stats; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       lan966x->stats_layout[i].name, ETH_GSTRING_LEN);
}

static void lan966x_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats, u64 *data)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	int i;

	/* check and update now */
	lan966x_stats_update(lan966x);

	/* Copy all counters */
	for (i = 0; i < lan966x->num_stats; i++)
		*data++ = lan966x->stats[port->chip_port *
					 lan966x->num_stats + i];
}

static void lan966x_get_eth_mac_stats(struct net_device *dev,
				      struct ethtool_eth_mac_stats *mac_stats)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	u32 idx;

	lan966x_stats_update(lan966x);

	idx = port->chip_port * lan966x->num_stats;

	mutex_lock(&lan966x->stats_lock);

	mac_stats->FramesTransmittedOK =
		lan966x->stats[idx + SYS_COUNT_TX_UC] +
		lan966x->stats[idx + SYS_COUNT_TX_MC] +
		lan966x->stats[idx + SYS_COUNT_TX_BC] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_UC] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_MC] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_BC];
	mac_stats->SingleCollisionFrames =
		lan966x->stats[idx + SYS_COUNT_TX_COL];
	mac_stats->FramesReceivedOK =
		lan966x->stats[idx + SYS_COUNT_RX_UC] +
		lan966x->stats[idx + SYS_COUNT_RX_MC] +
		lan966x->stats[idx + SYS_COUNT_RX_BC];
	mac_stats->FrameCheckSequenceErrors =
		lan966x->stats[idx + SYS_COUNT_RX_CRC] +
		lan966x->stats[idx + SYS_COUNT_RX_CRC];
	mac_stats->OctetsTransmittedOK =
		lan966x->stats[idx + SYS_COUNT_TX_OCT] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_OCT];
	mac_stats->FramesWithDeferredXmissions =
		lan966x->stats[idx + SYS_COUNT_TX_MM_HOLD];
	mac_stats->OctetsReceivedOK =
		lan966x->stats[idx + SYS_COUNT_RX_OCT];
	mac_stats->MulticastFramesXmittedOK =
		lan966x->stats[idx + SYS_COUNT_TX_MC] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_MC];
	mac_stats->BroadcastFramesXmittedOK =
		lan966x->stats[idx + SYS_COUNT_TX_BC] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_BC];
	mac_stats->MulticastFramesReceivedOK =
		lan966x->stats[idx + SYS_COUNT_RX_MC];
	mac_stats->BroadcastFramesReceivedOK =
		lan966x->stats[idx + SYS_COUNT_RX_BC];
	mac_stats->InRangeLengthErrors =
		lan966x->stats[idx + SYS_COUNT_RX_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_CRC] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_CRC];
	mac_stats->OutOfRangeLengthField =
		lan966x->stats[idx + SYS_COUNT_RX_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_LONG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_LONG];
	mac_stats->FrameTooLongErrors =
		lan966x->stats[idx + SYS_COUNT_RX_LONG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_LONG];

	mutex_unlock(&lan966x->stats_lock);
}

static const struct ethtool_rmon_hist_range lan966x_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519, 10239 },
	{}
};

static void lan966x_get_eth_rmon_stats(struct net_device *dev,
				       struct ethtool_rmon_stats *rmon_stats,
				       const struct ethtool_rmon_hist_range **ranges)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	u32 idx;

	lan966x_stats_update(lan966x);

	idx = port->chip_port * lan966x->num_stats;

	mutex_lock(&lan966x->stats_lock);

	rmon_stats->undersize_pkts =
		lan966x->stats[idx + SYS_COUNT_RX_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SHORT];
	rmon_stats->oversize_pkts =
		lan966x->stats[idx + SYS_COUNT_RX_LONG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_LONG];
	rmon_stats->fragments =
		lan966x->stats[idx + SYS_COUNT_RX_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_FRAG];
	rmon_stats->jabbers =
		lan966x->stats[idx + SYS_COUNT_RX_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_JABBER];
	rmon_stats->hist[0] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_64];
	rmon_stats->hist[1] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_65_127];
	rmon_stats->hist[2] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_128_255];
	rmon_stats->hist[3] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_256_511];
	rmon_stats->hist[4] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_512_1023];
	rmon_stats->hist[5] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_1024_1526];
	rmon_stats->hist[6] =
		lan966x->stats[idx + SYS_COUNT_RX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_1024_1526];

	rmon_stats->hist_tx[0] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_64];
	rmon_stats->hist_tx[1] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_65_127];
	rmon_stats->hist_tx[2] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_128_255];
	rmon_stats->hist_tx[3] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_256_511];
	rmon_stats->hist_tx[4] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_512_1023];
	rmon_stats->hist_tx[5] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_1024_1526];
	rmon_stats->hist_tx[6] =
		lan966x->stats[idx + SYS_COUNT_TX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_1024_1526];

	mutex_unlock(&lan966x->stats_lock);

	*ranges = lan966x_rmon_ranges;
}

static int lan966x_get_link_ksettings(struct net_device *ndev,
				      struct ethtool_link_ksettings *cmd)
{
	struct lan966x_port *port = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(port->phylink, cmd);
}

static int lan966x_set_link_ksettings(struct net_device *ndev,
				      const struct ethtool_link_ksettings *cmd)
{
	struct lan966x_port *port = netdev_priv(ndev);

	return phylink_ethtool_ksettings_set(port->phylink, cmd);
}

static void lan966x_get_pauseparam(struct net_device *dev,
				   struct ethtool_pauseparam *pause)
{
	struct lan966x_port *port = netdev_priv(dev);

	phylink_ethtool_get_pauseparam(port->phylink, pause);
}

static int lan966x_set_pauseparam(struct net_device *dev,
				  struct ethtool_pauseparam *pause)
{
	struct lan966x_port *port = netdev_priv(dev);

	return phylink_ethtool_set_pauseparam(port->phylink, pause);
}

static int lan966x_get_ts_info(struct net_device *dev,
			       struct kernel_ethtool_ts_info *info)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_phc *phc;

	if (!lan966x->ptp)
		return ethtool_op_get_ts_info(dev, info);

	phc = &lan966x->phc[LAN966X_PHC_PORT];

	if (phc->clock) {
		info->phc_index = ptp_clock_index(phc->clock);
	} else {
		info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE;
		return 0;
	}
	info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE |
				 SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON) |
			 BIT(HWTSTAMP_TX_ONESTEP_SYNC);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

const struct ethtool_ops lan966x_ethtool_ops = {
	.get_link_ksettings     = lan966x_get_link_ksettings,
	.set_link_ksettings     = lan966x_set_link_ksettings,
	.get_pauseparam		= lan966x_get_pauseparam,
	.set_pauseparam		= lan966x_set_pauseparam,
	.get_sset_count		= lan966x_get_sset_count,
	.get_strings		= lan966x_get_strings,
	.get_ethtool_stats	= lan966x_get_ethtool_stats,
	.get_eth_mac_stats      = lan966x_get_eth_mac_stats,
	.get_rmon_stats		= lan966x_get_eth_rmon_stats,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= lan966x_get_ts_info,
};

static void lan966x_check_stats_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct lan966x *lan966x = container_of(del_work, struct lan966x,
					       stats_work);

	lan966x_stats_update(lan966x);

	queue_delayed_work(lan966x->stats_queue, &lan966x->stats_work,
			   LAN966X_STATS_CHECK_DELAY);
}

void lan966x_stats_get(struct net_device *dev,
		       struct rtnl_link_stats64 *stats)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	u32 idx;
	int i;

	idx = port->chip_port * lan966x->num_stats;

	mutex_lock(&lan966x->stats_lock);

	stats->rx_bytes = lan966x->stats[idx + SYS_COUNT_RX_OCT] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_OCT];

	stats->rx_packets = lan966x->stats[idx + SYS_COUNT_RX_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_CRC] +
		lan966x->stats[idx + SYS_COUNT_RX_SYMBOL_ERR] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_RX_SZ_JUMBO] +
		lan966x->stats[idx + SYS_COUNT_RX_LONG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_SZ_JUMBO];

	stats->multicast = lan966x->stats[idx + SYS_COUNT_RX_MC] +
		lan966x->stats[idx + SYS_COUNT_RX_PMAC_MC];

	stats->rx_errors = lan966x->stats[idx + SYS_COUNT_RX_SHORT] +
		lan966x->stats[idx + SYS_COUNT_RX_FRAG] +
		lan966x->stats[idx + SYS_COUNT_RX_JABBER] +
		lan966x->stats[idx + SYS_COUNT_RX_CRC] +
		lan966x->stats[idx + SYS_COUNT_RX_SYMBOL_ERR] +
		lan966x->stats[idx + SYS_COUNT_RX_LONG];

	stats->rx_dropped = dev->stats.rx_dropped +
		lan966x->stats[idx + SYS_COUNT_RX_LONG] +
		lan966x->stats[idx + SYS_COUNT_DR_LOCAL] +
		lan966x->stats[idx + SYS_COUNT_DR_TAIL] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_0] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_1] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_2] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_3] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_4] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_5] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_6] +
		lan966x->stats[idx + SYS_COUNT_RX_RED_PRIO_7];

	for (i = 0; i < LAN966X_NUM_TC; i++) {
		stats->rx_dropped +=
			(lan966x->stats[idx + SYS_COUNT_DR_YELLOW_PRIO_0 + i] +
			 lan966x->stats[idx + SYS_COUNT_DR_GREEN_PRIO_0 + i]);
	}

	/* Get Tx stats */
	stats->tx_bytes = lan966x->stats[idx + SYS_COUNT_TX_OCT] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_OCT];

	stats->tx_packets = lan966x->stats[idx + SYS_COUNT_TX_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_TX_SZ_JUMBO] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_64] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_65_127] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_128_255] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_256_511] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_512_1023] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_1024_1526] +
		lan966x->stats[idx + SYS_COUNT_TX_PMAC_SZ_JUMBO];

	stats->tx_dropped = lan966x->stats[idx + SYS_COUNT_TX_DROP] +
		lan966x->stats[idx + SYS_COUNT_TX_AGED];

	stats->collisions = lan966x->stats[idx + SYS_COUNT_TX_COL];

	mutex_unlock(&lan966x->stats_lock);
}

int lan966x_stats_init(struct lan966x *lan966x)
{
	char queue_name[32];

	lan966x->stats_layout = lan966x_stats_layout;
	lan966x->num_stats = ARRAY_SIZE(lan966x_stats_layout);
	lan966x->stats = devm_kcalloc(lan966x->dev, lan966x->num_phys_ports *
				      lan966x->num_stats,
				      sizeof(u64), GFP_KERNEL);
	if (!lan966x->stats)
		return -ENOMEM;

	/* Init stats worker */
	mutex_init(&lan966x->stats_lock);
	snprintf(queue_name, sizeof(queue_name), "%s-stats",
		 dev_name(lan966x->dev));
	lan966x->stats_queue = create_singlethread_workqueue(queue_name);
	if (!lan966x->stats_queue)
		return -ENOMEM;

	INIT_DELAYED_WORK(&lan966x->stats_work, lan966x_check_stats_work);
	queue_delayed_work(lan966x->stats_queue, &lan966x->stats_work,
			   LAN966X_STATS_CHECK_DELAY);

	return 0;
}
