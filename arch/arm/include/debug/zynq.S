/*
 * Debugging macro include header
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#define UART_CR_OFFSET		0x00  /* Control Register [8:0] */
#define UART_SR_OFFSET		0x2C  /* Channel Status [11:0] */
#define UART_FIFO_OFFSET	0x30  /* FIFO [15:0] or [7:0] */

#define UART_SR_TXFULL		0x00000010	/* TX FIFO full */
#define UART_SR_TXEMPTY		0x00000008	/* TX FIFO empty */

#define UART0_PHYS		0xE0000000
#define UART1_PHYS		0xE0001000
#define UART_SIZE		SZ_4K
#define UART_VIRT		0xF0001000

#if IS_ENABLED(CONFIG_DEBUG_ZYNQ_UART1)
# define LL_UART_PADDR		UART1_PHYS
#else
# define LL_UART_PADDR		UART0_PHYS
#endif

#define LL_UART_VADDR		UART_VIRT

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =LL_UART_PADDR	@ physical
		ldr	\rv, =LL_UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_FIFO_OFFSET]	@ TXDATA
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_SR_OFFSET]	@ get status register
		tst	\rd, #UART_SR_TXFULL		@
		bne	1002b			@ wait if FIFO is full
		.endm
