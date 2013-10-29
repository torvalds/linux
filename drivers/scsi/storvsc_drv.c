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

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/hyperv.h>
#include <linux/mempool.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_dbg.h>

/*
 * All wire protocol details (storage protocol between the guest and the host)
 * are consolidated here.
 *
 * Begin protocol definitions.
 */

/*
 * Version history:
 * V1 Beta: 0.1
 * V1 RC < 2008/1/31: 1.0
 * V1 RC > 2008/1/31:  2.0
 * Win7: 4.2
 */

#define VMSTOR_CURRENT_MAJOR  4
#define VMSTOR_CURRENT_MINOR  2


/*  Packet structure describing virtual storage requests. */
enum vstor_packet_operation {
	VSTOR_OPERATION_COMPLETE_IO		= 1,
	VSTOR_OPERATION_REMOVE_DEVICE		= 2,
	VSTOR_OPERATION_EXECUTE_SRB		= 3,
	VSTOR_OPERATION_RESET_LUN		= 4,
	VSTOR_OPERATION_RESET_ADAPTER		= 5,
	VSTOR_OPERATION_RESET_BUS		= 6,
	VSTOR_OPERATION_BEGIN_INITIALIZATION	= 7,
	VSTOR_OPERATION_END_INITIALIZATION	= 8,
	VSTOR_OPERATION_QUERY_PROTOCOL_VERSION	= 9,
	VSTOR_OPERATION_QUERY_PROPERTIES	= 10,
	VSTOR_OPERATION_ENUMERATE_BUS		= 11,
	VSTOR_OPERATION_MAXIMUM			= 11
};

/*
 * Platform neutral description of a scsi request -
 * this remains the same across the write regardless of 32/64 bit
 * note: it's patterned off the SCSI_PASS_THROUGH structure
 */
#define STORVSC_MAX_CMD_LEN			0x10
#define STORVSC_SENSE_BUFFER_SIZE		0x12
#define STORVSC_MAX_BUF_LEN_WITH_PADDING	0x14

struct vmscsi_request {
	u16 length;
	u8 srb_status;
	u8 scsi_status;

	u8  port_number;
	u8  path_id;
	u8  target_id;
	u8  lun;

	u8  cdb_length;
	u8  sense_info_length;
	u8  data_in;
	u8  reserved;

	u32 data_transfer_length;

	union {
		u8 cdb[STORVSC_MAX_CMD_LEN];
		u8 sense_data[STORVSC_SENSE_BUFFER_SIZE];
		u8 reserved_array[STORVSC_MAX_BUF_LEN_WITH_PADDING];
	};
} __attribute((packed));


/*
 * This structure is sent during the intialization phase to get the different
 * properties of the channel.
 */
struct vmstorage_channel_properties {
	u16 protocol_version;
	u8  path_id;
	u8 target_id;

	/* Note: port number is only really known on the client side */
	u32  port_number;
	u32  flags;
	u32   max_transfer_bytes;

	/*
	 * This id is unique for each channel and will correspond with
	 * vendor specific data in the inquiry data.
	 */

	u64  unique_id;
} __packed;

/*  This structure is sent during the storage protocol negotiations. */
struct vmstorage_protocol_version {
	/* Major (MSW) and minor (LSW) version numbers. */
	u16 major_minor;

	/*
	 * Revision number is auto-incremented whenever this file is changed
	 * (See FILL_VMSTOR_REVISION macro above).  Mismatch does not
	 * definitely indicate incompatibility--but it does indicate mismatched
	 * builds.
	 * This is only used on the windows side. Just set it to 0.
	 */
	u16 revision;
} __packed;

/* Channel Property Flags */
#define STORAGE_CHANNEL_REMOVABLE_FLAG		0x1
#define STORAGE_CHANNEL_EMULATED_IDE_FLAG	0x2

struct vstor_packet {
	/* Requested operation type */
	enum vstor_packet_operation operation;

	/*  Flags - see below for values */
	u32 flags;

	/* Status of the request returned from the server side. */
	u32 status;

	/* Data payload area */
	union {
		/*
		 * Structure used to forward SCSI commands from the
		 * client to the server.
		 */
		struct vmscsi_request vm_srb;

		/* Structure used to query channel properties. */
		struct vmstorage_channel_properties storage_channel_properties;

		/* Used during version negotiations. */
		struct vmstorage_protocol_version version;
	};
} __packed;

/*
 * Packet Flags:
 *
 * This flag indicates that the server should send back a completion for this
 * packet.
 */

#define REQUEST_COMPLETION_FLAG	0x1

/* Matches Windows-end */
enum storvsc_request_type {
	WRITE_TYPE = 0,
	READ_TYPE,
	UNKNOWN_TYPE,
};

/*
 * SRB status codes and masks; a subset of the codes used here.
 */

#define SRB_STATUS_AUTOSENSE_VALID	0x80
#define SRB_STATUS_INVALID_LUN	0x20
#define SRB_STATUS_SUCCESS	0x01
#define SRB_STATUS_ERROR	0x04

/*
 * This is the end of Protocol specific defines.
 */


/*
 * We setup a mempool to allocate request structures for this driver
 * on a per-lun basis. The following define specifies the number of
 * elements in the pool.
 */

#define STORVSC_MIN_BUF_NR				64
static int storvsc_ringbuffer_size = (20 * PAGE_SIZE);

module_param(storvsc_ringbuffer_size, int, S_IRUGO);
MODULE_PARM_DESC(storvsc_ringbuffer_size, "Ring buffer size (bytes)");

#define STORVSC_MAX_IO_REQUESTS				128

/*
 * In Hyper-V, each port/path/target maps to 1 scsi host adapter.  In
 * reality, the path/target is not used (ie always set to 0) so our
 * scsi host adapter essentially has 1 bus with 1 target that contains
 * up to 256 luns.
 */
#define STORVSC_MAX_LUNS_PER_TARGET			64
#define STORVSC_MAX_TARGETS				1
#define STORVSC_MAX_CHANNELS				1



struct storvsc_cmd_request {
	struct list_head entry;
	struct scsi_cmnd *cmd;

	unsigned int bounce_sgl_count;
	struct scatterlist *bounce_sgl;

	struct hv_device *device;

	/* Synchronize the request/response if needed */
	struct completion wait_event;

	unsigned char *sense_buffer;
	struct hv_multipage_buffer data_buffer;
	struct vstor_packet vstor_packet;
};


/* A storvsc device is a device object that contains a vmbus channel */
struct storvsc_device {
	struct hv_device *device;

	bool	 destroy;
	bool	 drain_notify;
	atomic_t num_outstanding_req;
	struct Scsi_Host *host;

	wait_queue_head_t waiting_to_drain;

	/*
	 * Each unique Port/Path/Target represents 1 channel ie scsi
	 * controller. In reality, the pathid, targetid is always 0
	 * and the port is set by us
	 */
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;

	/* Used for vsc/vsp channel reset process */
	struct storvsc_cmd_request init_request;
	struct storvsc_cmd_request reset_request;
};

struct stor_mem_pools {
	struct kmem_cache *request_pool;
	mempool_t *request_mempool;
};

struct hv_host_device {
	struct hv_device *dev;
	unsigned int port;
	unsigned char path;
	unsigned char target;
};

struct storvsc_scan_work {
	struct work_struct work;
	struct Scsi_Host *host;
	uint lun;
};

static void storvsc_bus_scan(struct work_struct *work)
{
	struct storvsc_scan_work *wrk;
	int id, order_id;

	wrk = container_of(work, struct storvsc_scan_work, work);
	for (id = 0; id < wrk->host->max_id; ++id) {
		if (wrk->host->reverse_ordering)
			order_id = wrk->host->max_id - id - 1;
		else
			order_id = id;

		scsi_scan_target(&wrk->host->shost_gendev, 0,
				order_id, SCAN_WILD_CARD, 1);
	}
	kfree(wrk);
}

static void storvsc_remove_lun(struct work_struct *work)
{
	struct storvsc_scan_work *wrk;
	struct scsi_device *sdev;

	wrk = container_of(work, struct storvsc_scan_work, work);
	if (!scsi_host_get(wrk->host))
		goto done;

	sdev = scsi_device_lookup(wrk->host, 0, 0, wrk->lun);

	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
	scsi_host_put(wrk->host);

done:
	kfree(wrk);
}

/*
 * Major/minor macros.  Minor version is in LSB, meaning that earlier flat
 * version numbers will be interpreted as "0.x" (i.e., 1 becomes 0.1).
 */

static inline u16 storvsc_get_version(u8 major, u8 minor)
{
	u16 version;

	version = ((major << 8) | minor);
	return version;
}

/*
 * We can get incoming messages from the host that are not in response to
 * messages that we have sent out. An example of this would be messages
 * received by the guest to notify dynamic addition/removal of LUNs. To
 * deal with potential race conditions where the driver may be in the
 * midst of being unloaded when we might receive an unsolicited message
 * from the host, we have implemented a mechanism to gurantee sequential
 * consistency:
 *
 * 1) Once the device is marked as being destroyed, we will fail all
 *    outgoing messages.
 * 2) We permit incoming messages when the device is being destroyed,
 *    only to properly account for messages already sent out.
 */

static inline struct storvsc_device *get_out_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = hv_get_drvdata(device);

	if (stor_device && stor_device->destroy)
		stor_device = NULL;

	return stor_device;
}


static inline void storvsc_wait_to_drain(struct storvsc_device *dev)
{
	dev->drain_notify = true;
	wait_event(dev->waiting_to_drain,
		   atomic_read(&dev->num_outstanding_req) == 0);
	dev->drain_notify = false;
}

static inline struct storvsc_device *get_in_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = hv_get_drvdata(device);

	if (!stor_device)
		goto get_in_err;

	/*
	 * If the device is being destroyed; allow incoming
	 * traffic only to cleanup outstanding requests.
	 */

	if (stor_device->destroy  &&
		(atomic_read(&stor_device->num_outstanding_req) == 0))
		stor_device = NULL;

get_in_err:
	return stor_device;

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
						unsigned int len,
						int write)
{
	int i;
	int num_pages;
	struct scatterlist *bounce_sgl;
	struct page *page_buf;
	unsigned int buf_len = ((write == WRITE_TYPE) ? 0 : PAGE_SIZE);

	num_pages = ALIGN(len, PAGE_SIZE) >> PAGE_SHIFT;

	bounce_sgl = kcalloc(num_pages, sizeof(struct scatterlist), GFP_ATOMIC);
	if (!bounce_sgl)
		return NULL;

	sg_init_table(bounce_sgl, num_pages);
	for (i = 0; i < num_pages; i++) {
		page_buf = alloc_page(GFP_ATOMIC);
		if (!page_buf)
			goto cleanup;
		sg_set_page(&bounce_sgl[i], page_buf, buf_len, 0);
	}

	return bounce_sgl;

cleanup:
	destroy_bounce_buffer(bounce_sgl, num_pages);
	return NULL;
}

/* Disgusting wrapper functions */
static inline unsigned long sg_kmap_atomic(struct scatterlist *sgl, int idx)
{
	void *addr = kmap_atomic(sg_page(sgl + idx));
	return (unsigned long)addr;
}

static inline void sg_kunmap_atomic(unsigned long addr)
{
	kunmap_atomic((void *)addr);
}


/* Assume the original sgl has enough room */
static unsigned int copy_from_bounce_buffer(struct scatterlist *orig_sgl,
					    struct scatterlist *bounce_sgl,
					    unsigned int orig_sgl_count,
					    unsigned int bounce_sgl_count)
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
		dest_addr = sg_kmap_atomic(orig_sgl,i) + orig_sgl[i].offset;
		dest = dest_addr;
		destlen = orig_sgl[i].length;

		if (bounce_addr == 0)
			bounce_addr = sg_kmap_atomic(bounce_sgl,j);

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
				sg_kunmap_atomic(bounce_addr);
				j++;

				/*
				 * It is possible that the number of elements
				 * in the bounce buffer may not be equal to
				 * the number of elements in the original
				 * scatter list. Handle this correctly.
				 */

				if (j == bounce_sgl_count) {
					/*
					 * We are done; cleanup and return.
					 */
					sg_kunmap_atomic(dest_addr - orig_sgl[i].offset);
					local_irq_restore(flags);
					return total_copied;
				}

				/* if we need to use another bounce buffer */
				if (destlen || i != orig_sgl_count - 1)
					bounce_addr = sg_kmap_atomic(bounce_sgl,j);
			} else if (destlen == 0 && i == orig_sgl_count - 1) {
				/* unmap the last bounce that is < PAGE_SIZE */
				sg_kunmap_atomic(bounce_addr);
			}
		}

		sg_kunmap_atomic(dest_addr - orig_sgl[i].offset);
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
		src_addr = sg_kmap_atomic(orig_sgl,i) + orig_sgl[i].offset;
		src = src_addr;
		srclen = orig_sgl[i].length;

		if (bounce_addr == 0)
			bounce_addr = sg_kmap_atomic(bounce_sgl,j);

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
				sg_kunmap_atomic(bounce_addr);
				j++;

				/* if we need to use another bounce buffer */
				if (srclen || i != orig_sgl_count - 1)
					bounce_addr = sg_kmap_atomic(bounce_sgl,j);

			} else if (srclen == 0 && i == orig_sgl_count - 1) {
				/* unmap the last bounce that is < PAGE_SIZE */
				sg_kunmap_atomic(bounce_addr);
			}
		}

		sg_kunmap_atomic(src_addr - orig_sgl[i].offset);
	}

	local_irq_restore(flags);

	return total_copied;
}

static int storvsc_channel_init(struct hv_device *device)
{
	struct storvsc_device *stor_device;
	struct storvsc_cmd_request *request;
	struct vstor_packet *vstor_packet;
	int ret, t;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return -ENODEV;

	request = &stor_device->init_request;
	vstor_packet = &request->vstor_packet;

	/*
	 * Now, initiate the vsc/vsp initialization protocol on the open
	 * channel
	 */
	memset(request, 0, sizeof(struct storvsc_cmd_request));
	init_completion(&request->wait_event);
	vstor_packet->operation = VSTOR_OPERATION_BEGIN_INITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0)
		goto cleanup;


	/* reuse the packet for version range supported */
	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERY_PROTOCOL_VERSION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	vstor_packet->version.major_minor =
		storvsc_get_version(VMSTOR_CURRENT_MAJOR, VMSTOR_CURRENT_MINOR);

	/*
	 * The revision number is only used in Windows; set it to 0.
	 */
	vstor_packet->version.revision = 0;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0)
		goto cleanup;


	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERY_PROPERTIES;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->storage_channel_properties.port_number =
					stor_device->port_number;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0)
		goto cleanup;

	stor_device->path_id = vstor_packet->storage_channel_properties.path_id;
	stor_device->target_id
		= vstor_packet->storage_channel_properties.target_id;

	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_END_INITIALIZATION;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0)
		goto cleanup;


cleanup:
	return ret;
}


static void storvsc_command_completion(struct storvsc_cmd_request *cmd_request)
{
	struct scsi_cmnd *scmnd = cmd_request->cmd;
	struct hv_host_device *host_dev = shost_priv(scmnd->device->host);
	void (*scsi_done_fn)(struct scsi_cmnd *);
	struct scsi_sense_hdr sense_hdr;
	struct vmscsi_request *vm_srb;
	struct storvsc_scan_work *wrk;
	struct stor_mem_pools *memp = scmnd->device->hostdata;

	vm_srb = &cmd_request->vstor_packet.vm_srb;
	if (cmd_request->bounce_sgl_count) {
		if (vm_srb->data_in == READ_TYPE)
			copy_from_bounce_buffer(scsi_sglist(scmnd),
					cmd_request->bounce_sgl,
					scsi_sg_count(scmnd),
					cmd_request->bounce_sgl_count);
		destroy_bounce_buffer(cmd_request->bounce_sgl,
					cmd_request->bounce_sgl_count);
	}

	/*
	 * If there is an error; offline the device since all
	 * error recovery strategies would have already been
	 * deployed on the host side.
	 */
	if (vm_srb->srb_status == SRB_STATUS_ERROR)
		scmnd->result = DID_TARGET_FAILURE << 16;
	else
		scmnd->result = vm_srb->scsi_status;

	/*
	 * If the LUN is invalid; remove the device.
	 */
	if (vm_srb->srb_status == SRB_STATUS_INVALID_LUN) {
		struct storvsc_device *stor_dev;
		struct hv_device *dev = host_dev->dev;
		struct Scsi_Host *host;

		stor_dev = get_in_stor_device(dev);
		host = stor_dev->host;

		wrk = kmalloc(sizeof(struct storvsc_scan_work),
				GFP_ATOMIC);
		if (!wrk) {
			scmnd->result = DID_TARGET_FAILURE << 16;
		} else {
			wrk->host = host;
			wrk->lun = vm_srb->lun;
			INIT_WORK(&wrk->work, storvsc_remove_lun);
			schedule_work(&wrk->work);
		}
	}

	if (scmnd->result) {
		if (scsi_normalize_sense(scmnd->sense_buffer,
				SCSI_SENSE_BUFFERSIZE, &sense_hdr))
			scsi_print_sense_hdr("storvsc", &sense_hdr);
	}

	scsi_set_resid(scmnd,
		cmd_request->data_buffer.len -
		vm_srb->data_transfer_length);

	scsi_done_fn = scmnd->scsi_done;

	scmnd->host_scribble = NULL;
	scmnd->scsi_done = NULL;

	scsi_done_fn(scmnd);

	mempool_free(cmd_request, memp->request_mempool);
}

static void storvsc_on_io_completion(struct hv_device *device,
				  struct vstor_packet *vstor_packet,
				  struct storvsc_cmd_request *request)
{
	struct storvsc_device *stor_device;
	struct vstor_packet *stor_pkt;

	stor_device = hv_get_drvdata(device);
	stor_pkt = &request->vstor_packet;

	/*
	 * The current SCSI handling on the host side does
	 * not correctly handle:
	 * INQUIRY command with page code parameter set to 0x80
	 * MODE_SENSE command with cmd[2] == 0x1c
	 *
	 * Setup srb and scsi status so this won't be fatal.
	 * We do this so we can distinguish truly fatal failues
	 * (srb status == 0x4) and off-line the device in that case.
	 */

	if ((stor_pkt->vm_srb.cdb[0] == INQUIRY) ||
	   (stor_pkt->vm_srb.cdb[0] == MODE_SENSE)) {
		vstor_packet->vm_srb.scsi_status = 0;
		vstor_packet->vm_srb.srb_status = SRB_STATUS_SUCCESS;
	}


	/* Copy over the status...etc */
	stor_pkt->vm_srb.scsi_status = vstor_packet->vm_srb.scsi_status;
	stor_pkt->vm_srb.srb_status = vstor_packet->vm_srb.srb_status;
	stor_pkt->vm_srb.sense_info_length =
	vstor_packet->vm_srb.sense_info_length;

	if (vstor_packet->vm_srb.scsi_status != 0 ||
		vstor_packet->vm_srb.srb_status != SRB_STATUS_SUCCESS){
		dev_warn(&device->device,
			 "cmd 0x%x scsi status 0x%x srb status 0x%x\n",
			 stor_pkt->vm_srb.cdb[0],
			 vstor_packet->vm_srb.scsi_status,
			 vstor_packet->vm_srb.srb_status);
	}

	if ((vstor_packet->vm_srb.scsi_status & 0xFF) == 0x02) {
		/* CHECK_CONDITION */
		if (vstor_packet->vm_srb.srb_status &
			SRB_STATUS_AUTOSENSE_VALID) {
			/* autosense data available */
			dev_warn(&device->device,
				 "stor pkt %p autosense data valid - len %d\n",
				 request,
				 vstor_packet->vm_srb.sense_info_length);

			memcpy(request->sense_buffer,
			       vstor_packet->vm_srb.sense_data,
			       vstor_packet->vm_srb.sense_info_length);

		}
	}

	stor_pkt->vm_srb.data_transfer_length =
	vstor_packet->vm_srb.data_transfer_length;

	storvsc_command_completion(request);

	if (atomic_dec_and_test(&stor_device->num_outstanding_req) &&
		stor_device->drain_notify)
		wake_up(&stor_device->waiting_to_drain);


}

static void storvsc_on_receive(struct hv_device *device,
			     struct vstor_packet *vstor_packet,
			     struct storvsc_cmd_request *request)
{
	struct storvsc_scan_work *work;
	struct storvsc_device *stor_device;

	switch (vstor_packet->operation) {
	case VSTOR_OPERATION_COMPLETE_IO:
		storvsc_on_io_completion(device, vstor_packet, request);
		break;

	case VSTOR_OPERATION_REMOVE_DEVICE:
	case VSTOR_OPERATION_ENUMERATE_BUS:
		stor_device = get_in_stor_device(device);
		work = kmalloc(sizeof(struct storvsc_scan_work), GFP_ATOMIC);
		if (!work)
			return;

		INIT_WORK(&work->work, storvsc_bus_scan);
		work->host = stor_device->host;
		schedule_work(&work->work);
		break;

	default:
		break;
	}
}

static void storvsc_on_channel_callback(void *context)
{
	struct hv_device *device = (struct hv_device *)context;
	struct storvsc_device *stor_device;
	u32 bytes_recvd;
	u64 request_id;
	unsigned char packet[ALIGN(sizeof(struct vstor_packet), 8)];
	struct storvsc_cmd_request *request;
	int ret;


	stor_device = get_in_stor_device(device);
	if (!stor_device)
		return;

	do {
		ret = vmbus_recvpacket(device->channel, packet,
				       ALIGN(sizeof(struct vstor_packet), 8),
				       &bytes_recvd, &request_id);
		if (ret == 0 && bytes_recvd > 0) {

			request = (struct storvsc_cmd_request *)
					(unsigned long)request_id;

			if ((request == &stor_device->init_request) ||
			    (request == &stor_device->reset_request)) {

				memcpy(&request->vstor_packet, packet,
				       sizeof(struct vstor_packet));
				complete(&request->wait_event);
			} else {
				storvsc_on_receive(device,
						(struct vstor_packet *)packet,
						request);
			}
		} else {
			break;
		}
	} while (1);

	return;
}

static int storvsc_connect_to_vsp(struct hv_device *device, u32 ring_size)
{
	struct vmstorage_channel_properties props;
	int ret;

	memset(&props, 0, sizeof(struct vmstorage_channel_properties));

	ret = vmbus_open(device->channel,
			 ring_size,
			 ring_size,
			 (void *)&props,
			 sizeof(struct vmstorage_channel_properties),
			 storvsc_on_channel_callback, device);

	if (ret != 0)
		return ret;

	ret = storvsc_channel_init(device);

	return ret;
}

static int storvsc_dev_remove(struct hv_device *device)
{
	struct storvsc_device *stor_device;
	unsigned long flags;

	stor_device = hv_get_drvdata(device);

	spin_lock_irqsave(&device->channel->inbound_lock, flags);
	stor_device->destroy = true;
	spin_unlock_irqrestore(&device->channel->inbound_lock, flags);

	/*
	 * At this point, all outbound traffic should be disable. We
	 * only allow inbound traffic (responses) to proceed so that
	 * outstanding requests can be completed.
	 */

	storvsc_wait_to_drain(stor_device);

	/*
	 * Since we have already drained, we don't need to busy wait
	 * as was done in final_release_stor_device()
	 * Note that we cannot set the ext pointer to NULL until
	 * we have drained - to drain the outgoing packets, we need to
	 * allow incoming packets.
	 */
	spin_lock_irqsave(&device->channel->inbound_lock, flags);
	hv_set_drvdata(device, NULL);
	spin_unlock_irqrestore(&device->channel->inbound_lock, flags);

	/* Close the channel */
	vmbus_close(device->channel);

	kfree(stor_device);
	return 0;
}

static int storvsc_do_io(struct hv_device *device,
			      struct storvsc_cmd_request *request)
{
	struct storvsc_device *stor_device;
	struct vstor_packet *vstor_packet;
	int ret = 0;

	vstor_packet = &request->vstor_packet;
	stor_device = get_out_stor_device(device);

	if (!stor_device)
		return -ENODEV;


	request->device  = device;


	vstor_packet->flags |= REQUEST_COMPLETION_FLAG;

	vstor_packet->vm_srb.length = sizeof(struct vmscsi_request);


	vstor_packet->vm_srb.sense_info_length = STORVSC_SENSE_BUFFER_SIZE;


	vstor_packet->vm_srb.data_transfer_length =
	request->data_buffer.len;

	vstor_packet->operation = VSTOR_OPERATION_EXECUTE_SRB;

	if (request->data_buffer.len) {
		ret = vmbus_sendpacket_multipagebuffer(device->channel,
				&request->data_buffer,
				vstor_packet,
				sizeof(struct vstor_packet),
				(unsigned long)request);
	} else {
		ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	}

	if (ret != 0)
		return ret;

	atomic_inc(&stor_device->num_outstanding_req);

	return ret;
}

static int storvsc_device_alloc(struct scsi_device *sdevice)
{
	struct stor_mem_pools *memp;
	int number = STORVSC_MIN_BUF_NR;

	memp = kzalloc(sizeof(struct stor_mem_pools), GFP_KERNEL);
	if (!memp)
		return -ENOMEM;

	memp->request_pool =
		kmem_cache_create(dev_name(&sdevice->sdev_dev),
				sizeof(struct storvsc_cmd_request), 0,
				SLAB_HWCACHE_ALIGN, NULL);

	if (!memp->request_pool)
		goto err0;

	memp->request_mempool = mempool_create(number, mempool_alloc_slab,
						mempool_free_slab,
						memp->request_pool);

	if (!memp->request_mempool)
		goto err1;

	sdevice->hostdata = memp;

	return 0;

err1:
	kmem_cache_destroy(memp->request_pool);

err0:
	kfree(memp);
	return -ENOMEM;
}

static void storvsc_device_destroy(struct scsi_device *sdevice)
{
	struct stor_mem_pools *memp = sdevice->hostdata;

	mempool_destroy(memp->request_mempool);
	kmem_cache_destroy(memp->request_pool);
	kfree(memp);
	sdevice->hostdata = NULL;
}

static int storvsc_device_configure(struct scsi_device *sdevice)
{
	scsi_adjust_queue_depth(sdevice, MSG_SIMPLE_TAG,
				STORVSC_MAX_IO_REQUESTS);

	blk_queue_max_segment_size(sdevice->request_queue, PAGE_SIZE);

	blk_queue_bounce_limit(sdevice->request_queue, BLK_BOUNCE_ANY);

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

	return 0;
}

static int storvsc_host_reset_handler(struct scsi_cmnd *scmnd)
{
	struct hv_host_device *host_dev = shost_priv(scmnd->device->host);
	struct hv_device *device = host_dev->dev;

	struct storvsc_device *stor_device;
	struct storvsc_cmd_request *request;
	struct vstor_packet *vstor_packet;
	int ret, t;


	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return FAILED;

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
		return FAILED;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0)
		return TIMEOUT_ERROR;


	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 * There is a potential race here where the host may be in
	 * the process of responding when we return from here.
	 * Just wait for all in-transit packets to be accounted for
	 * before we return from here.
	 */
	storvsc_wait_to_drain(stor_device);

	return SUCCESS;
}

static bool storvsc_scsi_cmd_ok(struct scsi_cmnd *scmnd)
{
	bool allowed = true;
	u8 scsi_op = scmnd->cmnd[0];

	switch (scsi_op) {
	/*
	 * smartd sends this command and the host does not handle
	 * this. So, don't send it.
	 */
	case SET_WINDOW:
		scmnd->result = ILLEGAL_REQUEST << 16;
		allowed = false;
		break;
	default:
		break;
	}
	return allowed;
}

static int storvsc_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *scmnd)
{
	int ret;
	struct hv_host_device *host_dev = shost_priv(host);
	struct hv_device *dev = host_dev->dev;
	struct storvsc_cmd_request *cmd_request;
	unsigned int request_size = 0;
	int i;
	struct scatterlist *sgl;
	unsigned int sg_count = 0;
	struct vmscsi_request *vm_srb;
	struct stor_mem_pools *memp = scmnd->device->hostdata;

	if (!storvsc_scsi_cmd_ok(scmnd)) {
		scmnd->scsi_done(scmnd);
		return 0;
	}

	request_size = sizeof(struct storvsc_cmd_request);

	cmd_request = mempool_alloc(memp->request_mempool,
				       GFP_ATOMIC);

	/*
	 * We might be invoked in an interrupt context; hence
	 * mempool_alloc() can fail.
	 */
	if (!cmd_request)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	memset(cmd_request, 0, sizeof(struct storvsc_cmd_request));

	/* Setup the cmd request */
	cmd_request->cmd = scmnd;

	scmnd->host_scribble = (unsigned char *)cmd_request;

	vm_srb = &cmd_request->vstor_packet.vm_srb;


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


	vm_srb->port_number = host_dev->port;
	vm_srb->path_id = scmnd->device->channel;
	vm_srb->target_id = scmnd->device->id;
	vm_srb->lun = scmnd->device->lun;

	vm_srb->cdb_length = scmnd->cmd_len;

	memcpy(vm_srb->cdb, scmnd->cmnd, vm_srb->cdb_length);

	cmd_request->sense_buffer = scmnd->sense_buffer;


	cmd_request->data_buffer.len = scsi_bufflen(scmnd);
	if (scsi_sg_count(scmnd)) {
		sgl = (struct scatterlist *)scsi_sglist(scmnd);
		sg_count = scsi_sg_count(scmnd);

		/* check if we need to bounce the sgl */
		if (do_bounce_buffer(sgl, scsi_sg_count(scmnd)) != -1) {
			cmd_request->bounce_sgl =
				create_bounce_buffer(sgl, scsi_sg_count(scmnd),
						     scsi_bufflen(scmnd),
						     vm_srb->data_in);
			if (!cmd_request->bounce_sgl) {
				ret = SCSI_MLQUEUE_HOST_BUSY;
				goto queue_error;
			}

			cmd_request->bounce_sgl_count =
				ALIGN(scsi_bufflen(scmnd), PAGE_SIZE) >>
					PAGE_SHIFT;

			if (vm_srb->data_in == WRITE_TYPE)
				copy_to_bounce_buffer(sgl,
					cmd_request->bounce_sgl,
					scsi_sg_count(scmnd));

			sgl = cmd_request->bounce_sgl;
			sg_count = cmd_request->bounce_sgl_count;
		}

		cmd_request->data_buffer.offset = sgl[0].offset;

		for (i = 0; i < sg_count; i++)
			cmd_request->data_buffer.pfn_array[i] =
				page_to_pfn(sg_page((&sgl[i])));

	} else if (scsi_sglist(scmnd)) {
		cmd_request->data_buffer.offset =
			virt_to_phys(scsi_sglist(scmnd)) & (PAGE_SIZE-1);
		cmd_request->data_buffer.pfn_array[0] =
			virt_to_phys(scsi_sglist(scmnd)) >> PAGE_SHIFT;
	}

	/* Invokes the vsc to start an IO */
	ret = storvsc_do_io(dev, cmd_request);

	if (ret == -EAGAIN) {
		/* no more space */

		if (cmd_request->bounce_sgl_count) {
			destroy_bounce_buffer(cmd_request->bounce_sgl,
					cmd_request->bounce_sgl_count);

			ret = SCSI_MLQUEUE_DEVICE_BUSY;
			goto queue_error;
		}
	}

	return 0;

queue_error:
	mempool_free(cmd_request, memp->request_mempool);
	scmnd->host_scribble = NULL;
	return ret;
}

static struct scsi_host_template scsi_driver = {
	.module	=		THIS_MODULE,
	.name =			"storvsc_host_t",
	.bios_param =		storvsc_get_chs,
	.queuecommand =		storvsc_queuecommand,
	.eh_host_reset_handler =	storvsc_host_reset_handler,
	.slave_alloc =		storvsc_device_alloc,
	.slave_destroy =	storvsc_device_destroy,
	.slave_configure =	storvsc_device_configure,
	.cmd_per_lun =		1,
	/* 64 max_queue * 1 target */
	.can_queue =		STORVSC_MAX_IO_REQUESTS*STORVSC_MAX_TARGETS,
	.this_id =		-1,
	/* no use setting to 0 since ll_blk_rw reset it to 1 */
	/* currently 32 */
	.sg_tablesize =		MAX_MULTIPAGE_BUFFER_COUNT,
	.use_clustering =	DISABLE_CLUSTERING,
	/* Make sure we dont get a sg segment crosses a page boundary */
	.dma_boundary =		PAGE_SIZE-1,
};

enum {
	SCSI_GUID,
	IDE_GUID,
};

static const struct hv_vmbus_device_id id_table[] = {
	/* SCSI guid */
	{ VMBUS_DEVICE(0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
		       0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f)
	  .driver_data = SCSI_GUID },
	/* IDE guid */
	{ VMBUS_DEVICE(0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
		       0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5)
	  .driver_data = IDE_GUID },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static int storvsc_probe(struct hv_device *device,
			const struct hv_vmbus_device_id *dev_id)
{
	int ret;
	struct Scsi_Host *host;
	struct hv_host_device *host_dev;
	bool dev_is_ide = ((dev_id->driver_data == IDE_GUID) ? true : false);
	int target = 0;
	struct storvsc_device *stor_device;

	host = scsi_host_alloc(&scsi_driver,
			       sizeof(struct hv_host_device));
	if (!host)
		return -ENOMEM;

	host_dev = shost_priv(host);
	memset(host_dev, 0, sizeof(struct hv_host_device));

	host_dev->port = host->host_no;
	host_dev->dev = device;


	stor_device = kzalloc(sizeof(struct storvsc_device), GFP_KERNEL);
	if (!stor_device) {
		ret = -ENOMEM;
		goto err_out0;
	}

	stor_device->destroy = false;
	init_waitqueue_head(&stor_device->waiting_to_drain);
	stor_device->device = device;
	stor_device->host = host;
	hv_set_drvdata(device, stor_device);

	stor_device->port_number = host->host_no;
	ret = storvsc_connect_to_vsp(device, storvsc_ringbuffer_size);
	if (ret)
		goto err_out1;

	host_dev->path = stor_device->path_id;
	host_dev->target = stor_device->target_id;

	/* max # of devices per target */
	host->max_lun = STORVSC_MAX_LUNS_PER_TARGET;
	/* max # of targets per channel */
	host->max_id = STORVSC_MAX_TARGETS;
	/* max # of channels */
	host->max_channel = STORVSC_MAX_CHANNELS - 1;
	/* max cmd length */
	host->max_cmd_len = STORVSC_MAX_CMD_LEN;

	/* Register the HBA and start the scsi bus scan */
	ret = scsi_add_host(host, &device->device);
	if (ret != 0)
		goto err_out2;

	if (!dev_is_ide) {
		scsi_scan_host(host);
	} else {
		target = (device->dev_instance.b[5] << 8 |
			 device->dev_instance.b[4]);
		ret = scsi_add_device(host, 0, target, 0);
		if (ret) {
			scsi_remove_host(host);
			goto err_out2;
		}
	}
	return 0;

err_out2:
	/*
	 * Once we have connected with the host, we would need to
	 * to invoke storvsc_dev_remove() to rollback this state and
	 * this call also frees up the stor_device; hence the jump around
	 * err_out1 label.
	 */
	storvsc_dev_remove(device);
	goto err_out0;

err_out1:
	kfree(stor_device);

err_out0:
	scsi_host_put(host);
	return ret;
}

static int storvsc_remove(struct hv_device *dev)
{
	struct storvsc_device *stor_device = hv_get_drvdata(dev);
	struct Scsi_Host *host = stor_device->host;

	scsi_remove_host(host);
	storvsc_dev_remove(dev);
	scsi_host_put(host);

	return 0;
}

static struct hv_driver storvsc_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = storvsc_probe,
	.remove = storvsc_remove,
};

static int __init storvsc_drv_init(void)
{
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

	if (max_outstanding_req_per_channel <
	    STORVSC_MAX_IO_REQUESTS)
		return -EINVAL;

	return vmbus_driver_register(&storvsc_drv);
}

static void __exit storvsc_drv_exit(void)
{
	vmbus_driver_unregister(&storvsc_drv);
}

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V virtual storage driver");
module_init(storvsc_drv_init);
module_exit(storvsc_drv_exit);
