/*
 * arch/ppc/platforms/4xx/virtex-ii_pro.h
 *
 * Include file that defines the Xilinx Virtex-II Pro processor
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_VIRTEXIIPRO_H__
#define __ASM_VIRTEXIIPRO_H__

#include <linux/config.h>
#include <asm/xparameters.h>

/* serial defines */

#define RS_TABLE_SIZE  4	/* change this and add more devices below
				   if you have more then 4 16x50 UARTs */

#define BASE_BAUD		(XPAR_UARTNS550_0_CLOCK_FREQ_HZ/16)

/* The serial ports in the Virtex-II Pro have each I/O byte in the
 * LSByte of a word.  This means that iomem_reg_shift needs to be 2 to
 * change the byte offsets into word offsets.  In addition the base
 * addresses need to have 3 added to them to get to the LSByte.
 */
#define STD_UART_OP(num)						 \
	{ 0, BASE_BAUD, 0, XPAR_INTC_0_UARTNS550_##num##_VEC_ID,	 \
		ASYNC_BOOT_AUTOCONF,		 			 \
		.iomem_base = (u8 *)XPAR_UARTNS550_##num##_BASEADDR + 3, \
		.iomem_reg_shift = 2,					 \
		.io_type = SERIAL_IO_MEM},

#if defined(XPAR_INTC_0_UARTNS550_0_VEC_ID)
#define ML300_UART0 STD_UART_OP(0)
#else
#define ML300_UART0
#endif

#if defined(XPAR_INTC_0_UARTNS550_1_VEC_ID)
#define ML300_UART1 STD_UART_OP(1)
#else
#define ML300_UART1
#endif

#if defined(XPAR_INTC_0_UARTNS550_2_VEC_ID)
#define ML300_UART2 STD_UART_OP(2)
#else
#define ML300_UART2
#endif

#if defined(XPAR_INTC_0_UARTNS550_3_VEC_ID)
#define ML300_UART3 STD_UART_OP(3)
#else
#define ML300_UART3
#endif

#if defined(XPAR_INTC_0_UARTNS550_4_VEC_ID)
#error Edit this file to add more devices.
#elif defined(XPAR_INTC_0_UARTNS550_3_VEC_ID)
#define NR_SER_PORTS	4
#elif defined(XPAR_INTC_0_UARTNS550_2_VEC_ID)
#define NR_SER_PORTS	3
#elif defined(XPAR_INTC_0_UARTNS550_1_VEC_ID)
#define NR_SER_PORTS	2
#elif defined(XPAR_INTC_0_UARTNS550_0_VEC_ID)
#define NR_SER_PORTS	1
#else
#define NR_SER_PORTS	0
#endif

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_PORT_DFNS	\
	ML300_UART0		\
	ML300_UART1		\
	ML300_UART2		\
	ML300_UART3
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_PORT_DFNS	\
	ML300_UART1		\
	ML300_UART0		\
	ML300_UART2		\
	ML300_UART3
#endif

#define DCRN_CPMFR_BASE	0

#include <asm/ibm405.h>

#endif				/* __ASM_VIRTEXIIPRO_H__ */
#endif				/* __KERNEL__ */
