/*
 *	arch/arm/mach-meson6/include/mach/uart.h
 *
 *  Copyright (C) 2013 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block defintions for core devices like the timer.
 * copy from linux kernel
 */

#ifndef __MACH_MESSON_UART_REGS_H
#define __MACH_MESSON_UART_REGS_H

#define UART_AO    0
#define UART_A     1
#define UART_B     2
#define UART_C     3
#define UART_D     4

#define MESON_UART_PORT_NUM 5

#define MESON_UART_NAME "uart_ao","uart_a","uart_b","uart_c","uart_d"
#define MESON_UART_LINE UART_AO,UART_A,UART_B,UART_C,UART_D
#define MESON_UART_FIFO 64,128,64,64,64
#define MESON_UART_ADDRS      ((void *)P_AO_UART_WFIFO),((void *)P_UART0_WFIFO), \
						((void *)P_UART1_WFIFO),((void *)P_UART2_WFIFO),   \
						((void *)P_UART3_WFIFO)
#define MESON_UART_IRQS		INT_UART_AO, INT_UART_0, INT_UART_1, INT_UART_2    \
						, INT_UART_3

#define MESON_UART_CLK_NAME "NULL","uart0","uart1","uart2","uart3"
#endif
