/*
 * Driver interface for RADIUS server or WPS ER only (no driver)
 * Copyright (c) 2008, Atheros Communications
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
#include "driver.h"


struct none_driver_data {
	struct hostapd_data *hapd;
	void *ctx;
};


static void * none_driver_hapd_init(struct hostapd_data *hapd,
				    struct wpa_init_params *params)
{
	struct none_driver_data *drv;

	drv = os_zalloc(sizeof(struct none_driver_data));
	if (drv == NULL) {
		wpa_printf(MSG_ERROR, "Could not allocate memory for none "
			   "driver data");
		return NULL;
	}
	drv->hapd = hapd;

	return drv;
}


static void none_driver_hapd_deinit(void *priv)
{
	struct none_driver_data *drv = priv;

	os_free(drv);
}


static int none_driver_send_ether(void *priv, const u8 *dst, const u8 *src,
				  u16 proto, const u8 *data, size_t data_len)
{
	return 0;
}


static void * none_driver_init(void *ctx, const char *ifname)
{
	struct none_driver_data *drv;

	drv = os_zalloc(sizeof(struct none_driver_data));
	if (drv == NULL) {
		wpa_printf(MSG_ERROR, "Could not allocate memory for none "
			   "driver data");
		return NULL;
	}
	drv->ctx = ctx;

	return drv;
}


static void none_driver_deinit(void *priv)
{
	struct none_driver_data *drv = priv;

	os_free(drv);
}


static int none_driver_send_eapol(void *priv, const u8 *dest, u16 proto,
				  const u8 *data, size_t data_len)
{
	return -1;
}


const struct wpa_driver_ops wpa_driver_none_ops = {
	.name = "none",
	.desc = "no driver (RADIUS server/WPS ER)",
	.hapd_init = none_driver_hapd_init,
	.hapd_deinit = none_driver_hapd_deinit,
	.send_ether = none_driver_send_ether,
	.init = none_driver_init,
	.deinit = none_driver_deinit,
	.send_eapol = none_driver_send_eapol,
};
