/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CPU reset routines
 *
 * Copyright (C) 2001 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Huawei Futurewei Technologies.
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

.text
.pushsection    .idmap.text, "awx"

/*
 * cpu_soft_restart(el2_switch, entry, arg0, arg1, arg2)
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
SYM_TYPED_FUNC_START(cpu_soft_restart)
	mov_q	x12, INIT_SCTLR_EL1_MMU_OFF
	pre_disable_mmu_workaround
	/*
	 * either disable EL1&0 translation regime or disable EL2&0 translation
	 * regime if HCR_EL2.E2H == 1
	 */
	msr	sctlr_el1, x12
	isb

	cbz	x0, 1f				// el2_switch?
	mov	x0, #HVC_SOFT_RESTART
	hvc	#0				// no return

1:	mov	x8, x1				// entry
	mov	x0, x2				// arg0
	mov	x1, x3				// arg1
	mov	x2, x4				// arg2
	br	x8
SYM_FUNC_END(cpu_soft_restart)

.popsection
