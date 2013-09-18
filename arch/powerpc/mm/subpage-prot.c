/*
 * Copyright 2007-2008 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>

/*
 * Free all pages allocated for subpage protection maps and pointers.
 * Also makes sure that the subpage_prot_table structure is
 * reinitialized for the next user.
 */
void subpage_prot_free(struct mm_struct *mm)
{
	struct subpage_prot_table *spt = &mm->context.spt;
	unsigned long i, j, addr;
	u32 **p;

	for (i = 0; i < 4; ++i) {
		if (spt->low_prot[i]) {
			free_page((unsigned long)spt->low_prot[i]);
			spt->low_prot[i] = NULL;
		}
	}
	addr = 0;
	for (i = 0; i < 2; ++i) {
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
}

void subpage_prot_init_new_context(struct mm_struct *mm)
{
	struct subpage_prot_table *spt = &mm->context.spt;

	memset(spt, 0, sizeof(*spt));
}

static void hpte_flush_range(struct mm_struct *mm, unsigned long addr,
			     int npages)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	spinlock_t *ptl;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return;
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud))
		return;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return;
	pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	arch_enter_lazy_mmu_mode();
	for (; npages > 0; --npages) {
		pte_update(mm, addr, pte, 0, 0);
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
	struct subpage_prot_table *spt = &mm->context.spt;
	u32 **spm, *spp;
	unsigned long i;
	size_t nw;
	unsigned long next, limit;

	down_write(&mm->mmap_sem);
	limit = addr + len;
	if (limit > spt->maxaddr)
		limit = spt->maxaddr;
	for (; addr < limit; addr = next) {
		next = pmd_addr_end(addr, limit);
		if (addr < 0x100000000) {
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
	up_write(&mm->mmap_sem);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int subpage_walk_pmd_entry(pmd_t *pmd, unsigned long addr,
				  unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->private;
	split_huge_page_pmd(vma, addr, pmd);
	return 0;
}

static void subpage_mark_vma_nohuge(struct mm_struct *mm, unsigned long addr,
				    unsigned long len)
{
	struct vm_area_struct *vma;
	struct mm_walk subpage_proto_walk = {
		.mm = mm,
		.pmd_entry = subpage_walk_pmd_entry,
	};

	/*
	 * We don't try too hard, we just mark all the vma in that range
	 * VM_NOHUGEPAGE and split them.
	 */
	vma = find_vma(mm, addr);
	/*
	 * If the range is in unmapped range, just return
	 */
	if (vma && ((addr + len) <= vma->vm_start))
		return;

	while (vma) {
		if (vma->vm_start >= (addr + len))
			break;
		vma->vm_flags |= VM_NOHUGEPAGE;
		subpage_proto_walk.private = vma;
		walk_page_range(vma->vm_start, vma->vm_end,
				&subpage_proto_walk);
		vma = vma->vm_next;
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
long sys_subpage_prot(unsigned long addr, unsigned long len, u32 __user *map)
{
	struct mm_struct *mm = current->mm;
	struct subpage_prot_table *spt = &mm->context.spt;
	u32 **spm, *spp;
	unsigned long i;
	size_t nw;
	unsigned long next, limit;
	int err;

	/* Check parameters */
	if ((addr & ~PAGE_MASK) || (len & ~PAGE_MASK) ||
	    addr >= TASK_SIZE || len >= TASK_SIZE || addr + len > TASK_SIZE)
		return -EINVAL;

	if (is_hugepage_only_range(mm, addr, len))
		return -EINVAL;

	if (!map) {
		/* Clear out the protection map for the address range */
		subpage_prot_clear(addr, len);
		return 0;
	}

	if (!access_ok(VERIFY_READ, map, (len >> PAGE_SHIFT) * sizeof(u32)))
		return -EFAULT;

	down_write(&mm->mmap_sem);
	subpage_mark_vma_nohuge(mm, addr, len);
	for (limit = addr + len; addr < limit; addr = next) {
		next = pmd_addr_end(addr, limit);
		err = -ENOMEM;
		if (addr < 0x100000000) {
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

		up_write(&mm->mmap_sem);
		err = -EFAULT;
		if (__copy_from_user(spp, map, nw * sizeof(u32)))
			goto out2;
		map += nw;
		down_write(&mm->mmap_sem);

		/* now flush any existing HPTEs for the range */
		hpte_flush_range(mm, addr, nw);
	}
	if (limit > spt->maxaddr)
		spt->maxaddr = limit;
	err = 0;
 out:
	up_write(&mm->mmap_sem);
 out2:
	return err;
}
