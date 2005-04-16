/*
 * setup.c
 *
 * BRIEF MODULE DESCRIPTION
 * Momentum Computer Ocelot-3 board dependent boot routines
 *
 * Copyright (C) 1996, 1997, 01, 05  Ralf Baechle
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
 * Copyright 2004 PMC-Sierra
 * Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Manish Lachwani, mlachwani@mvista.com
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
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mc146818rtc.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <linux/bootmem.h>
#include <linux/mv643xx.h>
#include <asm/time.h>
#include <asm/page.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/mc146818rtc.h>
#include <asm/tlbflush.h>
#include "ocelot_3_fpga.h"

/* Marvell Discovery Register Base */
unsigned long marvell_base = (signed)0xf4000000;

/* CPU clock */
unsigned long cpu_clock;

/* RTC/NVRAM */
unsigned char* rtc_base = (unsigned char*)(signed)0xfc800000;

/* FPGA Base */
unsigned long ocelot_fpga_base = (signed)0xfc000000;

/* Serial base */
unsigned long uart_base = (signed)0xfd000000;

/*
 * Marvell Discovery SRAM. This is one place where Ethernet
 * Tx and Rx descriptors can be placed to improve performance
 */
extern unsigned long mv64340_sram_base;

/* These functions are used for rebooting or halting the machine*/
extern void momenco_ocelot_restart(char *command);
extern void momenco_ocelot_halt(void);
extern void momenco_ocelot_power_off(void);

void momenco_time_init(void);
static char reset_reason;

void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
		     unsigned long entryhi, unsigned long pagemask);

static inline unsigned long ENTRYLO(unsigned long paddr)
{
	return ((paddr & PAGE_MASK) |
		(_PAGE_PRESENT | __READABLE | __WRITEABLE | _PAGE_GLOBAL |
		_CACHE_UNCACHED)) >> 6;
}

void __init bus_error_init(void)
{
	/* nothing */
}

/*
 * setup code for a handoff from a version 2 PMON 2000 PROM
 */
void setup_wired_tlb_entries(void)
{
	write_c0_wired(0);
	local_flush_tlb_all();

	/* marvell and extra space */
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xf4010000), (signed)0xf4000000, PM_64K);

	/* fpga, rtc, and uart */
	add_wired_entry(ENTRYLO(0xfc000000), ENTRYLO(0xfd000000), (signed)0xfc000000, PM_16M);
}

#define CONV_BCD_TO_BIN(val)	(((val) & 0xf) + (((val) >> 4) * 10))
#define CONV_BIN_TO_BCD(val)	(((val) % 10) + (((val) / 10) << 4))

unsigned long m48t37y_get_time(void)
{
	unsigned int year, month, day, hour, min, sec;

	/* stop the update */
	rtc_base[0x7ff8] = 0x40;

	year = CONV_BCD_TO_BIN(rtc_base[0x7fff]);
	year += CONV_BCD_TO_BIN(rtc_base[0x7ff1]) * 100;

	month = CONV_BCD_TO_BIN(rtc_base[0x7ffe]);

	day = CONV_BCD_TO_BIN(rtc_base[0x7ffd]);

	hour = CONV_BCD_TO_BIN(rtc_base[0x7ffb]);
	min = CONV_BCD_TO_BIN(rtc_base[0x7ffa]);
	sec = CONV_BCD_TO_BIN(rtc_base[0x7ff9]);

	/* start the update */
	rtc_base[0x7ff8] = 0x00;

	return mktime(year, month, day, hour, min, sec);
}

int m48t37y_set_time(unsigned long sec)
{
	struct rtc_time tm;

	/* convert to a more useful format -- note months count from 0 */
	to_tm(sec, &tm);
	tm.tm_mon += 1;

	/* enable writing */
	rtc_base[0x7ff8] = 0x80;

	/* year */
	rtc_base[0x7fff] = CONV_BIN_TO_BCD(tm.tm_year % 100);
	rtc_base[0x7ff1] = CONV_BIN_TO_BCD(tm.tm_year / 100);

	/* month */
	rtc_base[0x7ffe] = CONV_BIN_TO_BCD(tm.tm_mon);

	/* day */
	rtc_base[0x7ffd] = CONV_BIN_TO_BCD(tm.tm_mday);

	/* hour/min/sec */
	rtc_base[0x7ffb] = CONV_BIN_TO_BCD(tm.tm_hour);
	rtc_base[0x7ffa] = CONV_BIN_TO_BCD(tm.tm_min);
	rtc_base[0x7ff9] = CONV_BIN_TO_BCD(tm.tm_sec);

	/* day of week -- not really used, but let's keep it up-to-date */
	rtc_base[0x7ffc] = CONV_BIN_TO_BCD(tm.tm_wday + 1);

	/* disable writing */
	rtc_base[0x7ff8] = 0x00;

	return 0;
}

void momenco_timer_setup(struct irqaction *irq)
{
	setup_irq(7, irq);	/* Timer interrupt, unmask status IM7 */
}

void momenco_time_init(void)
{
	setup_wired_tlb_entries();

	/*
	 * Ocelot-3 board has been built with both
	 * the Rm7900 and the Rm7065C
	 */
	mips_hpt_frequency = cpu_clock / 2;
	board_timer_setup = momenco_timer_setup;

	rtc_get_time = m48t37y_get_time;
	rtc_set_time = m48t37y_set_time;
}

/*
 * PCI Support for Ocelot-3
 */

/* Bus #0 IO and MEM space */
#define	OCELOT_3_PCI_IO_0_START		0xe0000000
#define	OCELOT_3_PCI_IO_0_SIZE		0x08000000
#define	OCELOT_3_PCI_MEM_0_START	0xc0000000
#define	OCELOT_3_PCI_MEM_0_SIZE		0x10000000

/* Bus #1 IO and MEM space */
#define	OCELOT_3_PCI_IO_1_START		0xe8000000
#define	OCELOT_3_PCI_IO_1_SIZE		0x08000000
#define	OCELOT_3_PCI_MEM_1_START	0xd0000000
#define	OCELOT_3_PCI_MEM_1_SIZE		0x10000000

static struct resource mv_pci_io_mem0_resource = {
	.name	= "MV64340 PCI0 IO MEM",
	.start	= OCELOT_3_PCI_IO_0_START,
	.end	= OCELOT_3_PCI_IO_0_START + OCELOT_3_PCI_IO_0_SIZE - 1,
	.flags  = IORESOURCE_IO,
};

static struct resource mv_pci_io_mem1_resource = {
	.name	= "MV64340 PCI1 IO MEM",
	.start	= OCELOT_3_PCI_IO_1_START,
	.end	= OCELOT_3_PCI_IO_1_START + OCELOT_3_PCI_IO_1_SIZE - 1,
	.flags	= IORESOURCE_IO,
};

static struct resource mv_pci_mem0_resource = {
	.name	= "MV64340 PCI0 MEM",
	.start	= OCELOT_3_PCI_MEM_0_START,
	.end	= OCELOT_3_PCI_MEM_0_START + OCELOT_3_PCI_MEM_0_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static struct resource mv_pci_mem1_resource = {
	.name	= "MV64340 PCI1 MEM",
	.start	= OCELOT_3_PCI_MEM_1_START,
	.end	= OCELOT_3_PCI_MEM_1_START + OCELOT_3_PCI_MEM_1_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

static struct mv_pci_controller mv_bus0_controller = {
	.pcic = {
		 .pci_ops	= &mv_pci_ops,
		 .mem_resource	= &mv_pci_mem0_resource,
		 .io_resource	= &mv_pci_io_mem0_resource,
	},
	.config_addr	= MV64340_PCI_0_CONFIG_ADDR,
	.config_vreg	= MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG,
};

static struct mv_pci_controller mv_bus1_controller = {
	.pcic = {
		 .pci_ops	= &mv_pci_ops,
		 .mem_resource	= &mv_pci_mem1_resource,
		 .io_resource	= &mv_pci_io_mem1_resource,
	},
	.config_addr	= MV64340_PCI_1_CONFIG_ADDR,
	.config_vreg	= MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG,
};

static __init int __init ja_pci_init(void)
{
	uint32_t enable;
	extern int pci_probe_only;

	/* PMON will assign PCI resources */
	pci_probe_only = 1;

	enable = ~MV_READ(MV64340_BASE_ADDR_ENABLE);
	/*
	 * We require at least one enabled I/O or PCI memory window or we
	 * will ignore this PCI bus.  We ignore PCI windows 1, 2 and 3.
	 */
	if (enable & (0x01 <<  9) || enable & (0x01 << 10))
		register_pci_controller(&mv_bus0_controller.pcic);

	if (enable & (0x01 << 14) || enable & (0x01 << 15))
		register_pci_controller(&mv_bus1_controller.pcic);

	ioport_resource.end = OCELOT_3_PCI_IO_0_START + OCELOT_3_PCI_IO_0_SIZE +
					OCELOT_3_PCI_IO_1_SIZE - 1;

	iomem_resource.end = OCELOT_3_PCI_MEM_0_START + OCELOT_3_PCI_MEM_0_SIZE +
					OCELOT_3_PCI_MEM_1_SIZE - 1;

	set_io_port_base(OCELOT_3_PCI_IO_0_START); /* mips_io_port_base */

	return 0;
}

arch_initcall(ja_pci_init);

static int __init momenco_ocelot_3_setup(void)
{
	unsigned int tmpword;

	board_time_init = momenco_time_init;

	_machine_restart = momenco_ocelot_restart;
	_machine_halt = momenco_ocelot_halt;
	_machine_power_off = momenco_ocelot_power_off;

	/* Wired TLB entries */
	setup_wired_tlb_entries();

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
	if (tmpword < 26)
		printk("Momenco Ocelot-3: Board Assembly Rev. %c\n",
			'A'+tmpword);
	else
		printk("Momenco Ocelot-3: Board Assembly Revision #0x%x\n",
			tmpword);

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
	printk("  - L3 cache size: %d MB\n", (1<<((tmpword&12) >> 2))&~1);

	/* Support for 128 MB memory */
	add_memory_region(0x0, 0x08000000, BOOT_MEM_RAM);

	return 0;
}

early_initcall(momenco_ocelot_3_setup);
