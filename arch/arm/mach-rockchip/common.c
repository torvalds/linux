/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
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

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>
#include "cpu_axi.h"

static int __init rockchip_cpu_axi_init(void)
{
	struct device_node *np, *cp;
	void __iomem *base, *cbase;

	np = of_find_compatible_node(NULL, NULL, "rockchip,cpu_axi_bus");
	if (!np)
		return -ENODEV;

	base = of_iomap(np, 0);

	np = of_get_child_by_name(np, "qos");
	if (np) {
		for_each_child_of_node(np, cp) {
			u32 offset, priority[2], mode, bandwidth, saturation;
			if (of_property_read_u32(cp, "rockchip,offset", &offset))
				continue;
			pr_debug("qos: %s offset %x\n", cp->name, offset);
			cbase = base + offset;
			if (!of_property_read_u32_array(cp, "rockchip,priority", priority, ARRAY_SIZE(priority))) {
				CPU_AXI_SET_QOS_PRIORITY(priority[0], priority[1], cbase);
				pr_debug("qos: %s priority %x %x\n", cp->name, priority[0], priority[1]);
			}
			if (!of_property_read_u32(cp, "rockchip,mode", &mode)) {
				CPU_AXI_SET_QOS_MODE(mode, cbase);
				pr_debug("qos: %s mode %x\n", cp->name, mode);
			}
			if (!of_property_read_u32(cp, "rockchip,bandwidth", &bandwidth)) {
				CPU_AXI_SET_QOS_BANDWIDTH(bandwidth, cbase);
				pr_debug("qos: %s bandwidth %x\n", cp->name, bandwidth);
			}
			if (!of_property_read_u32(cp, "rockchip,saturation", &saturation)) {
				CPU_AXI_SET_QOS_SATURATION(saturation, cbase);
				pr_debug("qos: %s saturation %x\n", cp->name, saturation);
			}
		}
	};

	writel_relaxed(0x3f, base + 0x0014);	// memory scheduler read latency
	dsb();

	iounmap(base);

	return 0;
}
early_initcall(rockchip_cpu_axi_init);

static const struct of_device_id pl330_ids[] __initconst = {
	{ .compatible = "arm,pl310-cache" },
	{}
};

static int __init rockchip_pl330_l2_cache_init(void)
{
	struct device_node *np;
	void __iomem *base;
	u32 aux[2] = { 0, ~0 }, prefetch, power;

	np = of_find_matching_node(NULL, pl330_ids);
	if (!np)
		return -ENODEV;

	base = of_iomap(np, 0);
	if (!base)
		return -EINVAL;

	if (!of_property_read_u32(np, "rockchip,prefetch-ctrl", &prefetch)) {
		/* L2X0 Prefetch Control */
		writel_relaxed(prefetch, base + L2X0_PREFETCH_CTRL);
		pr_debug("l2c: prefetch %x\n", prefetch);
	}

	if (!of_property_read_u32(np, "rockchip,power-ctrl", &power)) {
		/* L2X0 Power Control */
		writel_relaxed(power, base + L2X0_POWER_CTRL);
		pr_debug("l2c: power %x\n", power);
	}

	iounmap(base);

	of_property_read_u32_array(np, "rockchip,aux-ctrl", aux, ARRAY_SIZE(aux));
	pr_debug("l2c: aux %08x mask %08x\n", aux[0], aux[1]);

	l2x0_of_init(aux[0], aux[1]);

	return 0;
}
early_initcall(rockchip_pl330_l2_cache_init);
