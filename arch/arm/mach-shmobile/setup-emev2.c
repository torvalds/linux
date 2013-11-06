/*
 * Emma Mobile EV2 processor support
 *
 * Copyright (C) 2012  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

static struct map_desc emev2_io_desc[] __initdata = {
#ifdef CONFIG_SMP
	/* 2M mapping for SCU + L2 controller */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0x1e000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE
	},
#endif
};

void __init emev2_map_io(void)
{
	iotable_init(emev2_io_desc, ARRAY_SIZE(emev2_io_desc));
}

void __init emev2_init_delay(void)
{
	shmobile_setup_delay(533, 1, 3); /* Cortex-A9 @ 533MHz */
}

#ifdef CONFIG_USE_OF

static void __init emev2_add_standard_devices_dt(void)
{
#ifdef CONFIG_COMMON_CLK
	of_clk_init(NULL);
#else
	emev2_clock_init();
#endif
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *emev2_boards_compat_dt[] __initdata = {
	"renesas,emev2",
	NULL,
};

DT_MACHINE_START(EMEV2_DT, "Generic Emma Mobile EV2 (Flattened Device Tree)")
	.smp		= smp_ops(emev2_smp_ops),
	.map_io		= emev2_map_io,
	.init_early	= emev2_init_delay,
	.init_machine	= emev2_add_standard_devices_dt,
	.init_late	= shmobile_init_late,
	.dt_compat	= emev2_boards_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
