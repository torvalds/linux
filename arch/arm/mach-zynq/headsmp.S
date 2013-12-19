/*
 * Copyright (c) 2013 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 * Copyright (c) 2012-2013 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>

ENTRY(zynq_secondary_trampoline)
	ldr	r0, [pc]
	bx	r0
.globl zynq_secondary_trampoline_jump
zynq_secondary_trampoline_jump:
	/* Space for jumping address */
	.word	/* cpu 1 */
.globl zynq_secondary_trampoline_end
zynq_secondary_trampoline_end:

ENDPROC(zynq_secondary_trampoline)
