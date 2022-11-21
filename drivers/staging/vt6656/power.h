/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

int vnt_disable_power_saving(struct vnt_private *priv);
void vnt_enable_power_saving(struct vnt_private *priv, u16 listen_interval);
int vnt_next_tbtt_wakeup(struct vnt_private *priv);

#endif /* __POWER_H__ */
