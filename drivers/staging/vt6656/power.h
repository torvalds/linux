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
 *
 * File: power.h
 *
 * Purpose: Handles 802.11 power management  functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 17, 2002
 *
 */

#ifndef __POWER_H__
#define __POWER_H__

#define C_PWBT	1000 /* micro sec. power up before TBTT */

void vnt_disable_power_saving(struct vnt_private *priv);
void vnt_enable_power_saving(struct vnt_private *priv, u16 listen_interval);
int vnt_next_tbtt_wakeup(struct vnt_private *priv);

#endif /* __POWER_H__ */
