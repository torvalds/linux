/*
 * Clock provider that uses the SCPI interface for clock setting.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/kmod.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/scpi.h>

struct scpi_osc {
	uint8_t id;
	struct clk_hw hw;
	unsigned long rate_min;
	unsigned long rate_max;
	struct scpi_dev *scpi;
};

#define to_scpi_osc(osc) container_of(osc, struct scpi_osc, hw)

static unsigned long scpi_osc_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct scpi_osc *osc = to_scpi_osc(hw);
	char buf[4];
	int err;

	memset(buf, 0, 4);
	buf[0] = osc->id;
	err = scpi_exec_command(SCPI_CMD_GET_CLOCK_FREQ, &buf, 2, &buf, 4);
	if (err)
		return 0;

	return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

static long scpi_osc_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *parent_rate)
{
	struct scpi_osc *osc = to_scpi_osc(hw);
	if (osc->rate_min && rate < osc->rate_min)
		rate = osc->rate_min;
	if (osc->rate_max && rate > osc->rate_max)
		rate = osc->rate_max;

	return rate;
}

static int scpi_osc_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct scpi_osc *osc = to_scpi_osc(hw);
	char buf[6];
	int err;

	memcpy(buf, (char*)&rate, 4);
	buf[4] = osc->id;
	buf[5] = 0;
	err = scpi_exec_command(SCPI_CMD_SET_CLOCK_FREQ, buf, 6, NULL, 0);
	if (err) {
		pr_info("Failed to set clock frequency: %d\n", err);
	}

	return err;
}

static struct clk_ops scpi_osc_ops = {
	.recalc_rate = scpi_osc_recalc_rate,
	.round_rate = scpi_osc_round_rate,
	.set_rate = scpi_osc_set_rate,
};

void __init scpi_osc_setup(struct device_node *node)
{
	struct clk_init_data clk_init;
	struct scpi_osc *osc;
	struct clk *clk;
	int num_parents, i, err;
	const char *parent_names[2];
	uint32_t range[2];
	struct platform_device *pdev;
	struct of_phandle_args clkspec;

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return;

	if (of_property_read_u32_array(node, "freq-range", range,
					ARRAY_SIZE(range)) == 0) {
		osc->rate_min = range[0];
		osc->rate_max = range[1];
	}

	of_property_read_string(node, "clock-output-names", &clk_init.name);
	if (!clk_init.name)
		clk_init.name = node->full_name;

	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 0 || num_parents > 2)
		goto cleanup;

	for (i = 0; i < num_parents; i++) {
		parent_names[i] = of_clk_get_parent_name(node, i);
		if (!parent_names[i])
			goto cleanup;
		else
			pr_info("Found parent clock %s\n", parent_names[i]);

		err = of_parse_phandle_with_args(node, "clocks",
					"#clock-cells",	i, &clkspec);
		if (!err && clkspec.args_count)
			osc->id = clkspec.args[0];
		else
			osc->id = -1;
	}

	request_module("scpi-mhu");

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		pr_info("Failed to find platform device\n");
	} else {
		pr_info("Found platform device %s\n", pdev->name);
	}

	clk_init.ops = &scpi_osc_ops;
	clk_init.flags = CLK_IS_BASIC;
	clk_init.num_parents = num_parents;
	clk_init.parent_names = parent_names;
	osc->hw.init = &clk_init;

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk)) {
		pr_err("Failed to register clock '%s'!\n", clk_init.name);
		goto cleanup;
	}

	of_clk_add_provider(node, of_clk_src_simple_get, clk);
	pr_info("Registered clock '%s'\n", clk_init.name);

	return;

cleanup:
	if (osc)
		kfree(osc);
}
CLK_OF_DECLARE(scpi_clk, "arm,scpi-osc", scpi_osc_setup);
