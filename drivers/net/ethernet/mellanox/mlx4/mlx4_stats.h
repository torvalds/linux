/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MLX4_STATS_
#define _MLX4_STATS_

#ifdef MLX4_EN_PERF_STAT
#define NUM_PERF_STATS			NUM_PERF_COUNTERS
#else
#define NUM_PERF_STATS			0
#endif

#define NUM_PRIORITIES	9
#define NUM_PRIORITY_STATS 2

struct mlx4_en_pkt_stats {
	unsigned long rx_multicast_packets;
	unsigned long rx_broadcast_packets;
	unsigned long rx_jabbers;
	unsigned long rx_in_range_length_error;
	unsigned long rx_out_range_length_error;
	unsigned long tx_multicast_packets;
	unsigned long tx_broadcast_packets;
	unsigned long rx_prio[NUM_PRIORITIES][NUM_PRIORITY_STATS];
	unsigned long tx_prio[NUM_PRIORITIES][NUM_PRIORITY_STATS];
#define NUM_PKT_STATS		43
};

struct mlx4_en_counter_stats {
	unsigned long rx_packets;
	unsigned long rx_bytes;
	unsigned long tx_packets;
	unsigned long tx_bytes;
#define NUM_PF_STATS      4
};

struct mlx4_en_port_stats {
	unsigned long tso_packets;
	unsigned long xmit_more;
	unsigned long queue_stopped;
	unsigned long wake_queue;
	unsigned long tx_timeout;
	unsigned long rx_alloc_pages;
	unsigned long rx_chksum_good;
	unsigned long rx_chksum_none;
	unsigned long rx_chksum_complete;
	unsigned long tx_chksum_offload;
#define NUM_PORT_STATS		10
};

struct mlx4_en_perf_stats {
	u32 tx_poll;
	u64 tx_pktsz_avg;
	u32 inflight_avg;
	u16 tx_coal_avg;
	u16 rx_coal_avg;
	u32 napi_quota;
#define NUM_PERF_COUNTERS		6
};

struct mlx4_en_xdp_stats {
	unsigned long rx_xdp_drop;
	unsigned long rx_xdp_tx;
	unsigned long rx_xdp_tx_full;
#define NUM_XDP_STATS		3
};

struct mlx4_en_phy_stats {
	unsigned long rx_packets_phy;
	unsigned long rx_bytes_phy;
	unsigned long tx_packets_phy;
	unsigned long tx_bytes_phy;
#define NUM_PHY_STATS		4
};

#define NUM_MAIN_STATS	21

#define MLX4_NUM_PRIORITIES	8

struct mlx4_en_flow_stats_rx {
	u64 rx_pause;
	u64 rx_pause_duration;
	u64 rx_pause_transition;
#define NUM_FLOW_STATS_RX	3
#define NUM_FLOW_PRIORITY_STATS_RX	(NUM_FLOW_STATS_RX * \
					 MLX4_NUM_PRIORITIES)
};

#define FLOW_PRIORITY_STATS_IDX_RX_FRAMES	(NUM_MAIN_STATS +	\
						 NUM_PORT_STATS +	\
						 NUM_PF_STATS +		\
						 NUM_FLOW_PRIORITY_STATS_RX)

struct mlx4_en_flow_stats_tx {
	u64 tx_pause;
	u64 tx_pause_duration;
	u64 tx_pause_transition;
#define NUM_FLOW_STATS_TX	3
#define NUM_FLOW_PRIORITY_STATS_TX	(NUM_FLOW_STATS_TX * \
					 MLX4_NUM_PRIORITIES)
};

#define FLOW_PRIORITY_STATS_IDX_TX_FRAMES	(NUM_MAIN_STATS +	\
						 NUM_PORT_STATS +	\
						 NUM_PF_STATS +		\
						 NUM_FLOW_PRIORITY_STATS_RX + \
						 NUM_FLOW_STATS_RX +	\
						 NUM_FLOW_PRIORITY_STATS_TX)

#define NUM_FLOW_STATS (NUM_FLOW_STATS_RX + NUM_FLOW_STATS_TX + \
			NUM_FLOW_PRIORITY_STATS_TX + \
			NUM_FLOW_PRIORITY_STATS_RX)

struct mlx4_en_stat_out_flow_control_mbox {
	/* Total number of PAUSE frames received from the far-end port */
	__be64 rx_pause;
	/* Total number of microseconds that far-end port requested to pause
	* transmission of packets
	*/
	__be64 rx_pause_duration;
	/* Number of received transmission from XOFF state to XON state */
	__be64 rx_pause_transition;
	/* Total number of PAUSE frames sent from the far-end port */
	__be64 tx_pause;
	/* Total time in microseconds that transmission of packets has been
	* paused
	*/
	__be64 tx_pause_duration;
	/* Number of transmitter transitions from XOFF state to XON state */
	__be64 tx_pause_transition;
	/* Reserverd */
	__be64 reserved[2];
};

enum {
	MLX4_DUMP_ETH_STATS_FLOW_CONTROL = 1 << 12
};

#define NUM_ALL_STATS	(NUM_MAIN_STATS + NUM_PORT_STATS + NUM_PKT_STATS + \
			 NUM_FLOW_STATS + NUM_PERF_STATS + NUM_PF_STATS + \
			 NUM_XDP_STATS + NUM_PHY_STATS)

#define MLX4_FIND_NETDEV_STAT(n) (offsetof(struct net_device_stats, n) / \
				  sizeof(((struct net_device_stats *)0)->n))

#endif
