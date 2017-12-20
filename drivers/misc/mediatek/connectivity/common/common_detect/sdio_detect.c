/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[SDIO-DETECT]"

#include "wmt_detect.h"

#if MTK_HIF_SDIO_AUTOK_ENABLED
#include <mt_boot.h>
#endif

unsigned int gComboChipId = -1;
struct sdio_func *g_func = NULL;

MTK_WCN_HIF_SDIO_CHIP_INFO gChipInfoArray[] = {
	/* MT6620 *//* Not an SDIO standard class device */
	{{SDIO_DEVICE(0x037A, 0x020A)}, 0x6620},	/* SDIO1:FUNC1:WIFI */
	{{SDIO_DEVICE(0x037A, 0x020B)}, 0x6620},	/* SDIO2:FUNC1:BT+FM+GPS */
	{{SDIO_DEVICE(0x037A, 0x020C)}, 0x6620},	/* 2-function (SDIO2:FUNC1:BT+FM+GPS, FUNC2:WIFI) */

	/* MT6628 *//* SDIO1: Wi-Fi, SDIO2: BGF */
	{{SDIO_DEVICE(0x037A, 0x6628)}, 0x6628},

	/* MT6630 *//* SDIO1: Wi-Fi, SDIO2: BGF */
	{{SDIO_DEVICE(0x037A, 0x6630)}, 0x6630},

};

/* Supported SDIO device table */
static const struct sdio_device_id mtk_sdio_id_tbl[] = {
	/* MT6618 *//* Not an SDIO standard class device */
	{SDIO_DEVICE(0x037A, 0x018A)},	/* SDIO1:WIFI */
	{SDIO_DEVICE(0x037A, 0x018B)},	/* SDIO2:FUNC1:BT+FM */
	{SDIO_DEVICE(0x037A, 0x018C)},	/* 2-function (SDIO2:FUNC1:BT+FM, FUNC2:WIFI) */

	/* MT6619 *//* Not an SDIO standard class device */
	{SDIO_DEVICE(0x037A, 0x6619)},	/* SDIO2:FUNC1:BT+FM+GPS */

	/* MT6620 *//* Not an SDIO standard class device */
	{SDIO_DEVICE(0x037A, 0x020A)},	/* SDIO1:FUNC1:WIFI */
	{SDIO_DEVICE(0x037A, 0x020B)},	/* SDIO2:FUNC1:BT+FM+GPS */
	{SDIO_DEVICE(0x037A, 0x020C)},	/* 2-function (SDIO2:FUNC1:BT+FM+GPS, FUNC2:WIFI) */

	/* MT5921 *//* Not an SDIO standard class device */
	{SDIO_DEVICE(0x037A, 0x5921)},

	/* MT6628 *//* SDIO1: Wi-Fi, SDIO2: BGF */
	{SDIO_DEVICE(0x037A, 0x6628)},

	/* MT6630 *//* SDIO1: Wi-Fi, SDIO2: BGF */
	{SDIO_DEVICE(0x037A, 0x6630)},
	{ /* end: all zeroes */ },
};

static int sdio_detect_probe(struct sdio_func *func, const struct sdio_device_id *id);

static void sdio_detect_remove(struct sdio_func *func);

static struct sdio_driver mtk_sdio_client_drv = {
	.name = "mtk_sdio_client",	/* MTK SDIO Client Driver */
	.id_table = mtk_sdio_id_tbl,	/* all supported struct sdio_device_id table */
	.probe = sdio_detect_probe,
	.remove = sdio_detect_remove,
};

static int hif_sdio_match_chipid_by_dev_id(const struct sdio_device_id *id);

int hif_sdio_is_chipid_valid(int chipId)
{
	int index = -1;

	int left = 0;
	int middle = 0;
	int right = sizeof(gChipInfoArray) / sizeof(gChipInfoArray[0]) - 1;

	if ((chipId < gChipInfoArray[left].chipId) || (chipId > gChipInfoArray[right].chipId))
		return index;

	middle = (left + right) / 2;

	while (left <= right) {
		if (chipId > gChipInfoArray[middle].chipId) {
			left = middle + 1;
		} else if (chipId < gChipInfoArray[middle].chipId) {
			right = middle - 1;
		} else {
			index = middle;
			break;
		}
		middle = (left + right) / 2;
	}

	if (0 > index)
		WMT_DETECT_ERR_FUNC("no supported chipid found\n");
	else
		WMT_DETECT_INFO_FUNC("index:%d, chipId:0x%x\n", index, gChipInfoArray[index].chipId);

	return index;
}

int hif_sdio_match_chipid_by_dev_id(const struct sdio_device_id *id)
{
	int maxIndex = sizeof(gChipInfoArray) / sizeof(gChipInfoArray[0]);
	int index = 0;
	struct sdio_device_id *localId = NULL;
	int chipId = -1;

	for (index = 0; index < maxIndex; index++) {
		localId = &(gChipInfoArray[index].deviceId);
		if ((localId->vendor == id->vendor) && (localId->device == id->device)) {
			chipId = gChipInfoArray[index].chipId;
			WMT_DETECT_INFO_FUNC
			    ("valid chipId found, index(%d), vendor id(0x%x), device id(0x%x), chip id(0x%x)\n", index,
			     localId->vendor, localId->device, chipId);
			gComboChipId = chipId;
			mtk_wcn_wmt_set_chipid(gComboChipId);
			break;
		}
	}
	if (0 > chipId) {
		WMT_DETECT_ERR_FUNC("No valid chipId found, vendor id(0x%x), device id(0x%x)\n", id->vendor,
				    id->device);
	}

	return chipId;
}

int sdio_detect_query_chipid(int waitFlag)
{
	unsigned int timeSlotMs = 200;
	unsigned int maxTimeSlot = 15;
	unsigned int counter = 0;
	/* gComboChipId = 0x6628; */
	if (0 == waitFlag)
		return gComboChipId;
	if (0 <= hif_sdio_is_chipid_valid(gComboChipId))
		return gComboChipId;

	while (counter < maxTimeSlot) {
		if (0 <= hif_sdio_is_chipid_valid(gComboChipId))
			break;
		msleep(timeSlotMs);
		counter++;
	}

	return gComboChipId;
}

int sdio_detect_do_autok(int chipId)
{
	int i_ret = 0;

#if MTK_HIF_SDIO_AUTOK_ENABLED
#if 0
	BOOTMODE boot_mode;

	boot_mode = get_boot_mode();

	if (boot_mode == META_BOOT) {
		WMT_DETECT_INFO_FUNC("omit autok in meta mode\n");
		return 0;
	}
#endif
	if (0x6630 == chipId) {
#ifdef CONFIG_SDIOAUTOK_SUPPORT
		if (NULL != g_func) {
			WMT_DETECT_INFO_FUNC("wait_sdio_autok_ready++\n");
			i_ret = wait_sdio_autok_ready(g_func->card->host);
			WMT_DETECT_INFO_FUNC("wait_sdio_autok_ready--\n");
			if (0 == i_ret) {
				WMT_DETECT_INFO_FUNC("wait_sdio_autok_ready return success\n");
			} else {
				WMT_DETECT_INFO_FUNC("wait_sdio_autok_ready return fail, i_ret:%d\n", i_ret);
				gComboChipId = -1;
			}
		} else {
			WMT_DETECT_INFO_FUNC("g_func NULL, omit autok\n");
		}
#else
		i_ret = 0;
		WMT_DETECT_INFO_FUNC("MTK_SDIOAUTOK_SUPPORT not defined\n");
#endif
	} else {
		WMT_DETECT_INFO_FUNC("MT%x does not support SDIO3.0 autoK is not needed\n", chipId);
	}
#else
	i_ret = 0;
	WMT_DETECT_INFO_FUNC("MTK_HIF_SDIO_AUTOK_ENABLED is not defined\n");
#endif
	return i_ret;
}

/*!
 * \brief hif_sdio probe function
 *
 * hif_sdio probe function called by mmc driver when any matched SDIO function
 * is detected by it.
 *
 * \param func
 * \param id
 *
 * \retval 0    register successfully
 * \retval < 0  list error code here
 */
static int sdio_detect_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int chipId = 0;

	WMT_DETECT_INFO_FUNC("vendor(0x%x) device(0x%x) num(0x%x)\n", func->vendor, func->device, func->num);
	chipId = hif_sdio_match_chipid_by_dev_id(id);

	if ((0x6630 == chipId) && (1 == func->num)) {
		int ret = 0;

		g_func = func;
		WMT_DETECT_INFO_FUNC("autok function detected, func:0x%p\n", g_func);

		sdio_claim_host(func);
		ret = sdio_enable_func(func);
		sdio_release_host(func);
		if (ret)
			WMT_DETECT_ERR_FUNC("sdio_enable_func failed!\n");
	}

	return 0;
}

static void sdio_detect_remove(struct sdio_func *func)
{
	if (g_func == func) {
		sdio_claim_host(func);
		sdio_disable_func(func);
		sdio_release_host(func);
		g_func = NULL;
	}
	WMT_DETECT_INFO_FUNC("do sdio remove\n");
}

int sdio_detect_init(void)
{
	int ret = -1;
	/* register to mmc driver */
	ret = sdio_register_driver(&mtk_sdio_client_drv);
	WMT_DETECT_INFO_FUNC("sdio_register_driver() ret=%d\n", ret);
	return 0;
}

int sdio_detect_exit(void)
{
	g_func = NULL;
	/* register to mmc driver */
	sdio_unregister_driver(&mtk_sdio_client_drv);
	WMT_DETECT_INFO_FUNC("sdio_unregister_driver\n");
	return 0;
}
