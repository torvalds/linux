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

#ifdef CONFIG_DEBUG_PER_CPU_MAPS
# define DBG(x...) printk(KERN_DEBUG x)
#else
# define DBG(x...)
#endif

/*
 * Could be inside CONFIG_HAVE_SETUP_PER_CPU_AREA with other stuff but
 * voyager wants cpu_number too.
 */
#ifdef CONFIG_SMP
DEFINE_PER_CPU(int, cpu_number);
EXPORT_PER_CPU_SYMBOL(cpu_number);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
unsigned int num_processors;
unsigned disabled_cpus __cpuinitdata;
/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
EXPORT_SYMBOL(boot_cpu_physical_apicid);
unsigned int max_physical_apicid;

/* Bitmask of physically existing CPUs */
physid_mask_t phys_cpu_present_map;
#endif

/*
 * Map cpu index to physical APIC ID
 */
DEFINE_EARLY_PER_CPU(u16, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid, BAD_APICID);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

#ifdef CONFIG_X86_64

/* correctly size the local cpu masks */
static void setup_cpu_local_masks(void)
{
	alloc_bootmem_cpumask_var(&cpu_initialized_mask);
	alloc_bootmem_cpumask_var(&cpu_callin_mask);
	alloc_bootmem_cpumask_var(&cpu_callout_mask);
	alloc_bootmem_cpumask_var(&cpu_sibling_setup_mask);
}

#else /* CONFIG_X86_32 */

static inline void setup_cpu_local_masks(void)
{
}

#endif /* CONFIG_X86_32 */

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA

#ifdef CONFIG_X86_64
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly = {
	[0] = (unsigned long)__per_cpu_load,
};
#else
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
#endif
EXPORT_SYMBOL(__per_cpu_offset);

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{
	ssize_t size, old_size;
	char *ptr;
	int cpu;
	unsigned long align = 1;

	/* Copy section for each CPU (we discard the original) */
	old_size = PERCPU_ENOUGH_ROOM;
	align = max_t(unsigned long, PAGE_SIZE, align);
	size = roundup(old_size, align);

	pr_info("NR_CPUS:%d nr_cpumask_bits:%d nr_cpu_ids:%d nr_node_ids:%d\n",
		NR_CPUS, nr_cpumask_bits, nr_cpu_ids, nr_node_ids);

	pr_info("PERCPU: Allocating %zd bytes of per cpu data\n", size);

	for_each_possible_cpu(cpu) {
#ifndef CONFIG_NEED_MULTIPLE_NODES
		ptr = __alloc_bootmem(size, align,
				 __pa(MAX_DMA_ADDRESS));
#else
		int node = early_cpu_to_node(cpu);
		if (!node_online(node) || !NODE_DATA(node)) {
			ptr = __alloc_bootmem(size, align,
					 __pa(MAX_DMA_ADDRESS));
			pr_info("cpu %d has no node %d or node-local memory\n",
				cpu, node);
			pr_debug("per cpu data for cpu%d at %016lx\n",
				 cpu, __pa(ptr));
		} else {
			ptr = __alloc_bootmem_node(NODE_DATA(node), size, align,
							__pa(MAX_DMA_ADDRESS));
			pr_debug("per cpu data for cpu%d on node%d at %016lx\n",
				cpu, node, __pa(ptr));
		}
#endif

		memcpy(ptr, __per_cpu_load, __per_cpu_end - __per_cpu_start);
		per_cpu_offset(cpu) = ptr - __per_cpu_start;
		per_cpu(this_cpu_off, cpu) = per_cpu_offset(cpu);
		per_cpu(cpu_number, cpu) = cpu;
		/*
		 * Copy data used in early init routines from the initial arrays to the
		 * per cpu data areas.  These arrays then become expendable and the
		 * *_early_ptr's are zeroed indicating that the static arrays are gone.
		 */
		per_cpu(x86_cpu_to_apicid, cpu) =
				early_per_cpu_map(x86_cpu_to_apicid, cpu);
		per_cpu(x86_bios_cpu_apicid, cpu) =
				early_per_cpu_map(x86_bios_cpu_apicid, cpu);
#ifdef CONFIG_X86_64
		per_cpu(irq_stack_ptr, cpu) =
			per_cpu(irq_stack_union.irq_stack, cpu) + IRQ_STACK_SIZE - 64;
#ifdef CONFIG_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
				early_per_cpu_map(x86_cpu_to_node_map, cpu);
#endif
		/*
		 * Up to this point, CPU0 has been using .data.init
		 * area.  Reload %gs offset for CPU0.
		 */
		if (cpu == 0)
			load_gs_base(cpu);
#endif

		DBG("PERCPU: cpu %4d %p\n", cpu, ptr);
	}

	/* indicate the early static arrays will soon be gone */
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#if defined(CONFIG_X86_64) && defined(CONFIG_NUMA)
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();

	/* Setup cpu initialized, callin, callout masks */
	setup_cpu_local_masks();
}

#endif

