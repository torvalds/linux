// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */
#include "iommufd_private.h"

struct iommufd_object *_iommufd_object_alloc(struct iommufd_ctx *ictx,
					     size_t size,
					     enum iommufd_object_type type)
{
	struct iommufd_object *obj;
	int rc;

	obj = kzalloc(size, GFP_KERNEL_ACCOUNT);
	if (!obj)
		return ERR_PTR(-ENOMEM);
	obj->type = type;
	/* Starts out bias'd by 1 until it is removed from the xarray */
	refcount_set(&obj->shortterm_users, 1);
	refcount_set(&obj->users, 1);

	/*
	 * Reserve an ID in the xarray but do not publish the pointer yet since
	 * the caller hasn't initialized it yet. Once the pointer is published
	 * in the xarray and visible to other threads we can't reliably destroy
	 * it anymore, so the caller must complete all errorable operations
	 * before calling iommufd_object_finalize().
	 */
	rc = xa_alloc(&ictx->objects, &obj->id, XA_ZERO_ENTRY, xa_limit_31b,
		      GFP_KERNEL_ACCOUNT);
	if (rc)
		goto out_free;
	return obj;
out_free:
	kfree(obj);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(_iommufd_object_alloc, IOMMUFD);

/* Caller should xa_lock(&viommu->vdevs) to protect the return value */
struct device *iommufd_viommu_find_dev(struct iommufd_viommu *viommu,
				       unsigned long vdev_id)
{
	struct iommufd_vdevice *vdev;

	lockdep_assert_held(&viommu->vdevs.xa_lock);

	vdev = xa_load(&viommu->vdevs, vdev_id);
	return vdev ? vdev->dev : NULL;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_find_dev, IOMMUFD);

MODULE_DESCRIPTION("iommufd code shared with builtin modules");
MODULE_LICENSE("GPL");
