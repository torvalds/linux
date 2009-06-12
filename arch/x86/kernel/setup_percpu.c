#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/percpu.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/smp.h>
#include <linux/topology.h>
#include <linux/pfn.h>
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

/*
 * On x86_64 symbols referenced from code should be reachable using
 * 32bit relocations.  Reserve space for static percpu variables in
 * modules so that they are always served from the first chunk which
 * is located at the percpu segment base.  On x86_32, anything can
 * address anywhere.  No need to reserve space in the first chunk.
 */
#ifdef CONFIG_X86_64
#define PERCPU_FIRST_CHUNK_RESERVE	PERCPU_MODULE_RESERVE
#else
#define PERCPU_FIRST_CHUNK_RESERVE	0
#endif

/**
 * pcpu_need_numa - determine percpu allocation needs to consider NUMA
 *
 * If NUMA is not configured or there is only one NUMA node available,
 * there is no reason to consider NUMA.  This function determines
 * whether percpu allocation should consider NUMA or not.
 *
 * RETURNS:
 * true if NUMA should be considered; otherwise, false.
 */
static bool __init pcpu_need_numa(void)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	pg_data_t *last = NULL;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		int node = early_cpu_to_node(cpu);

		if (node_online(node) && NODE_DATA(node) &&
		    last && last != NODE_DATA(node))
			return true;

		last = NODE_DATA(node);
	}
#endif
	return false;
}

/**
 * pcpu_alloc_bootmem - NUMA friendly alloc_bootmem wrapper for percpu
 * @cpu: cpu to allocate for
 * @size: size allocation in bytes
 * @align: alignment
 *
 * Allocate @size bytes aligned at @align for cpu @cpu.  This wrapper
 * does the right thing for NUMA regardless of the current
 * configuration.
 *
 * RETURNS:
 * Pointer to the allocated area on success, NULL on failure.
 */
static void * __init pcpu_alloc_bootmem(unsigned int cpu, unsigned long size,
					unsigned long align)
{
	const unsigned long goal = __pa(MAX_DMA_ADDRESS);
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int node = early_cpu_to_node(cpu);
	void *ptr;

	if (!node_online(node) || !NODE_DATA(node)) {
		ptr = __alloc_bootmem_nopanic(size, align, goal);
		pr_info("cpu %d has no node %d or node-local memory\n",
			cpu, node);
		pr_debug("per cpu data for cpu%d %lu bytes at %016lx\n",
			 cpu, size, __pa(ptr));
	} else {
		ptr = __alloc_bootmem_node_nopanic(NODE_DATA(node),
						   size, align, goal);
		pr_debug("per cpu data for cpu%d %lu bytes on node%d at "
			 "%016lx\n", cpu, size, node, __pa(ptr));
	}
	return ptr;
#else
	return __alloc_bootmem_nopanic(size, align, goal);
#endif
}

/*
 * Remap allocator
 *
 * This allocator uses PMD page as unit.  A PMD page is allocated for
 * each cpu and each is remapped into vmalloc area using PMD mapping.
 * As PMD page is quite large, only part of it is used for the first
 * chunk.  Unused part is returned to the bootmem allocator.
 *
 * So, the PMD pages are mapped twice - once to the physical mapping
 * and to the vmalloc area for the first percpu chunk.  The double
 * mapping does add one more PMD TLB entry pressure but still is much
 * better than only using 4k mappings while still being NUMA friendly.
 */
#ifdef CONFIG_NEED_MULTIPLE_NODES
static size_t pcpur_size __initdata;
static void **pcpur_ptrs __initdata;

static struct page * __init pcpur_get_page(unsigned int cpu, int pageno)
{
	size_t off = (size_t)pageno << PAGE_SHIFT;

	if (off >= pcpur_size)
		return NULL;

	return virt_to_page(pcpur_ptrs[cpu] + off);
}

static ssize_t __init setup_pcpu_remap(size_t static_size)
{
	static struct vm_struct vm;
	size_t ptrs_size, dyn_size;
	unsigned int cpu;
	ssize_t ret;

	/*
	 * If large page isn't supported, there's no benefit in doing
	 * this.  Also, on non-NUMA, embedding is better.
	 *
	 * NOTE: disabled for now.
	 */
	if (true || !cpu_has_pse || !pcpu_need_numa())
		return -EINVAL;

	/*
	 * Currently supports only single page.  Supporting multiple
	 * pages won't be too difficult if it ever becomes necessary.
	 */
	pcpur_size = PFN_ALIGN(static_size + PERCPU_MODULE_RESERVE +
			       PERCPU_DYNAMIC_RESERVE);
	if (pcpur_size > PMD_SIZE) {
		pr_warning("PERCPU: static data is larger than large page, "
			   "can't use large page\n");
		return -EINVAL;
	}
	dyn_size = pcpur_size - static_size - PERCPU_FIRST_CHUNK_RESERVE;

	/* allocate pointer array and alloc large pages */
	ptrs_size = PFN_ALIGN(num_possible_cpus() * sizeof(pcpur_ptrs[0]));
	pcpur_ptrs = alloc_bootmem(ptrs_size);

	for_each_possible_cpu(cpu) {
		pcpur_ptrs[cpu] = pcpu_alloc_bootmem(cpu, PMD_SIZE, PMD_SIZE);
		if (!pcpur_ptrs[cpu])
			goto enomem;

		/*
		 * Only use pcpur_size bytes and give back the rest.
		 *
		 * Ingo: The 2MB up-rounding bootmem is needed to make
		 * sure the partial 2MB page is still fully RAM - it's
		 * not well-specified to have a PAT-incompatible area
		 * (unmapped RAM, device memory, etc.) in that hole.
		 */
		free_bootmem(__pa(pcpur_ptrs[cpu] + pcpur_size),
			     PMD_SIZE - pcpur_size);

		memcpy(pcpur_ptrs[cpu], __per_cpu_load, static_size);
	}

	/* allocate address and map */
	vm.flags = VM_ALLOC;
	vm.size = num_possible_cpus() * PMD_SIZE;
	vm_area_register_early(&vm, PMD_SIZE);

	for_each_possible_cpu(cpu) {
		pmd_t *pmd;

		pmd = populate_extra_pmd((unsigned long)vm.addr
					 + cpu * PMD_SIZE);
		set_pmd(pmd, pfn_pmd(page_to_pfn(virt_to_page(pcpur_ptrs[cpu])),
				     PAGE_KERNEL_LARGE));
	}

	/* we're ready, commit */
	pr_info("PERCPU: Remapped at %p with large pages, static data "
		"%zu bytes\n", vm.addr, static_size);

	ret = pcpu_setup_first_chunk(pcpur_get_page, static_size,
				     PERCPU_FIRST_CHUNK_RESERVE, dyn_size,
				     PMD_SIZE, vm.addr, NULL);
	goto out_free_ar;

enomem:
	for_each_possible_cpu(cpu)
		if (pcpur_ptrs[cpu])
			free_bootmem(__pa(pcpur_ptrs[cpu]), PMD_SIZE);
	ret = -ENOMEM;
out_free_ar:
	free_bootmem(__pa(pcpur_ptrs), ptrs_size);
	return ret;
}
#else
static ssize_t __init setup_pcpu_remap(size_t static_size)
{
	return -EINVAL;
}
#endif

/*
 * Embedding allocator
 *
 * The first chunk is sized to just contain the static area plus
 * module and dynamic reserves and embedded into linear physical
 * mapping so that it can use PMD mapping without additional TLB
 * pressure.
 */
static ssize_t __init setup_pcpu_embed(size_t static_size)
{
	size_t reserve = PERCPU_MODULE_RESERVE + PERCPU_DYNAMIC_RESERVE;

	/*
	 * If large page isn't supported, there's no benefit in doing
	 * this.  Also, embedding allocation doesn't play well with
	 * NUMA.
	 */
	if (!cpu_has_pse || pcpu_need_numa())
		return -EINVAL;

	return pcpu_embed_first_chunk(static_size, PERCPU_FIRST_CHUNK_RESERVE,
				      reserve - PERCPU_FIRST_CHUNK_RESERVE, -1);
}

/*
 * 4k page allocator
 *
 * This is the basic allocator.  Static percpu area is allocated
 * page-by-page and most of initialization is done by the generic
 * setup function.
 */
static struct page **pcpu4k_pages __initdata;
static int pcpu4k_nr_static_pages __initdata;

static struct page * __init pcpu4k_get_page(unsigned int cpu, int pageno)
{
	if (pageno < pcpu4k_nr_static_pages)
		return pcpu4k_pages[cpu * pcpu4k_nr_static_pages + pageno];
	return NULL;
}

static void __init pcpu4k_populate_pte(unsigned long addr)
{
	populate_extra_pte(addr);
}

static ssize_t __init setup_pcpu_4k(size_t static_size)
{
	size_t pages_size;
	unsigned int cpu;
	int i, j;
	ssize_t ret;

	pcpu4k_nr_static_pages = PFN_UP(static_size);

	/* unaligned allocations can't be freed, round up to page size */
	pages_size = PFN_ALIGN(pcpu4k_nr_static_pages * num_possible_cpus()
			       * sizeof(pcpu4k_pages[0]));
	pcpu4k_pages = alloc_bootmem(pages_size);

	/* allocate and copy */
	j = 0;
	for_each_possible_cpu(cpu)
		for (i = 0; i < pcpu4k_nr_static_pages; i++) {
			void *ptr;

			ptr = pcpu_alloc_bootmem(cpu, PAGE_SIZE, PAGE_SIZE);
			if (!ptr)
				goto enomem;

			memcpy(ptr, __per_cpu_load + i * PAGE_SIZE, PAGE_SIZE);
			pcpu4k_pages[j++] = virt_to_page(ptr);
		}

	/* we're ready, commit */
	pr_info("PERCPU: Allocated %d 4k pages, static data %zu bytes\n",
		pcpu4k_nr_static_pages, static_size);

	ret = pcpu_setup_first_chunk(pcpu4k_get_page, static_size,
				     PERCPU_FIRST_CHUNK_RESERVE, -1,
				     -1, NULL, pcpu4k_populate_pte);
	goto out_free_ar;

enomem:
	while (--j >= 0)
		free_bootmem(__pa(page_address(pcpu4k_pages[j])), PAGE_SIZE);
	ret = -ENOMEM;
out_free_ar:
	free_bootmem(__pa(pcpu4k_pages), pages_size);
	return ret;
}

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
	size_t static_size = __per_cpu_end - __per_cpu_start;
	unsigned int cpu;
	unsigned long delta;
	size_t pcpu_unit_size;
	ssize_t ret;

	pr_info("NR_CPUS:%d nr_cpumask_bits:%d nr_cpu_ids:%d nr_node_ids:%d\n",
		NR_CPUS, nr_cpumask_bits, nr_cpu_ids, nr_node_ids);

	/*
	 * Allocate percpu area.  If PSE is supported, try to make use
	 * of large page mappings.  Please read comments on top of
	 * each allocator for details.
	 */
	ret = setup_pcpu_remap(static_size);
	if (ret < 0)
		ret = setup_pcpu_embed(static_size);
	if (ret < 0)
		ret = setup_pcpu_4k(static_size);
	if (ret < 0)
		panic("cannot allocate static percpu area (%zu bytes, err=%zd)",
		      static_size, ret);

	pcpu_unit_size = ret;

	/* alrighty, percpu areas up and running */
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
	}

	/* indicate the early static arrays will soon be gone */
#ifdef CONFIG_X86_LOCAL_APIC
	early_per_cpu_ptr(x86_cpu_to_apicid) = NULL;
	early_per_cpu_ptr(x86_bios_cpu_apicid) = NULL;
#endif
#if defined(CONFIG_X86_64) && defined(CONFIG_NUMA)
	early_per_cpu_ptr(x86_cpu_to_node_map) = NULL;
#endif

#if defined(CONFIG_X86_64) && defined(CONFIG_NUMA)
	/*
	 * make sure boot cpu node_number is right, when boot cpu is on the
	 * node that doesn't have mem installed
	 */
	per_cpu(node_number, boot_cpu_id) = cpu_to_node(boot_cpu_id);
#endif

	/* Setup node to cpumask map */
	setup_node_to_cpumask_map();

	/* Setup cpu initialized, callin, callout masks */
	setup_cpu_local_masks();
}
