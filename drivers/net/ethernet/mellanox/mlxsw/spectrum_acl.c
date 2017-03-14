/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>

#include "reg.h"
#include "core.h"
#include "resources.h"
#include "spectrum.h"
#include "core_acl_flex_keys.h"
#include "core_acl_flex_actions.h"
#include "spectrum_acl_flex_keys.h"

struct mlxsw_sp_acl {
	struct mlxsw_afk *afk;
	struct mlxsw_afa *afa;
	const struct mlxsw_sp_acl_ops *ops;
	struct rhashtable ruleset_ht;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

struct mlxsw_afk *mlxsw_sp_acl_afk(struct mlxsw_sp_acl *acl)
{
	return acl->afk;
}

struct mlxsw_sp_acl_ruleset_ht_key {
	struct net_device *dev; /* dev this ruleset is bound to */
	bool ingress;
	const struct mlxsw_sp_acl_profile_ops *ops;
};

struct mlxsw_sp_acl_ruleset {
	struct rhash_head ht_node; /* Member of acl HT */
	struct mlxsw_sp_acl_ruleset_ht_key ht_key;
	struct rhashtable rule_ht;
	unsigned int ref_count;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

struct mlxsw_sp_acl_rule {
	struct rhash_head ht_node; /* Member of rule HT */
	unsigned long cookie; /* HT key */
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule_info *rulei;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

static const struct rhashtable_params mlxsw_sp_acl_ruleset_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_acl_ruleset_ht_key),
	.key_offset = offsetof(struct mlxsw_sp_acl_ruleset, ht_key),
	.head_offset = offsetof(struct mlxsw_sp_acl_ruleset, ht_node),
	.automatic_shrinking = true,
};

static const struct rhashtable_params mlxsw_sp_acl_rule_ht_params = {
	.key_len = sizeof(unsigned long),
	.key_offset = offsetof(struct mlxsw_sp_acl_rule, cookie),
	.head_offset = offsetof(struct mlxsw_sp_acl_rule, ht_node),
	.automatic_shrinking = true,
};

static struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_create(struct mlxsw_sp *mlxsw_sp,
			    const struct mlxsw_sp_acl_profile_ops *ops)
{
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	struct mlxsw_sp_acl_ruleset *ruleset;
	size_t alloc_size;
	int err;

	alloc_size = sizeof(*ruleset) + ops->ruleset_priv_size;
	ruleset = kzalloc(alloc_size, GFP_KERNEL);
	if (!ruleset)
		return ERR_PTR(-ENOMEM);
	ruleset->ref_count = 1;
	ruleset->ht_key.ops = ops;

	err = rhashtable_init(&ruleset->rule_ht, &mlxsw_sp_acl_rule_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = ops->ruleset_add(mlxsw_sp, acl->priv, ruleset->priv);
	if (err)
		goto err_ops_ruleset_add;

	return ruleset;

err_ops_ruleset_add:
	rhashtable_destroy(&ruleset->rule_ht);
err_rhashtable_init:
	kfree(ruleset);
	return ERR_PTR(err);
}

static void mlxsw_sp_acl_ruleset_destroy(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_ruleset *ruleset)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	ops->ruleset_del(mlxsw_sp, ruleset->priv);
	rhashtable_destroy(&ruleset->rule_ht);
	kfree(ruleset);
}

static int mlxsw_sp_acl_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_ruleset *ruleset,
				     struct net_device *dev, bool ingress)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	int err;

	ruleset->ht_key.dev = dev;
	ruleset->ht_key.ingress = ingress;
	err = rhashtable_insert_fast(&acl->ruleset_ht, &ruleset->ht_node,
				     mlxsw_sp_acl_ruleset_ht_params);
	if (err)
		return err;
	err = ops->ruleset_bind(mlxsw_sp, ruleset->priv, dev, ingress);
	if (err)
		goto err_ops_ruleset_bind;
	return 0;

err_ops_ruleset_bind:
	rhashtable_remove_fast(&acl->ruleset_ht, &ruleset->ht_node,
			       mlxsw_sp_acl_ruleset_ht_params);
	return err;
}

static void mlxsw_sp_acl_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_ruleset *ruleset)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;

	ops->ruleset_unbind(mlxsw_sp, ruleset->priv);
	rhashtable_remove_fast(&acl->ruleset_ht, &ruleset->ht_node,
			       mlxsw_sp_acl_ruleset_ht_params);
}

static void mlxsw_sp_acl_ruleset_ref_inc(struct mlxsw_sp_acl_ruleset *ruleset)
{
	ruleset->ref_count++;
}

static void mlxsw_sp_acl_ruleset_ref_dec(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_ruleset *ruleset)
{
	if (--ruleset->ref_count)
		return;
	mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, ruleset);
	mlxsw_sp_acl_ruleset_destroy(mlxsw_sp, ruleset);
}

struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_get(struct mlxsw_sp *mlxsw_sp,
			 struct net_device *dev, bool ingress,
			 enum mlxsw_sp_acl_profile profile)
{
	const struct mlxsw_sp_acl_profile_ops *ops;
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	struct mlxsw_sp_acl_ruleset_ht_key ht_key;
	struct mlxsw_sp_acl_ruleset *ruleset;
	int err;

	ops = acl->ops->profile_ops(mlxsw_sp, profile);
	if (!ops)
		return ERR_PTR(-EINVAL);

	memset(&ht_key, 0, sizeof(ht_key));
	ht_key.dev = dev;
	ht_key.ingress = ingress;
	ht_key.ops = ops;
	ruleset = rhashtable_lookup_fast(&acl->ruleset_ht, &ht_key,
					 mlxsw_sp_acl_ruleset_ht_params);
	if (ruleset) {
		mlxsw_sp_acl_ruleset_ref_inc(ruleset);
		return ruleset;
	}
	ruleset = mlxsw_sp_acl_ruleset_create(mlxsw_sp, ops);
	if (IS_ERR(ruleset))
		return ruleset;
	err = mlxsw_sp_acl_ruleset_bind(mlxsw_sp, ruleset, dev, ingress);
	if (err)
		goto err_ruleset_bind;
	return ruleset;

err_ruleset_bind:
	mlxsw_sp_acl_ruleset_destroy(mlxsw_sp, ruleset);
	return ERR_PTR(err);
}

void mlxsw_sp_acl_ruleset_put(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_ruleset *ruleset)
{
	mlxsw_sp_acl_ruleset_ref_dec(mlxsw_sp, ruleset);
}

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rulei_create(struct mlxsw_sp_acl *acl)
{
	struct mlxsw_sp_acl_rule_info *rulei;
	int err;

	rulei = kzalloc(sizeof(*rulei), GFP_KERNEL);
	if (!rulei)
		return NULL;
	rulei->act_block = mlxsw_afa_block_create(acl->afa);
	if (IS_ERR(rulei->act_block)) {
		err = PTR_ERR(rulei->act_block);
		goto err_afa_block_create;
	}
	return rulei;

err_afa_block_create:
	kfree(rulei);
	return ERR_PTR(err);
}

void mlxsw_sp_acl_rulei_destroy(struct mlxsw_sp_acl_rule_info *rulei)
{
	mlxsw_afa_block_destroy(rulei->act_block);
	kfree(rulei);
}

int mlxsw_sp_acl_rulei_commit(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_commit(rulei->act_block);
}

void mlxsw_sp_acl_rulei_priority(struct mlxsw_sp_acl_rule_info *rulei,
				 unsigned int priority)
{
	rulei->priority = priority;
}

void mlxsw_sp_acl_rulei_keymask_u32(struct mlxsw_sp_acl_rule_info *rulei,
				    enum mlxsw_afk_element element,
				    u32 key_value, u32 mask_value)
{
	mlxsw_afk_values_add_u32(&rulei->values, element,
				 key_value, mask_value);
}

void mlxsw_sp_acl_rulei_keymask_buf(struct mlxsw_sp_acl_rule_info *rulei,
				    enum mlxsw_afk_element element,
				    const char *key_value,
				    const char *mask_value, unsigned int len)
{
	mlxsw_afk_values_add_buf(&rulei->values, element,
				 key_value, mask_value, len);
}

void mlxsw_sp_acl_rulei_act_continue(struct mlxsw_sp_acl_rule_info *rulei)
{
	mlxsw_afa_block_continue(rulei->act_block);
}

void mlxsw_sp_acl_rulei_act_jump(struct mlxsw_sp_acl_rule_info *rulei,
				 u16 group_id)
{
	mlxsw_afa_block_jump(rulei->act_block, group_id);
}

int mlxsw_sp_acl_rulei_act_drop(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_append_drop(rulei->act_block);
}

int mlxsw_sp_acl_rulei_act_fwd(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule_info *rulei,
			       struct net_device *out_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 local_port;
	bool in_port;

	if (out_dev) {
		if (!mlxsw_sp_port_dev_check(out_dev))
			return -EINVAL;
		mlxsw_sp_port = netdev_priv(out_dev);
		if (mlxsw_sp_port->mlxsw_sp != mlxsw_sp)
			return -EINVAL;
		local_port = mlxsw_sp_port->local_port;
		in_port = false;
	} else {
		/* If out_dev is NULL, the called wants to
		 * set forward to ingress port.
		 */
		local_port = 0;
		in_port = true;
	}
	return mlxsw_afa_block_append_fwd(rulei->act_block,
					  local_port, in_port);
}

struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	struct mlxsw_sp_acl_rule *rule;
	int err;

	mlxsw_sp_acl_ruleset_ref_inc(ruleset);
	rule = kzalloc(sizeof(*rule) + ops->rule_priv_size, GFP_KERNEL);
	if (!rule) {
		err = -ENOMEM;
		goto err_alloc;
	}
	rule->cookie = cookie;
	rule->ruleset = ruleset;

	rule->rulei = mlxsw_sp_acl_rulei_create(mlxsw_sp->acl);
	if (IS_ERR(rule->rulei)) {
		err = PTR_ERR(rule->rulei);
		goto err_rulei_create;
	}
	return rule;

err_rulei_create:
	kfree(rule);
err_alloc:
	mlxsw_sp_acl_ruleset_ref_dec(mlxsw_sp, ruleset);
	return ERR_PTR(err);
}

void mlxsw_sp_acl_rule_destroy(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule *rule)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;

	mlxsw_sp_acl_rulei_destroy(rule->rulei);
	kfree(rule);
	mlxsw_sp_acl_ruleset_ref_dec(mlxsw_sp, ruleset);
}

int mlxsw_sp_acl_rule_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_rule *rule)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	int err;

	err = ops->rule_add(mlxsw_sp, ruleset->priv, rule->priv, rule->rulei);
	if (err)
		return err;

	err = rhashtable_insert_fast(&ruleset->rule_ht, &rule->ht_node,
				     mlxsw_sp_acl_rule_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return 0;

err_rhashtable_insert:
	ops->rule_del(mlxsw_sp, rule->priv);
	return err;
}

void mlxsw_sp_acl_rule_del(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_rule *rule)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	rhashtable_remove_fast(&ruleset->rule_ht, &rule->ht_node,
			       mlxsw_sp_acl_rule_ht_params);
	ops->rule_del(mlxsw_sp, rule->priv);
}

struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_lookup(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie)
{
	return rhashtable_lookup_fast(&ruleset->rule_ht, &cookie,
				       mlxsw_sp_acl_rule_ht_params);
}

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rule_rulei(struct mlxsw_sp_acl_rule *rule)
{
	return rule->rulei;
}

#define MLXSW_SP_KDVL_ACT_EXT_SIZE 1

static int mlxsw_sp_act_kvdl_set_add(void *priv, u32 *p_kvdl_index,
				     char *enc_actions, bool is_first)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char pefa_pl[MLXSW_REG_PEFA_LEN];
	u32 kvdl_index;
	int ret;
	int err;

	/* The first action set of a TCAM entry is stored directly in TCAM,
	 * not KVD linear area.
	 */
	if (is_first)
		return 0;

	ret = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KDVL_ACT_EXT_SIZE);
	if (ret < 0)
		return ret;
	kvdl_index = ret;
	mlxsw_reg_pefa_pack(pefa_pl, kvdl_index, enc_actions);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pefa), pefa_pl);
	if (err)
		goto err_pefa_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_pefa_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
	return err;
}

static void mlxsw_sp_act_kvdl_set_del(void *priv, u32 kvdl_index,
				      bool is_first)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	if (is_first)
		return;
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
}

static int mlxsw_sp_act_kvdl_fwd_entry_add(void *priv, u32 *p_kvdl_index,
					   u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char ppbs_pl[MLXSW_REG_PPBS_LEN];
	u32 kvdl_index;
	int ret;
	int err;

	ret = mlxsw_sp_kvdl_alloc(mlxsw_sp, 1);
	if (ret < 0)
		return ret;
	kvdl_index = ret;
	mlxsw_reg_ppbs_pack(ppbs_pl, kvdl_index, local_port);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppbs), ppbs_pl);
	if (err)
		goto err_ppbs_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_ppbs_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
	return err;
}

static void mlxsw_sp_act_kvdl_fwd_entry_del(void *priv, u32 kvdl_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
}

static const struct mlxsw_afa_ops mlxsw_sp_act_afa_ops = {
	.kvdl_set_add		= mlxsw_sp_act_kvdl_set_add,
	.kvdl_set_del		= mlxsw_sp_act_kvdl_set_del,
	.kvdl_fwd_entry_add	= mlxsw_sp_act_kvdl_fwd_entry_add,
	.kvdl_fwd_entry_del	= mlxsw_sp_act_kvdl_fwd_entry_del,
};

int mlxsw_sp_acl_init(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_sp_acl_ops *acl_ops = &mlxsw_sp_acl_tcam_ops;
	struct mlxsw_sp_acl *acl;
	int err;

	acl = kzalloc(sizeof(*acl) + acl_ops->priv_size, GFP_KERNEL);
	if (!acl)
		return -ENOMEM;
	mlxsw_sp->acl = acl;

	acl->afk = mlxsw_afk_create(MLXSW_CORE_RES_GET(mlxsw_sp->core,
						       ACL_FLEX_KEYS),
				    mlxsw_sp_afk_blocks,
				    MLXSW_SP_AFK_BLOCKS_COUNT);
	if (!acl->afk) {
		err = -ENOMEM;
		goto err_afk_create;
	}

	acl->afa = mlxsw_afa_create(MLXSW_CORE_RES_GET(mlxsw_sp->core,
						       ACL_ACTIONS_PER_SET),
				    &mlxsw_sp_act_afa_ops, mlxsw_sp);
	if (IS_ERR(acl->afa)) {
		err = PTR_ERR(acl->afa);
		goto err_afa_create;
	}

	err = rhashtable_init(&acl->ruleset_ht,
			      &mlxsw_sp_acl_ruleset_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = acl_ops->init(mlxsw_sp, acl->priv);
	if (err)
		goto err_acl_ops_init;

	acl->ops = acl_ops;
	return 0;

err_acl_ops_init:
	rhashtable_destroy(&acl->ruleset_ht);
err_rhashtable_init:
	mlxsw_afa_destroy(acl->afa);
err_afa_create:
	mlxsw_afk_destroy(acl->afk);
err_afk_create:
	kfree(acl);
	return err;
}

void mlxsw_sp_acl_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	const struct mlxsw_sp_acl_ops *acl_ops = acl->ops;

	acl_ops->fini(mlxsw_sp, acl->priv);
	rhashtable_destroy(&acl->ruleset_ht);
	mlxsw_afa_destroy(acl->afa);
	mlxsw_afk_destroy(acl->afk);
	kfree(acl);
}
