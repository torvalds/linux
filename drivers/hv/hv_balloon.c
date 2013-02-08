/*
 * Copyright (c) 2012, Microsoft Corporation.
 *
 * Author:
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mman.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/memory_hotplug.h>
#include <linux/memory.h>
#include <linux/notifier.h>
#include <linux/percpu_counter.h>

#include <linux/hyperv.h>

/*
 * We begin with definitions supporting the Dynamic Memory protocol
 * with the host.
 *
 * Begin protocol definitions.
 */



/*
 * Protocol versions. The low word is the minor version, the high word the major
 * version.
 *
 * History:
 * Initial version 1.0
 * Changed to 0.1 on 2009/03/25
 * Changes to 0.2 on 2009/05/14
 * Changes to 0.3 on 2009/12/03
 * Changed to 1.0 on 2011/04/05
 */

#define DYNMEM_MAKE_VERSION(Major, Minor) ((__u32)(((Major) << 16) | (Minor)))
#define DYNMEM_MAJOR_VERSION(Version) ((__u32)(Version) >> 16)
#define DYNMEM_MINOR_VERSION(Version) ((__u32)(Version) & 0xff)

enum {
	DYNMEM_PROTOCOL_VERSION_1 = DYNMEM_MAKE_VERSION(0, 3),
	DYNMEM_PROTOCOL_VERSION_2 = DYNMEM_MAKE_VERSION(1, 0),

	DYNMEM_PROTOCOL_VERSION_WIN7 = DYNMEM_PROTOCOL_VERSION_1,
	DYNMEM_PROTOCOL_VERSION_WIN8 = DYNMEM_PROTOCOL_VERSION_2,

	DYNMEM_PROTOCOL_VERSION_CURRENT = DYNMEM_PROTOCOL_VERSION_WIN8
};



/*
 * Message Types
 */

enum dm_message_type {
	/*
	 * Version 0.3
	 */
	DM_ERROR			= 0,
	DM_VERSION_REQUEST		= 1,
	DM_VERSION_RESPONSE		= 2,
	DM_CAPABILITIES_REPORT		= 3,
	DM_CAPABILITIES_RESPONSE	= 4,
	DM_STATUS_REPORT		= 5,
	DM_BALLOON_REQUEST		= 6,
	DM_BALLOON_RESPONSE		= 7,
	DM_UNBALLOON_REQUEST		= 8,
	DM_UNBALLOON_RESPONSE		= 9,
	DM_MEM_HOT_ADD_REQUEST		= 10,
	DM_MEM_HOT_ADD_RESPONSE		= 11,
	DM_VERSION_03_MAX		= 11,
	/*
	 * Version 1.0.
	 */
	DM_INFO_MESSAGE			= 12,
	DM_VERSION_1_MAX		= 12
};


/*
 * Structures defining the dynamic memory management
 * protocol.
 */

union dm_version {
	struct {
		__u16 minor_version;
		__u16 major_version;
	};
	__u32 version;
} __packed;


union dm_caps {
	struct {
		__u64 balloon:1;
		__u64 hot_add:1;
		__u64 reservedz:62;
	} cap_bits;
	__u64 caps;
} __packed;

union dm_mem_page_range {
	struct  {
		/*
		 * The PFN number of the first page in the range.
		 * 40 bits is the architectural limit of a PFN
		 * number for AMD64.
		 */
		__u64 start_page:40;
		/*
		 * The number of pages in the range.
		 */
		__u64 page_cnt:24;
	} finfo;
	__u64  page_range;
} __packed;



/*
 * The header for all dynamic memory messages:
 *
 * type: Type of the message.
 * size: Size of the message in bytes; including the header.
 * trans_id: The guest is responsible for manufacturing this ID.
 */

struct dm_header {
	__u16 type;
	__u16 size;
	__u32 trans_id;
} __packed;

/*
 * A generic message format for dynamic memory.
 * Specific message formats are defined later in the file.
 */

struct dm_message {
	struct dm_header hdr;
	__u8 data[]; /* enclosed message */
} __packed;


/*
 * Specific message types supporting the dynamic memory protocol.
 */

/*
 * Version negotiation message. Sent from the guest to the host.
 * The guest is free to try different versions until the host
 * accepts the version.
 *
 * dm_version: The protocol version requested.
 * is_last_attempt: If TRUE, this is the last version guest will request.
 * reservedz: Reserved field, set to zero.
 */

struct dm_version_request {
	struct dm_header hdr;
	union dm_version version;
	__u32 is_last_attempt:1;
	__u32 reservedz:31;
} __packed;

/*
 * Version response message; Host to Guest and indicates
 * if the host has accepted the version sent by the guest.
 *
 * is_accepted: If TRUE, host has accepted the version and the guest
 * should proceed to the next stage of the protocol. FALSE indicates that
 * guest should re-try with a different version.
 *
 * reservedz: Reserved field, set to zero.
 */

struct dm_version_response {
	struct dm_header hdr;
	__u64 is_accepted:1;
	__u64 reservedz:63;
} __packed;

/*
 * Message reporting capabilities. This is sent from the guest to the
 * host.
 */

struct dm_capabilities {
	struct dm_header hdr;
	union dm_caps caps;
	__u64 min_page_cnt;
	__u64 max_page_number;
} __packed;

/*
 * Response to the capabilities message. This is sent from the host to the
 * guest. This message notifies if the host has accepted the guest's
 * capabilities. If the host has not accepted, the guest must shutdown
 * the service.
 *
 * is_accepted: Indicates if the host has accepted guest's capabilities.
 * reservedz: Must be 0.
 */

struct dm_capabilities_resp_msg {
	struct dm_header hdr;
	__u64 is_accepted:1;
	__u64 reservedz:63;
} __packed;

/*
 * This message is used to report memory pressure from the guest.
 * This message is not part of any transaction and there is no
 * response to this message.
 *
 * num_avail: Available memory in pages.
 * num_committed: Committed memory in pages.
 * page_file_size: The accumulated size of all page files
 *		   in the system in pages.
 * zero_free: The nunber of zero and free pages.
 * page_file_writes: The writes to the page file in pages.
 * io_diff: An indicator of file cache efficiency or page file activity,
 *	    calculated as File Cache Page Fault Count - Page Read Count.
 *	    This value is in pages.
 *
 * Some of these metrics are Windows specific and fortunately
 * the algorithm on the host side that computes the guest memory
 * pressure only uses num_committed value.
 */

struct dm_status {
	struct dm_header hdr;
	__u64 num_avail;
	__u64 num_committed;
	__u64 page_file_size;
	__u64 zero_free;
	__u32 page_file_writes;
	__u32 io_diff;
} __packed;


/*
 * Message to ask the guest to allocate memory - balloon up message.
 * This message is sent from the host to the guest. The guest may not be
 * able to allocate as much memory as requested.
 *
 * num_pages: number of pages to allocate.
 */

struct dm_balloon {
	struct dm_header hdr;
	__u32 num_pages;
	__u32 reservedz;
} __packed;


/*
 * Balloon response message; this message is sent from the guest
 * to the host in response to the balloon message.
 *
 * reservedz: Reserved; must be set to zero.
 * more_pages: If FALSE, this is the last message of the transaction.
 * if TRUE there will atleast one more message from the guest.
 *
 * range_count: The number of ranges in the range array.
 *
 * range_array: An array of page ranges returned to the host.
 *
 */

struct dm_balloon_response {
	struct dm_header hdr;
	__u32 reservedz;
	__u32 more_pages:1;
	__u32 range_count:31;
	union dm_mem_page_range range_array[];
} __packed;

/*
 * Un-balloon message; this message is sent from the host
 * to the guest to give guest more memory.
 *
 * more_pages: If FALSE, this is the last message of the transaction.
 * if TRUE there will atleast one more message from the guest.
 *
 * reservedz: Reserved; must be set to zero.
 *
 * range_count: The number of ranges in the range array.
 *
 * range_array: An array of page ranges returned to the host.
 *
 */

struct dm_unballoon_request {
	struct dm_header hdr;
	__u32 more_pages:1;
	__u32 reservedz:31;
	__u32 range_count;
	union dm_mem_page_range range_array[];
} __packed;

/*
 * Un-balloon response message; this message is sent from the guest
 * to the host in response to an unballoon request.
 *
 */

struct dm_unballoon_response {
	struct dm_header hdr;
} __packed;


/*
 * Hot add request message. Message sent from the host to the guest.
 *
 * mem_range: Memory range to hot add.
 *
 * On Linux we currently don't support this since we cannot hot add
 * arbitrary granularity of memory.
 */

struct dm_hot_add {
	struct dm_header hdr;
	union dm_mem_page_range range;
} __packed;

/*
 * Hot add response message.
 * This message is sent by the guest to report the status of a hot add request.
 * If page_count is less than the requested page count, then the host should
 * assume all further hot add requests will fail, since this indicates that
 * the guest has hit an upper physical memory barrier.
 *
 * Hot adds may also fail due to low resources; in this case, the guest must
 * not complete this message until the hot add can succeed, and the host must
 * not send a new hot add request until the response is sent.
 * If VSC fails to hot add memory DYNMEM_NUMBER_OF_UNSUCCESSFUL_HOTADD_ATTEMPTS
 * times it fails the request.
 *
 *
 * page_count: number of pages that were successfully hot added.
 *
 * result: result of the operation 1: success, 0: failure.
 *
 */

struct dm_hot_add_response {
	struct dm_header hdr;
	__u32 page_count;
	__u32 result;
} __packed;

/*
 * Types of information sent from host to the guest.
 */

enum dm_info_type {
	INFO_TYPE_MAX_PAGE_CNT = 0,
	MAX_INFO_TYPE
};


/*
 * Header for the information message.
 */

struct dm_info_header {
	enum dm_info_type type;
	__u32 data_size;
} __packed;

/*
 * This message is sent from the host to the guest to pass
 * some relevant information (win8 addition).
 *
 * reserved: no used.
 * info_size: size of the information blob.
 * info: information blob.
 */

struct dm_info_msg {
	struct dm_header hdr;
	__u32 reserved;
	__u32 info_size;
	__u8  info[];
};

/*
 * End protocol definitions.
 */

static bool hot_add;
static bool do_hot_add;
/*
 * Delay reporting memory pressure by
 * the specified number of seconds.
 */
static uint pressure_report_delay = 30;

module_param(hot_add, bool, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(hot_add, "If set attempt memory hot_add");

module_param(pressure_report_delay, uint, (S_IRUGO | S_IWUSR));
MODULE_PARM_DESC(pressure_report_delay, "Delay in secs in reporting pressure");
static atomic_t trans_id = ATOMIC_INIT(0);

static int dm_ring_size = (5 * PAGE_SIZE);

/*
 * Driver specific state.
 */

enum hv_dm_state {
	DM_INITIALIZING = 0,
	DM_INITIALIZED,
	DM_BALLOON_UP,
	DM_BALLOON_DOWN,
	DM_HOT_ADD,
	DM_INIT_ERROR
};


static __u8 recv_buffer[PAGE_SIZE];
static __u8 *send_buffer;
#define PAGES_IN_2M	512

struct hv_dynmem_device {
	struct hv_device *dev;
	enum hv_dm_state state;
	struct completion host_event;
	struct completion config_event;

	/*
	 * Number of pages we have currently ballooned out.
	 */
	unsigned int num_pages_ballooned;

	/*
	 * This thread handles both balloon/hot-add
	 * requests from the host as well as notifying
	 * the host with regards to memory pressure in
	 * the guest.
	 */
	struct task_struct *thread;

	/*
	 * We start with the highest version we can support
	 * and downgrade based on the host; we save here the
	 * next version to try.
	 */
	__u32 next_version;
};

static struct hv_dynmem_device dm_device;

static void hot_add_req(struct hv_dynmem_device *dm, struct dm_hot_add *msg)
{

	struct dm_hot_add_response resp;

	if (do_hot_add) {

		pr_info("Memory hot add not supported\n");

		/*
		 * Currently we do not support hot add.
		 * Just fail the request.
		 */
	}

	memset(&resp, 0, sizeof(struct dm_hot_add_response));
	resp.hdr.type = DM_MEM_HOT_ADD_RESPONSE;
	resp.hdr.size = sizeof(struct dm_hot_add_response);
	resp.hdr.trans_id = atomic_inc_return(&trans_id);

	resp.page_count = 0;
	resp.result = 0;

	dm->state = DM_INITIALIZED;
	vmbus_sendpacket(dm->dev->channel, &resp,
			sizeof(struct dm_hot_add_response),
			(unsigned long)NULL,
			VM_PKT_DATA_INBAND, 0);

}

static void process_info(struct hv_dynmem_device *dm, struct dm_info_msg *msg)
{
	struct dm_info_header *info_hdr;

	info_hdr = (struct dm_info_header *)msg->info;

	switch (info_hdr->type) {
	case INFO_TYPE_MAX_PAGE_CNT:
		pr_info("Received INFO_TYPE_MAX_PAGE_CNT\n");
		pr_info("Data Size is %d\n", info_hdr->data_size);
		break;
	default:
		pr_info("Received Unknown type: %d\n", info_hdr->type);
	}
}

/*
 * Post our status as it relates memory pressure to the
 * host. Host expects the guests to post this status
 * periodically at 1 second intervals.
 *
 * The metrics specified in this protocol are very Windows
 * specific and so we cook up numbers here to convey our memory
 * pressure.
 */

static void post_status(struct hv_dynmem_device *dm)
{
	struct dm_status status;
	struct sysinfo val;

	if (pressure_report_delay > 0) {
		--pressure_report_delay;
		return;
	}
	si_meminfo(&val);
	memset(&status, 0, sizeof(struct dm_status));
	status.hdr.type = DM_STATUS_REPORT;
	status.hdr.size = sizeof(struct dm_status);
	status.hdr.trans_id = atomic_inc_return(&trans_id);

	/*
	 * The host expects the guest to report free memory.
	 * Further, the host expects the pressure information to
	 * include the ballooned out pages.
	 */
	status.num_avail = val.freeram;
	status.num_committed = vm_memory_committed() + dm->num_pages_ballooned;

	vmbus_sendpacket(dm->dev->channel, &status,
				sizeof(struct dm_status),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND, 0);

}

static void free_balloon_pages(struct hv_dynmem_device *dm,
			 union dm_mem_page_range *range_array)
{
	int num_pages = range_array->finfo.page_cnt;
	__u64 start_frame = range_array->finfo.start_page;
	struct page *pg;
	int i;

	for (i = 0; i < num_pages; i++) {
		pg = pfn_to_page(i + start_frame);
		__free_page(pg);
		dm->num_pages_ballooned--;
	}
}



static int  alloc_balloon_pages(struct hv_dynmem_device *dm, int num_pages,
			 struct dm_balloon_response *bl_resp, int alloc_unit,
			 bool *alloc_error)
{
	int i = 0;
	struct page *pg;

	if (num_pages < alloc_unit)
		return 0;

	for (i = 0; (i * alloc_unit) < num_pages; i++) {
		if (bl_resp->hdr.size + sizeof(union dm_mem_page_range) >
			PAGE_SIZE)
			return i * alloc_unit;

		/*
		 * We execute this code in a thread context. Furthermore,
		 * we don't want the kernel to try too hard.
		 */
		pg = alloc_pages(GFP_HIGHUSER | __GFP_NORETRY |
				__GFP_NOMEMALLOC | __GFP_NOWARN,
				get_order(alloc_unit << PAGE_SHIFT));

		if (!pg) {
			*alloc_error = true;
			return i * alloc_unit;
		}


		dm->num_pages_ballooned += alloc_unit;

		bl_resp->range_count++;
		bl_resp->range_array[i].finfo.start_page =
			page_to_pfn(pg);
		bl_resp->range_array[i].finfo.page_cnt = alloc_unit;
		bl_resp->hdr.size += sizeof(union dm_mem_page_range);

	}

	return num_pages;
}



static void balloon_up(struct hv_dynmem_device *dm, struct dm_balloon *req)
{
	int num_pages = req->num_pages;
	int num_ballooned = 0;
	struct dm_balloon_response *bl_resp;
	int alloc_unit;
	int ret;
	bool alloc_error = false;
	bool done = false;
	int i;


	/*
	 * Currently, we only support 4k allocations.
	 */
	alloc_unit = 1;

	while (!done) {
		bl_resp = (struct dm_balloon_response *)send_buffer;
		memset(send_buffer, 0, PAGE_SIZE);
		bl_resp->hdr.type = DM_BALLOON_RESPONSE;
		bl_resp->hdr.trans_id = atomic_inc_return(&trans_id);
		bl_resp->hdr.size = sizeof(struct dm_balloon_response);
		bl_resp->more_pages = 1;


		num_pages -= num_ballooned;
		num_ballooned = alloc_balloon_pages(dm, num_pages,
						bl_resp, alloc_unit,
						 &alloc_error);

		if ((alloc_error) || (num_ballooned == num_pages)) {
			bl_resp->more_pages = 0;
			done = true;
			dm->state = DM_INITIALIZED;
		}

		/*
		 * We are pushing a lot of data through the channel;
		 * deal with transient failures caused because of the
		 * lack of space in the ring buffer.
		 */

		do {
			ret = vmbus_sendpacket(dm_device.dev->channel,
						bl_resp,
						bl_resp->hdr.size,
						(unsigned long)NULL,
						VM_PKT_DATA_INBAND, 0);

			if (ret == -EAGAIN)
				msleep(20);

		} while (ret == -EAGAIN);

		if (ret) {
			/*
			 * Free up the memory we allocatted.
			 */
			pr_info("Balloon response failed\n");

			for (i = 0; i < bl_resp->range_count; i++)
				free_balloon_pages(dm,
						 &bl_resp->range_array[i]);

			done = true;
		}
	}

}

static void balloon_down(struct hv_dynmem_device *dm,
			struct dm_unballoon_request *req)
{
	union dm_mem_page_range *range_array = req->range_array;
	int range_count = req->range_count;
	struct dm_unballoon_response resp;
	int i;

	for (i = 0; i < range_count; i++)
		free_balloon_pages(dm, &range_array[i]);

	if (req->more_pages == 1)
		return;

	memset(&resp, 0, sizeof(struct dm_unballoon_response));
	resp.hdr.type = DM_UNBALLOON_RESPONSE;
	resp.hdr.trans_id = atomic_inc_return(&trans_id);
	resp.hdr.size = sizeof(struct dm_unballoon_response);

	vmbus_sendpacket(dm_device.dev->channel, &resp,
				sizeof(struct dm_unballoon_response),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND, 0);

	dm->state = DM_INITIALIZED;
}

static void balloon_onchannelcallback(void *context);

static int dm_thread_func(void *dm_dev)
{
	struct hv_dynmem_device *dm = dm_dev;
	int t;
	unsigned long  scan_start;

	while (!kthread_should_stop()) {
		t = wait_for_completion_timeout(&dm_device.config_event, 1*HZ);
		/*
		 * The host expects us to post information on the memory
		 * pressure every second.
		 */

		if (t == 0)
			post_status(dm);

		scan_start = jiffies;
		switch (dm->state) {
		case DM_BALLOON_UP:
			balloon_up(dm, (struct dm_balloon *)recv_buffer);
			break;

		case DM_HOT_ADD:
			hot_add_req(dm, (struct dm_hot_add *)recv_buffer);
			break;
		default:
			break;
		}

		if (!time_in_range(jiffies, scan_start, scan_start + HZ))
			post_status(dm);

	}

	return 0;
}


static void version_resp(struct hv_dynmem_device *dm,
			struct dm_version_response *vresp)
{
	struct dm_version_request version_req;
	int ret;

	if (vresp->is_accepted) {
		/*
		 * We are done; wakeup the
		 * context waiting for version
		 * negotiation.
		 */
		complete(&dm->host_event);
		return;
	}
	/*
	 * If there are more versions to try, continue
	 * with negotiations; if not
	 * shutdown the service since we are not able
	 * to negotiate a suitable version number
	 * with the host.
	 */
	if (dm->next_version == 0)
		goto version_error;

	dm->next_version = 0;
	memset(&version_req, 0, sizeof(struct dm_version_request));
	version_req.hdr.type = DM_VERSION_REQUEST;
	version_req.hdr.size = sizeof(struct dm_version_request);
	version_req.hdr.trans_id = atomic_inc_return(&trans_id);
	version_req.version.version = DYNMEM_PROTOCOL_VERSION_WIN7;
	version_req.is_last_attempt = 1;

	ret = vmbus_sendpacket(dm->dev->channel, &version_req,
				sizeof(struct dm_version_request),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND, 0);

	if (ret)
		goto version_error;

	return;

version_error:
	dm->state = DM_INIT_ERROR;
	complete(&dm->host_event);
}

static void cap_resp(struct hv_dynmem_device *dm,
			struct dm_capabilities_resp_msg *cap_resp)
{
	if (!cap_resp->is_accepted) {
		pr_info("Capabilities not accepted by host\n");
		dm->state = DM_INIT_ERROR;
	}
	complete(&dm->host_event);
}

static void balloon_onchannelcallback(void *context)
{
	struct hv_device *dev = context;
	u32 recvlen;
	u64 requestid;
	struct dm_message *dm_msg;
	struct dm_header *dm_hdr;
	struct hv_dynmem_device *dm = hv_get_drvdata(dev);

	memset(recv_buffer, 0, sizeof(recv_buffer));
	vmbus_recvpacket(dev->channel, recv_buffer,
			 PAGE_SIZE, &recvlen, &requestid);

	if (recvlen > 0) {
		dm_msg = (struct dm_message *)recv_buffer;
		dm_hdr = &dm_msg->hdr;

		switch (dm_hdr->type) {
		case DM_VERSION_RESPONSE:
			version_resp(dm,
				 (struct dm_version_response *)dm_msg);
			break;

		case DM_CAPABILITIES_RESPONSE:
			cap_resp(dm,
				 (struct dm_capabilities_resp_msg *)dm_msg);
			break;

		case DM_BALLOON_REQUEST:
			dm->state = DM_BALLOON_UP;
			complete(&dm->config_event);
			break;

		case DM_UNBALLOON_REQUEST:
			dm->state = DM_BALLOON_DOWN;
			balloon_down(dm,
				 (struct dm_unballoon_request *)recv_buffer);
			break;

		case DM_MEM_HOT_ADD_REQUEST:
			dm->state = DM_HOT_ADD;
			complete(&dm->config_event);
			break;

		case DM_INFO_MESSAGE:
			process_info(dm, (struct dm_info_msg *)dm_msg);
			break;

		default:
			pr_err("Unhandled message: type: %d\n", dm_hdr->type);

		}
	}

}

static int balloon_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{
	int ret, t;
	struct dm_version_request version_req;
	struct dm_capabilities cap_msg;

	do_hot_add = hot_add;

	/*
	 * First allocate a send buffer.
	 */

	send_buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!send_buffer)
		return -ENOMEM;

	ret = vmbus_open(dev->channel, dm_ring_size, dm_ring_size, NULL, 0,
			balloon_onchannelcallback, dev);

	if (ret)
		goto probe_error0;

	dm_device.dev = dev;
	dm_device.state = DM_INITIALIZING;
	dm_device.next_version = DYNMEM_PROTOCOL_VERSION_WIN7;
	init_completion(&dm_device.host_event);
	init_completion(&dm_device.config_event);

	dm_device.thread =
		 kthread_run(dm_thread_func, &dm_device, "hv_balloon");
	if (IS_ERR(dm_device.thread)) {
		ret = PTR_ERR(dm_device.thread);
		goto probe_error1;
	}

	hv_set_drvdata(dev, &dm_device);
	/*
	 * Initiate the hand shake with the host and negotiate
	 * a version that the host can support. We start with the
	 * highest version number and go down if the host cannot
	 * support it.
	 */
	memset(&version_req, 0, sizeof(struct dm_version_request));
	version_req.hdr.type = DM_VERSION_REQUEST;
	version_req.hdr.size = sizeof(struct dm_version_request);
	version_req.hdr.trans_id = atomic_inc_return(&trans_id);
	version_req.version.version = DYNMEM_PROTOCOL_VERSION_WIN8;
	version_req.is_last_attempt = 0;

	ret = vmbus_sendpacket(dev->channel, &version_req,
				sizeof(struct dm_version_request),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND,
				VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		goto probe_error2;

	t = wait_for_completion_timeout(&dm_device.host_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto probe_error2;
	}

	/*
	 * If we could not negotiate a compatible version with the host
	 * fail the probe function.
	 */
	if (dm_device.state == DM_INIT_ERROR) {
		ret = -ETIMEDOUT;
		goto probe_error2;
	}
	/*
	 * Now submit our capabilities to the host.
	 */
	memset(&cap_msg, 0, sizeof(struct dm_capabilities));
	cap_msg.hdr.type = DM_CAPABILITIES_REPORT;
	cap_msg.hdr.size = sizeof(struct dm_capabilities);
	cap_msg.hdr.trans_id = atomic_inc_return(&trans_id);

	cap_msg.caps.cap_bits.balloon = 1;
	/*
	 * While we currently don't support hot-add,
	 * we still advertise this capability since the
	 * host requires that guests partcipating in the
	 * dynamic memory protocol support hot add.
	 */
	cap_msg.caps.cap_bits.hot_add = 1;

	/*
	 * Currently the host does not use these
	 * values and we set them to what is done in the
	 * Windows driver.
	 */
	cap_msg.min_page_cnt = 0;
	cap_msg.max_page_number = -1;

	ret = vmbus_sendpacket(dev->channel, &cap_msg,
				sizeof(struct dm_capabilities),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND,
				VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret)
		goto probe_error2;

	t = wait_for_completion_timeout(&dm_device.host_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto probe_error2;
	}

	/*
	 * If the host does not like our capabilities,
	 * fail the probe function.
	 */
	if (dm_device.state == DM_INIT_ERROR) {
		ret = -ETIMEDOUT;
		goto probe_error2;
	}

	dm_device.state = DM_INITIALIZED;

	return 0;

probe_error2:
	kthread_stop(dm_device.thread);

probe_error1:
	vmbus_close(dev->channel);
probe_error0:
	kfree(send_buffer);
	return ret;
}

static int balloon_remove(struct hv_device *dev)
{
	struct hv_dynmem_device *dm = hv_get_drvdata(dev);

	if (dm->num_pages_ballooned != 0)
		pr_warn("Ballooned pages: %d\n", dm->num_pages_ballooned);

	vmbus_close(dev->channel);
	kthread_stop(dm->thread);
	kfree(send_buffer);

	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Dynamic Memory Class ID */
	/* 525074DC-8985-46e2-8057-A307DC18A502 */
	{ HV_DM_GUID, },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static  struct hv_driver balloon_drv = {
	.name = "hv_balloon",
	.id_table = id_table,
	.probe =  balloon_probe,
	.remove =  balloon_remove,
};

static int __init init_balloon_drv(void)
{

	return vmbus_driver_register(&balloon_drv);
}

static void exit_balloon_drv(void)
{

	vmbus_driver_unregister(&balloon_drv);
}

module_init(init_balloon_drv);
module_exit(exit_balloon_drv);

MODULE_DESCRIPTION("Hyper-V Balloon");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_LICENSE("GPL");
