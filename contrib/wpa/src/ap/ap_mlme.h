/*
 * hostapd / IEEE 802.11 MLME
 * Copyright 2003, Jouni Malinen <j@w1.fi>
 * Copyright 2003-2004, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MLME_H
#define MLME_H

void mlme_authenticate_indication(struct hostapd_data *hapd,
				  struct sta_info *sta);

void mlme_deauthenticate_indication(struct hostapd_data *hapd,
				    struct sta_info *sta, u16 reason_code);

void mlme_associate_indication(struct hostapd_data *hapd,
			       struct sta_info *sta);

void mlme_reassociate_indication(struct hostapd_data *hapd,
				 struct sta_info *sta);

void mlme_disassociate_indication(struct hostapd_data *hapd,
				  struct sta_info *sta, u16 reason_code);

void mlme_michaelmicfailure_indication(struct hostapd_data *hapd,
				       const u8 *addr);

void mlme_deletekeys_request(struct hostapd_data *hapd, struct sta_info *sta);

#endif /* MLME_H */
