// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) STMicroelectronics 2025 - All Rights Reserved
 * Author(s): Patrice Chotard <patrice.chotard@foss.st.com> for STMicroelectronics.
 */

#include <linux/bitfield.h>
#include <linux/bus/stm32_firewall_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define OMM_CR			0
#define CR_MUXEN		BIT(0)
#define CR_MUXENMODE_MASK	GENMASK(1, 0)
#define CR_CSSEL_OVR_EN		BIT(4)
#define CR_CSSEL_OVR_MASK	GENMASK(6, 5)
#define CR_REQ2ACK_MASK		GENMASK(23, 16)

#define OMM_CHILD_NB		2
#define OMM_CLK_NB		3

struct stm32_omm {
	struct resource *mm_res;
	struct clk_bulk_data clk_bulk[OMM_CLK_NB];
	struct reset_control *child_reset[OMM_CHILD_NB];
	void __iomem *io_base;
	u32 cr;
	u8 nb_child;
	bool restore_omm;
};

static int stm32_omm_set_amcr(struct device *dev, bool set)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);
	resource_size_t mm_ospi2_size = 0;
	static const char * const mm_name[] = { "ospi1", "ospi2" };
	struct regmap *syscfg_regmap;
	struct device_node *node;
	struct resource res, res1;
	unsigned int syscon_args[2];
	int ret, idx;
	unsigned int i, amcr, read_amcr;

	for (i = 0; i < omm->nb_child; i++) {
		idx = of_property_match_string(dev->of_node,
					       "memory-region-names",
					       mm_name[i]);
		if (idx < 0)
			continue;

		/* res1 only used on second loop iteration */
		res1.start = res.start;
		res1.end = res.end;

		node = of_parse_phandle(dev->of_node, "memory-region", idx);
		if (!node)
			continue;

		ret = of_address_to_resource(node, 0, &res);
		if (ret) {
			of_node_put(node);
			dev_err(dev, "unable to resolve memory region\n");
			return ret;
		}

		/* check that memory region fits inside OMM memory map area */
		if (!resource_contains(omm->mm_res, &res)) {
			dev_err(dev, "%s doesn't fit inside OMM memory map area\n",
				mm_name[i]);
			dev_err(dev, "%pR doesn't fit inside %pR\n", &res, omm->mm_res);
			of_node_put(node);

			return -EFAULT;
		}

		if (i == 1) {
			mm_ospi2_size = resource_size(&res);

			/* check that OMM memory region 1 doesn't overlap memory region 2 */
			if (resource_overlaps(&res, &res1)) {
				dev_err(dev, "OMM memory-region %s overlaps memory region %s\n",
					mm_name[0], mm_name[1]);
				dev_err(dev, "%pR overlaps %pR\n", &res1, &res);
				of_node_put(node);

				return -EFAULT;
			}
		}
		of_node_put(node);
	}

	syscfg_regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node, "st,syscfg-amcr",
							     2, syscon_args);
	if (IS_ERR(syscfg_regmap))
		return dev_err_probe(dev, PTR_ERR(syscfg_regmap),
				     "Failed to get st,syscfg-amcr property\n");

	amcr = mm_ospi2_size / SZ_64M;

	if (set)
		regmap_update_bits(syscfg_regmap, syscon_args[0], syscon_args[1], amcr);

	/* read AMCR and check coherency with memory-map areas defined in DT */
	regmap_read(syscfg_regmap, syscon_args[0], &read_amcr);
	read_amcr = read_amcr >> (ffs(syscon_args[1]) - 1);

	if (amcr != read_amcr) {
		dev_err(dev, "AMCR value not coherent with DT memory-map areas\n");
		ret = -EINVAL;
	}

	return ret;
}

static int stm32_omm_toggle_child_clock(struct device *dev, bool enable)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < omm->nb_child; i++) {
		if (enable) {
			ret = clk_prepare_enable(omm->clk_bulk[i + 1].clk);
			if (ret) {
				dev_err(dev, "Can not enable clock\n");
				goto clk_error;
			}
		} else {
			clk_disable_unprepare(omm->clk_bulk[i + 1].clk);
		}
	}

	return 0;

clk_error:
	while (i--)
		clk_disable_unprepare(omm->clk_bulk[i + 1].clk);

	return ret;
}

static int stm32_omm_disable_child(struct device *dev)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);
	struct reset_control *reset;
	int ret;
	u8 i;

	ret = stm32_omm_toggle_child_clock(dev, true);
	if (ret)
		return ret;

	for (i = 0; i < omm->nb_child; i++) {
		/* reset OSPI to ensure CR_EN bit is set to 0 */
		reset = omm->child_reset[i];
		ret = reset_control_acquire(reset);
		if (ret) {
			stm32_omm_toggle_child_clock(dev, false);
			dev_err(dev, "Can not acquire reset %d\n", ret);
			return ret;
		}

		reset_control_assert(reset);
		udelay(2);
		reset_control_deassert(reset);

		reset_control_release(reset);
	}

	return stm32_omm_toggle_child_clock(dev, false);
}

static int stm32_omm_configure(struct device *dev)
{
	static const char * const clocks_name[] = {"omm", "ospi1", "ospi2"};
	struct stm32_omm *omm = dev_get_drvdata(dev);
	unsigned long clk_rate_max = 0;
	u32 mux = 0;
	u32 cssel_ovr = 0;
	u32 req2ack = 0;
	struct reset_control *rstc;
	unsigned long clk_rate;
	int ret;
	u8 i;

	for (i = 0; i < OMM_CLK_NB; i++)
		omm->clk_bulk[i].id = clocks_name[i];

	/* retrieve OMM, OSPI1 and OSPI2 clocks */
	ret = devm_clk_bulk_get(dev, OMM_CLK_NB, omm->clk_bulk);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get OMM/OSPI's clocks\n");

	/* Ensure both OSPI instance are disabled before configuring OMM */
	ret = stm32_omm_disable_child(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	/* parse children's clock */
	for (i = 1; i <= omm->nb_child; i++) {
		clk_rate = clk_get_rate(omm->clk_bulk[i].clk);
		if (!clk_rate) {
			dev_err(dev, "Invalid clock rate\n");
			ret = -EINVAL;
			goto error;
		}

		if (clk_rate > clk_rate_max)
			clk_rate_max = clk_rate;
	}

	rstc = devm_reset_control_get_exclusive(dev, "omm");
	if (IS_ERR(rstc)) {
		ret = dev_err_probe(dev, PTR_ERR(rstc), "reset get failed\n");
		goto error;
	}

	reset_control_assert(rstc);
	udelay(2);
	reset_control_deassert(rstc);

	omm->cr = readl_relaxed(omm->io_base + OMM_CR);
	/* optional */
	ret = of_property_read_u32(dev->of_node, "st,omm-mux", &mux);
	if (!ret) {
		if (mux & CR_MUXEN) {
			ret = of_property_read_u32(dev->of_node, "st,omm-req2ack-ns",
						   &req2ack);
			if (!ret && req2ack) {
				req2ack = DIV_ROUND_UP(req2ack, NSEC_PER_SEC / clk_rate_max) - 1;

				if (req2ack > 256)
					req2ack = 256;
			}

			req2ack = FIELD_PREP(CR_REQ2ACK_MASK, req2ack);

			omm->cr &= ~CR_REQ2ACK_MASK;
			omm->cr |= FIELD_PREP(CR_REQ2ACK_MASK, req2ack);

			/*
			 * If the mux is enabled, the 2 OSPI clocks have to be
			 * always enabled
			 */
			ret = stm32_omm_toggle_child_clock(dev, true);
			if (ret)
				goto error;
		}

		omm->cr &= ~CR_MUXENMODE_MASK;
		omm->cr |= FIELD_PREP(CR_MUXENMODE_MASK, mux);
	}

	/* optional */
	ret = of_property_read_u32(dev->of_node, "st,omm-cssel-ovr", &cssel_ovr);
	if (!ret) {
		omm->cr &= ~CR_CSSEL_OVR_MASK;
		omm->cr |= FIELD_PREP(CR_CSSEL_OVR_MASK, cssel_ovr);
		omm->cr |= CR_CSSEL_OVR_EN;
	}

	omm->restore_omm = true;
	writel_relaxed(omm->cr, omm->io_base + OMM_CR);

	ret = stm32_omm_set_amcr(dev, true);

error:
	pm_runtime_put_sync_suspend(dev);

	return ret;
}

static int stm32_omm_check_access(struct device_node *np)
{
	struct stm32_firewall firewall;
	int ret;

	ret = stm32_firewall_get_firewall(np, &firewall, 1);
	if (ret)
		return ret;

	return stm32_firewall_grant_access(&firewall);
}

static int stm32_omm_probe(struct platform_device *pdev)
{
	static const char * const resets_name[] = {"ospi1", "ospi2"};
	struct device *dev = &pdev->dev;
	u8 child_access_granted = 0;
	struct stm32_omm *omm;
	int i, ret;

	omm = devm_kzalloc(dev, sizeof(*omm), GFP_KERNEL);
	if (!omm)
		return -ENOMEM;

	omm->io_base = devm_platform_ioremap_resource_byname(pdev, "regs");
	if (IS_ERR(omm->io_base))
		return PTR_ERR(omm->io_base);

	omm->mm_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "memory_map");
	if (!omm->mm_res)
		return -ENODEV;

	/* check child's access */
	for_each_child_of_node_scoped(dev->of_node, child) {
		if (omm->nb_child >= OMM_CHILD_NB) {
			dev_err(dev, "Bad DT, found too much children\n");
			return -E2BIG;
		}

		ret = stm32_omm_check_access(child);
		if (ret < 0 && ret != -EACCES)
			return ret;

		if (!ret)
			child_access_granted++;

		omm->nb_child++;
	}

	if (omm->nb_child != OMM_CHILD_NB)
		return -EINVAL;

	platform_set_drvdata(pdev, omm);

	devm_pm_runtime_enable(dev);

	/* check if OMM's resource access is granted */
	ret = stm32_omm_check_access(dev->of_node);
	if (ret < 0 && ret != -EACCES)
		return ret;

	for (i = 0; i < omm->nb_child; i++) {
		omm->child_reset[i] = devm_reset_control_get_exclusive_released(dev,
										resets_name[i]);

		if (IS_ERR(omm->child_reset[i]))
			return dev_err_probe(dev, PTR_ERR(omm->child_reset[i]),
					     "Can't get %s reset\n", resets_name[i]);
	}

	if (!ret && child_access_granted == OMM_CHILD_NB) {
		ret = stm32_omm_configure(dev);
		if (ret)
			return ret;
	} else {
		dev_dbg(dev, "Octo Memory Manager resource's access not granted\n");
		/*
		 * AMCR can't be set, so check if current value is coherent
		 * with memory-map areas defined in DT
		 */
		ret = stm32_omm_set_amcr(dev, false);
		if (ret)
			return ret;
	}

	ret = devm_of_platform_populate(dev);
	if (ret) {
		if (omm->cr & CR_MUXEN)
			stm32_omm_toggle_child_clock(&pdev->dev, false);

		return dev_err_probe(dev, ret, "Failed to create Octo Memory Manager child\n");
	}

	return 0;
}

static void stm32_omm_remove(struct platform_device *pdev)
{
	struct stm32_omm *omm = platform_get_drvdata(pdev);

	if (omm->cr & CR_MUXEN)
		stm32_omm_toggle_child_clock(&pdev->dev, false);
}

static const struct of_device_id stm32_omm_of_match[] = {
	{ .compatible = "st,stm32mp25-omm", },
	{}
};
MODULE_DEVICE_TABLE(of, stm32_omm_of_match);

static int __maybe_unused stm32_omm_runtime_suspend(struct device *dev)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);

	clk_disable_unprepare(omm->clk_bulk[0].clk);

	return 0;
}

static int __maybe_unused stm32_omm_runtime_resume(struct device *dev)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);

	return clk_prepare_enable(omm->clk_bulk[0].clk);
}

static int __maybe_unused stm32_omm_suspend(struct device *dev)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);

	if (omm->restore_omm && omm->cr & CR_MUXEN)
		stm32_omm_toggle_child_clock(dev, false);

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused stm32_omm_resume(struct device *dev)
{
	struct stm32_omm *omm = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);

	if (!omm->restore_omm)
		return 0;

	/* Ensure both OSPI instance are disabled before configuring OMM */
	ret = stm32_omm_disable_child(dev);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	writel_relaxed(omm->cr, omm->io_base + OMM_CR);
	ret = stm32_omm_set_amcr(dev, true);
	pm_runtime_put_sync_suspend(dev);
	if (ret)
		return ret;

	if (omm->cr & CR_MUXEN)
		ret = stm32_omm_toggle_child_clock(dev, true);

	return ret;
}

static const struct dev_pm_ops stm32_omm_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_omm_runtime_suspend,
			   stm32_omm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(stm32_omm_suspend, stm32_omm_resume)
};

static struct platform_driver stm32_omm_driver = {
	.probe	= stm32_omm_probe,
	.remove = stm32_omm_remove,
	.driver	= {
		.name = "stm32-omm",
		.of_match_table = stm32_omm_of_match,
		.pm = &stm32_omm_pm_ops,
	},
};
module_platform_driver(stm32_omm_driver);

MODULE_DESCRIPTION("STMicroelectronics Octo Memory Manager driver");
MODULE_LICENSE("GPL");
