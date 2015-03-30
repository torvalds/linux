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
	unsigned long broadcast;
	unsigned long rx_prio[8];
	unsigned long tx_prio[8];
#define NUM_PKT_STATS                17
};

struct mlx4_en_port_stats {
	unsigned long tso_packets;
	unsigned long xmit_more;
	unsigned long queue_stopped;
	unsigned long wake_queue;
	unsigned long tx_timeout;
	unsigned long rx_alloc_failed;
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

#define NUM_MAIN_STATS	21
#define NUM_ALL_STATS	(NUM_MAIN_STATS + NUM_PORT_STATS + NUM_PKT_STATS + \
			 NUM_PERF_STATS)

#define MLX4_FIND_NETDEV_STAT(n) (offsetof(struct net_device_stats, n) / \
				  sizeof(((struct net_device_stats *)0)->n))

#endif
