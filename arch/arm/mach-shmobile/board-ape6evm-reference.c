/*
 * APE6EVM board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
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

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/sh_clk.h>
#include <mach/common.h>
#include <mach/r8a73a4.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static void __init ape6evm_add_standard_devices(void)
{

	struct clk *parent;
	struct clk *mp;

	r8a73a4_clock_init();

	/* MP clock parent = extal2 */
	parent      = clk_get(NULL, "extal2");
	mp          = clk_get(NULL, "mp");
	BUG_ON(IS_ERR(parent) || IS_ERR(mp));

	clk_set_parent(mp, parent);
	clk_put(parent);
	clk_put(mp);

	r8a73a4_add_dt_devices();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	platform_device_register_simple("cpufreq-cpu0", -1, NULL, 0);
}

static const char *ape6evm_boards_compat_dt[] __initdata = {
	"renesas,ape6evm-reference",
	NULL,
};

DT_MACHINE_START(APE6EVM_DT, "ape6evm")
	.init_early	= r8a73a4_init_delay,
	.init_machine	= ape6evm_add_standard_devices,
	.dt_compat	= ape6evm_boards_compat_dt,
MACHINE_END
