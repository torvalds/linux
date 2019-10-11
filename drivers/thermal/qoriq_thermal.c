// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2016 Freescale Semiconductor, Inc.

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/thermal.h>

#include "thermal_core.h"

#define SITES_MAX		16
#define TMR_DISABLE		0x0
#define TMR_ME			0x80000000
#define TMR_ALPF		0x0c000000
#define TMR_ALPF_V2		0x03000000
#define TMTMIR_DEFAULT	0x0000000f
#define TIER_DISABLE	0x0
#define TEUMR0_V2		0x51009c00
#define TMU_VER1		0x1
#define TMU_VER2		0x2

/*
 * QorIQ TMU Registers
 */
struct qoriq_tmu_site_regs {
	u32 tritsr;		/* Immediate Temperature Site Register */
	u32 tratsr;		/* Average Temperature Site Register */
	u8 res0[0x8];
};

struct qoriq_tmu_regs_v1 {
	u32 tmr;		/* Mode Register */
	u32 tsr;		/* Status Register */
	u32 tmtmir;		/* Temperature measurement interval Register */
	u8 res0[0x14];
	u32 tier;		/* Interrupt Enable Register */
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
	u32 ttrcr[4];		/* Temperature Range Control Register */
};

struct qoriq_tmu_regs_v2 {
	u32 tmr;		/* Mode Register */
	u32 tsr;		/* Status Register */
	u32 tmsr;		/* monitor site register */
	u32 tmtmir;		/* Temperature measurement interval Register */
	u8 res0[0x10];
	u32 tier;		/* Interrupt Enable Register */
	u32 tidr;		/* Interrupt Detect Register */
	u8 res1[0x8];
	u32 tiiscr;		/* interrupt immediate site capture register */
	u32 tiascr;		/* interrupt average site capture register */
	u32 ticscr;		/* Interrupt Critical Site Capture Register */
	u32 res2;
	u32 tmhtcr;		/* monitor high temperature capture register */
	u32 tmltcr;		/* monitor low temperature capture register */
	u32 tmrtrcr;	/* monitor rising temperature rate capture register */
	u32 tmftrcr;	/* monitor falling temperature rate capture register */
	u32 tmhtitr;	/* High Temperature Immediate Threshold */
	u32 tmhtatr;	/* High Temperature Average Threshold */
	u32 tmhtactr;	/* High Temperature Average Crit Threshold */
	u32 res3;
	u32 tmltitr;	/* monitor low temperature immediate threshold */
	u32 tmltatr;	/* monitor low temperature average threshold register */
	u32 tmltactr;	/* monitor low temperature average critical threshold */
	u32 res4;
	u32 tmrtrctr;	/* monitor rising temperature rate critical threshold */
	u32 tmftrctr;	/* monitor falling temperature rate critical threshold*/
	u8 res5[0x8];
	u32 ttcfgr;	/* Temperature Configuration Register */
	u32 tscfgr;	/* Sensor Configuration Register */
	u8 res6[0x78];
	struct qoriq_tmu_site_regs site[SITES_MAX];
	u8 res7[0x9f8];
	u32 ipbrr0;		/* IP Block Revision Register 0 */
	u32 ipbrr1;		/* IP Block Revision Register 1 */
	u8 res8[0x300];
	u32 teumr0;
	u32 teumr1;
	u32 teumr2;
	u32 res9;
	u32 ttrcr[4];	/* Temperature Range Control Register */
};

struct qoriq_tmu_data;

/*
 * Thermal zone data
 */
struct qoriq_sensor {
	struct thermal_zone_device	*tzd;
	struct qoriq_tmu_data		*qdata;
	int				id;
};

struct qoriq_tmu_data {
	int ver;
	struct qoriq_tmu_regs_v1 __iomem *regs;
	struct qoriq_tmu_regs_v2 __iomem *regs_v2;
	struct clk *clk;
	bool little_endian;
	struct qoriq_sensor	*sensor[SITES_MAX];
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
	struct qoriq_sensor *qsensor = p;
	struct qoriq_tmu_data *qdata = qsensor->qdata;
	u32 val;

	val = tmu_read(qdata, &qdata->regs->site[qsensor->id].tritsr);
	*temp = (val & 0xff) * 1000;

	return 0;
}

static const struct thermal_zone_of_device_ops tmu_tz_ops = {
	.get_temp = tmu_get_temp,
};

static int qoriq_tmu_register_tmu_zone(struct platform_device *pdev)
{
	struct qoriq_tmu_data *qdata = platform_get_drvdata(pdev);
	int id, sites = 0;

	for (id = 0; id < SITES_MAX; id++) {
		qdata->sensor[id] = devm_kzalloc(&pdev->dev,
				sizeof(struct qoriq_sensor), GFP_KERNEL);
		if (!qdata->sensor[id])
			return -ENOMEM;

		qdata->sensor[id]->id = id;
		qdata->sensor[id]->qdata = qdata;
		qdata->sensor[id]->tzd = devm_thermal_zone_of_sensor_register(
				&pdev->dev, id, qdata->sensor[id], &tmu_tz_ops);
		if (IS_ERR(qdata->sensor[id]->tzd)) {
			if (PTR_ERR(qdata->sensor[id]->tzd) == -ENODEV)
				continue;
			else
				return PTR_ERR(qdata->sensor[id]->tzd);
		}

		if (qdata->ver == TMU_VER1)
			sites |= 0x1 << (15 - id);
		else
			sites |= 0x1 << id;
	}

	/* Enable monitoring */
	if (sites != 0) {
		if (qdata->ver == TMU_VER1) {
			tmu_write(qdata, sites | TMR_ME | TMR_ALPF,
					&qdata->regs->tmr);
		} else {
			tmu_write(qdata, sites, &qdata->regs_v2->tmsr);
			tmu_write(qdata, TMR_ME | TMR_ALPF_V2,
					&qdata->regs_v2->tmr);
		}
	}

	return 0;
}

static int qoriq_tmu_calibration(struct platform_device *pdev)
{
	int i, val, len;
	u32 range[4];
	const u32 *calibration;
	struct device_node *np = pdev->dev.of_node;
	struct qoriq_tmu_data *data = platform_get_drvdata(pdev);

	len = of_property_count_u32_elems(np, "fsl,tmu-range");
	if (len < 0 || len > 4) {
		dev_err(&pdev->dev, "invalid range data.\n");
		return len;
	}

	val = of_property_read_u32_array(np, "fsl,tmu-range", range, len);
	if (val != 0) {
		dev_err(&pdev->dev, "failed to read range data.\n");
		return val;
	}

	/* Init temperature range registers */
	for (i = 0; i < len; i++)
		tmu_write(data, range[i], &data->regs->ttrcr[i]);

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
	if (data->ver == TMU_VER1) {
		tmu_write(data, TMTMIR_DEFAULT, &data->regs->tmtmir);
	} else {
		tmu_write(data, TMTMIR_DEFAULT, &data->regs_v2->tmtmir);
		tmu_write(data, TEUMR0_V2, &data->regs_v2->teumr0);
	}

	/* Disable monitoring */
	tmu_write(data, TMR_DISABLE, &data->regs->tmr);
}

static int qoriq_tmu_probe(struct platform_device *pdev)
{
	int ret;
	u32 ver;
	struct qoriq_tmu_data *data;
	struct device_node *np = pdev->dev.of_node;

	data = devm_kzalloc(&pdev->dev, sizeof(struct qoriq_tmu_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->little_endian = of_property_read_bool(np, "little-endian");

	data->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->regs)) {
		dev_err(&pdev->dev, "Failed to get memory region\n");
		return PTR_ERR(data->regs);
	}

	data->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(data->clk))
		return PTR_ERR(data->clk);

	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable clock\n");
		return ret;
	}

	/* version register offset at: 0xbf8 on both v1 and v2 */
	ver = tmu_read(data, &data->regs->ipbrr0);
	data->ver = (ver >> 8) & 0xff;
	if (data->ver == TMU_VER2)
		data->regs_v2 = (void __iomem *)data->regs;

	qoriq_tmu_init_device(data);	/* TMU initialization */

	ret = qoriq_tmu_calibration(pdev);	/* TMU calibration */
	if (ret < 0)
		goto err;

	ret = qoriq_tmu_register_tmu_zone(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register sensors\n");
		ret = -ENODEV;
		goto err;
	}

	return 0;

err:
	clk_disable_unprepare(data->clk);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int qoriq_tmu_remove(struct platform_device *pdev)
{
	struct qoriq_tmu_data *data = platform_get_drvdata(pdev);

	/* Disable monitoring */
	tmu_write(data, TMR_DISABLE, &data->regs->tmr);

	clk_disable_unprepare(data->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __maybe_unused qoriq_tmu_suspend(struct device *dev)
{
	u32 tmr;
	struct qoriq_tmu_data *data = dev_get_drvdata(dev);

	/* Disable monitoring */
	tmr = tmu_read(data, &data->regs->tmr);
	tmr &= ~TMR_ME;
	tmu_write(data, tmr, &data->regs->tmr);

	clk_disable_unprepare(data->clk);

	return 0;
}

static int __maybe_unused qoriq_tmu_resume(struct device *dev)
{
	u32 tmr;
	int ret;
	struct qoriq_tmu_data *data = dev_get_drvdata(dev);

	ret = clk_prepare_enable(data->clk);
	if (ret)
		return ret;

	/* Enable monitoring */
	tmr = tmu_read(data, &data->regs->tmr);
	tmr |= TMR_ME;
	tmu_write(data, tmr, &data->regs->tmr);

	return 0;
}

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
