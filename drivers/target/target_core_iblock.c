/*******************************************************************************
 * Filename:  target_core_iblock.c
 *
 * This file contains the Storage Engine  <-> Linux BlockIO transport
 * specific functions.
 *
 * Copyright (c) 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/version.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/bio.h>
#include <linux/genhd.h>
#include <linux/file.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_transport.h>

#include "target_core_iblock.h"

#if 0
#define DEBUG_IBLOCK(x...) printk(x)
#else
#define DEBUG_IBLOCK(x...)
#endif

static struct se_subsystem_api iblock_template;

static void iblock_bio_done(struct bio *, int);

/*	iblock_attach_hba(): (Part of se_subsystem_api_t template)
 *
 *
 */
static int iblock_attach_hba(struct se_hba *hba, u32 host_id)
{
	struct iblock_hba *ib_host;

	ib_host = kzalloc(sizeof(struct iblock_hba), GFP_KERNEL);
	if (!(ib_host)) {
		printk(KERN_ERR "Unable to allocate memory for"
				" struct iblock_hba\n");
		return -ENOMEM;
	}

	ib_host->iblock_host_id = host_id;

	atomic_set(&hba->left_queue_depth, IBLOCK_HBA_QUEUE_DEPTH);
	atomic_set(&hba->max_queue_depth, IBLOCK_HBA_QUEUE_DEPTH);
	hba->hba_ptr = (void *) ib_host;

	printk(KERN_INFO "CORE_HBA[%d] - TCM iBlock HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		IBLOCK_VERSION, TARGET_CORE_MOD_VERSION);

	printk(KERN_INFO "CORE_HBA[%d] - Attached iBlock HBA: %u to Generic"
		" Target Core TCQ Depth: %d\n", hba->hba_id,
		ib_host->iblock_host_id, atomic_read(&hba->max_queue_depth));

	return 0;
}

static void iblock_detach_hba(struct se_hba *hba)
{
	struct iblock_hba *ib_host = hba->hba_ptr;

	printk(KERN_INFO "CORE_HBA[%d] - Detached iBlock HBA: %u from Generic"
		" Target Core\n", hba->hba_id, ib_host->iblock_host_id);

	kfree(ib_host);
	hba->hba_ptr = NULL;
}

static void *iblock_allocate_virtdevice(struct se_hba *hba, const char *name)
{
	struct iblock_dev *ib_dev = NULL;
	struct iblock_hba *ib_host = hba->hba_ptr;

	ib_dev = kzalloc(sizeof(struct iblock_dev), GFP_KERNEL);
	if (!(ib_dev)) {
		printk(KERN_ERR "Unable to allocate struct iblock_dev\n");
		return NULL;
	}
	ib_dev->ibd_host = ib_host;

	printk(KERN_INFO  "IBLOCK: Allocated ib_dev for %s\n", name);

	return ib_dev;
}

static struct se_device *iblock_create_virtdevice(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	void *p)
{
	struct iblock_dev *ib_dev = p;
	struct se_device *dev;
	struct se_dev_limits dev_limits;
	struct block_device *bd = NULL;
	struct request_queue *q;
	struct queue_limits *limits;
	u32 dev_flags = 0;

	if (!(ib_dev)) {
		printk(KERN_ERR "Unable to locate struct iblock_dev parameter\n");
		return 0;
	}
	memset(&dev_limits, 0, sizeof(struct se_dev_limits));
	/*
	 * These settings need to be made tunable..
	 */
	ib_dev->ibd_bio_set = bioset_create(32, 64);
	if (!(ib_dev->ibd_bio_set)) {
		printk(KERN_ERR "IBLOCK: Unable to create bioset()\n");
		return 0;
	}
	printk(KERN_INFO "IBLOCK: Created bio_set()\n");
	/*
	 * iblock_check_configfs_dev_params() ensures that ib_dev->ibd_udev_path
	 * must already have been set in order for echo 1 > $HBA/$DEV/enable to run.
	 */
	printk(KERN_INFO  "IBLOCK: Claiming struct block_device: %s\n",
			ib_dev->ibd_udev_path);

	bd = blkdev_get_by_path(ib_dev->ibd_udev_path,
				FMODE_WRITE|FMODE_READ|FMODE_EXCL, ib_dev);
	if (IS_ERR(bd))
		goto failed;
	/*
	 * Setup the local scope queue_limits from struct request_queue->limits
	 * to pass into transport_add_device_to_core_hba() as struct se_dev_limits.
	 */
	q = bdev_get_queue(bd);
	limits = &dev_limits.limits;
	limits->logical_block_size = bdev_logical_block_size(bd);
	limits->max_hw_sectors = queue_max_hw_sectors(q);
	limits->max_sectors = queue_max_sectors(q);
	dev_limits.hw_queue_depth = IBLOCK_MAX_DEVICE_QUEUE_DEPTH;
	dev_limits.queue_depth = IBLOCK_DEVICE_QUEUE_DEPTH;

	ib_dev->ibd_major = MAJOR(bd->bd_dev);
	ib_dev->ibd_minor = MINOR(bd->bd_dev);
	ib_dev->ibd_bd = bd;

	dev = transport_add_device_to_core_hba(hba,
			&iblock_template, se_dev, dev_flags, (void *)ib_dev,
			&dev_limits, "IBLOCK", IBLOCK_VERSION);
	if (!(dev))
		goto failed;

	ib_dev->ibd_depth = dev->queue_depth;

	/*
	 * Check if the underlying struct block_device request_queue supports
	 * the QUEUE_FLAG_DISCARD bit for UNMAP/WRITE_SAME in SCSI + TRIM
	 * in ATA and we need to set TPE=1
	 */
	if (blk_queue_discard(bdev_get_queue(bd))) {
		struct request_queue *q = bdev_get_queue(bd);

		DEV_ATTRIB(dev)->max_unmap_lba_count =
				q->limits.max_discard_sectors;
		/*
		 * Currently hardcoded to 1 in Linux/SCSI code..
		 */
		DEV_ATTRIB(dev)->max_unmap_block_desc_count = 1;
		DEV_ATTRIB(dev)->unmap_granularity =
				q->limits.discard_granularity;
		DEV_ATTRIB(dev)->unmap_granularity_alignment =
				q->limits.discard_alignment;

		printk(KERN_INFO "IBLOCK: BLOCK Discard support available,"
				" disabled by default\n");
	}

	return dev;

failed:
	if (ib_dev->ibd_bio_set) {
		bioset_free(ib_dev->ibd_bio_set);
		ib_dev->ibd_bio_set = NULL;
	}
	ib_dev->ibd_bd = NULL;
	ib_dev->ibd_major = 0;
	ib_dev->ibd_minor = 0;
	return NULL;
}

static void iblock_free_device(void *p)
{
	struct iblock_dev *ib_dev = p;

	if (ib_dev->ibd_bd != NULL)
		blkdev_put(ib_dev->ibd_bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);
	if (ib_dev->ibd_bio_set != NULL)
		bioset_free(ib_dev->ibd_bio_set);
	kfree(ib_dev);
}

static inline struct iblock_req *IBLOCK_REQ(struct se_task *task)
{
	return container_of(task, struct iblock_req, ib_task);
}

static struct se_task *
iblock_alloc_task(struct se_cmd *cmd)
{
	struct iblock_req *ib_req;

	ib_req = kzalloc(sizeof(struct iblock_req), GFP_KERNEL);
	if (!(ib_req)) {
		printk(KERN_ERR "Unable to allocate memory for struct iblock_req\n");
		return NULL;
	}

	ib_req->ib_dev = SE_DEV(cmd)->dev_ptr;
	atomic_set(&ib_req->ib_bio_cnt, 0);
	return &ib_req->ib_task;
}

static unsigned long long iblock_emulate_read_cap_with_block_size(
	struct se_device *dev,
	struct block_device *bd,
	struct request_queue *q)
{
	unsigned long long blocks_long = (div_u64(i_size_read(bd->bd_inode),
					bdev_logical_block_size(bd)) - 1);
	u32 block_size = bdev_logical_block_size(bd);

	if (block_size == DEV_ATTRIB(dev)->block_size)
		return blocks_long;

	switch (block_size) {
	case 4096:
		switch (DEV_ATTRIB(dev)->block_size) {
		case 2048:
			blocks_long <<= 1;
			break;
		case 1024:
			blocks_long <<= 2;
			break;
		case 512:
			blocks_long <<= 3;
		default:
			break;
		}
		break;
	case 2048:
		switch (DEV_ATTRIB(dev)->block_size) {
		case 4096:
			blocks_long >>= 1;
			break;
		case 1024:
			blocks_long <<= 1;
			break;
		case 512:
			blocks_long <<= 2;
			break;
		default:
			break;
		}
		break;
	case 1024:
		switch (DEV_ATTRIB(dev)->block_size) {
		case 4096:
			blocks_long >>= 2;
			break;
		case 2048:
			blocks_long >>= 1;
			break;
		case 512:
			blocks_long <<= 1;
			break;
		default:
			break;
		}
		break;
	case 512:
		switch (DEV_ATTRIB(dev)->block_size) {
		case 4096:
			blocks_long >>= 3;
			break;
		case 2048:
			blocks_long >>= 2;
			break;
		case 1024:
			blocks_long >>= 1;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return blocks_long;
}

/*
 * Emulate SYCHRONIZE_CACHE_*
 */
static void iblock_emulate_sync_cache(struct se_task *task)
{
	struct se_cmd *cmd = TASK_CMD(task);
	struct iblock_dev *ib_dev = cmd->se_dev->dev_ptr;
	int immed = (T_TASK(cmd)->t_task_cdb[1] & 0x2);
	sector_t error_sector;
	int ret;

	/*
	 * If the Immediate bit is set, queue up the GOOD response
	 * for this SYNCHRONIZE_CACHE op
	 */
	if (immed)
		transport_complete_sync_cache(cmd, 1);

	/*
	 * blkdev_issue_flush() does not support a specifying a range, so
	 * we have to flush the entire cache.
	 */
	ret = blkdev_issue_flush(ib_dev->ibd_bd, GFP_KERNEL, &error_sector);
	if (ret != 0) {
		printk(KERN_ERR "IBLOCK: block_issue_flush() failed: %d "
			" error_sector: %llu\n", ret,
			(unsigned long long)error_sector);
	}

	if (!immed)
		transport_complete_sync_cache(cmd, ret == 0);
}

/*
 * Tell TCM Core that we are capable of WriteCache emulation for
 * an underlying struct se_device.
 */
static int iblock_emulated_write_cache(struct se_device *dev)
{
	return 1;
}

static int iblock_emulated_dpo(struct se_device *dev)
{
	return 0;
}

/*
 * Tell TCM Core that we will be emulating Forced Unit Access (FUA) for WRITEs
 * for TYPE_DISK.
 */
static int iblock_emulated_fua_write(struct se_device *dev)
{
	return 1;
}

static int iblock_emulated_fua_read(struct se_device *dev)
{
	return 0;
}

static int iblock_do_task(struct se_task *task)
{
	struct se_device *dev = task->task_se_cmd->se_dev;
	struct iblock_req *req = IBLOCK_REQ(task);
	struct iblock_dev *ibd = (struct iblock_dev *)req->ib_dev;
	struct request_queue *q = bdev_get_queue(ibd->ibd_bd);
	struct bio *bio = req->ib_bio, *nbio = NULL;
	int rw;

	if (task->task_data_direction == DMA_TO_DEVICE) {
		/*
		 * Force data to disk if we pretend to not have a volatile
		 * write cache, or the initiator set the Force Unit Access bit.
		 */
		if (DEV_ATTRIB(dev)->emulate_write_cache == 0 ||
		    (DEV_ATTRIB(dev)->emulate_fua_write > 0 &&
		     T_TASK(task->task_se_cmd)->t_tasks_fua))
			rw = WRITE_FUA;
		else
			rw = WRITE;
	} else {
		rw = READ;
	}

	while (bio) {
		nbio = bio->bi_next;
		bio->bi_next = NULL;
		DEBUG_IBLOCK("Calling submit_bio() task: %p bio: %p"
			" bio->bi_sector: %llu\n", task, bio, bio->bi_sector);

		submit_bio(rw, bio);
		bio = nbio;
	}

	if (q->unplug_fn)
		q->unplug_fn(q);
	return PYX_TRANSPORT_SENT_TO_TRANSPORT;
}

static int iblock_do_discard(struct se_device *dev, sector_t lba, u32 range)
{
	struct iblock_dev *ibd = dev->dev_ptr;
	struct block_device *bd = ibd->ibd_bd;
	int barrier = 0;

	return blkdev_issue_discard(bd, lba, range, GFP_KERNEL, barrier);
}

static void iblock_free_task(struct se_task *task)
{
	struct iblock_req *req = IBLOCK_REQ(task);
	struct bio *bio, *hbio = req->ib_bio;
	/*
	 * We only release the bio(s) here if iblock_bio_done() has not called
	 * bio_put() -> iblock_bio_destructor().
	 */
	while (hbio != NULL) {
		bio = hbio;
		hbio = hbio->bi_next;
		bio->bi_next = NULL;
		bio_put(bio);
	}

	kfree(req);
}

enum {
	Opt_udev_path, Opt_force, Opt_err
};

static match_table_t tokens = {
	{Opt_udev_path, "udev_path=%s"},
	{Opt_force, "force=%d"},
	{Opt_err, NULL}
};

static ssize_t iblock_set_configfs_dev_params(struct se_hba *hba,
					       struct se_subsystem_dev *se_dev,
					       const char *page, ssize_t count)
{
	struct iblock_dev *ib_dev = se_dev->se_dev_su_ptr;
	char *orig, *ptr, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, arg, token;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_udev_path:
			if (ib_dev->ibd_bd) {
				printk(KERN_ERR "Unable to set udev_path= while"
					" ib_dev->ibd_bd exists\n");
				ret = -EEXIST;
				goto out;
			}

			ret = snprintf(ib_dev->ibd_udev_path, SE_UDEV_PATH_LEN,
				"%s", match_strdup(&args[0]));
			printk(KERN_INFO "IBLOCK: Referencing UDEV path: %s\n",
					ib_dev->ibd_udev_path);
			ib_dev->ibd_flags |= IBDF_HAS_UDEV_PATH;
			break;
		case Opt_force:
			match_int(args, &arg);
			ib_dev->ibd_force = arg;
			printk(KERN_INFO "IBLOCK: Set force=%d\n",
				ib_dev->ibd_force);
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t iblock_check_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev)
{
	struct iblock_dev *ibd = se_dev->se_dev_su_ptr;

	if (!(ibd->ibd_flags & IBDF_HAS_UDEV_PATH)) {
		printk(KERN_ERR "Missing udev_path= parameters for IBLOCK\n");
		return -1;
	}

	return 0;
}

static ssize_t iblock_show_configfs_dev_params(
	struct se_hba *hba,
	struct se_subsystem_dev *se_dev,
	char *b)
{
	struct iblock_dev *ibd = se_dev->se_dev_su_ptr;
	struct block_device *bd = ibd->ibd_bd;
	char buf[BDEVNAME_SIZE];
	ssize_t bl = 0;

	if (bd)
		bl += sprintf(b + bl, "iBlock device: %s",
				bdevname(bd, buf));
	if (ibd->ibd_flags & IBDF_HAS_UDEV_PATH) {
		bl += sprintf(b + bl, "  UDEV PATH: %s\n",
				ibd->ibd_udev_path);
	} else
		bl += sprintf(b + bl, "\n");

	bl += sprintf(b + bl, "        ");
	if (bd) {
		bl += sprintf(b + bl, "Major: %d Minor: %d  %s\n",
			ibd->ibd_major, ibd->ibd_minor, (!bd->bd_contains) ?
			"" : (bd->bd_holder == (struct iblock_dev *)ibd) ?
			"CLAIMED: IBLOCK" : "CLAIMED: OS");
	} else {
		bl += sprintf(b + bl, "Major: %d Minor: %d\n",
			ibd->ibd_major, ibd->ibd_minor);
	}

	return bl;
}

static void iblock_bio_destructor(struct bio *bio)
{
	struct se_task *task = bio->bi_private;
	struct iblock_dev *ib_dev = task->se_dev->dev_ptr;

	bio_free(bio, ib_dev->ibd_bio_set);
}

static struct bio *iblock_get_bio(
	struct se_task *task,
	struct iblock_req *ib_req,
	struct iblock_dev *ib_dev,
	int *ret,
	sector_t lba,
	u32 sg_num)
{
	struct bio *bio;

	bio = bio_alloc_bioset(GFP_NOIO, sg_num, ib_dev->ibd_bio_set);
	if (!(bio)) {
		printk(KERN_ERR "Unable to allocate memory for bio\n");
		*ret = PYX_TRANSPORT_OUT_OF_MEMORY_RESOURCES;
		return NULL;
	}

	DEBUG_IBLOCK("Allocated bio: %p task_sg_num: %u using ibd_bio_set:"
		" %p\n", bio, task->task_sg_num, ib_dev->ibd_bio_set);
	DEBUG_IBLOCK("Allocated bio: %p task_size: %u\n", bio, task->task_size);

	bio->bi_bdev = ib_dev->ibd_bd;
	bio->bi_private = (void *) task;
	bio->bi_destructor = iblock_bio_destructor;
	bio->bi_end_io = &iblock_bio_done;
	bio->bi_sector = lba;
	atomic_inc(&ib_req->ib_bio_cnt);

	DEBUG_IBLOCK("Set bio->bi_sector: %llu\n", bio->bi_sector);
	DEBUG_IBLOCK("Set ib_req->ib_bio_cnt: %d\n",
			atomic_read(&ib_req->ib_bio_cnt));
	return bio;
}

static int iblock_map_task_SG(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = SE_DEV(cmd);
	struct iblock_dev *ib_dev = task->se_dev->dev_ptr;
	struct iblock_req *ib_req = IBLOCK_REQ(task);
	struct bio *bio = NULL, *hbio = NULL, *tbio = NULL;
	struct scatterlist *sg;
	int ret = 0;
	u32 i, sg_num = task->task_sg_num;
	sector_t block_lba;
	/*
	 * Do starting conversion up from non 512-byte blocksize with
	 * struct se_task SCSI blocksize into Linux/Block 512 units for BIO.
	 */
	if (DEV_ATTRIB(dev)->block_size == 4096)
		block_lba = (task->task_lba << 3);
	else if (DEV_ATTRIB(dev)->block_size == 2048)
		block_lba = (task->task_lba << 2);
	else if (DEV_ATTRIB(dev)->block_size == 1024)
		block_lba = (task->task_lba << 1);
	else if (DEV_ATTRIB(dev)->block_size == 512)
		block_lba = task->task_lba;
	else {
		printk(KERN_ERR "Unsupported SCSI -> BLOCK LBA conversion:"
				" %u\n", DEV_ATTRIB(dev)->block_size);
		return PYX_TRANSPORT_LU_COMM_FAILURE;
	}

	bio = iblock_get_bio(task, ib_req, ib_dev, &ret, block_lba, sg_num);
	if (!(bio))
		return ret;

	ib_req->ib_bio = bio;
	hbio = tbio = bio;
	/*
	 * Use fs/bio.c:bio_add_pages() to setup the bio_vec maplist
	 * from TCM struct se_mem -> task->task_sg -> struct scatterlist memory.
	 */
	for_each_sg(task->task_sg, sg, task->task_sg_num, i) {
		DEBUG_IBLOCK("task: %p bio: %p Calling bio_add_page(): page:"
			" %p len: %u offset: %u\n", task, bio, sg_page(sg),
				sg->length, sg->offset);
again:
		ret = bio_add_page(bio, sg_page(sg), sg->length, sg->offset);
		if (ret != sg->length) {

			DEBUG_IBLOCK("*** Set bio->bi_sector: %llu\n",
					bio->bi_sector);
			DEBUG_IBLOCK("** task->task_size: %u\n",
					task->task_size);
			DEBUG_IBLOCK("*** bio->bi_max_vecs: %u\n",
					bio->bi_max_vecs);
			DEBUG_IBLOCK("*** bio->bi_vcnt: %u\n",
					bio->bi_vcnt);

			bio = iblock_get_bio(task, ib_req, ib_dev, &ret,
						block_lba, sg_num);
			if (!(bio))
				goto fail;

			tbio = tbio->bi_next = bio;
			DEBUG_IBLOCK("-----------------> Added +1 bio: %p to"
				" list, Going to again\n", bio);
			goto again;
		}
		/* Always in 512 byte units for Linux/Block */
		block_lba += sg->length >> IBLOCK_LBA_SHIFT;
		sg_num--;
		DEBUG_IBLOCK("task: %p bio-add_page() passed!, decremented"
			" sg_num to %u\n", task, sg_num);
		DEBUG_IBLOCK("task: %p bio_add_page() passed!, increased lba"
				" to %llu\n", task, block_lba);
		DEBUG_IBLOCK("task: %p bio_add_page() passed!, bio->bi_vcnt:"
				" %u\n", task, bio->bi_vcnt);
	}

	return 0;
fail:
	while (hbio) {
		bio = hbio;
		hbio = hbio->bi_next;
		bio->bi_next = NULL;
		bio_put(bio);
	}
	return ret;
}

static unsigned char *iblock_get_cdb(struct se_task *task)
{
	return IBLOCK_REQ(task)->ib_scsi_cdb;
}

static u32 iblock_get_device_rev(struct se_device *dev)
{
	return SCSI_SPC_2; /* Returns SPC-3 in Initiator Data */
}

static u32 iblock_get_device_type(struct se_device *dev)
{
	return TYPE_DISK;
}

static sector_t iblock_get_blocks(struct se_device *dev)
{
	struct iblock_dev *ibd = dev->dev_ptr;
	struct block_device *bd = ibd->ibd_bd;
	struct request_queue *q = bdev_get_queue(bd);

	return iblock_emulate_read_cap_with_block_size(dev, bd, q);
}

static void iblock_bio_done(struct bio *bio, int err)
{
	struct se_task *task = bio->bi_private;
	struct iblock_req *ibr = IBLOCK_REQ(task);
	/*
	 * Set -EIO if !BIO_UPTODATE and the passed is still err=0
	 */
	if (!(test_bit(BIO_UPTODATE, &bio->bi_flags)) && !(err))
		err = -EIO;

	if (err != 0) {
		printk(KERN_ERR "test_bit(BIO_UPTODATE) failed for bio: %p,"
			" err: %d\n", bio, err);
		/*
		 * Bump the ib_bio_err_cnt and release bio.
		 */
		atomic_inc(&ibr->ib_bio_err_cnt);
		smp_mb__after_atomic_inc();
		bio_put(bio);
		/*
		 * Wait to complete the task until the last bio as completed.
		 */
		if (!(atomic_dec_and_test(&ibr->ib_bio_cnt)))
			return;

		ibr->ib_bio = NULL;
		transport_complete_task(task, 0);
		return;
	}
	DEBUG_IBLOCK("done[%p] bio: %p task_lba: %llu bio_lba: %llu err=%d\n",
		task, bio, task->task_lba, bio->bi_sector, err);
	/*
	 * bio_put() will call iblock_bio_destructor() to release the bio back
	 * to ibr->ib_bio_set.
	 */
	bio_put(bio);
	/*
	 * Wait to complete the task until the last bio as completed.
	 */
	if (!(atomic_dec_and_test(&ibr->ib_bio_cnt)))
		return;
	/*
	 * Return GOOD status for task if zero ib_bio_err_cnt exists.
	 */
	ibr->ib_bio = NULL;
	transport_complete_task(task, (!atomic_read(&ibr->ib_bio_err_cnt)));
}

static struct se_subsystem_api iblock_template = {
	.name			= "iblock",
	.owner			= THIS_MODULE,
	.transport_type		= TRANSPORT_PLUGIN_VHBA_PDEV,
	.map_task_SG		= iblock_map_task_SG,
	.attach_hba		= iblock_attach_hba,
	.detach_hba		= iblock_detach_hba,
	.allocate_virtdevice	= iblock_allocate_virtdevice,
	.create_virtdevice	= iblock_create_virtdevice,
	.free_device		= iblock_free_device,
	.dpo_emulated		= iblock_emulated_dpo,
	.fua_write_emulated	= iblock_emulated_fua_write,
	.fua_read_emulated	= iblock_emulated_fua_read,
	.write_cache_emulated	= iblock_emulated_write_cache,
	.alloc_task		= iblock_alloc_task,
	.do_task		= iblock_do_task,
	.do_discard		= iblock_do_discard,
	.do_sync_cache		= iblock_emulate_sync_cache,
	.free_task		= iblock_free_task,
	.check_configfs_dev_params = iblock_check_configfs_dev_params,
	.set_configfs_dev_params = iblock_set_configfs_dev_params,
	.show_configfs_dev_params = iblock_show_configfs_dev_params,
	.get_cdb		= iblock_get_cdb,
	.get_device_rev		= iblock_get_device_rev,
	.get_device_type	= iblock_get_device_type,
	.get_blocks		= iblock_get_blocks,
};

static int __init iblock_module_init(void)
{
	return transport_subsystem_register(&iblock_template);
}

static void iblock_module_exit(void)
{
	transport_subsystem_release(&iblock_template);
}

MODULE_DESCRIPTION("TCM IBLOCK subsystem plugin");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(iblock_module_init);
module_exit(iblock_module_exit);
