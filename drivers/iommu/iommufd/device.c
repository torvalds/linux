// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/iommufd.h>
#include <linux/slab.h>
#include <linux/iommu.h>

#include "io_pagetable.h"
#include "iommufd_private.h"

static bool allow_unsafe_interrupts;
module_param(allow_unsafe_interrupts, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(
	allow_unsafe_interrupts,
	"Allow IOMMUFD to bind to devices even if the platform cannot isolate "
	"the MSI interrupt window. Enabling this is a security weakness.");

void iommufd_device_destroy(struct iommufd_object *obj)
{
	struct iommufd_device *idev =
		container_of(obj, struct iommufd_device, obj);

	iommu_device_release_dma_owner(idev->dev);
	iommu_group_put(idev->group);
	if (!iommufd_selftest_is_mock_dev(idev->dev))
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
	if (!iommufd_selftest_is_mock_dev(dev))
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
	iommufd_object_destroy_user(idev->ictx, &idev->obj);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_unbind, IOMMUFD);

static int iommufd_device_setup_msi(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt,
				    phys_addr_t sw_msi_start)
{
	int rc;

	/*
	 * If the IOMMU driver gives a IOMMU_RESV_SW_MSI then it is asking us to
	 * call iommu_get_msi_cookie() on its behalf. This is necessary to setup
	 * the MSI window so iommu_dma_prepare_msi() can install pages into our
	 * domain after request_irq(). If it is not done interrupts will not
	 * work on this domain.
	 *
	 * FIXME: This is conceptually broken for iommufd since we want to allow
	 * userspace to change the domains, eg switch from an identity IOAS to a
	 * DMA IOAS. There is currently no way to create a MSI window that
	 * matches what the IRQ layer actually expects in a newly created
	 * domain.
	 */
	if (sw_msi_start != PHYS_ADDR_MAX && !hwpt->msi_cookie) {
		rc = iommu_get_msi_cookie(hwpt->domain, sw_msi_start);
		if (rc)
			return rc;

		/*
		 * iommu_get_msi_cookie() can only be called once per domain,
		 * it returns -EBUSY on later calls.
		 */
		hwpt->msi_cookie = true;
	}

	/*
	 * For historical compat with VFIO the insecure interrupt path is
	 * allowed if the module parameter is set. Insecure means that a MemWr
	 * operation from the device (eg a simple DMA) cannot trigger an
	 * interrupt outside this iommufd context.
	 */
	if (!iommufd_selftest_is_mock_dev(idev->dev) &&
	    !iommu_group_has_isolated_msi(idev->group)) {
		if (!allow_unsafe_interrupts)
			return -EPERM;

		dev_warn(
			idev->dev,
			"MSI interrupts are not secure, they cannot be isolated by the platform. "
			"Check that platform features like interrupt remapping are enabled. "
			"Use the \"allow_unsafe_interrupts\" module parameter to override\n");
	}
	return 0;
}

static bool iommufd_hw_pagetable_has_group(struct iommufd_hw_pagetable *hwpt,
					   struct iommu_group *group)
{
	struct iommufd_device *cur_dev;

	lockdep_assert_held(&hwpt->devices_lock);

	list_for_each_entry(cur_dev, &hwpt->devices, devices_item)
		if (cur_dev->group == group)
			return true;
	return false;
}

int iommufd_hw_pagetable_attach(struct iommufd_hw_pagetable *hwpt,
				struct iommufd_device *idev)
{
	phys_addr_t sw_msi_start = PHYS_ADDR_MAX;
	int rc;

	lockdep_assert_held(&hwpt->devices_lock);

	if (WARN_ON(idev->hwpt))
		return -EINVAL;

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
			return -EINVAL;
		}
	}

	rc = iopt_table_enforce_group_resv_regions(&hwpt->ioas->iopt, idev->dev,
						   idev->group, &sw_msi_start);
	if (rc)
		return rc;

	rc = iommufd_device_setup_msi(idev, hwpt, sw_msi_start);
	if (rc)
		goto err_unresv;

	/*
	 * FIXME: Hack around missing a device-centric iommu api, only attach to
	 * the group once for the first device that is in the group.
	 */
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group)) {
		rc = iommu_attach_group(hwpt->domain, idev->group);
		if (rc)
			goto err_unresv;
	}
	return 0;
err_unresv:
	iopt_remove_reserved_iova(&hwpt->ioas->iopt, idev->dev);
	return rc;
}

void iommufd_hw_pagetable_detach(struct iommufd_hw_pagetable *hwpt,
				 struct iommufd_device *idev)
{
	if (!iommufd_hw_pagetable_has_group(hwpt, idev->group))
		iommu_detach_group(hwpt->domain, idev->group);
	iopt_remove_reserved_iova(&hwpt->ioas->iopt, idev->dev);
}

static int iommufd_device_do_attach(struct iommufd_device *idev,
				    struct iommufd_hw_pagetable *hwpt)
{
	int rc;

	mutex_lock(&hwpt->devices_lock);
	rc = iommufd_hw_pagetable_attach(hwpt, idev);
	if (rc)
		goto out_unlock;

	idev->hwpt = hwpt;
	refcount_inc(&hwpt->obj.users);
	list_add(&idev->devices_item, &hwpt->devices);
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

		if (!iommufd_lock_obj(&hwpt->obj))
			continue;
		rc = iommufd_device_do_attach(idev, hwpt);
		iommufd_put_object(&hwpt->obj);

		/*
		 * -EINVAL means the domain is incompatible with the device.
		 * Other error codes should propagate to userspace as failure.
		 * Success means the domain is attached.
		 */
		if (rc == -EINVAL)
			continue;
		goto out_unlock;
	}

	hwpt = iommufd_hw_pagetable_alloc(idev->ictx, ioas, idev, true);
	if (IS_ERR(hwpt)) {
		rc = PTR_ERR(hwpt);
		goto out_unlock;
	}
	hwpt->auto_domain = true;

	mutex_unlock(&ioas->mutex);
	iommufd_object_finalize(idev->ictx, &hwpt->obj);
	return 0;
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

	mutex_lock(&hwpt->devices_lock);
	list_del(&idev->devices_item);
	idev->hwpt = NULL;
	iommufd_hw_pagetable_detach(hwpt, idev);
	mutex_unlock(&hwpt->devices_lock);

	if (hwpt->auto_domain)
		iommufd_object_deref_user(idev->ictx, &hwpt->obj);
	else
		refcount_dec(&hwpt->obj.users);

	refcount_dec(&idev->obj.users);
}
EXPORT_SYMBOL_NS_GPL(iommufd_device_detach, IOMMUFD);

void iommufd_access_destroy_object(struct iommufd_object *obj)
{
	struct iommufd_access *access =
		container_of(obj, struct iommufd_access, obj);

	if (access->ioas) {
		iopt_remove_access(&access->ioas->iopt, access);
		refcount_dec(&access->ioas->obj.users);
		access->ioas = NULL;
	}
	iommufd_ctx_put(access->ictx);
}

/**
 * iommufd_access_create - Create an iommufd_access
 * @ictx: iommufd file descriptor
 * @ops: Driver's ops to associate with the access
 * @data: Opaque data to pass into ops functions
 * @id: Output ID number to return to userspace for this access
 *
 * An iommufd_access allows a driver to read/write to the IOAS without using
 * DMA. The underlying CPU memory can be accessed using the
 * iommufd_access_pin_pages() or iommufd_access_rw() functions.
 *
 * The provided ops are required to use iommufd_access_pin_pages().
 */
struct iommufd_access *
iommufd_access_create(struct iommufd_ctx *ictx,
		      const struct iommufd_access_ops *ops, void *data, u32 *id)
{
	struct iommufd_access *access;

	/*
	 * There is no uAPI for the access object, but to keep things symmetric
	 * use the object infrastructure anyhow.
	 */
	access = iommufd_object_alloc(ictx, access, IOMMUFD_OBJ_ACCESS);
	if (IS_ERR(access))
		return access;

	access->data = data;
	access->ops = ops;

	if (ops->needs_pin_pages)
		access->iova_alignment = PAGE_SIZE;
	else
		access->iova_alignment = 1;

	/* The calling driver is a user until iommufd_access_destroy() */
	refcount_inc(&access->obj.users);
	access->ictx = ictx;
	iommufd_ctx_get(ictx);
	iommufd_object_finalize(ictx, &access->obj);
	*id = access->obj.id;
	return access;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_create, IOMMUFD);

/**
 * iommufd_access_destroy - Destroy an iommufd_access
 * @access: The access to destroy
 *
 * The caller must stop using the access before destroying it.
 */
void iommufd_access_destroy(struct iommufd_access *access)
{
	iommufd_object_destroy_user(access->ictx, &access->obj);
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_destroy, IOMMUFD);

int iommufd_access_attach(struct iommufd_access *access, u32 ioas_id)
{
	struct iommufd_ioas *new_ioas;
	int rc = 0;

	if (access->ioas)
		return -EINVAL;

	new_ioas = iommufd_get_ioas(access->ictx, ioas_id);
	if (IS_ERR(new_ioas))
		return PTR_ERR(new_ioas);

	rc = iopt_add_access(&new_ioas->iopt, access);
	if (rc) {
		iommufd_put_object(&new_ioas->obj);
		return rc;
	}
	iommufd_ref_to_users(&new_ioas->obj);

	access->ioas = new_ioas;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_attach, IOMMUFD);

/**
 * iommufd_access_notify_unmap - Notify users of an iopt to stop using it
 * @iopt: iopt to work on
 * @iova: Starting iova in the iopt
 * @length: Number of bytes
 *
 * After this function returns there should be no users attached to the pages
 * linked to this iopt that intersect with iova,length. Anyone that has attached
 * a user through iopt_access_pages() needs to detach it through
 * iommufd_access_unpin_pages() before this function returns.
 *
 * iommufd_access_destroy() will wait for any outstanding unmap callback to
 * complete. Once iommufd_access_destroy() no unmap ops are running or will
 * run in the future. Due to this a driver must not create locking that prevents
 * unmap to complete while iommufd_access_destroy() is running.
 */
void iommufd_access_notify_unmap(struct io_pagetable *iopt, unsigned long iova,
				 unsigned long length)
{
	struct iommufd_ioas *ioas =
		container_of(iopt, struct iommufd_ioas, iopt);
	struct iommufd_access *access;
	unsigned long index;

	xa_lock(&ioas->iopt.access_list);
	xa_for_each(&ioas->iopt.access_list, index, access) {
		if (!iommufd_lock_obj(&access->obj))
			continue;
		xa_unlock(&ioas->iopt.access_list);

		access->ops->unmap(access->data, iova, length);

		iommufd_put_object(&access->obj);
		xa_lock(&ioas->iopt.access_list);
	}
	xa_unlock(&ioas->iopt.access_list);
}

/**
 * iommufd_access_unpin_pages() - Undo iommufd_access_pin_pages
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @length: Number of bytes to access
 *
 * Return the struct page's. The caller must stop accessing them before calling
 * this. The iova/length must exactly match the one provided to access_pages.
 */
void iommufd_access_unpin_pages(struct iommufd_access *access,
				unsigned long iova, unsigned long length)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	unsigned long last_iova;
	struct iopt_area *area;

	if (WARN_ON(!length) ||
	    WARN_ON(check_add_overflow(iova, length - 1, &last_iova)))
		return;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova)
		iopt_area_remove_access(
			area, iopt_area_iova_to_index(area, iter.cur_iova),
			iopt_area_iova_to_index(
				area,
				min(last_iova, iopt_area_last_iova(area))));
	WARN_ON(!iopt_area_contig_done(&iter));
	up_read(&iopt->iova_rwsem);
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_unpin_pages, IOMMUFD);

static bool iopt_area_contig_is_aligned(struct iopt_area_contig_iter *iter)
{
	if (iopt_area_start_byte(iter->area, iter->cur_iova) % PAGE_SIZE)
		return false;

	if (!iopt_area_contig_done(iter) &&
	    (iopt_area_start_byte(iter->area, iopt_area_last_iova(iter->area)) %
	     PAGE_SIZE) != (PAGE_SIZE - 1))
		return false;
	return true;
}

static bool check_area_prot(struct iopt_area *area, unsigned int flags)
{
	if (flags & IOMMUFD_ACCESS_RW_WRITE)
		return area->iommu_prot & IOMMU_WRITE;
	return area->iommu_prot & IOMMU_READ;
}

/**
 * iommufd_access_pin_pages() - Return a list of pages under the iova
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @length: Number of bytes to access
 * @out_pages: Output page list
 * @flags: IOPMMUFD_ACCESS_RW_* flags
 *
 * Reads @length bytes starting at iova and returns the struct page * pointers.
 * These can be kmap'd by the caller for CPU access.
 *
 * The caller must perform iommufd_access_unpin_pages() when done to balance
 * this.
 *
 * This API always requires a page aligned iova. This happens naturally if the
 * ioas alignment is >= PAGE_SIZE and the iova is PAGE_SIZE aligned. However
 * smaller alignments have corner cases where this API can fail on otherwise
 * aligned iova.
 */
int iommufd_access_pin_pages(struct iommufd_access *access, unsigned long iova,
			     unsigned long length, struct page **out_pages,
			     unsigned int flags)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	unsigned long last_iova;
	struct iopt_area *area;
	int rc;

	/* Driver's ops don't support pin_pages */
	if (IS_ENABLED(CONFIG_IOMMUFD_TEST) &&
	    WARN_ON(access->iova_alignment != PAGE_SIZE || !access->ops->unmap))
		return -EINVAL;

	if (!length)
		return -EINVAL;
	if (check_add_overflow(iova, length - 1, &last_iova))
		return -EOVERFLOW;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova) {
		unsigned long last = min(last_iova, iopt_area_last_iova(area));
		unsigned long last_index = iopt_area_iova_to_index(area, last);
		unsigned long index =
			iopt_area_iova_to_index(area, iter.cur_iova);

		if (area->prevent_access ||
		    !iopt_area_contig_is_aligned(&iter)) {
			rc = -EINVAL;
			goto err_remove;
		}

		if (!check_area_prot(area, flags)) {
			rc = -EPERM;
			goto err_remove;
		}

		rc = iopt_area_add_access(area, index, last_index, out_pages,
					  flags);
		if (rc)
			goto err_remove;
		out_pages += last_index - index + 1;
	}
	if (!iopt_area_contig_done(&iter)) {
		rc = -ENOENT;
		goto err_remove;
	}

	up_read(&iopt->iova_rwsem);
	return 0;

err_remove:
	if (iova < iter.cur_iova) {
		last_iova = iter.cur_iova - 1;
		iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova)
			iopt_area_remove_access(
				area,
				iopt_area_iova_to_index(area, iter.cur_iova),
				iopt_area_iova_to_index(
					area, min(last_iova,
						  iopt_area_last_iova(area))));
	}
	up_read(&iopt->iova_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_pin_pages, IOMMUFD);

/**
 * iommufd_access_rw - Read or write data under the iova
 * @access: IOAS access to act on
 * @iova: Starting IOVA
 * @data: Kernel buffer to copy to/from
 * @length: Number of bytes to access
 * @flags: IOMMUFD_ACCESS_RW_* flags
 *
 * Copy kernel to/from data into the range given by IOVA/length. If flags
 * indicates IOMMUFD_ACCESS_RW_KTHREAD then a large copy can be optimized
 * by changing it into copy_to/from_user().
 */
int iommufd_access_rw(struct iommufd_access *access, unsigned long iova,
		      void *data, size_t length, unsigned int flags)
{
	struct io_pagetable *iopt = &access->ioas->iopt;
	struct iopt_area_contig_iter iter;
	struct iopt_area *area;
	unsigned long last_iova;
	int rc;

	if (!length)
		return -EINVAL;
	if (check_add_overflow(iova, length - 1, &last_iova))
		return -EOVERFLOW;

	down_read(&iopt->iova_rwsem);
	iopt_for_each_contig_area(&iter, area, iopt, iova, last_iova) {
		unsigned long last = min(last_iova, iopt_area_last_iova(area));
		unsigned long bytes = (last - iter.cur_iova) + 1;

		if (area->prevent_access) {
			rc = -EINVAL;
			goto err_out;
		}

		if (!check_area_prot(area, flags)) {
			rc = -EPERM;
			goto err_out;
		}

		rc = iopt_pages_rw_access(
			area->pages, iopt_area_start_byte(area, iter.cur_iova),
			data, bytes, flags);
		if (rc)
			goto err_out;
		data += bytes;
	}
	if (!iopt_area_contig_done(&iter))
		rc = -ENOENT;
err_out:
	up_read(&iopt->iova_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_access_rw, IOMMUFD);
