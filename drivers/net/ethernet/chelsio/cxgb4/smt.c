/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
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
#include "smt.h"
#include "t4_msg.h"
#include "t4fw_api.h"
#include "t4_regs.h"
#include "t4_values.h"

struct smt_data *t4_init_smt(void)
{
	unsigned int smt_size;
	struct smt_data *s;
	int i;

	smt_size = SMT_SIZE;

	s = kvzalloc(sizeof(*s) + smt_size * sizeof(struct smt_entry),
		     GFP_KERNEL);
	if (!s)
		return NULL;
	s->smt_size = smt_size;
	rwlock_init(&s->lock);
	for (i = 0; i < s->smt_size; ++i) {
		s->smtab[i].idx = i;
		s->smtab[i].state = SMT_STATE_UNUSED;
		memset(&s->smtab[i].src_mac, 0, ETH_ALEN);
		spin_lock_init(&s->smtab[i].lock);
		atomic_set(&s->smtab[i].refcnt, 0);
	}
	return s;
}

static struct smt_entry *find_or_alloc_smte(struct smt_data *s, u8 *smac)
{
	struct smt_entry *first_free = NULL;
	struct smt_entry *e, *end;

	for (e = &s->smtab[0], end = &s->smtab[s->smt_size]; e != end; ++e) {
		if (atomic_read(&e->refcnt) == 0) {
			if (!first_free)
				first_free = e;
		} else {
			if (e->state == SMT_STATE_SWITCHING) {
				/* This entry is actually in use. See if we can
				 * re-use it ?
				 */
				if (memcmp(e->src_mac, smac, ETH_ALEN) == 0)
					goto found_reuse;
			}
		}
	}

	if (first_free) {
		e = first_free;
		goto found;
	}
	return NULL;

found:
	e->state = SMT_STATE_UNUSED;

found_reuse:
	return e;
}

static void t4_smte_free(struct smt_entry *e)
{
	spin_lock_bh(&e->lock);
	if (atomic_read(&e->refcnt) == 0) {  /* hasn't been recycled */
		e->state = SMT_STATE_UNUSED;
	}
	spin_unlock_bh(&e->lock);
}

/**
 * @e: smt entry to release
 *
 * Releases ref count and frees up an smt entry from SMT table
 */
void cxgb4_smt_release(struct smt_entry *e)
{
	if (atomic_dec_and_test(&e->refcnt))
		t4_smte_free(e);
}
EXPORT_SYMBOL(cxgb4_smt_release);

void do_smt_write_rpl(struct adapter *adap, const struct cpl_smt_write_rpl *rpl)
{
	unsigned int smtidx = TID_TID_G(GET_TID(rpl));
	struct smt_data *s = adap->smt;

	if (unlikely(rpl->status != CPL_ERR_NONE)) {
		struct smt_entry *e = &s->smtab[smtidx];

		dev_err(adap->pdev_dev,
			"Unexpected SMT_WRITE_RPL status %u for entry %u\n",
			rpl->status, smtidx);
		spin_lock(&e->lock);
		e->state = SMT_STATE_ERROR;
		spin_unlock(&e->lock);
		return;
	}
}

static int write_smt_entry(struct adapter *adapter, struct smt_entry *e)
{
	struct cpl_t6_smt_write_req *t6req;
	struct smt_data *s = adapter->smt;
	struct cpl_smt_write_req *req;
	struct sk_buff *skb;
	int size;
	u8 row;

	if (CHELSIO_CHIP_VERSION(adapter->params.chip) <= CHELSIO_T5) {
		size = sizeof(*req);
		skb = alloc_skb(size, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;
		/* Source MAC Table (SMT) contains 256 SMAC entries
		 * organized in 128 rows of 2 entries each.
		 */
		req = (struct cpl_smt_write_req *)__skb_put(skb, size);
		INIT_TP_WR(req, 0);

		/* Each row contains an SMAC pair.
		 * LSB selects the SMAC entry within a row
		 */
		row = (e->idx >> 1);
		if (e->idx & 1) {
			req->pfvf1 = 0x0;
			memcpy(req->src_mac1, e->src_mac, ETH_ALEN);

			/* fill pfvf0/src_mac0 with entry
			 * at prev index from smt-tab.
			 */
			req->pfvf0 = 0x0;
			memcpy(req->src_mac0, s->smtab[e->idx - 1].src_mac,
			       ETH_ALEN);
		} else {
			req->pfvf0 = 0x0;
			memcpy(req->src_mac0, e->src_mac, ETH_ALEN);

			/* fill pfvf1/src_mac1 with entry
			 * at next index from smt-tab
			 */
			req->pfvf1 = 0x0;
			memcpy(req->src_mac1, s->smtab[e->idx + 1].src_mac,
			       ETH_ALEN);
		}
	} else {
		size = sizeof(*t6req);
		skb = alloc_skb(size, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;
		/* Source MAC Table (SMT) contains 256 SMAC entries */
		t6req = (struct cpl_t6_smt_write_req *)__skb_put(skb, size);
		INIT_TP_WR(t6req, 0);
		req = (struct cpl_smt_write_req *)t6req;

		/* fill pfvf0/src_mac0 from smt-tab */
		req->pfvf0 = 0x0;
		memcpy(req->src_mac0, s->smtab[e->idx].src_mac, ETH_ALEN);
		row = e->idx;
	}

	OPCODE_TID(req) =
		htonl(MK_OPCODE_TID(CPL_SMT_WRITE_REQ, e->idx |
				    TID_QID_V(adapter->sge.fw_evtq.abs_id)));
	req->params = htonl(SMTW_NORPL_V(0) |
			    SMTW_IDX_V(row) |
			    SMTW_OVLAN_IDX_V(0));
	t4_mgmt_tx(adapter, skb);
	return 0;
}

static struct smt_entry *t4_smt_alloc_switching(struct adapter *adap, u16 pfvf,
						u8 *smac)
{
	struct smt_data *s = adap->smt;
	struct smt_entry *e;

	write_lock_bh(&s->lock);
	e = find_or_alloc_smte(s, smac);
	if (e) {
		spin_lock(&e->lock);
		if (!atomic_read(&e->refcnt)) {
			atomic_set(&e->refcnt, 1);
			e->state = SMT_STATE_SWITCHING;
			e->pfvf = pfvf;
			memcpy(e->src_mac, smac, ETH_ALEN);
			write_smt_entry(adap, e);
		} else {
			atomic_inc(&e->refcnt);
		}
		spin_unlock(&e->lock);
	}
	write_unlock_bh(&s->lock);
	return e;
}

/**
 * @dev: net_device pointer
 * @smac: MAC address to add to SMT
 * Returns pointer to the SMT entry created
 *
 * Allocates an SMT entry to be used by switching rule of a filter.
 */
struct smt_entry *cxgb4_smt_alloc_switching(struct net_device *dev, u8 *smac)
{
	struct adapter *adap = netdev2adap(dev);

	return t4_smt_alloc_switching(adap, 0x0, smac);
}
EXPORT_SYMBOL(cxgb4_smt_alloc_switching);
