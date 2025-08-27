// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/vfio_pci_core.h>
#include <linux/nvgrace-egm.h>
#include <linux/egm.h>

#define MAX_EGM_NODES 4

struct h_node {
	unsigned long mem_offset;
	struct hlist_node node;
};

static dev_t dev;
static struct class *class;
static DEFINE_XARRAY(egm_chardevs);

struct chardev {
	struct device device;
	struct cdev cdev;
	atomic_t open_count;
	DECLARE_HASHTABLE(htbl, 0x10);
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
	struct nvgrace_egm_dev *egm_dev =
		egm_chardev_to_nvgrace_egm_dev(egm_chardev);
	void *memaddr;

	if (atomic_inc_return(&egm_chardev->open_count) > 1)
		return 0;

	/*
	 * nvgrace-egm module is responsible to manage the EGM memory as
	 * the host kernel has no knowledge of it. Clear the region before
	 * handing over to userspace.
	 */
	memaddr = memremap(egm_dev->egmphys, egm_dev->egmlength, MEMREMAP_WB);
	if (!memaddr) {
		atomic_dec(&egm_chardev->open_count);
		return -EINVAL;
	}

	memset((u8 *)memaddr, 0, egm_dev->egmlength);
	memunmap(memaddr);

	file->private_data = egm_chardev;

	return 0;
}

static int nvgrace_egm_release(struct inode *inode, struct file *file)
{
	struct chardev *egm_chardev =
		container_of(inode->i_cdev, struct chardev, cdev);

	if (atomic_dec_and_test(&egm_chardev->open_count))
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

static long nvgrace_egm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long minsz = offsetofend(struct egm_bad_pages_list, count);
	struct egm_bad_pages_list info;
	void __user *uarg = (void __user *)arg;
	struct chardev *egm_chardev = file->private_data;

	if (copy_from_user(&info, uarg, minsz))
		return -EFAULT;

	if (info.argsz < minsz || !egm_chardev)
		return -EINVAL;

	switch (cmd) {
	case EGM_BAD_PAGES_LIST:
		int ret;
		unsigned long bad_page_struct_size = sizeof(struct egm_bad_pages_info);
		struct egm_bad_pages_info tmp;
		struct h_node *cur_page;
		struct hlist_node *tmp_node;
		unsigned long bkt;
		int count = 0, index = 0;

		hash_for_each_safe(egm_chardev->htbl, bkt, tmp_node, cur_page, node)
			count++;

		if (info.argsz < (minsz + count * bad_page_struct_size)) {
			info.argsz = minsz + count * bad_page_struct_size;
			info.count = 0;
			goto done;
		} else {
			hash_for_each_safe(egm_chardev->htbl, bkt, tmp_node, cur_page, node) {
				/*
				 * This check fails if there was an ECC error
				 * after the usermode app read the count of
				 * bad pages through this ioctl.
				 */
				if (minsz + index * bad_page_struct_size >= info.argsz) {
					info.argsz = minsz + index * bad_page_struct_size;
					info.count = index;
					goto done;
				}

				tmp.offset = cur_page->mem_offset;
				tmp.size = PAGE_SIZE;

				ret = copy_to_user(uarg + minsz +
						   index * bad_page_struct_size,
						   &tmp, bad_page_struct_size);
				if (ret)
					return -EFAULT;
				index++;
			}

			info.count = index;
		}
		break;
	default:
		return -EINVAL;
	}

done:
	return copy_to_user(uarg, &info, minsz) ? -EFAULT : 0;
}

static const struct file_operations file_ops = {
	.owner = THIS_MODULE,
	.open = nvgrace_egm_open,
	.release = nvgrace_egm_release,
	.mmap = nvgrace_egm_mmap,
	.unlocked_ioctl = nvgrace_egm_ioctl,
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
	atomic_set(&egm_chardev->open_count, 0);

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

static void cleanup_retired_pages(struct chardev *egm_chardev)
{
	struct h_node *cur_page;
	unsigned long bkt;
	struct hlist_node *temp_node;

	hash_for_each_safe(egm_chardev->htbl, bkt, temp_node, cur_page, node) {
		hash_del(&cur_page->node);
		kvfree(cur_page);
	}
}

static int nvgrace_egm_fetch_retired_pages(struct nvgrace_egm_dev *egm_dev,
					   struct chardev *egm_chardev)
{
	u64 count;
	void *memaddr;
	int index, ret = 0;

	memaddr = memremap(egm_dev->retiredpagesphys, PAGE_SIZE, MEMREMAP_WB);
	if (!memaddr)
		return -ENOMEM;

	count = *(u64 *)memaddr;

	for (index = 0; index < count; index++) {
		struct h_node *retired_page;

		/*
		 * Since the EGM is linearly mapped, the offset in the
		 * carveout is the same offset in the VM system memory.
		 *
		 * Calculate the offset to communicate to the usermode
		 * apps.
		 */
		retired_page = kvzalloc(sizeof(*retired_page), GFP_KERNEL);
		if (!retired_page) {
			ret = -ENOMEM;
			break;
		}

		retired_page->mem_offset = *((u64 *)memaddr + index + 1) -
					   egm_dev->egmphys;
		hash_add(egm_chardev->htbl, &retired_page->node,
			 retired_page->mem_offset);
	}

	memunmap(memaddr);

	if (ret)
		cleanup_retired_pages(egm_chardev);

	return ret;
}

static int egm_driver_probe(struct auxiliary_device *aux_dev,
			    const struct auxiliary_device_id *id)
{
	struct nvgrace_egm_dev *egm_dev =
		container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);
	struct chardev *egm_chardev;
	int ret;

	egm_chardev = setup_egm_chardev(egm_dev);
	if (!egm_chardev)
		return -EINVAL;

	hash_init(egm_chardev->htbl);

	ret = nvgrace_egm_fetch_retired_pages(egm_dev, egm_chardev);
	if (ret)
		goto error_exit;

	xa_store(&egm_chardevs, egm_dev->egmpxm, egm_chardev, GFP_KERNEL);

	return 0;

error_exit:
	del_egm_chardev(egm_chardev);
	return ret;
}

static void egm_driver_remove(struct auxiliary_device *aux_dev)
{
	struct nvgrace_egm_dev *egm_dev =
		container_of(aux_dev, struct nvgrace_egm_dev, aux_dev);
	struct chardev *egm_chardev = xa_erase(&egm_chardevs, egm_dev->egmpxm);
	struct h_node *cur_page;
	unsigned long bkt;
	struct hlist_node *temp_node;

	if (!egm_chardev)
		return;

	hash_for_each_safe(egm_chardev->htbl, bkt, temp_node, cur_page, node) {
		hash_del(&cur_page->node);
		kvfree(cur_page);
	}

	cleanup_retired_pages(egm_chardev);
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

static ssize_t egm_size_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct chardev *egm_chardev = container_of(dev, struct chardev, device);
	struct nvgrace_egm_dev *egm_dev =
		egm_chardev_to_nvgrace_egm_dev(egm_chardev);

	return sysfs_emit(buf, "0x%lx\n", egm_dev->egmlength);
}

static DEVICE_ATTR_RO(egm_size);

static struct attribute *attrs[] = {
	&dev_attr_egm_size.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static const struct attribute_group *attr_groups[2] = {
	&attr_group,
	NULL
};

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
	class->dev_groups = attr_groups;

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
