// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>

#include "rkflash_api.h"
#include "rkflash_blk.h"
#include "rk_sftl.h"

#include "../soc/rockchip/flash_vendor_storage.h"

void __printf(1, 2) sftl_printk(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

static struct flash_boot_ops nandc_nand_ops = {
#ifdef	CONFIG_RK_NANDC_NAND
	FLASH_TYPE_NANDC_NAND,
	sftl_flash_init,
	sftl_flash_read,
	sftl_flash_write,
	sftl_flash_get_capacity,
	sftl_flash_deinit,
	sftl_flash_resume,
	sftl_flash_vendor_read,
	sftl_flash_vendor_write,
#else
	-1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
#endif
};

static struct flash_boot_ops sfc_nor_ops = {
#ifdef	CONFIG_RK_SFC_NOR
	FLASH_TYPE_SFC_NOR,
	spi_flash_init,
	snor_read_lba,
	snor_write_lba,
	snor_capacity,
	snor_deinit,
	snor_resume,
	snor_vendor_read,
	snor_vendor_write,
#else
	-1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
#endif
};

static struct flash_boot_ops sfc_nand_ops = {
#ifdef	CONFIG_RK_SFC_NAND
	FLASH_TYPE_SFC_NAND,
	snand_init,
	snand_read,
	snand_write,
	snand_get_capacity,
	snand_deinit,
	snand_resume,
	snand_vendor_read,
	snand_vendor_write,
#else
	-1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
#endif
};

static struct flash_boot_ops *g_boot_ops[] = {
	&nandc_nand_ops,
	&sfc_nor_ops,
	&sfc_nand_ops,
};

static int g_flash_type = -1;

static struct flash_part disk_array[MAX_PART_COUNT];
static int g_max_part_num = 4;
#define FW_HRADER_PT_NAME		("fw_header_p")
static struct flash_part fw_header_p;

#define PART_READONLY 0x85
#define PART_WRITEONLY 0x86
#define PART_NO_ACCESS 0x87

static unsigned long totle_read_data;
static unsigned long totle_write_data;
static unsigned long totle_read_count;
static unsigned long totle_write_count;
static int rkflash_dev_initialised;

static char *mtd_read_temp_buffer;
#define MTD_RW_SECTORS (512)
static DEFINE_MUTEX(g_flash_ops_mutex);

int rkflash_vendor_read(u32 sec, u32 n_sec, void *p_data)
{
	int ret;

	if (g_boot_ops[g_flash_type]->vendor_read) {
		mutex_lock(&g_flash_ops_mutex);
		ret = g_boot_ops[g_flash_type]->vendor_read(sec, n_sec, p_data);
		mutex_unlock(&g_flash_ops_mutex);
	} else {
		ret = -EPERM;
	}

	return ret;
}

int rkflash_vendor_write(u32 sec, u32 n_sec, void *p_data)
{
	int ret;

	if (g_boot_ops[g_flash_type]->vendor_write) {
		mutex_lock(&g_flash_ops_mutex);
		ret = g_boot_ops[g_flash_type]->vendor_write(sec,
							     n_sec,
							     p_data);
		mutex_unlock(&g_flash_ops_mutex);
	} else {
		ret = -EPERM;
	}

	return ret;
}

static unsigned int rk_partition_init(struct flash_part *part)
{
	int i, part_num = 0;
	int desity;
	struct STRUCT_PART_INFO *g_part;  /* size 2KB */

	g_part = kmalloc(sizeof(*g_part), GFP_KERNEL | GFP_DMA);
	if (!g_part)
		return 0;

	if (g_boot_ops[g_flash_type]->read(0, 4, g_part) == 0) {
		if (g_part->hdr.ui_fw_tag == RK_PARTITION_TAG) {
			part_num = g_part->hdr.ui_part_entry_count;
			desity = g_boot_ops[g_flash_type]->get_capacity();
			for (i = 0; i < part_num; i++) {
				memcpy(part[i].name,
				       g_part->part[i].sz_name,
				       32);
				part[i].offset = g_part->part[i].ui_pt_off;
				part[i].size = g_part->part[i].ui_pt_sz;
				part[i].type = 0;
				if (part[i].size == UINT_MAX)
					part[i].size = desity - part[i].offset;
			}
		}
	}
	kfree(g_part);

	memset(&fw_header_p, 0x0, sizeof(fw_header_p));
	memcpy(fw_header_p.name, FW_HRADER_PT_NAME, strlen(FW_HRADER_PT_NAME));
	fw_header_p.offset = 0x0;
	fw_header_p.size = 0x4;
	fw_header_p.type = 0;

	return part_num;
}

static int rkflash_proc_show(struct seq_file *m, void *v)
{
	int real_size = 0;
	char *ftl_buf = kzalloc(4096, GFP_KERNEL);

	real_size = rknand_proc_ftlread(4096, ftl_buf);
	if (real_size > 0)
		seq_printf(m, "%s", ftl_buf);
	seq_printf(m, "Totle Read %ld KB\n", totle_read_data >> 1);
	seq_printf(m, "Totle Write %ld KB\n", totle_write_data >> 1);
	seq_printf(m, "totle_write_count %ld\n", totle_write_count);
	seq_printf(m, "totle_read_count %ld\n", totle_read_count);
	kfree(ftl_buf);
	return 0;
}

static int rkflash_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rkflash_proc_show, PDE_DATA(inode));
}

static const struct file_operations rkflash_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= rkflash_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rkflash_create_procfs(void)
{
	struct proc_dir_entry *ent;

	ent = proc_create_data("rkflash", 0x664, NULL, &rkflash_proc_fops,
			       (void *)0);
	if (!ent)
		return -1;

	return 0;
}

static int rkflash_xfer(struct flash_blk_dev *dev,
			unsigned long start,
			unsigned long nsector,
			char *buf,
			int cmd,
			int totle_nsec)
{
	int ret;

	if (dev->disable_access ||
	    (cmd == WRITE && dev->readonly) ||
	    (cmd == READ && dev->writeonly)) {
		return -EIO;
	}

	start += dev->off_size;

	switch (cmd) {
	case READ:
		totle_read_data += nsector;
		totle_read_count++;
		mutex_lock(&g_flash_ops_mutex);
		ret = g_boot_ops[g_flash_type]->read(start, nsector, buf);
		mutex_unlock(&g_flash_ops_mutex);
		if (ret)
			ret = -EIO;
		break;

	case WRITE:
		totle_write_data += nsector;
		totle_write_count++;
		mutex_lock(&g_flash_ops_mutex);
		ret = g_boot_ops[g_flash_type]->write(start, nsector, buf);
		mutex_unlock(&g_flash_ops_mutex);
		if (ret)
			ret = -EIO;
		break;

	default:
		ret = -EIO;
		break;
	}

	return ret;
}

static DECLARE_WAIT_QUEUE_HEAD(rkflash_thread_wait);
static unsigned long rkflash_req_jiffies;
static unsigned int rknand_req_do;

static int req_check_buffer_align(struct request *req, char **pbuf)
{
	int nr_vec = 0;
	struct bio_vec bvec;
	struct req_iterator iter;
	char *buffer;
	void *firstbuf = 0;
	char *nextbuffer = 0;

	rq_for_each_segment(bvec, req, iter) {
		buffer = page_address(bvec.bv_page) + bvec.bv_offset;
		if (!firstbuf)
			firstbuf = buffer;
		nr_vec++;
		if (nextbuffer && nextbuffer != buffer)
			return 0;
		nextbuffer = buffer + bvec.bv_len;
	}
	*pbuf = firstbuf;
	return 1;
}

static int rkflash_blktrans_thread(void *arg)
{
	struct flash_blk_ops *blk_ops = arg;
	struct request_queue *rq = blk_ops->rq;
	struct request *req = NULL;
	char *buf;
	struct req_iterator rq_iter;
	struct bio_vec bvec;
	unsigned long long sector_index = ULLONG_MAX;
	unsigned long totle_nsect;
	unsigned long rq_len = 0;
	int rw_flag = 0;

	spin_lock_irq(rq->queue_lock);
	while (!blk_ops->quit) {
		int res;
		struct flash_blk_dev *dev;
		DECLARE_WAITQUEUE(wait, current);

		if (!req)
			req = blk_fetch_request(rq);
		if (!req) {
			add_wait_queue(&blk_ops->thread_wq, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(rq->queue_lock);
			rkflash_req_jiffies = HZ / 10;
			wait_event_timeout(blk_ops->thread_wq,
					   blk_ops->quit || rknand_req_do,
					   rkflash_req_jiffies);
			rknand_req_do = 0;
			spin_lock_irq(rq->queue_lock);
			remove_wait_queue(&blk_ops->thread_wq, &wait);
			continue;
		} else {
			rkflash_req_jiffies = 1 * HZ;
		}

		dev = req->rq_disk->private_data;
		totle_nsect = (req->__data_len) >> 9;
		sector_index = blk_rq_pos(req);
		rq_len = 0;
		buf = 0;
		res = 0;

		if (req->cmd_flags & REQ_DISCARD) {
			if (!__blk_end_request_cur(req, res))
				req = NULL;
			continue;
		} else if (req->cmd_flags & REQ_FLUSH) {
			if (!__blk_end_request_cur(req, res))
				req = NULL;
			continue;
		}

		rw_flag = req->cmd_flags & REQ_WRITE;
		if (rw_flag == READ && mtd_read_temp_buffer) {
			buf = mtd_read_temp_buffer;
			req_check_buffer_align(req, &buf);
			spin_unlock_irq(rq->queue_lock);
			res = rkflash_xfer(dev,
					   sector_index,
					   totle_nsect,
					   buf,
					   rw_flag,
					   totle_nsect);
			spin_lock_irq(rq->queue_lock);
			if (buf == mtd_read_temp_buffer) {
				char *p = buf;

				rq_for_each_segment(bvec, req, rq_iter) {
					memcpy(page_address(bvec.bv_page) +
					       bvec.bv_offset,
					       p,
					       bvec.bv_len);
					p += bvec.bv_len;
				}
			}
		} else {
			rq_for_each_segment(bvec, req, rq_iter) {
				if ((page_address(bvec.bv_page)
					+ bvec.bv_offset)
					== (buf + rq_len)) {
					rq_len += bvec.bv_len;
				} else {
					if (rq_len) {
						spin_unlock_irq(rq->queue_lock);
						res = rkflash_xfer(dev,
								   sector_index,
								   rq_len >> 9,
								   buf,
								   rw_flag,
								   totle_nsect);
						spin_lock_irq(rq->queue_lock);
					}
					sector_index += rq_len >> 9;
					buf = (page_address(bvec.bv_page) +
						bvec.bv_offset);
					rq_len = bvec.bv_len;
				}
			}
			if (rq_len) {
				spin_unlock_irq(rq->queue_lock);
				res = rkflash_xfer(dev,
						   sector_index,
						   rq_len >> 9,
						   buf,
						   rw_flag,
						   totle_nsect);
				spin_lock_irq(rq->queue_lock);
			}
		}
		__blk_end_request_all(req, res);
		req = NULL;
	}
	pr_info("flash th quited\n");
	blk_ops->flash_th_quited = 1;
	if (req)
		__blk_end_request_all(req, -EIO);
	while ((req = blk_fetch_request(rq)) != NULL)
		__blk_end_request_all(req, -ENODEV);
	spin_unlock_irq(rq->queue_lock);
	complete_and_exit(&blk_ops->thread_exit, 0);
	return 0;
}

static void rkflash_blk_request(struct request_queue *rq)
{
	struct flash_blk_ops *blk_ops = rq->queuedata;
	struct request *req = NULL;

	if (blk_ops->flash_th_quited) {
		while ((req = blk_fetch_request(rq)) != NULL)
			__blk_end_request_all(req, -ENODEV);
		return;
	}
	rknand_req_do = 1;
	wake_up(&blk_ops->thread_wq);
}

static int rkflash_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void rkflash_release(struct gendisk *disk, fmode_t mode)
{
};

#define DISABLE_WRITE _IO('V', 0)
#define ENABLE_WRITE _IO('V', 1)
#define DISABLE_READ _IO('V', 2)
#define ENABLE_READ _IO('V', 3)
static int rkflash_ioctl(struct block_device *bdev, fmode_t mode,
			 unsigned int cmd,
			 unsigned long arg)
{
	struct flash_blk_dev *dev = bdev->bd_disk->private_data;

	switch (cmd) {
	case ENABLE_WRITE:
		dev->disable_access = 0;
		dev->readonly = 0;
		set_disk_ro(dev->blkcore_priv, 0);
		return 0;

	case DISABLE_WRITE:
		dev->readonly = 1;
		set_disk_ro(dev->blkcore_priv, 1);
		return 0;

	case ENABLE_READ:
		dev->disable_access = 0;
		dev->writeonly = 0;
		return 0;

	case DISABLE_READ:
		dev->writeonly = 1;
		return 0;
	default:
		return -ENOTTY;
	}
}

const struct block_device_operations rkflash_blktrans_ops = {
	.owner = THIS_MODULE,
	.open = rkflash_open,
	.release = rkflash_release,
	.ioctl = rkflash_ioctl,
};

static struct flash_blk_ops mytr = {
	.name =  "rkflash",
	.major = 31,
	.minorbits = 0,
	.owner = THIS_MODULE,
};

static int rkflash_add_dev(struct flash_blk_ops *blk_ops,
			   struct flash_part *part)
{
	struct flash_blk_dev *dev;
	struct gendisk *gd;

	if (part->size == 0)
		return -1;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	gd = alloc_disk(1 << blk_ops->minorbits);
	if (!gd) {
		kfree(dev);
		return -ENOMEM;
	}

	dev->blk_ops = blk_ops;
	dev->size = part->size;
	dev->off_size = part->offset;
	dev->devnum = blk_ops->last_dev_index;
	list_add_tail(&dev->list, &blk_ops->devs);
	blk_ops->last_dev_index++;

	gd->major = blk_ops->major;
	gd->first_minor = (dev->devnum) << blk_ops->minorbits;
	gd->fops = &rkflash_blktrans_ops;

	if (part->name[0]) {
		snprintf(gd->disk_name,
			 sizeof(gd->disk_name),
			 "%s",
			 part->name);
	} else {
		gd->flags = GENHD_FL_EXT_DEVT;
		gd->minors = 255;
		snprintf(gd->disk_name,
			 sizeof(gd->disk_name),
			 "%s%d",
			 blk_ops->name,
			 dev->devnum);
	}

	set_capacity(gd, dev->size);

	gd->private_data = dev;
	dev->blkcore_priv = gd;
	gd->queue = blk_ops->rq;
	gd->queue->bypass_depth = 1;

	if (part->type == PART_NO_ACCESS)
		dev->disable_access = 1;

	if (part->type == PART_READONLY)
		dev->readonly = 1;

	if (part->type == PART_WRITEONLY)
		dev->writeonly = 1;

	if (dev->readonly)
		set_disk_ro(gd, 1);

	add_disk(gd);

	return 0;
}

static int rkflash_remove_dev(struct flash_blk_dev *dev)
{
	struct gendisk *gd;

	gd = dev->blkcore_priv;
	list_del(&dev->list);
	gd->queue = NULL;
	del_gendisk(gd);
	put_disk(gd);
	kfree(dev);
	return 0;
}

static int rkflash_blk_register(struct flash_blk_ops *blk_ops)
{
	int i, ret;
	u64 offset;

	rknand_req_do = 0;
	blk_ops->quit = 0;
	blk_ops->flash_th_quited = 0;

	mtd_read_temp_buffer = kmalloc(MTD_RW_SECTORS * 512,
				       GFP_KERNEL | GFP_DMA);

	ret = register_blkdev(blk_ops->major, blk_ops->name);
	if (ret)
		return -1;

	spin_lock_init(&blk_ops->queue_lock);
	init_completion(&blk_ops->thread_exit);
	init_waitqueue_head(&blk_ops->thread_wq);

	blk_ops->rq = blk_init_queue(rkflash_blk_request, &blk_ops->queue_lock);
	if (!blk_ops->rq) {
		unregister_blkdev(blk_ops->major, blk_ops->name);
		return  -1;
	}

	blk_queue_max_hw_sectors(blk_ops->rq, MTD_RW_SECTORS);
	blk_queue_max_segments(blk_ops->rq, MTD_RW_SECTORS);

	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, blk_ops->rq);
	blk_queue_max_discard_sectors(blk_ops->rq, UINT_MAX >> 9);

	blk_ops->rq->queuedata = blk_ops;
	INIT_LIST_HEAD(&blk_ops->devs);
	kthread_run(rkflash_blktrans_thread, (void *)blk_ops, "rkflash");
	g_max_part_num = rk_partition_init(disk_array);
	if (g_max_part_num) {
		/* partition 0 is save vendor data, need hidden */
		blk_ops->last_dev_index = 0;
		for (i = 1; i < g_max_part_num; i++) {
			offset = (u64)disk_array[i].offset;
			pr_info("%10s: 0x%09llx -- 0x%09llx (%llu MB)\n",
				disk_array[i].name,
				offset * 512,
				(u64)(offset + disk_array[i].size) * 512,
				(u64)disk_array[i].size / 2048);
			rkflash_add_dev(blk_ops, &disk_array[i]);
		}
		rkflash_add_dev(blk_ops, &fw_header_p);
	} else {
		struct flash_part part;

		part.offset = 0;
		part.size = g_boot_ops[g_flash_type]->get_capacity();
		part.type = 0;
		part.name[0] = 0;
		rkflash_add_dev(&mytr, &part);
	}
	rkflash_create_procfs();

	return 0;
}

static void rkflash_blk_unregister(struct flash_blk_ops *blk_ops)
{
	struct list_head *this, *next;

	blk_ops->quit = 1;
	wake_up(&blk_ops->thread_wq);
	wait_for_completion(&blk_ops->thread_exit);
	list_for_each_safe(this, next, &blk_ops->devs) {
		struct flash_blk_dev *dev =
			list_entry(this, struct flash_blk_dev, list);

		rkflash_remove_dev(dev);
	}
	blk_cleanup_queue(blk_ops->rq);
	unregister_blkdev(blk_ops->major, blk_ops->name);
}

int rkflash_dev_init(void __iomem *reg_addr, enum flash_con_type con_type)
{
	int ret;
	int tmp_id, start_id, end_id;

	pr_err("%s\n", __func__);
	if (rkflash_dev_initialised) {
		pr_err("rkflash has already inited as id[%d]\n", g_flash_type);
		return -1;
	}

	if (con_type == FLASH_CON_TYPE_NANDC) {
		start_id = FLASH_TYPE_NANDC_NAND;
		end_id = FLASH_TYPE_NANDC_NAND;
	} else {
		start_id = FLASH_TYPE_SFC_NOR;
		end_id = FLASH_TYPE_SFC_NAND;
	}
	for (tmp_id = start_id; tmp_id <= end_id; tmp_id++) {
		pr_info("init rkflash[%d]\n", tmp_id);
		if (g_boot_ops[tmp_id]->id == -1) {
			pr_err("rkflash[%d] is invalid\n", tmp_id);
			if (tmp_id == end_id)
				return -1;
			continue;
		}
		ret = g_boot_ops[tmp_id]->init(reg_addr);
		if (ret) {
			pr_err("rkflash[%d] init fail\n", tmp_id);
			if (tmp_id == end_id)
				return -1;
			continue;
		} else {
			break;
		}
	}
	pr_info("rkflash[%d] init success\n", tmp_id);
	g_flash_type = tmp_id;
	mytr.quit = 1;
	if (g_flash_type == FLASH_TYPE_SFC_NOR) {
		flash_vendor_dev_ops_register(rkflash_vendor_read,
					      rkflash_vendor_write);
	} else {
#if defined(CONFIG_RK_NANDC_NAND) || defined(CONFIG_RK_SFC_NAND)
		rk_sftl_vendor_dev_ops_register(rkflash_vendor_read,
						rkflash_vendor_write);
		ret = rk_sftl_vendor_storage_init();
		if (!ret) {
			rk_vendor_register(rk_sftl_vendor_read,
					   rk_sftl_vendor_write);
			rk_sftl_vendor_register();
			pr_info("rkflashd vendor storage init ok !\n");
		} else {
			pr_info("rkflash vendor storage init failed !\n");
		}
#endif
	}
#ifdef CONFIG_RK_SFC_NOR_MTD
	if (g_flash_type == FLASH_TYPE_SFC_NOR) {
		pr_info("sfc_nor flash registered as a mtd device\n");
		rkflash_dev_initialised = 1;
		return 0;
	}
#endif
	ret = rkflash_blk_register(&mytr);
	if (ret) {
		pr_err("rkflash_blk_register fail\n");
		g_flash_type = -1;
		return -1;
	}
	rkflash_dev_initialised = 1;

	return ret;
}

int rkflash_dev_exit(void)
{
	if (rkflash_dev_initialised) {
		g_flash_type = -1;
		rkflash_dev_initialised = 0;
		rkflash_blk_unregister(&mytr);
		pr_info("%s:OK\n", __func__);
	}
	return 0;
}

int rkflash_dev_suspend(void)
{
	mutex_lock(&g_flash_ops_mutex);
	return 0;
}

int rkflash_dev_resume(void __iomem *reg_addr)
{
	g_boot_ops[g_flash_type]->resume(reg_addr);
	mutex_unlock(&g_flash_ops_mutex);
	return 0;
}

void rkflash_dev_shutdown(void)
{
	pr_info("rkflash_shutdown...\n");
	if (mytr.quit == 0) {
		mytr.quit = 1;
		wake_up(&mytr.thread_wq);
		wait_for_completion(&mytr.thread_exit);
	}
	g_boot_ops[g_flash_type]->deinit();
	pr_info("rkflash_shutdown:OK\n");
}
