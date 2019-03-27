/*
 * binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "iface.h"

namespace wpa_supplicant_binder {

Iface::Iface(struct wpa_supplicant *wpa_s) : wpa_s_(wpa_s) {}

} /* namespace wpa_supplicant_binder */
