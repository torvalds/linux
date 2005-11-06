/*
 *  linux/arch/m68knommu/kernel/setup.c
 *
 *  Copyright (C) 1999-2004  Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998,1999  D. Jeff Dionne <jeff@lineo.ca>
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/machdep.h>

#ifdef CONFIG_BLK_DEV_INITRD
#include <asm/pgtable.h>
#endif

unsigned long rom_length;
unsigned long memory_start;
unsigned long memory_end;

EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);

char command_line[COMMAND_LINE_SIZE];

/* setup some dummy routines */
static void dummy_waitbut(void)
{
}

void (*mach_sched_init) (irqreturn_t (*handler)(int, void *, struct pt_regs *)) = NULL;
void (*mach_tick)( void ) = NULL;
/* machine dependent keyboard functions */
int (*mach_keyb_init) (void) = NULL;
int (*mach_kbdrate) (struct kbd_repeat *) = NULL;
void (*mach_kbd_leds) (unsigned int) = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) = NULL;
irqreturn_t (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) = NULL;
void (*mach_enable_irq) (unsigned int) = NULL;
void (*mach_disable_irq) (unsigned int) = NULL;
int (*mach_get_irq_list) (struct seq_file *, void *) = NULL;
void (*mach_process_int) (int irq, struct pt_regs *fp) = NULL;
void (*mach_trap_init) (void);
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void) = NULL;
void (*mach_gettod) (int*, int*, int*, int*, int*, int*) = NULL;
int (*mach_hwclk) (int, struct hwclk_time*) = NULL;
int (*mach_set_clock_mmss) (unsigned long) = NULL;
void (*mach_mksound)( unsigned int count, unsigned int ticks ) = NULL;
void (*mach_reset)( void ) = NULL;
void (*waitbut)(void) = dummy_waitbut;
void (*mach_debug_init)(void) = NULL;
void (*mach_halt)( void ) = NULL;
void (*mach_power_off)( void ) = NULL;


#ifdef CONFIG_M68000
	#define CPU "MC68000"
#endif
#ifdef CONFIG_M68328
	#define CPU "MC68328"
#endif
#ifdef CONFIG_M68EZ328
	#define CPU "MC68EZ328"
#endif
#ifdef CONFIG_M68VZ328
	#define CPU "MC68VZ328"
#endif
#ifdef CONFIG_M68332
	#define CPU "MC68332"
#endif
#ifdef CONFIG_M68360
	#define CPU "MC68360"
#endif
#if defined(CONFIG_M5206)
	#define	CPU "COLDFIRE(m5206)"
#endif
#if defined(CONFIG_M5206e)
	#define	CPU "COLDFIRE(m5206e)"
#endif
#if defined(CONFIG_M523x)
	#define CPU "COLDFIRE(m523x)"
#endif
#if defined(CONFIG_M5249)
	#define CPU "COLDFIRE(m5249)"
#endif
#if defined(CONFIG_M5271)
	#define CPU "COLDFIRE(m5270/5271)"
#endif
#if defined(CONFIG_M5272)
	#define CPU "COLDFIRE(m5272)"
#endif
#if defined(CONFIG_M5275)
	#define CPU "COLDFIRE(m5274/5275)"
#endif
#if defined(CONFIG_M528x)
	#define CPU "COLDFIRE(m5280/5282)"
#endif
#if defined(CONFIG_M5307)
	#define	CPU "COLDFIRE(m5307)"
#endif
#if defined(CONFIG_M5407)
	#define	CPU "COLDFIRE(m5407)"
#endif
#ifndef CPU
	#define	CPU "UNKOWN"
#endif

/* (es) */
/* note: why is this defined here?  the must be a better place to put this */
#if defined( CONFIG_TELOS) || defined( CONFIG_UCDIMM ) || defined( CONFIG_UCSIMM ) || defined(CONFIG_DRAGEN2) || (defined( CONFIG_PILOT ) && defined( CONFIG_M68328 ))
#define CAT_ROMARRAY
#endif
/* (/es) */

extern int _stext, _etext, _sdata, _edata, _sbss, _ebss, _end;
extern int _ramstart, _ramend;

void setup_arch(char **cmdline_p)
{
	int bootmap_size;

#if defined(CAT_ROMARRAY) && defined(DEBUG)
	extern int __data_rom_start;
	extern int __data_start;
	int *romarray = (int *)((int) &__data_rom_start +
			      (int)&_edata - (int)&__data_start);
#endif

	memory_start = PAGE_ALIGN(_ramstart);
	memory_end = _ramend; /* by now the stack is part of the init task */

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) 0;

	config_BSP(&command_line[0], sizeof(command_line));

	printk(KERN_INFO "\x0F\r\n\nuClinux/" CPU "\n");

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
#ifdef CONFIG_TELOS
	printk(KERN_INFO "Modified for Omnia ToolVox by James D. Schettine, james@telos-systems.com\n");
#endif
#endif
	printk(KERN_INFO "Flat model support (C) 1998,1999 Kenneth Albanowski, D. Jeff Dionne\n");

#if defined( CONFIG_PILOT ) && defined( CONFIG_M68328 )
	printk(KERN_INFO "TRG SuperPilot FLASH card support <info@trgnet.com>\n");
#endif

#if defined( CONFIG_PILOT ) && defined( CONFIG_M68EZ328 )
	printk(KERN_INFO "PalmV support by Lineo Inc. <jeff@uclinux.com>\n");
#endif

#ifdef CONFIG_M68EZ328ADS
	printk(KERN_INFO "M68EZ328ADS board support (C) 1999 Vladimir Gurevich <vgurevic@cisco.com>\n");
#endif

#ifdef CONFIG_ALMA_ANS
	printk(KERN_INFO "Alma Electronics board support (C) 1999 Vladimir Gurevich <vgurevic@cisco.com>\n");
#endif
#if defined (CONFIG_M68360)
	printk(KERN_INFO "QUICC port done by SED Systems <hamilton@sedsystems.ca>,\n");
	printk(KERN_INFO "based on 2.0.38 port by Lineo Inc. <mleslie@lineo.com>.\n");
#endif
#ifdef CONFIG_DRAGEN2
	printk(KERN_INFO "DragonEngine II board support by Georges Menie\n");
#endif
#ifdef CONFIG_M5235EVB
	printk(KERN_INFO "Motorola M5235EVB support (C)2005 Syn-tech Systems, Inc. (Jate Sujjavanich)");
#endif

#ifdef DEBUG
	printk(KERN_DEBUG "KERNEL -> TEXT=0x%06x-0x%06x DATA=0x%06x-0x%06x "
		"BSS=0x%06x-0x%06x\n", (int) &_stext, (int) &_etext,
		(int) &_sdata, (int) &_edata,
		(int) &_sbss, (int) &_ebss);
	printk(KERN_DEBUG "KERNEL -> ROMFS=0x%06x-0x%06x MEM=0x%06x-0x%06x "
		"STACK=0x%06x-0x%06x\n",
#ifdef CAT_ROMARRAY
		(int) romarray, ((int) romarray) + romarray[2],
#else
		(int) &_ebss, (int) memory_start,
#endif
		(int) memory_start, (int) memory_end,
		(int) memory_end, (int) _ramend);
#endif

	/* Keep a copy of command line */
	*cmdline_p = &command_line[0];
	memcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = 0;

#ifdef DEBUG
	if (strlen(*cmdline_p))
		printk(KERN_DEBUG "Command line: '%s'\n", *cmdline_p);
#endif

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
	reserve_bootmem(memory_start, bootmap_size);

	/*
	 * Get kmalloc into gear.
	 */
	paging_init();
}

int get_cpuinfo(char * buffer)
{
    char *cpu, *mmu, *fpu;
    u_long clockfreq;

    cpu = CPU;
    mmu = "none";
    fpu = "none";

#ifdef CONFIG_COLDFIRE
    clockfreq = (loops_per_jiffy*HZ)*3;
#else
    clockfreq = (loops_per_jiffy*HZ)*16;
#endif

    return(sprintf(buffer, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   (loops_per_jiffy*HZ)/500000,((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ)));

}

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
    char *cpu, *mmu, *fpu;
    u_long clockfreq;

    cpu = CPU;
    mmu = "none";
    fpu = "none";

#ifdef CONFIG_COLDFIRE
    clockfreq = (loops_per_jiffy*HZ)*3;
#else
    clockfreq = (loops_per_jiffy*HZ)*16;
#endif

    seq_printf(m, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   (loops_per_jiffy*HZ)/500000,((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ));

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

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
}

