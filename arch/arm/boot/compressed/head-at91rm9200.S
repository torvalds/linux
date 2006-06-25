/*
 * linux/arch/arm/boot/compressed/head-at91rm9200.S
 *
 *  Copyright (C) 2003 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach-types.h>

		.section	".start", "ax"

		@ Atmel AT91RM9200-DK : 262
		mov	r3,	#(MACH_TYPE_AT91RM9200DK & 0xff)
		orr	r3, r3, #(MACH_TYPE_AT91RM9200DK & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Cogent CSB337 : 399
		mov	r3,	#(MACH_TYPE_CSB337 & 0xff)
		orr	r3, r3, #(MACH_TYPE_CSB337 & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Cogent CSB637 : 648
		mov	r3,	#(MACH_TYPE_CSB637 & 0xff)
		orr	r3, r3,	#(MACH_TYPE_CSB637 & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Atmel AT91RM9200-EK : 705
		mov	r3,	#(MACH_TYPE_AT91RM9200EK & 0xff)
		orr	r3, r3, #(MACH_TYPE_AT91RM9200EK & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Conitec Carmeva : 769
		mov	r3,	#(MACH_TYPE_CARMEVA & 0xff)
		orr	r3, r3, #(MACH_TYPE_CARMEVA & 0xff00)
		cmp	r7, r3
		beq	99f

		@ KwikByte KB920x : 612
		mov	r3,	#(MACH_TYPE_KB9200 & 0xff)
		orr	r3, r3, #(MACH_TYPE_KB9200 & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Embest ATEB9200 : 923
		mov	r3,	#(MACH_TYPE_ATEB9200 & 0xff)
		orr	r3, r3,	#(MACH_TYPE_ATEB9200 & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Sperry-Sun KAFA : 662
		mov	r3,	#(MACH_TYPE_KAFA & 0xff)
		orr	r3, r3,	#(MACH_TYPE_KAFA & 0xff00)
		cmp	r7, r3
		beq	99f

		@ Unknown board, use the AT91RM9200DK board
		@ mov	r7, #MACH_TYPE_AT91RM9200
		mov	r7,	#(MACH_TYPE_AT91RM9200DK & 0xff)
		orr	r7, r7, #(MACH_TYPE_AT91RM9200DK & 0xff00)

99:
