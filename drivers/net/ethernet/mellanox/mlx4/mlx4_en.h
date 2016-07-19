/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _MLX4_EN_H_
#define _MLX4_EN_H_

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/net_tstamp.h>
#ifdef CONFIG_MLX4_EN_DCB
#include <linux/dcbnl.h>
#endif
#include <linux/cpu_rmap.h>
#include <linux/ptp_clock_kernel.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/qp.h>
#include <linux/mlx4/cq.h>
#include <linux/mlx4/srq.h>
#include <linux/mlx4/doorbell.h>
#include <linux/mlx4/cmd.h>

#include "en_port.h"
#include "mlx4_stats.h"

#define DRV_NAME	"mlx4_en"
#define DRV_VERSION	"2.2-1"
#define DRV_RELDATE	"Feb 2014"

#define MLX4_EN_MSG_LEVEL	(NETIF_MSG_LINK | NETIF_MSG_IFDOWN)

/*
 * Device constants
 */


#define MLX4_EN_PAGE_SHIFT	12
#define MLX4_EN_PAGE_SIZE	(1 << MLX4_EN_PAGE_SHIFT)
#define DEF_RX_RINGS		16
#define MAX_RX_RINGS		128
#define MIN_RX_RINGS		4
#define TXBB_SIZE		64
#define HEADROOM		(2048 / TXBB_SIZE + 1)
#define STAMP_STRIDE		64
#define STAMP_DWORDS		(STAMP_STRIDE / 4)
#define STAMP_SHIFT		31
#define STAMP_VAL		0x7fffffff
#define STATS_DELAY		(HZ / 4)
#define SERVICE_TASK_DELAY	(HZ / 4)
#define MAX_NUM_OF_FS_RULES	256

#define MLX4_EN_FILTER_HASH_SHIFT 4
#define MLX4_EN_FILTER_EXPIRY_QUOTA 60

/* Typical TSO descriptor with 16 gather entries is 352 bytes... */
#define MAX_DESC_SIZE		512
#define MAX_DESC_TXBBS		(MAX_DESC_SIZE / TXBB_SIZE)

/*
 * OS related constants and tunables
 */

#define MLX4_EN_PRIV_FLAGS_BLUEFLAME 1
#define MLX4_EN_PRIV_FLAGS_PHV	     2

#define MLX4_EN_WATCHDOG_TIMEOUT	(15 * HZ)

/* Use the maximum between 16384 and a single page */
#define MLX4_EN_ALLOC_SIZE	PAGE_ALIGN(16384)

#define MLX4_EN_ALLOC_PREFER_ORDER	PAGE_ALLOC_COSTLY_ORDER

/* Receive fragment sizes; we use at most 3 fragments (for 9600 byte MTU
 * and 4K allocations) */
enum {
	FRAG_SZ0 = 1536 - NET_IP_ALIGN,
	FRAG_SZ1 = 4096,
	FRAG_SZ2 = 4096,
	FRAG_SZ3 = MLX4_EN_ALLOC_SIZE
};
#define MLX4_EN_MAX_RX_FRAGS	4

/* Maximum ring sizes */
#define MLX4_EN_MAX_TX_SIZE	8192
#define MLX4_EN_MAX_RX_SIZE	8192

/* Minimum ring size for our page-allocation scheme to work */
#define MLX4_EN_MIN_RX_SIZE	(MLX4_EN_ALLOC_SIZE / SMP_CACHE_BYTES)
#define MLX4_EN_MIN_TX_SIZE	(4096 / TXBB_SIZE)

#define MLX4_EN_SMALL_PKT_SIZE		64
#define MLX4_EN_MIN_TX_RING_P_UP	1
#define MLX4_EN_MAX_TX_RING_P_UP	32
#define MLX4_EN_NUM_UP			8
#define MLX4_EN_DEF_TX_RING_SIZE	512
#define MLX4_EN_DEF_RX_RING_SIZE  	1024
#define MAX_TX_RINGS			(MLX4_EN_MAX_TX_RING_P_UP * \
					 MLX4_EN_NUM_UP)

#define MLX4_EN_DEFAULT_TX_WORK		256

/* Target number of packets to coalesce with interrupt moderation */
#define MLX4_EN_RX_COAL_TARGET	44
#define MLX4_EN_RX_COAL_TIME	0x10

#define MLX4_EN_TX_COAL_PKTS	16
#define MLX4_EN_TX_COAL_TIME	0x10

#define MLX4_EN_RX_RATE_LOW		400000
#define MLX4_EN_RX_COAL_TIME_LOW	0
#define MLX4_EN_RX_RATE_HIGH		450000
#define MLX4_EN_RX_COAL_TIME_HIGH	128
#define MLX4_EN_RX_SIZE_THRESH		1024
#define MLX4_EN_RX_RATE_THRESH		(1000000 / MLX4_EN_RX_COAL_TIME_HIGH)
#define MLX4_EN_SAMPLE_INTERVAL		0
#define MLX4_EN_AVG_PKT_SMALL		256

#define MLX4_EN_AUTO_CONF	0xffff

#define MLX4_EN_DEF_RX_PAUSE	1
#define MLX4_EN_DEF_TX_PAUSE	1

/* Interval between successive polls in the Tx routine when polling is used
   instead of interrupts (in per-core Tx rings) - should be power of 2 */
#define MLX4_EN_TX_POLL_MODER	16
#define MLX4_EN_TX_POLL_TIMEOUT	(HZ / 4)

#define SMALL_PACKET_SIZE      (256 - NET_IP_ALIGN)
#define HEADER_COPY_SIZE       (128 - NET_IP_ALIGN)
#define MLX4_LOOPBACK_TEST_PAYLOAD (HEADER_COPY_SIZE - ETH_HLEN)

#define MLX4_EN_MIN_MTU		46
/* VLAN_HLEN is added twice,to support skb vlan tagged with multiple
 * headers. (For example: ETH_P_8021Q and ETH_P_8021AD).
 */
#define MLX4_EN_EFF_MTU(mtu)	((mtu) + ETH_HLEN + (2 * VLAN_HLEN))
#define ETH_BCAST		0xffffffffffffULL

#define MLX4_EN_LOOPBACK_RETRIES	5
#define MLX4_EN_LOOPBACK_TIMEOUT	100

#ifdef MLX4_EN_PERF_STAT
/* Number of samples to 'average' */
#define AVG_SIZE			128
#define AVG_FACTOR			1024

#define INC_PERF_COUNTER(cnt)		(++(cnt))
#define ADD_PERF_COUNTER(cnt, add)	((cnt) += (add))
#define AVG_PERF_COUNTER(cnt, sample) \
	((cnt) = ((cnt) * (AVG_SIZE - 1) + (sample) * AVG_FACTOR) / AVG_SIZE)
#define GET_PERF_COUNTER(cnt)		(cnt)
#define GET_AVG_PERF_COUNTER(cnt)	((cnt) / AVG_FACTOR)

#else

#define INC_PERF_COUNTER(cnt)		do {} while (0)
#define ADD_PERF_COUNTER(cnt, add)	do {} while (0)
#define AVG_PERF_COUNTER(cnt, sample)	do {} while (0)
#define GET_PERF_COUNTER(cnt)		(0)
#define GET_AVG_PERF_COUNTER(cnt)	(0)
#endif /* MLX4_EN_PERF_STAT */

/* Constants for TX flow */
enum {
	MAX_INLINE = 104, /* 128 - 16 - 4 - 4 */
	MAX_BF = 256,
	MIN_PKT_LEN = 17,
};

/*
 * Configurables
 */

enum cq_type {
	RX = 0,
	TX = 1,
};


/*
 * Useful macros
 */
#define ROUNDUP_LOG2(x)		ilog2(roundup_pow_of_two(x))
#define XNOR(x, y)		(!(x) == !(y))


struct mlx4_en_tx_info {
	struct sk_buff *skb;
	dma_addr_t	map0_dma;
	u32		map0_byte_count;
	u32		nr_txbb;
	u32		nr_bytes;
	u8		linear;
	u8		data_offset;
	u8		inl;
	u8		ts_requested;
	u8		nr_maps;
} ____cacheline_aligned_in_smp;


#define MLX4_EN_BIT_DESC_OWN	0x80000000
#define CTRL_SIZE	sizeof(struct mlx4_wqe_ctrl_seg)
#define MLX4_EN_MEMTYPE_PAD	0x100
#define DS_SIZE		sizeof(struct mlx4_wqe_data_seg)


struct mlx4_en_tx_desc {
	struct mlx4_wqe_ctrl_seg ctrl;
	union {
		struct mlx4_wqe_data_seg data; /* at least one data segment */
		struct mlx4_wqe_lso_seg lso;
		struct mlx4_wqe_inline_seg inl;
	};
};

#define MLX4_EN_USE_SRQ		0x01000000

#define MLX4_EN_CX3_LOW_ID	0x1000
#define MLX4_EN_CX3_HIGH_ID	0x1005

struct mlx4_en_rx_alloc {
	struct page	*page;
	dma_addr_t	dma;
	u32		page_offset;
	u32		page_size;
};

#define MLX4_EN_CACHE_SIZE (2 * NAPI_POLL_WEIGHT)
struct mlx4_en_page_cache {
	u32 index;
	struct mlx4_en_rx_alloc buf[MLX4_EN_CACHE_SIZE];
};

struct mlx4_en_tx_ring {
	/* cache line used and dirtied in tx completion
	 * (mlx4_en_free_tx_buf())
	 */
	u32			last_nr_txbb;
	u32			cons;
	unsigned long		wake_queue;

	/* cache line used and dirtied in mlx4_en_xmit() */
	u32			prod ____cacheline_aligned_in_smp;
	unsigned long		bytes;
	unsigned long		packets;
	unsigned long		tx_csum;
	unsigned long		tso_packets;
	unsigned long		xmit_more;
	unsigned int		tx_dropped;
	struct mlx4_bf		bf;
	unsigned long		queue_stopped;

	/* Following part should be mostly read */
	cpumask_t		affinity_mask;
	struct mlx4_qp		qp;
	struct mlx4_hwq_resources wqres;
	u32			size; /* number of TXBBs */
	u32			size_mask;
	u16			stride;
	u32			full_size;
	u16			cqn;	/* index of port CQ associated with this ring */
	u32			buf_size;
	__be32			doorbell_qpn;
	__be32			mr_key;
	void			*buf;
	struct mlx4_en_tx_info	*tx_info;
	u8			*bounce_buf;
	struct mlx4_qp_context	context;
	int			qpn;
	enum mlx4_qp_state	qp_state;
	u8			queue_index;
	bool			bf_enabled;
	bool			bf_alloced;
	struct netdev_queue	*tx_queue;
	int			hwtstamp_tx_type;
} ____cacheline_aligned_in_smp;

struct mlx4_en_rx_desc {
	/* actual number of entries depends on rx ring stride */
	struct mlx4_wqe_data_seg data[0];
};

struct mlx4_en_rx_ring {
	struct mlx4_hwq_resources wqres;
	struct mlx4_en_rx_alloc page_alloc[MLX4_EN_MAX_RX_FRAGS];
	u32 size ;	/* number of Rx descs*/
	u32 actual_size;
	u32 size_mask;
	u16 stride;
	u16 log_stride;
	u16 cqn;	/* index of port CQ associated with this ring */
	u32 prod;
	u32 cons;
	u32 buf_size;
	u8  fcs_del;
	void *buf;
	void *rx_info;
	struct bpf_prog *xdp_prog;
	struct mlx4_en_page_cache page_cache;
	unsigned long bytes;
	unsigned long packets;
	unsigned long csum_ok;
	unsigned long csum_none;
	unsigned long csum_complete;
	unsigned long dropped;
	int hwtstamp_rx_filter;
	cpumask_var_t affinity_mask;
};

struct mlx4_en_cq {
	struct mlx4_cq          mcq;
	struct mlx4_hwq_resources wqres;
	int                     ring;
	struct net_device      *dev;
	struct napi_struct	napi;
	int size;
	int buf_size;
	int vector;
	enum cq_type is_tx;
	u16 moder_time;
	u16 moder_cnt;
	struct mlx4_cqe *buf;
#define MLX4_EN_OPCODE_ERROR	0x1e

	struct irq_desc *irq_desc;
};

struct mlx4_en_port_profile {
	u32 flags;
	u32 tx_ring_num;
	u32 rx_ring_num;
	u32 tx_ring_size;
	u32 rx_ring_size;
	u8 rx_pause;
	u8 rx_ppp;
	u8 tx_pause;
	u8 tx_ppp;
	int rss_rings;
	int inline_thold;
};

struct mlx4_en_profile {
	int udp_rss;
	u8 rss_mask;
	u32 active_ports;
	u32 small_pkt_int;
	u8 no_reset;
	u8 num_tx_rings_p_up;
	struct mlx4_en_port_profile prof[MLX4_MAX_PORTS + 1];
};

struct mlx4_en_dev {
	struct mlx4_dev         *dev;
	struct pci_dev		*pdev;
	struct mutex		state_lock;
	struct net_device       *pndev[MLX4_MAX_PORTS + 1];
	struct net_device       *upper[MLX4_MAX_PORTS + 1];
	u32                     port_cnt;
	bool			device_up;
	struct mlx4_en_profile  profile;
	u32			LSO_support;
	struct workqueue_struct *workqueue;
	struct device           *dma_device;
	void __iomem            *uar_map;
	struct mlx4_uar         priv_uar;
	struct mlx4_mr		mr;
	u32                     priv_pdn;
	spinlock_t              uar_lock;
	u8			mac_removed[MLX4_MAX_PORTS + 1];
	rwlock_t		clock_lock;
	u32			nominal_c_mult;
	struct cyclecounter	cycles;
	struct timecounter	clock;
	unsigned long		last_overflow_check;
	unsigned long		overflow_period;
	struct ptp_clock	*ptp_clock;
	struct ptp_clock_info	ptp_clock_info;
	struct notifier_block	nb;
};


struct mlx4_en_rss_map {
	int base_qpn;
	struct mlx4_qp qps[MAX_RX_RINGS];
	enum mlx4_qp_state state[MAX_RX_RINGS];
	struct mlx4_qp indir_qp;
	enum mlx4_qp_state indir_state;
};

enum mlx4_en_port_flag {
	MLX4_EN_PORT_ANC = 1<<0, /* Auto-negotiation complete */
	MLX4_EN_PORT_ANE = 1<<1, /* Auto-negotiation enabled */
};

struct mlx4_en_port_state {
	int link_state;
	int link_speed;
	int transceiver;
	u32 flags;
};

enum mlx4_en_mclist_act {
	MCLIST_NONE,
	MCLIST_REM,
	MCLIST_ADD,
};

struct mlx4_en_mc_list {
	struct list_head	list;
	enum mlx4_en_mclist_act	action;
	u8			addr[ETH_ALEN];
	u64			reg_id;
	u64			tunnel_reg_id;
};

struct mlx4_en_frag_info {
	u16 frag_size;
	u16 frag_prefix_size;
	u32 frag_stride;
	enum dma_data_direction dma_dir;
	int order;
};

#ifdef CONFIG_MLX4_EN_DCB
/* Minimal TC BW - setting to 0 will block traffic */
#define MLX4_EN_BW_MIN 1
#define MLX4_EN_BW_MAX 100 /* Utilize 100% of the line */

#define MLX4_EN_TC_ETS 7

enum dcb_pfc_type {
	pfc_disabled = 0,
	pfc_enabled_full,
	pfc_enabled_tx,
	pfc_enabled_rx
};

struct tc_configuration {
	enum dcb_pfc_type  dcb_pfc;
};

struct mlx4_en_cee_config {
	bool	pfc_state;
	struct	tc_configuration tc_config[MLX4_EN_NUM_UP];
};

struct mlx4_en_cee_params {
	u8 dcbx_cap;
	struct mlx4_en_cee_config dcb_cfg;
};

#endif

struct ethtool_flow_id {
	struct list_head list;
	struct ethtool_rx_flow_spec flow_spec;
	u64 id;
};

enum {
	MLX4_EN_FLAG_PROMISC		= (1 << 0),
	MLX4_EN_FLAG_MC_PROMISC		= (1 << 1),
	/* whether we need to enable hardware loopback by putting dmac
	 * in Tx WQE
	 */
	MLX4_EN_FLAG_ENABLE_HW_LOOPBACK	= (1 << 2),
	/* whether we need to drop packets that hardware loopback-ed */
	MLX4_EN_FLAG_RX_FILTER_NEEDED	= (1 << 3),
	MLX4_EN_FLAG_FORCE_PROMISC	= (1 << 4),
	MLX4_EN_FLAG_RX_CSUM_NON_TCP_UDP	= (1 << 5),
#ifdef CONFIG_MLX4_EN_DCB
	MLX4_EN_FLAG_DCB_ENABLED        = (1 << 6),
#endif
};

#define PORT_BEACON_MAX_LIMIT (65535)
#define MLX4_EN_MAC_HASH_SIZE (1 << BITS_PER_BYTE)
#define MLX4_EN_MAC_HASH_IDX 5

struct mlx4_en_stats_bitmap {
	DECLARE_BITMAP(bitmap, NUM_ALL_STATS);
	struct mutex mutex; /* for mutual access to stats bitmap */
};

struct mlx4_en_priv {
	struct mlx4_en_dev *mdev;
	struct mlx4_en_port_profile *prof;
	struct net_device *dev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct mlx4_en_port_state port_state;
	spinlock_t stats_lock;
	struct ethtool_flow_id ethtool_rules[MAX_NUM_OF_FS_RULES];
	/* To allow rules removal while port is going down */
	struct list_head ethtool_list;

	unsigned long last_moder_packets[MAX_RX_RINGS];
	unsigned long last_moder_tx_packets;
	unsigned long last_moder_bytes[MAX_RX_RINGS];
	unsigned long last_moder_jiffies;
	int last_moder_time[MAX_RX_RINGS];
	u16 rx_usecs;
	u16 rx_frames;
	u16 tx_usecs;
	u16 tx_frames;
	u32 pkt_rate_low;
	u16 rx_usecs_low;
	u32 pkt_rate_high;
	u16 rx_usecs_high;
	u16 sample_interval;
	u16 adaptive_rx_coal;
	u32 msg_enable;
	u32 loopback_ok;
	u32 validate_loopback;

	struct mlx4_hwq_resources res;
	int link_state;
	int last_link_state;
	bool port_up;
	int port;
	int registered;
	int allocated;
	int stride;
	unsigned char current_mac[ETH_ALEN + 2];
	int mac_index;
	unsigned max_mtu;
	int base_qpn;
	int cqe_factor;
	int cqe_size;

	struct mlx4_en_rss_map rss_map;
	__be32 ctrl_flags;
	u32 flags;
	u8 num_tx_rings_p_up;
	u32 tx_work_limit;
	u32 tx_ring_num;
	u32 rx_ring_num;
	u32 rx_skb_size;
	struct mlx4_en_frag_info frag_info[MLX4_EN_MAX_RX_FRAGS];
	u16 num_frags;
	u16 log_rx_info;
	int xdp_ring_num;

	struct mlx4_en_tx_ring **tx_ring;
	struct mlx4_en_rx_ring *rx_ring[MAX_RX_RINGS];
	struct mlx4_en_cq **tx_cq;
	struct mlx4_en_cq *rx_cq[MAX_RX_RINGS];
	struct mlx4_qp drop_qp;
	struct work_struct rx_mode_task;
	struct work_struct watchdog_task;
	struct work_struct linkstate_task;
	struct delayed_work stats_task;
	struct delayed_work service_task;
	struct work_struct vxlan_add_task;
	struct work_struct vxlan_del_task;
	struct mlx4_en_perf_stats pstats;
	struct mlx4_en_pkt_stats pkstats;
	struct mlx4_en_counter_stats pf_stats;
	struct mlx4_en_flow_stats_rx rx_priority_flowstats[MLX4_NUM_PRIORITIES];
	struct mlx4_en_flow_stats_tx tx_priority_flowstats[MLX4_NUM_PRIORITIES];
	struct mlx4_en_flow_stats_rx rx_flowstats;
	struct mlx4_en_flow_stats_tx tx_flowstats;
	struct mlx4_en_port_stats port_stats;
	struct mlx4_en_stats_bitmap stats_bitmap;
	struct list_head mc_list;
	struct list_head curr_list;
	u64 broadcast_id;
	struct mlx4_en_stat_out_mbox hw_stats;
	int vids[128];
	bool wol;
	struct device *ddev;
	struct hlist_head mac_hash[MLX4_EN_MAC_HASH_SIZE];
	struct hwtstamp_config hwtstamp_config;
	u32 counter_index;

#ifdef CONFIG_MLX4_EN_DCB
#define MLX4_EN_DCB_ENABLED	0x3
	struct ieee_ets ets;
	u16 maxrate[IEEE_8021QAZ_MAX_TCS];
	enum dcbnl_cndd_states cndd_state[IEEE_8021QAZ_MAX_TCS];
	struct mlx4_en_cee_params cee_params;
#endif
#ifdef CONFIG_RFS_ACCEL
	spinlock_t filters_lock;
	int last_filter_id;
	struct list_head filters;
	struct hlist_head filter_hash[1 << MLX4_EN_FILTER_HASH_SHIFT];
#endif
	u64 tunnel_reg_id;
	__be16 vxlan_port;

	u32 pflags;
	u8 rss_key[MLX4_EN_RSS_KEY_SIZE];
	u8 rss_hash_fn;
};

enum mlx4_en_wol {
	MLX4_EN_WOL_MAGIC = (1ULL << 61),
	MLX4_EN_WOL_ENABLED = (1ULL << 62),
};

struct mlx4_mac_entry {
	struct hlist_node hlist;
	unsigned char mac[ETH_ALEN + 2];
	u64 reg_id;
	struct rcu_head rcu;
};

static inline struct mlx4_cqe *mlx4_en_get_cqe(void *buf, int idx, int cqe_sz)
{
	return buf + idx * cqe_sz;
}

#define MLX4_EN_WOL_DO_MODIFY (1ULL << 63)

void mlx4_en_init_ptys2ethtool_map(void);
void mlx4_en_update_loopback_state(struct net_device *dev,
				   netdev_features_t features);

void mlx4_en_destroy_netdev(struct net_device *dev);
int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof);

int mlx4_en_start_port(struct net_device *dev);
void mlx4_en_stop_port(struct net_device *dev, int detach);

void mlx4_en_set_stats_bitmap(struct mlx4_dev *dev,
			      struct mlx4_en_stats_bitmap *stats_bitmap,
			      u8 rx_ppp, u8 rx_pause,
			      u8 tx_ppp, u8 tx_pause);

void mlx4_en_free_resources(struct mlx4_en_priv *priv);
int mlx4_en_alloc_resources(struct mlx4_en_priv *priv);

int mlx4_en_create_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq **pcq,
		      int entries, int ring, enum cq_type mode, int node);
void mlx4_en_destroy_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq **pcq);
int mlx4_en_activate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq,
			int cq_idx);
void mlx4_en_deactivate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_set_cq_moder(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_arm_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);

void mlx4_en_tx_irq(struct mlx4_cq *mcq);
u16 mlx4_en_select_queue(struct net_device *dev, struct sk_buff *skb,
			 void *accel_priv, select_queue_fallback_t fallback);
netdev_tx_t mlx4_en_xmit(struct sk_buff *skb, struct net_device *dev);

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring,
			   u32 size, u16 stride,
			   int node, int queue_index);
void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring **pring);
int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int user_prio);
void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring);
void mlx4_en_set_num_rx_rings(struct mlx4_en_dev *mdev);
void mlx4_en_recover_from_oom(struct mlx4_en_priv *priv);
int mlx4_en_create_rx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_ring **pring,
			   u32 size, u16 stride, int node);
void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring **pring,
			     u32 size, u16 stride);
int mlx4_en_activate_rx_rings(struct mlx4_en_priv *priv);
void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring);
int mlx4_en_process_rx_cq(struct net_device *dev,
			  struct mlx4_en_cq *cq,
			  int budget);
int mlx4_en_poll_rx_cq(struct napi_struct *napi, int budget);
int mlx4_en_poll_tx_cq(struct napi_struct *napi, int budget);
void mlx4_en_fill_qp_context(struct mlx4_en_priv *priv, int size, int stride,
		int is_tx, int rss, int qpn, int cqn, int user_prio,
		struct mlx4_qp_context *context);
void mlx4_en_sqp_event(struct mlx4_qp *qp, enum mlx4_event event);
int mlx4_en_change_mcast_lb(struct mlx4_en_priv *priv, struct mlx4_qp *qp,
			    int loopback);
void mlx4_en_calc_rx_buf(struct net_device *dev);
int mlx4_en_config_rss_steer(struct mlx4_en_priv *priv);
void mlx4_en_release_rss_steer(struct mlx4_en_priv *priv);
int mlx4_en_create_drop_qp(struct mlx4_en_priv *priv);
void mlx4_en_destroy_drop_qp(struct mlx4_en_priv *priv);
int mlx4_en_free_tx_buf(struct net_device *dev, struct mlx4_en_tx_ring *ring);
void mlx4_en_rx_irq(struct mlx4_cq *mcq);

int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);
int mlx4_SET_VLAN_FLTR(struct mlx4_dev *dev, struct mlx4_en_priv *priv);

int mlx4_en_DUMP_ETH_STATS(struct mlx4_en_dev *mdev, u8 port, u8 reset);
int mlx4_en_QUERY_PORT(struct mlx4_en_dev *mdev, u8 port);

#ifdef CONFIG_MLX4_EN_DCB
extern const struct dcbnl_rtnl_ops mlx4_en_dcbnl_ops;
extern const struct dcbnl_rtnl_ops mlx4_en_dcbnl_pfc_ops;
#endif

int mlx4_en_setup_tc(struct net_device *dev, u8 up);

#ifdef CONFIG_RFS_ACCEL
void mlx4_en_cleanup_filters(struct mlx4_en_priv *priv);
#endif

#define MLX4_EN_NUM_SELF_TEST	5
void mlx4_en_ex_selftest(struct net_device *dev, u32 *flags, u64 *buf);
void mlx4_en_ptp_overflow_check(struct mlx4_en_dev *mdev);

#define DEV_FEATURE_CHANGED(dev, new_features, feature) \
	((dev->features & feature) ^ (new_features & feature))

int mlx4_en_reset_config(struct net_device *dev,
			 struct hwtstamp_config ts_config,
			 netdev_features_t new_features);
void mlx4_en_update_pfc_stats_bitmap(struct mlx4_dev *dev,
				     struct mlx4_en_stats_bitmap *stats_bitmap,
				     u8 rx_ppp, u8 rx_pause,
				     u8 tx_ppp, u8 tx_pause);
int mlx4_en_netdev_event(struct notifier_block *this,
			 unsigned long event, void *ptr);

/*
 * Functions for time stamping
 */
u64 mlx4_en_get_cqe_ts(struct mlx4_cqe *cqe);
void mlx4_en_fill_hwtstamps(struct mlx4_en_dev *mdev,
			    struct skb_shared_hwtstamps *hwts,
			    u64 timestamp);
void mlx4_en_init_timestamp(struct mlx4_en_dev *mdev);
void mlx4_en_remove_timestamp(struct mlx4_en_dev *mdev);

/* Globals
 */
extern const struct ethtool_ops mlx4_en_ethtool_ops;



/*
 * printk / logging functions
 */

__printf(3, 4)
void en_print(const char *level, const struct mlx4_en_priv *priv,
	      const char *format, ...);

#define en_dbg(mlevel, priv, format, ...)				\
do {									\
	if (NETIF_MSG_##mlevel & (priv)->msg_enable)			\
		en_print(KERN_DEBUG, priv, format, ##__VA_ARGS__);	\
} while (0)
#define en_warn(priv, format, ...)					\
	en_print(KERN_WARNING, priv, format, ##__VA_ARGS__)
#define en_err(priv, format, ...)					\
	en_print(KERN_ERR, priv, format, ##__VA_ARGS__)
#define en_info(priv, format, ...)					\
	en_print(KERN_INFO, priv, format, ##__VA_ARGS__)

#define mlx4_err(mdev, format, ...)					\
	pr_err(DRV_NAME " %s: " format,					\
	       dev_name(&(mdev)->pdev->dev), ##__VA_ARGS__)
#define mlx4_info(mdev, format, ...)					\
	pr_info(DRV_NAME " %s: " format,				\
		dev_name(&(mdev)->pdev->dev), ##__VA_ARGS__)
#define mlx4_warn(mdev, format, ...)					\
	pr_warn(DRV_NAME " %s: " format,				\
		dev_name(&(mdev)->pdev->dev), ##__VA_ARGS__)

#endif
