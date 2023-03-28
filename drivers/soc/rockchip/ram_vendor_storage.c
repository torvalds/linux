// SPDX-License-Identifier: (GPL-2.0+ OR MIT)

/* Copyright (c) 2023 Rockchip Electronics Co., Ltd */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/mempolicy.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/uaccess.h>
#include <misc/rkflash_vendor_storage.h>

static struct vendor_info *g_vendor;

static int ram_vendor_read(u32 id, void *pbuf, u32 size)
{
	u32 i;

	if (!g_vendor)
		return -ENOMEM;

	for (i = 0; i < g_vendor->item_num; i++) {
		if (g_vendor->item[i].id == id) {
			if (size > g_vendor->item[i].size)
				size = g_vendor->item[i].size;
			memcpy(pbuf, &g_vendor->data[g_vendor->item[i].offset], size);
			return size;
		}
	}

	return (-1);
}

static int ram_vendor_storage_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ram_vendor_storage_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long ram_vendor_storage_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	long ret = -1;
	int size;
	struct RK_VENDOR_REQ *v_req;
	u32 *page_buf;

	page_buf = kmalloc(4096, GFP_KERNEL);
	if (!page_buf)
		return -ENOMEM;

	v_req = (struct RK_VENDOR_REQ *)page_buf;

	switch (cmd) {
	case VENDOR_READ_IO:
	{
		if (copy_from_user(page_buf, (void __user *)arg, 8)) {
			ret = -EFAULT;
			break;
		}
		if (v_req->tag == VENDOR_REQ_TAG && v_req->len <= 4096 - 8) {
			size = ram_vendor_read(v_req->id, v_req->data, v_req->len);
			if (size != -1) {
				v_req->len = size;
				ret = 0;
				if (copy_to_user((void __user *)arg,
						 page_buf,
						 v_req->len + 8))
					ret = -EFAULT;
			}
		}
	} break;

	case VENDOR_WRITE_IO:
	default:
		ret = -EINVAL;
		goto exit;
	}
exit:
	kfree(page_buf);
	return ret;
}

static const struct file_operations vendor_storage_fops = {
	.open = ram_vendor_storage_open,
	.compat_ioctl = ram_vendor_storage_ioctl,
	.unlocked_ioctl = ram_vendor_storage_ioctl,
	.release = ram_vendor_storage_release,
};

static struct miscdevice vender_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vendor_storage",
	.fops  = &vendor_storage_fops,
};

static void *ram_vendor_stroage_map(phys_addr_t start, size_t len)
{
	int i;
	void *vaddr;
	pgprot_t pgprot = PAGE_KERNEL;
	phys_addr_t phys;
	int npages = PAGE_ALIGN(len) / PAGE_SIZE;
	struct page **p = vmalloc(sizeof(struct page *) * npages);

	if (!p)
		return NULL;

	phys = start;
	for (i = 0; i < npages; i++) {
		p[i] = phys_to_page(phys);
		phys += PAGE_SIZE;
	}

	vaddr = vmap(p, npages, VM_MAP, pgprot);
	vfree(p);

	return vaddr;
}

static int ram_vendor_storage_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *node;
	struct resource res;
	int ret;
	phys_addr_t size, start;

	if (g_vendor)
		return -EINVAL;

	node = of_parse_phandle(np, "memory-region", 0);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;

	ret = -EINVAL;

	size = resource_size(&res);
	start = res.start;
	if (size != VENDOR_PART_SIZE << 9 || (start & (PAGE_SIZE - 1)))
		goto un_reserved;

	g_vendor = ram_vendor_stroage_map(start, size);
	if (IS_ERR(g_vendor))
		goto un_reserved;

	if (g_vendor->tag != VENDOR_HEAD_TAG)
		goto un_remap;

	misc_register(&vender_storage_dev);
	rk_vendor_register(ram_vendor_read, NULL);

	return 0;

un_remap:
	vunmap(g_vendor);
un_reserved:
#ifndef MODULE
	free_reserved_area(phys_to_virt(start), phys_to_virt(start) + size, -1, "memory-region");
#endif
	g_vendor = NULL;

	return ret;
}

static int ram_vendor_storage_remove(struct platform_device *pdev)
{
	if (g_vendor) {
		misc_deregister(&vender_storage_dev);
		vunmap(g_vendor);
		g_vendor = NULL;
	}

	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "rockchip,ram-vendor-storage" },
	{}
};

static struct platform_driver vendor_storage_driver = {
	.probe		= ram_vendor_storage_probe,
	.remove		= ram_vendor_storage_remove,
	.driver		= {
		.name		= "vendor-storage",
		.of_match_table	= dt_match,
	},
};

module_platform_driver(vendor_storage_driver);
MODULE_LICENSE("GPL");
