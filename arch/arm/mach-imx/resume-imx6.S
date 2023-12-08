/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2014 Freescale Semiconductor, Inc.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>
#include "hardware.h"

/*
 * The following code must assume it is running from physical address
 * where absolute virtual addresses to the data section have to be
 * turned into relative ones.
 */

ENTRY(v7_cpu_resume)
	bl	v7_invalidate_l1
#ifdef CONFIG_CACHE_L2X0
	bl	l2c310_early_resume
#endif
	b	cpu_resume
ENDPROC(v7_cpu_resume)
