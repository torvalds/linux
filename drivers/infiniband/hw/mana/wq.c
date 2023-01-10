// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

struct ib_wq *mana_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata)
{
	struct mana_ib_dev *mdev =
		container_of(pd->device, struct mana_ib_dev, ib_dev);
	struct mana_ib_create_wq ucmd = {};
	struct mana_ib_wq *wq;
	struct ib_umem *umem;
	int err;

	if (udata->inlen < sizeof(ucmd))
		return ERR_PTR(-EINVAL);

	err = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to copy from udata for create wq, %d\n", err);
		return ERR_PTR(err);
	}

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return ERR_PTR(-ENOMEM);

	ibdev_dbg(&mdev->ib_dev, "ucmd wq_buf_addr 0x%llx\n", ucmd.wq_buf_addr);

	umem = ib_umem_get(pd->device, ucmd.wq_buf_addr, ucmd.wq_buf_size,
			   IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(umem)) {
		err = PTR_ERR(umem);
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to get umem for create wq, err %d\n", err);
		goto err_free_wq;
	}

	wq->umem = umem;
	wq->wqe = init_attr->max_wr;
	wq->wq_buf_size = ucmd.wq_buf_size;
	wq->rx_object = INVALID_MANA_HANDLE;

	err = mana_ib_gd_create_dma_region(mdev, wq->umem, &wq->gdma_region);
	if (err) {
		ibdev_dbg(&mdev->ib_dev,
			  "Failed to create dma region for create wq, %d\n",
			  err);
		goto err_release_umem;
	}

	ibdev_dbg(&mdev->ib_dev,
		  "mana_ib_gd_create_dma_region ret %d gdma_region 0x%llx\n",
		  err, wq->gdma_region);

	/* WQ ID is returned at wq_create time, doesn't know the value yet */

	return &wq->ibwq;

err_release_umem:
	ib_umem_release(umem);

err_free_wq:
	kfree(wq);

	return ERR_PTR(err);
}

int mana_ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata)
{
	/* modify_wq is not supported by this version of the driver */
	return -EOPNOTSUPP;
}

int mana_ib_destroy_wq(struct ib_wq *ibwq, struct ib_udata *udata)
{
	struct mana_ib_wq *wq = container_of(ibwq, struct mana_ib_wq, ibwq);
	struct ib_device *ib_dev = ibwq->device;
	struct mana_ib_dev *mdev;

	mdev = container_of(ib_dev, struct mana_ib_dev, ib_dev);

	mana_ib_gd_destroy_dma_region(mdev, wq->gdma_region);
	ib_umem_release(wq->umem);

	kfree(wq);

	return 0;
}

int mana_ib_create_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_table,
				 struct ib_rwq_ind_table_init_attr *init_attr,
				 struct ib_udata *udata)
{
	/*
	 * There is no additional data in ind_table to be maintained by this
	 * driver, do nothing
	 */
	return 0;
}

int mana_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl)
{
	/*
	 * There is no additional data in ind_table to be maintained by this
	 * driver, do nothing
	 */
	return 0;
}
