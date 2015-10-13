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
#include <linux/jiffies.h>
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
	DYNMEM_PROTOCOL_VERSION_3 = DYNMEM_MAKE_VERSION(2, 0),

	DYNMEM_PROTOCOL_VERSION_WIN7 = DYNMEM_PROTOCOL_VERSION_1,
	DYNMEM_PROTOCOL_VERSION_WIN8 = DYNMEM_PROTOCOL_VERSION_2,
	DYNMEM_PROTOCOL_VERSION_WIN10 = DYNMEM_PROTOCOL_VERSION_3,

	DYNMEM_PROTOCOL_VERSION_CURRENT = DYNMEM_PROTOCOL_VERSION_WIN10
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
		/*
		 * To support guests that may have alignment
		 * limitations on hot-add, the guest can specify
		 * its alignment requirements; a value of n
		 * represents an alignment of 2^n in mega bytes.
		 */
		__u64 hot_add_alignment:4;
		__u64 reservedz:58;
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

/*
 * State to manage hot adding memory into the guest.
 * The range start_pfn : end_pfn specifies the range
 * that the host has asked us to hot add. The range
 * start_pfn : ha_end_pfn specifies the range that we have
 * currently hot added. We hot add in multiples of 128M
 * chunks; it is possible that we may not be able to bring
 * online all the pages in the region. The range
 * covered_end_pfn defines the pages that can
 * be brough online.
 */

struct hv_hotadd_state {
	struct list_head list;
	unsigned long start_pfn;
	unsigned long covered_end_pfn;
	unsigned long ha_end_pfn;
	unsigned long end_pfn;
};

struct balloon_state {
	__u32 num_pages;
	struct work_struct wrk;
};

struct hot_add_wrk {
	union dm_mem_page_range ha_page_range;
	union dm_mem_page_range ha_region_range;
	struct work_struct wrk;
};

static bool hot_add = true;
static bool do_hot_add;
/*
 * Delay reporting memory pressure by
 * the specified number of seconds.
 */
static uint pressure_report_delay = 45;

/*
 * The last time we posted a pressure report to host.
 */
static unsigned long last_post_time;

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
#define HA_CHUNK (32 * 1024)

struct hv_dynmem_device {
	struct hv_device *dev;
	enum hv_dm_state state;
	struct completion host_event;
	struct completion config_event;

	/*
	 * Number of pages we have currently ballooned out.
	 */
	unsigned int num_pages_ballooned;
	unsigned int num_pages_onlined;
	unsigned int num_pages_added;

	/*
	 * State to manage the ballooning (up) operation.
	 */
	struct balloon_state balloon_wrk;

	/*
	 * State to execute the "hot-add" operation.
	 */
	struct hot_add_wrk ha_wrk;

	/*
	 * This state tracks if the host has specified a hot-add
	 * region.
	 */
	bool host_specified_ha_region;

	/*
	 * State to synchronize hot-add.
	 */
	struct completion  ol_waitevent;
	bool ha_waiting;
	/*
	 * This thread handles hot-add
	 * requests from the host as well as notifying
	 * the host with regards to memory pressure in
	 * the guest.
	 */
	struct task_struct *thread;

	struct mutex ha_region_mutex;

	/*
	 * A list of hot-add regions.
	 */
	struct list_head ha_region_list;

	/*
	 * We start with the highest version we can support
	 * and downgrade based on the host; we save here the
	 * next version to try.
	 */
	__u32 next_version;
};

static struct hv_dynmem_device dm_device;

static void post_status(struct hv_dynmem_device *dm);

#ifdef CONFIG_MEMORY_HOTPLUG
static int hv_memory_notifier(struct notifier_block *nb, unsigned long val,
			      void *v)
{
	struct memory_notify *mem = (struct memory_notify *)v;

	switch (val) {
	case MEM_GOING_ONLINE:
		mutex_lock(&dm_device.ha_region_mutex);
		break;

	case MEM_ONLINE:
		dm_device.num_pages_onlined += mem->nr_pages;
	case MEM_CANCEL_ONLINE:
		if (val == MEM_ONLINE ||
		    mutex_is_locked(&dm_device.ha_region_mutex))
			mutex_unlock(&dm_device.ha_region_mutex);
		if (dm_device.ha_waiting) {
			dm_device.ha_waiting = false;
			complete(&dm_device.ol_waitevent);
		}
		break;

	case MEM_OFFLINE:
		mutex_lock(&dm_device.ha_region_mutex);
		dm_device.num_pages_onlined -= mem->nr_pages;
		mutex_unlock(&dm_device.ha_region_mutex);
		break;
	case MEM_GOING_OFFLINE:
	case MEM_CANCEL_OFFLINE:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block hv_memory_nb = {
	.notifier_call = hv_memory_notifier,
	.priority = 0
};


static void hv_bring_pgs_online(unsigned long start_pfn, unsigned long size)
{
	int i;

	for (i = 0; i < size; i++) {
		struct page *pg;
		pg = pfn_to_page(start_pfn + i);
		__online_page_set_limits(pg);
		__online_page_increment_counters(pg);
		__online_page_free(pg);
	}
}

static void hv_mem_hot_add(unsigned long start, unsigned long size,
				unsigned long pfn_count,
				struct hv_hotadd_state *has)
{
	int ret = 0;
	int i, nid;
	unsigned long start_pfn;
	unsigned long processed_pfn;
	unsigned long total_pfn = pfn_count;

	for (i = 0; i < (size/HA_CHUNK); i++) {
		start_pfn = start + (i * HA_CHUNK);
		has->ha_end_pfn +=  HA_CHUNK;

		if (total_pfn > HA_CHUNK) {
			processed_pfn = HA_CHUNK;
			total_pfn -= HA_CHUNK;
		} else {
			processed_pfn = total_pfn;
			total_pfn = 0;
		}

		has->covered_end_pfn +=  processed_pfn;

		init_completion(&dm_device.ol_waitevent);
		dm_device.ha_waiting = true;

		mutex_unlock(&dm_device.ha_region_mutex);
		nid = memory_add_physaddr_to_nid(PFN_PHYS(start_pfn));
		ret = add_memory(nid, PFN_PHYS((start_pfn)),
				(HA_CHUNK << PAGE_SHIFT));

		if (ret) {
			pr_info("hot_add memory failed error is %d\n", ret);
			if (ret == -EEXIST) {
				/*
				 * This error indicates that the error
				 * is not a transient failure. This is the
				 * case where the guest's physical address map
				 * precludes hot adding memory. Stop all further
				 * memory hot-add.
				 */
				do_hot_add = false;
			}
			has->ha_end_pfn -= HA_CHUNK;
			has->covered_end_pfn -=  processed_pfn;
			mutex_lock(&dm_device.ha_region_mutex);
			break;
		}

		/*
		 * Wait for the memory block to be onlined.
		 * Since the hot add has succeeded, it is ok to
		 * proceed even if the pages in the hot added region
		 * have not been "onlined" within the allowed time.
		 */
		wait_for_completion_timeout(&dm_device.ol_waitevent, 5*HZ);
		mutex_lock(&dm_device.ha_region_mutex);
		post_status(&dm_device);
	}

	return;
}

static void hv_online_page(struct page *pg)
{
	struct list_head *cur;
	struct hv_hotadd_state *has;
	unsigned long cur_start_pgp;
	unsigned long cur_end_pgp;

	list_for_each(cur, &dm_device.ha_region_list) {
		has = list_entry(cur, struct hv_hotadd_state, list);
		cur_start_pgp = (unsigned long)pfn_to_page(has->start_pfn);
		cur_end_pgp = (unsigned long)pfn_to_page(has->covered_end_pfn);

		if (((unsigned long)pg >= cur_start_pgp) &&
			((unsigned long)pg < cur_end_pgp)) {
			/*
			 * This frame is currently backed; online the
			 * page.
			 */
			__online_page_set_limits(pg);
			__online_page_increment_counters(pg);
			__online_page_free(pg);
		}
	}
}

static bool pfn_covered(unsigned long start_pfn, unsigned long pfn_cnt)
{
	struct list_head *cur;
	struct hv_hotadd_state *has;
	unsigned long residual, new_inc;

	if (list_empty(&dm_device.ha_region_list))
		return false;

	list_for_each(cur, &dm_device.ha_region_list) {
		has = list_entry(cur, struct hv_hotadd_state, list);

		/*
		 * If the pfn range we are dealing with is not in the current
		 * "hot add block", move on.
		 */
		if ((start_pfn >= has->end_pfn))
			continue;
		/*
		 * If the current hot add-request extends beyond
		 * our current limit; extend it.
		 */
		if ((start_pfn + pfn_cnt) > has->end_pfn) {
			residual = (start_pfn + pfn_cnt - has->end_pfn);
			/*
			 * Extend the region by multiples of HA_CHUNK.
			 */
			new_inc = (residual / HA_CHUNK) * HA_CHUNK;
			if (residual % HA_CHUNK)
				new_inc += HA_CHUNK;

			has->end_pfn += new_inc;
		}

		/*
		 * If the current start pfn is not where the covered_end
		 * is, update it.
		 */

		if (has->covered_end_pfn != start_pfn)
			has->covered_end_pfn = start_pfn;

		return true;

	}

	return false;
}

static unsigned long handle_pg_range(unsigned long pg_start,
					unsigned long pg_count)
{
	unsigned long start_pfn = pg_start;
	unsigned long pfn_cnt = pg_count;
	unsigned long size;
	struct list_head *cur;
	struct hv_hotadd_state *has;
	unsigned long pgs_ol = 0;
	unsigned long old_covered_state;

	if (list_empty(&dm_device.ha_region_list))
		return 0;

	list_for_each(cur, &dm_device.ha_region_list) {
		has = list_entry(cur, struct hv_hotadd_state, list);

		/*
		 * If the pfn range we are dealing with is not in the current
		 * "hot add block", move on.
		 */
		if ((start_pfn >= has->end_pfn))
			continue;

		old_covered_state = has->covered_end_pfn;

		if (start_pfn < has->ha_end_pfn) {
			/*
			 * This is the case where we are backing pages
			 * in an already hot added region. Bring
			 * these pages online first.
			 */
			pgs_ol = has->ha_end_pfn - start_pfn;
			if (pgs_ol > pfn_cnt)
				pgs_ol = pfn_cnt;

			/*
			 * Check if the corresponding memory block is already
			 * online by checking its last previously backed page.
			 * In case it is we need to bring rest (which was not
			 * backed previously) online too.
			 */
			if (start_pfn > has->start_pfn &&
			    !PageReserved(pfn_to_page(start_pfn - 1)))
				hv_bring_pgs_online(start_pfn, pgs_ol);

			has->covered_end_pfn +=  pgs_ol;
			pfn_cnt -= pgs_ol;
		}

		if ((has->ha_end_pfn < has->end_pfn) && (pfn_cnt > 0)) {
			/*
			 * We have some residual hot add range
			 * that needs to be hot added; hot add
			 * it now. Hot add a multiple of
			 * of HA_CHUNK that fully covers the pages
			 * we have.
			 */
			size = (has->end_pfn - has->ha_end_pfn);
			if (pfn_cnt <= size) {
				size = ((pfn_cnt / HA_CHUNK) * HA_CHUNK);
				if (pfn_cnt % HA_CHUNK)
					size += HA_CHUNK;
			} else {
				pfn_cnt = size;
			}
			hv_mem_hot_add(has->ha_end_pfn, size, pfn_cnt, has);
		}
		/*
		 * If we managed to online any pages that were given to us,
		 * we declare success.
		 */
		return has->covered_end_pfn - old_covered_state;

	}

	return 0;
}

static unsigned long process_hot_add(unsigned long pg_start,
					unsigned long pfn_cnt,
					unsigned long rg_start,
					unsigned long rg_size)
{
	struct hv_hotadd_state *ha_region = NULL;

	if (pfn_cnt == 0)
		return 0;

	if (!dm_device.host_specified_ha_region)
		if (pfn_covered(pg_start, pfn_cnt))
			goto do_pg_range;

	/*
	 * If the host has specified a hot-add range; deal with it first.
	 */

	if (rg_size != 0) {
		ha_region = kzalloc(sizeof(struct hv_hotadd_state), GFP_KERNEL);
		if (!ha_region)
			return 0;

		INIT_LIST_HEAD(&ha_region->list);

		list_add_tail(&ha_region->list, &dm_device.ha_region_list);
		ha_region->start_pfn = rg_start;
		ha_region->ha_end_pfn = rg_start;
		ha_region->covered_end_pfn = pg_start;
		ha_region->end_pfn = rg_start + rg_size;
	}

do_pg_range:
	/*
	 * Process the page range specified; bringing them
	 * online if possible.
	 */
	return handle_pg_range(pg_start, pfn_cnt);
}

#endif

static void hot_add_req(struct work_struct *dummy)
{
	struct dm_hot_add_response resp;
#ifdef CONFIG_MEMORY_HOTPLUG
	unsigned long pg_start, pfn_cnt;
	unsigned long rg_start, rg_sz;
#endif
	struct hv_dynmem_device *dm = &dm_device;

	memset(&resp, 0, sizeof(struct dm_hot_add_response));
	resp.hdr.type = DM_MEM_HOT_ADD_RESPONSE;
	resp.hdr.size = sizeof(struct dm_hot_add_response);

#ifdef CONFIG_MEMORY_HOTPLUG
	mutex_lock(&dm_device.ha_region_mutex);
	pg_start = dm->ha_wrk.ha_page_range.finfo.start_page;
	pfn_cnt = dm->ha_wrk.ha_page_range.finfo.page_cnt;

	rg_start = dm->ha_wrk.ha_region_range.finfo.start_page;
	rg_sz = dm->ha_wrk.ha_region_range.finfo.page_cnt;

	if ((rg_start == 0) && (!dm->host_specified_ha_region)) {
		unsigned long region_size;
		unsigned long region_start;

		/*
		 * The host has not specified the hot-add region.
		 * Based on the hot-add page range being specified,
		 * compute a hot-add region that can cover the pages
		 * that need to be hot-added while ensuring the alignment
		 * and size requirements of Linux as it relates to hot-add.
		 */
		region_start = pg_start;
		region_size = (pfn_cnt / HA_CHUNK) * HA_CHUNK;
		if (pfn_cnt % HA_CHUNK)
			region_size += HA_CHUNK;

		region_start = (pg_start / HA_CHUNK) * HA_CHUNK;

		rg_start = region_start;
		rg_sz = region_size;
	}

	if (do_hot_add)
		resp.page_count = process_hot_add(pg_start, pfn_cnt,
						rg_start, rg_sz);

	dm->num_pages_added += resp.page_count;
	mutex_unlock(&dm_device.ha_region_mutex);
#endif
	/*
	 * The result field of the response structure has the
	 * following semantics:
	 *
	 * 1. If all or some pages hot-added: Guest should return success.
	 *
	 * 2. If no pages could be hot-added:
	 *
	 * If the guest returns success, then the host
	 * will not attempt any further hot-add operations. This
	 * signifies a permanent failure.
	 *
	 * If the guest returns failure, then this failure will be
	 * treated as a transient failure and the host may retry the
	 * hot-add operation after some delay.
	 */
	if (resp.page_count > 0)
		resp.result = 1;
	else if (!do_hot_add)
		resp.result = 1;
	else
		resp.result = 0;

	if (!do_hot_add || (resp.page_count == 0))
		pr_info("Memory hot add failed\n");

	dm->state = DM_INITIALIZED;
	resp.hdr.trans_id = atomic_inc_return(&trans_id);
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

static unsigned long compute_balloon_floor(void)
{
	unsigned long min_pages;
#define MB2PAGES(mb) ((mb) << (20 - PAGE_SHIFT))
	/* Simple continuous piecewiese linear function:
	 *  max MiB -> min MiB  gradient
	 *       0         0
	 *      16        16
	 *      32        24
	 *     128        72    (1/2)
	 *     512       168    (1/4)
	 *    2048       360    (1/8)
	 *    8192       744    (1/16)
	 *   32768      1512	(1/32)
	 */
	if (totalram_pages < MB2PAGES(128))
		min_pages = MB2PAGES(8) + (totalram_pages >> 1);
	else if (totalram_pages < MB2PAGES(512))
		min_pages = MB2PAGES(40) + (totalram_pages >> 2);
	else if (totalram_pages < MB2PAGES(2048))
		min_pages = MB2PAGES(104) + (totalram_pages >> 3);
	else if (totalram_pages < MB2PAGES(8192))
		min_pages = MB2PAGES(232) + (totalram_pages >> 4);
	else
		min_pages = MB2PAGES(488) + (totalram_pages >> 5);
#undef MB2PAGES
	return min_pages;
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
	unsigned long now = jiffies;
	unsigned long last_post = last_post_time;

	if (pressure_report_delay > 0) {
		--pressure_report_delay;
		return;
	}

	if (!time_after(now, (last_post_time + HZ)))
		return;

	si_meminfo(&val);
	memset(&status, 0, sizeof(struct dm_status));
	status.hdr.type = DM_STATUS_REPORT;
	status.hdr.size = sizeof(struct dm_status);
	status.hdr.trans_id = atomic_inc_return(&trans_id);

	/*
	 * The host expects the guest to report free and committed memory.
	 * Furthermore, the host expects the pressure information to include
	 * the ballooned out pages. For a given amount of memory that we are
	 * managing we need to compute a floor below which we should not
	 * balloon. Compute this and add it to the pressure report.
	 * We also need to report all offline pages (num_pages_added -
	 * num_pages_onlined) as committed to the host, otherwise it can try
	 * asking us to balloon them out.
	 */
	status.num_avail = val.freeram;
	status.num_committed = vm_memory_committed() +
		dm->num_pages_ballooned +
		(dm->num_pages_added > dm->num_pages_onlined ?
		 dm->num_pages_added - dm->num_pages_onlined : 0) +
		compute_balloon_floor();

	/*
	 * If our transaction ID is no longer current, just don't
	 * send the status. This can happen if we were interrupted
	 * after we picked our transaction ID.
	 */
	if (status.hdr.trans_id != atomic_read(&trans_id))
		return;

	/*
	 * If the last post time that we sampled has changed,
	 * we have raced, don't post the status.
	 */
	if (last_post != last_post_time)
		return;

	last_post_time = jiffies;
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



static unsigned int alloc_balloon_pages(struct hv_dynmem_device *dm,
					unsigned int num_pages,
					struct dm_balloon_response *bl_resp,
					int alloc_unit)
{
	unsigned int i = 0;
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

		if (!pg)
			return i * alloc_unit;

		dm->num_pages_ballooned += alloc_unit;

		/*
		 * If we allocatted 2M pages; split them so we
		 * can free them in any order we get.
		 */

		if (alloc_unit != 1)
			split_page(pg, get_order(alloc_unit << PAGE_SHIFT));

		bl_resp->range_count++;
		bl_resp->range_array[i].finfo.start_page =
			page_to_pfn(pg);
		bl_resp->range_array[i].finfo.page_cnt = alloc_unit;
		bl_resp->hdr.size += sizeof(union dm_mem_page_range);

	}

	return num_pages;
}



static void balloon_up(struct work_struct *dummy)
{
	unsigned int num_pages = dm_device.balloon_wrk.num_pages;
	unsigned int num_ballooned = 0;
	struct dm_balloon_response *bl_resp;
	int alloc_unit;
	int ret;
	bool done = false;
	int i;
	struct sysinfo val;
	unsigned long floor;

	/* The host balloons pages in 2M granularity. */
	WARN_ON_ONCE(num_pages % PAGES_IN_2M != 0);

	/*
	 * We will attempt 2M allocations. However, if we fail to
	 * allocate 2M chunks, we will go back to 4k allocations.
	 */
	alloc_unit = 512;

	si_meminfo(&val);
	floor = compute_balloon_floor();

	/* Refuse to balloon below the floor, keep the 2M granularity. */
	if (val.freeram < num_pages || val.freeram - num_pages < floor) {
		num_pages = val.freeram > floor ? (val.freeram - floor) : 0;
		num_pages -= num_pages % PAGES_IN_2M;
	}

	while (!done) {
		bl_resp = (struct dm_balloon_response *)send_buffer;
		memset(send_buffer, 0, PAGE_SIZE);
		bl_resp->hdr.type = DM_BALLOON_RESPONSE;
		bl_resp->hdr.size = sizeof(struct dm_balloon_response);
		bl_resp->more_pages = 1;


		num_pages -= num_ballooned;
		num_ballooned = alloc_balloon_pages(&dm_device, num_pages,
						    bl_resp, alloc_unit);

		if (alloc_unit != 1 && num_ballooned == 0) {
			alloc_unit = 1;
			continue;
		}

		if (num_ballooned == 0 || num_ballooned == num_pages) {
			bl_resp->more_pages = 0;
			done = true;
			dm_device.state = DM_INITIALIZED;
		}

		/*
		 * We are pushing a lot of data through the channel;
		 * deal with transient failures caused because of the
		 * lack of space in the ring buffer.
		 */

		do {
			bl_resp->hdr.trans_id = atomic_inc_return(&trans_id);
			ret = vmbus_sendpacket(dm_device.dev->channel,
						bl_resp,
						bl_resp->hdr.size,
						(unsigned long)NULL,
						VM_PKT_DATA_INBAND, 0);

			if (ret == -EAGAIN)
				msleep(20);
			post_status(&dm_device);
		} while (ret == -EAGAIN);

		if (ret) {
			/*
			 * Free up the memory we allocatted.
			 */
			pr_info("Balloon response failed\n");

			for (i = 0; i < bl_resp->range_count; i++)
				free_balloon_pages(&dm_device,
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

	for (i = 0; i < range_count; i++) {
		free_balloon_pages(dm, &range_array[i]);
		complete(&dm_device.config_event);
	}

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

	while (!kthread_should_stop()) {
		wait_for_completion_interruptible_timeout(
						&dm_device.config_event, 1*HZ);
		/*
		 * The host expects us to post information on the memory
		 * pressure every second.
		 */
		reinit_completion(&dm_device.config_event);
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

	memset(&version_req, 0, sizeof(struct dm_version_request));
	version_req.hdr.type = DM_VERSION_REQUEST;
	version_req.hdr.size = sizeof(struct dm_version_request);
	version_req.hdr.trans_id = atomic_inc_return(&trans_id);
	version_req.version.version = dm->next_version;

	/*
	 * Set the next version to try in case current version fails.
	 * Win7 protocol ought to be the last one to try.
	 */
	switch (version_req.version.version) {
	case DYNMEM_PROTOCOL_VERSION_WIN8:
		dm->next_version = DYNMEM_PROTOCOL_VERSION_WIN7;
		version_req.is_last_attempt = 0;
		break;
	default:
		dm->next_version = 0;
		version_req.is_last_attempt = 1;
	}

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
	struct dm_balloon *bal_msg;
	struct dm_hot_add *ha_msg;
	union dm_mem_page_range *ha_pg_range;
	union dm_mem_page_range *ha_region;

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
			if (dm->state == DM_BALLOON_UP)
				pr_warn("Currently ballooning\n");
			bal_msg = (struct dm_balloon *)recv_buffer;
			dm->state = DM_BALLOON_UP;
			dm_device.balloon_wrk.num_pages = bal_msg->num_pages;
			schedule_work(&dm_device.balloon_wrk.wrk);
			break;

		case DM_UNBALLOON_REQUEST:
			dm->state = DM_BALLOON_DOWN;
			balloon_down(dm,
				 (struct dm_unballoon_request *)recv_buffer);
			break;

		case DM_MEM_HOT_ADD_REQUEST:
			if (dm->state == DM_HOT_ADD)
				pr_warn("Currently hot-adding\n");
			dm->state = DM_HOT_ADD;
			ha_msg = (struct dm_hot_add *)recv_buffer;
			if (ha_msg->hdr.size == sizeof(struct dm_hot_add)) {
				/*
				 * This is a normal hot-add request specifying
				 * hot-add memory.
				 */
				ha_pg_range = &ha_msg->range;
				dm->ha_wrk.ha_page_range = *ha_pg_range;
				dm->ha_wrk.ha_region_range.page_range = 0;
			} else {
				/*
				 * Host is specifying that we first hot-add
				 * a region and then partially populate this
				 * region.
				 */
				dm->host_specified_ha_region = true;
				ha_pg_range = &ha_msg->range;
				ha_region = &ha_pg_range[1];
				dm->ha_wrk.ha_page_range = *ha_pg_range;
				dm->ha_wrk.ha_region_range = *ha_region;
			}
			schedule_work(&dm_device.ha_wrk.wrk);
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
	int ret;
	unsigned long t;
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
	dm_device.next_version = DYNMEM_PROTOCOL_VERSION_WIN8;
	init_completion(&dm_device.host_event);
	init_completion(&dm_device.config_event);
	INIT_LIST_HEAD(&dm_device.ha_region_list);
	mutex_init(&dm_device.ha_region_mutex);
	INIT_WORK(&dm_device.balloon_wrk.wrk, balloon_up);
	INIT_WORK(&dm_device.ha_wrk.wrk, hot_add_req);
	dm_device.host_specified_ha_region = false;

	dm_device.thread =
		 kthread_run(dm_thread_func, &dm_device, "hv_balloon");
	if (IS_ERR(dm_device.thread)) {
		ret = PTR_ERR(dm_device.thread);
		goto probe_error1;
	}

#ifdef CONFIG_MEMORY_HOTPLUG
	set_online_page_callback(&hv_online_page);
	register_memory_notifier(&hv_memory_nb);
#endif

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
	version_req.version.version = DYNMEM_PROTOCOL_VERSION_WIN10;
	version_req.is_last_attempt = 0;

	ret = vmbus_sendpacket(dev->channel, &version_req,
				sizeof(struct dm_version_request),
				(unsigned long)NULL,
				VM_PKT_DATA_INBAND, 0);
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
	cap_msg.caps.cap_bits.hot_add = 1;

	/*
	 * Specify our alignment requirements as it relates
	 * memory hot-add. Specify 128MB alignment.
	 */
	cap_msg.caps.cap_bits.hot_add_alignment = 7;

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
				VM_PKT_DATA_INBAND, 0);
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
#ifdef CONFIG_MEMORY_HOTPLUG
	restore_online_page_callback(&hv_online_page);
#endif
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
	struct list_head *cur, *tmp;
	struct hv_hotadd_state *has;

	if (dm->num_pages_ballooned != 0)
		pr_warn("Ballooned pages: %d\n", dm->num_pages_ballooned);

	cancel_work_sync(&dm->balloon_wrk.wrk);
	cancel_work_sync(&dm->ha_wrk.wrk);

	vmbus_close(dev->channel);
	kthread_stop(dm->thread);
	kfree(send_buffer);
#ifdef CONFIG_MEMORY_HOTPLUG
	restore_online_page_callback(&hv_online_page);
	unregister_memory_notifier(&hv_memory_nb);
#endif
	list_for_each_safe(cur, tmp, &dm->ha_region_list) {
		has = list_entry(cur, struct hv_hotadd_state, list);
		list_del(&has->list);
		kfree(has);
	}

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

module_init(init_balloon_drv);

MODULE_DESCRIPTION("Hyper-V Balloon");
MODULE_LICENSE("GPL");
