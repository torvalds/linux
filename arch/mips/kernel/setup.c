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
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/console.h>
#include <linux/pfn.h>
#include <linux/debugfs.h>
#include <linux/kexec.h>
#include <linux/sizes.h>
#include <linux/device.h>
#include <linux/dma-contiguous.h>
#include <linux/decompress/generic.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/bugs.h>
#include <asm/cache.h>
#include <asm/cdmm.h>
#include <asm/cpu.h>
#include <asm/debug.h>
#include <asm/dma-coherence.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp-ops.h>
#include <asm/prom.h>

#ifdef CONFIG_MIPS_ELF_APPENDED_DTB
const char __section(.appended_dtb) __appended_dtb[0x100000];
#endif /* CONFIG_MIPS_ELF_APPENDED_DTB */

struct cpuinfo_mips cpu_data[NR_CPUS] __read_mostly;

EXPORT_SYMBOL(cpu_data);

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

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
static struct resource bss_resource = { .name = "Kernel bss", };

static void *detect_magic __initdata = detect_memory_region;

#ifdef CONFIG_MIPS_AUTO_PFN_OFFSET
unsigned long ARCH_PFN_OFFSET;
EXPORT_SYMBOL(ARCH_PFN_OFFSET);
#endif

void __init add_memory_region(phys_addr_t start, phys_addr_t size, long type)
{
	int x = boot_mem_map.nr_map;
	int i;

	/*
	 * If the region reaches the top of the physical address space, adjust
	 * the size slightly so that (start + size) doesn't overflow
	 */
	if (start + size - 1 == PHYS_ADDR_MAX)
		--size;

	/* Sanity check */
	if (start + size < start) {
		pr_warn("Trying to add an invalid memory region, skipped\n");
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

void __init detect_memory_region(phys_addr_t start, phys_addr_t sz_min, phys_addr_t sz_max)
{
	void *dm = &detect_magic;
	phys_addr_t size;

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

static bool __init __maybe_unused memory_region_available(phys_addr_t start,
							  phys_addr_t size)
{
	int i;
	bool in_ram = false, free = true;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		phys_addr_t start_, end_;

		start_ = boot_mem_map.map[i].addr;
		end_ = boot_mem_map.map[i].addr + boot_mem_map.map[i].size;

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			if (start >= start_ && start + size <= end_)
				in_ram = true;
			break;
		case BOOT_MEM_RESERVED:
		case BOOT_MEM_NOMAP:
			if ((start >= start_ && start < end_) ||
			    (start < start_ && start + size >= start_))
				free = false;
			break;
		default:
			continue;
		}
	}

	return in_ram && free;
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
		case BOOT_MEM_NOMAP:
			printk(KERN_CONT "(nomap)\n");
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

/* In some conditions (e.g. big endian bootloader with a little endian
   kernel), the initrd might appear byte swapped.  Try to detect this and
   byte swap it if needed.  */
static void __init maybe_bswap_initrd(void)
{
#if defined(CONFIG_CPU_CAVIUM_OCTEON)
	u64 buf;

	/* Check for CPIO signature */
	if (!memcmp((void *)initrd_start, "070701", 6))
		return;

	/* Check for compressed initrd */
	if (decompress_method((unsigned char *)initrd_start, 8, NULL))
		return;

	/* Try again with a byte swapped header */
	buf = swab64p((u64 *)initrd_start);
	if (!memcmp(&buf, "070701", 6) ||
	    decompress_method((unsigned char *)(&buf), 8, NULL)) {
		unsigned long i;

		pr_info("Byteswapped initrd detected\n");
		for (i = initrd_start; i < ALIGN(initrd_end, 8); i += 8)
			swab64s((u64 *)i);
	}
#endif
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

	maybe_bswap_initrd();

	memblock_reserve(__pa(initrd_start), size);
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
	phys_addr_t ramstart = PHYS_ADDR_MAX;
	int i;

	/*
	 * Sanity check any INITRD first. We don't take it into account
	 * for bootmem setup initially, rely on the end-of-kernel-code
	 * as our memory range starting point. Once bootmem is inited we
	 * will reserve the area used for the initrd.
	 */
	init_initrd();

	/* Reserve memory occupied by kernel. */
	memblock_reserve(__pa_symbol(&_text),
			__pa_symbol(&_end) - __pa_symbol(&_text));

	/*
	 * max_low_pfn is not a number of pages. The number of pages
	 * of the system is given by 'max_low_pfn - min_low_pfn'.
	 */
	min_low_pfn = ~0UL;
	max_low_pfn = 0;

	/* Find the highest and lowest page frame numbers we have available. */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
				+ boot_mem_map.map[i].size);

		ramstart = min(ramstart, boot_mem_map.map[i].addr);

#ifndef CONFIG_HIGHMEM
		/*
		 * Skip highmem here so we get an accurate max_low_pfn if low
		 * memory stops short of high memory.
		 * If the region overlaps HIGHMEM_START, end is clipped so
		 * max_pfn excludes the highmem portion.
		 */
		if (start >= PFN_DOWN(HIGHMEM_START))
			continue;
		if (end > PFN_DOWN(HIGHMEM_START))
			end = PFN_DOWN(HIGHMEM_START);
#endif

		if (end > max_low_pfn)
			max_low_pfn = end;
		if (start < min_low_pfn)
			min_low_pfn = start;
	}

	if (min_low_pfn >= max_low_pfn)
		panic("Incorrect memory mapping !!!");

#ifdef CONFIG_MIPS_AUTO_PFN_OFFSET
	ARCH_PFN_OFFSET = PFN_UP(ramstart);
#else
	/*
	 * Reserve any memory between the start of RAM and PHYS_OFFSET
	 */
	if (ramstart > PHYS_OFFSET) {
		add_memory_region(PHYS_OFFSET, ramstart - PHYS_OFFSET,
				  BOOT_MEM_RESERVED);
		memblock_reserve(PHYS_OFFSET, ramstart - PHYS_OFFSET);
	}

	if (min_low_pfn > ARCH_PFN_OFFSET) {
		pr_info("Wasting %lu bytes for tracking %lu unused pages\n",
			(min_low_pfn - ARCH_PFN_OFFSET) * sizeof(struct page),
			min_low_pfn - ARCH_PFN_OFFSET);
	} else if (ARCH_PFN_OFFSET - min_low_pfn > 0UL) {
		pr_info("%lu free pages won't be used\n",
			ARCH_PFN_OFFSET - min_low_pfn);
	}
	min_low_pfn = ARCH_PFN_OFFSET;
#endif

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

	/* Install all valid RAM ranges to the memblock memory region */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
				+ boot_mem_map.map[i].size);

		if (start < min_low_pfn)
			start = min_low_pfn;
#ifndef CONFIG_HIGHMEM
		/* Ignore highmem regions if highmem is unsupported */
		if (end > max_low_pfn)
			end = max_low_pfn;
#endif
		if (end <= start)
			continue;

		memblock_add_node(PFN_PHYS(start), PFN_PHYS(end - start), 0);

		/* Reserve any memory except the ordinary RAM ranges. */
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			break;
		case BOOT_MEM_NOMAP: /* Discard the range from the system. */
			memblock_remove(PFN_PHYS(start), PFN_PHYS(end - start));
			continue;
		default: /* Reserve the rest of the memory types at boot time */
			memblock_reserve(PFN_PHYS(start), PFN_PHYS(end - start));
			break;
		}

		/*
		 * In any case the added to the memblock memory regions
		 * (highmem/lowmem, available/reserved, etc) are considered
		 * as present, so inform sparsemem about them.
		 */
		memory_present(0, start, end);
	}

	/*
	 * Reserve initrd memory if needed.
	 */
	finalize_initrd();
}

#endif	/* CONFIG_SGI_IP27 */

static int usermem __initdata;

static int __init early_parse_mem(char *p)
{
	phys_addr_t start, size;

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

static int __init early_parse_memmap(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
		pr_err("\"memmap=exactmap\" invalid on MIPS\n");
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	if (*p == '@') {
		start_at = memparse(p+1, &p);
		add_memory_region(start_at, mem_size, BOOT_MEM_RAM);
	} else if (*p == '#') {
		pr_err("\"memmap=nn#ss\" (force ACPI data) invalid on MIPS\n");
		return -EINVAL;
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		add_memory_region(start_at, mem_size, BOOT_MEM_RESERVED);
	} else {
		pr_err("\"memmap\" invalid format!\n");
		return -EINVAL;
	}

	if (*p == '\0') {
		usermem = 1;
		return 0;
	} else
		return -EINVAL;
}
early_param("memmap", early_parse_memmap);

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

static void __init arch_mem_addpart(phys_addr_t mem, phys_addr_t end, int type)
{
	phys_addr_t size;
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

	if (!memory_region_available(crash_base, crash_size)) {
		pr_warn("Invalid memory region reserved for crash kernel\n");
		return;
	}

	crashk_res.start = crash_base;
	crashk_res.end	 = crash_base + crash_size - 1;
}

static void __init request_crashkernel(struct resource *res)
{
	int ret;

	if (crashk_res.start == crashk_res.end)
		return;

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

#define USE_PROM_CMDLINE	IS_ENABLED(CONFIG_MIPS_CMDLINE_FROM_BOOTLOADER)
#define USE_DTB_CMDLINE		IS_ENABLED(CONFIG_MIPS_CMDLINE_FROM_DTB)
#define EXTEND_WITH_PROM	IS_ENABLED(CONFIG_MIPS_CMDLINE_DTB_EXTEND)
#define BUILTIN_EXTEND_WITH_PROM	\
	IS_ENABLED(CONFIG_MIPS_CMDLINE_BUILTIN_EXTEND)

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
 *  o dma_contiguous_reserve()
 *
 * At this stage the bootmem allocator is ready to use.
 *
 * NOTE: historically plat_mem_setup did the entire platform initialization.
 *	 This was rather impractical because it meant plat_mem_setup had to
 * get away without any kind of memory allocator.  To keep old code from
 * breaking plat_setup was just renamed to plat_mem_setup and a second platform
 * initialization hook for anything else was introduced.
 */
static void __init arch_mem_init(char **cmdline_p)
{
	extern void plat_mem_setup(void);

	/*
	 * Initialize boot_command_line to an innocuous but non-empty string in
	 * order to prevent early_init_dt_scan_chosen() from copying
	 * CONFIG_CMDLINE into it without our knowledge. We handle
	 * CONFIG_CMDLINE ourselves below & don't want to duplicate its
	 * content because repeating arguments can be problematic.
	 */
	strlcpy(boot_command_line, " ", COMMAND_LINE_SIZE);

	/* call board setup routine */
	plat_mem_setup();
	memblock_set_bottom_up(true);

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
	arch_mem_addpart(PFN_DOWN(__pa_symbol(&__bss_start)) << PAGE_SHIFT,
			 PFN_UP(__pa_symbol(&__bss_stop)) << PAGE_SHIFT,
			 BOOT_MEM_RAM);

	pr_info("Determined physical RAM map:\n");
	print_memory_map();

#if defined(CONFIG_CMDLINE_BOOL) && defined(CONFIG_CMDLINE_OVERRIDE)
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if ((USE_PROM_CMDLINE && arcs_cmdline[0]) ||
	    (USE_DTB_CMDLINE && !boot_command_line[0]))
		strlcpy(boot_command_line, arcs_cmdline, COMMAND_LINE_SIZE);

	if (EXTEND_WITH_PROM && arcs_cmdline[0]) {
		if (boot_command_line[0])
			strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);
		strlcat(boot_command_line, arcs_cmdline, COMMAND_LINE_SIZE);
	}

#if defined(CONFIG_CMDLINE_BOOL)
	if (builtin_cmdline[0]) {
		if (boot_command_line[0])
			strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);
		strlcat(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}

	if (BUILTIN_EXTEND_WITH_PROM && arcs_cmdline[0]) {
		if (boot_command_line[0])
			strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);
		strlcat(boot_command_line, arcs_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif
	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);

	*cmdline_p = command_line;

	parse_early_param();

	if (usermem) {
		pr_info("User-defined physical RAM map:\n");
		print_memory_map();
	}

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	bootmem_init();

	/*
	 * Prevent memblock from allocating high memory.
	 * This cannot be done before max_low_pfn is detected, so up
	 * to this point is possible to only reserve physical memory
	 * with memblock_reserve; memblock_alloc* can be used
	 * only after this point
	 */
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

#ifdef CONFIG_PROC_VMCORE
	if (setup_elfcorehdr && setup_elfcorehdr_size) {
		printk(KERN_INFO "kdump reserved memory at %lx-%lx\n",
		       setup_elfcorehdr, setup_elfcorehdr_size);
		memblock_reserve(setup_elfcorehdr, setup_elfcorehdr_size);
	}
#endif

	mips_parse_crashkernel();
#ifdef CONFIG_KEXEC
	if (crashk_res.start != crashk_res.end)
		memblock_reserve(crashk_res.start,
				 crashk_res.end - crashk_res.start + 1);
#endif
	device_tree_init();
	sparse_init();
	plat_swiotlb_setup();

	dma_contiguous_reserve(PFN_PHYS(max_low_pfn));

	/* Reserve for hibernation. */
	memblock_reserve(__pa_symbol(&__nosave_begin),
		__pa_symbol(&__nosave_end) - __pa_symbol(&__nosave_begin));

	fdt_init_reserved_mem();

	memblock_dump_all();

	early_memtest(PFN_PHYS(min_low_pfn), PFN_PHYS(max_low_pfn));
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
	bss_resource.start = __pa_symbol(&__bss_start);
	bss_resource.end = __pa_symbol(&__bss_stop) - 1;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct resource *res;
		unsigned long start, end;

		start = boot_mem_map.map[i].addr;
		end = boot_mem_map.map[i].addr + boot_mem_map.map[i].size - 1;
		if (start >= HIGHMEM_START)
			continue;
		if (end >= HIGHMEM_START)
			end = HIGHMEM_START - 1;

		res = memblock_alloc(sizeof(struct resource), SMP_CACHE_BYTES);
		if (!res)
			panic("%s: Failed to allocate %zu bytes\n", __func__,
			      sizeof(struct resource));

		res->start = start;
		res->end = end;
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_INIT_RAM:
		case BOOT_MEM_ROM_DATA:
			res->name = "System RAM";
			res->flags |= IORESOURCE_SYSRAM;
			break;
		case BOOT_MEM_RESERVED:
		case BOOT_MEM_NOMAP:
		default:
			res->name = "reserved";
		}

		request_resource(&iomem_resource, res);

		/*
		 *  We don't know which RAM region contains kernel data,
		 *  so we try it repeatedly and let the resource manager
		 *  test it.
		 */
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
		request_resource(res, &bss_resource);
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
	mips_cm_probe();
	prom_init();

	setup_early_fdc_console();
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
	paging_init();
}

unsigned long kernelsp[NR_CPUS];
unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

#ifdef CONFIG_USE_OF
unsigned long fw_passed_dtb;
#endif

#ifdef CONFIG_DEBUG_FS
struct dentry *mips_debugfs_dir;
static int __init debugfs_mips(void)
{
	mips_debugfs_dir = debugfs_create_dir("mips", NULL);
	return 0;
}
arch_initcall(debugfs_mips);
#endif

#ifdef CONFIG_DMA_MAYBE_COHERENT
/* User defined DMA coherency from command line. */
enum coherent_io_user_state coherentio = IO_COHERENCE_DEFAULT;
EXPORT_SYMBOL_GPL(coherentio);
int hw_coherentio = 0;	/* Actual hardware supported DMA coherency setting. */

static int __init setcoherentio(char *str)
{
	coherentio = IO_COHERENCE_ENABLED;
	pr_info("Hardware DMA cache coherency (command line)\n");
	return 0;
}
early_param("coherentio", setcoherentio);

static int __init setnocoherentio(char *str)
{
	coherentio = IO_COHERENCE_DISABLED;
	pr_info("Software DMA cache coherency (command line)\n");
	return 0;
}
early_param("nocoherentio", setnocoherentio);
#endif
