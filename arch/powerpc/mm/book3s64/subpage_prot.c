// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2007-2008 Paul Mackerras, IBM Corp.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/pagewalk.h>
#include <linux/hugetlb.h>
#include <linux/syscalls.h>

#include <linux/pgtable.h>
#include <linux/uaccess.h>

/*
 * Free all pages allocated for subpage protection maps and pointers.
 * Also makes sure that the subpage_prot_table structure is
 * reinitialized for the next user.
 */
void subpage_prot_free(struct mm_struct *mm)
{
	struct subpage_prot_table *spt = mm_ctx_subpage_prot(&mm->context);
	unsigned long i, j, addr;
	u32 **p;

	if (!spt)
		return;

	for (i = 0; i < 4; ++i) {
		if (spt->low_prot[i]) {
			free_page((unsigned long)spt->low_prot[i]);
			spt->low_prot[i] = NULL;
		}
	}
	addr = 0;
	for (i = 0; i < (TASK_SIZE_USER64 >> 43); ++i) {
		p = spt->protptrs[i];
		if (!p)
			continue;
		spt->protptrs[i] = NULL;
		for (j = 0; j < SBP_L2_COUNT && addr < spt->maxaddr;
		     ++j, addr += PAGE_SIZE)
			if (p[j])
				free_page((unsigned long)p[j]);
		free_page((unsigned long)p);
	}
	spt->maxaddr = 0;
	kfree(spt);
}

static void hpte_flush_range(struct mm_struct *mm, unsigned long addr,
			     int npages)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return;
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return;
	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!pte)
		return;
	arch_enter_lazy_mmu_mode();
	for (; npages > 0; --npages) {
		pte_update(mm, addr, pte, 0, 0, 0);
		addr += PAGE_SIZE;
		++pte;
	}
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(pte - 1, ptl);
}

/*
 * Clear the subpage protection map for an address range, allowing
 * all accesses that are allowed by the pte permissions.
 */
static void subpage_prot_clear(unsigned long addr, unsigned long len)
{
	struct mm_struct *mm = current->mm;
	struct subpage_prot_table *spt;
	u32 **spm, *spp;
	unsigned long i;
	size_t nw;
	unsigned long next, limit;

	mmap_write_lock(mm);

	spt = mm_ctx_subpage_prot(&mm->context);
	if (!spt)
		goto err_out;

	limit = addr + len;
	if (limit > spt->maxaddr)
		limit = spt->maxaddr;
	for (; addr < limit; addr = next) {
		next = pmd_addr_end(addr, limit);
		if (addr < 0x100000000UL) {
			spm = spt->low_prot;
		} else {
			spm = spt->protptrs[addr >> SBP_L3_SHIFT];
			if (!spm)
				continue;
		}
		spp = spm[(addr >> SBP_L2_SHIFT) & (SBP_L2_COUNT - 1)];
		if (!spp)
			continue;
		spp += (addr >> PAGE_SHIFT) & (SBP_L1_COUNT - 1);

		i = (addr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
		nw = PTRS_PER_PTE - i;
		if (addr + (nw << PAGE_SHIFT) > next)
			nw = (next - addr) >> PAGE_SHIFT;

		memset(spp, 0, nw * sizeof(u32));

		/* now flush any existing HPTEs for the range */
		hpte_flush_range(mm, addr, nw);
	}

err_out:
	mmap_write_unlock(mm);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int subpage_walk_pmd_entry(pmd_t *pmd, unsigned long addr,
				  unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	split_huge_pmd(vma, pmd, addr);
	return 0;
}

static const struct mm_walk_ops subpage_walk_ops = {
	.pmd_entry	= subpage_walk_pmd_entry,
};

static void subpage_mark_vma_nohuge(struct mm_struct *mm, unsigned long addr,
				    unsigned long len)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, addr);

	/*
	 * We don't try too hard, we just mark all the vma in that range
	 * VM_NOHUGEPAGE and split them.
	 */
	for_each_vma_range(vmi, vma, addr + len) {
		vm_flags_set(vma, VM_NOHUGEPAGE);
		walk_page_vma(vma, &subpage_walk_ops, NULL);
	}
}
#else
static void subpage_mark_vma_nohuge(struct mm_struct *mm, unsigned long addr,
				    unsigned long len)
{
	return;
}
#endif

/*
 * Copy in a subpage protection map for an address range.
 * The map has 2 bits per 4k subpage, so 32 bits per 64k page.
 * Each 2-bit field is 0 to allow any access, 1 to prevent writes,
 * 2 or 3 to prevent all accesses.
 * Note that the normal page protections also apply; the subpage
 * protection mechanism is an additional constraint, so putting 0
 * in a 2-bit field won't allow writes to a page that is otherwise
 * write-protected.
 */
SYSCALL_DEFINE3(subpage_prot, unsigned long, addr,
		unsigned long, len, u32 __user *, map)
{
	struct mm_struct *mm = current->mm;
	struct subpage_prot_table *spt;
	u32 **spm, *spp;
	unsigned long i;
	size_t nw;
	unsigned long next, limit;
	int err;

	if (radix_enabled())
		return -ENOENT;

	/* Check parameters */
	if ((addr & ~PAGE_MASK) || (len & ~PAGE_MASK) ||
	    addr >= mm->task_size || len >= mm->task_size ||
	    addr + len > mm->task_size)
		return -EINVAL;

	if (is_hugepage_only_range(mm, addr, len))
		return -EINVAL;

	if (!map) {
		/* Clear out the protection map for the address range */
		subpage_prot_clear(addr, len);
		return 0;
	}

	if (!access_ok(map, (len >> PAGE_SHIFT) * sizeof(u32)))
		return -EFAULT;

	mmap_write_lock(mm);

	spt = mm_ctx_subpage_prot(&mm->context);
	if (!spt) {
		/*
		 * Allocate subpage prot table if not already done.
		 * Do this with mmap_lock held
		 */
		spt = kzalloc(sizeof(struct subpage_prot_table), GFP_KERNEL);
		if (!spt) {
			err = -ENOMEM;
			goto out;
		}
		mm->context.hash_context->spt = spt;
	}

	subpage_mark_vma_nohuge(mm, addr, len);
	for (limit = addr + len; addr < limit; addr = next) {
		next = pmd_addr_end(addr, limit);
		err = -ENOMEM;
		if (addr < 0x100000000UL) {
			spm = spt->low_prot;
		} else {
			spm = spt->protptrs[addr >> SBP_L3_SHIFT];
			if (!spm) {
				spm = (u32 **)get_zeroed_page(GFP_KERNEL);
				if (!spm)
					goto out;
				spt->protptrs[addr >> SBP_L3_SHIFT] = spm;
			}
		}
		spm += (addr >> SBP_L2_SHIFT) & (SBP_L2_COUNT - 1);
		spp = *spm;
		if (!spp) {
			spp = (u32 *)get_zeroed_page(GFP_KERNEL);
			if (!spp)
				goto out;
			*spm = spp;
		}
		spp += (addr >> PAGE_SHIFT) & (SBP_L1_COUNT - 1);

		local_irq_disable();
		demote_segment_4k(mm, addr);
		local_irq_enable();

		i = (addr >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
		nw = PTRS_PER_PTE - i;
		if (addr + (nw << PAGE_SHIFT) > next)
			nw = (next - addr) >> PAGE_SHIFT;

		mmap_write_unlock(mm);
		if (__copy_from_user(spp, map, nw * sizeof(u32)))
			return -EFAULT;
		map += nw;
		mmap_write_lock(mm);

		/* now flush any existing HPTEs for the range */
		hpte_flush_range(mm, addr, nw);
	}
	if (limit > spt->maxaddr)
		spt->maxaddr = limit;
	err = 0;
 out:
	mmap_write_unlock(mm);
	return err;
}
