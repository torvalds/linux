/*
 * rk_camera_module.h
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _RK_CAMERA_MODULE_VERSION_H_
#define _RK_CAMERA_MODULE_VERSION_H_
#include <linux/version.h>

/*
 *       CIF DRIVER VERSION NOTE
 *
 * v0.1.0:
 * 1. Initialize version;
 * 2. Update sensor configuration after power up sensor in
 * ov_camera_module_s_power.
 * Because mipi datalane may be no still on LP11 state when
 * sensor configuration;
 *
 */

#define CONFIG_RK_CAMERA_MODULE_VERSION KERNEL_VERSION(0, 1, 0)

#endif
