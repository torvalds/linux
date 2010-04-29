/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
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
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/time.h>
#include <asm/mips-boards/sim.h>
#include <asm/mips-boards/simint.h>


static void __init serial_init(void);
unsigned int _isbonito;

const char *get_system_type(void)
{
	return "MIPSsim";
}

void __init plat_mem_setup(void)
{
	set_io_port_base(0xbfd00000);

	serial_init();
}

extern struct plat_smp_ops ssmtc_smp_ops;

void __init prom_init(void)
{
	set_io_port_base(0xbfd00000);

	prom_meminit();

#ifdef CONFIG_MIPS_MT_SMP
	if (cpu_has_mipsmt)
		register_smp_ops(&vsmp_smp_ops);
	else
		register_smp_ops(&up_smp_ops);
#endif
#ifdef CONFIG_MIPS_MT_SMTC
	if (cpu_has_mipsmt)
		register_smp_ops(&ssmtc_smp_ops);
	else
		register_smp_ops(&up_smp_ops);
#endif
}

static void __init serial_init(void)
{
#ifdef CONFIG_SERIAL_8250
	struct uart_port s;

	memset(&s, 0, sizeof(s));

	s.iobase = 0x3f8;

	/* hardware int 4 - the serial int, is CPU int 6
	 but poll for now */
	s.irq =  0;
	s.uartclk = 1843200;
	s.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	s.iotype = UPIO_PORT;
	s.regshift = 0;
	s.timeout = 4;

	if (early_serial_setup(&s) != 0) {
		printk(KERN_ERR "Serial setup failed!\n");
	}

#endif
}
