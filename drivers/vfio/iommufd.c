// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/vfio.h>
#include <linux/iommufd.h>

#include "vfio.h"

MODULE_IMPORT_NS(IOMMUFD);
MODULE_IMPORT_NS(IOMMUFD_VFIO);

int vfio_iommufd_bind(struct vfio_device *vdev, struct iommufd_ctx *ictx)
{
	u32 ioas_id;
	u32 device_id;
	int ret;

	lockdep_assert_held(&vdev->dev_set->lock);

	if (vfio_device_is_noiommu(vdev)) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;

		/*
		 * Require no compat ioas to be assigned to proceed. The basic
		 * statement is that the user cannot have done something that
		 * implies they expected translation to exist
		 */
		if (!iommufd_vfio_compat_ioas_get_id(ictx, &ioas_id))
			return -EPERM;
		return 0;
	}

	/*
	 * If the driver doesn't provide this op then it means the device does
	 * not do DMA at all. So nothing to do.
	 */
	if (!vdev->ops->bind_iommufd)
		return 0;

	ret = vdev->ops->bind_iommufd(vdev, ictx, &device_id);
	if (ret)
		return ret;

	ret = iommufd_vfio_compat_ioas_get_id(ictx, &ioas_id);
	if (ret)
		goto err_unbind;
	ret = vdev->ops->attach_ioas(vdev, &ioas_id);
	if (ret)
		goto err_unbind;

	/*
	 * The legacy path has no way to return the device id or the selected
	 * pt_id
	 */
	return 0;

err_unbind:
	if (vdev->ops->unbind_iommufd)
		vdev->ops->unbind_iommufd(vdev);
	return ret;
}

void vfio_iommufd_unbind(struct vfio_device *vdev)
{
	lockdep_assert_held(&vdev->dev_set->lock);

	if (vfio_device_is_noiommu(vdev))
		return;

	if (vdev->ops->unbind_iommufd)
		vdev->ops->unbind_iommufd(vdev);
}

/*
 * The physical standard ops mean that the iommufd_device is bound to the
 * physical device vdev->dev that was provided to vfio_init_group_dev(). Drivers
 * using this ops set should call vfio_register_group_dev()
 */
int vfio_iommufd_physical_bind(struct vfio_device *vdev,
			       struct iommufd_ctx *ictx, u32 *out_device_id)
{
	struct iommufd_device *idev;

	idev = iommufd_device_bind(ictx, vdev->dev, out_device_id);
	if (IS_ERR(idev))
		return PTR_ERR(idev);
	vdev->iommufd_device = idev;
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_physical_bind);

void vfio_iommufd_physical_unbind(struct vfio_device *vdev)
{
	lockdep_assert_held(&vdev->dev_set->lock);

	if (vdev->iommufd_attached) {
		iommufd_device_detach(vdev->iommufd_device);
		vdev->iommufd_attached = false;
	}
	iommufd_device_unbind(vdev->iommufd_device);
	vdev->iommufd_device = NULL;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_physical_unbind);

int vfio_iommufd_physical_attach_ioas(struct vfio_device *vdev, u32 *pt_id)
{
	int rc;

	rc = iommufd_device_attach(vdev->iommufd_device, pt_id);
	if (rc)
		return rc;
	vdev->iommufd_attached = true;
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_physical_attach_ioas);

/*
 * The emulated standard ops mean that vfio_device is going to use the
 * "mdev path" and will call vfio_pin_pages()/vfio_dma_rw(). Drivers using this
 * ops set should call vfio_register_emulated_iommu_dev().
 */

static void vfio_emulated_unmap(void *data, unsigned long iova,
				unsigned long length)
{
	struct vfio_device *vdev = data;

	vdev->ops->dma_unmap(vdev, iova, length);
}

static const struct iommufd_access_ops vfio_user_ops = {
	.needs_pin_pages = 1,
	.unmap = vfio_emulated_unmap,
};

int vfio_iommufd_emulated_bind(struct vfio_device *vdev,
			       struct iommufd_ctx *ictx, u32 *out_device_id)
{
	lockdep_assert_held(&vdev->dev_set->lock);

	vdev->iommufd_ictx = ictx;
	iommufd_ctx_get(ictx);
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_emulated_bind);

void vfio_iommufd_emulated_unbind(struct vfio_device *vdev)
{
	lockdep_assert_held(&vdev->dev_set->lock);

	if (vdev->iommufd_access) {
		iommufd_access_destroy(vdev->iommufd_access);
		vdev->iommufd_access = NULL;
	}
	iommufd_ctx_put(vdev->iommufd_ictx);
	vdev->iommufd_ictx = NULL;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_emulated_unbind);

int vfio_iommufd_emulated_attach_ioas(struct vfio_device *vdev, u32 *pt_id)
{
	struct iommufd_access *user;

	lockdep_assert_held(&vdev->dev_set->lock);

	user = iommufd_access_create(vdev->iommufd_ictx, *pt_id, &vfio_user_ops,
				     vdev);
	if (IS_ERR(user))
		return PTR_ERR(user);
	vdev->iommufd_access = user;
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_iommufd_emulated_attach_ioas);
