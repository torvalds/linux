// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  IOMMU helpers in MMU context.
 *
 *  Copyright (C) 2015 IBM Corp. <aik@ozlabs.ru>
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
#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/pte-walk.h>
#include <linux/mm_inline.h>

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
	u64 entries;		/* number of entries in hpas/hpages[] */
	/*
	 * in mm_iommu_get we temporarily use this to store
	 * struct page address.
	 *
	 * We need to convert ua to hpa in real mode. Make it
	 * simpler by storing physical address.
	 */
	union {
		struct page **hpages;	/* vmalloc'ed */
		phys_addr_t *hpas;
	};
#define MM_IOMMU_TABLE_INVALID_HPA	((uint64_t)-1)
	u64 dev_hpa;		/* Device memory base address */
};

bool mm_iommu_preregistered(struct mm_struct *mm)
{
	return !list_empty(&mm->context.iommu_group_mem_list);
}
EXPORT_SYMBOL_GPL(mm_iommu_preregistered);

static long mm_iommu_do_alloc(struct mm_struct *mm, unsigned long ua,
			      unsigned long entries, unsigned long dev_hpa,
			      struct mm_iommu_table_group_mem_t **pmem)
{
	struct mm_iommu_table_group_mem_t *mem, *mem2;
	long i, ret, locked_entries = 0, pinned = 0;
	unsigned int pageshift;
	unsigned long entry, chunk;

	if (dev_hpa == MM_IOMMU_TABLE_INVALID_HPA) {
		ret = account_locked_vm(mm, entries, true);
		if (ret)
			return ret;

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

	mmap_read_lock(mm);
	chunk = (1UL << (PAGE_SHIFT + MAX_ORDER)) /
			sizeof(struct vm_area_struct *);
	chunk = min(chunk, entries);
	for (entry = 0; entry < entries; entry += chunk) {
		unsigned long n = min(entries - entry, chunk);

		ret = pin_user_pages(ua + (entry << PAGE_SHIFT), n,
				FOLL_WRITE | FOLL_LONGTERM,
				mem->hpages + entry);
		if (ret == n) {
			pinned += n;
			continue;
		}
		if (ret > 0)
			pinned += ret;
		break;
	}
	mmap_read_unlock(mm);
	if (pinned != entries) {
		if (!ret)
			ret = -EFAULT;
		goto free_exit;
	}

good_exit:
	atomic64_set(&mem->mapped, 1);
	mem->used = 1;
	mem->ua = ua;
	mem->entries = entries;

	mutex_lock(&mem_list_mutex);

	list_for_each_entry_rcu(mem2, &mm->context.iommu_group_mem_list, next,
				lockdep_is_held(&mem_list_mutex)) {
		/* Overlap? */
		if ((mem2->ua < (ua + (entries << PAGE_SHIFT))) &&
				(ua < (mem2->ua +
				       (mem2->entries << PAGE_SHIFT)))) {
			ret = -EINVAL;
			mutex_unlock(&mem_list_mutex);
			goto free_exit;
		}
	}

	if (mem->dev_hpa == MM_IOMMU_TABLE_INVALID_HPA) {
		/*
		 * Allow to use larger than 64k IOMMU pages. Only do that
		 * if we are backed by hugetlb. Skip device memory as it is not
		 * backed with page structs.
		 */
		pageshift = PAGE_SHIFT;
		for (i = 0; i < entries; ++i) {
			struct page *page = mem->hpages[i];

			if ((mem->pageshift > PAGE_SHIFT) && PageHuge(page))
				pageshift = page_shift(compound_head(page));
			mem->pageshift = min(mem->pageshift, pageshift);
			/*
			 * We don't need struct page reference any more, switch
			 * to physical address.
			 */
			mem->hpas[i] = page_to_pfn(page) << PAGE_SHIFT;
		}
	}

	list_add_rcu(&mem->next, &mm->context.iommu_group_mem_list);

	mutex_unlock(&mem_list_mutex);

	*pmem = mem;

	return 0;

free_exit:
	/* free the references taken */
	unpin_user_pages(mem->hpages, pinned);

	vfree(mem->hpas);
	kfree(mem);

unlock_exit:
	account_locked_vm(mm, locked_entries, false);

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

		unpin_user_page(page);

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
	unsigned long unlock_entries = 0;

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
	if (atomic64_cmpxchg(&mem->mapped, 1, 0) != 1) {
		++mem->used;
		ret = -EBUSY;
		goto unlock_exit;
	}

	if (mem->dev_hpa == MM_IOMMU_TABLE_INVALID_HPA)
		unlock_entries = mem->entries;

	/* @mapped became 0 so now mappings are disabled, release the region */
	mm_iommu_release(mem);

unlock_exit:
	mutex_unlock(&mem_list_mutex);

	account_locked_vm(mm, unlock_entries, false);

	return ret;
}
EXPORT_SYMBOL_GPL(mm_iommu_put);

struct mm_iommu_table_group_mem_t *mm_iommu_lookup(struct mm_struct *mm,
		unsigned long ua, unsigned long size)
{
	struct mm_iommu_table_group_mem_t *mem, *ret = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list, next) {
		if ((mem->ua <= ua) &&
				(ua + size <= mem->ua +
				 (mem->entries << PAGE_SHIFT))) {
			ret = mem;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(mm_iommu_lookup);

struct mm_iommu_table_group_mem_t *mm_iommu_get(struct mm_struct *mm,
		unsigned long ua, unsigned long entries)
{
	struct mm_iommu_table_group_mem_t *mem, *ret = NULL;

	mutex_lock(&mem_list_mutex);

	list_for_each_entry_rcu(mem, &mm->context.iommu_group_mem_list, next,
				lockdep_is_held(&mem_list_mutex)) {
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

bool mm_iommu_is_devmem(struct mm_struct *mm, unsigned long hpa,
		unsigned int pageshift, unsigned long *size)
{
	struct mm_iommu_table_group_mem_t *mem;
	unsigned long end;

	rcu_read_lock();
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
	rcu_read_unlock();

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
