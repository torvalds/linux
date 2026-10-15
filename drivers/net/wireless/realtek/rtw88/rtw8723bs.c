// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 * Copyright(c) Michael Straube <straube.linux@gmail.com>
 * Copyright(c) 2024-2026 Luka Gejak <luka.gejak@linux.dev>
 */

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>
#include "main.h"
#include "rtw8723b.h"
#include "sdio.h"

static const struct sdio_device_id rtw_8723bs_id_table[] = {
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK,
			    SDIO_DEVICE_ID_REALTEK_RTW8723BS),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, 0x0523),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, 0x0525),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, 0x0623),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, 0x0626),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{
		SDIO_DEVICE(SDIO_VENDOR_ID_REALTEK, 0x0627),
		.driver_data = (kernel_ulong_t)&rtw8723b_hw_spec,
	},
	{}
};
MODULE_DEVICE_TABLE(sdio, rtw_8723bs_id_table);

static struct sdio_driver rtw_8723bs_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8723bs_id_table,
	.probe = rtw_sdio_probe,
	.remove = rtw_sdio_remove,
	.shutdown = rtw_sdio_shutdown,
	.drv = {
		.pm = &rtw_sdio_pm_ops,
	}
};
module_sdio_driver(rtw_8723bs_driver);

MODULE_AUTHOR("Luka Gejak <luka.gejak@linux.dev>");
MODULE_AUTHOR("Michael Straube <straube.linux@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11n wireless 8723bs driver");
MODULE_LICENSE("Dual BSD/GPL");
