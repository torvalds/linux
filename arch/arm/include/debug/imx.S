/* arch/arm/mach-imx/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifdef CONFIG_DEBUG_IMX1_UART
#define UART_PADDR	0x00206000
#elif defined (CONFIG_DEBUG_IMX25_UART)
#define UART_PADDR	0x43f90000
#elif defined (CONFIG_DEBUG_IMX21_IMX27_UART)
#define UART_PADDR	0x1000a000
#elif defined (CONFIG_DEBUG_IMX31_IMX35_UART)
#define UART_PADDR	0x43f90000
#elif defined (CONFIG_DEBUG_IMX51_UART)
#define UART_PADDR	0x73fbc000
#elif defined (CONFIG_DEBUG_IMX50_IMX53_UART)
#define UART_PADDR	0x53fbc000
#elif defined (CONFIG_DEBUG_IMX6Q_UART2)
#define UART_PADDR	0x021e8000
#elif defined (CONFIG_DEBUG_IMX6Q_UART4)
#define UART_PADDR	0x021f0000
#endif

/*
 * FIXME: This is a copy of IMX_IO_P2V in hardware.h, and needs to
 * stay sync with that.  It's hard to maintain, and should be fixed
 * globally for multi-platform build to use a fixed virtual address
 * for low-level debug uart port across platforms.
 */
#define IMX_IO_P2V(x)	(						\
			(((x) & 0x80000000) >> 7) |			\
			(0xf4000000 +					\
			(((x) & 0x50000000) >> 6) +			\
			(((x) & 0x0b000000) >> 4) +			\
			(((x) & 0x000fffff))))

#define UART_VADDR	IMX_IO_P2V(UART_PADDR)

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =UART_PADDR	@ physical
		ldr	\rv, =UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x40]	@ TXDATA
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x98]	@ SR2
		tst	\rd, #1 << 3		@ TXDC
		beq	1002b			@ wait until transmit done
		.endm
