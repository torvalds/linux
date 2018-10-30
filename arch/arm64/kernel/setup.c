/*
 * Based on arch/arm/kernel/setup.c
 *
 * Copyright (C) 1995-2001 Russell King
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

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/cache.h>
#include <linux/screen_info.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/efi.h>
#include <linux/psci.h>
#include <linux/sched/task.h>
#include <linux/mm.h>

#include <asm/acpi.h>
#include <asm/fixmap.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/daifflags.h>
#include <asm/elf.h>
#include <asm/cpufeature.h>
#include <asm/cpu_ops.h>
#include <asm/kasan.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>
#include <asm/memblock.h>
#include <asm/efi.h>
#include <asm/xen/hypervisor.h>
#include <asm/mmu_context.h>

static int num_standard_resources;
static struct resource *standard_resources;

phys_addr_t __fdt_pointer __initdata;

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{
		.name = "Kernel code",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	},
	{
		.name = "Kernel data",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	}
};

#define kernel_code mem_res[0]
#define kernel_data mem_res[1]

/*
 * The recorded values of x0 .. x3 upon kernel entry.
 */
u64 __cacheline_aligned boot_args[4];

void __init smp_setup_processor_id(void)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
	cpu_logical_map(0) = mpidr;

	/*
	 * clear __my_cpu_offset on boot CPU to avoid hang caused by
	 * using percpu variable early, for example, lockdep will
	 * access percpu variable inside lock_release
	 */
	set_my_cpu_offset(0);
	pr_info("Booting Linux on physical CPU 0x%010lx [0x%08x]\n",
		(unsigned long)mpidr, read_cpuid_id());
}

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == cpu_logical_map(cpu);
}

struct mpidr_hash mpidr_hash;
/**
 * smp_build_mpidr_hash - Pre-compute shifts required at each affinity
 *			  level in order to build a linear index from an
 *			  MPIDR value. Resulting algorithm is a collision
 *			  free hash carried out through shifting and ORing
 */
static void __init smp_build_mpidr_hash(void)
{
	u32 i, affinity, fs[4], bits[4], ls;
	u64 mask = 0;
	/*
	 * Pre-scan the list of MPIDRS and filter out bits that do
	 * not contribute to affinity levels, ie they never toggle.
	 */
	for_each_possible_cpu(i)
		mask |= (cpu_logical_map(i) ^ cpu_logical_map(0));
	pr_debug("mask of set bits %#llx\n", mask);
	/*
	 * Find and stash the last and first bit set at all affinity levels to
	 * check how many bits are required to represent them.
	 */
	for (i = 0; i < 4; i++) {
		affinity = MPIDR_AFFINITY_LEVEL(mask, i);
		/*
		 * Find the MSB bit and LSB bits position
		 * to determine how many bits are required
		 * to express the affinity level.
		 */
		ls = fls(affinity);
		fs[i] = affinity ? ffs(affinity) - 1 : 0;
		bits[i] = ls - fs[i];
	}
	/*
	 * An index can be created from the MPIDR_EL1 by isolating the
	 * significant bits at each affinity level and by shifting
	 * them in order to compress the 32 bits values space to a
	 * compressed set of values. This is equivalent to hashing
	 * the MPIDR_EL1 through shifting and ORing. It is a collision free
	 * hash though not minimal since some levels might contain a number
	 * of CPUs that is not an exact power of 2 and their bit
	 * representation might contain holes, eg MPIDR_EL1[7:0] = {0x2, 0x80}.
	 */
	mpidr_hash.shift_aff[0] = MPIDR_LEVEL_SHIFT(0) + fs[0];
	mpidr_hash.shift_aff[1] = MPIDR_LEVEL_SHIFT(1) + fs[1] - bits[0];
	mpidr_hash.shift_aff[2] = MPIDR_LEVEL_SHIFT(2) + fs[2] -
						(bits[1] + bits[0]);
	mpidr_hash.shift_aff[3] = MPIDR_LEVEL_SHIFT(3) +
				  fs[3] - (bits[2] + bits[1] + bits[0]);
	mpidr_hash.mask = mask;
	mpidr_hash.bits = bits[3] + bits[2] + bits[1] + bits[0];
	pr_debug("MPIDR hash: aff0[%u] aff1[%u] aff2[%u] aff3[%u] mask[%#llx] bits[%u]\n",
		mpidr_hash.shift_aff[0],
		mpidr_hash.shift_aff[1],
		mpidr_hash.shift_aff[2],
		mpidr_hash.shift_aff[3],
		mpidr_hash.mask,
		mpidr_hash.bits);
	/*
	 * 4x is an arbitrary value used to warn on a hash table much bigger
	 * than expected on most systems.
	 */
	if (mpidr_hash_size() > 4 * num_possible_cpus())
		pr_warn("Large number of MPIDR hash buckets detected\n");
}

static void __init setup_machine_fdt(phys_addr_t dt_phys)
{
	void *dt_virt = fixmap_remap_fdt(dt_phys);
	const char *name;

	if (!dt_virt || !early_init_dt_scan(dt_virt)) {
		pr_crit("\n"
			"Error: invalid device tree blob at physical address %pa (virtual address 0x%p)\n"
			"The dtb must be 8-byte aligned and must not exceed 2 MB in size\n"
			"\nPlease check your bootloader.",
			&dt_phys, dt_virt);

		while (true)
			cpu_relax();
	}

	name = of_flat_dt_get_machine_name();
	if (!name)
		return;

	pr_info("Machine model: %s\n", name);
	dump_stack_set_arch_desc("%s (DT)", name);
}

static void __init request_standard_resources(void)
{
	struct memblock_region *region;
	struct resource *res;
	unsigned long i = 0;

	kernel_code.start   = __pa_symbol(_text);
	kernel_code.end     = __pa_symbol(__init_begin - 1);
	kernel_data.start   = __pa_symbol(_sdata);
	kernel_data.end     = __pa_symbol(_end - 1);

	num_standard_resources = memblock.memory.cnt;
	standard_resources = memblock_alloc_low(num_standard_resources *
					        sizeof(*standard_resources),
					        0);

	for_each_memblock(memory, region) {
		res = &standard_resources[i++];
		if (memblock_is_nomap(region)) {
			res->name  = "reserved";
			res->flags = IORESOURCE_MEM;
		} else {
			res->name  = "System RAM";
			res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		}
		res->start = __pfn_to_phys(memblock_region_memory_base_pfn(region));
		res->end = __pfn_to_phys(memblock_region_memory_end_pfn(region)) - 1;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
#ifdef CONFIG_KEXEC_CORE
		/* Userspace will find "Crash kernel" region in /proc/iomem. */
		if (crashk_res.end && crashk_res.start >= res->start &&
		    crashk_res.end <= res->end)
			request_resource(res, &crashk_res);
#endif
	}
}

static int __init reserve_memblock_reserved_regions(void)
{
	u64 i, j;

	for (i = 0; i < num_standard_resources; ++i) {
		struct resource *mem = &standard_resources[i];
		phys_addr_t r_start, r_end, mem_size = resource_size(mem);

		if (!memblock_is_region_reserved(mem->start, mem_size))
			continue;

		for_each_reserved_mem_region(j, &r_start, &r_end) {
			resource_size_t start, end;

			start = max(PFN_PHYS(PFN_DOWN(r_start)), mem->start);
			end = min(PFN_PHYS(PFN_UP(r_end)) - 1, mem->end);

			if (start > mem->end || end < mem->start)
				continue;

			reserve_region_with_split(mem, start, end, "reserved");
		}
	}

	return 0;
}
arch_initcall(reserve_memblock_reserved_regions);

u64 __cpu_logical_map[NR_CPUS] = { [0 ... NR_CPUS-1] = INVALID_HWID };

void __init setup_arch(char **cmdline_p)
{
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk	   = (unsigned long) _end;

	*cmdline_p = boot_command_line;

	early_fixmap_init();
	early_ioremap_init();

	setup_machine_fdt(__fdt_pointer);

	parse_early_param();

	/*
	 * Unmask asynchronous aborts and fiq after bringing up possible
	 * earlycon. (Report possible System Errors once we can report this
	 * occurred).
	 */
	local_daif_restore(DAIF_PROCCTX_NOIRQ);

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_uninstall_idmap();

	xen_early_init();
	efi_init();
	arm64_memblock_init();

	paging_init();

	acpi_table_upgrade();

	/* Parse the ACPI tables for possible boot-time configuration */
	acpi_boot_table_init();

	if (acpi_disabled)
		unflatten_device_tree();

	bootmem_init();

	kasan_init();

	request_standard_resources();

	early_ioremap_reset();

	if (acpi_disabled)
		psci_dt_init();
	else
		psci_acpi_init();

	cpu_read_bootcpu_ops();
	smp_init_cpus();
	smp_build_mpidr_hash();

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	/*
	 * Make sure init_thread_info.ttbr0 always generates translation
	 * faults in case uaccess_enable() is inadvertently called by the init
	 * thread.
	 */
	init_task.thread_info.ttbr0 = __pa_symbol(empty_zero_page);
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif
	if (boot_args[1] || boot_args[2] || boot_args[3]) {
		pr_err("WARNING: x1-x3 nonzero in violation of boot protocol:\n"
			"\tx1: %016llx\n\tx2: %016llx\n\tx3: %016llx\n"
			"This indicates a broken bootloader or old kernel\n",
			boot_args[1], boot_args[2], boot_args[3]);
	}
}

static int __init topology_init(void)
{
	int i;

	for_each_online_node(i)
		register_one_node(i);

	for_each_possible_cpu(i) {
		struct cpu *cpu = &per_cpu(cpu_data.cpu, i);
		cpu->hotpluggable = 1;
		register_cpu(cpu, i);
	}

	return 0;
}
subsys_initcall(topology_init);

/*
 * Dump out kernel offset information on panic.
 */
static int dump_kernel_offset(struct notifier_block *self, unsigned long v,
			      void *p)
{
	const unsigned long offset = kaslr_offset();

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && offset > 0) {
		pr_emerg("Kernel Offset: 0x%lx from 0x%lx\n",
			 offset, KIMAGE_VADDR);
	} else {
		pr_emerg("Kernel Offset: disabled\n");
	}
	return 0;
}

static struct notifier_block kernel_offset_notifier = {
	.notifier_call = dump_kernel_offset
};

static int __init register_kernel_offset_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &kernel_offset_notifier);
	return 0;
}
__initcall(register_kernel_offset_dumper);
