/*
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/percpu.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/cachetype.h>

#define asid_bits(reg) \
	(((read_cpuid(ID_AA64MMFR0_EL1) & 0xf0) >> 2) + 8)

#define ASID_FIRST_VERSION	(1 << MAX_ASID_BITS)

static DEFINE_RAW_SPINLOCK(cpu_asid_lock);
unsigned int cpu_last_asid = ASID_FIRST_VERSION;

/*
 * We fork()ed a process, and we need a new context for the child to run in.
 */
void __init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.id = 0;
	raw_spin_lock_init(&mm->context.id_lock);
}

static void flush_context(void)
{
	/* set the reserved TTBR0 before flushing the TLB */
	cpu_set_reserved_ttbr0();
	flush_tlb_all();
	if (icache_is_aivivt())
		__flush_icache_all();
}

static void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	unsigned long flags;

	/*
	 * Locking needed for multi-threaded applications where the same
	 * mm->context.id could be set from different CPUs during the
	 * broadcast. This function is also called via IPI so the
	 * mm->context.id_lock has to be IRQ-safe.
	 */
	raw_spin_lock_irqsave(&mm->context.id_lock, flags);
	if (likely((mm->context.id ^ cpu_last_asid) >> MAX_ASID_BITS)) {
		/*
		 * Old version of ASID found. Set the new one and reset
		 * mm_cpumask(mm).
		 */
		mm->context.id = asid;
		cpumask_clear(mm_cpumask(mm));
	}
	raw_spin_unlock_irqrestore(&mm->context.id_lock, flags);

	/*
	 * Set the mm_cpumask(mm) bit for the current CPU.
	 */
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
}

/*
 * Reset the ASID on the current CPU. This function call is broadcast from the
 * CPU handling the ASID rollover and holding cpu_asid_lock.
 */
static void reset_context(void *info)
{
	unsigned int asid;
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm = current->active_mm;

	/*
	 * current->active_mm could be init_mm for the idle thread immediately
	 * after secondary CPU boot or hotplug. TTBR0_EL1 is already set to
	 * the reserved value, so no need to reset any context.
	 */
	if (mm == &init_mm)
		return;

	smp_rmb();
	asid = cpu_last_asid + cpu;

	flush_context();
	set_mm_context(mm, asid);

	/* set the new ASID */
	cpu_switch_mm(mm->pgd, mm);
}

void __new_context(struct mm_struct *mm)
{
	unsigned int asid;
	unsigned int bits = asid_bits();

	raw_spin_lock(&cpu_asid_lock);
	/*
	 * Check the ASID again, in case the change was broadcast from another
	 * CPU before we acquired the lock.
	 */
	if (!unlikely((mm->context.id ^ cpu_last_asid) >> MAX_ASID_BITS)) {
		cpumask_set_cpu(smp_processor_id(), mm_cpumask(mm));
		raw_spin_unlock(&cpu_asid_lock);
		return;
	}
	/*
	 * At this point, it is guaranteed that the current mm (with an old
	 * ASID) isn't active on any other CPU since the ASIDs are changed
	 * simultaneously via IPI.
	 */
	asid = ++cpu_last_asid;

	/*
	 * If we've used up all our ASIDs, we need to start a new version and
	 * flush the TLB.
	 */
	if (unlikely((asid & ((1 << bits) - 1)) == 0)) {
		/* increment the ASID version */
		cpu_last_asid += (1 << MAX_ASID_BITS) - (1 << bits);
		if (cpu_last_asid == 0)
			cpu_last_asid = ASID_FIRST_VERSION;
		asid = cpu_last_asid + smp_processor_id();
		flush_context();
		smp_wmb();
		smp_call_function(reset_context, NULL, 1);
		cpu_last_asid += NR_CPUS - 1;
	}

	set_mm_context(mm, asid);
	raw_spin_unlock(&cpu_asid_lock);
}
