// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/red.h>

#include "../nfpcore/nfp_cpp.h"
#include "../nfp_app.h"
#include "../nfp_port.h"
#include "main.h"

static int
__nfp_abm_reset_root(struct net_device *netdev, struct nfp_abm_link *alink,
		     u32 handle, unsigned int qs, u32 init_val)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	int ret;

	ret = nfp_abm_ctrl_set_all_q_lvls(alink, init_val);
	memset(alink->qdiscs, 0, sizeof(*alink->qdiscs) * alink->num_qdiscs);

	alink->parent = handle;
	alink->num_qdiscs = qs;
	port->tc_offload_cnt = qs;

	return ret;
}

static void
nfp_abm_reset_root(struct net_device *netdev, struct nfp_abm_link *alink,
		   u32 handle, unsigned int qs)
{
	__nfp_abm_reset_root(netdev, alink, handle, qs, NFP_ABM_LVL_INFINITY);
}

static int
nfp_abm_red_find(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	unsigned int i = TC_H_MIN(opt->parent) - 1;

	if (opt->parent == TC_H_ROOT)
		i = 0;
	else if (TC_H_MAJ(alink->parent) == TC_H_MAJ(opt->parent))
		i = TC_H_MIN(opt->parent) - 1;
	else
		return -EOPNOTSUPP;

	if (i >= alink->num_qdiscs || opt->handle != alink->qdiscs[i].handle)
		return -EOPNOTSUPP;

	return i;
}

static void
nfp_abm_red_destroy(struct net_device *netdev, struct nfp_abm_link *alink,
		    u32 handle)
{
	unsigned int i;

	for (i = 0; i < alink->num_qdiscs; i++)
		if (handle == alink->qdiscs[i].handle)
			break;
	if (i == alink->num_qdiscs)
		return;

	if (alink->parent == TC_H_ROOT) {
		nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 0);
	} else {
		nfp_abm_ctrl_set_q_lvl(alink, i, NFP_ABM_LVL_INFINITY);
		memset(&alink->qdiscs[i], 0, sizeof(*alink->qdiscs));
	}
}

static bool
nfp_abm_red_check_params(struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;

	if (!opt->set.is_ecn) {
		nfp_warn(cpp, "RED offload failed - drop is not supported (ECN option required) (p:%08x h:%08x)\n",
			 opt->parent, opt->handle);
		return false;
	}
	if (opt->set.is_harddrop) {
		nfp_warn(cpp, "RED offload failed - harddrop is not supported (p:%08x h:%08x)\n",
			 opt->parent, opt->handle);
		return false;
	}
	if (opt->set.min != opt->set.max) {
		nfp_warn(cpp, "RED offload failed - unsupported min/max parameters (p:%08x h:%08x)\n",
			 opt->parent, opt->handle);
		return false;
	}
	if (opt->set.min > NFP_ABM_LVL_INFINITY) {
		nfp_warn(cpp, "RED offload failed - threshold too large %d > %d (p:%08x h:%08x)\n",
			 opt->set.min, NFP_ABM_LVL_INFINITY, opt->parent,
			 opt->handle);
		return false;
	}

	return true;
}

static int
nfp_abm_red_replace(struct net_device *netdev, struct nfp_abm_link *alink,
		    struct tc_red_qopt_offload *opt)
{
	bool existing;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	existing = i >= 0;

	if (!nfp_abm_red_check_params(alink, opt)) {
		err = -EINVAL;
		goto err_destroy;
	}

	if (existing) {
		if (alink->parent == TC_H_ROOT)
			err = nfp_abm_ctrl_set_all_q_lvls(alink, opt->set.min);
		else
			err = nfp_abm_ctrl_set_q_lvl(alink, i, opt->set.min);
		if (err)
			goto err_destroy;
		return 0;
	}

	if (opt->parent == TC_H_ROOT) {
		i = 0;
		err = __nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 1,
					   opt->set.min);
	} else if (TC_H_MAJ(alink->parent) == TC_H_MAJ(opt->parent)) {
		i = TC_H_MIN(opt->parent) - 1;
		err = nfp_abm_ctrl_set_q_lvl(alink, i, opt->set.min);
	} else {
		return -EINVAL;
	}
	/* Set the handle to try full clean up, in case IO failed */
	alink->qdiscs[i].handle = opt->handle;
	if (err)
		goto err_destroy;

	if (opt->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_stats(alink, &alink->qdiscs[i].stats);
	else
		err = nfp_abm_ctrl_read_q_stats(alink, i,
						&alink->qdiscs[i].stats);
	if (err)
		goto err_destroy;

	if (opt->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_xstats(alink,
					       &alink->qdiscs[i].xstats);
	else
		err = nfp_abm_ctrl_read_q_xstats(alink, i,
						 &alink->qdiscs[i].xstats);
	if (err)
		goto err_destroy;

	alink->qdiscs[i].stats.backlog_pkts = 0;
	alink->qdiscs[i].stats.backlog_bytes = 0;

	return 0;
err_destroy:
	/* If the qdisc keeps on living, but we can't offload undo changes */
	if (existing) {
		opt->set.qstats->qlen -= alink->qdiscs[i].stats.backlog_pkts;
		opt->set.qstats->backlog -=
			alink->qdiscs[i].stats.backlog_bytes;
	}
	nfp_abm_red_destroy(netdev, alink, opt->handle);

	return err;
}

static void
nfp_abm_update_stats(struct nfp_alink_stats *new, struct nfp_alink_stats *old,
		     struct tc_qopt_offload_stats *stats)
{
	_bstats_update(stats->bstats, new->tx_bytes - old->tx_bytes,
		       new->tx_pkts - old->tx_pkts);
	stats->qstats->qlen += new->backlog_pkts - old->backlog_pkts;
	stats->qstats->backlog += new->backlog_bytes - old->backlog_bytes;
	stats->qstats->overlimits += new->overlimits - old->overlimits;
	stats->qstats->drops += new->drops - old->drops;
}

static int
nfp_abm_red_stats(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	struct nfp_alink_stats *prev_stats;
	struct nfp_alink_stats stats;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	if (i < 0)
		return i;
	prev_stats = &alink->qdiscs[i].stats;

	if (alink->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_stats(alink, &stats);
	else
		err = nfp_abm_ctrl_read_q_stats(alink, i, &stats);
	if (err)
		return err;

	nfp_abm_update_stats(&stats, prev_stats, &opt->stats);

	*prev_stats = stats;

	return 0;
}

static int
nfp_abm_red_xstats(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	struct nfp_alink_xstats *prev_xstats;
	struct nfp_alink_xstats xstats;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	if (i < 0)
		return i;
	prev_xstats = &alink->qdiscs[i].xstats;

	if (alink->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_xstats(alink, &xstats);
	else
		err = nfp_abm_ctrl_read_q_xstats(alink, i, &xstats);
	if (err)
		return err;

	opt->xstats->forced_mark += xstats.ecn_marked - prev_xstats->ecn_marked;
	opt->xstats->pdrop += xstats.pdrop - prev_xstats->pdrop;

	*prev_xstats = xstats;

	return 0;
}

int nfp_abm_setup_tc_red(struct net_device *netdev, struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_RED_REPLACE:
		return nfp_abm_red_replace(netdev, alink, opt);
	case TC_RED_DESTROY:
		nfp_abm_red_destroy(netdev, alink, opt->handle);
		return 0;
	case TC_RED_STATS:
		return nfp_abm_red_stats(alink, opt);
	case TC_RED_XSTATS:
		return nfp_abm_red_xstats(alink, opt);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nfp_abm_mq_stats(struct nfp_abm_link *alink, struct tc_mq_qopt_offload *opt)
{
	struct nfp_alink_stats stats;
	unsigned int i;
	int err;

	for (i = 0; i < alink->num_qdiscs; i++) {
		if (alink->qdiscs[i].handle == TC_H_UNSPEC)
			continue;

		err = nfp_abm_ctrl_read_q_stats(alink, i, &stats);
		if (err)
			return err;

		nfp_abm_update_stats(&stats, &alink->qdiscs[i].stats,
				     &opt->stats);
	}

	return 0;
}

int nfp_abm_setup_tc_mq(struct net_device *netdev, struct nfp_abm_link *alink,
			struct tc_mq_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_MQ_CREATE:
		nfp_abm_reset_root(netdev, alink, opt->handle,
				   alink->total_queues);
		return 0;
	case TC_MQ_DESTROY:
		if (opt->handle == alink->parent)
			nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 0);
		return 0;
	case TC_MQ_STATS:
		return nfp_abm_mq_stats(alink, opt);
	default:
		return -EOPNOTSUPP;
	}
}
