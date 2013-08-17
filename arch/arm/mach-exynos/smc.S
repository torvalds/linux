/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <mach/smc.h>

ENTRY(exynos_smc)
	stmfd	sp!, {lr}
#if defined(CONFIG_ARM_ERRATA_766421)
	dmb
#endif
	smc	0
#if defined(CONFIG_ARM_ERRATA_766421)
	dmb
#endif
	ldmfd	sp!, {pc}
ENDPROC(exynos_smc)

ENTRY(exynos_smc_readsfr)
	stmfd	sp!, {r2-r3, lr}
	stmfd	sp!, {r1}

	lsr	r1, r0, #2
	orr	r1, r1, #SMC_REG_CLASS_SFR_R
	mov	r0, #SMC_CMD_REG
#if defined(CONFIG_ARM_ERRATA_766421)
	dmb
#endif
	smc	0
#if defined(CONFIG_ARM_ERRATA_766421)
	dmb
#endif

	mov	r3, #SMC_CMD_REG
	cmp	r0, r3
	moveq	r0, r1
	ldmfd	sp!, {r3}
	cmp	r0, #0
	streq	r2, [r3]
	ldmfd	sp!, {r2-r3, pc}
ENDPROC(exynos_smc_readsfr)
