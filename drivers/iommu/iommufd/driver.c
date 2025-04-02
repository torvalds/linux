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

	vevent = kzalloc(struct_size(vevent, event_data, data_len), GFP_ATOMIC);
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

#ifdef CONFIG_IRQ_MSI_IOMMU
/*
 * Get a iommufd_sw_msi_map for the msi physical address requested by the irq
 * layer. The mapping to IOVA is global to the iommufd file descriptor, every
 * domain that is attached to a device using the same MSI parameters will use
 * the same IOVA.
 */
static struct iommufd_sw_msi_map *
iommufd_sw_msi_get_map(struct iommufd_ctx *ictx, phys_addr_t msi_addr,
		       phys_addr_t sw_msi_start)
{
	struct iommufd_sw_msi_map *cur;
	unsigned int max_pgoff = 0;

	lockdep_assert_held(&ictx->sw_msi_lock);

	list_for_each_entry(cur, &ictx->sw_msi_list, sw_msi_item) {
		if (cur->sw_msi_start != sw_msi_start)
			continue;
		max_pgoff = max(max_pgoff, cur->pgoff + 1);
		if (cur->msi_addr == msi_addr)
			return cur;
	}

	if (ictx->sw_msi_id >=
	    BITS_PER_BYTE * sizeof_field(struct iommufd_sw_msi_maps, bitmap))
		return ERR_PTR(-EOVERFLOW);

	cur = kzalloc(sizeof(*cur), GFP_KERNEL);
	if (!cur)
		return ERR_PTR(-ENOMEM);

	cur->sw_msi_start = sw_msi_start;
	cur->msi_addr = msi_addr;
	cur->pgoff = max_pgoff;
	cur->id = ictx->sw_msi_id++;
	list_add_tail(&cur->sw_msi_item, &ictx->sw_msi_list);
	return cur;
}

int iommufd_sw_msi_install(struct iommufd_ctx *ictx,
			   struct iommufd_hwpt_paging *hwpt_paging,
			   struct iommufd_sw_msi_map *msi_map)
{
	unsigned long iova;

	lockdep_assert_held(&ictx->sw_msi_lock);

	iova = msi_map->sw_msi_start + msi_map->pgoff * PAGE_SIZE;
	if (!test_bit(msi_map->id, hwpt_paging->present_sw_msi.bitmap)) {
		int rc;

		rc = iommu_map(hwpt_paging->common.domain, iova,
			       msi_map->msi_addr, PAGE_SIZE,
			       IOMMU_WRITE | IOMMU_READ | IOMMU_MMIO,
			       GFP_KERNEL_ACCOUNT);
		if (rc)
			return rc;
		__set_bit(msi_map->id, hwpt_paging->present_sw_msi.bitmap);
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(iommufd_sw_msi_install, "IOMMUFD_INTERNAL");

/*
 * Called by the irq code if the platform translates the MSI address through the
 * IOMMU. msi_addr is the physical address of the MSI page. iommufd will
 * allocate a fd global iova for the physical page that is the same on all
 * domains and devices.
 */
int iommufd_sw_msi(struct iommu_domain *domain, struct msi_desc *desc,
		   phys_addr_t msi_addr)
{
	struct device *dev = msi_desc_to_dev(desc);
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommu_attach_handle *raw_handle;
	struct iommufd_attach_handle *handle;
	struct iommufd_sw_msi_map *msi_map;
	struct iommufd_ctx *ictx;
	unsigned long iova;
	int rc;

	/*
	 * It is safe to call iommu_attach_handle_get() here because the iommu
	 * core code invokes this under the group mutex which also prevents any
	 * change of the attach handle for the duration of this function.
	 */
	iommu_group_mutex_assert(dev);

	raw_handle =
		iommu_attach_handle_get(dev->iommu_group, IOMMU_NO_PASID, 0);
	if (IS_ERR(raw_handle))
		return 0;
	hwpt_paging = find_hwpt_paging(domain->iommufd_hwpt);

	handle = to_iommufd_handle(raw_handle);
	/* No IOMMU_RESV_SW_MSI means no change to the msi_msg */
	if (handle->idev->igroup->sw_msi_start == PHYS_ADDR_MAX)
		return 0;

	ictx = handle->idev->ictx;
	guard(mutex)(&ictx->sw_msi_lock);
	/*
	 * The input msi_addr is the exact byte offset of the MSI doorbell, we
	 * assume the caller has checked that it is contained with a MMIO region
	 * that is secure to map at PAGE_SIZE.
	 */
	msi_map = iommufd_sw_msi_get_map(handle->idev->ictx,
					 msi_addr & PAGE_MASK,
					 handle->idev->igroup->sw_msi_start);
	if (IS_ERR(msi_map))
		return PTR_ERR(msi_map);

	rc = iommufd_sw_msi_install(ictx, hwpt_paging, msi_map);
	if (rc)
		return rc;
	__set_bit(msi_map->id, handle->idev->igroup->required_sw_msi.bitmap);

	iova = msi_map->sw_msi_start + msi_map->pgoff * PAGE_SIZE;
	msi_desc_set_iommu_msi_iova(desc, iova, PAGE_SHIFT);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(iommufd_sw_msi, "IOMMUFD");
#endif

MODULE_DESCRIPTION("iommufd code shared with builtin modules");
MODULE_IMPORT_NS("IOMMUFD_INTERNAL");
MODULE_LICENSE("GPL");
