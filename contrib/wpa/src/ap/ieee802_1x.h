/*
 * hostapd / IEEE 802.1X-2004 Authenticator
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_1X_H
#define IEEE802_1X_H

struct hostapd_data;
struct sta_info;
struct eapol_state_machine;
struct hostapd_config;
struct hostapd_bss_config;
struct hostapd_radius_attr;
struct radius_msg;


void ieee802_1x_receive(struct hostapd_data *hapd, const u8 *sa, const u8 *buf,
			size_t len);
void ieee802_1x_new_station(struct hostapd_data *hapd, struct sta_info *sta);
void ieee802_1x_free_station(struct hostapd_data *hapd, struct sta_info *sta);

void ieee802_1x_abort_auth(struct hostapd_data *hapd, struct sta_info *sta);
void ieee802_1x_set_sta_authorized(struct hostapd_data *hapd,
				   struct sta_info *sta, int authorized);
void ieee802_1x_dump_state(FILE *f, const char *prefix, struct sta_info *sta);
int ieee802_1x_init(struct hostapd_data *hapd);
void ieee802_1x_erp_flush(struct hostapd_data *hapd);
void ieee802_1x_deinit(struct hostapd_data *hapd);
int ieee802_1x_tx_status(struct hostapd_data *hapd, struct sta_info *sta,
			 const u8 *buf, size_t len, int ack);
int ieee802_1x_eapol_tx_status(struct hostapd_data *hapd, struct sta_info *sta,
			       const u8 *data, int len, int ack);
u8 * ieee802_1x_get_identity(struct eapol_state_machine *sm, size_t *len);
u8 * ieee802_1x_get_radius_class(struct eapol_state_machine *sm, size_t *len,
				 int idx);
struct wpabuf * ieee802_1x_get_radius_cui(struct eapol_state_machine *sm);
const u8 * ieee802_1x_get_key(struct eapol_state_machine *sm, size_t *len);
void ieee802_1x_notify_port_enabled(struct eapol_state_machine *sm,
				    int enabled);
void ieee802_1x_notify_port_valid(struct eapol_state_machine *sm,
				  int valid);
void ieee802_1x_notify_pre_auth(struct eapol_state_machine *sm, int pre_auth);
int ieee802_1x_get_mib(struct hostapd_data *hapd, char *buf, size_t buflen);
int ieee802_1x_get_mib_sta(struct hostapd_data *hapd, struct sta_info *sta,
			   char *buf, size_t buflen);
void hostapd_get_ntp_timestamp(u8 *buf);
char *eap_type_text(u8 type);

const char *radius_mode_txt(struct hostapd_data *hapd);
int radius_sta_rate(struct hostapd_data *hapd, struct sta_info *sta);

int add_common_radius_attr(struct hostapd_data *hapd,
			   struct hostapd_radius_attr *req_attr,
			   struct sta_info *sta,
			   struct radius_msg *msg);
void ieee802_1x_encapsulate_radius(struct hostapd_data *hapd,
				   struct sta_info *sta,
				   const u8 *eap, size_t len);
struct eapol_state_machine *
ieee802_1x_alloc_eapol_sm(struct hostapd_data *hapd, struct sta_info *sta);

#endif /* IEEE802_1X_H */
