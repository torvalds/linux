// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *
 * File: device_cfg.h
 *
 * Purpose: Driver configuration header
 * Author: Lyndon Chen
 *
 * Date: Dec 17, 2002
 *
 */
#ifndef __DEVICE_CONFIG_H
#define __DEVICE_CONFIG_H

#include <linux/types.h>

typedef
struct _version {
	unsigned char   major;
	unsigned char   minor;
	unsigned char   build;
} version_t, *pversion_t;

#define VID_TABLE_SIZE      64
#define MCAST_TABLE_SIZE    64
#define MCAM_SIZE           32
#define VCAM_SIZE           32
#define TX_QUEUE_NO         8

#define DEVICE_NAME         "vt6655"
#define DEVICE_FULL_DRV_NAM "VIA Networking Solomon-A/B/G Wireless LAN Adapter Driver"

#ifndef MAJOR_VERSION
#define MAJOR_VERSION       1
#endif

#ifndef MINOR_VERSION
#define MINOR_VERSION       17
#endif

#ifndef DEVICE_VERSION
#define DEVICE_VERSION       "1.19.12"
#endif

#include <linux/fs.h>
#include <linux/fcntl.h>
#ifndef CONFIG_PATH
#define CONFIG_PATH            "/etc/vntconfiguration.dat"
#endif

#define PKT_BUF_SZ          2390

typedef enum  _chip_type {
	VT3253 = 1
} CHIP_TYPE, *PCHIP_TYPE;

#endif
