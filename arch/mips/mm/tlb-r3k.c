/*
 * r2300.c: R2000 and R3000 specific mmu/cache code.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 *
 * with a lot of changes to make this thing work for R3000s
 * Tx39XX R4k style caches added. HK
 * Copyright (C) 1998, 1999, 2000 Harald Koerfgen
 * Copyright (C) 1998 Gleb Raiko & Vladimir Roganov
 * Copyright (C) 2002  Ralf Baechle
 * Copyright (C) 2002  Maciej W. Rozycki
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/tlbmisc.h>
#include <asm/isadep.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

#undef DEBUG_TLB

extern void build_tlb_refill_handler(void);

/* CP0 hazard avoidance. */
#define BARRIER				\
	__asm__ __volatile__(		\
		".set	push\n\t"	\
		".set	noreorder\n\t"	\
		"nop\n\t"		\
		".set	pop\n\t")

int r3k_have_wired_reg;		/* should be in cpu_data? */

/* TLB operations. */
void local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	local_irq_save(flags);
	old_ctx = read_c0_entryhi() & ASID_MASK;
	write_c0_entrylo0(0);
	entry = r3k_have_wired_reg ? read_c0_wired() : 8;
	for (; entry < current_cpu_data.tlbsize; entry++) {
		write_c0_index(entry << 8);
		write_c0_entryhi((entry | 0x80000) << 12);
		BARRIER;
		tlb_write_indexed();
	}
	write_c0_entryhi(old_ctx);
	local_irq_restore(flags);
}

void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0) {
#ifdef DEBUG_TLB
		printk("[tlbmm<%lu>]", (unsigned long)cpu_context(cpu, mm));
#endif
		drop_mmu_context(mm, cpu);
	}
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) != 0) {
		unsigned long size, flags;

#ifdef DEBUG_TLB
		printk("[tlbrange<%lu,0x%08lx,0x%08lx>]",
			cpu_context(cpu, mm) & ASID_MASK, start, end);
#endif
		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if (size <= current_cpu_data.tlbsize) {
			int oldpid = read_c0_entryhi() & ASID_MASK;
			int newpid = cpu_context(cpu, mm) & ASID_MASK;

			start &= PAGE_MASK;
			end += PAGE_SIZE - 1;
			end &= PAGE_MASK;
			while (start < end) {
				int idx;

				write_c0_entryhi(start | newpid);
				start += PAGE_SIZE;	/* BARRIER */
				tlb_probe();
				idx = read_c0_index();
				write_c0_entrylo0(0);
				write_c0_entryhi(KSEG0);
				if (idx < 0)		/* BARRIER */
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
	unsigned long size, flags;

#ifdef DEBUG_TLB
	printk("[tlbrange<%lu,0x%08lx,0x%08lx>]", start, end);
#endif
	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size <= current_cpu_data.tlbsize) {
		int pid = read_c0_entryhi();

		start &= PAGE_MASK;
		end += PAGE_SIZE - 1;
		end &= PAGE_MASK;

		while (start < end) {
			int idx;

			write_c0_entryhi(start);
			start += PAGE_SIZE;		/* BARRIER */
			tlb_probe();
			idx = read_c0_index();
			write_c0_entrylo0(0);
			write_c0_entryhi(KSEG0);
			if (idx < 0)			/* BARRIER */
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
	int cpu = smp_processor_id();

	if (cpu_context(cpu, vma->vm_mm) != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%lu,0x%08lx>]", cpu_context(cpu, vma->vm_mm), page);
#endif
		newpid = cpu_context(cpu, vma->vm_mm) & ASID_MASK;
		page &= PAGE_MASK;
		local_irq_save(flags);
		oldpid = read_c0_entryhi() & ASID_MASK;
		write_c0_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		idx = read_c0_index();
		write_c0_entrylo0(0);
		write_c0_entryhi(KSEG0);
		if (idx < 0)				/* BARRIER */
			goto finish;
		tlb_write_indexed();

finish:
		write_c0_entryhi(oldpid);
		local_irq_restore(flags);
	}
}

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long flags;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	pid = read_c0_entryhi() & ASID_MASK;

#ifdef DEBUG_TLB
	if ((pid != (cpu_context(cpu, vma->vm_mm) & ASID_MASK)) || (cpu_context(cpu, vma->vm_mm) == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%lu tlbpid=%d\n",
		       (cpu_context(cpu, vma->vm_mm)), pid);
	}
#endif

	local_irq_save(flags);
	address &= PAGE_MASK;
	write_c0_entryhi(address | pid);
	BARRIER;
	tlb_probe();
	idx = read_c0_index();
	write_c0_entrylo0(pte_val(pte));
	write_c0_entryhi(address | pid);
	if (idx < 0) {					/* BARRIER */
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	write_c0_entryhi(pid);
	local_irq_restore(flags);
}

void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
		     unsigned long entryhi, unsigned long pagemask)
{
	unsigned long flags;
	unsigned long old_ctx;
	static unsigned long wired = 0;

	if (r3k_have_wired_reg) {			/* TX39XX */
		unsigned long old_pagemask;
		unsigned long w;

#ifdef DEBUG_TLB
		printk("[tlbwired<entry lo0 %8x, hi %8x\n, pagemask %8x>]\n",
		       entrylo0, entryhi, pagemask);
#endif

		local_irq_save(flags);
		/* Save old context and create impossible VPN2 value */
		old_ctx = read_c0_entryhi() & ASID_MASK;
		old_pagemask = read_c0_pagemask();
		w = read_c0_wired();
		write_c0_wired(w + 1);
		write_c0_index(w << 8);
		write_c0_pagemask(pagemask);
		write_c0_entryhi(entryhi);
		write_c0_entrylo0(entrylo0);
		BARRIER;
		tlb_write_indexed();

		write_c0_entryhi(old_ctx);
		write_c0_pagemask(old_pagemask);
		local_flush_tlb_all();
		local_irq_restore(flags);

	} else if (wired < 8) {
#ifdef DEBUG_TLB
		printk("[tlbwired<entry lo0 %8x, hi %8x\n>]\n",
		       entrylo0, entryhi);
#endif

		local_irq_save(flags);
		old_ctx = read_c0_entryhi() & ASID_MASK;
		write_c0_entrylo0(entrylo0);
		write_c0_entryhi(entryhi);
		write_c0_index(wired);
		wired++;				/* BARRIER */
		tlb_write_indexed();
		write_c0_entryhi(old_ctx);
		local_flush_tlb_all();
		local_irq_restore(flags);
	}
}

void tlb_init(void)
{
	local_flush_tlb_all();

	build_tlb_refill_handler();
}
