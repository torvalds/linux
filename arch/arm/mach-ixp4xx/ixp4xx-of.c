// SPDX-License-Identifier: GPL-2.0
/*
 * IXP4xx Device Tree boot support
 */
#include <asm/mach/arch.h>

/*
 * We handle 4 different SoC families. These compatible strings are enough
 * to provide the core so that different boards can add their more detailed
 * specifics.
 */
static const char *ixp4xx_of_board_compat[] = {
	"intel,ixp42x",
	"intel,ixp43x",
	"intel,ixp45x",
	"intel,ixp46x",
	NULL,
};

DT_MACHINE_START(IXP4XX_DT, "IXP4xx (Device Tree)")
	.dt_compat	= ixp4xx_of_board_compat,
MACHINE_END
