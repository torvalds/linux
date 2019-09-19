/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_TX_H
#define WFX_HIF_TX_H

#include "hif_api_cmd.h"

struct wfx_dev;
struct wfx_vif;

struct wfx_hif_cmd {
	struct mutex      lock;
	struct completion ready;
	struct completion done;
	bool              async;
	struct hif_msg    *buf_send;
	void              *buf_recv;
	size_t            len_recv;
	int               ret;
};

void wfx_init_hif_cmd(struct wfx_hif_cmd *wfx_hif_cmd);
int wfx_cmd_send(struct wfx_dev *wdev, struct hif_msg *request,
		 void *reply, size_t reply_len, bool async);

#endif
