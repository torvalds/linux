// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/iommufd.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/irqdomain.h>

#include "iommufd_private.h"

static bool allow_unsafe_interrupts;
module_param(allow_unsafe_interrupts, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(
	allow_unsafe_interrupts,
	"Allow IOMMUFD to bind to devices even if the platform cannot isolate "
	"the MSI interrupt window. Enabling this is a security weakness.");

/*
 * A iommufd_device object represents the binding relationship between a
 * consuming driver and the iommufd. These objects are created/destroyed by
 * external drivers, not by userspace.
 */
struct iommufd_device {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct iommufd_hw_pagetable *hwpt;
	/* Head at iommufd_hw_pagetable::devices */
	struct list_head devices_item;
	/* always the physical device */
	struct device *dev;
	struct iommu_group *group;
	bool enforce_cache_coherency;
};

void iommufd_device_destroy(struct iommufd_object *obj)
{
	struct iommufd_device *idev =
		container_of(obj, struct iommufd_device, obj);

	iommu_device_release_dma_owner(idev->dev);
	iommu_group_put(idev->group);
	iommufd_ctx_put(idev->ictx);
}

/**
 * iommufd_device_bind - Bind a physical device to an iommu fd
 * @ictx: iommufd file descriptor
 * @dev: Pointer to a physical device struct
 * @id: Output ID number to return to userspace for this device
 *
 * A successful bind establishes an ownership over the device and returns
 * struct iommufd_device pointer, otherwise returns error pointer.
 *
 * A driver using this API must set driver_managed_dma and must not touch
 * the device until this routine succeeds and establishes ownership.
 *
 * Binding a PCI device places the entire RID under iommufd control.
 *
 * The caller must undo this with iommufd_device_unbind()
 */
struct iommufd_device *iommufd_device_bind(struct iommufd_ctx *ictx,
					   struct device *dev, u32 *id)
{
	struct iommufd_device *idev;
	struct iommu_group *group;
	int rc;

	/*
	 * iommufd always sets IOMMU_CACHE because we offer no way for userspace
	 * to restore cache coherency.
	 */
	if (!device_iommu_capable(dev, IOMMU_CAP_CACHE_COHERENCY))
		return ERR_PTR(-EINVAL);

	group = iommu_group_get(dev);
	if (!group)
		return ERR_PTR(-ENODEV);

	rc = iommu_device_claim_dma_owner(dev, ictx);
	if (rc)
		goto out_group_put;

	idev = iommufd_object_alloc(ictx, idev, IOMMUFD_OBJ_DEVICE);
	if (IS_ERR(idev)) {
		rc = PTR_ERR(idev);
		goto out_release_owner;
	}
	idev->ictx = ictx;
	iommufd_ctx_get(ictx);
	idev->dev = dev;
	idev->enforce_cache_coherency =
		device_iommu_capable(dev, IOMMU_CAP_ENFORCE_CACHE_COHERENCY);
	/* The calling driver is a user until iommufd_device_unbind() */
	refcount_inc(&idev->obj.users);
	/* group refcount moves into iommufd_device */
	idev->group = group;

	/*
	 * If the caller fails after this success it must call
	 * iommufd_unbind_device() which is safe since we hold this refcount.
	 * This also means the device is a leaf in the graph and no other object
	 * can take a reference on it.
	 */
	iommufd_object_finalize(ictx, &idev->obj);
	*id = idev->obj.id;
	return idev;

out_release_owner:
	iommu_device_release_dma_owner(dev);
out_group_put:
	iommu_group_put(group);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_bind, IOMMUFD);

/**
 * iommufd_device_unbind - Undo iommufd_device_bind()
 * @idev: Device returned by iommufd_device_bind()
 *
 * Release the device from iommufd control. The DMA ownership will return back
 * to unowned with DMA controlled by the DMA API. This invalidates the
 * iommufd_device pointer, other APIs that consume it must not be called
 * concurrently.
 */
void iommufd_device_unbind(struct iommufd_device *idev)
{
	bool was_destroyed;

	was_destroyed = iommufd_object_destroy_user(idev->ictx, &idev->obj);
	WARN_ON(!was_destroyed);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_unbind, IOMMUFD);

static int iommufd_device_setup_msi(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt,
				    phys_addr_t sw_msi_start)
{
	int rc;

	/*
	 * IOMMU_CAP_INTR_REMAP means that the platform is isolating MSI, and it
	 * creates the MSI window by default in the iommu domain. Nothing
	 * further to do.
	 */
	if (device_iommu_capable(idev->dev, IOMMU_CAP_INTR_REMAP))
		return 0;

	/*
	 * On ARM systems that set the global IRQ_DOMAIN_FLAG_MSI_REMAP every
	 * allocated iommu_domain will block interrupts by default and this
	 * special flow is needed to turn them back on. iommu_dma_prepare_msi()
	 * will install pages into our domain after request_irq() to make this
	 * work.
	 *
	 * FIXME: This is conceptually broken for iommufd since we want to allow
	 * userspace to change the domains, eg switch from an identity IOAS to a
	 * DMA IOAS. There is currently no way to create a MSI window that
	 * matches what the IRQ layer actually expects in a newly created
	 * domain.
	 */
	if (irq_domain_check_msi_remap()) {
		if (WARN_ON(!sw_msi_start))
			return -EPERM;
		/*
		 * iommu_get_msi_cookie() can only be called once per domain,
		 * it returns -EBUSY on later calls.
		 */
		if (hwpt->msi_cookie)
			return 0;
		rc = iommu_get_msi_cookie(hwpt->domain, sw_msi_start);
		if (rc)
			return rc;
		hwpt->msi_cookie = true;
		return 0;
	}

	/*
	 * Otherwise the platform has a MSI window that is not isolated. For
	 * historical compat with VFIO allow a module parameter to ignore the
	 * insecurity.
	 */
	if (!allow_unsafe_interrupts)
		return -EPERM;

	dev_warn(
		idev->dev,
		"MSI interrupt window cannot be isolated by the IOMMU, this platform is insecure. Use the \"allow_unsafe_interrupts\" module parameter to override\n");
	return 0;
}

static bool iommufd_hw_pagetable_has_group(struct iommufd_hw_pagetable *hwpt,
					   struct iommu_group *group)
{
	struct iommufd_device *cur_dev;

	list_for_each_entry(cur_dev, &hwpt->devices, devices_item)
		if (cur_dev->group == group)
			return true;
	return false;
}

static int iommufd_device_do_attach(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt)
{
	phys_addr_t sw_msi_start = 0;
	int rc;

	mutex_lock(&hwpt->devices_lock);

	/*
	 * Try to upgrade the domain we have, it is an iommu driver bug to
	 * report IOMMU_CAP_ENFORCE_CACHE_COHERENCY but fail
	 * enforce_cache_coherency when there are no devices attached to the
	 * domain.
	 */
	if (idev->enforce_cache_coherency && !hwpt->enforce_cache_coherency) {
		if (hwpt->domain->ops->enforce_cache_coherency)
			hwpt->enforce_cache_coherency =
				hwpt->domain->ops->enforce_cache_coherency(
					hwpt->domain);
		if (!hwpt->enforce_cache_coherency) {
			WARN_ON(list_empty(&hwpt->devices));
			rc = -EINVAL;
			goto out_unlock;
		}
	}

	rc = iopt_table_enforce_group_resv_regions(&hwpt->ioas->iopt, idev->dev,
						   idev->group, &sw_msi_start);
	if (rc)
		goto out_unlock;

	rc = iommufd_device_setup_msi(idev, hwpt, sw_msi_start);
	if (rc)
		goto out_iova;

	/*
	 * FIXME: Hack around missing a device-centric iommu api, only attach to
	 * the group once for the first device that is in the group.
	 */
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		rc = iommu_attach_group(hwpt->domain, idev->group);
		if (rc)
			goto out_iova;

		if (list_empty(&hwpt->devices)) {
			rc = iopt_table_add_domain(&hwpt->ioas->iopt,
						   hwpt->domain);
			if (rc)
				goto out_detach;
		}
	}

	idev->hwpt = hwpt;
	refcount_inc(&hwpt->obj.users);
	list_add(&idev->devices_item, &hwpt->devices);
	mutex_unlock(&hwpt->devices_lock);
	return 0;

out_detach:
	iommu_detach_group(hwpt->domain, idev->group);
out_iova:
	iopt_remove_reserved_iova(&hwpt->ioas->iopt, idev->dev);
out_unlock:
	mutex_unlock(&hwpt->devices_lock);
	return rc;
}

/*
 * When automatically managing the domains we search for a compatible domain in
 * the iopt and if one is found use it, otherwise create a new domain.
 * Automatic domain selection will never pick a manually created domain.
 */
static int iommufd_device_auto_get_domain(struct iommufd_device *idev,
					  struct iommufd_ioas *ioas)
{
	struct iommufd_hw_pagetable *hwpt;
	int rc;

	/*
	 * There is no differentiation when domains are allocated, so any domain
	 * that is willing to attach to the device is interchangeable with any
	 * other.
	 */
	mutex_lock(&ioas->mutex);
	list_for_each_entry(hwpt, &ioas->hwpt_list, hwpt_item) {
		if (!hwpt->auto_domain)
			continue;

		rc = iommufd_device_do_attach(idev, hwpt);

		/*
		 * -EINVAL means the domain is incompatible with the device.
		 * Other error codes should propagate to userspace as failure.
		 * Success means the domain is attached.
		 */
		if (rc == -EINVAL)
			continue;
		goto out_unlock;
	}

	hwpt = iommufd_hw_pagetable_alloc(idev->ictx, ioas, idev->dev);
	if (IS_ERR(hwpt)) {
		rc = PTR_ERR(hwpt);
		goto out_unlock;
	}
	hwpt->auto_domain = true;

	rc = iommufd_device_do_attach(idev, hwpt);
	if (rc)
		goto out_abort;
	list_add_tail(&hwpt->hwpt_item, &ioas->hwpt_list);

	mutex_unlock(&ioas->mutex);
	iommufd_object_finalize(idev->ictx, &hwpt->obj);
	return 0;

out_abort:
	iommufd_object_abort_and_destroy(idev->ictx, &hwpt->obj);
out_unlock:
	mutex_unlock(&ioas->mutex);
	return rc;
}

/**
 * iommufd_device_attach - Connect a device from an iommu_domain
 * @idev: device to attach
 * @pt_id: Input a IOMMUFD_OBJ_IOAS, or IOMMUFD_OBJ_HW_PAGETABLE
 *         Output the IOMMUFD_OBJ_HW_PAGETABLE ID
 *
 * This connects the device to an iommu_domain, either automatically or manually
 * selected. Once this completes the device could do DMA.
 *
 * The caller should return the resulting pt_id back to userspace.
 * This function is undone by calling iommufd_device_detach().
 */
int iommufd_device_attach(struct iommufd_device *idev, u32 *pt_id)
{
	struct iommufd_object *pt_obj;
	int rc;

	pt_obj = iommufd_get_object(idev->ictx, *pt_id, IOMMUFD_OBJ_ANY);
	if (IS_ERR(pt_obj))
		return PTR_ERR(pt_obj);

	switch (pt_obj->type) {
	case IOMMUFD_OBJ_HW_PAGETABLE: {
		struct iommufd_hw_pagetable *hwpt =
			container_of(pt_obj, struct iommufd_hw_pagetable, obj);

		rc = iommufd_device_do_attach(idev, hwpt);
		if (rc)
			goto out_put_pt_obj;

		mutex_lock(&hwpt->ioas->mutex);
		list_add_tail(&hwpt->hwpt_item, &hwpt->ioas->hwpt_list);
		mutex_unlock(&hwpt->ioas->mutex);
		break;
	}
	case IOMMUFD_OBJ_IOAS: {
		struct iommufd_ioas *ioas =
			container_of(pt_obj, struct iommufd_ioas, obj);

		rc = iommufd_device_auto_get_domain(idev, ioas);
		if (rc)
			goto out_put_pt_obj;
		break;
	}
	default:
		rc = -EINVAL;
		goto out_put_pt_obj;
	}

	refcount_inc(&idev->obj.users);
	*pt_id = idev->hwpt->obj.id;
	rc = 0;

out_put_pt_obj:
	iommufd_put_object(pt_obj);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_attach, IOMMUFD);

/**
 * iommufd_device_detach - Disconnect a device to an iommu_domain
 * @idev: device to detach
 *
 * Undo iommufd_device_attach(). This disconnects the idev from the previously
 * attached pt_id. The device returns back to a blocked DMA translation.
 */
void iommufd_device_detach(struct iommufd_device *idev)
{
	struct iommufd_hw_pagetable *hwpt = idev->hwpt;

	mutex_lock(&hwpt->ioas->mutex);
	mutex_lock(&hwpt->devices_lock);
	list_del(&idev->devices_item);
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		if (list_empty(&hwpt->devices)) {
			iopt_table_remove_domain(&hwpt->ioas->iopt,
						 hwpt->domain);
			list_del(&hwpt->hwpt_item);
		}
		iommu_detach_group(hwpt->domain, idev->group);
	}
	iopt_remove_reserved_iova(&hwpt->ioas->iopt, idev->dev);
	mutex_unlock(&hwpt->devices_lock);
	mutex_unlock(&hwpt->ioas->mutex);

	if (hwpt->auto_domain)
		iommufd_object_destroy_user(idev->ictx, &hwpt->obj);
	else
		refcount_dec(&hwpt->obj.users);

	idev->hwpt = NULL;

	refcount_dec(&idev->obj.users);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_detach, IOMMUFD);
