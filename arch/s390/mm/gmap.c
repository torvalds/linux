/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/ksm.h>
#include <linux/mman.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/gmap.h>
#include <asm/tlb.h>

/**
 * gmap_alloc - allocate and initialize a guest address space
 * @mm: pointer to the parent mm_struct
 * @limit: maximum address of the gmap address space
 *
 * Returns a guest address space structure.
 */
static struct gmap *gmap_alloc(unsigned long limit)
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
	atomic_set(&gmap->ref_count, 1);
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
	return gmap;

out_free:
	kfree(gmap);
out:
	return NULL;
}

/**
 * gmap_create - create a guest address space
 * @mm: pointer to the parent mm_struct
 * @limit: maximum size of the gmap address space
 *
 * Returns a guest address space structure.
 */
struct gmap *gmap_create(struct mm_struct *mm, unsigned long limit)
{
	struct gmap *gmap;

	gmap = gmap_alloc(limit);
	if (!gmap)
		return NULL;
	gmap->mm = mm;
	spin_lock(&mm->context.gmap_lock);
	list_add_rcu(&gmap->list, &mm->context.gmap_list);
	spin_unlock(&mm->context.gmap_lock);
	return gmap;
}
EXPORT_SYMBOL_GPL(gmap_create);

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
static void gmap_free(struct gmap *gmap)
{
	struct page *page, *next;

	/* Free all segment & region tables. */
	list_for_each_entry_safe(page, next, &gmap->crst_list, lru)
		__free_pages(page, 2);
	gmap_radix_tree_free(&gmap->guest_to_host);
	gmap_radix_tree_free(&gmap->host_to_guest);
	kfree(gmap);
}

/**
 * gmap_get - increase reference counter for guest address space
 * @gmap: pointer to the guest address space structure
 *
 * Returns the gmap pointer
 */
struct gmap *gmap_get(struct gmap *gmap)
{
	atomic_inc(&gmap->ref_count);
	return gmap;
}
EXPORT_SYMBOL_GPL(gmap_get);

/**
 * gmap_put - decrease reference counter for guest address space
 * @gmap: pointer to the guest address space structure
 *
 * If the reference counter reaches zero the guest address space is freed.
 */
void gmap_put(struct gmap *gmap)
{
	if (atomic_dec_return(&gmap->ref_count) == 0)
		gmap_free(gmap);
}
EXPORT_SYMBOL_GPL(gmap_put);

/**
 * gmap_remove - remove a guest address space but do not free it yet
 * @gmap: pointer to the guest address space structure
 */
void gmap_remove(struct gmap *gmap)
{
	/* Flush tlb. */
	gmap_flush_tlb(gmap);
	/* Remove gmap from the pre-mm list */
	spin_lock(&gmap->mm->context.gmap_lock);
	list_del_rcu(&gmap->list);
	spin_unlock(&gmap->mm->context.gmap_lock);
	synchronize_rcu();
	/* Put reference */
	gmap_put(gmap);
}
EXPORT_SYMBOL_GPL(gmap_remove);

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
 * gmap_map_segment - map a segment to the guest address space
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
	    from + len - 1 > TASK_MAX_SIZE || to + len - 1 > gmap->asce_end)
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
void gmap_unlink(struct mm_struct *mm, unsigned long *table,
		 unsigned long vmaddr)
{
	struct gmap *gmap;
	int flush;

	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		flush = __gmap_unlink_by_vmaddr(gmap, vmaddr);
		if (flush)
			gmap_flush_tlb(gmap);
	}
	rcu_read_unlock();
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
	bool unlocked;

	down_read(&gmap->mm->mmap_sem);

retry:
	unlocked = false;
	vmaddr = __gmap_translate(gmap, gaddr);
	if (IS_ERR_VALUE(vmaddr)) {
		rc = vmaddr;
		goto out_up;
	}
	if (fixup_user_fault(current, gmap->mm, vmaddr, fault_flags,
			     &unlocked)) {
		rc = -EFAULT;
		goto out_up;
	}
	/*
	 * In the case that fixup_user_fault unlocked the mmap_sem during
	 * faultin redo __gmap_translate to not race with a map/unmap_segment.
	 */
	if (unlocked)
		goto retry;

	rc = __gmap_link(gmap, gaddr, vmaddr);
out_up:
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_fault);

/*
 * this function is assumed to be called with mmap_sem held
 */
void __gmap_zap(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long vmaddr;
	spinlock_t *ptl;
	pte_t *ptep;

	/* Find the vm address for the guest address */
	vmaddr = (unsigned long) radix_tree_lookup(&gmap->guest_to_host,
						   gaddr >> PMD_SHIFT);
	if (vmaddr) {
		vmaddr |= gaddr & ~PMD_MASK;
		/* Get pointer to the page table entry */
		ptep = get_locked_pte(gmap->mm, vmaddr, &ptl);
		if (likely(ptep))
			ptep_zap_unused(gmap->mm, vmaddr, ptep, 0);
		pte_unmap_unlock(ptep, ptl);
	}
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
 * gmap_register_pte_notifier - register a pte invalidation callback
 * @nb: pointer to the gmap notifier block
 */
void gmap_register_pte_notifier(struct gmap_notifier *nb)
{
	spin_lock(&gmap_notifier_lock);
	list_add_rcu(&nb->list, &gmap_notifier_list);
	spin_unlock(&gmap_notifier_lock);
}
EXPORT_SYMBOL_GPL(gmap_register_pte_notifier);

/**
 * gmap_unregister_pte_notifier - remove a pte invalidation callback
 * @nb: pointer to the gmap notifier block
 */
void gmap_unregister_pte_notifier(struct gmap_notifier *nb)
{
	spin_lock(&gmap_notifier_lock);
	list_del_rcu(&nb->list);
	spin_unlock(&gmap_notifier_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(gmap_unregister_pte_notifier);

/**
 * gmap_call_notifier - call all registered invalidation callbacks
 * @gmap: pointer to guest mapping meta data structure
 * @start: start virtual address in the guest address space
 * @end: end virtual address in the guest address space
 */
static void gmap_call_notifier(struct gmap *gmap, unsigned long start,
			       unsigned long end)
{
	struct gmap_notifier *nb;

	list_for_each_entry(nb, &gmap_notifier_list, list)
		nb->notifier_call(gmap, start, end);
}

/**
 * gmap_table_walk - walk the gmap page tables
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 *
 * Returns a table pointer for the given guest address.
 */
static inline unsigned long *gmap_table_walk(struct gmap *gmap,
					     unsigned long gaddr)
{
	unsigned long *table;

	table = gmap->table;
	switch (gmap->asce & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_REGION1:
		table += (gaddr >> 53) & 0x7ff;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* Fallthrough */
	case _ASCE_TYPE_REGION2:
		table += (gaddr >> 42) & 0x7ff;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* Fallthrough */
	case _ASCE_TYPE_REGION3:
		table += (gaddr >> 31) & 0x7ff;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		/* Fallthrough */
	case _ASCE_TYPE_SEGMENT:
		table += (gaddr >> 20) & 0x7ff;
	}
	return table;
}

/**
 * gmap_pte_op_walk - walk the gmap page table, get the page table lock
 *		      and return the pte pointer
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @ptl: pointer to the spinlock pointer
 *
 * Returns a pointer to the locked pte for a guest address, or NULL
 */
static pte_t *gmap_pte_op_walk(struct gmap *gmap, unsigned long gaddr,
			       spinlock_t **ptl)
{
	unsigned long *table;

	/* Walk the gmap page table, lock and get pte pointer */
	table = gmap_table_walk(gmap, gaddr);
	if (!table || *table & _SEGMENT_ENTRY_INVALID)
		return NULL;
	return pte_alloc_map_lock(gmap->mm, (pmd_t *) table, gaddr, ptl);
}

/**
 * gmap_pte_op_fixup - force a page in and connect the gmap page table
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @vmaddr: address in the host process address space
 *
 * Returns 0 if the caller can retry __gmap_translate (might fail again),
 * -ENOMEM if out of memory and -EFAULT if anything goes wrong while fixing
 * up or connecting the gmap page table.
 */
static int gmap_pte_op_fixup(struct gmap *gmap, unsigned long gaddr,
			     unsigned long vmaddr)
{
	struct mm_struct *mm = gmap->mm;
	bool unlocked = false;

	if (fixup_user_fault(current, mm, vmaddr, FAULT_FLAG_WRITE, &unlocked))
		return -EFAULT;
	if (unlocked)
		/* lost mmap_sem, caller has to retry __gmap_translate */
		return 0;
	/* Connect the page tables */
	return __gmap_link(gmap, gaddr, vmaddr);
}

/**
 * gmap_pte_op_end - release the page table lock
 * @ptl: pointer to the spinlock pointer
 */
static void gmap_pte_op_end(spinlock_t *ptl)
{
	spin_unlock(ptl);
}

/**
 * gmap_mprotect_notify - change access rights for a range of ptes and
 *                        call the notifier if any pte changes again
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @len: size of area
 * @prot: indicates access rights: PROT_NONE, PROT_READ or PROT_WRITE
 *
 * Returns 0 if for each page in the given range a gmap mapping exists,
 * the new access rights could be set and the notifier could be armed.
 * If the gmap mapping is missing for one or more pages -EFAULT is
 * returned. If no memory could be allocated -ENOMEM is returned.
 * This function establishes missing page table entries.
 */
int gmap_mprotect_notify(struct gmap *gmap, unsigned long gaddr,
			 unsigned long len, int prot)
{
	unsigned long vmaddr;
	spinlock_t *ptl;
	pte_t *ptep;
	int rc = 0;

	if ((gaddr & ~PAGE_MASK) || (len & ~PAGE_MASK))
		return -EINVAL;
	if (!MACHINE_HAS_ESOP && prot == PROT_READ)
		return -EINVAL;
	down_read(&gmap->mm->mmap_sem);
	while (len) {
		rc = -EAGAIN;
		ptep = gmap_pte_op_walk(gmap, gaddr, &ptl);
		if (ptep) {
			rc = ptep_force_prot(gmap->mm, gaddr, ptep, prot);
			gmap_pte_op_end(ptl);
		}
		if (rc) {
			vmaddr = __gmap_translate(gmap, gaddr);
			if (IS_ERR_VALUE(vmaddr)) {
				rc = vmaddr;
				break;
			}
			rc = gmap_pte_op_fixup(gmap, gaddr, vmaddr);
			if (rc)
				break;
			continue;
		}
		gaddr += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
	up_read(&gmap->mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_mprotect_notify);

/**
 * ptep_notify - call all invalidation callbacks for a specific pte.
 * @mm: pointer to the process mm_struct
 * @addr: virtual address in the process address space
 * @pte: pointer to the page table entry
 *
 * This function is assumed to be called with the page table lock held
 * for the pte to notify.
 */
void ptep_notify(struct mm_struct *mm, unsigned long vmaddr, pte_t *pte)
{
	unsigned long offset, gaddr;
	unsigned long *table;
	struct gmap *gmap;

	offset = ((unsigned long) pte) & (255 * sizeof(pte_t));
	offset = offset * (4096 / sizeof(pte_t));
	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		spin_lock(&gmap->guest_table_lock);
		table = radix_tree_lookup(&gmap->host_to_guest,
					  vmaddr >> PMD_SHIFT);
		if (table)
			gaddr = __gmap_segment_gaddr(table) + offset;
		spin_unlock(&gmap->guest_table_lock);
		if (table)
			gmap_call_notifier(gmap, gaddr, gaddr + PAGE_SIZE - 1);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ptep_notify);

static inline void thp_split_mm(struct mm_struct *mm)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct vm_area_struct *vma;
	unsigned long addr;

	for (vma = mm->mmap; vma != NULL; vma = vma->vm_next) {
		for (addr = vma->vm_start;
		     addr < vma->vm_end;
		     addr += PAGE_SIZE)
			follow_page(vma, addr, FOLL_SPLIT);
		vma->vm_flags &= ~VM_HUGEPAGE;
		vma->vm_flags |= VM_NOHUGEPAGE;
	}
	mm->def_flags |= VM_NOHUGEPAGE;
#endif
}

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
	/*
	 * Remove all zero page mappings,
	 * after establishing a policy to forbid zero page mappings
	 * following faults for that page will get fresh anonymous pages
	 */
	if (is_zero_pfn(pte_pfn(*pte)))
		ptep_xchg_direct(walk->mm, addr, pte, __pte(_PAGE_INVALID));
	/* Clear storage key */
	ptep_zap_key(walk->mm, addr, pte);
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
	ptep_zap_unused(walk->mm, addr, pte, 1);
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
