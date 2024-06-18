// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies.

#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/fs.h>

#include "lib/fs_chains.h"
#include "fs_ft_pool.h"
#include "en/mapping.h"
#include "fs_core.h"
#include "en_tc.h"

#define chains_lock(chains) ((chains)->lock)
#define chains_ht(chains) ((chains)->chains_ht)
#define prios_ht(chains) ((chains)->prios_ht)
#define chains_default_ft(chains) ((chains)->chains_default_ft)
#define chains_end_ft(chains) ((chains)->chains_end_ft)
#define FT_TBL_SZ (64 * 1024)

struct mlx5_fs_chains {
	struct mlx5_core_dev *dev;

	struct rhashtable chains_ht;
	struct rhashtable prios_ht;
	/* Protects above chains_ht and prios_ht */
	struct mutex lock;

	struct mlx5_flow_table *chains_default_ft;
	struct mlx5_flow_table *chains_end_ft;
	struct mapping_ctx *chains_mapping;

	enum mlx5_flow_namespace_type ns;
	u32 group_num;
	u32 flags;
	int fs_base_prio;
	int fs_base_level;
};

struct fs_chain {
	struct rhash_head node;

	u32 chain;

	int ref;
	int id;

	struct mlx5_fs_chains *chains;
	struct list_head prios_list;
	struct mlx5_flow_handle *restore_rule;
	struct mlx5_modify_hdr *miss_modify_hdr;
};

struct prio_key {
	u32 chain;
	u32 prio;
	u32 level;
};

struct prio {
	struct rhash_head node;
	struct list_head list;

	struct prio_key key;

	int ref;

	struct fs_chain *chain;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_table *next_ft;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
};

static const struct rhashtable_params chain_params = {
	.head_offset = offsetof(struct fs_chain, node),
	.key_offset = offsetof(struct fs_chain, chain),
	.key_len = sizeof_field(struct fs_chain, chain),
	.automatic_shrinking = true,
};

static const struct rhashtable_params prio_params = {
	.head_offset = offsetof(struct prio, node),
	.key_offset = offsetof(struct prio, key),
	.key_len = sizeof_field(struct prio, key),
	.automatic_shrinking = true,
};

bool mlx5_chains_prios_supported(struct mlx5_fs_chains *chains)
{
	return chains->flags & MLX5_CHAINS_AND_PRIOS_SUPPORTED;
}

bool mlx5_chains_ignore_flow_level_supported(struct mlx5_fs_chains *chains)
{
	return chains->flags & MLX5_CHAINS_IGNORE_FLOW_LEVEL_SUPPORTED;
}

bool mlx5_chains_backwards_supported(struct mlx5_fs_chains *chains)
{
	return mlx5_chains_prios_supported(chains) &&
	       mlx5_chains_ignore_flow_level_supported(chains);
}

u32 mlx5_chains_get_chain_range(struct mlx5_fs_chains *chains)
{
	if (!mlx5_chains_prios_supported(chains))
		return 1;

	if (mlx5_chains_ignore_flow_level_supported(chains))
		return UINT_MAX - 1;

	/* We should get here only for eswitch case */
	return FDB_TC_MAX_CHAIN;
}

u32 mlx5_chains_get_nf_ft_chain(struct mlx5_fs_chains *chains)
{
	return mlx5_chains_get_chain_range(chains) + 1;
}

u32 mlx5_chains_get_prio_range(struct mlx5_fs_chains *chains)
{
	if (mlx5_chains_ignore_flow_level_supported(chains))
		return UINT_MAX;

	if (!chains->dev->priv.eswitch ||
	    chains->dev->priv.eswitch->mode != MLX5_ESWITCH_OFFLOADS)
		return 1;

	/* We should get here only for eswitch case */
	return FDB_TC_MAX_PRIO;
}

static unsigned int mlx5_chains_get_level_range(struct mlx5_fs_chains *chains)
{
	if (mlx5_chains_ignore_flow_level_supported(chains))
		return UINT_MAX;

	/* Same value for FDB and NIC RX tables */
	return FDB_TC_LEVELS_PER_PRIO;
}

void
mlx5_chains_set_end_ft(struct mlx5_fs_chains *chains,
		       struct mlx5_flow_table *ft)
{
	chains_end_ft(chains) = ft;
}

static struct mlx5_flow_table *
mlx5_chains_create_table(struct mlx5_fs_chains *chains,
			 u32 chain, u32 prio, u32 level)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	int sz;

	if (chains->flags & MLX5_CHAINS_FT_TUNNEL_SUPPORTED)
		ft_attr.flags |= (MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT |
				  MLX5_FLOW_TABLE_TUNNEL_EN_DECAP);

	sz = (chain == mlx5_chains_get_nf_ft_chain(chains)) ? FT_TBL_SZ : POOL_NEXT_SIZE;
	ft_attr.max_fte = sz;

	/* We use chains_default_ft(chains) as the table's next_ft till
	 * ignore_flow_level is allowed on FT creation and not just for FTEs.
	 * Instead caller should add an explicit miss rule if needed.
	 */
	ft_attr.next_ft = chains_default_ft(chains);

	/* The root table(chain 0, prio 1, level 0) is required to be
	 * connected to the previous fs_core managed prio.
	 * We always create it, as a managed table, in order to align with
	 * fs_core logic.
	 */
	if (!mlx5_chains_ignore_flow_level_supported(chains) ||
	    (chain == 0 && prio == 1 && level == 0)) {
		ft_attr.level = chains->fs_base_level;
		ft_attr.prio = chains->fs_base_prio + prio - 1;
		ns = (chains->ns == MLX5_FLOW_NAMESPACE_FDB) ?
			mlx5_get_fdb_sub_ns(chains->dev, chain) :
			mlx5_get_flow_namespace(chains->dev, chains->ns);
	} else {
		ft_attr.flags |= MLX5_FLOW_TABLE_UNMANAGED;
		ft_attr.prio = chains->fs_base_prio;
		/* Firmware doesn't allow us to create another level 0 table,
		 * so we create all unmanaged tables as level 1 (base + 1).
		 *
		 * To connect them, we use explicit miss rules with
		 * ignore_flow_level. Caller is responsible to create
		 * these rules (if needed).
		 */
		ft_attr.level = chains->fs_base_level + 1;
		ns = mlx5_get_flow_namespace(chains->dev, chains->ns);
	}

	ft_attr.autogroup.num_reserved_entries = 2;
	ft_attr.autogroup.max_num_groups = chains->group_num;
	ft = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		mlx5_core_warn(chains->dev, "Failed to create chains table err %d (chain: %d, prio: %d, level: %d, size: %d)\n",
			       (int)PTR_ERR(ft), chain, prio, level, sz);
		return ft;
	}

	return ft;
}

static int
create_chain_restore(struct fs_chain *chain)
{
	struct mlx5_eswitch *esw = chain->chains->dev->priv.eswitch;
	u8 modact[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_fs_chains *chains = chain->chains;
	enum mlx5e_tc_attr_to_reg mapped_obj_to_reg;
	struct mlx5_modify_hdr *mod_hdr;
	u32 index;
	int err;

	if (chain->chain == mlx5_chains_get_nf_ft_chain(chains) ||
	    !mlx5_chains_prios_supported(chains) ||
	    !chains->chains_mapping)
		return 0;

	err = mlx5_chains_get_chain_mapping(chains, chain->chain, &index);
	if (err)
		return err;
	if (index == MLX5_FS_DEFAULT_FLOW_TAG) {
		/* we got the special default flow tag id, so we won't know
		 * if we actually marked the packet with the restore rule
		 * we create.
		 *
		 * This case isn't possible with MLX5_FS_DEFAULT_FLOW_TAG = 0.
		 */
		err = mlx5_chains_get_chain_mapping(chains, chain->chain, &index);
		mapping_remove(chains->chains_mapping, MLX5_FS_DEFAULT_FLOW_TAG);
		if (err)
			return err;
	}

	chain->id = index;

	if (chains->ns == MLX5_FLOW_NAMESPACE_FDB) {
		mapped_obj_to_reg = MAPPED_OBJ_TO_REG;
		chain->restore_rule = esw_add_restore_rule(esw, chain->id);
		if (IS_ERR(chain->restore_rule)) {
			err = PTR_ERR(chain->restore_rule);
			goto err_rule;
		}
	} else if (chains->ns == MLX5_FLOW_NAMESPACE_KERNEL) {
		/* For NIC RX we don't need a restore rule
		 * since we write the metadata to reg_b
		 * that is passed to SW directly.
		 */
		mapped_obj_to_reg = NIC_MAPPED_OBJ_TO_REG;
	} else {
		err = -EINVAL;
		goto err_rule;
	}

	MLX5_SET(set_action_in, modact, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, modact, field,
		 mlx5e_tc_attr_to_reg_mappings[mapped_obj_to_reg].mfield);
	MLX5_SET(set_action_in, modact, offset,
		 mlx5e_tc_attr_to_reg_mappings[mapped_obj_to_reg].moffset);
	MLX5_SET(set_action_in, modact, length,
		 mlx5e_tc_attr_to_reg_mappings[mapped_obj_to_reg].mlen == 32 ?
		 0 : mlx5e_tc_attr_to_reg_mappings[mapped_obj_to_reg].mlen);
	MLX5_SET(set_action_in, modact, data, chain->id);
	mod_hdr = mlx5_modify_header_alloc(chains->dev, chains->ns,
					   1, modact);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		goto err_mod_hdr;
	}
	chain->miss_modify_hdr = mod_hdr;

	return 0;

err_mod_hdr:
	if (!IS_ERR_OR_NULL(chain->restore_rule))
		mlx5_del_flow_rules(chain->restore_rule);
err_rule:
	/* Datapath can't find this mapping, so we can safely remove it */
	mapping_remove(chains->chains_mapping, chain->id);
	return err;
}

static void destroy_chain_restore(struct fs_chain *chain)
{
	struct mlx5_fs_chains *chains = chain->chains;

	if (!chain->miss_modify_hdr)
		return;

	if (chain->restore_rule)
		mlx5_del_flow_rules(chain->restore_rule);

	mlx5_modify_header_dealloc(chains->dev, chain->miss_modify_hdr);
	mapping_remove(chains->chains_mapping, chain->id);
}

static struct fs_chain *
mlx5_chains_create_chain(struct mlx5_fs_chains *chains, u32 chain)
{
	struct fs_chain *chain_s = NULL;
	int err;

	chain_s = kvzalloc(sizeof(*chain_s), GFP_KERNEL);
	if (!chain_s)
		return ERR_PTR(-ENOMEM);

	chain_s->chains = chains;
	chain_s->chain = chain;
	INIT_LIST_HEAD(&chain_s->prios_list);

	err = create_chain_restore(chain_s);
	if (err)
		goto err_restore;

	err = rhashtable_insert_fast(&chains_ht(chains), &chain_s->node,
				     chain_params);
	if (err)
		goto err_insert;

	return chain_s;

err_insert:
	destroy_chain_restore(chain_s);
err_restore:
	kvfree(chain_s);
	return ERR_PTR(err);
}

static void
mlx5_chains_destroy_chain(struct fs_chain *chain)
{
	struct mlx5_fs_chains *chains = chain->chains;

	rhashtable_remove_fast(&chains_ht(chains), &chain->node,
			       chain_params);

	destroy_chain_restore(chain);
	kvfree(chain);
}

static struct fs_chain *
mlx5_chains_get_chain(struct mlx5_fs_chains *chains, u32 chain)
{
	struct fs_chain *chain_s;

	chain_s = rhashtable_lookup_fast(&chains_ht(chains), &chain,
					 chain_params);
	if (!chain_s) {
		chain_s = mlx5_chains_create_chain(chains, chain);
		if (IS_ERR(chain_s))
			return chain_s;
	}

	chain_s->ref++;

	return chain_s;
}

static struct mlx5_flow_handle *
mlx5_chains_add_miss_rule(struct fs_chain *chain,
			  struct mlx5_flow_table *ft,
			  struct mlx5_flow_table *next_ft)
{
	struct mlx5_fs_chains *chains = chain->chains;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act act = {};

	act.flags  = FLOW_ACT_NO_APPEND;
	if (mlx5_chains_ignore_flow_level_supported(chain->chains))
		act.flags |= FLOW_ACT_IGNORE_FLOW_LEVEL;

	act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.type  = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = next_ft;

	if (chains->chains_mapping && next_ft == chains_end_ft(chains) &&
	    chain->chain != mlx5_chains_get_nf_ft_chain(chains) &&
	    mlx5_chains_prios_supported(chains)) {
		act.modify_hdr = chain->miss_modify_hdr;
		act.action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	}

	return mlx5_add_flow_rules(ft, NULL, &act, &dest, 1);
}

static int
mlx5_chains_update_prio_prevs(struct prio *prio,
			      struct mlx5_flow_table *next_ft)
{
	struct mlx5_flow_handle *miss_rules[FDB_TC_LEVELS_PER_PRIO + 1] = {};
	struct fs_chain *chain = prio->chain;
	struct prio *pos;
	int n = 0, err;

	if (prio->key.level)
		return 0;

	/* Iterate in reverse order until reaching the level 0 rule of
	 * the previous priority, adding all the miss rules first, so we can
	 * revert them if any of them fails.
	 */
	pos = prio;
	list_for_each_entry_continue_reverse(pos,
					     &chain->prios_list,
					     list) {
		miss_rules[n] = mlx5_chains_add_miss_rule(chain,
							  pos->ft,
							  next_ft);
		if (IS_ERR(miss_rules[n])) {
			err = PTR_ERR(miss_rules[n]);
			goto err_prev_rule;
		}

		n++;
		if (!pos->key.level)
			break;
	}

	/* Success, delete old miss rules, and update the pointers. */
	n = 0;
	pos = prio;
	list_for_each_entry_continue_reverse(pos,
					     &chain->prios_list,
					     list) {
		mlx5_del_flow_rules(pos->miss_rule);

		pos->miss_rule = miss_rules[n];
		pos->next_ft = next_ft;

		n++;
		if (!pos->key.level)
			break;
	}

	return 0;

err_prev_rule:
	while (--n >= 0)
		mlx5_del_flow_rules(miss_rules[n]);

	return err;
}

static void
mlx5_chains_put_chain(struct fs_chain *chain)
{
	if (--chain->ref == 0)
		mlx5_chains_destroy_chain(chain);
}

static struct prio *
mlx5_chains_create_prio(struct mlx5_fs_chains *chains,
			u32 chain, u32 prio, u32 level)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_table *next_ft;
	struct mlx5_flow_table *ft;
	struct fs_chain *chain_s;
	struct list_head *pos;
	struct prio *prio_s;
	u32 *flow_group_in;
	int err;

	chain_s = mlx5_chains_get_chain(chains, chain);
	if (IS_ERR(chain_s))
		return ERR_CAST(chain_s);

	prio_s = kvzalloc(sizeof(*prio_s), GFP_KERNEL);
	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!prio_s || !flow_group_in) {
		err = -ENOMEM;
		goto err_alloc;
	}

	/* Chain's prio list is sorted by prio and level.
	 * And all levels of some prio point to the next prio's level 0.
	 * Example list (prio, level):
	 * (3,0)->(3,1)->(5,0)->(5,1)->(6,1)->(7,0)
	 * In hardware, we will we have the following pointers:
	 * (3,0) -> (5,0) -> (7,0) -> Slow path
	 * (3,1) -> (5,0)
	 * (5,1) -> (7,0)
	 * (6,1) -> (7,0)
	 */

	/* Default miss for each chain: */
	next_ft = (chain == mlx5_chains_get_nf_ft_chain(chains)) ?
		  chains_default_ft(chains) :
		  chains_end_ft(chains);
	list_for_each(pos, &chain_s->prios_list) {
		struct prio *p = list_entry(pos, struct prio, list);

		/* exit on first pos that is larger */
		if (prio < p->key.prio || (prio == p->key.prio &&
					   level < p->key.level)) {
			/* Get next level 0 table */
			next_ft = p->key.level == 0 ? p->ft : p->next_ft;
			break;
		}
	}

	ft = mlx5_chains_create_table(chains, chain, prio, level);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_create;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index,
		 ft->max_fte - 2);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
		 ft->max_fte - 1);
	miss_group = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(miss_group)) {
		err = PTR_ERR(miss_group);
		goto err_group;
	}

	/* Add miss rule to next_ft */
	miss_rule = mlx5_chains_add_miss_rule(chain_s, ft, next_ft);
	if (IS_ERR(miss_rule)) {
		err = PTR_ERR(miss_rule);
		goto err_miss_rule;
	}

	prio_s->miss_group = miss_group;
	prio_s->miss_rule = miss_rule;
	prio_s->next_ft = next_ft;
	prio_s->chain = chain_s;
	prio_s->key.chain = chain;
	prio_s->key.prio = prio;
	prio_s->key.level = level;
	prio_s->ft = ft;

	err = rhashtable_insert_fast(&prios_ht(chains), &prio_s->node,
				     prio_params);
	if (err)
		goto err_insert;

	list_add(&prio_s->list, pos->prev);

	/* Table is ready, connect it */
	err = mlx5_chains_update_prio_prevs(prio_s, ft);
	if (err)
		goto err_update;

	kvfree(flow_group_in);
	return prio_s;

err_update:
	list_del(&prio_s->list);
	rhashtable_remove_fast(&prios_ht(chains), &prio_s->node,
			       prio_params);
err_insert:
	mlx5_del_flow_rules(miss_rule);
err_miss_rule:
	mlx5_destroy_flow_group(miss_group);
err_group:
	mlx5_destroy_flow_table(ft);
err_create:
err_alloc:
	kvfree(prio_s);
	kvfree(flow_group_in);
	mlx5_chains_put_chain(chain_s);
	return ERR_PTR(err);
}

static void
mlx5_chains_destroy_prio(struct mlx5_fs_chains *chains,
			 struct prio *prio)
{
	struct fs_chain *chain = prio->chain;

	WARN_ON(mlx5_chains_update_prio_prevs(prio,
					      prio->next_ft));

	list_del(&prio->list);
	rhashtable_remove_fast(&prios_ht(chains), &prio->node,
			       prio_params);
	mlx5_del_flow_rules(prio->miss_rule);
	mlx5_destroy_flow_group(prio->miss_group);
	mlx5_destroy_flow_table(prio->ft);
	mlx5_chains_put_chain(chain);
	kvfree(prio);
}

struct mlx5_flow_table *
mlx5_chains_get_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level)
{
	struct mlx5_flow_table *prev_fts;
	struct prio *prio_s;
	struct prio_key key;
	int l = 0;

	if ((chain > mlx5_chains_get_chain_range(chains) &&
	     chain != mlx5_chains_get_nf_ft_chain(chains)) ||
	    prio > mlx5_chains_get_prio_range(chains) ||
	    level > mlx5_chains_get_level_range(chains))
		return ERR_PTR(-EOPNOTSUPP);

	/* create earlier levels for correct fs_core lookup when
	 * connecting tables.
	 */
	for (l = 0; l < level; l++) {
		prev_fts = mlx5_chains_get_table(chains, chain, prio, l);
		if (IS_ERR(prev_fts)) {
			prio_s = ERR_CAST(prev_fts);
			goto err_get_prevs;
		}
	}

	key.chain = chain;
	key.prio = prio;
	key.level = level;

	mutex_lock(&chains_lock(chains));
	prio_s = rhashtable_lookup_fast(&prios_ht(chains), &key,
					prio_params);
	if (!prio_s) {
		prio_s = mlx5_chains_create_prio(chains, chain,
						 prio, level);
		if (IS_ERR(prio_s))
			goto err_create_prio;
	}

	++prio_s->ref;
	mutex_unlock(&chains_lock(chains));

	return prio_s->ft;

err_create_prio:
	mutex_unlock(&chains_lock(chains));
err_get_prevs:
	while (--l >= 0)
		mlx5_chains_put_table(chains, chain, prio, l);
	return ERR_CAST(prio_s);
}

void
mlx5_chains_put_table(struct mlx5_fs_chains *chains, u32 chain, u32 prio,
		      u32 level)
{
	struct prio *prio_s;
	struct prio_key key;

	key.chain = chain;
	key.prio = prio;
	key.level = level;

	mutex_lock(&chains_lock(chains));
	prio_s = rhashtable_lookup_fast(&prios_ht(chains), &key,
					prio_params);
	if (!prio_s)
		goto err_get_prio;

	if (--prio_s->ref == 0)
		mlx5_chains_destroy_prio(chains, prio_s);
	mutex_unlock(&chains_lock(chains));

	while (level-- > 0)
		mlx5_chains_put_table(chains, chain, prio, level);

	return;

err_get_prio:
	mutex_unlock(&chains_lock(chains));
	WARN_ONCE(1,
		  "Couldn't find table: (chain: %d prio: %d level: %d)",
		  chain, prio, level);
}

struct mlx5_flow_table *
mlx5_chains_get_tc_end_ft(struct mlx5_fs_chains *chains)
{
	return chains_end_ft(chains);
}

struct mlx5_flow_table *
mlx5_chains_create_global_table(struct mlx5_fs_chains *chains)
{
	u32 chain, prio, level;
	int err;

	if (!mlx5_chains_ignore_flow_level_supported(chains)) {
		err = -EOPNOTSUPP;

		mlx5_core_warn(chains->dev,
			       "Couldn't create global flow table, ignore_flow_level not supported.");
		goto err_ignore;
	}

	chain = mlx5_chains_get_chain_range(chains),
	prio = mlx5_chains_get_prio_range(chains);
	level = mlx5_chains_get_level_range(chains);

	return mlx5_chains_create_table(chains, chain, prio, level);

err_ignore:
	return ERR_PTR(err);
}

void
mlx5_chains_destroy_global_table(struct mlx5_fs_chains *chains,
				 struct mlx5_flow_table *ft)
{
	mlx5_destroy_flow_table(ft);
}

static struct mlx5_fs_chains *
mlx5_chains_init(struct mlx5_core_dev *dev, struct mlx5_chains_attr *attr)
{
	struct mlx5_fs_chains *chains;
	int err;

	chains = kzalloc(sizeof(*chains), GFP_KERNEL);
	if (!chains)
		return ERR_PTR(-ENOMEM);

	chains->dev = dev;
	chains->flags = attr->flags;
	chains->ns = attr->ns;
	chains->group_num = attr->max_grp_num;
	chains->chains_mapping = attr->mapping;
	chains->fs_base_prio = attr->fs_base_prio;
	chains->fs_base_level = attr->fs_base_level;
	chains_default_ft(chains) = chains_end_ft(chains) = attr->default_ft;

	err = rhashtable_init(&chains_ht(chains), &chain_params);
	if (err)
		goto init_chains_ht_err;

	err = rhashtable_init(&prios_ht(chains), &prio_params);
	if (err)
		goto init_prios_ht_err;

	mutex_init(&chains_lock(chains));

	return chains;

init_prios_ht_err:
	rhashtable_destroy(&chains_ht(chains));
init_chains_ht_err:
	kfree(chains);
	return ERR_PTR(err);
}

static void
mlx5_chains_cleanup(struct mlx5_fs_chains *chains)
{
	mutex_destroy(&chains_lock(chains));
	rhashtable_destroy(&prios_ht(chains));
	rhashtable_destroy(&chains_ht(chains));

	kfree(chains);
}

struct mlx5_fs_chains *
mlx5_chains_create(struct mlx5_core_dev *dev, struct mlx5_chains_attr *attr)
{
	struct mlx5_fs_chains *chains;

	chains = mlx5_chains_init(dev, attr);

	return chains;
}

void
mlx5_chains_destroy(struct mlx5_fs_chains *chains)
{
	mlx5_chains_cleanup(chains);
}

int
mlx5_chains_get_chain_mapping(struct mlx5_fs_chains *chains, u32 chain,
			      u32 *chain_mapping)
{
	struct mapping_ctx *ctx = chains->chains_mapping;
	struct mlx5_mapped_obj mapped_obj = {};

	mapped_obj.type = MLX5_MAPPED_OBJ_CHAIN;
	mapped_obj.chain = chain;
	return mapping_add(ctx, &mapped_obj, chain_mapping);
}

int
mlx5_chains_put_chain_mapping(struct mlx5_fs_chains *chains, u32 chain_mapping)
{
	struct mapping_ctx *ctx = chains->chains_mapping;

	return mapping_remove(ctx, chain_mapping);
}

void
mlx5_chains_print_info(struct mlx5_fs_chains *chains)
{
	mlx5_core_dbg(chains->dev, "Flow table chains groups(%d)\n", chains->group_num);
}
