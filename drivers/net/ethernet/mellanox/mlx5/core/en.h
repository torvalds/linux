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
#include <linux/crash_dump.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/port.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/transobj.h>
#include <linux/mlx5/fs.h>
#include <linux/rhashtable.h>
#include <net/udp_tunnel.h>
#include <net/switchdev.h>
#include <net/xdp.h>
#include <linux/dim.h>
#include <linux/bits.h>
#include "wq.h"
#include "mlx5_core.h"
#include "en_stats.h"
#include "en/dcbnl.h"
#include "en/fs.h"
#include "en/qos.h"
#include "lib/hv_vhca.h"
#include "lib/clock.h"
#include "en/rx_res.h"
#include "en/selq.h"
#include "lib/sd.h"

extern const struct net_device_ops mlx5e_netdev_ops;
struct page_pool;

#define MLX5E_METADATA_ETHER_TYPE (0x8CE4)
#define MLX5E_METADATA_ETHER_LEN 8

#define MLX5E_ETH_HARD_MTU (ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)

#define MLX5E_HW2SW_MTU(params, hwmtu) ((hwmtu) - ((params)->hard_mtu))
#define MLX5E_SW2HW_MTU(params, swmtu) ((swmtu) + ((params)->hard_mtu))

#define MLX5E_MAX_NUM_MQPRIO_CH_TC TC_QOPT_MAX_QUEUE

#define MLX5_RX_HEADROOM NET_SKB_PAD
#define MLX5_SKB_FRAG_SZ(len)	(SKB_DATA_ALIGN(len) +	\
				 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#define MLX5E_RX_MAX_HEAD (256)
#define MLX5E_SHAMPO_LOG_HEADER_ENTRY_SIZE (8)
#define MLX5E_SHAMPO_LOG_MAX_HEADER_ENTRY_SIZE (9)
#define MLX5E_SHAMPO_WQ_HEADER_PER_PAGE (PAGE_SIZE >> MLX5E_SHAMPO_LOG_MAX_HEADER_ENTRY_SIZE)
#define MLX5E_SHAMPO_LOG_WQ_HEADER_PER_PAGE (PAGE_SHIFT - MLX5E_SHAMPO_LOG_MAX_HEADER_ENTRY_SIZE)
#define MLX5E_SHAMPO_WQ_BASE_HEAD_ENTRY_SIZE (64)
#define MLX5E_SHAMPO_WQ_RESRV_SIZE (64 * 1024)
#define MLX5E_SHAMPO_WQ_BASE_RESRV_SIZE (4096)

#define MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev) \
	(6 + MLX5_CAP_GEN(mdev, cache_line_128byte)) /* HW restriction */
#define MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, req) \
	max_t(u32, MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(mdev), req)
#define MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev) \
	MLX5_MPWRQ_LOG_STRIDE_SZ(mdev, order_base_2(MLX5E_RX_MAX_HEAD))

/* Keep in sync with mlx5e_mpwrq_log_wqe_sz.
 * These are theoretical maximums, which can be further restricted by
 * capabilities. These values are used for static resource allocations and
 * sanity checks.
 * MLX5_SEND_WQE_MAX_SIZE is a bit bigger than the maximum cacheline-aligned WQE
 * size actually used at runtime, but it's not a problem when calculating static
 * array sizes.
 */
#define MLX5_UMR_MAX_FLEX_SPACE \
	(ALIGN_DOWN(MLX5_SEND_WQE_MAX_SIZE - sizeof(struct mlx5e_umr_wqe), \
		    MLX5_UMR_FLEX_ALIGNMENT))
#define MLX5_MPWRQ_MAX_PAGES_PER_WQE \
	rounddown_pow_of_two(MLX5_UMR_MAX_FLEX_SPACE / sizeof(struct mlx5_mtt))

#define MLX5E_MAX_RQ_NUM_MTTS	\
	(ALIGN_DOWN(U16_MAX, 4) * 2) /* Fits into u16 and aligned by WQEBB. */
#define MLX5E_MAX_RQ_NUM_KSMS (U16_MAX - 1) /* So that num_ksms fits into u16. */
#define MLX5E_ORDER2_MAX_PACKET_MTU (order_base_2(10 * 1024))

#define MLX5E_MIN_SKB_FRAG_SZ		(MLX5_SKB_FRAG_SZ(MLX5_RX_HEADROOM))
#define MLX5E_LOG_MAX_RX_WQE_BULK	\
	(ilog2(PAGE_SIZE / roundup_pow_of_two(MLX5E_MIN_SKB_FRAG_SZ)))

#define MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x6
#define MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE (1 + MLX5E_LOG_MAX_RX_WQE_BULK)
#define MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE		0xd

#define MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW            0x2

#define MLX5E_DEFAULT_LRO_TIMEOUT                       32
#define MLX5E_DEFAULT_SHAMPO_TIMEOUT			1024

#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE 0x3
#define MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC      0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC_FROM_CQE 0x10
#define MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS      0x20
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES                0x80
#define MLX5E_PARAMS_DEFAULT_MIN_RX_WQES_MPW            0x2

#define MLX5E_MIN_NUM_CHANNELS         0x1
#define MLX5E_MAX_NUM_CHANNELS         256
#define MLX5E_TX_CQ_POLL_BUDGET        128
#define MLX5E_TX_XSK_POLL_BUDGET       64
#define MLX5E_SQ_RECOVER_MIN_INTERVAL  500 /* msecs */

#define mlx5e_state_dereference(priv, p) \
	rcu_dereference_protected((p), lockdep_is_held(&(priv)->state_lock))

enum mlx5e_devcom_events {
	MPV_DEVCOM_MASTER_UP,
	MPV_DEVCOM_MASTER_DOWN,
	MPV_DEVCOM_IPSEC_MASTER_UP,
	MPV_DEVCOM_IPSEC_MASTER_DOWN,
};

static inline u8 mlx5e_get_num_lag_ports(struct mlx5_core_dev *mdev)
{
	if (mlx5_lag_is_lacp_owner(mdev))
		return 1;

	return clamp_t(u8, MLX5_CAP_GEN(mdev, num_lag_ports), 1, MLX5_MAX_PORTS);
}

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
		min3(mlx5_comp_vectors_max(mdev), (u32)MLX5E_MAX_NUM_CHANNELS,
		     (u32)(1 << MLX5_CAP_GEN(mdev, log_max_rqt_size)));
}

/* The maximum WQE size can be retrieved by max_wqe_sz_sq in
 * bytes units. Driver hardens the limitation to 1KB (16
 * WQEBBs), unless firmware capability is stricter.
 */
static inline u8 mlx5e_get_max_sq_wqebbs(struct mlx5_core_dev *mdev)
{
	BUILD_BUG_ON(MLX5_SEND_WQE_MAX_WQEBBS > U8_MAX);

	return (u8)min_t(u16, MLX5_SEND_WQE_MAX_WQEBBS,
			 MLX5_CAP_GEN(mdev, max_wqe_sz_sq) / MLX5_SEND_WQE_BB);
}

static inline u8 mlx5e_get_max_sq_aligned_wqebbs(struct mlx5_core_dev *mdev)
{
/* The return value will be multiplied by MLX5_SEND_WQEBB_NUM_DS.
 * Since max_sq_wqebbs may be up to MLX5_SEND_WQE_MAX_WQEBBS == 16,
 * see mlx5e_get_max_sq_wqebbs(), the multiplication (16 * 4 == 64)
 * overflows the 6-bit DS field of Ctrl Segment. Use a bound lower
 * than MLX5_SEND_WQE_MAX_WQEBBS to let a full-session WQE be
 * cache-aligned.
 */
	u8 wqebbs = mlx5e_get_max_sq_wqebbs(mdev);

	wqebbs = min_t(u8, wqebbs, MLX5_SEND_WQE_MAX_WQEBBS - 1);
#if L1_CACHE_BYTES >= 128
	wqebbs = ALIGN_DOWN(wqebbs, 2);
#endif
	return wqebbs;
}

struct mlx5e_tx_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_eth_seg  eth;
	struct mlx5_wqe_data_seg data[];
};

struct mlx5e_rx_wqe_ll {
	struct mlx5_wqe_srq_next_seg  next;
	struct mlx5_wqe_data_seg      data[];
};

struct mlx5e_rx_wqe_cyc {
	DECLARE_FLEX_ARRAY(struct mlx5_wqe_data_seg, data);
};

struct mlx5e_umr_wqe_hdr {
	struct mlx5_wqe_ctrl_seg       ctrl;
	struct mlx5_wqe_umr_ctrl_seg   uctrl;
	struct mlx5_mkey_seg           mkc;
};

struct mlx5e_umr_wqe {
	struct mlx5e_umr_wqe_hdr hdr;
	union {
		DECLARE_FLEX_ARRAY(struct mlx5_mtt, inline_mtts);
		DECLARE_FLEX_ARRAY(struct mlx5_klm, inline_klms);
		DECLARE_FLEX_ARRAY(struct mlx5_ksm, inline_ksms);
	};
};
static_assert(offsetof(struct mlx5e_umr_wqe, inline_mtts) == sizeof(struct mlx5e_umr_wqe_hdr),
	      "struct members should be included in struct mlx5e_umr_wqe_hdr, not in struct mlx5e_umr_wqe");

enum mlx5e_priv_flag {
	MLX5E_PFLAG_RX_CQE_BASED_MODER,
	MLX5E_PFLAG_TX_CQE_BASED_MODER,
	MLX5E_PFLAG_RX_CQE_COMPRESS,
	MLX5E_PFLAG_RX_STRIDING_RQ,
	MLX5E_PFLAG_RX_NO_CSUM_COMPLETE,
	MLX5E_PFLAG_XDP_TX_MPWQE,
	MLX5E_PFLAG_SKB_TX_MPWQE,
	MLX5E_PFLAG_TX_PORT_TS,
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

enum packet_merge {
	MLX5E_PACKET_MERGE_NONE,
	MLX5E_PACKET_MERGE_LRO,
	MLX5E_PACKET_MERGE_SHAMPO,
};

struct mlx5e_packet_merge_param {
	enum packet_merge type;
	u32 timeout;
	struct {
		u8 match_criteria_type;
		u8 alignment_granularity;
	} shampo;
};

struct mlx5e_params {
	u8  log_sq_size;
	u8  rq_wq_type;
	u8  log_rq_mtu_frames;
	u16 num_channels;
	struct {
		u16 mode;
		u8 num_tc;
		struct netdev_tc_txq tc_to_txq[TC_MAX_QUEUE];
		struct {
			u64 max_rate[TC_MAX_QUEUE];
			u32 hw_id[TC_MAX_QUEUE];
		} channel;
	} mqprio;
	bool rx_cqe_compress_def;
	struct dim_cq_moder rx_cq_moderation;
	struct dim_cq_moder tx_cq_moderation;
	struct mlx5e_packet_merge_param packet_merge;
	u8  tx_min_inline_mode;
	bool vlan_strip_disable;
	bool scatter_fcs_en;
	bool rx_dim_enabled;
	bool tx_dim_enabled;
	bool rx_moder_use_cqe_mode;
	bool tx_moder_use_cqe_mode;
	u32 pflags;
	struct bpf_prog *xdp_prog;
	struct mlx5e_xsk *xsk;
	unsigned int sw_mtu;
	int hard_mtu;
	bool ptp_rx;
	__be32 terminate_lkey_be;
};

static inline u8 mlx5e_get_dcb_num_tc(struct mlx5e_params *params)
{
	return params->mqprio.mode == TC_MQPRIO_MODE_DCB ?
		params->mqprio.num_tc : 1;
}

/* Keep this enum consistent with the corresponding strings array
 * declared in en/reporter_rx.c
 */
enum {
	MLX5E_RQ_STATE_ENABLED = 0,
	MLX5E_RQ_STATE_RECOVERING,
	MLX5E_RQ_STATE_DIM,
	MLX5E_RQ_STATE_NO_CSUM_COMPLETE,
	MLX5E_RQ_STATE_CSUM_FULL, /* cqe_csum_full hw bit is set */
	MLX5E_RQ_STATE_MINI_CQE_HW_STRIDX, /* set when mini_cqe_resp_stride_index cap is used */
	MLX5E_RQ_STATE_SHAMPO, /* set when SHAMPO cap is used */
	MLX5E_RQ_STATE_MINI_CQE_ENHANCED,  /* set when enhanced mini_cqe_cap is used */
	MLX5E_RQ_STATE_XSK, /* set to indicate an xsk rq */
	MLX5E_NUM_RQ_STATES, /* Must be kept last */
};

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	u16                        event_ctr;
	struct napi_struct        *napi;
	struct mlx5_core_cq        mcq;
	struct mlx5e_ch_stats     *ch_stats;

	/* control */
	struct net_device         *netdev;
	struct mlx5_core_dev      *mdev;
	struct workqueue_struct   *workqueue;
	struct mlx5_wq_ctrl        wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5e_cq_decomp {
	/* cqe decompression */
	struct mlx5_cqe64          title;
	struct mlx5_mini_cqe8      mini_arr[MLX5_MINI_CQE_ARRAY_SIZE];
	u8                         mini_arr_idx;
	u16                        left;
	u16                        wqe_counter;
	bool                       last_cqe_title;
} ____cacheline_aligned_in_smp;

enum mlx5e_dma_map_type {
	MLX5E_DMA_MAP_SINGLE,
	MLX5E_DMA_MAP_PAGE
};

struct mlx5e_sq_dma {
	dma_addr_t              addr;
	u32                     size;
	enum mlx5e_dma_map_type type;
};

/* Keep this enum consistent with with the corresponding strings array
 * declared in en/reporter_tx.c
 */
enum {
	MLX5E_SQ_STATE_ENABLED = 0,
	MLX5E_SQ_STATE_MPWQE,
	MLX5E_SQ_STATE_RECOVERING,
	MLX5E_SQ_STATE_IPSEC,
	MLX5E_SQ_STATE_DIM,
	MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE,
	MLX5E_SQ_STATE_PENDING_XSK_TX,
	MLX5E_SQ_STATE_PENDING_TLS_RX_RESYNC,
	MLX5E_NUM_SQ_STATES, /* Must be kept last */
};

struct mlx5e_tx_mpwqe {
	/* Current MPWQE session */
	struct mlx5e_tx_wqe *wqe;
	u32 bytes_count;
	u8 ds_count;
	u8 ds_count_max;
	u8 pkt_count;
	u8 inline_on;
};

struct mlx5e_skb_fifo {
	struct sk_buff **fifo;
	u16 *pc;
	u16 *cc;
	u16 mask;
};

struct mlx5e_ptpsq;

struct mlx5e_txqsq {
	/* data path */

	/* dirtied @completion */
	u16                        cc;
	u16                        skb_fifo_cc;
	u32                        dma_fifo_cc;
	struct dim                *dim; /* Adaptive Moderation */

	/* dirtied @xmit */
	u16                        pc ____cacheline_aligned_in_smp;
	u16                        skb_fifo_pc;
	u32                        dma_fifo_pc;
	struct mlx5e_tx_mpwqe      mpwqe;

	struct mlx5e_cq            cq;

	/* read only */
	struct mlx5_wq_cyc         wq;
	u32                        dma_fifo_mask;
	struct mlx5e_sq_stats     *stats;
	struct {
		struct mlx5e_sq_dma       *dma_fifo;
		struct mlx5e_skb_fifo      skb_fifo;
		struct mlx5e_tx_wqe_info  *wqe_info;
	} db;
	void __iomem              *uar_map;
	struct netdev_queue       *txq;
	u32                        sqn;
	u16                        stop_room;
	u8                         max_sq_mpw_wqebbs;
	u8                         min_inline_mode;
	struct device             *pdev;
	__be32                     mkey_be;
	unsigned long              state;
	unsigned int               hw_mtu;
	struct mlx5_clock         *clock;
	struct net_device         *netdev;
	struct mlx5_core_dev      *mdev;
	struct mlx5e_channel      *channel;
	struct mlx5e_priv         *priv;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	int                        ch_ix;
	int                        txq_ix;
	u32                        rate_limit;
	struct work_struct         recover_work;
	struct mlx5e_ptpsq        *ptpsq;
	cqe_ts_to_ns               ptp_cyc2time;
} ____cacheline_aligned_in_smp;

struct mlx5e_xdp_info_fifo {
	union mlx5e_xdp_info *xi;
	u32 *cc;
	u32 *pc;
	u32 mask;
};

struct mlx5e_xdpsq;
struct mlx5e_xmit_data;
struct xsk_tx_metadata;
typedef int (*mlx5e_fp_xmit_xdp_frame_check)(struct mlx5e_xdpsq *);
typedef bool (*mlx5e_fp_xmit_xdp_frame)(struct mlx5e_xdpsq *,
					struct mlx5e_xmit_data *,
					int,
					struct xsk_tx_metadata *);

struct mlx5e_xdpsq {
	/* data path */

	/* dirtied @completion */
	u32                        xdpi_fifo_cc;
	u16                        cc;

	/* dirtied @xmit */
	u32                        xdpi_fifo_pc ____cacheline_aligned_in_smp;
	u16                        pc;
	struct mlx5_wqe_ctrl_seg   *doorbell_cseg;
	struct mlx5e_tx_mpwqe      mpwqe;

	struct mlx5e_cq            cq;

	/* read only */
	struct xsk_buff_pool      *xsk_pool;
	struct mlx5_wq_cyc         wq;
	struct mlx5e_xdpsq_stats  *stats;
	mlx5e_fp_xmit_xdp_frame_check xmit_xdp_frame_check;
	mlx5e_fp_xmit_xdp_frame    xmit_xdp_frame;
	struct {
		struct mlx5e_xdp_wqe_info *wqe_info;
		struct mlx5e_xdp_info_fifo xdpi_fifo;
	} db;
	void __iomem              *uar_map;
	u32                        sqn;
	struct device             *pdev;
	__be32                     mkey_be;
	u16                        stop_room;
	u8                         max_sq_mpw_wqebbs;
	u8                         min_inline_mode;
	unsigned long              state;
	unsigned int               hw_mtu;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;
} ____cacheline_aligned_in_smp;

struct mlx5e_xdp_buff {
	struct xdp_buff xdp;
	struct mlx5_cqe64 *cqe;
	struct mlx5e_rq *rq;
};

struct mlx5e_ktls_resync_resp;

struct mlx5e_icosq {
	/* data path */
	u16                        cc;
	u16                        pc;

	struct mlx5_wqe_ctrl_seg  *doorbell_cseg;
	struct mlx5e_cq            cq;

	/* write@xmit, read@completion */
	struct {
		struct mlx5e_icosq_wqe_info *wqe_info;
	} db;

	/* read only */
	struct mlx5_wq_cyc         wq;
	void __iomem              *uar_map;
	u32                        sqn;
	u16                        reserved_room;
	unsigned long              state;
	struct mlx5e_ktls_resync_resp *ktls_resync;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;
	struct mlx5e_channel      *channel;

	struct work_struct         recover_work;
} ____cacheline_aligned_in_smp;

struct mlx5e_frag_page {
	struct page *page;
	u16 frags;
};

enum mlx5e_wqe_frag_flag {
	MLX5E_WQE_FRAG_LAST_IN_PAGE,
	MLX5E_WQE_FRAG_SKIP_RELEASE,
};

struct mlx5e_wqe_frag_info {
	union {
		struct mlx5e_frag_page *frag_page;
		struct xdp_buff **xskp;
	};
	u32 offset;
	u8 flags;
};

union mlx5e_alloc_units {
	DECLARE_FLEX_ARRAY(struct mlx5e_frag_page, frag_pages);
	DECLARE_FLEX_ARRAY(struct page *, pages);
	DECLARE_FLEX_ARRAY(struct xdp_buff *, xsk_buffs);
};

struct mlx5e_mpw_info {
	u16 consumed_strides;
	DECLARE_BITMAP(skip_release_bitmap, MLX5_MPWRQ_MAX_PAGES_PER_WQE);
	struct mlx5e_frag_page linear_page;
	union mlx5e_alloc_units alloc_units;
};

#define MLX5E_MAX_RX_FRAGS 4

struct mlx5e_rq;
typedef void (*mlx5e_fp_handle_rx_cqe)(struct mlx5e_rq*, struct mlx5_cqe64*);
typedef struct sk_buff *
(*mlx5e_fp_skb_from_cqe_mpwrq)(struct mlx5e_rq *rq, struct mlx5e_mpw_info *wi,
			       struct mlx5_cqe64 *cqe, u16 cqe_bcnt,
			       u32 head_offset, u32 page_idx);
typedef struct sk_buff *
(*mlx5e_fp_skb_from_cqe)(struct mlx5e_rq *rq, struct mlx5e_wqe_frag_info *wi,
			 struct mlx5_cqe64 *cqe, u32 cqe_bcnt);
typedef bool (*mlx5e_fp_post_rx_wqes)(struct mlx5e_rq *rq);
typedef void (*mlx5e_fp_dealloc_wqe)(struct mlx5e_rq*, u16);
typedef void (*mlx5e_fp_shampo_dealloc_hd)(struct mlx5e_rq*, u16, u16, bool);

int mlx5e_rq_set_handlers(struct mlx5e_rq *rq, struct mlx5e_params *params, bool xsk);
void mlx5e_rq_set_trap_handlers(struct mlx5e_rq *rq, struct mlx5e_params *params);

enum mlx5e_rq_flag {
	MLX5E_RQ_FLAG_XDP_XMIT,
	MLX5E_RQ_FLAG_XDP_REDIRECT,
};

struct mlx5e_rq_frag_info {
	int frag_size;
	int frag_stride;
};

struct mlx5e_rq_frags_info {
	struct mlx5e_rq_frag_info arr[MLX5E_MAX_RX_FRAGS];
	u8 num_frags;
	u8 log_num_frags;
	u16 wqe_bulk;
	u16 refill_unit;
	u8 wqe_index_mask;
};

struct mlx5e_dma_info {
	dma_addr_t addr;
	union {
		struct mlx5e_frag_page *frag_page;
		struct page *page;
	};
};

struct mlx5e_shampo_hd {
	u32 mkey;
	struct mlx5e_frag_page *pages;
	u32 hd_per_wq;
	u16 hd_per_wqe;
	u16 pages_per_wq;
	unsigned long *bitmap;
	u16 pi;
	u16 ci;
	__be32 key;
};

struct mlx5e_hw_gro_data {
	struct sk_buff *skb;
	struct flow_keys fk;
	int second_ip_id;
};

enum mlx5e_mpwrq_umr_mode {
	MLX5E_MPWRQ_UMR_MODE_ALIGNED,
	MLX5E_MPWRQ_UMR_MODE_UNALIGNED,
	MLX5E_MPWRQ_UMR_MODE_OVERSIZED,
	MLX5E_MPWRQ_UMR_MODE_TRIPLE,
};

struct mlx5e_rq {
	/* data path */
	union {
		struct {
			struct mlx5_wq_cyc          wq;
			struct mlx5e_wqe_frag_info *frags;
			union mlx5e_alloc_units    *alloc_units;
			struct mlx5e_rq_frags_info  info;
			mlx5e_fp_skb_from_cqe       skb_from_cqe;
		} wqe;
		struct {
			struct mlx5_wq_ll      wq;
			struct mlx5e_umr_wqe_hdr umr_wqe;
			struct mlx5e_mpw_info *info;
			mlx5e_fp_skb_from_cqe_mpwrq skb_from_cqe_mpwrq;
			__be32                 umr_mkey_be;
			u16                    num_strides;
			u16                    actual_wq_head;
			u8                     log_stride_sz;
			u8                     umr_in_progress;
			u8                     umr_last_bulk;
			u8                     umr_completed;
			u8                     min_wqe_bulk;
			u8                     page_shift;
			u8                     pages_per_wqe;
			u8                     umr_wqebbs;
			u8                     mtts_per_wqe;
			u8                     umr_mode;
			struct mlx5e_shampo_hd *shampo;
		} mpwqe;
	};
	struct {
		u16            headroom;
		u32            frame0_sz;
		u8             map_dir;   /* dma map direction */
	} buff;

	struct device         *pdev;
	struct net_device     *netdev;
	struct mlx5e_rq_stats *stats;
	struct mlx5e_cq        cq;
	struct mlx5e_cq_decomp cqd;
	struct hwtstamp_config *tstamp;
	struct mlx5_clock      *clock;
	struct mlx5e_icosq    *icosq;
	struct mlx5e_priv     *priv;

	struct mlx5e_hw_gro_data *hw_gro_data;

	mlx5e_fp_handle_rx_cqe handle_rx_cqe;
	mlx5e_fp_post_rx_wqes  post_wqes;
	mlx5e_fp_dealloc_wqe   dealloc_wqe;

	unsigned long          state;
	int                    ix;
	unsigned int           hw_mtu;

	struct dim            *dim; /* Dynamic Interrupt Moderation */

	/* XDP */
	struct bpf_prog __rcu *xdp_prog;
	struct mlx5e_xdpsq    *xdpsq;
	DECLARE_BITMAP(flags, 8);
	struct page_pool      *page_pool;
	struct mlx5e_xdp_buff mxbuf;

	/* AF_XDP zero-copy */
	struct xsk_buff_pool  *xsk_pool;

	struct work_struct     recover_work;
	struct work_struct     rx_timeout_work;

	/* control */
	struct mlx5_wq_ctrl    wq_ctrl;
	__be32                 mkey_be;
	u8                     wq_type;
	u32                    rqn;
	struct mlx5_core_dev  *mdev;
	struct mlx5e_channel  *channel;
	struct mlx5e_dma_info  wqe_overflow;

	/* XDP read-mostly */
	struct xdp_rxq_info    xdp_rxq;
	cqe_ts_to_ns           ptp_cyc2time;
} ____cacheline_aligned_in_smp;

enum mlx5e_channel_state {
	MLX5E_CHANNEL_STATE_XSK,
	MLX5E_CHANNEL_NUM_STATES
};

struct mlx5e_channel {
	/* data path */
	struct mlx5e_rq            rq;
	struct mlx5e_xdpsq         rq_xdpsq;
	struct mlx5e_txqsq         sq[MLX5_MAX_NUM_TC];
	struct mlx5e_icosq         icosq;   /* internal control operations */
	struct mlx5e_txqsq __rcu * __rcu *qos_sqs;
	bool                       xdp;
	struct napi_struct         napi;
	struct device             *pdev;
	struct net_device         *netdev;
	__be32                     mkey_be;
	u16                        qos_sqs_size;
	u8                         num_tc;
	u8                         lag_port;

	/* XDP_REDIRECT */
	struct mlx5e_xdpsq        *xdpsq;

	/* AF_XDP zero-copy */
	struct mlx5e_rq            xskrq;
	struct mlx5e_xdpsq         xsksq;

	/* Async ICOSQ */
	struct mlx5e_icosq         async_icosq;
	/* async_icosq can be accessed from any CPU - the spinlock protects it. */
	spinlock_t                 async_icosq_lock;

	/* data path - accessed per napi poll */
	const struct cpumask	  *aff_mask;
	struct mlx5e_ch_stats     *stats;

	/* control */
	struct mlx5e_priv         *priv;
	struct mlx5_core_dev      *mdev;
	struct hwtstamp_config    *tstamp;
	DECLARE_BITMAP(state, MLX5E_CHANNEL_NUM_STATES);
	int                        ix;
	int                        vec_ix;
	int                        sd_ix;
	int                        cpu;
	/* Sync between icosq recovery and XSK enable/disable. */
	struct mutex               icosq_recovery_lock;

	/* coalescing configuration */
	struct dim_cq_moder        rx_cq_moder;
	struct dim_cq_moder        tx_cq_moder;
};

struct mlx5e_ptp;

struct mlx5e_channels {
	struct mlx5e_channel **c;
	struct mlx5e_ptp      *ptp;
	unsigned int           num;
	struct mlx5e_params    params;
};

struct mlx5e_channel_stats {
	struct mlx5e_ch_stats ch;
	struct mlx5e_sq_stats sq[MLX5_MAX_NUM_TC];
	struct mlx5e_rq_stats rq;
	struct mlx5e_rq_stats xskrq;
	struct mlx5e_xdpsq_stats rq_xdpsq;
	struct mlx5e_xdpsq_stats xdpsq;
	struct mlx5e_xdpsq_stats xsksq;
} ____cacheline_aligned_in_smp;

struct mlx5e_ptp_stats {
	struct mlx5e_ch_stats ch;
	struct mlx5e_sq_stats sq[MLX5_MAX_NUM_TC];
	struct mlx5e_ptp_cq_stats cq[MLX5_MAX_NUM_TC];
	struct mlx5e_rq_stats rq;
} ____cacheline_aligned_in_smp;

enum {
	MLX5E_STATE_OPENED,
	MLX5E_STATE_DESTROYING,
	MLX5E_STATE_XDP_TX_ENABLED,
	MLX5E_STATE_XDP_ACTIVE,
	MLX5E_STATE_CHANNELS_ACTIVE,
};

struct mlx5e_modify_sq_param {
	int curr_state;
	int next_state;
	int rl_update;
	int rl_index;
	bool qos_update;
	u16 qos_queue_group_id;
};

#if IS_ENABLED(CONFIG_PCI_HYPERV_INTERFACE)
struct mlx5e_hv_vhca_stats_agent {
	struct mlx5_hv_vhca_agent *agent;
	struct delayed_work        work;
	u16                        delay;
	void                      *buf;
};
#endif

struct mlx5e_xsk {
	/* XSK buffer pools are stored separately from channels,
	 * because we don't want to lose them when channels are
	 * recreated. The kernel also stores buffer pool, but it doesn't
	 * distinguish between zero-copy and non-zero-copy UMEMs, so
	 * rely on our mechanism.
	 */
	struct xsk_buff_pool **pools;
	u16 refcnt;
	bool ever_used;
};

/* Temporary storage for variables that are allocated when struct mlx5e_priv is
 * initialized, and used where we can't allocate them because that functions
 * must not fail. Use with care and make sure the same variable is not used
 * simultaneously by multiple users.
 */
struct mlx5e_scratchpad {
	cpumask_var_t cpumask;
};

struct mlx5e_trap;
struct mlx5e_htb;

struct mlx5e_priv {
	/* priv data path fields - start */
	struct mlx5e_selq selq;
	struct mlx5e_txqsq **txq2sq;
	struct mlx5e_sq_stats **txq2sq_stats;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct mlx5e_dcbx_dp       dcbx_dp;
#endif
	/* priv data path fields - end */

	unsigned long              state;
	struct mutex               state_lock; /* Protects Interface state */
	struct mlx5e_rq            drop_rq;

	struct mlx5e_channels      channels;
	struct mlx5e_rx_res       *rx_res;
	u32                       *tx_rates;

	struct mlx5e_flow_steering *fs;

	struct workqueue_struct    *wq;
	struct work_struct         update_carrier_work;
	struct work_struct         set_rx_mode_work;
	struct work_struct         tx_timeout_work;
	struct work_struct         update_stats_work;
	struct work_struct         monitor_counters_work;
	struct mlx5_nb             monitor_counters_nb;

	struct mlx5_core_dev      *mdev;
	struct net_device         *netdev;
	struct mlx5e_trap         *en_trap;
	struct mlx5e_stats         stats;
	struct mlx5e_channel_stats **channel_stats;
	struct mlx5e_channel_stats trap_stats;
	struct mlx5e_ptp_stats     ptp_stats;
	struct mlx5e_sq_stats      **htb_qos_sq_stats;
	u16                        htb_max_qos_sqs;
	u16                        stats_nch;
	u16                        max_nch;
	u8                         max_opened_tc;
	bool                       tx_ptp_opened;
	bool                       rx_ptp_opened;
	struct hwtstamp_config     tstamp;
	u16                        q_counter[MLX5_SD_MAX_GROUP_SZ];
	u16                        drop_rq_q_counter;
	struct notifier_block      events_nb;
	struct notifier_block      blocking_events_nb;

	struct udp_tunnel_nic_info nic_info;
#ifdef CONFIG_MLX5_CORE_EN_DCB
	struct mlx5e_dcbx          dcbx;
#endif

	const struct mlx5e_profile *profile;
	void                      *ppriv;
#ifdef CONFIG_MLX5_MACSEC
	struct mlx5e_macsec       *macsec;
#endif
#ifdef CONFIG_MLX5_EN_IPSEC
	struct mlx5e_ipsec        *ipsec;
#endif
#ifdef CONFIG_MLX5_EN_TLS
	struct mlx5e_tls          *tls;
#endif
	struct devlink_health_reporter *tx_reporter;
	struct devlink_health_reporter *rx_reporter;
	struct mlx5e_xsk           xsk;
#if IS_ENABLED(CONFIG_PCI_HYPERV_INTERFACE)
	struct mlx5e_hv_vhca_stats_agent stats_agent;
#endif
	struct mlx5e_scratchpad    scratchpad;
	struct mlx5e_htb          *htb;
	struct mlx5e_mqprio_rl    *mqprio_rl;
	struct dentry             *dfs_root;
	struct mlx5_devcom_comp_dev *devcom;
};

struct mlx5e_dev {
	struct mlx5e_priv *priv;
	struct devlink_port dl_port;
};

struct mlx5e_rx_handlers {
	mlx5e_fp_handle_rx_cqe handle_rx_cqe;
	mlx5e_fp_handle_rx_cqe handle_rx_cqe_mpwqe;
	mlx5e_fp_handle_rx_cqe handle_rx_cqe_mpwqe_shampo;
};

extern const struct mlx5e_rx_handlers mlx5e_rx_handlers_nic;

enum mlx5e_profile_feature {
	MLX5E_PROFILE_FEATURE_PTP_RX,
	MLX5E_PROFILE_FEATURE_PTP_TX,
	MLX5E_PROFILE_FEATURE_QOS_HTB,
	MLX5E_PROFILE_FEATURE_FS_VLAN,
	MLX5E_PROFILE_FEATURE_FS_TC,
};

struct mlx5e_profile {
	int	(*init)(struct mlx5_core_dev *mdev,
			struct net_device *netdev);
	void	(*cleanup)(struct mlx5e_priv *priv);
	int	(*init_rx)(struct mlx5e_priv *priv);
	void	(*cleanup_rx)(struct mlx5e_priv *priv);
	int	(*init_tx)(struct mlx5e_priv *priv);
	void	(*cleanup_tx)(struct mlx5e_priv *priv);
	void	(*enable)(struct mlx5e_priv *priv);
	void	(*disable)(struct mlx5e_priv *priv);
	int	(*update_rx)(struct mlx5e_priv *priv);
	void	(*update_stats)(struct mlx5e_priv *priv);
	void	(*update_carrier)(struct mlx5e_priv *priv);
	int	(*max_nch_limit)(struct mlx5_core_dev *mdev);
	u32	(*get_tisn)(struct mlx5_core_dev *mdev, struct mlx5e_priv *priv,
			    u8 lag_port, u8 tc);
	unsigned int (*stats_grps_num)(struct mlx5e_priv *priv);
	mlx5e_stats_grp_t *stats_grps;
	const struct mlx5e_rx_handlers *rx_handlers;
	int	max_tc;
	u32     features;
};

u32 mlx5e_profile_get_tisn(struct mlx5_core_dev *mdev,
			   struct mlx5e_priv *priv,
			   const struct mlx5e_profile *profile,
			   u8 lag_port, u8 tc);

#define mlx5e_profile_feature_cap(profile, feature)	\
	((profile)->features & BIT(MLX5E_PROFILE_FEATURE_##feature))

void mlx5e_build_ptys2ethtool_map(void);

bool mlx5e_check_fragmented_striding_rq_cap(struct mlx5_core_dev *mdev, u8 page_shift,
					    enum mlx5e_mpwrq_umr_mode umr_mode);

void mlx5e_shampo_fill_umr(struct mlx5e_rq *rq, int len);
void mlx5e_shampo_dealloc_hd(struct mlx5e_rq *rq);
void mlx5e_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats);
void mlx5e_fold_sw_stats64(struct mlx5e_priv *priv, struct rtnl_link_stats64 *s);

int mlx5e_self_test_num(struct mlx5e_priv *priv);
int mlx5e_self_test_fill_strings(struct mlx5e_priv *priv, u8 *data);
void mlx5e_self_test(struct net_device *ndev, struct ethtool_test *etest,
		     u64 *buf);
void mlx5e_set_rx_mode_work(struct work_struct *work);

int mlx5e_hwstamp_set(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_hwstamp_get(struct mlx5e_priv *priv, struct ifreq *ifr);
int mlx5e_modify_rx_cqe_compression_locked(struct mlx5e_priv *priv, bool val, bool rx_filter);

int mlx5e_vlan_rx_add_vid(struct net_device *dev, __always_unused __be16 proto,
			  u16 vid);
int mlx5e_vlan_rx_kill_vid(struct net_device *dev, __always_unused __be16 proto,
			   u16 vid);
void mlx5e_timestamp_init(struct mlx5e_priv *priv);

struct mlx5e_xsk_param;

struct mlx5e_rq_param;
int mlx5e_open_rq(struct mlx5e_params *params, struct mlx5e_rq_param *param,
		  struct mlx5e_xsk_param *xsk, int node, u16 q_counter,
		  struct mlx5e_rq *rq);
#define MLX5E_RQ_WQES_TIMEOUT 20000 /* msecs */
int mlx5e_wait_for_min_rx_wqes(struct mlx5e_rq *rq, int wait_time);
void mlx5e_close_rq(struct mlx5e_rq *rq);
int mlx5e_create_rq(struct mlx5e_rq *rq, struct mlx5e_rq_param *param, u16 q_counter);
void mlx5e_destroy_rq(struct mlx5e_rq *rq);

bool mlx5e_reset_rx_moderation(struct dim_cq_moder *cq_moder, u8 cq_period_mode,
			       bool dim_enabled);
bool mlx5e_reset_rx_channels_moderation(struct mlx5e_channels *chs, u8 cq_period_mode,
					bool dim_enabled, bool keep_dim_state);

struct mlx5e_sq_param;
int mlx5e_open_xdpsq(struct mlx5e_channel *c, struct mlx5e_params *params,
		     struct mlx5e_sq_param *param, struct xsk_buff_pool *xsk_pool,
		     struct mlx5e_xdpsq *sq, bool is_redirect);
void mlx5e_close_xdpsq(struct mlx5e_xdpsq *sq);

struct mlx5e_create_cq_param {
	struct net_device *netdev;
	struct workqueue_struct *wq;
	struct napi_struct *napi;
	struct mlx5e_ch_stats *ch_stats;
	int node;
	int ix;
};

struct mlx5e_cq_param;
int mlx5e_open_cq(struct mlx5_core_dev *mdev, struct dim_cq_moder moder,
		  struct mlx5e_cq_param *param, struct mlx5e_create_cq_param *ccp,
		  struct mlx5e_cq *cq);
void mlx5e_close_cq(struct mlx5e_cq *cq);
int mlx5e_modify_cq_period_mode(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
				u8 cq_period_mode);
int mlx5e_modify_cq_moderation(struct mlx5_core_dev *dev, struct mlx5_core_cq *cq,
			       u16 cq_period, u16 cq_max_count, u8 cq_period_mode);

int mlx5e_open_locked(struct net_device *netdev);
int mlx5e_close_locked(struct net_device *netdev);

void mlx5e_trigger_napi_icosq(struct mlx5e_channel *c);
void mlx5e_trigger_napi_sched(struct napi_struct *napi);

int mlx5e_open_channels(struct mlx5e_priv *priv,
			struct mlx5e_channels *chs);
void mlx5e_close_channels(struct mlx5e_channels *chs);

/* Function pointer to be used to modify HW or kernel settings while
 * switching channels
 */
typedef int (*mlx5e_fp_preactivate)(struct mlx5e_priv *priv, void *context);
#define MLX5E_DEFINE_PREACTIVATE_WRAPPER_CTX(fn) \
int fn##_ctx(struct mlx5e_priv *priv, void *context) \
{ \
	return fn(priv); \
}
int mlx5e_safe_reopen_channels(struct mlx5e_priv *priv);
int mlx5e_safe_switch_params(struct mlx5e_priv *priv,
			     struct mlx5e_params *new_params,
			     mlx5e_fp_preactivate preactivate,
			     void *context, bool reset);
int mlx5e_update_tx_netdev_queues(struct mlx5e_priv *priv);
int mlx5e_num_channels_changed_ctx(struct mlx5e_priv *priv, void *context);
int mlx5e_update_tc_and_tx_queues_ctx(struct mlx5e_priv *priv, void *context);
void mlx5e_activate_priv_channels(struct mlx5e_priv *priv);
void mlx5e_deactivate_priv_channels(struct mlx5e_priv *priv);
int mlx5e_ptp_rx_manage_fs_ctx(struct mlx5e_priv *priv, void *ctx);

int mlx5e_flush_rq(struct mlx5e_rq *rq, int curr_state);
void mlx5e_activate_rq(struct mlx5e_rq *rq);
void mlx5e_deactivate_rq(struct mlx5e_rq *rq);
void mlx5e_activate_icosq(struct mlx5e_icosq *icosq);
void mlx5e_deactivate_icosq(struct mlx5e_icosq *icosq);

int mlx5e_modify_sq(struct mlx5_core_dev *mdev, u32 sqn,
		    struct mlx5e_modify_sq_param *p);
int mlx5e_open_txqsq(struct mlx5e_channel *c, u32 tisn, int txq_ix,
		     struct mlx5e_params *params, struct mlx5e_sq_param *param,
		     struct mlx5e_txqsq *sq, int tc, u16 qos_queue_group_id,
		     struct mlx5e_sq_stats *sq_stats);
void mlx5e_activate_txqsq(struct mlx5e_txqsq *sq);
void mlx5e_deactivate_txqsq(struct mlx5e_txqsq *sq);
void mlx5e_free_txqsq(struct mlx5e_txqsq *sq);
void mlx5e_tx_disable_queue(struct netdev_queue *txq);
int mlx5e_alloc_txqsq_db(struct mlx5e_txqsq *sq, int numa);
void mlx5e_free_txqsq_db(struct mlx5e_txqsq *sq);
struct mlx5e_create_sq_param;
int mlx5e_create_sq_rdy(struct mlx5_core_dev *mdev,
			struct mlx5e_sq_param *param,
			struct mlx5e_create_sq_param *csp,
			u16 qos_queue_group_id,
			u32 *sqn);
void mlx5e_tx_err_cqe_work(struct work_struct *recover_work);
void mlx5e_close_txqsq(struct mlx5e_txqsq *sq);

bool mlx5e_reset_tx_moderation(struct dim_cq_moder *cq_moder, u8 cq_period_mode,
			       bool dim_enabled);
bool mlx5e_reset_tx_channels_moderation(struct mlx5e_channels *chs, u8 cq_period_mode,
					bool dim_enabled, bool keep_dim_state);

static inline bool mlx5_tx_swp_supported(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_ETH(mdev, swp) &&
		MLX5_CAP_ETH(mdev, swp_csum) && MLX5_CAP_ETH(mdev, swp_lso);
}

extern const struct ethtool_ops mlx5e_ethtool_ops;

int mlx5e_create_mkey(struct mlx5_core_dev *mdev, u32 pdn, u32 *mkey);
int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev, bool create_tises);
void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev);
int mlx5e_refresh_tirs(struct mlx5e_priv *priv, bool enable_uc_lb,
		       bool enable_mc_lb);
void mlx5e_mkey_set_relaxed_ordering(struct mlx5_core_dev *mdev, void *mkc);

/* common netdev helpers */
void mlx5e_create_q_counters(struct mlx5e_priv *priv);
void mlx5e_destroy_q_counters(struct mlx5e_priv *priv);
int mlx5e_open_drop_rq(struct mlx5e_priv *priv,
		       struct mlx5e_rq *drop_rq);
void mlx5e_close_drop_rq(struct mlx5e_rq *drop_rq);

int mlx5e_create_tis(struct mlx5_core_dev *mdev, void *in, u32 *tisn);
void mlx5e_destroy_tis(struct mlx5_core_dev *mdev, u32 tisn);

void mlx5e_update_carrier(struct mlx5e_priv *priv);
int mlx5e_close(struct net_device *netdev);
int mlx5e_open(struct net_device *netdev);

void mlx5e_queue_update_stats(struct mlx5e_priv *priv);

int mlx5e_set_dev_port_mtu(struct mlx5e_priv *priv);
int mlx5e_set_dev_port_mtu_ctx(struct mlx5e_priv *priv, void *context);
int mlx5e_change_mtu(struct net_device *netdev, int new_mtu,
		     mlx5e_fp_preactivate preactivate);
void mlx5e_vxlan_set_netdev_info(struct mlx5e_priv *priv);

/* ethtool helpers */
void mlx5e_ethtool_get_drvinfo(struct mlx5e_priv *priv,
			       struct ethtool_drvinfo *drvinfo);
void mlx5e_ethtool_get_strings(struct mlx5e_priv *priv,
			       u32 stringset, u8 *data);
int mlx5e_ethtool_get_sset_count(struct mlx5e_priv *priv, int sset);
void mlx5e_ethtool_get_ethtool_stats(struct mlx5e_priv *priv,
				     struct ethtool_stats *stats, u64 *data);
void mlx5e_ethtool_get_ringparam(struct mlx5e_priv *priv,
				 struct ethtool_ringparam *param,
				 struct kernel_ethtool_ringparam *kernel_param);
int mlx5e_ethtool_set_ringparam(struct mlx5e_priv *priv,
				struct ethtool_ringparam *param,
				struct netlink_ext_ack *extack);
void mlx5e_ethtool_get_channels(struct mlx5e_priv *priv,
				struct ethtool_channels *ch);
int mlx5e_ethtool_set_channels(struct mlx5e_priv *priv,
			       struct ethtool_channels *ch);
int mlx5e_ethtool_get_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack);
int mlx5e_ethtool_set_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack);
int mlx5e_get_per_queue_coalesce(struct net_device *dev, u32 queue,
				 struct ethtool_coalesce *coal);
int mlx5e_set_per_queue_coalesce(struct net_device *dev, u32 queue,
				 struct ethtool_coalesce *coal);
u32 mlx5e_ethtool_get_rxfh_key_size(struct mlx5e_priv *priv);
u32 mlx5e_ethtool_get_rxfh_indir_size(struct mlx5e_priv *priv);
int mlx5e_ethtool_get_ts_info(struct mlx5e_priv *priv,
			      struct kernel_ethtool_ts_info *info);
int mlx5e_ethtool_flash_device(struct mlx5e_priv *priv,
			       struct ethtool_flash *flash);

/* mlx5e generic netdev management API */
static inline bool
mlx5e_tx_mpwqe_supported(struct mlx5_core_dev *mdev)
{
	return !is_kdump_kernel() &&
		MLX5_CAP_ETH(mdev, enhanced_multi_pkt_send_wqe);
}

int mlx5e_get_pf_num_tirs(struct mlx5_core_dev *mdev);
int mlx5e_priv_init(struct mlx5e_priv *priv,
		    const struct mlx5e_profile *profile,
		    struct net_device *netdev,
		    struct mlx5_core_dev *mdev);
void mlx5e_priv_cleanup(struct mlx5e_priv *priv);
struct net_device *
mlx5e_create_netdev(struct mlx5_core_dev *mdev, const struct mlx5e_profile *profile);
int mlx5e_attach_netdev(struct mlx5e_priv *priv);
void mlx5e_detach_netdev(struct mlx5e_priv *priv);
void mlx5e_destroy_netdev(struct mlx5e_priv *priv);
int mlx5e_netdev_change_profile(struct mlx5e_priv *priv,
				const struct mlx5e_profile *new_profile, void *new_ppriv);
void mlx5e_netdev_attach_nic_profile(struct mlx5e_priv *priv);
void mlx5e_set_netdev_mtu_boundaries(struct mlx5e_priv *priv);
void mlx5e_build_nic_params(struct mlx5e_priv *priv, struct mlx5e_xsk *xsk, u16 mtu);

void mlx5e_set_xdp_feature(struct net_device *netdev);
netdev_features_t mlx5e_features_check(struct sk_buff *skb,
				       struct net_device *netdev,
				       netdev_features_t features);
int mlx5e_set_features(struct net_device *netdev, netdev_features_t features);
#ifdef CONFIG_MLX5_ESWITCH
int mlx5e_set_vf_mac(struct net_device *dev, int vf, u8 *mac);
int mlx5e_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate, int max_tx_rate);
int mlx5e_get_vf_config(struct net_device *dev, int vf, struct ifla_vf_info *ivi);
int mlx5e_get_vf_stats(struct net_device *dev, int vf, struct ifla_vf_stats *vf_stats);
#endif
int mlx5e_create_mkey(struct mlx5_core_dev *mdev, u32 pdn, u32 *mkey);
#endif /* __MLX5_EN_H__ */
