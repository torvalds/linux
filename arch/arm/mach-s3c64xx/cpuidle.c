/* linux/arch/arm/mach-s3c64xx/cpuidle.c
 *
 * Copyright (c) 2011 Wolfson Microelectronics, plc
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpuidle.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/time.h>

#include <asm/proc-fns.h>

#include <mach/map.h>

#include <mach/regs-sys.h>
#include <mach/regs-syscon-power.h>

static int s3c64xx_enter_idle(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index)
{
	struct timeval before, after;
	unsigned long tmp;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);

	/* Setup PWRCFG to enter idle mode */
	tmp = __raw_readl(S3C64XX_PWR_CFG);
	tmp &= ~S3C64XX_PWRCFG_CFG_WFI_MASK;
	tmp |= S3C64XX_PWRCFG_CFG_WFI_IDLE;
	__raw_writel(tmp, S3C64XX_PWR_CFG);

	cpu_do_idle();

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
		    (after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

static struct cpuidle_state s3c64xx_cpuidle_set[] = {
	[0] = {
		.enter			= s3c64xx_enter_idle,
		.exit_latency		= 1,
		.target_residency	= 1,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "IDLE",
		.desc			= "System active, ARM gated",
	},
};

static struct cpuidle_driver s3c64xx_cpuidle_driver = {
	.name		= "s3c64xx_cpuidle",
	.owner		= THIS_MODULE,
	.state_count	= ARRAY_SIZE(s3c64xx_cpuidle_set),
};

static struct cpuidle_device s3c64xx_cpuidle_device = {
	.state_count	= ARRAY_SIZE(s3c64xx_cpuidle_set),
};

static int __init s3c64xx_init_cpuidle(void)
{
	int ret;

	memcpy(s3c64xx_cpuidle_driver.states, s3c64xx_cpuidle_set,
	       sizeof(s3c64xx_cpuidle_set));
	cpuidle_register_driver(&s3c64xx_cpuidle_driver);

	ret = cpuidle_register_device(&s3c64xx_cpuidle_device);
	if (ret) {
		pr_err("Failed to register cpuidle device: %d\n", ret);
		return ret;
	}

	return 0;
}
device_initcall(s3c64xx_init_cpuidle);
