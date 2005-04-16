/*
 * linux/arch/arm/boot/compressed/head.S
 *
 * Copyright (C) 1999, 2000, 2001 Nexus Electronics Ltd
 */

#include <linux/config.h>

		/* There are three different ways the kernel can be
		   booted on a 7500 system: from Angel (loaded in RAM), from
		   16-bit ROM or from 32-bit Flash.  Luckily, a single kernel
		   image does for them all. */
		/* This branch is taken if the CPU memory width matches the
		   actual device in use.  The default at power on is 16 bits
		   so we must be prepared for a mismatch. */
		.section ".start", "ax"
2:
		b	1f
		.word	0xffff
		.word	0xb632		@ mov r11, #0x03200000
		.word	0xe3a0
		.word	0x0000		@ mov r0, #0
		.word	0xe3a0
		.word	0x0080		@ strb r0, [r11, #0x80]
		.word	0xe5cb
		.word	0xf000		@ mov pc, #0
		.word	0xe3a0
1:
		adr	r1, 2b
		teq	r1, #0
		bne	.Langel
		/* This is a direct-from-ROM boot.  Copy the kernel into 
		   RAM and run it there. */
		mov	r0, #0x30
		mcr	p15, 0, r0, c1, c0, 0
		mov	r0, #0x13
		msr	cpsr_cxsf, r0
		mov	r12, #0x03000000	@ point to LEDs
		orr	r12, r12, #0x00020000
		orr	r12, r12, #0xba00
		mov	r0, #0x5500
		str	r0, [r12]
		mov	r0, #0x10000000
		orr	r0, r0, #0x8000
		mov	r4, r0
		ldr	r2, =_end
2:
		ldr	r3, [r1], #4
		str	r3, [r0], #4
		teq	r0, r2
		bne	2b
		mov	r0, #0xff00
		str	r0, [r12]
1:	
		mov	r12, #0x03000000	@ point to LEDs
		orr	r12, r12, #0x00020000
		orr	r12, r12, #0xba00
		mov	r0, #0xfe00
		str	r0, [r12]

		adr	lr, 1f
		mov	r0, #0
		mov	r1, #14		/* MACH_TYPE_CLPS7500 */
		mov	pc, lr
.Langel:
#ifdef CONFIG_ANGELBOOT
		/* Call Angel to switch into SVC mode. */
		mov	r0, #0x17
		swi	0x123456
#endif
		/* Ensure all interrupts are off and MMU disabled */
		mrs	r0, cpsr
		orr	r0, r0, #0xc0
		msr	cpsr_cxsf, r0

		adr	lr, 1b
		orr	lr, lr, #0x10000000
		mov	r0, #0x30		@ MMU off
		mcr	p15, 0, r0, c1, c0, 0
		mov	r0, r0
	 	mov	pc, lr

		.ltorg

1:
/* And the rest */
#include "head.S"
