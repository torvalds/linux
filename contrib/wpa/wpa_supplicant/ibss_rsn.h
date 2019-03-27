/*
 * wpa_supplicant - IBSS RSN
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IBSS_RSN_H
#define IBSS_RSN_H

struct ibss_rsn;

/* not authenticated */
#define IBSS_RSN_AUTH_NOT_AUTHENTICATED	0x00
/* remote peer sent an EAPOL message */
#define IBSS_RSN_AUTH_EAPOL_BY_PEER	0x01
/* we sent an AUTH message with seq 1 */
#define IBSS_RSN_AUTH_BY_US		0x02
/* we sent an EAPOL message */
#define IBSS_RSN_AUTH_EAPOL_BY_US	0x04
/* PTK derived as supplicant */
#define IBSS_RSN_SET_PTK_SUPP		0x08
/* PTK derived as authenticator */
#define IBSS_RSN_SET_PTK_AUTH		0x10
/* PTK completion reported */
#define IBSS_RSN_REPORTED_PTK		0x20

struct ibss_rsn_peer {
	struct ibss_rsn_peer *next;
	struct ibss_rsn *ibss_rsn;

	u8 addr[ETH_ALEN];

	struct wpa_sm *supp;
	enum wpa_states supp_state;
	u8 supp_ie[80];
	size_t supp_ie_len;

	struct wpa_state_machine *auth;
	int authentication_status;

	struct os_reltime own_auth_tx;
};

struct ibss_rsn {
	struct wpa_supplicant *wpa_s;
	struct wpa_authenticator *auth_group;
	struct ibss_rsn_peer *peers;
	u8 psk[PMK_LEN];
};


struct ibss_rsn * ibss_rsn_init(struct wpa_supplicant *wpa_s,
				struct wpa_ssid *ssid);
void ibss_rsn_deinit(struct ibss_rsn *ibss_rsn);
int ibss_rsn_start(struct ibss_rsn *ibss_rsn, const u8 *addr);
void ibss_rsn_stop(struct ibss_rsn *ibss_rsn, const u8 *peermac);
int ibss_rsn_rx_eapol(struct ibss_rsn *ibss_rsn, const u8 *src_addr,
		      const u8 *buf, size_t len);
void ibss_rsn_set_psk(struct ibss_rsn *ibss_rsn, const u8 *psk);
void ibss_rsn_handle_auth(struct ibss_rsn *ibss_rsn, const u8 *auth_frame,
			  size_t len);

#endif /* IBSS_RSN_H */
