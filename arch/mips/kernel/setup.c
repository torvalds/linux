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
 * Copyright (C) 2000 2001, 2002  Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/screen_info.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/console.h>
#include <linux/pfn.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/system.h>

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
unsigned long mips_machgroup __read_mostly = MACH_GROUP_UNKNOWN;

EXPORT_SYMBOL(mips_machtype);
EXPORT_SYMBOL(mips_machgroup);

struct boot_mem_map boot_mem_map;

static char command_line[CL_SIZE];
       char arcs_cmdline[CL_SIZE]=CONFIG_CMDLINE;

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
const unsigned long mips_io_port_base __read_mostly = -1;
EXPORT_SYMBOL(mips_io_port_base);

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;
EXPORT_SYMBOL(isa_slot_offset);

static struct resource code_resource = { .name = "Kernel code", };
static struct resource data_resource = { .name = "Kernel data", };

void __init add_memory_region(phys_t start, phys_t size, long type)
{
	int x = boot_mem_map.nr_map;
	struct boot_mem_map_entry *prev = boot_mem_map.map + x - 1;

	/* Sanity check */
	if (start + size < start) {
		printk("Trying to add an invalid memory region, skipped\n");
		return;
	}

	/*
	 * Try to merge with previous entry if any.  This is far less than
	 * perfect but is sufficient for most real world cases.
	 */
	if (x && prev->addr + prev->size == start && prev->type == type) {
		prev->size += size;
		return;
	}

	if (x == BOOT_MEM_MAP_MAX) {
		printk("Ooops! Too many entries in the memory map!\n");
		return;
	}

	boot_mem_map.map[x].addr = start;
	boot_mem_map.map[x].size = size;
	boot_mem_map.map[x].type = type;
	boot_mem_map.nr_map++;
}

static void __init print_memory_map(void)
{
	int i;
	const int field = 2 * sizeof(unsigned long);

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		printk(" memory: %0*Lx @ %0*Lx ",
		       field, (unsigned long long) boot_mem_map.map[i].size,
		       field, (unsigned long long) boot_mem_map.map[i].addr);

		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			printk("(usable)\n");
			break;
		case BOOT_MEM_ROM_DATA:
			printk("(ROM data)\n");
			break;
		case BOOT_MEM_RESERVED:
			printk("(reserved)\n");
			break;
		default:
			printk("type %lu\n", boot_mem_map.map[i].type);
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
	/* HACK: Guess if the sign extension was forgotten */
	if (start > 0x0000000080000000 && start < 0x00000000ffffffff)
		start |= 0xffffffff00000000UL;
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

static unsigned long __init init_initrd(void)
{
	unsigned long tmp, end, size;
	u32 *initrd_header;

	ROOT_DEV = Root_RAM0;

	/*
	 * Board specific code or command line parser should have
	 * already set up initrd_start and initrd_end. In these cases
	 * perfom sanity checks and use them if all looks good.
	 */
	size = initrd_end - initrd_start;
	if (initrd_end == 0 || size == 0) {
		initrd_start = 0;
		initrd_end = 0;
	} else
		return initrd_end;

	end = (unsigned long)&_end;
	tmp = PAGE_ALIGN(end) - sizeof(u32) * 2;
	if (tmp < end)
		tmp += PAGE_SIZE;

	initrd_header = (u32 *)tmp;
	if (initrd_header[0] == 0x494E5244) {
		initrd_start = (unsigned long)&initrd_header[2];
		initrd_end = initrd_start + initrd_header[1];
	}
	return initrd_end;
}

static void __init finalize_initrd(void)
{
	unsigned long size = initrd_end - initrd_start;

	if (size == 0) {
		printk(KERN_INFO "Initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		printk("Initrd extends beyond end of memory");
		goto disable;
	}

	reserve_bootmem(__pa(initrd_start), size);
	initrd_below_start_ok = 1;

	printk(KERN_INFO "Initial ramdisk at: 0x%lx (%lu bytes)\n",
	       initrd_start, size);
	return;
disable:
	printk(" - disabling initrd\n");
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
#ifdef CONFIG_SGI_IP27

static void __init bootmem_init(void)
{
	init_initrd();
	finalize_initrd();
}

#else  /* !CONFIG_SGI_IP27 */

static void __init bootmem_init(void)
{
	unsigned long reserved_end;
	unsigned long highest = 0;
	unsigned long mapstart = -1UL;
	unsigned long bootmap_size;
	int i;

	/*
	 * Init any data related to initrd. It's a nop if INITRD is
	 * not selected. Once that done we can determine the low bound
	 * of usable memory.
	 */
	reserved_end = init_initrd();
	reserved_end = PFN_UP(__pa(max(reserved_end, (unsigned long)&_end)));

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

		if (end > highest)
			highest = end;
		if (end <= reserved_end)
			continue;
		if (start >= mapstart)
			continue;
		mapstart = max(reserved_end, start);
	}

	/*
	 * Determine low and high memory ranges
	 */
	if (highest > PFN_DOWN(HIGHMEM_START)) {
#ifdef CONFIG_HIGHMEM
		highstart_pfn = PFN_DOWN(HIGHMEM_START);
		highend_pfn = highest;
#endif
		highest = PFN_DOWN(HIGHMEM_START);
	}

	/*
	 * Initialize the boot-time allocator with low memory only.
	 */
	bootmap_size = init_bootmem(mapstart, highest);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end, size;

		/*
		 * Reserve usable memory.
		 */
		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end   = PFN_DOWN(boot_mem_map.map[i].addr
				    + boot_mem_map.map[i].size);
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
	reserve_bootmem(PFN_PHYS(mapstart), bootmap_size);

	/*
	 * Reserve initrd memory if needed.
	 */
	finalize_initrd();
}

#endif	/* CONFIG_SGI_IP27 */

/*
 * arch_mem_init - initialize memory managment subsystem
 *
 *  o plat_mem_setup() detects the memory configuration and will record detected
 *    memory areas using add_memory_region.
 *
 * At this stage the memory configuration of the system is known to the
 * kernel but generic memory managment system is still entirely uninitialized.
 *
 *  o bootmem_init()
 *  o sparse_init()
 *  o paging_init()
 *
 * At this stage the bootmem allocator is ready to use.
 *
 * NOTE: historically plat_mem_setup did the entire platform initialization.
 *       This was rather impractical because it meant plat_mem_setup had to
 * get away without any kind of memory allocator.  To keep old code from
 * breaking plat_setup was just renamed to plat_setup and a second platform
 * initialization hook for anything else was introduced.
 */

static int usermem __initdata = 0;

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

static void __init arch_mem_init(char **cmdline_p)
{
	extern void plat_mem_setup(void);

	/* call board setup routine */
	plat_mem_setup();

	printk("Determined physical RAM map:\n");
	print_memory_map();

	strlcpy(command_line, arcs_cmdline, sizeof(command_line));
	strlcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);

	*cmdline_p = command_line;

	parse_early_param();

	if (usermem) {
		printk("User-defined physical RAM map:\n");
		print_memory_map();
	}

	bootmem_init();
	sparse_init();
	paging_init();
}

static void __init resource_init(void)
{
	int i;

	if (UNCAC_BASE != IO_BASE)
		return;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext) - 1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata) - 1;

	/*
	 * Request address space for all standard RAM.
	 */
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
	}
}

void __init setup_arch(char **cmdline_p)
{
	cpu_probe();
	prom_init();
	cpu_report();

#if defined(CONFIG_VT)
#if defined(CONFIG_VGA_CONSOLE)
        conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
        conswitchp = &dummy_con;
#endif
#endif

	arch_mem_init(cmdline_p);

	resource_init();
#ifdef CONFIG_SMP
	plat_smp_setup();
#endif
}

int __init fpu_disable(char *s)
{
	int i;

	for (i = 0; i < NR_CPUS; i++)
		cpu_data[i].options &= ~MIPS_CPU_FPU;

	return 1;
}

__setup("nofpu", fpu_disable);

int __init dsp_disable(char *s)
{
	cpu_data[0].ases &= ~MIPS_ASE_DSP;

	return 1;
}

__setup("nodsp", dsp_disable);
