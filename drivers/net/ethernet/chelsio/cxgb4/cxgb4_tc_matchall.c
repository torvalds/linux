// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#include "cxgb4.h"
#include "cxgb4_tc_matchall.h"
#include "sched.h"
#include "cxgb4_uld.h"
#include "cxgb4_filter.h"
#include "cxgb4_tc_flower.h"

static int cxgb4_matchall_egress_validate(struct net_device *dev,
					  struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action *actions = &cls->rule->action;
	struct port_info *pi = netdev2pinfo(dev);
	struct flow_action_entry *entry;
	struct ch_sched_queue qe;
	struct sched_class *e;
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

	for (i = 0; i < pi->nqsets; i++) {
		memset(&qe, 0, sizeof(qe));
		qe.queue = i;

		e = cxgb4_sched_queue_lookup(dev, &qe);
		if (e && e->info.u.params.level != SCHED_CLASS_LEVEL_CH_RL) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Some queues are already bound to different class");
			return -EBUSY;
		}
	}

	return 0;
}

static int cxgb4_matchall_tc_bind_queues(struct net_device *dev, u32 tc)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct ch_sched_queue qe;
	int ret;
	u32 i;

	for (i = 0; i < pi->nqsets; i++) {
		qe.queue = i;
		qe.class = tc;
		ret = cxgb4_sched_class_bind(dev, &qe, SCHED_QUEUE);
		if (ret)
			goto out_free;
	}

	return 0;

out_free:
	while (i--) {
		qe.queue = i;
		qe.class = SCHED_CLS_NONE;
		cxgb4_sched_class_unbind(dev, &qe, SCHED_QUEUE);
	}

	return ret;
}

static void cxgb4_matchall_tc_unbind_queues(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct ch_sched_queue qe;
	u32 i;

	for (i = 0; i < pi->nqsets; i++) {
		qe.queue = i;
		qe.class = SCHED_CLS_NONE;
		cxgb4_sched_class_unbind(dev, &qe, SCHED_QUEUE);
	}
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
	int ret;
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

	ret = cxgb4_matchall_tc_bind_queues(dev, e->idx);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Could not bind queues to traffic class");
		goto out_free;
	}

	tc_port_matchall->egress.hwtc = e->idx;
	tc_port_matchall->egress.cookie = cls->cookie;
	tc_port_matchall->egress.state = CXGB4_MATCHALL_STATE_ENABLED;
	return 0;

out_free:
	cxgb4_sched_class_free(dev, e->idx);
	return ret;
}

static void cxgb4_matchall_free_tc(struct net_device *dev)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	cxgb4_matchall_tc_unbind_queues(dev);
	cxgb4_sched_class_free(dev, tc_port_matchall->egress.hwtc);

	tc_port_matchall->egress.hwtc = SCHED_CLS_NONE;
	tc_port_matchall->egress.cookie = 0;
	tc_port_matchall->egress.state = CXGB4_MATCHALL_STATE_DISABLED;
}

static int cxgb4_matchall_alloc_filter(struct net_device *dev,
				       struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct ch_filter_specification *fs;
	int ret, fidx;

	/* Note that TC uses prio 0 to indicate stack to generate
	 * automatic prio and hence doesn't pass prio 0 to driver.
	 * However, the hardware TCAM index starts from 0. Hence, the
	 * -1 here. 1 slot is enough to create a wildcard matchall
	 * VIID rule.
	 */
	if (cls->common.prio <= (adap->tids.nftids + adap->tids.nhpftids))
		fidx = cls->common.prio - 1;
	else
		fidx = cxgb4_get_free_ftid(dev, PF_INET);

	/* Only insert MATCHALL rule if its priority doesn't conflict
	 * with existing rules in the LETCAM.
	 */
	if (fidx < 0 ||
	    !cxgb4_filter_prio_in_range(dev, fidx, cls->common.prio)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "No free LETCAM index available");
		return -ENOMEM;
	}

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	fs = &tc_port_matchall->ingress.fs;
	memset(fs, 0, sizeof(*fs));

	if (fidx < adap->tids.nhpftids)
		fs->prio = 1;
	fs->tc_prio = cls->common.prio;
	fs->tc_cookie = cls->cookie;
	fs->hitcnts = 1;

	fs->val.pfvf_vld = 1;
	fs->val.pf = adap->pf;
	fs->val.vf = pi->vin;

	cxgb4_process_flow_actions(dev, &cls->rule->action, fs);

	ret = cxgb4_set_filter(dev, fidx, fs);
	if (ret)
		return ret;

	tc_port_matchall->ingress.tid = fidx;
	tc_port_matchall->ingress.state = CXGB4_MATCHALL_STATE_ENABLED;
	return 0;
}

static int cxgb4_matchall_free_filter(struct net_device *dev)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	int ret;

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];

	ret = cxgb4_del_filter(dev, tc_port_matchall->ingress.tid,
			       &tc_port_matchall->ingress.fs);
	if (ret)
		return ret;

	tc_port_matchall->ingress.packets = 0;
	tc_port_matchall->ingress.bytes = 0;
	tc_port_matchall->ingress.last_used = 0;
	tc_port_matchall->ingress.tid = 0;
	tc_port_matchall->ingress.state = CXGB4_MATCHALL_STATE_DISABLED;
	return 0;
}

int cxgb4_tc_matchall_replace(struct net_device *dev,
			      struct tc_cls_matchall_offload *cls_matchall,
			      bool ingress)
{
	struct netlink_ext_ack *extack = cls_matchall->common.extack;
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	int ret;

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (ingress) {
		if (tc_port_matchall->ingress.state ==
		    CXGB4_MATCHALL_STATE_ENABLED) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only 1 Ingress MATCHALL can be offloaded");
			return -ENOMEM;
		}

		ret = cxgb4_validate_flow_actions(dev,
						  &cls_matchall->rule->action);
		if (ret)
			return ret;

		return cxgb4_matchall_alloc_filter(dev, cls_matchall);
	}

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
			      struct tc_cls_matchall_offload *cls_matchall,
			      bool ingress)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (ingress) {
		if (cls_matchall->cookie !=
		    tc_port_matchall->ingress.fs.tc_cookie)
			return -ENOENT;

		return cxgb4_matchall_free_filter(dev);
	}

	if (cls_matchall->cookie != tc_port_matchall->egress.cookie)
		return -ENOENT;

	cxgb4_matchall_free_tc(dev);
	return 0;
}

int cxgb4_tc_matchall_stats(struct net_device *dev,
			    struct tc_cls_matchall_offload *cls_matchall)
{
	struct cxgb4_tc_port_matchall *tc_port_matchall;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	u64 packets, bytes;
	int ret;

	tc_port_matchall = &adap->tc_matchall->port_matchall[pi->port_id];
	if (tc_port_matchall->ingress.state == CXGB4_MATCHALL_STATE_DISABLED)
		return -ENOENT;

	ret = cxgb4_get_filter_counters(dev, tc_port_matchall->ingress.tid,
					&packets, &bytes,
					tc_port_matchall->ingress.fs.hash);
	if (ret)
		return ret;

	if (tc_port_matchall->ingress.packets != packets) {
		flow_stats_update(&cls_matchall->stats,
				  bytes - tc_port_matchall->ingress.bytes,
				  packets - tc_port_matchall->ingress.packets,
				  tc_port_matchall->ingress.last_used);

		tc_port_matchall->ingress.packets = packets;
		tc_port_matchall->ingress.bytes = bytes;
		tc_port_matchall->ingress.last_used = jiffies;
	}

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

	if (tc_port_matchall->ingress.state == CXGB4_MATCHALL_STATE_ENABLED)
		cxgb4_matchall_free_filter(dev);
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
