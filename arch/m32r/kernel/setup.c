/*
 *  linux/arch/m32r/kernel/setup.c
 *
 *  Setup routines for Renesas M32R
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/fs.h>
#include <linux/sched/mm.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/seq_file.h>
#include <linux/timex.h>
#include <linux/screen_info.h>
#include <linux/cpu.h>
#include <linux/nodemask.h>
#include <linux/pfn.h>

#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/m32r.h>
#include <asm/setup.h>
#include <asm/sections.h>

#ifdef CONFIG_MMU
extern void init_mmu(void);
#endif

extern char _end[];

/*
 * Machine setup..
 */
struct cpuinfo_m32r boot_cpu_data;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;	/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;	/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

#if defined(CONFIG_VGA_CONSOLE)
struct screen_info screen_info = {
	.orig_video_lines      = 25,
	.orig_video_cols       = 80,
	.orig_video_mode       = 0,
	.orig_video_ega_bx     = 0,
	.orig_video_isVGA      = 1,
	.orig_video_points     = 8
};
#endif

extern int root_mountflags;

static char __initdata command_line[COMMAND_LINE_SIZE];

static struct resource data_resource = {
	.name   = "Kernel data",
	.start  = 0,
	.end    = 0,
	.flags  = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

static struct resource code_resource = {
	.name   = "Kernel code",
	.start  = 0,
	.end    = 0,
	.flags  = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM
};

unsigned long memory_start;
EXPORT_SYMBOL(memory_start);

unsigned long memory_end;
EXPORT_SYMBOL(memory_end);

void __init setup_arch(char **);
int get_cpuinfo(char *);

static __inline__ void parse_mem_cmdline(char ** cmdline_p)
{
	char c = ' ';
	char *to = command_line;
	char *from = COMMAND_LINE;
	int len = 0;
	int usermem = 0;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(boot_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE-1] = '\0';

	memory_start = (unsigned long)CONFIG_MEMORY_START+PAGE_OFFSET;
	memory_end = memory_start+(unsigned long)CONFIG_MEMORY_SIZE;

	for ( ; ; ) {
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;

			{
				unsigned long mem_size;

				usermem = 1;
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
	if (usermem)
		printk(KERN_INFO "user-defined physical RAM map:\n");
}

#ifndef CONFIG_DISCONTIGMEM
static unsigned long __init setup_memory(void)
{
	unsigned long start_pfn, max_low_pfn, bootmap_size;

	start_pfn = PFN_UP( __pa(_end) );
	max_low_pfn = PFN_DOWN( __pa(memory_end) );

	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = init_bootmem_node(NODE_DATA(0), start_pfn,
		CONFIG_MEMORY_START>>PAGE_SHIFT, max_low_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	{
		unsigned long curr_pfn;
		unsigned long last_pfn;
		unsigned long pages;

		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(__pa(memory_start));

		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(__pa(memory_end));

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		pages = last_pfn - curr_pfn;
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(pages));
	}

	/*
	 * Reserve the kernel text and
	 * Reserve the bootmem bitmap. We do this in two steps (first step
	 * was init_bootmem()), because this catches the (definitely buggy)
	 * case of us accidentally initializing the bootmem allocator with
	 * an invalid RAM area.
	 */
	reserve_bootmem(CONFIG_MEMORY_START + PAGE_SIZE,
		(PFN_PHYS(start_pfn) + bootmap_size + PAGE_SIZE - 1)
		- CONFIG_MEMORY_START,
		BOOTMEM_DEFAULT);

	/*
	 * reserve physical page 0 - it's a special BIOS page on many boxes,
	 * enabling clean reboots, SMP operation, laptop functions.
	 */
	reserve_bootmem(CONFIG_MEMORY_START, PAGE_SIZE, BOOTMEM_DEFAULT);

	/*
	 * reserve memory hole
	 */
#ifdef CONFIG_MEMHOLE
	reserve_bootmem(CONFIG_MEMHOLE_START, CONFIG_MEMHOLE_SIZE,
			BOOTMEM_DEFAULT);
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE && INITRD_START) {
		if (INITRD_START + INITRD_SIZE <= (max_low_pfn << PAGE_SHIFT)) {
			reserve_bootmem(INITRD_START, INITRD_SIZE,
					BOOTMEM_DEFAULT);
			initrd_start = INITRD_START + PAGE_OFFSET;
			initrd_end = initrd_start + INITRD_SIZE;
			printk("initrd:start[%08lx],size[%08lx]\n",
				initrd_start, INITRD_SIZE);
		} else {
			printk("initrd extends beyond end of memory "
				"(0x%08lx > 0x%08lx)\ndisabling initrd\n",
				INITRD_START + INITRD_SIZE,
				max_low_pfn << PAGE_SHIFT);

			initrd_start = 0;
		}
	}
#endif

	return max_low_pfn;
}
#else	/* CONFIG_DISCONTIGMEM */
extern unsigned long setup_memory(void);
#endif	/* CONFIG_DISCONTIGMEM */

void __init setup_arch(char **cmdline_p)
{
	ROOT_DEV = old_decode_dev(ORIG_ROOT_DEV);

	boot_cpu_data.cpu_clock = M32R_CPUCLK;
	boot_cpu_data.bus_clock = M32R_BUSCLK;
	boot_cpu_data.timer_divide = M32R_TIMER_DIVIDE;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif

	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

#ifdef CONFIG_DISCONTIGMEM
	nodes_clear(node_online_map);
	node_set_online(0);
	node_set_online(1);
#endif	/* CONFIG_DISCONTIGMEM */

	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) _end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;

	parse_mem_cmdline(cmdline_p);

	setup_memory();

	paging_init();
}

static struct cpu cpu_devices[NR_CPUS];

static int __init topology_init(void)
{
	int i;

	for_each_present_cpu(i)
		register_cpu(&cpu_devices[i], i);

	return 0;
}

subsys_initcall(topology_init);

#ifdef CONFIG_PROC_FS
/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct cpuinfo_m32r *c = v;
	unsigned long cpu = c - cpu_data;

#ifdef CONFIG_SMP
	if (!cpu_online(cpu))
		return 0;
#endif	/* CONFIG_SMP */

	seq_printf(m, "processor\t: %ld\n", cpu);

#if defined(CONFIG_CHIP_VDEC2)
	seq_printf(m, "cpu family\t: VDEC2\n"
		"cache size\t: Unknown\n");
#elif defined(CONFIG_CHIP_M32700)
	seq_printf(m,"cpu family\t: M32700\n"
		"cache size\t: I-8KB/D-8KB\n");
#elif defined(CONFIG_CHIP_M32102)
	seq_printf(m,"cpu family\t: M32102\n"
		"cache size\t: I-8KB\n");
#elif defined(CONFIG_CHIP_OPSP)
	seq_printf(m,"cpu family\t: OPSP\n"
		"cache size\t: I-8KB/D-8KB\n");
#elif defined(CONFIG_CHIP_MP)
	seq_printf(m, "cpu family\t: M32R-MP\n"
		"cache size\t: I-xxKB/D-xxKB\n");
#elif  defined(CONFIG_CHIP_M32104)
	seq_printf(m,"cpu family\t: M32104\n"
		"cache size\t: I-8KB/D-8KB\n");
#else
	seq_printf(m, "cpu family\t: Unknown\n");
#endif
	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		c->loops_per_jiffy/(500000/HZ),
		(c->loops_per_jiffy/(5000/HZ)) % 100);
#if defined(CONFIG_PLAT_MAPPI)
	seq_printf(m, "Machine\t\t: Mappi Evaluation board\n");
#elif defined(CONFIG_PLAT_MAPPI2)
	seq_printf(m, "Machine\t\t: Mappi-II Evaluation board\n");
#elif defined(CONFIG_PLAT_MAPPI3)
	seq_printf(m, "Machine\t\t: Mappi-III Evaluation board\n");
#elif defined(CONFIG_PLAT_M32700UT)
	seq_printf(m, "Machine\t\t: M32700UT Evaluation board\n");
#elif defined(CONFIG_PLAT_OPSPUT)
	seq_printf(m, "Machine\t\t: OPSPUT Evaluation board\n");
#elif defined(CONFIG_PLAT_USRV)
	seq_printf(m, "Machine\t\t: uServer\n");
#elif defined(CONFIG_PLAT_OAKS32R)
	seq_printf(m, "Machine\t\t: OAKS32R\n");
#elif  defined(CONFIG_PLAT_M32104UT)
	seq_printf(m, "Machine\t\t: M3T-M32104UT uT Engine board\n");
#else
	seq_printf(m, "Machine\t\t: Unknown\n");
#endif

#define PRINT_CLOCK(name, value)				\
	seq_printf(m, name " clock\t: %d.%02dMHz\n",		\
		((value) / 1000000), ((value) % 1000000)/10000)

	PRINT_CLOCK("CPU", (int)c->cpu_clock);
	PRINT_CLOCK("Bus", (int)c->bus_clock);

	seq_printf(m, "\n");

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
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};
#endif	/* CONFIG_PROC_FS */

unsigned long cpu_initialized __initdata = 0;

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process.
 * We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 */
#if defined(CONFIG_CHIP_VDEC2) || defined(CONFIG_CHIP_XNUX2)	\
	|| defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_M32102) \
	|| defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_M32104)
void __init cpu_init (void)
{
	int cpu_id = smp_processor_id();

	if (test_and_set_bit(cpu_id, &cpu_initialized)) {
		printk(KERN_WARNING "CPU#%d already initialized!\n", cpu_id);
		for ( ; ; )
			local_irq_enable();
	}
	printk(KERN_INFO "Initializing CPU#%d\n", cpu_id);

	/* Set up and load the per-CPU TSS and LDT */
	mmgrab(&init_mm);
	current->active_mm = &init_mm;
	if (current->mm)
		BUG();

	/* Force FPU initialization */
	current_thread_info()->status = 0;
	clear_used_math();

#ifdef CONFIG_MMU
	/* Set up MMU */
	init_mmu();
#endif

	/* Set up ICUIMASK */
	outl(0x00070000, M32R_ICU_IMASK_PORTL);		/* imask=111 */
}
#endif	/* defined(CONFIG_CHIP_VDEC2) ... */
