/*
 * Tegra 124 cpufreq driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

struct tegra124_cpufreq_priv {
	struct regulator *vdd_cpu_reg;
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

static void tegra124_cpu_switch_to_pllx(struct tegra124_cpufreq_priv *priv)
{
	clk_set_parent(priv->cpu_clk, priv->pllp_clk);
	clk_disable_unprepare(priv->dfll_clk);
	regulator_sync_voltage(priv->vdd_cpu_reg);
	clk_set_parent(priv->cpu_clk, priv->pllx_clk);
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

	priv->vdd_cpu_reg = regulator_get(cpu_dev, "vdd-cpu");
	if (IS_ERR(priv->vdd_cpu_reg)) {
		ret = PTR_ERR(priv->vdd_cpu_reg);
		goto out_put_np;
	}

	priv->cpu_clk = of_clk_get_by_name(np, "cpu_g");
	if (IS_ERR(priv->cpu_clk)) {
		ret = PTR_ERR(priv->cpu_clk);
		goto out_put_vdd_cpu_reg;
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
		goto out_switch_to_pllx;
	}

	platform_set_drvdata(pdev, priv);

	of_node_put(np);

	return 0;

out_switch_to_pllx:
	tegra124_cpu_switch_to_pllx(priv);
out_put_pllp_clk:
	clk_put(priv->pllp_clk);
out_put_pllx_clk:
	clk_put(priv->pllx_clk);
out_put_dfll_clk:
	clk_put(priv->dfll_clk);
out_put_cpu_clk:
	clk_put(priv->cpu_clk);
out_put_vdd_cpu_reg:
	regulator_put(priv->vdd_cpu_reg);
out_put_np:
	of_node_put(np);

	return ret;
}

static int tegra124_cpufreq_remove(struct platform_device *pdev)
{
	struct tegra124_cpufreq_priv *priv = platform_get_drvdata(pdev);

	platform_device_unregister(priv->cpufreq_dt_pdev);
	tegra124_cpu_switch_to_pllx(priv);

	clk_put(priv->pllp_clk);
	clk_put(priv->pllx_clk);
	clk_put(priv->dfll_clk);
	clk_put(priv->cpu_clk);
	regulator_put(priv->vdd_cpu_reg);

	return 0;
}

static struct platform_driver tegra124_cpufreq_platdrv = {
	.driver.name	= "cpufreq-tegra124",
	.probe		= tegra124_cpufreq_probe,
	.remove		= tegra124_cpufreq_remove,
};

static int __init tegra_cpufreq_init(void)
{
	int ret;
	struct platform_device *pdev;

	if (!of_machine_is_compatible("nvidia,tegra124"))
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
MODULE_LICENSE("GPL v2");
