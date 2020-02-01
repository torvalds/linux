/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of chip-to-host event (aka indications) of WFxxx Split Mac
 * (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_RX_H
#define WFX_HIF_RX_H

struct wfx_dev;
struct sk_buff;

void wfx_handle_rx(struct wfx_dev *wdev, struct sk_buff *skb);

#endif
