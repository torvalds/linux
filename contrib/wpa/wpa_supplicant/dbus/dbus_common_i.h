/*
 * wpa_supplicant D-Bus control interface - internal definitions
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DBUS_COMMON_I_H
#define DBUS_COMMON_I_H

#include <dbus/dbus.h>

struct wpa_dbus_property_desc;

struct wpas_dbus_priv {
	DBusConnection *con;
	int should_dispatch;
	struct wpa_global *global;
	u32 next_objid;
	int dbus_new_initialized;

#if defined(CONFIG_CTRL_IFACE_DBUS_NEW)
	struct wpa_dbus_property_desc *all_interface_properties;
	int globals_start;
#if defined(CONFIG_AP)
	int dbus_noc_refcnt;
#endif /* CONFIG_AP */
#endif /* CONFIG_CTRL_IFACE_DBUS_NEW */
};

#endif /* DBUS_COMMON_I_H */
