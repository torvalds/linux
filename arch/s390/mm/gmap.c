// SPDX-License-Identifier: GPL-2.0
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2016, 2018
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 David Hildenbrand <david@redhat.com>
 *		 Janosch Frank <frankja@linux.vnet.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/pagewalk.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/ksm.h>
#include <linux/mman.h>
#include <linux/pgtable.h>

#include <asm/pgalloc.h>
#include <asm/gmap.h>
#include <asm/tlb.h>

#define GMAP_SHADOW_FAKE_TABLE 1ULL

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

	if (limit < _REGION3_SIZE) {
		limit = _REGION3_SIZE - 1;
		atype = _ASCE_TYPE_SEGMENT;
		etype = _SEGMENT_ENTRY_EMPTY;
	} else if (limit < _REGION2_SIZE) {
		limit = _REGION2_SIZE - 1;
		atype = _ASCE_TYPE_REGION3;
		etype = _REGION3_ENTRY_EMPTY;
	} else if (limit < _REGION1_SIZE) {
		limit = _REGION1_SIZE - 1;
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
	INIT_LIST_HEAD(&gmap->children);
	INIT_LIST_HEAD(&gmap->pt_list);
	INIT_RADIX_TREE(&gmap->guest_to_host, GFP_KERNEL);
	INIT_RADIX_TREE(&gmap->host_to_guest, GFP_ATOMIC);
	INIT_RADIX_TREE(&gmap->host_to_rmap, GFP_ATOMIC);
	spin_lock_init(&gmap->guest_table_lock);
	spin_lock_init(&gmap->shadow_lock);
	refcount_set(&gmap->ref_count, 1);
	page = alloc_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
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
	unsigned long gmap_asce;

	gmap = gmap_alloc(limit);
	if (!gmap)
		return NULL;
	gmap->mm = mm;
	spin_lock(&mm->context.lock);
	list_add_rcu(&gmap->list, &mm->context.gmap_list);
	if (list_is_singular(&mm->context.gmap_list))
		gmap_asce = gmap->asce;
	else
		gmap_asce = -1UL;
	WRITE_ONCE(mm->context.gmap_asce, gmap_asce);
	spin_unlock(&mm->context.lock);
	return gmap;
}
EXPORT_SYMBOL_GPL(gmap_create);

static void gmap_flush_tlb(struct gmap *gmap)
{
	if (MACHINE_HAS_IDTE)
		__tlb_flush_idte(gmap->asce);
	else
		__tlb_flush_global();
}

static void gmap_radix_tree_free(struct radix_tree_root *root)
{
	struct radix_tree_iter iter;
	unsigned long indices[16];
	unsigned long index;
	void __rcu **slot;
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

static void gmap_rmap_radix_tree_free(struct radix_tree_root *root)
{
	struct gmap_rmap *rmap, *rnext, *head;
	struct radix_tree_iter iter;
	unsigned long indices[16];
	unsigned long index;
	void __rcu **slot;
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
			head = radix_tree_delete(root, index);
			gmap_for_each_rmap_safe(rmap, rnext, head)
				kfree(rmap);
		}
	} while (nr > 0);
}

/**
 * gmap_free - free a guest address space
 * @gmap: pointer to the guest address space structure
 *
 * No locks required. There are no references to this gmap anymore.
 */
static void gmap_free(struct gmap *gmap)
{
	struct page *page, *next;

	/* Flush tlb of all gmaps (if not already done for shadows) */
	if (!(gmap_is_shadow(gmap) && gmap->removed))
		gmap_flush_tlb(gmap);
	/* Free all segment & region tables. */
	list_for_each_entry_safe(page, next, &gmap->crst_list, lru)
		__free_pages(page, CRST_ALLOC_ORDER);
	gmap_radix_tree_free(&gmap->guest_to_host);
	gmap_radix_tree_free(&gmap->host_to_guest);

	/* Free additional data for a shadow gmap */
	if (gmap_is_shadow(gmap)) {
		/* Free all page tables. */
		list_for_each_entry_safe(page, next, &gmap->pt_list, lru)
			page_table_free_pgste(page);
		gmap_rmap_radix_tree_free(&gmap->host_to_rmap);
		/* Release reference to the parent */
		gmap_put(gmap->parent);
	}

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
	refcount_inc(&gmap->ref_count);
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
	if (refcount_dec_and_test(&gmap->ref_count))
		gmap_free(gmap);
}
EXPORT_SYMBOL_GPL(gmap_put);

/**
 * gmap_remove - remove a guest address space but do not free it yet
 * @gmap: pointer to the guest address space structure
 */
void gmap_remove(struct gmap *gmap)
{
	struct gmap *sg, *next;
	unsigned long gmap_asce;

	/* Remove all shadow gmaps linked to this gmap */
	if (!list_empty(&gmap->children)) {
		spin_lock(&gmap->shadow_lock);
		list_for_each_entry_safe(sg, next, &gmap->children, list) {
			list_del(&sg->list);
			gmap_put(sg);
		}
		spin_unlock(&gmap->shadow_lock);
	}
	/* Remove gmap from the pre-mm list */
	spin_lock(&gmap->mm->context.lock);
	list_del_rcu(&gmap->list);
	if (list_empty(&gmap->mm->context.gmap_list))
		gmap_asce = 0;
	else if (list_is_singular(&gmap->mm->context.gmap_list))
		gmap_asce = list_first_entry(&gmap->mm->context.gmap_list,
					     struct gmap, list)->asce;
	else
		gmap_asce = -1UL;
	WRITE_ONCE(gmap->mm->context.gmap_asce, gmap_asce);
	spin_unlock(&gmap->mm->context.lock);
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

/**
 * gmap_get_enabled - get a pointer to the currently enabled gmap
 *
 * Returns a pointer to the currently enabled gmap. 0 if none is enabled.
 */
struct gmap *gmap_get_enabled(void)
{
	return (struct gmap *) S390_lowcore.gmap;
}
EXPORT_SYMBOL_GPL(gmap_get_enabled);

/*
 * gmap_alloc_table is assumed to be called with mmap_lock held
 */
static int gmap_alloc_table(struct gmap *gmap, unsigned long *table,
			    unsigned long init, unsigned long gaddr)
{
	struct page *page;
	unsigned long *new;

	/* since we dont free the gmap table until gmap_free we can unlock */
	page = alloc_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (!page)
		return -ENOMEM;
	new = (unsigned long *) page_to_phys(page);
	crst_table_init(new, init);
	spin_lock(&gmap->guest_table_lock);
	if (*table & _REGION_ENTRY_INVALID) {
		list_add(&page->lru, &gmap->crst_list);
		*table = (unsigned long) new | _REGION_ENTRY_LENGTH |
			(*table & _REGION_ENTRY_TYPE_MASK);
		page->index = gaddr;
		page = NULL;
	}
	spin_unlock(&gmap->guest_table_lock);
	if (page)
		__free_pages(page, CRST_ALLOC_ORDER);
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

	BUG_ON(gmap_is_shadow(gmap));
	spin_lock(&gmap->guest_table_lock);
	entry = radix_tree_delete(&gmap->host_to_guest, vmaddr >> PMD_SHIFT);
	if (entry) {
		flush = (*entry != _SEGMENT_ENTRY_EMPTY);
		*entry = _SEGMENT_ENTRY_EMPTY;
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

	BUG_ON(gmap_is_shadow(gmap));
	if ((to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || to + len < to)
		return -EINVAL;

	flush = 0;
	mmap_write_lock(gmap->mm);
	for (off = 0; off < len; off += PMD_SIZE)
		flush |= __gmap_unmap_by_gaddr(gmap, to + off);
	mmap_write_unlock(gmap->mm);
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

	BUG_ON(gmap_is_shadow(gmap));
	if ((from | to | len) & (PMD_SIZE - 1))
		return -EINVAL;
	if (len == 0 || from + len < from || to + len < to ||
	    from + len - 1 > TASK_SIZE_MAX || to + len - 1 > gmap->asce_end)
		return -EINVAL;

	flush = 0;
	mmap_write_lock(gmap->mm);
	for (off = 0; off < len; off += PMD_SIZE) {
		/* Remove old translation */
		flush |= __gmap_unmap_by_gaddr(gmap, to + off);
		/* Store new translation */
		if (radix_tree_insert(&gmap->guest_to_host,
				      (to + off) >> PMD_SHIFT,
				      (void *) from + off))
			break;
	}
	mmap_write_unlock(gmap->mm);
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
 * The mmap_lock of the mm that belongs to the address space must be held
 * when this function gets called.
 *
 * Note: Can also be called for shadow gmaps.
 */
unsigned long __gmap_translate(struct gmap *gmap, unsigned long gaddr)
{
	unsigned long vmaddr;

	vmaddr = (unsigned long)
		radix_tree_lookup(&gmap->guest_to_host, gaddr >> PMD_SHIFT);
	/* Note: guest_to_host is empty for a shadow gmap */
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

	mmap_read_lock(gmap->mm);
	rc = __gmap_translate(gmap, gaddr);
	mmap_read_unlock(gmap->mm);
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

static void gmap_pmdp_xchg(struct gmap *gmap, pmd_t *old, pmd_t new,
			   unsigned long gaddr);

/**
 * gmap_link - set up shadow page tables to connect a host to a guest address
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: guest address
 * @vmaddr: vm address
 *
 * Returns 0 on success, -ENOMEM for out of memory conditions, and -EFAULT
 * if the vm address is already mapped to a different guest segment.
 * The mmap_lock of the mm that belongs to the address space must be held
 * when this function gets called.
 */
int __gmap_link(struct gmap *gmap, unsigned long gaddr, unsigned long vmaddr)
{
	struct mm_struct *mm;
	unsigned long *table;
	spinlock_t *ptl;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	u64 unprot;
	int rc;

	BUG_ON(gmap_is_shadow(gmap));
	/* Create higher level tables in the gmap page table */
	table = gmap->table;
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION1) {
		table += (gaddr & _REGION1_INDEX) >> _REGION1_SHIFT;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _REGION2_ENTRY_EMPTY,
				     gaddr & _REGION1_MASK))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION2) {
		table += (gaddr & _REGION2_INDEX) >> _REGION2_SHIFT;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _REGION3_ENTRY_EMPTY,
				     gaddr & _REGION2_MASK))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	if ((gmap->asce & _ASCE_TYPE_MASK) >= _ASCE_TYPE_REGION3) {
		table += (gaddr & _REGION3_INDEX) >> _REGION3_SHIFT;
		if ((*table & _REGION_ENTRY_INVALID) &&
		    gmap_alloc_table(gmap, table, _SEGMENT_ENTRY_EMPTY,
				     gaddr & _REGION3_MASK))
			return -ENOMEM;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
	}
	table += (gaddr & _SEGMENT_INDEX) >> _SEGMENT_SHIFT;
	/* Walk the parent mm page table */
	mm = gmap->mm;
	pgd = pgd_offset(mm, vmaddr);
	VM_BUG_ON(pgd_none(*pgd));
	p4d = p4d_offset(pgd, vmaddr);
	VM_BUG_ON(p4d_none(*p4d));
	pud = pud_offset(p4d, vmaddr);
	VM_BUG_ON(pud_none(*pud));
	/* large puds cannot yet be handled */
	if (pud_large(*pud))
		return -EFAULT;
	pmd = pmd_offset(pud, vmaddr);
	VM_BUG_ON(pmd_none(*pmd));
	/* Are we allowed to use huge pages? */
	if (pmd_large(*pmd) && !gmap->mm->context.allow_gmap_hpage_1m)
		return -EFAULT;
	/* Link gmap segment table entry location to page table. */
	rc = radix_tree_preload(GFP_KERNEL);
	if (rc)
		return rc;
	ptl = pmd_lock(mm, pmd);
	spin_lock(&gmap->guest_table_lock);
	if (*table == _SEGMENT_ENTRY_EMPTY) {
		rc = radix_tree_insert(&gmap->host_to_guest,
				       vmaddr >> PMD_SHIFT, table);
		if (!rc) {
			if (pmd_large(*pmd)) {
				*table = (pmd_val(*pmd) &
					  _SEGMENT_ENTRY_HARDWARE_BITS_LARGE)
					| _SEGMENT_ENTRY_GMAP_UC;
			} else
				*table = pmd_val(*pmd) &
					_SEGMENT_ENTRY_HARDWARE_BITS;
		}
	} else if (*table & _SEGMENT_ENTRY_PROTECT &&
		   !(pmd_val(*pmd) & _SEGMENT_ENTRY_PROTECT)) {
		unprot = (u64)*table;
		unprot &= ~_SEGMENT_ENTRY_PROTECT;
		unprot |= _SEGMENT_ENTRY_GMAP_UC;
		gmap_pmdp_xchg(gmap, (pmd_t *)table, __pmd(unprot), gaddr);
	}
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

	mmap_read_lock(gmap->mm);

retry:
	unlocked = false;
	vmaddr = __gmap_translate(gmap, gaddr);
	if (IS_ERR_VALUE(vmaddr)) {
		rc = vmaddr;
		goto out_up;
	}
	if (fixup_user_fault(gmap->mm, vmaddr, fault_flags,
			     &unlocked)) {
		rc = -EFAULT;
		goto out_up;
	}
	/*
	 * In the case that fixup_user_fault unlocked the mmap_lock during
	 * faultin redo __gmap_translate to not race with a map/unmap_segment.
	 */
	if (unlocked)
		goto retry;

	rc = __gmap_link(gmap, gaddr, vmaddr);
out_up:
	mmap_read_unlock(gmap->mm);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_fault);

/*
 * this function is assumed to be called with mmap_lock held
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
		if (likely(ptep)) {
			ptep_zap_unused(gmap->mm, vmaddr, ptep, 0);
			pte_unmap_unlock(ptep, ptl);
		}
	}
}
EXPORT_SYMBOL_GPL(__gmap_zap);

void gmap_discard(struct gmap *gmap, unsigned long from, unsigned long to)
{
	unsigned long gaddr, vmaddr, size;
	struct vm_area_struct *vma;

	mmap_read_lock(gmap->mm);
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
		if (!vma)
			continue;
		/*
		 * We do not discard pages that are backed by
		 * hugetlbfs, so we don't have to refault them.
		 */
		if (is_vm_hugetlb_page(vma))
			continue;
		size = min(to - gaddr, PMD_SIZE - (gaddr & ~PMD_MASK));
		zap_page_range(vma, vmaddr, size);
	}
	mmap_read_unlock(gmap->mm);
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
 * @level: page table level to stop at
 *
 * Returns a table entry pointer for the given guest address and @level
 * @level=0 : returns a pointer to a page table table entry (or NULL)
 * @level=1 : returns a pointer to a segment table entry (or NULL)
 * @level=2 : returns a pointer to a region-3 table entry (or NULL)
 * @level=3 : returns a pointer to a region-2 table entry (or NULL)
 * @level=4 : returns a pointer to a region-1 table entry (or NULL)
 *
 * Returns NULL if the gmap page tables could not be walked to the
 * requested level.
 *
 * Note: Can also be called for shadow gmaps.
 */
static inline unsigned long *gmap_table_walk(struct gmap *gmap,
					     unsigned long gaddr, int level)
{
	const int asce_type = gmap->asce & _ASCE_TYPE_MASK;
	unsigned long *table = gmap->table;

	if (gmap_is_shadow(gmap) && gmap->removed)
		return NULL;

	if (WARN_ON_ONCE(level > (asce_type >> 2) + 1))
		return NULL;

	if (asce_type != _ASCE_TYPE_REGION1 &&
	    gaddr & (-1UL << (31 + (asce_type >> 2) * 11)))
		return NULL;

	switch (asce_type) {
	case _ASCE_TYPE_REGION1:
		table += (gaddr & _REGION1_INDEX) >> _REGION1_SHIFT;
		if (level == 4)
			break;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_REGION2:
		table += (gaddr & _REGION2_INDEX) >> _REGION2_SHIFT;
		if (level == 3)
			break;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_REGION3:
		table += (gaddr & _REGION3_INDEX) >> _REGION3_SHIFT;
		if (level == 2)
			break;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_SEGMENT:
		table += (gaddr & _SEGMENT_INDEX) >> _SEGMENT_SHIFT;
		if (level == 1)
			break;
		if (*table & _REGION_ENTRY_INVALID)
			return NULL;
		table = (unsigned long *)(*table & _SEGMENT_ENTRY_ORIGIN);
		table += (gaddr & _PAGE_INDEX) >> _PAGE_SHIFT;
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

	BUG_ON(gmap_is_shadow(gmap));
	/* Walk the gmap page table, lock and get pte pointer */
	table = gmap_table_walk(gmap, gaddr, 1); /* get segment pointer */
	if (!table || *table & _SEGMENT_ENTRY_INVALID)
		return NULL;
	return pte_alloc_map_lock(gmap->mm, (pmd_t *) table, gaddr, ptl);
}

/**
 * gmap_pte_op_fixup - force a page in and connect the gmap page table
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @vmaddr: address in the host process address space
 * @prot: indicates access rights: PROT_NONE, PROT_READ or PROT_WRITE
 *
 * Returns 0 if the caller can retry __gmap_translate (might fail again),
 * -ENOMEM if out of memory and -EFAULT if anything goes wrong while fixing
 * up or connecting the gmap page table.
 */
static int gmap_pte_op_fixup(struct gmap *gmap, unsigned long gaddr,
			     unsigned long vmaddr, int prot)
{
	struct mm_struct *mm = gmap->mm;
	unsigned int fault_flags;
	bool unlocked = false;

	BUG_ON(gmap_is_shadow(gmap));
	fault_flags = (prot == PROT_WRITE) ? FAULT_FLAG_WRITE : 0;
	if (fixup_user_fault(mm, vmaddr, fault_flags, &unlocked))
		return -EFAULT;
	if (unlocked)
		/* lost mmap_lock, caller has to retry __gmap_translate */
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
	if (ptl)
		spin_unlock(ptl);
}

/**
 * gmap_pmd_op_walk - walk the gmap tables, get the guest table lock
 *		      and return the pmd pointer
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 *
 * Returns a pointer to the pmd for a guest address, or NULL
 */
static inline pmd_t *gmap_pmd_op_walk(struct gmap *gmap, unsigned long gaddr)
{
	pmd_t *pmdp;

	BUG_ON(gmap_is_shadow(gmap));
	pmdp = (pmd_t *) gmap_table_walk(gmap, gaddr, 1);
	if (!pmdp)
		return NULL;

	/* without huge pages, there is no need to take the table lock */
	if (!gmap->mm->context.allow_gmap_hpage_1m)
		return pmd_none(*pmdp) ? NULL : pmdp;

	spin_lock(&gmap->guest_table_lock);
	if (pmd_none(*pmdp)) {
		spin_unlock(&gmap->guest_table_lock);
		return NULL;
	}

	/* 4k page table entries are locked via the pte (pte_alloc_map_lock). */
	if (!pmd_large(*pmdp))
		spin_unlock(&gmap->guest_table_lock);
	return pmdp;
}

/**
 * gmap_pmd_op_end - release the guest_table_lock if needed
 * @gmap: pointer to the guest mapping meta data structure
 * @pmdp: pointer to the pmd
 */
static inline void gmap_pmd_op_end(struct gmap *gmap, pmd_t *pmdp)
{
	if (pmd_large(*pmdp))
		spin_unlock(&gmap->guest_table_lock);
}

/*
 * gmap_protect_pmd - remove access rights to memory and set pmd notification bits
 * @pmdp: pointer to the pmd to be protected
 * @prot: indicates access rights: PROT_NONE, PROT_READ or PROT_WRITE
 * @bits: notification bits to set
 *
 * Returns:
 * 0 if successfully protected
 * -EAGAIN if a fixup is needed
 * -EINVAL if unsupported notifier bits have been specified
 *
 * Expected to be called with sg->mm->mmap_lock in read and
 * guest_table_lock held.
 */
static int gmap_protect_pmd(struct gmap *gmap, unsigned long gaddr,
			    pmd_t *pmdp, int prot, unsigned long bits)
{
	int pmd_i = pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID;
	int pmd_p = pmd_val(*pmdp) & _SEGMENT_ENTRY_PROTECT;
	pmd_t new = *pmdp;

	/* Fixup needed */
	if ((pmd_i && (prot != PROT_NONE)) || (pmd_p && (prot == PROT_WRITE)))
		return -EAGAIN;

	if (prot == PROT_NONE && !pmd_i) {
		pmd_val(new) |= _SEGMENT_ENTRY_INVALID;
		gmap_pmdp_xchg(gmap, pmdp, new, gaddr);
	}

	if (prot == PROT_READ && !pmd_p) {
		pmd_val(new) &= ~_SEGMENT_ENTRY_INVALID;
		pmd_val(new) |= _SEGMENT_ENTRY_PROTECT;
		gmap_pmdp_xchg(gmap, pmdp, new, gaddr);
	}

	if (bits & GMAP_NOTIFY_MPROT)
		pmd_val(*pmdp) |= _SEGMENT_ENTRY_GMAP_IN;

	/* Shadow GMAP protection needs split PMDs */
	if (bits & GMAP_NOTIFY_SHADOW)
		return -EINVAL;

	return 0;
}

/*
 * gmap_protect_pte - remove access rights to memory and set pgste bits
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @pmdp: pointer to the pmd associated with the pte
 * @prot: indicates access rights: PROT_NONE, PROT_READ or PROT_WRITE
 * @bits: notification bits to set
 *
 * Returns 0 if successfully protected, -ENOMEM if out of memory and
 * -EAGAIN if a fixup is needed.
 *
 * Expected to be called with sg->mm->mmap_lock in read
 */
static int gmap_protect_pte(struct gmap *gmap, unsigned long gaddr,
			    pmd_t *pmdp, int prot, unsigned long bits)
{
	int rc;
	pte_t *ptep;
	spinlock_t *ptl = NULL;
	unsigned long pbits = 0;

	if (pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID)
		return -EAGAIN;

	ptep = pte_alloc_map_lock(gmap->mm, pmdp, gaddr, &ptl);
	if (!ptep)
		return -ENOMEM;

	pbits |= (bits & GMAP_NOTIFY_MPROT) ? PGSTE_IN_BIT : 0;
	pbits |= (bits & GMAP_NOTIFY_SHADOW) ? PGSTE_VSIE_BIT : 0;
	/* Protect and unlock. */
	rc = ptep_force_prot(gmap->mm, gaddr, ptep, prot, pbits);
	gmap_pte_op_end(ptl);
	return rc;
}

/*
 * gmap_protect_range - remove access rights to memory and set pgste bits
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @len: size of area
 * @prot: indicates access rights: PROT_NONE, PROT_READ or PROT_WRITE
 * @bits: pgste notification bits to set
 *
 * Returns 0 if successfully protected, -ENOMEM if out of memory and
 * -EFAULT if gaddr is invalid (or mapping for shadows is missing).
 *
 * Called with sg->mm->mmap_lock in read.
 */
static int gmap_protect_range(struct gmap *gmap, unsigned long gaddr,
			      unsigned long len, int prot, unsigned long bits)
{
	unsigned long vmaddr, dist;
	pmd_t *pmdp;
	int rc;

	BUG_ON(gmap_is_shadow(gmap));
	while (len) {
		rc = -EAGAIN;
		pmdp = gmap_pmd_op_walk(gmap, gaddr);
		if (pmdp) {
			if (!pmd_large(*pmdp)) {
				rc = gmap_protect_pte(gmap, gaddr, pmdp, prot,
						      bits);
				if (!rc) {
					len -= PAGE_SIZE;
					gaddr += PAGE_SIZE;
				}
			} else {
				rc = gmap_protect_pmd(gmap, gaddr, pmdp, prot,
						      bits);
				if (!rc) {
					dist = HPAGE_SIZE - (gaddr & ~HPAGE_MASK);
					len = len < dist ? 0 : len - dist;
					gaddr = (gaddr & HPAGE_MASK) + HPAGE_SIZE;
				}
			}
			gmap_pmd_op_end(gmap, pmdp);
		}
		if (rc) {
			if (rc == -EINVAL)
				return rc;

			/* -EAGAIN, fixup of userspace mm and gmap */
			vmaddr = __gmap_translate(gmap, gaddr);
			if (IS_ERR_VALUE(vmaddr))
				return vmaddr;
			rc = gmap_pte_op_fixup(gmap, gaddr, vmaddr, prot);
			if (rc)
				return rc;
		}
	}
	return 0;
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
	int rc;

	if ((gaddr & ~PAGE_MASK) || (len & ~PAGE_MASK) || gmap_is_shadow(gmap))
		return -EINVAL;
	if (!MACHINE_HAS_ESOP && prot == PROT_READ)
		return -EINVAL;
	mmap_read_lock(gmap->mm);
	rc = gmap_protect_range(gmap, gaddr, len, prot, GMAP_NOTIFY_MPROT);
	mmap_read_unlock(gmap->mm);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_mprotect_notify);

/**
 * gmap_read_table - get an unsigned long value from a guest page table using
 *                   absolute addressing, without marking the page referenced.
 * @gmap: pointer to guest mapping meta data structure
 * @gaddr: virtual address in the guest address space
 * @val: pointer to the unsigned long value to return
 *
 * Returns 0 if the value was read, -ENOMEM if out of memory and -EFAULT
 * if reading using the virtual address failed. -EINVAL if called on a gmap
 * shadow.
 *
 * Called with gmap->mm->mmap_lock in read.
 */
int gmap_read_table(struct gmap *gmap, unsigned long gaddr, unsigned long *val)
{
	unsigned long address, vmaddr;
	spinlock_t *ptl;
	pte_t *ptep, pte;
	int rc;

	if (gmap_is_shadow(gmap))
		return -EINVAL;

	while (1) {
		rc = -EAGAIN;
		ptep = gmap_pte_op_walk(gmap, gaddr, &ptl);
		if (ptep) {
			pte = *ptep;
			if (pte_present(pte) && (pte_val(pte) & _PAGE_READ)) {
				address = pte_val(pte) & PAGE_MASK;
				address += gaddr & ~PAGE_MASK;
				*val = *(unsigned long *) address;
				pte_val(*ptep) |= _PAGE_YOUNG;
				/* Do *NOT* clear the _PAGE_INVALID bit! */
				rc = 0;
			}
			gmap_pte_op_end(ptl);
		}
		if (!rc)
			break;
		vmaddr = __gmap_translate(gmap, gaddr);
		if (IS_ERR_VALUE(vmaddr)) {
			rc = vmaddr;
			break;
		}
		rc = gmap_pte_op_fixup(gmap, gaddr, vmaddr, PROT_READ);
		if (rc)
			break;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_read_table);

/**
 * gmap_insert_rmap - add a rmap to the host_to_rmap radix tree
 * @sg: pointer to the shadow guest address space structure
 * @vmaddr: vm address associated with the rmap
 * @rmap: pointer to the rmap structure
 *
 * Called with the sg->guest_table_lock
 */
static inline void gmap_insert_rmap(struct gmap *sg, unsigned long vmaddr,
				    struct gmap_rmap *rmap)
{
	void __rcu **slot;

	BUG_ON(!gmap_is_shadow(sg));
	slot = radix_tree_lookup_slot(&sg->host_to_rmap, vmaddr >> PAGE_SHIFT);
	if (slot) {
		rmap->next = radix_tree_deref_slot_protected(slot,
							&sg->guest_table_lock);
		radix_tree_replace_slot(&sg->host_to_rmap, slot, rmap);
	} else {
		rmap->next = NULL;
		radix_tree_insert(&sg->host_to_rmap, vmaddr >> PAGE_SHIFT,
				  rmap);
	}
}

/**
 * gmap_protect_rmap - restrict access rights to memory (RO) and create an rmap
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow gmap
 * @paddr: address in the parent guest address space
 * @len: length of the memory area to protect
 *
 * Returns 0 if successfully protected and the rmap was created, -ENOMEM
 * if out of memory and -EFAULT if paddr is invalid.
 */
static int gmap_protect_rmap(struct gmap *sg, unsigned long raddr,
			     unsigned long paddr, unsigned long len)
{
	struct gmap *parent;
	struct gmap_rmap *rmap;
	unsigned long vmaddr;
	spinlock_t *ptl;
	pte_t *ptep;
	int rc;

	BUG_ON(!gmap_is_shadow(sg));
	parent = sg->parent;
	while (len) {
		vmaddr = __gmap_translate(parent, paddr);
		if (IS_ERR_VALUE(vmaddr))
			return vmaddr;
		rmap = kzalloc(sizeof(*rmap), GFP_KERNEL);
		if (!rmap)
			return -ENOMEM;
		rmap->raddr = raddr;
		rc = radix_tree_preload(GFP_KERNEL);
		if (rc) {
			kfree(rmap);
			return rc;
		}
		rc = -EAGAIN;
		ptep = gmap_pte_op_walk(parent, paddr, &ptl);
		if (ptep) {
			spin_lock(&sg->guest_table_lock);
			rc = ptep_force_prot(parent->mm, paddr, ptep, PROT_READ,
					     PGSTE_VSIE_BIT);
			if (!rc)
				gmap_insert_rmap(sg, vmaddr, rmap);
			spin_unlock(&sg->guest_table_lock);
			gmap_pte_op_end(ptl);
		}
		radix_tree_preload_end();
		if (rc) {
			kfree(rmap);
			rc = gmap_pte_op_fixup(parent, paddr, vmaddr, PROT_READ);
			if (rc)
				return rc;
			continue;
		}
		paddr += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
	return 0;
}

#define _SHADOW_RMAP_MASK	0x7
#define _SHADOW_RMAP_REGION1	0x5
#define _SHADOW_RMAP_REGION2	0x4
#define _SHADOW_RMAP_REGION3	0x3
#define _SHADOW_RMAP_SEGMENT	0x2
#define _SHADOW_RMAP_PGTABLE	0x1

/**
 * gmap_idte_one - invalidate a single region or segment table entry
 * @asce: region or segment table *origin* + table-type bits
 * @vaddr: virtual address to identify the table entry to flush
 *
 * The invalid bit of a single region or segment table entry is set
 * and the associated TLB entries depending on the entry are flushed.
 * The table-type of the @asce identifies the portion of the @vaddr
 * that is used as the invalidation index.
 */
static inline void gmap_idte_one(unsigned long asce, unsigned long vaddr)
{
	asm volatile(
		"	.insn	rrf,0xb98e0000,%0,%1,0,0"
		: : "a" (asce), "a" (vaddr) : "cc", "memory");
}

/**
 * gmap_unshadow_page - remove a page from a shadow page table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 *
 * Called with the sg->guest_table_lock
 */
static void gmap_unshadow_page(struct gmap *sg, unsigned long raddr)
{
	unsigned long *table;

	BUG_ON(!gmap_is_shadow(sg));
	table = gmap_table_walk(sg, raddr, 0); /* get page table pointer */
	if (!table || *table & _PAGE_INVALID)
		return;
	gmap_call_notifier(sg, raddr, raddr + _PAGE_SIZE - 1);
	ptep_unshadow_pte(sg->mm, raddr, (pte_t *) table);
}

/**
 * __gmap_unshadow_pgt - remove all entries from a shadow page table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 * @pgt: pointer to the start of a shadow page table
 *
 * Called with the sg->guest_table_lock
 */
static void __gmap_unshadow_pgt(struct gmap *sg, unsigned long raddr,
				unsigned long *pgt)
{
	int i;

	BUG_ON(!gmap_is_shadow(sg));
	for (i = 0; i < _PAGE_ENTRIES; i++, raddr += _PAGE_SIZE)
		pgt[i] = _PAGE_INVALID;
}

/**
 * gmap_unshadow_pgt - remove a shadow page table from a segment entry
 * @sg: pointer to the shadow guest address space structure
 * @raddr: address in the shadow guest address space
 *
 * Called with the sg->guest_table_lock
 */
static void gmap_unshadow_pgt(struct gmap *sg, unsigned long raddr)
{
	unsigned long sto, *ste, *pgt;
	struct page *page;

	BUG_ON(!gmap_is_shadow(sg));
	ste = gmap_table_walk(sg, raddr, 1); /* get segment pointer */
	if (!ste || !(*ste & _SEGMENT_ENTRY_ORIGIN))
		return;
	gmap_call_notifier(sg, raddr, raddr + _SEGMENT_SIZE - 1);
	sto = (unsigned long) (ste - ((raddr & _SEGMENT_INDEX) >> _SEGMENT_SHIFT));
	gmap_idte_one(sto | _ASCE_TYPE_SEGMENT, raddr);
	pgt = (unsigned long *)(*ste & _SEGMENT_ENTRY_ORIGIN);
	*ste = _SEGMENT_ENTRY_EMPTY;
	__gmap_unshadow_pgt(sg, raddr, pgt);
	/* Free page table */
	page = pfn_to_page(__pa(pgt) >> PAGE_SHIFT);
	list_del(&page->lru);
	page_table_free_pgste(page);
}

/**
 * __gmap_unshadow_sgt - remove all entries from a shadow segment table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 * @sgt: pointer to the start of a shadow segment table
 *
 * Called with the sg->guest_table_lock
 */
static void __gmap_unshadow_sgt(struct gmap *sg, unsigned long raddr,
				unsigned long *sgt)
{
	unsigned long *pgt;
	struct page *page;
	int i;

	BUG_ON(!gmap_is_shadow(sg));
	for (i = 0; i < _CRST_ENTRIES; i++, raddr += _SEGMENT_SIZE) {
		if (!(sgt[i] & _SEGMENT_ENTRY_ORIGIN))
			continue;
		pgt = (unsigned long *)(sgt[i] & _REGION_ENTRY_ORIGIN);
		sgt[i] = _SEGMENT_ENTRY_EMPTY;
		__gmap_unshadow_pgt(sg, raddr, pgt);
		/* Free page table */
		page = pfn_to_page(__pa(pgt) >> PAGE_SHIFT);
		list_del(&page->lru);
		page_table_free_pgste(page);
	}
}

/**
 * gmap_unshadow_sgt - remove a shadow segment table from a region-3 entry
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 *
 * Called with the shadow->guest_table_lock
 */
static void gmap_unshadow_sgt(struct gmap *sg, unsigned long raddr)
{
	unsigned long r3o, *r3e, *sgt;
	struct page *page;

	BUG_ON(!gmap_is_shadow(sg));
	r3e = gmap_table_walk(sg, raddr, 2); /* get region-3 pointer */
	if (!r3e || !(*r3e & _REGION_ENTRY_ORIGIN))
		return;
	gmap_call_notifier(sg, raddr, raddr + _REGION3_SIZE - 1);
	r3o = (unsigned long) (r3e - ((raddr & _REGION3_INDEX) >> _REGION3_SHIFT));
	gmap_idte_one(r3o | _ASCE_TYPE_REGION3, raddr);
	sgt = (unsigned long *)(*r3e & _REGION_ENTRY_ORIGIN);
	*r3e = _REGION3_ENTRY_EMPTY;
	__gmap_unshadow_sgt(sg, raddr, sgt);
	/* Free segment table */
	page = pfn_to_page(__pa(sgt) >> PAGE_SHIFT);
	list_del(&page->lru);
	__free_pages(page, CRST_ALLOC_ORDER);
}

/**
 * __gmap_unshadow_r3t - remove all entries from a shadow region-3 table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: address in the shadow guest address space
 * @r3t: pointer to the start of a shadow region-3 table
 *
 * Called with the sg->guest_table_lock
 */
static void __gmap_unshadow_r3t(struct gmap *sg, unsigned long raddr,
				unsigned long *r3t)
{
	unsigned long *sgt;
	struct page *page;
	int i;

	BUG_ON(!gmap_is_shadow(sg));
	for (i = 0; i < _CRST_ENTRIES; i++, raddr += _REGION3_SIZE) {
		if (!(r3t[i] & _REGION_ENTRY_ORIGIN))
			continue;
		sgt = (unsigned long *)(r3t[i] & _REGION_ENTRY_ORIGIN);
		r3t[i] = _REGION3_ENTRY_EMPTY;
		__gmap_unshadow_sgt(sg, raddr, sgt);
		/* Free segment table */
		page = pfn_to_page(__pa(sgt) >> PAGE_SHIFT);
		list_del(&page->lru);
		__free_pages(page, CRST_ALLOC_ORDER);
	}
}

/**
 * gmap_unshadow_r3t - remove a shadow region-3 table from a region-2 entry
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 *
 * Called with the sg->guest_table_lock
 */
static void gmap_unshadow_r3t(struct gmap *sg, unsigned long raddr)
{
	unsigned long r2o, *r2e, *r3t;
	struct page *page;

	BUG_ON(!gmap_is_shadow(sg));
	r2e = gmap_table_walk(sg, raddr, 3); /* get region-2 pointer */
	if (!r2e || !(*r2e & _REGION_ENTRY_ORIGIN))
		return;
	gmap_call_notifier(sg, raddr, raddr + _REGION2_SIZE - 1);
	r2o = (unsigned long) (r2e - ((raddr & _REGION2_INDEX) >> _REGION2_SHIFT));
	gmap_idte_one(r2o | _ASCE_TYPE_REGION2, raddr);
	r3t = (unsigned long *)(*r2e & _REGION_ENTRY_ORIGIN);
	*r2e = _REGION2_ENTRY_EMPTY;
	__gmap_unshadow_r3t(sg, raddr, r3t);
	/* Free region 3 table */
	page = pfn_to_page(__pa(r3t) >> PAGE_SHIFT);
	list_del(&page->lru);
	__free_pages(page, CRST_ALLOC_ORDER);
}

/**
 * __gmap_unshadow_r2t - remove all entries from a shadow region-2 table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 * @r2t: pointer to the start of a shadow region-2 table
 *
 * Called with the sg->guest_table_lock
 */
static void __gmap_unshadow_r2t(struct gmap *sg, unsigned long raddr,
				unsigned long *r2t)
{
	unsigned long *r3t;
	struct page *page;
	int i;

	BUG_ON(!gmap_is_shadow(sg));
	for (i = 0; i < _CRST_ENTRIES; i++, raddr += _REGION2_SIZE) {
		if (!(r2t[i] & _REGION_ENTRY_ORIGIN))
			continue;
		r3t = (unsigned long *)(r2t[i] & _REGION_ENTRY_ORIGIN);
		r2t[i] = _REGION2_ENTRY_EMPTY;
		__gmap_unshadow_r3t(sg, raddr, r3t);
		/* Free region 3 table */
		page = pfn_to_page(__pa(r3t) >> PAGE_SHIFT);
		list_del(&page->lru);
		__free_pages(page, CRST_ALLOC_ORDER);
	}
}

/**
 * gmap_unshadow_r2t - remove a shadow region-2 table from a region-1 entry
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 *
 * Called with the sg->guest_table_lock
 */
static void gmap_unshadow_r2t(struct gmap *sg, unsigned long raddr)
{
	unsigned long r1o, *r1e, *r2t;
	struct page *page;

	BUG_ON(!gmap_is_shadow(sg));
	r1e = gmap_table_walk(sg, raddr, 4); /* get region-1 pointer */
	if (!r1e || !(*r1e & _REGION_ENTRY_ORIGIN))
		return;
	gmap_call_notifier(sg, raddr, raddr + _REGION1_SIZE - 1);
	r1o = (unsigned long) (r1e - ((raddr & _REGION1_INDEX) >> _REGION1_SHIFT));
	gmap_idte_one(r1o | _ASCE_TYPE_REGION1, raddr);
	r2t = (unsigned long *)(*r1e & _REGION_ENTRY_ORIGIN);
	*r1e = _REGION1_ENTRY_EMPTY;
	__gmap_unshadow_r2t(sg, raddr, r2t);
	/* Free region 2 table */
	page = pfn_to_page(__pa(r2t) >> PAGE_SHIFT);
	list_del(&page->lru);
	__free_pages(page, CRST_ALLOC_ORDER);
}

/**
 * __gmap_unshadow_r1t - remove all entries from a shadow region-1 table
 * @sg: pointer to the shadow guest address space structure
 * @raddr: rmap address in the shadow guest address space
 * @r1t: pointer to the start of a shadow region-1 table
 *
 * Called with the shadow->guest_table_lock
 */
static void __gmap_unshadow_r1t(struct gmap *sg, unsigned long raddr,
				unsigned long *r1t)
{
	unsigned long asce, *r2t;
	struct page *page;
	int i;

	BUG_ON(!gmap_is_shadow(sg));
	asce = (unsigned long) r1t | _ASCE_TYPE_REGION1;
	for (i = 0; i < _CRST_ENTRIES; i++, raddr += _REGION1_SIZE) {
		if (!(r1t[i] & _REGION_ENTRY_ORIGIN))
			continue;
		r2t = (unsigned long *)(r1t[i] & _REGION_ENTRY_ORIGIN);
		__gmap_unshadow_r2t(sg, raddr, r2t);
		/* Clear entry and flush translation r1t -> r2t */
		gmap_idte_one(asce, raddr);
		r1t[i] = _REGION1_ENTRY_EMPTY;
		/* Free region 2 table */
		page = pfn_to_page(__pa(r2t) >> PAGE_SHIFT);
		list_del(&page->lru);
		__free_pages(page, CRST_ALLOC_ORDER);
	}
}

/**
 * gmap_unshadow - remove a shadow page table completely
 * @sg: pointer to the shadow guest address space structure
 *
 * Called with sg->guest_table_lock
 */
static void gmap_unshadow(struct gmap *sg)
{
	unsigned long *table;

	BUG_ON(!gmap_is_shadow(sg));
	if (sg->removed)
		return;
	sg->removed = 1;
	gmap_call_notifier(sg, 0, -1UL);
	gmap_flush_tlb(sg);
	table = (unsigned long *)(sg->asce & _ASCE_ORIGIN);
	switch (sg->asce & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_REGION1:
		__gmap_unshadow_r1t(sg, 0, table);
		break;
	case _ASCE_TYPE_REGION2:
		__gmap_unshadow_r2t(sg, 0, table);
		break;
	case _ASCE_TYPE_REGION3:
		__gmap_unshadow_r3t(sg, 0, table);
		break;
	case _ASCE_TYPE_SEGMENT:
		__gmap_unshadow_sgt(sg, 0, table);
		break;
	}
}

/**
 * gmap_find_shadow - find a specific asce in the list of shadow tables
 * @parent: pointer to the parent gmap
 * @asce: ASCE for which the shadow table is created
 * @edat_level: edat level to be used for the shadow translation
 *
 * Returns the pointer to a gmap if a shadow table with the given asce is
 * already available, ERR_PTR(-EAGAIN) if another one is just being created,
 * otherwise NULL
 */
static struct gmap *gmap_find_shadow(struct gmap *parent, unsigned long asce,
				     int edat_level)
{
	struct gmap *sg;

	list_for_each_entry(sg, &parent->children, list) {
		if (sg->orig_asce != asce || sg->edat_level != edat_level ||
		    sg->removed)
			continue;
		if (!sg->initialized)
			return ERR_PTR(-EAGAIN);
		refcount_inc(&sg->ref_count);
		return sg;
	}
	return NULL;
}

/**
 * gmap_shadow_valid - check if a shadow guest address space matches the
 *                     given properties and is still valid
 * @sg: pointer to the shadow guest address space structure
 * @asce: ASCE for which the shadow table is requested
 * @edat_level: edat level to be used for the shadow translation
 *
 * Returns 1 if the gmap shadow is still valid and matches the given
 * properties, the caller can continue using it. Returns 0 otherwise, the
 * caller has to request a new shadow gmap in this case.
 *
 */
int gmap_shadow_valid(struct gmap *sg, unsigned long asce, int edat_level)
{
	if (sg->removed)
		return 0;
	return sg->orig_asce == asce && sg->edat_level == edat_level;
}
EXPORT_SYMBOL_GPL(gmap_shadow_valid);

/**
 * gmap_shadow - create/find a shadow guest address space
 * @parent: pointer to the parent gmap
 * @asce: ASCE for which the shadow table is created
 * @edat_level: edat level to be used for the shadow translation
 *
 * The pages of the top level page table referred by the asce parameter
 * will be set to read-only and marked in the PGSTEs of the kvm process.
 * The shadow table will be removed automatically on any change to the
 * PTE mapping for the source table.
 *
 * Returns a guest address space structure, ERR_PTR(-ENOMEM) if out of memory,
 * ERR_PTR(-EAGAIN) if the caller has to retry and ERR_PTR(-EFAULT) if the
 * parent gmap table could not be protected.
 */
struct gmap *gmap_shadow(struct gmap *parent, unsigned long asce,
			 int edat_level)
{
	struct gmap *sg, *new;
	unsigned long limit;
	int rc;

	BUG_ON(parent->mm->context.allow_gmap_hpage_1m);
	BUG_ON(gmap_is_shadow(parent));
	spin_lock(&parent->shadow_lock);
	sg = gmap_find_shadow(parent, asce, edat_level);
	spin_unlock(&parent->shadow_lock);
	if (sg)
		return sg;
	/* Create a new shadow gmap */
	limit = -1UL >> (33 - (((asce & _ASCE_TYPE_MASK) >> 2) * 11));
	if (asce & _ASCE_REAL_SPACE)
		limit = -1UL;
	new = gmap_alloc(limit);
	if (!new)
		return ERR_PTR(-ENOMEM);
	new->mm = parent->mm;
	new->parent = gmap_get(parent);
	new->orig_asce = asce;
	new->edat_level = edat_level;
	new->initialized = false;
	spin_lock(&parent->shadow_lock);
	/* Recheck if another CPU created the same shadow */
	sg = gmap_find_shadow(parent, asce, edat_level);
	if (sg) {
		spin_unlock(&parent->shadow_lock);
		gmap_free(new);
		return sg;
	}
	if (asce & _ASCE_REAL_SPACE) {
		/* only allow one real-space gmap shadow */
		list_for_each_entry(sg, &parent->children, list) {
			if (sg->orig_asce & _ASCE_REAL_SPACE) {
				spin_lock(&sg->guest_table_lock);
				gmap_unshadow(sg);
				spin_unlock(&sg->guest_table_lock);
				list_del(&sg->list);
				gmap_put(sg);
				break;
			}
		}
	}
	refcount_set(&new->ref_count, 2);
	list_add(&new->list, &parent->children);
	if (asce & _ASCE_REAL_SPACE) {
		/* nothing to protect, return right away */
		new->initialized = true;
		spin_unlock(&parent->shadow_lock);
		return new;
	}
	spin_unlock(&parent->shadow_lock);
	/* protect after insertion, so it will get properly invalidated */
	mmap_read_lock(parent->mm);
	rc = gmap_protect_range(parent, asce & _ASCE_ORIGIN,
				((asce & _ASCE_TABLE_LENGTH) + 1) * PAGE_SIZE,
				PROT_READ, GMAP_NOTIFY_SHADOW);
	mmap_read_unlock(parent->mm);
	spin_lock(&parent->shadow_lock);
	new->initialized = true;
	if (rc) {
		list_del(&new->list);
		gmap_free(new);
		new = ERR_PTR(rc);
	}
	spin_unlock(&parent->shadow_lock);
	return new;
}
EXPORT_SYMBOL_GPL(gmap_shadow);

/**
 * gmap_shadow_r2t - create an empty shadow region 2 table
 * @sg: pointer to the shadow guest address space structure
 * @saddr: faulting address in the shadow gmap
 * @r2t: parent gmap address of the region 2 table to get shadowed
 * @fake: r2t references contiguous guest memory block, not a r2t
 *
 * The r2t parameter specifies the address of the source table. The
 * four pages of the source table are made read-only in the parent gmap
 * address space. A write to the source table area @r2t will automatically
 * remove the shadow r2 table and all of its decendents.
 *
 * Returns 0 if successfully shadowed or already shadowed, -EAGAIN if the
 * shadow table structure is incomplete, -ENOMEM if out of memory and
 * -EFAULT if an address in the parent gmap could not be resolved.
 *
 * Called with sg->mm->mmap_lock in read.
 */
int gmap_shadow_r2t(struct gmap *sg, unsigned long saddr, unsigned long r2t,
		    int fake)
{
	unsigned long raddr, origin, offset, len;
	unsigned long *s_r2t, *table;
	struct page *page;
	int rc;

	BUG_ON(!gmap_is_shadow(sg));
	/* Allocate a shadow region second table */
	page = alloc_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (!page)
		return -ENOMEM;
	page->index = r2t & _REGION_ENTRY_ORIGIN;
	if (fake)
		page->index |= GMAP_SHADOW_FAKE_TABLE;
	s_r2t = (unsigned long *) page_to_phys(page);
	/* Install shadow region second table */
	spin_lock(&sg->guest_table_lock);
	table = gmap_table_walk(sg, saddr, 4); /* get region-1 pointer */
	if (!table) {
		rc = -EAGAIN;		/* Race with unshadow */
		goto out_free;
	}
	if (!(*table & _REGION_ENTRY_INVALID)) {
		rc = 0;			/* Already established */
		goto out_free;
	} else if (*table & _REGION_ENTRY_ORIGIN) {
		rc = -EAGAIN;		/* Race with shadow */
		goto out_free;
	}
	crst_table_init(s_r2t, _REGION2_ENTRY_EMPTY);
	/* mark as invalid as long as the parent table is not protected */
	*table = (unsigned long) s_r2t | _REGION_ENTRY_LENGTH |
		 _REGION_ENTRY_TYPE_R1 | _REGION_ENTRY_INVALID;
	if (sg->edat_level >= 1)
		*table |= (r2t & _REGION_ENTRY_PROTECT);
	list_add(&page->lru, &sg->crst_list);
	if (fake) {
		/* nothing to protect for fake tables */
		*table &= ~_REGION_ENTRY_INVALID;
		spin_unlock(&sg->guest_table_lock);
		return 0;
	}
	spin_unlock(&sg->guest_table_lock);
	/* Make r2t read-only in parent gmap page table */
	raddr = (saddr & _REGION1_MASK) | _SHADOW_RMAP_REGION1;
	origin = r2t & _REGION_ENTRY_ORIGIN;
	offset = ((r2t & _REGION_ENTRY_OFFSET) >> 6) * PAGE_SIZE;
	len = ((r2t & _REGION_ENTRY_LENGTH) + 1) * PAGE_SIZE - offset;
	rc = gmap_protect_rmap(sg, raddr, origin + offset, len);
	spin_lock(&sg->guest_table_lock);
	if (!rc) {
		table = gmap_table_walk(sg, saddr, 4);
		if (!table || (*table & _REGION_ENTRY_ORIGIN) !=
			      (unsigned long) s_r2t)
			rc = -EAGAIN;		/* Race with unshadow */
		else
			*table &= ~_REGION_ENTRY_INVALID;
	} else {
		gmap_unshadow_r2t(sg, raddr);
	}
	spin_unlock(&sg->guest_table_lock);
	return rc;
out_free:
	spin_unlock(&sg->guest_table_lock);
	__free_pages(page, CRST_ALLOC_ORDER);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_shadow_r2t);

/**
 * gmap_shadow_r3t - create a shadow region 3 table
 * @sg: pointer to the shadow guest address space structure
 * @saddr: faulting address in the shadow gmap
 * @r3t: parent gmap address of the region 3 table to get shadowed
 * @fake: r3t references contiguous guest memory block, not a r3t
 *
 * Returns 0 if successfully shadowed or already shadowed, -EAGAIN if the
 * shadow table structure is incomplete, -ENOMEM if out of memory and
 * -EFAULT if an address in the parent gmap could not be resolved.
 *
 * Called with sg->mm->mmap_lock in read.
 */
int gmap_shadow_r3t(struct gmap *sg, unsigned long saddr, unsigned long r3t,
		    int fake)
{
	unsigned long raddr, origin, offset, len;
	unsigned long *s_r3t, *table;
	struct page *page;
	int rc;

	BUG_ON(!gmap_is_shadow(sg));
	/* Allocate a shadow region second table */
	page = alloc_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (!page)
		return -ENOMEM;
	page->index = r3t & _REGION_ENTRY_ORIGIN;
	if (fake)
		page->index |= GMAP_SHADOW_FAKE_TABLE;
	s_r3t = (unsigned long *) page_to_phys(page);
	/* Install shadow region second table */
	spin_lock(&sg->guest_table_lock);
	table = gmap_table_walk(sg, saddr, 3); /* get region-2 pointer */
	if (!table) {
		rc = -EAGAIN;		/* Race with unshadow */
		goto out_free;
	}
	if (!(*table & _REGION_ENTRY_INVALID)) {
		rc = 0;			/* Already established */
		goto out_free;
	} else if (*table & _REGION_ENTRY_ORIGIN) {
		rc = -EAGAIN;		/* Race with shadow */
		goto out_free;
	}
	crst_table_init(s_r3t, _REGION3_ENTRY_EMPTY);
	/* mark as invalid as long as the parent table is not protected */
	*table = (unsigned long) s_r3t | _REGION_ENTRY_LENGTH |
		 _REGION_ENTRY_TYPE_R2 | _REGION_ENTRY_INVALID;
	if (sg->edat_level >= 1)
		*table |= (r3t & _REGION_ENTRY_PROTECT);
	list_add(&page->lru, &sg->crst_list);
	if (fake) {
		/* nothing to protect for fake tables */
		*table &= ~_REGION_ENTRY_INVALID;
		spin_unlock(&sg->guest_table_lock);
		return 0;
	}
	spin_unlock(&sg->guest_table_lock);
	/* Make r3t read-only in parent gmap page table */
	raddr = (saddr & _REGION2_MASK) | _SHADOW_RMAP_REGION2;
	origin = r3t & _REGION_ENTRY_ORIGIN;
	offset = ((r3t & _REGION_ENTRY_OFFSET) >> 6) * PAGE_SIZE;
	len = ((r3t & _REGION_ENTRY_LENGTH) + 1) * PAGE_SIZE - offset;
	rc = gmap_protect_rmap(sg, raddr, origin + offset, len);
	spin_lock(&sg->guest_table_lock);
	if (!rc) {
		table = gmap_table_walk(sg, saddr, 3);
		if (!table || (*table & _REGION_ENTRY_ORIGIN) !=
			      (unsigned long) s_r3t)
			rc = -EAGAIN;		/* Race with unshadow */
		else
			*table &= ~_REGION_ENTRY_INVALID;
	} else {
		gmap_unshadow_r3t(sg, raddr);
	}
	spin_unlock(&sg->guest_table_lock);
	return rc;
out_free:
	spin_unlock(&sg->guest_table_lock);
	__free_pages(page, CRST_ALLOC_ORDER);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_shadow_r3t);

/**
 * gmap_shadow_sgt - create a shadow segment table
 * @sg: pointer to the shadow guest address space structure
 * @saddr: faulting address in the shadow gmap
 * @sgt: parent gmap address of the segment table to get shadowed
 * @fake: sgt references contiguous guest memory block, not a sgt
 *
 * Returns: 0 if successfully shadowed or already shadowed, -EAGAIN if the
 * shadow table structure is incomplete, -ENOMEM if out of memory and
 * -EFAULT if an address in the parent gmap could not be resolved.
 *
 * Called with sg->mm->mmap_lock in read.
 */
int gmap_shadow_sgt(struct gmap *sg, unsigned long saddr, unsigned long sgt,
		    int fake)
{
	unsigned long raddr, origin, offset, len;
	unsigned long *s_sgt, *table;
	struct page *page;
	int rc;

	BUG_ON(!gmap_is_shadow(sg) || (sgt & _REGION3_ENTRY_LARGE));
	/* Allocate a shadow segment table */
	page = alloc_pages(GFP_KERNEL, CRST_ALLOC_ORDER);
	if (!page)
		return -ENOMEM;
	page->index = sgt & _REGION_ENTRY_ORIGIN;
	if (fake)
		page->index |= GMAP_SHADOW_FAKE_TABLE;
	s_sgt = (unsigned long *) page_to_phys(page);
	/* Install shadow region second table */
	spin_lock(&sg->guest_table_lock);
	table = gmap_table_walk(sg, saddr, 2); /* get region-3 pointer */
	if (!table) {
		rc = -EAGAIN;		/* Race with unshadow */
		goto out_free;
	}
	if (!(*table & _REGION_ENTRY_INVALID)) {
		rc = 0;			/* Already established */
		goto out_free;
	} else if (*table & _REGION_ENTRY_ORIGIN) {
		rc = -EAGAIN;		/* Race with shadow */
		goto out_free;
	}
	crst_table_init(s_sgt, _SEGMENT_ENTRY_EMPTY);
	/* mark as invalid as long as the parent table is not protected */
	*table = (unsigned long) s_sgt | _REGION_ENTRY_LENGTH |
		 _REGION_ENTRY_TYPE_R3 | _REGION_ENTRY_INVALID;
	if (sg->edat_level >= 1)
		*table |= sgt & _REGION_ENTRY_PROTECT;
	list_add(&page->lru, &sg->crst_list);
	if (fake) {
		/* nothing to protect for fake tables */
		*table &= ~_REGION_ENTRY_INVALID;
		spin_unlock(&sg->guest_table_lock);
		return 0;
	}
	spin_unlock(&sg->guest_table_lock);
	/* Make sgt read-only in parent gmap page table */
	raddr = (saddr & _REGION3_MASK) | _SHADOW_RMAP_REGION3;
	origin = sgt & _REGION_ENTRY_ORIGIN;
	offset = ((sgt & _REGION_ENTRY_OFFSET) >> 6) * PAGE_SIZE;
	len = ((sgt & _REGION_ENTRY_LENGTH) + 1) * PAGE_SIZE - offset;
	rc = gmap_protect_rmap(sg, raddr, origin + offset, len);
	spin_lock(&sg->guest_table_lock);
	if (!rc) {
		table = gmap_table_walk(sg, saddr, 2);
		if (!table || (*table & _REGION_ENTRY_ORIGIN) !=
			      (unsigned long) s_sgt)
			rc = -EAGAIN;		/* Race with unshadow */
		else
			*table &= ~_REGION_ENTRY_INVALID;
	} else {
		gmap_unshadow_sgt(sg, raddr);
	}
	spin_unlock(&sg->guest_table_lock);
	return rc;
out_free:
	spin_unlock(&sg->guest_table_lock);
	__free_pages(page, CRST_ALLOC_ORDER);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_shadow_sgt);

/**
 * gmap_shadow_lookup_pgtable - find a shadow page table
 * @sg: pointer to the shadow guest address space structure
 * @saddr: the address in the shadow aguest address space
 * @pgt: parent gmap address of the page table to get shadowed
 * @dat_protection: if the pgtable is marked as protected by dat
 * @fake: pgt references contiguous guest memory block, not a pgtable
 *
 * Returns 0 if the shadow page table was found and -EAGAIN if the page
 * table was not found.
 *
 * Called with sg->mm->mmap_lock in read.
 */
int gmap_shadow_pgt_lookup(struct gmap *sg, unsigned long saddr,
			   unsigned long *pgt, int *dat_protection,
			   int *fake)
{
	unsigned long *table;
	struct page *page;
	int rc;

	BUG_ON(!gmap_is_shadow(sg));
	spin_lock(&sg->guest_table_lock);
	table = gmap_table_walk(sg, saddr, 1); /* get segment pointer */
	if (table && !(*table & _SEGMENT_ENTRY_INVALID)) {
		/* Shadow page tables are full pages (pte+pgste) */
		page = pfn_to_page(*table >> PAGE_SHIFT);
		*pgt = page->index & ~GMAP_SHADOW_FAKE_TABLE;
		*dat_protection = !!(*table & _SEGMENT_ENTRY_PROTECT);
		*fake = !!(page->index & GMAP_SHADOW_FAKE_TABLE);
		rc = 0;
	} else  {
		rc = -EAGAIN;
	}
	spin_unlock(&sg->guest_table_lock);
	return rc;

}
EXPORT_SYMBOL_GPL(gmap_shadow_pgt_lookup);

/**
 * gmap_shadow_pgt - instantiate a shadow page table
 * @sg: pointer to the shadow guest address space structure
 * @saddr: faulting address in the shadow gmap
 * @pgt: parent gmap address of the page table to get shadowed
 * @fake: pgt references contiguous guest memory block, not a pgtable
 *
 * Returns 0 if successfully shadowed or already shadowed, -EAGAIN if the
 * shadow table structure is incomplete, -ENOMEM if out of memory,
 * -EFAULT if an address in the parent gmap could not be resolved and
 *
 * Called with gmap->mm->mmap_lock in read
 */
int gmap_shadow_pgt(struct gmap *sg, unsigned long saddr, unsigned long pgt,
		    int fake)
{
	unsigned long raddr, origin;
	unsigned long *s_pgt, *table;
	struct page *page;
	int rc;

	BUG_ON(!gmap_is_shadow(sg) || (pgt & _SEGMENT_ENTRY_LARGE));
	/* Allocate a shadow page table */
	page = page_table_alloc_pgste(sg->mm);
	if (!page)
		return -ENOMEM;
	page->index = pgt & _SEGMENT_ENTRY_ORIGIN;
	if (fake)
		page->index |= GMAP_SHADOW_FAKE_TABLE;
	s_pgt = (unsigned long *) page_to_phys(page);
	/* Install shadow page table */
	spin_lock(&sg->guest_table_lock);
	table = gmap_table_walk(sg, saddr, 1); /* get segment pointer */
	if (!table) {
		rc = -EAGAIN;		/* Race with unshadow */
		goto out_free;
	}
	if (!(*table & _SEGMENT_ENTRY_INVALID)) {
		rc = 0;			/* Already established */
		goto out_free;
	} else if (*table & _SEGMENT_ENTRY_ORIGIN) {
		rc = -EAGAIN;		/* Race with shadow */
		goto out_free;
	}
	/* mark as invalid as long as the parent table is not protected */
	*table = (unsigned long) s_pgt | _SEGMENT_ENTRY |
		 (pgt & _SEGMENT_ENTRY_PROTECT) | _SEGMENT_ENTRY_INVALID;
	list_add(&page->lru, &sg->pt_list);
	if (fake) {
		/* nothing to protect for fake tables */
		*table &= ~_SEGMENT_ENTRY_INVALID;
		spin_unlock(&sg->guest_table_lock);
		return 0;
	}
	spin_unlock(&sg->guest_table_lock);
	/* Make pgt read-only in parent gmap page table (not the pgste) */
	raddr = (saddr & _SEGMENT_MASK) | _SHADOW_RMAP_SEGMENT;
	origin = pgt & _SEGMENT_ENTRY_ORIGIN & PAGE_MASK;
	rc = gmap_protect_rmap(sg, raddr, origin, PAGE_SIZE);
	spin_lock(&sg->guest_table_lock);
	if (!rc) {
		table = gmap_table_walk(sg, saddr, 1);
		if (!table || (*table & _SEGMENT_ENTRY_ORIGIN) !=
			      (unsigned long) s_pgt)
			rc = -EAGAIN;		/* Race with unshadow */
		else
			*table &= ~_SEGMENT_ENTRY_INVALID;
	} else {
		gmap_unshadow_pgt(sg, raddr);
	}
	spin_unlock(&sg->guest_table_lock);
	return rc;
out_free:
	spin_unlock(&sg->guest_table_lock);
	page_table_free_pgste(page);
	return rc;

}
EXPORT_SYMBOL_GPL(gmap_shadow_pgt);

/**
 * gmap_shadow_page - create a shadow page mapping
 * @sg: pointer to the shadow guest address space structure
 * @saddr: faulting address in the shadow gmap
 * @pte: pte in parent gmap address space to get shadowed
 *
 * Returns 0 if successfully shadowed or already shadowed, -EAGAIN if the
 * shadow table structure is incomplete, -ENOMEM if out of memory and
 * -EFAULT if an address in the parent gmap could not be resolved.
 *
 * Called with sg->mm->mmap_lock in read.
 */
int gmap_shadow_page(struct gmap *sg, unsigned long saddr, pte_t pte)
{
	struct gmap *parent;
	struct gmap_rmap *rmap;
	unsigned long vmaddr, paddr;
	spinlock_t *ptl;
	pte_t *sptep, *tptep;
	int prot;
	int rc;

	BUG_ON(!gmap_is_shadow(sg));
	parent = sg->parent;
	prot = (pte_val(pte) & _PAGE_PROTECT) ? PROT_READ : PROT_WRITE;

	rmap = kzalloc(sizeof(*rmap), GFP_KERNEL);
	if (!rmap)
		return -ENOMEM;
	rmap->raddr = (saddr & PAGE_MASK) | _SHADOW_RMAP_PGTABLE;

	while (1) {
		paddr = pte_val(pte) & PAGE_MASK;
		vmaddr = __gmap_translate(parent, paddr);
		if (IS_ERR_VALUE(vmaddr)) {
			rc = vmaddr;
			break;
		}
		rc = radix_tree_preload(GFP_KERNEL);
		if (rc)
			break;
		rc = -EAGAIN;
		sptep = gmap_pte_op_walk(parent, paddr, &ptl);
		if (sptep) {
			spin_lock(&sg->guest_table_lock);
			/* Get page table pointer */
			tptep = (pte_t *) gmap_table_walk(sg, saddr, 0);
			if (!tptep) {
				spin_unlock(&sg->guest_table_lock);
				gmap_pte_op_end(ptl);
				radix_tree_preload_end();
				break;
			}
			rc = ptep_shadow_pte(sg->mm, saddr, sptep, tptep, pte);
			if (rc > 0) {
				/* Success and a new mapping */
				gmap_insert_rmap(sg, vmaddr, rmap);
				rmap = NULL;
				rc = 0;
			}
			gmap_pte_op_end(ptl);
			spin_unlock(&sg->guest_table_lock);
		}
		radix_tree_preload_end();
		if (!rc)
			break;
		rc = gmap_pte_op_fixup(parent, paddr, vmaddr, prot);
		if (rc)
			break;
	}
	kfree(rmap);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_shadow_page);

/**
 * gmap_shadow_notify - handle notifications for shadow gmap
 *
 * Called with sg->parent->shadow_lock.
 */
static void gmap_shadow_notify(struct gmap *sg, unsigned long vmaddr,
			       unsigned long gaddr)
{
	struct gmap_rmap *rmap, *rnext, *head;
	unsigned long start, end, bits, raddr;

	BUG_ON(!gmap_is_shadow(sg));

	spin_lock(&sg->guest_table_lock);
	if (sg->removed) {
		spin_unlock(&sg->guest_table_lock);
		return;
	}
	/* Check for top level table */
	start = sg->orig_asce & _ASCE_ORIGIN;
	end = start + ((sg->orig_asce & _ASCE_TABLE_LENGTH) + 1) * PAGE_SIZE;
	if (!(sg->orig_asce & _ASCE_REAL_SPACE) && gaddr >= start &&
	    gaddr < end) {
		/* The complete shadow table has to go */
		gmap_unshadow(sg);
		spin_unlock(&sg->guest_table_lock);
		list_del(&sg->list);
		gmap_put(sg);
		return;
	}
	/* Remove the page table tree from on specific entry */
	head = radix_tree_delete(&sg->host_to_rmap, vmaddr >> PAGE_SHIFT);
	gmap_for_each_rmap_safe(rmap, rnext, head) {
		bits = rmap->raddr & _SHADOW_RMAP_MASK;
		raddr = rmap->raddr ^ bits;
		switch (bits) {
		case _SHADOW_RMAP_REGION1:
			gmap_unshadow_r2t(sg, raddr);
			break;
		case _SHADOW_RMAP_REGION2:
			gmap_unshadow_r3t(sg, raddr);
			break;
		case _SHADOW_RMAP_REGION3:
			gmap_unshadow_sgt(sg, raddr);
			break;
		case _SHADOW_RMAP_SEGMENT:
			gmap_unshadow_pgt(sg, raddr);
			break;
		case _SHADOW_RMAP_PGTABLE:
			gmap_unshadow_page(sg, raddr);
			break;
		}
		kfree(rmap);
	}
	spin_unlock(&sg->guest_table_lock);
}

/**
 * ptep_notify - call all invalidation callbacks for a specific pte.
 * @mm: pointer to the process mm_struct
 * @addr: virtual address in the process address space
 * @pte: pointer to the page table entry
 * @bits: bits from the pgste that caused the notify call
 *
 * This function is assumed to be called with the page table lock held
 * for the pte to notify.
 */
void ptep_notify(struct mm_struct *mm, unsigned long vmaddr,
		 pte_t *pte, unsigned long bits)
{
	unsigned long offset, gaddr = 0;
	unsigned long *table;
	struct gmap *gmap, *sg, *next;

	offset = ((unsigned long) pte) & (255 * sizeof(pte_t));
	offset = offset * (PAGE_SIZE / sizeof(pte_t));
	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		spin_lock(&gmap->guest_table_lock);
		table = radix_tree_lookup(&gmap->host_to_guest,
					  vmaddr >> PMD_SHIFT);
		if (table)
			gaddr = __gmap_segment_gaddr(table) + offset;
		spin_unlock(&gmap->guest_table_lock);
		if (!table)
			continue;

		if (!list_empty(&gmap->children) && (bits & PGSTE_VSIE_BIT)) {
			spin_lock(&gmap->shadow_lock);
			list_for_each_entry_safe(sg, next,
						 &gmap->children, list)
				gmap_shadow_notify(sg, vmaddr, gaddr);
			spin_unlock(&gmap->shadow_lock);
		}
		if (bits & PGSTE_IN_BIT)
			gmap_call_notifier(gmap, gaddr, gaddr + PAGE_SIZE - 1);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ptep_notify);

static void pmdp_notify_gmap(struct gmap *gmap, pmd_t *pmdp,
			     unsigned long gaddr)
{
	pmd_val(*pmdp) &= ~_SEGMENT_ENTRY_GMAP_IN;
	gmap_call_notifier(gmap, gaddr, gaddr + HPAGE_SIZE - 1);
}

/**
 * gmap_pmdp_xchg - exchange a gmap pmd with another
 * @gmap: pointer to the guest address space structure
 * @pmdp: pointer to the pmd entry
 * @new: replacement entry
 * @gaddr: the affected guest address
 *
 * This function is assumed to be called with the guest_table_lock
 * held.
 */
static void gmap_pmdp_xchg(struct gmap *gmap, pmd_t *pmdp, pmd_t new,
			   unsigned long gaddr)
{
	gaddr &= HPAGE_MASK;
	pmdp_notify_gmap(gmap, pmdp, gaddr);
	pmd_val(new) &= ~_SEGMENT_ENTRY_GMAP_IN;
	if (MACHINE_HAS_TLB_GUEST)
		__pmdp_idte(gaddr, (pmd_t *)pmdp, IDTE_GUEST_ASCE, gmap->asce,
			    IDTE_GLOBAL);
	else if (MACHINE_HAS_IDTE)
		__pmdp_idte(gaddr, (pmd_t *)pmdp, 0, 0, IDTE_GLOBAL);
	else
		__pmdp_csp(pmdp);
	*pmdp = new;
}

static void gmap_pmdp_clear(struct mm_struct *mm, unsigned long vmaddr,
			    int purge)
{
	pmd_t *pmdp;
	struct gmap *gmap;
	unsigned long gaddr;

	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		spin_lock(&gmap->guest_table_lock);
		pmdp = (pmd_t *)radix_tree_delete(&gmap->host_to_guest,
						  vmaddr >> PMD_SHIFT);
		if (pmdp) {
			gaddr = __gmap_segment_gaddr((unsigned long *)pmdp);
			pmdp_notify_gmap(gmap, pmdp, gaddr);
			WARN_ON(pmd_val(*pmdp) & ~(_SEGMENT_ENTRY_HARDWARE_BITS_LARGE |
						   _SEGMENT_ENTRY_GMAP_UC));
			if (purge)
				__pmdp_csp(pmdp);
			pmd_val(*pmdp) = _SEGMENT_ENTRY_EMPTY;
		}
		spin_unlock(&gmap->guest_table_lock);
	}
	rcu_read_unlock();
}

/**
 * gmap_pmdp_invalidate - invalidate all affected guest pmd entries without
 *                        flushing
 * @mm: pointer to the process mm_struct
 * @vmaddr: virtual address in the process address space
 */
void gmap_pmdp_invalidate(struct mm_struct *mm, unsigned long vmaddr)
{
	gmap_pmdp_clear(mm, vmaddr, 0);
}
EXPORT_SYMBOL_GPL(gmap_pmdp_invalidate);

/**
 * gmap_pmdp_csp - csp all affected guest pmd entries
 * @mm: pointer to the process mm_struct
 * @vmaddr: virtual address in the process address space
 */
void gmap_pmdp_csp(struct mm_struct *mm, unsigned long vmaddr)
{
	gmap_pmdp_clear(mm, vmaddr, 1);
}
EXPORT_SYMBOL_GPL(gmap_pmdp_csp);

/**
 * gmap_pmdp_idte_local - invalidate and clear a guest pmd entry
 * @mm: pointer to the process mm_struct
 * @vmaddr: virtual address in the process address space
 */
void gmap_pmdp_idte_local(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *entry, gaddr;
	struct gmap *gmap;
	pmd_t *pmdp;

	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		spin_lock(&gmap->guest_table_lock);
		entry = radix_tree_delete(&gmap->host_to_guest,
					  vmaddr >> PMD_SHIFT);
		if (entry) {
			pmdp = (pmd_t *)entry;
			gaddr = __gmap_segment_gaddr(entry);
			pmdp_notify_gmap(gmap, pmdp, gaddr);
			WARN_ON(*entry & ~(_SEGMENT_ENTRY_HARDWARE_BITS_LARGE |
					   _SEGMENT_ENTRY_GMAP_UC));
			if (MACHINE_HAS_TLB_GUEST)
				__pmdp_idte(gaddr, pmdp, IDTE_GUEST_ASCE,
					    gmap->asce, IDTE_LOCAL);
			else if (MACHINE_HAS_IDTE)
				__pmdp_idte(gaddr, pmdp, 0, 0, IDTE_LOCAL);
			*entry = _SEGMENT_ENTRY_EMPTY;
		}
		spin_unlock(&gmap->guest_table_lock);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(gmap_pmdp_idte_local);

/**
 * gmap_pmdp_idte_global - invalidate and clear a guest pmd entry
 * @mm: pointer to the process mm_struct
 * @vmaddr: virtual address in the process address space
 */
void gmap_pmdp_idte_global(struct mm_struct *mm, unsigned long vmaddr)
{
	unsigned long *entry, gaddr;
	struct gmap *gmap;
	pmd_t *pmdp;

	rcu_read_lock();
	list_for_each_entry_rcu(gmap, &mm->context.gmap_list, list) {
		spin_lock(&gmap->guest_table_lock);
		entry = radix_tree_delete(&gmap->host_to_guest,
					  vmaddr >> PMD_SHIFT);
		if (entry) {
			pmdp = (pmd_t *)entry;
			gaddr = __gmap_segment_gaddr(entry);
			pmdp_notify_gmap(gmap, pmdp, gaddr);
			WARN_ON(*entry & ~(_SEGMENT_ENTRY_HARDWARE_BITS_LARGE |
					   _SEGMENT_ENTRY_GMAP_UC));
			if (MACHINE_HAS_TLB_GUEST)
				__pmdp_idte(gaddr, pmdp, IDTE_GUEST_ASCE,
					    gmap->asce, IDTE_GLOBAL);
			else if (MACHINE_HAS_IDTE)
				__pmdp_idte(gaddr, pmdp, 0, 0, IDTE_GLOBAL);
			else
				__pmdp_csp(pmdp);
			*entry = _SEGMENT_ENTRY_EMPTY;
		}
		spin_unlock(&gmap->guest_table_lock);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(gmap_pmdp_idte_global);

/**
 * gmap_test_and_clear_dirty_pmd - test and reset segment dirty status
 * @gmap: pointer to guest address space
 * @pmdp: pointer to the pmd to be tested
 * @gaddr: virtual address in the guest address space
 *
 * This function is assumed to be called with the guest_table_lock
 * held.
 */
static bool gmap_test_and_clear_dirty_pmd(struct gmap *gmap, pmd_t *pmdp,
					  unsigned long gaddr)
{
	if (pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID)
		return false;

	/* Already protected memory, which did not change is clean */
	if (pmd_val(*pmdp) & _SEGMENT_ENTRY_PROTECT &&
	    !(pmd_val(*pmdp) & _SEGMENT_ENTRY_GMAP_UC))
		return false;

	/* Clear UC indication and reset protection */
	pmd_val(*pmdp) &= ~_SEGMENT_ENTRY_GMAP_UC;
	gmap_protect_pmd(gmap, gaddr, pmdp, PROT_READ, 0);
	return true;
}

/**
 * gmap_sync_dirty_log_pmd - set bitmap based on dirty status of segment
 * @gmap: pointer to guest address space
 * @bitmap: dirty bitmap for this pmd
 * @gaddr: virtual address in the guest address space
 * @vmaddr: virtual address in the host address space
 *
 * This function is assumed to be called with the guest_table_lock
 * held.
 */
void gmap_sync_dirty_log_pmd(struct gmap *gmap, unsigned long bitmap[4],
			     unsigned long gaddr, unsigned long vmaddr)
{
	int i;
	pmd_t *pmdp;
	pte_t *ptep;
	spinlock_t *ptl;

	pmdp = gmap_pmd_op_walk(gmap, gaddr);
	if (!pmdp)
		return;

	if (pmd_large(*pmdp)) {
		if (gmap_test_and_clear_dirty_pmd(gmap, pmdp, gaddr))
			bitmap_fill(bitmap, _PAGE_ENTRIES);
	} else {
		for (i = 0; i < _PAGE_ENTRIES; i++, vmaddr += PAGE_SIZE) {
			ptep = pte_alloc_map_lock(gmap->mm, pmdp, vmaddr, &ptl);
			if (!ptep)
				continue;
			if (ptep_test_and_clear_uc(gmap->mm, vmaddr, ptep))
				set_bit(i, bitmap);
			spin_unlock(ptl);
		}
	}
	gmap_pmd_op_end(gmap, pmdp);
}
EXPORT_SYMBOL_GPL(gmap_sync_dirty_log_pmd);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int thp_split_walk_pmd_entry(pmd_t *pmd, unsigned long addr,
				    unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;

	split_huge_pmd(vma, pmd, addr);
	return 0;
}

static const struct mm_walk_ops thp_split_walk_ops = {
	.pmd_entry	= thp_split_walk_pmd_entry,
};

static inline void thp_split_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	for (vma = mm->mmap; vma != NULL; vma = vma->vm_next) {
		vma->vm_flags &= ~VM_HUGEPAGE;
		vma->vm_flags |= VM_NOHUGEPAGE;
		walk_page_vma(vma, &thp_split_walk_ops, NULL);
	}
	mm->def_flags |= VM_NOHUGEPAGE;
}
#else
static inline void thp_split_mm(struct mm_struct *mm)
{
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * Remove all empty zero pages from the mapping for lazy refaulting
 * - This must be called after mm->context.has_pgste is set, to avoid
 *   future creation of zero pages
 * - This must be called after THP was enabled
 */
static int __zap_zero_pages(pmd_t *pmd, unsigned long start,
			   unsigned long end, struct mm_walk *walk)
{
	unsigned long addr;

	for (addr = start; addr != end; addr += PAGE_SIZE) {
		pte_t *ptep;
		spinlock_t *ptl;

		ptep = pte_offset_map_lock(walk->mm, pmd, addr, &ptl);
		if (is_zero_pfn(pte_pfn(*ptep)))
			ptep_xchg_direct(walk->mm, addr, ptep, __pte(_PAGE_INVALID));
		pte_unmap_unlock(ptep, ptl);
	}
	return 0;
}

static const struct mm_walk_ops zap_zero_walk_ops = {
	.pmd_entry	= __zap_zero_pages,
};

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
	mmap_write_lock(mm);
	mm->context.has_pgste = 1;
	/* split thp mappings and disable thp for future mappings */
	thp_split_mm(mm);
	walk_page_range(mm, 0, TASK_SIZE, &zap_zero_walk_ops, NULL);
	mmap_write_unlock(mm);
	return 0;
}
EXPORT_SYMBOL_GPL(s390_enable_sie);

int gmap_mark_unmergeable(void)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int ret;

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		ret = ksm_madvise(vma, vma->vm_start, vma->vm_end,
				  MADV_UNMERGEABLE, &vma->vm_flags);
		if (ret)
			return ret;
	}
	mm->def_flags &= ~VM_MERGEABLE;
	return 0;
}
EXPORT_SYMBOL_GPL(gmap_mark_unmergeable);

/*
 * Enable storage key handling from now on and initialize the storage
 * keys with the default key.
 */
static int __s390_enable_skey_pte(pte_t *pte, unsigned long addr,
				  unsigned long next, struct mm_walk *walk)
{
	/* Clear storage key */
	ptep_zap_key(walk->mm, addr, pte);
	return 0;
}

/*
 * Give a chance to schedule after setting a key to 256 pages.
 * We only hold the mm lock, which is a rwsem and the kvm srcu.
 * Both can sleep.
 */
static int __s390_enable_skey_pmd(pmd_t *pmd, unsigned long addr,
				  unsigned long next, struct mm_walk *walk)
{
	cond_resched();
	return 0;
}

static int __s390_enable_skey_hugetlb(pte_t *pte, unsigned long addr,
				      unsigned long hmask, unsigned long next,
				      struct mm_walk *walk)
{
	pmd_t *pmd = (pmd_t *)pte;
	unsigned long start, end;
	struct page *page = pmd_page(*pmd);

	/*
	 * The write check makes sure we do not set a key on shared
	 * memory. This is needed as the walker does not differentiate
	 * between actual guest memory and the process executable or
	 * shared libraries.
	 */
	if (pmd_val(*pmd) & _SEGMENT_ENTRY_INVALID ||
	    !(pmd_val(*pmd) & _SEGMENT_ENTRY_WRITE))
		return 0;

	start = pmd_val(*pmd) & HPAGE_MASK;
	end = start + HPAGE_SIZE - 1;
	__storage_key_init_range(start, end);
	set_bit(PG_arch_1, &page->flags);
	cond_resched();
	return 0;
}

static const struct mm_walk_ops enable_skey_walk_ops = {
	.hugetlb_entry		= __s390_enable_skey_hugetlb,
	.pte_entry		= __s390_enable_skey_pte,
	.pmd_entry		= __s390_enable_skey_pmd,
};

int s390_enable_skey(void)
{
	struct mm_struct *mm = current->mm;
	int rc = 0;

	mmap_write_lock(mm);
	if (mm_uses_skeys(mm))
		goto out_up;

	mm->context.uses_skeys = 1;
	rc = gmap_mark_unmergeable();
	if (rc) {
		mm->context.uses_skeys = 0;
		goto out_up;
	}
	walk_page_range(mm, 0, TASK_SIZE, &enable_skey_walk_ops, NULL);

out_up:
	mmap_write_unlock(mm);
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

static const struct mm_walk_ops reset_cmma_walk_ops = {
	.pte_entry		= __s390_reset_cmma,
};

void s390_reset_cmma(struct mm_struct *mm)
{
	mmap_write_lock(mm);
	walk_page_range(mm, 0, TASK_SIZE, &reset_cmma_walk_ops, NULL);
	mmap_write_unlock(mm);
}
EXPORT_SYMBOL_GPL(s390_reset_cmma);

/*
 * make inaccessible pages accessible again
 */
static int __s390_reset_acc(pte_t *ptep, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	pte_t pte = READ_ONCE(*ptep);

	if (pte_present(pte))
		WARN_ON_ONCE(uv_destroy_page(pte_val(pte) & PAGE_MASK));
	return 0;
}

static const struct mm_walk_ops reset_acc_walk_ops = {
	.pte_entry		= __s390_reset_acc,
};

#include <linux/sched/mm.h>
void s390_reset_acc(struct mm_struct *mm)
{
	if (!mm_is_protected(mm))
		return;
	/*
	 * we might be called during
	 * reset:                             we walk the pages and clear
	 * close of all kvm file descriptors: we walk the pages and clear
	 * exit of process on fd closure:     vma already gone, do nothing
	 */
	if (!mmget_not_zero(mm))
		return;
	mmap_read_lock(mm);
	walk_page_range(mm, 0, TASK_SIZE, &reset_acc_walk_ops, NULL);
	mmap_read_unlock(mm);
	mmput(mm);
}
EXPORT_SYMBOL_GPL(s390_reset_acc);
