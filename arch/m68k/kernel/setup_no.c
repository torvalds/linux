// SPDX-License-Identifier: GPL-2.0
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
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/rtc.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>
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
int (*mach_hwclk) (int, struct rtc_time*);

/* machine dependent reboot functions */
void (*mach_reset)(void);
void (*mach_halt)(void);
void (*mach_power_off)(void);

#ifdef CONFIG_M68000
#if defined(CONFIG_M68328)
#define CPU_NAME	"MC68328"
#elif defined(CONFIG_M68EZ328)
#define CPU_NAME	"MC68EZ328"
#elif defined(CONFIG_M68VZ328)
#define CPU_NAME	"MC68VZ328"
#else
#define CPU_NAME	"MC68000"
#endif
#endif /* CONFIG_M68000 */
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

void __init setup_arch(char **cmdline_p)
{
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

	process_uboot_commandline(&command_line[0], sizeof(command_line));

	pr_info("uClinux with CPU " CPU_NAME "\n");

#ifdef CONFIG_UCDIMM
	pr_info("uCdimm by Lineo, Inc. <www.lineo.com>\n");
#endif
#ifdef CONFIG_M68VZ328
	pr_info("M68VZ328 support by Evan Stawnyczy <e@lineo.ca>\n");
#endif
#ifdef CONFIG_COLDFIRE
	pr_info("COLDFIRE port done by Greg Ungerer, gerg@snapgear.com\n");
#ifdef CONFIG_M5307
	pr_info("Modified for M5307 by Dave Miller, dmiller@intellistor.com\n");
#endif
#ifdef CONFIG_ELITE
	pr_info("Modified for M5206eLITE by Rob Scott, rscott@mtrob.fdns.net\n");
#endif
#endif
	pr_info("Flat model support (C) 1998,1999 Kenneth Albanowski, D. Jeff Dionne\n");

#if defined( CONFIG_PILOT ) && defined( CONFIG_M68328 )
	pr_info("TRG SuperPilot FLASH card support <info@trgnet.com>\n");
#endif
#if defined( CONFIG_PILOT ) && defined( CONFIG_M68EZ328 )
	pr_info("PalmV support by Lineo Inc. <jeff@uclinux.com>\n");
#endif
#ifdef CONFIG_DRAGEN2
	pr_info("DragonEngine II board support by Georges Menie\n");
#endif
#ifdef CONFIG_M5235EVB
	pr_info("Motorola M5235EVB support (C)2005 Syn-tech Systems, Inc. (Jate Sujjavanich)\n");
#endif

	pr_debug("KERNEL -> TEXT=0x%p-0x%p DATA=0x%p-0x%p BSS=0x%p-0x%p\n",
		 _stext, _etext, _sdata, _edata, __bss_start, __bss_stop);
	pr_debug("MEMORY -> ROMFS=0x%p-0x%06lx MEM=0x%06lx-0x%06lx\n ",
		 __bss_stop, memory_start, memory_start, memory_end);

	memblock_add(memory_start, memory_end - memory_start);

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
	min_low_pfn = PFN_DOWN(memory_start);
	max_pfn = max_low_pfn = PFN_DOWN(memory_end);

#if defined(CONFIG_UBOOT) && defined(CONFIG_BLK_DEV_INITRD)
	if ((initrd_start > 0) && (initrd_start < initrd_end) &&
			(initrd_end < memory_end))
		memblock_reserve(initrd_start, initrd_end - initrd_start);
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

