/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common private data for Silicon Labs WFx chips.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#ifndef WFX_H
#define WFX_H

#include <linux/completion.h>
#include <net/mac80211.h>

#include "bh.h"
#include "main.h"
#include "secure_link.h"
#include "hif_tx.h"
#include "hif_api_general.h"

struct hwbus_ops;

struct wfx_dev {
	struct wfx_platform_data pdata;
	struct device		*dev;
	struct ieee80211_hw	*hw;
	struct ieee80211_vif	*vif[2];
	struct mac_address	addresses[2];
	const struct hwbus_ops	*hwbus_ops;
	void			*hwbus_priv;

	u8			keyset;
	struct completion	firmware_ready;
	struct hif_ind_startup	hw_caps;
	struct wfx_hif		hif;
	struct sl_context	sl;
	int			chip_frozen;

	struct wfx_hif_cmd	hif_cmd;

	struct hif_rx_stats	rx_stats;
	struct mutex		rx_stats_lock;
};

struct wfx_vif {
	struct wfx_dev		*wdev;
	struct ieee80211_vif	*vif;
	int			id;
};

static inline struct wfx_vif *wdev_to_wvif(struct wfx_dev *wdev, int vif_id)
{
	if (vif_id >= ARRAY_SIZE(wdev->vif)) {
		dev_dbg(wdev->dev, "requesting non-existent vif: %d\n", vif_id);
		return NULL;
	}
	if (!wdev->vif[vif_id]) {
		dev_dbg(wdev->dev, "requesting non-allocated vif: %d\n", vif_id);
		return NULL;
	}
	return (struct wfx_vif *) wdev->vif[vif_id]->drv_priv;
}

#endif /* WFX_H */
