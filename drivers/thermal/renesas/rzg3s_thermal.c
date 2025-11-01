// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G3S TSU Thermal Sensor Driver
 *
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iio/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "../thermal_hwmon.h"

#define TSU_SM			0x0
#define TSU_SM_EN		BIT(0)
#define TSU_SM_OE		BIT(1)
#define OTPTSUTRIM_REG(n)	(0x18 + (n) * 0x4)
#define OTPTSUTRIM_EN_MASK	BIT(31)
#define OTPTSUTRIM_MASK		GENMASK(11, 0)

#define TSU_READ_STEPS		8

/* Default calibration values, if FUSE values are missing. */
#define SW_CALIB0_VAL		1297
#define SW_CALIB1_VAL		751

#define MCELSIUS(temp)		((temp) * MILLIDEGREE_PER_DEGREE)

/**
 * struct rzg3s_thermal_priv - RZ/G3S thermal private data structure
 * @base: TSU base address
 * @dev: device pointer
 * @tz: thermal zone pointer
 * @rstc: reset control
 * @channel: IIO channel to read the TSU
 * @mode: current device mode
 * @calib0: calibration value
 * @calib1: calibration value
 */
struct rzg3s_thermal_priv {
	void __iomem *base;
	struct device *dev;
	struct thermal_zone_device *tz;
	struct reset_control *rstc;
	struct iio_channel *channel;
	enum thermal_device_mode mode;
	u16 calib0;
	u16 calib1;
};

static int rzg3s_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct rzg3s_thermal_priv *priv = thermal_zone_device_priv(tz);
	int ts_code_ave = 0;

	if (priv->mode != THERMAL_DEVICE_ENABLED)
		return -EAGAIN;

	for (u8 i = 0; i < TSU_READ_STEPS; i++) {
		int ret, val;

		ret = iio_read_channel_raw(priv->channel, &val);
		if (ret < 0)
			return ret;

		ts_code_ave += val;
		/*
		 * According to the HW manual (Rev.1.10, section 40.4.4 Procedure for Measuring
		 * the Temperature) we need to wait here at leat 3us.
		 */
		usleep_range(5, 10);
	}

	ts_code_ave = DIV_ROUND_CLOSEST(MCELSIUS(ts_code_ave), TSU_READ_STEPS);

	/*
	 * According to the HW manual (Rev.1.10, section 40.4.4 Procedure for Measuring the
	 * Temperature) the computation formula is as follows:
	 *
	 * Tj = (ts_code_ave - priv->calib1) * 165 / (priv->calib0 - priv->calib1) - 40
	 *
	 * Convert everything to milli Celsius before applying the formula to avoid
	 * losing precision.
	 */

	*temp = div_s64((s64)(ts_code_ave - MCELSIUS(priv->calib1)) * MCELSIUS(165),
			MCELSIUS(priv->calib0 - priv->calib1)) - MCELSIUS(40);

	/* Report it in milli degrees Celsius and round it up to 0.5 degrees Celsius. */
	*temp = roundup(*temp, 500);

	return 0;
}

static void rzg3s_thermal_set_mode(struct rzg3s_thermal_priv *priv,
				   enum thermal_device_mode mode)
{
	struct device *dev = priv->dev;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return;

	if (mode == THERMAL_DEVICE_DISABLED) {
		writel(0, priv->base + TSU_SM);
	} else {
		writel(TSU_SM_EN, priv->base + TSU_SM);
		/*
		 * According to the HW manual (Rev.1.10, section 40.4.1 Procedure for
		 * Starting the TSU) we need to wait here 30us or more.
		 */
		usleep_range(30, 40);

		writel(TSU_SM_OE | TSU_SM_EN, priv->base + TSU_SM);
		/*
		 * According to the HW manual (Rev.1.10, section 40.4.1 Procedure for
		 * Starting the TSU) we need to wait here 50us or more.
		 */
		usleep_range(50, 60);
	}

	pm_runtime_put_autosuspend(dev);
}

static int rzg3s_thermal_change_mode(struct thermal_zone_device *tz,
				     enum thermal_device_mode mode)
{
	struct rzg3s_thermal_priv *priv = thermal_zone_device_priv(tz);

	if (priv->mode == mode)
		return 0;

	rzg3s_thermal_set_mode(priv, mode);
	priv->mode = mode;

	return 0;
}

static const struct thermal_zone_device_ops rzg3s_tz_of_ops = {
	.get_temp = rzg3s_thermal_get_temp,
	.change_mode = rzg3s_thermal_change_mode,
};

static int rzg3s_thermal_read_calib(struct rzg3s_thermal_priv *priv)
{
	struct device *dev = priv->dev;
	u32 val;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	val = readl(priv->base + OTPTSUTRIM_REG(0));
	if (val & OTPTSUTRIM_EN_MASK)
		priv->calib0 = FIELD_GET(OTPTSUTRIM_MASK, val);
	else
		priv->calib0 = SW_CALIB0_VAL;

	val = readl(priv->base + OTPTSUTRIM_REG(1));
	if (val & OTPTSUTRIM_EN_MASK)
		priv->calib1 = FIELD_GET(OTPTSUTRIM_MASK, val);
	else
		priv->calib1 = SW_CALIB1_VAL;

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int rzg3s_thermal_probe(struct platform_device *pdev)
{
	struct rzg3s_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->channel = devm_iio_channel_get(dev, "tsu");
	if (IS_ERR(priv->channel))
		return dev_err_probe(dev, PTR_ERR(priv->channel), "Failed to get IIO channel!\n");

	priv->rstc = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(priv->rstc))
		return dev_err_probe(dev, PTR_ERR(priv->rstc), "Failed to get reset!\n");

	priv->dev = dev;
	priv->mode = THERMAL_DEVICE_DISABLED;
	platform_set_drvdata(pdev, priv);

	pm_runtime_set_autosuspend_delay(dev, 300);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable runtime PM!\n");

	ret = rzg3s_thermal_read_calib(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read calibration data!\n");

	priv->tz = devm_thermal_of_zone_register(dev, 0, priv, &rzg3s_tz_of_ops);
	if (IS_ERR(priv->tz))
		return dev_err_probe(dev, PTR_ERR(priv->tz), "Failed to register thermal zone!\n");

	ret = devm_thermal_add_hwmon_sysfs(dev, priv->tz);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add hwmon sysfs!\n");

	return 0;
}

static int rzg3s_thermal_suspend(struct device *dev)
{
	struct rzg3s_thermal_priv *priv = dev_get_drvdata(dev);

	rzg3s_thermal_set_mode(priv, THERMAL_DEVICE_DISABLED);

	return reset_control_assert(priv->rstc);
}

static int rzg3s_thermal_resume(struct device *dev)
{
	struct rzg3s_thermal_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(priv->rstc);
	if (ret)
		return ret;

	if (priv->mode != THERMAL_DEVICE_DISABLED)
		rzg3s_thermal_set_mode(priv, priv->mode);

	return 0;
}

static const struct dev_pm_ops rzg3s_thermal_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(rzg3s_thermal_suspend, rzg3s_thermal_resume)
};

static const struct of_device_id rzg3s_thermal_dt_ids[] = {
	{ .compatible = "renesas,r9a08g045-tsu" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg3s_thermal_dt_ids);

static struct platform_driver rzg3s_thermal_driver = {
	.driver = {
		.name = "rzg3s-thermal",
		.of_match_table = rzg3s_thermal_dt_ids,
		.pm = pm_ptr(&rzg3s_thermal_pm_ops),
	},
	.probe = rzg3s_thermal_probe,
};
module_platform_driver(rzg3s_thermal_driver);

MODULE_DESCRIPTION("Renesas RZ/G3S Thermal Sensor Unit Driver");
MODULE_AUTHOR("Claudiu Beznea <claudiu.beznea.uj@bp.renesas.com>");
MODULE_LICENSE("GPL");
