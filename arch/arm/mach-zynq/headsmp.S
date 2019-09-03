/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 * Copyright (c) 2012-2013 Xilinx
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

	.arm

ENTRY(zynq_secondary_trampoline)
ARM_BE8(setend	be)				@ ensure we are in BE8 mode
	ldr	r0, zynq_secondary_trampoline_jump
ARM_BE8(rev	r0, r0)
	bx	r0
.globl zynq_secondary_trampoline_jump
zynq_secondary_trampoline_jump:
	/* Space for jumping address */
	.word	0	/* cpu 1 */
.globl zynq_secondary_trampoline_end
zynq_secondary_trampoline_end:
ENDPROC(zynq_secondary_trampoline)
