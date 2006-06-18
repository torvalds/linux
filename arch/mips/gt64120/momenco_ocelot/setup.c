/*
 * setup.c
 *
 * BRIEF MODULE DESCRIPTION
 * Momentum Computer Ocelot (CP7000) - board dependent boot routines
 *
 * Copyright (C) 1996, 1997, 2001, 06  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2000 RidgeRun, Inc.
 * Copyright (C) 2001 Red Hat, Inc.
 * Copyright (C) 2002 Momentum Computer
 *
 * Author: RidgeRun, Inc.
 *   glonnon@ridgerun.com, skranz@ridgerun.com, stevej@ridgerun.com
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <linux/vmalloc.h>
#include <linux/pm.h>

#include <asm/time.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/traps.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <asm/gt64120.h>
#include "ocelot_pld.h"

unsigned long gt64120_base = KSEG1ADDR(GT_DEF_BASE);

/* These functions are used for rebooting or halting the machine*/
extern void momenco_ocelot_restart(char *command);
extern void momenco_ocelot_halt(void);
extern void momenco_ocelot_power_off(void);

extern void gt64120_time_init(void);
extern void momenco_ocelot_irq_setup(void);

static char reset_reason;

#define ENTRYLO(x) ((pte_val(pfn_pte((x) >> PAGE_SHIFT, PAGE_KERNEL_UNCACHED)) >> 6)|1)

static void __init setup_l3cache(unsigned long size);

/* setup code for a handoff from a version 1 PMON 2000 PROM */
void PMON_v1_setup()
{
	/* A wired TLB entry for the GT64120A and the serial port. The
	   GT64120A is going to be hit on every IRQ anyway - there's
	   absolutely no point in letting it be a random TLB entry, as
	   it'll just cause needless churning of the TLB. And we use
	   the other half for the serial port, which is just a PITA
	   otherwise :)

		Device			Physical	Virtual
		GT64120 Internal Regs	0x24000000	0xe0000000
		UARTs (CS2)		0x2d000000	0xe0001000
	*/
	add_wired_entry(ENTRYLO(0x24000000), ENTRYLO(0x2D000000), 0xe0000000, PM_4K);

	/* Also a temporary entry to let us talk to the Ocelot PLD and NVRAM
	   in the CS[012] region. We can't use ioremap() yet. The NVRAM
	   is a ST M48T37Y, which includes NVRAM, RTC, and Watchdog functions.

		Ocelot PLD (CS0)	0x2c000000	0xe0020000
		NVRAM			0x2c800000	0xe0030000
	*/

	add_temporary_entry(ENTRYLO(0x2C000000), ENTRYLO(0x2d000000), 0xe0020000, PM_64K);

	/* Relocate the CS3/BootCS region */
  	GT_WRITE(GT_CS3BOOTLD_OFS, 0x2f000000 >> 21);

	/* Relocate CS[012] */
 	GT_WRITE(GT_CS20LD_OFS, 0x2c000000 >> 21);

	/* Relocate the GT64120A itself... */
	GT_WRITE(GT_ISD_OFS, 0x24000000 >> 21);
	mb();
	gt64120_base = 0xe0000000;

	/* ...and the PCI0 view of it. */
	GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000020);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x24000000);
	GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000024);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x24000001);
}

/* setup code for a handoff from a version 2 PMON 2000 PROM */
void PMON_v2_setup()
{
	/* A wired TLB entry for the GT64120A and the serial port. The
	   GT64120A is going to be hit on every IRQ anyway - there's
	   absolutely no point in letting it be a random TLB entry, as
	   it'll just cause needless churning of the TLB. And we use
	   the other half for the serial port, which is just a PITA
	   otherwise :)

		Device			Physical	Virtual
		GT64120 Internal Regs	0xf4000000	0xe0000000
		UARTs (CS2)		0xfd000000	0xe0001000
	*/
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xfD000000), 0xe0000000, PM_4K);

	/* Also a temporary entry to let us talk to the Ocelot PLD and NVRAM
	   in the CS[012] region. We can't use ioremap() yet. The NVRAM
	   is a ST M48T37Y, which includes NVRAM, RTC, and Watchdog functions.

		Ocelot PLD (CS0)	0xfc000000	0xe0020000
		NVRAM			0xfc800000	0xe0030000
	*/
	add_temporary_entry(ENTRYLO(0xfC000000), ENTRYLO(0xfd000000), 0xe0020000, PM_64K);

	gt64120_base = 0xe0000000;
}

void __init plat_mem_setup(void)
{
	void (*l3func)(unsigned long)=KSEG1ADDR(&setup_l3cache);
	unsigned int tmpword;

	board_time_init = gt64120_time_init;

	_machine_restart = momenco_ocelot_restart;
	_machine_halt = momenco_ocelot_halt;
	pm_power_off = momenco_ocelot_power_off;

	/*
	 * initrd_start = (ulong)ocelot_initrd_start;
	 * initrd_end = (ulong)ocelot_initrd_start + (ulong)ocelot_initrd_size;
	 * initrd_below_start_ok = 1;
	 */

	/* do handoff reconfiguration */
	if (gt64120_base == KSEG1ADDR(GT_DEF_BASE))
		PMON_v1_setup();
	else
		PMON_v2_setup();

	/* Turn off the Bit-Error LED */
	OCELOT_PLD_WRITE(0x80, INTCLR);

	/* Relocate all the PCI1 stuff, not that we use it */
	GT_WRITE(GT_PCI1IOLD_OFS, 0x30000000 >> 21);
	GT_WRITE(GT_PCI1M0LD_OFS, 0x32000000 >> 21);
	GT_WRITE(GT_PCI1M1LD_OFS, 0x34000000 >> 21);

	/* Relocate PCI0 I/O and Mem0 */
	GT_WRITE(GT_PCI0IOLD_OFS, 0x20000000 >> 21);
	GT_WRITE(GT_PCI0M0LD_OFS, 0x22000000 >> 21);

	/* Relocate PCI0 Mem1 */
	GT_WRITE(GT_PCI0M1LD_OFS, 0x36000000 >> 21);

	/* For the initial programming, we assume 512MB configuration */
	/* Relocate the CPU's view of the RAM... */
	GT_WRITE(GT_SCS10LD_OFS, 0);
	GT_WRITE(GT_SCS10HD_OFS, 0x0fe00000 >> 21);
	GT_WRITE(GT_SCS32LD_OFS, 0x10000000 >> 21);
	GT_WRITE(GT_SCS32HD_OFS, 0x0fe00000 >> 21);

	GT_WRITE(GT_SCS1LD_OFS, 0xff);
	GT_WRITE(GT_SCS1HD_OFS, 0x00);
	GT_WRITE(GT_SCS0LD_OFS, 0);
	GT_WRITE(GT_SCS0HD_OFS, 0xff);
	GT_WRITE(GT_SCS3LD_OFS, 0xff);
	GT_WRITE(GT_SCS3HD_OFS, 0x00);
	GT_WRITE(GT_SCS2LD_OFS, 0);
	GT_WRITE(GT_SCS2HD_OFS, 0xff);

	/* ...and the PCI0 view of it. */
	GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000010);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x00000000);
	GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000014);
	GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x10000000);
	GT_WRITE(GT_PCI0_BS_SCS10_OFS, 0x0ffff000);
	GT_WRITE(GT_PCI0_BS_SCS32_OFS, 0x0ffff000);

	tmpword = OCELOT_PLD_READ(BOARDREV);
	if (tmpword < 26)
		printk("Momenco Ocelot: Board Assembly Rev. %c\n", 'A'+tmpword);
	else
		printk("Momenco Ocelot: Board Assembly Revision #0x%x\n", tmpword);

	tmpword = OCELOT_PLD_READ(PLD1_ID);
	printk("PLD 1 ID: %d.%d\n", tmpword>>4, tmpword&15);
	tmpword = OCELOT_PLD_READ(PLD2_ID);
	printk("PLD 2 ID: %d.%d\n", tmpword>>4, tmpword&15);
	tmpword = OCELOT_PLD_READ(RESET_STATUS);
	printk("Reset reason: 0x%x\n", tmpword);
	reset_reason = tmpword;
	OCELOT_PLD_WRITE(0xff, RESET_STATUS);

	tmpword = OCELOT_PLD_READ(BOARD_STATUS);
	printk("Board Status register: 0x%02x\n", tmpword);
	printk("  - User jumper: %s\n", (tmpword & 0x80)?"installed":"absent");
	printk("  - Boot flash write jumper: %s\n", (tmpword&0x40)?"installed":"absent");
	printk("  - Tulip PHY %s connected\n", (tmpword&0x10)?"is":"not");
	printk("  - L3 Cache size: %d MiB\n", (1<<((tmpword&12) >> 2))&~1);
	printk("  - SDRAM size: %d MiB\n", 1<<(6+(tmpword&3)));

	if (tmpword&12)
		l3func((1<<(((tmpword&12) >> 2)+20)));

	switch(tmpword &3) {
	case 3:
		/* 512MiB */
		/* Decoders are allready set -- just add the
		 * appropriate region */
		add_memory_region( 0x40<<20,  0xC0<<20, BOOT_MEM_RAM);
		add_memory_region(0x100<<20, 0x100<<20, BOOT_MEM_RAM);
		break;
	case 2:
		/* 256MiB -- two banks of 128MiB */
		GT_WRITE(GT_SCS10HD_OFS, 0x07e00000 >> 21);
		GT_WRITE(GT_SCS32LD_OFS, 0x08000000 >> 21);
		GT_WRITE(GT_SCS32HD_OFS, 0x0fe00000 >> 21);

		GT_WRITE(GT_SCS0HD_OFS, 0x7f);
		GT_WRITE(GT_SCS2LD_OFS, 0x80);
		GT_WRITE(GT_SCS2HD_OFS, 0xff);

		/* reconfigure the PCI0 interface view of memory */
		GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000014);
		GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x08000000);
		GT_WRITE(GT_PCI0_BS_SCS10_OFS, 0x0ffff000);
		GT_WRITE(GT_PCI0_BS_SCS32_OFS, 0x0ffff000);

		add_memory_region(0x40<<20, 0x40<<20, BOOT_MEM_RAM);
		add_memory_region(0x80<<20, 0x80<<20, BOOT_MEM_RAM);
		break;
	case 1:
		/* 128MiB -- 64MiB per bank */
		GT_WRITE(GT_SCS10HD_OFS, 0x03e00000 >> 21);
		GT_WRITE(GT_SCS32LD_OFS, 0x04000000 >> 21);
		GT_WRITE(GT_SCS32HD_OFS, 0x07e00000 >> 21);

		GT_WRITE(GT_SCS0HD_OFS, 0x3f);
		GT_WRITE(GT_SCS2LD_OFS, 0x40);
		GT_WRITE(GT_SCS2HD_OFS, 0x7f);

		/* reconfigure the PCI0 interface view of memory */
		GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000014);
		GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x04000000);
		GT_WRITE(GT_PCI0_BS_SCS10_OFS, 0x03fff000);
		GT_WRITE(GT_PCI0_BS_SCS32_OFS, 0x03fff000);

		/* add the appropriate region */
		add_memory_region(0x40<<20, 0x40<<20, BOOT_MEM_RAM);
		break;
	case 0:
		/* 64MiB */
		GT_WRITE(GT_SCS10HD_OFS, 0x01e00000 >> 21);
		GT_WRITE(GT_SCS32LD_OFS, 0x02000000 >> 21);
		GT_WRITE(GT_SCS32HD_OFS, 0x03e00000 >> 21);

		GT_WRITE(GT_SCS0HD_OFS, 0x1f);
		GT_WRITE(GT_SCS2LD_OFS, 0x20);
		GT_WRITE(GT_SCS2HD_OFS, 0x3f);

		/* reconfigure the PCI0 interface view of memory */
		GT_WRITE(GT_PCI0_CFGADDR_OFS, 0x80000014);
		GT_WRITE(GT_PCI0_CFGDATA_OFS, 0x04000000);
		GT_WRITE(GT_PCI0_BS_SCS10_OFS, 0x01fff000);
		GT_WRITE(GT_PCI0_BS_SCS32_OFS, 0x01fff000);

		break;
	}

	/* Fix up the DiskOnChip mapping */
	GT_WRITE(GT_DEV_B3_OFS, 0xfef73);
}

extern int rm7k_tcache_enabled;
/*
 * This runs in KSEG1. See the verbiage in rm7k.c::probe_scache()
 */
#define Page_Invalidate_T 0x16
static void __init setup_l3cache(unsigned long size)
{
	int register i;
	unsigned long tmp;

	printk("Enabling L3 cache...");

	/* Enable the L3 cache in the GT64120A's CPU Configuration register */
	tmp = GT_READ(GT_CPU_OFS);
	GT_WRITE(GT_CPU_OFS, tmp | (1<<14));

	/* Enable the L3 cache in the CPU */
	set_c0_config(1<<12 /* CONF_TE */);

	/* Clear the cache */
	write_c0_taglo(0);
	write_c0_taghi(0);

	for (i=0; i < size; i+= 4096) {
		__asm__ __volatile__ (
			".set noreorder\n\t"
			".set mips3\n\t"
			"cache %1, (%0)\n\t"
			".set mips0\n\t"
			".set reorder"
			:
			: "r" (KSEG0ADDR(i)),
			  "i" (Page_Invalidate_T));
	}

	/* Let the RM7000 MM code know that the tertiary cache is enabled */
	rm7k_tcache_enabled = 1;

	printk("Done\n");
}


/* This needs to be one of the first initcalls, because no I/O port access
   can work before this */

static int io_base_ioremap(void)
{
	void *io_remap_range = ioremap(GT_PCI_IO_BASE, GT_PCI_IO_SIZE);

	if (!io_remap_range) {
		panic("Could not ioremap I/O port range");
	}
	set_io_port_base(io_remap_range - GT_PCI_IO_BASE);

	return 0;
}

module_init(io_base_ioremap);
