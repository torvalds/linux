// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
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
	struct mutex rules_lock; /* Protects rules list */
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

struct mlxsw_sp_acl_ruleset_ht_key {
	struct mlxsw_sp_flow_block *block;
	u32 chain_index;
	const struct mlxsw_sp_acl_profile_ops *ops;
};

struct mlxsw_sp_acl_ruleset {
	struct rhash_head ht_node; /* Member of acl HT */
	struct mlxsw_sp_acl_ruleset_ht_key ht_key;
	struct rhashtable rule_ht;
	unsigned int ref_count;
	unsigned int min_prio;
	unsigned int max_prio;
	unsigned long priv[];
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
	u64 last_drops;
	unsigned long priv[];
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

static bool
mlxsw_sp_acl_ruleset_is_singular(const struct mlxsw_sp_acl_ruleset *ruleset)
{
	/* We hold a reference on ruleset ourselves */
	return ruleset->ref_count == 2;
}

int mlxsw_sp_acl_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_flow_block *block,
			      struct mlxsw_sp_flow_block_binding *binding)
{
	struct mlxsw_sp_acl_ruleset *ruleset = block->ruleset_zero;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	return ops->ruleset_bind(mlxsw_sp, ruleset->priv,
				 binding->mlxsw_sp_port, binding->ingress);
}

void mlxsw_sp_acl_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_flow_block *block,
				 struct mlxsw_sp_flow_block_binding *binding)
{
	struct mlxsw_sp_acl_ruleset *ruleset = block->ruleset_zero;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;

	ops->ruleset_unbind(mlxsw_sp, ruleset->priv,
			    binding->mlxsw_sp_port, binding->ingress);
}

static int
mlxsw_sp_acl_ruleset_block_bind(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_ruleset *ruleset,
				struct mlxsw_sp_flow_block *block)
{
	struct mlxsw_sp_flow_block_binding *binding;
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
				  struct mlxsw_sp_flow_block *block)
{
	struct mlxsw_sp_flow_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, block, binding);
	block->ruleset_zero = NULL;
}

static struct mlxsw_sp_acl_ruleset *
mlxsw_sp_acl_ruleset_create(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_flow_block *block, u32 chain_index,
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
			       tmplt_elusage, &ruleset->min_prio,
			       &ruleset->max_prio);
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
			      struct mlxsw_sp_flow_block *block, u32 chain_index,
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
			    struct mlxsw_sp_flow_block *block, u32 chain_index,
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
			 struct mlxsw_sp_flow_block *block, u32 chain_index,
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

void mlxsw_sp_acl_ruleset_prio_get(struct mlxsw_sp_acl_ruleset *ruleset,
				   unsigned int *p_min_prio,
				   unsigned int *p_max_prio)
{
	*p_min_prio = ruleset->min_prio;
	*p_max_prio = ruleset->max_prio;
}

struct mlxsw_sp_acl_rule_info *
mlxsw_sp_acl_rulei_create(struct mlxsw_sp_acl *acl,
			  struct mlxsw_afa_block *afa_block)
{
	struct mlxsw_sp_acl_rule_info *rulei;
	int err;

	rulei = kzalloc(sizeof(*rulei), GFP_KERNEL);
	if (!rulei)
		return ERR_PTR(-ENOMEM);

	if (afa_block) {
		rulei->act_block = afa_block;
		return rulei;
	}

	rulei->act_block = mlxsw_afa_block_create(acl->mlxsw_sp->afa);
	if (IS_ERR(rulei->act_block)) {
		err = PTR_ERR(rulei->act_block);
		goto err_afa_block_create;
	}
	rulei->action_created = 1;
	return rulei;

err_afa_block_create:
	kfree(rulei);
	return ERR_PTR(err);
}

void mlxsw_sp_acl_rulei_destroy(struct mlxsw_sp_acl_rule_info *rulei)
{
	if (rulei->action_created)
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

int mlxsw_sp_acl_rulei_act_drop(struct mlxsw_sp_acl_rule_info *rulei,
				bool ingress,
				const struct flow_action_cookie *fa_cookie,
				struct netlink_ext_ack *extack)
{
	return mlxsw_afa_block_append_drop(rulei->act_block, ingress,
					   fa_cookie, extack);
}

int mlxsw_sp_acl_rulei_act_trap(struct mlxsw_sp_acl_rule_info *rulei)
{
	return mlxsw_afa_block_append_trap(rulei->act_block,
					   MLXSW_TRAP_ID_ACL0);
}

int mlxsw_sp_acl_rulei_act_fwd(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_rule_info *rulei,
			       struct net_device *out_dev,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	u16 local_port;
	bool in_port;

	if (out_dev) {
		if (!mlxsw_sp_port_dev_check(out_dev)) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid output device");
			return -EINVAL;
		}
		mlxsw_sp_port = netdev_priv(out_dev);
		if (mlxsw_sp_port->mlxsw_sp != mlxsw_sp) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid output device");
			return -EINVAL;
		}
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
					  local_port, in_port, extack);
}

int mlxsw_sp_acl_rulei_act_mirror(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  struct mlxsw_sp_flow_block *block,
				  struct net_device *out_dev,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_flow_block_binding *binding;
	struct mlxsw_sp_port *in_port;

	if (!list_is_singular(&block->binding_list)) {
		NL_SET_ERR_MSG_MOD(extack, "Only a single mirror source is allowed");
		return -EOPNOTSUPP;
	}
	binding = list_first_entry(&block->binding_list,
				   struct mlxsw_sp_flow_block_binding, list);
	in_port = binding->mlxsw_sp_port;

	return mlxsw_afa_block_append_mirror(rulei->act_block,
					     in_port->local_port,
					     out_dev,
					     binding->ingress,
					     extack);
}

int mlxsw_sp_acl_rulei_act_vlan(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_rule_info *rulei,
				u32 action, u16 vid, u16 proto, u8 prio,
				struct netlink_ext_ack *extack)
{
	u8 ethertype;

	if (action == FLOW_ACTION_VLAN_MANGLE) {
		switch (proto) {
		case ETH_P_8021Q:
			ethertype = 0;
			break;
		case ETH_P_8021AD:
			ethertype = 1;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported VLAN protocol");
			dev_err(mlxsw_sp->bus_info->dev, "Unsupported VLAN protocol %#04x\n",
				proto);
			return -EINVAL;
		}

		return mlxsw_afa_block_append_vlan_modify(rulei->act_block,
							  vid, prio, ethertype,
							  extack);
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported VLAN action");
		dev_err(mlxsw_sp->bus_info->dev, "Unsupported VLAN action\n");
		return -EINVAL;
	}
}

int mlxsw_sp_acl_rulei_act_priority(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_rule_info *rulei,
				    u32 prio, struct netlink_ext_ack *extack)
{
	/* Even though both Linux and Spectrum switches support 16 priorities,
	 * spectrum_qdisc only processes the first eight priomap elements, and
	 * the DCB and PFC features are tied to 8 priorities as well. Therefore
	 * bounce attempts to prioritize packets to higher priorities.
	 */
	if (prio >= IEEE_8021QAZ_MAX_TCS) {
		NL_SET_ERR_MSG_MOD(extack, "Only priorities 0..7 are supported");
		return -EINVAL;
	}
	return mlxsw_afa_block_append_qos_switch_prio(rulei->act_block, prio,
						      extack);
}

enum mlxsw_sp_acl_mangle_field {
	MLXSW_SP_ACL_MANGLE_FIELD_IP_DSFIELD,
	MLXSW_SP_ACL_MANGLE_FIELD_IP_DSCP,
	MLXSW_SP_ACL_MANGLE_FIELD_IP_ECN,
	MLXSW_SP_ACL_MANGLE_FIELD_IP_SPORT,
	MLXSW_SP_ACL_MANGLE_FIELD_IP_DPORT,
	MLXSW_SP_ACL_MANGLE_FIELD_IP4_SIP,
	MLXSW_SP_ACL_MANGLE_FIELD_IP4_DIP,
};

struct mlxsw_sp_acl_mangle_action {
	enum flow_action_mangle_base htype;
	/* Offset is u32-aligned. */
	u32 offset;
	/* Mask bits are unset for the modified field. */
	u32 mask;
	/* Shift required to extract the set value. */
	u32 shift;
	enum mlxsw_sp_acl_mangle_field field;
};

#define MLXSW_SP_ACL_MANGLE_ACTION(_htype, _offset, _mask, _shift, _field) \
	{								\
		.htype = _htype,					\
		.offset = _offset,					\
		.mask = _mask,						\
		.shift = _shift,					\
		.field = MLXSW_SP_ACL_MANGLE_FIELD_##_field,		\
	}

#define MLXSW_SP_ACL_MANGLE_ACTION_IP4(_offset, _mask, _shift, _field) \
	MLXSW_SP_ACL_MANGLE_ACTION(FLOW_ACT_MANGLE_HDR_TYPE_IP4,       \
				   _offset, _mask, _shift, _field)

#define MLXSW_SP_ACL_MANGLE_ACTION_IP6(_offset, _mask, _shift, _field) \
	MLXSW_SP_ACL_MANGLE_ACTION(FLOW_ACT_MANGLE_HDR_TYPE_IP6,       \
				   _offset, _mask, _shift, _field)

#define MLXSW_SP_ACL_MANGLE_ACTION_TCP(_offset, _mask, _shift, _field) \
	MLXSW_SP_ACL_MANGLE_ACTION(FLOW_ACT_MANGLE_HDR_TYPE_TCP, _offset, _mask, _shift, _field)

#define MLXSW_SP_ACL_MANGLE_ACTION_UDP(_offset, _mask, _shift, _field) \
	MLXSW_SP_ACL_MANGLE_ACTION(FLOW_ACT_MANGLE_HDR_TYPE_UDP, _offset, _mask, _shift, _field)

static struct mlxsw_sp_acl_mangle_action mlxsw_sp_acl_mangle_actions[] = {
	MLXSW_SP_ACL_MANGLE_ACTION_IP4(0, 0xff00ffff, 16, IP_DSFIELD),
	MLXSW_SP_ACL_MANGLE_ACTION_IP4(0, 0xff03ffff, 18, IP_DSCP),
	MLXSW_SP_ACL_MANGLE_ACTION_IP4(0, 0xfffcffff, 16, IP_ECN),

	MLXSW_SP_ACL_MANGLE_ACTION_IP6(0, 0xf00fffff, 20, IP_DSFIELD),
	MLXSW_SP_ACL_MANGLE_ACTION_IP6(0, 0xf03fffff, 22, IP_DSCP),
	MLXSW_SP_ACL_MANGLE_ACTION_IP6(0, 0xffcfffff, 20, IP_ECN),

	MLXSW_SP_ACL_MANGLE_ACTION_TCP(0, 0x0000ffff, 16, IP_SPORT),
	MLXSW_SP_ACL_MANGLE_ACTION_TCP(0, 0xffff0000, 0,  IP_DPORT),

	MLXSW_SP_ACL_MANGLE_ACTION_UDP(0, 0x0000ffff, 16, IP_SPORT),
	MLXSW_SP_ACL_MANGLE_ACTION_UDP(0, 0xffff0000, 0,  IP_DPORT),

	MLXSW_SP_ACL_MANGLE_ACTION_IP4(12, 0x00000000, 0, IP4_SIP),
	MLXSW_SP_ACL_MANGLE_ACTION_IP4(16, 0x00000000, 0, IP4_DIP),
};

static int
mlxsw_sp_acl_rulei_act_mangle_field(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_acl_rule_info *rulei,
				    struct mlxsw_sp_acl_mangle_action *mact,
				    u32 val, struct netlink_ext_ack *extack)
{
	switch (mact->field) {
	case MLXSW_SP_ACL_MANGLE_FIELD_IP_DSFIELD:
		return mlxsw_afa_block_append_qos_dsfield(rulei->act_block,
							  val, extack);
	case MLXSW_SP_ACL_MANGLE_FIELD_IP_DSCP:
		return mlxsw_afa_block_append_qos_dscp(rulei->act_block,
						       val, extack);
	case MLXSW_SP_ACL_MANGLE_FIELD_IP_ECN:
		return mlxsw_afa_block_append_qos_ecn(rulei->act_block,
						      val, extack);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlxsw_sp1_acl_rulei_act_mangle_field(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_acl_rule_info *rulei,
						struct mlxsw_sp_acl_mangle_action *mact,
						u32 val, struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_sp_acl_rulei_act_mangle_field(mlxsw_sp, rulei, mact, val, extack);
	if (err != -EOPNOTSUPP)
		return err;

	NL_SET_ERR_MSG_MOD(extack, "Unsupported mangle field");
	return err;
}

static int mlxsw_sp2_acl_rulei_act_mangle_field(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_acl_rule_info *rulei,
						struct mlxsw_sp_acl_mangle_action *mact,
						u32 val, struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_sp_acl_rulei_act_mangle_field(mlxsw_sp, rulei, mact, val, extack);
	if (err != -EOPNOTSUPP)
		return err;

	switch (mact->field) {
	case MLXSW_SP_ACL_MANGLE_FIELD_IP_SPORT:
		return mlxsw_afa_block_append_l4port(rulei->act_block, false, val, extack);
	case MLXSW_SP_ACL_MANGLE_FIELD_IP_DPORT:
		return mlxsw_afa_block_append_l4port(rulei->act_block, true, val, extack);
	/* IPv4 fields */
	case MLXSW_SP_ACL_MANGLE_FIELD_IP4_SIP:
		return mlxsw_afa_block_append_ip(rulei->act_block, false,
						 true, val, 0, extack);
	case MLXSW_SP_ACL_MANGLE_FIELD_IP4_DIP:
		return mlxsw_afa_block_append_ip(rulei->act_block, true,
						 true, val, 0, extack);
	default:
		break;
	}

	NL_SET_ERR_MSG_MOD(extack, "Unsupported mangle field");
	return err;
}

int mlxsw_sp_acl_rulei_act_mangle(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  enum flow_action_mangle_base htype,
				  u32 offset, u32 mask, u32 val,
				  struct netlink_ext_ack *extack)
{
	const struct mlxsw_sp_acl_rulei_ops *acl_rulei_ops = mlxsw_sp->acl_rulei_ops;
	struct mlxsw_sp_acl_mangle_action *mact;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_acl_mangle_actions); ++i) {
		mact = &mlxsw_sp_acl_mangle_actions[i];
		if (mact->htype == htype &&
		    mact->offset == offset &&
		    mact->mask == mask) {
			val >>= mact->shift;
			return acl_rulei_ops->act_mangle_field(mlxsw_sp,
							       rulei, mact,
							       val, extack);
		}
	}

	NL_SET_ERR_MSG_MOD(extack, "Unknown mangle field");
	return -EINVAL;
}

int mlxsw_sp_acl_rulei_act_police(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  u32 index, u64 rate_bytes_ps,
				  u32 burst, struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_afa_block_append_police(rulei->act_block, index,
					    rate_bytes_ps, burst,
					    &rulei->policer_index, extack);
	if (err)
		return err;

	rulei->policer_index_valid = true;

	return 0;
}

int mlxsw_sp_acl_rulei_act_count(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_afa_block_append_counter(rulei->act_block,
					     &rulei->counter_index, extack);
	if (err)
		return err;
	rulei->counter_valid = true;
	return 0;
}

int mlxsw_sp_acl_rulei_act_fid_set(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_rule_info *rulei,
				   u16 fid, struct netlink_ext_ack *extack)
{
	return mlxsw_afa_block_append_fid_set(rulei->act_block, fid, extack);
}

int mlxsw_sp_acl_rulei_act_sample(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_rule_info *rulei,
				  struct mlxsw_sp_flow_block *block,
				  struct psample_group *psample_group, u32 rate,
				  u32 trunc_size, bool truncate,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_flow_block_binding *binding;
	struct mlxsw_sp_port *mlxsw_sp_port;

	if (!list_is_singular(&block->binding_list)) {
		NL_SET_ERR_MSG_MOD(extack, "Only a single sampling source is allowed");
		return -EOPNOTSUPP;
	}
	binding = list_first_entry(&block->binding_list,
				   struct mlxsw_sp_flow_block_binding, list);
	mlxsw_sp_port = binding->mlxsw_sp_port;

	return mlxsw_afa_block_append_sampler(rulei->act_block,
					      mlxsw_sp_port->local_port,
					      psample_group, rate, trunc_size,
					      truncate, binding->ingress,
					      extack);
}

struct mlxsw_sp_acl_rule *
mlxsw_sp_acl_rule_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_acl_ruleset *ruleset,
			 unsigned long cookie,
			 struct mlxsw_afa_block *afa_block,
			 struct netlink_ext_ack *extack)
{
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	struct mlxsw_sp_acl_rule *rule;
	int err;

	mlxsw_sp_acl_ruleset_ref_inc(ruleset);
	rule = kzalloc(sizeof(*rule) + ops->rule_priv_size,
		       GFP_KERNEL);
	if (!rule) {
		err = -ENOMEM;
		goto err_alloc;
	}
	rule->cookie = cookie;
	rule->ruleset = ruleset;

	rule->rulei = mlxsw_sp_acl_rulei_create(mlxsw_sp->acl, afa_block);
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
	struct mlxsw_sp_flow_block *block = ruleset->ht_key.block;
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
		err = mlxsw_sp_acl_ruleset_block_bind(mlxsw_sp, ruleset, block);
		if (err)
			goto err_ruleset_block_bind;
	}

	mutex_lock(&mlxsw_sp->acl->rules_lock);
	list_add_tail(&rule->list, &mlxsw_sp->acl->rules);
	mutex_unlock(&mlxsw_sp->acl->rules_lock);
	block->rule_count++;
	block->ingress_blocker_rule_count += rule->rulei->ingress_bind_blocker;
	block->egress_blocker_rule_count += rule->rulei->egress_bind_blocker;
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
	struct mlxsw_sp_flow_block *block = ruleset->ht_key.block;

	block->egress_blocker_rule_count -= rule->rulei->egress_bind_blocker;
	block->ingress_blocker_rule_count -= rule->rulei->ingress_bind_blocker;
	block->rule_count--;
	mutex_lock(&mlxsw_sp->acl->rules_lock);
	list_del(&rule->list);
	mutex_unlock(&mlxsw_sp->acl->rules_lock);
	if (!ruleset->ht_key.chain_index &&
	    mlxsw_sp_acl_ruleset_is_singular(ruleset))
		mlxsw_sp_acl_ruleset_block_unbind(mlxsw_sp, ruleset, block);
	rhashtable_remove_fast(&ruleset->rule_ht, &rule->ht_node,
			       mlxsw_sp_acl_rule_ht_params);
	ops->rule_del(mlxsw_sp, rule->priv);
}

int mlxsw_sp_acl_rule_action_replace(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_rule *rule,
				     struct mlxsw_afa_block *afa_block)
{
	struct mlxsw_sp_acl_ruleset *ruleset = rule->ruleset;
	const struct mlxsw_sp_acl_profile_ops *ops = ruleset->ht_key.ops;
	struct mlxsw_sp_acl_rule_info *rulei;

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	rulei->act_block = afa_block;

	return ops->rule_action_replace(mlxsw_sp, rule->priv, rule->rulei);
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

	mutex_lock(&acl->rules_lock);
	list_for_each_entry(rule, &acl->rules, list) {
		err = mlxsw_sp_acl_rule_activity_update(acl->mlxsw_sp,
							rule);
		if (err)
			goto err_rule_update;
	}
	mutex_unlock(&acl->rules_lock);
	return 0;

err_rule_update:
	mutex_unlock(&acl->rules_lock);
	return err;
}

static void mlxsw_sp_acl_rule_activity_work_schedule(struct mlxsw_sp_acl *acl)
{
	unsigned long interval = acl->rule_activity_update.interval;

	mlxsw_core_schedule_dw(&acl->rule_activity_update.dw,
			       msecs_to_jiffies(interval));
}

static void mlxsw_sp_acl_rule_activity_update_work(struct work_struct *work)
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
				u64 *packets, u64 *bytes, u64 *drops,
				u64 *last_use,
				enum flow_action_hw_stats *used_hw_stats)

{
	enum mlxsw_sp_policer_type type = MLXSW_SP_POLICER_TYPE_SINGLE_RATE;
	struct mlxsw_sp_acl_rule_info *rulei;
	u64 current_packets = 0;
	u64 current_bytes = 0;
	u64 current_drops = 0;
	int err;

	rulei = mlxsw_sp_acl_rule_rulei(rule);
	if (rulei->counter_valid) {
		err = mlxsw_sp_flow_counter_get(mlxsw_sp, rulei->counter_index,
						&current_packets,
						&current_bytes);
		if (err)
			return err;
		*used_hw_stats = FLOW_ACTION_HW_STATS_IMMEDIATE;
	}
	if (rulei->policer_index_valid) {
		err = mlxsw_sp_policer_drops_counter_get(mlxsw_sp, type,
							 rulei->policer_index,
							 &current_drops);
		if (err)
			return err;
	}
	*packets = current_packets - rule->last_packets;
	*bytes = current_bytes - rule->last_bytes;
	*drops = current_drops - rule->last_drops;
	*last_use = rule->last_used;

	rule->last_bytes = current_bytes;
	rule->last_packets = current_packets;
	rule->last_drops = current_drops;

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
	mutex_init(&acl->rules_lock);
	err = mlxsw_sp_acl_tcam_init(mlxsw_sp, &acl->tcam);
	if (err)
		goto err_acl_ops_init;

	/* Create the delayed work for the rule activity_update */
	INIT_DELAYED_WORK(&acl->rule_activity_update.dw,
			  mlxsw_sp_acl_rule_activity_update_work);
	acl->rule_activity_update.interval = MLXSW_SP_ACL_RULE_ACTIVITY_UPDATE_PERIOD_MS;
	mlxsw_core_schedule_dw(&acl->rule_activity_update.dw, 0);
	return 0;

err_acl_ops_init:
	mutex_destroy(&acl->rules_lock);
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
	mutex_destroy(&acl->rules_lock);
	WARN_ON(!list_empty(&acl->rules));
	mlxsw_sp_fid_put(acl->dummy_fid);
	rhashtable_destroy(&acl->ruleset_ht);
	mlxsw_afk_destroy(acl->afk);
	kfree(acl);
}

u32 mlxsw_sp_acl_region_rehash_intrvl_get(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;

	return mlxsw_sp_acl_tcam_vregion_rehash_intrvl_get(mlxsw_sp,
							   &acl->tcam);
}

int mlxsw_sp_acl_region_rehash_intrvl_set(struct mlxsw_sp *mlxsw_sp, u32 val)
{
	struct mlxsw_sp_acl *acl = mlxsw_sp->acl;

	return mlxsw_sp_acl_tcam_vregion_rehash_intrvl_set(mlxsw_sp,
							   &acl->tcam, val);
}

struct mlxsw_sp_acl_rulei_ops mlxsw_sp1_acl_rulei_ops = {
	.act_mangle_field = mlxsw_sp1_acl_rulei_act_mangle_field,
};

struct mlxsw_sp_acl_rulei_ops mlxsw_sp2_acl_rulei_ops = {
	.act_mangle_field = mlxsw_sp2_acl_rulei_act_mangle_field,
};
