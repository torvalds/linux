// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2020 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/flow_offload.h>

#include "spectrum.h"
#include "spectrum_span.h"
#include "reg.h"

enum mlxsw_sp_mall_action_type {
	MLXSW_SP_MALL_ACTION_TYPE_MIRROR,
	MLXSW_SP_MALL_ACTION_TYPE_SAMPLE,
};

struct mlxsw_sp_mall_mirror_entry {
	const struct net_device *to_dev;
	int span_id;
	bool ingress;
};

struct mlxsw_sp_mall_entry {
	struct list_head list;
	unsigned long cookie;
	enum mlxsw_sp_mall_action_type type;
	union {
		struct mlxsw_sp_mall_mirror_entry mirror;
	};
};

static struct mlxsw_sp_mall_entry *
mlxsw_sp_mall_entry_find(struct mlxsw_sp_port *port, unsigned long cookie)
{
	struct mlxsw_sp_mall_entry *mall_entry;

	list_for_each_entry(mall_entry, &port->mall_list, list)
		if (mall_entry->cookie == cookie)
			return mall_entry;

	return NULL;
}

static int
mlxsw_sp_mall_port_mirror_add(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_mall_entry *mall_entry,
			      bool ingress)
{
	enum mlxsw_sp_span_type span_type;

	if (!mall_entry->mirror.to_dev) {
		netdev_err(mlxsw_sp_port->dev, "Could not find requested device\n");
		return -EINVAL;
	}

	mall_entry->mirror.ingress = ingress;
	span_type = mall_entry->mirror.ingress ? MLXSW_SP_SPAN_INGRESS :
						 MLXSW_SP_SPAN_EGRESS;
	return mlxsw_sp_span_mirror_add(mlxsw_sp_port,
					mall_entry->mirror.to_dev,
					span_type, true,
					&mall_entry->mirror.span_id);
}

static void
mlxsw_sp_mall_port_mirror_del(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_mall_entry *mall_entry)
{
	enum mlxsw_sp_span_type span_type;

	span_type = mall_entry->mirror.ingress ? MLXSW_SP_SPAN_INGRESS :
						 MLXSW_SP_SPAN_EGRESS;
	mlxsw_sp_span_mirror_del(mlxsw_sp_port, mall_entry->mirror.span_id,
				 span_type, true);
}

static int mlxsw_sp_mall_port_sample_set(struct mlxsw_sp_port *mlxsw_sp_port,
					 bool enable, u32 rate)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char mpsc_pl[MLXSW_REG_MPSC_LEN];

	mlxsw_reg_mpsc_pack(mpsc_pl, mlxsw_sp_port->local_port, enable, rate);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpsc), mpsc_pl);
}

static int
mlxsw_sp_mall_port_sample_add(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct tc_cls_matchall_offload *cls,
			      const struct flow_action_entry *act, bool ingress)
{
	int err;

	if (!mlxsw_sp_port->sample)
		return -EOPNOTSUPP;
	if (rtnl_dereference(mlxsw_sp_port->sample->psample_group)) {
		netdev_err(mlxsw_sp_port->dev, "sample already active\n");
		return -EEXIST;
	}
	if (act->sample.rate > MLXSW_REG_MPSC_RATE_MAX) {
		netdev_err(mlxsw_sp_port->dev, "sample rate not supported\n");
		return -EOPNOTSUPP;
	}

	rcu_assign_pointer(mlxsw_sp_port->sample->psample_group,
			   act->sample.psample_group);
	mlxsw_sp_port->sample->truncate = act->sample.truncate;
	mlxsw_sp_port->sample->trunc_size = act->sample.trunc_size;
	mlxsw_sp_port->sample->rate = act->sample.rate;

	err = mlxsw_sp_mall_port_sample_set(mlxsw_sp_port, true,
					    act->sample.rate);
	if (err)
		goto err_port_sample_set;
	return 0;

err_port_sample_set:
	RCU_INIT_POINTER(mlxsw_sp_port->sample->psample_group, NULL);
	return err;
}

static void
mlxsw_sp_mall_port_sample_del(struct mlxsw_sp_port *mlxsw_sp_port)
{
	if (!mlxsw_sp_port->sample)
		return;

	mlxsw_sp_mall_port_sample_set(mlxsw_sp_port, false, 1);
	RCU_INIT_POINTER(mlxsw_sp_port->sample->psample_group, NULL);
}

int mlxsw_sp_mall_replace(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_cls_matchall_offload *f, bool ingress)
{
	struct mlxsw_sp_mall_entry *mall_entry;
	__be16 protocol = f->common.protocol;
	struct flow_action_entry *act;
	int err;

	if (!flow_offload_has_one_action(&f->rule->action)) {
		netdev_err(mlxsw_sp_port->dev, "only singular actions are supported\n");
		return -EOPNOTSUPP;
	}

	mall_entry = kzalloc(sizeof(*mall_entry), GFP_KERNEL);
	if (!mall_entry)
		return -ENOMEM;
	mall_entry->cookie = f->cookie;

	act = &f->rule->action.entries[0];

	if (act->id == FLOW_ACTION_MIRRED && protocol == htons(ETH_P_ALL)) {
		mall_entry->type = MLXSW_SP_MALL_ACTION_TYPE_MIRROR;
		mall_entry->mirror.to_dev = act->dev;
		err = mlxsw_sp_mall_port_mirror_add(mlxsw_sp_port, mall_entry,
						    ingress);
	} else if (act->id == FLOW_ACTION_SAMPLE &&
		   protocol == htons(ETH_P_ALL)) {
		mall_entry->type = MLXSW_SP_MALL_ACTION_TYPE_SAMPLE;
		err = mlxsw_sp_mall_port_sample_add(mlxsw_sp_port, f, act,
						    ingress);
	} else {
		err = -EOPNOTSUPP;
	}

	if (err)
		goto err_add_action;

	list_add_tail(&mall_entry->list, &mlxsw_sp_port->mall_list);
	return 0;

err_add_action:
	kfree(mall_entry);
	return err;
}

void mlxsw_sp_mall_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct tc_cls_matchall_offload *f)
{
	struct mlxsw_sp_mall_entry *mall_entry;

	mall_entry = mlxsw_sp_mall_entry_find(mlxsw_sp_port, f->cookie);
	if (!mall_entry) {
		netdev_dbg(mlxsw_sp_port->dev, "tc entry not found on port\n");
		return;
	}
	list_del(&mall_entry->list);

	switch (mall_entry->type) {
	case MLXSW_SP_MALL_ACTION_TYPE_MIRROR:
		mlxsw_sp_mall_port_mirror_del(mlxsw_sp_port, mall_entry);
		break;
	case MLXSW_SP_MALL_ACTION_TYPE_SAMPLE:
		mlxsw_sp_mall_port_sample_del(mlxsw_sp_port);
		break;
	default:
		WARN_ON(1);
	}

	kfree(mall_entry);
}
