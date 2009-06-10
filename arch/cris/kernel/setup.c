/*
 *
 *  linux/arch/cris/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (c) 2001  Axis Communications AB
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <asm/pgtable.h>
#include <linux/seq_file.h>
#include <linux/screen_info.h>
#include <linux/utsname.h>
#include <linux/pfn.h>
#include <linux/cpu.h>
#include <asm/setup.h>

/*
 * Setup options
 */
struct screen_info screen_info;

extern int root_mountflags;
extern char _etext, _edata, _end;

char __initdata cris_command_line[COMMAND_LINE_SIZE] = { 0, };

extern const unsigned long text_start, edata; /* set by the linker script */
extern unsigned long dram_start, dram_end;

extern unsigned long romfs_start, romfs_length, romfs_in_flash; /* from head.S */

static struct cpu cpu_devices[NR_CPUS];

extern void show_etrax_copyright(void);		/* arch-vX/kernel/setup.c */

/* This mainly sets up the memory area, and can be really confusing.
 *
 * The physical DRAM is virtually mapped into dram_start to dram_end
 * (usually c0000000 to c0000000 + DRAM size). The physical address is
 * given by the macro __pa().
 *
 * In this DRAM, the kernel code and data is loaded, in the beginning.
 * It really starts at c0004000 to make room for some special pages -
 * the start address is text_start. The kernel data ends at _end. After
 * this the ROM filesystem is appended (if there is any).
 *
 * Between this address and dram_end, we have RAM pages usable to the
 * boot code and the system.
 *
 */

void __init setup_arch(char **cmdline_p)
{
	extern void init_etrax_debug(void);
	unsigned long bootmap_size;
	unsigned long start_pfn, max_pfn;
	unsigned long memory_start;

	/* register an initial console printing routine for printk's */

	init_etrax_debug();

	/* we should really poll for DRAM size! */

	high_memory = &dram_end;

	if(romfs_in_flash || !romfs_length) {
		/* if we have the romfs in flash, or if there is no rom filesystem,
		 * our free area starts directly after the BSS
		 */
		memory_start = (unsigned long) &_end;
	} else {
		/* otherwise the free area starts after the ROM filesystem */
		printk("ROM fs in RAM, size %lu bytes\n", romfs_length);
		memory_start = romfs_start + romfs_length;
	}

	/* process 1's initial memory region is the kernel code/data */

	init_mm.start_code = (unsigned long) &text_start;
	init_mm.end_code =   (unsigned long) &_etext;
	init_mm.end_data =   (unsigned long) &_edata;
	init_mm.brk =        (unsigned long) &_end;

	/* min_low_pfn points to the start of DRAM, start_pfn points
	 * to the first DRAM pages after the kernel, and max_low_pfn
	 * to the end of DRAM.
	 */

        /*
         * partially used pages are not usable - thus
         * we are rounding upwards:
         */

        start_pfn = PFN_UP(memory_start);  /* usually c0000000 + kernel + romfs */
	max_pfn =   PFN_DOWN((unsigned long)high_memory); /* usually c0000000 + dram size */

        /*
         * Initialize the boot-time allocator (start, end)
	 *
	 * We give it access to all our DRAM, but we could as well just have
	 * given it a small slice. No point in doing that though, unless we
	 * have non-contiguous memory and want the boot-stuff to be in, say,
	 * the smallest area.
	 *
	 * It will put a bitmap of the allocated pages in the beginning
	 * of the range we give it, but it won't mark the bitmaps pages
	 * as reserved. We have to do that ourselves below.
	 *
	 * We need to use init_bootmem_node instead of init_bootmem
	 * because our map starts at a quite high address (min_low_pfn).
         */

	max_low_pfn = max_pfn;
	min_low_pfn = PAGE_OFFSET >> PAGE_SHIFT;

	bootmap_size = init_bootmem_node(NODE_DATA(0), start_pfn,
					 min_low_pfn,
					 max_low_pfn);

	/* And free all memory not belonging to the kernel (addr, size) */

	free_bootmem(PFN_PHYS(start_pfn), PFN_PHYS(max_pfn - start_pfn));

        /*
         * Reserve the bootmem bitmap itself as well. We do this in two
         * steps (first step was init_bootmem()) because this catches
         * the (very unlikely) case of us accidentally initializing the
         * bootmem allocator with an invalid RAM area.
	 *
	 * Arguments are start, size
         */

	reserve_bootmem(PFN_PHYS(start_pfn), bootmap_size, BOOTMEM_DEFAULT);

	/* paging_init() sets up the MMU and marks all pages as reserved */

	paging_init();

	*cmdline_p = cris_command_line;

#ifdef CONFIG_ETRAX_CMDLINE
        if (!strcmp(cris_command_line, "")) {
		strlcpy(cris_command_line, CONFIG_ETRAX_CMDLINE, COMMAND_LINE_SIZE);
		cris_command_line[COMMAND_LINE_SIZE - 1] = '\0';
	}
#endif

	/* Save command line for future references. */
	memcpy(boot_command_line, cris_command_line, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE - 1] = '\0';

	/* give credit for the CRIS port */
	show_etrax_copyright();

	/* Setup utsname */
	strcpy(init_utsname()->machine, cris_machine_name);
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? (void *)(int)(*pos + 1) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

extern int show_cpuinfo(struct seq_file *m, void *v);

const struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next  = c_next,
	.stop  = c_stop,
	.show  = show_cpuinfo,
};

static int __init topology_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		 return register_cpu(&cpu_devices[i], i);
	}

	return 0;
}

subsys_initcall(topology_init);

