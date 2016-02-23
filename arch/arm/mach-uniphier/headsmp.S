/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/cp15.h>

ENTRY(uniphier_smp_trampoline)
ARM_BE8(setend	be)			@ ensure we are in BE8 mode
	mrc	p15, 0, r0, c0, c0, 5	@ MPIDR (Multiprocessor Affinity Reg)
	and  	r2, r0, #0x3		@ CPU ID
	ldr	r1, uniphier_smp_trampoline_jump
	ldr	r3, uniphier_smp_trampoline_poll_addr
	mrc	p15, 0, r0, c1, c0, 0	@ SCTLR (System Control Register)
	orr	r0, r0, #CR_I		@ Enable ICache
	bic	r0, r0, #(CR_C | CR_M)	@ Disable MMU and Dcache
	mcr	p15, 0, r0, c1, c0, 0
	b	1f			@ cache the following 5 instructions
0:	wfe
1:	ldr	r0, [r3]
	cmp	r0, r2
	bxeq	r1			@ branch to secondary_startup
	b	0b
	.globl	uniphier_smp_trampoline_jump
uniphier_smp_trampoline_jump:
	.word	0			@ set virt_to_phys(secondary_startup)
	.globl	uniphier_smp_trampoline_poll_addr
uniphier_smp_trampoline_poll_addr:
	.word	0			@ set CPU ID to be kicked to this reg
	.globl	uniphier_smp_trampoline_end
uniphier_smp_trampoline_end:
ENDPROC(uniphier_smp_trampoline)
