// SPDX-License-Identifier: GPL-2.0-only
/*
 *	linux/arch/arm/mach-nspire/nspire.c
 *
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include "mmio.h"

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
