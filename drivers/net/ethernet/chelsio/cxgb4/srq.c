/*
 * This file is part of the Chelsio T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017-2018 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "cxgb4.h"
#include "t4_msg.h"
#include "srq.h"

struct srq_data *t4_init_srq(int srq_size)
{
	struct srq_data *s;

	s = kvzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return NULL;

	s->srq_size = srq_size;
	init_completion(&s->comp);
	mutex_init(&s->lock);

	return s;
}

/* cxgb4_get_srq_entry: read the SRQ table entry
 * @dev: Pointer to the net_device
 * @idx: Index to the srq
 * @entryp: pointer to the srq entry
 *
 * Sends CPL_SRQ_TABLE_REQ message for the given index.
 * Contents will be returned in CPL_SRQ_TABLE_RPL message.
 *
 * Returns zero if the read is successful, else a error
 * number will be returned. Caller should not use the srq
 * entry if the return value is non-zero.
 *
 *
 */
int cxgb4_get_srq_entry(struct net_device *dev,
			int srq_idx, struct srq_entry *entryp)
{
	struct cpl_srq_table_req *req;
	struct adapter *adap;
	struct sk_buff *skb;
	struct srq_data *s;
	int rc = -ENODEV;

	adap = netdev2adap(dev);
	s = adap->srq;

	if (!(adap->flags & CXGB4_FULL_INIT_DONE) || !s)
		goto out;

	skb = alloc_skb(sizeof(*req), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	req = (struct cpl_srq_table_req *)
		__skb_put_zero(skb, sizeof(*req));
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SRQ_TABLE_REQ,
					      TID_TID_V(srq_idx) |
				TID_QID_V(adap->sge.fw_evtq.abs_id)));
	req->idx = srq_idx;

	mutex_lock(&s->lock);

	s->entryp = entryp;
	t4_mgmt_tx(adap, skb);

	rc = wait_for_completion_timeout(&s->comp, SRQ_WAIT_TO);
	if (rc)
		rc = 0;
	else /* !rc means we timed out */
		rc = -ETIMEDOUT;

	WARN_ON_ONCE(entryp->idx != srq_idx);
	mutex_unlock(&s->lock);
out:
	return rc;
}
EXPORT_SYMBOL(cxgb4_get_srq_entry);

void do_srq_table_rpl(struct adapter *adap,
		      const struct cpl_srq_table_rpl *rpl)
{
	unsigned int idx = TID_TID_G(GET_TID(rpl));
	struct srq_data *s = adap->srq;
	struct srq_entry *e;

	if (unlikely(rpl->status != CPL_CONTAINS_READ_RPL)) {
		dev_err(adap->pdev_dev,
			"Unexpected SRQ_TABLE_RPL status %u for entry %u\n",
				rpl->status, idx);
		goto out;
	}

	/* Store the read entry */
	e = s->entryp;
	e->valid = 1;
	e->idx = idx;
	e->pdid = SRQT_PDID_G(be64_to_cpu(rpl->rsvd_pdid));
	e->qlen = SRQT_QLEN_G(be32_to_cpu(rpl->qlen_qbase));
	e->qbase = SRQT_QBASE_G(be32_to_cpu(rpl->qlen_qbase));
	e->cur_msn = be16_to_cpu(rpl->cur_msn);
	e->max_msn = be16_to_cpu(rpl->max_msn);
out:
	complete(&s->comp);
}
