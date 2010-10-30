/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>
#include "osd.h"
#include "logging.h"
#include "version_info.h"
#include "vmbus.h"
#include "storvsc_api.h"


#define BLKVSC_MINORS	64

enum blkvsc_device_type {
	UNKNOWN_DEV_TYPE,
	HARDDISK_TYPE,
	DVD_TYPE,
};

/*
 * This request ties the struct request and struct
 * blkvsc_request/hv_storvsc_request together A struct request may be
 * represented by 1 or more struct blkvsc_request
 */
struct blkvsc_request_group {
	int outstanding;
	int status;
	struct list_head blkvsc_req_list;	/* list of blkvsc_requests */
};

struct blkvsc_request {
	/* blkvsc_request_group.blkvsc_req_list */
	struct list_head req_entry;

	/* block_device_context.pending_list */
	struct list_head pend_entry;

	/* This may be null if we generate a request internally */
	struct request *req;

	struct block_device_context *dev;

	/* The group this request is part of. Maybe null */
	struct blkvsc_request_group *group;

	wait_queue_head_t wevent;
	int cond;

	int write;
	sector_t sector_start;
	unsigned long sector_count;

	unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];
	unsigned char cmd_len;
	unsigned char cmnd[MAX_COMMAND_SIZE];

	struct hv_storvsc_request request;
	/*
	 * !!!DO NOT ADD ANYTHING BELOW HERE!!! Otherwise, memory can overlap,
	 * because - The extension buffer falls right here and is pointed to by
	 * request.Extension;
	 * Which sounds like a horrible idea, who designed this?
	 */
};

/* Per device structure */
struct block_device_context {
	/* point back to our device context */
	struct vm_device *device_ctx;
	struct kmem_cache *request_pool;
	spinlock_t lock;
	struct gendisk *gd;
	enum blkvsc_device_type	device_type;
	struct list_head pending_list;

	unsigned char device_id[64];
	unsigned int device_id_len;
	int num_outstanding_reqs;
	int shutting_down;
	int media_not_present;
	unsigned int sector_size;
	sector_t capacity;
	unsigned int port;
	unsigned char path;
	unsigned char target;
	int users;
};

/* Per driver */
struct blkvsc_driver_context {
	/* !! These must be the first 2 fields !! */
	/* FIXME this is a bug! */
	struct driver_context drv_ctx;
	struct storvsc_driver_object drv_obj;
};

/* Static decl */
static DEFINE_MUTEX(blkvsc_mutex);
static int blkvsc_probe(struct device *dev);
static int blkvsc_remove(struct device *device);
static void blkvsc_shutdown(struct device *device);

static int blkvsc_open(struct block_device *bdev,  fmode_t mode);
static int blkvsc_release(struct gendisk *disk, fmode_t mode);
static int blkvsc_media_changed(struct gendisk *gd);
static int blkvsc_revalidate_disk(struct gendisk *gd);
static int blkvsc_getgeo(struct block_device *bd, struct hd_geometry *hg);
static int blkvsc_ioctl(struct block_device *bd, fmode_t mode,
			unsigned cmd, unsigned long argument);
static void blkvsc_request(struct request_queue *queue);
static void blkvsc_request_completion(struct hv_storvsc_request *request);
static int blkvsc_do_request(struct block_device_context *blkdev,
			     struct request *req);
static int blkvsc_submit_request(struct blkvsc_request *blkvsc_req,
		void (*request_completion)(struct hv_storvsc_request *));
static void blkvsc_init_rw(struct blkvsc_request *blkvsc_req);
static void blkvsc_cmd_completion(struct hv_storvsc_request *request);
static int blkvsc_do_inquiry(struct block_device_context *blkdev);
static int blkvsc_do_read_capacity(struct block_device_context *blkdev);
static int blkvsc_do_read_capacity16(struct block_device_context *blkdev);
static int blkvsc_do_flush(struct block_device_context *blkdev);
static int blkvsc_cancel_pending_reqs(struct block_device_context *blkdev);
static int blkvsc_do_pending_reqs(struct block_device_context *blkdev);

static int blkvsc_ringbuffer_size = BLKVSC_RING_BUFFER_SIZE;
module_param(blkvsc_ringbuffer_size, int, S_IRUGO);
MODULE_PARM_DESC(ring_size, "Ring buffer size (in bytes)");

/* The one and only one */
static struct blkvsc_driver_context g_blkvsc_drv;

static const struct block_device_operations block_ops = {
	.owner = THIS_MODULE,
	.open = blkvsc_open,
	.release = blkvsc_release,
	.media_changed = blkvsc_media_changed,
	.revalidate_disk = blkvsc_revalidate_disk,
	.getgeo = blkvsc_getgeo,
	.ioctl  = blkvsc_ioctl,
};

/*
 * blkvsc_drv_init -  BlkVsc driver initialization.
 */
static int blkvsc_drv_init(int (*drv_init)(struct hv_driver *drv))
{
	struct storvsc_driver_object *storvsc_drv_obj = &g_blkvsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_blkvsc_drv.drv_ctx;
	int ret;

	vmbus_get_interface(&storvsc_drv_obj->Base.VmbusChannelInterface);

	storvsc_drv_obj->RingBufferSize = blkvsc_ringbuffer_size;

	/* Callback to client driver to complete the initialization */
	drv_init(&storvsc_drv_obj->Base);

	drv_ctx->driver.name = storvsc_drv_obj->Base.name;
	memcpy(&drv_ctx->class_id, &storvsc_drv_obj->Base.deviceType,
	       sizeof(struct hv_guid));

	drv_ctx->probe = blkvsc_probe;
	drv_ctx->remove = blkvsc_remove;
	drv_ctx->shutdown = blkvsc_shutdown;

	/* The driver belongs to vmbus */
	ret = vmbus_child_driver_register(drv_ctx);

	return ret;
}

static int blkvsc_drv_exit_cb(struct device *dev, void *data)
{
	struct device **curr = (struct device **)data;
	*curr = dev;
	return 1; /* stop iterating */
}

static void blkvsc_drv_exit(void)
{
	struct storvsc_driver_object *storvsc_drv_obj = &g_blkvsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_blkvsc_drv.drv_ctx;
	struct device *current_dev;
	int ret;

	while (1) {
		current_dev = NULL;

		/* Get the device */
		ret = driver_for_each_device(&drv_ctx->driver, NULL,
					     (void *) &current_dev,
					     blkvsc_drv_exit_cb);

		if (ret)
			DPRINT_WARN(BLKVSC_DRV,
				    "driver_for_each_device returned %d", ret);


		if (current_dev == NULL)
			break;

		/* Initiate removal from the top-down */
		device_unregister(current_dev);
	}

	if (storvsc_drv_obj->Base.OnCleanup)
		storvsc_drv_obj->Base.OnCleanup(&storvsc_drv_obj->Base);

	vmbus_child_driver_unregister(drv_ctx);

	return;
}

/*
 * blkvsc_probe - Add a new device for this driver
 */
static int blkvsc_probe(struct device *device)
{
	struct driver_context *driver_ctx =
				driver_to_driver_context(device->driver);
	struct blkvsc_driver_context *blkvsc_drv_ctx =
				(struct blkvsc_driver_context *)driver_ctx;
	struct storvsc_driver_object *storvsc_drv_obj =
				&blkvsc_drv_ctx->drv_obj;
	struct vm_device *device_ctx = device_to_vm_device(device);
	struct hv_device *device_obj = &device_ctx->device_obj;

	struct block_device_context *blkdev = NULL;
	struct storvsc_device_info device_info;
	int major = 0;
	int devnum = 0;
	int ret = 0;
	static int ide0_registered;
	static int ide1_registered;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_probe - enter");

	if (!storvsc_drv_obj->Base.OnDeviceAdd) {
		DPRINT_ERR(BLKVSC_DRV, "OnDeviceAdd() not set");
		ret = -1;
		goto Cleanup;
	}

	blkdev = kzalloc(sizeof(struct block_device_context), GFP_KERNEL);
	if (!blkdev) {
		ret = -ENOMEM;
		goto Cleanup;
	}

	INIT_LIST_HEAD(&blkdev->pending_list);

	/* Initialize what we can here */
	spin_lock_init(&blkdev->lock);

	/* ASSERT(sizeof(struct blkvsc_request_group) <= */
	/* 	sizeof(struct blkvsc_request)); */

	blkdev->request_pool = kmem_cache_create(dev_name(&device_ctx->device),
					sizeof(struct blkvsc_request) +
					storvsc_drv_obj->RequestExtSize, 0,
					SLAB_HWCACHE_ALIGN, NULL);
	if (!blkdev->request_pool) {
		ret = -ENOMEM;
		goto Cleanup;
	}


	/* Call to the vsc driver to add the device */
	ret = storvsc_drv_obj->Base.OnDeviceAdd(device_obj, &device_info);
	if (ret != 0) {
		DPRINT_ERR(BLKVSC_DRV, "unable to add blkvsc device");
		goto Cleanup;
	}

	blkdev->device_ctx = device_ctx;
	/* this identified the device 0 or 1 */
	blkdev->target = device_info.TargetId;
	/* this identified the ide ctrl 0 or 1 */
	blkdev->path = device_info.PathId;

	dev_set_drvdata(device, blkdev);

	/* Calculate the major and device num */
	if (blkdev->path == 0) {
		major = IDE0_MAJOR;
		devnum = blkdev->path + blkdev->target;		/* 0 or 1 */

		if (!ide0_registered) {
			ret = register_blkdev(major, "ide");
			if (ret != 0) {
				DPRINT_ERR(BLKVSC_DRV,
					   "register_blkdev() failed! ret %d",
					   ret);
				goto Remove;
			}

			ide0_registered = 1;
		}
	} else if (blkdev->path == 1) {
		major = IDE1_MAJOR;
		devnum = blkdev->path + blkdev->target + 1; /* 2 or 3 */

		if (!ide1_registered) {
			ret = register_blkdev(major, "ide");
			if (ret != 0) {
				DPRINT_ERR(BLKVSC_DRV,
					   "register_blkdev() failed! ret %d",
					   ret);
				goto Remove;
			}

			ide1_registered = 1;
		}
	} else {
		DPRINT_ERR(BLKVSC_DRV, "invalid pathid");
		ret = -1;
		goto Cleanup;
	}

	DPRINT_INFO(BLKVSC_DRV, "blkvsc registered for major %d!!", major);

	blkdev->gd = alloc_disk(BLKVSC_MINORS);
	if (!blkdev->gd) {
		DPRINT_ERR(BLKVSC_DRV, "register_blkdev() failed! ret %d", ret);
		ret = -1;
		goto Cleanup;
	}

	blkdev->gd->queue = blk_init_queue(blkvsc_request, &blkdev->lock);

	blk_queue_max_segment_size(blkdev->gd->queue, PAGE_SIZE);
	blk_queue_max_segments(blkdev->gd->queue, MAX_MULTIPAGE_BUFFER_COUNT);
	blk_queue_segment_boundary(blkdev->gd->queue, PAGE_SIZE-1);
	blk_queue_bounce_limit(blkdev->gd->queue, BLK_BOUNCE_ANY);
	blk_queue_dma_alignment(blkdev->gd->queue, 511);

	blkdev->gd->major = major;
	if (devnum == 1 || devnum == 3)
		blkdev->gd->first_minor = BLKVSC_MINORS;
	else
		blkdev->gd->first_minor = 0;
	blkdev->gd->fops = &block_ops;
	blkdev->gd->private_data = blkdev;
	sprintf(blkdev->gd->disk_name, "hd%c", 'a' + devnum);

	blkvsc_do_inquiry(blkdev);
	if (blkdev->device_type == DVD_TYPE) {
		set_disk_ro(blkdev->gd, 1);
		blkdev->gd->flags |= GENHD_FL_REMOVABLE;
		blkvsc_do_read_capacity(blkdev);
	} else {
		blkvsc_do_read_capacity16(blkdev);
	}

	set_capacity(blkdev->gd, blkdev->capacity * (blkdev->sector_size/512));
	blk_queue_logical_block_size(blkdev->gd->queue, blkdev->sector_size);
	/* go! */
	add_disk(blkdev->gd);

	DPRINT_INFO(BLKVSC_DRV, "%s added!! capacity %lu sector_size %d",
		    blkdev->gd->disk_name, (unsigned long)blkdev->capacity,
		    blkdev->sector_size);

	return ret;

Remove:
	storvsc_drv_obj->Base.OnDeviceRemove(device_obj);

Cleanup:
	if (blkdev) {
		if (blkdev->request_pool) {
			kmem_cache_destroy(blkdev->request_pool);
			blkdev->request_pool = NULL;
		}
		kfree(blkdev);
		blkdev = NULL;
	}

	return ret;
}

static void blkvsc_shutdown(struct device *device)
{
	struct block_device_context *blkdev = dev_get_drvdata(device);
	unsigned long flags;

	if (!blkdev)
		return;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_shutdown - users %d disk %s\n",
		   blkdev->users, blkdev->gd->disk_name);

	spin_lock_irqsave(&blkdev->lock, flags);

	blkdev->shutting_down = 1;

	blk_stop_queue(blkdev->gd->queue);

	spin_unlock_irqrestore(&blkdev->lock, flags);

	while (blkdev->num_outstanding_reqs) {
		DPRINT_INFO(STORVSC, "waiting for %d requests to complete...",
			    blkdev->num_outstanding_reqs);
		udelay(100);
	}

	blkvsc_do_flush(blkdev);

	spin_lock_irqsave(&blkdev->lock, flags);

	blkvsc_cancel_pending_reqs(blkdev);

	spin_unlock_irqrestore(&blkdev->lock, flags);
}

static int blkvsc_do_flush(struct block_device_context *blkdev)
{
	struct blkvsc_request *blkvsc_req;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_do_flush()\n");

	if (blkdev->device_type != HARDDISK_TYPE)
		return 0;

	blkvsc_req = kmem_cache_alloc(blkdev->request_pool, GFP_KERNEL);
	if (!blkvsc_req)
		return -ENOMEM;

	memset(blkvsc_req, 0, sizeof(struct blkvsc_request));
	init_waitqueue_head(&blkvsc_req->wevent);
	blkvsc_req->dev = blkdev;
	blkvsc_req->req = NULL;
	blkvsc_req->write = 0;

	blkvsc_req->request.DataBuffer.PfnArray[0] = 0;
	blkvsc_req->request.DataBuffer.Offset = 0;
	blkvsc_req->request.DataBuffer.Length = 0;

	blkvsc_req->cmnd[0] = SYNCHRONIZE_CACHE;
	blkvsc_req->cmd_len = 10;

	/*
	 * Set this here since the completion routine may be invoked and
	 * completed before we return
	 */
	blkvsc_req->cond = 0;
	blkvsc_submit_request(blkvsc_req, blkvsc_cmd_completion);

	wait_event_interruptible(blkvsc_req->wevent, blkvsc_req->cond);

	kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);

	return 0;
}

/* Do a scsi INQUIRY cmd here to get the device type (ie disk or dvd) */
static int blkvsc_do_inquiry(struct block_device_context *blkdev)
{
	struct blkvsc_request *blkvsc_req;
	struct page *page_buf;
	unsigned char *buf;
	unsigned char device_type;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_do_inquiry()\n");

	blkvsc_req = kmem_cache_alloc(blkdev->request_pool, GFP_KERNEL);
	if (!blkvsc_req)
		return -ENOMEM;

	memset(blkvsc_req, 0, sizeof(struct blkvsc_request));
	page_buf = alloc_page(GFP_KERNEL);
	if (!page_buf) {
		kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);
		return -ENOMEM;
	}

	init_waitqueue_head(&blkvsc_req->wevent);
	blkvsc_req->dev = blkdev;
	blkvsc_req->req = NULL;
	blkvsc_req->write = 0;

	blkvsc_req->request.DataBuffer.PfnArray[0] = page_to_pfn(page_buf);
	blkvsc_req->request.DataBuffer.Offset = 0;
	blkvsc_req->request.DataBuffer.Length = 64;

	blkvsc_req->cmnd[0] = INQUIRY;
	blkvsc_req->cmnd[1] = 0x1;		/* Get product data */
	blkvsc_req->cmnd[2] = 0x83;		/* mode page 83 */
	blkvsc_req->cmnd[4] = 64;
	blkvsc_req->cmd_len = 6;

	/*
	 * Set this here since the completion routine may be invoked and
	 * completed before we return
	 */
	blkvsc_req->cond = 0;

	blkvsc_submit_request(blkvsc_req, blkvsc_cmd_completion);

	DPRINT_DBG(BLKVSC_DRV, "waiting %p to complete - cond %d\n",
		   blkvsc_req, blkvsc_req->cond);

	wait_event_interruptible(blkvsc_req->wevent, blkvsc_req->cond);

	buf = kmap(page_buf);

	/* print_hex_dump_bytes("", DUMP_PREFIX_NONE, buf, 64); */
	/* be to le */
	device_type = buf[0] & 0x1F;

	if (device_type == 0x0) {
		blkdev->device_type = HARDDISK_TYPE;
	} else if (device_type == 0x5) {
		blkdev->device_type = DVD_TYPE;
	} else {
		/* TODO: this is currently unsupported device type */
		blkdev->device_type = UNKNOWN_DEV_TYPE;
	}

	DPRINT_DBG(BLKVSC_DRV, "device type %d\n", device_type);

	blkdev->device_id_len = buf[7];
	if (blkdev->device_id_len > 64)
		blkdev->device_id_len = 64;

	memcpy(blkdev->device_id, &buf[8], blkdev->device_id_len);
	/* printk_hex_dump_bytes("", DUMP_PREFIX_NONE, blkdev->device_id,
	 * blkdev->device_id_len); */

	kunmap(page_buf);

	__free_page(page_buf);

	kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);

	return 0;
}

/* Do a scsi READ_CAPACITY cmd here to get the size of the disk */
static int blkvsc_do_read_capacity(struct block_device_context *blkdev)
{
	struct blkvsc_request *blkvsc_req;
	struct page *page_buf;
	unsigned char *buf;
	struct scsi_sense_hdr sense_hdr;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_do_read_capacity()\n");

	blkdev->sector_size = 0;
	blkdev->capacity = 0;
	blkdev->media_not_present = 0; /* assume a disk is present */

	blkvsc_req = kmem_cache_alloc(blkdev->request_pool, GFP_KERNEL);
	if (!blkvsc_req)
		return -ENOMEM;

	memset(blkvsc_req, 0, sizeof(struct blkvsc_request));
	page_buf = alloc_page(GFP_KERNEL);
	if (!page_buf) {
		kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);
		return -ENOMEM;
	}

	init_waitqueue_head(&blkvsc_req->wevent);
	blkvsc_req->dev = blkdev;
	blkvsc_req->req = NULL;
	blkvsc_req->write = 0;

	blkvsc_req->request.DataBuffer.PfnArray[0] = page_to_pfn(page_buf);
	blkvsc_req->request.DataBuffer.Offset = 0;
	blkvsc_req->request.DataBuffer.Length = 8;

	blkvsc_req->cmnd[0] = READ_CAPACITY;
	blkvsc_req->cmd_len = 16;

	/*
	 * Set this here since the completion routine may be invoked
	 * and completed before we return
	 */
	blkvsc_req->cond = 0;

	blkvsc_submit_request(blkvsc_req, blkvsc_cmd_completion);

	DPRINT_DBG(BLKVSC_DRV, "waiting %p to complete - cond %d\n",
		   blkvsc_req, blkvsc_req->cond);

	wait_event_interruptible(blkvsc_req->wevent, blkvsc_req->cond);

	/* check error */
	if (blkvsc_req->request.Status) {
		scsi_normalize_sense(blkvsc_req->sense_buffer,
				     SCSI_SENSE_BUFFERSIZE, &sense_hdr);

		if (sense_hdr.asc == 0x3A) {
			/* Medium not present */
			blkdev->media_not_present = 1;
		}
		return 0;
	}
	buf = kmap(page_buf);

	/* be to le */
	blkdev->capacity = ((buf[0] << 24) | (buf[1] << 16) |
			    (buf[2] << 8) | buf[3]) + 1;
	blkdev->sector_size = (buf[4] << 24) | (buf[5] << 16) |
			      (buf[6] << 8) | buf[7];

	kunmap(page_buf);

	__free_page(page_buf);

	kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);

	return 0;
}

static int blkvsc_do_read_capacity16(struct block_device_context *blkdev)
{
	struct blkvsc_request *blkvsc_req;
	struct page *page_buf;
	unsigned char *buf;
	struct scsi_sense_hdr sense_hdr;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_do_read_capacity16()\n");

	blkdev->sector_size = 0;
	blkdev->capacity = 0;
	blkdev->media_not_present = 0; /* assume a disk is present */

	blkvsc_req = kmem_cache_alloc(blkdev->request_pool, GFP_KERNEL);
	if (!blkvsc_req)
		return -ENOMEM;

	memset(blkvsc_req, 0, sizeof(struct blkvsc_request));
	page_buf = alloc_page(GFP_KERNEL);
	if (!page_buf) {
		kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);
		return -ENOMEM;
	}

	init_waitqueue_head(&blkvsc_req->wevent);
	blkvsc_req->dev = blkdev;
	blkvsc_req->req = NULL;
	blkvsc_req->write = 0;

	blkvsc_req->request.DataBuffer.PfnArray[0] = page_to_pfn(page_buf);
	blkvsc_req->request.DataBuffer.Offset = 0;
	blkvsc_req->request.DataBuffer.Length = 12;

	blkvsc_req->cmnd[0] = 0x9E; /* READ_CAPACITY16; */
	blkvsc_req->cmd_len = 16;

	/*
	 * Set this here since the completion routine may be invoked
	 * and completed before we return
	 */
	blkvsc_req->cond = 0;

	blkvsc_submit_request(blkvsc_req, blkvsc_cmd_completion);

	DPRINT_DBG(BLKVSC_DRV, "waiting %p to complete - cond %d\n",
		   blkvsc_req, blkvsc_req->cond);

	wait_event_interruptible(blkvsc_req->wevent, blkvsc_req->cond);

	/* check error */
	if (blkvsc_req->request.Status) {
		scsi_normalize_sense(blkvsc_req->sense_buffer,
				     SCSI_SENSE_BUFFERSIZE, &sense_hdr);
		if (sense_hdr.asc == 0x3A) {
			/* Medium not present */
			blkdev->media_not_present = 1;
		}
		return 0;
	}
	buf = kmap(page_buf);

	/* be to le */
	blkdev->capacity = be64_to_cpu(*(unsigned long long *) &buf[0]) + 1;
	blkdev->sector_size = be32_to_cpu(*(unsigned int *)&buf[8]);

#if 0
	blkdev->capacity = ((buf[0] << 24) | (buf[1] << 16) |
			    (buf[2] << 8) | buf[3]) + 1;
	blkdev->sector_size = (buf[4] << 24) | (buf[5] << 16) |
			      (buf[6] << 8) | buf[7];
#endif

	kunmap(page_buf);

	__free_page(page_buf);

	kmem_cache_free(blkvsc_req->dev->request_pool, blkvsc_req);

	return 0;
}

/*
 * blkvsc_remove() - Callback when our device is removed
 */
static int blkvsc_remove(struct device *device)
{
	struct driver_context *driver_ctx =
				driver_to_driver_context(device->driver);
	struct blkvsc_driver_context *blkvsc_drv_ctx =
				(struct blkvsc_driver_context *)driver_ctx;
	struct storvsc_driver_object *storvsc_drv_obj =
				&blkvsc_drv_ctx->drv_obj;
	struct vm_device *device_ctx = device_to_vm_device(device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	struct block_device_context *blkdev = dev_get_drvdata(device);
	unsigned long flags;
	int ret;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_remove()\n");

	if (!storvsc_drv_obj->Base.OnDeviceRemove)
		return -1;

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	ret = storvsc_drv_obj->Base.OnDeviceRemove(device_obj);
	if (ret != 0) {
		/* TODO: */
		DPRINT_ERR(BLKVSC_DRV,
			   "unable to remove blkvsc device (ret %d)", ret);
	}

	/* Get to a known state */
	spin_lock_irqsave(&blkdev->lock, flags);

	blkdev->shutting_down = 1;

	blk_stop_queue(blkdev->gd->queue);

	spin_unlock_irqrestore(&blkdev->lock, flags);

	while (blkdev->num_outstanding_reqs) {
		DPRINT_INFO(STORVSC, "waiting for %d requests to complete...",
			    blkdev->num_outstanding_reqs);
		udelay(100);
	}

	blkvsc_do_flush(blkdev);

	spin_lock_irqsave(&blkdev->lock, flags);

	blkvsc_cancel_pending_reqs(blkdev);

	spin_unlock_irqrestore(&blkdev->lock, flags);

	blk_cleanup_queue(blkdev->gd->queue);

	del_gendisk(blkdev->gd);

	kmem_cache_destroy(blkdev->request_pool);

	kfree(blkdev);

	return ret;
}

static void blkvsc_init_rw(struct blkvsc_request *blkvsc_req)
{
	/* ASSERT(blkvsc_req->req); */
	/* ASSERT(blkvsc_req->sector_count <= (MAX_MULTIPAGE_BUFFER_COUNT*8)); */

	blkvsc_req->cmd_len = 16;

	if (blkvsc_req->sector_start > 0xffffffff) {
		if (rq_data_dir(blkvsc_req->req)) {
			blkvsc_req->write = 1;
			blkvsc_req->cmnd[0] = WRITE_16;
		} else {
			blkvsc_req->write = 0;
			blkvsc_req->cmnd[0] = READ_16;
		}

		blkvsc_req->cmnd[1] |=
			(blkvsc_req->req->cmd_flags & REQ_FUA) ? 0x8 : 0;

		*(unsigned long long *)&blkvsc_req->cmnd[2] =
				cpu_to_be64(blkvsc_req->sector_start);
		*(unsigned int *)&blkvsc_req->cmnd[10] =
				cpu_to_be32(blkvsc_req->sector_count);
	} else if ((blkvsc_req->sector_count > 0xff) ||
		   (blkvsc_req->sector_start > 0x1fffff)) {
		if (rq_data_dir(blkvsc_req->req)) {
			blkvsc_req->write = 1;
			blkvsc_req->cmnd[0] = WRITE_10;
		} else {
			blkvsc_req->write = 0;
			blkvsc_req->cmnd[0] = READ_10;
		}

		blkvsc_req->cmnd[1] |=
			(blkvsc_req->req->cmd_flags & REQ_FUA) ? 0x8 : 0;

		*(unsigned int *)&blkvsc_req->cmnd[2] =
				cpu_to_be32(blkvsc_req->sector_start);
		*(unsigned short *)&blkvsc_req->cmnd[7] =
				cpu_to_be16(blkvsc_req->sector_count);
	} else {
		if (rq_data_dir(blkvsc_req->req)) {
			blkvsc_req->write = 1;
			blkvsc_req->cmnd[0] = WRITE_6;
		} else {
			blkvsc_req->write = 0;
			blkvsc_req->cmnd[0] = READ_6;
		}

		*(unsigned int *)&blkvsc_req->cmnd[1] =
				cpu_to_be32(blkvsc_req->sector_start) >> 8;
		blkvsc_req->cmnd[1] &= 0x1f;
		blkvsc_req->cmnd[4] = (unsigned char)blkvsc_req->sector_count;
	}
}

static int blkvsc_submit_request(struct blkvsc_request *blkvsc_req,
			void (*request_completion)(struct hv_storvsc_request *))
{
	struct block_device_context *blkdev = blkvsc_req->dev;
	struct vm_device *device_ctx = blkdev->device_ctx;
	struct driver_context *driver_ctx =
			driver_to_driver_context(device_ctx->device.driver);
	struct blkvsc_driver_context *blkvsc_drv_ctx =
			(struct blkvsc_driver_context *)driver_ctx;
	struct storvsc_driver_object *storvsc_drv_obj =
			&blkvsc_drv_ctx->drv_obj;
	struct hv_storvsc_request *storvsc_req;
	int ret;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_submit_request() - "
		   "req %p type %s start_sector %lu count %ld offset %d "
		   "len %d\n", blkvsc_req,
		   (blkvsc_req->write) ? "WRITE" : "READ",
		   (unsigned long) blkvsc_req->sector_start,
		   blkvsc_req->sector_count,
		   blkvsc_req->request.DataBuffer.Offset,
		   blkvsc_req->request.DataBuffer.Length);
#if 0
	for (i = 0; i < (blkvsc_req->request.DataBuffer.Length >> 12); i++) {
		DPRINT_DBG(BLKVSC_DRV, "blkvsc_submit_request() - "
			   "req %p pfn[%d] %llx\n",
			   blkvsc_req, i,
			   blkvsc_req->request.DataBuffer.PfnArray[i]);
	}
#endif

	storvsc_req = &blkvsc_req->request;
	storvsc_req->Extension = (void *)((unsigned long)blkvsc_req +
					  sizeof(struct blkvsc_request));

	storvsc_req->Type = blkvsc_req->write ? WRITE_TYPE : READ_TYPE;

	storvsc_req->OnIOCompletion = request_completion;
	storvsc_req->Context = blkvsc_req;

	storvsc_req->Host = blkdev->port;
	storvsc_req->Bus = blkdev->path;
	storvsc_req->TargetId = blkdev->target;
	storvsc_req->LunId = 0;	 /* this is not really used at all */

	storvsc_req->CdbLen = blkvsc_req->cmd_len;
	storvsc_req->Cdb = blkvsc_req->cmnd;

	storvsc_req->SenseBuffer = blkvsc_req->sense_buffer;
	storvsc_req->SenseBufferSize = SCSI_SENSE_BUFFERSIZE;

	ret = storvsc_drv_obj->OnIORequest(&blkdev->device_ctx->device_obj,
					   &blkvsc_req->request);
	if (ret == 0)
		blkdev->num_outstanding_reqs++;

	return ret;
}

/*
 * We break the request into 1 or more blkvsc_requests and submit
 * them.  If we cant submit them all, we put them on the
 * pending_list. The blkvsc_request() will work on the pending_list.
 */
static int blkvsc_do_request(struct block_device_context *blkdev,
			     struct request *req)
{
	struct bio *bio = NULL;
	struct bio_vec *bvec = NULL;
	struct bio_vec *prev_bvec = NULL;
	struct blkvsc_request *blkvsc_req = NULL;
	struct blkvsc_request *tmp;
	int databuf_idx = 0;
	int seg_idx = 0;
	sector_t start_sector;
	unsigned long num_sectors = 0;
	int ret = 0;
	int pending = 0;
	struct blkvsc_request_group *group = NULL;

	DPRINT_DBG(BLKVSC_DRV, "blkdev %p req %p sect %lu\n", blkdev, req,
		  (unsigned long)blk_rq_pos(req));

	/* Create a group to tie req to list of blkvsc_reqs */
	group = kmem_cache_alloc(blkdev->request_pool, GFP_ATOMIC);
	if (!group)
		return -ENOMEM;

	INIT_LIST_HEAD(&group->blkvsc_req_list);
	group->outstanding = group->status = 0;

	start_sector = blk_rq_pos(req);

	/* foreach bio in the request */
	if (req->bio) {
		for (bio = req->bio; bio; bio = bio->bi_next) {
			/*
			 * Map this bio into an existing or new storvsc request
			 */
			bio_for_each_segment(bvec, bio, seg_idx) {
				DPRINT_DBG(BLKVSC_DRV, "bio_for_each_segment() "
					   "- req %p bio %p bvec %p seg_idx %d "
					   "databuf_idx %d\n", req, bio, bvec,
					   seg_idx, databuf_idx);

				/* Get a new storvsc request */
				/* 1st-time */
				if ((!blkvsc_req) ||
				    (databuf_idx >= MAX_MULTIPAGE_BUFFER_COUNT)
				    /* hole at the begin of page */
				    || (bvec->bv_offset != 0) ||
				    /* hold at the end of page */
				    (prev_bvec &&
				     (prev_bvec->bv_len != PAGE_SIZE))) {
					/* submit the prev one */
					if (blkvsc_req) {
						blkvsc_req->sector_start = start_sector;
						sector_div(blkvsc_req->sector_start, (blkdev->sector_size >> 9));

						blkvsc_req->sector_count = num_sectors / (blkdev->sector_size >> 9);
						blkvsc_init_rw(blkvsc_req);
					}

					/*
					 * Create new blkvsc_req to represent
					 * the current bvec
					 */
					blkvsc_req = kmem_cache_alloc(blkdev->request_pool, GFP_ATOMIC);
					if (!blkvsc_req) {
						/* free up everything */
						list_for_each_entry_safe(
							blkvsc_req, tmp,
							&group->blkvsc_req_list,
							req_entry) {
							list_del(&blkvsc_req->req_entry);
							kmem_cache_free(blkdev->request_pool, blkvsc_req);
						}

						kmem_cache_free(blkdev->request_pool, group);
						return -ENOMEM;
					}

					memset(blkvsc_req, 0,
					       sizeof(struct blkvsc_request));

					blkvsc_req->dev = blkdev;
					blkvsc_req->req = req;
					blkvsc_req->request.DataBuffer.Offset = bvec->bv_offset;
					blkvsc_req->request.DataBuffer.Length = 0;

					/* Add to the group */
					blkvsc_req->group = group;
					blkvsc_req->group->outstanding++;
					list_add_tail(&blkvsc_req->req_entry,
						&blkvsc_req->group->blkvsc_req_list);

					start_sector += num_sectors;
					num_sectors = 0;
					databuf_idx = 0;
				}

				/* Add the curr bvec/segment to the curr blkvsc_req */
				blkvsc_req->request.DataBuffer.PfnArray[databuf_idx] = page_to_pfn(bvec->bv_page);
				blkvsc_req->request.DataBuffer.Length += bvec->bv_len;

				prev_bvec = bvec;

				databuf_idx++;
				num_sectors += bvec->bv_len >> 9;

			} /* bio_for_each_segment */

		} /* rq_for_each_bio */
	}

	/* Handle the last one */
	if (blkvsc_req) {
		DPRINT_DBG(BLKVSC_DRV, "blkdev %p req %p group %p count %d\n",
			   blkdev, req, blkvsc_req->group,
			   blkvsc_req->group->outstanding);

		blkvsc_req->sector_start = start_sector;
		sector_div(blkvsc_req->sector_start,
			   (blkdev->sector_size >> 9));

		blkvsc_req->sector_count = num_sectors /
					   (blkdev->sector_size >> 9);

		blkvsc_init_rw(blkvsc_req);
	}

	list_for_each_entry(blkvsc_req, &group->blkvsc_req_list, req_entry) {
		if (pending) {
			DPRINT_DBG(BLKVSC_DRV, "adding blkvsc_req to "
				   "pending_list - blkvsc_req %p start_sect %lu"
				   " sect_count %ld (%lu %ld)\n", blkvsc_req,
				   (unsigned long)blkvsc_req->sector_start,
				   blkvsc_req->sector_count,
				   (unsigned long)start_sector,
				   (unsigned long)num_sectors);

			list_add_tail(&blkvsc_req->pend_entry,
				      &blkdev->pending_list);
		} else {
			ret = blkvsc_submit_request(blkvsc_req,
						    blkvsc_request_completion);
			if (ret == -1) {
				pending = 1;
				list_add_tail(&blkvsc_req->pend_entry,
					      &blkdev->pending_list);
			}

			DPRINT_DBG(BLKVSC_DRV, "submitted blkvsc_req %p "
				   "start_sect %lu sect_count %ld (%lu %ld) "
				   "ret %d\n", blkvsc_req,
				   (unsigned long)blkvsc_req->sector_start,
				   blkvsc_req->sector_count,
				   (unsigned long)start_sector,
				   num_sectors, ret);
		}
	}

	return pending;
}

static void blkvsc_cmd_completion(struct hv_storvsc_request *request)
{
	struct blkvsc_request *blkvsc_req =
			(struct blkvsc_request *)request->Context;
	struct block_device_context *blkdev =
			(struct block_device_context *)blkvsc_req->dev;
	struct scsi_sense_hdr sense_hdr;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_cmd_completion() - req %p\n",
		   blkvsc_req);

	blkdev->num_outstanding_reqs--;

	if (blkvsc_req->request.Status)
		if (scsi_normalize_sense(blkvsc_req->sense_buffer,
					 SCSI_SENSE_BUFFERSIZE, &sense_hdr))
			scsi_print_sense_hdr("blkvsc", &sense_hdr);

	blkvsc_req->cond = 1;
	wake_up_interruptible(&blkvsc_req->wevent);
}

static void blkvsc_request_completion(struct hv_storvsc_request *request)
{
	struct blkvsc_request *blkvsc_req =
			(struct blkvsc_request *)request->Context;
	struct block_device_context *blkdev =
			(struct block_device_context *)blkvsc_req->dev;
	unsigned long flags;
	struct blkvsc_request *comp_req, *tmp;

	/* ASSERT(blkvsc_req->group); */

	DPRINT_DBG(BLKVSC_DRV, "blkdev %p blkvsc_req %p group %p type %s "
		   "sect_start %lu sect_count %ld len %d group outstd %d "
		   "total outstd %d\n",
		   blkdev, blkvsc_req, blkvsc_req->group,
		   (blkvsc_req->write) ? "WRITE" : "READ",
		   (unsigned long)blkvsc_req->sector_start,
		   blkvsc_req->sector_count,
		   blkvsc_req->request.DataBuffer.Length,
		   blkvsc_req->group->outstanding,
		   blkdev->num_outstanding_reqs);

	spin_lock_irqsave(&blkdev->lock, flags);

	blkdev->num_outstanding_reqs--;
	blkvsc_req->group->outstanding--;

	/*
	 * Only start processing when all the blkvsc_reqs are
	 * completed. This guarantees no out-of-order blkvsc_req
	 * completion when calling end_that_request_first()
	 */
	if (blkvsc_req->group->outstanding == 0) {
		list_for_each_entry_safe(comp_req, tmp,
					 &blkvsc_req->group->blkvsc_req_list,
					 req_entry) {
			DPRINT_DBG(BLKVSC_DRV, "completing blkvsc_req %p "
				   "sect_start %lu sect_count %ld\n",
				   comp_req,
				   (unsigned long)comp_req->sector_start,
				   comp_req->sector_count);

			list_del(&comp_req->req_entry);

			if (!__blk_end_request(comp_req->req,
				(!comp_req->request.Status ? 0 : -EIO),
				comp_req->sector_count * blkdev->sector_size)) {
				/*
				 * All the sectors have been xferred ie the
				 * request is done
				 */
				DPRINT_DBG(BLKVSC_DRV, "req %p COMPLETED\n",
					   comp_req->req);
				kmem_cache_free(blkdev->request_pool,
						comp_req->group);
			}

			kmem_cache_free(blkdev->request_pool, comp_req);
		}

		if (!blkdev->shutting_down) {
			blkvsc_do_pending_reqs(blkdev);
			blk_start_queue(blkdev->gd->queue);
			blkvsc_request(blkdev->gd->queue);
		}
	}

	spin_unlock_irqrestore(&blkdev->lock, flags);
}

static int blkvsc_cancel_pending_reqs(struct block_device_context *blkdev)
{
	struct blkvsc_request *pend_req, *tmp;
	struct blkvsc_request *comp_req, *tmp2;

	int ret = 0;

	DPRINT_DBG(BLKVSC_DRV, "blkvsc_cancel_pending_reqs()");

	/* Flush the pending list first */
	list_for_each_entry_safe(pend_req, tmp, &blkdev->pending_list,
				 pend_entry) {
		/*
		 * The pend_req could be part of a partially completed
		 * request. If so, complete those req first until we
		 * hit the pend_req
		 */
		list_for_each_entry_safe(comp_req, tmp2,
					 &pend_req->group->blkvsc_req_list,
					 req_entry) {
			DPRINT_DBG(BLKVSC_DRV, "completing blkvsc_req %p "
				   "sect_start %lu sect_count %ld\n",
				   comp_req,
				   (unsigned long) comp_req->sector_start,
				   comp_req->sector_count);

			if (comp_req == pend_req)
				break;

			list_del(&comp_req->req_entry);

			if (comp_req->req) {
				ret = __blk_end_request(comp_req->req,
					(!comp_req->request.Status ? 0 : -EIO),
					comp_req->sector_count *
					blkdev->sector_size);

				/* FIXME: shouldn't this do more than return? */
				if (ret)
					goto out;
			}

			kmem_cache_free(blkdev->request_pool, comp_req);
		}

		DPRINT_DBG(BLKVSC_DRV, "cancelling pending request - %p\n",
			   pend_req);

		list_del(&pend_req->pend_entry);

		list_del(&pend_req->req_entry);

		if (comp_req->req) {
			if (!__blk_end_request(pend_req->req, -EIO,
					       pend_req->sector_count *
					       blkdev->sector_size)) {
				/*
				 * All the sectors have been xferred ie the
				 * request is done
				 */
				DPRINT_DBG(BLKVSC_DRV,
					   "blkvsc_cancel_pending_reqs() - "
					   "req %p COMPLETED\n", pend_req->req);
				kmem_cache_free(blkdev->request_pool,
						pend_req->group);
			}
		}

		kmem_cache_free(blkdev->request_pool, pend_req);
	}

out:
	return ret;
}

static int blkvsc_do_pending_reqs(struct block_device_context *blkdev)
{
	struct blkvsc_request *pend_req, *tmp;
	int ret = 0;

	/* Flush the pending list first */
	list_for_each_entry_safe(pend_req, tmp, &blkdev->pending_list,
				 pend_entry) {
		DPRINT_DBG(BLKVSC_DRV, "working off pending_list - %p\n",
			   pend_req);

		ret = blkvsc_submit_request(pend_req,
					    blkvsc_request_completion);
		if (ret != 0)
			break;
		else
			list_del(&pend_req->pend_entry);
	}

	return ret;
}

static void blkvsc_request(struct request_queue *queue)
{
	struct block_device_context *blkdev = NULL;
	struct request *req;
	int ret = 0;

	DPRINT_DBG(BLKVSC_DRV, "- enter\n");
	while ((req = blk_peek_request(queue)) != NULL) {
		DPRINT_DBG(BLKVSC_DRV, "- req %p\n", req);

		blkdev = req->rq_disk->private_data;
		if (blkdev->shutting_down || req->cmd_type != REQ_TYPE_FS ||
		    blkdev->media_not_present) {
			__blk_end_request_cur(req, 0);
			continue;
		}

		ret = blkvsc_do_pending_reqs(blkdev);

		if (ret != 0) {
			DPRINT_DBG(BLKVSC_DRV,
				   "- stop queue - pending_list not empty\n");
			blk_stop_queue(queue);
			break;
		}

		blk_start_request(req);

		ret = blkvsc_do_request(blkdev, req);
		if (ret > 0) {
			DPRINT_DBG(BLKVSC_DRV, "- stop queue - no room\n");
			blk_stop_queue(queue);
			break;
		} else if (ret < 0) {
			DPRINT_DBG(BLKVSC_DRV, "- stop queue - no mem\n");
			blk_requeue_request(queue, req);
			blk_stop_queue(queue);
			break;
		}
	}
}

static int blkvsc_open(struct block_device *bdev, fmode_t mode)
{
	struct block_device_context *blkdev = bdev->bd_disk->private_data;

	DPRINT_DBG(BLKVSC_DRV, "- users %d disk %s\n", blkdev->users,
		   blkdev->gd->disk_name);

	mutex_lock(&blkvsc_mutex);
	spin_lock(&blkdev->lock);

	if (!blkdev->users && blkdev->device_type == DVD_TYPE) {
		spin_unlock(&blkdev->lock);
		check_disk_change(bdev);
		spin_lock(&blkdev->lock);
	}

	blkdev->users++;

	spin_unlock(&blkdev->lock);
	mutex_unlock(&blkvsc_mutex);
	return 0;
}

static int blkvsc_release(struct gendisk *disk, fmode_t mode)
{
	struct block_device_context *blkdev = disk->private_data;

	DPRINT_DBG(BLKVSC_DRV, "- users %d disk %s\n", blkdev->users,
		   blkdev->gd->disk_name);

	mutex_lock(&blkvsc_mutex);
	spin_lock(&blkdev->lock);
	if (blkdev->users == 1) {
		spin_unlock(&blkdev->lock);
		blkvsc_do_flush(blkdev);
		spin_lock(&blkdev->lock);
	}

	blkdev->users--;

	spin_unlock(&blkdev->lock);
	mutex_unlock(&blkvsc_mutex);
	return 0;
}

static int blkvsc_media_changed(struct gendisk *gd)
{
	DPRINT_DBG(BLKVSC_DRV, "- enter\n");
	return 1;
}

static int blkvsc_revalidate_disk(struct gendisk *gd)
{
	struct block_device_context *blkdev = gd->private_data;

	DPRINT_DBG(BLKVSC_DRV, "- enter\n");

	if (blkdev->device_type == DVD_TYPE) {
		blkvsc_do_read_capacity(blkdev);
		set_capacity(blkdev->gd, blkdev->capacity *
			    (blkdev->sector_size/512));
		blk_queue_logical_block_size(gd->queue, blkdev->sector_size);
	}
	return 0;
}

static int blkvsc_getgeo(struct block_device *bd, struct hd_geometry *hg)
{
	sector_t total_sectors = get_capacity(bd->bd_disk);
	sector_t cylinder_times_heads = 0;
	sector_t temp = 0;

	int sectors_per_track = 0;
	int heads = 0;
	int cylinders = 0;
	int rem = 0;

	if (total_sectors > (65535 * 16 * 255))
		total_sectors = (65535 * 16 * 255);

	if (total_sectors >= (65535 * 16 * 63)) {
		sectors_per_track = 255;
		heads = 16;

		cylinder_times_heads = total_sectors;
		/* sector_div stores the quotient in cylinder_times_heads */
		rem = sector_div(cylinder_times_heads, sectors_per_track);
	} else {
		sectors_per_track = 17;

		cylinder_times_heads = total_sectors;
		/* sector_div stores the quotient in cylinder_times_heads */
		rem = sector_div(cylinder_times_heads, sectors_per_track);

		temp = cylinder_times_heads + 1023;
		/* sector_div stores the quotient in temp */
		rem = sector_div(temp, 1024);

		heads = temp;

		if (heads < 4)
			heads = 4;


		if (cylinder_times_heads >= (heads * 1024) || (heads > 16)) {
			sectors_per_track = 31;
			heads = 16;

			cylinder_times_heads = total_sectors;
			/*
			 * sector_div stores the quotient in
			 * cylinder_times_heads
			 */
			rem = sector_div(cylinder_times_heads,
					 sectors_per_track);
		}

		if (cylinder_times_heads >= (heads * 1024)) {
			sectors_per_track = 63;
			heads = 16;

			cylinder_times_heads = total_sectors;
			/*
			 * sector_div stores the quotient in
			 * cylinder_times_heads
			 */
			rem = sector_div(cylinder_times_heads,
					 sectors_per_track);
		}
	}

	temp = cylinder_times_heads;
	/* sector_div stores the quotient in temp */
	rem = sector_div(temp, heads);
	cylinders = temp;

	hg->heads = heads;
	hg->sectors = sectors_per_track;
	hg->cylinders = cylinders;

	DPRINT_INFO(BLKVSC_DRV, "CHS (%d, %d, %d)", cylinders, heads,
		    sectors_per_track);

    return 0;
}

static int blkvsc_ioctl(struct block_device *bd, fmode_t mode,
			unsigned cmd, unsigned long argument)
{
/*	struct block_device_context *blkdev = bd->bd_disk->private_data; */
	int ret;

	switch (cmd) {
	/*
	 * TODO: I think there is certain format for HDIO_GET_IDENTITY rather
	 * than just a GUID. Commented it out for now.
	 */
#if 0
	case HDIO_GET_IDENTITY:
		DPRINT_INFO(BLKVSC_DRV, "HDIO_GET_IDENTITY\n");
		if (copy_to_user((void __user *)arg, blkdev->device_id,
				 blkdev->device_id_len))
			ret = -EFAULT;
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int __init blkvsc_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(sector_t) != 8);

	DPRINT_INFO(BLKVSC_DRV, "Blkvsc initializing....");

	ret = blkvsc_drv_init(BlkVscInitialize);

	return ret;
}

static void __exit blkvsc_exit(void)
{
	blkvsc_drv_exit();
}

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V virtual block driver");
module_init(blkvsc_init);
module_exit(blkvsc_exit);
