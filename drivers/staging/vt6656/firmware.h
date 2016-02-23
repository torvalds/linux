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
 *
 * File: firmware.h
 *
 * Purpose: Version and Release Information
 *
 * Author: Yiching Chen
 *
 * Date: May 20, 2004
 *
 */

#ifndef __FIRMWARE_H__
#define __FIRMWARE_H__

#include "device.h"

int vnt_download_firmware(struct vnt_private *);
int vnt_firmware_branch_to_sram(struct vnt_private *);
int vnt_check_firmware_version(struct vnt_private *);

#endif /* __FIRMWARE_H__ */
