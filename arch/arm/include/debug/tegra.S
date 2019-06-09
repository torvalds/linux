/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010,2011 Google, Inc.
 * Copyright (C) 2011-2012 NVIDIA CORPORATION. All Rights Reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *	Doug Anderson <dianders@chromium.org>
 *	Stephen Warren <swarren@nvidia.com>
 *
 * Portions based on mach-omap2's debug-macro.S
 * Copyright (C) 1994-1999 Russell King
 */

#include <linux/serial_reg.h>

#define UART_SHIFT 2

/* Physical addresses */
#define TEGRA_CLK_RESET_BASE		0x60006000
#define TEGRA_APB_MISC_BASE		0x70000000
#define TEGRA_UARTA_BASE		0x70006000
#define TEGRA_UARTB_BASE		0x70006040
#define TEGRA_UARTC_BASE		0x70006200
#define TEGRA_UARTD_BASE		0x70006300
#define TEGRA_UARTE_BASE		0x70006400
#define TEGRA_PMC_BASE			0x7000e400

#define TEGRA_CLK_RST_DEVICES_L		(TEGRA_CLK_RESET_BASE + 0x04)
#define TEGRA_CLK_RST_DEVICES_H		(TEGRA_CLK_RESET_BASE + 0x08)
#define TEGRA_CLK_RST_DEVICES_U		(TEGRA_CLK_RESET_BASE + 0x0c)
#define TEGRA_CLK_OUT_ENB_L		(TEGRA_CLK_RESET_BASE + 0x10)
#define TEGRA_CLK_OUT_ENB_H		(TEGRA_CLK_RESET_BASE + 0x14)
#define TEGRA_CLK_OUT_ENB_U		(TEGRA_CLK_RESET_BASE + 0x18)
#define TEGRA_PMC_SCRATCH20		(TEGRA_PMC_BASE + 0xa0)
#define TEGRA_APB_MISC_GP_HIDREV	(TEGRA_APB_MISC_BASE + 0x804)

/*
 * Must be section-aligned since a section mapping is used early on.
 * Must not overlap with regions in mach-tegra/io.c:tegra_io_desc[].
 */
#define UART_VIRTUAL_BASE		0xfe800000

#define checkuart(rp, rv, lhu, bit, uart) \
		/* Load address of CLK_RST register */ \
		ldr	rp, =TEGRA_CLK_RST_DEVICES_##lhu ; \
		/* Load value from CLK_RST register */ \
		ldr	rp, [rp, #0] ; \
		/* Test UART's reset bit */ \
		tst	rp, #(1 << bit) ; \
		/* If set, can't use UART; jump to save no UART */ \
		bne	90f ; \
		/* Load address of CLK_OUT_ENB register */ \
		ldr	rp, =TEGRA_CLK_OUT_ENB_##lhu ; \
		/* Load value from CLK_OUT_ENB register */ \
		ldr	rp, [rp, #0] ; \
		/* Test UART's clock enable bit */ \
		tst	rp, #(1 << bit) ; \
		/* If clear, can't use UART; jump to save no UART */ \
		beq	90f ; \
		/* Passed all tests, load address of UART registers */ \
		ldr	rp, =TEGRA_UART##uart##_BASE ; \
		/* Jump to save UART address */ \
		b 91f

		.macro  addruart, rp, rv, tmp
		adr	\rp, 99f		@ actual addr of 99f
		ldr	\rv, [\rp]		@ linked addr is stored there
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ linked tegra_uart_config
		sub	\tmp, \rp, \rv		@ actual tegra_uart_config
		ldr	\rp, [\tmp]		@ Load tegra_uart_config
		cmp	\rp, #1			@ needs initialization?
		bne	100f			@ no; go load the addresses
		mov	\rv, #0			@ yes; record init is done
		str	\rv, [\tmp]

#ifdef CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA
		/* Check ODMDATA */
10:		ldr	\rp, =TEGRA_PMC_SCRATCH20
		ldr	\rp, [\rp, #0]		@ Load PMC_SCRATCH20
		lsr	\rv, \rp, #18		@ 19:18 are console type
		and	\rv, \rv, #3
		cmp	\rv, #2			@ 2 and 3 mean DCC, UART
		beq	11f			@ some boards swap the meaning
		cmp	\rv, #3			@ so accept either
		bne	90f
11:		lsr	\rv, \rp, #15		@ 17:15 are UART ID
		and	\rv, #7	
		cmp	\rv, #0			@ UART 0?
		beq	20f
		cmp	\rv, #1			@ UART 1?
		beq	21f
		cmp	\rv, #2			@ UART 2?
		beq	22f
		cmp	\rv, #3			@ UART 3?
		beq	23f
		cmp	\rv, #4			@ UART 4?
		beq	24f
		b	90f			@ invalid
#endif

#if defined(CONFIG_TEGRA_DEBUG_UARTA) || \
    defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
		/* Check UART A validity */
20:		checkuart(\rp, \rv, L, 6, A)
#endif

#if defined(CONFIG_TEGRA_DEBUG_UARTB) || \
    defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
		/* Check UART B validity */
21:		checkuart(\rp, \rv, L, 7, B)
#endif

#if defined(CONFIG_TEGRA_DEBUG_UARTC) || \
    defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
		/* Check UART C validity */
22:		checkuart(\rp, \rv, H, 23, C)
#endif

#if defined(CONFIG_TEGRA_DEBUG_UARTD) || \
    defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
		/* Check UART D validity */
23:		checkuart(\rp, \rv, U, 1, D)
#endif

#if defined(CONFIG_TEGRA_DEBUG_UARTE) || \
    defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
		/* Check UART E validity */
24:
		checkuart(\rp, \rv, U, 2, E)
#endif

		/* No valid UART found */
90:		mov	\rp, #0
		/* fall through */

		/* Record whichever UART we chose */
91:		str	\rp, [\tmp, #4]		@ Store in tegra_uart_phys
		cmp	\rp, #0			@ Valid UART address?
		bne	92f			@ Yes, go process it
		str	\rp, [\tmp, #8]		@ Store 0 in tegra_uart_virt
		b	100f			@ Done
92:		and	\rv, \rp, #0xffffff	@ offset within 1MB section
		add	\rv, \rv, #UART_VIRTUAL_BASE
		str	\rv, [\tmp, #8]		@ Store in tegra_uart_virt
		b	100f

		.align
99:		.word	.
		.word	tegra_uart_config
		.ltorg

		/* Load previously selected UART address */
100:		ldr	\rp, [\tmp, #4]		@ Load tegra_uart_phys
		ldr	\rv, [\tmp, #8]		@ Load tegra_uart_virt
		.endm

/*
 * Code below is swiped from <asm/hardware/debug-8250.S>, but add an extra
 * check to make sure that the UART address is actually valid.
 */

		.macro	senduart, rd, rx
		cmp	\rx, #0
		strbne	\rd, [\rx, #UART_TX << UART_SHIFT]
1001:
		.endm

		.macro	busyuart, rd, rx
		cmp	\rx, #0
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_THRE
		teq	\rd, #UART_LSR_THRE
		bne	1001b
1002:
		.endm

		.macro	waituart, rd, rx
#ifdef FLOW_CONTROL
		cmp	\rx, #0
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
1002:
#endif
		.endm

/*
 * Storage for the state maintained by the macros above.
 *
 * In the kernel proper, this data is located in arch/arm/mach-tegra/tegra.c.
 * That's because this header is included from multiple files, and we only
 * want a single copy of the data. In particular, the UART probing code above
 * assumes it's running using physical addresses. This is true when this file
 * is included from head.o, but not when included from debug.o. So we need
 * to share the probe results between the two copies, rather than having
 * to re-run the probing again later.
 *
 * In the decompressor, we put the symbol/storage right here, since common.c
 * isn't included in the decompressor build. This symbol gets put in .text
 * even though it's really data, since .data is discarded from the
 * decompressor. Luckily, .text is writeable in the decompressor, unless
 * CONFIG_ZBOOT_ROM. That dependency is handled in arch/arm/Kconfig.debug.
 */
#if defined(ZIMAGE)
tegra_uart_config:
	/* Debug UART initialization required */
	.word 1
	/* Debug UART physical address */
	.word 0
	/* Debug UART virtual address */
	.word 0
#endif
