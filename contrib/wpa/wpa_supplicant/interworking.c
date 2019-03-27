/*
 * Interworking (IEEE 802.11u)
 * Copyright (c) 2011-2013, Qualcomm Atheros, Inc.
 * Copyright (c) 2011-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/gas.h"
#include "common/wpa_ctrl.h"
#include "utils/pcsc_funcs.h"
#include "utils/eloop.h"
#include "drivers/driver.h"
#include "eap_common/eap_defs.h"
#include "eap_peer/eap.h"
#include "eap_peer/eap_methods.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "rsn_supp/wpa.h"
#include "wpa_supplicant_i.h"
#include "config.h"
#include "config_ssid.h"
#include "bss.h"
#include "scan.h"
#include "notify.h"
#include "driver_i.h"
#include "gas_query.h"
#include "hs20_supplicant.h"
#include "interworking.h"


#if defined(EAP_SIM) | defined(EAP_SIM_DYNAMIC)
#define INTERWORKING_3GPP
#else
#if defined(EAP_AKA) | defined(EAP_AKA_DYNAMIC)
#define INTERWORKING_3GPP
#else
#if defined(EAP_AKA_PRIME) | defined(EAP_AKA_PRIME_DYNAMIC)
#define INTERWORKING_3GPP
#endif
#endif
#endif

static void interworking_next_anqp_fetch(struct wpa_supplicant *wpa_s);
static struct wpa_cred * interworking_credentials_available_realm(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded);
static struct wpa_cred * interworking_credentials_available_3gpp(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded);


static int cred_prio_cmp(const struct wpa_cred *a, const struct wpa_cred *b)
{
	if (a->priority > b->priority)
		return 1;
	if (a->priority < b->priority)
		return -1;
	if (a->provisioning_sp == NULL || b->provisioning_sp == NULL ||
	    os_strcmp(a->provisioning_sp, b->provisioning_sp) != 0)
		return 0;
	if (a->sp_priority < b->sp_priority)
		return 1;
	if (a->sp_priority > b->sp_priority)
		return -1;
	return 0;
}


static void interworking_reconnect(struct wpa_supplicant *wpa_s)
{
	unsigned int tried;

	if (wpa_s->wpa_state >= WPA_AUTHENTICATING) {
		wpa_supplicant_cancel_sched_scan(wpa_s);
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
	}
	wpa_s->disconnected = 0;
	wpa_s->reassociate = 1;
	tried = wpa_s->interworking_fast_assoc_tried;
	wpa_s->interworking_fast_assoc_tried = 1;

	if (!tried && wpa_supplicant_fast_associate(wpa_s) >= 0)
		return;

	wpa_s->interworking_fast_assoc_tried = 0;
	wpa_supplicant_req_scan(wpa_s, 0, 0);
}


static struct wpabuf * anqp_build_req(u16 info_ids[], size_t num_ids,
				      struct wpabuf *extra)
{
	struct wpabuf *buf;
	size_t i;
	u8 *len_pos;

	buf = gas_anqp_build_initial_req(0, 4 + num_ids * 2 +
					 (extra ? wpabuf_len(extra) : 0));
	if (buf == NULL)
		return NULL;

	if (num_ids > 0) {
		len_pos = gas_anqp_add_element(buf, ANQP_QUERY_LIST);
		for (i = 0; i < num_ids; i++)
			wpabuf_put_le16(buf, info_ids[i]);
		gas_anqp_set_element_len(buf, len_pos);
	}
	if (extra)
		wpabuf_put_buf(buf, extra);

	gas_anqp_set_len(buf);

	return buf;
}


static void interworking_anqp_resp_cb(void *ctx, const u8 *dst,
				      u8 dialog_token,
				      enum gas_query_result result,
				      const struct wpabuf *adv_proto,
				      const struct wpabuf *resp,
				      u16 status_code)
{
	struct wpa_supplicant *wpa_s = ctx;

	wpa_printf(MSG_DEBUG, "ANQP: Response callback dst=" MACSTR
		   " dialog_token=%u result=%d status_code=%u",
		   MAC2STR(dst), dialog_token, result, status_code);
	anqp_resp_cb(wpa_s, dst, dialog_token, result, adv_proto, resp,
		     status_code);
	interworking_next_anqp_fetch(wpa_s);
}


static int cred_with_roaming_consortium(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->roaming_consortium_len)
			return 1;
		if (cred->required_roaming_consortium_len)
			return 1;
		if (cred->num_roaming_consortiums)
			return 1;
	}
	return 0;
}


static int cred_with_3gpp(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->pcsc || cred->imsi)
			return 1;
	}
	return 0;
}


static int cred_with_nai_realm(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->pcsc || cred->imsi)
			continue;
		if (!cred->eap_method)
			return 1;
		if (cred->realm && cred->roaming_consortium_len == 0)
			return 1;
	}
	return 0;
}


static int cred_with_domain(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->domain || cred->pcsc || cred->imsi ||
		    cred->roaming_partner)
			return 1;
	}
	return 0;
}


#ifdef CONFIG_HS20

static int cred_with_min_backhaul(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->min_dl_bandwidth_home ||
		    cred->min_ul_bandwidth_home ||
		    cred->min_dl_bandwidth_roaming ||
		    cred->min_ul_bandwidth_roaming)
			return 1;
	}
	return 0;
}


static int cred_with_conn_capab(struct wpa_supplicant *wpa_s)
{
	struct wpa_cred *cred;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->num_req_conn_capab)
			return 1;
	}
	return 0;
}

#endif /* CONFIG_HS20 */


static int additional_roaming_consortiums(struct wpa_bss *bss)
{
	const u8 *ie;
	ie = wpa_bss_get_ie(bss, WLAN_EID_ROAMING_CONSORTIUM);
	if (ie == NULL || ie[1] == 0)
		return 0;
	return ie[2]; /* Number of ANQP OIs */
}


static void interworking_continue_anqp(void *eloop_ctx, void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	interworking_next_anqp_fetch(wpa_s);
}


static int interworking_anqp_send_req(struct wpa_supplicant *wpa_s,
				      struct wpa_bss *bss)
{
	struct wpabuf *buf;
	int ret = 0;
	int res;
	u16 info_ids[8];
	size_t num_info_ids = 0;
	struct wpabuf *extra = NULL;
	int all = wpa_s->fetch_all_anqp;

	wpa_msg(wpa_s, MSG_DEBUG, "Interworking: ANQP Query Request to " MACSTR,
		MAC2STR(bss->bssid));
	wpa_s->interworking_gas_bss = bss;

	info_ids[num_info_ids++] = ANQP_CAPABILITY_LIST;
	if (all) {
		info_ids[num_info_ids++] = ANQP_VENUE_NAME;
		info_ids[num_info_ids++] = ANQP_NETWORK_AUTH_TYPE;
	}
	if (all || (cred_with_roaming_consortium(wpa_s) &&
		    additional_roaming_consortiums(bss)))
		info_ids[num_info_ids++] = ANQP_ROAMING_CONSORTIUM;
	if (all)
		info_ids[num_info_ids++] = ANQP_IP_ADDR_TYPE_AVAILABILITY;
	if (all || cred_with_nai_realm(wpa_s))
		info_ids[num_info_ids++] = ANQP_NAI_REALM;
	if (all || cred_with_3gpp(wpa_s)) {
		info_ids[num_info_ids++] = ANQP_3GPP_CELLULAR_NETWORK;
		wpa_supplicant_scard_init(wpa_s, NULL);
	}
	if (all || cred_with_domain(wpa_s))
		info_ids[num_info_ids++] = ANQP_DOMAIN_NAME;
	wpa_hexdump(MSG_DEBUG, "Interworking: ANQP Query info",
		    (u8 *) info_ids, num_info_ids * 2);

#ifdef CONFIG_HS20
	if (wpa_bss_get_vendor_ie(bss, HS20_IE_VENDOR_TYPE)) {
		u8 *len_pos;

		extra = wpabuf_alloc(100);
		if (!extra)
			return -1;

		len_pos = gas_anqp_add_element(extra, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(extra, OUI_WFA);
		wpabuf_put_u8(extra, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(extra, HS20_STYPE_QUERY_LIST);
		wpabuf_put_u8(extra, 0); /* Reserved */
		wpabuf_put_u8(extra, HS20_STYPE_CAPABILITY_LIST);
		if (all)
			wpabuf_put_u8(extra,
				      HS20_STYPE_OPERATOR_FRIENDLY_NAME);
		if (all || cred_with_min_backhaul(wpa_s))
			wpabuf_put_u8(extra, HS20_STYPE_WAN_METRICS);
		if (all || cred_with_conn_capab(wpa_s))
			wpabuf_put_u8(extra, HS20_STYPE_CONNECTION_CAPABILITY);
		if (all)
			wpabuf_put_u8(extra, HS20_STYPE_OPERATING_CLASS);
		if (all) {
			wpabuf_put_u8(extra, HS20_STYPE_OSU_PROVIDERS_LIST);
			wpabuf_put_u8(extra, HS20_STYPE_OSU_PROVIDERS_NAI_LIST);
		}
		gas_anqp_set_element_len(extra, len_pos);
	}
#endif /* CONFIG_HS20 */

	buf = anqp_build_req(info_ids, num_info_ids, extra);
	wpabuf_free(extra);
	if (buf == NULL)
		return -1;

	res = gas_query_req(wpa_s->gas, bss->bssid, bss->freq, 0, buf,
			    interworking_anqp_resp_cb, wpa_s);
	if (res < 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "ANQP: Failed to send Query Request");
		wpabuf_free(buf);
		ret = -1;
		eloop_register_timeout(0, 0, interworking_continue_anqp, wpa_s,
				       NULL);
	} else
		wpa_msg(wpa_s, MSG_DEBUG,
			"ANQP: Query started with dialog token %u", res);

	return ret;
}


struct nai_realm_eap {
	u8 method;
	u8 inner_method;
	enum nai_realm_eap_auth_inner_non_eap inner_non_eap;
	u8 cred_type;
	u8 tunneled_cred_type;
};

struct nai_realm {
	u8 encoding;
	char *realm;
	u8 eap_count;
	struct nai_realm_eap *eap;
};


static void nai_realm_free(struct nai_realm *realms, u16 count)
{
	u16 i;

	if (realms == NULL)
		return;
	for (i = 0; i < count; i++) {
		os_free(realms[i].eap);
		os_free(realms[i].realm);
	}
	os_free(realms);
}


static const u8 * nai_realm_parse_eap(struct nai_realm_eap *e, const u8 *pos,
				      const u8 *end)
{
	u8 elen, auth_count, a;
	const u8 *e_end;

	if (end - pos < 3) {
		wpa_printf(MSG_DEBUG, "No room for EAP Method fixed fields");
		return NULL;
	}

	elen = *pos++;
	if (elen > end - pos || elen < 2) {
		wpa_printf(MSG_DEBUG, "No room for EAP Method subfield");
		return NULL;
	}
	e_end = pos + elen;
	e->method = *pos++;
	auth_count = *pos++;
	wpa_printf(MSG_DEBUG, "EAP Method: len=%u method=%u auth_count=%u",
		   elen, e->method, auth_count);

	for (a = 0; a < auth_count; a++) {
		u8 id, len;

		if (end - pos < 2) {
			wpa_printf(MSG_DEBUG,
				   "No room for Authentication Parameter subfield header");
			return NULL;
		}

		id = *pos++;
		len = *pos++;
		if (len > end - pos) {
			wpa_printf(MSG_DEBUG,
				   "No room for Authentication Parameter subfield");
			return NULL;
		}

		switch (id) {
		case NAI_REALM_EAP_AUTH_NON_EAP_INNER_AUTH:
			if (len < 1)
				break;
			e->inner_non_eap = *pos;
			if (e->method != EAP_TYPE_TTLS)
				break;
			switch (*pos) {
			case NAI_REALM_INNER_NON_EAP_PAP:
				wpa_printf(MSG_DEBUG, "EAP-TTLS/PAP");
				break;
			case NAI_REALM_INNER_NON_EAP_CHAP:
				wpa_printf(MSG_DEBUG, "EAP-TTLS/CHAP");
				break;
			case NAI_REALM_INNER_NON_EAP_MSCHAP:
				wpa_printf(MSG_DEBUG, "EAP-TTLS/MSCHAP");
				break;
			case NAI_REALM_INNER_NON_EAP_MSCHAPV2:
				wpa_printf(MSG_DEBUG, "EAP-TTLS/MSCHAPV2");
				break;
			}
			break;
		case NAI_REALM_EAP_AUTH_INNER_AUTH_EAP_METHOD:
			if (len < 1)
				break;
			e->inner_method = *pos;
			wpa_printf(MSG_DEBUG, "Inner EAP method: %u",
				   e->inner_method);
			break;
		case NAI_REALM_EAP_AUTH_CRED_TYPE:
			if (len < 1)
				break;
			e->cred_type = *pos;
			wpa_printf(MSG_DEBUG, "Credential Type: %u",
				   e->cred_type);
			break;
		case NAI_REALM_EAP_AUTH_TUNNELED_CRED_TYPE:
			if (len < 1)
				break;
			e->tunneled_cred_type = *pos;
			wpa_printf(MSG_DEBUG, "Tunneled EAP Method Credential "
				   "Type: %u", e->tunneled_cred_type);
			break;
		default:
			wpa_printf(MSG_DEBUG, "Unsupported Authentication "
				   "Parameter: id=%u len=%u", id, len);
			wpa_hexdump(MSG_DEBUG, "Authentication Parameter "
				    "Value", pos, len);
			break;
		}

		pos += len;
	}

	return e_end;
}


static const u8 * nai_realm_parse_realm(struct nai_realm *r, const u8 *pos,
					const u8 *end)
{
	u16 len;
	const u8 *f_end;
	u8 realm_len, e;

	if (end - pos < 4) {
		wpa_printf(MSG_DEBUG, "No room for NAI Realm Data "
			   "fixed fields");
		return NULL;
	}

	len = WPA_GET_LE16(pos); /* NAI Realm Data field Length */
	pos += 2;
	if (len > end - pos || len < 3) {
		wpa_printf(MSG_DEBUG, "No room for NAI Realm Data "
			   "(len=%u; left=%u)",
			   len, (unsigned int) (end - pos));
		return NULL;
	}
	f_end = pos + len;

	r->encoding = *pos++;
	realm_len = *pos++;
	if (realm_len > f_end - pos) {
		wpa_printf(MSG_DEBUG, "No room for NAI Realm "
			   "(len=%u; left=%u)",
			   realm_len, (unsigned int) (f_end - pos));
		return NULL;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "NAI Realm", pos, realm_len);
	r->realm = dup_binstr(pos, realm_len);
	if (r->realm == NULL)
		return NULL;
	pos += realm_len;

	if (f_end - pos < 1) {
		wpa_printf(MSG_DEBUG, "No room for EAP Method Count");
		return NULL;
	}
	r->eap_count = *pos++;
	wpa_printf(MSG_DEBUG, "EAP Count: %u", r->eap_count);
	if (r->eap_count * 3 > f_end - pos) {
		wpa_printf(MSG_DEBUG, "No room for EAP Methods");
		return NULL;
	}
	r->eap = os_calloc(r->eap_count, sizeof(struct nai_realm_eap));
	if (r->eap == NULL)
		return NULL;

	for (e = 0; e < r->eap_count; e++) {
		pos = nai_realm_parse_eap(&r->eap[e], pos, f_end);
		if (pos == NULL)
			return NULL;
	}

	return f_end;
}


static struct nai_realm * nai_realm_parse(struct wpabuf *anqp, u16 *count)
{
	struct nai_realm *realm;
	const u8 *pos, *end;
	u16 i, num;
	size_t left;

	if (anqp == NULL)
		return NULL;
	left = wpabuf_len(anqp);
	if (left < 2)
		return NULL;

	pos = wpabuf_head_u8(anqp);
	end = pos + left;
	num = WPA_GET_LE16(pos);
	wpa_printf(MSG_DEBUG, "NAI Realm Count: %u", num);
	pos += 2;
	left -= 2;

	if (num > left / 5) {
		wpa_printf(MSG_DEBUG, "Invalid NAI Realm Count %u - not "
			   "enough data (%u octets) for that many realms",
			   num, (unsigned int) left);
		return NULL;
	}

	realm = os_calloc(num, sizeof(struct nai_realm));
	if (realm == NULL)
		return NULL;

	for (i = 0; i < num; i++) {
		pos = nai_realm_parse_realm(&realm[i], pos, end);
		if (pos == NULL) {
			nai_realm_free(realm, num);
			return NULL;
		}
	}

	*count = num;
	return realm;
}


static int nai_realm_match(struct nai_realm *realm, const char *home_realm)
{
	char *tmp, *pos, *end;
	int match = 0;

	if (realm->realm == NULL || home_realm == NULL)
		return 0;

	if (os_strchr(realm->realm, ';') == NULL)
		return os_strcasecmp(realm->realm, home_realm) == 0;

	tmp = os_strdup(realm->realm);
	if (tmp == NULL)
		return 0;

	pos = tmp;
	while (*pos) {
		end = os_strchr(pos, ';');
		if (end)
			*end = '\0';
		if (os_strcasecmp(pos, home_realm) == 0) {
			match = 1;
			break;
		}
		if (end == NULL)
			break;
		pos = end + 1;
	}

	os_free(tmp);

	return match;
}


static int nai_realm_cred_username(struct wpa_supplicant *wpa_s,
				   struct nai_realm_eap *eap)
{
	if (eap_get_name(EAP_VENDOR_IETF, eap->method) == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-cred-username: EAP method not supported: %d",
			eap->method);
		return 0; /* method not supported */
	}

	if (eap->method != EAP_TYPE_TTLS && eap->method != EAP_TYPE_PEAP &&
	    eap->method != EAP_TYPE_FAST) {
		/* Only tunneled methods with username/password supported */
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-cred-username: Method: %d is not TTLS, PEAP, or FAST",
			eap->method);
		return 0;
	}

	if (eap->method == EAP_TYPE_PEAP || eap->method == EAP_TYPE_FAST) {
		if (eap->inner_method &&
		    eap_get_name(EAP_VENDOR_IETF, eap->inner_method) == NULL) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"nai-realm-cred-username: PEAP/FAST: Inner method not supported: %d",
				eap->inner_method);
			return 0;
		}
		if (!eap->inner_method &&
		    eap_get_name(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2) == NULL) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"nai-realm-cred-username: MSCHAPv2 not supported");
			return 0;
		}
	}

	if (eap->method == EAP_TYPE_TTLS) {
		if (eap->inner_method == 0 && eap->inner_non_eap == 0)
			return 1; /* Assume TTLS/MSCHAPv2 is used */
		if (eap->inner_method &&
		    eap_get_name(EAP_VENDOR_IETF, eap->inner_method) == NULL) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"nai-realm-cred-username: TTLS, but inner not supported: %d",
				eap->inner_method);
			return 0;
		}
		if (eap->inner_non_eap &&
		    eap->inner_non_eap != NAI_REALM_INNER_NON_EAP_PAP &&
		    eap->inner_non_eap != NAI_REALM_INNER_NON_EAP_CHAP &&
		    eap->inner_non_eap != NAI_REALM_INNER_NON_EAP_MSCHAP &&
		    eap->inner_non_eap != NAI_REALM_INNER_NON_EAP_MSCHAPV2) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"nai-realm-cred-username: TTLS, inner-non-eap not supported: %d",
				eap->inner_non_eap);
			return 0;
		}
	}

	if (eap->inner_method &&
	    eap->inner_method != EAP_TYPE_GTC &&
	    eap->inner_method != EAP_TYPE_MSCHAPV2) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-cred-username: inner-method not GTC or MSCHAPv2: %d",
			eap->inner_method);
		return 0;
	}

	return 1;
}


static int nai_realm_cred_cert(struct wpa_supplicant *wpa_s,
			       struct nai_realm_eap *eap)
{
	if (eap_get_name(EAP_VENDOR_IETF, eap->method) == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-cred-cert: Method not supported: %d",
			eap->method);
		return 0; /* method not supported */
	}

	if (eap->method != EAP_TYPE_TLS) {
		/* Only EAP-TLS supported for credential authentication */
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-cred-cert: Method not TLS: %d",
			eap->method);
		return 0;
	}

	return 1;
}


static struct nai_realm_eap * nai_realm_find_eap(struct wpa_supplicant *wpa_s,
						 struct wpa_cred *cred,
						 struct nai_realm *realm)
{
	u8 e;

	if (cred->username == NULL ||
	    cred->username[0] == '\0' ||
	    ((cred->password == NULL ||
	      cred->password[0] == '\0') &&
	     (cred->private_key == NULL ||
	      cred->private_key[0] == '\0'))) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"nai-realm-find-eap: incomplete cred info: username: %s  password: %s private_key: %s",
			cred->username ? cred->username : "NULL",
			cred->password ? cred->password : "NULL",
			cred->private_key ? cred->private_key : "NULL");
		return NULL;
	}

	for (e = 0; e < realm->eap_count; e++) {
		struct nai_realm_eap *eap = &realm->eap[e];
		if (cred->password && cred->password[0] &&
		    nai_realm_cred_username(wpa_s, eap))
			return eap;
		if (cred->private_key && cred->private_key[0] &&
		    nai_realm_cred_cert(wpa_s, eap))
			return eap;
	}

	return NULL;
}


#ifdef INTERWORKING_3GPP

static int plmn_id_match(struct wpabuf *anqp, const char *imsi, int mnc_len)
{
	u8 plmn[3], plmn2[3];
	const u8 *pos, *end;
	u8 udhl;

	/*
	 * See Annex A of 3GPP TS 24.234 v8.1.0 for description. The network
	 * operator is allowed to include only two digits of the MNC, so allow
	 * matches based on both two and three digit MNC assumptions. Since some
	 * SIM/USIM cards may not expose MNC length conveniently, we may be
	 * provided the default MNC length 3 here and as such, checking with MNC
	 * length 2 is justifiable even though 3GPP TS 24.234 does not mention
	 * that case. Anyway, MCC/MNC pair where both 2 and 3 digit MNC is used
	 * with otherwise matching values would not be good idea in general, so
	 * this should not result in selecting incorrect networks.
	 */
	/* Match with 3 digit MNC */
	plmn[0] = (imsi[0] - '0') | ((imsi[1] - '0') << 4);
	plmn[1] = (imsi[2] - '0') | ((imsi[5] - '0') << 4);
	plmn[2] = (imsi[3] - '0') | ((imsi[4] - '0') << 4);
	/* Match with 2 digit MNC */
	plmn2[0] = (imsi[0] - '0') | ((imsi[1] - '0') << 4);
	plmn2[1] = (imsi[2] - '0') | 0xf0;
	plmn2[2] = (imsi[3] - '0') | ((imsi[4] - '0') << 4);

	if (anqp == NULL)
		return 0;
	pos = wpabuf_head_u8(anqp);
	end = pos + wpabuf_len(anqp);
	if (end - pos < 2)
		return 0;
	if (*pos != 0) {
		wpa_printf(MSG_DEBUG, "Unsupported GUD version 0x%x", *pos);
		return 0;
	}
	pos++;
	udhl = *pos++;
	if (udhl > end - pos) {
		wpa_printf(MSG_DEBUG, "Invalid UDHL");
		return 0;
	}
	end = pos + udhl;

	wpa_printf(MSG_DEBUG, "Interworking: Matching against MCC/MNC alternatives: %02x:%02x:%02x or %02x:%02x:%02x (IMSI %s, MNC length %d)",
		   plmn[0], plmn[1], plmn[2], plmn2[0], plmn2[1], plmn2[2],
		   imsi, mnc_len);

	while (end - pos >= 2) {
		u8 iei, len;
		const u8 *l_end;
		iei = *pos++;
		len = *pos++ & 0x7f;
		if (len > end - pos)
			break;
		l_end = pos + len;

		if (iei == 0 && len > 0) {
			/* PLMN List */
			u8 num, i;
			wpa_hexdump(MSG_DEBUG, "Interworking: PLMN List information element",
				    pos, len);
			num = *pos++;
			for (i = 0; i < num; i++) {
				if (l_end - pos < 3)
					break;
				if (os_memcmp(pos, plmn, 3) == 0 ||
				    os_memcmp(pos, plmn2, 3) == 0)
					return 1; /* Found matching PLMN */
				pos += 3;
			}
		} else {
			wpa_hexdump(MSG_DEBUG, "Interworking: Unrecognized 3GPP information element",
				    pos, len);
		}

		pos = l_end;
	}

	return 0;
}


static int build_root_nai(char *nai, size_t nai_len, const char *imsi,
			  size_t mnc_len, char prefix)
{
	const char *sep, *msin;
	char *end, *pos;
	size_t msin_len, plmn_len;

	/*
	 * TS 23.003, Clause 14 (3GPP to WLAN Interworking)
	 * Root NAI:
	 * <aka:0|sim:1><IMSI>@wlan.mnc<MNC>.mcc<MCC>.3gppnetwork.org
	 * <MNC> is zero-padded to three digits in case two-digit MNC is used
	 */

	if (imsi == NULL || os_strlen(imsi) > 16) {
		wpa_printf(MSG_DEBUG, "No valid IMSI available");
		return -1;
	}
	sep = os_strchr(imsi, '-');
	if (sep) {
		plmn_len = sep - imsi;
		msin = sep + 1;
	} else if (mnc_len && os_strlen(imsi) >= 3 + mnc_len) {
		plmn_len = 3 + mnc_len;
		msin = imsi + plmn_len;
	} else
		return -1;
	if (plmn_len != 5 && plmn_len != 6)
		return -1;
	msin_len = os_strlen(msin);

	pos = nai;
	end = nai + nai_len;
	if (prefix)
		*pos++ = prefix;
	os_memcpy(pos, imsi, plmn_len);
	pos += plmn_len;
	os_memcpy(pos, msin, msin_len);
	pos += msin_len;
	pos += os_snprintf(pos, end - pos, "@wlan.mnc");
	if (plmn_len == 5) {
		*pos++ = '0';
		*pos++ = imsi[3];
		*pos++ = imsi[4];
	} else {
		*pos++ = imsi[3];
		*pos++ = imsi[4];
		*pos++ = imsi[5];
	}
	os_snprintf(pos, end - pos, ".mcc%c%c%c.3gppnetwork.org",
		    imsi[0], imsi[1], imsi[2]);

	return 0;
}


static int set_root_nai(struct wpa_ssid *ssid, const char *imsi, char prefix)
{
	char nai[100];
	if (build_root_nai(nai, sizeof(nai), imsi, 0, prefix) < 0)
		return -1;
	return wpa_config_set_quoted(ssid, "identity", nai);
}

#endif /* INTERWORKING_3GPP */


static int already_connected(struct wpa_supplicant *wpa_s,
			     struct wpa_cred *cred, struct wpa_bss *bss)
{
	struct wpa_ssid *ssid, *sel_ssid;
	struct wpa_bss *selected;

	if (wpa_s->wpa_state < WPA_ASSOCIATED || wpa_s->current_ssid == NULL)
		return 0;

	ssid = wpa_s->current_ssid;
	if (ssid->parent_cred != cred)
		return 0;

	if (ssid->ssid_len != bss->ssid_len ||
	    os_memcmp(ssid->ssid, bss->ssid, bss->ssid_len) != 0)
		return 0;

	sel_ssid = NULL;
	selected = wpa_supplicant_pick_network(wpa_s, &sel_ssid);
	if (selected && sel_ssid && sel_ssid->priority > ssid->priority)
		return 0; /* higher priority network in scan results */

	return 1;
}


static void remove_duplicate_network(struct wpa_supplicant *wpa_s,
				     struct wpa_cred *cred,
				     struct wpa_bss *bss)
{
	struct wpa_ssid *ssid;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (ssid->parent_cred != cred)
			continue;
		if (ssid->ssid_len != bss->ssid_len ||
		    os_memcmp(ssid->ssid, bss->ssid, bss->ssid_len) != 0)
			continue;

		break;
	}

	if (ssid == NULL)
		return;

	wpa_printf(MSG_DEBUG, "Interworking: Remove duplicate network entry for the same credential");

	if (ssid == wpa_s->current_ssid) {
		wpa_sm_set_config(wpa_s->wpa, NULL);
		eapol_sm_notify_config(wpa_s->eapol, NULL, NULL);
		wpa_s->own_disconnect_req = 1;
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
	}

	wpas_notify_network_removed(wpa_s, ssid);
	wpa_config_remove_network(wpa_s->conf, ssid->id);
}


static int interworking_set_hs20_params(struct wpa_supplicant *wpa_s,
					struct wpa_ssid *ssid)
{
	const char *key_mgmt = NULL;
#ifdef CONFIG_IEEE80211R
	int res;
	struct wpa_driver_capa capa;

	res = wpa_drv_get_capa(wpa_s, &capa);
	if (res == 0 && capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_FT) {
		key_mgmt = wpa_s->conf->pmf != NO_MGMT_FRAME_PROTECTION ?
			"WPA-EAP WPA-EAP-SHA256 FT-EAP" :
			"WPA-EAP FT-EAP";
	}
#endif /* CONFIG_IEEE80211R */

	if (!key_mgmt)
		key_mgmt = wpa_s->conf->pmf != NO_MGMT_FRAME_PROTECTION ?
			"WPA-EAP WPA-EAP-SHA256" : "WPA-EAP";
	if (wpa_config_set(ssid, "key_mgmt", key_mgmt, 0) < 0 ||
	    wpa_config_set(ssid, "proto", "RSN", 0) < 0 ||
	    wpa_config_set(ssid, "pairwise", "CCMP", 0) < 0)
		return -1;
	return 0;
}


static int interworking_connect_3gpp(struct wpa_supplicant *wpa_s,
				     struct wpa_cred *cred,
				     struct wpa_bss *bss, int only_add)
{
#ifdef INTERWORKING_3GPP
	struct wpa_ssid *ssid;
	int eap_type;
	int res;
	char prefix;

	if (bss->anqp == NULL || bss->anqp->anqp_3gpp == NULL)
		return -1;

	wpa_msg(wpa_s, MSG_DEBUG, "Interworking: Connect with " MACSTR
		" (3GPP)", MAC2STR(bss->bssid));

	if (already_connected(wpa_s, cred, bss)) {
		wpa_msg(wpa_s, MSG_INFO, INTERWORKING_ALREADY_CONNECTED MACSTR,
			MAC2STR(bss->bssid));
		return wpa_s->current_ssid->id;
	}

	remove_duplicate_network(wpa_s, cred, bss);

	ssid = wpa_config_add_network(wpa_s->conf);
	if (ssid == NULL)
		return -1;
	ssid->parent_cred = cred;

	wpas_notify_network_added(wpa_s, ssid);
	wpa_config_set_network_defaults(ssid);
	ssid->priority = cred->priority;
	ssid->temporary = 1;
	ssid->ssid = os_zalloc(bss->ssid_len + 1);
	if (ssid->ssid == NULL)
		goto fail;
	os_memcpy(ssid->ssid, bss->ssid, bss->ssid_len);
	ssid->ssid_len = bss->ssid_len;
	ssid->eap.sim_num = cred->sim_num;

	if (interworking_set_hs20_params(wpa_s, ssid) < 0)
		goto fail;

	eap_type = EAP_TYPE_SIM;
	if (cred->pcsc && wpa_s->scard && scard_supports_umts(wpa_s->scard))
		eap_type = EAP_TYPE_AKA;
	if (cred->eap_method && cred->eap_method[0].vendor == EAP_VENDOR_IETF) {
		if (cred->eap_method[0].method == EAP_TYPE_SIM ||
		    cred->eap_method[0].method == EAP_TYPE_AKA ||
		    cred->eap_method[0].method == EAP_TYPE_AKA_PRIME)
			eap_type = cred->eap_method[0].method;
	}

	switch (eap_type) {
	case EAP_TYPE_SIM:
		prefix = '1';
		res = wpa_config_set(ssid, "eap", "SIM", 0);
		break;
	case EAP_TYPE_AKA:
		prefix = '0';
		res = wpa_config_set(ssid, "eap", "AKA", 0);
		break;
	case EAP_TYPE_AKA_PRIME:
		prefix = '6';
		res = wpa_config_set(ssid, "eap", "AKA'", 0);
		break;
	default:
		res = -1;
		break;
	}
	if (res < 0) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Selected EAP method (%d) not supported", eap_type);
		goto fail;
	}

	if (!cred->pcsc && set_root_nai(ssid, cred->imsi, prefix) < 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "Failed to set Root NAI");
		goto fail;
	}

	if (cred->milenage && cred->milenage[0]) {
		if (wpa_config_set_quoted(ssid, "password",
					  cred->milenage) < 0)
			goto fail;
	} else if (cred->pcsc) {
		if (wpa_config_set_quoted(ssid, "pcsc", "") < 0)
			goto fail;
		if (wpa_s->conf->pcsc_pin &&
		    wpa_config_set_quoted(ssid, "pin", wpa_s->conf->pcsc_pin)
		    < 0)
			goto fail;
	}

	wpa_s->next_ssid = ssid;
	wpa_config_update_prio_list(wpa_s->conf);
	if (!only_add)
		interworking_reconnect(wpa_s);

	return ssid->id;

fail:
	wpas_notify_network_removed(wpa_s, ssid);
	wpa_config_remove_network(wpa_s->conf, ssid->id);
#endif /* INTERWORKING_3GPP */
	return -1;
}


static int roaming_consortium_element_match(const u8 *ie, const u8 *rc_id,
					    size_t rc_len)
{
	const u8 *pos, *end;
	u8 lens;

	if (ie == NULL)
		return 0;

	pos = ie + 2;
	end = ie + 2 + ie[1];

	/* Roaming Consortium element:
	 * Number of ANQP OIs
	 * OI #1 and #2 lengths
	 * OI #1, [OI #2], [OI #3]
	 */

	if (end - pos < 2)
		return 0;

	pos++; /* skip Number of ANQP OIs */
	lens = *pos++;
	if ((lens & 0x0f) + (lens >> 4) > end - pos)
		return 0;

	if ((lens & 0x0f) == rc_len && os_memcmp(pos, rc_id, rc_len) == 0)
		return 1;
	pos += lens & 0x0f;

	if ((lens >> 4) == rc_len && os_memcmp(pos, rc_id, rc_len) == 0)
		return 1;
	pos += lens >> 4;

	if (pos < end && (size_t) (end - pos) == rc_len &&
	    os_memcmp(pos, rc_id, rc_len) == 0)
		return 1;

	return 0;
}


static int roaming_consortium_anqp_match(const struct wpabuf *anqp,
					 const u8 *rc_id, size_t rc_len)
{
	const u8 *pos, *end;
	u8 len;

	if (anqp == NULL)
		return 0;

	pos = wpabuf_head(anqp);
	end = pos + wpabuf_len(anqp);

	/* Set of <OI Length, OI> duples */
	while (pos < end) {
		len = *pos++;
		if (len > end - pos)
			break;
		if (len == rc_len && os_memcmp(pos, rc_id, rc_len) == 0)
			return 1;
		pos += len;
	}

	return 0;
}


static int roaming_consortium_match(const u8 *ie, const struct wpabuf *anqp,
				    const u8 *rc_id, size_t rc_len)
{
	return roaming_consortium_element_match(ie, rc_id, rc_len) ||
		roaming_consortium_anqp_match(anqp, rc_id, rc_len);
}


static int cred_roaming_consortiums_match(const u8 *ie,
					  const struct wpabuf *anqp,
					  const struct wpa_cred *cred)
{
	unsigned int i;

	for (i = 0; i < cred->num_roaming_consortiums; i++) {
		if (roaming_consortium_match(ie, anqp,
					     cred->roaming_consortiums[i],
					     cred->roaming_consortiums_len[i]))
			return 1;
	}

	return 0;
}


static int cred_no_required_oi_match(struct wpa_cred *cred, struct wpa_bss *bss)
{
	const u8 *ie;

	if (cred->required_roaming_consortium_len == 0)
		return 0;

	ie = wpa_bss_get_ie(bss, WLAN_EID_ROAMING_CONSORTIUM);

	if (ie == NULL &&
	    (bss->anqp == NULL || bss->anqp->roaming_consortium == NULL))
		return 1;

	return !roaming_consortium_match(ie,
					 bss->anqp ?
					 bss->anqp->roaming_consortium : NULL,
					 cred->required_roaming_consortium,
					 cred->required_roaming_consortium_len);
}


static int cred_excluded_ssid(struct wpa_cred *cred, struct wpa_bss *bss)
{
	size_t i;

	if (!cred->excluded_ssid)
		return 0;

	for (i = 0; i < cred->num_excluded_ssid; i++) {
		struct excluded_ssid *e = &cred->excluded_ssid[i];
		if (bss->ssid_len == e->ssid_len &&
		    os_memcmp(bss->ssid, e->ssid, e->ssid_len) == 0)
			return 1;
	}

	return 0;
}


static int cred_below_min_backhaul(struct wpa_supplicant *wpa_s,
				   struct wpa_cred *cred, struct wpa_bss *bss)
{
#ifdef CONFIG_HS20
	int res;
	unsigned int dl_bandwidth, ul_bandwidth;
	const u8 *wan;
	u8 wan_info, dl_load, ul_load;
	u16 lmd;
	u32 ul_speed, dl_speed;

	if (!cred->min_dl_bandwidth_home &&
	    !cred->min_ul_bandwidth_home &&
	    !cred->min_dl_bandwidth_roaming &&
	    !cred->min_ul_bandwidth_roaming)
		return 0; /* No bandwidth constraint specified */

	if (bss->anqp == NULL || bss->anqp->hs20_wan_metrics == NULL)
		return 0; /* No WAN Metrics known - ignore constraint */

	wan = wpabuf_head(bss->anqp->hs20_wan_metrics);
	wan_info = wan[0];
	if (wan_info & BIT(3))
		return 1; /* WAN link at capacity */
	lmd = WPA_GET_LE16(wan + 11);
	if (lmd == 0)
		return 0; /* Downlink/Uplink Load was not measured */
	dl_speed = WPA_GET_LE32(wan + 1);
	ul_speed = WPA_GET_LE32(wan + 5);
	dl_load = wan[9];
	ul_load = wan[10];

	if (dl_speed >= 0xffffff)
		dl_bandwidth = dl_speed / 255 * (255 - dl_load);
	else
		dl_bandwidth = dl_speed * (255 - dl_load) / 255;

	if (ul_speed >= 0xffffff)
		ul_bandwidth = ul_speed / 255 * (255 - ul_load);
	else
		ul_bandwidth = ul_speed * (255 - ul_load) / 255;

	res = interworking_home_sp_cred(wpa_s, cred, bss->anqp ?
					bss->anqp->domain_name : NULL);
	if (res > 0) {
		if (cred->min_dl_bandwidth_home > dl_bandwidth)
			return 1;
		if (cred->min_ul_bandwidth_home > ul_bandwidth)
			return 1;
	} else {
		if (cred->min_dl_bandwidth_roaming > dl_bandwidth)
			return 1;
		if (cred->min_ul_bandwidth_roaming > ul_bandwidth)
			return 1;
	}
#endif /* CONFIG_HS20 */

	return 0;
}


static int cred_over_max_bss_load(struct wpa_supplicant *wpa_s,
				  struct wpa_cred *cred, struct wpa_bss *bss)
{
	const u8 *ie;
	int res;

	if (!cred->max_bss_load)
		return 0; /* No BSS Load constraint specified */

	ie = wpa_bss_get_ie(bss, WLAN_EID_BSS_LOAD);
	if (ie == NULL || ie[1] < 3)
		return 0; /* No BSS Load advertised */

	res = interworking_home_sp_cred(wpa_s, cred, bss->anqp ?
					bss->anqp->domain_name : NULL);
	if (res <= 0)
		return 0; /* Not a home network */

	return ie[4] > cred->max_bss_load;
}


#ifdef CONFIG_HS20

static int has_proto_match(const u8 *pos, const u8 *end, u8 proto)
{
	while (end - pos >= 4) {
		if (pos[0] == proto && pos[3] == 1 /* Open */)
			return 1;
		pos += 4;
	}

	return 0;
}


static int has_proto_port_match(const u8 *pos, const u8 *end, u8 proto,
				u16 port)
{
	while (end - pos >= 4) {
		if (pos[0] == proto && WPA_GET_LE16(&pos[1]) == port &&
		    pos[3] == 1 /* Open */)
			return 1;
		pos += 4;
	}

	return 0;
}

#endif /* CONFIG_HS20 */


static int cred_conn_capab_missing(struct wpa_supplicant *wpa_s,
				   struct wpa_cred *cred, struct wpa_bss *bss)
{
#ifdef CONFIG_HS20
	int res;
	const u8 *capab, *end;
	unsigned int i, j;
	int *ports;

	if (!cred->num_req_conn_capab)
		return 0; /* No connection capability constraint specified */

	if (bss->anqp == NULL || bss->anqp->hs20_connection_capability == NULL)
		return 0; /* No Connection Capability known - ignore constraint
			   */

	res = interworking_home_sp_cred(wpa_s, cred, bss->anqp ?
					bss->anqp->domain_name : NULL);
	if (res > 0)
		return 0; /* No constraint in home network */

	capab = wpabuf_head(bss->anqp->hs20_connection_capability);
	end = capab + wpabuf_len(bss->anqp->hs20_connection_capability);

	for (i = 0; i < cred->num_req_conn_capab; i++) {
		ports = cred->req_conn_capab_port[i];
		if (!ports) {
			if (!has_proto_match(capab, end,
					     cred->req_conn_capab_proto[i]))
				return 1;
		} else {
			for (j = 0; ports[j] > -1; j++) {
				if (!has_proto_port_match(
					    capab, end,
					    cred->req_conn_capab_proto[i],
					    ports[j]))
					return 1;
			}
		}
	}
#endif /* CONFIG_HS20 */

	return 0;
}


static struct wpa_cred * interworking_credentials_available_roaming_consortium(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded)
{
	struct wpa_cred *cred, *selected = NULL;
	const u8 *ie;
	const struct wpabuf *anqp;
	int is_excluded = 0;

	ie = wpa_bss_get_ie(bss, WLAN_EID_ROAMING_CONSORTIUM);
	anqp = bss->anqp ? bss->anqp->roaming_consortium : NULL;

	if (!ie && !anqp)
		return NULL;

	if (wpa_s->conf->cred == NULL)
		return NULL;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->roaming_consortium_len == 0 &&
		    cred->num_roaming_consortiums == 0)
			continue;

		if ((cred->roaming_consortium_len == 0 ||
		     !roaming_consortium_match(ie, anqp,
					       cred->roaming_consortium,
					       cred->roaming_consortium_len)) &&
		    !cred_roaming_consortiums_match(ie, anqp, cred))
			continue;

		if (cred_no_required_oi_match(cred, bss))
			continue;
		if (!ignore_bw && cred_below_min_backhaul(wpa_s, cred, bss))
			continue;
		if (!ignore_bw && cred_over_max_bss_load(wpa_s, cred, bss))
			continue;
		if (!ignore_bw && cred_conn_capab_missing(wpa_s, cred, bss))
			continue;
		if (cred_excluded_ssid(cred, bss)) {
			if (excluded == NULL)
				continue;
			if (selected == NULL) {
				selected = cred;
				is_excluded = 1;
			}
		} else {
			if (selected == NULL || is_excluded ||
			    cred_prio_cmp(selected, cred) < 0) {
				selected = cred;
				is_excluded = 0;
			}
		}
	}

	if (excluded)
		*excluded = is_excluded;

	return selected;
}


static int interworking_set_eap_params(struct wpa_ssid *ssid,
				       struct wpa_cred *cred, int ttls)
{
	if (cred->eap_method) {
		ttls = cred->eap_method->vendor == EAP_VENDOR_IETF &&
			cred->eap_method->method == EAP_TYPE_TTLS;

		os_free(ssid->eap.eap_methods);
		ssid->eap.eap_methods =
			os_malloc(sizeof(struct eap_method_type) * 2);
		if (ssid->eap.eap_methods == NULL)
			return -1;
		os_memcpy(ssid->eap.eap_methods, cred->eap_method,
			  sizeof(*cred->eap_method));
		ssid->eap.eap_methods[1].vendor = EAP_VENDOR_IETF;
		ssid->eap.eap_methods[1].method = EAP_TYPE_NONE;
	}

	if (ttls && cred->username && cred->username[0]) {
		const char *pos;
		char *anon;
		/* Use anonymous NAI in Phase 1 */
		pos = os_strchr(cred->username, '@');
		if (pos) {
			size_t buflen = 9 + os_strlen(pos) + 1;
			anon = os_malloc(buflen);
			if (anon == NULL)
				return -1;
			os_snprintf(anon, buflen, "anonymous%s", pos);
		} else if (cred->realm) {
			size_t buflen = 10 + os_strlen(cred->realm) + 1;
			anon = os_malloc(buflen);
			if (anon == NULL)
				return -1;
			os_snprintf(anon, buflen, "anonymous@%s", cred->realm);
		} else {
			anon = os_strdup("anonymous");
			if (anon == NULL)
				return -1;
		}
		if (wpa_config_set_quoted(ssid, "anonymous_identity", anon) <
		    0) {
			os_free(anon);
			return -1;
		}
		os_free(anon);
	}

	if (!ttls && cred->username && cred->username[0] && cred->realm &&
	    !os_strchr(cred->username, '@')) {
		char *id;
		size_t buflen;
		int res;

		buflen = os_strlen(cred->username) + 1 +
			os_strlen(cred->realm) + 1;

		id = os_malloc(buflen);
		if (!id)
			return -1;
		os_snprintf(id, buflen, "%s@%s", cred->username, cred->realm);
		res = wpa_config_set_quoted(ssid, "identity", id);
		os_free(id);
		if (res < 0)
			return -1;
	} else if (cred->username && cred->username[0] &&
	    wpa_config_set_quoted(ssid, "identity", cred->username) < 0)
		return -1;

	if (cred->password && cred->password[0]) {
		if (cred->ext_password &&
		    wpa_config_set(ssid, "password", cred->password, 0) < 0)
			return -1;
		if (!cred->ext_password &&
		    wpa_config_set_quoted(ssid, "password", cred->password) <
		    0)
			return -1;
	}

	if (cred->client_cert && cred->client_cert[0] &&
	    wpa_config_set_quoted(ssid, "client_cert", cred->client_cert) < 0)
		return -1;

#ifdef ANDROID
	if (cred->private_key &&
	    os_strncmp(cred->private_key, "keystore://", 11) == 0) {
		/* Use OpenSSL engine configuration for Android keystore */
		if (wpa_config_set_quoted(ssid, "engine_id", "keystore") < 0 ||
		    wpa_config_set_quoted(ssid, "key_id",
					  cred->private_key + 11) < 0 ||
		    wpa_config_set(ssid, "engine", "1", 0) < 0)
			return -1;
	} else
#endif /* ANDROID */
	if (cred->private_key && cred->private_key[0] &&
	    wpa_config_set_quoted(ssid, "private_key", cred->private_key) < 0)
		return -1;

	if (cred->private_key_passwd && cred->private_key_passwd[0] &&
	    wpa_config_set_quoted(ssid, "private_key_passwd",
				  cred->private_key_passwd) < 0)
		return -1;

	if (cred->phase1) {
		os_free(ssid->eap.phase1);
		ssid->eap.phase1 = os_strdup(cred->phase1);
	}
	if (cred->phase2) {
		os_free(ssid->eap.phase2);
		ssid->eap.phase2 = os_strdup(cred->phase2);
	}

	if (cred->ca_cert && cred->ca_cert[0] &&
	    wpa_config_set_quoted(ssid, "ca_cert", cred->ca_cert) < 0)
		return -1;

	if (cred->domain_suffix_match && cred->domain_suffix_match[0] &&
	    wpa_config_set_quoted(ssid, "domain_suffix_match",
				  cred->domain_suffix_match) < 0)
		return -1;

	ssid->eap.ocsp = cred->ocsp;

	return 0;
}


static int interworking_connect_roaming_consortium(
	struct wpa_supplicant *wpa_s, struct wpa_cred *cred,
	struct wpa_bss *bss, int only_add)
{
	struct wpa_ssid *ssid;
	const u8 *ie;
	const struct wpabuf *anqp;
	unsigned int i;

	wpa_msg(wpa_s, MSG_DEBUG, "Interworking: Connect with " MACSTR
		" based on roaming consortium match", MAC2STR(bss->bssid));

	if (already_connected(wpa_s, cred, bss)) {
		wpa_msg(wpa_s, MSG_INFO, INTERWORKING_ALREADY_CONNECTED MACSTR,
			MAC2STR(bss->bssid));
		return wpa_s->current_ssid->id;
	}

	remove_duplicate_network(wpa_s, cred, bss);

	ssid = wpa_config_add_network(wpa_s->conf);
	if (ssid == NULL)
		return -1;
	ssid->parent_cred = cred;
	wpas_notify_network_added(wpa_s, ssid);
	wpa_config_set_network_defaults(ssid);
	ssid->priority = cred->priority;
	ssid->temporary = 1;
	ssid->ssid = os_zalloc(bss->ssid_len + 1);
	if (ssid->ssid == NULL)
		goto fail;
	os_memcpy(ssid->ssid, bss->ssid, bss->ssid_len);
	ssid->ssid_len = bss->ssid_len;

	if (interworking_set_hs20_params(wpa_s, ssid) < 0)
		goto fail;

	ie = wpa_bss_get_ie(bss, WLAN_EID_ROAMING_CONSORTIUM);
	anqp = bss->anqp ? bss->anqp->roaming_consortium : NULL;
	for (i = 0; (ie || anqp) && i < cred->num_roaming_consortiums; i++) {
		if (!roaming_consortium_match(
			    ie, anqp, cred->roaming_consortiums[i],
			    cred->roaming_consortiums_len[i]))
			continue;

		ssid->roaming_consortium_selection =
			os_malloc(cred->roaming_consortiums_len[i]);
		if (!ssid->roaming_consortium_selection)
			goto fail;
		os_memcpy(ssid->roaming_consortium_selection,
			  cred->roaming_consortiums[i],
			  cred->roaming_consortiums_len[i]);
		ssid->roaming_consortium_selection_len =
			cred->roaming_consortiums_len[i];
		break;
	}

	if (cred->eap_method == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: No EAP method set for credential using roaming consortium");
		goto fail;
	}

	if (interworking_set_eap_params(
		    ssid, cred,
		    cred->eap_method->vendor == EAP_VENDOR_IETF &&
		    cred->eap_method->method == EAP_TYPE_TTLS) < 0)
		goto fail;

	wpa_s->next_ssid = ssid;
	wpa_config_update_prio_list(wpa_s->conf);
	if (!only_add)
		interworking_reconnect(wpa_s);

	return ssid->id;

fail:
	wpas_notify_network_removed(wpa_s, ssid);
	wpa_config_remove_network(wpa_s->conf, ssid->id);
	return -1;
}


int interworking_connect(struct wpa_supplicant *wpa_s, struct wpa_bss *bss,
			 int only_add)
{
	struct wpa_cred *cred, *cred_rc, *cred_3gpp;
	struct wpa_ssid *ssid;
	struct nai_realm *realm;
	struct nai_realm_eap *eap = NULL;
	u16 count, i;
	char buf[100];
	int excluded = 0, *excl = &excluded;
	const char *name;

	if (wpa_s->conf->cred == NULL || bss == NULL)
		return -1;
	if (disallowed_bssid(wpa_s, bss->bssid) ||
	    disallowed_ssid(wpa_s, bss->ssid, bss->ssid_len)) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Reject connection to disallowed BSS "
			MACSTR, MAC2STR(bss->bssid));
		return -1;
	}

	wpa_printf(MSG_DEBUG, "Interworking: Considering BSS " MACSTR
		   " for connection",
		   MAC2STR(bss->bssid));

	if (!wpa_bss_get_ie(bss, WLAN_EID_RSN)) {
		/*
		 * We currently support only HS 2.0 networks and those are
		 * required to use WPA2-Enterprise.
		 */
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Network does not use RSN");
		return -1;
	}

	cred_rc = interworking_credentials_available_roaming_consortium(
		wpa_s, bss, 0, excl);
	if (cred_rc) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Highest roaming consortium matching credential priority %d sp_priority %d",
			cred_rc->priority, cred_rc->sp_priority);
		if (excl && !(*excl))
			excl = NULL;
	}

	cred = interworking_credentials_available_realm(wpa_s, bss, 0, excl);
	if (cred) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Highest NAI Realm list matching credential priority %d sp_priority %d",
			cred->priority, cred->sp_priority);
		if (excl && !(*excl))
			excl = NULL;
	}

	cred_3gpp = interworking_credentials_available_3gpp(wpa_s, bss, 0,
							    excl);
	if (cred_3gpp) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Highest 3GPP matching credential priority %d sp_priority %d",
			cred_3gpp->priority, cred_3gpp->sp_priority);
		if (excl && !(*excl))
			excl = NULL;
	}

	if (!cred_rc && !cred && !cred_3gpp) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: No full credential matches - consider options without BW(etc.) limits");
		cred_rc = interworking_credentials_available_roaming_consortium(
			wpa_s, bss, 1, excl);
		if (cred_rc) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Highest roaming consortium matching credential priority %d sp_priority %d (ignore BW)",
				cred_rc->priority, cred_rc->sp_priority);
			if (excl && !(*excl))
				excl = NULL;
		}

		cred = interworking_credentials_available_realm(wpa_s, bss, 1,
								excl);
		if (cred) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Highest NAI Realm list matching credential priority %d sp_priority %d (ignore BW)",
				cred->priority, cred->sp_priority);
			if (excl && !(*excl))
				excl = NULL;
		}

		cred_3gpp = interworking_credentials_available_3gpp(wpa_s, bss,
								    1, excl);
		if (cred_3gpp) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Highest 3GPP matching credential priority %d sp_priority %d (ignore BW)",
				cred_3gpp->priority, cred_3gpp->sp_priority);
			if (excl && !(*excl))
				excl = NULL;
		}
	}

	if (cred_rc &&
	    (cred == NULL || cred_prio_cmp(cred_rc, cred) >= 0) &&
	    (cred_3gpp == NULL || cred_prio_cmp(cred_rc, cred_3gpp) >= 0))
		return interworking_connect_roaming_consortium(wpa_s, cred_rc,
							       bss, only_add);

	if (cred_3gpp &&
	    (cred == NULL || cred_prio_cmp(cred_3gpp, cred) >= 0)) {
		return interworking_connect_3gpp(wpa_s, cred_3gpp, bss,
						 only_add);
	}

	if (cred == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: No matching credentials found for "
			MACSTR, MAC2STR(bss->bssid));
		return -1;
	}

	realm = nai_realm_parse(bss->anqp ? bss->anqp->nai_realm : NULL,
				&count);
	if (realm == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Could not parse NAI Realm list from "
			MACSTR, MAC2STR(bss->bssid));
		return -1;
	}

	for (i = 0; i < count; i++) {
		if (!nai_realm_match(&realm[i], cred->realm))
			continue;
		eap = nai_realm_find_eap(wpa_s, cred, &realm[i]);
		if (eap)
			break;
	}

	if (!eap) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: No matching credentials and EAP method found for "
			MACSTR, MAC2STR(bss->bssid));
		nai_realm_free(realm, count);
		return -1;
	}

	wpa_msg(wpa_s, MSG_DEBUG, "Interworking: Connect with " MACSTR,
		MAC2STR(bss->bssid));

	if (already_connected(wpa_s, cred, bss)) {
		wpa_msg(wpa_s, MSG_INFO, INTERWORKING_ALREADY_CONNECTED MACSTR,
			MAC2STR(bss->bssid));
		nai_realm_free(realm, count);
		return 0;
	}

	remove_duplicate_network(wpa_s, cred, bss);

	ssid = wpa_config_add_network(wpa_s->conf);
	if (ssid == NULL) {
		nai_realm_free(realm, count);
		return -1;
	}
	ssid->parent_cred = cred;
	wpas_notify_network_added(wpa_s, ssid);
	wpa_config_set_network_defaults(ssid);
	ssid->priority = cred->priority;
	ssid->temporary = 1;
	ssid->ssid = os_zalloc(bss->ssid_len + 1);
	if (ssid->ssid == NULL)
		goto fail;
	os_memcpy(ssid->ssid, bss->ssid, bss->ssid_len);
	ssid->ssid_len = bss->ssid_len;

	if (interworking_set_hs20_params(wpa_s, ssid) < 0)
		goto fail;

	if (wpa_config_set(ssid, "eap", eap_get_name(EAP_VENDOR_IETF,
						     eap->method), 0) < 0)
		goto fail;

	switch (eap->method) {
	case EAP_TYPE_TTLS:
		if (eap->inner_method) {
			name = eap_get_name(EAP_VENDOR_IETF, eap->inner_method);
			if (!name)
				goto fail;
			os_snprintf(buf, sizeof(buf), "\"autheap=%s\"", name);
			if (wpa_config_set(ssid, "phase2", buf, 0) < 0)
				goto fail;
			break;
		}
		switch (eap->inner_non_eap) {
		case NAI_REALM_INNER_NON_EAP_PAP:
			if (wpa_config_set(ssid, "phase2", "\"auth=PAP\"", 0) <
			    0)
				goto fail;
			break;
		case NAI_REALM_INNER_NON_EAP_CHAP:
			if (wpa_config_set(ssid, "phase2", "\"auth=CHAP\"", 0)
			    < 0)
				goto fail;
			break;
		case NAI_REALM_INNER_NON_EAP_MSCHAP:
			if (wpa_config_set(ssid, "phase2", "\"auth=MSCHAP\"",
					   0) < 0)
				goto fail;
			break;
		case NAI_REALM_INNER_NON_EAP_MSCHAPV2:
			if (wpa_config_set(ssid, "phase2", "\"auth=MSCHAPV2\"",
					   0) < 0)
				goto fail;
			break;
		default:
			/* EAP params were not set - assume TTLS/MSCHAPv2 */
			if (wpa_config_set(ssid, "phase2", "\"auth=MSCHAPV2\"",
					   0) < 0)
				goto fail;
			break;
		}
		break;
	case EAP_TYPE_PEAP:
	case EAP_TYPE_FAST:
		if (wpa_config_set(ssid, "phase1", "\"fast_provisioning=2\"",
				   0) < 0)
			goto fail;
		if (wpa_config_set(ssid, "pac_file",
				   "\"blob://pac_interworking\"", 0) < 0)
			goto fail;
		name = eap_get_name(EAP_VENDOR_IETF,
				    eap->inner_method ? eap->inner_method :
				    EAP_TYPE_MSCHAPV2);
		if (name == NULL)
			goto fail;
		os_snprintf(buf, sizeof(buf), "\"auth=%s\"", name);
		if (wpa_config_set(ssid, "phase2", buf, 0) < 0)
			goto fail;
		break;
	case EAP_TYPE_TLS:
		break;
	}

	if (interworking_set_eap_params(ssid, cred,
					eap->method == EAP_TYPE_TTLS) < 0)
		goto fail;

	nai_realm_free(realm, count);

	wpa_s->next_ssid = ssid;
	wpa_config_update_prio_list(wpa_s->conf);
	if (!only_add)
		interworking_reconnect(wpa_s);

	return ssid->id;

fail:
	wpas_notify_network_removed(wpa_s, ssid);
	wpa_config_remove_network(wpa_s->conf, ssid->id);
	nai_realm_free(realm, count);
	return -1;
}


#ifdef PCSC_FUNCS
static int interworking_pcsc_read_imsi(struct wpa_supplicant *wpa_s)
{
	size_t len;

	if (wpa_s->imsi[0] && wpa_s->mnc_len)
		return 0;

	len = sizeof(wpa_s->imsi) - 1;
	if (scard_get_imsi(wpa_s->scard, wpa_s->imsi, &len)) {
		scard_deinit(wpa_s->scard);
		wpa_s->scard = NULL;
		wpa_msg(wpa_s, MSG_ERROR, "Could not read IMSI");
		return -1;
	}
	wpa_s->imsi[len] = '\0';
	wpa_s->mnc_len = scard_get_mnc_len(wpa_s->scard);
	wpa_printf(MSG_DEBUG, "SCARD: IMSI %s (MNC length %d)",
		   wpa_s->imsi, wpa_s->mnc_len);

	return 0;
}
#endif /* PCSC_FUNCS */


static struct wpa_cred * interworking_credentials_available_3gpp(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded)
{
	struct wpa_cred *selected = NULL;
#ifdef INTERWORKING_3GPP
	struct wpa_cred *cred;
	int ret;
	int is_excluded = 0;

	if (bss->anqp == NULL || bss->anqp->anqp_3gpp == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"interworking-avail-3gpp: not avail, anqp: %p  anqp_3gpp: %p",
			bss->anqp, bss->anqp ? bss->anqp->anqp_3gpp : NULL);
		return NULL;
	}

#ifdef CONFIG_EAP_PROXY
	if (!wpa_s->imsi[0]) {
		size_t len;
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: IMSI not available - try to read again through eap_proxy");
		wpa_s->mnc_len = eapol_sm_get_eap_proxy_imsi(wpa_s->eapol, -1,
							     wpa_s->imsi,
							     &len);
		if (wpa_s->mnc_len > 0) {
			wpa_s->imsi[len] = '\0';
			wpa_msg(wpa_s, MSG_DEBUG,
				"eap_proxy: IMSI %s (MNC length %d)",
				wpa_s->imsi, wpa_s->mnc_len);
		} else {
			wpa_msg(wpa_s, MSG_DEBUG,
				"eap_proxy: IMSI not available");
		}
	}
#endif /* CONFIG_EAP_PROXY */

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		char *sep;
		const char *imsi;
		int mnc_len;
		char imsi_buf[16];
		size_t msin_len;

#ifdef PCSC_FUNCS
		if (cred->pcsc && wpa_s->scard) {
			if (interworking_pcsc_read_imsi(wpa_s) < 0)
				continue;
			imsi = wpa_s->imsi;
			mnc_len = wpa_s->mnc_len;
			goto compare;
		}
#endif /* PCSC_FUNCS */
#ifdef CONFIG_EAP_PROXY
		if (cred->pcsc && wpa_s->mnc_len > 0 && wpa_s->imsi[0]) {
			imsi = wpa_s->imsi;
			mnc_len = wpa_s->mnc_len;
			goto compare;
		}
#endif /* CONFIG_EAP_PROXY */

		if (cred->imsi == NULL || !cred->imsi[0] ||
		    (!wpa_s->conf->external_sim &&
		     (cred->milenage == NULL || !cred->milenage[0])))
			continue;

		sep = os_strchr(cred->imsi, '-');
		if (sep == NULL ||
		    (sep - cred->imsi != 5 && sep - cred->imsi != 6))
			continue;
		mnc_len = sep - cred->imsi - 3;
		os_memcpy(imsi_buf, cred->imsi, 3 + mnc_len);
		sep++;
		msin_len = os_strlen(cred->imsi);
		if (3 + mnc_len + msin_len >= sizeof(imsi_buf) - 1)
			msin_len = sizeof(imsi_buf) - 3 - mnc_len - 1;
		os_memcpy(&imsi_buf[3 + mnc_len], sep, msin_len);
		imsi_buf[3 + mnc_len + msin_len] = '\0';
		imsi = imsi_buf;

#if defined(PCSC_FUNCS) || defined(CONFIG_EAP_PROXY)
	compare:
#endif /* PCSC_FUNCS || CONFIG_EAP_PROXY */
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Parsing 3GPP info from " MACSTR,
			MAC2STR(bss->bssid));
		ret = plmn_id_match(bss->anqp->anqp_3gpp, imsi, mnc_len);
		wpa_msg(wpa_s, MSG_DEBUG, "PLMN match %sfound",
			ret ? "" : "not ");
		if (ret) {
			if (cred_no_required_oi_match(cred, bss))
				continue;
			if (!ignore_bw &&
			    cred_below_min_backhaul(wpa_s, cred, bss))
				continue;
			if (!ignore_bw &&
			    cred_over_max_bss_load(wpa_s, cred, bss))
				continue;
			if (!ignore_bw &&
			    cred_conn_capab_missing(wpa_s, cred, bss))
				continue;
			if (cred_excluded_ssid(cred, bss)) {
				if (excluded == NULL)
					continue;
				if (selected == NULL) {
					selected = cred;
					is_excluded = 1;
				}
			} else {
				if (selected == NULL || is_excluded ||
				    cred_prio_cmp(selected, cred) < 0) {
					selected = cred;
					is_excluded = 0;
				}
			}
		}
	}

	if (excluded)
		*excluded = is_excluded;
#endif /* INTERWORKING_3GPP */
	return selected;
}


static struct wpa_cred * interworking_credentials_available_realm(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded)
{
	struct wpa_cred *cred, *selected = NULL;
	struct nai_realm *realm;
	u16 count, i;
	int is_excluded = 0;

	if (bss->anqp == NULL || bss->anqp->nai_realm == NULL)
		return NULL;

	if (wpa_s->conf->cred == NULL)
		return NULL;

	wpa_msg(wpa_s, MSG_DEBUG, "Interworking: Parsing NAI Realm list from "
		MACSTR, MAC2STR(bss->bssid));
	realm = nai_realm_parse(bss->anqp->nai_realm, &count);
	if (realm == NULL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Could not parse NAI Realm list from "
			MACSTR, MAC2STR(bss->bssid));
		return NULL;
	}

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		if (cred->realm == NULL)
			continue;

		for (i = 0; i < count; i++) {
			if (!nai_realm_match(&realm[i], cred->realm))
				continue;
			if (nai_realm_find_eap(wpa_s, cred, &realm[i])) {
				if (cred_no_required_oi_match(cred, bss))
					continue;
				if (!ignore_bw &&
				    cred_below_min_backhaul(wpa_s, cred, bss))
					continue;
				if (!ignore_bw &&
				    cred_over_max_bss_load(wpa_s, cred, bss))
					continue;
				if (!ignore_bw &&
				    cred_conn_capab_missing(wpa_s, cred, bss))
					continue;
				if (cred_excluded_ssid(cred, bss)) {
					if (excluded == NULL)
						continue;
					if (selected == NULL) {
						selected = cred;
						is_excluded = 1;
					}
				} else {
					if (selected == NULL || is_excluded ||
					    cred_prio_cmp(selected, cred) < 0)
					{
						selected = cred;
						is_excluded = 0;
					}
				}
				break;
			} else {
				wpa_msg(wpa_s, MSG_DEBUG,
					"Interworking: realm-find-eap returned false");
			}
		}
	}

	nai_realm_free(realm, count);

	if (excluded)
		*excluded = is_excluded;

	return selected;
}


static struct wpa_cred * interworking_credentials_available_helper(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int ignore_bw,
	int *excluded)
{
	struct wpa_cred *cred, *cred2;
	int excluded1, excluded2 = 0;

	if (disallowed_bssid(wpa_s, bss->bssid) ||
	    disallowed_ssid(wpa_s, bss->ssid, bss->ssid_len)) {
		wpa_printf(MSG_DEBUG, "Interworking: Ignore disallowed BSS "
			   MACSTR, MAC2STR(bss->bssid));
		return NULL;
	}

	cred = interworking_credentials_available_realm(wpa_s, bss, ignore_bw,
							&excluded1);
	cred2 = interworking_credentials_available_3gpp(wpa_s, bss, ignore_bw,
							&excluded2);
	if (cred && cred2 &&
	    (cred_prio_cmp(cred2, cred) >= 0 || (!excluded2 && excluded1))) {
		cred = cred2;
		excluded1 = excluded2;
	}
	if (!cred) {
		cred = cred2;
		excluded1 = excluded2;
	}

	cred2 = interworking_credentials_available_roaming_consortium(
		wpa_s, bss, ignore_bw, &excluded2);
	if (cred && cred2 &&
	    (cred_prio_cmp(cred2, cred) >= 0 || (!excluded2 && excluded1))) {
		cred = cred2;
		excluded1 = excluded2;
	}
	if (!cred) {
		cred = cred2;
		excluded1 = excluded2;
	}

	if (excluded)
		*excluded = excluded1;
	return cred;
}


static struct wpa_cred * interworking_credentials_available(
	struct wpa_supplicant *wpa_s, struct wpa_bss *bss, int *excluded)
{
	struct wpa_cred *cred;

	if (excluded)
		*excluded = 0;
	cred = interworking_credentials_available_helper(wpa_s, bss, 0,
							 excluded);
	if (cred)
		return cred;
	return interworking_credentials_available_helper(wpa_s, bss, 1,
							 excluded);
}


int domain_name_list_contains(struct wpabuf *domain_names,
			      const char *domain, int exact_match)
{
	const u8 *pos, *end;
	size_t len;

	len = os_strlen(domain);
	pos = wpabuf_head(domain_names);
	end = pos + wpabuf_len(domain_names);

	while (end - pos > 1) {
		u8 elen;

		elen = *pos++;
		if (elen > end - pos)
			break;

		wpa_hexdump_ascii(MSG_DEBUG, "Interworking: AP domain name",
				  pos, elen);
		if (elen == len &&
		    os_strncasecmp(domain, (const char *) pos, len) == 0)
			return 1;
		if (!exact_match && elen > len && pos[elen - len - 1] == '.') {
			const char *ap = (const char *) pos;
			int offset = elen - len;

			if (os_strncasecmp(domain, ap + offset, len) == 0)
				return 1;
		}

		pos += elen;
	}

	return 0;
}


int interworking_home_sp_cred(struct wpa_supplicant *wpa_s,
			      struct wpa_cred *cred,
			      struct wpabuf *domain_names)
{
	size_t i;
	int ret = -1;
#ifdef INTERWORKING_3GPP
	char nai[100], *realm;

	char *imsi = NULL;
	int mnc_len = 0;
	if (cred->imsi)
		imsi = cred->imsi;
#ifdef PCSC_FUNCS
	else if (cred->pcsc && wpa_s->scard) {
		if (interworking_pcsc_read_imsi(wpa_s) < 0)
			return -1;
		imsi = wpa_s->imsi;
		mnc_len = wpa_s->mnc_len;
	}
#endif /* PCSC_FUNCS */
#ifdef CONFIG_EAP_PROXY
	else if (cred->pcsc && wpa_s->mnc_len > 0 && wpa_s->imsi[0]) {
		imsi = wpa_s->imsi;
		mnc_len = wpa_s->mnc_len;
	}
#endif /* CONFIG_EAP_PROXY */
	if (domain_names &&
	    imsi && build_root_nai(nai, sizeof(nai), imsi, mnc_len, 0) == 0) {
		realm = os_strchr(nai, '@');
		if (realm)
			realm++;
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Search for match with SIM/USIM domain %s",
			realm);
		if (realm &&
		    domain_name_list_contains(domain_names, realm, 1))
			return 1;
		if (realm)
			ret = 0;
	}
#endif /* INTERWORKING_3GPP */

	if (domain_names == NULL || cred->domain == NULL)
		return ret;

	for (i = 0; i < cred->num_domain; i++) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Search for match with home SP FQDN %s",
			cred->domain[i]);
		if (domain_name_list_contains(domain_names, cred->domain[i], 1))
			return 1;
	}

	return 0;
}


static int interworking_home_sp(struct wpa_supplicant *wpa_s,
				struct wpabuf *domain_names)
{
	struct wpa_cred *cred;

	if (domain_names == NULL || wpa_s->conf->cred == NULL)
		return -1;

	for (cred = wpa_s->conf->cred; cred; cred = cred->next) {
		int res = interworking_home_sp_cred(wpa_s, cred, domain_names);
		if (res)
			return res;
	}

	return 0;
}


static int interworking_find_network_match(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;
	struct wpa_ssid *ssid;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
			if (wpas_network_disabled(wpa_s, ssid) ||
			    ssid->mode != WPAS_MODE_INFRA)
				continue;
			if (ssid->ssid_len != bss->ssid_len ||
			    os_memcmp(ssid->ssid, bss->ssid, ssid->ssid_len) !=
			    0)
				continue;
			/*
			 * TODO: Consider more accurate matching of security
			 * configuration similarly to what is done in events.c
			 */
			return 1;
		}
	}

	return 0;
}


static int roaming_partner_match(struct wpa_supplicant *wpa_s,
				 struct roaming_partner *partner,
				 struct wpabuf *domain_names)
{
	wpa_printf(MSG_DEBUG, "Interworking: Comparing roaming_partner info fqdn='%s' exact_match=%d priority=%u country='%s'",
		   partner->fqdn, partner->exact_match, partner->priority,
		   partner->country);
	wpa_hexdump_ascii(MSG_DEBUG, "Interworking: Domain names",
			  wpabuf_head(domain_names),
			  wpabuf_len(domain_names));
	if (!domain_name_list_contains(domain_names, partner->fqdn,
				       partner->exact_match))
		return 0;
	/* TODO: match Country */
	return 1;
}


static u8 roaming_prio(struct wpa_supplicant *wpa_s, struct wpa_cred *cred,
		       struct wpa_bss *bss)
{
	size_t i;

	if (bss->anqp == NULL || bss->anqp->domain_name == NULL) {
		wpa_printf(MSG_DEBUG, "Interworking: No ANQP domain name info -> use default roaming partner priority 128");
		return 128; /* cannot check preference with domain name */
	}

	if (interworking_home_sp_cred(wpa_s, cred, bss->anqp->domain_name) > 0)
	{
		wpa_printf(MSG_DEBUG, "Interworking: Determined to be home SP -> use maximum preference 0 as roaming partner priority");
		return 0; /* max preference for home SP network */
	}

	for (i = 0; i < cred->num_roaming_partner; i++) {
		if (roaming_partner_match(wpa_s, &cred->roaming_partner[i],
					  bss->anqp->domain_name)) {
			wpa_printf(MSG_DEBUG, "Interworking: Roaming partner preference match - priority %u",
				   cred->roaming_partner[i].priority);
			return cred->roaming_partner[i].priority;
		}
	}

	wpa_printf(MSG_DEBUG, "Interworking: No roaming partner preference match - use default roaming partner priority 128");
	return 128;
}


static struct wpa_bss * pick_best_roaming_partner(struct wpa_supplicant *wpa_s,
						  struct wpa_bss *selected,
						  struct wpa_cred *cred)
{
	struct wpa_bss *bss;
	u8 best_prio, prio;
	struct wpa_cred *cred2;

	/*
	 * Check if any other BSS is operated by a more preferred roaming
	 * partner.
	 */

	best_prio = roaming_prio(wpa_s, cred, selected);
	wpa_printf(MSG_DEBUG, "Interworking: roaming_prio=%u for selected BSS "
		   MACSTR " (cred=%d)", best_prio, MAC2STR(selected->bssid),
		   cred->id);

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (bss == selected)
			continue;
		cred2 = interworking_credentials_available(wpa_s, bss, NULL);
		if (!cred2)
			continue;
		if (!wpa_bss_get_ie(bss, WLAN_EID_RSN))
			continue;
		prio = roaming_prio(wpa_s, cred2, bss);
		wpa_printf(MSG_DEBUG, "Interworking: roaming_prio=%u for BSS "
			   MACSTR " (cred=%d)", prio, MAC2STR(bss->bssid),
			   cred2->id);
		if (prio < best_prio) {
			int bh1, bh2, load1, load2, conn1, conn2;
			bh1 = cred_below_min_backhaul(wpa_s, cred, selected);
			load1 = cred_over_max_bss_load(wpa_s, cred, selected);
			conn1 = cred_conn_capab_missing(wpa_s, cred, selected);
			bh2 = cred_below_min_backhaul(wpa_s, cred2, bss);
			load2 = cred_over_max_bss_load(wpa_s, cred2, bss);
			conn2 = cred_conn_capab_missing(wpa_s, cred2, bss);
			wpa_printf(MSG_DEBUG, "Interworking: old: %d %d %d  new: %d %d %d",
				   bh1, load1, conn1, bh2, load2, conn2);
			if (bh1 || load1 || conn1 || !(bh2 || load2 || conn2)) {
				wpa_printf(MSG_DEBUG, "Interworking: Better roaming partner " MACSTR " selected", MAC2STR(bss->bssid));
				best_prio = prio;
				selected = bss;
			}
		}
	}

	return selected;
}


static void interworking_select_network(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss, *selected = NULL, *selected_home = NULL;
	struct wpa_bss *selected2 = NULL, *selected2_home = NULL;
	unsigned int count = 0;
	const char *type;
	int res;
	struct wpa_cred *cred, *selected_cred = NULL;
	struct wpa_cred *selected_home_cred = NULL;
	struct wpa_cred *selected2_cred = NULL;
	struct wpa_cred *selected2_home_cred = NULL;

	wpa_s->network_select = 0;

	wpa_printf(MSG_DEBUG, "Interworking: Select network (auto_select=%d)",
		   wpa_s->auto_select);
	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		int excluded = 0;
		int bh, bss_load, conn_capab;
		cred = interworking_credentials_available(wpa_s, bss,
							  &excluded);
		if (!cred)
			continue;

		if (!wpa_bss_get_ie(bss, WLAN_EID_RSN)) {
			/*
			 * We currently support only HS 2.0 networks and those
			 * are required to use WPA2-Enterprise.
			 */
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Credential match with " MACSTR
				" but network does not use RSN",
				MAC2STR(bss->bssid));
			continue;
		}
		if (!excluded)
			count++;
		res = interworking_home_sp(wpa_s, bss->anqp ?
					   bss->anqp->domain_name : NULL);
		if (res > 0)
			type = "home";
		else if (res == 0)
			type = "roaming";
		else
			type = "unknown";
		bh = cred_below_min_backhaul(wpa_s, cred, bss);
		bss_load = cred_over_max_bss_load(wpa_s, cred, bss);
		conn_capab = cred_conn_capab_missing(wpa_s, cred, bss);
		wpa_msg(wpa_s, MSG_INFO, "%s" MACSTR " type=%s%s%s%s id=%d priority=%d sp_priority=%d",
			excluded ? INTERWORKING_BLACKLISTED : INTERWORKING_AP,
			MAC2STR(bss->bssid), type,
			bh ? " below_min_backhaul=1" : "",
			bss_load ? " over_max_bss_load=1" : "",
			conn_capab ? " conn_capab_missing=1" : "",
			cred->id, cred->priority, cred->sp_priority);
		if (excluded)
			continue;
		if (wpa_s->auto_select ||
		    (wpa_s->conf->auto_interworking &&
		     wpa_s->auto_network_select)) {
			if (bh || bss_load || conn_capab) {
				if (selected2_cred == NULL ||
				    cred_prio_cmp(cred, selected2_cred) > 0) {
					wpa_printf(MSG_DEBUG, "Interworking: Mark as selected2");
					selected2 = bss;
					selected2_cred = cred;
				}
				if (res > 0 &&
				    (selected2_home_cred == NULL ||
				     cred_prio_cmp(cred, selected2_home_cred) >
				     0)) {
					wpa_printf(MSG_DEBUG, "Interworking: Mark as selected2_home");
					selected2_home = bss;
					selected2_home_cred = cred;
				}
			} else {
				if (selected_cred == NULL ||
				    cred_prio_cmp(cred, selected_cred) > 0) {
					wpa_printf(MSG_DEBUG, "Interworking: Mark as selected");
					selected = bss;
					selected_cred = cred;
				}
				if (res > 0 &&
				    (selected_home_cred == NULL ||
				     cred_prio_cmp(cred, selected_home_cred) >
				     0)) {
					wpa_printf(MSG_DEBUG, "Interworking: Mark as selected_home");
					selected_home = bss;
					selected_home_cred = cred;
				}
			}
		}
	}

	if (selected_home && selected_home != selected &&
	    selected_home_cred &&
	    (selected_cred == NULL ||
	     cred_prio_cmp(selected_home_cred, selected_cred) >= 0)) {
		/* Prefer network operated by the Home SP */
		wpa_printf(MSG_DEBUG, "Interworking: Overrided selected with selected_home");
		selected = selected_home;
		selected_cred = selected_home_cred;
	}

	if (!selected) {
		if (selected2_home) {
			wpa_printf(MSG_DEBUG, "Interworking: Use home BSS with BW limit mismatch since no other network could be selected");
			selected = selected2_home;
			selected_cred = selected2_home_cred;
		} else if (selected2) {
			wpa_printf(MSG_DEBUG, "Interworking: Use visited BSS with BW limit mismatch since no other network could be selected");
			selected = selected2;
			selected_cred = selected2_cred;
		}
	}

	if (count == 0) {
		/*
		 * No matching network was found based on configured
		 * credentials. Check whether any of the enabled network blocks
		 * have matching APs.
		 */
		if (interworking_find_network_match(wpa_s)) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Possible BSS match for enabled network configurations");
			if (wpa_s->auto_select) {
				interworking_reconnect(wpa_s);
				return;
			}
		}

		if (wpa_s->auto_network_select) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Continue scanning after ANQP fetch");
			wpa_supplicant_req_scan(wpa_s, wpa_s->scan_interval,
						0);
			return;
		}

		wpa_msg(wpa_s, MSG_INFO, INTERWORKING_NO_MATCH "No network "
			"with matching credentials found");
		if (wpa_s->wpa_state == WPA_SCANNING)
			wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
	}

	if (selected) {
		wpa_printf(MSG_DEBUG, "Interworking: Selected " MACSTR,
			   MAC2STR(selected->bssid));
		selected = pick_best_roaming_partner(wpa_s, selected,
						     selected_cred);
		wpa_printf(MSG_DEBUG, "Interworking: Selected " MACSTR
			   " (after best roaming partner selection)",
			   MAC2STR(selected->bssid));
		wpa_msg(wpa_s, MSG_INFO, INTERWORKING_SELECTED MACSTR,
			MAC2STR(selected->bssid));
		interworking_connect(wpa_s, selected, 0);
	} else if (wpa_s->wpa_state == WPA_SCANNING)
		wpa_supplicant_set_state(wpa_s, WPA_DISCONNECTED);
}


static struct wpa_bss_anqp *
interworking_match_anqp_info(struct wpa_supplicant *wpa_s, struct wpa_bss *bss)
{
	struct wpa_bss *other;

	if (is_zero_ether_addr(bss->hessid))
		return NULL; /* Cannot be in the same homegenous ESS */

	dl_list_for_each(other, &wpa_s->bss, struct wpa_bss, list) {
		if (other == bss)
			continue;
		if (other->anqp == NULL)
			continue;
		if (other->anqp->roaming_consortium == NULL &&
		    other->anqp->nai_realm == NULL &&
		    other->anqp->anqp_3gpp == NULL &&
		    other->anqp->domain_name == NULL)
			continue;
		if (!(other->flags & WPA_BSS_ANQP_FETCH_TRIED))
			continue;
		if (os_memcmp(bss->hessid, other->hessid, ETH_ALEN) != 0)
			continue;
		if (bss->ssid_len != other->ssid_len ||
		    os_memcmp(bss->ssid, other->ssid, bss->ssid_len) != 0)
			continue;

		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Share ANQP data with already fetched BSSID "
			MACSTR " and " MACSTR,
			MAC2STR(other->bssid), MAC2STR(bss->bssid));
		other->anqp->users++;
		return other->anqp;
	}

	return NULL;
}


static void interworking_next_anqp_fetch(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;
	int found = 0;
	const u8 *ie;

	wpa_printf(MSG_DEBUG, "Interworking: next_anqp_fetch - "
		   "fetch_anqp_in_progress=%d fetch_osu_icon_in_progress=%d",
		   wpa_s->fetch_anqp_in_progress,
		   wpa_s->fetch_osu_icon_in_progress);

	if (eloop_terminated() || !wpa_s->fetch_anqp_in_progress) {
		wpa_printf(MSG_DEBUG, "Interworking: Stop next-ANQP-fetch");
		return;
	}

#ifdef CONFIG_HS20
	if (wpa_s->fetch_osu_icon_in_progress) {
		wpa_printf(MSG_DEBUG, "Interworking: Next icon (in progress)");
		hs20_next_osu_icon(wpa_s);
		return;
	}
#endif /* CONFIG_HS20 */

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list) {
		if (!(bss->caps & IEEE80211_CAP_ESS))
			continue;
		ie = wpa_bss_get_ie(bss, WLAN_EID_EXT_CAPAB);
		if (ie == NULL || ie[1] < 4 || !(ie[5] & 0x80))
			continue; /* AP does not support Interworking */
		if (disallowed_bssid(wpa_s, bss->bssid) ||
		    disallowed_ssid(wpa_s, bss->ssid, bss->ssid_len))
			continue; /* Disallowed BSS */

		if (!(bss->flags & WPA_BSS_ANQP_FETCH_TRIED)) {
			if (bss->anqp == NULL) {
				bss->anqp = interworking_match_anqp_info(wpa_s,
									 bss);
				if (bss->anqp) {
					/* Shared data already fetched */
					continue;
				}
				bss->anqp = wpa_bss_anqp_alloc();
				if (bss->anqp == NULL)
					break;
			}
			found++;
			bss->flags |= WPA_BSS_ANQP_FETCH_TRIED;
			wpa_msg(wpa_s, MSG_INFO, "Starting ANQP fetch for "
				MACSTR, MAC2STR(bss->bssid));
			interworking_anqp_send_req(wpa_s, bss);
			break;
		}
	}

	if (found == 0) {
#ifdef CONFIG_HS20
		if (wpa_s->fetch_osu_info) {
			if (wpa_s->num_prov_found == 0 &&
			    wpa_s->fetch_osu_waiting_scan &&
			    wpa_s->num_osu_scans < 3) {
				wpa_printf(MSG_DEBUG, "HS 2.0: No OSU providers seen - try to scan again");
				hs20_start_osu_scan(wpa_s);
				return;
			}
			wpa_printf(MSG_DEBUG, "Interworking: Next icon");
			hs20_osu_icon_fetch(wpa_s);
			return;
		}
#endif /* CONFIG_HS20 */
		wpa_msg(wpa_s, MSG_INFO, "ANQP fetch completed");
		wpa_s->fetch_anqp_in_progress = 0;
		if (wpa_s->network_select)
			interworking_select_network(wpa_s);
	}
}


void interworking_start_fetch_anqp(struct wpa_supplicant *wpa_s)
{
	struct wpa_bss *bss;

	dl_list_for_each(bss, &wpa_s->bss, struct wpa_bss, list)
		bss->flags &= ~WPA_BSS_ANQP_FETCH_TRIED;

	wpa_s->fetch_anqp_in_progress = 1;

	/*
	 * Start actual ANQP operation from eloop call to make sure the loop
	 * does not end up using excessive recursion.
	 */
	eloop_register_timeout(0, 0, interworking_continue_anqp, wpa_s, NULL);
}


int interworking_fetch_anqp(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->fetch_anqp_in_progress || wpa_s->network_select)
		return 0;

	wpa_s->network_select = 0;
	wpa_s->fetch_all_anqp = 1;
	wpa_s->fetch_osu_info = 0;

	interworking_start_fetch_anqp(wpa_s);

	return 0;
}


void interworking_stop_fetch_anqp(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s->fetch_anqp_in_progress)
		return;

	wpa_s->fetch_anqp_in_progress = 0;
}


int anqp_send_req(struct wpa_supplicant *wpa_s, const u8 *dst,
		  u16 info_ids[], size_t num_ids, u32 subtypes,
		  u32 mbo_subtypes)
{
	struct wpabuf *buf;
	struct wpabuf *extra_buf = NULL;
	int ret = 0;
	int freq;
	struct wpa_bss *bss;
	int res;

	bss = wpa_bss_get_bssid(wpa_s, dst);
	if (!bss) {
		wpa_printf(MSG_WARNING,
			   "ANQP: Cannot send query to unknown BSS "
			   MACSTR, MAC2STR(dst));
		return -1;
	}

	wpa_bss_anqp_unshare_alloc(bss);
	freq = bss->freq;

	wpa_msg(wpa_s, MSG_DEBUG,
		"ANQP: Query Request to " MACSTR " for %u id(s)",
		MAC2STR(dst), (unsigned int) num_ids);

#ifdef CONFIG_HS20
	if (subtypes != 0) {
		extra_buf = wpabuf_alloc(100);
		if (extra_buf == NULL)
			return -1;
		hs20_put_anqp_req(subtypes, NULL, 0, extra_buf);
	}
#endif /* CONFIG_HS20 */

#ifdef CONFIG_MBO
	if (mbo_subtypes) {
		struct wpabuf *mbo;

		mbo = mbo_build_anqp_buf(wpa_s, bss, mbo_subtypes);
		if (mbo) {
			if (wpabuf_resize(&extra_buf, wpabuf_len(mbo))) {
				wpabuf_free(extra_buf);
				wpabuf_free(mbo);
				return -1;
			}
			wpabuf_put_buf(extra_buf, mbo);
			wpabuf_free(mbo);
		}
	}
#endif /* CONFIG_MBO */

	buf = anqp_build_req(info_ids, num_ids, extra_buf);
	wpabuf_free(extra_buf);
	if (buf == NULL)
		return -1;

	res = gas_query_req(wpa_s->gas, dst, freq, 0, buf, anqp_resp_cb, wpa_s);
	if (res < 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "ANQP: Failed to send Query Request");
		wpabuf_free(buf);
		ret = -1;
	} else {
		wpa_msg(wpa_s, MSG_DEBUG,
			"ANQP: Query started with dialog token %u", res);
	}

	return ret;
}


static void anqp_add_extra(struct wpa_supplicant *wpa_s,
			   struct wpa_bss_anqp *anqp, u16 info_id,
			   const u8 *data, size_t slen)
{
	struct wpa_bss_anqp_elem *tmp, *elem = NULL;

	if (!anqp)
		return;

	dl_list_for_each(tmp, &anqp->anqp_elems, struct wpa_bss_anqp_elem,
			 list) {
		if (tmp->infoid == info_id) {
			elem = tmp;
			break;
		}
	}

	if (!elem) {
		elem = os_zalloc(sizeof(*elem));
		if (!elem)
			return;
		elem->infoid = info_id;
		dl_list_add(&anqp->anqp_elems, &elem->list);
	} else {
		wpabuf_free(elem->payload);
	}

	elem->payload = wpabuf_alloc_copy(data, slen);
	if (!elem->payload) {
		dl_list_del(&elem->list);
		os_free(elem);
	}
}


static void interworking_parse_venue_url(struct wpa_supplicant *wpa_s,
					 const u8 *data, size_t len)
{
	const u8 *pos = data, *end = data + len;
	char url[255];

	while (end - pos >= 2) {
		u8 slen, num;

		slen = *pos++;
		if (slen < 1 || slen > end - pos) {
			wpa_printf(MSG_DEBUG,
				   "ANQP: Truncated Venue URL Duple field");
			return;
		}

		num = *pos++;
		os_memcpy(url, pos, slen - 1);
		url[slen - 1] = '\0';
		wpa_msg(wpa_s, MSG_INFO, RX_VENUE_URL "%u %s", num, url);
		pos += slen - 1;
	}
}


static void interworking_parse_rx_anqp_resp(struct wpa_supplicant *wpa_s,
					    struct wpa_bss *bss, const u8 *sa,
					    u16 info_id,
					    const u8 *data, size_t slen,
					    u8 dialog_token)
{
	const u8 *pos = data;
	struct wpa_bss_anqp *anqp = NULL;
	u8 type;

	if (bss)
		anqp = bss->anqp;

	switch (info_id) {
	case ANQP_CAPABILITY_LIST:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" ANQP Capability list", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: Capability list",
				  pos, slen);
		if (anqp) {
			wpabuf_free(anqp->capability_list);
			anqp->capability_list = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_VENUE_NAME:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" Venue Name", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: Venue Name", pos, slen);
		if (anqp) {
			wpabuf_free(anqp->venue_name);
			anqp->venue_name = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_NETWORK_AUTH_TYPE:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" Network Authentication Type information",
			MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: Network Authentication "
				  "Type", pos, slen);
		if (anqp) {
			wpabuf_free(anqp->network_auth_type);
			anqp->network_auth_type = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_ROAMING_CONSORTIUM:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" Roaming Consortium list", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: Roaming Consortium",
				  pos, slen);
		if (anqp) {
			wpabuf_free(anqp->roaming_consortium);
			anqp->roaming_consortium = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_IP_ADDR_TYPE_AVAILABILITY:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" IP Address Type Availability information",
			MAC2STR(sa));
		wpa_hexdump(MSG_MSGDUMP, "ANQP: IP Address Availability",
			    pos, slen);
		if (anqp) {
			wpabuf_free(anqp->ip_addr_type_availability);
			anqp->ip_addr_type_availability =
				wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_NAI_REALM:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" NAI Realm list", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: NAI Realm", pos, slen);
		if (anqp) {
			wpabuf_free(anqp->nai_realm);
			anqp->nai_realm = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_3GPP_CELLULAR_NETWORK:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" 3GPP Cellular Network information", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_DEBUG, "ANQP: 3GPP Cellular Network",
				  pos, slen);
		if (anqp) {
			wpabuf_free(anqp->anqp_3gpp);
			anqp->anqp_3gpp = wpabuf_alloc_copy(pos, slen);
		}
		break;
	case ANQP_DOMAIN_NAME:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" Domain Name list", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_MSGDUMP, "ANQP: Domain Name", pos, slen);
		if (anqp) {
			wpabuf_free(anqp->domain_name);
			anqp->domain_name = wpabuf_alloc_copy(pos, slen);
		}
		break;
#ifdef CONFIG_FILS
	case ANQP_FILS_REALM_INFO:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR
			" FILS Realm Information", MAC2STR(sa));
		wpa_hexdump_ascii(MSG_MSGDUMP, "ANQP: FILS Realm Information",
			pos, slen);
		if (anqp) {
			wpabuf_free(anqp->fils_realm_info);
			anqp->fils_realm_info = wpabuf_alloc_copy(pos, slen);
		}
		break;
#endif /* CONFIG_FILS */
	case ANQP_VENUE_URL:
		wpa_msg(wpa_s, MSG_INFO, RX_ANQP MACSTR " Venue URL",
			MAC2STR(sa));
		anqp_add_extra(wpa_s, anqp, info_id, pos, slen);

		if (!wpa_sm_pmf_enabled(wpa_s->wpa)) {
			wpa_printf(MSG_DEBUG,
				   "ANQP: Ignore Venue URL since PMF was not enabled");
			break;
		}
		interworking_parse_venue_url(wpa_s, pos, slen);
		break;
	case ANQP_VENDOR_SPECIFIC:
		if (slen < 3)
			return;

		switch (WPA_GET_BE24(pos)) {
		case OUI_WFA:
			pos += 3;
			slen -= 3;

			if (slen < 1)
				return;
			type = *pos++;
			slen--;

			switch (type) {
#ifdef CONFIG_HS20
			case HS20_ANQP_OUI_TYPE:
				hs20_parse_rx_hs20_anqp_resp(wpa_s, bss, sa,
							     pos, slen,
							     dialog_token);
				break;
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
			case MBO_ANQP_OUI_TYPE:
				mbo_parse_rx_anqp_resp(wpa_s, bss, sa,
						       pos, slen);
				break;
#endif /* CONFIG_MBO */
			default:
				wpa_msg(wpa_s, MSG_DEBUG,
					"ANQP: Unsupported ANQP vendor type %u",
					type);
				break;
			}
			break;
		default:
			wpa_msg(wpa_s, MSG_DEBUG,
				"Interworking: Unsupported vendor-specific ANQP OUI %06x",
				WPA_GET_BE24(pos));
			return;
		}
		break;
	default:
		wpa_msg(wpa_s, MSG_DEBUG,
			"Interworking: Unsupported ANQP Info ID %u", info_id);
		anqp_add_extra(wpa_s, anqp, info_id, data, slen);
		break;
	}
}


void anqp_resp_cb(void *ctx, const u8 *dst, u8 dialog_token,
		  enum gas_query_result result,
		  const struct wpabuf *adv_proto,
		  const struct wpabuf *resp, u16 status_code)
{
	struct wpa_supplicant *wpa_s = ctx;
	const u8 *pos;
	const u8 *end;
	u16 info_id;
	u16 slen;
	struct wpa_bss *bss = NULL, *tmp;
	const char *anqp_result = "SUCCESS";

	wpa_printf(MSG_DEBUG, "Interworking: anqp_resp_cb dst=" MACSTR
		   " dialog_token=%u result=%d status_code=%u",
		   MAC2STR(dst), dialog_token, result, status_code);
	if (result != GAS_QUERY_SUCCESS) {
#ifdef CONFIG_HS20
		if (wpa_s->fetch_osu_icon_in_progress)
			hs20_icon_fetch_failed(wpa_s);
#endif /* CONFIG_HS20 */
		anqp_result = "FAILURE";
		goto out;
	}

	pos = wpabuf_head(adv_proto);
	if (wpabuf_len(adv_proto) < 4 || pos[0] != WLAN_EID_ADV_PROTO ||
	    pos[1] < 2 || pos[3] != ACCESS_NETWORK_QUERY_PROTOCOL) {
		wpa_msg(wpa_s, MSG_DEBUG,
			"ANQP: Unexpected Advertisement Protocol in response");
#ifdef CONFIG_HS20
		if (wpa_s->fetch_osu_icon_in_progress)
			hs20_icon_fetch_failed(wpa_s);
#endif /* CONFIG_HS20 */
		anqp_result = "INVALID_FRAME";
		goto out;
	}

	/*
	 * If possible, select the BSS entry based on which BSS entry was used
	 * for the request. This can help in cases where multiple BSS entries
	 * may exist for the same AP.
	 */
	dl_list_for_each_reverse(tmp, &wpa_s->bss, struct wpa_bss, list) {
		if (tmp == wpa_s->interworking_gas_bss &&
		    os_memcmp(tmp->bssid, dst, ETH_ALEN) == 0) {
			bss = tmp;
			break;
		}
	}
	if (bss == NULL)
		bss = wpa_bss_get_bssid(wpa_s, dst);

	pos = wpabuf_head(resp);
	end = pos + wpabuf_len(resp);

	while (pos < end) {
		unsigned int left = end - pos;

		if (left < 4) {
			wpa_msg(wpa_s, MSG_DEBUG, "ANQP: Invalid element");
			anqp_result = "INVALID_FRAME";
			goto out_parse_done;
		}
		info_id = WPA_GET_LE16(pos);
		pos += 2;
		slen = WPA_GET_LE16(pos);
		pos += 2;
		left -= 4;
		if (left < slen) {
			wpa_msg(wpa_s, MSG_DEBUG,
				"ANQP: Invalid element length for Info ID %u",
				info_id);
			anqp_result = "INVALID_FRAME";
			goto out_parse_done;
		}
		interworking_parse_rx_anqp_resp(wpa_s, bss, dst, info_id, pos,
						slen, dialog_token);
		pos += slen;
	}

out_parse_done:
#ifdef CONFIG_HS20
	hs20_notify_parse_done(wpa_s);
#endif /* CONFIG_HS20 */
out:
	wpa_msg(wpa_s, MSG_INFO, ANQP_QUERY_DONE "addr=" MACSTR " result=%s",
		MAC2STR(dst), anqp_result);
}


static void interworking_scan_res_handler(struct wpa_supplicant *wpa_s,
					  struct wpa_scan_results *scan_res)
{
	wpa_msg(wpa_s, MSG_DEBUG,
		"Interworking: Scan results available - start ANQP fetch");
	interworking_start_fetch_anqp(wpa_s);
}


int interworking_select(struct wpa_supplicant *wpa_s, int auto_select,
			int *freqs)
{
	interworking_stop_fetch_anqp(wpa_s);
	wpa_s->network_select = 1;
	wpa_s->auto_network_select = 0;
	wpa_s->auto_select = !!auto_select;
	wpa_s->fetch_all_anqp = 0;
	wpa_s->fetch_osu_info = 0;
	wpa_msg(wpa_s, MSG_DEBUG,
		"Interworking: Start scan for network selection");
	wpa_s->scan_res_handler = interworking_scan_res_handler;
	wpa_s->normal_scans = 0;
	wpa_s->scan_req = MANUAL_SCAN_REQ;
	os_free(wpa_s->manual_scan_freqs);
	wpa_s->manual_scan_freqs = freqs;
	wpa_s->after_wps = 0;
	wpa_s->known_wps_freq = 0;
	wpa_supplicant_req_scan(wpa_s, 0, 0);

	return 0;
}


static void gas_resp_cb(void *ctx, const u8 *addr, u8 dialog_token,
			enum gas_query_result result,
			const struct wpabuf *adv_proto,
			const struct wpabuf *resp, u16 status_code)
{
	struct wpa_supplicant *wpa_s = ctx;
	struct wpabuf *n;

	wpa_msg(wpa_s, MSG_INFO, GAS_RESPONSE_INFO "addr=" MACSTR
		" dialog_token=%d status_code=%d resp_len=%d",
		MAC2STR(addr), dialog_token, status_code,
		resp ? (int) wpabuf_len(resp) : -1);
	if (!resp)
		return;

	n = wpabuf_dup(resp);
	if (n == NULL)
		return;
	wpabuf_free(wpa_s->prev_gas_resp);
	wpa_s->prev_gas_resp = wpa_s->last_gas_resp;
	os_memcpy(wpa_s->prev_gas_addr, wpa_s->last_gas_addr, ETH_ALEN);
	wpa_s->prev_gas_dialog_token = wpa_s->last_gas_dialog_token;
	wpa_s->last_gas_resp = n;
	os_memcpy(wpa_s->last_gas_addr, addr, ETH_ALEN);
	wpa_s->last_gas_dialog_token = dialog_token;
}


int gas_send_request(struct wpa_supplicant *wpa_s, const u8 *dst,
		     const struct wpabuf *adv_proto,
		     const struct wpabuf *query)
{
	struct wpabuf *buf;
	int ret = 0;
	int freq;
	struct wpa_bss *bss;
	int res;
	size_t len;
	u8 query_resp_len_limit = 0;

	freq = wpa_s->assoc_freq;
	bss = wpa_bss_get_bssid(wpa_s, dst);
	if (bss)
		freq = bss->freq;
	if (freq <= 0)
		return -1;

	wpa_msg(wpa_s, MSG_DEBUG, "GAS request to " MACSTR " (freq %d MHz)",
		MAC2STR(dst), freq);
	wpa_hexdump_buf(MSG_DEBUG, "Advertisement Protocol ID", adv_proto);
	wpa_hexdump_buf(MSG_DEBUG, "GAS Query", query);

	len = 3 + wpabuf_len(adv_proto) + 2;
	if (query)
		len += wpabuf_len(query);
	buf = gas_build_initial_req(0, len);
	if (buf == NULL)
		return -1;

	/* Advertisement Protocol IE */
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 1 + wpabuf_len(adv_proto)); /* Length */
	wpabuf_put_u8(buf, query_resp_len_limit & 0x7f);
	wpabuf_put_buf(buf, adv_proto);

	/* GAS Query */
	if (query) {
		wpabuf_put_le16(buf, wpabuf_len(query));
		wpabuf_put_buf(buf, query);
	} else
		wpabuf_put_le16(buf, 0);

	res = gas_query_req(wpa_s->gas, dst, freq, 0, buf, gas_resp_cb, wpa_s);
	if (res < 0) {
		wpa_msg(wpa_s, MSG_DEBUG, "GAS: Failed to send Query Request");
		wpabuf_free(buf);
		ret = -1;
	} else
		wpa_msg(wpa_s, MSG_DEBUG,
			"GAS: Query started with dialog token %u", res);

	return ret;
}
