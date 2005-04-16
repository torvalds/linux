/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * SEAD specific setup.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/sead.h>
#include <asm/mips-boards/seadint.h>
#include <asm/time.h>

extern void mips_reboot_setup(void);
extern void mips_time_init(void);
extern void mips_timer_setup(struct irqaction *irq);

static void __init serial_init(void);

const char *get_system_type(void)
{
	return "MIPS SEAD";
}

static void __init sead_setup(void)
{
	ioport_resource.end = 0x7fffffff;

	serial_init ();

	board_time_init = mips_time_init;
	board_timer_setup = mips_timer_setup;

	mips_reboot_setup();
}

early_initcall(sead_setup);

static void __init serial_init(void)
{
#ifdef CONFIG_SERIAL_8250
	struct uart_port s;

	memset(&s, 0, sizeof(s));

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	s.iobase = SEAD_UART0_REGS_BASE;
#else
	s.iobase = SEAD_UART0_REGS_BASE+3;
#endif
	s.irq = SEADINT_UART0;
	s.uartclk = SEAD_BASE_BAUD * 16;
	s.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	s.iotype = 0;
	s.regshift = 3;

	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial setup failed!\n");
	}
#endif
}
