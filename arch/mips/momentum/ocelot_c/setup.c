/*
 * BRIEF MODULE DESCRIPTION
 * Momentum Computer Ocelot-C and -CS board dependent boot routines
 *
 * Copyright (C) 1996, 1997, 2001  Ralf Baechle
 * Copyright (C) 2000 RidgeRun, Inc.
 * Copyright (C) 2001 Red Hat, Inc.
 * Copyright (C) 2002 Momentum Computer
 *
 * Author: Matthew Dharm, Momentum Computer
 *   mdharm@momenco.com
 *
 * Louis Hamilton, Red Hat, Inc.
 *   hamilton@redhat.com  [MIPS64 modifications]
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
#include <linux/config.h>
#include <linux/bcd.h>
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
#include <asm/time.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
#include <asm/mv64340.h>
#include "ocelot_c_fpga.h"

unsigned long marvell_base;
extern unsigned long mv64340_sram_base;
unsigned long cpu_clock;

/* These functions are used for rebooting or halting the machine*/
extern void momenco_ocelot_restart(char *command);
extern void momenco_ocelot_halt(void);
extern void momenco_ocelot_power_off(void);

void momenco_time_init(void);

static char reset_reason;

void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1, unsigned long entryhi, unsigned long pagemask);

static unsigned long ENTRYLO(unsigned long paddr)
{
	return ((paddr & PAGE_MASK) |
	       (_PAGE_PRESENT | __READABLE | __WRITEABLE | _PAGE_GLOBAL |
		_CACHE_UNCACHED)) >> 6;
}

/* setup code for a handoff from a version 2 PMON 2000 PROM */
void PMON_v2_setup(void)
{
	/* Some wired TLB entries for the MV64340 and perhiperals. The
	   MV64340 is going to be hit on every IRQ anyway - there's
	   absolutely no point in letting it be a random TLB entry, as
	   it'll just cause needless churning of the TLB. And we use
	   the other half for the serial port, which is just a PITA
	   otherwise :)

		Device			Physical	Virtual
		MV64340 Internal Regs	0xf4000000	0xf4000000
		Ocelot-C[S] PLD (CS0)	0xfc000000	0xfc000000
		NVRAM (CS1)		0xfc800000	0xfc800000
		UARTs (CS2)		0xfd000000	0xfd000000
		Internal SRAM		0xfe000000	0xfe000000
		M-Systems DOC (CS3)	0xff000000	0xff000000
	*/
  printk("PMON_v2_setup\n");

#ifdef CONFIG_64BIT
	/* marvell and extra space */
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xf4010000), 0xfffffffff4000000, PM_64K);
	/* fpga, rtc, and uart */
	add_wired_entry(ENTRYLO(0xfc000000), ENTRYLO(0xfd000000), 0xfffffffffc000000, PM_16M);
	/* m-sys and internal SRAM */
	add_wired_entry(ENTRYLO(0xfe000000), ENTRYLO(0xff000000), 0xfffffffffe000000, PM_16M);

	marvell_base = 0xfffffffff4000000;
	mv64340_sram_base = 0xfffffffffe000000;
#else
	/* marvell and extra space */
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xf4010000), 0xf4000000, PM_64K);
	/* fpga, rtc, and uart */
	add_wired_entry(ENTRYLO(0xfc000000), ENTRYLO(0xfd000000), 0xfc000000, PM_16M);
	/* m-sys and internal SRAM */
	add_wired_entry(ENTRYLO(0xfe000000), ENTRYLO(0xff000000), 0xfe000000, PM_16M);

	marvell_base = 0xf4000000;
	mv64340_sram_base = 0xfe000000;
#endif
}

unsigned long m48t37y_get_time(void)
{
#ifdef CONFIG_64BIT
	unsigned char *rtc_base = (unsigned char*)0xfffffffffc800000;
#else
	unsigned char* rtc_base = (unsigned char*)0xfc800000;
#endif
	unsigned int year, month, day, hour, min, sec;

	/* stop the update */
	rtc_base[0x7ff8] = 0x40;

	year = BCD2BIN(rtc_base[0x7fff]);
	year += BCD2BIN(rtc_base[0x7ff1]) * 100;

	month = BCD2BIN(rtc_base[0x7ffe]);

	day = BCD2BIN(rtc_base[0x7ffd]);

	hour = BCD2BIN(rtc_base[0x7ffb]);
	min = BCD2BIN(rtc_base[0x7ffa]);
	sec = BCD2BIN(rtc_base[0x7ff9]);

	/* start the update */
	rtc_base[0x7ff8] = 0x00;

	return mktime(year, month, day, hour, min, sec);
}

int m48t37y_set_time(unsigned long sec)
{
#ifdef CONFIG_64BIT
	unsigned char* rtc_base = (unsigned char*)0xfffffffffc800000;
#else
	unsigned char* rtc_base = (unsigned char*)0xfc800000;
#endif
	struct rtc_time tm;

	/* convert to a more useful format -- note months count from 0 */
	to_tm(sec, &tm);
	tm.tm_mon += 1;

	/* enable writing */
	rtc_base[0x7ff8] = 0x80;

	/* year */
	rtc_base[0x7fff] = BIN2BCD(tm.tm_year % 100);
	rtc_base[0x7ff1] = BIN2BCD(tm.tm_year / 100);

	/* month */
	rtc_base[0x7ffe] = BIN2BCD(tm.tm_mon);

	/* day */
	rtc_base[0x7ffd] = BIN2BCD(tm.tm_mday);

	/* hour/min/sec */
	rtc_base[0x7ffb] = BIN2BCD(tm.tm_hour);
	rtc_base[0x7ffa] = BIN2BCD(tm.tm_min);
	rtc_base[0x7ff9] = BIN2BCD(tm.tm_sec);

	/* day of week -- not really used, but let's keep it up-to-date */
	rtc_base[0x7ffc] = BIN2BCD(tm.tm_wday + 1);

	/* disable writing */
	rtc_base[0x7ff8] = 0x00;

	return 0;
}

void momenco_timer_setup(struct irqaction *irq)
{
	setup_irq(7, irq);
}

void momenco_time_init(void)
{
#ifdef CONFIG_CPU_SR71000
	mips_hpt_frequency = cpu_clock;
#elif defined(CONFIG_CPU_RM7000)
	mips_hpt_frequency = cpu_clock / 2;
#else
#error Unknown CPU for this board
#endif
	printk("momenco_time_init cpu_clock=%d\n", cpu_clock);
	board_timer_setup = momenco_timer_setup;

	rtc_get_time = m48t37y_get_time;
	rtc_set_time = m48t37y_set_time;
}

static void __init momenco_ocelot_c_setup(void)
{
	unsigned int tmpword;

	board_time_init = momenco_time_init;

	_machine_restart = momenco_ocelot_restart;
	_machine_halt = momenco_ocelot_halt;
	_machine_power_off = momenco_ocelot_power_off;

	/*
	 * initrd_start = (ulong)ocelot_initrd_start;
	 * initrd_end = (ulong)ocelot_initrd_start + (ulong)ocelot_initrd_size;
	 * initrd_below_start_ok = 1;
	 */

	/* do handoff reconfiguration */
	PMON_v2_setup();

	/* shut down ethernet ports, just to be sure our memory doesn't get
	 * corrupted by random ethernet traffic.
	 */
	MV_WRITE(MV64340_ETH_TRANSMIT_QUEUE_COMMAND_REG(0), 0xff << 8);
	MV_WRITE(MV64340_ETH_TRANSMIT_QUEUE_COMMAND_REG(1), 0xff << 8);
	MV_WRITE(MV64340_ETH_RECEIVE_QUEUE_COMMAND_REG(0), 0xff << 8);
	MV_WRITE(MV64340_ETH_RECEIVE_QUEUE_COMMAND_REG(1), 0xff << 8);
	do {}
	  while (MV_READ(MV64340_ETH_RECEIVE_QUEUE_COMMAND_REG(0)) & 0xff);
	do {}
	  while (MV_READ(MV64340_ETH_RECEIVE_QUEUE_COMMAND_REG(1)) & 0xff);
	do {}
	  while (MV_READ(MV64340_ETH_TRANSMIT_QUEUE_COMMAND_REG(0)) & 0xff);
	do {}
	  while (MV_READ(MV64340_ETH_TRANSMIT_QUEUE_COMMAND_REG(1)) & 0xff);
	MV_WRITE(MV64340_ETH_PORT_SERIAL_CONTROL_REG(0),
	         MV_READ(MV64340_ETH_PORT_SERIAL_CONTROL_REG(0)) & ~1);
	MV_WRITE(MV64340_ETH_PORT_SERIAL_CONTROL_REG(1),
	         MV_READ(MV64340_ETH_PORT_SERIAL_CONTROL_REG(1)) & ~1);

	/* Turn off the Bit-Error LED */
	OCELOT_FPGA_WRITE(0x80, CLR);

	tmpword = OCELOT_FPGA_READ(BOARDREV);
#ifdef CONFIG_CPU_SR71000
	if (tmpword < 26)
		printk("Momenco Ocelot-CS: Board Assembly Rev. %c\n",
			'A'+tmpword);
	else
		printk("Momenco Ocelot-CS: Board Assembly Revision #0x%x\n",
			tmpword);
#else
	if (tmpword < 26)
		printk("Momenco Ocelot-C: Board Assembly Rev. %c\n",
			'A'+tmpword);
	else
		printk("Momenco Ocelot-C: Board Assembly Revision #0x%x\n",
			tmpword);
#endif

	tmpword = OCELOT_FPGA_READ(FPGA_REV);
	printk("FPGA Rev: %d.%d\n", tmpword>>4, tmpword&15);
	tmpword = OCELOT_FPGA_READ(RESET_STATUS);
	printk("Reset reason: 0x%x\n", tmpword);
	switch (tmpword) {
		case 0x1:
			printk("  - Power-up reset\n");
			break;
		case 0x2:
			printk("  - Push-button reset\n");
			break;
		case 0x4:
			printk("  - cPCI bus reset\n");
			break;
		case 0x8:
			printk("  - Watchdog reset\n");
			break;
		case 0x10:
			printk("  - Software reset\n");
			break;
		default:
			printk("  - Unknown reset cause\n");
	}
	reset_reason = tmpword;
	OCELOT_FPGA_WRITE(0xff, RESET_STATUS);

	tmpword = OCELOT_FPGA_READ(CPCI_ID);
	printk("cPCI ID register: 0x%02x\n", tmpword);
	printk("  - Slot number: %d\n", tmpword & 0x1f);
	printk("  - PCI bus present: %s\n", tmpword & 0x40 ? "yes" : "no");
	printk("  - System Slot: %s\n", tmpword & 0x20 ? "yes" : "no");

	tmpword = OCELOT_FPGA_READ(BOARD_STATUS);
	printk("Board Status register: 0x%02x\n", tmpword);
	printk("  - User jumper: %s\n", (tmpword & 0x80)?"installed":"absent");
	printk("  - Boot flash write jumper: %s\n", (tmpword&0x40)?"installed":"absent");
	printk("  - L3 Cache size: %d MiB\n", (1<<((tmpword&12) >> 2))&~1);
	printk("  - SDRAM size: %d MiB\n", 1<<(6+(tmpword&3)));

	switch(tmpword &3) {
	case 3:
		/* 512MiB */
		add_memory_region(0x0, 0x200<<20, BOOT_MEM_RAM);
		break;
	case 2:
		/* 256MiB */
		add_memory_region(0x0, 0x100<<20, BOOT_MEM_RAM);
		break;
	case 1:
		/* 128MiB */
		add_memory_region(0x0,  0x80<<20, BOOT_MEM_RAM);
		break;
	case 0:
		/* 1GiB -- needs CONFIG_HIGHMEM */
		add_memory_region(0x0, 0x400<<20, BOOT_MEM_RAM);
		break;
	}
}

early_initcall(momenco_ocelot_c_setup);

#ifndef CONFIG_64BIT
/* This needs to be one of the first initcalls, because no I/O port access
   can work before this */
static int io_base_ioremap(void)
{
	/* we're mapping PCI accesses from 0xc0000000 to 0xf0000000 */
	void *io_remap_range = ioremap(0xc0000000, 0x30000000);

	if (!io_remap_range) {
		panic("Could not ioremap I/O port range");
	}
	printk("io_remap_range set at 0x%08x\n", (uint32_t)io_remap_range);
	set_io_port_base(io_remap_range - 0xc0000000);

	return 0;
}

module_init(io_base_ioremap);
#endif
