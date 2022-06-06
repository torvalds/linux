/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "rk_nand: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
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
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>

#include "rk_nand_blk.h"
#include "rk_ftl_api.h"

#define PART_READONLY 0x85
#define PART_WRITEONLY 0x86
#define PART_NO_ACCESS 0x87

static unsigned long totle_read_data;
static unsigned long totle_write_data;
static unsigned long totle_read_count;
static unsigned long totle_write_count;
static int rk_nand_dev_initialised;
static unsigned long rk_ftl_gc_do;
static DECLARE_WAIT_QUEUE_HEAD(rknand_thread_wait);
static unsigned long rk_ftl_gc_jiffies;

static char *mtd_read_temp_buffer;
#define MTD_RW_SECTORS (512)

#define DISABLE_WRITE _IO('V', 0)
#define ENABLE_WRITE _IO('V', 1)
#define DISABLE_READ _IO('V', 2)
#define ENABLE_READ _IO('V', 3)
static int rknand_proc_show(struct seq_file *m, void *v)
{
	m->count = rknand_proc_ftlread(m->buf);
	seq_printf(m, "Totle Read %ld KB\n", totle_read_data >> 1);
	seq_printf(m, "Totle Write %ld KB\n", totle_write_data >> 1);
	seq_printf(m, "totle_write_count %ld\n", totle_write_count);
	seq_printf(m, "totle_read_count %ld\n", totle_read_count);
	return 0;
}

static int rknand_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rknand_proc_show, PDE_DATA(inode));
}

static const struct proc_ops rknand_proc_fops = {
	.proc_open	= rknand_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int rknand_create_procfs(void)
{
	struct proc_dir_entry *ent;

	ent = proc_create_data("rknand", 0444, NULL, &rknand_proc_fops,
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
			     int cmd)
{
	int ret;

	if (dev->disable_access ||
	    ((cmd == WRITE) && dev->readonly) ||
	    ((cmd == READ) && dev->writeonly)) {
		return BLK_STS_IOERR;
	}

	start += dev->off_size;

	switch (cmd) {
	case READ:
		totle_read_data += nsector;
		totle_read_count++;
		ret = FtlRead(0, start, nsector, buf);
		if (ret)
			ret = BLK_STS_IOERR;
		break;

	case WRITE:
		totle_write_data += nsector;
		totle_write_count++;
		ret = FtlWrite(0, start, nsector, buf);
		if (ret)
			ret = BLK_STS_IOERR;
		break;

	default:
		ret = BLK_STS_IOERR;
		break;
	}

	return ret;
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
		/* high mem return 0 and using kernel buffer */
		if (PageHighMem(bv.bv_page))
			return 0;

		buffer = page_address(bv.bv_page) + bv.bv_offset;
		if (!buffer)
			return 0;
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

static blk_status_t do_blktrans_all_request(struct nand_blk_dev *dev,
					    struct request *req)
{
	unsigned long block, nsect;
	char *buf = NULL;
	struct req_iterator rq_iter;
	struct bio_vec bvec;
	int ret = BLK_STS_IOERR;
	unsigned long totle_nsect;
	unsigned long rq_len = 0;

	block = blk_rq_pos(req);
	nsect = blk_rq_cur_bytes(req) >> 9;
	totle_nsect = (req->__data_len) >> 9;

	if (blk_rq_pos(req) + blk_rq_cur_sectors(req) >
	    get_capacity(req->rq_disk))
		return BLK_STS_IOERR;

	switch (req_op(req)) {
	case REQ_OP_DISCARD:
		if (FtlDiscard(block, nsect))
			return BLK_STS_IOERR;
		return BLK_STS_OK;
	case REQ_OP_READ:
		buf = mtd_read_temp_buffer;
		req_check_buffer_align(req, &buf);
		ret = nand_dev_transfer(dev,
				       block,
				       totle_nsect,
				       buf,
				       REQ_OP_READ);
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

		if (ret)
			return BLK_STS_IOERR;
		else
			return BLK_STS_OK;
	case REQ_OP_WRITE:
		rq_for_each_segment(bvec, req, rq_iter) {
			if ((page_address(bvec.bv_page) + bvec.bv_offset) == (buf + rq_len)) {
				rq_len += bvec.bv_len;
			} else {
				if (rq_len) {
					ret = nand_dev_transfer(dev,
							       block,
							       rq_len >> 9,
							       buf,
							       REQ_OP_WRITE);
					if (ret)
						return BLK_STS_IOERR;
					else
						return BLK_STS_OK;
				}
				block += rq_len >> 9;
				buf = (page_address(bvec.bv_page) + bvec.bv_offset);
				rq_len = bvec.bv_len;
			}
		}

		if (rq_len) {
			ret = nand_dev_transfer(dev,
					       block,
					       rq_len >> 9,
					       buf,
					       REQ_OP_WRITE);
		}

		if (ret)
			return BLK_STS_IOERR;
		else
			return BLK_STS_OK;

	default:
		return BLK_STS_IOERR;
	}
}

static struct request *rk_nand_next_request(struct nand_blk_dev *dev)
{
	struct nand_blk_ops *nand_ops = dev->nand_ops;
	struct request *rq;

	rq = list_first_entry_or_null(&nand_ops->rq_list, struct request, queuelist);
	if (rq) {
		list_del_init(&rq->queuelist);
		blk_mq_start_request(rq);
		return rq;
	}

	return NULL;
}

static void rk_nand_blktrans_work(struct nand_blk_dev *dev)
	__releases(&dev->nand_ops->queue_lock)
	__acquires(&dev->nand_ops->queue_lock)
{
	struct request *req = NULL;

	while (1) {
		blk_status_t res;

		req = rk_nand_next_request(dev);
		if (!req)
			break;

		spin_unlock_irq(&dev->nand_ops->queue_lock);

		rknand_device_lock();
		res = do_blktrans_all_request(dev, req);
		rknand_device_unlock();

		if (!blk_update_request(req, res, req->__data_len)) {
			__blk_mq_end_request(req, res);
			req = NULL;
		}

		spin_lock_irq(&dev->nand_ops->queue_lock);
	}
}

static blk_status_t rk_nand_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct nand_blk_dev *dev;

	dev = hctx->queue->queuedata;
	if (!dev) {
		blk_mq_start_request(bd->rq);
		return BLK_STS_IOERR;
	}

	rk_ftl_gc_do = 0;
	spin_lock_irq(&dev->nand_ops->queue_lock);
	list_add_tail(&bd->rq->queuelist, &dev->nand_ops->rq_list);
	rk_nand_blktrans_work(dev);
	spin_unlock_irq(&dev->nand_ops->queue_lock);

	/* wake up gc thread */
	rk_ftl_gc_do = 1;
	wake_up(&dev->nand_ops->thread_wq);

	return BLK_STS_OK;
}

static const struct blk_mq_ops rk_nand_mq_ops = {
	.queue_rq	= rk_nand_queue_rq,
};

static int nand_gc_thread(void *arg)
{
	struct nand_blk_ops *nand_ops = arg;
	int ftl_gc_status = 0;
	int req_empty_times = 0;
	int gc_done_times = 0;

	rk_ftl_gc_jiffies = HZ / 10;
	rk_ftl_gc_do = 1;

	while (!nand_ops->quit) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&nand_ops->thread_wq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		if (rk_ftl_gc_do) {
			 /* do garbage collect at idle state */
			if (rknand_device_trylock()) {
				ftl_gc_status = rk_ftl_garbage_collect(1, 0);
				rknand_device_unlock();
				rk_ftl_gc_jiffies = HZ / 50;
				if (ftl_gc_status == 0) {
					gc_done_times++;
					if (gc_done_times > 10)
						rk_ftl_gc_jiffies = 10 * HZ;
					else
						rk_ftl_gc_jiffies = 1 * HZ;
				} else {
					gc_done_times = 0;
				}
			} else {
				rk_ftl_gc_jiffies = 1 * HZ;
			}
			req_empty_times++;
			if (req_empty_times < 10)
				rk_ftl_gc_jiffies = HZ / 50;
			/* cache write back after 100ms */
			if (req_empty_times >= 5 && req_empty_times < 7) {
				rknand_device_lock();
				rk_ftl_cache_write_back();
				rknand_device_unlock();
			}
		} else {
			req_empty_times = 0;
			rk_ftl_gc_jiffies = 1 * HZ;
		}
		wait_event_timeout(nand_ops->thread_wq, nand_ops->quit,
				   rk_ftl_gc_jiffies);
		remove_wait_queue(&nand_ops->thread_wq, &wait);
		continue;
	}
	pr_info("nand gc quited\n");
	nand_ops->nand_th_quited = 1;
	complete_and_exit(&nand_ops->thread_exit, 0);
	return 0;
}

static int rknand_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void rknand_release(struct gendisk *disk, fmode_t mode)
{
};

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

static int nand_add_dev(struct nand_blk_ops *nand_ops, struct nand_part *part)
{
	struct nand_blk_dev *dev;
	struct gendisk *gd;

	if (part->size == 0)
		return -1;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	gd = alloc_disk(1 << nand_ops->minorbits);
	if (!gd) {
		kfree(dev);
		return -ENOMEM;
	}
	nand_ops->rq->queuedata = dev;
	dev->nand_ops = nand_ops;
	dev->size = part->size;
	dev->off_size = part->offset;
	dev->devnum = nand_ops->last_dev_index;
	list_add_tail(&dev->list, &nand_ops->devs);
	nand_ops->last_dev_index++;

	gd->major = nand_ops->major;
	gd->first_minor = (dev->devnum) << nand_ops->minorbits;

	gd->fops = &nand_blktrans_ops;

	gd->flags = GENHD_FL_EXT_DEVT;
	gd->minors = 255;
	snprintf(gd->disk_name,
		 sizeof(gd->disk_name),
		 "%s%d",
		 nand_ops->name,
		 dev->devnum);

	set_capacity(gd, dev->size);

	gd->private_data = dev;
	dev->blkcore_priv = gd;
	gd->queue = nand_ops->rq;

	if (part->type == PART_NO_ACCESS)
		dev->disable_access = 1;

	if (part->type == PART_READONLY)
		dev->readonly = 1;

	if (part->type == PART_WRITEONLY)
		dev->writeonly = 1;

	if (dev->readonly)
		set_disk_ro(gd, 1);

	device_add_disk(g_nand_device, gd, NULL);

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

static int nand_blk_register(struct nand_blk_ops *nand_ops)
{
	struct nand_part part;
	int ret;

	rk_nand_schedule_enable_config(1);
	nand_ops->quit = 0;
	nand_ops->nand_th_quited = 0;

	ret = register_blkdev(nand_ops->major, nand_ops->name);
	if (ret)
		return ret;

	mtd_read_temp_buffer = kmalloc(MTD_RW_SECTORS * 512, GFP_KERNEL | GFP_DMA);
	if (!mtd_read_temp_buffer) {
		ret = -ENOMEM;
		goto mtd_buffer_error;
	}

	init_completion(&nand_ops->thread_exit);
	init_waitqueue_head(&nand_ops->thread_wq);
	rknand_device_lock_init();

	/* Create the request queue */
	spin_lock_init(&nand_ops->queue_lock);
	INIT_LIST_HEAD(&nand_ops->rq_list);

	nand_ops->tag_set = kzalloc(sizeof(*nand_ops->tag_set), GFP_KERNEL);
	if (!nand_ops->tag_set) {
		ret = -ENOMEM;
		goto tag_set_error;
	}

	nand_ops->rq = blk_mq_init_sq_queue(nand_ops->tag_set, &rk_nand_mq_ops, 1,
					   BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING);
	if (IS_ERR(nand_ops->rq)) {
		ret = PTR_ERR(nand_ops->rq);
		nand_ops->rq = NULL;
		goto rq_init_error;
	}

	blk_queue_max_hw_sectors(nand_ops->rq, MTD_RW_SECTORS);
	blk_queue_max_segments(nand_ops->rq, MTD_RW_SECTORS);

	blk_queue_flag_set(QUEUE_FLAG_DISCARD, nand_ops->rq);
	blk_queue_max_discard_sectors(nand_ops->rq, UINT_MAX >> 9);
	/* discard_granularity config to one nand page size 32KB*/
	nand_ops->rq->limits.discard_granularity = 64 << 9;

	INIT_LIST_HEAD(&nand_ops->devs);
	kthread_run(nand_gc_thread, (void *)nand_ops, "rknand_gc");

	nand_ops->last_dev_index = 0;
	part.offset = 0;
	part.size = rk_ftl_get_capacity();
	part.type = 0;
	part.name[0] = 0;
	nand_add_dev(nand_ops, &part);

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

rq_init_error:
	kfree(nand_ops->tag_set);
tag_set_error:
	kfree(mtd_read_temp_buffer);
	mtd_read_temp_buffer = NULL;
mtd_buffer_error:
	unregister_blkdev(nand_ops->major, nand_ops->name);

	return ret;
}

static void nand_blk_unregister(struct nand_blk_ops *nand_ops)
{
	struct list_head *this, *next;

	if (!rk_nand_dev_initialised)
		return;
	nand_ops->quit = 1;
	wake_up(&nand_ops->thread_wq);
	wait_for_completion(&nand_ops->thread_exit);
	list_for_each_safe(this, next, &nand_ops->devs) {
		struct nand_blk_dev *dev
			= list_entry(this, struct nand_blk_dev, list);

		nand_remove_dev(dev);
	}
	blk_cleanup_queue(nand_ops->rq);
	unregister_blkdev(nand_ops->major, nand_ops->name);
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
