// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 */

#include "hfi.h"
#include "verbs_txreq.h"
#include "qp.h"
#include "trace.h"

#define TXREQ_LEN 24

void hfi1_put_txreq(struct verbs_txreq *tx)
{
	struct hfi1_ibdev *dev;
	struct rvt_qp *qp;
	unsigned long flags;
	unsigned int seq;
	struct hfi1_qp_priv *priv;

	qp = tx->qp;
	dev = to_idev(qp->ibqp.device);

	if (tx->mr)
		rvt_put_mr(tx->mr);

	sdma_txclean(dd_from_dev(dev), &tx->txreq);

	/* Free verbs_txreq and return to slab cache */
	kmem_cache_free(dev->verbs_txreq_cache, tx);

	do {
		seq = read_seqbegin(&dev->txwait_lock);
		if (!list_empty(&dev->txwait)) {
			struct iowait *wait;

			write_seqlock_irqsave(&dev->txwait_lock, flags);
			wait = list_first_entry(&dev->txwait, struct iowait,
						list);
			qp = iowait_to_qp(wait);
			priv = qp->priv;
			list_del_init(&priv->s_iowait.list);
			/* refcount held until actual wake up */
			write_sequnlock_irqrestore(&dev->txwait_lock, flags);
			hfi1_qp_wakeup(qp, RVT_S_WAIT_TX);
			break;
		}
	} while (read_seqretry(&dev->txwait_lock, seq));
}

struct verbs_txreq *__get_txreq(struct hfi1_ibdev *dev,
				struct rvt_qp *qp)
	__must_hold(&qp->s_lock)
{
	struct verbs_txreq *tx = NULL;

	write_seqlock(&dev->txwait_lock);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		struct hfi1_qp_priv *priv;

		tx = kmem_cache_alloc(dev->verbs_txreq_cache, VERBS_TXREQ_GFP);
		if (tx)
			goto out;
		priv = qp->priv;
		if (list_empty(&priv->s_iowait.list)) {
			dev->n_txwait++;
			qp->s_flags |= RVT_S_WAIT_TX;
			list_add_tail(&priv->s_iowait.list, &dev->txwait);
			priv->s_iowait.lock = &dev->txwait_lock;
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_TX);
			rvt_get_qp(qp);
		}
		qp->s_flags &= ~RVT_S_BUSY;
	}
out:
	write_sequnlock(&dev->txwait_lock);
	return tx;
}

int verbs_txreq_init(struct hfi1_ibdev *dev)
{
	char buf[TXREQ_LEN];
	struct hfi1_devdata *dd = dd_from_dev(dev);

	snprintf(buf, sizeof(buf), "hfi1_%u_vtxreq_cache", dd->unit);
	dev->verbs_txreq_cache = kmem_cache_create(buf,
						   sizeof(struct verbs_txreq),
						   0, SLAB_HWCACHE_ALIGN,
						   NULL);
	if (!dev->verbs_txreq_cache)
		return -ENOMEM;
	return 0;
}

void verbs_txreq_exit(struct hfi1_ibdev *dev)
{
	kmem_cache_destroy(dev->verbs_txreq_cache);
	dev->verbs_txreq_cache = NULL;
}
