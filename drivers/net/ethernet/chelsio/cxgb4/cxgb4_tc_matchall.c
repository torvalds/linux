// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#include "cxgb4.h"
#include "cxgb4_tc_matchall.h"
#include "sched.h"

static int cxgb4_matchall_egress_validate(struct net_device *dev,
					  struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action *actions = &cls->rule->action;
	struct port_info *pi = netdev2pinfo(dev);
	struct flow_action_entry *entry;
	u64 max_link_rate;
	u32 i, speed;
	int ret;

	if (!flow_action_has_entries(actions)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Egress MATCHALL offload needs at least 1 policing action");
		return -EINVAL;
	} else if (!flow_offload_has_one_action(actions)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Egress MATCHALL offload only supports 1 policing action");
		return -EINVAL;
	} else if (pi->tc_block_shared) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Egress MATCHALL offload not supported with shared blocks");
		return -EINVAL;
	}

	ret = t4_get_link_params(pi, NULL, &speed, NULL);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to get max speed supported by the link");
		return -EINVAL;
	}

	/* Convert from Mbps to bps */
	max_link_rate = (u64)speed * 1000 * 1000;

	flow_action_for_each(i, entry, actions) {
		switch (entry->id) {
		case FLOW_ACTION_POLICE:
			/* Convert bytes per second to bits per second */
			if (entry->police.rate_bytes_ps * 8 > max_link_rate) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Specified policing max rate is larger than underlying link speed");
				return -ERANGE;
			}
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack,
					   "Only policing action supported with Egress MATCHALL offload");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int cxgb4_matchall_alloc_tc(struct net_device *dev,
				   struct tc_cls_matchall_offload *cls)
{
	struct ch_sched_params p = {
		.type = SCHED_CLASS_TYPE_PACKET,
		.u.params.level = SCHED_CLASS_LEVEL_CH_RL,
		.u.params.mode = SCHED_CLASS_MODE_CLASS,
		.u.params.rateunit = SCHED_CLASS_RATEUNIT_BITS,
		.u.params.ratemode = SCHED_CLASS_RATEMODE_ABS,
		.u.params.class = SCHED_CLS_NONE,
		.u.params.minrate = 0,
		.u.params.weight = 0,
		.u.params.pktsize = dev->mtu,
	};
	struct netlink_ext_ack *extack = cls->common.extack;
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct flow_action_entry *entry;
	struct sched_class *e;
	u32 i;

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];

	flow_action_for_each(i, entry, &cls->rule->action)
		if (entry->id == FLOW_ACTION_POLICE)
			break;

	/* Convert from bytes per second to Kbps */
	p.u.params.maxrate = div_u64(entry->police.rate_bytes_ps * 8, 1000);
	p.u.params.channel = pi->tx_chan;
	e = cxgb4_sched_class_alloc(dev, &p);
	if (!e) {
		NL_SET_ERR_MSG_MOD(extack,
				   "No free traffic class available for policing action");
		return -ENOMEM;
	}

	tc_port_matchall->egress.hwtc = e->idx;
	tc_port_matchall->egress.cookie = cls->cookie;
	tc_port_matchall->egress.state = CXGB4_MATCHALL_STATE_ENABLED;
	return 0;
}

static void cxgb4_matchall_free_tc(struct net_device *dev)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	cxgb4_sched_class_free(dev, tc_port_matchall->egress.hwtc);

	tc_port_matchall->egress.hwtc = SCHED_CLS_NONE;
	tc_port_matchall->egress.cookie = 0;
	tc_port_matchall->egress.state = CXGB4_MATCHALL_STATE_DISABLED;
}

int cxgb4_tc_matchall_replace(struct net_device *dev,
			      struct tc_cls_matchall_offload *cls_matchall)
{
	struct netlink_ext_ack *extack = cls_matchall->common.extack;
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	int ret;

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (tc_port_matchall->egress.state == CXGB4_MATCHALL_STATE_ENABLED) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only 1 Egress MATCHALL can be offloaded");
		return -ENOMEM;
	}

	ret = cxgb4_matchall_egress_validate(dev, cls_matchall);
	if (ret)
		return ret;

	return cxgb4_matchall_alloc_tc(dev, cls_matchall);
}

int cxgb4_tc_matchall_destroy(struct net_device *dev,
			      struct tc_cls_matchall_offload *cls_matchall)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (cls_matchall->cookie != tc_port_matchall->egress.cookie)
		return -ENOENT;

	cxgb4_matchall_free_tc(dev);
	return 0;
}

static void cxgb4_matchall_disable_offload(struct net_device *dev)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (tc_port_matchall->egress.state == CXGB4_MATCHALL_STATE_ENABLED)
		cxgb4_matchall_free_tc(dev);
}

int cxgb4_init_tc_matchall(struct adapter *adap)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct cxgb4_tc_matchall *tc_matchall;
	int ret;

	tc_matchall = kzalloc(sizeof(*tc_matchall), GFP_KERNEL);
	if (!tc_matchall)
		return -ENOMEM;

	tc_port_matchall = kcalloc(adap->params.nports,
				   sizeof(*tc_port_matchall),
				   GFP_KERNEL);
	if (!tc_port_matchall) {
		ret = -ENOMEM;
		goto out_free_matchall;
	}

	tc_matchall->port_matchall = tc_port_matchall;
	adap->tc_matchall = tc_matchall;
	return 0;

out_free_matchall:
	kfree(tc_matchall);
	return ret;
}

void cxgb4_cleanup_tc_matchall(struct adapter *adap)
{
	u8 i;

	if (adap->tc_matchall) {
		if (adap->tc_matchall->port_matchall) {
			for (i = 0; i < adap->params.nports; i++) {
				struct net_device *dev = adap->port[i];

				if (dev)
					cxgb4_matchall_disable_offload(dev);
			}
			kfree(adap->tc_matchall->port_matchall);
		}
		kfree(adap->tc_matchall);
	}
}
