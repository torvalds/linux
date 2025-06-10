// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>
#include <linux/virtio_pci.h>
#include <linux/virtio_net.h>
#include <linux/virtio_pci_admin.h>

#include "common.h"

static int virtiovf_pci_open_device(struct vfio_device *core_vdev)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(core_vdev,
			struct virtiovf_pci_core_device, core_device.vdev);
	struct vfio_pci_core_device *vdev = &virtvdev->core_device;
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
	ret = virtiovf_open_legacy_io(virtvdev);
	if (ret) {
		vfio_pci_core_disable(vdev);
		return ret;
	}
#endif

	virtiovf_open_migration(virtvdev);
	vfio_pci_core_finish_enable(vdev);
	return 0;
}

static void virtiovf_pci_close_device(struct vfio_device *core_vdev)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(core_vdev,
			struct virtiovf_pci_core_device, core_device.vdev);

	virtiovf_close_migration(virtvdev);
	vfio_pci_core_close_device(core_vdev);
}

#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
static int virtiovf_pci_init_device(struct vfio_device *core_vdev)
{
	struct virtiovf_pci_core_device *virtvdev = container_of(core_vdev,
			struct virtiovf_pci_core_device, core_device.vdev);
	int ret;

	ret = vfio_pci_core_init_dev(core_vdev);
	if (ret)
		return ret;

	/*
	 * The vfio_device_ops.init() callback is set to virtiovf_pci_init_device()
	 * only when legacy I/O is supported. Now, let's initialize it.
	 */
	return virtiovf_init_legacy_io(virtvdev);
}
#endif

static void virtiovf_pci_core_release_dev(struct vfio_device *core_vdev)
{
#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
	struct virtiovf_pci_core_device *virtvdev = container_of(core_vdev,
			struct virtiovf_pci_core_device, core_device.vdev);

	virtiovf_release_legacy_io(virtvdev);
#endif
	vfio_pci_core_release_dev(core_vdev);
}

static const struct vfio_device_ops virtiovf_vfio_pci_lm_ops = {
	.name = "virtio-vfio-pci-lm",
	.init = vfio_pci_core_init_dev,
	.release = virtiovf_pci_core_release_dev,
	.open_device = virtiovf_pci_open_device,
	.close_device = virtiovf_pci_close_device,
	.ioctl = vfio_pci_core_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = vfio_pci_core_read,
	.write = vfio_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
	.bind_iommufd = vfio_iommufd_physical_bind,
	.unbind_iommufd = vfio_iommufd_physical_unbind,
	.attach_ioas = vfio_iommufd_physical_attach_ioas,
	.detach_ioas = vfio_iommufd_physical_detach_ioas,
};

#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
static const struct vfio_device_ops virtiovf_vfio_pci_tran_lm_ops = {
	.name = "virtio-vfio-pci-trans-lm",
	.init = virtiovf_pci_init_device,
	.release = virtiovf_pci_core_release_dev,
	.open_device = virtiovf_pci_open_device,
	.close_device = virtiovf_pci_close_device,
	.ioctl = virtiovf_vfio_pci_core_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = virtiovf_pci_core_read,
	.write = virtiovf_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
	.bind_iommufd = vfio_iommufd_physical_bind,
	.unbind_iommufd = vfio_iommufd_physical_unbind,
	.attach_ioas = vfio_iommufd_physical_attach_ioas,
	.detach_ioas = vfio_iommufd_physical_detach_ioas,
};
#endif

static const struct vfio_device_ops virtiovf_vfio_pci_ops = {
	.name = "virtio-vfio-pci",
	.init = vfio_pci_core_init_dev,
	.release = vfio_pci_core_release_dev,
	.open_device = virtiovf_pci_open_device,
	.close_device = vfio_pci_core_close_device,
	.ioctl = vfio_pci_core_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = vfio_pci_core_read,
	.write = vfio_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
	.bind_iommufd = vfio_iommufd_physical_bind,
	.unbind_iommufd = vfio_iommufd_physical_unbind,
	.attach_ioas = vfio_iommufd_physical_attach_ioas,
	.detach_ioas = vfio_iommufd_physical_detach_ioas,
};

static int virtiovf_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	const struct vfio_device_ops *ops = &virtiovf_vfio_pci_ops;
	struct virtiovf_pci_core_device *virtvdev;
	bool sup_legacy_io = false;
	bool sup_lm = false;
	int ret;

	if (pdev->is_virtfn) {
#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
		sup_legacy_io = virtiovf_support_legacy_io(pdev);
		if (sup_legacy_io)
			ops = &virtiovf_vfio_pci_tran_lm_ops;
#endif
		sup_lm = virtio_pci_admin_has_dev_parts(pdev);
		if (sup_lm && !sup_legacy_io)
			ops = &virtiovf_vfio_pci_lm_ops;
	}

	virtvdev = vfio_alloc_device(virtiovf_pci_core_device, core_device.vdev,
				     &pdev->dev, ops);
	if (IS_ERR(virtvdev))
		return PTR_ERR(virtvdev);

	if (sup_lm)
		virtiovf_set_migratable(virtvdev);

	dev_set_drvdata(&pdev->dev, &virtvdev->core_device);
	ret = vfio_pci_core_register_device(&virtvdev->core_device);
	if (ret)
		goto out;
	return 0;
out:
	vfio_put_device(&virtvdev->core_device.vdev);
	return ret;
}

static void virtiovf_pci_remove(struct pci_dev *pdev)
{
	struct virtiovf_pci_core_device *virtvdev = dev_get_drvdata(&pdev->dev);

	vfio_pci_core_unregister_device(&virtvdev->core_device);
	vfio_put_device(&virtvdev->core_device.vdev);
}

static const struct pci_device_id virtiovf_pci_table[] = {
	/* Only virtio-net and virtio-block are supported/tested so far */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_REDHAT_QUMRANET, 0x1041) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_REDHAT_QUMRANET, 0x1042) },
	{}
};

MODULE_DEVICE_TABLE(pci, virtiovf_pci_table);

static void virtiovf_pci_aer_reset_done(struct pci_dev *pdev)
{
#ifdef CONFIG_VIRTIO_VFIO_PCI_ADMIN_LEGACY
	virtiovf_legacy_io_reset_done(pdev);
#endif
	virtiovf_migration_reset_done(pdev);
}

static const struct pci_error_handlers virtiovf_err_handlers = {
	.reset_done = virtiovf_pci_aer_reset_done,
	.error_detected = vfio_pci_core_aer_err_detected,
};

static struct pci_driver virtiovf_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = virtiovf_pci_table,
	.probe = virtiovf_pci_probe,
	.remove = virtiovf_pci_remove,
	.err_handler = &virtiovf_err_handlers,
	.driver_managed_dma = true,
};

module_pci_driver(virtiovf_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yishai Hadas <yishaih@nvidia.com>");
MODULE_DESCRIPTION(
	"VIRTIO VFIO PCI - User Level meta-driver for VIRTIO NET and BLOCK devices");
