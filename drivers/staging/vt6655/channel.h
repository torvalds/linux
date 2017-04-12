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
 * File: channel.h
 *
 */

#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include "card.h"

void vnt_init_bands(struct vnt_private *);

bool set_channel(struct vnt_private *, struct ieee80211_channel *);

#endif /* _CHANNEL_H_ */
