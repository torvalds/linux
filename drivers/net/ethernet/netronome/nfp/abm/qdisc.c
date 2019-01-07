// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include <linux/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/red.h>

#include "../nfpcore/nfp_cpp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_port.h"
#include "main.h"

static bool nfp_abm_qdisc_is_red(struct nfp_qdisc *qdisc)
{
	return qdisc->type == NFP_QDISC_RED || qdisc->type == NFP_QDISC_GRED;
}

static bool nfp_abm_qdisc_child_valid(struct nfp_qdisc *qdisc, unsigned int id)
{
	return qdisc->children[id] &&
	       qdisc->children[id] != NFP_QDISC_UNTRACKED;
}

static void *nfp_abm_qdisc_tree_deref_slot(void __rcu **slot)
{
	return rtnl_dereference(*slot);
}

static void
nfp_abm_stats_propagate(struct nfp_alink_stats *parent,
			struct nfp_alink_stats *child)
{
	parent->tx_pkts		+= child->tx_pkts;
	parent->tx_bytes	+= child->tx_bytes;
	parent->backlog_pkts	+= child->backlog_pkts;
	parent->backlog_bytes	+= child->backlog_bytes;
	parent->overlimits	+= child->overlimits;
	parent->drops		+= child->drops;
}

static void
nfp_abm_stats_update_red(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc,
			 unsigned int queue)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	unsigned int i;
	int err;

	if (!qdisc->offloaded)
		return;

	for (i = 0; i < qdisc->red.num_bands; i++) {
		err = nfp_abm_ctrl_read_q_stats(alink, i, queue,
						&qdisc->red.band[i].stats);
		if (err)
			nfp_err(cpp, "RED stats (%d, %d) read failed with error %d\n",
				i, queue, err);

		err = nfp_abm_ctrl_read_q_xstats(alink, i, queue,
						 &qdisc->red.band[i].xstats);
		if (err)
			nfp_err(cpp, "RED xstats (%d, %d) read failed with error %d\n",
				i, queue, err);
	}
}

static void
nfp_abm_stats_update_mq(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc)
{
	unsigned int i;

	if (qdisc->type != NFP_QDISC_MQ)
		return;

	for (i = 0; i < alink->total_queues; i++)
		if (nfp_abm_qdisc_child_valid(qdisc, i))
			nfp_abm_stats_update_red(alink, qdisc->children[i], i);
}

static void __nfp_abm_stats_update(struct nfp_abm_link *alink, u64 time_now)
{
	alink->last_stats_update = time_now;
	if (alink->root_qdisc)
		nfp_abm_stats_update_mq(alink, alink->root_qdisc);
}

static void nfp_abm_stats_update(struct nfp_abm_link *alink)
{
	u64 now;

	/* Limit the frequency of updates - stats of non-leaf qdiscs are a sum
	 * of all their leafs, so we would read the same stat multiple times
	 * for every dump.
	 */
	now = ktime_get();
	if (now - alink->last_stats_update < NFP_ABM_STATS_REFRESH_IVAL)
		return;

	__nfp_abm_stats_update(alink, now);
}

static void
nfp_abm_qdisc_unlink_children(struct nfp_qdisc *qdisc,
			      unsigned int start, unsigned int end)
{
	unsigned int i;

	for (i = start; i < end; i++)
		if (nfp_abm_qdisc_child_valid(qdisc, i)) {
			qdisc->children[i]->use_cnt--;
			qdisc->children[i] = NULL;
		}
}

static void
nfp_abm_qdisc_offload_stop(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc)
{
	unsigned int i;

	/* Don't complain when qdisc is getting unlinked */
	if (qdisc->use_cnt)
		nfp_warn(alink->abm->app->cpp, "Offload of '%08x' stopped\n",
			 qdisc->handle);

	if (!nfp_abm_qdisc_is_red(qdisc))
		return;

	for (i = 0; i < qdisc->red.num_bands; i++) {
		qdisc->red.band[i].stats.backlog_pkts = 0;
		qdisc->red.band[i].stats.backlog_bytes = 0;
	}
}

static int
__nfp_abm_stats_init(struct nfp_abm_link *alink, unsigned int band,
		     unsigned int queue, struct nfp_alink_stats *prev_stats,
		     struct nfp_alink_xstats *prev_xstats)
{
	u64 backlog_pkts, backlog_bytes;
	int err;

	/* Don't touch the backlog, backlog can only be reset after it has
	 * been reported back to the tc qdisc stats.
	 */
	backlog_pkts = prev_stats->backlog_pkts;
	backlog_bytes = prev_stats->backlog_bytes;

	err = nfp_abm_ctrl_read_q_stats(alink, band, queue, prev_stats);
	if (err) {
		nfp_err(alink->abm->app->cpp,
			"RED stats init (%d, %d) failed with error %d\n",
			band, queue, err);
		return err;
	}

	err = nfp_abm_ctrl_read_q_xstats(alink, band, queue, prev_xstats);
	if (err) {
		nfp_err(alink->abm->app->cpp,
			"RED xstats init (%d, %d) failed with error %d\n",
			band, queue, err);
		return err;
	}

	prev_stats->backlog_pkts = backlog_pkts;
	prev_stats->backlog_bytes = backlog_bytes;
	return 0;
}

static int
nfp_abm_stats_init(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc,
		   unsigned int queue)
{
	unsigned int i;
	int err;

	for (i = 0; i < qdisc->red.num_bands; i++) {
		err = __nfp_abm_stats_init(alink, i, queue,
					   &qdisc->red.band[i].prev_stats,
					   &qdisc->red.band[i].prev_xstats);
		if (err)
			return err;
	}

	return 0;
}

static void
nfp_abm_offload_compile_red(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc,
			    unsigned int queue)
{
	bool good_red, good_gred;
	unsigned int i;

	good_red = qdisc->type == NFP_QDISC_RED &&
		   qdisc->params_ok &&
		   qdisc->use_cnt == 1 &&
		   !alink->has_prio &&
		   !qdisc->children[0];
	good_gred = qdisc->type == NFP_QDISC_GRED &&
		    qdisc->params_ok &&
		    qdisc->use_cnt == 1;
	qdisc->offload_mark = good_red || good_gred;

	/* If we are starting offload init prev_stats */
	if (qdisc->offload_mark && !qdisc->offloaded)
		if (nfp_abm_stats_init(alink, qdisc, queue))
			qdisc->offload_mark = false;

	if (!qdisc->offload_mark)
		return;

	for (i = 0; i < alink->abm->num_bands; i++) {
		enum nfp_abm_q_action act;

		nfp_abm_ctrl_set_q_lvl(alink, i, queue,
				       qdisc->red.band[i].threshold);
		act = qdisc->red.band[i].ecn ?
			NFP_ABM_ACT_MARK_DROP : NFP_ABM_ACT_DROP;
		nfp_abm_ctrl_set_q_act(alink, i, queue, act);
	}
}

static void
nfp_abm_offload_compile_mq(struct nfp_abm_link *alink, struct nfp_qdisc *qdisc)
{
	unsigned int i;

	qdisc->offload_mark = qdisc->type == NFP_QDISC_MQ;
	if (!qdisc->offload_mark)
		return;

	for (i = 0; i < alink->total_queues; i++) {
		struct nfp_qdisc *child = qdisc->children[i];

		if (!nfp_abm_qdisc_child_valid(qdisc, i))
			continue;

		nfp_abm_offload_compile_red(alink, child, i);
	}
}

void nfp_abm_qdisc_offload_update(struct nfp_abm_link *alink)
{
	struct nfp_abm *abm = alink->abm;
	struct radix_tree_iter iter;
	struct nfp_qdisc *qdisc;
	void __rcu **slot;
	size_t i;

	/* Mark all thresholds as unconfigured */
	for (i = 0; i < abm->num_bands; i++)
		__bitmap_set(abm->threshold_undef,
			     i * NFP_NET_MAX_RX_RINGS + alink->queue_base,
			     alink->total_queues);

	/* Clear offload marks */
	radix_tree_for_each_slot(slot, &alink->qdiscs, &iter, 0) {
		qdisc = nfp_abm_qdisc_tree_deref_slot(slot);
		qdisc->offload_mark = false;
	}

	if (alink->root_qdisc)
		nfp_abm_offload_compile_mq(alink, alink->root_qdisc);

	/* Refresh offload status */
	radix_tree_for_each_slot(slot, &alink->qdiscs, &iter, 0) {
		qdisc = nfp_abm_qdisc_tree_deref_slot(slot);
		if (!qdisc->offload_mark && qdisc->offloaded)
			nfp_abm_qdisc_offload_stop(alink, qdisc);
		qdisc->offloaded = qdisc->offload_mark;
	}

	/* Reset the unconfigured thresholds */
	for (i = 0; i < abm->num_thresholds; i++)
		if (test_bit(i, abm->threshold_undef))
			__nfp_abm_ctrl_set_q_lvl(abm, i, NFP_ABM_LVL_INFINITY);

	__nfp_abm_stats_update(alink, ktime_get());
}

static void
nfp_abm_qdisc_clear_mq(struct net_device *netdev, struct nfp_abm_link *alink,
		       struct nfp_qdisc *qdisc)
{
	struct radix_tree_iter iter;
	unsigned int mq_refs = 0;
	void __rcu **slot;

	if (!qdisc->use_cnt)
		return;
	/* MQ doesn't notify well on destruction, we need special handling of
	 * MQ's children.
	 */
	if (qdisc->type == NFP_QDISC_MQ &&
	    qdisc == alink->root_qdisc &&
	    netdev->reg_state == NETREG_UNREGISTERING)
		return;

	/* Count refs held by MQ instances and clear pointers */
	radix_tree_for_each_slot(slot, &alink->qdiscs, &iter, 0) {
		struct nfp_qdisc *mq = nfp_abm_qdisc_tree_deref_slot(slot);
		unsigned int i;

		if (mq->type != NFP_QDISC_MQ || mq->netdev != netdev)
			continue;
		for (i = 0; i < mq->num_children; i++)
			if (mq->children[i] == qdisc) {
				mq->children[i] = NULL;
				mq_refs++;
			}
	}

	WARN(qdisc->use_cnt != mq_refs, "non-zero qdisc use count: %d (- %d)\n",
	     qdisc->use_cnt, mq_refs);
}

static void
nfp_abm_qdisc_free(struct net_device *netdev, struct nfp_abm_link *alink,
		   struct nfp_qdisc *qdisc)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);

	if (!qdisc)
		return;
	nfp_abm_qdisc_clear_mq(netdev, alink, qdisc);
	WARN_ON(radix_tree_delete(&alink->qdiscs,
				  TC_H_MAJ(qdisc->handle)) != qdisc);

	kfree(qdisc->children);
	kfree(qdisc);

	port->tc_offload_cnt--;
}

static struct nfp_qdisc *
nfp_abm_qdisc_alloc(struct net_device *netdev, struct nfp_abm_link *alink,
		    enum nfp_qdisc_type type, u32 parent_handle, u32 handle,
		    unsigned int children)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	struct nfp_qdisc *qdisc;
	int err;

	qdisc = kzalloc(sizeof(*qdisc), GFP_KERNEL);
	if (!qdisc)
		return NULL;

	if (children) {
		qdisc->children = kcalloc(children, sizeof(void *), GFP_KERNEL);
		if (!qdisc->children)
			goto err_free_qdisc;
	}

	qdisc->netdev = netdev;
	qdisc->type = type;
	qdisc->parent_handle = parent_handle;
	qdisc->handle = handle;
	qdisc->num_children = children;

	err = radix_tree_insert(&alink->qdiscs, TC_H_MAJ(qdisc->handle), qdisc);
	if (err) {
		nfp_err(alink->abm->app->cpp,
			"Qdisc insertion into radix tree failed: %d\n", err);
		goto err_free_child_tbl;
	}

	port->tc_offload_cnt++;
	return qdisc;

err_free_child_tbl:
	kfree(qdisc->children);
err_free_qdisc:
	kfree(qdisc);
	return NULL;
}

static struct nfp_qdisc *
nfp_abm_qdisc_find(struct nfp_abm_link *alink, u32 handle)
{
	return radix_tree_lookup(&alink->qdiscs, TC_H_MAJ(handle));
}

static int
nfp_abm_qdisc_replace(struct net_device *netdev, struct nfp_abm_link *alink,
		      enum nfp_qdisc_type type, u32 parent_handle, u32 handle,
		      unsigned int children, struct nfp_qdisc **qdisc)
{
	*qdisc = nfp_abm_qdisc_find(alink, handle);
	if (*qdisc) {
		if (WARN_ON((*qdisc)->type != type))
			return -EINVAL;
		return 1;
	}

	*qdisc = nfp_abm_qdisc_alloc(netdev, alink, type, parent_handle, handle,
				     children);
	return *qdisc ? 0 : -ENOMEM;
}

static void
nfp_abm_qdisc_destroy(struct net_device *netdev, struct nfp_abm_link *alink,
		      u32 handle)
{
	struct nfp_qdisc *qdisc;

	qdisc = nfp_abm_qdisc_find(alink, handle);
	if (!qdisc)
		return;

	/* We don't get TC_SETUP_ROOT_QDISC w/ MQ when netdev is unregistered */
	if (alink->root_qdisc == qdisc)
		qdisc->use_cnt--;

	nfp_abm_qdisc_unlink_children(qdisc, 0, qdisc->num_children);
	nfp_abm_qdisc_free(netdev, alink, qdisc);

	if (alink->root_qdisc == qdisc) {
		alink->root_qdisc = NULL;
		/* Only root change matters, other changes are acted upon on
		 * the graft notification.
		 */
		nfp_abm_qdisc_offload_update(alink);
	}
}

static int
nfp_abm_qdisc_graft(struct nfp_abm_link *alink, u32 handle, u32 child_handle,
		    unsigned int id)
{
	struct nfp_qdisc *parent, *child;

	parent = nfp_abm_qdisc_find(alink, handle);
	if (!parent)
		return 0;

	if (WARN(id >= parent->num_children,
		 "graft child out of bound %d >= %d\n",
		 id, parent->num_children))
		return -EINVAL;

	nfp_abm_qdisc_unlink_children(parent, id, id + 1);

	child = nfp_abm_qdisc_find(alink, child_handle);
	if (child)
		child->use_cnt++;
	else
		child = NFP_QDISC_UNTRACKED;
	parent->children[id] = child;

	nfp_abm_qdisc_offload_update(alink);

	return 0;
}

static void
nfp_abm_stats_calculate(struct nfp_alink_stats *new,
			struct nfp_alink_stats *old,
			struct gnet_stats_basic_packed *bstats,
			struct gnet_stats_queue *qstats)
{
	_bstats_update(bstats, new->tx_bytes - old->tx_bytes,
		       new->tx_pkts - old->tx_pkts);
	qstats->qlen += new->backlog_pkts - old->backlog_pkts;
	qstats->backlog += new->backlog_bytes - old->backlog_bytes;
	qstats->overlimits += new->overlimits - old->overlimits;
	qstats->drops += new->drops - old->drops;
}

static void
nfp_abm_stats_red_calculate(struct nfp_alink_xstats *new,
			    struct nfp_alink_xstats *old,
			    struct red_stats *stats)
{
	stats->forced_mark += new->ecn_marked - old->ecn_marked;
	stats->pdrop += new->pdrop - old->pdrop;
}

static int
nfp_abm_gred_stats(struct nfp_abm_link *alink, u32 handle,
		   struct tc_gred_qopt_offload_stats *stats)
{
	struct nfp_qdisc *qdisc;
	unsigned int i;

	nfp_abm_stats_update(alink);

	qdisc = nfp_abm_qdisc_find(alink, handle);
	if (!qdisc)
		return -EOPNOTSUPP;
	/* If the qdisc offload has stopped we may need to adjust the backlog
	 * counters back so carry on even if qdisc is not currently offloaded.
	 */

	for (i = 0; i < qdisc->red.num_bands; i++) {
		if (!stats->xstats[i])
			continue;

		nfp_abm_stats_calculate(&qdisc->red.band[i].stats,
					&qdisc->red.band[i].prev_stats,
					&stats->bstats[i], &stats->qstats[i]);
		qdisc->red.band[i].prev_stats = qdisc->red.band[i].stats;

		nfp_abm_stats_red_calculate(&qdisc->red.band[i].xstats,
					    &qdisc->red.band[i].prev_xstats,
					    stats->xstats[i]);
		qdisc->red.band[i].prev_xstats = qdisc->red.band[i].xstats;
	}

	return qdisc->offloaded ? 0 : -EOPNOTSUPP;
}

static bool
nfp_abm_gred_check_params(struct nfp_abm_link *alink,
			  struct tc_gred_qopt_offload *opt)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	struct nfp_abm *abm = alink->abm;
	unsigned int i;

	if (opt->set.grio_on || opt->set.wred_on) {
		nfp_warn(cpp, "GRED offload failed - GRIO and WRED not supported (p:%08x h:%08x)\n",
			 opt->parent, opt->handle);
		return false;
	}
	if (opt->set.dp_def != alink->def_band) {
		nfp_warn(cpp, "GRED offload failed - default band must be %d (p:%08x h:%08x)\n",
			 alink->def_band, opt->parent, opt->handle);
		return false;
	}
	if (opt->set.dp_cnt != abm->num_bands) {
		nfp_warn(cpp, "GRED offload failed - band count must be %d (p:%08x h:%08x)\n",
			 abm->num_bands, opt->parent, opt->handle);
		return false;
	}

	for (i = 0; i < abm->num_bands; i++) {
		struct tc_gred_vq_qopt_offload_params *band = &opt->set.tab[i];

		if (!band->present)
			return false;
		if (!band->is_ecn && !nfp_abm_has_drop(abm)) {
			nfp_warn(cpp, "GRED offload failed - drop is not supported (ECN option required) (p:%08x h:%08x vq:%d)\n",
				 opt->parent, opt->handle, i);
			return false;
		}
		if (band->is_ecn && !nfp_abm_has_mark(abm)) {
			nfp_warn(cpp, "GRED offload failed - ECN marking not supported (p:%08x h:%08x vq:%d)\n",
				 opt->parent, opt->handle, i);
			return false;
		}
		if (band->is_harddrop) {
			nfp_warn(cpp, "GRED offload failed - harddrop is not supported (p:%08x h:%08x vq:%d)\n",
				 opt->parent, opt->handle, i);
			return false;
		}
		if (band->min != band->max) {
			nfp_warn(cpp, "GRED offload failed - threshold mismatch (p:%08x h:%08x vq:%d)\n",
				 opt->parent, opt->handle, i);
			return false;
		}
		if (band->min > S32_MAX) {
			nfp_warn(cpp, "GRED offload failed - threshold too large %d > %d (p:%08x h:%08x vq:%d)\n",
				 band->min, S32_MAX, opt->parent, opt->handle,
				 i);
			return false;
		}
	}

	return true;
}

static int
nfp_abm_gred_replace(struct net_device *netdev, struct nfp_abm_link *alink,
		     struct tc_gred_qopt_offload *opt)
{
	struct nfp_qdisc *qdisc;
	unsigned int i;
	int ret;

	ret = nfp_abm_qdisc_replace(netdev, alink, NFP_QDISC_GRED, opt->parent,
				    opt->handle, 0, &qdisc);
	if (ret < 0)
		return ret;

	qdisc->params_ok = nfp_abm_gred_check_params(alink, opt);
	if (qdisc->params_ok) {
		qdisc->red.num_bands = opt->set.dp_cnt;
		for (i = 0; i < qdisc->red.num_bands; i++) {
			qdisc->red.band[i].ecn = opt->set.tab[i].is_ecn;
			qdisc->red.band[i].threshold = opt->set.tab[i].min;
		}
	}

	if (qdisc->use_cnt)
		nfp_abm_qdisc_offload_update(alink);

	return 0;
}

int nfp_abm_setup_tc_gred(struct net_device *netdev, struct nfp_abm_link *alink,
			  struct tc_gred_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_GRED_REPLACE:
		return nfp_abm_gred_replace(netdev, alink, opt);
	case TC_GRED_DESTROY:
		nfp_abm_qdisc_destroy(netdev, alink, opt->handle);
		return 0;
	case TC_GRED_STATS:
		return nfp_abm_gred_stats(alink, opt->handle, &opt->stats);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nfp_abm_red_xstats(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	struct nfp_qdisc *qdisc;

	nfp_abm_stats_update(alink);

	qdisc = nfp_abm_qdisc_find(alink, opt->handle);
	if (!qdisc || !qdisc->offloaded)
		return -EOPNOTSUPP;

	nfp_abm_stats_red_calculate(&qdisc->red.band[0].xstats,
				    &qdisc->red.band[0].prev_xstats,
				    opt->xstats);
	qdisc->red.band[0].prev_xstats = qdisc->red.band[0].xstats;
	return 0;
}

static int
nfp_abm_red_stats(struct nfp_abm_link *alink, u32 handle,
		  struct tc_qopt_offload_stats *stats)
{
	struct nfp_qdisc *qdisc;

	nfp_abm_stats_update(alink);

	qdisc = nfp_abm_qdisc_find(alink, handle);
	if (!qdisc)
		return -EOPNOTSUPP;
	/* If the qdisc offload has stopped we may need to adjust the backlog
	 * counters back so carry on even if qdisc is not currently offloaded.
	 */

	nfp_abm_stats_calculate(&qdisc->red.band[0].stats,
				&qdisc->red.band[0].prev_stats,
				stats->bstats, stats->qstats);
	qdisc->red.band[0].prev_stats = qdisc->red.band[0].stats;

	return qdisc->offloaded ? 0 : -EOPNOTSUPP;
}

static bool
nfp_abm_red_check_params(struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	struct nfp_abm *abm = alink->abm;

	if (!opt->set.is_ecn && !nfp_abm_has_drop(abm)) {
		nfp_warn(cpp, "RED offload failed - drop is not supported (ECN option required) (p:%08x h:%08x)\n",
			 opt->parent, opt->handle);
		return false;
	}
	if (opt->set.is_ecn && !nfp_abm_has_mark(abm)) {
		nfp_warn(cpp, "RED offload failed - ECN marking not supported (p:%08x h:%08x)\n",
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
	struct nfp_qdisc *qdisc;
	int ret;

	ret = nfp_abm_qdisc_replace(netdev, alink, NFP_QDISC_RED, opt->parent,
				    opt->handle, 1, &qdisc);
	if (ret < 0)
		return ret;

	/* If limit != 0 child gets reset */
	if (opt->set.limit) {
		if (nfp_abm_qdisc_child_valid(qdisc, 0))
			qdisc->children[0]->use_cnt--;
		qdisc->children[0] = NULL;
	} else {
		/* Qdisc was just allocated without a limit will use noop_qdisc,
		 * i.e. a block hole.
		 */
		if (!ret)
			qdisc->children[0] = NFP_QDISC_UNTRACKED;
	}

	qdisc->params_ok = nfp_abm_red_check_params(alink, opt);
	if (qdisc->params_ok) {
		qdisc->red.num_bands = 1;
		qdisc->red.band[0].ecn = opt->set.is_ecn;
		qdisc->red.band[0].threshold = opt->set.min;
	}

	if (qdisc->use_cnt == 1)
		nfp_abm_qdisc_offload_update(alink);

	return 0;
}

int nfp_abm_setup_tc_red(struct net_device *netdev, struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_RED_REPLACE:
		return nfp_abm_red_replace(netdev, alink, opt);
	case TC_RED_DESTROY:
		nfp_abm_qdisc_destroy(netdev, alink, opt->handle);
		return 0;
	case TC_RED_STATS:
		return nfp_abm_red_stats(alink, opt->handle, &opt->stats);
	case TC_RED_XSTATS:
		return nfp_abm_red_xstats(alink, opt);
	case TC_RED_GRAFT:
		return nfp_abm_qdisc_graft(alink, opt->handle,
					   opt->child_handle, 0);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nfp_abm_mq_create(struct net_device *netdev, struct nfp_abm_link *alink,
		  struct tc_mq_qopt_offload *opt)
{
	struct nfp_qdisc *qdisc;
	int ret;

	ret = nfp_abm_qdisc_replace(netdev, alink, NFP_QDISC_MQ,
				    TC_H_ROOT, opt->handle, alink->total_queues,
				    &qdisc);
	if (ret < 0)
		return ret;

	qdisc->params_ok = true;
	qdisc->offloaded = true;
	nfp_abm_qdisc_offload_update(alink);
	return 0;
}

static int
nfp_abm_mq_stats(struct nfp_abm_link *alink, u32 handle,
		 struct tc_qopt_offload_stats *stats)
{
	struct nfp_qdisc *qdisc, *red;
	unsigned int i, j;

	qdisc = nfp_abm_qdisc_find(alink, handle);
	if (!qdisc)
		return -EOPNOTSUPP;

	nfp_abm_stats_update(alink);

	/* MQ stats are summed over the children in the core, so we need
	 * to add up the unreported child values.
	 */
	memset(&qdisc->mq.stats, 0, sizeof(qdisc->mq.stats));
	memset(&qdisc->mq.prev_stats, 0, sizeof(qdisc->mq.prev_stats));

	for (i = 0; i < qdisc->num_children; i++) {
		if (!nfp_abm_qdisc_child_valid(qdisc, i))
			continue;

		if (!nfp_abm_qdisc_is_red(qdisc->children[i]))
			continue;
		red = qdisc->children[i];

		for (j = 0; j < red->red.num_bands; j++) {
			nfp_abm_stats_propagate(&qdisc->mq.stats,
						&red->red.band[j].stats);
			nfp_abm_stats_propagate(&qdisc->mq.prev_stats,
						&red->red.band[j].prev_stats);
		}
	}

	nfp_abm_stats_calculate(&qdisc->mq.stats, &qdisc->mq.prev_stats,
				stats->bstats, stats->qstats);

	return qdisc->offloaded ? 0 : -EOPNOTSUPP;
}

int nfp_abm_setup_tc_mq(struct net_device *netdev, struct nfp_abm_link *alink,
			struct tc_mq_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_MQ_CREATE:
		return nfp_abm_mq_create(netdev, alink, opt);
	case TC_MQ_DESTROY:
		nfp_abm_qdisc_destroy(netdev, alink, opt->handle);
		return 0;
	case TC_MQ_STATS:
		return nfp_abm_mq_stats(alink, opt->handle, &opt->stats);
	case TC_MQ_GRAFT:
		return nfp_abm_qdisc_graft(alink, opt->handle,
					   opt->graft_params.child_handle,
					   opt->graft_params.queue);
	default:
		return -EOPNOTSUPP;
	}
}

int nfp_abm_setup_root(struct net_device *netdev, struct nfp_abm_link *alink,
		       struct tc_root_qopt_offload *opt)
{
	if (opt->ingress)
		return -EOPNOTSUPP;
	if (alink->root_qdisc)
		alink->root_qdisc->use_cnt--;
	alink->root_qdisc = nfp_abm_qdisc_find(alink, opt->handle);
	if (alink->root_qdisc)
		alink->root_qdisc->use_cnt++;

	nfp_abm_qdisc_offload_update(alink);

	return 0;
}
