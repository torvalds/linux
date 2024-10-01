// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Adapted for ARM and earlycon:
 * Copyright (C) 2014 Linaro Ltd.
 * Author: Rob Herring <robh@kernel.org>
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
static void smh_putc(struct uart_port *port, unsigned char c)
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
