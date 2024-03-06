// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>
#include "main.h"
#include "rtw8723d.h"
#include "sdio.h"

static const struct sdio_device_id rtw_8723ds_id_table[] =  {
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK,
			    SDIO_DEVICE_ID_REALTEK_RTW8723DS_1ANT),
		.driver_data = (kernel_ulong_t)&rtw8723d_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK,
			    SDIO_DEVICE_ID_REALTEK_RTW8723DS_2ANT),
		.driver_data = (kernel_ulong_t)&rtw8723d_hw_spec,
	},
	{}
};
MODULE_DEVICE_TABLE(sdio, rtw_8723ds_id_table);

static struct sdio_driver rtw_8723ds_driver = {
	.name = "rtw_8723ds",
	.probe = rtw_sdio_probe,
	.remove = rtw_sdio_remove,
	.id_table = rtw_8723ds_id_table,
	.drv = {
		.pm = &rtw_sdio_pm_ops,
		.shutdown = rtw_sdio_shutdown,
	}
};
module_sdio_driver(rtw_8723ds_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Realtek 802.11n wireless 8723ds driver");
MODULE_LICENSE("Dual BSD/GPL");
