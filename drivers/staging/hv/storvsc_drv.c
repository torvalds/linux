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
 *   K. Y. Srinivasan <kys@microsoft.com>
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

#include "hyperv.h"
#include "hyperv_storage.h"

static int storvsc_ringbuffer_size = STORVSC_RING_BUFFER_SIZE;

module_param(storvsc_ringbuffer_size, int, S_IRUGO);
MODULE_PARM_DESC(storvsc_ringbuffer_size, "Ring buffer size (bytes)");

static const char *driver_name = "storvsc";

/* {ba6163d9-04a1-4d29-b605-72e2ffb1dc7f} */
static const struct hv_guid gStorVscDeviceType = {
	.data = {
		0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
		0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f
	}
};

struct hv_host_device {
	struct hv_device *dev;
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
};


static int storvsc_device_alloc(struct scsi_device *sdevice)
{
	/*
	 * This enables luns to be located sparsely. Otherwise, we may not
	 * discovered them.
	 */
	sdevice->sdev_bflags |= BLIST_SPARSELUN | BLIST_LARGELUN;
	return 0;
}

static int storvsc_merge_bvec(struct request_queue *q,
			      struct bvec_merge_data *bmd, struct bio_vec *bvec)
{
	/* checking done by caller. */
	return bvec->bv_len;
}

static int storvsc_device_configure(struct scsi_device *sdevice)
{
	scsi_adjust_queue_depth(sdevice, MSG_SIMPLE_TAG,
				STORVSC_MAX_IO_REQUESTS);

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - setting max segment size to %ld",
		    sdevice, PAGE_SIZE);
	blk_queue_max_segment_size(sdevice->request_queue, PAGE_SIZE);

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) - adding merge bio vec routine",
		    sdevice);
	blk_queue_merge_bvec(sdevice->request_queue, storvsc_merge_bvec);

	blk_queue_bounce_limit(sdevice->request_queue, BLK_BOUNCE_ANY);

	return 0;
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

		if (bounce_addr == 0)
			bounce_addr =
			(unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])),
							KM_IRQ0);

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
					bounce_addr =
					(unsigned long)kmap_atomic(
					sg_page((&bounce_sgl[j])), KM_IRQ0);
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

		if (bounce_addr == 0)
			bounce_addr =
			(unsigned long)kmap_atomic(sg_page((&bounce_sgl[j])),
						KM_IRQ0);

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
					bounce_addr =
					(unsigned long)kmap_atomic(
					sg_page((&bounce_sgl[j])), KM_IRQ0);

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


/*
 * storvsc_remove - Callback when our device is removed
 */
static int storvsc_remove(struct hv_device *dev)
{
	struct Scsi_Host *host = dev_get_drvdata(&dev->device);
	struct hv_host_device *host_dev =
			(struct hv_host_device *)host->hostdata;

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	storvsc_dev_remove(dev);

	if (host_dev->request_pool) {
		kmem_cache_destroy(host_dev->request_pool);
		host_dev->request_pool = NULL;
	}

	DPRINT_INFO(STORVSC, "removing host adapter (%p)...", host);
	scsi_remove_host(host);

	DPRINT_INFO(STORVSC, "releasing host adapter (%p)...", host);
	scsi_host_put(host);
	return 0;
}


static int storvsc_get_chs(struct scsi_device *sdev, struct block_device * bdev,
			   sector_t capacity, int *info)
{
	sector_t nsect = capacity;
	sector_t cylinders = nsect;
	int heads, sectors_pt;

	/*
	 * We are making up these values; let us keep it simple.
	 */
	heads = 0xff;
	sectors_pt = 0x3f;      /* Sectors per track */
	sector_div(cylinders, heads * sectors_pt);
	if ((sector_t)(cylinders + 1) * heads * sectors_pt < nsect)
		cylinders = 0xffff;

	info[0] = heads;
	info[1] = sectors_pt;
	info[2] = (int)cylinders;

	DPRINT_INFO(STORVSC_DRV, "CHS (%d, %d, %d)", (int)cylinders, heads,
			sectors_pt);

	return 0;
}

static int storvsc_host_reset(struct hv_device *device)
{
	struct storvsc_device *stor_device;
	struct hv_storvsc_request *request;
	struct vstor_packet *vstor_packet;
	int ret, t;

	DPRINT_INFO(STORVSC, "resetting host adapter...");

	stor_device = get_stor_device(device);
	if (!stor_device)
		return -1;

	request = &stor_device->reset_request;
	vstor_packet = &request->vstor_packet;

	init_completion(&request->wait_event);

	vstor_packet->operation = VSTOR_OPERATION_RESET_BUS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->vm_srb.path_id = stor_device->path_id;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)&stor_device->reset_request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	DPRINT_INFO(STORVSC, "host adapter reset completed");

	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 */

cleanup:
	put_stor_device(device);
	return ret;
}


/*
 * storvsc_host_reset_handler - Reset the scsi HBA
 */
static int storvsc_host_reset_handler(struct scsi_cmnd *scmnd)
{
	int ret;
	struct hv_host_device *host_dev =
		(struct hv_host_device *)scmnd->device->host->hostdata;
	struct hv_device *dev = host_dev->dev;

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) dev obj (%p) - host resetting...",
		    scmnd->device, dev);

	/* Invokes the vsc to reset the host/bus */
	ret = storvsc_host_reset(dev);
	if (ret != 0)
		return ret;

	DPRINT_INFO(STORVSC_DRV, "sdev (%p) dev obj (%p) - host reseted",
		    scmnd->device, dev);

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
	struct hv_host_device *host_dev =
		(struct hv_host_device *)scmnd->device->host->hostdata;
	void (*scsi_done_fn)(struct scsi_cmnd *);
	struct scsi_sense_hdr sense_hdr;
	struct vmscsi_request *vm_srb;

	if (cmd_request->bounce_sgl_count) {

		/* FIXME: We can optimize on writes by just skipping this */
		copy_from_bounce_buffer(scsi_sglist(scmnd),
					cmd_request->bounce_sgl,
					scsi_sg_count(scmnd));
		destroy_bounce_buffer(cmd_request->bounce_sgl,
				      cmd_request->bounce_sgl_count);
	}

	vm_srb = &request->vstor_packet.vm_srb;
	scmnd->result = vm_srb->scsi_status;

	if (scmnd->result) {
		if (scsi_normalize_sense(scmnd->sense_buffer,
				SCSI_SENSE_BUFFERSIZE, &sense_hdr))
			scsi_print_sense_hdr("storvsc", &sense_hdr);
	}

	scsi_set_resid(scmnd,
		request->data_buffer.len -
		vm_srb->data_transfer_length);

	scsi_done_fn = scmnd->scsi_done;

	scmnd->host_scribble = NULL;
	scmnd->scsi_done = NULL;

	/* !!DO NOT MODIFY the scmnd after this call */
	scsi_done_fn(scmnd);

	kmem_cache_free(host_dev->request_pool, cmd_request);
}


/*
 * storvsc_queuecommand - Initiate command processing
 */
static int storvsc_queuecommand_lck(struct scsi_cmnd *scmnd,
				void (*done)(struct scsi_cmnd *))
{
	int ret;
	struct hv_host_device *host_dev =
		(struct hv_host_device *)scmnd->device->host->hostdata;
	struct hv_device *dev = host_dev->dev;
	struct hv_storvsc_request *request;
	struct storvsc_cmd_request *cmd_request;
	unsigned int request_size = 0;
	int i;
	struct scatterlist *sgl;
	unsigned int sg_count = 0;
	struct vmscsi_request *vm_srb;


	/* If retrying, no need to prep the cmd */
	if (scmnd->host_scribble) {

		cmd_request =
			(struct storvsc_cmd_request *)scmnd->host_scribble;
		DPRINT_INFO(STORVSC_DRV, "retrying scmnd %p cmd_request %p",
			    scmnd, cmd_request);

		goto retry_request;
	}

	scmnd->scsi_done = done;

	request_size = sizeof(struct storvsc_cmd_request);

	cmd_request = kmem_cache_zalloc(host_dev->request_pool,
				       GFP_ATOMIC);
	if (!cmd_request) {
		scmnd->scsi_done = NULL;
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	/* Setup the cmd request */
	cmd_request->bounce_sgl_count = 0;
	cmd_request->bounce_sgl = NULL;
	cmd_request->cmd = scmnd;

	scmnd->host_scribble = (unsigned char *)cmd_request;

	request = &cmd_request->request;
	vm_srb = &request->vstor_packet.vm_srb;


	/* Build the SRB */
	switch (scmnd->sc_data_direction) {
	case DMA_TO_DEVICE:
		vm_srb->data_in = WRITE_TYPE;
		break;
	case DMA_FROM_DEVICE:
		vm_srb->data_in = READ_TYPE;
		break;
	default:
		vm_srb->data_in = UNKNOWN_TYPE;
		break;
	}

	request->on_io_completion = storvsc_commmand_completion;
	request->context = cmd_request;/* scmnd; */

	vm_srb->port_number = host_dev->port;
	vm_srb->path_id = scmnd->device->channel;
	vm_srb->target_id = scmnd->device->id;
	vm_srb->lun = scmnd->device->lun;

	vm_srb->cdb_length = scmnd->cmd_len;

	memcpy(vm_srb->cdb, scmnd->cmnd, vm_srb->cdb_length);

	request->sense_buffer = scmnd->sense_buffer;


	request->data_buffer.len = scsi_bufflen(scmnd);
	if (scsi_sg_count(scmnd)) {
		sgl = (struct scatterlist *)scsi_sglist(scmnd);
		sg_count = scsi_sg_count(scmnd);

		/* check if we need to bounce the sgl */
		if (do_bounce_buffer(sgl, scsi_sg_count(scmnd)) != -1) {
			cmd_request->bounce_sgl =
				create_bounce_buffer(sgl, scsi_sg_count(scmnd),
						     scsi_bufflen(scmnd));
			if (!cmd_request->bounce_sgl) {
				scmnd->scsi_done = NULL;
				scmnd->host_scribble = NULL;
				kmem_cache_free(host_dev->request_pool,
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

		for (i = 0; i < sg_count; i++)
			request->data_buffer.pfn_array[i] =
				page_to_pfn(sg_page((&sgl[i])));

	} else if (scsi_sglist(scmnd)) {
		request->data_buffer.offset =
			virt_to_phys(scsi_sglist(scmnd)) & (PAGE_SIZE-1);
		request->data_buffer.pfn_array[0] =
			virt_to_phys(scsi_sglist(scmnd)) >> PAGE_SHIFT;
	}

retry_request:
	/* Invokes the vsc to start an IO */
	ret = storvsc_do_io(dev, &cmd_request->request);

	if (ret == -1) {
		/* no more space */

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

		kmem_cache_free(host_dev->request_pool, cmd_request);

		scmnd->scsi_done = NULL;
		scmnd->host_scribble = NULL;

		ret = SCSI_MLQUEUE_DEVICE_BUSY;
	}

	return ret;
}

static DEF_SCSI_QCMD(storvsc_queuecommand)


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
 * storvsc_probe - Add a new device for this driver
 */

static int storvsc_probe(struct hv_device *device)
{
	int ret;
	struct Scsi_Host *host;
	struct hv_host_device *host_dev;
	struct storvsc_device_info device_info;

	host = scsi_host_alloc(&scsi_driver,
			       sizeof(struct hv_host_device));
	if (!host)
		return -ENOMEM;

	dev_set_drvdata(&device->device, host);

	host_dev = (struct hv_host_device *)host->hostdata;
	memset(host_dev, 0, sizeof(struct hv_host_device));

	host_dev->port = host->host_no;
	host_dev->dev = device;

	host_dev->request_pool =
				kmem_cache_create(dev_name(&device->device),
					sizeof(struct storvsc_cmd_request), 0,
					SLAB_HWCACHE_ALIGN, NULL);

	if (!host_dev->request_pool) {
		scsi_host_put(host);
		return -ENOMEM;
	}

	device_info.port_number = host->host_no;
	device_info.ring_buffer_size  = storvsc_ringbuffer_size;
	/* Call to the vsc driver to add the device */
	ret = storvsc_dev_add(device, (void *)&device_info);

	if (ret != 0) {
		kmem_cache_destroy(host_dev->request_pool);
		scsi_host_put(host);
		return -1;
	}

	host_dev->path = device_info.path_id;
	host_dev->target = device_info.target_id;

	/* max # of devices per target */
	host->max_lun = STORVSC_MAX_LUNS_PER_TARGET;
	/* max # of targets per channel */
	host->max_id = STORVSC_MAX_TARGETS;
	/* max # of channels */
	host->max_channel = STORVSC_MAX_CHANNELS - 1;

	/* Register the HBA and start the scsi bus scan */
	ret = scsi_add_host(host, &device->device);
	if (ret != 0) {

		storvsc_dev_remove(device);

		kmem_cache_destroy(host_dev->request_pool);
		scsi_host_put(host);
		return -1;
	}

	scsi_scan_host(host);
	return ret;
}

/* The one and only one */

static struct hv_driver storvsc_drv = {
	.probe = storvsc_probe,
	.remove = storvsc_remove,
};


/*
 * storvsc_drv_init - StorVsc driver initialization.
 */
static int storvsc_drv_init(void)
{
	int ret;
	struct hv_driver *drv = &storvsc_drv;
	u32 max_outstanding_req_per_channel;

	/*
	 * Divide the ring buffer data size (which is 1 page less
	 * than the ring buffer size since that page is reserved for
	 * the ring buffer indices) by the max request size (which is
	 * vmbus_channel_packet_multipage_buffer + struct vstor_packet + u64)
	 */

	max_outstanding_req_per_channel =
	((storvsc_ringbuffer_size - PAGE_SIZE) /
	ALIGN(MAX_MULTIPAGE_BUFFER_PACKET +
	sizeof(struct vstor_packet) + sizeof(u64),
	sizeof(u64)));

	memcpy(&drv->dev_type, &gStorVscDeviceType,
	       sizeof(struct hv_guid));

	if (max_outstanding_req_per_channel <
	    STORVSC_MAX_IO_REQUESTS)
		return -1;

	drv->name = driver_name;
	drv->driver.name = driver_name;


	/* The driver belongs to vmbus */
	ret = vmbus_child_driver_register(&drv->driver);

	return ret;
}

static void storvsc_drv_exit(void)
{
	vmbus_child_driver_unregister(&storvsc_drv.driver);
}

static int __init storvsc_init(void)
{
	int ret;

	DPRINT_INFO(STORVSC_DRV, "Storvsc initializing....");
	ret = storvsc_drv_init();
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
