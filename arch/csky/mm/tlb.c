// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/setup.h>

#define CSKY_TLB_SIZE CONFIG_CPU_TLB_SIZE

void flush_tlb_all(void)
{
	tlb_invalid_all();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0)
		drop_mmu_context(mm, cpu);

	tlb_invalid_all();
}

#define restore_asid_inv_utlb(oldpid, newpid) \
do { \
	if ((oldpid & ASID_MASK) == newpid) \
		write_mmu_entryhi(oldpid + 1); \
	write_mmu_entryhi(oldpid); \
} while (0)

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0) {
		unsigned long size, flags;
		int newpid = cpu_asid(cpu, mm);

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if (size <= CSKY_TLB_SIZE/2) {
			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
#ifdef CONFIG_CPU_HAS_TLBI
			while (start < end) {
				asm volatile("tlbi.vaas %0"
					     ::"r"(start | newpid));
				start += (PAGE_SIZE << 1);
			}
			sync_is();
#else
			{
			int oldpid = read_mmu_entryhi();

			while (start < end) {
				int idx;

				write_mmu_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				tlb_probe();
				idx = read_mmu_index();
				if (idx >= 0)
					tlb_invalid_indexed();
			}
			restore_asid_inv_utlb(oldpid, newpid);
			}
#endif
		} else {
			drop_mmu_context(mm, cpu);
		}
		local_irq_restore(flags);
	}
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long size, flags;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size <= CSKY_TLB_SIZE) {
		start &= (PAGE_MASK << 1);
		end += ((PAGE_SIZE << 1) - 1);
		end &= (PAGE_MASK << 1);
#ifdef CONFIG_CPU_HAS_TLBI
		while (start < end) {
			asm volatile("tlbi.vaas %0"::"r"(start));
			start += (PAGE_SIZE << 1);
		}
		sync_is();
#else
		{
		int oldpid = read_mmu_entryhi();

		while (start < end) {
			int idx;

			write_mmu_entryhi(start);
			start += (PAGE_SIZE << 1);
			tlb_probe();
			idx = read_mmu_index();
			if (idx >= 0)
				tlb_invalid_indexed();
		}
		restore_asid_inv_utlb(oldpid, 0);
		}
#endif
	} else {
		flush_tlb_all();
	}

	local_irq_restore(flags);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	int cpu = smp_processor_id();
	int newpid = cpu_asid(cpu, vma->vm_mm);

	if (!vma || cpu_context(cpu, vma->vm_mm) != 0) {
		page &= (PAGE_MASK << 1);

#ifdef CONFIG_CPU_HAS_TLBI
		asm volatile("tlbi.vaas %0"::"r"(page | newpid));
		sync_is();
#else
		{
		int oldpid, idx;
		unsigned long flags;

		local_irq_save(flags);
		oldpid = read_mmu_entryhi();
		write_mmu_entryhi(page | newpid);
		tlb_probe();
		idx = read_mmu_index();
		if (idx >= 0)
			tlb_invalid_indexed();

		restore_asid_inv_utlb(oldpid, newpid);
		local_irq_restore(flags);
		}
#endif
	}
}

/*
 * Remove one kernel space TLB entry.  This entry is assumed to be marked
 * global so we don't do the ASID thing.
 */
void flush_tlb_one(unsigned long page)
{
	int oldpid;

	oldpid = read_mmu_entryhi();
	page &= (PAGE_MASK << 1);

#ifdef CONFIG_CPU_HAS_TLBI
	page = page | (oldpid & 0xfff);
	asm volatile("tlbi.vaas %0"::"r"(page));
	sync_is();
#else
	{
	int idx;
	unsigned long flags;

	page = page | (oldpid & 0xff);

	local_irq_save(flags);
	write_mmu_entryhi(page);
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

/* show current 32 jtlbs */
void show_jtlb_table(void)
{
	unsigned long flags;
	int entryhi, entrylo0, entrylo1;
	int entry;
	int oldpid;

	local_irq_save(flags);
	entry = 0;
	pr_info("\n\n\n");

	oldpid = read_mmu_entryhi();
	while (entry < CSKY_TLB_SIZE) {
		write_mmu_index(entry);
		tlb_read();
		entryhi = read_mmu_entryhi();
		entrylo0 = read_mmu_entrylo0();
		entrylo0 = entrylo0;
		entrylo1 = read_mmu_entrylo1();
		entrylo1 = entrylo1;
		pr_info("jtlb[%d]:	entryhi - 0x%x;	entrylo0 - 0x%x;"
			"	entrylo1 - 0x%x\n",
			entry, entryhi, entrylo0, entrylo1);
		entry++;
	}
	write_mmu_entryhi(oldpid);
	local_irq_restore(flags);
}
