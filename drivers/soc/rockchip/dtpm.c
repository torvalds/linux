// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * DTPM hierarchy description
 */
#include <linux/dtpm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static struct dtpm_node __initdata rk3399_hierarchy[] = {
	[0]{ .name = "rk3399",
	     .type = DTPM_NODE_VIRTUAL },
	[1]{ .name = "package",
	     .type = DTPM_NODE_VIRTUAL,
	     .parent = &rk3399_hierarchy[0] },
	[2]{ .name = "/cpus/cpu@0",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[3]{ .name = "/cpus/cpu@1",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[4]{ .name = "/cpus/cpu@2",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[5]{ .name = "/cpus/cpu@3",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[6]{ .name = "/cpus/cpu@100",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[7]{ .name = "/cpus/cpu@101",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[8]{ .name = "/gpu@ff9a0000",
	     .type = DTPM_NODE_DT,
	     .parent = &rk3399_hierarchy[1] },
	[9]{ /* sentinel */ }
};

static struct of_device_id __initdata rockchip_dtpm_match_table[] = {
        { .compatible = "rockchip,rk3399", .data = rk3399_hierarchy },
        {},
};

static int __init rockchip_dtpm_init(void)
{
	return dtpm_create_hierarchy(rockchip_dtpm_match_table);
}
module_init(rockchip_dtpm_init);

MODULE_SOFTDEP("pre: panfrost cpufreq-dt");
MODULE_DESCRIPTION("Rockchip DTPM driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dtpm");
MODULE_AUTHOR("Daniel Lezcano <daniel.lezcano@kernel.org");
