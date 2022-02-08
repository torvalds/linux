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
static struct rxe_mcg *__rxe_create_mcg(struct rxe_dev *rxe,
					struct rxe_pool *pool,
					union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	int err;

	mcg = rxe_alloc_locked(pool);
	if (!mcg)
		return ERR_PTR(-ENOMEM);

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err)) {
		rxe_drop_ref(mcg);
		return ERR_PTR(err);
	}

	INIT_LIST_HEAD(&mcg->qp_list);
	mcg->rxe = rxe;

	/* rxe_alloc_locked takes a ref on mcg but that will be
	 * dropped when mcg goes out of scope. We need to take a ref
	 * on the pointer that will be saved in the red-black tree
	 * by rxe_add_key and used to lookup mcg from mgid later.
	 * Adding key makes object visible to outside so this should
	 * be done last after the object is ready.
	 */
	rxe_add_ref(mcg);
	rxe_add_key_locked(mcg, mgid);

	return mcg;
}

static struct rxe_mcg *rxe_get_mcg(struct rxe_dev *rxe,
					 union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	unsigned long flags;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	mcg = rxe_pool_get_key_locked(pool, mgid);
	if (!mcg)
		mcg = __rxe_create_mcg(rxe, pool, mgid);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);

	return mcg;
}

static int rxe_attach_mcg(struct rxe_dev *rxe, struct rxe_qp *qp,
				  struct rxe_mcg *mcg)
{
	struct rxe_mca *mca, *tmp;
	unsigned long flags;
	int err;

	/* check to see if the qp is already a member of the group */
	spin_lock_irqsave(&rxe->mcg_lock, flags);
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
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
	list_for_each_entry(tmp, &mcg->qp_list, qp_list) {
		if (tmp->qp == qp) {
			kfree(mca);
			err = 0;
			goto out;
		}
	}

	/* check limits after checking if already attached */
	if (mcg->num_qp >= rxe->attr.max_mcast_qp_attach) {
		kfree(mca);
		err = -ENOMEM;
		goto out;
	}

	/* protect pointer to qp in mca */
	rxe_add_ref(qp);
	mca->qp = qp;

	atomic_inc(&qp->mcg_num);
	mcg->num_qp++;
	list_add(&mca->qp_list, &mcg->qp_list);

	err = 0;
out:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return err;
}

/* caller should be holding rxe->mcg_lock */
static void __rxe_destroy_mcg(struct rxe_mcg *mcg)
{
	/* first remove mcg from red-black tree then drop ref */
	rxe_drop_key_locked(mcg);
	rxe_drop_ref(mcg);

	rxe_mcast_delete(mcg->rxe, &mcg->mgid);
}

static void rxe_destroy_mcg(struct rxe_mcg *mcg)
{
	struct rxe_dev *rxe = mcg->rxe;
	unsigned long flags;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	__rxe_destroy_mcg(mcg);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	/* nothing left to do for now */
}

static int rxe_detach_mcg(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	struct rxe_mca *mca, *tmp;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	mcg = rxe_pool_get_key_locked(&rxe->mc_grp_pool, mgid);
	if (!mcg) {
		/* we didn't find the mcast group for mgid */
		err = -EINVAL;
		goto out_unlock;
	}

	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			list_del(&mca->qp_list);

			/* if the number of qp's attached to the
			 * mcast group falls to zero go ahead and
			 * tear it down. This will not free the
			 * object since we are still holding a ref
			 * from the get key above.
			 */
			mcg->num_qp--;
			if (mcg->num_qp <= 0)
				__rxe_destroy_mcg(mcg);

			atomic_dec(&qp->mcg_num);

			/* drop the ref from get key. This will free the
			 * object if num_qp is zero.
			 */
			rxe_drop_ref(mcg);
			kfree(mca);
			err = 0;
			goto out_unlock;
		}
	}

	/* we didn't find the qp on the list */
	rxe_drop_ref(mcg);
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
	struct rxe_mcg *mcg;

	/* takes a ref on mcg if successful */
	mcg = rxe_get_mcg(rxe, mgid);
	if (IS_ERR(mcg))
		return PTR_ERR(mcg);

	err = rxe_attach_mcg(rxe, qp, mcg);

	/* if we failed to attach the first qp to mcg tear it down */
	if (mcg->num_qp == 0)
		rxe_destroy_mcg(mcg);

	rxe_drop_ref(mcg);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_detach_mcg(rxe, qp, mgid);
}
