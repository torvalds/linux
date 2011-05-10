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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_dbg.h>
#include "hv_api.h"
#include "logging.h"
#include "version_info.h"
#include "vmbus.h"
#include "storvsc_api.h"


struct host_device_context {
	/* must be 1st field
	 * FIXME this is a bug */
	/* point back to our device context */
	struct hv_device *device_ctx;
	struct kmem_cache *request_pool;
	unsigned int port;
	unsigned char path;
	unsigned char target;
};

struct storvsc_cmd_request {
	struct list_head entry;
	struct scsi_cmnd *cmd;

	unsigned int bounce_sgl_count;
	struct scatterlist *bounce_sgl;

	struct hv_storvsc_request request;
	/* !!!DO NOT ADD ANYTHING BELOW HERE!!! */
	/* The extension buffer falls right here and is pointed to by
	 * request.Extension;
	 * Which sounds like a very bad design... */
};


/* Static decl */
static int storvsc_probe(struct device *dev);
static int storvsc_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *scmnd);
static int storvsc_device_alloc(struct scsi_device *);
static int storvsc_device_configure(struct scsi_device *);
static int storvsc_host_reset_handler(struct scsi_cmnd *scmnd);
static int storvsc_remove(struct device *dev);

static struct scatterlist *create_bounce_buffer(struct scatterlist *sgl,
						unsigned int sg_count,
						unsigned int len);
static void destroy_bounce_buffer(struct scatterlist *sgl,
				  unsigned int sg_count);
static int do_bounce_buffer(struct scatterlist *sgl, unsigned int sg_count);
static unsigned int copy_from_bounce_buffer(struct scatterlist *orig_sgl,
					    struct scatterlist *bounce_sgl,
					    unsigned int orig_sgl_count);
static unsigned int copy_to_bounce_buffer(struct scatterlist *orig_sgl,
					  struct scatterlist *bounce_sgl,
					  unsigned int orig_sgl_count);

static int storvsc_get_chs(struct scsi_device *sdev, struct block_device *bdev,
			   sector_t capacity, int *info);


static int storvsc_ringbuffer_size = STORVSC_RING_BUFFER_SIZE;
module_param(storvsc_ringbuffer_size, int, S_IRUGO);
MODULE_PARM_DESC(storvsc_ringbuffer_size, "Ring buffer size (bytes)");

/* The one and only one */
static struct storvsc_driver_object g_storvsc_drv;

/* Scsi driver */
static struct scsi_host_template scsi_driver = {
	.module	=		THIS_MODULE,
	.name =			"storvsc_host_t",
	.bios_param =		storvsc_get_chs,
	.queuecommand =		storvsc_queuecommand,
	.eh_host_reset_handler =	storvsc_host_reset_handler,
	.slave_alloc =		storvsc_device_alloc,
	.slave_configure =	storvsc_device_configure,
	.cmd_per_lun =		1,
	/* 64 max_queue * 1 target */
	.can_queue =		STORVSC_MAX_IO_REQUESTS*STORVSC_MAX_TARGETS,
	.this_id =		-1,
	/* no use setting to 0 since ll_blk_rw reset it to 1 */
	/* currently 32 */
	.sg_tablesize =		MAX_MULTIPAGE_BUFFER_COUNT,
	/*
	 * ENABLE_CLUSTERING allows mutiple physically contig bio_vecs to merge
	 * into 1 sg element. If set, we must limit the max_segment_size to
	 * PAGE_SIZE, otherwise we may get 1 sg element that represents
	 * multiple
	 */
	/* physically contig pfns (ie sg[x].length > PAGE_SIZE). */
	.use_clustering =	ENABLE_CLUSTERING,
	/* Make sure we dont get a sg segment crosses a page boundary */
	.dma_boundary =		PAGE_SIZE-1,
};


/*
 * storvsc_drv_init - StorVsc driver initialization.
 */
static int storvsc_drv_init(int (*drv_init)(struct hv_driver *drv))
{
	int ret;
	struct storvsc_driver_object *storvsc_drv_obj = &g_storvsc_drv;
	struct hv_driver *drv = &g_storvsc_drv.base;

	storvsc_drv_obj->ring_buffer_size = storvsc_ringbuffer_size;

	/* Callback to client driver to complete the initialization */
	drv_init(&storvsc_drv_obj->base);

	drv->priv = storvsc_drv_obj;

	DPRINT_INFO(STORVSC_DRV,
		    "request extension size %u, max outstanding reqs %u",
		    storvsc_drv_obj->request_ext_size,
		    storvsc_drv_obj->max_outstanding_req_per_channel);

	if (storvsc_drv_obj->max_outstanding_req_per_channel <
	    STORVSC_MAX_IO_REQUESTS) {
		DPRINT_ERR(STORVSC_DRV,
			   "The number of outstanding io requests (%d) "
			   "is larger than that supported (%d) internally.",
			   STORVSC_MAX_IO_REQUESTS,
			   storvsc_drv_obj->max_outstanding_req_per_channel);
		return -1;
	}

	drv->driver.name = storvsc_drv_obj->base.name;

	drv->driver.probe = storvsc_probe;
	drv->driver.remove = storvsc_remove;

	/* The driver belongs to vmbus */
	ret = vmbus_child_driver_register(&drv->driver);

	return ret;
}

static int storvsc_drv_exit_cb(struct device *dev, void *data)
{
	struct device **curr = (struct device **)data;
	*curr = dev;
	return 1; /* stop iterating */
}

static void storvsc_drv_exit(void)
{
	struct storvsc_driver_object *storvsc_drv_obj = &g_storvsc_drv;
	struct hv_driver *drv = &g_storvsc_drv.base;
	struct device *current_dev = NULL;
	int ret;

	while (1) {
		current_dev = NULL;

		/* Get the device */
		ret = driver_for_each_device(&drv->driver, NULL,
					     (void *) &current_dev,
					     storvsc_drv_exit_cb);

		if (ret)
			DPRINT_WARN(STORVSC_DRV,
				    "driver_for_each_device returned %d", ret);

		if (current_dev == NULL)
			break;

		/* Initiate removal from the top-down */
		device_unregister(current_dev);
	}

	if (storvsc_drv_obj->base.cleanup)
		storvsc_drv_obj->base.cleanup(&storvsc_drv_obj->base);

	vmbus_child_driver_unregister(&drv->driver);
	return;
}

/*
 * storvsc_probe - Add a new device for this driver
 */
static int storvsc_probe(struct device *device)
{
	int ret;
	struct hv_driver *drv =
				drv_to_hv_drv(device->driver);
	struct storvsc_driver_object *storvsc_drv_obj = drv->priv;
	struct hv_device *device_obj = device_to_hv_device(device);
	struct Scsi_Host *host;
	struct host_device_context *host_device_ctx;
	struct storvsc_device_info device_info;

	if (!storvsc_drv_obj->base.dev_add)
		return -1;

	host = scsi_host_alloc(&scsi_driver,
			       sizeof(struct host_device_context));
	if (!host) {
		DPRINT_ERR(STORVSC_DRV, "unable to allocate scsi host object");
		return -ENOMEM;
	}

	dev_set_drvdata(device, host);

	host_device_ctx = (struct host_device_context *)host->hostdata;
	memset(host_device_ctx, 0, sizeof(struct host_device_context));

	host_device_ctx->port = host->host_no;
	host_device_ctx->device_ctx = device_obj;

	host_device_ctx->request_pool =
				kmem_cache_create(dev_name(&device_obj->device),
					sizeof(struct storvsc_cmd_request) +
					storvsc_drv_obj->request_ext_size, 0,
					SLAB_HWCACHE_ALIGN, NULL);

	if (!host_device_ctx->request_pool) {
		scsi_host_put(host);
		return -ENOMEM;
	}

	device_info.port_number = host->host_no;
	/* Call to the vsc driver to add the device */
	ret = storvsc_drv_obj->base.dev_add(device_obj,
						(void *)&device_info);
	if (ret != 0) {
		DPRINT_ERR(STORVSC_DRV, "unable to add scsi vsc device");
		kmem_cache_destroy(host_device_ctx->request_pool);
		scsi_host_put(host);
		return -1;
	}

	/* host_device_ctx->port = device_info.PortNumber; */
	host_device_ctx->path = device_info.path_id;
	host_device_ctx->target = device_info.target_id;

	/* max # of devices per target */
	host->max_lun = STORVSC_MAX_LUNS_PER_TARGET;
	/* max # of targets per channel */
	host->max_id = STORVSC_MAX_TARGETS;
	/* max # of channels */
	host->max_channel = STORVSC_MAX_CHANNELS - 1;

	/* Register the HBA and start the scsi bus scan */
	ret = scsi_add_host(host, device);
	if (ret != 0) {
		DPRINT_ERR(STORVSC_DRV, "unable to add scsi host device");

		storvsc_drv_obj->base.dev_rm(device_obj);

		kmem_cache_destroy(host_device_ctx->request_pool);
		scsi_host_put(host);
		return -1;
	}

	scsi_scan_host(host);
	return ret;
}

/*
 * storvsc_remove - Callback when our device is removed
 */
static int storvsc_remove(struct device *device)
{
	int ret;
	struct hv_driver *drv =
			drv_to_hv_drv(device->driver);
	struct storvsc_driver_object *storvsc_drv_obj = drv->priv;
	struct hv_device *device_obj = device_to_hv_device(device);
	struct Scsi_Host *host = dev_get_drvdata(device);
	struct host_device_context *host_device_ctx =
			(struct host_device_context *)host->hostdata;


	if (!storvsc_drv_obj->base.dev_rm)
		return -1;

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	ret = storvsc_drv_obj->base.dev_rm(device_obj);
	if (ret != 0) {
		/* TODO: */
		DPRINT_ERR(STORVSC, "unable to remove vsc device (ret %d)",
			   ret);
	}

	if (host_device_ctx->request_pool) {
		kmem_cache_destroy(host_device_ctx->request_pool);
		host_device_ctx->request_pool = NULL;
	}

	DPRINT_INFO(STORVSC, "removing host adapter (%p)...", host);
	scsi_remove_host(host);

	DPRINT_INFO(STORVSC, "releasing host adapter (%p)...", host);
	scsi_host_put(host);
	return ret;
}

/*
 * storvsc_commmand_completion - Command completion processing
 */
static void storvsc_commmand_completion(struct hv_storvsc_request *request)
{
	struct storvsc_cmd_request *cmd_request =
		(struct storvsc_cmd_request *)request->context;
	struct scsi_cmnd *scmnd = cmd_request->cmd;
	struct host_device_context *host_device_ctx =
		(struct host_device_context *)scmnd->device->host->hostdata;
	void (*scsi_done_fn)(struct scsi_cmnd *);
	struct scsi_sense_hdr sense_hdr;

	/* ASSERT(request == &cmd_request->request); */
	/* ASSERT(scmnd); */
	/* ASSERT((unsigned long)scmnd->host_scribble == */
	/*        (unsigned long)cmd_request); */
	/* ASSERT(scmnd->scsi_done); */

	if (cmd_request->bounce_sgl_count) {
		/* using bounce buffer */
		/* printk("copy_from_bounce_buffer\n"); */

		/* FIXME: We can optimize on writes by just skipping this */
		copy_from_bounce_buffer(scsi_sglist(scmnd),
					cmd_request->bounce_sgl,
					scsi_sg_count(scmnd));
		destroy_bounce_buffer(cmd_request->bounce_sgl,
				      cmd_request->bounce_sgl_count);
	}

	scmnd->result = request->status;

	if (scmnd->result) {
		if (scsi_normalize_sense(scmnd->sense_buffer,
				request->sense_buffer_size, &sense_hdr))
			scsi_print_sense_hdr("storvsc", &sense_hdr);
	}

	/* ASSERT(request->BytesXfer <= request->data_buffer.Length); */
	scsi_set_resid(scmnd,
		request->data_buffer.len - request->bytes_xfer);

	scsi_done_fn = scmnd->scsi_done;

	scmnd->host_scribble = NULL;
	scmnd->scsi_done = NULL;

	/* !!DO NOT MODIFY the scmnd after this call */
	scsi_done_fn(scmnd);

	kmem_cache_free(host_device_ctx->request_pool, cmd_request);
}

static int do_bounce_buffer(struct scatterlist *sgl, unsigned int sg_count)
{
	int i;

	/* No need to check */
	if (sg_count < 2)
		return -1;

	/* We have at least 2 sg entries */
	for (i = 0; i < sg_count; i++) {
		if (i == 0) {
			/* make sure 1st one does not have hole */
			if (sgl[i].offset + sgl[i].length != PAGE_SIZE)
				return i;
		} else if (i == sg_count - 1) {
			/* make sure last one does not have hole */
			if (sgl[i].offset != 0)
				return i;
		} else {
			/* make sure no hole in the middle */
			if (sgl[i].length != PAGE_SIZE || sgl[i].offset != 0)
				return i;
		}
	}
	return -1;
}

static struct scatterlist *create_bounce_buffer(struct scatterlist *sgl,
						unsigned int sg_count,
						unsigned int len)
{
	int i;
	int num_pages;
	struct scatterlist *bounce_sgl;
	struct page *page_buf;

	num_pages = ALIGN(len, PAGE_SIZE) >> PAGE_SHIFT;

	bounce_sgl = kcalloc(num_pages, sizeof(struct scatterlist), GFP_ATOMIC);
	if (!bounce_sgl)
		return NULL;

	for (i = 0; i < num_pages; i++) {
		page_buf = alloc_page(GFP_ATOMIC);
		if (!page_buf)
			goto cleanup;
		sg_set_page(&bounce_sgl[i], page_buf, 0, 0);
	}

	return bounce_sgl;

cleanup:
	destroy_bounce_buffer(bounce_sgl, num_pages);
	return NULL;
}

static void destroy_bounce_buffer(struct scatterlist *sgl,
				  unsigned int sg_count)
{
	int i;
	struct page *page_buf;

	for (i = 0; i < sg_count; i++) {
		page_buf = sg_page((&sgl[i]));
		if (page_buf != NULL)
			__free_page(page_buf);
	}

	kfree(sgl);
}

/* Assume the bounce_sgl has enough room ie using the create_bounce_buffer() */
static unsigned int copy_to_bounce_buffer(struct scatterlist *orig_sgl,
					  struct scatterlist *bounce_sgl,
					  unsigned int orig_sgl_count)
{
	int i;
	int j = 0;
	unsigned long src, dest;
	unsigned int srclen, destlen, copylen;
	unsigned int total_copied = 0;
	unsigned long bounce_addr = 0;
	unsigned long src_addr = 0;
	unsigned long flags;

	local_irq_save(flags);

	for (i = 0; i < orig_sgl_count; i++) {
		src_addr = (unsigned long)kmap_atomic(sg_page((&orig_sgl[i])),
				KM_IRQ0) + orig_sgl[i].offset;
		src = src_addr;
		srclen = orig_sgl[i].length;

		/* ASSERT(orig_sgl[i].offset + orig_sgl[i].length <= PAGE_SIZE); */

		if (bounce_addr == 0)
			bounce_addr = (unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])), KM_IRQ0);

		while (srclen) {
			/* assume bounce offset always == 0 */
			dest = bounce_addr + bounce_sgl[j].length;
			destlen = PAGE_SIZE - bounce_sgl[j].length;

			copylen = min(srclen, destlen);
			memcpy((void *)dest, (void *)src, copylen);

			total_copied += copylen;
			bounce_sgl[j].length += copylen;
			srclen -= copylen;
			src += copylen;

			if (bounce_sgl[j].length == PAGE_SIZE) {
				/* full..move to next entry */
				kunmap_atomic((void *)bounce_addr, KM_IRQ0);
				j++;

				/* if we need to use another bounce buffer */
				if (srclen || i != orig_sgl_count - 1)
					bounce_addr = (unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])), KM_IRQ0);
			} else if (srclen == 0 && i == orig_sgl_count - 1) {
				/* unmap the last bounce that is < PAGE_SIZE */
				kunmap_atomic((void *)bounce_addr, KM_IRQ0);
			}
		}

		kunmap_atomic((void *)(src_addr - orig_sgl[i].offset), KM_IRQ0);
	}

	local_irq_restore(flags);

	return total_copied;
}

/* Assume the original sgl has enough room */
static unsigned int copy_from_bounce_buffer(struct scatterlist *orig_sgl,
					    struct scatterlist *bounce_sgl,
					    unsigned int orig_sgl_count)
{
	int i;
	int j = 0;
	unsigned long src, dest;
	unsigned int srclen, destlen, copylen;
	unsigned int total_copied = 0;
	unsigned long bounce_addr = 0;
	unsigned long dest_addr = 0;
	unsigned long flags;

	local_irq_save(flags);

	for (i = 0; i < orig_sgl_count; i++) {
		dest_addr = (unsigned long)kmap_atomic(sg_page((&orig_sgl[i])),
					KM_IRQ0) + orig_sgl[i].offset;
		dest = dest_addr;
		destlen = orig_sgl[i].length;
		/* ASSERT(orig_sgl[i].offset + orig_sgl[i].length <= PAGE_SIZE); */

		if (bounce_addr == 0)
			bounce_addr = (unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])), KM_IRQ0);

		while (destlen) {
			src = bounce_addr + bounce_sgl[j].offset;
			srclen = bounce_sgl[j].length - bounce_sgl[j].offset;

			copylen = min(srclen, destlen);
			memcpy((void *)dest, (void *)src, copylen);

			total_copied += copylen;
			bounce_sgl[j].offset += copylen;
			destlen -= copylen;
			dest += copylen;

			if (bounce_sgl[j].offset == bounce_sgl[j].length) {
				/* full */
				kunmap_atomic((void *)bounce_addr, KM_IRQ0);
				j++;

				/* if we need to use another bounce buffer */
				if (destlen || i != orig_sgl_count - 1)
					bounce_addr = (unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])), KM_IRQ0);
			} else if (destlen == 0 && i == orig_sgl_count - 1) {
				/* unmap the last bounce that is < PAGE_SIZE */
				kunmap_atomic((void *)bounce_addr, KM_IRQ0);
			}
		}

		kunmap_atomic((void *)(dest_addr - orig_sgl[i].offset),
			      KM_IRQ0);
	}

	local_irq_restore(flags);

	return total_copied;
}

/*
 * storvsc_queuecommand - Initiate command processing
 */
static int storvsc_queuecommand_lck(struct scsi_cmnd *scmnd,
				void (*done)(struct scsi_cmnd *))
{
	int ret;
	struct host_device_context *host_device_ctx =
		(struct host_device_context *)scmnd->device->host->hostdata;
	struct hv_device *device_ctx = host_device_ctx->device_ctx;
	struct hv_driver *drv =
		drv_to_hv_drv(device_ctx->device.driver);
	struct storvsc_driver_object *storvsc_drv_obj = drv->priv;
	struct hv_storvsc_request *request;
	struct storvsc_cmd_request *cmd_request;
	unsigned int request_size = 0;
	int i;
	struct scatterlist *sgl;
	unsigned int sg_count = 0;

	DPRINT_DBG(STORVSC_DRV, "scmnd %p dir %d, use_sg %d buf %p len %d "
		   "queue depth %d tagged %d", scmnd, scmnd->sc_data_direction,
		   scsi_sg_count(scmnd), scsi_sglist(scmnd),
		   scsi_bufflen(scmnd), scmnd->device->queue_depth,
		   scmnd->device->tagged_supported);

	/* If retrying, no need to prep the cmd */
	if (scmnd->host_scribble) {
		/* ASSERT(scmnd->scsi_done != NULL); */

		cmd_request =
			(struct storvsc_cmd_request *)scmnd->host_scribble;
		DPRINT_INFO(STORVSC_DRV, "retrying scmnd %p cmd_request %p",
			    scmnd, cmd_request);

		goto retry_request;
	}

	/* ASSERT(scmnd->scsi_done == NULL); */
	/* ASSERT(scmnd->host_scribble == NULL); */

	scmnd->scsi_done = done;

	request_size = sizeof(struct storvsc_cmd_request);

	cmd_request = kmem_cache_alloc(host_device_ctx->request_pool,
				       GFP_ATOMIC);
	if (!cmd_request) {
		DPRINT_ERR(STORVSC_DRV, "scmnd (%p) - unable to allocate "
			   "storvsc_cmd_request...marking queue busy", scmnd);
		scmnd->scsi_done = NULL;
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	/* Setup the cmd request */
	cmd_request->bounce_sgl_count = 0;
	cmd_request->bounce_sgl = NULL;
	cmd_request->cmd = scmnd;

	scmnd->host_scribble = (unsigned char *)cmd_request;

	request = &cmd_request->request;

	request->extension =
		(void *)((unsigned long)cmd_request + request_size);
	DPRINT_DBG(STORVSC_DRV, "req %p size %d ext %d", request, request_size,
		   storvsc_drv_obj->request_ext_size);

	/* Build the SRB */
	switch (scmnd->sc_data_direction) {
	case DMA_TO_DEVICE:
		request->type = WRITE_TYPE;
		break;
	case DMA_FROM_DEVICE:
		request->type = READ_TYPE;
		break;
	default:
		request->type = UNKNOWN_TYPE;
		break;
	}

	request->on_io_completion = storvsc_commmand_completion;
	request->context = cmd_request;/* scmnd; */

	/* request->PortId = scmnd->device->channel; */
	request->host = host_device_ctx->port;
	request->bus = scmnd->device->channel;
	request->target_id = scmnd->device->id;
	request->lun_id = scmnd->device->lun;

	/* ASSERT(scmnd->cmd_len <= 16); */
	request->cdb_len = scmnd->cmd_len;
	request->cdb = scmnd->cmnd;

	request->sense_buffer = scmnd->sense_buffer;
	request->sense_buffer_size = SCSI_SENSE_BUFFERSIZE;


	request->data_buffer.len = scsi_bufflen(scmnd);
	if (scsi_sg_count(scmnd)) {
		sgl = (struct scatterlist *)scsi_sglist(scmnd);
		sg_count = scsi_sg_count(scmnd);

		/* check if we need to bounce the sgl */
		if (do_bounce_buffer(sgl, scsi_sg_count(scmnd)) != -1) {
			DPRINT_INFO(STORVSC_DRV,
				    "need to bounce buffer for this scmnd %p",
				    scmnd);
			cmd_request->bounce_sgl =
				create_bounce_buffer(sgl, scsi_sg_count(scmnd),
						     scsi_bufflen(scmnd));
			if (!cmd_request->bounce_sgl) {
				DPRINT_ERR(STORVSC_DRV,
					   "unable to create bounce buffer for "
					   "this scmnd %p", scmnd);

				scmnd->scsi_done = NULL;
				scmnd->host_scribble = NULL;
				kmem_cache_free(host_device_ctx->request_pool,
						cmd_request);

				return SCSI_MLQUEUE_HOST_BUSY;
			}

			cmd_request->bounce_sgl_count =
				ALIGN(scsi_bufflen(scmnd), PAGE_SIZE) >>
					PAGE_SHIFT;

			/*
			 * FIXME: We can optimize on reads by just skipping
			 * this
			 */
			copy_to_bounce_buffer(sgl, cmd_request->bounce_sgl,
					      scsi_sg_count(scmnd));

			sgl = cmd_request->bounce_sgl;
			sg_count = cmd_request->bounce_sgl_count;
		}

		request->data_buffer.offset = sgl[0].offset;

		for (i = 0; i < sg_count; i++) {
			DPRINT_DBG(STORVSC_DRV, "sgl[%d] len %d offset %d\n",
				   i, sgl[i].length, sgl[i].offset);
			request->data_buffer.pfn_array[i] =
				page_to_pfn(sg_page((&sgl[i])));
		}
	} else if (scsi_sglist(scmnd)) {
		/* ASSERT(scsi_bufflen(scmnd) <= PAGE_SIZE); */
		request->data_buffer.offset =
			virt_to_phys(scsi_sglist(scmnd)) & (PAGE_SIZE-1);
		request->data_buffer.pfn_array[0] =
			virt_to_phys(scsi_sglist(scmnd)) >> PAGE_SHIFT;
	}

retry_request:
	/* Invokes the vsc to start an IO */
	ret = storvsc_drv_obj->on_io_request(device_ctx,
					   &cmd_request->request);
	if (ret == -1) {
		/* no more space */
		DPRINT_ERR(STORVSC_DRV,
			   "scmnd (%p) - queue FULL...marking queue busy",
			   scmnd);

		if (cmd_request->bounce_sgl_count) {
			/*
			 * FIXME: We can optimize on writes by just skipping
			 * this
			 */
			copy_from_bounce_buffer(scsi_sglist(scmnd),
						cmd_request->bounce_sgl,
						scsi_sg_count(scmnd));
			destroy_bounce_buffer(cmd_request->bounce_sgl,
					      cmd_request->bounce_sgl_count);
		}

		kmem_cache_free(host_device_ctx->request_pool, cmd_request);

		scmnd->scsi_done = NULL;
		scmnd->host_scribble = NULL;

		ret = SCSI_MLQUEUE_DEVICE_BUSY;
	}

	return ret;
}

static DEF_SCSI_QCMD(storvsc_queuecommand)

static int storvsc_merge_bvec(struct request_queue *q,
			      struct bvec_merge_data *bmd, struct bio_vec *bvec)
{
	/* checking done by caller. */
	return bvec->bv_len;
}

/*
 * storvsc_device_configure - Configure the specified scsi device
 */
static int storvsc_device_alloc(struct scsi_device *sdevice)
{
	DPRINT_DBG(STORVSC_DRV, "sdev (%p) - setting device flag to %d",
		   sdevice, BLIST_SPARSELUN);
	/*
	 * This enables luns to be located sparsely. Otherwise, we may not
	 * discovered them.
	 */
	sdevice->sdev_bflags |= BLIST_SPARSELUN | BLIST_LARGELUN;
	return 0;
}

static int storvsc_device_configure(struct scsi_device *sdevice)
{
	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - curr queue depth %d", sdevice,
		    sdevice->queue_depth);

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - setting queue depth to %d",
		    sdevice, STORVSC_MAX_IO_REQUESTS);
	scsi_adjust_queue_depth(sdevice, MSG_SIMPLE_TAG,
				STORVSC_MAX_IO_REQUESTS);

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - setting max segment size to %ld",
		    sdevice, PAGE_SIZE);
	blk_queue_max_segment_size(sdevice->request_queue, PAGE_SIZE);

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - adding merge bio vec routine",
		    sdevice);
	blk_queue_merge_bvec(sdevice->request_queue, storvsc_merge_bvec);

	blk_queue_bounce_limit(sdevice->request_queue, BLK_BOUNCE_ANY);
	/* sdevice->timeout = (2000 * HZ);//(75 * HZ); */

	return 0;
}

/*
 * storvsc_host_reset_handler - Reset the scsi HBA
 */
static int storvsc_host_reset_handler(struct scsi_cmnd *scmnd)
{
	int ret;
	struct host_device_context *host_device_ctx =
		(struct host_device_context *)scmnd->device->host->hostdata;
	struct hv_device *device_ctx = host_device_ctx->device_ctx;

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) dev obj (%p) - host resetting...",
		    scmnd->device, device_ctx);

	/* Invokes the vsc to reset the host/bus */
	ret = stor_vsc_on_host_reset(device_ctx);
	if (ret != 0)
		return ret;

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) dev obj (%p) - host reseted",
		    scmnd->device, device_ctx);

	return ret;
}

static int storvsc_get_chs(struct scsi_device *sdev, struct block_device * bdev,
			   sector_t capacity, int *info)
{
	sector_t total_sectors = capacity;
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

	info[0] = heads;
	info[1] = sectors_per_track;
	info[2] = cylinders;

	DPRINT_INFO(STORVSC_DRV, "CHS (%d, %d, %d)", cylinders, heads,
		    sectors_per_track);

    return 0;
}

static int __init storvsc_init(void)
{
	int ret;

	DPRINT_INFO(STORVSC_DRV, "Storvsc initializing....");
	ret = storvsc_drv_init(stor_vsc_initialize);
	return ret;
}

static void __exit storvsc_exit(void)
{
	storvsc_drv_exit();
}

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V virtual storage driver");
module_init(storvsc_init);
module_exit(storvsc_exit);
