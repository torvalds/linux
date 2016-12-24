/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Adapted for ARM and earlycon:
 * Copyright (C) 2014 Linaro Ltd.
 * Author: Rob Herring <robh@kernel.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_core.h>

#ifdef CONFIG_THUMB2_KERNEL
#define SEMIHOST_SWI	"0xab"
#else
#define SEMIHOST_SWI	"0x123456"
#endif

/*
 * Semihosting-based debug console
 */
static void smh_putc(struct uart_port *port, int c)
{
#ifdef CONFIG_ARM64
	asm volatile("mov  x1, %0\n"
		     "mov  x0, #3\n"
		     "hlt  0xf000\n"
		     : : "r" (&c) : "x0", "x1", "memory");
#else
	asm volatile("mov  r1, %0\n"
		     "mov  r0, #3\n"
		     "svc  " SEMIHOST_SWI "\n"
		     : : "r" (&c) : "r0", "r1", "memory");
#endif
}

static void smh_write(struct console *con, const char *s, unsigned n)
{
	struct earlycon_device *dev = con->data;
	uart_console_write(&dev->port, s, n, smh_putc);
}

static int
__init early_smh_setup(struct earlycon_device *device, const char *opt)
{
	device->con->write = smh_write;
	return 0;
}
EARLYCON_DECLARE(smh, early_smh_setup);
