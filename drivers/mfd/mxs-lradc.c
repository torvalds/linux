// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale MXS Low Resolution Analog-to-Digital Converter driver
 *
 * Copyright (c) 2012 DENX Software Engineering, GmbH.
 * Copyright (c) 2017 Ksenija Stanojevic <ksenija.stanojevic@gmail.com>
 *
 * Authors:
 *  Marek Vasut <marex@denx.de>
 *  Ksenija Stanojevic <ksenija.stanojevic@gmail.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mxs-lradc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define ADC_CELL		0
#define TSC_CELL		1
#define RES_MEM			0

enum mx23_lradc_irqs {
	MX23_LRADC_TS_IRQ = 0,
	MX23_LRADC_CH0_IRQ,
	MX23_LRADC_CH1_IRQ,
	MX23_LRADC_CH2_IRQ,
	MX23_LRADC_CH3_IRQ,
	MX23_LRADC_CH4_IRQ,
	MX23_LRADC_CH5_IRQ,
	MX23_LRADC_CH6_IRQ,
	MX23_LRADC_CH7_IRQ,
};

enum mx28_lradc_irqs {
	MX28_LRADC_TS_IRQ = 0,
	MX28_LRADC_TRESH0_IRQ,
	MX28_LRADC_TRESH1_IRQ,
	MX28_LRADC_CH0_IRQ,
	MX28_LRADC_CH1_IRQ,
	MX28_LRADC_CH2_IRQ,
	MX28_LRADC_CH3_IRQ,
	MX28_LRADC_CH4_IRQ,
	MX28_LRADC_CH5_IRQ,
	MX28_LRADC_CH6_IRQ,
	MX28_LRADC_CH7_IRQ,
	MX28_LRADC_BUTTON0_IRQ,
	MX28_LRADC_BUTTON1_IRQ,
};

static struct resource mx23_adc_resources[] = {
	DEFINE_RES_MEM(0x0, 0x0),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH0_IRQ, "mxs-lradc-channel0"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH1_IRQ, "mxs-lradc-channel1"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH2_IRQ, "mxs-lradc-channel2"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH3_IRQ, "mxs-lradc-channel3"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH4_IRQ, "mxs-lradc-channel4"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH5_IRQ, "mxs-lradc-channel5"),
};

static struct resource mx23_touchscreen_resources[] = {
	DEFINE_RES_MEM(0x0, 0x0),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_TS_IRQ, "mxs-lradc-touchscreen"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH6_IRQ, "mxs-lradc-channel6"),
	DEFINE_RES_IRQ_NAMED(MX23_LRADC_CH7_IRQ, "mxs-lradc-channel7"),
};

static struct resource mx28_adc_resources[] = {
	DEFINE_RES_MEM(0x0, 0x0),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_TRESH0_IRQ, "mxs-lradc-thresh0"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_TRESH1_IRQ, "mxs-lradc-thresh1"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH0_IRQ, "mxs-lradc-channel0"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH1_IRQ, "mxs-lradc-channel1"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH2_IRQ, "mxs-lradc-channel2"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH3_IRQ, "mxs-lradc-channel3"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH4_IRQ, "mxs-lradc-channel4"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH5_IRQ, "mxs-lradc-channel5"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_BUTTON0_IRQ, "mxs-lradc-button0"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_BUTTON1_IRQ, "mxs-lradc-button1"),
};

static struct resource mx28_touchscreen_resources[] = {
	DEFINE_RES_MEM(0x0, 0x0),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_TS_IRQ, "mxs-lradc-touchscreen"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH6_IRQ, "mxs-lradc-channel6"),
	DEFINE_RES_IRQ_NAMED(MX28_LRADC_CH7_IRQ, "mxs-lradc-channel7"),
};

static struct mfd_cell mx23_cells[] = {
	{
		.name = "mxs-lradc-adc",
		.resources = mx23_adc_resources,
		.num_resources = ARRAY_SIZE(mx23_adc_resources),
	},
	{
		.name = "mxs-lradc-ts",
		.resources = mx23_touchscreen_resources,
		.num_resources = ARRAY_SIZE(mx23_touchscreen_resources),
	},
};

static struct mfd_cell mx28_cells[] = {
	{
		.name = "mxs-lradc-adc",
		.resources = mx28_adc_resources,
		.num_resources = ARRAY_SIZE(mx28_adc_resources),
	},
	{
		.name = "mxs-lradc-ts",
		.resources = mx28_touchscreen_resources,
		.num_resources = ARRAY_SIZE(mx28_touchscreen_resources),
	}
};

static const struct of_device_id mxs_lradc_dt_ids[] = {
	{ .compatible = "fsl,imx23-lradc", .data = (void *)IMX23_LRADC, },
	{ .compatible = "fsl,imx28-lradc", .data = (void *)IMX28_LRADC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_lradc_dt_ids);

static int mxs_lradc_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mxs_lradc *lradc;
	struct mfd_cell *cells = NULL;
	struct resource *res;
	int ret = 0;
	u32 ts_wires = 0;

	lradc = devm_kzalloc(&pdev->dev, sizeof(*lradc), GFP_KERNEL);
	if (!lradc)
		return -ENOMEM;

	of_id = of_match_device(mxs_lradc_dt_ids, &pdev->dev);
	if (!of_id)
		return -EINVAL;

	lradc->soc = (uintptr_t)of_id->data;

	lradc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lradc->clk)) {
		dev_err(dev, "Failed to get the delay unit clock\n");
		return PTR_ERR(lradc->clk);
	}

	ret = clk_prepare_enable(lradc->clk);
	if (ret) {
		dev_err(dev, "Failed to enable the delay unit clock\n");
		return ret;
	}

	ret = of_property_read_u32(node, "fsl,lradc-touchscreen-wires",
					 &ts_wires);

	if (!ret) {
		lradc->buffer_vchans = BUFFER_VCHANS_LIMITED;

		switch (ts_wires) {
		case 4:
			lradc->touchscreen_wire = MXS_LRADC_TOUCHSCREEN_4WIRE;
			break;
		case 5:
			if (lradc->soc == IMX28_LRADC) {
				lradc->touchscreen_wire =
					MXS_LRADC_TOUCHSCREEN_5WIRE;
				break;
			}
			fallthrough;	/* to an error message for i.MX23 */
		default:
			dev_err(&pdev->dev,
				"Unsupported number of touchscreen wires (%d)\n"
				, ts_wires);
			ret = -EINVAL;
			goto err_clk;
		}
	} else {
		lradc->buffer_vchans = BUFFER_VCHANS_ALL;
	}

	platform_set_drvdata(pdev, lradc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOMEM;
		goto err_clk;
	}

	switch (lradc->soc) {
	case IMX23_LRADC:
		mx23_adc_resources[RES_MEM] = *res;
		mx23_touchscreen_resources[RES_MEM] = *res;
		cells = mx23_cells;
		break;
	case IMX28_LRADC:
		mx28_adc_resources[RES_MEM] = *res;
		mx28_touchscreen_resources[RES_MEM] = *res;
		cells = mx28_cells;
		break;
	default:
		dev_err(dev, "Unsupported SoC\n");
		ret = -ENODEV;
		goto err_clk;
	}

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   &cells[ADC_CELL], 1, NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add the ADC subdevice\n");
		goto err_clk;
	}

	if (!lradc->touchscreen_wire)
		return 0;

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   &cells[TSC_CELL], 1, NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to add the touchscreen subdevice\n");
		goto err_clk;
	}

	return 0;

err_clk:
	clk_disable_unprepare(lradc->clk);

	return ret;
}

static int mxs_lradc_remove(struct platform_device *pdev)
{
	struct mxs_lradc *lradc = platform_get_drvdata(pdev);

	clk_disable_unprepare(lradc->clk);

	return 0;
}

static struct platform_driver mxs_lradc_driver = {
	.driver = {
		.name = "mxs-lradc",
		.of_match_table = mxs_lradc_dt_ids,
	},
	.probe = mxs_lradc_probe,
	.remove = mxs_lradc_remove,
};
module_platform_driver(mxs_lradc_driver);

MODULE_AUTHOR("Ksenija Stanojevic <ksenija.stanojevic@gmail.com>");
MODULE_DESCRIPTION("Freescale i.MX23/i.MX28 LRADC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-lradc");
