/*
 * Device Tree support for Marvell Berlin SoCs.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * based on GPL'ed 2.6 kernel sources
 *  (c) Marvell International Ltd.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>

static void __init berlin_init_machine(void)
{
	/*
	 * with DT probing for L2CCs, berlin_init_machine can be removed.
	 * Note: 88DE3005 (Armada 1500-mini) uses pl310 l2cc
	 */
	l2x0_of_init(0x70c00000, 0xfeffffff);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const berlin_dt_compat[] = {
	"marvell,berlin",
	NULL,
};

DT_MACHINE_START(BERLIN_DT, "Marvell Berlin")
	.dt_compat	= berlin_dt_compat,
	.init_machine	= berlin_init_machine,
MACHINE_END
