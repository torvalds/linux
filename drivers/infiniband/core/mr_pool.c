// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
 */
#include <rdma/ib_verbs.h>
#include <rdma/mr_pool.h>

struct ib_mr *ib_mr_pool_get(struct ib_qp *qp, struct list_head *list)
{
	struct ib_mr *mr;
	unsigned long flags;

	spin_lock_irqsave(&qp->mr_lock, flags);
	mr = list_first_entry_or_null(list, struct ib_mr, qp_entry);
	if (mr) {
		list_del(&mr->qp_entry);
		qp->mrs_used++;
	}
	spin_unlock_irqrestore(&qp->mr_lock, flags);

	return mr;
}
EXPORT_SYMBOL(ib_mr_pool_get);

void ib_mr_pool_put(struct ib_qp *qp, struct list_head *list, struct ib_mr *mr)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->mr_lock, flags);
	list_add(&mr->qp_entry, list);
	qp->mrs_used--;
	spin_unlock_irqrestore(&qp->mr_lock, flags);
}
EXPORT_SYMBOL(ib_mr_pool_put);

int ib_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
		enum ib_mr_type type, u32 max_num_sg)
{
	struct ib_mr *mr;
	unsigned long flags;
	int ret, i;

	for (i = 0; i < nr; i++) {
		mr = ib_alloc_mr(qp->pd, type, max_num_sg);
		if (IS_ERR(mr)) {
			ret = PTR_ERR(mr);
			goto out;
		}

		spin_lock_irqsave(&qp->mr_lock, flags);
		list_add_tail(&mr->qp_entry, list);
		spin_unlock_irqrestore(&qp->mr_lock, flags);
	}

	return 0;
out:
	ib_mr_pool_destroy(qp, list);
	return ret;
}
EXPORT_SYMBOL(ib_mr_pool_init);

void ib_mr_pool_destroy(struct ib_qp *qp, struct list_head *list)
{
	struct ib_mr *mr;
	unsigned long flags;

	spin_lock_irqsave(&qp->mr_lock, flags);
	while (!list_empty(list)) {
		mr = list_first_entry(list, struct ib_mr, qp_entry);
		list_del(&mr->qp_entry);

		spin_unlock_irqrestore(&qp->mr_lock, flags);
		ib_dereg_mr(mr);
		spin_lock_irqsave(&qp->mr_lock, flags);
	}
	spin_unlock_irqrestore(&qp->mr_lock, flags);
}
EXPORT_SYMBOL(ib_mr_pool_destroy);
