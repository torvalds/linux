/*
 * Linux rfkill helper functions for driver wrappers
 * Copyright (c) 2010, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include <fcntl.h>

#include "utils/common.h"
#include "utils/eloop.h"
#include "rfkill.h"

#define RFKILL_EVENT_SIZE_V1 8

struct rfkill_event {
	u32 idx;
	u8 type;
	u8 op;
	u8 soft;
	u8 hard;
} STRUCT_PACKED;

enum rfkill_operation {
	RFKILL_OP_ADD = 0,
	RFKILL_OP_DEL,
	RFKILL_OP_CHANGE,
	RFKILL_OP_CHANGE_ALL,
};

enum rfkill_type {
	RFKILL_TYPE_ALL = 0,
	RFKILL_TYPE_WLAN,
	RFKILL_TYPE_BLUETOOTH,
	RFKILL_TYPE_UWB,
	RFKILL_TYPE_WIMAX,
	RFKILL_TYPE_WWAN,
	RFKILL_TYPE_GPS,
	RFKILL_TYPE_FM,
	NUM_RFKILL_TYPES,
};


struct rfkill_data {
	struct rfkill_config *cfg;
	int fd;
	int blocked;
};


static void rfkill_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct rfkill_data *rfkill = eloop_ctx;
	struct rfkill_event event;
	ssize_t len;
	int new_blocked;

	len = read(rfkill->fd, &event, sizeof(event));
	if (len < 0) {
		wpa_printf(MSG_ERROR, "rfkill: Event read failed: %s",
			   strerror(errno));
		return;
	}
	if (len != RFKILL_EVENT_SIZE_V1) {
		wpa_printf(MSG_DEBUG, "rfkill: Unexpected event size "
			   "%d (expected %d)",
			   (int) len, RFKILL_EVENT_SIZE_V1);
		return;
	}
	wpa_printf(MSG_DEBUG, "rfkill: event: idx=%u type=%d "
		   "op=%u soft=%u hard=%u",
		   event.idx, event.type, event.op, event.soft,
		   event.hard);
	if (event.op != RFKILL_OP_CHANGE || event.type != RFKILL_TYPE_WLAN)
		return;

	if (event.hard) {
		wpa_printf(MSG_INFO, "rfkill: WLAN hard blocked");
		new_blocked = 1;
	} else if (event.soft) {
		wpa_printf(MSG_INFO, "rfkill: WLAN soft blocked");
		new_blocked = 1;
	} else {
		wpa_printf(MSG_INFO, "rfkill: WLAN unblocked");
		new_blocked = 0;
	}

	if (new_blocked != rfkill->blocked) {
		rfkill->blocked = new_blocked;
		if (new_blocked)
			rfkill->cfg->blocked_cb(rfkill->cfg->ctx);
		else
			rfkill->cfg->unblocked_cb(rfkill->cfg->ctx);
	}
}


struct rfkill_data * rfkill_init(struct rfkill_config *cfg)
{
	struct rfkill_data *rfkill;
	struct rfkill_event event;
	ssize_t len;

	rfkill = os_zalloc(sizeof(*rfkill));
	if (rfkill == NULL)
		return NULL;

	rfkill->cfg = cfg;
	rfkill->fd = open("/dev/rfkill", O_RDONLY);
	if (rfkill->fd < 0) {
		wpa_printf(MSG_INFO, "rfkill: Cannot open RFKILL control "
			   "device");
		goto fail;
	}

	if (fcntl(rfkill->fd, F_SETFL, O_NONBLOCK) < 0) {
		wpa_printf(MSG_ERROR, "rfkill: Cannot set non-blocking mode: "
			   "%s", strerror(errno));
		goto fail2;
	}

	for (;;) {
		len = read(rfkill->fd, &event, sizeof(event));
		if (len < 0) {
			if (errno == EAGAIN)
				break; /* No more entries */
			wpa_printf(MSG_ERROR, "rfkill: Event read failed: %s",
				   strerror(errno));
			break;
		}
		if (len != RFKILL_EVENT_SIZE_V1) {
			wpa_printf(MSG_DEBUG, "rfkill: Unexpected event size "
				   "%d (expected %d)",
				   (int) len, RFKILL_EVENT_SIZE_V1);
			continue;
		}
		wpa_printf(MSG_DEBUG, "rfkill: initial event: idx=%u type=%d "
			   "op=%u soft=%u hard=%u",
			   event.idx, event.type, event.op, event.soft,
			   event.hard);
		if (event.op != RFKILL_OP_ADD ||
		    event.type != RFKILL_TYPE_WLAN)
			continue;
		if (event.hard) {
			wpa_printf(MSG_INFO, "rfkill: WLAN hard blocked");
			rfkill->blocked = 1;
		} else if (event.soft) {
			wpa_printf(MSG_INFO, "rfkill: WLAN soft blocked");
			rfkill->blocked = 1;
		}
	}

	eloop_register_read_sock(rfkill->fd, rfkill_receive, rfkill, NULL);

	return rfkill;

fail2:
	close(rfkill->fd);
fail:
	os_free(rfkill);
	return NULL;
}


void rfkill_deinit(struct rfkill_data *rfkill)
{
	if (rfkill == NULL)
		return;

	if (rfkill->fd >= 0) {
		eloop_unregister_read_sock(rfkill->fd);
		close(rfkill->fd);
	}

	os_free(rfkill->cfg);
	os_free(rfkill);
}


int rfkill_is_blocked(struct rfkill_data *rfkill)
{
	if (rfkill == NULL)
		return 0;

	return rfkill->blocked;
}
