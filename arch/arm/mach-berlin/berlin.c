// SPDX-License-Identifier: GPL-2.0
/*
 * Device Tree support for Marvell Berlin SoCs.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * based on GPL'ed 2.6 kernel sources
 *  (c) Marvell International Ltd.
 */

#include <asm/mach/arch.h>

static const char * const berlin_dt_compat[] = {
	"marvell,berlin",
	NULL,
};

DT_MACHINE_START(BERLIN_DT, "Marvell Berlin")
	.dt_compat	= berlin_dt_compat,
	/*
	 * with DT probing for L2CCs, berlin_init_machine can be removed.
	 * Note: 88DE3005 (Armada 1500-mini) uses pl310 l2cc
	 */
	.l2c_aux_val	= 0x30c00000,
	.l2c_aux_mask	= 0xfeffffff,
MACHINE_END
