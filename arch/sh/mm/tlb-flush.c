/*
 * TLB flushing operations for SH with an MMU.
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2003 - 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned int cpu = smp_processor_id();

	if (vma->vm_mm && cpu_context(cpu, vma->vm_mm) != NO_CONTEXT) {
		unsigned long flags;
		unsigned long asid;
		unsigned long saved_asid = MMU_NO_ASID;

		asid = cpu_asid(cpu, vma->vm_mm);
		page &= PAGE_MASK;

		local_irq_save(flags);
		if (vma->vm_mm != current->mm) {
			saved_asid = get_asid();
			set_asid(asid);
		}
		__flush_tlb_page(asid, page);
		if (saved_asid != MMU_NO_ASID)
			set_asid(saved_asid);
		local_irq_restore(flags);
	}
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != NO_CONTEXT) {
		unsigned long flags;
		int size;

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if (size > (MMU_NTLB_ENTRIES/4)) { /* Too many TLB to flush */
			cpu_context(cpu, mm) = NO_CONTEXT;
			if (mm == current->mm)
				activate_context(mm, cpu);
		} else {
			unsigned long asid;
			unsigned long saved_asid = MMU_NO_ASID;

			asid = cpu_asid(cpu, mm);
			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;
			if (mm != current->mm) {
				saved_asid = get_asid();
				set_asid(asid);
			}
			while (start < end) {
				__flush_tlb_page(asid, start);
				start += PAGE_SIZE;
			}
			if (saved_asid != MMU_NO_ASID)
				set_asid(saved_asid);
		}
		local_irq_restore(flags);
	}
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags;
	int size;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size > (MMU_NTLB_ENTRIES/4)) { /* Too many TLB to flush */
		flush_tlb_all();
	} else {
		unsigned long asid;
		unsigned long saved_asid = get_asid();

		asid = cpu_asid(cpu, &init_mm);
		start &= PAGE_MASK;
		end += (PAGE_SIZE - 1);
		end &= PAGE_MASK;
		set_asid(asid);
		while (start < end) {
			__flush_tlb_page(asid, start);
			start += PAGE_SIZE;
		}
		set_asid(saved_asid);
	}
	local_irq_restore(flags);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int cpu = smp_processor_id();

	/* Invalidate all TLB of this process. */
	/* Instead of invalidating each TLB, we get new MMU context. */
	if (cpu_context(cpu, mm) != NO_CONTEXT) {
		unsigned long flags;

		local_irq_save(flags);
		cpu_context(cpu, mm) = NO_CONTEXT;
		if (mm == current->mm)
			activate_context(mm, cpu);
		local_irq_restore(flags);
	}
}

void flush_tlb_all(void)
{
	unsigned long flags, status;

	/*
	 * Flush all the TLB.
	 *
	 * Write to the MMU control register's bit:
	 *	TF-bit for SH-3, TI-bit for SH-4.
	 *      It's same position, bit #2.
	 */
	local_irq_save(flags);
	status = ctrl_inl(MMUCR);
	status |= 0x04;
	ctrl_outl(status, MMUCR);
	ctrl_barrier();
	local_irq_restore(flags);
}

void update_mmu_cache(struct vm_area_struct *vma,
		      unsigned long address, pte_t pte)
{
	unsigned long flags;
	unsigned long pteval;
	unsigned long vpn;
	struct page *page;
	unsigned long pfn = pte_pfn(pte);
	struct address_space *mapping;

	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	mapping = page_mapping(page);
	if (mapping) {
		unsigned long phys = pte_val(pte) & PTE_PHYS_MASK;
		int dirty = test_and_clear_bit(PG_dcache_dirty, &page->flags);

		if (dirty)
			__flush_wback_region((void *)P1SEGADDR(phys),
					     PAGE_SIZE);
	}

	local_irq_save(flags);

	/* Set PTEH register */
	vpn = (address & MMU_VPN_MASK) | get_asid();
	ctrl_outl(vpn, MMU_PTEH);

	pteval = pte_val(pte);

#ifdef CONFIG_CPU_HAS_PTEA
	/* Set PTEA register */
	/* TODO: make this look less hacky */
	ctrl_outl(((pteval >> 28) & 0xe) | (pteval & 0x1), MMU_PTEA);
#endif

	/* Set PTEL register */
	pteval &= _PAGE_FLAGS_HARDWARE_MASK; /* drop software flags */
#if defined(CONFIG_SH_WRITETHROUGH) && defined(CONFIG_CPU_SH4)
	pteval |= _PAGE_WT;
#endif
	/* conveniently, we want all the software flags to be 0 anyway */
	ctrl_outl(pteval, MMU_PTEL);

	/* Load the TLB */
	asm volatile("ldtlb": /* no output */ : /* no input */ : "memory");
	local_irq_restore(flags);
}
