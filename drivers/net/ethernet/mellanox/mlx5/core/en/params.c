// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "en/params.h"
#include "en/txrx.h"
#include "en_accel/tls_rxtx.h"

static inline bool mlx5e_rx_is_xdp(struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	return params->xdp_prog || xsk;
}

u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
				 struct mlx5e_xsk_param *xsk)
{
	u16 headroom;

	if (xsk)
		return xsk->headroom;

	headroom = NET_IP_ALIGN;
	if (mlx5e_rx_is_xdp(params, xsk))
		headroom += XDP_PACKET_HEADROOM;
	else
		headroom += MLX5_RX_HEADROOM;

	return headroom;
}

u32 mlx5e_rx_get_min_frag_sz(struct mlx5e_params *params,
			     struct mlx5e_xsk_param *xsk)
{
	u32 hw_mtu = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	u16 linear_rq_headroom = mlx5e_get_linear_rq_headroom(params, xsk);

	return linear_rq_headroom + hw_mtu;
}

u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk)
{
	u32 frag_sz = mlx5e_rx_get_min_frag_sz(params, xsk);

	/* AF_XDP doesn't build SKBs in place. */
	if (!xsk)
		frag_sz = MLX5_SKB_FRAG_SZ(frag_sz);

	/* XDP in mlx5e doesn't support multiple packets per page. AF_XDP is a
	 * special case. It can run with frames smaller than a page, as it
	 * doesn't allocate pages dynamically. However, here we pretend that
	 * fragments are page-sized: it allows to treat XSK frames like pages
	 * by redirecting alloc and free operations to XSK rings and by using
	 * the fact there are no multiple packets per "page" (which is a frame).
	 * The latter is important, because frames may come in a random order,
	 * and we will have trouble assemblying a real page of multiple frames.
	 */
	if (mlx5e_rx_is_xdp(params, xsk))
		frag_sz = max_t(u32, frag_sz, PAGE_SIZE);

	/* Even if we can go with a smaller fragment size, we must not put
	 * multiple packets into a single frame.
	 */
	if (xsk)
		frag_sz = max_t(u32, frag_sz, xsk->chunk_size);

	return frag_sz;
}

u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk)
{
	u32 linear_frag_sz = mlx5e_rx_get_linear_frag_sz(params, xsk);

	return MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(linear_frag_sz);
}

bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params,
			    struct mlx5e_xsk_param *xsk)
{
	/* AF_XDP allocates SKBs on XDP_PASS - ensure they don't occupy more
	 * than one page. For this, check both with and without xsk.
	 */
	u32 linear_frag_sz = max(mlx5e_rx_get_linear_frag_sz(params, xsk),
				 mlx5e_rx_get_linear_frag_sz(params, NULL));

	return !params->lro_en && linear_frag_sz <= PAGE_SIZE;
}

#define MLX5_MAX_MPWQE_LOG_WQE_STRIDE_SZ ((BIT(__mlx5_bit_sz(wq, log_wqe_stride_size)) - 1) + \
					  MLX5_MPWQE_LOG_STRIDE_SZ_BASE)
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params,
				  struct mlx5e_xsk_param *xsk)
{
	u32 linear_frag_sz = mlx5e_rx_get_linear_frag_sz(params, xsk);
	s8 signed_log_num_strides_param;
	u8 log_num_strides;

	if (!mlx5e_rx_is_linear_skb(params, xsk))
		return false;

	if (order_base_2(linear_frag_sz) > MLX5_MAX_MPWQE_LOG_WQE_STRIDE_SZ)
		return false;

	if (MLX5_CAP_GEN(mdev, ext_stride_num_range))
		return true;

	log_num_strides = MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(linear_frag_sz);
	signed_log_num_strides_param =
		(s8)log_num_strides - MLX5_MPWQE_LOG_NUM_STRIDES_BASE;

	return signed_log_num_strides_param >= 0;
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

u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	if (mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk))
		return order_base_2(mlx5e_rx_get_linear_frag_sz(params, xsk));

	return MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev);
}

u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	return MLX5_MPWRQ_LOG_WQE_SZ -
		mlx5e_mpwqe_get_log_stride_size(mdev, params, xsk);
}

u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params,
			  struct mlx5e_xsk_param *xsk)
{
	bool is_linear_skb = (params->rq_wq_type == MLX5_WQ_TYPE_CYCLIC) ?
		mlx5e_rx_is_linear_skb(params, xsk) :
		mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk);

	return is_linear_skb ? mlx5e_get_linear_rq_headroom(params, xsk) : 0;
}

u16 mlx5e_calc_sq_stop_room(struct mlx5_core_dev *mdev, struct mlx5e_params *params)
{
	bool is_mpwqe = MLX5E_GET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE);
	u16 stop_room;

	stop_room  = mlx5e_tls_get_stop_room(mdev, params);
	stop_room += mlx5e_stop_room_for_wqe(MLX5_SEND_WQE_MAX_WQEBBS);
	if (is_mpwqe)
		/* A MPWQE can take up to the maximum-sized WQE + all the normal
		 * stop room can be taken if a new packet breaks the active
		 * MPWQE session and allocates its WQEs right away.
		 */
		stop_room += mlx5e_stop_room_for_wqe(MLX5_SEND_WQE_MAX_WQEBBS);

	return stop_room;
}

int mlx5e_validate_params(struct mlx5e_priv *priv, struct mlx5e_params *params)
{
	size_t sq_size = 1 << params->log_sq_size;
	u16 stop_room;

	stop_room = mlx5e_calc_sq_stop_room(priv->mdev, params);
	if (stop_room >= sq_size) {
		netdev_err(priv->netdev, "Stop room %u is bigger than the SQ size %zu\n",
			   stop_room, sq_size);
		return -EINVAL;
	}

	return 0;
}
