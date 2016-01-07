/*
 * Copyright (c) 2006, 2007, 2008, 2009 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <linux/rculist.h>

#include "qib.h"

/**
 * qib_mcast_qp_alloc - alloc a struct to link a QP to mcast GID struct
 * @qp: the QP to link
 */
static struct qib_mcast_qp *qib_mcast_qp_alloc(struct qib_qp *qp)
{
	struct qib_mcast_qp *mqp;

	mqp = kmalloc(sizeof *mqp, GFP_KERNEL);
	if (!mqp)
		goto bail;

	mqp->qp = qp;
	atomic_inc(&qp->refcount);

bail:
	return mqp;
}

static void qib_mcast_qp_free(struct qib_mcast_qp *mqp)
{
	struct qib_qp *qp = mqp->qp;

	/* Notify qib_destroy_qp() if it is waiting. */
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);

	kfree(mqp);
}

/**
 * qib_mcast_alloc - allocate the multicast GID structure
 * @mgid: the multicast GID
 *
 * A list of QPs will be attached to this structure.
 */
static struct qib_mcast *qib_mcast_alloc(union ib_gid *mgid)
{
	struct qib_mcast *mcast;

	mcast = kmalloc(sizeof *mcast, GFP_KERNEL);
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

static void qib_mcast_free(struct qib_mcast *mcast)
{
	struct qib_mcast_qp *p, *tmp;

	list_for_each_entry_safe(p, tmp, &mcast->qp_list, list)
		qib_mcast_qp_free(p);

	kfree(mcast);
}

/**
 * qib_mcast_find - search the global table for the given multicast GID
 * @ibp: the IB port structure
 * @mgid: the multicast GID to search for
 *
 * Returns NULL if not found.
 *
 * The caller is responsible for decrementing the reference count if found.
 */
struct qib_mcast *qib_mcast_find(struct qib_ibport *ibp, union ib_gid *mgid)
{
	struct rb_node *n;
	unsigned long flags;
	struct qib_mcast *mcast;

	spin_lock_irqsave(&ibp->lock, flags);
	n = ibp->mcast_tree.rb_node;
	while (n) {
		int ret;

		mcast = rb_entry(n, struct qib_mcast, rb_node);

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
 * qib_mcast_add - insert mcast GID into table and attach QP struct
 * @mcast: the mcast GID table
 * @mqp: the QP to attach
 *
 * Return zero if both were added.  Return EEXIST if the GID was already in
 * the table but the QP was added.  Return ESRCH if the QP was already
 * attached and neither structure was added.
 */
static int qib_mcast_add(struct qib_ibdev *dev, struct qib_ibport *ibp,
			 struct qib_mcast *mcast, struct qib_mcast_qp *mqp)
{
	struct rb_node **n = &ibp->mcast_tree.rb_node;
	struct rb_node *pn = NULL;
	int ret;

	spin_lock_irq(&ibp->lock);

	while (*n) {
		struct qib_mcast *tmcast;
		struct qib_mcast_qp *p;

		pn = *n;
		tmcast = rb_entry(pn, struct qib_mcast, rb_node);

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
		if (tmcast->n_attached == ib_qib_max_mcast_qp_attached) {
			ret = ENOMEM;
			goto bail;
		}

		tmcast->n_attached++;

		list_add_tail_rcu(&mqp->list, &tmcast->qp_list);
		ret = EEXIST;
		goto bail;
	}

	spin_lock(&dev->n_mcast_grps_lock);
	if (dev->n_mcast_grps_allocated == ib_qib_max_mcast_grps) {
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

int qib_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct qib_qp *qp = to_iqp(ibqp);
	struct qib_ibdev *dev = to_idev(ibqp->device);
	struct qib_ibport *ibp;
	struct qib_mcast *mcast;
	struct qib_mcast_qp *mqp;
	int ret;

	if (ibqp->qp_num <= 1 || qp->state == IB_QPS_RESET) {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Allocate data structures since its better to do this outside of
	 * spin locks and it will most likely be needed.
	 */
	mcast = qib_mcast_alloc(gid);
	if (mcast == NULL) {
		ret = -ENOMEM;
		goto bail;
	}
	mqp = qib_mcast_qp_alloc(qp);
	if (mqp == NULL) {
		qib_mcast_free(mcast);
		ret = -ENOMEM;
		goto bail;
	}
	ibp = to_iport(ibqp->device, qp->port_num);
	switch (qib_mcast_add(dev, ibp, mcast, mqp)) {
	case ESRCH:
		/* Neither was used: OK to attach the same QP twice. */
		qib_mcast_qp_free(mqp);
		qib_mcast_free(mcast);
		break;

	case EEXIST:            /* The mcast wasn't used */
		qib_mcast_free(mcast);
		break;

	case ENOMEM:
		/* Exceeded the maximum number of mcast groups. */
		qib_mcast_qp_free(mqp);
		qib_mcast_free(mcast);
		ret = -ENOMEM;
		goto bail;

	default:
		break;
	}

	ret = 0;

bail:
	return ret;
}

int qib_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct qib_qp *qp = to_iqp(ibqp);
	struct qib_ibdev *dev = to_idev(ibqp->device);
	struct qib_ibport *ibp = to_iport(ibqp->device, qp->port_num);
	struct qib_mcast *mcast = NULL;
	struct qib_mcast_qp *p, *tmp, *delp = NULL;
	struct rb_node *n;
	int last = 0;
	int ret;

	if (ibqp->qp_num <= 1 || qp->state == IB_QPS_RESET)
		return -EINVAL;

	spin_lock_irq(&ibp->lock);

	/* Find the GID in the mcast table. */
	n = ibp->mcast_tree.rb_node;
	while (1) {
		if (n == NULL) {
			spin_unlock_irq(&ibp->lock);
			return -EINVAL;
		}

		mcast = rb_entry(n, struct qib_mcast, rb_node);
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
		delp = p;

		/* If this was the last attached QP, remove the GID too. */
		if (list_empty(&mcast->qp_list)) {
			rb_erase(&mcast->rb_node, &ibp->mcast_tree);
			last = 1;
		}
		break;
	}

	spin_unlock_irq(&ibp->lock);
	/* QP not attached */
	if (!delp)
		return -EINVAL;
	/*
	 * Wait for any list walkers to finish before freeing the
	 * list element.
	 */
	wait_event(mcast->wait, atomic_read(&mcast->refcount) <= 1);
	qib_mcast_qp_free(delp);

	if (last) {
		atomic_dec(&mcast->refcount);
		wait_event(mcast->wait, !atomic_read(&mcast->refcount));
		qib_mcast_free(mcast);
		spin_lock_irq(&dev->n_mcast_grps_lock);
		dev->n_mcast_grps_allocated--;
		spin_unlock_irq(&dev->n_mcast_grps_lock);
	}
	return 0;
}

int qib_mcast_tree_empty(struct qib_ibport *ibp)
{
	return ibp->mcast_tree.rb_node == NULL;
}
