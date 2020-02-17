// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

int mlx5dr_table_set_miss_action(struct mlx5dr_table *tbl,
				 struct mlx5dr_action *action)
{
	struct mlx5dr_matcher *last_matcher = NULL;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_ste_htbl *last_htbl;
	int ret;

	if (action && action->action_type != DR_ACTION_TYP_FT)
		return -EOPNOTSUPP;

	mutex_lock(&tbl->dmn->mutex);

	if (!list_empty(&tbl->matcher_list))
		last_matcher = list_last_entry(&tbl->matcher_list,
					       struct mlx5dr_matcher,
					       matcher_list);

	if (tbl->dmn->type == MLX5DR_DOMAIN_TYPE_NIC_RX ||
	    tbl->dmn->type == MLX5DR_DOMAIN_TYPE_FDB) {
		if (last_matcher)
			last_htbl = last_matcher->rx.e_anchor;
		else
			last_htbl = tbl->rx.s_anchor;

		tbl->rx.default_icm_addr = action ?
			action->dest_tbl.tbl->rx.s_anchor->chunk->icm_addr :
			tbl->rx.nic_dmn->default_icm_addr;

		info.type = CONNECT_MISS;
		info.miss_icm_addr = tbl->rx.default_icm_addr;

		ret = mlx5dr_ste_htbl_init_and_postsend(tbl->dmn,
							tbl->rx.nic_dmn,
							last_htbl,
							&info, true);
		if (ret) {
			mlx5dr_dbg(tbl->dmn, "Failed to set RX miss action, ret %d\n", ret);
			goto out;
		}
	}

	if (tbl->dmn->type == MLX5DR_DOMAIN_TYPE_NIC_TX ||
	    tbl->dmn->type == MLX5DR_DOMAIN_TYPE_FDB) {
		if (last_matcher)
			last_htbl = last_matcher->tx.e_anchor;
		else
			last_htbl = tbl->tx.s_anchor;

		tbl->tx.default_icm_addr = action ?
			action->dest_tbl.tbl->tx.s_anchor->chunk->icm_addr :
			tbl->tx.nic_dmn->default_icm_addr;

		info.type = CONNECT_MISS;
		info.miss_icm_addr = tbl->tx.default_icm_addr;

		ret = mlx5dr_ste_htbl_init_and_postsend(tbl->dmn,
							tbl->tx.nic_dmn,
							last_htbl, &info, true);
		if (ret) {
			mlx5dr_dbg(tbl->dmn, "Failed to set TX miss action, ret %d\n", ret);
			goto out;
		}
	}

	/* Release old action */
	if (tbl->miss_action)
		refcount_dec(&tbl->miss_action->refcount);

	/* Set new miss action */
	tbl->miss_action = action;
	if (tbl->miss_action)
		refcount_inc(&action->refcount);

out:
	mutex_unlock(&tbl->dmn->mutex);
	return ret;
}

static void dr_table_uninit_nic(struct mlx5dr_table_rx_tx *nic_tbl)
{
	mlx5dr_htbl_put(nic_tbl->s_anchor);
}

static void dr_table_uninit_fdb(struct mlx5dr_table *tbl)
{
	dr_table_uninit_nic(&tbl->rx);
	dr_table_uninit_nic(&tbl->tx);
}

static void dr_table_uninit(struct mlx5dr_table *tbl)
{
	mutex_lock(&tbl->dmn->mutex);

	switch (tbl->dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		dr_table_uninit_nic(&tbl->rx);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		dr_table_uninit_nic(&tbl->tx);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		dr_table_uninit_fdb(tbl);
		break;
	default:
		WARN_ON(true);
		break;
	}

	mutex_unlock(&tbl->dmn->mutex);
}

static int dr_table_init_nic(struct mlx5dr_domain *dmn,
			     struct mlx5dr_table_rx_tx *nic_tbl)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_tbl->nic_dmn;
	struct mlx5dr_htbl_connect_info info;
	int ret;

	nic_tbl->default_icm_addr = nic_dmn->default_icm_addr;

	nic_tbl->s_anchor = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
						  DR_CHUNK_SIZE_1,
						  MLX5DR_STE_LU_TYPE_DONT_CARE,
						  0);
	if (!nic_tbl->s_anchor)
		return -ENOMEM;

	info.type = CONNECT_MISS;
	info.miss_icm_addr = nic_dmn->default_icm_addr;
	ret = mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn,
						nic_tbl->s_anchor,
						&info, true);
	if (ret)
		goto free_s_anchor;

	mlx5dr_htbl_get(nic_tbl->s_anchor);

	return 0;

free_s_anchor:
	mlx5dr_ste_htbl_free(nic_tbl->s_anchor);
	return ret;
}

static int dr_table_init_fdb(struct mlx5dr_table *tbl)
{
	int ret;

	ret = dr_table_init_nic(tbl->dmn, &tbl->rx);
	if (ret)
		return ret;

	ret = dr_table_init_nic(tbl->dmn, &tbl->tx);
	if (ret)
		goto destroy_rx;

	return 0;

destroy_rx:
	dr_table_uninit_nic(&tbl->rx);
	return ret;
}

static int dr_table_init(struct mlx5dr_table *tbl)
{
	int ret = 0;

	INIT_LIST_HEAD(&tbl->matcher_list);

	mutex_lock(&tbl->dmn->mutex);

	switch (tbl->dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		tbl->table_type = MLX5_FLOW_TABLE_TYPE_NIC_RX;
		tbl->rx.nic_dmn = &tbl->dmn->info.rx;
		ret = dr_table_init_nic(tbl->dmn, &tbl->rx);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		tbl->table_type = MLX5_FLOW_TABLE_TYPE_NIC_TX;
		tbl->tx.nic_dmn = &tbl->dmn->info.tx;
		ret = dr_table_init_nic(tbl->dmn, &tbl->tx);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		tbl->table_type = MLX5_FLOW_TABLE_TYPE_FDB;
		tbl->rx.nic_dmn = &tbl->dmn->info.rx;
		tbl->tx.nic_dmn = &tbl->dmn->info.tx;
		ret = dr_table_init_fdb(tbl);
		break;
	default:
		WARN_ON(true);
		break;
	}

	mutex_unlock(&tbl->dmn->mutex);

	return ret;
}

static int dr_table_destroy_sw_owned_tbl(struct mlx5dr_table *tbl)
{
	return mlx5dr_cmd_destroy_flow_table(tbl->dmn->mdev,
					     tbl->table_id,
					     tbl->table_type);
}

static int dr_table_create_sw_owned_tbl(struct mlx5dr_table *tbl)
{
	bool en_encap = !!(tbl->flags & MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT);
	bool en_decap = !!(tbl->flags & MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);
	struct mlx5dr_cmd_create_flow_table_attr ft_attr = {};
	u64 icm_addr_rx = 0;
	u64 icm_addr_tx = 0;
	int ret;

	if (tbl->rx.s_anchor)
		icm_addr_rx = tbl->rx.s_anchor->chunk->icm_addr;

	if (tbl->tx.s_anchor)
		icm_addr_tx = tbl->tx.s_anchor->chunk->icm_addr;

	ft_attr.table_type = tbl->table_type;
	ft_attr.icm_addr_rx = icm_addr_rx;
	ft_attr.icm_addr_tx = icm_addr_tx;
	ft_attr.level = tbl->dmn->info.caps.max_ft_level - 1;
	ft_attr.sw_owner = true;
	ft_attr.decap_en = en_decap;
	ft_attr.reformat_en = en_encap;

	ret = mlx5dr_cmd_create_flow_table(tbl->dmn->mdev, &ft_attr,
					   NULL, &tbl->table_id);

	return ret;
}

struct mlx5dr_table *mlx5dr_table_create(struct mlx5dr_domain *dmn, u32 level, u32 flags)
{
	struct mlx5dr_table *tbl;
	int ret;

	refcount_inc(&dmn->refcount);

	tbl = kzalloc(sizeof(*tbl), GFP_KERNEL);
	if (!tbl)
		goto dec_ref;

	tbl->dmn = dmn;
	tbl->level = level;
	tbl->flags = flags;
	refcount_set(&tbl->refcount, 1);

	ret = dr_table_init(tbl);
	if (ret)
		goto free_tbl;

	ret = dr_table_create_sw_owned_tbl(tbl);
	if (ret)
		goto uninit_tbl;

	return tbl;

uninit_tbl:
	dr_table_uninit(tbl);
free_tbl:
	kfree(tbl);
dec_ref:
	refcount_dec(&dmn->refcount);
	return NULL;
}

int mlx5dr_table_destroy(struct mlx5dr_table *tbl)
{
	int ret;

	if (refcount_read(&tbl->refcount) > 1)
		return -EBUSY;

	ret = dr_table_destroy_sw_owned_tbl(tbl);
	if (ret)
		return ret;

	dr_table_uninit(tbl);

	if (tbl->miss_action)
		refcount_dec(&tbl->miss_action->refcount);

	refcount_dec(&tbl->dmn->refcount);
	kfree(tbl);

	return ret;
}

u32 mlx5dr_table_get_id(struct mlx5dr_table *tbl)
{
	return tbl->table_id;
}
