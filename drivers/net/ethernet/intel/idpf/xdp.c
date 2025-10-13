// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include "idpf.h"
#include "idpf_virtchnl.h"
#include "xdp.h"
#include "xsk.h"

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
	int err;

	err = __xdp_rxq_info_reg(&rxq->xdp_rxq, vport->netdev, rxq->idx,
				 rxq->q_vector->napi.napi_id,
				 rxq->rx_buf_size);
	if (err)
		return err;

	if (idpf_queue_has(XSK, rxq)) {
		err = xdp_rxq_info_reg_mem_model(&rxq->xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL,
						 rxq->pool);
		if (err)
			goto unreg;
	} else {
		const struct page_pool *pp;

		pp = split ? rxq->bufq_sets[0].bufq.pp : rxq->pp;
		xdp_rxq_info_attach_page_pool(&rxq->xdp_rxq, pp);
	}

	if (!split)
		return 0;

	rxq->xdpsqs = &vport->txqs[vport->xdp_txq_offset];
	rxq->num_xdp_txq = vport->num_xdp_txq;

	return 0;

unreg:
	xdp_rxq_info_unreg(&rxq->xdp_rxq);

	return err;
}

int idpf_xdp_rxq_info_init(struct idpf_rx_queue *rxq)
{
	return __idpf_xdp_rxq_info_init(rxq, NULL);
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

	if (!idpf_queue_has(XSK, rxq))
		xdp_rxq_info_detach_mem_model(&rxq->xdp_rxq);

	xdp_rxq_info_unreg(&rxq->xdp_rxq);

	return 0;
}

void idpf_xdp_rxq_info_deinit(struct idpf_rx_queue *rxq, u32 model)
{
	__idpf_xdp_rxq_info_deinit(rxq, (void *)(size_t)model);
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

static void idpf_xdp_tx_timer(struct work_struct *work);

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
		libeth_xdpsq_init_timer(xdpsq->timer, xdpsq, &xdpsq->xdp_lock,
					idpf_xdp_tx_timer);

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

		libeth_xdpsq_deinit_timer(xdpsq->timer);
		libeth_xdpsq_put(&xdpsq->xdp_lock, dev);

		kfree(xdpsq->timer);
		xdpsq->refillq = NULL;
		idpf_queue_clear(NOIRQ, xdpsq);
	}
}

static int idpf_xdp_parse_cqe(const struct idpf_splitq_4b_tx_compl_desc *desc,
			      bool gen)
{
	u32 val;

#ifdef __LIBETH_WORD_ACCESS
	val = *(const u32 *)desc;
#else
	val = ((u32)le16_to_cpu(desc->q_head_compl_tag.q_head) << 16) |
	      le16_to_cpu(desc->qid_comptype_gen);
#endif
	if (!!(val & IDPF_TXD_COMPLQ_GEN_M) != gen)
		return -ENODATA;

	if (unlikely((val & GENMASK(IDPF_TXD_COMPLQ_GEN_S - 1, 0)) !=
		     FIELD_PREP(IDPF_TXD_COMPLQ_COMPL_TYPE_M,
				IDPF_TXD_COMPLT_RS)))
		return -EINVAL;

	return upper_16_bits(val);
}

u32 idpf_xdpsq_poll(struct idpf_tx_queue *xdpsq, u32 budget)
{
	struct idpf_compl_queue *cq = xdpsq->complq;
	u32 tx_ntc = xdpsq->next_to_clean;
	u32 tx_cnt = xdpsq->desc_count;
	u32 ntc = cq->next_to_clean;
	u32 cnt = cq->desc_count;
	u32 done_frames;
	bool gen;

	gen = idpf_queue_has(GEN_CHK, cq);

	for (done_frames = 0; done_frames < budget; ) {
		int ret;

		ret = idpf_xdp_parse_cqe(&cq->comp_4b[ntc], gen);
		if (ret >= 0) {
			done_frames = ret > tx_ntc ? ret - tx_ntc :
						     ret + tx_cnt - tx_ntc;
			goto next;
		}

		switch (ret) {
		case -ENODATA:
			goto out;
		case -EINVAL:
			break;
		}

next:
		if (unlikely(++ntc == cnt)) {
			ntc = 0;
			gen = !gen;
			idpf_queue_change(GEN_CHK, cq);
		}
	}

out:
	cq->next_to_clean = ntc;

	return done_frames;
}

static u32 idpf_xdpsq_complete(void *_xdpsq, u32 budget)
{
	struct libeth_xdpsq_napi_stats ss = { };
	struct idpf_tx_queue *xdpsq = _xdpsq;
	u32 tx_ntc = xdpsq->next_to_clean;
	u32 tx_cnt = xdpsq->desc_count;
	struct xdp_frame_bulk bq;
	struct libeth_cq_pp cp = {
		.dev	= xdpsq->dev,
		.bq	= &bq,
		.xss	= &ss,
		.napi	= true,
	};
	u32 done_frames;

	done_frames = idpf_xdpsq_poll(xdpsq, budget);
	if (unlikely(!done_frames))
		return 0;

	xdp_frame_bulk_init(&bq);

	for (u32 i = 0; likely(i < done_frames); i++) {
		libeth_xdp_complete_tx(&xdpsq->tx_buf[tx_ntc], &cp);

		if (unlikely(++tx_ntc == tx_cnt))
			tx_ntc = 0;
	}

	xdp_flush_frame_bulk(&bq);

	xdpsq->next_to_clean = tx_ntc;
	xdpsq->pending -= done_frames;
	xdpsq->xdp_tx -= cp.xdp_tx;

	return done_frames;
}

static u32 idpf_xdp_tx_prep(void *_xdpsq, struct libeth_xdpsq *sq)
{
	struct idpf_tx_queue *xdpsq = _xdpsq;
	u32 free;

	libeth_xdpsq_lock(&xdpsq->xdp_lock);

	free = xdpsq->desc_count - xdpsq->pending;
	if (free < xdpsq->thresh)
		free += idpf_xdpsq_complete(xdpsq, xdpsq->thresh);

	*sq = (struct libeth_xdpsq){
		.sqes		= xdpsq->tx_buf,
		.descs		= xdpsq->desc_ring,
		.count		= xdpsq->desc_count,
		.lock		= &xdpsq->xdp_lock,
		.ntu		= &xdpsq->next_to_use,
		.pending	= &xdpsq->pending,
		.xdp_tx		= &xdpsq->xdp_tx,
	};

	return free;
}

LIBETH_XDP_DEFINE_START();
LIBETH_XDP_DEFINE_TIMER(static idpf_xdp_tx_timer, idpf_xdpsq_complete);
LIBETH_XDP_DEFINE_FLUSH_TX(idpf_xdp_tx_flush_bulk, idpf_xdp_tx_prep,
			   idpf_xdp_tx_xmit);
LIBETH_XDP_DEFINE_FLUSH_XMIT(static idpf_xdp_xmit_flush_bulk, idpf_xdp_tx_prep,
			     idpf_xdp_tx_xmit);
LIBETH_XDP_DEFINE_END();

int idpf_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		  u32 flags)
{
	const struct idpf_netdev_priv *np = netdev_priv(dev);
	const struct idpf_vport *vport = np->vport;

	if (unlikely(!netif_carrier_ok(dev) || !vport->link_up))
		return -ENETDOWN;

	return libeth_xdp_xmit_do_bulk(dev, n, frames, flags,
				       &vport->txqs[vport->xdp_txq_offset],
				       vport->num_xdp_txq,
				       idpf_xdp_xmit_flush_bulk,
				       idpf_xdp_tx_finalize);
}

static int idpf_xdpmo_rx_hash(const struct xdp_md *ctx, u32 *hash,
			      enum xdp_rss_hash_type *rss_type)
{
	const struct libeth_xdp_buff *xdp = (typeof(xdp))ctx;
	struct idpf_xdp_rx_desc desc __uninitialized;
	const struct idpf_rx_queue *rxq;
	struct libeth_rx_pt pt;

	rxq = libeth_xdp_buff_to_rq(xdp, typeof(*rxq), xdp_rxq);

	idpf_xdp_get_qw0(&desc, xdp->desc);

	pt = rxq->rx_ptype_lkup[idpf_xdp_rx_pt(&desc)];
	if (!libeth_rx_pt_has_hash(rxq->xdp_rxq.dev, pt))
		return -ENODATA;

	idpf_xdp_get_qw2(&desc, xdp->desc);

	return libeth_xdpmo_rx_hash(hash, rss_type, idpf_xdp_rx_hash(&desc),
				    pt);
}

static const struct xdp_metadata_ops idpf_xdpmo = {
	.xmo_rx_hash		= idpf_xdpmo_rx_hash,
};

void idpf_xdp_set_features(const struct idpf_vport *vport)
{
	if (!idpf_is_queue_model_split(vport->rxq_model))
		return;

	libeth_xdp_set_features_noredir(vport->netdev, &idpf_xdpmo,
					idpf_get_max_tx_bufs(vport->adapter),
					libeth_xsktmo);
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

	libeth_xdp_set_redirect(vport->netdev, vport->xdp_prog);

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
	case XDP_SETUP_XSK_POOL:
		ret = idpf_xsk_pool_setup(vport, xdp);
		break;
	default:
notsupp:
		ret = -EOPNOTSUPP;
		break;
	}

	idpf_vport_ctrl_unlock(dev);

	return ret;
}
