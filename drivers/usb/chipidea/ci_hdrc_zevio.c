/*
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * Based off drivers/usb/chipidea/ci_hdrc_msm.c
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/gadget.h>
#include <linux/usb/chipidea.h>

#include "ci.h"

static struct ci_hdrc_platform_data ci_hdrc_zevio_platdata = {
	.name			= "ci_hdrc_zevio",
	.flags			= CI_HDRC_REGS_SHARED,
	.capoffset		= DEF_CAPOFFSET,
};

static int ci_hdrc_zevio_probe(struct platform_device *pdev)
{
	struct platform_device *ci_pdev;

	dev_dbg(&pdev->dev, "ci_hdrc_zevio_probe\n");

	ci_pdev = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci_hdrc_zevio_platdata);

	if (IS_ERR(ci_pdev)) {
		dev_err(&pdev->dev, "ci_hdrc_add_device failed!\n");
		return PTR_ERR(ci_pdev);
	}

	platform_set_drvdata(pdev, ci_pdev);

	return 0;
}

static int ci_hdrc_zevio_remove(struct platform_device *pdev)
{
	struct platform_device *ci_pdev = platform_get_drvdata(pdev);

	ci_hdrc_remove_device(ci_pdev);

	return 0;
}

static const struct of_device_id ci_hdrc_zevio_dt_ids[] = {
	{ .compatible = "lsi,zevio-usb", },
	{ /* sentinel */ }
};

static struct platform_driver ci_hdrc_zevio_driver = {
	.probe = ci_hdrc_zevio_probe,
	.remove = ci_hdrc_zevio_remove,
	.driver = {
		.name = "zevio_usb",
		.owner = THIS_MODULE,
		.of_match_table = ci_hdrc_zevio_dt_ids,
	},
};

MODULE_DEVICE_TABLE(of, ci_hdrc_zevio_dt_ids);
module_platform_driver(ci_hdrc_zevio_driver);

MODULE_LICENSE("GPL v2");
