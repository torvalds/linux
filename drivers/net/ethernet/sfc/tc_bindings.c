// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2022 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "tc_bindings.h"
#include "tc.h"

struct efx_tc_block_binding {
	struct list_head list;
	struct efx_nic *efx;
	struct efx_rep *efv;
	struct net_device *otherdev; /* may actually be us */
	struct flow_block *block;
};

static struct efx_tc_block_binding *efx_tc_find_binding(struct efx_nic *efx,
							struct net_device *otherdev)
{
	struct efx_tc_block_binding *binding;

	ASSERT_RTNL();
	list_for_each_entry(binding, &efx->tc->block_list, list)
		if (binding->otherdev == otherdev)
			return binding;
	return NULL;
}

static int efx_tc_block_cb(enum tc_setup_type type, void *type_data,
			   void *cb_priv)
{
	struct efx_tc_block_binding *binding = cb_priv;
	struct flow_cls_offload *tcf = type_data;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return efx_tc_flower(binding->efx, binding->otherdev,
				     tcf, binding->efv);
	default:
		return -EOPNOTSUPP;
	}
}

void efx_tc_block_unbind(void *cb_priv)
{
	struct efx_tc_block_binding *binding = cb_priv;

	list_del(&binding->list);
	kfree(binding);
}

static struct efx_tc_block_binding *efx_tc_create_binding(
			struct efx_nic *efx, struct efx_rep *efv,
			struct net_device *otherdev, struct flow_block *block)
{
	struct efx_tc_block_binding *binding = kmalloc(sizeof(*binding), GFP_KERNEL);

	if (!binding)
		return ERR_PTR(-ENOMEM);
	binding->efx = efx;
	binding->efv = efv;
	binding->otherdev = otherdev;
	binding->block = block;
	list_add(&binding->list, &efx->tc->block_list);
	return binding;
}

int efx_tc_setup_block(struct net_device *net_dev, struct efx_nic *efx,
		       struct flow_block_offload *tcb, struct efx_rep *efv)
{
	struct efx_tc_block_binding *binding;
	struct flow_block_cb *block_cb;
	int rc;

	if (tcb->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	if (WARN_ON(!efx->tc))
		return -ENETDOWN;

	switch (tcb->command) {
	case FLOW_BLOCK_BIND:
		binding = efx_tc_create_binding(efx, efv, net_dev, tcb->block);
		if (IS_ERR(binding))
			return PTR_ERR(binding);
		block_cb = flow_block_cb_alloc(efx_tc_block_cb, binding,
					       binding, efx_tc_block_unbind);
		rc = PTR_ERR_OR_ZERO(block_cb);
		netif_dbg(efx, drv, efx->net_dev,
			  "bind %sdirect block for device %s, rc %d\n",
			  net_dev == efx->net_dev ? "" :
			  efv ? "semi" : "in",
			  net_dev ? net_dev->name : NULL, rc);
		if (rc) {
			list_del(&binding->list);
			kfree(binding);
		} else {
			flow_block_cb_add(block_cb, tcb);
		}
		return rc;
	case FLOW_BLOCK_UNBIND:
		binding = efx_tc_find_binding(efx, net_dev);
		if (binding) {
			block_cb = flow_block_cb_lookup(tcb->block,
							efx_tc_block_cb,
							binding);
			if (block_cb) {
				flow_block_cb_remove(block_cb, tcb);
				netif_dbg(efx, drv, efx->net_dev,
					  "unbound %sdirect block for device %s\n",
					  net_dev == efx->net_dev ? "" :
					  binding->efv ? "semi" : "in",
					  net_dev ? net_dev->name : NULL);
				return 0;
			}
		}
		/* If we're in driver teardown, then we expect to have
		 * already unbound all our blocks (we did it early while
		 * we still had MCDI to remove the filters), so getting
		 * unbind callbacks now isn't a problem.
		 */
		netif_cond_dbg(efx, drv, efx->net_dev,
			       !efx->tc->up, warn,
			       "%sdirect block unbind for device %s, was never bound\n",
			       net_dev == efx->net_dev ? "" : "in",
			       net_dev ? net_dev->name : NULL);
		return -ENOENT;
	default:
		return -EOPNOTSUPP;
	}
}

int efx_tc_indr_setup_cb(struct net_device *net_dev, struct Qdisc *sch,
			 void *cb_priv, enum tc_setup_type type,
			 void *type_data, void *data,
			 void (*cleanup)(struct flow_block_cb *block_cb))
{
	struct flow_block_offload *tcb = type_data;
	struct efx_tc_block_binding *binding;
	struct flow_block_cb *block_cb;
	struct efx_nic *efx = cb_priv;
	bool is_ovs_int_port;
	int rc;

	if (!net_dev)
		return -EOPNOTSUPP;

	if (tcb->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS &&
	    tcb->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		return -EOPNOTSUPP;

	is_ovs_int_port = netif_is_ovs_master(net_dev);
	if (tcb->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS &&
	    !is_ovs_int_port)
		return -EOPNOTSUPP;

	if (is_ovs_int_port)
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_BLOCK:
		switch (tcb->command) {
		case FLOW_BLOCK_BIND:
			binding = efx_tc_create_binding(efx, NULL, net_dev, tcb->block);
			if (IS_ERR(binding))
				return PTR_ERR(binding);
			block_cb = flow_indr_block_cb_alloc(efx_tc_block_cb, binding,
							    binding, efx_tc_block_unbind,
							    tcb, net_dev, sch, data, binding,
							    cleanup);
			rc = PTR_ERR_OR_ZERO(block_cb);
			netif_dbg(efx, drv, efx->net_dev,
				  "bind indr block for device %s, rc %d\n",
				  net_dev ? net_dev->name : NULL, rc);
			if (rc) {
				list_del(&binding->list);
				kfree(binding);
			} else {
				flow_block_cb_add(block_cb, tcb);
			}
			return rc;
		case FLOW_BLOCK_UNBIND:
			binding = efx_tc_find_binding(efx, net_dev);
			if (!binding)
				return -ENOENT;
			block_cb = flow_block_cb_lookup(tcb->block,
							efx_tc_block_cb,
							binding);
			if (!block_cb)
				return -ENOENT;
			flow_indr_block_cb_remove(block_cb, tcb);
			netif_dbg(efx, drv, efx->net_dev,
				  "unbind indr block for device %s\n",
				  net_dev ? net_dev->name : NULL);
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

/* .ndo_setup_tc implementation
 * Entry point for flower block and filter management.
 */
int efx_tc_setup(struct net_device *net_dev, enum tc_setup_type type,
		 void *type_data)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	if (efx->type->is_vf)
		return -EOPNOTSUPP;
	if (!efx->tc)
		return -EOPNOTSUPP;

	if (type == TC_SETUP_CLSFLOWER)
		return efx_tc_flower(efx, net_dev, type_data, NULL);
	if (type == TC_SETUP_BLOCK)
		return efx_tc_setup_block(net_dev, efx, type_data, NULL);

	return -EOPNOTSUPP;
}
