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
#include <linux/dmi.h>

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

static char __initdata command_line[COMMAND_LINE_SIZE];
char __initdata arcs_cmdline[COMMAND_LINE_SIZE];

#ifdef CONFIG_CMDLINE_BOOL
static const char builtin_cmdline[] __initconst = CONFIG_CMDLINE;
#else
static const char builtin_cmdline[] __initconst = "";
#endif

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
unsigned long mips_io_port_base = -1;
EXPORT_SYMBOL(mips_io_port_base);

static struct resource code_resource = { .name = "Kernel code", };
static struct resource data_resource = { .name = "Kernel data", };
static struct resource bss_resource = { .name = "Kernel bss", };

static void *detect_magic __initdata = detect_memory_region;

#ifdef CONFIG_MIPS_AUTO_PFN_OFFSET
unsigned long ARCH_PFN_OFFSET;
EXPORT_SYMBOL(ARCH_PFN_OFFSET);
#endif

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

	memblock_add(start, size);
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
#if defined(CONFIG_SGI_IP27) || (defined(CONFIG_CPU_LOONGSON64) && defined(CONFIG_NUMA))

static void __init bootmem_init(void)
{
	init_initrd();
	finalize_initrd();
}

#else  /* !CONFIG_SGI_IP27 */

static void __init bootmem_init(void)
{
	struct memblock_region *mem;
	phys_addr_t ramstart, ramend;

	ramstart = memblock_start_of_DRAM();
	ramend = memblock_end_of_DRAM();

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

	/* max_low_pfn is not a number of pages but the end pfn of low mem */

#ifdef CONFIG_MIPS_AUTO_PFN_OFFSET
	ARCH_PFN_OFFSET = PFN_UP(ramstart);
#else
	/*
	 * Reserve any memory between the start of RAM and PHYS_OFFSET
	 */
	if (ramstart > PHYS_OFFSET)
		memblock_reserve(PHYS_OFFSET, ramstart - PHYS_OFFSET);

	if (PFN_UP(ramstart) > ARCH_PFN_OFFSET) {
		pr_info("Wasting %lu bytes for tracking %lu unused pages\n",
			(unsigned long)((PFN_UP(ramstart) - ARCH_PFN_OFFSET) * sizeof(struct page)),
			(unsigned long)(PFN_UP(ramstart) - ARCH_PFN_OFFSET));
	}
#endif

	min_low_pfn = ARCH_PFN_OFFSET;
	max_pfn = PFN_DOWN(ramend);
	for_each_memblock(memory, mem) {
		unsigned long start = memblock_region_memory_base_pfn(mem);
		unsigned long end = memblock_region_memory_end_pfn(mem);

		/*
		 * Skip highmem here so we get an accurate max_low_pfn if low
		 * memory stops short of high memory.
		 * If the region overlaps HIGHMEM_START, end is clipped so
		 * max_pfn excludes the highmem portion.
		 */
		if (memblock_is_nomap(mem))
			continue;
		if (start >= PFN_DOWN(HIGHMEM_START))
			continue;
		if (end > PFN_DOWN(HIGHMEM_START))
			end = PFN_DOWN(HIGHMEM_START);
		if (end > max_low_pfn)
			max_low_pfn = end;
	}

	if (min_low_pfn >= max_low_pfn)
		panic("Incorrect memory mapping !!!");

	if (max_pfn > PFN_DOWN(HIGHMEM_START)) {
#ifdef CONFIG_HIGHMEM
		highstart_pfn = PFN_DOWN(HIGHMEM_START);
		highend_pfn = max_pfn;
#else
		max_low_pfn = PFN_DOWN(HIGHMEM_START);
		max_pfn = max_low_pfn;
#endif
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
		usermem = 1;
		memblock_remove(memblock_start_of_DRAM(),
			memblock_end_of_DRAM() - memblock_start_of_DRAM());
	}
	start = 0;
	size = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);

	memblock_add(start, size);

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
		memblock_add(start_at, mem_size);
	} else if (*p == '#') {
		pr_err("\"memmap=nn#ss\" (force ACPI data) invalid on MIPS\n");
		return -EINVAL;
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		memblock_add(start_at, mem_size);
		memblock_reserve(start_at, mem_size);
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
static unsigned long setup_elfcorehdr, setup_elfcorehdr_size;
static int __init early_parse_elfcorehdr(char *p)
{
	struct memblock_region *mem;

	setup_elfcorehdr = memparse(p, &p);

	 for_each_memblock(memory, mem) {
		unsigned long start = mem->base;
		unsigned long end = start + mem->size;
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

#ifdef CONFIG_KEXEC

/* 64M alignment for crash kernel regions */
#define CRASH_ALIGN	SZ_64M
#define CRASH_ADDR_MAX	SZ_512M

static void __init mips_parse_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = memblock_phys_mem_size();
	ret = parse_crashkernel(boot_command_line, total_mem,
				&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0)
		return;

	if (crash_base <= 0) {
		crash_base = memblock_find_in_range(CRASH_ALIGN, CRASH_ADDR_MAX,
							crash_size, CRASH_ALIGN);
		if (!crash_base) {
			pr_warn("crashkernel reservation failed - No suitable area found.\n");
			return;
		}
	} else {
		unsigned long long start;

		start = memblock_find_in_range(crash_base, crash_base + crash_size,
						crash_size, 1);
		if (start != crash_base) {
			pr_warn("Invalid memory region reserved for crash kernel\n");
			return;
		}
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
			(unsigned long)(resource_size(&crashk_res) >> 20),
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

static void __init check_kernel_sections_mem(void)
{
	phys_addr_t start = PFN_PHYS(PFN_DOWN(__pa_symbol(&_text)));
	phys_addr_t size = PFN_PHYS(PFN_UP(__pa_symbol(&_end))) - start;

	if (!memblock_is_region_memory(start, size)) {
		pr_info("Kernel sections are not in the memory maps\n");
		memblock_add(start, size);
	}
}

static void __init bootcmdline_append(const char *s, size_t max)
{
	if (!s[0] || !max)
		return;

	if (boot_command_line[0])
		strlcat(boot_command_line, " ", COMMAND_LINE_SIZE);

	strlcat(boot_command_line, s, max);
}

#ifdef CONFIG_OF_EARLY_FLATTREE

static int __init bootcmdline_scan_chosen(unsigned long node, const char *uname,
					  int depth, void *data)
{
	bool *dt_bootargs = data;
	const char *p;
	int l;

	if (depth != 1 || !data ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0) {
		bootcmdline_append(p, min(l, COMMAND_LINE_SIZE));
		*dt_bootargs = true;
	}

	return 1;
}

#endif /* CONFIG_OF_EARLY_FLATTREE */

static void __init bootcmdline_init(void)
{
	bool dt_bootargs = false;

	/*
	 * If CMDLINE_OVERRIDE is enabled then initializing the command line is
	 * trivial - we simply use the built-in command line unconditionally &
	 * unmodified.
	 */
	if (IS_ENABLED(CONFIG_CMDLINE_OVERRIDE)) {
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
		return;
	}

	/*
	 * If the user specified a built-in command line &
	 * MIPS_CMDLINE_BUILTIN_EXTEND, then the built-in command line is
	 * prepended to arguments from the bootloader or DT so we'll copy them
	 * to the start of boot_command_line here. Otherwise, empty
	 * boot_command_line to undo anything early_init_dt_scan_chosen() did.
	 */
	if (IS_ENABLED(CONFIG_MIPS_CMDLINE_BUILTIN_EXTEND))
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	else
		boot_command_line[0] = 0;

#ifdef CONFIG_OF_EARLY_FLATTREE
	/*
	 * If we're configured to take boot arguments from DT, look for those
	 * now.
	 */
	if (IS_ENABLED(CONFIG_MIPS_CMDLINE_FROM_DTB) ||
	    IS_ENABLED(CONFIG_MIPS_CMDLINE_DTB_EXTEND))
		of_scan_flat_dt(bootcmdline_scan_chosen, &dt_bootargs);
#endif

	/*
	 * If we didn't get any arguments from DT (regardless of whether that's
	 * because we weren't configured to look for them, or because we looked
	 * & found none) then we'll take arguments from the bootloader.
	 * plat_mem_setup() should have filled arcs_cmdline with arguments from
	 * the bootloader.
	 */
	if (IS_ENABLED(CONFIG_MIPS_CMDLINE_DTB_EXTEND) || !dt_bootargs)
		bootcmdline_append(arcs_cmdline, COMMAND_LINE_SIZE);

	/*
	 * If the user specified a built-in command line & we didn't already
	 * prepend it, we append it to boot_command_line here.
	 */
	if (IS_ENABLED(CONFIG_CMDLINE_BOOL) &&
	    !IS_ENABLED(CONFIG_MIPS_CMDLINE_BUILTIN_EXTEND))
		bootcmdline_append(builtin_cmdline, COMMAND_LINE_SIZE);
}

/*
 * arch_mem_init - initialize memory management subsystem
 *
 *  o plat_mem_setup() detects the memory configuration and will record detected
 *    memory areas using memblock_add.
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
	/* call board setup routine */
	plat_mem_setup();
	memblock_set_bottom_up(true);

	bootcmdline_init();
	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();

	if (usermem)
		pr_info("User-defined physical RAM map overwrite\n");

	check_kernel_sections_mem();

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

#ifndef CONFIG_NUMA
	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);
#endif
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
		memblock_reserve(crashk_res.start, resource_size(&crashk_res));
#endif
	device_tree_init();

	/*
	 * In order to reduce the possibility of kernel panic when failed to
	 * get IO TLB memory under CONFIG_SWIOTLB, it is better to allocate
	 * low memory as small as possible before plat_swiotlb_setup(), so
	 * make sparse_init() using top-down allocation.
	 */
	memblock_set_bottom_up(false);
	sparse_init();
	memblock_set_bottom_up(true);

	plat_swiotlb_setup();

	dma_contiguous_reserve(PFN_PHYS(max_low_pfn));

	/* Reserve for hibernation. */
	memblock_reserve(__pa_symbol(&__nosave_begin),
		__pa_symbol(&__nosave_end) - __pa_symbol(&__nosave_begin));

	fdt_init_reserved_mem();

	memblock_dump_all();

	early_memtest(PFN_PHYS(ARCH_PFN_OFFSET), PFN_PHYS(max_low_pfn));
}

static void __init resource_init(void)
{
	struct memblock_region *region;

	if (UNCAC_BASE != IO_BASE)
		return;

	code_resource.start = __pa_symbol(&_text);
	code_resource.end = __pa_symbol(&_etext) - 1;
	data_resource.start = __pa_symbol(&_etext);
	data_resource.end = __pa_symbol(&_edata) - 1;
	bss_resource.start = __pa_symbol(&__bss_start);
	bss_resource.end = __pa_symbol(&__bss_stop) - 1;

	for_each_memblock(memory, region) {
		phys_addr_t start = PFN_PHYS(memblock_region_memory_base_pfn(region));
		phys_addr_t end = PFN_PHYS(memblock_region_memory_end_pfn(region)) - 1;
		struct resource *res;

		res = memblock_alloc(sizeof(struct resource), SMP_CACHE_BYTES);
		if (!res)
			panic("%s: Failed to allocate %zu bytes\n", __func__,
			      sizeof(struct resource));

		res->start = start;
		res->end = end;
		res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		res->name = "System RAM";

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
#endif
#endif

	arch_mem_init(cmdline_p);
	dmi_setup();

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
int hw_coherentio;	/* Actual hardware supported DMA coherency setting. */

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
