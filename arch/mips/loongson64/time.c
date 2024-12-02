// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */

#include <asm/time.h>
#include <asm/hpet.h>

#include <loongson.h>
#include <linux/clk.h>
#include <linux/of_clk.h>

void __init plat_time_init(void)
{
	struct clk *clk;
	struct device_node *np;

	if (loongson_sysconf.fw_interface == LOONGSON_DTB) {
		of_clk_init(NULL);

		np = of_get_cpu_node(0, NULL);
		if (!np) {
			pr_err("Failed to get CPU node\n");
			return;
		}

		clk = of_clk_get(np, 0);
		if (IS_ERR(clk)) {
			pr_err("Failed to get CPU clock: %ld\n", PTR_ERR(clk));
			return;
		}

		cpu_clock_freq = clk_get_rate(clk);
		clk_put(clk);
	}

	/* setup mips r4k timer */
	mips_hpt_frequency = cpu_clock_freq / 2;

#ifdef CONFIG_RS780_HPET
	setup_hpet_timer();
#endif
}
