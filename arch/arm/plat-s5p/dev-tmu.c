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

static struct resource s5p_tmu_resource[] = {
	[0] = {
		.start	= S5P_PA_TMU,
		.end	= S5P_PA_TMU + 0xFFFF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_TMU,
		.end	= IRQ_TMU,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_tmu = {
	.name	= "s5p-tmu",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_tmu_resource),
	.resource	= s5p_tmu_resource,
};

/*
 * The below temperature value is derived from testing smdk4x12 board
 * in chamber
 * 78 degree  : threshold temp
 * 87 degree  : throttling temperature
 * 103 degree : Waring temperature
 * 110 degree : Tripping temperature
 * 85 degree  : Memory throttling
 * 120 degree : To protect chip,forcely kernel panic
*/
static struct s5p_platform_tmu default_tmu_data __initdata = {
	.ts = {
		.stop_1st_throttle  = 78,
		.start_1st_throttle = 80,
		.stop_2nd_throttle  = 87,
		.start_2nd_throttle = 103,
		.start_tripping     = 110,
		.start_emergency    = 120,
		.stop_mem_throttle  = 80,
		.start_mem_throttle = 85,
	},
	.cpufreq = {
		.limit_1st_throttle = 800000,
		.limit_2nd_throttle = 200000,
	},
	.mp = {
		.rclk = 24000000,
		.period_bank_refresh = 3900,
	},
	.cfg = {
		.mode  = 1,
		.slope = 80,
		.sampling_rate   = 1000,
		.monitoring_rate = 10000,
	},
};

int s5p_tmu_get_irqno(int num)
{
	return platform_get_irq(&s5p_device_tmu, num);
}

struct s5p_tmu *s5p_tmu_get_platdata(void)
{
	return platform_get_drvdata(&s5p_device_tmu);
}

void __init s5p_tmu_set_platdata(struct s5p_platform_tmu *pd)
{
	struct s5p_platform_tmu *npd;

	npd = kmalloc(sizeof(struct s5p_platform_tmu), GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	} else {
		if (!pd->ts.stop_1st_throttle)
			memcpy(&npd->ts, &default_tmu_data.ts,
				sizeof(struct temperature_params));
		else
			memcpy(&npd->ts, &pd->ts,
				sizeof(struct temperature_params));

		if (!(pd->cpufreq.limit_1st_throttle))
			memcpy(&npd->cpufreq, &default_tmu_data.cpufreq,
				sizeof(struct cpufreq_params));
		else
			memcpy(&npd->cpufreq, &pd->cpufreq,
				 sizeof(struct cpufreq_params));

		if (pd->temp_compensate.arm_volt)
			memcpy(&npd->temp_compensate, &pd->temp_compensate,
				 sizeof(struct temp_compensate_params));

		s5p_device_tmu.dev.platform_data = npd;
	}
}
