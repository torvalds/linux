/*
 * hostapd / IEEE 802.11 Management
 * Copyright (c) 2002-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#ifndef CONFIG_NATIVE_WINDOWS

#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "crypto/sha384.h"
#include "crypto/sha512.h"
#include "crypto/random.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "common/wpa_ctrl.h"
#include "common/sae.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "p2p/p2p.h"
#include "wps/wps.h"
#include "fst/fst.h"
#include "hostapd.h"
#include "beacon.h"
#include "ieee802_11_auth.h"
#include "sta_info.h"
#include "ieee802_1x.h"
#include "wpa_auth.h"
#include "pmksa_cache_auth.h"
#include "wmm.h"
#include "ap_list.h"
#include "accounting.h"
#include "ap_config.h"
#include "ap_mlme.h"
#include "p2p_hostapd.h"
#include "ap_drv_ops.h"
#include "wnm_ap.h"
#include "hw_features.h"
#include "ieee802_11.h"
#include "dfs.h"
#include "mbo_ap.h"
#include "rrm.h"
#include "taxonomy.h"
#include "fils_hlp.h"
#include "dpp_hostapd.h"
#include "gas_query_ap.h"


#ifdef CONFIG_FILS
static struct wpabuf *
prepare_auth_resp_fils(struct hostapd_data *hapd,
		       struct sta_info *sta, u16 *resp,
		       struct rsn_pmksa_cache_entry *pmksa,
		       struct wpabuf *erp_resp,
		       const u8 *msk, size_t msk_len,
		       int *is_pub);
#endif /* CONFIG_FILS */

u8 * hostapd_eid_supp_rates(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	int i, num, count;

	if (hapd->iface->current_rates == NULL)
		return eid;

	*pos++ = WLAN_EID_SUPP_RATES;
	num = hapd->iface->num_rates;
	if (hapd->iconf->ieee80211n && hapd->iconf->require_ht)
		num++;
	if (hapd->iconf->ieee80211ac && hapd->iconf->require_vht)
		num++;
	if (num > 8) {
		/* rest of the rates are encoded in Extended supported
		 * rates element */
		num = 8;
	}

	*pos++ = num;
	for (i = 0, count = 0; i < hapd->iface->num_rates && count < num;
	     i++) {
		count++;
		*pos = hapd->iface->current_rates[i].rate / 5;
		if (hapd->iface->current_rates[i].flags & HOSTAPD_RATE_BASIC)
			*pos |= 0x80;
		pos++;
	}

	if (hapd->iconf->ieee80211n && hapd->iconf->require_ht && count < 8) {
		count++;
		*pos++ = 0x80 | BSS_MEMBERSHIP_SELECTOR_HT_PHY;
	}

	if (hapd->iconf->ieee80211ac && hapd->iconf->require_vht && count < 8) {
		count++;
		*pos++ = 0x80 | BSS_MEMBERSHIP_SELECTOR_VHT_PHY;
	}

	return pos;
}


u8 * hostapd_eid_ext_supp_rates(struct hostapd_data *hapd, u8 *eid)
{
	u8 *pos = eid;
	int i, num, count;

	if (hapd->iface->current_rates == NULL)
		return eid;

	num = hapd->iface->num_rates;
	if (hapd->iconf->ieee80211n && hapd->iconf->require_ht)
		num++;
	if (hapd->iconf->ieee80211ac && hapd->iconf->require_vht)
		num++;
	if (num <= 8)
		return eid;
	num -= 8;

	*pos++ = WLAN_EID_EXT_SUPP_RATES;
	*pos++ = num;
	for (i = 0, count = 0; i < hapd->iface->num_rates && count < num + 8;
	     i++) {
		count++;
		if (count <= 8)
			continue; /* already in SuppRates IE */
		*pos = hapd->iface->current_rates[i].rate / 5;
		if (hapd->iface->current_rates[i].flags & HOSTAPD_RATE_BASIC)
			*pos |= 0x80;
		pos++;
	}

	if (hapd->iconf->ieee80211n && hapd->iconf->require_ht) {
		count++;
		if (count > 8)
			*pos++ = 0x80 | BSS_MEMBERSHIP_SELECTOR_HT_PHY;
	}

	if (hapd->iconf->ieee80211ac && hapd->iconf->require_vht) {
		count++;
		if (count > 8)
			*pos++ = 0x80 | BSS_MEMBERSHIP_SELECTOR_VHT_PHY;
	}

	return pos;
}


u16 hostapd_own_capab_info(struct hostapd_data *hapd)
{
	int capab = WLAN_CAPABILITY_ESS;
	int privacy;
	int dfs;
	int i;

	/* Check if any of configured channels require DFS */
	dfs = hostapd_is_dfs_required(hapd->iface);
	if (dfs < 0) {
		wpa_printf(MSG_WARNING, "Failed to check if DFS is required; ret=%d",
			   dfs);
		dfs = 0;
	}

	if (hapd->iface->num_sta_no_short_preamble == 0 &&
	    hapd->iconf->preamble == SHORT_PREAMBLE)
		capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;

	privacy = hapd->conf->ssid.wep.keys_set;

	if (hapd->conf->ieee802_1x &&
	    (hapd->conf->default_wep_key_len ||
	     hapd->conf->individual_wep_key_len))
		privacy = 1;

	if (hapd->conf->wpa)
		privacy = 1;

#ifdef CONFIG_HS20
	if (hapd->conf->osen)
		privacy = 1;
#endif /* CONFIG_HS20 */

	if (privacy)
		capab |= WLAN_CAPABILITY_PRIVACY;

	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G &&
	    hapd->iface->num_sta_no_short_slot_time == 0)
		capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;

	/*
	 * Currently, Spectrum Management capability bit is set when directly
	 * requested in configuration by spectrum_mgmt_required or when AP is
	 * running on DFS channel.
	 * TODO: Also consider driver support for TPC to set Spectrum Mgmt bit
	 */
	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211A &&
	    (hapd->iconf->spectrum_mgmt_required || dfs))
		capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	for (i = 0; i < RRM_CAPABILITIES_IE_LEN; i++) {
		if (hapd->conf->radio_measurements[i]) {
			capab |= IEEE80211_CAP_RRM;
			break;
		}
	}

	return capab;
}


#ifndef CONFIG_NO_RC4
static u16 auth_shared_key(struct hostapd_data *hapd, struct sta_info *sta,
			   u16 auth_transaction, const u8 *challenge,
			   int iswep)
{
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "authentication (shared key, transaction %d)",
		       auth_transaction);

	if (auth_transaction == 1) {
		if (!sta->challenge) {
			/* Generate a pseudo-random challenge */
			u8 key[8];

			sta->challenge = os_zalloc(WLAN_AUTH_CHALLENGE_LEN);
			if (sta->challenge == NULL)
				return WLAN_STATUS_UNSPECIFIED_FAILURE;

			if (os_get_random(key, sizeof(key)) < 0) {
				os_free(sta->challenge);
				sta->challenge = NULL;
				return WLAN_STATUS_UNSPECIFIED_FAILURE;
			}

			rc4_skip(key, sizeof(key), 0,
				 sta->challenge, WLAN_AUTH_CHALLENGE_LEN);
		}
		return 0;
	}

	if (auth_transaction != 3)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	/* Transaction 3 */
	if (!iswep || !sta->challenge || !challenge ||
	    os_memcmp_const(sta->challenge, challenge,
			    WLAN_AUTH_CHALLENGE_LEN)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO,
			       "shared key authentication - invalid "
			       "challenge-response");
		return WLAN_STATUS_CHALLENGE_FAIL;
	}

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "authentication OK (shared key)");
	sta->flags |= WLAN_STA_AUTH;
	wpa_auth_sm_event(sta->wpa_sm, WPA_AUTH);
	os_free(sta->challenge);
	sta->challenge = NULL;

	return 0;
}
#endif /* CONFIG_NO_RC4 */


static int send_auth_reply(struct hostapd_data *hapd,
			   const u8 *dst, const u8 *bssid,
			   u16 auth_alg, u16 auth_transaction, u16 resp,
			   const u8 *ies, size_t ies_len, const char *dbg)
{
	struct ieee80211_mgmt *reply;
	u8 *buf;
	size_t rlen;
	int reply_res = WLAN_STATUS_UNSPECIFIED_FAILURE;

	rlen = IEEE80211_HDRLEN + sizeof(reply->u.auth) + ies_len;
	buf = os_zalloc(rlen);
	if (buf == NULL)
		return -1;

	reply = (struct ieee80211_mgmt *) buf;
	reply->frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					    WLAN_FC_STYPE_AUTH);
	os_memcpy(reply->da, dst, ETH_ALEN);
	os_memcpy(reply->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(reply->bssid, bssid, ETH_ALEN);

	reply->u.auth.auth_alg = host_to_le16(auth_alg);
	reply->u.auth.auth_transaction = host_to_le16(auth_transaction);
	reply->u.auth.status_code = host_to_le16(resp);

	if (ies && ies_len)
		os_memcpy(reply->u.auth.variable, ies, ies_len);

	wpa_printf(MSG_DEBUG, "authentication reply: STA=" MACSTR
		   " auth_alg=%d auth_transaction=%d resp=%d (IE len=%lu) (dbg=%s)",
		   MAC2STR(dst), auth_alg, auth_transaction,
		   resp, (unsigned long) ies_len, dbg);
	if (hostapd_drv_send_mlme(hapd, reply, rlen, 0) < 0)
		wpa_printf(MSG_INFO, "send_auth_reply: send failed");
	else
		reply_res = WLAN_STATUS_SUCCESS;

	os_free(buf);

	return reply_res;
}


#ifdef CONFIG_IEEE80211R_AP
static void handle_auth_ft_finish(void *ctx, const u8 *dst, const u8 *bssid,
				  u16 auth_transaction, u16 status,
				  const u8 *ies, size_t ies_len)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	int reply_res;

	reply_res = send_auth_reply(hapd, dst, bssid, WLAN_AUTH_FT,
				    auth_transaction, status, ies, ies_len,
				    "auth-ft-finish");

	sta = ap_get_sta(hapd, dst);
	if (sta == NULL)
		return;

	if (sta->added_unassoc && (reply_res != WLAN_STATUS_SUCCESS ||
				   status != WLAN_STATUS_SUCCESS)) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->added_unassoc = 0;
		return;
	}

	if (status != WLAN_STATUS_SUCCESS)
		return;

	hostapd_logger(hapd, dst, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG, "authentication OK (FT)");
	sta->flags |= WLAN_STA_AUTH;
	mlme_authenticate_indication(hapd, sta);
}
#endif /* CONFIG_IEEE80211R_AP */


#ifdef CONFIG_SAE

static void sae_set_state(struct sta_info *sta, enum sae_state state,
			  const char *reason)
{
	wpa_printf(MSG_DEBUG, "SAE: State %s -> %s for peer " MACSTR " (%s)",
		   sae_state_txt(sta->sae->state), sae_state_txt(state),
		   MAC2STR(sta->addr), reason);
	sta->sae->state = state;
}


static struct wpabuf * auth_build_sae_commit(struct hostapd_data *hapd,
					     struct sta_info *sta, int update)
{
	struct wpabuf *buf;
	const char *password = NULL;
	struct sae_password_entry *pw;
	const char *rx_id = NULL;

	if (sta->sae->tmp)
		rx_id = sta->sae->tmp->pw_id;

	for (pw = hapd->conf->sae_passwords; pw; pw = pw->next) {
		if (!is_broadcast_ether_addr(pw->peer_addr) &&
		    os_memcmp(pw->peer_addr, sta->addr, ETH_ALEN) != 0)
			continue;
		if ((rx_id && !pw->identifier) || (!rx_id && pw->identifier))
			continue;
		if (rx_id && pw->identifier &&
		    os_strcmp(rx_id, pw->identifier) != 0)
			continue;
		password = pw->password;
		break;
	}
	if (!password)
		password = hapd->conf->ssid.wpa_passphrase;
	if (!password) {
		wpa_printf(MSG_DEBUG, "SAE: No password available");
		return NULL;
	}

	if (update &&
	    sae_prepare_commit(hapd->own_addr, sta->addr,
			       (u8 *) password, os_strlen(password), rx_id,
			       sta->sae) < 0) {
		wpa_printf(MSG_DEBUG, "SAE: Could not pick PWE");
		return NULL;
	}

	buf = wpabuf_alloc(SAE_COMMIT_MAX_LEN +
			   (rx_id ? 3 + os_strlen(rx_id) : 0));
	if (buf == NULL)
		return NULL;
	sae_write_commit(sta->sae, buf, sta->sae->tmp ?
			 sta->sae->tmp->anti_clogging_token : NULL, rx_id);

	return buf;
}


static struct wpabuf * auth_build_sae_confirm(struct hostapd_data *hapd,
					      struct sta_info *sta)
{
	struct wpabuf *buf;

	buf = wpabuf_alloc(SAE_CONFIRM_MAX_LEN);
	if (buf == NULL)
		return NULL;

	sae_write_confirm(sta->sae, buf);

	return buf;
}


static int auth_sae_send_commit(struct hostapd_data *hapd,
				struct sta_info *sta,
				const u8 *bssid, int update)
{
	struct wpabuf *data;
	int reply_res;

	data = auth_build_sae_commit(hapd, sta, update);
	if (!data && sta->sae->tmp && sta->sae->tmp->pw_id)
		return WLAN_STATUS_UNKNOWN_PASSWORD_IDENTIFIER;
	if (data == NULL)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	reply_res = send_auth_reply(hapd, sta->addr, bssid, WLAN_AUTH_SAE, 1,
				    WLAN_STATUS_SUCCESS, wpabuf_head(data),
				    wpabuf_len(data), "sae-send-commit");

	wpabuf_free(data);

	return reply_res;
}


static int auth_sae_send_confirm(struct hostapd_data *hapd,
				 struct sta_info *sta,
				 const u8 *bssid)
{
	struct wpabuf *data;
	int reply_res;

	data = auth_build_sae_confirm(hapd, sta);
	if (data == NULL)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	reply_res = send_auth_reply(hapd, sta->addr, bssid, WLAN_AUTH_SAE, 2,
				    WLAN_STATUS_SUCCESS, wpabuf_head(data),
				    wpabuf_len(data), "sae-send-confirm");

	wpabuf_free(data);

	return reply_res;
}


static int use_sae_anti_clogging(struct hostapd_data *hapd)
{
	struct sta_info *sta;
	unsigned int open = 0;

	if (hapd->conf->sae_anti_clogging_threshold == 0)
		return 1;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (!sta->sae)
			continue;
		if (sta->sae->state != SAE_COMMITTED &&
		    sta->sae->state != SAE_CONFIRMED)
			continue;
		open++;
		if (open >= hapd->conf->sae_anti_clogging_threshold)
			return 1;
	}

	return 0;
}


static int check_sae_token(struct hostapd_data *hapd, const u8 *addr,
			   const u8 *token, size_t token_len)
{
	u8 mac[SHA256_MAC_LEN];

	if (token_len != SHA256_MAC_LEN)
		return -1;
	if (hmac_sha256(hapd->sae_token_key, sizeof(hapd->sae_token_key),
			addr, ETH_ALEN, mac) < 0 ||
	    os_memcmp_const(token, mac, SHA256_MAC_LEN) != 0)
		return -1;

	return 0;
}


static struct wpabuf * auth_build_token_req(struct hostapd_data *hapd,
					    int group, const u8 *addr)
{
	struct wpabuf *buf;
	u8 *token;
	struct os_reltime now;

	os_get_reltime(&now);
	if (!os_reltime_initialized(&hapd->last_sae_token_key_update) ||
	    os_reltime_expired(&now, &hapd->last_sae_token_key_update, 60)) {
		if (random_get_bytes(hapd->sae_token_key,
				     sizeof(hapd->sae_token_key)) < 0)
			return NULL;
		wpa_hexdump(MSG_DEBUG, "SAE: Updated token key",
			    hapd->sae_token_key, sizeof(hapd->sae_token_key));
		hapd->last_sae_token_key_update = now;
	}

	buf = wpabuf_alloc(sizeof(le16) + SHA256_MAC_LEN);
	if (buf == NULL)
		return NULL;

	wpabuf_put_le16(buf, group); /* Finite Cyclic Group */

	token = wpabuf_put(buf, SHA256_MAC_LEN);
	hmac_sha256(hapd->sae_token_key, sizeof(hapd->sae_token_key),
		    addr, ETH_ALEN, token);

	return buf;
}


static int sae_check_big_sync(struct hostapd_data *hapd, struct sta_info *sta)
{
	if (sta->sae->sync > hapd->conf->sae_sync) {
		sae_set_state(sta, SAE_NOTHING, "Sync > dot11RSNASAESync");
		sta->sae->sync = 0;
		return -1;
	}
	return 0;
}


static void auth_sae_retransmit_timer(void *eloop_ctx, void *eloop_data)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = eloop_data;
	int ret;

	if (sae_check_big_sync(hapd, sta))
		return;
	sta->sae->sync++;
	wpa_printf(MSG_DEBUG, "SAE: Auth SAE retransmit timer for " MACSTR
		   " (sync=%d state=%s)",
		   MAC2STR(sta->addr), sta->sae->sync,
		   sae_state_txt(sta->sae->state));

	switch (sta->sae->state) {
	case SAE_COMMITTED:
		ret = auth_sae_send_commit(hapd, sta, hapd->own_addr, 0);
		eloop_register_timeout(0,
				       hapd->dot11RSNASAERetransPeriod * 1000,
				       auth_sae_retransmit_timer, hapd, sta);
		break;
	case SAE_CONFIRMED:
		ret = auth_sae_send_confirm(hapd, sta, hapd->own_addr);
		eloop_register_timeout(0,
				       hapd->dot11RSNASAERetransPeriod * 1000,
				       auth_sae_retransmit_timer, hapd, sta);
		break;
	default:
		ret = -1;
		break;
	}

	if (ret != WLAN_STATUS_SUCCESS)
		wpa_printf(MSG_INFO, "SAE: Failed to retransmit: ret=%d", ret);
}


void sae_clear_retransmit_timer(struct hostapd_data *hapd, struct sta_info *sta)
{
	eloop_cancel_timeout(auth_sae_retransmit_timer, hapd, sta);
}


static void sae_set_retransmit_timer(struct hostapd_data *hapd,
				     struct sta_info *sta)
{
	if (!(hapd->conf->mesh & MESH_ENABLED))
		return;

	eloop_cancel_timeout(auth_sae_retransmit_timer, hapd, sta);
	eloop_register_timeout(0, hapd->dot11RSNASAERetransPeriod * 1000,
			       auth_sae_retransmit_timer, hapd, sta);
}


void sae_accept_sta(struct hostapd_data *hapd, struct sta_info *sta)
{
	sta->flags |= WLAN_STA_AUTH;
	sta->auth_alg = WLAN_AUTH_SAE;
	mlme_authenticate_indication(hapd, sta);
	wpa_auth_sm_event(sta->wpa_sm, WPA_AUTH);
	sae_set_state(sta, SAE_ACCEPTED, "Accept Confirm");
	wpa_auth_pmksa_add_sae(hapd->wpa_auth, sta->addr,
			       sta->sae->pmk, sta->sae->pmkid);
}


static int sae_sm_step(struct hostapd_data *hapd, struct sta_info *sta,
		       const u8 *bssid, u8 auth_transaction)
{
	int ret;

	if (auth_transaction != 1 && auth_transaction != 2)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	wpa_printf(MSG_DEBUG, "SAE: Peer " MACSTR " state=%s auth_trans=%u",
		   MAC2STR(sta->addr), sae_state_txt(sta->sae->state),
		   auth_transaction);
	switch (sta->sae->state) {
	case SAE_NOTHING:
		if (auth_transaction == 1) {
			ret = auth_sae_send_commit(hapd, sta, bssid, 1);
			if (ret)
				return ret;
			sae_set_state(sta, SAE_COMMITTED, "Sent Commit");

			if (sae_process_commit(sta->sae) < 0)
				return WLAN_STATUS_UNSPECIFIED_FAILURE;

			/*
			 * In mesh case, both Commit and Confirm can be sent
			 * immediately. In infrastructure BSS, only a single
			 * Authentication frame (Commit) is expected from the AP
			 * here and the second one (Confirm) will be sent once
			 * the STA has sent its second Authentication frame
			 * (Confirm).
			 */
			if (hapd->conf->mesh & MESH_ENABLED) {
				/*
				 * Send both Commit and Confirm immediately
				 * based on SAE finite state machine
				 * Nothing -> Confirm transition.
				 */
				ret = auth_sae_send_confirm(hapd, sta, bssid);
				if (ret)
					return ret;
				sae_set_state(sta, SAE_CONFIRMED,
					      "Sent Confirm (mesh)");
			} else {
				/*
				 * For infrastructure BSS, send only the Commit
				 * message now to get alternating sequence of
				 * Authentication frames between the AP and STA.
				 * Confirm will be sent in
				 * Committed -> Confirmed/Accepted transition
				 * when receiving Confirm from STA.
				 */
			}
			sta->sae->sync = 0;
			sae_set_retransmit_timer(hapd, sta);
		} else {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG,
				       "SAE confirm before commit");
		}
		break;
	case SAE_COMMITTED:
		sae_clear_retransmit_timer(hapd, sta);
		if (auth_transaction == 1) {
			if (sae_process_commit(sta->sae) < 0)
				return WLAN_STATUS_UNSPECIFIED_FAILURE;

			ret = auth_sae_send_confirm(hapd, sta, bssid);
			if (ret)
				return ret;
			sae_set_state(sta, SAE_CONFIRMED, "Sent Confirm");
			sta->sae->sync = 0;
			sae_set_retransmit_timer(hapd, sta);
		} else if (hapd->conf->mesh & MESH_ENABLED) {
			/*
			 * In mesh case, follow SAE finite state machine and
			 * send Commit now, if sync count allows.
			 */
			if (sae_check_big_sync(hapd, sta))
				return WLAN_STATUS_SUCCESS;
			sta->sae->sync++;

			ret = auth_sae_send_commit(hapd, sta, bssid, 0);
			if (ret)
				return ret;

			sae_set_retransmit_timer(hapd, sta);
		} else {
			/*
			 * For instructure BSS, send the postponed Confirm from
			 * Nothing -> Confirmed transition that was reduced to
			 * Nothing -> Committed above.
			 */
			ret = auth_sae_send_confirm(hapd, sta, bssid);
			if (ret)
				return ret;

			sae_set_state(sta, SAE_CONFIRMED, "Sent Confirm");

			/*
			 * Since this was triggered on Confirm RX, run another
			 * step to get to Accepted without waiting for
			 * additional events.
			 */
			return sae_sm_step(hapd, sta, bssid, auth_transaction);
		}
		break;
	case SAE_CONFIRMED:
		sae_clear_retransmit_timer(hapd, sta);
		if (auth_transaction == 1) {
			if (sae_check_big_sync(hapd, sta))
				return WLAN_STATUS_SUCCESS;
			sta->sae->sync++;

			ret = auth_sae_send_commit(hapd, sta, bssid, 1);
			if (ret)
				return ret;

			if (sae_process_commit(sta->sae) < 0)
				return WLAN_STATUS_UNSPECIFIED_FAILURE;

			ret = auth_sae_send_confirm(hapd, sta, bssid);
			if (ret)
				return ret;

			sae_set_retransmit_timer(hapd, sta);
		} else {
			sta->sae->send_confirm = 0xffff;
			sae_accept_sta(hapd, sta);
		}
		break;
	case SAE_ACCEPTED:
		if (auth_transaction == 1 &&
		    (hapd->conf->mesh & MESH_ENABLED)) {
			wpa_printf(MSG_DEBUG, "SAE: remove the STA (" MACSTR
				   ") doing reauthentication",
				   MAC2STR(sta->addr));
			ap_free_sta(hapd, sta);
			wpa_auth_pmksa_remove(hapd->wpa_auth, sta->addr);
		} else if (auth_transaction == 1) {
			wpa_printf(MSG_DEBUG, "SAE: Start reauthentication");
			ret = auth_sae_send_commit(hapd, sta, bssid, 1);
			if (ret)
				return ret;
			sae_set_state(sta, SAE_COMMITTED, "Sent Commit");

			if (sae_process_commit(sta->sae) < 0)
				return WLAN_STATUS_UNSPECIFIED_FAILURE;
			sta->sae->sync = 0;
			sae_set_retransmit_timer(hapd, sta);
		} else {
			if (sae_check_big_sync(hapd, sta))
				return WLAN_STATUS_SUCCESS;
			sta->sae->sync++;

			ret = auth_sae_send_confirm(hapd, sta, bssid);
			sae_clear_temp_data(sta->sae);
			if (ret)
				return ret;
		}
		break;
	default:
		wpa_printf(MSG_ERROR, "SAE: invalid state %d",
			   sta->sae->state);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}
	return WLAN_STATUS_SUCCESS;
}


static void sae_pick_next_group(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct sae_data *sae = sta->sae;
	int i, *groups = hapd->conf->sae_groups;

	if (sae->state != SAE_COMMITTED)
		return;

	wpa_printf(MSG_DEBUG, "SAE: Previously selected group: %d", sae->group);

	for (i = 0; groups && groups[i] > 0; i++) {
		if (sae->group == groups[i])
			break;
	}

	if (!groups || groups[i] <= 0) {
		wpa_printf(MSG_DEBUG,
			   "SAE: Previously selected group not found from the current configuration");
		return;
	}

	for (;;) {
		i++;
		if (groups[i] <= 0) {
			wpa_printf(MSG_DEBUG,
				   "SAE: No alternative group enabled");
			return;
		}

		if (sae_set_group(sae, groups[i]) < 0)
			continue;

		break;
	}
	wpa_printf(MSG_DEBUG, "SAE: Selected new group: %d", groups[i]);
}


static void handle_auth_sae(struct hostapd_data *hapd, struct sta_info *sta,
			    const struct ieee80211_mgmt *mgmt, size_t len,
			    u16 auth_transaction, u16 status_code)
{
	int resp = WLAN_STATUS_SUCCESS;
	struct wpabuf *data = NULL;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->conf->sae_reflection_attack && auth_transaction == 1) {
		const u8 *pos, *end;

		wpa_printf(MSG_DEBUG, "SAE: TESTING - reflection attack");
		pos = mgmt->u.auth.variable;
		end = ((const u8 *) mgmt) + len;
		send_auth_reply(hapd, mgmt->sa, mgmt->bssid, WLAN_AUTH_SAE,
				auth_transaction, resp, pos, end - pos,
				"auth-sae-reflection-attack");
		goto remove_sta;
	}

	if (hapd->conf->sae_commit_override && auth_transaction == 1) {
		wpa_printf(MSG_DEBUG, "SAE: TESTING - commit override");
		send_auth_reply(hapd, mgmt->sa, mgmt->bssid, WLAN_AUTH_SAE,
				auth_transaction, resp,
				wpabuf_head(hapd->conf->sae_commit_override),
				wpabuf_len(hapd->conf->sae_commit_override),
				"sae-commit-override");
		goto remove_sta;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	if (!sta->sae) {
		if (auth_transaction != 1 ||
		    status_code != WLAN_STATUS_SUCCESS) {
			resp = -1;
			goto remove_sta;
		}
		sta->sae = os_zalloc(sizeof(*sta->sae));
		if (!sta->sae) {
			resp = -1;
			goto remove_sta;
		}
		sae_set_state(sta, SAE_NOTHING, "Init");
		sta->sae->sync = 0;
	}

	if (sta->mesh_sae_pmksa_caching) {
		wpa_printf(MSG_DEBUG,
			   "SAE: Cancel use of mesh PMKSA caching because peer starts SAE authentication");
		wpa_auth_pmksa_remove(hapd->wpa_auth, sta->addr);
		sta->mesh_sae_pmksa_caching = 0;
	}

	if (auth_transaction == 1) {
		const u8 *token = NULL, *pos, *end;
		size_t token_len = 0;
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "start SAE authentication (RX commit, status=%u)",
			       status_code);

		if ((hapd->conf->mesh & MESH_ENABLED) &&
		    status_code == WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ &&
		    sta->sae->tmp) {
			pos = mgmt->u.auth.variable;
			end = ((const u8 *) mgmt) + len;
			if (pos + sizeof(le16) > end) {
				wpa_printf(MSG_ERROR,
					   "SAE: Too short anti-clogging token request");
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto reply;
			}
			resp = sae_group_allowed(sta->sae,
						 hapd->conf->sae_groups,
						 WPA_GET_LE16(pos));
			if (resp != WLAN_STATUS_SUCCESS) {
				wpa_printf(MSG_ERROR,
					   "SAE: Invalid group in anti-clogging token request");
				goto reply;
			}
			pos += sizeof(le16);

			wpabuf_free(sta->sae->tmp->anti_clogging_token);
			sta->sae->tmp->anti_clogging_token =
				wpabuf_alloc_copy(pos, end - pos);
			if (sta->sae->tmp->anti_clogging_token == NULL) {
				wpa_printf(MSG_ERROR,
					   "SAE: Failed to alloc for anti-clogging token");
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto remove_sta;
			}

			/*
			 * IEEE Std 802.11-2012, 11.3.8.6.4: If the Status code
			 * is 76, a new Commit Message shall be constructed
			 * with the Anti-Clogging Token from the received
			 * Authentication frame, and the commit-scalar and
			 * COMMIT-ELEMENT previously sent.
			 */
			resp = auth_sae_send_commit(hapd, sta, mgmt->bssid, 0);
			if (resp != WLAN_STATUS_SUCCESS) {
				wpa_printf(MSG_ERROR,
					   "SAE: Failed to send commit message");
				goto remove_sta;
			}
			sae_set_state(sta, SAE_COMMITTED,
				      "Sent Commit (anti-clogging token case in mesh)");
			sta->sae->sync = 0;
			sae_set_retransmit_timer(hapd, sta);
			return;
		}

		if ((hapd->conf->mesh & MESH_ENABLED) &&
		    status_code ==
		    WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED &&
		    sta->sae->tmp) {
			wpa_printf(MSG_DEBUG,
				   "SAE: Peer did not accept our SAE group");
			sae_pick_next_group(hapd, sta);
			goto remove_sta;
		}

		if (status_code != WLAN_STATUS_SUCCESS)
			goto remove_sta;

		if (!(hapd->conf->mesh & MESH_ENABLED) &&
		    sta->sae->state == SAE_COMMITTED) {
			/* This is needed in the infrastructure BSS case to
			 * address a sequence where a STA entry may remain in
			 * hostapd across two attempts to do SAE authentication
			 * by the same STA. The second attempt may end up trying
			 * to use a different group and that would not be
			 * allowed if we remain in Committed state with the
			 * previously set parameters. */
			sae_set_state(sta, SAE_NOTHING,
				      "Clear existing state to allow restart");
			sae_clear_data(sta->sae);
		}

		resp = sae_parse_commit(sta->sae, mgmt->u.auth.variable,
					((const u8 *) mgmt) + len -
					mgmt->u.auth.variable, &token,
					&token_len, hapd->conf->sae_groups);
		if (resp == SAE_SILENTLY_DISCARD) {
			wpa_printf(MSG_DEBUG,
				   "SAE: Drop commit message from " MACSTR " due to reflection attack",
				   MAC2STR(sta->addr));
			goto remove_sta;
		}

		if (resp == WLAN_STATUS_UNKNOWN_PASSWORD_IDENTIFIER) {
			wpa_msg(hapd->msg_ctx, MSG_INFO,
				WPA_EVENT_SAE_UNKNOWN_PASSWORD_IDENTIFIER
				MACSTR, MAC2STR(sta->addr));
			sae_clear_retransmit_timer(hapd, sta);
			sae_set_state(sta, SAE_NOTHING,
				      "Unknown Password Identifier");
			goto remove_sta;
		}

		if (token && check_sae_token(hapd, sta->addr, token, token_len)
		    < 0) {
			wpa_printf(MSG_DEBUG, "SAE: Drop commit message with "
				   "incorrect token from " MACSTR,
				   MAC2STR(sta->addr));
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto remove_sta;
		}

		if (resp != WLAN_STATUS_SUCCESS)
			goto reply;

		if (!token && use_sae_anti_clogging(hapd)) {
			wpa_printf(MSG_DEBUG,
				   "SAE: Request anti-clogging token from "
				   MACSTR, MAC2STR(sta->addr));
			data = auth_build_token_req(hapd, sta->sae->group,
						    sta->addr);
			resp = WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ;
			if (hapd->conf->mesh & MESH_ENABLED)
				sae_set_state(sta, SAE_NOTHING,
					      "Request anti-clogging token case in mesh");
			goto reply;
		}

		resp = sae_sm_step(hapd, sta, mgmt->bssid, auth_transaction);
	} else if (auth_transaction == 2) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "SAE authentication (RX confirm, status=%u)",
			       status_code);
		if (status_code != WLAN_STATUS_SUCCESS)
			goto remove_sta;
		if (sta->sae->state >= SAE_CONFIRMED ||
		    !(hapd->conf->mesh & MESH_ENABLED)) {
			const u8 *var;
			size_t var_len;
			u16 peer_send_confirm;

			var = mgmt->u.auth.variable;
			var_len = ((u8 *) mgmt) + len - mgmt->u.auth.variable;
			if (var_len < 2) {
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto reply;
			}

			peer_send_confirm = WPA_GET_LE16(var);

			if (sta->sae->state == SAE_ACCEPTED &&
			    (peer_send_confirm <= sta->sae->rc ||
			     peer_send_confirm == 0xffff)) {
				wpa_printf(MSG_DEBUG,
					   "SAE: Silently ignore unexpected Confirm from peer "
					   MACSTR
					   " (peer-send-confirm=%u Rc=%u)",
					   MAC2STR(sta->addr),
					   peer_send_confirm, sta->sae->rc);
				return;
			}

			if (sae_check_confirm(sta->sae, var, var_len) < 0) {
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto reply;
			}
			sta->sae->rc = peer_send_confirm;
		}
		resp = sae_sm_step(hapd, sta, mgmt->bssid, auth_transaction);
	} else {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "unexpected SAE authentication transaction %u (status=%u)",
			       auth_transaction, status_code);
		if (status_code != WLAN_STATUS_SUCCESS)
			goto remove_sta;
		resp = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
	}

reply:
	if (resp != WLAN_STATUS_SUCCESS) {
		send_auth_reply(hapd, mgmt->sa, mgmt->bssid, WLAN_AUTH_SAE,
				auth_transaction, resp,
				data ? wpabuf_head(data) : (u8 *) "",
				data ? wpabuf_len(data) : 0, "auth-sae");
	}

remove_sta:
	if (sta->added_unassoc && (resp != WLAN_STATUS_SUCCESS ||
				   status_code != WLAN_STATUS_SUCCESS)) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->added_unassoc = 0;
	}
	wpabuf_free(data);
}


/**
 * auth_sae_init_committed - Send COMMIT and start SAE in committed state
 * @hapd: BSS data for the device initiating the authentication
 * @sta: the peer to which commit authentication frame is sent
 *
 * This function implements Init event handling (IEEE Std 802.11-2012,
 * 11.3.8.6.3) in which initial COMMIT message is sent. Prior to calling, the
 * sta->sae structure should be initialized appropriately via a call to
 * sae_prepare_commit().
 */
int auth_sae_init_committed(struct hostapd_data *hapd, struct sta_info *sta)
{
	int ret;

	if (!sta->sae || !sta->sae->tmp)
		return -1;

	if (sta->sae->state != SAE_NOTHING)
		return -1;

	ret = auth_sae_send_commit(hapd, sta, hapd->own_addr, 0);
	if (ret)
		return -1;

	sae_set_state(sta, SAE_COMMITTED, "Init and sent commit");
	sta->sae->sync = 0;
	sae_set_retransmit_timer(hapd, sta);

	return 0;
}

#endif /* CONFIG_SAE */


static u16 wpa_res_to_status_code(int res)
{
	if (res == WPA_INVALID_GROUP)
		return WLAN_STATUS_GROUP_CIPHER_NOT_VALID;
	if (res == WPA_INVALID_PAIRWISE)
		return WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
	if (res == WPA_INVALID_AKMP)
		return WLAN_STATUS_AKMP_NOT_VALID;
	if (res == WPA_ALLOC_FAIL)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
#ifdef CONFIG_IEEE80211W
	if (res == WPA_MGMT_FRAME_PROTECTION_VIOLATION)
		return WLAN_STATUS_ROBUST_MGMT_FRAME_POLICY_VIOLATION;
	if (res == WPA_INVALID_MGMT_GROUP_CIPHER)
		return WLAN_STATUS_CIPHER_REJECTED_PER_POLICY;
#endif /* CONFIG_IEEE80211W */
	if (res == WPA_INVALID_MDIE)
		return WLAN_STATUS_INVALID_MDIE;
	if (res == WPA_INVALID_PMKID)
		return WLAN_STATUS_INVALID_PMKID;
	if (res != WPA_IE_OK)
		return WLAN_STATUS_INVALID_IE;
	return WLAN_STATUS_SUCCESS;
}


#ifdef CONFIG_FILS

static void handle_auth_fils_finish(struct hostapd_data *hapd,
				    struct sta_info *sta, u16 resp,
				    struct wpabuf *data, int pub);

void handle_auth_fils(struct hostapd_data *hapd, struct sta_info *sta,
		      const u8 *pos, size_t len, u16 auth_alg,
		      u16 auth_transaction, u16 status_code,
		      void (*cb)(struct hostapd_data *hapd,
				 struct sta_info *sta, u16 resp,
				 struct wpabuf *data, int pub))
{
	u16 resp = WLAN_STATUS_SUCCESS;
	const u8 *end;
	struct ieee802_11_elems elems;
	int res;
	struct wpa_ie_data rsn;
	struct rsn_pmksa_cache_entry *pmksa = NULL;

	if (auth_transaction != 1 || status_code != WLAN_STATUS_SUCCESS)
		return;

	end = pos + len;

	wpa_hexdump(MSG_DEBUG, "FILS: Authentication frame fields",
		    pos, end - pos);

	/* TODO: FILS PK */
#ifdef CONFIG_FILS_SK_PFS
	if (auth_alg == WLAN_AUTH_FILS_SK_PFS) {
		u16 group;
		struct wpabuf *pub;
		size_t elem_len;

		/* Using FILS PFS */

		/* Finite Cyclic Group */
		if (end - pos < 2) {
			wpa_printf(MSG_DEBUG,
				   "FILS: No room for Finite Cyclic Group");
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		group = WPA_GET_LE16(pos);
		pos += 2;
		if (group != hapd->conf->fils_dh_group) {
			wpa_printf(MSG_DEBUG,
				   "FILS: Unsupported Finite Cyclic Group: %u (expected %u)",
				   group, hapd->conf->fils_dh_group);
			resp = WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
			goto fail;
		}

		crypto_ecdh_deinit(sta->fils_ecdh);
		sta->fils_ecdh = crypto_ecdh_init(group);
		if (!sta->fils_ecdh) {
			wpa_printf(MSG_INFO,
				   "FILS: Could not initialize ECDH with group %d",
				   group);
			resp = WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
			goto fail;
		}

		pub = crypto_ecdh_get_pubkey(sta->fils_ecdh, 1);
		if (!pub) {
			wpa_printf(MSG_DEBUG,
				   "FILS: Failed to derive ECDH public key");
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		elem_len = wpabuf_len(pub);
		wpabuf_free(pub);

		/* Element */
		if ((size_t) (end - pos) < elem_len) {
			wpa_printf(MSG_DEBUG, "FILS: No room for Element");
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}

		wpabuf_free(sta->fils_g_sta);
		sta->fils_g_sta = wpabuf_alloc_copy(pos, elem_len);
		wpabuf_clear_free(sta->fils_dh_ss);
		sta->fils_dh_ss = crypto_ecdh_set_peerkey(sta->fils_ecdh, 1,
							  pos, elem_len);
		if (!sta->fils_dh_ss) {
			wpa_printf(MSG_DEBUG, "FILS: ECDH operation failed");
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		wpa_hexdump_buf_key(MSG_DEBUG, "FILS: DH_SS", sta->fils_dh_ss);
		pos += elem_len;
	} else {
		crypto_ecdh_deinit(sta->fils_ecdh);
		sta->fils_ecdh = NULL;
		wpabuf_clear_free(sta->fils_dh_ss);
		sta->fils_dh_ss = NULL;
	}
#endif /* CONFIG_FILS_SK_PFS */

	wpa_hexdump(MSG_DEBUG, "FILS: Remaining IEs", pos, end - pos);
	if (ieee802_11_parse_elems(pos, end - pos, &elems, 1) == ParseFailed) {
		wpa_printf(MSG_DEBUG, "FILS: Could not parse elements");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	/* RSNE */
	wpa_hexdump(MSG_DEBUG, "FILS: RSN element",
		    elems.rsn_ie, elems.rsn_ie_len);
	if (!elems.rsn_ie ||
	    wpa_parse_wpa_ie_rsn(elems.rsn_ie - 2, elems.rsn_ie_len + 2,
				 &rsn) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: No valid RSN element");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (!sta->wpa_sm)
		sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth, sta->addr,
						NULL);
	if (!sta->wpa_sm) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Failed to initialize RSN state machine");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	res = wpa_validate_wpa_ie(hapd->wpa_auth, sta->wpa_sm,
				  elems.rsn_ie - 2, elems.rsn_ie_len + 2,
				  elems.mdie, elems.mdie_len, NULL, 0);
	resp = wpa_res_to_status_code(res);
	if (resp != WLAN_STATUS_SUCCESS)
		goto fail;

	if (!elems.fils_nonce) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Nonce field");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "FILS: SNonce", elems.fils_nonce,
		    FILS_NONCE_LEN);
	os_memcpy(sta->fils_snonce, elems.fils_nonce, FILS_NONCE_LEN);

	/* PMKID List */
	if (rsn.pmkid && rsn.num_pmkid > 0) {
		u8 num;
		const u8 *pmkid;

		wpa_hexdump(MSG_DEBUG, "FILS: PMKID List",
			    rsn.pmkid, rsn.num_pmkid * PMKID_LEN);

		pmkid = rsn.pmkid;
		num = rsn.num_pmkid;
		while (num) {
			wpa_hexdump(MSG_DEBUG, "FILS: PMKID", pmkid, PMKID_LEN);
			pmksa = wpa_auth_pmksa_get(hapd->wpa_auth, sta->addr,
						   pmkid);
			if (pmksa)
				break;
			pmksa = wpa_auth_pmksa_get_fils_cache_id(hapd->wpa_auth,
								 sta->addr,
								 pmkid);
			if (pmksa)
				break;
			pmkid += PMKID_LEN;
			num--;
		}
	}
	if (pmksa && wpa_auth_sta_key_mgmt(sta->wpa_sm) != pmksa->akmp) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Matching PMKSA cache entry has different AKMP (0x%x != 0x%x) - ignore",
			   wpa_auth_sta_key_mgmt(sta->wpa_sm), pmksa->akmp);
		pmksa = NULL;
	}
	if (pmksa)
		wpa_printf(MSG_DEBUG, "FILS: Found matching PMKSA cache entry");

	/* FILS Session */
	if (!elems.fils_session) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Session element");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "FILS: FILS Session", elems.fils_session,
		    FILS_SESSION_LEN);
	os_memcpy(sta->fils_session, elems.fils_session, FILS_SESSION_LEN);

	/* FILS Wrapped Data */
	if (elems.fils_wrapped_data) {
		wpa_hexdump(MSG_DEBUG, "FILS: Wrapped Data",
			    elems.fils_wrapped_data,
			    elems.fils_wrapped_data_len);
		if (!pmksa) {
#ifndef CONFIG_NO_RADIUS
			if (!sta->eapol_sm) {
				sta->eapol_sm =
					ieee802_1x_alloc_eapol_sm(hapd, sta);
			}
			wpa_printf(MSG_DEBUG,
				   "FILS: Forward EAP-Initiate/Re-auth to authentication server");
			ieee802_1x_encapsulate_radius(
				hapd, sta, elems.fils_wrapped_data,
				elems.fils_wrapped_data_len);
			sta->fils_pending_cb = cb;
			wpa_printf(MSG_DEBUG,
				   "FILS: Will send Authentication frame once the response from authentication server is available");
			sta->flags |= WLAN_STA_PENDING_FILS_ERP;
			/* Calculate pending PMKID here so that we do not need
			 * to maintain a copy of the EAP-Initiate/Reauth
			 * message. */
			if (fils_pmkid_erp(wpa_auth_sta_key_mgmt(sta->wpa_sm),
					   elems.fils_wrapped_data,
					   elems.fils_wrapped_data_len,
					   sta->fils_erp_pmkid) == 0)
				sta->fils_erp_pmkid_set = 1;
			return;
#else /* CONFIG_NO_RADIUS */
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
#endif /* CONFIG_NO_RADIUS */
		}
	}

fail:
	if (cb) {
		struct wpabuf *data;
		int pub = 0;

		data = prepare_auth_resp_fils(hapd, sta, &resp, pmksa, NULL,
					      NULL, 0, &pub);
		if (!data) {
			wpa_printf(MSG_DEBUG,
				   "%s: prepare_auth_resp_fils() returned failure",
				   __func__);
		}

		cb(hapd, sta, resp, data, pub);
	}
}


static struct wpabuf *
prepare_auth_resp_fils(struct hostapd_data *hapd,
		       struct sta_info *sta, u16 *resp,
		       struct rsn_pmksa_cache_entry *pmksa,
		       struct wpabuf *erp_resp,
		       const u8 *msk, size_t msk_len,
		       int *is_pub)
{
	u8 fils_nonce[FILS_NONCE_LEN];
	size_t ielen;
	struct wpabuf *data = NULL;
	const u8 *ie;
	u8 *ie_buf = NULL;
	const u8 *pmk = NULL;
	size_t pmk_len = 0;
	u8 pmk_buf[PMK_LEN_MAX];
	struct wpabuf *pub = NULL;

	if (*resp != WLAN_STATUS_SUCCESS)
		goto fail;

	ie = wpa_auth_get_wpa_ie(hapd->wpa_auth, &ielen);
	if (!ie) {
		*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (pmksa) {
		/* Add PMKID of the selected PMKSA into RSNE */
		ie_buf = os_malloc(ielen + 2 + 2 + PMKID_LEN);
		if (!ie_buf) {
			*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}

		os_memcpy(ie_buf, ie, ielen);
		if (wpa_insert_pmkid(ie_buf, &ielen, pmksa->pmkid) < 0) {
			*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		ie = ie_buf;
	}

	if (random_get_bytes(fils_nonce, FILS_NONCE_LEN) < 0) {
		*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "RSN: Generated FILS Nonce",
		    fils_nonce, FILS_NONCE_LEN);

#ifdef CONFIG_FILS_SK_PFS
	if (sta->fils_dh_ss && sta->fils_ecdh) {
		pub = crypto_ecdh_get_pubkey(sta->fils_ecdh, 1);
		if (!pub) {
			*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
	}
#endif /* CONFIG_FILS_SK_PFS */

	data = wpabuf_alloc(1000 + ielen + (pub ? wpabuf_len(pub) : 0));
	if (!data) {
		*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	/* TODO: FILS PK */
#ifdef CONFIG_FILS_SK_PFS
	if (pub) {
		/* Finite Cyclic Group */
		wpabuf_put_le16(data, hapd->conf->fils_dh_group);

		/* Element */
		wpabuf_put_buf(data, pub);
	}
#endif /* CONFIG_FILS_SK_PFS */

	/* RSNE */
	wpabuf_put_data(data, ie, ielen);

	/* MDE when using FILS+FT (already included in ie,ielen with RSNE) */

#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(wpa_auth_sta_key_mgmt(sta->wpa_sm))) {
		/* FTE[R1KH-ID,R0KH-ID] when using FILS+FT */
		int res;
		int use_sha384 = wpa_key_mgmt_sha384(
			wpa_auth_sta_key_mgmt(sta->wpa_sm));

		res = wpa_auth_write_fte(hapd->wpa_auth, use_sha384,
					 wpabuf_put(data, 0),
					 wpabuf_tailroom(data));
		if (res < 0) {
			*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		wpabuf_put(data, res);
	}
#endif /* CONFIG_IEEE80211R_AP */

	/* FILS Nonce */
	wpabuf_put_u8(data, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(data, 1 + FILS_NONCE_LEN); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(data, WLAN_EID_EXT_FILS_NONCE);
	wpabuf_put_data(data, fils_nonce, FILS_NONCE_LEN);

	/* FILS Session */
	wpabuf_put_u8(data, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(data, 1 + FILS_SESSION_LEN); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(data, WLAN_EID_EXT_FILS_SESSION);
	wpabuf_put_data(data, sta->fils_session, FILS_SESSION_LEN);

	/* FILS Wrapped Data */
	if (!pmksa && erp_resp) {
		wpabuf_put_u8(data, WLAN_EID_EXTENSION); /* Element ID */
		wpabuf_put_u8(data, 1 + wpabuf_len(erp_resp)); /* Length */
		/* Element ID Extension */
		wpabuf_put_u8(data, WLAN_EID_EXT_FILS_WRAPPED_DATA);
		wpabuf_put_buf(data, erp_resp);

		if (fils_rmsk_to_pmk(wpa_auth_sta_key_mgmt(sta->wpa_sm),
				     msk, msk_len, sta->fils_snonce, fils_nonce,
				     sta->fils_dh_ss ?
				     wpabuf_head(sta->fils_dh_ss) : NULL,
				     sta->fils_dh_ss ?
				     wpabuf_len(sta->fils_dh_ss) : 0,
				     pmk_buf, &pmk_len)) {
			wpa_printf(MSG_DEBUG, "FILS: Failed to derive PMK");
			*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			wpabuf_free(data);
			data = NULL;
			goto fail;
		}
		pmk = pmk_buf;

		/* Don't use DHss in PTK derivation if PMKSA caching is not
		 * used. */
		wpabuf_clear_free(sta->fils_dh_ss);
		sta->fils_dh_ss = NULL;

		if (sta->fils_erp_pmkid_set) {
			/* TODO: get PMKLifetime from WPA parameters */
			unsigned int dot11RSNAConfigPMKLifetime = 43200;
			int session_timeout;

			session_timeout = dot11RSNAConfigPMKLifetime;
			if (sta->session_timeout_set) {
				struct os_reltime now, diff;

				os_get_reltime(&now);
				os_reltime_sub(&sta->session_timeout, &now,
					       &diff);
				session_timeout = diff.sec;
			}

			sta->fils_erp_pmkid_set = 0;
			if (wpa_auth_pmksa_add2(
				    hapd->wpa_auth, sta->addr,
				    pmk, pmk_len,
				    sta->fils_erp_pmkid,
				    session_timeout,
				    wpa_auth_sta_key_mgmt(sta->wpa_sm)) < 0) {
				wpa_printf(MSG_ERROR,
					   "FILS: Failed to add PMKSA cache entry based on ERP");
			}
		}
	} else if (pmksa) {
		pmk = pmksa->pmk;
		pmk_len = pmksa->pmk_len;
	}

	if (!pmk) {
		wpa_printf(MSG_DEBUG, "FILS: No PMK available");
		*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		wpabuf_free(data);
		data = NULL;
		goto fail;
	}

	if (fils_auth_pmk_to_ptk(sta->wpa_sm, pmk, pmk_len,
				 sta->fils_snonce, fils_nonce,
				 sta->fils_dh_ss ?
				 wpabuf_head(sta->fils_dh_ss) : NULL,
				 sta->fils_dh_ss ?
				 wpabuf_len(sta->fils_dh_ss) : 0,
				 sta->fils_g_sta, pub) < 0) {
		*resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		wpabuf_free(data);
		data = NULL;
		goto fail;
	}

fail:
	if (is_pub)
		*is_pub = pub != NULL;
	os_free(ie_buf);
	wpabuf_free(pub);
	wpabuf_clear_free(sta->fils_dh_ss);
	sta->fils_dh_ss = NULL;
#ifdef CONFIG_FILS_SK_PFS
	crypto_ecdh_deinit(sta->fils_ecdh);
	sta->fils_ecdh = NULL;
#endif /* CONFIG_FILS_SK_PFS */
	return data;
}


static void handle_auth_fils_finish(struct hostapd_data *hapd,
				    struct sta_info *sta, u16 resp,
				    struct wpabuf *data, int pub)
{
	u16 auth_alg;

	auth_alg = (pub ||
		    resp == WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED) ?
		WLAN_AUTH_FILS_SK_PFS : WLAN_AUTH_FILS_SK;
	send_auth_reply(hapd, sta->addr, hapd->own_addr, auth_alg, 2, resp,
			data ? wpabuf_head(data) : (u8 *) "",
			data ? wpabuf_len(data) : 0, "auth-fils-finish");
	wpabuf_free(data);

	if (resp == WLAN_STATUS_SUCCESS) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "authentication OK (FILS)");
		sta->flags |= WLAN_STA_AUTH;
		wpa_auth_sm_event(sta->wpa_sm, WPA_AUTH);
		sta->auth_alg = pub ? WLAN_AUTH_FILS_SK_PFS : WLAN_AUTH_FILS_SK;
		mlme_authenticate_indication(hapd, sta);
	}
}


void ieee802_11_finish_fils_auth(struct hostapd_data *hapd,
				 struct sta_info *sta, int success,
				 struct wpabuf *erp_resp,
				 const u8 *msk, size_t msk_len)
{
	struct wpabuf *data;
	int pub = 0;
	u16 resp;

	sta->flags &= ~WLAN_STA_PENDING_FILS_ERP;

	if (!sta->fils_pending_cb)
		return;
	resp = success ? WLAN_STATUS_SUCCESS : WLAN_STATUS_UNSPECIFIED_FAILURE;
	data = prepare_auth_resp_fils(hapd, sta, &resp, NULL, erp_resp,
				      msk, msk_len, &pub);
	if (!data) {
		wpa_printf(MSG_DEBUG,
			   "%s: prepare_auth_resp_fils() returned failure",
			   __func__);
	}
	sta->fils_pending_cb(hapd, sta, resp, data, pub);
}

#endif /* CONFIG_FILS */


int
ieee802_11_allowed_address(struct hostapd_data *hapd, const u8 *addr,
			   const u8 *msg, size_t len, u32 *session_timeout,
			   u32 *acct_interim_interval,
			   struct vlan_description *vlan_id,
			   struct hostapd_sta_wpa_psk_short **psk,
			   char **identity, char **radius_cui, int is_probe_req)
{
	int res;

	os_memset(vlan_id, 0, sizeof(*vlan_id));
	res = hostapd_allowed_address(hapd, addr, msg, len,
				      session_timeout, acct_interim_interval,
				      vlan_id, psk, identity, radius_cui,
				      is_probe_req);

	if (res == HOSTAPD_ACL_REJECT) {
		if (!is_probe_req)
			wpa_printf(MSG_DEBUG,
				   "Station " MACSTR
				   " not allowed to authenticate",
				   MAC2STR(addr));
		return HOSTAPD_ACL_REJECT;
	}

	if (res == HOSTAPD_ACL_PENDING) {
		wpa_printf(MSG_DEBUG, "Authentication frame from " MACSTR
			   " waiting for an external authentication",
			   MAC2STR(addr));
		/* Authentication code will re-send the authentication frame
		 * after it has received (and cached) information from the
		 * external source. */
		return HOSTAPD_ACL_PENDING;
	}

	return res;
}


static int
ieee802_11_set_radius_info(struct hostapd_data *hapd, struct sta_info *sta,
			   int res, u32 session_timeout,
			   u32 acct_interim_interval,
			   struct vlan_description *vlan_id,
			   struct hostapd_sta_wpa_psk_short **psk,
			   char **identity, char **radius_cui)
{
	if (vlan_id->notempty &&
	    !hostapd_vlan_valid(hapd->conf->vlan, vlan_id)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_INFO,
			       "Invalid VLAN %d%s received from RADIUS server",
			       vlan_id->untagged,
			       vlan_id->tagged[0] ? "+" : "");
		return -1;
	}
	if (ap_sta_set_vlan(hapd, sta, vlan_id) < 0)
		return -1;
	if (sta->vlan_id)
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_INFO, "VLAN ID %d", sta->vlan_id);

	hostapd_free_psk_list(sta->psk);
	if (hapd->conf->wpa_psk_radius != PSK_RADIUS_IGNORED) {
		sta->psk = *psk;
		*psk = NULL;
	} else {
		sta->psk = NULL;
	}

	os_free(sta->identity);
	sta->identity = *identity;
	*identity = NULL;

	os_free(sta->radius_cui);
	sta->radius_cui = *radius_cui;
	*radius_cui = NULL;

	if (hapd->conf->acct_interim_interval == 0 && acct_interim_interval)
		sta->acct_interim_interval = acct_interim_interval;
	if (res == HOSTAPD_ACL_ACCEPT_TIMEOUT) {
		sta->session_timeout_set = 1;
		os_get_reltime(&sta->session_timeout);
		sta->session_timeout.sec += session_timeout;
		ap_sta_session_timeout(hapd, sta, session_timeout);
	} else {
		sta->session_timeout_set = 0;
		ap_sta_no_session_timeout(hapd, sta);
	}

	return 0;
}


static void handle_auth(struct hostapd_data *hapd,
			const struct ieee80211_mgmt *mgmt, size_t len)
{
	u16 auth_alg, auth_transaction, status_code;
	u16 resp = WLAN_STATUS_SUCCESS;
	struct sta_info *sta = NULL;
	int res, reply_res;
	u16 fc;
	const u8 *challenge = NULL;
	u32 session_timeout, acct_interim_interval;
	struct vlan_description vlan_id;
	struct hostapd_sta_wpa_psk_short *psk = NULL;
	u8 resp_ies[2 + WLAN_AUTH_CHALLENGE_LEN];
	size_t resp_ies_len = 0;
	char *identity = NULL;
	char *radius_cui = NULL;
	u16 seq_ctrl;

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.auth)) {
		wpa_printf(MSG_INFO, "handle_auth - too short payload (len=%lu)",
			   (unsigned long) len);
		return;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->iconf->ignore_auth_probability > 0.0 &&
	    drand48() < hapd->iconf->ignore_auth_probability) {
		wpa_printf(MSG_INFO,
			   "TESTING: ignoring auth frame from " MACSTR,
			   MAC2STR(mgmt->sa));
		return;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	auth_alg = le_to_host16(mgmt->u.auth.auth_alg);
	auth_transaction = le_to_host16(mgmt->u.auth.auth_transaction);
	status_code = le_to_host16(mgmt->u.auth.status_code);
	fc = le_to_host16(mgmt->frame_control);
	seq_ctrl = le_to_host16(mgmt->seq_ctrl);

	if (len >= IEEE80211_HDRLEN + sizeof(mgmt->u.auth) +
	    2 + WLAN_AUTH_CHALLENGE_LEN &&
	    mgmt->u.auth.variable[0] == WLAN_EID_CHALLENGE &&
	    mgmt->u.auth.variable[1] == WLAN_AUTH_CHALLENGE_LEN)
		challenge = &mgmt->u.auth.variable[2];

	wpa_printf(MSG_DEBUG, "authentication: STA=" MACSTR " auth_alg=%d "
		   "auth_transaction=%d status_code=%d wep=%d%s "
		   "seq_ctrl=0x%x%s",
		   MAC2STR(mgmt->sa), auth_alg, auth_transaction,
		   status_code, !!(fc & WLAN_FC_ISWEP),
		   challenge ? " challenge" : "",
		   seq_ctrl, (fc & WLAN_FC_RETRY) ? " retry" : "");

#ifdef CONFIG_NO_RC4
	if (auth_alg == WLAN_AUTH_SHARED_KEY) {
		wpa_printf(MSG_INFO,
			   "Unsupported authentication algorithm (%d)",
			   auth_alg);
		resp = WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;
		goto fail;
	}
#endif /* CONFIG_NO_RC4 */

	if (hapd->tkip_countermeasures) {
		wpa_printf(MSG_DEBUG,
			   "Ongoing TKIP countermeasures (Michael MIC failure) - reject authentication");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (!(((hapd->conf->auth_algs & WPA_AUTH_ALG_OPEN) &&
	       auth_alg == WLAN_AUTH_OPEN) ||
#ifdef CONFIG_IEEE80211R_AP
	      (hapd->conf->wpa && wpa_key_mgmt_ft(hapd->conf->wpa_key_mgmt) &&
	       auth_alg == WLAN_AUTH_FT) ||
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_SAE
	      (hapd->conf->wpa && wpa_key_mgmt_sae(hapd->conf->wpa_key_mgmt) &&
	       auth_alg == WLAN_AUTH_SAE) ||
#endif /* CONFIG_SAE */
#ifdef CONFIG_FILS
	      (hapd->conf->wpa && wpa_key_mgmt_fils(hapd->conf->wpa_key_mgmt) &&
	       auth_alg == WLAN_AUTH_FILS_SK) ||
	      (hapd->conf->wpa && wpa_key_mgmt_fils(hapd->conf->wpa_key_mgmt) &&
	       hapd->conf->fils_dh_group &&
	       auth_alg == WLAN_AUTH_FILS_SK_PFS) ||
#endif /* CONFIG_FILS */
	      ((hapd->conf->auth_algs & WPA_AUTH_ALG_SHARED) &&
	       auth_alg == WLAN_AUTH_SHARED_KEY))) {
		wpa_printf(MSG_INFO, "Unsupported authentication algorithm (%d)",
			   auth_alg);
		resp = WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;
		goto fail;
	}

	if (!(auth_transaction == 1 || auth_alg == WLAN_AUTH_SAE ||
	      (auth_alg == WLAN_AUTH_SHARED_KEY && auth_transaction == 3))) {
		wpa_printf(MSG_INFO, "Unknown authentication transaction number (%d)",
			   auth_transaction);
		resp = WLAN_STATUS_UNKNOWN_AUTH_TRANSACTION;
		goto fail;
	}

	if (os_memcmp(mgmt->sa, hapd->own_addr, ETH_ALEN) == 0) {
		wpa_printf(MSG_INFO, "Station " MACSTR " not allowed to authenticate",
			   MAC2STR(mgmt->sa));
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (hapd->conf->no_auth_if_seen_on) {
		struct hostapd_data *other;

		other = sta_track_seen_on(hapd->iface, mgmt->sa,
					  hapd->conf->no_auth_if_seen_on);
		if (other) {
			u8 *pos;
			u32 info;
			u8 op_class, channel, phytype;

			wpa_printf(MSG_DEBUG, "%s: Reject authentication from "
				   MACSTR " since STA has been seen on %s",
				   hapd->conf->iface, MAC2STR(mgmt->sa),
				   hapd->conf->no_auth_if_seen_on);

			resp = WLAN_STATUS_REJECTED_WITH_SUGGESTED_BSS_TRANSITION;
			pos = &resp_ies[0];
			*pos++ = WLAN_EID_NEIGHBOR_REPORT;
			*pos++ = 13;
			os_memcpy(pos, other->own_addr, ETH_ALEN);
			pos += ETH_ALEN;
			info = 0; /* TODO: BSSID Information */
			WPA_PUT_LE32(pos, info);
			pos += 4;
			if (other->iconf->hw_mode == HOSTAPD_MODE_IEEE80211AD)
				phytype = 8; /* dmg */
			else if (other->iconf->ieee80211ac)
				phytype = 9; /* vht */
			else if (other->iconf->ieee80211n)
				phytype = 7; /* ht */
			else if (other->iconf->hw_mode ==
				 HOSTAPD_MODE_IEEE80211A)
				phytype = 4; /* ofdm */
			else if (other->iconf->hw_mode ==
				 HOSTAPD_MODE_IEEE80211G)
				phytype = 6; /* erp */
			else
				phytype = 5; /* hrdsss */
			if (ieee80211_freq_to_channel_ext(
				    hostapd_hw_get_freq(other,
							other->iconf->channel),
				    other->iconf->secondary_channel,
				    other->iconf->ieee80211ac,
				    &op_class, &channel) == NUM_HOSTAPD_MODES) {
				op_class = 0;
				channel = other->iconf->channel;
			}
			*pos++ = op_class;
			*pos++ = channel;
			*pos++ = phytype;
			resp_ies_len = pos - &resp_ies[0];
			goto fail;
		}
	}

	res = ieee802_11_allowed_address(
		hapd, mgmt->sa, (const u8 *) mgmt, len, &session_timeout,
		&acct_interim_interval, &vlan_id, &psk, &identity, &radius_cui,
		0);
	if (res == HOSTAPD_ACL_REJECT) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG,
			"Ignore Authentication frame from " MACSTR
			" due to ACL reject", MAC2STR(mgmt->sa));
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}
	if (res == HOSTAPD_ACL_PENDING)
		return;

	sta = ap_get_sta(hapd, mgmt->sa);
	if (sta) {
		sta->flags &= ~WLAN_STA_PENDING_FILS_ERP;
		if ((fc & WLAN_FC_RETRY) &&
		    sta->last_seq_ctrl != WLAN_INVALID_MGMT_SEQ &&
		    sta->last_seq_ctrl == seq_ctrl &&
		    sta->last_subtype == WLAN_FC_STYPE_AUTH) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG,
				       "Drop repeated authentication frame seq_ctrl=0x%x",
				       seq_ctrl);
			return;
		}
#ifdef CONFIG_MESH
		if ((hapd->conf->mesh & MESH_ENABLED) &&
		    sta->plink_state == PLINK_BLOCKED) {
			wpa_printf(MSG_DEBUG, "Mesh peer " MACSTR
				   " is blocked - drop Authentication frame",
				   MAC2STR(mgmt->sa));
			return;
		}
#endif /* CONFIG_MESH */
	} else {
#ifdef CONFIG_MESH
		if (hapd->conf->mesh & MESH_ENABLED) {
			/* if the mesh peer is not available, we don't do auth.
			 */
			wpa_printf(MSG_DEBUG, "Mesh peer " MACSTR
				   " not yet known - drop Authentication frame",
				   MAC2STR(mgmt->sa));
			/*
			 * Save a copy of the frame so that it can be processed
			 * if a new peer entry is added shortly after this.
			 */
			wpabuf_free(hapd->mesh_pending_auth);
			hapd->mesh_pending_auth = wpabuf_alloc_copy(mgmt, len);
			os_get_reltime(&hapd->mesh_pending_auth_time);
			return;
		}
#endif /* CONFIG_MESH */

		sta = ap_sta_add(hapd, mgmt->sa);
		if (!sta) {
			wpa_printf(MSG_DEBUG, "ap_sta_add() failed");
			resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
			goto fail;
		}
	}
	sta->last_seq_ctrl = seq_ctrl;
	sta->last_subtype = WLAN_FC_STYPE_AUTH;

	res = ieee802_11_set_radius_info(
		hapd, sta, res, session_timeout, acct_interim_interval,
		&vlan_id, &psk, &identity, &radius_cui);
	if (res) {
		wpa_printf(MSG_DEBUG, "ieee802_11_set_radius_info() failed");
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	sta->flags &= ~WLAN_STA_PREAUTH;
	ieee802_1x_notify_pre_auth(sta->eapol_sm, 0);

	/*
	 * If the driver supports full AP client state, add a station to the
	 * driver before sending authentication reply to make sure the driver
	 * has resources, and not to go through the entire authentication and
	 * association handshake, and fail it at the end.
	 *
	 * If this is not the first transaction, in a multi-step authentication
	 * algorithm, the station already exists in the driver
	 * (sta->added_unassoc = 1) so skip it.
	 *
	 * In mesh mode, the station was already added to the driver when the
	 * NEW_PEER_CANDIDATE event is received.
	 *
	 * If PMF was negotiated for the existing association, skip this to
	 * avoid dropping the STA entry and the associated keys. This is needed
	 * to allow the original connection work until the attempt can complete
	 * (re)association, so that unprotected Authentication frame cannot be
	 * used to bypass PMF protection.
	 */
	if (FULL_AP_CLIENT_STATE_SUPP(hapd->iface->drv_flags) &&
	    (!(sta->flags & WLAN_STA_MFP) || !ap_sta_is_authorized(sta)) &&
	    !(hapd->conf->mesh & MESH_ENABLED) &&
	    !(sta->added_unassoc)) {
		/*
		 * If a station that is already associated to the AP, is trying
		 * to authenticate again, remove the STA entry, in order to make
		 * sure the STA PS state gets cleared and configuration gets
		 * updated. To handle this, station's added_unassoc flag is
		 * cleared once the station has completed association.
		 */
		ap_sta_set_authorized(hapd, sta, 0);
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->flags &= ~(WLAN_STA_ASSOC | WLAN_STA_AUTH |
				WLAN_STA_AUTHORIZED);

		if (hostapd_sta_add(hapd, sta->addr, 0, 0, NULL, 0, 0,
				    NULL, NULL, sta->flags, 0, 0, 0, 0)) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_NOTICE,
				       "Could not add STA to kernel driver");
			resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
			goto fail;
		}

		sta->added_unassoc = 1;
	}

	switch (auth_alg) {
	case WLAN_AUTH_OPEN:
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "authentication OK (open system)");
		sta->flags |= WLAN_STA_AUTH;
		wpa_auth_sm_event(sta->wpa_sm, WPA_AUTH);
		sta->auth_alg = WLAN_AUTH_OPEN;
		mlme_authenticate_indication(hapd, sta);
		break;
#ifndef CONFIG_NO_RC4
	case WLAN_AUTH_SHARED_KEY:
		resp = auth_shared_key(hapd, sta, auth_transaction, challenge,
				       fc & WLAN_FC_ISWEP);
		if (resp != 0)
			wpa_printf(MSG_DEBUG,
				   "auth_shared_key() failed: status=%d", resp);
		sta->auth_alg = WLAN_AUTH_SHARED_KEY;
		mlme_authenticate_indication(hapd, sta);
		if (sta->challenge && auth_transaction == 1) {
			resp_ies[0] = WLAN_EID_CHALLENGE;
			resp_ies[1] = WLAN_AUTH_CHALLENGE_LEN;
			os_memcpy(resp_ies + 2, sta->challenge,
				  WLAN_AUTH_CHALLENGE_LEN);
			resp_ies_len = 2 + WLAN_AUTH_CHALLENGE_LEN;
		}
		break;
#endif /* CONFIG_NO_RC4 */
#ifdef CONFIG_IEEE80211R_AP
	case WLAN_AUTH_FT:
		sta->auth_alg = WLAN_AUTH_FT;
		if (sta->wpa_sm == NULL)
			sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth,
							sta->addr, NULL);
		if (sta->wpa_sm == NULL) {
			wpa_printf(MSG_DEBUG, "FT: Failed to initialize WPA "
				   "state machine");
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		wpa_ft_process_auth(sta->wpa_sm, mgmt->bssid,
				    auth_transaction, mgmt->u.auth.variable,
				    len - IEEE80211_HDRLEN -
				    sizeof(mgmt->u.auth),
				    handle_auth_ft_finish, hapd);
		/* handle_auth_ft_finish() callback will complete auth. */
		return;
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_SAE
	case WLAN_AUTH_SAE:
#ifdef CONFIG_MESH
		if (status_code == WLAN_STATUS_SUCCESS &&
		    hapd->conf->mesh & MESH_ENABLED) {
			if (sta->wpa_sm == NULL)
				sta->wpa_sm =
					wpa_auth_sta_init(hapd->wpa_auth,
							  sta->addr, NULL);
			if (sta->wpa_sm == NULL) {
				wpa_printf(MSG_DEBUG,
					   "SAE: Failed to initialize WPA state machine");
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto fail;
			}
		}
#endif /* CONFIG_MESH */
		handle_auth_sae(hapd, sta, mgmt, len, auth_transaction,
				status_code);
		return;
#endif /* CONFIG_SAE */
#ifdef CONFIG_FILS
	case WLAN_AUTH_FILS_SK:
	case WLAN_AUTH_FILS_SK_PFS:
		handle_auth_fils(hapd, sta, mgmt->u.auth.variable,
				 len - IEEE80211_HDRLEN - sizeof(mgmt->u.auth),
				 auth_alg, auth_transaction, status_code,
				 handle_auth_fils_finish);
		return;
#endif /* CONFIG_FILS */
	}

 fail:
	os_free(identity);
	os_free(radius_cui);
	hostapd_free_psk_list(psk);

	reply_res = send_auth_reply(hapd, mgmt->sa, mgmt->bssid, auth_alg,
				    auth_transaction + 1, resp, resp_ies,
				    resp_ies_len, "handle-auth");

	if (sta && sta->added_unassoc && (resp != WLAN_STATUS_SUCCESS ||
					  reply_res != WLAN_STATUS_SUCCESS)) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->added_unassoc = 0;
	}
}


int hostapd_get_aid(struct hostapd_data *hapd, struct sta_info *sta)
{
	int i, j = 32, aid;

	/* get a unique AID */
	if (sta->aid > 0) {
		wpa_printf(MSG_DEBUG, "  old AID %d", sta->aid);
		return 0;
	}

	if (TEST_FAIL())
		return -1;

	for (i = 0; i < AID_WORDS; i++) {
		if (hapd->sta_aid[i] == (u32) -1)
			continue;
		for (j = 0; j < 32; j++) {
			if (!(hapd->sta_aid[i] & BIT(j)))
				break;
		}
		if (j < 32)
			break;
	}
	if (j == 32)
		return -1;
	aid = i * 32 + j + 1;
	if (aid > 2007)
		return -1;

	sta->aid = aid;
	hapd->sta_aid[i] |= BIT(j);
	wpa_printf(MSG_DEBUG, "  new AID %d", sta->aid);
	return 0;
}


static u16 check_ssid(struct hostapd_data *hapd, struct sta_info *sta,
		      const u8 *ssid_ie, size_t ssid_ie_len)
{
	if (ssid_ie == NULL)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	if (ssid_ie_len != hapd->conf->ssid.ssid_len ||
	    os_memcmp(ssid_ie, hapd->conf->ssid.ssid, ssid_ie_len) != 0) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO,
			       "Station tried to associate with unknown SSID "
			       "'%s'", wpa_ssid_txt(ssid_ie, ssid_ie_len));
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	return WLAN_STATUS_SUCCESS;
}


static u16 check_wmm(struct hostapd_data *hapd, struct sta_info *sta,
		     const u8 *wmm_ie, size_t wmm_ie_len)
{
	sta->flags &= ~WLAN_STA_WMM;
	sta->qosinfo = 0;
	if (wmm_ie && hapd->conf->wmm_enabled) {
		struct wmm_information_element *wmm;

		if (!hostapd_eid_wmm_valid(hapd, wmm_ie, wmm_ie_len)) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_WPA,
				       HOSTAPD_LEVEL_DEBUG,
				       "invalid WMM element in association "
				       "request");
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
		}

		sta->flags |= WLAN_STA_WMM;
		wmm = (struct wmm_information_element *) wmm_ie;
		sta->qosinfo = wmm->qos_info;
	}
	return WLAN_STATUS_SUCCESS;
}


static u16 copy_supp_rates(struct hostapd_data *hapd, struct sta_info *sta,
			   struct ieee802_11_elems *elems)
{
	/* Supported rates not used in IEEE 802.11ad/DMG */
	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211AD)
		return WLAN_STATUS_SUCCESS;

	if (!elems->supp_rates) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "No supported rates element in AssocReq");
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	if (elems->supp_rates_len + elems->ext_supp_rates_len >
	    sizeof(sta->supported_rates)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Invalid supported rates element length %d+%d",
			       elems->supp_rates_len,
			       elems->ext_supp_rates_len);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	sta->supported_rates_len = merge_byte_arrays(
		sta->supported_rates, sizeof(sta->supported_rates),
		elems->supp_rates, elems->supp_rates_len,
		elems->ext_supp_rates, elems->ext_supp_rates_len);

	return WLAN_STATUS_SUCCESS;
}


static u16 check_ext_capab(struct hostapd_data *hapd, struct sta_info *sta,
			   const u8 *ext_capab_ie, size_t ext_capab_ie_len)
{
#ifdef CONFIG_INTERWORKING
	/* check for QoS Map support */
	if (ext_capab_ie_len >= 5) {
		if (ext_capab_ie[4] & 0x01)
			sta->qos_map_enabled = 1;
	}
#endif /* CONFIG_INTERWORKING */

	if (ext_capab_ie_len > 0) {
		sta->ecsa_supported = !!(ext_capab_ie[0] & BIT(2));
		os_free(sta->ext_capability);
		sta->ext_capability = os_malloc(1 + ext_capab_ie_len);
		if (sta->ext_capability) {
			sta->ext_capability[0] = ext_capab_ie_len;
			os_memcpy(sta->ext_capability + 1, ext_capab_ie,
				  ext_capab_ie_len);
		}
	}

	return WLAN_STATUS_SUCCESS;
}


#ifdef CONFIG_OWE

static int owe_group_supported(struct hostapd_data *hapd, u16 group)
{
	int i;
	int *groups = hapd->conf->owe_groups;

	if (group != 19 && group != 20 && group != 21)
		return 0;

	if (!groups)
		return 1;

	for (i = 0; groups[i] > 0; i++) {
		if (groups[i] == group)
			return 1;
	}

	return 0;
}


static u16 owe_process_assoc_req(struct hostapd_data *hapd,
				 struct sta_info *sta, const u8 *owe_dh,
				 u8 owe_dh_len)
{
	struct wpabuf *secret, *pub, *hkey;
	int res;
	u8 prk[SHA512_MAC_LEN], pmkid[SHA512_MAC_LEN];
	const char *info = "OWE Key Generation";
	const u8 *addr[2];
	size_t len[2];
	u16 group;
	size_t hash_len, prime_len;

	if (wpa_auth_sta_get_pmksa(sta->wpa_sm)) {
		wpa_printf(MSG_DEBUG, "OWE: Using PMKSA caching");
		return WLAN_STATUS_SUCCESS;
	}

	group = WPA_GET_LE16(owe_dh);
	if (!owe_group_supported(hapd, group)) {
		wpa_printf(MSG_DEBUG, "OWE: Unsupported DH group %u", group);
		return WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
	}
	if (group == 19)
		prime_len = 32;
	else if (group == 20)
		prime_len = 48;
	else if (group == 21)
		prime_len = 66;
	else
		return WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;

	crypto_ecdh_deinit(sta->owe_ecdh);
	sta->owe_ecdh = crypto_ecdh_init(group);
	if (!sta->owe_ecdh)
		return WLAN_STATUS_FINITE_CYCLIC_GROUP_NOT_SUPPORTED;
	sta->owe_group = group;

	secret = crypto_ecdh_set_peerkey(sta->owe_ecdh, 0, owe_dh + 2,
					 owe_dh_len - 2);
	secret = wpabuf_zeropad(secret, prime_len);
	if (!secret) {
		wpa_printf(MSG_DEBUG, "OWE: Invalid peer DH public key");
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}
	wpa_hexdump_buf_key(MSG_DEBUG, "OWE: DH shared secret", secret);

	/* prk = HKDF-extract(C | A | group, z) */

	pub = crypto_ecdh_get_pubkey(sta->owe_ecdh, 0);
	if (!pub) {
		wpabuf_clear_free(secret);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	/* PMKID = Truncate-128(Hash(C | A)) */
	addr[0] = owe_dh + 2;
	len[0] = owe_dh_len - 2;
	addr[1] = wpabuf_head(pub);
	len[1] = wpabuf_len(pub);
	if (group == 19) {
		res = sha256_vector(2, addr, len, pmkid);
		hash_len = SHA256_MAC_LEN;
	} else if (group == 20) {
		res = sha384_vector(2, addr, len, pmkid);
		hash_len = SHA384_MAC_LEN;
	} else if (group == 21) {
		res = sha512_vector(2, addr, len, pmkid);
		hash_len = SHA512_MAC_LEN;
	} else {
		wpabuf_free(pub);
		wpabuf_clear_free(secret);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}
	pub = wpabuf_zeropad(pub, prime_len);
	if (res < 0 || !pub) {
		wpabuf_free(pub);
		wpabuf_clear_free(secret);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	hkey = wpabuf_alloc(owe_dh_len - 2 + wpabuf_len(pub) + 2);
	if (!hkey) {
		wpabuf_free(pub);
		wpabuf_clear_free(secret);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	wpabuf_put_data(hkey, owe_dh + 2, owe_dh_len - 2); /* C */
	wpabuf_put_buf(hkey, pub); /* A */
	wpabuf_free(pub);
	wpabuf_put_le16(hkey, group); /* group */
	if (group == 19)
		res = hmac_sha256(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	else if (group == 20)
		res = hmac_sha384(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	else if (group == 21)
		res = hmac_sha512(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	wpabuf_clear_free(hkey);
	wpabuf_clear_free(secret);
	if (res < 0)
		return WLAN_STATUS_UNSPECIFIED_FAILURE;

	wpa_hexdump_key(MSG_DEBUG, "OWE: prk", prk, hash_len);

	/* PMK = HKDF-expand(prk, "OWE Key Generation", n) */

	os_free(sta->owe_pmk);
	sta->owe_pmk = os_malloc(hash_len);
	if (!sta->owe_pmk) {
		os_memset(prk, 0, SHA512_MAC_LEN);
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	if (group == 19)
		res = hmac_sha256_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sta->owe_pmk, hash_len);
	else if (group == 20)
		res = hmac_sha384_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sta->owe_pmk, hash_len);
	else if (group == 21)
		res = hmac_sha512_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sta->owe_pmk, hash_len);
	os_memset(prk, 0, SHA512_MAC_LEN);
	if (res < 0) {
		os_free(sta->owe_pmk);
		sta->owe_pmk = NULL;
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}
	sta->owe_pmk_len = hash_len;

	wpa_hexdump_key(MSG_DEBUG, "OWE: PMK", sta->owe_pmk, sta->owe_pmk_len);
	wpa_hexdump(MSG_DEBUG, "OWE: PMKID", pmkid, PMKID_LEN);
	wpa_auth_pmksa_add2(hapd->wpa_auth, sta->addr, sta->owe_pmk,
			    sta->owe_pmk_len, pmkid, 0, WPA_KEY_MGMT_OWE);

	return WLAN_STATUS_SUCCESS;
}

#endif /* CONFIG_OWE */


static u16 check_assoc_ies(struct hostapd_data *hapd, struct sta_info *sta,
			   const u8 *ies, size_t ies_len, int reassoc)
{
	struct ieee802_11_elems elems;
	u16 resp;
	const u8 *wpa_ie;
	size_t wpa_ie_len;
	const u8 *p2p_dev_addr = NULL;

	if (ieee802_11_parse_elems(ies, ies_len, &elems, 1) == ParseFailed) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "Station sent an invalid "
			       "association request");
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	resp = check_ssid(hapd, sta, elems.ssid, elems.ssid_len);
	if (resp != WLAN_STATUS_SUCCESS)
		return resp;
	resp = check_wmm(hapd, sta, elems.wmm, elems.wmm_len);
	if (resp != WLAN_STATUS_SUCCESS)
		return resp;
	resp = check_ext_capab(hapd, sta, elems.ext_capab, elems.ext_capab_len);
	if (resp != WLAN_STATUS_SUCCESS)
		return resp;
	resp = copy_supp_rates(hapd, sta, &elems);
	if (resp != WLAN_STATUS_SUCCESS)
		return resp;
#ifdef CONFIG_IEEE80211N
	resp = copy_sta_ht_capab(hapd, sta, elems.ht_capabilities);
	if (resp != WLAN_STATUS_SUCCESS)
		return resp;
	if (hapd->iconf->ieee80211n && hapd->iconf->require_ht &&
	    !(sta->flags & WLAN_STA_HT)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "Station does not support "
			       "mandatory HT PHY - reject association");
		return WLAN_STATUS_ASSOC_DENIED_NO_HT;
	}
#endif /* CONFIG_IEEE80211N */

#ifdef CONFIG_IEEE80211AC
	if (hapd->iconf->ieee80211ac) {
		resp = copy_sta_vht_capab(hapd, sta, elems.vht_capabilities);
		if (resp != WLAN_STATUS_SUCCESS)
			return resp;

		resp = set_sta_vht_opmode(hapd, sta, elems.vht_opmode_notif);
		if (resp != WLAN_STATUS_SUCCESS)
			return resp;
	}

	if (hapd->iconf->ieee80211ac && hapd->iconf->require_vht &&
	    !(sta->flags & WLAN_STA_VHT)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "Station does not support "
			       "mandatory VHT PHY - reject association");
		return WLAN_STATUS_ASSOC_DENIED_NO_VHT;
	}

	if (hapd->conf->vendor_vht && !elems.vht_capabilities) {
		resp = copy_sta_vendor_vht(hapd, sta, elems.vendor_vht,
					   elems.vendor_vht_len);
		if (resp != WLAN_STATUS_SUCCESS)
			return resp;
	}
#endif /* CONFIG_IEEE80211AC */

#ifdef CONFIG_P2P
	if (elems.p2p) {
		wpabuf_free(sta->p2p_ie);
		sta->p2p_ie = ieee802_11_vendor_ie_concat(ies, ies_len,
							  P2P_IE_VENDOR_TYPE);
		if (sta->p2p_ie)
			p2p_dev_addr = p2p_get_go_dev_addr(sta->p2p_ie);
	} else {
		wpabuf_free(sta->p2p_ie);
		sta->p2p_ie = NULL;
	}
#endif /* CONFIG_P2P */

	if ((hapd->conf->wpa & WPA_PROTO_RSN) && elems.rsn_ie) {
		wpa_ie = elems.rsn_ie;
		wpa_ie_len = elems.rsn_ie_len;
	} else if ((hapd->conf->wpa & WPA_PROTO_WPA) &&
		   elems.wpa_ie) {
		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;
	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

#ifdef CONFIG_WPS
	sta->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS | WLAN_STA_WPS2);
	if (hapd->conf->wps_state && elems.wps_ie) {
		wpa_printf(MSG_DEBUG, "STA included WPS IE in (Re)Association "
			   "Request - assume WPS is used");
		sta->flags |= WLAN_STA_WPS;
		wpabuf_free(sta->wps_ie);
		sta->wps_ie = ieee802_11_vendor_ie_concat(ies, ies_len,
							  WPS_IE_VENDOR_TYPE);
		if (sta->wps_ie && wps_is_20(sta->wps_ie)) {
			wpa_printf(MSG_DEBUG, "WPS: STA supports WPS 2.0");
			sta->flags |= WLAN_STA_WPS2;
		}
		wpa_ie = NULL;
		wpa_ie_len = 0;
		if (sta->wps_ie && wps_validate_assoc_req(sta->wps_ie) < 0) {
			wpa_printf(MSG_DEBUG, "WPS: Invalid WPS IE in "
				   "(Re)Association Request - reject");
			return WLAN_STATUS_INVALID_IE;
		}
	} else if (hapd->conf->wps_state && wpa_ie == NULL) {
		wpa_printf(MSG_DEBUG, "STA did not include WPA/RSN IE in "
			   "(Re)Association Request - possible WPS use");
		sta->flags |= WLAN_STA_MAYBE_WPS;
	} else
#endif /* CONFIG_WPS */
	if (hapd->conf->wpa && wpa_ie == NULL) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO,
			       "No WPA/RSN IE in association request");
		return WLAN_STATUS_INVALID_IE;
	}

	if (hapd->conf->wpa && wpa_ie) {
		int res;
		wpa_ie -= 2;
		wpa_ie_len += 2;
		if (sta->wpa_sm == NULL)
			sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth,
							sta->addr,
							p2p_dev_addr);
		if (sta->wpa_sm == NULL) {
			wpa_printf(MSG_WARNING, "Failed to initialize WPA "
				   "state machine");
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
		}
		res = wpa_validate_wpa_ie(hapd->wpa_auth, sta->wpa_sm,
					  wpa_ie, wpa_ie_len,
					  elems.mdie, elems.mdie_len,
					  elems.owe_dh, elems.owe_dh_len);
		resp = wpa_res_to_status_code(res);
		if (resp != WLAN_STATUS_SUCCESS)
			return resp;
#ifdef CONFIG_IEEE80211W
		if ((sta->flags & (WLAN_STA_ASSOC | WLAN_STA_MFP)) ==
		    (WLAN_STA_ASSOC | WLAN_STA_MFP) &&
		    !sta->sa_query_timed_out &&
		    sta->sa_query_count > 0)
			ap_check_sa_query_timeout(hapd, sta);
		if ((sta->flags & (WLAN_STA_ASSOC | WLAN_STA_MFP)) ==
		    (WLAN_STA_ASSOC | WLAN_STA_MFP) &&
		    !sta->sa_query_timed_out &&
		    (!reassoc || sta->auth_alg != WLAN_AUTH_FT)) {
			/*
			 * STA has already been associated with MFP and SA
			 * Query timeout has not been reached. Reject the
			 * association attempt temporarily and start SA Query,
			 * if one is not pending.
			 */

			if (sta->sa_query_count == 0)
				ap_sta_start_sa_query(hapd, sta);

			return WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY;
		}

		if (wpa_auth_uses_mfp(sta->wpa_sm))
			sta->flags |= WLAN_STA_MFP;
		else
			sta->flags &= ~WLAN_STA_MFP;
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_IEEE80211R_AP
		if (sta->auth_alg == WLAN_AUTH_FT) {
			if (!reassoc) {
				wpa_printf(MSG_DEBUG, "FT: " MACSTR " tried "
					   "to use association (not "
					   "re-association) with FT auth_alg",
					   MAC2STR(sta->addr));
				return WLAN_STATUS_UNSPECIFIED_FAILURE;
			}

			resp = wpa_ft_validate_reassoc(sta->wpa_sm, ies,
						       ies_len);
			if (resp != WLAN_STATUS_SUCCESS)
				return resp;
		}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_SAE
		if (wpa_auth_uses_sae(sta->wpa_sm) && sta->sae &&
		    sta->sae->state == SAE_ACCEPTED)
			wpa_auth_add_sae_pmkid(sta->wpa_sm, sta->sae->pmkid);

		if (wpa_auth_uses_sae(sta->wpa_sm) &&
		    sta->auth_alg == WLAN_AUTH_OPEN) {
			struct rsn_pmksa_cache_entry *sa;
			sa = wpa_auth_sta_get_pmksa(sta->wpa_sm);
			if (!sa || sa->akmp != WPA_KEY_MGMT_SAE) {
				wpa_printf(MSG_DEBUG,
					   "SAE: No PMKSA cache entry found for "
					   MACSTR, MAC2STR(sta->addr));
				return WLAN_STATUS_INVALID_PMKID;
			}
			wpa_printf(MSG_DEBUG, "SAE: " MACSTR
				   " using PMKSA caching", MAC2STR(sta->addr));
		} else if (wpa_auth_uses_sae(sta->wpa_sm) &&
			   sta->auth_alg != WLAN_AUTH_SAE &&
			   !(sta->auth_alg == WLAN_AUTH_FT &&
			     wpa_auth_uses_ft_sae(sta->wpa_sm))) {
			wpa_printf(MSG_DEBUG, "SAE: " MACSTR " tried to use "
				   "SAE AKM after non-SAE auth_alg %u",
				   MAC2STR(sta->addr), sta->auth_alg);
			return WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;
		}
#endif /* CONFIG_SAE */

#ifdef CONFIG_OWE
		if ((hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE) &&
		    wpa_auth_sta_key_mgmt(sta->wpa_sm) == WPA_KEY_MGMT_OWE &&
		    elems.owe_dh) {
			resp = owe_process_assoc_req(hapd, sta, elems.owe_dh,
						     elems.owe_dh_len);
			if (resp != WLAN_STATUS_SUCCESS)
				return resp;
		}
#endif /* CONFIG_OWE */

#ifdef CONFIG_IEEE80211N
		if ((sta->flags & (WLAN_STA_HT | WLAN_STA_VHT)) &&
		    wpa_auth_get_pairwise(sta->wpa_sm) == WPA_CIPHER_TKIP) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_INFO,
				       "Station tried to use TKIP with HT "
				       "association");
			return WLAN_STATUS_CIPHER_REJECTED_PER_POLICY;
		}
#endif /* CONFIG_IEEE80211N */
#ifdef CONFIG_HS20
	} else if (hapd->conf->osen) {
		if (elems.osen == NULL) {
			hostapd_logger(
				hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
				HOSTAPD_LEVEL_INFO,
				"No HS 2.0 OSEN element in association request");
			return WLAN_STATUS_INVALID_IE;
		}

		wpa_printf(MSG_DEBUG, "HS 2.0: OSEN association");
		if (sta->wpa_sm == NULL)
			sta->wpa_sm = wpa_auth_sta_init(hapd->wpa_auth,
							sta->addr, NULL);
		if (sta->wpa_sm == NULL) {
			wpa_printf(MSG_WARNING, "Failed to initialize WPA "
				   "state machine");
			return WLAN_STATUS_UNSPECIFIED_FAILURE;
		}
		if (wpa_validate_osen(hapd->wpa_auth, sta->wpa_sm,
				      elems.osen - 2, elems.osen_len + 2) < 0)
			return WLAN_STATUS_INVALID_IE;
#endif /* CONFIG_HS20 */
	} else
		wpa_auth_sta_no_wpa(sta->wpa_sm);

#ifdef CONFIG_P2P
	p2p_group_notif_assoc(hapd->p2p_group, sta->addr, ies, ies_len);
#endif /* CONFIG_P2P */

#ifdef CONFIG_HS20
	wpabuf_free(sta->hs20_ie);
	if (elems.hs20 && elems.hs20_len > 4) {
		sta->hs20_ie = wpabuf_alloc_copy(elems.hs20 + 4,
						 elems.hs20_len - 4);
	} else
		sta->hs20_ie = NULL;

	wpabuf_free(sta->roaming_consortium);
	if (elems.roaming_cons_sel)
		sta->roaming_consortium = wpabuf_alloc_copy(
			elems.roaming_cons_sel + 4,
			elems.roaming_cons_sel_len - 4);
	else
		sta->roaming_consortium = NULL;
#endif /* CONFIG_HS20 */

#ifdef CONFIG_FST
	wpabuf_free(sta->mb_ies);
	if (hapd->iface->fst)
		sta->mb_ies = mb_ies_by_info(&elems.mb_ies);
	else
		sta->mb_ies = NULL;
#endif /* CONFIG_FST */

#ifdef CONFIG_MBO
	mbo_ap_check_sta_assoc(hapd, sta, &elems);

	if (hapd->conf->mbo_enabled && (hapd->conf->wpa & 2) &&
	    elems.mbo && sta->cell_capa && !(sta->flags & WLAN_STA_MFP) &&
	    hapd->conf->ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		wpa_printf(MSG_INFO,
			   "MBO: Reject WPA2 association without PMF");
		return WLAN_STATUS_UNSPECIFIED_FAILURE;
	}
#endif /* CONFIG_MBO */

	ap_copy_sta_supp_op_classes(sta, elems.supp_op_classes,
				    elems.supp_op_classes_len);

	if ((sta->capability & WLAN_CAPABILITY_RADIO_MEASUREMENT) &&
	    elems.rrm_enabled &&
	    elems.rrm_enabled_len >= sizeof(sta->rrm_enabled_capa))
		os_memcpy(sta->rrm_enabled_capa, elems.rrm_enabled,
			  sizeof(sta->rrm_enabled_capa));

	if (elems.power_capab) {
		sta->min_tx_power = elems.power_capab[0];
		sta->max_tx_power = elems.power_capab[1];
		sta->power_capab = 1;
	} else {
		sta->power_capab = 0;
	}

	return WLAN_STATUS_SUCCESS;
}


static void send_deauth(struct hostapd_data *hapd, const u8 *addr,
			u16 reason_code)
{
	int send_len;
	struct ieee80211_mgmt reply;

	os_memset(&reply, 0, sizeof(reply));
	reply.frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_MGMT, WLAN_FC_STYPE_DEAUTH);
	os_memcpy(reply.da, addr, ETH_ALEN);
	os_memcpy(reply.sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(reply.bssid, hapd->own_addr, ETH_ALEN);

	send_len = IEEE80211_HDRLEN + sizeof(reply.u.deauth);
	reply.u.deauth.reason_code = host_to_le16(reason_code);

	if (hostapd_drv_send_mlme(hapd, &reply, send_len, 0) < 0)
		wpa_printf(MSG_INFO, "Failed to send deauth: %s",
			   strerror(errno));
}


static int add_associated_sta(struct hostapd_data *hapd,
			      struct sta_info *sta)
{
	struct ieee80211_ht_capabilities ht_cap;
	struct ieee80211_vht_capabilities vht_cap;
	int set = 1;

	/*
	 * Remove the STA entry to ensure the STA PS state gets cleared and
	 * configuration gets updated. This is relevant for cases, such as
	 * FT-over-the-DS, where a station re-associates back to the same AP but
	 * skips the authentication flow, or if working with a driver that
	 * does not support full AP client state.
	 *
	 * Skip this if the STA has already completed FT reassociation and the
	 * TK has been configured since the TX/RX PN must not be reset to 0 for
	 * the same key.
	 */
	if (!sta->added_unassoc &&
	    (!(sta->flags & WLAN_STA_AUTHORIZED) ||
	     (!wpa_auth_sta_ft_tk_already_set(sta->wpa_sm) &&
	      !wpa_auth_sta_fils_tk_already_set(sta->wpa_sm)))) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		wpa_auth_sm_event(sta->wpa_sm, WPA_DRV_STA_REMOVED);
		set = 0;
	}

#ifdef CONFIG_IEEE80211N
	if (sta->flags & WLAN_STA_HT)
		hostapd_get_ht_capab(hapd, sta->ht_capabilities, &ht_cap);
#endif /* CONFIG_IEEE80211N */
#ifdef CONFIG_IEEE80211AC
	if (sta->flags & WLAN_STA_VHT)
		hostapd_get_vht_capab(hapd, sta->vht_capabilities, &vht_cap);
#endif /* CONFIG_IEEE80211AC */

	/*
	 * Add the station with forced WLAN_STA_ASSOC flag. The sta->flags
	 * will be set when the ACK frame for the (Re)Association Response frame
	 * is processed (TX status driver event).
	 */
	if (hostapd_sta_add(hapd, sta->addr, sta->aid, sta->capability,
			    sta->supported_rates, sta->supported_rates_len,
			    sta->listen_interval,
			    sta->flags & WLAN_STA_HT ? &ht_cap : NULL,
			    sta->flags & WLAN_STA_VHT ? &vht_cap : NULL,
			    sta->flags | WLAN_STA_ASSOC, sta->qosinfo,
			    sta->vht_opmode, sta->p2p_ie ? 1 : 0,
			    set)) {
		hostapd_logger(hapd, sta->addr,
			       HOSTAPD_MODULE_IEEE80211, HOSTAPD_LEVEL_NOTICE,
			       "Could not %s STA to kernel driver",
			       set ? "set" : "add");

		if (sta->added_unassoc) {
			hostapd_drv_sta_remove(hapd, sta->addr);
			sta->added_unassoc = 0;
		}

		return -1;
	}

	sta->added_unassoc = 0;

	return 0;
}


static u16 send_assoc_resp(struct hostapd_data *hapd, struct sta_info *sta,
			   const u8 *addr, u16 status_code, int reassoc,
			   const u8 *ies, size_t ies_len)
{
	int send_len;
	u8 *buf;
	size_t buflen;
	struct ieee80211_mgmt *reply;
	u8 *p;
	u16 res = WLAN_STATUS_SUCCESS;

	buflen = sizeof(struct ieee80211_mgmt) + 1024;
#ifdef CONFIG_FILS
	if (sta && sta->fils_hlp_resp)
		buflen += wpabuf_len(sta->fils_hlp_resp);
#endif /* CONFIG_FILS */
#ifdef CONFIG_OWE
	if (sta && (hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE))
		buflen += 150;
#endif /* CONFIG_OWE */
	buf = os_zalloc(buflen);
	if (!buf) {
		res = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto done;
	}
	reply = (struct ieee80211_mgmt *) buf;
	reply->frame_control =
		IEEE80211_FC(WLAN_FC_TYPE_MGMT,
			     (reassoc ? WLAN_FC_STYPE_REASSOC_RESP :
			      WLAN_FC_STYPE_ASSOC_RESP));
	os_memcpy(reply->da, addr, ETH_ALEN);
	os_memcpy(reply->sa, hapd->own_addr, ETH_ALEN);
	os_memcpy(reply->bssid, hapd->own_addr, ETH_ALEN);

	send_len = IEEE80211_HDRLEN;
	send_len += sizeof(reply->u.assoc_resp);
	reply->u.assoc_resp.capab_info =
		host_to_le16(hostapd_own_capab_info(hapd));
	reply->u.assoc_resp.status_code = host_to_le16(status_code);

	reply->u.assoc_resp.aid = host_to_le16((sta ? sta->aid : 0) |
					       BIT(14) | BIT(15));
	/* Supported rates */
	p = hostapd_eid_supp_rates(hapd, reply->u.assoc_resp.variable);
	/* Extended supported rates */
	p = hostapd_eid_ext_supp_rates(hapd, p);

#ifdef CONFIG_IEEE80211R_AP
	if (sta && status_code == WLAN_STATUS_SUCCESS) {
		/* IEEE 802.11r: Mobility Domain Information, Fast BSS
		 * Transition Information, RSN, [RIC Response] */
		p = wpa_sm_write_assoc_resp_ies(sta->wpa_sm, p,
						buf + buflen - p,
						sta->auth_alg, ies, ies_len);
		if (!p) {
			wpa_printf(MSG_DEBUG,
				   "FT: Failed to write AssocResp IEs");
			res = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}
	}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_OWE
	if (sta && status_code == WLAN_STATUS_SUCCESS &&
	    (hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE))
		p = wpa_auth_write_assoc_resp_owe(sta->wpa_sm, p,
						  buf + buflen - p,
						  ies, ies_len);
#endif /* CONFIG_OWE */

#ifdef CONFIG_IEEE80211W
	if (sta && status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY)
		p = hostapd_eid_assoc_comeback_time(hapd, sta, p);
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_IEEE80211N
	p = hostapd_eid_ht_capabilities(hapd, p);
	p = hostapd_eid_ht_operation(hapd, p);
#endif /* CONFIG_IEEE80211N */

#ifdef CONFIG_IEEE80211AC
	if (hapd->iconf->ieee80211ac && !hapd->conf->disable_11ac) {
		u32 nsts = 0, sta_nsts;

		if (sta && hapd->conf->use_sta_nsts && sta->vht_capabilities) {
			struct ieee80211_vht_capabilities *capa;

			nsts = (hapd->iface->conf->vht_capab >>
				VHT_CAP_BEAMFORMEE_STS_OFFSET) & 7;
			capa = sta->vht_capabilities;
			sta_nsts = (le_to_host32(capa->vht_capabilities_info) >>
				    VHT_CAP_BEAMFORMEE_STS_OFFSET) & 7;

			if (nsts < sta_nsts)
				nsts = 0;
			else
				nsts = sta_nsts;
		}
		p = hostapd_eid_vht_capabilities(hapd, p, nsts);
		p = hostapd_eid_vht_operation(hapd, p);
	}
#endif /* CONFIG_IEEE80211AC */

	p = hostapd_eid_ext_capab(hapd, p);
	p = hostapd_eid_bss_max_idle_period(hapd, p);
	if (sta && sta->qos_map_enabled)
		p = hostapd_eid_qos_map_set(hapd, p);

#ifdef CONFIG_FST
	if (hapd->iface->fst_ies) {
		os_memcpy(p, wpabuf_head(hapd->iface->fst_ies),
			  wpabuf_len(hapd->iface->fst_ies));
		p += wpabuf_len(hapd->iface->fst_ies);
	}
#endif /* CONFIG_FST */

#ifdef CONFIG_IEEE80211AC
	if (sta && hapd->conf->vendor_vht && (sta->flags & WLAN_STA_VENDOR_VHT))
		p = hostapd_eid_vendor_vht(hapd, p);
#endif /* CONFIG_IEEE80211AC */

	if (sta && (sta->flags & WLAN_STA_WMM))
		p = hostapd_eid_wmm(hapd, p);

#ifdef CONFIG_WPS
	if (sta &&
	    ((sta->flags & WLAN_STA_WPS) ||
	     ((sta->flags & WLAN_STA_MAYBE_WPS) && hapd->conf->wpa))) {
		struct wpabuf *wps = wps_build_assoc_resp_ie();
		if (wps) {
			os_memcpy(p, wpabuf_head(wps), wpabuf_len(wps));
			p += wpabuf_len(wps);
			wpabuf_free(wps);
		}
	}
#endif /* CONFIG_WPS */

#ifdef CONFIG_P2P
	if (sta && sta->p2p_ie && hapd->p2p_group) {
		struct wpabuf *p2p_resp_ie;
		enum p2p_status_code status;
		switch (status_code) {
		case WLAN_STATUS_SUCCESS:
			status = P2P_SC_SUCCESS;
			break;
		case WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA:
			status = P2P_SC_FAIL_LIMIT_REACHED;
			break;
		default:
			status = P2P_SC_FAIL_INVALID_PARAMS;
			break;
		}
		p2p_resp_ie = p2p_group_assoc_resp_ie(hapd->p2p_group, status);
		if (p2p_resp_ie) {
			os_memcpy(p, wpabuf_head(p2p_resp_ie),
				  wpabuf_len(p2p_resp_ie));
			p += wpabuf_len(p2p_resp_ie);
			wpabuf_free(p2p_resp_ie);
		}
	}
#endif /* CONFIG_P2P */

#ifdef CONFIG_P2P_MANAGER
	if (hapd->conf->p2p & P2P_MANAGE)
		p = hostapd_eid_p2p_manage(hapd, p);
#endif /* CONFIG_P2P_MANAGER */

	p = hostapd_eid_mbo(hapd, p, buf + buflen - p);

	if (hapd->conf->assocresp_elements &&
	    (size_t) (buf + buflen - p) >=
	    wpabuf_len(hapd->conf->assocresp_elements)) {
		os_memcpy(p, wpabuf_head(hapd->conf->assocresp_elements),
			  wpabuf_len(hapd->conf->assocresp_elements));
		p += wpabuf_len(hapd->conf->assocresp_elements);
	}

	send_len += p - reply->u.assoc_resp.variable;

#ifdef CONFIG_FILS
	if (sta &&
	    (sta->auth_alg == WLAN_AUTH_FILS_SK ||
	     sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	     sta->auth_alg == WLAN_AUTH_FILS_PK) &&
	    status_code == WLAN_STATUS_SUCCESS) {
		struct ieee802_11_elems elems;

		if (ieee802_11_parse_elems(ies, ies_len, &elems, 0) ==
		    ParseFailed || !elems.fils_session) {
			res = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}

		/* FILS Session */
		*p++ = WLAN_EID_EXTENSION; /* Element ID */
		*p++ = 1 + FILS_SESSION_LEN; /* Length */
		*p++ = WLAN_EID_EXT_FILS_SESSION; /* Element ID Extension */
		os_memcpy(p, elems.fils_session, FILS_SESSION_LEN);
		send_len += 2 + 1 + FILS_SESSION_LEN;

		send_len = fils_encrypt_assoc(sta->wpa_sm, buf, send_len,
					      buflen, sta->fils_hlp_resp);
		if (send_len < 0) {
			res = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}
	}
#endif /* CONFIG_FILS */

#ifdef CONFIG_OWE
	if ((hapd->conf->wpa_key_mgmt & WPA_KEY_MGMT_OWE) &&
	    sta && sta->owe_ecdh && status_code == WLAN_STATUS_SUCCESS &&
	    wpa_auth_sta_key_mgmt(sta->wpa_sm) == WPA_KEY_MGMT_OWE) {
		struct wpabuf *pub;

		pub = crypto_ecdh_get_pubkey(sta->owe_ecdh, 0);
		if (!pub) {
			res = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto done;
		}
		/* OWE Diffie-Hellman Parameter element */
		*p++ = WLAN_EID_EXTENSION; /* Element ID */
		*p++ = 1 + 2 + wpabuf_len(pub); /* Length */
		*p++ = WLAN_EID_EXT_OWE_DH_PARAM; /* Element ID Extension */
		WPA_PUT_LE16(p, sta->owe_group);
		p += 2;
		os_memcpy(p, wpabuf_head(pub), wpabuf_len(pub));
		p += wpabuf_len(pub);
		send_len += 3 + 2 + wpabuf_len(pub);
		wpabuf_free(pub);
	}
#endif /* CONFIG_OWE */

	if (hostapd_drv_send_mlme(hapd, reply, send_len, 0) < 0) {
		wpa_printf(MSG_INFO, "Failed to send assoc resp: %s",
			   strerror(errno));
		res = WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

done:
	os_free(buf);
	return res;
}


#ifdef CONFIG_OWE
u8 * owe_assoc_req_process(struct hostapd_data *hapd, struct sta_info *sta,
			   const u8 *owe_dh, u8 owe_dh_len,
			   u8 *owe_buf, size_t owe_buf_len, u16 *reason)
{
#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->conf->own_ie_override) {
		wpa_printf(MSG_DEBUG, "OWE: Using IE override");
		*reason = WLAN_STATUS_SUCCESS;
		return wpa_auth_write_assoc_resp_owe(sta->wpa_sm, owe_buf,
						     owe_buf_len, NULL, 0);
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (wpa_auth_sta_get_pmksa(sta->wpa_sm)) {
		wpa_printf(MSG_DEBUG, "OWE: Using PMKSA caching");
		owe_buf = wpa_auth_write_assoc_resp_owe(sta->wpa_sm, owe_buf,
							owe_buf_len, NULL, 0);
		*reason = WLAN_STATUS_SUCCESS;
		return owe_buf;
	}

	*reason = owe_process_assoc_req(hapd, sta, owe_dh, owe_dh_len);
	if (*reason != WLAN_STATUS_SUCCESS)
		return NULL;

	owe_buf = wpa_auth_write_assoc_resp_owe(sta->wpa_sm, owe_buf,
						owe_buf_len, NULL, 0);

	if (sta->owe_ecdh && owe_buf) {
		struct wpabuf *pub;

		pub = crypto_ecdh_get_pubkey(sta->owe_ecdh, 0);
		if (!pub) {
			*reason = WLAN_STATUS_UNSPECIFIED_FAILURE;
			return owe_buf;
		}

		/* OWE Diffie-Hellman Parameter element */
		*owe_buf++ = WLAN_EID_EXTENSION; /* Element ID */
		*owe_buf++ = 1 + 2 + wpabuf_len(pub); /* Length */
		*owe_buf++ = WLAN_EID_EXT_OWE_DH_PARAM; /* Element ID Extension
							 */
		WPA_PUT_LE16(owe_buf, sta->owe_group);
		owe_buf += 2;
		os_memcpy(owe_buf, wpabuf_head(pub), wpabuf_len(pub));
		owe_buf += wpabuf_len(pub);
		wpabuf_free(pub);
	}

	return owe_buf;
}
#endif /* CONFIG_OWE */


#ifdef CONFIG_FILS

void fils_hlp_finish_assoc(struct hostapd_data *hapd, struct sta_info *sta)
{
	u16 reply_res;

	wpa_printf(MSG_DEBUG, "FILS: Finish association with " MACSTR,
		   MAC2STR(sta->addr));
	eloop_cancel_timeout(fils_hlp_timeout, hapd, sta);
	if (!sta->fils_pending_assoc_req)
		return;
	reply_res = send_assoc_resp(hapd, sta, sta->addr, WLAN_STATUS_SUCCESS,
				    sta->fils_pending_assoc_is_reassoc,
				    sta->fils_pending_assoc_req,
				    sta->fils_pending_assoc_req_len);
	os_free(sta->fils_pending_assoc_req);
	sta->fils_pending_assoc_req = NULL;
	sta->fils_pending_assoc_req_len = 0;
	wpabuf_free(sta->fils_hlp_resp);
	sta->fils_hlp_resp = NULL;
	wpabuf_free(sta->hlp_dhcp_discover);
	sta->hlp_dhcp_discover = NULL;

	/*
	 * Remove the station in case transmission of a success response fails.
	 * At this point the station was already added associated to the driver.
	 */
	if (reply_res != WLAN_STATUS_SUCCESS)
		hostapd_drv_sta_remove(hapd, sta->addr);
}


void fils_hlp_timeout(void *eloop_ctx, void *eloop_data)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = eloop_data;

	wpa_printf(MSG_DEBUG,
		   "FILS: HLP response timeout - continue with association response for "
		   MACSTR, MAC2STR(sta->addr));
	if (sta->fils_drv_assoc_finish)
		hostapd_notify_assoc_fils_finish(hapd, sta);
	else
		fils_hlp_finish_assoc(hapd, sta);
}

#endif /* CONFIG_FILS */


static void handle_assoc(struct hostapd_data *hapd,
			 const struct ieee80211_mgmt *mgmt, size_t len,
			 int reassoc)
{
	u16 capab_info, listen_interval, seq_ctrl, fc;
	u16 resp = WLAN_STATUS_SUCCESS, reply_res;
	const u8 *pos;
	int left, i;
	struct sta_info *sta;
	u8 *tmp = NULL;
	struct hostapd_sta_wpa_psk_short *psk = NULL;
	char *identity = NULL;
	char *radius_cui = NULL;
#ifdef CONFIG_FILS
	int delay_assoc = 0;
#endif /* CONFIG_FILS */

	if (len < IEEE80211_HDRLEN + (reassoc ? sizeof(mgmt->u.reassoc_req) :
				      sizeof(mgmt->u.assoc_req))) {
		wpa_printf(MSG_INFO, "handle_assoc(reassoc=%d) - too short payload (len=%lu)",
			   reassoc, (unsigned long) len);
		return;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (reassoc) {
		if (hapd->iconf->ignore_reassoc_probability > 0.0 &&
		    drand48() < hapd->iconf->ignore_reassoc_probability) {
			wpa_printf(MSG_INFO,
				   "TESTING: ignoring reassoc request from "
				   MACSTR, MAC2STR(mgmt->sa));
			return;
		}
	} else {
		if (hapd->iconf->ignore_assoc_probability > 0.0 &&
		    drand48() < hapd->iconf->ignore_assoc_probability) {
			wpa_printf(MSG_INFO,
				   "TESTING: ignoring assoc request from "
				   MACSTR, MAC2STR(mgmt->sa));
			return;
		}
	}
#endif /* CONFIG_TESTING_OPTIONS */

	fc = le_to_host16(mgmt->frame_control);
	seq_ctrl = le_to_host16(mgmt->seq_ctrl);

	if (reassoc) {
		capab_info = le_to_host16(mgmt->u.reassoc_req.capab_info);
		listen_interval = le_to_host16(
			mgmt->u.reassoc_req.listen_interval);
		wpa_printf(MSG_DEBUG, "reassociation request: STA=" MACSTR
			   " capab_info=0x%02x listen_interval=%d current_ap="
			   MACSTR " seq_ctrl=0x%x%s",
			   MAC2STR(mgmt->sa), capab_info, listen_interval,
			   MAC2STR(mgmt->u.reassoc_req.current_ap),
			   seq_ctrl, (fc & WLAN_FC_RETRY) ? " retry" : "");
		left = len - (IEEE80211_HDRLEN + sizeof(mgmt->u.reassoc_req));
		pos = mgmt->u.reassoc_req.variable;
	} else {
		capab_info = le_to_host16(mgmt->u.assoc_req.capab_info);
		listen_interval = le_to_host16(
			mgmt->u.assoc_req.listen_interval);
		wpa_printf(MSG_DEBUG, "association request: STA=" MACSTR
			   " capab_info=0x%02x listen_interval=%d "
			   "seq_ctrl=0x%x%s",
			   MAC2STR(mgmt->sa), capab_info, listen_interval,
			   seq_ctrl, (fc & WLAN_FC_RETRY) ? " retry" : "");
		left = len - (IEEE80211_HDRLEN + sizeof(mgmt->u.assoc_req));
		pos = mgmt->u.assoc_req.variable;
	}

	sta = ap_get_sta(hapd, mgmt->sa);
#ifdef CONFIG_IEEE80211R_AP
	if (sta && sta->auth_alg == WLAN_AUTH_FT &&
	    (sta->flags & WLAN_STA_AUTH) == 0) {
		wpa_printf(MSG_DEBUG, "FT: Allow STA " MACSTR " to associate "
			   "prior to authentication since it is using "
			   "over-the-DS FT", MAC2STR(mgmt->sa));

		/*
		 * Mark station as authenticated, to avoid adding station
		 * entry in the driver as associated and not authenticated
		 */
		sta->flags |= WLAN_STA_AUTH;
	} else
#endif /* CONFIG_IEEE80211R_AP */
	if (sta == NULL || (sta->flags & WLAN_STA_AUTH) == 0) {
		if (hapd->iface->current_mode &&
		    hapd->iface->current_mode->mode ==
			HOSTAPD_MODE_IEEE80211AD) {
			int acl_res;
			u32 session_timeout, acct_interim_interval;
			struct vlan_description vlan_id;

			acl_res = ieee802_11_allowed_address(
				hapd, mgmt->sa, (const u8 *) mgmt, len,
				&session_timeout, &acct_interim_interval,
				&vlan_id, &psk, &identity, &radius_cui, 0);
			if (acl_res == HOSTAPD_ACL_REJECT) {
				wpa_msg(hapd->msg_ctx, MSG_DEBUG,
					"Ignore Association Request frame from "
					MACSTR " due to ACL reject",
					MAC2STR(mgmt->sa));
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto fail;
			}
			if (acl_res == HOSTAPD_ACL_PENDING)
				return;

			/* DMG/IEEE 802.11ad does not use authentication.
			 * Allocate sta entry upon association. */
			sta = ap_sta_add(hapd, mgmt->sa);
			if (!sta) {
				hostapd_logger(hapd, mgmt->sa,
					       HOSTAPD_MODULE_IEEE80211,
					       HOSTAPD_LEVEL_INFO,
					       "Failed to add STA");
				resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
				goto fail;
			}

			acl_res = ieee802_11_set_radius_info(
				hapd, sta, acl_res, session_timeout,
				acct_interim_interval, &vlan_id, &psk,
				&identity, &radius_cui);
			if (acl_res) {
				resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
				goto fail;
			}

			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG,
				       "Skip authentication for DMG/IEEE 802.11ad");
			sta->flags |= WLAN_STA_AUTH;
			wpa_auth_sm_event(sta->wpa_sm, WPA_AUTH);
			sta->auth_alg = WLAN_AUTH_OPEN;
		} else {
			hostapd_logger(hapd, mgmt->sa,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_INFO,
				       "Station tried to associate before authentication (aid=%d flags=0x%x)",
				       sta ? sta->aid : -1,
				       sta ? sta->flags : 0);
			send_deauth(hapd, mgmt->sa,
				    WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA);
			return;
		}
	}

	if ((fc & WLAN_FC_RETRY) &&
	    sta->last_seq_ctrl != WLAN_INVALID_MGMT_SEQ &&
	    sta->last_seq_ctrl == seq_ctrl &&
	    sta->last_subtype == (reassoc ? WLAN_FC_STYPE_REASSOC_REQ :
				  WLAN_FC_STYPE_ASSOC_REQ)) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Drop repeated association frame seq_ctrl=0x%x",
			       seq_ctrl);
		return;
	}
	sta->last_seq_ctrl = seq_ctrl;
	sta->last_subtype = reassoc ? WLAN_FC_STYPE_REASSOC_REQ :
		WLAN_FC_STYPE_ASSOC_REQ;

	if (hapd->tkip_countermeasures) {
		resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
		goto fail;
	}

	if (listen_interval > hapd->conf->max_listen_interval) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Too large Listen Interval (%d)",
			       listen_interval);
		resp = WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE;
		goto fail;
	}

#ifdef CONFIG_MBO
	if (hapd->conf->mbo_enabled && hapd->mbo_assoc_disallow) {
		resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
		goto fail;
	}
#endif /* CONFIG_MBO */

	/*
	 * sta->capability is used in check_assoc_ies() for RRM enabled
	 * capability element.
	 */
	sta->capability = capab_info;

#ifdef CONFIG_FILS
	if (sta->auth_alg == WLAN_AUTH_FILS_SK ||
	    sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	    sta->auth_alg == WLAN_AUTH_FILS_PK) {
		int res;

		/* The end of the payload is encrypted. Need to decrypt it
		 * before parsing. */

		tmp = os_memdup(pos, left);
		if (!tmp) {
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}

		res = fils_decrypt_assoc(sta->wpa_sm, sta->fils_session, mgmt,
					 len, tmp, left);
		if (res < 0) {
			resp = WLAN_STATUS_UNSPECIFIED_FAILURE;
			goto fail;
		}
		pos = tmp;
		left = res;
	}
#endif /* CONFIG_FILS */

	/* followed by SSID and Supported rates; and HT capabilities if 802.11n
	 * is used */
	resp = check_assoc_ies(hapd, sta, pos, left, reassoc);
	if (resp != WLAN_STATUS_SUCCESS)
		goto fail;

	if (hostapd_get_aid(hapd, sta) < 0) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "No room for more AIDs");
		resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
		goto fail;
	}

	sta->listen_interval = listen_interval;

	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G)
		sta->flags |= WLAN_STA_NONERP;
	for (i = 0; i < sta->supported_rates_len; i++) {
		if ((sta->supported_rates[i] & 0x7f) > 22) {
			sta->flags &= ~WLAN_STA_NONERP;
			break;
		}
	}
	if (sta->flags & WLAN_STA_NONERP && !sta->nonerp_set) {
		sta->nonerp_set = 1;
		hapd->iface->num_sta_non_erp++;
		if (hapd->iface->num_sta_non_erp == 1)
			ieee802_11_set_beacons(hapd->iface);
	}

	if (!(sta->capability & WLAN_CAPABILITY_SHORT_SLOT_TIME) &&
	    !sta->no_short_slot_time_set) {
		sta->no_short_slot_time_set = 1;
		hapd->iface->num_sta_no_short_slot_time++;
		if (hapd->iface->current_mode &&
		    hapd->iface->current_mode->mode ==
		    HOSTAPD_MODE_IEEE80211G &&
		    hapd->iface->num_sta_no_short_slot_time == 1)
			ieee802_11_set_beacons(hapd->iface);
	}

	if (sta->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		sta->flags |= WLAN_STA_SHORT_PREAMBLE;
	else
		sta->flags &= ~WLAN_STA_SHORT_PREAMBLE;

	if (!(sta->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) &&
	    !sta->no_short_preamble_set) {
		sta->no_short_preamble_set = 1;
		hapd->iface->num_sta_no_short_preamble++;
		if (hapd->iface->current_mode &&
		    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G
		    && hapd->iface->num_sta_no_short_preamble == 1)
			ieee802_11_set_beacons(hapd->iface);
	}

#ifdef CONFIG_IEEE80211N
	update_ht_state(hapd, sta);
#endif /* CONFIG_IEEE80211N */

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "association OK (aid %d)", sta->aid);
	/* Station will be marked associated, after it acknowledges AssocResp
	 */
	sta->flags |= WLAN_STA_ASSOC_REQ_OK;

#ifdef CONFIG_IEEE80211W
	if ((sta->flags & WLAN_STA_MFP) && sta->sa_query_timed_out) {
		wpa_printf(MSG_DEBUG, "Allowing %sassociation after timed out "
			   "SA Query procedure", reassoc ? "re" : "");
		/* TODO: Send a protected Disassociate frame to the STA using
		 * the old key and Reason Code "Previous Authentication no
		 * longer valid". Make sure this is only sent protected since
		 * unprotected frame would be received by the STA that is now
		 * trying to associate.
		 */
	}
#endif /* CONFIG_IEEE80211W */

	/* Make sure that the previously registered inactivity timer will not
	 * remove the STA immediately. */
	sta->timeout_next = STA_NULLFUNC;

#ifdef CONFIG_TAXONOMY
	taxonomy_sta_info_assoc_req(hapd, sta, pos, left);
#endif /* CONFIG_TAXONOMY */

	sta->pending_wds_enable = 0;

#ifdef CONFIG_FILS
	if (sta->auth_alg == WLAN_AUTH_FILS_SK ||
	    sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	    sta->auth_alg == WLAN_AUTH_FILS_PK) {
		if (fils_process_hlp(hapd, sta, pos, left) > 0)
			delay_assoc = 1;
	}
#endif /* CONFIG_FILS */

 fail:
	os_free(identity);
	os_free(radius_cui);
	hostapd_free_psk_list(psk);

	/*
	 * In case of a successful response, add the station to the driver.
	 * Otherwise, the kernel may ignore Data frames before we process the
	 * ACK frame (TX status). In case of a failure, this station will be
	 * removed.
	 *
	 * Note that this is not compliant with the IEEE 802.11 standard that
	 * states that a non-AP station should transition into the
	 * authenticated/associated state only after the station acknowledges
	 * the (Re)Association Response frame. However, still do this as:
	 *
	 * 1. In case the station does not acknowledge the (Re)Association
	 *    Response frame, it will be removed.
	 * 2. Data frames will be dropped in the kernel until the station is
	 *    set into authorized state, and there are no significant known
	 *    issues with processing other non-Data Class 3 frames during this
	 *    window.
	 */
	if (resp == WLAN_STATUS_SUCCESS && sta && add_associated_sta(hapd, sta))
		resp = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;

#ifdef CONFIG_FILS
	if (sta) {
		eloop_cancel_timeout(fils_hlp_timeout, hapd, sta);
		os_free(sta->fils_pending_assoc_req);
		sta->fils_pending_assoc_req = NULL;
		sta->fils_pending_assoc_req_len = 0;
		wpabuf_free(sta->fils_hlp_resp);
		sta->fils_hlp_resp = NULL;
	}
	if (sta && delay_assoc && resp == WLAN_STATUS_SUCCESS) {
		sta->fils_pending_assoc_req = tmp;
		sta->fils_pending_assoc_req_len = left;
		sta->fils_pending_assoc_is_reassoc = reassoc;
		sta->fils_drv_assoc_finish = 0;
		wpa_printf(MSG_DEBUG,
			   "FILS: Waiting for HLP processing before sending (Re)Association Response frame to "
			   MACSTR, MAC2STR(sta->addr));
		eloop_cancel_timeout(fils_hlp_timeout, hapd, sta);
		eloop_register_timeout(0, hapd->conf->fils_hlp_wait_time * 1024,
				       fils_hlp_timeout, hapd, sta);
		return;
	}
#endif /* CONFIG_FILS */

	reply_res = send_assoc_resp(hapd, sta, mgmt->sa, resp, reassoc, pos,
				    left);
	os_free(tmp);

	/*
	 * Remove the station in case tranmission of a success response fails
	 * (the STA was added associated to the driver) or if the station was
	 * previously added unassociated.
	 */
	if (sta && ((reply_res != WLAN_STATUS_SUCCESS &&
		     resp == WLAN_STATUS_SUCCESS) || sta->added_unassoc)) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->added_unassoc = 0;
	}
}


static void handle_disassoc(struct hostapd_data *hapd,
			    const struct ieee80211_mgmt *mgmt, size_t len)
{
	struct sta_info *sta;

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.disassoc)) {
		wpa_printf(MSG_INFO, "handle_disassoc - too short payload (len=%lu)",
			   (unsigned long) len);
		return;
	}

	wpa_printf(MSG_DEBUG, "disassocation: STA=" MACSTR " reason_code=%d",
		   MAC2STR(mgmt->sa),
		   le_to_host16(mgmt->u.disassoc.reason_code));

	sta = ap_get_sta(hapd, mgmt->sa);
	if (sta == NULL) {
		wpa_printf(MSG_INFO, "Station " MACSTR " trying to disassociate, but it is not associated",
			   MAC2STR(mgmt->sa));
		return;
	}

	ap_sta_set_authorized(hapd, sta, 0);
	sta->last_seq_ctrl = WLAN_INVALID_MGMT_SEQ;
	sta->flags &= ~(WLAN_STA_ASSOC | WLAN_STA_ASSOC_REQ_OK);
	wpa_auth_sm_event(sta->wpa_sm, WPA_DISASSOC);
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO, "disassociated");
	sta->acct_terminate_cause = RADIUS_ACCT_TERMINATE_CAUSE_USER_REQUEST;
	ieee802_1x_notify_port_enabled(sta->eapol_sm, 0);
	/* Stop Accounting and IEEE 802.1X sessions, but leave the STA
	 * authenticated. */
	accounting_sta_stop(hapd, sta);
	ieee802_1x_free_station(hapd, sta);
	if (sta->ipaddr)
		hostapd_drv_br_delete_ip_neigh(hapd, 4, (u8 *) &sta->ipaddr);
	ap_sta_ip6addr_del(hapd, sta);
	hostapd_drv_sta_remove(hapd, sta->addr);
	sta->added_unassoc = 0;

	if (sta->timeout_next == STA_NULLFUNC ||
	    sta->timeout_next == STA_DISASSOC) {
		sta->timeout_next = STA_DEAUTH;
		eloop_cancel_timeout(ap_handle_timer, hapd, sta);
		eloop_register_timeout(AP_DEAUTH_DELAY, 0, ap_handle_timer,
				       hapd, sta);
	}

	mlme_disassociate_indication(
		hapd, sta, le_to_host16(mgmt->u.disassoc.reason_code));

	/* DMG/IEEE 802.11ad does not use deauthication. Deallocate sta upon
	 * disassociation. */
	if (hapd->iface->current_mode &&
	    hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211AD) {
		sta->flags &= ~WLAN_STA_AUTH;
		wpa_auth_sm_event(sta->wpa_sm, WPA_DEAUTH);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG, "deauthenticated");
		ap_free_sta(hapd, sta);
	}
}


static void handle_deauth(struct hostapd_data *hapd,
			  const struct ieee80211_mgmt *mgmt, size_t len)
{
	struct sta_info *sta;

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.deauth)) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "handle_deauth - too short "
			"payload (len=%lu)", (unsigned long) len);
		return;
	}

	wpa_msg(hapd->msg_ctx, MSG_DEBUG, "deauthentication: STA=" MACSTR
		" reason_code=%d",
		MAC2STR(mgmt->sa), le_to_host16(mgmt->u.deauth.reason_code));

	sta = ap_get_sta(hapd, mgmt->sa);
	if (sta == NULL) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "Station " MACSTR " trying "
			"to deauthenticate, but it is not authenticated",
			MAC2STR(mgmt->sa));
		return;
	}

	ap_sta_set_authorized(hapd, sta, 0);
	sta->last_seq_ctrl = WLAN_INVALID_MGMT_SEQ;
	sta->flags &= ~(WLAN_STA_AUTH | WLAN_STA_ASSOC |
			WLAN_STA_ASSOC_REQ_OK);
	wpa_auth_sm_event(sta->wpa_sm, WPA_DEAUTH);
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG, "deauthenticated");
	mlme_deauthenticate_indication(
		hapd, sta, le_to_host16(mgmt->u.deauth.reason_code));
	sta->acct_terminate_cause = RADIUS_ACCT_TERMINATE_CAUSE_USER_REQUEST;
	ieee802_1x_notify_port_enabled(sta->eapol_sm, 0);
	ap_free_sta(hapd, sta);
}


static void handle_beacon(struct hostapd_data *hapd,
			  const struct ieee80211_mgmt *mgmt, size_t len,
			  struct hostapd_frame_info *fi)
{
	struct ieee802_11_elems elems;

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.beacon)) {
		wpa_printf(MSG_INFO, "handle_beacon - too short payload (len=%lu)",
			   (unsigned long) len);
		return;
	}

	(void) ieee802_11_parse_elems(mgmt->u.beacon.variable,
				      len - (IEEE80211_HDRLEN +
					     sizeof(mgmt->u.beacon)), &elems,
				      0);

	ap_list_process_beacon(hapd->iface, mgmt, &elems, fi);
}


#ifdef CONFIG_IEEE80211W

static int hostapd_sa_query_action(struct hostapd_data *hapd,
				   const struct ieee80211_mgmt *mgmt,
				   size_t len)
{
	const u8 *end;

	end = mgmt->u.action.u.sa_query_resp.trans_id +
		WLAN_SA_QUERY_TR_ID_LEN;
	if (((u8 *) mgmt) + len < end) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: Too short SA Query Action "
			   "frame (len=%lu)", (unsigned long) len);
		return 0;
	}

	ieee802_11_sa_query_action(hapd, mgmt->sa,
				   mgmt->u.action.u.sa_query_resp.action,
				   mgmt->u.action.u.sa_query_resp.trans_id);
	return 1;
}


static int robust_action_frame(u8 category)
{
	return category != WLAN_ACTION_PUBLIC &&
		category != WLAN_ACTION_HT;
}
#endif /* CONFIG_IEEE80211W */


static int handle_action(struct hostapd_data *hapd,
			 const struct ieee80211_mgmt *mgmt, size_t len,
			 unsigned int freq)
{
	struct sta_info *sta;
	u8 *action __maybe_unused;

	if (len < IEEE80211_HDRLEN + 2 + 1) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "handle_action - too short payload (len=%lu)",
			       (unsigned long) len);
		return 0;
	}

	action = (u8 *) &mgmt->u.action.u;
	wpa_printf(MSG_DEBUG, "RX_ACTION category %u action %u sa " MACSTR
		   " da " MACSTR " len %d freq %u",
		   mgmt->u.action.category, *action,
		   MAC2STR(mgmt->sa), MAC2STR(mgmt->da), (int) len, freq);

	sta = ap_get_sta(hapd, mgmt->sa);

	if (mgmt->u.action.category != WLAN_ACTION_PUBLIC &&
	    (sta == NULL || !(sta->flags & WLAN_STA_ASSOC))) {
		wpa_printf(MSG_DEBUG, "IEEE 802.11: Ignored Action "
			   "frame (category=%u) from unassociated STA " MACSTR,
			   mgmt->u.action.category, MAC2STR(mgmt->sa));
		return 0;
	}

#ifdef CONFIG_IEEE80211W
	if (sta && (sta->flags & WLAN_STA_MFP) &&
	    !(mgmt->frame_control & host_to_le16(WLAN_FC_ISWEP)) &&
	    robust_action_frame(mgmt->u.action.category)) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Dropped unprotected Robust Action frame from "
			       "an MFP STA");
		return 0;
	}
#endif /* CONFIG_IEEE80211W */

	if (sta) {
		u16 fc = le_to_host16(mgmt->frame_control);
		u16 seq_ctrl = le_to_host16(mgmt->seq_ctrl);

		if ((fc & WLAN_FC_RETRY) &&
		    sta->last_seq_ctrl != WLAN_INVALID_MGMT_SEQ &&
		    sta->last_seq_ctrl == seq_ctrl &&
		    sta->last_subtype == WLAN_FC_STYPE_ACTION) {
			hostapd_logger(hapd, sta->addr,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_DEBUG,
				       "Drop repeated action frame seq_ctrl=0x%x",
				       seq_ctrl);
			return 1;
		}

		sta->last_seq_ctrl = seq_ctrl;
		sta->last_subtype = WLAN_FC_STYPE_ACTION;
	}

	switch (mgmt->u.action.category) {
#ifdef CONFIG_IEEE80211R_AP
	case WLAN_ACTION_FT:
		if (!sta ||
		    wpa_ft_action_rx(sta->wpa_sm, (u8 *) &mgmt->u.action,
				     len - IEEE80211_HDRLEN))
			break;
		return 1;
#endif /* CONFIG_IEEE80211R_AP */
	case WLAN_ACTION_WMM:
		hostapd_wmm_action(hapd, mgmt, len);
		return 1;
#ifdef CONFIG_IEEE80211W
	case WLAN_ACTION_SA_QUERY:
		return hostapd_sa_query_action(hapd, mgmt, len);
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_WNM_AP
	case WLAN_ACTION_WNM:
		ieee802_11_rx_wnm_action_ap(hapd, mgmt, len);
		return 1;
#endif /* CONFIG_WNM_AP */
#ifdef CONFIG_FST
	case WLAN_ACTION_FST:
		if (hapd->iface->fst)
			fst_rx_action(hapd->iface->fst, mgmt, len);
		else
			wpa_printf(MSG_DEBUG,
				   "FST: Ignore FST Action frame - no FST attached");
		return 1;
#endif /* CONFIG_FST */
	case WLAN_ACTION_PUBLIC:
	case WLAN_ACTION_PROTECTED_DUAL:
#ifdef CONFIG_IEEE80211N
		if (len >= IEEE80211_HDRLEN + 2 &&
		    mgmt->u.action.u.public_action.action ==
		    WLAN_PA_20_40_BSS_COEX) {
			hostapd_2040_coex_action(hapd, mgmt, len);
			return 1;
		}
#endif /* CONFIG_IEEE80211N */
#ifdef CONFIG_DPP
		if (len >= IEEE80211_HDRLEN + 6 &&
		    mgmt->u.action.u.vs_public_action.action ==
		    WLAN_PA_VENDOR_SPECIFIC &&
		    WPA_GET_BE24(mgmt->u.action.u.vs_public_action.oui) ==
		    OUI_WFA &&
		    mgmt->u.action.u.vs_public_action.variable[0] ==
		    DPP_OUI_TYPE) {
			const u8 *pos, *end;

			pos = mgmt->u.action.u.vs_public_action.oui;
			end = ((const u8 *) mgmt) + len;
			hostapd_dpp_rx_action(hapd, mgmt->sa, pos, end - pos,
					      freq);
			return 1;
		}
		if (len >= IEEE80211_HDRLEN + 2 &&
		    (mgmt->u.action.u.public_action.action ==
		     WLAN_PA_GAS_INITIAL_RESP ||
		     mgmt->u.action.u.public_action.action ==
		     WLAN_PA_GAS_COMEBACK_RESP)) {
			const u8 *pos, *end;

			pos = &mgmt->u.action.u.public_action.action;
			end = ((const u8 *) mgmt) + len;
			gas_query_ap_rx(hapd->gas, mgmt->sa,
					mgmt->u.action.category,
					pos, end - pos, hapd->iface->freq);
			return 1;
		}
#endif /* CONFIG_DPP */
		if (hapd->public_action_cb) {
			hapd->public_action_cb(hapd->public_action_cb_ctx,
					       (u8 *) mgmt, len,
					       hapd->iface->freq);
		}
		if (hapd->public_action_cb2) {
			hapd->public_action_cb2(hapd->public_action_cb2_ctx,
						(u8 *) mgmt, len,
						hapd->iface->freq);
		}
		if (hapd->public_action_cb || hapd->public_action_cb2)
			return 1;
		break;
	case WLAN_ACTION_VENDOR_SPECIFIC:
		if (hapd->vendor_action_cb) {
			if (hapd->vendor_action_cb(hapd->vendor_action_cb_ctx,
						   (u8 *) mgmt, len,
						   hapd->iface->freq) == 0)
				return 1;
		}
		break;
	case WLAN_ACTION_RADIO_MEASUREMENT:
		hostapd_handle_radio_measurement(hapd, (const u8 *) mgmt, len);
		return 1;
	}

	hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_DEBUG,
		       "handle_action - unknown action category %d or invalid "
		       "frame",
		       mgmt->u.action.category);
	if (!is_multicast_ether_addr(mgmt->da) &&
	    !(mgmt->u.action.category & 0x80) &&
	    !is_multicast_ether_addr(mgmt->sa)) {
		struct ieee80211_mgmt *resp;

		/*
		 * IEEE 802.11-REVma/D9.0 - 7.3.1.11
		 * Return the Action frame to the source without change
		 * except that MSB of the Category set to 1.
		 */
		wpa_printf(MSG_DEBUG, "IEEE 802.11: Return unknown Action "
			   "frame back to sender");
		resp = os_memdup(mgmt, len);
		if (resp == NULL)
			return 0;
		os_memcpy(resp->da, resp->sa, ETH_ALEN);
		os_memcpy(resp->sa, hapd->own_addr, ETH_ALEN);
		os_memcpy(resp->bssid, hapd->own_addr, ETH_ALEN);
		resp->u.action.category |= 0x80;

		if (hostapd_drv_send_mlme(hapd, resp, len, 0) < 0) {
			wpa_printf(MSG_ERROR, "IEEE 802.11: Failed to send "
				   "Action frame");
		}
		os_free(resp);
	}

	return 1;
}


/**
 * ieee802_11_mgmt - process incoming IEEE 802.11 management frames
 * @hapd: hostapd BSS data structure (the BSS to which the management frame was
 * sent to)
 * @buf: management frame data (starting from IEEE 802.11 header)
 * @len: length of frame data in octets
 * @fi: meta data about received frame (signal level, etc.)
 *
 * Process all incoming IEEE 802.11 management frames. This will be called for
 * each frame received from the kernel driver through wlan#ap interface. In
 * addition, it can be called to re-inserted pending frames (e.g., when using
 * external RADIUS server as an MAC ACL).
 */
int ieee802_11_mgmt(struct hostapd_data *hapd, const u8 *buf, size_t len,
		    struct hostapd_frame_info *fi)
{
	struct ieee80211_mgmt *mgmt;
	u16 fc, stype;
	int ret = 0;
	unsigned int freq;
	int ssi_signal = fi ? fi->ssi_signal : 0;

	if (len < 24)
		return 0;

	if (fi && fi->freq)
		freq = fi->freq;
	else
		freq = hapd->iface->freq;

	mgmt = (struct ieee80211_mgmt *) buf;
	fc = le_to_host16(mgmt->frame_control);
	stype = WLAN_FC_GET_STYPE(fc);

	if (stype == WLAN_FC_STYPE_BEACON) {
		handle_beacon(hapd, mgmt, len, fi);
		return 1;
	}

	if (!is_broadcast_ether_addr(mgmt->bssid) &&
#ifdef CONFIG_P2P
	    /* Invitation responses can be sent with the peer MAC as BSSID */
	    !((hapd->conf->p2p & P2P_GROUP_OWNER) &&
	      stype == WLAN_FC_STYPE_ACTION) &&
#endif /* CONFIG_P2P */
#ifdef CONFIG_MESH
	    !(hapd->conf->mesh & MESH_ENABLED) &&
#endif /* CONFIG_MESH */
	    os_memcmp(mgmt->bssid, hapd->own_addr, ETH_ALEN) != 0) {
		wpa_printf(MSG_INFO, "MGMT: BSSID=" MACSTR " not our address",
			   MAC2STR(mgmt->bssid));
		return 0;
	}


	if (stype == WLAN_FC_STYPE_PROBE_REQ) {
		handle_probe_req(hapd, mgmt, len, ssi_signal);
		return 1;
	}

	if ((!is_broadcast_ether_addr(mgmt->da) ||
	     stype != WLAN_FC_STYPE_ACTION) &&
	    os_memcmp(mgmt->da, hapd->own_addr, ETH_ALEN) != 0) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "MGMT: DA=" MACSTR " not our address",
			       MAC2STR(mgmt->da));
		return 0;
	}

	if (hapd->iconf->track_sta_max_num)
		sta_track_add(hapd->iface, mgmt->sa, ssi_signal);

	switch (stype) {
	case WLAN_FC_STYPE_AUTH:
		wpa_printf(MSG_DEBUG, "mgmt::auth");
		handle_auth(hapd, mgmt, len);
		ret = 1;
		break;
	case WLAN_FC_STYPE_ASSOC_REQ:
		wpa_printf(MSG_DEBUG, "mgmt::assoc_req");
		handle_assoc(hapd, mgmt, len, 0);
		ret = 1;
		break;
	case WLAN_FC_STYPE_REASSOC_REQ:
		wpa_printf(MSG_DEBUG, "mgmt::reassoc_req");
		handle_assoc(hapd, mgmt, len, 1);
		ret = 1;
		break;
	case WLAN_FC_STYPE_DISASSOC:
		wpa_printf(MSG_DEBUG, "mgmt::disassoc");
		handle_disassoc(hapd, mgmt, len);
		ret = 1;
		break;
	case WLAN_FC_STYPE_DEAUTH:
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "mgmt::deauth");
		handle_deauth(hapd, mgmt, len);
		ret = 1;
		break;
	case WLAN_FC_STYPE_ACTION:
		wpa_printf(MSG_DEBUG, "mgmt::action");
		ret = handle_action(hapd, mgmt, len, freq);
		break;
	default:
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "unknown mgmt frame subtype %d", stype);
		break;
	}

	return ret;
}


static void handle_auth_cb(struct hostapd_data *hapd,
			   const struct ieee80211_mgmt *mgmt,
			   size_t len, int ok)
{
	u16 auth_alg, auth_transaction, status_code;
	struct sta_info *sta;

	sta = ap_get_sta(hapd, mgmt->da);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "handle_auth_cb: STA " MACSTR
			   " not found",
			   MAC2STR(mgmt->da));
		return;
	}

	auth_alg = le_to_host16(mgmt->u.auth.auth_alg);
	auth_transaction = le_to_host16(mgmt->u.auth.auth_transaction);
	status_code = le_to_host16(mgmt->u.auth.status_code);

	if (!ok) {
		hostapd_logger(hapd, mgmt->da, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_NOTICE,
			       "did not acknowledge authentication response");
		goto fail;
	}

	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.auth)) {
		wpa_printf(MSG_INFO, "handle_auth_cb - too short payload (len=%lu)",
			   (unsigned long) len);
		goto fail;
	}

	if (status_code == WLAN_STATUS_SUCCESS &&
	    ((auth_alg == WLAN_AUTH_OPEN && auth_transaction == 2) ||
	     (auth_alg == WLAN_AUTH_SHARED_KEY && auth_transaction == 4))) {
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_INFO, "authenticated");
		sta->flags |= WLAN_STA_AUTH;
		if (sta->added_unassoc)
			hostapd_set_sta_flags(hapd, sta);
		return;
	}

fail:
	if (status_code != WLAN_STATUS_SUCCESS && sta->added_unassoc) {
		hostapd_drv_sta_remove(hapd, sta->addr);
		sta->added_unassoc = 0;
	}
}


static void hostapd_set_wds_encryption(struct hostapd_data *hapd,
				       struct sta_info *sta,
				       char *ifname_wds)
{
	int i;
	struct hostapd_ssid *ssid = &hapd->conf->ssid;

	if (hapd->conf->ieee802_1x || hapd->conf->wpa)
		return;

	for (i = 0; i < 4; i++) {
		if (ssid->wep.key[i] &&
		    hostapd_drv_set_key(ifname_wds, hapd, WPA_ALG_WEP, NULL, i,
					i == ssid->wep.idx, NULL, 0,
					ssid->wep.key[i], ssid->wep.len[i])) {
			wpa_printf(MSG_WARNING,
				   "Could not set WEP keys for WDS interface; %s",
				   ifname_wds);
			break;
		}
	}
}


static void handle_assoc_cb(struct hostapd_data *hapd,
			    const struct ieee80211_mgmt *mgmt,
			    size_t len, int reassoc, int ok)
{
	u16 status;
	struct sta_info *sta;
	int new_assoc = 1;

	sta = ap_get_sta(hapd, mgmt->da);
	if (!sta) {
		wpa_printf(MSG_INFO, "handle_assoc_cb: STA " MACSTR " not found",
			   MAC2STR(mgmt->da));
		return;
	}

	if (len < IEEE80211_HDRLEN + (reassoc ? sizeof(mgmt->u.reassoc_resp) :
				      sizeof(mgmt->u.assoc_resp))) {
		wpa_printf(MSG_INFO,
			   "handle_assoc_cb(reassoc=%d) - too short payload (len=%lu)",
			   reassoc, (unsigned long) len);
		hostapd_drv_sta_remove(hapd, sta->addr);
		return;
	}

	if (reassoc)
		status = le_to_host16(mgmt->u.reassoc_resp.status_code);
	else
		status = le_to_host16(mgmt->u.assoc_resp.status_code);

	if (!ok) {
		hostapd_logger(hapd, mgmt->da, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "did not acknowledge association response");
		sta->flags &= ~WLAN_STA_ASSOC_REQ_OK;
		/* The STA is added only in case of SUCCESS */
		if (status == WLAN_STATUS_SUCCESS)
			hostapd_drv_sta_remove(hapd, sta->addr);

		return;
	}

	if (status != WLAN_STATUS_SUCCESS)
		return;

	/* Stop previous accounting session, if one is started, and allocate
	 * new session id for the new session. */
	accounting_sta_stop(hapd, sta);

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_IEEE80211,
		       HOSTAPD_LEVEL_INFO,
		       "associated (aid %d)",
		       sta->aid);

	if (sta->flags & WLAN_STA_ASSOC)
		new_assoc = 0;
	sta->flags |= WLAN_STA_ASSOC;
	sta->flags &= ~WLAN_STA_WNM_SLEEP_MODE;
	if ((!hapd->conf->ieee802_1x && !hapd->conf->wpa &&
	     !hapd->conf->osen) ||
	    sta->auth_alg == WLAN_AUTH_FILS_SK ||
	    sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	    sta->auth_alg == WLAN_AUTH_FILS_PK ||
	    sta->auth_alg == WLAN_AUTH_FT) {
		/*
		 * Open, static WEP, FT protocol, or FILS; no separate
		 * authorization step.
		 */
		ap_sta_set_authorized(hapd, sta, 1);
	}

	if (reassoc)
		mlme_reassociate_indication(hapd, sta);
	else
		mlme_associate_indication(hapd, sta);

#ifdef CONFIG_IEEE80211W
	sta->sa_query_timed_out = 0;
#endif /* CONFIG_IEEE80211W */

	if (sta->eapol_sm == NULL) {
		/*
		 * This STA does not use RADIUS server for EAP authentication,
		 * so bind it to the selected VLAN interface now, since the
		 * interface selection is not going to change anymore.
		 */
		if (ap_sta_bind_vlan(hapd, sta) < 0)
			return;
	} else if (sta->vlan_id) {
		/* VLAN ID already set (e.g., by PMKSA caching), so bind STA */
		if (ap_sta_bind_vlan(hapd, sta) < 0)
			return;
	}

	hostapd_set_sta_flags(hapd, sta);

	if (!(sta->flags & WLAN_STA_WDS) && sta->pending_wds_enable) {
		wpa_printf(MSG_DEBUG, "Enable 4-address WDS mode for STA "
			   MACSTR " based on pending request",
			   MAC2STR(sta->addr));
		sta->pending_wds_enable = 0;
		sta->flags |= WLAN_STA_WDS;
	}

	if (sta->flags & WLAN_STA_WDS) {
		int ret;
		char ifname_wds[IFNAMSIZ + 1];

		wpa_printf(MSG_DEBUG, "Reenable 4-address WDS mode for STA "
			   MACSTR " (aid %u)",
			   MAC2STR(sta->addr), sta->aid);
		ret = hostapd_set_wds_sta(hapd, ifname_wds, sta->addr,
					  sta->aid, 1);
		if (!ret)
			hostapd_set_wds_encryption(hapd, sta, ifname_wds);
	}

	if (sta->auth_alg == WLAN_AUTH_FT)
		wpa_auth_sm_event(sta->wpa_sm, WPA_ASSOC_FT);
	else
		wpa_auth_sm_event(sta->wpa_sm, WPA_ASSOC);
	hapd->new_assoc_sta_cb(hapd, sta, !new_assoc);
	ieee802_1x_notify_port_enabled(sta->eapol_sm, 1);

#ifdef CONFIG_FILS
	if ((sta->auth_alg == WLAN_AUTH_FILS_SK ||
	     sta->auth_alg == WLAN_AUTH_FILS_SK_PFS ||
	     sta->auth_alg == WLAN_AUTH_FILS_PK) &&
	    fils_set_tk(sta->wpa_sm) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: TK configuration failed");
		ap_sta_disconnect(hapd, sta, sta->addr,
				  WLAN_REASON_UNSPECIFIED);
		return;
	}
#endif /* CONFIG_FILS */

	if (sta->pending_eapol_rx) {
		struct os_reltime now, age;

		os_get_reltime(&now);
		os_reltime_sub(&now, &sta->pending_eapol_rx->rx_time, &age);
		if (age.sec == 0 && age.usec < 200000) {
			wpa_printf(MSG_DEBUG,
				   "Process pending EAPOL frame that was received from " MACSTR " just before association notification",
				   MAC2STR(sta->addr));
			ieee802_1x_receive(
				hapd, mgmt->da,
				wpabuf_head(sta->pending_eapol_rx->buf),
				wpabuf_len(sta->pending_eapol_rx->buf));
		}
		wpabuf_free(sta->pending_eapol_rx->buf);
		os_free(sta->pending_eapol_rx);
		sta->pending_eapol_rx = NULL;
	}
}


static void handle_deauth_cb(struct hostapd_data *hapd,
			     const struct ieee80211_mgmt *mgmt,
			     size_t len, int ok)
{
	struct sta_info *sta;
	if (is_multicast_ether_addr(mgmt->da))
		return;
	sta = ap_get_sta(hapd, mgmt->da);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "handle_deauth_cb: STA " MACSTR
			   " not found", MAC2STR(mgmt->da));
		return;
	}
	if (ok)
		wpa_printf(MSG_DEBUG, "STA " MACSTR " acknowledged deauth",
			   MAC2STR(sta->addr));
	else
		wpa_printf(MSG_DEBUG, "STA " MACSTR " did not acknowledge "
			   "deauth", MAC2STR(sta->addr));

	ap_sta_deauth_cb(hapd, sta);
}


static void handle_disassoc_cb(struct hostapd_data *hapd,
			       const struct ieee80211_mgmt *mgmt,
			       size_t len, int ok)
{
	struct sta_info *sta;
	if (is_multicast_ether_addr(mgmt->da))
		return;
	sta = ap_get_sta(hapd, mgmt->da);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "handle_disassoc_cb: STA " MACSTR
			   " not found", MAC2STR(mgmt->da));
		return;
	}
	if (ok)
		wpa_printf(MSG_DEBUG, "STA " MACSTR " acknowledged disassoc",
			   MAC2STR(sta->addr));
	else
		wpa_printf(MSG_DEBUG, "STA " MACSTR " did not acknowledge "
			   "disassoc", MAC2STR(sta->addr));

	ap_sta_disassoc_cb(hapd, sta);
}


static void handle_action_cb(struct hostapd_data *hapd,
			     const struct ieee80211_mgmt *mgmt,
			     size_t len, int ok)
{
	struct sta_info *sta;
	const struct rrm_measurement_report_element *report;

	if (is_multicast_ether_addr(mgmt->da))
		return;
#ifdef CONFIG_DPP
	if (len >= IEEE80211_HDRLEN + 6 &&
	    mgmt->u.action.category == WLAN_ACTION_PUBLIC &&
	    mgmt->u.action.u.vs_public_action.action ==
	    WLAN_PA_VENDOR_SPECIFIC &&
	    WPA_GET_BE24(mgmt->u.action.u.vs_public_action.oui) ==
	    OUI_WFA &&
	    mgmt->u.action.u.vs_public_action.variable[0] ==
	    DPP_OUI_TYPE) {
		const u8 *pos, *end;

		pos = &mgmt->u.action.u.vs_public_action.variable[1];
		end = ((const u8 *) mgmt) + len;
		hostapd_dpp_tx_status(hapd, mgmt->da, pos, end - pos, ok);
		return;
	}
	if (len >= IEEE80211_HDRLEN + 2 &&
	    mgmt->u.action.category == WLAN_ACTION_PUBLIC &&
	    (mgmt->u.action.u.public_action.action ==
	     WLAN_PA_GAS_INITIAL_REQ ||
	     mgmt->u.action.u.public_action.action ==
	     WLAN_PA_GAS_COMEBACK_REQ)) {
		const u8 *pos, *end;

		pos = mgmt->u.action.u.public_action.variable;
		end = ((const u8 *) mgmt) + len;
		gas_query_ap_tx_status(hapd->gas, mgmt->da, pos, end - pos, ok);
		return;
	}
#endif /* CONFIG_DPP */
	sta = ap_get_sta(hapd, mgmt->da);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "handle_action_cb: STA " MACSTR
			   " not found", MAC2STR(mgmt->da));
		return;
	}

	if (len < 24 + 5 + sizeof(*report))
		return;
	report = (const struct rrm_measurement_report_element *)
		&mgmt->u.action.u.rrm.variable[2];
	if (mgmt->u.action.category == WLAN_ACTION_RADIO_MEASUREMENT &&
	    mgmt->u.action.u.rrm.action == WLAN_RRM_RADIO_MEASUREMENT_REQUEST &&
	    report->eid == WLAN_EID_MEASURE_REQUEST &&
	    report->len >= 3 &&
	    report->type == MEASURE_TYPE_BEACON)
		hostapd_rrm_beacon_req_tx_status(hapd, mgmt, len, ok);
}


/**
 * ieee802_11_mgmt_cb - Process management frame TX status callback
 * @hapd: hostapd BSS data structure (the BSS from which the management frame
 * was sent from)
 * @buf: management frame data (starting from IEEE 802.11 header)
 * @len: length of frame data in octets
 * @stype: management frame subtype from frame control field
 * @ok: Whether the frame was ACK'ed
 */
void ieee802_11_mgmt_cb(struct hostapd_data *hapd, const u8 *buf, size_t len,
			u16 stype, int ok)
{
	const struct ieee80211_mgmt *mgmt;
	mgmt = (const struct ieee80211_mgmt *) buf;

#ifdef CONFIG_TESTING_OPTIONS
	if (hapd->ext_mgmt_frame_handling) {
		size_t hex_len = 2 * len + 1;
		char *hex = os_malloc(hex_len);

		if (hex) {
			wpa_snprintf_hex(hex, hex_len, buf, len);
			wpa_msg(hapd->msg_ctx, MSG_INFO,
				"MGMT-TX-STATUS stype=%u ok=%d buf=%s",
				stype, ok, hex);
			os_free(hex);
		}
		return;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	switch (stype) {
	case WLAN_FC_STYPE_AUTH:
		wpa_printf(MSG_DEBUG, "mgmt::auth cb");
		handle_auth_cb(hapd, mgmt, len, ok);
		break;
	case WLAN_FC_STYPE_ASSOC_RESP:
		wpa_printf(MSG_DEBUG, "mgmt::assoc_resp cb");
		handle_assoc_cb(hapd, mgmt, len, 0, ok);
		break;
	case WLAN_FC_STYPE_REASSOC_RESP:
		wpa_printf(MSG_DEBUG, "mgmt::reassoc_resp cb");
		handle_assoc_cb(hapd, mgmt, len, 1, ok);
		break;
	case WLAN_FC_STYPE_PROBE_RESP:
		wpa_printf(MSG_EXCESSIVE, "mgmt::proberesp cb ok=%d", ok);
		break;
	case WLAN_FC_STYPE_DEAUTH:
		wpa_printf(MSG_DEBUG, "mgmt::deauth cb");
		handle_deauth_cb(hapd, mgmt, len, ok);
		break;
	case WLAN_FC_STYPE_DISASSOC:
		wpa_printf(MSG_DEBUG, "mgmt::disassoc cb");
		handle_disassoc_cb(hapd, mgmt, len, ok);
		break;
	case WLAN_FC_STYPE_ACTION:
		wpa_printf(MSG_DEBUG, "mgmt::action cb ok=%d", ok);
		handle_action_cb(hapd, mgmt, len, ok);
		break;
	default:
		wpa_printf(MSG_INFO, "unknown mgmt cb frame subtype %d", stype);
		break;
	}
}


int ieee802_11_get_mib(struct hostapd_data *hapd, char *buf, size_t buflen)
{
	/* TODO */
	return 0;
}


int ieee802_11_get_mib_sta(struct hostapd_data *hapd, struct sta_info *sta,
			   char *buf, size_t buflen)
{
	/* TODO */
	return 0;
}


void hostapd_tx_status(struct hostapd_data *hapd, const u8 *addr,
		       const u8 *buf, size_t len, int ack)
{
	struct sta_info *sta;
	struct hostapd_iface *iface = hapd->iface;

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL && iface->num_bss > 1) {
		size_t j;
		for (j = 0; j < iface->num_bss; j++) {
			hapd = iface->bss[j];
			sta = ap_get_sta(hapd, addr);
			if (sta)
				break;
		}
	}
	if (sta == NULL || !(sta->flags & WLAN_STA_ASSOC))
		return;
	if (sta->flags & WLAN_STA_PENDING_POLL) {
		wpa_printf(MSG_DEBUG, "STA " MACSTR " %s pending "
			   "activity poll", MAC2STR(sta->addr),
			   ack ? "ACKed" : "did not ACK");
		if (ack)
			sta->flags &= ~WLAN_STA_PENDING_POLL;
	}

	ieee802_1x_tx_status(hapd, sta, buf, len, ack);
}


void hostapd_eapol_tx_status(struct hostapd_data *hapd, const u8 *dst,
			     const u8 *data, size_t len, int ack)
{
	struct sta_info *sta;
	struct hostapd_iface *iface = hapd->iface;

	sta = ap_get_sta(hapd, dst);
	if (sta == NULL && iface->num_bss > 1) {
		size_t j;
		for (j = 0; j < iface->num_bss; j++) {
			hapd = iface->bss[j];
			sta = ap_get_sta(hapd, dst);
			if (sta)
				break;
		}
	}
	if (sta == NULL || !(sta->flags & WLAN_STA_ASSOC)) {
		wpa_printf(MSG_DEBUG, "Ignore TX status for Data frame to STA "
			   MACSTR " that is not currently associated",
			   MAC2STR(dst));
		return;
	}

	ieee802_1x_eapol_tx_status(hapd, sta, data, len, ack);
}


void hostapd_client_poll_ok(struct hostapd_data *hapd, const u8 *addr)
{
	struct sta_info *sta;
	struct hostapd_iface *iface = hapd->iface;

	sta = ap_get_sta(hapd, addr);
	if (sta == NULL && iface->num_bss > 1) {
		size_t j;
		for (j = 0; j < iface->num_bss; j++) {
			hapd = iface->bss[j];
			sta = ap_get_sta(hapd, addr);
			if (sta)
				break;
		}
	}
	if (sta == NULL)
		return;
	wpa_msg(hapd->msg_ctx, MSG_INFO, AP_STA_POLL_OK MACSTR,
		MAC2STR(sta->addr));
	if (!(sta->flags & WLAN_STA_PENDING_POLL))
		return;

	wpa_printf(MSG_DEBUG, "STA " MACSTR " ACKed pending "
		   "activity poll", MAC2STR(sta->addr));
	sta->flags &= ~WLAN_STA_PENDING_POLL;
}


void ieee802_11_rx_from_unknown(struct hostapd_data *hapd, const u8 *src,
				int wds)
{
	struct sta_info *sta;

	sta = ap_get_sta(hapd, src);
	if (sta &&
	    ((sta->flags & WLAN_STA_ASSOC) ||
	     ((sta->flags & WLAN_STA_ASSOC_REQ_OK) && wds))) {
		if (!hapd->conf->wds_sta)
			return;

		if ((sta->flags & (WLAN_STA_ASSOC | WLAN_STA_ASSOC_REQ_OK)) ==
		    WLAN_STA_ASSOC_REQ_OK) {
			wpa_printf(MSG_DEBUG,
				   "Postpone 4-address WDS mode enabling for STA "
				   MACSTR " since TX status for AssocResp is not yet known",
				   MAC2STR(sta->addr));
			sta->pending_wds_enable = 1;
			return;
		}

		if (wds && !(sta->flags & WLAN_STA_WDS)) {
			int ret;
			char ifname_wds[IFNAMSIZ + 1];

			wpa_printf(MSG_DEBUG, "Enable 4-address WDS mode for "
				   "STA " MACSTR " (aid %u)",
				   MAC2STR(sta->addr), sta->aid);
			sta->flags |= WLAN_STA_WDS;
			ret = hostapd_set_wds_sta(hapd, ifname_wds,
						  sta->addr, sta->aid, 1);
			if (!ret)
				hostapd_set_wds_encryption(hapd, sta,
							   ifname_wds);
		}
		return;
	}

	wpa_printf(MSG_DEBUG, "Data/PS-poll frame from not associated STA "
		   MACSTR, MAC2STR(src));
	if (is_multicast_ether_addr(src)) {
		/* Broadcast bit set in SA?! Ignore the frame silently. */
		return;
	}

	if (sta && (sta->flags & WLAN_STA_ASSOC_REQ_OK)) {
		wpa_printf(MSG_DEBUG, "Association Response to the STA has "
			   "already been sent, but no TX status yet known - "
			   "ignore Class 3 frame issue with " MACSTR,
			   MAC2STR(src));
		return;
	}

	if (sta && (sta->flags & WLAN_STA_AUTH))
		hostapd_drv_sta_disassoc(
			hapd, src,
			WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
	else
		hostapd_drv_sta_deauth(
			hapd, src,
			WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
}


#endif /* CONFIG_NATIVE_WINDOWS */
