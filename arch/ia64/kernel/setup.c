// SPDX-License-Identifier: GPL-2.0
/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2001, 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, 2004 Intel Corp
 * 	Rohit Seth <rohit.seth@intel.com>
 * 	Suresh Siddha <suresh.b.siddha@intel.com>
 * 	Gordon Jin <gordon.jin@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * 12/26/04 S.Siddha, G.Jin, R.Seth
 *			Add multi-threading and multi-core detection
 * 11/12/01 D.Mosberger Convert get_cpuinfo() to seq_file based show_cpuinfo().
 * 04/04/00 D.Mosberger renamed cpu_initialized to cpu_online_map
 * 03/31/00 R.Seth	cpu_initialized and current->processor fixes
 * 02/04/00 D.Mosberger	some more get_cpuinfo fixes...
 * 02/01/00 R.Seth	fixed get_cpuinfo for SMP
 * 01/07/99 S.Eranian	added the support for command line argument
 * 06/24/99 W.Drummond	added boot_cpu_data.
 * 05/28/05 Z. Menyhart	Dynamic stride size for "flush_icache_range()"
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pgtable.h>

#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/reboot.h>
#include <linux/sched/mm.h>
#include <linux/sched/clock.h>
#include <linux/sched/task_stack.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/screen_info.h>
#include <linux/dmi.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/efi.h>
#include <linux/initrd.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>

#include <asm/mca.h>
#include <asm/meminit.h>
#include <asm/page.h>
#include <asm/patch.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>
#include <asm/unistd.h>
#include <asm/uv/uv.h>
#include <asm/xtp.h>

#if defined(CONFIG_SMP) && (IA64_CPU_SIZE > PAGE_SIZE)
# error "struct cpuinfo_ia64 too big!"
#endif

char ia64_platform_name[64];

#ifdef CONFIG_SMP
unsigned long __per_cpu_offset[NR_CPUS];
EXPORT_SYMBOL(__per_cpu_offset);
#endif

DEFINE_PER_CPU(struct cpuinfo_ia64, ia64_cpu_info);
EXPORT_SYMBOL(ia64_cpu_info);
DEFINE_PER_CPU(unsigned long, local_per_cpu_offset);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(local_per_cpu_offset);
#endif
unsigned long ia64_cycles_per_usec;
struct ia64_boot_param *ia64_boot_param;
struct screen_info screen_info;
unsigned long vga_console_iobase;
unsigned long vga_console_membase;

static struct resource data_resource = {
	.name	= "Kernel data",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.flags	= IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

unsigned long ia64_max_cacheline_size;

unsigned long ia64_iobase;	/* virtual address for I/O accesses */
EXPORT_SYMBOL(ia64_iobase);
struct io_space io_space[MAX_IO_SPACES];
EXPORT_SYMBOL(io_space);
unsigned int num_io_spaces;

/*
 * "flush_icache_range()" needs to know what processor dependent stride size to use
 * when it makes i-cache(s) coherent with d-caches.
 */
#define	I_CACHE_STRIDE_SHIFT	5	/* Safest way to go: 32 bytes by 32 bytes */
unsigned long ia64_i_cache_stride_shift = ~0;
/*
 * "clflush_cache_range()" needs to know what processor dependent stride size to
 * use when it flushes cache lines including both d-cache and i-cache.
 */
/* Safest way to go: 32 bytes by 32 bytes */
#define	CACHE_STRIDE_SHIFT	5
unsigned long ia64_cache_stride_shift = ~0;

/*
 * We use a special marker for the end of memory and it uses the extra (+1) slot
 */
struct rsvd_region rsvd_region[IA64_MAX_RSVD_REGIONS + 1] __initdata;
static int num_rsvd_regions __initdata;


/*
 * Filter incoming memory segments based on the primitive map created from the boot
 * parameters. Segments contained in the map are removed from the memory ranges. A
 * caller-specified function is called with the memory ranges that remain after filtering.
 * This routine does not assume the incoming segments are sorted.
 */
int __init
filter_rsvd_memory (u64 start, u64 end, void *arg)
{
	u64 range_start, range_end, prev_start;
	void (*func)(unsigned long, unsigned long, int);
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		printk(KERN_WARNING "warning: skipping physical page 0\n");
		start += PAGE_SIZE;
		if (start >= end) return 0;
	}
#endif
	/*
	 * lowest possible address(walker uses virtual)
	 */
	prev_start = PAGE_OFFSET;
	func = arg;

	for (i = 0; i < num_rsvd_regions; ++i) {
		range_start = max(start, prev_start);
		range_end   = min(end, rsvd_region[i].start);

		if (range_start < range_end)
			call_pernode_memory(__pa(range_start), range_end - range_start, func);

		/* nothing more available in this segment */
		if (range_end == end) return 0;

		prev_start = rsvd_region[i].end;
	}
	/* end of memory marker allows full processing inside loop body */
	return 0;
}

/*
 * Similar to "filter_rsvd_memory()", but the reserved memory ranges
 * are not filtered out.
 */
int __init
filter_memory(u64 start, u64 end, void *arg)
{
	void (*func)(unsigned long, unsigned long, int);

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		printk(KERN_WARNING "warning: skipping physical page 0\n");
		start += PAGE_SIZE;
		if (start >= end)
			return 0;
	}
#endif
	func = arg;
	if (start < end)
		call_pernode_memory(__pa(start), end - start, func);
	return 0;
}

static void __init
sort_regions (struct rsvd_region *rsvd_region, int max)
{
	int j;

	/* simple bubble sorting */
	while (max--) {
		for (j = 0; j < max; ++j) {
			if (rsvd_region[j].start > rsvd_region[j+1].start) {
				swap(rsvd_region[j], rsvd_region[j + 1]);
			}
		}
	}
}

/* merge overlaps */
static int __init
merge_regions (struct rsvd_region *rsvd_region, int max)
{
	int i;
	for (i = 1; i < max; ++i) {
		if (rsvd_region[i].start >= rsvd_region[i-1].end)
			continue;
		if (rsvd_region[i].end > rsvd_region[i-1].end)
			rsvd_region[i-1].end = rsvd_region[i].end;
		--max;
		memmove(&rsvd_region[i], &rsvd_region[i+1],
			(max - i) * sizeof(struct rsvd_region));
	}
	return max;
}

/*
 * Request address space for all standard resources
 */
static int __init register_memory(void)
{
	code_resource.start = ia64_tpa(_text);
	code_resource.end   = ia64_tpa(_etext) - 1;
	data_resource.start = ia64_tpa(_etext);
	data_resource.end   = ia64_tpa(_edata) - 1;
	bss_resource.start  = ia64_tpa(__bss_start);
	bss_resource.end    = ia64_tpa(_end) - 1;
	efi_initialize_iomem_resources(&code_resource, &data_resource,
			&bss_resource);

	return 0;
}

__initcall(register_memory);


#ifdef CONFIG_KEXEC

/*
 * This function checks if the reserved crashkernel is allowed on the specific
 * IA64 machine flavour. Machines without an IO TLB use swiotlb and require
 * some memory below 4 GB (i.e. in 32 bit area), see the implementation of
 * kernel/dma/swiotlb.c. The hpzx1 architecture has an IO TLB but cannot use that
 * in kdump case. See the comment in sba_init() in sba_iommu.c.
 *
 * So, the only machvec that really supports loading the kdump kernel
 * over 4 GB is "uv".
 */
static int __init check_crashkernel_memory(unsigned long pbase, size_t size)
{
	if (is_uv_system())
		return 1;
	else
		return pbase < (1UL << 32);
}

static void __init setup_crashkernel(unsigned long total, int *n)
{
	unsigned long long base = 0, size = 0;
	int ret;

	ret = parse_crashkernel(boot_command_line, total,
			&size, &base);
	if (ret == 0 && size > 0) {
		if (!base) {
			sort_regions(rsvd_region, *n);
			*n = merge_regions(rsvd_region, *n);
			base = kdump_find_rsvd_region(size,
					rsvd_region, *n);
		}

		if (!check_crashkernel_memory(base, size)) {
			pr_warn("crashkernel: There would be kdump memory "
				"at %ld GB but this is unusable because it "
				"must\nbe below 4 GB. Change the memory "
				"configuration of the machine.\n",
				(unsigned long)(base >> 30));
			return;
		}

		if (base != ~0UL) {
			printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
					"for crashkernel (System RAM: %ldMB)\n",
					(unsigned long)(size >> 20),
					(unsigned long)(base >> 20),
					(unsigned long)(total >> 20));
			rsvd_region[*n].start =
				(unsigned long)__va(base);
			rsvd_region[*n].end =
				(unsigned long)__va(base + size);
			(*n)++;
			crashk_res.start = base;
			crashk_res.end = base + size - 1;
		}
	}
	efi_memmap_res.start = ia64_boot_param->efi_memmap;
	efi_memmap_res.end = efi_memmap_res.start +
		ia64_boot_param->efi_memmap_size;
	boot_param_res.start = __pa(ia64_boot_param);
	boot_param_res.end = boot_param_res.start +
		sizeof(*ia64_boot_param);
}
#else
static inline void __init setup_crashkernel(unsigned long total, int *n)
{}
#endif

#ifdef CONFIG_CRASH_DUMP
static int __init reserve_elfcorehdr(u64 *start, u64 *end)
{
	u64 length;

	/* We get the address using the kernel command line,
	 * but the size is extracted from the EFI tables.
	 * Both address and size are required for reservation
	 * to work properly.
	 */

	if (!is_vmcore_usable())
		return -EINVAL;

	if ((length = vmcore_find_descriptor_size(elfcorehdr_addr)) == 0) {
		vmcore_unusable();
		return -EINVAL;
	}

	*start = (unsigned long)__va(elfcorehdr_addr);
	*end = *start + length;
	return 0;
}
#endif /* CONFIG_CRASH_DUMP */

/**
 * reserve_memory - setup reserved memory areas
 *
 * Setup the reserved memory areas set aside for the boot parameters,
 * initrd, etc.  There are currently %IA64_MAX_RSVD_REGIONS defined,
 * see arch/ia64/include/asm/meminit.h if you need to define more.
 */
void __init
reserve_memory (void)
{
	int n = 0;
	unsigned long total_memory;

	/*
	 * none of the entries in this table overlap
	 */
	rsvd_region[n].start = (unsigned long) ia64_boot_param;
	rsvd_region[n].end   = rsvd_region[n].start + sizeof(*ia64_boot_param);
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->efi_memmap);
	rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->efi_memmap_size;
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->command_line);
	rsvd_region[n].end   = (rsvd_region[n].start
				+ strlen(__va(ia64_boot_param->command_line)) + 1);
	n++;

	rsvd_region[n].start = (unsigned long) ia64_imva((void *)KERNEL_START);
	rsvd_region[n].end   = (unsigned long) ia64_imva(_end);
	n++;

#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		rsvd_region[n].start = (unsigned long)__va(ia64_boot_param->initrd_start);
		rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->initrd_size;
		n++;
	}
#endif

#ifdef CONFIG_CRASH_DUMP
	if (reserve_elfcorehdr(&rsvd_region[n].start,
			       &rsvd_region[n].end) == 0)
		n++;
#endif

	total_memory = efi_memmap_init(&rsvd_region[n].start, &rsvd_region[n].end);
	n++;

	setup_crashkernel(total_memory, &n);

	/* end of memory marker */
	rsvd_region[n].start = ~0UL;
	rsvd_region[n].end   = ~0UL;
	n++;

	num_rsvd_regions = n;
	BUG_ON(IA64_MAX_RSVD_REGIONS + 1 < n);

	sort_regions(rsvd_region, num_rsvd_regions);
	num_rsvd_regions = merge_regions(rsvd_region, num_rsvd_regions);

	/* reserve all regions except the end of memory marker with memblock */
	for (n = 0; n < num_rsvd_regions - 1; n++) {
		struct rsvd_region *region = &rsvd_region[n];
		phys_addr_t addr = __pa(region->start);
		phys_addr_t size = region->end - region->start;

		memblock_reserve(addr, size);
	}
}

/**
 * find_initrd - get initrd parameters from the boot parameter structure
 *
 * Grab the initrd start and end from the boot parameter struct given us by
 * the boot loader.
 */
void __init
find_initrd (void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		initrd_start = (unsigned long)__va(ia64_boot_param->initrd_start);
		initrd_end   = initrd_start+ia64_boot_param->initrd_size;

		printk(KERN_INFO "Initial ramdisk at: 0x%lx (%llu bytes)\n",
		       initrd_start, ia64_boot_param->initrd_size);
	}
#endif
}

static void __init
io_port_init (void)
{
	unsigned long phys_iobase;

	/*
	 * Set `iobase' based on the EFI memory map or, failing that, the
	 * value firmware left in ar.k0.
	 *
	 * Note that in ia32 mode, IN/OUT instructions use ar.k0 to compute
	 * the port's virtual address, so ia32_load_state() loads it with a
	 * user virtual address.  But in ia64 mode, glibc uses the
	 * *physical* address in ar.k0 to mmap the appropriate area from
	 * /dev/mem, and the inX()/outX() interfaces use MMIO.  In both
	 * cases, user-mode can only use the legacy 0-64K I/O port space.
	 *
	 * ar.k0 is not involved in kernel I/O port accesses, which can use
	 * any of the I/O port spaces and are done via MMIO using the
	 * virtual mmio_base from the appropriate io_space[].
	 */
	phys_iobase = efi_get_iobase();
	if (!phys_iobase) {
		phys_iobase = ia64_get_kr(IA64_KR_IO_BASE);
		printk(KERN_INFO "No I/O port range found in EFI memory map, "
			"falling back to AR.KR0 (0x%lx)\n", phys_iobase);
	}
	ia64_iobase = (unsigned long) ioremap(phys_iobase, 0);
	ia64_set_kr(IA64_KR_IO_BASE, __pa(ia64_iobase));

	/* setup legacy IO port space */
	io_space[0].mmio_base = ia64_iobase;
	io_space[0].sparse = 1;
	num_io_spaces = 1;
}

/**
 * early_console_setup - setup debugging console
 *
 * Consoles started here require little enough setup that we can start using
 * them very early in the boot process, either right after the machine
 * vector initialization, or even before if the drivers can detect their hw.
 *
 * Returns non-zero if a console couldn't be setup.
 */
static inline int __init
early_console_setup (char *cmdline)
{
#ifdef CONFIG_EFI_PCDP
	if (!efi_setup_pcdp_console(cmdline))
		return 0;
#endif
	return -1;
}

static void __init
screen_info_setup(void)
{
	unsigned int orig_x, orig_y, num_cols, num_rows, font_height;

	memset(&screen_info, 0, sizeof(screen_info));

	if (!ia64_boot_param->console_info.num_rows ||
	    !ia64_boot_param->console_info.num_cols) {
		printk(KERN_WARNING "invalid screen-info, guessing 80x25\n");
		orig_x = 0;
		orig_y = 0;
		num_cols = 80;
		num_rows = 25;
		font_height = 16;
	} else {
		orig_x = ia64_boot_param->console_info.orig_x;
		orig_y = ia64_boot_param->console_info.orig_y;
		num_cols = ia64_boot_param->console_info.num_cols;
		num_rows = ia64_boot_param->console_info.num_rows;
		font_height = 400 / num_rows;
	}

	screen_info.orig_x = orig_x;
	screen_info.orig_y = orig_y;
	screen_info.orig_video_cols  = num_cols;
	screen_info.orig_video_lines = num_rows;
	screen_info.orig_video_points = font_height;
	screen_info.orig_video_mode = 3;	/* XXX fake */
	screen_info.orig_video_isVGA = 1;	/* XXX fake */
	screen_info.orig_video_ega_bx = 3;	/* XXX fake */
}

static inline void
mark_bsp_online (void)
{
#ifdef CONFIG_SMP
	/* If we register an early console, allow CPU 0 to printk */
	set_cpu_online(smp_processor_id(), true);
#endif
}

static __initdata int nomca;
static __init int setup_nomca(char *s)
{
	nomca = 1;
	return 0;
}
early_param("nomca", setup_nomca);

void __init
setup_arch (char **cmdline_p)
{
	unw_init();

	ia64_patch_vtop((u64) __start___vtop_patchlist, (u64) __end___vtop_patchlist);

	*cmdline_p = __va(ia64_boot_param->command_line);
	strlcpy(boot_command_line, *cmdline_p, COMMAND_LINE_SIZE);

	efi_init();
	io_port_init();

	uv_probe_system_type();
	parse_early_param();

	if (early_console_setup(*cmdline_p) == 0)
		mark_bsp_online();

	/* Initialize the ACPI boot-time table parser */
	acpi_table_init();
	early_acpi_boot_init();
#ifdef CONFIG_ACPI_NUMA
	acpi_numa_init();
	acpi_numa_fixup();
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	prefill_possible_map();
#endif
	per_cpu_scan_finalize((cpumask_weight(&early_cpu_possible_map) == 0 ?
		32 : cpumask_weight(&early_cpu_possible_map)),
		additional_cpus > 0 ? additional_cpus : 0);
#endif /* CONFIG_ACPI_NUMA */

#ifdef CONFIG_SMP
	smp_build_cpu_map();
#endif
	find_memory();

	/* process SAL system table: */
	ia64_sal_init(__va(sal_systab_phys));

#ifdef CONFIG_ITANIUM
	ia64_patch_rse((u64) __start___rse_patchlist, (u64) __end___rse_patchlist);
#else
	{
		unsigned long num_phys_stacked;

		if (ia64_pal_rse_info(&num_phys_stacked, 0) == 0 && num_phys_stacked > 96)
			ia64_patch_rse((u64) __start___rse_patchlist, (u64) __end___rse_patchlist);
	}
#endif

#ifdef CONFIG_SMP
	cpu_physical_id(0) = hard_smp_processor_id();
#endif

	cpu_init();	/* initialize the bootstrap CPU */
	mmu_context_init();	/* initialize context_id bitmap */

#ifdef CONFIG_VT
	if (!conswitchp) {
# if defined(CONFIG_VGA_CONSOLE)
		/*
		 * Non-legacy systems may route legacy VGA MMIO range to system
		 * memory.  vga_con probes the MMIO hole, so memory looks like
		 * a VGA device to it.  The EFI memory map can tell us if it's
		 * memory so we can avoid this problem.
		 */
		if (efi_mem_type(0xA0000) != EFI_CONVENTIONAL_MEMORY)
			conswitchp = &vga_con;
# endif
	}
#endif

	/* enable IA-64 Machine Check Abort Handling unless disabled */
	if (!nomca)
		ia64_mca_init();

	/*
	 * Default to /dev/sda2.  This assumes that the EFI partition
	 * is physical disk 1 partition 1 and the Linux root disk is
	 * physical disk 1 partition 2.
	 */
	ROOT_DEV = Root_SDA2;		/* default to second partition on first drive */

	if (is_uv_system())
		uv_setup(cmdline_p);
#ifdef CONFIG_SMP
	else
		init_smp_config();
#endif

	screen_info_setup();
	paging_init();

	clear_sched_clock_stable();
}

/*
 * Display cpu info for all CPUs.
 */
static int
show_cpuinfo (struct seq_file *m, void *v)
{
#ifdef CONFIG_SMP
#	define lpj	c->loops_per_jiffy
#	define cpunum	c->cpu
#else
#	define lpj	loops_per_jiffy
#	define cpunum	0
#endif
	static struct {
		unsigned long mask;
		const char *feature_name;
	} feature_bits[] = {
		{ 1UL << 0, "branchlong" },
		{ 1UL << 1, "spontaneous deferral"},
		{ 1UL << 2, "16-byte atomic ops" }
	};
	char features[128], *cp, *sep;
	struct cpuinfo_ia64 *c = v;
	unsigned long mask;
	unsigned long proc_freq;
	int i, size;

	mask = c->features;

	/* build the feature string: */
	memcpy(features, "standard", 9);
	cp = features;
	size = sizeof(features);
	sep = "";
	for (i = 0; i < ARRAY_SIZE(feature_bits) && size > 1; ++i) {
		if (mask & feature_bits[i].mask) {
			cp += snprintf(cp, size, "%s%s", sep,
				       feature_bits[i].feature_name),
			sep = ", ";
			mask &= ~feature_bits[i].mask;
			size = sizeof(features) - (cp - features);
		}
	}
	if (mask && size > 1) {
		/* print unknown features as a hex value */
		snprintf(cp, size, "%s0x%lx", sep, mask);
	}

	proc_freq = cpufreq_quick_get(cpunum);
	if (!proc_freq)
		proc_freq = c->proc_freq / 1000;

	seq_printf(m,
		   "processor  : %d\n"
		   "vendor     : %s\n"
		   "arch       : IA-64\n"
		   "family     : %u\n"
		   "model      : %u\n"
		   "model name : %s\n"
		   "revision   : %u\n"
		   "archrev    : %u\n"
		   "features   : %s\n"
		   "cpu number : %lu\n"
		   "cpu regs   : %u\n"
		   "cpu MHz    : %lu.%03lu\n"
		   "itc MHz    : %lu.%06lu\n"
		   "BogoMIPS   : %lu.%02lu\n",
		   cpunum, c->vendor, c->family, c->model,
		   c->model_name, c->revision, c->archrev,
		   features, c->ppn, c->number,
		   proc_freq / 1000, proc_freq % 1000,
		   c->itc_freq / 1000000, c->itc_freq % 1000000,
		   lpj*HZ/500000, (lpj*HZ/5000) % 100);
#ifdef CONFIG_SMP
	seq_printf(m, "siblings   : %u\n",
		   cpumask_weight(&cpu_core_map[cpunum]));
	if (c->socket_id != -1)
		seq_printf(m, "physical id: %u\n", c->socket_id);
	if (c->threads_per_core > 1 || c->cores_per_socket > 1)
		seq_printf(m,
			   "core id    : %u\n"
			   "thread id  : %u\n",
			   c->core_id, c->thread_id);
#endif
	seq_printf(m,"\n");

	return 0;
}

static void *
c_start (struct seq_file *m, loff_t *pos)
{
#ifdef CONFIG_SMP
	while (*pos < nr_cpu_ids && !cpu_online(*pos))
		++*pos;
#endif
	return *pos < nr_cpu_ids ? cpu_data(*pos) : NULL;
}

static void *
c_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void
c_stop (struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start =	c_start,
	.next =		c_next,
	.stop =		c_stop,
	.show =		show_cpuinfo
};

#define MAX_BRANDS	8
static char brandname[MAX_BRANDS][128];

static char *
get_model_name(__u8 family, __u8 model)
{
	static int overflow;
	char brand[128];
	int i;

	memcpy(brand, "Unknown", 8);
	if (ia64_pal_get_brand_info(brand)) {
		if (family == 0x7)
			memcpy(brand, "Merced", 7);
		else if (family == 0x1f) switch (model) {
			case 0: memcpy(brand, "McKinley", 9); break;
			case 1: memcpy(brand, "Madison", 8); break;
			case 2: memcpy(brand, "Madison up to 9M cache", 23); break;
		}
	}
	for (i = 0; i < MAX_BRANDS; i++)
		if (strcmp(brandname[i], brand) == 0)
			return brandname[i];
	for (i = 0; i < MAX_BRANDS; i++)
		if (brandname[i][0] == '\0')
			return strcpy(brandname[i], brand);
	if (overflow++ == 0)
		printk(KERN_ERR
		       "%s: Table overflow. Some processor model information will be missing\n",
		       __func__);
	return "Unknown";
}

static void
identify_cpu (struct cpuinfo_ia64 *c)
{
	union {
		unsigned long bits[5];
		struct {
			/* id 0 & 1: */
			char vendor[16];

			/* id 2 */
			u64 ppn;		/* processor serial number */

			/* id 3: */
			unsigned number		:  8;
			unsigned revision	:  8;
			unsigned model		:  8;
			unsigned family		:  8;
			unsigned archrev	:  8;
			unsigned reserved	: 24;

			/* id 4: */
			u64 features;
		} field;
	} cpuid;
	pal_vm_info_1_u_t vm1;
	pal_vm_info_2_u_t vm2;
	pal_status_t status;
	unsigned long impl_va_msb = 50, phys_addr_size = 44;	/* Itanium defaults */
	int i;
	for (i = 0; i < 5; ++i)
		cpuid.bits[i] = ia64_get_cpuid(i);

	memcpy(c->vendor, cpuid.field.vendor, 16);
#ifdef CONFIG_SMP
	c->cpu = smp_processor_id();

	/* below default values will be overwritten  by identify_siblings() 
	 * for Multi-Threading/Multi-Core capable CPUs
	 */
	c->threads_per_core = c->cores_per_socket = c->num_log = 1;
	c->socket_id = -1;

	identify_siblings(c);

	if (c->threads_per_core > smp_num_siblings)
		smp_num_siblings = c->threads_per_core;
#endif
	c->ppn = cpuid.field.ppn;
	c->number = cpuid.field.number;
	c->revision = cpuid.field.revision;
	c->model = cpuid.field.model;
	c->family = cpuid.field.family;
	c->archrev = cpuid.field.archrev;
	c->features = cpuid.field.features;
	c->model_name = get_model_name(c->family, c->model);

	status = ia64_pal_vm_summary(&vm1, &vm2);
	if (status == PAL_STATUS_SUCCESS) {
		impl_va_msb = vm2.pal_vm_info_2_s.impl_va_msb;
		phys_addr_size = vm1.pal_vm_info_1_s.phys_add_size;
	}
	c->unimpl_va_mask = ~((7L<<61) | ((1L << (impl_va_msb + 1)) - 1));
	c->unimpl_pa_mask = ~((1L<<63) | ((1L << phys_addr_size) - 1));
}

/*
 * Do the following calculations:
 *
 * 1. the max. cache line size.
 * 2. the minimum of the i-cache stride sizes for "flush_icache_range()".
 * 3. the minimum of the cache stride sizes for "clflush_cache_range()".
 */
static void
get_cache_info(void)
{
	unsigned long line_size, max = 1;
	unsigned long l, levels, unique_caches;
	pal_cache_config_info_t cci;
	long status;

        status = ia64_pal_cache_summary(&levels, &unique_caches);
        if (status != 0) {
                printk(KERN_ERR "%s: ia64_pal_cache_summary() failed (status=%ld)\n",
                       __func__, status);
                max = SMP_CACHE_BYTES;
		/* Safest setup for "flush_icache_range()" */
		ia64_i_cache_stride_shift = I_CACHE_STRIDE_SHIFT;
		/* Safest setup for "clflush_cache_range()" */
		ia64_cache_stride_shift = CACHE_STRIDE_SHIFT;
		goto out;
        }

	for (l = 0; l < levels; ++l) {
		/* cache_type (data_or_unified)=2 */
		status = ia64_pal_cache_config_info(l, 2, &cci);
		if (status != 0) {
			printk(KERN_ERR "%s: ia64_pal_cache_config_info"
				"(l=%lu, 2) failed (status=%ld)\n",
				__func__, l, status);
			max = SMP_CACHE_BYTES;
			/* The safest setup for "flush_icache_range()" */
			cci.pcci_stride = I_CACHE_STRIDE_SHIFT;
			/* The safest setup for "clflush_cache_range()" */
			ia64_cache_stride_shift = CACHE_STRIDE_SHIFT;
			cci.pcci_unified = 1;
		} else {
			if (cci.pcci_stride < ia64_cache_stride_shift)
				ia64_cache_stride_shift = cci.pcci_stride;

			line_size = 1 << cci.pcci_line_size;
			if (line_size > max)
				max = line_size;
		}

		if (!cci.pcci_unified) {
			/* cache_type (instruction)=1*/
			status = ia64_pal_cache_config_info(l, 1, &cci);
			if (status != 0) {
				printk(KERN_ERR "%s: ia64_pal_cache_config_info"
					"(l=%lu, 1) failed (status=%ld)\n",
					__func__, l, status);
				/* The safest setup for flush_icache_range() */
				cci.pcci_stride = I_CACHE_STRIDE_SHIFT;
			}
		}
		if (cci.pcci_stride < ia64_i_cache_stride_shift)
			ia64_i_cache_stride_shift = cci.pcci_stride;
	}
  out:
	if (max > ia64_max_cacheline_size)
		ia64_max_cacheline_size = max;
}

/*
 * cpu_init() initializes state that is per-CPU.  This function acts
 * as a 'CPU state barrier', nothing should get across.
 */
void
cpu_init (void)
{
	extern void ia64_mmu_init(void *);
	static unsigned long max_num_phys_stacked = IA64_NUM_PHYS_STACK_REG;
	unsigned long num_phys_stacked;
	pal_vm_info_2_u_t vmi;
	unsigned int max_ctx;
	struct cpuinfo_ia64 *cpu_info;
	void *cpu_data;

	cpu_data = per_cpu_init();
#ifdef CONFIG_SMP
	/*
	 * insert boot cpu into sibling and core mapes
	 * (must be done after per_cpu area is setup)
	 */
	if (smp_processor_id() == 0) {
		cpumask_set_cpu(0, &per_cpu(cpu_sibling_map, 0));
		cpumask_set_cpu(0, &cpu_core_map[0]);
	} else {
		/*
		 * Set ar.k3 so that assembly code in MCA handler can compute
		 * physical addresses of per cpu variables with a simple:
		 *   phys = ar.k3 + &per_cpu_var
		 * and the alt-dtlb-miss handler can set per-cpu mapping into
		 * the TLB when needed. head.S already did this for cpu0.
		 */
		ia64_set_kr(IA64_KR_PER_CPU_DATA,
			    ia64_tpa(cpu_data) - (long) __per_cpu_start);
	}
#endif

	get_cache_info();

	/*
	 * We can't pass "local_cpu_data" to identify_cpu() because we haven't called
	 * ia64_mmu_init() yet.  And we can't call ia64_mmu_init() first because it
	 * depends on the data returned by identify_cpu().  We break the dependency by
	 * accessing cpu_data() through the canonical per-CPU address.
	 */
	cpu_info = cpu_data + ((char *) &__ia64_per_cpu_var(ia64_cpu_info) - __per_cpu_start);
	identify_cpu(cpu_info);

#ifdef CONFIG_MCKINLEY
	{
#		define FEATURE_SET 16
		struct ia64_pal_retval iprv;

		if (cpu_info->family == 0x1f) {
			PAL_CALL_PHYS(iprv, PAL_PROC_GET_FEATURES, 0, FEATURE_SET, 0);
			if ((iprv.status == 0) && (iprv.v0 & 0x80) && (iprv.v2 & 0x80))
				PAL_CALL_PHYS(iprv, PAL_PROC_SET_FEATURES,
				              (iprv.v1 | 0x80), FEATURE_SET, 0);
		}
	}
#endif

	/* Clear the stack memory reserved for pt_regs: */
	memset(task_pt_regs(current), 0, sizeof(struct pt_regs));

	ia64_set_kr(IA64_KR_FPU_OWNER, 0);

	/*
	 * Initialize the page-table base register to a global
	 * directory with all zeroes.  This ensure that we can handle
	 * TLB-misses to user address-space even before we created the
	 * first user address-space.  This may happen, e.g., due to
	 * aggressive use of lfetch.fault.
	 */
	ia64_set_kr(IA64_KR_PT_BASE, __pa(ia64_imva(empty_zero_page)));

	/*
	 * Initialize default control register to defer speculative faults except
	 * for those arising from TLB misses, which are not deferred.  The
	 * kernel MUST NOT depend on a particular setting of these bits (in other words,
	 * the kernel must have recovery code for all speculative accesses).  Turn on
	 * dcr.lc as per recommendation by the architecture team.  Most IA-32 apps
	 * shouldn't be affected by this (moral: keep your ia32 locks aligned and you'll
	 * be fine).
	 */
	ia64_setreg(_IA64_REG_CR_DCR,  (  IA64_DCR_DP | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_DR
					| IA64_DCR_DA | IA64_DCR_DD | IA64_DCR_LC));
	mmgrab(&init_mm);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);

	ia64_mmu_init(ia64_imva(cpu_data));
	ia64_mca_cpu_init(ia64_imva(cpu_data));

	/* Clear ITC to eliminate sched_clock() overflows in human time.  */
	ia64_set_itc(0);

	/* disable all local interrupt sources: */
	ia64_set_itv(1 << 16);
	ia64_set_lrr0(1 << 16);
	ia64_set_lrr1(1 << 16);
	ia64_setreg(_IA64_REG_CR_PMV, 1 << 16);
	ia64_setreg(_IA64_REG_CR_CMCV, 1 << 16);

	/* clear TPR & XTP to enable all interrupt classes: */
	ia64_setreg(_IA64_REG_CR_TPR, 0);

	/* Clear any pending interrupts left by SAL/EFI */
	while (ia64_get_ivr() != IA64_SPURIOUS_INT_VECTOR)
		ia64_eoi();

#ifdef CONFIG_SMP
	normal_xtp();
#endif

	/* set ia64_ctx.max_rid to the maximum RID that is supported by all CPUs: */
	if (ia64_pal_vm_summary(NULL, &vmi) == 0) {
		max_ctx = (1U << (vmi.pal_vm_info_2_s.rid_size - 3)) - 1;
		setup_ptcg_sem(vmi.pal_vm_info_2_s.max_purges, NPTCG_FROM_PAL);
	} else {
		printk(KERN_WARNING "cpu_init: PAL VM summary failed, assuming 18 RID bits\n");
		max_ctx = (1U << 15) - 1;	/* use architected minimum */
	}
	while (max_ctx < ia64_ctx.max_ctx) {
		unsigned int old = ia64_ctx.max_ctx;
		if (cmpxchg(&ia64_ctx.max_ctx, old, max_ctx) == old)
			break;
	}

	if (ia64_pal_rse_info(&num_phys_stacked, NULL) != 0) {
		printk(KERN_WARNING "cpu_init: PAL RSE info failed; assuming 96 physical "
		       "stacked regs\n");
		num_phys_stacked = 96;
	}
	/* size of physical stacked register partition plus 8 bytes: */
	if (num_phys_stacked > max_num_phys_stacked) {
		ia64_patch_phys_stack_reg(num_phys_stacked*8 + 8);
		max_num_phys_stacked = num_phys_stacked;
	}
}

void __init
check_bugs (void)
{
	ia64_patch_mckinley_e9((unsigned long) __start___mckinley_e9_bundles,
			       (unsigned long) __end___mckinley_e9_bundles);
}

static int __init run_dmi_scan(void)
{
	dmi_setup();
	return 0;
}
core_initcall(run_dmi_scan);
