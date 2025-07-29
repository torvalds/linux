// SPDX-License-Identifier: GPL-2.0
/*
 * T-HEAD TH1520 GPU Power Sequencer Driver
 *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 * Author: Michal Wilczynski <m.wilczynski@samsung.com>
 *
 * This driver implements the power sequence for the Imagination BXM-4-64
 * GPU on the T-HEAD TH1520 SoC. The sequence requires coordinating resources
 * from both the sequencer's parent device node (clkgen_reset) and the GPU's
 * device node (clocks and core reset).
 *
 * The `match` function is used to acquire the GPU's resources when the
 * GPU driver requests the "gpu-power" sequence target.
 */

#include <linux/auxiliary_bus.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pwrseq/provider.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <dt-bindings/power/thead,th1520-power.h>

struct pwrseq_thead_gpu_ctx {
	struct pwrseq_device *pwrseq;
	struct reset_control *clkgen_reset;
	struct device_node *aon_node;

	/* Consumer resources */
	struct device_node *consumer_node;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *gpu_reset;
};

static int pwrseq_thead_gpu_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_thead_gpu_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	int ret;

	if (!ctx->clks || !ctx->gpu_reset)
		return -ENODEV;

	ret = clk_bulk_prepare_enable(ctx->num_clks, ctx->clks);
	if (ret)
		return ret;

	ret = reset_control_deassert(ctx->clkgen_reset);
	if (ret)
		goto err_disable_clks;

	/*
	 * According to the hardware manual, a delay of at least 32 clock
	 * cycles is required between de-asserting the clkgen reset and
	 * de-asserting the GPU reset. Assuming a worst-case scenario with
	 * a very high GPU clock frequency, a delay of 1 microsecond is
	 * sufficient to ensure this requirement is met across all
	 * feasible GPU clock speeds.
	 */
	udelay(1);

	ret = reset_control_deassert(ctx->gpu_reset);
	if (ret)
		goto err_assert_clkgen;

	return 0;

err_assert_clkgen:
	reset_control_assert(ctx->clkgen_reset);
err_disable_clks:
	clk_bulk_disable_unprepare(ctx->num_clks, ctx->clks);
	return ret;
}

static int pwrseq_thead_gpu_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_thead_gpu_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	int ret = 0, err;

	if (!ctx->clks || !ctx->gpu_reset)
		return -ENODEV;

	err = reset_control_assert(ctx->gpu_reset);
	if (err)
		ret = err;

	err = reset_control_assert(ctx->clkgen_reset);
	if (err && !ret)
		ret = err;

	clk_bulk_disable_unprepare(ctx->num_clks, ctx->clks);

	/* ret stores values of the first error code */
	return ret;
}

static const struct pwrseq_unit_data pwrseq_thead_gpu_unit = {
	.name = "gpu-power-sequence",
	.enable = pwrseq_thead_gpu_enable,
	.disable = pwrseq_thead_gpu_disable,
};

static const struct pwrseq_target_data pwrseq_thead_gpu_target = {
	.name = "gpu-power",
	.unit = &pwrseq_thead_gpu_unit,
};

static const struct pwrseq_target_data *pwrseq_thead_gpu_targets[] = {
	&pwrseq_thead_gpu_target,
	NULL
};

static int pwrseq_thead_gpu_match(struct pwrseq_device *pwrseq,
				  struct device *dev)
{
	struct pwrseq_thead_gpu_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	static const char *const clk_names[] = { "core", "sys" };
	struct of_phandle_args pwr_spec;
	int i, ret;

	/* We only match the specific T-HEAD TH1520 GPU compatible */
	if (!of_device_is_compatible(dev->of_node, "thead,th1520-gpu"))
		return PWRSEQ_NO_MATCH;

	ret = of_parse_phandle_with_args(dev->of_node, "power-domains",
					 "#power-domain-cells", 0, &pwr_spec);
	if (ret)
		return PWRSEQ_NO_MATCH;

	/* Additionally verify consumer device has AON as power-domain */
	if (pwr_spec.np != ctx->aon_node || pwr_spec.args[0] != TH1520_GPU_PD) {
		of_node_put(pwr_spec.np);
		return PWRSEQ_NO_MATCH;
	}

	of_node_put(pwr_spec.np);

	/* If a consumer is already bound, only allow a re-match from it */
	if (ctx->consumer_node)
		return ctx->consumer_node == dev->of_node ?
				PWRSEQ_MATCH_OK : PWRSEQ_NO_MATCH;

	ctx->num_clks = ARRAY_SIZE(clk_names);
	ctx->clks = kcalloc(ctx->num_clks, sizeof(*ctx->clks), GFP_KERNEL);
	if (!ctx->clks)
		return -ENOMEM;

	for (i = 0; i < ctx->num_clks; i++)
		ctx->clks[i].id = clk_names[i];

	ret = clk_bulk_get(dev, ctx->num_clks, ctx->clks);
	if (ret)
		goto err_free_clks;

	ctx->gpu_reset = reset_control_get_shared(dev, NULL);
	if (IS_ERR(ctx->gpu_reset)) {
		ret = PTR_ERR(ctx->gpu_reset);
		goto err_put_clks;
	}

	ctx->consumer_node = of_node_get(dev->of_node);

	return PWRSEQ_MATCH_OK;

err_put_clks:
	clk_bulk_put(ctx->num_clks, ctx->clks);
err_free_clks:
	kfree(ctx->clks);
	ctx->clks = NULL;

	return ret;
}

static int pwrseq_thead_gpu_probe(struct auxiliary_device *adev,
				  const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct device *parent_dev = dev->parent;
	struct pwrseq_thead_gpu_ctx *ctx;
	struct pwrseq_config config = {};

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->aon_node = parent_dev->of_node;

	ctx->clkgen_reset =
		devm_reset_control_get_exclusive(parent_dev, "gpu-clkgen");
	if (IS_ERR(ctx->clkgen_reset))
		return dev_err_probe(
			dev, PTR_ERR(ctx->clkgen_reset),
			"Failed to get GPU clkgen reset from parent\n");

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = ctx;
	config.match = pwrseq_thead_gpu_match;
	config.targets = pwrseq_thead_gpu_targets;

	ctx->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(ctx->pwrseq))
		return dev_err_probe(dev, PTR_ERR(ctx->pwrseq),
				     "Failed to register power sequencer\n");

	auxiliary_set_drvdata(adev, ctx);

	return 0;
}

static void pwrseq_thead_gpu_remove(struct auxiliary_device *adev)
{
	struct pwrseq_thead_gpu_ctx *ctx = auxiliary_get_drvdata(adev);

	if (ctx->gpu_reset)
		reset_control_put(ctx->gpu_reset);

	if (ctx->clks) {
		clk_bulk_put(ctx->num_clks, ctx->clks);
		kfree(ctx->clks);
	}

	if (ctx->consumer_node)
		of_node_put(ctx->consumer_node);
}

static const struct auxiliary_device_id pwrseq_thead_gpu_id_table[] = {
	{ .name = "th1520_pm_domains.pwrseq-gpu" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, pwrseq_thead_gpu_id_table);

static struct auxiliary_driver pwrseq_thead_gpu_driver = {
	.driver = {
		.name = "pwrseq-thead-gpu",
	},
	.probe = pwrseq_thead_gpu_probe,
	.remove = pwrseq_thead_gpu_remove,
	.id_table = pwrseq_thead_gpu_id_table,
};
module_auxiliary_driver(pwrseq_thead_gpu_driver);

MODULE_AUTHOR("Michal Wilczynski <m.wilczynski@samsung.com>");
MODULE_DESCRIPTION("T-HEAD TH1520 GPU power sequencer driver");
MODULE_LICENSE("GPL");
