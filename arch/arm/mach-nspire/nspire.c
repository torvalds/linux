// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */

#include <asm/mach/arch.h>

static const char *const nspire_dt_match[] __initconst = {
	"ti,nspire",
	"ti,nspire-cx",
	"ti,nspire-tp",
	"ti,nspire-clp",
	NULL,
};

DT_MACHINE_START(NSPIRE, "TI-NSPIRE")
	.dt_compat	= nspire_dt_match,
MACHINE_END
