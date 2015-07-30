/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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

#include <linux/rculist.h>

#include "hfi.h"

/**
 * mcast_qp_alloc - alloc a struct to link a QP to mcast GID struct
 * @qp: the QP to link
 */
static struct hfi1_mcast_qp *mcast_qp_alloc(struct hfi1_qp *qp)
{
	struct hfi1_mcast_qp *mqp;

	mqp = kmalloc(sizeof(*mqp), GFP_KERNEL);
	if (!mqp)
		goto bail;

	mqp->qp = qp;
	atomic_inc(&qp->refcount);

bail:
	return mqp;
}

static void mcast_qp_free(struct hfi1_mcast_qp *mqp)
{
	struct hfi1_qp *qp = mqp->qp;

	/* Notify hfi1_destroy_qp() if it is waiting. */
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);

	kfree(mqp);
}

/**
 * mcast_alloc - allocate the multicast GID structure
 * @mgid: the multicast GID
 *
 * A list of QPs will be attached to this structure.
 */
static struct hfi1_mcast *mcast_alloc(union ib_gid *mgid)
{
	struct hfi1_mcast *mcast;

	mcast = kmalloc(sizeof(*mcast), GFP_KERNEL);
	if (!mcast)
		goto bail;

	mcast->mgid = *mgid;
	INIT_LIST_HEAD(&mcast->qp_list);
	init_waitqueue_head(&mcast->wait);
	atomic_set(&mcast->refcount, 0);
	mcast->n_attached = 0;

bail:
	return mcast;
}

static void mcast_free(struct hfi1_mcast *mcast)
{
	struct hfi1_mcast_qp *p, *tmp;

	list_for_each_entry_safe(p, tmp, &mcast->qp_list, list)
		mcast_qp_free(p);

	kfree(mcast);
}

/**
 * hfi1_mcast_find - search the global table for the given multicast GID
 * @ibp: the IB port structure
 * @mgid: the multicast GID to search for
 *
 * Returns NULL if not found.
 *
 * The caller is responsible for decrementing the reference count if found.
 */
struct hfi1_mcast *hfi1_mcast_find(struct hfi1_ibport *ibp, union ib_gid *mgid)
{
	struct rb_node *n;
	unsigned long flags;
	struct hfi1_mcast *mcast;

	spin_lock_irqsave(&ibp->lock, flags);
	n = ibp->mcast_tree.rb_node;
	while (n) {
		int ret;

		mcast = rb_entry(n, struct hfi1_mcast, rb_node);

		ret = memcmp(mgid->raw, mcast->mgid.raw,
			     sizeof(union ib_gid));
		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else {
			atomic_inc(&mcast->refcount);
			spin_unlock_irqrestore(&ibp->lock, flags);
			goto bail;
		}
	}
	spin_unlock_irqrestore(&ibp->lock, flags);

	mcast = NULL;

bail:
	return mcast;
}

/**
 * mcast_add - insert mcast GID into table and attach QP struct
 * @mcast: the mcast GID table
 * @mqp: the QP to attach
 *
 * Return zero if both were added.  Return EEXIST if the GID was already in
 * the table but the QP was added.  Return ESRCH if the QP was already
 * attached and neither structure was added.
 */
static int mcast_add(struct hfi1_ibdev *dev, struct hfi1_ibport *ibp,
		     struct hfi1_mcast *mcast, struct hfi1_mcast_qp *mqp)
{
	struct rb_node **n = &ibp->mcast_tree.rb_node;
	struct rb_node *pn = NULL;
	int ret;

	spin_lock_irq(&ibp->lock);

	while (*n) {
		struct hfi1_mcast *tmcast;
		struct hfi1_mcast_qp *p;

		pn = *n;
		tmcast = rb_entry(pn, struct hfi1_mcast, rb_node);

		ret = memcmp(mcast->mgid.raw, tmcast->mgid.raw,
			     sizeof(union ib_gid));
		if (ret < 0) {
			n = &pn->rb_left;
			continue;
		}
		if (ret > 0) {
			n = &pn->rb_right;
			continue;
		}

		/* Search the QP list to see if this is already there. */
		list_for_each_entry_rcu(p, &tmcast->qp_list, list) {
			if (p->qp == mqp->qp) {
				ret = ESRCH;
				goto bail;
			}
		}
		if (tmcast->n_attached == hfi1_max_mcast_qp_attached) {
			ret = ENOMEM;
			goto bail;
		}

		tmcast->n_attached++;

		list_add_tail_rcu(&mqp->list, &tmcast->qp_list);
		ret = EEXIST;
		goto bail;
	}

	spin_lock(&dev->n_mcast_grps_lock);
	if (dev->n_mcast_grps_allocated == hfi1_max_mcast_grps) {
		spin_unlock(&dev->n_mcast_grps_lock);
		ret = ENOMEM;
		goto bail;
	}

	dev->n_mcast_grps_allocated++;
	spin_unlock(&dev->n_mcast_grps_lock);

	mcast->n_attached++;

	list_add_tail_rcu(&mqp->list, &mcast->qp_list);

	atomic_inc(&mcast->refcount);
	rb_link_node(&mcast->rb_node, pn, n);
	rb_insert_color(&mcast->rb_node, &ibp->mcast_tree);

	ret = 0;

bail:
	spin_unlock_irq(&ibp->lock);

	return ret;
}

int hfi1_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct hfi1_qp *qp = to_iqp(ibqp);
	struct hfi1_ibdev *dev = to_idev(ibqp->device);
	struct hfi1_ibport *ibp;
	struct hfi1_mcast *mcast;
	struct hfi1_mcast_qp *mqp;
	int ret;

	if (ibqp->qp_num <= 1 || qp->state == IB_QPS_RESET) {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Allocate data structures since its better to do this outside of
	 * spin locks and it will most likely be needed.
	 */
	mcast = mcast_alloc(gid);
	if (mcast == NULL) {
		ret = -ENOMEM;
		goto bail;
	}
	mqp = mcast_qp_alloc(qp);
	if (mqp == NULL) {
		mcast_free(mcast);
		ret = -ENOMEM;
		goto bail;
	}
	ibp = to_iport(ibqp->device, qp->port_num);
	switch (mcast_add(dev, ibp, mcast, mqp)) {
	case ESRCH:
		/* Neither was used: OK to attach the same QP twice. */
		mcast_qp_free(mqp);
		mcast_free(mcast);
		break;

	case EEXIST:            /* The mcast wasn't used */
		mcast_free(mcast);
		break;

	case ENOMEM:
		/* Exceeded the maximum number of mcast groups. */
		mcast_qp_free(mqp);
		mcast_free(mcast);
		ret = -ENOMEM;
		goto bail;

	default:
		break;
	}

	ret = 0;

bail:
	return ret;
}

int hfi1_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct hfi1_qp *qp = to_iqp(ibqp);
	struct hfi1_ibdev *dev = to_idev(ibqp->device);
	struct hfi1_ibport *ibp = to_iport(ibqp->device, qp->port_num);
	struct hfi1_mcast *mcast = NULL;
	struct hfi1_mcast_qp *p, *tmp;
	struct rb_node *n;
	int last = 0;
	int ret;

	if (ibqp->qp_num <= 1 || qp->state == IB_QPS_RESET) {
		ret = -EINVAL;
		goto bail;
	}

	spin_lock_irq(&ibp->lock);

	/* Find the GID in the mcast table. */
	n = ibp->mcast_tree.rb_node;
	while (1) {
		if (n == NULL) {
			spin_unlock_irq(&ibp->lock);
			ret = -EINVAL;
			goto bail;
		}

		mcast = rb_entry(n, struct hfi1_mcast, rb_node);
		ret = memcmp(gid->raw, mcast->mgid.raw,
			     sizeof(union ib_gid));
		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			break;
	}

	/* Search the QP list. */
	list_for_each_entry_safe(p, tmp, &mcast->qp_list, list) {
		if (p->qp != qp)
			continue;
		/*
		 * We found it, so remove it, but don't poison the forward
		 * link until we are sure there are no list walkers.
		 */
		list_del_rcu(&p->list);
		mcast->n_attached--;

		/* If this was the last attached QP, remove the GID too. */
		if (list_empty(&mcast->qp_list)) {
			rb_erase(&mcast->rb_node, &ibp->mcast_tree);
			last = 1;
		}
		break;
	}

	spin_unlock_irq(&ibp->lock);

	if (p) {
		/*
		 * Wait for any list walkers to finish before freeing the
		 * list element.
		 */
		wait_event(mcast->wait, atomic_read(&mcast->refcount) <= 1);
		mcast_qp_free(p);
	}
	if (last) {
		atomic_dec(&mcast->refcount);
		wait_event(mcast->wait, !atomic_read(&mcast->refcount));
		mcast_free(mcast);
		spin_lock_irq(&dev->n_mcast_grps_lock);
		dev->n_mcast_grps_allocated--;
		spin_unlock_irq(&dev->n_mcast_grps_lock);
	}

	ret = 0;

bail:
	return ret;
}

int hfi1_mcast_tree_empty(struct hfi1_ibport *ibp)
{
	return ibp->mcast_tree.rb_node == NULL;
}
