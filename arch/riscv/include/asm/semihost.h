/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 tinylab.org
 * Author: Bin Meng <bmeng@tinylab.org>
 */

#ifndef _RISCV_SEMIHOST_H_
#define _RISCV_SEMIHOST_H_

struct uart_port;

static inline void smh_putc(struct uart_port *port, unsigned char c)
{
	asm volatile("addi    a1, %0, 0\n"
		     "addi    a0, zero, 3\n"
		     ".balign 16\n"
		     ".option push\n"
		     ".option norvc\n"
		     "slli    zero, zero, 0x1f\n"
		     "ebreak\n"
		     "srai    zero, zero, 0x7\n"
		     ".option pop\n"
		     : : "r" (&c) : "a0", "a1", "memory");
}

#endif /* _RISCV_SEMIHOST_H_ */
