/*
 * EAP proxy definitions
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_PROXY_H
#define EAP_PROXY_H

struct eap_proxy_sm;
struct eapol_callbacks;
struct eap_sm;
struct eap_peer_config;

enum eap_proxy_status {
	EAP_PROXY_FAILURE = 0x00,
	EAP_PROXY_SUCCESS
};

struct eap_proxy_sm *
eap_proxy_init(void *eapol_ctx, const struct eapol_callbacks *eapol_cb,
	       void *msg_ctx);

void eap_proxy_deinit(struct eap_proxy_sm *eap_proxy);

int eap_proxy_key_available(struct eap_proxy_sm *sm);

const u8 * eap_proxy_get_eapKeyData(struct eap_proxy_sm *sm, size_t *len);

struct wpabuf * eap_proxy_get_eapRespData(struct eap_proxy_sm *sm);

int eap_proxy_sm_step(struct eap_proxy_sm *sm, struct eap_sm *eap_sm);

enum eap_proxy_status
eap_proxy_packet_update(struct eap_proxy_sm *eap_proxy, u8 *eapReqData,
			int eapReqDataLen);

int eap_proxy_sm_get_status(struct eap_proxy_sm *sm, char *buf, size_t buflen,
			    int verbose);

int eap_proxy_get_imsi(struct eap_proxy_sm *eap_proxy, int sim_num,
		       char *imsi_buf, size_t *imsi_len);

int eap_proxy_notify_config(struct eap_proxy_sm *sm,
			    struct eap_peer_config *config);

u8 * eap_proxy_get_eap_session_id(struct eap_proxy_sm *sm, size_t *len);

u8 * eap_proxy_get_emsk(struct eap_proxy_sm *sm, size_t *len);

void eap_proxy_sm_abort(struct eap_proxy_sm *sm);

#endif /* EAP_PROXY_H */
