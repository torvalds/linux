// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "en/params.h"

u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params)
{
	u16 hw_mtu = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	u16 linear_rq_headroom = params->xdp_prog ?
		XDP_PACKET_HEADROOM : MLX5_RX_HEADROOM;
	u32 frag_sz;

	linear_rq_headroom += NET_IP_ALIGN;

	frag_sz = MLX5_SKB_FRAG_SZ(linear_rq_headroom + hw_mtu);

	if (params->xdp_prog && frag_sz < PAGE_SIZE)
		frag_sz = PAGE_SIZE;

	return frag_sz;
}

u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params)
{
	u32 linear_frag_sz = mlx5e_rx_get_linear_frag_sz(params);

	return MLX5_MPWRQ_LOG_WQE_SZ - order_base_2(linear_frag_sz);
}

bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params)
{
	u32 frag_sz = mlx5e_rx_get_linear_frag_sz(params);

	return !params->lro_en && frag_sz <= PAGE_SIZE;
}

#define MLX5_MAX_MPWQE_LOG_WQE_STRIDE_SZ ((BIT(__mlx5_bit_sz(wq, log_wqe_stride_size)) - 1) + \
					  MLX5_MPWQE_LOG_STRIDE_SZ_BASE)
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params)
{
	u32 frag_sz = mlx5e_rx_get_linear_frag_sz(params);
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
		return order_base_2(mlx5e_rx_get_linear_frag_sz(params));

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
	u16 linear_rq_headroom = params->xdp_prog ?
		XDP_PACKET_HEADROOM : MLX5_RX_HEADROOM;
	bool is_linear_skb;

	linear_rq_headroom += NET_IP_ALIGN;

	is_linear_skb = (params->rq_wq_type == MLX5_WQ_TYPE_CYCLIC) ?
		mlx5e_rx_is_linear_skb(params) :
		mlx5e_rx_mpwqe_is_linear_skb(mdev, params);

	return is_linear_skb ? linear_rq_headroom : 0;
}
