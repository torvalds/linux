// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <asm/mmu_context.h>
#include <asm/setup.h>

/*
 * One C-SKY MMU TLB entry contain two PFN/page entry, ie:
 * 1VPN -> 2PFN
 */
#define TLB_ENTRY_SIZE		(PAGE_SIZE * 2)
#define TLB_ENTRY_SIZE_MASK	(PAGE_MASK << 1)

void flush_tlb_all(void)
{
	tlb_invalid_all();
}

void flush_tlb_mm(struct mm_struct *mm)
{
#ifdef CONFIG_CPU_HAS_TLBI
	asm volatile("tlbi.asids %0"::"r"(cpu_asid(mm)));
#else
	tlb_invalid_all();
#endif
}

/*
 * MMU operation regs only could invalid tlb entry in jtlb and we
 * need change asid field to invalid I-utlb & D-utlb.
 */
#ifndef CONFIG_CPU_HAS_TLBI
#define restore_asid_inv_utlb(oldpid, newpid) \
do { \
	if (oldpid == newpid) \
		write_mmu_entryhi(oldpid + 1); \
	write_mmu_entryhi(oldpid); \
} while (0)
#endif

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	unsigned long newpid = cpu_asid(vma->vm_mm);

	start &= TLB_ENTRY_SIZE_MASK;
	end   += TLB_ENTRY_SIZE - 1;
	end   &= TLB_ENTRY_SIZE_MASK;

#ifdef CONFIG_CPU_HAS_TLBI
	while (start < end) {
		asm volatile("tlbi.vas %0"::"r"(start | newpid));
		start += 2*PAGE_SIZE;
	}
	sync_is();
#else
	{
	unsigned long flags, oldpid;

	local_irq_save(flags);
	oldpid = read_mmu_entryhi() & ASID_MASK;
	while (start < end) {
		int idx;

		write_mmu_entryhi(start | newpid);
		start += 2*PAGE_SIZE;
		tlb_probe();
		idx = read_mmu_index();
		if (idx >= 0)
			tlb_invalid_indexed();
	}
	restore_asid_inv_utlb(oldpid, newpid);
	local_irq_restore(flags);
	}
#endif
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	start &= TLB_ENTRY_SIZE_MASK;
	end   += TLB_ENTRY_SIZE - 1;
	end   &= TLB_ENTRY_SIZE_MASK;

#ifdef CONFIG_CPU_HAS_TLBI
	while (start < end) {
		asm volatile("tlbi.vaas %0"::"r"(start));
		start += 2*PAGE_SIZE;
	}
	sync_is();
#else
	{
	unsigned long flags, oldpid;

	local_irq_save(flags);
	oldpid = read_mmu_entryhi() & ASID_MASK;
	while (start < end) {
		int idx;

		write_mmu_entryhi(start | oldpid);
		start += 2*PAGE_SIZE;
		tlb_probe();
		idx = read_mmu_index();
		if (idx >= 0)
			tlb_invalid_indexed();
	}
	restore_asid_inv_utlb(oldpid, oldpid);
	local_irq_restore(flags);
	}
#endif
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	int newpid = cpu_asid(vma->vm_mm);

	addr &= TLB_ENTRY_SIZE_MASK;

#ifdef CONFIG_CPU_HAS_TLBI
	asm volatile("tlbi.vas %0"::"r"(addr | newpid));
	sync_is();
#else
	{
	int oldpid, idx;
	unsigned long flags;

	local_irq_save(flags);
	oldpid = read_mmu_entryhi() & ASID_MASK;
	write_mmu_entryhi(addr | newpid);
	tlb_probe();
	idx = read_mmu_index();
	if (idx >= 0)
		tlb_invalid_indexed();

	restore_asid_inv_utlb(oldpid, newpid);
	local_irq_restore(flags);
	}
#endif
}

void flush_tlb_one(unsigned long addr)
{
	addr &= TLB_ENTRY_SIZE_MASK;

#ifdef CONFIG_CPU_HAS_TLBI
	asm volatile("tlbi.vaas %0"::"r"(addr));
	sync_is();
#else
	{
	int oldpid, idx;
	unsigned long flags;

	local_irq_save(flags);
	oldpid = read_mmu_entryhi() & ASID_MASK;
	write_mmu_entryhi(addr | oldpid);
	tlb_probe();
	idx = read_mmu_index();
	if (idx >= 0)
		tlb_invalid_indexed();

	restore_asid_inv_utlb(oldpid, oldpid);
	local_irq_restore(flags);
	}
#endif
}
EXPORT_SYMBOL(flush_tlb_one);
