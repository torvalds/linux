/*
 * BRIEF MODULE DESCRIPTION
 * Momentum Computer Jaguar-ATX board dependent boot routines
 *
 * Copyright (C) 1996, 1997, 2001, 04, 06  Ralf Baechle (ralf@linux-mips.org)
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
 */
#include <linux/config.h>
#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/swap.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/vmalloc.h>
#include <linux/mv643xx.h>

#include <asm/time.h>
#include <asm/bootinfo.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/reboot.h>
#include <asm/tlbflush.h>

#include "jaguar_atx_fpga.h"

extern unsigned long mv64340_sram_base;
unsigned long cpu_clock;

/* These functions are used for rebooting or halting the machine*/
extern void momenco_jaguar_restart(char *command);
extern void momenco_jaguar_halt(void);
extern void momenco_jaguar_power_off(void);

void momenco_time_init(void);

static char reset_reason;

static inline unsigned long ENTRYLO(unsigned long paddr)
{
	return ((paddr & PAGE_MASK) |
	       (_PAGE_PRESENT | __READABLE | __WRITEABLE | _PAGE_GLOBAL |
		_CACHE_UNCACHED)) >> 6;
}

void __init bus_error_init(void) { /* nothing */ }

/*
 * Load a few TLB entries for the MV64340 and perhiperals. The MV64340 is going
 * to be hit on every IRQ anyway - there's absolutely no point in letting it be
 * a random TLB entry, as it'll just cause needless churning of the TLB. And we
 * use the other half for the serial port, which is just a PITA otherwise :)
 *
 *	Device			Physical	Virtual
 *	MV64340 Internal Regs	0xf4000000	0xf4000000
 *	Ocelot-C[S] PLD (CS0)	0xfc000000	0xfc000000
 *	NVRAM (CS1)		0xfc800000	0xfc800000
 *	UARTs (CS2)		0xfd000000	0xfd000000
 *	Internal SRAM		0xfe000000	0xfe000000
 *	M-Systems DOC (CS3)	0xff000000	0xff000000
 */

static __init void wire_stupidity_into_tlb(void)
{
#ifdef CONFIG_32BIT
	write_c0_wired(0);
	local_flush_tlb_all();

	/* marvell and extra space */
	add_wired_entry(ENTRYLO(0xf4000000), ENTRYLO(0xf4010000),
	                0xf4000000UL, PM_64K);
	/* fpga, rtc, and uart */
	add_wired_entry(ENTRYLO(0xfc000000), ENTRYLO(0xfd000000),
	                0xfc000000UL, PM_16M);
//	/* m-sys and internal SRAM */
//	add_wired_entry(ENTRYLO(0xfe000000), ENTRYLO(0xff000000),
//	                0xfe000000UL, PM_16M);

	marvell_base = 0xf4000000;
	//mv64340_sram_base = 0xfe000000;	/* Currently unused */
#endif
}

unsigned long marvell_base	= 0xf4000000L;
unsigned long ja_fpga_base	= JAGUAR_ATX_CS0_ADDR;
unsigned long uart_base		= 0xfd000000L;
static unsigned char *rtc_base	= (unsigned char*) 0xfc800000L;

EXPORT_SYMBOL(marvell_base);

static __init int per_cpu_mappings(void)
{
	marvell_base	= (unsigned long) ioremap(0xf4000000, 0x10000);
	ja_fpga_base	= (unsigned long) ioremap(JAGUAR_ATX_CS0_ADDR,  0x1000);
	uart_base	= (unsigned long) ioremap(0xfd000000UL, 0x1000);
	rtc_base	= ioremap(0xfc000000UL, 0x8000);
	// ioremap(0xfe000000,  32 << 20);
	write_c0_wired(0);
	local_flush_tlb_all();
	ja_setup_console();

	return 0;
}
arch_initcall(per_cpu_mappings);

unsigned long m48t37y_get_time(void)
{
	unsigned int year, month, day, hour, min, sec;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
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
	spin_unlock_irqrestore(&rtc_lock, flags);

	return mktime(year, month, day, hour, min, sec);
}

int m48t37y_set_time(unsigned long sec)
{
	struct rtc_time tm;
	unsigned long flags;

	/* convert to a more useful format -- note months count from 0 */
	to_tm(sec, &tm);
	tm.tm_mon += 1;

	spin_lock_irqsave(&rtc_lock, flags);
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
	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}

void momenco_timer_setup(struct irqaction *irq)
{
	setup_irq(8, irq);
}

/*
 * Ugly but the least of all evils.  TLB initialization did flush the TLB so
 * We need to setup mappings again before we can touch the RTC.
 */
void momenco_time_init(void)
{
	wire_stupidity_into_tlb();

	mips_hpt_frequency = cpu_clock / 2;
	board_timer_setup = momenco_timer_setup;

	rtc_mips_get_time = m48t37y_get_time;
	rtc_mips_set_time = m48t37y_set_time;
}

static struct resource mv_pci_io_mem0_resource = {
	.name	= "MV64340 PCI0 IO MEM",
	.flags	= IORESOURCE_IO
};

static struct resource mv_pci_mem0_resource = {
	.name	= "MV64340 PCI0 MEM",
	.flags	= IORESOURCE_MEM
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

static uint32_t mv_io_base, mv_io_size;

static void ja_pci0_init(void)
{
	uint32_t mem0_base, mem0_size;
	uint32_t io_base, io_size;

	io_base = MV_READ(MV64340_PCI_0_IO_BASE_ADDR) << 16;
	io_size = (MV_READ(MV64340_PCI_0_IO_SIZE) + 1) << 16;
	mem0_base = MV_READ(MV64340_PCI_0_MEMORY0_BASE_ADDR) << 16;
	mem0_size = (MV_READ(MV64340_PCI_0_MEMORY0_SIZE) + 1) << 16;

	mv_pci_io_mem0_resource.start		= 0;
	mv_pci_io_mem0_resource.end		= io_size - 1;
	mv_pci_mem0_resource.start		= mem0_base;
	mv_pci_mem0_resource.end		= mem0_base + mem0_size - 1;
	mv_bus0_controller.pcic.mem_offset	= mem0_base;
	mv_bus0_controller.pcic.io_offset	= 0;

	ioport_resource.end		= io_size - 1;

	register_pci_controller(&mv_bus0_controller.pcic);

	mv_io_base = io_base;
	mv_io_size = io_size;
}

static struct resource mv_pci_io_mem1_resource = {
	.name	= "MV64340 PCI1 IO MEM",
	.flags	= IORESOURCE_IO
};

static struct resource mv_pci_mem1_resource = {
	.name	= "MV64340 PCI1 MEM",
	.flags	= IORESOURCE_MEM
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

static __init void ja_pci1_init(void)
{
	uint32_t mem0_base, mem0_size;
	uint32_t io_base, io_size;

	io_base = MV_READ(MV64340_PCI_1_IO_BASE_ADDR) << 16;
	io_size = (MV_READ(MV64340_PCI_1_IO_SIZE) + 1) << 16;
	mem0_base = MV_READ(MV64340_PCI_1_MEMORY0_BASE_ADDR) << 16;
	mem0_size = (MV_READ(MV64340_PCI_1_MEMORY0_SIZE) + 1) << 16;

	/*
	 * Here we assume the I/O window of second bus to be contiguous with
	 * the first.  A gap is no problem but would waste address space for
	 * remapping the port space.
	 */
	mv_pci_io_mem1_resource.start		= mv_io_size;
	mv_pci_io_mem1_resource.end		= mv_io_size + io_size - 1;
	mv_pci_mem1_resource.start		= mem0_base;
	mv_pci_mem1_resource.end		= mem0_base + mem0_size - 1;
	mv_bus1_controller.pcic.mem_offset	= mem0_base;
	mv_bus1_controller.pcic.io_offset	= 0;

	ioport_resource.end		= io_base + io_size -mv_io_base - 1;

	register_pci_controller(&mv_bus1_controller.pcic);

	mv_io_size = io_base + io_size - mv_io_base;
}

static __init int __init ja_pci_init(void)
{
	unsigned long io_v_base;
	uint32_t enable;

	enable = ~MV_READ(MV64340_BASE_ADDR_ENABLE);

	/*
	 * We require at least one enabled I/O or PCI memory window or we
	 * will ignore this PCI bus.  We ignore PCI windows 1, 2 and 3.
	 */
	if (enable & (0x01 <<  9) || enable & (0x01 << 10))
		ja_pci0_init();

	if (enable & (0x01 << 14) || enable & (0x01 << 15))
		ja_pci1_init();

	if (mv_io_size) {
		io_v_base = (unsigned long) ioremap(mv_io_base, mv_io_size);
		if (!io_v_base)
			panic("Could not ioremap I/O port range");

		set_io_port_base(io_v_base);
	}

	return 0;
}

arch_initcall(ja_pci_init);

void __init plat_setup(void)
{
	unsigned int tmpword;

	board_time_init = momenco_time_init;

	_machine_restart = momenco_jaguar_restart;
	_machine_halt = momenco_jaguar_halt;
	pm_power_off = momenco_jaguar_power_off;

	/*
	 * initrd_start = (ulong)jaguar_initrd_start;
	 * initrd_end = (ulong)jaguar_initrd_start + (ulong)jaguar_initrd_size;
	 * initrd_below_start_ok = 1;
	 */

	wire_stupidity_into_tlb();

	/*
	 * shut down ethernet ports, just to be sure our memory doesn't get
	 * corrupted by random ethernet traffic.
	 */
	MV_WRITE(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(0), 0xff << 8);
	MV_WRITE(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(1), 0xff << 8);
	MV_WRITE(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(2), 0xff << 8);
	MV_WRITE(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(0), 0xff << 8);
	MV_WRITE(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(1), 0xff << 8);
	MV_WRITE(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(2), 0xff << 8);
	while (MV_READ(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(0)) & 0xff);
	while (MV_READ(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(1)) & 0xff);
	while (MV_READ(MV643XX_ETH_RECEIVE_QUEUE_COMMAND_REG(2)) & 0xff);
	while (MV_READ(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(0)) & 0xff);
	while (MV_READ(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(1)) & 0xff);
	while (MV_READ(MV643XX_ETH_TRANSMIT_QUEUE_COMMAND_REG(2)) & 0xff);
	MV_WRITE(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(0),
	         MV_READ(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(0)) & ~1);
	MV_WRITE(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(1),
	         MV_READ(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(1)) & ~1);
	MV_WRITE(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(2),
	         MV_READ(MV643XX_ETH_PORT_SERIAL_CONTROL_REG(2)) & ~1);

	/* Turn off the Bit-Error LED */
	JAGUAR_FPGA_WRITE(0x80, CLR);

	tmpword = JAGUAR_FPGA_READ(BOARDREV);
	if (tmpword < 26)
		printk("Momentum Jaguar-ATX: Board Assembly Rev. %c\n",
			'A'+tmpword);
	else
		printk("Momentum Jaguar-ATX: Board Assembly Revision #0x%x\n",
			tmpword);

	tmpword = JAGUAR_FPGA_READ(FPGA_REV);
	printk("FPGA Rev: %d.%d\n", tmpword>>4, tmpword&15);
	tmpword = JAGUAR_FPGA_READ(RESET_STATUS);
	printk("Reset reason: 0x%x\n", tmpword);
	switch (tmpword) {
	case 0x1:
		printk("  - Power-up reset\n");
		break;
	case 0x2:
		printk("  - Push-button reset\n");
		break;
	case 0x8:
		printk("  - Watchdog reset\n");
		break;
	case 0x10:
		printk("  - JTAG reset\n");
		break;
	default:
		printk("  - Unknown reset cause\n");
	}
	reset_reason = tmpword;
	JAGUAR_FPGA_WRITE(0xff, RESET_STATUS);

	tmpword = JAGUAR_FPGA_READ(BOARD_STATUS);
	printk("Board Status register: 0x%02x\n", tmpword);
	printk("  - User jumper: %s\n", (tmpword & 0x80)?"installed":"absent");
	printk("  - Boot flash write jumper: %s\n", (tmpword&0x40)?"installed":"absent");

	/* 256MiB of RM9000x2 DDR */
//	add_memory_region(0x0, 0x100<<20, BOOT_MEM_RAM);

	/* 128MiB of MV-64340 DDR */
//	add_memory_region(0x100<<20, 0x80<<20, BOOT_MEM_RAM);

	/* XXX Memory configuration should be picked up from PMON2k */
#ifdef CONFIG_JAGUAR_DMALOW
	printk("Jaguar ATX DMA-low mode set\n");
	add_memory_region(0x00000000, 0x08000000, BOOT_MEM_RAM);
	add_memory_region(0x08000000, 0x10000000, BOOT_MEM_RAM);
#else
	/* 128MiB of MV-64340 DDR RAM */
	printk("Jaguar ATX DMA-low mode is not set\n");
	add_memory_region(0x100<<20, 0x80<<20, BOOT_MEM_RAM);
#endif

#ifdef GEMDEBUG_TRACEBUFFER
	{
	  unsigned int tbControl;
	  tbControl =
	    0 << 26 |  /* post trigger delay 0 */
		    0x2 << 16 |		/* sequential trace mode */
	    //	    0x0 << 16 |		/* non-sequential trace mode */
	    //	    0xf << 4 |		/* watchpoints disabled */
	    2 << 2 |		/* armed */
	    2 ;			/* interrupt disabled  */
	  printk ("setting     tbControl = %08lx\n", tbControl);
	  write_32bit_cp0_set1_register($22, tbControl);
	  __asm__ __volatile__(".set noreorder\n\t" \
			       "nop; nop; nop; nop; nop; nop;\n\t" \
			       "nop; nop; nop; nop; nop; nop;\n\t" \
			       ".set reorder\n\t");

	}
#endif
}
