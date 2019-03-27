/*
 * WPA Supplicant - Basic mesh mode routines
 * Copyright (c) 2013-2014, cozybit, Inc.  All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/uuid.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "ap/sta_info.h"
#include "ap/hostapd.h"
#include "ap/ieee802_11.h"
#include "config_ssid.h"
#include "config.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "notify.h"
#include "ap.h"
#include "mesh_mpm.h"
#include "mesh_rsn.h"
#include "mesh.h"


static void wpa_supplicant_mesh_deinit(struct wpa_supplicant *wpa_s)
{
	wpa_supplicant_mesh_iface_deinit(wpa_s, wpa_s->ifmsh);
	wpa_s->ifmsh = NULL;
	wpa_s->current_ssid = NULL;
	os_free(wpa_s->mesh_rsn);
	wpa_s->mesh_rsn = NULL;
	/* TODO: leave mesh (stop beacon). This will happen on link down
	 * anyway, so it's not urgent */
}


void wpa_supplicant_mesh_iface_deinit(struct wpa_supplicant *wpa_s,
				      struct hostapd_iface *ifmsh)
{
	if (!ifmsh)
		return;

	if (ifmsh->mconf) {
		mesh_mpm_deinit(wpa_s, ifmsh);
		if (ifmsh->mconf->rsn_ie) {
			ifmsh->mconf->rsn_ie = NULL;
			/* We cannot free this struct
			 * because wpa_authenticator on
			 * hostapd side is also using it
			 * for now just set to NULL and
			 * let hostapd code free it.
			 */
		}
		os_free(ifmsh->mconf);
		ifmsh->mconf = NULL;
	}

	/* take care of shared data */
	hostapd_interface_deinit(ifmsh);
	hostapd_interface_free(ifmsh);
}


static struct mesh_conf * mesh_config_create(struct wpa_supplicant *wpa_s,
					     struct wpa_ssid *ssid)
{
	struct mesh_conf *conf;
	int cipher;

	conf = os_zalloc(sizeof(struct mesh_conf));
	if (!conf)
		return NULL;

	os_memcpy(conf->meshid, ssid->ssid, ssid->ssid_len);
	conf->meshid_len = ssid->ssid_len;

	if (ssid->key_mgmt & WPA_KEY_MGMT_SAE)
		conf->security |= MESH_CONF_SEC_AUTH |
			MESH_CONF_SEC_AMPE;
	else
		conf->security |= MESH_CONF_SEC_NONE;
#ifdef CONFIG_IEEE80211W
	conf->ieee80211w = ssid->ieee80211w;
	if (conf->ieee80211w == MGMT_FRAME_PROTECTION_DEFAULT) {
		if (wpa_s->drv_enc & WPA_DRIVER_CAPA_ENC_BIP)
			conf->ieee80211w = wpa_s->conf->pmf;
		else
			conf->ieee80211w = NO_MGMT_FRAME_PROTECTION;
	}
#endif /* CONFIG_IEEE80211W */

	cipher = wpa_pick_pairwise_cipher(ssid->pairwise_cipher, 0);
	if (cipher < 0 || cipher == WPA_CIPHER_TKIP) {
		wpa_msg(wpa_s, MSG_INFO, "mesh: Invalid pairwise cipher");
		os_free(conf);
		return NULL;
	}
	conf->pairwise_cipher = cipher;

	cipher = wpa_pick_group_cipher(ssid->group_cipher);
	if (cipher < 0 || cipher == WPA_CIPHER_TKIP ||
	    cipher == WPA_CIPHER_GTK_NOT_USED) {
		wpa_msg(wpa_s, MSG_INFO, "mesh: Invalid group cipher");
		os_free(conf);
		return NULL;
	}

	conf->group_cipher = cipher;
	if (conf->ieee80211w != NO_MGMT_FRAME_PROTECTION)
		conf->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;

	/* defaults */
	conf->mesh_pp_id = MESH_PATH_PROTOCOL_HWMP;
	conf->mesh_pm_id = MESH_PATH_METRIC_AIRTIME;
	conf->mesh_cc_id = 0;
	conf->mesh_sp_id = MESH_SYNC_METHOD_NEIGHBOR_OFFSET;
	conf->mesh_auth_id = (conf->security & MESH_CONF_SEC_AUTH) ? 1 : 0;
	conf->dot11MeshMaxRetries = ssid->dot11MeshMaxRetries;
	conf->dot11MeshRetryTimeout = ssid->dot11MeshRetryTimeout;
	conf->dot11MeshConfirmTimeout = ssid->dot11MeshConfirmTimeout;
	conf->dot11MeshHoldingTimeout = ssid->dot11MeshHoldingTimeout;

	return conf;
}


static void wpas_mesh_copy_groups(struct hostapd_data *bss,
				  struct wpa_supplicant *wpa_s)
{
	int num_groups;
	size_t groups_size;

	for (num_groups = 0; wpa_s->conf->sae_groups[num_groups] > 0;
	     num_groups++)
		;

	groups_size = (num_groups + 1) * sizeof(wpa_s->conf->sae_groups[0]);
	bss->conf->sae_groups = os_malloc(groups_size);
	if (bss->conf->sae_groups)
		os_memcpy(bss->conf->sae_groups, wpa_s->conf->sae_groups,
			  groups_size);
}


static int wpa_supplicant_mesh_init(struct wpa_supplicant *wpa_s,
				    struct wpa_ssid *ssid,
				    struct hostapd_freq_params *freq)
{
	struct hostapd_iface *ifmsh;
	struct hostapd_data *bss;
	struct hostapd_config *conf;
	struct mesh_conf *mconf;
	int basic_rates_erp[] = { 10, 20, 55, 60, 110, 120, 240, -1 };
	static int default_groups[] = { 19, 20, 21, 25, 26, -1 };
	const char *password;
	size_t len;
	int rate_len;
	int frequency;

	if (!wpa_s->conf->user_mpm) {
		/* not much for us to do here */
		wpa_msg(wpa_s, MSG_WARNING,
			"user_mpm is not enabled in configuration");
		return 0;
	}

	wpa_s->ifmsh = ifmsh = hostapd_alloc_iface();
	if (!ifmsh)
		return -ENOMEM;

	ifmsh->drv_flags = wpa_s->drv_flags;
	ifmsh->num_bss = 1;
	ifmsh->bss = os_calloc(wpa_s->ifmsh->num_bss,
			       sizeof(struct hostapd_data *));
	if (!ifmsh->bss)
		goto out_free;

	ifmsh->bss[0] = bss = hostapd_alloc_bss_data(NULL, NULL, NULL);
	if (!bss)
		goto out_free;

	ifmsh->bss[0]->msg_ctx = wpa_s;
	os_memcpy(bss->own_addr, wpa_s->own_addr, ETH_ALEN);
	bss->driver = wpa_s->driver;
	bss->drv_priv = wpa_s->drv_priv;
	bss->iface = ifmsh;
	bss->mesh_sta_free_cb = mesh_mpm_free_sta;
	frequency = ssid->frequency;
	if (frequency != freq->freq &&
	    frequency == freq->freq + freq->sec_channel_offset * 20) {
		wpa_printf(MSG_DEBUG, "mesh: pri/sec channels switched");
		frequency = freq->freq;
	}
	wpa_s->assoc_freq = frequency;
	wpa_s->current_ssid = ssid;

	/* setup an AP config for auth processing */
	conf = hostapd_config_defaults();
	if (!conf)
		goto out_free;

	bss->conf = *conf->bss;
	bss->conf->start_disabled = 1;
	bss->conf->mesh = MESH_ENABLED;
	bss->conf->ap_max_inactivity = wpa_s->conf->mesh_max_inactivity;
	bss->iconf = conf;
	ifmsh->conf = conf;

	ifmsh->bss[0]->max_plinks = wpa_s->conf->max_peer_links;
	ifmsh->bss[0]->dot11RSNASAERetransPeriod =
		wpa_s->conf->dot11RSNASAERetransPeriod;
	os_strlcpy(bss->conf->iface, wpa_s->ifname, sizeof(bss->conf->iface));

	mconf = mesh_config_create(wpa_s, ssid);
	if (!mconf)
		goto out_free;
	ifmsh->mconf = mconf;

	/* need conf->hw_mode for supported rates. */
	conf->hw_mode = ieee80211_freq_to_chan(frequency, &conf->channel);
	if (conf->hw_mode == NUM_HOSTAPD_MODES) {
		wpa_printf(MSG_ERROR, "Unsupported mesh mode frequency: %d MHz",
			   frequency);
		goto out_free;
	}
	if (ssid->ht40)
		conf->secondary_channel = ssid->ht40;
	if (conf->hw_mode == HOSTAPD_MODE_IEEE80211A && ssid->vht) {
		conf->vht_oper_chwidth = ssid->max_oper_chwidth;
		switch (conf->vht_oper_chwidth) {
		case VHT_CHANWIDTH_80MHZ:
		case VHT_CHANWIDTH_80P80MHZ:
			ieee80211_freq_to_chan(
				frequency,
				&conf->vht_oper_centr_freq_seg0_idx);
			conf->vht_oper_centr_freq_seg0_idx += ssid->ht40 * 2;
			break;
		case VHT_CHANWIDTH_160MHZ:
			ieee80211_freq_to_chan(
				frequency,
				&conf->vht_oper_centr_freq_seg0_idx);
			conf->vht_oper_centr_freq_seg0_idx += ssid->ht40 * 2;
			conf->vht_oper_centr_freq_seg0_idx += 40 / 5;
			break;
		}
		ieee80211_freq_to_chan(ssid->vht_center_freq2,
				       &conf->vht_oper_centr_freq_seg1_idx);
	}

	if (ssid->mesh_basic_rates == NULL) {
		/*
		 * XXX: Hack! This is so an MPM which correctly sets the ERP
		 * mandatory rates as BSSBasicRateSet doesn't reject us. We
		 * could add a new hw_mode HOSTAPD_MODE_IEEE80211G_ERP, but
		 * this is way easier. This also makes our BSSBasicRateSet
		 * advertised in beacons match the one in peering frames, sigh.
		 */
		if (conf->hw_mode == HOSTAPD_MODE_IEEE80211G) {
			conf->basic_rates = os_memdup(basic_rates_erp,
						      sizeof(basic_rates_erp));
			if (!conf->basic_rates)
				goto out_free;
		}
	} else {
		rate_len = 0;
		while (1) {
			if (ssid->mesh_basic_rates[rate_len] < 1)
				break;
			rate_len++;
		}
		conf->basic_rates = os_calloc(rate_len + 1, sizeof(int));
		if (conf->basic_rates == NULL)
			goto out_free;
		os_memcpy(conf->basic_rates, ssid->mesh_basic_rates,
			  rate_len * sizeof(int));
		conf->basic_rates[rate_len] = -1;
	}

	if (hostapd_setup_interface(ifmsh)) {
		wpa_printf(MSG_ERROR,
			   "Failed to initialize hostapd interface for mesh");
		return -1;
	}

	if (wpa_drv_init_mesh(wpa_s)) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to init mesh in driver");
		return -1;
	}

	if (mconf->security != MESH_CONF_SEC_NONE) {
		password = ssid->sae_password;
		if (!password)
			password = ssid->passphrase;
		if (!password) {
			wpa_printf(MSG_ERROR,
				   "mesh: Passphrase for SAE not configured");
			goto out_free;
		}

		bss->conf->wpa = ssid->proto;
		bss->conf->wpa_key_mgmt = ssid->key_mgmt;

		if (wpa_s->conf->sae_groups &&
		    wpa_s->conf->sae_groups[0] > 0) {
			wpas_mesh_copy_groups(bss, wpa_s);
		} else {
			bss->conf->sae_groups =
				os_memdup(default_groups,
					  sizeof(default_groups));
			if (!bss->conf->sae_groups)
				goto out_free;
		}

		len = os_strlen(password);
		bss->conf->ssid.wpa_passphrase =
			dup_binstr(password, len);

		wpa_s->mesh_rsn = mesh_rsn_auth_init(wpa_s, mconf);
		if (!wpa_s->mesh_rsn)
			goto out_free;
	}

	wpa_supplicant_conf_ap_ht(wpa_s, ssid, conf);

	return 0;
out_free:
	wpa_supplicant_mesh_deinit(wpa_s);
	return -ENOMEM;
}


void wpa_mesh_notify_peer(struct wpa_supplicant *wpa_s, const u8 *addr,
			  const u8 *ies, size_t ie_len)
{
	struct ieee802_11_elems elems;

	wpa_msg(wpa_s, MSG_INFO,
		"new peer notification for " MACSTR, MAC2STR(addr));

	if (ieee802_11_parse_elems(ies, ie_len, &elems, 0) == ParseFailed) {
		wpa_msg(wpa_s, MSG_INFO, "Could not parse beacon from " MACSTR,
			MAC2STR(addr));
		return;
	}
	wpa_mesh_new_mesh_peer(wpa_s, addr, &elems);
}


void wpa_supplicant_mesh_add_scan_ie(struct wpa_supplicant *wpa_s,
				     struct wpabuf **extra_ie)
{
	/* EID + 0-length (wildcard) mesh-id */
	size_t ielen = 2;

	if (wpabuf_resize(extra_ie, ielen) == 0) {
		wpabuf_put_u8(*extra_ie, WLAN_EID_MESH_ID);
		wpabuf_put_u8(*extra_ie, 0);
	}
}


int wpa_supplicant_join_mesh(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *ssid)
{
	struct wpa_driver_mesh_join_params params;
	int ret = 0;

	if (!ssid || !ssid->ssid || !ssid->ssid_len || !ssid->frequency) {
		ret = -ENOENT;
		goto out;
	}

	wpa_supplicant_mesh_deinit(wpa_s);

	wpa_s->pairwise_cipher = WPA_CIPHER_NONE;
	wpa_s->group_cipher = WPA_CIPHER_NONE;
	wpa_s->mgmt_group_cipher = 0;

	os_memset(&params, 0, sizeof(params));
	params.meshid = ssid->ssid;
	params.meshid_len = ssid->ssid_len;
	ibss_mesh_setup_freq(wpa_s, ssid, &params.freq);
	wpa_s->mesh_ht_enabled = !!params.freq.ht_enabled;
	wpa_s->mesh_vht_enabled = !!params.freq.vht_enabled;
	if (params.freq.ht_enabled && params.freq.sec_channel_offset)
		ssid->ht40 = params.freq.sec_channel_offset;
	if (wpa_s->mesh_vht_enabled) {
		ssid->vht = 1;
		switch (params.freq.bandwidth) {
		case 80:
			if (params.freq.center_freq2) {
				ssid->max_oper_chwidth = VHT_CHANWIDTH_80P80MHZ;
				ssid->vht_center_freq2 =
					params.freq.center_freq2;
			} else {
				ssid->max_oper_chwidth = VHT_CHANWIDTH_80MHZ;
			}
			break;
		case 160:
			ssid->max_oper_chwidth = VHT_CHANWIDTH_160MHZ;
			break;
		default:
			ssid->max_oper_chwidth = VHT_CHANWIDTH_USE_HT;
			break;
		}
	}
	if (ssid->beacon_int > 0)
		params.beacon_int = ssid->beacon_int;
	else if (wpa_s->conf->beacon_int > 0)
		params.beacon_int = wpa_s->conf->beacon_int;
	if (ssid->dtim_period > 0)
		params.dtim_period = ssid->dtim_period;
	else if (wpa_s->conf->dtim_period > 0)
		params.dtim_period = wpa_s->conf->dtim_period;
	params.conf.max_peer_links = wpa_s->conf->max_peer_links;
	if (ssid->mesh_rssi_threshold < DEFAULT_MESH_RSSI_THRESHOLD) {
		params.conf.rssi_threshold = ssid->mesh_rssi_threshold;
		params.conf.flags |= WPA_DRIVER_MESH_CONF_FLAG_RSSI_THRESHOLD;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_SAE) {
		params.flags |= WPA_DRIVER_MESH_FLAG_SAE_AUTH;
		params.flags |= WPA_DRIVER_MESH_FLAG_AMPE;
		wpa_s->conf->user_mpm = 1;
	}

	if (wpa_s->conf->user_mpm) {
		params.flags |= WPA_DRIVER_MESH_FLAG_USER_MPM;
		params.conf.auto_plinks = 0;
	} else {
		params.flags |= WPA_DRIVER_MESH_FLAG_DRIVER_MPM;
		params.conf.auto_plinks = 1;
	}
	params.conf.peer_link_timeout = wpa_s->conf->mesh_max_inactivity;

	if (wpa_supplicant_mesh_init(wpa_s, ssid, &params.freq)) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to init mesh");
		wpa_drv_leave_mesh(wpa_s);
		ret = -1;
		goto out;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_SAE) {
		wpa_s->pairwise_cipher = wpa_s->mesh_rsn->pairwise_cipher;
		wpa_s->group_cipher = wpa_s->mesh_rsn->group_cipher;
		wpa_s->mgmt_group_cipher = wpa_s->mesh_rsn->mgmt_group_cipher;
	}

	if (wpa_s->ifmsh) {
		params.ies = wpa_s->ifmsh->mconf->rsn_ie;
		params.ie_len = wpa_s->ifmsh->mconf->rsn_ie_len;
		params.basic_rates = wpa_s->ifmsh->basic_rates;
		params.conf.flags |= WPA_DRIVER_MESH_CONF_FLAG_HT_OP_MODE;
		params.conf.ht_opmode = wpa_s->ifmsh->bss[0]->iface->ht_op_mode;
	}

	wpa_msg(wpa_s, MSG_INFO, "joining mesh %s",
		wpa_ssid_txt(ssid->ssid, ssid->ssid_len));
	ret = wpa_drv_join_mesh(wpa_s, &params);
	if (ret)
		wpa_msg(wpa_s, MSG_ERROR, "mesh join error=%d", ret);

	/* hostapd sets the interface down until we associate */
	wpa_drv_set_operstate(wpa_s, 1);

	if (!ret)
		wpa_supplicant_set_state(wpa_s, WPA_COMPLETED);

out:
	return ret;
}


int wpa_supplicant_leave_mesh(struct wpa_supplicant *wpa_s)
{
	int ret = 0;

	wpa_msg(wpa_s, MSG_INFO, "leaving mesh");

	/* Need to send peering close messages first */
	wpa_supplicant_mesh_deinit(wpa_s);

	ret = wpa_drv_leave_mesh(wpa_s);
	if (ret)
		wpa_msg(wpa_s, MSG_ERROR, "mesh leave error=%d", ret);

	wpa_drv_set_operstate(wpa_s, 1);

	return ret;
}


static int mesh_attr_text(const u8 *ies, size_t ies_len, char *buf, char *end)
{
	struct ieee802_11_elems elems;
	char *mesh_id, *pos = buf;
	u8 *bss_basic_rate_set;
	int bss_basic_rate_set_len, ret, i;

	if (ieee802_11_parse_elems(ies, ies_len, &elems, 0) == ParseFailed)
		return -1;

	if (elems.mesh_id_len < 1)
		return 0;

	mesh_id = os_malloc(elems.mesh_id_len + 1);
	if (mesh_id == NULL)
		return -1;

	os_memcpy(mesh_id, elems.mesh_id, elems.mesh_id_len);
	mesh_id[elems.mesh_id_len] = '\0';
	ret = os_snprintf(pos, end - pos, "mesh_id=%s\n", mesh_id);
	os_free(mesh_id);
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	if (elems.mesh_config_len > 6) {
		ret = os_snprintf(pos, end - pos,
				  "active_path_selection_protocol_id=0x%02x\n"
				  "active_path_selection_metric_id=0x%02x\n"
				  "congestion_control_mode_id=0x%02x\n"
				  "synchronization_method_id=0x%02x\n"
				  "authentication_protocol_id=0x%02x\n"
				  "mesh_formation_info=0x%02x\n"
				  "mesh_capability=0x%02x\n",
				  elems.mesh_config[0], elems.mesh_config[1],
				  elems.mesh_config[2], elems.mesh_config[3],
				  elems.mesh_config[4], elems.mesh_config[5],
				  elems.mesh_config[6]);
		if (os_snprintf_error(end - pos, ret))
			return pos - buf;
		pos += ret;
	}

	bss_basic_rate_set = os_malloc(elems.supp_rates_len +
		elems.ext_supp_rates_len);
	if (bss_basic_rate_set == NULL)
		return -1;

	bss_basic_rate_set_len = 0;
	for (i = 0; i < elems.supp_rates_len; i++) {
		if (elems.supp_rates[i] & 0x80) {
			bss_basic_rate_set[bss_basic_rate_set_len++] =
				(elems.supp_rates[i] & 0x7f) * 5;
		}
	}
	for (i = 0; i < elems.ext_supp_rates_len; i++) {
		if (elems.ext_supp_rates[i] & 0x80) {
			bss_basic_rate_set[bss_basic_rate_set_len++] =
				(elems.ext_supp_rates[i] & 0x7f) * 5;
		}
	}
	if (bss_basic_rate_set_len > 0) {
		ret = os_snprintf(pos, end - pos, "bss_basic_rate_set=%d",
				  bss_basic_rate_set[0]);
		if (os_snprintf_error(end - pos, ret))
			goto fail;
		pos += ret;

		for (i = 1; i < bss_basic_rate_set_len; i++) {
			ret = os_snprintf(pos, end - pos, " %d",
					  bss_basic_rate_set[i]);
			if (os_snprintf_error(end - pos, ret))
				goto fail;
			pos += ret;
		}

		ret = os_snprintf(pos, end - pos, "\n");
		if (os_snprintf_error(end - pos, ret))
			goto fail;
		pos += ret;
	}
fail:
	os_free(bss_basic_rate_set);

	return pos - buf;
}


int wpas_mesh_scan_result_text(const u8 *ies, size_t ies_len, char *buf,
			       char *end)
{
	return mesh_attr_text(ies, ies_len, buf, end);
}


static int wpas_mesh_get_ifname(struct wpa_supplicant *wpa_s, char *ifname,
				size_t len)
{
	char *ifname_ptr = wpa_s->ifname;
	int res;

	res = os_snprintf(ifname, len, "mesh-%s-%d", ifname_ptr,
			  wpa_s->mesh_if_idx);
	if (os_snprintf_error(len, res) ||
	    (os_strlen(ifname) >= IFNAMSIZ &&
	     os_strlen(wpa_s->ifname) < IFNAMSIZ)) {
		/* Try to avoid going over the IFNAMSIZ length limit */
		res = os_snprintf(ifname, len, "mesh-%d", wpa_s->mesh_if_idx);
		if (os_snprintf_error(len, res))
			return -1;
	}
	wpa_s->mesh_if_idx++;
	return 0;
}


int wpas_mesh_add_interface(struct wpa_supplicant *wpa_s, char *ifname,
			    size_t len)
{
	struct wpa_interface iface;
	struct wpa_supplicant *mesh_wpa_s;
	u8 addr[ETH_ALEN];

	if (ifname[0] == '\0' && wpas_mesh_get_ifname(wpa_s, ifname, len) < 0)
		return -1;

	if (wpa_drv_if_add(wpa_s, WPA_IF_MESH, ifname, NULL, NULL, NULL, addr,
			   NULL) < 0) {
		wpa_printf(MSG_ERROR,
			   "mesh: Failed to create new mesh interface");
		return -1;
	}
	wpa_printf(MSG_INFO, "mesh: Created virtual interface %s addr "
		   MACSTR, ifname, MAC2STR(addr));

	os_memset(&iface, 0, sizeof(iface));
	iface.ifname = ifname;
	iface.driver = wpa_s->driver->name;
	iface.driver_param = wpa_s->conf->driver_param;
	iface.ctrl_interface = wpa_s->conf->ctrl_interface;

	mesh_wpa_s = wpa_supplicant_add_iface(wpa_s->global, &iface, wpa_s);
	if (!mesh_wpa_s) {
		wpa_printf(MSG_ERROR,
			   "mesh: Failed to create new wpa_supplicant interface");
		wpa_drv_if_remove(wpa_s, WPA_IF_MESH, ifname);
		return -1;
	}
	mesh_wpa_s->mesh_if_created = 1;
	return 0;
}


int wpas_mesh_peer_remove(struct wpa_supplicant *wpa_s, const u8 *addr)
{
	return mesh_mpm_close_peer(wpa_s, addr);
}


int wpas_mesh_peer_add(struct wpa_supplicant *wpa_s, const u8 *addr,
		       int duration)
{
	return mesh_mpm_connect_peer(wpa_s, addr, duration);
}
