// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G3E TSU Temperature Sensor Unit
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */
#include <linux/clk.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "../thermal_hwmon.h"

/* TSU Register offsets and bits */
#define TSU_SSUSR		0x00
#define TSU_SSUSR_EN_TS		BIT(0)
#define TSU_SSUSR_ADC_PD_TS	BIT(1)
#define TSU_SSUSR_SOC_TS_EN	BIT(2)

#define TSU_STRGR		0x04
#define TSU_STRGR_ADST		BIT(0)

#define TSU_SOSR1		0x08
#define TSU_SOSR1_ADCT_8	0x03
#define TSU_SOSR1_ADCS		BIT(4)
#define TSU_SOSR1_OUTSEL	BIT(9)

#define TSU_SCRR		0x10
#define TSU_SCRR_OUT12BIT_TS	GENMASK(11, 0)

#define TSU_SSR			0x14
#define TSU_SSR_CONV		BIT(0)

#define TSU_CMSR		0x18
#define TSU_CMSR_CMPEN		BIT(0)

#define TSU_LLSR		0x1C
#define TSU_ULSR		0x20

#define TSU_SISR		0x30
#define TSU_SISR_ADF		BIT(0)
#define TSU_SISR_CMPF		BIT(1)

#define TSU_SIER		0x34
#define TSU_SIER_CMPIE		BIT(1)

#define TSU_SICR		0x38
#define TSU_SICR_ADCLR		BIT(0)
#define TSU_SICR_CMPCLR	BIT(1)

/* Temperature calculation constants from datasheet */
#define TSU_TEMP_D		(-41)
#define TSU_TEMP_E		126
#define TSU_CODE_MAX		0xFFF

/* Timing specifications from datasheet */
#define TSU_POWERUP_TIME_US	120	/* 120T at 1MHz sensor clock per datasheet */
#define TSU_CONV_TIME_US	50	/* Per sample conversion time */
#define TSU_POLL_DELAY_US	10	/* Polling interval */
#define TSU_MIN_CLOCK_RATE	24000000  /* TSU_PCLK minimum 24MHz */

/**
 * struct rzg3e_thermal_priv - RZ/G3E TSU private data
 * @base: TSU register base
 * @dev: device pointer
 * @syscon: regmap for calibration values
 * @zone: thermal zone device
 * @rstc: reset control
 * @trmval0: calibration value 0 (b)
 * @trmval1: calibration value 1 (c)
 * @trim_offset: offset for trim registers in syscon
 * @lock: protects hardware access during conversions
 */
struct rzg3e_thermal_priv {
	void __iomem *base;
	struct device *dev;
	struct regmap *syscon;
	struct thermal_zone_device *zone;
	struct reset_control *rstc;
	u16 trmval0;
	u16 trmval1;
	u32 trim_offset;
	struct mutex lock;
};

static int rzg3e_thermal_power_on(struct rzg3e_thermal_priv *priv)
{
	u32 val;
	int ret;

	/* Clear any pending interrupts */
	writel(TSU_SICR_ADCLR | TSU_SICR_CMPCLR, priv->base + TSU_SICR);

	/* Disable all interrupts during setup */
	writel(0, priv->base + TSU_SIER);

	/*
	 * Power-on sequence per datasheet 7.11.9.1:
	 * SOC_TS_EN must be set at same time or before EN_TS and ADC_PD_TS
	 */
	val = TSU_SSUSR_SOC_TS_EN | TSU_SSUSR_EN_TS;
	writel(val, priv->base + TSU_SSUSR);

	/* Wait for sensor stabilization per datasheet 7.11.7.1 */
	usleep_range(TSU_POWERUP_TIME_US, TSU_POWERUP_TIME_US + 10);

	/* Configure for average mode with 8 samples */
	val = TSU_SOSR1_OUTSEL | TSU_SOSR1_ADCT_8;
	writel(val, priv->base + TSU_SOSR1);

	/* Ensure we're in single scan mode (default) */
	val = readl(priv->base + TSU_SOSR1);
	if (val & TSU_SOSR1_ADCS) {
		dev_err(priv->dev, "Invalid scan mode setting\n");
		return -EINVAL;
	}

	/* Wait for any ongoing conversion to complete */
	ret = readl_poll_timeout(priv->base + TSU_SSR, val,
				 !(val & TSU_SSR_CONV),
				 TSU_POLL_DELAY_US,
				 USEC_PER_MSEC);
	if (ret) {
		dev_err(priv->dev, "Timeout waiting for conversion\n");
		return ret;
	}

	return 0;
}

static void rzg3e_thermal_power_off(struct rzg3e_thermal_priv *priv)
{
	/* Disable all interrupts */
	writel(0, priv->base + TSU_SIER);

	/* Clear pending interrupts */
	writel(TSU_SICR_ADCLR | TSU_SICR_CMPCLR, priv->base + TSU_SICR);

	/* Power down sequence per datasheet */
	writel(TSU_SSUSR_ADC_PD_TS, priv->base + TSU_SSUSR);
}

/*
 * Convert 12-bit sensor code to temperature in millicelsius
 * Formula from datasheet 7.11.7.8:
 * T(°C) = ((e - d) / (c - b)) * (a - b) + d
 * where: a = sensor code, b = trmval0, c = trmval1, d = -41, e = 126
 */
static int rzg3e_thermal_code_to_temp(struct rzg3e_thermal_priv *priv, u16 code)
{
	int temp_e_mc = TSU_TEMP_E * MILLIDEGREE_PER_DEGREE;
	int temp_d_mc = TSU_TEMP_D * MILLIDEGREE_PER_DEGREE;
	s64 numerator, denominator;
	int temp_mc;

	numerator = (temp_e_mc - temp_d_mc) * (s64)(code - priv->trmval0);
	denominator = priv->trmval1 - priv->trmval0;

	temp_mc = div64_s64(numerator, denominator) + temp_d_mc;

	return clamp(temp_mc, temp_d_mc, temp_e_mc);
}

/*
 * Convert temperature in millicelsius to 12-bit sensor code
 * Formula from datasheet 7.11.7.9 (inverse of above)
 */
static u16 rzg3e_thermal_temp_to_code(struct rzg3e_thermal_priv *priv, int temp_mc)
{
	int temp_e_mc = TSU_TEMP_E * MILLIDEGREE_PER_DEGREE;
	int temp_d_mc = TSU_TEMP_D * MILLIDEGREE_PER_DEGREE;
	s64 numerator, denominator;
	s64 code;

	numerator = (temp_mc - temp_d_mc) * (priv->trmval1 - priv->trmval0);
	denominator = temp_e_mc - temp_d_mc;

	code = div64_s64(numerator, denominator) + priv->trmval0;

	return clamp_val(code, 0, TSU_CODE_MAX);
}

static int rzg3e_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct rzg3e_thermal_priv *priv = thermal_zone_device_priv(tz);
	u32 status, code;
	int ret, timeout;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret < 0)
		return ret;

	guard(mutex)(&priv->lock);

	/* Clear any previous conversion status */
	writel(TSU_SICR_ADCLR, priv->base + TSU_SICR);

	/* Start single conversion */
	writel(TSU_STRGR_ADST, priv->base + TSU_STRGR);

	/* Wait for conversion completion - 8 samples at ~50us each */
	timeout = TSU_CONV_TIME_US * 8 * 2;  /* Double for margin */
	ret = readl_poll_timeout(priv->base + TSU_SISR, status,
				 status & TSU_SISR_ADF,
				 TSU_POLL_DELAY_US, timeout);
	if (ret) {
		dev_err(priv->dev, "Conversion timeout (status=0x%08x)\n", status);
		goto out;
	}

	/* Read the averaged result and clear the complete flag */
	code = readl(priv->base + TSU_SCRR) & TSU_SCRR_OUT12BIT_TS;
	writel(TSU_SICR_ADCLR, priv->base + TSU_SICR);

	/* Convert to temperature */
	*temp = rzg3e_thermal_code_to_temp(priv, code);

	dev_dbg(priv->dev, "temp=%d mC (%d.%03d°C), code=0x%03x\n",
		*temp, *temp / 1000, abs(*temp) % 1000, code);

out:
	pm_runtime_mark_last_busy(priv->dev);
	pm_runtime_put_autosuspend(priv->dev);
	return ret;
}

static int rzg3e_thermal_set_trips(struct thermal_zone_device *tz,
				   int low, int high)
{
	struct rzg3e_thermal_priv *priv = thermal_zone_device_priv(tz);
	u16 low_code, high_code;
	u32 val;
	int ret;

	/* Hardware requires low < high */
	if (low >= high)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret < 0)
		return ret;

	guard(mutex)(&priv->lock);

	/* Convert temperatures to codes */
	low_code = rzg3e_thermal_temp_to_code(priv, low);
	high_code = rzg3e_thermal_temp_to_code(priv, high);

	dev_dbg(priv->dev, "set_trips: low=%d high=%d (codes: 0x%03x/0x%03x)\n",
		low, high, low_code, high_code);

	/* Disable comparison during reconfiguration */
	writel(0, priv->base + TSU_SIER);
	writel(0, priv->base + TSU_CMSR);

	/* Clear any pending comparison interrupts */
	writel(TSU_SICR_CMPCLR, priv->base + TSU_SICR);

	/* Set trip points */
	writel(low_code, priv->base + TSU_LLSR);
	writel(high_code, priv->base + TSU_ULSR);

	/*
	 * Ensure OUTSEL is set for comparison per datasheet 7.11.7.4
	 * Comparison uses averaged data
	 */
	val = readl(priv->base + TSU_SOSR1);
	val |= TSU_SOSR1_OUTSEL;
	writel(val, priv->base + TSU_SOSR1);

	/* Enable comparison with "out of range" mode (CMPCOND=0) */
	writel(TSU_CMSR_CMPEN, priv->base + TSU_CMSR);

	/* Unmask compare IRQ and start a conversion to evaluate window */
	writel(TSU_SIER_CMPIE, priv->base + TSU_SIER);
	writel(TSU_STRGR_ADST, priv->base + TSU_STRGR);

	pm_runtime_mark_last_busy(priv->dev);
	pm_runtime_put_autosuspend(priv->dev);

	return 0;
}

static irqreturn_t rzg3e_thermal_irq_thread(int irq, void *data)
{
	struct rzg3e_thermal_priv *priv = data;

	dev_dbg(priv->dev, "Temperature threshold crossed\n");

	/* Notify thermal framework to re-evaluate trip points */
	thermal_zone_device_update(priv->zone, THERMAL_TRIP_VIOLATED);

	return IRQ_HANDLED;
}

static irqreturn_t rzg3e_thermal_irq(int irq, void *data)
{
	struct rzg3e_thermal_priv *priv = data;
	u32 status;

	status = readl(priv->base + TSU_SISR);

	/* Check if comparison interrupt occurred */
	if (status & TSU_SISR_CMPF) {
		/* Clear irq flag and disable interrupt until reconfigured */
		writel(TSU_SICR_CMPCLR, priv->base + TSU_SICR);
		writel(0, priv->base + TSU_SIER);

		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static const struct thermal_zone_device_ops rzg3e_tz_ops = {
	.get_temp = rzg3e_thermal_get_temp,
	.set_trips = rzg3e_thermal_set_trips,
};

static int rzg3e_thermal_get_calibration(struct rzg3e_thermal_priv *priv)
{
	u32 val;
	int ret;

	/* Read calibration values from syscon */
	ret = regmap_read(priv->syscon, priv->trim_offset, &val);
	if (ret)
		return ret;
	priv->trmval0 = val & GENMASK(11, 0);

	ret = regmap_read(priv->syscon, priv->trim_offset + 4, &val);
	if (ret)
		return ret;
	priv->trmval1 = val & GENMASK(11, 0);

	/* Validate calibration data */
	if (!priv->trmval0 || !priv->trmval1 ||
	    priv->trmval0 == priv->trmval1 ||
	    priv->trmval0 == 0xFFF || priv->trmval1 == 0xFFF) {
		dev_err(priv->dev, "Invalid calibration: b=0x%03x, c=0x%03x\n",
			priv->trmval0, priv->trmval1);
		return -EINVAL;
	}

	dev_dbg(priv->dev, "Calibration: b=0x%03x (%u), c=0x%03x (%u)\n",
		priv->trmval0, priv->trmval0, priv->trmval1, priv->trmval1);

	return 0;
}

static int rzg3e_thermal_parse_dt(struct rzg3e_thermal_priv *priv)
{
	struct device_node *np = priv->dev->of_node;
	u32 offset;

	priv->syscon = syscon_regmap_lookup_by_phandle_args(np, "renesas,tsu-trim", 1, &offset);
	if (IS_ERR(priv->syscon))
		return dev_err_probe(priv->dev, PTR_ERR(priv->syscon),
				     "Failed to parse renesas,tsu-trim\n");

	priv->trim_offset = offset;
	return 0;
}

static int rzg3e_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg3e_thermal_priv *priv;
	struct clk *clk;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/* Parse device tree for trim register info */
	ret = rzg3e_thermal_parse_dt(priv);
	if (ret)
		return ret;

	/* Get clock to verify frequency - clock is managed by power domain */
	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk),
				     "Failed to get clock\n");

	if (clk_get_rate(clk) < TSU_MIN_CLOCK_RATE)
		return dev_err_probe(dev, -EINVAL,
				     "Clock rate %lu Hz too low (min %u Hz)\n",
				     clk_get_rate(clk), TSU_MIN_CLOCK_RATE);

	priv->rstc = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(priv->rstc))
		return dev_err_probe(dev, PTR_ERR(priv->rstc),
				     "Failed to get/deassert reset control\n");

	/* Get calibration data */
	ret = rzg3e_thermal_get_calibration(priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get valid calibration data\n");

	/* Get comparison interrupt */
	irq = platform_get_irq_byname(pdev, "adcmpi");
	if (irq < 0)
		return irq;

	/* Enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	devm_pm_runtime_enable(dev);

	/* Initial hardware setup */
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Runtime resume failed\n");

	/* Register thermal zone - this will trigger DT parsing */
	priv->zone = devm_thermal_of_zone_register(dev, 0, priv, &rzg3e_tz_ops);
	if (IS_ERR(priv->zone)) {
		ret = PTR_ERR(priv->zone);
		dev_err(dev, "Failed to register thermal zone: %d\n", ret);
		goto err_pm_put;
	}

	/* Request threaded IRQ for comparison interrupt */
	ret = devm_request_threaded_irq(dev, irq, rzg3e_thermal_irq,
					rzg3e_thermal_irq_thread,
					IRQF_ONESHOT, "rzg3e_thermal", priv);
	if (ret) {
		dev_err(dev, "Failed to request IRQ: %d\n", ret);
		goto err_pm_put;
	}

	/* Add hwmon sysfs interface */
	ret = devm_thermal_add_hwmon_sysfs(dev, priv->zone);
	if (ret)
		dev_warn(dev, "Failed to add hwmon sysfs attributes\n");

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	dev_info(dev, "RZ/G3E thermal sensor registered\n");

	return 0;

err_pm_put:
	pm_runtime_put_sync(dev);
	return ret;
}

static int rzg3e_thermal_runtime_suspend(struct device *dev)
{
	struct rzg3e_thermal_priv *priv = dev_get_drvdata(dev);

	rzg3e_thermal_power_off(priv);
	return 0;
}

static int rzg3e_thermal_runtime_resume(struct device *dev)
{
	struct rzg3e_thermal_priv *priv = dev_get_drvdata(dev);

	return rzg3e_thermal_power_on(priv);
}

static int rzg3e_thermal_suspend(struct device *dev)
{
	struct rzg3e_thermal_priv *priv = dev_get_drvdata(dev);

	/* If device is active, power it off */
	if (pm_runtime_active(dev))
		rzg3e_thermal_power_off(priv);

	/* Assert reset to ensure clean state after resume */
	reset_control_assert(priv->rstc);

	return 0;
}

static int rzg3e_thermal_resume(struct device *dev)
{
	struct rzg3e_thermal_priv *priv = dev_get_drvdata(dev);
	int ret;

	/* Deassert reset */
	ret = reset_control_deassert(priv->rstc);
	if (ret) {
		dev_err(dev, "Failed to deassert reset: %d\n", ret);
		return ret;
	}

	/* If device was active before suspend, power it back on */
	if (pm_runtime_active(dev))
		return rzg3e_thermal_power_on(priv);

	return 0;
}

static const struct dev_pm_ops rzg3e_thermal_pm_ops = {
	RUNTIME_PM_OPS(rzg3e_thermal_runtime_suspend,
		       rzg3e_thermal_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(rzg3e_thermal_suspend, rzg3e_thermal_resume)
};

static const struct of_device_id rzg3e_thermal_dt_ids[] = {
	{ .compatible = "renesas,r9a09g047-tsu" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg3e_thermal_dt_ids);

static struct platform_driver rzg3e_thermal_driver = {
	.driver = {
		.name = "rzg3e_thermal",
		.of_match_table = rzg3e_thermal_dt_ids,
		.pm = pm_ptr(&rzg3e_thermal_pm_ops),
	},
	.probe = rzg3e_thermal_probe,
};
module_platform_driver(rzg3e_thermal_driver);

MODULE_DESCRIPTION("Renesas RZ/G3E TSU Thermal Sensor Driver");
MODULE_AUTHOR("John Madieu <john.madieu.xa@bp.renesas.com>");
MODULE_LICENSE("GPL");
