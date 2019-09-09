/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#include "device.h"

#define C_PWBT                   1000    /* micro sec. power up before TBTT */
#define PS_FAST_INTERVAL         1       /* Fast power saving listen interval */
#define PS_MAX_INTERVAL          4       /* MAX power saving listen interval */

void PSvDisablePowerSaving(struct vnt_private *priv);

void PSvEnablePowerSaving(struct vnt_private *priv, unsigned short wListenInterval);

bool PSbIsNextTBTTWakeUp(struct vnt_private *priv);

#endif /* __POWER_H__ */
