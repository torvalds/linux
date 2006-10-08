/*
 * BRIEF MODULE DESCRIPTION
 * Momentum Computer Ocelot-G (CP7000G) - board dependent boot routines
 *
 * Copyright (C) 1996, 1997, 2001  Ralf Baechle
 * Copyright (C) 2000 RidgeRun, Inc.
 * Copyright (C) 2001 Red Hat, Inc.
 * Copyright (C) 2002 Momentum Computer
 *
 * Author: Matthew Dharm, Momentum Computer
 *   mdharm@momenco.com
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
#include <linux/pm.h>
#include <linux/timex.h>
#include <linux/vmalloc.h>

#include <asm/time.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/gt64240.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <linux/bootmem.h>

#include "ocelot_pld.h"

#ifdef CONFIG_GALILLEO_GT64240_ETH
extern unsigned char prom_mac_addr_base[6];
#endif

unsigned long marvell_base;

/* These functions are used for rebooting or halting the machine*/
extern void momenco_ocelot_restart(char *command);
extern void momenco_ocelot_halt(void);
extern void momenco_ocelot_power_off(void);

extern void gt64240_time_init(void);
extern void momenco_ocelot_irq_setup(void);

static char reset_reason;

static unsigned long ENTRYLO(unsigned long paddr)
{
	return ((paddr & PAGE_MASK) |
	       (_PAGE_PRESENT | __READABLE | __WRITEABLE | _PAGE_GLOBAL |
		_CACHE_UNCACHED)) >> 6;
}

/* setup code for a handoff from a version 2 PMON 2000 PROM */
void PMON_v2_setup(void)
{
	/* A wired TLB entry for the GT64240 and the serial port. The
	   GT64240 is going to be hit on every IRQ anyway - there's
	   absolutely no point in letting it be a random TLB entry, as
	   it'll just cause needless churning of the TLB. And we use
	   the other half for the serial port, which is just a PITA
	   otherwise :)

		Device			Physical	Virtual
		GT64240 Internal Regs	0xf4000000	0xe0000000
		UARTs (CS2)		0xfd000000	0xe0001000
	*/
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xf4010000),
	                0xf4000000, PM_64K);
	add_wired_entry(ENTRYLO(0xfd000000), ENTRYLO(0xfd001000),
	                0xfd000000, PM_4K);

	/* Also a temporary entry to let us talk to the Ocelot PLD and NVRAM
	   in the CS[012] region. We can't use ioremap() yet. The NVRAM
	   is a ST M48T37Y, which includes NVRAM, RTC, and Watchdog functions.

		Ocelot PLD (CS0)	0xfc000000	0xe0020000
		NVRAM (CS1)		0xfc800000	0xe0030000
	*/
	add_temporary_entry(ENTRYLO(0xfc000000), ENTRYLO(0xfc010000),
	                    0xfc000000, PM_64K);
	add_temporary_entry(ENTRYLO(0xfc800000), ENTRYLO(0xfc810000),
	                    0xfc800000, PM_64K);

	marvell_base = 0xf4000000;
}

extern int rm7k_tcache_enabled;

/*
 * This runs in KSEG1. See the verbiage in rm7k.c::probe_scache()
 */
#define Page_Invalidate_T 0x16
static void __init setup_l3cache(unsigned long size)
{
	int register i;

	printk("Enabling L3 cache...");

	/* Enable the L3 cache in the GT64120A's CPU Configuration register */
	MV_WRITE(0, MV_READ(0) | (1<<14));

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

void __init plat_mem_setup(void)
{
	void (*l3func)(unsigned long) = (void *) KSEG1ADDR(setup_l3cache);
	unsigned int tmpword;

	board_time_init = gt64240_time_init;

	_machine_restart = momenco_ocelot_restart;
	_machine_halt = momenco_ocelot_halt;
	pm_power_off = momenco_ocelot_power_off;

	/*
	 * initrd_start = (unsigned long)ocelot_initrd_start;
	 * initrd_end = (unsigned long)ocelot_initrd_start + (ulong)ocelot_initrd_size;
	 * initrd_below_start_ok = 1;
	 */

	/* do handoff reconfiguration */
	PMON_v2_setup();

#ifdef CONFIG_GALILLEO_GT64240_ETH
	/* get the mac addr */
	memcpy(prom_mac_addr_base, (void*)0xfc807cf2, 6);
#endif

	/* Turn off the Bit-Error LED */
	OCELOT_PLD_WRITE(0x80, INTCLR);

	tmpword = OCELOT_PLD_READ(BOARDREV);
	if (tmpword < 26)
		printk("Momenco Ocelot-G: Board Assembly Rev. %c\n", 'A'+tmpword);
	else
		printk("Momenco Ocelot-G: Board Assembly Revision #0x%x\n", tmpword);

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
		/* 512MiB -- two banks of 256MiB */
		add_memory_region(  0x0<<20, 0x100<<20, BOOT_MEM_RAM);
/*
		add_memory_region(0x100<<20, 0x100<<20, BOOT_MEM_RAM);
*/
		break;
	case 2:
		/* 256MiB -- two banks of 128MiB */
		add_memory_region( 0x0<<20, 0x80<<20, BOOT_MEM_RAM);
		add_memory_region(0x80<<20, 0x80<<20, BOOT_MEM_RAM);
		break;
	case 1:
		/* 128MiB -- 64MiB per bank */
		add_memory_region( 0x0<<20, 0x40<<20, BOOT_MEM_RAM);
		add_memory_region(0x40<<20, 0x40<<20, BOOT_MEM_RAM);
		break;
	case 0:
		/* 64MiB */
		add_memory_region( 0x0<<20, 0x40<<20, BOOT_MEM_RAM);
		break;
	}

	/* FIXME: Fix up the DiskOnChip mapping */
	MV_WRITE(0x468, 0xfef73);
}

/* This needs to be one of the first initcalls, because no I/O port access
   can work before this */

static int io_base_ioremap(void)
{
	/* we're mapping PCI accesses from 0xc0000000 to 0xf0000000 */
	unsigned long io_remap_range;

	io_remap_range = (unsigned long) ioremap(0xc0000000, 0x30000000);
	if (!io_remap_range)
		panic("Could not ioremap I/O port range");

	set_io_port_base(io_remap_range - 0xc0000000);

	return 0;
}

module_init(io_base_ioremap);
