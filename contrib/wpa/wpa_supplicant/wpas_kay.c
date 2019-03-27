/*
 * IEEE 802.1X-2010 KaY Interface
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "eap_peer/eap.h"
#include "eap_peer/eap_i.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "pae/ieee802_1x_key.h"
#include "pae/ieee802_1x_kay.h"
#include "wpa_supplicant_i.h"
#include "config.h"
#include "config_ssid.h"
#include "driver_i.h"
#include "wpas_kay.h"


#define DEFAULT_KEY_LEN		16
/* secure Connectivity Association Key Name (CKN) */
#define DEFAULT_CKN_LEN		16


static int wpas_macsec_init(void *priv, struct macsec_init_params *params)
{
	return wpa_drv_macsec_init(priv, params);
}


static int wpas_macsec_deinit(void *priv)
{
	return wpa_drv_macsec_deinit(priv);
}


static int wpas_macsec_get_capability(void *priv, enum macsec_cap *cap)
{
	return wpa_drv_macsec_get_capability(priv, cap);
}


static int wpas_enable_protect_frames(void *wpa_s, Boolean enabled)
{
	return wpa_drv_enable_protect_frames(wpa_s, enabled);
}


static int wpas_enable_encrypt(void *wpa_s, Boolean enabled)
{
	return wpa_drv_enable_encrypt(wpa_s, enabled);
}


static int wpas_set_replay_protect(void *wpa_s, Boolean enabled, u32 window)
{
	return wpa_drv_set_replay_protect(wpa_s, enabled, window);
}


static int wpas_set_current_cipher_suite(void *wpa_s, u64 cs)
{
	return wpa_drv_set_current_cipher_suite(wpa_s, cs);
}


static int wpas_enable_controlled_port(void *wpa_s, Boolean enabled)
{
	return wpa_drv_enable_controlled_port(wpa_s, enabled);
}


static int wpas_get_receive_lowest_pn(void *wpa_s, struct receive_sa *sa)
{
	return wpa_drv_get_receive_lowest_pn(wpa_s, sa);
}


static int wpas_get_transmit_next_pn(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_get_transmit_next_pn(wpa_s, sa);
}


static int wpas_set_transmit_next_pn(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_set_transmit_next_pn(wpa_s, sa);
}


static unsigned int conf_offset_val(enum confidentiality_offset co)
{
	switch (co) {
	case CONFIDENTIALITY_OFFSET_30:
		return 30;
		break;
	case CONFIDENTIALITY_OFFSET_50:
		return 50;
	default:
		return 0;
	}
}


static int wpas_create_receive_sc(void *wpa_s, struct receive_sc *sc,
				  enum validate_frames vf,
				  enum confidentiality_offset co)
{
	return wpa_drv_create_receive_sc(wpa_s, sc, conf_offset_val(co), vf);
}


static int wpas_delete_receive_sc(void *wpa_s, struct receive_sc *sc)
{
	return wpa_drv_delete_receive_sc(wpa_s, sc);
}


static int wpas_create_receive_sa(void *wpa_s, struct receive_sa *sa)
{
	return wpa_drv_create_receive_sa(wpa_s, sa);
}


static int wpas_delete_receive_sa(void *wpa_s, struct receive_sa *sa)
{
	return wpa_drv_delete_receive_sa(wpa_s, sa);
}


static int wpas_enable_receive_sa(void *wpa_s, struct receive_sa *sa)
{
	return wpa_drv_enable_receive_sa(wpa_s, sa);
}


static int wpas_disable_receive_sa(void *wpa_s, struct receive_sa *sa)
{
	return wpa_drv_disable_receive_sa(wpa_s, sa);
}


static int
wpas_create_transmit_sc(void *wpa_s, struct transmit_sc *sc,
			enum confidentiality_offset co)
{
	return wpa_drv_create_transmit_sc(wpa_s, sc, conf_offset_val(co));
}


static int wpas_delete_transmit_sc(void *wpa_s, struct transmit_sc *sc)
{
	return wpa_drv_delete_transmit_sc(wpa_s, sc);
}


static int wpas_create_transmit_sa(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_create_transmit_sa(wpa_s, sa);
}


static int wpas_delete_transmit_sa(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_delete_transmit_sa(wpa_s, sa);
}


static int wpas_enable_transmit_sa(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_enable_transmit_sa(wpa_s, sa);
}


static int wpas_disable_transmit_sa(void *wpa_s, struct transmit_sa *sa)
{
	return wpa_drv_disable_transmit_sa(wpa_s, sa);
}


int ieee802_1x_alloc_kay_sm(struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	struct ieee802_1x_kay_ctx *kay_ctx;
	struct ieee802_1x_kay *res = NULL;
	enum macsec_policy policy;

	ieee802_1x_dealloc_kay_sm(wpa_s);

	if (!ssid || ssid->macsec_policy == 0)
		return 0;

	if (ssid->macsec_policy == 1) {
		if (ssid->macsec_integ_only == 1)
			policy = SHOULD_SECURE;
		else
			policy = SHOULD_ENCRYPT;
	} else {
		policy = DO_NOT_SECURE;
	}

	kay_ctx = os_zalloc(sizeof(*kay_ctx));
	if (!kay_ctx)
		return -1;

	kay_ctx->ctx = wpa_s;

	kay_ctx->macsec_init = wpas_macsec_init;
	kay_ctx->macsec_deinit = wpas_macsec_deinit;
	kay_ctx->macsec_get_capability = wpas_macsec_get_capability;
	kay_ctx->enable_protect_frames = wpas_enable_protect_frames;
	kay_ctx->enable_encrypt = wpas_enable_encrypt;
	kay_ctx->set_replay_protect = wpas_set_replay_protect;
	kay_ctx->set_current_cipher_suite = wpas_set_current_cipher_suite;
	kay_ctx->enable_controlled_port = wpas_enable_controlled_port;
	kay_ctx->get_receive_lowest_pn = wpas_get_receive_lowest_pn;
	kay_ctx->get_transmit_next_pn = wpas_get_transmit_next_pn;
	kay_ctx->set_transmit_next_pn = wpas_set_transmit_next_pn;
	kay_ctx->create_receive_sc = wpas_create_receive_sc;
	kay_ctx->delete_receive_sc = wpas_delete_receive_sc;
	kay_ctx->create_receive_sa = wpas_create_receive_sa;
	kay_ctx->delete_receive_sa = wpas_delete_receive_sa;
	kay_ctx->enable_receive_sa = wpas_enable_receive_sa;
	kay_ctx->disable_receive_sa = wpas_disable_receive_sa;
	kay_ctx->create_transmit_sc = wpas_create_transmit_sc;
	kay_ctx->delete_transmit_sc = wpas_delete_transmit_sc;
	kay_ctx->create_transmit_sa = wpas_create_transmit_sa;
	kay_ctx->delete_transmit_sa = wpas_delete_transmit_sa;
	kay_ctx->enable_transmit_sa = wpas_enable_transmit_sa;
	kay_ctx->disable_transmit_sa = wpas_disable_transmit_sa;

	res = ieee802_1x_kay_init(kay_ctx, policy, ssid->macsec_port,
				  ssid->mka_priority, wpa_s->ifname,
				  wpa_s->own_addr);
	/* ieee802_1x_kay_init() frees kay_ctx on failure */
	if (res == NULL)
		return -1;

	wpa_s->kay = res;

	return 0;
}


void ieee802_1x_dealloc_kay_sm(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s->kay)
		return;

	ieee802_1x_kay_deinit(wpa_s->kay);
	wpa_s->kay = NULL;
}


static int ieee802_1x_auth_get_session_id(struct wpa_supplicant *wpa_s,
					  const u8 *addr, u8 *sid, size_t *len)
{
	const u8 *session_id;
	size_t id_len, need_len;

	session_id = eapol_sm_get_session_id(wpa_s->eapol, &id_len);
	if (session_id == NULL) {
		wpa_printf(MSG_DEBUG,
			   "Failed to get SessionID from EAPOL state machines");
		return -1;
	}

	need_len = 1 + 2 * 32 /* random size */;
	if (need_len > id_len) {
		wpa_printf(MSG_DEBUG, "EAP Session-Id not long enough");
		return -1;
	}

	os_memcpy(sid, session_id, need_len);
	*len = need_len;

	return 0;
}


static int ieee802_1x_auth_get_msk(struct wpa_supplicant *wpa_s, const u8 *addr,
				   u8 *msk, size_t *len)
{
	u8 key[EAP_MSK_LEN];
	size_t keylen;
	struct eapol_sm *sm;
	int res;

	sm = wpa_s->eapol;
	if (sm == NULL)
		return -1;

	keylen = EAP_MSK_LEN;
	res = eapol_sm_get_key(sm, key, keylen);
	if (res) {
		wpa_printf(MSG_DEBUG,
			   "Failed to get MSK from EAPOL state machines");
		return -1;
	}

	if (keylen > *len)
		keylen = *len;
	os_memcpy(msk, key, keylen);
	*len = keylen;

	return 0;
}


void * ieee802_1x_notify_create_actor(struct wpa_supplicant *wpa_s,
				      const u8 *peer_addr)
{
	u8 *sid;
	size_t sid_len = 128;
	struct mka_key_name *ckn;
	struct mka_key *cak;
	struct mka_key *msk;
	void *res = NULL;

	if (!wpa_s->kay || wpa_s->kay->policy == DO_NOT_SECURE)
		return NULL;

	wpa_printf(MSG_DEBUG,
		   "IEEE 802.1X: External notification - Create MKA for "
		   MACSTR, MAC2STR(peer_addr));

	msk = os_zalloc(sizeof(*msk));
	sid = os_zalloc(sid_len);
	ckn = os_zalloc(sizeof(*ckn));
	cak = os_zalloc(sizeof(*cak));
	if (!msk || !sid || !ckn || !cak)
		goto fail;

	msk->len = DEFAULT_KEY_LEN;
	if (ieee802_1x_auth_get_msk(wpa_s, wpa_s->bssid, msk->key, &msk->len)) {
		wpa_printf(MSG_ERROR, "IEEE 802.1X: Could not get MSK");
		goto fail;
	}

	if (ieee802_1x_auth_get_session_id(wpa_s, wpa_s->bssid, sid, &sid_len))
	{
		wpa_printf(MSG_ERROR,
			   "IEEE 802.1X: Could not get EAP Session Id");
		goto fail;
	}

	/* Derive CAK from MSK */
	cak->len = DEFAULT_KEY_LEN;
	if (ieee802_1x_cak_128bits_aes_cmac(msk->key, wpa_s->own_addr,
					    peer_addr, cak->key)) {
		wpa_printf(MSG_ERROR,
			   "IEEE 802.1X: Deriving CAK failed");
		goto fail;
	}
	wpa_hexdump_key(MSG_DEBUG, "Derived CAK", cak->key, cak->len);

	/* Derive CKN from MSK */
	ckn->len = DEFAULT_CKN_LEN;
	if (ieee802_1x_ckn_128bits_aes_cmac(msk->key, wpa_s->own_addr,
					    peer_addr, sid, sid_len,
					    ckn->name)) {
		wpa_printf(MSG_ERROR,
			   "IEEE 802.1X: Deriving CKN failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "Derived CKN", ckn->name, ckn->len);

	res = ieee802_1x_kay_create_mka(wpa_s->kay, ckn, cak, 0,
					EAP_EXCHANGE, FALSE);

fail:
	if (msk) {
		os_memset(msk, 0, sizeof(*msk));
		os_free(msk);
	}
	os_free(sid);
	os_free(ckn);
	if (cak) {
		os_memset(cak, 0, sizeof(*cak));
		os_free(cak);
	}

	return res;
}


void * ieee802_1x_create_preshared_mka(struct wpa_supplicant *wpa_s,
				       struct wpa_ssid *ssid)
{
	struct mka_key *cak;
	struct mka_key_name *ckn;
	void *res = NULL;

	if ((ssid->mka_psk_set & MKA_PSK_SET) != MKA_PSK_SET)
		goto end;

	ckn = os_zalloc(sizeof(*ckn));
	if (!ckn)
		goto end;

	cak = os_zalloc(sizeof(*cak));
	if (!cak)
		goto free_ckn;

	if (ieee802_1x_alloc_kay_sm(wpa_s, ssid) < 0 || !wpa_s->kay)
		goto free_cak;

	if (wpa_s->kay->policy == DO_NOT_SECURE)
		goto dealloc;

	cak->len = MACSEC_CAK_LEN;
	os_memcpy(cak->key, ssid->mka_cak, cak->len);

	ckn->len = MACSEC_CKN_LEN;
	os_memcpy(ckn->name, ssid->mka_ckn, ckn->len);

	res = ieee802_1x_kay_create_mka(wpa_s->kay, ckn, cak, 0, PSK, FALSE);
	if (res)
		goto free_cak;

dealloc:
	/* Failed to create MKA */
	ieee802_1x_dealloc_kay_sm(wpa_s);
free_cak:
	os_free(cak);
free_ckn:
	os_free(ckn);
end:
	return res;
}
