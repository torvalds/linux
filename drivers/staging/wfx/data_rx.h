/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_DATA_RX_H
#define WFX_DATA_RX_H

struct wfx_vif;
struct sk_buff;
struct hif_ind_rx;

void wfx_rx_cb(struct wfx_vif *wvif,
	       const struct hif_ind_rx *arg, struct sk_buff *skb);

#endif /* WFX_DATA_RX_H */
