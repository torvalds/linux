/*
 * Copyright (C) 2014 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * This is the entry point through which CPUs exiting cpuidle deep
 * idle state are going.
 */
ENTRY(armada_370_xp_cpu_resume)
ARM_BE8(setend	be )			@ go BE8 if entered LE
	bl	ll_add_cpu_to_smp_group
	bl	ll_enable_coherency
	b	cpu_resume
ENDPROC(armada_370_xp_cpu_resume)

.global mvebu_boot_wa_start
.global mvebu_boot_wa_end

/* The following code will be executed from SRAM */
ENTRY(mvebu_boot_wa_start)
mvebu_boot_wa_start:
ARM_BE8(setend	be)
	adr	r0, 1f
	ldr	r0, [r0]		@ load the address of the
					@ resume register
	ldr	r0, [r0]		@ load the value in the
					@ resume register
ARM_BE8(rev	r0, r0)			@ the value is stored LE
	mov	pc, r0			@ jump to this value
/*
 * the last word of this piece of code will be filled by the physical
 * address of the boot address register just after being copied in SRAM
 */
1:
	.long   .
mvebu_boot_wa_end:
ENDPROC(mvebu_boot_wa_end)
