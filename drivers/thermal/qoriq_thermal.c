// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2016 Freescale Semiconductor, Inc.

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/thermal.h>

#include "thermal_core.h"

#define SITES_MAX	16

/*
 * QorIQ TMU Registers
 */
struct qoriq_tmu_site_regs {
	u32 tritsr;		/* Immediate Temperature Site Register */
	u32 tratsr;		/* Average Temperature Site Register */
	u8 res0[0x8];
};

struct qoriq_tmu_regs {
	u32 tmr;		/* Mode Register */
#define TMR_DISABLE	0x0
#define TMR_ME		0x80000000
#define TMR_ALPF	0x0c000000
	u32 tsr;		/* Status Register */
	u32 tmtmir;		/* Temperature measurement interval Register */
#define TMTMIR_DEFAULT	0x0000000f
	u8 res0[0x14];
	u32 tier;		/* Interrupt Enable Register */
#define TIER_DISABLE	0x0
	u32 tidr;		/* Interrupt Detect Register */
	u32 tiscr;		/* Interrupt Site Capture Register */
	u32 ticscr;		/* Interrupt Critical Site Capture Register */
	u8 res1[0x10];
	u32 tmhtcrh;		/* High Temperature Capture Register */
	u32 tmhtcrl;		/* Low Temperature Capture Register */
	u8 res2[0x8];
	u32 tmhtitr;		/* High Temperature Immediate Threshold */
	u32 tmhtatr;		/* High Temperature Average Threshold */
	u32 tmhtactr;	/* High Temperature Average Crit Threshold */
	u8 res3[0x24];
	u32 ttcfgr;		/* Temperature Configuration Register */
	u32 tscfgr;		/* Sensor Configuration Register */
	u8 res4[0x78];
	struct qoriq_tmu_site_regs site[SITES_MAX];
	u8 res5[0x9f8];
	u32 ipbrr0;		/* IP Block Revision Register 0 */
	u32 ipbrr1;		/* IP Block Revision Register 1 */
	u8 res6[0x310];
	u32 ttr0cr;		/* Temperature Range 0 Control Register */
	u32 ttr1cr;		/* Temperature Range 1 Control Register */
	u32 ttr2cr;		/* Temperature Range 2 Control Register */
	u32 ttr3cr;		/* Temperature Range 3 Control Register */
};

/*
 * Thermal zone data
 */
struct qoriq_tmu_data {
	struct thermal_zone_device *tz;
	struct qoriq_tmu_regs __iomem *regs;
	int sensor_id;
	bool little_endian;
};

static void tmu_write(struct qoriq_tmu_data *p, u32 val, void __iomem *addr)
{
	if (p->little_endian)
		iowrite32(val, addr);
	else
		iowrite32be(val, addr);
}

static u32 tmu_read(struct qoriq_tmu_data *p, void __iomem *addr)
{
	if (p->little_endian)
		return ioread32(addr);
	else
		return ioread32be(addr);
}

static int tmu_get_temp(void *p, int *temp)
{
	u32 val;
	struct qoriq_tmu_data *data = p;

	val = tmu_read(data, &data->regs->site[data->sensor_id].tritsr);
	*temp = (val & 0xff) * 1000;

	return 0;
}

static int qoriq_tmu_get_sensor_id(void)
{
	int ret, id;
	struct of_phandle_args sensor_specs;
	struct device_node *np, *sensor_np;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return -ENODEV;

	sensor_np = of_get_next_child(np, NULL);
	ret = of_parse_phandle_with_args(sensor_np, "thermal-sensors",
			"#thermal-sensor-cells",
			0, &sensor_specs);
	if (ret) {
		of_node_put(np);
		of_node_put(sensor_np);
		return ret;
	}

	if (sensor_specs.args_count >= 1) {
		id = sensor_specs.args[0];
		WARN(sensor_specs.args_count > 1,
				"%pOFn: too many cells in sensor specifier %d\n",
				sensor_specs.np, sensor_specs.args_count);
	} else {
		id = 0;
	}

	of_node_put(np);
	of_node_put(sensor_np);

	return id;
}

static int qoriq_tmu_calibration(struct platform_device *pdev)
{
	int i, val, len;
	u32 range[4];
	const u32 *calibration;
	struct device_node *np = pdev->dev.of_node;
	struct qoriq_tmu_data *data = platform_get_drvdata(pdev);

	if (of_property_read_u32_array(np, "fsl,tmu-range", range, 4)) {
		dev_err(&pdev->dev, "missing calibration range.\n");
		return -ENODEV;
	}

	/* Init temperature range registers */
	tmu_write(data, range[0], &data->regs->ttr0cr);
	tmu_write(data, range[1], &data->regs->ttr1cr);
	tmu_write(data, range[2], &data->regs->ttr2cr);
	tmu_write(data, range[3], &data->regs->ttr3cr);

	calibration = of_get_property(np, "fsl,tmu-calibration", &len);
	if (calibration == NULL || len % 8) {
		dev_err(&pdev->dev, "invalid calibration data.\n");
		return -ENODEV;
	}

	for (i = 0; i < len; i += 8, calibration += 2) {
		val = of_read_number(calibration, 1);
		tmu_write(data, val, &data->regs->ttcfgr);
		val = of_read_number(calibration + 1, 1);
		tmu_write(data, val, &data->regs->tscfgr);
	}

	return 0;
}

static void qoriq_tmu_init_device(struct qoriq_tmu_data *data)
{
	/* Disable interrupt, using polling instead */
	tmu_write(data, TIER_DISABLE, &data->regs->tier);

	/* Set update_interval */
	tmu_write(data, TMTMIR_DEFAULT, &data->regs->tmtmir);

	/* Disable monitoring */
	tmu_write(data, TMR_DISABLE, &data->regs->tmr);
}

static const struct thermal_zone_of_device_ops tmu_tz_ops = {
	.get_temp = tmu_get_temp,
};

static int qoriq_tmu_probe(struct platform_device *pdev)
{
	int ret;
	struct qoriq_tmu_data *data;
	struct device_node *np = pdev->dev.of_node;
	u32 site;

	if (!np) {
		dev_err(&pdev->dev, "Device OF-Node is NULL");
		return -ENODEV;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct qoriq_tmu_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->little_endian = of_property_read_bool(np, "little-endian");

	data->sensor_id = qoriq_tmu_get_sensor_id();
	if (data->sensor_id < 0) {
		dev_err(&pdev->dev, "Failed to get sensor id\n");
		ret = -ENODEV;
		goto err_iomap;
	}

	data->regs = of_iomap(np, 0);
	if (!data->regs) {
		dev_err(&pdev->dev, "Failed to get memory region\n");
		ret = -ENODEV;
		goto err_iomap;
	}

	qoriq_tmu_init_device(data);	/* TMU initialization */

	ret = qoriq_tmu_calibration(pdev);	/* TMU calibration */
	if (ret < 0)
		goto err_tmu;

	data->tz = devm_thermal_zone_of_sensor_register(&pdev->dev,
							data->sensor_id,
							data, &tmu_tz_ops);
	if (IS_ERR(data->tz)) {
		ret = PTR_ERR(data->tz);
		dev_err(&pdev->dev,
			"Failed to register thermal zone device %d\n", ret);
		goto err_tmu;
	}

	/* Enable monitoring */
	site = 0x1 << (15 - data->sensor_id);
	tmu_write(data, site | TMR_ME | TMR_ALPF, &data->regs->tmr);

	return 0;

err_tmu:
	iounmap(data->regs);

err_iomap:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int qoriq_tmu_remove(struct platform_device *pdev)
{
	struct qoriq_tmu_data *data = platform_get_drvdata(pdev);

	/* Disable monitoring */
	tmu_write(data, TMR_DISABLE, &data->regs->tmr);

	iounmap(data->regs);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int qoriq_tmu_suspend(struct device *dev)
{
	u32 tmr;
	struct qoriq_tmu_data *data = dev_get_drvdata(dev);

	/* Disable monitoring */
	tmr = tmu_read(data, &data->regs->tmr);
	tmr &= ~TMR_ME;
	tmu_write(data, tmr, &data->regs->tmr);

	return 0;
}

static int qoriq_tmu_resume(struct device *dev)
{
	u32 tmr;
	struct qoriq_tmu_data *data = dev_get_drvdata(dev);

	/* Enable monitoring */
	tmr = tmu_read(data, &data->regs->tmr);
	tmr |= TMR_ME;
	tmu_write(data, tmr, &data->regs->tmr);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(qoriq_tmu_pm_ops,
			 qoriq_tmu_suspend, qoriq_tmu_resume);

static const struct of_device_id qoriq_tmu_match[] = {
	{ .compatible = "fsl,qoriq-tmu", },
	{ .compatible = "fsl,imx8mq-tmu", },
	{},
};
MODULE_DEVICE_TABLE(of, qoriq_tmu_match);

static struct platform_driver qoriq_tmu = {
	.driver	= {
		.name		= "qoriq_thermal",
		.pm		= &qoriq_tmu_pm_ops,
		.of_match_table	= qoriq_tmu_match,
	},
	.probe	= qoriq_tmu_probe,
	.remove	= qoriq_tmu_remove,
};
module_platform_driver(qoriq_tmu);

MODULE_AUTHOR("Jia Hongtao <hongtao.jia@nxp.com>");
MODULE_DESCRIPTION("QorIQ Thermal Monitoring Unit driver");
MODULE_LICENSE("GPL v2");
