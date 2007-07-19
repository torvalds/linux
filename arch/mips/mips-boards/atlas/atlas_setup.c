/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/atlasint.h>
#include <asm/time.h>
#include <asm/traps.h>

extern void mips_reboot_setup(void);
extern void mips_time_init(void);
extern unsigned long mips_rtc_get_time(void);

#ifdef CONFIG_KGDB
extern void kgdb_config(void);
#endif

static void __init serial_init(void);

const char *get_system_type(void)
{
	return "MIPS Atlas";
}

const char display_string[] = "        LINUX ON ATLAS       ";

void __init plat_mem_setup(void)
{
	mips_pcibios_init();

	ioport_resource.end = 0x7fffffff;

	serial_init ();

#ifdef CONFIG_KGDB
	kgdb_config();
#endif
	mips_reboot_setup();

	board_time_init = mips_time_init;
	rtc_mips_get_time = mips_rtc_get_time;
}

static void __init serial_init(void)
{
#ifdef CONFIG_SERIAL_8250
	struct uart_port s;

	memset(&s, 0, sizeof(s));

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	s.iobase = ATLAS_UART_REGS_BASE;
#else
	s.iobase = ATLAS_UART_REGS_BASE+3;
#endif
	s.irq = ATLAS_INT_UART;
	s.uartclk = ATLAS_BASE_BAUD * 16;
	s.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ;
	s.iotype = UPIO_PORT;
	s.regshift = 3;

	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial setup failed!\n");
	}
#endif
}
