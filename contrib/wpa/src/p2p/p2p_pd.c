/*
 * Wi-Fi Direct - P2P provision discovery
 * Copyright (c) 2009-2010, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "wps/wps_defs.h"
#include "p2p_i.h"
#include "p2p.h"


/*
 * Number of retries to attempt for provision discovery requests
 * in case the peer is not listening.
 */
#define MAX_PROV_DISC_REQ_RETRIES 120


static void p2p_build_wps_ie_config_methods(struct wpabuf *buf,
					    u16 config_methods)
{
	u8 *len;
	wpabuf_put_u8(buf, WLAN_EID_VENDOR_SPECIFIC);
	len = wpabuf_put(buf, 1);
	wpabuf_put_be32(buf, WPS_DEV_OUI_WFA);

	/* Config Methods */
	wpabuf_put_be16(buf, ATTR_CONFIG_METHODS);
	wpabuf_put_be16(buf, 2);
	wpabuf_put_be16(buf, config_methods);

	p2p_buf_update_ie_hdr(buf, len);
}


static void p2ps_add_new_group_info(struct p2p_data *p2p,
				    struct p2p_device *dev,
				    struct wpabuf *buf)
{
	int found;
	u8 intended_addr[ETH_ALEN];
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	int group_iface;
	unsigned int force_freq;

	if (!p2p->cfg->get_go_info)
		return;

	found = p2p->cfg->get_go_info(
		p2p->cfg->cb_ctx, intended_addr, ssid,
		&ssid_len, &group_iface, &force_freq);
	if (found) {
		if (force_freq > 0) {
			p2p->p2ps_prov->force_freq = force_freq;
			p2p->p2ps_prov->pref_freq = 0;

			if (dev)
				p2p_prepare_channel(p2p, dev, force_freq, 0, 0);
		}
		p2p_buf_add_group_id(buf, p2p->cfg->dev_addr,
				     ssid, ssid_len);

		if (group_iface)
			p2p_buf_add_intended_addr(buf, p2p->intended_addr);
		else
			p2p_buf_add_intended_addr(buf, intended_addr);
	} else {
		if (!p2p->ssid_set) {
			p2p_build_ssid(p2p, p2p->ssid, &p2p->ssid_len);
			p2p->ssid_set = 1;
		}

		/* Add pre-composed P2P Group ID */
		p2p_buf_add_group_id(buf, p2p->cfg->dev_addr,
				     p2p->ssid, p2p->ssid_len);

		if (group_iface)
			p2p_buf_add_intended_addr(
				buf, p2p->intended_addr);
		else
			p2p_buf_add_intended_addr(
				buf, p2p->cfg->dev_addr);
	}
}


static void p2ps_add_pd_req_attrs(struct p2p_data *p2p, struct p2p_device *dev,
				  struct wpabuf *buf, u16 config_methods)
{
	struct p2ps_provision *prov = p2p->p2ps_prov;
	struct p2ps_feature_capab fcap = { prov->cpt_mask, 0 };
	int shared_group = 0;
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	u8 go_dev_addr[ETH_ALEN];
	u8 intended_addr[ETH_ALEN];
	int follow_on_req_fail = prov->status >= 0 &&
		prov->status != P2P_SC_SUCCESS_DEFERRED;

	/* If we might be explicite group owner, add GO details */
	if (!follow_on_req_fail &&
	    (prov->conncap & (P2PS_SETUP_GROUP_OWNER | P2PS_SETUP_NEW)))
		p2ps_add_new_group_info(p2p, dev, buf);

	if (prov->status >= 0)
		p2p_buf_add_status(buf, (u8) prov->status);
	else
		prov->method = config_methods;

	if (!follow_on_req_fail) {
		if (p2p->cfg->get_persistent_group) {
			shared_group = p2p->cfg->get_persistent_group(
				p2p->cfg->cb_ctx, dev->info.p2p_device_addr,
				NULL, 0, go_dev_addr, ssid, &ssid_len,
				intended_addr);
		}

		if (shared_group ||
		    (prov->conncap & (P2PS_SETUP_CLIENT | P2PS_SETUP_NEW)))
			p2p_buf_add_channel_list(buf, p2p->cfg->country,
						 &p2p->channels);

		if ((shared_group && !is_zero_ether_addr(intended_addr)) ||
		    (prov->conncap & (P2PS_SETUP_GROUP_OWNER | P2PS_SETUP_NEW)))
			p2p_buf_add_operating_channel(buf, p2p->cfg->country,
						      p2p->op_reg_class,
						      p2p->op_channel);
	}

	if (prov->status < 0 && prov->info[0])
		p2p_buf_add_session_info(buf, prov->info);

	if (!follow_on_req_fail)
		p2p_buf_add_connection_capability(buf, prov->conncap);

	p2p_buf_add_advertisement_id(buf, prov->adv_id, prov->adv_mac);

	if (!follow_on_req_fail) {
		if (shared_group || prov->conncap == P2PS_SETUP_NEW ||
		    prov->conncap ==
		    (P2PS_SETUP_GROUP_OWNER | P2PS_SETUP_NEW) ||
		    prov->conncap ==
		    (P2PS_SETUP_GROUP_OWNER | P2PS_SETUP_CLIENT)) {
			/* Add Config Timeout */
			p2p_buf_add_config_timeout(buf, p2p->go_timeout,
						   p2p->client_timeout);
		}

		p2p_buf_add_listen_channel(buf, p2p->cfg->country,
					   p2p->cfg->reg_class,
					   p2p->cfg->channel);
	}

	p2p_buf_add_session_id(buf, prov->session_id, prov->session_mac);

	p2p_buf_add_feature_capability(buf, sizeof(fcap), (const u8 *) &fcap);

	if (shared_group) {
		p2p_buf_add_persistent_group_info(buf, go_dev_addr,
						  ssid, ssid_len);
		/* Add intended interface address if it is not added yet */
		if ((prov->conncap == P2PS_SETUP_NONE ||
		     prov->conncap == P2PS_SETUP_CLIENT) &&
		    !is_zero_ether_addr(intended_addr))
			p2p_buf_add_intended_addr(buf, intended_addr);
	}
}


static struct wpabuf * p2p_build_prov_disc_req(struct p2p_data *p2p,
					       struct p2p_device *dev,
					       int join)
{
	struct wpabuf *buf;
	u8 *len;
	size_t extra = 0;
	u8 dialog_token = dev->dialog_token;
	u16 config_methods = dev->req_config_methods;
	struct p2p_device *go = join ? dev : NULL;
	u8 group_capab;

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_prov_disc_req)
		extra = wpabuf_len(p2p->wfd_ie_prov_disc_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_PD_REQ])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_PD_REQ]);

	if (p2p->p2ps_prov)
		extra += os_strlen(p2p->p2ps_prov->info) + 1 +
			sizeof(struct p2ps_provision);

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_PROV_DISC_REQ, dialog_token);

	len = p2p_buf_add_ie_hdr(buf);

	group_capab = 0;
	if (p2p->p2ps_prov) {
		group_capab |= P2P_GROUP_CAPAB_PERSISTENT_GROUP;
		group_capab |= P2P_GROUP_CAPAB_PERSISTENT_RECONN;
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
	}
	p2p_buf_add_capability(buf, p2p->dev_capab &
			       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY,
			       group_capab);
	p2p_buf_add_device_info(buf, p2p, NULL);
	if (p2p->p2ps_prov) {
		p2ps_add_pd_req_attrs(p2p, dev, buf, config_methods);
	} else if (go) {
		p2p_buf_add_group_id(buf, go->info.p2p_device_addr,
				     go->oper_ssid, go->oper_ssid_len);
	}
	p2p_buf_update_ie_hdr(buf, len);

	/* WPS IE with Config Methods attribute */
	p2p_build_wps_ie_config_methods(buf, config_methods);

#ifdef CONFIG_WIFI_DISPLAY
	if (p2p->wfd_ie_prov_disc_req)
		wpabuf_put_buf(buf, p2p->wfd_ie_prov_disc_req);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_PD_REQ])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_PD_REQ]);

	return buf;
}


static struct wpabuf * p2p_build_prov_disc_resp(struct p2p_data *p2p,
						struct p2p_device *dev,
						u8 dialog_token,
						enum p2p_status_code status,
						u16 config_methods,
						u32 adv_id,
						const u8 *group_id,
						size_t group_id_len,
						const u8 *persist_ssid,
						size_t persist_ssid_len,
						const u8 *fcap,
						u16 fcap_len)
{
	struct wpabuf *buf;
	size_t extra = 0;
	int persist = 0;

#ifdef CONFIG_WIFI_DISPLAY
	struct wpabuf *wfd_ie = p2p->wfd_ie_prov_disc_resp;
	if (wfd_ie && group_id) {
		size_t i;
		for (i = 0; i < p2p->num_groups; i++) {
			struct p2p_group *g = p2p->groups[i];
			struct wpabuf *ie;
			if (!p2p_group_is_group_id_match(g, group_id,
							 group_id_len))
				continue;
			ie = p2p_group_get_wfd_ie(g);
			if (ie) {
				wfd_ie = ie;
				break;
			}
		}
	}
	if (wfd_ie)
		extra = wpabuf_len(wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_PD_RESP])
		extra += wpabuf_len(p2p->vendor_elem[VENDOR_ELEM_P2P_PD_RESP]);

	buf = wpabuf_alloc(1000 + extra);
	if (buf == NULL)
		return NULL;

	p2p_buf_add_public_action_hdr(buf, P2P_PROV_DISC_RESP, dialog_token);

	/* Add P2P IE for P2PS */
	if (p2p->p2ps_prov && p2p->p2ps_prov->adv_id == adv_id) {
		u8 *len = p2p_buf_add_ie_hdr(buf);
		struct p2ps_provision *prov = p2p->p2ps_prov;
		u8 group_capab;
		u8 conncap = 0;

		if (status == P2P_SC_SUCCESS ||
		    status == P2P_SC_SUCCESS_DEFERRED)
			conncap = prov->conncap;

		if (!status && prov->status != -1)
			status = prov->status;

		p2p_buf_add_status(buf, status);
		group_capab = P2P_GROUP_CAPAB_PERSISTENT_GROUP |
			P2P_GROUP_CAPAB_PERSISTENT_RECONN;
		if (p2p->cross_connect)
			group_capab |= P2P_GROUP_CAPAB_CROSS_CONN;
		if (p2p->cfg->p2p_intra_bss)
			group_capab |= P2P_GROUP_CAPAB_INTRA_BSS_DIST;
		p2p_buf_add_capability(buf, p2p->dev_capab &
				       ~P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY,
				       group_capab);
		p2p_buf_add_device_info(buf, p2p, NULL);

		if (persist_ssid && p2p->cfg->get_persistent_group && dev &&
		    (status == P2P_SC_SUCCESS ||
		     status == P2P_SC_SUCCESS_DEFERRED)) {
			u8 ssid[SSID_MAX_LEN];
			size_t ssid_len;
			u8 go_dev_addr[ETH_ALEN];
			u8 intended_addr[ETH_ALEN];

			persist = p2p->cfg->get_persistent_group(
				p2p->cfg->cb_ctx,
				dev->info.p2p_device_addr,
				persist_ssid, persist_ssid_len, go_dev_addr,
				ssid, &ssid_len, intended_addr);
			if (persist) {
				p2p_buf_add_persistent_group_info(
					buf, go_dev_addr, ssid, ssid_len);
				if (!is_zero_ether_addr(intended_addr))
					p2p_buf_add_intended_addr(
						buf, intended_addr);
			}
		}

		if (!persist && (conncap & P2PS_SETUP_GROUP_OWNER))
			p2ps_add_new_group_info(p2p, dev, buf);

		/* Add Operating Channel if conncap indicates GO */
		if (persist || (conncap & P2PS_SETUP_GROUP_OWNER)) {
			if (p2p->op_reg_class && p2p->op_channel)
				p2p_buf_add_operating_channel(
					buf, p2p->cfg->country,
					p2p->op_reg_class,
					p2p->op_channel);
			else
				p2p_buf_add_operating_channel(
					buf, p2p->cfg->country,
					p2p->cfg->op_reg_class,
					p2p->cfg->op_channel);
		}

		if (persist ||
		    (conncap & (P2PS_SETUP_CLIENT | P2PS_SETUP_GROUP_OWNER)))
			p2p_buf_add_channel_list(buf, p2p->cfg->country,
						 &p2p->channels);

		if (!persist && conncap)
			p2p_buf_add_connection_capability(buf, conncap);

		p2p_buf_add_advertisement_id(buf, adv_id, prov->adv_mac);

		if (persist ||
		    (conncap & (P2PS_SETUP_CLIENT | P2PS_SETUP_GROUP_OWNER)))
			p2p_buf_add_config_timeout(buf, p2p->go_timeout,
						   p2p->client_timeout);

		p2p_buf_add_session_id(buf, prov->session_id,
				       prov->session_mac);

		p2p_buf_add_feature_capability(buf, fcap_len, fcap);
		p2p_buf_update_ie_hdr(buf, len);
	} else if (status != P2P_SC_SUCCESS || adv_id) {
		u8 *len = p2p_buf_add_ie_hdr(buf);

		p2p_buf_add_status(buf, status);

		if (p2p->p2ps_prov)
			p2p_buf_add_advertisement_id(buf, adv_id,
						     p2p->p2ps_prov->adv_mac);

		p2p_buf_update_ie_hdr(buf, len);
	}

	/* WPS IE with Config Methods attribute */
	p2p_build_wps_ie_config_methods(buf, config_methods);

#ifdef CONFIG_WIFI_DISPLAY
	if (wfd_ie)
		wpabuf_put_buf(buf, wfd_ie);
#endif /* CONFIG_WIFI_DISPLAY */

	if (p2p->vendor_elem && p2p->vendor_elem[VENDOR_ELEM_P2P_PD_RESP])
		wpabuf_put_buf(buf, p2p->vendor_elem[VENDOR_ELEM_P2P_PD_RESP]);

	return buf;
}


static int p2ps_setup_p2ps_prov(struct p2p_data *p2p, u32 adv_id,
				u32 session_id, u16 method,
				const u8 *session_mac, const u8 *adv_mac)
{
	struct p2ps_provision *tmp;

	if (!p2p->p2ps_prov) {
		p2p->p2ps_prov = os_zalloc(sizeof(struct p2ps_provision) + 1);
		if (!p2p->p2ps_prov)
			return -1;
	} else {
		os_memset(p2p->p2ps_prov, 0, sizeof(struct p2ps_provision) + 1);
	}

	tmp = p2p->p2ps_prov;
	tmp->adv_id = adv_id;
	tmp->session_id = session_id;
	tmp->method = method;
	os_memcpy(tmp->session_mac, session_mac, ETH_ALEN);
	os_memcpy(tmp->adv_mac, adv_mac, ETH_ALEN);
	tmp->info[0] = '\0';

	return 0;
}


static u8 p2ps_own_preferred_cpt(const u8 *cpt_priority, u8 req_cpt_mask)
{
	int i;

	for (i = 0; cpt_priority[i]; i++)
		if (req_cpt_mask & cpt_priority[i])
			return cpt_priority[i];

	return 0;
}


/* Check if the message contains a valid P2PS PD Request */
static int p2ps_validate_pd_req(struct p2p_data *p2p, struct p2p_message *msg,
				const u8 *addr)
{
	u8 group_id = 0;
	u8 intended_addr = 0;
	u8 operating_channel = 0;
	u8 channel_list = 0;
	u8 config_timeout = 0;
	u8 listen_channel = 0;

#define P2PS_PD_REQ_CHECK(_val, _attr) \
do { \
	if ((_val) && !msg->_attr) { \
		p2p_dbg(p2p, "Not P2PS PD Request. Missing %s", #_attr); \
		return -1; \
	} \
} while (0)

	P2PS_PD_REQ_CHECK(1, adv_id);
	P2PS_PD_REQ_CHECK(1, session_id);
	P2PS_PD_REQ_CHECK(1, session_mac);
	P2PS_PD_REQ_CHECK(1, adv_mac);
	P2PS_PD_REQ_CHECK(1, capability);
	P2PS_PD_REQ_CHECK(1, p2p_device_info);
	P2PS_PD_REQ_CHECK(1, feature_cap);

	/*
	 * We don't need to check Connection Capability, Persistent Group,
	 * and related attributes for follow-on PD Request with a status
	 * other than SUCCESS_DEFERRED.
	 */
	if (msg->status && *msg->status != P2P_SC_SUCCESS_DEFERRED)
		return 0;

	P2PS_PD_REQ_CHECK(1, conn_cap);

	/*
	 * Note 1: A feature capability attribute structure can be changed
	 * in the future. The assumption is that such modifications are
	 * backward compatible, therefore we allow processing of msg.feature_cap
	 * exceeding the size of the p2ps_feature_capab structure.
	 * Note 2: Verification of msg.feature_cap_len below has to be changed
	 * to allow 2 byte feature capability processing if
	 * struct p2ps_feature_capab is extended to include additional fields
	 * and it affects the structure size.
	 */
	if (msg->feature_cap_len < sizeof(struct p2ps_feature_capab)) {
		p2p_dbg(p2p, "P2PS: Invalid feature capability len");
		return -1;
	}

	switch (*msg->conn_cap) {
	case P2PS_SETUP_NEW:
		group_id = 1;
		intended_addr = 1;
		operating_channel = 1;
		channel_list = 1;
		config_timeout = 1;
		listen_channel = 1;
		break;
	case P2PS_SETUP_CLIENT:
		channel_list = 1;
		listen_channel = 1;
		break;
	case P2PS_SETUP_GROUP_OWNER:
		group_id = 1;
		intended_addr = 1;
		operating_channel = 1;
		break;
	case P2PS_SETUP_NEW | P2PS_SETUP_GROUP_OWNER:
		group_id = 1;
		operating_channel = 1;
		intended_addr = 1;
		channel_list = 1;
		config_timeout = 1;
		break;
	case P2PS_SETUP_CLIENT | P2PS_SETUP_GROUP_OWNER:
		group_id = 1;
		intended_addr = 1;
		operating_channel = 1;
		channel_list = 1;
		config_timeout = 1;
		break;
	default:
		p2p_dbg(p2p, "Invalid P2PS PD connection capability");
		return -1;
	}

	if (msg->persistent_dev) {
		channel_list = 1;
		config_timeout = 1;
		if (os_memcmp(msg->persistent_dev, addr, ETH_ALEN) == 0) {
			intended_addr = 1;
			operating_channel = 1;
		}
	}

	P2PS_PD_REQ_CHECK(group_id, group_id);
	P2PS_PD_REQ_CHECK(intended_addr, intended_addr);
	P2PS_PD_REQ_CHECK(operating_channel, operating_channel);
	P2PS_PD_REQ_CHECK(channel_list, channel_list);
	P2PS_PD_REQ_CHECK(config_timeout, config_timeout);
	P2PS_PD_REQ_CHECK(listen_channel, listen_channel);

#undef P2PS_PD_REQ_CHECK

	return 0;
}


void p2p_process_prov_disc_req(struct p2p_data *p2p, const u8 *sa,
			       const u8 *data, size_t len, int rx_freq)
{
	struct p2p_message msg;
	struct p2p_device *dev;
	int freq;
	enum p2p_status_code reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
	struct wpabuf *resp;
	u32 adv_id = 0;
	struct p2ps_advertisement *p2ps_adv = NULL;
	u8 conncap = P2PS_SETUP_NEW;
	u8 auto_accept = 0;
	u32 session_id = 0;
	u8 session_mac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
	u8 adv_mac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
	const u8 *group_mac;
	int passwd_id = DEV_PW_DEFAULT;
	u16 config_methods;
	u16 allowed_config_methods = WPS_CONFIG_DISPLAY | WPS_CONFIG_KEYPAD;
	struct p2ps_feature_capab resp_fcap = { 0, 0 };
	struct p2ps_feature_capab *req_fcap = NULL;
	u8 remote_conncap;
	u16 method;

	if (p2p_parse(data, len, &msg))
		return;

	p2p_dbg(p2p, "Received Provision Discovery Request from " MACSTR
		" with config methods 0x%x (freq=%d)",
		MAC2STR(sa), msg.wps_config_methods, rx_freq);
	group_mac = msg.intended_addr;

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		p2p_dbg(p2p, "Provision Discovery Request from unknown peer "
			MACSTR, MAC2STR(sa));

		if (p2p_add_device(p2p, sa, rx_freq, NULL, 0, data + 1, len - 1,
				   0)) {
			p2p_dbg(p2p, "Provision Discovery Request add device failed "
				MACSTR, MAC2STR(sa));
			goto out;
		}

		if (!dev) {
			dev = p2p_get_device(p2p, sa);
			if (!dev) {
				p2p_dbg(p2p,
					"Provision Discovery device not found "
					MACSTR, MAC2STR(sa));
				goto out;
			}
		}
	} else if (msg.wfd_subelems) {
		wpabuf_free(dev->info.wfd_subelems);
		dev->info.wfd_subelems = wpabuf_dup(msg.wfd_subelems);
	}

	if (!msg.adv_id) {
		allowed_config_methods |= WPS_CONFIG_PUSHBUTTON;
		if (!(msg.wps_config_methods & allowed_config_methods)) {
			p2p_dbg(p2p,
				"Unsupported Config Methods in Provision Discovery Request");
			goto out;
		}

		/* Legacy (non-P2PS) - Unknown groups allowed for P2PS */
		if (msg.group_id) {
			size_t i;

			for (i = 0; i < p2p->num_groups; i++) {
				if (p2p_group_is_group_id_match(
					    p2p->groups[i],
					    msg.group_id, msg.group_id_len))
					break;
			}
			if (i == p2p->num_groups) {
				p2p_dbg(p2p,
					"PD request for unknown P2P Group ID - reject");
				goto out;
			}
		}
	} else {
		allowed_config_methods |= WPS_CONFIG_P2PS;

		/*
		 * Set adv_id here, so in case of an error, a P2PS PD Response
		 * will be sent.
		 */
		adv_id = WPA_GET_LE32(msg.adv_id);
		if (p2ps_validate_pd_req(p2p, &msg, sa) < 0) {
			reject = P2P_SC_FAIL_INVALID_PARAMS;
			goto out;
		}

		req_fcap = (struct p2ps_feature_capab *) msg.feature_cap;

		os_memcpy(session_mac, msg.session_mac, ETH_ALEN);
		os_memcpy(adv_mac, msg.adv_mac, ETH_ALEN);

		session_id = WPA_GET_LE32(msg.session_id);

		if (msg.conn_cap)
			conncap = *msg.conn_cap;

		/*
		 * We need to verify a P2PS config methog in an initial PD
		 * request or in a follow-on PD request with the status
		 * SUCCESS_DEFERRED.
		 */
		if ((!msg.status || *msg.status == P2P_SC_SUCCESS_DEFERRED) &&
		    !(msg.wps_config_methods & allowed_config_methods)) {
			p2p_dbg(p2p,
				"Unsupported Config Methods in Provision Discovery Request");
			goto out;
		}

		/*
		 * TODO: since we don't support multiple PD, reject PD request
		 * if we are in the middle of P2PS PD with some other peer
		 */
	}

	dev->flags &= ~(P2P_DEV_PD_PEER_DISPLAY |
			P2P_DEV_PD_PEER_KEYPAD |
			P2P_DEV_PD_PEER_P2PS);

	if (msg.wps_config_methods & WPS_CONFIG_DISPLAY) {
		p2p_dbg(p2p, "Peer " MACSTR
			" requested us to show a PIN on display", MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_KEYPAD;
		passwd_id = DEV_PW_USER_SPECIFIED;
	} else if (msg.wps_config_methods & WPS_CONFIG_KEYPAD) {
		p2p_dbg(p2p, "Peer " MACSTR
			" requested us to write its PIN using keypad",
			MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_DISPLAY;
		passwd_id = DEV_PW_REGISTRAR_SPECIFIED;
	} else if (msg.wps_config_methods & WPS_CONFIG_P2PS) {
		p2p_dbg(p2p, "Peer " MACSTR " requesting P2PS PIN",
			MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_P2PS;
		passwd_id = DEV_PW_P2PS_DEFAULT;
	}

	/* Remove stale persistent groups */
	if (p2p->cfg->remove_stale_groups) {
		p2p->cfg->remove_stale_groups(
			p2p->cfg->cb_ctx, dev->info.p2p_device_addr,
			msg.persistent_dev,
			msg.persistent_ssid, msg.persistent_ssid_len);
	}

	reject = P2P_SC_SUCCESS;

	/*
	 * End of a legacy P2P PD Request processing, from this point continue
	 * with P2PS one.
	 */
	if (!msg.adv_id)
		goto out;

	remote_conncap = conncap;

	if (!msg.status) {
		unsigned int forced_freq, pref_freq;

		if (os_memcmp(p2p->cfg->dev_addr, msg.adv_mac, ETH_ALEN)) {
			p2p_dbg(p2p,
				"P2PS PD adv mac does not match the local one");
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
			goto out;
		}

		p2ps_adv = p2p_service_p2ps_id(p2p, adv_id);
		if (!p2ps_adv) {
			p2p_dbg(p2p, "P2PS PD invalid adv_id=0x%X", adv_id);
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
			goto out;
		}
		p2p_dbg(p2p, "adv_id: 0x%X, p2ps_adv: %p", adv_id, p2ps_adv);

		auto_accept = p2ps_adv->auto_accept;
		conncap = p2p->cfg->p2ps_group_capability(p2p->cfg->cb_ctx,
							  conncap, auto_accept,
							  &forced_freq,
							  &pref_freq);

		p2p_dbg(p2p, "Conncap: local:%d remote:%d result:%d",
			auto_accept, remote_conncap, conncap);

		p2p_prepare_channel(p2p, dev, forced_freq, pref_freq, 0);

		resp_fcap.cpt = p2ps_own_preferred_cpt(p2ps_adv->cpt_priority,
						       req_fcap->cpt);

		p2p_dbg(p2p, "cpt: service:0x%x remote:0x%x result:0x%x",
			p2ps_adv->cpt_mask, req_fcap->cpt, resp_fcap.cpt);

		if (!resp_fcap.cpt) {
			p2p_dbg(p2p,
				"Incompatible P2PS feature capability CPT bitmask");
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
		} else if (p2ps_adv->config_methods &&
			   !(msg.wps_config_methods &
			     p2ps_adv->config_methods)) {
			p2p_dbg(p2p,
				"Unsupported config methods in Provision Discovery Request (own=0x%x peer=0x%x)",
				p2ps_adv->config_methods,
				msg.wps_config_methods);
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
		} else if (!p2ps_adv->state) {
			p2p_dbg(p2p, "P2PS state unavailable");
			reject = P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
		} else if (!conncap) {
			p2p_dbg(p2p, "Conncap resolution failed");
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
		}

		if (msg.wps_config_methods & WPS_CONFIG_KEYPAD) {
			p2p_dbg(p2p, "Keypad - always defer");
			auto_accept = 0;
		}

		if ((remote_conncap & (P2PS_SETUP_NEW | P2PS_SETUP_CLIENT) ||
		     msg.persistent_dev) && conncap != P2PS_SETUP_NEW &&
		    msg.channel_list && msg.channel_list_len &&
		    p2p_peer_channels_check(p2p, &p2p->channels, dev,
					    msg.channel_list,
					    msg.channel_list_len) < 0) {
			p2p_dbg(p2p,
				"No common channels - force deferred flow");
			auto_accept = 0;
		}

		if (((remote_conncap & P2PS_SETUP_GROUP_OWNER) ||
		     msg.persistent_dev) && msg.operating_channel) {
			struct p2p_channels intersect;

			/*
			 * There are cases where only the operating channel is
			 * provided. This requires saving the channel as the
			 * supported channel list, and verifying that it is
			 * supported.
			 */
			if (dev->channels.reg_classes == 0 ||
			    !p2p_channels_includes(&dev->channels,
						   msg.operating_channel[3],
						   msg.operating_channel[4])) {
				struct p2p_channels *ch = &dev->channels;

				os_memset(ch, 0, sizeof(*ch));
				ch->reg_class[0].reg_class =
					msg.operating_channel[3];
				ch->reg_class[0].channel[0] =
					msg.operating_channel[4];
				ch->reg_class[0].channels = 1;
				ch->reg_classes = 1;
			}

			p2p_channels_intersect(&p2p->channels, &dev->channels,
					       &intersect);

			if (intersect.reg_classes == 0) {
				p2p_dbg(p2p,
					"No common channels - force deferred flow");
				auto_accept = 0;
			}
		}

		if (auto_accept || reject != P2P_SC_SUCCESS) {
			struct p2ps_provision *tmp;

			if (p2ps_setup_p2ps_prov(p2p, adv_id, session_id,
						 msg.wps_config_methods,
						 session_mac, adv_mac) < 0) {
				reject = P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
				goto out;
			}

			tmp = p2p->p2ps_prov;
			tmp->force_freq = forced_freq;
			tmp->pref_freq = pref_freq;
			if (conncap) {
				tmp->conncap = conncap;
				tmp->status = P2P_SC_SUCCESS;
			} else {
				tmp->conncap = auto_accept;
				tmp->status = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
			}

			if (reject != P2P_SC_SUCCESS)
				goto out;
		}
	}

	if (!msg.status && !auto_accept &&
	    (!p2p->p2ps_prov || p2p->p2ps_prov->adv_id != adv_id)) {
		struct p2ps_provision *tmp;

		if (!conncap) {
			reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
			goto out;
		}

		if (p2ps_setup_p2ps_prov(p2p, adv_id, session_id,
					 msg.wps_config_methods,
					 session_mac, adv_mac) < 0) {
			reject = P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE;
			goto out;
		}
		tmp = p2p->p2ps_prov;
		reject = P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE;
		tmp->status = reject;
	}

	/* Not a P2PS Follow-on PD */
	if (!msg.status)
		goto out;

	if (*msg.status && *msg.status != P2P_SC_SUCCESS_DEFERRED) {
		reject = *msg.status;
		goto out;
	}

	if (*msg.status != P2P_SC_SUCCESS_DEFERRED || !p2p->p2ps_prov)
		goto out;

	if (p2p->p2ps_prov->adv_id != adv_id ||
	    os_memcmp(p2p->p2ps_prov->adv_mac, msg.adv_mac, ETH_ALEN)) {
		p2p_dbg(p2p,
			"P2PS Follow-on PD with mismatch Advertisement ID/MAC");
		goto out;
	}

	if (p2p->p2ps_prov->session_id != session_id ||
	    os_memcmp(p2p->p2ps_prov->session_mac, msg.session_mac, ETH_ALEN)) {
		p2p_dbg(p2p, "P2PS Follow-on PD with mismatch Session ID/MAC");
		goto out;
	}

	method = p2p->p2ps_prov->method;

	conncap = p2p->cfg->p2ps_group_capability(p2p->cfg->cb_ctx,
						  remote_conncap,
						  p2p->p2ps_prov->conncap,
						  &p2p->p2ps_prov->force_freq,
						  &p2p->p2ps_prov->pref_freq);

	resp_fcap.cpt = p2ps_own_preferred_cpt(p2p->p2ps_prov->cpt_priority,
					       req_fcap->cpt);

	p2p_dbg(p2p, "cpt: local:0x%x remote:0x%x result:0x%x",
		p2p->p2ps_prov->cpt_mask, req_fcap->cpt, resp_fcap.cpt);

	p2p_prepare_channel(p2p, dev, p2p->p2ps_prov->force_freq,
			    p2p->p2ps_prov->pref_freq, 0);

	/*
	 * Ensure that if we asked for PIN originally, our method is consistent
	 * with original request.
	 */
	if (method & WPS_CONFIG_DISPLAY)
		method = WPS_CONFIG_KEYPAD;
	else if (method & WPS_CONFIG_KEYPAD)
		method = WPS_CONFIG_DISPLAY;

	if (!conncap || !(msg.wps_config_methods & method)) {
		/*
		 * Reject this "Deferred Accept*
		 * if incompatible conncap or method
		 */
		reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
	} else if (!resp_fcap.cpt) {
		p2p_dbg(p2p,
			"Incompatible P2PS feature capability CPT bitmask");
		reject = P2P_SC_FAIL_INCOMPATIBLE_PARAMS;
	} else if ((remote_conncap & (P2PS_SETUP_NEW | P2PS_SETUP_CLIENT) ||
		    msg.persistent_dev) && conncap != P2PS_SETUP_NEW &&
		   msg.channel_list && msg.channel_list_len &&
		   p2p_peer_channels_check(p2p, &p2p->channels, dev,
					   msg.channel_list,
					   msg.channel_list_len) < 0) {
		p2p_dbg(p2p,
			"No common channels in Follow-On Provision Discovery Request");
		reject = P2P_SC_FAIL_NO_COMMON_CHANNELS;
	} else {
		reject = P2P_SC_SUCCESS;
	}

	dev->oper_freq = 0;
	if (reject == P2P_SC_SUCCESS || reject == P2P_SC_SUCCESS_DEFERRED) {
		u8 tmp;

		if (msg.operating_channel)
			dev->oper_freq =
				p2p_channel_to_freq(msg.operating_channel[3],
						    msg.operating_channel[4]);

		if ((conncap & P2PS_SETUP_GROUP_OWNER) &&
		    p2p_go_select_channel(p2p, dev, &tmp) < 0)
			reject = P2P_SC_FAIL_NO_COMMON_CHANNELS;
	}

	p2p->p2ps_prov->status = reject;
	p2p->p2ps_prov->conncap = conncap;

out:
	if (reject == P2P_SC_SUCCESS ||
	    reject == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE)
		config_methods = msg.wps_config_methods;
	else
		config_methods = 0;

	/*
	 * Send PD Response for an initial PD Request or for follow-on
	 * PD Request with P2P_SC_SUCCESS_DEFERRED status.
	 */
	if (!msg.status || *msg.status == P2P_SC_SUCCESS_DEFERRED) {
		resp = p2p_build_prov_disc_resp(p2p, dev, msg.dialog_token,
						reject, config_methods, adv_id,
						msg.group_id, msg.group_id_len,
						msg.persistent_ssid,
						msg.persistent_ssid_len,
						(const u8 *) &resp_fcap,
						sizeof(resp_fcap));
		if (!resp) {
			p2p_parse_free(&msg);
			return;
		}
		p2p_dbg(p2p, "Sending Provision Discovery Response");
		if (rx_freq > 0)
			freq = rx_freq;
		else
			freq = p2p_channel_to_freq(p2p->cfg->reg_class,
						   p2p->cfg->channel);
		if (freq < 0) {
			p2p_dbg(p2p, "Unknown regulatory class/channel");
			wpabuf_free(resp);
			p2p_parse_free(&msg);
			return;
		}
		p2p->pending_action_state = P2P_PENDING_PD_RESPONSE;
		if (p2p_send_action(p2p, freq, sa, p2p->cfg->dev_addr,
				    p2p->cfg->dev_addr,
				    wpabuf_head(resp), wpabuf_len(resp),
				    50) < 0)
			p2p_dbg(p2p, "Failed to send Action frame");
		else
			p2p->send_action_in_progress = 1;

		wpabuf_free(resp);
	}

	if (!dev) {
		p2p_parse_free(&msg);
		return;
	}

	freq = 0;
	if (reject == P2P_SC_SUCCESS && conncap == P2PS_SETUP_GROUP_OWNER) {
		freq = p2p_channel_to_freq(p2p->op_reg_class,
					   p2p->op_channel);
		if (freq < 0)
			freq = 0;
	}

	if (!p2p->cfg->p2ps_prov_complete) {
		/* Don't emit anything */
	} else if (msg.status && *msg.status != P2P_SC_SUCCESS &&
		   *msg.status != P2P_SC_SUCCESS_DEFERRED) {
		reject = *msg.status;
		p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx, reject,
					     sa, adv_mac, session_mac,
					     NULL, adv_id, session_id,
					     0, 0, msg.persistent_ssid,
					     msg.persistent_ssid_len,
					     0, 0, NULL, NULL, 0, freq,
					     NULL, 0);
	} else if (msg.status && *msg.status == P2P_SC_SUCCESS_DEFERRED &&
		   p2p->p2ps_prov) {
		p2p->p2ps_prov->status = reject;
		p2p->p2ps_prov->conncap = conncap;

		if (reject != P2P_SC_SUCCESS)
			p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx, reject,
						     sa, adv_mac, session_mac,
						     NULL, adv_id,
						     session_id, conncap, 0,
						     msg.persistent_ssid,
						     msg.persistent_ssid_len, 0,
						     0, NULL, NULL, 0, freq,
						     NULL, 0);
		else
			p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx,
						     *msg.status,
						     sa, adv_mac, session_mac,
						     group_mac, adv_id,
						     session_id, conncap,
						     passwd_id,
						     msg.persistent_ssid,
						     msg.persistent_ssid_len, 0,
						     0, NULL,
						     (const u8 *) &resp_fcap,
						     sizeof(resp_fcap), freq,
						     NULL, 0);
	} else if (msg.status && p2p->p2ps_prov) {
		p2p->p2ps_prov->status = P2P_SC_SUCCESS;
		p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx, *msg.status, sa,
					     adv_mac, session_mac, group_mac,
					     adv_id, session_id, conncap,
					     passwd_id,
					     msg.persistent_ssid,
					     msg.persistent_ssid_len,
					     0, 0, NULL,
					     (const u8 *) &resp_fcap,
					     sizeof(resp_fcap), freq, NULL, 0);
	} else if (msg.status) {
	} else if (auto_accept && reject == P2P_SC_SUCCESS) {
		p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx, P2P_SC_SUCCESS,
					     sa, adv_mac, session_mac,
					     group_mac, adv_id, session_id,
					     conncap, passwd_id,
					     msg.persistent_ssid,
					     msg.persistent_ssid_len,
					     0, 0, NULL,
					     (const u8 *) &resp_fcap,
					     sizeof(resp_fcap), freq,
					     msg.group_id ?
					     msg.group_id + ETH_ALEN : NULL,
					     msg.group_id ?
					     msg.group_id_len - ETH_ALEN : 0);
	} else if (reject == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE &&
		   (!msg.session_info || !msg.session_info_len)) {
		p2p->p2ps_prov->method = msg.wps_config_methods;

		p2p->cfg->p2ps_prov_complete(p2p->cfg->cb_ctx, P2P_SC_SUCCESS,
					     sa, adv_mac, session_mac,
					     group_mac, adv_id, session_id,
					     conncap, passwd_id,
					     msg.persistent_ssid,
					     msg.persistent_ssid_len,
					     0, 1, NULL,
					     (const u8 *) &resp_fcap,
					     sizeof(resp_fcap), freq, NULL, 0);
	} else if (reject == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE) {
		size_t buf_len = msg.session_info_len;
		char *buf = os_malloc(2 * buf_len + 1);

		if (buf) {
			p2p->p2ps_prov->method = msg.wps_config_methods;

			utf8_escape((char *) msg.session_info, buf_len,
				    buf, 2 * buf_len + 1);

			p2p->cfg->p2ps_prov_complete(
				p2p->cfg->cb_ctx, P2P_SC_SUCCESS, sa,
				adv_mac, session_mac, group_mac, adv_id,
				session_id, conncap, passwd_id,
				msg.persistent_ssid, msg.persistent_ssid_len,
				0, 1, buf,
				(const u8 *) &resp_fcap, sizeof(resp_fcap),
				freq, NULL, 0);

			os_free(buf);
		}
	}

	/*
	 * prov_disc_req callback is used to generate P2P-PROV-DISC-ENTER-PIN,
	 * P2P-PROV-DISC-SHOW-PIN, and P2P-PROV-DISC-PBC-REQ events.
	 * Call it either on legacy P2P PD or on P2PS PD only if we need to
	 * enter/show PIN.
	 *
	 * The callback is called in the following cases:
	 * 1. Legacy P2P PD request, response status SUCCESS
	 * 2. P2PS advertiser, method: DISPLAY, autoaccept: TRUE,
	 *    response status: SUCCESS
	 * 3. P2PS advertiser, method  DISPLAY, autoaccept: FALSE,
	 *    response status: INFO_CURRENTLY_UNAVAILABLE
	 * 4. P2PS advertiser, method: KEYPAD, autoaccept==any,
	 *    response status: INFO_CURRENTLY_UNAVAILABLE
	 * 5. P2PS follow-on with SUCCESS_DEFERRED,
	 *    advertiser role: DISPLAY, autoaccept: FALSE,
	 *    seeker: KEYPAD, response status: SUCCESS
	 */
	if (p2p->cfg->prov_disc_req &&
	    ((reject == P2P_SC_SUCCESS && !msg.adv_id) ||
	     (!msg.status &&
	     (reject == P2P_SC_SUCCESS ||
	      reject == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE) &&
	      passwd_id == DEV_PW_USER_SPECIFIED) ||
	     (!msg.status &&
	      reject == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE &&
	      passwd_id == DEV_PW_REGISTRAR_SPECIFIED) ||
	     (reject == P2P_SC_SUCCESS &&
	      msg.status && *msg.status == P2P_SC_SUCCESS_DEFERRED &&
	       passwd_id == DEV_PW_REGISTRAR_SPECIFIED))) {
		const u8 *dev_addr = sa;

		if (msg.p2p_device_addr)
			dev_addr = msg.p2p_device_addr;
		p2p->cfg->prov_disc_req(p2p->cfg->cb_ctx, sa,
					msg.wps_config_methods,
					dev_addr, msg.pri_dev_type,
					msg.device_name, msg.config_methods,
					msg.capability ? msg.capability[0] : 0,
					msg.capability ? msg.capability[1] :
					0,
					msg.group_id, msg.group_id_len);
	}

	if (reject != P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE)
		p2ps_prov_free(p2p);

	if (reject == P2P_SC_SUCCESS) {
		switch (config_methods) {
		case WPS_CONFIG_DISPLAY:
			dev->wps_prov_info = WPS_CONFIG_KEYPAD;
			break;
		case WPS_CONFIG_KEYPAD:
			dev->wps_prov_info = WPS_CONFIG_DISPLAY;
			break;
		case WPS_CONFIG_PUSHBUTTON:
			dev->wps_prov_info = WPS_CONFIG_PUSHBUTTON;
			break;
		case WPS_CONFIG_P2PS:
			dev->wps_prov_info = WPS_CONFIG_P2PS;
			break;
		default:
			dev->wps_prov_info = 0;
			break;
		}

		if (msg.intended_addr)
			os_memcpy(dev->interface_addr, msg.intended_addr,
				  ETH_ALEN);
	}
	p2p_parse_free(&msg);
}


static int p2p_validate_p2ps_pd_resp(struct p2p_data *p2p,
				     struct p2p_message *msg)
{
	u8 conn_cap_go = 0;
	u8 conn_cap_cli = 0;
	u32 session_id;
	u32 adv_id;

#define P2PS_PD_RESP_CHECK(_val, _attr) \
	do { \
		if ((_val) && !msg->_attr) { \
			p2p_dbg(p2p, "P2PS PD Response missing " #_attr); \
			return -1; \
		} \
	} while (0)

	P2PS_PD_RESP_CHECK(1, status);
	P2PS_PD_RESP_CHECK(1, adv_id);
	P2PS_PD_RESP_CHECK(1, adv_mac);
	P2PS_PD_RESP_CHECK(1, capability);
	P2PS_PD_RESP_CHECK(1, p2p_device_info);
	P2PS_PD_RESP_CHECK(1, session_id);
	P2PS_PD_RESP_CHECK(1, session_mac);
	P2PS_PD_RESP_CHECK(1, feature_cap);

	session_id = WPA_GET_LE32(msg->session_id);
	adv_id = WPA_GET_LE32(msg->adv_id);

	if (p2p->p2ps_prov->session_id != session_id) {
		p2p_dbg(p2p,
			"Ignore PD Response with unexpected Session ID");
		return -1;
	}

	if (os_memcmp(p2p->p2ps_prov->session_mac, msg->session_mac,
		      ETH_ALEN)) {
		p2p_dbg(p2p,
			"Ignore PD Response with unexpected Session MAC");
		return -1;
	}

	if (p2p->p2ps_prov->adv_id != adv_id) {
		p2p_dbg(p2p,
			"Ignore PD Response with unexpected Advertisement ID");
		return -1;
	}

	if (os_memcmp(p2p->p2ps_prov->adv_mac, msg->adv_mac, ETH_ALEN) != 0) {
		p2p_dbg(p2p,
			"Ignore PD Response with unexpected Advertisement MAC");
		return -1;
	}

	if (msg->listen_channel) {
		p2p_dbg(p2p,
			"Ignore malformed PD Response - unexpected Listen Channel");
		return -1;
	}

	if (*msg->status == P2P_SC_SUCCESS &&
	    !(!!msg->conn_cap ^ !!msg->persistent_dev)) {
		p2p_dbg(p2p,
			"Ignore malformed PD Response - either conn_cap or persistent group should be present");
		return -1;
	}

	if (msg->persistent_dev && *msg->status != P2P_SC_SUCCESS) {
		p2p_dbg(p2p,
			"Ignore malformed PD Response - persistent group is present, but the status isn't success");
		return -1;
	}

	if (msg->conn_cap) {
		conn_cap_go = *msg->conn_cap == P2PS_SETUP_GROUP_OWNER;
		conn_cap_cli = *msg->conn_cap == P2PS_SETUP_CLIENT;
	}

	P2PS_PD_RESP_CHECK(msg->persistent_dev || conn_cap_go || conn_cap_cli,
			   channel_list);
	P2PS_PD_RESP_CHECK(msg->persistent_dev || conn_cap_go || conn_cap_cli,
			   config_timeout);

	P2PS_PD_RESP_CHECK(conn_cap_go, group_id);
	P2PS_PD_RESP_CHECK(conn_cap_go, intended_addr);
	P2PS_PD_RESP_CHECK(conn_cap_go, operating_channel);
	/*
	 * TODO: Also validate that operating channel is present if the device
	 * is a GO in a persistent group. We can't do it here since we don't
	 * know what is the role of the peer. It should be probably done in
	 * p2ps_prov_complete callback, but currently operating channel isn't
	 * passed to it.
	 */

#undef P2PS_PD_RESP_CHECK

	return 0;
}


void p2p_process_prov_disc_resp(struct p2p_data *p2p, const u8 *sa,
				const u8 *data, size_t len)
{
	struct p2p_message msg;
	struct p2p_device *dev;
	u16 report_config_methods = 0, req_config_methods;
	u8 status = P2P_SC_SUCCESS;
	u32 adv_id = 0;
	u8 conncap = P2PS_SETUP_NEW;
	u8 adv_mac[ETH_ALEN];
	const u8 *group_mac;
	int passwd_id = DEV_PW_DEFAULT;
	int p2ps_seeker;

	if (p2p_parse(data, len, &msg))
		return;

	if (p2p->p2ps_prov && p2p_validate_p2ps_pd_resp(p2p, &msg)) {
		p2p_parse_free(&msg);
		return;
	}

	/* Parse the P2PS members present */
	if (msg.status)
		status = *msg.status;

	group_mac = msg.intended_addr;

	if (msg.adv_mac)
		os_memcpy(adv_mac, msg.adv_mac, ETH_ALEN);
	else
		os_memset(adv_mac, 0, ETH_ALEN);

	if (msg.adv_id)
		adv_id = WPA_GET_LE32(msg.adv_id);

	if (msg.conn_cap) {
		conncap = *msg.conn_cap;

		/* Switch bits to local relative */
		switch (conncap) {
		case P2PS_SETUP_GROUP_OWNER:
			conncap = P2PS_SETUP_CLIENT;
			break;
		case P2PS_SETUP_CLIENT:
			conncap = P2PS_SETUP_GROUP_OWNER;
			break;
		}
	}

	p2p_dbg(p2p, "Received Provision Discovery Response from " MACSTR
		" with config methods 0x%x",
		MAC2STR(sa), msg.wps_config_methods);

	dev = p2p_get_device(p2p, sa);
	if (dev == NULL || !dev->req_config_methods) {
		p2p_dbg(p2p, "Ignore Provision Discovery Response from " MACSTR
			" with no pending request", MAC2STR(sa));
		p2p_parse_free(&msg);
		return;
	} else if (msg.wfd_subelems) {
		wpabuf_free(dev->info.wfd_subelems);
		dev->info.wfd_subelems = wpabuf_dup(msg.wfd_subelems);
	}

	if (dev->dialog_token != msg.dialog_token) {
		p2p_dbg(p2p, "Ignore Provision Discovery Response with unexpected Dialog Token %u (expected %u)",
			msg.dialog_token, dev->dialog_token);
		p2p_parse_free(&msg);
		return;
	}

	if (p2p->pending_action_state == P2P_PENDING_PD) {
		os_memset(p2p->pending_pd_devaddr, 0, ETH_ALEN);
		p2p->pending_action_state = P2P_NO_PENDING_ACTION;
	}

	p2ps_seeker = p2p->p2ps_prov && p2p->p2ps_prov->pd_seeker;

	/*
	 * Use a local copy of the requested config methods since
	 * p2p_reset_pending_pd() can clear this in the peer entry.
	 */
	req_config_methods = dev->req_config_methods;

	/*
	 * If the response is from the peer to whom a user initiated request
	 * was sent earlier, we reset that state info here.
	 */
	if (p2p->user_initiated_pd &&
	    os_memcmp(p2p->pending_pd_devaddr, sa, ETH_ALEN) == 0)
		p2p_reset_pending_pd(p2p);

	if (msg.wps_config_methods != req_config_methods) {
		p2p_dbg(p2p, "Peer rejected our Provision Discovery Request (received config_methods 0x%x expected 0x%x",
			msg.wps_config_methods, req_config_methods);
		if (p2p->cfg->prov_disc_fail)
			p2p->cfg->prov_disc_fail(p2p->cfg->cb_ctx, sa,
						 P2P_PROV_DISC_REJECTED,
						 adv_id, adv_mac, NULL);
		p2p_parse_free(&msg);
		p2ps_prov_free(p2p);
		goto out;
	}

	report_config_methods = req_config_methods;
	dev->flags &= ~(P2P_DEV_PD_PEER_DISPLAY |
			P2P_DEV_PD_PEER_KEYPAD |
			P2P_DEV_PD_PEER_P2PS);
	if (req_config_methods & WPS_CONFIG_DISPLAY) {
		p2p_dbg(p2p, "Peer " MACSTR
			" accepted to show a PIN on display", MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_DISPLAY;
		passwd_id = DEV_PW_REGISTRAR_SPECIFIED;
	} else if (msg.wps_config_methods & WPS_CONFIG_KEYPAD) {
		p2p_dbg(p2p, "Peer " MACSTR
			" accepted to write our PIN using keypad",
			MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_KEYPAD;
		passwd_id = DEV_PW_USER_SPECIFIED;
	} else if (msg.wps_config_methods & WPS_CONFIG_P2PS) {
		p2p_dbg(p2p, "Peer " MACSTR " accepted P2PS PIN",
			MAC2STR(sa));
		dev->flags |= P2P_DEV_PD_PEER_P2PS;
		passwd_id = DEV_PW_P2PS_DEFAULT;
	}

	if ((status == P2P_SC_SUCCESS || status == P2P_SC_SUCCESS_DEFERRED) &&
	    p2p->p2ps_prov) {
		dev->oper_freq = 0;

		/*
		 * Save the reported channel list and operating frequency.
		 * Note that the specification mandates that the responder
		 * should include in the channel list only channels reported by
		 * the initiator, so this is only a sanity check, and if this
		 * fails the flow would continue, although it would probably
		 * fail. Same is true for the operating channel.
		 */
		if (msg.channel_list && msg.channel_list_len &&
		    p2p_peer_channels_check(p2p, &p2p->channels, dev,
					    msg.channel_list,
					    msg.channel_list_len) < 0)
			p2p_dbg(p2p, "P2PS PD Response - no common channels");

		if (msg.operating_channel) {
			if (p2p_channels_includes(&p2p->channels,
						  msg.operating_channel[3],
						  msg.operating_channel[4]) &&
			    p2p_channels_includes(&dev->channels,
						  msg.operating_channel[3],
						  msg.operating_channel[4])) {
				dev->oper_freq =
					p2p_channel_to_freq(
						msg.operating_channel[3],
						msg.operating_channel[4]);
			} else {
				p2p_dbg(p2p,
					"P2PS PD Response - invalid operating channel");
			}
		}

		if (p2p->cfg->p2ps_prov_complete) {
			int freq = 0;

			if (conncap == P2PS_SETUP_GROUP_OWNER) {
				u8 tmp;

				/*
				 * Re-select the operating channel as it is
				 * possible that original channel is no longer
				 * valid. This should not really fail.
				 */
				if (p2p_go_select_channel(p2p, dev, &tmp) < 0)
					p2p_dbg(p2p,
						"P2PS PD channel selection failed");

				freq = p2p_channel_to_freq(p2p->op_reg_class,
							   p2p->op_channel);
				if (freq < 0)
					freq = 0;
			}

			p2p->cfg->p2ps_prov_complete(
				p2p->cfg->cb_ctx, status, sa, adv_mac,
				p2p->p2ps_prov->session_mac,
				group_mac, adv_id, p2p->p2ps_prov->session_id,
				conncap, passwd_id, msg.persistent_ssid,
				msg.persistent_ssid_len, 1, 0, NULL,
				msg.feature_cap, msg.feature_cap_len, freq,
				msg.group_id ? msg.group_id + ETH_ALEN : NULL,
				msg.group_id ? msg.group_id_len - ETH_ALEN : 0);
		}
		p2ps_prov_free(p2p);
	} else if (status != P2P_SC_SUCCESS &&
		   status != P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE &&
		   status != P2P_SC_SUCCESS_DEFERRED && p2p->p2ps_prov) {
		if (p2p->cfg->p2ps_prov_complete)
			p2p->cfg->p2ps_prov_complete(
				p2p->cfg->cb_ctx, status, sa, adv_mac,
				p2p->p2ps_prov->session_mac,
				group_mac, adv_id, p2p->p2ps_prov->session_id,
				0, 0, NULL, 0, 1, 0, NULL, NULL, 0, 0, NULL, 0);
		p2ps_prov_free(p2p);
	}

	if (status == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE) {
		if (p2p->cfg->remove_stale_groups) {
			p2p->cfg->remove_stale_groups(p2p->cfg->cb_ctx,
						      dev->info.p2p_device_addr,
						      NULL, NULL, 0);
		}

		if (msg.session_info && msg.session_info_len) {
			size_t info_len = msg.session_info_len;
			char *deferred_sess_resp = os_malloc(2 * info_len + 1);

			if (!deferred_sess_resp) {
				p2p_parse_free(&msg);
				p2ps_prov_free(p2p);
				goto out;
			}
			utf8_escape((char *) msg.session_info, info_len,
				    deferred_sess_resp, 2 * info_len + 1);

			if (p2p->cfg->prov_disc_fail)
				p2p->cfg->prov_disc_fail(
					p2p->cfg->cb_ctx, sa,
					P2P_PROV_DISC_INFO_UNAVAILABLE,
					adv_id, adv_mac,
					deferred_sess_resp);
			os_free(deferred_sess_resp);
		} else
			if (p2p->cfg->prov_disc_fail)
				p2p->cfg->prov_disc_fail(
					p2p->cfg->cb_ctx, sa,
					P2P_PROV_DISC_INFO_UNAVAILABLE,
					adv_id, adv_mac, NULL);
	} else if (status != P2P_SC_SUCCESS) {
		p2p_dbg(p2p, "Peer rejected our Provision Discovery Request");
		if (p2p->cfg->prov_disc_fail)
			p2p->cfg->prov_disc_fail(p2p->cfg->cb_ctx, sa,
						 P2P_PROV_DISC_REJECTED,
						 adv_id, adv_mac, NULL);
		p2p_parse_free(&msg);
		p2ps_prov_free(p2p);
		goto out;
	}

	/* Store the provisioning info */
	dev->wps_prov_info = msg.wps_config_methods;
	if (msg.intended_addr)
		os_memcpy(dev->interface_addr, msg.intended_addr, ETH_ALEN);

	p2p_parse_free(&msg);

out:
	dev->req_config_methods = 0;
	p2p->cfg->send_action_done(p2p->cfg->cb_ctx);
	if (dev->flags & P2P_DEV_PD_BEFORE_GO_NEG) {
		p2p_dbg(p2p, "Start GO Neg after the PD-before-GO-Neg workaround with "
			MACSTR, MAC2STR(dev->info.p2p_device_addr));
		dev->flags &= ~P2P_DEV_PD_BEFORE_GO_NEG;
		p2p_connect_send(p2p, dev);
		return;
	}

	/*
	 * prov_disc_resp callback is used to generate P2P-PROV-DISC-ENTER-PIN,
	 * P2P-PROV-DISC-SHOW-PIN, and P2P-PROV-DISC-PBC-REQ events.
	 * Call it only for a legacy P2P PD or for P2PS PD scenarios where
	 * show/enter PIN events are needed.
	 *
	 * The callback is called in the following cases:
	 * 1. Legacy P2P PD response with a status SUCCESS
	 * 2. P2PS, advertiser method: DISPLAY, autoaccept: true,
	 *    response status: SUCCESS, local method KEYPAD
	 * 3. P2PS, advertiser method: KEYPAD,Seeker side,
	 *    response status: INFO_CURRENTLY_UNAVAILABLE,
	 *    local method: DISPLAY
	 */
	if (p2p->cfg->prov_disc_resp &&
	    ((status == P2P_SC_SUCCESS && !adv_id) ||
	     (p2ps_seeker && status == P2P_SC_SUCCESS &&
	      passwd_id == DEV_PW_REGISTRAR_SPECIFIED) ||
	     (p2ps_seeker &&
	      status == P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE &&
	      passwd_id == DEV_PW_USER_SPECIFIED)))
		p2p->cfg->prov_disc_resp(p2p->cfg->cb_ctx, sa,
					 report_config_methods);

	if (p2p->state == P2P_PD_DURING_FIND) {
		p2p_stop_listen_for_freq(p2p, 0);
		p2p_continue_find(p2p);
	}
}


int p2p_send_prov_disc_req(struct p2p_data *p2p, struct p2p_device *dev,
			   int join, int force_freq)
{
	struct wpabuf *req;
	int freq;

	if (force_freq > 0)
		freq = force_freq;
	else
		freq = dev->listen_freq > 0 ? dev->listen_freq :
			dev->oper_freq;
	if (freq <= 0) {
		p2p_dbg(p2p, "No Listen/Operating frequency known for the peer "
			MACSTR " to send Provision Discovery Request",
			MAC2STR(dev->info.p2p_device_addr));
		return -1;
	}

	if (dev->flags & P2P_DEV_GROUP_CLIENT_ONLY) {
		if (!(dev->info.dev_capab &
		      P2P_DEV_CAPAB_CLIENT_DISCOVERABILITY)) {
			p2p_dbg(p2p, "Cannot use PD with P2P Device " MACSTR
				" that is in a group and is not discoverable",
				MAC2STR(dev->info.p2p_device_addr));
			return -1;
		}
		/* TODO: use device discoverability request through GO */
	}

	if (p2p->p2ps_prov) {
		if (p2p->p2ps_prov->status == P2P_SC_SUCCESS_DEFERRED) {
			if (p2p->p2ps_prov->method == WPS_CONFIG_DISPLAY)
				dev->req_config_methods = WPS_CONFIG_KEYPAD;
			else if (p2p->p2ps_prov->method == WPS_CONFIG_KEYPAD)
				dev->req_config_methods = WPS_CONFIG_DISPLAY;
			else
				dev->req_config_methods = WPS_CONFIG_P2PS;
		} else {
			/* Order of preference, based on peer's capabilities */
			if (p2p->p2ps_prov->method)
				dev->req_config_methods =
					p2p->p2ps_prov->method;
			else if (dev->info.config_methods & WPS_CONFIG_P2PS)
				dev->req_config_methods = WPS_CONFIG_P2PS;
			else if (dev->info.config_methods & WPS_CONFIG_DISPLAY)
				dev->req_config_methods = WPS_CONFIG_DISPLAY;
			else
				dev->req_config_methods = WPS_CONFIG_KEYPAD;
		}
		p2p_dbg(p2p,
			"Building PD Request based on P2PS config method 0x%x status %d --> req_config_methods 0x%x",
			p2p->p2ps_prov->method, p2p->p2ps_prov->status,
			dev->req_config_methods);

		if (p2p_prepare_channel(p2p, dev, p2p->p2ps_prov->force_freq,
					p2p->p2ps_prov->pref_freq, 1) < 0)
			return -1;
	}

	req = p2p_build_prov_disc_req(p2p, dev, join);
	if (req == NULL)
		return -1;

	if (p2p->state != P2P_IDLE)
		p2p_stop_listen_for_freq(p2p, freq);
	p2p->pending_action_state = P2P_PENDING_PD;
	if (p2p_send_action(p2p, freq, dev->info.p2p_device_addr,
			    p2p->cfg->dev_addr, dev->info.p2p_device_addr,
			    wpabuf_head(req), wpabuf_len(req), 200) < 0) {
		p2p_dbg(p2p, "Failed to send Action frame");
		wpabuf_free(req);
		return -1;
	}

	os_memcpy(p2p->pending_pd_devaddr, dev->info.p2p_device_addr, ETH_ALEN);

	wpabuf_free(req);
	return 0;
}


int p2p_prov_disc_req(struct p2p_data *p2p, const u8 *peer_addr,
		      struct p2ps_provision *p2ps_prov,
		      u16 config_methods, int join, int force_freq,
		      int user_initiated_pd)
{
	struct p2p_device *dev;

	dev = p2p_get_device(p2p, peer_addr);
	if (dev == NULL)
		dev = p2p_get_device_interface(p2p, peer_addr);
	if (dev == NULL || (dev->flags & P2P_DEV_PROBE_REQ_ONLY)) {
		p2p_dbg(p2p, "Provision Discovery Request destination " MACSTR
			" not yet known", MAC2STR(peer_addr));
		os_free(p2ps_prov);
		return -1;
	}

	p2p_dbg(p2p, "Provision Discovery Request with " MACSTR
		" (config methods 0x%x)",
		MAC2STR(peer_addr), config_methods);
	if (config_methods == 0 && !p2ps_prov) {
		os_free(p2ps_prov);
		return -1;
	}

	if (p2ps_prov && p2ps_prov->status == P2P_SC_SUCCESS_DEFERRED &&
	    p2p->p2ps_prov) {
		/* Use cached method from deferred provisioning */
		p2ps_prov->method = p2p->p2ps_prov->method;
	}

	/* Reset provisioning info */
	dev->wps_prov_info = 0;
	p2ps_prov_free(p2p);
	p2p->p2ps_prov = p2ps_prov;

	dev->req_config_methods = config_methods;
	if (join)
		dev->flags |= P2P_DEV_PD_FOR_JOIN;
	else
		dev->flags &= ~P2P_DEV_PD_FOR_JOIN;

	if (p2p->state != P2P_IDLE && p2p->state != P2P_SEARCH &&
	    p2p->state != P2P_LISTEN_ONLY) {
		p2p_dbg(p2p, "Busy with other operations; postpone Provision Discovery Request with "
			MACSTR " (config methods 0x%x)",
			MAC2STR(peer_addr), config_methods);
		return 0;
	}

	p2p->user_initiated_pd = user_initiated_pd;
	p2p->pd_force_freq = force_freq;

	if (p2p->user_initiated_pd)
		p2p->pd_retries = MAX_PROV_DISC_REQ_RETRIES;

	/*
	 * Assign dialog token here to use the same value in each retry within
	 * the same PD exchange.
	 */
	dev->dialog_token++;
	if (dev->dialog_token == 0)
		dev->dialog_token = 1;

	return p2p_send_prov_disc_req(p2p, dev, join, force_freq);
}


void p2p_reset_pending_pd(struct p2p_data *p2p)
{
	struct p2p_device *dev;

	dl_list_for_each(dev, &p2p->devices, struct p2p_device, list) {
		if (os_memcmp(p2p->pending_pd_devaddr,
			      dev->info.p2p_device_addr, ETH_ALEN))
			continue;
		if (!dev->req_config_methods)
			continue;
		if (dev->flags & P2P_DEV_PD_FOR_JOIN)
			continue;
		/* Reset the config methods of the device */
		dev->req_config_methods = 0;
	}

	p2p->user_initiated_pd = 0;
	os_memset(p2p->pending_pd_devaddr, 0, ETH_ALEN);
	p2p->pd_retries = 0;
	p2p->pd_force_freq = 0;
}


void p2ps_prov_free(struct p2p_data *p2p)
{
	os_free(p2p->p2ps_prov);
	p2p->p2ps_prov = NULL;
}
