/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/serial_reg.h>

/* Physical register offset and virtual register offset */
#define REG_PHYS_BASE		0xf0000000
#define REG_VIRT_BASE		0xfc000000
#define REG_PHYS_ADDR(x)	((x) + REG_PHYS_BASE)

/* Product id can be read from here */
#define SUN_TOP_CTRL_BASE	REG_PHYS_ADDR(0x404000)

#define UARTA_3390		REG_PHYS_ADDR(0x40a900)
#define UARTA_7250		REG_PHYS_ADDR(0x40b400)
#define UARTA_7260		REG_PHYS_ADDR(0x40c000)
#define UARTA_7268		UARTA_7260
#define UARTA_7271		UARTA_7268
#define UARTA_7364		REG_PHYS_ADDR(0x40b000)
#define UARTA_7366		UARTA_7364
#define UARTA_74371		REG_PHYS_ADDR(0x406b00)
#define UARTA_7439		REG_PHYS_ADDR(0x40a900)
#define UARTA_7445		REG_PHYS_ADDR(0x40ab00)

#define UART_SHIFT		2

#define checkuart(rp, rv, family_id, family) \
		/* Load family id */ \
		ldr	rp, =family_id ; \
		/* Compare SUN_TOP_CTRL value against it */ \
		cmp	rp, rv ; \
		/* Passed test, load address */ \
		ldreq	rp, =UARTA_##family ; \
		/* Jump to save UART address */ \
		beq	91f

		.macro  addruart, rp, rv, tmp
		adr	\rp, 99f		@ actual addr of 99f
		ldr	\rv, [\rp]		@ linked addr is stored there
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ linked brcmstb_uart_config
		sub	\tmp, \rp, \rv		@ actual brcmstb_uart_config
		ldr	\rp, [\tmp]		@ Load brcmstb_uart_config
		cmp	\rp, #1			@ needs initialization?
		bne	100f			@ no; go load the addresses
		mov	\rv, #0			@ yes; record init is done
		str	\rv, [\tmp]

		/* Check SUN_TOP_CTRL base */
		ldr	\rp, =SUN_TOP_CTRL_BASE	@ load SUN_TOP_CTRL PA
		ldr	\rv, [\rp, #0]		@ get register contents
		and	\rv, \rv, #0xffffff00	@ strip revision bits [7:0]

		/* Chip specific detection starts here */
20:		checkuart(\rp, \rv, 0x33900000, 3390)
21:		checkuart(\rp, \rv, 0x72500000, 7250)
22:		checkuart(\rp, \rv, 0x72600000, 7260)
23:		checkuart(\rp, \rv, 0x72680000, 7268)
24:		checkuart(\rp, \rv, 0x72710000, 7271)
25:		checkuart(\rp, \rv, 0x73640000, 7364)
26:		checkuart(\rp, \rv, 0x73660000, 7366)
27:		checkuart(\rp, \rv, 0x07437100, 74371)
28:		checkuart(\rp, \rv, 0x74390000, 7439)
29:		checkuart(\rp, \rv, 0x74450000, 7445)

		/* No valid UART found */
90:		mov	\rp, #0
		/* fall through */

		/* Record whichever UART we chose */
91:		str	\rp, [\tmp, #4]		@ Store in brcmstb_uart_phys
		cmp	\rp, #0			@ Valid UART address?
		bne	92f			@ Yes, go process it
		str	\rp, [\tmp, #8]		@ Store 0 in brcmstb_uart_virt
		b	100f			@ Done
92:		and     \rv, \rp, #0xffffff	@ offset within 16MB section
		add	\rv, \rv, #REG_VIRT_BASE
		str	\rv, [\tmp, #8]		@ Store in brcmstb_uart_virt
		b	100f

		.align
99:		.word	.
		.word	brcmstb_uart_config
		.ltorg

		/* Load previously selected UART address */
100:		ldr	\rp, [\tmp, #4]		@ Load brcmstb_uart_phys
		ldr	\rv, [\tmp, #8]		@ Load brcmstb_uart_virt
		.endm

		.macro	store, rd, rx:vararg
		str	\rd, \rx
		.endm

		.macro	load, rd, rx:vararg
		ldr	\rd, \rx
		.endm

		.macro	senduart,rd,rx
		store	\rd, [\rx, #UART_TX << UART_SHIFT]
		.endm

		.macro	busyuart,rd,rx
1002:		load	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1002b
		.endm

		.macro	waituart,rd,rx
		.endm

/*
 * Storage for the state maintained by the macros above.
 *
 * In the kernel proper, this data is located in arch/arm/mach-bcm/brcmstb.c.
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
brcmstb_uart_config:
	/* Debug UART initialization required */
	.word 1
	/* Debug UART physical address */
	.word 0
	/* Debug UART virtual address */
	.word 0
#endif
