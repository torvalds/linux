// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, HiSilicon Ltd.
 */

#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/hisi_acc_qm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

static int hisi_acc_vfio_pci_open_device(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	vfio_pci_core_finish_enable(vdev);

	return 0;
}

static const struct vfio_device_ops hisi_acc_vfio_pci_ops = {
	.name = "hisi-acc-vfio-pci",
	.open_device = hisi_acc_vfio_pci_open_device,
	.close_device = vfio_pci_core_close_device,
	.ioctl = vfio_pci_core_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = vfio_pci_core_read,
	.write = vfio_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
};

static int hisi_acc_vfio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct vfio_pci_core_device *vdev;
	int ret;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vfio_pci_core_init_device(vdev, pdev, &hisi_acc_vfio_pci_ops);

	ret = vfio_pci_core_register_device(vdev);
	if (ret)
		goto out_free;

	dev_set_drvdata(&pdev->dev, vdev);

	return 0;

out_free:
	vfio_pci_core_uninit_device(vdev);
	kfree(vdev);
	return ret;
}

static void hisi_acc_vfio_pci_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(&pdev->dev);

	vfio_pci_core_unregister_device(vdev);
	vfio_pci_core_uninit_device(vdev);
	kfree(vdev);
}

static const struct pci_device_id hisi_acc_vfio_pci_table[] = {
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HUAWEI_SEC_VF) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HUAWEI_HPRE_VF) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, PCI_DEVICE_ID_HUAWEI_ZIP_VF) },
	{ }
};

MODULE_DEVICE_TABLE(pci, hisi_acc_vfio_pci_table);

static struct pci_driver hisi_acc_vfio_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = hisi_acc_vfio_pci_table,
	.probe = hisi_acc_vfio_pci_probe,
	.remove = hisi_acc_vfio_pci_remove,
	.err_handler = &vfio_pci_core_err_handlers,
};

module_pci_driver(hisi_acc_vfio_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Liu Longfang <liulongfang@huawei.com>");
MODULE_AUTHOR("Shameer Kolothum <shameerali.kolothum.thodi@huawei.com>");
MODULE_DESCRIPTION("HiSilicon VFIO PCI - Generic VFIO PCI driver for HiSilicon ACC device family");
