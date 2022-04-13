// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include "rxe.h"
#include "rxe_loc.h"

/* caller should hold mc_grp_pool->pool_lock */
static struct rxe_mc_grp *create_grp(struct rxe_dev *rxe,
				     struct rxe_pool *pool,
				     union ib_gid *mgid)
{
	int err;
	struct rxe_mc_grp *grp;

	grp = rxe_alloc_locked(&rxe->mc_grp_pool);
	if (!grp)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&grp->qp_list);
	spin_lock_init(&grp->mcg_lock);
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

int rxe_mcast_get_grp(struct rxe_dev *rxe, union ib_gid *mgid,
		      struct rxe_mc_grp **grp_p)
{
	int err;
	struct rxe_mc_grp *grp;
	struct rxe_pool *pool = &rxe->mc_grp_pool;

	if (rxe->attr.max_mcast_qp_attach == 0)
		return -EINVAL;

	write_lock_bh(&pool->pool_lock);

	grp = rxe_pool_get_key_locked(pool, mgid);
	if (grp)
		goto done;

	grp = create_grp(rxe, pool, mgid);
	if (IS_ERR(grp)) {
		write_unlock_bh(&pool->pool_lock);
		err = PTR_ERR(grp);
		return err;
	}

done:
	write_unlock_bh(&pool->pool_lock);
	*grp_p = grp;
	return 0;
}

int rxe_mcast_add_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct rxe_mc_grp *grp)
{
	int err;
	struct rxe_mc_elem *elem;

	/* check to see of the qp is already a member of the group */
	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);
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
	elem->grp = grp;

	list_add(&elem->qp_list, &grp->qp_list);
	list_add(&elem->grp_list, &qp->grp_list);

	err = 0;
out:
	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);
	return err;
}

int rxe_mcast_drop_grp_elem(struct rxe_dev *rxe, struct rxe_qp *qp,
			    union ib_gid *mgid)
{
	struct rxe_mc_grp *grp;
	struct rxe_mc_elem *elem, *tmp;

	grp = rxe_pool_get_key(&rxe->mc_grp_pool, mgid);
	if (!grp)
		goto err1;

	spin_lock_bh(&qp->grp_lock);
	spin_lock_bh(&grp->mcg_lock);

	list_for_each_entry_safe(elem, tmp, &grp->qp_list, qp_list) {
		if (elem->qp == qp) {
			list_del(&elem->qp_list);
			list_del(&elem->grp_list);
			grp->num_qp--;

			spin_unlock_bh(&grp->mcg_lock);
			spin_unlock_bh(&qp->grp_lock);
			rxe_drop_ref(elem);
			rxe_drop_ref(grp);	/* ref held by QP */
			rxe_drop_ref(grp);	/* ref from get_key */
			return 0;
		}
	}

	spin_unlock_bh(&grp->mcg_lock);
	spin_unlock_bh(&qp->grp_lock);
	rxe_drop_ref(grp);			/* ref from get_key */
err1:
	return -EINVAL;
}

void rxe_drop_all_mcast_groups(struct rxe_qp *qp)
{
	struct rxe_mc_grp *grp;
	struct rxe_mc_elem *elem;

	while (1) {
		spin_lock_bh(&qp->grp_lock);
		if (list_empty(&qp->grp_list)) {
			spin_unlock_bh(&qp->grp_lock);
			break;
		}
		elem = list_first_entry(&qp->grp_list, struct rxe_mc_elem,
					grp_list);
		list_del(&elem->grp_list);
		spin_unlock_bh(&qp->grp_lock);

		grp = elem->grp;
		spin_lock_bh(&grp->mcg_lock);
		list_del(&elem->qp_list);
		grp->num_qp--;
		spin_unlock_bh(&grp->mcg_lock);
		rxe_drop_ref(grp);
		rxe_drop_ref(elem);
	}
}

void rxe_mc_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mc_grp *grp = container_of(elem, typeof(*grp), elem);
	struct rxe_dev *rxe = grp->rxe;

	rxe_drop_key(grp);
	rxe_mcast_delete(rxe, &grp->mgid);
}
