/*
 * hostapd / WPA authenticator glue code
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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

#ifndef WPA_AUTH_GLUE_H
#define WPA_AUTH_GLUE_H

int hostapd_setup_wpa(struct hostapd_data *hapd);
void hostapd_reconfig_wpa(struct hostapd_data *hapd);
void hostapd_deinit_wpa(struct hostapd_data *hapd);

#endif /* WPA_AUTH_GLUE_H */
