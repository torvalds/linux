// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/skbuff.h>
#include <net/psample.h>
#include "en/mapping.h"
#include "esw/sample.h"
#include "eswitch.h"
#include "en_tc.h"
#include "fs_core.h"

#define MLX5_ESW_VPORT_TBL_SIZE_SAMPLE (64 * 1024)

static const struct esw_vport_tbl_namespace mlx5_esw_vport_tbl_sample_ns = {
	.max_fte = MLX5_ESW_VPORT_TBL_SIZE_SAMPLE,
	.max_num_groups = 0,    /* default num of groups */
	.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT | MLX5_FLOW_TABLE_TUNNEL_EN_DECAP,
};

struct mlx5_esw_psample {
	struct mlx5_eswitch *esw;
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
	struct mlx5_flow_attr *pre_attr;
	struct mlx5_flow_handle *pre_rule;
	struct mlx5_flow_handle *rule;
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
	struct mlx5_eswitch *esw = esw_psample->esw;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_core_dev *dev = esw->dev;
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

	err = sampler_obj_create(esw_psample->esw->dev, sampler);
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
		sampler_obj_destroy(esw_psample->esw->dev, sampler->sampler_id);
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
	struct mlx5_eswitch *esw = esw_psample->esw;
	struct mlx5_core_dev *mdev = esw->dev;
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
		mlx5_modify_header_dealloc(esw_psample->esw->dev, restore->modify_hdr);
		kfree(restore);
	}
}

void mlx5_esw_sample_skb(struct sk_buff *skb, struct mlx5_mapped_obj *mapped_obj)
{
	u32 trunc_size = mapped_obj->sample.trunc_size;
	struct psample_group psample_group = {};
	struct psample_metadata md = {};

	md.trunc_size = trunc_size ? min(trunc_size, skb->len) : skb->len;
	md.in_ifindex = skb->dev->ifindex;
	psample_group.group_num = mapped_obj->sample.group_id;
	psample_group.net = &init_net;
	skb_push(skb, skb->mac_len);

	psample_sample_packet(&psample_group, skb, mapped_obj->sample.rate, &md);
}

/* For the following typical flow table:
 *
 * +-------------------------------+
 * +       original flow table     +
 * +-------------------------------+
 * +         original match        +
 * +-------------------------------+
 * + sample action + other actions +
 * +-------------------------------+
 *
 * We translate the tc filter with sample action to the following HW model:
 *
 *         +---------------------+
 *         + original flow table +
 *         +---------------------+
 *         +   original match    +
 *         +---------------------+
 *                    |
 *                    v
 * +------------------------------------------------+
 * +                Flow Sampler Object             +
 * +------------------------------------------------+
 * +                    sample ratio                +
 * +------------------------------------------------+
 * +    sample table id    |    default table id    +
 * +------------------------------------------------+
 *            |                            |
 *            v                            v
 * +-----------------------------+  +----------------------------------------+
 * +        sample table         +  + default table per <vport, chain, prio> +
 * +-----------------------------+  +----------------------------------------+
 * + forward to management vport +  +            original match              +
 * +-----------------------------+  +----------------------------------------+
 *                                  +            other actions               +
 *                                  +----------------------------------------+
 */
struct mlx5_flow_handle *
mlx5_esw_sample_offload(struct mlx5_esw_psample *esw_psample,
			struct mlx5_flow_spec *spec,
			struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_vport_tbl_attr per_vport_tbl_attr;
	struct mlx5_esw_flow_attr *pre_esw_attr;
	struct mlx5_mapped_obj restore_obj = {};
	struct mlx5_sample_flow *sample_flow;
	struct mlx5_sample_attr *sample_attr;
	struct mlx5_flow_table *default_tbl;
	struct mlx5_flow_attr *pre_attr;
	struct mlx5_eswitch *esw;
	u32 obj_id;
	int err;

	if (IS_ERR_OR_NULL(esw_psample))
		return ERR_PTR(-EOPNOTSUPP);

	/* If slow path flag is set, eg. when the neigh is invalid for encap,
	 * don't offload sample action.
	 */
	esw = esw_psample->esw;
	if (attr->flags & MLX5_ESW_ATTR_FLAG_SLOW_PATH)
		return mlx5_eswitch_add_offloaded_rule(esw, spec, attr);

	sample_flow = kzalloc(sizeof(*sample_flow), GFP_KERNEL);
	if (!sample_flow)
		return ERR_PTR(-ENOMEM);
	esw_attr->sample->sample_flow = sample_flow;

	/* Allocate default table per vport, chain and prio. Otherwise, there is
	 * only one default table for the same sampler object. Rules with different
	 * prio and chain may overlap. For CT sample action, per vport default
	 * table is needed to resotre the metadata.
	 */
	per_vport_tbl_attr.chain = attr->chain;
	per_vport_tbl_attr.prio = attr->prio;
	per_vport_tbl_attr.vport = esw_attr->in_rep->vport;
	per_vport_tbl_attr.vport_ns = &mlx5_esw_vport_tbl_sample_ns;
	default_tbl = mlx5_esw_vporttbl_get(esw, &per_vport_tbl_attr);
	if (IS_ERR(default_tbl)) {
		err = PTR_ERR(default_tbl);
		goto err_default_tbl;
	}

	/* Perform the original matches on the default table.
	 * Offload all actions except the sample action.
	 */
	esw_attr->sample->sample_default_tbl = default_tbl;
	/* When offloading sample and encap action, if there is no valid
	 * neigh data struct, a slow path rule is offloaded first. Source
	 * port metadata match is set at that time. A per vport table is
	 * already allocated. No need to match it again. So clear the source
	 * port metadata match.
	 */
	mlx5_eswitch_clear_rule_source_port(esw, spec);
	sample_flow->rule = mlx5_eswitch_add_offloaded_rule(esw, spec, attr);
	if (IS_ERR(sample_flow->rule)) {
		err = PTR_ERR(sample_flow->rule);
		goto err_offload_rule;
	}

	/* Create sampler object. */
	sample_flow->sampler = sampler_get(esw_psample, esw_attr->sample->rate, default_tbl->id);
	if (IS_ERR(sample_flow->sampler)) {
		err = PTR_ERR(sample_flow->sampler);
		goto err_sampler;
	}

	/* Create an id mapping reg_c0 value to sample object. */
	restore_obj.type = MLX5_MAPPED_OBJ_SAMPLE;
	restore_obj.sample.group_id = esw_attr->sample->group_num;
	restore_obj.sample.rate = esw_attr->sample->rate;
	restore_obj.sample.trunc_size = esw_attr->sample->trunc_size;
	err = mapping_add(esw->offloads.reg_c0_obj_pool, &restore_obj, &obj_id);
	if (err)
		goto err_obj_id;
	esw_attr->sample->restore_obj_id = obj_id;

	/* Create sample restore context. */
	sample_flow->restore = sample_restore_get(esw_psample, obj_id);
	if (IS_ERR(sample_flow->restore)) {
		err = PTR_ERR(sample_flow->restore);
		goto err_sample_restore;
	}

	/* Perform the original matches on the original table. Offload the
	 * sample action. The destination is the sampler object.
	 */
	pre_attr = mlx5_alloc_flow_attr(MLX5_FLOW_NAMESPACE_FDB);
	if (!pre_attr) {
		err = -ENOMEM;
		goto err_alloc_flow_attr;
	}
	sample_attr = kzalloc(sizeof(*sample_attr), GFP_KERNEL);
	if (!sample_attr) {
		err = -ENOMEM;
		goto err_alloc_sample_attr;
	}
	pre_esw_attr = pre_attr->esw_attr;
	pre_attr->action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	pre_attr->modify_hdr = sample_flow->restore->modify_hdr;
	pre_attr->flags = MLX5_ESW_ATTR_FLAG_SAMPLE;
	pre_attr->chain = attr->chain;
	pre_attr->prio = attr->prio;
	pre_esw_attr->sample = sample_attr;
	pre_esw_attr->sample->sampler_id = sample_flow->sampler->sampler_id;
	pre_esw_attr->in_mdev = esw_attr->in_mdev;
	pre_esw_attr->in_rep = esw_attr->in_rep;
	sample_flow->pre_rule = mlx5_eswitch_add_offloaded_rule(esw, spec, pre_attr);
	if (IS_ERR(sample_flow->pre_rule)) {
		err = PTR_ERR(sample_flow->pre_rule);
		goto err_pre_offload_rule;
	}
	sample_flow->pre_attr = pre_attr;

	return sample_flow->rule;

err_pre_offload_rule:
	kfree(sample_attr);
err_alloc_sample_attr:
	kfree(pre_attr);
err_alloc_flow_attr:
	sample_restore_put(esw_psample, sample_flow->restore);
err_sample_restore:
	mapping_remove(esw->offloads.reg_c0_obj_pool, obj_id);
err_obj_id:
	sampler_put(esw_psample, sample_flow->sampler);
err_sampler:
	/* For sample offload, rule is added in default_tbl. No need to call
	 * mlx5_esw_chains_put_table()
	 */
	attr->prio = 0;
	attr->chain = 0;
	mlx5_eswitch_del_offloaded_rule(esw, sample_flow->rule, attr);
err_offload_rule:
	mlx5_esw_vporttbl_put(esw, &per_vport_tbl_attr);
err_default_tbl:
	kfree(sample_flow);
	return ERR_PTR(err);
}

void
mlx5_esw_sample_unoffload(struct mlx5_esw_psample *esw_psample,
			  struct mlx5_flow_handle *rule,
			  struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_sample_flow *sample_flow;
	struct mlx5_vport_tbl_attr tbl_attr;
	struct mlx5_flow_attr *pre_attr;
	struct mlx5_eswitch *esw;

	if (IS_ERR_OR_NULL(esw_psample))
		return;

	/* If slow path flag is set, sample action is not offloaded.
	 * No need to delete sample rule.
	 */
	esw = esw_psample->esw;
	if (attr->flags & MLX5_ESW_ATTR_FLAG_SLOW_PATH) {
		mlx5_eswitch_del_offloaded_rule(esw, rule, attr);
		return;
	}

	sample_flow = esw_attr->sample->sample_flow;
	pre_attr = sample_flow->pre_attr;
	memset(pre_attr, 0, sizeof(*pre_attr));
	mlx5_eswitch_del_offloaded_rule(esw, sample_flow->pre_rule, pre_attr);
	mlx5_eswitch_del_offloaded_rule(esw, sample_flow->rule, attr);

	sample_restore_put(esw_psample, sample_flow->restore);
	mapping_remove(esw->offloads.reg_c0_obj_pool, esw_attr->sample->restore_obj_id);
	sampler_put(esw_psample, sample_flow->sampler);
	tbl_attr.chain = attr->chain;
	tbl_attr.prio = attr->prio;
	tbl_attr.vport = esw_attr->in_rep->vport;
	tbl_attr.vport_ns = &mlx5_esw_vport_tbl_sample_ns;
	mlx5_esw_vporttbl_put(esw, &tbl_attr);

	kfree(pre_attr->esw_attr->sample);
	kfree(pre_attr);
	kfree(sample_flow);
}

struct mlx5_esw_psample *
mlx5_esw_sample_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_psample *esw_psample;
	int err;

	esw_psample = kzalloc(sizeof(*esw_psample), GFP_KERNEL);
	if (!esw_psample)
		return ERR_PTR(-ENOMEM);
	esw_psample->esw = esw;
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
