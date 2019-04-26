/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define EMMC_IDB_PART_OFFSET		64
#define EMMC_SYS_PART_OFFSET		8064
#define EMMC_BOOT_PART_SIZE		1024
#define EMMC_VENDOR_PART_START		(1024 * 7)
#define EMMC_VENDOR_PART_SIZE		128
#define EMMC_VENDOR_PART_NUM		4
#define EMMC_VENDOR_TAG			0x524B5644

struct rk_vendor_req {
	u32 tag;
	u16 id;
	u16 len;
	u8 data[1024];
};

struct vendor_item {
	u16  id;
	u16  offset;
	u16  size;
	u16  flag;
};

struct vendor_info {
	u32	tag;
	u32	version;
	u16	next_index;
	u16	item_num;
	u16	free_offset;
	u16	free_size;
	struct	vendor_item item[126]; /* 126 * 8*/
	u8	data[EMMC_VENDOR_PART_SIZE * 512 - 1024 - 8];
	u32	hash;
	u32	version2;
};

#ifdef CONFIG_ROCKCHIP_VENDOR_STORAGE_UPDATE_LOADER
#define READ_SECTOR_IO		_IOW('r', 0x04, unsigned int)
#define WRITE_SECTOR_IO		_IOW('r', 0x05, unsigned int)
#define END_WRITE_SECTOR_IO	_IOW('r', 0x52, unsigned int)
#define GET_FLASH_INFO_IO	_IOW('r', 0x1A, unsigned int)
#define GET_BAD_BLOCK_IO	_IOW('r', 0x03, unsigned int)
#define GET_LOCK_FLAG_IO	_IOW('r', 0x53, unsigned int)
#endif

#define VENDOR_REQ_TAG		0x56524551
#define VENDOR_READ_IO		_IOW('v', 0x01, unsigned int)
#define VENDOR_WRITE_IO		_IOW('v', 0x02, unsigned int)

static u8 *g_idb_buffer;
static struct vendor_info *g_vendor;
static DEFINE_MUTEX(vendor_ops_mutex);
extern int rk_emmc_transfer(u8 *buffer, unsigned addr, unsigned blksz,
			    int write);

static int emmc_vendor_ops(u8 *buffer, u32 addr, u32 n_sec, int write)
{
	u32 i, ret = 0;

	for (i = 0; i < n_sec; i++)
		ret = rk_emmc_transfer(buffer + i * 512, addr + i, 512, write);

	return ret;
}

static int emmc_vendor_storage_init(void)
{
	u32 i, max_ver, max_index;
	u8 *p_buf;

	g_vendor = kmalloc(sizeof(*g_vendor), GFP_KERNEL | GFP_DMA);
	if (!g_vendor)
		return -ENOMEM;

	max_ver = 0;
	max_index = 0;
	for (i = 0; i < EMMC_VENDOR_PART_NUM; i++) {
		/* read first 512 bytes */
		p_buf = (u8 *)g_vendor;
		if (rk_emmc_transfer(p_buf, EMMC_VENDOR_PART_START +
				 EMMC_VENDOR_PART_SIZE * i, 512, 0))
			goto error_exit;
		/* read last 512 bytes */
		p_buf += (EMMC_VENDOR_PART_SIZE - 1) * 512;
		if (rk_emmc_transfer(p_buf, EMMC_VENDOR_PART_START +
				 EMMC_VENDOR_PART_SIZE * (i + 1) - 1,
				 512, 0))
			goto error_exit;

		if (g_vendor->tag == EMMC_VENDOR_TAG &&
		    g_vendor->version2 == g_vendor->version) {
			if (max_ver < g_vendor->version) {
				max_index = i;
				max_ver = g_vendor->version;
			}
		}
	}
	if (max_ver) {
		if (emmc_vendor_ops((u8 *)g_vendor, EMMC_VENDOR_PART_START +
				EMMC_VENDOR_PART_SIZE * max_index,
				EMMC_VENDOR_PART_SIZE, 0))
			goto error_exit;
	} else {
		memset((void *)g_vendor, 0, sizeof(*g_vendor));
		g_vendor->version = 1;
		g_vendor->tag = EMMC_VENDOR_TAG;
		g_vendor->version2 = g_vendor->version;
		g_vendor->free_offset = 0;
		g_vendor->free_size = sizeof(g_vendor->data);
	}
	return 0;
error_exit:
	kfree(g_vendor);
	g_vendor = NULL;
	return -1;
}

static int emmc_vendor_read(u32 id, void *pbuf, u32 size)
{
	u32 i;

	if (!g_vendor)
		return -ENOMEM;

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

static int emmc_vendor_write(u32 id, void *pbuf, u32 size)
{
	u32 i, j, next_index, align_size, alloc_size, item_num;
	u32 offset, next_size;
	u8 *p_data;
	struct vendor_item *item;
	struct vendor_item *next_item;

	if (!g_vendor)
		return -ENOMEM;

	p_data = g_vendor->data;
	item_num = g_vendor->item_num;
	align_size = ALIGN(size, 0x40); /* align to 64 bytes*/
	next_index = g_vendor->next_index;
	for (i = 0; i < item_num; i++) {
		item = &g_vendor->item[i];
		if (item->id == id) {
			alloc_size = ALIGN(item->size, 0x40);
			if (size > alloc_size) {
				if (g_vendor->free_size < align_size)
					return -1;
				offset = item->offset;
				for (j = i; j < item_num - 1; j++) {
					item = &g_vendor->item[j];
					next_item = &g_vendor->item[j + 1];
					item->id = next_item->id;
					item->size = next_item->size;
					item->offset = offset;
					next_size = ALIGN(next_item->size,
							  0x40);
					memcpy(&p_data[offset],
					       &p_data[next_item->offset],
					       next_size);
					offset += next_size;
				}
				item = &g_vendor->item[j];
				item->id = id;
				item->offset = offset;
				item->size = size;
				memcpy(&p_data[item->offset], pbuf, size);
				g_vendor->free_offset = offset + align_size;
				g_vendor->free_size -= (align_size -
							alloc_size);
			} else {
				memcpy(&p_data[item->offset],
				       pbuf,
				       size);
				g_vendor->item[i].size = size;
			}
			g_vendor->version++;
			g_vendor->version2 = g_vendor->version;
			g_vendor->next_index++;
			if (g_vendor->next_index >= EMMC_VENDOR_PART_NUM)
				g_vendor->next_index = 0;
			emmc_vendor_ops((u8 *)g_vendor, EMMC_VENDOR_PART_START +
					EMMC_VENDOR_PART_SIZE * next_index,
					EMMC_VENDOR_PART_SIZE, 1);
			return 0;
		}
	}

	if (g_vendor->free_size >= align_size) {
		item = &g_vendor->item[g_vendor->item_num];
		item->id = id;
		item->offset = g_vendor->free_offset;
		item->size = size;
		g_vendor->free_offset += align_size;
		g_vendor->free_size -= align_size;
		memcpy(&g_vendor->data[item->offset], pbuf, size);
		g_vendor->item_num++;
		g_vendor->version++;
		g_vendor->version2 = g_vendor->version;
		g_vendor->next_index++;
		if (g_vendor->next_index >= EMMC_VENDOR_PART_NUM)
			g_vendor->next_index = 0;
		emmc_vendor_ops((u8 *)g_vendor, EMMC_VENDOR_PART_START +
				EMMC_VENDOR_PART_SIZE * next_index,
				EMMC_VENDOR_PART_SIZE, 1);
		return 0;
	}
	return(-1);
}

#ifdef CONFIG_ROCKCHIP_VENDOR_STORAGE_UPDATE_LOADER
static int id_blk_read_data(u32 index, u32 n_sec, u8 *buf)
{
	u32 i;
	u32 ret = 0;

	if (index + n_sec >= 1024 * 5)
		return 0;
	index = index + EMMC_IDB_PART_OFFSET;
	for (i = 0; i < n_sec; i++) {
		ret = rk_emmc_transfer(buf + i * 512, index + i, 512, 0);
		if (ret)
			return ret;
	}
	return ret;
}

static int id_blk_write_data(u32 index, u32 n_sec, u8 *buf)
{
	u32 i;
	u32 ret = 0;

	if (index + n_sec >= 1024 * 5)
		return 0;
	index = index + EMMC_IDB_PART_OFFSET;
	for (i = 0; i < n_sec; i++) {
		ret = rk_emmc_transfer(buf + i * 512, index + i, 512, 1);
		if (ret)
			return ret;
	}
	return ret;
}

static int emmc_write_idblock(u32 size, u8 *buf, u32 *id_blk_tbl)
{
	u32 i, totle_sec, j;
	u32 totle_write_count = 0;
	u32 *p_raw_data = (u32 *)buf;
	u32 *p_check_buf = kmalloc(EMMC_BOOT_PART_SIZE * 512, GFP_KERNEL);

	if (!p_check_buf)
		return -ENOMEM;

	totle_sec = (size + 511) >> 9;
	if (totle_sec <= 8)
		totle_sec = 8;

	for (i = 0; i < 5; i++) {
		memset(p_check_buf, 0, 512);
		id_blk_write_data(EMMC_BOOT_PART_SIZE * i, 1,
				  (u8 *)p_check_buf);
		id_blk_write_data(EMMC_BOOT_PART_SIZE * i + 1,
				  totle_sec - 1, buf + 512);
		id_blk_write_data(EMMC_BOOT_PART_SIZE * i, 1, buf);
		id_blk_read_data(EMMC_BOOT_PART_SIZE * i, totle_sec,
				 (u8 *)p_check_buf);
		for (j = 0; j < totle_sec * 128; j++) {
			if (p_check_buf[j] != p_raw_data[j]) {
				memset(p_check_buf, 0, 512);
				id_blk_write_data(EMMC_BOOT_PART_SIZE * i, 1,
						  (u8 *)p_check_buf);
				break;
			}
		}
		if (j >= totle_sec * 128)
			totle_write_count++;
	}
	kfree(p_check_buf);
	if (totle_write_count)
		return 0;
	return (-1);
}
#endif

static int vendor_storage_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int vendor_storage_release(struct inode *inode, struct file *file)
{
	return 0;
}

#ifdef CONFIG_ROCKCHIP_VENDOR_STORAGE_UPDATE_LOADER
static const u32 g_crc32_tbl[256] = {
	0x00000000, 0x04c10db7, 0x09821b6e, 0x0d4316d9,
	0x130436dc, 0x17c53b6b, 0x1a862db2, 0x1e472005,
	0x26086db8, 0x22c9600f, 0x2f8a76d6, 0x2b4b7b61,
	0x350c5b64, 0x31cd56d3, 0x3c8e400a, 0x384f4dbd,
	0x4c10db70, 0x48d1d6c7, 0x4592c01e, 0x4153cda9,
	0x5f14edac, 0x5bd5e01b, 0x5696f6c2, 0x5257fb75,
	0x6a18b6c8, 0x6ed9bb7f, 0x639aada6, 0x675ba011,
	0x791c8014, 0x7ddd8da3, 0x709e9b7a, 0x745f96cd,
	0x9821b6e0, 0x9ce0bb57, 0x91a3ad8e, 0x9562a039,
	0x8b25803c, 0x8fe48d8b, 0x82a79b52, 0x866696e5,
	0xbe29db58, 0xbae8d6ef, 0xb7abc036, 0xb36acd81,
	0xad2ded84, 0xa9ece033, 0xa4aff6ea, 0xa06efb5d,
	0xd4316d90, 0xd0f06027, 0xddb376fe, 0xd9727b49,
	0xc7355b4c, 0xc3f456fb, 0xceb74022, 0xca764d95,
	0xf2390028, 0xf6f80d9f, 0xfbbb1b46, 0xff7a16f1,
	0xe13d36f4, 0xe5fc3b43, 0xe8bf2d9a, 0xec7e202d,
	0x34826077, 0x30436dc0, 0x3d007b19, 0x39c176ae,
	0x278656ab, 0x23475b1c, 0x2e044dc5, 0x2ac54072,
	0x128a0dcf, 0x164b0078, 0x1b0816a1, 0x1fc91b16,
	0x018e3b13, 0x054f36a4, 0x080c207d, 0x0ccd2dca,
	0x7892bb07, 0x7c53b6b0, 0x7110a069, 0x75d1adde,
	0x6b968ddb, 0x6f57806c, 0x621496b5, 0x66d59b02,
	0x5e9ad6bf, 0x5a5bdb08, 0x5718cdd1, 0x53d9c066,
	0x4d9ee063, 0x495fedd4, 0x441cfb0d, 0x40ddf6ba,
	0xaca3d697, 0xa862db20, 0xa521cdf9, 0xa1e0c04e,
	0xbfa7e04b, 0xbb66edfc, 0xb625fb25, 0xb2e4f692,
	0x8aabbb2f, 0x8e6ab698, 0x8329a041, 0x87e8adf6,
	0x99af8df3, 0x9d6e8044, 0x902d969d, 0x94ec9b2a,
	0xe0b30de7, 0xe4720050, 0xe9311689, 0xedf01b3e,
	0xf3b73b3b, 0xf776368c, 0xfa352055, 0xfef42de2,
	0xc6bb605f, 0xc27a6de8, 0xcf397b31, 0xcbf87686,
	0xd5bf5683, 0xd17e5b34, 0xdc3d4ded, 0xd8fc405a,
	0x6904c0ee, 0x6dc5cd59, 0x6086db80, 0x6447d637,
	0x7a00f632, 0x7ec1fb85, 0x7382ed5c, 0x7743e0eb,
	0x4f0cad56, 0x4bcda0e1, 0x468eb638, 0x424fbb8f,
	0x5c089b8a, 0x58c9963d, 0x558a80e4, 0x514b8d53,
	0x25141b9e, 0x21d51629, 0x2c9600f0, 0x28570d47,
	0x36102d42, 0x32d120f5, 0x3f92362c, 0x3b533b9b,
	0x031c7626, 0x07dd7b91, 0x0a9e6d48, 0x0e5f60ff,
	0x101840fa, 0x14d94d4d, 0x199a5b94, 0x1d5b5623,
	0xf125760e, 0xf5e47bb9, 0xf8a76d60, 0xfc6660d7,
	0xe22140d2, 0xe6e04d65, 0xeba35bbc, 0xef62560b,
	0xd72d1bb6, 0xd3ec1601, 0xdeaf00d8, 0xda6e0d6f,
	0xc4292d6a, 0xc0e820dd, 0xcdab3604, 0xc96a3bb3,
	0xbd35ad7e, 0xb9f4a0c9, 0xb4b7b610, 0xb076bba7,
	0xae319ba2, 0xaaf09615, 0xa7b380cc, 0xa3728d7b,
	0x9b3dc0c6, 0x9ffccd71, 0x92bfdba8, 0x967ed61f,
	0x8839f61a, 0x8cf8fbad, 0x81bbed74, 0x857ae0c3,
	0x5d86a099, 0x5947ad2e, 0x5404bbf7, 0x50c5b640,
	0x4e829645, 0x4a439bf2, 0x47008d2b, 0x43c1809c,
	0x7b8ecd21, 0x7f4fc096, 0x720cd64f, 0x76cddbf8,
	0x688afbfd, 0x6c4bf64a, 0x6108e093, 0x65c9ed24,
	0x11967be9, 0x1557765e, 0x18146087, 0x1cd56d30,
	0x02924d35, 0x06534082, 0x0b10565b, 0x0fd15bec,
	0x379e1651, 0x335f1be6, 0x3e1c0d3f, 0x3add0088,
	0x249a208d, 0x205b2d3a, 0x2d183be3, 0x29d93654,
	0xc5a71679, 0xc1661bce, 0xcc250d17, 0xc8e400a0,
	0xd6a320a5, 0xd2622d12, 0xdf213bcb, 0xdbe0367c,
	0xe3af7bc1, 0xe76e7676, 0xea2d60af, 0xeeec6d18,
	0xf0ab4d1d, 0xf46a40aa, 0xf9295673, 0xfde85bc4,
	0x89b7cd09, 0x8d76c0be, 0x8035d667, 0x84f4dbd0,
	0x9ab3fbd5, 0x9e72f662, 0x9331e0bb, 0x97f0ed0c,
	0xafbfa0b1, 0xab7ead06, 0xa63dbbdf, 0xa2fcb668,
	0xbcbb966d, 0xb87a9bda, 0xb5398d03, 0xb1f880b4,
};

static u32 rk_crc_32(unsigned char *buf, u32 len)
{
	u32 i;
	u32 crc = 0;

	for (i = 0; i < len; i++)
		crc = (crc << 8) ^ g_crc32_tbl[(crc >> 24) ^ *buf++];
	return crc;
}
#endif

static long vendor_storage_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	long ret = -1;
	int size;
	struct rk_vendor_req *v_req;
	u32 *page_buf;

	page_buf = kmalloc(4096, GFP_KERNEL);
	if (!page_buf)
		return -ENOMEM;

	mutex_lock(&vendor_ops_mutex);

	v_req = (struct rk_vendor_req *)page_buf;

	switch (cmd) {
	case VENDOR_READ_IO:
	{
		if (copy_from_user(page_buf, (void __user *)arg, 8)) {
			ret = -EFAULT;
			break;
		}
		if (v_req->tag == VENDOR_REQ_TAG) {
			size = emmc_vendor_read(v_req->id, v_req->data,
						v_req->len);
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
	{
		if (copy_from_user(page_buf, (void __user *)arg, 8)) {
			ret = -EFAULT;
			break;
		}
		if (v_req->tag == VENDOR_REQ_TAG && (v_req->len < 4096 - 8)) {
			if (copy_from_user(page_buf, (void __user *)arg,
					   v_req->len + 8)) {
				ret = -EFAULT;
				break;
			}
			ret = emmc_vendor_write(v_req->id,
						v_req->data,
						v_req->len);
		}
	} break;

#ifdef CONFIG_ROCKCHIP_VENDOR_STORAGE_UPDATE_LOADER
	case READ_SECTOR_IO:
	{
		if (copy_from_user(page_buf, (void __user *)arg, 512)) {
			ret = -EFAULT;
			goto exit;
		}

		size = page_buf[1];
		if (size <= 8) {
			id_blk_read_data(page_buf[0], size, (u8 *)page_buf);
			if (copy_to_user((void __user *)arg, page_buf,
					 size * 512)) {
				ret = -EFAULT;
				goto exit;
			}
		} else {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;

	case WRITE_SECTOR_IO:
	{
		if (copy_from_user(page_buf, (void __user *)arg, 4096)) {
			ret = -EFAULT;
			goto exit;
		}
		if (!g_idb_buffer) {
			g_idb_buffer = kmalloc(4096 + EMMC_BOOT_PART_SIZE * 512,
					       GFP_KERNEL);
			if (!g_idb_buffer) {
				ret = -EFAULT;
				goto exit;
			}
		}
		if (page_buf[1] <= 4088 && page_buf[0] <=
		    (EMMC_BOOT_PART_SIZE * 512 - 4096)) {
			memcpy(g_idb_buffer + page_buf[0], page_buf + 2,
			       page_buf[1]);
		} else {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;

	case END_WRITE_SECTOR_IO:
	{
		if (copy_from_user(page_buf, (void __user *)arg, 28)) {
			ret = -EFAULT;
			goto exit;
		}
		if (page_buf[0] <= (EMMC_BOOT_PART_SIZE * 512)) {
			if (!g_idb_buffer) {
				ret = -EFAULT;
				goto exit;
			}
			if (page_buf[1] !=
				rk_crc_32(g_idb_buffer, page_buf[0])) {
				ret = -2;
				goto exit;
			}
			ret =  emmc_write_idblock(page_buf[0],
						  (u8 *)g_idb_buffer,
						  &page_buf[2]);
			kfree(g_idb_buffer);
			g_idb_buffer = NULL;
		} else {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;

	case GET_BAD_BLOCK_IO:
	{
		memset(page_buf, 0, 64);
		if (copy_to_user((void __user *)arg, page_buf, 64)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;

	case GET_LOCK_FLAG_IO:
	{
		page_buf[0] = 0;
		if (copy_to_user((void __user *)arg, page_buf, 4)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;

	case GET_FLASH_INFO_IO:
	{
		page_buf[0] = 0x00800000;
		page_buf[1] = 0x00040400;
		page_buf[2] = 0x00010028;
		if (copy_to_user((void __user *)arg, page_buf, 11)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = 0;
	} break;
#endif

	default:
		ret = -EINVAL;
		goto exit;
	}
exit:
	mutex_unlock(&vendor_ops_mutex);
	kfree(page_buf);
	return ret;
}

const struct file_operations vendor_storage_fops = {
	.open = vendor_storage_open,
	.compat_ioctl	= vendor_storage_ioctl,
	.unlocked_ioctl = vendor_storage_ioctl,
	.release = vendor_storage_release,
};

static struct miscdevice vender_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vendor_storage",
	.fops  = &vendor_storage_fops,
};

static int vendor_init_thread(void *arg)
{
	int ret, try_count = 5;

	do {
		ret = emmc_vendor_storage_init();
		if (!ret) {
			break;
		}
		/* sleep 500ms wait emmc initialize completed */
		msleep(500);
	} while (try_count--);

	if (!ret) {
		ret = misc_register(&vender_storage_dev);
		rk_vendor_register(emmc_vendor_read, emmc_vendor_write);
	}
	pr_info("vendor storage:20190527 ret = %d\n", ret);
	return ret;
}

static int __init vendor_storage_init(void)
{
	g_idb_buffer = NULL;
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
