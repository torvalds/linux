// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "en/params.h"
#include "en/txrx.h"
#include "en/port.h"
#include "en_accel/en_accel.h"
#include "en_accel/ipsec.h"
#include <net/xdp_sock_drv.h>

static u8 mlx5e_mpwrq_min_page_shift(struct mlx5_core_dev *mdev)
{
	u8 min_page_shift = MLX5_CAP_GEN_2(mdev, log_min_mkey_entity_size);

	return min_page_shift ? : 12;
}

u8 mlx5e_mpwrq_page_shift(struct mlx5_core_dev *mdev, struct mlx5e_xsk_param *xsk)
{
	u8 req_page_shift = xsk ? order_base_2(xsk->chunk_size) : PAGE_SHIFT;
	u8 min_page_shift = mlx5e_mpwrq_min_page_shift(mdev);

	/* Regular RQ uses order-0 pages, the NIC must be able to map them. */
	if (WARN_ON_ONCE(!xsk && req_page_shift < min_page_shift))
		min_page_shift = req_page_shift;

	return max(req_page_shift, min_page_shift);
}

enum mlx5e_mpwrq_umr_mode
mlx5e_mpwrq_umr_mode(struct mlx5_core_dev *mdev, struct mlx5e_xsk_param *xsk)
{
	/* Different memory management schemes use different mechanisms to map
	 * user-mode memory. The stricter guarantees we have, the faster
	 * mechanisms we use:
	 * 1. MTT - direct mapping in page granularity.
	 * 2. KSM - indirect mapping to another MKey to arbitrary addresses, but
	 *    all mappings have the same size.
	 * 3. KLM - indirect mapping to another MKey to arbitrary addresses, and
	 *    mappings can have different sizes.
	 */
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	bool unaligned = xsk ? xsk->unaligned : false;
	bool oversized = false;

	if (xsk) {
		oversized = xsk->chunk_size < (1 << page_shift);
		WARN_ON_ONCE(xsk->chunk_size > (1 << page_shift));
	}

	/* XSK frame size doesn't match the UMR page size, either because the
	 * frame size is not a power of two, or it's smaller than the minimal
	 * page size supported by the firmware.
	 * It's possible to receive packets bigger than MTU in certain setups.
	 * To avoid writing over the XSK frame boundary, the top region of each
	 * stride is mapped to a garbage page, resulting in two mappings of
	 * different sizes per frame.
	 */
	if (oversized) {
		/* An optimization for frame sizes equal to 3 * power_of_two.
		 * 3 KSMs point to the frame, and one KSM points to the garbage
		 * page, which works faster than KLM.
		 */
		if (xsk->chunk_size % 3 == 0 && is_power_of_2(xsk->chunk_size / 3))
			return MLX5E_MPWRQ_UMR_MODE_TRIPLE;

		return MLX5E_MPWRQ_UMR_MODE_OVERSIZED;
	}

	/* XSK frames can start at arbitrary unaligned locations, but they all
	 * have the same size which is a power of two. It allows to optimize to
	 * one KSM per frame.
	 */
	if (unaligned)
		return MLX5E_MPWRQ_UMR_MODE_UNALIGNED;

	/* XSK: frames are naturally aligned, MTT can be used.
	 * Non-XSK: Allocations happen in units of CPU pages, therefore, the
	 * mappings are naturally aligned.
	 */
	return MLX5E_MPWRQ_UMR_MODE_ALIGNED;
}

u8 mlx5e_mpwrq_umr_entry_size(enum mlx5e_mpwrq_umr_mode mode)
{
	switch (mode) {
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		return sizeof(struct mlx5_mtt);
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		return sizeof(struct mlx5_ksm);
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		return sizeof(struct mlx5_klm) * 2;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		return sizeof(struct mlx5_ksm) * 4;
	}
	WARN_ONCE(1, "MPWRQ UMR mode %d is not known\n", mode);
	return 0;
}

u8 mlx5e_mpwrq_log_wqe_sz(struct mlx5_core_dev *mdev, u8 page_shift,
			  enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 umr_entry_size = mlx5e_mpwrq_umr_entry_size(umr_mode);
	u8 max_pages_per_wqe, max_log_mpwqe_size;
	u16 max_wqe_size;

	/* Keep in sync with MLX5_MPWRQ_MAX_PAGES_PER_WQE. */
	max_wqe_size = mlx5e_get_max_sq_aligned_wqebbs(mdev) * MLX5_SEND_WQE_BB;
	max_pages_per_wqe = ALIGN_DOWN(max_wqe_size - sizeof(struct mlx5e_umr_wqe),
				       MLX5_UMR_FLEX_ALIGNMENT) / umr_entry_size;
	max_log_mpwqe_size = ilog2(max_pages_per_wqe) + page_shift;

	WARN_ON_ONCE(max_log_mpwqe_size < MLX5E_ORDER2_MAX_PACKET_MTU);

	return min_t(u8, max_log_mpwqe_size, MLX5_MPWRQ_MAX_LOG_WQE_SZ);
}

u8 mlx5e_mpwrq_pages_per_wqe(struct mlx5_core_dev *mdev, u8 page_shift,
			     enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 log_wqe_sz = mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode);
	u8 pages_per_wqe;

	pages_per_wqe = log_wqe_sz > page_shift ? (1 << (log_wqe_sz - page_shift)) : 1;

	/* Two MTTs are needed to form an octword. The number of MTTs is encoded
	 * in octwords in a UMR WQE, so we need at least two to avoid mapping
	 * garbage addresses.
	 */
	if (WARN_ON_ONCE(pages_per_wqe < 2 && umr_mode == MLX5E_MPWRQ_UMR_MODE_ALIGNED))
		pages_per_wqe = 2;

	/* Sanity check for further calculations to succeed. */
	BUILD_BUG_ON(MLX5_MPWRQ_MAX_PAGES_PER_WQE > 64);
	if (WARN_ON_ONCE(pages_per_wqe > MLX5_MPWRQ_MAX_PAGES_PER_WQE))
		return MLX5_MPWRQ_MAX_PAGES_PER_WQE;

	return pages_per_wqe;
}

u16 mlx5e_mpwrq_umr_wqe_sz(struct mlx5_core_dev *mdev, u8 page_shift,
			   enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 pages_per_wqe = mlx5e_mpwrq_pages_per_wqe(mdev, page_shift, umr_mode);
	u8 umr_entry_size = mlx5e_mpwrq_umr_entry_size(umr_mode);
	u16 umr_wqe_sz;

	umr_wqe_sz = sizeof(struct mlx5e_umr_wqe) +
		ALIGN(pages_per_wqe * umr_entry_size, MLX5_UMR_FLEX_ALIGNMENT);

	WARN_ON_ONCE(DIV_ROUND_UP(umr_wqe_sz, MLX5_SEND_WQE_DS) > MLX5_WQE_CTRL_DS_MASK);

	return umr_wqe_sz;
}

u8 mlx5e_mpwrq_umr_wqebbs(struct mlx5_core_dev *mdev, u8 page_shift,
			  enum mlx5e_mpwrq_umr_mode umr_mode)
{
	return DIV_ROUND_UP(mlx5e_mpwrq_umr_wqe_sz(mdev, page_shift, umr_mode),
			    MLX5_SEND_WQE_BB);
}

u8 mlx5e_mpwrq_mtts_per_wqe(struct mlx5_core_dev *mdev, u8 page_shift,
			    enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 pages_per_wqe = mlx5e_mpwrq_pages_per_wqe(mdev, page_shift, umr_mode);

	/* Add another page as a buffer between WQEs. This page will absorb
	 * write overflow by the hardware, when receiving packets larger than
	 * MTU. These oversize packets are dropped by the driver at a later
	 * stage.
	 */
	return ALIGN(pages_per_wqe + 1,
		     MLX5_SEND_WQE_BB / mlx5e_mpwrq_umr_entry_size(umr_mode));
}

u32 mlx5e_mpwrq_max_num_entries(struct mlx5_core_dev *mdev,
				enum mlx5e_mpwrq_umr_mode umr_mode)
{
	/* Same limits apply to KSMs and KLMs. */
	u32 klm_limit = min(MLX5E_MAX_RQ_NUM_KSMS,
			    1 << MLX5_CAP_GEN(mdev, log_max_klm_list_size));

	switch (umr_mode) {
	case MLX5E_MPWRQ_UMR_MODE_ALIGNED:
		return MLX5E_MAX_RQ_NUM_MTTS;
	case MLX5E_MPWRQ_UMR_MODE_UNALIGNED:
		return klm_limit;
	case MLX5E_MPWRQ_UMR_MODE_OVERSIZED:
		/* Each entry is two KLMs. */
		return klm_limit / 2;
	case MLX5E_MPWRQ_UMR_MODE_TRIPLE:
		/* Each entry is four KSMs. */
		return klm_limit / 4;
	}
	WARN_ONCE(1, "MPWRQ UMR mode %d is not known\n", umr_mode);
	return 0;
}

static u8 mlx5e_mpwrq_max_log_rq_size(struct mlx5_core_dev *mdev, u8 page_shift,
				      enum mlx5e_mpwrq_umr_mode umr_mode)
{
	u8 mtts_per_wqe = mlx5e_mpwrq_mtts_per_wqe(mdev, page_shift, umr_mode);
	u32 max_entries = mlx5e_mpwrq_max_num_entries(mdev, umr_mode);

	return ilog2(max_entries / mtts_per_wqe);
}

u8 mlx5e_mpwrq_max_log_rq_pkts(struct mlx5_core_dev *mdev, u8 page_shift,
			       enum mlx5e_mpwrq_umr_mode umr_mode)
{
	return mlx5e_mpwrq_max_log_rq_size(mdev, page_shift, umr_mode) +
		mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode) -
		MLX5E_ORDER2_MAX_PACKET_MTU;
}

u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
				 struct mlx5e_xsk_param *xsk)
{
	u16 headroom;

	if (xsk)
		return xsk->headroom;

	headroom = NET_IP_ALIGN;
	if (params->xdp_prog)
		headroom += XDP_PACKET_HEADROOM;
	else
		headroom += MLX5_RX_HEADROOM;

	return headroom;
}

static u32 mlx5e_rx_get_linear_sz_xsk(struct mlx5e_params *params,
				      struct mlx5e_xsk_param *xsk)
{
	u32 hw_mtu = MLX5E_SW2HW_MTU(params, params->sw_mtu);

	return xsk->headroom + hw_mtu;
}

static u32 mlx5e_rx_get_linear_sz_skb(struct mlx5e_params *params, bool xsk)
{
	/* SKBs built on XDP_PASS on XSK RQs don't have headroom. */
	u16 headroom = xsk ? 0 : mlx5e_get_linear_rq_headroom(params, NULL);
	u32 hw_mtu = MLX5E_SW2HW_MTU(params, params->sw_mtu);

	return MLX5_SKB_FRAG_SZ(headroom + hw_mtu);
}

static u32 mlx5e_rx_get_linear_stride_sz(struct mlx5_core_dev *mdev,
					 struct mlx5e_params *params,
					 struct mlx5e_xsk_param *xsk,
					 bool mpwqe)
{
	/* XSK frames are mapped as individual pages, because frames may come in
	 * an arbitrary order from random locations in the UMEM.
	 */
	if (xsk)
		return mpwqe ? 1 << mlx5e_mpwrq_page_shift(mdev, xsk) : PAGE_SIZE;

	/* XDP in mlx5e doesn't support multiple packets per page. */
	if (params->xdp_prog)
		return PAGE_SIZE;

	return roundup_pow_of_two(mlx5e_rx_get_linear_sz_skb(params, false));
}

static u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5_core_dev *mdev,
				       struct mlx5e_params *params,
				       struct mlx5e_xsk_param *xsk)
{
	u32 linear_stride_sz = mlx5e_rx_get_linear_stride_sz(mdev, params, xsk, true);
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);

	return mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode) -
		order_base_2(linear_stride_sz);
}

bool mlx5e_rx_is_linear_skb(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    struct mlx5e_xsk_param *xsk)
{
	if (params->packet_merge.type != MLX5E_PACKET_MERGE_NONE)
		return false;

	/* Both XSK and non-XSK cases allocate an SKB on XDP_PASS. Packet data
	 * must fit into a CPU page.
	 */
	if (mlx5e_rx_get_linear_sz_skb(params, xsk) > PAGE_SIZE)
		return false;

	/* XSK frames must be big enough to hold the packet data. */
	if (xsk && mlx5e_rx_get_linear_sz_xsk(params, xsk) > xsk->chunk_size)
		return false;

	return true;
}

static bool mlx5e_verify_rx_mpwqe_strides(struct mlx5_core_dev *mdev,
					  u8 log_stride_sz, u8 log_num_strides,
					  u8 page_shift,
					  enum mlx5e_mpwrq_umr_mode umr_mode)
{
	if (log_stride_sz + log_num_strides !=
	    mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode))
		return false;

	if (log_stride_sz < MLX5_MPWQE_LOG_STRIDE_SZ_BASE ||
	    log_stride_sz > MLX5_MPWQE_LOG_STRIDE_SZ_MAX)
		return false;

	if (log_num_strides > MLX5_MPWQE_LOG_NUM_STRIDES_MAX)
		return false;

	if (MLX5_CAP_GEN(mdev, ext_stride_num_range))
		return log_num_strides >= MLX5_MPWQE_LOG_NUM_STRIDES_EXT_BASE;

	return log_num_strides >= MLX5_MPWQE_LOG_NUM_STRIDES_BASE;
}

bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params,
				  struct mlx5e_xsk_param *xsk)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	u8 log_num_strides;
	u8 log_stride_sz;
	u8 log_wqe_sz;

	if (!mlx5e_rx_is_linear_skb(mdev, params, xsk))
		return false;

	log_stride_sz = order_base_2(mlx5e_rx_get_linear_stride_sz(mdev, params, xsk, true));
	log_wqe_sz = mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode);

	if (log_wqe_sz < log_stride_sz)
		return false;

	log_num_strides = log_wqe_sz - log_stride_sz;

	return mlx5e_verify_rx_mpwqe_strides(mdev, log_stride_sz,
					     log_num_strides, page_shift,
					     umr_mode);
}

u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params,
			       struct mlx5e_xsk_param *xsk)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 log_pkts_per_wqe, page_shift, max_log_rq_size;

	log_pkts_per_wqe = mlx5e_mpwqe_log_pkts_per_wqe(mdev, params, xsk);
	page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	max_log_rq_size = mlx5e_mpwrq_max_log_rq_size(mdev, page_shift, umr_mode);

	/* Numbers are unsigned, don't subtract to avoid underflow. */
	if (params->log_rq_mtu_frames <
	    log_pkts_per_wqe + MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW)
		return MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW;

	/* Ethtool's rx_max_pending is calculated for regular RQ, that uses
	 * pages of PAGE_SIZE. Max length of an XSK RQ might differ if it uses a
	 * frame size not equal to PAGE_SIZE.
	 * A stricter condition is checked in mlx5e_mpwrq_validate_xsk, WARN on
	 * unexpected failure.
	 */
	if (WARN_ON_ONCE(params->log_rq_mtu_frames > log_pkts_per_wqe + max_log_rq_size))
		return max_log_rq_size;

	return params->log_rq_mtu_frames - log_pkts_per_wqe;
}

u8 mlx5e_shampo_get_log_hd_entry_size(struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params)
{
	return order_base_2(DIV_ROUND_UP(MLX5E_RX_MAX_HEAD, MLX5E_SHAMPO_WQ_BASE_HEAD_ENTRY_SIZE));
}

u8 mlx5e_shampo_get_log_rsrv_size(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params)
{
	return order_base_2(MLX5E_SHAMPO_WQ_RESRV_SIZE / MLX5E_SHAMPO_WQ_BASE_RESRV_SIZE);
}

u8 mlx5e_shampo_get_log_pkt_per_rsrv(struct mlx5_core_dev *mdev,
				     struct mlx5e_params *params)
{
	u32 resrv_size = BIT(mlx5e_shampo_get_log_rsrv_size(mdev, params)) *
			 PAGE_SIZE;

	return order_base_2(DIV_ROUND_UP(resrv_size, params->sw_mtu));
}

u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	if (mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk))
		return order_base_2(mlx5e_rx_get_linear_stride_sz(mdev, params, xsk, true));

	return MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev);
}

u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	u8 log_wqe_size, log_stride_size;

	log_wqe_size = mlx5e_mpwrq_log_wqe_sz(mdev, page_shift, umr_mode);
	log_stride_size = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
	WARN(log_wqe_size < log_stride_size,
	     "Log WQE size %u < log stride size %u (page shift %u, umr mode %d, xsk on? %d)\n",
	     log_wqe_size, log_stride_size, page_shift, umr_mode, !!xsk);
	return log_wqe_size - log_stride_size;
}

u8 mlx5e_mpwqe_get_min_wqe_bulk(unsigned int wq_sz)
{
#define UMR_WQE_BULK (2)
	return min_t(unsigned int, UMR_WQE_BULK, wq_sz / 2 - 1);
}

u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk)
{
	u16 linear_headroom = mlx5e_get_linear_rq_headroom(params, xsk);

	if (params->rq_wq_type == MLX5_WQ_TYPE_CYCLIC)
		return linear_headroom;

	if (mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk))
		return linear_headroom;

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
		return linear_headroom;

	return 0;
}

u16 mlx5e_calc_sq_stop_room(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	bool is_mpwqe = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE);
	u16 stop_room;

	stop_room  = mlx5e_ktls_get_stop_room(mdev, params);
	stop_room += mlx5e_stop_room_for_max_wqe(mdev);
	if (is_mpwqe)
		/* A MPWQE can take up to the maximum cacheline-aligned WQE +
		 * all the normal stop room can be taken if a new packet breaks
		 * the active MPWQE session and allocates its WQEs right away.
		 */
		stop_room += mlx5e_stop_room_for_mpwqe(mdev);

	return stop_room;
}

int mlx5e_validate_params(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	size_t sq_size = 1 << params->log_sq_size;
	u16 stop_room;

	stop_room = mlx5e_calc_sq_stop_room(mdev, params);
	if (stop_room >= sq_size) {
		mlx5_core_err(mdev, "Stop room %u is bigger than the SQ size %zu\n",
			      stop_room, sq_size);
		return -EINVAL;
	}

	return 0;
}

static struct dim_cq_moder mlx5e_get_def_tx_moderation(u8 cq_period_mode)
{
	struct dim_cq_moder moder = {};

	moder.cq_period_mode = cq_period_mode;
	moder.pkts = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS;
	moder.usec = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC;
	if (cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		moder.usec = MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC_FROM_CQE;

	return moder;
}

static struct dim_cq_moder mlx5e_get_def_rx_moderation(u8 cq_period_mode)
{
	struct dim_cq_moder moder = {};

	moder.cq_period_mode = cq_period_mode;
	moder.pkts = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS;
	moder.usec = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC;
	if (cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE)
		moder.usec = MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE;

	return moder;
}

static u8 mlx5_to_net_dim_cq_period_mode(u8 cq_period_mode)
{
	return cq_period_mode == MLX5_CQ_PERIOD_MODE_START_FROM_CQE ?
		DIM_CQ_PERIOD_MODE_START_FROM_CQE :
		DIM_CQ_PERIOD_MODE_START_FROM_EQE;
}

void mlx5e_reset_tx_moderation(struct mlx5e_params *params, u8 cq_period_mode)
{
	if (params->tx_dim_enabled) {
		u8 dim_period_mode = mlx5_to_net_dim_cq_period_mode(cq_period_mode);

		params->tx_cq_moderation = net_dim_get_def_tx_moderation(dim_period_mode);
	} else {
		params->tx_cq_moderation = mlx5e_get_def_tx_moderation(cq_period_mode);
	}
}

void mlx5e_reset_rx_moderation(struct mlx5e_params *params, u8 cq_period_mode)
{
	if (params->rx_dim_enabled) {
		u8 dim_period_mode = mlx5_to_net_dim_cq_period_mode(cq_period_mode);

		params->rx_cq_moderation = net_dim_get_def_rx_moderation(dim_period_mode);
	} else {
		params->rx_cq_moderation = mlx5e_get_def_rx_moderation(cq_period_mode);
	}
}

void mlx5e_set_tx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode)
{
	mlx5e_reset_tx_moderation(params, cq_period_mode);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_TX_CQE_BASED_MODER,
			params->tx_cq_moderation.cq_period_mode ==
				MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
}

void mlx5e_set_rx_cq_mode_params(struct mlx5e_params *params, u8 cq_period_mode)
{
	mlx5e_reset_rx_moderation(params, cq_period_mode);
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_CQE_BASED_MODER,
			params->rx_cq_moderation.cq_period_mode ==
				MLX5_CQ_PERIOD_MODE_START_FROM_CQE);
}

bool slow_pci_heuristic(struct mlx5_core_dev *mdev)
{
	u32 link_speed = 0;
	u32 pci_bw = 0;

	mlx5e_port_max_linkspeed(mdev, &link_speed);
	pci_bw = pcie_bandwidth_available(mdev->pdev, NULL, NULL, NULL);
	mlx5_core_dbg_once(mdev, "Max link speed = %d, PCI BW = %d\n",
			   link_speed, pci_bw);

#define MLX5E_SLOW_PCI_RATIO (2)

	return link_speed && pci_bw &&
		link_speed > MLX5E_SLOW_PCI_RATIO * pci_bw;
}

int mlx5e_mpwrq_validate_regular(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, NULL);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, NULL);

	if (!mlx5e_check_fragmented_striding_rq_cap(mdev, page_shift, umr_mode))
		return -EOPNOTSUPP;

	if (params->xdp_prog && !mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL))
		return -EINVAL;

	return 0;
}

int mlx5e_mpwrq_validate_xsk(struct mlx5_core_dev *mdev, struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	u16 max_mtu_pkts;

	if (!mlx5e_check_fragmented_striding_rq_cap(mdev, page_shift, umr_mode)) {
		mlx5_core_err(mdev, "Striding RQ for XSK can't be activated with page_shift %u and umr_mode %d\n",
			      page_shift, umr_mode);
		return -EOPNOTSUPP;
	}

	if (!mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk)) {
		mlx5_core_err(mdev, "Striding RQ linear mode for XSK can't be activated with current params\n");
		return -EINVAL;
	}

	/* Current RQ length is too big for the given frame size, the
	 * needed number of WQEs exceeds the maximum.
	 */
	max_mtu_pkts = min_t(u8, MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE,
			     mlx5e_mpwrq_max_log_rq_pkts(mdev, page_shift, xsk->unaligned));
	if (params->log_rq_mtu_frames > max_mtu_pkts) {
		mlx5_core_err(mdev, "Current RQ length %d is too big for XSK with given frame size %u\n",
			      1 << params->log_rq_mtu_frames, xsk->chunk_size);
		return -EINVAL;
	}

	return 0;
}

void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params)
{
	params->log_rq_mtu_frames = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;
}

void mlx5e_set_rq_type(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	params->rq_wq_type = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ) ?
		MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ :
		MLX5_WQ_TYPE_CYCLIC;
}

void mlx5e_build_rq_params(struct mlx5_core_dev *mdev,
			   struct mlx5e_params *params)
{
	/* Prefer Striding RQ, unless any of the following holds:
	 * - Striding RQ configuration is not possible/supported.
	 * - CQE compression is ON, and stride_index mini_cqe layout is not supported.
	 * - Legacy RQ would use linear SKB while Striding RQ would use non-linear.
	 *
	 * No XSK params: checking the availability of striding RQ in general.
	 */
	if ((!MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS) ||
	     MLX5_CAP_GEN(mdev, mini_cqe_resp_stride_index)) &&
	    !mlx5e_mpwrq_validate_regular(mdev, params) &&
	    (mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL) ||
	     !mlx5e_rx_is_linear_skb(mdev, params, NULL)))
		MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ, true);
	mlx5e_set_rq_type(mdev, params);
	mlx5e_init_rq_type_params(mdev, params);
}

/* Build queue parameters */

void mlx5e_build_create_cq_param(struct mlx5e_create_cq_param *ccp, struct mlx5e_channel *c)
{
	*ccp = (struct mlx5e_create_cq_param) {
		.napi = &c->napi,
		.ch_stats = c->stats,
		.node = cpu_to_node(c->cpu),
		.ix = c->ix,
	};
}

static int mlx5e_max_nonlinear_mtu(int first_frag_size, int frag_size, bool xdp)
{
	if (xdp)
		/* XDP requires all fragments to be of the same size. */
		return first_frag_size + (MLX5E_MAX_RX_FRAGS - 1) * frag_size;

	/* Optimization for small packets: the last fragment is bigger than the others. */
	return first_frag_size + (MLX5E_MAX_RX_FRAGS - 2) * frag_size + PAGE_SIZE;
}

#define DEFAULT_FRAG_SIZE (2048)

static int mlx5e_build_rq_frags_info(struct mlx5_core_dev *mdev,
				     struct mlx5e_params *params,
				     struct mlx5e_xsk_param *xsk,
				     struct mlx5e_rq_frags_info *info)
{
	u32 byte_count = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	int frag_size_max = DEFAULT_FRAG_SIZE;
	int first_frag_size_max;
	u32 buf_size = 0;
	u16 headroom;
	int max_mtu;
	int i;

	if (mlx5e_rx_is_linear_skb(mdev, params, xsk)) {
		int frag_stride;

		frag_stride = mlx5e_rx_get_linear_stride_sz(mdev, params, xsk, false);

		info->arr[0].frag_size = byte_count;
		info->arr[0].frag_stride = frag_stride;
		info->num_frags = 1;

		/* N WQEs share the same page, N = PAGE_SIZE / frag_stride. The
		 * first WQE in the page is responsible for allocation of this
		 * page, this WQE's index is k*N. If WQEs [k*N+1; k*N+N-1] are
		 * still not completed, the allocation must stop before k*N.
		 */
		info->wqe_index_mask = (PAGE_SIZE / frag_stride) - 1;

		goto out;
	}

	headroom = mlx5e_get_linear_rq_headroom(params, xsk);
	first_frag_size_max = SKB_WITH_OVERHEAD(frag_size_max - headroom);

	max_mtu = mlx5e_max_nonlinear_mtu(first_frag_size_max, frag_size_max,
					  params->xdp_prog);
	if (byte_count > max_mtu || params->xdp_prog) {
		frag_size_max = PAGE_SIZE;
		first_frag_size_max = SKB_WITH_OVERHEAD(frag_size_max - headroom);

		max_mtu = mlx5e_max_nonlinear_mtu(first_frag_size_max, frag_size_max,
						  params->xdp_prog);
		if (byte_count > max_mtu) {
			mlx5_core_err(mdev, "MTU %u is too big for non-linear legacy RQ (max %d)\n",
				      params->sw_mtu, max_mtu);
			return -EINVAL;
		}
	}

	i = 0;
	while (buf_size < byte_count) {
		int frag_size = byte_count - buf_size;

		if (i == 0)
			frag_size = min(frag_size, first_frag_size_max);
		else if (i < MLX5E_MAX_RX_FRAGS - 1)
			frag_size = min(frag_size, frag_size_max);

		info->arr[i].frag_size = frag_size;
		buf_size += frag_size;

		if (params->xdp_prog) {
			/* XDP multi buffer expects fragments of the same size. */
			info->arr[i].frag_stride = frag_size_max;
		} else {
			if (i == 0) {
				/* Ensure that headroom and tailroom are included. */
				frag_size += headroom;
				frag_size += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
			}
			info->arr[i].frag_stride = roundup_pow_of_two(frag_size);
		}

		i++;
	}
	info->num_frags = i;

	/* The last fragment of WQE with index 2*N may share the page with the
	 * first fragment of WQE with index 2*N+1 in certain cases. If WQE 2*N+1
	 * is not completed yet, WQE 2*N must not be allocated, as it's
	 * responsible for allocating a new page.
	 */
	if (frag_size_max == PAGE_SIZE) {
		/* No WQE can start in the middle of a page. */
		info->wqe_index_mask = 0;
	} else {
		/* PAGE_SIZEs starting from 8192 don't use 2K-sized fragments,
		 * because there would be more than MLX5E_MAX_RX_FRAGS of them.
		 */
		WARN_ON(PAGE_SIZE != 2 * DEFAULT_FRAG_SIZE);

		/* Odd number of fragments allows to pack the last fragment of
		 * the previous WQE and the first fragment of the next WQE into
		 * the same page.
		 * As long as DEFAULT_FRAG_SIZE is 2048, and MLX5E_MAX_RX_FRAGS
		 * is 4, the last fragment can be bigger than the rest only if
		 * it's the fourth one, so WQEs consisting of 3 fragments will
		 * always share a page.
		 * When a page is shared, WQE bulk size is 2, otherwise just 1.
		 */
		info->wqe_index_mask = info->num_frags % 2;
	}

out:
	/* Bulking optimization to skip allocation until at least 8 WQEs can be
	 * allocated in a row. At the same time, never start allocation when
	 * the page is still used by older WQEs.
	 */
	info->wqe_bulk = max_t(u8, info->wqe_index_mask + 1, 8);

	info->log_num_frags = order_base_2(info->num_frags);

	return 0;
}

static u8 mlx5e_get_rqwq_log_stride(u8 wq_type, int ndsegs)
{
	int sz = sizeof(struct mlx5_wqe_data_seg) * ndsegs;

	switch (wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		sz += sizeof(struct mlx5e_rx_wqe_ll);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		sz += sizeof(struct mlx5e_rx_wqe_cyc);
	}

	return order_base_2(sz);
}

static void mlx5e_build_common_cq_param(struct mlx5_core_dev *mdev,
					struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, uar_page, mdev->priv.uar->index);
	if (MLX5_CAP_GEN(mdev, cqe_128_always) && cache_line_size() >= 128)
		MLX5_SET(cqc, cqc, cqe_sz, CQE_STRIDE_128_PAD);
}

static u32 mlx5e_shampo_get_log_cq_size(struct mlx5_core_dev *mdev,
					struct mlx5e_params *params,
					struct mlx5e_xsk_param *xsk)
{
	int rsrv_size = BIT(mlx5e_shampo_get_log_rsrv_size(mdev, params)) * PAGE_SIZE;
	u16 num_strides = BIT(mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk));
	int pkt_per_rsrv = BIT(mlx5e_shampo_get_log_pkt_per_rsrv(mdev, params));
	u8 log_stride_sz = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
	int wq_size = BIT(mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk));
	int wqe_size = BIT(log_stride_sz) * num_strides;

	/* +1 is for the case that the pkt_per_rsrv dont consume the reservation
	 * so we get a filler cqe for the rest of the reservation.
	 */
	return order_base_2((wqe_size / rsrv_size) * wq_size * (pkt_per_rsrv + 1));
}

static void mlx5e_build_rx_cq_param(struct mlx5_core_dev *mdev,
				    struct mlx5e_params *params,
				    struct mlx5e_xsk_param *xsk,
				    struct mlx5e_cq_param *param)
{
	bool hw_stridx = false;
	void *cqc = param->cqc;
	u8 log_cq_size;

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		hw_stridx = MLX5_CAP_GEN(mdev, mini_cqe_resp_stride_index);
		if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
			log_cq_size = mlx5e_shampo_get_log_cq_size(mdev, params, xsk);
		else
			log_cq_size = mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk) +
				mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		log_cq_size = params->log_rq_mtu_frames;
	}

	MLX5_SET(cqc, cqc, log_cq_size, log_cq_size);
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		MLX5_SET(cqc, cqc, mini_cqe_res_format, hw_stridx ?
			 MLX5_CQE_FORMAT_CSUM_STRIDX : MLX5_CQE_FORMAT_CSUM);
		MLX5_SET(cqc, cqc, cqe_compression_layout,
			 MLX5_CAP_GEN(mdev, enhanced_cqe_compression) ?
			 MLX5_CQE_COMPRESS_LAYOUT_ENHANCED :
			 MLX5_CQE_COMPRESS_LAYOUT_BASIC);
		MLX5_SET(cqc, cqc, cqe_comp_en, 1);
	}

	mlx5e_build_common_cq_param(mdev, param);
	param->cq_period_mode = params->rx_cq_moderation.cq_period_mode;
}

static u8 rq_end_pad_mode(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	bool lro_en = params->packet_merge.type == MLX5E_PACKET_MERGE_LRO;
	bool ro = pcie_relaxed_ordering_enabled(mdev->pdev) &&
		MLX5_CAP_GEN(mdev, relaxed_ordering_write);

	return ro && lro_en ?
		MLX5_WQ_END_PAD_MODE_NONE : MLX5_WQ_END_PAD_MODE_ALIGN;
}

int mlx5e_build_rq_param(struct mlx5_core_dev *mdev,
			 struct mlx5e_params *params,
			 struct mlx5e_xsk_param *xsk,
			 u16 q_counter,
			 struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);
	int ndsegs = 1;
	int err;

	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ: {
		u8 log_wqe_num_of_strides = mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk);
		u8 log_wqe_stride_size = mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
		enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
		u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);

		if (!mlx5e_verify_rx_mpwqe_strides(mdev, log_wqe_stride_size,
						   log_wqe_num_of_strides,
						   page_shift, umr_mode)) {
			mlx5_core_err(mdev,
				      "Bad RX MPWQE params: log_stride_size %u, log_num_strides %u, umr_mode %d\n",
				      log_wqe_stride_size, log_wqe_num_of_strides,
				      umr_mode);
			return -EINVAL;
		}

		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 log_wqe_num_of_strides - MLX5_MPWQE_LOG_NUM_STRIDES_BASE);
		MLX5_SET(wq, wq, log_wqe_stride_size,
			 log_wqe_stride_size - MLX5_MPWQE_LOG_STRIDE_SZ_BASE);
		MLX5_SET(wq, wq, log_wq_sz, mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk));
		if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO) {
			MLX5_SET(wq, wq, shampo_enable, true);
			MLX5_SET(wq, wq, log_reservation_size,
				 mlx5e_shampo_get_log_rsrv_size(mdev, params));
			MLX5_SET(wq, wq,
				 log_max_num_of_packets_per_reservation,
				 mlx5e_shampo_get_log_pkt_per_rsrv(mdev, params));
			MLX5_SET(wq, wq, log_headers_entry_size,
				 mlx5e_shampo_get_log_hd_entry_size(mdev, params));
			MLX5_SET(rqc, rqc, reservation_timeout,
				 params->packet_merge.timeout);
			MLX5_SET(rqc, rqc, shampo_match_criteria_type,
				 params->packet_merge.shampo.match_criteria_type);
			MLX5_SET(rqc, rqc, shampo_no_match_alignment_granularity,
				 params->packet_merge.shampo.alignment_granularity);
		}
		break;
	}
	default: /* MLX5_WQ_TYPE_CYCLIC */
		MLX5_SET(wq, wq, log_wq_sz, params->log_rq_mtu_frames);
		err = mlx5e_build_rq_frags_info(mdev, params, xsk, &param->frags_info);
		if (err)
			return err;
		ndsegs = param->frags_info.num_frags;
	}

	MLX5_SET(wq, wq, wq_type,          params->rq_wq_type);
	MLX5_SET(wq, wq, end_padding_mode, rq_end_pad_mode(mdev, params));
	MLX5_SET(wq, wq, log_wq_stride,
		 mlx5e_get_rqwq_log_stride(params->rq_wq_type, ndsegs));
	MLX5_SET(wq, wq, pd,               mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET(rqc, rqc, counter_set_id, q_counter);
	MLX5_SET(rqc, rqc, vsd,            params->vlan_strip_disable);
	MLX5_SET(rqc, rqc, scatter_fcs,    params->scatter_fcs_en);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
	mlx5e_build_rx_cq_param(mdev, params, xsk, &param->cqp);

	return 0;
}

void mlx5e_build_drop_rq_param(struct mlx5_core_dev *mdev,
			       u16 q_counter,
			       struct mlx5e_rq_param *param)
{
	void *rqc = param->rqc;
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq, wq, log_wq_stride,
		 mlx5e_get_rqwq_log_stride(MLX5_WQ_TYPE_CYCLIC, 1));
	MLX5_SET(rqc, rqc, counter_set_id, q_counter);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
}

void mlx5e_build_tx_cq_param(struct mlx5_core_dev *mdev,
			     struct mlx5e_params *params,
			     struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, params->log_sq_size);

	mlx5e_build_common_cq_param(mdev, param);
	param->cq_period_mode = params->tx_cq_moderation.cq_period_mode;
}

void mlx5e_build_sq_param_common(struct mlx5_core_dev *mdev,
				 struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd,            mdev->mlx5e_res.hw_objs.pdn);

	param->wq.buf_numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
}

void mlx5e_build_sq_param(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);
	bool allow_swp;

	allow_swp =
		mlx5_geneve_tx_allowed(mdev) || !!mlx5_ipsec_device_caps(mdev);
	mlx5e_build_sq_param_common(mdev, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	MLX5_SET(sqc, sqc, allow_swp, allow_swp);
	param->is_mpw = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE);
	param->stop_room = mlx5e_calc_sq_stop_room(mdev, params);
	mlx5e_build_tx_cq_param(mdev, params, &param->cqp);
}

static void mlx5e_build_ico_cq_param(struct mlx5_core_dev *mdev,
				     u8 log_wq_size,
				     struct mlx5e_cq_param *param)
{
	void *cqc = param->cqc;

	MLX5_SET(cqc, cqc, log_cq_size, log_wq_size);

	mlx5e_build_common_cq_param(mdev, param);

	param->cq_period_mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
}

/* This function calculates the maximum number of headers entries that are needed
 * per WQE, the formula is based on the size of the reservations and the
 * restriction we have about max packets for reservation that is equal to max
 * headers per reservation.
 */
u32 mlx5e_shampo_hd_per_wqe(struct mlx5_core_dev *mdev,
			    struct mlx5e_params *params,
			    struct mlx5e_rq_param *rq_param)
{
	int resv_size = BIT(mlx5e_shampo_get_log_rsrv_size(mdev, params)) * PAGE_SIZE;
	u16 num_strides = BIT(mlx5e_mpwqe_get_log_num_strides(mdev, params, NULL));
	int pkt_per_resv = BIT(mlx5e_shampo_get_log_pkt_per_rsrv(mdev, params));
	u8 log_stride_sz = mlx5e_mpwqe_get_log_stride_size(mdev, params, NULL);
	int wqe_size = BIT(log_stride_sz) * num_strides;
	u32 hd_per_wqe;

	/* Assumption: hd_per_wqe % 8 == 0. */
	hd_per_wqe = (wqe_size / resv_size) * pkt_per_resv;
	mlx5_core_dbg(mdev, "%s hd_per_wqe = %d rsrv_size = %d wqe_size = %d pkt_per_resv = %d\n",
		      __func__, hd_per_wqe, resv_size, wqe_size, pkt_per_resv);
	return hd_per_wqe;
}

/* This function calculates the maximum number of headers entries that are needed
 * for the WQ, this value is uesed to allocate the header buffer in HW, thus
 * must be a pow of 2.
 */
u32 mlx5e_shampo_hd_per_wq(struct mlx5_core_dev *mdev,
			   struct mlx5e_params *params,
			   struct mlx5e_rq_param *rq_param)
{
	void *wqc = MLX5_ADDR_OF(rqc, rq_param->rqc, wq);
	int wq_size = BIT(MLX5_GET(wq, wqc, log_wq_sz));
	u32 hd_per_wqe, hd_per_wq;

	hd_per_wqe = mlx5e_shampo_hd_per_wqe(mdev, params, rq_param);
	hd_per_wq = roundup_pow_of_two(hd_per_wqe * wq_size);
	return hd_per_wq;
}

static u32 mlx5e_shampo_icosq_sz(struct mlx5_core_dev *mdev,
				 struct mlx5e_params *params,
				 struct mlx5e_rq_param *rq_param)
{
	int max_num_of_umr_per_wqe, max_hd_per_wqe, max_klm_per_umr, rest;
	void *wqc = MLX5_ADDR_OF(rqc, rq_param->rqc, wq);
	int wq_size = BIT(MLX5_GET(wq, wqc, log_wq_sz));
	u32 wqebbs;

	max_klm_per_umr = MLX5E_MAX_KLM_PER_WQE(mdev);
	max_hd_per_wqe = mlx5e_shampo_hd_per_wqe(mdev, params, rq_param);
	max_num_of_umr_per_wqe = max_hd_per_wqe / max_klm_per_umr;
	rest = max_hd_per_wqe % max_klm_per_umr;
	wqebbs = MLX5E_KLM_UMR_WQEBBS(max_klm_per_umr) * max_num_of_umr_per_wqe;
	if (rest)
		wqebbs += MLX5E_KLM_UMR_WQEBBS(rest);
	wqebbs *= wq_size;
	return wqebbs;
}

static u32 mlx5e_mpwrq_total_umr_wqebbs(struct mlx5_core_dev *mdev,
					struct mlx5e_params *params,
					struct mlx5e_xsk_param *xsk)
{
	enum mlx5e_mpwrq_umr_mode umr_mode = mlx5e_mpwrq_umr_mode(mdev, xsk);
	u8 page_shift = mlx5e_mpwrq_page_shift(mdev, xsk);
	u8 umr_wqebbs;

	umr_wqebbs = mlx5e_mpwrq_umr_wqebbs(mdev, page_shift, umr_mode);

	return umr_wqebbs * (1 << mlx5e_mpwqe_get_log_rq_size(mdev, params, xsk));
}

static u8 mlx5e_build_icosq_log_wq_sz(struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params,
				      struct mlx5e_rq_param *rqp)
{
	u32 wqebbs, total_pages, useful_space;

	/* MLX5_WQ_TYPE_CYCLIC */
	if (params->rq_wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;

	/* UMR WQEs for the regular RQ. */
	wqebbs = mlx5e_mpwrq_total_umr_wqebbs(mdev, params, NULL);

	/* If XDP program is attached, XSK may be turned on at any time without
	 * restarting the channel. ICOSQ must be big enough to fit UMR WQEs of
	 * both regular RQ and XSK RQ.
	 *
	 * XSK uses different values of page_shift, and the total number of UMR
	 * WQEBBs depends on it. This dependency is complex and not monotonic,
	 * especially taking into consideration that some of the parameters come
	 * from capabilities. Hence, we have to try all valid values of XSK
	 * frame size (and page_shift) to find the maximum.
	 */
	if (params->xdp_prog) {
		u32 max_xsk_wqebbs = 0;
		u8 frame_shift;

		for (frame_shift = XDP_UMEM_MIN_CHUNK_SHIFT;
		     frame_shift <= PAGE_SHIFT; frame_shift++) {
			/* The headroom doesn't affect the calculation. */
			struct mlx5e_xsk_param xsk = {
				.chunk_size = 1 << frame_shift,
				.unaligned = false,
			};

			/* XSK aligned mode. */
			max_xsk_wqebbs = max(max_xsk_wqebbs,
				mlx5e_mpwrq_total_umr_wqebbs(mdev, params, &xsk));

			/* XSK unaligned mode, frame size is a power of two. */
			xsk.unaligned = true;
			max_xsk_wqebbs = max(max_xsk_wqebbs,
				mlx5e_mpwrq_total_umr_wqebbs(mdev, params, &xsk));

			/* XSK unaligned mode, frame size is not equal to stride size. */
			xsk.chunk_size -= 1;
			max_xsk_wqebbs = max(max_xsk_wqebbs,
				mlx5e_mpwrq_total_umr_wqebbs(mdev, params, &xsk));

			/* XSK unaligned mode, frame size is a triple power of two. */
			xsk.chunk_size = (1 << frame_shift) / 4 * 3;
			max_xsk_wqebbs = max(max_xsk_wqebbs,
				mlx5e_mpwrq_total_umr_wqebbs(mdev, params, &xsk));
		}

		wqebbs += max_xsk_wqebbs;
	}

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
		wqebbs += mlx5e_shampo_icosq_sz(mdev, params, rqp);

	/* UMR WQEs don't cross the page boundary, they are padded with NOPs.
	 * This padding is always smaller than the max WQE size. That gives us
	 * at least (PAGE_SIZE - (max WQE size - MLX5_SEND_WQE_BB)) useful bytes
	 * per page. The number of pages is estimated as the total size of WQEs
	 * divided by the useful space in page, rounding up. If some WQEs don't
	 * fully fit into the useful space, they can occupy part of the padding,
	 * which proves this estimation to be correct (reserve enough space).
	 */
	useful_space = PAGE_SIZE - mlx5e_get_max_sq_wqebbs(mdev) + MLX5_SEND_WQE_BB;
	total_pages = DIV_ROUND_UP(wqebbs * MLX5_SEND_WQE_BB, useful_space);
	wqebbs = total_pages * (PAGE_SIZE / MLX5_SEND_WQE_BB);

	return max_t(u8, MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE, order_base_2(wqebbs));
}

static u8 mlx5e_build_async_icosq_log_wq_sz(struct mlx5_core_dev *mdev)
{
	if (mlx5e_is_ktls_rx(mdev))
		return MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;

	return MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;
}

static void mlx5e_build_icosq_param(struct mlx5_core_dev *mdev,
				    u8 log_wq_size,
				    struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(mdev, param);

	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(mdev, reg_umr_sq));
	mlx5e_build_ico_cq_param(mdev, log_wq_size, &param->cqp);
}

static void mlx5e_build_async_icosq_param(struct mlx5_core_dev *mdev,
					  u8 log_wq_size,
					  struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(mdev, param);
	param->stop_room = mlx5e_stop_room_for_wqe(mdev, 1); /* for XSK NOP */
	param->is_tls = mlx5e_is_ktls_rx(mdev);
	if (param->is_tls)
		param->stop_room += mlx5e_stop_room_for_wqe(mdev, 1); /* for TLS RX resync NOP */
	MLX5_SET(sqc, sqc, reg_umr, MLX5_CAP_ETH(mdev, reg_umr_sq));
	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
	mlx5e_build_ico_cq_param(mdev, log_wq_size, &param->cqp);
}

void mlx5e_build_xdpsq_param(struct mlx5_core_dev *mdev,
			     struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk,
			     struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(mdev, param);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	param->is_mpw = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_XDP_TX_MPWQE);
	param->is_xdp_mb = !mlx5e_rx_is_linear_skb(mdev, params, xsk);
	mlx5e_build_tx_cq_param(mdev, params, &param->cqp);
}

int mlx5e_build_channel_param(struct mlx5_core_dev *mdev,
			      struct mlx5e_params *params,
			      u16 q_counter,
			      struct mlx5e_channel_param *cparam)
{
	u8 icosq_log_wq_sz, async_icosq_log_wq_sz;
	int err;

	err = mlx5e_build_rq_param(mdev, params, NULL, q_counter, &cparam->rq);
	if (err)
		return err;

	icosq_log_wq_sz = mlx5e_build_icosq_log_wq_sz(mdev, params, &cparam->rq);
	async_icosq_log_wq_sz = mlx5e_build_async_icosq_log_wq_sz(mdev);

	mlx5e_build_sq_param(mdev, params, &cparam->txq_sq);
	mlx5e_build_xdpsq_param(mdev, params, NULL, &cparam->xdp_sq);
	mlx5e_build_icosq_param(mdev, icosq_log_wq_sz, &cparam->icosq);
	mlx5e_build_async_icosq_param(mdev, async_icosq_log_wq_sz, &cparam->async_icosq);

	return 0;
}
