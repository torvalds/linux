/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Padmavathi Venna <padma.v@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Audio Subsystem Clock Controller.
*/

#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clk/exynos-audss-clk.h>

static DEFINE_SPINLOCK(lock);
static struct clk **clk_table;
static void __iomem *reg_base;
static struct clk_onecell_data clk_data;

#define ASS_CLK_SRC 0x0
#define ASS_CLK_DIV 0x4
#define ASS_CLK_GATE 0x8

static unsigned long reg_save[][2] = {
	{ASS_CLK_SRC,  0},
	{ASS_CLK_DIV,  0},
	{ASS_CLK_GATE, 0},
};

/* list of all parent clock list */
static const char *mout_audss_p[] = { "fin_pll", "fout_epll" };
static const char *mout_i2s_p[] = { "mout_audss", "cdclk0", "sclk_audio0" };

#ifdef CONFIG_PM_SLEEP
static int exynos_audss_clk_suspend(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(reg_save); i++)
		reg_save[i][1] = readl(reg_base + reg_save[i][0]);

	return 0;
}

static void exynos_audss_clk_resume(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(reg_save); i++)
		writel(reg_save[i][1], reg_base + reg_save[i][0]);
}

static struct syscore_ops exynos_audss_clk_syscore_ops = {
	.suspend	= exynos_audss_clk_suspend,
	.resume		= exynos_audss_clk_resume,
};
#endif /* CONFIG_PM_SLEEP */

/* register exynos_audss clocks */
void __init exynos_audss_clk_init(struct device_node *np)
{
	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: failed to map audss registers\n", __func__);
		return;
	}

	clk_table = kzalloc(sizeof(struct clk *) * EXYNOS_AUDSS_MAX_CLKS,
				GFP_KERNEL);
	if (!clk_table) {
		pr_err("%s: could not allocate clk lookup table\n", __func__);
		return;
	}

	clk_data.clks = clk_table;
	clk_data.clk_num = EXYNOS_AUDSS_MAX_CLKS;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	clk_table[EXYNOS_MOUT_AUDSS] = clk_register_mux(NULL, "mout_audss",
				mout_audss_p, ARRAY_SIZE(mout_audss_p), 0,
				reg_base + ASS_CLK_SRC, 0, 1, 0, &lock);

	clk_table[EXYNOS_MOUT_I2S] = clk_register_mux(NULL, "mout_i2s",
				mout_i2s_p, ARRAY_SIZE(mout_i2s_p), 0,
				reg_base + ASS_CLK_SRC, 2, 2, 0, &lock);

	clk_table[EXYNOS_DOUT_SRP] = clk_register_divider(NULL, "dout_srp",
				"mout_audss", 0, reg_base + ASS_CLK_DIV, 0, 4,
				0, &lock);

	clk_table[EXYNOS_DOUT_AUD_BUS] = clk_register_divider(NULL,
				"dout_aud_bus", "dout_srp", 0,
				reg_base + ASS_CLK_DIV, 4, 4, 0, &lock);

	clk_table[EXYNOS_DOUT_I2S] = clk_register_divider(NULL, "dout_i2s",
				"mout_i2s", 0, reg_base + ASS_CLK_DIV, 8, 4, 0,
				&lock);

	clk_table[EXYNOS_SRP_CLK] = clk_register_gate(NULL, "srp_clk",
				"dout_srp", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 0, 0, &lock);

	clk_table[EXYNOS_I2S_BUS] = clk_register_gate(NULL, "i2s_bus",
				"dout_aud_bus", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 2, 0, &lock);

	clk_table[EXYNOS_SCLK_I2S] = clk_register_gate(NULL, "sclk_i2s",
				"dout_i2s", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 3, 0, &lock);

	clk_table[EXYNOS_PCM_BUS] = clk_register_gate(NULL, "pcm_bus",
				 "sclk_pcm", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 4, 0, &lock);

	clk_table[EXYNOS_SCLK_PCM] = clk_register_gate(NULL, "sclk_pcm",
				"div_pcm0", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 5, 0, &lock);

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&exynos_audss_clk_syscore_ops);
#endif

	pr_info("Exynos: Audss: clock setup completed\n");
}
CLK_OF_DECLARE(exynos4210_audss_clk, "samsung,exynos4210-audss-clock",
		exynos_audss_clk_init);
CLK_OF_DECLARE(exynos5250_audss_clk, "samsung,exynos5250-audss-clock",
		exynos_audss_clk_init);
