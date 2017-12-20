/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_BOOT_SHARE_PAGE_H__
#define __MTK_BOOT_SHARE_PAGE_H__

#define BOOT_SHARE_BASE  (0xC0002000)      /* address in linux kernel */
#define BOOT_SHARE_SIZE  (0x1000)          /* page size 4K bytes */

#define BOOT_SHARE_MAGIC (0x4545544D)      /* MTEE */

/* Memory map & defines for boot share page */
/*
 *  Note:
 *  1. BOOT_SHARE_XXXXX_OFST is the address offset related to BOOT_SHARE_BASE
 */
#define BOOT_SHARE_MAGIC1_OFST   (0)
#define BOOT_SHARE_MAGIC1_SIZE   (4)

#define BOOT_SHARE_DEV_INFO_OFST (BOOT_SHARE_MAGIC1_OFST+BOOT_SHARE_MAGIC1_SIZE)
#define BOOT_SHARE_DEV_INFO_SIZE (16)

#define BOOT_SHARE_HOTPLUG_OFST  (1008)    /* 16 bytes for hotplug/jump-reg */
#define BOOT_SHARE_HOTPLUG_SIZE  (32)

#define BOOT_SHARE_MAGIC2_OFST   (4092)
#define BOOT_SHARE_MAGIC2_SIZE   (4)

#endif /* __MTK_BOOT_SHARE_PAGE_H__ */
