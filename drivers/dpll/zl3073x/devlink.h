/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_DEVLINK_H
#define _ZL3073X_DEVLINK_H

struct zl3073x_dev;

struct zl3073x_dev *zl3073x_devm_alloc(struct device *dev);

int zl3073x_devlink_register(struct zl3073x_dev *zldev);

#endif /* _ZL3073X_DEVLINK_H */
