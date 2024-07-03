// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched/signal.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <as-layout.h>
#include <mem_user.h>
#include <os.h>
#include <skas.h>
#include <kern_util.h>

struct vm_ops {
	struct mm_id *mm_idp;

	int (*mmap)(struct mm_id *mm_idp,
		    unsigned long virt, unsigned long len, int prot,
		    int phys_fd, unsigned long long offset);
	int (*unmap)(struct mm_id *mm_idp,
		     unsigned long virt, unsigned long len);
	int (*mprotect)(struct mm_id *mm_idp,
			unsigned long virt, unsigned long len,
			unsigned int prot);
};

static int kern_map(struct mm_id *mm_idp,
		    unsigned long virt, unsigned long len, int prot,
		    int phys_fd, unsigned long long offset)
{
	/* TODO: Why is executable needed to be always set in the kernel? */
	return os_map_memory((void *)virt, phys_fd, offset, len,
			     prot & UM_PROT_READ, prot & UM_PROT_WRITE,
			     1);
}

static int kern_unmap(struct mm_id *mm_idp,
		      unsigned long virt, unsigned long len)
{
	return os_unmap_memory((void *)virt, len);
}

static int kern_mprotect(struct mm_id *mm_idp,
			 unsigned long virt, unsigned long len,
			 unsigned int prot)
{
	return os_protect_memory((void *)virt, len,
				 prot & UM_PROT_READ, prot & UM_PROT_WRITE,
				 1);
}

void report_enomem(void)
{
	printk(KERN_ERR "UML ran out of memory on the host side! "
			"This can happen due to a memory limitation or "
			"vm.max_map_count has been reached.\n");
}

static inline int update_pte_range(pmd_t *pmd, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops)
{
	pte_t *pte;
	int r, w, x, prot, ret = 0;

	pte = pte_offset_kernel(pmd, addr);
	do {
		r = pte_read(*pte);
		w = pte_write(*pte);
		x = pte_exec(*pte);
		if (!pte_young(*pte)) {
			r = 0;
			w = 0;
		} else if (!pte_dirty(*pte))
			w = 0;

		prot = ((r ? UM_PROT_READ : 0) | (w ? UM_PROT_WRITE : 0) |
			(x ? UM_PROT_EXEC : 0));
		if (pte_newpage(*pte)) {
			if (pte_present(*pte)) {
				if (pte_newpage(*pte)) {
					__u64 offset;
					unsigned long phys =
						pte_val(*pte) & PAGE_MASK;
					int fd =  phys_mapping(phys, &offset);

					ret = ops->mmap(ops->mm_idp, addr,
							PAGE_SIZE, prot, fd,
							offset);
				}
			} else
				ret = ops->unmap(ops->mm_idp, addr, PAGE_SIZE);
		} else if (pte_newprot(*pte))
			ret = ops->mprotect(ops->mm_idp, addr, PAGE_SIZE, prot);
		*pte = pte_mkuptodate(*pte);
	} while (pte++, addr += PAGE_SIZE, ((addr < end) && !ret));
	return ret;
}

static inline int update_pmd_range(pud_t *pud, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops)
{
	pmd_t *pmd;
	unsigned long next;
	int ret = 0;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (!pmd_present(*pmd)) {
			if (pmd_newpage(*pmd)) {
				ret = ops->unmap(ops->mm_idp, addr,
						 next - addr);
				pmd_mkuptodate(*pmd);
			}
		}
		else ret = update_pte_range(pmd, addr, next, ops);
	} while (pmd++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_pud_range(p4d_t *p4d, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops)
{
	pud_t *pud;
	unsigned long next;
	int ret = 0;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (!pud_present(*pud)) {
			if (pud_newpage(*pud)) {
				ret = ops->unmap(ops->mm_idp, addr,
						 next - addr);
				pud_mkuptodate(*pud);
			}
		}
		else ret = update_pmd_range(pud, addr, next, ops);
	} while (pud++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_p4d_range(pgd_t *pgd, unsigned long addr,
				   unsigned long end,
				   struct vm_ops *ops)
{
	p4d_t *p4d;
	unsigned long next;
	int ret = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (!p4d_present(*p4d)) {
			if (p4d_newpage(*p4d)) {
				ret = ops->unmap(ops->mm_idp, addr,
						 next - addr);
				p4d_mkuptodate(*p4d);
			}
		} else
			ret = update_pud_range(p4d, addr, next, ops);
	} while (p4d++, addr = next, ((addr < end) && !ret));
	return ret;
}

static int fix_range_common(struct mm_struct *mm, unsigned long start_addr,
			     unsigned long end_addr)
{
	pgd_t *pgd;
	struct vm_ops ops;
	unsigned long addr = start_addr, next;
	int ret = 0;

	ops.mm_idp = &mm->context.id;
	if (mm == &init_mm) {
		ops.mmap = kern_map;
		ops.unmap = kern_unmap;
		ops.mprotect = kern_mprotect;
	} else {
		ops.mmap = map;
		ops.unmap = unmap;
		ops.mprotect = protect;
	}

	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end_addr);
		if (!pgd_present(*pgd)) {
			if (pgd_newpage(*pgd)) {
				ret = ops.unmap(ops.mm_idp, addr,
						next - addr);
				pgd_mkuptodate(*pgd);
			}
		} else
			ret = update_p4d_range(pgd, addr, next, &ops);
	} while (pgd++, addr = next, ((addr < end_addr) && !ret));

	if (ret == -ENOMEM)
		report_enomem();

	return ret;
}

static void flush_tlb_kernel_range_common(unsigned long start, unsigned long end)
{
	int err;

	err = fix_range_common(&init_mm, start, end);

	if (err)
		panic("flush_tlb_kernel failed, errno = %d\n", err);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct mm_struct *mm = vma->vm_mm;
	int r, w, x, prot;
	struct mm_id *mm_id;

	address &= PAGE_MASK;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto kill;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto kill;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		goto kill;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		goto kill;

	pte = pte_offset_kernel(pmd, address);

	r = pte_read(*pte);
	w = pte_write(*pte);
	x = pte_exec(*pte);
	if (!pte_young(*pte)) {
		r = 0;
		w = 0;
	} else if (!pte_dirty(*pte)) {
		w = 0;
	}

	mm_id = &mm->context.id;
	prot = ((r ? UM_PROT_READ : 0) | (w ? UM_PROT_WRITE : 0) |
		(x ? UM_PROT_EXEC : 0));
	if (pte_newpage(*pte)) {
		if (pte_present(*pte)) {
			unsigned long long offset;
			int fd;

			fd = phys_mapping(pte_val(*pte) & PAGE_MASK, &offset);
			map(mm_id, address, PAGE_SIZE, prot, fd, offset);
		} else
			unmap(mm_id, address, PAGE_SIZE);
	} else if (pte_newprot(*pte))
		protect(mm_id, address, PAGE_SIZE, prot);

	*pte = pte_mkuptodate(*pte);

	return;

kill:
	printk(KERN_ERR "Failed to flush page for address 0x%lx\n", address);
	force_sig(SIGKILL);
}

void flush_tlb_all(void)
{
	/*
	 * Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if (atomic_read(&current->mm->mm_users) == 0)
		return;

	flush_tlb_mm(current->mm);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_tlb_kernel_range_common(start, end);
}

void flush_tlb_kernel_vm(void)
{
	flush_tlb_kernel_range_common(start_vm, end_vm);
}

void __flush_tlb_one(unsigned long addr)
{
	flush_tlb_kernel_range_common(addr, addr + PAGE_SIZE);
}

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr)
{
	/*
	 * Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if (atomic_read(&mm->mm_users) == 0)
		return;

	fix_range_common(mm, start_addr, end_addr);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	if (vma->vm_mm == NULL)
		flush_tlb_kernel_range_common(start, end);
	else fix_range(vma->vm_mm, start, end);
}
EXPORT_SYMBOL(flush_tlb_range);

void flush_tlb_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	for_each_vma(vmi, vma)
		fix_range(mm, vma->vm_start, vma->vm_end);
}
