// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

#define VALID_MR_FLAGS                                                         \
	(IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_READ)

static enum gdma_mr_access_flags
mana_ib_verbs_to_gdma_access_flags(int access_flags)
{
	enum gdma_mr_access_flags flags = GDMA_ACCESS_FLAG_LOCAL_READ;

	if (access_flags & IB_ACCESS_LOCAL_WRITE)
		flags |= GDMA_ACCESS_FLAG_LOCAL_WRITE;

	if (access_flags & IB_ACCESS_REMOTE_WRITE)
		flags |= GDMA_ACCESS_FLAG_REMOTE_WRITE;

	if (access_flags & IB_ACCESS_REMOTE_READ)
		flags |= GDMA_ACCESS_FLAG_REMOTE_READ;

	return flags;
}

static int mana_ib_gd_create_mr(struct mana_ib_dev *dev, struct mana_ib_mr *mr,
				struct gdma_create_mr_params *mr_params)
{
	struct gdma_create_mr_response resp = {};
	struct gdma_create_mr_request req = {};
	struct gdma_context *gc = mdev_to_gc(dev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_CREATE_MR, sizeof(req),
			     sizeof(resp));
	req.pd_handle = mr_params->pd_handle;
	req.mr_type = mr_params->mr_type;

	switch (mr_params->mr_type) {
	case GDMA_MR_TYPE_GVA:
		req.gva.dma_region_handle = mr_params->gva.dma_region_handle;
		req.gva.virtual_address = mr_params->gva.virtual_address;
		req.gva.access_flags = mr_params->gva.access_flags;
		break;

	default:
		ibdev_dbg(&dev->ib_dev,
			  "invalid param (GDMA_MR_TYPE) passed, type %d\n",
			  req.mr_type);
		return -EINVAL;
	}

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);

	if (err || resp.hdr.status) {
		ibdev_dbg(&dev->ib_dev, "Failed to create mr %d, %u", err,
			  resp.hdr.status);
		if (!err)
			err = -EPROTO;

		return err;
	}

	mr->ibmr.lkey = resp.lkey;
	mr->ibmr.rkey = resp.rkey;
	mr->mr_handle = resp.mr_handle;

	return 0;
}

static int mana_ib_gd_destroy_mr(struct mana_ib_dev *dev, u64 mr_handle)
{
	struct gdma_destroy_mr_response resp = {};
	struct gdma_destroy_mr_request req = {};
	struct gdma_context *gc = mdev_to_gc(dev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_DESTROY_MR, sizeof(req),
			     sizeof(resp));

	req.mr_handle = mr_handle;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev, "Failed to destroy MR: %d, 0x%x\n", err,
			resp.hdr.status);
		if (!err)
			err = -EPROTO;
		return err;
	}

	return 0;
}

struct ib_mr *mana_ib_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 length,
				  u64 iova, int access_flags,
				  struct ib_udata *udata)
{
	struct mana_ib_pd *pd = container_of(ibpd, struct mana_ib_pd, ibpd);
	struct gdma_create_mr_params mr_params = {};
	struct ib_device *ibdev = ibpd->device;
	struct mana_ib_dev *dev;
	struct mana_ib_mr *mr;
	u64 dma_region_handle;
	int err;

	dev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	ibdev_dbg(ibdev,
		  "start 0x%llx, iova 0x%llx length 0x%llx access_flags 0x%x",
		  start, iova, length, access_flags);

	if (access_flags & ~VALID_MR_FLAGS)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = ib_umem_get(ibdev, start, length, access_flags);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		ibdev_dbg(ibdev,
			  "Failed to get umem for register user-mr, %d\n", err);
		goto err_free;
	}

	err = mana_ib_gd_create_dma_region(dev, mr->umem, &dma_region_handle);
	if (err) {
		ibdev_dbg(ibdev, "Failed create dma region for user-mr, %d\n",
			  err);
		goto err_umem;
	}

	ibdev_dbg(ibdev,
		  "mana_ib_gd_create_dma_region ret %d gdma_region %llx\n", err,
		  dma_region_handle);

	mr_params.pd_handle = pd->pd_handle;
	mr_params.mr_type = GDMA_MR_TYPE_GVA;
	mr_params.gva.dma_region_handle = dma_region_handle;
	mr_params.gva.virtual_address = iova;
	mr_params.gva.access_flags =
		mana_ib_verbs_to_gdma_access_flags(access_flags);

	err = mana_ib_gd_create_mr(dev, mr, &mr_params);
	if (err)
		goto err_dma_region;

	/*
	 * There is no need to keep track of dma_region_handle after MR is
	 * successfully created. The dma_region_handle is tracked in the PF
	 * as part of the lifecycle of this MR.
	 */

	return &mr->ibmr;

err_dma_region:
	mana_gd_destroy_dma_region(mdev_to_gc(dev), dma_region_handle);

err_umem:
	ib_umem_release(mr->umem);

err_free:
	kfree(mr);
	return ERR_PTR(err);
}

int mana_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct mana_ib_mr *mr = container_of(ibmr, struct mana_ib_mr, ibmr);
	struct ib_device *ibdev = ibmr->device;
	struct mana_ib_dev *dev;
	int err;

	dev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	err = mana_ib_gd_destroy_mr(dev, mr->mr_handle);
	if (err)
		return err;

	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr);

	return 0;
}
