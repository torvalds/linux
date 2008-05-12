#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/percpu.h>
#include <asm/smp.h>
#include <asm/percpu.h>
#include <asm/sections.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/topology.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>

#ifdef CONFIG_X86_LOCAL_APIC
unsigned int num_processors;
unsigned disabled_cpus __cpuinitdata;
/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
EXPORT_SYMBOL(boot_cpu_physical_apicid);

/* Bitmask of physically existing CPUs */
physid_mask_t phys_cpu_present_map;
#endif

/* map cpu index to physical APIC ID */
DEFINE_EARLY_PER_CPU(u16, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid, BAD_APICID);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

#if defined(CONFIG_NUMA) && defined(CONFIG_X86_64)
#define	X86_64_NUMA	1

DEFINE_EARLY_PER_CPU(int, x86_cpu_to_node_map, NUMA_NO_NODE);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_node_map);
#endif

#if defined(CONFIG_HAVE_SETUP_PER_CPU_AREA) && defined(CONFIG_X86_SMP)
/*
 * Copy data used in early init routines from the initial arrays to the
 * per cpu data areas.  These arrays then become expendable and the
 * *_early_ptr's are zeroed indicating that the static arrays are gone.
 */
static void __init setup_per_cpu_maps(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		per_cpu(x86_cpu_to_apicid, cpu) =
				early_per_cpu_map(x86_cpu_to_apicid, cpu);
		per_cpu(x86_bios_cpu_apicid, cpu) =
				early_per_cpu_map(x86_bios_cpu_apicid, cpu);
#ifdef X86_64_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
				early_per_cpu_map(x86_cpu_to_node_map, cpu);
#endif
	}

	/* indicate the early static arrays will soon be gone */
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#ifdef X86_64_NUMA
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif
}

#ifdef CONFIG_HAVE_CPUMASK_OF_CPU_MAP
cpumask_t *cpumask_of_cpu_map __read_mostly;
EXPORT_SYMBOL(cpumask_of_cpu_map);

/* requires nr_cpu_ids to be initialized */
static void __init setup_cpumask_of_cpu(void)
{
	int i;

	/* alloc_bootmem zeroes memory */
	cpumask_of_cpu_map = alloc_bootmem_low(sizeof(cpumask_t) * nr_cpu_ids);
	for (i = 0; i < nr_cpu_ids; i++)
		cpu_set(i, cpumask_of_cpu_map[i]);
}
#else
static inline void setup_cpumask_of_cpu(void) { }
#endif

#ifdef CONFIG_X86_32
/*
 * Great future not-so-futuristic plan: make i386 and x86_64 do it
 * the same way
 */
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);
#endif

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{
	int i, highest_cpu = 0;
	unsigned long size;

#ifdef CONFIG_HOTPLUG_CPU
	prefill_possible_map();
#endif

	/* Copy section for each CPU (we discard the original) */
	size = PERCPU_ENOUGH_ROOM;
	printk(KERN_INFO "PERCPU: Allocating %lu bytes of per cpu data\n",
			  size);

	for_each_possible_cpu(i) {
		char *ptr;
#ifndef CONFIG_NEED_MULTIPLE_NODES
		ptr = alloc_bootmem_pages(size);
#else
		int node = early_cpu_to_node(i);
		if (!node_online(node) || !NODE_DATA(node)) {
			ptr = alloc_bootmem_pages(size);
			printk(KERN_INFO
			       "cpu %d has no node %d or node-local memory\n",
				i, node);
		}
		else
			ptr = alloc_bootmem_pages_node(NODE_DATA(node), size);
#endif
		if (!ptr)
			panic("Cannot allocate cpu data for CPU %d\n", i);
#ifdef CONFIG_X86_64
		cpu_pda(i)->data_offset = ptr - __per_cpu_start;
#else
		__per_cpu_offset[i] = ptr - __per_cpu_start;
#endif
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);

		highest_cpu = i;
	}

	nr_cpu_ids = highest_cpu + 1;
	printk(KERN_DEBUG "NR_CPUS: %d, nr_cpu_ids: %d\n", NR_CPUS, nr_cpu_ids);

	/* Setup percpu data maps */
	setup_per_cpu_maps();

	/* Setup cpumask_of_cpu map */
	setup_cpumask_of_cpu();
}

#endif

#ifdef X86_64_NUMA
void __cpuinit numa_set_node(int cpu, int node)
{
	int *cpu_to_node_map = early_per_cpu_ptr(x86_cpu_to_node_map);

	if (cpu_to_node_map)
		cpu_to_node_map[cpu] = node;

	else if (per_cpu_offset(cpu))
		per_cpu(x86_cpu_to_node_map, cpu) = node;

	else
		Dprintk(KERN_INFO "Setting node for non-present cpu %d\n", cpu);
}

void __cpuinit numa_clear_node(int cpu)
{
	numa_set_node(cpu, NUMA_NO_NODE);
}

void __cpuinit numa_add_cpu(int cpu)
{
	cpu_set(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	cpu_clear(cpu, node_to_cpumask_map[cpu_to_node(cpu)]);
}
#endif /* CONFIG_NUMA */

#if defined(CONFIG_DEBUG_PER_CPU_MAPS) && defined(CONFIG_X86_64)

int cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map)) {
		printk(KERN_WARNING
			"cpu_to_node(%d): usage too early!\n", cpu);
		dump_stack();
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}
EXPORT_SYMBOL(cpu_to_node);

int early_cpu_to_node(int cpu)
{
	if (early_per_cpu_ptr(x86_cpu_to_node_map))
		return early_per_cpu_ptr(x86_cpu_to_node_map)[cpu];

	if (!per_cpu_offset(cpu)) {
		printk(KERN_WARNING
			"early_cpu_to_node(%d): no per_cpu area!\n", cpu);
			dump_stack();
		return NUMA_NO_NODE;
	}
	return per_cpu(x86_cpu_to_node_map, cpu);
}
#endif
