// SPDX-License-Identifier: GPL-2.0
/*
 * Thermal sensor driver for SpacemiT K1 SoC
 *
 * Copyright (C) 2026 Shuwei Wu <shuwei.wu@mailbox.org>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "../thermal_hwmon.h"

#define K1_TSENSOR_PCTRL_REG		0x00
#define K1_TSENSOR_PCTRL_ENABLE		BIT(0)
#define K1_TSENSOR_PCTRL_TEMP_MODE	BIT(3)
#define K1_TSENSOR_PCTRL_RAW_SEL	BIT(7)

#define K1_TSENSOR_PCTRL_CTUNE		GENMASK(11, 8)
#define K1_TSENSOR_PCTRL_SW_CTRL	GENMASK(21, 18)
#define K1_TSENSOR_PCTRL_HW_AUTO_MODE	BIT(23)

#define K1_TSENSOR_EN_REG		0x08
#define K1_TSENSOR_EN_ALL		GENMASK(MAX_SENSOR_NUMBER - 1, 0)

#define K1_TSENSOR_TIME_REG		0x0C
#define K1_TSENSOR_TIME_WAIT_REF_CNT	GENMASK(3, 0)
#define K1_TSENSOR_TIME_ADC_CNT_RST	GENMASK(7, 4)
#define K1_TSENSOR_TIME_FILTER_PERIOD	GENMASK(21, 20)
#define K1_TSENSOR_TIME_MASK		GENMASK(23, 0)

#define K1_TSENSOR_INT_CLR_REG		0x10
#define K1_TSENSOR_INT_EN_REG		0x14
#define K1_TSENSOR_INT_STA_REG		0x18

#define K1_TSENSOR_INT_EN_MASK		BIT(0)
#define K1_TSENSOR_INT_MASK(x)		(GENMASK(2, 1) << ((x) * 2))

#define K1_TSENSOR_DATA_BASE_REG	0x20
#define K1_TSENSOR_DATA_REG(x)		(K1_TSENSOR_DATA_BASE_REG + ((x) / 2) * 4)
#define K1_TSENSOR_DATA_LOW_MASK	GENMASK(15, 0)
#define K1_TSENSOR_DATA_HIGH_MASK	GENMASK(31, 16)

#define K1_TSENSOR_THRSH_BASE_REG	0x40
#define K1_TSENSOR_THRSH_REG(x)		(K1_TSENSOR_THRSH_BASE_REG + ((x) * 4))
#define K1_TSENSOR_THRSH_LOW_MASK	GENMASK(15, 0)
#define K1_TSENSOR_THRSH_HIGH_MASK	GENMASK(31, 16)

#define MAX_SENSOR_NUMBER		5

/* Hardware offset value required for temperature calculation */
#define TEMPERATURE_OFFSET		278

struct k1_tsensor_channel {
	struct k1_tsensor *ts;
	struct thermal_zone_device *tzd;
	int id;
};

struct k1_tsensor {
	void __iomem *base;
	struct k1_tsensor_channel ch[MAX_SENSOR_NUMBER];
};

static void k1_tsensor_init(struct k1_tsensor *ts)
{
	u32 val;

	/* Disable all the interrupts */
	writel(0xffffffff, ts->base + K1_TSENSOR_INT_EN_REG);

	/* Configure ADC sampling time and filter period */
	val = readl(ts->base + K1_TSENSOR_TIME_REG);
	val &= ~K1_TSENSOR_TIME_MASK;
	val |= K1_TSENSOR_TIME_FILTER_PERIOD |
	       K1_TSENSOR_TIME_ADC_CNT_RST |
	       K1_TSENSOR_TIME_WAIT_REF_CNT;
	writel(val, ts->base + K1_TSENSOR_TIME_REG);

	/*
	 * Enable all sensors' auto mode, enable dither control,
	 * consecutive mode, and power up sensor.
	 */
	val = readl(ts->base + K1_TSENSOR_PCTRL_REG);
	val &= ~K1_TSENSOR_PCTRL_SW_CTRL;
	val &= ~K1_TSENSOR_PCTRL_CTUNE;
	val |= K1_TSENSOR_PCTRL_RAW_SEL |
	       K1_TSENSOR_PCTRL_TEMP_MODE |
	       K1_TSENSOR_PCTRL_HW_AUTO_MODE |
	       K1_TSENSOR_PCTRL_ENABLE;
	writel(val, ts->base + K1_TSENSOR_PCTRL_REG);

	/* Enable each sensor */
	val = readl(ts->base + K1_TSENSOR_EN_REG);
	val |= K1_TSENSOR_EN_ALL;
	writel(val, ts->base + K1_TSENSOR_EN_REG);
}

static void k1_tsensor_enable_irq(struct k1_tsensor_channel *ch)
{
	struct k1_tsensor *ts = ch->ts;
	u32 val;

	val = readl(ts->base + K1_TSENSOR_INT_CLR_REG);
	val |= K1_TSENSOR_INT_MASK(ch->id);
	writel(val, ts->base + K1_TSENSOR_INT_CLR_REG);

	val = readl(ts->base + K1_TSENSOR_INT_EN_REG);
	val &= ~K1_TSENSOR_INT_MASK(ch->id);
	writel(val, ts->base + K1_TSENSOR_INT_EN_REG);

	/* Enable thermal interrupt */
	val = readl(ts->base + K1_TSENSOR_INT_EN_REG);
	val |= K1_TSENSOR_INT_EN_MASK;
	writel(val, ts->base + K1_TSENSOR_INT_EN_REG);
}

/*
 * The conversion formula used is:
 * T(m°C) = (((raw_value & mask) >> shift) - TEMPERATURE_OFFSET) * 1000
 */
static int k1_tsensor_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct k1_tsensor_channel *ch = thermal_zone_device_priv(tz);
	struct k1_tsensor *ts = ch->ts;
	u32 val;

	val = readl(ts->base + K1_TSENSOR_DATA_REG(ch->id));
	if (ch->id % 2)
		*temp = FIELD_GET(K1_TSENSOR_DATA_HIGH_MASK, val);
	else
		*temp = FIELD_GET(K1_TSENSOR_DATA_LOW_MASK, val);

	*temp -= TEMPERATURE_OFFSET;
	*temp *= 1000;

	return 0;
}

/*
 * For each sensor, the hardware threshold register is 32 bits:
 * - Lower 16 bits [15:0] configure the low threshold temperature.
 * - Upper 16 bits [31:16] configure the high threshold temperature.
 */
static int k1_tsensor_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct k1_tsensor_channel *ch = thermal_zone_device_priv(tz);
	struct k1_tsensor *ts = ch->ts;
	u32 val;

	if (low >= high)
		return -EINVAL;

	low = clamp_val(low / 1000 + TEMPERATURE_OFFSET, TEMPERATURE_OFFSET,
			FIELD_MAX(K1_TSENSOR_THRSH_LOW_MASK));
	high = clamp_val(high / 1000 + TEMPERATURE_OFFSET, TEMPERATURE_OFFSET,
			 FIELD_MAX(K1_TSENSOR_THRSH_HIGH_MASK));

	val = readl(ts->base + K1_TSENSOR_THRSH_REG(ch->id));

	val &= ~(K1_TSENSOR_THRSH_LOW_MASK | K1_TSENSOR_THRSH_HIGH_MASK);
	val |= FIELD_PREP(K1_TSENSOR_THRSH_LOW_MASK, low);
	val |= FIELD_PREP(K1_TSENSOR_THRSH_HIGH_MASK, high);

	writel(val, ts->base + K1_TSENSOR_THRSH_REG(ch->id));

	return 0;
}

static const struct thermal_zone_device_ops k1_tsensor_ops = {
	.get_temp = k1_tsensor_get_temp,
	.set_trips = k1_tsensor_set_trips,
};

static irqreturn_t k1_tsensor_irq_thread(int irq, void *data)
{
	struct k1_tsensor *ts = (struct k1_tsensor *)data;
	int mask, status, i;

	status = readl(ts->base + K1_TSENSOR_INT_STA_REG);

	for (i = 0; i < MAX_SENSOR_NUMBER; i++) {
		if (status & K1_TSENSOR_INT_MASK(i)) {
			mask = readl(ts->base + K1_TSENSOR_INT_CLR_REG);
			mask |= K1_TSENSOR_INT_MASK(i);
			writel(mask, ts->base + K1_TSENSOR_INT_CLR_REG);
			thermal_zone_device_update(ts->ch[i].tzd, THERMAL_EVENT_UNSPECIFIED);
		}
	}

	return IRQ_HANDLED;
}

static int k1_tsensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct k1_tsensor *ts;
	struct reset_control *reset;
	struct clk *clk;
	int i, irq, ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ts->base))
		return dev_err_probe(dev, PTR_ERR(ts->base), "Failed to get reg\n");

	reset = devm_reset_control_get_exclusive_deasserted(dev, NULL);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "Failed to get/deassert reset control\n");

	clk = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get core clock\n");

	clk = devm_clk_get_enabled(dev, "bus");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get bus clock\n");

	k1_tsensor_init(ts);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL,
					k1_tsensor_irq_thread,
					IRQF_ONESHOT, "k1_tsensor", ts);
	if (ret < 0)
		return ret;

	for (i = 0; i < MAX_SENSOR_NUMBER; ++i) {
		ts->ch[i].id = i;
		ts->ch[i].ts = ts;
		ts->ch[i].tzd = devm_thermal_of_zone_register(dev, i, ts->ch + i, &k1_tsensor_ops);
		if (IS_ERR(ts->ch[i].tzd))
			return PTR_ERR(ts->ch[i].tzd);

		/* Attach sysfs hwmon attributes for userspace monitoring */
		ret = devm_thermal_add_hwmon_sysfs(dev, ts->ch[i].tzd);
		if (ret)
			dev_warn(dev, "Failed to add hwmon sysfs attributes\n");

		k1_tsensor_enable_irq(ts->ch + i);
	}

	platform_set_drvdata(pdev, ts);

	return 0;
}

static const struct of_device_id k1_tsensor_dt_ids[] = {
	{ .compatible = "spacemit,k1-tsensor" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, k1_tsensor_dt_ids);

static struct platform_driver k1_tsensor_driver = {
	.driver = {
		.name		= "k1_tsensor",
		.of_match_table = k1_tsensor_dt_ids,
	},
	.probe	= k1_tsensor_probe,
};
module_platform_driver(k1_tsensor_driver);

MODULE_DESCRIPTION("SpacemiT K1 Thermal Sensor Driver");
MODULE_AUTHOR("Shuwei Wu <shuwei.wu@mailbox.org>");
MODULE_LICENSE("GPL");
