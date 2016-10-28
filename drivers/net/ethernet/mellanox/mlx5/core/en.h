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
#include <net/switchdev.h>
#include "wq.h"
#include "mlx5_core.h"
#include "en_stats.h"

#define MLX5_SET_CFG(p, f, v) MLX5_SET(create_flow_group_in, p, f, v)

#define MLX5E_MAX_NUM_TC	8

#define MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x6
#define MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE                0x1
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW            0x1
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE_MPW            0x3
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE_MPW            0x6

#define MLX5_RX_HEADROOM NET_SKB_PAD

#define MLX5_MPWRQ_LOG_STRIDE_SIZE		6  /* >= 6, HW restriction */
#define MLX5_MPWRQ_LOG_STRIDE_SIZE_CQE_COMPRESS	8  /* >= 6, HW restriction */
#define MLX5_MPWRQ_LOG_WQE_SZ			18
#define MLX5_MPWRQ_WQE_PAGE_ORDER  (MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT > 0 ? \
				    MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT : 0)
#define MLX5_MPWRQ_PAGES_PER_WQE		BIT(MLX5_MPWRQ_WQE_PAGE_ORDER)
#define MLX5_MPWRQ_STRIDES_PER_PAGE		(MLX5_MPWRQ_NUM_STRIDES >> \
						 MLX5_MPWRQ_WQE_PAGE_ORDER)

#define MLX5_MTT_OCTW(npages) (ALIGN(npages, 8) / 2)
#define MLX5E_REQUIRED_MTTS(rqs, wqes)\
	(rqs * wqes * ALIGN(MLX5_MPWRQ_PAGES_PER_WQE, 8))
#define MLX5E_VALID_NUM_MTTS(num_mtts) (MLX5_MTT_OCTW(num_mtts) <= U16_MAX)

#define MLX5_UMR_ALIGN				(2048)
#define MLX5_MPWRQ_SMALL_PACKET_THRESHOLD	(128)

#define MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ                 (64 * 1024)
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE 0x3
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES                0x80
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES_MPW            0x2

#define MLX5E_LOG_INDIR_RQT_SIZE       0x7
#define MLX5E_INDIR_RQT_SIZE           BIT(MLX5E_LOG_INDIR_RQT_SIZE)
#define MLX5E_MAX_NUM_CHANNELS         (MLX5E_INDIR_RQT_SIZE >> 1)
#define MLX5E_MAX_NUM_SQS              (MLX5E_MAX_NUM_CHANNELS * MLX5E_MAX_NUM_TC)
#define MLX5E_TX_CQ_POLL_BUDGET        128
#define MLX5E_UPDATE_STATS_INTERVAL    200 /* msecs */
#define MLX5E_SQ_BF_BUDGET             16

#define MLX5E_ICOSQ_MAX_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_umr_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_XDP_MIN_INLINE (ETH_HLEN + VLAN_HLEN)
#define MLX5E_XDP_IHS_DS_COUNT \
	DIV_ROUND_UP(MLX5E_XDP_MIN_INLINE - 2, MLX5_SEND_WQE_DS)
#define MLX5E_XDP_TX_DS_COUNT \
	(MLX5E_XDP_IHS_DS_COUNT + \
	 (sizeof(struct mlx5e_tx_wqe) / MLX5_SEND_WQE_DS) + 1 /* SG DS */)
#define MLX5E_XDP_TX_WQEBBS \
	DIV_ROUND_UP(MLX5E_XDP_TX_DS_COUNT, MLX5_SEND_WQEBB_NUM_DS)

#define MLX5E_NUM_MAIN_GROUPS 9

static inline u16 mlx5_min_rx_wqes(int wq_type, u32 wq_size)
{
	switch (wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return min_t(u16, MLX5E_PARAMS_DEFAULT_MIN_RX_WQES_MPW,
			     wq_size / 2);
	default:
		return min_t(u16, MLX5E_PARAMS_DEFAULT_MIN_RX_WQES,
			     wq_size / 2);
	}
}

static inline int mlx5_min_log_rq_size(int wq_type)
{
	switch (wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW;
	default:
		return MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE;
	}
}

static inline int mlx5_max_log_rq_size(int wq_type)
{
	switch (wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE_MPW;
	default:
		return MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE;
	}
}

enum {
	MLX5E_INLINE_MODE_L2,
	MLX5E_INLINE_MODE_VPORT_CONTEXT,
	MLX5_INLINE_MODE_NOT_REQUIRED,
};

struct mlx5e_tx_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_eth_seg  eth;
};

struct mlx5e_rx_wqe {
	struct mlx5_wqe_srq_next_seg  next;
	struct mlx5_wqe_data_seg      data;
};

struct mlx5e_umr_wqe {
	struct mlx5_wqe_ctrl_seg       ctrl;
	struct mlx5_wqe_umr_ctrl_seg   uctrl;
	struct mlx5_mkey_seg           mkc;
	struct mlx5_wqe_data_seg       data;
};

static const char mlx5e_priv_flags[][ETH_GSTRING_LEN] = {
	"rx_cqe_moder",
};

enum mlx5e_priv_flag {
	MLX5E_PFLAG_RX_CQE_BASED_MODER = (1 << 0),
};

#define MLX5E_SET_PRIV_FLAG(priv, pflag, enable)    \
	do {                                        \
		if (enable)                         \
			priv->pflags |= pflag;      \
		else                                \
			priv->pflags &= ~pflag;     \
	} while (0)

#ifdef CONFIG_MLX5_CORE_EN_DCB
#define MLX5E_MAX_BW_ALLOC 100 /* Max percentage of BW allocation */
#endif

struct mlx5e_cq_moder {
	u16 usec;
	u16 pkts;
};

struct mlx5e_params {
	u8  log_sq_size;
	u8  rq_wq_type;
	u8  mpwqe_log_stride_sz;
	u8  mpwqe_log_num_strides;
	u8  log_rq_size;
	u16 num_channels;
	u8  num_tc;
	u8  rx_cq_period_mode;
	bool rx_cqe_compress_admin;
	bool rx_cqe_compress;
	struct mlx5e_cq_moder rx_cq_moderation;
	struct mlx5e_cq_moder tx_cq_moderation;
	u16 min_rx_wqes;
	bool lro_en;
	u32 lro_wqe_sz;
	u16 tx_max_inline;
	u8  tx_min_inline_mode;
	u8  rss_hfunc;
	u8  toeplitz_hash_key[40];
	u32 indirection_rqt[MLX5E_INDIR_RQT_SIZE];
	bool vlan_strip_disable;
#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct ieee_ets ets;
#endif
	bool rx_am_enabled;
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
	MLX5E_RQ_STATE_FLUSH,
	MLX5E_RQ_STATE_UMR_WQE_IN_PROGRESS,
	MLX5E_RQ_STATE_AM,
};

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	u16                        event_ctr;
	struct napi_struct        *napi;
	struct mlx5_core_cq        mcq;
	struct mlx5e_channel      *channel;
	struct mlx5e_priv         *priv;

	/* cqe decompression */
	struct mlx5_cqe64          title;
	struct mlx5_mini_cqe8      mini_arr[MLX5_MINI_CQE_ARRAY_SIZE];
	u8                         mini_arr_idx;
	u16                        decmprs_left;
	u16                        decmprs_wqe_counter;

	/* control */
	struct mlx5_wq_ctrl        wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5e_rq;
typedef void (*mlx5e_fp_handle_rx_cqe)(struct mlx5e_rq *rq,
				       struct mlx5_cqe64 *cqe);
typedef int (*mlx5e_fp_alloc_wqe)(struct mlx5e_rq *rq, struct mlx5e_rx_wqe *wqe,
				  u16 ix);

typedef void (*mlx5e_fp_dealloc_wqe)(struct mlx5e_rq *rq, u16 ix);

struct mlx5e_dma_info {
	struct page	*page;
	dma_addr_t	addr;
};

struct mlx5e_rx_am_stats {
	int ppms; /* packets per msec */
	int epms; /* events per msec */
};

struct mlx5e_rx_am_sample {
	ktime_t		time;
	unsigned int	pkt_ctr;
	u16		event_ctr;
};

struct mlx5e_rx_am { /* Adaptive Moderation */
	u8					state;
	struct mlx5e_rx_am_stats		prev_stats;
	struct mlx5e_rx_am_sample		start_sample;
	struct work_struct			work;
	u8					profile_ix;
	u8					mode;
	u8					tune_state;
	u8					steps_right;
	u8					steps_left;
	u8					tired;
};

/* a single cache unit is capable to serve one napi call (for non-striding rq)
 * or a MPWQE (for striding rq).
 */
#define MLX5E_CACHE_UNIT	(MLX5_MPWRQ_PAGES_PER_WQE > NAPI_POLL_WEIGHT ? \
				 MLX5_MPWRQ_PAGES_PER_WQE : NAPI_POLL_WEIGHT)
#define MLX5E_CACHE_SIZE	(2 * roundup_pow_of_two(MLX5E_CACHE_UNIT))
struct mlx5e_page_cache {
	u32 head;
	u32 tail;
	struct mlx5e_dma_info page_cache[MLX5E_CACHE_SIZE];
};

struct mlx5e_rq {
	/* data path */
	struct mlx5_wq_ll      wq;

	union {
		struct mlx5e_dma_info *dma_info;
		struct {
			struct mlx5e_mpw_info *info;
			void                  *mtt_no_align;
			u32                    mtt_offset;
		} mpwqe;
	};
	struct {
		u8             page_order;
		u32            wqe_sz;    /* wqe data buffer size */
		u8             map_dir;   /* dma map direction */
	} buff;
	__be32                 mkey_be;

	struct device         *pdev;
	struct net_device     *netdev;
	struct mlx5e_tstamp   *tstamp;
	struct mlx5e_rq_stats  stats;
	struct mlx5e_cq        cq;
	struct mlx5e_page_cache page_cache;

	mlx5e_fp_handle_rx_cqe handle_rx_cqe;
	mlx5e_fp_alloc_wqe     alloc_wqe;
	mlx5e_fp_dealloc_wqe   dealloc_wqe;

	unsigned long          state;
	int                    ix;

	struct mlx5e_rx_am     am; /* Adaptive Moderation */
	struct bpf_prog       *xdp_prog;

	/* control */
	struct mlx5_wq_ctrl    wq_ctrl;
	u8                     wq_type;
	u32                    mpwqe_stride_sz;
	u32                    mpwqe_num_strides;
	u32                    rqn;
	struct mlx5e_channel  *channel;
	struct mlx5e_priv     *priv;
} ____cacheline_aligned_in_smp;

struct mlx5e_umr_dma_info {
	__be64                *mtt;
	dma_addr_t             mtt_addr;
	struct mlx5e_dma_info  dma_info[MLX5_MPWRQ_PAGES_PER_WQE];
	struct mlx5e_umr_wqe   wqe;
};

struct mlx5e_mpw_info {
	struct mlx5e_umr_dma_info umr;
	u16 consumed_strides;
	u16 skbs_frags[MLX5_MPWRQ_PAGES_PER_WQE];
};

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
	MLX5E_SQ_STATE_FLUSH,
	MLX5E_SQ_STATE_BF_ENABLE,
};

struct mlx5e_sq_wqe_info {
	u8  opcode;
	u8  num_wqebbs;
};

enum mlx5e_sq_type {
	MLX5E_SQ_TXQ,
	MLX5E_SQ_ICO,
	MLX5E_SQ_XDP
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

	/* pointers to per tx element info: write@xmit, read@completion */
	union {
		struct {
			struct sk_buff           **skb;
			struct mlx5e_sq_dma       *dma_fifo;
			struct mlx5e_tx_wqe_info  *wqe_info;
		} txq;
		struct mlx5e_sq_wqe_info *ico_wqe;
		struct {
			struct mlx5e_sq_wqe_info  *wqe_info;
			struct mlx5e_dma_info     *di;
			bool                       doorbell;
		} xdp;
	} db;

	/* read only */
	struct mlx5_wq_cyc         wq;
	u32                        dma_fifo_mask;
	void __iomem              *uar_map;
	struct netdev_queue       *txq;
	u32                        sqn;
	u16                        bf_buf_size;
	u16                        max_inline;
	u8                         min_inline_mode;
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
	u32                        rate_limit;
	u8                         type;
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
	struct mlx5e_sq            xdp_sq;
	struct mlx5e_sq            sq[MLX5E_MAX_NUM_TC];
	struct mlx5e_sq            icosq;   /* internal control operations */
	bool                       xdp;
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
	MLX5E_NUM_INDIR_TIRS = MLX5E_TT_ANY,
};

enum {
	MLX5E_STATE_ASYNC_EVENTS_ENABLED,
	MLX5E_STATE_OPENED,
	MLX5E_STATE_DESTROYING,
};

struct mlx5e_vxlan_db {
	spinlock_t			lock; /* protect vxlan table */
	struct radix_tree_root		tree;
};

struct mlx5e_l2_rule {
	u8  addr[ETH_ALEN + 2];
	struct mlx5_flow_rule *rule;
};

struct mlx5e_flow_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
};

#define MLX5E_L2_ADDR_HASH_SIZE BIT(BITS_PER_BYTE)

struct mlx5e_tc_table {
	struct mlx5_flow_table		*t;

	struct rhashtable_params        ht_params;
	struct rhashtable               ht;
};

struct mlx5e_vlan_table {
	struct mlx5e_flow_table		ft;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct mlx5_flow_rule	*active_vlans_rule[VLAN_N_VID];
	struct mlx5_flow_rule	*untagged_rule;
	struct mlx5_flow_rule	*any_vlan_rule;
	bool          filter_disabled;
};

struct mlx5e_l2_table {
	struct mlx5e_flow_table    ft;
	struct hlist_head          netdev_uc[MLX5E_L2_ADDR_HASH_SIZE];
	struct hlist_head          netdev_mc[MLX5E_L2_ADDR_HASH_SIZE];
	struct mlx5e_l2_rule	   broadcast;
	struct mlx5e_l2_rule	   allmulti;
	struct mlx5e_l2_rule	   promisc;
	bool                       broadcast_enabled;
	bool                       allmulti_enabled;
	bool                       promisc_enabled;
};

/* L3/L4 traffic type classifier */
struct mlx5e_ttc_table {
	struct mlx5e_flow_table  ft;
	struct mlx5_flow_rule	 *rules[MLX5E_NUM_TT];
};

#define ARFS_HASH_SHIFT BITS_PER_BYTE
#define ARFS_HASH_SIZE BIT(BITS_PER_BYTE)
struct arfs_table {
	struct mlx5e_flow_table  ft;
	struct mlx5_flow_rule    *default_rule;
	struct hlist_head	 rules_hash[ARFS_HASH_SIZE];
};

enum  arfs_type {
	ARFS_IPV4_TCP,
	ARFS_IPV6_TCP,
	ARFS_IPV4_UDP,
	ARFS_IPV6_UDP,
	ARFS_NUM_TYPES,
};

struct mlx5e_arfs_tables {
	struct arfs_table arfs_tables[ARFS_NUM_TYPES];
	/* Protect aRFS rules list */
	spinlock_t                     arfs_lock;
	struct list_head               rules;
	int                            last_filter_id;
	struct workqueue_struct        *wq;
};

/* NIC prio FTS */
enum {
	MLX5E_VLAN_FT_LEVEL = 0,
	MLX5E_L2_FT_LEVEL,
	MLX5E_TTC_FT_LEVEL,
	MLX5E_ARFS_FT_LEVEL
};

struct mlx5e_ethtool_table {
	struct mlx5_flow_table *ft;
	int                    num_rules;
};

#define ETHTOOL_NUM_L3_L4_FTS 7
#define ETHTOOL_NUM_L2_FTS 4

struct mlx5e_ethtool_steering {
	struct mlx5e_ethtool_table      l3_l4_ft[ETHTOOL_NUM_L3_L4_FTS];
	struct mlx5e_ethtool_table      l2_ft[ETHTOOL_NUM_L2_FTS];
	struct list_head                rules;
	int                             tot_num_rules;
};

struct mlx5e_flow_steering {
	struct mlx5_flow_namespace      *ns;
	struct mlx5e_ethtool_steering   ethtool;
	struct mlx5e_tc_table           tc;
	struct mlx5e_vlan_table         vlan;
	struct mlx5e_l2_table           l2;
	struct mlx5e_ttc_table          ttc;
	struct mlx5e_arfs_tables        arfs;
};

struct mlx5e_rqt {
	u32              rqtn;
	bool		 enabled;
};

struct mlx5e_tir {
	u32		  tirn;
	struct mlx5e_rqt  rqt;
	struct list_head  list;
};

enum {
	MLX5E_TC_PRIO = 0,
	MLX5E_NIC_PRIO
};

struct mlx5e_profile {
	void	(*init)(struct mlx5_core_dev *mdev,
			struct net_device *netdev,
			const struct mlx5e_profile *profile, void *ppriv);
	void	(*cleanup)(struct mlx5e_priv *priv);
	int	(*init_rx)(struct mlx5e_priv *priv);
	void	(*cleanup_rx)(struct mlx5e_priv *priv);
	int	(*init_tx)(struct mlx5e_priv *priv);
	void	(*cleanup_tx)(struct mlx5e_priv *priv);
	void	(*enable)(struct mlx5e_priv *priv);
	void	(*disable)(struct mlx5e_priv *priv);
	void	(*update_stats)(struct mlx5e_priv *priv);
	int	(*max_nch)(struct mlx5_core_dev *mdev);
	int	max_tc;
};

struct mlx5e_priv {
	/* priv data path fields - start */
	struct mlx5e_sq            **txq_to_sq_map;
	int channeltc_to_txq_map[MLX5E_MAX_NUM_CHANNELS][MLX5E_MAX_NUM_TC];
	struct bpf_prog *xdp_prog;
	/* priv data path fields - end */

	unsigned long              state;
	struct mutex               state_lock; /* Protects Interface state */
	struct mlx5_core_mkey      umr_mkey;
	struct mlx5e_rq            drop_rq;

	struct mlx5e_channel     **channel;
	u32                        tisn[MLX5E_MAX_NUM_TC];
	struct mlx5e_rqt           indir_rqt;
	struct mlx5e_tir           indir_tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir           direct_tir[MLX5E_MAX_NUM_CHANNELS];
	u32                        tx_rates[MLX5E_MAX_NUM_SQS];

	struct mlx5e_flow_steering fs;
	struct mlx5e_vxlan_db      vxlan;

	struct mlx5e_params        params;
	struct workqueue_struct    *wq;
	struct work_struct         update_carrier_work;
	struct work_struct         set_rx_mode_work;
	struct work_struct         tx_timeout_work;
	struct delayed_work        update_stats_work;

	u32                        pflags;
	struct mlx5_core_dev      *mdev;
	struct net_device         *netdev;
	struct mlx5e_stats         stats;
	struct mlx5e_tstamp        tstamp;
	u16 q_counter;
	const struct mlx5e_profile *profile;
	void                      *ppriv;
};

void mlx5e_build_ptys2ethtool_map(void);

void mlx5e_send_nop(struct mlx5e_sq *sq, bool notify_hw);
u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback);
netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev);

void mlx5e_completion_event(struct mlx5_core_cq *mcq);
void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event);
int mlx5e_napi_poll(struct napi_struct *napi, int budget);
bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget);
int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget);
void mlx5e_free_sq_descs(struct mlx5e_sq *sq);

void mlx5e_page_release(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info,
			bool recycle);
void mlx5e_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
void mlx5e_handle_rx_cqe_mpwrq(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq);
int mlx5e_alloc_rx_wqe(struct mlx5e_rq *rq, struct mlx5e_rx_wqe *wqe, u16 ix);
int mlx5e_alloc_rx_mpwqe(struct mlx5e_rq *rq, struct mlx5e_rx_wqe *wqe,	u16 ix);
void mlx5e_dealloc_rx_wqe(struct mlx5e_rq *rq, u16 ix);
void mlx5e_dealloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix);
void mlx5e_post_rx_mpwqe(struct mlx5e_rq *rq);
void mlx5e_free_rx_mpwqe(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi);
struct mlx5_cqe64 *mlx5e_get_cqe(struct mlx5e_cq *cq);

void mlx5e_rx_am(struct mlx5e_rq *rq);
void mlx5e_rx_am_work(struct work_struct *work);
struct mlx5e_cq_moder mlx5e_am_get_def_profile(u8 rx_cq_period_mode);

void mlx5e_update_stats(struct mlx5e_priv *priv);

int mlx5e_create_flow_steering(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_steering(struct mlx5e_priv *priv);
void mlx5e_init_l2_addr(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft);
int mlx5e_ethtool_get_flow(struct mlx5e_priv *priv, struct ethtool_rxnfc *info,
			   int location);
int mlx5e_ethtool_get_all_flows(struct mlx5e_priv *priv,
				struct ethtool_rxnfc *info, u32 *rule_locs);
int mlx5e_ethtool_flow_replace(struct mlx5e_priv *priv,
			       struct ethtool_rx_flow_spec *fs);
int mlx5e_ethtool_flow_remove(struct mlx5e_priv *priv,
			      int location);
void mlx5e_ethtool_init_steering(struct mlx5e_priv *priv);
void mlx5e_ethtool_cleanup_steering(struct mlx5e_priv *priv);
void mlx5e_set_rx_mode_work(struct work_struct *work);

void mlx5e_fill_hwstamp(struct mlx5e_tstamp *clock, u64 timestamp,
			struct skb_shared_hwtstamps *hwts);
void mlx5e_timestamp_init(struct mlx5e_priv *priv);
void mlx5e_timestamp_cleanup(struct mlx5e_priv *priv);
int mlx5e_hwstamp_set(struct net_device *dev, struct ifreq *ifr);
int mlx5e_hwstamp_get(struct net_device *dev, struct ifreq *ifr);
void mlx5e_modify_rx_cqe_compression(struct mlx5e_priv *priv, bool val);

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid);
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid);
void mlx5e_enable_vlan_filter(struct mlx5e_priv *priv);
void mlx5e_disable_vlan_filter(struct mlx5e_priv *priv);

int mlx5e_modify_rqs_vsd(struct mlx5e_priv *priv, bool vsd);

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, u32 rqtn, int sz, int ix);
void mlx5e_build_tir_ctx_hash(void *tirc, struct mlx5e_priv *priv);

int mlx5e_open_locked(struct net_device *netdev);
int mlx5e_close_locked(struct net_device *netdev);
void mlx5e_build_default_indir_rqt(struct mlx5_core_dev *mdev,
				   u32 *indirection_rqt, int len,
				   int num_channels);
int mlx5e_get_max_linkspeed(struct mlx5_core_dev *mdev, u32 *speed);

void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params,
				 u8 cq_period_mode);

static inline void mlx5e_tx_notify_hw(struct mlx5e_sq *sq,
				      struct mlx5_wqe_ctrl_seg *ctrl, int bf_sz)
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
		__iowrite64_copy(sq->uar_map + ofst, ctrl, bf_sz);
	else
		mlx5_write64((__be32 *)ctrl, sq->uar_map + ofst, NULL);
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

static inline u32 mlx5e_get_wqe_mtt_offset(struct mlx5e_rq *rq, u16 wqe_ix)
{
	return rq->mpwqe.mtt_offset +
		wqe_ix * ALIGN(MLX5_MPWRQ_PAGES_PER_WQE, 8);
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

#ifndef CONFIG_RFS_ACCEL
static inline int mlx5e_arfs_create_tables(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv) {}

static inline int mlx5e_arfs_enable(struct mlx5e_priv *priv)
{
	return -ENOTSUPP;
}

static inline int mlx5e_arfs_disable(struct mlx5e_priv *priv)
{
	return -ENOTSUPP;
}
#else
int mlx5e_arfs_create_tables(struct mlx5e_priv *priv);
void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv);
int mlx5e_arfs_enable(struct mlx5e_priv *priv);
int mlx5e_arfs_disable(struct mlx5e_priv *priv);
int mlx5e_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			u16 rxq_index, u32 flow_id);
#endif

u16 mlx5e_get_max_inline_cap(struct mlx5_core_dev *mdev);
int mlx5e_create_tir(struct mlx5_core_dev *mdev,
		     struct mlx5e_tir *tir, u32 *in, int inlen);
void mlx5e_destroy_tir(struct mlx5_core_dev *mdev,
		       struct mlx5e_tir *tir);
int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev);
void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev);
int mlx5e_refresh_tirs_self_loopback_enable(struct mlx5_core_dev *mdev);

struct mlx5_eswitch_rep;
int mlx5e_vport_rep_load(struct mlx5_eswitch *esw,
			 struct mlx5_eswitch_rep *rep);
void mlx5e_vport_rep_unload(struct mlx5_eswitch *esw,
			    struct mlx5_eswitch_rep *rep);
int mlx5e_nic_rep_load(struct mlx5_eswitch *esw, struct mlx5_eswitch_rep *rep);
void mlx5e_nic_rep_unload(struct mlx5_eswitch *esw,
			  struct mlx5_eswitch_rep *rep);
int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv);
void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv);
int mlx5e_attr_get(struct net_device *dev, struct switchdev_attr *attr);
void mlx5e_handle_rx_cqe_rep(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv);
void mlx5e_destroy_rqt(struct mlx5e_priv *priv, struct mlx5e_rqt *rqt);
int mlx5e_create_direct_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv);
int mlx5e_create_tises(struct mlx5e_priv *priv);
void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv);
int mlx5e_close(struct net_device *netdev);
int mlx5e_open(struct net_device *netdev);
void mlx5e_update_stats_work(struct work_struct *work);
struct net_device *mlx5e_create_netdev(struct mlx5_core_dev *mdev,
				       const struct mlx5e_profile *profile,
				       void *ppriv);
void mlx5e_destroy_netdev(struct mlx5_core_dev *mdev, struct mlx5e_priv *priv);
int mlx5e_attach_netdev(struct mlx5_core_dev *mdev, struct net_device *netdev);
void mlx5e_detach_netdev(struct mlx5_core_dev *mdev, struct net_device *netdev);
struct rtnl_link_stats64 *
mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats);

#endif /* __MLX5_EN_H__ */
