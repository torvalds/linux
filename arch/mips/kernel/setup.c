/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 Linus Torvalds
 * Copyright (C) 1995 Waldorf Electronics
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 01, 02, 03  Ralf Baechle
 * Copyright (C) 1996 Stoned Elipot
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002, 2007	 Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/screen_info.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/console.h>
#include <linux/pfn.h>
#include <linux/debugfs.h>
#include <linux/kexec.h>
#include <linux/sizes.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/bugs.h>
#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp-ops.h>
#include <asm/prom.h>

struct cpuinfo_mips cpu_data[NR_CPUS] __read_mostly;

EXPORT_SYMBOL(cpu_data);

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

/*
 * Despite it's name this variable is even if we don't have PCI
 */
unsigned int PCI_DMA_BUS_IS_PHYS;

EXPORT_SYMBOL(PCI_DMA_BUS_IS_PHYS);

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */
unsigned long mips_machtype __read_mostly = MACH_UNKNOWN;

EXPORT_SYMBOL(mips_machtype);

struct boot_mem_map boot_mem_map;

static char __initdata command_line[COMMAND_LINE_SIZE];
char __initdata arcs_cmdline[COMMAND_LINE_SIZE];

#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
const unsigned long mips_io_port_base = -1;
EXPORT_SYMBOL(mips_io_port_base);

static struct resource code_resource = { .name = "Kernel code", };
static struct resource data_resource = { .name = "Kernel data", };

static void *detect_magic __initdata = detect_memory_region;

void __init add_memory_region(phys_t start, phys_t size, long type)
{
	int x = boot_mem_map.nr_map;
	int i;

	/* Sanity check */
	if (start + size < start) {
		pr_warning("Trying to add an invalid memory region, skipped\n");
		return;
	}

	/*
	 * Try to merge with existing entry, if any.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct boot_mem_map_entry *entry = boot_mem_map.map + i;
		unsigned long top;

		if (entry->type != type)
			continue;

		if (start + size < entry->addr)
			continue;			/* no overlap */

		if (entry->addr + entry->size < start)
			continue;			/* no overlap */

		top = max(entry->addr + entry->size, start + size);
		entry->addr = min(entry->addr, start);
		entry->size = top - entry->addr;

		return;
	}

	if (boot_mem_map.nr_map == BOOT_MEM_MAP_MAX) {
		pr_err("Ooops! Too many entries in the memory map!\n");
		return;
	}

	boot_mem_map.map[x].addr = start;
	boot_mem_map.map[x].size = size;
	boot_mem_map.map[x].type = type;
	boot_mem_map.nr_map++;
}

void __init detect_memory_region(phys_t start, phys_t sz_min, phys_t sz_max)
{
	void *dm = &detect_magic;
	phys_t size;

	for (size = sz_min; size < sz_max; size <<= 1) {
		if (!memcmp(dm, dm + size, sizeof(detect_magic)))
			break;
	}

	pr_debug("Memory: %lluMB of RAM detected at 0x%llx (min: %lluMB, max: %lluMB)\n",
		((unsigned long long) size) / SZ_1M,
		(unsigned long long) start,
		((unsigned long long) sz_min) / SZ_1M,
		((unsigned long long) sz_max) / SZ_1M);

	add_memory_region(start, size, BOOT_MEM_RAM);
}

static void __init print_memory_map(void)
{
	int i;
	const int field = 2 * sizeof(unsigned long);

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		printk(KERN_INFO " memory: %0*Lx @ %0*Lx ",
		       field, (unsigned long long) boot_mem_map.map[i].size,
		       field, (unsigned long long) boot_mem_map.map[i].addr);

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			printk(KERN_CONT "(usable)\n");
			break;
		case BOOT_MEM_INIT_RAM:
			printk(KERN_CONT "(usable after init)\n");
			break;
		case BOOT_MEM_ROM_DATA:
			printk(KERN_CONT "(ROM data)\n");
			break;
		case BOOT_MEM_RESERVED:
			printk(KERN_CONT "(reserved)\n");
			break;
		default:
			printk(KERN_CONT "type %lu\n", boot_mem_map.map[i].type);
			break;
		}
	}
}

/*
 * Manage initrd
 */
#ifdef CONFIG_BLK_DEV_INITRD

static int __init rd_start_early(char *p)
{
	unsigned long start = memparse(p, &p);

#ifdef CONFIG_64BIT
	/* Guess if the sign extension was forgotten by bootloader */
	if (start < XKPHYS)
		start = (int)start;
#endif
	initrd_start = start;
	initrd_end += start;
	return 0;
}
early_param("rd_start", rd_start_early);

static int __init rd_size_early(char *p)
{
	initrd_end += memparse(p, &p);
	return 0;
}
early_param("rd_size", rd_size_early);

/* it returns the next free pfn after initrd */
static unsigned long __init init_initrd(void)
{
	unsigned long end;

	/*
	 * Board specific code or command line parser should have
	 * already set up initrd_start and initrd_end. In these cases
	 * perfom sanity checks and use them if all looks good.
	 */
	if (!initrd_start || initrd_end <= initrd_start)
		goto disable;

	if (initrd_start & ~PAGE_MASK) {
		pr_err("initrd start must be page aligned\n");
		goto disable;
	}
	if (initrd_start < PAGE_OFFSET) {
		pr_err("initrd start < PAGE_OFFSET\n");
		goto disable;
	}

	/*
	 * Sanitize initrd addresses. For example firmware
	 * can't guess if they need to pass them through
	 * 64-bits values if the kernel has been built in pure
	 * 32-bit. We need also to switch from KSEG0 to XKPHYS
	 * addresses now, so the code can now safely use __pa().
	 */
	end = __pa(initrd_end);
	initrd_end = (unsigned long)__va(end);
	initrd_start = (unsigned long)__va(__pa(initrd_start));

	ROOT_DEV = Root_RAM0;
	return PFN_UP(end);
disable:
	initrd_start = 0;
	initrd_end = 0;
	return 0;
}

static void __init finalize_initrd(void)
{
	unsigned long size = initrd_end - initrd_start;

	if (size == 0) {
		printk(KERN_INFO "Initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		printk(KERN_ERR "Initrd extends beyond end of memory");
		goto disable;
	}

	reserve_bootmem(__pa(initrd_start), size, BOOTMEM_DEFAULT);
	initrd_below_start_ok = 1;

	pr_info("Initial ramdisk at: 0x%lx (%lu bytes)\n",
		initrd_start, size);
	return;
disable:
	printk(KERN_CONT " - disabling initrd\n");
	initrd_start = 0;
	initrd_end = 0;
}

#else  /* !CONFIG_BLK_DEV_INITRD */

static unsigned long __init init_initrd(void)
{
	return 0;
}

#define finalize_initrd()	do {} while (0)

#endif

/*
 * Initialize the bootmem allocator. It also setup initrd related data
 * if needed.
 */
#if defined(CONFIG_SGI_IP27) || (defined(CONFIG_CPU_LOONGSON3) && defined(CONFIG_NUMA))

static void __init bootmem_init(void)
{
	init_initrd();
	finalize_initrd();
}

#else  /* !CONFIG_SGI_IP27 */

static void __init bootmem_init(void)
{
	unsigned long reserved_end;
	unsigned long mapstart = ~0UL;
	unsigned long bootmap_size;
	int i;

	/*
	 * Sanity check any INITRD first. We don't take it into account
	 * for bootmem setup initially, rely on the end-of-kernel-code
	 * as our memory range starting point. Once bootmem is inited we
	 * will reserve the area used for the initrd.
	 */
	init_initrd();
	reserved_end = (unsigned long) PFN_UP(__pa_symbol(&_end));

	/*
	 * max_low_pfn is not a number of pages. The number of pages
	 * of the system is given by 'max_low_pfn - min_low_pfn'.
	 */
	min_low_pfn = ~0UL;
	max_low_pfn = 0;

	/*
	 * Find the highest page frame number we have available.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
				+ boot_mem_map.map[i].size);

		if (end > max_low_pfn)
			max_low_pfn = end;
		if (start < min_low_pfn)
			min_low_pfn = start;
		if (end <= reserved_end)
			continue;
		if (start >= mapstart)
			continue;
		mapstart = max(reserved_end, start);
	}

	if (min_low_pfn >= max_low_pfn)
		panic("Incorrect memory mapping !!!");
	if (min_low_pfn > ARCH_PFN_OFFSET) {
		pr_info("Wasting %lu bytes for tracking %lu unused pages\n",
			(min_low_pfn - ARCH_PFN_OFFSET) * sizeof(struct page),
			min_low_pfn - ARCH_PFN_OFFSET);
	} else if (min_low_pfn < ARCH_PFN_OFFSET) {
		pr_info("%lu free pages won't be used\n",
			ARCH_PFN_OFFSET - min_low_pfn);
	}
	min_low_pfn = ARCH_PFN_OFFSET;

	/*
	 * Determine low and high memory ranges
	 */
	max_pfn = max_low_pfn;
	if (max_low_pfn > PFN_DOWN(HIGHMEM_START)) {
#ifdef CONFIG_HIGHMEM
		highstart_pfn = PFN_DOWN(HIGHMEM_START);
		highend_pfn = max_low_pfn;
#endif
		max_low_pfn = PFN_DOWN(HIGHMEM_START);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * mapstart should be after initrd_end
	 */
	if (initrd_end)
		mapstart = max(mapstart, (unsigned long)PFN_UP(__pa(initrd_end)));
#endif

	/*
	 * Initialize the boot-time allocator with low memory only.
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), mapstart,
					 min_low_pfn, max_low_pfn);


	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
				+ boot_mem_map.map[i].size);

		if (start <= min_low_pfn)
			start = min_low_pfn;
		if (start >= end)
			continue;

#ifndef CONFIG_HIGHMEM
		if (end > max_low_pfn)
			end = max_low_pfn;

		/*
		 * ... finally, is the area going away?
		 */
		if (end <= start)
			continue;
#endif

		memblock_add_node(PFN_PHYS(start), PFN_PHYS(end - start), 0);
	}

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end, size;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end   = PFN_DOWN(boot_mem_map.map[i].addr
				    + boot_mem_map.map[i].size);

		/*
		 * Reserve usable memory.
		 */
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			break;
		case BOOT_MEM_INIT_RAM:
			memory_present(0, start, end);
			continue;
		default:
			/* Not usable memory */
			continue;
		}

		/*
		 * We are rounding up the start address of usable memory
		 * and at the end of the usable range downwards.
		 */
		if (start >= max_low_pfn)
			continue;
		if (start < reserved_end)
			start = reserved_end;
		if (end > max_low_pfn)
			end = max_low_pfn;

		/*
		 * ... finally, is the area going away?
		 */
		if (end <= start)
			continue;
		size = end - start;

		/* Register lowmem ranges */
		free_bootmem(PFN_PHYS(start), size << PAGE_SHIFT);
		memory_present(0, start, end);
	}

	/*
	 * Reserve the bootmap memory.
	 */
	reserve_bootmem(PFN_PHYS(mapstart), bootmap_size, BOOTMEM_DEFAULT);

	/*
	 * Reserve initrd memory if needed.
	 */
	finalize_initrd();
}

#endif	/* CONFIG_SGI_IP27 */

/*
 * arch_mem_init - initialize memory management subsystem
 *
 *  o plat_mem_setup() detects the memory configuration and will record detected
 *    memory areas using add_memory_region.
 *
 * At this stage the memory configuration of the system is known to the
 * kernel but generic memory management system is still entirely uninitialized.
 *
 *  o bootmem_init()
 *  o sparse_init()
 *  o paging_init()
 *
 * At this stage the bootmem allocator is ready to use.
 *
 * NOTE: historically plat_mem_setup did the entire platform initialization.
 *	 This was rather impractical because it meant plat_mem_setup had to
 * get away without any kind of memory allocator.  To keep old code from
 * breaking plat_setup was just renamed to plat_setup and a second platform
 * initialization hook for anything else was introduced.
 */

static int usermem __initdata;

static int __init early_parse_mem(char *p)
{
	unsigned long start, size;

	/*
	 * If a user specifies memory size, we
	 * blow away any automatically generated
	 * size.
	 */
	if (usermem == 0) {
		boot_mem_map.nr_map = 0;
		usermem = 1;
	}
	start = 0;
	size = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);

	add_memory_region(start, size, BOOT_MEM_RAM);
	return 0;
}
early_param("mem", early_parse_mem);

#ifdef CONFIG_PROC_VMCORE
unsigned long setup_elfcorehdr, setup_elfcorehdr_size;
static int __init early_parse_elfcorehdr(char *p)
{
	int i;

	setup_elfcorehdr = memparse(p, &p);

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start = boot_mem_map.map[i].addr;
		unsigned long end = (boot_mem_map.map[i].addr +
				     boot_mem_map.map[i].size);
		if (setup_elfcorehdr >= start && setup_elfcorehdr < end) {
			/*
			 * Reserve from the elf core header to the end of
			 * the memory segment, that should all be kdump
			 * reserved memory.
			 */
			setup_elfcorehdr_size = end - setup_elfcorehdr;
			break;
		}
	}
	/*
	 * If we don't find it in the memory map, then we shouldn't
	 * have to worry about it, as the new kernel won't use it.
	 */
	return 0;
}
early_param("elfcorehdr", early_parse_elfcorehdr);
#endif

static void __init arch_mem_addpart(phys_t mem, phys_t end, int type)
{
	phys_t size;
	int i;

	size = end - mem;
	if (!size)
		return;

	/* Make sure it is in the boot_mem_map */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		if (mem >= boot_mem_map.map[i].addr &&
		    mem < (boot_mem_map.map[i].addr +
			   boot_mem_map.map[i].size))
			return;
	}
	add_memory_region(mem, size, type);
}

#ifdef CONFIG_KEXEC
static inline unsigned long long get_total_mem(void)
{
	unsigned long long total;

	total = max_pfn - min_low_pfn;
	return total << PAGE_SHIFT;
}

static void __init mips_parse_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = get_total_mem();
	ret = parse_crashkernel(boot_command_line, total_mem,
				&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0)
		return;

	crashk_res.start = crash_base;
	crashk_res.end	 = crash_base + crash_size - 1;
}

static void __init request_crashkernel(struct resource *res)
{
	int ret;

	ret = request_resource(res, &crashk_res);
	if (!ret)
		pr_info("Reserving %ldMB of memory at %ldMB for crashkernel\n",
			(unsigned long)((crashk_res.end -
					 crashk_res.start + 1) >> 20),
			(unsigned long)(crashk_res.start  >> 20));
}
#else /* !defined(CONFIG_KEXEC)		*/
static void __init mips_parse_crashkernel(void)
{
}

static void __init request_crashkernel(struct resource *res)
{
}
#endif /* !defined(CONFIG_KEXEC)  */

static void __init arch_mem_init(char **cmdline_p)
{
	extern void plat_mem_setup(void);

	/* call board setup routine */
	plat_mem_setup();

	/*
	 * Make sure all kernel memory is in the maps.  The "UP" and
	 * "DOWN" are opposite for initdata since if it crosses over
	 * into another memory section you don't want that to be
	 * freed when the initdata is freed.
	 */
	arch_mem_addpart(PFN_DOWN(__pa_symbol(&_text)) << PAGE_SHIFT,
			 PFN_UP(__pa_symbol(&_edata)) << PAGE_SHIFT,
			 BOOT_MEM_RAM);
	arch_mem_addpart(PFN_UP(__pa_symbol(&__init_begin)) << PAGE_SHIFT,
			 PFN_DOWN(__pa_symbol(&__init_end)) << PAGE_SHIFT,
			 BOOT_MEM_INIT_RAM);

	pr_info("Determined physical RAM map:\n");
	print_memory_map();

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(arcs_cmdline, builtin_cmdline, COMMAND_LINE_SIZE);
	}
	strlcpy(boot_command_line, arcs_cmdline, COMMAND_LINE_SIZE);
#endif
#else
	strlcpy(boot_command_line, arcs_cmdline, COMMAND_LINE_SIZE);
#endif
	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);

	*cmdline_p = command_line;

	parse_early_param();

	if (usermem) {
		pr_info("User-defined physical RAM map:\n");
		print_memory_map();
	}

	bootmem_init();
#ifdef CONFIG_PROC_VMCORE
	if (setup_elfcorehdr && setup_elfcorehdr_size) {
		printk(KERN_INFO "kdump reserved memory at %lx-%lx\n",
		       setup_elfcorehdr, setup_elfcorehdr_size);
		reserve_bootmem(setup_elfcorehdr, setup_elfcorehdr_size,
				BOOTMEM_DEFAULT);
	}
#endif

	mips_parse_crashkernel();
#ifdef CONFIG_KEXEC
	if (crashk_res.start != crashk_res.end)
		reserve_bootmem(crashk_res.start,
				crashk_res.end - crashk_res.start + 1,
				BOOTMEM_DEFAULT);
#endif
	device_tree_init();
	sparse_init();
	plat_swiotlb_setup();
	paging_init();
}

static void __init resource_init(void)
{
	int i;

	if (UNCAC_BASE != IO_BASE)
		return;

	code_resource.start = __pa_symbol(&_text);
	code_resource.end = __pa_symbol(&_etext) - 1;
	data_resource.start = __pa_symbol(&_etext);
	data_resource.end = __pa_symbol(&_edata) - 1;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct resource *res;
		unsigned long start, end;

		start = boot_mem_map.map[i].addr;
		end = boot_mem_map.map[i].addr + boot_mem_map.map[i].size - 1;
		if (start >= HIGHMEM_START)
			continue;
		if (end >= HIGHMEM_START)
			end = HIGHMEM_START - 1;

		res = alloc_bootmem(sizeof(struct resource));
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_INIT_RAM:
		case BOOT_MEM_ROM_DATA:
			res->name = "System RAM";
			break;
		case BOOT_MEM_RESERVED:
		default:
			res->name = "reserved";
		}

		res->start = start;
		res->end = end;

		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);

		/*
		 *  We don't know which RAM region contains kernel data,
		 *  so we try it repeatedly and let the resource manager
		 *  test it.
		 */
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
		request_crashkernel(res);
	}
}

#ifdef CONFIG_SMP
static void __init prefill_possible_map(void)
{
	int i, possible = num_possible_cpus();

	if (possible > nr_cpu_ids)
		possible = nr_cpu_ids;

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
	for (; i < NR_CPUS; i++)
		set_cpu_possible(i, false);

	nr_cpu_ids = possible;
}
#else
static inline void prefill_possible_map(void) {}
#endif

void __init setup_arch(char **cmdline_p)
{
	cpu_probe();
	prom_init();

#ifdef CONFIG_EARLY_PRINTK
	setup_early_printk();
#endif
	cpu_report();
	check_bugs_early();

#if defined(CONFIG_VT)
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	arch_mem_init(cmdline_p);

	resource_init();
	plat_smp_setup();
	prefill_possible_map();

	cpu_cache_init();
}

unsigned long kernelsp[NR_CPUS];
unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

#ifdef CONFIG_DEBUG_FS
struct dentry *mips_debugfs_dir;
static int __init debugfs_mips(void)
{
	struct dentry *d;

	d = debugfs_create_dir("mips", NULL);
	if (!d)
		return -ENOMEM;
	mips_debugfs_dir = d;
	return 0;
}
arch_initcall(debugfs_mips);
#endif
