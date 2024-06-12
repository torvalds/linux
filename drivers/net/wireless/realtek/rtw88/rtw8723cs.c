// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright Fiona Klute <fiona.klute@gmx.de> */

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>
#include "main.h"
#include "rtw8703b.h"
#include "sdio.h"

static const struct sdio_device_id rtw_8723cs_id_table[] = {
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK,
			    SDIO_DEVICE_ID_REALTEK_RTW8723CS),
		.driver_data = (kernel_ulong_t)&rtw8703b_hw_spec,
	},
	{}
};
MODULE_DEVICE_TABLE(sdio, rtw_8723cs_id_table);

static struct sdio_driver rtw_8723cs_driver = {
	.name = "rtw8723cs",
	.id_table = rtw_8723cs_id_table,
	.probe = rtw_sdio_probe,
	.remove = rtw_sdio_remove,
	.drv = {
		.pm = &rtw_sdio_pm_ops,
		.shutdown = rtw_sdio_shutdown
	}};
module_sdio_driver(rtw_8723cs_driver);

MODULE_AUTHOR("Fiona Klute <fiona.klute@gmx.de>");
MODULE_DESCRIPTION("Realtek 802.11n wireless 8723cs driver");
MODULE_LICENSE("Dual BSD/GPL");
