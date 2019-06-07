// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm MSM vibrator driver
 *
 * Copyright (c) 2018 Brian Masney <masneyb@onstation.org>
 *
 * Based on qcom,pwm-vibrator.c from:
 * Copyright (c) 2018 Jonathan Marek <jonathan@marek.ca>
 *
 * Based on msm_pwm_vibrator.c from downstream Android sources:
 * Copyright (C) 2009-2014 LGE, Inc.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define REG_CMD_RCGR           0x00
#define REG_CFG_RCGR           0x04
#define REG_M                  0x08
#define REG_N                  0x0C
#define REG_D                  0x10
#define REG_CBCR               0x24
#define MMSS_CC_M_DEFAULT      1

struct msm_vibrator {
	struct input_dev *input;
	struct mutex mutex;
	struct work_struct worker;
	void __iomem *base;
	struct regulator *vcc;
	struct clk *clk;
	struct gpio_desc *enable_gpio;
	u16 magnitude;
	bool enabled;
};

static void msm_vibrator_write(struct msm_vibrator *vibrator, int offset,
			       u32 value)
{
	writel(value, vibrator->base + offset);
}

static int msm_vibrator_start(struct msm_vibrator *vibrator)
{
	int d_reg_val, ret = 0;

	mutex_lock(&vibrator->mutex);

	if (!vibrator->enabled) {
		ret = clk_set_rate(vibrator->clk, 24000);
		if (ret) {
			dev_err(&vibrator->input->dev,
				"Failed to set clock rate: %d\n", ret);
			goto unlock;
		}

		ret = clk_prepare_enable(vibrator->clk);
		if (ret) {
			dev_err(&vibrator->input->dev,
				"Failed to enable clock: %d\n", ret);
			goto unlock;
		}

		ret = regulator_enable(vibrator->vcc);
		if (ret) {
			dev_err(&vibrator->input->dev,
				"Failed to enable regulator: %d\n", ret);
			clk_disable(vibrator->clk);
			goto unlock;
		}

		gpiod_set_value_cansleep(vibrator->enable_gpio, 1);

		vibrator->enabled = true;
	}

	d_reg_val = 127 - ((126 * vibrator->magnitude) / 0xffff);
	msm_vibrator_write(vibrator, REG_CFG_RCGR,
			   (2 << 12) | /* dual edge mode */
			   (0 << 8) |  /* cxo */
			   (7 << 0));
	msm_vibrator_write(vibrator, REG_M, 1);
	msm_vibrator_write(vibrator, REG_N, 128);
	msm_vibrator_write(vibrator, REG_D, d_reg_val);
	msm_vibrator_write(vibrator, REG_CMD_RCGR, 1);
	msm_vibrator_write(vibrator, REG_CBCR, 1);

unlock:
	mutex_unlock(&vibrator->mutex);

	return ret;
}

static void msm_vibrator_stop(struct msm_vibrator *vibrator)
{
	mutex_lock(&vibrator->mutex);

	if (vibrator->enabled) {
		gpiod_set_value_cansleep(vibrator->enable_gpio, 0);
		regulator_disable(vibrator->vcc);
		clk_disable(vibrator->clk);
		vibrator->enabled = false;
	}

	mutex_unlock(&vibrator->mutex);
}

static void msm_vibrator_worker(struct work_struct *work)
{
	struct msm_vibrator *vibrator = container_of(work,
						     struct msm_vibrator,
						     worker);

	if (vibrator->magnitude)
		msm_vibrator_start(vibrator);
	else
		msm_vibrator_stop(vibrator);
}

static int msm_vibrator_play_effect(struct input_dev *dev, void *data,
				    struct ff_effect *effect)
{
	struct msm_vibrator *vibrator = input_get_drvdata(dev);

	mutex_lock(&vibrator->mutex);

	if (effect->u.rumble.strong_magnitude > 0)
		vibrator->magnitude = effect->u.rumble.strong_magnitude;
	else
		vibrator->magnitude = effect->u.rumble.weak_magnitude;

	mutex_unlock(&vibrator->mutex);

	schedule_work(&vibrator->worker);

	return 0;
}

static void msm_vibrator_close(struct input_dev *input)
{
	struct msm_vibrator *vibrator = input_get_drvdata(input);

	cancel_work_sync(&vibrator->worker);
	msm_vibrator_stop(vibrator);
}

static int msm_vibrator_probe(struct platform_device *pdev)
{
	struct msm_vibrator *vibrator;
	struct resource *res;
	int ret;

	vibrator = devm_kzalloc(&pdev->dev, sizeof(*vibrator), GFP_KERNEL);
	if (!vibrator)
		return -ENOMEM;

	vibrator->input = devm_input_allocate_device(&pdev->dev);
	if (!vibrator->input)
		return -ENOMEM;

	vibrator->vcc = devm_regulator_get(&pdev->dev, "vcc");
	if (IS_ERR(vibrator->vcc)) {
		if (PTR_ERR(vibrator->vcc) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get regulator: %ld\n",
				PTR_ERR(vibrator->vcc));
		return PTR_ERR(vibrator->vcc);
	}

	vibrator->enable_gpio = devm_gpiod_get(&pdev->dev, "enable",
					       GPIOD_OUT_LOW);
	if (IS_ERR(vibrator->enable_gpio)) {
		if (PTR_ERR(vibrator->enable_gpio) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get enable gpio: %ld\n",
				PTR_ERR(vibrator->enable_gpio));
		return PTR_ERR(vibrator->enable_gpio);
	}

	vibrator->clk = devm_clk_get(&pdev->dev, "pwm");
	if (IS_ERR(vibrator->clk)) {
		if (PTR_ERR(vibrator->clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to lookup pwm clock: %ld\n",
				PTR_ERR(vibrator->clk));
		return PTR_ERR(vibrator->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get platform resource\n");
		return -ENODEV;
	}

	vibrator->base = devm_ioremap(&pdev->dev, res->start,
				     resource_size(res));
	if (!vibrator->base) {
		dev_err(&pdev->dev, "Failed to iomap resource.\n");
		return -ENOMEM;
	}

	vibrator->enabled = false;
	mutex_init(&vibrator->mutex);
	INIT_WORK(&vibrator->worker, msm_vibrator_worker);

	vibrator->input->name = "msm-vibrator";
	vibrator->input->id.bustype = BUS_HOST;
	vibrator->input->close = msm_vibrator_close;

	input_set_drvdata(vibrator->input, vibrator);
	input_set_capability(vibrator->input, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(vibrator->input, NULL,
				      msm_vibrator_play_effect);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create ff memless: %d", ret);
		return ret;
	}

	ret = input_register_device(vibrator->input);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register input device: %d", ret);
		return ret;
	}

	platform_set_drvdata(pdev, vibrator);

	return 0;
}

static int __maybe_unused msm_vibrator_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_vibrator *vibrator = platform_get_drvdata(pdev);

	cancel_work_sync(&vibrator->worker);

	if (vibrator->enabled)
		msm_vibrator_stop(vibrator);

	return 0;
}

static int __maybe_unused msm_vibrator_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_vibrator *vibrator = platform_get_drvdata(pdev);

	if (vibrator->enabled)
		msm_vibrator_start(vibrator);

	return 0;
}

static SIMPLE_DEV_PM_OPS(msm_vibrator_pm_ops, msm_vibrator_suspend,
			 msm_vibrator_resume);

static const struct of_device_id msm_vibrator_of_match[] = {
	{ .compatible = "qcom,msm8226-vibrator" },
	{ .compatible = "qcom,msm8974-vibrator" },
	{},
};
MODULE_DEVICE_TABLE(of, msm_vibrator_of_match);

static struct platform_driver msm_vibrator_driver = {
	.probe	= msm_vibrator_probe,
	.driver	= {
		.name = "msm-vibrator",
		.pm = &msm_vibrator_pm_ops,
		.of_match_table = of_match_ptr(msm_vibrator_of_match),
	},
};
module_platform_driver(msm_vibrator_driver);

MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_DESCRIPTION("Qualcomm MSM vibrator driver");
MODULE_LICENSE("GPL");
