/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/kernel/setup.c
 *
 * sh64 Arch Support
 *
 * This file handles the architecture-dependent parts of initialization
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * benedict.gaster@superh.com:   2nd May 2002
 *    Modified to use the empty_zero_page to pass command line arguments.
 *
 * benedict.gaster@superh.com:	 3rd May 2002
 *    Added support for ramdisk, removing statically linked romfs at the same time.
 *
 * lethal@linux-sh.org:          15th May 2003
 *    Added generic procfs cpuinfo reporting. Make boards just export their name.
 *
 * lethal@linux-sh.org:          25th May 2003
 *    Added generic get_cpu_subtype() for subtype reporting from cpu_data->type.
 *
 */
#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/initrd.h>
#include <linux/pfn.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/platform.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp.h>

#ifdef CONFIG_VT
#include <linux/console.h>
#endif

struct screen_info screen_info;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;
extern char *get_system_type(void);
extern void platform_setup(void);
extern void platform_monitor(void);
extern void platform_reserve(void);
extern int sh64_cache_init(void);
extern int sh64_tlb_init(void);

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char command_line[COMMAND_LINE_SIZE] = { 0, };
unsigned long long memory_start = CONFIG_MEMORY_START;
unsigned long long memory_end = CONFIG_MEMORY_START + (CONFIG_MEMORY_SIZE_IN_MB * 1024 * 1024);

struct sh_cpuinfo boot_cpu_data;

static inline void parse_mem_cmdline (char ** cmdline_p)
{
        char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

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

static void __init sh64_cpu_type_detect(void)
{
	extern unsigned long long peek_real_address_q(unsigned long long addr);
	unsigned long long cir;
	/* Do peeks in real mode to avoid having to set up a mapping for the
	   WPC registers.  On SH5-101 cut2, such a mapping would be exposed to
	   an address translation erratum which would make it hard to set up
	   correctly. */
	cir = peek_real_address_q(0x0d000008);

	if ((cir & 0xffff) == 0x5103) {
		boot_cpu_data.type = CPU_SH5_103;
	} else if (((cir >> 32) & 0xffff) == 0x51e2) {
		/* CPU.VCR aliased at CIR address on SH5-101 */
		boot_cpu_data.type = CPU_SH5_101;
	} else {
		boot_cpu_data.type = CPU_SH_NONE;
	}
}

void __init setup_arch(char **cmdline_p)
{
	unsigned long bootmap_size, i;
	unsigned long first_pfn, start_pfn, last_pfn, pages;

#ifdef CONFIG_EARLY_PRINTK
	extern void enable_early_printk(void);

	/*
	 * Setup Early SCIF console
	 */
	enable_early_printk();
#endif

	/*
	 * Setup TLB mappings
	 */
	sh64_tlb_init();

	/*
	 * Caches are already initialized by the time we get here, so we just
	 * fill in cpu_data info for the caches.
	 */
	sh64_cache_init();

	platform_setup();
	platform_monitor();

	sh64_cpu_type_detect();

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

	code_resource.start = __pa(_text);
	code_resource.end = __pa(_etext)-1;
	data_resource.start = __pa(_etext);
	data_resource.end = __pa(_edata)-1;

	parse_mem_cmdline(cmdline_p);

	/*
	 * Find the lowest and highest page frame numbers we have available
	 */
	first_pfn = PFN_DOWN(memory_start);
	last_pfn = PFN_DOWN(memory_end);
	pages = last_pfn - first_pfn;

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
					 first_pfn,
					 last_pfn);
        /*
         * Round it up.
         */
        bootmap_size = PFN_PHYS(PFN_UP(bootmap_size));

	/*
	 * Register fully available RAM pages with the bootmem allocator.
	 */
	free_bootmem_node(NODE_DATA(0), PFN_PHYS(first_pfn), PFN_PHYS(pages));

	/*
	 * Reserve all kernel sections + bootmem bitmap + a guard page.
	 */
	reserve_bootmem_node(NODE_DATA(0), PFN_PHYS(first_pfn),
		        (PFN_PHYS(start_pfn) + bootmap_size + PAGE_SIZE) - PFN_PHYS(first_pfn));

	/*
	 * Reserve platform dependent sections
	 */
	platform_reserve();

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (PFN_PHYS(last_pfn))) {
		        reserve_bootmem_node(NODE_DATA(0), INITRD_START + __MEMORY_START, INITRD_SIZE);

			initrd_start =
			  (long) INITRD_START ? INITRD_START + PAGE_OFFSET +  __MEMORY_START : 0;

			initrd_end = initrd_start + INITRD_SIZE;
		} else {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				    (long) INITRD_START + INITRD_SIZE,
				    PFN_PHYS(last_pfn));
			initrd_start = 0;
		}
	}
#endif

	/*
	 * Claim all RAM, ROM, and I/O resources.
	 */

	/* Kernel RAM */
	request_resource(&iomem_resource, &code_resource);
	request_resource(&iomem_resource, &data_resource);

	/* Other KRAM space */
	for (i = 0; i < STANDARD_KRAM_RESOURCES - 2; i++)
		request_resource(&iomem_resource,
				 &platform_parms.kram_res_p[i]);

	/* XRAM space */
	for (i = 0; i < STANDARD_XRAM_RESOURCES; i++)
		request_resource(&iomem_resource,
				 &platform_parms.xram_res_p[i]);

	/* ROM space */
	for (i = 0; i < STANDARD_ROM_RESOURCES; i++)
		request_resource(&iomem_resource,
				 &platform_parms.rom_res_p[i]);

	/* I/O space */
	for (i = 0; i < STANDARD_IO_RESOURCES; i++)
		request_resource(&ioport_resource,
				 &platform_parms.io_res_p[i]);


#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	printk("Hardware FPU: %s\n", fpu_in_use ? "enabled" : "disabled");

	paging_init();
}

void __xchg_called_with_bad_pointer(void)
{
	printk(KERN_EMERG "xchg() called with bad pointer !\n");
}

static struct cpu cpu[1];

static int __init topology_init(void)
{
	return register_cpu(cpu, 0, NULL);
}

subsys_initcall(topology_init);

/*
 *	Get CPU information
 */
static const char *cpu_name[] = {
	[CPU_SH5_101]	= "SH5-101",
	[CPU_SH5_103]	= "SH5-103",
	[CPU_SH_NONE]	= "Unknown",
};

const char *get_cpu_subtype(void)
{
	return cpu_name[boot_cpu_data.type];
}

#ifdef CONFIG_PROC_FS
static int show_cpuinfo(struct seq_file *m,void *v)
{
	unsigned int cpu = smp_processor_id();

	if (!cpu)
		seq_printf(m, "machine\t\t: %s\n", get_system_type());

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "cpu family\t: SH-5\n");
	seq_printf(m, "cpu type\t: %s\n", get_cpu_subtype());

	seq_printf(m, "icache size\t: %dK-bytes\n",
		   (boot_cpu_data.icache.ways *
		    boot_cpu_data.icache.sets *
		    boot_cpu_data.icache.linesz) >> 10);
	seq_printf(m, "dcache size\t: %dK-bytes\n",
		   (boot_cpu_data.dcache.ways *
		    boot_cpu_data.dcache.sets *
		    boot_cpu_data.dcache.linesz) >> 10);
	seq_printf(m, "itlb entries\t: %d\n", boot_cpu_data.itlb.entries);
	seq_printf(m, "dtlb entries\t: %d\n", boot_cpu_data.dtlb.entries);

#define PRINT_CLOCK(name, value) \
	seq_printf(m, name " clock\t: %d.%02dMHz\n", \
		     ((value) / 1000000), ((value) % 1000000)/10000)

	PRINT_CLOCK("cpu", boot_cpu_data.cpu_clock);
	PRINT_CLOCK("bus", boot_cpu_data.bus_clock);
	PRINT_CLOCK("module", boot_cpu_data.module_clock);

        seq_printf(m, "bogomips\t: %lu.%02lu\n\n",
		     (loops_per_jiffy*HZ+2500)/500000,
		     ((loops_per_jiffy*HZ+2500)/5000) % 100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return (void*)(*pos == 0);
}
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
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
