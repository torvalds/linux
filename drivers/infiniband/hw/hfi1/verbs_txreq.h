/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 */

#ifndef HFI1_VERBS_TXREQ_H
#define HFI1_VERBS_TXREQ_H

#include <linux/types.h>
#include <linux/slab.h>

#include "verbs.h"
#include "sdma_txreq.h"
#include "iowait.h"

struct verbs_txreq {
	struct hfi1_sdma_header	phdr;
	struct sdma_txreq       txreq;
	struct rvt_qp           *qp;
	struct rvt_swqe         *wqe;
	struct rvt_mregion	*mr;
	struct rvt_sge_state    *ss;
	struct sdma_engine     *sde;
	struct send_context     *psc;
	u16                     hdr_dwords;
	u16			s_cur_size;
};

struct hfi1_ibdev;
struct verbs_txreq *__get_txreq(struct hfi1_ibdev *dev,
				struct rvt_qp *qp);

#define VERBS_TXREQ_GFP (GFP_ATOMIC | __GFP_NOWARN)
static inline struct verbs_txreq *get_txreq(struct hfi1_ibdev *dev,
					    struct rvt_qp *qp)
	__must_hold(&qp->slock)
{
	struct verbs_txreq *tx;
	struct hfi1_qp_priv *priv = qp->priv;

	tx = kmem_cache_alloc(dev->verbs_txreq_cache, VERBS_TXREQ_GFP);
	if (unlikely(!tx)) {
		/* call slow path to get the lock */
		tx = __get_txreq(dev, qp);
		if (!tx)
			return tx;
	}
	tx->qp = qp;
	tx->mr = NULL;
	tx->sde = priv->s_sde;
	tx->psc = priv->s_sendcontext;
	/* so that we can test if the sdma descriptors are there */
	tx->txreq.num_desc = 0;
	/* Set the header type */
	tx->phdr.hdr.hdr_type = priv->hdr_type;
	tx->txreq.flags = 0;
	return tx;
}

static inline struct verbs_txreq *get_waiting_verbs_txreq(struct iowait_work *w)
{
	struct sdma_txreq *stx;

	stx = iowait_get_txhead(w);
	if (stx)
		return container_of(stx, struct verbs_txreq, txreq);
	return NULL;
}

static inline bool verbs_txreq_queued(struct iowait_work *w)
{
	return iowait_packet_queued(w);
}

void hfi1_put_txreq(struct verbs_txreq *tx);
int verbs_txreq_init(struct hfi1_ibdev *dev);
void verbs_txreq_exit(struct hfi1_ibdev *dev);

#endif                         /* HFI1_VERBS_TXREQ_H */
