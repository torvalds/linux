/* linux/arch/arm/plat-s5p/dev-tmu.c
 *
 * Copyright 2009 by SAMSUNG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/irq.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/devs.h>
#include <plat/s5p-tmu.h>

static struct resource tmu_resource[] = {
	[0] = {
		.start	= S5P_PA_TMU,
		.end	= S5P_PA_TMU + 0xFFFF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_TMU,
		.end	= IRQ_TMU,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device exynos_device_tmu = {
	.name	= "s5p-tmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(tmu_resource),
	.resource	= tmu_resource,
};

static struct tmu_data default_tmu_data __initdata = {
	.ts = {
		.stop_throttle  = 0,
		.start_throttle = 0,
		.stop_warning  = 0,
		.start_warning = 0,
		.start_tripping = 0,
	},
	.cpulimit = {
		.throttle_freq = 0,
		.warning_freq = 0,
	},
	.efuse_value = 0,
	.slope = 0,
	.mode = 0,
};

int s5p_tmu_get_irqno(int num)
{
	return platform_get_irq(&exynos_device_tmu, num);
}

struct tmu_info *s5p_tmu_get_platdata(void)
{
	return platform_get_drvdata(&exynos_device_tmu);
}

void __init s5p_tmu_set_platdata(struct tmu_data *pd)
{
	struct tmu_data *npd;

	npd = kmalloc(sizeof(struct tmu_data), GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	} else {
		if (!pd->ts.stop_throttle)
			memcpy(&npd->ts, &default_tmu_data.ts, sizeof(struct temperature_params));
		else
			memcpy(&npd->ts, &pd->ts, sizeof(struct tmu_data));
	}
	exynos_device_tmu.dev.platform_data = npd;
}
