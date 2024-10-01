/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generic code to perform an ultravisor call.
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#include <linux/export.h>
#include <asm/ppc_asm.h>

_GLOBAL(ucall_norets)
EXPORT_SYMBOL_GPL(ucall_norets)
	sc	2	/* Invoke the ultravisor */
	blr		/* Return r3 = status */
