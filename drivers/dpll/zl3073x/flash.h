/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __ZL3073X_FLASH_H
#define __ZL3073X_FLASH_H

#include <linux/types.h>

struct netlink_ext_ack;
struct zl3073x_dev;

int zl3073x_flash_mode_enter(struct zl3073x_dev *zldev, const void *util_ptr,
			     size_t util_size, struct netlink_ext_ack *extack);

int zl3073x_flash_mode_leave(struct zl3073x_dev *zldev,
			     struct netlink_ext_ack *extack);

int zl3073x_flash_page(struct zl3073x_dev *zldev, const char *component,
		       u32 page, u32 addr, const void *data, size_t size,
		       struct netlink_ext_ack *extack);

int zl3073x_flash_page_copy(struct zl3073x_dev *zldev, const char *component,
			    u32 src_page, u32 dst_page,
			    struct netlink_ext_ack *extack);

int zl3073x_flash_sectors(struct zl3073x_dev *zldev, const char *component,
			  u32 page, u32 addr, const void *data, size_t size,
			  struct netlink_ext_ack *extack);

#endif /* __ZL3073X_FLASH_H */
