// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Flash Storage Host controller Platform bus based glue driver
 * Copyright (C) 2011-2013 Samsung India Software Operations
 *
 * Authors:
 *	Santosh Yaraganavi <santosh.sy@samsung.com>
 *	Vinayak Holikatti <h.vinayak@samsung.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>

#include <ufs/ufshcd.h>
#include "ufshcd-pltfrm.h"
#include <ufs/unipro.h>

#define UFSHCD_DEFAULT_LANES_PER_DIRECTION		2

static int ufshcd_parse_clock_info(struct ufs_hba *hba)
{
	int ret = 0;
	int cnt;
	int i;
	struct device *dev = hba->dev;
	struct device_node *np = dev->of_node;
	const char *name;
	u32 *clkfreq = NULL;
	struct ufs_clk_info *clki;
	ssize_t sz = 0;

	if (!np)
		goto out;

	cnt = of_property_count_strings(np, "clock-names");
	if (!cnt || (cnt == -EINVAL)) {
		dev_info(dev, "%s: Unable to find clocks, assuming enabled\n",
				__func__);
	} else if (cnt < 0) {
		dev_err(dev, "%s: count clock strings failed, err %d\n",
				__func__, cnt);
		ret = cnt;
	}

	if (cnt <= 0)
		goto out;

	sz = of_property_count_u32_elems(np, "freq-table-hz");
	if (sz <= 0) {
		dev_info(dev, "freq-table-hz property not specified\n");
		goto out;
	}

	if (sz != 2 * cnt) {
		dev_err(dev, "%s len mismatch\n", "freq-table-hz");
		ret = -EINVAL;
		goto out;
	}

	clkfreq = devm_kcalloc(dev, sz, sizeof(*clkfreq),
			       GFP_KERNEL);
	if (!clkfreq) {
		ret = -ENOMEM;
		goto out;
	}

	ret = of_property_read_u32_array(np, "freq-table-hz",
			clkfreq, sz);
	if (ret && (ret != -EINVAL)) {
		dev_err(dev, "%s: error reading array %d\n",
				"freq-table-hz", ret);
		return ret;
	}

	for (i = 0; i < sz; i += 2) {
		ret = of_property_read_string_index(np,	"clock-names", i/2,
						    &name);
		if (ret)
			goto out;

		clki = devm_kzalloc(dev, sizeof(*clki), GFP_KERNEL);
		if (!clki) {
			ret = -ENOMEM;
			goto out;
		}

		clki->min_freq = clkfreq[i];
		clki->max_freq = clkfreq[i+1];
		clki->name = devm_kstrdup(dev, name, GFP_KERNEL);
		if (!clki->name) {
			ret = -ENOMEM;
			goto out;
		}

		if (!strcmp(name, "ref_clk"))
			clki->keep_link_active = true;
		dev_dbg(dev, "%s: min %u max %u name %s\n", "freq-table-hz",
				clki->min_freq, clki->max_freq, clki->name);
		list_add_tail(&clki->list, &hba->clk_list_head);
	}
out:
	return ret;
}

static bool phandle_exists(const struct device_node *np,
			   const char *phandle_name, int index)
{
	struct device_node *parse_np = of_parse_phandle(np, phandle_name, index);

	if (parse_np)
		of_node_put(parse_np);

	return parse_np != NULL;
}

#define MAX_PROP_SIZE 32
int ufshcd_populate_vreg(struct device *dev, const char *name,
			 struct ufs_vreg **out_vreg, bool skip_current)
{
	char prop_name[MAX_PROP_SIZE];
	struct ufs_vreg *vreg = NULL;
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "%s: non DT initialization\n", __func__);
		goto out;
	}

	snprintf(prop_name, MAX_PROP_SIZE, "%s-supply", name);
	if (!phandle_exists(np, prop_name, 0)) {
		dev_info(dev, "%s: Unable to find %s regulator, assuming enabled\n",
				__func__, prop_name);
		goto out;
	}

	vreg = devm_kzalloc(dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vreg->name = devm_kstrdup(dev, name, GFP_KERNEL);
	if (!vreg->name)
		return -ENOMEM;

	if (skip_current) {
		vreg->max_uA = 0;
		goto out;
	}

	snprintf(prop_name, MAX_PROP_SIZE, "%s-max-microamp", name);
	if (of_property_read_u32(np, prop_name, &vreg->max_uA)) {
		dev_info(dev, "%s: unable to find %s\n", __func__, prop_name);
		vreg->max_uA = 0;
	}
out:
	*out_vreg = vreg;
	return 0;
}
EXPORT_SYMBOL_GPL(ufshcd_populate_vreg);

/**
 * ufshcd_parse_regulator_info - get regulator info from device tree
 * @hba: per adapter instance
 *
 * Get regulator info from device tree for vcc, vccq, vccq2 power supplies.
 * If any of the supplies are not defined it is assumed that they are always-on
 * and hence return zero. If the property is defined but parsing is failed
 * then return corresponding error.
 *
 * Return: 0 upon success; < 0 upon failure.
 */
static int ufshcd_parse_regulator_info(struct ufs_hba *hba)
{
	int err;
	struct device *dev = hba->dev;
	struct ufs_vreg_info *info = &hba->vreg_info;

	err = ufshcd_populate_vreg(dev, "vdd-hba", &info->vdd_hba, true);
	if (err)
		goto out;

	err = ufshcd_populate_vreg(dev, "vcc", &info->vcc, false);
	if (err)
		goto out;

	err = ufshcd_populate_vreg(dev, "vccq", &info->vccq, false);
	if (err)
		goto out;

	err = ufshcd_populate_vreg(dev, "vccq2", &info->vccq2, false);
out:
	return err;
}

static void ufshcd_init_lanes_per_dir(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	int ret;

	ret = of_property_read_u32(dev->of_node, "lanes-per-direction",
		&hba->lanes_per_direction);
	if (ret) {
		dev_dbg(hba->dev,
			"%s: failed to read lanes-per-direction, ret=%d\n",
			__func__, ret);
		hba->lanes_per_direction = UFSHCD_DEFAULT_LANES_PER_DIRECTION;
	}
}

/**
 * ufshcd_parse_clock_min_max_freq  - Parse MIN and MAX clocks freq
 * @hba: per adapter instance
 *
 * This function parses MIN and MAX frequencies of all clocks required
 * by the host drivers.
 *
 * Returns 0 for success and non-zero for failure
 */
static int ufshcd_parse_clock_min_max_freq(struct ufs_hba *hba)
{
	struct list_head *head = &hba->clk_list_head;
	struct ufs_clk_info *clki;
	struct dev_pm_opp *opp;
	unsigned long freq;
	u8 idx = 0;

	list_for_each_entry(clki, head, list) {
		if (!clki->name)
			continue;

		clki->clk = devm_clk_get(hba->dev, clki->name);
		if (IS_ERR(clki->clk))
			continue;

		/* Find Max Freq */
		freq = ULONG_MAX;
		opp = dev_pm_opp_find_freq_floor_indexed(hba->dev, &freq, idx);
		if (IS_ERR(opp)) {
			dev_err(hba->dev, "Failed to find OPP for MAX frequency\n");
			return PTR_ERR(opp);
		}
		clki->max_freq = dev_pm_opp_get_freq_indexed(opp, idx);
		dev_pm_opp_put(opp);

		/* Find Min Freq */
		freq = 0;
		opp = dev_pm_opp_find_freq_ceil_indexed(hba->dev, &freq, idx);
		if (IS_ERR(opp)) {
			dev_err(hba->dev, "Failed to find OPP for MIN frequency\n");
			return PTR_ERR(opp);
		}
		clki->min_freq = dev_pm_opp_get_freq_indexed(opp, idx++);
		dev_pm_opp_put(opp);
	}

	return 0;
}

static int ufshcd_parse_operating_points(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct device_node *np = dev->of_node;
	struct dev_pm_opp_config config = {};
	struct ufs_clk_info *clki;
	const char **clk_names;
	int cnt, i, ret;

	if (!of_property_present(np, "operating-points-v2"))
		return 0;

	if (of_property_present(np, "freq-table-hz")) {
		dev_err(dev, "%s: operating-points and freq-table-hz are incompatible\n",
			 __func__);
		return -EINVAL;
	}

	cnt = of_property_count_strings(np, "clock-names");
	if (cnt <= 0) {
		dev_err(dev, "%s: Missing clock-names\n",  __func__);
		return -ENODEV;
	}

	/* OPP expects clk_names to be NULL terminated */
	clk_names = devm_kcalloc(dev, cnt + 1, sizeof(*clk_names), GFP_KERNEL);
	if (!clk_names)
		return -ENOMEM;

	/*
	 * We still need to get reference to all clocks as the UFS core uses
	 * them separately.
	 */
	for (i = 0; i < cnt; i++) {
		ret = of_property_read_string_index(np, "clock-names", i,
						    &clk_names[i]);
		if (ret)
			return ret;

		clki = devm_kzalloc(dev, sizeof(*clki), GFP_KERNEL);
		if (!clki)
			return -ENOMEM;

		clki->name = devm_kstrdup(dev, clk_names[i], GFP_KERNEL);
		if (!clki->name)
			return -ENOMEM;

		if (!strcmp(clk_names[i], "ref_clk"))
			clki->keep_link_active = true;

		list_add_tail(&clki->list, &hba->clk_list_head);
	}

	config.clk_names = clk_names,
	config.config_clks = ufshcd_opp_config_clks;

	ret = devm_pm_opp_set_config(dev, &config);
	if (ret)
		return ret;

	ret = devm_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "Failed to add OPP table: %d\n", ret);
		return ret;
	}

	ret = ufshcd_parse_clock_min_max_freq(hba);
	if (ret)
		return ret;

	hba->use_pm_opp = true;

	return 0;
}

/**
 * ufshcd_negotiate_pwr_params - find power mode settings that are supported by
 *				 both the controller and the device
 * @host_params: pointer to host parameters
 * @dev_max: pointer to device attributes
 * @agreed_pwr: returned agreed attributes
 *
 * Return: 0 on success, non-zero value on failure.
 */
int ufshcd_negotiate_pwr_params(const struct ufs_host_params *host_params,
				const struct ufs_pa_layer_attr *dev_max,
				struct ufs_pa_layer_attr *agreed_pwr)
{
	int min_host_gear;
	int min_dev_gear;
	bool is_dev_sup_hs = false;
	bool is_host_max_hs = false;

	if (dev_max->pwr_rx == FAST_MODE)
		is_dev_sup_hs = true;

	if (host_params->desired_working_mode == UFS_HS_MODE) {
		is_host_max_hs = true;
		min_host_gear = min_t(u32, host_params->hs_rx_gear,
					host_params->hs_tx_gear);
	} else {
		min_host_gear = min_t(u32, host_params->pwm_rx_gear,
					host_params->pwm_tx_gear);
	}

	/*
	 * device doesn't support HS but host_params->desired_working_mode is HS,
	 * thus device and host_params don't agree
	 */
	if (!is_dev_sup_hs && is_host_max_hs) {
		pr_info("%s: device doesn't support HS\n",
			__func__);
		return -ENOTSUPP;
	} else if (is_dev_sup_hs && is_host_max_hs) {
		/*
		 * since device supports HS, it supports FAST_MODE.
		 * since host_params->desired_working_mode is also HS
		 * then final decision (FAST/FASTAUTO) is done according
		 * to pltfrm_params as it is the restricting factor
		 */
		agreed_pwr->pwr_rx = host_params->rx_pwr_hs;
		agreed_pwr->pwr_tx = agreed_pwr->pwr_rx;
	} else {
		/*
		 * here host_params->desired_working_mode is PWM.
		 * it doesn't matter whether device supports HS or PWM,
		 * in both cases host_params->desired_working_mode will
		 * determine the mode
		 */
		agreed_pwr->pwr_rx = host_params->rx_pwr_pwm;
		agreed_pwr->pwr_tx = agreed_pwr->pwr_rx;
	}

	/*
	 * we would like tx to work in the minimum number of lanes
	 * between device capability and vendor preferences.
	 * the same decision will be made for rx
	 */
	agreed_pwr->lane_tx = min_t(u32, dev_max->lane_tx,
				    host_params->tx_lanes);
	agreed_pwr->lane_rx = min_t(u32, dev_max->lane_rx,
				    host_params->rx_lanes);

	/* device maximum gear is the minimum between device rx and tx gears */
	min_dev_gear = min_t(u32, dev_max->gear_rx, dev_max->gear_tx);

	/*
	 * if both device capabilities and vendor pre-defined preferences are
	 * both HS or both PWM then set the minimum gear to be the chosen
	 * working gear.
	 * if one is PWM and one is HS then the one that is PWM get to decide
	 * what is the gear, as it is the one that also decided previously what
	 * pwr the device will be configured to.
	 */
	if ((is_dev_sup_hs && is_host_max_hs) ||
	    (!is_dev_sup_hs && !is_host_max_hs)) {
		agreed_pwr->gear_rx =
			min_t(u32, min_dev_gear, min_host_gear);
	} else if (!is_dev_sup_hs) {
		agreed_pwr->gear_rx = min_dev_gear;
	} else {
		agreed_pwr->gear_rx = min_host_gear;
	}
	agreed_pwr->gear_tx = agreed_pwr->gear_rx;

	agreed_pwr->hs_rate = host_params->hs_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(ufshcd_negotiate_pwr_params);

void ufshcd_init_host_params(struct ufs_host_params *host_params)
{
	*host_params = (struct ufs_host_params){
		.tx_lanes = UFS_LANE_2,
		.rx_lanes = UFS_LANE_2,
		.hs_rx_gear = UFS_HS_G3,
		.hs_tx_gear = UFS_HS_G3,
		.pwm_rx_gear = UFS_PWM_G4,
		.pwm_tx_gear = UFS_PWM_G4,
		.rx_pwr_pwm = SLOW_MODE,
		.tx_pwr_pwm = SLOW_MODE,
		.rx_pwr_hs = FAST_MODE,
		.tx_pwr_hs = FAST_MODE,
		.hs_rate = PA_HS_MODE_B,
		.desired_working_mode = UFS_HS_MODE,
	};
}
EXPORT_SYMBOL_GPL(ufshcd_init_host_params);

/**
 * ufshcd_pltfrm_init - probe routine of the driver
 * @pdev: pointer to Platform device handle
 * @vops: pointer to variant ops
 *
 * Return: 0 on success, non-zero value on failure.
 */
int ufshcd_pltfrm_init(struct platform_device *pdev,
		       const struct ufs_hba_variant_ops *vops)
{
	struct ufs_hba *hba;
	void __iomem *mmio_base;
	int irq, err;
	struct device *dev = &pdev->dev;

	mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio_base)) {
		err = PTR_ERR(mmio_base);
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto out;
	}

	err = ufshcd_alloc_host(dev, &hba);
	if (err) {
		dev_err(dev, "Allocation failed\n");
		goto out;
	}

	hba->vops = vops;

	err = ufshcd_parse_clock_info(hba);
	if (err) {
		dev_err(dev, "%s: clock parse failed %d\n",
				__func__, err);
		goto dealloc_host;
	}
	err = ufshcd_parse_regulator_info(hba);
	if (err) {
		dev_err(dev, "%s: regulator init failed %d\n",
				__func__, err);
		goto dealloc_host;
	}

	ufshcd_init_lanes_per_dir(hba);

	err = ufshcd_parse_operating_points(hba);
	if (err) {
		dev_err(dev, "%s: OPP parse failed %d\n", __func__, err);
		goto dealloc_host;
	}

	err = ufshcd_init(hba, mmio_base, irq);
	if (err) {
		dev_err_probe(dev, err, "Initialization failed with error %d\n",
			      err);
		goto dealloc_host;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

dealloc_host:
	ufshcd_dealloc_host(hba);
out:
	return err;
}
EXPORT_SYMBOL_GPL(ufshcd_pltfrm_init);

MODULE_AUTHOR("Santosh Yaragnavi <santosh.sy@samsung.com>");
MODULE_AUTHOR("Vinayak Holikatti <h.vinayak@samsung.com>");
MODULE_DESCRIPTION("UFS host controller Platform bus based glue driver");
MODULE_LICENSE("GPL");
