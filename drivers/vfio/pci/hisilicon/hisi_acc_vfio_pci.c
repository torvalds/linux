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

static int hisi_acc_pci_rw_access_check(struct vfio_device *core_vdev,
					size_t count, loff_t *ppos,
					size_t *new_count)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (index == VFIO_PCI_BAR2_REGION_INDEX) {
		loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
		resource_size_t end = pci_resource_len(vdev->pdev, index) / 2;

		/* Check if access is for migration control region */
		if (pos >= end)
			return -EINVAL;

		*new_count = min(count, (size_t)(end - pos));
	}

	return 0;
}

static int hisi_acc_vfio_pci_mmap(struct vfio_device *core_vdev,
				  struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	unsigned int index;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);
	if (index == VFIO_PCI_BAR2_REGION_INDEX) {
		u64 req_len, pgoff, req_start;
		resource_size_t end = pci_resource_len(vdev->pdev, index) / 2;

		req_len = vma->vm_end - vma->vm_start;
		pgoff = vma->vm_pgoff &
			((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
		req_start = pgoff << PAGE_SHIFT;

		if (req_start + req_len > end)
			return -EINVAL;
	}

	return vfio_pci_core_mmap(core_vdev, vma);
}

static ssize_t hisi_acc_vfio_pci_write(struct vfio_device *core_vdev,
				       const char __user *buf, size_t count,
				       loff_t *ppos)
{
	size_t new_count = count;
	int ret;

	ret = hisi_acc_pci_rw_access_check(core_vdev, count, ppos, &new_count);
	if (ret)
		return ret;

	return vfio_pci_core_write(core_vdev, buf, new_count, ppos);
}

static ssize_t hisi_acc_vfio_pci_read(struct vfio_device *core_vdev,
				      char __user *buf, size_t count,
				      loff_t *ppos)
{
	size_t new_count = count;
	int ret;

	ret = hisi_acc_pci_rw_access_check(core_vdev, count, ppos, &new_count);
	if (ret)
		return ret;

	return vfio_pci_core_read(core_vdev, buf, new_count, ppos);
}

static long hisi_acc_vfio_pci_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
				    unsigned long arg)
{
	if (cmd == VFIO_DEVICE_GET_REGION_INFO) {
		struct vfio_pci_core_device *vdev =
			container_of(core_vdev, struct vfio_pci_core_device, vdev);
		struct pci_dev *pdev = vdev->pdev;
		struct vfio_region_info info;
		unsigned long minsz;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index == VFIO_PCI_BAR2_REGION_INDEX) {
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);

			/*
			 * ACC VF dev BAR2 region consists of both functional
			 * register space and migration control register space.
			 * Report only the functional region to Guest.
			 */
			info.size = pci_resource_len(pdev, info.index) / 2;

			info.flags = VFIO_REGION_INFO_FLAG_READ |
					VFIO_REGION_INFO_FLAG_WRITE |
					VFIO_REGION_INFO_FLAG_MMAP;

			return copy_to_user((void __user *)arg, &info, minsz) ?
					    -EFAULT : 0;
		}
	}
	return vfio_pci_core_ioctl(core_vdev, cmd, arg);
}

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

static const struct vfio_device_ops hisi_acc_vfio_pci_migrn_ops = {
	.name = "hisi-acc-vfio-pci-migration",
	.open_device = hisi_acc_vfio_pci_open_device,
	.close_device = vfio_pci_core_close_device,
	.ioctl = hisi_acc_vfio_pci_ioctl,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = hisi_acc_vfio_pci_read,
	.write = hisi_acc_vfio_pci_write,
	.mmap = hisi_acc_vfio_pci_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
};

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
