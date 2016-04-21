#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>

/*
 * According to a data sheet draft, "this temperature sensor uses a bandgap
 * type of circuit to compare a voltage which has a negative temperature
 * coefficient with a voltage that is proportional to absolute temperature.
 * A resistor bank allows 41 different temperature thresholds to be selected
 * and the logic output will then indicate whether the actual die temperature
 * lies above or below the selected threshold."
 */

#define TEMPSI_CMD	0
#define TEMPSI_RES	4
#define TEMPSI_CFG	8

#define CMD_OFF		0
#define CMD_ON		1
#define CMD_READ	2

#define IDX_MIN		15
#define IDX_MAX		40

struct tango_thermal_priv {
	void __iomem *base;
	int thresh_idx;
};

static bool temp_above_thresh(void __iomem *base, int thresh_idx)
{
	writel(CMD_READ | thresh_idx << 8, base + TEMPSI_CMD);
	usleep_range(10, 20);
	writel(CMD_READ | thresh_idx << 8, base + TEMPSI_CMD);

	return readl(base + TEMPSI_RES);
}

static int tango_get_temp(void *arg, int *res)
{
	struct tango_thermal_priv *priv = arg;
	int idx = priv->thresh_idx;

	if (temp_above_thresh(priv->base, idx)) {
		/* Search upward by incrementing thresh_idx */
		while (idx < IDX_MAX && temp_above_thresh(priv->base, ++idx))
			cpu_relax();
		idx = idx - 1; /* always return lower bound */
	} else {
		/* Search downward by decrementing thresh_idx */
		while (idx > IDX_MIN && !temp_above_thresh(priv->base, --idx))
			cpu_relax();
	}

	*res = (idx * 9 / 2 - 38) * 1000; /* millidegrees Celsius */
	priv->thresh_idx = idx;

	return 0;
}

static const struct thermal_zone_of_device_ops ops = {
	.get_temp	= tango_get_temp,
};

static int tango_thermal_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct tango_thermal_priv *priv;
	struct thermal_zone_device *tzdev;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->thresh_idx = IDX_MIN;
	writel(CMD_ON, priv->base + TEMPSI_CMD);

	tzdev = devm_thermal_zone_of_sensor_register(&pdev->dev, 0, priv, &ops);
	return PTR_ERR_OR_ZERO(tzdev);
}

static const struct of_device_id tango_sensor_ids[] = {
	{
		.compatible = "sigma,smp8758-thermal",
	},
	{ /* sentinel */ }
};

static struct platform_driver tango_thermal_driver = {
	.probe	= tango_thermal_probe,
	.driver	= {
		.name		= "tango-thermal",
		.of_match_table	= tango_sensor_ids,
	},
};

module_platform_driver(tango_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sigma Designs");
MODULE_DESCRIPTION("Tango temperature sensor");
