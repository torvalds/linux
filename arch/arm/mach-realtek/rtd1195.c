// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek RTD1195
 *
 * Copyright (c) 2017-2019 Andreas Färber
 */

#include <linux/memblock.h>
#include <asm/mach/arch.h>

static void __init rtd1195_memblock_remove(phys_addr_t base, phys_addr_t size)
{
	int ret;

	ret = memblock_remove(base, size);
	if (ret)
		pr_err("Failed to remove memblock %pa (%d)\n", &base, ret);
}

static void __init rtd1195_reserve(void)
{
	/* Exclude boot ROM from RAM */
	rtd1195_memblock_remove(0x00000000, 0x0000a800);

	/* Exclude peripheral register spaces from RAM */
	rtd1195_memblock_remove(0x18000000, 0x00070000);
	rtd1195_memblock_remove(0x18100000, 0x01000000);
}

static const char *const rtd1195_dt_compat[] __initconst = {
	"realtek,rtd1195",
	NULL
};

DT_MACHINE_START(rtd1195, "Realtek RTD1195")
	.dt_compat = rtd1195_dt_compat,
	.reserve = rtd1195_reserve,
	.l2c_aux_val = 0x0,
	.l2c_aux_mask = ~0x0,
MACHINE_END
