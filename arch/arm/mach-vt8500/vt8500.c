/*
 *  arch/arm/mach-vt8500/vt8500.c
 *
 *  Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/clocksource.h>
#include <linux/io.h>
#include <linux/pm.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include "common.h"

#define LEGACY_GPIO_BASE	0xD8110000
#define LEGACY_PMC_BASE		0xD8130000

/* Registers in GPIO Controller */
#define VT8500_GPIO_MUX_REG	0x200

/* Registers in Power Management Controller */
#define VT8500_HCR_REG		0x12
#define VT8500_PMSR_REG		0x60

static void __iomem *pmc_base;

void vt8500_restart(char mode, const char *cmd)
{
	if (pmc_base)
		writel(1, pmc_base + VT8500_PMSR_REG);
}

static struct map_desc vt8500_io_desc[] __initdata = {
	/* SoC MMIO registers */
	[0] = {
		.virtual	= 0xf8000000,
		.pfn		= __phys_to_pfn(0xd8000000),
		.length		= 0x00390000, /* max of all chip variants */
		.type		= MT_DEVICE
	},
};

void __init vt8500_map_io(void)
{
	iotable_init(vt8500_io_desc, ARRAY_SIZE(vt8500_io_desc));
}

static void vt8500_power_off(void)
{
	local_irq_disable();
	writew(5, pmc_base + VT8500_HCR_REG);
	asm("mcr%? p15, 0, %0, c7, c0, 4" : : "r" (0));
}

void __init vt8500_init(void)
{
	struct device_node *np;
#if defined(CONFIG_FB_VT8500) || defined(CONFIG_FB_WM8505)
	struct device_node *fb;
	void __iomem *gpio_base;
#endif

#ifdef CONFIG_FB_VT8500
	fb = of_find_compatible_node(NULL, NULL, "via,vt8500-fb");
	if (fb) {
		np = of_find_compatible_node(NULL, NULL, "via,vt8500-gpio");
		if (np) {
			gpio_base = of_iomap(np, 0);

			if (!gpio_base)
				pr_err("%s: of_iomap(gpio_mux) failed\n",
								__func__);

			of_node_put(np);
		} else {
			gpio_base = ioremap(LEGACY_GPIO_BASE, 0x1000);
			if (!gpio_base)
				pr_err("%s: ioremap(legacy_gpio_mux) failed\n",
								__func__);
		}
		if (gpio_base) {
			writel(readl(gpio_base + VT8500_GPIO_MUX_REG) | 1,
				gpio_base + VT8500_GPIO_MUX_REG);
			iounmap(gpio_base);
		} else
			pr_err("%s: Could not remap GPIO mux\n", __func__);

		of_node_put(fb);
	}
#endif

#ifdef CONFIG_FB_WM8505
	fb = of_find_compatible_node(NULL, NULL, "wm,wm8505-fb");
	if (fb) {
		np = of_find_compatible_node(NULL, NULL, "wm,wm8505-gpio");
		if (!np)
			np = of_find_compatible_node(NULL, NULL,
							"wm,wm8650-gpio");
		if (np) {
			gpio_base = of_iomap(np, 0);

			if (!gpio_base)
				pr_err("%s: of_iomap(gpio_mux) failed\n",
								__func__);

			of_node_put(np);
		} else {
			gpio_base = ioremap(LEGACY_GPIO_BASE, 0x1000);
			if (!gpio_base)
				pr_err("%s: ioremap(legacy_gpio_mux) failed\n",
								__func__);
		}
		if (gpio_base) {
			writel(readl(gpio_base + VT8500_GPIO_MUX_REG) |
				0x80000000, gpio_base + VT8500_GPIO_MUX_REG);
			iounmap(gpio_base);
		} else
			pr_err("%s: Could not remap GPIO mux\n", __func__);

		of_node_put(fb);
	}
#endif

	np = of_find_compatible_node(NULL, NULL, "via,vt8500-pmc");
	if (np) {
		pmc_base = of_iomap(np, 0);

		if (!pmc_base)
			pr_err("%s:of_iomap(pmc) failed\n", __func__);

		of_node_put(np);
	} else {
		pmc_base = ioremap(LEGACY_PMC_BASE, 0x1000);
		if (!pmc_base)
			pr_err("%s:ioremap(power_off) failed\n", __func__);
	}
	if (pmc_base)
		pm_power_off = &vt8500_power_off;
	else
		pr_err("%s: PMC Hibernation register could not be remapped, not enabling power off!\n", __func__);

	vtwm_clk_init(pmc_base);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const vt8500_dt_compat[] = {
	"via,vt8500",
	"wm,wm8650",
	"wm,wm8505",
	"wm,wm8750",
	"wm,wm8850",
	NULL
};

DT_MACHINE_START(WMT_DT, "VIA/Wondermedia SoC (Device Tree Support)")
	.dt_compat	= vt8500_dt_compat,
	.map_io		= vt8500_map_io,
	.init_machine	= vt8500_init,
	.init_time	= clocksource_of_init,
	.restart	= vt8500_restart,
MACHINE_END

