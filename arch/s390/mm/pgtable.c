/*
 *    Copyright IBM Corp. 2007, 2011
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
#include <linux/slab.h>

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
	return 0;
}

void crst_table_downgrade(struct mm_struct *mm, unsigned long limit)
{
	pgd_t *pgd;

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
}
#endif

#ifdef CONFIG_PGSTE

/**
 * gmap_alloc - allocate a guest address space
 * @mm: pointer to the parent mm_struct
 *
 * Returns a guest address space structure.
 */
struct gmap *gmap_alloc(struct mm_struct *mm)
{
	struct gmap *gmap;
	struct page *page;
	unsigned long *table;

	gmap = kzalloc(sizeof(struct gmap), GFP_KERNEL);
	if (!gmap)
		goto out;
	INIT_LIST_HEAD(&gmap->crst_list);
	gmap->mm = mm;
	page = alloc_pages(GFP_KERNEL, ALLOC_ORDER);
	if (!page)
		goto out_free;
	list_add(&page->lru, &gmap->crst_list);
	table = (unsigned long *) page_to_phys(page);
	crst_table_init(table, _REGION1_ENTRY_EMPTY);
	gmap->table = table;
	gmap->asce = _ASCE_TYPE_REGION1 | _ASCE_TABLE_LENGTH |
		     _ASCE_USER_BITS | __pa(table);
	list_add(&gmap->list, &mm->context.gmap_list);
	return gmap;

out_free:
	kfree(gmap);
out:
	return NULL;
}
EXPORT_SYMBOL_GPL(gmap_alloc);

static int gmap_unlink_segment(struct gmap *gmap, unsigned long *table)
{
	struct gmap_pgtable *mp;
	struct gmap_rmap *rmap;
	struct page *page;

	if (*table & _SEGMENT_ENTRY_INV)
		return 0;
	page = pfn_to_page(*table >> PAGE_SHIFT);
	mp = (struct gmap_pgtable *) page->index;
	list_for_each_entry(rmap, &mp->mapper, list) {
		if (rmap->entry != table)
			continue;
		list_del(&rmap->list);
		kfree(rmap);
		break;
	}
	*table = _SEGMENT_ENTRY_INV | _SEGMENT_ENTRY_RO | mp->vmaddr;
	return 1;
}

static void gmap_flush_tlb(struct gmap *gmap)
{
	if (MACHINE_HAS_IDTE)
		__tlb_flush_idte((unsigned long) gmap->table |
				 _ASCE_TYPE_REGION1);
	else
		__tlb_flush_global();
}

/**
 * gmap_free - free a guest address space
 * @gmap: pointer to the guest address space structure
 */
void gmap_free(struct gmap *gmap)
{
	struct page *page, *next;
	unsigned long *table;
	int i;


	/* Flush tlb. */
	if (MACHINE_HAS_IDTE)
		__tlb_flush_idte((unsigned long) gmap->table |
				 _ASCE_TYPE_REGION1);
	else
		__tlb_flush_global();

	/* Free all segment & region tables. */
	down_read(&gmap->mm->mmap_sem);
	spin_lock(&gmap->mm->page_table_lock);
	list_for_each_entry_safe(page, next, &gmap->crst_list, lru) {
		table = (unsigned long *) page_to_phys(page);
		if ((*table & _REGION_ENTRY_TYPE_MASK) == 0)
			/* Remove gmap rmap structures for segment table. */
			for (i = 0; i < PTRS_PER_PMD; i++, table++)
				gmap_unlink_segment(gmap, table);
		__free_pages(page, ALLOC_ORDER);
	}
	spin_unlock(&gmap->mm->page_table_lock);
	up_read(&gmap->mm->mmap_sem);
	list_del(&gmap->list);
	kfree(gmap);
}
EXPORT_SYMBOL_GPL(gmap_free);

/**
 * gmap_enable - switch primary space to the guest address space
 * @gmap: pointer to the guest address space structure
 */
void gmap_enable(struct gmap *gmap)
{
	S390_lowcore.gmap = (unsigned long) gmap;
}
EXPORT_SYMBOL_GPL(gmap_enable);

/**
 * gmap_disable - switch back to the standard primary address space
 * @gmap: pointer to the guest address space structure
 */
void gmap_disable(struct gmap *gmap)
{
	S390_lowcore.gmap = 0UL;
}
EXPORT_SYMBOL_GPL(gmap_disable);

/*
 * gmap_alloc_table is assumed to be called with mmap_sem held
 */
static int gmap_alloc_table(struct gmap *gmap,
			       unsigned long *table, unsigned long init)
{
	struct page *page;
	unsigned long *new;

	/* since we dont free the gmap table until gmap_free we can unlock */
	spin_unlock(&gmap->mm->page_table_lock);
	page = alloc_pages(GFP_KERNEL, ALLOC_ORDER);
	spin_lock(&gmap->mm->page_table_lock);
	if (!page)
		return -ENOMEM;
	new = (unsigned long *) page_to_phys(page);
	crst_table_init(new, init);
	if (*table & _REGION_ENTRY_INV) {
		list_add(&page->lru, &gmap->crst_list);
		*table = (unsigned long) new | _REGION_ENTRY_LENGTH |
			(*table & _REGION_ENTRY_TYPE_MASK);
	} else
		__free_pages(page, ALLOC_ORDER);
	return 0;
}

/**
 * gmap_unmap_segment - unmap segment from the guest address space
 * @gmap: pointer to the guest address space structure
 * @addr: address in the guest address space
 * @len: length of the memory area to unmap
 *
 * Returns 0 if the unmap succeded, -EINVAL if not.
 */
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len)
{
	unsigned long *table;
	unsigned long off;
	int flush;

	if ((to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || to + len < to)
		return -EINVAL;

	flush = 0;
	down_read(&gmap->mm->mmap_sem);
	spin_lock(&gmap->mm->page_table_lock);
	for (off = 0; off < len; off += PMD_SIZE) {
		/* Walk the guest addr space page table */
		table = gmap->table + (((to + off) >> 53) & 0x7ff);
		if (*table & _REGION_ENTRY_INV)
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 42) & 0x7ff);
		if (*table & _REGION_ENTRY_INV)
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 31) & 0x7ff);
		if (*table & _REGION_ENTRY_INV)
			goto out;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 20) & 0x7ff);

		/* Clear segment table entry in guest address space. */
		flush |= gmap_unlink_segment(gmap, table);
		*table = _SEGMENT_ENTRY_INV;
	}
out:
	spin_unlock(&gmap->mm->page_table_lock);
	up_read(&gmap->mm->mmap_sem);
	if (flush)
		gmap_flush_tlb(gmap);
	return 0;
}
EXPORT_SYMBOL_GPL(gmap_unmap_segment);

/**
 * gmap_mmap_segment - map a segment to the guest address space
 * @gmap: pointer to the guest address space structure
 * @from: source address in the parent address space
 * @to: target address in the guest address space
 *
 * Returns 0 if the mmap succeded, -EINVAL or -ENOMEM if not.
 */
int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len)
{
	unsigned long *table;
	unsigned long off;
	int flush;

	if ((from | to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || from + len > PGDIR_SIZE ||
	    from + len < from || to + len < to)
		return -EINVAL;

	flush = 0;
	down_read(&gmap->mm->mmap_sem);
	spin_lock(&gmap->mm->page_table_lock);
	for (off = 0; off < len; off += PMD_SIZE) {
		/* Walk the gmap address space page table */
		table = gmap->table + (((to + off) >> 53) & 0x7ff);
		if ((*table & _REGION_ENTRY_INV) &&
		    gmap_alloc_table(gmap, table, _REGION2_ENTRY_EMPTY))
			goto out_unmap;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 42) & 0x7ff);
		if ((*table & _REGION_ENTRY_INV) &&
		    gmap_alloc_table(gmap, table, _REGION3_ENTRY_EMPTY))
			goto out_unmap;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 31) & 0x7ff);
		if ((*table & _REGION_ENTRY_INV) &&
		    gmap_alloc_table(gmap, table, _SEGMENT_ENTRY_EMPTY))
			goto out_unmap;
		table = (unsigned long *) (*table & _REGION_ENTRY_ORIGIN);
		table = table + (((to + off) >> 20) & 0x7ff);

		/* Store 'from' address in an invalid segment table entry. */
		flush |= gmap_unlink_segment(gmap, table);
		*table = _SEGMENT_ENTRY_INV | _SEGMENT_ENTRY_RO | (from + off);
	}
	spin_unlock(&gmap->mm->page_table_lock);
	up_read(&gmap->mm->mmap_sem);
	if (flush)
		gmap_flush_tlb(gmap);
	return 0;

out_unmap:
	spin_unlock(&gmap->mm->page_table_lock);
	up_read(&gmap->mm->mmap_sem);
	gmap_unmap_segment(gmap, to, len);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(gmap_map_segment);

static unsigned long *gmap_table_walk(unsigned long address, struct gmap *gmap)
{
	unsigned long *table;

	table = gmap->table + ((address >> 53) & 0x7ff);
	if (unlikely(*table & _REGION_ENTRY_INV))
		return ERR_PTR(-EFAULT);
	table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	table = table + ((address >> 42) & 0x7ff);
	if (unlikely(*table & _REGION_ENTRY_INV))
		return ERR_PTR(-EFAULT);
	table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	table = table + ((address >> 31) & 0x7ff);
	if (unlikely(*table & _REGION_ENTRY_INV))
		return ERR_PTR(-EFAULT);
	table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	table = table + ((address >> 20) & 0x7ff);
	return table;
}

/**
 * __gmap_translate - translate a guest address to a user space address
 * @address: guest address
 * @gmap: pointer to guest mapping meta data structure
 *
 * Returns user space address which corresponds to the guest address or
 * -EFAULT if no such mapping exists.
 * This function does not establish potentially missing page table entries.
 * The mmap_sem of the mm that belongs to the address space must be held
 * when this function gets called.
 */
unsigned long __gmap_translate(unsigned long address, struct gmap *gmap)
{
	unsigned long *segment_ptr, vmaddr, segment;
	struct gmap_pgtable *mp;
	struct page *page;

	current->thread.gmap_addr = address;
	segment_ptr = gmap_table_walk(address, gmap);
	if (IS_ERR(segment_ptr))
		return PTR_ERR(segment_ptr);
	/* Convert the gmap address to an mm address. */
	segment = *segment_ptr;
	if (!(segment & _SEGMENT_ENTRY_INV)) {
		page = pfn_to_page(segment >> PAGE_SHIFT);
		mp = (struct gmap_pgtable *) page->index;
		return mp->vmaddr | (address & ~PMD_MASK);
	} else if (segment & _SEGMENT_ENTRY_RO) {
		vmaddr = segment & _SEGMENT_ENTRY_ORIGIN;
		return vmaddr | (address & ~PMD_MASK);
	}
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(__gmap_translate);

/**
 * gmap_translate - translate a guest address to a user space address
 * @address: guest address
 * @gmap: pointer to guest mapping meta data structure
 *
 * Returns user space address which corresponds to the guest address or
 * -EFAULT if no such mapping exists.
 * This function does not establish potentially missing page table entries.
 */
unsigned long gmap_translate(unsigned long address, struct gmap *gmap)
{
	unsigned long rc;

	down_read(&gmap->mm->mmap_sem);
	rc = __gmap_translate(address, gmap);
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_translate);

static int gmap_connect_pgtable(unsigned long address, unsigned long segment,
				unsigned long *segment_ptr, struct gmap *gmap)
{
	unsigned long vmaddr;
	struct vm_area_struct *vma;
	struct gmap_pgtable *mp;
	struct gmap_rmap *rmap;
	struct mm_struct *mm;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	mm = gmap->mm;
	vmaddr = segment & _SEGMENT_ENTRY_ORIGIN;
	vma = find_vma(mm, vmaddr);
	if (!vma || vma->vm_start > vmaddr)
		return -EFAULT;
	/* Walk the parent mm page table */
	pgd = pgd_offset(mm, vmaddr);
	pud = pud_alloc(mm, pgd, vmaddr);
	if (!pud)
		return -ENOMEM;
	pmd = pmd_alloc(mm, pud, vmaddr);
	if (!pmd)
		return -ENOMEM;
	if (!pmd_present(*pmd) &&
	    __pte_alloc(mm, vma, pmd, vmaddr))
		return -ENOMEM;
	/* pmd now points to a valid segment table entry. */
	rmap = kmalloc(sizeof(*rmap), GFP_KERNEL|__GFP_REPEAT);
	if (!rmap)
		return -ENOMEM;
	/* Link gmap segment table entry location to page table. */
	page = pmd_page(*pmd);
	mp = (struct gmap_pgtable *) page->index;
	rmap->gmap = gmap;
	rmap->entry = segment_ptr;
	rmap->vmaddr = address;
	spin_lock(&mm->page_table_lock);
	if (*segment_ptr == segment) {
		list_add(&rmap->list, &mp->mapper);
		/* Set gmap segment table entry to page table. */
		*segment_ptr = pmd_val(*pmd) & PAGE_MASK;
		rmap = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	kfree(rmap);
	return 0;
}

static void gmap_disconnect_pgtable(struct mm_struct *mm, unsigned long *table)
{
	struct gmap_rmap *rmap, *next;
	struct gmap_pgtable *mp;
	struct page *page;
	int flush;

	flush = 0;
	spin_lock(&mm->page_table_lock);
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	mp = (struct gmap_pgtable *) page->index;
	list_for_each_entry_safe(rmap, next, &mp->mapper, list) {
		*rmap->entry =
			_SEGMENT_ENTRY_INV | _SEGMENT_ENTRY_RO | mp->vmaddr;
		list_del(&rmap->list);
		kfree(rmap);
		flush = 1;
	}
	spin_unlock(&mm->page_table_lock);
	if (flush)
		__tlb_flush_global();
}

/*
 * this function is assumed to be called with mmap_sem held
 */
unsigned long __gmap_fault(unsigned long address, struct gmap *gmap)
{
	unsigned long *segment_ptr, segment;
	struct gmap_pgtable *mp;
	struct page *page;
	int rc;

	current->thread.gmap_addr = address;
	segment_ptr = gmap_table_walk(address, gmap);
	if (IS_ERR(segment_ptr))
		return -EFAULT;
	/* Convert the gmap address to an mm address. */
	while (1) {
		segment = *segment_ptr;
		if (!(segment & _SEGMENT_ENTRY_INV)) {
			/* Page table is present */
			page = pfn_to_page(segment >> PAGE_SHIFT);
			mp = (struct gmap_pgtable *) page->index;
			return mp->vmaddr | (address & ~PMD_MASK);
		}
		if (!(segment & _SEGMENT_ENTRY_RO))
			/* Nothing mapped in the gmap address space. */
			break;
		rc = gmap_connect_pgtable(address, segment, segment_ptr, gmap);
		if (rc)
			return rc;
	}
	return -EFAULT;
}

unsigned long gmap_fault(unsigned long address, struct gmap *gmap)
{
	unsigned long rc;

	down_read(&gmap->mm->mmap_sem);
	rc = __gmap_fault(address, gmap);
	up_read(&gmap->mm->mmap_sem);

	return rc;
}
EXPORT_SYMBOL_GPL(gmap_fault);

void gmap_discard(unsigned long from, unsigned long to, struct gmap *gmap)
{

	unsigned long *table, address, size;
	struct vm_area_struct *vma;
	struct gmap_pgtable *mp;
	struct page *page;

	down_read(&gmap->mm->mmap_sem);
	address = from;
	while (address < to) {
		/* Walk the gmap address space page table */
		table = gmap->table + ((address >> 53) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INV)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + ((address >> 42) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INV)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + ((address >> 31) & 0x7ff);
		if (unlikely(*table & _REGION_ENTRY_INV)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		table = table + ((address >> 20) & 0x7ff);
		if (unlikely(*table & _SEGMENT_ENTRY_INV)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}
		page = pfn_to_page(*table >> PAGE_SHIFT);
		mp = (struct gmap_pgtable *) page->index;
		vma = find_vma(gmap->mm, mp->vmaddr);
		size = min(to - address, PMD_SIZE - (address & ~PMD_MASK));
		zap_page_range(vma, mp->vmaddr | (address & ~PMD_MASK),
			       size, NULL);
		address = (address + PMD_SIZE) & PMD_MASK;
	}
	up_read(&gmap->mm->mmap_sem);
}
EXPORT_SYMBOL_GPL(gmap_discard);

static LIST_HEAD(gmap_notifier_list);
static DEFINE_SPINLOCK(gmap_notifier_lock);

/**
 * gmap_register_ipte_notifier - register a pte invalidation callback
 * @nb: pointer to the gmap notifier block
 */
void gmap_register_ipte_notifier(struct gmap_notifier *nb)
{
	spin_lock(&gmap_notifier_lock);
	list_add(&nb->list, &gmap_notifier_list);
	spin_unlock(&gmap_notifier_lock);
}
EXPORT_SYMBOL_GPL(gmap_register_ipte_notifier);

/**
 * gmap_unregister_ipte_notifier - remove a pte invalidation callback
 * @nb: pointer to the gmap notifier block
 */
void gmap_unregister_ipte_notifier(struct gmap_notifier *nb)
{
	spin_lock(&gmap_notifier_lock);
	list_del_init(&nb->list);
	spin_unlock(&gmap_notifier_lock);
}
EXPORT_SYMBOL_GPL(gmap_unregister_ipte_notifier);

/**
 * gmap_ipte_notify - mark a range of ptes for invalidation notification
 * @gmap: pointer to guest mapping meta data structure
 * @address: virtual address in the guest address space
 * @len: size of area
 *
 * Returns 0 if for each page in the given range a gmap mapping exists and
 * the invalidation notification could be set. If the gmap mapping is missing
 * for one or more pages -EFAULT is returned. If no memory could be allocated
 * -ENOMEM is returned. This function establishes missing page table entries.
 */
int gmap_ipte_notify(struct gmap *gmap, unsigned long start, unsigned long len)
{
	unsigned long addr;
	spinlock_t *ptl;
	pte_t *ptep, entry;
	pgste_t pgste;
	int rc = 0;

	if ((start & ~PAGE_MASK) || (len & ~PAGE_MASK))
		return -EINVAL;
	down_read(&gmap->mm->mmap_sem);
	while (len) {
		/* Convert gmap address and connect the page tables */
		addr = __gmap_fault(start, gmap);
		if (IS_ERR_VALUE(addr)) {
			rc = addr;
			break;
		}
		/* Get the page mapped */
		if (get_user_pages(current, gmap->mm, addr, 1, 1, 0,
				   NULL, NULL) != 1) {
			rc = -EFAULT;
			break;
		}
		/* Walk the process page table, lock and get pte pointer */
		ptep = get_locked_pte(gmap->mm, addr, &ptl);
		if (unlikely(!ptep))
			continue;
		/* Set notification bit in the pgste of the pte */
		entry = *ptep;
		if ((pte_val(entry) & (_PAGE_INVALID | _PAGE_RO)) == 0) {
			pgste = pgste_get_lock(ptep);
			pgste_val(pgste) |= RCP_IN_BIT;
			pgste_set_unlock(ptep, pgste);
			start += PAGE_SIZE;
			len -= PAGE_SIZE;
		}
		spin_unlock(ptl);
	}
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_ipte_notify);

/**
 * gmap_do_ipte_notify - call all invalidation callbacks for a specific pte.
 * @mm: pointer to the process mm_struct
 * @addr: virtual address in the process address space
 * @pte: pointer to the page table entry
 *
 * This function is assumed to be called with the page table lock held
 * for the pte to notify.
 */
void gmap_do_ipte_notify(struct mm_struct *mm, unsigned long addr, pte_t *pte)
{
	unsigned long segment_offset;
	struct gmap_notifier *nb;
	struct gmap_pgtable *mp;
	struct gmap_rmap *rmap;
	struct page *page;

	segment_offset = ((unsigned long) pte) & (255 * sizeof(pte_t));
	segment_offset = segment_offset * (4096 / sizeof(pte_t));
	page = pfn_to_page(__pa(pte) >> PAGE_SHIFT);
	mp = (struct gmap_pgtable *) page->index;
	spin_lock(&gmap_notifier_lock);
	list_for_each_entry(rmap, &mp->mapper, list) {
		list_for_each_entry(nb, &gmap_notifier_list, list)
			nb->notifier_call(rmap->gmap,
					  rmap->vmaddr + segment_offset);
	}
	spin_unlock(&gmap_notifier_lock);
}

static inline unsigned long *page_table_alloc_pgste(struct mm_struct *mm,
						    unsigned long vmaddr)
{
	struct page *page;
	unsigned long *table;
	struct gmap_pgtable *mp;

	page = alloc_page(GFP_KERNEL|__GFP_REPEAT);
	if (!page)
		return NULL;
	mp = kmalloc(sizeof(*mp), GFP_KERNEL|__GFP_REPEAT);
	if (!mp) {
		__free_page(page);
		return NULL;
	}
	pgtable_page_ctor(page);
	mp->vmaddr = vmaddr & PMD_MASK;
	INIT_LIST_HEAD(&mp->mapper);
	page->index = (unsigned long) mp;
	atomic_set(&page->_mapcount, 3);
	table = (unsigned long *) page_to_phys(page);
	clear_table(table, _PAGE_TYPE_EMPTY, PAGE_SIZE/2);
	clear_table(table + PTRS_PER_PTE, 0, PAGE_SIZE/2);
	return table;
}

static inline void page_table_free_pgste(unsigned long *table)
{
	struct page *page;
	struct gmap_pgtable *mp;

	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	mp = (struct gmap_pgtable *) page->index;
	BUG_ON(!list_empty(&mp->mapper));
	pgtable_page_dtor(page);
	atomic_set(&page->_mapcount, -1);
	kfree(mp);
	__free_page(page);
}

#else /* CONFIG_PGSTE */

static inline unsigned long *page_table_alloc_pgste(struct mm_struct *mm,
						    unsigned long vmaddr)
{
	return NULL;
}

static inline void page_table_free_pgste(unsigned long *table)
{
}

static inline void gmap_disconnect_pgtable(struct mm_struct *mm,
					   unsigned long *table)
{
}

#endif /* CONFIG_PGSTE */

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
unsigned long *page_table_alloc(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *uninitialized_var(table);
	struct page *uninitialized_var(page);
	unsigned int mask, bit;

	if (mm_has_pgste(mm))
		return page_table_alloc_pgste(mm, vmaddr);
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

	if (mm_has_pgste(mm)) {
		gmap_disconnect_pgtable(mm, table);
		return page_table_free_pgste(table);
	}
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

static void __page_table_free_rcu(void *table, unsigned bit)
{
	struct page *page;

	if (bit == FRAG_MASK)
		return page_table_free_pgste(table);
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
	if (mm_has_pgste(mm)) {
		gmap_disconnect_pgtable(mm, table);
		table = (unsigned long *) (__pa(table) | FRAG_MASK);
		tlb_remove_table(tlb, table);
		return;
	}
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
	const unsigned long mask = (FRAG_MASK << 4) | FRAG_MASK;
	void *table = (void *)((unsigned long) _table & ~mask);
	unsigned type = (unsigned long) _table & mask;

	if (type)
		__page_table_free_rcu(table, type);
	else
		free_pages((unsigned long) table, ALLOC_ORDER);
}

static void tlb_remove_table_smp_sync(void *arg)
{
	/* Simply deliver the interrupt */
}

static void tlb_remove_table_one(void *table)
{
	/*
	 * This isn't an RCU grace period and hence the page-tables cannot be
	 * assumed to be actually RCU-freed.
	 *
	 * It is however sufficient for software page-table walkers that rely
	 * on IRQ disabling. See the comment near struct mmu_table_batch.
	 */
	smp_call_function(tlb_remove_table_smp_sync, NULL, 1);
	__tlb_remove_table(table);
}

static void tlb_remove_table_rcu(struct rcu_head *head)
{
	struct mmu_table_batch *batch;
	int i;

	batch = container_of(head, struct mmu_table_batch, rcu);

	for (i = 0; i < batch->nr; i++)
		__tlb_remove_table(batch->tables[i]);

	free_page((unsigned long)batch);
}

void tlb_table_flush(struct mmu_gather *tlb)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch) {
		__tlb_flush_mm(tlb->mm);
		call_rcu_sched(&(*batch)->rcu, tlb_remove_table_rcu);
		*batch = NULL;
	}
}

void tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	struct mmu_table_batch **batch = &tlb->batch;

	if (*batch == NULL) {
		*batch = (struct mmu_table_batch *)
			__get_free_page(GFP_NOWAIT | __GFP_NOWARN);
		if (*batch == NULL) {
			__tlb_flush_mm(tlb->mm);
			tlb_remove_table_one(table);
			return;
		}
		(*batch)->nr = 0;
	}
	(*batch)->tables[(*batch)->nr++] = table;
	if ((*batch)->nr == MAX_TABLE_BATCH)
		tlb_table_flush(tlb);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void thp_split_vma(struct vm_area_struct *vma)
{
	unsigned long addr;
	struct page *page;

	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
		page = follow_page(vma, addr, FOLL_SPLIT);
	}
}

void thp_split_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma = mm->mmap;

	while (vma != NULL) {
		thp_split_vma(vma);
		vma->vm_flags &= ~VM_HUGEPAGE;
		vma->vm_flags |= VM_NOHUGEPAGE;
		vma = vma->vm_next;
	}
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * switch on pgstes for its userspace process (for kvm)
 */
int s390_enable_sie(void)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm, *old_mm;

	/* Do we have switched amode? If no, we cannot do sie */
	if (s390_user_mode == HOME_SPACE_MODE)
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
	/* make sure that both mms have a correct rss state */
	sync_mm_rss(tsk->mm);
	mm = dup_mm(tsk);
	tsk->mm->context.alloc_pgste = 0;
	if (!mm)
		return -ENOMEM;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* split thp mappings and disable thp for future mappings */
	thp_split_mm(mm);
	mm->def_flags |= VM_NOHUGEPAGE;
#endif

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

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
int pmdp_clear_flush_young(struct vm_area_struct *vma, unsigned long address,
			   pmd_t *pmdp)
{
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	/* No need to flush TLB
	 * On s390 reference bits are in storage key and never in TLB */
	return pmdp_test_and_clear_young(vma, address, pmdp);
}

int pmdp_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pmd_t *pmdp,
			  pmd_t entry, int dirty)
{
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	if (pmd_same(*pmdp, entry))
		return 0;
	pmdp_invalidate(vma, address, pmdp);
	set_pmd_at(vma->vm_mm, address, pmdp, entry);
	return 1;
}

static void pmdp_splitting_flush_sync(void *arg)
{
	/* Simply deliver the interrupt */
}

void pmdp_splitting_flush(struct vm_area_struct *vma, unsigned long address,
			  pmd_t *pmdp)
{
	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	if (!test_and_set_bit(_SEGMENT_ENTRY_SPLIT_BIT,
			      (unsigned long *) pmdp)) {
		/* need to serialize against gup-fast (IRQ disabled) */
		smp_call_function(pmdp_splitting_flush_sync, NULL, 1);
	}
}

void pgtable_trans_huge_deposit(struct mm_struct *mm, pgtable_t pgtable)
{
	struct list_head *lh = (struct list_head *) pgtable;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	if (!mm->pmd_huge_pte)
		INIT_LIST_HEAD(lh);
	else
		list_add(lh, (struct list_head *) mm->pmd_huge_pte);
	mm->pmd_huge_pte = pgtable;
}

pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm)
{
	struct list_head *lh;
	pgtable_t pgtable;
	pte_t *ptep;

	assert_spin_locked(&mm->page_table_lock);

	/* FIFO */
	pgtable = mm->pmd_huge_pte;
	lh = (struct list_head *) pgtable;
	if (list_empty(lh))
		mm->pmd_huge_pte = NULL;
	else {
		mm->pmd_huge_pte = (pgtable_t) lh->next;
		list_del(lh);
	}
	ptep = (pte_t *) pgtable;
	pte_val(*ptep) = _PAGE_TYPE_EMPTY;
	ptep++;
	pte_val(*ptep) = _PAGE_TYPE_EMPTY;
	return pgtable;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
