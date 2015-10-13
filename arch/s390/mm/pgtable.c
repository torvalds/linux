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
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/sysctl.h>
#include <linux/ksm.h>
#include <linux/mman.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

unsigned long *crst_table_alloc(struct mm_struct *mm)
{
	struct page *page = alloc_pages(GFP_KERNEL, 2);

	if (!page)
		return NULL;
	return (unsigned long *) page_to_phys(page);
}

void crst_table_free(struct mm_struct *mm, unsigned long *table)
{
	free_pages((unsigned long) table, 2);
}

static void __crst_table_upgrade(void *arg)
{
	struct mm_struct *mm = arg;

	if (current->active_mm == mm) {
		clear_user_asce();
		set_user_asce(mm);
	}
	__tlb_flush_local();
}

int crst_table_upgrade(struct mm_struct *mm, unsigned long limit)
{
	unsigned long *table, *pgd;
	unsigned long entry;
	int flush;

	BUG_ON(limit > (1UL << 53));
	flush = 0;
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
		flush = 1;
	}
	spin_unlock_bh(&mm->page_table_lock);
	if (table)
		crst_table_free(mm, table);
	if (mm->context.asce_limit < limit)
		goto repeat;
	if (flush)
		on_each_cpu(__crst_table_upgrade, mm, 0);
	return 0;
}

void crst_table_downgrade(struct mm_struct *mm, unsigned long limit)
{
	pgd_t *pgd;

	if (current->active_mm == mm) {
		clear_user_asce();
		__tlb_flush_mm(mm);
	}
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
	if (current->active_mm == mm)
		set_user_asce(mm);
}

#ifdef CONFIG_PGSTE

/**
 * gmap_alloc - allocate a guest address space
 * @mm: pointer to the parent mm_struct
 * @limit: maximum size of the gmap address space
 *
 * Returns a guest address space structure.
 */
struct gmap *gmap_alloc(struct mm_struct *mm, unsigned long limit)
{
	struct gmap *gmap;
	struct page *page;
	unsigned long *table;
	unsigned long etype, atype;

	if (limit < (1UL << 31)) {
		limit = (1UL << 31) - 1;
		atype = _ASCE_TYPE_SEGMENT;
		etype = _SEGMENT_ENTRY_EMPTY;
	} else if (limit < (1UL << 42)) {
		limit = (1UL << 42) - 1;
		atype = _ASCE_TYPE_REGION3;
		etype = _REGION3_ENTRY_EMPTY;
	} else if (limit < (1UL << 53)) {
		limit = (1UL << 53) - 1;
		atype = _ASCE_TYPE_REGION2;
		etype = _REGION2_ENTRY_EMPTY;
	} else {
		limit = -1UL;
		atype = _ASCE_TYPE_REGION1;
		etype = _REGION1_ENTRY_EMPTY;
	}
	gmap = kzalloc(sizeof(struct gmap), GFP_KERNEL);
	if (!gmap)
		goto out;
	INIT_LIST_HEAD(&gmap->crst_list);
	INIT_RADIX_TREE(&gmap->guest_to_host, GFP_KERNEL);
	INIT_RADIX_TREE(&gmap->host_to_guest, GFP_ATOMIC);
	spin_lock_init(&gmap->guest_table_lock);
	gmap->mm = mm;
	page = alloc_pages(GFP_KERNEL, 2);
	if (!page)
		goto out_free;
	page->index = 0;
	list_add(&page->lru, &gmap->crst_list);
	table = (unsigned long *) page_to_phys(page);
	crst_table_init(table, etype);
	gmap->table = table;
	gmap->asce = atype | _ASCE_TABLE_LENGTH |
		_ASCE_USER_BITS | __pa(table);
	gmap->asce_end = limit;
	down_write(&mm->mmap_sem);
	list_add(&gmap->list, &mm->context.gmap_list);
	up_write(&mm->mmap_sem);
	return gmap;

out_free:
	kfree(gmap);
out:
	return NULL;
}
EXPORT_SYMBOL_GPL(gmap_alloc);

static void gmap_flush_tlb(struct gmap *gmap)
{
	if (MACHINE_HAS_IDTE)
		__tlb_flush_asce(gmap->mm, gmap->asce);
	else
		__tlb_flush_global();
}

static void gmap_radix_tree_free(struct radix_tree_root *root)
{
	struct radix_tree_iter iter;
	unsigned long indices[16];
	unsigned long index;
	void **slot;
	int i, nr;

	/* A radix tree is freed by deleting all of its entries */
	index = 0;
	do {
		nr = 0;
		radix_tree_for_each_slot(slot, root, &iter, index) {
			indices[nr] = iter.index;
			if (++nr == 16)
				break;
		}
		for (i = 0; i < nr; i++) {
			index = indices[i];
			radix_tree_delete(root, index);
		}
	} while (nr > 0);
}

/**
 * gmap_free - free a guest address space
 * @gmap: pointer to the guest address space structure
 */
void gmap_free(struct gmap *gmap)
{
	struct page *page, *next;

	/* Flush tlb. */
	if (MACHINE_HAS_IDTE)
		__tlb_flush_asce(gmap->mm, gmap->asce);
	else
		__tlb_flush_global();

	/* Free all segment & region tables. */
	list_for_each_entry_safe(page, next, &gmap->crst_list, lru)
		__free_pages(page, 2);
	gmap_radix_tree_free(&gmap->guest_to_host);
	gmap_radix_tree_free(&gmap->host_to_guest);
	down_write(&gmap->mm->mmap_sem);
	list_del(&gmap->list);
	up_write(&gmap->mm->mmap_sem);
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
static int gmap_alloc_table(struct gmap *gmap, unsigned long *table,
			    unsigned long init, unsigned long gaddr)
{
	struct page *page;
	unsigned long *new;

	/* since we dont free the gmap table until gmap_free we can unlock */
	page = alloc_pages(GFP_KERNEL, 2);
	if (!page)
		return -ENOMEM;
	new = (unsigned long *) page_to_phys(page);
	crst_table_init(new, init);
	spin_lock(&gmap->mm->page_table_lock);
	if (*table & _REGION_ENTRY_INVALID) {
		list_add(&page->lru, &gmap->crst_list);
		*table = (unsigned long) new | _REGION_ENTRY_LENGTH |
			(*table & _REGION_ENTRY_TYPE_MASK);
		page->index = gaddr;
		page = NULL;
	}
	spin_unlock(&gmap->mm->page_table_lock);
	if (page)
		__free_pages(page, 2);
	return 0;
}

/**
 * __gmap_segment_gaddr - find virtual address from segment pointer
 * @entry: pointer to a segment table entry in the guest address space
 *
 * Returns the virtual address in the guest address space for the segment
 */
static unsigned long __gmap_segment_gaddr(unsigned long *entry)
{
	struct page *page;
	unsigned long offset, mask;

	offset = (unsigned long) entry / sizeof(unsigned long);
	offset = (offset & (PTRS_PER_PMD - 1)) * PMD_SIZE;
	mask = ~(PTRS_PER_PMD * sizeof(pmd_t) - 1);
	page = virt_to_page((void *)((unsigned long) entry & mask));
	return page->index + offset;
}

/**
 * __gmap_unlink_by_vmaddr - unlink a single segment via a host address
 * @gmap: pointer to the guest address space structure
 * @vmaddr: address in the host process address space
 *
 * Returns 1 if a TLB flush is required
 */
static int __gmap_unlink_by_vmaddr(struct gmap *gmap, unsigned long vmaddr)
{
	unsigned long *entry;
	int flush = 0;

	spin_lock(&gmap->guest_table_lock);
	entry = radix_tree_delete(&gmap->host_to_guest, vmaddr >> PMD_SHIFT);
	if (entry) {
		flush = (*entry != _SEGMENT_ENTRY_INVALID);
		*entry = _SEGMENT_ENTRY_INVALID;
	}
	spin_unlock(&gmap->guest_table_lock);
	return flush;
}

/**
 * __gmap_unmap_by_gaddr - unmap a single segment via a guest address
 * @gmap: pointer to the guest address space structure
 * @gaddr: address in the guest address space
 *
 * Returns 1 if a TLB flush is required
 */
static int __gmap_unmap_by_gaddr(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long vmaddr;

	vmaddr = (unsigned long) radix_tree_delete(&gmap->guest_to_host,
						   gaddr >> PMD_SHIFT);
	return vmaddr ? __gmap_unlink_by_vmaddr(gmap, vmaddr) : 0;
}

/**
 * gmap_unmap_segment - unmap segment from the guest address space
 * @gmap: pointer to the guest address space structure
 * @to: address in the guest address space
 * @len: length of the memory area to unmap
 *
 * Returns 0 if the unmap succeeded, -EINVAL if not.
 */
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len)
{
	unsigned long off;
	int flush;

	if ((to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || to + len < to)
		return -EINVAL;

	flush = 0;
	down_write(&gmap->mm->mmap_sem);
	for (off = 0; off < len; off += PMD_SIZE)
		flush |= __gmap_unmap_by_gaddr(gmap, to + off);
	up_write(&gmap->mm->mmap_sem);
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
 * @len: length of the memory area to map
 *
 * Returns 0 if the mmap succeeded, -EINVAL or -ENOMEM if not.
 */
int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len)
{
	unsigned long off;
	int flush;

	if ((from | to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || from + len < from || to + len < to ||
	    from + len > TASK_MAX_SIZE || to + len > gmap->asce_end)
		return -EINVAL;

	flush = 0;
	down_write(&gmap->mm->mmap_sem);
	for (off = 0; off < len; off += PMD_SIZE) {
		/* Remove old translation */
		flush |= __gmap_unmap_by_gaddr(gmap, to + off);
		/* Store new translation */
		if (radix_tree_insert(&gmap->guest_to_host,
				      (to + off) >> PMD_SHIFT,
				      (void *) from + off))
			break;
	}
	up_write(&gmap->mm->mmap_sem);
	if (flush)
		gmap_flush_tlb(gmap);
	if (off >= len)
		return 0;
	gmap_unmap_segment(gmap, to, len);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(gmap_map_segment);

/**
 * __gmap_translate - translate a guest address to a user space address
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: guest address
 *
 * Returns user space address which corresponds to the guest address or
 * -EFAULT if no such mapping exists.
 * This function does not establish potentially missing page table entries.
 * The mmap_sem of the mm that belongs to the address space must be held
 * when this function gets called.
 */
unsigned long __gmap_translate(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long vmaddr;

	vmaddr = (unsigned long)
		radix_tree_lookup(&gmap->guest_to_host, gaddr >> PMD_SHIFT);
	return vmaddr ? (vmaddr | (gaddr & ~PMD_MASK)) : -EFAULT;
}
EXPORT_SYMBOL_GPL(__gmap_translate);

/**
 * gmap_translate - translate a guest address to a user space address
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: guest address
 *
 * Returns user space address which corresponds to the guest address or
 * -EFAULT if no such mapping exists.
 * This function does not establish potentially missing page table entries.
 */
unsigned long gmap_translate(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long rc;

	down_read(&gmap->mm->mmap_sem);
	rc = __gmap_translate(gmap, gaddr);
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_translate);

/**
 * gmap_unlink - disconnect a page table from the gmap shadow tables
 * @gmap: pointer to guest mapping meta data structure
 * @table: pointer to the host page table
 * @vmaddr: vm address associated with the host page table
 */
static void gmap_unlink(struct mm_struct *mm, unsigned long *table,
			unsigned long vmaddr)
{
	struct gmap *gmap;
	int flush;

	list_for_each_entry(gmap, &mm->context.gmap_list, list) {
		flush = __gmap_unlink_by_vmaddr(gmap, vmaddr);
		if (flush)
			gmap_flush_tlb(gmap);
	}
}

/**
 * gmap_link - set up shadow page tables to connect a host to a guest address
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: guest address
 * @vmaddr: vm address
 *
 * Returns 0 on success, -ENOMEM for out of memory conditions, and -EFAULT
 * if the vm address is already mapped to a different guest segment.
 * The mmap_sem of the mm that belongs to the address space must be held
 * when this function gets called.
 */
int __gmap_link(struct gmap *gmap, unsigned long gaddr, unsigned long vmaddr)
{
	struct mm_struct *mm;
	unsigned long *table;
	spinlock_t *ptl;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	int rc;

	/* Create higher level tables in the gmap page table */
	table = gmap->table;
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION1) {
		table += (gaddr >> 53) & 0x7ff;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _REGION2_ENTRY_EMPTY,
				     gaddr & 0xffe0000000000000UL))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION2) {
		table += (gaddr >> 42) & 0x7ff;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _REGION3_ENTRY_EMPTY,
				     gaddr & 0xfffffc0000000000UL))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION3) {
		table += (gaddr >> 31) & 0x7ff;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _SEGMENT_ENTRY_EMPTY,
				     gaddr & 0xffffffff80000000UL))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	table += (gaddr >> 20) & 0x7ff;
	/* Walk the parent mm page table */
	mm = gmap->mm;
	pgd = pgd_offset(mm, vmaddr);
	VM_BUG_ON(pgd_none(*pgd));
	pud = pud_offset(pgd, vmaddr);
	VM_BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, vmaddr);
	VM_BUG_ON(pmd_none(*pmd));
	/* large pmds cannot yet be handled */
	if (pmd_large(*pmd))
		return -EFAULT;
	/* Link gmap segment table entry location to page table. */
	rc = radix_tree_preload(GFP_KERNEL);
	if (rc)
		return rc;
	ptl = pmd_lock(mm, pmd);
	spin_lock(&gmap->guest_table_lock);
	if (*table == _SEGMENT_ENTRY_INVALID) {
		rc = radix_tree_insert(&gmap->host_to_guest,
				       vmaddr >> PMD_SHIFT, table);
		if (!rc)
			*table = pmd_val(*pmd);
	} else
		rc = 0;
	spin_unlock(&gmap->guest_table_lock);
	spin_unlock(ptl);
	radix_tree_preload_end();
	return rc;
}

/**
 * gmap_fault - resolve a fault on a guest address
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: guest address
 * @fault_flags: flags to pass down to handle_mm_fault()
 *
 * Returns 0 on success, -ENOMEM for out of memory conditions, and -EFAULT
 * if the vm address is already mapped to a different guest segment.
 */
int gmap_fault(struct gmap *gmap, unsigned long gaddr,
	       unsigned int fault_flags)
{
	unsigned long vmaddr;
	int rc;

	down_read(&gmap->mm->mmap_sem);
	vmaddr = __gmap_translate(gmap, gaddr);
	if (IS_ERR_VALUE(vmaddr)) {
		rc = vmaddr;
		goto out_up;
	}
	if (fixup_user_fault(current, gmap->mm, vmaddr, fault_flags)) {
		rc = -EFAULT;
		goto out_up;
	}
	rc = __gmap_link(gmap, gaddr, vmaddr);
out_up:
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_fault);

static void gmap_zap_swap_entry(swp_entry_t entry, struct mm_struct *mm)
{
	if (!non_swap_entry(entry))
		dec_mm_counter(mm, MM_SWAPENTS);
	else if (is_migration_entry(entry)) {
		struct page *page = migration_entry_to_page(entry);

		if (PageAnon(page))
			dec_mm_counter(mm, MM_ANONPAGES);
		else
			dec_mm_counter(mm, MM_FILEPAGES);
	}
	free_swap_and_cache(entry);
}

/*
 * this function is assumed to be called with mmap_sem held
 */
void __gmap_zap(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long vmaddr, ptev, pgstev;
	pte_t *ptep, pte;
	spinlock_t *ptl;
	pgste_t pgste;

	/* Find the vm address for the guest address */
	vmaddr = (unsigned long) radix_tree_lookup(&gmap->guest_to_host,
						   gaddr >> PMD_SHIFT);
	if (!vmaddr)
		return;
	vmaddr |= gaddr & ~PMD_MASK;
	/* Get pointer to the page table entry */
	ptep = get_locked_pte(gmap->mm, vmaddr, &ptl);
	if (unlikely(!ptep))
		return;
	pte = *ptep;
	if (!pte_swap(pte))
		goto out_pte;
	/* Zap unused and logically-zero pages */
	pgste = pgste_get_lock(ptep);
	pgstev = pgste_val(pgste);
	ptev = pte_val(pte);
	if (((pgstev & _PGSTE_GPS_USAGE_MASK) == _PGSTE_GPS_USAGE_UNUSED) ||
	    ((pgstev & _PGSTE_GPS_ZERO) && (ptev & _PAGE_INVALID))) {
		gmap_zap_swap_entry(pte_to_swp_entry(pte), gmap->mm);
		pte_clear(gmap->mm, vmaddr, ptep);
	}
	pgste_set_unlock(ptep, pgste);
out_pte:
	pte_unmap_unlock(ptep, ptl);
}
EXPORT_SYMBOL_GPL(__gmap_zap);

void gmap_discard(struct gmap *gmap, unsigned long from, unsigned long to)
{
	unsigned long gaddr, vmaddr, size;
	struct vm_area_struct *vma;

	down_read(&gmap->mm->mmap_sem);
	for (gaddr = from; gaddr < to;
	     gaddr = (gaddr + PMD_SIZE) & PMD_MASK) {
		/* Find the vm address for the guest address */
		vmaddr = (unsigned long)
			radix_tree_lookup(&gmap->guest_to_host,
					  gaddr >> PMD_SHIFT);
		if (!vmaddr)
			continue;
		vmaddr |= gaddr & ~PMD_MASK;
		/* Find vma in the parent mm */
		vma = find_vma(gmap->mm, vmaddr);
		size = min(to - gaddr, PMD_SIZE - (gaddr & ~PMD_MASK));
		zap_page_range(vma, vmaddr, size, NULL);
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
 * @gaddr: virtual address in the guest address space
 * @len: size of area
 *
 * Returns 0 if for each page in the given range a gmap mapping exists and
 * the invalidation notification could be set. If the gmap mapping is missing
 * for one or more pages -EFAULT is returned. If no memory could be allocated
 * -ENOMEM is returned. This function establishes missing page table entries.
 */
int gmap_ipte_notify(struct gmap *gmap, unsigned long gaddr, unsigned long len)
{
	unsigned long addr;
	spinlock_t *ptl;
	pte_t *ptep, entry;
	pgste_t pgste;
	int rc = 0;

	if ((gaddr & ~PAGE_MASK) || (len & ~PAGE_MASK))
		return -EINVAL;
	down_read(&gmap->mm->mmap_sem);
	while (len) {
		/* Convert gmap address and connect the page tables */
		addr = __gmap_translate(gmap, gaddr);
		if (IS_ERR_VALUE(addr)) {
			rc = addr;
			break;
		}
		/* Get the page mapped */
		if (fixup_user_fault(current, gmap->mm, addr, FAULT_FLAG_WRITE)) {
			rc = -EFAULT;
			break;
		}
		rc = __gmap_link(gmap, gaddr, addr);
		if (rc)
			break;
		/* Walk the process page table, lock and get pte pointer */
		ptep = get_locked_pte(gmap->mm, addr, &ptl);
		VM_BUG_ON(!ptep);
		/* Set notification bit in the pgste of the pte */
		entry = *ptep;
		if ((pte_val(entry) & (_PAGE_INVALID | _PAGE_PROTECT)) == 0) {
			pgste = pgste_get_lock(ptep);
			pgste_val(pgste) |= PGSTE_IN_BIT;
			pgste_set_unlock(ptep, pgste);
			gaddr += PAGE_SIZE;
			len -= PAGE_SIZE;
		}
		pte_unmap_unlock(ptep, ptl);
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
void gmap_do_ipte_notify(struct mm_struct *mm, unsigned long vmaddr, pte_t *pte)
{
	unsigned long offset, gaddr;
	unsigned long *table;
	struct gmap_notifier *nb;
	struct gmap *gmap;

	offset = ((unsigned long) pte) & (255 * sizeof(pte_t));
	offset = offset * (4096 / sizeof(pte_t));
	spin_lock(&gmap_notifier_lock);
	list_for_each_entry(gmap, &mm->context.gmap_list, list) {
		table = radix_tree_lookup(&gmap->host_to_guest,
					  vmaddr >> PMD_SHIFT);
		if (!table)
			continue;
		gaddr = __gmap_segment_gaddr(table) + offset;
		list_for_each_entry(nb, &gmap_notifier_list, list)
			nb->notifier_call(gmap, gaddr);
	}
	spin_unlock(&gmap_notifier_lock);
}
EXPORT_SYMBOL_GPL(gmap_do_ipte_notify);

int set_guest_storage_key(struct mm_struct *mm, unsigned long addr,
			  unsigned long key, bool nq)
{
	spinlock_t *ptl;
	pgste_t old, new;
	pte_t *ptep;

	down_read(&mm->mmap_sem);
retry:
	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep)) {
		up_read(&mm->mmap_sem);
		return -EFAULT;
	}
	if (!(pte_val(*ptep) & _PAGE_INVALID) &&
	     (pte_val(*ptep) & _PAGE_PROTECT)) {
		pte_unmap_unlock(ptep, ptl);
		if (fixup_user_fault(current, mm, addr, FAULT_FLAG_WRITE)) {
			up_read(&mm->mmap_sem);
			return -EFAULT;
		}
		goto retry;
	}

	new = old = pgste_get_lock(ptep);
	pgste_val(new) &= ~(PGSTE_GR_BIT | PGSTE_GC_BIT |
			    PGSTE_ACC_BITS | PGSTE_FP_BIT);
	pgste_val(new) |= (key & (_PAGE_CHANGED | _PAGE_REFERENCED)) << 48;
	pgste_val(new) |= (key & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
		unsigned long address, bits, skey;

		address = pte_val(*ptep) & PAGE_MASK;
		skey = (unsigned long) page_get_storage_key(address);
		bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
		skey = key & (_PAGE_ACC_BITS | _PAGE_FP_BIT);
		/* Set storage key ACC and FP */
		page_set_storage_key(address, skey, !nq);
		/* Merge host changed & referenced into pgste  */
		pgste_val(new) |= bits << 52;
	}
	/* changing the guest storage key is considered a change of the page */
	if ((pgste_val(new) ^ pgste_val(old)) &
	    (PGSTE_ACC_BITS | PGSTE_FP_BIT | PGSTE_GR_BIT | PGSTE_GC_BIT))
		pgste_val(new) |= PGSTE_UC_BIT;

	pgste_set_unlock(ptep, new);
	pte_unmap_unlock(ptep, ptl);
	up_read(&mm->mmap_sem);
	return 0;
}
EXPORT_SYMBOL(set_guest_storage_key);

unsigned long get_guest_storage_key(struct mm_struct *mm, unsigned long addr)
{
	spinlock_t *ptl;
	pgste_t pgste;
	pte_t *ptep;
	uint64_t physaddr;
	unsigned long key = 0;

	down_read(&mm->mmap_sem);
	ptep = get_locked_pte(mm, addr, &ptl);
	if (unlikely(!ptep)) {
		up_read(&mm->mmap_sem);
		return -EFAULT;
	}
	pgste = pgste_get_lock(ptep);

	if (pte_val(*ptep) & _PAGE_INVALID) {
		key |= (pgste_val(pgste) & PGSTE_ACC_BITS) >> 56;
		key |= (pgste_val(pgste) & PGSTE_FP_BIT) >> 56;
		key |= (pgste_val(pgste) & PGSTE_GR_BIT) >> 48;
		key |= (pgste_val(pgste) & PGSTE_GC_BIT) >> 48;
	} else {
		physaddr = pte_val(*ptep) & PAGE_MASK;
		key = page_get_storage_key(physaddr);

		/* Reflect guest's logical view, not physical */
		if (pgste_val(pgste) & PGSTE_GR_BIT)
			key |= _PAGE_REFERENCED;
		if (pgste_val(pgste) & PGSTE_GC_BIT)
			key |= _PAGE_CHANGED;
	}

	pgste_set_unlock(ptep, pgste);
	pte_unmap_unlock(ptep, ptl);
	up_read(&mm->mmap_sem);
	return key;
}
EXPORT_SYMBOL(get_guest_storage_key);

static int page_table_allocate_pgste_min = 0;
static int page_table_allocate_pgste_max = 1;
int page_table_allocate_pgste = 0;
EXPORT_SYMBOL(page_table_allocate_pgste);

static struct ctl_table page_table_sysctl[] = {
	{
		.procname	= "allocate_pgste",
		.data		= &page_table_allocate_pgste,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO | S_IWUSR,
		.proc_handler	= proc_dointvec,
		.extra1		= &page_table_allocate_pgste_min,
		.extra2		= &page_table_allocate_pgste_max,
	},
	{ }
};

static struct ctl_table page_table_sysctl_dir[] = {
	{
		.procname	= "vm",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= page_table_sysctl,
	},
	{ }
};

static int __init page_table_register_sysctl(void)
{
	return register_sysctl_table(page_table_sysctl_dir) ? 0 : -ENOMEM;
}
__initcall(page_table_register_sysctl);

#else /* CONFIG_PGSTE */

static inline void gmap_unlink(struct mm_struct *mm, unsigned long *table,
			unsigned long vmaddr)
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
unsigned long *page_table_alloc(struct mm_struct *mm)
{
	unsigned long *table;
	struct page *page;
	unsigned int mask, bit;

	/* Try to get a fragment of a 4K page as a 2K page table */
	if (!mm_alloc_pgste(mm)) {
		table = NULL;
		spin_lock_bh(&mm->context.list_lock);
		if (!list_empty(&mm->context.pgtable_list)) {
			page = list_first_entry(&mm->context.pgtable_list,
						struct page, lru);
			mask = atomic_read(&page->_mapcount);
			mask = (mask | (mask >> 4)) & 3;
			if (mask != 3) {
				table = (unsigned long *) page_to_phys(page);
				bit = mask & 1;		/* =1 -> second 2K */
				if (bit)
					table += PTRS_PER_PTE;
				atomic_xor_bits(&page->_mapcount, 1U << bit);
				list_del(&page->lru);
			}
		}
		spin_unlock_bh(&mm->context.list_lock);
		if (table)
			return table;
	}
	/* Allocate a fresh page */
	page = alloc_page(GFP_KERNEL|__GFP_REPEAT);
	if (!page)
		return NULL;
	if (!pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	/* Initialize page table */
	table = (unsigned long *) page_to_phys(page);
	if (mm_alloc_pgste(mm)) {
		/* Return 4K page table with PGSTEs */
		atomic_set(&page->_mapcount, 3);
		clear_table(table, _PAGE_INVALID, PAGE_SIZE/2);
		clear_table(table + PTRS_PER_PTE, 0, PAGE_SIZE/2);
	} else {
		/* Return the first 2K fragment of the page */
		atomic_set(&page->_mapcount, 1);
		clear_table(table, _PAGE_INVALID, PAGE_SIZE);
		spin_lock_bh(&mm->context.list_lock);
		list_add(&page->lru, &mm->context.pgtable_list);
		spin_unlock_bh(&mm->context.list_lock);
	}
	return table;
}

void page_table_free(struct mm_struct *mm, unsigned long *table)
{
	struct page *page;
	unsigned int bit, mask;

	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	if (!mm_alloc_pgste(mm)) {
		/* Free 2K page table fragment of a 4K page */
		bit = (__pa(table) & ~PAGE_MASK)/(PTRS_PER_PTE*sizeof(pte_t));
		spin_lock_bh(&mm->context.list_lock);
		mask = atomic_xor_bits(&page->_mapcount, 1U << bit);
		if (mask & 3)
			list_add(&page->lru, &mm->context.pgtable_list);
		else
			list_del(&page->lru);
		spin_unlock_bh(&mm->context.list_lock);
		if (mask != 0)
			return;
	}

	pgtable_page_dtor(page);
	atomic_set(&page->_mapcount, -1);
	__free_page(page);
}

void page_table_free_rcu(struct mmu_gather *tlb, unsigned long *table,
			 unsigned long vmaddr)
{
	struct mm_struct *mm;
	struct page *page;
	unsigned int bit, mask;

	mm = tlb->mm;
	page = pfn_to_page(__pa(table) >> PAGE_SHIFT);
	if (mm_alloc_pgste(mm)) {
		gmap_unlink(mm, table, vmaddr);
		table = (unsigned long *) (__pa(table) | 3);
		tlb_remove_table(tlb, table);
		return;
	}
	bit = (__pa(table) & ~PAGE_MASK) / (PTRS_PER_PTE*sizeof(pte_t));
	spin_lock_bh(&mm->context.list_lock);
	mask = atomic_xor_bits(&page->_mapcount, 0x11U << bit);
	if (mask & 3)
		list_add_tail(&page->lru, &mm->context.pgtable_list);
	else
		list_del(&page->lru);
	spin_unlock_bh(&mm->context.list_lock);
	table = (unsigned long *) (__pa(table) | (1U << bit));
	tlb_remove_table(tlb, table);
}

static void __tlb_remove_table(void *_table)
{
	unsigned int mask = (unsigned long) _table & 3;
	void *table = (void *)((unsigned long) _table ^ mask);
	struct page *page = pfn_to_page(__pa(table) >> PAGE_SHIFT);

	switch (mask) {
	case 0:		/* pmd or pud */
		free_pages((unsigned long) table, 2);
		break;
	case 1:		/* lower 2K of a 4K page table */
	case 2:		/* higher 2K of a 4K page table */
		if (atomic_xor_bits(&page->_mapcount, mask << 4) != 0)
			break;
		/* fallthrough */
	case 3:		/* 4K page table with pgstes */
		pgtable_page_dtor(page);
		atomic_set(&page->_mapcount, -1);
		__free_page(page);
		break;
	}
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
		call_rcu_sched(&(*batch)->rcu, tlb_remove_table_rcu);
		*batch = NULL;
	}
}

void tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	struct mmu_table_batch **batch = &tlb->batch;

	tlb->mm->context.flush_mm = 1;
	if (*batch == NULL) {
		*batch = (struct mmu_table_batch *)
			__get_free_page(GFP_NOWAIT | __GFP_NOWARN);
		if (*batch == NULL) {
			__tlb_flush_mm_lazy(tlb->mm);
			tlb_remove_table_one(table);
			return;
		}
		(*batch)->nr = 0;
	}
	(*batch)->tables[(*batch)->nr++] = table;
	if ((*batch)->nr == MAX_TABLE_BATCH)
		tlb_flush_mmu(tlb);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline void thp_split_vma(struct vm_area_struct *vma)
{
	unsigned long addr;

	for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE)
		follow_page(vma, addr, FOLL_SPLIT);
}

static inline void thp_split_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	for (vma = mm->mmap; vma != NULL; vma = vma->vm_next) {
		thp_split_vma(vma);
		vma->vm_flags &= ~VM_HUGEPAGE;
		vma->vm_flags |= VM_NOHUGEPAGE;
	}
	mm->def_flags |= VM_NOHUGEPAGE;
}
#else
static inline void thp_split_mm(struct mm_struct *mm)
{
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * switch on pgstes for its userspace process (for kvm)
 */
int s390_enable_sie(void)
{
	struct mm_struct *mm = current->mm;

	/* Do we have pgstes? if yes, we are done */
	if (mm_has_pgste(mm))
		return 0;
	/* Fail if the page tables are 2K */
	if (!mm_alloc_pgste(mm))
		return -EINVAL;
	down_write(&mm->mmap_sem);
	mm->context.has_pgste = 1;
	/* split thp mappings and disable thp for future mappings */
	thp_split_mm(mm);
	up_write(&mm->mmap_sem);
	return 0;
}
EXPORT_SYMBOL_GPL(s390_enable_sie);

/*
 * Enable storage key handling from now on and initialize the storage
 * keys with the default key.
 */
static int __s390_enable_skey(pte_t *pte, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	unsigned long ptev;
	pgste_t pgste;

	pgste = pgste_get_lock(pte);
	/*
	 * Remove all zero page mappings,
	 * after establishing a policy to forbid zero page mappings
	 * following faults for that page will get fresh anonymous pages
	 */
	if (is_zero_pfn(pte_pfn(*pte))) {
		ptep_flush_direct(walk->mm, addr, pte);
		pte_val(*pte) = _PAGE_INVALID;
	}
	/* Clear storage key */
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT |
			      PGSTE_GR_BIT | PGSTE_GC_BIT);
	ptev = pte_val(*pte);
	if (!(ptev & _PAGE_INVALID) && (ptev & _PAGE_WRITE))
		page_set_storage_key(ptev & PAGE_MASK, PAGE_DEFAULT_KEY, 1);
	pgste_set_unlock(pte, pgste);
	return 0;
}

int s390_enable_skey(void)
{
	struct mm_walk walk = { .pte_entry = __s390_enable_skey };
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc = 0;

	down_write(&mm->mmap_sem);
	if (mm_use_skey(mm))
		goto out_up;

	mm->context.use_skey = 1;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (ksm_madvise(vma, vma->vm_start, vma->vm_end,
				MADV_UNMERGEABLE, &vma->vm_flags)) {
			mm->context.use_skey = 0;
			rc = -ENOMEM;
			goto out_up;
		}
	}
	mm->def_flags &= ~VM_MERGEABLE;

	walk.mm = mm;
	walk_page_range(0, TASK_SIZE, &walk);

out_up:
	up_write(&mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(s390_enable_skey);

/*
 * Reset CMMA state, make all pages stable again.
 */
static int __s390_reset_cmma(pte_t *pte, unsigned long addr,
			     unsigned long next, struct mm_walk *walk)
{
	pgste_t pgste;

	pgste = pgste_get_lock(pte);
	pgste_val(pgste) &= ~_PGSTE_GPS_USAGE_MASK;
	pgste_set_unlock(pte, pgste);
	return 0;
}

void s390_reset_cmma(struct mm_struct *mm)
{
	struct mm_walk walk = { .pte_entry = __s390_reset_cmma };

	down_write(&mm->mmap_sem);
	walk.mm = mm;
	walk_page_range(0, TASK_SIZE, &walk);
	up_write(&mm->mmap_sem);
}
EXPORT_SYMBOL_GPL(s390_reset_cmma);

/*
 * Test and reset if a guest page is dirty
 */
bool gmap_test_and_clear_dirty(unsigned long address, struct gmap *gmap)
{
	pte_t *pte;
	spinlock_t *ptl;
	bool dirty = false;

	pte = get_locked_pte(gmap->mm, address, &ptl);
	if (unlikely(!pte))
		return false;

	if (ptep_test_and_clear_user_dirty(gmap->mm, address, pte))
		dirty = true;

	spin_unlock(ptl);
	return dirty;
}
EXPORT_SYMBOL_GPL(gmap_test_and_clear_dirty);

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

	entry = pmd_mkyoung(entry);
	if (dirty)
		entry = pmd_mkdirty(entry);
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

void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable)
{
	struct list_head *lh = (struct list_head *) pgtable;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	if (!pmd_huge_pte(mm, pmdp))
		INIT_LIST_HEAD(lh);
	else
		list_add(lh, (struct list_head *) pmd_huge_pte(mm, pmdp));
	pmd_huge_pte(mm, pmdp) = pgtable;
}

pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	struct list_head *lh;
	pgtable_t pgtable;
	pte_t *ptep;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

	/* FIFO */
	pgtable = pmd_huge_pte(mm, pmdp);
	lh = (struct list_head *) pgtable;
	if (list_empty(lh))
		pmd_huge_pte(mm, pmdp) = NULL;
	else {
		pmd_huge_pte(mm, pmdp) = (pgtable_t) lh->next;
		list_del(lh);
	}
	ptep = (pte_t *) pgtable;
	pte_val(*ptep) = _PAGE_INVALID;
	ptep++;
	pte_val(*ptep) = _PAGE_INVALID;
	return pgtable;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
