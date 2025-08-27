// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/vfio_pci_core.h>
#include <linux/nvgrace-egm.h>

#define MAX_EGM_NODES 4

static dev_t dev;
static struct class *class;
static DEFINE_XARRAY(egm_chardevs);

struct chardev {
	struct device device;
	struct cdev cdev;
};

static struct nvgrace_egm_dev *
egm_chardev_to_nvgrace_egm_dev(struct chardev *egm_chardev)
{
	struct auxiliary_device *aux_dev =
		container_of(egm_chardev->device.parent, struct auxiliary_device, dev);

	return container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);
}

static int nvgrace_egm_open(struct inode *inode, struct file *file)
{
	struct chardev *egm_chardev =
		container_of(inode->i_cdev, struct chardev, cdev);

	file->private_data = egm_chardev;

	return 0;
}

static int nvgrace_egm_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static int nvgrace_egm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct chardev *egm_chardev = file->private_data;
	struct nvgrace_egm_dev *egm_dev =
		egm_chardev_to_nvgrace_egm_dev(egm_chardev);

	/*
	 * EGM memory is invisible to the host kernel and is not managed
	 * by it. Map the usermode VMA to the EGM region.
	 */
	return remap_pfn_range(vma, vma->vm_start,
			       PHYS_PFN(egm_dev->egmphys),
			       (vma->vm_end - vma->vm_start),
			       vma->vm_page_prot);
}

static const struct file_operations file_ops = {
	.owner = THIS_MODULE,
	.open = nvgrace_egm_open,
	.release = nvgrace_egm_release,
	.mmap = nvgrace_egm_mmap,
};

static void egm_chardev_release(struct device *dev)
{
	struct chardev *egm_chardev = container_of(dev, struct chardev, device);

	kvfree(egm_chardev);
}

static struct chardev *
setup_egm_chardev(struct nvgrace_egm_dev *egm_dev)
{
	struct chardev *egm_chardev;
	int ret;

	egm_chardev = kvzalloc(sizeof(*egm_chardev), GFP_KERNEL);
	if (!egm_chardev)
		goto create_err;

	device_initialize(&egm_chardev->device);

	/*
	 * Use the proximity domain number as the device minor
	 * number. So the EGM corresponding to node X would be
	 * /dev/egmX.
	 */
	egm_chardev->device.devt = MKDEV(MAJOR(dev), egm_dev->egmpxm);
	egm_chardev->device.class = class;
	egm_chardev->device.release = egm_chardev_release;
	egm_chardev->device.parent = &egm_dev->aux_dev.dev;
	cdev_init(&egm_chardev->cdev, &file_ops);
	egm_chardev->cdev.owner = THIS_MODULE;

	ret = dev_set_name(&egm_chardev->device, "egm%lld", egm_dev->egmpxm);
	if (ret)
		goto error_exit;

	ret = cdev_device_add(&egm_chardev->cdev, &egm_chardev->device);
	if (ret)
		goto error_exit;

	return egm_chardev;

error_exit:
	kvfree(egm_chardev);
create_err:
	return NULL;
}

static void del_egm_chardev(struct chardev *egm_chardev)
{
	cdev_device_del(&egm_chardev->cdev, &egm_chardev->device);
	put_device(&egm_chardev->device);
}

static int egm_driver_probe(struct auxiliary_device *aux_dev,
			    const struct auxiliary_device_id *id)
{
	struct nvgrace_egm_dev *egm_dev =
		container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);
	struct chardev *egm_chardev;

	egm_chardev = setup_egm_chardev(egm_dev);
	if (!egm_chardev)
		return -EINVAL;

	xa_store(&egm_chardevs, egm_dev->egmpxm, egm_chardev, GFP_KERNEL);

	return 0;
}

static void egm_driver_remove(struct auxiliary_device *aux_dev)
{
	struct nvgrace_egm_dev *egm_dev =
		container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);
	struct chardev *egm_chardev = xa_erase(&egm_chardevs, egm_dev->egmpxm);

	if (!egm_chardev)
		return;

	del_egm_chardev(egm_chardev);
}

static const struct auxiliary_device_id egm_id_table[] = {
	{ .name = "nvgrace_gpu_vfio_pci.egm" },
	{ .name = "nvidia_vgpu_vfio.egm" },
	{ },
};
MODULE_DEVICE_TABLE(auxiliary, egm_id_table);

static struct auxiliary_driver egm_driver = {
	.name = KBUILD_MODNAME,
	.id_table = egm_id_table,
	.probe = egm_driver_probe,
	.remove = egm_driver_remove,
};

static char *egm_devnode(const struct device *device, umode_t *mode)
{
	if (mode)
		*mode = 0600;

	return NULL;
}

static int __init nvgrace_egm_init(void)
{
	int ret;

	/*
	 * Each EGM region on a system is represented with a unique
	 * char device with a different minor number. Allow a range
	 * of char device creation.
	 */
	ret = alloc_chrdev_region(&dev, 0, MAX_EGM_NODES,
				  NVGRACE_EGM_DEV_NAME);
	if (ret < 0)
		return ret;

	class = class_create(NVGRACE_EGM_DEV_NAME);
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		goto unregister_chrdev;
	}

	class->devnode = egm_devnode;

	ret = auxiliary_driver_register(&egm_driver);
	if (!ret)
		goto fn_exit;

	class_destroy(class);
unregister_chrdev:
	unregister_chrdev_region(dev, MAX_EGM_NODES);
fn_exit:
	return ret;
}

static void __exit nvgrace_egm_cleanup(void)
{
	class_destroy(class);
	unregister_chrdev_region(dev, MAX_EGM_NODES);
	auxiliary_driver_unregister(&egm_driver);
}

module_init(nvgrace_egm_init);
module_exit(nvgrace_egm_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ankit Agrawal <ankita@nvidia.com>");
MODULE_DESCRIPTION("NVGRACE EGM - Module to support Extended GPU Memory on NVIDIA Grace Based systems");
