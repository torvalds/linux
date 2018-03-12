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
#include <linux/crash_dump.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/port.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/transobj.h>
#include <linux/mlx5/fs.h>
#include <linux/rhashtable.h>
#include <net/switchdev.h>
#include <net/xdp.h>
#include <linux/net_dim.h>
#include "wq.h"
#include "mlx5_core.h"
#include "en_stats.h"

#define MLX5_SET_CFG(p, f, v) MLX5_SET(create_flow_group_in, p, f, v)

#define MLX5E_ETH_HARD_MTU (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)

#define MLX5E_HW2SW_MTU(params, hwmtu) ((hwmtu) - ((params)->hard_mtu))
#define MLX5E_SW2HW_MTU(params, swmtu) ((swmtu) + ((params)->hard_mtu))

#define MLX5E_MAX_DSCP          64
#define MLX5E_MAX_NUM_TC	8

#define MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x6
#define MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE                0x1
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW            0x2
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE_MPW            0x3
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE_MPW            0x6

#define MLX5_RX_HEADROOM NET_SKB_PAD
#define MLX5_SKB_FRAG_SZ(len)	(SKB_DATA_ALIGN(len) +	\
				 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#define MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev) \
	(6 + MLX5_CAP_GEN(mdev, cache_line_128byte)) /* HW restriction */
#define MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, req) \
	max_t(u32, MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev), req)
#define MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev)       MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, 6)
#define MLX5_MPWRQ_CQE_CMPRS_LOG_STRIDE_SZ(mdev) MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, 8)
#define MLX5E_MPWQE_STRIDE_SZ(mdev, cqe_cmprs) \
	(cqe_cmprs ? MLX5_MPWRQ_CQE_CMPRS_LOG_STRIDE_SZ(mdev) : \
	MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev))

#define MLX5_MPWRQ_LOG_WQE_SZ			18
#define MLX5_MPWRQ_WQE_PAGE_ORDER  (MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT > 0 ? \
				    MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT : 0)
#define MLX5_MPWRQ_PAGES_PER_WQE		BIT(MLX5_MPWRQ_WQE_PAGE_ORDER)

#define MLX5_MTT_OCTW(npages) (ALIGN(npages, 8) / 2)
#define MLX5E_REQUIRED_MTTS(wqes)		\
	(wqes * ALIGN(MLX5_MPWRQ_PAGES_PER_WQE, 8))
#define MLX5E_VALID_NUM_MTTS(num_mtts) (MLX5_MTT_OCTW(num_mtts) - 1 <= U16_MAX)

#define MLX5_UMR_ALIGN				(2048)
#define MLX5_MPWRQ_SMALL_PACKET_THRESHOLD	(256)

#define MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ                 (64 * 1024)
#define MLX5E_DEFAULT_LRO_TIMEOUT                       32
#define MLX5E_LRO_TIMEOUT_ARR_SIZE                      4

#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE 0x3
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC_FROM_CQE 0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES                0x80
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES_MPW            0x2

#define MLX5E_LOG_INDIR_RQT_SIZE       0x7
#define MLX5E_INDIR_RQT_SIZE           BIT(MLX5E_LOG_INDIR_RQT_SIZE)
#define MLX5E_MIN_NUM_CHANNELS         0x1
#define MLX5E_MAX_NUM_CHANNELS         (MLX5E_INDIR_RQT_SIZE >> 1)
#define MLX5E_MAX_NUM_SQS              (MLX5E_MAX_NUM_CHANNELS * MLX5E_MAX_NUM_TC)
#define MLX5E_TX_CQ_POLL_BUDGET        128
#define MLX5E_UPDATE_STATS_INTERVAL    200 /* msecs */
#define MLX5E_SQ_RECOVER_MIN_INTERVAL  500 /* msecs */

#define MLX5E_ICOSQ_MAX_WQEBBS \
	(DIV_ROUND_UP(sizeof(struct mlx5e_umr_wqe), MLX5_SEND_WQE_BB))

#define MLX5E_XDP_MIN_INLINE (ETH_HLEN + VLAN_HLEN)
#define MLX5E_XDP_TX_DS_COUNT \
	((sizeof(struct mlx5e_tx_wqe) / MLX5_SEND_WQE_DS) + 1 /* SG DS */)

#define MLX5E_NUM_MAIN_GROUPS 9

#define MLX5E_MSG_LEVEL			NETIF_MSG_LINK

#define mlx5e_dbg(mlevel, priv, format, ...)                    \
do {                                                            \
	if (NETIF_MSG_##mlevel & (priv)->msglevel)              \
		netdev_warn(priv->netdev, format,               \
			    ##__VA_ARGS__);                     \
} while (0)


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

static inline int mlx5e_get_max_num_channels(struct mlx5_core_dev *mdev)
{
	return is_kdump_kernel() ?
		MLX5E_MIN_NUM_CHANNELS :
		min_t(int, mdev->priv.eq_table.num_comp_vectors,
		      MLX5E_MAX_NUM_CHANNELS);
}

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

extern const char mlx5e_self_tests[][ETH_GSTRING_LEN];

static const char mlx5e_priv_flags[][ETH_GSTRING_LEN] = {
	"rx_cqe_moder",
	"tx_cqe_moder",
	"rx_cqe_compress",
	"rx_striding_rq",
};

enum mlx5e_priv_flag {
	MLX5E_PFLAG_RX_CQE_BASED_MODER = (1 << 0),
	MLX5E_PFLAG_TX_CQE_BASED_MODER = (1 << 1),
	MLX5E_PFLAG_RX_CQE_COMPRESS = (1 << 2),
	MLX5E_PFLAG_RX_STRIDING_RQ = (1 << 3),
};

#define MLX5E_SET_PFLAG(params, pflag, enable)			\
	do {							\
		if (enable)					\
			(params)->pflags |= (pflag);		\
		else						\
			(params)->pflags &= ~(pflag);		\
	} while (0)

#define MLX5E_GET_PFLAG(params, pflag) (!!((params)->pflags & (pflag)))

#ifdef CONFIG_MLX5_CORE_EN_DCB
#define MLX5E_MAX_BW_ALLOC 100 /* Max percentage of BW allocation */
#endif

struct mlx5e_params {
	u8  log_sq_size;
	u8  rq_wq_type;
	u8  log_rq_size;
	u16 num_channels;
	u8  num_tc;
	bool rx_cqe_compress_def;
	struct net_dim_cq_moder rx_cq_moderation;
	struct net_dim_cq_moder tx_cq_moderation;
	bool lro_en;
	u32 lro_wqe_sz;
	u8  tx_min_inline_mode;
	u8  rss_hfunc;
	u8  toeplitz_hash_key[40];
	u32 indirection_rqt[MLX5E_INDIR_RQT_SIZE];
	bool vlan_strip_disable;
	bool scatter_fcs_en;
	bool rx_dim_enabled;
	u32 lro_timeout;
	u32 pflags;
	struct bpf_prog *xdp_prog;
	unsigned int sw_mtu;
	int hard_mtu;
};

#ifdef CONFIG_MLX5_CORE_EN_DCB
struct mlx5e_cee_config {
	/* bw pct for priority group */
	u8                         pg_bw_pct[CEE_DCBX_MAX_PGS];
	u8                         prio_to_pg_map[CEE_DCBX_MAX_PRIO];
	bool                       pfc_setting[CEE_DCBX_MAX_PRIO];
	bool                       pfc_enable;
};

enum {
	MLX5_DCB_CHG_RESET,
	MLX5_DCB_NO_CHG,
	MLX5_DCB_CHG_NO_RESET,
};

struct mlx5e_dcbx {
	enum mlx5_dcbx_oper_mode   mode;
	struct mlx5e_cee_config    cee_cfg; /* pending configuration */
	u8                         dscp_app_cnt;

	/* The only setting that cannot be read from FW */
	u8                         tc_tsa[IEEE_8021QAZ_MAX_TCS];
	u8                         cap;
};

struct mlx5e_dcbx_dp {
	u8                         dscp2prio[MLX5E_MAX_DSCP];
	u8                         trust_state;
};
#endif

enum {
	MLX5E_RQ_STATE_ENABLED,
	MLX5E_RQ_STATE_AM,
};

#define MLX5E_TEST_BIT(state, nr) (state & BIT(nr))

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	u16                        event_ctr;
	struct napi_struct        *napi;
	struct mlx5_core_cq        mcq;
	struct mlx5e_channel      *channel;

	/* cqe decompression */
	struct mlx5_cqe64          title;
	struct mlx5_mini_cqe8      mini_arr[MLX5_MINI_CQE_ARRAY_SIZE];
	u8                         mini_arr_idx;
	u16                        decmprs_left;
	u16                        decmprs_wqe_counter;

	/* control */
	struct mlx5_core_dev      *mdev;
	struct mlx5_frag_wq_ctrl   wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5e_tx_wqe_info {
	struct sk_buff *skb;
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
	MLX5E_SQ_STATE_ENABLED,
	MLX5E_SQ_STATE_RECOVERING,
	MLX5E_SQ_STATE_IPSEC,
};

struct mlx5e_sq_wqe_info {
	u8  opcode;
};

struct mlx5e_txqsq {
	/* data path */

	/* dirtied @completion */
	u16                        cc;
	u32                        dma_fifo_cc;

	/* dirtied @xmit */
	u16                        pc ____cacheline_aligned_in_smp;
	u32                        dma_fifo_pc;
	struct mlx5e_sq_stats      stats;

	struct mlx5e_cq            cq;

	/* write@xmit, read@completion */
	struct {
		struct mlx5e_sq_dma       *dma_fifo;
		struct mlx5e_tx_wqe_info  *wqe_info;
	} db;

	/* read only */
	struct mlx5_wq_cyc         wq;
	u32                        dma_fifo_mask;
	void __iomem              *uar_map;
	struct netdev_queue       *txq;
	u32                        sqn;
	u8                         min_inline_mode;
	u16                        edge;
	struct device             *pdev;
	__be32                     mkey_be;
	unsigned long              state;
	struct hwtstamp_config    *tstamp;
	struct mlx5_clock         *clock;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;
	int                        txq_ix;
	u32                        rate_limit;
	struct mlx5e_txqsq_recover {
		struct work_struct         recover_work;
		u64                        last_recover;
	} recover;
} ____cacheline_aligned_in_smp;

struct mlx5e_xdpsq {
	/* data path */

	/* dirtied @rx completion */
	u16                        cc;
	u16                        pc;

	struct mlx5e_cq            cq;

	/* write@xmit, read@completion */
	struct {
		struct mlx5e_dma_info     *di;
		bool                       doorbell;
	} db;

	/* read only */
	struct mlx5_wq_cyc         wq;
	void __iomem              *uar_map;
	u32                        sqn;
	struct device             *pdev;
	__be32                     mkey_be;
	u8                         min_inline_mode;
	unsigned long              state;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;
} ____cacheline_aligned_in_smp;

struct mlx5e_icosq {
	/* data path */

	/* dirtied @xmit */
	u16                        pc ____cacheline_aligned_in_smp;

	struct mlx5e_cq            cq;

	/* write@xmit, read@completion */
	struct {
		struct mlx5e_sq_wqe_info *ico_wqe;
	} db;

	/* read only */
	struct mlx5_wq_cyc         wq;
	void __iomem              *uar_map;
	u32                        sqn;
	u16                        edge;
	__be32                     mkey_be;
	unsigned long              state;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;
} ____cacheline_aligned_in_smp;

static inline bool
mlx5e_wqc_has_room_for(struct mlx5_wq_cyc *wq, u16 cc, u16 pc, u16 n)
{
	return (((wq->sz_m1 & (cc - pc)) >= n) || (cc == pc));
}

struct mlx5e_dma_info {
	struct page	*page;
	dma_addr_t	addr;
};

struct mlx5e_wqe_frag_info {
	struct mlx5e_dma_info di;
	u32 offset;
};

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

/* a single cache unit is capable to serve one napi call (for non-striding rq)
 * or a MPWQE (for striding rq).
 */
#define MLX5E_CACHE_UNIT	(MLX5_MPWRQ_PAGES_PER_WQE > NAPI_POLL_WEIGHT ? \
				 MLX5_MPWRQ_PAGES_PER_WQE : NAPI_POLL_WEIGHT)
#define MLX5E_CACHE_SIZE	(4 * roundup_pow_of_two(MLX5E_CACHE_UNIT))
struct mlx5e_page_cache {
	u32 head;
	u32 tail;
	struct mlx5e_dma_info page_cache[MLX5E_CACHE_SIZE];
};

struct mlx5e_rq;
typedef void (*mlx5e_fp_handle_rx_cqe)(struct mlx5e_rq*, struct mlx5_cqe64*);
typedef bool (*mlx5e_fp_post_rx_wqes)(struct mlx5e_rq *rq);
typedef void (*mlx5e_fp_dealloc_wqe)(struct mlx5e_rq*, u16);

struct mlx5e_rq {
	/* data path */
	struct mlx5_wq_ll      wq;

	union {
		struct {
			struct mlx5e_wqe_frag_info *frag_info;
			u32 frag_sz;	/* max possible skb frag_sz */
			union {
				bool page_reuse;
				bool xdp_xmit;
			};
		} wqe;
		struct {
			struct mlx5e_mpw_info *info;
			void                  *mtt_no_align;
			u16                    num_strides;
			u8                     log_stride_sz;
			bool                   umr_in_progress;
		} mpwqe;
	};
	struct {
		u16            headroom;
		u8             page_order;
		u8             map_dir;   /* dma map direction */
	} buff;

	struct mlx5e_channel  *channel;
	struct device         *pdev;
	struct net_device     *netdev;
	struct mlx5e_rq_stats  stats;
	struct mlx5e_cq        cq;
	struct mlx5e_page_cache page_cache;
	struct hwtstamp_config *tstamp;
	struct mlx5_clock      *clock;

	mlx5e_fp_handle_rx_cqe handle_rx_cqe;
	mlx5e_fp_post_rx_wqes  post_wqes;
	mlx5e_fp_dealloc_wqe   dealloc_wqe;

	unsigned long          state;
	int                    ix;

	struct net_dim         dim; /* Dynamic Interrupt Moderation */

	/* XDP */
	struct bpf_prog       *xdp_prog;
	unsigned int           hw_mtu;
	struct mlx5e_xdpsq     xdpsq;

	/* control */
	struct mlx5_wq_ctrl    wq_ctrl;
	__be32                 mkey_be;
	u8                     wq_type;
	u32                    rqn;
	struct mlx5_core_dev  *mdev;
	struct mlx5_core_mkey  umr_mkey;

	/* XDP read-mostly */
	struct xdp_rxq_info    xdp_rxq;
} ____cacheline_aligned_in_smp;

struct mlx5e_channel {
	/* data path */
	struct mlx5e_rq            rq;
	struct mlx5e_txqsq         sq[MLX5E_MAX_NUM_TC];
	struct mlx5e_icosq         icosq;   /* internal control operations */
	bool                       xdp;
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;
	u8                         num_tc;

	/* data path - accessed per napi poll */
	struct irq_desc *irq_desc;
	struct mlx5e_ch_stats      stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	int                        ix;
	int                        cpu;
};

struct mlx5e_channels {
	struct mlx5e_channel **c;
	unsigned int           num;
	struct mlx5e_params    params;
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

enum mlx5e_tunnel_types {
	MLX5E_TT_IPV4_GRE,
	MLX5E_TT_IPV6_GRE,
	MLX5E_NUM_TUNNEL_TT,
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
	struct mlx5_flow_handle *rule;
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

	DECLARE_HASHTABLE(mod_hdr_tbl, 8);
	DECLARE_HASHTABLE(hairpin_tbl, 8);
};

struct mlx5e_vlan_table {
	struct mlx5e_flow_table		ft;
	DECLARE_BITMAP(active_cvlans, VLAN_N_VID);
	DECLARE_BITMAP(active_svlans, VLAN_N_VID);
	struct mlx5_flow_handle	*active_cvlans_rule[VLAN_N_VID];
	struct mlx5_flow_handle	*active_svlans_rule[VLAN_N_VID];
	struct mlx5_flow_handle	*untagged_rule;
	struct mlx5_flow_handle	*any_cvlan_rule;
	struct mlx5_flow_handle	*any_svlan_rule;
	bool			cvlan_filter_disabled;
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
	struct mlx5_flow_handle	 *rules[MLX5E_NUM_TT];
	struct mlx5_flow_handle  *tunnel_rules[MLX5E_NUM_TUNNEL_TT];
};

#define ARFS_HASH_SHIFT BITS_PER_BYTE
#define ARFS_HASH_SIZE BIT(BITS_PER_BYTE)
struct arfs_table {
	struct mlx5e_flow_table  ft;
	struct mlx5_flow_handle	 *default_rule;
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
	MLX5E_INNER_TTC_FT_LEVEL,
	MLX5E_ARFS_FT_LEVEL
};

enum {
	MLX5E_TC_FT_LEVEL = 0,
	MLX5E_TC_TTC_FT_LEVEL,
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
	struct mlx5e_ttc_table          inner_ttc;
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

struct mlx5e_priv {
	/* priv data path fields - start */
	struct mlx5e_txqsq *txq2sq[MLX5E_MAX_NUM_CHANNELS * MLX5E_MAX_NUM_TC];
	int channel_tc2txq[MLX5E_MAX_NUM_CHANNELS][MLX5E_MAX_NUM_TC];
#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct mlx5e_dcbx_dp       dcbx_dp;
#endif
	/* priv data path fields - end */

	u32                        msglevel;
	unsigned long              state;
	struct mutex               state_lock; /* Protects Interface state */
	struct mlx5e_rq            drop_rq;

	struct mlx5e_channels      channels;
	u32                        tisn[MLX5E_MAX_NUM_TC];
	struct mlx5e_rqt           indir_rqt;
	struct mlx5e_tir           indir_tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir           inner_indir_tir[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_tir           direct_tir[MLX5E_MAX_NUM_CHANNELS];
	u32                        tx_rates[MLX5E_MAX_NUM_SQS];

	struct mlx5e_flow_steering fs;
	struct mlx5e_vxlan_db      vxlan;

	struct workqueue_struct    *wq;
	struct work_struct         update_carrier_work;
	struct work_struct         set_rx_mode_work;
	struct work_struct         tx_timeout_work;
	struct delayed_work        update_stats_work;

	struct mlx5_core_dev      *mdev;
	struct net_device         *netdev;
	struct mlx5e_stats         stats;
	struct hwtstamp_config     tstamp;
	u16                        q_counter;
	u16                        drop_rq_q_counter;
#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct mlx5e_dcbx          dcbx;
#endif

	const struct mlx5e_profile *profile;
	void                      *ppriv;
#ifdef CONFIG_MLX5_EN_IPSEC
	struct mlx5e_ipsec        *ipsec;
#endif
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
	void	(*update_carrier)(struct mlx5e_priv *priv);
	int	(*max_nch)(struct mlx5_core_dev *mdev);
	struct {
		mlx5e_fp_handle_rx_cqe handle_rx_cqe;
		mlx5e_fp_handle_rx_cqe handle_rx_cqe_mpwqe;
	} rx_handlers;
	void	(*netdev_registered_init)(struct mlx5e_priv *priv);
	void    (*netdev_registered_remove)(struct mlx5e_priv *priv);
	int	max_tc;
};

void mlx5e_build_ptys2ethtool_map(void);

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback);
netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev);

void mlx5e_completion_event(struct mlx5_core_cq *mcq);
void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event);
int mlx5e_napi_poll(struct napi_struct *napi, int budget);
bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget);
int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget);
bool mlx5e_poll_xdpsq_cq(struct mlx5e_cq *cq);
void mlx5e_free_txqsq_descs(struct mlx5e_txqsq *sq);
void mlx5e_free_xdpsq_descs(struct mlx5e_xdpsq *sq);

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev);
bool mlx5e_striding_rq_possible(struct mlx5_core_dev *mdev,
				struct mlx5e_params *params);

void mlx5e_page_release(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info,
			bool recycle);
void mlx5e_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
void mlx5e_handle_rx_cqe_mpwrq(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq);
bool mlx5e_post_rx_mpwqes(struct mlx5e_rq *rq);
void mlx5e_dealloc_rx_wqe(struct mlx5e_rq *rq, u16 ix);
void mlx5e_dealloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix);
void mlx5e_free_rx_mpwqe(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi);

u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);

void mlx5e_update_stats(struct mlx5e_priv *priv);

int mlx5e_create_flow_steering(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_steering(struct mlx5e_priv *priv);
void mlx5e_init_l2_addr(struct mlx5e_priv *priv);
void mlx5e_destroy_flow_table(struct mlx5e_flow_table *ft);
int mlx5e_self_test_num(struct mlx5e_priv *priv);
void mlx5e_self_test(struct net_device *ndev, struct ethtool_test *etest,
		     u64 *buf);
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

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_hwstamp_get(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_modify_rx_cqe_compression_locked(struct mlx5e_priv *priv, bool val);

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid);
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid);
void mlx5e_enable_cvlan_filter(struct mlx5e_priv *priv);
void mlx5e_disable_cvlan_filter(struct mlx5e_priv *priv);
void mlx5e_timestamp_init(struct mlx5e_priv *priv);

struct mlx5e_redirect_rqt_param {
	bool is_rss;
	union {
		u32 rqn; /* Direct RQN (Non-RSS) */
		struct {
			u8 hfunc;
			struct mlx5e_channels *channels;
		} rss; /* RSS data */
	};
};

int mlx5e_redirect_rqt(struct mlx5e_priv *priv, u32 rqtn, int sz,
		       struct mlx5e_redirect_rqt_param rrp);
void mlx5e_build_indir_tir_ctx_hash(struct mlx5e_params *params,
				    enum mlx5e_traffic_types tt,
				    void *tirc, bool inner);

int mlx5e_open_locked(struct net_device *netdev);
int mlx5e_close_locked(struct net_device *netdev);

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs);
void mlx5e_close_channels(struct mlx5e_channels *chs);

/* Function pointer to be used to modify WH settings while
 * switching channels
 */
typedef int (*mlx5e_fp_hw_modify)(struct mlx5e_priv *priv);
void mlx5e_switch_priv_channels(struct mlx5e_priv *priv,
				struct mlx5e_channels *new_chs,
				mlx5e_fp_hw_modify hw_modify);
void mlx5e_activate_priv_channels(struct mlx5e_priv *priv);
void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv);

void mlx5e_build_default_indir_rqt(u32 *indirection_rqt, int len,
				   int num_channels);
int mlx5e_get_max_linkspeed(struct mlx5_core_dev *mdev, u32 *speed);

void mlx5e_set_tx_cq_mode_params(struct mlx5e_params *params,
				 u8 cq_period_mode);
void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params,
				 u8 cq_period_mode);
void mlx5e_set_rq_type(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params);

static inline bool mlx5e_tunnel_inner_ft_supported(struct mlx5_core_dev *mdev)
{
	return (MLX5_CAP_ETH(mdev, tunnel_stateless_gre) &&
		MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ft_field_support.inner_ip_version));
}

static inline
struct mlx5e_tx_wqe *mlx5e_post_nop(struct mlx5_wq_cyc *wq, u32 sqn, u16 *pc)
{
	u16                         pi   = *pc & wq->sz_m1;
	struct mlx5e_tx_wqe        *wqe  = mlx5_wq_cyc_get_wqe(wq, pi);
	struct mlx5_wqe_ctrl_seg   *cseg = &wqe->ctrl;

	memset(cseg, 0, sizeof(*cseg));

	cseg->opmod_idx_opcode = cpu_to_be32((*pc << 8) | MLX5_OPCODE_NOP);
	cseg->qpn_ds           = cpu_to_be32((sqn << 8) | 0x01);

	(*pc)++;

	return wqe;
}

static inline
void mlx5e_notify_hw(struct mlx5_wq_cyc *wq, u16 pc,
		     void __iomem *uar_map,
		     struct mlx5_wqe_ctrl_seg *ctrl)
{
	ctrl->fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	*wq->db = cpu_to_be32(pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	mlx5_write64((__be32 *)ctrl, uar_map, NULL);
}

static inline void mlx5e_cq_arm(struct mlx5e_cq *cq)
{
	struct mlx5_core_cq *mcq;

	mcq = &cq->mcq;
	mlx5_cq_arm(mcq, MLX5_CQ_DB_REQ_NOT, mcq->uar->map, cq->wq.cc);
}

static inline u32 mlx5e_get_wqe_mtt_offset(struct mlx5e_rq *rq, u16 wqe_ix)
{
	return wqe_ix * ALIGN(MLX5_MPWRQ_PAGES_PER_WQE, 8);
}

extern const struct ethtool_ops mlx5e_ethtool_ops;
#ifdef CONFIG_MLX5_CORE_EN_DCB
extern const struct dcbnl_rtnl_ops mlx5e_dcbnl_ops;
int mlx5e_dcbnl_ieee_setets_core(struct mlx5e_priv *priv, struct ieee_ets *ets);
void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv);
void mlx5e_dcbnl_init_app(struct mlx5e_priv *priv);
void mlx5e_dcbnl_delete_app(struct mlx5e_priv *priv);
#endif

#ifndef CONFIG_RFS_ACCEL
static inline int mlx5e_arfs_create_tables(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv) {}

static inline int mlx5e_arfs_enable(struct mlx5e_priv *priv)
{
	return -EOPNOTSUPP;
}

static inline int mlx5e_arfs_disable(struct mlx5e_priv *priv)
{
	return -EOPNOTSUPP;
}
#else
int mlx5e_arfs_create_tables(struct mlx5e_priv *priv);
void mlx5e_arfs_destroy_tables(struct mlx5e_priv *priv);
int mlx5e_arfs_enable(struct mlx5e_priv *priv);
int mlx5e_arfs_disable(struct mlx5e_priv *priv);
int mlx5e_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
			u16 rxq_index, u32 flow_id);
#endif

int mlx5e_create_tir(struct mlx5_core_dev *mdev,
		     struct mlx5e_tir *tir, u32 *in, int inlen);
void mlx5e_destroy_tir(struct mlx5_core_dev *mdev,
		       struct mlx5e_tir *tir);
int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev);
void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev);
int mlx5e_refresh_tirs(struct mlx5e_priv *priv, bool enable_uc_lb);

/* common netdev helpers */
int mlx5e_create_indirect_rqt(struct mlx5e_priv *priv);

int mlx5e_create_indirect_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_indirect_tirs(struct mlx5e_priv *priv);

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv);
void mlx5e_destroy_direct_rqts(struct mlx5e_priv *priv);
int mlx5e_create_direct_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_rqt(struct mlx5e_priv *priv, struct mlx5e_rqt *rqt);

struct ttc_params {
	struct mlx5_flow_table_attr ft_attr;
	u32 any_tt_tirn;
	u32 indir_tirn[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_ttc_table *inner_ttc;
};

void mlx5e_set_ttc_basic_params(struct mlx5e_priv *priv, struct ttc_params *ttc_params);
void mlx5e_set_ttc_ft_params(struct ttc_params *ttc_params);
void mlx5e_set_inner_ttc_ft_params(struct ttc_params *ttc_params);

int mlx5e_create_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
			   struct mlx5e_ttc_table *ttc);
void mlx5e_destroy_ttc_table(struct mlx5e_priv *priv,
			     struct mlx5e_ttc_table *ttc);

int mlx5e_create_inner_ttc_table(struct mlx5e_priv *priv, struct ttc_params *params,
				 struct mlx5e_ttc_table *ttc);
void mlx5e_destroy_inner_ttc_table(struct mlx5e_priv *priv,
				   struct mlx5e_ttc_table *ttc);

int mlx5e_create_tis(struct mlx5_core_dev *mdev, int tc,
		     u32 underlay_qpn, u32 *tisn);
void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn);

int mlx5e_create_tises(struct mlx5e_priv *priv);
void mlx5e_cleanup_nic_tx(struct mlx5e_priv *priv);
int mlx5e_close(struct net_device *netdev);
int mlx5e_open(struct net_device *netdev);
void mlx5e_update_stats_work(struct work_struct *work);

int mlx5e_bits_invert(unsigned long a, int size);

/* ethtool helpers */
void mlx5e_ethtool_get_drvinfo(struct mlx5e_priv *priv,
			       struct ethtool_drvinfo *drvinfo);
void mlx5e_ethtool_get_strings(struct mlx5e_priv *priv,
			       uint32_t stringset, uint8_t *data);
int mlx5e_ethtool_get_sset_count(struct mlx5e_priv *priv, int sset);
void mlx5e_ethtool_get_ethtool_stats(struct mlx5e_priv *priv,
				     struct ethtool_stats *stats, u64 *data);
void mlx5e_ethtool_get_ringparam(struct mlx5e_priv *priv,
				 struct ethtool_ringparam *param);
int mlx5e_ethtool_set_ringparam(struct mlx5e_priv *priv,
				struct ethtool_ringparam *param);
void mlx5e_ethtool_get_channels(struct mlx5e_priv *priv,
				struct ethtool_channels *ch);
int mlx5e_ethtool_set_channels(struct mlx5e_priv *priv,
			       struct ethtool_channels *ch);
int mlx5e_ethtool_get_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal);
int mlx5e_ethtool_set_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal);
int mlx5e_ethtool_get_ts_info(struct mlx5e_priv *priv,
			      struct ethtool_ts_info *info);
int mlx5e_ethtool_flash_device(struct mlx5e_priv *priv,
			       struct ethtool_flash *flash);

int mlx5e_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			    void *cb_priv);

/* mlx5e generic netdev management API */
struct net_device*
mlx5e_create_netdev(struct mlx5_core_dev *mdev, const struct mlx5e_profile *profile,
		    void *ppriv);
int mlx5e_attach_netdev(struct mlx5e_priv *priv);
void mlx5e_detach_netdev(struct mlx5e_priv *priv);
void mlx5e_destroy_netdev(struct mlx5e_priv *priv);
void mlx5e_build_nic_params(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    u16 max_channels, u16 mtu);
u8 mlx5e_params_calculate_tx_min_inline(struct mlx5_core_dev *mdev);
void mlx5e_rx_dim_work(struct work_struct *work);
#endif /* __MLX5_EN_H__ */
