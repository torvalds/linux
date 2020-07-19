// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

#include "clk-opp.h"
#include "clk-regmap.h"

static int derive_device_list(struct device **device_list,
				struct clk_hw *hw,
				struct device_node *np,
				char *clk_handle_name, int count)
{
	struct platform_device *pdev;
	struct device_node *dev_node;
	int j;

	for (j = 0; j < count; j++) {
		device_list[j] = NULL;
		dev_node = of_parse_phandle(np, clk_handle_name, j);
		if (!dev_node) {
			pr_err("Unable to get device_node pointer for %s opp-handle (%s)\n",
					clk_hw_get_name(hw), clk_handle_name);
			return -ENODEV;
		}

		pdev = of_find_device_by_node(dev_node);
		if (!pdev) {
			pr_err("Unable to find platform_device node for %s opp-handle\n",
						clk_hw_get_name(hw));
			return -ENODEV;
		}
		device_list[j] = &pdev->dev;
	}
	return 0;
}

static int clk_get_voltage(struct clk_hw *hw, unsigned long rate, int n)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);
	struct clk_vdd_class_data *vdd_data = &rclk->vdd_data;
	struct clk_vdd_class *vdd;
	int level, corner;

	/* Use the first regulator in the vdd class for the OPP table. */
	vdd = rclk->vdd_data.vdd_class;
	if (vdd->num_regulators > 1) {
		corner = vdd->vdd_uv[vdd->num_regulators * n];
	} else {
		level = clk_find_vdd_level(hw, vdd_data, rate);
		if (level < 0) {
			pr_err("Could not find vdd level\n");
			return -EINVAL;
		}
		corner = vdd->vdd_uv[level];
	}

	if (!corner) {
		pr_err("%s: Unable to find vdd level for rate %lu\n",
					clk_hw_get_name(hw), rate);
		return -EINVAL;
	}

	return corner;
}

static int clk_add_and_print_opp(struct clk_hw *hw,
				struct device **device_list, int count,
				unsigned long rate, int uv, int n)
{
	int j, ret;

	for (j = 0; j < count; j++) {
		ret = dev_pm_opp_add(device_list[j], rate, uv);
		if (ret) {
			pr_err("%s: couldn't add OPP for %lu - err: %d\n",
						clk_hw_get_name(hw), rate, ret);
			return ret;
		}

		pr_info("%s: set OPP pair(%lu Hz: %u uV) on %s\n",
						clk_hw_get_name(hw), rate, uv,
						dev_name(device_list[j]));
	}
	return 0;
}

void clk_hw_populate_clock_opp_table(struct device_node *np, struct clk_hw *hw)
{
	struct device **device_list;
	struct clk_regmap *rclk = to_clk_regmap(hw);
	struct clk_vdd_class_data *vdd_data;
	char clk_handle_name[MAX_LEN_OPP_HANDLE];
	int n, len, count, uv, ret;
	unsigned long rate = 0, rrate;

	if (!rclk->vdd_data.vdd_class || !rclk->vdd_data.num_rate_max)
		return;

	if (strlen(clk_hw_get_name(hw)) + LEN_OPP_HANDLE < MAX_LEN_OPP_HANDLE) {
		ret = scnprintf(clk_handle_name, ARRAY_SIZE(clk_handle_name),
				"qcom,%s-opp-handle", clk_hw_get_name(hw));
		if (ret < strlen(clk_hw_get_name(hw)) + LEN_OPP_HANDLE) {
			pr_err("%s: Failed to hold clk_handle_name\n",
							clk_hw_get_name(hw));
			return;
		}
	} else {
		pr_err("clk name (%s) too large to fit in clk_handle_name\n",
							clk_hw_get_name(hw));
		return;
	}

	if (of_find_property(np, clk_handle_name, &len)) {
		count = len/sizeof(u32);

		device_list = kmalloc_array(count, sizeof(struct device *),
							GFP_KERNEL);
		if (!device_list)
			return;

		ret = derive_device_list(device_list, hw, np,
					clk_handle_name, count);
		if (ret < 0) {
			pr_err("Failed to fill device_list for %s\n",
						clk_handle_name);
			goto err_derive_device_list;
		}
	} else {
		pr_debug("Unable to find %s\n", clk_handle_name);
		return;
	}

	vdd_data = &rclk->vdd_data;

	for (n = 0; n < vdd_data->num_rate_max; n++) {
		rrate = clk_hw_round_rate(hw, rate + 1);
		if (!rrate) {
			/*
			 * If the parent is not ready before this clock,
			 * most likely the round rate would fail.
			 */
			pr_err("clk_hw_round_rate failed for %s\n",
					clk_hw_get_name(hw));
			goto err_derive_device_list;
		}

		pr_debug("Rate %lu , uv %d, rrate %lu\n", rate + 1, uv,
				clk_hw_round_rate(hw, rate + 1));

		/*
		 * If clk_hw_round_rate gives the same value on consecutive
		 * iterations, exit the loop since we're at the maximum clock
		 * frequency.
		 */
		if (rate == rrate)
			break;
		rate = rrate;

		uv = clk_get_voltage(hw, rate, n);
		if (uv < 0)
			goto err_derive_device_list;

		ret = clk_add_and_print_opp(hw, device_list, count,
							rate, uv, n);
		if (ret)
			pr_err("Failed to add OPP table\n");
	}

err_derive_device_list:
	kfree(device_list);
}
EXPORT_SYMBOL(clk_hw_populate_clock_opp_table);
