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

#ifndef _SDIO_DETECT_H_
#define _SDIO_DETECT_H_

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>

#ifdef CONFIG_SDIOAUTOK_SUPPORT
#define MTK_HIF_SDIO_AUTOK_ENABLED 1
extern int wait_sdio_autok_ready(void *);
#else
#define MTK_HIF_SDIO_AUTOK_ENABLED 0
#endif

typedef struct _MTK_WCN_HIF_SDIO_CHIP_INFO_ {
	struct sdio_device_id deviceId;
	unsigned int chipId;
} MTK_WCN_HIF_SDIO_CHIP_INFO, *P_MTK_WCN_HIF_SDIO_CHIP_INFO;

extern int sdio_detect_exit(void);
extern int sdio_detect_init(void);
extern int sdio_detect_query_chipid(int waitFlag);
extern int hif_sdio_is_chipid_valid(int chipId);

extern int sdio_detect_do_autok(int chipId);

#endif
