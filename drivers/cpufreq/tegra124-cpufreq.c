// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra 124 cpufreq driver
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/types.h>

struct tegra124_cpufreq_priv {
	struct clk *cpu_clk;
	struct clk *pllp_clk;
	struct clk *pllx_clk;
	struct clk *dfll_clk;
	struct platform_device *cpufreq_dt_pdev;
};

static int tegra124_cpu_switch_to_dfll(struct tegra124_cpufreq_priv *priv)
{
	struct clk *orig_parent;
	int ret;

	ret = clk_set_rate(priv->dfll_clk, clk_get_rate(priv->cpu_clk));
	if (ret)
		return ret;

	orig_parent = clk_get_parent(priv->cpu_clk);
	clk_set_parent(priv->cpu_clk, priv->pllp_clk);

	ret = clk_prepare_enable(priv->dfll_clk);
	if (ret)
		goto out;

	clk_set_parent(priv->cpu_clk, priv->dfll_clk);

	return 0;

out:
	clk_set_parent(priv->cpu_clk, orig_parent);

	return ret;
}

static int tegra124_cpufreq_probe(struct platform_device *pdev)
{
	struct tegra124_cpufreq_priv *priv;
	struct device_node *np;
	struct device *cpu_dev;
	struct platform_device_info cpufreq_dt_devinfo = {};
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -ENODEV;

	np = of_cpu_device_node_get(0);
	if (!np)
		return -ENODEV;

	priv->cpu_clk = of_clk_get_by_name(np, "cpu_g");
	if (IS_ERR(priv->cpu_clk)) {
		ret = PTR_ERR(priv->cpu_clk);
		goto out_put_np;
	}

	priv->dfll_clk = of_clk_get_by_name(np, "dfll");
	if (IS_ERR(priv->dfll_clk)) {
		ret = PTR_ERR(priv->dfll_clk);
		goto out_put_cpu_clk;
	}

	priv->pllx_clk = of_clk_get_by_name(np, "pll_x");
	if (IS_ERR(priv->pllx_clk)) {
		ret = PTR_ERR(priv->pllx_clk);
		goto out_put_dfll_clk;
	}

	priv->pllp_clk = of_clk_get_by_name(np, "pll_p");
	if (IS_ERR(priv->pllp_clk)) {
		ret = PTR_ERR(priv->pllp_clk);
		goto out_put_pllx_clk;
	}

	ret = tegra124_cpu_switch_to_dfll(priv);
	if (ret)
		goto out_put_pllp_clk;

	cpufreq_dt_devinfo.name = "cpufreq-dt";
	cpufreq_dt_devinfo.parent = &pdev->dev;

	priv->cpufreq_dt_pdev =
		platform_device_register_full(&cpufreq_dt_devinfo);
	if (IS_ERR(priv->cpufreq_dt_pdev)) {
		ret = PTR_ERR(priv->cpufreq_dt_pdev);
		goto out_put_pllp_clk;
	}

	platform_set_drvdata(pdev, priv);

	of_node_put(np);

	return 0;

out_put_pllp_clk:
	clk_put(priv->pllp_clk);
out_put_pllx_clk:
	clk_put(priv->pllx_clk);
out_put_dfll_clk:
	clk_put(priv->dfll_clk);
out_put_cpu_clk:
	clk_put(priv->cpu_clk);
out_put_np:
	of_node_put(np);

	return ret;
}

static int __maybe_unused tegra124_cpufreq_suspend(struct device *dev)
{
	struct tegra124_cpufreq_priv *priv = dev_get_drvdata(dev);
	int err;

	/*
	 * PLLP rate 408Mhz is below the CPU Fmax at Vmin and is safe to
	 * use during suspend and resume. So, switch the CPU clock source
	 * to PLLP and disable DFLL.
	 */
	err = clk_set_parent(priv->cpu_clk, priv->pllp_clk);
	if (err < 0) {
		dev_err(dev, "failed to reparent to PLLP: %d\n", err);
		return err;
	}

	clk_disable_unprepare(priv->dfll_clk);

	return 0;
}

static int __maybe_unused tegra124_cpufreq_resume(struct device *dev)
{
	struct tegra124_cpufreq_priv *priv = dev_get_drvdata(dev);
	int err;

	/*
	 * Warmboot code powers up the CPU with PLLP clock source.
	 * Enable DFLL clock and switch CPU clock source back to DFLL.
	 */
	err = clk_prepare_enable(priv->dfll_clk);
	if (err < 0) {
		dev_err(dev, "failed to enable DFLL clock for CPU: %d\n", err);
		goto disable_cpufreq;
	}

	err = clk_set_parent(priv->cpu_clk, priv->dfll_clk);
	if (err < 0) {
		dev_err(dev, "failed to reparent to DFLL clock: %d\n", err);
		goto disable_dfll;
	}

	return 0;

disable_dfll:
	clk_disable_unprepare(priv->dfll_clk);
disable_cpufreq:
	disable_cpufreq();

	return err;
}

static const struct dev_pm_ops tegra124_cpufreq_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra124_cpufreq_suspend,
				tegra124_cpufreq_resume)
};

static struct platform_driver tegra124_cpufreq_platdrv = {
	.driver.name	= "cpufreq-tegra124",
	.driver.pm	= &tegra124_cpufreq_pm_ops,
	.probe		= tegra124_cpufreq_probe,
};

static int __init tegra_cpufreq_init(void)
{
	int ret;
	struct platform_device *pdev;

	if (!(of_machine_is_compatible("nvidia,tegra124") ||
		of_machine_is_compatible("nvidia,tegra210")))
		return -ENODEV;

	/*
	 * Platform driver+device required for handling EPROBE_DEFER with
	 * the regulator and the DFLL clock
	 */
	ret = platform_driver_register(&tegra124_cpufreq_platdrv);
	if (ret)
		return ret;

	pdev = platform_device_register_simple("cpufreq-tegra124", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&tegra124_cpufreq_platdrv);
		return PTR_ERR(pdev);
	}

	return 0;
}
module_init(tegra_cpufreq_init);

MODULE_AUTHOR("Tuomas Tynkkynen <ttynkkynen@nvidia.com>");
MODULE_DESCRIPTION("cpufreq driver for NVIDIA Tegra124");
