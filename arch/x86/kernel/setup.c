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

unsigned int num_processors;
unsigned disabled_cpus __cpuinitdata;
/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
EXPORT_SYMBOL(boot_cpu_physical_apicid);

physid_mask_t phys_cpu_present_map;

DEFINE_PER_CPU(u16, x86_cpu_to_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_cpu_to_apicid);

/* Bitmask of physically existing CPUs */
physid_mask_t phys_cpu_present_map;

#if defined(CONFIG_HAVE_SETUP_PER_CPU_AREA) && defined(CONFIG_SMP)
/*
 * Copy data used in early init routines from the initial arrays to the
 * per cpu data areas.  These arrays then become expendable and the
 * *_early_ptr's are zeroed indicating that the static arrays are gone.
 */
static void __init setup_per_cpu_maps(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		per_cpu(x86_cpu_to_apicid, cpu) = x86_cpu_to_apicid_init[cpu];
		per_cpu(x86_bios_cpu_apicid, cpu) =
						x86_bios_cpu_apicid_init[cpu];
#ifdef CONFIG_NUMA
		per_cpu(x86_cpu_to_node_map, cpu) =
						x86_cpu_to_node_map_init[cpu];
#endif
	}

	/* indicate the early static arrays will soon be gone */
	x86_cpu_to_apicid_early_ptr = NULL;
	x86_bios_cpu_apicid_early_ptr = NULL;
#ifdef CONFIG_NUMA
	x86_cpu_to_node_map_early_ptr = NULL;
#endif
}

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
	int i;
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
			       "cpu %d has no node or node-local memory\n", i);
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
	}

	/* Setup percpu data maps */
	setup_per_cpu_maps();
}

#endif
