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
