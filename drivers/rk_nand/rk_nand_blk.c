/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/spinlock.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>

#include "rk_nand_blk.h"
#include "rk_ftl_api.h"

static struct nand_part disk_array[MAX_PART_COUNT];
static int g_max_part_num = 4;

#define PART_READONLY 0x85
#define PART_WRITEONLY 0x86
#define PART_NO_ACCESS 0x87

static unsigned long totle_read_data;
static unsigned long totle_write_data;
static unsigned long totle_read_count;
static unsigned long totle_write_count;
static int rk_nand_dev_initialised;

static char *mtd_read_temp_buffer;
#define MTD_RW_SECTORS (512)

static int rknand_proc_show(struct seq_file *m, void *v)
{
	m->count = rknand_proc_ftlread(m->buf);
	seq_printf(m, "Totle Read %ld KB\n", totle_read_data >> 1);
	seq_printf(m, "Totle Write %ld KB\n", totle_write_data >> 1);
	seq_printf(m, "totle_write_count %ld\n", totle_write_count);
	seq_printf(m, "totle_read_count %ld\n", totle_read_count);
	return 0;
}

static int rknand_mtd_proc_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "%s", "dev:    size   erasesize  name\n");
	for (i = 0; i < g_max_part_num; i++) {
		seq_printf(m, "rknand%d: %8.8llx %8.8x \"%s\"\n", i,
			   (unsigned long long)disk_array[i].size * 512,
			   32 * 0x200, disk_array[i].name);
	}
	return 0;
}

static int rknand_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rknand_proc_show, PDE_DATA(inode));
}

static int rknand_mtd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rknand_mtd_proc_show, PDE_DATA(inode));
}

static const struct file_operations rknand_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= rknand_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations rknand_mtd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= rknand_mtd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rknand_create_procfs(void)
{
	struct proc_dir_entry *ent;

	ent = proc_create_data("rknand", 0444, NULL, &rknand_proc_fops,
			       (void *)0);
	if (!ent)
		return -1;

	ent = proc_create_data("mtd", 0444, NULL, &rknand_mtd_proc_fops,
			       (void *)0);
	if (!ent)
		return -1;
	return 0;
}

static struct mutex g_rk_nand_ops_mutex;

static void rknand_device_lock_init(void)
{
	mutex_init(&g_rk_nand_ops_mutex);
}

void rknand_device_lock(void)
{
	mutex_lock(&g_rk_nand_ops_mutex);
}

int rknand_device_trylock(void)
{
	return mutex_trylock(&g_rk_nand_ops_mutex);
}

void rknand_device_unlock(void)
{
	mutex_unlock(&g_rk_nand_ops_mutex);
}

static int nand_dev_transfer(struct nand_blk_dev *dev,
			     unsigned long start,
			     unsigned long nsector,
			     char *buf,
			     int cmd,
			     int totle_nsec)
{
	int ret;

	if (dev->disable_access ||
	    ((cmd == WRITE) && dev->readonly) ||
	    ((cmd == READ) && dev->writeonly)) {
		return -EIO;
	}

	start += dev->off_size;
	rknand_device_lock();

	switch (cmd) {
	case READ:
		totle_read_data += nsector;
		totle_read_count++;
		ret = FtlRead(0, start, nsector, buf);
		if (ret)
			ret = -EIO;
		break;

	case WRITE:
		totle_write_data += nsector;
		totle_write_count++;
		ret = FtlWrite(0, start, nsector, buf);
		if (ret)
			ret = -EIO;
		break;

	default:
		ret = -EIO;
		break;
	}

	rknand_device_unlock();
	return ret;
}

static DECLARE_WAIT_QUEUE_HEAD(rknand_thread_wait);
static void rk_ftl_gc_timeout_hack(unsigned long data);
static DEFINE_TIMER(rk_ftl_gc_timeout, rk_ftl_gc_timeout_hack, 0, 0);
static unsigned long rk_ftl_gc_jiffies;
static unsigned long rk_ftl_gc_do;

static void rk_ftl_gc_timeout_hack(unsigned long data)
{
	del_timer(&rk_ftl_gc_timeout);
	rk_ftl_gc_do++;
	rk_ftl_gc_timeout.expires = jiffies + rk_ftl_gc_jiffies * rk_ftl_gc_do;
	add_timer(&rk_ftl_gc_timeout);
}

static int req_check_buffer_align(struct request *req, char **pbuf)
{
	int nr_vec = 0;
	struct bio_vec bv;
	struct req_iterator iter;
	char *buffer;
	void *firstbuf = 0;
	char *nextbuffer = 0;

	rq_for_each_segment(bv, req, iter) {
		buffer = page_address(bv.bv_page) + bv.bv_offset;
		if (!firstbuf)
			firstbuf = buffer;
		nr_vec++;
		if (nextbuffer && nextbuffer != buffer)
			return 0;
		nextbuffer = buffer + bv.bv_len;
	}
	*pbuf = firstbuf;
	return 1;
}

static int nand_blktrans_thread(void *arg)
{
	struct nand_blk_ops *nandr = arg;
	struct request_queue *rq = nandr->rq;
	struct request *req = NULL;
	int ftl_gc_status = 0;
	char *buf;
	struct req_iterator rq_iter;
	struct bio_vec bvec;
	unsigned long long sector_index = ULLONG_MAX;
	unsigned long totle_nsect;
	unsigned long rq_len = 0;
	int rw_flag = 0;
	int req_empty_times = 0;

	spin_lock_irq(rq->queue_lock);
	rk_ftl_gc_jiffies = HZ / 10; /* do garbage collect after 100ms */
	rk_ftl_gc_do = 0;
	rk_ftl_gc_timeout.expires = jiffies + rk_ftl_gc_jiffies;
	add_timer(&rk_ftl_gc_timeout);

	while (!nandr->quit) {
		int res;
		struct nand_blk_dev *dev;
		DECLARE_WAITQUEUE(wait, current);

		if (!req)
			req = blk_fetch_request(rq);
		if (!req) {
			add_wait_queue(&nandr->thread_wq, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(rq->queue_lock);
			if (rknand_device_trylock()) {
				ftl_gc_status = rk_ftl_garbage_collect(1, 0);
				rknand_device_unlock();
				rk_ftl_gc_jiffies = HZ / 50;
				if (ftl_gc_status == 0)
					rk_ftl_gc_jiffies = 1 * HZ;

			} else {
				rk_ftl_gc_jiffies = HZ / 50;
			}
			req_empty_times++;
			if (req_empty_times < 10)
				rk_ftl_gc_jiffies = HZ / 50;
			/* 100ms cache write back */
			if (req_empty_times >= 5 && req_empty_times < 7) {
				rknand_device_lock();
				rk_ftl_cache_write_back();
				rknand_device_unlock();
			}
			wait_event_timeout(nandr->thread_wq,
					   rk_ftl_gc_do || nandr->quit,
					   rk_ftl_gc_jiffies);
			rk_ftl_gc_do = 0;
			spin_lock_irq(rq->queue_lock);
			remove_wait_queue(&nandr->thread_wq, &wait);
			continue;
		} else {
			rk_ftl_gc_jiffies = 1 * HZ;
			req_empty_times = 0;
		}

		dev = req->rq_disk->private_data;
		totle_nsect = (req->__data_len) >> 9;
		sector_index = blk_rq_pos(req);
		rq_len = 0;
		buf = 0;
		res = 0;

		if (req->cmd_flags & REQ_DISCARD) {
			spin_unlock_irq(rq->queue_lock);
			rknand_device_lock();
			if (FtlDiscard(blk_rq_pos(req) +
				       dev->off_size, totle_nsect))
				res = -EIO;
			rknand_device_unlock();
			spin_lock_irq(rq->queue_lock);
			if (!__blk_end_request_cur(req, res))
				req = NULL;
			continue;
		} else if (req->cmd_flags & REQ_FLUSH) {
			spin_unlock_irq(rq->queue_lock);
			rknand_device_lock();
			rk_ftl_cache_write_back();
			rknand_device_unlock();
			spin_lock_irq(rq->queue_lock);
			if (!__blk_end_request_cur(req, res))
				req = NULL;
			continue;
		}

		rw_flag = req->cmd_flags & REQ_WRITE;
		if (rw_flag == READ && mtd_read_temp_buffer) {
			buf = mtd_read_temp_buffer;
			req_check_buffer_align(req, &buf);
			spin_unlock_irq(rq->queue_lock);
			res = nand_dev_transfer(dev,
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
						res = nand_dev_transfer(dev,
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
				res = nand_dev_transfer(dev,
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
	pr_info("nand th quited\n");
	nandr->nand_th_quited = 1;
	if (req)
		__blk_end_request_all(req, -EIO);
	rk_nand_schedule_enable_config(0);
	while ((req = blk_fetch_request(rq)) != NULL)
		__blk_end_request_all(req, -ENODEV);
	spin_unlock_irq(rq->queue_lock);
	complete_and_exit(&nandr->thread_exit, 0);
	return 0;
}

static void nand_blk_request(struct request_queue *rq)
{
	struct nand_blk_ops *nandr = rq->queuedata;
	struct request *req = NULL;

	if (nandr->nand_th_quited) {
		while ((req = blk_fetch_request(rq)) != NULL)
			__blk_end_request_all(req, -ENODEV);
		return;
	}
	rk_ftl_gc_do = 1;
	wake_up(&nandr->thread_wq);
}

static int rknand_get_part(char *parts,
			   struct nand_part *this_part,
			   int *part_index)
{
	char delim;
	unsigned int mask_flags;
	unsigned long long size, offset = ULLONG_MAX;
	char name[40] = "\0";

	if (*parts == '-') {
		size = ULLONG_MAX;
		parts++;
	} else {
		size = memparse(parts, &parts);
	}

	if (*parts == '@') {
		parts++;
		offset = memparse(parts, &parts);
	}

	mask_flags = 0;
	delim = 0;

	if (*parts == '(')
		delim = ')';

	if (delim) {
		char *p;

		p = strchr(parts + 1, delim);
		if (!p)
			return 0;
		strncpy(name, parts + 1, p - (parts + 1));
		parts = p + 1;
	}

	if (strncmp(parts, "ro", 2) == 0) {
		mask_flags = PART_READONLY;
		parts += 2;
	}

	if (strncmp(parts, "wo", 2) == 0) {
		mask_flags = PART_WRITEONLY;
		parts += 2;
	}

	this_part->size = (unsigned long)size;
	this_part->offset = (unsigned long)offset;
	this_part->type = mask_flags;
	sprintf(this_part->name, "%s", name);

	if ((++(*part_index) < MAX_PART_COUNT) && (*parts == ','))
		rknand_get_part(++parts, this_part + 1, part_index);

	return 1;
}

static int nand_prase_cmdline_part(struct nand_part *pdisk_part)
{
	char *pbuf;
	int part_num = 0, i;
	unsigned int cap_size = rk_ftl_get_capacity();
	char *cmdline;

	cmdline = strstr(saved_command_line, "mtdparts=");
	if (!cmdline)
		return 0;
	cmdline += 9;
	if (!memcmp(cmdline, "rk29xxnand:", strlen("rk29xxnand:"))) {
		pbuf = cmdline + strlen("rk29xxnand:");
		rknand_get_part(pbuf, pdisk_part, &part_num);
		if (part_num)
			pdisk_part[part_num - 1].size = cap_size -
				pdisk_part[part_num - 1].offset;

		for (i = 0; i < part_num; i++) {
			if (pdisk_part[i].size + pdisk_part[i].offset
				> cap_size) {
				pdisk_part[i].size = cap_size -
					pdisk_part[i].offset;
				pr_err("partition error....max cap:%x\n",
					cap_size);
				if (!pdisk_part[i].size)
					return i;
				else
					return (i + 1);
			}
		}
		return part_num;
	}
	return 0;
}

static int rknand_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void rknand_release(struct gendisk *disk, fmode_t mode)
{
};

#define DISABLE_WRITE _IO('V', 0)
#define ENABLE_WRITE _IO('V', 1)
#define DISABLE_READ _IO('V', 2)
#define ENABLE_READ _IO('V', 3)
static int rknand_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd,
			unsigned long arg)
{
	struct nand_blk_dev *dev = bdev->bd_disk->private_data;

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

const struct block_device_operations nand_blktrans_ops = {
	.owner = THIS_MODULE,
	.open = rknand_open,
	.release = rknand_release,
	.ioctl = rknand_ioctl,
};

static struct nand_blk_ops mytr = {
	.name =  "rknand",
	.major = 31,
	.minorbits = 0,
	.owner = THIS_MODULE,
};

static int nand_add_dev(struct nand_blk_ops *nandr, struct nand_part *part)
{
	struct nand_blk_dev *dev;
	struct gendisk *gd;

	if (part->size == 0)
		return -1;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	gd = alloc_disk(1 << nandr->minorbits);
	if (!gd) {
		kfree(dev);
		return -ENOMEM;
	}

	dev->nandr = nandr;
	dev->size = part->size;
	dev->off_size = part->offset;
	dev->devnum = nandr->last_dev_index;
	list_add_tail(&dev->list, &nandr->devs);
	nandr->last_dev_index++;

	gd->major = nandr->major;
	gd->first_minor = (dev->devnum) << nandr->minorbits;

	gd->fops = &nand_blktrans_ops;

	if (part->name[0]) {
		snprintf(gd->disk_name,
			 sizeof(gd->disk_name),
			 "%s_%s",
			 nandr->name,
			 part->name);
	} else {
		gd->flags = GENHD_FL_EXT_DEVT;
		gd->minors = 255;
		snprintf(gd->disk_name,
			 sizeof(gd->disk_name),
			 "%s%d",
			 nandr->name,
			 dev->devnum);
	}
	set_capacity(gd, dev->size);

	gd->private_data = dev;
	dev->blkcore_priv = gd;
	gd->queue = nandr->rq;
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

static int nand_remove_dev(struct nand_blk_dev *dev)
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

int nand_blk_add_whole_disk(void)
{
	struct nand_part part;

	part.offset = 0;
	part.size = rk_ftl_get_capacity();
	part.type = 0;
	memcpy(part.name, "rknand", sizeof("rknand"));
	nand_add_dev(&mytr, &part);
	return 0;
}

static int nand_blk_register(struct nand_blk_ops *nandr)
{
	int i, ret;
	u32 part_size;

	rk_nand_schedule_enable_config(1);
	nandr->quit = 0;
	nandr->nand_th_quited = 0;

	mtd_read_temp_buffer = kmalloc(MTD_RW_SECTORS * 512,
				       GFP_KERNEL | GFP_DMA);

	ret = register_blkdev(nandr->major, nandr->name);
	if (ret)
		return -1;

	spin_lock_init(&nandr->queue_lock);
	init_completion(&nandr->thread_exit);
	init_waitqueue_head(&nandr->thread_wq);
	rknand_device_lock_init();

	nandr->rq = blk_init_queue(nand_blk_request, &nandr->queue_lock);
	if (!nandr->rq) {
		unregister_blkdev(nandr->major, nandr->name);
		return  -1;
	}

	blk_queue_max_hw_sectors(nandr->rq, MTD_RW_SECTORS);
	blk_queue_max_segments(nandr->rq, MTD_RW_SECTORS);

	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, nandr->rq);
	blk_queue_max_discard_sectors(nandr->rq, UINT_MAX >> 9);

	nandr->rq->queuedata = nandr;
	INIT_LIST_HEAD(&nandr->devs);
	kthread_run(nand_blktrans_thread, (void *)nandr, "rknand");

	g_max_part_num = nand_prase_cmdline_part(disk_array);
	if (g_max_part_num) {
		nandr->last_dev_index = 0;
		for (i = 0; i < g_max_part_num; i++) {
			part_size = (disk_array[i].offset + disk_array[i].size);
			pr_info("%10s: 0x%09llx -- 0x%09llx (%llu MB)\n",
				disk_array[i].name,
				(u64)disk_array[i].offset * 512,
				(u64)part_size * 512,
				(u64)disk_array[i].size / 2048);
			nand_add_dev(nandr, &disk_array[i]);
		}
	} else {
		struct nand_part part;

		part.offset = 0;
		part.size = rk_ftl_get_capacity();
		part.type = 0;
		part.name[0] = 0;
		nand_add_dev(&mytr, &part);
	}

	rknand_create_procfs();
	rk_ftl_storage_sys_init();

	ret = rk_ftl_vendor_storage_init();
	if (!ret) {
		rk_vendor_register(rk_ftl_vendor_read, rk_ftl_vendor_write);
		rknand_vendor_storage_init();
		pr_info("rknand vendor storage init ok !\n");
	} else {
		pr_info("rknand vendor storage init failed !\n");
	}

	return 0;
}

static void nand_blk_unregister(struct nand_blk_ops *nandr)
{
	struct list_head *this, *next;

	if (!rk_nand_dev_initialised)
		return;
	nandr->quit = 1;
	wake_up(&nandr->thread_wq);
	wait_for_completion(&nandr->thread_exit);
	list_for_each_safe(this, next, &nandr->devs) {
		struct nand_blk_dev *dev
			= list_entry(this, struct nand_blk_dev, list);

		nand_remove_dev(dev);
	}
	blk_cleanup_queue(nandr->rq);
	unregister_blkdev(nandr->major, nandr->name);
}

void rknand_dev_flush(void)
{
	if (!rk_nand_dev_initialised)
		return;
	rknand_device_lock();
	rk_ftl_cache_write_back();
	rknand_device_unlock();
	pr_info("Nand flash flush ok!\n");
}

int __init rknand_dev_init(void)
{
	int ret;
	void __iomem *nandc0;
	void __iomem *nandc1;

	rknand_get_reg_addr((unsigned long *)&nandc0, (unsigned long *)&nandc1);
	if (!nandc0)
		return -1;

	ret = rk_ftl_init();
	if (ret) {
		pr_err("rk_ftl_init fail\n");
		return -1;
	}

	ret = nand_blk_register(&mytr);
	if (ret) {
		pr_err("nand_blk_register fail\n");
		return -1;
	}

	rk_nand_dev_initialised = 1;
	return ret;
}

int rknand_dev_exit(void)
{
	if (!rk_nand_dev_initialised)
		return -1;
	rk_nand_dev_initialised = 0;
	if (rknand_device_trylock()) {
		rk_ftl_cache_write_back();
		rknand_device_unlock();
	}
	nand_blk_unregister(&mytr);
	rk_ftl_de_init();
	pr_info("nand_blk_dev_exit:OK\n");
	return 0;
}

void rknand_dev_suspend(void)
{
	if (!rk_nand_dev_initialised)
		return;
	pr_info("rk_nand_suspend\n");
	rk_nand_schedule_enable_config(0);
	rknand_device_lock();
	rk_nand_suspend();
}

void rknand_dev_resume(void)
{
	if (!rk_nand_dev_initialised)
		return;
	pr_info("rk_nand_resume\n");
	rk_nand_resume();
	rknand_device_unlock();
	rk_nand_schedule_enable_config(1);
}

void rknand_dev_shutdown(void)
{
	pr_info("rknand_shutdown...\n");
	if (!rk_nand_dev_initialised)
		return;
	if (mytr.quit == 0) {
		mytr.quit = 1;
		wake_up(&mytr.thread_wq);
		wait_for_completion(&mytr.thread_exit);
		rk_ftl_de_init();
	}
	pr_info("rknand_shutdown:OK\n");
}
