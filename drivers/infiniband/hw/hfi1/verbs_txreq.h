/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
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

static inline struct sdma_txreq *get_sdma_txreq(struct verbs_txreq *tx)
{
	return &tx->txreq;
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
