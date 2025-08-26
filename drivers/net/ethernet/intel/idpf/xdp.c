// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <net/libeth/xdp.h>

#include "idpf.h"
#include "idpf_virtchnl.h"
#include "xdp.h"

static int idpf_rxq_for_each(const struct idpf_vport *vport,
			     int (*fn)(struct idpf_rx_queue *rxq, void *arg),
			     void *arg)
{
	bool splitq = idpf_is_queue_model_split(vport->rxq_model);

	if (!vport->rxq_grps)
		return -ENETDOWN;

	for (u32 i = 0; i < vport->num_rxq_grp; i++) {
		const struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u32 num_rxq;

		if (splitq)
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			struct idpf_rx_queue *q;
			int err;

			if (splitq)
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];

			err = fn(q, arg);
			if (err)
				return err;
		}
	}

	return 0;
}

static int __idpf_xdp_rxq_info_init(struct idpf_rx_queue *rxq, void *arg)
{
	const struct idpf_vport *vport = rxq->q_vector->vport;
	bool split = idpf_is_queue_model_split(vport->rxq_model);
	const struct page_pool *pp;
	int err;

	err = __xdp_rxq_info_reg(&rxq->xdp_rxq, vport->netdev, rxq->idx,
				 rxq->q_vector->napi.napi_id,
				 rxq->rx_buf_size);
	if (err)
		return err;

	pp = split ? rxq->bufq_sets[0].bufq.pp : rxq->pp;
	xdp_rxq_info_attach_page_pool(&rxq->xdp_rxq, pp);

	if (!split)
		return 0;

	rxq->xdpsqs = &vport->txqs[vport->xdp_txq_offset];
	rxq->num_xdp_txq = vport->num_xdp_txq;

	return 0;
}

int idpf_xdp_rxq_info_init_all(const struct idpf_vport *vport)
{
	return idpf_rxq_for_each(vport, __idpf_xdp_rxq_info_init, NULL);
}

static int __idpf_xdp_rxq_info_deinit(struct idpf_rx_queue *rxq, void *arg)
{
	if (idpf_is_queue_model_split((size_t)arg)) {
		rxq->xdpsqs = NULL;
		rxq->num_xdp_txq = 0;
	}

	xdp_rxq_info_detach_mem_model(&rxq->xdp_rxq);
	xdp_rxq_info_unreg(&rxq->xdp_rxq);

	return 0;
}

void idpf_xdp_rxq_info_deinit_all(const struct idpf_vport *vport)
{
	idpf_rxq_for_each(vport, __idpf_xdp_rxq_info_deinit,
			  (void *)(size_t)vport->rxq_model);
}

static int idpf_xdp_rxq_assign_prog(struct idpf_rx_queue *rxq, void *arg)
{
	struct bpf_prog *prog = arg;
	struct bpf_prog *old;

	if (prog)
		bpf_prog_inc(prog);

	old = rcu_replace_pointer(rxq->xdp_prog, prog, lockdep_rtnl_is_held());
	if (old)
		bpf_prog_put(old);

	return 0;
}

void idpf_xdp_copy_prog_to_rqs(const struct idpf_vport *vport,
			       struct bpf_prog *xdp_prog)
{
	idpf_rxq_for_each(vport, idpf_xdp_rxq_assign_prog, xdp_prog);
}

int idpf_xdpsqs_get(const struct idpf_vport *vport)
{
	struct libeth_xdpsq_timer **timers __free(kvfree) = NULL;
	struct net_device *dev;
	u32 sqs;

	if (!idpf_xdp_enabled(vport))
		return 0;

	timers = kvcalloc(vport->num_xdp_txq, sizeof(*timers), GFP_KERNEL);
	if (!timers)
		return -ENOMEM;

	for (u32 i = 0; i < vport->num_xdp_txq; i++) {
		timers[i] = kzalloc_node(sizeof(*timers[i]), GFP_KERNEL,
					 cpu_to_mem(i));
		if (!timers[i]) {
			for (int j = i - 1; j >= 0; j--)
				kfree(timers[j]);

			return -ENOMEM;
		}
	}

	dev = vport->netdev;
	sqs = vport->xdp_txq_offset;

	for (u32 i = sqs; i < vport->num_txq; i++) {
		struct idpf_tx_queue *xdpsq = vport->txqs[i];

		xdpsq->complq = xdpsq->txq_grp->complq;
		kfree(xdpsq->refillq);
		xdpsq->refillq = NULL;

		idpf_queue_clear(FLOW_SCH_EN, xdpsq);
		idpf_queue_clear(FLOW_SCH_EN, xdpsq->complq);
		idpf_queue_set(NOIRQ, xdpsq);
		idpf_queue_set(XDP, xdpsq);
		idpf_queue_set(XDP, xdpsq->complq);

		xdpsq->timer = timers[i - sqs];
		libeth_xdpsq_get(&xdpsq->xdp_lock, dev, vport->xdpsq_share);

		xdpsq->pending = 0;
		xdpsq->xdp_tx = 0;
		xdpsq->thresh = libeth_xdp_queue_threshold(xdpsq->desc_count);
	}

	return 0;
}

void idpf_xdpsqs_put(const struct idpf_vport *vport)
{
	struct net_device *dev;
	u32 sqs;

	if (!idpf_xdp_enabled(vport))
		return;

	dev = vport->netdev;
	sqs = vport->xdp_txq_offset;

	for (u32 i = sqs; i < vport->num_txq; i++) {
		struct idpf_tx_queue *xdpsq = vport->txqs[i];

		if (!idpf_queue_has_clear(XDP, xdpsq))
			continue;

		libeth_xdpsq_put(&xdpsq->xdp_lock, dev);

		kfree(xdpsq->timer);
		xdpsq->refillq = NULL;
		idpf_queue_clear(NOIRQ, xdpsq);
	}
}

static int idpf_xdp_setup_prog(struct idpf_vport *vport,
			       const struct netdev_bpf *xdp)
{
	const struct idpf_netdev_priv *np = netdev_priv(vport->netdev);
	struct bpf_prog *old, *prog = xdp->prog;
	struct idpf_vport_config *cfg;
	int ret;

	cfg = vport->adapter->vport_config[vport->idx];

	if (test_bit(IDPF_REMOVE_IN_PROG, vport->adapter->flags) ||
	    !test_bit(IDPF_VPORT_REG_NETDEV, cfg->flags) ||
	    !!vport->xdp_prog == !!prog) {
		if (np->state == __IDPF_VPORT_UP)
			idpf_xdp_copy_prog_to_rqs(vport, prog);

		old = xchg(&vport->xdp_prog, prog);
		if (old)
			bpf_prog_put(old);

		cfg->user_config.xdp_prog = prog;

		return 0;
	}

	if (!vport->num_xdp_txq && vport->num_txq == cfg->max_q.max_txq) {
		NL_SET_ERR_MSG_MOD(xdp->extack,
				   "No Tx queues available for XDP, please decrease the number of regular SQs");
		return -ENOSPC;
	}

	old = cfg->user_config.xdp_prog;
	cfg->user_config.xdp_prog = prog;

	ret = idpf_initiate_soft_reset(vport, IDPF_SR_Q_CHANGE);
	if (ret) {
		NL_SET_ERR_MSG_MOD(xdp->extack,
				   "Could not reopen the vport after XDP setup");

		cfg->user_config.xdp_prog = old;
		old = prog;
	}

	if (old)
		bpf_prog_put(old);

	return ret;
}

int idpf_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct idpf_vport *vport;
	int ret;

	idpf_vport_ctrl_lock(dev);
	vport = idpf_netdev_to_vport(dev);

	if (!idpf_is_queue_model_split(vport->txq_model))
		goto notsupp;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		ret = idpf_xdp_setup_prog(vport, xdp);
		break;
	default:
notsupp:
		ret = -EOPNOTSUPP;
		break;
	}

	idpf_vport_ctrl_unlock(dev);

	return ret;
}
