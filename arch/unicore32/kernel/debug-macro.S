/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/kernel/debug-macro.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * Debugging macro include header
 */
#include <generated/asm-offsets.h>
#include <mach/hardware.h>

		.macro	put_word_ocd, rd, rx=r16
1001:		movc		\rx, p1.c0, #0
		cand.a	\rx, #2
		bne	1001b
		movc		p1.c1, \rd, #1
		.endm

#ifdef CONFIG_DEBUG_OCD
		/* debug using UniCore On-Chip-Debugger */
		.macro	addruart, rx
		.endm

		.macro	senduart, rd, rx
		put_word_ocd	\rd, \rx
		.endm

		.macro	busyuart, rd, rx
		.endm

		.macro	waituart, rd, rx
		.endm
#else
#define UART_CLK_DEFAULT        3686400 * 20
	/* Uartclk = MCLK/ 2, The MCLK on my board is 3686400 * 40  */
#define BAUD_RATE_DEFAULT	115200
	/* The baud rate of the serial port */

#define UART_DIVISOR_DEFAULT	(UART_CLK_DEFAULT \
				/ (16 * BAUD_RATE_DEFAULT) - 1)

		.macro	addruart,rx
		mrc	p0, #0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0xee000000	@ physical base address
		movne	\rx, #0x6e000000	@ virtual address

		@ We probe for the active serial port here
		@ However, now we assume UART0 is active:	epip4d
		@ We assume r1 and r2 can be clobbered.

		movl 	r2, #UART_DIVISOR_DEFAULT
		mov 	r1, #0x80
		str	r1, [\rx, #UART_LCR_OFFSET]
		and	r1, r2, #0xff00
		mov	r1, r1, lsr #8
		str	r1, [\rx, #UART_DLH_OFFSET]
		and	r1, r2, #0xff
		str	r1, [\rx, #UART_DLL_OFFSET]
		mov 	r1, #0x7
		str	r1, [\rx, #UART_FCR_OFFSET]
		mov 	r1, #0x3
		str	r1, [\rx, #UART_LCR_OFFSET]
		mov 	r1, #0x0
		str	r1, [\rx, #UART_IER_OFFSET]
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_THR_OFFSET]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #UART_LSR_OFFSET]
		tst	\rd, #UART_LSR_THRE
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UART_LSR_OFFSET]
		tst	\rd, #UART_LSR_TEMT
		bne	1001b
		.endm
#endif

