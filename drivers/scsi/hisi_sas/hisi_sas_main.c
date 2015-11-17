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
#define DRV_NAME "hisi_sas"

static struct scsi_transport_template *hisi_sas_stt;

static struct sas_domain_function_template hisi_sas_transport_ops = {
};

static __init int hisi_sas_init(void)
{
	pr_info("hisi_sas: driver version %s\n", DRV_VERSION);

	hisi_sas_stt = sas_domain_attach_transport(&hisi_sas_transport_ops);
	if (!hisi_sas_stt)
		return -ENOMEM;

	return 0;
}

static __exit void hisi_sas_exit(void)
{
	sas_release_transport(hisi_sas_stt);
}

module_init(hisi_sas_init);
module_exit(hisi_sas_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller driver");
MODULE_ALIAS("platform:" DRV_NAME);
