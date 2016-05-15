/*
 * Copyright (c) 2015-2016, Mellanox Technologies. All rights reserved.
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
 */
#ifndef __MLX5_EN_H__
#define __MLX5_EN_H__

#include <linux/if_vlan.h>
#include <linux/etherdevice.h>
#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/port.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/transobj.h>
#include <linux/rhashtable.h>
#include "wq.h"
#include "mlx5_core.h"

#define MLX5E_MAX_NUM_TC	8

#define MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x6
#define MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE                0x1
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE                0xd

#define MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ                 (64 * 1024)
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES                0x80

#define MLX5E_LOG_INDIR_RQT_SIZE       0x7
#define MLX5E_INDIR_RQT_SIZE           BIT(MLX5E_LOG_INDIR_RQT_SIZE)
#define MLX5E_MAX_NUM_CHANNELS         (MLX5E_INDIR_RQT_SIZE >> 1)
#define MLX5E_TX_CQ_POLL_BUDGET        128
#define MLX5E_UPDATE_STATS_INTERVAL    200 /* msecs */
#define MLX5E_SQ_BF_BUDGET             16

#define MLX5E_NUM_MAIN_GROUPS 9

#ifdef CONFIG_MLX5_CORE_EN_DCB
#define MLX5E_MAX_BW_ALLOC 100 /* Max percentage of BW allocation */
#define MLX5E_MIN_BW_ALLOC 1   /* Min percentage of BW allocation */
#endif

static const char vport_strings[][ETH_GSTRING_LEN] = {
	/* vport statistics */
	"rx_packets",
	"rx_bytes",
	"tx_packets",
	"tx_bytes",
	"rx_error_packets",
	"rx_error_bytes",
	"tx_error_packets",
	"tx_error_bytes",
	"rx_unicast_packets",
	"rx_unicast_bytes",
	"tx_unicast_packets",
	"tx_unicast_bytes",
	"rx_multicast_packets",
	"rx_multicast_bytes",
	"tx_multicast_packets",
	"tx_multicast_bytes",
	"rx_broadcast_packets",
	"rx_broadcast_bytes",
	"tx_broadcast_packets",
	"tx_broadcast_bytes",

	/* SW counters */
	"tso_packets",
	"tso_bytes",
	"tso_inner_packets",
	"tso_inner_bytes",
	"lro_packets",
	"lro_bytes",
	"rx_csum_good",
	"rx_csum_none",
	"rx_csum_sw",
	"tx_csum_offload",
	"tx_csum_inner",
	"tx_queue_stopped",
	"tx_queue_wake",
	"tx_queue_dropped",
	"rx_wqe_err",
};

struct mlx5e_vport_stats {
	/* HW counters */
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_error_packets;
	u64 rx_error_bytes;
	u64 tx_error_packets;
	u64 tx_error_bytes;
	u64 rx_unicast_packets;
	u64 rx_unicast_bytes;
	u64 tx_unicast_packets;
	u64 tx_unicast_bytes;
	u64 rx_multicast_packets;
	u64 rx_multicast_bytes;
	u64 tx_multicast_packets;
	u64 tx_multicast_bytes;
	u64 rx_broadcast_packets;
	u64 rx_broadcast_bytes;
	u64 tx_broadcast_packets;
	u64 tx_broadcast_bytes;

	/* SW counters */
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 lro_packets;
	u64 lro_bytes;
	u64 rx_csum_good;
	u64 rx_csum_none;
	u64 rx_csum_sw;
	u64 tx_csum_offload;
	u64 tx_csum_inner;
	u64 tx_queue_stopped;
	u64 tx_queue_wake;
	u64 tx_queue_dropped;
	u64 rx_wqe_err;

#define NUM_VPORT_COUNTERS     35
};

static const char pport_strings[][ETH_GSTRING_LEN] = {
	/* IEEE802.3 counters */
	"frames_tx",
	"frames_rx",
	"check_seq_err",
	"alignment_err",
	"octets_tx",
	"octets_received",
	"multicast_xmitted",
	"broadcast_xmitted",
	"multicast_rx",
	"broadcast_rx",
	"in_range_len_errors",
	"out_of_range_len",
	"too_long_errors",
	"symbol_err",
	"mac_control_tx",
	"mac_control_rx",
	"unsupported_op_rx",
	"pause_ctrl_rx",
	"pause_ctrl_tx",

	/* RFC2863 counters */
	"in_octets",
	"in_ucast_pkts",
	"in_discards",
	"in_errors",
	"in_unknown_protos",
	"out_octets",
	"out_ucast_pkts",
	"out_discards",
	"out_errors",
	"in_multicast_pkts",
	"in_broadcast_pkts",
	"out_multicast_pkts",
	"out_broadcast_pkts",

	/* RFC2819 counters */
	"drop_events",
	"octets",
	"pkts",
	"broadcast_pkts",
	"multicast_pkts",
	"crc_align_errors",
	"undersize_pkts",
	"oversize_pkts",
	"fragments",
	"jabbers",
	"collisions",
	"p64octets",
	"p65to127octets",
	"p128to255octets",
	"p256to511octets",
	"p512to1023octets",
	"p1024to1518octets",
	"p1519to2047octets",
	"p2048to4095octets",
	"p4096to8191octets",
	"p8192to10239octets",
};

#define NUM_IEEE_802_3_COUNTERS		19
#define NUM_RFC_2863_COUNTERS		13
#define NUM_RFC_2819_COUNTERS		21
#define NUM_PPORT_COUNTERS		(NUM_IEEE_802_3_COUNTERS + \
					 NUM_RFC_2863_COUNTERS + \
					 NUM_RFC_2819_COUNTERS)

struct mlx5e_pport_stats {
	__be64 IEEE_802_3_counters[NUM_IEEE_802_3_COUNTERS];
	__be64 RFC_2863_counters[NUM_RFC_2863_COUNTERS];
	__be64 RFC_2819_counters[NUM_RFC_2819_COUNTERS];
};

static const char rq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",
	"bytes",
	"csum_none",
	"csum_sw",
	"lro_packets",
	"lro_bytes",
	"wqe_err"
};

struct mlx5e_rq_stats {
	u64 packets;
	u64 bytes;
	u64 csum_none;
	u64 csum_sw;
	u64 lro_packets;
	u64 lro_bytes;
	u64 wqe_err;
#define NUM_RQ_STATS 7
};

static const char sq_stats_strings[][ETH_GSTRING_LEN] = {
	"packets",
	"bytes",
	"tso_packets",
	"tso_bytes",
	"tso_inner_packets",
	"tso_inner_bytes",
	"csum_offload_inner",
	"nop",
	"csum_offload_none",
	"stopped",
	"wake",
	"dropped",
};

struct mlx5e_sq_stats {
	/* commonly accessed in data path */
	u64 packets;
	u64 bytes;
	u64 tso_packets;
	u64 tso_bytes;
	u64 tso_inner_packets;
	u64 tso_inner_bytes;
	u64 csum_offload_inner;
	u64 nop;
	/* less likely accessed in data path */
	u64 csum_offload_none;
	u64 stopped;
	u64 wake;
	u64 dropped;
#define NUM_SQ_STATS 12
};

struct mlx5e_stats {
	struct mlx5e_vport_stats   vport;
	struct mlx5e_pport_stats   pport;
};

struct mlx5e_params {
	u8  log_sq_size;
	u8  log_rq_size;
	u16 num_channels;
	u8  num_tc;
	u16 rx_cq_moderation_usec;
	u16 rx_cq_moderation_pkts;
	u16 tx_cq_moderation_usec;
	u16 tx_cq_moderation_pkts;
	u16 min_rx_wqes;
	bool lro_en;
	u32 lro_wqe_sz;
	u16 tx_max_inline;
	u8  rss_hfunc;
	u8  toeplitz_hash_key[40];
	u32 indirection_rqt[MLX5E_INDIR_RQT_SIZE];
#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct ieee_ets ets;
#endif
};

struct mlx5e_tstamp {
	rwlock_t                   lock;
	struct cyclecounter        cycles;
	struct timecounter         clock;
	struct hwtstamp_config     hwtstamp_config;
	u32                        nominal_c_mult;
	unsigned long              overflow_period;
	struct delayed_work        overflow_work;
	struct mlx5_core_dev      *mdev;
	struct ptp_clock          *ptp;
	struct ptp_clock_info      ptp_info;
};

enum {
	MLX5E_RQ_STATE_POST_WQES_ENABLE,
};

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	struct napi_struct        *napi;
	struct mlx5_core_cq        mcq;
	struct mlx5e_channel      *channel;
	struct mlx5e_priv         *priv;

	/* control */
	struct mlx5_wq_ctrl        wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5e_rq {
	/* data path */
	struct mlx5_wq_ll      wq;
	u32                    wqe_sz;
	struct sk_buff       **skb;

	struct device         *pdev;
	struct net_device     *netdev;
	struct mlx5e_tstamp   *tstamp;
	struct mlx5e_rq_stats  stats;
	struct mlx5e_cq        cq;

	unsigned long          state;
	int                    ix;

	/* control */
	struct mlx5_wq_ctrl    wq_ctrl;
	u32                    rqn;
	struct mlx5e_channel  *channel;
	struct mlx5e_priv     *priv;
} ____cacheline_aligned_in_smp;

struct mlx5e_tx_wqe_info {
	u32 num_bytes;
	u8  num_wqebbs;
	u8  num_dma;
};

enum mlx5e_dma_map_type {
	MLX5E_DMA_MAP_SINGLE,
	MLX5E_DMA_MAP_PAGE
};

struct mlx5e_sq_dma {
	dma_addr_t              addr;
	u32                     size;
	enum mlx5e_dma_map_type type;
};

enum {
	MLX5E_SQ_STATE_WAKE_TXQ_ENABLE,
	MLX5E_SQ_STATE_BF_ENABLE,
};

struct mlx5e_sq {
	/* data path */

	/* dirtied @completion */
	u16                        cc;
	u32                        dma_fifo_cc;

	/* dirtied @xmit */
	u16                        pc ____cacheline_aligned_in_smp;
	u32                        dma_fifo_pc;
	u16                        bf_offset;
	u16                        prev_cc;
	u8                         bf_budget;
	struct mlx5e_sq_stats      stats;

	struct mlx5e_cq            cq;

	/* pointers to per packet info: write@xmit, read@completion */
	struct sk_buff           **skb;
	struct mlx5e_sq_dma       *dma_fifo;
	struct mlx5e_tx_wqe_info  *wqe_info;

	/* read only */
	struct mlx5_wq_cyc         wq;
	u32                        dma_fifo_mask;
	void __iomem              *uar_map;
	struct netdev_queue       *txq;
	u32                        sqn;
	u16                        bf_buf_size;
	u16                        max_inline;
	u16                        edge;
	struct device             *pdev;
	struct mlx5e_tstamp       *tstamp;
	__be32                     mkey_be;
	unsigned long              state;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5_uar            uar;
	struct mlx5e_channel      *channel;
	int                        tc;
} ____cacheline_aligned_in_smp;

static inline bool mlx5e_sq_has_room_for(struct mlx5e_sq *sq, u16 n)
{
	return (((sq->wq.sz_m1 & (sq->cc - sq->pc)) >= n) ||
		(sq->cc  == sq->pc));
}

enum channel_flags {
	MLX5E_CHANNEL_NAPI_SCHED = 1,
};

struct mlx5e_channel {
	/* data path */
	struct mlx5e_rq            rq;
	struct mlx5e_sq            sq[MLX5E_MAX_NUM_TC];
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;
	u8                         num_tc;
	unsigned long              flags;

	/* control */
	struct mlx5e_priv         *priv;
	int                        ix;
	int                        cpu;
};

enum mlx5e_traffic_types {
	MLX5E_TT_IPV4_TCP,
	MLX5E_TT_IPV6_TCP,
	MLX5E_TT_IPV4_UDP,
	MLX5E_TT_IPV6_UDP,
	MLX5E_TT_IPV4_IPSEC_AH,
	MLX5E_TT_IPV6_IPSEC_AH,
	MLX5E_TT_IPV4_IPSEC_ESP,
	MLX5E_TT_IPV6_IPSEC_ESP,
	MLX5E_TT_IPV4,
	MLX5E_TT_IPV6,
	MLX5E_TT_ANY,
	MLX5E_NUM_TT,
};

#define IS_HASHING_TT(tt) (tt != MLX5E_TT_ANY)

enum mlx5e_rqt_ix {
	MLX5E_INDIRECTION_RQT,
	MLX5E_SINGLE_RQ_RQT,
	MLX5E_NUM_RQT,
};

struct mlx5e_eth_addr_info {
	u8  addr[ETH_ALEN + 2];
	u32 tt_vec;
	struct mlx5_flow_rule *ft_rule[MLX5E_NUM_TT];
};

#define MLX5E_ETH_ADDR_HASH_SIZE (1 << BITS_PER_BYTE)

struct mlx5e_eth_addr_db {
	struct hlist_head          netdev_uc[MLX5E_ETH_ADDR_HASH_SIZE];
	struct hlist_head          netdev_mc[MLX5E_ETH_ADDR_HASH_SIZE];
	struct mlx5e_eth_addr_info broadcast;
	struct mlx5e_eth_addr_info allmulti;
	struct mlx5e_eth_addr_info promisc;
	bool                       broadcast_enabled;
	bool                       allmulti_enabled;
	bool                       promisc_enabled;
};

enum {
	MLX5E_STATE_ASYNC_EVENTS_ENABLE,
	MLX5E_STATE_OPENED,
	MLX5E_STATE_DESTROYING,
};

struct mlx5e_vlan_db {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct mlx5_flow_rule	*active_vlans_rule[VLAN_N_VID];
	struct mlx5_flow_rule	*untagged_rule;
	struct mlx5_flow_rule	*any_vlan_rule;
	bool          filter_disabled;
};

struct mlx5e_vxlan_db {
	spinlock_t			lock; /* protect vxlan table */
	struct radix_tree_root		tree;
};

struct mlx5e_flow_table {
	int num_groups;
	struct mlx5_flow_table		*t;
	struct mlx5_flow_group		**g;
};

struct mlx5e_tc_flow_table {
	struct mlx5_flow_table		*t;

	struct rhashtable_params        ht_params;
	struct rhashtable               ht;
};

struct mlx5e_flow_tables {
	struct mlx5_flow_namespace	*ns;
	struct mlx5e_tc_flow_table	tc;
	struct mlx5e_flow_table		vlan;
	struct mlx5e_flow_table		main;
};

struct mlx5e_priv {
	/* priv data path fields - start */
	struct mlx5e_sq            **txq_to_sq_map;
	int channeltc_to_txq_map[MLX5E_MAX_NUM_CHANNELS][MLX5E_MAX_NUM_TC];
	/* priv data path fields - end */

	unsigned long              state;
	struct mutex               state_lock; /* Protects Interface state */
	struct mlx5_uar            cq_uar;
	u32                        pdn;
	u32                        tdn;
	struct mlx5_core_mkey      mkey;
	struct mlx5e_rq            drop_rq;

	struct mlx5e_channel     **channel;
	u32                        tisn[MLX5E_MAX_NUM_TC];
	u32                        rqtn[MLX5E_NUM_RQT];
	u32                        tirn[MLX5E_NUM_TT];

	struct mlx5e_flow_tables   fts;
	struct mlx5e_eth_addr_db   eth_addr;
	struct mlx5e_vlan_db       vlan;
#ifdef CONFIG_MLX5_CORE_EN_VXLAN
	struct mlx5e_vxlan_db      vxlan;
#endif

	struct mlx5e_params        params;
	struct workqueue_struct    *wq;
	struct work_struct         update_carrier_work;
	struct work_struct         set_rx_mode_work;
	struct delayed_work        update_stats_work;

	struct mlx5_core_dev      *mdev;
	struct net_device         *netdev;
	struct mlx5e_stats         stats;
	struct mlx5e_tstamp        tstamp;
};

#define MLX5E_NET_IP_ALIGN 2

struct mlx5e_tx_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_eth_seg  eth;
};

struct mlx5e_rx_wqe {
	struct mlx5_wqe_srq_next_seg  next;
	struct mlx5_wqe_data_seg      data;
};

enum mlx5e_link_mode {
	MLX5E_1000BASE_CX_SGMII	 = 0,
	MLX5E_1000BASE_KX	 = 1,
	MLX5E_10GBASE_CX4	 = 2,
	MLX5E_10GBASE_KX4	 = 3,
	MLX5E_10GBASE_KR	 = 4,
	MLX5E_20GBASE_KR2	 = 5,
	MLX5E_40GBASE_CR4	 = 6,
	MLX5E_40GBASE_KR4	 = 7,
	MLX5E_56GBASE_R4	 = 8,
	MLX5E_10GBASE_CR	 = 12,
	MLX5E_10GBASE_SR	 = 13,
	MLX5E_10GBASE_ER	 = 14,
	MLX5E_40GBASE_SR4	 = 15,
	MLX5E_40GBASE_LR4	 = 16,
	MLX5E_100GBASE_CR4	 = 20,
	MLX5E_100GBASE_SR4	 = 21,
	MLX5E_100GBASE_KR4	 = 22,
	MLX5E_100GBASE_LR4	 = 23,
	MLX5E_100BASE_TX	 = 24,
	MLX5E_1000BASE_T	 = 25,
	MLX5E_10GBASE_T		 = 26,
	MLX5E_25GBASE_CR	 = 27,
	MLX5E_25GBASE_KR	 = 28,
	MLX5E_25GBASE_SR	 = 29,
	MLX5E_50GBASE_CR2	 = 30,
	MLX5E_50GBASE_KR2	 = 31,
	MLX5E_LINK_MODES_NUMBER,
};

#define MLX5E_PROT_MASK(link_mode) (1 << link_mode)

void mlx5e_send_nop(struct mlx5e_sq *sq, bool notify_hw);
u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback);
netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev);

void mlx5e_completion_event(struct mlx5_core_cq *mcq);
void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event);
int mlx5e_napi_poll(struct napi_struct *napi, int budget);
bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget);
int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget);
bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq);
struct mlx5_cqe64 *mlx5e_get_cqe(struct mlx5e_cq *cq);

void mlx5e_update_stats(struct mlx5e_priv *priv);

int mlx5e_create_flow_tables(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_tables(struct mlx5e_priv *priv);
void mlx5e_init_eth_addr(struct mlx5e_priv *priv);
void mlx5e_set_rx_mode_work(struct work_struct *work);

void mlx5e_fill_hwstamp(struct mlx5e_tstamp *clock, u64 timestamp,
			struct skb_shared_hwtstamps *hwts);
void mlx5e_timestamp_init(struct mlx5e_priv *priv);
void mlx5e_timestamp_cleanup(struct mlx5e_priv *priv);
int mlx5e_hwstamp_set(struct net_device *dev, struct ifreq *ifr);
int mlx5e_hwstamp_get(struct net_device *dev, struct ifreq *ifr);

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid);
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid);
void mlx5e_enable_vlan_filter(struct mlx5e_priv *priv);
void mlx5e_disable_vlan_filter(struct mlx5e_priv *priv);

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, enum mlx5e_rqt_ix rqt_ix);
void mlx5e_build_tir_ctx_hash(void *tirc, struct mlx5e_priv *priv);

int mlx5e_open_locked(struct net_device *netdev);
int mlx5e_close_locked(struct net_device *netdev);
void mlx5e_build_default_indir_rqt(u32 *indirection_rqt, int len,
				   int num_channels);

static inline void mlx5e_tx_notify_hw(struct mlx5e_sq *sq,
				      struct mlx5e_tx_wqe *wqe, int bf_sz)
{
	u16 ofst = MLX5_BF_OFFSET + sq->bf_offset;

	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	*sq->wq.db = cpu_to_be32(sq->pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();
	if (bf_sz)
		__iowrite64_copy(sq->uar_map + ofst, &wqe->ctrl, bf_sz);
	else
		mlx5_write64((__be32 *)&wqe->ctrl, sq->uar_map + ofst, NULL);
	/* flush the write-combining mapped buffer */
	wmb();

	sq->bf_offset ^= sq->bf_buf_size;
}

static inline void mlx5e_cq_arm(struct mlx5e_cq *cq)
{
	struct mlx5_core_cq *mcq;

	mcq = &cq->mcq;
	mlx5_cq_arm(mcq, MLX5_CQ_DB_REQ_NOT, mcq->uar->map, NULL, cq->wq.cc);
}

static inline int mlx5e_get_max_num_channels(struct mlx5_core_dev *mdev)
{
	return min_t(int, mdev->priv.eq_table.num_comp_vectors,
		     MLX5E_MAX_NUM_CHANNELS);
}

extern const struct ethtool_ops mlx5e_ethtool_ops;
#ifdef CONFIG_MLX5_CORE_EN_DCB
extern const struct dcbnl_rtnl_ops mlx5e_dcbnl_ops;
int mlx5e_dcbnl_ieee_setets_core(struct mlx5e_priv *priv, struct ieee_ets *ets);
#endif

u16 mlx5e_get_max_inline_cap(struct mlx5_core_dev *mdev);

#endif /* __MLX5_EN_H__ */
