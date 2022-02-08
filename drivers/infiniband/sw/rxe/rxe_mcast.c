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
static struct rxe_mcg *create_grp(struct rxe_dev *rxe,
				     struct rxe_pool *pool,
				     union ib_gid *mgid)
{
	int err;
	struct rxe_mcg *grp;

	grp = rxe_alloc_locked(&rxe->mc_grp_pool);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&grp->qp_list);
	grp->rxe = rxe;
	rxe_add_key_locked(grp, mgid);

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err)) {
		rxe_drop_key_locked(grp);
		rxe_drop_ref(grp);
		return ERR_PTR(err);
	}

	return grp;
}

static int rxe_mcast_get_grp(struct rxe_dev *rxe, union ib_gid *mgid,
			     struct rxe_mcg **grp_p)
{
	int err;
	struct rxe_mcg *grp;
	struct rxe_pool *pool = &rxe->mc_grp_pool;
	unsigned long flags;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return -EINVAL;

	spin_lock_irqsave(&rxe->mcg_lock, flags);

	grp = rxe_pool_get_key_locked(pool, mgid);
	if (grp)
		goto done;

	grp = create_grp(rxe, pool, mgid);
	if (IS_ERR(grp)) {
		spin_unlock_irqrestore(&rxe->mcg_lock, flags);
		err = PTR_ERR(grp);
		return err;
	}

done:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	*grp_p = grp;
	return 0;
}

static int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct rxe_mcg *grp)
{
	int err;
	struct rxe_mca *elem;
	unsigned long flags;

	/* check to see of the qp is already a member of the group */
	spin_lock_irqsave(&rxe->mcg_lock, flags);
	list_for_each_entry(elem, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			err = 0;
			goto out;
		}
	}

	if (grp->num_qp >= rxe->attr.max_mcast_qp_attach) {
		err = -ENOMEM;
		goto out;
	}

	elem = rxe_alloc_locked(&rxe->mc_elem_pool);
	if (!elem) {
		err = -ENOMEM;
		goto out;
	}

	/* each qp holds a ref on the grp */
	rxe_add_ref(grp);

	grp->num_qp++;
	elem->qp = qp;
	atomic_inc(&qp->mcg_num);

	list_add(&elem->qp_list, &grp->qp_list);

	err = 0;
out:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return err;
}

static int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *grp;
	struct rxe_mca *elem, *tmp;
	unsigned long flags;

	grp = rxe_pool_get_key(&rxe->mc_grp_pool, mgid);
	if (!grp)
		goto err1;

	spin_lock_irqsave(&rxe->mcg_lock, flags);

	list_for_each_entry_safe(elem, tmp, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			list_del(&elem->qp_list);
			grp->num_qp--;
			atomic_dec(&qp->mcg_num);

			spin_unlock_irqrestore(&rxe->mcg_lock, flags);
			rxe_drop_ref(elem);
			rxe_drop_ref(grp);	/* ref held by QP */
			rxe_drop_ref(grp);	/* ref from get_key */
			return 0;
		}
	}

	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	rxe_drop_ref(grp);			/* ref from get_key */
err1:
	return -EINVAL;
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mcg *grp = container_of(elem, typeof(*grp), elem);
	struct rxe_dev *rxe = grp->rxe;

	rxe_drop_key(grp);
	rxe_mcast_delete(rxe, &grp->mgid);
}

int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	int err;
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);
	struct rxe_mcg *grp;

	/* takes a ref on grp if successful */
	err = rxe_mcast_get_grp(rxe, mgid, &grp);
	if (err)
		return err;

	err = rxe_mcast_add_grp_elem(rxe, qp, grp);

	rxe_drop_ref(grp);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_mcast_drop_grp_elem(rxe, qp, mgid);
}
