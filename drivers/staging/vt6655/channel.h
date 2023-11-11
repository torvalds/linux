/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 */

#ifndef _CHANNEL_H_
#define _CHANNEL_H_

#include "card.h"

void vnt_init_bands(struct vnt_private *priv);

bool set_channel(struct vnt_private *priv, struct ieee80211_channel *ch);

#endif /* _CHANNEL_H_ */
