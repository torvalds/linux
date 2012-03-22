/*
 *  linux/arch/m68knommu/kernel/setup.c
 *
 *  Copyright (C) 1999-2007  Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998,1999  D. Jeff Dionne <jeff@uClinux.org>
 *  Copyleft  ()) 2000       James D. Schettine {james@telos-systems.com}
 *  Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *  Copyright (C) 1995       Hamish Macdonald
 *  Copyright (C) 2000       Lineo Inc. (www.lineo.com)
 *  Copyright (C) 2001 	     Lineo, Inc. <www.lineo.com>
 *
 *  68VZ328 Fixes/support    Evan Stawnyczy <e@lineo.ca>
 */

/*
 * This file handles the architecture-dependent parts of system setup
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/rtc.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/pgtable.h>
#include <asm/sections.h>

unsigned long memory_start;
unsigned long memory_end;

EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);

char __initdata command_line[COMMAND_LINE_SIZE];

/* machine dependent timer functions */
void (*mach_sched_init)(irq_handler_t handler) __initdata = NULL;
int (*mach_set_clock_mmss)(unsigned long);
int (*mach_hwclk) (int, struct rtc_time*);

/* machine dependent reboot functions */
void (*mach_reset)(void);
void (*mach_halt)(void);
void (*mach_power_off)(void);

#ifdef CONFIG_M68328
#define CPU_NAME	"MC68328"
#endif
#ifdef CONFIG_M68EZ328
#define CPU_NAME	"MC68EZ328"
#endif
#ifdef CONFIG_M68VZ328
#define CPU_NAME	"MC68VZ328"
#endif
#ifdef CONFIG_M68360
#define CPU_NAME	"MC68360"
#endif
#ifndef CPU_NAME
#define	CPU_NAME	"UNKNOWN"
#endif

/*
 * Different cores have different instruction execution timings.
 * The old/traditional 68000 cores are basically all the same, at 16.
 * The ColdFire cores vary a little, their values are defined in their
 * headers. We default to the standard 68000 value here.
 */
#ifndef CPU_INSTR_PER_JIFFY
#define	CPU_INSTR_PER_JIFFY	16
#endif

#if defined(CONFIG_UBOOT)
/*
 * parse_uboot_commandline
 *
 * Copies u-boot commandline arguments and store them in the proper linux
 * variables.
 *
 * Assumes:
 *	_init_sp global contains the address in the stack pointer when the
 *	kernel starts (see head.S::_start)
 *
 *	U-Boot calling convention:
 *	(*kernel) (kbd, initrd_start, initrd_end, cmd_start, cmd_end);
 *
 *	_init_sp can be parsed as such
 *
 *	_init_sp+00 = u-boot cmd after jsr into kernel (skip)
 *	_init_sp+04 = &kernel board_info (residual data)
 *	_init_sp+08 = &initrd_start
 *	_init_sp+12 = &initrd_end
 *	_init_sp+16 = &cmd_start
 *	_init_sp+20 = &cmd_end
 *
 *	This also assumes that the memory locations pointed to are still
 *	unmodified. U-boot places them near the end of external SDRAM.
 *
 * Argument(s):
 *	commandp = the linux commandline arg container to fill.
 *	size     = the sizeof commandp.
 *
 * Returns:
 */
void parse_uboot_commandline(char *commandp, int size)
{
	extern unsigned long _init_sp;
	unsigned long *sp;
	unsigned long uboot_kbd;
	unsigned long uboot_initrd_start, uboot_initrd_end;
	unsigned long uboot_cmd_start, uboot_cmd_end;


	sp = (unsigned long *)_init_sp;
	uboot_kbd = sp[1];
	uboot_initrd_start = sp[2];
	uboot_initrd_end = sp[3];
	uboot_cmd_start = sp[4];
	uboot_cmd_end = sp[5];

	if (uboot_cmd_start && uboot_cmd_end)
		strncpy(commandp, (const char *)uboot_cmd_start, size);
#if defined(CONFIG_BLK_DEV_INITRD)
	if (uboot_initrd_start && uboot_initrd_end &&
		(uboot_initrd_end > uboot_initrd_start)) {
		initrd_start = uboot_initrd_start;
		initrd_end = uboot_initrd_end;
		ROOT_DEV = Root_RAM0;
		printk(KERN_INFO "initrd at 0x%lx:0x%lx\n",
			initrd_start, initrd_end);
	}
#endif /* if defined(CONFIG_BLK_DEV_INITRD) */
}
#endif /* #if defined(CONFIG_UBOOT) */

void __init setup_arch(char **cmdline_p)
{
	int bootmap_size;

	memory_start = PAGE_ALIGN(_ramstart);
	memory_end = _ramend;

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) 0;

	config_BSP(&command_line[0], sizeof(command_line));

#if defined(CONFIG_BOOTPARAM)
	strncpy(&command_line[0], CONFIG_BOOTPARAM_STRING, sizeof(command_line));
	command_line[sizeof(command_line) - 1] = 0;
#endif /* CONFIG_BOOTPARAM */

#if defined(CONFIG_UBOOT)
	/* CONFIG_UBOOT and CONFIG_BOOTPARAM defined, concatenate cmdline */
	#if defined(CONFIG_BOOTPARAM)
		/* Add the whitespace separator */
		command_line[strlen(CONFIG_BOOTPARAM_STRING)] = ' ';
		/* Parse uboot command line into the rest of the buffer */
		parse_uboot_commandline(
			&command_line[(strlen(CONFIG_BOOTPARAM_STRING)+1)],
			(sizeof(command_line) -
			(strlen(CONFIG_BOOTPARAM_STRING)+1)));
	/* Only CONFIG_UBOOT defined, create cmdline */
	#else
		parse_uboot_commandline(&command_line[0], sizeof(command_line));
	#endif /* CONFIG_BOOTPARAM */
	command_line[sizeof(command_line) - 1] = 0;
#endif /* CONFIG_UBOOT */

	printk(KERN_INFO "\x0F\r\n\nuClinux/" CPU_NAME "\n");

#ifdef CONFIG_UCDIMM
	printk(KERN_INFO "uCdimm by Lineo, Inc. <www.lineo.com>\n");
#endif
#ifdef CONFIG_M68VZ328
	printk(KERN_INFO "M68VZ328 support by Evan Stawnyczy <e@lineo.ca>\n");
#endif
#ifdef CONFIG_COLDFIRE
	printk(KERN_INFO "COLDFIRE port done by Greg Ungerer, gerg@snapgear.com\n");
#ifdef CONFIG_M5307
	printk(KERN_INFO "Modified for M5307 by Dave Miller, dmiller@intellistor.com\n");
#endif
#ifdef CONFIG_ELITE
	printk(KERN_INFO "Modified for M5206eLITE by Rob Scott, rscott@mtrob.fdns.net\n");
#endif
#endif
	printk(KERN_INFO "Flat model support (C) 1998,1999 Kenneth Albanowski, D. Jeff Dionne\n");

#if defined( CONFIG_PILOT ) && defined( CONFIG_M68328 )
	printk(KERN_INFO "TRG SuperPilot FLASH card support <info@trgnet.com>\n");
#endif
#if defined( CONFIG_PILOT ) && defined( CONFIG_M68EZ328 )
	printk(KERN_INFO "PalmV support by Lineo Inc. <jeff@uclinux.com>\n");
#endif
#if defined (CONFIG_M68360)
	printk(KERN_INFO "QUICC port done by SED Systems <hamilton@sedsystems.ca>,\n");
	printk(KERN_INFO "based on 2.0.38 port by Lineo Inc. <mleslie@lineo.com>.\n");
#endif
#ifdef CONFIG_DRAGEN2
	printk(KERN_INFO "DragonEngine II board support by Georges Menie\n");
#endif
#ifdef CONFIG_M5235EVB
	printk(KERN_INFO "Motorola M5235EVB support (C)2005 Syn-tech Systems, Inc. (Jate Sujjavanich)\n");
#endif

	pr_debug("KERNEL -> TEXT=0x%06x-0x%06x DATA=0x%06x-0x%06x "
		 "BSS=0x%06x-0x%06x\n", (int) &_stext, (int) &_etext,
		 (int) &_sdata, (int) &_edata,
		 (int) &_sbss, (int) &_ebss);
	pr_debug("MEMORY -> ROMFS=0x%06x-0x%06x MEM=0x%06x-0x%06x\n ",
		 (int) &_ebss, (int) memory_start,
		 (int) memory_start, (int) memory_end);

	/* Keep a copy of command line */
	*cmdline_p = &command_line[0];
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE-1] = 0;

#if defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif

	/*
	 * Give all the memory to the bootmap allocator, tell it to put the
	 * boot mem_map at the start of memory.
	 */
	bootmap_size = init_bootmem_node(
			NODE_DATA(0),
			memory_start >> PAGE_SHIFT, /* map goes here */
			PAGE_OFFSET >> PAGE_SHIFT,	/* 0 on coldfire */
			memory_end >> PAGE_SHIFT);
	/*
	 * Free the usable memory, we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	free_bootmem(memory_start, memory_end - memory_start);
	reserve_bootmem(memory_start, bootmap_size, BOOTMEM_DEFAULT);

#if defined(CONFIG_UBOOT) && defined(CONFIG_BLK_DEV_INITRD)
	if ((initrd_start > 0) && (initrd_start < initrd_end) &&
			(initrd_end < memory_end))
		reserve_bootmem(initrd_start, initrd_end - initrd_start,
				 BOOTMEM_DEFAULT);
#endif /* if defined(CONFIG_BLK_DEV_INITRD) */

	/*
	 * Get kmalloc into gear.
	 */
	paging_init();
}

/*
 *	Get CPU information for use by the procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *cpu, *mmu, *fpu;
	u_long clockfreq;

	cpu = CPU_NAME;
	mmu = "none";
	fpu = "none";
	clockfreq = (loops_per_jiffy * HZ) * CPU_INSTR_PER_JIFFY;

	seq_printf(m, "CPU:\t\t%s\n"
		      "MMU:\t\t%s\n"
		      "FPU:\t\t%s\n"
		      "Clocking:\t%lu.%1luMHz\n"
		      "BogoMips:\t%lu.%02lu\n"
		      "Calibration:\t%lu loops\n",
		      cpu, mmu, fpu,
		      clockfreq / 1000000,
		      (clockfreq / 100000) % 10,
		      (loops_per_jiffy * HZ) / 500000,
		      ((loops_per_jiffy * HZ) / 5000) % 100,
		      (loops_per_jiffy * HZ));

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? ((void *) 0x12345678) : NULL;
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

