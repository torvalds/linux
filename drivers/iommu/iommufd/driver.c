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
EXPORT_SYMBOL_NS_GPL(_iommufd_object_alloc, "IOMMUFD");

/* Caller should xa_lock(&viommu->vdevs) to protect the return value */
struct device *iommufd_viommu_find_dev(struct iommufd_viommu *viommu,
				       unsigned long vdev_id)
{
	struct iommufd_vdevice *vdev;

	lockdep_assert_held(&viommu->vdevs.xa_lock);

	vdev = xa_load(&viommu->vdevs, vdev_id);
	return vdev ? vdev->dev : NULL;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_find_dev, "IOMMUFD");

/* Return -ENOENT if device is not associated to the vIOMMU */
int iommufd_viommu_get_vdev_id(struct iommufd_viommu *viommu,
			       struct device *dev, unsigned long *vdev_id)
{
	struct iommufd_vdevice *vdev;
	unsigned long index;
	int rc = -ENOENT;

	if (WARN_ON_ONCE(!vdev_id))
		return -EINVAL;

	xa_lock(&viommu->vdevs);
	xa_for_each(&viommu->vdevs, index, vdev) {
		if (vdev->dev == dev) {
			*vdev_id = vdev->id;
			rc = 0;
			break;
		}
	}
	xa_unlock(&viommu->vdevs);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_get_vdev_id, "IOMMUFD");

/*
 * Typically called in driver's threaded IRQ handler.
 * The @type and @event_data must be defined in include/uapi/linux/iommufd.h
 */
int iommufd_viommu_report_event(struct iommufd_viommu *viommu,
				enum iommu_veventq_type type, void *event_data,
				size_t data_len)
{
	struct iommufd_veventq *veventq;
	struct iommufd_vevent *vevent;
	int rc = 0;

	if (WARN_ON_ONCE(!data_len || !event_data))
		return -EINVAL;

	down_read(&viommu->veventqs_rwsem);

	veventq = iommufd_viommu_find_veventq(viommu, type);
	if (!veventq) {
		rc = -EOPNOTSUPP;
		goto out_unlock_veventqs;
	}

	spin_lock(&veventq->common.lock);
	if (veventq->num_events == veventq->depth) {
		vevent = &veventq->lost_events_header;
		goto out_set_header;
	}

	vevent = kmalloc(struct_size(vevent, event_data, data_len), GFP_ATOMIC);
	if (!vevent) {
		rc = -ENOMEM;
		vevent = &veventq->lost_events_header;
		goto out_set_header;
	}
	memcpy(vevent->event_data, event_data, data_len);
	vevent->data_len = data_len;
	veventq->num_events++;

out_set_header:
	iommufd_vevent_handler(veventq, vevent);
	spin_unlock(&veventq->common.lock);
out_unlock_veventqs:
	up_read(&viommu->veventqs_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_report_event, "IOMMUFD");

MODULE_DESCRIPTION("iommufd code shared with builtin modules");
MODULE_LICENSE("GPL");
