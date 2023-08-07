// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

#include "vfio_dev.h"

struct pci_dev *pds_vfio_to_pci_dev(struct pds_vfio_pci_device *pds_vfio)
{
	return pds_vfio->vfio_coredev.pdev;
}

struct pds_vfio_pci_device *pds_vfio_pci_drvdata(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_device = dev_get_drvdata(&pdev->dev);

	return container_of(core_device, struct pds_vfio_pci_device,
			    vfio_coredev);
}

static int pds_vfio_init_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	struct pci_dev *pdev = to_pci_dev(vdev->dev);
	int err, vf_id, pci_id;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	err = vfio_pci_core_init_dev(vdev);
	if (err)
		return err;

	pds_vfio->vf_id = vf_id;

	pci_id = PCI_DEVID(pdev->bus->number, pdev->devfn);
	dev_dbg(&pdev->dev,
		"%s: PF %#04x VF %#04x vf_id %d domain %d pds_vfio %p\n",
		__func__, pci_dev_id(pdev->physfn), pci_id, vf_id,
		pci_domain_nr(pdev->bus), pds_vfio);

	return 0;
}

static int pds_vfio_open_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	int err;

	err = vfio_pci_core_enable(&pds_vfio->vfio_coredev);
	if (err)
		return err;

	vfio_pci_core_finish_enable(&pds_vfio->vfio_coredev);

	return 0;
}

static const struct vfio_device_ops pds_vfio_ops = {
	.name = "pds-vfio",
	.init = pds_vfio_init_device,
	.release = vfio_pci_core_release_dev,
	.open_device = pds_vfio_open_device,
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
};

const struct vfio_device_ops *pds_vfio_ops_info(void)
{
	return &pds_vfio_ops;
}
