/*
 * wpa_supplicant D-Bus control interface - common definitions
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DBUS_COMMON_H
#define DBUS_COMMON_H

struct wpas_dbus_priv;
struct wpa_global;

struct wpas_dbus_priv * wpas_dbus_init(struct wpa_global *global);
void wpas_dbus_deinit(struct wpas_dbus_priv *priv);

#endif /* DBUS_COMMON_H */
