/* serial-regs.h: serial port registers
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_SERIAL_REGS_H
#define _ASM_SERIAL_REGS_H

#include <linux/serial_reg.h>
#include <asm/irc-regs.h>

#define SERIAL_ICLK	33333333	/* the target serial input clock */
#define UART0_BASE	0xfeff9c00
#define UART1_BASE	0xfeff9c40

#define __get_UART0(R) ({ __reg(UART0_BASE + (R) * 8) >> 24; })
#define __get_UART1(R) ({ __reg(UART1_BASE + (R) * 8) >> 24; })
#define __set_UART0(R,V) do { __reg(UART0_BASE + (R) * 8) = (V) << 24; } while(0)
#define __set_UART1(R,V) do { __reg(UART1_BASE + (R) * 8) = (V) << 24; } while(0)

#define __get_UART0_LSR() ({ __get_UART0(UART_LSR); })
#define __get_UART1_LSR() ({ __get_UART1(UART_LSR); })

#define __set_UART0_IER(V) __set_UART0(UART_IER,(V))
#define __set_UART1_IER(V) __set_UART1(UART_IER,(V))

/* serial prescaler select register */
#define __get_UCPSR()	({ *(volatile unsigned long *)(0xfeff9c90); })
#define __set_UCPSR(V)	do { *(volatile unsigned long *)(0xfeff9c90) = (V); } while(0)
#define UCPSR_SELECT0	0x07000000
#define UCPSR_SELECT1	0x38000000

/* serial prescaler base value register */
#define __get_UCPVR()	({ *(volatile unsigned long *)(0xfeff9c98); mb(); })
#define __set_UCPVR(V)	do { *(volatile unsigned long *)(0xfeff9c98) = (V) << 24; mb(); } while(0)


#endif /* _ASM_SERIAL_REGS_H */
