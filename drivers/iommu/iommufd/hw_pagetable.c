// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/iommu.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"
#include "iommufd_private.h"

static void __iommufd_hwpt_destroy(struct iommufd_hw_pagetable *hwpt)
{
	if (hwpt->domain)
		iommu_domain_free(hwpt->domain);

	if (hwpt->fault)
		refcount_dec(&hwpt->fault->obj.users);
}

void iommufd_hwpt_paging_destroy(struct iommufd_object *obj)
{
	struct iommufd_hwpt_paging *hwpt_paging =
		container_of(obj, struct iommufd_hwpt_paging, common.obj);

	if (!list_empty(&hwpt_paging->hwpt_item)) {
		mutex_lock(&hwpt_paging->ioas->mutex);
		list_del(&hwpt_paging->hwpt_item);
		mutex_unlock(&hwpt_paging->ioas->mutex);

		iopt_table_remove_domain(&hwpt_paging->ioas->iopt,
					 hwpt_paging->common.domain);
	}

	__iommufd_hwpt_destroy(&hwpt_paging->common);
	refcount_dec(&hwpt_paging->ioas->obj.users);
}

void iommufd_hwpt_paging_abort(struct iommufd_object *obj)
{
	struct iommufd_hwpt_paging *hwpt_paging =
		container_of(obj, struct iommufd_hwpt_paging, common.obj);

	/* The ioas->mutex must be held until finalize is called. */
	lockdep_assert_held(&hwpt_paging->ioas->mutex);

	if (!list_empty(&hwpt_paging->hwpt_item)) {
		list_del_init(&hwpt_paging->hwpt_item);
		iopt_table_remove_domain(&hwpt_paging->ioas->iopt,
					 hwpt_paging->common.domain);
	}
	iommufd_hwpt_paging_destroy(obj);
}

void iommufd_hwpt_nested_destroy(struct iommufd_object *obj)
{
	struct iommufd_hwpt_nested *hwpt_nested =
		container_of(obj, struct iommufd_hwpt_nested, common.obj);

	__iommufd_hwpt_destroy(&hwpt_nested->common);
	if (hwpt_nested->viommu)
		refcount_dec(&hwpt_nested->viommu->obj.users);
	else
		refcount_dec(&hwpt_nested->parent->common.obj.users);
}

void iommufd_hwpt_nested_abort(struct iommufd_object *obj)
{
	iommufd_hwpt_nested_destroy(obj);
}

static int
iommufd_hwpt_paging_enforce_cc(struct iommufd_hwpt_paging *hwpt_paging)
{
	struct iommu_domain *paging_domain = hwpt_paging->common.domain;

	if (hwpt_paging->enforce_cache_coherency)
		return 0;

	if (paging_domain->ops->enforce_cache_coherency)
		hwpt_paging->enforce_cache_coherency =
			paging_domain->ops->enforce_cache_coherency(
				paging_domain);
	if (!hwpt_paging->enforce_cache_coherency)
		return -EINVAL;
	return 0;
}

/**
 * iommufd_hwpt_paging_alloc() - Get a PAGING iommu_domain for a device
 * @ictx: iommufd context
 * @ioas: IOAS to associate the domain with
 * @idev: Device to get an iommu_domain for
 * @flags: Flags from userspace
 * @immediate_attach: True if idev should be attached to the hwpt
 * @user_data: The user provided driver specific data describing the domain to
 *             create
 *
 * Allocate a new iommu_domain and return it as a hw_pagetable. The HWPT
 * will be linked to the given ioas and upon return the underlying iommu_domain
 * is fully popoulated.
 *
 * The caller must hold the ioas->mutex until after
 * iommufd_object_abort_and_destroy() or iommufd_object_finalize() is called on
 * the returned hwpt.
 */
struct iommufd_hwpt_paging *
iommufd_hwpt_paging_alloc(struct iommufd_ctx *ictx, struct iommufd_ioas *ioas,
			  struct iommufd_device *idev, u32 flags,
			  bool immediate_attach,
			  const struct iommu_user_data *user_data)
{
	const u32 valid_flags = IOMMU_HWPT_ALLOC_NEST_PARENT |
				IOMMU_HWPT_ALLOC_DIRTY_TRACKING |
				IOMMU_HWPT_FAULT_ID_VALID;
	const struct iommu_ops *ops = dev_iommu_ops(idev->dev);
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	lockdep_assert_held(&ioas->mutex);

	if ((flags || user_data) && !ops->domain_alloc_paging_flags)
		return ERR_PTR(-EOPNOTSUPP);
	if (flags & ~valid_flags)
		return ERR_PTR(-EOPNOTSUPP);
	if ((flags & IOMMU_HWPT_ALLOC_DIRTY_TRACKING) &&
	    !device_iommu_capable(idev->dev, IOMMU_CAP_DIRTY_TRACKING))
		return ERR_PTR(-EOPNOTSUPP);

	hwpt_paging = __iommufd_object_alloc(
		ictx, hwpt_paging, IOMMUFD_OBJ_HWPT_PAGING, common.obj);
	if (IS_ERR(hwpt_paging))
		return ERR_CAST(hwpt_paging);
	hwpt = &hwpt_paging->common;

	INIT_LIST_HEAD(&hwpt_paging->hwpt_item);
	/* Pairs with iommufd_hw_pagetable_destroy() */
	refcount_inc(&ioas->obj.users);
	hwpt_paging->ioas = ioas;
	hwpt_paging->nest_parent = flags & IOMMU_HWPT_ALLOC_NEST_PARENT;

	if (ops->domain_alloc_paging_flags) {
		hwpt->domain = ops->domain_alloc_paging_flags(idev->dev,
				flags & ~IOMMU_HWPT_FAULT_ID_VALID, user_data);
		if (IS_ERR(hwpt->domain)) {
			rc = PTR_ERR(hwpt->domain);
			hwpt->domain = NULL;
			goto out_abort;
		}
		hwpt->domain->owner = ops;
	} else {
		hwpt->domain = iommu_paging_domain_alloc(idev->dev);
		if (IS_ERR(hwpt->domain)) {
			rc = PTR_ERR(hwpt->domain);
			hwpt->domain = NULL;
			goto out_abort;
		}
	}

	/*
	 * Set the coherency mode before we do iopt_table_add_domain() as some
	 * iommus have a per-PTE bit that controls it and need to decide before
	 * doing any maps. It is an iommu driver bug to report
	 * IOMMU_CAP_ENFORCE_CACHE_COHERENCY but fail enforce_cache_coherency on
	 * a new domain.
	 *
	 * The cache coherency mode must be configured here and unchanged later.
	 * Note that a HWPT (non-CC) created for a device (non-CC) can be later
	 * reused by another device (either non-CC or CC). However, A HWPT (CC)
	 * created for a device (CC) cannot be reused by another device (non-CC)
	 * but only devices (CC). Instead user space in this case would need to
	 * allocate a separate HWPT (non-CC).
	 */
	if (idev->enforce_cache_coherency) {
		rc = iommufd_hwpt_paging_enforce_cc(hwpt_paging);
		if (WARN_ON(rc))
			goto out_abort;
	}

	/*
	 * immediate_attach exists only to accommodate iommu drivers that cannot
	 * directly allocate a domain. These drivers do not finish creating the
	 * domain until attach is completed. Thus we must have this call
	 * sequence. Once those drivers are fixed this should be removed.
	 */
	if (immediate_attach) {
		rc = iommufd_hw_pagetable_attach(hwpt, idev);
		if (rc)
			goto out_abort;
	}

	rc = iopt_table_add_domain(&ioas->iopt, hwpt->domain);
	if (rc)
		goto out_detach;
	list_add_tail(&hwpt_paging->hwpt_item, &ioas->hwpt_list);
	return hwpt_paging;

out_detach:
	if (immediate_attach)
		iommufd_hw_pagetable_detach(idev);
out_abort:
	iommufd_object_abort_and_destroy(ictx, &hwpt->obj);
	return ERR_PTR(rc);
}

/**
 * iommufd_hwpt_nested_alloc() - Get a NESTED iommu_domain for a device
 * @ictx: iommufd context
 * @parent: Parent PAGING-type hwpt to associate the domain with
 * @idev: Device to get an iommu_domain for
 * @flags: Flags from userspace
 * @user_data: user_data pointer. Must be valid
 *
 * Allocate a new iommu_domain (must be IOMMU_DOMAIN_NESTED) and return it as
 * a NESTED hw_pagetable. The given parent PAGING-type hwpt must be capable of
 * being a parent.
 */
static struct iommufd_hwpt_nested *
iommufd_hwpt_nested_alloc(struct iommufd_ctx *ictx,
			  struct iommufd_hwpt_paging *parent,
			  struct iommufd_device *idev, u32 flags,
			  const struct iommu_user_data *user_data)
{
	const struct iommu_ops *ops = dev_iommu_ops(idev->dev);
	struct iommufd_hwpt_nested *hwpt_nested;
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	if ((flags & ~IOMMU_HWPT_FAULT_ID_VALID) ||
	    !user_data->len || !ops->domain_alloc_nested)
		return ERR_PTR(-EOPNOTSUPP);
	if (parent->auto_domain || !parent->nest_parent ||
	    parent->common.domain->owner != ops)
		return ERR_PTR(-EINVAL);

	hwpt_nested = __iommufd_object_alloc(
		ictx, hwpt_nested, IOMMUFD_OBJ_HWPT_NESTED, common.obj);
	if (IS_ERR(hwpt_nested))
		return ERR_CAST(hwpt_nested);
	hwpt = &hwpt_nested->common;

	refcount_inc(&parent->common.obj.users);
	hwpt_nested->parent = parent;

	hwpt->domain = ops->domain_alloc_nested(
		idev->dev, parent->common.domain,
		flags & ~IOMMU_HWPT_FAULT_ID_VALID, user_data);
	if (IS_ERR(hwpt->domain)) {
		rc = PTR_ERR(hwpt->domain);
		hwpt->domain = NULL;
		goto out_abort;
	}
	hwpt->domain->owner = ops;

	if (WARN_ON_ONCE(hwpt->domain->type != IOMMU_DOMAIN_NESTED)) {
		rc = -EINVAL;
		goto out_abort;
	}
	return hwpt_nested;

out_abort:
	iommufd_object_abort_and_destroy(ictx, &hwpt->obj);
	return ERR_PTR(rc);
}

/**
 * iommufd_viommu_alloc_hwpt_nested() - Get a hwpt_nested for a vIOMMU
 * @viommu: vIOMMU ojbect to associate the hwpt_nested/domain with
 * @flags: Flags from userspace
 * @user_data: user_data pointer. Must be valid
 *
 * Allocate a new IOMMU_DOMAIN_NESTED for a vIOMMU and return it as a NESTED
 * hw_pagetable.
 */
static struct iommufd_hwpt_nested *
iommufd_viommu_alloc_hwpt_nested(struct iommufd_viommu *viommu, u32 flags,
				 const struct iommu_user_data *user_data)
{
	struct iommufd_hwpt_nested *hwpt_nested;
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	if (flags & ~IOMMU_HWPT_FAULT_ID_VALID)
		return ERR_PTR(-EOPNOTSUPP);
	if (!user_data->len)
		return ERR_PTR(-EOPNOTSUPP);
	if (!viommu->ops || !viommu->ops->alloc_domain_nested)
		return ERR_PTR(-EOPNOTSUPP);

	hwpt_nested = __iommufd_object_alloc(
		viommu->ictx, hwpt_nested, IOMMUFD_OBJ_HWPT_NESTED, common.obj);
	if (IS_ERR(hwpt_nested))
		return ERR_CAST(hwpt_nested);
	hwpt = &hwpt_nested->common;

	hwpt_nested->viommu = viommu;
	refcount_inc(&viommu->obj.users);
	hwpt_nested->parent = viommu->hwpt;

	hwpt->domain =
		viommu->ops->alloc_domain_nested(viommu,
				flags & ~IOMMU_HWPT_FAULT_ID_VALID,
				user_data);
	if (IS_ERR(hwpt->domain)) {
		rc = PTR_ERR(hwpt->domain);
		hwpt->domain = NULL;
		goto out_abort;
	}
	hwpt->domain->owner = viommu->iommu_dev->ops;

	if (WARN_ON_ONCE(hwpt->domain->type != IOMMU_DOMAIN_NESTED)) {
		rc = -EINVAL;
		goto out_abort;
	}
	return hwpt_nested;

out_abort:
	iommufd_object_abort_and_destroy(viommu->ictx, &hwpt->obj);
	return ERR_PTR(rc);
}

int iommufd_hwpt_alloc(struct iommufd_ucmd *ucmd)
{
	struct iommu_hwpt_alloc *cmd = ucmd->cmd;
	const struct iommu_user_data user_data = {
		.type = cmd->data_type,
		.uptr = u64_to_user_ptr(cmd->data_uptr),
		.len = cmd->data_len,
	};
	struct iommufd_hw_pagetable *hwpt;
	struct iommufd_ioas *ioas = NULL;
	struct iommufd_object *pt_obj;
	struct iommufd_device *idev;
	int rc;

	if (cmd->__reserved)
		return -EOPNOTSUPP;
	if ((cmd->data_type == IOMMU_HWPT_DATA_NONE && cmd->data_len) ||
	    (cmd->data_type != IOMMU_HWPT_DATA_NONE && !cmd->data_len))
		return -EINVAL;

	idev = iommufd_get_device(ucmd, cmd->dev_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);

	pt_obj = iommufd_get_object(ucmd->ictx, cmd->pt_id, IOMMUFD_OBJ_ANY);
	if (IS_ERR(pt_obj)) {
		rc = -EINVAL;
		goto out_put_idev;
	}

	if (pt_obj->type == IOMMUFD_OBJ_IOAS) {
		struct iommufd_hwpt_paging *hwpt_paging;

		ioas = container_of(pt_obj, struct iommufd_ioas, obj);
		mutex_lock(&ioas->mutex);
		hwpt_paging = iommufd_hwpt_paging_alloc(
			ucmd->ictx, ioas, idev, cmd->flags, false,
			user_data.len ? &user_data : NULL);
		if (IS_ERR(hwpt_paging)) {
			rc = PTR_ERR(hwpt_paging);
			goto out_unlock;
		}
		hwpt = &hwpt_paging->common;
	} else if (pt_obj->type == IOMMUFD_OBJ_HWPT_PAGING) {
		struct iommufd_hwpt_nested *hwpt_nested;

		hwpt_nested = iommufd_hwpt_nested_alloc(
			ucmd->ictx,
			container_of(pt_obj, struct iommufd_hwpt_paging,
				     common.obj),
			idev, cmd->flags, &user_data);
		if (IS_ERR(hwpt_nested)) {
			rc = PTR_ERR(hwpt_nested);
			goto out_unlock;
		}
		hwpt = &hwpt_nested->common;
	} else if (pt_obj->type == IOMMUFD_OBJ_VIOMMU) {
		struct iommufd_hwpt_nested *hwpt_nested;
		struct iommufd_viommu *viommu;

		viommu = container_of(pt_obj, struct iommufd_viommu, obj);
		if (viommu->iommu_dev != __iommu_get_iommu_dev(idev->dev)) {
			rc = -EINVAL;
			goto out_unlock;
		}
		hwpt_nested = iommufd_viommu_alloc_hwpt_nested(
			viommu, cmd->flags, &user_data);
		if (IS_ERR(hwpt_nested)) {
			rc = PTR_ERR(hwpt_nested);
			goto out_unlock;
		}
		hwpt = &hwpt_nested->common;
	} else {
		rc = -EINVAL;
		goto out_put_pt;
	}

	if (cmd->flags & IOMMU_HWPT_FAULT_ID_VALID) {
		struct iommufd_fault *fault;

		fault = iommufd_get_fault(ucmd, cmd->fault_id);
		if (IS_ERR(fault)) {
			rc = PTR_ERR(fault);
			goto out_hwpt;
		}
		hwpt->fault = fault;
		hwpt->domain->iopf_handler = iommufd_fault_iopf_handler;
		hwpt->domain->fault_data = hwpt;
		refcount_inc(&fault->obj.users);
		iommufd_put_object(ucmd->ictx, &fault->obj);
	}

	cmd->out_hwpt_id = hwpt->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_hwpt;
	iommufd_object_finalize(ucmd->ictx, &hwpt->obj);
	goto out_unlock;

out_hwpt:
	iommufd_object_abort_and_destroy(ucmd->ictx, &hwpt->obj);
out_unlock:
	if (ioas)
		mutex_unlock(&ioas->mutex);
out_put_pt:
	iommufd_put_object(ucmd->ictx, pt_obj);
out_put_idev:
	iommufd_put_object(ucmd->ictx, &idev->obj);
	return rc;
}

int iommufd_hwpt_set_dirty_tracking(struct iommufd_ucmd *ucmd)
{
	struct iommu_hwpt_set_dirty_tracking *cmd = ucmd->cmd;
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_ioas *ioas;
	int rc = -EOPNOTSUPP;
	bool enable;

	if (cmd->flags & ~IOMMU_HWPT_DIRTY_TRACKING_ENABLE)
		return rc;

	hwpt_paging = iommufd_get_hwpt_paging(ucmd, cmd->hwpt_id);
	if (IS_ERR(hwpt_paging))
		return PTR_ERR(hwpt_paging);

	ioas = hwpt_paging->ioas;
	enable = cmd->flags & IOMMU_HWPT_DIRTY_TRACKING_ENABLE;

	rc = iopt_set_dirty_tracking(&ioas->iopt, hwpt_paging->common.domain,
				     enable);

	iommufd_put_object(ucmd->ictx, &hwpt_paging->common.obj);
	return rc;
}

int iommufd_hwpt_get_dirty_bitmap(struct iommufd_ucmd *ucmd)
{
	struct iommu_hwpt_get_dirty_bitmap *cmd = ucmd->cmd;
	struct iommufd_hwpt_paging *hwpt_paging;
	struct iommufd_ioas *ioas;
	int rc = -EOPNOTSUPP;

	if ((cmd->flags & ~(IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR)) ||
	    cmd->__reserved)
		return -EOPNOTSUPP;

	hwpt_paging = iommufd_get_hwpt_paging(ucmd, cmd->hwpt_id);
	if (IS_ERR(hwpt_paging))
		return PTR_ERR(hwpt_paging);

	ioas = hwpt_paging->ioas;
	rc = iopt_read_and_clear_dirty_data(
		&ioas->iopt, hwpt_paging->common.domain, cmd->flags, cmd);

	iommufd_put_object(ucmd->ictx, &hwpt_paging->common.obj);
	return rc;
}

int iommufd_hwpt_invalidate(struct iommufd_ucmd *ucmd)
{
	struct iommu_hwpt_invalidate *cmd = ucmd->cmd;
	struct iommu_user_data_array data_array = {
		.type = cmd->data_type,
		.uptr = u64_to_user_ptr(cmd->data_uptr),
		.entry_len = cmd->entry_len,
		.entry_num = cmd->entry_num,
	};
	struct iommufd_object *pt_obj;
	u32 done_num = 0;
	int rc;

	if (cmd->__reserved) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	if (cmd->entry_num && (!cmd->data_uptr || !cmd->entry_len)) {
		rc = -EINVAL;
		goto out;
	}

	pt_obj = iommufd_get_object(ucmd->ictx, cmd->hwpt_id, IOMMUFD_OBJ_ANY);
	if (IS_ERR(pt_obj)) {
		rc = PTR_ERR(pt_obj);
		goto out;
	}
	if (pt_obj->type == IOMMUFD_OBJ_HWPT_NESTED) {
		struct iommufd_hw_pagetable *hwpt =
			container_of(pt_obj, struct iommufd_hw_pagetable, obj);

		if (!hwpt->domain->ops ||
		    !hwpt->domain->ops->cache_invalidate_user) {
			rc = -EOPNOTSUPP;
			goto out_put_pt;
		}
		rc = hwpt->domain->ops->cache_invalidate_user(hwpt->domain,
							      &data_array);
	} else if (pt_obj->type == IOMMUFD_OBJ_VIOMMU) {
		struct iommufd_viommu *viommu =
			container_of(pt_obj, struct iommufd_viommu, obj);

		if (!viommu->ops || !viommu->ops->cache_invalidate) {
			rc = -EOPNOTSUPP;
			goto out_put_pt;
		}
		rc = viommu->ops->cache_invalidate(viommu, &data_array);
	} else {
		rc = -EINVAL;
		goto out_put_pt;
	}

	done_num = data_array.entry_num;

out_put_pt:
	iommufd_put_object(ucmd->ictx, pt_obj);
out:
	cmd->entry_num = done_num;
	if (iommufd_ucmd_respond(ucmd, sizeof(*cmd)))
		return -EFAULT;
	return rc;
}
