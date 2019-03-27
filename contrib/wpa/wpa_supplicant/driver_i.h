/*
 * wpa_supplicant - Internal driver interface wrappers
 * Copyright (c) 2003-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DRIVER_I_H
#define DRIVER_I_H

#include "drivers/driver.h"

/* driver_ops */
static inline void * wpa_drv_init(struct wpa_supplicant *wpa_s,
				  const char *ifname)
{
	if (wpa_s->driver->init2)
		return wpa_s->driver->init2(wpa_s, ifname,
					    wpa_s->global_drv_priv);
	if (wpa_s->driver->init) {
		return wpa_s->driver->init(wpa_s, ifname);
	}
	return NULL;
}

static inline void wpa_drv_deinit(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->deinit)
		wpa_s->driver->deinit(wpa_s->drv_priv);
}

static inline int wpa_drv_set_param(struct wpa_supplicant *wpa_s,
				    const char *param)
{
	if (wpa_s->driver->set_param)
		return wpa_s->driver->set_param(wpa_s->drv_priv, param);
	return 0;
}

static inline int wpa_drv_set_countermeasures(struct wpa_supplicant *wpa_s,
					      int enabled)
{
	if (wpa_s->driver->set_countermeasures) {
		return wpa_s->driver->set_countermeasures(wpa_s->drv_priv,
							  enabled);
	}
	return -1;
}

static inline int wpa_drv_authenticate(struct wpa_supplicant *wpa_s,
				       struct wpa_driver_auth_params *params)
{
	if (wpa_s->driver->authenticate)
		return wpa_s->driver->authenticate(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_associate(struct wpa_supplicant *wpa_s,
				    struct wpa_driver_associate_params *params)
{
	if (wpa_s->driver->associate) {
		return wpa_s->driver->associate(wpa_s->drv_priv, params);
	}
	return -1;
}

static inline int wpa_drv_init_mesh(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->init_mesh)
		return wpa_s->driver->init_mesh(wpa_s->drv_priv);
	return -1;
}

static inline int wpa_drv_join_mesh(struct wpa_supplicant *wpa_s,
				    struct wpa_driver_mesh_join_params *params)
{
	if (wpa_s->driver->join_mesh)
		return wpa_s->driver->join_mesh(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_leave_mesh(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->leave_mesh)
		return wpa_s->driver->leave_mesh(wpa_s->drv_priv);
	return -1;
}

static inline int wpa_drv_scan(struct wpa_supplicant *wpa_s,
			       struct wpa_driver_scan_params *params)
{
#ifdef CONFIG_TESTING_OPTIONS
	if (wpa_s->test_failure == WPAS_TEST_FAILURE_SCAN_TRIGGER)
		return -EBUSY;
#endif /* CONFIG_TESTING_OPTIONS */
	if (wpa_s->driver->scan2)
		return wpa_s->driver->scan2(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_sched_scan(struct wpa_supplicant *wpa_s,
				     struct wpa_driver_scan_params *params)
{
	if (wpa_s->driver->sched_scan)
		return wpa_s->driver->sched_scan(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_stop_sched_scan(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->stop_sched_scan)
		return wpa_s->driver->stop_sched_scan(wpa_s->drv_priv);
	return -1;
}

static inline struct wpa_scan_results * wpa_drv_get_scan_results2(
	struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->get_scan_results2)
		return wpa_s->driver->get_scan_results2(wpa_s->drv_priv);
	return NULL;
}

static inline int wpa_drv_get_bssid(struct wpa_supplicant *wpa_s, u8 *bssid)
{
	if (wpa_s->driver->get_bssid) {
		return wpa_s->driver->get_bssid(wpa_s->drv_priv, bssid);
	}
	return -1;
}

static inline int wpa_drv_get_ssid(struct wpa_supplicant *wpa_s, u8 *ssid)
{
	if (wpa_s->driver->get_ssid) {
		return wpa_s->driver->get_ssid(wpa_s->drv_priv, ssid);
	}
	return -1;
}

static inline int wpa_drv_set_key(struct wpa_supplicant *wpa_s,
				  enum wpa_alg alg, const u8 *addr,
				  int key_idx, int set_tx,
				  const u8 *seq, size_t seq_len,
				  const u8 *key, size_t key_len)
{
	if (alg != WPA_ALG_NONE) {
		if (key_idx >= 0 && key_idx <= 6)
			wpa_s->keys_cleared &= ~BIT(key_idx);
		else
			wpa_s->keys_cleared = 0;
	}
	if (wpa_s->driver->set_key) {
		return wpa_s->driver->set_key(wpa_s->ifname, wpa_s->drv_priv,
					      alg, addr, key_idx, set_tx,
					      seq, seq_len, key, key_len);
	}
	return -1;
}

static inline int wpa_drv_get_seqnum(struct wpa_supplicant *wpa_s,
				     const u8 *addr, int idx, u8 *seq)
{
	if (wpa_s->driver->get_seqnum)
		return wpa_s->driver->get_seqnum(wpa_s->ifname, wpa_s->drv_priv,
						 addr, idx, seq);
	return -1;
}

static inline int wpa_drv_sta_deauth(struct wpa_supplicant *wpa_s,
				     const u8 *addr, int reason_code)
{
	if (wpa_s->driver->sta_deauth) {
		return wpa_s->driver->sta_deauth(wpa_s->drv_priv,
						 wpa_s->own_addr, addr,
						 reason_code);
	}
	return -1;
}

static inline int wpa_drv_deauthenticate(struct wpa_supplicant *wpa_s,
					 const u8 *addr, int reason_code)
{
	if (wpa_s->driver->deauthenticate) {
		return wpa_s->driver->deauthenticate(wpa_s->drv_priv, addr,
						     reason_code);
	}
	return -1;
}

static inline int wpa_drv_add_pmkid(struct wpa_supplicant *wpa_s,
				    struct wpa_pmkid_params *params)
{
	if (wpa_s->driver->add_pmkid) {
		return wpa_s->driver->add_pmkid(wpa_s->drv_priv, params);
	}
	return -1;
}

static inline int wpa_drv_remove_pmkid(struct wpa_supplicant *wpa_s,
				       struct wpa_pmkid_params *params)
{
	if (wpa_s->driver->remove_pmkid) {
		return wpa_s->driver->remove_pmkid(wpa_s->drv_priv, params);
	}
	return -1;
}

static inline int wpa_drv_flush_pmkid(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->flush_pmkid) {
		return wpa_s->driver->flush_pmkid(wpa_s->drv_priv);
	}
	return -1;
}

static inline int wpa_drv_get_capa(struct wpa_supplicant *wpa_s,
				   struct wpa_driver_capa *capa)
{
	if (wpa_s->driver->get_capa) {
		return wpa_s->driver->get_capa(wpa_s->drv_priv, capa);
	}
	return -1;
}

static inline void wpa_drv_poll(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->poll) {
		wpa_s->driver->poll(wpa_s->drv_priv);
	}
}

static inline const char * wpa_drv_get_ifname(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->get_ifname) {
		return wpa_s->driver->get_ifname(wpa_s->drv_priv);
	}
	return NULL;
}

static inline const char *
wpa_driver_get_radio_name(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->get_radio_name)
		return wpa_s->driver->get_radio_name(wpa_s->drv_priv);
	return NULL;
}

static inline const u8 * wpa_drv_get_mac_addr(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->get_mac_addr) {
		return wpa_s->driver->get_mac_addr(wpa_s->drv_priv);
	}
	return NULL;
}

static inline int wpa_drv_set_operstate(struct wpa_supplicant *wpa_s,
					int state)
{
	if (wpa_s->driver->set_operstate)
		return wpa_s->driver->set_operstate(wpa_s->drv_priv, state);
	return 0;
}

static inline int wpa_drv_mlme_setprotection(struct wpa_supplicant *wpa_s,
					     const u8 *addr, int protect_type,
					     int key_type)
{
	if (wpa_s->driver->mlme_setprotection)
		return wpa_s->driver->mlme_setprotection(wpa_s->drv_priv, addr,
							 protect_type,
							 key_type);
	return 0;
}

static inline struct hostapd_hw_modes *
wpa_drv_get_hw_feature_data(struct wpa_supplicant *wpa_s, u16 *num_modes,
			    u16 *flags, u8 *dfs_domain)
{
	if (wpa_s->driver->get_hw_feature_data)
		return wpa_s->driver->get_hw_feature_data(wpa_s->drv_priv,
							  num_modes, flags,
							  dfs_domain);
	return NULL;
}

static inline int wpa_drv_set_country(struct wpa_supplicant *wpa_s,
				      const char *alpha2)
{
	if (wpa_s->driver->set_country)
		return wpa_s->driver->set_country(wpa_s->drv_priv, alpha2);
	return 0;
}

static inline int wpa_drv_send_mlme(struct wpa_supplicant *wpa_s,
				    const u8 *data, size_t data_len, int noack,
				    unsigned int freq)
{
	if (wpa_s->driver->send_mlme)
		return wpa_s->driver->send_mlme(wpa_s->drv_priv,
						data, data_len, noack,
						freq, NULL, 0);
	return -1;
}

static inline int wpa_drv_update_ft_ies(struct wpa_supplicant *wpa_s,
					const u8 *md,
					const u8 *ies, size_t ies_len)
{
	if (wpa_s->driver->update_ft_ies)
		return wpa_s->driver->update_ft_ies(wpa_s->drv_priv, md,
						    ies, ies_len);
	return -1;
}

static inline int wpa_drv_set_ap(struct wpa_supplicant *wpa_s,
				 struct wpa_driver_ap_params *params)
{
	if (wpa_s->driver->set_ap)
		return wpa_s->driver->set_ap(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_sta_add(struct wpa_supplicant *wpa_s,
				  struct hostapd_sta_add_params *params)
{
	if (wpa_s->driver->sta_add)
		return wpa_s->driver->sta_add(wpa_s->drv_priv, params);
	return -1;
}

static inline int wpa_drv_sta_remove(struct wpa_supplicant *wpa_s,
				     const u8 *addr)
{
	if (wpa_s->driver->sta_remove)
		return wpa_s->driver->sta_remove(wpa_s->drv_priv, addr);
	return -1;
}

static inline int wpa_drv_hapd_send_eapol(struct wpa_supplicant *wpa_s,
					  const u8 *addr, const u8 *data,
					  size_t data_len, int encrypt,
					  const u8 *own_addr, u32 flags)
{
	if (wpa_s->driver->hapd_send_eapol)
		return wpa_s->driver->hapd_send_eapol(wpa_s->drv_priv, addr,
						      data, data_len, encrypt,
						      own_addr, flags);
	return -1;
}

static inline int wpa_drv_sta_set_flags(struct wpa_supplicant *wpa_s,
					const u8 *addr, int total_flags,
					int flags_or, int flags_and)
{
	if (wpa_s->driver->sta_set_flags)
		return wpa_s->driver->sta_set_flags(wpa_s->drv_priv, addr,
						    total_flags, flags_or,
						    flags_and);
	return -1;
}

static inline int wpa_drv_set_supp_port(struct wpa_supplicant *wpa_s,
					int authorized)
{
	if (wpa_s->driver->set_supp_port) {
		return wpa_s->driver->set_supp_port(wpa_s->drv_priv,
						    authorized);
	}
	return 0;
}

static inline int wpa_drv_send_action(struct wpa_supplicant *wpa_s,
				      unsigned int freq,
				      unsigned int wait,
				      const u8 *dst, const u8 *src,
				      const u8 *bssid,
				      const u8 *data, size_t data_len,
				      int no_cck)
{
	if (wpa_s->driver->send_action)
		return wpa_s->driver->send_action(wpa_s->drv_priv, freq,
						  wait, dst, src, bssid,
						  data, data_len, no_cck);
	return -1;
}

static inline void wpa_drv_send_action_cancel_wait(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->send_action_cancel_wait)
		wpa_s->driver->send_action_cancel_wait(wpa_s->drv_priv);
}

static inline int wpa_drv_set_freq(struct wpa_supplicant *wpa_s,
				   struct hostapd_freq_params *freq)
{
	if (wpa_s->driver->set_freq)
		return wpa_s->driver->set_freq(wpa_s->drv_priv, freq);
	return -1;
}

static inline int wpa_drv_if_add(struct wpa_supplicant *wpa_s,
				 enum wpa_driver_if_type type,
				 const char *ifname, const u8 *addr,
				 void *bss_ctx, char *force_ifname,
				 u8 *if_addr, const char *bridge)
{
	if (wpa_s->driver->if_add)
		return wpa_s->driver->if_add(wpa_s->drv_priv, type, ifname,
					     addr, bss_ctx, NULL, force_ifname,
					     if_addr, bridge, 0, 0);
	return -1;
}

static inline int wpa_drv_if_remove(struct wpa_supplicant *wpa_s,
				    enum wpa_driver_if_type type,
				    const char *ifname)
{
	if (wpa_s->driver->if_remove)
		return wpa_s->driver->if_remove(wpa_s->drv_priv, type, ifname);
	return -1;
}

static inline int wpa_drv_remain_on_channel(struct wpa_supplicant *wpa_s,
					    unsigned int freq,
					    unsigned int duration)
{
	if (wpa_s->driver->remain_on_channel)
		return wpa_s->driver->remain_on_channel(wpa_s->drv_priv, freq,
							duration);
	return -1;
}

static inline int wpa_drv_cancel_remain_on_channel(
	struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->cancel_remain_on_channel)
		return wpa_s->driver->cancel_remain_on_channel(
			wpa_s->drv_priv);
	return -1;
}

static inline int wpa_drv_probe_req_report(struct wpa_supplicant *wpa_s,
					   int report)
{
	if (wpa_s->driver->probe_req_report)
		return wpa_s->driver->probe_req_report(wpa_s->drv_priv,
						       report);
	return -1;
}

static inline int wpa_drv_deinit_ap(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->deinit_ap)
		return wpa_s->driver->deinit_ap(wpa_s->drv_priv);
	return 0;
}

static inline int wpa_drv_deinit_p2p_cli(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->deinit_p2p_cli)
		return wpa_s->driver->deinit_p2p_cli(wpa_s->drv_priv);
	return 0;
}

static inline void wpa_drv_suspend(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->suspend)
		wpa_s->driver->suspend(wpa_s->drv_priv);
}

static inline void wpa_drv_resume(struct wpa_supplicant *wpa_s)
{
	if (wpa_s->driver->resume)
		wpa_s->driver->resume(wpa_s->drv_priv);
}

static inline int wpa_drv_signal_monitor(struct wpa_supplicant *wpa_s,
					 int threshold, int hysteresis)
{
	if (wpa_s->driver->signal_monitor)
		return wpa_s->driver->signal_monitor(wpa_s->drv_priv,
						     threshold, hysteresis);
	return -1;
}

static inline int wpa_drv_signal_poll(struct wpa_supplicant *wpa_s,
				      struct wpa_signal_info *si)
{
	if (wpa_s->driver->signal_poll)
		return wpa_s->driver->signal_poll(wpa_s->drv_priv, si);
	return -1;
}

static inline int wpa_drv_pktcnt_poll(struct wpa_supplicant *wpa_s,
				      struct hostap_sta_driver_data *sta)
{
	if (wpa_s->driver->read_sta_data)
		return wpa_s->driver->read_sta_data(wpa_s->drv_priv, sta,
						    wpa_s->bssid);
	return -1;
}

static inline int wpa_drv_set_ap_wps_ie(struct wpa_supplicant *wpa_s,
					const struct wpabuf *beacon,
					const struct wpabuf *proberesp,
					const struct wpabuf *assocresp)
{
	if (!wpa_s->driver->set_ap_wps_ie)
		return -1;
	return wpa_s->driver->set_ap_wps_ie(wpa_s->drv_priv, beacon,
					    proberesp, assocresp);
}

static inline int wpa_drv_get_noa(struct wpa_supplicant *wpa_s,
				  u8 *buf, size_t buf_len)
{
	if (!wpa_s->driver->get_noa)
		return -1;
	return wpa_s->driver->get_noa(wpa_s->drv_priv, buf, buf_len);
}

static inline int wpa_drv_set_p2p_powersave(struct wpa_supplicant *wpa_s,
					    int legacy_ps, int opp_ps,
					    int ctwindow)
{
	if (!wpa_s->driver->set_p2p_powersave)
		return -1;
	return wpa_s->driver->set_p2p_powersave(wpa_s->drv_priv, legacy_ps,
						opp_ps, ctwindow);
}

static inline int wpa_drv_ampdu(struct wpa_supplicant *wpa_s, int ampdu)
{
	if (!wpa_s->driver->ampdu)
		return -1;
	return wpa_s->driver->ampdu(wpa_s->drv_priv, ampdu);
}

static inline int wpa_drv_send_tdls_mgmt(struct wpa_supplicant *wpa_s,
					 const u8 *dst, u8 action_code,
					 u8 dialog_token, u16 status_code,
					 u32 peer_capab, int initiator,
					 const u8 *buf, size_t len)
{
	if (wpa_s->driver->send_tdls_mgmt) {
		return wpa_s->driver->send_tdls_mgmt(wpa_s->drv_priv, dst,
						     action_code, dialog_token,
						     status_code, peer_capab,
						     initiator, buf, len);
	}
	return -1;
}

static inline int wpa_drv_tdls_oper(struct wpa_supplicant *wpa_s,
				    enum tdls_oper oper, const u8 *peer)
{
	if (!wpa_s->driver->tdls_oper)
		return -1;
	return wpa_s->driver->tdls_oper(wpa_s->drv_priv, oper, peer);
}

#ifdef ANDROID
static inline int wpa_drv_driver_cmd(struct wpa_supplicant *wpa_s,
				     char *cmd, char *buf, size_t buf_len)
{
	if (!wpa_s->driver->driver_cmd)
		return -1;
	return wpa_s->driver->driver_cmd(wpa_s->drv_priv, cmd, buf, buf_len);
}
#endif /* ANDROID */

static inline void wpa_drv_set_rekey_info(struct wpa_supplicant *wpa_s,
					  const u8 *kek, size_t kek_len,
					  const u8 *kck, size_t kck_len,
					  const u8 *replay_ctr)
{
	if (!wpa_s->driver->set_rekey_info)
		return;
	wpa_s->driver->set_rekey_info(wpa_s->drv_priv, kek, kek_len,
				      kck, kck_len, replay_ctr);
}

static inline int wpa_drv_radio_disable(struct wpa_supplicant *wpa_s,
					int disabled)
{
	if (!wpa_s->driver->radio_disable)
		return -1;
	return wpa_s->driver->radio_disable(wpa_s->drv_priv, disabled);
}

static inline int wpa_drv_switch_channel(struct wpa_supplicant *wpa_s,
					 struct csa_settings *settings)
{
	if (!wpa_s->driver->switch_channel)
		return -1;
	return wpa_s->driver->switch_channel(wpa_s->drv_priv, settings);
}

static inline int wpa_drv_add_ts(struct wpa_supplicant *wpa_s, u8 tsid,
				 const u8 *address, u8 user_priority,
				 u16 admitted_time)
{
	if (!wpa_s->driver->add_tx_ts)
		return -1;
	return wpa_s->driver->add_tx_ts(wpa_s->drv_priv, tsid, address,
					user_priority, admitted_time);
}

static inline int wpa_drv_del_ts(struct wpa_supplicant *wpa_s, u8 tid,
				 const u8 *address)
{
	if (!wpa_s->driver->del_tx_ts)
		return -1;
	return wpa_s->driver->del_tx_ts(wpa_s->drv_priv, tid, address);
}

static inline int wpa_drv_tdls_enable_channel_switch(
	struct wpa_supplicant *wpa_s, const u8 *addr, u8 oper_class,
	const struct hostapd_freq_params *freq_params)
{
	if (!wpa_s->driver->tdls_enable_channel_switch)
		return -1;
	return wpa_s->driver->tdls_enable_channel_switch(wpa_s->drv_priv, addr,
							 oper_class,
							 freq_params);
}

static inline int
wpa_drv_tdls_disable_channel_switch(struct wpa_supplicant *wpa_s,
				    const u8 *addr)
{
	if (!wpa_s->driver->tdls_disable_channel_switch)
		return -1;
	return wpa_s->driver->tdls_disable_channel_switch(wpa_s->drv_priv,
							  addr);
}

static inline int wpa_drv_wnm_oper(struct wpa_supplicant *wpa_s,
				   enum wnm_oper oper, const u8 *peer,
				   u8 *buf, u16 *buf_len)
{
	if (!wpa_s->driver->wnm_oper)
		return -1;
	return wpa_s->driver->wnm_oper(wpa_s->drv_priv, oper, peer, buf,
				       buf_len);
}

static inline int wpa_drv_status(struct wpa_supplicant *wpa_s,
				 char *buf, size_t buflen)
{
	if (!wpa_s->driver->status)
		return -1;
	return wpa_s->driver->status(wpa_s->drv_priv, buf, buflen);
}

static inline int wpa_drv_set_qos_map(struct wpa_supplicant *wpa_s,
				      const u8 *qos_map_set, u8 qos_map_set_len)
{
	if (!wpa_s->driver->set_qos_map)
		return -1;
	return wpa_s->driver->set_qos_map(wpa_s->drv_priv, qos_map_set,
					  qos_map_set_len);
}

static inline int wpa_drv_wowlan(struct wpa_supplicant *wpa_s,
				 const struct wowlan_triggers *triggers)
{
	if (!wpa_s->driver->set_wowlan)
		return -1;
	return wpa_s->driver->set_wowlan(wpa_s->drv_priv, triggers);
}

static inline int wpa_drv_vendor_cmd(struct wpa_supplicant *wpa_s,
				     int vendor_id, int subcmd, const u8 *data,
				     size_t data_len, struct wpabuf *buf)
{
	if (!wpa_s->driver->vendor_cmd)
		return -1;
	return wpa_s->driver->vendor_cmd(wpa_s->drv_priv, vendor_id, subcmd,
					 data, data_len, buf);
}

static inline int wpa_drv_roaming(struct wpa_supplicant *wpa_s, int allowed,
				  const u8 *bssid)
{
	if (!wpa_s->driver->roaming)
		return -1;
	return wpa_s->driver->roaming(wpa_s->drv_priv, allowed, bssid);
}

static inline int wpa_drv_disable_fils(struct wpa_supplicant *wpa_s,
				       int disable)
{
	if (!wpa_s->driver->disable_fils)
		return -1;
	return wpa_s->driver->disable_fils(wpa_s->drv_priv, disable);
}

static inline int wpa_drv_set_mac_addr(struct wpa_supplicant *wpa_s,
				       const u8 *addr)
{
	if (!wpa_s->driver->set_mac_addr)
		return -1;
	return wpa_s->driver->set_mac_addr(wpa_s->drv_priv, addr);
}


#ifdef CONFIG_MACSEC

static inline int wpa_drv_macsec_init(struct wpa_supplicant *wpa_s,
				      struct macsec_init_params *params)
{
	if (!wpa_s->driver->macsec_init)
		return -1;
	return wpa_s->driver->macsec_init(wpa_s->drv_priv, params);
}

static inline int wpa_drv_macsec_deinit(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s->driver->macsec_deinit)
		return -1;
	return wpa_s->driver->macsec_deinit(wpa_s->drv_priv);
}

static inline int wpa_drv_macsec_get_capability(struct wpa_supplicant *wpa_s,
						enum macsec_cap *cap)
{
	if (!wpa_s->driver->macsec_get_capability)
		return -1;
	return wpa_s->driver->macsec_get_capability(wpa_s->drv_priv, cap);
}

static inline int wpa_drv_enable_protect_frames(struct wpa_supplicant *wpa_s,
						Boolean enabled)
{
	if (!wpa_s->driver->enable_protect_frames)
		return -1;
	return wpa_s->driver->enable_protect_frames(wpa_s->drv_priv, enabled);
}

static inline int wpa_drv_enable_encrypt(struct wpa_supplicant *wpa_s,
						Boolean enabled)
{
	if (!wpa_s->driver->enable_encrypt)
		return -1;
	return wpa_s->driver->enable_encrypt(wpa_s->drv_priv, enabled);
}

static inline int wpa_drv_set_replay_protect(struct wpa_supplicant *wpa_s,
					     Boolean enabled, u32 window)
{
	if (!wpa_s->driver->set_replay_protect)
		return -1;
	return wpa_s->driver->set_replay_protect(wpa_s->drv_priv, enabled,
						 window);
}

static inline int wpa_drv_set_current_cipher_suite(struct wpa_supplicant *wpa_s,
						   u64 cs)
{
	if (!wpa_s->driver->set_current_cipher_suite)
		return -1;
	return wpa_s->driver->set_current_cipher_suite(wpa_s->drv_priv, cs);
}

static inline int wpa_drv_enable_controlled_port(struct wpa_supplicant *wpa_s,
						 Boolean enabled)
{
	if (!wpa_s->driver->enable_controlled_port)
		return -1;
	return wpa_s->driver->enable_controlled_port(wpa_s->drv_priv, enabled);
}

static inline int wpa_drv_get_receive_lowest_pn(struct wpa_supplicant *wpa_s,
						struct receive_sa *sa)
{
	if (!wpa_s->driver->get_receive_lowest_pn)
		return -1;
	return wpa_s->driver->get_receive_lowest_pn(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_get_transmit_next_pn(struct wpa_supplicant *wpa_s,
						struct transmit_sa *sa)
{
	if (!wpa_s->driver->get_transmit_next_pn)
		return -1;
	return wpa_s->driver->get_transmit_next_pn(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_set_transmit_next_pn(struct wpa_supplicant *wpa_s,
						struct transmit_sa *sa)
{
	if (!wpa_s->driver->set_transmit_next_pn)
		return -1;
	return wpa_s->driver->set_transmit_next_pn(wpa_s->drv_priv, sa);
}

static inline int
wpa_drv_create_receive_sc(struct wpa_supplicant *wpa_s, struct receive_sc *sc,
			  unsigned int conf_offset, int validation)
{
	if (!wpa_s->driver->create_receive_sc)
		return -1;
	return wpa_s->driver->create_receive_sc(wpa_s->drv_priv, sc,
						conf_offset, validation);
}

static inline int wpa_drv_delete_receive_sc(struct wpa_supplicant *wpa_s,
					    struct receive_sc *sc)
{
	if (!wpa_s->driver->delete_receive_sc)
		return -1;
	return wpa_s->driver->delete_receive_sc(wpa_s->drv_priv, sc);
}

static inline int wpa_drv_create_receive_sa(struct wpa_supplicant *wpa_s,
					    struct receive_sa *sa)
{
	if (!wpa_s->driver->create_receive_sa)
		return -1;
	return wpa_s->driver->create_receive_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_delete_receive_sa(struct wpa_supplicant *wpa_s,
					    struct receive_sa *sa)
{
	if (!wpa_s->driver->delete_receive_sa)
		return -1;
	return wpa_s->driver->delete_receive_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_enable_receive_sa(struct wpa_supplicant *wpa_s,
					    struct receive_sa *sa)
{
	if (!wpa_s->driver->enable_receive_sa)
		return -1;
	return wpa_s->driver->enable_receive_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_disable_receive_sa(struct wpa_supplicant *wpa_s,
					     struct receive_sa *sa)
{
	if (!wpa_s->driver->disable_receive_sa)
		return -1;
	return wpa_s->driver->disable_receive_sa(wpa_s->drv_priv, sa);
}

static inline int
wpa_drv_create_transmit_sc(struct wpa_supplicant *wpa_s, struct transmit_sc *sc,
			   unsigned int conf_offset)
{
	if (!wpa_s->driver->create_transmit_sc)
		return -1;
	return wpa_s->driver->create_transmit_sc(wpa_s->drv_priv, sc,
						 conf_offset);
}

static inline int wpa_drv_delete_transmit_sc(struct wpa_supplicant *wpa_s,
					     struct transmit_sc *sc)
{
	if (!wpa_s->driver->delete_transmit_sc)
		return -1;
	return wpa_s->driver->delete_transmit_sc(wpa_s->drv_priv, sc);
}

static inline int wpa_drv_create_transmit_sa(struct wpa_supplicant *wpa_s,
					     struct transmit_sa *sa)
{
	if (!wpa_s->driver->create_transmit_sa)
		return -1;
	return wpa_s->driver->create_transmit_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_delete_transmit_sa(struct wpa_supplicant *wpa_s,
					     struct transmit_sa *sa)
{
	if (!wpa_s->driver->delete_transmit_sa)
		return -1;
	return wpa_s->driver->delete_transmit_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_enable_transmit_sa(struct wpa_supplicant *wpa_s,
					     struct transmit_sa *sa)
{
	if (!wpa_s->driver->enable_transmit_sa)
		return -1;
	return wpa_s->driver->enable_transmit_sa(wpa_s->drv_priv, sa);
}

static inline int wpa_drv_disable_transmit_sa(struct wpa_supplicant *wpa_s,
					      struct transmit_sa *sa)
{
	if (!wpa_s->driver->disable_transmit_sa)
		return -1;
	return wpa_s->driver->disable_transmit_sa(wpa_s->drv_priv, sa);
}
#endif /* CONFIG_MACSEC */

static inline int wpa_drv_setband(struct wpa_supplicant *wpa_s,
				  enum set_band band)
{
	if (!wpa_s->driver->set_band)
		return -1;
	return wpa_s->driver->set_band(wpa_s->drv_priv, band);
}

static inline int wpa_drv_get_pref_freq_list(struct wpa_supplicant *wpa_s,
					     enum wpa_driver_if_type if_type,
					     unsigned int *num,
					     unsigned int *freq_list)
{
#ifdef CONFIG_TESTING_OPTIONS
	if (wpa_s->get_pref_freq_list_override)
		return wpas_ctrl_iface_get_pref_freq_list_override(
			wpa_s, if_type, num, freq_list);
#endif /* CONFIG_TESTING_OPTIONS */
	if (!wpa_s->driver->get_pref_freq_list)
		return -1;
	return wpa_s->driver->get_pref_freq_list(wpa_s->drv_priv, if_type,
						 num, freq_list);
}

static inline int wpa_drv_set_prob_oper_freq(struct wpa_supplicant *wpa_s,
					     unsigned int freq)
{
	if (!wpa_s->driver->set_prob_oper_freq)
		return 0;
	return wpa_s->driver->set_prob_oper_freq(wpa_s->drv_priv, freq);
}

static inline int wpa_drv_abort_scan(struct wpa_supplicant *wpa_s,
				     u64 scan_cookie)
{
	if (!wpa_s->driver->abort_scan)
		return -1;
	return wpa_s->driver->abort_scan(wpa_s->drv_priv, scan_cookie);
}

static inline int wpa_drv_configure_frame_filters(struct wpa_supplicant *wpa_s,
						  u32 filters)
{
	if (!wpa_s->driver->configure_data_frame_filters)
		return -1;
	return wpa_s->driver->configure_data_frame_filters(wpa_s->drv_priv,
							   filters);
}

static inline int wpa_drv_get_ext_capa(struct wpa_supplicant *wpa_s,
				       enum wpa_driver_if_type type)
{
	if (!wpa_s->driver->get_ext_capab)
		return -1;
	return wpa_s->driver->get_ext_capab(wpa_s->drv_priv, type,
					    &wpa_s->extended_capa,
					    &wpa_s->extended_capa_mask,
					    &wpa_s->extended_capa_len);
}

static inline int wpa_drv_p2p_lo_start(struct wpa_supplicant *wpa_s,
				       unsigned int channel,
				       unsigned int period,
				       unsigned int interval,
				       unsigned int count,
				       const u8 *device_types,
				       size_t dev_types_len,
				       const u8 *ies, size_t ies_len)
{
	if (!wpa_s->driver->p2p_lo_start)
		return -1;
	return wpa_s->driver->p2p_lo_start(wpa_s->drv_priv, channel, period,
					   interval, count, device_types,
					   dev_types_len, ies, ies_len);
}

static inline int wpa_drv_p2p_lo_stop(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s->driver->p2p_lo_stop)
		return -1;
	return wpa_s->driver->p2p_lo_stop(wpa_s->drv_priv);
}

static inline int wpa_drv_set_default_scan_ies(struct wpa_supplicant *wpa_s,
					       const u8 *ies, size_t len)
{
	if (!wpa_s->driver->set_default_scan_ies)
		return -1;
	return wpa_s->driver->set_default_scan_ies(wpa_s->drv_priv, ies, len);
}

static inline int wpa_drv_set_tdls_mode(struct wpa_supplicant *wpa_s,
					int tdls_external_control)
{
	if (!wpa_s->driver->set_tdls_mode)
		return -1;
	return wpa_s->driver->set_tdls_mode(wpa_s->drv_priv,
					    tdls_external_control);
}

static inline struct wpa_bss_candidate_info *
wpa_drv_get_bss_trans_status(struct wpa_supplicant *wpa_s,
			     struct wpa_bss_trans_info *params)
{
	if (!wpa_s->driver->get_bss_transition_status)
		return NULL;
	return wpa_s->driver->get_bss_transition_status(wpa_s->drv_priv,
							params);
}

static inline int wpa_drv_ignore_assoc_disallow(struct wpa_supplicant *wpa_s,
						int val)
{
	if (!wpa_s->driver->ignore_assoc_disallow)
		return -1;
	return wpa_s->driver->ignore_assoc_disallow(wpa_s->drv_priv, val);
}

static inline int wpa_drv_set_bssid_blacklist(struct wpa_supplicant *wpa_s,
					      unsigned int num_bssid,
					      const u8 *bssids)
{
	if (!wpa_s->driver->set_bssid_blacklist)
		return -1;
	return wpa_s->driver->set_bssid_blacklist(wpa_s->drv_priv, num_bssid,
						  bssids);
}

static inline int wpa_drv_update_connect_params(
	struct wpa_supplicant *wpa_s,
	struct wpa_driver_associate_params *params,
	enum wpa_drv_update_connect_params_mask mask)
{
	if (!wpa_s->driver->update_connect_params)
		return -1;
	return wpa_s->driver->update_connect_params(wpa_s->drv_priv, params,
						    mask);
}

static inline int
wpa_drv_send_external_auth_status(struct wpa_supplicant *wpa_s,
				  struct external_auth *params)
{
	if (!wpa_s->driver->send_external_auth_status)
		return -1;
	return wpa_s->driver->send_external_auth_status(wpa_s->drv_priv,
							params);
}

#endif /* DRIVER_I_H */
