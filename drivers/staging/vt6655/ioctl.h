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
 * File: hostap.h
 *
 * Purpose:
 *
 * Author: Lyndon Chen
 *
 * Date: May 21, 2003
 *
 */

#ifndef __IOCTL_H__
#define __IOCTL_H__

#include "device.h"

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

int private_ioctl(PSDevice pDevice, struct ifreq *rq);

/*
  void vConfigWEPKey(
  PSDevice pDevice,
  unsigned long dwKeyIndex,
  unsigned char *pbyKey,
  unsigned long uKeyLength
);
*/

#endif // __IOCTL_H__
