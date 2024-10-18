// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pwrseq/provider.h>
#include <linux/string.h>
#include <linux/types.h>

struct pwrseq_qcom_wcn_pdata {
	const char *const *vregs;
	size_t num_vregs;
	unsigned int pwup_delay_ms;
	unsigned int gpio_enable_delay_ms;
	const struct pwrseq_target_data **targets;
};

struct pwrseq_qcom_wcn_ctx {
	struct pwrseq_device *pwrseq;
	struct device_node *of_node;
	const struct pwrseq_qcom_wcn_pdata *pdata;
	struct regulator_bulk_data *regs;
	struct gpio_desc *bt_gpio;
	struct gpio_desc *wlan_gpio;
	struct gpio_desc *xo_clk_gpio;
	struct clk *clk;
	unsigned long last_gpio_enable_jf;
};

static void pwrseq_qcom_wcn_ensure_gpio_delay(struct pwrseq_qcom_wcn_ctx *ctx)
{
	unsigned long diff_jiffies;
	unsigned int diff_msecs;

	if (!ctx->pdata->gpio_enable_delay_ms)
		return;

	diff_jiffies = jiffies - ctx->last_gpio_enable_jf;
	diff_msecs = jiffies_to_msecs(diff_jiffies);

	if (diff_msecs < ctx->pdata->gpio_enable_delay_ms)
		msleep(ctx->pdata->gpio_enable_delay_ms - diff_msecs);
}

static int pwrseq_qcom_wcn_vregs_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_enable(ctx->pdata->num_vregs, ctx->regs);
}

static int pwrseq_qcom_wcn_vregs_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_disable(ctx->pdata->num_vregs, ctx->regs);
}

static const struct pwrseq_unit_data pwrseq_qcom_wcn_vregs_unit_data = {
	.name = "regulators-enable",
	.enable = pwrseq_qcom_wcn_vregs_enable,
	.disable = pwrseq_qcom_wcn_vregs_disable,
};

static int pwrseq_qcom_wcn_clk_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return clk_prepare_enable(ctx->clk);
}

static int pwrseq_qcom_wcn_clk_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	clk_disable_unprepare(ctx->clk);

	return 0;
}

static const struct pwrseq_unit_data pwrseq_qcom_wcn_clk_unit_data = {
	.name = "clock-enable",
	.enable = pwrseq_qcom_wcn_clk_enable,
	.disable = pwrseq_qcom_wcn_clk_disable,
};

static const struct pwrseq_unit_data *pwrseq_qcom_wcn_unit_deps[] = {
	&pwrseq_qcom_wcn_vregs_unit_data,
	&pwrseq_qcom_wcn_clk_unit_data,
	NULL
};

static int pwrseq_qcom_wcn6855_clk_assert(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	if (!ctx->xo_clk_gpio)
		return 0;

	msleep(1);

	gpiod_set_value_cansleep(ctx->xo_clk_gpio, 1);
	usleep_range(100, 200);

	return 0;
}

static const struct pwrseq_unit_data pwrseq_qcom_wcn6855_xo_clk_assert = {
	.name = "xo-clk-assert",
	.enable = pwrseq_qcom_wcn6855_clk_assert,
};

static const struct pwrseq_unit_data *pwrseq_qcom_wcn6855_unit_deps[] = {
	&pwrseq_qcom_wcn_vregs_unit_data,
	&pwrseq_qcom_wcn_clk_unit_data,
	&pwrseq_qcom_wcn6855_xo_clk_assert,
	NULL
};

static int pwrseq_qcom_wcn_bt_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	pwrseq_qcom_wcn_ensure_gpio_delay(ctx);
	gpiod_set_value_cansleep(ctx->bt_gpio, 1);
	ctx->last_gpio_enable_jf = jiffies;

	return 0;
}

static int pwrseq_qcom_wcn_bt_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	gpiod_set_value_cansleep(ctx->bt_gpio, 0);

	return 0;
}

static const struct pwrseq_unit_data pwrseq_qcom_wcn_bt_unit_data = {
	.name = "bluetooth-enable",
	.deps = pwrseq_qcom_wcn_unit_deps,
	.enable = pwrseq_qcom_wcn_bt_enable,
	.disable = pwrseq_qcom_wcn_bt_disable,
};

static const struct pwrseq_unit_data pwrseq_qcom_wcn6855_bt_unit_data = {
	.name = "wlan-enable",
	.deps = pwrseq_qcom_wcn6855_unit_deps,
	.enable = pwrseq_qcom_wcn_bt_enable,
	.disable = pwrseq_qcom_wcn_bt_disable,
};

static int pwrseq_qcom_wcn_wlan_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	pwrseq_qcom_wcn_ensure_gpio_delay(ctx);
	gpiod_set_value_cansleep(ctx->wlan_gpio, 1);
	ctx->last_gpio_enable_jf = jiffies;

	return 0;
}

static int pwrseq_qcom_wcn_wlan_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	gpiod_set_value_cansleep(ctx->wlan_gpio, 0);

	return 0;
}

static const struct pwrseq_unit_data pwrseq_qcom_wcn_wlan_unit_data = {
	.name = "wlan-enable",
	.deps = pwrseq_qcom_wcn_unit_deps,
	.enable = pwrseq_qcom_wcn_wlan_enable,
	.disable = pwrseq_qcom_wcn_wlan_disable,
};

static const struct pwrseq_unit_data pwrseq_qcom_wcn6855_wlan_unit_data = {
	.name = "wlan-enable",
	.deps = pwrseq_qcom_wcn6855_unit_deps,
	.enable = pwrseq_qcom_wcn_wlan_enable,
	.disable = pwrseq_qcom_wcn_wlan_disable,
};

static int pwrseq_qcom_wcn_pwup_delay(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	if (ctx->pdata->pwup_delay_ms)
		msleep(ctx->pdata->pwup_delay_ms);

	return 0;
}

static int pwrseq_qcom_wcn6855_xo_clk_deassert(struct pwrseq_device *pwrseq)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	if (ctx->xo_clk_gpio) {
		usleep_range(2000, 5000);
		gpiod_set_value_cansleep(ctx->xo_clk_gpio, 0);
	}

	return pwrseq_qcom_wcn_pwup_delay(pwrseq);
}

static const struct pwrseq_target_data pwrseq_qcom_wcn_bt_target_data = {
	.name = "bluetooth",
	.unit = &pwrseq_qcom_wcn_bt_unit_data,
	.post_enable = pwrseq_qcom_wcn_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_qcom_wcn_wlan_target_data = {
	.name = "wlan",
	.unit = &pwrseq_qcom_wcn_wlan_unit_data,
	.post_enable = pwrseq_qcom_wcn_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_qcom_wcn6855_bt_target_data = {
	.name = "bluetooth",
	.unit = &pwrseq_qcom_wcn6855_bt_unit_data,
	.post_enable = pwrseq_qcom_wcn6855_xo_clk_deassert,
};

static const struct pwrseq_target_data pwrseq_qcom_wcn6855_wlan_target_data = {
	.name = "wlan",
	.unit = &pwrseq_qcom_wcn6855_wlan_unit_data,
	.post_enable = pwrseq_qcom_wcn6855_xo_clk_deassert,
};

static const struct pwrseq_target_data *pwrseq_qcom_wcn_targets[] = {
	&pwrseq_qcom_wcn_bt_target_data,
	&pwrseq_qcom_wcn_wlan_target_data,
	NULL
};

static const struct pwrseq_target_data *pwrseq_qcom_wcn6855_targets[] = {
	&pwrseq_qcom_wcn6855_bt_target_data,
	&pwrseq_qcom_wcn6855_wlan_target_data,
	NULL
};

static const char *const pwrseq_qca6390_vregs[] = {
	"vddio",
	"vddaon",
	"vddpmu",
	"vddrfa0p95",
	"vddrfa1p3",
	"vddrfa1p9",
	"vddpcie1p3",
	"vddpcie1p9",
};

static const struct pwrseq_qcom_wcn_pdata pwrseq_qca6390_of_data = {
	.vregs = pwrseq_qca6390_vregs,
	.num_vregs = ARRAY_SIZE(pwrseq_qca6390_vregs),
	.pwup_delay_ms = 60,
	.gpio_enable_delay_ms = 100,
	.targets = pwrseq_qcom_wcn_targets,
};

static const char *const pwrseq_wcn6855_vregs[] = {
	"vddio",
	"vddaon",
	"vddpmu",
	"vddpmumx",
	"vddpmucx",
	"vddrfa0p95",
	"vddrfa1p3",
	"vddrfa1p9",
	"vddpcie1p3",
	"vddpcie1p9",
};

static const struct pwrseq_qcom_wcn_pdata pwrseq_wcn6855_of_data = {
	.vregs = pwrseq_wcn6855_vregs,
	.num_vregs = ARRAY_SIZE(pwrseq_wcn6855_vregs),
	.pwup_delay_ms = 50,
	.gpio_enable_delay_ms = 5,
	.targets = pwrseq_qcom_wcn6855_targets,
};

static const char *const pwrseq_wcn7850_vregs[] = {
	"vdd",
	"vddio",
	"vddio1p2",
	"vddaon",
	"vdddig",
	"vddrfa1p2",
	"vddrfa1p8",
};

static const struct pwrseq_qcom_wcn_pdata pwrseq_wcn7850_of_data = {
	.vregs = pwrseq_wcn7850_vregs,
	.num_vregs = ARRAY_SIZE(pwrseq_wcn7850_vregs),
	.pwup_delay_ms = 50,
	.targets = pwrseq_qcom_wcn_targets,
};

static int pwrseq_qcom_wcn_match(struct pwrseq_device *pwrseq,
				 struct device *dev)
{
	struct pwrseq_qcom_wcn_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	struct device_node *dev_node = dev->of_node;

	/*
	 * The PMU supplies power to the Bluetooth and WLAN modules. both
	 * consume the PMU AON output so check the presence of the
	 * 'vddaon-supply' property and whether it leads us to the right
	 * device.
	 */
	if (!of_property_present(dev_node, "vddaon-supply"))
		return 0;

	struct device_node *reg_node __free(device_node) =
			of_parse_phandle(dev_node, "vddaon-supply", 0);
	if (!reg_node)
		return 0;

	/*
	 * `reg_node` is the PMU AON regulator, its parent is the `regulators`
	 * node and finally its grandparent is the PMU device node that we're
	 * looking for.
	 */
	if (!reg_node->parent || !reg_node->parent->parent ||
	    reg_node->parent->parent != ctx->of_node)
		return 0;

	return 1;
}

static int pwrseq_qcom_wcn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwrseq_qcom_wcn_ctx *ctx;
	struct pwrseq_config config;
	int i, ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->of_node = dev->of_node;

	ctx->pdata = of_device_get_match_data(dev);
	if (!ctx->pdata)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to obtain platform data\n");

	ctx->regs = devm_kcalloc(dev, ctx->pdata->num_vregs,
				 sizeof(*ctx->regs), GFP_KERNEL);
	if (!ctx->regs)
		return -ENOMEM;

	for (i = 0; i < ctx->pdata->num_vregs; i++)
		ctx->regs[i].supply = ctx->pdata->vregs[i];

	ret = devm_regulator_bulk_get(dev, ctx->pdata->num_vregs, ctx->regs);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get all regulators\n");

	ctx->bt_gpio = devm_gpiod_get_optional(dev, "bt-enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->bt_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->bt_gpio),
				     "Failed to get the Bluetooth enable GPIO\n");

	ctx->wlan_gpio = devm_gpiod_get_optional(dev, "wlan-enable",
						 GPIOD_ASIS);
	if (IS_ERR(ctx->wlan_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->wlan_gpio),
				     "Failed to get the WLAN enable GPIO\n");

	ctx->xo_clk_gpio = devm_gpiod_get_optional(dev, "xo-clk",
						   GPIOD_OUT_LOW);
	if (IS_ERR(ctx->xo_clk_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->xo_clk_gpio),
				     "Failed to get the XO_CLK GPIO\n");

	/*
	 * Set direction to output but keep the current value in order to not
	 * disable the WLAN module accidentally if it's already powered on.
	 */
	gpiod_direction_output(ctx->wlan_gpio,
			       gpiod_get_value_cansleep(ctx->wlan_gpio));

	ctx->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(ctx->clk))
		return dev_err_probe(dev, PTR_ERR(ctx->clk),
				     "Failed to get the reference clock\n");

	memset(&config, 0, sizeof(config));

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = ctx;
	config.match = pwrseq_qcom_wcn_match;
	config.targets = ctx->pdata->targets;

	ctx->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(ctx->pwrseq))
		return dev_err_probe(dev, PTR_ERR(ctx->pwrseq),
				     "Failed to register the power sequencer\n");

	return 0;
}

static const struct of_device_id pwrseq_qcom_wcn_of_match[] = {
	{
		.compatible = "qcom,qca6390-pmu",
		.data = &pwrseq_qca6390_of_data,
	},
	{
		.compatible = "qcom,wcn6855-pmu",
		.data = &pwrseq_wcn6855_of_data,
	},
	{
		.compatible = "qcom,wcn7850-pmu",
		.data = &pwrseq_wcn7850_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pwrseq_qcom_wcn_of_match);

static struct platform_driver pwrseq_qcom_wcn_driver = {
	.driver = {
		.name = "pwrseq-qcom_wcn",
		.of_match_table = pwrseq_qcom_wcn_of_match,
	},
	.probe = pwrseq_qcom_wcn_probe,
};
module_platform_driver(pwrseq_qcom_wcn_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Qualcomm WCN PMU power sequencing driver");
MODULE_LICENSE("GPL");
