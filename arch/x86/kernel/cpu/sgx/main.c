// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/node.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#include <asm/msr.h>
#include <asm/sgx.h>
#include "driver.h"
#include "encl.h"
#include "encls.h"

struct sgx_epc_section sgx_epc_sections[SGX_MAX_EPC_SECTIONS];
static int sgx_nr_epc_sections;
static struct task_struct *ksgxd_tsk;
static DECLARE_WAIT_QUEUE_HEAD(ksgxd_waitq);
static DEFINE_XARRAY(sgx_epc_address_space);

/*
 * These variables are part of the state of the reclaimer, and must be accessed
 * with sgx_reclaimer_lock acquired.
 */
static LIST_HEAD(sgx_active_page_list);
static DEFINE_SPINLOCK(sgx_reclaimer_lock);

static atomic_long_t sgx_nr_free_pages = ATOMIC_LONG_INIT(0);

/* Nodes with one or more EPC sections. */
static nodemask_t sgx_numa_mask;

/*
 * Array with one list_head for each possible NUMA node.  Each
 * list contains all the sgx_epc_section's which are on that
 * node.
 */
static struct sgx_numa_node *sgx_numa_nodes;

static LIST_HEAD(sgx_dirty_page_list);

/*
 * Reset post-kexec EPC pages to the uninitialized state. The pages are removed
 * from the input list, and made available for the page allocator. SECS pages
 * prepending their children in the input list are left intact.
 *
 * Return 0 when sanitization was successful or kthread was stopped, and the
 * number of unsanitized pages otherwise.
 */
static unsigned long __sgx_sanitize_pages(struct list_head *dirty_page_list)
{
	unsigned long left_dirty = 0;
	struct sgx_epc_page *page;
	LIST_HEAD(dirty);
	int ret;

	/* dirty_page_list is thread-local, no need for a lock: */
	while (!list_empty(dirty_page_list)) {
		if (kthread_should_stop())
			return 0;

		page = list_first_entry(dirty_page_list, struct sgx_epc_page, list);

		/*
		 * Checking page->poison without holding the node->lock
		 * is racy, but losing the race (i.e. poison is set just
		 * after the check) just means __eremove() will be uselessly
		 * called for a page that sgx_free_epc_page() will put onto
		 * the node->sgx_poison_page_list later.
		 */
		if (page->poison) {
			struct sgx_epc_section *section = &sgx_epc_sections[page->section];
			struct sgx_numa_node *node = section->node;

			spin_lock(&node->lock);
			list_move(&page->list, &node->sgx_poison_page_list);
			spin_unlock(&node->lock);

			continue;
		}

		ret = __eremove(sgx_get_epc_virt_addr(page));
		if (!ret) {
			/*
			 * page is now sanitized.  Make it available via the SGX
			 * page allocator:
			 */
			list_del(&page->list);
			sgx_free_epc_page(page);
		} else {
			/* The page is not yet clean - move to the dirty list. */
			list_move_tail(&page->list, &dirty);
			left_dirty++;
		}

		cond_resched();
	}

	list_splice(&dirty, dirty_page_list);
	return left_dirty;
}

static bool sgx_reclaimer_age(struct sgx_epc_page *epc_page)
{
	struct sgx_encl_page *page = epc_page->owner;
	struct sgx_encl *encl = page->encl;
	struct sgx_encl_mm *encl_mm;
	bool ret = true;
	int idx;

	idx = srcu_read_lock(&encl->srcu);

	list_for_each_entry_rcu(encl_mm, &encl->mm_list, list) {
		if (!mmget_not_zero(encl_mm->mm))
			continue;

		mmap_read_lock(encl_mm->mm);
		ret = !sgx_encl_test_and_clear_young(encl_mm->mm, page);
		mmap_read_unlock(encl_mm->mm);

		mmput_async(encl_mm->mm);

		if (!ret)
			break;
	}

	srcu_read_unlock(&encl->srcu, idx);

	if (!ret)
		return false;

	return true;
}

static void sgx_reclaimer_block(struct sgx_epc_page *epc_page)
{
	struct sgx_encl_page *page = epc_page->owner;
	unsigned long addr = page->desc & PAGE_MASK;
	struct sgx_encl *encl = page->encl;
	int ret;

	sgx_zap_enclave_ptes(encl, addr);

	mutex_lock(&encl->lock);

	ret = __eblock(sgx_get_epc_virt_addr(epc_page));
	if (encls_failed(ret))
		ENCLS_WARN(ret, "EBLOCK");

	mutex_unlock(&encl->lock);
}

static int __sgx_encl_ewb(struct sgx_epc_page *epc_page, void *va_slot,
			  struct sgx_backing *backing)
{
	struct sgx_pageinfo pginfo;
	int ret;

	pginfo.addr = 0;
	pginfo.secs = 0;

	pginfo.contents = (unsigned long)kmap_local_page(backing->contents);
	pginfo.metadata = (unsigned long)kmap_local_page(backing->pcmd) +
			  backing->pcmd_offset;

	ret = __ewb(&pginfo, sgx_get_epc_virt_addr(epc_page), va_slot);
	set_page_dirty(backing->pcmd);
	set_page_dirty(backing->contents);

	kunmap_local((void *)(unsigned long)(pginfo.metadata -
					      backing->pcmd_offset));
	kunmap_local((void *)(unsigned long)pginfo.contents);

	return ret;
}

void sgx_ipi_cb(void *info)
{
}

/*
 * Swap page to the regular memory transformed to the blocked state by using
 * EBLOCK, which means that it can no longer be referenced (no new TLB entries).
 *
 * The first trial just tries to write the page assuming that some other thread
 * has reset the count for threads inside the enclave by using ETRACK, and
 * previous thread count has been zeroed out. The second trial calls ETRACK
 * before EWB. If that fails we kick all the HW threads out, and then do EWB,
 * which should be guaranteed the succeed.
 */
static void sgx_encl_ewb(struct sgx_epc_page *epc_page,
			 struct sgx_backing *backing)
{
	struct sgx_encl_page *encl_page = epc_page->owner;
	struct sgx_encl *encl = encl_page->encl;
	struct sgx_va_page *va_page;
	unsigned int va_offset;
	void *va_slot;
	int ret;

	encl_page->desc &= ~SGX_ENCL_PAGE_BEING_RECLAIMED;

	va_page = list_first_entry(&encl->va_pages, struct sgx_va_page,
				   list);
	va_offset = sgx_alloc_va_slot(va_page);
	va_slot = sgx_get_epc_virt_addr(va_page->epc_page) + va_offset;
	if (sgx_va_page_full(va_page))
		list_move_tail(&va_page->list, &encl->va_pages);

	ret = __sgx_encl_ewb(epc_page, va_slot, backing);
	if (ret == SGX_NOT_TRACKED) {
		ret = __etrack(sgx_get_epc_virt_addr(encl->secs.epc_page));
		if (ret) {
			if (encls_failed(ret))
				ENCLS_WARN(ret, "ETRACK");
		}

		ret = __sgx_encl_ewb(epc_page, va_slot, backing);
		if (ret == SGX_NOT_TRACKED) {
			/*
			 * Slow path, send IPIs to kick cpus out of the
			 * enclave.  Note, it's imperative that the cpu
			 * mask is generated *after* ETRACK, else we'll
			 * miss cpus that entered the enclave between
			 * generating the mask and incrementing epoch.
			 */
			on_each_cpu_mask(sgx_encl_cpumask(encl),
					 sgx_ipi_cb, NULL, 1);
			ret = __sgx_encl_ewb(epc_page, va_slot, backing);
		}
	}

	if (ret) {
		if (encls_failed(ret))
			ENCLS_WARN(ret, "EWB");

		sgx_free_va_slot(va_page, va_offset);
	} else {
		encl_page->desc |= va_offset;
		encl_page->va_page = va_page;
	}
}

static void sgx_reclaimer_write(struct sgx_epc_page *epc_page,
				struct sgx_backing *backing)
{
	struct sgx_encl_page *encl_page = epc_page->owner;
	struct sgx_encl *encl = encl_page->encl;
	struct sgx_backing secs_backing;
	int ret;

	mutex_lock(&encl->lock);

	sgx_encl_ewb(epc_page, backing);
	encl_page->epc_page = NULL;
	encl->secs_child_cnt--;
	sgx_encl_put_backing(backing);

	if (!encl->secs_child_cnt && test_bit(SGX_ENCL_INITIALIZED, &encl->flags)) {
		ret = sgx_encl_alloc_backing(encl, PFN_DOWN(encl->size),
					   &secs_backing);
		if (ret)
			goto out;

		sgx_encl_ewb(encl->secs.epc_page, &secs_backing);

		sgx_encl_free_epc_page(encl->secs.epc_page);
		encl->secs.epc_page = NULL;

		sgx_encl_put_backing(&secs_backing);
	}

out:
	mutex_unlock(&encl->lock);
}

/*
 * Take a fixed number of pages from the head of the active page pool and
 * reclaim them to the enclave's private shmem files. Skip the pages, which have
 * been accessed since the last scan. Move those pages to the tail of active
 * page pool so that the pages get scanned in LRU like fashion.
 *
 * Batch process a chunk of pages (at the moment 16) in order to degrade amount
 * of IPI's and ETRACK's potentially required. sgx_encl_ewb() does degrade a bit
 * among the HW threads with three stage EWB pipeline (EWB, ETRACK + EWB and IPI
 * + EWB) but not sufficiently. Reclaiming one page at a time would also be
 * problematic as it would increase the lock contention too much, which would
 * halt forward progress.
 */
static void sgx_reclaim_pages(void)
{
	struct sgx_epc_page *chunk[SGX_NR_TO_SCAN];
	struct sgx_backing backing[SGX_NR_TO_SCAN];
	struct sgx_encl_page *encl_page;
	struct sgx_epc_page *epc_page;
	pgoff_t page_index;
	int cnt = 0;
	int ret;
	int i;

	spin_lock(&sgx_reclaimer_lock);
	for (i = 0; i < SGX_NR_TO_SCAN; i++) {
		if (list_empty(&sgx_active_page_list))
			break;

		epc_page = list_first_entry(&sgx_active_page_list,
					    struct sgx_epc_page, list);
		list_del_init(&epc_page->list);
		encl_page = epc_page->owner;

		if (kref_get_unless_zero(&encl_page->encl->refcount) != 0)
			chunk[cnt++] = epc_page;
		else
			/* The owner is freeing the page. No need to add the
			 * page back to the list of reclaimable pages.
			 */
			epc_page->flags &= ~SGX_EPC_PAGE_RECLAIMER_TRACKED;
	}
	spin_unlock(&sgx_reclaimer_lock);

	for (i = 0; i < cnt; i++) {
		epc_page = chunk[i];
		encl_page = epc_page->owner;

		if (!sgx_reclaimer_age(epc_page))
			goto skip;

		page_index = PFN_DOWN(encl_page->desc - encl_page->encl->base);

		mutex_lock(&encl_page->encl->lock);
		ret = sgx_encl_alloc_backing(encl_page->encl, page_index, &backing[i]);
		if (ret) {
			mutex_unlock(&encl_page->encl->lock);
			goto skip;
		}

		encl_page->desc |= SGX_ENCL_PAGE_BEING_RECLAIMED;
		mutex_unlock(&encl_page->encl->lock);
		continue;

skip:
		spin_lock(&sgx_reclaimer_lock);
		list_add_tail(&epc_page->list, &sgx_active_page_list);
		spin_unlock(&sgx_reclaimer_lock);

		kref_put(&encl_page->encl->refcount, sgx_encl_release);

		chunk[i] = NULL;
	}

	for (i = 0; i < cnt; i++) {
		epc_page = chunk[i];
		if (epc_page)
			sgx_reclaimer_block(epc_page);
	}

	for (i = 0; i < cnt; i++) {
		epc_page = chunk[i];
		if (!epc_page)
			continue;

		encl_page = epc_page->owner;
		sgx_reclaimer_write(epc_page, &backing[i]);

		kref_put(&encl_page->encl->refcount, sgx_encl_release);
		epc_page->flags &= ~SGX_EPC_PAGE_RECLAIMER_TRACKED;

		sgx_free_epc_page(epc_page);
	}
}

static bool sgx_should_reclaim(unsigned long watermark)
{
	return atomic_long_read(&sgx_nr_free_pages) < watermark &&
	       !list_empty(&sgx_active_page_list);
}

/*
 * sgx_reclaim_direct() should be called (without enclave's mutex held)
 * in locations where SGX memory resources might be low and might be
 * needed in order to make forward progress.
 */
void sgx_reclaim_direct(void)
{
	if (sgx_should_reclaim(SGX_NR_LOW_PAGES))
		sgx_reclaim_pages();
}

static int ksgxd(void *p)
{
	set_freezable();

	/*
	 * Sanitize pages in order to recover from kexec(). The 2nd pass is
	 * required for SECS pages, whose child pages blocked EREMOVE.
	 */
	__sgx_sanitize_pages(&sgx_dirty_page_list);
	WARN_ON(__sgx_sanitize_pages(&sgx_dirty_page_list));

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;

		wait_event_freezable(ksgxd_waitq,
				     kthread_should_stop() ||
				     sgx_should_reclaim(SGX_NR_HIGH_PAGES));

		if (sgx_should_reclaim(SGX_NR_HIGH_PAGES))
			sgx_reclaim_pages();

		cond_resched();
	}

	return 0;
}

static bool __init sgx_page_reclaimer_init(void)
{
	struct task_struct *tsk;

	tsk = kthread_run(ksgxd, NULL, "ksgxd");
	if (IS_ERR(tsk))
		return false;

	ksgxd_tsk = tsk;

	return true;
}

bool current_is_ksgxd(void)
{
	return current == ksgxd_tsk;
}

static struct sgx_epc_page *__sgx_alloc_epc_page_from_node(int nid)
{
	struct sgx_numa_node *node = &sgx_numa_nodes[nid];
	struct sgx_epc_page *page = NULL;

	spin_lock(&node->lock);

	if (list_empty(&node->free_page_list)) {
		spin_unlock(&node->lock);
		return NULL;
	}

	page = list_first_entry(&node->free_page_list, struct sgx_epc_page, list);
	list_del_init(&page->list);
	page->flags = 0;

	spin_unlock(&node->lock);
	atomic_long_dec(&sgx_nr_free_pages);

	return page;
}

/**
 * __sgx_alloc_epc_page() - Allocate an EPC page
 *
 * Iterate through NUMA nodes and reserve ia free EPC page to the caller. Start
 * from the NUMA node, where the caller is executing.
 *
 * Return:
 * - an EPC page:	A borrowed EPC pages were available.
 * - NULL:		Out of EPC pages.
 */
struct sgx_epc_page *__sgx_alloc_epc_page(void)
{
	struct sgx_epc_page *page;
	int nid_of_current = numa_node_id();
	int nid_start, nid;

	/*
	 * Try local node first. If it doesn't have an EPC section,
	 * fall back to the non-local NUMA nodes.
	 */
	if (node_isset(nid_of_current, sgx_numa_mask))
		nid_start = nid_of_current;
	else
		nid_start = next_node_in(nid_of_current, sgx_numa_mask);

	nid = nid_start;
	do {
		page = __sgx_alloc_epc_page_from_node(nid);
		if (page)
			return page;

		nid = next_node_in(nid, sgx_numa_mask);
	} while (nid != nid_start);

	return ERR_PTR(-ENOMEM);
}

/**
 * sgx_mark_page_reclaimable() - Mark a page as reclaimable
 * @page:	EPC page
 *
 * Mark a page as reclaimable and add it to the active page list. Pages
 * are automatically removed from the active list when freed.
 */
void sgx_mark_page_reclaimable(struct sgx_epc_page *page)
{
	spin_lock(&sgx_reclaimer_lock);
	page->flags |= SGX_EPC_PAGE_RECLAIMER_TRACKED;
	list_add_tail(&page->list, &sgx_active_page_list);
	spin_unlock(&sgx_reclaimer_lock);
}

/**
 * sgx_unmark_page_reclaimable() - Remove a page from the reclaim list
 * @page:	EPC page
 *
 * Clear the reclaimable flag and remove the page from the active page list.
 *
 * Return:
 *   0 on success,
 *   -EBUSY if the page is in the process of being reclaimed
 */
int sgx_unmark_page_reclaimable(struct sgx_epc_page *page)
{
	spin_lock(&sgx_reclaimer_lock);
	if (page->flags & SGX_EPC_PAGE_RECLAIMER_TRACKED) {
		/* The page is being reclaimed. */
		if (list_empty(&page->list)) {
			spin_unlock(&sgx_reclaimer_lock);
			return -EBUSY;
		}

		list_del(&page->list);
		page->flags &= ~SGX_EPC_PAGE_RECLAIMER_TRACKED;
	}
	spin_unlock(&sgx_reclaimer_lock);

	return 0;
}

/**
 * sgx_alloc_epc_page() - Allocate an EPC page
 * @owner:	the owner of the EPC page
 * @reclaim:	reclaim pages if necessary
 *
 * Iterate through EPC sections and borrow a free EPC page to the caller. When a
 * page is no longer needed it must be released with sgx_free_epc_page(). If
 * @reclaim is set to true, directly reclaim pages when we are out of pages. No
 * mm's can be locked when @reclaim is set to true.
 *
 * Finally, wake up ksgxd when the number of pages goes below the watermark
 * before returning back to the caller.
 *
 * Return:
 *   an EPC page,
 *   -errno on error
 */
struct sgx_epc_page *sgx_alloc_epc_page(void *owner, bool reclaim)
{
	struct sgx_epc_page *page;

	for ( ; ; ) {
		page = __sgx_alloc_epc_page();
		if (!IS_ERR(page)) {
			page->owner = owner;
			break;
		}

		if (list_empty(&sgx_active_page_list))
			return ERR_PTR(-ENOMEM);

		if (!reclaim) {
			page = ERR_PTR(-EBUSY);
			break;
		}

		if (signal_pending(current)) {
			page = ERR_PTR(-ERESTARTSYS);
			break;
		}

		sgx_reclaim_pages();
		cond_resched();
	}

	if (sgx_should_reclaim(SGX_NR_LOW_PAGES))
		wake_up(&ksgxd_waitq);

	return page;
}

/**
 * sgx_free_epc_page() - Free an EPC page
 * @page:	an EPC page
 *
 * Put the EPC page back to the list of free pages. It's the caller's
 * responsibility to make sure that the page is in uninitialized state. In other
 * words, do EREMOVE, EWB or whatever operation is necessary before calling
 * this function.
 */
void sgx_free_epc_page(struct sgx_epc_page *page)
{
	struct sgx_epc_section *section = &sgx_epc_sections[page->section];
	struct sgx_numa_node *node = section->node;

	spin_lock(&node->lock);

	page->owner = NULL;
	if (page->poison)
		list_add(&page->list, &node->sgx_poison_page_list);
	else
		list_add_tail(&page->list, &node->free_page_list);
	page->flags = SGX_EPC_PAGE_IS_FREE;

	spin_unlock(&node->lock);
	atomic_long_inc(&sgx_nr_free_pages);
}

static bool __init sgx_setup_epc_section(u64 phys_addr, u64 size,
					 unsigned long index,
					 struct sgx_epc_section *section)
{
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long i;

	section->virt_addr = memremap(phys_addr, size, MEMREMAP_WB);
	if (!section->virt_addr)
		return false;

	section->pages = vmalloc_array(nr_pages, sizeof(struct sgx_epc_page));
	if (!section->pages) {
		memunmap(section->virt_addr);
		return false;
	}

	section->phys_addr = phys_addr;
	xa_store_range(&sgx_epc_address_space, section->phys_addr,
		       phys_addr + size - 1, section, GFP_KERNEL);

	for (i = 0; i < nr_pages; i++) {
		section->pages[i].section = index;
		section->pages[i].flags = 0;
		section->pages[i].owner = NULL;
		section->pages[i].poison = 0;
		list_add_tail(&section->pages[i].list, &sgx_dirty_page_list);
	}

	return true;
}

bool arch_is_platform_page(u64 paddr)
{
	return !!xa_load(&sgx_epc_address_space, paddr);
}
EXPORT_SYMBOL_GPL(arch_is_platform_page);

static struct sgx_epc_page *sgx_paddr_to_page(u64 paddr)
{
	struct sgx_epc_section *section;

	section = xa_load(&sgx_epc_address_space, paddr);
	if (!section)
		return NULL;

	return &section->pages[PFN_DOWN(paddr - section->phys_addr)];
}

/*
 * Called in process context to handle a hardware reported
 * error in an SGX EPC page.
 * If the MF_ACTION_REQUIRED bit is set in flags, then the
 * context is the task that consumed the poison data. Otherwise
 * this is called from a kernel thread unrelated to the page.
 */
int arch_memory_failure(unsigned long pfn, int flags)
{
	struct sgx_epc_page *page = sgx_paddr_to_page(pfn << PAGE_SHIFT);
	struct sgx_epc_section *section;
	struct sgx_numa_node *node;

	/*
	 * mm/memory-failure.c calls this routine for all errors
	 * where there isn't a "struct page" for the address. But that
	 * includes other address ranges besides SGX.
	 */
	if (!page)
		return -ENXIO;

	/*
	 * If poison was consumed synchronously. Send a SIGBUS to
	 * the task. Hardware has already exited the SGX enclave and
	 * will not allow re-entry to an enclave that has a memory
	 * error. The signal may help the task understand why the
	 * enclave is broken.
	 */
	if (flags & MF_ACTION_REQUIRED)
		force_sig(SIGBUS);

	section = &sgx_epc_sections[page->section];
	node = section->node;

	spin_lock(&node->lock);

	/* Already poisoned? Nothing more to do */
	if (page->poison)
		goto out;

	page->poison = 1;

	/*
	 * If the page is on a free list, move it to the per-node
	 * poison page list.
	 */
	if (page->flags & SGX_EPC_PAGE_IS_FREE) {
		list_move(&page->list, &node->sgx_poison_page_list);
		goto out;
	}

	sgx_unmark_page_reclaimable(page);

	/*
	 * TBD: Add additional plumbing to enable pre-emptive
	 * action for asynchronous poison notification. Until
	 * then just hope that the poison:
	 * a) is not accessed - sgx_free_epc_page() will deal with it
	 *    when the user gives it back
	 * b) results in a recoverable machine check rather than
	 *    a fatal one
	 */
out:
	spin_unlock(&node->lock);
	return 0;
}

/*
 * A section metric is concatenated in a way that @low bits 12-31 define the
 * bits 12-31 of the metric and @high bits 0-19 define the bits 32-51 of the
 * metric.
 */
static inline u64 __init sgx_calc_section_metric(u64 low, u64 high)
{
	return (low & GENMASK_ULL(31, 12)) +
	       ((high & GENMASK_ULL(19, 0)) << 32);
}

#ifdef CONFIG_NUMA
static ssize_t sgx_total_bytes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lu\n", sgx_numa_nodes[dev->id].size);
}
static DEVICE_ATTR_RO(sgx_total_bytes);

static umode_t arch_node_attr_is_visible(struct kobject *kobj,
		struct attribute *attr, int idx)
{
	/* Make all x86/ attributes invisible when SGX is not initialized: */
	if (nodes_empty(sgx_numa_mask))
		return 0;

	return attr->mode;
}

static struct attribute *arch_node_dev_attrs[] = {
	&dev_attr_sgx_total_bytes.attr,
	NULL,
};

const struct attribute_group arch_node_dev_group = {
	.name = "x86",
	.attrs = arch_node_dev_attrs,
	.is_visible = arch_node_attr_is_visible,
};

static void __init arch_update_sysfs_visibility(int nid)
{
	struct node *node = node_devices[nid];
	int ret;

	ret = sysfs_update_group(&node->dev.kobj, &arch_node_dev_group);

	if (ret)
		pr_err("sysfs update failed (%d), files may be invisible", ret);
}
#else /* !CONFIG_NUMA */
static void __init arch_update_sysfs_visibility(int nid) {}
#endif

static bool __init sgx_page_cache_init(void)
{
	u32 eax, ebx, ecx, edx, type;
	u64 pa, size;
	int nid;
	int i;

	sgx_numa_nodes = kmalloc_array(num_possible_nodes(), sizeof(*sgx_numa_nodes), GFP_KERNEL);
	if (!sgx_numa_nodes)
		return false;

	for (i = 0; i < ARRAY_SIZE(sgx_epc_sections); i++) {
		cpuid_count(SGX_CPUID, i + SGX_CPUID_EPC, &eax, &ebx, &ecx, &edx);

		type = eax & SGX_CPUID_EPC_MASK;
		if (type == SGX_CPUID_EPC_INVALID)
			break;

		if (type != SGX_CPUID_EPC_SECTION) {
			pr_err_once("Unknown EPC section type: %u\n", type);
			break;
		}

		pa   = sgx_calc_section_metric(eax, ebx);
		size = sgx_calc_section_metric(ecx, edx);

		pr_info("EPC section 0x%llx-0x%llx\n", pa, pa + size - 1);

		if (!sgx_setup_epc_section(pa, size, i, &sgx_epc_sections[i])) {
			pr_err("No free memory for an EPC section\n");
			break;
		}

		nid = numa_map_to_online_node(phys_to_target_node(pa));
		if (nid == NUMA_NO_NODE) {
			/* The physical address is already printed above. */
			pr_warn(FW_BUG "Unable to map EPC section to online node. Fallback to the NUMA node 0.\n");
			nid = 0;
		}

		if (!node_isset(nid, sgx_numa_mask)) {
			spin_lock_init(&sgx_numa_nodes[nid].lock);
			INIT_LIST_HEAD(&sgx_numa_nodes[nid].free_page_list);
			INIT_LIST_HEAD(&sgx_numa_nodes[nid].sgx_poison_page_list);
			node_set(nid, sgx_numa_mask);
			sgx_numa_nodes[nid].size = 0;

			/* Make SGX-specific node sysfs files visible: */
			arch_update_sysfs_visibility(nid);
		}

		sgx_epc_sections[i].node =  &sgx_numa_nodes[nid];
		sgx_numa_nodes[nid].size += size;

		sgx_nr_epc_sections++;
	}

	if (!sgx_nr_epc_sections) {
		pr_err("There are zero EPC sections.\n");
		return false;
	}

	for_each_online_node(nid) {
		if (!node_isset(nid, sgx_numa_mask) &&
		    node_state(nid, N_MEMORY) && node_state(nid, N_CPU))
			pr_info("node%d has both CPUs and memory but doesn't have an EPC section\n",
				nid);
	}

	return true;
}

/*
 * Update the SGX_LEPUBKEYHASH MSRs to the values specified by caller.
 * Bare-metal driver requires to update them to hash of enclave's signer
 * before EINIT. KVM needs to update them to guest's virtual MSR values
 * before doing EINIT from guest.
 */
void sgx_update_lepubkeyhash(u64 *lepubkeyhash)
{
	int i;

	WARN_ON_ONCE(preemptible());

	for (i = 0; i < 4; i++)
		wrmsrq(MSR_IA32_SGXLEPUBKEYHASH0 + i, lepubkeyhash[i]);
}

const struct file_operations sgx_provision_fops = {
	.owner			= THIS_MODULE,
};

static struct miscdevice sgx_dev_provision = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sgx_provision",
	.nodename = "sgx_provision",
	.fops = &sgx_provision_fops,
};

/**
 * sgx_set_attribute() - Update allowed attributes given file descriptor
 * @allowed_attributes:		Pointer to allowed enclave attributes
 * @attribute_fd:		File descriptor for specific attribute
 *
 * Append enclave attribute indicated by file descriptor to allowed
 * attributes. Currently only SGX_ATTR_PROVISIONKEY indicated by
 * /dev/sgx_provision is supported.
 *
 * Return:
 * -0:		SGX_ATTR_PROVISIONKEY is appended to allowed_attributes
 * -EINVAL:	Invalid, or not supported file descriptor
 */
int sgx_set_attribute(unsigned long *allowed_attributes,
		      unsigned int attribute_fd)
{
	CLASS(fd, f)(attribute_fd);

	if (fd_empty(f))
		return -EINVAL;

	if (fd_file(f)->f_op != &sgx_provision_fops)
		return -EINVAL;

	*allowed_attributes |= SGX_ATTR_PROVISIONKEY;
	return 0;
}
EXPORT_SYMBOL_GPL(sgx_set_attribute);

static int __init sgx_init(void)
{
	int ret;
	int i;

	if (!cpu_feature_enabled(X86_FEATURE_SGX))
		return -ENODEV;

	if (!sgx_page_cache_init())
		return -ENOMEM;

	if (!sgx_page_reclaimer_init()) {
		ret = -ENOMEM;
		goto err_page_cache;
	}

	ret = misc_register(&sgx_dev_provision);
	if (ret)
		goto err_kthread;

	/*
	 * Always try to initialize the native *and* KVM drivers.
	 * The KVM driver is less picky than the native one and
	 * can function if the native one is not supported on the
	 * current system or fails to initialize.
	 *
	 * Error out only if both fail to initialize.
	 */
	ret = sgx_drv_init();

	if (sgx_vepc_init() && ret)
		goto err_provision;

	return 0;

err_provision:
	misc_deregister(&sgx_dev_provision);

err_kthread:
	kthread_stop(ksgxd_tsk);

err_page_cache:
	for (i = 0; i < sgx_nr_epc_sections; i++) {
		vfree(sgx_epc_sections[i].pages);
		memunmap(sgx_epc_sections[i].virt_addr);
	}

	return ret;
}

device_initcall(sgx_init);
