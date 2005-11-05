/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2002 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/termios.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/page.h>
#include <asm/semaphore.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/sn_private.h>

#include <linux/serial.h>
#include <linux/serial_core.h>

#define IOC3_CLK	(22000000 / 3)
#define IOC3_FLAGS	(0)

static inline struct ioc3_uartregs *console_uart(void)
{
	struct ioc3 *ioc3;
	nasid_t nasid;

	nasid = (master_nasid == INVALID_NASID) ? get_nasid() : master_nasid;
	ioc3 = (struct ioc3 *)KL_CONFIG_CH_CONS_INFO(nasid)->memory_base;

	return &ioc3->sregs.uarta;
}

void prom_putchar(char c)
{
	struct ioc3_uartregs *uart = console_uart();

	while ((uart->iu_lsr & 0x20) == 0);
	uart->iu_thr = c;
}

char __init prom_getchar(void)
{
	return 0;
}

static void inline ioc3_console_probe(void)
{
	struct uart_port up;

	/*
	 * Register to interrupt zero because we share the interrupt with
	 * the serial driver which we don't properly support yet.
	 */
	memset(&up, 0, sizeof(up));
	up.membase	= (unsigned char *) console_uart();
	up.irq		= 0;
	up.uartclk	= IOC3_CLK;
	up.regshift	= 0;
	up.iotype	= UPIO_MEM;
	up.flags	= IOC3_FLAGS;
	up.line		= 0;

	if (early_serial_setup(&up))
		printk(KERN_ERR "Early serial init of port 0 failed\n");
}

__init void ip27_setup_console(void)
{
	ioc3_console_probe();
}
