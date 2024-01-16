/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Exynos low-level resume code
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>
#include "smc.h"

#define CPU_MASK	0xff0ffff0
#define CPU_CORTEX_A9	0x410fc090

	.text
	.align

	/*
	 * sleep magic, to allow the bootloader to check for an valid
	 * image to resume to. Must be the first word before the
	 * exynos_cpu_resume entry.
	 */

	.word	0x2bedf00d

	/*
	 * exynos_cpu_resume
	 *
	 * resume code entry for bootloader to call
	 */

ENTRY(exynos_cpu_resume)
#ifdef CONFIG_CACHE_L2X0
	mrc	p15, 0, r0, c0, c0, 0
	ldr	r1, =CPU_MASK
	and	r0, r0, r1
	ldr	r1, =CPU_CORTEX_A9
	cmp	r0, r1
	bleq	l2c310_early_resume
#endif
	b	cpu_resume
ENDPROC(exynos_cpu_resume)

	.align
	.arch armv7-a
	.arch_extension sec
ENTRY(exynos_cpu_resume_ns)
	mrc	p15, 0, r0, c0, c0, 0
	ldr	r1, =CPU_MASK
	and	r0, r0, r1
	ldr	r1, =CPU_CORTEX_A9
	cmp	r0, r1
	bne	skip_cp15

	adr	r0, _cp15_save_power
	ldr	r1, [r0]
	ldr	r1, [r0, r1]
	adr	r0, _cp15_save_diag
	ldr	r2, [r0]
	ldr	r2, [r0, r2]
	mov	r0, #SMC_CMD_C15RESUME
	dsb
	smc	#0
#ifdef CONFIG_CACHE_L2X0
	adr	r0, 1f
	ldr	r2, [r0]
	add	r0, r2, r0

	/* Check that the address has been initialised. */
	ldr	r1, [r0, #L2X0_R_PHY_BASE]
	teq	r1, #0
	beq	skip_l2x0

	/* Check if controller has been enabled. */
	ldr	r2, [r1, #L2X0_CTRL]
	tst	r2, #0x1
	bne	skip_l2x0

	ldr	r1, [r0, #L2X0_R_TAG_LATENCY]
	ldr	r2, [r0, #L2X0_R_DATA_LATENCY]
	ldr	r3, [r0, #L2X0_R_PREFETCH_CTRL]
	mov	r0, #SMC_CMD_L2X0SETUP1
	smc	#0

	/* Reload saved regs pointer because smc corrupts registers. */
	adr	r0, 1f
	ldr	r2, [r0]
	add	r0, r2, r0

	ldr	r1, [r0, #L2X0_R_PWR_CTRL]
	ldr	r2, [r0, #L2X0_R_AUX_CTRL]
	mov	r0, #SMC_CMD_L2X0SETUP2
	smc	#0

	mov	r0, #SMC_CMD_L2X0INVALL
	smc	#0

	mov	r1, #1
	mov	r0, #SMC_CMD_L2X0CTRL
	smc	#0
skip_l2x0:
#endif /* CONFIG_CACHE_L2X0 */
skip_cp15:
	b	cpu_resume
ENDPROC(exynos_cpu_resume_ns)

	.align
_cp15_save_power:
	.long	cp15_save_power - .
_cp15_save_diag:
	.long	cp15_save_diag - .
#ifdef CONFIG_CACHE_L2X0
1:	.long	l2x0_saved_regs - .
#endif /* CONFIG_CACHE_L2X0 */

	.data
	.align	2
	.globl cp15_save_diag
cp15_save_diag:
	.long	0	@ cp15 diagnostic
	.globl cp15_save_power
cp15_save_power:
	.long	0	@ cp15 power control
