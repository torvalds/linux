/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Debugging macro include header
 *
 *  Copyright (C) 2011 Xilinx
 */
#define UART_CR_OFFSET		0x00  /* Control Register [8:0] */
#define UART_SR_OFFSET		0x2C  /* Channel Status [11:0] */
#define UART_FIFO_OFFSET	0x30  /* FIFO [15:0] or [7:0] */

#define UART_SR_TXFULL		0x00000010	/* TX FIFO full */
#define UART_SR_TXEMPTY		0x00000008	/* TX FIFO empty */

#define UART0_PHYS		0xE0000000
#define UART0_VIRT		0xF0800000
#define UART1_PHYS		0xE0001000
#define UART1_VIRT		0xF0801000

#if IS_ENABLED(CONFIG_DEBUG_ZYNQ_UART1)
# define LL_UART_PADDR		UART1_PHYS
# define LL_UART_VADDR		UART1_VIRT
#else
# define LL_UART_PADDR		UART0_PHYS
# define LL_UART_VADDR		UART0_VIRT
#endif

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =LL_UART_PADDR	@ physical
		ldr	\rv, =LL_UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UART_FIFO_OFFSET]	@ TXDATA
		.endm

		.macro	waituartcts,rd,rx
		.endm

		.macro	waituarttxrdy,rd,rx
1001:		ldr	\rd, [\rx, #UART_SR_OFFSET]
ARM_BE8(	rev	\rd, \rd )
		tst	\rd, #UART_SR_TXEMPTY
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #UART_SR_OFFSET]	@ get status register
ARM_BE8(	rev	\rd, \rd )
		tst	\rd, #UART_SR_TXFULL		@
		bne	1002b			@ wait if FIFO is full
		.endm
