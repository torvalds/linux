// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd
 */

#include <linux/module.h>
#include <soc/rockchip/pm_domains.h>
#include <../drivers/clk/rockchip/clk.h>

#ifdef MODULE
static int __init rockchip_disable_unused_driver_init(void)
{
	rockchip_pd_disable_unused();
	rockchip_clk_disable_unused();
	rockchip_clk_unprotect();

	return 0;
}
module_init(rockchip_disable_unused_driver_init);

MODULE_AUTHOR("Elaine Zhang <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip driver for disable unused clk and power domain");
MODULE_LICENSE("GPL");
#endif
