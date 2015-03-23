/*
 * hostapd / IEEE 802.11F-2003 Inter-Access Point Protocol (IAPP)
 * Copyright (c) 2002-2005, Jouni Malinen <j@w1.fi>
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

#ifndef IAPP_H
#define IAPP_H

struct iapp_data;

#ifdef CONFIG_IAPP

void iapp_new_station(struct iapp_data *iapp, struct sta_info *sta);
struct iapp_data * iapp_init(struct hostapd_data *hapd, const char *iface);
void iapp_deinit(struct iapp_data *iapp);

#else /* CONFIG_IAPP */

static inline void iapp_new_station(struct iapp_data *iapp,
				    struct sta_info *sta)
{
}

static inline struct iapp_data * iapp_init(struct hostapd_data *hapd,
					   const char *iface)
{
	return NULL;
}

static inline void iapp_deinit(struct iapp_data *iapp)
{
}

#endif /* CONFIG_IAPP */

#endif /* IAPP_H */
