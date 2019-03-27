/*
 * WPA Supplicant - Windows/NDIS driver interface - event processing
 * Copyright (c) 2004-2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "driver.h"
#include "eloop.h"

/* Keep this event processing in a separate file and without WinPcap headers to
 * avoid conflicts with some of the header files. */
struct _ADAPTER;
typedef struct _ADAPTER * LPADAPTER;
#include "driver_ndis.h"


void wpa_driver_ndis_event_connect(struct wpa_driver_ndis_data *drv);
void wpa_driver_ndis_event_disconnect(struct wpa_driver_ndis_data *drv);
void wpa_driver_ndis_event_media_specific(struct wpa_driver_ndis_data *drv,
					  const u8 *data, size_t data_len);
void wpa_driver_ndis_event_adapter_arrival(struct wpa_driver_ndis_data *drv);
void wpa_driver_ndis_event_adapter_removal(struct wpa_driver_ndis_data *drv);


enum event_types { EVENT_CONNECT, EVENT_DISCONNECT,
		   EVENT_MEDIA_SPECIFIC, EVENT_ADAPTER_ARRIVAL,
		   EVENT_ADAPTER_REMOVAL };

/* Event data:
 * enum event_types (as int, i.e., 4 octets)
 * data length (2 octets (big endian), optional)
 * data (variable len, optional)
 */


static void wpa_driver_ndis_event_process(struct wpa_driver_ndis_data *drv,
					  u8 *buf, size_t len)
{
	u8 *pos, *data = NULL;
	enum event_types type;
	size_t data_len = 0;

	wpa_hexdump(MSG_MSGDUMP, "NDIS: received event data", buf, len);
	if (len < sizeof(int))
		return;
	type = *((int *) buf);
	pos = buf + sizeof(int);
	wpa_printf(MSG_DEBUG, "NDIS: event - type %d", type);

	if (buf + len - pos > 2) {
		data_len = (int) *pos++ << 8;
		data_len += *pos++;
		if (data_len > (size_t) (buf + len - pos)) {
			wpa_printf(MSG_DEBUG, "NDIS: event data overflow");
			return;
		}
		data = pos;
		wpa_hexdump(MSG_MSGDUMP, "NDIS: event data", data, data_len);
	}

	switch (type) {
	case EVENT_CONNECT:
		wpa_driver_ndis_event_connect(drv);
		break;
	case EVENT_DISCONNECT:
		wpa_driver_ndis_event_disconnect(drv);
		break;
	case EVENT_MEDIA_SPECIFIC:
		wpa_driver_ndis_event_media_specific(drv, data, data_len);
		break;
	case EVENT_ADAPTER_ARRIVAL:
		wpa_driver_ndis_event_adapter_arrival(drv);
		break;
	case EVENT_ADAPTER_REMOVAL:
		wpa_driver_ndis_event_adapter_removal(drv);
		break;
	}
}


void wpa_driver_ndis_event_pipe_cb(void *eloop_data, void *user_data)
{
	struct wpa_driver_ndis_data *drv = eloop_data;
	u8 buf[512];
	DWORD len;

	ResetEvent(drv->event_avail);
	if (ReadFile(drv->events_pipe, buf, sizeof(buf), &len, NULL))
		wpa_driver_ndis_event_process(drv, buf, len);
	else {
		wpa_printf(MSG_DEBUG, "%s: ReadFile() failed: %d", __func__,
			   (int) GetLastError());
	}
}
