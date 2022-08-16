/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2022, Intel Corporation.
 */

#ifndef __T7XX_UEVENT_H__
#define __T7XX_UEVENT_H__

#include <linux/device.h>
#include <linux/kobject.h>

/* Maximum length of user events */
#define T7XX_MAX_UEVENT_LEN 64

/* T7XX Host driver uevents */
#define T7XX_UEVENT_MODEM_READY			"T7XX_MODEM_READY"
#define T7XX_UEVENT_MODEM_FASTBOOT_DL_MODE	"T7XX_MODEM_FASTBOOT_DL_MODE"
#define T7XX_UEVENT_MODEM_FASTBOOT_DUMP_MODE	"T7XX_MODEM_FASTBOOT_DUMP_MODE"
#define T7XX_UEVENT_MRDUMP_READY		"T7XX_MRDUMP_READY"
#define T7XX_UEVENT_LKDUMP_READY		"T7XX_LKDUMP_READY"
#define T7XX_UEVENT_MRD_DISCD			"T7XX_MRDUMP_DISCARDED"
#define T7XX_UEVENT_LKD_DISCD			"T7XX_LKDUMP_DISCARDED"
#define T7XX_UEVENT_FLASHING_SUCCESS		"T7XX_FLASHING_SUCCESS"
#define T7XX_UEVENT_FLASHING_FAILURE		"T7XX_FLASHING_FAILURE"

/**
 * struct t7xx_uevent_info - Uevent information structure.
 * @dev:	Pointer to device structure
 * @uevent:	Uevent information
 * @work:	Uevent work struct
 */
struct t7xx_uevent_info {
	struct device *dev;
	char uevent[T7XX_MAX_UEVENT_LEN];
	struct work_struct work;
};

void t7xx_uevent_send(struct device *dev, char *uevent);
#endif
