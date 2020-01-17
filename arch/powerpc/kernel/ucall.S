/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generic code to perform an ultravisor call.
 *
 * Copyright 2019, IBM Corporation.
 *
 */
#include <asm/ppc_asm.h>
#include <asm/export.h>

_GLOBAL(ucall_yesrets)
EXPORT_SYMBOL_GPL(ucall_yesrets)
	sc	2	/* Invoke the ultravisor */
	blr		/* Return r3 = status */
