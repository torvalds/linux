// SPDX-License-Identifier: (GPL-2.0+ OR MIT)

/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd */

#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/uaccess.h>
#include <misc/rkflash_vendor_storage.h>

#include "flash_vendor_storage.h"

#define FLASH_VENDOR_TEST	0
#define DRM_DEBUG		1

#if DRM_DEBUG
#define DLOG(fmt, args...)	pr_info(fmt, ##args)
#else
#define DLOG(x...)
#endif

struct vendor_item {
	u16  id;
	u16  offset;
	u16  size;
	u16  flag;
};

#define FLASH_VENDOR_PART_START		8
#define FLASH_VENDOR_PART_SIZE		8
#define FLASH_VENDOR_PART_NUM		4
#define FLASH_VENDOR_TAG		0x524B5644

struct tag_vendor_info {
	u32	tag;
	u32	version;
	u16	next_index;
	u16	item_num;
	u16	free_offset;
	u16	free_size;
	struct vendor_item item[62]; /* 62 * 8 */
	u8	data[FLASH_VENDOR_PART_SIZE * 512 - 512 - 8];
	u32	hash;
	u32	version2;
};

static int (*_flash_read)(u32 sec, u32 n_sec, void *p_data);
static int (*_flash_write)(u32 sec, u32 n_sec, void *p_data);
static struct tag_vendor_info *g_vendor;

int flash_vendor_dev_ops_register(int (*read)(u32 sec,
					      u32 n_sec,
					      void *p_data),
				  int (*write)(u32 sec,
					       u32 n_sec,
					       void *p_data))
{
	if (!_flash_read) {
		_flash_read = read;
		_flash_write = write;
		return 0;
	}
	return -1;
}

static u32 flash_vendor_init(void)
{
	u32 i, max_ver, max_index;

	if (!_flash_read)
		return -EPERM;

	g_vendor = kmalloc(sizeof(*g_vendor), GFP_KERNEL | GFP_DMA);
	if (!g_vendor)
		return 0;

	max_ver = 0;
	max_index = 0;
	for (i = 0; i < FLASH_VENDOR_PART_NUM; i++) {
		_flash_read(FLASH_VENDOR_PART_START +
				FLASH_VENDOR_PART_SIZE * i,
				FLASH_VENDOR_PART_SIZE,
				g_vendor);
		if (g_vendor->tag == FLASH_VENDOR_TAG &&
		    g_vendor->version == g_vendor->version2) {
			if (max_ver < g_vendor->version) {
				max_index = i;
				max_ver = g_vendor->version;
			}
		}
	}
	/* DLOG("max_ver = %d\n",max_ver); */
	if (max_ver) {
		_flash_read(FLASH_VENDOR_PART_START +
				FLASH_VENDOR_PART_SIZE * max_index,
				FLASH_VENDOR_PART_SIZE,
		g_vendor);
	} else {
		memset(g_vendor, 0, sizeof(*g_vendor));
		g_vendor->version = 1;
		g_vendor->tag = FLASH_VENDOR_TAG;
		g_vendor->version2 = g_vendor->version;
		g_vendor->free_offset = 0;
		g_vendor->free_size = sizeof(g_vendor->data);
	}
	/* rknand_print_hex("vendor:", g_vendor, 4, 1024); */

	return 0;
}

static int flash_vendor_read(u32 id, void *pbuf, u32 size)
{
	u32 i;

	if (!g_vendor)
		return -1;

	for (i = 0; i < g_vendor->item_num; i++) {
		if (g_vendor->item[i].id == id) {
			if (size > g_vendor->item[i].size)
				size = g_vendor->item[i].size;
			memcpy(pbuf,
			       &g_vendor->data[g_vendor->item[i].offset],
			       size);
			return size;
		}
	}
	return (-1);
}

static int flash_vendor_write(u32 id, void *pbuf, u32 size)
{
	u32 i, next_index, algin_size;
	struct vendor_item *item;

	algin_size = (size + 0x3F) & (~0x3F); /* algin to 64 bytes */
	next_index = g_vendor->next_index;
	for (i = 0; i < g_vendor->item_num; i++) {
		if (g_vendor->item[i].id == id) {
			if (size > algin_size)
				return -1;
			memcpy(&g_vendor->data[g_vendor->item[i].offset],
			       pbuf,
			       size);
			g_vendor->item[i].size = size;
			g_vendor->version++;
			g_vendor->version2 = g_vendor->version;
			g_vendor->next_index++;
			if (g_vendor->next_index >= FLASH_VENDOR_PART_NUM)
				g_vendor->next_index = 0;
			_flash_write(FLASH_VENDOR_PART_START +
					FLASH_VENDOR_PART_SIZE * next_index,
					FLASH_VENDOR_PART_SIZE,
					g_vendor);
			return 0;
		}
	}

	if (g_vendor->free_size >= algin_size) {
		item = &g_vendor->item[g_vendor->item_num];
		item->id = id;
		item->offset = g_vendor->free_offset;
		item->size = algin_size;
		item->size = size;
		g_vendor->free_offset += algin_size;
		g_vendor->free_size -= algin_size;
		memcpy(&g_vendor->data[item->offset], pbuf, size);
		g_vendor->item_num++;
		g_vendor->version++;
		g_vendor->next_index++;
		g_vendor->version2 = g_vendor->version;
		if (g_vendor->next_index >= FLASH_VENDOR_PART_NUM)
			g_vendor->next_index = 0;
		_flash_write(FLASH_VENDOR_PART_START +
				FLASH_VENDOR_PART_SIZE * next_index,
				FLASH_VENDOR_PART_SIZE,
			g_vendor);
		return 0;
	}

	return(-1);
}

#if (FLASH_VENDOR_TEST)
static void print_hex(char *s, void *buf, int width, int len)
{
	print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET,
		       16, width, buf, len * width, 0);
}

static void flash_vendor_test(void)
{
	u32 i;
	u8 test_buf[512];

	memset(test_buf, 0, 512);
	for (i = 0; i < 62; i++) {
		memset(test_buf, i, i + 1);
		flash_vendor_write(i, test_buf, i + 1);
	}
	memset(test_buf, 0, 512);
	for (i = 0; i < 62; i++) {
		flash_vendor_read(i, test_buf, i + 1);
		DLOG("id = %d ,size = %d\n", i, i + 1);
		print_hex("data:", test_buf, 1, i + 1);
	}
	flash_vendor_init();
	memset(test_buf, 0, 512);
	for (i = 0; i < 62; i++) {
		flash_vendor_read(i, test_buf, i + 1);
		DLOG("id = %d ,size = %d\n", i, i + 1);
		print_hex("data:", test_buf, 1, i + 1);
	}
	while (1)
		;
}
#endif

static long vendor_storage_ioctl(struct file *file,
				 unsigned int cmd,
				 unsigned long arg)
{
	long ret = -EINVAL;
	int size;
	u32 *temp_buf;
	struct RK_VENDOR_REQ *req;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return ret;

	temp_buf = (u32 *)req;

	switch (cmd) {
	case VENDOR_READ_IO:
	{
		if (copy_from_user(temp_buf,
				   (void __user *)arg,
				   sizeof(*req))) {
			DLOG("copy_from_user error\n");
			ret = -EFAULT;
			break;
		}
		if (req->tag == VENDOR_REQ_TAG) {
			size = flash_vendor_read(req->id,
						 req->data,
						 req->len);
			if (size > 0) {
				req->len = size;
				ret = 0;
				if (copy_to_user((void __user *)arg,
						 temp_buf,
						 sizeof(*req)))
					ret = -EFAULT;
			}
		}
	} break;
	case VENDOR_WRITE_IO:
	{
		if (copy_from_user(temp_buf,
				   (void __user *)arg,
				   sizeof(struct RK_VENDOR_REQ))) {
			DLOG("copy_from_user error\n");
			ret = -EFAULT;
			break;
		}
		if (req->tag == VENDOR_REQ_TAG)
			ret = flash_vendor_write(req->id,
						 req->data,
						 req->len);
	} break;
	default:
		return -EINVAL;
	}
	kfree(temp_buf);
	DLOG("flash_vendor_ioctl cmd=%x ret = %lx\n", cmd, ret);
	return ret;
}

static const struct file_operations vendor_storage_fops = {
	.compat_ioctl	= vendor_storage_ioctl,
	.unlocked_ioctl = vendor_storage_ioctl,
};

static struct miscdevice vender_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vendor_storage",
	.fops  = &vendor_storage_fops,
};

static int vendor_init_thread(void *arg)
{
	int ret;

	pr_info("flash %s!\n", __func__);
	ret = flash_vendor_init();
	if (!ret) {
		ret = misc_register(&vender_storage_dev);
		#ifdef CONFIG_ROCKCHIP_VENDOR_STORAGE
		rk_vendor_register(flash_vendor_read, flash_vendor_write);
		#endif
	}
	pr_info("flash vendor storage:20170308 ret = %d\n", ret);
	return ret;
}

static int __init vendor_storage_init(void)
{
	kthread_run(vendor_init_thread, (void *)NULL, "vendor_storage_init");
	return 0;
}

static __exit void vendor_storage_deinit(void)
{
	if (g_vendor)
		misc_deregister(&vender_storage_dev);
}

device_initcall_sync(vendor_storage_init);
module_exit(vendor_storage_deinit);
