/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx and 8260 by Dan.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/seq_file.h>
#include <linux/irq.h>

#include <asm/mmu.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/mpc8260.h>
#include <asm/cpm2.h>
#include <asm/machdep.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/ppc_sys.h>

#include "cpm2_pic.h"

unsigned char __res[sizeof(bd_t)];

extern void pq2_find_bridges(void);
extern void pq2pci_init_irq(void);
extern void idma_pci9_init(void);

/* Place-holder for board-specific init */
void __attribute__ ((weak)) __init
m82xx_board_setup(void)
{
}

static void __init
m8260_setup_arch(void)
{
	/* Print out Vendor and Machine info. */
	printk(KERN_INFO "%s %s port\n", CPUINFO_VENDOR, CPUINFO_MACHINE);

	/* Reset the Communication Processor Module. */
	cpm2_reset();
#ifdef CONFIG_8260_PCI9
	/* Initialise IDMA for PCI erratum workaround */
	idma_pci9_init();
#endif
#ifdef CONFIG_PCI_8260
	pq2_find_bridges();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
#endif

	identify_ppc_sys_by_name_and_id(BOARD_CHIP_NAME,
			in_be32((void *)CPM_MAP_ADDR + CPM_IMMR_OFFSET));

	m82xx_board_setup();
}

/* The decrementer counts at the system (internal) clock frequency
 * divided by four.
 */
static void __init
m8260_calibrate_decr(void)
{
	bd_t *binfo = (bd_t *)__res;
	int freq, divisor;

	freq = binfo->bi_busfreq;
        divisor = 4;
        tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

/* The 8260 has an internal 1-second timer update register that
 * we should use for this purpose.
 */
static uint rtc_time;

static int
m8260_set_rtc_time(unsigned long time)
{
	rtc_time = time;

	return(0);
}

static unsigned long
m8260_get_rtc_time(void)
{
	/* Get time from the RTC.
	*/
	return((unsigned long)rtc_time);
}

#ifndef BOOTROM_RESTART_ADDR
#warning "Using default BOOTROM_RESTART_ADDR!"
#define BOOTROM_RESTART_ADDR	0xff000104
#endif

static void
m8260_restart(char *cmd)
{
	extern void m8260_gorom(bd_t *bi, uint addr);
	uint	startaddr;

	/* Most boot roms have a warmstart as the second instruction
	 * of the reset vector.  If that doesn't work for you, change this
	 * or the reboot program to send a proper address.
	 */
	startaddr = BOOTROM_RESTART_ADDR;
	if (cmd != NULL) {
		if (!strncmp(cmd, "startaddr=", 10))
			startaddr = simple_strtoul(&cmd[10], NULL, 0);
	}

	m8260_gorom((void*)__pa(__res), startaddr);
}

static void
m8260_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
m8260_power_off(void)
{
	m8260_halt();
}

static int
m8260_show_cpuinfo(struct seq_file *m)
{
	bd_t *bp = (bd_t *)__res;

	seq_printf(m, "vendor\t\t: %s\n"
		   "machine\t\t: %s\n"
		   "\n"
		   "mem size\t\t: 0x%08lx\n"
		   "console baud\t\t: %ld\n"
		   "\n"
		   "core clock\t: %lu MHz\n"
		   "CPM  clock\t: %lu MHz\n"
		   "bus  clock\t: %lu MHz\n",
		   CPUINFO_VENDOR, CPUINFO_MACHINE, bp->bi_memsize,
		   bp->bi_baudrate, bp->bi_intfreq / 1000000,
		   bp->bi_cpmfreq / 1000000, bp->bi_busfreq / 1000000);
	return 0;
}

/* Initialize the internal interrupt controller.  The number of
 * interrupts supported can vary with the processor type, and the
 * 8260 family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
static void __init
m8260_init_IRQ(void)
{
	cpm2_init_IRQ();

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	cpm2_immr->im_intctl.ic_siprr = 0x05309770;
}

/*
 * Same hack as 8xx
 */
static unsigned long __init
m8260_find_end_of_memory(void)
{
	bd_t *binfo = (bd_t *)__res;

	return binfo->bi_memsize;
}

/* Map the IMMR, plus anything else we can cover
 * in that upper space according to the memory controller
 * chip select mapping.  Grab another bunch of space
 * below that for stuff we can't cover in the upper.
 */
static void __init
m8260_map_io(void)
{
	uint addr;

	/* Map IMMR region to a 256MB BAT */
	addr = (cpm2_immr != NULL) ? (uint)cpm2_immr : CPM_MAP_ADDR;
	io_block_mapping(addr, addr, 0x10000000, _PAGE_IO);

	/* Map I/O region to a 256MB BAT */
	io_block_mapping(IO_VIRT_ADDR, IO_PHYS_ADDR, 0x10000000, _PAGE_IO);
}

/* Place-holder for board-specific ppc_md hooking */
void __attribute__ ((weak)) __init
m82xx_board_init(void)
{
}

/* Inputs:
 *   r3 - Optional pointer to a board information structure.
 *   r4 - Optional pointer to the physical starting address of the init RAM
 *        disk.
 *   r5 - Optional pointer to the physical ending address of the init RAM
 *        disk.
 *   r6 - Optional pointer to the physical starting address of any kernel
 *        command-line parameters.
 *   r7 - Optional pointer to the physical ending address of any kernel
 *        command-line parameters.
 */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	if ( r3 )
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 ) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 ) {
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

	ppc_md.setup_arch		= m8260_setup_arch;
	ppc_md.show_cpuinfo		= m8260_show_cpuinfo;
	ppc_md.init_IRQ			= m8260_init_IRQ;
	ppc_md.get_irq			= cpm2_get_irq;

	ppc_md.restart			= m8260_restart;
	ppc_md.power_off		= m8260_power_off;
	ppc_md.halt			= m8260_halt;

	ppc_md.set_rtc_time		= m8260_set_rtc_time;
	ppc_md.get_rtc_time		= m8260_get_rtc_time;
	ppc_md.calibrate_decr		= m8260_calibrate_decr;

	ppc_md.find_end_of_memory	= m8260_find_end_of_memory;
	ppc_md.setup_io_mappings	= m8260_map_io;

	/* Call back for board-specific settings and overrides. */
	m82xx_board_init();
}
