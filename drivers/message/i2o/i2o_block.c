/*
 *	Block OSM
 *
 * 	Copyright (C) 1999-2002	Red Hat Software
 *
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 *
 *	For the purpose of avoiding doubt the preferred form of the work
 *	for making modifications shall be a standards compliant form such
 *	gzipped tar and not one requiring a proprietary or patent encumbered
 *	tool to unpack.
 *
 *	Fixes/additions:
 *		Steve Ralston:
 *			Multiple device handling error fixes,
 *			Added a queue depth.
 *		Alan Cox:
 *			FC920 has an rmw bug. Dont or in the end marker.
 *			Removed queue walk, fixed for 64bitness.
 *			Rewrote much of the code over time
 *			Added indirect block lists
 *			Handle 64K limits on many controllers
 *			Don't use indirects on the Promise (breaks)
 *			Heavily chop down the queue depths
 *		Deepak Saxena:
 *			Independent queues per IOP
 *			Support for dynamic device creation/deletion
 *			Code cleanup
 *	    		Support for larger I/Os through merge* functions
 *			(taken from DAC960 driver)
 *		Boji T Kannanthanam:
 *			Set the I2O Block devices to be detected in increasing
 *			order of TIDs during boot.
 *			Search and set the I2O block device that we boot off
 *			from as the first device to be claimed (as /dev/i2o/hda)
 *			Properly attach/detach I2O gendisk structure from the
 *			system gendisk list. The I2O block devices now appear in
 *			/proc/partitions.
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *			Minor bugfixes for 2.6.
 */

#include <linux/module.h>
#include <linux/i2o.h>

#include <linux/mempool.h>

#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include "i2o_block.h"

#define OSM_NAME	"block-osm"
#define OSM_VERSION	"$Rev$"
#define OSM_DESCRIPTION	"I2O Block Device OSM"

static struct i2o_driver i2o_block_driver;

/* global Block OSM request mempool */
static struct i2o_block_mempool i2o_blk_req_pool;

/* Block OSM class handling definition */
static struct i2o_class_id i2o_block_class_id[] = {
	{I2O_CLASS_RANDOM_BLOCK_STORAGE},
	{I2O_CLASS_END}
};

/**
 *	i2o_block_device_free - free the memory of the I2O Block device
 *	@dev: I2O Block device, which should be cleaned up
 *
 *	Frees the request queue, gendisk and the i2o_block_device structure.
 */
static void i2o_block_device_free(struct i2o_block_device *dev)
{
	blk_cleanup_queue(dev->gd->queue);

	put_disk(dev->gd);

	kfree(dev);
};

/**
 *	i2o_block_remove - remove the I2O Block device from the system again
 *	@dev: I2O Block device which should be removed
 *
 *	Remove gendisk from system and free all allocated memory.
 *
 *	Always returns 0.
 */
static int i2o_block_remove(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_block_device *i2o_blk_dev = dev_get_drvdata(dev);

	osm_info("Device removed %s\n", i2o_blk_dev->gd->disk_name);

	i2o_event_register(i2o_dev, &i2o_block_driver, 0, 0);

	del_gendisk(i2o_blk_dev->gd);

	dev_set_drvdata(dev, NULL);

	i2o_device_claim_release(i2o_dev);

	i2o_block_device_free(i2o_blk_dev);

	return 0;
};

/**
 *	i2o_block_device flush - Flush all dirty data of I2O device dev
 *	@dev: I2O device which should be flushed
 *
 *	Flushes all dirty data on device dev.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_device_flush(struct i2o_device *dev)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BLOCK_CFLUSH << 24 | HOST_TID << 12 | dev->lct_data.tid,
	       &msg->u.head[1]);
	writel(60 << 16, &msg->body[0]);
	osm_debug("Flushing...\n");

	return i2o_msg_post_wait(dev->iop, m, 60);
};

/**
 *	i2o_block_device_mount - Mount (load) the media of device dev
 *	@dev: I2O device which should receive the mount request
 *	@media_id: Media Identifier
 *
 *	Load a media into drive. Identifier should be set to -1, because the
 *	spec does not support any other value.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_device_mount(struct i2o_device *dev, u32 media_id)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BLOCK_MMOUNT << 24 | HOST_TID << 12 | dev->lct_data.tid,
	       &msg->u.head[1]);
	writel(-1, &msg->body[0]);
	writel(0, &msg->body[1]);
	osm_debug("Mounting...\n");

	return i2o_msg_post_wait(dev->iop, m, 2);
};

/**
 *	i2o_block_device_lock - Locks the media of device dev
 *	@dev: I2O device which should receive the lock request
 *	@media_id: Media Identifier
 *
 *	Lock media of device dev to prevent removal. The media identifier
 *	should be set to -1, because the spec does not support any other value.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_device_lock(struct i2o_device *dev, u32 media_id)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BLOCK_MLOCK << 24 | HOST_TID << 12 | dev->lct_data.tid,
	       &msg->u.head[1]);
	writel(-1, &msg->body[0]);
	osm_debug("Locking...\n");

	return i2o_msg_post_wait(dev->iop, m, 2);
};

/**
 *	i2o_block_device_unlock - Unlocks the media of device dev
 *	@dev: I2O device which should receive the unlocked request
 *	@media_id: Media Identifier
 *
 *	Unlocks the media in device dev. The media identifier should be set to
 *	-1, because the spec does not support any other value.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_device_unlock(struct i2o_device *dev, u32 media_id)
{
	struct i2o_message __iomem *msg;
	u32 m;

	m = i2o_msg_get_wait(dev->iop, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FIVE_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BLOCK_MUNLOCK << 24 | HOST_TID << 12 | dev->lct_data.tid,
	       &msg->u.head[1]);
	writel(media_id, &msg->body[0]);
	osm_debug("Unlocking...\n");

	return i2o_msg_post_wait(dev->iop, m, 2);
};

/**
 *	i2o_block_device_power - Power management for device dev
 *	@dev: I2O device which should receive the power management request
 *	@operation: Operation which should be send
 *
 *	Send a power management request to the device dev.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_device_power(struct i2o_block_device *dev, u8 op)
{
	struct i2o_device *i2o_dev = dev->i2o_dev;
	struct i2o_controller *c = i2o_dev->iop;
	struct i2o_message __iomem *msg;
	u32 m;
	int rc;

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return -ETIMEDOUT;

	writel(FOUR_WORD_MSG_SIZE | SGL_OFFSET_0, &msg->u.head[0]);
	writel(I2O_CMD_BLOCK_POWER << 24 | HOST_TID << 12 | i2o_dev->lct_data.
	       tid, &msg->u.head[1]);
	writel(op << 24, &msg->body[0]);
	osm_debug("Power...\n");

	rc = i2o_msg_post_wait(c, m, 60);
	if (!rc)
		dev->power = op;

	return rc;
};

/**
 *	i2o_block_request_alloc - Allocate an I2O block request struct
 *
 *	Allocates an I2O block request struct and initialize the list.
 *
 *	Returns a i2o_block_request pointer on success or negative error code
 *	on failure.
 */
static inline struct i2o_block_request *i2o_block_request_alloc(void)
{
	struct i2o_block_request *ireq;

	ireq = mempool_alloc(i2o_blk_req_pool.pool, GFP_ATOMIC);
	if (!ireq)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ireq->queue);

	return ireq;
};

/**
 *	i2o_block_request_free - Frees a I2O block request
 *	@ireq: I2O block request which should be freed
 *
 *	Fres the allocated memory (give it back to the request mempool).
 */
static inline void i2o_block_request_free(struct i2o_block_request *ireq)
{
	mempool_free(ireq, i2o_blk_req_pool.pool);
};

/**
 *	i2o_block_sglist_alloc - Allocate the SG list and map it
 *	@ireq: I2O block request
 *
 *	Builds the SG list and map it into to be accessable by the controller.
 *
 *	Returns the number of elements in the SG list or 0 on failure.
 */
static inline int i2o_block_sglist_alloc(struct i2o_block_request *ireq)
{
	struct device *dev = &ireq->i2o_blk_dev->i2o_dev->iop->pdev->dev;
	int nents;

	nents = blk_rq_map_sg(ireq->req->q, ireq->req, ireq->sg_table);

	if (rq_data_dir(ireq->req) == READ)
		ireq->sg_dma_direction = PCI_DMA_FROMDEVICE;
	else
		ireq->sg_dma_direction = PCI_DMA_TODEVICE;

	ireq->sg_nents = dma_map_sg(dev, ireq->sg_table, nents,
				    ireq->sg_dma_direction);

	return ireq->sg_nents;
};

/**
 *	i2o_block_sglist_free - Frees the SG list
 *	@ireq: I2O block request from which the SG should be freed
 *
 *	Frees the SG list from the I2O block request.
 */
static inline void i2o_block_sglist_free(struct i2o_block_request *ireq)
{
	struct device *dev = &ireq->i2o_blk_dev->i2o_dev->iop->pdev->dev;

	dma_unmap_sg(dev, ireq->sg_table, ireq->sg_nents,
		     ireq->sg_dma_direction);
};

/**
 *	i2o_block_prep_req_fn - Allocates I2O block device specific struct
 *	@q: request queue for the request
 *	@req: the request to prepare
 *
 *	Allocate the necessary i2o_block_request struct and connect it to
 *	the request. This is needed that we not loose the SG list later on.
 *
 *	Returns BLKPREP_OK on success or BLKPREP_DEFER on failure.
 */
static int i2o_block_prep_req_fn(struct request_queue *q, struct request *req)
{
	struct i2o_block_device *i2o_blk_dev = q->queuedata;
	struct i2o_block_request *ireq;

	/* request is already processed by us, so return */
	if (req->flags & REQ_SPECIAL) {
		osm_debug("REQ_SPECIAL already set!\n");
		req->flags |= REQ_DONTPREP;
		return BLKPREP_OK;
	}

	/* connect the i2o_block_request to the request */
	if (!req->special) {
		ireq = i2o_block_request_alloc();
		if (unlikely(IS_ERR(ireq))) {
			osm_debug("unable to allocate i2o_block_request!\n");
			return BLKPREP_DEFER;
		}

		ireq->i2o_blk_dev = i2o_blk_dev;
		req->special = ireq;
		ireq->req = req;
	} else
		ireq = req->special;

	/* do not come back here */
	req->flags |= REQ_DONTPREP | REQ_SPECIAL;

	return BLKPREP_OK;
};

/**
 *	i2o_block_delayed_request_fn - delayed request queue function
 *	delayed_request: the delayed request with the queue to start
 *
 *	If the request queue is stopped for a disk, and there is no open
 *	request, a new event is created, which calls this function to start
 *	the queue after I2O_BLOCK_REQUEST_TIME. Otherwise the queue will never
 *	be started again.
 */
static void i2o_block_delayed_request_fn(void *delayed_request)
{
	struct i2o_block_delayed_request *dreq = delayed_request;
	struct request_queue *q = dreq->queue;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_start_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
	kfree(dreq);
};

/**
 *	i2o_block_reply - Block OSM reply handler.
 *	@c: I2O controller from which the message arrives
 *	@m: message id of reply
 *	qmsg: the actuall I2O message reply
 *
 *	This function gets all the message replies.
 *
 */
static int i2o_block_reply(struct i2o_controller *c, u32 m,
			   struct i2o_message *msg)
{
	struct i2o_block_request *ireq;
	struct request *req;
	struct i2o_block_device *dev;
	struct request_queue *q;
	u8 st;
	unsigned long flags;

	/* FAILed message */
	if (unlikely(le32_to_cpu(msg->u.head[0]) & (1 << 13))) {
		struct i2o_message *pmsg;
		u32 pm;

		/*
		 * FAILed message from controller
		 * We increment the error count and abort it
		 *
		 * In theory this will never happen.  The I2O block class
		 * specification states that block devices never return
		 * FAILs but instead use the REQ status field...but
		 * better be on the safe side since no one really follows
		 * the spec to the book :)
		 */
		pm = le32_to_cpu(msg->body[3]);
		pmsg = i2o_msg_in_to_virt(c, pm);

		req = i2o_cntxt_list_get(c, le32_to_cpu(pmsg->u.s.tcntxt));
		if (unlikely(!req)) {
			osm_err("NULL reply received!\n");
			return -1;
		}

		ireq = req->special;
		dev = ireq->i2o_blk_dev;
		q = dev->gd->queue;

		req->errors++;

		spin_lock_irqsave(q->queue_lock, flags);

		while (end_that_request_chunk(req, !req->errors,
					      le32_to_cpu(pmsg->body[1]))) ;
		end_that_request_last(req);

		dev->open_queue_depth--;
		list_del(&ireq->queue);
		blk_start_queue(q);

		spin_unlock_irqrestore(q->queue_lock, flags);

		/* Now flush the message by making it a NOP */
		i2o_msg_nop(c, pm);

		return -1;
	}

	req = i2o_cntxt_list_get(c, le32_to_cpu(msg->u.s.tcntxt));
	if (unlikely(!req)) {
		osm_err("NULL reply received!\n");
		return -1;
	}

	ireq = req->special;
	dev = ireq->i2o_blk_dev;
	q = dev->gd->queue;

	if (unlikely(!dev->i2o_dev)) {
		/*
		 * This is HACK, but Intel Integrated RAID allows user
		 * to delete a volume that is claimed, locked, and in use
		 * by the OS. We have to check for a reply from a
		 * non-existent device and flag it as an error or the system
		 * goes kaput...
		 */
		req->errors++;
		osm_warn("Data transfer to deleted device!\n");
		spin_lock_irqsave(q->queue_lock, flags);
		while (end_that_request_chunk
		       (req, !req->errors, le32_to_cpu(msg->body[1]))) ;
		end_that_request_last(req);

		dev->open_queue_depth--;
		list_del(&ireq->queue);
		blk_start_queue(q);

		spin_unlock_irqrestore(q->queue_lock, flags);
		return -1;
	}

	/*
	 *      Lets see what is cooking. We stuffed the
	 *      request in the context.
	 */

	st = le32_to_cpu(msg->body[0]) >> 24;

	if (st != 0) {
		int err;
		char *bsa_errors[] = {
			"Success",
			"Media Error",
			"Failure communicating to device",
			"Device Failure",
			"Device is not ready",
			"Media not present",
			"Media is locked by another user",
			"Media has failed",
			"Failure communicating to device",
			"Device bus failure",
			"Device is locked by another user",
			"Device is write protected",
			"Device has reset",
			"Volume has changed, waiting for acknowledgement"
		};

		err = le32_to_cpu(msg->body[0]) & 0xffff;

		/*
		 *      Device not ready means two things. One is that the
		 *      the thing went offline (but not a removal media)
		 *
		 *      The second is that you have a SuperTrak 100 and the
		 *      firmware got constipated. Unlike standard i2o card
		 *      setups the supertrak returns an error rather than
		 *      blocking for the timeout in these cases.
		 *
		 *      Don't stick a supertrak100 into cache aggressive modes
		 */

		osm_err("block-osm: /dev/%s error: %s", dev->gd->disk_name,
			bsa_errors[le32_to_cpu(msg->body[0]) & 0xffff]);
		if (le32_to_cpu(msg->body[0]) & 0x00ff0000)
			printk(KERN_ERR " - DDM attempted %d retries",
			       (le32_to_cpu(msg->body[0]) >> 16) & 0x00ff);
		printk(KERN_ERR ".\n");
		req->errors++;
	} else
		req->errors = 0;

	if (!end_that_request_chunk
	    (req, !req->errors, le32_to_cpu(msg->body[1]))) {
		add_disk_randomness(req->rq_disk);
		spin_lock_irqsave(q->queue_lock, flags);

		end_that_request_last(req);

		dev->open_queue_depth--;
		list_del(&ireq->queue);
		blk_start_queue(q);

		spin_unlock_irqrestore(q->queue_lock, flags);

		i2o_block_sglist_free(ireq);
		i2o_block_request_free(ireq);
	} else
		osm_err("still remaining chunks\n");

	return 1;
};

static void i2o_block_event(struct i2o_event *evt)
{
	osm_info("block-osm: event received\n");
};

/*
 *	SCSI-CAM for ioctl geometry mapping
 *	Duplicated with SCSI - this should be moved into somewhere common
 *	perhaps genhd ?
 *
 * LBA -> CHS mapping table taken from:
 *
 * "Incorporating the I2O Architecture into BIOS for Intel Architecture
 *  Platforms"
 *
 * This is an I2O document that is only available to I2O members,
 * not developers.
 *
 * From my understanding, this is how all the I2O cards do this
 *
 * Disk Size      | Sectors | Heads | Cylinders
 * ---------------+---------+-------+-------------------
 * 1 < X <= 528M  | 63      | 16    | X/(63 * 16 * 512)
 * 528M < X <= 1G | 63      | 32    | X/(63 * 32 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 *
 */
#define	BLOCK_SIZE_528M		1081344
#define	BLOCK_SIZE_1G		2097152
#define	BLOCK_SIZE_21G		4403200
#define	BLOCK_SIZE_42G		8806400
#define	BLOCK_SIZE_84G		17612800

static void i2o_block_biosparam(unsigned long capacity, unsigned short *cyls,
				unsigned char *hds, unsigned char *secs)
{
	unsigned long heads, sectors, cylinders;

	sectors = 63L;		/* Maximize sectors per track */
	if (capacity <= BLOCK_SIZE_528M)
		heads = 16;
	else if (capacity <= BLOCK_SIZE_1G)
		heads = 32;
	else if (capacity <= BLOCK_SIZE_21G)
		heads = 64;
	else if (capacity <= BLOCK_SIZE_42G)
		heads = 128;
	else
		heads = 255;

	cylinders = (unsigned long)capacity / (heads * sectors);

	*cyls = (unsigned short)cylinders;	/* Stuff return values */
	*secs = (unsigned char)sectors;
	*hds = (unsigned char)heads;
}

/**
 *	i2o_block_open - Open the block device
 *
 *	Power up the device, mount and lock the media. This function is called,
 *	if the block device is opened for access.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_open(struct inode *inode, struct file *file)
{
	struct i2o_block_device *dev = inode->i_bdev->bd_disk->private_data;

	if (!dev->i2o_dev)
		return -ENODEV;

	if (dev->power > 0x1f)
		i2o_block_device_power(dev, 0x02);

	i2o_block_device_mount(dev->i2o_dev, -1);

	i2o_block_device_lock(dev->i2o_dev, -1);

	osm_debug("Ready.\n");

	return 0;
};

/**
 *	i2o_block_release - Release the I2O block device
 *
 *	Unlock and unmount the media, and power down the device. Gets called if
 *	the block device is closed.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_release(struct inode *inode, struct file *file)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct i2o_block_device *dev = disk->private_data;
	u8 operation;

	/*
	 * This is to deail with the case of an application
	 * opening a device and then the device dissapears while
	 * it's in use, and then the application tries to release
	 * it.  ex: Unmounting a deleted RAID volume at reboot.
	 * If we send messages, it will just cause FAILs since
	 * the TID no longer exists.
	 */
	if (!dev->i2o_dev)
		return 0;

	i2o_block_device_flush(dev->i2o_dev);

	i2o_block_device_unlock(dev->i2o_dev, -1);

	if (dev->flags & (1 << 3 | 1 << 4))	/* Removable */
		operation = 0x21;
	else
		operation = 0x24;

	i2o_block_device_power(dev, operation);

	return 0;
}

/**
 *	i2o_block_ioctl - Issue device specific ioctl calls.
 *	@cmd: ioctl command
 *	@arg: arg
 *
 *	Handles ioctl request for the block device.
 *
 *	Return 0 on success or negative error on failure.
 */
static int i2o_block_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct i2o_block_device *dev = disk->private_data;
	void __user *argp = (void __user *)arg;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case HDIO_GETGEO:
		{
			struct hd_geometry g;
			i2o_block_biosparam(get_capacity(disk),
					    &g.cylinders, &g.heads, &g.sectors);
			g.start = get_start_sect(inode->i_bdev);
			return copy_to_user(argp, &g, sizeof(g)) ? -EFAULT : 0;
		}

	case BLKI2OGRSTRAT:
		return put_user(dev->rcache, (int __user *)arg);
	case BLKI2OGWSTRAT:
		return put_user(dev->wcache, (int __user *)arg);
	case BLKI2OSRSTRAT:
		if (arg < 0 || arg > CACHE_SMARTFETCH)
			return -EINVAL;
		dev->rcache = arg;
		break;
	case BLKI2OSWSTRAT:
		if (arg != 0
		    && (arg < CACHE_WRITETHROUGH || arg > CACHE_SMARTBACK))
			return -EINVAL;
		dev->wcache = arg;
		break;
	}
	return -ENOTTY;
};

/**
 *	i2o_block_media_changed - Have we seen a media change?
 *	@disk: gendisk which should be verified
 *
 *	Verifies if the media has changed.
 *
 *	Returns 1 if the media was changed or 0 otherwise.
 */
static int i2o_block_media_changed(struct gendisk *disk)
{
	struct i2o_block_device *p = disk->private_data;

	if (p->media_change_flag) {
		p->media_change_flag = 0;
		return 1;
	}
	return 0;
}

/**
 *	i2o_block_transfer - Transfer a request to/from the I2O controller
 *	@req: the request which should be transfered
 *
 *	This function converts the request into a I2O message. The necessary
 *	DMA buffers are allocated and after everything is setup post the message
 *	to the I2O controller. No cleanup is done by this function. It is done
 *	on the interrupt side when the reply arrives.
 *
 *	Return 0 on success or negative error code on failure.
 */
static int i2o_block_transfer(struct request *req)
{
	struct i2o_block_device *dev = req->rq_disk->private_data;
	struct i2o_controller *c = dev->i2o_dev->iop;
	int tid = dev->i2o_dev->lct_data.tid;
	struct i2o_message __iomem *msg;
	void __iomem *mptr;
	struct i2o_block_request *ireq = req->special;
	struct scatterlist *sg;
	int sgnum;
	int i;
	u32 m;
	u32 tcntxt;
	u32 sg_flags;
	int rc;

	m = i2o_msg_get(c, &msg);
	if (m == I2O_QUEUE_EMPTY) {
		rc = -EBUSY;
		goto exit;
	}

	tcntxt = i2o_cntxt_list_add(c, req);
	if (!tcntxt) {
		rc = -ENOMEM;
		goto nop_msg;
	}

	if ((sgnum = i2o_block_sglist_alloc(ireq)) <= 0) {
		rc = -ENOMEM;
		goto context_remove;
	}

	/* Build the message based on the request. */
	writel(i2o_block_driver.context, &msg->u.s.icntxt);
	writel(tcntxt, &msg->u.s.tcntxt);
	writel(req->nr_sectors << 9, &msg->body[1]);

	writel((((u64) req->sector) << 9) & 0xffffffff, &msg->body[2]);
	writel(req->sector >> 23, &msg->body[3]);

	mptr = &msg->body[4];

	sg = ireq->sg_table;

	if (rq_data_dir(req) == READ) {
		writel(I2O_CMD_BLOCK_READ << 24 | HOST_TID << 12 | tid,
		       &msg->u.head[1]);
		sg_flags = 0x10000000;
		switch (dev->rcache) {
		case CACHE_NULL:
			writel(0, &msg->body[0]);
			break;
		case CACHE_PREFETCH:
			writel(0x201F0008, &msg->body[0]);
			break;
		case CACHE_SMARTFETCH:
			if (req->nr_sectors > 16)
				writel(0x201F0008, &msg->body[0]);
			else
				writel(0x001F0000, &msg->body[0]);
			break;
		}
	} else {
		writel(I2O_CMD_BLOCK_WRITE << 24 | HOST_TID << 12 | tid,
		       &msg->u.head[1]);
		sg_flags = 0x14000000;
		switch (dev->wcache) {
		case CACHE_NULL:
			writel(0, &msg->body[0]);
			break;
		case CACHE_WRITETHROUGH:
			writel(0x001F0008, &msg->body[0]);
			break;
		case CACHE_WRITEBACK:
			writel(0x001F0010, &msg->body[0]);
			break;
		case CACHE_SMARTBACK:
			if (req->nr_sectors > 16)
				writel(0x001F0004, &msg->body[0]);
			else
				writel(0x001F0010, &msg->body[0]);
			break;
		case CACHE_SMARTTHROUGH:
			if (req->nr_sectors > 16)
				writel(0x001F0004, &msg->body[0]);
			else
				writel(0x001F0010, &msg->body[0]);
		}
	}

	for (i = sgnum; i > 0; i--) {
		if (i == 1)
			sg_flags |= 0x80000000;
		writel(sg_flags | sg_dma_len(sg), mptr);
		writel(sg_dma_address(sg), mptr + 4);
		mptr += 8;
		sg++;
	}

	writel(I2O_MESSAGE_SIZE
	       (((unsigned long)mptr -
		 (unsigned long)&msg->u.head[0]) >> 2) | SGL_OFFSET_8,
	       &msg->u.head[0]);

	list_add_tail(&ireq->queue, &dev->open_queue);
	dev->open_queue_depth++;

	i2o_msg_post(c, m);

	return 0;

      context_remove:
	i2o_cntxt_list_remove(c, req);

      nop_msg:
	i2o_msg_nop(c, m);

      exit:
	return rc;
};

/**
 *	i2o_block_request_fn - request queue handling function
 *	q: request queue from which the request could be fetched
 *
 *	Takes the next request from the queue, transfers it and if no error
 *	occurs dequeue it from the queue. On arrival of the reply the message
 *	will be processed further. If an error occurs requeue the request.
 */
static void i2o_block_request_fn(struct request_queue *q)
{
	struct request *req;

	while (!blk_queue_plugged(q)) {
		req = elv_next_request(q);
		if (!req)
			break;

		if (blk_fs_request(req)) {
			struct i2o_block_delayed_request *dreq;
			struct i2o_block_request *ireq = req->special;
			unsigned int queue_depth;

			queue_depth = ireq->i2o_blk_dev->open_queue_depth;

			if (queue_depth < I2O_BLOCK_MAX_OPEN_REQUESTS)
				if (!i2o_block_transfer(req)) {
					blkdev_dequeue_request(req);
					continue;
				}

			if (queue_depth)
				break;

			/* stop the queue and retry later */
			dreq = kmalloc(sizeof(*dreq), GFP_ATOMIC);
			if (!dreq)
				continue;

			dreq->queue = q;
			INIT_WORK(&dreq->work, i2o_block_delayed_request_fn,
				  dreq);

			osm_info("transfer error\n");
			if (!queue_delayed_work(i2o_block_driver.event_queue,
						&dreq->work,
						I2O_BLOCK_RETRY_TIME))
				kfree(dreq);
			else {
				blk_stop_queue(q);
				break;
			}
		} else
			end_request(req, 0);
	}
};

/* I2O Block device operations definition */
static struct block_device_operations i2o_block_fops = {
	.owner = THIS_MODULE,
	.open = i2o_block_open,
	.release = i2o_block_release,
	.ioctl = i2o_block_ioctl,
	.media_changed = i2o_block_media_changed
};

/**
 *	i2o_block_device_alloc - Allocate memory for a I2O Block device
 *
 *	Allocate memory for the i2o_block_device struct, gendisk and request
 *	queue and initialize them as far as no additional information is needed.
 *
 *	Returns a pointer to the allocated I2O Block device on succes or a
 *	negative error code on failure.
 */
static struct i2o_block_device *i2o_block_device_alloc(void)
{
	struct i2o_block_device *dev;
	struct gendisk *gd;
	struct request_queue *queue;
	int rc;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		osm_err("Insufficient memory to allocate I2O Block disk.\n");
		rc = -ENOMEM;
		goto exit;
	}
	memset(dev, 0, sizeof(*dev));

	INIT_LIST_HEAD(&dev->open_queue);
	spin_lock_init(&dev->lock);
	dev->rcache = CACHE_PREFETCH;
	dev->wcache = CACHE_WRITEBACK;

	/* allocate a gendisk with 16 partitions */
	gd = alloc_disk(16);
	if (!gd) {
		osm_err("Insufficient memory to allocate gendisk.\n");
		rc = -ENOMEM;
		goto cleanup_dev;
	}

	/* initialize the request queue */
	queue = blk_init_queue(i2o_block_request_fn, &dev->lock);
	if (!queue) {
		osm_err("Insufficient memory to allocate request queue.\n");
		rc = -ENOMEM;
		goto cleanup_queue;
	}

	blk_queue_prep_rq(queue, i2o_block_prep_req_fn);

	gd->major = I2O_MAJOR;
	gd->queue = queue;
	gd->fops = &i2o_block_fops;
	gd->private_data = dev;

	dev->gd = gd;

	return dev;

      cleanup_queue:
	put_disk(gd);

      cleanup_dev:
	kfree(dev);

      exit:
	return ERR_PTR(rc);
};

/**
 *	i2o_block_probe - verify if dev is a I2O Block device and install it
 *	@dev: device to verify if it is a I2O Block device
 *
 *	We only verify if the user_tid of the device is 0xfff and then install
 *	the device. Otherwise it is used by some other device (e. g. RAID).
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_block_probe(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_block_device *i2o_blk_dev;
	struct i2o_controller *c = i2o_dev->iop;
	struct gendisk *gd;
	struct request_queue *queue;
	static int unit = 0;
	int rc;
	u64 size;
	u32 blocksize;
	u16 power;
	u32 flags, status;
	int segments;

	/* skip devices which are used by IOP */
	if (i2o_dev->lct_data.user_tid != 0xfff) {
		osm_debug("skipping used device %03x\n", i2o_dev->lct_data.tid);
		return -ENODEV;
	}

	osm_info("New device detected (TID: %03x)\n", i2o_dev->lct_data.tid);

	if (i2o_device_claim(i2o_dev)) {
		osm_warn("Unable to claim device. Installation aborted\n");
		rc = -EFAULT;
		goto exit;
	}

	i2o_blk_dev = i2o_block_device_alloc();
	if (IS_ERR(i2o_blk_dev)) {
		osm_err("could not alloc a new I2O block device");
		rc = PTR_ERR(i2o_blk_dev);
		goto claim_release;
	}

	i2o_blk_dev->i2o_dev = i2o_dev;
	dev_set_drvdata(dev, i2o_blk_dev);

	/* setup gendisk */
	gd = i2o_blk_dev->gd;
	gd->first_minor = unit << 4;
	sprintf(gd->disk_name, "i2o/hd%c", 'a' + unit);
	sprintf(gd->devfs_name, "i2o/hd%c", 'a' + unit);
	gd->driverfs_dev = &i2o_dev->device;

	/* setup request queue */
	queue = gd->queue;
	queue->queuedata = i2o_blk_dev;

	blk_queue_max_phys_segments(queue, I2O_MAX_SEGMENTS);
	blk_queue_max_sectors(queue, I2O_MAX_SECTORS);

	if (c->short_req)
		segments = 8;
	else {
		i2o_status_block *sb;

		sb = c->status_block.virt;

		segments = (sb->inbound_frame_size -
			    sizeof(struct i2o_message) / 4 - 4) / 2;
	}

	blk_queue_max_hw_segments(queue, segments);

	osm_debug("max sectors = %d\n", I2O_MAX_SECTORS);
	osm_debug("phys segments = %d\n", I2O_MAX_SEGMENTS);
	osm_debug("hw segments = %d\n", segments);

	/*
	 *      Ask for the current media data. If that isn't supported
	 *      then we ask for the device capacity data
	 */
	if (i2o_parm_field_get(i2o_dev, 0x0004, 1, &blocksize, 4) != 0
	    || i2o_parm_field_get(i2o_dev, 0x0004, 0, &size, 8) != 0) {
		i2o_parm_field_get(i2o_dev, 0x0000, 3, &blocksize, 4);
		i2o_parm_field_get(i2o_dev, 0x0000, 4, &size, 8);
	}
	osm_debug("blocksize = %d\n", blocksize);

	if (i2o_parm_field_get(i2o_dev, 0x0000, 2, &power, 2))
		power = 0;
	i2o_parm_field_get(i2o_dev, 0x0000, 5, &flags, 4);
	i2o_parm_field_get(i2o_dev, 0x0000, 6, &status, 4);

	set_capacity(gd, size >> 9);

	i2o_event_register(i2o_dev, &i2o_block_driver, 0, 0xffffffff);

	add_disk(gd);

	unit++;

	return 0;

      claim_release:
	i2o_device_claim_release(i2o_dev);

      exit:
	return rc;
};

/* Block OSM driver struct */
static struct i2o_driver i2o_block_driver = {
	.name = OSM_NAME,
	.event = i2o_block_event,
	.reply = i2o_block_reply,
	.classes = i2o_block_class_id,
	.driver = {
		   .probe = i2o_block_probe,
		   .remove = i2o_block_remove,
		   },
};

/**
 *	i2o_block_init - Block OSM initialization function
 *
 *	Allocate the slab and mempool for request structs, registers i2o_block
 *	block device and finally register the Block OSM in the I2O core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_block_init(void)
{
	int rc;
	int size;

	printk(KERN_INFO OSM_DESCRIPTION " v" OSM_VERSION "\n");

	/* Allocate request mempool and slab */
	size = sizeof(struct i2o_block_request);
	i2o_blk_req_pool.slab = kmem_cache_create("i2o_block_req", size, 0,
						  SLAB_HWCACHE_ALIGN, NULL,
						  NULL);
	if (!i2o_blk_req_pool.slab) {
		osm_err("can't init request slab\n");
		rc = -ENOMEM;
		goto exit;
	}

	i2o_blk_req_pool.pool = mempool_create(I2O_REQ_MEMPOOL_SIZE,
					       mempool_alloc_slab,
					       mempool_free_slab,
					       i2o_blk_req_pool.slab);
	if (!i2o_blk_req_pool.pool) {
		osm_err("can't init request mempool\n");
		rc = -ENOMEM;
		goto free_slab;
	}

	/* Register the block device interfaces */
	rc = register_blkdev(I2O_MAJOR, "i2o_block");
	if (rc) {
		osm_err("unable to register block device\n");
		goto free_mempool;
	}
#ifdef MODULE
	osm_info("registered device at major %d\n", I2O_MAJOR);
#endif

	/* Register Block OSM into I2O core */
	rc = i2o_driver_register(&i2o_block_driver);
	if (rc) {
		osm_err("Could not register Block driver\n");
		goto unregister_blkdev;
	}

	return 0;

      unregister_blkdev:
	unregister_blkdev(I2O_MAJOR, "i2o_block");

      free_mempool:
	mempool_destroy(i2o_blk_req_pool.pool);

      free_slab:
	kmem_cache_destroy(i2o_blk_req_pool.slab);

      exit:
	return rc;
};

/**
 *	i2o_block_exit - Block OSM exit function
 *
 *	Unregisters Block OSM from I2O core, unregisters i2o_block block device
 *	and frees the mempool and slab.
 */
static void __exit i2o_block_exit(void)
{
	/* Unregister I2O Block OSM from I2O core */
	i2o_driver_unregister(&i2o_block_driver);

	/* Unregister block device */
	unregister_blkdev(I2O_MAJOR, "i2o_block");

	/* Free request mempool and slab */
	mempool_destroy(i2o_blk_req_pool.pool);
	kmem_cache_destroy(i2o_blk_req_pool.slab);
};

MODULE_AUTHOR("Red Hat");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(OSM_DESCRIPTION);
MODULE_VERSION(OSM_VERSION);

module_init(i2o_block_init);
module_exit(i2o_block_exit);
