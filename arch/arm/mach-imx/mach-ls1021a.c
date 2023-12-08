// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013-2014 Freescale Semiconductor, Inc.
 */

#include <asm/mach/arch.h>

#include "common.h"

static const char * const ls1021a_dt_compat[] __initconst = {
	"fsl,ls1021a",
	NULL,
};

DT_MACHINE_START(LS1021A, "Freescale LS1021A")
	.smp		= smp_ops(ls1021a_smp_ops),
	.dt_compat	= ls1021a_dt_compat,
MACHINE_END
