/*
 * arch/v850/kernel/setup.c -- Arch-dependent initialization functions
 *
 *  Copyright (C) 2001,02,03,05  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>		/* we don't have swap, but for nr_free_pages */
#include <linux/irq.h>
#include <linux/reboot.h>
#include <linux/personality.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/mtd/mtd.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/setup.h>

#include "mach.h"

/* These symbols are all defined in the linker map to delineate various
   statically allocated regions of memory.  */

extern char _intv_start, _intv_end;
/* `kram' is only used if the kernel uses part of normal user RAM.  */
extern char _kram_start __attribute__ ((__weak__));
extern char _kram_end __attribute__ ((__weak__));
extern char _init_start, _init_end;
extern char _bootmap;
extern char _stext, _etext, _sdata, _edata, _sbss, _ebss;
/* Many platforms use an embedded root image.  */
extern char _root_fs_image_start __attribute__ ((__weak__));
extern char _root_fs_image_end __attribute__ ((__weak__));


char command_line[COMMAND_LINE_SIZE];

/* Memory not used by the kernel.  */
static unsigned long total_ram_pages;

/* System RAM.  */
static unsigned long ram_start = 0, ram_len = 0;


#define ADDR_TO_PAGE_UP(x)   ((((unsigned long)x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define ADDR_TO_PAGE(x)	     (((unsigned long)x) >> PAGE_SHIFT)
#define PAGE_TO_ADDR(x)	     (((unsigned long)x) << PAGE_SHIFT)

static void init_mem_alloc (unsigned long ram_start, unsigned long ram_len);

void set_mem_root (void *addr, size_t len, char *cmd_line);


void __init setup_arch (char **cmdline)
{
	/* Keep a copy of command line */
	*cmdline = command_line;
	memcpy (saved_command_line, command_line, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE - 1] = '\0';

	console_verbose ();

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_kram_end;

	/* Find out what mem this machine has.  */
	mach_get_physical_ram (&ram_start, &ram_len);
	/* ... and tell the kernel about it.  */
	init_mem_alloc (ram_start, ram_len);

	printk (KERN_INFO "CPU: %s\nPlatform: %s\n",
		CPU_MODEL_LONG, PLATFORM_LONG);

	/* do machine-specific setups.  */
	mach_setup (cmdline);

#ifdef CONFIG_MTD
	if (!ROOT_DEV && &_root_fs_image_end > &_root_fs_image_start)
		set_mem_root (&_root_fs_image_start,
			      &_root_fs_image_end - &_root_fs_image_start,
			      *cmdline);
#endif
}

void __init trap_init (void)
{
}

#ifdef CONFIG_MTD

/* From drivers/mtd/devices/slram.c */
#define SLRAM_BLK_SZ 0x4000

/* Set the root filesystem to be the given memory region.
   Some parameter may be appended to CMD_LINE.  */
void set_mem_root (void *addr, size_t len, char *cmd_line)
{
	/* Some sort of idiocy in MTD means we must supply a length that's
	   a multiple of SLRAM_BLK_SZ.  We just round up the real length,
	   as the file system shouldn't attempt to access anything beyond
	   the end of the image anyway.  */
	len = (((len - 1) + SLRAM_BLK_SZ) / SLRAM_BLK_SZ) * SLRAM_BLK_SZ;

	/* The only way to pass info to the MTD slram driver is via
	   the command line.  */
	if (*cmd_line) {
		cmd_line += strlen (cmd_line);
		*cmd_line++ = ' ';
	}
	sprintf (cmd_line, "slram=root,0x%x,+0x%x", (u32)addr, (u32)len);

	ROOT_DEV = MKDEV (MTD_BLOCK_MAJOR, 0);
}
#endif


static void irq_nop (unsigned irq) { }
static unsigned irq_zero (unsigned irq) { return 0; }

static void nmi_end (unsigned irq)
{
	if (irq != IRQ_NMI (0)) {
		printk (KERN_CRIT "NMI %d is unrecoverable; restarting...",
			irq - IRQ_NMI (0));
		machine_restart (0);
	}
}

static struct hw_interrupt_type nmi_irq_type = {
	.typename = "NMI",
	.startup = irq_zero,		/* startup */
	.shutdown = irq_nop,		/* shutdown */
	.enable = irq_nop,		/* enable */
	.disable = irq_nop,		/* disable */
	.ack = irq_nop,		/* ack */
	.end = nmi_end,		/* end */
};

void __init init_IRQ (void)
{
	init_irq_handlers (0, NUM_MACH_IRQS, 1, 0);
	init_irq_handlers (IRQ_NMI (0), NUM_NMIS, 1, &nmi_irq_type);
	mach_init_irqs ();
}


void __init mem_init (void)
{
	max_mapnr = MAP_NR (ram_start + ram_len);

	num_physpages = ADDR_TO_PAGE (ram_len);

	total_ram_pages = free_all_bootmem ();

	printk (KERN_INFO
		"Memory: %luK/%luK available"
		" (%luK kernel code, %luK data)\n",
		PAGE_TO_ADDR (nr_free_pages()) / 1024,
		ram_len / 1024,
		((unsigned long)&_etext - (unsigned long)&_stext) / 1024,
		((unsigned long)&_ebss - (unsigned long)&_sdata) / 1024);
}

void free_initmem (void)
{
	unsigned long ram_end = ram_start + ram_len;
	unsigned long start = PAGE_ALIGN ((unsigned long)(&_init_start));

	if (start >= ram_start && start < ram_end) {
		unsigned long addr;
		unsigned long end = PAGE_ALIGN ((unsigned long)(&_init_end));

		if (end > ram_end)
			end = ram_end;

		printk("Freeing unused kernel memory: %ldK freed\n",
		       (end - start) / 1024);

		for (addr = start; addr < end; addr += PAGE_SIZE) {
			struct page *page = virt_to_page (addr);
			ClearPageReserved (page);
			set_page_count (page, 1);
			__free_page (page);
			total_ram_pages++;
		}
	}
}


/* Initialize the `bootmem allocator'.  RAM_START and RAM_LEN identify
   what RAM may be used.  */
static void __init
init_bootmem_alloc (unsigned long ram_start, unsigned long ram_len)
{
	/* The part of the kernel that's in the same managed RAM space
	   used for general allocation.  */
	unsigned long kram_start = (unsigned long)&_kram_start;
	unsigned long kram_end = (unsigned long)&_kram_end;
	/* End of the managed RAM space.  */
	unsigned long ram_end = ram_start + ram_len;
	/* Address range of the interrupt vector table.  */
	unsigned long intv_start = (unsigned long)&_intv_start;
	unsigned long intv_end = (unsigned long)&_intv_end;
	/* True if the interrupt vectors are in the managed RAM area.  */
	int intv_in_ram = (intv_end > ram_start && intv_start < ram_end);
	/* True if the interrupt vectors are inside the kernel's RAM.  */
	int intv_in_kram = (intv_end > kram_start && intv_start < kram_end);
	/* A pointer to an optional function that reserves platform-specific
	   memory regions.  We declare the pointer `volatile' to avoid gcc
	   turning the call into a static call (the problem is that since
	   it's a weak symbol, a static call may end up trying to reference
	   the location 0x0, which is not always reachable).  */
	void (*volatile mrb) (void) = mach_reserve_bootmem;
	/* The bootmem allocator's allocation bitmap.  */
	unsigned long bootmap = (unsigned long)&_bootmap;
	unsigned long bootmap_len;

	/* Round bootmap location up to next page.  */
	bootmap = PAGE_TO_ADDR (ADDR_TO_PAGE_UP (bootmap));

	/* Initialize bootmem allocator.  */
	bootmap_len = init_bootmem_node (NODE_DATA (0),
					 ADDR_TO_PAGE (bootmap),
					 ADDR_TO_PAGE (PAGE_OFFSET),
					 ADDR_TO_PAGE (ram_end));

	/* Now make the RAM actually allocatable (it starts out `reserved'). */
	free_bootmem (ram_start, ram_len);

	if (kram_end > kram_start)
		/* Reserve the RAM part of the kernel's address space, so it
		   doesn't get allocated.  */
		reserve_bootmem (kram_start, kram_end - kram_start);
	
	if (intv_in_ram && !intv_in_kram)
		/* Reserve the interrupt vector space.  */
		reserve_bootmem (intv_start, intv_end - intv_start);

	if (bootmap >= ram_start && bootmap < ram_end)
		/* Reserve the bootmap space.  */
		reserve_bootmem (bootmap, bootmap_len);

	/* Reserve the memory used by the root filesystem image if it's
	   in RAM.  */
	if (&_root_fs_image_end > &_root_fs_image_start
	    && (unsigned long)&_root_fs_image_start >= ram_start
	    && (unsigned long)&_root_fs_image_start < ram_end)
		reserve_bootmem ((unsigned long)&_root_fs_image_start,
				 &_root_fs_image_end - &_root_fs_image_start);

	/* Let the platform-dependent code reserve some too.  */
	if (mrb)
		(*mrb) ();
}

/* Tell the kernel about what RAM it may use for memory allocation.  */
static void __init
init_mem_alloc (unsigned long ram_start, unsigned long ram_len)
{
	unsigned i;
	unsigned long zones_size[MAX_NR_ZONES];

	init_bootmem_alloc (ram_start, ram_len);

	for (i = 0; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	/* We stuff all the memory into one area, which includes the
	   initial gap from PAGE_OFFSET to ram_start.  */
	zones_size[ZONE_DMA]
		= ADDR_TO_PAGE (ram_len + (ram_start - PAGE_OFFSET));

	/* The allocator is very picky about the address of the first
	   allocatable page -- it must be at least as aligned as the
	   maximum allocation -- so try to detect cases where it will get
	   confused and signal them at compile time (this is a common
	   problem when porting to a new platform with ).  There is a
	   similar runtime check in free_area_init_core.  */
#if ((PAGE_OFFSET >> PAGE_SHIFT) & ((1UL << (MAX_ORDER - 1)) - 1))
#error MAX_ORDER is too large for given PAGE_OFFSET (use CONFIG_FORCE_MAX_ZONEORDER to change it)
#endif
	NODE_DATA(0)->node_mem_map = NULL;
	free_area_init_node (0, NODE_DATA(0), zones_size,
			     ADDR_TO_PAGE (PAGE_OFFSET), 0);
}



/* Taken from m68knommu */
void show_mem(void)
{
    unsigned long i;
    int free = 0, total = 0, reserved = 0, shared = 0;
    int cached = 0;

    printk(KERN_INFO "\nMem-info:\n");
    show_free_areas();
    i = max_mapnr;
    while (i-- > 0) {
	total++;
	if (PageReserved(mem_map+i))
	    reserved++;
	else if (PageSwapCache(mem_map+i))
	    cached++;
	else if (!page_count(mem_map+i))
	    free++;
	else
	    shared += page_count(mem_map+i) - 1;
    }
    printk(KERN_INFO "%d pages of RAM\n",total);
    printk(KERN_INFO "%d free pages\n",free);
    printk(KERN_INFO "%d reserved pages\n",reserved);
    printk(KERN_INFO "%d pages shared\n",shared);
    printk(KERN_INFO "%d pages swap cached\n",cached);
}
