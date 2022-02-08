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

/**
 * __rxe_insert_mcg - insert an mcg into red-black tree (rxe->mcg_tree)
 * @mcg: mcg object with an embedded red-black tree node
 *
 * Context: caller must hold a reference to mcg and rxe->mcg_lock and
 * is responsible to avoid adding the same mcg twice to the tree.
 */
static void __rxe_insert_mcg(struct rxe_mcg *mcg)
{
	struct rb_root *tree = &mcg->rxe->mcg_tree;
	struct rb_node **link = &tree->rb_node;
	struct rb_node *node = NULL;
	struct rxe_mcg *tmp;
	int cmp;

	while (*link) {
		node = *link;
		tmp = rb_entry(node, struct rxe_mcg, node);

		cmp = memcmp(&tmp->mgid, &mcg->mgid, sizeof(mcg->mgid));
		if (cmp > 0)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&mcg->node, node, link);
	rb_insert_color(&mcg->node, tree);
}

/**
 * __rxe_remove_mcg - remove an mcg from red-black tree holding lock
 * @mcg: mcast group object with an embedded red-black tree node
 *
 * Context: caller must hold a reference to mcg and rxe->mcg_lock
 */
static void __rxe_remove_mcg(struct rxe_mcg *mcg)
{
	rb_erase(&mcg->node, &mcg->rxe->mcg_tree);
}

/**
 * __rxe_lookup_mcg - lookup mcg in rxe->mcg_tree while holding lock
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Context: caller must hold rxe->mcg_lock
 * Returns: mcg on success and takes a ref to mcg else NULL
 */
static struct rxe_mcg *__rxe_lookup_mcg(struct rxe_dev *rxe,
					union ib_gid *mgid)
{
	struct rb_root *tree = &rxe->mcg_tree;
	struct rxe_mcg *mcg;
	struct rb_node *node;
	int cmp;

	node = tree->rb_node;

	while (node) {
		mcg = rb_entry(node, struct rxe_mcg, node);

		cmp = memcmp(&mcg->mgid, mgid, sizeof(*mgid));

		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			break;
	}

	if (node) {
		kref_get(&mcg->ref_cnt);
		return mcg;
	}

	return NULL;
}

/**
 * rxe_lookup_mcg - lookup up mcg in red-back tree
 * @rxe: rxe device object
 * @mgid: multicast IP address
 *
 * Returns: mcg if found else NULL
 */
struct rxe_mcg *rxe_lookup_mcg(struct rxe_dev *rxe, union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	unsigned long flags;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	mcg = __rxe_lookup_mcg(rxe, mgid);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);

	return mcg;
}

/**
 * __rxe_init_mcg - initialize a new mcg
 * @rxe: rxe device
 * @mgid: multicast address as a gid
 * @mcg: new mcg object
 *
 * Context: caller should hold rxe->mcg lock
 * Returns: 0 on success else an error
 */
static int __rxe_init_mcg(struct rxe_dev *rxe, union ib_gid *mgid,
			  struct rxe_mcg *mcg)
{
	int err;

	err = rxe_mcast_add(rxe, mgid);
	if (unlikely(err))
		return err;

	kref_init(&mcg->ref_cnt);
	memcpy(&mcg->mgid, mgid, sizeof(mcg->mgid));
	INIT_LIST_HEAD(&mcg->qp_list);
	mcg->rxe = rxe;

	/* caller holds a ref on mcg but that will be
	 * dropped when mcg goes out of scope. We need to take a ref
	 * on the pointer that will be saved in the red-black tree
	 * by __rxe_insert_mcg and used to lookup mcg from mgid later.
	 * Inserting mcg makes it visible to outside so this should
	 * be done last after the object is ready.
	 */
	kref_get(&mcg->ref_cnt);
	__rxe_insert_mcg(mcg);

	return 0;
}

/**
 * rxe_get_mcg - lookup or allocate a mcg
 * @rxe: rxe device object
 * @mgid: multicast IP address as a gid
 *
 * Returns: mcg on success else ERR_PTR(error)
 */
static struct rxe_mcg *rxe_get_mcg(struct rxe_dev *rxe, union ib_gid *mgid)
{
	struct rxe_mcg *mcg, *tmp;
	unsigned long flags;
	int err;

	if (rxe->attr.max_mcast_grp == 0)
		return ERR_PTR(-EINVAL);

	/* check to see if mcg already exists */
	mcg = rxe_lookup_mcg(rxe, mgid);
	if (mcg)
		return mcg;

	/* speculative alloc of new mcg */
	mcg = kzalloc(sizeof(*mcg), GFP_KERNEL);
	if (!mcg)
		return ERR_PTR(-ENOMEM);

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	/* re-check to see if someone else just added it */
	tmp = __rxe_lookup_mcg(rxe, mgid);
	if (tmp) {
		kfree(mcg);
		mcg = tmp;
		goto out;
	}

	if (atomic_inc_return(&rxe->mcg_num) > rxe->attr.max_mcast_grp) {
		err = -ENOMEM;
		goto err_dec;
	}

	err = __rxe_init_mcg(rxe, mgid, mcg);
	if (err)
		goto err_dec;
out:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return mcg;

err_dec:
	atomic_dec(&rxe->mcg_num);
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	kfree(mcg);
	return ERR_PTR(err);
}

/**
 * rxe_cleanup_mcg - cleanup mcg for kref_put
 * @kref:
 */
void rxe_cleanup_mcg(struct kref *kref)
{
	struct rxe_mcg *mcg = container_of(kref, typeof(*mcg), ref_cnt);

	kfree(mcg);
}

/**
 * __rxe_destroy_mcg - destroy mcg object holding rxe->mcg_lock
 * @mcg: the mcg object
 *
 * Context: caller is holding rxe->mcg_lock
 * no qp's are attached to mcg
 */
static void __rxe_destroy_mcg(struct rxe_mcg *mcg)
{
	struct rxe_dev *rxe = mcg->rxe;

	/* remove mcg from red-black tree then drop ref */
	__rxe_remove_mcg(mcg);
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	rxe_mcast_delete(mcg->rxe, &mcg->mgid);
	atomic_dec(&rxe->mcg_num);
}

/**
 * rxe_destroy_mcg - destroy mcg object
 * @mcg: the mcg object
 *
 * Context: no qp's are attached to mcg
 */
static void rxe_destroy_mcg(struct rxe_mcg *mcg)
{
	unsigned long flags;

	spin_lock_irqsave(&mcg->rxe->mcg_lock, flags);
	__rxe_destroy_mcg(mcg);
	spin_unlock_irqrestore(&mcg->rxe->mcg_lock, flags);
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
	if (atomic_inc_return(&mcg->qp_num) > rxe->attr.max_mcast_qp_attach) {
		atomic_dec(&mcg->qp_num);
		kfree(mca);
		err = -ENOMEM;
		goto out;
	}

	/* protect pointer to qp in mca */
	rxe_add_ref(qp);
	mca->qp = qp;

	atomic_inc(&qp->mcg_num);
	list_add(&mca->qp_list, &mcg->qp_list);

	err = 0;
out:
	spin_unlock_irqrestore(&rxe->mcg_lock, flags);
	return err;
}

static int rxe_detach_mcg(struct rxe_dev *rxe, struct rxe_qp *qp,
				   union ib_gid *mgid)
{
	struct rxe_mcg *mcg;
	struct rxe_mca *mca, *tmp;
	unsigned long flags;
	int err;

	mcg = rxe_lookup_mcg(rxe, mgid);
	if (!mcg)
		return -EINVAL;

	spin_lock_irqsave(&rxe->mcg_lock, flags);
	list_for_each_entry_safe(mca, tmp, &mcg->qp_list, qp_list) {
		if (mca->qp == qp) {
			list_del(&mca->qp_list);
			atomic_dec(&qp->mcg_num);
			rxe_drop_ref(qp);

			/* if the number of qp's attached to the
			 * mcast group falls to zero go ahead and
			 * tear it down. This will not free the
			 * object since we are still holding a ref
			 * from the get key above.
			 */
			if (atomic_dec_return(&mcg->qp_num) <= 0)
				__rxe_destroy_mcg(mcg);

			/* drop the ref from get key. This will free the
			 * object if qp_num is zero.
			 */
			kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);
			kfree(mca);
			err = 0;
			goto out_unlock;
		}
	}

	/* we didn't find the qp on the list */
	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);
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
	if (atomic_read(&mcg->qp_num) == 0)
		rxe_destroy_mcg(mcg);

	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);
	return err;
}

int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid)
{
	struct rxe_dev *rxe = to_rdev(ibqp->device);
	struct rxe_qp *qp = to_rqp(ibqp);

	return rxe_detach_mcg(rxe, qp, mgid);
}
