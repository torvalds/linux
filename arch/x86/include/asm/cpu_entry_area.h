// SPDX-License-Identifier: GPL-2.0

#ifndef _ASM_X86_CPU_ENTRY_AREA_H
#define _ASM_X86_CPU_ENTRY_AREA_H

#include <linux/percpu-defs.h>
#include <asm/processor.h>
#include <asm/intel_ds.h>

/*
 * cpu_entry_area is a percpu region that contains things needed by the CPU
 * and early entry/exit code.  Real types aren't used for all fields here
 * to avoid circular header dependencies.
 *
 * Every field is a virtual alias of some other allocated backing store.
 * There is no direct allocation of a struct cpu_entry_area.
 */
struct cpu_entry_area {
	char gdt[PAGE_SIZE];

	/*
	 * The GDT is just below entry_stack and thus serves (on x86_64) as
	 * a a read-only guard page.
	 */
	struct entry_stack_page entry_stack_page;

	/*
	 * On x86_64, the TSS is mapped RO.  On x86_32, it's mapped RW because
	 * we need task switches to work, and task switches write to the TSS.
	 */
	struct tss_struct tss;

#ifdef CONFIG_X86_64
	/*
	 * Exception stacks used for IST entries.
	 *
	 * In the future, this should have a separate slot for each stack
	 * with guard pages between them.
	 */
	char exception_stacks[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ + DEBUG_STKSZ];
#endif
#ifdef CONFIG_CPU_SUP_INTEL
	/*
	 * Per CPU debug store for Intel performance monitoring. Wastes a
	 * full page at the moment.
	 */
	struct debug_store cpu_debug_store;
	/*
	 * The actual PEBS/BTS buffers must be mapped to user space
	 * Reserve enough fixmap PTEs.
	 */
	struct debug_store_buffers cpu_debug_buffers;
#endif
};

#define CPU_ENTRY_AREA_SIZE	(sizeof(struct cpu_entry_area))
#define CPU_ENTRY_AREA_TOT_SIZE	(CPU_ENTRY_AREA_SIZE * NR_CPUS)

DECLARE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);

extern void setup_cpu_entry_areas(void);
extern void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags);

#define	CPU_ENTRY_AREA_RO_IDT		CPU_ENTRY_AREA_BASE
#define CPU_ENTRY_AREA_PER_CPU		(CPU_ENTRY_AREA_RO_IDT + PAGE_SIZE)

#define CPU_ENTRY_AREA_RO_IDT_VADDR	((void *)CPU_ENTRY_AREA_RO_IDT)

#define CPU_ENTRY_AREA_MAP_SIZE			\
	(CPU_ENTRY_AREA_PER_CPU + CPU_ENTRY_AREA_TOT_SIZE - CPU_ENTRY_AREA_BASE)

extern struct cpu_entry_area *get_cpu_entry_area(int cpu);

static inline struct entry_stack *cpu_entry_stack(int cpu)
{
	return &get_cpu_entry_area(cpu)->entry_stack_page.stack;
}

#endif
