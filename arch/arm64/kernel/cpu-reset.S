/*
 * CPU reset routines
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Huawei Futurewei Technologies.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

.text
.pushsection    .idmap.text, "awx"

/*
 * __cpu_soft_restart(el2_switch, entry, arg0, arg1, arg2) - Helper for
 * cpu_soft_restart.
 *
 * @el2_switch: Flag to indicate a switch to EL2 is needed.
 * @entry: Location to jump to for soft reset.
 * arg0: First argument passed to @entry. (relocation list)
 * arg1: Second argument passed to @entry.(physical kernel entry)
 * arg2: Third argument passed to @entry. (physical dtb address)
 *
 * Put the CPU into the same state as it would be if it had been reset, and
 * branch to what would be the reset vector. It must be executed with the
 * flat identity mapping.
 */
ENTRY(__cpu_soft_restart)
	/* Clear sctlr_el1 flags. */
	mrs	x12, sctlr_el1
	ldr	x13, =SCTLR_ELx_FLAGS
	bic	x12, x12, x13
	pre_disable_mmu_workaround
	msr	sctlr_el1, x12
	isb

	cbz	x0, 1f				// el2_switch?
	mov	x0, #HVC_SOFT_RESTART
	hvc	#0				// no return

1:	mov	x18, x1				// entry
	mov	x0, x2				// arg0
	mov	x1, x3				// arg1
	mov	x2, x4				// arg2
	br	x18
ENDPROC(__cpu_soft_restart)

.popsection
