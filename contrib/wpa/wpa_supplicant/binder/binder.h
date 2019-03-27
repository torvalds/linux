/*
 * binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_BINDER_BINDER_H
#define WPA_SUPPLICANT_BINDER_BINDER_H

#ifdef _cplusplus
extern "C" {
#endif /* _cplusplus */

/**
 * This is the binder RPC interface entry point to the wpa_supplicant core.
 * This initializes the binder driver & BinderManager instance and then forwards
 * all the notifcations from the supplicant core to the BinderManager.
 */
struct wpas_binder_priv;
struct wpa_global;

struct wpas_binder_priv *wpas_binder_init(struct wpa_global *global);
void wpas_binder_deinit(struct wpas_binder_priv *priv);

#ifdef CONFIG_CTRL_IFACE_BINDER
int wpas_binder_register_interface(struct wpa_supplicant *wpa_s);
int wpas_binder_unregister_interface(struct wpa_supplicant *wpa_s);
#else  /* CONFIG_CTRL_IFACE_BINDER */
static inline int wpas_binder_register_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}
static inline int wpas_binder_unregister_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}
#endif /* CONFIG_CTRL_IFACE_BINDER */

#ifdef _cplusplus
}
#endif /* _cplusplus */

#endif /* WPA_SUPPLICANT_BINDER_BINDER_H */
