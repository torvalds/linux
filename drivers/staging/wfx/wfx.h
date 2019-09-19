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
#include "data_tx.h"
#include "main.h"
#include "queue.h"
#include "secure_link.h"
#include "sta.h"
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
	struct wfx_queue	tx_queue[4];
	struct wfx_queue_stats	tx_queue_stats;
	int			tx_burst_idx;
	atomic_t		tx_lock;

	struct hif_rx_stats	rx_stats;
	struct mutex		rx_stats_lock;
};

struct wfx_vif {
	struct wfx_dev		*wdev;
	struct ieee80211_vif	*vif;
	int			id;


	u32			link_id_map;
	struct wfx_link_entry	link_id_db[WFX_MAX_STA_IN_AP_MODE];
	struct delayed_work	link_id_gc_work;
	struct work_struct	link_id_work;

	bool			aid0_bit_set;
	bool			mcast_tx;
	bool			mcast_buffered;
	struct timer_list	mcast_timeout;
	struct work_struct	mcast_start_work;
	struct work_struct	mcast_stop_work;


	struct tx_policy_cache	tx_policy_cache;
	struct work_struct	tx_policy_upload_work;
	u32			sta_asleep_mask;
	u32			pspoll_mask;
	spinlock_t		ps_state_lock;

	struct wfx_edca_params	edca;
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

static inline struct wfx_vif *wvif_iterate(struct wfx_dev *wdev, struct wfx_vif *cur)
{
	int i;
	int mark = 0;
	struct wfx_vif *tmp;

	if (!cur)
		mark = 1;
	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		tmp = wdev_to_wvif(wdev, i);
		if (mark && tmp)
			return tmp;
		if (tmp == cur)
			mark = 1;
	}
	return NULL;
}

static inline int memzcmp(void *src, unsigned int size)
{
	uint8_t *buf = src;

	if (!size)
		return 0;
	if (*buf)
		return 1;
	return memcmp(buf, buf + 1, size - 1);
}

#endif /* WFX_H */
