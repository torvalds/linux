// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

static int rxe_mcast_add(struct rxe_dev *rxe, union ib_gid *mgid)
{
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);

	return dev_mc_add(rxe->ndev, ll_addr);
}

static int rxe_mcast_delete(struct rxe_dev *rxe, union ib_gid *mgid)
{
	unsigned char ll_addr[ETH_ALEN];

	ipv6_eth_mc_map((struct in6_addr *)mgid->raw, ll_addr);

	return dev_mc_del(rxe->ndev, ll_addr);
}

/* caller should hold rxe->mcg_lock */
static struct rxe_mcg *__rxe_create_grp(struct rxe_dev *rxe,
					struct rxe_pool *pool,
					union ib_gid *mgid)
{
	struct rxe_mcg *grp;
	int err;

	grp = rxe_alloc_locked(pool);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err)) {
		rxe_drop_ref(grp);
		return ERR_PTR(err);
	}

	INIT_LIST_HEAD(&grp->qp_list);
	grp->rxe = rxe;

	/* rxe_alloc_locked takes a ref on grp but that will be
	 * dropped when grp goes out of scope. We need to take a ref
	 * on the pointer that will be saved in the red-black tree
	 * by rxe_add_key and used to lookup grp from mgid later.
	 * Adding key makes object visible to outside so this should
	 * be done last after the object is ready.
	 */
	rxe_add_ref(grp);
	rxe_add_key_locked(grp, mgid);

	return grp;
}

static struct rxe_mcg *rxe_mcast_get_grp(struct rxe_dev *rxe,
					 union ib_gid *mgid)
{
	struct rxe_mcg *grp;
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	unsigned long flags;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	grp = rxe_pool_get_key_locked(pool, mgid);
	if (!grp)
		grp = __rxe_create_grp(rxe, pool, mgid);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);

	return grp;
}

static int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
				  struct rxe_mcg *grp)
{
	struct rxe_mca *mca, *tmp;
	unsigned long flags;
	int err;

	/* check to see if the qp is already a member of the group */
	spin_lock_irqsave(&rxe->mcg_lock, flags);
	list_for_each_entry(mca, &grp->qp_list, qp_list) {
		if (mca->qp == qp) {
			spin_unlock_irqrestore(&rxe->mcg_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);

	/* speculative alloc new mca without using GFP_ATOMIC */
	mca = kzalloc(sizeof(*mca), GFP_KERNEL);
	if (!mca)
		return -ENOMEM;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	/* re-check to see if someone else just attached qp */
	list_for_each_entry(tmp, &grp->qp_list, qp_list) {
		if (tmp->qp == qp) {
			kfree(mca);
			err = 0;
			goto out;
		}
	}

	/* check limits after checking if already attached */
	if (grp->num_qp >= rxe->attr.max_mcast_qp_attach) {
		kfree(mca);
		err = -ENOMEM;
		goto out;
	}

	/* protect pointer to qp in mca */
	rxe_add_ref(qp);
	mca->qp = qp;

	atomic_inc(&qp->mcg_num);
	grp->num_qp++;
	list_add(&mca->qp_list, &grp->qp_list);

	err = 0;
out:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return err;
}

/* caller should be holding rxe->mcg_lock */
static void __rxe_destroy_grp(struct rxe_mcg *grp)
{
	/* first remove grp from red-black tree then drop ref */
	rxe_drop_key_locked(grp);
	rxe_drop_ref(grp);

	rxe_mcast_delete(grp->rxe, &grp->mgid);
}

static void rxe_destroy_grp(struct rxe_mcg *grp)
{
	struct rxe_dev *rxe = grp->rxe;
	unsigned long flags;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	__rxe_destroy_grp(grp);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	/* nothing left to do for now */
}

static int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *grp;
	struct rxe_mca *mca, *tmp;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	grp = rxe_pool_get_key_locked(&rxe->mc_grp_pool, mgid);
	if (!grp) {
		/* we didn't find the mcast group for mgid */
		err = -EINVAL;
		goto out_unlock;
	}

	list_for_each_entry_safe(mca, tmp, &grp->qp_list, qp_list) {
		if (mca->qp == qp) {
			list_del(&mca->qp_list);

			/* if the number of qp's attached to the
			 * mcast group falls to zero go ahead and
			 * tear it down. This will not free the
			 * object since we are still holding a ref
			 * from the get key above.
			 */
			grp->num_qp--;
			if (grp->num_qp <= 0)
				__rxe_destroy_grp(grp);

			atomic_dec(&qp->mcg_num);

			/* drop the ref from get key. This will free the
			 * object if num_qp is zero.
			 */
			rxe_drop_ref(grp);
			kfree(mca);
			err = 0;
			goto out_unlock;
		}
	}

	/* we didn't find the qp on the list */
	rxe_drop_ref(grp);
	err = -EINVAL;

out_unlock:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return err;
}

int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	int err;
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *grp;

	/* takes a ref on grp if successful */
	grp = rxe_mcast_get_grp(rxe, mgid);
	if (IS_ERR(grp))
		return PTR_ERR(grp);

	err = rxe_mcast_add_grp_elem(rxe, qp, grp);

	/* if we failed to attach the first qp to grp tear it down */
	if (grp->num_qp == 0)
		rxe_destroy_grp(grp);

	rxe_drop_ref(grp);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_mcast_drop_grp_elem(rxe, qp, mgid);
}
