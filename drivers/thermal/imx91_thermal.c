// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 NXP.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/nvmem-consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/thermal.h>
#include <linux/units.h>

#define REG_SET					0x4
#define REG_CLR					0x8
#define REG_TOG					0xc

#define IMX91_TMU_CTRL0				0x0
#define   IMX91_TMU_CTRL0_THR1_IE		BIT(9)
#define   IMX91_TMU_CTRL0_THR1_MASK		GENMASK(3, 2)
#define   IMX91_TMU_CTRL0_CLR_FLT1		BIT(21)

#define IMX91_TMU_THR_MODE_LE			0
#define IMX91_TMU_THR_MODE_GE			1

#define IMX91_TMU_STAT0				0x10
#define   IMX91_TMU_STAT0_THR1_IF		BIT(9)
#define   IMX91_TMU_STAT0_THR1_STAT		BIT(13)
#define   IMX91_TMU_STAT0_DRDY0_IF_MASK		BIT(16)

#define IMX91_TMU_DATA0				0x20

#define IMX91_TMU_CTRL1				0x200
#define IMX91_TMU_CTRL1_EN			BIT(31)
#define IMX91_TMU_CTRL1_START			BIT(30)
#define IMX91_TMU_CTRL1_STOP			BIT(29)
#define IMX91_TMU_CTRL1_RES_MASK		GENMASK(19, 18)
#define IMX91_TMU_CTRL1_MEAS_MODE_MASK		GENMASK(25, 24)
#define   IMX91_TMU_CTRL1_MEAS_MODE_SINGLE	0
#define   IMX91_TMU_CTRL1_MEAS_MODE_CONTINUES	1
#define   IMX91_TMU_CTRL1_MEAS_MODE_PERIODIC	2

#define IMX91_TMU_THR_CTRL01			0x30
#define   IMX91_TMU_THR_CTRL01_THR1_MASK	GENMASK(31, 16)

#define IMX91_TMU_REF_DIV			0x280
#define IMX91_TMU_DIV_EN			BIT(31)
#define IMX91_TMU_DIV_MASK			GENMASK(23, 16)
#define IMX91_TMU_DIV_MAX			255

#define IMX91_TMU_PUD_ST_CTRL			0x2b0
#define IMX91_TMU_PUDL_MASK			GENMASK(23, 16)

#define IMX91_TMU_TRIM1				0x2e0
#define IMX91_TMU_TRIM2				0x2f0

#define IMX91_TMU_TEMP_LOW_LIMIT		-40000
#define IMX91_TMU_TEMP_HIGH_LIMIT		125000

#define IMX91_TMU_DEFAULT_TRIM1_CONFIG		0xb561bc2d
#define IMX91_TMU_DEFAULT_TRIM2_CONFIG		0x65d4

#define IMX91_TMU_PERIOD_CTRL			0x270
#define   IMX91_TMU_PERIOD_CTRL_MEAS_MASK	GENMASK(23, 0)

#define IMX91_TMP_FRAC				64

struct imx91_tmu {
	void __iomem *base;
	struct clk *clk;
	struct device *dev;
	struct thermal_zone_device *tzd;
};

static void imx91_tmu_start(struct imx91_tmu *tmu, bool start)
{
	u32 val = start ? IMX91_TMU_CTRL1_START : IMX91_TMU_CTRL1_STOP;

	writel_relaxed(val, tmu->base + IMX91_TMU_CTRL1 + REG_SET);
}

static void imx91_tmu_enable(struct imx91_tmu *tmu, bool enable)
{
	u32 reg = IMX91_TMU_CTRL1;

	reg += enable ? REG_SET : REG_CLR;

	writel_relaxed(IMX91_TMU_CTRL1_EN, tmu->base + reg);
}

static int imx91_tmu_to_mcelsius(int x)
{
	return x * MILLIDEGREE_PER_DEGREE / IMX91_TMP_FRAC;
}

static int imx91_tmu_from_mcelsius(int x)
{
	return x * IMX91_TMP_FRAC / MILLIDEGREE_PER_DEGREE;
}

static int imx91_tmu_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct imx91_tmu *tmu = thermal_zone_device_priv(tz);
	s16 data;

	/* DATA0 is 16bit signed number */
	data = readw_relaxed(tmu->base + IMX91_TMU_DATA0);
	*temp = imx91_tmu_to_mcelsius(data);

	return 0;
}

static int imx91_tmu_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct imx91_tmu *tmu = thermal_zone_device_priv(tz);
	int val;

	if (high >= IMX91_TMU_TEMP_HIGH_LIMIT)
		return -EINVAL;

	writel_relaxed(IMX91_TMU_CTRL0_THR1_IE, tmu->base + IMX91_TMU_CTRL0 + REG_CLR);

	/* Comparator1 for temperature threshold */
	writel_relaxed(IMX91_TMU_THR_CTRL01_THR1_MASK, tmu->base + IMX91_TMU_THR_CTRL01 + REG_CLR);
	val = FIELD_PREP(IMX91_TMU_THR_CTRL01_THR1_MASK, imx91_tmu_from_mcelsius(high));

	writel_relaxed(val, tmu->base + IMX91_TMU_THR_CTRL01 + REG_SET);

	writel_relaxed(IMX91_TMU_STAT0_THR1_IF, tmu->base + IMX91_TMU_STAT0 + REG_CLR);

	writel_relaxed(IMX91_TMU_CTRL0_THR1_IE, tmu->base + IMX91_TMU_CTRL0 + REG_SET);

	return 0;
}

static int imx91_init_from_nvmem_cells(struct imx91_tmu *tmu)
{
	struct device *dev = tmu->dev;
	u32 trim1, trim2;
	int ret;

	ret = nvmem_cell_read_u32(dev, "trim1", &trim1);
	if (ret)
		return ret;

	ret = nvmem_cell_read_u32(dev, "trim2", &trim2);
	if (ret)
		return ret;

	if (trim1 == 0 || trim2 == 0)
		return -EINVAL;

	writel_relaxed(trim1, tmu->base + IMX91_TMU_TRIM1);
	writel_relaxed(trim2, tmu->base + IMX91_TMU_TRIM2);

	return 0;
}

static void imx91_tmu_action_remove(void *data)
{
	struct imx91_tmu *tmu = data;

	/* disable tmu */
	imx91_tmu_enable(tmu, false);
}

static irqreturn_t imx91_tmu_alarm_irq(int irq, void *data)
{
	struct imx91_tmu *tmu = data;
	u32 val;

	val = readl_relaxed(tmu->base + IMX91_TMU_STAT0);

	/* Check if comparison interrupt occurred */
	if (val & IMX91_TMU_STAT0_THR1_IF) {
		/* Clear irq flag and disable interrupt until reconfigured */
		writel(IMX91_TMU_STAT0_THR1_IF, tmu->base + IMX91_TMU_STAT0 + REG_CLR);
		writel_relaxed(IMX91_TMU_CTRL0_THR1_IE, tmu->base + IMX91_TMU_CTRL0 + REG_CLR);

		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t imx91_tmu_alarm_irq_thread(int irq, void *data)
{
	struct imx91_tmu *tmu = data;

	thermal_zone_device_update(tmu->tzd, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int imx91_tmu_change_mode(struct thermal_zone_device *tz, enum thermal_device_mode mode)
{
	struct imx91_tmu *tmu = thermal_zone_device_priv(tz);
	int ret;

	if (mode == THERMAL_DEVICE_ENABLED) {
		ret = pm_runtime_get(tmu->dev);
		if (ret < 0)
			return ret;

		writel_relaxed(IMX91_TMU_CTRL0_THR1_IE | IMX91_TMU_CTRL0_THR1_MASK,
			       tmu->base + IMX91_TMU_CTRL0 + REG_CLR);

		writel_relaxed(FIELD_PREP(IMX91_TMU_CTRL0_THR1_MASK, IMX91_TMU_THR_MODE_GE),
			       tmu->base + IMX91_TMU_CTRL0 + REG_SET);
		imx91_tmu_start(tmu, true);
	} else {
		writel_relaxed(IMX91_TMU_CTRL0_THR1_IE, tmu->base + IMX91_TMU_CTRL0 + REG_CLR);
		imx91_tmu_start(tmu, false);
		pm_runtime_put(tmu->dev);
	}

	return 0;
}

static struct thermal_zone_device_ops tmu_tz_ops = {
	.get_temp = imx91_tmu_get_temp,
	.change_mode = imx91_tmu_change_mode,
	.set_trips = imx91_tmu_set_trips,
};

static int imx91_tmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx91_tmu *tmu;
	unsigned long rate;
	int irq, ret;
	u32 div;

	tmu = devm_kzalloc(dev, sizeof(struct imx91_tmu), GFP_KERNEL);
	if (!tmu)
		return -ENOMEM;

	tmu->dev = dev;

	tmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tmu->base))
		return dev_err_probe(dev, PTR_ERR(tmu->base), "failed to get io resource");

	tmu->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(tmu->clk))
		return dev_err_probe(dev, PTR_ERR(tmu->clk), "failed to get tmu clock\n");

	platform_set_drvdata(pdev, tmu);

	/* disable the monitor during initialization */
	imx91_tmu_enable(tmu, false);
	imx91_tmu_start(tmu, false);

	ret = imx91_init_from_nvmem_cells(tmu);
	if (ret) {
		dev_warn(dev, "can't get trim value, use default settings\n");

		writel_relaxed(IMX91_TMU_DEFAULT_TRIM1_CONFIG, tmu->base + IMX91_TMU_TRIM1);
		writel_relaxed(IMX91_TMU_DEFAULT_TRIM2_CONFIG, tmu->base + IMX91_TMU_TRIM2);
	}

	/* The typical conv clk is 4MHz, the output freq is 'rate / (div + 1)' */
	rate = clk_get_rate(tmu->clk);
	div = (rate / (4 * HZ_PER_MHZ)) - 1;
	if (div > IMX91_TMU_DIV_MAX)
		return dev_err_probe(dev, -EINVAL, "clock divider exceed hardware limitation");

	/* Set divider value and enable divider */
	writel_relaxed(IMX91_TMU_DIV_EN | FIELD_PREP(IMX91_TMU_DIV_MASK, div),
		       tmu->base + IMX91_TMU_REF_DIV);

	/* Set max power up delay: 'Tpud(ms) = 0xFF * 1000 / 4000000' */
	writel_relaxed(FIELD_PREP(IMX91_TMU_PUDL_MASK, 100U), tmu->base + IMX91_TMU_PUD_ST_CTRL);

	/*
	 * Set resolution mode
	 * 00b - Conversion time = 0.59325 ms
	 * 01b - Conversion time = 1.10525 ms
	 * 10b - Conversion time = 2.12925 ms
	 * 11b - Conversion time = 4.17725 ms
	 */
	writel_relaxed(FIELD_PREP(IMX91_TMU_CTRL1_RES_MASK, 0x3),
		       tmu->base + IMX91_TMU_CTRL1 + REG_CLR);
	writel_relaxed(FIELD_PREP(IMX91_TMU_CTRL1_RES_MASK, 0x1),
		       tmu->base + IMX91_TMU_CTRL1 + REG_SET);

	writel_relaxed(IMX91_TMU_CTRL1_MEAS_MODE_MASK, tmu->base + IMX91_TMU_CTRL1 + REG_CLR);
	writel_relaxed(FIELD_PREP(IMX91_TMU_CTRL1_MEAS_MODE_MASK,
				  IMX91_TMU_CTRL1_MEAS_MODE_PERIODIC),
		       tmu->base + IMX91_TMU_CTRL1 + REG_SET);

	/*
	 * Set Periodic Measurement Frequency to 25Hz:
	 * tMEAS_FREQ = tCONV_CLK * PERIOD_CTRL[MEAS_FREQ]
	 */
	writel_relaxed(FIELD_PREP(IMX91_TMU_PERIOD_CTRL_MEAS_MASK, 4 * HZ_PER_MHZ / 25),
		       tmu->base + IMX91_TMU_PERIOD_CTRL);

	imx91_tmu_enable(tmu, true);
	ret = devm_add_action(dev, imx91_tmu_action_remove, tmu);
	if (ret)
		return dev_err_probe(dev, ret, "Failure to add action imx91_tmu_action_remove()\n");

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	tmu->tzd = devm_thermal_of_zone_register(dev, 0, tmu, &tmu_tz_ops);
	if (IS_ERR(tmu->tzd))
		return dev_err_probe(dev, PTR_ERR(tmu->tzd),
				     "failed to register thermal zone sensor\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, imx91_tmu_alarm_irq,
					imx91_tmu_alarm_irq_thread,
					IRQF_ONESHOT, "imx91_thermal", tmu);

	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to request alarm irq\n");

	pm_runtime_put(dev);

	return 0;
}

static int imx91_tmu_runtime_suspend(struct device *dev)
{
	struct imx91_tmu *tmu = dev_get_drvdata(dev);

	/* disable tmu */
	imx91_tmu_enable(tmu, false);

	clk_disable_unprepare(tmu->clk);

	return 0;
}

static int imx91_tmu_runtime_resume(struct device *dev)
{
	struct imx91_tmu *tmu = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(tmu->clk);
	if (ret)
		return ret;

	imx91_tmu_enable(tmu, true);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx91_tmu_pm_ops, imx91_tmu_runtime_suspend,
				 imx91_tmu_runtime_resume, NULL);

static const struct of_device_id imx91_tmu_table[] = {
	{ .compatible = "fsl,imx91-tmu", },
	{ },
};
MODULE_DEVICE_TABLE(of, imx91_tmu_table);

static struct platform_driver imx91_tmu = {
	.driver = {
		.name	= "imx91_thermal",
		.pm	= pm_ptr(&imx91_tmu_pm_ops),
		.of_match_table = imx91_tmu_table,
	},
	.probe = imx91_tmu_probe,
};
module_platform_driver(imx91_tmu);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("i.MX91 Thermal Monitor Unit driver");
MODULE_LICENSE("GPL");
