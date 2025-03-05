// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "tir.h"
#include "params.h"
#include <linux/mlx5/transobj.h>

#define MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ (64 * 1024)

/* max() doesn't work inside square brackets. */
#define MLX5E_TIR_CMD_IN_SZ_DW ( \
	MLX5_ST_SZ_DW(create_tir_in) > MLX5_ST_SZ_DW(modify_tir_in) ? \
	MLX5_ST_SZ_DW(create_tir_in) : MLX5_ST_SZ_DW(modify_tir_in) \
)

struct mlx5e_tir_builder {
	u32 in[MLX5E_TIR_CMD_IN_SZ_DW];
	bool modify;
};

struct mlx5e_tir_builder *mlx5e_tir_builder_alloc(bool modify)
{
	struct mlx5e_tir_builder *builder;

	builder = kvzalloc(sizeof(*builder), GFP_KERNEL);
	if (!builder)
		return NULL;

	builder->modify = modify;

	return builder;
}

void mlx5e_tir_builder_free(struct mlx5e_tir_builder *builder)
{
	kvfree(builder);
}

void mlx5e_tir_builder_clear(struct mlx5e_tir_builder *builder)
{
	memset(builder->in, 0, sizeof(builder->in));
}

static void *mlx5e_tir_builder_get_tirc(struct mlx5e_tir_builder *builder)
{
	if (builder->modify)
		return MLX5_ADDR_OF(modify_tir_in, builder->in, ctx);
	return MLX5_ADDR_OF(create_tir_in, builder->in, ctx);
}

void mlx5e_tir_builder_build_inline(struct mlx5e_tir_builder *builder, u32 tdn, u32 rqn)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);

	WARN_ON(builder->modify);

	MLX5_SET(tirc, tirc, transport_domain, tdn);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_DIRECT);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_NONE);
	MLX5_SET(tirc, tirc, inline_rqn, rqn);
}

void mlx5e_tir_builder_build_rqt(struct mlx5e_tir_builder *builder, u32 tdn,
				 u32 rqtn, bool inner_ft_support)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);

	WARN_ON(builder->modify);

	MLX5_SET(tirc, tirc, transport_domain, tdn);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
	MLX5_SET(tirc, tirc, indirect_table, rqtn);
	MLX5_SET(tirc, tirc, tunneled_offload_en, inner_ft_support);
}

void mlx5e_tir_builder_build_packet_merge(struct mlx5e_tir_builder *builder,
					  const struct mlx5e_packet_merge_param *pkt_merge_param)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);
	const unsigned int rough_max_l2_l3_hdr_sz = 256;

	if (builder->modify)
		MLX5_SET(modify_tir_in, builder->in, bitmask.packet_merge, 1);

	switch (pkt_merge_param->type) {
	case MLX5E_PACKET_MERGE_LRO:
		MLX5_SET(tirc, tirc, packet_merge_mask,
			 MLX5_TIRC_PACKET_MERGE_MASK_IPV4_LRO |
			 MLX5_TIRC_PACKET_MERGE_MASK_IPV6_LRO);
		MLX5_SET(tirc, tirc, lro_max_ip_payload_size,
			 (MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ - rough_max_l2_l3_hdr_sz) >> 8);
		MLX5_SET(tirc, tirc, lro_timeout_period_usecs, pkt_merge_param->timeout);
		break;
	default:
		break;
	}
}

static int mlx5e_hfunc_to_hw(u8 hfunc)
{
	switch (hfunc) {
	case ETH_RSS_HASH_TOP:
		return MLX5_RX_HASH_FN_TOEPLITZ;
	case ETH_RSS_HASH_XOR:
		return MLX5_RX_HASH_FN_INVERTED_XOR8;
	default:
		return MLX5_RX_HASH_FN_NONE;
	}
}

void mlx5e_tir_builder_build_rss(struct mlx5e_tir_builder *builder,
				 const struct mlx5e_rss_params_hash *rss_hash,
				 const struct mlx5e_rss_params_traffic_type *rss_tt,
				 bool inner)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);
	void *hfso;

	if (builder->modify)
		MLX5_SET(modify_tir_in, builder->in, bitmask.hash, 1);

	MLX5_SET(tirc, tirc, rx_hash_fn, mlx5e_hfunc_to_hw(rss_hash->hfunc));
	if (rss_hash->hfunc == ETH_RSS_HASH_TOP) {
		const size_t len = MLX5_FLD_SZ_BYTES(tirc, rx_hash_toeplitz_key);
		void *rss_key = MLX5_ADDR_OF(tirc, tirc, rx_hash_toeplitz_key);

		MLX5_SET(tirc, tirc, rx_hash_symmetric, 1);
		memcpy(rss_key, rss_hash->toeplitz_hash_key, len);
	}

	if (inner)
		hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_inner);
	else
		hfso = MLX5_ADDR_OF(tirc, tirc, rx_hash_field_selector_outer);
	MLX5_SET(rx_hash_field_select, hfso, l3_prot_type, rss_tt->l3_prot_type);
	MLX5_SET(rx_hash_field_select, hfso, l4_prot_type, rss_tt->l4_prot_type);
	MLX5_SET(rx_hash_field_select, hfso, selected_fields, rss_tt->rx_hash_fields);
}

void mlx5e_tir_builder_build_direct(struct mlx5e_tir_builder *builder)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);

	WARN_ON(builder->modify);

	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_INVERTED_XOR8);
}

void mlx5e_tir_builder_build_tls(struct mlx5e_tir_builder *builder)
{
	void *tirc = mlx5e_tir_builder_get_tirc(builder);

	WARN_ON(builder->modify);

	MLX5_SET(tirc, tirc, tls_en, 1);
	MLX5_SET(tirc, tirc, self_lb_block,
		 MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST |
		 MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST);
}

int mlx5e_tir_init(struct mlx5e_tir *tir, struct mlx5e_tir_builder *builder,
		   struct mlx5_core_dev *mdev, bool reg)
{
	int err;

	tir->mdev = mdev;

	err = mlx5_core_create_tir(tir->mdev, builder->in, &tir->tirn);
	if (err)
		return err;

	if (reg) {
		struct mlx5e_hw_objs *res = &tir->mdev->mlx5e_res.hw_objs;

		mutex_lock(&res->td.list_lock);
		list_add(&tir->list, &res->td.tirs_list);
		mutex_unlock(&res->td.list_lock);
	} else {
		INIT_LIST_HEAD(&tir->list);
	}

	return 0;
}

void mlx5e_tir_destroy(struct mlx5e_tir *tir)
{
	struct mlx5e_hw_objs *res = &tir->mdev->mlx5e_res.hw_objs;

	/* Skip mutex if list_del is no-op (the TIR wasn't registered in the
	 * list). list_empty will never return true for an item of tirs_list,
	 * and READ_ONCE/WRITE_ONCE in list_empty/list_del guarantee consistency
	 * of the list->next value.
	 */
	if (!list_empty(&tir->list)) {
		mutex_lock(&res->td.list_lock);
		list_del(&tir->list);
		mutex_unlock(&res->td.list_lock);
	}

	mlx5_core_destroy_tir(tir->mdev, tir->tirn);
}

int mlx5e_tir_modify(struct mlx5e_tir *tir, struct mlx5e_tir_builder *builder)
{
	return mlx5_core_modify_tir(tir->mdev, tir->tirn, builder->in);
}
