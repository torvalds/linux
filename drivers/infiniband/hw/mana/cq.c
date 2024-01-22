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
	struct gdma_context *gc;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	if (udata->inlen < sizeof(ucmd))
		return -EINVAL;

	if (attr->comp_vector > gc->max_num_queues)
		return -EINVAL;

	cq->comp_vector = attr->comp_vector;

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
	cq->umem = ib_umem_get(ibdev, ucmd.buf_addr, cq->cqe * COMP_ENTRY_SIZE,
			       IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(cq->umem)) {
		err = PTR_ERR(cq->umem);
		ibdev_dbg(ibdev, "Failed to get umem for create cq, err %d\n",
			  err);
		return err;
	}

	err = mana_ib_gd_create_dma_region(mdev, cq->umem, &cq->gdma_region);
	if (err) {
		ibdev_dbg(ibdev,
			  "Failed to create dma region for create cq, %d\n",
			  err);
		goto err_release_umem;
	}

	ibdev_dbg(ibdev,
		  "mana_ib_gd_create_dma_region ret %d gdma_region 0x%llx\n",
		  err, cq->gdma_region);

	/*
	 * The CQ ID is not known at this time. The ID is generated at create_qp
	 */
	cq->id = INVALID_QUEUE_ID;

	return 0;

err_release_umem:
	ib_umem_release(cq->umem);
	return err;
}

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_dev *mdev;
	struct gdma_context *gc;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	err = mana_ib_gd_destroy_dma_region(mdev, cq->gdma_region);
	if (err) {
		ibdev_dbg(ibdev,
			  "Failed to destroy dma region, %d\n", err);
		return err;
	}

	if (cq->id != INVALID_QUEUE_ID) {
		kfree(gc->cq_table[cq->id]);
		gc->cq_table[cq->id] = NULL;
	}

	ib_umem_release(cq->umem);

	return 0;
}

void mana_ib_cq_handler(void *ctx, struct gdma_queue *gdma_cq)
{
	struct mana_ib_cq *cq = ctx;

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}
