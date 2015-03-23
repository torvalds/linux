/*
 * NFC routines for Wi-Fi Protected Setup
 * Copyright (c) 2009, Masashi Honma <honma@ictec.co.jp>
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
#include "common.h"

#include "wps/wps.h"
#include "wps_i.h"


struct wps_nfc_data {
	struct oob_nfc_device_data *oob_nfc_dev;
};


static void * init_nfc(struct wps_context *wps,
		       struct oob_device_data *oob_dev, int registrar)
{
	struct oob_nfc_device_data *oob_nfc_dev;
	struct wps_nfc_data *data;

	oob_nfc_dev = wps_get_oob_nfc_device(oob_dev->device_name);
	if (oob_nfc_dev == NULL) {
		wpa_printf(MSG_ERROR, "WPS (NFC): Unknown NFC device (%s)",
			   oob_dev->device_name);
		return NULL;
	}

	if (oob_nfc_dev->init_func(oob_dev->device_path) < 0)
		return NULL;

	data = os_zalloc(sizeof(*data));
	if (data == NULL) {
		wpa_printf(MSG_ERROR, "WPS (NFC): Failed to allocate "
			   "nfc data area");
		return NULL;
	}
	data->oob_nfc_dev = oob_nfc_dev;
	return data;
}


static struct wpabuf * read_nfc(void *priv)
{
	struct wps_nfc_data *data = priv;
	struct wpabuf *wifi, *buf;
	char *raw_data;
	size_t len;

	raw_data = data->oob_nfc_dev->read_func(&len);
	if (raw_data == NULL)
		return NULL;

	wifi = wpabuf_alloc_copy(raw_data, len);
	os_free(raw_data);
	if (wifi == NULL) {
		wpa_printf(MSG_ERROR, "WPS (NFC): Failed to allocate "
			   "nfc read area");
		return NULL;
	}

	buf = ndef_parse_wifi(wifi);
	wpabuf_free(wifi);
	if (buf == NULL)
		wpa_printf(MSG_ERROR, "WPS (NFC): Failed to unwrap");
	return buf;
}


static int write_nfc(void *priv, struct wpabuf *buf)
{
	struct wps_nfc_data *data = priv;
	struct wpabuf *wifi;
	int ret;

	wifi = ndef_build_wifi(buf);
	if (wifi == NULL) {
		wpa_printf(MSG_ERROR, "WPS (NFC): Failed to wrap");
		return -1;
	}

	ret = data->oob_nfc_dev->write_func(wpabuf_mhead(wifi),
					    wpabuf_len(wifi));
	wpabuf_free(wifi);
	return ret;
}


static void deinit_nfc(void *priv)
{
	struct wps_nfc_data *data = priv;

	data->oob_nfc_dev->deinit_func();

	os_free(data);
}


struct oob_device_data oob_nfc_device_data = {
	.device_name	= NULL,
	.device_path	= NULL,
	.init_func	= init_nfc,
	.read_func	= read_nfc,
	.write_func	= write_nfc,
	.deinit_func	= deinit_nfc,
};
