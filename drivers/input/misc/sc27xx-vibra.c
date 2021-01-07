// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define CUR_DRV_CAL_SEL			GENMASK(13, 12)
#define SLP_LDOVIBR_PD_EN		BIT(9)
#define LDO_VIBR_PD			BIT(8)
#define SC2730_CUR_DRV_CAL_SEL		0
#define SC2730_SLP_LDOVIBR_PD_EN	BIT(14)
#define SC2730_LDO_VIBR_PD		BIT(13)

struct sc27xx_vibra_data {
	u32 cur_drv_cal_sel;
	u32 slp_pd_en;
	u32 ldo_pd;
};

struct vibra_info {
	struct input_dev	*input_dev;
	struct work_struct	play_work;
	struct regmap		*regmap;
	const struct sc27xx_vibra_data *data;
	u32			base;
	u32			strength;
	bool			enabled;
};

static const struct sc27xx_vibra_data sc2731_data = {
	.cur_drv_cal_sel = CUR_DRV_CAL_SEL,
	.slp_pd_en = SLP_LDOVIBR_PD_EN,
	.ldo_pd = LDO_VIBR_PD,
};

static const struct sc27xx_vibra_data sc2730_data = {
	.cur_drv_cal_sel = SC2730_CUR_DRV_CAL_SEL,
	.slp_pd_en = SC2730_SLP_LDOVIBR_PD_EN,
	.ldo_pd = SC2730_LDO_VIBR_PD,
};

static const struct sc27xx_vibra_data sc2721_data = {
	.cur_drv_cal_sel = CUR_DRV_CAL_SEL,
	.slp_pd_en = SLP_LDOVIBR_PD_EN,
	.ldo_pd = LDO_VIBR_PD,
};

static void sc27xx_vibra_set(struct vibra_info *info, bool on)
{
	const struct sc27xx_vibra_data *data = info->data;
	if (on) {
		regmap_update_bits(info->regmap, info->base, data->ldo_pd, 0);
		regmap_update_bits(info->regmap, info->base,
				   data->slp_pd_en, 0);
		info->enabled = true;
	} else {
		regmap_update_bits(info->regmap, info->base, data->ldo_pd,
				   data->ldo_pd);
		regmap_update_bits(info->regmap, info->base,
				   data->slp_pd_en, data->slp_pd_en);
		info->enabled = false;
	}
}

static int sc27xx_vibra_hw_init(struct vibra_info *info)
{
	const struct sc27xx_vibra_data *data = info->data;

	if (!data->cur_drv_cal_sel)
		return 0;

	return regmap_update_bits(info->regmap, info->base,
				  data->cur_drv_cal_sel, 0);
}

static void sc27xx_vibra_play_work(struct work_struct *work)
{
	struct vibra_info *info = container_of(work, struct vibra_info,
					       play_work);

	if (info->strength && !info->enabled)
		sc27xx_vibra_set(info, true);
	else if (info->strength == 0 && info->enabled)
		sc27xx_vibra_set(info, false);
}

static int sc27xx_vibra_play(struct input_dev *input, void *data,
			     struct ff_effect *effect)
{
	struct vibra_info *info = input_get_drvdata(input);

	info->strength = effect->u.rumble.weak_magnitude;
	schedule_work(&info->play_work);

	return 0;
}

static void sc27xx_vibra_close(struct input_dev *input)
{
	struct vibra_info *info = input_get_drvdata(input);

	cancel_work_sync(&info->play_work);
	if (info->enabled)
		sc27xx_vibra_set(info, false);
}

static int sc27xx_vibra_probe(struct platform_device *pdev)
{
	struct vibra_info *info;
	const struct sc27xx_vibra_data *data;
	int error;

	data = device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no matching driver data found\n");
		return -EINVAL;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!info->regmap) {
		dev_err(&pdev->dev, "failed to get vibrator regmap.\n");
		return -ENODEV;
	}

	error = device_property_read_u32(&pdev->dev, "reg", &info->base);
	if (error) {
		dev_err(&pdev->dev, "failed to get vibrator base address.\n");
		return error;
	}

	info->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!info->input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device.\n");
		return -ENOMEM;
	}

	info->input_dev->name = "sc27xx:vibrator";
	info->input_dev->id.version = 0;
	info->input_dev->close = sc27xx_vibra_close;
	info->data = data;

	input_set_drvdata(info->input_dev, info);
	input_set_capability(info->input_dev, EV_FF, FF_RUMBLE);
	INIT_WORK(&info->play_work, sc27xx_vibra_play_work);
	info->enabled = false;

	error = sc27xx_vibra_hw_init(info);
	if (error) {
		dev_err(&pdev->dev, "failed to initialize the vibrator.\n");
		return error;
	}

	error = input_ff_create_memless(info->input_dev, NULL,
					sc27xx_vibra_play);
	if (error) {
		dev_err(&pdev->dev, "failed to register vibrator to FF.\n");
		return error;
	}

	error = input_register_device(info->input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device.\n");
		return error;
	}

	return 0;
}

static const struct of_device_id sc27xx_vibra_of_match[] = {
	{ .compatible = "sprd,sc2721-vibrator", .data = &sc2721_data },
	{ .compatible = "sprd,sc2730-vibrator", .data = &sc2730_data },
	{ .compatible = "sprd,sc2731-vibrator", .data = &sc2731_data },
	{}
};
MODULE_DEVICE_TABLE(of, sc27xx_vibra_of_match);

static struct platform_driver sc27xx_vibra_driver = {
	.driver = {
		.name = "sc27xx-vibrator",
		.of_match_table = sc27xx_vibra_of_match,
	},
	.probe = sc27xx_vibra_probe,
};

module_platform_driver(sc27xx_vibra_driver);

MODULE_DESCRIPTION("Spreadtrum SC27xx Vibrator Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xiaotong Lu <xiaotong.lu@spreadtrum.com>");
