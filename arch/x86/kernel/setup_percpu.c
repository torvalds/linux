#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/percpu.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/smp.h>
#include <linux/topology.h>
#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/highmem.h>
#include <asm/proto.h>
#include <asm/cpumask.h>
#include <asm/cpu.h>
#include <asm/stackprotector.h>

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
# define DBG(x...) printk(KERN_DEBUG x)
#else
# define DBG(x...)
#endif

DEFINE_PER_CPU(int, cpu_number);
EXPORT_PER_CPU_SYMBOL(cpu_number);

#ifdef CONFIG_X86_64
#define BOOT_PERCPU_OFFSET ((unsigned long)__per_cpu_load)
#else
#define BOOT_PERCPU_OFFSET 0
#endif

DEFINE_PER_CPU(unsigned long, this_cpu_off) = BOOT_PERCPU_OFFSET;
EXPORT_PER_CPU_SYMBOL(this_cpu_off);

unsigned long __per_cpu_offset[NR_CPUS] __read_mostly = {
	[0 ... NR_CPUS-1] = BOOT_PERCPU_OFFSET,
};
EXPORT_SYMBOL(__per_cpu_offset);

static inline void setup_percpu_segment(int cpu)
{
#ifdef CONFIG_X86_32
	struct desc_struct gdt;

	pack_descriptor(&gdt, per_cpu_offset(cpu), 0xFFFFF,
			0x2 | DESCTYPE_S, 0x8);
	gdt.s = 1;
	write_gdt_entry(get_cpu_gdt_table(cpu),
			GDT_ENTRY_PERCPU, &gdt, DESCTYPE_S);
#endif
}

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{
	ssize_t size = __per_cpu_end - __per_cpu_start;
	unsigned int nr_cpu_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	static struct page **pages;
	size_t pages_size;
	unsigned int cpu, i, j;
	unsigned long delta;
	size_t pcpu_unit_size;

	pr_info("NR_CPUS:%d nr_cpumask_bits:%d nr_cpu_ids:%d nr_node_ids:%d\n",
		NR_CPUS, nr_cpumask_bits, nr_cpu_ids, nr_node_ids);
	pr_info("PERCPU: Allocating %zd bytes for static per cpu data\n", size);

	pages_size = nr_cpu_pages * num_possible_cpus() * sizeof(pages[0]);
	pages = alloc_bootmem(pages_size);

	j = 0;
	for_each_possible_cpu(cpu) {
		void *ptr;

		for (i = 0; i < nr_cpu_pages; i++) {
#ifndef CONFIG_NEED_MULTIPLE_NODES
			ptr = alloc_bootmem_pages(PAGE_SIZE);
#else
			int node = early_cpu_to_node(cpu);

			if (!node_online(node) || !NODE_DATA(node)) {
				ptr = alloc_bootmem_pages(PAGE_SIZE);
				pr_info("cpu %d has no node %d or node-local "
					"memory\n", cpu, node);
				pr_debug("per cpu data for cpu%d at %016lx\n",
					 cpu, __pa(ptr));
			} else {
				ptr = alloc_bootmem_pages_node(NODE_DATA(node),
							       PAGE_SIZE);
				pr_debug("per cpu data for cpu%d on node%d "
					 "at %016lx\n", cpu, node, __pa(ptr));
			}
#endif
			memcpy(ptr, __per_cpu_load + i * PAGE_SIZE, PAGE_SIZE);
			pages[j++] = virt_to_page(ptr);
		}
	}

	pcpu_unit_size = pcpu_setup_static(populate_extra_pte, pages, size);

	free_bootmem(__pa(pages), pages_size);

	delta = (unsigned long)pcpu_base_addr - (unsigned long)__per_cpu_start;
	for_each_possible_cpu(cpu) {
		per_cpu_offset(cpu) = delta + cpu * pcpu_unit_size;
		per_cpu(this_cpu_off, cpu) = per_cpu_offset(cpu);
		per_cpu(cpu_number, cpu) = cpu;
		setup_percpu_segment(cpu);
		setup_stack_canary_segment(cpu);
		/*
		 * Copy data used in early init routines from the
		 * initial arrays to the per cpu data areas.  These
		 * arrays then become expendable and the *_early_ptr's
		 * are zeroed indicating that the static arrays are
		 * gone.
		 */
#ifdef CONFIG_X86_LOCAL_APIC
		per_cpu(x86_cpu_to_apicid, cpu) =
			early_per_cpu_map(x86_cpu_to_apicid, cpu);
		per_cpu(x86_bios_cpu_apicid, cpu) =
			early_per_cpu_map(x86_bios_cpu_apicid, cpu);
#endif
#ifdef CONFIG_X86_64
		per_cpu(irq_stack_ptr, cpu) =
			per_cpu(irq_stack_union.irq_stack, cpu) +
			IRQ_STACK_SIZE - 64;
#ifdef CONFIG_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
			early_per_cpu_map(x86_cpu_to_node_map, cpu);
#endif
#endif
		/*
		 * Up to this point, the boot CPU has been using .data.init
		 * area.  Reload any changed state for the boot CPU.
		 */
		if (cpu == boot_cpu_id)
			switch_to_new_gdt(cpu);

		DBG("PERCPU: cpu %4d %p\n", cpu, ptr);
	}

	/* indicate the early static arrays will soon be gone */
#ifdef CONFIG_X86_LOCAL_APIC
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#endif
#if defined(CONFIG_X86_64) && defined(CONFIG_NUMA)
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();

	/* Setup cpu initialized, callin, callout masks */
	setup_cpu_local_masks();
}
