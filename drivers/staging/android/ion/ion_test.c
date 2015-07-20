/*
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "ion-test: " fmt

#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "ion.h"
#include "../uapi/ion_test.h"

#define u64_to_uptr(x) ((void __user *)(unsigned long)(x))

struct ion_test_device {
	struct miscdevice misc;
};

struct ion_test_data {
	struct dma_buf *dma_buf;
	struct device *dev;
};

static int ion_handle_test_dma(struct device *dev, struct dma_buf *dma_buf,
		void __user *ptr, size_t offset, size_t size, bool write)
{
	int ret = 0;
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	pgprot_t pgprot = pgprot_writecombine(PAGE_KERNEL);
	enum dma_data_direction dir = write ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct sg_page_iter sg_iter;
	unsigned long offset_page;

	attach = dma_buf_attach(dma_buf, dev);
	if (IS_ERR(attach))
		return PTR_ERR(attach);

	table = dma_buf_map_attachment(attach, dir);
	if (IS_ERR(table))
		return PTR_ERR(table);

	offset_page = offset >> PAGE_SHIFT;
	offset %= PAGE_SIZE;

	for_each_sg_page(table->sgl, &sg_iter, table->nents, offset_page) {
		struct page *page = sg_page_iter_page(&sg_iter);
		void *vaddr = vmap(&page, 1, VM_MAP, pgprot);
		size_t to_copy = PAGE_SIZE - offset;

		to_copy = min(to_copy, size);
		if (!vaddr) {
			ret = -ENOMEM;
			goto err;
		}

		if (write)
			ret = copy_from_user(vaddr + offset, ptr, to_copy);
		else
			ret = copy_to_user(ptr, vaddr + offset, to_copy);

		vunmap(vaddr);
		if (ret) {
			ret = -EFAULT;
			goto err;
		}
		size -= to_copy;
		if (!size)
			break;
		ptr += to_copy;
		offset = 0;
	}

err:
	dma_buf_unmap_attachment(attach, table, dir);
	dma_buf_detach(dma_buf, attach);
	return ret;
}

static int ion_handle_test_kernel(struct dma_buf *dma_buf, void __user *ptr,
		size_t offset, size_t size, bool write)
{
	int ret;
	unsigned long page_offset = offset >> PAGE_SHIFT;
	size_t copy_offset = offset % PAGE_SIZE;
	size_t copy_size = size;
	enum dma_data_direction dir = write ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	if (offset > dma_buf->size || size > dma_buf->size - offset)
		return -EINVAL;

	ret = dma_buf_begin_cpu_access(dma_buf, offset, size, dir);
	if (ret)
		return ret;

	while (copy_size > 0) {
		size_t to_copy;
		void *vaddr = dma_buf_kmap(dma_buf, page_offset);

		if (!vaddr)
			goto err;

		to_copy = min_t(size_t, PAGE_SIZE - copy_offset, copy_size);

		if (write)
			ret = copy_from_user(vaddr + copy_offset, ptr, to_copy);
		else
			ret = copy_to_user(ptr, vaddr + copy_offset, to_copy);

		dma_buf_kunmap(dma_buf, page_offset, vaddr);
		if (ret) {
			ret = -EFAULT;
			goto err;
		}

		copy_size -= to_copy;
		ptr += to_copy;
		page_offset++;
		copy_offset = 0;
	}
err:
	dma_buf_end_cpu_access(dma_buf, offset, size, dir);
	return ret;
}

static long ion_test_ioctl(struct file *filp, unsigned int cmd,
						unsigned long arg)
{
	struct ion_test_data *test_data = filp->private_data;
	int ret = 0;

	union {
		struct ion_test_rw_data test_rw;
	} data;

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (_IOC_DIR(cmd) & _IOC_WRITE)
		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

	switch (cmd) {
	case ION_IOC_TEST_SET_FD:
	{
		struct dma_buf *dma_buf = NULL;
		int fd = arg;

		if (fd >= 0) {
			dma_buf = dma_buf_get((int)arg);
			if (IS_ERR(dma_buf))
				return PTR_ERR(dma_buf);
		}
		if (test_data->dma_buf)
			dma_buf_put(test_data->dma_buf);
		test_data->dma_buf = dma_buf;
		break;
	}
	case ION_IOC_TEST_DMA_MAPPING:
	{
		ret = ion_handle_test_dma(test_data->dev, test_data->dma_buf,
					u64_to_uptr(data.test_rw.ptr),
					data.test_rw.offset, data.test_rw.size,
					data.test_rw.write);
		break;
	}
	case ION_IOC_TEST_KERNEL_MAPPING:
	{
		ret = ion_handle_test_kernel(test_data->dma_buf,
					u64_to_uptr(data.test_rw.ptr),
					data.test_rw.offset, data.test_rw.size,
					data.test_rw.write);
		break;
	}
	default:
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, sizeof(data)))
			return -EFAULT;
	}
	return ret;
}

static int ion_test_open(struct inode *inode, struct file *file)
{
	struct ion_test_data *data;
	struct miscdevice *miscdev = file->private_data;

	data = kzalloc(sizeof(struct ion_test_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = miscdev->parent;

	file->private_data = data;

	return 0;
}

static int ion_test_release(struct inode *inode, struct file *file)
{
	struct ion_test_data *data = file->private_data;

	kfree(data);

	return 0;
}

static const struct file_operations ion_test_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ion_test_ioctl,
	.compat_ioctl = ion_test_ioctl,
	.open = ion_test_open,
	.release = ion_test_release,
};

static int __init ion_test_probe(struct platform_device *pdev)
{
	int ret;
	struct ion_test_device *testdev;

	testdev = devm_kzalloc(&pdev->dev, sizeof(struct ion_test_device),
				GFP_KERNEL);
	if (!testdev)
		return -ENOMEM;

	testdev->misc.minor = MISC_DYNAMIC_MINOR;
	testdev->misc.name = "ion-test";
	testdev->misc.fops = &ion_test_fops;
	testdev->misc.parent = &pdev->dev;
	ret = misc_register(&testdev->misc);
	if (ret) {
		pr_err("failed to register misc device.\n");
		return ret;
	}

	platform_set_drvdata(pdev, testdev);

	return 0;
}

static int ion_test_remove(struct platform_device *pdev)
{
	struct ion_test_device *testdev;

	testdev = platform_get_drvdata(pdev);
	if (!testdev)
		return -ENODATA;

	return misc_deregister(&testdev->misc);
}

static struct platform_device *ion_test_pdev;
static struct platform_driver ion_test_platform_driver = {
	.remove = ion_test_remove,
	.driver = {
		.name = "ion-test",
	},
};

static int __init ion_test_init(void)
{
	ion_test_pdev = platform_device_register_simple("ion-test",
							-1, NULL, 0);
	if (!ion_test_pdev)
		return -ENODEV;

	return platform_driver_probe(&ion_test_platform_driver, ion_test_probe);
}

static void __exit ion_test_exit(void)
{
	platform_driver_unregister(&ion_test_platform_driver);
	platform_device_unregister(ion_test_pdev);
}

module_init(ion_test_init);
module_exit(ion_test_exit);
MODULE_LICENSE("GPL v2");
