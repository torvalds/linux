/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define VIB_MAX_LEVEL_mV	(3100)
#define VIB_MIN_LEVEL_mV	(1200)
#define VIB_MAX_LEVELS		(VIB_MAX_LEVEL_mV - VIB_MIN_LEVEL_mV)

#define MAX_FF_SPEED		0xff

struct pm8xxx_regs {
	unsigned int enable_addr;
	unsigned int enable_mask;

	unsigned int drv_addr;
	unsigned int drv_mask;
	unsigned int drv_shift;
	unsigned int drv_en_manual_mask;
};

static const struct pm8xxx_regs pm8058_regs = {
	.drv_addr = 0x4A,
	.drv_mask = 0xf8,
	.drv_shift = 3,
	.drv_en_manual_mask = 0xfc,
};

static struct pm8xxx_regs pm8916_regs = {
	.enable_addr = 0xc046,
	.enable_mask = BIT(7),
	.drv_addr = 0xc041,
	.drv_mask = 0x1F,
	.drv_shift = 0,
	.drv_en_manual_mask = 0,
};

/**
 * struct pm8xxx_vib - structure to hold vibrator data
 * @vib_input_dev: input device supporting force feedback
 * @work: work structure to set the vibration parameters
 * @regmap: regmap for register read/write
 * @regs: registers' info
 * @speed: speed of vibration set from userland
 * @active: state of vibrator
 * @level: level of vibration to set in the chip
 * @reg_vib_drv: regs->drv_addr register value
 */
struct pm8xxx_vib {
	struct input_dev *vib_input_dev;
	struct work_struct work;
	struct regmap *regmap;
	const struct pm8xxx_regs *regs;
	int speed;
	int level;
	bool active;
	u8  reg_vib_drv;
};

/**
 * pm8xxx_vib_set - handler to start/stop vibration
 * @vib: pointer to vibrator structure
 * @on: state to set
 */
static int pm8xxx_vib_set(struct pm8xxx_vib *vib, bool on)
{
	int rc;
	unsigned int val = vib->reg_vib_drv;
	const struct pm8xxx_regs *regs = vib->regs;

	if (on)
		val |= (vib->level << regs->drv_shift) & regs->drv_mask;
	else
		val &= ~regs->drv_mask;

	rc = regmap_write(vib->regmap, regs->drv_addr, val);
	if (rc < 0)
		return rc;

	vib->reg_vib_drv = val;

	if (regs->enable_mask)
		rc = regmap_update_bits(vib->regmap, regs->enable_addr,
					on ? regs->enable_mask : 0, val);

	return rc;
}

/**
 * pm8xxx_work_handler - worker to set vibration level
 * @work: pointer to work_struct
 */
static void pm8xxx_work_handler(struct work_struct *work)
{
	struct pm8xxx_vib *vib = container_of(work, struct pm8xxx_vib, work);
	const struct pm8xxx_regs *regs = vib->regs;
	int rc;
	unsigned int val;

	rc = regmap_read(vib->regmap, regs->drv_addr, &val);
	if (rc < 0)
		return;

	/*
	 * pmic vibrator supports voltage ranges from 1.2 to 3.1V, so
	 * scale the level to fit into these ranges.
	 */
	if (vib->speed) {
		vib->active = true;
		vib->level = ((VIB_MAX_LEVELS * vib->speed) / MAX_FF_SPEED) +
						VIB_MIN_LEVEL_mV;
		vib->level /= 100;
	} else {
		vib->active = false;
		vib->level = VIB_MIN_LEVEL_mV / 100;
	}

	pm8xxx_vib_set(vib, vib->active);
}

/**
 * pm8xxx_vib_close - callback of input close callback
 * @dev: input device pointer
 *
 * Turns off the vibrator.
 */
static void pm8xxx_vib_close(struct input_dev *dev)
{
	struct pm8xxx_vib *vib = input_get_drvdata(dev);

	cancel_work_sync(&vib->work);
	if (vib->active)
		pm8xxx_vib_set(vib, false);
}

/**
 * pm8xxx_vib_play_effect - function to handle vib effects.
 * @dev: input device pointer
 * @data: data of effect
 * @effect: effect to play
 *
 * Currently this driver supports only rumble effects.
 */
static int pm8xxx_vib_play_effect(struct input_dev *dev, void *data,
				  struct ff_effect *effect)
{
	struct pm8xxx_vib *vib = input_get_drvdata(dev);

	vib->speed = effect->u.rumble.strong_magnitude >> 8;
	if (!vib->speed)
		vib->speed = effect->u.rumble.weak_magnitude >> 9;

	schedule_work(&vib->work);

	return 0;
}

static int pm8xxx_vib_probe(struct platform_device *pdev)
{
	struct pm8xxx_vib *vib;
	struct input_dev *input_dev;
	int error;
	unsigned int val;
	const struct pm8xxx_regs *regs;

	vib = devm_kzalloc(&pdev->dev, sizeof(*vib), GFP_KERNEL);
	if (!vib)
		return -ENOMEM;

	vib->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!vib->regmap)
		return -ENODEV;

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return -ENOMEM;

	INIT_WORK(&vib->work, pm8xxx_work_handler);
	vib->vib_input_dev = input_dev;

	regs = of_device_get_match_data(&pdev->dev);

	/* operate in manual mode */
	error = regmap_read(vib->regmap, regs->drv_addr, &val);
	if (error < 0)
		return error;

	val &= regs->drv_en_manual_mask;
	error = regmap_write(vib->regmap, regs->drv_addr, val);
	if (error < 0)
		return error;

	vib->regs = regs;
	vib->reg_vib_drv = val;

	input_dev->name = "pm8xxx_vib_ffmemless";
	input_dev->id.version = 1;
	input_dev->close = pm8xxx_vib_close;
	input_set_drvdata(input_dev, vib);
	input_set_capability(vib->vib_input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(input_dev, NULL,
					pm8xxx_vib_play_effect);
	if (error) {
		dev_err(&pdev->dev,
			"couldn't register vibrator as FF device\n");
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev, "couldn't register input device\n");
		return error;
	}

	platform_set_drvdata(pdev, vib);
	return 0;
}

static int __maybe_unused pm8xxx_vib_suspend(struct device *dev)
{
	struct pm8xxx_vib *vib = dev_get_drvdata(dev);

	/* Turn off the vibrator */
	pm8xxx_vib_set(vib, false);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pm8xxx_vib_pm_ops, pm8xxx_vib_suspend, NULL);

static const struct of_device_id pm8xxx_vib_id_table[] = {
	{ .compatible = "qcom,pm8058-vib", .data = &pm8058_regs },
	{ .compatible = "qcom,pm8921-vib", .data = &pm8058_regs },
	{ .compatible = "qcom,pm8916-vib", .data = &pm8916_regs },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8xxx_vib_id_table);

static struct platform_driver pm8xxx_vib_driver = {
	.probe		= pm8xxx_vib_probe,
	.driver		= {
		.name	= "pm8xxx-vib",
		.pm	= &pm8xxx_vib_pm_ops,
		.of_match_table = pm8xxx_vib_id_table,
	},
};
module_platform_driver(pm8xxx_vib_driver);

MODULE_ALIAS("platform:pm8xxx_vib");
MODULE_DESCRIPTION("PMIC8xxx vibrator driver based on ff-memless framework");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Amy Maloche <amaloche@codeaurora.org>");
