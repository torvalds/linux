// SPDX-License-Identifier: GPL-2.0

#include <linux/spinlock.h>
#include <linux/percpu.h>

#include <asm/cpu_entry_area.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>
#include <asm/desc.h>

static DEFINE_PER_CPU_PAGE_ALIGNED(struct entry_stack_page, entry_stack_storage);

#ifdef CONFIG_X86_64
static DEFINE_PER_CPU_PAGE_ALIGNED(char, exception_stacks
	[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ + DEBUG_STKSZ]);
#endif

static void __init
set_percpu_fixmap_pages(int idx, void *ptr, int pages, pgprot_t prot)
{
	for ( ; pages; pages--, idx--, ptr += PAGE_SIZE)
		__set_fixmap(idx, per_cpu_ptr_to_phys(ptr), prot);
}

/* Setup the fixmap mappings only once per-processor */
static void __init setup_cpu_entry_area(int cpu)
{
#ifdef CONFIG_X86_64
	extern char _entry_trampoline[];

	/* On 64-bit systems, we use a read-only fixmap GDT and TSS. */
	pgprot_t gdt_prot = PAGE_KERNEL_RO;
	pgprot_t tss_prot = PAGE_KERNEL_RO;
#else
	/*
	 * On native 32-bit systems, the GDT cannot be read-only because
	 * our double fault handler uses a task gate, and entering through
	 * a task gate needs to change an available TSS to busy.  If the
	 * GDT is read-only, that will triple fault.  The TSS cannot be
	 * read-only because the CPU writes to it on task switches.
	 *
	 * On Xen PV, the GDT must be read-only because the hypervisor
	 * requires it.
	 */
	pgprot_t gdt_prot = boot_cpu_has(X86_FEATURE_XENPV) ?
		PAGE_KERNEL_RO : PAGE_KERNEL;
	pgprot_t tss_prot = PAGE_KERNEL;
#endif

	__set_fixmap(get_cpu_entry_area_index(cpu, gdt), get_cpu_gdt_paddr(cpu), gdt_prot);
	set_percpu_fixmap_pages(get_cpu_entry_area_index(cpu, entry_stack_page),
				per_cpu_ptr(&entry_stack_storage, cpu), 1,
				PAGE_KERNEL);

	/*
	 * The Intel SDM says (Volume 3, 7.2.1):
	 *
	 *  Avoid placing a page boundary in the part of the TSS that the
	 *  processor reads during a task switch (the first 104 bytes). The
	 *  processor may not correctly perform address translations if a
	 *  boundary occurs in this area. During a task switch, the processor
	 *  reads and writes into the first 104 bytes of each TSS (using
	 *  contiguous physical addresses beginning with the physical address
	 *  of the first byte of the TSS). So, after TSS access begins, if
	 *  part of the 104 bytes is not physically contiguous, the processor
	 *  will access incorrect information without generating a page-fault
	 *  exception.
	 *
	 * There are also a lot of errata involving the TSS spanning a page
	 * boundary.  Assert that we're not doing that.
	 */
	BUILD_BUG_ON((offsetof(struct tss_struct, x86_tss) ^
		      offsetofend(struct tss_struct, x86_tss)) & PAGE_MASK);
	BUILD_BUG_ON(sizeof(struct tss_struct) % PAGE_SIZE != 0);
	set_percpu_fixmap_pages(get_cpu_entry_area_index(cpu, tss),
				&per_cpu(cpu_tss_rw, cpu),
				sizeof(struct tss_struct) / PAGE_SIZE,
				tss_prot);

#ifdef CONFIG_X86_32
	per_cpu(cpu_entry_area, cpu) = get_cpu_entry_area(cpu);
#endif

#ifdef CONFIG_X86_64
	BUILD_BUG_ON(sizeof(exception_stacks) % PAGE_SIZE != 0);
	BUILD_BUG_ON(sizeof(exception_stacks) !=
		     sizeof(((struct cpu_entry_area *)0)->exception_stacks));
	set_percpu_fixmap_pages(get_cpu_entry_area_index(cpu, exception_stacks),
				&per_cpu(exception_stacks, cpu),
				sizeof(exception_stacks) / PAGE_SIZE,
				PAGE_KERNEL);

	__set_fixmap(get_cpu_entry_area_index(cpu, entry_trampoline),
		     __pa_symbol(_entry_trampoline), PAGE_KERNEL_RX);
#endif
}

void __init setup_cpu_entry_areas(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		setup_cpu_entry_area(cpu);
}
