/*
 *    Copyright IBM Corp. 2007,2009
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/quicklist.h>
#include <linux/rcupdate.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

#ifndef CONFIG_64BIT
#define ALLOC_ORDER	1
#define FRAG_MASK	0x0f
#else
#define ALLOC_ORDER	2
#define FRAG_MASK	0x03
#endif

unsigned long VMALLOC_START = VMALLOC_END - VMALLOC_SIZE;
EXPORT_SYMBOL(VMALLOC_START);

static int __init parse_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;
	VMALLOC_START = (VMALLOC_END - memparse(arg, &arg)) & PAGE_MASK;
	return 0;
}
early_param("vmalloc", parse_vmalloc);

unsigned long *crst_table_alloc(struct mm_struct *mm)
{
	struct page *page = alloc_pages(GFP_KERNEL, ALLOC_ORDER);

	if (!page)
		return NULL;
	return (unsigned long *) page_to_phys(page);
}

void crst_table_free(struct mm_struct *mm, unsigned long *table)
{
	free_pages((unsigned long) table, ALLOC_ORDER);
}

#ifdef CONFIG_64BIT
int crst_table_upgrade(struct mm_struct *mm, unsigned long limit)
{
	unsigned long *table, *pgd;
	unsigned long entry;

	BUG_ON(limit > (1UL << 53));
repeat:
	table = crst_table_alloc(mm);
	if (!table)
		return -ENOMEM;
	spin_lock_bh(&mm->page_table_lock);
	if (mm->context.asce_limit < limit) {
		pgd = (unsigned long *) mm->pgd;
		if (mm->context.asce_limit <= (1UL << 31)) {
			entry = _REGION3_ENTRY_EMPTY;
			mm->context.asce_limit = 1UL << 42;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION3;
		} else {
			entry = _REGION2_ENTRY_EMPTY;
			mm->context.asce_limit = 1UL << 53;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION2;
		}
		crst_table_init(table, entry);
		pgd_populate(mm, (pgd_t *) table, (pud_t *) pgd);
		mm->pgd = (pgd_t *) table;
		mm->task_size = mm->context.asce_limit;
		table = NULL;
	}
	spin_unlock_bh(&mm->page_table_lock);
	if (table)
		crst_table_free(mm, table);
	if (mm->context.asce_limit < limit)
		goto repeat;
	update_mm(mm, current);
	return 0;
}

void crst_table_downgrade(struct mm_struct *mm, unsigned long limit)
{
	pgd_t *pgd;

	if (mm->context.asce_limit <= limit)
		return;
	__tlb_flush_mm(mm);
	while (mm->context.asce_limit > limit) {
		pgd = mm->pgd;
		switch (pgd_val(*pgd) & _REGION_ENTRY_TYPE_MASK) {
		case _REGION_ENTRY_TYPE_R2:
			mm->context.asce_limit = 1UL << 42;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_REGION3;
			break;
		case _REGION_ENTRY_TYPE_R3:
			mm->context.asce_limit = 1UL << 31;
			mm->context.asce_bits = _ASCE_TABLE_LENGTH |
						_ASCE_USER_BITS |
						_ASCE_TYPE_SEGMENT;
			break;
		default:
			BUG();
		}
		mm->pgd = (pgd_t *) (pgd_val(*pgd) & _REGION_ENTRY_ORIGIN);
		mm->task_size = mm->context.asce_limit;
		crst_table_free(mm, (unsigned long *) pgd);
	}
	update_mm(mm, current);
}
#endif

static inline unsigned int atomic_xor_bits(atomic_t *v, unsigned int bits)
{
	unsigned int old, new;

	do {
		old = atomic_read(v);
		new = old ^ bits;
	} while (atomic_cmpxchg(v, old, new) != old);
	return new;
}

/*
 * page table entry allocation/free routines.
 */
#ifdef CONFIG_PGSTE
static inline unsigned long *page_table_alloc_pgste(struct mm_struct *mm)
{
	struct page *page;
	unsigned long *table;

	page = alloc_page(GFP_KERNEL|__GFP_REPEAT);
	if (!page)
		return NULL;
	pgtable_page_ctor(page);
	atomic_set(&page->_mapcount, 3);
	table = (unsigned long *) page_to_phys(page);
	clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE/2);
	clear_table(table + PTRS_PER_PTE, 0, PAGE_SIZE/2);
	return table;
}

static inline void page_table_free_pgste(unsigned long *table)
{
	struct page *page;

	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	pgtable_page_ctor(page);
	atomic_set(&page->_mapcount, -1);
	__free_page(page);
}
#endif

unsigned long *page_table_alloc(struct mm_struct *mm)
{
	struct page *page;
	unsigned long *table;
	unsigned int mask, bit;

#ifdef CONFIG_PGSTE
	if (mm_has_pgste(mm))
		return page_table_alloc_pgste(mm);
#endif
	/* Allocate fragments of a 4K page as 1K/2K page table */
	spin_lock_bh(&mm->context.list_lock);
	mask = FRAG_MASK;
	if (!list_empty(&mm->context.pgtable_list)) {
		page = list_first_entry(&mm->context.pgtable_list,
					struct page, lru);
		table = (unsigned long *) page_to_phys(page);
		mask = atomic_read(&page->_mapcount);
		mask = mask | (mask >> 4);
	}
	if ((mask & FRAG_MASK) == FRAG_MASK) {
		spin_unlock_bh(&mm->context.list_lock);
		page = alloc_page(GFP_KERNEL|__GFP_REPEAT);
		if (!page)
			return NULL;
		pgtable_page_ctor(page);
		atomic_set(&page->_mapcount, 1);
		table = (unsigned long *) page_to_phys(page);
		clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE);
		spin_lock_bh(&mm->context.list_lock);
		list_add(&page->lru, &mm->context.pgtable_list);
	} else {
		for (bit = 1; mask & bit; bit <<= 1)
			table += PTRS_PER_PTE;
		mask = atomic_xor_bits(&page->_mapcount, bit);
		if ((mask & FRAG_MASK) == FRAG_MASK)
			list_del(&page->lru);
	}
	spin_unlock_bh(&mm->context.list_lock);
	return table;
}

void page_table_free(struct mm_struct *mm, unsigned long *table)
{
	struct page *page;
	unsigned int bit, mask;

#ifdef CONFIG_PGSTE
	if (mm_has_pgste(mm))
		return page_table_free_pgste(table);
#endif
	/* Free 1K/2K page table fragment of a 4K page */
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	bit = 1 << ((__pa(table) & ~PAGE_MASK)/(PTRS_PER_PTE*sizeof(pte_t)));
	spin_lock_bh(&mm->context.list_lock);
	if ((atomic_read(&page->_mapcount) & FRAG_MASK) != FRAG_MASK)
		list_del(&page->lru);
	mask = atomic_xor_bits(&page->_mapcount, bit);
	if (mask & FRAG_MASK)
		list_add(&page->lru, &mm->context.pgtable_list);
	spin_unlock_bh(&mm->context.list_lock);
	if (mask == 0) {
		pgtable_page_dtor(page);
		atomic_set(&page->_mapcount, -1);
		__free_page(page);
	}
}

#ifdef CONFIG_HAVE_RCU_TABLE_FREE

static void __page_table_free_rcu(void *table, unsigned bit)
{
	struct page *page;

#ifdef CONFIG_PGSTE
	if (bit == FRAG_MASK)
		return page_table_free_pgste(table);
#endif
	/* Free 1K/2K page table fragment of a 4K page */
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	if (atomic_xor_bits(&page->_mapcount, bit) == 0) {
		pgtable_page_dtor(page);
		atomic_set(&page->_mapcount, -1);
		__free_page(page);
	}
}

void page_table_free_rcu(struct mmu_gather *tlb, unsigned long *table)
{
	struct mm_struct *mm;
	struct page *page;
	unsigned int bit, mask;

	mm = tlb->mm;
#ifdef CONFIG_PGSTE
	if (mm_has_pgste(mm)) {
		table = (unsigned long *) (__pa(table) | FRAG_MASK);
		tlb_remove_table(tlb, table);
		return;
	}
#endif
	bit = 1 << ((__pa(table) & ~PAGE_MASK) / (PTRS_PER_PTE*sizeof(pte_t)));
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	spin_lock_bh(&mm->context.list_lock);
	if ((atomic_read(&page->_mapcount) & FRAG_MASK) != FRAG_MASK)
		list_del(&page->lru);
	mask = atomic_xor_bits(&page->_mapcount, bit | (bit << 4));
	if (mask & FRAG_MASK)
		list_add_tail(&page->lru, &mm->context.pgtable_list);
	spin_unlock_bh(&mm->context.list_lock);
	table = (unsigned long *) (__pa(table) | (bit << 4));
	tlb_remove_table(tlb, table);
}

void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long) _table & PAGE_MASK);
	unsigned type = (unsigned long) _table & ~PAGE_MASK;

	if (type)
		__page_table_free_rcu(table, type);
	else
		free_pages((unsigned long) table, ALLOC_ORDER);
}

#endif

/*
 * switch on pgstes for its userspace process (for kvm)
 */
int s390_enable_sie(void)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm, *old_mm;

	/* Do we have switched amode? If no, we cannot do sie */
	if (user_mode == HOME_SPACE_MODE)
		return -EINVAL;

	/* Do we have pgstes? if yes, we are done */
	if (mm_has_pgste(tsk->mm))
		return 0;

	/* lets check if we are allowed to replace the mm */
	task_lock(tsk);
	if (!tsk->mm || atomic_read(&tsk->mm->mm_users) > 1 ||
#ifdef CONFIG_AIO
	    !hlist_empty(&tsk->mm->ioctx_list) ||
#endif
	    tsk->mm != tsk->active_mm) {
		task_unlock(tsk);
		return -EINVAL;
	}
	task_unlock(tsk);

	/* we copy the mm and let dup_mm create the page tables with_pgstes */
	tsk->mm->context.alloc_pgste = 1;
	mm = dup_mm(tsk);
	tsk->mm->context.alloc_pgste = 0;
	if (!mm)
		return -ENOMEM;

	/* Now lets check again if something happened */
	task_lock(tsk);
	if (!tsk->mm || atomic_read(&tsk->mm->mm_users) > 1 ||
#ifdef CONFIG_AIO
	    !hlist_empty(&tsk->mm->ioctx_list) ||
#endif
	    tsk->mm != tsk->active_mm) {
		mmput(mm);
		task_unlock(tsk);
		return -EINVAL;
	}

	/* ok, we are alone. No ptrace, no threads, etc. */
	old_mm = tsk->mm;
	tsk->mm = tsk->active_mm = mm;
	preempt_disable();
	update_mm(mm, tsk);
	atomic_inc(&mm->context.attach_count);
	atomic_dec(&old_mm->context.attach_count);
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
	preempt_enable();
	task_unlock(tsk);
	mmput(old_mm);
	return 0;
}
EXPORT_SYMBOL_GPL(s390_enable_sie);

#if defined(CONFIG_DEBUG_PAGEALLOC) && defined(CONFIG_HIBERNATION)
bool kernel_page_present(struct page *page)
{
	unsigned long addr;
	int cc;

	addr = page_to_phys(page);
	asm volatile(
		"	lra	%1,0(%1)\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (cc), "+a" (addr) : : "cc");
	return cc == 0;
}
#endif /* CONFIG_HIBERNATION && CONFIG_DEBUG_PAGEALLOC */
