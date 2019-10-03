// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

struct thermal_mmio {
	void __iomem *mmio_base;
	u32 (*read_mmio)(void __iomem *mmio_base);
	u32 mask;
	int factor;
};

static u32 thermal_mmio_readb(void __iomem *mmio_base)
{
	return readb(mmio_base);
}

static int thermal_mmio_get_temperature(void *private, int *temp)
{
	int t;
	struct thermal_mmio *sensor =
		(struct thermal_mmio *)private;

	t = sensor->read_mmio(sensor->mmio_base) & sensor->mask;
	t *= sensor->factor;

	*temp = t;

	return 0;
}

static struct thermal_zone_of_device_ops thermal_mmio_ops = {
	.get_temp = thermal_mmio_get_temperature,
};

static int thermal_mmio_probe(struct platform_device *pdev)
{
	struct resource *resource;
	struct thermal_mmio *sensor;
	int (*sensor_init_func)(struct platform_device *pdev,
				struct thermal_mmio *sensor);
	struct thermal_zone_device *thermal_zone;
	int ret;
	int temperature;

	sensor = devm_kzalloc(&pdev->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sensor->mmio_base = devm_ioremap_resource(&pdev->dev, resource);
	if (IS_ERR(sensor->mmio_base)) {
		dev_err(&pdev->dev, "failed to ioremap memory (%ld)\n",
			PTR_ERR(sensor->mmio_base));
		return PTR_ERR(sensor->mmio_base);
	}

	sensor_init_func = device_get_match_data(&pdev->dev);
	if (sensor_init_func) {
		ret = sensor_init_func(pdev, sensor);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to initialize sensor (%d)\n",
				ret);
			return ret;
		}
	}

	thermal_zone = devm_thermal_zone_of_sensor_register(&pdev->dev,
							    0,
							    sensor,
							    &thermal_mmio_ops);
	if (IS_ERR(thermal_zone)) {
		dev_err(&pdev->dev,
			"failed to register sensor (%ld)\n",
			PTR_ERR(thermal_zone));
		return PTR_ERR(thermal_zone);
	}

	thermal_mmio_get_temperature(sensor, &temperature);
	dev_info(&pdev->dev,
		 "thermal mmio sensor %s registered, current temperature: %d\n",
		 pdev->name, temperature);

	return 0;
}

static int al_thermal_init(struct platform_device *pdev,
			   struct thermal_mmio *sensor)
{
	sensor->read_mmio = thermal_mmio_readb;
	sensor->mask = 0xff;
	sensor->factor = 1000;

	return 0;
}

static const struct of_device_id thermal_mmio_id_table[] = {
	{ .compatible = "amazon,al-thermal", .data = al_thermal_init},
	{}
};
MODULE_DEVICE_TABLE(of, thermal_mmio_id_table);

static struct platform_driver thermal_mmio_driver = {
	.probe = thermal_mmio_probe,
	.driver = {
		.name = "thermal-mmio",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(thermal_mmio_id_table),
	},
};

module_platform_driver(thermal_mmio_driver);

MODULE_AUTHOR("Talel Shenhar <talel@amazon.com>");
MODULE_DESCRIPTION("Thermal MMIO Driver");
MODULE_LICENSE("GPL v2");
