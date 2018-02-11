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

struct cpu_entry_area *get_cpu_entry_area(int cpu)
{
	unsigned long va = CPU_ENTRY_AREA_PER_CPU + cpu * CPU_ENTRY_AREA_SIZE;
	BUILD_BUG_ON(sizeof(struct cpu_entry_area) % PAGE_SIZE != 0);

	return (struct cpu_entry_area *) va;
}
EXPORT_SYMBOL(get_cpu_entry_area);

void cea_set_pte(void *cea_vaddr, phys_addr_t pa, pgprot_t flags)
{
	unsigned long va = (unsigned long) cea_vaddr;

	set_pte_vaddr(va, pfn_pte(pa >> PAGE_SHIFT, flags));
}

static void __init
cea_map_percpu_pages(void *cea_vaddr, void *ptr, int pages, pgprot_t prot)
{
	for ( ; pages; pages--, cea_vaddr+= PAGE_SIZE, ptr += PAGE_SIZE)
		cea_set_pte(cea_vaddr, per_cpu_ptr_to_phys(ptr), prot);
}

static void percpu_setup_debug_store(int cpu)
{
#ifdef CONFIG_CPU_SUP_INTEL
	int npages;
	void *cea;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return;

	cea = &get_cpu_entry_area(cpu)->cpu_debug_store;
	npages = sizeof(struct debug_store) / PAGE_SIZE;
	BUILD_BUG_ON(sizeof(struct debug_store) % PAGE_SIZE != 0);
	cea_map_percpu_pages(cea, &per_cpu(cpu_debug_store, cpu), npages,
			     PAGE_KERNEL);

	cea = &get_cpu_entry_area(cpu)->cpu_debug_buffers;
	/*
	 * Force the population of PMDs for not yet allocated per cpu
	 * memory like debug store buffers.
	 */
	npages = sizeof(struct debug_store_buffers) / PAGE_SIZE;
	for (; npages; npages--, cea += PAGE_SIZE)
		cea_set_pte(cea, 0, PAGE_NONE);
#endif
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

	cea_set_pte(&get_cpu_entry_area(cpu)->gdt, get_cpu_gdt_paddr(cpu),
		    gdt_prot);

	cea_map_percpu_pages(&get_cpu_entry_area(cpu)->entry_stack_page,
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
	cea_map_percpu_pages(&get_cpu_entry_area(cpu)->tss,
			     &per_cpu(cpu_tss_rw, cpu),
			     sizeof(struct tss_struct) / PAGE_SIZE, tss_prot);

#ifdef CONFIG_X86_32
	per_cpu(cpu_entry_area, cpu) = get_cpu_entry_area(cpu);
#endif

#ifdef CONFIG_X86_64
	BUILD_BUG_ON(sizeof(exception_stacks) % PAGE_SIZE != 0);
	BUILD_BUG_ON(sizeof(exception_stacks) !=
		     sizeof(((struct cpu_entry_area *)0)->exception_stacks));
	cea_map_percpu_pages(&get_cpu_entry_area(cpu)->exception_stacks,
			     &per_cpu(exception_stacks, cpu),
			     sizeof(exception_stacks) / PAGE_SIZE, PAGE_KERNEL);

	cea_set_pte(&get_cpu_entry_area(cpu)->entry_trampoline,
		     __pa_symbol(_entry_trampoline), PAGE_KERNEL_RX);
#endif
	percpu_setup_debug_store(cpu);
}

static __init void setup_cpu_entry_area_ptes(void)
{
#ifdef CONFIG_X86_32
	unsigned long start, end;

	BUILD_BUG_ON(CPU_ENTRY_AREA_PAGES * PAGE_SIZE < CPU_ENTRY_AREA_MAP_SIZE);
	BUG_ON(CPU_ENTRY_AREA_BASE & ~PMD_MASK);

	start = CPU_ENTRY_AREA_BASE;
	end = start + CPU_ENTRY_AREA_MAP_SIZE;

	/* Careful here: start + PMD_SIZE might wrap around */
	for (; start < end && start >= CPU_ENTRY_AREA_BASE; start += PMD_SIZE)
		populate_extra_pte(start);
#endif
}

void __init setup_cpu_entry_areas(void)
{
	unsigned int cpu;

	setup_cpu_entry_area_ptes();

	for_each_possible_cpu(cpu)
		setup_cpu_entry_area(cpu);
}
