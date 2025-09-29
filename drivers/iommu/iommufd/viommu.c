// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */
#include "iommufd_private.h"

void iommufd_viommu_destroy(struct iommufd_object *obj)
{
	struct iommufd_viommu *viommu =
		container_of(obj, struct iommufd_viommu, obj);

	if (viommu->ops && viommu->ops->destroy)
		viommu->ops->destroy(viommu);
	refcount_dec(&viommu->hwpt->common.obj.users);
	xa_destroy(&viommu->vdevs);
}

int iommufd_viommu_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_viommu_alloc *cmd = ucmd->cmd;
	const struct iommu_user_data user_data = {
		.type = cmd->type,
		.uptr = u64_to_user_ptr(cmd->data_uptr),
		.len = cmd->data_len,
	};
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;
	const struct iommu_ops *ops;
	size_t viommu_size;
	int rc;

	if (cmd->flags || cmd->type == IOMMU_VIOMMU_TYPE_DEFAULT)
		return -EOPNOTSUPP;

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);

	ops = dev_iommu_ops(idev->dev);
	if (!ops->get_viommu_size || !ops->viommu_init) {
		rc = -EOPNOTSUPP;
		goto out_put_idev;
	}

	viommu_size = ops->get_viommu_size(idev->dev, cmd->type);
	if (!viommu_size) {
		rc = -EOPNOTSUPP;
		goto out_put_idev;
	}

	/*
	 * It is a driver bug for providing a viommu_size smaller than the core
	 * vIOMMU structure size
	 */
	if (WARN_ON_ONCE(viommu_size < sizeof(*viommu))) {
		rc = -EOPNOTSUPP;
		goto out_put_idev;
	}

	hwpt_paging = iommufd_get_hwpt_paging(ucmd, cmd->hwpt_id);
	if (IS_ERR(hwpt_paging)) {
		rc = PTR_ERR(hwpt_paging);
		goto out_put_idev;
	}

	if (!hwpt_paging->nest_parent) {
		rc = -EINVAL;
		goto out_put_hwpt;
	}

	viommu = (struct iommufd_viommu *)_iommufd_object_alloc_ucmd(
		ucmd, viommu_size, IOMMUFD_OBJ_VIOMMU);
	if (IS_ERR(viommu)) {
		rc = PTR_ERR(viommu);
		goto out_put_hwpt;
	}

	xa_init(&viommu->vdevs);
	viommu->type = cmd->type;
	viommu->ictx = ucmd->ictx;
	viommu->hwpt = hwpt_paging;
	refcount_inc(&viommu->hwpt->common.obj.users);
	INIT_LIST_HEAD(&viommu->veventqs);
	init_rwsem(&viommu->veventqs_rwsem);
	/*
	 * It is the most likely case that a physical IOMMU is unpluggable. A
	 * pluggable IOMMU instance (if exists) is responsible for refcounting
	 * on its own.
	 */
	viommu->iommu_dev = __iommu_get_iommu_dev(idev->dev);

	rc = ops->viommu_init(viommu, hwpt_paging->common.domain,
			      user_data.len ? &user_data : NULL);
	if (rc)
		goto out_put_hwpt;

	/* It is a driver bug that viommu->ops isn't filled */
	if (WARN_ON_ONCE(!viommu->ops)) {
		rc = -EOPNOTSUPP;
		goto out_put_hwpt;
	}

	cmd->out_viommu_id = viommu->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));

out_put_hwpt:
	iommufd_put_object(ucmd->ictx, &hwpt_paging->common.obj);
out_put_idev:
	iommufd_put_object(ucmd->ictx, &idev->obj);
	return rc;
}

void iommufd_vdevice_abort(struct iommufd_object *obj)
{
	struct iommufd_vdevice *vdev =
		container_of(obj, struct iommufd_vdevice, obj);
	struct iommufd_viommu *viommu = vdev->viommu;
	struct iommufd_device *idev = vdev->idev;

	lockdep_assert_held(&idev->igroup->lock);

	if (vdev->destroy)
		vdev->destroy(vdev);
	/* xa_cmpxchg is okay to fail if alloc failed xa_cmpxchg previously */
	xa_cmpxchg(&viommu->vdevs, vdev->virt_id, vdev, NULL, GFP_KERNEL);
	refcount_dec(&viommu->obj.users);
	idev->vdev = NULL;
}

void iommufd_vdevice_destroy(struct iommufd_object *obj)
{
	struct iommufd_vdevice *vdev =
		container_of(obj, struct iommufd_vdevice, obj);
	struct iommufd_device *idev = vdev->idev;
	struct iommufd_ctx *ictx = idev->ictx;

	mutex_lock(&idev->igroup->lock);
	iommufd_vdevice_abort(obj);
	mutex_unlock(&idev->igroup->lock);
	iommufd_put_object(ictx, &idev->obj);
}

int iommufd_vdevice_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_vdevice_alloc *cmd = ucmd->cmd;
	struct iommufd_vdevice *vdev, *curr;
	size_t vdev_size = sizeof(*vdev);
	struct iommufd_viommu *viommu;
	struct iommufd_device *idev;
	u64 virt_id = cmd->virt_id;
	int rc = 0;

	/* virt_id indexes an xarray */
	if (virt_id > ULONG_MAX)
		return -EINVAL;

	viommu = iommufd_get_viommu(ucmd, cmd->viommu_id);
	if (IS_ERR(viommu))
		return PTR_ERR(viommu);

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev)) {
		rc = PTR_ERR(idev);
		goto out_put_viommu;
	}

	if (viommu->iommu_dev != __iommu_get_iommu_dev(idev->dev)) {
		rc = -EINVAL;
		goto out_put_idev;
	}

	mutex_lock(&idev->igroup->lock);
	if (idev->destroying) {
		rc = -ENOENT;
		goto out_unlock_igroup;
	}

	if (idev->vdev) {
		rc = -EEXIST;
		goto out_unlock_igroup;
	}

	if (viommu->ops && viommu->ops->vdevice_size) {
		/*
		 * It is a driver bug for:
		 * - ops->vdevice_size smaller than the core structure size
		 * - not implementing a pairing ops->vdevice_init op
		 */
		if (WARN_ON_ONCE(viommu->ops->vdevice_size < vdev_size ||
				 !viommu->ops->vdevice_init)) {
			rc = -EOPNOTSUPP;
			goto out_put_idev;
		}
		vdev_size = viommu->ops->vdevice_size;
	}

	vdev = (struct iommufd_vdevice *)_iommufd_object_alloc(
		ucmd->ictx, vdev_size, IOMMUFD_OBJ_VDEVICE);
	if (IS_ERR(vdev)) {
		rc = PTR_ERR(vdev);
		goto out_unlock_igroup;
	}

	vdev->virt_id = virt_id;
	vdev->viommu = viommu;
	refcount_inc(&viommu->obj.users);
	/*
	 * A wait_cnt reference is held on the idev so long as we have the
	 * pointer. iommufd_device_pre_destroy() will revoke it before the
	 * idev real destruction.
	 */
	vdev->idev = idev;

	/*
	 * iommufd_device_destroy() delays until idev->vdev is NULL before
	 * freeing the idev, which only happens once the vdev is finished
	 * destruction.
	 */
	idev->vdev = vdev;

	curr = xa_cmpxchg(&viommu->vdevs, virt_id, NULL, vdev, GFP_KERNEL);
	if (curr) {
		rc = xa_err(curr) ?: -EEXIST;
		goto out_abort;
	}

	if (viommu->ops && viommu->ops->vdevice_init) {
		rc = viommu->ops->vdevice_init(vdev);
		if (rc)
			goto out_abort;
	}

	cmd->out_vdevice_id = vdev->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_abort;
	iommufd_object_finalize(ucmd->ictx, &vdev->obj);
	goto out_unlock_igroup;

out_abort:
	iommufd_object_abort_and_destroy(ucmd->ictx, &vdev->obj);
out_unlock_igroup:
	mutex_unlock(&idev->igroup->lock);
out_put_idev:
	if (rc)
		iommufd_put_object(ucmd->ictx, &idev->obj);
out_put_viommu:
	iommufd_put_object(ucmd->ictx, &viommu->obj);
	return rc;
}

static void iommufd_hw_queue_destroy_access(struct iommufd_ctx *ictx,
					    struct iommufd_access *access,
					    u64 base_iova, size_t length)
{
	u64 aligned_iova = PAGE_ALIGN_DOWN(base_iova);
	u64 offset = base_iova - aligned_iova;

	iommufd_access_unpin_pages(access, aligned_iova,
				   PAGE_ALIGN(length + offset));
	iommufd_access_detach_internal(access);
	iommufd_access_destroy_internal(ictx, access);
}

void iommufd_hw_queue_destroy(struct iommufd_object *obj)
{
	struct iommufd_hw_queue *hw_queue =
		container_of(obj, struct iommufd_hw_queue, obj);

	if (hw_queue->destroy)
		hw_queue->destroy(hw_queue);
	if (hw_queue->access)
		iommufd_hw_queue_destroy_access(hw_queue->viommu->ictx,
						hw_queue->access,
						hw_queue->base_addr,
						hw_queue->length);
	if (hw_queue->viommu)
		refcount_dec(&hw_queue->viommu->obj.users);
}

/*
 * When the HW accesses the guest queue via physical addresses, the underlying
 * physical pages of the guest queue must be contiguous. Also, for the security
 * concern that IOMMUFD_CMD_IOAS_UNMAP could potentially remove the mappings of
 * the guest queue from the nesting parent iopt while the HW is still accessing
 * the guest queue memory physically, such a HW queue must require an access to
 * pin the underlying pages and prevent that from happening.
 */
static struct iommufd_access *
iommufd_hw_queue_alloc_phys(struct iommu_hw_queue_alloc *cmd,
			    struct iommufd_viommu *viommu, phys_addr_t *base_pa)
{
	u64 aligned_iova = PAGE_ALIGN_DOWN(cmd->nesting_parent_iova);
	u64 offset = cmd->nesting_parent_iova - aligned_iova;
	struct iommufd_access *access;
	struct page **pages;
	size_t max_npages;
	size_t length;
	size_t i;
	int rc;

	/* max_npages = DIV_ROUND_UP(offset + cmd->length, PAGE_SIZE) */
	if (check_add_overflow(offset, cmd->length, &length))
		return ERR_PTR(-ERANGE);
	if (check_add_overflow(length, PAGE_SIZE - 1, &length))
		return ERR_PTR(-ERANGE);
	max_npages = length / PAGE_SIZE;
	/* length needs to be page aligned too */
	length = max_npages * PAGE_SIZE;

	/*
	 * Use kvcalloc() to avoid memory fragmentation for a large page array.
	 * Set __GFP_NOWARN to avoid syzkaller blowups
	 */
	pages = kvcalloc(max_npages, sizeof(*pages), GFP_KERNEL | __GFP_NOWARN);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	access = iommufd_access_create_internal(viommu->ictx);
	if (IS_ERR(access)) {
		rc = PTR_ERR(access);
		goto out_free;
	}

	rc = iommufd_access_attach_internal(access, viommu->hwpt->ioas);
	if (rc)
		goto out_destroy;

	rc = iommufd_access_pin_pages(access, aligned_iova, length, pages, 0);
	if (rc)
		goto out_detach;

	/* Validate if the underlying physical pages are contiguous */
	for (i = 1; i < max_npages; i++) {
		if (page_to_pfn(pages[i]) == page_to_pfn(pages[i - 1]) + 1)
			continue;
		rc = -EFAULT;
		goto out_unpin;
	}

	*base_pa = (page_to_pfn(pages[0]) << PAGE_SHIFT) + offset;
	kvfree(pages);
	return access;

out_unpin:
	iommufd_access_unpin_pages(access, aligned_iova, length);
out_detach:
	iommufd_access_detach_internal(access);
out_destroy:
	iommufd_access_destroy_internal(viommu->ictx, access);
out_free:
	kvfree(pages);
	return ERR_PTR(rc);
}

int iommufd_hw_queue_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_hw_queue_alloc *cmd = ucmd->cmd;
	struct iommufd_hw_queue *hw_queue;
	struct iommufd_viommu *viommu;
	struct iommufd_access *access;
	size_t hw_queue_size;
	phys_addr_t base_pa;
	u64 last;
	int rc;

	if (cmd->flags || cmd->type == IOMMU_HW_QUEUE_TYPE_DEFAULT)
		return -EOPNOTSUPP;
	if (!cmd->length)
		return -EINVAL;
	if (check_add_overflow(cmd->nesting_parent_iova, cmd->length - 1,
			       &last))
		return -EOVERFLOW;

	viommu = iommufd_get_viommu(ucmd, cmd->viommu_id);
	if (IS_ERR(viommu))
		return PTR_ERR(viommu);

	if (!viommu->ops || !viommu->ops->get_hw_queue_size ||
	    !viommu->ops->hw_queue_init_phys) {
		rc = -EOPNOTSUPP;
		goto out_put_viommu;
	}

	hw_queue_size = viommu->ops->get_hw_queue_size(viommu, cmd->type);
	if (!hw_queue_size) {
		rc = -EOPNOTSUPP;
		goto out_put_viommu;
	}

	/*
	 * It is a driver bug for providing a hw_queue_size smaller than the
	 * core HW queue structure size
	 */
	if (WARN_ON_ONCE(hw_queue_size < sizeof(*hw_queue))) {
		rc = -EOPNOTSUPP;
		goto out_put_viommu;
	}

	hw_queue = (struct iommufd_hw_queue *)_iommufd_object_alloc_ucmd(
		ucmd, hw_queue_size, IOMMUFD_OBJ_HW_QUEUE);
	if (IS_ERR(hw_queue)) {
		rc = PTR_ERR(hw_queue);
		goto out_put_viommu;
	}

	access = iommufd_hw_queue_alloc_phys(cmd, viommu, &base_pa);
	if (IS_ERR(access)) {
		rc = PTR_ERR(access);
		goto out_put_viommu;
	}

	hw_queue->viommu = viommu;
	refcount_inc(&viommu->obj.users);
	hw_queue->access = access;
	hw_queue->type = cmd->type;
	hw_queue->length = cmd->length;
	hw_queue->base_addr = cmd->nesting_parent_iova;

	rc = viommu->ops->hw_queue_init_phys(hw_queue, cmd->index, base_pa);
	if (rc)
		goto out_put_viommu;

	cmd->out_hw_queue_id = hw_queue->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));

out_put_viommu:
	iommufd_put_object(ucmd->ictx, &viommu->obj);
	return rc;
}
