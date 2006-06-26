/*
 *  arch/mips/emma2rh/markeins/setup.c
 *      This file is setup for EMMA2RH Mark-eins.
 *
 *  Copyright (C) NEC Electronics Corporation 2004-2006
 *
 *  This file is based on the arch/mips/ddb5xxx/ddb5477/setup.c.
 *
 *	Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/initrd.h>
#include <linux/irq.h>
#include <linux/ide.h>
#include <linux/ioport.h>
#include <linux/param.h>	/* for HZ */
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/time.h>
#include <asm/bcache.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/traps.h>
#include <asm/debug.h>

#include <asm/emma2rh/emma2rh.h>

#define	USE_CPU_COUNTER_TIMER	/* whether we use cpu counter */

extern void markeins_led(const char *);

static int bus_frequency = 0;

static void markeins_machine_restart(char *command)
{
	static void (*back_to_prom) (void) = (void (*)(void))0xbfc00000;

	printk("cannot EMMA2RH Mark-eins restart.\n");
	markeins_led("restart.");
	back_to_prom();
}

static void markeins_machine_halt(void)
{
	printk("EMMA2RH Mark-eins halted.\n");
	markeins_led("halted.");
	while (1) ;
}

static void markeins_machine_power_off(void)
{
	printk("EMMA2RH Mark-eins halted. Please turn off the power.\n");
	markeins_led("poweroff.");
	while (1) ;
}

static unsigned long clock[4] = { 166500000, 187312500, 199800000, 210600000 };

static unsigned int __init detect_bus_frequency(unsigned long rtc_base)
{
	u32 reg;

	/* detect from boot strap */
	reg = emma2rh_in32(EMMA2RH_BHIF_STRAP_0);
	reg = (reg >> 4) & 0x3;
	return clock[reg];
}

static void __init emma2rh_time_init(void)
{
	u32 reg;
	if (bus_frequency == 0)
		bus_frequency = detect_bus_frequency(0);

	reg = emma2rh_in32(EMMA2RH_BHIF_STRAP_0);
	if ((reg & 0x3) == 0)
		reg = (reg >> 6) & 0x3;
	else {
		reg = emma2rh_in32(EMMA2RH_BHIF_MAIN_CTRL);
		reg = (reg >> 4) & 0x3;
	}
	mips_hpt_frequency = (bus_frequency * (4 + reg)) / 4 / 2;
}

static void __init emma2rh_timer_setup(struct irqaction *irq)
{
	/* we are using the cpu counter for timer interrupts */
	setup_irq(CPU_IRQ_BASE + 7, irq);
}

static void markeins_board_init(void);
extern void markeins_irq_setup(void);

static void inline __init markeins_sio_setup(void)
{
#ifdef CONFIG_KGDB_8250
	struct uart_port emma_port;

	memset(&emma_port, 0, sizeof(emma_port));

	emma_port.flags =
	    UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	emma_port.iotype = UPIO_MEM;
	emma_port.regshift = 4;	/* I/O addresses are every 8 bytes */
	emma_port.uartclk = 18544000;	/* Clock rate of the chip */

	emma_port.line = 0;
	emma_port.mapbase = KSEG1ADDR(EMMA2RH_PFUR0_BASE + 3);
	emma_port.membase = (u8*)emma_port.mapbase;
	early_serial_setup(&emma_port);

	emma_port.line = 1;
	emma_port.mapbase = KSEG1ADDR(EMMA2RH_PFUR1_BASE + 3);
	emma_port.membase = (u8*)emma_port.mapbase;
	early_serial_setup(&emma_port);

	emma_port.irq = EMMA2RH_IRQ_PFUR1;
	kgdb8250_add_port(1, &emma_port);
#endif
}

void __init plat_mem_setup(void)
{
	/* initialize board - we don't trust the loader */
	markeins_board_init();

	set_io_port_base(KSEG1ADDR(EMMA2RH_PCI_IO_BASE));

	board_time_init = emma2rh_time_init;
	board_timer_setup = emma2rh_timer_setup;

	_machine_restart = markeins_machine_restart;
	_machine_halt = markeins_machine_halt;
	pm_power_off = markeins_machine_power_off;

	/* setup resource limits */
	ioport_resource.start = EMMA2RH_PCI_IO_BASE;
	ioport_resource.end = EMMA2RH_PCI_IO_BASE + EMMA2RH_PCI_IO_SIZE - 1;
	iomem_resource.start = EMMA2RH_IO_BASE;
	iomem_resource.end = EMMA2RH_ROM_BASE - 1;

	/* Reboot on panic */
	panic_timeout = 180;

	markeins_sio_setup();
}

static void __init markeins_board_init(void)
{
	u32 val;

	val = emma2rh_in32(EMMA2RH_PBRD_INT_EN);	/* open serial interrupts. */
	emma2rh_out32(EMMA2RH_PBRD_INT_EN, val | 0xaa);
	val = emma2rh_in32(EMMA2RH_PBRD_CLKSEL);	/* set serial clocks. */
	emma2rh_out32(EMMA2RH_PBRD_CLKSEL, val | 0x5);	/* 18MHz */
	emma2rh_out32(EMMA2RH_PCI_CONTROL, 0);

	markeins_led("MVL E2RH");
}
