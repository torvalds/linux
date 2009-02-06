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

/* map cpu index to physical APIC ID */
DEFINE_EARLY_PER_CPU(u16, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid, BAD_APICID);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_bios_cpu_apicid);

#if defined(CONFIG_NUMA) && defined(CONFIG_X86_64)
#define	X86_64_NUMA	1

/* map cpu index to node index */
DEFINE_EARLY_PER_CPU(int, x86_cpu_to_node_map, NUMA_NO_NODE);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_node_map);

/* which logical CPUs are on which nodes */
cpumask_t *node_to_cpumask_map;
EXPORT_SYMBOL(node_to_cpumask_map);

/* setup node_to_cpumask_map */
static void __init setup_node_to_cpumask_map(void);

#else
static inline void setup_node_to_cpumask_map(void) { }
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

#ifdef CONFIG_X86_32
/*
 * Great future not-so-futuristic plan: make i386 and x86_64 do it
 * the same way
 */
unsigned long __per_cpu_offset[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(__per_cpu_offset);
static inline void setup_cpu_pda_map(void) { }

#elif !defined(CONFIG_SMP)
static inline void setup_cpu_pda_map(void) { }

#else /* CONFIG_SMP && CONFIG_X86_64 */

/*
 * Allocate cpu_pda pointer table and array via alloc_bootmem.
 */
static void __init setup_cpu_pda_map(void)
{
	char *pda;
	struct x8664_pda **new_cpu_pda;
	unsigned long size;
	int cpu;

	size = roundup(sizeof(struct x8664_pda), cache_line_size());

	/* allocate cpu_pda array and pointer table */
	{
		unsigned long tsize = nr_cpu_ids * sizeof(void *);
		unsigned long asize = size * (nr_cpu_ids - 1);

		tsize = roundup(tsize, cache_line_size());
		new_cpu_pda = alloc_bootmem(tsize + asize);
		pda = (char *)new_cpu_pda + tsize;
	}

	/* initialize pointer table to static pda's */
	for_each_possible_cpu(cpu) {
		if (cpu == 0) {
			/* leave boot cpu pda in place */
			new_cpu_pda[0] = cpu_pda(0);
			continue;
		}
		new_cpu_pda[cpu] = (struct x8664_pda *)pda;
		new_cpu_pda[cpu]->in_bootmem = 1;
		pda += size;
	}

	/* point to new pointer table */
	_cpu_pda = new_cpu_pda;
}

#endif /* CONFIG_SMP && CONFIG_X86_64 */

#ifdef CONFIG_X86_64

/* correctly size the local cpu masks */
static void __init setup_cpu_local_masks(void)
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

	/* Setup cpu_pda map */
	setup_cpu_pda_map();

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
		per_cpu_offset(cpu) = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
	}

	/* Setup percpu data maps */
	setup_per_cpu_maps();

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();

	/* Setup cpu initialized, callin, callout masks */
	setup_cpu_local_masks();
}

#endif

#ifdef X86_64_NUMA

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: node_to_cpumask() is not valid until after this is done.
 */
static void __init setup_node_to_cpumask_map(void)
{
	unsigned int node, num = 0;
	cpumask_t *map;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES) {
		for_each_node_mask(node, node_possible_map)
			num = node;
		nr_node_ids = num + 1;
	}

	/* allocate the map */
	map = alloc_bootmem_low(nr_node_ids * sizeof(cpumask_t));

	pr_debug("Node to cpumask map at %p for %d nodes\n",
		 map, nr_node_ids);

	/* node_to_cpumask() will now work */
	node_to_cpumask_map = map;
}

void __cpuinit numa_set_node(int cpu, int node)
{
	int *cpu_to_node_map = early_per_cpu_ptr(x86_cpu_to_node_map);

	if (cpu_pda(cpu) && node != NUMA_NO_NODE)
		cpu_pda(cpu)->nodenumber = node;

	if (cpu_to_node_map)
		cpu_to_node_map[cpu] = node;

	else if (per_cpu_offset(cpu))
		per_cpu(x86_cpu_to_node_map, cpu) = node;

	else
		pr_debug("Setting node for non-present cpu %d\n", cpu);
}

void __cpuinit numa_clear_node(int cpu)
{
	numa_set_node(cpu, NUMA_NO_NODE);
}

#ifndef CONFIG_DEBUG_PER_CPU_MAPS

void __cpuinit numa_add_cpu(int cpu)
{
	cpu_set(cpu, node_to_cpumask_map[early_cpu_to_node(cpu)]);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	cpu_clear(cpu, node_to_cpumask_map[cpu_to_node(cpu)]);
}

#else /* CONFIG_DEBUG_PER_CPU_MAPS */

/*
 * --------- debug versions of the numa functions ---------
 */
static void __cpuinit numa_set_cpumask(int cpu, int enable)
{
	int node = cpu_to_node(cpu);
	cpumask_t *mask;
	char buf[64];

	if (node_to_cpumask_map == NULL) {
		printk(KERN_ERR "node_to_cpumask_map NULL\n");
		dump_stack();
		return;
	}

	mask = &node_to_cpumask_map[node];
	if (enable)
		cpu_set(cpu, *mask);
	else
		cpu_clear(cpu, *mask);

	cpulist_scnprintf(buf, sizeof(buf), mask);
	printk(KERN_DEBUG "%s cpu %d node %d: mask now %s\n",
		enable ? "numa_add_cpu" : "numa_remove_cpu", cpu, node, buf);
}

void __cpuinit numa_add_cpu(int cpu)
{
	numa_set_cpumask(cpu, 1);
}

void __cpuinit numa_remove_cpu(int cpu)
{
	numa_set_cpumask(cpu, 0);
}

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

/*
 * Same function as cpu_to_node() but used if called before the
 * per_cpu areas are setup.
 */
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


/* empty cpumask */
static const cpumask_t cpu_mask_none;

/*
 * Returns a pointer to the bitmask of CPUs on Node 'node'.
 */
const cpumask_t *cpumask_of_node(int node)
{
	if (node_to_cpumask_map == NULL) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): no node_to_cpumask_map!\n",
			node);
		dump_stack();
		return (const cpumask_t *)&cpu_online_map;
	}
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"cpumask_of_node(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return &cpu_mask_none;
	}
	return &node_to_cpumask_map[node];
}
EXPORT_SYMBOL(cpumask_of_node);

/*
 * Returns a bitmask of CPUs on Node 'node'.
 *
 * Side note: this function creates the returned cpumask on the stack
 * so with a high NR_CPUS count, excessive stack space is used.  The
 * node_to_cpumask_ptr function should be used whenever possible.
 */
cpumask_t node_to_cpumask(int node)
{
	if (node_to_cpumask_map == NULL) {
		printk(KERN_WARNING
			"node_to_cpumask(%d): no node_to_cpumask_map!\n", node);
		dump_stack();
		return cpu_online_map;
	}
	if (node >= nr_node_ids) {
		printk(KERN_WARNING
			"node_to_cpumask(%d): node > nr_node_ids(%d)\n",
			node, nr_node_ids);
		dump_stack();
		return cpu_mask_none;
	}
	return node_to_cpumask_map[node];
}
EXPORT_SYMBOL(node_to_cpumask);

/*
 * --------- end of debug versions of the numa functions ---------
 */

#endif /* CONFIG_DEBUG_PER_CPU_MAPS */

#endif /* X86_64_NUMA */

