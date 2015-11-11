/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/mach/arch.h>

static const char * const uniphier_dt_compat[] __initconst = {
	"socionext,ph1-sld3",
	"socionext,ph1-ld4",
	"socionext,ph1-pro4",
	"socionext,ph1-sld8",
	"socionext,ph1-pro5",
	"socionext,proxstream2",
	"socionext,ph1-ld6b",
	NULL,
};

DT_MACHINE_START(UNIPHIER, "Socionext UniPhier")
	.dt_compat	= uniphier_dt_compat,
MACHINE_END
