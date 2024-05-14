// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Broadcom
 */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

/*
 * In stingray thermal IO memory,
 * Total Number of available TMONs MASK is at offset 0
 * temperature registers BASE is at 4 byte offset.
 * Each TMON temperature register size is 4.
 */
#define SR_TMON_TEMP_BASE(id)   ((id) * 0x4)

#define SR_TMON_MAX_LIST        6

struct sr_tmon {
	unsigned int crit_temp;
	unsigned int tmon_id;
	struct sr_thermal *priv;
};

struct sr_thermal {
	void __iomem *regs;
	unsigned int max_crit_temp;
	struct sr_tmon tmon[SR_TMON_MAX_LIST];
};

static int sr_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct sr_tmon *tmon = tz->devdata;
	struct sr_thermal *sr_thermal = tmon->priv;

	*temp = readl(sr_thermal->regs + SR_TMON_TEMP_BASE(tmon->tmon_id));

	return 0;
}

static const struct thermal_zone_device_ops sr_tz_ops = {
	.get_temp = sr_get_temp,
};

static int sr_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_zone_device *tz;
	struct sr_thermal *sr_thermal;
	struct sr_tmon *tmon;
	struct resource *res;
	u32 sr_tmon_list = 0;
	unsigned int i;
	int ret;

	sr_thermal = devm_kzalloc(dev, sizeof(*sr_thermal), GFP_KERNEL);
	if (!sr_thermal)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	sr_thermal->regs = (void __iomem *)devm_memremap(&pdev->dev, res->start,
							 resource_size(res),
							 MEMREMAP_WB);
	if (IS_ERR(sr_thermal->regs)) {
		dev_err(dev, "failed to get io address\n");
		return PTR_ERR(sr_thermal->regs);
	}

	ret = device_property_read_u32(dev, "brcm,tmon-mask", &sr_tmon_list);
	if (ret)
		return ret;

	tmon = sr_thermal->tmon;
	for (i = 0; i < SR_TMON_MAX_LIST; i++, tmon++) {
		if (!(sr_tmon_list & BIT(i)))
			continue;

		/* Flush temperature registers */
		writel(0, sr_thermal->regs + SR_TMON_TEMP_BASE(i));
		tmon->tmon_id = i;
		tmon->priv = sr_thermal;
		tz = devm_thermal_of_zone_register(dev, i, tmon,
						   &sr_tz_ops);
		if (IS_ERR(tz))
			return PTR_ERR(tz);

		dev_dbg(dev, "thermal sensor %d registered\n", i);
	}
	platform_set_drvdata(pdev, sr_thermal);

	return 0;
}

static const struct of_device_id sr_thermal_of_match[] = {
	{ .compatible = "brcm,sr-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, sr_thermal_of_match);

static struct platform_driver sr_thermal_driver = {
	.probe		= sr_thermal_probe,
	.driver = {
		.name = "sr-thermal",
		.of_match_table = sr_thermal_of_match,
	},
};
module_platform_driver(sr_thermal_driver);

MODULE_AUTHOR("Pramod Kumar <pramod.kumar@broadcom.com>");
MODULE_DESCRIPTION("Stingray thermal driver");
MODULE_LICENSE("GPL v2");
