/*
 *  Copyright (C) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 *  Based on linux/arch/arm/lib/floppydma.S
 *      Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text
		.global	mx1_camera_sof_fiq_end
		.global	mx1_camera_sof_fiq_start
mx1_camera_sof_fiq_start:
		@ enable dma
		ldr	r12, [r9]
		orr	r12, r12, #0x00000001
		str	r12, [r9]
		@ unmask DMA interrupt
		ldr	r12, [r8]
		bic	r12, r12, r13
		str	r12, [r8]
		@ disable SOF interrupt
		ldr	r12, [r10]
		bic	r12, r12, #0x00010000
		str	r12, [r10]
		@ clear SOF flag
		mov	r12, #0x00010000
		str	r12, [r11]
		@ return from FIQ
		subs	pc, lr, #4
mx1_camera_sof_fiq_end:
