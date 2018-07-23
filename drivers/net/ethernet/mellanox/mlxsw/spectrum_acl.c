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
#include <net/net_namespace.h>
#include <net/tc_act/tc_vlan.h>

#include "reg.h"
#include "core.h"
#include "resources.h"
#include "spectrum.h"
#include "core_acl_flex_keys.h"
#include "core_acl_flex_actions.h"
#include "spectrum_acl_tcam.h"

struct mlxsw_sp_acl {
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_afk *afk;
	struct mlxsw_sp_fid *dummy_fid;
	struct rhashtable ruleset_ht;
	struct list_head rules;
	struct {
		struct delayed_work dw;
		unsigned long interval;	/* ms */
#define MLXSW_SP_ACL_RULE_ACTIVITY_UPDATE_PERIOD_MS 1000
	} rule_activity_update;
	struct mlxsw_sp_acl_tcam tcam;
};

struct mlxsw_afk *mlxsw_sp_acl_afk(struct mlxsw_sp_acl *acl)
{
	return acl->afk;
}

struct mlxsw_sp_acl_block_binding {
	struct list_head list;
	struct net_device *dev;
	struct mlxsw_sp_port *mlxsw_sp_port;
	bool ingress;
};

struct mlxsw_sp_acl_block {
	struct list_head binding_list;
	struct mlxsw_sp_acl_ruleset *ruleset_zero;
	struct mlxsw_sp *mlxsw_sp;
	unsigned int rule_count;
	unsigned int disable_count;
};

struct mlxsw_sp_acl_ruleset_ht_key {
	struct mlxsw_sp_acl_block *block;
	u32 chain_index;
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
	struct list_head list;
	unsigned long cookie; /* HT key */
	struct mlxsw_sp_acl_ruleset *ruleset;
	struct mlxsw_sp_acl_rule_info *rulei;
	u64 last_used;
	u64 last_packets;
	u64 last_bytes;
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

struct mlxsw_sp_fid *mlxsw_sp_acl_dummy_fid(struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_sp->acl->dummy_fid;
}

struct mlxsw_sp *mlxsw_sp_acl_block_mlxsw_sp(struct mlxsw_sp_acl_block *block)
{
	return block->mlxsw_sp;
}

unsigned int mlxsw_sp_acl_block_rule_count(struct mlxsw_sp_acl_block *block)
{
	return block ? block->rule_count : 0;
}

void mlxsw_sp_acl_block_disable_inc(struct mlxsw_sp_acl_block *block)
{
	if (block)
		block->disable_count++;
}

void mlxsw_sp_acl_block_disable_dec(struct mlxsw_sp_acl_block *block)
{
	if (block)
		block->disable_count--;
}

bool mlxsw_sp_acl_block_disabled(struct mlxsw_sp_acl_block *block)
{
	return block->disable_count;
}

static bool
mlxsw_sp_acl_ruleset_is_singular(const struct mlxsw_sp_acl_ruleset *ruleset)
{
	/* We hold a reference on ruleset ourselves */
	return ruleset->ref_count == 2;
}

static int
mlxsw_sp_acl_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_block *block,
			  struct mlxsw_sp_acl_block_binding *binding)
{
	struct mlxsw_sp_acl_ruleset *ruleset = block->ruleset_zero;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	return ops->ruleset_bind(mlxsw_sp, ruleset->priv,
				 binding->mlxsw_sp_port, binding->ingress);
}

static void
mlxsw_sp_acl_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_block *block,
			    struct mlxsw_sp_acl_block_binding *binding)
{
	struct mlxsw_sp_acl_ruleset *ruleset = block->ruleset_zero;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	ops->ruleset_unbind(mlxsw_sp, ruleset->priv,
			    binding->mlxsw_sp_port, binding->ingress);
}

static bool mlxsw_sp_acl_ruleset_block_bound(struct mlxsw_sp_acl_block *block)
{
	return block->ruleset_zero;
}

static int
mlxsw_sp_acl_ruleset_block_bind(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_ruleset *ruleset,
				struct mlxsw_sp_acl_block *block)
{
	struct mlxsw_sp_acl_block_binding *binding;
	int err;

	block->ruleset_zero = ruleset;
	list_for_each_entry(binding, &block->binding_list, list) {
		err = mlxsw_sp_acl_ruleset_bind(mlxsw_sp, block, binding);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	list_for_each_entry_continue_reverse(binding, &block->binding_list,
					     list)
		mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, block, binding);
	block->ruleset_zero = NULL;

	return err;
}

static void
mlxsw_sp_acl_ruleset_block_unbind(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_ruleset *ruleset,
				  struct mlxsw_sp_acl_block *block)
{
	struct mlxsw_sp_acl_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, block, binding);
	block->ruleset_zero = NULL;
}

struct mlxsw_sp_acl_block *mlxsw_sp_acl_block_create(struct mlxsw_sp *mlxsw_sp,
						     struct net *net)
{
	struct mlxsw_sp_acl_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return NULL;
	INIT_LIST_HEAD(&block->binding_list);
	block->mlxsw_sp = mlxsw_sp;
	return block;
}

void mlxsw_sp_acl_block_destroy(struct mlxsw_sp_acl_block *block)
{
	WARN_ON(!list_empty(&block->binding_list));
	kfree(block);
}

static struct mlxsw_sp_acl_block_binding *
mlxsw_sp_acl_block_lookup(struct mlxsw_sp_acl_block *block,
			  struct mlxsw_sp_port *mlxsw_sp_port, bool ingress)
{
	struct mlxsw_sp_acl_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		if (binding->mlxsw_sp_port == mlxsw_sp_port &&
		    binding->ingress == ingress)
			return binding;
	return NULL;
}

int mlxsw_sp_acl_block_bind(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_block *block,
			    struct mlxsw_sp_port *mlxsw_sp_port,
			    bool ingress)
{
	struct mlxsw_sp_acl_block_binding *binding;
	int err;

	if (WARN_ON(mlxsw_sp_acl_block_lookup(block, mlxsw_sp_port, ingress)))
		return -EEXIST;

	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding)
		return -ENOMEM;
	binding->mlxsw_sp_port = mlxsw_sp_port;
	binding->ingress = ingress;

	if (mlxsw_sp_acl_ruleset_block_bound(block)) {
		err = mlxsw_sp_acl_ruleset_bind(mlxsw_sp, block, binding);
		if (err)
			goto err_ruleset_bind;
	}

	list_add(&binding->list, &block->binding_list);
	return 0;

err_ruleset_bind:
	kfree(binding);
	return err;
}

int mlxsw_sp_acl_block_unbind(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_block *block,
			      struct mlxsw_sp_port *mlxsw_sp_port,
			      bool ingress)
{
	struct mlxsw_sp_acl_block_binding *binding;

	binding = mlxsw_sp_acl_block_lookup(block, mlxsw_sp_port, ingress);
	if (!binding)
		return -ENOENT;

	list_del(&binding->list);

	if (mlxsw_sp_acl_ruleset_block_bound(block))
		mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, block, binding);

	kfree(binding);
	return 0;
}

static struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_create(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_block *block, u32 chain_index,
			    const struct mlxsw_sp_acl_profile_ops *ops,
			    struct mlxsw_afk_element_usage *tmplt_elusage)
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
	ruleset->ht_key.block = block;
	ruleset->ht_key.chain_index = chain_index;
	ruleset->ht_key.ops = ops;

	err = rhashtable_init(&ruleset->rule_ht, &mlxsw_sp_acl_rule_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = ops->ruleset_add(mlxsw_sp, &acl->tcam, ruleset->priv,
			       tmplt_elusage);
	if (err)
		goto err_ops_ruleset_add;

	err = rhashtable_insert_fast(&acl->ruleset_ht, &ruleset->ht_node,
				     mlxsw_sp_acl_ruleset_ht_params);
	if (err)
		goto err_ht_insert;

	return ruleset;

err_ht_insert:
	ops->ruleset_del(mlxsw_sp, ruleset->priv);
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
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;

	rhashtable_remove_fast(&acl->ruleset_ht, &ruleset->ht_node,
			       mlxsw_sp_acl_ruleset_ht_params);
	ops->ruleset_del(mlxsw_sp, ruleset->priv);
	rhashtable_destroy(&ruleset->rule_ht);
	kfree(ruleset);
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
	mlxsw_sp_acl_ruleset_destroy(mlxsw_sp, ruleset);
}

static struct mlxsw_sp_acl_ruleset *
__mlxsw_sp_acl_ruleset_lookup(struct mlxsw_sp_acl *acl,
			      struct mlxsw_sp_acl_block *block, u32 chain_index,
			      const struct mlxsw_sp_acl_profile_ops *ops)
{
	struct mlxsw_sp_acl_ruleset_ht_key ht_key;

	memset(&ht_key, 0, sizeof(ht_key));
	ht_key.block = block;
	ht_key.chain_index = chain_index;
	ht_key.ops = ops;
	return rhashtable_lookup_fast(&acl->ruleset_ht, &ht_key,
				      mlxsw_sp_acl_ruleset_ht_params);
}

struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_lookup(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_block *block, u32 chain_index,
			    enum mlxsw_sp_acl_profile profile)
{
	const struct mlxsw_sp_acl_profile_ops *ops;
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	struct mlxsw_sp_acl_ruleset *ruleset;

	ops = mlxsw_sp_acl_tcam_profile_ops(mlxsw_sp, profile);
	if (!ops)
		return ERR_PTR(-EINVAL);
	ruleset = __mlxsw_sp_acl_ruleset_lookup(acl, block, chain_index, ops);
	if (!ruleset)
		return ERR_PTR(-ENOENT);
	return ruleset;
}

struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_get(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_block *block, u32 chain_index,
			 enum mlxsw_sp_acl_profile profile,
			 struct mlxsw_afk_element_usage *tmplt_elusage)
{
	const struct mlxsw_sp_acl_profile_ops *ops;
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;
	struct mlxsw_sp_acl_ruleset *ruleset;

	ops = mlxsw_sp_acl_tcam_profile_ops(mlxsw_sp, profile);
	if (!ops)
		return ERR_PTR(-EINVAL);

	ruleset = __mlxsw_sp_acl_ruleset_lookup(acl, block, chain_index, ops);
	if (ruleset) {
		mlxsw_sp_acl_ruleset_ref_inc(ruleset);
		return ruleset;
	}
	return mlxsw_sp_acl_ruleset_create(mlxsw_sp, block, chain_index, ops,
					   tmplt_elusage);
}

void mlxsw_sp_acl_ruleset_put(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_ruleset *ruleset)
{
	mlxsw_sp_acl_ruleset_ref_dec(mlxsw_sp, ruleset);
}

u16 mlxsw_sp_acl_ruleset_group_id(struct mlxsw_sp_acl_ruleset *ruleset)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	return ops->ruleset_group_id(ruleset->priv);
}

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rulei_create(struct mlxsw_sp_acl *acl)
{
	struct mlxsw_sp_acl_rule_info *rulei;
	int err;

	rulei = kzalloc(sizeof(*rulei), GFP_KERNEL);
	if (!rulei)
		return NULL;
	rulei->act_block = mlxsw_afa_block_create(acl->mlxsw_sp->afa);
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
	rulei->priority = priority >> 16;
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

int mlxsw_sp_acl_rulei_act_continue(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_continue(rulei->act_block);
}

int mlxsw_sp_acl_rulei_act_jump(struct mlxsw_sp_acl_rule_info *rulei,
				u16 group_id)
{
	return mlxsw_afa_block_jump(rulei->act_block, group_id);
}

int mlxsw_sp_acl_rulei_act_terminate(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_terminate(rulei->act_block);
}

int mlxsw_sp_acl_rulei_act_drop(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_append_drop(rulei->act_block);
}

int mlxsw_sp_acl_rulei_act_trap(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_append_trap(rulei->act_block,
					   MLXSW_TRAP_ID_ACL0);
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
		/* If out_dev is NULL, the caller wants to
		 * set forward to ingress port.
		 */
		local_port = 0;
		in_port = true;
	}
	return mlxsw_afa_block_append_fwd(rulei->act_block,
					  local_port, in_port);
}

int mlxsw_sp_acl_rulei_act_mirror(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  struct mlxsw_sp_acl_block *block,
				  struct net_device *out_dev)
{
	struct mlxsw_sp_acl_block_binding *binding;
	struct mlxsw_sp_port *in_port;

	if (!list_is_singular(&block->binding_list))
		return -EOPNOTSUPP;

	binding = list_first_entry(&block->binding_list,
				   struct mlxsw_sp_acl_block_binding, list);
	in_port = binding->mlxsw_sp_port;

	return mlxsw_afa_block_append_mirror(rulei->act_block,
					     in_port->local_port,
					     out_dev,
					     binding->ingress);
}

int mlxsw_sp_acl_rulei_act_vlan(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_rule_info *rulei,
				u32 action, u16 vid, u16 proto, u8 prio)
{
	u8 ethertype;

	if (action == TCA_VLAN_ACT_MODIFY) {
		switch (proto) {
		case ETH_P_8021Q:
			ethertype = 0;
			break;
		case ETH_P_8021AD:
			ethertype = 1;
			break;
		default:
			dev_err(mlxsw_sp->bus_info->dev, "Unsupported VLAN protocol %#04x\n",
				proto);
			return -EINVAL;
		}

		return mlxsw_afa_block_append_vlan_modify(rulei->act_block,
							  vid, prio, ethertype);
	} else {
		dev_err(mlxsw_sp->bus_info->dev, "Unsupported VLAN action\n");
		return -EINVAL;
	}
}

int mlxsw_sp_acl_rulei_act_count(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_append_counter(rulei->act_block,
					      &rulei->counter_index);
}

int mlxsw_sp_acl_rulei_act_fid_set(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_rule_info *rulei,
				   u16 fid)
{
	return mlxsw_afa_block_append_fid_set(rulei->act_block, fid);
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
	rule = kzalloc(sizeof(*rule) + ops->rule_priv_size(mlxsw_sp),
		       GFP_KERNEL);
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

	if (!ruleset->ht_key.chain_index &&
	    mlxsw_sp_acl_ruleset_is_singular(ruleset)) {
		/* We only need ruleset with chain index 0, the implicit
		 * one, to be directly bound to device. The rest of the
		 * rulesets are bound by "Goto action set".
		 */
		err = mlxsw_sp_acl_ruleset_block_bind(mlxsw_sp, ruleset,
						      ruleset->ht_key.block);
		if (err)
			goto err_ruleset_block_bind;
	}

	list_add_tail(&rule->list, &mlxsw_sp->acl->rules);
	ruleset->ht_key.block->rule_count++;
	return 0;

err_ruleset_block_bind:
	rhashtable_remove_fast(&ruleset->rule_ht, &rule->ht_node,
			       mlxsw_sp_acl_rule_ht_params);
err_rhashtable_insert:
	ops->rule_del(mlxsw_sp, rule->priv);
	return err;
}

void mlxsw_sp_acl_rule_del(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_rule *rule)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	ruleset->ht_key.block->rule_count--;
	list_del(&rule->list);
	if (!ruleset->ht_key.chain_index &&
	    mlxsw_sp_acl_ruleset_is_singular(ruleset))
		mlxsw_sp_acl_ruleset_block_unbind(mlxsw_sp, ruleset,
						  ruleset->ht_key.block);
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

static int mlxsw_sp_acl_rule_activity_update(struct mlxsw_sp *mlxsw_sp,
					     struct mlxsw_sp_acl_rule *rule)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	bool active;
	int err;

	err = ops->rule_activity_get(mlxsw_sp, rule->priv, &active);
	if (err)
		return err;
	if (active)
		rule->last_used = jiffies;
	return 0;
}

static int mlxsw_sp_acl_rules_activity_update(struct mlxsw_sp_acl *acl)
{
	struct mlxsw_sp_acl_rule *rule;
	int err;

	/* Protect internal structures from changes */
	rtnl_lock();
	list_for_each_entry(rule, &acl->rules, list) {
		err = mlxsw_sp_acl_rule_activity_update(acl->mlxsw_sp,
							rule);
		if (err)
			goto err_rule_update;
	}
	rtnl_unlock();
	return 0;

err_rule_update:
	rtnl_unlock();
	return err;
}

static void mlxsw_sp_acl_rule_activity_work_schedule(struct mlxsw_sp_acl *acl)
{
	unsigned long interval = acl->rule_activity_update.interval;

	mlxsw_core_schedule_dw(&acl->rule_activity_update.dw,
			       msecs_to_jiffies(interval));
}

static void mlxsw_sp_acl_rul_activity_update_work(struct work_struct *work)
{
	struct mlxsw_sp_acl *acl = container_of(work, struct mlxsw_sp_acl,
						rule_activity_update.dw.work);
	int err;

	err = mlxsw_sp_acl_rules_activity_update(acl);
	if (err)
		dev_err(acl->mlxsw_sp->bus_info->dev, "Could not update acl activity");

	mlxsw_sp_acl_rule_activity_work_schedule(acl);
}

int mlxsw_sp_acl_rule_get_stats(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *last_use)

{
	struct mlxsw_sp_acl_rule_info *rulei;
	u64 current_packets;
	u64 current_bytes;
	int err;

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	err = mlxsw_sp_flow_counter_get(mlxsw_sp, rulei->counter_index,
					&current_packets, &current_bytes);
	if (err)
		return err;

	*packets = current_packets - rule->last_packets;
	*bytes = current_bytes - rule->last_bytes;
	*last_use = rule->last_used;

	rule->last_bytes = current_bytes;
	rule->last_packets = current_packets;

	return 0;
}

int mlxsw_sp_acl_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_fid *fid;
	struct mlxsw_sp_acl *acl;
	size_t alloc_size;
	int err;

	alloc_size = sizeof(*acl) + mlxsw_sp_acl_tcam_priv_size(mlxsw_sp);
	acl = kzalloc(alloc_size, GFP_KERNEL);
	if (!acl)
		return -ENOMEM;
	mlxsw_sp->acl = acl;
	acl->mlxsw_sp = mlxsw_sp;
	acl->afk = mlxsw_afk_create(MLXSW_CORE_RES_GET(mlxsw_sp->core,
						       ACL_FLEX_KEYS),
				    mlxsw_sp->afk_ops);
	if (!acl->afk) {
		err = -ENOMEM;
		goto err_afk_create;
	}

	err = rhashtable_init(&acl->ruleset_ht,
			      &mlxsw_sp_acl_ruleset_ht_params);
	if (err)
		goto err_rhashtable_init;

	fid = mlxsw_sp_fid_dummy_get(mlxsw_sp);
	if (IS_ERR(fid)) {
		err = PTR_ERR(fid);
		goto err_fid_get;
	}
	acl->dummy_fid = fid;

	INIT_LIST_HEAD(&acl->rules);
	err = mlxsw_sp_acl_tcam_init(mlxsw_sp, &acl->tcam);
	if (err)
		goto err_acl_ops_init;

	/* Create the delayed work for the rule activity_update */
	INIT_DELAYED_WORK(&acl->rule_activity_update.dw,
			  mlxsw_sp_acl_rul_activity_update_work);
	acl->rule_activity_update.interval = MLXSW_SP_ACL_RULE_ACTIVITY_UPDATE_PERIOD_MS;
	mlxsw_core_schedule_dw(&acl->rule_activity_update.dw, 0);
	return 0;

err_acl_ops_init:
	mlxsw_sp_fid_put(fid);
err_fid_get:
	rhashtable_destroy(&acl->ruleset_ht);
err_rhashtable_init:
	mlxsw_afk_destroy(acl->afk);
err_afk_create:
	kfree(acl);
	return err;
}

void mlxsw_sp_acl_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;

	cancel_delayed_work_sync(&mlxsw_sp->acl->rule_activity_update.dw);
	mlxsw_sp_acl_tcam_fini(mlxsw_sp, &acl->tcam);
	WARN_ON(!list_empty(&acl->rules));
	mlxsw_sp_fid_put(acl->dummy_fid);
	rhashtable_destroy(&acl->ruleset_ht);
	mlxsw_afk_destroy(acl->afk);
	kfree(acl);
}
