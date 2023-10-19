// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/arm-smccc.h>

#define IMX_SIP_DDR_DVFS			0xc2000004

/* Query available frequencies. */
#define IMX_SIP_DDR_DVFS_GET_FREQ_COUNT		0x10
#define IMX_SIP_DDR_DVFS_GET_FREQ_INFO		0x11

/*
 * This should be in a 1:1 mapping with devicetree OPPs but
 * firmware provides additional info.
 */
struct imx8m_ddrc_freq {
	unsigned long rate;
	unsigned long smcarg;
	int dram_core_parent_index;
	int dram_alt_parent_index;
	int dram_apb_parent_index;
};

/* Hardware limitation */
#define IMX8M_DDRC_MAX_FREQ_COUNT 4

/*
 * i.MX8M DRAM Controller clocks have the following structure (abridged):
 *
 * +----------+       |\            +------+
 * | dram_pll |-------|M| dram_core |      |
 * +----------+       |U|---------->| D    |
 *                 /--|X|           |  D   |
 *   dram_alt_root |  |/            |   R  |
 *                 |                |    C |
 *            +---------+           |      |
 *            |FIX DIV/4|           |      |
 *            +---------+           |      |
 *  composite:     |                |      |
 * +----------+    |                |      |
 * | dram_alt |----/                |      |
 * +----------+                     |      |
 * | dram_apb |-------------------->|      |
 * +----------+                     +------+
 *
 * The dram_pll is used for higher rates and dram_alt is used for lower rates.
 *
 * Frequency switching is implemented in TF-A (via SMC call) and can change the
 * configuration of the clocks, including mux parents. The dram_alt and
 * dram_apb clocks are "imx composite" and their parent can change too.
 *
 * We need to prepare/enable the new mux parents head of switching and update
 * their information afterwards.
 */
struct imx8m_ddrc {
	struct devfreq_dev_profile profile;
	struct devfreq *devfreq;

	/* For frequency switching: */
	struct clk *dram_core;
	struct clk *dram_pll;
	struct clk *dram_alt;
	struct clk *dram_apb;

	int freq_count;
	struct imx8m_ddrc_freq freq_table[IMX8M_DDRC_MAX_FREQ_COUNT];
};

static struct imx8m_ddrc_freq *imx8m_ddrc_find_freq(struct imx8m_ddrc *priv,
						    unsigned long rate)
{
	struct imx8m_ddrc_freq *freq;
	int i;

	/*
	 * Firmware reports values in MT/s, so we round-down from Hz
	 * Rounding is extra generous to ensure a match.
	 */
	rate = DIV_ROUND_CLOSEST(rate, 250000);
	for (i = 0; i < priv->freq_count; ++i) {
		freq = &priv->freq_table[i];
		if (freq->rate == rate ||
				freq->rate + 1 == rate ||
				freq->rate - 1 == rate)
			return freq;
	}

	return NULL;
}

static void imx8m_ddrc_smc_set_freq(int target_freq)
{
	struct arm_smccc_res res;
	u32 online_cpus = 0;
	int cpu;

	local_irq_disable();

	for_each_online_cpu(cpu)
		online_cpus |= (1 << (cpu * 8));

	/* change the ddr freqency */
	arm_smccc_smc(IMX_SIP_DDR_DVFS, target_freq, online_cpus,
			0, 0, 0, 0, 0, &res);

	local_irq_enable();
}

static struct clk *clk_get_parent_by_index(struct clk *clk, int index)
{
	struct clk_hw *hw;

	hw = clk_hw_get_parent_by_index(__clk_get_hw(clk), index);

	return hw ? hw->clk : NULL;
}

static int imx8m_ddrc_set_freq(struct device *dev, struct imx8m_ddrc_freq *freq)
{
	struct imx8m_ddrc *priv = dev_get_drvdata(dev);
	struct clk *new_dram_core_parent;
	struct clk *new_dram_alt_parent;
	struct clk *new_dram_apb_parent;
	int ret;

	/*
	 * Fetch new parents
	 *
	 * new_dram_alt_parent and new_dram_apb_parent are optional but
	 * new_dram_core_parent is not.
	 */
	new_dram_core_parent = clk_get_parent_by_index(
			priv->dram_core, freq->dram_core_parent_index - 1);
	if (!new_dram_core_parent) {
		dev_err(dev, "failed to fetch new dram_core parent\n");
		return -EINVAL;
	}
	if (freq->dram_alt_parent_index) {
		new_dram_alt_parent = clk_get_parent_by_index(
				priv->dram_alt,
				freq->dram_alt_parent_index - 1);
		if (!new_dram_alt_parent) {
			dev_err(dev, "failed to fetch new dram_alt parent\n");
			return -EINVAL;
		}
	} else
		new_dram_alt_parent = NULL;

	if (freq->dram_apb_parent_index) {
		new_dram_apb_parent = clk_get_parent_by_index(
				priv->dram_apb,
				freq->dram_apb_parent_index - 1);
		if (!new_dram_apb_parent) {
			dev_err(dev, "failed to fetch new dram_apb parent\n");
			return -EINVAL;
		}
	} else
		new_dram_apb_parent = NULL;

	/* increase reference counts and ensure clks are ON before switch */
	ret = clk_prepare_enable(new_dram_core_parent);
	if (ret) {
		dev_err(dev, "failed to enable new dram_core parent: %d\n",
			ret);
		goto out;
	}
	ret = clk_prepare_enable(new_dram_alt_parent);
	if (ret) {
		dev_err(dev, "failed to enable new dram_alt parent: %d\n",
			ret);
		goto out_disable_core_parent;
	}
	ret = clk_prepare_enable(new_dram_apb_parent);
	if (ret) {
		dev_err(dev, "failed to enable new dram_apb parent: %d\n",
			ret);
		goto out_disable_alt_parent;
	}

	imx8m_ddrc_smc_set_freq(freq->smcarg);

	/* update parents in clk tree after switch. */
	ret = clk_set_parent(priv->dram_core, new_dram_core_parent);
	if (ret)
		dev_warn(dev, "failed to set dram_core parent: %d\n", ret);
	if (new_dram_alt_parent) {
		ret = clk_set_parent(priv->dram_alt, new_dram_alt_parent);
		if (ret)
			dev_warn(dev, "failed to set dram_alt parent: %d\n",
				 ret);
	}
	if (new_dram_apb_parent) {
		ret = clk_set_parent(priv->dram_apb, new_dram_apb_parent);
		if (ret)
			dev_warn(dev, "failed to set dram_apb parent: %d\n",
				 ret);
	}

	/*
	 * Explicitly refresh dram PLL rate.
	 *
	 * Even if it's marked with CLK_GET_RATE_NOCACHE the rate will not be
	 * automatically refreshed when clk_get_rate is called on children.
	 */
	clk_get_rate(priv->dram_pll);

	/*
	 * clk_set_parent transfer the reference count from old parent.
	 * now we drop extra reference counts used during the switch
	 */
	clk_disable_unprepare(new_dram_apb_parent);
out_disable_alt_parent:
	clk_disable_unprepare(new_dram_alt_parent);
out_disable_core_parent:
	clk_disable_unprepare(new_dram_core_parent);
out:
	return ret;
}

static int imx8m_ddrc_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct imx8m_ddrc *priv = dev_get_drvdata(dev);
	struct imx8m_ddrc_freq *freq_info;
	struct dev_pm_opp *new_opp;
	unsigned long old_freq, new_freq;
	int ret;

	new_opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(new_opp)) {
		ret = PTR_ERR(new_opp);
		dev_err(dev, "failed to get recommended opp: %d\n", ret);
		return ret;
	}
	dev_pm_opp_put(new_opp);

	old_freq = clk_get_rate(priv->dram_core);
	if (*freq == old_freq)
		return 0;

	freq_info = imx8m_ddrc_find_freq(priv, *freq);
	if (!freq_info)
		return -EINVAL;

	/*
	 * Read back the clk rate to verify switch was correct and so that
	 * we can report it on all error paths.
	 */
	ret = imx8m_ddrc_set_freq(dev, freq_info);

	new_freq = clk_get_rate(priv->dram_core);
	if (ret)
		dev_err(dev, "ddrc failed freq switch to %lu from %lu: error %d. now at %lu\n",
			*freq, old_freq, ret, new_freq);
	else if (*freq != new_freq)
		dev_err(dev, "ddrc failed freq update to %lu from %lu, now at %lu\n",
			*freq, old_freq, new_freq);
	else
		dev_dbg(dev, "ddrc freq set to %lu (was %lu)\n",
			*freq, old_freq);

	return ret;
}

static int imx8m_ddrc_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct imx8m_ddrc *priv = dev_get_drvdata(dev);

	*freq = clk_get_rate(priv->dram_core);

	return 0;
}

static int imx8m_ddrc_init_freq_info(struct device *dev)
{
	struct imx8m_ddrc *priv = dev_get_drvdata(dev);
	struct arm_smccc_res res;
	int index;

	/* An error here means DDR DVFS API not supported by firmware */
	arm_smccc_smc(IMX_SIP_DDR_DVFS, IMX_SIP_DDR_DVFS_GET_FREQ_COUNT,
			0, 0, 0, 0, 0, 0, &res);
	priv->freq_count = res.a0;
	if (priv->freq_count <= 0 ||
			priv->freq_count > IMX8M_DDRC_MAX_FREQ_COUNT)
		return -ENODEV;

	for (index = 0; index < priv->freq_count; ++index) {
		struct imx8m_ddrc_freq *freq = &priv->freq_table[index];

		arm_smccc_smc(IMX_SIP_DDR_DVFS, IMX_SIP_DDR_DVFS_GET_FREQ_INFO,
			      index, 0, 0, 0, 0, 0, &res);
		/* Result should be strictly positive */
		if ((long)res.a0 <= 0)
			return -ENODEV;

		freq->rate = res.a0;
		freq->smcarg = index;
		freq->dram_core_parent_index = res.a1;
		freq->dram_alt_parent_index = res.a2;
		freq->dram_apb_parent_index = res.a3;

		/* dram_core has 2 options: dram_pll or dram_alt_root */
		if (freq->dram_core_parent_index != 1 &&
				freq->dram_core_parent_index != 2)
			return -ENODEV;
		/* dram_apb and dram_alt have exactly 8 possible parents */
		if (freq->dram_alt_parent_index > 8 ||
				freq->dram_apb_parent_index > 8)
			return -ENODEV;
		/* dram_core from alt requires explicit dram_alt parent */
		if (freq->dram_core_parent_index == 2 &&
				freq->dram_alt_parent_index == 0)
			return -ENODEV;
	}

	return 0;
}

static int imx8m_ddrc_check_opps(struct device *dev)
{
	struct imx8m_ddrc *priv = dev_get_drvdata(dev);
	struct imx8m_ddrc_freq *freq_info;
	struct dev_pm_opp *opp;
	unsigned long freq;
	int i, opp_count;

	/* Enumerate DT OPPs and disable those not supported by firmware */
	opp_count = dev_pm_opp_get_opp_count(dev);
	if (opp_count < 0)
		return opp_count;
	for (i = 0, freq = 0; i < opp_count; ++i, ++freq) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			dev_err(dev, "Failed enumerating OPPs: %ld\n",
				PTR_ERR(opp));
			return PTR_ERR(opp);
		}
		dev_pm_opp_put(opp);

		freq_info = imx8m_ddrc_find_freq(priv, freq);
		if (!freq_info) {
			dev_info(dev, "Disable unsupported OPP %luHz %luMT/s\n",
					freq, DIV_ROUND_CLOSEST(freq, 250000));
			dev_pm_opp_disable(dev, freq);
		}
	}

	return 0;
}

static void imx8m_ddrc_exit(struct device *dev)
{
	dev_pm_opp_of_remove_table(dev);
}

static int imx8m_ddrc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx8m_ddrc *priv;
	const char *gov = DEVFREQ_GOV_USERSPACE;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	ret = imx8m_ddrc_init_freq_info(dev);
	if (ret) {
		dev_err(dev, "failed to init firmware freq info: %d\n", ret);
		return ret;
	}

	priv->dram_core = devm_clk_get(dev, "core");
	if (IS_ERR(priv->dram_core)) {
		ret = PTR_ERR(priv->dram_core);
		dev_err(dev, "failed to fetch core clock: %d\n", ret);
		return ret;
	}
	priv->dram_pll = devm_clk_get(dev, "pll");
	if (IS_ERR(priv->dram_pll)) {
		ret = PTR_ERR(priv->dram_pll);
		dev_err(dev, "failed to fetch pll clock: %d\n", ret);
		return ret;
	}
	priv->dram_alt = devm_clk_get(dev, "alt");
	if (IS_ERR(priv->dram_alt)) {
		ret = PTR_ERR(priv->dram_alt);
		dev_err(dev, "failed to fetch alt clock: %d\n", ret);
		return ret;
	}
	priv->dram_apb = devm_clk_get(dev, "apb");
	if (IS_ERR(priv->dram_apb)) {
		ret = PTR_ERR(priv->dram_apb);
		dev_err(dev, "failed to fetch apb clock: %d\n", ret);
		return ret;
	}

	ret = dev_pm_opp_of_add_table(dev);
	if (ret < 0) {
		dev_err(dev, "failed to get OPP table\n");
		return ret;
	}

	ret = imx8m_ddrc_check_opps(dev);
	if (ret < 0)
		goto err;

	priv->profile.target = imx8m_ddrc_target;
	priv->profile.exit = imx8m_ddrc_exit;
	priv->profile.get_cur_freq = imx8m_ddrc_get_cur_freq;
	priv->profile.initial_freq = clk_get_rate(priv->dram_core);

	priv->devfreq = devm_devfreq_add_device(dev, &priv->profile,
						gov, NULL);
	if (IS_ERR(priv->devfreq)) {
		ret = PTR_ERR(priv->devfreq);
		dev_err(dev, "failed to add devfreq device: %d\n", ret);
		goto err;
	}

	return 0;

err:
	dev_pm_opp_of_remove_table(dev);
	return ret;
}

static const struct of_device_id imx8m_ddrc_of_match[] = {
	{ .compatible = "fsl,imx8m-ddrc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx8m_ddrc_of_match);

static struct platform_driver imx8m_ddrc_platdrv = {
	.probe		= imx8m_ddrc_probe,
	.driver = {
		.name	= "imx8m-ddrc-devfreq",
		.of_match_table = imx8m_ddrc_of_match,
	},
};
module_platform_driver(imx8m_ddrc_platdrv);

MODULE_DESCRIPTION("i.MX8M DDR Controller frequency driver");
MODULE_AUTHOR("Leonard Crestez <leonard.crestez@nxp.com>");
MODULE_LICENSE("GPL v2");
