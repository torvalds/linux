/*
 * Copyright 2011-2015 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/linkage.h>
#include <linux/init.h>

diag_reg_offset:
	.word	g_diag_reg - .

	.macro	set_diag_reg
	adr	r0, diag_reg_offset
	ldr	r1, [r0]
	add	r1, r1, r0		@ r1 = physical &g_diag_reg
	ldr	r0, [r1]
	mcr	p15, 0, r0, c15, c0, 1	@ write diagnostic register
	.endm

ENTRY(v7_secondary_startup)
	mrc	p15, 0, r0, c0, c0, 0
	ldr	r1, =0xf00
	orr	r1, r1, #0xff
	mov	r0, r0, lsr #4
	and	r0, r0, r1
	/* 0xc07 is cortex A7's ID */
	ldr	r1, =0xc00
	orr	r1, r1, #0x7
	cmp	r0, r1
	beq	secondary_startup

	set_diag_reg
	b	secondary_startup
ENDPROC(v7_secondary_startup)
