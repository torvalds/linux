/*
 * ZTE's zx2967 family thermal sensor driver
 *
 * Copyright (C) 2017 ZTE Ltd.
 *
 * Author: Baoyou Xie <baoyou.xie@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

/* Power Mode: 0->low 1->high */
#define ZX2967_THERMAL_POWER_MODE	0
#define ZX2967_POWER_MODE_LOW		0
#define ZX2967_POWER_MODE_HIGH		1

/* DCF Control Register */
#define ZX2967_THERMAL_DCF		0x4
#define ZX2967_DCF_EN			BIT(1)
#define ZX2967_DCF_FREEZE		BIT(0)

/* Selection Register */
#define ZX2967_THERMAL_SEL		0x8

/* Control Register */
#define ZX2967_THERMAL_CTRL		0x10

#define ZX2967_THERMAL_READY		BIT(12)
#define ZX2967_THERMAL_TEMP_MASK	GENMASK(11, 0)
#define ZX2967_THERMAL_ID_MASK		0x18
#define ZX2967_THERMAL_ID		0x10

#define ZX2967_GET_TEMP_TIMEOUT_US	(100 * 1024)

/**
 * struct zx2967_thermal_priv - zx2967 thermal sensor private structure
 * @tzd: struct thermal_zone_device where the sensor is registered
 * @lock: prevents read sensor in parallel
 * @clk_topcrm: topcrm clk structure
 * @clk_apb: apb clk structure
 * @regs: pointer to base address of the thermal sensor
 */

struct zx2967_thermal_priv {
	struct thermal_zone_device	*tzd;
	struct mutex			lock;
	struct clk			*clk_topcrm;
	struct clk			*clk_apb;
	void __iomem			*regs;
	struct device			*dev;
};

static int zx2967_thermal_get_temp(void *data, int *temp)
{
	void __iomem *regs;
	struct zx2967_thermal_priv *priv = data;
	u32 val;
	int ret;

	if (!priv->tzd)
		return -EAGAIN;

	regs = priv->regs;
	mutex_lock(&priv->lock);
	writel_relaxed(ZX2967_POWER_MODE_LOW,
		       regs + ZX2967_THERMAL_POWER_MODE);
	writel_relaxed(ZX2967_DCF_EN, regs + ZX2967_THERMAL_DCF);

	val = readl_relaxed(regs + ZX2967_THERMAL_SEL);
	val &= ~ZX2967_THERMAL_ID_MASK;
	val |= ZX2967_THERMAL_ID;
	writel_relaxed(val, regs + ZX2967_THERMAL_SEL);

	/*
	 * Must wait for a while, surely it's a bit odd.
	 * otherwise temperature value we got has a few deviation, even if
	 * the THERMAL_READY bit is set.
	 */
	usleep_range(100, 300);
	ret = readx_poll_timeout(readl, regs + ZX2967_THERMAL_CTRL,
				 val, val & ZX2967_THERMAL_READY, 300,
				 ZX2967_GET_TEMP_TIMEOUT_US);
	if (ret) {
		dev_err(priv->dev, "Thermal sensor data timeout\n");
		goto unlock;
	}

	writel_relaxed(ZX2967_DCF_FREEZE | ZX2967_DCF_EN,
		       regs + ZX2967_THERMAL_DCF);
	val = readl_relaxed(regs + ZX2967_THERMAL_CTRL)
			 & ZX2967_THERMAL_TEMP_MASK;
	writel_relaxed(ZX2967_POWER_MODE_HIGH,
		       regs + ZX2967_THERMAL_POWER_MODE);

	/*
	 * Calculate temperature
	 * In dts, slope is multiplied by 1000.
	 */
	*temp = DIV_ROUND_CLOSEST(((s32)val + priv->tzd->tzp->offset) * 1000,
				  priv->tzd->tzp->slope);

unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static const struct thermal_zone_of_device_ops zx2967_of_thermal_ops = {
	.get_temp = zx2967_thermal_get_temp,
};

static int zx2967_thermal_probe(struct platform_device *pdev)
{
	struct zx2967_thermal_priv *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	priv->clk_topcrm = devm_clk_get(&pdev->dev, "topcrm");
	if (IS_ERR(priv->clk_topcrm)) {
		ret = PTR_ERR(priv->clk_topcrm);
		dev_err(&pdev->dev, "failed to get topcrm clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->clk_topcrm);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable topcrm clock: %d\n",
			ret);
		return ret;
	}

	priv->clk_apb = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(priv->clk_apb)) {
		ret = PTR_ERR(priv->clk_apb);
		dev_err(&pdev->dev, "failed to get apb clock: %d\n", ret);
		goto disable_clk_topcrm;
	}

	ret = clk_prepare_enable(priv->clk_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable apb clock: %d\n",
			ret);
		goto disable_clk_topcrm;
	}

	mutex_init(&priv->lock);
	priv->tzd = thermal_zone_of_sensor_register(&pdev->dev,
					0, priv, &zx2967_of_thermal_ops);

	if (IS_ERR(priv->tzd)) {
		ret = PTR_ERR(priv->tzd);
		dev_err(&pdev->dev, "failed to register sensor: %d\n", ret);
		goto disable_clk_all;
	}

	if (priv->tzd->tzp->slope == 0) {
		thermal_zone_of_sensor_unregister(&pdev->dev, priv->tzd);
		dev_err(&pdev->dev, "coefficients of sensor is invalid\n");
		ret = -EINVAL;
		goto disable_clk_all;
	}

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	return 0;

disable_clk_all:
	clk_disable_unprepare(priv->clk_apb);
disable_clk_topcrm:
	clk_disable_unprepare(priv->clk_topcrm);
	return ret;
}

static int zx2967_thermal_exit(struct platform_device *pdev)
{
	struct zx2967_thermal_priv *priv = platform_get_drvdata(pdev);

	thermal_zone_of_sensor_unregister(&pdev->dev, priv->tzd);
	clk_disable_unprepare(priv->clk_topcrm);
	clk_disable_unprepare(priv->clk_apb);

	return 0;
}

static const struct of_device_id zx2967_thermal_id_table[] = {
	{ .compatible = "zte,zx296718-thermal" },
	{}
};
MODULE_DEVICE_TABLE(of, zx2967_thermal_id_table);

#ifdef CONFIG_PM_SLEEP
static int zx2967_thermal_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zx2967_thermal_priv *priv = platform_get_drvdata(pdev);

	if (priv && priv->clk_topcrm)
		clk_disable_unprepare(priv->clk_topcrm);

	if (priv && priv->clk_apb)
		clk_disable_unprepare(priv->clk_apb);

	return 0;
}

static int zx2967_thermal_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct zx2967_thermal_priv *priv = platform_get_drvdata(pdev);
	int error;

	error = clk_prepare_enable(priv->clk_topcrm);
	if (error)
		return error;

	error = clk_prepare_enable(priv->clk_apb);
	if (error) {
		clk_disable_unprepare(priv->clk_topcrm);
		return error;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(zx2967_thermal_pm_ops,
			 zx2967_thermal_suspend, zx2967_thermal_resume);

static struct platform_driver zx2967_thermal_driver = {
	.probe = zx2967_thermal_probe,
	.remove = zx2967_thermal_exit,
	.driver = {
		.name = "zx2967_thermal",
		.of_match_table = zx2967_thermal_id_table,
		.pm = &zx2967_thermal_pm_ops,
	},
};
module_platform_driver(zx2967_thermal_driver);

MODULE_AUTHOR("Baoyou Xie <baoyou.xie@linaro.org>");
MODULE_DESCRIPTION("ZTE zx2967 thermal driver");
MODULE_LICENSE("GPL v2");
