/*
 * Abilis Systems TB10x platform initialisation
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/pinctrl/consumer.h>

#include <asm/mach_desc.h>


static void __init tb10x_platform_init(void)
{
	of_clk_init(NULL);
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static void __init tb10x_platform_late_init(void)
{
	struct device_node *dn;

	/*
	 * Pinctrl documentation recommends setting up the iomux here for
	 * all modules which don't require control over the pins themselves.
	 * Modules which need this kind of assistance are compatible with
	 * "abilis,simple-pinctrl", i.e. we can easily iterate over them.
	 * TODO: Does this recommended method work cleanly with pins required
	 * by modules?
	 */
	for_each_compatible_node(dn, NULL, "abilis,simple-pinctrl") {
		struct platform_device *pd = of_find_device_by_node(dn);
		struct pinctrl *pctl;

		pctl = pinctrl_get_select(&pd->dev, "abilis,simple-default");
		if (IS_ERR(pctl)) {
			int ret = PTR_ERR(pctl);
			dev_err(&pd->dev, "Could not set up pinctrl: %d\n",
				ret);
		}
	}
}

static const char *tb10x_compat[] __initdata = {
	"abilis,arc-tb10x",
	NULL,
};

MACHINE_START(TB10x, "tb10x")
	.dt_compat	= tb10x_compat,
	.init_machine	= tb10x_platform_init,
	.init_late	= tb10x_platform_late_init,
MACHINE_END
