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
#include <linux/bits.h>
#include "wq.h"
#include "mlx5_core.h"
#include "en_stats.h"
#include "en/fs.h"

extern const struct net_device_ops mlx5e_netdev_ops;
struct page_pool;

#define MLX5E_METADATA_ETHER_TYPE (0x8CE4)
#define MLX5E_METADATA_ETHER_LEN 8

#define MLX5_SET_CFG(p, f, v) MLX5_SET(create_flow_group_in, p, f, v)

#define MLX5E_ETH_HARD_MTU (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)

#define MLX5E_HW2SW_MTU(params, hwmtu) ((hwmtu) - ((params)->hard_mtu))
#define MLX5E_SW2HW_MTU(params, swmtu) ((swmtu) + ((params)->hard_mtu))

#define MLX5E_MAX_PRIORITY      8
#define MLX5E_MAX_DSCP          64
#define MLX5E_MAX_NUM_TC	8

#define MLX5_RX_HEADROOM NET_SKB_PAD
#define MLX5_SKB_FRAG_SZ(len)	(SKB_DATA_ALIGN(len) +	\
				 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#define MLX5E_RX_MAX_HEAD (256)

#define MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev) \
	(6 + MLX5_CAP_GEN(mdev, cache_line_128byte)) /* HW restriction */
#define MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, req) \
	max_t(u32, MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev), req)
#define MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev) \
	MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, order_base_2(MLX5E_RX_MAX_HEAD))

#define MLX5_MPWRQ_LOG_WQE_SZ			18
#define MLX5_MPWRQ_WQE_PAGE_ORDER  (MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT > 0 ? \
				    MLX5_MPWRQ_LOG_WQE_SZ - PAGE_SHIFT : 0)
#define MLX5_MPWRQ_PAGES_PER_WQE		BIT(MLX5_MPWRQ_WQE_PAGE_ORDER)

#define MLX5_MTT_OCTW(npages) (ALIGN(npages, 8) / 2)
#define MLX5E_REQUIRED_WQE_MTTS		(ALIGN(MLX5_MPWRQ_PAGES_PER_WQE, 8))
#define MLX5E_LOG_ALIGNED_MPWQE_PPW	(ilog2(MLX5E_REQUIRED_WQE_MTTS))
#define MLX5E_REQUIRED_MTTS(wqes)	(wqes * MLX5E_REQUIRED_WQE_MTTS)
#define MLX5E_MAX_RQ_NUM_MTTS	\
	((1 << 16) * 2) /* So that MLX5_MTT_OCTW(num_mtts) fits into u16 */
#define MLX5E_ORDER2_MAX_PACKET_MTU (order_base_2(10 * 1024))
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE_MPW	\
		(ilog2(MLX5E_MAX_RQ_NUM_MTTS / MLX5E_REQUIRED_WQE_MTTS))
#define MLX5E_LOG_MAX_RQ_NUM_PACKETS_MPW \
	(MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE_MPW + \
	 (MLX5_MPWRQ_LOG_WQE_SZ - MLX5E_ORDER2_MAX_PACKET_MTU))

#define MLX5E_MIN_SKB_FRAG_SZ		(MLX5_SKB_FRAG_SZ(MLX5_RX_HEADROOM))
#define MLX5E_LOG_MAX_RX_WQE_BULK	\
	(ilog2(PAGE_SIZE / roundup_pow_of_two(MLX5E_MIN_SKB_FRAG_SZ)))

#define MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x6
#define MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE (1 + MLX5E_LOG_MAX_RX_WQE_BULK)
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE min_t(u8, 0xd,	\
					       MLX5E_LOG_MAX_RQ_NUM_PACKETS_MPW)

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW            0x2

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
#define MLX5E_SQ_RECOVER_MIN_INTERVAL  500 /* msecs */

#define MLX5E_UMR_WQE_INLINE_SZ \
	(sizeof(struct mlx5e_umr_wqe) + \
	 ALIGN(MLX5_MPWRQ_PAGES_PER_WQE * sizeof(struct mlx5_mtt), \
	       MLX5_UMR_MTT_ALIGNMENT))
#define MLX5E_UMR_WQEBBS \
	(DIV_ROUND_UP(MLX5E_UMR_WQE_INLINE_SZ, MLX5_SEND_WQE_BB))

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

/* Use this function to get max num channels (rxqs/txqs) only to create netdev */
static inline int mlx5e_get_max_num_channels(struct mlx5_core_dev *mdev)
{
	return is_kdump_kernel() ?
		MLX5E_MIN_NUM_CHANNELS :
		min_t(int, mlx5_comp_vectors_count(mdev), MLX5E_MAX_NUM_CHANNELS);
}

/* Use this function to get max num channels after netdev was created */
static inline int mlx5e_get_netdev_max_channels(struct net_device *netdev)
{
	return min_t(unsigned int, netdev->num_rx_queues,
		     netdev->num_tx_queues);
}

struct mlx5e_tx_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_eth_seg  eth;
	struct mlx5_wqe_data_seg data[0];
};

struct mlx5e_rx_wqe_ll {
	struct mlx5_wqe_srq_next_seg  next;
	struct mlx5_wqe_data_seg      data[0];
};

struct mlx5e_rx_wqe_cyc {
	struct mlx5_wqe_data_seg      data[0];
};

struct mlx5e_umr_wqe {
	struct mlx5_wqe_ctrl_seg       ctrl;
	struct mlx5_wqe_umr_ctrl_seg   uctrl;
	struct mlx5_mkey_seg           mkc;
	struct mlx5_mtt                inline_mtts[0];
};

extern const char mlx5e_self_tests[][ETH_GSTRING_LEN];

enum mlx5e_priv_flag {
	MLX5E_PFLAG_RX_CQE_BASED_MODER,
	MLX5E_PFLAG_TX_CQE_BASED_MODER,
	MLX5E_PFLAG_RX_CQE_COMPRESS,
	MLX5E_PFLAG_RX_STRIDING_RQ,
	MLX5E_PFLAG_RX_NO_CSUM_COMPLETE,
	MLX5E_PFLAG_XDP_TX_MPWQE,
	MLX5E_NUM_PFLAGS, /* Keep last */
};

#define MLX5E_SET_PFLAG(params, pflag, enable)			\
	do {							\
		if (enable)					\
			(params)->pflags |= BIT(pflag);		\
		else						\
			(params)->pflags &= ~(BIT(pflag));	\
	} while (0)

#define MLX5E_GET_PFLAG(params, pflag) (!!((params)->pflags & (BIT(pflag))))

#ifdef CONFIG_MLX5_CORE_EN_DCB
#define MLX5E_MAX_BW_ALLOC 100 /* Max percentage of BW allocation */
#endif

struct mlx5e_params {
	u8  log_sq_size;
	u8  rq_wq_type;
	u8  log_rq_mtu_frames;
	u16 num_channels;
	u8  num_tc;
	bool rx_cqe_compress_def;
	struct net_dim_cq_moder rx_cq_moderation;
	struct net_dim_cq_moder tx_cq_moderation;
	bool lro_en;
	u32 lro_wqe_sz;
	u8  tx_min_inline_mode;
	bool vlan_strip_disable;
	bool scatter_fcs_en;
	bool rx_dim_enabled;
	bool tx_dim_enabled;
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

	/* Buffer configuration */
	bool                       manual_buffer;
	u32                        cable_len;
	u32                        xoff;
};

struct mlx5e_dcbx_dp {
	u8                         dscp2prio[MLX5E_MAX_DSCP];
	u8                         trust_state;
};
#endif

enum {
	MLX5E_RQ_STATE_ENABLED,
	MLX5E_RQ_STATE_AM,
	MLX5E_RQ_STATE_NO_CSUM_COMPLETE,
};

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	u16                        event_ctr;
	struct napi_struct        *napi;
	struct mlx5_core_cq        mcq;
	struct mlx5e_channel      *channel;

	/* control */
	struct mlx5_core_dev      *mdev;
	struct mlx5_wq_ctrl        wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5e_cq_decomp {
	/* cqe decompression */
	struct mlx5_cqe64          title;
	struct mlx5_mini_cqe8      mini_arr[MLX5_MINI_CQE_ARRAY_SIZE];
	u8                         mini_arr_idx;
	u16                        left;
	u16                        wqe_counter;
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
	MLX5E_SQ_STATE_AM,
	MLX5E_SQ_STATE_TLS,
};

struct mlx5e_sq_wqe_info {
	u8  opcode;
};

struct mlx5e_txqsq {
	/* data path */

	/* dirtied @completion */
	u16                        cc;
	u32                        dma_fifo_cc;
	struct net_dim             dim; /* Adaptive Moderation */

	/* dirtied @xmit */
	u16                        pc ____cacheline_aligned_in_smp;
	u32                        dma_fifo_pc;

	struct mlx5e_cq            cq;

	/* read only */
	struct mlx5_wq_cyc         wq;
	u32                        dma_fifo_mask;
	struct mlx5e_sq_stats     *stats;
	struct {
		struct mlx5e_sq_dma       *dma_fifo;
		struct mlx5e_tx_wqe_info  *wqe_info;
	} db;
	void __iomem              *uar_map;
	struct netdev_queue       *txq;
	u32                        sqn;
	u8                         min_inline_mode;
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
	struct work_struct         recover_work;
} ____cacheline_aligned_in_smp;

struct mlx5e_dma_info {
	struct page     *page;
	dma_addr_t      addr;
};

struct mlx5e_xdp_info {
	struct xdp_frame      *xdpf;
	dma_addr_t            dma_addr;
	struct mlx5e_dma_info di;
};

struct mlx5e_xdp_info_fifo {
	struct mlx5e_xdp_info *xi;
	u32 *cc;
	u32 *pc;
	u32 mask;
};

struct mlx5e_xdp_wqe_info {
	u8 num_wqebbs;
	u8 num_ds;
};

struct mlx5e_xdp_mpwqe {
	/* Current MPWQE session */
	struct mlx5e_tx_wqe *wqe;
	u8                   ds_count;
	u8                   max_ds_count;
};

struct mlx5e_xdpsq;
typedef bool (*mlx5e_fp_xmit_xdp_frame)(struct mlx5e_xdpsq*,
					struct mlx5e_xdp_info*);
struct mlx5e_xdpsq {
	/* data path */

	/* dirtied @completion */
	u32                        xdpi_fifo_cc;
	u16                        cc;
	bool                       redirect_flush;

	/* dirtied @xmit */
	u32                        xdpi_fifo_pc ____cacheline_aligned_in_smp;
	u16                        pc;
	struct mlx5_wqe_ctrl_seg   *doorbell_cseg;
	struct mlx5e_xdp_mpwqe     mpwqe;

	struct mlx5e_cq            cq;

	/* read only */
	struct mlx5_wq_cyc         wq;
	struct mlx5e_xdpsq_stats  *stats;
	mlx5e_fp_xmit_xdp_frame    xmit_xdp_frame;
	struct {
		struct mlx5e_xdp_wqe_info *wqe_info;
		struct mlx5e_xdp_info_fifo xdpi_fifo;
	} db;
	void __iomem              *uar_map;
	u32                        sqn;
	struct device             *pdev;
	__be32                     mkey_be;
	u8                         min_inline_mode;
	unsigned long              state;
	unsigned int               hw_mtu;

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
	unsigned long              state;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;
} ____cacheline_aligned_in_smp;

static inline bool
mlx5e_wqc_has_room_for(struct mlx5_wq_cyc *wq, u16 cc, u16 pc, u16 n)
{
	return (mlx5_wq_cyc_ctr2ix(wq, cc - pc) >= n) || (cc == pc);
}

struct mlx5e_wqe_frag_info {
	struct mlx5e_dma_info *di;
	u32 offset;
	bool last_in_page;
};

struct mlx5e_umr_dma_info {
	struct mlx5e_dma_info  dma_info[MLX5_MPWRQ_PAGES_PER_WQE];
};

struct mlx5e_mpw_info {
	struct mlx5e_umr_dma_info umr;
	u16 consumed_strides;
	DECLARE_BITMAP(xdp_xmit_bitmap, MLX5_MPWRQ_PAGES_PER_WQE);
};

#define MLX5E_MAX_RX_FRAGS 4

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
typedef struct sk_buff *
(*mlx5e_fp_skb_from_cqe_mpwrq)(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
			       u16 cqe_bcnt, u32 head_offset, u32 page_idx);
typedef struct sk_buff *
(*mlx5e_fp_skb_from_cqe)(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe,
			 struct mlx5e_wqe_frag_info *wi, u32 cqe_bcnt);
typedef bool (*mlx5e_fp_post_rx_wqes)(struct mlx5e_rq *rq);
typedef void (*mlx5e_fp_dealloc_wqe)(struct mlx5e_rq*, u16);

enum mlx5e_rq_flag {
	MLX5E_RQ_FLAG_XDP_XMIT = BIT(0),
};

struct mlx5e_rq_frag_info {
	int frag_size;
	int frag_stride;
};

struct mlx5e_rq_frags_info {
	struct mlx5e_rq_frag_info arr[MLX5E_MAX_RX_FRAGS];
	u8 num_frags;
	u8 log_num_frags;
	u8 wqe_bulk;
};

struct mlx5e_rq {
	/* data path */
	union {
		struct {
			struct mlx5_wq_cyc          wq;
			struct mlx5e_wqe_frag_info *frags;
			struct mlx5e_dma_info      *di;
			struct mlx5e_rq_frags_info  info;
			mlx5e_fp_skb_from_cqe       skb_from_cqe;
		} wqe;
		struct {
			struct mlx5_wq_ll      wq;
			struct mlx5e_umr_wqe   umr_wqe;
			struct mlx5e_mpw_info *info;
			mlx5e_fp_skb_from_cqe_mpwrq skb_from_cqe_mpwrq;
			u16                    num_strides;
			u8                     log_stride_sz;
			bool                   umr_in_progress;
		} mpwqe;
	};
	struct {
		u16            headroom;
		u8             map_dir;   /* dma map direction */
	} buff;

	struct mlx5e_channel  *channel;
	struct device         *pdev;
	struct net_device     *netdev;
	struct mlx5e_rq_stats *stats;
	struct mlx5e_cq        cq;
	struct mlx5e_cq_decomp cqd;
	struct mlx5e_page_cache page_cache;
	struct hwtstamp_config *tstamp;
	struct mlx5_clock      *clock;

	mlx5e_fp_handle_rx_cqe handle_rx_cqe;
	mlx5e_fp_post_rx_wqes  post_wqes;
	mlx5e_fp_dealloc_wqe   dealloc_wqe;

	unsigned long          state;
	int                    ix;
	unsigned int           hw_mtu;

	struct net_dim         dim; /* Dynamic Interrupt Moderation */

	/* XDP */
	struct bpf_prog       *xdp_prog;
	struct mlx5e_xdpsq     xdpsq;
	DECLARE_BITMAP(flags, 8);
	struct page_pool      *page_pool;

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

	/* XDP_REDIRECT */
	struct mlx5e_xdpsq         xdpsq;

	/* data path - accessed per napi poll */
	struct irq_desc *irq_desc;
	struct mlx5e_ch_stats     *stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	int                        ix;
	int                        cpu;
	cpumask_var_t              xps_cpumask;
};

struct mlx5e_channels {
	struct mlx5e_channel **c;
	unsigned int           num;
	struct mlx5e_params    params;
};

struct mlx5e_channel_stats {
	struct mlx5e_ch_stats ch;
	struct mlx5e_sq_stats sq[MLX5E_MAX_NUM_TC];
	struct mlx5e_rq_stats rq;
	struct mlx5e_xdpsq_stats rq_xdpsq;
	struct mlx5e_xdpsq_stats xdpsq;
} ____cacheline_aligned_in_smp;

enum {
	MLX5E_STATE_OPENED,
	MLX5E_STATE_DESTROYING,
	MLX5E_STATE_XDP_TX_ENABLED,
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

struct mlx5e_rss_params {
	u32	indirection_rqt[MLX5E_INDIR_RQT_SIZE];
	u32	rx_hash_fields[MLX5E_NUM_INDIR_TIRS];
	u8	toeplitz_hash_key[40];
	u8	hfunc;
};

struct mlx5e_modify_sq_param {
	int curr_state;
	int next_state;
	int rl_update;
	int rl_index;
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
	struct mlx5e_rss_params    rss_params;
	u32                        tx_rates[MLX5E_MAX_NUM_SQS];

	struct mlx5e_flow_steering fs;

	struct workqueue_struct    *wq;
	struct work_struct         update_carrier_work;
	struct work_struct         set_rx_mode_work;
	struct work_struct         tx_timeout_work;
	struct work_struct         update_stats_work;
	struct work_struct         monitor_counters_work;
	struct mlx5_nb             monitor_counters_nb;

	struct mlx5_core_dev      *mdev;
	struct net_device         *netdev;
	struct mlx5e_stats         stats;
	struct mlx5e_channel_stats channel_stats[MLX5E_MAX_NUM_CHANNELS];
	u8                         max_opened_tc;
	struct hwtstamp_config     tstamp;
	u16                        q_counter;
	u16                        drop_rq_q_counter;
	struct notifier_block      events_nb;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct mlx5e_dcbx          dcbx;
#endif

	const struct mlx5e_profile *profile;
	void                      *ppriv;
#ifdef CONFIG_MLX5_EN_IPSEC
	struct mlx5e_ipsec        *ipsec;
#endif
#ifdef CONFIG_MLX5_EN_TLS
	struct mlx5e_tls          *tls;
#endif
	struct devlink_health_reporter *tx_reporter;
};

struct mlx5e_profile {
	int	(*init)(struct mlx5_core_dev *mdev,
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
	struct {
		mlx5e_fp_handle_rx_cqe handle_rx_cqe;
		mlx5e_fp_handle_rx_cqe handle_rx_cqe_mpwqe;
	} rx_handlers;
	int	max_tc;
};

void mlx5e_build_ptys2ethtool_map(void);

u16 mlx5e_select_queue(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev,
		       select_queue_fallback_t fallback);
netdev_tx_t mlx5e_xmit(struct sk_buff *skb, struct net_device *dev);
netdev_tx_t mlx5e_sq_xmit(struct mlx5e_txqsq *sq, struct sk_buff *skb,
			  struct mlx5e_tx_wqe *wqe, u16 pi);

void mlx5e_completion_event(struct mlx5_core_cq *mcq);
void mlx5e_cq_error_event(struct mlx5_core_cq *mcq, enum mlx5_event event);
int mlx5e_napi_poll(struct napi_struct *napi, int budget);
bool mlx5e_poll_tx_cq(struct mlx5e_cq *cq, int napi_budget);
int mlx5e_poll_rx_cq(struct mlx5e_cq *cq, int budget);
void mlx5e_free_txqsq_descs(struct mlx5e_txqsq *sq);

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev);
bool mlx5e_striding_rq_possible(struct mlx5_core_dev *mdev,
				struct mlx5e_params *params);

void mlx5e_page_dma_unmap(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info);
void mlx5e_page_release(struct mlx5e_rq *rq, struct mlx5e_dma_info *dma_info,
			bool recycle);
void mlx5e_handle_rx_cqe(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
void mlx5e_handle_rx_cqe_mpwrq(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);
bool mlx5e_post_rx_wqes(struct mlx5e_rq *rq);
bool mlx5e_post_rx_mpwqes(struct mlx5e_rq *rq);
void mlx5e_dealloc_rx_wqe(struct mlx5e_rq *rq, u16 ix);
void mlx5e_dealloc_rx_mpwqe(struct mlx5e_rq *rq, u16 ix);
struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_linear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				u16 cqe_bcnt, u32 head_offset, u32 page_idx);
struct sk_buff *
mlx5e_skb_from_cqe_mpwrq_nonlinear(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
				   u16 cqe_bcnt, u32 head_offset, u32 page_idx);
struct sk_buff *
mlx5e_skb_from_cqe_linear(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe,
			  struct mlx5e_wqe_frag_info *wi, u32 cqe_bcnt);
struct sk_buff *
mlx5e_skb_from_cqe_nonlinear(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe,
			     struct mlx5e_wqe_frag_info *wi, u32 cqe_bcnt);

void mlx5e_update_stats(struct mlx5e_priv *priv);
void mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats);
void mlx5e_fold_sw_stats64(struct mlx5e_priv *priv, struct rtnl_link_stats64 *s);

void mlx5e_init_l2_addr(struct mlx5e_priv *priv);
int mlx5e_self_test_num(struct mlx5e_priv *priv);
void mlx5e_self_test(struct net_device *ndev, struct ethtool_test *etest,
		     u64 *buf);
void mlx5e_set_rx_mode_work(struct work_struct *work);

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_hwstamp_get(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_modify_rx_cqe_compression_locked(struct mlx5e_priv *priv, bool val);

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid);
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid);
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
void mlx5e_build_indir_tir_ctx_hash(struct mlx5e_rss_params *rss_params,
				    const struct mlx5e_tirc_config *ttconfig,
				    void *tirc, bool inner);
void mlx5e_modify_tirs_hash(struct mlx5e_priv *priv, void *in, int inlen);
struct mlx5e_tirc_config mlx5e_tirc_get_default_config(enum mlx5e_traffic_types tt);

int mlx5e_open_locked(struct net_device *netdev);
int mlx5e_close_locked(struct net_device *netdev);

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs);
void mlx5e_close_channels(struct mlx5e_channels *chs);

/* Function pointer to be used to modify WH settings while
 * switching channels
 */
typedef int (*mlx5e_fp_hw_modify)(struct mlx5e_priv *priv);
int mlx5e_safe_reopen_channels(struct mlx5e_priv *priv);
int mlx5e_safe_switch_channels(struct mlx5e_priv *priv,
			       struct mlx5e_channels *new_chs,
			       mlx5e_fp_hw_modify hw_modify);
void mlx5e_activate_priv_channels(struct mlx5e_priv *priv);
void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv);

void mlx5e_build_default_indir_rqt(u32 *indirection_rqt, int len,
				   int num_channels);
void mlx5e_set_tx_cq_mode_params(struct mlx5e_params *params,
				 u8 cq_period_mode);
void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params,
				 u8 cq_period_mode);
void mlx5e_set_rq_type(struct mlx5_core_dev *mdev, struct mlx5e_params *params);
void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params);

int mlx5e_modify_sq(struct mlx5_core_dev *mdev, u32 sqn,
		    struct mlx5e_modify_sq_param *p);
void mlx5e_activate_txqsq(struct mlx5e_txqsq *sq);
void mlx5e_tx_disable_queue(struct netdev_queue *txq);

static inline bool mlx5e_tunnel_inner_ft_supported(struct mlx5_core_dev *mdev)
{
	return (MLX5_CAP_ETH(mdev, tunnel_stateless_gre) &&
		MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ft_field_support.inner_ip_version));
}

static inline void mlx5e_sq_fetch_wqe(struct mlx5e_txqsq *sq,
				      struct mlx5e_tx_wqe **wqe,
				      u16 *pi)
{
	struct mlx5_wq_cyc *wq = &sq->wq;

	*pi  = mlx5_wq_cyc_ctr2ix(wq, sq->pc);
	*wqe = mlx5_wq_cyc_get_wqe(wq, *pi);
	memset(*wqe, 0, sizeof(**wqe));
}

static inline
struct mlx5e_tx_wqe *mlx5e_post_nop(struct mlx5_wq_cyc *wq, u32 sqn, u16 *pc)
{
	u16                         pi   = mlx5_wq_cyc_ctr2ix(wq, *pc);
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

extern const struct ethtool_ops mlx5e_ethtool_ops;
#ifdef CONFIG_MLX5_CORE_EN_DCB
extern const struct dcbnl_rtnl_ops mlx5e_dcbnl_ops;
int mlx5e_dcbnl_ieee_setets_core(struct mlx5e_priv *priv, struct ieee_ets *ets);
void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv);
void mlx5e_dcbnl_init_app(struct mlx5e_priv *priv);
void mlx5e_dcbnl_delete_app(struct mlx5e_priv *priv);
#endif

int mlx5e_create_tir(struct mlx5_core_dev *mdev,
		     struct mlx5e_tir *tir, u32 *in, int inlen);
void mlx5e_destroy_tir(struct mlx5_core_dev *mdev,
		       struct mlx5e_tir *tir);
int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev);
void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev);
int mlx5e_refresh_tirs(struct mlx5e_priv *priv, bool enable_uc_lb);

/* common netdev helpers */
void mlx5e_create_q_counters(struct mlx5e_priv *priv);
void mlx5e_destroy_q_counters(struct mlx5e_priv *priv);
int mlx5e_open_drop_rq(struct mlx5e_priv *priv,
		       struct mlx5e_rq *drop_rq);
void mlx5e_close_drop_rq(struct mlx5e_rq *drop_rq);

int mlx5e_create_indirect_rqt(struct mlx5e_priv *priv);

int mlx5e_create_indirect_tirs(struct mlx5e_priv *priv, bool inner_ttc);
void mlx5e_destroy_indirect_tirs(struct mlx5e_priv *priv, bool inner_ttc);

int mlx5e_create_direct_rqts(struct mlx5e_priv *priv);
void mlx5e_destroy_direct_rqts(struct mlx5e_priv *priv);
int mlx5e_create_direct_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_direct_tirs(struct mlx5e_priv *priv);
void mlx5e_destroy_rqt(struct mlx5e_priv *priv, struct mlx5e_rqt *rqt);

int mlx5e_create_tis(struct mlx5_core_dev *mdev, int tc,
		     u32 underlay_qpn, u32 *tisn);
void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn);

int mlx5e_create_tises(struct mlx5e_priv *priv);
void mlx5e_update_carrier(struct mlx5e_priv *priv);
int mlx5e_close(struct net_device *netdev);
int mlx5e_open(struct net_device *netdev);
void mlx5e_update_ndo_stats(struct mlx5e_priv *priv);

void mlx5e_queue_update_stats(struct mlx5e_priv *priv);
int mlx5e_bits_invert(unsigned long a, int size);

typedef int (*change_hw_mtu_cb)(struct mlx5e_priv *priv);
int mlx5e_set_dev_port_mtu(struct mlx5e_priv *priv);
int mlx5e_change_mtu(struct net_device *netdev, int new_mtu,
		     change_hw_mtu_cb set_mtu_cb);

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
int mlx5e_ethtool_get_link_ksettings(struct mlx5e_priv *priv,
				     struct ethtool_link_ksettings *link_ksettings);
int mlx5e_ethtool_set_link_ksettings(struct mlx5e_priv *priv,
				     const struct ethtool_link_ksettings *link_ksettings);
u32 mlx5e_ethtool_get_rxfh_key_size(struct mlx5e_priv *priv);
u32 mlx5e_ethtool_get_rxfh_indir_size(struct mlx5e_priv *priv);
int mlx5e_ethtool_get_ts_info(struct mlx5e_priv *priv,
			      struct ethtool_ts_info *info);
int mlx5e_ethtool_flash_device(struct mlx5e_priv *priv,
			       struct ethtool_flash *flash);
void mlx5e_ethtool_get_pauseparam(struct mlx5e_priv *priv,
				  struct ethtool_pauseparam *pauseparam);
int mlx5e_ethtool_set_pauseparam(struct mlx5e_priv *priv,
				 struct ethtool_pauseparam *pauseparam);

/* mlx5e generic netdev management API */
int mlx5e_netdev_init(struct net_device *netdev,
		      struct mlx5e_priv *priv,
		      struct mlx5_core_dev *mdev,
		      const struct mlx5e_profile *profile,
		      void *ppriv);
void mlx5e_netdev_cleanup(struct net_device *netdev, struct mlx5e_priv *priv);
struct net_device*
mlx5e_create_netdev(struct mlx5_core_dev *mdev, const struct mlx5e_profile *profile,
		    int nch, void *ppriv);
int mlx5e_attach_netdev(struct mlx5e_priv *priv);
void mlx5e_detach_netdev(struct mlx5e_priv *priv);
void mlx5e_destroy_netdev(struct mlx5e_priv *priv);
void mlx5e_build_nic_params(struct mlx5_core_dev *mdev,
			    struct mlx5e_rss_params *rss_params,
			    struct mlx5e_params *params,
			    u16 max_channels, u16 mtu);
void mlx5e_build_rq_params(struct mlx5_core_dev *mdev,
			   struct mlx5e_params *params);
void mlx5e_build_rss_params(struct mlx5e_rss_params *rss_params,
			    u16 num_channels);
u8 mlx5e_params_calculate_tx_min_inline(struct mlx5_core_dev *mdev);
void mlx5e_rx_dim_work(struct work_struct *work);
void mlx5e_tx_dim_work(struct work_struct *work);

void mlx5e_add_vxlan_port(struct net_device *netdev, struct udp_tunnel_info *ti);
void mlx5e_del_vxlan_port(struct net_device *netdev, struct udp_tunnel_info *ti);
netdev_features_t mlx5e_features_check(struct sk_buff *skb,
				       struct net_device *netdev,
				       netdev_features_t features);
#ifdef CONFIG_MLX5_ESWITCH
int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac);
int mlx5e_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate, int max_tx_rate);
int mlx5e_get_vf_config(struct net_device *dev, int vf, struct ifla_vf_info *ivi);
int mlx5e_get_vf_stats(struct net_device *dev, int vf, struct ifla_vf_stats *vf_stats);
#endif
#endif /* __MLX5_EN_H__ */
