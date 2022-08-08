// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021 Mellanox Technologies.

#include "eswitch.h"

/* This struct is used as a key to the hash table and we need it to be packed
 * so hash result is consistent
 */
struct mlx5_vport_key {
	u32 chain;
	u16 prio;
	u16 vport;
	u16 vhca_id;
	const struct esw_vport_tbl_namespace *vport_ns;
} __packed;

struct mlx5_vport_table {
	struct hlist_node hlist;
	struct mlx5_flow_table *fdb;
	u32 num_rules;
	struct mlx5_vport_key key;
};

static struct mlx5_flow_table *
esw_vport_tbl_create(struct mlx5_eswitch *esw, struct mlx5_flow_namespace *ns,
		     const struct esw_vport_tbl_namespace *vport_ns)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *fdb;

	if (vport_ns->max_num_groups)
		ft_attr.autogroup.max_num_groups = vport_ns->max_num_groups;
	else
		ft_attr.autogroup.max_num_groups = esw->params.large_group_num;
	ft_attr.max_fte = vport_ns->max_fte;
	ft_attr.prio = FDB_PER_VPORT;
	ft_attr.flags = vport_ns->flags;
	fdb = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb)) {
		esw_warn(esw->dev, "Failed to create per vport FDB Table err %ld\n",
			 PTR_ERR(fdb));
	}

	return fdb;
}

static u32 flow_attr_to_vport_key(struct mlx5_eswitch *esw,
				  struct mlx5_vport_tbl_attr *attr,
				  struct mlx5_vport_key *key)
{
	key->vport = attr->vport;
	key->chain = attr->chain;
	key->prio = attr->prio;
	key->vhca_id = MLX5_CAP_GEN(esw->dev, vhca_id);
	key->vport_ns  = attr->vport_ns;
	return jhash(key, sizeof(*key), 0);
}

/* caller must hold vports.lock */
static struct mlx5_vport_table *
esw_vport_tbl_lookup(struct mlx5_eswitch *esw, struct mlx5_vport_key *skey, u32 key)
{
	struct mlx5_vport_table *e;

	hash_for_each_possible(esw->fdb_table.offloads.vports.table, e, hlist, key)
		if (!memcmp(&e->key, skey, sizeof(*skey)))
			return e;

	return NULL;
}

struct mlx5_flow_table *
mlx5_esw_vporttbl_get(struct mlx5_eswitch *esw, struct mlx5_vport_tbl_attr *attr)
{
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *fdb;
	struct mlx5_vport_table *e;
	struct mlx5_vport_key skey;
	u32 hkey;

	mutex_lock(&esw->fdb_table.offloads.vports.lock);
	hkey = flow_attr_to_vport_key(esw, attr, &skey);
	e = esw_vport_tbl_lookup(esw, &skey, hkey);
	if (e) {
		e->num_rules++;
		goto out;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		fdb = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!ns) {
		esw_warn(dev, "Failed to get FDB namespace\n");
		fdb = ERR_PTR(-ENOENT);
		goto err_ns;
	}

	fdb = esw_vport_tbl_create(esw, ns, attr->vport_ns);
	if (IS_ERR(fdb))
		goto err_ns;

	e->fdb = fdb;
	e->num_rules = 1;
	e->key = skey;
	hash_add(esw->fdb_table.offloads.vports.table, &e->hlist, hkey);
out:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
	return e->fdb;

err_ns:
	kfree(e);
err_alloc:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
	return fdb;
}

void
mlx5_esw_vporttbl_put(struct mlx5_eswitch *esw, struct mlx5_vport_tbl_attr *attr)
{
	struct mlx5_vport_table *e;
	struct mlx5_vport_key key;
	u32 hkey;

	mutex_lock(&esw->fdb_table.offloads.vports.lock);
	hkey = flow_attr_to_vport_key(esw, attr, &key);
	e = esw_vport_tbl_lookup(esw, &key, hkey);
	if (!e || --e->num_rules)
		goto out;

	hash_del(&e->hlist);
	mlx5_destroy_flow_table(e->fdb);
	kfree(e);
out:
	mutex_unlock(&esw->fdb_table.offloads.vports.lock);
}
