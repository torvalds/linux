/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Adapted for ARM and earlycon:
 * Copyright (C) 2014 Linaro Ltd.
 * Author: Rob Herring <robh@kernel.org>
 */

#ifndef _ARM_SEMIHOST_H_
#define _ARM_SEMIHOST_H_

#ifdef CONFIG_THUMB2_KERNEL
#define SEMIHOST_SWI	"0xab"
#else
#define SEMIHOST_SWI	"0x123456"
#endif

struct uart_port;

static inline void smh_putc(struct uart_port *port, unsigned char c)
{
	asm volatile("mov  r1, %0\n"
		     "mov  r0, #3\n"
		     "svc  " SEMIHOST_SWI "\n"
		     : : "r" (&c) : "r0", "r1", "memory");
}

#endif /* _ARM_SEMIHOST_H_ */
