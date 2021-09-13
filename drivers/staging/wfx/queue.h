/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Queue between the tx operation and the bh workqueue.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_QUEUE_H
#define WFX_QUEUE_H

#include <linux/skbuff.h>
#include <linux/atomic.h>

struct wfx_dev;
struct wfx_vif;

struct wfx_queue {
	struct sk_buff_head	normal;
	struct sk_buff_head	cab; /* Content After (DTIM) Beacon */
	atomic_t		pending_frames;
	int			priority;
};

void wfx_tx_lock(struct wfx_dev *wdev);
void wfx_tx_unlock(struct wfx_dev *wdev);
void wfx_tx_flush(struct wfx_dev *wdev);
void wfx_tx_lock_flush(struct wfx_dev *wdev);

void wfx_tx_queues_init(struct wfx_vif *wvif);
void wfx_tx_queues_check_empty(struct wfx_vif *wvif);
bool wfx_tx_queues_has_cab(struct wfx_vif *wvif);
void wfx_tx_queues_put(struct wfx_vif *wvif, struct sk_buff *skb);
struct hif_msg *wfx_tx_queues_get(struct wfx_dev *wdev);

bool wfx_tx_queue_empty(struct wfx_vif *wvif, struct wfx_queue *queue);
void wfx_tx_queue_drop(struct wfx_vif *wvif, struct wfx_queue *queue,
		       struct sk_buff_head *dropped);

struct sk_buff *wfx_pending_get(struct wfx_dev *wdev, u32 packet_id);
void wfx_pending_drop(struct wfx_dev *wdev, struct sk_buff_head *dropped);
unsigned int wfx_pending_get_pkt_us_delay(struct wfx_dev *wdev,
					  struct sk_buff *skb);
void wfx_pending_dump_old_frames(struct wfx_dev *wdev, unsigned int limit_ms);

#endif /* WFX_QUEUE_H */
