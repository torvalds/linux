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

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos-audss-clk.h>

enum exynos_audss_clk_type {
	TYPE_EXYNOS4210,
	TYPE_EXYNOS5250,
	TYPE_EXYNOS5420,
};

static DEFINE_SPINLOCK(lock);
static struct clk **clk_table;
static void __iomem *reg_base;
static struct clk_onecell_data clk_data;
/*
 * On Exynos5420 this will be a clock which has to be enabled before any
 * access to audss registers. Typically a child of EPLL.
 *
 * On other platforms this will be -ENODEV.
 */
static struct clk *epll;

#define ASS_CLK_SRC 0x0
#define ASS_CLK_DIV 0x4
#define ASS_CLK_GATE 0x8

#ifdef CONFIG_PM_SLEEP
static unsigned long reg_save[][2] = {
	{ASS_CLK_SRC,  0},
	{ASS_CLK_DIV,  0},
	{ASS_CLK_GATE, 0},
};

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

static const struct of_device_id exynos_audss_clk_of_match[] = {
	{ .compatible = "samsung,exynos4210-audss-clock",
	  .data = (void *)TYPE_EXYNOS4210, },
	{ .compatible = "samsung,exynos5250-audss-clock",
	  .data = (void *)TYPE_EXYNOS5250, },
	{ .compatible = "samsung,exynos5420-audss-clock",
	  .data = (void *)TYPE_EXYNOS5420, },
	{},
};

static void exynos_audss_clk_teardown(void)
{
	int i;

	for (i = EXYNOS_MOUT_AUDSS; i < EXYNOS_DOUT_SRP; i++) {
		if (!IS_ERR(clk_table[i]))
			clk_unregister_mux(clk_table[i]);
	}

	for (; i < EXYNOS_SRP_CLK; i++) {
		if (!IS_ERR(clk_table[i]))
			clk_unregister_divider(clk_table[i]);
	}

	for (; i < clk_data.clk_num; i++) {
		if (!IS_ERR(clk_table[i]))
			clk_unregister_gate(clk_table[i]);
	}
}

/* register exynos_audss clocks */
static int exynos_audss_clk_probe(struct platform_device *pdev)
{
	int i, ret = 0;
	struct resource *res;
	const char *mout_audss_p[] = {"fin_pll", "fout_epll"};
	const char *mout_i2s_p[] = {"mout_audss", "cdclk0", "sclk_audio0"};
	const char *sclk_pcm_p = "sclk_pcm0";
	struct clk *pll_ref, *pll_in, *cdclk, *sclk_audio, *sclk_pcm_in;
	const struct of_device_id *match;
	enum exynos_audss_clk_type variant;

	match = of_match_node(exynos_audss_clk_of_match, pdev->dev.of_node);
	if (!match)
		return -EINVAL;
	variant = (enum exynos_audss_clk_type)match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reg_base)) {
		dev_err(&pdev->dev, "failed to map audss registers\n");
		return PTR_ERR(reg_base);
	}
	/* EPLL don't have to be enabled for boards other than Exynos5420 */
	epll = ERR_PTR(-ENODEV);

	clk_table = devm_kzalloc(&pdev->dev,
				sizeof(struct clk *) * EXYNOS_AUDSS_MAX_CLKS,
				GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	clk_data.clks = clk_table;
	if (variant == TYPE_EXYNOS5420)
		clk_data.clk_num = EXYNOS_AUDSS_MAX_CLKS;
	else
		clk_data.clk_num = EXYNOS_AUDSS_MAX_CLKS - 1;

	pll_ref = devm_clk_get(&pdev->dev, "pll_ref");
	pll_in = devm_clk_get(&pdev->dev, "pll_in");
	if (!IS_ERR(pll_ref))
		mout_audss_p[0] = __clk_get_name(pll_ref);
	if (!IS_ERR(pll_in)) {
		mout_audss_p[1] = __clk_get_name(pll_in);

		if (variant == TYPE_EXYNOS5420) {
			epll = pll_in;

			ret = clk_prepare_enable(epll);
			if (ret) {
				dev_err(&pdev->dev,
						"failed to prepare the epll clock\n");
				return ret;
			}
		}
	}
	clk_table[EXYNOS_MOUT_AUDSS] = clk_register_mux(NULL, "mout_audss",
				mout_audss_p, ARRAY_SIZE(mout_audss_p),
				CLK_SET_RATE_NO_REPARENT,
				reg_base + ASS_CLK_SRC, 0, 1, 0, &lock);

	cdclk = devm_clk_get(&pdev->dev, "cdclk");
	sclk_audio = devm_clk_get(&pdev->dev, "sclk_audio");
	if (!IS_ERR(cdclk))
		mout_i2s_p[1] = __clk_get_name(cdclk);
	if (!IS_ERR(sclk_audio))
		mout_i2s_p[2] = __clk_get_name(sclk_audio);
	clk_table[EXYNOS_MOUT_I2S] = clk_register_mux(NULL, "mout_i2s",
				mout_i2s_p, ARRAY_SIZE(mout_i2s_p),
				CLK_SET_RATE_NO_REPARENT,
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

	sclk_pcm_in = devm_clk_get(&pdev->dev, "sclk_pcm_in");
	if (!IS_ERR(sclk_pcm_in))
		sclk_pcm_p = __clk_get_name(sclk_pcm_in);
	clk_table[EXYNOS_SCLK_PCM] = clk_register_gate(NULL, "sclk_pcm",
				sclk_pcm_p, CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 5, 0, &lock);

	if (variant == TYPE_EXYNOS5420) {
		clk_table[EXYNOS_ADMA] = clk_register_gate(NULL, "adma",
				"dout_srp", CLK_SET_RATE_PARENT,
				reg_base + ASS_CLK_GATE, 9, 0, &lock);
	}

	for (i = 0; i < clk_data.clk_num; i++) {
		if (IS_ERR(clk_table[i])) {
			dev_err(&pdev->dev, "failed to register clock %d\n", i);
			ret = PTR_ERR(clk_table[i]);
			goto unregister;
		}
	}

	ret = of_clk_add_provider(pdev->dev.of_node, of_clk_src_onecell_get,
					&clk_data);
	if (ret) {
		dev_err(&pdev->dev, "failed to add clock provider\n");
		goto unregister;
	}

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&exynos_audss_clk_syscore_ops);
#endif

	dev_info(&pdev->dev, "setup completed\n");

	return 0;

unregister:
	exynos_audss_clk_teardown();

	if (!IS_ERR(epll))
		clk_disable_unprepare(epll);

	return ret;
}

static int exynos_audss_clk_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_SLEEP
	unregister_syscore_ops(&exynos_audss_clk_syscore_ops);
#endif

	of_clk_del_provider(pdev->dev.of_node);

	exynos_audss_clk_teardown();

	if (!IS_ERR(epll))
		clk_disable_unprepare(epll);

	return 0;
}

static struct platform_driver exynos_audss_clk_driver = {
	.driver	= {
		.name = "exynos-audss-clk",
		.of_match_table = exynos_audss_clk_of_match,
	},
	.probe = exynos_audss_clk_probe,
	.remove = exynos_audss_clk_remove,
};

module_platform_driver(exynos_audss_clk_driver);

MODULE_AUTHOR("Padmavathi Venna <padma.v@samsung.com>");
MODULE_DESCRIPTION("Exynos Audio Subsystem Clock Controller");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos-audss-clk");
