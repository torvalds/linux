/*
 * WPA Supplicant - Mesh RSN routines
 * Copyright (c) 2013-2014, cozybit, Inc.  All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/sha256.h"
#include "crypto/random.h"
#include "crypto/aes.h"
#include "crypto/aes_siv.h"
#include "rsn_supp/wpa.h"
#include "ap/hostapd.h"
#include "ap/wpa_auth.h"
#include "ap/sta_info.h"
#include "ap/ieee802_11.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "wpas_glue.h"
#include "mesh_mpm.h"
#include "mesh_rsn.h"

#define MESH_AUTH_TIMEOUT 10
#define MESH_AUTH_RETRY 3

void mesh_auth_timer(void *eloop_ctx, void *user_data)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	struct sta_info *sta = user_data;
	struct hostapd_data *hapd;

	if (sta->sae->state != SAE_ACCEPTED) {
		wpa_printf(MSG_DEBUG, "AUTH: Re-authenticate with " MACSTR
			   " (attempt %d) ",
			   MAC2STR(sta->addr), sta->sae_auth_retry);
		wpa_msg(wpa_s, MSG_INFO, MESH_SAE_AUTH_FAILURE "addr=" MACSTR,
			MAC2STR(sta->addr));
		if (sta->sae_auth_retry < MESH_AUTH_RETRY) {
			mesh_rsn_auth_sae_sta(wpa_s, sta);
		} else {
			hapd = wpa_s->ifmsh->bss[0];

			if (sta->sae_auth_retry > MESH_AUTH_RETRY) {
				ap_free_sta(hapd, sta);
				return;
			}

			/* block the STA if exceeded the number of attempts */
			wpa_mesh_set_plink_state(wpa_s, sta, PLINK_BLOCKED);
			sta->sae->state = SAE_NOTHING;
			wpa_msg(wpa_s, MSG_INFO, MESH_SAE_AUTH_BLOCKED "addr="
				MACSTR " duration=%d",
				MAC2STR(sta->addr),
				hapd->conf->ap_max_inactivity);
		}
		sta->sae_auth_retry++;
	}
}


static void auth_logger(void *ctx, const u8 *addr, logger_level level,
			const char *txt)
{
	if (addr)
		wpa_printf(MSG_DEBUG, "AUTH: " MACSTR " - %s",
			   MAC2STR(addr), txt);
	else
		wpa_printf(MSG_DEBUG, "AUTH: %s", txt);
}


static const u8 *auth_get_psk(void *ctx, const u8 *addr,
			      const u8 *p2p_dev_addr, const u8 *prev_psk,
			      size_t *psk_len)
{
	struct mesh_rsn *mesh_rsn = ctx;
	struct hostapd_data *hapd = mesh_rsn->wpa_s->ifmsh->bss[0];
	struct sta_info *sta = ap_get_sta(hapd, addr);

	if (psk_len)
		*psk_len = PMK_LEN;
	wpa_printf(MSG_DEBUG, "AUTH: %s (addr=" MACSTR " prev_psk=%p)",
		   __func__, MAC2STR(addr), prev_psk);

	if (sta && sta->auth_alg == WLAN_AUTH_SAE) {
		if (!sta->sae || prev_psk)
			return NULL;
		return sta->sae->pmk;
	}

	return NULL;
}


static int auth_set_key(void *ctx, int vlan_id, enum wpa_alg alg,
			const u8 *addr, int idx, u8 *key, size_t key_len)
{
	struct mesh_rsn *mesh_rsn = ctx;
	u8 seq[6];

	os_memset(seq, 0, sizeof(seq));

	if (addr) {
		wpa_printf(MSG_DEBUG, "AUTH: %s(alg=%d addr=" MACSTR
			   " key_idx=%d)",
			   __func__, alg, MAC2STR(addr), idx);
	} else {
		wpa_printf(MSG_DEBUG, "AUTH: %s(alg=%d key_idx=%d)",
			   __func__, alg, idx);
	}
	wpa_hexdump_key(MSG_DEBUG, "AUTH: set_key - key", key, key_len);

	return wpa_drv_set_key(mesh_rsn->wpa_s, alg, addr, idx,
			       1, seq, 6, key, key_len);
}


static int auth_start_ampe(void *ctx, const u8 *addr)
{
	struct mesh_rsn *mesh_rsn = ctx;
	struct hostapd_data *hapd;
	struct sta_info *sta;

	if (mesh_rsn->wpa_s->current_ssid->mode != WPAS_MODE_MESH)
		return -1;

	hapd = mesh_rsn->wpa_s->ifmsh->bss[0];
	sta = ap_get_sta(hapd, addr);
	if (sta)
		eloop_cancel_timeout(mesh_auth_timer, mesh_rsn->wpa_s, sta);

	mesh_mpm_auth_peer(mesh_rsn->wpa_s, addr);
	return 0;
}


static int __mesh_rsn_auth_init(struct mesh_rsn *rsn, const u8 *addr,
				enum mfp_options ieee80211w)
{
	struct wpa_auth_config conf;
	static const struct wpa_auth_callbacks cb = {
		.logger = auth_logger,
		.get_psk = auth_get_psk,
		.set_key = auth_set_key,
		.start_ampe = auth_start_ampe,
	};
	u8 seq[6] = {};

	wpa_printf(MSG_DEBUG, "AUTH: Initializing group state machine");

	os_memset(&conf, 0, sizeof(conf));
	conf.wpa = WPA_PROTO_RSN;
	conf.wpa_key_mgmt = WPA_KEY_MGMT_SAE;
	conf.wpa_pairwise = rsn->pairwise_cipher;
	conf.rsn_pairwise = rsn->pairwise_cipher;
	conf.wpa_group = rsn->group_cipher;
	conf.eapol_version = 0;
	conf.wpa_group_rekey = -1;
	conf.wpa_group_update_count = 4;
	conf.wpa_pairwise_update_count = 4;
#ifdef CONFIG_IEEE80211W
	conf.ieee80211w = ieee80211w;
	if (ieee80211w != NO_MGMT_FRAME_PROTECTION)
		conf.group_mgmt_cipher = rsn->mgmt_group_cipher;
#endif /* CONFIG_IEEE80211W */

	rsn->auth = wpa_init(addr, &conf, &cb, rsn);
	if (rsn->auth == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH: wpa_init() failed");
		return -1;
	}

	/* TODO: support rekeying */
	rsn->mgtk_len = wpa_cipher_key_len(conf.wpa_group);
	if (random_get_bytes(rsn->mgtk, rsn->mgtk_len) < 0)
		return -1;
	rsn->mgtk_key_id = 1;

#ifdef CONFIG_IEEE80211W
	if (ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		rsn->igtk_len = wpa_cipher_key_len(conf.group_mgmt_cipher);
		if (random_get_bytes(rsn->igtk, rsn->igtk_len) < 0)
			return -1;
		rsn->igtk_key_id = 4;

		/* group mgmt */
		wpa_hexdump_key(MSG_DEBUG, "mesh: Own TX IGTK",
				rsn->igtk, rsn->igtk_len);
		wpa_drv_set_key(rsn->wpa_s,
				wpa_cipher_to_alg(rsn->mgmt_group_cipher), NULL,
				rsn->igtk_key_id, 1,
				seq, sizeof(seq), rsn->igtk, rsn->igtk_len);
	}
#endif /* CONFIG_IEEE80211W */

	/* group privacy / data frames */
	wpa_hexdump_key(MSG_DEBUG, "mesh: Own TX MGTK",
			rsn->mgtk, rsn->mgtk_len);
	wpa_drv_set_key(rsn->wpa_s, wpa_cipher_to_alg(rsn->group_cipher), NULL,
			rsn->mgtk_key_id, 1, seq, sizeof(seq),
			rsn->mgtk, rsn->mgtk_len);

	return 0;
}


static void mesh_rsn_deinit(struct mesh_rsn *rsn)
{
	os_memset(rsn->mgtk, 0, sizeof(rsn->mgtk));
	rsn->mgtk_len = 0;
	os_memset(rsn->igtk, 0, sizeof(rsn->igtk));
	rsn->igtk_len = 0;
	if (rsn->auth)
		wpa_deinit(rsn->auth);
}


struct mesh_rsn *mesh_rsn_auth_init(struct wpa_supplicant *wpa_s,
				    struct mesh_conf *conf)
{
	struct mesh_rsn *mesh_rsn;
	struct hostapd_data *bss = wpa_s->ifmsh->bss[0];
	const u8 *ie;
	size_t ie_len;
#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
	struct external_pmksa_cache *entry;
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */

	mesh_rsn = os_zalloc(sizeof(*mesh_rsn));
	if (mesh_rsn == NULL)
		return NULL;
	mesh_rsn->wpa_s = wpa_s;
	mesh_rsn->pairwise_cipher = conf->pairwise_cipher;
	mesh_rsn->group_cipher = conf->group_cipher;
	mesh_rsn->mgmt_group_cipher = conf->mgmt_group_cipher;

	if (__mesh_rsn_auth_init(mesh_rsn, wpa_s->own_addr,
				 conf->ieee80211w) < 0) {
		mesh_rsn_deinit(mesh_rsn);
		os_free(mesh_rsn);
		return NULL;
	}

	bss->wpa_auth = mesh_rsn->auth;

#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
	while ((entry = dl_list_last(&wpa_s->mesh_external_pmksa_cache,
				     struct external_pmksa_cache,
				     list)) != NULL) {
		int ret;

		ret = wpa_auth_pmksa_add_entry(bss->wpa_auth,
					       entry->pmksa_cache);
		dl_list_del(&entry->list);
		os_free(entry);

		if (ret < 0)
			return NULL;
	}
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */

	ie = wpa_auth_get_wpa_ie(mesh_rsn->auth, &ie_len);
	conf->rsn_ie = (u8 *) ie;
	conf->rsn_ie_len = ie_len;

	wpa_supplicant_rsn_supp_set_config(wpa_s, wpa_s->current_ssid);

	return mesh_rsn;
}


static int index_within_array(const int *array, int idx)
{
	int i;

	for (i = 0; i < idx; i++) {
		if (array[i] == -1)
			return 0;
	}

	return 1;
}


static int mesh_rsn_sae_group(struct wpa_supplicant *wpa_s,
			      struct sae_data *sae)
{
	int *groups = wpa_s->ifmsh->bss[0]->conf->sae_groups;

	/* Configuration may have changed, so validate current index */
	if (!index_within_array(groups, wpa_s->mesh_rsn->sae_group_index))
		return -1;

	for (;;) {
		int group = groups[wpa_s->mesh_rsn->sae_group_index];

		if (group <= 0)
			break;
		if (sae_set_group(sae, group) == 0) {
			wpa_dbg(wpa_s, MSG_DEBUG, "SME: Selected SAE group %d",
				sae->group);
			return 0;
		}
		wpa_s->mesh_rsn->sae_group_index++;
	}

	return -1;
}


static int mesh_rsn_build_sae_commit(struct wpa_supplicant *wpa_s,
				     struct wpa_ssid *ssid,
				     struct sta_info *sta)
{
	const char *password;

	password = ssid->sae_password;
	if (!password)
		password = ssid->passphrase;
	if (!password) {
		wpa_msg(wpa_s, MSG_DEBUG, "SAE: No password available");
		return -1;
	}

	if (mesh_rsn_sae_group(wpa_s, sta->sae) < 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "SAE: Failed to select group");
		return -1;
	}

	if (sta->sae->tmp && !sta->sae->tmp->pw_id && ssid->sae_password_id) {
		sta->sae->tmp->pw_id = os_strdup(ssid->sae_password_id);
		if (!sta->sae->tmp->pw_id)
			return -1;
	}
	return sae_prepare_commit(wpa_s->own_addr, sta->addr,
				  (u8 *) password, os_strlen(password),
				  ssid->sae_password_id,
				  sta->sae);
}


/* initiate new SAE authentication with sta */
int mesh_rsn_auth_sae_sta(struct wpa_supplicant *wpa_s,
			  struct sta_info *sta)
{
	struct hostapd_data *hapd = wpa_s->ifmsh->bss[0];
	struct wpa_ssid *ssid = wpa_s->current_ssid;
	struct rsn_pmksa_cache_entry *pmksa;
	unsigned int rnd;
	int ret;

	if (!ssid) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"AUTH: No current_ssid known to initiate new SAE");
		return -1;
	}

	if (!sta->sae) {
		sta->sae = os_zalloc(sizeof(*sta->sae));
		if (sta->sae == NULL)
			return -1;
	}

	pmksa = wpa_auth_pmksa_get(hapd->wpa_auth, sta->addr, NULL);
	if (pmksa) {
		if (!sta->wpa_sm)
			sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth,
							sta->addr, NULL);
		if (!sta->wpa_sm) {
			wpa_printf(MSG_ERROR,
				   "mesh: Failed to initialize RSN state machine");
			return -1;
		}

		wpa_printf(MSG_DEBUG,
			   "AUTH: Mesh PMKSA cache entry found for " MACSTR
			   " - try to use PMKSA caching instead of new SAE authentication",
			   MAC2STR(sta->addr));
		wpa_auth_pmksa_set_to_sm(pmksa, sta->wpa_sm, hapd->wpa_auth,
					 sta->sae->pmkid, sta->sae->pmk);
		sae_accept_sta(hapd, sta);
		sta->mesh_sae_pmksa_caching = 1;
		return 0;
	}
	sta->mesh_sae_pmksa_caching = 0;

	if (mesh_rsn_build_sae_commit(wpa_s, ssid, sta))
		return -1;

	wpa_msg(wpa_s, MSG_DEBUG,
		"AUTH: started authentication with SAE peer: " MACSTR,
		MAC2STR(sta->addr));

	ret = auth_sae_init_committed(hapd, sta);
	if (ret)
		return ret;

	eloop_cancel_timeout(mesh_auth_timer, wpa_s, sta);
	rnd = rand() % MESH_AUTH_TIMEOUT;
	eloop_register_timeout(MESH_AUTH_TIMEOUT + rnd, 0, mesh_auth_timer,
			       wpa_s, sta);
	return 0;
}


void mesh_rsn_get_pmkid(struct mesh_rsn *rsn, struct sta_info *sta, u8 *pmkid)
{
	os_memcpy(pmkid, sta->sae->pmkid, SAE_PMKID_LEN);
}


static void
mesh_rsn_derive_aek(struct mesh_rsn *rsn, struct sta_info *sta)
{
	u8 *myaddr = rsn->wpa_s->own_addr;
	u8 *peer = sta->addr;
	u8 *addr1, *addr2;
	u8 context[RSN_SELECTOR_LEN + 2 * ETH_ALEN], *ptr = context;

	/*
	 * AEK = KDF-Hash-256(PMK, "AEK Derivation", Selected AKM Suite ||
	 *       min(localMAC, peerMAC) || max(localMAC, peerMAC))
	 */
	/* Selected AKM Suite: SAE */
	RSN_SELECTOR_PUT(ptr, RSN_AUTH_KEY_MGMT_SAE);
	ptr += RSN_SELECTOR_LEN;

	if (os_memcmp(myaddr, peer, ETH_ALEN) < 0) {
		addr1 = myaddr;
		addr2 = peer;
	} else {
		addr1 = peer;
		addr2 = myaddr;
	}
	os_memcpy(ptr, addr1, ETH_ALEN);
	ptr += ETH_ALEN;
	os_memcpy(ptr, addr2, ETH_ALEN);

	sha256_prf(sta->sae->pmk, sizeof(sta->sae->pmk), "AEK Derivation",
		   context, sizeof(context), sta->aek, sizeof(sta->aek));
}


/* derive mesh temporal key from pmk */
int mesh_rsn_derive_mtk(struct wpa_supplicant *wpa_s, struct sta_info *sta)
{
	u8 *ptr;
	u8 *min, *max;
	u8 *myaddr = wpa_s->own_addr;
	u8 *peer = sta->addr;
	u8 context[2 * WPA_NONCE_LEN + 2 * 2 + RSN_SELECTOR_LEN + 2 * ETH_ALEN];

	/*
	 * MTK = KDF-Hash-Length(PMK, "Temporal Key Derivation", min(localNonce,
	 *  peerNonce) || max(localNonce, peerNonce) || min(localLinkID,
	 *  peerLinkID) || max(localLinkID, peerLinkID) || Selected AKM Suite ||
	 *  min(localMAC, peerMAC) || max(localMAC, peerMAC))
	 */
	ptr = context;
	if (os_memcmp(sta->my_nonce, sta->peer_nonce, WPA_NONCE_LEN) < 0) {
		min = sta->my_nonce;
		max = sta->peer_nonce;
	} else {
		min = sta->peer_nonce;
		max = sta->my_nonce;
	}
	os_memcpy(ptr, min, WPA_NONCE_LEN);
	ptr += WPA_NONCE_LEN;
	os_memcpy(ptr, max, WPA_NONCE_LEN);
	ptr += WPA_NONCE_LEN;

	if (sta->my_lid < sta->peer_lid) {
		WPA_PUT_LE16(ptr, sta->my_lid);
		ptr += 2;
		WPA_PUT_LE16(ptr, sta->peer_lid);
		ptr += 2;
	} else {
		WPA_PUT_LE16(ptr, sta->peer_lid);
		ptr += 2;
		WPA_PUT_LE16(ptr, sta->my_lid);
		ptr += 2;
	}

	/* Selected AKM Suite: SAE */
	RSN_SELECTOR_PUT(ptr, RSN_AUTH_KEY_MGMT_SAE);
	ptr += RSN_SELECTOR_LEN;

	if (os_memcmp(myaddr, peer, ETH_ALEN) < 0) {
		min = myaddr;
		max = peer;
	} else {
		min = peer;
		max = myaddr;
	}
	os_memcpy(ptr, min, ETH_ALEN);
	ptr += ETH_ALEN;
	os_memcpy(ptr, max, ETH_ALEN);

	sta->mtk_len = wpa_cipher_key_len(wpa_s->mesh_rsn->pairwise_cipher);
	sha256_prf(sta->sae->pmk, SAE_PMK_LEN,
		   "Temporal Key Derivation", context, sizeof(context),
		   sta->mtk, sta->mtk_len);
	return 0;
}


void mesh_rsn_init_ampe_sta(struct wpa_supplicant *wpa_s, struct sta_info *sta)
{
	if (random_get_bytes(sta->my_nonce, WPA_NONCE_LEN) < 0) {
		wpa_printf(MSG_INFO, "mesh: Failed to derive random nonce");
		/* TODO: How to handle this more cleanly? */
	}
	os_memset(sta->peer_nonce, 0, WPA_NONCE_LEN);
	mesh_rsn_derive_aek(wpa_s->mesh_rsn, sta);
}


/* insert AMPE and encrypted MIC at @ie.
 * @mesh_rsn: mesh RSN context
 * @sta: STA we're sending to
 * @cat: pointer to category code in frame header.
 * @buf: wpabuf to add encrypted AMPE and MIC to.
 * */
int mesh_rsn_protect_frame(struct mesh_rsn *rsn, struct sta_info *sta,
			   const u8 *cat, struct wpabuf *buf)
{
	struct ieee80211_ampe_ie *ampe;
	u8 const *ie = wpabuf_head_u8(buf) + wpabuf_len(buf);
	u8 *ampe_ie, *pos, *mic_payload;
	const u8 *aad[] = { rsn->wpa_s->own_addr, sta->addr, cat };
	const size_t aad_len[] = { ETH_ALEN, ETH_ALEN, ie - cat };
	int ret = 0;
	size_t len;

	len = sizeof(*ampe);
	if (cat[1] == PLINK_OPEN)
		len += rsn->mgtk_len + WPA_KEY_RSC_LEN + 4;
#ifdef CONFIG_IEEE80211W
	if (cat[1] == PLINK_OPEN && rsn->igtk_len)
		len += 2 + 6 + rsn->igtk_len;
#endif /* CONFIG_IEEE80211W */

	if (2 + AES_BLOCK_SIZE + 2 + len > wpabuf_tailroom(buf)) {
		wpa_printf(MSG_ERROR, "protect frame: buffer too small");
		return -EINVAL;
	}

	ampe_ie = os_zalloc(2 + len);
	if (!ampe_ie) {
		wpa_printf(MSG_ERROR, "protect frame: out of memory");
		return -ENOMEM;
	}

	/*  IE: AMPE */
	ampe_ie[0] = WLAN_EID_AMPE;
	ampe_ie[1] = len;
	ampe = (struct ieee80211_ampe_ie *) (ampe_ie + 2);

	RSN_SELECTOR_PUT(ampe->selected_pairwise_suite,
			 RSN_CIPHER_SUITE_CCMP);
	os_memcpy(ampe->local_nonce, sta->my_nonce, WPA_NONCE_LEN);
	os_memcpy(ampe->peer_nonce, sta->peer_nonce, WPA_NONCE_LEN);

	pos = (u8 *) (ampe + 1);
	if (cat[1] != PLINK_OPEN)
		goto skip_keys;

	/* TODO: Key Replay Counter[8] optionally for
	 * Mesh Group Key Inform/Acknowledge frames */

	/* TODO: static mgtk for now since we don't support rekeying! */
	/*
	 * GTKdata[variable]:
	 * MGTK[variable] || Key RSC[8] || GTKExpirationTime[4]
	 */
	os_memcpy(pos, rsn->mgtk, rsn->mgtk_len);
	pos += rsn->mgtk_len;
	wpa_drv_get_seqnum(rsn->wpa_s, NULL, rsn->mgtk_key_id, pos);
	pos += WPA_KEY_RSC_LEN;
	/* Use fixed GTKExpirationTime for now */
	WPA_PUT_LE32(pos, 0xffffffff);
	pos += 4;

#ifdef CONFIG_IEEE80211W
	/*
	 * IGTKdata[variable]:
	 * Key ID[2], IPN[6], IGTK[variable]
	 */
	if (rsn->igtk_len) {
		WPA_PUT_LE16(pos, rsn->igtk_key_id);
		pos += 2;
		wpa_drv_get_seqnum(rsn->wpa_s, NULL, rsn->igtk_key_id, pos);
		pos += 6;
		os_memcpy(pos, rsn->igtk, rsn->igtk_len);
	}
#endif /* CONFIG_IEEE80211W */

skip_keys:
	wpa_hexdump_key(MSG_DEBUG, "mesh: Plaintext AMPE element",
			ampe_ie, 2 + len);

	/* IE: MIC */
	wpabuf_put_u8(buf, WLAN_EID_MIC);
	wpabuf_put_u8(buf, AES_BLOCK_SIZE);
	/* MIC field is output ciphertext */

	/* encrypt after MIC */
	mic_payload = wpabuf_put(buf, 2 + len + AES_BLOCK_SIZE);

	if (aes_siv_encrypt(sta->aek, sizeof(sta->aek), ampe_ie, 2 + len, 3,
			    aad, aad_len, mic_payload)) {
		wpa_printf(MSG_ERROR, "protect frame: failed to encrypt");
		ret = -ENOMEM;
	}

	os_free(ampe_ie);

	return ret;
}


int mesh_rsn_process_ampe(struct wpa_supplicant *wpa_s, struct sta_info *sta,
			  struct ieee802_11_elems *elems, const u8 *cat,
			  const u8 *chosen_pmk,
			  const u8 *start, size_t elems_len)
{
	int ret = 0;
	struct ieee80211_ampe_ie *ampe;
	u8 null_nonce[WPA_NONCE_LEN] = {};
	u8 ampe_eid;
	u8 ampe_ie_len;
	u8 *ampe_buf, *crypt = NULL, *pos, *end;
	size_t crypt_len;
	const u8 *aad[] = { sta->addr, wpa_s->own_addr, cat };
	const size_t aad_len[] = { ETH_ALEN, ETH_ALEN,
				   (elems->mic - 2) - cat };
	size_t key_len;

	if (!sta->sae) {
		struct hostapd_data *hapd = wpa_s->ifmsh->bss[0];

		if (!wpa_auth_pmksa_get(hapd->wpa_auth, sta->addr, NULL)) {
			wpa_printf(MSG_INFO,
				   "Mesh RSN: SAE is not prepared yet");
			return -1;
		}
		mesh_rsn_auth_sae_sta(wpa_s, sta);
	}

	if (chosen_pmk && os_memcmp(chosen_pmk, sta->sae->pmkid, PMKID_LEN)) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Mesh RSN: Invalid PMKID (Chosen PMK did not match calculated PMKID)");
		return -1;
	}

	if (!elems->mic || elems->mic_len < AES_BLOCK_SIZE) {
		wpa_msg(wpa_s, MSG_DEBUG, "Mesh RSN: missing mic ie");
		return -1;
	}

	ampe_buf = (u8 *) elems->mic + elems->mic_len;
	if ((int) elems_len < ampe_buf - start)
		return -1;

	crypt_len = elems_len - (elems->mic - start);
	if (crypt_len < 2 + AES_BLOCK_SIZE) {
		wpa_msg(wpa_s, MSG_DEBUG, "Mesh RSN: missing ampe ie");
		return -1;
	}

	/* crypt is modified by siv_decrypt */
	crypt = os_zalloc(crypt_len);
	if (!crypt) {
		wpa_printf(MSG_ERROR, "Mesh RSN: out of memory");
		ret = -ENOMEM;
		goto free;
	}

	os_memcpy(crypt, elems->mic, crypt_len);

	if (aes_siv_decrypt(sta->aek, sizeof(sta->aek), crypt, crypt_len, 3,
			    aad, aad_len, ampe_buf)) {
		wpa_printf(MSG_ERROR, "Mesh RSN: frame verification failed!");
		ret = -2;
		goto free;
	}

	crypt_len -= AES_BLOCK_SIZE;
	wpa_hexdump_key(MSG_DEBUG, "mesh: Decrypted AMPE element",
			ampe_buf, crypt_len);

	ampe_eid = *ampe_buf++;
	ampe_ie_len = *ampe_buf++;

	if (ampe_eid != WLAN_EID_AMPE ||
	    (size_t) 2 + ampe_ie_len > crypt_len ||
	    ampe_ie_len < sizeof(struct ieee80211_ampe_ie)) {
		wpa_msg(wpa_s, MSG_DEBUG, "Mesh RSN: invalid ampe ie");
		ret = -1;
		goto free;
	}

	ampe = (struct ieee80211_ampe_ie *) ampe_buf;
	pos = (u8 *) (ampe + 1);
	end = ampe_buf + ampe_ie_len;
	if (os_memcmp(ampe->peer_nonce, null_nonce, WPA_NONCE_LEN) != 0 &&
	    os_memcmp(ampe->peer_nonce, sta->my_nonce, WPA_NONCE_LEN) != 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "Mesh RSN: invalid peer nonce");
		ret = -1;
		goto free;
	}
	os_memcpy(sta->peer_nonce, ampe->local_nonce,
		  sizeof(ampe->local_nonce));

	/* TODO: Key Replay Counter[8] in Mesh Group Key Inform/Acknowledge
	 * frames */

	/*
	 * GTKdata shall not be included in Mesh Peering Confirm. While the
	 * standard does not state the same about IGTKdata, that same constraint
	 * needs to apply for it. It makes no sense to include the keys in Mesh
	 * Peering Close frames either, so while the standard does not seem to
	 * have a shall statement for these, they are described without
	 * mentioning GTKdata.
	 *
	 * An earlier implementation used to add GTKdata to both Mesh Peering
	 * Open and Mesh Peering Confirm frames, so ignore the possibly present
	 * GTKdata frame without rejecting the frame as a backwards
	 * compatibility mechanism.
	 */
	if (cat[1] != PLINK_OPEN) {
		if (end > pos) {
			wpa_hexdump_key(MSG_DEBUG,
					"mesh: Ignore unexpected GTKdata(etc.) fields in the end of AMPE element in Mesh Peering Confirm/Close",
					pos, end - pos);
		}
		goto free;
	}

	/*
	 * GTKdata[variable]:
	 * MGTK[variable] || Key RSC[8] || GTKExpirationTime[4]
	 */
	sta->mgtk_key_id = 1; /* FIX: Where to get Key ID? */
	key_len = wpa_cipher_key_len(wpa_s->mesh_rsn->group_cipher);
	if ((int) key_len + WPA_KEY_RSC_LEN + 4 > end - pos) {
		wpa_dbg(wpa_s, MSG_DEBUG, "mesh: Truncated AMPE element");
		ret = -1;
		goto free;
	}
	sta->mgtk_len = key_len;
	os_memcpy(sta->mgtk, pos, sta->mgtk_len);
	wpa_hexdump_key(MSG_DEBUG, "mesh: GTKdata - MGTK",
			sta->mgtk, sta->mgtk_len);
	pos += sta->mgtk_len;
	wpa_hexdump(MSG_DEBUG, "mesh: GTKdata - MGTK - Key RSC",
		    pos, WPA_KEY_RSC_LEN);
	os_memcpy(sta->mgtk_rsc, pos, sizeof(sta->mgtk_rsc));
	pos += WPA_KEY_RSC_LEN;
	wpa_printf(MSG_DEBUG,
		   "mesh: GTKdata - MGTK - GTKExpirationTime: %u seconds",
		   WPA_GET_LE32(pos));
	pos += 4;

#ifdef CONFIG_IEEE80211W
	/*
	 * IGTKdata[variable]:
	 * Key ID[2], IPN[6], IGTK[variable]
	 */
	key_len = wpa_cipher_key_len(wpa_s->mesh_rsn->mgmt_group_cipher);
	if (end - pos >= (int) (2 + 6 + key_len)) {
		sta->igtk_key_id = WPA_GET_LE16(pos);
		wpa_printf(MSG_DEBUG, "mesh: IGTKdata - Key ID %u",
			   sta->igtk_key_id);
		pos += 2;
		os_memcpy(sta->igtk_rsc, pos, sizeof(sta->igtk_rsc));
		wpa_hexdump(MSG_DEBUG, "mesh: IGTKdata - IPN",
			    sta->igtk_rsc, sizeof(sta->igtk_rsc));
		pos += 6;
		os_memcpy(sta->igtk, pos, key_len);
		sta->igtk_len = key_len;
		wpa_hexdump_key(MSG_DEBUG, "mesh: IGTKdata - IGTK",
				sta->igtk, sta->igtk_len);
	}
#endif /* CONFIG_IEEE80211W */

free:
	os_free(crypt);
	return ret;
}
