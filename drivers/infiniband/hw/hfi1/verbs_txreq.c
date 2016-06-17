/*
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
		seq = read_seqbegin(&dev->iowait_lock);
		if (!list_empty(&dev->txwait)) {
			struct iowait *wait;

			write_seqlock_irqsave(&dev->iowait_lock, flags);
			wait = list_first_entry(&dev->txwait, struct iowait,
						list);
			qp = iowait_to_qp(wait);
			priv = qp->priv;
			list_del_init(&priv->s_iowait.list);
			/* refcount held until actual wake up */
			write_sequnlock_irqrestore(&dev->iowait_lock, flags);
			hfi1_qp_wakeup(qp, RVT_S_WAIT_TX);
			break;
		}
	} while (read_seqretry(&dev->iowait_lock, seq));
}

struct verbs_txreq *__get_txreq(struct hfi1_ibdev *dev,
				struct rvt_qp *qp)
{
	struct verbs_txreq *tx = ERR_PTR(-EBUSY);
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	write_seqlock(&dev->iowait_lock);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		struct hfi1_qp_priv *priv;

		tx = kmem_cache_alloc(dev->verbs_txreq_cache, GFP_ATOMIC);
		if (tx)
			goto out;
		priv = qp->priv;
		if (list_empty(&priv->s_iowait.list)) {
			dev->n_txwait++;
			qp->s_flags |= RVT_S_WAIT_TX;
			list_add_tail(&priv->s_iowait.list, &dev->txwait);
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_TX);
			atomic_inc(&qp->refcount);
		}
		qp->s_flags &= ~RVT_S_BUSY;
	}
out:
	write_sequnlock(&dev->iowait_lock);
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return tx;
}

static void verbs_txreq_kmem_cache_ctor(void *obj)
{
	struct verbs_txreq *tx = (struct verbs_txreq *)obj;

	memset(tx, 0, sizeof(*tx));
}

int verbs_txreq_init(struct hfi1_ibdev *dev)
{
	char buf[TXREQ_LEN];
	struct hfi1_devdata *dd = dd_from_dev(dev);

	snprintf(buf, sizeof(buf), "hfi1_%u_vtxreq_cache", dd->unit);
	dev->verbs_txreq_cache = kmem_cache_create(buf,
						   sizeof(struct verbs_txreq),
						   0, SLAB_HWCACHE_ALIGN,
						   verbs_txreq_kmem_cache_ctor);
	if (!dev->verbs_txreq_cache)
		return -ENOMEM;
	return 0;
}

void verbs_txreq_exit(struct hfi1_ibdev *dev)
{
	kmem_cache_destroy(dev->verbs_txreq_cache);
	dev->verbs_txreq_cache = NULL;
}
