/*
 * VMware Balloon driver.
 *
 * Copyright (C) 2000-2014, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained by:	Xavier Deguillard <xdeguillard@vmware.com>
 *			Philip Moltmann <moltmann@vmware.com>
 */

/*
 * This is VMware physical memory management driver for Linux. The driver
 * acts like a "balloon" that can be inflated to reclaim physical pages by
 * reserving them in the guest and invalidating them in the monitor,
 * freeing up the underlying machine pages so they can be allocated to
 * other guests.  The balloon can also be deflated to allow the guest to
 * use more physical memory. Higher level policies can control the sizes
 * of balloons in VMs in order to manage physical memory resources.
 */

//#define DEBUG
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>
#include <asm/hypervisor.h>

MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION("VMware Memory Control (Balloon) Driver");
MODULE_VERSION("1.5.0.0-k");
MODULE_ALIAS("dmi:*:svnVMware*:*");
MODULE_ALIAS("vmware_vmmemctl");
MODULE_LICENSE("GPL");

/*
 * Various constants controlling rate of inflaint/deflating balloon,
 * measured in pages.
 */

/*
 * Rates of memory allocaton when guest experiences memory pressure
 * (driver performs sleeping allocations).
 */
#define VMW_BALLOON_RATE_ALLOC_MIN	512U
#define VMW_BALLOON_RATE_ALLOC_MAX	2048U
#define VMW_BALLOON_RATE_ALLOC_INC	16U

/*
 * When guest is under memory pressure, use a reduced page allocation
 * rate for next several cycles.
 */
#define VMW_BALLOON_SLOW_CYCLES		4

/*
 * Use __GFP_HIGHMEM to allow pages from HIGHMEM zone. We don't
 * allow wait (__GFP_RECLAIM) for NOSLEEP page allocations. Use
 * __GFP_NOWARN, to suppress page allocation failure warnings.
 */
#define VMW_PAGE_ALLOC_NOSLEEP		(__GFP_HIGHMEM|__GFP_NOWARN)

/*
 * Use GFP_HIGHUSER when executing in a separate kernel thread
 * context and allocation can sleep.  This is less stressful to
 * the guest memory system, since it allows the thread to block
 * while memory is reclaimed, and won't take pages from emergency
 * low-memory pools.
 */
#define VMW_PAGE_ALLOC_CANSLEEP		(GFP_HIGHUSER)

/* Maximum number of refused pages we accumulate during inflation cycle */
#define VMW_BALLOON_MAX_REFUSED		16

/*
 * Hypervisor communication port definitions.
 */
#define VMW_BALLOON_HV_PORT		0x5670
#define VMW_BALLOON_HV_MAGIC		0x456c6d6f
#define VMW_BALLOON_GUEST_ID		1	/* Linux */

enum vmwballoon_capabilities {
	/*
	 * Bit 0 is reserved and not associated to any capability.
	 */
	VMW_BALLOON_BASIC_CMDS			= (1 << 1),
	VMW_BALLOON_BATCHED_CMDS		= (1 << 2),
	VMW_BALLOON_BATCHED_2M_CMDS		= (1 << 3),
	VMW_BALLOON_SIGNALLED_WAKEUP_CMD	= (1 << 4),
};

#define VMW_BALLOON_CAPABILITIES	(VMW_BALLOON_BASIC_CMDS \
					| VMW_BALLOON_BATCHED_CMDS \
					| VMW_BALLOON_BATCHED_2M_CMDS \
					| VMW_BALLOON_SIGNALLED_WAKEUP_CMD)

#define VMW_BALLOON_2M_SHIFT		(9)
#define VMW_BALLOON_NUM_PAGE_SIZES	(2)

/*
 * Backdoor commands availability:
 *
 * START, GET_TARGET and GUEST_ID are always available,
 *
 * VMW_BALLOON_BASIC_CMDS:
 *	LOCK and UNLOCK commands,
 * VMW_BALLOON_BATCHED_CMDS:
 *	BATCHED_LOCK and BATCHED_UNLOCK commands.
 * VMW BALLOON_BATCHED_2M_CMDS:
 *	BATCHED_2M_LOCK and BATCHED_2M_UNLOCK commands,
 * VMW VMW_BALLOON_SIGNALLED_WAKEUP_CMD:
 *	VMW_BALLOON_CMD_VMCI_DOORBELL_SET command.
 */
#define VMW_BALLOON_CMD_START			0
#define VMW_BALLOON_CMD_GET_TARGET		1
#define VMW_BALLOON_CMD_LOCK			2
#define VMW_BALLOON_CMD_UNLOCK			3
#define VMW_BALLOON_CMD_GUEST_ID		4
#define VMW_BALLOON_CMD_BATCHED_LOCK		6
#define VMW_BALLOON_CMD_BATCHED_UNLOCK		7
#define VMW_BALLOON_CMD_BATCHED_2M_LOCK		8
#define VMW_BALLOON_CMD_BATCHED_2M_UNLOCK	9
#define VMW_BALLOON_CMD_VMCI_DOORBELL_SET	10


/* error codes */
#define VMW_BALLOON_SUCCESS		        0
#define VMW_BALLOON_FAILURE		        -1
#define VMW_BALLOON_ERROR_CMD_INVALID	        1
#define VMW_BALLOON_ERROR_PPN_INVALID	        2
#define VMW_BALLOON_ERROR_PPN_LOCKED	        3
#define VMW_BALLOON_ERROR_PPN_UNLOCKED	        4
#define VMW_BALLOON_ERROR_PPN_PINNED	        5
#define VMW_BALLOON_ERROR_PPN_NOTNEEDED	        6
#define VMW_BALLOON_ERROR_RESET		        7
#define VMW_BALLOON_ERROR_BUSY		        8

#define VMW_BALLOON_SUCCESS_WITH_CAPABILITIES	(0x03000000)

/* Batch page description */

/*
 * Layout of a page in the batch page:
 *
 * +-------------+----------+--------+
 * |             |          |        |
 * | Page number | Reserved | Status |
 * |             |          |        |
 * +-------------+----------+--------+
 * 64  PAGE_SHIFT          6         0
 *
 * The reserved field should be set to 0.
 */
#define VMW_BALLOON_BATCH_MAX_PAGES	(PAGE_SIZE / sizeof(u64))
#define VMW_BALLOON_BATCH_STATUS_MASK	((1UL << 5) - 1)
#define VMW_BALLOON_BATCH_PAGE_MASK	(~((1UL << PAGE_SHIFT) - 1))

struct vmballoon_batch_page {
	u64 pages[VMW_BALLOON_BATCH_MAX_PAGES];
};

static u64 vmballoon_batch_get_pa(struct vmballoon_batch_page *batch, int idx)
{
	return batch->pages[idx] & VMW_BALLOON_BATCH_PAGE_MASK;
}

static int vmballoon_batch_get_status(struct vmballoon_batch_page *batch,
				int idx)
{
	return (int)(batch->pages[idx] & VMW_BALLOON_BATCH_STATUS_MASK);
}

static void vmballoon_batch_set_pa(struct vmballoon_batch_page *batch, int idx,
				u64 pa)
{
	batch->pages[idx] = pa;
}


#define VMWARE_BALLOON_CMD(cmd, arg1, arg2, result)		\
({								\
	unsigned long __status, __dummy1, __dummy2, __dummy3;	\
	__asm__ __volatile__ ("inl %%dx" :			\
		"=a"(__status),					\
		"=c"(__dummy1),					\
		"=d"(__dummy2),					\
		"=b"(result),					\
		"=S" (__dummy3) :				\
		"0"(VMW_BALLOON_HV_MAGIC),			\
		"1"(VMW_BALLOON_CMD_##cmd),			\
		"2"(VMW_BALLOON_HV_PORT),			\
		"3"(arg1),					\
		"4" (arg2) :					\
		"memory");					\
	if (VMW_BALLOON_CMD_##cmd == VMW_BALLOON_CMD_START)	\
		result = __dummy1;				\
	result &= -1UL;						\
	__status & -1UL;					\
})

#ifdef CONFIG_DEBUG_FS
struct vmballoon_stats {
	unsigned int timer;
	unsigned int doorbell;

	/* allocation statistics */
	unsigned int alloc[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int alloc_fail[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int sleep_alloc;
	unsigned int sleep_alloc_fail;
	unsigned int refused_alloc[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int refused_free[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int free[VMW_BALLOON_NUM_PAGE_SIZES];

	/* monitor operations */
	unsigned int lock[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int lock_fail[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int unlock[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int unlock_fail[VMW_BALLOON_NUM_PAGE_SIZES];
	unsigned int target;
	unsigned int target_fail;
	unsigned int start;
	unsigned int start_fail;
	unsigned int guest_type;
	unsigned int guest_type_fail;
	unsigned int doorbell_set;
	unsigned int doorbell_unset;
};

#define STATS_INC(stat) (stat)++
#else
#define STATS_INC(stat)
#endif

struct vmballoon;

struct vmballoon_ops {
	void (*add_page)(struct vmballoon *b, int idx, struct page *p);
	int (*lock)(struct vmballoon *b, unsigned int num_pages,
			bool is_2m_pages, unsigned int *target);
	int (*unlock)(struct vmballoon *b, unsigned int num_pages,
			bool is_2m_pages, unsigned int *target);
};

struct vmballoon_page_size {
	/* list of reserved physical pages */
	struct list_head pages;

	/* transient list of non-balloonable pages */
	struct list_head refused_pages;
	unsigned int n_refused_pages;
};

struct vmballoon {
	struct vmballoon_page_size page_sizes[VMW_BALLOON_NUM_PAGE_SIZES];

	/* supported page sizes. 1 == 4k pages only, 2 == 4k and 2m pages */
	unsigned supported_page_sizes;

	/* balloon size in pages */
	unsigned int size;
	unsigned int target;

	/* reset flag */
	bool reset_required;

	/* adjustment rates (pages per second) */
	unsigned int rate_alloc;

	/* slowdown page allocations for next few cycles */
	unsigned int slow_allocation_cycles;

	unsigned long capabilities;

	struct vmballoon_batch_page *batch_page;
	unsigned int batch_max_pages;
	struct page *page;

	const struct vmballoon_ops *ops;

#ifdef CONFIG_DEBUG_FS
	/* statistics */
	struct vmballoon_stats stats;

	/* debugfs file exporting statistics */
	struct dentry *dbg_entry;
#endif

	struct sysinfo sysinfo;

	struct delayed_work dwork;

	struct vmci_handle vmci_doorbell;
};

static struct vmballoon balloon;

/*
 * Send "start" command to the host, communicating supported version
 * of the protocol.
 */
static bool vmballoon_send_start(struct vmballoon *b, unsigned long req_caps)
{
	unsigned long status, capabilities, dummy = 0;
	bool success;

	STATS_INC(b->stats.start);

	status = VMWARE_BALLOON_CMD(START, req_caps, dummy, capabilities);

	switch (status) {
	case VMW_BALLOON_SUCCESS_WITH_CAPABILITIES:
		b->capabilities = capabilities;
		success = true;
		break;
	case VMW_BALLOON_SUCCESS:
		b->capabilities = VMW_BALLOON_BASIC_CMDS;
		success = true;
		break;
	default:
		success = false;
	}

	if (b->capabilities & VMW_BALLOON_BATCHED_2M_CMDS)
		b->supported_page_sizes = 2;
	else
		b->supported_page_sizes = 1;

	if (!success) {
		pr_debug("%s - failed, hv returns %ld\n", __func__, status);
		STATS_INC(b->stats.start_fail);
	}
	return success;
}

static bool vmballoon_check_status(struct vmballoon *b, unsigned long status)
{
	switch (status) {
	case VMW_BALLOON_SUCCESS:
		return true;

	case VMW_BALLOON_ERROR_RESET:
		b->reset_required = true;
		/* fall through */

	default:
		return false;
	}
}

/*
 * Communicate guest type to the host so that it can adjust ballooning
 * algorithm to the one most appropriate for the guest. This command
 * is normally issued after sending "start" command and is part of
 * standard reset sequence.
 */
static bool vmballoon_send_guest_id(struct vmballoon *b)
{
	unsigned long status, dummy = 0;

	status = VMWARE_BALLOON_CMD(GUEST_ID, VMW_BALLOON_GUEST_ID, dummy,
				dummy);

	STATS_INC(b->stats.guest_type);

	if (vmballoon_check_status(b, status))
		return true;

	pr_debug("%s - failed, hv returns %ld\n", __func__, status);
	STATS_INC(b->stats.guest_type_fail);
	return false;
}

static u16 vmballoon_page_size(bool is_2m_page)
{
	if (is_2m_page)
		return 1 << VMW_BALLOON_2M_SHIFT;

	return 1;
}

/*
 * Retrieve desired balloon size from the host.
 */
static bool vmballoon_send_get_target(struct vmballoon *b, u32 *new_target)
{
	unsigned long status;
	unsigned long target;
	unsigned long limit;
	unsigned long dummy = 0;
	u32 limit32;

	/*
	 * si_meminfo() is cheap. Moreover, we want to provide dynamic
	 * max balloon size later. So let us call si_meminfo() every
	 * iteration.
	 */
	si_meminfo(&b->sysinfo);
	limit = b->sysinfo.totalram;

	/* Ensure limit fits in 32-bits */
	limit32 = (u32)limit;
	if (limit != limit32)
		return false;

	/* update stats */
	STATS_INC(b->stats.target);

	status = VMWARE_BALLOON_CMD(GET_TARGET, limit, dummy, target);
	if (vmballoon_check_status(b, status)) {
		*new_target = target;
		return true;
	}

	pr_debug("%s - failed, hv returns %ld\n", __func__, status);
	STATS_INC(b->stats.target_fail);
	return false;
}

/*
 * Notify the host about allocated page so that host can use it without
 * fear that guest will need it. Host may reject some pages, we need to
 * check the return value and maybe submit a different page.
 */
static int vmballoon_send_lock_page(struct vmballoon *b, unsigned long pfn,
				unsigned int *hv_status, unsigned int *target)
{
	unsigned long status, dummy = 0;
	u32 pfn32;

	pfn32 = (u32)pfn;
	if (pfn32 != pfn)
		return -1;

	STATS_INC(b->stats.lock[false]);

	*hv_status = status = VMWARE_BALLOON_CMD(LOCK, pfn, dummy, *target);
	if (vmballoon_check_status(b, status))
		return 0;

	pr_debug("%s - ppn %lx, hv returns %ld\n", __func__, pfn, status);
	STATS_INC(b->stats.lock_fail[false]);
	return 1;
}

static int vmballoon_send_batched_lock(struct vmballoon *b,
		unsigned int num_pages, bool is_2m_pages, unsigned int *target)
{
	unsigned long status;
	unsigned long pfn = PHYS_PFN(virt_to_phys(b->batch_page));

	STATS_INC(b->stats.lock[is_2m_pages]);

	if (is_2m_pages)
		status = VMWARE_BALLOON_CMD(BATCHED_2M_LOCK, pfn, num_pages,
				*target);
	else
		status = VMWARE_BALLOON_CMD(BATCHED_LOCK, pfn, num_pages,
				*target);

	if (vmballoon_check_status(b, status))
		return 0;

	pr_debug("%s - batch ppn %lx, hv returns %ld\n", __func__, pfn, status);
	STATS_INC(b->stats.lock_fail[is_2m_pages]);
	return 1;
}

/*
 * Notify the host that guest intends to release given page back into
 * the pool of available (to the guest) pages.
 */
static bool vmballoon_send_unlock_page(struct vmballoon *b, unsigned long pfn,
							unsigned int *target)
{
	unsigned long status, dummy = 0;
	u32 pfn32;

	pfn32 = (u32)pfn;
	if (pfn32 != pfn)
		return false;

	STATS_INC(b->stats.unlock[false]);

	status = VMWARE_BALLOON_CMD(UNLOCK, pfn, dummy, *target);
	if (vmballoon_check_status(b, status))
		return true;

	pr_debug("%s - ppn %lx, hv returns %ld\n", __func__, pfn, status);
	STATS_INC(b->stats.unlock_fail[false]);
	return false;
}

static bool vmballoon_send_batched_unlock(struct vmballoon *b,
		unsigned int num_pages, bool is_2m_pages, unsigned int *target)
{
	unsigned long status;
	unsigned long pfn = PHYS_PFN(virt_to_phys(b->batch_page));

	STATS_INC(b->stats.unlock[is_2m_pages]);

	if (is_2m_pages)
		status = VMWARE_BALLOON_CMD(BATCHED_2M_UNLOCK, pfn, num_pages,
				*target);
	else
		status = VMWARE_BALLOON_CMD(BATCHED_UNLOCK, pfn, num_pages,
				*target);

	if (vmballoon_check_status(b, status))
		return true;

	pr_debug("%s - batch ppn %lx, hv returns %ld\n", __func__, pfn, status);
	STATS_INC(b->stats.unlock_fail[is_2m_pages]);
	return false;
}

static struct page *vmballoon_alloc_page(gfp_t flags, bool is_2m_page)
{
	if (is_2m_page)
		return alloc_pages(flags, VMW_BALLOON_2M_SHIFT);

	return alloc_page(flags);
}

static void vmballoon_free_page(struct page *page, bool is_2m_page)
{
	if (is_2m_page)
		__free_pages(page, VMW_BALLOON_2M_SHIFT);
	else
		__free_page(page);
}

/*
 * Quickly release all pages allocated for the balloon. This function is
 * called when host decides to "reset" balloon for one reason or another.
 * Unlike normal "deflate" we do not (shall not) notify host of the pages
 * being released.
 */
static void vmballoon_pop(struct vmballoon *b)
{
	struct page *page, *next;
	unsigned is_2m_pages;

	for (is_2m_pages = 0; is_2m_pages < VMW_BALLOON_NUM_PAGE_SIZES;
			is_2m_pages++) {
		struct vmballoon_page_size *page_size =
				&b->page_sizes[is_2m_pages];
		u16 size_per_page = vmballoon_page_size(is_2m_pages);

		list_for_each_entry_safe(page, next, &page_size->pages, lru) {
			list_del(&page->lru);
			vmballoon_free_page(page, is_2m_pages);
			STATS_INC(b->stats.free[is_2m_pages]);
			b->size -= size_per_page;
			cond_resched();
		}
	}

	/* Clearing the batch_page unconditionally has no adverse effect */
	free_page((unsigned long)b->batch_page);
	b->batch_page = NULL;
}

/*
 * Notify the host of a ballooned page. If host rejects the page put it on the
 * refuse list, those refused page are then released at the end of the
 * inflation cycle.
 */
static int vmballoon_lock_page(struct vmballoon *b, unsigned int num_pages,
				bool is_2m_pages, unsigned int *target)
{
	int locked, hv_status;
	struct page *page = b->page;
	struct vmballoon_page_size *page_size = &b->page_sizes[false];

	/* is_2m_pages can never happen as 2m pages support implies batching */

	locked = vmballoon_send_lock_page(b, page_to_pfn(page), &hv_status,
								target);
	if (locked > 0) {
		STATS_INC(b->stats.refused_alloc[false]);

		if (hv_status == VMW_BALLOON_ERROR_RESET ||
				hv_status == VMW_BALLOON_ERROR_PPN_NOTNEEDED) {
			vmballoon_free_page(page, false);
			return -EIO;
		}

		/*
		 * Place page on the list of non-balloonable pages
		 * and retry allocation, unless we already accumulated
		 * too many of them, in which case take a breather.
		 */
		if (page_size->n_refused_pages < VMW_BALLOON_MAX_REFUSED) {
			page_size->n_refused_pages++;
			list_add(&page->lru, &page_size->refused_pages);
		} else {
			vmballoon_free_page(page, false);
		}
		return -EIO;
	}

	/* track allocated page */
	list_add(&page->lru, &page_size->pages);

	/* update balloon size */
	b->size++;

	return 0;
}

static int vmballoon_lock_batched_page(struct vmballoon *b,
		unsigned int num_pages, bool is_2m_pages, unsigned int *target)
{
	int locked, i;
	u16 size_per_page = vmballoon_page_size(is_2m_pages);

	locked = vmballoon_send_batched_lock(b, num_pages, is_2m_pages,
			target);
	if (locked > 0) {
		for (i = 0; i < num_pages; i++) {
			u64 pa = vmballoon_batch_get_pa(b->batch_page, i);
			struct page *p = pfn_to_page(pa >> PAGE_SHIFT);

			vmballoon_free_page(p, is_2m_pages);
		}

		return -EIO;
	}

	for (i = 0; i < num_pages; i++) {
		u64 pa = vmballoon_batch_get_pa(b->batch_page, i);
		struct page *p = pfn_to_page(pa >> PAGE_SHIFT);
		struct vmballoon_page_size *page_size =
				&b->page_sizes[is_2m_pages];

		locked = vmballoon_batch_get_status(b->batch_page, i);

		switch (locked) {
		case VMW_BALLOON_SUCCESS:
			list_add(&p->lru, &page_size->pages);
			b->size += size_per_page;
			break;
		case VMW_BALLOON_ERROR_PPN_PINNED:
		case VMW_BALLOON_ERROR_PPN_INVALID:
			if (page_size->n_refused_pages
					< VMW_BALLOON_MAX_REFUSED) {
				list_add(&p->lru, &page_size->refused_pages);
				page_size->n_refused_pages++;
				break;
			}
			/* Fallthrough */
		case VMW_BALLOON_ERROR_RESET:
		case VMW_BALLOON_ERROR_PPN_NOTNEEDED:
			vmballoon_free_page(p, is_2m_pages);
			break;
		default:
			/* This should never happen */
			WARN_ON_ONCE(true);
		}
	}

	return 0;
}

/*
 * Release the page allocated for the balloon. Note that we first notify
 * the host so it can make sure the page will be available for the guest
 * to use, if needed.
 */
static int vmballoon_unlock_page(struct vmballoon *b, unsigned int num_pages,
		bool is_2m_pages, unsigned int *target)
{
	struct page *page = b->page;
	struct vmballoon_page_size *page_size = &b->page_sizes[false];

	/* is_2m_pages can never happen as 2m pages support implies batching */

	if (!vmballoon_send_unlock_page(b, page_to_pfn(page), target)) {
		list_add(&page->lru, &page_size->pages);
		return -EIO;
	}

	/* deallocate page */
	vmballoon_free_page(page, false);
	STATS_INC(b->stats.free[false]);

	/* update balloon size */
	b->size--;

	return 0;
}

static int vmballoon_unlock_batched_page(struct vmballoon *b,
				unsigned int num_pages, bool is_2m_pages,
				unsigned int *target)
{
	int locked, i, ret = 0;
	bool hv_success;
	u16 size_per_page = vmballoon_page_size(is_2m_pages);

	hv_success = vmballoon_send_batched_unlock(b, num_pages, is_2m_pages,
			target);
	if (!hv_success)
		ret = -EIO;

	for (i = 0; i < num_pages; i++) {
		u64 pa = vmballoon_batch_get_pa(b->batch_page, i);
		struct page *p = pfn_to_page(pa >> PAGE_SHIFT);
		struct vmballoon_page_size *page_size =
				&b->page_sizes[is_2m_pages];

		locked = vmballoon_batch_get_status(b->batch_page, i);
		if (!hv_success || locked != VMW_BALLOON_SUCCESS) {
			/*
			 * That page wasn't successfully unlocked by the
			 * hypervisor, re-add it to the list of pages owned by
			 * the balloon driver.
			 */
			list_add(&p->lru, &page_size->pages);
		} else {
			/* deallocate page */
			vmballoon_free_page(p, is_2m_pages);
			STATS_INC(b->stats.free[is_2m_pages]);

			/* update balloon size */
			b->size -= size_per_page;
		}
	}

	return ret;
}

/*
 * Release pages that were allocated while attempting to inflate the
 * balloon but were refused by the host for one reason or another.
 */
static void vmballoon_release_refused_pages(struct vmballoon *b,
		bool is_2m_pages)
{
	struct page *page, *next;
	struct vmballoon_page_size *page_size =
			&b->page_sizes[is_2m_pages];

	list_for_each_entry_safe(page, next, &page_size->refused_pages, lru) {
		list_del(&page->lru);
		vmballoon_free_page(page, is_2m_pages);
		STATS_INC(b->stats.refused_free[is_2m_pages]);
	}

	page_size->n_refused_pages = 0;
}

static void vmballoon_add_page(struct vmballoon *b, int idx, struct page *p)
{
	b->page = p;
}

static void vmballoon_add_batched_page(struct vmballoon *b, int idx,
				struct page *p)
{
	vmballoon_batch_set_pa(b->batch_page, idx,
			(u64)page_to_pfn(p) << PAGE_SHIFT);
}

/*
 * Inflate the balloon towards its target size. Note that we try to limit
 * the rate of allocation to make sure we are not choking the rest of the
 * system.
 */
static void vmballoon_inflate(struct vmballoon *b)
{
	unsigned rate;
	unsigned int allocations = 0;
	unsigned int num_pages = 0;
	int error = 0;
	gfp_t flags = VMW_PAGE_ALLOC_NOSLEEP;
	bool is_2m_pages;

	pr_debug("%s - size: %d, target %d\n", __func__, b->size, b->target);

	/*
	 * First try NOSLEEP page allocations to inflate balloon.
	 *
	 * If we do not throttle nosleep allocations, we can drain all
	 * free pages in the guest quickly (if the balloon target is high).
	 * As a side-effect, draining free pages helps to inform (force)
	 * the guest to start swapping if balloon target is not met yet,
	 * which is a desired behavior. However, balloon driver can consume
	 * all available CPU cycles if too many pages are allocated in a
	 * second. Therefore, we throttle nosleep allocations even when
	 * the guest is not under memory pressure. OTOH, if we have already
	 * predicted that the guest is under memory pressure, then we
	 * slowdown page allocations considerably.
	 */

	/*
	 * Start with no sleep allocation rate which may be higher
	 * than sleeping allocation rate.
	 */
	if (b->slow_allocation_cycles) {
		rate = b->rate_alloc;
		is_2m_pages = false;
	} else {
		rate = UINT_MAX;
		is_2m_pages =
			b->supported_page_sizes == VMW_BALLOON_NUM_PAGE_SIZES;
	}

	pr_debug("%s - goal: %d, no-sleep rate: %u, sleep rate: %d\n",
		 __func__, b->target - b->size, rate, b->rate_alloc);

	while (!b->reset_required &&
		b->size + num_pages * vmballoon_page_size(is_2m_pages)
		< b->target) {
		struct page *page;

		if (flags == VMW_PAGE_ALLOC_NOSLEEP)
			STATS_INC(b->stats.alloc[is_2m_pages]);
		else
			STATS_INC(b->stats.sleep_alloc);

		page = vmballoon_alloc_page(flags, is_2m_pages);
		if (!page) {
			STATS_INC(b->stats.alloc_fail[is_2m_pages]);

			if (is_2m_pages) {
				b->ops->lock(b, num_pages, true, &b->target);

				/*
				 * ignore errors from locking as we now switch
				 * to 4k pages and we might get different
				 * errors.
				 */

				num_pages = 0;
				is_2m_pages = false;
				continue;
			}

			if (flags == VMW_PAGE_ALLOC_CANSLEEP) {
				/*
				 * CANSLEEP page allocation failed, so guest
				 * is under severe memory pressure. Quickly
				 * decrease allocation rate.
				 */
				b->rate_alloc = max(b->rate_alloc / 2,
						    VMW_BALLOON_RATE_ALLOC_MIN);
				STATS_INC(b->stats.sleep_alloc_fail);
				break;
			}

			/*
			 * NOSLEEP page allocation failed, so the guest is
			 * under memory pressure. Let us slow down page
			 * allocations for next few cycles so that the guest
			 * gets out of memory pressure. Also, if we already
			 * allocated b->rate_alloc pages, let's pause,
			 * otherwise switch to sleeping allocations.
			 */
			b->slow_allocation_cycles = VMW_BALLOON_SLOW_CYCLES;

			if (allocations >= b->rate_alloc)
				break;

			flags = VMW_PAGE_ALLOC_CANSLEEP;
			/* Lower rate for sleeping allocations. */
			rate = b->rate_alloc;
			continue;
		}

		b->ops->add_page(b, num_pages++, page);
		if (num_pages == b->batch_max_pages) {
			error = b->ops->lock(b, num_pages, is_2m_pages,
					&b->target);
			num_pages = 0;
			if (error)
				break;
		}

		cond_resched();

		if (allocations >= rate) {
			/* We allocated enough pages, let's take a break. */
			break;
		}
	}

	if (num_pages > 0)
		b->ops->lock(b, num_pages, is_2m_pages, &b->target);

	/*
	 * We reached our goal without failures so try increasing
	 * allocation rate.
	 */
	if (error == 0 && allocations >= b->rate_alloc) {
		unsigned int mult = allocations / b->rate_alloc;

		b->rate_alloc =
			min(b->rate_alloc + mult * VMW_BALLOON_RATE_ALLOC_INC,
			    VMW_BALLOON_RATE_ALLOC_MAX);
	}

	vmballoon_release_refused_pages(b, true);
	vmballoon_release_refused_pages(b, false);
}

/*
 * Decrease the size of the balloon allowing guest to use more memory.
 */
static void vmballoon_deflate(struct vmballoon *b)
{
	unsigned is_2m_pages;

	pr_debug("%s - size: %d, target %d\n", __func__, b->size, b->target);

	/* free pages to reach target */
	for (is_2m_pages = 0; is_2m_pages < b->supported_page_sizes;
			is_2m_pages++) {
		struct page *page, *next;
		unsigned int num_pages = 0;
		struct vmballoon_page_size *page_size =
				&b->page_sizes[is_2m_pages];

		list_for_each_entry_safe(page, next, &page_size->pages, lru) {
			if (b->reset_required ||
				(b->target > 0 &&
					b->size - num_pages
					* vmballoon_page_size(is_2m_pages)
				< b->target + vmballoon_page_size(true)))
				break;

			list_del(&page->lru);
			b->ops->add_page(b, num_pages++, page);

			if (num_pages == b->batch_max_pages) {
				int error;

				error = b->ops->unlock(b, num_pages,
						is_2m_pages, &b->target);
				num_pages = 0;
				if (error)
					return;
			}

			cond_resched();
		}

		if (num_pages > 0)
			b->ops->unlock(b, num_pages, is_2m_pages, &b->target);
	}
}

static const struct vmballoon_ops vmballoon_basic_ops = {
	.add_page = vmballoon_add_page,
	.lock = vmballoon_lock_page,
	.unlock = vmballoon_unlock_page
};

static const struct vmballoon_ops vmballoon_batched_ops = {
	.add_page = vmballoon_add_batched_page,
	.lock = vmballoon_lock_batched_page,
	.unlock = vmballoon_unlock_batched_page
};

static bool vmballoon_init_batching(struct vmballoon *b)
{
	struct page *page;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return false;

	b->batch_page = page_address(page);
	return true;
}

/*
 * Receive notification and resize balloon
 */
static void vmballoon_doorbell(void *client_data)
{
	struct vmballoon *b = client_data;

	STATS_INC(b->stats.doorbell);

	mod_delayed_work(system_freezable_wq, &b->dwork, 0);
}

/*
 * Clean up vmci doorbell
 */
static void vmballoon_vmci_cleanup(struct vmballoon *b)
{
	int error;

	VMWARE_BALLOON_CMD(VMCI_DOORBELL_SET, VMCI_INVALID_ID,
			VMCI_INVALID_ID, error);
	STATS_INC(b->stats.doorbell_unset);

	if (!vmci_handle_is_invalid(b->vmci_doorbell)) {
		vmci_doorbell_destroy(b->vmci_doorbell);
		b->vmci_doorbell = VMCI_INVALID_HANDLE;
	}
}

/*
 * Initialize vmci doorbell, to get notified as soon as balloon changes
 */
static int vmballoon_vmci_init(struct vmballoon *b)
{
	int error = 0;

	if ((b->capabilities & VMW_BALLOON_SIGNALLED_WAKEUP_CMD) != 0) {
		error = vmci_doorbell_create(&b->vmci_doorbell,
				VMCI_FLAG_DELAYED_CB,
				VMCI_PRIVILEGE_FLAG_RESTRICTED,
				vmballoon_doorbell, b);

		if (error == VMCI_SUCCESS) {
			VMWARE_BALLOON_CMD(VMCI_DOORBELL_SET,
					b->vmci_doorbell.context,
					b->vmci_doorbell.resource, error);
			STATS_INC(b->stats.doorbell_set);
		}
	}

	if (error != 0) {
		vmballoon_vmci_cleanup(b);

		return -EIO;
	}

	return 0;
}

/*
 * Perform standard reset sequence by popping the balloon (in case it
 * is not  empty) and then restarting protocol. This operation normally
 * happens when host responds with VMW_BALLOON_ERROR_RESET to a command.
 */
static void vmballoon_reset(struct vmballoon *b)
{
	int error;

	vmballoon_vmci_cleanup(b);

	/* free all pages, skipping monitor unlock */
	vmballoon_pop(b);

	if (!vmballoon_send_start(b, VMW_BALLOON_CAPABILITIES))
		return;

	if ((b->capabilities & VMW_BALLOON_BATCHED_CMDS) != 0) {
		b->ops = &vmballoon_batched_ops;
		b->batch_max_pages = VMW_BALLOON_BATCH_MAX_PAGES;
		if (!vmballoon_init_batching(b)) {
			/*
			 * We failed to initialize batching, inform the monitor
			 * about it by sending a null capability.
			 *
			 * The guest will retry in one second.
			 */
			vmballoon_send_start(b, 0);
			return;
		}
	} else if ((b->capabilities & VMW_BALLOON_BASIC_CMDS) != 0) {
		b->ops = &vmballoon_basic_ops;
		b->batch_max_pages = 1;
	}

	b->reset_required = false;

	error = vmballoon_vmci_init(b);
	if (error)
		pr_err("failed to initialize vmci doorbell\n");

	if (!vmballoon_send_guest_id(b))
		pr_err("failed to send guest ID to the host\n");
}

/*
 * Balloon work function: reset protocol, if needed, get the new size and
 * adjust balloon as needed. Repeat in 1 sec.
 */
static void vmballoon_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct vmballoon *b = container_of(dwork, struct vmballoon, dwork);
	unsigned int target;

	STATS_INC(b->stats.timer);

	if (b->reset_required)
		vmballoon_reset(b);

	if (b->slow_allocation_cycles > 0)
		b->slow_allocation_cycles--;

	if (!b->reset_required && vmballoon_send_get_target(b, &target)) {
		/* update target, adjust size */
		b->target = target;

		if (b->size < target)
			vmballoon_inflate(b);
		else if (target == 0 ||
				b->size > target + vmballoon_page_size(true))
			vmballoon_deflate(b);
	}

	/*
	 * We are using a freezable workqueue so that balloon operations are
	 * stopped while the system transitions to/from sleep/hibernation.
	 */
	queue_delayed_work(system_freezable_wq,
			   dwork, round_jiffies_relative(HZ));
}

/*
 * DEBUGFS Interface
 */
#ifdef CONFIG_DEBUG_FS

static int vmballoon_debug_show(struct seq_file *f, void *offset)
{
	struct vmballoon *b = f->private;
	struct vmballoon_stats *stats = &b->stats;

	/* format capabilities info */
	seq_printf(f,
		   "balloon capabilities:   %#4x\n"
		   "used capabilities:      %#4lx\n"
		   "is resetting:           %c\n",
		   VMW_BALLOON_CAPABILITIES, b->capabilities,
		   b->reset_required ? 'y' : 'n');

	/* format size info */
	seq_printf(f,
		   "target:             %8d pages\n"
		   "current:            %8d pages\n",
		   b->target, b->size);

	/* format rate info */
	seq_printf(f,
		   "rateSleepAlloc:     %8d pages/sec\n",
		   b->rate_alloc);

	seq_printf(f,
		   "\n"
		   "timer:              %8u\n"
		   "doorbell:           %8u\n"
		   "start:              %8u (%4u failed)\n"
		   "guestType:          %8u (%4u failed)\n"
		   "2m-lock:            %8u (%4u failed)\n"
		   "lock:               %8u (%4u failed)\n"
		   "2m-unlock:          %8u (%4u failed)\n"
		   "unlock:             %8u (%4u failed)\n"
		   "target:             %8u (%4u failed)\n"
		   "prim2mAlloc:        %8u (%4u failed)\n"
		   "primNoSleepAlloc:   %8u (%4u failed)\n"
		   "primCanSleepAlloc:  %8u (%4u failed)\n"
		   "prim2mFree:         %8u\n"
		   "primFree:           %8u\n"
		   "err2mAlloc:         %8u\n"
		   "errAlloc:           %8u\n"
		   "err2mFree:          %8u\n"
		   "errFree:            %8u\n"
		   "doorbellSet:        %8u\n"
		   "doorbellUnset:      %8u\n",
		   stats->timer,
		   stats->doorbell,
		   stats->start, stats->start_fail,
		   stats->guest_type, stats->guest_type_fail,
		   stats->lock[true],  stats->lock_fail[true],
		   stats->lock[false],  stats->lock_fail[false],
		   stats->unlock[true], stats->unlock_fail[true],
		   stats->unlock[false], stats->unlock_fail[false],
		   stats->target, stats->target_fail,
		   stats->alloc[true], stats->alloc_fail[true],
		   stats->alloc[false], stats->alloc_fail[false],
		   stats->sleep_alloc, stats->sleep_alloc_fail,
		   stats->free[true],
		   stats->free[false],
		   stats->refused_alloc[true], stats->refused_alloc[false],
		   stats->refused_free[true], stats->refused_free[false],
		   stats->doorbell_set, stats->doorbell_unset);

	return 0;
}

static int vmballoon_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, vmballoon_debug_show, inode->i_private);
}

static const struct file_operations vmballoon_debug_fops = {
	.owner		= THIS_MODULE,
	.open		= vmballoon_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init vmballoon_debugfs_init(struct vmballoon *b)
{
	int error;

	b->dbg_entry = debugfs_create_file("vmmemctl", S_IRUGO, NULL, b,
					   &vmballoon_debug_fops);
	if (IS_ERR(b->dbg_entry)) {
		error = PTR_ERR(b->dbg_entry);
		pr_err("failed to create debugfs entry, error: %d\n", error);
		return error;
	}

	return 0;
}

static void __exit vmballoon_debugfs_exit(struct vmballoon *b)
{
	debugfs_remove(b->dbg_entry);
}

#else

static inline int vmballoon_debugfs_init(struct vmballoon *b)
{
	return 0;
}

static inline void vmballoon_debugfs_exit(struct vmballoon *b)
{
}

#endif	/* CONFIG_DEBUG_FS */

static int __init vmballoon_init(void)
{
	int error;
	unsigned is_2m_pages;
	/*
	 * Check if we are running on VMware's hypervisor and bail out
	 * if we are not.
	 */
	if (x86_hyper_type != X86_HYPER_VMWARE)
		return -ENODEV;

	for (is_2m_pages = 0; is_2m_pages < VMW_BALLOON_NUM_PAGE_SIZES;
			is_2m_pages++) {
		INIT_LIST_HEAD(&balloon.page_sizes[is_2m_pages].pages);
		INIT_LIST_HEAD(&balloon.page_sizes[is_2m_pages].refused_pages);
	}

	/* initialize rates */
	balloon.rate_alloc = VMW_BALLOON_RATE_ALLOC_MAX;

	INIT_DELAYED_WORK(&balloon.dwork, vmballoon_work);

	error = vmballoon_debugfs_init(&balloon);
	if (error)
		return error;

	balloon.vmci_doorbell = VMCI_INVALID_HANDLE;
	balloon.batch_page = NULL;
	balloon.page = NULL;
	balloon.reset_required = true;

	queue_delayed_work(system_freezable_wq, &balloon.dwork, 0);

	return 0;
}
module_init(vmballoon_init);

static void __exit vmballoon_exit(void)
{
	vmballoon_vmci_cleanup(&balloon);
	cancel_delayed_work_sync(&balloon.dwork);

	vmballoon_debugfs_exit(&balloon);

	/*
	 * Deallocate all reserved memory, and reset connection with monitor.
	 * Reset connection before deallocating memory to avoid potential for
	 * additional spurious resets from guest touching deallocated pages.
	 */
	vmballoon_send_start(&balloon, 0);
	vmballoon_pop(&balloon);
}
module_exit(vmballoon_exit);
