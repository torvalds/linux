/*
 * arch/sh/kernel/setup.c
 *
 * This file handles the architecture-dependent parts of initialization
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2007 Paul Mundt
 */
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/utsname.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/pfn.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/crash_dump.h>
#include <linux/mmzone.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/lmb.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/elf.h>
#include <asm/sections.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/clock.h>
#include <asm/mmu_context.h>

/*
 * Initialize loops_per_jiffy as 10000000 (1000MIPS).
 * This value will be used at the very early stage of serial setup.
 * The bigger value means no problem.
 */
struct sh_cpuinfo cpu_data[NR_CPUS] __read_mostly = {
	[0] = {
		.type			= CPU_SH_NONE,
		.family			= CPU_FAMILY_UNKNOWN,
		.loops_per_jiffy	= 10000000,
	},
};
EXPORT_SYMBOL(cpu_data);

/*
 * The machine vector. First entry in .machvec.init, or clobbered by
 * sh_mv= on the command line, prior to .machvec.init teardown.
 */
struct sh_machine_vector sh_mv = { .mv_name = "generic", };
EXPORT_SYMBOL(sh_mv);

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

extern int root_mountflags;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE] = { 0, };

static struct resource code_resource = {
	.name = "Kernel code",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource data_resource = {
	.name = "Kernel data",
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM,
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM,
};

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_end = 0;
EXPORT_SYMBOL(memory_end);

static struct resource mem_resources[MAX_NUMNODES];

int l1i_cache_shape, l1d_cache_shape, l2_cache_shape;

static int __init early_parse_mem(char *p)
{
	unsigned long size;

	memory_start = (unsigned long)__va(__MEMORY_START);
	size = memparse(p, &p);

	if (size > __MEMORY_SIZE) {
		printk(KERN_ERR
			"Using mem= to increase the size of kernel memory "
			"is not allowed.\n"
			"  Recompile the kernel with the correct value for "
			"CONFIG_MEMORY_SIZE.\n");
		return 0;
	}

	memory_end = memory_start + size;

	return 0;
}
early_param("mem", early_parse_mem);

/*
 * Register fully available low RAM pages with the bootmem allocator.
 */
static void __init register_bootmem_low_pages(void)
{
	unsigned long curr_pfn, last_pfn, pages;

	/*
	 * We are rounding up the start address of usable memory:
	 */
	curr_pfn = PFN_UP(__MEMORY_START);

	/*
	 * ... and at the end of the usable range downwards:
	 */
	last_pfn = PFN_DOWN(__pa(memory_end));

	if (last_pfn > max_low_pfn)
		last_pfn = max_low_pfn;

	pages = last_pfn - curr_pfn;
	free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(pages));
}

#ifdef CONFIG_KEXEC
static void __init reserve_crashkernel(void)
{
	unsigned long long free_mem;
	unsigned long long crash_size, crash_base;
	void *vp;
	int ret;

	free_mem = ((unsigned long long)max_low_pfn - min_low_pfn) << PAGE_SHIFT;

	ret = parse_crashkernel(boot_command_line, free_mem,
			&crash_size, &crash_base);
	if (ret == 0 && crash_size) {
		if (crash_base <= 0) {
			vp = alloc_bootmem_nopanic(crash_size);
			if (!vp) {
				printk(KERN_INFO "crashkernel allocation "
				       "failed\n");
				return;
			}
			crash_base = __pa(vp);
		} else if (reserve_bootmem(crash_base, crash_size,
					BOOTMEM_EXCLUSIVE) < 0) {
			printk(KERN_INFO "crashkernel reservation failed - "
					"memory is in use\n");
			return;
		}

		printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
				"for crashkernel (System RAM: %ldMB)\n",
				(unsigned long)(crash_size >> 20),
				(unsigned long)(crash_base >> 20),
				(unsigned long)(free_mem >> 20));
		crashk_res.start = crash_base;
		crashk_res.end   = crash_base + crash_size - 1;
		insert_resource(&iomem_resource, &crashk_res);
	}
}
#else
static inline void __init reserve_crashkernel(void)
{}
#endif

void __cpuinit calibrate_delay(void)
{
	struct clk *clk = clk_get(NULL, "cpu_clk");

	if (IS_ERR(clk))
		panic("Need a sane CPU clock definition!");

	loops_per_jiffy = (clk_get_rate(clk) >> 1) / HZ;

	printk(KERN_INFO "Calibrating delay loop (skipped)... "
			 "%lu.%02lu BogoMIPS PRESET (lpj=%lu)\n",
			 loops_per_jiffy/(500000/HZ),
			 (loops_per_jiffy/(5000/HZ)) % 100,
			 loops_per_jiffy);
}

void __init __add_active_range(unsigned int nid, unsigned long start_pfn,
						unsigned long end_pfn)
{
	struct resource *res = &mem_resources[nid];

	WARN_ON(res->name); /* max one active range per node for now */

	res->name = "System RAM";
	res->start = start_pfn << PAGE_SHIFT;
	res->end = (end_pfn << PAGE_SHIFT) - 1;
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, res)) {
		pr_err("unable to request memory_resource 0x%lx 0x%lx\n",
		       start_pfn, end_pfn);
		return;
	}

	/*
	 *  We don't know which RAM region contains kernel data,
	 *  so we try it repeatedly and let the resource manager
	 *  test it.
	 */
	request_resource(res, &code_resource);
	request_resource(res, &data_resource);
	request_resource(res, &bss_resource);

	add_active_range(nid, start_pfn, end_pfn);
}

void __init setup_bootmem_allocator(unsigned long free_pfn)
{
	unsigned long bootmap_size;
	unsigned long bootmap_pages, bootmem_paddr;
	u64 total_pages = (lmb_end_of_DRAM() - __MEMORY_START) >> PAGE_SHIFT;
	int i;

	bootmap_pages = bootmem_bootmap_pages(total_pages);

	bootmem_paddr = lmb_alloc(bootmap_pages << PAGE_SHIFT, PAGE_SIZE);

	/*
	 * Find a proper area for the bootmem bitmap. After this
	 * bootstrap step all allocations (until the page allocator
	 * is intact) must be done via bootmem_alloc().
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 bootmem_paddr >> PAGE_SHIFT,
					 min_low_pfn, max_low_pfn);

	/* Add active regions with valid PFNs. */
	for (i = 0; i < lmb.memory.cnt; i++) {
		unsigned long start_pfn, end_pfn;
		start_pfn = lmb.memory.region[i].base >> PAGE_SHIFT;
		end_pfn = start_pfn + lmb_size_pages(&lmb.memory, i);
		__add_active_range(0, start_pfn, end_pfn);
	}

	/*
	 * Add all physical memory to the bootmem map and mark each
	 * area as present.
	 */
	register_bootmem_low_pages();

	/* Reserve the sections we're already using. */
	for (i = 0; i < lmb.reserved.cnt; i++)
		reserve_bootmem(lmb.reserved.region[i].base,
				lmb_size_bytes(&lmb.reserved, i),
				BOOTMEM_DEFAULT);

	node_set_online(0);

	sparse_memory_present_with_active_regions(0);

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = Root_RAM0;

	if (LOADER_TYPE && INITRD_START) {
		unsigned long initrd_start_phys = INITRD_START + __MEMORY_START;

		if (initrd_start_phys + INITRD_SIZE <= PFN_PHYS(max_low_pfn)) {
			reserve_bootmem(initrd_start_phys, INITRD_SIZE,
					BOOTMEM_DEFAULT);
			initrd_start = (unsigned long)__va(initrd_start_phys);
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_start_phys + INITRD_SIZE,
			       (unsigned long)PFN_PHYS(max_low_pfn));
			initrd_start = 0;
		}
	}
#endif

	reserve_crashkernel();
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
static void __init setup_memory(void)
{
	unsigned long start_pfn;
	u64 base = min_low_pfn << PAGE_SHIFT;
	u64 size = (max_low_pfn << PAGE_SHIFT) - base;

	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(_end));

	lmb_add(base, size);

	/*
	 * Reserve the kernel text and
	 * Reserve the bootmem bitmap. We do this in two steps (first step
	 * was init_bootmem()), because this catches the (definitely buggy)
	 * case of us accidentally initializing the bootmem allocator with
	 * an invalid RAM area.
	 */
	lmb_reserve(__MEMORY_START + CONFIG_ZERO_PAGE_OFFSET,
		    (PFN_PHYS(start_pfn) + PAGE_SIZE - 1) -
		    (__MEMORY_START + CONFIG_ZERO_PAGE_OFFSET));

	/*
	 * Reserve physical pages below CONFIG_ZERO_PAGE_OFFSET.
	 */
	if (CONFIG_ZERO_PAGE_OFFSET != 0)
		lmb_reserve(__MEMORY_START, CONFIG_ZERO_PAGE_OFFSET);

	lmb_analyze();
	lmb_dump_all();

	setup_bootmem_allocator(start_pfn);
}
#else
extern void __init setup_memory(void);
#endif

/*
 * Note: elfcorehdr_addr is not just limited to vmcore. It is also used by
 * is_kdump_kernel() to determine if we are booting after a panic. Hence
 * ifdef it under CONFIG_CRASH_DUMP and not CONFIG_PROC_VMCORE.
 */
#ifdef CONFIG_CRASH_DUMP
/* elfcorehdr= specifies the location of elf core header
 * stored by the crashed kernel.
 */
static int __init parse_elfcorehdr(char *arg)
{
	if (!arg)
		return -EINVAL;
	elfcorehdr_addr = memparse(arg, &arg);
	return 0;
}
early_param("elfcorehdr", parse_elfcorehdr);
#endif

void __init __attribute__ ((weak)) plat_early_device_setup(void)
{
}

void __init setup_arch(char **cmdline_p)
{
	enable_mmu();

	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);

	printk(KERN_NOTICE "Boot params:\n"
			   "... MOUNT_ROOT_RDONLY - %08lx\n"
			   "... RAMDISK_FLAGS     - %08lx\n"
			   "... ORIG_ROOT_DEV     - %08lx\n"
			   "... LOADER_TYPE       - %08lx\n"
			   "... INITRD_START      - %08lx\n"
			   "... INITRD_SIZE       - %08lx\n",
			   MOUNT_ROOT_RDONLY, RAMDISK_FLAGS,
			   ORIG_ROOT_DEV, LOADER_TYPE,
			   INITRD_START, INITRD_SIZE);

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(__bss_start);
	bss_resource.end = virt_to_phys(_ebss)-1;

	memory_start = (unsigned long)__va(__MEMORY_START);
	if (!memory_end)
		memory_end = memory_start + __MEMORY_SIZE;

#ifdef CONFIG_CMDLINE_OVERWRITE
	strlcpy(command_line, CONFIG_CMDLINE, sizeof(command_line));
#else
	strlcpy(command_line, COMMAND_LINE, sizeof(command_line));
#ifdef CONFIG_CMDLINE_EXTEND
	strlcat(command_line, " ", sizeof(command_line));
	strlcat(command_line, CONFIG_CMDLINE, sizeof(command_line));
#endif
#endif

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();

	plat_early_device_setup();

	/* Let earlyprintk output early console messages */
	early_platform_driver_probe("earlyprintk", 1, 1);

	sh_mv_setup();

	/*
	 * Find the highest page frame number we have available
	 */
	max_pfn = PFN_DOWN(__pa(memory_end));

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn;
	min_low_pfn = __MEMORY_START >> PAGE_SHIFT;

	nodes_clear(node_online_map);

	/* Setup bootmem with available RAM */
	lmb_init();
	setup_memory();
	sparse_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Perform the machine specific initialisation */
	if (likely(sh_mv.mv_setup))
		sh_mv.mv_setup(cmdline_p);

	paging_init();

#ifdef CONFIG_PMB_ENABLE
	pmb_init();
#endif

#ifdef CONFIG_SMP
	plat_smp_setup();
#endif
}

/* processor boot mode configuration */
int generic_mode_pins(void)
{
	pr_warning("generic_mode_pins(): missing mode pin configuration\n");
	return 0;
}

int test_mode_pin(int pin)
{
	return sh_mv.mv_mode_pins() & pin;
}

static const char *cpu_name[] = {
	[CPU_SH7201]	= "SH7201",
	[CPU_SH7203]	= "SH7203",	[CPU_SH7263]	= "SH7263",
	[CPU_SH7206]	= "SH7206",	[CPU_SH7619]	= "SH7619",
	[CPU_SH7705]	= "SH7705",	[CPU_SH7706]	= "SH7706",
	[CPU_SH7707]	= "SH7707",	[CPU_SH7708]	= "SH7708",
	[CPU_SH7709]	= "SH7709",	[CPU_SH7710]	= "SH7710",
	[CPU_SH7712]	= "SH7712",	[CPU_SH7720]	= "SH7720",
	[CPU_SH7721]	= "SH7721",	[CPU_SH7729]	= "SH7729",
	[CPU_SH7750]	= "SH7750",	[CPU_SH7750S]	= "SH7750S",
	[CPU_SH7750R]	= "SH7750R",	[CPU_SH7751]	= "SH7751",
	[CPU_SH7751R]	= "SH7751R",	[CPU_SH7760]	= "SH7760",
	[CPU_SH4_202]	= "SH4-202",	[CPU_SH4_501]	= "SH4-501",
	[CPU_SH7763]	= "SH7763",	[CPU_SH7770]	= "SH7770",
	[CPU_SH7780]	= "SH7780",	[CPU_SH7781]	= "SH7781",
	[CPU_SH7343]	= "SH7343",	[CPU_SH7785]	= "SH7785",
	[CPU_SH7786]	= "SH7786",	[CPU_SH7757]	= "SH7757",
	[CPU_SH7722]	= "SH7722",	[CPU_SHX3]	= "SH-X3",
	[CPU_SH5_101]	= "SH5-101",	[CPU_SH5_103]	= "SH5-103",
	[CPU_MXG]	= "MX-G",	[CPU_SH7723]	= "SH7723",
	[CPU_SH7366]	= "SH7366",	[CPU_SH7724]	= "SH7724",
	[CPU_SH_NONE]	= "Unknown"
};

const char *get_cpu_subtype(struct sh_cpuinfo *c)
{
	return cpu_name[c->type];
}
EXPORT_SYMBOL(get_cpu_subtype);

#ifdef CONFIG_PROC_FS
/* Symbolic CPU flags, keep in sync with asm/cpu-features.h */
static const char *cpu_flags[] = {
	"none", "fpu", "p2flush", "mmuassoc", "dsp", "perfctr",
	"ptea", "llsc", "l2", "op32", "pteaex", NULL
};

static void show_cpuflags(struct seq_file *m, struct sh_cpuinfo *c)
{
	unsigned long i;

	seq_printf(m, "cpu flags\t:");

	if (!c->flags) {
		seq_printf(m, " %s\n", cpu_flags[0]);
		return;
	}

	for (i = 0; cpu_flags[i]; i++)
		if ((c->flags & (1 << i)))
			seq_printf(m, " %s", cpu_flags[i+1]);

	seq_printf(m, "\n");
}

static void show_cacheinfo(struct seq_file *m, const char *type,
			   struct cache_info info)
{
	unsigned int cache_size;

	cache_size = info.ways * info.sets * info.linesz;

	seq_printf(m, "%s size\t: %2dKiB (%d-way)\n",
		   type, cache_size >> 10, info.ways);
}

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct sh_cpuinfo *c = v;
	unsigned int cpu = c - cpu_data;

	if (!cpu_online(cpu))
		return 0;

	if (cpu == 0)
		seq_printf(m, "machine\t\t: %s\n", get_system_type());
	else
		seq_printf(m, "\n");

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "cpu family\t: %s\n", init_utsname()->machine);
	seq_printf(m, "cpu type\t: %s\n", get_cpu_subtype(c));
	if (c->cut_major == -1)
		seq_printf(m, "cut\t\t: unknown\n");
	else if (c->cut_minor == -1)
		seq_printf(m, "cut\t\t: %d.x\n", c->cut_major);
	else
		seq_printf(m, "cut\t\t: %d.%d\n", c->cut_major, c->cut_minor);

	show_cpuflags(m, c);

	seq_printf(m, "cache type\t: ");

	/*
	 * Check for what type of cache we have, we support both the
	 * unified cache on the SH-2 and SH-3, as well as the harvard
	 * style cache on the SH-4.
	 */
	if (c->icache.flags & SH_CACHE_COMBINED) {
		seq_printf(m, "unified\n");
		show_cacheinfo(m, "cache", c->icache);
	} else {
		seq_printf(m, "split (harvard)\n");
		show_cacheinfo(m, "icache", c->icache);
		show_cacheinfo(m, "dcache", c->dcache);
	}

	/* Optional secondary cache */
	if (c->flags & CPU_HAS_L2_CACHE)
		show_cacheinfo(m, "scache", c->scache);

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		     c->loops_per_jiffy/(500000/HZ),
		     (c->loops_per_jiffy/(5000/HZ)) % 100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? cpu_data + *pos : NULL;
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}
static void c_stop(struct seq_file *m, void *v)
{
}
const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
#endif /* CONFIG_PROC_FS */

struct dentry *sh_debugfs_root;

static int __init sh_debugfs_init(void)
{
	sh_debugfs_root = debugfs_create_dir("sh", NULL);
	if (!sh_debugfs_root)
		return -ENOMEM;
	if (IS_ERR(sh_debugfs_root))
		return PTR_ERR(sh_debugfs_root);

	return 0;
}
arch_initcall(sh_debugfs_init);
