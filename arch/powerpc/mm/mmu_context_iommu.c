/*
 *  IOMMU helpers in MMU context.
 *
 *  Copyright (C) 2015 IBM Corp. <aik@ozlabs.ru>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/swap.h>
#include <linux/sizes.h>
#include <asm/mmu_context.h>
#include <asm/pte-walk.h>

static DEFINE_MUTEX(mem_list_mutex);

#define MM_IOMMU_TABLE_GROUP_PAGE_DIRTY	0x1
#define MM_IOMMU_TABLE_GROUP_PAGE_MASK	~(SZ_4K - 1)

struct mm_iommu_table_group_mem_t {
	struct list_head next;
	struct rcu_head rcu;
	unsigned long used;
	atomic64_t mapped;
	unsigned int pageshift;
	u64 ua;			/* userspace address */
	u64 entries;		/* number of entries in hpas[] */
	u64 *hpas;		/* vmalloc'ed */
#define MM_IOMMU_TABLE_INVALID_HPA	((uint64_t)-1)
	u64 dev_hpa;		/* Device memory base address */
};

static long mm_iommu_adjust_locked_vm(struct mm_struct *mm,
		unsigned long npages, bool incr)
{
	long ret = 0, locked, lock_limit;

	if (!npages)
		return 0;

	down_write(&mm->mmap_sem);

	if (incr) {
		locked = mm->locked_vm + npages;
		lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			ret = -ENOMEM;
		else
			mm->locked_vm += npages;
	} else {
		if (WARN_ON_ONCE(npages > mm->locked_vm))
			npages = mm->locked_vm;
		mm->locked_vm -= npages;
	}

	pr_debug("[%d] RLIMIT_MEMLOCK HASH64 %c%ld %ld/%ld\n",
			current ? current->pid : 0,
			incr ? '+' : '-',
			npages << PAGE_SHIFT,
			mm->locked_vm << PAGE_SHIFT,
			rlimit(RLIMIT_MEMLOCK));
	up_write(&mm->mmap_sem);

	return ret;
}

bool mm_iommu_preregistered(struct mm_struct *mm)
{
	return !list_empty(&mm->context.iommu_group_mem_list);
}
EXPORT_SYMBOL_GPL(mm_iommu_preregistered);

/*
 * Taken from alloc_migrate_target with changes to remove CMA allocations
 */
struct page *new_iommu_non_cma_page(struct page *page, unsigned long private)
{
	gfp_t gfp_mask = GFP_USER;
	struct page *new_page;

	if (PageCompound(page))
		return NULL;

	if (PageHighMem(page))
		gfp_mask |= __GFP_HIGHMEM;

	/*
	 * We don't want the allocation to force an OOM if possibe
	 */
	new_page = alloc_page(gfp_mask | __GFP_NORETRY | __GFP_NOWARN);
	return new_page;
}

static int mm_iommu_move_page_from_cma(struct page *page)
{
	int ret = 0;
	LIST_HEAD(cma_migrate_pages);

	/* Ignore huge pages for now */
	if (PageCompound(page))
		return -EBUSY;

	lru_add_drain();
	ret = isolate_lru_page(page);
	if (ret)
		return ret;

	list_add(&page->lru, &cma_migrate_pages);
	put_page(page); /* Drop the gup reference */

	ret = migrate_pages(&cma_migrate_pages, new_iommu_non_cma_page,
				NULL, 0, MIGRATE_SYNC, MR_CONTIG_RANGE);
	if (ret) {
		if (!list_empty(&cma_migrate_pages))
			putback_movable_pages(&cma_migrate_pages);
	}

	return 0;
}

static long mm_iommu_do_alloc(struct mm_struct *mm, unsigned long ua,
		unsigned long entries, unsigned long dev_hpa,
		struct mm_iommu_table_group_mem_t **pmem)
{
	struct mm_iommu_table_group_mem_t *mem;
	long i, j, ret = 0, locked_entries = 0;
	unsigned int pageshift;
	unsigned long flags;
	unsigned long cur_ua;
	struct page *page = NULL;

	mutex_lock(&mem_list_mutex);

	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list,
			next) {
		/* Overlap? */
		if ((mem->ua < (ua + (entries << PAGE_SHIFT))) &&
				(ua < (mem->ua +
				       (mem->entries << PAGE_SHIFT)))) {
			ret = -EINVAL;
			goto unlock_exit;
		}

	}

	if (dev_hpa == MM_IOMMU_TABLE_INVALID_HPA) {
		ret = mm_iommu_adjust_locked_vm(mm, entries, true);
		if (ret)
			goto unlock_exit;

		locked_entries = entries;
	}

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem) {
		ret = -ENOMEM;
		goto unlock_exit;
	}

	if (dev_hpa != MM_IOMMU_TABLE_INVALID_HPA) {
		mem->pageshift = __ffs(dev_hpa | (entries << PAGE_SHIFT));
		mem->dev_hpa = dev_hpa;
		goto good_exit;
	}
	mem->dev_hpa = MM_IOMMU_TABLE_INVALID_HPA;

	/*
	 * For a starting point for a maximum page size calculation
	 * we use @ua and @entries natural alignment to allow IOMMU pages
	 * smaller than huge pages but still bigger than PAGE_SIZE.
	 */
	mem->pageshift = __ffs(ua | (entries << PAGE_SHIFT));
	mem->hpas = vzalloc(array_size(entries, sizeof(mem->hpas[0])));
	if (!mem->hpas) {
		kfree(mem);
		ret = -ENOMEM;
		goto unlock_exit;
	}

	for (i = 0; i < entries; ++i) {
		cur_ua = ua + (i << PAGE_SHIFT);
		if (1 != get_user_pages_fast(cur_ua,
					1/* pages */, 1/* iswrite */, &page)) {
			ret = -EFAULT;
			for (j = 0; j < i; ++j)
				put_page(pfn_to_page(mem->hpas[j] >>
						PAGE_SHIFT));
			vfree(mem->hpas);
			kfree(mem);
			goto unlock_exit;
		}
		/*
		 * If we get a page from the CMA zone, since we are going to
		 * be pinning these entries, we might as well move them out
		 * of the CMA zone if possible. NOTE: faulting in + migration
		 * can be expensive. Batching can be considered later
		 */
		if (is_migrate_cma_page(page)) {
			if (mm_iommu_move_page_from_cma(page))
				goto populate;
			if (1 != get_user_pages_fast(cur_ua,
						1/* pages */, 1/* iswrite */,
						&page)) {
				ret = -EFAULT;
				for (j = 0; j < i; ++j)
					put_page(pfn_to_page(mem->hpas[j] >>
								PAGE_SHIFT));
				vfree(mem->hpas);
				kfree(mem);
				goto unlock_exit;
			}
		}
populate:
		pageshift = PAGE_SHIFT;
		if (mem->pageshift > PAGE_SHIFT && PageCompound(page)) {
			pte_t *pte;
			struct page *head = compound_head(page);
			unsigned int compshift = compound_order(head);
			unsigned int pteshift;

			local_irq_save(flags); /* disables as well */
			pte = find_linux_pte(mm->pgd, cur_ua, NULL, &pteshift);

			/* Double check it is still the same pinned page */
			if (pte && pte_page(*pte) == head &&
			    pteshift == compshift + PAGE_SHIFT)
				pageshift = max_t(unsigned int, pteshift,
						PAGE_SHIFT);
			local_irq_restore(flags);
		}
		mem->pageshift = min(mem->pageshift, pageshift);
		mem->hpas[i] = page_to_pfn(page) << PAGE_SHIFT;
	}

good_exit:
	atomic64_set(&mem->mapped, 1);
	mem->used = 1;
	mem->ua = ua;
	mem->entries = entries;
	*pmem = mem;

	list_add_rcu(&mem->next, &mm->context.iommu_group_mem_list);

unlock_exit:
	if (locked_entries && ret)
		mm_iommu_adjust_locked_vm(mm, locked_entries, false);

	mutex_unlock(&mem_list_mutex);

	return ret;
}

long mm_iommu_new(struct mm_struct *mm, unsigned long ua, unsigned long entries,
		struct mm_iommu_table_group_mem_t **pmem)
{
	return mm_iommu_do_alloc(mm, ua, entries, MM_IOMMU_TABLE_INVALID_HPA,
			pmem);
}
EXPORT_SYMBOL_GPL(mm_iommu_new);

long mm_iommu_newdev(struct mm_struct *mm, unsigned long ua,
		unsigned long entries, unsigned long dev_hpa,
		struct mm_iommu_table_group_mem_t **pmem)
{
	return mm_iommu_do_alloc(mm, ua, entries, dev_hpa, pmem);
}
EXPORT_SYMBOL_GPL(mm_iommu_newdev);

static void mm_iommu_unpin(struct mm_iommu_table_group_mem_t *mem)
{
	long i;
	struct page *page = NULL;

	if (!mem->hpas)
		return;

	for (i = 0; i < mem->entries; ++i) {
		if (!mem->hpas[i])
			continue;

		page = pfn_to_page(mem->hpas[i] >> PAGE_SHIFT);
		if (!page)
			continue;

		if (mem->hpas[i] & MM_IOMMU_TABLE_GROUP_PAGE_DIRTY)
			SetPageDirty(page);

		put_page(page);
		mem->hpas[i] = 0;
	}
}

static void mm_iommu_do_free(struct mm_iommu_table_group_mem_t *mem)
{

	mm_iommu_unpin(mem);
	vfree(mem->hpas);
	kfree(mem);
}

static void mm_iommu_free(struct rcu_head *head)
{
	struct mm_iommu_table_group_mem_t *mem = container_of(head,
			struct mm_iommu_table_group_mem_t, rcu);

	mm_iommu_do_free(mem);
}

static void mm_iommu_release(struct mm_iommu_table_group_mem_t *mem)
{
	list_del_rcu(&mem->next);
	call_rcu(&mem->rcu, mm_iommu_free);
}

long mm_iommu_put(struct mm_struct *mm, struct mm_iommu_table_group_mem_t *mem)
{
	long ret = 0;
	unsigned long entries, dev_hpa;

	mutex_lock(&mem_list_mutex);

	if (mem->used == 0) {
		ret = -ENOENT;
		goto unlock_exit;
	}

	--mem->used;
	/* There are still users, exit */
	if (mem->used)
		goto unlock_exit;

	/* Are there still mappings? */
	if (atomic_cmpxchg(&mem->mapped, 1, 0) != 1) {
		++mem->used;
		ret = -EBUSY;
		goto unlock_exit;
	}

	/* @mapped became 0 so now mappings are disabled, release the region */
	entries = mem->entries;
	dev_hpa = mem->dev_hpa;
	mm_iommu_release(mem);

	if (dev_hpa == MM_IOMMU_TABLE_INVALID_HPA)
		mm_iommu_adjust_locked_vm(mm, entries, false);

unlock_exit:
	mutex_unlock(&mem_list_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mm_iommu_put);

struct mm_iommu_table_group_mem_t *mm_iommu_lookup(struct mm_struct *mm,
		unsigned long ua, unsigned long size)
{
	struct mm_iommu_table_group_mem_t *mem, *ret = NULL;

	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list, next) {
		if ((mem->ua <= ua) &&
				(ua + size <= mem->ua +
				 (mem->entries << PAGE_SHIFT))) {
			ret = mem;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mm_iommu_lookup);

struct mm_iommu_table_group_mem_t *mm_iommu_lookup_rm(struct mm_struct *mm,
		unsigned long ua, unsigned long size)
{
	struct mm_iommu_table_group_mem_t *mem, *ret = NULL;

	list_for_each_entry_lockless(mem, &mm->context.iommu_group_mem_list,
			next) {
		if ((mem->ua <= ua) &&
				(ua + size <= mem->ua +
				 (mem->entries << PAGE_SHIFT))) {
			ret = mem;
			break;
		}
	}

	return ret;
}

struct mm_iommu_table_group_mem_t *mm_iommu_get(struct mm_struct *mm,
		unsigned long ua, unsigned long entries)
{
	struct mm_iommu_table_group_mem_t *mem, *ret = NULL;

	mutex_lock(&mem_list_mutex);

	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list, next) {
		if ((mem->ua == ua) && (mem->entries == entries)) {
			ret = mem;
			++mem->used;
			break;
		}
	}

	mutex_unlock(&mem_list_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mm_iommu_get);

long mm_iommu_ua_to_hpa(struct mm_iommu_table_group_mem_t *mem,
		unsigned long ua, unsigned int pageshift, unsigned long *hpa)
{
	const long entry = (ua - mem->ua) >> PAGE_SHIFT;
	u64 *va;

	if (entry >= mem->entries)
		return -EFAULT;

	if (pageshift > mem->pageshift)
		return -EFAULT;

	if (!mem->hpas) {
		*hpa = mem->dev_hpa + (ua - mem->ua);
		return 0;
	}

	va = &mem->hpas[entry];
	*hpa = (*va & MM_IOMMU_TABLE_GROUP_PAGE_MASK) | (ua & ~PAGE_MASK);

	return 0;
}
EXPORT_SYMBOL_GPL(mm_iommu_ua_to_hpa);

long mm_iommu_ua_to_hpa_rm(struct mm_iommu_table_group_mem_t *mem,
		unsigned long ua, unsigned int pageshift, unsigned long *hpa)
{
	const long entry = (ua - mem->ua) >> PAGE_SHIFT;
	unsigned long *pa;

	if (entry >= mem->entries)
		return -EFAULT;

	if (pageshift > mem->pageshift)
		return -EFAULT;

	if (!mem->hpas) {
		*hpa = mem->dev_hpa + (ua - mem->ua);
		return 0;
	}

	pa = (void *) vmalloc_to_phys(&mem->hpas[entry]);
	if (!pa)
		return -EFAULT;

	*hpa = (*pa & MM_IOMMU_TABLE_GROUP_PAGE_MASK) | (ua & ~PAGE_MASK);

	return 0;
}

extern void mm_iommu_ua_mark_dirty_rm(struct mm_struct *mm, unsigned long ua)
{
	struct mm_iommu_table_group_mem_t *mem;
	long entry;
	void *va;
	unsigned long *pa;

	mem = mm_iommu_lookup_rm(mm, ua, PAGE_SIZE);
	if (!mem)
		return;

	if (mem->dev_hpa != MM_IOMMU_TABLE_INVALID_HPA)
		return;

	entry = (ua - mem->ua) >> PAGE_SHIFT;
	va = &mem->hpas[entry];

	pa = (void *) vmalloc_to_phys(va);
	if (!pa)
		return;

	*pa |= MM_IOMMU_TABLE_GROUP_PAGE_DIRTY;
}

bool mm_iommu_is_devmem(struct mm_struct *mm, unsigned long hpa,
		unsigned int pageshift, unsigned long *size)
{
	struct mm_iommu_table_group_mem_t *mem;
	unsigned long end;

	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list, next) {
		if (mem->dev_hpa == MM_IOMMU_TABLE_INVALID_HPA)
			continue;

		end = mem->dev_hpa + (mem->entries << PAGE_SHIFT);
		if ((mem->dev_hpa <= hpa) && (hpa < end)) {
			/*
			 * Since the IOMMU page size might be bigger than
			 * PAGE_SIZE, the amount of preregistered memory
			 * starting from @hpa might be smaller than 1<<pageshift
			 * and the caller needs to distinguish this situation.
			 */
			*size = min(1UL << pageshift, end - hpa);
			return true;
		}
	}

	return false;
}
EXPORT_SYMBOL_GPL(mm_iommu_is_devmem);

long mm_iommu_mapped_inc(struct mm_iommu_table_group_mem_t *mem)
{
	if (atomic64_inc_not_zero(&mem->mapped))
		return 0;

	/* Last mm_iommu_put() has been called, no more mappings allowed() */
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(mm_iommu_mapped_inc);

void mm_iommu_mapped_dec(struct mm_iommu_table_group_mem_t *mem)
{
	atomic64_add_unless(&mem->mapped, -1, 1);
}
EXPORT_SYMBOL_GPL(mm_iommu_mapped_dec);

void mm_iommu_init(struct mm_struct *mm)
{
	INIT_LIST_HEAD_RCU(&mm->context.iommu_group_mem_list);
}
