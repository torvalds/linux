// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/rculist.h>
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_qp.h>

#include "mcast.h"

/**
 * rvt_driver_mcast_init - init resources for multicast
 * @rdi: rvt dev struct
 *
 * This is per device that registers with rdmavt
 */
void rvt_driver_mcast_init(struct rvt_dev_info *rdi)
{
	/*
	 * Anything that needs setup for multicast on a per driver or per rdi
	 * basis should be done in here.
	 */
	spin_lock_init(&rdi->n_mcast_grps_lock);
}

/**
 * rvt_mcast_qp_alloc - alloc a struct to link a QP to mcast GID struct
 * @qp: the QP to link
 */
static struct rvt_mcast_qp *rvt_mcast_qp_alloc(struct rvt_qp *qp)
{
	struct rvt_mcast_qp *mqp;

	mqp = kmalloc(sizeof(*mqp), GFP_KERNEL);
	if (!mqp)
		goto bail;

	mqp->qp = qp;
	rvt_get_qp(qp);

bail:
	return mqp;
}

static void rvt_mcast_qp_free(struct rvt_mcast_qp *mqp)
{
	struct rvt_qp *qp = mqp->qp;

	/* Notify hfi1_destroy_qp() if it is waiting. */
	rvt_put_qp(qp);

	kfree(mqp);
}

/**
 * rvt_mcast_alloc - allocate the multicast GID structure
 * @mgid: the multicast GID
 * @lid: the muilticast LID (host order)
 *
 * A list of QPs will be attached to this structure.
 */
static struct rvt_mcast *rvt_mcast_alloc(union ib_gid *mgid, u16 lid)
{
	struct rvt_mcast *mcast;

	mcast = kzalloc(sizeof(*mcast), GFP_KERNEL);
	if (!mcast)
		goto bail;

	mcast->mcast_addr.mgid = *mgid;
	mcast->mcast_addr.lid = lid;

	INIT_LIST_HEAD(&mcast->qp_list);
	init_waitqueue_head(&mcast->wait);
	atomic_set(&mcast->refcount, 0);

bail:
	return mcast;
}

static void rvt_mcast_free(struct rvt_mcast *mcast)
{
	struct rvt_mcast_qp *p, *tmp;

	list_for_each_entry_safe(p, tmp, &mcast->qp_list, list)
		rvt_mcast_qp_free(p);

	kfree(mcast);
}

/**
 * rvt_mcast_find - search the global table for the given multicast GID/LID
 * NOTE: It is valid to have 1 MLID with multiple MGIDs.  It is not valid
 * to have 1 MGID with multiple MLIDs.
 * @ibp: the IB port structure
 * @mgid: the multicast GID to search for
 * @lid: the multicast LID portion of the multicast address (host order)
 *
 * The caller is responsible for decrementing the reference count if found.
 *
 * Return: NULL if not found.
 */
struct rvt_mcast *rvt_mcast_find(struct rvt_ibport *ibp, union ib_gid *mgid,
				 u16 lid)
{
	struct rb_node *n;
	unsigned long flags;
	struct rvt_mcast *found = NULL;

	spin_lock_irqsave(&ibp->lock, flags);
	n = ibp->mcast_tree.rb_node;
	while (n) {
		int ret;
		struct rvt_mcast *mcast;

		mcast = rb_entry(n, struct rvt_mcast, rb_node);

		ret = memcmp(mgid->raw, mcast->mcast_addr.mgid.raw,
			     sizeof(*mgid));
		if (ret < 0) {
			n = n->rb_left;
		} else if (ret > 0) {
			n = n->rb_right;
		} else {
			/* MGID/MLID must match */
			if (mcast->mcast_addr.lid == lid) {
				atomic_inc(&mcast->refcount);
				found = mcast;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&ibp->lock, flags);
	return found;
}
EXPORT_SYMBOL(rvt_mcast_find);

/*
 * rvt_mcast_add - insert mcast GID into table and attach QP struct
 * @mcast: the mcast GID table
 * @mqp: the QP to attach
 *
 * Return: zero if both were added.  Return EEXIST if the GID was already in
 * the table but the QP was added.  Return ESRCH if the QP was already
 * attached and neither structure was added. Return EINVAL if the MGID was
 * found, but the MLID did NOT match.
 */
static int rvt_mcast_add(struct rvt_dev_info *rdi, struct rvt_ibport *ibp,
			 struct rvt_mcast *mcast, struct rvt_mcast_qp *mqp)
{
	struct rb_node **n = &ibp->mcast_tree.rb_node;
	struct rb_node *pn = NULL;
	int ret;

	spin_lock_irq(&ibp->lock);

	while (*n) {
		struct rvt_mcast *tmcast;
		struct rvt_mcast_qp *p;

		pn = *n;
		tmcast = rb_entry(pn, struct rvt_mcast, rb_node);

		ret = memcmp(mcast->mcast_addr.mgid.raw,
			     tmcast->mcast_addr.mgid.raw,
			     sizeof(mcast->mcast_addr.mgid));
		if (ret < 0) {
			n = &pn->rb_left;
			continue;
		}
		if (ret > 0) {
			n = &pn->rb_right;
			continue;
		}

		if (tmcast->mcast_addr.lid != mcast->mcast_addr.lid) {
			ret = EINVAL;
			goto bail;
		}

		/* Search the QP list to see if this is already there. */
		list_for_each_entry_rcu(p, &tmcast->qp_list, list) {
			if (p->qp == mqp->qp) {
				ret = ESRCH;
				goto bail;
			}
		}
		if (tmcast->n_attached ==
		    rdi->dparms.props.max_mcast_qp_attach) {
			ret = ENOMEM;
			goto bail;
		}

		tmcast->n_attached++;

		list_add_tail_rcu(&mqp->list, &tmcast->qp_list);
		ret = EEXIST;
		goto bail;
	}

	spin_lock(&rdi->n_mcast_grps_lock);
	if (rdi->n_mcast_grps_allocated == rdi->dparms.props.max_mcast_grp) {
		spin_unlock(&rdi->n_mcast_grps_lock);
		ret = ENOMEM;
		goto bail;
	}

	rdi->n_mcast_grps_allocated++;
	spin_unlock(&rdi->n_mcast_grps_lock);

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

/**
 * rvt_attach_mcast - attach a qp to a multicast group
 * @ibqp: Infiniband qp
 * @gid: multicast guid
 * @lid: multicast lid
 *
 * Return: 0 on success
 */
int rvt_attach_mcast(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);
	struct rvt_ibport *ibp = rdi->ports[qp->port_num - 1];
	struct rvt_mcast *mcast;
	struct rvt_mcast_qp *mqp;
	int ret = -ENOMEM;

	if (ibqp->qp_num <= 1 || qp->state == IB_QPS_RESET)
		return -EINVAL;

	/*
	 * Allocate data structures since its better to do this outside of
	 * spin locks and it will most likely be needed.
	 */
	mcast = rvt_mcast_alloc(gid, lid);
	if (!mcast)
		return -ENOMEM;

	mqp = rvt_mcast_qp_alloc(qp);
	if (!mqp)
		goto bail_mcast;

	switch (rvt_mcast_add(rdi, ibp, mcast, mqp)) {
	case ESRCH:
		/* Neither was used: OK to attach the same QP twice. */
		ret = 0;
		goto bail_mqp;
	case EEXIST: /* The mcast wasn't used */
		ret = 0;
		goto bail_mcast;
	case ENOMEM:
		/* Exceeded the maximum number of mcast groups. */
		ret = -ENOMEM;
		goto bail_mqp;
	case EINVAL:
		/* Invalid MGID/MLID pair */
		ret = -EINVAL;
		goto bail_mqp;
	default:
		break;
	}

	return 0;

bail_mqp:
	rvt_mcast_qp_free(mqp);

bail_mcast:
	rvt_mcast_free(mcast);

	return ret;
}

/**
 * rvt_detach_mcast - remove a qp from a multicast group
 * @ibqp: Infiniband qp
 * @gid: multicast guid
 * @lid: multicast lid
 *
 * Return: 0 on success
 */
int rvt_detach_mcast(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);
	struct rvt_ibport *ibp = rdi->ports[qp->port_num - 1];
	struct rvt_mcast *mcast = NULL;
	struct rvt_mcast_qp *p, *tmp, *delp = NULL;
	struct rb_node *n;
	int last = 0;
	int ret = 0;

	if (ibqp->qp_num <= 1)
		return -EINVAL;

	spin_lock_irq(&ibp->lock);

	/* Find the GID in the mcast table. */
	n = ibp->mcast_tree.rb_node;
	while (1) {
		if (!n) {
			spin_unlock_irq(&ibp->lock);
			return -EINVAL;
		}

		mcast = rb_entry(n, struct rvt_mcast, rb_node);
		ret = memcmp(gid->raw, mcast->mcast_addr.mgid.raw,
			     sizeof(*gid));
		if (ret < 0) {
			n = n->rb_left;
		} else if (ret > 0) {
			n = n->rb_right;
		} else {
			/* MGID/MLID must match */
			if (mcast->mcast_addr.lid != lid) {
				spin_unlock_irq(&ibp->lock);
				return -EINVAL;
			}
			break;
		}
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
	rvt_mcast_qp_free(delp);

	if (last) {
		atomic_dec(&mcast->refcount);
		wait_event(mcast->wait, !atomic_read(&mcast->refcount));
		rvt_mcast_free(mcast);
		spin_lock_irq(&rdi->n_mcast_grps_lock);
		rdi->n_mcast_grps_allocated--;
		spin_unlock_irq(&rdi->n_mcast_grps_lock);
	}

	return 0;
}

/**
 * rvt_mcast_tree_empty - determine if any qps are attached to any mcast group
 * @rdi: rvt dev struct
 *
 * Return: in use count
 */
int rvt_mcast_tree_empty(struct rvt_dev_info *rdi)
{
	int i;
	int in_use = 0;

	for (i = 0; i < rdi->dparms.nports; i++)
		if (rdi->ports[i]->mcast_tree.rb_node)
			in_use++;
	return in_use;
}
