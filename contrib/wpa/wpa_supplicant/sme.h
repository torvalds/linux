/*
 * wpa_supplicant - SME
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SME_H
#define SME_H

#ifdef CONFIG_SME

void sme_authenticate(struct wpa_supplicant *wpa_s,
		      struct wpa_bss *bss, struct wpa_ssid *ssid);
void sme_associate(struct wpa_supplicant *wpa_s, enum wpas_mode mode,
		   const u8 *bssid, u16 auth_type);
void sme_event_auth(struct wpa_supplicant *wpa_s, union wpa_event_data *data);
int sme_update_ft_ies(struct wpa_supplicant *wpa_s, const u8 *md,
		      const u8 *ies, size_t ies_len);
void sme_event_assoc_reject(struct wpa_supplicant *wpa_s,
			    union wpa_event_data *data);
void sme_event_auth_timed_out(struct wpa_supplicant *wpa_s,
			      union wpa_event_data *data);
void sme_event_assoc_timed_out(struct wpa_supplicant *wpa_s,
			       union wpa_event_data *data);
void sme_event_disassoc(struct wpa_supplicant *wpa_s,
			struct disassoc_info *info);
void sme_event_unprot_disconnect(struct wpa_supplicant *wpa_s, const u8 *sa,
				 const u8 *da, u16 reason_code);
void sme_sa_query_rx(struct wpa_supplicant *wpa_s, const u8 *sa,
		     const u8 *data, size_t len);
void sme_state_changed(struct wpa_supplicant *wpa_s);
void sme_disassoc_while_authenticating(struct wpa_supplicant *wpa_s,
				       const u8 *prev_pending_bssid);
void sme_clear_on_disassoc(struct wpa_supplicant *wpa_s);
void sme_deinit(struct wpa_supplicant *wpa_s);

int sme_proc_obss_scan(struct wpa_supplicant *wpa_s);
void sme_sched_obss_scan(struct wpa_supplicant *wpa_s, int enable);
void sme_external_auth_trigger(struct wpa_supplicant *wpa_s,
			       union wpa_event_data *data);
void sme_external_auth_mgmt_rx(struct wpa_supplicant *wpa_s,
			       const u8 *auth_frame, size_t len);

#else /* CONFIG_SME */

static inline void sme_authenticate(struct wpa_supplicant *wpa_s,
				    struct wpa_bss *bss,
				    struct wpa_ssid *ssid)
{
}

static inline void sme_event_auth(struct wpa_supplicant *wpa_s,
				  union wpa_event_data *data)
{
}

static inline int sme_update_ft_ies(struct wpa_supplicant *wpa_s, const u8 *md,
				    const u8 *ies, size_t ies_len)
{
	return -1;
}


static inline void sme_event_assoc_reject(struct wpa_supplicant *wpa_s,
					  union wpa_event_data *data)
{
}

static inline void sme_event_auth_timed_out(struct wpa_supplicant *wpa_s,
					    union wpa_event_data *data)
{
}

static inline void sme_event_assoc_timed_out(struct wpa_supplicant *wpa_s,
					     union wpa_event_data *data)
{
}

static inline void sme_event_disassoc(struct wpa_supplicant *wpa_s,
				      struct disassoc_info *info)
{
}

static inline void sme_event_unprot_disconnect(struct wpa_supplicant *wpa_s,
					       const u8 *sa, const u8 *da,
					       u16 reason_code)
{
}

static inline void sme_state_changed(struct wpa_supplicant *wpa_s)
{
}

static inline void
sme_disassoc_while_authenticating(struct wpa_supplicant *wpa_s,
				  const u8 *prev_pending_bssid)
{
}

static inline void sme_clear_on_disassoc(struct wpa_supplicant *wpa_s)
{
}

static inline void sme_deinit(struct wpa_supplicant *wpa_s)
{
}

static inline int sme_proc_obss_scan(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline void sme_sched_obss_scan(struct wpa_supplicant *wpa_s,
				       int enable)
{
}

static inline void sme_external_auth_trigger(struct wpa_supplicant *wpa_s,
					     union wpa_event_data *data)
{
}

static inline void sme_external_auth_mgmt_rx(struct wpa_supplicant *wpa_s,
					     const u8 *auth_frame, size_t len)
{
}

#endif /* CONFIG_SME */

#endif /* SME_H */
