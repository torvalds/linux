/*
 * Copyright (c) 2015 Linaro Ltd.
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v1_hw"


struct hisi_sas_complete_v1_hdr {
	__le32 data;
};
static const struct hisi_sas_hw hisi_sas_v1_hw = {
	.complete_hdr_size = sizeof(struct hisi_sas_complete_v1_hdr),
};

static int hisi_sas_v1_probe(struct platform_device *pdev)
{
	return hisi_sas_probe(pdev, &hisi_sas_v1_hw);
}

static int hisi_sas_v1_remove(struct platform_device *pdev)
{
	return hisi_sas_remove(pdev);
}

static const struct of_device_id sas_v1_of_match[] = {
	{ .compatible = "hisilicon,hip05-sas-v1",},
	{},
};
MODULE_DEVICE_TABLE(of, sas_v1_of_match);

static struct platform_driver hisi_sas_v1_driver = {
	.probe = hisi_sas_v1_probe,
	.remove = hisi_sas_v1_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sas_v1_of_match,
	},
};

module_platform_driver(hisi_sas_v1_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v1 hw driver");
MODULE_ALIAS("platform:" DRV_NAME);
