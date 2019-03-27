/*
 * Generic advertisement service (GAS) server
 * Copyright (c) 2011-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/gas.h"
#include "common/wpa_ctrl.h"
#include "utils/eloop.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "dpp_hostapd.h"
#include "sta_info.h"
#include "gas_serv.h"


#ifdef CONFIG_DPP
static void gas_serv_write_dpp_adv_proto(struct wpabuf *buf)
{
	wpabuf_put_u8(buf, WLAN_EID_ADV_PROTO);
	wpabuf_put_u8(buf, 8); /* Length */
	wpabuf_put_u8(buf, 0x7f);
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	wpabuf_put_u8(buf, 5);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, DPP_OUI_TYPE);
	wpabuf_put_u8(buf, 0x01);
}
#endif /* CONFIG_DPP */


static void convert_to_protected_dual(struct wpabuf *msg)
{
	u8 *categ = wpabuf_mhead_u8(msg);
	*categ = WLAN_ACTION_PROTECTED_DUAL;
}


static struct gas_dialog_info *
gas_dialog_create(struct hostapd_data *hapd, const u8 *addr, u8 dialog_token)
{
	struct sta_info *sta;
	struct gas_dialog_info *dia = NULL;
	int i, j;

	sta = ap_get_sta(hapd, addr);
	if (!sta) {
		/*
		 * We need a STA entry to be able to maintain state for
		 * the GAS query.
		 */
		wpa_printf(MSG_DEBUG, "ANQP: Add a temporary STA entry for "
			   "GAS query");
		sta = ap_sta_add(hapd, addr);
		if (!sta) {
			wpa_printf(MSG_DEBUG, "Failed to add STA " MACSTR
				   " for GAS query", MAC2STR(addr));
			return NULL;
		}
		sta->flags |= WLAN_STA_GAS;
		/*
		 * The default inactivity is 300 seconds. We don't need
		 * it to be that long. Use five second timeout and increase this
		 * with the comeback_delay for testing cases.
		 */
		ap_sta_session_timeout(hapd, sta,
				       hapd->conf->gas_comeback_delay / 1024 +
				       5);
	} else {
		ap_sta_replenish_timeout(hapd, sta, 5);
	}

	if (sta->gas_dialog == NULL) {
		sta->gas_dialog = os_calloc(GAS_DIALOG_MAX,
					    sizeof(struct gas_dialog_info));
		if (sta->gas_dialog == NULL)
			return NULL;
	}

	for (i = sta->gas_dialog_next, j = 0; j < GAS_DIALOG_MAX; i++, j++) {
		if (i == GAS_DIALOG_MAX)
			i = 0;
		if (sta->gas_dialog[i].valid)
			continue;
		dia = &sta->gas_dialog[i];
		dia->valid = 1;
		dia->dialog_token = dialog_token;
		sta->gas_dialog_next = (++i == GAS_DIALOG_MAX) ? 0 : i;
		return dia;
	}

	wpa_msg(hapd->msg_ctx, MSG_ERROR, "ANQP: Could not create dialog for "
		MACSTR " dialog_token %u. Consider increasing "
		"GAS_DIALOG_MAX.", MAC2STR(addr), dialog_token);

	return NULL;
}


struct gas_dialog_info *
gas_serv_dialog_find(struct hostapd_data *hapd, const u8 *addr,
		     u8 dialog_token)
{
	struct sta_info *sta;
	int i;

	sta = ap_get_sta(hapd, addr);
	if (!sta) {
		wpa_printf(MSG_DEBUG, "ANQP: could not find STA " MACSTR,
			   MAC2STR(addr));
		return NULL;
	}
	for (i = 0; sta->gas_dialog && i < GAS_DIALOG_MAX; i++) {
		if (sta->gas_dialog[i].dialog_token != dialog_token ||
		    !sta->gas_dialog[i].valid)
			continue;
		ap_sta_replenish_timeout(hapd, sta, 5);
		return &sta->gas_dialog[i];
	}
	wpa_printf(MSG_DEBUG, "ANQP: Could not find dialog for "
		   MACSTR " dialog_token %u", MAC2STR(addr), dialog_token);
	return NULL;
}


void gas_serv_dialog_clear(struct gas_dialog_info *dia)
{
	wpabuf_free(dia->sd_resp);
	os_memset(dia, 0, sizeof(*dia));
}


static void gas_serv_free_dialogs(struct hostapd_data *hapd,
				  const u8 *sta_addr)
{
	struct sta_info *sta;
	int i;

	sta = ap_get_sta(hapd, sta_addr);
	if (sta == NULL || sta->gas_dialog == NULL)
		return;

	for (i = 0; i < GAS_DIALOG_MAX; i++) {
		if (sta->gas_dialog[i].valid)
			return;
	}

	os_free(sta->gas_dialog);
	sta->gas_dialog = NULL;
}


#ifdef CONFIG_HS20
static void anqp_add_hs_capab_list(struct hostapd_data *hapd,
				   struct wpabuf *buf)
{
	u8 *len;

	len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
	wpabuf_put_u8(buf, HS20_STYPE_CAPABILITY_LIST);
	wpabuf_put_u8(buf, 0); /* Reserved */
	wpabuf_put_u8(buf, HS20_STYPE_CAPABILITY_LIST);
	if (hapd->conf->hs20_oper_friendly_name)
		wpabuf_put_u8(buf, HS20_STYPE_OPERATOR_FRIENDLY_NAME);
	if (hapd->conf->hs20_wan_metrics)
		wpabuf_put_u8(buf, HS20_STYPE_WAN_METRICS);
	if (hapd->conf->hs20_connection_capability)
		wpabuf_put_u8(buf, HS20_STYPE_CONNECTION_CAPABILITY);
	if (hapd->conf->nai_realm_data)
		wpabuf_put_u8(buf, HS20_STYPE_NAI_HOME_REALM_QUERY);
	if (hapd->conf->hs20_operating_class)
		wpabuf_put_u8(buf, HS20_STYPE_OPERATING_CLASS);
	if (hapd->conf->hs20_osu_providers_count)
		wpabuf_put_u8(buf, HS20_STYPE_OSU_PROVIDERS_LIST);
	if (hapd->conf->hs20_osu_providers_nai_count)
		wpabuf_put_u8(buf, HS20_STYPE_OSU_PROVIDERS_NAI_LIST);
	if (hapd->conf->hs20_icons_count)
		wpabuf_put_u8(buf, HS20_STYPE_ICON_REQUEST);
	if (hapd->conf->hs20_operator_icon_count)
		wpabuf_put_u8(buf, HS20_STYPE_OPERATOR_ICON_METADATA);
	gas_anqp_set_element_len(buf, len);
}
#endif /* CONFIG_HS20 */


static struct anqp_element * get_anqp_elem(struct hostapd_data *hapd,
					   u16 infoid)
{
	struct anqp_element *elem;

	dl_list_for_each(elem, &hapd->conf->anqp_elem, struct anqp_element,
			 list) {
		if (elem->infoid == infoid)
			return elem;
	}

	return NULL;
}


static void anqp_add_elem(struct hostapd_data *hapd, struct wpabuf *buf,
			  u16 infoid)
{
	struct anqp_element *elem;

	elem = get_anqp_elem(hapd, infoid);
	if (!elem)
		return;
	if (wpabuf_tailroom(buf) < 2 + 2 + wpabuf_len(elem->payload)) {
		wpa_printf(MSG_DEBUG, "ANQP: No room for InfoID %u payload",
			   infoid);
		return;
	}

	wpabuf_put_le16(buf, infoid);
	wpabuf_put_le16(buf, wpabuf_len(elem->payload));
	wpabuf_put_buf(buf, elem->payload);
}


static int anqp_add_override(struct hostapd_data *hapd, struct wpabuf *buf,
			     u16 infoid)
{
	if (get_anqp_elem(hapd, infoid)) {
		anqp_add_elem(hapd, buf, infoid);
		return 1;
	}

	return 0;
}


static void anqp_add_capab_list(struct hostapd_data *hapd,
				struct wpabuf *buf)
{
	u8 *len;
	u16 id;

	if (anqp_add_override(hapd, buf, ANQP_CAPABILITY_LIST))
		return;

	len = gas_anqp_add_element(buf, ANQP_CAPABILITY_LIST);
	wpabuf_put_le16(buf, ANQP_CAPABILITY_LIST);
	if (hapd->conf->venue_name || get_anqp_elem(hapd, ANQP_VENUE_NAME))
		wpabuf_put_le16(buf, ANQP_VENUE_NAME);
	if (get_anqp_elem(hapd, ANQP_EMERGENCY_CALL_NUMBER))
		wpabuf_put_le16(buf, ANQP_EMERGENCY_CALL_NUMBER);
	if (hapd->conf->network_auth_type ||
	    get_anqp_elem(hapd, ANQP_NETWORK_AUTH_TYPE))
		wpabuf_put_le16(buf, ANQP_NETWORK_AUTH_TYPE);
	if (hapd->conf->roaming_consortium ||
	    get_anqp_elem(hapd, ANQP_ROAMING_CONSORTIUM))
		wpabuf_put_le16(buf, ANQP_ROAMING_CONSORTIUM);
	if (hapd->conf->ipaddr_type_configured ||
	    get_anqp_elem(hapd, ANQP_IP_ADDR_TYPE_AVAILABILITY))
		wpabuf_put_le16(buf, ANQP_IP_ADDR_TYPE_AVAILABILITY);
	if (hapd->conf->nai_realm_data ||
	    get_anqp_elem(hapd, ANQP_NAI_REALM))
		wpabuf_put_le16(buf, ANQP_NAI_REALM);
	if (hapd->conf->anqp_3gpp_cell_net ||
	    get_anqp_elem(hapd, ANQP_3GPP_CELLULAR_NETWORK))
		wpabuf_put_le16(buf, ANQP_3GPP_CELLULAR_NETWORK);
	if (get_anqp_elem(hapd, ANQP_AP_GEOSPATIAL_LOCATION))
		wpabuf_put_le16(buf, ANQP_AP_GEOSPATIAL_LOCATION);
	if (get_anqp_elem(hapd, ANQP_AP_CIVIC_LOCATION))
		wpabuf_put_le16(buf, ANQP_AP_CIVIC_LOCATION);
	if (get_anqp_elem(hapd, ANQP_AP_LOCATION_PUBLIC_URI))
		wpabuf_put_le16(buf, ANQP_AP_LOCATION_PUBLIC_URI);
	if (hapd->conf->domain_name || get_anqp_elem(hapd, ANQP_DOMAIN_NAME))
		wpabuf_put_le16(buf, ANQP_DOMAIN_NAME);
	if (get_anqp_elem(hapd, ANQP_EMERGENCY_ALERT_URI))
		wpabuf_put_le16(buf, ANQP_EMERGENCY_ALERT_URI);
	if (get_anqp_elem(hapd, ANQP_TDLS_CAPABILITY))
		wpabuf_put_le16(buf, ANQP_TDLS_CAPABILITY);
	if (get_anqp_elem(hapd, ANQP_EMERGENCY_NAI))
		wpabuf_put_le16(buf, ANQP_EMERGENCY_NAI);
	if (get_anqp_elem(hapd, ANQP_NEIGHBOR_REPORT))
		wpabuf_put_le16(buf, ANQP_NEIGHBOR_REPORT);
#ifdef CONFIG_FILS
	if (!dl_list_empty(&hapd->conf->fils_realms) ||
	    get_anqp_elem(hapd, ANQP_FILS_REALM_INFO))
		wpabuf_put_le16(buf, ANQP_FILS_REALM_INFO);
#endif /* CONFIG_FILS */
	if (get_anqp_elem(hapd, ANQP_CAG))
		wpabuf_put_le16(buf, ANQP_CAG);
	if (hapd->conf->venue_url || get_anqp_elem(hapd, ANQP_VENUE_URL))
		wpabuf_put_le16(buf, ANQP_VENUE_URL);
	if (get_anqp_elem(hapd, ANQP_ADVICE_OF_CHARGE))
		wpabuf_put_le16(buf, ANQP_ADVICE_OF_CHARGE);
	if (get_anqp_elem(hapd, ANQP_LOCAL_CONTENT))
		wpabuf_put_le16(buf, ANQP_LOCAL_CONTENT);
	for (id = 280; id < 300; id++) {
		if (get_anqp_elem(hapd, id))
			wpabuf_put_le16(buf, id);
	}
#ifdef CONFIG_HS20
	anqp_add_hs_capab_list(hapd, buf);
#endif /* CONFIG_HS20 */
	gas_anqp_set_element_len(buf, len);
}


static void anqp_add_venue_name(struct hostapd_data *hapd, struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_VENUE_NAME))
		return;

	if (hapd->conf->venue_name) {
		u8 *len;
		unsigned int i;
		len = gas_anqp_add_element(buf, ANQP_VENUE_NAME);
		wpabuf_put_u8(buf, hapd->conf->venue_group);
		wpabuf_put_u8(buf, hapd->conf->venue_type);
		for (i = 0; i < hapd->conf->venue_name_count; i++) {
			struct hostapd_lang_string *vn;
			vn = &hapd->conf->venue_name[i];
			wpabuf_put_u8(buf, 3 + vn->name_len);
			wpabuf_put_data(buf, vn->lang, 3);
			wpabuf_put_data(buf, vn->name, vn->name_len);
		}
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_venue_url(struct hostapd_data *hapd, struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_VENUE_URL))
		return;

	if (hapd->conf->venue_url) {
		u8 *len;
		unsigned int i;

		len = gas_anqp_add_element(buf, ANQP_VENUE_URL);
		for (i = 0; i < hapd->conf->venue_url_count; i++) {
			struct hostapd_venue_url *url;

			url = &hapd->conf->venue_url[i];
			wpabuf_put_u8(buf, 1 + url->url_len);
			wpabuf_put_u8(buf, url->venue_number);
			wpabuf_put_data(buf, url->url, url->url_len);
		}
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_network_auth_type(struct hostapd_data *hapd,
				       struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_NETWORK_AUTH_TYPE))
		return;

	if (hapd->conf->network_auth_type) {
		wpabuf_put_le16(buf, ANQP_NETWORK_AUTH_TYPE);
		wpabuf_put_le16(buf, hapd->conf->network_auth_type_len);
		wpabuf_put_data(buf, hapd->conf->network_auth_type,
				hapd->conf->network_auth_type_len);
	}
}


static void anqp_add_roaming_consortium(struct hostapd_data *hapd,
					struct wpabuf *buf)
{
	unsigned int i;
	u8 *len;

	if (anqp_add_override(hapd, buf, ANQP_ROAMING_CONSORTIUM))
		return;

	len = gas_anqp_add_element(buf, ANQP_ROAMING_CONSORTIUM);
	for (i = 0; i < hapd->conf->roaming_consortium_count; i++) {
		struct hostapd_roaming_consortium *rc;
		rc = &hapd->conf->roaming_consortium[i];
		wpabuf_put_u8(buf, rc->len);
		wpabuf_put_data(buf, rc->oi, rc->len);
	}
	gas_anqp_set_element_len(buf, len);
}


static void anqp_add_ip_addr_type_availability(struct hostapd_data *hapd,
					       struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_IP_ADDR_TYPE_AVAILABILITY))
		return;

	if (hapd->conf->ipaddr_type_configured) {
		wpabuf_put_le16(buf, ANQP_IP_ADDR_TYPE_AVAILABILITY);
		wpabuf_put_le16(buf, 1);
		wpabuf_put_u8(buf, hapd->conf->ipaddr_type_availability);
	}
}


static void anqp_add_nai_realm_eap(struct wpabuf *buf,
				   struct hostapd_nai_realm_data *realm)
{
	unsigned int i, j;

	wpabuf_put_u8(buf, realm->eap_method_count);

	for (i = 0; i < realm->eap_method_count; i++) {
		struct hostapd_nai_realm_eap *eap = &realm->eap_method[i];
		wpabuf_put_u8(buf, 2 + (3 * eap->num_auths));
		wpabuf_put_u8(buf, eap->eap_method);
		wpabuf_put_u8(buf, eap->num_auths);
		for (j = 0; j < eap->num_auths; j++) {
			wpabuf_put_u8(buf, eap->auth_id[j]);
			wpabuf_put_u8(buf, 1);
			wpabuf_put_u8(buf, eap->auth_val[j]);
		}
	}
}


static void anqp_add_nai_realm_data(struct wpabuf *buf,
				    struct hostapd_nai_realm_data *realm,
				    unsigned int realm_idx)
{
	u8 *realm_data_len;

	wpa_printf(MSG_DEBUG, "realm=%s, len=%d", realm->realm[realm_idx],
		   (int) os_strlen(realm->realm[realm_idx]));
	realm_data_len = wpabuf_put(buf, 2);
	wpabuf_put_u8(buf, realm->encoding);
	wpabuf_put_u8(buf, os_strlen(realm->realm[realm_idx]));
	wpabuf_put_str(buf, realm->realm[realm_idx]);
	anqp_add_nai_realm_eap(buf, realm);
	gas_anqp_set_element_len(buf, realm_data_len);
}


static int hs20_add_nai_home_realm_matches(struct hostapd_data *hapd,
					   struct wpabuf *buf,
					   const u8 *home_realm,
					   size_t home_realm_len)
{
	unsigned int i, j, k;
	u8 num_realms, num_matching = 0, encoding, realm_len, *realm_list_len;
	struct hostapd_nai_realm_data *realm;
	const u8 *pos, *realm_name, *end;
	struct {
		unsigned int realm_data_idx;
		unsigned int realm_idx;
	} matches[10];

	pos = home_realm;
	end = pos + home_realm_len;
	if (end - pos < 1) {
		wpa_hexdump(MSG_DEBUG, "Too short NAI Home Realm Query",
			    home_realm, home_realm_len);
		return -1;
	}
	num_realms = *pos++;

	for (i = 0; i < num_realms && num_matching < 10; i++) {
		if (end - pos < 2) {
			wpa_hexdump(MSG_DEBUG,
				    "Truncated NAI Home Realm Query",
				    home_realm, home_realm_len);
			return -1;
		}
		encoding = *pos++;
		realm_len = *pos++;
		if (realm_len > end - pos) {
			wpa_hexdump(MSG_DEBUG,
				    "Truncated NAI Home Realm Query",
				    home_realm, home_realm_len);
			return -1;
		}
		realm_name = pos;
		for (j = 0; j < hapd->conf->nai_realm_count &&
			     num_matching < 10; j++) {
			const u8 *rpos, *rend;
			realm = &hapd->conf->nai_realm_data[j];
			if (encoding != realm->encoding)
				continue;

			rpos = realm_name;
			while (rpos < realm_name + realm_len &&
			       num_matching < 10) {
				for (rend = rpos;
				     rend < realm_name + realm_len; rend++) {
					if (*rend == ';')
						break;
				}
				for (k = 0; k < MAX_NAI_REALMS &&
					     realm->realm[k] &&
					     num_matching < 10; k++) {
					if ((int) os_strlen(realm->realm[k]) !=
					    rend - rpos ||
					    os_strncmp((char *) rpos,
						       realm->realm[k],
						       rend - rpos) != 0)
						continue;
					matches[num_matching].realm_data_idx =
						j;
					matches[num_matching].realm_idx = k;
					num_matching++;
				}
				rpos = rend + 1;
			}
		}
		pos += realm_len;
	}

	realm_list_len = gas_anqp_add_element(buf, ANQP_NAI_REALM);
	wpabuf_put_le16(buf, num_matching);

	/*
	 * There are two ways to format. 1. each realm in a NAI Realm Data unit
	 * 2. all realms that share the same EAP methods in a NAI Realm Data
	 * unit. The first format is likely to be bigger in size than the
	 * second, but may be easier to parse and process by the receiver.
	 */
	for (i = 0; i < num_matching; i++) {
		wpa_printf(MSG_DEBUG, "realm_idx %d, realm_data_idx %d",
			   matches[i].realm_data_idx, matches[i].realm_idx);
		realm = &hapd->conf->nai_realm_data[matches[i].realm_data_idx];
		anqp_add_nai_realm_data(buf, realm, matches[i].realm_idx);
	}
	gas_anqp_set_element_len(buf, realm_list_len);
	return 0;
}


static void anqp_add_nai_realm(struct hostapd_data *hapd, struct wpabuf *buf,
			       const u8 *home_realm, size_t home_realm_len,
			       int nai_realm, int nai_home_realm)
{
	if (nai_realm && !nai_home_realm &&
	    anqp_add_override(hapd, buf, ANQP_NAI_REALM))
		return;

	if (nai_realm && hapd->conf->nai_realm_data) {
		u8 *len;
		unsigned int i, j;
		len = gas_anqp_add_element(buf, ANQP_NAI_REALM);
		wpabuf_put_le16(buf, hapd->conf->nai_realm_count);
		for (i = 0; i < hapd->conf->nai_realm_count; i++) {
			u8 *realm_data_len, *realm_len;
			struct hostapd_nai_realm_data *realm;

			realm = &hapd->conf->nai_realm_data[i];
			realm_data_len = wpabuf_put(buf, 2);
			wpabuf_put_u8(buf, realm->encoding);
			realm_len = wpabuf_put(buf, 1);
			for (j = 0; realm->realm[j]; j++) {
				if (j > 0)
					wpabuf_put_u8(buf, ';');
				wpabuf_put_str(buf, realm->realm[j]);
			}
			*realm_len = (u8 *) wpabuf_put(buf, 0) - realm_len - 1;
			anqp_add_nai_realm_eap(buf, realm);
			gas_anqp_set_element_len(buf, realm_data_len);
		}
		gas_anqp_set_element_len(buf, len);
	} else if (nai_home_realm && hapd->conf->nai_realm_data && home_realm) {
		hs20_add_nai_home_realm_matches(hapd, buf, home_realm,
						home_realm_len);
	}
}


static void anqp_add_3gpp_cellular_network(struct hostapd_data *hapd,
					   struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_3GPP_CELLULAR_NETWORK))
		return;

	if (hapd->conf->anqp_3gpp_cell_net) {
		wpabuf_put_le16(buf, ANQP_3GPP_CELLULAR_NETWORK);
		wpabuf_put_le16(buf,
				hapd->conf->anqp_3gpp_cell_net_len);
		wpabuf_put_data(buf, hapd->conf->anqp_3gpp_cell_net,
				hapd->conf->anqp_3gpp_cell_net_len);
	}
}


static void anqp_add_domain_name(struct hostapd_data *hapd, struct wpabuf *buf)
{
	if (anqp_add_override(hapd, buf, ANQP_DOMAIN_NAME))
		return;

	if (hapd->conf->domain_name) {
		wpabuf_put_le16(buf, ANQP_DOMAIN_NAME);
		wpabuf_put_le16(buf, hapd->conf->domain_name_len);
		wpabuf_put_data(buf, hapd->conf->domain_name,
				hapd->conf->domain_name_len);
	}
}


#ifdef CONFIG_FILS
static void anqp_add_fils_realm_info(struct hostapd_data *hapd,
				     struct wpabuf *buf)
{
	size_t count;

	if (anqp_add_override(hapd, buf, ANQP_FILS_REALM_INFO))
		return;

	count = dl_list_len(&hapd->conf->fils_realms);
	if (count > 10000)
		count = 10000;
	if (count) {
		struct fils_realm *realm;

		wpabuf_put_le16(buf, ANQP_FILS_REALM_INFO);
		wpabuf_put_le16(buf, 2 * count);

		dl_list_for_each(realm, &hapd->conf->fils_realms,
				 struct fils_realm, list) {
			if (count == 0)
				break;
			wpabuf_put_data(buf, realm->hash, 2);
			count--;
		}
	}
}
#endif /* CONFIG_FILS */


#ifdef CONFIG_HS20

static void anqp_add_operator_friendly_name(struct hostapd_data *hapd,
					    struct wpabuf *buf)
{
	if (hapd->conf->hs20_oper_friendly_name) {
		u8 *len;
		unsigned int i;
		len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_OPERATOR_FRIENDLY_NAME);
		wpabuf_put_u8(buf, 0); /* Reserved */
		for (i = 0; i < hapd->conf->hs20_oper_friendly_name_count; i++)
		{
			struct hostapd_lang_string *vn;
			vn = &hapd->conf->hs20_oper_friendly_name[i];
			wpabuf_put_u8(buf, 3 + vn->name_len);
			wpabuf_put_data(buf, vn->lang, 3);
			wpabuf_put_data(buf, vn->name, vn->name_len);
		}
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_wan_metrics(struct hostapd_data *hapd,
				 struct wpabuf *buf)
{
	if (hapd->conf->hs20_wan_metrics) {
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_WAN_METRICS);
		wpabuf_put_u8(buf, 0); /* Reserved */
		wpabuf_put_data(buf, hapd->conf->hs20_wan_metrics, 13);
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_connection_capability(struct hostapd_data *hapd,
					   struct wpabuf *buf)
{
	if (hapd->conf->hs20_connection_capability) {
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_CONNECTION_CAPABILITY);
		wpabuf_put_u8(buf, 0); /* Reserved */
		wpabuf_put_data(buf, hapd->conf->hs20_connection_capability,
				hapd->conf->hs20_connection_capability_len);
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_operating_class(struct hostapd_data *hapd,
				     struct wpabuf *buf)
{
	if (hapd->conf->hs20_operating_class) {
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_OPERATING_CLASS);
		wpabuf_put_u8(buf, 0); /* Reserved */
		wpabuf_put_data(buf, hapd->conf->hs20_operating_class,
				hapd->conf->hs20_operating_class_len);
		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_icon(struct wpabuf *buf, struct hostapd_bss_config *bss,
			  const char *name)
{
	size_t j;
	struct hs20_icon *icon = NULL;

	for (j = 0; j < bss->hs20_icons_count && !icon; j++) {
		if (os_strcmp(name, bss->hs20_icons[j].name) == 0)
			icon = &bss->hs20_icons[j];
	}
	if (!icon)
		return; /* icon info not found */

	wpabuf_put_le16(buf, icon->width);
	wpabuf_put_le16(buf, icon->height);
	wpabuf_put_data(buf, icon->language, 3);
	wpabuf_put_u8(buf, os_strlen(icon->type));
	wpabuf_put_str(buf, icon->type);
	wpabuf_put_u8(buf, os_strlen(icon->name));
	wpabuf_put_str(buf, icon->name);
}


static void anqp_add_osu_provider(struct wpabuf *buf,
				  struct hostapd_bss_config *bss,
				  struct hs20_osu_provider *p)
{
	u8 *len, *len2, *count;
	unsigned int i;

	len = wpabuf_put(buf, 2); /* OSU Provider Length to be filled */

	/* OSU Friendly Name Duples */
	len2 = wpabuf_put(buf, 2);
	for (i = 0; i < p->friendly_name_count; i++) {
		struct hostapd_lang_string *s = &p->friendly_name[i];
		wpabuf_put_u8(buf, 3 + s->name_len);
		wpabuf_put_data(buf, s->lang, 3);
		wpabuf_put_data(buf, s->name, s->name_len);
	}
	WPA_PUT_LE16(len2, (u8 *) wpabuf_put(buf, 0) - len2 - 2);

	/* OSU Server URI */
	if (p->server_uri) {
		wpabuf_put_u8(buf, os_strlen(p->server_uri));
		wpabuf_put_str(buf, p->server_uri);
	} else
		wpabuf_put_u8(buf, 0);

	/* OSU Method List */
	count = wpabuf_put(buf, 1);
	for (i = 0; p->method_list && p->method_list[i] >= 0; i++)
		wpabuf_put_u8(buf, p->method_list[i]);
	*count = i;

	/* Icons Available */
	len2 = wpabuf_put(buf, 2);
	for (i = 0; i < p->icons_count; i++)
		anqp_add_icon(buf, bss, p->icons[i]);
	WPA_PUT_LE16(len2, (u8 *) wpabuf_put(buf, 0) - len2 - 2);

	/* OSU_NAI */
	if (p->osu_nai) {
		wpabuf_put_u8(buf, os_strlen(p->osu_nai));
		wpabuf_put_str(buf, p->osu_nai);
	} else
		wpabuf_put_u8(buf, 0);

	/* OSU Service Description Duples */
	len2 = wpabuf_put(buf, 2);
	for (i = 0; i < p->service_desc_count; i++) {
		struct hostapd_lang_string *s = &p->service_desc[i];
		wpabuf_put_u8(buf, 3 + s->name_len);
		wpabuf_put_data(buf, s->lang, 3);
		wpabuf_put_data(buf, s->name, s->name_len);
	}
	WPA_PUT_LE16(len2, (u8 *) wpabuf_put(buf, 0) - len2 - 2);

	WPA_PUT_LE16(len, (u8 *) wpabuf_put(buf, 0) - len - 2);
}


static void anqp_add_osu_providers_list(struct hostapd_data *hapd,
					struct wpabuf *buf)
{
	if (hapd->conf->hs20_osu_providers_count) {
		size_t i;
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_OSU_PROVIDERS_LIST);
		wpabuf_put_u8(buf, 0); /* Reserved */

		/* OSU SSID */
		wpabuf_put_u8(buf, hapd->conf->osu_ssid_len);
		wpabuf_put_data(buf, hapd->conf->osu_ssid,
				hapd->conf->osu_ssid_len);

		/* Number of OSU Providers */
		wpabuf_put_u8(buf, hapd->conf->hs20_osu_providers_count);

		for (i = 0; i < hapd->conf->hs20_osu_providers_count; i++) {
			anqp_add_osu_provider(
				buf, hapd->conf,
				&hapd->conf->hs20_osu_providers[i]);
		}

		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_osu_provider_nai(struct wpabuf *buf,
				      struct hs20_osu_provider *p)
{
	/* OSU_NAI for shared BSS (Single SSID) */
	if (p->osu_nai2) {
		wpabuf_put_u8(buf, os_strlen(p->osu_nai2));
		wpabuf_put_str(buf, p->osu_nai2);
	} else {
		wpabuf_put_u8(buf, 0);
	}
}


static void anqp_add_osu_providers_nai_list(struct hostapd_data *hapd,
					    struct wpabuf *buf)
{
	if (hapd->conf->hs20_osu_providers_nai_count) {
		size_t i;
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, HS20_STYPE_OSU_PROVIDERS_NAI_LIST);
		wpabuf_put_u8(buf, 0); /* Reserved */

		for (i = 0; i < hapd->conf->hs20_osu_providers_count; i++) {
			anqp_add_osu_provider_nai(
				buf, &hapd->conf->hs20_osu_providers[i]);
		}

		gas_anqp_set_element_len(buf, len);
	}
}


static void anqp_add_icon_binary_file(struct hostapd_data *hapd,
				      struct wpabuf *buf,
				      const u8 *name, size_t name_len)
{
	struct hs20_icon *icon;
	size_t i;
	u8 *len;

	wpa_hexdump_ascii(MSG_DEBUG, "HS 2.0: Requested Icon Filename",
			  name, name_len);
	for (i = 0; i < hapd->conf->hs20_icons_count; i++) {
		icon = &hapd->conf->hs20_icons[i];
		if (name_len == os_strlen(icon->name) &&
		    os_memcmp(name, icon->name, name_len) == 0)
			break;
	}

	if (i < hapd->conf->hs20_icons_count)
		icon = &hapd->conf->hs20_icons[i];
	else
		icon = NULL;

	len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
	wpabuf_put_u8(buf, HS20_STYPE_ICON_BINARY_FILE);
	wpabuf_put_u8(buf, 0); /* Reserved */

	if (icon) {
		char *data;
		size_t data_len;

		data = os_readfile(icon->file, &data_len);
		if (data == NULL || data_len > 65535) {
			wpabuf_put_u8(buf, 2); /* Download Status:
						* Unspecified file error */
			wpabuf_put_u8(buf, 0);
			wpabuf_put_le16(buf, 0);
		} else {
			wpabuf_put_u8(buf, 0); /* Download Status: Success */
			wpabuf_put_u8(buf, os_strlen(icon->type));
			wpabuf_put_str(buf, icon->type);
			wpabuf_put_le16(buf, data_len);
			wpabuf_put_data(buf, data, data_len);
		}
		os_free(data);
	} else {
		wpabuf_put_u8(buf, 1); /* Download Status: File not found */
		wpabuf_put_u8(buf, 0);
		wpabuf_put_le16(buf, 0);
	}

	gas_anqp_set_element_len(buf, len);
}


static void anqp_add_operator_icon_metadata(struct hostapd_data *hapd,
					    struct wpabuf *buf)
{
	struct hostapd_bss_config *bss = hapd->conf;
	size_t i;
	u8 *len;

	if (!bss->hs20_operator_icon_count)
		return;

	len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);

	wpabuf_put_be24(buf, OUI_WFA);
	wpabuf_put_u8(buf, HS20_ANQP_OUI_TYPE);
	wpabuf_put_u8(buf, HS20_STYPE_OPERATOR_ICON_METADATA);
	wpabuf_put_u8(buf, 0); /* Reserved */

	for (i = 0; i < bss->hs20_operator_icon_count; i++)
		anqp_add_icon(buf, bss, bss->hs20_operator_icon[i]);

	gas_anqp_set_element_len(buf, len);
}

#endif /* CONFIG_HS20 */


#ifdef CONFIG_MBO
static void anqp_add_mbo_cell_data_conn_pref(struct hostapd_data *hapd,
					     struct wpabuf *buf)
{
	if (hapd->conf->mbo_cell_data_conn_pref >= 0) {
		u8 *len = gas_anqp_add_element(buf, ANQP_VENDOR_SPECIFIC);
		wpabuf_put_be24(buf, OUI_WFA);
		wpabuf_put_u8(buf, MBO_ANQP_OUI_TYPE);
		wpabuf_put_u8(buf, MBO_ANQP_SUBTYPE_CELL_CONN_PREF);
		wpabuf_put_u8(buf, hapd->conf->mbo_cell_data_conn_pref);
		gas_anqp_set_element_len(buf, len);
	}
}
#endif /* CONFIG_MBO */


static size_t anqp_get_required_len(struct hostapd_data *hapd,
				    const u16 *infoid,
				    unsigned int num_infoid)
{
	size_t len = 0;
	unsigned int i;

	for (i = 0; i < num_infoid; i++) {
		struct anqp_element *elem = get_anqp_elem(hapd, infoid[i]);

		if (elem)
			len += 2 + 2 + wpabuf_len(elem->payload);
	}

	return len;
}


static struct wpabuf *
gas_serv_build_gas_resp_payload(struct hostapd_data *hapd,
				unsigned int request,
				const u8 *home_realm, size_t home_realm_len,
				const u8 *icon_name, size_t icon_name_len,
				const u16 *extra_req,
				unsigned int num_extra_req)
{
	struct wpabuf *buf;
	size_t len;
	unsigned int i;

	len = 1400;
	if (request & (ANQP_REQ_NAI_REALM | ANQP_REQ_NAI_HOME_REALM))
		len += 1000;
	if (request & ANQP_REQ_ICON_REQUEST)
		len += 65536;
#ifdef CONFIG_FILS
	if (request & ANQP_FILS_REALM_INFO)
		len += 2 * dl_list_len(&hapd->conf->fils_realms);
#endif /* CONFIG_FILS */
	len += anqp_get_required_len(hapd, extra_req, num_extra_req);

	buf = wpabuf_alloc(len);
	if (buf == NULL)
		return NULL;

	if (request & ANQP_REQ_CAPABILITY_LIST)
		anqp_add_capab_list(hapd, buf);
	if (request & ANQP_REQ_VENUE_NAME)
		anqp_add_venue_name(hapd, buf);
	if (request & ANQP_REQ_EMERGENCY_CALL_NUMBER)
		anqp_add_elem(hapd, buf, ANQP_EMERGENCY_CALL_NUMBER);
	if (request & ANQP_REQ_NETWORK_AUTH_TYPE)
		anqp_add_network_auth_type(hapd, buf);
	if (request & ANQP_REQ_ROAMING_CONSORTIUM)
		anqp_add_roaming_consortium(hapd, buf);
	if (request & ANQP_REQ_IP_ADDR_TYPE_AVAILABILITY)
		anqp_add_ip_addr_type_availability(hapd, buf);
	if (request & (ANQP_REQ_NAI_REALM | ANQP_REQ_NAI_HOME_REALM))
		anqp_add_nai_realm(hapd, buf, home_realm, home_realm_len,
				   request & ANQP_REQ_NAI_REALM,
				   request & ANQP_REQ_NAI_HOME_REALM);
	if (request & ANQP_REQ_3GPP_CELLULAR_NETWORK)
		anqp_add_3gpp_cellular_network(hapd, buf);
	if (request & ANQP_REQ_AP_GEOSPATIAL_LOCATION)
		anqp_add_elem(hapd, buf, ANQP_AP_GEOSPATIAL_LOCATION);
	if (request & ANQP_REQ_AP_CIVIC_LOCATION)
		anqp_add_elem(hapd, buf, ANQP_AP_CIVIC_LOCATION);
	if (request & ANQP_REQ_AP_LOCATION_PUBLIC_URI)
		anqp_add_elem(hapd, buf, ANQP_AP_LOCATION_PUBLIC_URI);
	if (request & ANQP_REQ_DOMAIN_NAME)
		anqp_add_domain_name(hapd, buf);
	if (request & ANQP_REQ_EMERGENCY_ALERT_URI)
		anqp_add_elem(hapd, buf, ANQP_EMERGENCY_ALERT_URI);
	if (request & ANQP_REQ_TDLS_CAPABILITY)
		anqp_add_elem(hapd, buf, ANQP_TDLS_CAPABILITY);
	if (request & ANQP_REQ_EMERGENCY_NAI)
		anqp_add_elem(hapd, buf, ANQP_EMERGENCY_NAI);

	for (i = 0; i < num_extra_req; i++) {
#ifdef CONFIG_FILS
		if (extra_req[i] == ANQP_FILS_REALM_INFO) {
			anqp_add_fils_realm_info(hapd, buf);
			continue;
		}
#endif /* CONFIG_FILS */
		if (extra_req[i] == ANQP_VENUE_URL) {
			anqp_add_venue_url(hapd, buf);
			continue;
		}
		anqp_add_elem(hapd, buf, extra_req[i]);
	}

#ifdef CONFIG_HS20
	if (request & ANQP_REQ_HS_CAPABILITY_LIST)
		anqp_add_hs_capab_list(hapd, buf);
	if (request & ANQP_REQ_OPERATOR_FRIENDLY_NAME)
		anqp_add_operator_friendly_name(hapd, buf);
	if (request & ANQP_REQ_WAN_METRICS)
		anqp_add_wan_metrics(hapd, buf);
	if (request & ANQP_REQ_CONNECTION_CAPABILITY)
		anqp_add_connection_capability(hapd, buf);
	if (request & ANQP_REQ_OPERATING_CLASS)
		anqp_add_operating_class(hapd, buf);
	if (request & ANQP_REQ_OSU_PROVIDERS_LIST)
		anqp_add_osu_providers_list(hapd, buf);
	if (request & ANQP_REQ_ICON_REQUEST)
		anqp_add_icon_binary_file(hapd, buf, icon_name, icon_name_len);
	if (request & ANQP_REQ_OPERATOR_ICON_METADATA)
		anqp_add_operator_icon_metadata(hapd, buf);
	if (request & ANQP_REQ_OSU_PROVIDERS_NAI_LIST)
		anqp_add_osu_providers_nai_list(hapd, buf);
#endif /* CONFIG_HS20 */

#ifdef CONFIG_MBO
	if (request & ANQP_REQ_MBO_CELL_DATA_CONN_PREF)
		anqp_add_mbo_cell_data_conn_pref(hapd, buf);
#endif /* CONFIG_MBO */

	return buf;
}


#define ANQP_MAX_EXTRA_REQ 20

struct anqp_query_info {
	unsigned int request;
	const u8 *home_realm_query;
	size_t home_realm_query_len;
	const u8 *icon_name;
	size_t icon_name_len;
	int p2p_sd;
	u16 extra_req[ANQP_MAX_EXTRA_REQ];
	unsigned int num_extra_req;
};


static void set_anqp_req(unsigned int bit, const char *name, int local,
			 struct anqp_query_info *qi)
{
	qi->request |= bit;
	if (local) {
		wpa_printf(MSG_DEBUG, "ANQP: %s (local)", name);
	} else {
		wpa_printf(MSG_DEBUG, "ANQP: %s not available", name);
	}
}


static void rx_anqp_query_list_id(struct hostapd_data *hapd, u16 info_id,
				  struct anqp_query_info *qi)
{
	switch (info_id) {
	case ANQP_CAPABILITY_LIST:
		set_anqp_req(ANQP_REQ_CAPABILITY_LIST, "Capability List", 1,
			     qi);
		break;
	case ANQP_VENUE_NAME:
		set_anqp_req(ANQP_REQ_VENUE_NAME, "Venue Name",
			     hapd->conf->venue_name != NULL, qi);
		break;
	case ANQP_EMERGENCY_CALL_NUMBER:
		set_anqp_req(ANQP_REQ_EMERGENCY_CALL_NUMBER,
			     "Emergency Call Number",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_NETWORK_AUTH_TYPE:
		set_anqp_req(ANQP_REQ_NETWORK_AUTH_TYPE, "Network Auth Type",
			     hapd->conf->network_auth_type != NULL, qi);
		break;
	case ANQP_ROAMING_CONSORTIUM:
		set_anqp_req(ANQP_REQ_ROAMING_CONSORTIUM, "Roaming Consortium",
			     hapd->conf->roaming_consortium != NULL, qi);
		break;
	case ANQP_IP_ADDR_TYPE_AVAILABILITY:
		set_anqp_req(ANQP_REQ_IP_ADDR_TYPE_AVAILABILITY,
			     "IP Addr Type Availability",
			     hapd->conf->ipaddr_type_configured, qi);
		break;
	case ANQP_NAI_REALM:
		set_anqp_req(ANQP_REQ_NAI_REALM, "NAI Realm",
			     hapd->conf->nai_realm_data != NULL, qi);
		break;
	case ANQP_3GPP_CELLULAR_NETWORK:
		set_anqp_req(ANQP_REQ_3GPP_CELLULAR_NETWORK,
			     "3GPP Cellular Network",
			     hapd->conf->anqp_3gpp_cell_net != NULL, qi);
		break;
	case ANQP_AP_GEOSPATIAL_LOCATION:
		set_anqp_req(ANQP_REQ_AP_GEOSPATIAL_LOCATION,
			     "AP Geospatial Location",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_AP_CIVIC_LOCATION:
		set_anqp_req(ANQP_REQ_AP_CIVIC_LOCATION,
			     "AP Civic Location",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_AP_LOCATION_PUBLIC_URI:
		set_anqp_req(ANQP_REQ_AP_LOCATION_PUBLIC_URI,
			     "AP Location Public URI",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_DOMAIN_NAME:
		set_anqp_req(ANQP_REQ_DOMAIN_NAME, "Domain Name",
			     hapd->conf->domain_name != NULL, qi);
		break;
	case ANQP_EMERGENCY_ALERT_URI:
		set_anqp_req(ANQP_REQ_EMERGENCY_ALERT_URI,
			     "Emergency Alert URI",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_TDLS_CAPABILITY:
		set_anqp_req(ANQP_REQ_TDLS_CAPABILITY,
			     "TDLS Capability",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	case ANQP_EMERGENCY_NAI:
		set_anqp_req(ANQP_REQ_EMERGENCY_NAI,
			     "Emergency NAI",
			     get_anqp_elem(hapd, info_id) != NULL, qi);
		break;
	default:
#ifdef CONFIG_FILS
		if (info_id == ANQP_FILS_REALM_INFO &&
		    !dl_list_empty(&hapd->conf->fils_realms)) {
			wpa_printf(MSG_DEBUG,
				   "ANQP: FILS Realm Information (local)");
		} else
#endif /* CONFIG_FILS */
		if (info_id == ANQP_VENUE_URL && hapd->conf->venue_url) {
			wpa_printf(MSG_DEBUG,
				   "ANQP: Venue URL (local)");
		} else if (!get_anqp_elem(hapd, info_id)) {
			wpa_printf(MSG_DEBUG, "ANQP: Unsupported Info Id %u",
				   info_id);
			break;
		}
		if (qi->num_extra_req == ANQP_MAX_EXTRA_REQ) {
			wpa_printf(MSG_DEBUG,
				   "ANQP: No more room for extra requests - ignore Info Id %u",
				   info_id);
			break;
		}
		wpa_printf(MSG_DEBUG, "ANQP: Info Id %u (local)", info_id);
		qi->extra_req[qi->num_extra_req] = info_id;
		qi->num_extra_req++;
		break;
	}
}


static void rx_anqp_query_list(struct hostapd_data *hapd,
			       const u8 *pos, const u8 *end,
			       struct anqp_query_info *qi)
{
	wpa_printf(MSG_DEBUG, "ANQP: %u Info IDs requested in Query list",
		   (unsigned int) (end - pos) / 2);

	while (end - pos >= 2) {
		rx_anqp_query_list_id(hapd, WPA_GET_LE16(pos), qi);
		pos += 2;
	}
}


#ifdef CONFIG_HS20

static void rx_anqp_hs_query_list(struct hostapd_data *hapd, u8 subtype,
				  struct anqp_query_info *qi)
{
	switch (subtype) {
	case HS20_STYPE_CAPABILITY_LIST:
		set_anqp_req(ANQP_REQ_HS_CAPABILITY_LIST, "HS Capability List",
			     1, qi);
		break;
	case HS20_STYPE_OPERATOR_FRIENDLY_NAME:
		set_anqp_req(ANQP_REQ_OPERATOR_FRIENDLY_NAME,
			     "Operator Friendly Name",
			     hapd->conf->hs20_oper_friendly_name != NULL, qi);
		break;
	case HS20_STYPE_WAN_METRICS:
		set_anqp_req(ANQP_REQ_WAN_METRICS, "WAN Metrics",
			     hapd->conf->hs20_wan_metrics != NULL, qi);
		break;
	case HS20_STYPE_CONNECTION_CAPABILITY:
		set_anqp_req(ANQP_REQ_CONNECTION_CAPABILITY,
			     "Connection Capability",
			     hapd->conf->hs20_connection_capability != NULL,
			     qi);
		break;
	case HS20_STYPE_OPERATING_CLASS:
		set_anqp_req(ANQP_REQ_OPERATING_CLASS, "Operating Class",
			     hapd->conf->hs20_operating_class != NULL, qi);
		break;
	case HS20_STYPE_OSU_PROVIDERS_LIST:
		set_anqp_req(ANQP_REQ_OSU_PROVIDERS_LIST, "OSU Providers list",
			     hapd->conf->hs20_osu_providers_count, qi);
		break;
	case HS20_STYPE_OPERATOR_ICON_METADATA:
		set_anqp_req(ANQP_REQ_OPERATOR_ICON_METADATA,
			     "Operator Icon Metadata",
			     hapd->conf->hs20_operator_icon_count, qi);
		break;
	case HS20_STYPE_OSU_PROVIDERS_NAI_LIST:
		set_anqp_req(ANQP_REQ_OSU_PROVIDERS_NAI_LIST,
			     "OSU Providers NAI List",
			     hapd->conf->hs20_osu_providers_nai_count, qi);
		break;
	default:
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported HS 2.0 subtype %u",
			   subtype);
		break;
	}
}


static void rx_anqp_hs_nai_home_realm(struct hostapd_data *hapd,
				      const u8 *pos, const u8 *end,
				      struct anqp_query_info *qi)
{
	qi->request |= ANQP_REQ_NAI_HOME_REALM;
	qi->home_realm_query = pos;
	qi->home_realm_query_len = end - pos;
	if (hapd->conf->nai_realm_data != NULL) {
		wpa_printf(MSG_DEBUG, "ANQP: HS 2.0 NAI Home Realm Query "
			   "(local)");
	} else {
		wpa_printf(MSG_DEBUG, "ANQP: HS 2.0 NAI Home Realm Query not "
			   "available");
	}
}


static void rx_anqp_hs_icon_request(struct hostapd_data *hapd,
				    const u8 *pos, const u8 *end,
				    struct anqp_query_info *qi)
{
	qi->request |= ANQP_REQ_ICON_REQUEST;
	qi->icon_name = pos;
	qi->icon_name_len = end - pos;
	if (hapd->conf->hs20_icons_count) {
		wpa_printf(MSG_DEBUG, "ANQP: HS 2.0 Icon Request Query "
			   "(local)");
	} else {
		wpa_printf(MSG_DEBUG, "ANQP: HS 2.0 Icon Request Query not "
			   "available");
	}
}


static void rx_anqp_vendor_specific_hs20(struct hostapd_data *hapd,
					 const u8 *pos, const u8 *end,
					 struct anqp_query_info *qi)
{
	u8 subtype;

	if (end - pos <= 1)
		return;

	subtype = *pos++;
	pos++; /* Reserved */
	switch (subtype) {
	case HS20_STYPE_QUERY_LIST:
		wpa_printf(MSG_DEBUG, "ANQP: HS 2.0 Query List");
		while (pos < end) {
			rx_anqp_hs_query_list(hapd, *pos, qi);
			pos++;
		}
		break;
	case HS20_STYPE_NAI_HOME_REALM_QUERY:
		rx_anqp_hs_nai_home_realm(hapd, pos, end, qi);
		break;
	case HS20_STYPE_ICON_REQUEST:
		rx_anqp_hs_icon_request(hapd, pos, end, qi);
		break;
	default:
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported HS 2.0 query subtype "
			   "%u", subtype);
		break;
	}
}

#endif /* CONFIG_HS20 */


#ifdef CONFIG_P2P
static void rx_anqp_vendor_specific_p2p(struct hostapd_data *hapd,
					struct anqp_query_info *qi)
{
	/*
	 * This is for P2P SD and will be taken care of by the P2P
	 * implementation. This query needs to be ignored in the generic
	 * GAS server to avoid duplicated response.
	 */
	wpa_printf(MSG_DEBUG,
		   "ANQP: Ignore WFA vendor type %u (P2P SD) in generic GAS server",
		   P2P_OUI_TYPE);
	qi->p2p_sd = 1;
	return;
}
#endif /* CONFIG_P2P */


#ifdef CONFIG_MBO

static void rx_anqp_mbo_query_list(struct hostapd_data *hapd, u8 subtype,
				  struct anqp_query_info *qi)
{
	switch (subtype) {
	case MBO_ANQP_SUBTYPE_CELL_CONN_PREF:
		set_anqp_req(ANQP_REQ_MBO_CELL_DATA_CONN_PREF,
			     "Cellular Data Connection Preference",
			     hapd->conf->mbo_cell_data_conn_pref >= 0, qi);
		break;
	default:
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported MBO subtype %u",
			   subtype);
		break;
	}
}


static void rx_anqp_vendor_specific_mbo(struct hostapd_data *hapd,
					const u8 *pos, const u8 *end,
					struct anqp_query_info *qi)
{
	u8 subtype;

	if (end - pos < 1)
		return;

	subtype = *pos++;
	switch (subtype) {
	case MBO_ANQP_SUBTYPE_QUERY_LIST:
		wpa_printf(MSG_DEBUG, "ANQP: MBO Query List");
		while (pos < end) {
			rx_anqp_mbo_query_list(hapd, *pos, qi);
			pos++;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported MBO query subtype %u",
			   subtype);
		break;
	}
}

#endif /* CONFIG_MBO */


static void rx_anqp_vendor_specific(struct hostapd_data *hapd,
				    const u8 *pos, const u8 *end,
				    struct anqp_query_info *qi)
{
	u32 oui;

	if (end - pos < 4) {
		wpa_printf(MSG_DEBUG, "ANQP: Too short vendor specific ANQP "
			   "Query element");
		return;
	}

	oui = WPA_GET_BE24(pos);
	pos += 3;
	if (oui != OUI_WFA) {
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported vendor OUI %06x",
			   oui);
		return;
	}

	switch (*pos) {
#ifdef CONFIG_P2P
	case P2P_OUI_TYPE:
		rx_anqp_vendor_specific_p2p(hapd, qi);
		break;
#endif /* CONFIG_P2P */
#ifdef CONFIG_HS20
	case HS20_ANQP_OUI_TYPE:
		rx_anqp_vendor_specific_hs20(hapd, pos + 1, end, qi);
		break;
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
	case MBO_ANQP_OUI_TYPE:
		rx_anqp_vendor_specific_mbo(hapd, pos + 1, end, qi);
		break;
#endif /* CONFIG_MBO */
	default:
		wpa_printf(MSG_DEBUG, "ANQP: Unsupported WFA vendor type %u",
			   *pos);
		break;
	}
}


static void gas_serv_req_local_processing(struct hostapd_data *hapd,
					  const u8 *sa, u8 dialog_token,
					  struct anqp_query_info *qi, int prot,
					  int std_addr3)
{
	struct wpabuf *buf, *tx_buf;

	buf = gas_serv_build_gas_resp_payload(hapd, qi->request,
					      qi->home_realm_query,
					      qi->home_realm_query_len,
					      qi->icon_name, qi->icon_name_len,
					      qi->extra_req, qi->num_extra_req);
	wpa_hexdump_buf(MSG_MSGDUMP, "ANQP: Locally generated ANQP responses",
			buf);
	if (!buf)
		return;
#ifdef CONFIG_P2P
	if (wpabuf_len(buf) == 0 && qi->p2p_sd) {
		wpa_printf(MSG_DEBUG,
			   "ANQP: Do not send response to P2P SD from generic GAS service (P2P SD implementation will process this)");
		wpabuf_free(buf);
		return;
	}
#endif /* CONFIG_P2P */

	if (wpabuf_len(buf) > hapd->conf->gas_frag_limit ||
	    hapd->conf->gas_comeback_delay) {
		struct gas_dialog_info *di;
		u16 comeback_delay = 1;

		if (hapd->conf->gas_comeback_delay) {
			/* Testing - allow overriding of the delay value */
			comeback_delay = hapd->conf->gas_comeback_delay;
		}

		wpa_printf(MSG_DEBUG, "ANQP: Too long response to fit in "
			   "initial response - use GAS comeback");
		di = gas_dialog_create(hapd, sa, dialog_token);
		if (!di) {
			wpa_printf(MSG_INFO, "ANQP: Could not create dialog "
				   "for " MACSTR " (dialog token %u)",
				   MAC2STR(sa), dialog_token);
			wpabuf_free(buf);
			tx_buf = gas_anqp_build_initial_resp_buf(
				dialog_token, WLAN_STATUS_UNSPECIFIED_FAILURE,
				0, NULL);
		} else {
			di->prot = prot;
			di->sd_resp = buf;
			di->sd_resp_pos = 0;
			tx_buf = gas_anqp_build_initial_resp_buf(
				dialog_token, WLAN_STATUS_SUCCESS,
				comeback_delay, NULL);
		}
	} else {
		wpa_printf(MSG_DEBUG, "ANQP: Initial response (no comeback)");
		tx_buf = gas_anqp_build_initial_resp_buf(
			dialog_token, WLAN_STATUS_SUCCESS, 0, buf);
		wpabuf_free(buf);
	}
	if (!tx_buf)
		return;
	if (prot)
		convert_to_protected_dual(tx_buf);
	if (std_addr3)
		hostapd_drv_send_action(hapd, hapd->iface->freq, 0, sa,
					wpabuf_head(tx_buf),
					wpabuf_len(tx_buf));
	else
		hostapd_drv_send_action_addr3_ap(hapd, hapd->iface->freq, 0, sa,
						 wpabuf_head(tx_buf),
						 wpabuf_len(tx_buf));
	wpabuf_free(tx_buf);
}


#ifdef CONFIG_DPP
static void gas_serv_req_dpp_processing(struct hostapd_data *hapd,
					const u8 *sa, u8 dialog_token,
					int prot, struct wpabuf *buf)
{
	struct wpabuf *tx_buf;

	if (wpabuf_len(buf) > hapd->conf->gas_frag_limit ||
	    hapd->conf->gas_comeback_delay) {
		struct gas_dialog_info *di;
		u16 comeback_delay = 1;

		if (hapd->conf->gas_comeback_delay) {
			/* Testing - allow overriding of the delay value */
			comeback_delay = hapd->conf->gas_comeback_delay;
		}

		wpa_printf(MSG_DEBUG,
			   "DPP: Too long response to fit in initial response - use GAS comeback");
		di = gas_dialog_create(hapd, sa, dialog_token);
		if (!di) {
			wpa_printf(MSG_INFO, "DPP: Could not create dialog for "
				   MACSTR " (dialog token %u)",
				   MAC2STR(sa), dialog_token);
			wpabuf_free(buf);
			tx_buf = gas_build_initial_resp(
				dialog_token, WLAN_STATUS_UNSPECIFIED_FAILURE,
				0, 10);
			if (tx_buf)
				gas_serv_write_dpp_adv_proto(tx_buf);
		} else {
			di->prot = prot;
			di->sd_resp = buf;
			di->sd_resp_pos = 0;
			tx_buf = gas_build_initial_resp(
				dialog_token, WLAN_STATUS_SUCCESS,
				comeback_delay, 10);
			if (tx_buf)
				gas_serv_write_dpp_adv_proto(tx_buf);
		}
	} else {
		wpa_printf(MSG_DEBUG,
			   "DPP: GAS Initial response (no comeback)");
		tx_buf = gas_build_initial_resp(
			dialog_token, WLAN_STATUS_SUCCESS, 0,
			10 + 2 + wpabuf_len(buf));
		if (tx_buf) {
			gas_serv_write_dpp_adv_proto(tx_buf);
			wpabuf_put_le16(tx_buf, wpabuf_len(buf));
			wpabuf_put_buf(tx_buf, buf);
			hostapd_dpp_gas_status_handler(hapd, 1);
		}
		wpabuf_free(buf);
	}
	if (!tx_buf)
		return;
	if (prot)
		convert_to_protected_dual(tx_buf);
	hostapd_drv_send_action(hapd, hapd->iface->freq, 0, sa,
				wpabuf_head(tx_buf),
				wpabuf_len(tx_buf));
	wpabuf_free(tx_buf);
}
#endif /* CONFIG_DPP */


static void gas_serv_rx_gas_initial_req(struct hostapd_data *hapd,
					const u8 *sa,
					const u8 *data, size_t len, int prot,
					int std_addr3)
{
	const u8 *pos = data;
	const u8 *end = data + len;
	const u8 *next;
	u8 dialog_token;
	u16 slen;
	struct anqp_query_info qi;
	const u8 *adv_proto;
#ifdef CONFIG_DPP
	int dpp = 0;
#endif /* CONFIG_DPP */

	if (len < 1 + 2)
		return;

	os_memset(&qi, 0, sizeof(qi));

	dialog_token = *pos++;
	wpa_msg(hapd->msg_ctx, MSG_DEBUG,
		"GAS: GAS Initial Request from " MACSTR " (dialog token %u) ",
		MAC2STR(sa), dialog_token);

	if (*pos != WLAN_EID_ADV_PROTO) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG,
			"GAS: Unexpected IE in GAS Initial Request: %u", *pos);
		return;
	}
	adv_proto = pos++;

	slen = *pos++;
	if (slen > end - pos || slen < 2) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG,
			"GAS: Invalid IE in GAS Initial Request");
		return;
	}
	next = pos + slen;
	pos++; /* skip QueryRespLenLimit and PAME-BI */

#ifdef CONFIG_DPP
	if (slen == 8 && *pos == WLAN_EID_VENDOR_SPECIFIC &&
	    pos[1] == 5 && WPA_GET_BE24(&pos[2]) == OUI_WFA &&
	    pos[5] == DPP_OUI_TYPE && pos[6] == 0x01) {
		wpa_printf(MSG_DEBUG, "DPP: Configuration Request");
		dpp = 1;
	} else
#endif /* CONFIG_DPP */

	if (*pos != ACCESS_NETWORK_QUERY_PROTOCOL) {
		struct wpabuf *buf;
		wpa_msg(hapd->msg_ctx, MSG_DEBUG,
			"GAS: Unsupported GAS advertisement protocol id %u",
			*pos);
		if (sa[0] & 0x01)
			return; /* Invalid source address - drop silently */
		buf = gas_build_initial_resp(
			dialog_token, WLAN_STATUS_GAS_ADV_PROTO_NOT_SUPPORTED,
			0, 2 + slen + 2);
		if (buf == NULL)
			return;
		wpabuf_put_data(buf, adv_proto, 2 + slen);
		wpabuf_put_le16(buf, 0); /* Query Response Length */
		if (prot)
			convert_to_protected_dual(buf);
		if (std_addr3)
			hostapd_drv_send_action(hapd, hapd->iface->freq, 0, sa,
						wpabuf_head(buf),
						wpabuf_len(buf));
		else
			hostapd_drv_send_action_addr3_ap(hapd,
							 hapd->iface->freq, 0,
							 sa, wpabuf_head(buf),
							 wpabuf_len(buf));
		wpabuf_free(buf);
		return;
	}

	pos = next;
	/* Query Request */
	if (end - pos < 2)
		return;
	slen = WPA_GET_LE16(pos);
	pos += 2;
	if (slen > end - pos)
		return;
	end = pos + slen;

#ifdef CONFIG_DPP
	if (dpp) {
		struct wpabuf *msg;

		msg = hostapd_dpp_gas_req_handler(hapd, sa, pos, slen);
		if (!msg)
			return;
		gas_serv_req_dpp_processing(hapd, sa, dialog_token, prot, msg);
		return;
	}
#endif /* CONFIG_DPP */

	/* ANQP Query Request */
	while (pos < end) {
		u16 info_id, elen;

		if (end - pos < 4)
			return;

		info_id = WPA_GET_LE16(pos);
		pos += 2;
		elen = WPA_GET_LE16(pos);
		pos += 2;

		if (elen > end - pos) {
			wpa_printf(MSG_DEBUG, "ANQP: Invalid Query Request");
			return;
		}

		switch (info_id) {
		case ANQP_QUERY_LIST:
			rx_anqp_query_list(hapd, pos, pos + elen, &qi);
			break;
		case ANQP_VENDOR_SPECIFIC:
			rx_anqp_vendor_specific(hapd, pos, pos + elen, &qi);
			break;
		default:
			wpa_printf(MSG_DEBUG, "ANQP: Unsupported Query "
				   "Request element %u", info_id);
			break;
		}

		pos += elen;
	}

	gas_serv_req_local_processing(hapd, sa, dialog_token, &qi, prot,
				      std_addr3);
}


static void gas_serv_rx_gas_comeback_req(struct hostapd_data *hapd,
					 const u8 *sa,
					 const u8 *data, size_t len, int prot,
					 int std_addr3)
{
	struct gas_dialog_info *dialog;
	struct wpabuf *buf, *tx_buf;
	u8 dialog_token;
	size_t frag_len;
	int more = 0;

	wpa_hexdump(MSG_DEBUG, "GAS: RX GAS Comeback Request", data, len);
	if (len < 1)
		return;
	dialog_token = *data;
	wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: Dialog Token: %u",
		dialog_token);

	dialog = gas_serv_dialog_find(hapd, sa, dialog_token);
	if (!dialog) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: No pending SD "
			"response fragment for " MACSTR " dialog token %u",
			MAC2STR(sa), dialog_token);

		if (sa[0] & 0x01)
			return; /* Invalid source address - drop silently */
		tx_buf = gas_anqp_build_comeback_resp_buf(
			dialog_token, WLAN_STATUS_NO_OUTSTANDING_GAS_REQ, 0, 0,
			0, NULL);
		if (tx_buf == NULL)
			return;
		goto send_resp;
	}

	frag_len = wpabuf_len(dialog->sd_resp) - dialog->sd_resp_pos;
	if (frag_len > hapd->conf->gas_frag_limit) {
		frag_len = hapd->conf->gas_frag_limit;
		more = 1;
	}
	wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: resp frag_len %u",
		(unsigned int) frag_len);
	buf = wpabuf_alloc_copy(wpabuf_head_u8(dialog->sd_resp) +
				dialog->sd_resp_pos, frag_len);
	if (buf == NULL) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: Failed to allocate "
			"buffer");
		gas_serv_dialog_clear(dialog);
		return;
	}
#ifdef CONFIG_DPP
	if (dialog->dpp) {
		tx_buf = gas_build_comeback_resp(dialog_token,
						 WLAN_STATUS_SUCCESS,
						 dialog->sd_frag_id, more, 0,
						 10 + frag_len);
		if (tx_buf) {
			gas_serv_write_dpp_adv_proto(tx_buf);
			wpabuf_put_buf(tx_buf, buf);
		}
	} else
#endif /* CONFIG_DPP */
	tx_buf = gas_anqp_build_comeback_resp_buf(dialog_token,
						  WLAN_STATUS_SUCCESS,
						  dialog->sd_frag_id,
						  more, 0, buf);
	wpabuf_free(buf);
	if (tx_buf == NULL) {
		gas_serv_dialog_clear(dialog);
		return;
	}
	wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: Tx GAS Comeback Response "
		"(frag_id %d more=%d frag_len=%d)",
		dialog->sd_frag_id, more, (int) frag_len);
	dialog->sd_frag_id++;
	dialog->sd_resp_pos += frag_len;

	if (more) {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: %d more bytes remain "
			"to be sent",
			(int) (wpabuf_len(dialog->sd_resp) -
			       dialog->sd_resp_pos));
	} else {
		wpa_msg(hapd->msg_ctx, MSG_DEBUG, "GAS: All fragments of "
			"SD response sent");
#ifdef CONFIG_DPP
		if (dialog->dpp)
			hostapd_dpp_gas_status_handler(hapd, 1);
#endif /* CONFIG_DPP */
		gas_serv_dialog_clear(dialog);
		gas_serv_free_dialogs(hapd, sa);
	}

send_resp:
	if (prot)
		convert_to_protected_dual(tx_buf);
	if (std_addr3)
		hostapd_drv_send_action(hapd, hapd->iface->freq, 0, sa,
					wpabuf_head(tx_buf),
					wpabuf_len(tx_buf));
	else
		hostapd_drv_send_action_addr3_ap(hapd, hapd->iface->freq, 0, sa,
						 wpabuf_head(tx_buf),
						 wpabuf_len(tx_buf));
	wpabuf_free(tx_buf);
}


static void gas_serv_rx_public_action(void *ctx, const u8 *buf, size_t len,
				      int freq)
{
	struct hostapd_data *hapd = ctx;
	const struct ieee80211_mgmt *mgmt;
	const u8 *sa, *data;
	int prot, std_addr3;

	mgmt = (const struct ieee80211_mgmt *) buf;
	if (len < IEEE80211_HDRLEN + 2)
		return;
	if (mgmt->u.action.category != WLAN_ACTION_PUBLIC &&
	    mgmt->u.action.category != WLAN_ACTION_PROTECTED_DUAL)
		return;
	/*
	 * Note: Public Action and Protected Dual of Public Action frames share
	 * the same payload structure, so it is fine to use definitions of
	 * Public Action frames to process both.
	 */
	prot = mgmt->u.action.category == WLAN_ACTION_PROTECTED_DUAL;
	sa = mgmt->sa;
	if (hapd->conf->gas_address3 == 1)
		std_addr3 = 1;
	else if (hapd->conf->gas_address3 == 2)
		std_addr3 = 0;
	else
		std_addr3 = is_broadcast_ether_addr(mgmt->bssid);
	len -= IEEE80211_HDRLEN + 1;
	data = buf + IEEE80211_HDRLEN + 1;
	switch (data[0]) {
	case WLAN_PA_GAS_INITIAL_REQ:
		gas_serv_rx_gas_initial_req(hapd, sa, data + 1, len - 1, prot,
					    std_addr3);
		break;
	case WLAN_PA_GAS_COMEBACK_REQ:
		gas_serv_rx_gas_comeback_req(hapd, sa, data + 1, len - 1, prot,
					     std_addr3);
		break;
	}
}


int gas_serv_init(struct hostapd_data *hapd)
{
	hapd->public_action_cb2 = gas_serv_rx_public_action;
	hapd->public_action_cb2_ctx = hapd;
	return 0;
}


void gas_serv_deinit(struct hostapd_data *hapd)
{
}
