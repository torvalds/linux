// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>

#include "spectrum_mr_tcam.h"
#include "reg.h"
#include "spectrum.h"
#include "core_acl_flex_actions.h"
#include "spectrum_mr.h"

struct mlxsw_sp_mr_tcam {
	void *priv;
};

/* This struct maps to one RIGR2 register entry */
struct mlxsw_sp_mr_erif_sublist {
	struct list_head list;
	u32 rigr2_kvdl_index;
	int num_erifs;
	u16 erif_indices[MLXSW_REG_RIGR2_MAX_ERIFS];
	bool synced;
};

struct mlxsw_sp_mr_tcam_erif_list {
	struct list_head erif_sublists;
	u32 kvdl_index;
};

static bool
mlxsw_sp_mr_erif_sublist_full(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_mr_erif_sublist *erif_sublist)
{
	int erif_list_entries = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						   MC_ERIF_LIST_ENTRIES);

	return erif_sublist->num_erifs == erif_list_entries;
}

static void
mlxsw_sp_mr_erif_list_init(struct mlxsw_sp_mr_tcam_erif_list *erif_list)
{
	INIT_LIST_HEAD(&erif_list->erif_sublists);
}

static struct mlxsw_sp_mr_erif_sublist *
mlxsw_sp_mr_erif_sublist_create(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_mr_tcam_erif_list *erif_list)
{
	struct mlxsw_sp_mr_erif_sublist *erif_sublist;
	int err;

	erif_sublist = kzalloc(sizeof(*erif_sublist), GFP_KERNEL);
	if (!erif_sublist)
		return ERR_PTR(-ENOMEM);
	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_MCRIGR,
				  1, &erif_sublist->rigr2_kvdl_index);
	if (err) {
		kfree(erif_sublist);
		return ERR_PTR(err);
	}

	list_add_tail(&erif_sublist->list, &erif_list->erif_sublists);
	return erif_sublist;
}

static void
mlxsw_sp_mr_erif_sublist_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_mr_erif_sublist *erif_sublist)
{
	list_del(&erif_sublist->list);
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_MCRIGR,
			   1, erif_sublist->rigr2_kvdl_index);
	kfree(erif_sublist);
}

static int
mlxsw_sp_mr_erif_list_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_mr_tcam_erif_list *erif_list,
			  u16 erif_index)
{
	struct mlxsw_sp_mr_erif_sublist *sublist;

	/* If either there is no erif_entry or the last one is full, allocate a
	 * new one.
	 */
	if (list_empty(&erif_list->erif_sublists)) {
		sublist = mlxsw_sp_mr_erif_sublist_create(mlxsw_sp, erif_list);
		if (IS_ERR(sublist))
			return PTR_ERR(sublist);
		erif_list->kvdl_index = sublist->rigr2_kvdl_index;
	} else {
		sublist = list_last_entry(&erif_list->erif_sublists,
					  struct mlxsw_sp_mr_erif_sublist,
					  list);
		sublist->synced = false;
		if (mlxsw_sp_mr_erif_sublist_full(mlxsw_sp, sublist)) {
			sublist = mlxsw_sp_mr_erif_sublist_create(mlxsw_sp,
								  erif_list);
			if (IS_ERR(sublist))
				return PTR_ERR(sublist);
		}
	}

	/* Add the eRIF to the last entry's last index */
	sublist->erif_indices[sublist->num_erifs++] = erif_index;
	return 0;
}

static void
mlxsw_sp_mr_erif_list_flush(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_mr_tcam_erif_list *erif_list)
{
	struct mlxsw_sp_mr_erif_sublist *erif_sublist, *tmp;

	list_for_each_entry_safe(erif_sublist, tmp, &erif_list->erif_sublists,
				 list)
		mlxsw_sp_mr_erif_sublist_destroy(mlxsw_sp, erif_sublist);
}

static int
mlxsw_sp_mr_erif_list_commit(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_mr_tcam_erif_list *erif_list)
{
	struct mlxsw_sp_mr_erif_sublist *curr_sublist;
	char rigr2_pl[MLXSW_REG_RIGR2_LEN];
	int err;
	int i;

	list_for_each_entry(curr_sublist, &erif_list->erif_sublists, list) {
		if (curr_sublist->synced)
			continue;

		/* If the sublist is not the last one, pack the next index */
		if (list_is_last(&curr_sublist->list,
				 &erif_list->erif_sublists)) {
			mlxsw_reg_rigr2_pack(rigr2_pl,
					     curr_sublist->rigr2_kvdl_index,
					     false, 0);
		} else {
			struct mlxsw_sp_mr_erif_sublist *next_sublist;

			next_sublist = list_next_entry(curr_sublist, list);
			mlxsw_reg_rigr2_pack(rigr2_pl,
					     curr_sublist->rigr2_kvdl_index,
					     true,
					     next_sublist->rigr2_kvdl_index);
		}

		/* Pack all the erifs */
		for (i = 0; i < curr_sublist->num_erifs; i++) {
			u16 erif_index = curr_sublist->erif_indices[i];

			mlxsw_reg_rigr2_erif_entry_pack(rigr2_pl, i, true,
							erif_index);
		}

		/* Write the entry */
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rigr2),
				      rigr2_pl);
		if (err)
			/* No need of a rollback here because this
			 * hardware entry should not be pointed yet.
			 */
			return err;
		curr_sublist->synced = true;
	}
	return 0;
}

static void mlxsw_sp_mr_erif_list_move(struct mlxsw_sp_mr_tcam_erif_list *to,
				       struct mlxsw_sp_mr_tcam_erif_list *from)
{
	list_splice(&from->erif_sublists, &to->erif_sublists);
	to->kvdl_index = from->kvdl_index;
}

struct mlxsw_sp_mr_tcam_route {
	struct mlxsw_sp_mr_tcam_erif_list erif_list;
	struct mlxsw_afa_block *afa_block;
	u32 counter_index;
	enum mlxsw_sp_mr_route_action action;
	struct mlxsw_sp_mr_route_key key;
	u16 irif_index;
	u16 min_mtu;
	void *priv;
};

static struct mlxsw_afa_block *
mlxsw_sp_mr_tcam_afa_block_create(struct mlxsw_sp *mlxsw_sp,
				  enum mlxsw_sp_mr_route_action route_action,
				  u16 irif_index, u32 counter_index,
				  u16 min_mtu,
				  struct mlxsw_sp_mr_tcam_erif_list *erif_list)
{
	struct mlxsw_afa_block *afa_block;
	int err;

	afa_block = mlxsw_afa_block_create(mlxsw_sp->afa);
	if (!afa_block)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_afa_block_append_allocated_counter(afa_block,
						       counter_index);
	if (err)
		goto err;

	switch (route_action) {
	case MLXSW_SP_MR_ROUTE_ACTION_TRAP:
		err = mlxsw_afa_block_append_trap(afa_block,
						  MLXSW_TRAP_ID_ACL1);
		if (err)
			goto err;
		break;
	case MLXSW_SP_MR_ROUTE_ACTION_TRAP_AND_FORWARD:
	case MLXSW_SP_MR_ROUTE_ACTION_FORWARD:
		/* If we are about to append a multicast router action, commit
		 * the erif_list.
		 */
		err = mlxsw_sp_mr_erif_list_commit(mlxsw_sp, erif_list);
		if (err)
			goto err;

		err = mlxsw_afa_block_append_mcrouter(afa_block, irif_index,
						      min_mtu, false,
						      erif_list->kvdl_index);
		if (err)
			goto err;

		if (route_action == MLXSW_SP_MR_ROUTE_ACTION_TRAP_AND_FORWARD) {
			err = mlxsw_afa_block_append_trap_and_forward(afa_block,
								      MLXSW_TRAP_ID_ACL2);
			if (err)
				goto err;
		}
		break;
	default:
		err = -EINVAL;
		goto err;
	}

	err = mlxsw_afa_block_commit(afa_block);
	if (err)
		goto err;
	return afa_block;
err:
	mlxsw_afa_block_destroy(afa_block);
	return ERR_PTR(err);
}

static void
mlxsw_sp_mr_tcam_afa_block_destroy(struct mlxsw_afa_block *afa_block)
{
	mlxsw_afa_block_destroy(afa_block);
}

static int
mlxsw_sp_mr_tcam_erif_populate(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_mr_tcam_erif_list *erif_list,
			       struct mlxsw_sp_mr_route_info *route_info)
{
	int err;
	int i;

	for (i = 0; i < route_info->erif_num; i++) {
		u16 erif_index = route_info->erif_indices[i];

		err = mlxsw_sp_mr_erif_list_add(mlxsw_sp, erif_list,
						erif_index);
		if (err)
			return err;
	}
	return 0;
}

static int
mlxsw_sp_mr_tcam_route_create(struct mlxsw_sp *mlxsw_sp, void *priv,
			      void *route_priv,
			      struct mlxsw_sp_mr_route_params *route_params)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_sp_mr_tcam *mr_tcam = priv;
	int err;

	route->key = route_params->key;
	route->irif_index = route_params->value.irif_index;
	route->min_mtu = route_params->value.min_mtu;
	route->action = route_params->value.route_action;

	/* Create the egress RIFs list */
	mlxsw_sp_mr_erif_list_init(&route->erif_list);
	err = mlxsw_sp_mr_tcam_erif_populate(mlxsw_sp, &route->erif_list,
					     &route_params->value);
	if (err)
		goto err_erif_populate;

	/* Create the flow counter */
	err = mlxsw_sp_flow_counter_alloc(mlxsw_sp, &route->counter_index);
	if (err)
		goto err_counter_alloc;

	/* Create the flexible action block */
	route->afa_block = mlxsw_sp_mr_tcam_afa_block_create(mlxsw_sp,
							     route->action,
							     route->irif_index,
							     route->counter_index,
							     route->min_mtu,
							     &route->erif_list);
	if (IS_ERR(route->afa_block)) {
		err = PTR_ERR(route->afa_block);
		goto err_afa_block_create;
	}

	route->priv = kzalloc(ops->route_priv_size, GFP_KERNEL);
	if (!route->priv) {
		err = -ENOMEM;
		goto err_route_priv_alloc;
	}

	/* Write the route to the TCAM */
	err = ops->route_create(mlxsw_sp, mr_tcam->priv, route->priv,
				&route->key, route->afa_block,
				route_params->prio);
	if (err)
		goto err_route_create;
	return 0;

err_route_create:
	kfree(route->priv);
err_route_priv_alloc:
	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
err_afa_block_create:
	mlxsw_sp_flow_counter_free(mlxsw_sp, route->counter_index);
err_erif_populate:
err_counter_alloc:
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &route->erif_list);
	return err;
}

static void mlxsw_sp_mr_tcam_route_destroy(struct mlxsw_sp *mlxsw_sp,
					   void *priv, void *route_priv)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_sp_mr_tcam *mr_tcam = priv;

	ops->route_destroy(mlxsw_sp, mr_tcam->priv, route->priv, &route->key);
	kfree(route->priv);
	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
	mlxsw_sp_flow_counter_free(mlxsw_sp, route->counter_index);
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &route->erif_list);
}

static int mlxsw_sp_mr_tcam_route_stats(struct mlxsw_sp *mlxsw_sp,
					void *route_priv, u64 *packets,
					u64 *bytes)
{
	struct mlxsw_sp_mr_tcam_route *route = route_priv;

	return mlxsw_sp_flow_counter_get(mlxsw_sp, route->counter_index,
					 packets, bytes);
}

static int
mlxsw_sp_mr_tcam_route_action_update(struct mlxsw_sp *mlxsw_sp,
				     void *route_priv,
				     enum mlxsw_sp_mr_route_action route_action)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_afa_block *afa_block;
	int err;

	/* Create a new flexible action block */
	afa_block = mlxsw_sp_mr_tcam_afa_block_create(mlxsw_sp, route_action,
						      route->irif_index,
						      route->counter_index,
						      route->min_mtu,
						      &route->erif_list);
	if (IS_ERR(afa_block))
		return PTR_ERR(afa_block);

	/* Update the TCAM route entry */
	err = ops->route_update(mlxsw_sp, route->priv, &route->key, afa_block);
	if (err)
		goto err;

	/* Delete the old one */
	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
	route->afa_block = afa_block;
	route->action = route_action;
	return 0;
err:
	mlxsw_sp_mr_tcam_afa_block_destroy(afa_block);
	return err;
}

static int mlxsw_sp_mr_tcam_route_min_mtu_update(struct mlxsw_sp *mlxsw_sp,
						 void *route_priv, u16 min_mtu)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_afa_block *afa_block;
	int err;

	/* Create a new flexible action block */
	afa_block = mlxsw_sp_mr_tcam_afa_block_create(mlxsw_sp,
						      route->action,
						      route->irif_index,
						      route->counter_index,
						      min_mtu,
						      &route->erif_list);
	if (IS_ERR(afa_block))
		return PTR_ERR(afa_block);

	/* Update the TCAM route entry */
	err = ops->route_update(mlxsw_sp, route->priv, &route->key, afa_block);
	if (err)
		goto err;

	/* Delete the old one */
	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
	route->afa_block = afa_block;
	route->min_mtu = min_mtu;
	return 0;
err:
	mlxsw_sp_mr_tcam_afa_block_destroy(afa_block);
	return err;
}

static int mlxsw_sp_mr_tcam_route_irif_update(struct mlxsw_sp *mlxsw_sp,
					      void *route_priv, u16 irif_index)
{
	struct mlxsw_sp_mr_tcam_route *route = route_priv;

	if (route->action != MLXSW_SP_MR_ROUTE_ACTION_TRAP)
		return -EINVAL;
	route->irif_index = irif_index;
	return 0;
}

static int mlxsw_sp_mr_tcam_route_erif_add(struct mlxsw_sp *mlxsw_sp,
					   void *route_priv, u16 erif_index)
{
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	int err;

	err = mlxsw_sp_mr_erif_list_add(mlxsw_sp, &route->erif_list,
					erif_index);
	if (err)
		return err;

	/* Commit the action only if the route action is not TRAP */
	if (route->action != MLXSW_SP_MR_ROUTE_ACTION_TRAP)
		return mlxsw_sp_mr_erif_list_commit(mlxsw_sp,
						    &route->erif_list);
	return 0;
}

static int mlxsw_sp_mr_tcam_route_erif_del(struct mlxsw_sp *mlxsw_sp,
					   void *route_priv, u16 erif_index)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_sp_mr_erif_sublist *erif_sublist;
	struct mlxsw_sp_mr_tcam_erif_list erif_list;
	struct mlxsw_afa_block *afa_block;
	int err;
	int i;

	/* Create a copy of the original erif_list without the deleted entry */
	mlxsw_sp_mr_erif_list_init(&erif_list);
	list_for_each_entry(erif_sublist, &route->erif_list.erif_sublists, list) {
		for (i = 0; i < erif_sublist->num_erifs; i++) {
			u16 curr_erif = erif_sublist->erif_indices[i];

			if (curr_erif == erif_index)
				continue;
			err = mlxsw_sp_mr_erif_list_add(mlxsw_sp, &erif_list,
							curr_erif);
			if (err)
				goto err_erif_list_add;
		}
	}

	/* Create the flexible action block pointing to the new erif_list */
	afa_block = mlxsw_sp_mr_tcam_afa_block_create(mlxsw_sp, route->action,
						      route->irif_index,
						      route->counter_index,
						      route->min_mtu,
						      &erif_list);
	if (IS_ERR(afa_block)) {
		err = PTR_ERR(afa_block);
		goto err_afa_block_create;
	}

	/* Update the TCAM route entry */
	err = ops->route_update(mlxsw_sp, route->priv, &route->key, afa_block);
	if (err)
		goto err_route_write;

	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &route->erif_list);
	route->afa_block = afa_block;
	mlxsw_sp_mr_erif_list_move(&route->erif_list, &erif_list);
	return 0;

err_route_write:
	mlxsw_sp_mr_tcam_afa_block_destroy(afa_block);
err_afa_block_create:
err_erif_list_add:
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &erif_list);
	return err;
}

static int
mlxsw_sp_mr_tcam_route_update(struct mlxsw_sp *mlxsw_sp, void *route_priv,
			      struct mlxsw_sp_mr_route_info *route_info)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam_route *route = route_priv;
	struct mlxsw_sp_mr_tcam_erif_list erif_list;
	struct mlxsw_afa_block *afa_block;
	int err;

	/* Create a new erif_list */
	mlxsw_sp_mr_erif_list_init(&erif_list);
	err = mlxsw_sp_mr_tcam_erif_populate(mlxsw_sp, &erif_list, route_info);
	if (err)
		goto err_erif_populate;

	/* Create the flexible action block pointing to the new erif_list */
	afa_block = mlxsw_sp_mr_tcam_afa_block_create(mlxsw_sp,
						      route_info->route_action,
						      route_info->irif_index,
						      route->counter_index,
						      route_info->min_mtu,
						      &erif_list);
	if (IS_ERR(afa_block)) {
		err = PTR_ERR(afa_block);
		goto err_afa_block_create;
	}

	/* Update the TCAM route entry */
	err = ops->route_update(mlxsw_sp, route->priv, &route->key, afa_block);
	if (err)
		goto err_route_write;

	mlxsw_sp_mr_tcam_afa_block_destroy(route->afa_block);
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &route->erif_list);
	route->afa_block = afa_block;
	mlxsw_sp_mr_erif_list_move(&route->erif_list, &erif_list);
	route->action = route_info->route_action;
	route->irif_index = route_info->irif_index;
	route->min_mtu = route_info->min_mtu;
	return 0;

err_route_write:
	mlxsw_sp_mr_tcam_afa_block_destroy(afa_block);
err_afa_block_create:
err_erif_populate:
	mlxsw_sp_mr_erif_list_flush(mlxsw_sp, &erif_list);
	return err;
}

static int mlxsw_sp_mr_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam *mr_tcam = priv;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MC_ERIF_LIST_ENTRIES))
		return -EIO;

	mr_tcam->priv = kzalloc(ops->priv_size, GFP_KERNEL);
	if (!mr_tcam->priv)
		return -ENOMEM;

	err = ops->init(mlxsw_sp, mr_tcam->priv);
	if (err)
		goto err_init;
	return 0;

err_init:
	kfree(mr_tcam->priv);
	return err;
}

static void mlxsw_sp_mr_tcam_fini(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	const struct mlxsw_sp_mr_tcam_ops *ops = mlxsw_sp->mr_tcam_ops;
	struct mlxsw_sp_mr_tcam *mr_tcam = priv;

	ops->fini(mr_tcam->priv);
	kfree(mr_tcam->priv);
}

const struct mlxsw_sp_mr_ops mlxsw_sp_mr_tcam_ops = {
	.priv_size = sizeof(struct mlxsw_sp_mr_tcam),
	.route_priv_size = sizeof(struct mlxsw_sp_mr_tcam_route),
	.init = mlxsw_sp_mr_tcam_init,
	.route_create = mlxsw_sp_mr_tcam_route_create,
	.route_update = mlxsw_sp_mr_tcam_route_update,
	.route_stats = mlxsw_sp_mr_tcam_route_stats,
	.route_action_update = mlxsw_sp_mr_tcam_route_action_update,
	.route_min_mtu_update = mlxsw_sp_mr_tcam_route_min_mtu_update,
	.route_irif_update = mlxsw_sp_mr_tcam_route_irif_update,
	.route_erif_add = mlxsw_sp_mr_tcam_route_erif_add,
	.route_erif_del = mlxsw_sp_mr_tcam_route_erif_del,
	.route_destroy = mlxsw_sp_mr_tcam_route_destroy,
	.fini = mlxsw_sp_mr_tcam_fini,
};
