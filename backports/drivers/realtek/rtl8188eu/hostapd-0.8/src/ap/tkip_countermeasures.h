/*
 * hostapd / TKIP countermeasures
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

#ifndef TKIP_COUNTERMEASURES_H
#define TKIP_COUNTERMEASURES_H

void michael_mic_failure(struct hostapd_data *hapd, const u8 *addr, int local);

#endif /* TKIP_COUNTERMEASURES_H */
