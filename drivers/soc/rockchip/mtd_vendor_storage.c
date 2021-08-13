// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mtd vendor storage
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define MTD_VENDOR_PART_START		0
#define MTD_VENDOR_PART_SIZE		8
#define MTD_VENDOR_PART_NUM		1
#define MTD_VENDOR_TAG			0x524B5644

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
	struct	vendor_item item[62];
	u8	data[MTD_VENDOR_PART_SIZE * 512 - 512 - 8];
	u32	hash;
	u32	version2;
};

struct mtd_nand_info {
	u32 blk_offset;
	u32 page_offset;
	u32 version;
	u32 ops_size;
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
static struct mtd_info *mtd;
static const char *vendor_mtd_name = "vnvm";
static struct mtd_nand_info nand_info;
static struct platform_device *g_pdev;

static int mtd_vendor_nand_write(void)
{
	size_t bytes_write;
	int err, count = 0;
	struct erase_info ei;

re_write:
	if (nand_info.page_offset >= mtd->erasesize) {
		nand_info.blk_offset += mtd->erasesize;
		if (nand_info.blk_offset >= mtd->size)
			nand_info.blk_offset = 0;
		if (mtd_block_isbad(mtd, nand_info.blk_offset))
			goto re_write;

		memset(&ei, 0, sizeof(struct erase_info));
		ei.addr = nand_info.blk_offset;
		ei.len	= mtd->erasesize;
		if (mtd_erase(mtd, &ei))
			goto re_write;

		nand_info.page_offset = 0;
	}

	err = mtd_write(mtd, nand_info.blk_offset + nand_info.page_offset,
			nand_info.ops_size, &bytes_write, (u8 *)g_vendor);
	nand_info.page_offset += nand_info.ops_size;
	if (err)
		goto re_write;

	count++;
	/* write 2 copies for reliability */
	if (count < 2)
		goto re_write;

	return 0;
}

static int mtd_vendor_storage_init(void)
{
	int err, offset;
	size_t bytes_read;
	struct erase_info ei;

	mtd = get_mtd_device_nm(vendor_mtd_name);
	if (IS_ERR(mtd))
		return -EIO;

	nand_info.page_offset = 0;
	nand_info.blk_offset = 0;
	nand_info.version = 0;
	nand_info.ops_size = (sizeof(*g_vendor) + mtd->writesize - 1) / mtd->writesize;
	nand_info.ops_size *= mtd->writesize;

	for (offset = 0; offset < mtd->size; offset += mtd->erasesize) {
		if (!mtd_block_isbad(mtd, offset)) {
			err = mtd_read(mtd, offset, sizeof(*g_vendor),
				       &bytes_read, (u8 *)g_vendor);
			if (err && err != -EUCLEAN)
				continue;
			if (bytes_read == sizeof(*g_vendor) &&
			    g_vendor->tag == MTD_VENDOR_TAG &&
			    g_vendor->version == g_vendor->version2) {
				if (g_vendor->version > nand_info.version) {
					nand_info.version = g_vendor->version;
					nand_info.blk_offset = offset;
				}
			}
		} else if (nand_info.blk_offset == offset)
			nand_info.blk_offset += mtd->erasesize;
	}

	if (nand_info.version) {
		for (offset = mtd->erasesize - nand_info.ops_size;
		     offset >= 0;
		     offset -= nand_info.ops_size) {
			err = mtd_read(mtd, nand_info.blk_offset + offset,
				       sizeof(*g_vendor),
				       &bytes_read,
				       (u8 *)g_vendor);

			/* the page is not programmed */
			if (!err && bytes_read == sizeof(*g_vendor) &&
			    g_vendor->tag == 0xFFFFFFFF &&
			    g_vendor->version == 0xFFFFFFFF &&
			    g_vendor->version2 == 0xFFFFFFFF)
				continue;

			/* point to the next free page */
			if (nand_info.page_offset < offset)
				nand_info.page_offset = offset + nand_info.ops_size;

			/* ecc error or io error */
			if (err && err != -EUCLEAN)
				continue;

			if (bytes_read == sizeof(*g_vendor) &&
			    g_vendor->tag == MTD_VENDOR_TAG &&
			    g_vendor->version == g_vendor->version2) {
				nand_info.version = g_vendor->version;
				break;
			}
		}
	} else {
		memset((u8 *)g_vendor, 0, sizeof(*g_vendor));
		g_vendor->version = 1;
		g_vendor->tag = MTD_VENDOR_TAG;
		g_vendor->free_size = sizeof(g_vendor->data);
		g_vendor->version2 = g_vendor->version;
		for (offset = 0; offset < mtd->size; offset += mtd->erasesize) {
			if (!mtd_block_isbad(mtd, offset)) {
				memset(&ei, 0, sizeof(struct erase_info));
				ei.addr = nand_info.blk_offset + offset;
				ei.len  = mtd->erasesize;
				mtd_erase(mtd, &ei);
			}
		}
		mtd_vendor_nand_write();
	}

	return 0;
}

static int mtd_vendor_read(u32 id, void *pbuf, u32 size)
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

static int mtd_vendor_write(u32 id, void *pbuf, u32 size)
{
	u32 i, j, align_size, alloc_size, item_num;
	u32 offset, next_size;
	u8 *p_data;
	struct vendor_item *item;
	struct vendor_item *next_item;

	if (!g_vendor)
		return -ENOMEM;

	p_data = g_vendor->data;
	item_num = g_vendor->item_num;
	align_size = ALIGN(size, 0x40); /* align to 64 bytes*/
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
				g_vendor->free_size = sizeof(g_vendor->data) - g_vendor->free_offset;
			} else {
				memcpy(&p_data[item->offset],
				       pbuf,
				       size);
				g_vendor->item[i].size = size;
			}
			g_vendor->version++;
			g_vendor->version2 = g_vendor->version;
			mtd_vendor_nand_write();
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
		mtd_vendor_nand_write();
		return 0;
	}
	return(-1);
}

static int vendor_storage_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int vendor_storage_release(struct inode *inode, struct file *file)
{
	return 0;
}

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
			size = mtd_vendor_read(v_req->id, v_req->data,
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
			ret = mtd_vendor_write(v_req->id,
						v_req->data,
						v_req->len);
		}
	} break;

	default:
		ret = -EINVAL;
		goto exit;
	}
exit:
	mutex_unlock(&vendor_ops_mutex);
	kfree(page_buf);
	return ret;
}

static const struct file_operations vendor_storage_fops = {
	.open = vendor_storage_open,
	.compat_ioctl	= vendor_storage_ioctl,
	.unlocked_ioctl = vendor_storage_ioctl,
	.release = vendor_storage_release,
};

static struct miscdevice vendor_storage_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "vendor_storage",
	.fops  = &vendor_storage_fops,
};

static int vendor_storage_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	mtd = get_mtd_device_nm(vendor_mtd_name);
	if (IS_ERR(mtd))
		return -EPROBE_DEFER;

	g_vendor = devm_kmalloc(dev, sizeof(*g_vendor), GFP_KERNEL | GFP_DMA);
	if (!g_vendor)
		return -ENOMEM;

	ret = mtd_vendor_storage_init();
	if (ret) {
		g_vendor = NULL;
		return ret;
	}

	ret = misc_register(&vendor_storage_dev);
	rk_vendor_register(mtd_vendor_read, mtd_vendor_write);

	pr_err("mtd vendor storage:20200313 ret = %d\n", ret);

	return ret;
}

static int vendor_storage_remove(struct platform_device *pdev)
{
	if (g_vendor) {
		misc_deregister(&vendor_storage_dev);
		g_vendor = NULL;
	}

	return 0;
}

static const struct platform_device_id vendor_storage_ids[] = {
	{ "mtd_vendor_storage", },
	{ }
};

static struct platform_driver vendor_storage_driver = {
	.probe  = vendor_storage_probe,
	.remove = vendor_storage_remove,
	.driver = {
		.name	= "mtd_vendor_storage",
	},
	.id_table	= vendor_storage_ids,
};

static int __init vendor_storage_init(void)
{
	struct platform_device *pdev;
	int ret;

	g_idb_buffer = NULL;
	ret = platform_driver_register(&vendor_storage_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple("mtd_vendor_storage",
					       -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&vendor_storage_driver);
		return PTR_ERR(pdev);
	}
	g_pdev = pdev;

	return ret;
}

static __exit void vendor_storage_deinit(void)
{
	platform_device_unregister(g_pdev);
	platform_driver_unregister(&vendor_storage_driver);
}

device_initcall_sync(vendor_storage_init);
module_exit(vendor_storage_deinit);
MODULE_LICENSE("GPL");
