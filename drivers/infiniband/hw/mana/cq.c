// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct mana_ib_create_cq_resp resp = {};
	struct mana_ib_ucontext *mana_ucontext;
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_create_cq ucmd = {};
	struct mana_ib_dev *mdev;
	bool is_rnic_cq;
	u32 doorbell;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	cq->comp_vector = attr->comp_vector % ibdev->num_comp_vectors;
	cq->cq_handle = INVALID_MANA_HANDLE;

	if (udata->inlen < offsetof(struct mana_ib_create_cq, flags))
		return -EINVAL;

	err = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
	if (err) {
		ibdev_dbg(ibdev,
			  "Failed to copy from udata for create cq, %d\n", err);
		return err;
	}

	is_rnic_cq = !!(ucmd.flags & MANA_IB_CREATE_RNIC_CQ);

	if (!is_rnic_cq && attr->cqe > mdev->adapter_caps.max_qp_wr) {
		ibdev_dbg(ibdev, "CQE %d exceeding limit\n", attr->cqe);
		return -EINVAL;
	}

	cq->cqe = attr->cqe;
	err = mana_ib_create_queue(mdev, ucmd.buf_addr, cq->cqe * COMP_ENTRY_SIZE, &cq->queue);
	if (err) {
		ibdev_dbg(ibdev, "Failed to create queue for create cq, %d\n", err);
		return err;
	}

	mana_ucontext = rdma_udata_to_drv_context(udata, struct mana_ib_ucontext,
						  ibucontext);
	doorbell = mana_ucontext->doorbell;

	if (is_rnic_cq) {
		err = mana_ib_gd_create_cq(mdev, cq, doorbell);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create RNIC cq, %d\n", err);
			goto err_destroy_queue;
		}

		err = mana_ib_install_cq_cb(mdev, cq);
		if (err) {
			ibdev_dbg(ibdev, "Failed to install cq callback, %d\n", err);
			goto err_destroy_rnic_cq;
		}
	}

	resp.cqid = cq->queue.id;
	err = ib_copy_to_udata(udata, &resp, min(sizeof(resp), udata->outlen));
	if (err) {
		ibdev_dbg(&mdev->ib_dev, "Failed to copy to udata, %d\n", err);
		goto err_remove_cq_cb;
	}

	return 0;

err_remove_cq_cb:
	mana_ib_remove_cq_cb(mdev, cq);
err_destroy_rnic_cq:
	mana_ib_gd_destroy_cq(mdev, cq);
err_destroy_queue:
	mana_ib_destroy_queue(mdev, &cq->queue);

	return err;
}

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_dev *mdev;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	mana_ib_remove_cq_cb(mdev, cq);

	/* Ignore return code as there is not much we can do about it.
	 * The error message is printed inside.
	 */
	mana_ib_gd_destroy_cq(mdev, cq);

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

	if (cq->queue.id >= gc->max_num_cqs)
		return -EINVAL;
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

void mana_ib_remove_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);

	if (cq->queue.id >= gc->max_num_cqs || cq->queue.id == INVALID_QUEUE_ID)
		return;

	kfree(gc->cq_table[cq->queue.id]);
	gc->cq_table[cq->queue.id] = NULL;
}
