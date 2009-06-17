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

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/inet_lro.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/qp.h>
#include <linux/mlx4/cq.h>
#include <linux/mlx4/srq.h>
#include <linux/mlx4/doorbell.h>

#include "en_port.h"

#define DRV_NAME	"mlx4_en"
#define DRV_VERSION	"1.4.1.1"
#define DRV_RELDATE	"June 2009"


#define MLX4_EN_MSG_LEVEL	(NETIF_MSG_LINK | NETIF_MSG_IFDOWN)

#define en_print(level, priv, format, arg...)			\
	{							\
	if ((priv)->registered)					\
		printk(level "%s: %s: " format, DRV_NAME,	\
			(priv->dev)->name, ## arg);		\
	else							\
		printk(level "%s: %s: Port %d: " format,	\
			DRV_NAME, dev_name(&priv->mdev->pdev->dev), \
			(priv)->port, ## arg);			\
	}

#define en_dbg(mlevel, priv, format, arg...)			\
	{							\
	if (NETIF_MSG_##mlevel & priv->msg_enable)		\
		en_print(KERN_DEBUG, priv, format, ## arg)	\
	}
#define en_warn(priv, format, arg...)				\
	en_print(KERN_WARNING, priv, format, ## arg)
#define en_err(priv, format, arg...)				\
	en_print(KERN_ERR, priv, format, ## arg)

#define mlx4_err(mdev, format, arg...) \
	printk(KERN_ERR "%s %s: " format , DRV_NAME ,\
		dev_name(&mdev->pdev->dev) , ## arg)
#define mlx4_info(mdev, format, arg...) \
	printk(KERN_INFO "%s %s: " format , DRV_NAME ,\
		dev_name(&mdev->pdev->dev) , ## arg)
#define mlx4_warn(mdev, format, arg...) \
	printk(KERN_WARNING "%s %s: " format , DRV_NAME ,\
		dev_name(&mdev->pdev->dev) , ## arg)

/*
 * Device constants
 */


#define MLX4_EN_PAGE_SHIFT	12
#define MLX4_EN_PAGE_SIZE	(1 << MLX4_EN_PAGE_SHIFT)
#define MAX_TX_RINGS		16
#define MAX_RX_RINGS		16
#define MAX_RSS_MAP_SIZE	64
#define RSS_FACTOR		2
#define TXBB_SIZE		64
#define HEADROOM		(2048 / TXBB_SIZE + 1)
#define MAX_LSO_HDR_SIZE	92
#define STAMP_STRIDE		64
#define STAMP_DWORDS		(STAMP_STRIDE / 4)
#define STAMP_SHIFT		31
#define STAMP_VAL		0x7fffffff
#define STATS_DELAY		(HZ / 4)

/* Typical TSO descriptor with 16 gather entries is 352 bytes... */
#define MAX_DESC_SIZE		512
#define MAX_DESC_TXBBS		(MAX_DESC_SIZE / TXBB_SIZE)

/*
 * OS related constants and tunables
 */

#define MLX4_EN_WATCHDOG_TIMEOUT	(15 * HZ)

#define MLX4_EN_ALLOC_ORDER	2
#define MLX4_EN_ALLOC_SIZE	(PAGE_SIZE << MLX4_EN_ALLOC_ORDER)

#define MLX4_EN_MAX_LRO_DESCRIPTORS	32

/* Receive fragment sizes; we use at most 4 fragments (for 9600 byte MTU
 * and 4K allocations) */
enum {
	FRAG_SZ0 = 512 - NET_IP_ALIGN,
	FRAG_SZ1 = 1024,
	FRAG_SZ2 = 4096,
	FRAG_SZ3 = MLX4_EN_ALLOC_SIZE
};
#define MLX4_EN_MAX_RX_FRAGS	4

/* Maximum ring sizes */
#define MLX4_EN_MAX_TX_SIZE	8192
#define MLX4_EN_MAX_RX_SIZE	8192

/* Minimum ring size for our page-allocation sceme to work */
#define MLX4_EN_MIN_RX_SIZE	(MLX4_EN_ALLOC_SIZE / SMP_CACHE_BYTES)
#define MLX4_EN_MIN_TX_SIZE	(4096 / TXBB_SIZE)

#define MLX4_EN_SMALL_PKT_SIZE		64
#define MLX4_EN_NUM_TX_RINGS		8
#define MLX4_EN_NUM_PPP_RINGS		8
#define MLX4_EN_DEF_TX_RING_SIZE	512
#define MLX4_EN_DEF_RX_RING_SIZE  	1024

/* Target number of packets to coalesce with interrupt moderation */
#define MLX4_EN_RX_COAL_TARGET	44
#define MLX4_EN_RX_COAL_TIME	0x10

#define MLX4_EN_TX_COAL_PKTS	5
#define MLX4_EN_TX_COAL_TIME	0x80

#define MLX4_EN_RX_RATE_LOW		400000
#define MLX4_EN_RX_COAL_TIME_LOW	0
#define MLX4_EN_RX_RATE_HIGH		450000
#define MLX4_EN_RX_COAL_TIME_HIGH	128
#define MLX4_EN_RX_SIZE_THRESH		1024
#define MLX4_EN_RX_RATE_THRESH		(1000000 / MLX4_EN_RX_COAL_TIME_HIGH)
#define MLX4_EN_SAMPLE_INTERVAL		0

#define MLX4_EN_AUTO_CONF	0xffff

#define MLX4_EN_DEF_RX_PAUSE	1
#define MLX4_EN_DEF_TX_PAUSE	1

/* Interval between sucessive polls in the Tx routine when polling is used
   instead of interrupts (in per-core Tx rings) - should be power of 2 */
#define MLX4_EN_TX_POLL_MODER	16
#define MLX4_EN_TX_POLL_TIMEOUT	(HZ / 4)

#define ETH_LLC_SNAP_SIZE	8

#define SMALL_PACKET_SIZE      (256 - NET_IP_ALIGN)
#define HEADER_COPY_SIZE       (128 - NET_IP_ALIGN)

#define MLX4_EN_MIN_MTU		46
#define ETH_BCAST		0xffffffffffffULL

#ifdef MLX4_EN_PERF_STAT
/* Number of samples to 'average' */
#define AVG_SIZE			128
#define AVG_FACTOR			1024
#define NUM_PERF_STATS			NUM_PERF_COUNTERS

#define INC_PERF_COUNTER(cnt)		(++(cnt))
#define ADD_PERF_COUNTER(cnt, add)	((cnt) += (add))
#define AVG_PERF_COUNTER(cnt, sample) \
	((cnt) = ((cnt) * (AVG_SIZE - 1) + (sample) * AVG_FACTOR) / AVG_SIZE)
#define GET_PERF_COUNTER(cnt)		(cnt)
#define GET_AVG_PERF_COUNTER(cnt)	((cnt) / AVG_FACTOR)

#else

#define NUM_PERF_STATS			0
#define INC_PERF_COUNTER(cnt)		do {} while (0)
#define ADD_PERF_COUNTER(cnt, add)	do {} while (0)
#define AVG_PERF_COUNTER(cnt, sample)	do {} while (0)
#define GET_PERF_COUNTER(cnt)		(0)
#define GET_AVG_PERF_COUNTER(cnt)	(0)
#endif /* MLX4_EN_PERF_STAT */

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
#define ILLEGAL_MAC(addr)	(addr == 0xffffffffffffULL || addr == 0x0)


struct mlx4_en_tx_info {
	struct sk_buff *skb;
	u32 nr_txbb;
	u8 linear;
	u8 data_offset;
	u8 inl;
};


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

struct mlx4_en_rx_alloc {
	struct page *page;
	u16 offset;
};

struct mlx4_en_tx_ring {
	struct mlx4_hwq_resources wqres;
	u32 size ; /* number of TXBBs */
	u32 size_mask;
	u16 stride;
	u16 cqn;	/* index of port CQ associated with this ring */
	u32 prod;
	u32 cons;
	u32 buf_size;
	u32 doorbell_qpn;
	void *buf;
	u16 poll_cnt;
	int blocked;
	struct mlx4_en_tx_info *tx_info;
	u8 *bounce_buf;
	u32 last_nr_txbb;
	struct mlx4_qp qp;
	struct mlx4_qp_context context;
	int qpn;
	enum mlx4_qp_state qp_state;
	struct mlx4_srq dummy;
	unsigned long bytes;
	unsigned long packets;
	spinlock_t comp_lock;
};

struct mlx4_en_rx_desc {
	struct mlx4_wqe_srq_next_seg next;
	/* actual number of entries depends on rx ring stride */
	struct mlx4_wqe_data_seg data[0];
};

struct mlx4_en_rx_ring {
	struct mlx4_srq srq;
	struct mlx4_hwq_resources wqres;
	struct mlx4_en_rx_alloc page_alloc[MLX4_EN_MAX_RX_FRAGS];
	struct net_lro_mgr lro;
	u32 size ;	/* number of Rx descs*/
	u32 actual_size;
	u32 size_mask;
	u16 stride;
	u16 log_stride;
	u16 cqn;	/* index of port CQ associated with this ring */
	u32 prod;
	u32 cons;
	u32 buf_size;
	int need_refill;
	int full;
	void *buf;
	void *rx_info;
	unsigned long bytes;
	unsigned long packets;
};


static inline int mlx4_en_can_lro(__be16 status)
{
	return (status & cpu_to_be16(MLX4_CQE_STATUS_IPV4	|
				     MLX4_CQE_STATUS_IPV4F	|
				     MLX4_CQE_STATUS_IPV6	|
				     MLX4_CQE_STATUS_IPV4OPT	|
				     MLX4_CQE_STATUS_TCP	|
				     MLX4_CQE_STATUS_UDP	|
				     MLX4_CQE_STATUS_IPOK)) ==
		cpu_to_be16(MLX4_CQE_STATUS_IPV4 |
			    MLX4_CQE_STATUS_IPOK |
			    MLX4_CQE_STATUS_TCP);
}

struct mlx4_en_cq {
	struct mlx4_cq          mcq;
	struct mlx4_hwq_resources wqres;
	int                     ring;
	spinlock_t              lock;
	struct net_device      *dev;
	struct napi_struct	napi;
	/* Per-core Tx cq processing support */
	struct timer_list timer;
	int size;
	int buf_size;
	unsigned vector;
	enum cq_type is_tx;
	u16 moder_time;
	u16 moder_cnt;
	struct mlx4_cqe *buf;
#define MLX4_EN_OPCODE_ERROR	0x1e
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
};

struct mlx4_en_profile {
	int rss_xor;
	int num_lro;
	u8 rss_mask;
	u32 active_ports;
	u32 small_pkt_int;
	u8 no_reset;
	struct mlx4_en_port_profile prof[MLX4_MAX_PORTS + 1];
};

struct mlx4_en_dev {
	struct mlx4_dev         *dev;
	struct pci_dev		*pdev;
	struct mutex		state_lock;
	struct net_device       *pndev[MLX4_MAX_PORTS + 1];
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
};


struct mlx4_en_rss_map {
	int size;
	int base_qpn;
	u16 map[MAX_RSS_MAP_SIZE];
	struct mlx4_qp qps[MAX_RSS_MAP_SIZE];
	enum mlx4_qp_state state[MAX_RSS_MAP_SIZE];
	struct mlx4_qp indir_qp;
	enum mlx4_qp_state indir_state;
};

struct mlx4_en_rss_context {
	__be32 base_qpn;
	__be32 default_qpn;
	u16 reserved;
	u8 hash_fn;
	u8 flags;
	__be32 rss_key[10];
};

struct mlx4_en_pkt_stats {
	unsigned long broadcast;
	unsigned long rx_prio[8];
	unsigned long tx_prio[8];
#define NUM_PKT_STATS		17
};

struct mlx4_en_port_stats {
	unsigned long lro_aggregated;
	unsigned long lro_flushed;
	unsigned long lro_no_desc;
	unsigned long tso_packets;
	unsigned long queue_stopped;
	unsigned long wake_queue;
	unsigned long tx_timeout;
	unsigned long rx_alloc_failed;
	unsigned long rx_chksum_good;
	unsigned long rx_chksum_none;
	unsigned long tx_chksum_offload;
#define NUM_PORT_STATS		11
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

struct mlx4_en_frag_info {
	u16 frag_size;
	u16 frag_prefix_size;
	u16 frag_stride;
	u16 frag_align;
	u16 last_offset;

};

struct mlx4_en_priv {
	struct mlx4_en_dev *mdev;
	struct mlx4_en_port_profile *prof;
	struct net_device *dev;
	struct vlan_group *vlgrp;
	struct net_device_stats stats;
	struct net_device_stats ret_stats;
	spinlock_t stats_lock;

	unsigned long last_moder_packets;
	unsigned long last_moder_tx_packets;
	unsigned long last_moder_bytes;
	unsigned long last_moder_jiffies;
	int last_moder_time;
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

	struct mlx4_hwq_resources res;
	int link_state;
	int last_link_state;
	bool port_up;
	int port;
	int registered;
	int allocated;
	int stride;
	int rx_csum;
	u64 mac;
	int mac_index;
	unsigned max_mtu;
	int base_qpn;

	struct mlx4_en_rss_map rss_map;
	u32 flags;
#define MLX4_EN_FLAG_PROMISC	0x1
	u32 tx_ring_num;
	u32 rx_ring_num;
	u32 rx_skb_size;
	struct mlx4_en_frag_info frag_info[MLX4_EN_MAX_RX_FRAGS];
	u16 num_frags;
	u16 log_rx_info;

	struct mlx4_en_tx_ring tx_ring[MAX_TX_RINGS];
	struct mlx4_en_rx_ring rx_ring[MAX_RX_RINGS];
	struct mlx4_en_cq tx_cq[MAX_TX_RINGS];
	struct mlx4_en_cq rx_cq[MAX_RX_RINGS];
	struct work_struct mcast_task;
	struct work_struct mac_task;
	struct delayed_work refill_task;
	struct work_struct watchdog_task;
	struct work_struct linkstate_task;
	struct delayed_work stats_task;
	struct mlx4_en_perf_stats pstats;
	struct mlx4_en_pkt_stats pkstats;
	struct mlx4_en_port_stats port_stats;
	struct dev_mc_list *mc_list;
	struct mlx4_en_stat_out_mbox hw_stats;
};


void mlx4_en_destroy_netdev(struct net_device *dev);
int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof);

int mlx4_en_start_port(struct net_device *dev);
void mlx4_en_stop_port(struct net_device *dev);

void mlx4_en_free_resources(struct mlx4_en_priv *priv);
int mlx4_en_alloc_resources(struct mlx4_en_priv *priv);

int mlx4_en_create_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq,
		      int entries, int ring, enum cq_type mode);
void mlx4_en_destroy_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_activate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
void mlx4_en_deactivate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_set_cq_moder(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_arm_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);

void mlx4_en_poll_tx_cq(unsigned long data);
void mlx4_en_tx_irq(struct mlx4_cq *mcq);
u16 mlx4_en_select_queue(struct net_device *dev, struct sk_buff *skb);
int mlx4_en_xmit(struct sk_buff *skb, struct net_device *dev);

int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv, struct mlx4_en_tx_ring *ring,
			   u32 size, u16 stride);
void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv, struct mlx4_en_tx_ring *ring);
int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int srqn);
void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring);

int mlx4_en_create_rx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_ring *ring,
			   u32 size, u16 stride);
void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring *ring);
int mlx4_en_activate_rx_rings(struct mlx4_en_priv *priv);
void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring);
int mlx4_en_process_rx_cq(struct net_device *dev,
			  struct mlx4_en_cq *cq,
			  int budget);
int mlx4_en_poll_rx_cq(struct napi_struct *napi, int budget);
void mlx4_en_fill_qp_context(struct mlx4_en_priv *priv, int size, int stride,
			     int is_tx, int rss, int qpn, int cqn, int srqn,
			     struct mlx4_qp_context *context);
void mlx4_en_sqp_event(struct mlx4_qp *qp, enum mlx4_event event);
int mlx4_en_map_buffer(struct mlx4_buf *buf);
void mlx4_en_unmap_buffer(struct mlx4_buf *buf);

void mlx4_en_calc_rx_buf(struct net_device *dev);
void mlx4_en_set_default_rss_map(struct mlx4_en_priv *priv,
				 struct mlx4_en_rss_map *rss_map,
				 int num_entries, int num_rings);
int mlx4_en_config_rss_steer(struct mlx4_en_priv *priv);
void mlx4_en_release_rss_steer(struct mlx4_en_priv *priv);
int mlx4_en_free_tx_buf(struct net_device *dev, struct mlx4_en_tx_ring *ring);
void mlx4_en_rx_refill(struct work_struct *work);
void mlx4_en_rx_irq(struct mlx4_cq *mcq);

int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);
int mlx4_SET_VLAN_FLTR(struct mlx4_dev *dev, u8 port, struct vlan_group *grp);
int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx);
int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc);

int mlx4_en_DUMP_ETH_STATS(struct mlx4_en_dev *mdev, u8 port, u8 reset);

/*
 * Globals
 */
extern const struct ethtool_ops mlx4_en_ethtool_ops;
#endif
