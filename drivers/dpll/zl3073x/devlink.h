/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_DEVLINK_H
#define _ZL3073X_DEVLINK_H

struct zl3073x_dev;

struct zl3073x_dev *zl3073x_devm_alloc(struct device *dev);

int zl3073x_devlink_register(struct zl3073x_dev *zldev);

void zl3073x_devlink_flash_notify(struct zl3073x_dev *zldev, const char *msg,
				  const char *component, u32 done, u32 total);

#endif /* _ZL3073X_DEVLINK_H */
