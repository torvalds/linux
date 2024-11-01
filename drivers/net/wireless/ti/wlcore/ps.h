/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#ifndef __PS_H__
#define __PS_H__

#include "wlcore.h"
#include "acx.h"

int wl1271_ps_set_mode(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       enum wl1271_cmd_ps_mode mode);
void wl12xx_ps_link_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  u8 hlid, bool clean_queues);
void wl12xx_ps_link_end(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 hlid);

#define WL1271_PS_COMPLETE_TIMEOUT 500

#endif /* __WL1271_PS_H__ */
