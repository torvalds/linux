/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interrupt bottom half (BH).
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_BH_H
#define WFX_BH_H

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

struct wfx_dev;

struct wfx_hif {
	struct work_struct bh;
	struct completion ctrl_ready;
	wait_queue_head_t tx_buffers_empty;
	atomic_t ctrl_reg;
	int rx_seqnum;
	int tx_seqnum;
	int tx_buffers_used;
};

void wfx_bh_register(struct wfx_dev *wdev);
void wfx_bh_unregister(struct wfx_dev *wdev);
void wfx_bh_request_rx(struct wfx_dev *wdev);
void wfx_bh_request_tx(struct wfx_dev *wdev);
void wfx_bh_poll_irq(struct wfx_dev *wdev);

#endif
