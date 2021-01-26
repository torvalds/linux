// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/skbuff.h>
#include <net/psample.h>
#include "esw/sample.h"
#include "eswitch.h"
#include "en_tc.h"
#include "fs_core.h"

struct mlx5_esw_psample {
	struct mlx5e_priv *priv;
	struct mlx5_flow_table *termtbl;
	struct mlx5_flow_handle *termtbl_rule;
	DECLARE_HASHTABLE(hashtbl, 8);
	struct mutex ht_lock; /* protect hashtbl */
	DECLARE_HASHTABLE(restore_hashtbl, 8);
	struct mutex restore_lock; /* protect restore_hashtbl */
};

struct mlx5_sampler {
	struct hlist_node hlist;
	u32 sampler_id;
	u32 sample_ratio;
	u32 sample_table_id;
	u32 default_table_id;
	int count;
};

struct mlx5_sample_flow {
	struct mlx5_sampler *sampler;
	struct mlx5_sample_restore *restore;
};

struct mlx5_sample_restore {
	struct hlist_node hlist;
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_flow_handle *rule;
	u32 obj_id;
	int count;
};

static int
sampler_termtbl_create(struct mlx5_esw_psample *esw_psample)
{
	struct mlx5_core_dev *dev = esw_psample->priv->mdev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_act act = {};
	int err;

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(dev, termination_table))  {
		mlx5_core_warn(dev, "termination table is not supported\n");
		return -EOPNOTSUPP;
	}

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		mlx5_core_warn(dev, "failed to get FDB flow namespace\n");
		return -EOPNOTSUPP;
	}

	ft_attr.flags = MLX5_FLOW_TABLE_TERMINATION | MLX5_FLOW_TABLE_UNMANAGED;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.prio = FDB_SLOW_PATH;
	ft_attr.max_fte = 1;
	ft_attr.level = 1;
	esw_psample->termtbl = mlx5_create_auto_grouped_flow_table(root_ns, &ft_attr);
	if (IS_ERR(esw_psample->termtbl)) {
		err = PTR_ERR(esw_psample->termtbl);
		mlx5_core_warn(dev, "failed to create termtbl, err: %d\n", err);
		return err;
	}

	act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.vport.num = esw->manager_vport;
	esw_psample->termtbl_rule = mlx5_add_flow_rules(esw_psample->termtbl, NULL, &act, &dest, 1);
	if (IS_ERR(esw_psample->termtbl_rule)) {
		err = PTR_ERR(esw_psample->termtbl_rule);
		mlx5_core_warn(dev, "failed to create termtbl rule, err: %d\n", err);
		mlx5_destroy_flow_table(esw_psample->termtbl);
		return err;
	}

	return 0;
}

static void
sampler_termtbl_destroy(struct mlx5_esw_psample *esw_psample)
{
	mlx5_del_flow_rules(esw_psample->termtbl_rule);
	mlx5_destroy_flow_table(esw_psample->termtbl);
}

static int
sampler_obj_create(struct mlx5_core_dev *mdev, struct mlx5_sampler *sampler)
{
	u32 in[MLX5_ST_SZ_DW(create_sampler_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u64 general_obj_types;
	void *obj;
	int err;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_SAMPLER))
		return -EOPNOTSUPP;
	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, ignore_flow_level))
		return -EOPNOTSUPP;

	obj = MLX5_ADDR_OF(create_sampler_obj_in, in, sampler_object);
	MLX5_SET(sampler_obj, obj, table_type, FS_FT_FDB);
	MLX5_SET(sampler_obj, obj, ignore_flow_level, 1);
	MLX5_SET(sampler_obj, obj, level, 1);
	MLX5_SET(sampler_obj, obj, sample_ratio, sampler->sample_ratio);
	MLX5_SET(sampler_obj, obj, sample_table_id, sampler->sample_table_id);
	MLX5_SET(sampler_obj, obj, default_table_id, sampler->default_table_id);
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_SAMPLER);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		sampler->sampler_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

static void
sampler_obj_destroy(struct mlx5_core_dev *mdev, u32 sampler_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_SAMPLER);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, sampler_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static u32
sampler_hash(u32 sample_ratio, u32 default_table_id)
{
	return jhash_2words(sample_ratio, default_table_id, 0);
}

static int
sampler_cmp(u32 sample_ratio1, u32 default_table_id1, u32 sample_ratio2, u32 default_table_id2)
{
	return sample_ratio1 != sample_ratio2 || default_table_id1 != default_table_id2;
}

static struct mlx5_sampler *
sampler_get(struct mlx5_esw_psample *esw_psample, u32 sample_ratio, u32 default_table_id)
{
	struct mlx5_sampler *sampler;
	u32 hash_key;
	int err;

	mutex_lock(&esw_psample->ht_lock);
	hash_key = sampler_hash(sample_ratio, default_table_id);
	hash_for_each_possible(esw_psample->hashtbl, sampler, hlist, hash_key)
		if (!sampler_cmp(sampler->sample_ratio, sampler->default_table_id,
				 sample_ratio, default_table_id))
			goto add_ref;

	sampler = kzalloc(sizeof(*sampler), GFP_KERNEL);
	if (!sampler) {
		err = -ENOMEM;
		goto err_alloc;
	}

	sampler->sample_table_id = esw_psample->termtbl->id;
	sampler->default_table_id = default_table_id;
	sampler->sample_ratio = sample_ratio;

	err = sampler_obj_create(esw_psample->priv->mdev, sampler);
	if (err)
		goto err_create;

	hash_add(esw_psample->hashtbl, &sampler->hlist, hash_key);

add_ref:
	sampler->count++;
	mutex_unlock(&esw_psample->ht_lock);
	return sampler;

err_create:
	kfree(sampler);
err_alloc:
	mutex_unlock(&esw_psample->ht_lock);
	return ERR_PTR(err);
}

static void
sampler_put(struct mlx5_esw_psample *esw_psample, struct mlx5_sampler *sampler)
{
	mutex_lock(&esw_psample->ht_lock);
	if (--sampler->count == 0) {
		hash_del(&sampler->hlist);
		sampler_obj_destroy(esw_psample->priv->mdev, sampler->sampler_id);
		kfree(sampler);
	}
	mutex_unlock(&esw_psample->ht_lock);
}

static struct mlx5_modify_hdr *
sample_metadata_rule_get(struct mlx5_core_dev *mdev, u32 obj_id)
{
	struct mlx5e_tc_mod_hdr_acts mod_acts = {};
	struct mlx5_modify_hdr *modify_hdr;
	int err;

	err = mlx5e_tc_match_to_reg_set(mdev, &mod_acts, MLX5_FLOW_NAMESPACE_FDB,
					CHAIN_TO_REG, obj_id);
	if (err)
		goto err_set_regc0;

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_FDB,
					      mod_acts.num_actions,
					      mod_acts.actions);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		goto err_modify_hdr;
	}

	dealloc_mod_hdr_actions(&mod_acts);
	return modify_hdr;

err_modify_hdr:
	dealloc_mod_hdr_actions(&mod_acts);
err_set_regc0:
	return ERR_PTR(err);
}

static struct mlx5_sample_restore *
sample_restore_get(struct mlx5_esw_psample *esw_psample, u32 obj_id)
{
	struct mlx5_core_dev *mdev = esw_psample->priv->mdev;
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	struct mlx5_sample_restore *restore;
	struct mlx5_modify_hdr *modify_hdr;
	int err;

	mutex_lock(&esw_psample->restore_lock);
	hash_for_each_possible(esw_psample->restore_hashtbl, restore, hlist, obj_id)
		if (restore->obj_id == obj_id)
			goto add_ref;

	restore = kzalloc(sizeof(*restore), GFP_KERNEL);
	if (!restore) {
		err = -ENOMEM;
		goto err_alloc;
	}
	restore->obj_id = obj_id;

	modify_hdr = sample_metadata_rule_get(mdev, obj_id);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		goto err_modify_hdr;
	}
	restore->modify_hdr = modify_hdr;

	restore->rule = esw_add_restore_rule(esw, obj_id);
	if (IS_ERR(restore->rule)) {
		err = PTR_ERR(restore->rule);
		goto err_restore;
	}

	hash_add(esw_psample->restore_hashtbl, &restore->hlist, obj_id);
add_ref:
	restore->count++;
	mutex_unlock(&esw_psample->restore_lock);
	return restore;

err_restore:
	mlx5_modify_header_dealloc(mdev, restore->modify_hdr);
err_modify_hdr:
	kfree(restore);
err_alloc:
	mutex_unlock(&esw_psample->restore_lock);
	return ERR_PTR(err);
}

static void
sample_restore_put(struct mlx5_esw_psample *esw_psample, struct mlx5_sample_restore *restore)
{
	mutex_lock(&esw_psample->restore_lock);
	if (--restore->count == 0)
		hash_del(&restore->hlist);
	mutex_unlock(&esw_psample->restore_lock);

	if (!restore->count) {
		mlx5_del_flow_rules(restore->rule);
		mlx5_modify_header_dealloc(esw_psample->priv->mdev, restore->modify_hdr);
		kfree(restore);
	}
}

struct mlx5_esw_psample *
mlx5_esw_sample_init(struct mlx5e_priv *priv)
{
	struct mlx5_esw_psample *esw_psample;
	int err;

	esw_psample = kzalloc(sizeof(*esw_psample), GFP_KERNEL);
	if (!esw_psample)
		return ERR_PTR(-ENOMEM);
	esw_psample->priv = priv;
	err = sampler_termtbl_create(esw_psample);
	if (err)
		goto err_termtbl;

	mutex_init(&esw_psample->ht_lock);
	mutex_init(&esw_psample->restore_lock);

	return esw_psample;

err_termtbl:
	kfree(esw_psample);
	return ERR_PTR(err);
}

void
mlx5_esw_sample_cleanup(struct mlx5_esw_psample *esw_psample)
{
	if (IS_ERR_OR_NULL(esw_psample))
		return;

	mutex_destroy(&esw_psample->restore_lock);
	mutex_destroy(&esw_psample->ht_lock);
	sampler_termtbl_destroy(esw_psample);
	kfree(esw_psample);
}
