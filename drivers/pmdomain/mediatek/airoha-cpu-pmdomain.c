// SPDX-License-Identifier: GPL-2.0

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

#define AIROHA_SIP_AVS_HANDLE			0x82000301
#define AIROHA_AVS_OP_BASE			0xddddddd0
#define AIROHA_AVS_OP_MASK			GENMASK(1, 0)
#define AIROHA_AVS_OP_FREQ_DYN_ADJ		(AIROHA_AVS_OP_BASE | \
						 FIELD_PREP(AIROHA_AVS_OP_MASK, 0x1))
#define AIROHA_AVS_OP_GET_FREQ			(AIROHA_AVS_OP_BASE | \
						 FIELD_PREP(AIROHA_AVS_OP_MASK, 0x2))

struct airoha_cpu_pmdomain_priv {
	struct clk_hw hw;
	struct generic_pm_domain pd;
};

static int airoha_cpu_pmdomain_clk_determine_rate(struct clk_hw *hw,
						  struct clk_rate_request *req)
{
	return 0;
}

static unsigned long airoha_cpu_pmdomain_clk_get(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(AIROHA_SIP_AVS_HANDLE, AIROHA_AVS_OP_GET_FREQ,
			     0, 0, 0, 0, 0, 0, &res);

	/* SMCCC returns freq in MHz */
	return (int)(res.a0 * 1000 * 1000);
}

/* Airoha CPU clk SMCC is always enabled */
static int airoha_cpu_pmdomain_clk_is_enabled(struct clk_hw *hw)
{
	return true;
}

static const struct clk_ops airoha_cpu_pmdomain_clk_ops = {
	.recalc_rate = airoha_cpu_pmdomain_clk_get,
	.is_enabled = airoha_cpu_pmdomain_clk_is_enabled,
	.determine_rate = airoha_cpu_pmdomain_clk_determine_rate,
};

static int airoha_cpu_pmdomain_set_performance_state(struct generic_pm_domain *domain,
						     unsigned int state)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(AIROHA_SIP_AVS_HANDLE, AIROHA_AVS_OP_FREQ_DYN_ADJ,
			     0, state, 0, 0, 0, 0, &res);

	/* SMC signal correct apply by unsetting BIT 0 */
	return res.a0 & BIT(0) ? -EINVAL : 0;
}

static int airoha_cpu_pmdomain_probe(struct platform_device *pdev)
{
	struct airoha_cpu_pmdomain_priv *priv;
	struct device *dev = &pdev->dev;
	const struct clk_init_data init = {
		.name = "cpu",
		.ops = &airoha_cpu_pmdomain_clk_ops,
		/* Clock with no set_rate, can't cache */
		.flags = CLK_GET_RATE_NOCACHE,
	};
	struct generic_pm_domain *pd;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Init and register a get-only clk for Cpufreq */
	priv->hw.init = &init;
	ret = devm_clk_hw_register(dev, &priv->hw);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &priv->hw);
	if (ret)
		return ret;

	/* Init and register a PD for CPU */
	pd = &priv->pd;
	pd->name = "cpu_pd";
	pd->flags = GENPD_FLAG_ALWAYS_ON;
	pd->set_performance_state = airoha_cpu_pmdomain_set_performance_state;

	ret = pm_genpd_init(pd, NULL, false);
	if (ret)
		return ret;

	ret = of_genpd_add_provider_simple(dev->of_node, pd);
	if (ret)
		goto err_add_provider;

	platform_set_drvdata(pdev, priv);

	return 0;

err_add_provider:
	pm_genpd_remove(pd);

	return ret;
}

static void airoha_cpu_pmdomain_remove(struct platform_device *pdev)
{
	struct airoha_cpu_pmdomain_priv *priv = platform_get_drvdata(pdev);

	of_genpd_del_provider(pdev->dev.of_node);
	pm_genpd_remove(&priv->pd);
}

static const struct of_device_id airoha_cpu_pmdomain_of_match[] = {
	{ .compatible = "airoha,en7581-cpufreq" },
	{ },
};
MODULE_DEVICE_TABLE(of, airoha_cpu_pmdomain_of_match);

static struct platform_driver airoha_cpu_pmdomain_driver = {
	.probe = airoha_cpu_pmdomain_probe,
	.remove = airoha_cpu_pmdomain_remove,
	.driver = {
		.name = "airoha-cpu-pmdomain",
		.of_match_table = airoha_cpu_pmdomain_of_match,
	},
};
module_platform_driver(airoha_cpu_pmdomain_driver);

MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("CPU PM domain driver for Airoha SoCs");
MODULE_LICENSE("GPL");
