/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_mr.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/rhashtable.h>

#include "spectrum_mr.h"
#include "spectrum_router.h"

struct mlxsw_sp_mr {
	const struct mlxsw_sp_mr_ops *mr_ops;
	void *catchall_route_priv;
	struct delayed_work stats_update_dw;
	struct list_head table_list;
#define MLXSW_SP_MR_ROUTES_COUNTER_UPDATE_INTERVAL 5000 /* ms */
	unsigned long priv[0];
	/* priv has to be always the last item */
};

struct mlxsw_sp_mr_vif;
struct mlxsw_sp_mr_vif_ops {
	bool (*is_regular)(const struct mlxsw_sp_mr_vif *vif);
};

struct mlxsw_sp_mr_vif {
	struct net_device *dev;
	const struct mlxsw_sp_rif *rif;
	unsigned long vif_flags;

	/* A list of route_vif_entry structs that point to routes that the VIF
	 * instance is used as one of the egress VIFs
	 */
	struct list_head route_evif_list;

	/* A list of route_vif_entry structs that point to routes that the VIF
	 * instance is used as an ingress VIF
	 */
	struct list_head route_ivif_list;

	/* Protocol specific operations for a VIF */
	const struct mlxsw_sp_mr_vif_ops *ops;
};

struct mlxsw_sp_mr_route_vif_entry {
	struct list_head vif_node;
	struct list_head route_node;
	struct mlxsw_sp_mr_vif *mr_vif;
	struct mlxsw_sp_mr_route *mr_route;
};

struct mlxsw_sp_mr_table;
struct mlxsw_sp_mr_table_ops {
	bool (*is_route_valid)(const struct mlxsw_sp_mr_table *mr_table,
			       const struct mr_mfc *mfc);
	void (*key_create)(struct mlxsw_sp_mr_table *mr_table,
			   struct mlxsw_sp_mr_route_key *key,
			   struct mr_mfc *mfc);
	bool (*is_route_starg)(const struct mlxsw_sp_mr_table *mr_table,
			       const struct mlxsw_sp_mr_route *mr_route);
};

struct mlxsw_sp_mr_table {
	struct list_head node;
	enum mlxsw_sp_l3proto proto;
	struct mlxsw_sp *mlxsw_sp;
	u32 vr_id;
	struct mlxsw_sp_mr_vif vifs[MAXVIFS];
	struct list_head route_list;
	struct rhashtable route_ht;
	const struct mlxsw_sp_mr_table_ops *ops;
	char catchall_route_priv[0];
	/* catchall_route_priv has to be always the last item */
};

struct mlxsw_sp_mr_route {
	struct list_head node;
	struct rhash_head ht_node;
	struct mlxsw_sp_mr_route_key key;
	enum mlxsw_sp_mr_route_action route_action;
	u16 min_mtu;
	struct mr_mfc *mfc;
	void *route_priv;
	const struct mlxsw_sp_mr_table *mr_table;
	/* A list of route_vif_entry structs that point to the egress VIFs */
	struct list_head evif_list;
	/* A route_vif_entry struct that point to the ingress VIF */
	struct mlxsw_sp_mr_route_vif_entry ivif;
};

static const struct rhashtable_params mlxsw_sp_mr_route_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_mr_route_key),
	.key_offset = offsetof(struct mlxsw_sp_mr_route, key),
	.head_offset = offsetof(struct mlxsw_sp_mr_route, ht_node),
	.automatic_shrinking = true,
};

static bool mlxsw_sp_mr_vif_valid(const struct mlxsw_sp_mr_vif *vif)
{
	return vif->ops->is_regular(vif) && vif->dev && vif->rif;
}

static bool mlxsw_sp_mr_vif_exists(const struct mlxsw_sp_mr_vif *vif)
{
	return vif->dev;
}

static bool
mlxsw_sp_mr_route_ivif_in_evifs(const struct mlxsw_sp_mr_route *mr_route)
{
	vifi_t ivif = mr_route->mfc->mfc_parent;

	return mr_route->mfc->mfc_un.res.ttls[ivif] != 255;
}

static int
mlxsw_sp_mr_route_valid_evifs_num(const struct mlxsw_sp_mr_route *mr_route)
{
	struct mlxsw_sp_mr_route_vif_entry *rve;
	int valid_evifs;

	valid_evifs = 0;
	list_for_each_entry(rve, &mr_route->evif_list, route_node)
		if (mlxsw_sp_mr_vif_valid(rve->mr_vif))
			valid_evifs++;
	return valid_evifs;
}

static enum mlxsw_sp_mr_route_action
mlxsw_sp_mr_route_action(const struct mlxsw_sp_mr_route *mr_route)
{
	struct mlxsw_sp_mr_route_vif_entry *rve;

	/* If the ingress port is not regular and resolved, trap the route */
	if (!mlxsw_sp_mr_vif_valid(mr_route->ivif.mr_vif))
		return MLXSW_SP_MR_ROUTE_ACTION_TRAP;

	/* The kernel does not match a (*,G) route that the ingress interface is
	 * not one of the egress interfaces, so trap these kind of routes.
	 */
	if (mr_route->mr_table->ops->is_route_starg(mr_route->mr_table,
						    mr_route) &&
	    !mlxsw_sp_mr_route_ivif_in_evifs(mr_route))
		return MLXSW_SP_MR_ROUTE_ACTION_TRAP;

	/* If the route has no valid eVIFs, trap it. */
	if (!mlxsw_sp_mr_route_valid_evifs_num(mr_route))
		return MLXSW_SP_MR_ROUTE_ACTION_TRAP;

	/* If one of the eVIFs has no RIF, trap-and-forward the route as there
	 * is some more routing to do in software too.
	 */
	list_for_each_entry(rve, &mr_route->evif_list, route_node)
		if (mlxsw_sp_mr_vif_exists(rve->mr_vif) && !rve->mr_vif->rif)
			return MLXSW_SP_MR_ROUTE_ACTION_TRAP_AND_FORWARD;

	return MLXSW_SP_MR_ROUTE_ACTION_FORWARD;
}

static enum mlxsw_sp_mr_route_prio
mlxsw_sp_mr_route_prio(const struct mlxsw_sp_mr_route *mr_route)
{
	return mr_route->mr_table->ops->is_route_starg(mr_route->mr_table,
						       mr_route) ?
		MLXSW_SP_MR_ROUTE_PRIO_STARG : MLXSW_SP_MR_ROUTE_PRIO_SG;
}

static int mlxsw_sp_mr_route_evif_link(struct mlxsw_sp_mr_route *mr_route,
				       struct mlxsw_sp_mr_vif *mr_vif)
{
	struct mlxsw_sp_mr_route_vif_entry *rve;

	rve = kzalloc(sizeof(*rve), GFP_KERNEL);
	if (!rve)
		return -ENOMEM;
	rve->mr_route = mr_route;
	rve->mr_vif = mr_vif;
	list_add_tail(&rve->route_node, &mr_route->evif_list);
	list_add_tail(&rve->vif_node, &mr_vif->route_evif_list);
	return 0;
}

static void
mlxsw_sp_mr_route_evif_unlink(struct mlxsw_sp_mr_route_vif_entry *rve)
{
	list_del(&rve->route_node);
	list_del(&rve->vif_node);
	kfree(rve);
}

static void mlxsw_sp_mr_route_ivif_link(struct mlxsw_sp_mr_route *mr_route,
					struct mlxsw_sp_mr_vif *mr_vif)
{
	mr_route->ivif.mr_route = mr_route;
	mr_route->ivif.mr_vif = mr_vif;
	list_add_tail(&mr_route->ivif.vif_node, &mr_vif->route_ivif_list);
}

static void mlxsw_sp_mr_route_ivif_unlink(struct mlxsw_sp_mr_route *mr_route)
{
	list_del(&mr_route->ivif.vif_node);
}

static int
mlxsw_sp_mr_route_info_create(struct mlxsw_sp_mr_table *mr_table,
			      struct mlxsw_sp_mr_route *mr_route,
			      struct mlxsw_sp_mr_route_info *route_info)
{
	struct mlxsw_sp_mr_route_vif_entry *rve;
	u16 *erif_indices;
	u16 irif_index;
	u16 erif = 0;

	erif_indices = kmalloc_array(MAXVIFS, sizeof(*erif_indices),
				     GFP_KERNEL);
	if (!erif_indices)
		return -ENOMEM;

	list_for_each_entry(rve, &mr_route->evif_list, route_node) {
		if (mlxsw_sp_mr_vif_valid(rve->mr_vif)) {
			u16 rifi = mlxsw_sp_rif_index(rve->mr_vif->rif);

			erif_indices[erif++] = rifi;
		}
	}

	if (mlxsw_sp_mr_vif_valid(mr_route->ivif.mr_vif))
		irif_index = mlxsw_sp_rif_index(mr_route->ivif.mr_vif->rif);
	else
		irif_index = 0;

	route_info->irif_index = irif_index;
	route_info->erif_indices = erif_indices;
	route_info->min_mtu = mr_route->min_mtu;
	route_info->route_action = mr_route->route_action;
	route_info->erif_num = erif;
	return 0;
}

static void
mlxsw_sp_mr_route_info_destroy(struct mlxsw_sp_mr_route_info *route_info)
{
	kfree(route_info->erif_indices);
}

static int mlxsw_sp_mr_route_write(struct mlxsw_sp_mr_table *mr_table,
				   struct mlxsw_sp_mr_route *mr_route,
				   bool replace)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	struct mlxsw_sp_mr_route_info route_info;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	int err;

	err = mlxsw_sp_mr_route_info_create(mr_table, mr_route, &route_info);
	if (err)
		return err;

	if (!replace) {
		struct mlxsw_sp_mr_route_params route_params;

		mr_route->route_priv = kzalloc(mr->mr_ops->route_priv_size,
					       GFP_KERNEL);
		if (!mr_route->route_priv) {
			err = -ENOMEM;
			goto out;
		}

		route_params.key = mr_route->key;
		route_params.value = route_info;
		route_params.prio = mlxsw_sp_mr_route_prio(mr_route);
		err = mr->mr_ops->route_create(mlxsw_sp, mr->priv,
					       mr_route->route_priv,
					       &route_params);
		if (err)
			kfree(mr_route->route_priv);
	} else {
		err = mr->mr_ops->route_update(mlxsw_sp, mr_route->route_priv,
					       &route_info);
	}
out:
	mlxsw_sp_mr_route_info_destroy(&route_info);
	return err;
}

static void mlxsw_sp_mr_route_erase(struct mlxsw_sp_mr_table *mr_table,
				    struct mlxsw_sp_mr_route *mr_route)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;

	mr->mr_ops->route_destroy(mlxsw_sp, mr->priv, mr_route->route_priv);
	kfree(mr_route->route_priv);
}

static struct mlxsw_sp_mr_route *
mlxsw_sp_mr_route4_create(struct mlxsw_sp_mr_table *mr_table,
			  struct mfc_cache *mfc)
{
	struct mlxsw_sp_mr_route_vif_entry *rve, *tmp;
	struct mlxsw_sp_mr_route *mr_route;
	int err = 0;
	int i;

	/* Allocate and init a new route and fill it with parameters */
	mr_route = kzalloc(sizeof(*mr_route), GFP_KERNEL);
	if (!mr_route)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&mr_route->evif_list);

	/* Find min_mtu and link iVIF and eVIFs */
	mr_route->min_mtu = ETH_MAX_MTU;
	mr_cache_hold(&mfc->_c);
	mr_route->mfc = &mfc->_c;
	mr_table->ops->key_create(mr_table, &mr_route->key, mr_route->mfc);

	mr_route->mr_table = mr_table;
	for (i = 0; i < MAXVIFS; i++) {
		if (mfc->_c.mfc_un.res.ttls[i] != 255) {
			err = mlxsw_sp_mr_route_evif_link(mr_route,
							  &mr_table->vifs[i]);
			if (err)
				goto err;
			if (mr_table->vifs[i].dev &&
			    mr_table->vifs[i].dev->mtu < mr_route->min_mtu)
				mr_route->min_mtu = mr_table->vifs[i].dev->mtu;
		}
	}
	mlxsw_sp_mr_route_ivif_link(mr_route,
				    &mr_table->vifs[mfc->_c.mfc_parent]);

	mr_route->route_action = mlxsw_sp_mr_route_action(mr_route);
	return mr_route;
err:
	mr_cache_put(&mfc->_c);
	list_for_each_entry_safe(rve, tmp, &mr_route->evif_list, route_node)
		mlxsw_sp_mr_route_evif_unlink(rve);
	kfree(mr_route);
	return ERR_PTR(err);
}

static void mlxsw_sp_mr_route4_destroy(struct mlxsw_sp_mr_table *mr_table,
				       struct mlxsw_sp_mr_route *mr_route)
{
	struct mlxsw_sp_mr_route_vif_entry *rve, *tmp;

	mlxsw_sp_mr_route_ivif_unlink(mr_route);
	mr_cache_put((struct mr_mfc *)mr_route->mfc);
	list_for_each_entry_safe(rve, tmp, &mr_route->evif_list, route_node)
		mlxsw_sp_mr_route_evif_unlink(rve);
	kfree(mr_route);
}

static void mlxsw_sp_mr_route_destroy(struct mlxsw_sp_mr_table *mr_table,
				      struct mlxsw_sp_mr_route *mr_route)
{
	switch (mr_table->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_mr_route4_destroy(mr_table, mr_route);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		/* fall through */
	default:
		WARN_ON_ONCE(1);
	}
}

static void mlxsw_sp_mr_mfc_offload_set(struct mlxsw_sp_mr_route *mr_route,
					bool offload)
{
	if (offload)
		mr_route->mfc->mfc_flags |= MFC_OFFLOAD;
	else
		mr_route->mfc->mfc_flags &= ~MFC_OFFLOAD;
}

static void mlxsw_sp_mr_mfc_offload_update(struct mlxsw_sp_mr_route *mr_route)
{
	bool offload;

	offload = mr_route->route_action != MLXSW_SP_MR_ROUTE_ACTION_TRAP;
	mlxsw_sp_mr_mfc_offload_set(mr_route, offload);
}

static void __mlxsw_sp_mr_route_del(struct mlxsw_sp_mr_table *mr_table,
				    struct mlxsw_sp_mr_route *mr_route)
{
	mlxsw_sp_mr_mfc_offload_set(mr_route, false);
	mlxsw_sp_mr_route_erase(mr_table, mr_route);
	rhashtable_remove_fast(&mr_table->route_ht, &mr_route->ht_node,
			       mlxsw_sp_mr_route_ht_params);
	list_del(&mr_route->node);
	mlxsw_sp_mr_route_destroy(mr_table, mr_route);
}

int mlxsw_sp_mr_route4_add(struct mlxsw_sp_mr_table *mr_table,
			   struct mfc_cache *mfc, bool replace)
{
	struct mlxsw_sp_mr_route *mr_orig_route = NULL;
	struct mlxsw_sp_mr_route *mr_route;
	int err;

	if (!mr_table->ops->is_route_valid(mr_table, &mfc->_c))
		return -EINVAL;

	/* Create a new route */
	mr_route = mlxsw_sp_mr_route4_create(mr_table, mfc);
	if (IS_ERR(mr_route))
		return PTR_ERR(mr_route);

	/* Find any route with a matching key */
	mr_orig_route = rhashtable_lookup_fast(&mr_table->route_ht,
					       &mr_route->key,
					       mlxsw_sp_mr_route_ht_params);
	if (replace) {
		/* On replace case, make the route point to the new route_priv.
		 */
		if (WARN_ON(!mr_orig_route)) {
			err = -ENOENT;
			goto err_no_orig_route;
		}
		mr_route->route_priv = mr_orig_route->route_priv;
	} else if (mr_orig_route) {
		/* On non replace case, if another route with the same key was
		 * found, abort, as duplicate routes are used for proxy routes.
		 */
		dev_warn(mr_table->mlxsw_sp->bus_info->dev,
			 "Offloading proxy routes is not supported.\n");
		err = -EINVAL;
		goto err_duplicate_route;
	}

	/* Put it in the table data-structures */
	list_add_tail(&mr_route->node, &mr_table->route_list);
	err = rhashtable_insert_fast(&mr_table->route_ht,
				     &mr_route->ht_node,
				     mlxsw_sp_mr_route_ht_params);
	if (err)
		goto err_rhashtable_insert;

	/* Write the route to the hardware */
	err = mlxsw_sp_mr_route_write(mr_table, mr_route, replace);
	if (err)
		goto err_mr_route_write;

	/* Destroy the original route */
	if (replace) {
		rhashtable_remove_fast(&mr_table->route_ht,
				       &mr_orig_route->ht_node,
				       mlxsw_sp_mr_route_ht_params);
		list_del(&mr_orig_route->node);
		mlxsw_sp_mr_route4_destroy(mr_table, mr_orig_route);
	}

	mlxsw_sp_mr_mfc_offload_update(mr_route);
	return 0;

err_mr_route_write:
	rhashtable_remove_fast(&mr_table->route_ht, &mr_route->ht_node,
			       mlxsw_sp_mr_route_ht_params);
err_rhashtable_insert:
	list_del(&mr_route->node);
err_no_orig_route:
err_duplicate_route:
	mlxsw_sp_mr_route4_destroy(mr_table, mr_route);
	return err;
}

void mlxsw_sp_mr_route4_del(struct mlxsw_sp_mr_table *mr_table,
			    struct mfc_cache *mfc)
{
	struct mlxsw_sp_mr_route *mr_route;
	struct mlxsw_sp_mr_route_key key;

	mr_table->ops->key_create(mr_table, &key, &mfc->_c);
	mr_route = rhashtable_lookup_fast(&mr_table->route_ht, &key,
					  mlxsw_sp_mr_route_ht_params);
	if (mr_route)
		__mlxsw_sp_mr_route_del(mr_table, mr_route);
}

/* Should be called after the VIF struct is updated */
static int
mlxsw_sp_mr_route_ivif_resolve(struct mlxsw_sp_mr_table *mr_table,
			       struct mlxsw_sp_mr_route_vif_entry *rve)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	enum mlxsw_sp_mr_route_action route_action;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	u16 irif_index;
	int err;

	route_action = mlxsw_sp_mr_route_action(rve->mr_route);
	if (route_action == MLXSW_SP_MR_ROUTE_ACTION_TRAP)
		return 0;

	/* rve->mr_vif->rif is guaranteed to be valid at this stage */
	irif_index = mlxsw_sp_rif_index(rve->mr_vif->rif);
	err = mr->mr_ops->route_irif_update(mlxsw_sp, rve->mr_route->route_priv,
					    irif_index);
	if (err)
		return err;

	err = mr->mr_ops->route_action_update(mlxsw_sp,
					      rve->mr_route->route_priv,
					      route_action);
	if (err)
		/* No need to rollback here because the iRIF change only takes
		 * place after the action has been updated.
		 */
		return err;

	rve->mr_route->route_action = route_action;
	mlxsw_sp_mr_mfc_offload_update(rve->mr_route);
	return 0;
}

static void
mlxsw_sp_mr_route_ivif_unresolve(struct mlxsw_sp_mr_table *mr_table,
				 struct mlxsw_sp_mr_route_vif_entry *rve)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;

	mr->mr_ops->route_action_update(mlxsw_sp, rve->mr_route->route_priv,
					MLXSW_SP_MR_ROUTE_ACTION_TRAP);
	rve->mr_route->route_action = MLXSW_SP_MR_ROUTE_ACTION_TRAP;
	mlxsw_sp_mr_mfc_offload_update(rve->mr_route);
}

/* Should be called after the RIF struct is updated */
static int
mlxsw_sp_mr_route_evif_resolve(struct mlxsw_sp_mr_table *mr_table,
			       struct mlxsw_sp_mr_route_vif_entry *rve)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	enum mlxsw_sp_mr_route_action route_action;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	u16 erif_index = 0;
	int err;

	/* Update the route action, as the new eVIF can be a tunnel or a pimreg
	 * device which will require updating the action.
	 */
	route_action = mlxsw_sp_mr_route_action(rve->mr_route);
	if (route_action != rve->mr_route->route_action) {
		err = mr->mr_ops->route_action_update(mlxsw_sp,
						      rve->mr_route->route_priv,
						      route_action);
		if (err)
			return err;
	}

	/* Add the eRIF */
	if (mlxsw_sp_mr_vif_valid(rve->mr_vif)) {
		erif_index = mlxsw_sp_rif_index(rve->mr_vif->rif);
		err = mr->mr_ops->route_erif_add(mlxsw_sp,
						 rve->mr_route->route_priv,
						 erif_index);
		if (err)
			goto err_route_erif_add;
	}

	/* Update the minimum MTU */
	if (rve->mr_vif->dev->mtu < rve->mr_route->min_mtu) {
		rve->mr_route->min_mtu = rve->mr_vif->dev->mtu;
		err = mr->mr_ops->route_min_mtu_update(mlxsw_sp,
						       rve->mr_route->route_priv,
						       rve->mr_route->min_mtu);
		if (err)
			goto err_route_min_mtu_update;
	}

	rve->mr_route->route_action = route_action;
	mlxsw_sp_mr_mfc_offload_update(rve->mr_route);
	return 0;

err_route_min_mtu_update:
	if (mlxsw_sp_mr_vif_valid(rve->mr_vif))
		mr->mr_ops->route_erif_del(mlxsw_sp, rve->mr_route->route_priv,
					   erif_index);
err_route_erif_add:
	if (route_action != rve->mr_route->route_action)
		mr->mr_ops->route_action_update(mlxsw_sp,
						rve->mr_route->route_priv,
						rve->mr_route->route_action);
	return err;
}

/* Should be called before the RIF struct is updated */
static void
mlxsw_sp_mr_route_evif_unresolve(struct mlxsw_sp_mr_table *mr_table,
				 struct mlxsw_sp_mr_route_vif_entry *rve)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	enum mlxsw_sp_mr_route_action route_action;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	u16 rifi;

	/* If the unresolved RIF was not valid, no need to delete it */
	if (!mlxsw_sp_mr_vif_valid(rve->mr_vif))
		return;

	/* Update the route action: if there is only one valid eVIF in the
	 * route, set the action to trap as the VIF deletion will lead to zero
	 * valid eVIFs. On any other case, use the mlxsw_sp_mr_route_action to
	 * determine the route action.
	 */
	if (mlxsw_sp_mr_route_valid_evifs_num(rve->mr_route) == 1)
		route_action = MLXSW_SP_MR_ROUTE_ACTION_TRAP;
	else
		route_action = mlxsw_sp_mr_route_action(rve->mr_route);
	if (route_action != rve->mr_route->route_action)
		mr->mr_ops->route_action_update(mlxsw_sp,
						rve->mr_route->route_priv,
						route_action);

	/* Delete the erif from the route */
	rifi = mlxsw_sp_rif_index(rve->mr_vif->rif);
	mr->mr_ops->route_erif_del(mlxsw_sp, rve->mr_route->route_priv, rifi);
	rve->mr_route->route_action = route_action;
	mlxsw_sp_mr_mfc_offload_update(rve->mr_route);
}

static int mlxsw_sp_mr_vif_resolve(struct mlxsw_sp_mr_table *mr_table,
				   struct net_device *dev,
				   struct mlxsw_sp_mr_vif *mr_vif,
				   unsigned long vif_flags,
				   const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_mr_route_vif_entry *irve, *erve;
	int err;

	/* Update the VIF */
	mr_vif->dev = dev;
	mr_vif->rif = rif;
	mr_vif->vif_flags = vif_flags;

	/* Update all routes where this VIF is used as an unresolved iRIF */
	list_for_each_entry(irve, &mr_vif->route_ivif_list, vif_node) {
		err = mlxsw_sp_mr_route_ivif_resolve(mr_table, irve);
		if (err)
			goto err_irif_unresolve;
	}

	/* Update all routes where this VIF is used as an unresolved eRIF */
	list_for_each_entry(erve, &mr_vif->route_evif_list, vif_node) {
		err = mlxsw_sp_mr_route_evif_resolve(mr_table, erve);
		if (err)
			goto err_erif_unresolve;
	}
	return 0;

err_erif_unresolve:
	list_for_each_entry_from_reverse(erve, &mr_vif->route_evif_list,
					 vif_node)
		mlxsw_sp_mr_route_evif_unresolve(mr_table, erve);
err_irif_unresolve:
	list_for_each_entry_from_reverse(irve, &mr_vif->route_ivif_list,
					 vif_node)
		mlxsw_sp_mr_route_ivif_unresolve(mr_table, irve);
	mr_vif->rif = NULL;
	return err;
}

static void mlxsw_sp_mr_vif_unresolve(struct mlxsw_sp_mr_table *mr_table,
				      struct net_device *dev,
				      struct mlxsw_sp_mr_vif *mr_vif)
{
	struct mlxsw_sp_mr_route_vif_entry *rve;

	/* Update all routes where this VIF is used as an unresolved eRIF */
	list_for_each_entry(rve, &mr_vif->route_evif_list, vif_node)
		mlxsw_sp_mr_route_evif_unresolve(mr_table, rve);

	/* Update all routes where this VIF is used as an unresolved iRIF */
	list_for_each_entry(rve, &mr_vif->route_ivif_list, vif_node)
		mlxsw_sp_mr_route_ivif_unresolve(mr_table, rve);

	/* Update the VIF */
	mr_vif->dev = dev;
	mr_vif->rif = NULL;
}

int mlxsw_sp_mr_vif_add(struct mlxsw_sp_mr_table *mr_table,
			struct net_device *dev, vifi_t vif_index,
			unsigned long vif_flags, const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_mr_vif *mr_vif = &mr_table->vifs[vif_index];

	if (WARN_ON(vif_index >= MAXVIFS))
		return -EINVAL;
	if (mr_vif->dev)
		return -EEXIST;
	return mlxsw_sp_mr_vif_resolve(mr_table, dev, mr_vif, vif_flags, rif);
}

void mlxsw_sp_mr_vif_del(struct mlxsw_sp_mr_table *mr_table, vifi_t vif_index)
{
	struct mlxsw_sp_mr_vif *mr_vif = &mr_table->vifs[vif_index];

	if (WARN_ON(vif_index >= MAXVIFS))
		return;
	if (WARN_ON(!mr_vif->dev))
		return;
	mlxsw_sp_mr_vif_unresolve(mr_table, NULL, mr_vif);
}

static struct mlxsw_sp_mr_vif *
mlxsw_sp_mr_dev_vif_lookup(struct mlxsw_sp_mr_table *mr_table,
			   const struct net_device *dev)
{
	vifi_t vif_index;

	for (vif_index = 0; vif_index < MAXVIFS; vif_index++)
		if (mr_table->vifs[vif_index].dev == dev)
			return &mr_table->vifs[vif_index];
	return NULL;
}

int mlxsw_sp_mr_rif_add(struct mlxsw_sp_mr_table *mr_table,
			const struct mlxsw_sp_rif *rif)
{
	const struct net_device *rif_dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp_mr_vif *mr_vif;

	if (!rif_dev)
		return 0;

	mr_vif = mlxsw_sp_mr_dev_vif_lookup(mr_table, rif_dev);
	if (!mr_vif)
		return 0;
	return mlxsw_sp_mr_vif_resolve(mr_table, mr_vif->dev, mr_vif,
				       mr_vif->vif_flags, rif);
}

void mlxsw_sp_mr_rif_del(struct mlxsw_sp_mr_table *mr_table,
			 const struct mlxsw_sp_rif *rif)
{
	const struct net_device *rif_dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp_mr_vif *mr_vif;

	if (!rif_dev)
		return;

	mr_vif = mlxsw_sp_mr_dev_vif_lookup(mr_table, rif_dev);
	if (!mr_vif)
		return;
	mlxsw_sp_mr_vif_unresolve(mr_table, mr_vif->dev, mr_vif);
}

void mlxsw_sp_mr_rif_mtu_update(struct mlxsw_sp_mr_table *mr_table,
				const struct mlxsw_sp_rif *rif, int mtu)
{
	const struct net_device *rif_dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	struct mlxsw_sp_mr_route_vif_entry *rve;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	struct mlxsw_sp_mr_vif *mr_vif;

	if (!rif_dev)
		return;

	/* Search for a VIF that use that RIF */
	mr_vif = mlxsw_sp_mr_dev_vif_lookup(mr_table, rif_dev);
	if (!mr_vif)
		return;

	/* Update all the routes that uses that VIF as eVIF */
	list_for_each_entry(rve, &mr_vif->route_evif_list, vif_node) {
		if (mtu < rve->mr_route->min_mtu) {
			rve->mr_route->min_mtu = mtu;
			mr->mr_ops->route_min_mtu_update(mlxsw_sp,
							 rve->mr_route->route_priv,
							 mtu);
		}
	}
}

/* Protocol specific functions */
static bool
mlxsw_sp_mr_route4_validate(const struct mlxsw_sp_mr_table *mr_table,
			    const struct mr_mfc *c)
{
	struct mfc_cache *mfc = (struct mfc_cache *) c;

	/* If the route is a (*,*) route, abort, as these kind of routes are
	 * used for proxy routes.
	 */
	if (mfc->mfc_origin == htonl(INADDR_ANY) &&
	    mfc->mfc_mcastgrp == htonl(INADDR_ANY)) {
		dev_warn(mr_table->mlxsw_sp->bus_info->dev,
			 "Offloading proxy routes is not supported.\n");
		return false;
	}
	return true;
}

static void mlxsw_sp_mr_route4_key(struct mlxsw_sp_mr_table *mr_table,
				   struct mlxsw_sp_mr_route_key *key,
				   struct mr_mfc *c)
{
	const struct mfc_cache *mfc = (struct mfc_cache *) c;
	bool starg;

	starg = (mfc->mfc_origin == htonl(INADDR_ANY));

	memset(key, 0, sizeof(*key));
	key->vrid = mr_table->vr_id;
	key->proto = MLXSW_SP_L3_PROTO_IPV4;
	key->group.addr4 = mfc->mfc_mcastgrp;
	key->group_mask.addr4 = htonl(0xffffffff);
	key->source.addr4 = mfc->mfc_origin;
	key->source_mask.addr4 = htonl(starg ? 0 : 0xffffffff);
}

static bool mlxsw_sp_mr_route4_starg(const struct mlxsw_sp_mr_table *mr_table,
				     const struct mlxsw_sp_mr_route *mr_route)
{
	return mr_route->key.source_mask.addr4 == htonl(INADDR_ANY);
}

static bool mlxsw_sp_mr_vif4_is_regular(const struct mlxsw_sp_mr_vif *vif)
{
	return !(vif->vif_flags & (VIFF_TUNNEL | VIFF_REGISTER));
}

static struct
mlxsw_sp_mr_vif_ops mlxsw_sp_mr_vif_ops_arr[] = {
	{
		.is_regular = mlxsw_sp_mr_vif4_is_regular,
	},
};

static struct
mlxsw_sp_mr_table_ops mlxsw_sp_mr_table_ops_arr[] = {
	{
		.is_route_valid = mlxsw_sp_mr_route4_validate,
		.key_create = mlxsw_sp_mr_route4_key,
		.is_route_starg = mlxsw_sp_mr_route4_starg,
	},
};

struct mlxsw_sp_mr_table *mlxsw_sp_mr_table_create(struct mlxsw_sp *mlxsw_sp,
						   u32 vr_id,
						   enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_mr_route_params catchall_route_params = {
		.prio = MLXSW_SP_MR_ROUTE_PRIO_CATCHALL,
		.key = {
			.vrid = vr_id,
			.proto = proto,
		},
		.value = {
			.route_action = MLXSW_SP_MR_ROUTE_ACTION_TRAP,
		}
	};
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	struct mlxsw_sp_mr_table *mr_table;
	int err;
	int i;

	mr_table = kzalloc(sizeof(*mr_table) + mr->mr_ops->route_priv_size,
			   GFP_KERNEL);
	if (!mr_table)
		return ERR_PTR(-ENOMEM);

	mr_table->vr_id = vr_id;
	mr_table->mlxsw_sp = mlxsw_sp;
	mr_table->proto = proto;
	mr_table->ops = &mlxsw_sp_mr_table_ops_arr[proto];
	INIT_LIST_HEAD(&mr_table->route_list);

	err = rhashtable_init(&mr_table->route_ht,
			      &mlxsw_sp_mr_route_ht_params);
	if (err)
		goto err_route_rhashtable_init;

	for (i = 0; i < MAXVIFS; i++) {
		INIT_LIST_HEAD(&mr_table->vifs[i].route_evif_list);
		INIT_LIST_HEAD(&mr_table->vifs[i].route_ivif_list);
		mr_table->vifs[i].ops = &mlxsw_sp_mr_vif_ops_arr[proto];
	}

	err = mr->mr_ops->route_create(mlxsw_sp, mr->priv,
				       mr_table->catchall_route_priv,
				       &catchall_route_params);
	if (err)
		goto err_ops_route_create;
	list_add_tail(&mr_table->node, &mr->table_list);
	return mr_table;

err_ops_route_create:
	rhashtable_destroy(&mr_table->route_ht);
err_route_rhashtable_init:
	kfree(mr_table);
	return ERR_PTR(err);
}

void mlxsw_sp_mr_table_destroy(struct mlxsw_sp_mr_table *mr_table)
{
	struct mlxsw_sp *mlxsw_sp = mr_table->mlxsw_sp;
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;

	WARN_ON(!mlxsw_sp_mr_table_empty(mr_table));
	list_del(&mr_table->node);
	mr->mr_ops->route_destroy(mlxsw_sp, mr->priv,
				  &mr_table->catchall_route_priv);
	rhashtable_destroy(&mr_table->route_ht);
	kfree(mr_table);
}

void mlxsw_sp_mr_table_flush(struct mlxsw_sp_mr_table *mr_table)
{
	struct mlxsw_sp_mr_route *mr_route, *tmp;
	int i;

	list_for_each_entry_safe(mr_route, tmp, &mr_table->route_list, node)
		__mlxsw_sp_mr_route_del(mr_table, mr_route);

	for (i = 0; i < MAXVIFS; i++) {
		mr_table->vifs[i].dev = NULL;
		mr_table->vifs[i].rif = NULL;
	}
}

bool mlxsw_sp_mr_table_empty(const struct mlxsw_sp_mr_table *mr_table)
{
	int i;

	for (i = 0; i < MAXVIFS; i++)
		if (mr_table->vifs[i].dev)
			return false;
	return list_empty(&mr_table->route_list);
}

static void mlxsw_sp_mr_route_stats_update(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_mr_route *mr_route)
{
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;
	u64 packets, bytes;

	if (mr_route->route_action == MLXSW_SP_MR_ROUTE_ACTION_TRAP)
		return;

	mr->mr_ops->route_stats(mlxsw_sp, mr_route->route_priv, &packets,
				&bytes);

	if (mr_route->mfc->mfc_un.res.pkt != packets)
		mr_route->mfc->mfc_un.res.lastuse = jiffies;
	mr_route->mfc->mfc_un.res.pkt = packets;
	mr_route->mfc->mfc_un.res.bytes = bytes;
}

static void mlxsw_sp_mr_stats_update(struct work_struct *work)
{
	struct mlxsw_sp_mr *mr = container_of(work, struct mlxsw_sp_mr,
					      stats_update_dw.work);
	struct mlxsw_sp_mr_table *mr_table;
	struct mlxsw_sp_mr_route *mr_route;
	unsigned long interval;

	rtnl_lock();
	list_for_each_entry(mr_table, &mr->table_list, node)
		list_for_each_entry(mr_route, &mr_table->route_list, node)
			mlxsw_sp_mr_route_stats_update(mr_table->mlxsw_sp,
						       mr_route);
	rtnl_unlock();

	interval = msecs_to_jiffies(MLXSW_SP_MR_ROUTES_COUNTER_UPDATE_INTERVAL);
	mlxsw_core_schedule_dw(&mr->stats_update_dw, interval);
}

int mlxsw_sp_mr_init(struct mlxsw_sp *mlxsw_sp,
		     const struct mlxsw_sp_mr_ops *mr_ops)
{
	struct mlxsw_sp_mr *mr;
	unsigned long interval;
	int err;

	mr = kzalloc(sizeof(*mr) + mr_ops->priv_size, GFP_KERNEL);
	if (!mr)
		return -ENOMEM;
	mr->mr_ops = mr_ops;
	mlxsw_sp->mr = mr;
	INIT_LIST_HEAD(&mr->table_list);

	err = mr_ops->init(mlxsw_sp, mr->priv);
	if (err)
		goto err;

	/* Create the delayed work for counter updates */
	INIT_DELAYED_WORK(&mr->stats_update_dw, mlxsw_sp_mr_stats_update);
	interval = msecs_to_jiffies(MLXSW_SP_MR_ROUTES_COUNTER_UPDATE_INTERVAL);
	mlxsw_core_schedule_dw(&mr->stats_update_dw, interval);
	return 0;
err:
	kfree(mr);
	return err;
}

void mlxsw_sp_mr_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_mr *mr = mlxsw_sp->mr;

	cancel_delayed_work_sync(&mr->stats_update_dw);
	mr->mr_ops->fini(mr->priv);
	kfree(mr);
}
