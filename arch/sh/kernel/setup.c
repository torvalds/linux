/* $Id: setup.c,v 1.30 2003/10/13 07:21:19 lethal Exp $
 *
 *  linux/arch/sh/kernel/setup.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002, 2003  Paul Mundt
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/utsname.h>
#include <linux/cpu.h>
#include <linux/pfn.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/clock.h>

#ifdef CONFIG_SH_KGDB
#include <asm/kgdb.h>
static int kgdb_parse_options(char *options);
#endif
extern void * __rd_start, * __rd_end;
/*
 * Machine setup..
 */

/*
 * Initialize loops_per_jiffy as 10000000 (1000MIPS).
 * This value will be used at the very early stage of serial setup.
 * The bigger value means no problem.
 */
struct sh_cpuinfo boot_cpu_data = { CPU_SH_NONE, 10000000, };
struct screen_info screen_info;

#if defined(CONFIG_SH_UNKNOWN)
struct sh_machine_vector sh_mv;
#endif

/* We need this to satisfy some external references. */
struct screen_info screen_info = {
        0, 25,                  /* orig-x, orig-y */
        0,                      /* unused */
        0,                      /* orig-video-page */
        0,                      /* orig-video-mode */
        80,                     /* orig-video-cols */
        0,0,0,                  /* ega_ax, ega_bx, ega_cx */
        25,                     /* orig-video-lines */
        0,                      /* orig-video-isVGA */
        16                      /* orig-video-points */
};

extern void platform_setup(void);
extern char *get_system_type(void);
extern int root_mountflags;

#define MV_NAME_SIZE 32

static struct sh_machine_vector* __init get_mv_byname(const char* name);

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)

#define MOUNT_ROOT_RDONLY (*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS (*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV (*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))
/* ... */
#define COMMAND_LINE ((char *) (PARAM+0x100))

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char command_line[COMMAND_LINE_SIZE] = { 0, };

struct resource standard_io_resources[] = {
	{ "dma1", 0x00, 0x1f },
	{ "pic1", 0x20, 0x3f },
	{ "timer", 0x40, 0x5f },
	{ "keyboard", 0x60, 0x6f },
	{ "dma page reg", 0x80, 0x8f },
	{ "pic2", 0xa0, 0xbf },
	{ "dma2", 0xc0, 0xdf },
	{ "fpu", 0xf0, 0xff }
};

#define STANDARD_IO_RESOURCES (sizeof(standard_io_resources)/sizeof(struct resource))

/* System RAM - interrupted by the 640kB-1M hole */
#define code_resource (ram_resources[3])
#define data_resource (ram_resources[4])
static struct resource ram_resources[] = {
	{ "System RAM", 0x000000, 0x09ffff, IORESOURCE_BUSY },
	{ "System RAM", 0x100000, 0x100000, IORESOURCE_BUSY },
	{ "Video RAM area", 0x0a0000, 0x0bffff },
	{ "Kernel code", 0x100000, 0 },
	{ "Kernel data", 0, 0 }
};

unsigned long memory_start, memory_end;

static inline void parse_cmdline (char ** cmdline_p, char mv_name[MV_NAME_SIZE],
				  struct sh_machine_vector** mvp,
				  unsigned long *mv_io_base,
				  int *mv_mmio_enable)
{
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	memory_start = (unsigned long)PAGE_OFFSET+__MEMORY_START;
	memory_end = memory_start + __MEMORY_SIZE;

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" defines a size of memory.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			{
				unsigned long mem_size;

				mem_size = memparse(from+4, &from);
				memory_end = memory_start + mem_size;
			}
		}
		if (c == ' ' && !memcmp(from, "sh_mv=", 6)) {
			char* mv_end;
			char* mv_comma;
			int mv_len;
			if (to != command_line)
				to--;
			from += 6;
			mv_end = strchr(from, ' ');
			if (mv_end == NULL)
				mv_end = from + strlen(from);

			mv_comma = strchr(from, ',');
			if ((mv_comma != NULL) && (mv_comma < mv_end)) {
				int ints[3];
				get_options(mv_comma+1, ARRAY_SIZE(ints), ints);
				*mv_io_base = ints[1];
				*mv_mmio_enable = ints[2];
				mv_len = mv_comma - from;
			} else {
				mv_len = mv_end - from;
			}
			if (mv_len > (MV_NAME_SIZE-1))
				mv_len = MV_NAME_SIZE-1;
			memcpy(mv_name, from, mv_len);
			mv_name[mv_len] = '\0';
			from = mv_end;

			*mvp = get_mv_byname(mv_name);
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

static int __init sh_mv_setup(char **cmdline_p)
{
#ifdef CONFIG_SH_UNKNOWN
	extern struct sh_machine_vector mv_unknown;
#endif
	struct sh_machine_vector *mv = NULL;
	char mv_name[MV_NAME_SIZE] = "";
	unsigned long mv_io_base = 0;
	int mv_mmio_enable = 0;

	parse_cmdline(cmdline_p, mv_name, &mv, &mv_io_base, &mv_mmio_enable);

#ifdef CONFIG_SH_UNKNOWN
	if (mv == NULL) {
		mv = &mv_unknown;
		if (*mv_name != '\0') {
			printk("Warning: Unsupported machine %s, using unknown\n",
			       mv_name);
		}
	}
	sh_mv = *mv;
#endif

	/*
	 * Manually walk the vec, fill in anything that the board hasn't yet
	 * by hand, wrapping to the generic implementation.
	 */
#define mv_set(elem) do { \
	if (!sh_mv.mv_##elem) \
		sh_mv.mv_##elem = generic_##elem; \
} while (0)

	mv_set(inb);	mv_set(inw);	mv_set(inl);
	mv_set(outb);	mv_set(outw);	mv_set(outl);

	mv_set(inb_p);	mv_set(inw_p);	mv_set(inl_p);
	mv_set(outb_p);	mv_set(outw_p);	mv_set(outl_p);

	mv_set(insb);	mv_set(insw);	mv_set(insl);
	mv_set(outsb);	mv_set(outsw);	mv_set(outsl);

	mv_set(readb);	mv_set(readw);	mv_set(readl);
	mv_set(writeb);	mv_set(writew);	mv_set(writel);

	mv_set(ioport_map);
	mv_set(ioport_unmap);
	mv_set(irq_demux);

#ifdef CONFIG_SH_UNKNOWN
	__set_io_port_base(mv_io_base);
#endif

	return 0;
}

void __init setup_arch(char **cmdline_p)
{
	unsigned long bootmap_size;
	unsigned long start_pfn, max_pfn, max_low_pfn;

#ifdef CONFIG_EARLY_PRINTK
	extern void enable_early_printk(void);

	enable_early_printk();
#endif
#ifdef CONFIG_CMDLINE_BOOL
        strcpy(COMMAND_LINE, CONFIG_CMDLINE);
#endif

	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);

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

	code_resource.start = (unsigned long)virt_to_phys(_text);
	code_resource.end = (unsigned long)virt_to_phys(_etext)-1;
	data_resource.start = (unsigned long)virt_to_phys(_etext);
	data_resource.end = (unsigned long)virt_to_phys(_edata)-1;

	sh_mv_setup(cmdline_p);

	/*
	 * Find the highest page frame number we have available
	 */
	max_pfn = PFN_DOWN(__pa(memory_end));

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn;

	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(_end));

	/*
	 * Find a proper area for the bootmem bitmap. After this
	 * bootstrap step all allocations (until the page allocator
	 * is intact) must be done via bootmem_alloc().
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), start_pfn,
					 __MEMORY_START>>PAGE_SHIFT,
					 max_low_pfn);
	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
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
		free_bootmem_node(NODE_DATA(0), PFN_PHYS(curr_pfn),
				  PFN_PHYS(pages));
	}

	/*
	 * Reserve the kernel text and
	 * Reserve the bootmem bitmap. We do this in two steps (first step
	 * was init_bootmem()), because this catches the (definitely buggy)
	 * case of us accidentally initializing the bootmem allocator with
	 * an invalid RAM area.
	 */
	reserve_bootmem_node(NODE_DATA(0), __MEMORY_START+PAGE_SIZE,
		(PFN_PHYS(start_pfn)+bootmap_size+PAGE_SIZE-1)-__MEMORY_START);

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem_node(NODE_DATA(0), __MEMORY_START, PAGE_SIZE);

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	if (&__rd_start != &__rd_end) {
		LOADER_TYPE = 1;
		INITRD_START = PHYSADDR((unsigned long)&__rd_start) - __MEMORY_START;
		INITRD_SIZE = (unsigned long)&__rd_end - (unsigned long)&__rd_start;
	}

	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem_node(NODE_DATA(0), INITRD_START+__MEMORY_START, INITRD_SIZE);
			initrd_start =
				INITRD_START ? INITRD_START + PAGE_OFFSET + __MEMORY_START : 0;
			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				    INITRD_START + INITRD_SIZE,
				    max_low_pfn << PAGE_SHIFT);
			initrd_start = 0;
		}
	}
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	/* Perform the machine specific initialisation */
	platform_setup();

	paging_init();
}

struct sh_machine_vector* __init get_mv_byname(const char* name)
{
	extern int strcasecmp(const char *, const char *);
	extern long __machvec_start, __machvec_end;
	struct sh_machine_vector *all_vecs =
		(struct sh_machine_vector *)&__machvec_start;

	int i, n = ((unsigned long)&__machvec_end
		    - (unsigned long)&__machvec_start)/
		sizeof(struct sh_machine_vector);

	for (i = 0; i < n; ++i) {
		struct sh_machine_vector *mv = &all_vecs[i];
		if (mv == NULL)
			continue;
		if (strcasecmp(name, get_system_type()) == 0) {
			return mv;
		}
	}
	return NULL;
}

static struct cpu cpu[NR_CPUS];

static int __init topology_init(void)
{
	int cpu_id;

	for_each_cpu(cpu_id)
		register_cpu(&cpu[cpu_id], cpu_id, NULL);

	return 0;
}

subsys_initcall(topology_init);

static const char *cpu_name[] = {
	[CPU_SH7604]	= "SH7604",
	[CPU_SH7705]	= "SH7705",
	[CPU_SH7708]	= "SH7708",
	[CPU_SH7729]	= "SH7729",
	[CPU_SH7300]	= "SH7300",
	[CPU_SH7750]	= "SH7750",
	[CPU_SH7750S]	= "SH7750S",
	[CPU_SH7750R]	= "SH7750R",
	[CPU_SH7751]	= "SH7751",
	[CPU_SH7751R]	= "SH7751R",
	[CPU_SH7760]	= "SH7760",
	[CPU_SH73180]	= "SH73180",
	[CPU_ST40RA]	= "ST40RA",
	[CPU_ST40GX1]	= "ST40GX1",
	[CPU_SH4_202]	= "SH4-202",
	[CPU_SH4_501]	= "SH4-501",
	[CPU_SH7770]	= "SH7770",
	[CPU_SH7780]	= "SH7780",
	[CPU_SH7781]	= "SH7781",
	[CPU_SH_NONE]	= "Unknown"
};

const char *get_cpu_subtype(void)
{
	return cpu_name[boot_cpu_data.type];
}

#ifdef CONFIG_PROC_FS
static const char *cpu_flags[] = {
	"none", "fpu", "p2flush", "mmuassoc", "dsp", "perfctr", "ptea", NULL
};

static void show_cpuflags(struct seq_file *m)
{
	unsigned long i;

	seq_printf(m, "cpu flags\t:");

	if (!cpu_data->flags) {
		seq_printf(m, " %s\n", cpu_flags[0]);
		return;
	}

	for (i = 0; cpu_flags[i]; i++)
		if ((cpu_data->flags & (1 << i)))
			seq_printf(m, " %s", cpu_flags[i+1]);

	seq_printf(m, "\n");
}

static void show_cacheinfo(struct seq_file *m, const char *type, struct cache_info info)
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
	unsigned int cpu = smp_processor_id();

	if (!cpu && cpu_online(cpu))
		seq_printf(m, "machine\t\t: %s\n", get_system_type());

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "cpu family\t: %s\n", system_utsname.machine);
	seq_printf(m, "cpu type\t: %s\n", get_cpu_subtype());

	show_cpuflags(m);

	seq_printf(m, "cache type\t: ");

	/*
	 * Check for what type of cache we have, we support both the
	 * unified cache on the SH-2 and SH-3, as well as the harvard
	 * style cache on the SH-4.
	 */
	if (test_bit(SH_CACHE_COMBINED, &(boot_cpu_data.icache.flags))) {
		seq_printf(m, "unified\n");
		show_cacheinfo(m, "cache", boot_cpu_data.icache);
	} else {
		seq_printf(m, "split (harvard)\n");
		show_cacheinfo(m, "icache", boot_cpu_data.icache);
		show_cacheinfo(m, "dcache", boot_cpu_data.dcache);
	}

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		     boot_cpu_data.loops_per_jiffy/(500000/HZ),
		     (boot_cpu_data.loops_per_jiffy/(5000/HZ)) % 100);

	return show_clocks(m);
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
struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_SH_KGDB
/*
 * Parse command-line kgdb options.  By default KGDB is enabled,
 * entered on error (or other action) using default serial info.
 * The command-line option can include a serial port specification
 * and an action to override default or configured behavior.
 */
struct kgdb_sermap kgdb_sci_sermap =
{ "ttySC", 5, kgdb_sci_setup, NULL };

struct kgdb_sermap *kgdb_serlist = &kgdb_sci_sermap;
struct kgdb_sermap *kgdb_porttype = &kgdb_sci_sermap;

void kgdb_register_sermap(struct kgdb_sermap *map)
{
	struct kgdb_sermap *last;

	for (last = kgdb_serlist; last->next; last = last->next)
		;
	last->next = map;
	if (!map->namelen) {
		map->namelen = strlen(map->name);
	}
}

static int __init kgdb_parse_options(char *options)
{
	char c;
	int baud;

	/* Check for port spec (or use default) */

	/* Determine port type and instance */
	if (!memcmp(options, "tty", 3)) {
		struct kgdb_sermap *map = kgdb_serlist;

		while (map && memcmp(options, map->name, map->namelen))
			map = map->next;

		if (!map) {
			KGDB_PRINTK("unknown port spec in %s\n", options);
			return -1;
		}

		kgdb_porttype = map;
		kgdb_serial_setup = map->setup_fn;
		kgdb_portnum = options[map->namelen] - '0';
		options += map->namelen + 1;

		options = (*options == ',') ? options+1 : options;

		/* Read optional parameters (baud/parity/bits) */
		baud = simple_strtoul(options, &options, 10);
		if (baud != 0) {
			kgdb_baud = baud;

			c = toupper(*options);
			if (c == 'E' || c == 'O' || c == 'N') {
				kgdb_parity = c;
				options++;
			}

			c = *options;
			if (c == '7' || c == '8') {
				kgdb_bits = c;
				options++;
			}
			options = (*options == ',') ? options+1 : options;
		}
	}

	/* Check for action specification */
	if (!memcmp(options, "halt", 4)) {
		kgdb_halt = 1;
		options += 4;
	} else if (!memcmp(options, "disabled", 8)) {
		kgdb_enabled = 0;
		options += 8;
	}

	if (*options) {
                KGDB_PRINTK("ignored unknown options: %s\n", options);
		return 0;
	}
	return 1;
}
__setup("kgdb=", kgdb_parse_options);
#endif /* CONFIG_SH_KGDB */

