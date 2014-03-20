/*
 * st_spi_fsm.c	- ST Fast Sequence Mode (FSM) Serial Flash Controller
 *
 * Author: Angus Clark <angus.clark@st.com>
 *
 * Copyright (C) 2010-2014 STicroelectronics Limited
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

struct stfsm {
	struct device		*dev;
	void __iomem		*base;
	struct resource		*region;
	struct mtd_info		mtd;
	struct mutex		lock;
};

static int stfsm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct stfsm *fsm;

	if (!np) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	fsm = devm_kzalloc(&pdev->dev, sizeof(*fsm), GFP_KERNEL);
	if (!fsm)
		return -ENOMEM;

	fsm->dev = &pdev->dev;

	platform_set_drvdata(pdev, fsm);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Resource not found\n");
		return -ENODEV;
	}

	fsm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fsm->base)) {
		dev_err(&pdev->dev,
			"Failed to reserve memory region %pR\n", res);
		return PTR_ERR(fsm->base);
	}

	mutex_init(&fsm->lock);

	fsm->mtd.dev.parent	= &pdev->dev;
	fsm->mtd.type		= MTD_NORFLASH;
	fsm->mtd.writesize	= 4;
	fsm->mtd.writebufsize	= fsm->mtd.writesize;
	fsm->mtd.flags		= MTD_CAP_NORFLASH;

	return mtd_device_parse_register(&fsm->mtd, NULL, NULL, NULL, 0);
}

static int stfsm_remove(struct platform_device *pdev)
{
	struct stfsm *fsm = platform_get_drvdata(pdev);
	int err;

	err = mtd_device_unregister(&fsm->mtd);
	if (err)
		return err;

	return 0;
}

static struct of_device_id stfsm_match[] = {
	{ .compatible = "st,spi-fsm", },
	{},
};
MODULE_DEVICE_TABLE(of, stfsm_match);

static struct platform_driver stfsm_driver = {
	.probe		= stfsm_probe,
	.remove		= stfsm_remove,
	.driver		= {
		.name	= "st-spi-fsm",
		.owner	= THIS_MODULE,
		.of_match_table = stfsm_match,
	},
};
module_platform_driver(stfsm_driver);

MODULE_AUTHOR("Angus Clark <angus.clark@st.com>");
MODULE_DESCRIPTION("ST SPI FSM driver");
MODULE_LICENSE("GPL");
