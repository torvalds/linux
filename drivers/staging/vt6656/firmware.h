// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

int vnt_download_firmware(struct vnt_private *priv);
int vnt_firmware_branch_to_sram(struct vnt_private *priv);
int vnt_check_firmware_version(struct vnt_private *priv);

#endif /* __FIRMWARE_H__ */
