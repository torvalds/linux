// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/skbuff.h>
#include <net/psample.h>
#include "en/mapping.h"
#include "en/tc/post_act.h"
#include "en/tc/act/sample.h"
#include "en/mod_hdr.h"
#include "sample.h"
#include "eswitch.h"
#include "en_tc.h"
#include "fs_core.h"

#define MLX5_ESW_VPORT_TBL_SIZE_SAMPLE (64 * 1024)

static const struct esw_vport_tbl_namespace mlx5_esw_vport_tbl_sample_ns = {
	.max_fte = MLX5_ESW_VPORT_TBL_SIZE_SAMPLE,
	.max_num_groups = 0,    /* default num of groups */
	.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT | MLX5_FLOW_TABLE_TUNNEL_EN_DECAP,
};

struct mlx5e_tc_psample {
	struct mlx5_eswitch *esw;
	struct mlx5_flow_table *termtbl;
	struct mlx5_flow_handle *termtbl_rule;
	DECLARE_HASHTABLE(hashtbl, 8);
	struct mutex ht_lock; /* protect hashtbl */
	DECLARE_HASHTABLE(restore_hashtbl, 8);
	struct mutex restore_lock; /* protect restore_hashtbl */
	struct mlx5e_post_act *post_act;
};

struct mlx5e_sampler {
	struct hlist_node hlist;
	u32 sampler_id;
	u32 sample_ratio;
	u32 sample_table_id;
	u32 default_table_id;
	int count;
};

struct mlx5e_sample_flow {
	struct mlx5e_sampler *sampler;
	struct mlx5e_sample_restore *restore;
	struct mlx5_flow_attr *pre_attr;
	struct mlx5_flow_handle *pre_rule;
	struct mlx5_flow_attr *post_attr;
	struct mlx5_flow_handle *post_rule;
};

struct mlx5e_sample_restore {
	struct hlist_node hlist;
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_flow_handle *rule;
	u32 obj_id;
	int count;
};

static int
sampler_termtbl_create(struct mlx5e_tc_psample *tc_psample)
{
	struct mlx5_eswitch *esw = tc_psample->esw;
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
	tc_psample->termtbl = mlx5_create_auto_grouped_flow_table(root_ns, &ft_attr);
	if (IS_ERR(tc_psample->termtbl)) {
		err = PTR_ERR(tc_psample->termtbl);
		mlx5_core_warn(dev, "failed to create termtbl, err: %d\n", err);
		return err;
	}

	act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.vport.num = esw->manager_vport;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	tc_psample->termtbl_rule = mlx5_add_flow_rules(tc_psample->termtbl, NULL, &act, &dest, 1);
	if (IS_ERR(tc_psample->termtbl_rule)) {
		err = PTR_ERR(tc_psample->termtbl_rule);
		mlx5_core_warn(dev, "failed to create termtbl rule, err: %d\n", err);
		mlx5_destroy_flow_table(tc_psample->termtbl);
		return err;
	}

	return 0;
}

static void
sampler_termtbl_destroy(struct mlx5e_tc_psample *tc_psample)
{
	mlx5_del_flow_rules(tc_psample->termtbl_rule);
	mlx5_destroy_flow_table(tc_psample->termtbl);
}

static int
sampler_obj_create(struct mlx5_core_dev *mdev, struct mlx5e_sampler *sampler)
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

static struct mlx5e_sampler *
sampler_get(struct mlx5e_tc_psample *tc_psample, u32 sample_ratio, u32 default_table_id)
{
	struct mlx5e_sampler *sampler;
	u32 hash_key;
	int err;

	mutex_lock(&tc_psample->ht_lock);
	hash_key = sampler_hash(sample_ratio, default_table_id);
	hash_for_each_possible(tc_psample->hashtbl, sampler, hlist, hash_key)
		if (!sampler_cmp(sampler->sample_ratio, sampler->default_table_id,
				 sample_ratio, default_table_id))
			goto add_ref;

	sampler = kzalloc(sizeof(*sampler), GFP_KERNEL);
	if (!sampler) {
		err = -ENOMEM;
		goto err_alloc;
	}

	sampler->sample_table_id = tc_psample->termtbl->id;
	sampler->default_table_id = default_table_id;
	sampler->sample_ratio = sample_ratio;

	err = sampler_obj_create(tc_psample->esw->dev, sampler);
	if (err)
		goto err_create;

	hash_add(tc_psample->hashtbl, &sampler->hlist, hash_key);

add_ref:
	sampler->count++;
	mutex_unlock(&tc_psample->ht_lock);
	return sampler;

err_create:
	kfree(sampler);
err_alloc:
	mutex_unlock(&tc_psample->ht_lock);
	return ERR_PTR(err);
}

static void
sampler_put(struct mlx5e_tc_psample *tc_psample, struct mlx5e_sampler *sampler)
{
	mutex_lock(&tc_psample->ht_lock);
	if (--sampler->count == 0) {
		hash_del(&sampler->hlist);
		sampler_obj_destroy(tc_psample->esw->dev, sampler->sampler_id);
		kfree(sampler);
	}
	mutex_unlock(&tc_psample->ht_lock);
}

/* obj_id is used to restore the sample parameters.
 * Set fte_id in original flow table, then match it in the default table.
 * Only set it for NICs can preserve reg_c or decap action. For other cases,
 * use the same match in the default table.
 * Use one header rewrite for both obj_id and fte_id.
 */
static struct mlx5_modify_hdr *
sample_modify_hdr_get(struct mlx5_core_dev *mdev, u32 obj_id,
		      struct mlx5e_tc_mod_hdr_acts *mod_acts)
{
	struct mlx5_modify_hdr *modify_hdr;
	int err;

	err = mlx5e_tc_match_to_reg_set(mdev, mod_acts, MLX5_FLOW_NAMESPACE_FDB,
					CHAIN_TO_REG, obj_id);
	if (err)
		goto err_set_regc0;

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_FDB,
					      mod_acts->num_actions,
					      mod_acts->actions);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		goto err_modify_hdr;
	}

	mlx5e_mod_hdr_dealloc(mod_acts);
	return modify_hdr;

err_modify_hdr:
	mlx5e_mod_hdr_dealloc(mod_acts);
err_set_regc0:
	return ERR_PTR(err);
}

static struct mlx5e_sample_restore *
sample_restore_get(struct mlx5e_tc_psample *tc_psample, u32 obj_id,
		   struct mlx5e_tc_mod_hdr_acts *mod_acts)
{
	struct mlx5_eswitch *esw = tc_psample->esw;
	struct mlx5_core_dev *mdev = esw->dev;
	struct mlx5e_sample_restore *restore;
	struct mlx5_modify_hdr *modify_hdr;
	int err;

	mutex_lock(&tc_psample->restore_lock);
	hash_for_each_possible(tc_psample->restore_hashtbl, restore, hlist, obj_id)
		if (restore->obj_id == obj_id)
			goto add_ref;

	restore = kzalloc(sizeof(*restore), GFP_KERNEL);
	if (!restore) {
		err = -ENOMEM;
		goto err_alloc;
	}
	restore->obj_id = obj_id;

	modify_hdr = sample_modify_hdr_get(mdev, obj_id, mod_acts);
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

	hash_add(tc_psample->restore_hashtbl, &restore->hlist, obj_id);
add_ref:
	restore->count++;
	mutex_unlock(&tc_psample->restore_lock);
	return restore;

err_restore:
	mlx5_modify_header_dealloc(mdev, restore->modify_hdr);
err_modify_hdr:
	kfree(restore);
err_alloc:
	mutex_unlock(&tc_psample->restore_lock);
	return ERR_PTR(err);
}

static void
sample_restore_put(struct mlx5e_tc_psample *tc_psample, struct mlx5e_sample_restore *restore)
{
	mutex_lock(&tc_psample->restore_lock);
	if (--restore->count == 0)
		hash_del(&restore->hlist);
	mutex_unlock(&tc_psample->restore_lock);

	if (!restore->count) {
		mlx5_del_flow_rules(restore->rule);
		mlx5_modify_header_dealloc(tc_psample->esw->dev, restore->modify_hdr);
		kfree(restore);
	}
}

void mlx5e_tc_sample_skb(struct sk_buff *skb, struct mlx5_mapped_obj *mapped_obj)
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

static int
add_post_rule(struct mlx5_eswitch *esw, struct mlx5e_sample_flow *sample_flow,
	      struct mlx5_flow_spec *spec, struct mlx5_flow_attr *attr,
	      u32 *default_tbl_id)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	u32 attr_sz = ns_to_attr_sz(MLX5_FLOW_NAMESPACE_FDB);
	struct mlx5_vport_tbl_attr per_vport_tbl_attr;
	struct mlx5_flow_table *default_tbl;
	struct mlx5_flow_attr *post_attr;
	int err;

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
	*default_tbl_id = default_tbl->id;

	post_attr = mlx5_alloc_flow_attr(MLX5_FLOW_NAMESPACE_FDB);
	if (!post_attr) {
		err = -ENOMEM;
		goto err_attr;
	}
	sample_flow->post_attr = post_attr;
	memcpy(post_attr, attr, attr_sz);
	/* Perform the original matches on the default table.
	 * Offload all actions except the sample action.
	 */
	post_attr->chain = 0;
	post_attr->prio = 0;
	post_attr->ft = default_tbl;
	post_attr->flags = MLX5_ATTR_FLAG_NO_IN_PORT;

	/* When offloading sample and encap action, if there is no valid
	 * neigh data struct, a slow path rule is offloaded first. Source
	 * port metadata match is set at that time. A per vport table is
	 * already allocated. No need to match it again. So clear the source
	 * port metadata match.
	 */
	mlx5_eswitch_clear_rule_source_port(esw, spec);
	sample_flow->post_rule = mlx5_eswitch_add_offloaded_rule(esw, spec, post_attr);
	if (IS_ERR(sample_flow->post_rule)) {
		err = PTR_ERR(sample_flow->post_rule);
		goto err_rule;
	}
	return 0;

err_rule:
	kfree(post_attr);
err_attr:
	mlx5_esw_vporttbl_put(esw, &per_vport_tbl_attr);
err_default_tbl:
	return err;
}

static void
del_post_rule(struct mlx5_eswitch *esw, struct mlx5e_sample_flow *sample_flow,
	      struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_vport_tbl_attr tbl_attr;

	mlx5_eswitch_del_offloaded_rule(esw, sample_flow->post_rule, sample_flow->post_attr);
	kfree(sample_flow->post_attr);
	tbl_attr.chain = attr->chain;
	tbl_attr.prio = attr->prio;
	tbl_attr.vport = esw_attr->in_rep->vport;
	tbl_attr.vport_ns = &mlx5_esw_vport_tbl_sample_ns;
	mlx5_esw_vporttbl_put(esw, &tbl_attr);
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
 *               | set fte_id (if reg_c preserve cap)
 *               | do decap (if required)
 *               v
 * +------------------------------------------------+
 * +                Flow Sampler Object             +
 * +------------------------------------------------+
 * +                    sample ratio                +
 * +------------------------------------------------+
 * +    sample table id    |    default table id    +
 * +------------------------------------------------+
 *            |                            |
 *            v                            v
 * +-----------------------------+  +-------------------+
 * +        sample table         +  +   default table   +
 * +-----------------------------+  +-------------------+
 * + forward to management vport +             |
 * +-----------------------------+             |
 *                                     +-------+------+
 *                                     |              |reg_c preserve cap
 *                                     |              |or decap action
 *                                     v              v
 *                        +-----------------+   +-------------+
 *                        + per vport table +   + post action +
 *                        +-----------------+   +-------------+
 *                        + original match  +
 *                        +-----------------+
 *                        + other actions   +
 *                        +-----------------+
 */
struct mlx5_flow_handle *
mlx5e_tc_sample_offload(struct mlx5e_tc_psample *tc_psample,
			struct mlx5_flow_spec *spec,
			struct mlx5_flow_attr *attr)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct mlx5_esw_flow_attr *pre_esw_attr;
	struct mlx5_mapped_obj restore_obj = {};
	struct mlx5e_tc_mod_hdr_acts *mod_acts;
	struct mlx5e_sample_flow *sample_flow;
	struct mlx5e_sample_attr *sample_attr;
	struct mlx5_flow_attr *pre_attr;
	u32 tunnel_id = attr->tunnel_id;
	struct mlx5_eswitch *esw;
	u32 default_tbl_id;
	u32 obj_id;
	int err;

	if (IS_ERR_OR_NULL(tc_psample))
		return ERR_PTR(-EOPNOTSUPP);

	sample_flow = kzalloc(sizeof(*sample_flow), GFP_KERNEL);
	if (!sample_flow)
		return ERR_PTR(-ENOMEM);
	sample_attr = &attr->sample_attr;
	sample_attr->sample_flow = sample_flow;

	/* For NICs with reg_c_preserve support or decap action, use
	 * post action instead of the per vport, chain and prio table.
	 * Only match the fte id instead of the same match in the
	 * original flow table.
	 */
	esw = tc_psample->esw;
	if (mlx5e_tc_act_sample_is_multi_table(esw->dev, attr)) {
		struct mlx5_flow_table *ft;

		ft = mlx5e_tc_post_act_get_ft(tc_psample->post_act);
		default_tbl_id = ft->id;
	} else {
		err = add_post_rule(esw, sample_flow, spec, attr, &default_tbl_id);
		if (err)
			goto err_post_rule;
	}

	/* Create sampler object. */
	sample_flow->sampler = sampler_get(tc_psample, sample_attr->rate, default_tbl_id);
	if (IS_ERR(sample_flow->sampler)) {
		err = PTR_ERR(sample_flow->sampler);
		goto err_sampler;
	}
	sample_attr->sampler_id = sample_flow->sampler->sampler_id;

	/* Create an id mapping reg_c0 value to sample object. */
	restore_obj.type = MLX5_MAPPED_OBJ_SAMPLE;
	restore_obj.sample.group_id = sample_attr->group_num;
	restore_obj.sample.rate = sample_attr->rate;
	restore_obj.sample.trunc_size = sample_attr->trunc_size;
	restore_obj.sample.tunnel_id = tunnel_id;
	err = mapping_add(esw->offloads.reg_c0_obj_pool, &restore_obj, &obj_id);
	if (err)
		goto err_obj_id;
	sample_attr->restore_obj_id = obj_id;

	/* Create sample restore context. */
	mod_acts = &attr->parse_attr->mod_hdr_acts;
	sample_flow->restore = sample_restore_get(tc_psample, obj_id, mod_acts);
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
		goto err_alloc_pre_flow_attr;
	}
	pre_attr->action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	/* For decap action, do decap in the original flow table instead of the
	 * default flow table.
	 */
	if (tunnel_id)
		pre_attr->action |= MLX5_FLOW_CONTEXT_ACTION_DECAP;
	pre_attr->modify_hdr = sample_flow->restore->modify_hdr;
	pre_attr->flags = MLX5_ATTR_FLAG_SAMPLE;
	pre_attr->inner_match_level = attr->inner_match_level;
	pre_attr->outer_match_level = attr->outer_match_level;
	pre_attr->chain = attr->chain;
	pre_attr->prio = attr->prio;
	pre_attr->ft = attr->ft;
	pre_attr->sample_attr = *sample_attr;
	pre_esw_attr = pre_attr->esw_attr;
	pre_esw_attr->in_mdev = esw_attr->in_mdev;
	pre_esw_attr->in_rep = esw_attr->in_rep;
	sample_flow->pre_rule = mlx5_eswitch_add_offloaded_rule(esw, spec, pre_attr);
	if (IS_ERR(sample_flow->pre_rule)) {
		err = PTR_ERR(sample_flow->pre_rule);
		goto err_pre_offload_rule;
	}
	sample_flow->pre_attr = pre_attr;

	return sample_flow->pre_rule;

err_pre_offload_rule:
	kfree(pre_attr);
err_alloc_pre_flow_attr:
	sample_restore_put(tc_psample, sample_flow->restore);
err_sample_restore:
	mapping_remove(esw->offloads.reg_c0_obj_pool, obj_id);
err_obj_id:
	sampler_put(tc_psample, sample_flow->sampler);
err_sampler:
	if (sample_flow->post_rule)
		del_post_rule(esw, sample_flow, attr);
err_post_rule:
	kfree(sample_flow);
	return ERR_PTR(err);
}

void
mlx5e_tc_sample_unoffload(struct mlx5e_tc_psample *tc_psample,
			  struct mlx5_flow_handle *rule,
			  struct mlx5_flow_attr *attr)
{
	struct mlx5e_sample_flow *sample_flow;
	struct mlx5_eswitch *esw;

	if (IS_ERR_OR_NULL(tc_psample))
		return;

	/* The following delete order can't be changed, otherwise,
	 * will hit fw syndromes.
	 */
	esw = tc_psample->esw;
	sample_flow = attr->sample_attr.sample_flow;
	mlx5_eswitch_del_offloaded_rule(esw, sample_flow->pre_rule, sample_flow->pre_attr);

	sample_restore_put(tc_psample, sample_flow->restore);
	mapping_remove(esw->offloads.reg_c0_obj_pool, attr->sample_attr.restore_obj_id);
	sampler_put(tc_psample, sample_flow->sampler);
	if (sample_flow->post_rule)
		del_post_rule(esw, sample_flow, attr);

	kfree(sample_flow->pre_attr);
	kfree(sample_flow);
}

struct mlx5e_tc_psample *
mlx5e_tc_sample_init(struct mlx5_eswitch *esw, struct mlx5e_post_act *post_act)
{
	struct mlx5e_tc_psample *tc_psample;
	int err;

	tc_psample = kzalloc(sizeof(*tc_psample), GFP_KERNEL);
	if (!tc_psample)
		return ERR_PTR(-ENOMEM);
	if (IS_ERR_OR_NULL(post_act)) {
		err = PTR_ERR(post_act);
		goto err_post_act;
	}
	tc_psample->post_act = post_act;
	tc_psample->esw = esw;
	err = sampler_termtbl_create(tc_psample);
	if (err)
		goto err_post_act;

	mutex_init(&tc_psample->ht_lock);
	mutex_init(&tc_psample->restore_lock);

	return tc_psample;

err_post_act:
	kfree(tc_psample);
	return ERR_PTR(err);
}

void
mlx5e_tc_sample_cleanup(struct mlx5e_tc_psample *tc_psample)
{
	if (IS_ERR_OR_NULL(tc_psample))
		return;

	mutex_destroy(&tc_psample->restore_lock);
	mutex_destroy(&tc_psample->ht_lock);
	sampler_termtbl_destroy(tc_psample);
	kfree(tc_psample);
}
