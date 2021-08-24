/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/reset.h>

#include "rknpu_drv.h"

int rknpu_reset_get(struct rknpu_device *rknpu_dev);

int rknpu_soft_reset(struct rknpu_device *rknpu_dev);
