/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2022, Intel Corporation.
 */

#ifndef __T7XX_PORT_DEVLINK_H__
#define __T7XX_PORT_DEVLINK_H__

#include <net/devlink.h>

#include "t7xx_pci.h"

#define T7XX_MAX_QUEUE_LENGTH 32
#define T7XX_FB_COMMAND_SIZE  64
#define T7XX_FB_RESPONSE_SIZE 64
#define T7XX_FB_MCMD_SIZE     64
#define T7XX_FB_MDATA_SIZE    1024
#define T7XX_FB_RESP_COUNT    30

#define T7XX_FB_CMD_RTS          "_RTS"
#define T7XX_FB_CMD_CTS          "_CTS"
#define T7XX_FB_CMD_FIN          "_FIN"
#define T7XX_FB_CMD_OEM_MRDUMP   "oem mrdump"
#define T7XX_FB_CMD_OEM_LKDUMP   "oem dump_pllk_log"
#define T7XX_FB_CMD_DOWNLOAD     "download"
#define T7XX_FB_CMD_FLASH        "flash"
#define T7XX_FB_CMD_REBOOT       "reboot"
#define T7XX_FB_RESP_MRDUMP_DONE "MRDUMP08_DONE"
#define T7XX_FB_RESP_OKAY        "OKAY"
#define T7XX_FB_RESP_FAIL        "FAIL"
#define T7XX_FB_RESP_DATA        "DATA"
#define T7XX_FB_RESP_INFO        "INFO"

#define T7XX_FB_EVENT_SIZE      50

#define T7XX_MAX_SNAPSHOTS  1
#define T7XX_MAX_REGION_NAME_LENGTH 20
#define T7XX_MRDUMP_SIZE    (160 * 1024 * 1024)
#define T7XX_LKDUMP_SIZE    (256 * 1024)
#define T7XX_TOTAL_REGIONS  2

#define T7XX_FLASH_STATUS   0
#define T7XX_MRDUMP_STATUS  1
#define T7XX_LKDUMP_STATUS  2
#define T7XX_DEVLINK_IDLE   0

#define T7XX_FB_NO_MODE     0
#define T7XX_FB_DL_MODE     1
#define T7XX_FB_DUMP_MODE   2

#define T7XX_MRDUMP_INDEX   0
#define T7XX_LKDUMP_INDEX   1

struct t7xx_devlink_work {
	struct work_struct work;
	struct t7xx_port *port;
};

struct t7xx_devlink_region_info {
	char region_name[T7XX_MAX_REGION_NAME_LENGTH];
	u32 default_size;
	u32 actual_size;
	u32 entry;
	u8 *dump;
};

struct t7xx_devlink {
	struct t7xx_pci_dev *mtk_dev;
	struct t7xx_port *port;
	struct device *dev;
	struct devlink *dl_ctx;
	struct t7xx_devlink_work *dl_work;
	struct workqueue_struct *dl_wq;
	struct t7xx_devlink_region_info *dl_region_info[T7XX_TOTAL_REGIONS];
	struct devlink_region_ops dl_region_ops[T7XX_TOTAL_REGIONS];
	struct devlink_region *dl_region[T7XX_TOTAL_REGIONS];
	u8 mode;
	unsigned long status;
	int set_fastboot_dl;
};

int t7xx_devlink_register(struct t7xx_pci_dev *t7xx_dev);
void t7xx_devlink_unregister(struct t7xx_pci_dev *t7xx_dev);

#endif /*__T7XX_PORT_DEVLINK_H__*/
