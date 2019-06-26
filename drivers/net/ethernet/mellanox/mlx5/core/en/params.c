// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "en/params.h"

static inline bool mlx5e_rx_is_xdp(struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk)
{
	return params->xdp_prog || xsk;
}

static inline u16 mlx5e_get_linear_rq_headroom(struct mlx5e_params *params,
					       struct mlx5e_xsk_param *xsk)
{
	u16 headroom = NET_IP_ALIGN;

	if (mlx5e_rx_is_xdp(params, xsk)) {
		headroom += XDP_PACKET_HEADROOM;
		if (xsk)
			headroom += xsk->headroom;
	} else {
		headroom += MLX5_RX_HEADROOM;
	}

	return headroom;
}

u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params,
				struct mlx5e_xsk_param *xsk)
{
	u32 hw_mtu = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	u16 linear_rq_headroom = mlx5e_get_linear_rq_headroom(params, xsk);
	u32 frag_sz = linear_rq_headroom + hw_mtu;

	/* AF_XDP doesn't build SKBs in place. */
	if (!xsk)
		frag_sz = MLX5_SKB_FRAG_SZ(frag_sz);

	/* XDP in mlx5e doesn't support multiple packets per page. */
	if (mlx5e_rx_is_xdp(params, xsk))
		frag_sz = max_t(u32, frag_sz, PAGE_SIZE);

	/* Even if we can go with a smaller fragment size, we must not put
	 * multiple packets into a single frame.
	 */
	if (xsk)
		frag_sz = max_t(u32, frag_sz, xsk->chunk_size);

	return frag_sz;
}

u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params)
{
	u32 linear_frag_sz = mlx5e_rx_get_linear_frag_sz(params, NULL);

	return MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(linear_frag_sz);
}

bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params)
{
	u32 frag_sz = mlx5e_rx_get_linear_frag_sz(params, NULL);

	return !params->lro_en && frag_sz <= PAGE_SIZE;
}

#define MLX5_MAX_MPWQE_LOG_WQE_STRIDE_SZ ((BIT(__mlx5_bit_sz(wq, log_wqe_stride_size)) - 1) + \
					  MLX5_MPWQE_LOG_STRIDE_SZ_BASE)
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params)
{
	u32 frag_sz = mlx5e_rx_get_linear_frag_sz(params, NULL);
	s8 signed_log_num_strides_param;
	u8 log_num_strides;

	if (!mlx5e_rx_is_linear_skb(params))
		return false;

	if (order_base_2(frag_sz) > MLX5_MAX_MPWQE_LOG_WQE_STRIDE_SZ)
		return false;

	if (MLX5_CAP_GEN(mdev, ext_stride_num_range))
		return true;

	log_num_strides = MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(frag_sz);
	signed_log_num_strides_param =
		(s8)log_num_strides - MLX5_MPWQE_LOG_NUM_STRIDES_BASE;

	return signed_log_num_strides_param >= 0;
}

u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5e_params *params)
{
	u8 log_pkts_per_wqe = mlx5e_mpwqe_log_pkts_per_wqe(params);

	/* Numbers are unsigned, don't subtract to avoid underflow. */
	if (params->log_rq_mtu_frames <
	    log_pkts_per_wqe + MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW)
		return MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE_MPW;

	return params->log_rq_mtu_frames - log_pkts_per_wqe;
}

u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params)
{
	if (mlx5e_rx_mpwqe_is_linear_skb(mdev, params))
		return order_base_2(mlx5e_rx_get_linear_frag_sz(params, NULL));

	return MLX5_MPWRQ_DEF_LOG_STRIDE_SZ(mdev);
}

u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params)
{
	return MLX5_MPWRQ_LOG_WQE_SZ -
		mlx5e_mpwqe_get_log_stride_size(mdev, params);
}

u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params)
{
	bool is_linear_skb = (params->rq_wq_type == MLX5_WQ_TYPE_CYCLIC) ?
		mlx5e_rx_is_linear_skb(params) :
		mlx5e_rx_mpwqe_is_linear_skb(mdev, params);

	return is_linear_skb ? mlx5e_get_linear_rq_headroom(params, NULL) : 0;
}
