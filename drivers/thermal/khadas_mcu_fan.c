// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Khadas MCU Controlled FAN driver
 *
 * Copyright (C) 2020 BayLibre SAS
 * Author(s): Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mfd/khadas-mcu.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/regulator/consumer.h>
#include <linux/minmax.h>

#define MAX_LEVEL 5

struct khadas_mcu_fan_ctx {
	struct khadas_mcu *mcu;
	unsigned int fan_reg;
	unsigned int level;
	const unsigned int *levels;
	unsigned int nlevels;
	struct thermal_cooling_device *cdev;
	struct regulator *power;
};

static int khadas_mcu_fan_set_level(struct khadas_mcu_fan_ctx *ctx,
				    unsigned int level)
{
	return regmap_write(ctx->mcu->regmap, ctx->fan_reg, level);
}

static int khadas_mcu_fan_get_max_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct khadas_mcu_fan_ctx *ctx = cdev->devdata;

	*state = min_t(unsigned int, MAX_LEVEL, ctx->nlevels - 1);

	return 0;
}

static int khadas_mcu_fan_get_cur_state(struct thermal_cooling_device *cdev,
					unsigned long *state)
{
	struct khadas_mcu_fan_ctx *ctx = cdev->devdata;

	*state = ctx->level;

	return 0;
}

static int
khadas_mcu_fan_set_cur_state(struct thermal_cooling_device *cdev,
			     unsigned long state)
{
	struct khadas_mcu_fan_ctx *ctx = cdev->devdata;
	int ret;

	if (state > MAX_LEVEL || state >= ctx->nlevels)
		return -EINVAL;

	if (state == ctx->level)
		return 0;

	ret = khadas_mcu_fan_set_level(ctx, ctx->levels[state]);
	if (ret)
		return ret;

	ctx->level = state;

	return 0;
}

static const struct thermal_cooling_device_ops khadas_mcu_fan_cooling_ops = {
	.get_max_state = khadas_mcu_fan_get_max_state,
	.get_cur_state = khadas_mcu_fan_get_cur_state,
	.set_cur_state = khadas_mcu_fan_set_cur_state,
};

static void khadas_mcu_fan_regulator_disable(void *data)
{
	struct regulator *power = data;

	regulator_disable(power);
}

static const unsigned int khadas_mcu_fan_levels[] = { 0, 1, 2, 3 };

static const unsigned int khadas_mcu_vim4_fan_levels[] = { 0, 30, 40, 55, 75, 100 };

static int khadas_mcu_fan_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct khadas_mcu *mcu = dev_get_drvdata(pdev->dev.parent);
	struct thermal_cooling_device *cdev;
	struct device *dev = &pdev->dev;
	struct khadas_mcu_fan_ctx *ctx;
	int ret;


	if (!dev->of_node)
		dev->of_node = of_node_get(dev->parent->of_node);

	if (!id)
		return -EINVAL;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->mcu = mcu;
	switch (id->driver_data) {
	case KHADAS_MCU_GENERIC:
		ctx->fan_reg = KHADAS_MCU_CMD_FAN_STATUS_CTRL_REG;
		ctx->levels = khadas_mcu_fan_levels;
		ctx->nlevels = ARRAY_SIZE(khadas_mcu_fan_levels);
		break;
	case KHADAS_MCU_VIM4:
		ctx->fan_reg = KHADAS_MCU_VIM4_FAN_CTRL_REG;
		ctx->levels = khadas_mcu_vim4_fan_levels;
		ctx->nlevels = ARRAY_SIZE(khadas_mcu_vim4_fan_levels);
		break;
	default:
		return -ENODEV;
	}

	ctx->power = devm_regulator_get(dev, "fan");
	if (IS_ERR(ctx->power))
		return PTR_ERR(ctx->power);

	ret = regulator_enable(ctx->power);
	if (ret) {
		dev_err(dev, "Failed to enable fan power supply: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, khadas_mcu_fan_regulator_disable, ctx->power);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, ctx);

	cdev = devm_thermal_of_child_cooling_device_register(dev,
							     dev->of_node,
							     "khadas-mcu-fan", ctx,
							     &khadas_mcu_fan_cooling_ops);
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		dev_err(dev, "Failed to register khadas-mcu-fan as cooling device: %d\n",
			ret);
		return ret;
	}
	ctx->cdev = cdev;

	return 0;
}

static void khadas_mcu_fan_shutdown(struct platform_device *pdev)
{
	struct khadas_mcu_fan_ctx *ctx = platform_get_drvdata(pdev);

	khadas_mcu_fan_set_level(ctx, ctx->levels[0]);
}

#ifdef CONFIG_PM_SLEEP
static int khadas_mcu_fan_suspend(struct device *dev)
{
	struct khadas_mcu_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	ret = khadas_mcu_fan_set_level(ctx, ctx->levels[0]);
	if (ret)
		return ret;

	ret = regulator_disable(ctx->power);
	if (ret) {
		khadas_mcu_fan_set_level(ctx, ctx->levels[ctx->level]);
		return ret;
	}

	return 0;
}

static int khadas_mcu_fan_resume(struct device *dev)
{
	struct khadas_mcu_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(ctx->power);
	if (ret)
		return ret;

	return khadas_mcu_fan_set_level(ctx, ctx->levels[ctx->level]);
}
#endif

static SIMPLE_DEV_PM_OPS(khadas_mcu_fan_pm, khadas_mcu_fan_suspend,
			 khadas_mcu_fan_resume);

static const struct platform_device_id khadas_mcu_fan_id_table[] = {
	{ .name = "khadas-mcu-fan-ctrl", .driver_data = KHADAS_MCU_GENERIC },
	{ .name = "khadas-mcu-vim4-fan", .driver_data = KHADAS_MCU_VIM4 },
	{},
};
MODULE_DEVICE_TABLE(platform, khadas_mcu_fan_id_table);

static struct platform_driver khadas_mcu_fan_driver = {
	.probe		= khadas_mcu_fan_probe,
	.shutdown	= khadas_mcu_fan_shutdown,
	.driver	= {
		.name		= "khadas-mcu-fan-ctrl",
		.pm		= &khadas_mcu_fan_pm,
	},
	.id_table	= khadas_mcu_fan_id_table,
};

module_platform_driver(khadas_mcu_fan_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Khadas MCU FAN driver");
MODULE_LICENSE("GPL");
