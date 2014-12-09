/*
 * Copyright (C) 2014 ROCKCHIP, Inc.
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

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/pmu.h>
#include "rk3126b.h"
#define CPU 3126b
#include "sram.h"
#include "pm.h"
#include "pm-rk312x.c"

char PIE_DATA(sram_stack)[1024];
EXPORT_PIE_SYMBOL(DATA(sram_stack));

static int __init rk3126b_pie_init(void)
{
	int err;

	if (!soc_is_rk3126b())
		return 0;

	err = rockchip_pie_init();
	if (err)
		return err;

	rockchip_pie_chunk = pie_load_sections(rockchip_sram_pool, rk3126b);
	if (IS_ERR(rockchip_pie_chunk)) {
		err = PTR_ERR(rockchip_pie_chunk);
		pr_err("%s: failed to load section %d\n", __func__, err);
		rockchip_pie_chunk = NULL;
		return err;
	}

	rockchip_sram_virt = kern_to_pie(rockchip_pie_chunk, &__pie_common_start[0]);
	rockchip_sram_stack = kern_to_pie(rockchip_pie_chunk, (char *)DATA(sram_stack) + sizeof(DATA(sram_stack)));

	return 0;
}
arch_initcall(rk3126b_pie_init);

#ifdef CONFIG_PM
void __init rk3126b_init_suspend(void)
{
	pr_info("%s\n", __func__);
	rkpm_pie_init();
	rk312x_suspend_init();
}
#endif

#include "ddr_rk3126b.c"
static int __init rk3126b_ddr_init(void)
{
	if (!soc_is_rk3126b())
		return 0;

	ddr_change_freq = _ddr_change_freq;
	ddr_round_rate = _ddr_round_rate;
	ddr_set_auto_self_refresh = _ddr_set_auto_self_refresh;
	ddr_bandwidth_get = _ddr_bandwidth_get;
	ddr_init(DDR3_DEFAULT, 0);

	return 0;
}
arch_initcall_sync(rk3126b_ddr_init);
