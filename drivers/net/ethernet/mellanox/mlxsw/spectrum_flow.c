// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2020 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <net/net_namespace.h>

#include "spectrum.h"

struct mlxsw_sp_flow_block *
mlxsw_sp_flow_block_create(struct mlxsw_sp *mlxsw_sp, struct net *net)
{
	struct mlxsw_sp_flow_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return NULL;
	INIT_LIST_HEAD(&block->binding_list);
	block->mlxsw_sp = mlxsw_sp;
	block->net = net;
	return block;
}

void mlxsw_sp_flow_block_destroy(struct mlxsw_sp_flow_block *block)
{
	WARN_ON(!list_empty(&block->binding_list));
	kfree(block);
}

static struct mlxsw_sp_flow_block_binding *
mlxsw_sp_flow_block_lookup(struct mlxsw_sp_flow_block *block,
			   struct mlxsw_sp_port *mlxsw_sp_port, bool ingress)
{
	struct mlxsw_sp_flow_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		if (binding->mlxsw_sp_port == mlxsw_sp_port &&
		    binding->ingress == ingress)
			return binding;
	return NULL;
}

static bool
mlxsw_sp_flow_block_ruleset_bound(const struct mlxsw_sp_flow_block *block)
{
	return block->ruleset_zero;
}

int mlxsw_sp_flow_block_bind(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_flow_block *block,
			     struct mlxsw_sp_port *mlxsw_sp_port,
			     bool ingress,
			     struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_flow_block_binding *binding;
	int err;

	if (WARN_ON(mlxsw_sp_flow_block_lookup(block, mlxsw_sp_port, ingress)))
		return -EEXIST;

	if (ingress && block->ingress_blocker_rule_count) {
		NL_SET_ERR_MSG_MOD(extack, "Block cannot be bound to ingress because it contains unsupported rules");
		return -EOPNOTSUPP;
	}

	if (!ingress && block->egress_blocker_rule_count) {
		NL_SET_ERR_MSG_MOD(extack, "Block cannot be bound to egress because it contains unsupported rules");
		return -EOPNOTSUPP;
	}

	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding)
		return -ENOMEM;
	binding->mlxsw_sp_port = mlxsw_sp_port;
	binding->ingress = ingress;

	if (mlxsw_sp_flow_block_ruleset_bound(block)) {
		err = mlxsw_sp_acl_ruleset_bind(mlxsw_sp, block, binding);
		if (err)
			goto err_ruleset_bind;
	}

	if (ingress)
		block->ingress_binding_count++;
	else
		block->egress_binding_count++;
	list_add(&binding->list, &block->binding_list);
	return 0;

err_ruleset_bind:
	kfree(binding);
	return err;
}

int mlxsw_sp_flow_block_unbind(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_flow_block *block,
			       struct mlxsw_sp_port *mlxsw_sp_port,
			       bool ingress)
{
	struct mlxsw_sp_flow_block_binding *binding;

	binding = mlxsw_sp_flow_block_lookup(block, mlxsw_sp_port, ingress);
	if (!binding)
		return -ENOENT;

	list_del(&binding->list);

	if (ingress)
		block->ingress_binding_count--;
	else
		block->egress_binding_count--;

	if (mlxsw_sp_flow_block_ruleset_bound(block))
		mlxsw_sp_acl_ruleset_unbind(mlxsw_sp, block, binding);

	kfree(binding);
	return 0;
}
