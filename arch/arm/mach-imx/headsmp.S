/*
 * Copyright 2011 Freescale Semiconductor, Inc.
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
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>

	.section ".text.head", "ax"

#ifdef CONFIG_SMP
ENTRY(v7_secondary_startup)
	bl	v7_invalidate_l1
	b	secondary_startup
ENDPROC(v7_secondary_startup)
#endif

#ifdef CONFIG_PM
/*
 * The following code is located into the .data section.  This is to
 * allow phys_l2x0_saved_regs to be accessed with a relative load
 * as we are running on physical address here.
 */
	.data
	.align

#ifdef CONFIG_CACHE_L2X0
	.macro	pl310_resume
	ldr	r2, phys_l2x0_saved_regs
	ldr	r0, [r2, #L2X0_R_PHY_BASE]	@ get physical base of l2x0
	ldr	r1, [r2, #L2X0_R_AUX_CTRL]	@ get aux_ctrl value
	str	r1, [r0, #L2X0_AUX_CTRL]	@ restore aux_ctrl
	mov	r1, #0x1
	str	r1, [r0, #L2X0_CTRL]		@ re-enable L2
	.endm

	.globl	phys_l2x0_saved_regs
phys_l2x0_saved_regs:
        .long   0
#else
	.macro	pl310_resume
	.endm
#endif

ENTRY(v7_cpu_resume)
	bl	v7_invalidate_l1
	pl310_resume
	b	cpu_resume
ENDPROC(v7_cpu_resume)
#endif
