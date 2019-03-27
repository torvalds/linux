/*
 * EAP proxy - dummy implementation for build testing
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_proxy.h"

struct eap_proxy_sm *
eap_proxy_init(void *eapol_ctx, const struct eapol_callbacks *eapol_cb,
	       void *msg_ctx)
{
	return NULL;
}


void eap_proxy_deinit(struct eap_proxy_sm *eap_proxy)
{
}


int eap_proxy_key_available(struct eap_proxy_sm *sm)
{
	return 0;
}


const u8 * eap_proxy_get_eapKeyData(struct eap_proxy_sm *sm, size_t *len)
{
	return NULL;
}


struct wpabuf * eap_proxy_get_eapRespData(struct eap_proxy_sm *sm)
{
	return NULL;
}


int eap_proxy_sm_step(struct eap_proxy_sm *sm, struct eap_sm *eap_sm)
{
	return 0;
}


enum eap_proxy_status
eap_proxy_packet_update(struct eap_proxy_sm *eap_proxy, u8 *eapReqData,
			int eapReqDataLen)
{
	return EAP_PROXY_FAILURE;
}


int eap_proxy_sm_get_status(struct eap_proxy_sm *sm, char *buf, size_t buflen,
			    int verbose)
{
	return 0;
}


int eap_proxy_get_imsi(struct eap_proxy_sm *eap_proxy, int sim_num,
		       char *imsi_buf, size_t *imsi_len)
{
	return -1;
}


int eap_proxy_notify_config(struct eap_proxy_sm *sm,
			    struct eap_peer_config *config)
{
	return -1;
}


u8 * eap_proxy_get_eap_session_id(struct eap_proxy_sm *sm, size_t *len)
{
	return NULL;
}


u8 * eap_proxy_get_emsk(struct eap_proxy_sm *sm, size_t *len)
{
	return NULL;
}


void eap_proxy_sm_abort(struct eap_proxy_sm *sm)
{
}
