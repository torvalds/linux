// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "en/params.h"
#include "en/txrx.h"
#include "en/port.h"
#include "en_accel/en_accel.h"
#include "en_accel/ipsec.h"

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

static u32 mlx5e_rx_get_linear_stride_sz(struct mlx5e_params *params,
					 struct mlx5e_xsk_param *xsk)
{
	/* XSK frames are mapped as individual pages, because frames may come in
	 * an arbitrary order from random locations in the UMEM.
	 */
	if (xsk)
		return PAGE_SIZE;

	/* XDP in mlx5e doesn't support multiple packets per page. */
	if (params->xdp_prog)
		return PAGE_SIZE;

	return roundup_pow_of_two(mlx5e_rx_get_linear_sz_skb(params, false));
}

static u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params,
				       struct mlx5e_xsk_param *xsk)
{
	u32 linear_stride_sz = mlx5e_rx_get_linear_stride_sz(params, xsk);

	return MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(linear_stride_sz);
}

bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params,
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
					  u8 log_stride_sz, u8 log_num_strides)
{
	if (log_stride_sz + log_num_strides != MLX5_MPWRQ_LOG_WQE_SZ)
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
	s8 log_num_strides;
	u8 log_stride_sz;

	if (!mlx5e_rx_is_linear_skb(params, xsk))
		return false;

	log_stride_sz = order_base_2(mlx5e_rx_get_linear_stride_sz(params, xsk));
	log_num_strides = MLX5_MPWRQ_LOG_WQE_SZ - log_stride_sz;

	return mlx5e_verify_rx_mpwqe_strides(mdev, log_stride_sz, log_num_strides);
}

u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5e_params *params,
			       struct mlx5e_xsk_param *xsk)
{
	u8 log_pkts_per_wqe = mlx5e_mpwqe_log_pkts_per_wqe(params, xsk);

	/* Numbers are unsigned, don't subtract to avoid underflow. */
	if (params->log_rq_mtu_frames <
	    log_pkts_per_wqe + MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW)
		return MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW;

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
		return order_base_2(mlx5e_rx_get_linear_stride_sz(params, xsk));

	return MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev);
}

u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	return MLX5_MPWRQ_LOG_WQE_SZ -
		mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
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
	if (!mlx5e_check_fragmented_striding_rq_cap(mdev))
		return -EOPNOTSUPP;

	if (params->xdp_prog && !mlx5e_rx_mpwqe_is_linear_skb(mdev, params, NULL))
		return -EINVAL;

	return 0;
}

int mlx5e_mpwrq_validate_xsk(struct mlx5_core_dev *mdev, struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk)
{
	if (!mlx5e_check_fragmented_striding_rq_cap(mdev))
		return -EOPNOTSUPP;

	if (!mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk))
		return -EINVAL;

	return 0;
}

void mlx5e_init_rq_type_params(struct mlx5_core_dev *mdev,
			       struct mlx5e_params *params)
{
	params->log_rq_mtu_frames = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE;

	mlx5_core_info(mdev, "MLX5E: StrdRq(%d) RqSz(%ld) StrdSz(%ld) RxCqeCmprss(%d)\n",
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ,
		       params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ ?
		       BIT(mlx5e_mpwqe_get_log_rq_size(params, NULL)) :
		       BIT(params->log_rq_mtu_frames),
		       BIT(mlx5e_mpwqe_get_log_stride_size(mdev, params, NULL)),
		       MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS));
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
	     !mlx5e_rx_is_linear_skb(params, NULL)))
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

	if (mlx5e_rx_is_linear_skb(params, xsk)) {
		int frag_stride;

		frag_stride = mlx5e_rx_get_linear_stride_sz(params, xsk);

		info->arr[0].frag_size = byte_count;
		info->arr[0].frag_stride = frag_stride;
		info->num_frags = 1;
		info->wqe_bulk = PAGE_SIZE / frag_stride;
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
	/* number of different wqes sharing a page */
	info->wqe_bulk = 1 + (info->num_frags % 2);

out:
	info->wqe_bulk = max_t(u8, info->wqe_bulk, 8);
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
	int wq_size = BIT(mlx5e_mpwqe_get_log_rq_size(params, xsk));
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
			log_cq_size = mlx5e_mpwqe_get_log_rq_size(params, xsk) +
				mlx5e_mpwqe_get_log_num_strides(mdev, params, xsk);
		break;
	default: /* MLX5_WQ_TYPE_CYCLIC */
		log_cq_size = params->log_rq_mtu_frames;
	}

	MLX5_SET(cqc, cqc, log_cq_size, log_cq_size);
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_RX_CQE_COMPRESS)) {
		MLX5_SET(cqc, cqc, mini_cqe_res_format, hw_stridx ?
			 MLX5_CQE_FORMAT_CSUM_STRIDX : MLX5_CQE_FORMAT_CSUM);
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

		if (!mlx5e_verify_rx_mpwqe_strides(mdev, log_wqe_stride_size,
						   log_wqe_num_of_strides)) {
			mlx5_core_err(mdev,
				      "Bad RX MPWQE params: log_stride_size %u, log_num_strides %u\n",
				      log_wqe_stride_size, log_wqe_num_of_strides);
			return -EINVAL;
		}

		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 log_wqe_num_of_strides - MLX5_MPWQE_LOG_NUM_STRIDES_BASE);
		MLX5_SET(wq, wq, log_wqe_stride_size,
			 log_wqe_stride_size - MLX5_MPWQE_LOG_STRIDE_SZ_BASE);
		MLX5_SET(wq, wq, log_wq_sz, mlx5e_mpwqe_get_log_rq_size(params, xsk));
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

static u8 mlx5e_get_rq_log_wq_sz(void *rqc)
{
	void *wq = MLX5_ADDR_OF(rqc, rqc, wq);

	return MLX5_GET(wq, wq, log_wq_sz);
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

static u8 mlx5e_build_icosq_log_wq_sz(struct mlx5_core_dev *mdev,
				      struct mlx5e_params *params,
				      struct mlx5e_rq_param *rqp)
{
	u32 wqebbs;

	/* MLX5_WQ_TYPE_CYCLIC */
	if (params->rq_wq_type != MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ)
		return MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;

	wqebbs = MLX5E_UMR_WQEBBS * BIT(mlx5e_get_rq_log_wq_sz(rqp->rqc));

	/* If XDP program is attached, XSK may be turned on at any time without
	 * restarting the channel. ICOSQ must be big enough to fit UMR WQEs of
	 * both regular RQ and XSK RQ.
	 * Although mlx5e_mpwqe_get_log_rq_size accepts mlx5e_xsk_param, it
	 * doesn't affect its return value, as long as params->xdp_prog != NULL,
	 * so we can just multiply by 2.
	 */
	if (params->xdp_prog)
		wqebbs *= 2;

	if (params->packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO)
		wqebbs += mlx5e_shampo_icosq_sz(mdev, params, rqp);

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
	param->is_xdp_mb = !mlx5e_rx_is_linear_skb(params, xsk);
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
