// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/skbuff.h>
#include <linux/etherdevice.h>

#include "hif_tx.h"
#include "wfx.h"
#include "bh.h"
#include "debug.h"

void wfx_init_hif_cmd(struct wfx_hif_cmd *hif_cmd)
{
	init_completion(&hif_cmd->ready);
	init_completion(&hif_cmd->done);
	mutex_init(&hif_cmd->lock);
}

int wfx_cmd_send(struct wfx_dev *wdev, struct hif_msg *request, void *reply, size_t reply_len, bool async)
{
	const char *mib_name = "";
	const char *mib_sep = "";
	int cmd = request->id;
	int vif = request->interface;
	int ret;

	WARN(wdev->hif_cmd.buf_recv && wdev->hif_cmd.async, "API usage error");

	// Do not wait for any reply if chip is frozen
	if (wdev->chip_frozen)
		return -ETIMEDOUT;

	mutex_lock(&wdev->hif_cmd.lock);
	WARN(wdev->hif_cmd.buf_send, "data locking error");

	// Note: call to complete() below has an implicit memory barrier that
	// hopefully protect buf_send
	wdev->hif_cmd.buf_send = request;
	wdev->hif_cmd.buf_recv = reply;
	wdev->hif_cmd.len_recv = reply_len;
	wdev->hif_cmd.async = async;
	complete(&wdev->hif_cmd.ready);

	wfx_bh_request_tx(wdev);

	// NOTE: no timeout is catched async is enabled
	if (async)
		return 0;

	ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 1 * HZ);
	if (!ret) {
		dev_err(wdev->dev, "chip is abnormally long to answer\n");
		reinit_completion(&wdev->hif_cmd.ready);
		ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 3 * HZ);
	}
	if (!ret) {
		dev_err(wdev->dev, "chip did not answer\n");
		wdev->chip_frozen = 1;
		reinit_completion(&wdev->hif_cmd.done);
		ret = -ETIMEDOUT;
	} else {
		ret = wdev->hif_cmd.ret;
	}

	wdev->hif_cmd.buf_send = NULL;
	mutex_unlock(&wdev->hif_cmd.lock);

	if (ret && (cmd == HIF_REQ_ID_READ_MIB || cmd == HIF_REQ_ID_WRITE_MIB)) {
		mib_name = get_mib_name(((u16 *) request)[2]);
		mib_sep = "/";
	}
	if (ret < 0)
		dev_err(wdev->dev,
			"WSM request %s%s%s (%#.2x) on vif %d returned error %d\n",
			get_hif_name(cmd), mib_sep, mib_name, cmd, vif, ret);
	if (ret > 0)
		dev_warn(wdev->dev,
			 "WSM request %s%s%s (%#.2x) on vif %d returned status %d\n",
			 get_hif_name(cmd), mib_sep, mib_name, cmd, vif, ret);

	return ret;
}
