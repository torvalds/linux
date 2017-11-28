/* SPDX-License-Identifier: GPL-2.0 */
#include <mach/hardware.h>
#include <asm/hardware/entry-macro-iomd.S>

	.equ	ioc_base_high, IOC_BASE & 0xff000000
	.equ	ioc_base_low, IOC_BASE & 0x00ff0000

	.macro  get_irqnr_preamble, base, tmp
	mov	\base, #ioc_base_high		@ point at IOC
	.if	ioc_base_low
	orr	\base, \base, #ioc_base_low
	.endif
	.endm
