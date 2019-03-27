/*
 * hostapd / TKIP countermeasures
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TKIP_COUNTERMEASURES_H
#define TKIP_COUNTERMEASURES_H

int michael_mic_failure(struct hostapd_data *hapd, const u8 *addr, int local);
void ieee80211_tkip_countermeasures_deinit(struct hostapd_data *hapd);

#endif /* TKIP_COUNTERMEASURES_H */
