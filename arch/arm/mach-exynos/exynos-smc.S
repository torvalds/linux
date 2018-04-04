/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Samsung Electronics.
 *
 * Copied from omap-smc.S Copyright (C) 2010 Texas Instruments, Inc.
 */

#include <linux/linkage.h>

/*
 * Function signature: void exynos_smc(u32 cmd, u32 arg1, u32 arg2, u32 arg3)
 */

ENTRY(exynos_smc)
	stmfd	sp!, {r4-r11, lr}
	dsb
	smc	#0
	ldmfd	sp!, {r4-r11, pc}
ENDPROC(exynos_smc)
