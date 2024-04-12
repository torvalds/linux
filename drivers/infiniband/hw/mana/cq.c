// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_create_cq ucmd = {};
	struct mana_ib_dev *mdev;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	if (udata->inlen < sizeof(ucmd))
		return -EINVAL;

	cq->comp_vector = attr->comp_vector % ibdev->num_comp_vectors;

	err = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
	if (err) {
		ibdev_dbg(ibdev,
			  "Failed to copy from udata for create cq, %d\n", err);
		return err;
	}

	if (attr->cqe > mdev->adapter_caps.max_qp_wr) {
		ibdev_dbg(ibdev, "CQE %d exceeding limit\n", attr->cqe);
		return -EINVAL;
	}

	cq->cqe = attr->cqe;
	err = mana_ib_create_queue(mdev, ucmd.buf_addr, cq->cqe * COMP_ENTRY_SIZE, &cq->queue);
	if (err) {
		ibdev_dbg(ibdev, "Failed to create queue for create cq, %d\n", err);
		return err;
	}

	return 0;
}

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_dev *mdev;
	struct gdma_context *gc;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	if (cq->queue.id != INVALID_QUEUE_ID) {
		kfree(gc->cq_table[cq->queue.id]);
		gc->cq_table[cq->queue.id] = NULL;
	}

	mana_ib_destroy_queue(mdev, &cq->queue);

	return 0;
}

static void mana_ib_cq_handler(void *ctx, struct gdma_queue *gdma_cq)
{
	struct mana_ib_cq *cq = ctx;

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

int mana_ib_install_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct gdma_queue *gdma_cq;

	/* Create CQ table entry */
	WARN_ON(gc->cq_table[cq->queue.id]);
	gdma_cq = kzalloc(sizeof(*gdma_cq), GFP_KERNEL);
	if (!gdma_cq)
		return -ENOMEM;

	gdma_cq->cq.context = cq;
	gdma_cq->type = GDMA_CQ;
	gdma_cq->cq.callback = mana_ib_cq_handler;
	gdma_cq->id = cq->queue.id;
	gc->cq_table[cq->queue.id] = gdma_cq;
	return 0;
}
