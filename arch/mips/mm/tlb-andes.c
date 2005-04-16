/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000 Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>

extern void build_tlb_refill_handler(void);

#define NTLB_ENTRIES       64
#define NTLB_ENTRIES_HALF  32

void local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	unsigned long entry;

	local_irq_save(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi() & ASID_MASK;
	write_c0_entryhi(CKSEG0);
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);

	entry = read_c0_wired();

	/* Blast 'em all away. */
	while (entry < NTLB_ENTRIES) {
		write_c0_index(entry);
		tlb_write_indexed();
		entry++;
	}
	write_c0_entryhi(old_ctx);
	local_irq_restore(flags);
}

void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();
	if (cpu_context(cpu, mm) != 0) {
		drop_mmu_context(mm,cpu);
	}
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
                           unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0) {
		unsigned long flags;
		int size;

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if (size <= NTLB_ENTRIES_HALF) {
			int oldpid = (read_c0_entryhi() & ASID_MASK);
			int newpid = (cpu_context(smp_processor_id(), mm)
				      & ASID_MASK);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				write_c0_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				tlb_probe();
				idx = read_c0_index();
				write_c0_entrylo0(0);
				write_c0_entrylo1(0);
				write_c0_entryhi(CKSEG0);
				if(idx < 0)
					continue;
				tlb_write_indexed();
			}
			write_c0_entryhi(oldpid);
		} else {
			drop_mmu_context(mm, cpu);
		}
		local_irq_restore(flags);
	}
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size;

	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	size = (size + 1) >> 1;

	local_irq_save(flags);
	if (size <= NTLB_ENTRIES_HALF) {
		int pid = read_c0_entryhi();

		start &= (PAGE_MASK << 1);
		end += ((PAGE_SIZE << 1) - 1);
		end &= (PAGE_MASK << 1);

		while (start < end) {
			int idx;

			write_c0_entryhi(start);
			start += (PAGE_SIZE << 1);
			tlb_probe();
			idx = read_c0_index();
			write_c0_entrylo0(0);
			write_c0_entrylo1(0);
			write_c0_entryhi(CKSEG0 + (idx << (PAGE_SHIFT+1)));
			if (idx < 0)
				continue;
			tlb_write_indexed();
		}
		write_c0_entryhi(pid);
	} else {
		local_flush_tlb_all();
	}
	local_irq_restore(flags);
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (cpu_context(smp_processor_id(), vma->vm_mm) != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

		newpid = (cpu_context(smp_processor_id(), vma->vm_mm) &
			  ASID_MASK);
		page &= (PAGE_MASK << 1);
		local_irq_save(flags);
		oldpid = (read_c0_entryhi() & ASID_MASK);
		write_c0_entryhi(page | newpid);
		tlb_probe();
		idx = read_c0_index();
		write_c0_entrylo0(0);
		write_c0_entrylo1(0);
		write_c0_entryhi(CKSEG0);
		if (idx < 0)
			goto finish;
		tlb_write_indexed();

	finish:
		write_c0_entryhi(oldpid);
		local_irq_restore(flags);
	}
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
void local_flush_tlb_one(unsigned long page)
{
	unsigned long flags;
	int oldpid, idx;

	local_irq_save(flags);
	page &= (PAGE_MASK << 1);
	oldpid = read_c0_entryhi() & 0xff;
	write_c0_entryhi(page);
	tlb_probe();
	idx = read_c0_index();
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	if (idx >= 0) {
		/* Make sure all entries differ. */
		write_c0_entryhi(CKSEG0+(idx<<(PAGE_SHIFT+1)));
		tlb_write_indexed();
	}
	write_c0_entryhi(oldpid);

	local_irq_restore(flags);
}

/* XXX Simplify this.  On the R10000 writing a TLB entry for an virtual
   address that already exists will overwrite the old entry and not result
   in TLB malfunction or TLB shutdown.  */
void __update_tlb(struct vm_area_struct * vma, unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	pid = read_c0_entryhi() & ASID_MASK;

	if ((pid != (cpu_context(smp_processor_id(), vma->vm_mm) & ASID_MASK))
	    || (cpu_context(smp_processor_id(), vma->vm_mm) == 0)) {
		printk(KERN_WARNING
		       "%s: Wheee, bogus tlbpid mmpid=%d tlbpid=%d\n",
		       __FUNCTION__, (int) (cpu_context(smp_processor_id(),
		       vma->vm_mm) & ASID_MASK), pid);
	}

	local_irq_save(flags);
	address &= (PAGE_MASK << 1);
	write_c0_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = read_c0_index();
	ptep = pte_offset_map(pmdp, address);
	write_c0_entrylo0(pte_val(*ptep++) >> 6);
	write_c0_entrylo1(pte_val(*ptep) >> 6);
	write_c0_entryhi(address | pid);
	if (idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	write_c0_entryhi(pid);
	local_irq_restore(flags);
}

void __init tlb_init(void)
{
	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	write_c0_pagemask(PM_4K);
	write_c0_wired(0);
	write_c0_framemask(0);

        /* From this point on the ARC firmware is dead.  */
	local_flush_tlb_all();

	/* Did I tell you that ARC SUCKS?  */

	build_tlb_refill_handler();
}
