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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: device_cfg.h
 *
 * Purpose: Driver configuration header
 * Author: Lyndon Chen
 *
 * Date: Dec 9, 2005
 *
 */
#ifndef __DEVICE_CONFIG_H
#define __DEVICE_CONFIG_H

#define DEVICE_NAME         "vt6656"
#define DEVICE_FULL_DRV_NAM "VIA Networking Wireless LAN USB Driver"

#define DEVICE_VERSION       "1.19_12"

#define MAX_RATE	12

#define CONFIG_PATH            "/etc/vntconfiguration.dat"

#define MAX_UINTS           8
#define OPTION_DEFAULT      { [0 ... MAX_UINTS-1] = -1}

#endif
