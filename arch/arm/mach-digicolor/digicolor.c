/*
 * Support for Conexant Digicolor SoCs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <asm/mach/arch.h>

static const char *const digicolor_dt_compat[] __initconst = {
	"cnxt,cx92755",
	NULL,
};

DT_MACHINE_START(DIGICOLOR, "Conexant Digicolor (Flattened Device Tree)")
	.dt_compat	= digicolor_dt_compat,
MACHINE_END
