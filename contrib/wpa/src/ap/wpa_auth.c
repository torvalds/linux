/*
 * IEEE 802.11 RSN / WPA Authenticator
 * Copyright (c) 2004-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/state_machine.h"
#include "utils/bitfield.h"
#include "common/ieee802_11_defs.h"
#include "crypto/aes.h"
#include "crypto/aes_wrap.h"
#include "crypto/aes_siv.h"
#include "crypto/crypto.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha384.h"
#include "crypto/random.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "ap_config.h"
#include "ieee802_11.h"
#include "wpa_auth.h"
#include "pmksa_cache_auth.h"
#include "wpa_auth_i.h"
#include "wpa_auth_ie.h"

#define STATE_MACHINE_DATA struct wpa_state_machine
#define STATE_MACHINE_DEBUG_PREFIX "WPA"
#define STATE_MACHINE_ADDR sm->addr


static void wpa_send_eapol_timeout(void *eloop_ctx, void *timeout_ctx);
static int wpa_sm_step(struct wpa_state_machine *sm);
static int wpa_verify_key_mic(int akmp, size_t pmk_len, struct wpa_ptk *PTK,
			      u8 *data, size_t data_len);
#ifdef CONFIG_FILS
static int wpa_aead_decrypt(struct wpa_state_machine *sm, struct wpa_ptk *ptk,
			    u8 *buf, size_t buf_len, u16 *_key_data_len);
static struct wpabuf * fils_prepare_plainbuf(struct wpa_state_machine *sm,
					     const struct wpabuf *hlp);
#endif /* CONFIG_FILS */
static void wpa_sm_call_step(void *eloop_ctx, void *timeout_ctx);
static void wpa_group_sm_step(struct wpa_authenticator *wpa_auth,
			      struct wpa_group *group);
static void wpa_request_new_ptk(struct wpa_state_machine *sm);
static int wpa_gtk_update(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group);
static int wpa_group_config_group_keys(struct wpa_authenticator *wpa_auth,
				       struct wpa_group *group);
static int wpa_derive_ptk(struct wpa_state_machine *sm, const u8 *snonce,
			  const u8 *pmk, unsigned int pmk_len,
			  struct wpa_ptk *ptk);
static void wpa_group_free(struct wpa_authenticator *wpa_auth,
			   struct wpa_group *group);
static void wpa_group_get(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group);
static void wpa_group_put(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group);
static u8 * ieee80211w_kde_add(struct wpa_state_machine *sm, u8 *pos);

static const u32 eapol_key_timeout_first = 100; /* ms */
static const u32 eapol_key_timeout_subseq = 1000; /* ms */
static const u32 eapol_key_timeout_first_group = 500; /* ms */
static const u32 eapol_key_timeout_no_retrans = 4000; /* ms */

/* TODO: make these configurable */
static const int dot11RSNAConfigPMKLifetime = 43200;
static const int dot11RSNAConfigPMKReauthThreshold = 70;
static const int dot11RSNAConfigSATimeout = 60;


static inline int wpa_auth_mic_failure_report(
	struct wpa_authenticator *wpa_auth, const u8 *addr)
{
	if (wpa_auth->cb->mic_failure_report)
		return wpa_auth->cb->mic_failure_report(wpa_auth->cb_ctx, addr);
	return 0;
}


static inline void wpa_auth_psk_failure_report(
	struct wpa_authenticator *wpa_auth, const u8 *addr)
{
	if (wpa_auth->cb->psk_failure_report)
		wpa_auth->cb->psk_failure_report(wpa_auth->cb_ctx, addr);
}


static inline void wpa_auth_set_eapol(struct wpa_authenticator *wpa_auth,
				      const u8 *addr, wpa_eapol_variable var,
				      int value)
{
	if (wpa_auth->cb->set_eapol)
		wpa_auth->cb->set_eapol(wpa_auth->cb_ctx, addr, var, value);
}


static inline int wpa_auth_get_eapol(struct wpa_authenticator *wpa_auth,
				     const u8 *addr, wpa_eapol_variable var)
{
	if (wpa_auth->cb->get_eapol == NULL)
		return -1;
	return wpa_auth->cb->get_eapol(wpa_auth->cb_ctx, addr, var);
}


static inline const u8 * wpa_auth_get_psk(struct wpa_authenticator *wpa_auth,
					  const u8 *addr,
					  const u8 *p2p_dev_addr,
					  const u8 *prev_psk, size_t *psk_len)
{
	if (wpa_auth->cb->get_psk == NULL)
		return NULL;
	return wpa_auth->cb->get_psk(wpa_auth->cb_ctx, addr, p2p_dev_addr,
				     prev_psk, psk_len);
}


static inline int wpa_auth_get_msk(struct wpa_authenticator *wpa_auth,
				   const u8 *addr, u8 *msk, size_t *len)
{
	if (wpa_auth->cb->get_msk == NULL)
		return -1;
	return wpa_auth->cb->get_msk(wpa_auth->cb_ctx, addr, msk, len);
}


static inline int wpa_auth_set_key(struct wpa_authenticator *wpa_auth,
				   int vlan_id,
				   enum wpa_alg alg, const u8 *addr, int idx,
				   u8 *key, size_t key_len)
{
	if (wpa_auth->cb->set_key == NULL)
		return -1;
	return wpa_auth->cb->set_key(wpa_auth->cb_ctx, vlan_id, alg, addr, idx,
				     key, key_len);
}


static inline int wpa_auth_get_seqnum(struct wpa_authenticator *wpa_auth,
				      const u8 *addr, int idx, u8 *seq)
{
	if (wpa_auth->cb->get_seqnum == NULL)
		return -1;
	return wpa_auth->cb->get_seqnum(wpa_auth->cb_ctx, addr, idx, seq);
}


static inline int
wpa_auth_send_eapol(struct wpa_authenticator *wpa_auth, const u8 *addr,
		    const u8 *data, size_t data_len, int encrypt)
{
	if (wpa_auth->cb->send_eapol == NULL)
		return -1;
	return wpa_auth->cb->send_eapol(wpa_auth->cb_ctx, addr, data, data_len,
					encrypt);
}


#ifdef CONFIG_MESH
static inline int wpa_auth_start_ampe(struct wpa_authenticator *wpa_auth,
				      const u8 *addr)
{
	if (wpa_auth->cb->start_ampe == NULL)
		return -1;
	return wpa_auth->cb->start_ampe(wpa_auth->cb_ctx, addr);
}
#endif /* CONFIG_MESH */


int wpa_auth_for_each_sta(struct wpa_authenticator *wpa_auth,
			  int (*cb)(struct wpa_state_machine *sm, void *ctx),
			  void *cb_ctx)
{
	if (wpa_auth->cb->for_each_sta == NULL)
		return 0;
	return wpa_auth->cb->for_each_sta(wpa_auth->cb_ctx, cb, cb_ctx);
}


int wpa_auth_for_each_auth(struct wpa_authenticator *wpa_auth,
			   int (*cb)(struct wpa_authenticator *a, void *ctx),
			   void *cb_ctx)
{
	if (wpa_auth->cb->for_each_auth == NULL)
		return 0;
	return wpa_auth->cb->for_each_auth(wpa_auth->cb_ctx, cb, cb_ctx);
}


void wpa_auth_logger(struct wpa_authenticator *wpa_auth, const u8 *addr,
		     logger_level level, const char *txt)
{
	if (wpa_auth->cb->logger == NULL)
		return;
	wpa_auth->cb->logger(wpa_auth->cb_ctx, addr, level, txt);
}


void wpa_auth_vlogger(struct wpa_authenticator *wpa_auth, const u8 *addr,
		      logger_level level, const char *fmt, ...)
{
	char *format;
	int maxlen;
	va_list ap;

	if (wpa_auth->cb->logger == NULL)
		return;

	maxlen = os_strlen(fmt) + 100;
	format = os_malloc(maxlen);
	if (!format)
		return;

	va_start(ap, fmt);
	vsnprintf(format, maxlen, fmt, ap);
	va_end(ap);

	wpa_auth_logger(wpa_auth, addr, level, format);

	os_free(format);
}


static void wpa_sta_disconnect(struct wpa_authenticator *wpa_auth,
			       const u8 *addr, u16 reason)
{
	if (wpa_auth->cb->disconnect == NULL)
		return;
	wpa_printf(MSG_DEBUG, "wpa_sta_disconnect STA " MACSTR " (reason %u)",
		   MAC2STR(addr), reason);
	wpa_auth->cb->disconnect(wpa_auth->cb_ctx, addr, reason);
}


static void wpa_rekey_gmk(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_authenticator *wpa_auth = eloop_ctx;

	if (random_get_bytes(wpa_auth->group->GMK, WPA_GMK_LEN)) {
		wpa_printf(MSG_ERROR, "Failed to get random data for WPA "
			   "initialization.");
	} else {
		wpa_auth_logger(wpa_auth, NULL, LOGGER_DEBUG, "GMK rekeyd");
		wpa_hexdump_key(MSG_DEBUG, "GMK",
				wpa_auth->group->GMK, WPA_GMK_LEN);
	}

	if (wpa_auth->conf.wpa_gmk_rekey) {
		eloop_register_timeout(wpa_auth->conf.wpa_gmk_rekey, 0,
				       wpa_rekey_gmk, wpa_auth, NULL);
	}
}


static void wpa_rekey_gtk(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_authenticator *wpa_auth = eloop_ctx;
	struct wpa_group *group, *next;

	wpa_auth_logger(wpa_auth, NULL, LOGGER_DEBUG, "rekeying GTK");
	group = wpa_auth->group;
	while (group) {
		wpa_group_get(wpa_auth, group);

		group->GTKReKey = TRUE;
		do {
			group->changed = FALSE;
			wpa_group_sm_step(wpa_auth, group);
		} while (group->changed);

		next = group->next;
		wpa_group_put(wpa_auth, group);
		group = next;
	}

	if (wpa_auth->conf.wpa_group_rekey) {
		eloop_register_timeout(wpa_auth->conf.wpa_group_rekey,
				       0, wpa_rekey_gtk, wpa_auth, NULL);
	}
}


static void wpa_rekey_ptk(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_authenticator *wpa_auth = eloop_ctx;
	struct wpa_state_machine *sm = timeout_ctx;

	wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG, "rekeying PTK");
	wpa_request_new_ptk(sm);
	wpa_sm_step(sm);
}


static int wpa_auth_pmksa_clear_cb(struct wpa_state_machine *sm, void *ctx)
{
	if (sm->pmksa == ctx)
		sm->pmksa = NULL;
	return 0;
}


static void wpa_auth_pmksa_free_cb(struct rsn_pmksa_cache_entry *entry,
				   void *ctx)
{
	struct wpa_authenticator *wpa_auth = ctx;
	wpa_auth_for_each_sta(wpa_auth, wpa_auth_pmksa_clear_cb, entry);
}


static int wpa_group_init_gmk_and_counter(struct wpa_authenticator *wpa_auth,
					  struct wpa_group *group)
{
	u8 buf[ETH_ALEN + 8 + sizeof(unsigned long)];
	u8 rkey[32];
	unsigned long ptr;

	if (random_get_bytes(group->GMK, WPA_GMK_LEN) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "GMK", group->GMK, WPA_GMK_LEN);

	/*
	 * Counter = PRF-256(Random number, "Init Counter",
	 *                   Local MAC Address || Time)
	 */
	os_memcpy(buf, wpa_auth->addr, ETH_ALEN);
	wpa_get_ntp_timestamp(buf + ETH_ALEN);
	ptr = (unsigned long) group;
	os_memcpy(buf + ETH_ALEN + 8, &ptr, sizeof(ptr));
	if (random_get_bytes(rkey, sizeof(rkey)) < 0)
		return -1;

	if (sha1_prf(rkey, sizeof(rkey), "Init Counter", buf, sizeof(buf),
		     group->Counter, WPA_NONCE_LEN) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "Key Counter",
			group->Counter, WPA_NONCE_LEN);

	return 0;
}


static struct wpa_group * wpa_group_init(struct wpa_authenticator *wpa_auth,
					 int vlan_id, int delay_init)
{
	struct wpa_group *group;

	group = os_zalloc(sizeof(struct wpa_group));
	if (group == NULL)
		return NULL;

	group->GTKAuthenticator = TRUE;
	group->vlan_id = vlan_id;
	group->GTK_len = wpa_cipher_key_len(wpa_auth->conf.wpa_group);

	if (random_pool_ready() != 1) {
		wpa_printf(MSG_INFO, "WPA: Not enough entropy in random pool "
			   "for secure operations - update keys later when "
			   "the first station connects");
	}

	/*
	 * Set initial GMK/Counter value here. The actual values that will be
	 * used in negotiations will be set once the first station tries to
	 * connect. This allows more time for collecting additional randomness
	 * on embedded devices.
	 */
	if (wpa_group_init_gmk_and_counter(wpa_auth, group) < 0) {
		wpa_printf(MSG_ERROR, "Failed to get random data for WPA "
			   "initialization.");
		os_free(group);
		return NULL;
	}

	group->GInit = TRUE;
	if (delay_init) {
		wpa_printf(MSG_DEBUG, "WPA: Delay group state machine start "
			   "until Beacon frames have been configured");
		/* Initialization is completed in wpa_init_keys(). */
	} else {
		wpa_group_sm_step(wpa_auth, group);
		group->GInit = FALSE;
		wpa_group_sm_step(wpa_auth, group);
	}

	return group;
}


/**
 * wpa_init - Initialize WPA authenticator
 * @addr: Authenticator address
 * @conf: Configuration for WPA authenticator
 * @cb: Callback functions for WPA authenticator
 * Returns: Pointer to WPA authenticator data or %NULL on failure
 */
struct wpa_authenticator * wpa_init(const u8 *addr,
				    struct wpa_auth_config *conf,
				    const struct wpa_auth_callbacks *cb,
				    void *cb_ctx)
{
	struct wpa_authenticator *wpa_auth;

	wpa_auth = os_zalloc(sizeof(struct wpa_authenticator));
	if (wpa_auth == NULL)
		return NULL;
	os_memcpy(wpa_auth->addr, addr, ETH_ALEN);
	os_memcpy(&wpa_auth->conf, conf, sizeof(*conf));
	wpa_auth->cb = cb;
	wpa_auth->cb_ctx = cb_ctx;

	if (wpa_auth_gen_wpa_ie(wpa_auth)) {
		wpa_printf(MSG_ERROR, "Could not generate WPA IE.");
		os_free(wpa_auth);
		return NULL;
	}

	wpa_auth->group = wpa_group_init(wpa_auth, 0, 1);
	if (wpa_auth->group == NULL) {
		os_free(wpa_auth->wpa_ie);
		os_free(wpa_auth);
		return NULL;
	}

	wpa_auth->pmksa = pmksa_cache_auth_init(wpa_auth_pmksa_free_cb,
						wpa_auth);
	if (wpa_auth->pmksa == NULL) {
		wpa_printf(MSG_ERROR, "PMKSA cache initialization failed.");
		os_free(wpa_auth->group);
		os_free(wpa_auth->wpa_ie);
		os_free(wpa_auth);
		return NULL;
	}

#ifdef CONFIG_IEEE80211R_AP
	wpa_auth->ft_pmk_cache = wpa_ft_pmk_cache_init();
	if (wpa_auth->ft_pmk_cache == NULL) {
		wpa_printf(MSG_ERROR, "FT PMK cache initialization failed.");
		os_free(wpa_auth->group);
		os_free(wpa_auth->wpa_ie);
		pmksa_cache_auth_deinit(wpa_auth->pmksa);
		os_free(wpa_auth);
		return NULL;
	}
#endif /* CONFIG_IEEE80211R_AP */

	if (wpa_auth->conf.wpa_gmk_rekey) {
		eloop_register_timeout(wpa_auth->conf.wpa_gmk_rekey, 0,
				       wpa_rekey_gmk, wpa_auth, NULL);
	}

	if (wpa_auth->conf.wpa_group_rekey) {
		eloop_register_timeout(wpa_auth->conf.wpa_group_rekey, 0,
				       wpa_rekey_gtk, wpa_auth, NULL);
	}

#ifdef CONFIG_P2P
	if (WPA_GET_BE32(conf->ip_addr_start)) {
		int count = WPA_GET_BE32(conf->ip_addr_end) -
			WPA_GET_BE32(conf->ip_addr_start) + 1;
		if (count > 1000)
			count = 1000;
		if (count > 0)
			wpa_auth->ip_pool = bitfield_alloc(count);
	}
#endif /* CONFIG_P2P */

	return wpa_auth;
}


int wpa_init_keys(struct wpa_authenticator *wpa_auth)
{
	struct wpa_group *group = wpa_auth->group;

	wpa_printf(MSG_DEBUG, "WPA: Start group state machine to set initial "
		   "keys");
	wpa_group_sm_step(wpa_auth, group);
	group->GInit = FALSE;
	wpa_group_sm_step(wpa_auth, group);
	if (group->wpa_group_state == WPA_GROUP_FATAL_FAILURE)
		return -1;
	return 0;
}


/**
 * wpa_deinit - Deinitialize WPA authenticator
 * @wpa_auth: Pointer to WPA authenticator data from wpa_init()
 */
void wpa_deinit(struct wpa_authenticator *wpa_auth)
{
	struct wpa_group *group, *prev;

	eloop_cancel_timeout(wpa_rekey_gmk, wpa_auth, NULL);
	eloop_cancel_timeout(wpa_rekey_gtk, wpa_auth, NULL);

	pmksa_cache_auth_deinit(wpa_auth->pmksa);

#ifdef CONFIG_IEEE80211R_AP
	wpa_ft_pmk_cache_deinit(wpa_auth->ft_pmk_cache);
	wpa_auth->ft_pmk_cache = NULL;
	wpa_ft_deinit(wpa_auth);
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_P2P
	bitfield_free(wpa_auth->ip_pool);
#endif /* CONFIG_P2P */


	os_free(wpa_auth->wpa_ie);

	group = wpa_auth->group;
	while (group) {
		prev = group;
		group = group->next;
		os_free(prev);
	}

	os_free(wpa_auth);
}


/**
 * wpa_reconfig - Update WPA authenticator configuration
 * @wpa_auth: Pointer to WPA authenticator data from wpa_init()
 * @conf: Configuration for WPA authenticator
 */
int wpa_reconfig(struct wpa_authenticator *wpa_auth,
		 struct wpa_auth_config *conf)
{
	struct wpa_group *group;
	if (wpa_auth == NULL)
		return 0;

	os_memcpy(&wpa_auth->conf, conf, sizeof(*conf));
	if (wpa_auth_gen_wpa_ie(wpa_auth)) {
		wpa_printf(MSG_ERROR, "Could not generate WPA IE.");
		return -1;
	}

	/*
	 * Reinitialize GTK to make sure it is suitable for the new
	 * configuration.
	 */
	group = wpa_auth->group;
	group->GTK_len = wpa_cipher_key_len(wpa_auth->conf.wpa_group);
	group->GInit = TRUE;
	wpa_group_sm_step(wpa_auth, group);
	group->GInit = FALSE;
	wpa_group_sm_step(wpa_auth, group);

	return 0;
}


struct wpa_state_machine *
wpa_auth_sta_init(struct wpa_authenticator *wpa_auth, const u8 *addr,
		  const u8 *p2p_dev_addr)
{
	struct wpa_state_machine *sm;

	if (wpa_auth->group->wpa_group_state == WPA_GROUP_FATAL_FAILURE)
		return NULL;

	sm = os_zalloc(sizeof(struct wpa_state_machine));
	if (sm == NULL)
		return NULL;
	os_memcpy(sm->addr, addr, ETH_ALEN);
	if (p2p_dev_addr)
		os_memcpy(sm->p2p_dev_addr, p2p_dev_addr, ETH_ALEN);

	sm->wpa_auth = wpa_auth;
	sm->group = wpa_auth->group;
	wpa_group_get(sm->wpa_auth, sm->group);

	return sm;
}


int wpa_auth_sta_associated(struct wpa_authenticator *wpa_auth,
			    struct wpa_state_machine *sm)
{
	if (wpa_auth == NULL || !wpa_auth->conf.wpa || sm == NULL)
		return -1;

#ifdef CONFIG_IEEE80211R_AP
	if (sm->ft_completed) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
				"FT authentication already completed - do not "
				"start 4-way handshake");
		/* Go to PTKINITDONE state to allow GTK rekeying */
		sm->wpa_ptk_state = WPA_PTK_PTKINITDONE;
		sm->Pair = TRUE;
		return 0;
	}
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_FILS
	if (sm->fils_completed) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
				"FILS authentication already completed - do not start 4-way handshake");
		/* Go to PTKINITDONE state to allow GTK rekeying */
		sm->wpa_ptk_state = WPA_PTK_PTKINITDONE;
		sm->Pair = TRUE;
		return 0;
	}
#endif /* CONFIG_FILS */

	if (sm->started) {
		os_memset(&sm->key_replay, 0, sizeof(sm->key_replay));
		sm->ReAuthenticationRequest = TRUE;
		return wpa_sm_step(sm);
	}

	wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
			"start authentication");
	sm->started = 1;

	sm->Init = TRUE;
	if (wpa_sm_step(sm) == 1)
		return 1; /* should not really happen */
	sm->Init = FALSE;
	sm->AuthenticationRequest = TRUE;
	return wpa_sm_step(sm);
}


void wpa_auth_sta_no_wpa(struct wpa_state_machine *sm)
{
	/* WPA/RSN was not used - clear WPA state. This is needed if the STA
	 * reassociates back to the same AP while the previous entry for the
	 * STA has not yet been removed. */
	if (sm == NULL)
		return;

	sm->wpa_key_mgmt = 0;
}


static void wpa_free_sta_sm(struct wpa_state_machine *sm)
{
#ifdef CONFIG_P2P
	if (WPA_GET_BE32(sm->ip_addr)) {
		u32 start;
		wpa_printf(MSG_DEBUG, "P2P: Free assigned IP "
			   "address %u.%u.%u.%u from " MACSTR,
			   sm->ip_addr[0], sm->ip_addr[1],
			   sm->ip_addr[2], sm->ip_addr[3],
			   MAC2STR(sm->addr));
		start = WPA_GET_BE32(sm->wpa_auth->conf.ip_addr_start);
		bitfield_clear(sm->wpa_auth->ip_pool,
			       WPA_GET_BE32(sm->ip_addr) - start);
	}
#endif /* CONFIG_P2P */
	if (sm->GUpdateStationKeys) {
		sm->group->GKeyDoneStations--;
		sm->GUpdateStationKeys = FALSE;
	}
#ifdef CONFIG_IEEE80211R_AP
	os_free(sm->assoc_resp_ftie);
	wpabuf_free(sm->ft_pending_req_ies);
#endif /* CONFIG_IEEE80211R_AP */
	os_free(sm->last_rx_eapol_key);
	os_free(sm->wpa_ie);
	wpa_group_put(sm->wpa_auth, sm->group);
	os_free(sm);
}


void wpa_auth_sta_deinit(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return;

	if (sm->wpa_auth->conf.wpa_strict_rekey && sm->has_GTK) {
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"strict rekeying - force GTK rekey since STA "
				"is leaving");
		if (eloop_deplete_timeout(0, 500000, wpa_rekey_gtk,
					  sm->wpa_auth, NULL) == -1)
			eloop_register_timeout(0, 500000, wpa_rekey_gtk, sm->wpa_auth,
					       NULL);
	}

	eloop_cancel_timeout(wpa_send_eapol_timeout, sm->wpa_auth, sm);
	sm->pending_1_of_4_timeout = 0;
	eloop_cancel_timeout(wpa_sm_call_step, sm, NULL);
	eloop_cancel_timeout(wpa_rekey_ptk, sm->wpa_auth, sm);
#ifdef CONFIG_IEEE80211R_AP
	wpa_ft_sta_deinit(sm);
#endif /* CONFIG_IEEE80211R_AP */
	if (sm->in_step_loop) {
		/* Must not free state machine while wpa_sm_step() is running.
		 * Freeing will be completed in the end of wpa_sm_step(). */
		wpa_printf(MSG_DEBUG, "WPA: Registering pending STA state "
			   "machine deinit for " MACSTR, MAC2STR(sm->addr));
		sm->pending_deinit = 1;
	} else
		wpa_free_sta_sm(sm);
}


static void wpa_request_new_ptk(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return;

	sm->PTKRequest = TRUE;
	sm->PTK_valid = 0;
}


static int wpa_replay_counter_valid(struct wpa_key_replay_counter *ctr,
				    const u8 *replay_counter)
{
	int i;
	for (i = 0; i < RSNA_MAX_EAPOL_RETRIES; i++) {
		if (!ctr[i].valid)
			break;
		if (os_memcmp(replay_counter, ctr[i].counter,
			      WPA_REPLAY_COUNTER_LEN) == 0)
			return 1;
	}
	return 0;
}


static void wpa_replay_counter_mark_invalid(struct wpa_key_replay_counter *ctr,
					    const u8 *replay_counter)
{
	int i;
	for (i = 0; i < RSNA_MAX_EAPOL_RETRIES; i++) {
		if (ctr[i].valid &&
		    (replay_counter == NULL ||
		     os_memcmp(replay_counter, ctr[i].counter,
			       WPA_REPLAY_COUNTER_LEN) == 0))
			ctr[i].valid = FALSE;
	}
}


#ifdef CONFIG_IEEE80211R_AP
static int ft_check_msg_2_of_4(struct wpa_authenticator *wpa_auth,
			       struct wpa_state_machine *sm,
			       struct wpa_eapol_ie_parse *kde)
{
	struct wpa_ie_data ie;
	struct rsn_mdie *mdie;

	if (wpa_parse_wpa_ie_rsn(kde->rsn_ie, kde->rsn_ie_len, &ie) < 0 ||
	    ie.num_pmkid != 1 || ie.pmkid == NULL) {
		wpa_printf(MSG_DEBUG, "FT: No PMKR1Name in "
			   "FT 4-way handshake message 2/4");
		return -1;
	}

	os_memcpy(sm->sup_pmk_r1_name, ie.pmkid, PMKID_LEN);
	wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name from Supplicant",
		    sm->sup_pmk_r1_name, PMKID_LEN);

	if (!kde->mdie || !kde->ftie) {
		wpa_printf(MSG_DEBUG, "FT: No %s in FT 4-way handshake "
			   "message 2/4", kde->mdie ? "FTIE" : "MDIE");
		return -1;
	}

	mdie = (struct rsn_mdie *) (kde->mdie + 2);
	if (kde->mdie[1] < sizeof(struct rsn_mdie) ||
	    os_memcmp(wpa_auth->conf.mobility_domain, mdie->mobility_domain,
		      MOBILITY_DOMAIN_ID_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FT: MDIE mismatch");
		return -1;
	}

	if (sm->assoc_resp_ftie &&
	    (kde->ftie[1] != sm->assoc_resp_ftie[1] ||
	     os_memcmp(kde->ftie, sm->assoc_resp_ftie,
		       2 + sm->assoc_resp_ftie[1]) != 0)) {
		wpa_printf(MSG_DEBUG, "FT: FTIE mismatch");
		wpa_hexdump(MSG_DEBUG, "FT: FTIE in EAPOL-Key msg 2/4",
			    kde->ftie, kde->ftie_len);
		wpa_hexdump(MSG_DEBUG, "FT: FTIE in (Re)AssocResp",
			    sm->assoc_resp_ftie, 2 + sm->assoc_resp_ftie[1]);
		return -1;
	}

	return 0;
}
#endif /* CONFIG_IEEE80211R_AP */


static int wpa_receive_error_report(struct wpa_authenticator *wpa_auth,
				    struct wpa_state_machine *sm, int group)
{
	/* Supplicant reported a Michael MIC error */
	wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
			 "received EAPOL-Key Error Request "
			 "(STA detected Michael MIC failure (group=%d))",
			 group);

	if (group && wpa_auth->conf.wpa_group != WPA_CIPHER_TKIP) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"ignore Michael MIC failure report since "
				"group cipher is not TKIP");
	} else if (!group && sm->pairwise != WPA_CIPHER_TKIP) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"ignore Michael MIC failure report since "
				"pairwise cipher is not TKIP");
	} else {
		if (wpa_auth_mic_failure_report(wpa_auth, sm->addr) > 0)
			return 1; /* STA entry was removed */
		sm->dot11RSNAStatsTKIPRemoteMICFailures++;
		wpa_auth->dot11RSNAStatsTKIPRemoteMICFailures++;
	}

	/*
	 * Error report is not a request for a new key handshake, but since
	 * Authenticator may do it, let's change the keys now anyway.
	 */
	wpa_request_new_ptk(sm);
	return 0;
}


static int wpa_try_alt_snonce(struct wpa_state_machine *sm, u8 *data,
			      size_t data_len)
{
	struct wpa_ptk PTK;
	int ok = 0;
	const u8 *pmk = NULL;
	size_t pmk_len;

	os_memset(&PTK, 0, sizeof(PTK));
	for (;;) {
		if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) &&
		    !wpa_key_mgmt_sae(sm->wpa_key_mgmt)) {
			pmk = wpa_auth_get_psk(sm->wpa_auth, sm->addr,
					       sm->p2p_dev_addr, pmk, &pmk_len);
			if (pmk == NULL)
				break;
#ifdef CONFIG_IEEE80211R_AP
			if (wpa_key_mgmt_ft_psk(sm->wpa_key_mgmt)) {
				os_memcpy(sm->xxkey, pmk, pmk_len);
				sm->xxkey_len = pmk_len;
			}
#endif /* CONFIG_IEEE80211R_AP */
		} else {
			pmk = sm->PMK;
			pmk_len = sm->pmk_len;
		}

		if (wpa_derive_ptk(sm, sm->alt_SNonce, pmk, pmk_len, &PTK) < 0)
			break;

		if (wpa_verify_key_mic(sm->wpa_key_mgmt, pmk_len, &PTK,
				       data, data_len) == 0) {
			ok = 1;
			break;
		}

		if (!wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) ||
		    wpa_key_mgmt_sae(sm->wpa_key_mgmt))
			break;
	}

	if (!ok) {
		wpa_printf(MSG_DEBUG,
			   "WPA: Earlier SNonce did not result in matching MIC");
		return -1;
	}

	wpa_printf(MSG_DEBUG,
		   "WPA: Earlier SNonce resulted in matching MIC");
	sm->alt_snonce_valid = 0;
	os_memcpy(sm->SNonce, sm->alt_SNonce, WPA_NONCE_LEN);
	os_memcpy(&sm->PTK, &PTK, sizeof(PTK));
	sm->PTK_valid = TRUE;

	return 0;
}


void wpa_receive(struct wpa_authenticator *wpa_auth,
		 struct wpa_state_machine *sm,
		 u8 *data, size_t data_len)
{
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u16 key_info, key_data_length;
	enum { PAIRWISE_2, PAIRWISE_4, GROUP_2, REQUEST } msg;
	char *msgtxt;
	struct wpa_eapol_ie_parse kde;
	const u8 *key_data;
	size_t keyhdrlen, mic_len;
	u8 *mic;

	if (wpa_auth == NULL || !wpa_auth->conf.wpa || sm == NULL)
		return;
	wpa_hexdump(MSG_MSGDUMP, "WPA: RX EAPOL data", data, data_len);

	mic_len = wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len);
	keyhdrlen = sizeof(*key) + mic_len + 2;

	if (data_len < sizeof(*hdr) + keyhdrlen) {
		wpa_printf(MSG_DEBUG, "WPA: Ignore too short EAPOL-Key frame");
		return;
	}

	hdr = (struct ieee802_1x_hdr *) data;
	key = (struct wpa_eapol_key *) (hdr + 1);
	mic = (u8 *) (key + 1);
	key_info = WPA_GET_BE16(key->key_info);
	key_data = mic + mic_len + 2;
	key_data_length = WPA_GET_BE16(mic + mic_len);
	wpa_printf(MSG_DEBUG, "WPA: Received EAPOL-Key from " MACSTR
		   " key_info=0x%x type=%u mic_len=%u key_data_length=%u",
		   MAC2STR(sm->addr), key_info, key->type,
		   (unsigned int) mic_len, key_data_length);
	wpa_hexdump(MSG_MSGDUMP,
		    "WPA: EAPOL-Key header (ending before Key MIC)",
		    key, sizeof(*key));
	wpa_hexdump(MSG_MSGDUMP, "WPA: EAPOL-Key Key MIC",
		    mic, mic_len);
	if (key_data_length > data_len - sizeof(*hdr) - keyhdrlen) {
		wpa_printf(MSG_INFO, "WPA: Invalid EAPOL-Key frame - "
			   "key_data overflow (%d > %lu)",
			   key_data_length,
			   (unsigned long) (data_len - sizeof(*hdr) -
					    keyhdrlen));
		return;
	}

	if (sm->wpa == WPA_VERSION_WPA2) {
		if (key->type == EAPOL_KEY_TYPE_WPA) {
			/*
			 * Some deployed station implementations seem to send
			 * msg 4/4 with incorrect type value in WPA2 mode.
			 */
			wpa_printf(MSG_DEBUG, "Workaround: Allow EAPOL-Key "
				   "with unexpected WPA type in RSN mode");
		} else if (key->type != EAPOL_KEY_TYPE_RSN) {
			wpa_printf(MSG_DEBUG, "Ignore EAPOL-Key with "
				   "unexpected type %d in RSN mode",
				   key->type);
			return;
		}
	} else {
		if (key->type != EAPOL_KEY_TYPE_WPA) {
			wpa_printf(MSG_DEBUG, "Ignore EAPOL-Key with "
				   "unexpected type %d in WPA mode",
				   key->type);
			return;
		}
	}

	wpa_hexdump(MSG_DEBUG, "WPA: Received Key Nonce", key->key_nonce,
		    WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: Received Replay Counter",
		    key->replay_counter, WPA_REPLAY_COUNTER_LEN);

	/* FIX: verify that the EAPOL-Key frame was encrypted if pairwise keys
	 * are set */

	if (key_info & WPA_KEY_INFO_SMK_MESSAGE) {
		wpa_printf(MSG_DEBUG, "WPA: Ignore SMK message");
		return;
	}

	if (key_info & WPA_KEY_INFO_REQUEST) {
		msg = REQUEST;
		msgtxt = "Request";
	} else if (!(key_info & WPA_KEY_INFO_KEY_TYPE)) {
		msg = GROUP_2;
		msgtxt = "2/2 Group";
	} else if (key_data_length == 0 ||
		   (mic_len == 0 && (key_info & WPA_KEY_INFO_ENCR_KEY_DATA) &&
		    key_data_length == AES_BLOCK_SIZE)) {
		msg = PAIRWISE_4;
		msgtxt = "4/4 Pairwise";
	} else {
		msg = PAIRWISE_2;
		msgtxt = "2/4 Pairwise";
	}

	if (msg == REQUEST || msg == PAIRWISE_2 || msg == PAIRWISE_4 ||
	    msg == GROUP_2) {
		u16 ver = key_info & WPA_KEY_INFO_TYPE_MASK;
		if (sm->pairwise == WPA_CIPHER_CCMP ||
		    sm->pairwise == WPA_CIPHER_GCMP) {
			if (wpa_use_cmac(sm->wpa_key_mgmt) &&
			    !wpa_use_akm_defined(sm->wpa_key_mgmt) &&
			    ver != WPA_KEY_INFO_TYPE_AES_128_CMAC) {
				wpa_auth_logger(wpa_auth, sm->addr,
						LOGGER_WARNING,
						"advertised support for "
						"AES-128-CMAC, but did not "
						"use it");
				return;
			}

			if (!wpa_use_cmac(sm->wpa_key_mgmt) &&
			    !wpa_use_akm_defined(sm->wpa_key_mgmt) &&
			    ver != WPA_KEY_INFO_TYPE_HMAC_SHA1_AES) {
				wpa_auth_logger(wpa_auth, sm->addr,
						LOGGER_WARNING,
						"did not use HMAC-SHA1-AES "
						"with CCMP/GCMP");
				return;
			}
		}

		if (wpa_use_akm_defined(sm->wpa_key_mgmt) &&
		    ver != WPA_KEY_INFO_TYPE_AKM_DEFINED) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_WARNING,
					"did not use EAPOL-Key descriptor version 0 as required for AKM-defined cases");
			return;
		}
	}

	if (key_info & WPA_KEY_INFO_REQUEST) {
		if (sm->req_replay_counter_used &&
		    os_memcmp(key->replay_counter, sm->req_replay_counter,
			      WPA_REPLAY_COUNTER_LEN) <= 0) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_WARNING,
					"received EAPOL-Key request with "
					"replayed counter");
			return;
		}
	}

	if (!(key_info & WPA_KEY_INFO_REQUEST) &&
	    !wpa_replay_counter_valid(sm->key_replay, key->replay_counter)) {
		int i;

		if (msg == PAIRWISE_2 &&
		    wpa_replay_counter_valid(sm->prev_key_replay,
					     key->replay_counter) &&
		    sm->wpa_ptk_state == WPA_PTK_PTKINITNEGOTIATING &&
		    os_memcmp(sm->SNonce, key->key_nonce, WPA_NONCE_LEN) != 0)
		{
			/*
			 * Some supplicant implementations (e.g., Windows XP
			 * WZC) update SNonce for each EAPOL-Key 2/4. This
			 * breaks the workaround on accepting any of the
			 * pending requests, so allow the SNonce to be updated
			 * even if we have already sent out EAPOL-Key 3/4.
			 */
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
					 "Process SNonce update from STA "
					 "based on retransmitted EAPOL-Key "
					 "1/4");
			sm->update_snonce = 1;
			os_memcpy(sm->alt_SNonce, sm->SNonce, WPA_NONCE_LEN);
			sm->alt_snonce_valid = TRUE;
			os_memcpy(sm->alt_replay_counter,
				  sm->key_replay[0].counter,
				  WPA_REPLAY_COUNTER_LEN);
			goto continue_processing;
		}

		if (msg == PAIRWISE_4 && sm->alt_snonce_valid &&
		    sm->wpa_ptk_state == WPA_PTK_PTKINITNEGOTIATING &&
		    os_memcmp(key->replay_counter, sm->alt_replay_counter,
			      WPA_REPLAY_COUNTER_LEN) == 0) {
			/*
			 * Supplicant may still be using the old SNonce since
			 * there was two EAPOL-Key 2/4 messages and they had
			 * different SNonce values.
			 */
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
					 "Try to process received EAPOL-Key 4/4 based on old Replay Counter and SNonce from an earlier EAPOL-Key 1/4");
			goto continue_processing;
		}

		if (msg == PAIRWISE_2 &&
		    wpa_replay_counter_valid(sm->prev_key_replay,
					     key->replay_counter) &&
		    sm->wpa_ptk_state == WPA_PTK_PTKINITNEGOTIATING) {
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
					 "ignore retransmitted EAPOL-Key %s - "
					 "SNonce did not change", msgtxt);
		} else {
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
					 "received EAPOL-Key %s with "
					 "unexpected replay counter", msgtxt);
		}
		for (i = 0; i < RSNA_MAX_EAPOL_RETRIES; i++) {
			if (!sm->key_replay[i].valid)
				break;
			wpa_hexdump(MSG_DEBUG, "pending replay counter",
				    sm->key_replay[i].counter,
				    WPA_REPLAY_COUNTER_LEN);
		}
		wpa_hexdump(MSG_DEBUG, "received replay counter",
			    key->replay_counter, WPA_REPLAY_COUNTER_LEN);
		return;
	}

continue_processing:
#ifdef CONFIG_FILS
	if (sm->wpa == WPA_VERSION_WPA2 && mic_len == 0 &&
	    !(key_info & WPA_KEY_INFO_ENCR_KEY_DATA)) {
		wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
				 "WPA: Encr Key Data bit not set even though AEAD cipher is supposed to be used - drop frame");
		return;
	}
#endif /* CONFIG_FILS */

	switch (msg) {
	case PAIRWISE_2:
		if (sm->wpa_ptk_state != WPA_PTK_PTKSTART &&
		    sm->wpa_ptk_state != WPA_PTK_PTKCALCNEGOTIATING &&
		    (!sm->update_snonce ||
		     sm->wpa_ptk_state != WPA_PTK_PTKINITNEGOTIATING)) {
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
					 "received EAPOL-Key msg 2/4 in "
					 "invalid state (%d) - dropped",
					 sm->wpa_ptk_state);
			return;
		}
		random_add_randomness(key->key_nonce, WPA_NONCE_LEN);
		if (sm->group->reject_4way_hs_for_entropy) {
			/*
			 * The system did not have enough entropy to generate
			 * strong random numbers. Reject the first 4-way
			 * handshake(s) and collect some entropy based on the
			 * information from it. Once enough entropy is
			 * available, the next atempt will trigger GMK/Key
			 * Counter update and the station will be allowed to
			 * continue.
			 */
			wpa_printf(MSG_DEBUG, "WPA: Reject 4-way handshake to "
				   "collect more entropy for random number "
				   "generation");
			random_mark_pool_ready();
			wpa_sta_disconnect(wpa_auth, sm->addr,
					   WLAN_REASON_PREV_AUTH_NOT_VALID);
			return;
		}
		break;
	case PAIRWISE_4:
		if (sm->wpa_ptk_state != WPA_PTK_PTKINITNEGOTIATING ||
		    !sm->PTK_valid) {
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
					 "received EAPOL-Key msg 4/4 in "
					 "invalid state (%d) - dropped",
					 sm->wpa_ptk_state);
			return;
		}
		break;
	case GROUP_2:
		if (sm->wpa_ptk_group_state != WPA_PTK_GROUP_REKEYNEGOTIATING
		    || !sm->PTK_valid) {
			wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
					 "received EAPOL-Key msg 2/2 in "
					 "invalid state (%d) - dropped",
					 sm->wpa_ptk_group_state);
			return;
		}
		break;
	case REQUEST:
		break;
	}

	wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_DEBUG,
			 "received EAPOL-Key frame (%s)", msgtxt);

	if (key_info & WPA_KEY_INFO_ACK) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"received invalid EAPOL-Key: Key Ack set");
		return;
	}

	if (!wpa_key_mgmt_fils(sm->wpa_key_mgmt) &&
	    !(key_info & WPA_KEY_INFO_MIC)) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"received invalid EAPOL-Key: Key MIC not set");
		return;
	}

#ifdef CONFIG_FILS
	if (wpa_key_mgmt_fils(sm->wpa_key_mgmt) &&
	    (key_info & WPA_KEY_INFO_MIC)) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"received invalid EAPOL-Key: Key MIC set");
		return;
	}
#endif /* CONFIG_FILS */

	sm->MICVerified = FALSE;
	if (sm->PTK_valid && !sm->update_snonce) {
		if (mic_len &&
		    wpa_verify_key_mic(sm->wpa_key_mgmt, sm->pmk_len, &sm->PTK,
				       data, data_len) &&
		    (msg != PAIRWISE_4 || !sm->alt_snonce_valid ||
		     wpa_try_alt_snonce(sm, data, data_len))) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"received EAPOL-Key with invalid MIC");
			return;
		}
#ifdef CONFIG_FILS
		if (!mic_len &&
		    wpa_aead_decrypt(sm, &sm->PTK, data, data_len,
				     &key_data_length) < 0) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"received EAPOL-Key with invalid MIC");
			return;
		}
#endif /* CONFIG_FILS */
		sm->MICVerified = TRUE;
		eloop_cancel_timeout(wpa_send_eapol_timeout, wpa_auth, sm);
		sm->pending_1_of_4_timeout = 0;
	}

	if (key_info & WPA_KEY_INFO_REQUEST) {
		if (sm->MICVerified) {
			sm->req_replay_counter_used = 1;
			os_memcpy(sm->req_replay_counter, key->replay_counter,
				  WPA_REPLAY_COUNTER_LEN);
		} else {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"received EAPOL-Key request with "
					"invalid MIC");
			return;
		}

		/*
		 * TODO: should decrypt key data field if encryption was used;
		 * even though MAC address KDE is not normally encrypted,
		 * supplicant is allowed to encrypt it.
		 */
		if (key_info & WPA_KEY_INFO_ERROR) {
			if (wpa_receive_error_report(
				    wpa_auth, sm,
				    !(key_info & WPA_KEY_INFO_KEY_TYPE)) > 0)
				return; /* STA entry was removed */
		} else if (key_info & WPA_KEY_INFO_KEY_TYPE) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"received EAPOL-Key Request for new "
					"4-Way Handshake");
			wpa_request_new_ptk(sm);
		} else if (key_data_length > 0 &&
			   wpa_parse_kde_ies(key_data, key_data_length,
					     &kde) == 0 &&
			   kde.mac_addr) {
		} else {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"received EAPOL-Key Request for GTK "
					"rekeying");
			eloop_cancel_timeout(wpa_rekey_gtk, wpa_auth, NULL);
			wpa_rekey_gtk(wpa_auth, NULL);
		}
	} else {
		/* Do not allow the same key replay counter to be reused. */
		wpa_replay_counter_mark_invalid(sm->key_replay,
						key->replay_counter);

		if (msg == PAIRWISE_2) {
			/*
			 * Maintain a copy of the pending EAPOL-Key frames in
			 * case the EAPOL-Key frame was retransmitted. This is
			 * needed to allow EAPOL-Key msg 2/4 reply to another
			 * pending msg 1/4 to update the SNonce to work around
			 * unexpected supplicant behavior.
			 */
			os_memcpy(sm->prev_key_replay, sm->key_replay,
				  sizeof(sm->key_replay));
		} else {
			os_memset(sm->prev_key_replay, 0,
				  sizeof(sm->prev_key_replay));
		}

		/*
		 * Make sure old valid counters are not accepted anymore and
		 * do not get copied again.
		 */
		wpa_replay_counter_mark_invalid(sm->key_replay, NULL);
	}

	os_free(sm->last_rx_eapol_key);
	sm->last_rx_eapol_key = os_memdup(data, data_len);
	if (sm->last_rx_eapol_key == NULL)
		return;
	sm->last_rx_eapol_key_len = data_len;

	sm->rx_eapol_key_secure = !!(key_info & WPA_KEY_INFO_SECURE);
	sm->EAPOLKeyReceived = TRUE;
	sm->EAPOLKeyPairwise = !!(key_info & WPA_KEY_INFO_KEY_TYPE);
	sm->EAPOLKeyRequest = !!(key_info & WPA_KEY_INFO_REQUEST);
	os_memcpy(sm->SNonce, key->key_nonce, WPA_NONCE_LEN);
	wpa_sm_step(sm);
}


static int wpa_gmk_to_gtk(const u8 *gmk, const char *label, const u8 *addr,
			  const u8 *gnonce, u8 *gtk, size_t gtk_len)
{
	u8 data[ETH_ALEN + WPA_NONCE_LEN + 8 + WPA_GTK_MAX_LEN];
	u8 *pos;
	int ret = 0;

	/* GTK = PRF-X(GMK, "Group key expansion",
	 *	AA || GNonce || Time || random data)
	 * The example described in the IEEE 802.11 standard uses only AA and
	 * GNonce as inputs here. Add some more entropy since this derivation
	 * is done only at the Authenticator and as such, does not need to be
	 * exactly same.
	 */
	os_memset(data, 0, sizeof(data));
	os_memcpy(data, addr, ETH_ALEN);
	os_memcpy(data + ETH_ALEN, gnonce, WPA_NONCE_LEN);
	pos = data + ETH_ALEN + WPA_NONCE_LEN;
	wpa_get_ntp_timestamp(pos);
	pos += 8;
	if (random_get_bytes(pos, gtk_len) < 0)
		ret = -1;

#ifdef CONFIG_SHA384
	if (sha384_prf(gmk, WPA_GMK_LEN, label, data, sizeof(data),
		       gtk, gtk_len) < 0)
		ret = -1;
#else /* CONFIG_SHA384 */
#ifdef CONFIG_SHA256
	if (sha256_prf(gmk, WPA_GMK_LEN, label, data, sizeof(data),
		       gtk, gtk_len) < 0)
		ret = -1;
#else /* CONFIG_SHA256 */
	if (sha1_prf(gmk, WPA_GMK_LEN, label, data, sizeof(data),
		     gtk, gtk_len) < 0)
		ret = -1;
#endif /* CONFIG_SHA256 */
#endif /* CONFIG_SHA384 */

	return ret;
}


static void wpa_send_eapol_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_authenticator *wpa_auth = eloop_ctx;
	struct wpa_state_machine *sm = timeout_ctx;

	sm->pending_1_of_4_timeout = 0;
	wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG, "EAPOL-Key timeout");
	sm->TimeoutEvt = TRUE;
	wpa_sm_step(sm);
}


void __wpa_send_eapol(struct wpa_authenticator *wpa_auth,
		      struct wpa_state_machine *sm, int key_info,
		      const u8 *key_rsc, const u8 *nonce,
		      const u8 *kde, size_t kde_len,
		      int keyidx, int encr, int force_version)
{
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	size_t len, mic_len, keyhdrlen;
	int alg;
	int key_data_len, pad_len = 0;
	u8 *buf, *pos;
	int version, pairwise;
	int i;
	u8 *key_mic, *key_data;

	mic_len = wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len);
	keyhdrlen = sizeof(*key) + mic_len + 2;

	len = sizeof(struct ieee802_1x_hdr) + keyhdrlen;

	if (force_version)
		version = force_version;
	else if (wpa_use_akm_defined(sm->wpa_key_mgmt))
		version = WPA_KEY_INFO_TYPE_AKM_DEFINED;
	else if (wpa_use_cmac(sm->wpa_key_mgmt))
		version = WPA_KEY_INFO_TYPE_AES_128_CMAC;
	else if (sm->pairwise != WPA_CIPHER_TKIP)
		version = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		version = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	pairwise = !!(key_info & WPA_KEY_INFO_KEY_TYPE);

	wpa_printf(MSG_DEBUG, "WPA: Send EAPOL(version=%d secure=%d mic=%d "
		   "ack=%d install=%d pairwise=%d kde_len=%lu keyidx=%d "
		   "encr=%d)",
		   version,
		   (key_info & WPA_KEY_INFO_SECURE) ? 1 : 0,
		   (key_info & WPA_KEY_INFO_MIC) ? 1 : 0,
		   (key_info & WPA_KEY_INFO_ACK) ? 1 : 0,
		   (key_info & WPA_KEY_INFO_INSTALL) ? 1 : 0,
		   pairwise, (unsigned long) kde_len, keyidx, encr);

	key_data_len = kde_len;

	if ((version == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES ||
	     wpa_use_aes_key_wrap(sm->wpa_key_mgmt) ||
	     version == WPA_KEY_INFO_TYPE_AES_128_CMAC) && encr) {
		pad_len = key_data_len % 8;
		if (pad_len)
			pad_len = 8 - pad_len;
		key_data_len += pad_len + 8;
	}

	len += key_data_len;
	if (!mic_len && encr)
		len += AES_BLOCK_SIZE;

	hdr = os_zalloc(len);
	if (hdr == NULL)
		return;
	hdr->version = wpa_auth->conf.eapol_version;
	hdr->type = IEEE802_1X_TYPE_EAPOL_KEY;
	hdr->length = host_to_be16(len  - sizeof(*hdr));
	key = (struct wpa_eapol_key *) (hdr + 1);
	key_mic = (u8 *) (key + 1);
	key_data = ((u8 *) (hdr + 1)) + keyhdrlen;

	key->type = sm->wpa == WPA_VERSION_WPA2 ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	key_info |= version;
	if (encr && sm->wpa == WPA_VERSION_WPA2)
		key_info |= WPA_KEY_INFO_ENCR_KEY_DATA;
	if (sm->wpa != WPA_VERSION_WPA2)
		key_info |= keyidx << WPA_KEY_INFO_KEY_INDEX_SHIFT;
	WPA_PUT_BE16(key->key_info, key_info);

	alg = pairwise ? sm->pairwise : wpa_auth->conf.wpa_group;
	if (sm->wpa == WPA_VERSION_WPA2 && !pairwise)
		WPA_PUT_BE16(key->key_length, 0);
	else
		WPA_PUT_BE16(key->key_length, wpa_cipher_key_len(alg));

	for (i = RSNA_MAX_EAPOL_RETRIES - 1; i > 0; i--) {
		sm->key_replay[i].valid = sm->key_replay[i - 1].valid;
		os_memcpy(sm->key_replay[i].counter,
			  sm->key_replay[i - 1].counter,
			  WPA_REPLAY_COUNTER_LEN);
	}
	inc_byte_array(sm->key_replay[0].counter, WPA_REPLAY_COUNTER_LEN);
	os_memcpy(key->replay_counter, sm->key_replay[0].counter,
		  WPA_REPLAY_COUNTER_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: Replay Counter",
		    key->replay_counter, WPA_REPLAY_COUNTER_LEN);
	sm->key_replay[0].valid = TRUE;

	if (nonce)
		os_memcpy(key->key_nonce, nonce, WPA_NONCE_LEN);

	if (key_rsc)
		os_memcpy(key->key_rsc, key_rsc, WPA_KEY_RSC_LEN);

	if (kde && !encr) {
		os_memcpy(key_data, kde, kde_len);
		WPA_PUT_BE16(key_mic + mic_len, kde_len);
#ifdef CONFIG_FILS
	} else if (!mic_len && kde) {
		const u8 *aad[1];
		size_t aad_len[1];

		WPA_PUT_BE16(key_mic, AES_BLOCK_SIZE + kde_len);
		wpa_hexdump_key(MSG_DEBUG, "Plaintext EAPOL-Key Key Data",
				kde, kde_len);

		wpa_hexdump_key(MSG_DEBUG, "WPA: KEK",
				sm->PTK.kek, sm->PTK.kek_len);
		/* AES-SIV AAD from EAPOL protocol version field (inclusive) to
		 * to Key Data (exclusive). */
		aad[0] = (u8 *) hdr;
		aad_len[0] = key_mic + 2 - (u8 *) hdr;
		if (aes_siv_encrypt(sm->PTK.kek, sm->PTK.kek_len, kde, kde_len,
				    1, aad, aad_len, key_mic + 2) < 0) {
			wpa_printf(MSG_DEBUG, "WPA: AES-SIV encryption failed");
			return;
		}

		wpa_hexdump(MSG_DEBUG, "WPA: Encrypted Key Data from SIV",
			    key_mic + 2, AES_BLOCK_SIZE + kde_len);
#endif /* CONFIG_FILS */
	} else if (encr && kde) {
		buf = os_zalloc(key_data_len);
		if (buf == NULL) {
			os_free(hdr);
			return;
		}
		pos = buf;
		os_memcpy(pos, kde, kde_len);
		pos += kde_len;

		if (pad_len)
			*pos++ = 0xdd;

		wpa_hexdump_key(MSG_DEBUG, "Plaintext EAPOL-Key Key Data",
				buf, key_data_len);
		if (version == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES ||
		    wpa_use_aes_key_wrap(sm->wpa_key_mgmt) ||
		    version == WPA_KEY_INFO_TYPE_AES_128_CMAC) {
			wpa_printf(MSG_DEBUG,
				   "WPA: Encrypt Key Data using AES-WRAP (KEK length %u)",
				   (unsigned int) sm->PTK.kek_len);
			if (aes_wrap(sm->PTK.kek, sm->PTK.kek_len,
				     (key_data_len - 8) / 8, buf, key_data)) {
				os_free(hdr);
				os_free(buf);
				return;
			}
			WPA_PUT_BE16(key_mic + mic_len, key_data_len);
#ifndef CONFIG_NO_RC4
		} else if (sm->PTK.kek_len == 16) {
			u8 ek[32];

			wpa_printf(MSG_DEBUG,
				   "WPA: Encrypt Key Data using RC4");
			os_memcpy(key->key_iv,
				  sm->group->Counter + WPA_NONCE_LEN - 16, 16);
			inc_byte_array(sm->group->Counter, WPA_NONCE_LEN);
			os_memcpy(ek, key->key_iv, 16);
			os_memcpy(ek + 16, sm->PTK.kek, sm->PTK.kek_len);
			os_memcpy(key_data, buf, key_data_len);
			rc4_skip(ek, 32, 256, key_data, key_data_len);
			WPA_PUT_BE16(key_mic + mic_len, key_data_len);
#endif /* CONFIG_NO_RC4 */
		} else {
			os_free(hdr);
			os_free(buf);
			return;
		}
		os_free(buf);
	}

	if (key_info & WPA_KEY_INFO_MIC) {
		if (!sm->PTK_valid || !mic_len) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
					"PTK not valid when sending EAPOL-Key "
					"frame");
			os_free(hdr);
			return;
		}

		if (wpa_eapol_key_mic(sm->PTK.kck, sm->PTK.kck_len,
				      sm->wpa_key_mgmt, version,
				      (u8 *) hdr, len, key_mic) < 0) {
			os_free(hdr);
			return;
		}
#ifdef CONFIG_TESTING_OPTIONS
		if (!pairwise &&
		    wpa_auth->conf.corrupt_gtk_rekey_mic_probability > 0.0 &&
		    drand48() <
		    wpa_auth->conf.corrupt_gtk_rekey_mic_probability) {
			wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
					"Corrupting group EAPOL-Key Key MIC");
			key_mic[0]++;
		}
#endif /* CONFIG_TESTING_OPTIONS */
	}

	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_inc_EapolFramesTx,
			   1);
	wpa_auth_send_eapol(wpa_auth, sm->addr, (u8 *) hdr, len,
			    sm->pairwise_set);
	os_free(hdr);
}


static void wpa_send_eapol(struct wpa_authenticator *wpa_auth,
			   struct wpa_state_machine *sm, int key_info,
			   const u8 *key_rsc, const u8 *nonce,
			   const u8 *kde, size_t kde_len,
			   int keyidx, int encr)
{
	int timeout_ms;
	int pairwise = key_info & WPA_KEY_INFO_KEY_TYPE;
	u32 ctr;

	if (sm == NULL)
		return;

	__wpa_send_eapol(wpa_auth, sm, key_info, key_rsc, nonce, kde, kde_len,
			 keyidx, encr, 0);

	ctr = pairwise ? sm->TimeoutCtr : sm->GTimeoutCtr;
	if (ctr == 1 && wpa_auth->conf.tx_status)
		timeout_ms = pairwise ? eapol_key_timeout_first :
			eapol_key_timeout_first_group;
	else
		timeout_ms = eapol_key_timeout_subseq;
	if (wpa_auth->conf.wpa_disable_eapol_key_retries &&
	    (!pairwise || (key_info & WPA_KEY_INFO_MIC)))
		timeout_ms = eapol_key_timeout_no_retrans;
	if (pairwise && ctr == 1 && !(key_info & WPA_KEY_INFO_MIC))
		sm->pending_1_of_4_timeout = 1;
	wpa_printf(MSG_DEBUG, "WPA: Use EAPOL-Key timeout of %u ms (retry "
		   "counter %u)", timeout_ms, ctr);
	eloop_register_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000,
			       wpa_send_eapol_timeout, wpa_auth, sm);
}


static int wpa_verify_key_mic(int akmp, size_t pmk_len, struct wpa_ptk *PTK,
			      u8 *data, size_t data_len)
{
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u16 key_info;
	int ret = 0;
	u8 mic[WPA_EAPOL_KEY_MIC_MAX_LEN], *mic_pos;
	size_t mic_len = wpa_mic_len(akmp, pmk_len);

	if (data_len < sizeof(*hdr) + sizeof(*key))
		return -1;

	hdr = (struct ieee802_1x_hdr *) data;
	key = (struct wpa_eapol_key *) (hdr + 1);
	mic_pos = (u8 *) (key + 1);
	key_info = WPA_GET_BE16(key->key_info);
	os_memcpy(mic, mic_pos, mic_len);
	os_memset(mic_pos, 0, mic_len);
	if (wpa_eapol_key_mic(PTK->kck, PTK->kck_len, akmp,
			      key_info & WPA_KEY_INFO_TYPE_MASK,
			      data, data_len, mic_pos) ||
	    os_memcmp_const(mic, mic_pos, mic_len) != 0)
		ret = -1;
	os_memcpy(mic_pos, mic, mic_len);
	return ret;
}


void wpa_remove_ptk(struct wpa_state_machine *sm)
{
	sm->PTK_valid = FALSE;
	os_memset(&sm->PTK, 0, sizeof(sm->PTK));
	if (wpa_auth_set_key(sm->wpa_auth, 0, WPA_ALG_NONE, sm->addr, 0, NULL,
			     0))
		wpa_printf(MSG_DEBUG,
			   "RSN: PTK removal from the driver failed");
	sm->pairwise_set = FALSE;
	eloop_cancel_timeout(wpa_rekey_ptk, sm->wpa_auth, sm);
}


int wpa_auth_sm_event(struct wpa_state_machine *sm, enum wpa_event event)
{
	int remove_ptk = 1;

	if (sm == NULL)
		return -1;

	wpa_auth_vlogger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			 "event %d notification", event);

	switch (event) {
	case WPA_AUTH:
#ifdef CONFIG_MESH
		/* PTKs are derived through AMPE */
		if (wpa_auth_start_ampe(sm->wpa_auth, sm->addr)) {
			/* not mesh */
			break;
		}
		return 0;
#endif /* CONFIG_MESH */
	case WPA_ASSOC:
		break;
	case WPA_DEAUTH:
	case WPA_DISASSOC:
		sm->DeauthenticationRequest = TRUE;
		break;
	case WPA_REAUTH:
	case WPA_REAUTH_EAPOL:
		if (!sm->started) {
			/*
			 * When using WPS, we may end up here if the STA
			 * manages to re-associate without the previous STA
			 * entry getting removed. Consequently, we need to make
			 * sure that the WPA state machines gets initialized
			 * properly at this point.
			 */
			wpa_printf(MSG_DEBUG, "WPA state machine had not been "
				   "started - initialize now");
			sm->started = 1;
			sm->Init = TRUE;
			if (wpa_sm_step(sm) == 1)
				return 1; /* should not really happen */
			sm->Init = FALSE;
			sm->AuthenticationRequest = TRUE;
			break;
		}
		if (sm->GUpdateStationKeys) {
			/*
			 * Reauthentication cancels the pending group key
			 * update for this STA.
			 */
			sm->group->GKeyDoneStations--;
			sm->GUpdateStationKeys = FALSE;
			sm->PtkGroupInit = TRUE;
		}
		sm->ReAuthenticationRequest = TRUE;
		break;
	case WPA_ASSOC_FT:
#ifdef CONFIG_IEEE80211R_AP
		wpa_printf(MSG_DEBUG, "FT: Retry PTK configuration "
			   "after association");
		wpa_ft_install_ptk(sm);

		/* Using FT protocol, not WPA auth state machine */
		sm->ft_completed = 1;
		return 0;
#else /* CONFIG_IEEE80211R_AP */
		break;
#endif /* CONFIG_IEEE80211R_AP */
	case WPA_ASSOC_FILS:
#ifdef CONFIG_FILS
		wpa_printf(MSG_DEBUG,
			   "FILS: TK configuration after association");
		fils_set_tk(sm);
		sm->fils_completed = 1;
		return 0;
#else /* CONFIG_FILS */
		break;
#endif /* CONFIG_FILS */
	case WPA_DRV_STA_REMOVED:
		sm->tk_already_set = FALSE;
		return 0;
	}

#ifdef CONFIG_IEEE80211R_AP
	sm->ft_completed = 0;
#endif /* CONFIG_IEEE80211R_AP */

#ifdef CONFIG_IEEE80211W
	if (sm->mgmt_frame_prot && event == WPA_AUTH)
		remove_ptk = 0;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_FILS
	if (wpa_key_mgmt_fils(sm->wpa_key_mgmt) &&
	    (event == WPA_AUTH || event == WPA_ASSOC))
		remove_ptk = 0;
#endif /* CONFIG_FILS */

	if (remove_ptk) {
		sm->PTK_valid = FALSE;
		os_memset(&sm->PTK, 0, sizeof(sm->PTK));

		if (event != WPA_REAUTH_EAPOL)
			wpa_remove_ptk(sm);
	}

	if (sm->in_step_loop) {
		/*
		 * wpa_sm_step() is already running - avoid recursive call to
		 * it by making the existing loop process the new update.
		 */
		sm->changed = TRUE;
		return 0;
	}
	return wpa_sm_step(sm);
}


SM_STATE(WPA_PTK, INITIALIZE)
{
	SM_ENTRY_MA(WPA_PTK, INITIALIZE, wpa_ptk);
	if (sm->Init) {
		/* Init flag is not cleared here, so avoid busy
		 * loop by claiming nothing changed. */
		sm->changed = FALSE;
	}

	sm->keycount = 0;
	if (sm->GUpdateStationKeys)
		sm->group->GKeyDoneStations--;
	sm->GUpdateStationKeys = FALSE;
	if (sm->wpa == WPA_VERSION_WPA)
		sm->PInitAKeys = FALSE;
	if (1 /* Unicast cipher supported AND (ESS OR ((IBSS or WDS) and
	       * Local AA > Remote AA)) */) {
		sm->Pair = TRUE;
	}
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_portEnabled, 0);
	wpa_remove_ptk(sm);
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_portValid, 0);
	sm->TimeoutCtr = 0;
	if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) ||
	    sm->wpa_key_mgmt == WPA_KEY_MGMT_DPP ||
	    sm->wpa_key_mgmt == WPA_KEY_MGMT_OWE) {
		wpa_auth_set_eapol(sm->wpa_auth, sm->addr,
				   WPA_EAPOL_authorized, 0);
	}
}


SM_STATE(WPA_PTK, DISCONNECT)
{
	u16 reason = sm->disconnect_reason;

	SM_ENTRY_MA(WPA_PTK, DISCONNECT, wpa_ptk);
	sm->Disconnect = FALSE;
	sm->disconnect_reason = 0;
	if (!reason)
		reason = WLAN_REASON_PREV_AUTH_NOT_VALID;
	wpa_sta_disconnect(sm->wpa_auth, sm->addr, reason);
}


SM_STATE(WPA_PTK, DISCONNECTED)
{
	SM_ENTRY_MA(WPA_PTK, DISCONNECTED, wpa_ptk);
	sm->DeauthenticationRequest = FALSE;
}


SM_STATE(WPA_PTK, AUTHENTICATION)
{
	SM_ENTRY_MA(WPA_PTK, AUTHENTICATION, wpa_ptk);
	os_memset(&sm->PTK, 0, sizeof(sm->PTK));
	sm->PTK_valid = FALSE;
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_portControl_Auto,
			   1);
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_portEnabled, 1);
	sm->AuthenticationRequest = FALSE;
}


static void wpa_group_ensure_init(struct wpa_authenticator *wpa_auth,
				  struct wpa_group *group)
{
	if (group->first_sta_seen)
		return;
	/*
	 * System has run bit further than at the time hostapd was started
	 * potentially very early during boot up. This provides better chances
	 * of collecting more randomness on embedded systems. Re-initialize the
	 * GMK and Counter here to improve their strength if there was not
	 * enough entropy available immediately after system startup.
	 */
	wpa_printf(MSG_DEBUG, "WPA: Re-initialize GMK/Counter on first "
		   "station");
	if (random_pool_ready() != 1) {
		wpa_printf(MSG_INFO, "WPA: Not enough entropy in random pool "
			   "to proceed - reject first 4-way handshake");
		group->reject_4way_hs_for_entropy = TRUE;
	} else {
		group->first_sta_seen = TRUE;
		group->reject_4way_hs_for_entropy = FALSE;
	}

	if (wpa_group_init_gmk_and_counter(wpa_auth, group) < 0 ||
	    wpa_gtk_update(wpa_auth, group) < 0 ||
	    wpa_group_config_group_keys(wpa_auth, group) < 0) {
		wpa_printf(MSG_INFO, "WPA: GMK/GTK setup failed");
		group->first_sta_seen = FALSE;
		group->reject_4way_hs_for_entropy = TRUE;
	}
}


SM_STATE(WPA_PTK, AUTHENTICATION2)
{
	SM_ENTRY_MA(WPA_PTK, AUTHENTICATION2, wpa_ptk);

	wpa_group_ensure_init(sm->wpa_auth, sm->group);
	sm->ReAuthenticationRequest = FALSE;

	/*
	 * Definition of ANonce selection in IEEE Std 802.11i-2004 is somewhat
	 * ambiguous. The Authenticator state machine uses a counter that is
	 * incremented by one for each 4-way handshake. However, the security
	 * analysis of 4-way handshake points out that unpredictable nonces
	 * help in preventing precomputation attacks. Instead of the state
	 * machine definition, use an unpredictable nonce value here to provide
	 * stronger protection against potential precomputation attacks.
	 */
	if (random_get_bytes(sm->ANonce, WPA_NONCE_LEN)) {
		wpa_printf(MSG_ERROR, "WPA: Failed to get random data for "
			   "ANonce.");
		sm->Disconnect = TRUE;
		return;
	}
	wpa_hexdump(MSG_DEBUG, "WPA: Assign ANonce", sm->ANonce,
		    WPA_NONCE_LEN);
	/* IEEE 802.11i does not clear TimeoutCtr here, but this is more
	 * logical place than INITIALIZE since AUTHENTICATION2 can be
	 * re-entered on ReAuthenticationRequest without going through
	 * INITIALIZE. */
	sm->TimeoutCtr = 0;
}


static int wpa_auth_sm_ptk_update(struct wpa_state_machine *sm)
{
	if (random_get_bytes(sm->ANonce, WPA_NONCE_LEN)) {
		wpa_printf(MSG_ERROR,
			   "WPA: Failed to get random data for ANonce");
		sm->Disconnect = TRUE;
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "WPA: Assign new ANonce", sm->ANonce,
		    WPA_NONCE_LEN);
	sm->TimeoutCtr = 0;
	return 0;
}


SM_STATE(WPA_PTK, INITPMK)
{
	u8 msk[2 * PMK_LEN];
	size_t len = 2 * PMK_LEN;

	SM_ENTRY_MA(WPA_PTK, INITPMK, wpa_ptk);
#ifdef CONFIG_IEEE80211R_AP
	sm->xxkey_len = 0;
#endif /* CONFIG_IEEE80211R_AP */
	if (sm->pmksa) {
		wpa_printf(MSG_DEBUG, "WPA: PMK from PMKSA cache");
		os_memcpy(sm->PMK, sm->pmksa->pmk, sm->pmksa->pmk_len);
		sm->pmk_len = sm->pmksa->pmk_len;
#ifdef CONFIG_DPP
	} else if (sm->wpa_key_mgmt == WPA_KEY_MGMT_DPP) {
		wpa_printf(MSG_DEBUG,
			   "DPP: No PMKSA cache entry for STA - reject connection");
		sm->Disconnect = TRUE;
		sm->disconnect_reason = WLAN_REASON_INVALID_PMKID;
		return;
#endif /* CONFIG_DPP */
	} else if (wpa_auth_get_msk(sm->wpa_auth, sm->addr, msk, &len) == 0) {
		unsigned int pmk_len;

		if (wpa_key_mgmt_sha384(sm->wpa_key_mgmt))
			pmk_len = PMK_LEN_SUITE_B_192;
		else
			pmk_len = PMK_LEN;
		wpa_printf(MSG_DEBUG, "WPA: PMK from EAPOL state machine "
			   "(MSK len=%lu PMK len=%u)", (unsigned long) len,
			   pmk_len);
		if (len < pmk_len) {
			wpa_printf(MSG_DEBUG,
				   "WPA: MSK not long enough (%u) to create PMK (%u)",
				   (unsigned int) len, (unsigned int) pmk_len);
			sm->Disconnect = TRUE;
			return;
		}
		os_memcpy(sm->PMK, msk, pmk_len);
		sm->pmk_len = pmk_len;
#ifdef CONFIG_IEEE80211R_AP
		if (len >= 2 * PMK_LEN) {
			if (wpa_key_mgmt_sha384(sm->wpa_key_mgmt)) {
				os_memcpy(sm->xxkey, msk, SHA384_MAC_LEN);
				sm->xxkey_len = SHA384_MAC_LEN;
			} else {
				os_memcpy(sm->xxkey, msk + PMK_LEN, PMK_LEN);
				sm->xxkey_len = PMK_LEN;
			}
		}
#endif /* CONFIG_IEEE80211R_AP */
	} else {
		wpa_printf(MSG_DEBUG, "WPA: Could not get PMK, get_msk: %p",
			   sm->wpa_auth->cb->get_msk);
		sm->Disconnect = TRUE;
		return;
	}
	os_memset(msk, 0, sizeof(msk));

	sm->req_replay_counter_used = 0;
	/* IEEE 802.11i does not set keyRun to FALSE, but not doing this
	 * will break reauthentication since EAPOL state machines may not be
	 * get into AUTHENTICATING state that clears keyRun before WPA state
	 * machine enters AUTHENTICATION2 state and goes immediately to INITPMK
	 * state and takes PMK from the previously used AAA Key. This will
	 * eventually fail in 4-Way Handshake because Supplicant uses PMK
	 * derived from the new AAA Key. Setting keyRun = FALSE here seems to
	 * be good workaround for this issue. */
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_keyRun, 0);
}


SM_STATE(WPA_PTK, INITPSK)
{
	const u8 *psk;
	size_t psk_len;

	SM_ENTRY_MA(WPA_PTK, INITPSK, wpa_ptk);
	psk = wpa_auth_get_psk(sm->wpa_auth, sm->addr, sm->p2p_dev_addr, NULL,
			       &psk_len);
	if (psk) {
		os_memcpy(sm->PMK, psk, psk_len);
		sm->pmk_len = psk_len;
#ifdef CONFIG_IEEE80211R_AP
		os_memcpy(sm->xxkey, psk, PMK_LEN);
		sm->xxkey_len = PMK_LEN;
#endif /* CONFIG_IEEE80211R_AP */
	}
#ifdef CONFIG_SAE
	if (wpa_auth_uses_sae(sm) && sm->pmksa) {
		wpa_printf(MSG_DEBUG, "SAE: PMK from PMKSA cache");
		os_memcpy(sm->PMK, sm->pmksa->pmk, sm->pmksa->pmk_len);
		sm->pmk_len = sm->pmksa->pmk_len;
	}
#endif /* CONFIG_SAE */
	sm->req_replay_counter_used = 0;
}


SM_STATE(WPA_PTK, PTKSTART)
{
	u8 buf[2 + RSN_SELECTOR_LEN + PMKID_LEN], *pmkid = NULL;
	size_t pmkid_len = 0;

	SM_ENTRY_MA(WPA_PTK, PTKSTART, wpa_ptk);
	sm->PTKRequest = FALSE;
	sm->TimeoutEvt = FALSE;
	sm->alt_snonce_valid = FALSE;

	sm->TimeoutCtr++;
	if (sm->TimeoutCtr > sm->wpa_auth->conf.wpa_pairwise_update_count) {
		/* No point in sending the EAPOL-Key - we will disconnect
		 * immediately following this. */
		return;
	}

	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 1/4 msg of 4-Way Handshake");
	/*
	 * For infrastructure BSS cases, it is better for the AP not to include
	 * the PMKID KDE in EAPOL-Key msg 1/4 since it could be used to initiate
	 * offline search for the passphrase/PSK without having to be able to
	 * capture a 4-way handshake from a STA that has access to the network.
	 *
	 * For IBSS cases, addition of PMKID KDE could be considered even with
	 * WPA2-PSK cases that use multiple PSKs, but only if there is a single
	 * possible PSK for this STA. However, this should not be done unless
	 * there is support for using that information on the supplicant side.
	 * The concern about exposing PMKID unnecessarily in infrastructure BSS
	 * cases would also apply here, but at least in the IBSS case, this
	 * would cover a potential real use case.
	 */
	if (sm->wpa == WPA_VERSION_WPA2 &&
	    (wpa_key_mgmt_wpa_ieee8021x(sm->wpa_key_mgmt) ||
	     (sm->wpa_key_mgmt == WPA_KEY_MGMT_OWE && sm->pmksa) ||
	     wpa_key_mgmt_sae(sm->wpa_key_mgmt)) &&
	    sm->wpa_key_mgmt != WPA_KEY_MGMT_OSEN) {
		pmkid = buf;
		pmkid_len = 2 + RSN_SELECTOR_LEN + PMKID_LEN;
		pmkid[0] = WLAN_EID_VENDOR_SPECIFIC;
		pmkid[1] = RSN_SELECTOR_LEN + PMKID_LEN;
		RSN_SELECTOR_PUT(&pmkid[2], RSN_KEY_DATA_PMKID);
		if (sm->pmksa) {
			wpa_hexdump(MSG_DEBUG,
				    "RSN: Message 1/4 PMKID from PMKSA entry",
				    sm->pmksa->pmkid, PMKID_LEN);
			os_memcpy(&pmkid[2 + RSN_SELECTOR_LEN],
				  sm->pmksa->pmkid, PMKID_LEN);
		} else if (wpa_key_mgmt_suite_b(sm->wpa_key_mgmt)) {
			/* No KCK available to derive PMKID */
			wpa_printf(MSG_DEBUG,
				   "RSN: No KCK available to derive PMKID for message 1/4");
			pmkid = NULL;
#ifdef CONFIG_SAE
		} else if (wpa_key_mgmt_sae(sm->wpa_key_mgmt)) {
			if (sm->pmkid_set) {
				wpa_hexdump(MSG_DEBUG,
					    "RSN: Message 1/4 PMKID from SAE",
					    sm->pmkid, PMKID_LEN);
				os_memcpy(&pmkid[2 + RSN_SELECTOR_LEN],
					  sm->pmkid, PMKID_LEN);
			} else {
				/* No PMKID available */
				wpa_printf(MSG_DEBUG,
					   "RSN: No SAE PMKID available for message 1/4");
				pmkid = NULL;
			}
#endif /* CONFIG_SAE */
		} else {
			/*
			 * Calculate PMKID since no PMKSA cache entry was
			 * available with pre-calculated PMKID.
			 */
			rsn_pmkid(sm->PMK, sm->pmk_len, sm->wpa_auth->addr,
				  sm->addr, &pmkid[2 + RSN_SELECTOR_LEN],
				  sm->wpa_key_mgmt);
			wpa_hexdump(MSG_DEBUG,
				    "RSN: Message 1/4 PMKID derived from PMK",
				    &pmkid[2 + RSN_SELECTOR_LEN], PMKID_LEN);
		}
	}
	wpa_send_eapol(sm->wpa_auth, sm,
		       WPA_KEY_INFO_ACK | WPA_KEY_INFO_KEY_TYPE, NULL,
		       sm->ANonce, pmkid, pmkid_len, 0, 0);
}


static int wpa_derive_ptk(struct wpa_state_machine *sm, const u8 *snonce,
			  const u8 *pmk, unsigned int pmk_len,
			  struct wpa_ptk *ptk)
{
#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt))
		return wpa_auth_derive_ptk_ft(sm, pmk, ptk);
#endif /* CONFIG_IEEE80211R_AP */

	return wpa_pmk_to_ptk(pmk, pmk_len, "Pairwise key expansion",
			      sm->wpa_auth->addr, sm->addr, sm->ANonce, snonce,
			      ptk, sm->wpa_key_mgmt, sm->pairwise);
}


#ifdef CONFIG_FILS

int fils_auth_pmk_to_ptk(struct wpa_state_machine *sm, const u8 *pmk,
			 size_t pmk_len, const u8 *snonce, const u8 *anonce,
			 const u8 *dhss, size_t dhss_len,
			 struct wpabuf *g_sta, struct wpabuf *g_ap)
{
	u8 ick[FILS_ICK_MAX_LEN];
	size_t ick_len;
	int res;
	u8 fils_ft[FILS_FT_MAX_LEN];
	size_t fils_ft_len = 0;

	res = fils_pmk_to_ptk(pmk, pmk_len, sm->addr, sm->wpa_auth->addr,
			      snonce, anonce, dhss, dhss_len,
			      &sm->PTK, ick, &ick_len,
			      sm->wpa_key_mgmt, sm->pairwise,
			      fils_ft, &fils_ft_len);
	if (res < 0)
		return res;
	sm->PTK_valid = TRUE;
	sm->tk_already_set = FALSE;

#ifdef CONFIG_IEEE80211R_AP
	if (fils_ft_len) {
		struct wpa_authenticator *wpa_auth = sm->wpa_auth;
		struct wpa_auth_config *conf = &wpa_auth->conf;
		u8 pmk_r0[PMK_LEN_MAX], pmk_r0_name[WPA_PMK_NAME_LEN];
		int use_sha384 = wpa_key_mgmt_sha384(sm->wpa_key_mgmt);
		size_t pmk_r0_len = use_sha384 ? SHA384_MAC_LEN : PMK_LEN;

		if (wpa_derive_pmk_r0(fils_ft, fils_ft_len,
				      conf->ssid, conf->ssid_len,
				      conf->mobility_domain,
				      conf->r0_key_holder,
				      conf->r0_key_holder_len,
				      sm->addr, pmk_r0, pmk_r0_name,
				      use_sha384) < 0)
			return -1;

		wpa_hexdump_key(MSG_DEBUG, "FILS+FT: PMK-R0",
				pmk_r0, pmk_r0_len);
		wpa_hexdump(MSG_DEBUG, "FILS+FT: PMKR0Name",
			    pmk_r0_name, WPA_PMK_NAME_LEN);
		wpa_ft_store_pmk_fils(sm, pmk_r0, pmk_r0_name);
		os_memset(fils_ft, 0, sizeof(fils_ft));
	}
#endif /* CONFIG_IEEE80211R_AP */

	res = fils_key_auth_sk(ick, ick_len, snonce, anonce,
			       sm->addr, sm->wpa_auth->addr,
			       g_sta ? wpabuf_head(g_sta) : NULL,
			       g_sta ? wpabuf_len(g_sta) : 0,
			       g_ap ? wpabuf_head(g_ap) : NULL,
			       g_ap ? wpabuf_len(g_ap) : 0,
			       sm->wpa_key_mgmt, sm->fils_key_auth_sta,
			       sm->fils_key_auth_ap,
			       &sm->fils_key_auth_len);
	os_memset(ick, 0, sizeof(ick));

	/* Store nonces for (Re)Association Request/Response frame processing */
	os_memcpy(sm->SNonce, snonce, FILS_NONCE_LEN);
	os_memcpy(sm->ANonce, anonce, FILS_NONCE_LEN);

	return res;
}


static int wpa_aead_decrypt(struct wpa_state_machine *sm, struct wpa_ptk *ptk,
			    u8 *buf, size_t buf_len, u16 *_key_data_len)
{
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u8 *pos;
	u16 key_data_len;
	u8 *tmp;
	const u8 *aad[1];
	size_t aad_len[1];

	hdr = (struct ieee802_1x_hdr *) buf;
	key = (struct wpa_eapol_key *) (hdr + 1);
	pos = (u8 *) (key + 1);
	key_data_len = WPA_GET_BE16(pos);
	if (key_data_len < AES_BLOCK_SIZE ||
	    key_data_len > buf_len - sizeof(*hdr) - sizeof(*key) - 2) {
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_INFO,
				"No room for AES-SIV data in the frame");
		return -1;
	}
	pos += 2; /* Pointing at the Encrypted Key Data field */

	tmp = os_malloc(key_data_len);
	if (!tmp)
		return -1;

	/* AES-SIV AAD from EAPOL protocol version field (inclusive) to
	 * to Key Data (exclusive). */
	aad[0] = buf;
	aad_len[0] = pos - buf;
	if (aes_siv_decrypt(ptk->kek, ptk->kek_len, pos, key_data_len,
			    1, aad, aad_len, tmp) < 0) {
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_INFO,
				"Invalid AES-SIV data in the frame");
		bin_clear_free(tmp, key_data_len);
		return -1;
	}

	/* AEAD decryption and validation completed successfully */
	key_data_len -= AES_BLOCK_SIZE;
	wpa_hexdump_key(MSG_DEBUG, "WPA: Decrypted Key Data",
			tmp, key_data_len);

	/* Replace Key Data field with the decrypted version */
	os_memcpy(pos, tmp, key_data_len);
	pos -= 2; /* Key Data Length field */
	WPA_PUT_BE16(pos, key_data_len);
	bin_clear_free(tmp, key_data_len);
	if (_key_data_len)
		*_key_data_len = key_data_len;
	return 0;
}


const u8 * wpa_fils_validate_fils_session(struct wpa_state_machine *sm,
					  const u8 *ies, size_t ies_len,
					  const u8 *fils_session)
{
	const u8 *ie, *end;
	const u8 *session = NULL;

	if (!wpa_key_mgmt_fils(sm->wpa_key_mgmt)) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Not a FILS AKM - reject association");
		return NULL;
	}

	/* Verify Session element */
	ie = ies;
	end = ((const u8 *) ie) + ies_len;
	while (ie + 1 < end) {
		if (ie + 2 + ie[1] > end)
			break;
		if (ie[0] == WLAN_EID_EXTENSION &&
		    ie[1] >= 1 + FILS_SESSION_LEN &&
		    ie[2] == WLAN_EID_EXT_FILS_SESSION) {
			session = ie;
			break;
		}
		ie += 2 + ie[1];
	}

	if (!session) {
		wpa_printf(MSG_DEBUG,
			   "FILS: %s: Could not find FILS Session element in Assoc Req - reject",
			   __func__);
		return NULL;
	}

	if (!fils_session) {
		wpa_printf(MSG_DEBUG,
			   "FILS: %s: Could not find FILS Session element in STA entry - reject",
			   __func__);
		return NULL;
	}

	if (os_memcmp(fils_session, session + 3, FILS_SESSION_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FILS: Session mismatch");
		wpa_hexdump(MSG_DEBUG, "FILS: Expected FILS Session",
			    fils_session, FILS_SESSION_LEN);
		wpa_hexdump(MSG_DEBUG, "FILS: Received FILS Session",
			    session + 3, FILS_SESSION_LEN);
		return NULL;
	}
	return session;
}


int wpa_fils_validate_key_confirm(struct wpa_state_machine *sm, const u8 *ies,
				  size_t ies_len)
{
	struct ieee802_11_elems elems;

	if (ieee802_11_parse_elems(ies, ies_len, &elems, 1) == ParseFailed) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Failed to parse decrypted elements");
		return -1;
	}

	if (!elems.fils_session) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Session element");
		return -1;
	}

	if (!elems.fils_key_confirm) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Key Confirm element");
		return -1;
	}

	if (elems.fils_key_confirm_len != sm->fils_key_auth_len) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Unexpected Key-Auth length %d (expected %d)",
			   elems.fils_key_confirm_len,
			   (int) sm->fils_key_auth_len);
		return -1;
	}

	if (os_memcmp(elems.fils_key_confirm, sm->fils_key_auth_sta,
		      sm->fils_key_auth_len) != 0) {
		wpa_printf(MSG_DEBUG, "FILS: Key-Auth mismatch");
		wpa_hexdump(MSG_DEBUG, "FILS: Received Key-Auth",
			    elems.fils_key_confirm, elems.fils_key_confirm_len);
		wpa_hexdump(MSG_DEBUG, "FILS: Expected Key-Auth",
			    sm->fils_key_auth_sta, sm->fils_key_auth_len);
		return -1;
	}

	return 0;
}


int fils_decrypt_assoc(struct wpa_state_machine *sm, const u8 *fils_session,
		       const struct ieee80211_mgmt *mgmt, size_t frame_len,
		       u8 *pos, size_t left)
{
	u16 fc, stype;
	const u8 *end, *ie_start, *ie, *session, *crypt;
	const u8 *aad[5];
	size_t aad_len[5];

	if (!sm || !sm->PTK_valid) {
		wpa_printf(MSG_DEBUG,
			   "FILS: No KEK to decrypt Assocication Request frame");
		return -1;
	}

	if (!wpa_key_mgmt_fils(sm->wpa_key_mgmt)) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Not a FILS AKM - reject association");
		return -1;
	}

	end = ((const u8 *) mgmt) + frame_len;
	fc = le_to_host16(mgmt->frame_control);
	stype = WLAN_FC_GET_STYPE(fc);
	if (stype == WLAN_FC_STYPE_REASSOC_REQ)
		ie_start = mgmt->u.reassoc_req.variable;
	else
		ie_start = mgmt->u.assoc_req.variable;
	ie = ie_start;

	/*
	 * Find FILS Session element which is the last unencrypted element in
	 * the frame.
	 */
	session = wpa_fils_validate_fils_session(sm, ie, end - ie,
						 fils_session);
	if (!session) {
		wpa_printf(MSG_DEBUG, "FILS: Session validation failed");
		return -1;
	}

	crypt = session + 2 + session[1];

	if (end - crypt < AES_BLOCK_SIZE) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Too short frame to include AES-SIV data");
		return -1;
	}

	/* AES-SIV AAD vectors */

	/* The STA's MAC address */
	aad[0] = mgmt->sa;
	aad_len[0] = ETH_ALEN;
	/* The AP's BSSID */
	aad[1] = mgmt->da;
	aad_len[1] = ETH_ALEN;
	/* The STA's nonce */
	aad[2] = sm->SNonce;
	aad_len[2] = FILS_NONCE_LEN;
	/* The AP's nonce */
	aad[3] = sm->ANonce;
	aad_len[3] = FILS_NONCE_LEN;
	/*
	 * The (Re)Association Request frame from the Capability Information
	 * field to the FILS Session element (both inclusive).
	 */
	aad[4] = (const u8 *) &mgmt->u.assoc_req.capab_info;
	aad_len[4] = crypt - aad[4];

	if (aes_siv_decrypt(sm->PTK.kek, sm->PTK.kek_len, crypt, end - crypt,
			    5, aad, aad_len, pos + (crypt - ie_start)) < 0) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Invalid AES-SIV data in the frame");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "FILS: Decrypted Association Request elements",
		    pos, left - AES_BLOCK_SIZE);

	if (wpa_fils_validate_key_confirm(sm, pos, left - AES_BLOCK_SIZE) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: Key Confirm validation failed");
		return -1;
	}

	return left - AES_BLOCK_SIZE;
}


int fils_encrypt_assoc(struct wpa_state_machine *sm, u8 *buf,
		       size_t current_len, size_t max_len,
		       const struct wpabuf *hlp)
{
	u8 *end = buf + max_len;
	u8 *pos = buf + current_len;
	struct ieee80211_mgmt *mgmt;
	struct wpabuf *plain;
	const u8 *aad[5];
	size_t aad_len[5];

	if (!sm || !sm->PTK_valid)
		return -1;

	wpa_hexdump(MSG_DEBUG,
		    "FILS: Association Response frame before FILS processing",
		    buf, current_len);

	mgmt = (struct ieee80211_mgmt *) buf;

	/* AES-SIV AAD vectors */

	/* The AP's BSSID */
	aad[0] = mgmt->sa;
	aad_len[0] = ETH_ALEN;
	/* The STA's MAC address */
	aad[1] = mgmt->da;
	aad_len[1] = ETH_ALEN;
	/* The AP's nonce */
	aad[2] = sm->ANonce;
	aad_len[2] = FILS_NONCE_LEN;
	/* The STA's nonce */
	aad[3] = sm->SNonce;
	aad_len[3] = FILS_NONCE_LEN;
	/*
	 * The (Re)Association Response frame from the Capability Information
	 * field (the same offset in both Association and Reassociation
	 * Response frames) to the FILS Session element (both inclusive).
	 */
	aad[4] = (const u8 *) &mgmt->u.assoc_resp.capab_info;
	aad_len[4] = pos - aad[4];

	/* The following elements will be encrypted with AES-SIV */
	plain = fils_prepare_plainbuf(sm, hlp);
	if (!plain) {
		wpa_printf(MSG_DEBUG, "FILS: Plain buffer prep failed");
		return -1;
	}

	if (pos + wpabuf_len(plain) + AES_BLOCK_SIZE > end) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Not enough room for FILS elements");
		wpabuf_free(plain);
		return -1;
	}

	wpa_hexdump_buf_key(MSG_DEBUG, "FILS: Association Response plaintext",
			    plain);

	if (aes_siv_encrypt(sm->PTK.kek, sm->PTK.kek_len,
			    wpabuf_head(plain), wpabuf_len(plain),
			    5, aad, aad_len, pos) < 0) {
		wpabuf_free(plain);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG,
		    "FILS: Encrypted Association Response elements",
		    pos, AES_BLOCK_SIZE + wpabuf_len(plain));
	current_len += wpabuf_len(plain) + AES_BLOCK_SIZE;
	wpabuf_free(plain);

	sm->fils_completed = 1;

	return current_len;
}


static struct wpabuf * fils_prepare_plainbuf(struct wpa_state_machine *sm,
					     const struct wpabuf *hlp)
{
	struct wpabuf *plain;
	u8 *len, *tmp, *tmp2;
	u8 hdr[2];
	u8 *gtk, dummy_gtk[32];
	size_t gtk_len;
	struct wpa_group *gsm;

	plain = wpabuf_alloc(1000);
	if (!plain)
		return NULL;

	/* TODO: FILS Public Key */

	/* FILS Key Confirmation */
	wpabuf_put_u8(plain, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(plain, 1 + sm->fils_key_auth_len); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(plain, WLAN_EID_EXT_FILS_KEY_CONFIRM);
	wpabuf_put_data(plain, sm->fils_key_auth_ap, sm->fils_key_auth_len);

	/* FILS HLP Container */
	if (hlp)
		wpabuf_put_buf(plain, hlp);

	/* TODO: FILS IP Address Assignment */

	/* Key Delivery */
	gsm = sm->group;
	wpabuf_put_u8(plain, WLAN_EID_EXTENSION); /* Element ID */
	len = wpabuf_put(plain, 1);
	wpabuf_put_u8(plain, WLAN_EID_EXT_KEY_DELIVERY);
	wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN,
			    wpabuf_put(plain, WPA_KEY_RSC_LEN));
	/* GTK KDE */
	gtk = gsm->GTK[gsm->GN - 1];
	gtk_len = gsm->GTK_len;
	if (sm->wpa_auth->conf.disable_gtk ||
	    sm->wpa_key_mgmt == WPA_KEY_MGMT_OSEN) {
		/*
		 * Provide unique random GTK to each STA to prevent use
		 * of GTK in the BSS.
		 */
		if (random_get_bytes(dummy_gtk, gtk_len) < 0) {
			wpabuf_free(plain);
			return NULL;
		}
		gtk = dummy_gtk;
	}
	hdr[0] = gsm->GN & 0x03;
	hdr[1] = 0;
	tmp = wpabuf_put(plain, 0);
	tmp2 = wpa_add_kde(tmp, RSN_KEY_DATA_GROUPKEY, hdr, 2,
			   gtk, gtk_len);
	wpabuf_put(plain, tmp2 - tmp);

	/* IGTK KDE */
	tmp = wpabuf_put(plain, 0);
	tmp2 = ieee80211w_kde_add(sm, tmp);
	wpabuf_put(plain, tmp2 - tmp);

	*len = (u8 *) wpabuf_put(plain, 0) - len - 1;
	return plain;
}


int fils_set_tk(struct wpa_state_machine *sm)
{
	enum wpa_alg alg;
	int klen;

	if (!sm || !sm->PTK_valid) {
		wpa_printf(MSG_DEBUG, "FILS: No valid PTK available to set TK");
		return -1;
	}
	if (sm->tk_already_set) {
		wpa_printf(MSG_DEBUG, "FILS: TK already set to the driver");
		return -1;
	}

	alg = wpa_cipher_to_alg(sm->pairwise);
	klen = wpa_cipher_key_len(sm->pairwise);

	wpa_printf(MSG_DEBUG, "FILS: Configure TK to the driver");
	if (wpa_auth_set_key(sm->wpa_auth, 0, alg, sm->addr, 0,
			     sm->PTK.tk, klen)) {
		wpa_printf(MSG_DEBUG, "FILS: Failed to set TK to the driver");
		return -1;
	}
	sm->tk_already_set = TRUE;

	return 0;
}


u8 * hostapd_eid_assoc_fils_session(struct wpa_state_machine *sm, u8 *buf,
				    const u8 *fils_session, struct wpabuf *hlp)
{
	struct wpabuf *plain;
	u8 *pos = buf;

	/* FILS Session */
	*pos++ = WLAN_EID_EXTENSION; /* Element ID */
	*pos++ = 1 + FILS_SESSION_LEN; /* Length */
	*pos++ = WLAN_EID_EXT_FILS_SESSION; /* Element ID Extension */
	os_memcpy(pos, fils_session, FILS_SESSION_LEN);
	pos += FILS_SESSION_LEN;

	plain = fils_prepare_plainbuf(sm, hlp);
	if (!plain) {
		wpa_printf(MSG_DEBUG, "FILS: Plain buffer prep failed");
		return NULL;
	}

	os_memcpy(pos, wpabuf_head(plain), wpabuf_len(plain));
	pos += wpabuf_len(plain);

	wpa_printf(MSG_DEBUG, "%s: plain buf_len: %u", __func__,
		   (unsigned int) wpabuf_len(plain));
	wpabuf_free(plain);
	sm->fils_completed = 1;
	return pos;
}

#endif /* CONFIG_FILS */


SM_STATE(WPA_PTK, PTKCALCNEGOTIATING)
{
	struct wpa_authenticator *wpa_auth = sm->wpa_auth;
	struct wpa_ptk PTK;
	int ok = 0, psk_found = 0;
	const u8 *pmk = NULL;
	size_t pmk_len;
	int ft;
	const u8 *eapol_key_ie, *key_data, *mic;
	u16 key_data_length;
	size_t mic_len, eapol_key_ie_len;
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	struct wpa_eapol_ie_parse kde;

	SM_ENTRY_MA(WPA_PTK, PTKCALCNEGOTIATING, wpa_ptk);
	sm->EAPOLKeyReceived = FALSE;
	sm->update_snonce = FALSE;
	os_memset(&PTK, 0, sizeof(PTK));

	mic_len = wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len);

	/* WPA with IEEE 802.1X: use the derived PMK from EAP
	 * WPA-PSK: iterate through possible PSKs and select the one matching
	 * the packet */
	for (;;) {
		if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) &&
		    !wpa_key_mgmt_sae(sm->wpa_key_mgmt)) {
			pmk = wpa_auth_get_psk(sm->wpa_auth, sm->addr,
					       sm->p2p_dev_addr, pmk, &pmk_len);
			if (pmk == NULL)
				break;
			psk_found = 1;
#ifdef CONFIG_IEEE80211R_AP
			if (wpa_key_mgmt_ft_psk(sm->wpa_key_mgmt)) {
				os_memcpy(sm->xxkey, pmk, pmk_len);
				sm->xxkey_len = pmk_len;
			}
#endif /* CONFIG_IEEE80211R_AP */
		} else {
			pmk = sm->PMK;
			pmk_len = sm->pmk_len;
		}

		if (wpa_derive_ptk(sm, sm->SNonce, pmk, pmk_len, &PTK) < 0)
			break;

		if (mic_len &&
		    wpa_verify_key_mic(sm->wpa_key_mgmt, pmk_len, &PTK,
				       sm->last_rx_eapol_key,
				       sm->last_rx_eapol_key_len) == 0) {
			ok = 1;
			break;
		}

#ifdef CONFIG_FILS
		if (!mic_len &&
		    wpa_aead_decrypt(sm, &PTK, sm->last_rx_eapol_key,
				     sm->last_rx_eapol_key_len, NULL) == 0) {
			ok = 1;
			break;
		}
#endif /* CONFIG_FILS */

		if (!wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) ||
		    wpa_key_mgmt_sae(sm->wpa_key_mgmt))
			break;
	}

	if (!ok) {
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"invalid MIC in msg 2/4 of 4-Way Handshake");
		if (psk_found)
			wpa_auth_psk_failure_report(sm->wpa_auth, sm->addr);
		return;
	}

	/*
	 * Note: last_rx_eapol_key length fields have already been validated in
	 * wpa_receive().
	 */
	hdr = (struct ieee802_1x_hdr *) sm->last_rx_eapol_key;
	key = (struct wpa_eapol_key *) (hdr + 1);
	mic = (u8 *) (key + 1);
	key_data = mic + mic_len + 2;
	key_data_length = WPA_GET_BE16(mic + mic_len);
	if (key_data_length > sm->last_rx_eapol_key_len - sizeof(*hdr) -
	    sizeof(*key) - mic_len - 2)
		return;

	if (wpa_parse_kde_ies(key_data, key_data_length, &kde) < 0) {
		wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
				 "received EAPOL-Key msg 2/4 with invalid Key Data contents");
		return;
	}
	if (kde.rsn_ie) {
		eapol_key_ie = kde.rsn_ie;
		eapol_key_ie_len = kde.rsn_ie_len;
	} else if (kde.osen) {
		eapol_key_ie = kde.osen;
		eapol_key_ie_len = kde.osen_len;
	} else {
		eapol_key_ie = kde.wpa_ie;
		eapol_key_ie_len = kde.wpa_ie_len;
	}
	ft = sm->wpa == WPA_VERSION_WPA2 && wpa_key_mgmt_ft(sm->wpa_key_mgmt);
	if (sm->wpa_ie == NULL ||
	    wpa_compare_rsn_ie(ft, sm->wpa_ie, sm->wpa_ie_len,
			       eapol_key_ie, eapol_key_ie_len)) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_INFO,
				"WPA IE from (Re)AssocReq did not match with msg 2/4");
		if (sm->wpa_ie) {
			wpa_hexdump(MSG_DEBUG, "WPA IE in AssocReq",
				    sm->wpa_ie, sm->wpa_ie_len);
		}
		wpa_hexdump(MSG_DEBUG, "WPA IE in msg 2/4",
			    eapol_key_ie, eapol_key_ie_len);
		/* MLME-DEAUTHENTICATE.request */
		wpa_sta_disconnect(wpa_auth, sm->addr,
				   WLAN_REASON_PREV_AUTH_NOT_VALID);
		return;
	}
#ifdef CONFIG_IEEE80211R_AP
	if (ft && ft_check_msg_2_of_4(wpa_auth, sm, &kde) < 0) {
		wpa_sta_disconnect(wpa_auth, sm->addr,
				   WLAN_REASON_PREV_AUTH_NOT_VALID);
		return;
	}
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_P2P
	if (kde.ip_addr_req && kde.ip_addr_req[0] &&
	    wpa_auth->ip_pool && WPA_GET_BE32(sm->ip_addr) == 0) {
		int idx;
		wpa_printf(MSG_DEBUG,
			   "P2P: IP address requested in EAPOL-Key exchange");
		idx = bitfield_get_first_zero(wpa_auth->ip_pool);
		if (idx >= 0) {
			u32 start = WPA_GET_BE32(wpa_auth->conf.ip_addr_start);
			bitfield_set(wpa_auth->ip_pool, idx);
			WPA_PUT_BE32(sm->ip_addr, start + idx);
			wpa_printf(MSG_DEBUG,
				   "P2P: Assigned IP address %u.%u.%u.%u to "
				   MACSTR, sm->ip_addr[0], sm->ip_addr[1],
				   sm->ip_addr[2], sm->ip_addr[3],
				   MAC2STR(sm->addr));
		}
	}
#endif /* CONFIG_P2P */

#ifdef CONFIG_IEEE80211R_AP
	if (sm->wpa == WPA_VERSION_WPA2 && wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		/*
		 * Verify that PMKR1Name from EAPOL-Key message 2/4 matches
		 * with the value we derived.
		 */
		if (os_memcmp_const(sm->sup_pmk_r1_name, sm->pmk_r1_name,
				    WPA_PMK_NAME_LEN) != 0) {
			wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
					"PMKR1Name mismatch in FT 4-way "
					"handshake");
			wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name from "
				    "Supplicant",
				    sm->sup_pmk_r1_name, WPA_PMK_NAME_LEN);
			wpa_hexdump(MSG_DEBUG, "FT: Derived PMKR1Name",
				    sm->pmk_r1_name, WPA_PMK_NAME_LEN);
			return;
		}
	}
#endif /* CONFIG_IEEE80211R_AP */

	sm->pending_1_of_4_timeout = 0;
	eloop_cancel_timeout(wpa_send_eapol_timeout, sm->wpa_auth, sm);

	if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt)) {
		/* PSK may have changed from the previous choice, so update
		 * state machine data based on whatever PSK was selected here.
		 */
		os_memcpy(sm->PMK, pmk, PMK_LEN);
		sm->pmk_len = PMK_LEN;
	}

	sm->MICVerified = TRUE;

	os_memcpy(&sm->PTK, &PTK, sizeof(PTK));
	sm->PTK_valid = TRUE;
}


SM_STATE(WPA_PTK, PTKCALCNEGOTIATING2)
{
	SM_ENTRY_MA(WPA_PTK, PTKCALCNEGOTIATING2, wpa_ptk);
	sm->TimeoutCtr = 0;
}


#ifdef CONFIG_IEEE80211W

static int ieee80211w_kde_len(struct wpa_state_machine *sm)
{
	if (sm->mgmt_frame_prot) {
		size_t len;
		len = wpa_cipher_key_len(sm->wpa_auth->conf.group_mgmt_cipher);
		return 2 + RSN_SELECTOR_LEN + WPA_IGTK_KDE_PREFIX_LEN + len;
	}

	return 0;
}


static u8 * ieee80211w_kde_add(struct wpa_state_machine *sm, u8 *pos)
{
	struct wpa_igtk_kde igtk;
	struct wpa_group *gsm = sm->group;
	u8 rsc[WPA_KEY_RSC_LEN];
	size_t len = wpa_cipher_key_len(sm->wpa_auth->conf.group_mgmt_cipher);

	if (!sm->mgmt_frame_prot)
		return pos;

	igtk.keyid[0] = gsm->GN_igtk;
	igtk.keyid[1] = 0;
	if (gsm->wpa_group_state != WPA_GROUP_SETKEYSDONE ||
	    wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN_igtk, rsc) < 0)
		os_memset(igtk.pn, 0, sizeof(igtk.pn));
	else
		os_memcpy(igtk.pn, rsc, sizeof(igtk.pn));
	os_memcpy(igtk.igtk, gsm->IGTK[gsm->GN_igtk - 4], len);
	if (sm->wpa_auth->conf.disable_gtk ||
	    sm->wpa_key_mgmt == WPA_KEY_MGMT_OSEN) {
		/*
		 * Provide unique random IGTK to each STA to prevent use of
		 * IGTK in the BSS.
		 */
		if (random_get_bytes(igtk.igtk, len) < 0)
			return pos;
	}
	pos = wpa_add_kde(pos, RSN_KEY_DATA_IGTK,
			  (const u8 *) &igtk, WPA_IGTK_KDE_PREFIX_LEN + len,
			  NULL, 0);

	return pos;
}

#else /* CONFIG_IEEE80211W */

static int ieee80211w_kde_len(struct wpa_state_machine *sm)
{
	return 0;
}


static u8 * ieee80211w_kde_add(struct wpa_state_machine *sm, u8 *pos)
{
	return pos;
}

#endif /* CONFIG_IEEE80211W */


SM_STATE(WPA_PTK, PTKINITNEGOTIATING)
{
	u8 rsc[WPA_KEY_RSC_LEN], *_rsc, *gtk, *kde, *pos, dummy_gtk[32];
	size_t gtk_len, kde_len;
	struct wpa_group *gsm = sm->group;
	u8 *wpa_ie;
	int wpa_ie_len, secure, keyidx, encr = 0;

	SM_ENTRY_MA(WPA_PTK, PTKINITNEGOTIATING, wpa_ptk);
	sm->TimeoutEvt = FALSE;

	sm->TimeoutCtr++;
	if (sm->wpa_auth->conf.wpa_disable_eapol_key_retries &&
	    sm->TimeoutCtr > 1) {
		/* Do not allow retransmission of EAPOL-Key msg 3/4 */
		return;
	}
	if (sm->TimeoutCtr > sm->wpa_auth->conf.wpa_pairwise_update_count) {
		/* No point in sending the EAPOL-Key - we will disconnect
		 * immediately following this. */
		return;
	}

	/* Send EAPOL(1, 1, 1, Pair, P, RSC, ANonce, MIC(PTK), RSNIE, [MDIE],
	   GTK[GN], IGTK, [FTIE], [TIE * 2])
	 */
	os_memset(rsc, 0, WPA_KEY_RSC_LEN);
	wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN, rsc);
	/* If FT is used, wpa_auth->wpa_ie includes both RSNIE and MDIE */
	wpa_ie = sm->wpa_auth->wpa_ie;
	wpa_ie_len = sm->wpa_auth->wpa_ie_len;
	if (sm->wpa == WPA_VERSION_WPA &&
	    (sm->wpa_auth->conf.wpa & WPA_PROTO_RSN) &&
	    wpa_ie_len > wpa_ie[1] + 2 && wpa_ie[0] == WLAN_EID_RSN) {
		/* WPA-only STA, remove RSN IE and possible MDIE */
		wpa_ie = wpa_ie + wpa_ie[1] + 2;
		if (wpa_ie[0] == WLAN_EID_MOBILITY_DOMAIN)
			wpa_ie = wpa_ie + wpa_ie[1] + 2;
		wpa_ie_len = wpa_ie[1] + 2;
	}
	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 3/4 msg of 4-Way Handshake");
	if (sm->wpa == WPA_VERSION_WPA2) {
		/* WPA2 send GTK in the 4-way handshake */
		secure = 1;
		gtk = gsm->GTK[gsm->GN - 1];
		gtk_len = gsm->GTK_len;
		if (sm->wpa_auth->conf.disable_gtk ||
		    sm->wpa_key_mgmt == WPA_KEY_MGMT_OSEN) {
			/*
			 * Provide unique random GTK to each STA to prevent use
			 * of GTK in the BSS.
			 */
			if (random_get_bytes(dummy_gtk, gtk_len) < 0)
				return;
			gtk = dummy_gtk;
		}
		keyidx = gsm->GN;
		_rsc = rsc;
		encr = 1;
	} else {
		/* WPA does not include GTK in msg 3/4 */
		secure = 0;
		gtk = NULL;
		gtk_len = 0;
		keyidx = 0;
		_rsc = NULL;
		if (sm->rx_eapol_key_secure) {
			/*
			 * It looks like Windows 7 supplicant tries to use
			 * Secure bit in msg 2/4 after having reported Michael
			 * MIC failure and it then rejects the 4-way handshake
			 * if msg 3/4 does not set Secure bit. Work around this
			 * by setting the Secure bit here even in the case of
			 * WPA if the supplicant used it first.
			 */
			wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
					"STA used Secure bit in WPA msg 2/4 - "
					"set Secure for 3/4 as workaround");
			secure = 1;
		}
	}

	kde_len = wpa_ie_len + ieee80211w_kde_len(sm);
	if (gtk)
		kde_len += 2 + RSN_SELECTOR_LEN + 2 + gtk_len;
#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		kde_len += 2 + PMKID_LEN; /* PMKR1Name into RSN IE */
		kde_len += 300; /* FTIE + 2 * TIE */
	}
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_P2P
	if (WPA_GET_BE32(sm->ip_addr) > 0)
		kde_len += 2 + RSN_SELECTOR_LEN + 3 * 4;
#endif /* CONFIG_P2P */
	kde = os_malloc(kde_len);
	if (kde == NULL)
		return;

	pos = kde;
	os_memcpy(pos, wpa_ie, wpa_ie_len);
	pos += wpa_ie_len;
#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		int res;
		size_t elen;

		elen = pos - kde;
		res = wpa_insert_pmkid(kde, &elen, sm->pmk_r1_name);
		if (res < 0) {
			wpa_printf(MSG_ERROR, "FT: Failed to insert "
				   "PMKR1Name into RSN IE in EAPOL-Key data");
			os_free(kde);
			return;
		}
		pos -= wpa_ie_len;
		pos += elen;
	}
#endif /* CONFIG_IEEE80211R_AP */
	if (gtk) {
		u8 hdr[2];
		hdr[0] = keyidx & 0x03;
		hdr[1] = 0;
		pos = wpa_add_kde(pos, RSN_KEY_DATA_GROUPKEY, hdr, 2,
				  gtk, gtk_len);
	}
	pos = ieee80211w_kde_add(sm, pos);

#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		int res;
		struct wpa_auth_config *conf;

		conf = &sm->wpa_auth->conf;
		if (sm->assoc_resp_ftie &&
		    kde + kde_len - pos >= 2 + sm->assoc_resp_ftie[1]) {
			os_memcpy(pos, sm->assoc_resp_ftie,
				  2 + sm->assoc_resp_ftie[1]);
			res = 2 + sm->assoc_resp_ftie[1];
		} else {
			int use_sha384 = wpa_key_mgmt_sha384(sm->wpa_key_mgmt);

			res = wpa_write_ftie(conf, use_sha384,
					     conf->r0_key_holder,
					     conf->r0_key_holder_len,
					     NULL, NULL, pos,
					     kde + kde_len - pos,
					     NULL, 0);
		}
		if (res < 0) {
			wpa_printf(MSG_ERROR, "FT: Failed to insert FTIE "
				   "into EAPOL-Key Key Data");
			os_free(kde);
			return;
		}
		pos += res;

		/* TIE[ReassociationDeadline] (TU) */
		*pos++ = WLAN_EID_TIMEOUT_INTERVAL;
		*pos++ = 5;
		*pos++ = WLAN_TIMEOUT_REASSOC_DEADLINE;
		WPA_PUT_LE32(pos, conf->reassociation_deadline);
		pos += 4;

		/* TIE[KeyLifetime] (seconds) */
		*pos++ = WLAN_EID_TIMEOUT_INTERVAL;
		*pos++ = 5;
		*pos++ = WLAN_TIMEOUT_KEY_LIFETIME;
		WPA_PUT_LE32(pos, conf->r0_key_lifetime);
		pos += 4;
	}
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_P2P
	if (WPA_GET_BE32(sm->ip_addr) > 0) {
		u8 addr[3 * 4];
		os_memcpy(addr, sm->ip_addr, 4);
		os_memcpy(addr + 4, sm->wpa_auth->conf.ip_addr_mask, 4);
		os_memcpy(addr + 8, sm->wpa_auth->conf.ip_addr_go, 4);
		pos = wpa_add_kde(pos, WFA_KEY_DATA_IP_ADDR_ALLOC,
				  addr, sizeof(addr), NULL, 0);
	}
#endif /* CONFIG_P2P */

	wpa_send_eapol(sm->wpa_auth, sm,
		       (secure ? WPA_KEY_INFO_SECURE : 0) |
		       (wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len) ?
			WPA_KEY_INFO_MIC : 0) |
		       WPA_KEY_INFO_ACK | WPA_KEY_INFO_INSTALL |
		       WPA_KEY_INFO_KEY_TYPE,
		       _rsc, sm->ANonce, kde, pos - kde, keyidx, encr);
	os_free(kde);
}


SM_STATE(WPA_PTK, PTKINITDONE)
{
	SM_ENTRY_MA(WPA_PTK, PTKINITDONE, wpa_ptk);
	sm->EAPOLKeyReceived = FALSE;
	if (sm->Pair) {
		enum wpa_alg alg = wpa_cipher_to_alg(sm->pairwise);
		int klen = wpa_cipher_key_len(sm->pairwise);
		if (wpa_auth_set_key(sm->wpa_auth, 0, alg, sm->addr, 0,
				     sm->PTK.tk, klen)) {
			wpa_sta_disconnect(sm->wpa_auth, sm->addr,
					   WLAN_REASON_PREV_AUTH_NOT_VALID);
			return;
		}
		/* FIX: MLME-SetProtection.Request(TA, Tx_Rx) */
		sm->pairwise_set = TRUE;

		if (sm->wpa_auth->conf.wpa_ptk_rekey) {
			eloop_cancel_timeout(wpa_rekey_ptk, sm->wpa_auth, sm);
			eloop_register_timeout(sm->wpa_auth->conf.
					       wpa_ptk_rekey, 0, wpa_rekey_ptk,
					       sm->wpa_auth, sm);
		}

		if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) ||
		    sm->wpa_key_mgmt == WPA_KEY_MGMT_DPP ||
		    sm->wpa_key_mgmt == WPA_KEY_MGMT_OWE) {
			wpa_auth_set_eapol(sm->wpa_auth, sm->addr,
					   WPA_EAPOL_authorized, 1);
		}
	}

	if (0 /* IBSS == TRUE */) {
		sm->keycount++;
		if (sm->keycount == 2) {
			wpa_auth_set_eapol(sm->wpa_auth, sm->addr,
					   WPA_EAPOL_portValid, 1);
		}
	} else {
		wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_portValid,
				   1);
	}
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_keyAvailable, 0);
	wpa_auth_set_eapol(sm->wpa_auth, sm->addr, WPA_EAPOL_keyDone, 1);
	if (sm->wpa == WPA_VERSION_WPA)
		sm->PInitAKeys = TRUE;
	else
		sm->has_GTK = TRUE;
	wpa_auth_vlogger(sm->wpa_auth, sm->addr, LOGGER_INFO,
			 "pairwise key handshake completed (%s)",
			 sm->wpa == WPA_VERSION_WPA ? "WPA" : "RSN");

#ifdef CONFIG_IEEE80211R_AP
	wpa_ft_push_pmk_r1(sm->wpa_auth, sm->addr);
#endif /* CONFIG_IEEE80211R_AP */
}


SM_STEP(WPA_PTK)
{
	struct wpa_authenticator *wpa_auth = sm->wpa_auth;

	if (sm->Init)
		SM_ENTER(WPA_PTK, INITIALIZE);
	else if (sm->Disconnect
		 /* || FIX: dot11RSNAConfigSALifetime timeout */) {
		wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
				"WPA_PTK: sm->Disconnect");
		SM_ENTER(WPA_PTK, DISCONNECT);
	}
	else if (sm->DeauthenticationRequest)
		SM_ENTER(WPA_PTK, DISCONNECTED);
	else if (sm->AuthenticationRequest)
		SM_ENTER(WPA_PTK, AUTHENTICATION);
	else if (sm->ReAuthenticationRequest)
		SM_ENTER(WPA_PTK, AUTHENTICATION2);
	else if (sm->PTKRequest) {
		if (wpa_auth_sm_ptk_update(sm) < 0)
			SM_ENTER(WPA_PTK, DISCONNECTED);
		else
			SM_ENTER(WPA_PTK, PTKSTART);
	} else switch (sm->wpa_ptk_state) {
	case WPA_PTK_INITIALIZE:
		break;
	case WPA_PTK_DISCONNECT:
		SM_ENTER(WPA_PTK, DISCONNECTED);
		break;
	case WPA_PTK_DISCONNECTED:
		SM_ENTER(WPA_PTK, INITIALIZE);
		break;
	case WPA_PTK_AUTHENTICATION:
		SM_ENTER(WPA_PTK, AUTHENTICATION2);
		break;
	case WPA_PTK_AUTHENTICATION2:
		if (wpa_key_mgmt_wpa_ieee8021x(sm->wpa_key_mgmt) &&
		    wpa_auth_get_eapol(sm->wpa_auth, sm->addr,
				       WPA_EAPOL_keyRun) > 0)
			SM_ENTER(WPA_PTK, INITPMK);
		else if (wpa_key_mgmt_wpa_psk(sm->wpa_key_mgmt) ||
			 sm->wpa_key_mgmt == WPA_KEY_MGMT_OWE
			 /* FIX: && 802.1X::keyRun */)
			SM_ENTER(WPA_PTK, INITPSK);
		else if (sm->wpa_key_mgmt == WPA_KEY_MGMT_DPP)
			SM_ENTER(WPA_PTK, INITPMK);
		break;
	case WPA_PTK_INITPMK:
		if (wpa_auth_get_eapol(sm->wpa_auth, sm->addr,
				       WPA_EAPOL_keyAvailable) > 0) {
			SM_ENTER(WPA_PTK, PTKSTART);
#ifdef CONFIG_DPP
		} else if (sm->wpa_key_mgmt == WPA_KEY_MGMT_DPP && sm->pmksa) {
			SM_ENTER(WPA_PTK, PTKSTART);
#endif /* CONFIG_DPP */
		} else {
			wpa_auth->dot11RSNA4WayHandshakeFailures++;
			wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_INFO,
					"INITPMK - keyAvailable = false");
			SM_ENTER(WPA_PTK, DISCONNECT);
		}
		break;
	case WPA_PTK_INITPSK:
		if (wpa_auth_get_psk(sm->wpa_auth, sm->addr, sm->p2p_dev_addr,
				     NULL, NULL)) {
			SM_ENTER(WPA_PTK, PTKSTART);
#ifdef CONFIG_SAE
		} else if (wpa_auth_uses_sae(sm) && sm->pmksa) {
			SM_ENTER(WPA_PTK, PTKSTART);
#endif /* CONFIG_SAE */
		} else {
			wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_INFO,
					"no PSK configured for the STA");
			wpa_auth->dot11RSNA4WayHandshakeFailures++;
			SM_ENTER(WPA_PTK, DISCONNECT);
		}
		break;
	case WPA_PTK_PTKSTART:
		if (sm->EAPOLKeyReceived && !sm->EAPOLKeyRequest &&
		    sm->EAPOLKeyPairwise)
			SM_ENTER(WPA_PTK, PTKCALCNEGOTIATING);
		else if (sm->TimeoutCtr >
			 sm->wpa_auth->conf.wpa_pairwise_update_count) {
			wpa_auth->dot11RSNA4WayHandshakeFailures++;
			wpa_auth_vlogger(
				sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"PTKSTART: Retry limit %u reached",
				sm->wpa_auth->conf.wpa_pairwise_update_count);
			SM_ENTER(WPA_PTK, DISCONNECT);
		} else if (sm->TimeoutEvt)
			SM_ENTER(WPA_PTK, PTKSTART);
		break;
	case WPA_PTK_PTKCALCNEGOTIATING:
		if (sm->MICVerified)
			SM_ENTER(WPA_PTK, PTKCALCNEGOTIATING2);
		else if (sm->EAPOLKeyReceived && !sm->EAPOLKeyRequest &&
			 sm->EAPOLKeyPairwise)
			SM_ENTER(WPA_PTK, PTKCALCNEGOTIATING);
		else if (sm->TimeoutEvt)
			SM_ENTER(WPA_PTK, PTKSTART);
		break;
	case WPA_PTK_PTKCALCNEGOTIATING2:
		SM_ENTER(WPA_PTK, PTKINITNEGOTIATING);
		break;
	case WPA_PTK_PTKINITNEGOTIATING:
		if (sm->update_snonce)
			SM_ENTER(WPA_PTK, PTKCALCNEGOTIATING);
		else if (sm->EAPOLKeyReceived && !sm->EAPOLKeyRequest &&
			 sm->EAPOLKeyPairwise && sm->MICVerified)
			SM_ENTER(WPA_PTK, PTKINITDONE);
		else if (sm->TimeoutCtr >
			 sm->wpa_auth->conf.wpa_pairwise_update_count ||
			 (sm->wpa_auth->conf.wpa_disable_eapol_key_retries &&
			  sm->TimeoutCtr > 1)) {
			wpa_auth->dot11RSNA4WayHandshakeFailures++;
			wpa_auth_vlogger(
				sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"PTKINITNEGOTIATING: Retry limit %u reached",
				sm->wpa_auth->conf.wpa_pairwise_update_count);
			SM_ENTER(WPA_PTK, DISCONNECT);
		} else if (sm->TimeoutEvt)
			SM_ENTER(WPA_PTK, PTKINITNEGOTIATING);
		break;
	case WPA_PTK_PTKINITDONE:
		break;
	}
}


SM_STATE(WPA_PTK_GROUP, IDLE)
{
	SM_ENTRY_MA(WPA_PTK_GROUP, IDLE, wpa_ptk_group);
	if (sm->Init) {
		/* Init flag is not cleared here, so avoid busy
		 * loop by claiming nothing changed. */
		sm->changed = FALSE;
	}
	sm->GTimeoutCtr = 0;
}


SM_STATE(WPA_PTK_GROUP, REKEYNEGOTIATING)
{
	u8 rsc[WPA_KEY_RSC_LEN];
	struct wpa_group *gsm = sm->group;
	const u8 *kde;
	u8 *kde_buf = NULL, *pos, hdr[2];
	size_t kde_len;
	u8 *gtk, dummy_gtk[32];

	SM_ENTRY_MA(WPA_PTK_GROUP, REKEYNEGOTIATING, wpa_ptk_group);

	sm->GTimeoutCtr++;
	if (sm->wpa_auth->conf.wpa_disable_eapol_key_retries &&
	    sm->GTimeoutCtr > 1) {
		/* Do not allow retransmission of EAPOL-Key group msg 1/2 */
		return;
	}
	if (sm->GTimeoutCtr > sm->wpa_auth->conf.wpa_group_update_count) {
		/* No point in sending the EAPOL-Key - we will disconnect
		 * immediately following this. */
		return;
	}

	if (sm->wpa == WPA_VERSION_WPA)
		sm->PInitAKeys = FALSE;
	sm->TimeoutEvt = FALSE;
	/* Send EAPOL(1, 1, 1, !Pair, G, RSC, GNonce, MIC(PTK), GTK[GN]) */
	os_memset(rsc, 0, WPA_KEY_RSC_LEN);
	if (gsm->wpa_group_state == WPA_GROUP_SETKEYSDONE)
		wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN, rsc);
	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 1/2 msg of Group Key Handshake");

	gtk = gsm->GTK[gsm->GN - 1];
	if (sm->wpa_auth->conf.disable_gtk ||
	    sm->wpa_key_mgmt == WPA_KEY_MGMT_OSEN) {
		/*
		 * Provide unique random GTK to each STA to prevent use
		 * of GTK in the BSS.
		 */
		if (random_get_bytes(dummy_gtk, gsm->GTK_len) < 0)
			return;
		gtk = dummy_gtk;
	}
	if (sm->wpa == WPA_VERSION_WPA2) {
		kde_len = 2 + RSN_SELECTOR_LEN + 2 + gsm->GTK_len +
			ieee80211w_kde_len(sm);
		kde_buf = os_malloc(kde_len);
		if (kde_buf == NULL)
			return;

		kde = pos = kde_buf;
		hdr[0] = gsm->GN & 0x03;
		hdr[1] = 0;
		pos = wpa_add_kde(pos, RSN_KEY_DATA_GROUPKEY, hdr, 2,
				  gtk, gsm->GTK_len);
		pos = ieee80211w_kde_add(sm, pos);
		kde_len = pos - kde;
	} else {
		kde = gtk;
		kde_len = gsm->GTK_len;
	}

	wpa_send_eapol(sm->wpa_auth, sm,
		       WPA_KEY_INFO_SECURE |
		       (wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len) ?
			WPA_KEY_INFO_MIC : 0) |
		       WPA_KEY_INFO_ACK |
		       (!sm->Pair ? WPA_KEY_INFO_INSTALL : 0),
		       rsc, NULL, kde, kde_len, gsm->GN, 1);

	os_free(kde_buf);
}


SM_STATE(WPA_PTK_GROUP, REKEYESTABLISHED)
{
	SM_ENTRY_MA(WPA_PTK_GROUP, REKEYESTABLISHED, wpa_ptk_group);
	sm->EAPOLKeyReceived = FALSE;
	if (sm->GUpdateStationKeys)
		sm->group->GKeyDoneStations--;
	sm->GUpdateStationKeys = FALSE;
	sm->GTimeoutCtr = 0;
	/* FIX: MLME.SetProtection.Request(TA, Tx_Rx) */
	wpa_auth_vlogger(sm->wpa_auth, sm->addr, LOGGER_INFO,
			 "group key handshake completed (%s)",
			 sm->wpa == WPA_VERSION_WPA ? "WPA" : "RSN");
	sm->has_GTK = TRUE;
}


SM_STATE(WPA_PTK_GROUP, KEYERROR)
{
	SM_ENTRY_MA(WPA_PTK_GROUP, KEYERROR, wpa_ptk_group);
	if (sm->GUpdateStationKeys)
		sm->group->GKeyDoneStations--;
	sm->GUpdateStationKeys = FALSE;
	sm->Disconnect = TRUE;
	wpa_auth_vlogger(sm->wpa_auth, sm->addr, LOGGER_INFO,
			 "group key handshake failed (%s) after %u tries",
			 sm->wpa == WPA_VERSION_WPA ? "WPA" : "RSN",
			 sm->wpa_auth->conf.wpa_group_update_count);
}


SM_STEP(WPA_PTK_GROUP)
{
	if (sm->Init || sm->PtkGroupInit) {
		SM_ENTER(WPA_PTK_GROUP, IDLE);
		sm->PtkGroupInit = FALSE;
	} else switch (sm->wpa_ptk_group_state) {
	case WPA_PTK_GROUP_IDLE:
		if (sm->GUpdateStationKeys ||
		    (sm->wpa == WPA_VERSION_WPA && sm->PInitAKeys))
			SM_ENTER(WPA_PTK_GROUP, REKEYNEGOTIATING);
		break;
	case WPA_PTK_GROUP_REKEYNEGOTIATING:
		if (sm->EAPOLKeyReceived && !sm->EAPOLKeyRequest &&
		    !sm->EAPOLKeyPairwise && sm->MICVerified)
			SM_ENTER(WPA_PTK_GROUP, REKEYESTABLISHED);
		else if (sm->GTimeoutCtr >
			 sm->wpa_auth->conf.wpa_group_update_count ||
			 (sm->wpa_auth->conf.wpa_disable_eapol_key_retries &&
			  sm->GTimeoutCtr > 1))
			SM_ENTER(WPA_PTK_GROUP, KEYERROR);
		else if (sm->TimeoutEvt)
			SM_ENTER(WPA_PTK_GROUP, REKEYNEGOTIATING);
		break;
	case WPA_PTK_GROUP_KEYERROR:
		SM_ENTER(WPA_PTK_GROUP, IDLE);
		break;
	case WPA_PTK_GROUP_REKEYESTABLISHED:
		SM_ENTER(WPA_PTK_GROUP, IDLE);
		break;
	}
}


static int wpa_gtk_update(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group)
{
	int ret = 0;

	os_memcpy(group->GNonce, group->Counter, WPA_NONCE_LEN);
	inc_byte_array(group->Counter, WPA_NONCE_LEN);
	if (wpa_gmk_to_gtk(group->GMK, "Group key expansion",
			   wpa_auth->addr, group->GNonce,
			   group->GTK[group->GN - 1], group->GTK_len) < 0)
		ret = -1;
	wpa_hexdump_key(MSG_DEBUG, "GTK",
			group->GTK[group->GN - 1], group->GTK_len);

#ifdef CONFIG_IEEE80211W
	if (wpa_auth->conf.ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		size_t len;
		len = wpa_cipher_key_len(wpa_auth->conf.group_mgmt_cipher);
		os_memcpy(group->GNonce, group->Counter, WPA_NONCE_LEN);
		inc_byte_array(group->Counter, WPA_NONCE_LEN);
		if (wpa_gmk_to_gtk(group->GMK, "IGTK key expansion",
				   wpa_auth->addr, group->GNonce,
				   group->IGTK[group->GN_igtk - 4], len) < 0)
			ret = -1;
		wpa_hexdump_key(MSG_DEBUG, "IGTK",
				group->IGTK[group->GN_igtk - 4], len);
	}
#endif /* CONFIG_IEEE80211W */

	return ret;
}


static void wpa_group_gtk_init(struct wpa_authenticator *wpa_auth,
			       struct wpa_group *group)
{
	wpa_printf(MSG_DEBUG, "WPA: group state machine entering state "
		   "GTK_INIT (VLAN-ID %d)", group->vlan_id);
	group->changed = FALSE; /* GInit is not cleared here; avoid loop */
	group->wpa_group_state = WPA_GROUP_GTK_INIT;

	/* GTK[0..N] = 0 */
	os_memset(group->GTK, 0, sizeof(group->GTK));
	group->GN = 1;
	group->GM = 2;
#ifdef CONFIG_IEEE80211W
	group->GN_igtk = 4;
	group->GM_igtk = 5;
#endif /* CONFIG_IEEE80211W */
	/* GTK[GN] = CalcGTK() */
	wpa_gtk_update(wpa_auth, group);
}


static int wpa_group_update_sta(struct wpa_state_machine *sm, void *ctx)
{
	if (ctx != NULL && ctx != sm->group)
		return 0;

	if (sm->wpa_ptk_state != WPA_PTK_PTKINITDONE) {
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"Not in PTKINITDONE; skip Group Key update");
		sm->GUpdateStationKeys = FALSE;
		return 0;
	}
	if (sm->GUpdateStationKeys) {
		/*
		 * This should not really happen, so add a debug log entry.
		 * Since we clear the GKeyDoneStations before the loop, the
		 * station needs to be counted here anyway.
		 */
		wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
				"GUpdateStationKeys was already set when "
				"marking station for GTK rekeying");
	}

	/* Do not rekey GTK/IGTK when STA is in WNM-Sleep Mode */
	if (sm->is_wnmsleep)
		return 0;

	sm->group->GKeyDoneStations++;
	sm->GUpdateStationKeys = TRUE;

	wpa_sm_step(sm);
	return 0;
}


#ifdef CONFIG_WNM_AP
/* update GTK when exiting WNM-Sleep Mode */
void wpa_wnmsleep_rekey_gtk(struct wpa_state_machine *sm)
{
	if (sm == NULL || sm->is_wnmsleep)
		return;

	wpa_group_update_sta(sm, NULL);
}


void wpa_set_wnmsleep(struct wpa_state_machine *sm, int flag)
{
	if (sm)
		sm->is_wnmsleep = !!flag;
}


int wpa_wnmsleep_gtk_subelem(struct wpa_state_machine *sm, u8 *pos)
{
	struct wpa_group *gsm = sm->group;
	u8 *start = pos;

	/*
	 * GTK subelement:
	 * Sub-elem ID[1] | Length[1] | Key Info[2] | Key Length[1] | RSC[8] |
	 * Key[5..32]
	 */
	*pos++ = WNM_SLEEP_SUBELEM_GTK;
	*pos++ = 11 + gsm->GTK_len;
	/* Key ID in B0-B1 of Key Info */
	WPA_PUT_LE16(pos, gsm->GN & 0x03);
	pos += 2;
	*pos++ = gsm->GTK_len;
	if (wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN, pos) != 0)
		return 0;
	pos += 8;
	os_memcpy(pos, gsm->GTK[gsm->GN - 1], gsm->GTK_len);
	pos += gsm->GTK_len;

	wpa_printf(MSG_DEBUG, "WNM: GTK Key ID %u in WNM-Sleep Mode exit",
		   gsm->GN);
	wpa_hexdump_key(MSG_DEBUG, "WNM: GTK in WNM-Sleep Mode exit",
			gsm->GTK[gsm->GN - 1], gsm->GTK_len);

	return pos - start;
}


#ifdef CONFIG_IEEE80211W
int wpa_wnmsleep_igtk_subelem(struct wpa_state_machine *sm, u8 *pos)
{
	struct wpa_group *gsm = sm->group;
	u8 *start = pos;
	size_t len = wpa_cipher_key_len(sm->wpa_auth->conf.group_mgmt_cipher);

	/*
	 * IGTK subelement:
	 * Sub-elem ID[1] | Length[1] | KeyID[2] | PN[6] | Key[16]
	 */
	*pos++ = WNM_SLEEP_SUBELEM_IGTK;
	*pos++ = 2 + 6 + len;
	WPA_PUT_LE16(pos, gsm->GN_igtk);
	pos += 2;
	if (wpa_auth_get_seqnum(sm->wpa_auth, NULL, gsm->GN_igtk, pos) != 0)
		return 0;
	pos += 6;

	os_memcpy(pos, gsm->IGTK[gsm->GN_igtk - 4], len);
	pos += len;

	wpa_printf(MSG_DEBUG, "WNM: IGTK Key ID %u in WNM-Sleep Mode exit",
		   gsm->GN_igtk);
	wpa_hexdump_key(MSG_DEBUG, "WNM: IGTK in WNM-Sleep Mode exit",
			gsm->IGTK[gsm->GN_igtk - 4], len);

	return pos - start;
}
#endif /* CONFIG_IEEE80211W */
#endif /* CONFIG_WNM_AP */


static void wpa_group_setkeys(struct wpa_authenticator *wpa_auth,
			      struct wpa_group *group)
{
	int tmp;

	wpa_printf(MSG_DEBUG, "WPA: group state machine entering state "
		   "SETKEYS (VLAN-ID %d)", group->vlan_id);
	group->changed = TRUE;
	group->wpa_group_state = WPA_GROUP_SETKEYS;
	group->GTKReKey = FALSE;
	tmp = group->GM;
	group->GM = group->GN;
	group->GN = tmp;
#ifdef CONFIG_IEEE80211W
	tmp = group->GM_igtk;
	group->GM_igtk = group->GN_igtk;
	group->GN_igtk = tmp;
#endif /* CONFIG_IEEE80211W */
	/* "GKeyDoneStations = GNoStations" is done in more robust way by
	 * counting the STAs that are marked with GUpdateStationKeys instead of
	 * including all STAs that could be in not-yet-completed state. */
	wpa_gtk_update(wpa_auth, group);

	if (group->GKeyDoneStations) {
		wpa_printf(MSG_DEBUG, "wpa_group_setkeys: Unexpected "
			   "GKeyDoneStations=%d when starting new GTK rekey",
			   group->GKeyDoneStations);
		group->GKeyDoneStations = 0;
	}
	wpa_auth_for_each_sta(wpa_auth, wpa_group_update_sta, group);
	wpa_printf(MSG_DEBUG, "wpa_group_setkeys: GKeyDoneStations=%d",
		   group->GKeyDoneStations);
}


static int wpa_group_config_group_keys(struct wpa_authenticator *wpa_auth,
				       struct wpa_group *group)
{
	int ret = 0;

	if (wpa_auth_set_key(wpa_auth, group->vlan_id,
			     wpa_cipher_to_alg(wpa_auth->conf.wpa_group),
			     broadcast_ether_addr, group->GN,
			     group->GTK[group->GN - 1], group->GTK_len) < 0)
		ret = -1;

#ifdef CONFIG_IEEE80211W
	if (wpa_auth->conf.ieee80211w != NO_MGMT_FRAME_PROTECTION) {
		enum wpa_alg alg;
		size_t len;

		alg = wpa_cipher_to_alg(wpa_auth->conf.group_mgmt_cipher);
		len = wpa_cipher_key_len(wpa_auth->conf.group_mgmt_cipher);

		if (ret == 0 &&
		    wpa_auth_set_key(wpa_auth, group->vlan_id, alg,
				     broadcast_ether_addr, group->GN_igtk,
				     group->IGTK[group->GN_igtk - 4], len) < 0)
			ret = -1;
	}
#endif /* CONFIG_IEEE80211W */

	return ret;
}


static int wpa_group_disconnect_cb(struct wpa_state_machine *sm, void *ctx)
{
	if (sm->group == ctx) {
		wpa_printf(MSG_DEBUG, "WPA: Mark STA " MACSTR
			   " for discconnection due to fatal failure",
			   MAC2STR(sm->addr));
		sm->Disconnect = TRUE;
	}

	return 0;
}


static void wpa_group_fatal_failure(struct wpa_authenticator *wpa_auth,
				    struct wpa_group *group)
{
	wpa_printf(MSG_DEBUG, "WPA: group state machine entering state FATAL_FAILURE");
	group->changed = TRUE;
	group->wpa_group_state = WPA_GROUP_FATAL_FAILURE;
	wpa_auth_for_each_sta(wpa_auth, wpa_group_disconnect_cb, group);
}


static int wpa_group_setkeysdone(struct wpa_authenticator *wpa_auth,
				 struct wpa_group *group)
{
	wpa_printf(MSG_DEBUG, "WPA: group state machine entering state "
		   "SETKEYSDONE (VLAN-ID %d)", group->vlan_id);
	group->changed = TRUE;
	group->wpa_group_state = WPA_GROUP_SETKEYSDONE;

	if (wpa_group_config_group_keys(wpa_auth, group) < 0) {
		wpa_group_fatal_failure(wpa_auth, group);
		return -1;
	}

	return 0;
}


static void wpa_group_sm_step(struct wpa_authenticator *wpa_auth,
			      struct wpa_group *group)
{
	if (group->GInit) {
		wpa_group_gtk_init(wpa_auth, group);
	} else if (group->wpa_group_state == WPA_GROUP_FATAL_FAILURE) {
		/* Do not allow group operations */
	} else if (group->wpa_group_state == WPA_GROUP_GTK_INIT &&
		   group->GTKAuthenticator) {
		wpa_group_setkeysdone(wpa_auth, group);
	} else if (group->wpa_group_state == WPA_GROUP_SETKEYSDONE &&
		   group->GTKReKey) {
		wpa_group_setkeys(wpa_auth, group);
	} else if (group->wpa_group_state == WPA_GROUP_SETKEYS) {
		if (group->GKeyDoneStations == 0)
			wpa_group_setkeysdone(wpa_auth, group);
		else if (group->GTKReKey)
			wpa_group_setkeys(wpa_auth, group);
	}
}


static int wpa_sm_step(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return 0;

	if (sm->in_step_loop) {
		/* This should not happen, but if it does, make sure we do not
		 * end up freeing the state machine too early by exiting the
		 * recursive call. */
		wpa_printf(MSG_ERROR, "WPA: wpa_sm_step() called recursively");
		return 0;
	}

	sm->in_step_loop = 1;
	do {
		if (sm->pending_deinit)
			break;

		sm->changed = FALSE;
		sm->wpa_auth->group->changed = FALSE;

		SM_STEP_RUN(WPA_PTK);
		if (sm->pending_deinit)
			break;
		SM_STEP_RUN(WPA_PTK_GROUP);
		if (sm->pending_deinit)
			break;
		wpa_group_sm_step(sm->wpa_auth, sm->group);
	} while (sm->changed || sm->wpa_auth->group->changed);
	sm->in_step_loop = 0;

	if (sm->pending_deinit) {
		wpa_printf(MSG_DEBUG, "WPA: Completing pending STA state "
			   "machine deinit for " MACSTR, MAC2STR(sm->addr));
		wpa_free_sta_sm(sm);
		return 1;
	}
	return 0;
}


static void wpa_sm_call_step(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_state_machine *sm = eloop_ctx;
	wpa_sm_step(sm);
}


void wpa_auth_sm_notify(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return;
	eloop_register_timeout(0, 0, wpa_sm_call_step, sm, NULL);
}


void wpa_gtk_rekey(struct wpa_authenticator *wpa_auth)
{
	int tmp, i;
	struct wpa_group *group;

	if (wpa_auth == NULL)
		return;

	group = wpa_auth->group;

	for (i = 0; i < 2; i++) {
		tmp = group->GM;
		group->GM = group->GN;
		group->GN = tmp;
#ifdef CONFIG_IEEE80211W
		tmp = group->GM_igtk;
		group->GM_igtk = group->GN_igtk;
		group->GN_igtk = tmp;
#endif /* CONFIG_IEEE80211W */
		wpa_gtk_update(wpa_auth, group);
		wpa_group_config_group_keys(wpa_auth, group);
	}
}


static const char * wpa_bool_txt(int val)
{
	return val ? "TRUE" : "FALSE";
}


#define RSN_SUITE "%02x-%02x-%02x-%d"
#define RSN_SUITE_ARG(s) \
((s) >> 24) & 0xff, ((s) >> 16) & 0xff, ((s) >> 8) & 0xff, (s) & 0xff

int wpa_get_mib(struct wpa_authenticator *wpa_auth, char *buf, size_t buflen)
{
	int len = 0, ret;
	char pmkid_txt[PMKID_LEN * 2 + 1];
#ifdef CONFIG_RSN_PREAUTH
	const int preauth = 1;
#else /* CONFIG_RSN_PREAUTH */
	const int preauth = 0;
#endif /* CONFIG_RSN_PREAUTH */

	if (wpa_auth == NULL)
		return len;

	ret = os_snprintf(buf + len, buflen - len,
			  "dot11RSNAOptionImplemented=TRUE\n"
			  "dot11RSNAPreauthenticationImplemented=%s\n"
			  "dot11RSNAEnabled=%s\n"
			  "dot11RSNAPreauthenticationEnabled=%s\n",
			  wpa_bool_txt(preauth),
			  wpa_bool_txt(wpa_auth->conf.wpa & WPA_PROTO_RSN),
			  wpa_bool_txt(wpa_auth->conf.rsn_preauth));
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	wpa_snprintf_hex(pmkid_txt, sizeof(pmkid_txt),
			 wpa_auth->dot11RSNAPMKIDUsed, PMKID_LEN);

	ret = os_snprintf(
		buf + len, buflen - len,
		"dot11RSNAConfigVersion=%u\n"
		"dot11RSNAConfigPairwiseKeysSupported=9999\n"
		/* FIX: dot11RSNAConfigGroupCipher */
		/* FIX: dot11RSNAConfigGroupRekeyMethod */
		/* FIX: dot11RSNAConfigGroupRekeyTime */
		/* FIX: dot11RSNAConfigGroupRekeyPackets */
		"dot11RSNAConfigGroupRekeyStrict=%u\n"
		"dot11RSNAConfigGroupUpdateCount=%u\n"
		"dot11RSNAConfigPairwiseUpdateCount=%u\n"
		"dot11RSNAConfigGroupCipherSize=%u\n"
		"dot11RSNAConfigPMKLifetime=%u\n"
		"dot11RSNAConfigPMKReauthThreshold=%u\n"
		"dot11RSNAConfigNumberOfPTKSAReplayCounters=0\n"
		"dot11RSNAConfigSATimeout=%u\n"
		"dot11RSNAAuthenticationSuiteSelected=" RSN_SUITE "\n"
		"dot11RSNAPairwiseCipherSelected=" RSN_SUITE "\n"
		"dot11RSNAGroupCipherSelected=" RSN_SUITE "\n"
		"dot11RSNAPMKIDUsed=%s\n"
		"dot11RSNAAuthenticationSuiteRequested=" RSN_SUITE "\n"
		"dot11RSNAPairwiseCipherRequested=" RSN_SUITE "\n"
		"dot11RSNAGroupCipherRequested=" RSN_SUITE "\n"
		"dot11RSNATKIPCounterMeasuresInvoked=%u\n"
		"dot11RSNA4WayHandshakeFailures=%u\n"
		"dot11RSNAConfigNumberOfGTKSAReplayCounters=0\n",
		RSN_VERSION,
		!!wpa_auth->conf.wpa_strict_rekey,
		wpa_auth->conf.wpa_group_update_count,
		wpa_auth->conf.wpa_pairwise_update_count,
		wpa_cipher_key_len(wpa_auth->conf.wpa_group) * 8,
		dot11RSNAConfigPMKLifetime,
		dot11RSNAConfigPMKReauthThreshold,
		dot11RSNAConfigSATimeout,
		RSN_SUITE_ARG(wpa_auth->dot11RSNAAuthenticationSuiteSelected),
		RSN_SUITE_ARG(wpa_auth->dot11RSNAPairwiseCipherSelected),
		RSN_SUITE_ARG(wpa_auth->dot11RSNAGroupCipherSelected),
		pmkid_txt,
		RSN_SUITE_ARG(wpa_auth->dot11RSNAAuthenticationSuiteRequested),
		RSN_SUITE_ARG(wpa_auth->dot11RSNAPairwiseCipherRequested),
		RSN_SUITE_ARG(wpa_auth->dot11RSNAGroupCipherRequested),
		wpa_auth->dot11RSNATKIPCounterMeasuresInvoked,
		wpa_auth->dot11RSNA4WayHandshakeFailures);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* TODO: dot11RSNAConfigPairwiseCiphersTable */
	/* TODO: dot11RSNAConfigAuthenticationSuitesTable */

	/* Private MIB */
	ret = os_snprintf(buf + len, buflen - len, "hostapdWPAGroupState=%d\n",
			  wpa_auth->group->wpa_group_state);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	return len;
}


int wpa_get_mib_sta(struct wpa_state_machine *sm, char *buf, size_t buflen)
{
	int len = 0, ret;
	u32 pairwise = 0;

	if (sm == NULL)
		return 0;

	/* TODO: FF-FF-FF-FF-FF-FF entry for broadcast/multicast stats */

	/* dot11RSNAStatsEntry */

	pairwise = wpa_cipher_to_suite(sm->wpa == WPA_VERSION_WPA2 ?
				       WPA_PROTO_RSN : WPA_PROTO_WPA,
				       sm->pairwise);
	if (pairwise == 0)
		return 0;

	ret = os_snprintf(
		buf + len, buflen - len,
		/* TODO: dot11RSNAStatsIndex */
		"dot11RSNAStatsSTAAddress=" MACSTR "\n"
		"dot11RSNAStatsVersion=1\n"
		"dot11RSNAStatsSelectedPairwiseCipher=" RSN_SUITE "\n"
		/* TODO: dot11RSNAStatsTKIPICVErrors */
		"dot11RSNAStatsTKIPLocalMICFailures=%u\n"
		"dot11RSNAStatsTKIPRemoteMICFailures=%u\n"
		/* TODO: dot11RSNAStatsCCMPReplays */
		/* TODO: dot11RSNAStatsCCMPDecryptErrors */
		/* TODO: dot11RSNAStatsTKIPReplays */,
		MAC2STR(sm->addr),
		RSN_SUITE_ARG(pairwise),
		sm->dot11RSNAStatsTKIPLocalMICFailures,
		sm->dot11RSNAStatsTKIPRemoteMICFailures);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	/* Private MIB */
	ret = os_snprintf(buf + len, buflen - len,
			  "hostapdWPAPTKState=%d\n"
			  "hostapdWPAPTKGroupState=%d\n",
			  sm->wpa_ptk_state,
			  sm->wpa_ptk_group_state);
	if (os_snprintf_error(buflen - len, ret))
		return len;
	len += ret;

	return len;
}


void wpa_auth_countermeasures_start(struct wpa_authenticator *wpa_auth)
{
	if (wpa_auth)
		wpa_auth->dot11RSNATKIPCounterMeasuresInvoked++;
}


int wpa_auth_pairwise_set(struct wpa_state_machine *sm)
{
	return sm && sm->pairwise_set;
}


int wpa_auth_get_pairwise(struct wpa_state_machine *sm)
{
	return sm->pairwise;
}


int wpa_auth_sta_key_mgmt(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return -1;
	return sm->wpa_key_mgmt;
}


int wpa_auth_sta_wpa_version(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return 0;
	return sm->wpa;
}


int wpa_auth_sta_ft_tk_already_set(struct wpa_state_machine *sm)
{
	if (!sm || !wpa_key_mgmt_ft(sm->wpa_key_mgmt))
		return 0;
	return sm->tk_already_set;
}


int wpa_auth_sta_fils_tk_already_set(struct wpa_state_machine *sm)
{
	if (!sm || !wpa_key_mgmt_fils(sm->wpa_key_mgmt))
		return 0;
	return sm->tk_already_set;
}


int wpa_auth_sta_clear_pmksa(struct wpa_state_machine *sm,
			     struct rsn_pmksa_cache_entry *entry)
{
	if (sm == NULL || sm->pmksa != entry)
		return -1;
	sm->pmksa = NULL;
	return 0;
}


struct rsn_pmksa_cache_entry *
wpa_auth_sta_get_pmksa(struct wpa_state_machine *sm)
{
	return sm ? sm->pmksa : NULL;
}


void wpa_auth_sta_local_mic_failure_report(struct wpa_state_machine *sm)
{
	if (sm)
		sm->dot11RSNAStatsTKIPLocalMICFailures++;
}


const u8 * wpa_auth_get_wpa_ie(struct wpa_authenticator *wpa_auth, size_t *len)
{
	if (wpa_auth == NULL)
		return NULL;
	*len = wpa_auth->wpa_ie_len;
	return wpa_auth->wpa_ie;
}


int wpa_auth_pmksa_add(struct wpa_state_machine *sm, const u8 *pmk,
		       unsigned int pmk_len,
		       int session_timeout, struct eapol_state_machine *eapol)
{
	if (sm == NULL || sm->wpa != WPA_VERSION_WPA2 ||
	    sm->wpa_auth->conf.disable_pmksa_caching)
		return -1;

	if (wpa_key_mgmt_sha384(sm->wpa_key_mgmt)) {
		if (pmk_len > PMK_LEN_SUITE_B_192)
			pmk_len = PMK_LEN_SUITE_B_192;
	} else if (pmk_len > PMK_LEN) {
		pmk_len = PMK_LEN;
	}

	if (pmksa_cache_auth_add(sm->wpa_auth->pmksa, pmk, pmk_len, NULL,
				 sm->PTK.kck, sm->PTK.kck_len,
				 sm->wpa_auth->addr, sm->addr, session_timeout,
				 eapol, sm->wpa_key_mgmt))
		return 0;

	return -1;
}


int wpa_auth_pmksa_add_preauth(struct wpa_authenticator *wpa_auth,
			       const u8 *pmk, size_t len, const u8 *sta_addr,
			       int session_timeout,
			       struct eapol_state_machine *eapol)
{
	if (wpa_auth == NULL)
		return -1;

	if (pmksa_cache_auth_add(wpa_auth->pmksa, pmk, len, NULL,
				 NULL, 0,
				 wpa_auth->addr,
				 sta_addr, session_timeout, eapol,
				 WPA_KEY_MGMT_IEEE8021X))
		return 0;

	return -1;
}


int wpa_auth_pmksa_add_sae(struct wpa_authenticator *wpa_auth, const u8 *addr,
			   const u8 *pmk, const u8 *pmkid)
{
	if (wpa_auth->conf.disable_pmksa_caching)
		return -1;

	if (pmksa_cache_auth_add(wpa_auth->pmksa, pmk, PMK_LEN, pmkid,
				 NULL, 0,
				 wpa_auth->addr, addr, 0, NULL,
				 WPA_KEY_MGMT_SAE))
		return 0;

	return -1;
}


void wpa_auth_add_sae_pmkid(struct wpa_state_machine *sm, const u8 *pmkid)
{
	os_memcpy(sm->pmkid, pmkid, PMKID_LEN);
	sm->pmkid_set = 1;
}


int wpa_auth_pmksa_add2(struct wpa_authenticator *wpa_auth, const u8 *addr,
			const u8 *pmk, size_t pmk_len, const u8 *pmkid,
			int session_timeout, int akmp)
{
	if (wpa_auth->conf.disable_pmksa_caching)
		return -1;

	if (pmksa_cache_auth_add(wpa_auth->pmksa, pmk, pmk_len, pmkid,
				 NULL, 0, wpa_auth->addr, addr, session_timeout,
				 NULL, akmp))
		return 0;

	return -1;
}


void wpa_auth_pmksa_remove(struct wpa_authenticator *wpa_auth,
			   const u8 *sta_addr)
{
	struct rsn_pmksa_cache_entry *pmksa;

	if (wpa_auth == NULL || wpa_auth->pmksa == NULL)
		return;
	pmksa = pmksa_cache_auth_get(wpa_auth->pmksa, sta_addr, NULL);
	if (pmksa) {
		wpa_printf(MSG_DEBUG, "WPA: Remove PMKSA cache entry for "
			   MACSTR " based on request", MAC2STR(sta_addr));
		pmksa_cache_free_entry(wpa_auth->pmksa, pmksa);
	}
}


int wpa_auth_pmksa_list(struct wpa_authenticator *wpa_auth, char *buf,
			size_t len)
{
	if (!wpa_auth || !wpa_auth->pmksa)
		return 0;
	return pmksa_cache_auth_list(wpa_auth->pmksa, buf, len);
}


void wpa_auth_pmksa_flush(struct wpa_authenticator *wpa_auth)
{
	if (wpa_auth && wpa_auth->pmksa)
		pmksa_cache_auth_flush(wpa_auth->pmksa);
}


#ifdef CONFIG_PMKSA_CACHE_EXTERNAL
#ifdef CONFIG_MESH

int wpa_auth_pmksa_list_mesh(struct wpa_authenticator *wpa_auth, const u8 *addr,
			     char *buf, size_t len)
{
	if (!wpa_auth || !wpa_auth->pmksa)
		return 0;

	return pmksa_cache_auth_list_mesh(wpa_auth->pmksa, addr, buf, len);
}


struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_create_entry(const u8 *aa, const u8 *spa, const u8 *pmk,
			    const u8 *pmkid, int expiration)
{
	struct rsn_pmksa_cache_entry *entry;
	struct os_reltime now;

	entry = pmksa_cache_auth_create_entry(pmk, PMK_LEN, pmkid, NULL, 0, aa,
					      spa, 0, NULL, WPA_KEY_MGMT_SAE);
	if (!entry)
		return NULL;

	os_get_reltime(&now);
	entry->expiration = now.sec + expiration;
	return entry;
}


int wpa_auth_pmksa_add_entry(struct wpa_authenticator *wpa_auth,
			     struct rsn_pmksa_cache_entry *entry)
{
	int ret;

	if (!wpa_auth || !wpa_auth->pmksa)
		return -1;

	ret = pmksa_cache_auth_add_entry(wpa_auth->pmksa, entry);
	if (ret < 0)
		wpa_printf(MSG_DEBUG,
			   "RSN: Failed to store external PMKSA cache for "
			   MACSTR, MAC2STR(entry->spa));

	return ret;
}

#endif /* CONFIG_MESH */
#endif /* CONFIG_PMKSA_CACHE_EXTERNAL */


struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_get(struct wpa_authenticator *wpa_auth, const u8 *sta_addr,
		   const u8 *pmkid)
{
	if (!wpa_auth || !wpa_auth->pmksa)
		return NULL;
	return pmksa_cache_auth_get(wpa_auth->pmksa, sta_addr, pmkid);
}


void wpa_auth_pmksa_set_to_sm(struct rsn_pmksa_cache_entry *pmksa,
			      struct wpa_state_machine *sm,
			      struct wpa_authenticator *wpa_auth,
			      u8 *pmkid, u8 *pmk)
{
	if (!sm)
		return;

	sm->pmksa = pmksa;
	os_memcpy(pmk, pmksa->pmk, PMK_LEN);
	os_memcpy(pmkid, pmksa->pmkid, PMKID_LEN);
	os_memcpy(wpa_auth->dot11RSNAPMKIDUsed, pmksa->pmkid, PMKID_LEN);
}


/*
 * Remove and free the group from wpa_authenticator. This is triggered by a
 * callback to make sure nobody is currently iterating the group list while it
 * gets modified.
 */
static void wpa_group_free(struct wpa_authenticator *wpa_auth,
			   struct wpa_group *group)
{
	struct wpa_group *prev = wpa_auth->group;

	wpa_printf(MSG_DEBUG, "WPA: Remove group state machine for VLAN-ID %d",
		   group->vlan_id);

	while (prev) {
		if (prev->next == group) {
			/* This never frees the special first group as needed */
			prev->next = group->next;
			os_free(group);
			break;
		}
		prev = prev->next;
	}

}


/* Increase the reference counter for group */
static void wpa_group_get(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group)
{
	/* Skip the special first group */
	if (wpa_auth->group == group)
		return;

	group->references++;
}


/* Decrease the reference counter and maybe free the group */
static void wpa_group_put(struct wpa_authenticator *wpa_auth,
			  struct wpa_group *group)
{
	/* Skip the special first group */
	if (wpa_auth->group == group)
		return;

	group->references--;
	if (group->references)
		return;
	wpa_group_free(wpa_auth, group);
}


/*
 * Add a group that has its references counter set to zero. Caller needs to
 * call wpa_group_get() on the return value to mark the entry in use.
 */
static struct wpa_group *
wpa_auth_add_group(struct wpa_authenticator *wpa_auth, int vlan_id)
{
	struct wpa_group *group;

	if (wpa_auth == NULL || wpa_auth->group == NULL)
		return NULL;

	wpa_printf(MSG_DEBUG, "WPA: Add group state machine for VLAN-ID %d",
		   vlan_id);
	group = wpa_group_init(wpa_auth, vlan_id, 0);
	if (group == NULL)
		return NULL;

	group->next = wpa_auth->group->next;
	wpa_auth->group->next = group;

	return group;
}


/*
 * Enforce that the group state machine for the VLAN is running, increase
 * reference counter as interface is up. References might have been increased
 * even if a negative value is returned.
 * Returns: -1 on error (group missing, group already failed); otherwise, 0
 */
int wpa_auth_ensure_group(struct wpa_authenticator *wpa_auth, int vlan_id)
{
	struct wpa_group *group;

	if (wpa_auth == NULL)
		return 0;

	group = wpa_auth->group;
	while (group) {
		if (group->vlan_id == vlan_id)
			break;
		group = group->next;
	}

	if (group == NULL) {
		group = wpa_auth_add_group(wpa_auth, vlan_id);
		if (group == NULL)
			return -1;
	}

	wpa_printf(MSG_DEBUG,
		   "WPA: Ensure group state machine running for VLAN ID %d",
		   vlan_id);

	wpa_group_get(wpa_auth, group);
	group->num_setup_iface++;

	if (group->wpa_group_state == WPA_GROUP_FATAL_FAILURE)
		return -1;

	return 0;
}


/*
 * Decrease reference counter, expected to be zero afterwards.
 * returns: -1 on error (group not found, group in fail state)
 *          -2 if wpa_group is still referenced
 *           0 else
 */
int wpa_auth_release_group(struct wpa_authenticator *wpa_auth, int vlan_id)
{
	struct wpa_group *group;
	int ret = 0;

	if (wpa_auth == NULL)
		return 0;

	group = wpa_auth->group;
	while (group) {
		if (group->vlan_id == vlan_id)
			break;
		group = group->next;
	}

	if (group == NULL)
		return -1;

	wpa_printf(MSG_DEBUG,
		   "WPA: Try stopping group state machine for VLAN ID %d",
		   vlan_id);

	if (group->num_setup_iface <= 0) {
		wpa_printf(MSG_ERROR,
			   "WPA: wpa_auth_release_group called more often than wpa_auth_ensure_group for VLAN ID %d, skipping.",
			   vlan_id);
		return -1;
	}
	group->num_setup_iface--;

	if (group->wpa_group_state == WPA_GROUP_FATAL_FAILURE)
		ret = -1;

	if (group->references > 1) {
		wpa_printf(MSG_DEBUG,
			   "WPA: Cannot stop group state machine for VLAN ID %d as references are still hold",
			   vlan_id);
		ret = -2;
	}

	wpa_group_put(wpa_auth, group);

	return ret;
}


int wpa_auth_sta_set_vlan(struct wpa_state_machine *sm, int vlan_id)
{
	struct wpa_group *group;

	if (sm == NULL || sm->wpa_auth == NULL)
		return 0;

	group = sm->wpa_auth->group;
	while (group) {
		if (group->vlan_id == vlan_id)
			break;
		group = group->next;
	}

	if (group == NULL) {
		group = wpa_auth_add_group(sm->wpa_auth, vlan_id);
		if (group == NULL)
			return -1;
	}

	if (sm->group == group)
		return 0;

	if (group->wpa_group_state == WPA_GROUP_FATAL_FAILURE)
		return -1;

	wpa_printf(MSG_DEBUG, "WPA: Moving STA " MACSTR " to use group state "
		   "machine for VLAN ID %d", MAC2STR(sm->addr), vlan_id);

	wpa_group_get(sm->wpa_auth, group);
	wpa_group_put(sm->wpa_auth, sm->group);
	sm->group = group;

	return 0;
}


void wpa_auth_eapol_key_tx_status(struct wpa_authenticator *wpa_auth,
				  struct wpa_state_machine *sm, int ack)
{
	if (wpa_auth == NULL || sm == NULL)
		return;
	wpa_printf(MSG_DEBUG, "WPA: EAPOL-Key TX status for STA " MACSTR
		   " ack=%d", MAC2STR(sm->addr), ack);
	if (sm->pending_1_of_4_timeout && ack) {
		/*
		 * Some deployed supplicant implementations update their SNonce
		 * for each EAPOL-Key 2/4 message even within the same 4-way
		 * handshake and then fail to use the first SNonce when
		 * deriving the PTK. This results in unsuccessful 4-way
		 * handshake whenever the relatively short initial timeout is
		 * reached and EAPOL-Key 1/4 is retransmitted. Try to work
		 * around this by increasing the timeout now that we know that
		 * the station has received the frame.
		 */
		int timeout_ms = eapol_key_timeout_subseq;
		wpa_printf(MSG_DEBUG, "WPA: Increase initial EAPOL-Key 1/4 "
			   "timeout by %u ms because of acknowledged frame",
			   timeout_ms);
		eloop_cancel_timeout(wpa_send_eapol_timeout, wpa_auth, sm);
		eloop_register_timeout(timeout_ms / 1000,
				       (timeout_ms % 1000) * 1000,
				       wpa_send_eapol_timeout, wpa_auth, sm);
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (sm->eapol_status_cb) {
		sm->eapol_status_cb(sm->eapol_status_cb_ctx1,
				    sm->eapol_status_cb_ctx2);
		sm->eapol_status_cb = NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */
}


int wpa_auth_uses_sae(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return 0;
	return wpa_key_mgmt_sae(sm->wpa_key_mgmt);
}


int wpa_auth_uses_ft_sae(struct wpa_state_machine *sm)
{
	if (sm == NULL)
		return 0;
	return sm->wpa_key_mgmt == WPA_KEY_MGMT_FT_SAE;
}


#ifdef CONFIG_P2P
int wpa_auth_get_ip_addr(struct wpa_state_machine *sm, u8 *addr)
{
	if (sm == NULL || WPA_GET_BE32(sm->ip_addr) == 0)
		return -1;
	os_memcpy(addr, sm->ip_addr, 4);
	return 0;
}
#endif /* CONFIG_P2P */


int wpa_auth_radius_das_disconnect_pmksa(struct wpa_authenticator *wpa_auth,
					 struct radius_das_attrs *attr)
{
	return pmksa_cache_auth_radius_das_disconnect(wpa_auth->pmksa, attr);
}


void wpa_auth_reconfig_group_keys(struct wpa_authenticator *wpa_auth)
{
	struct wpa_group *group;

	if (!wpa_auth)
		return;
	for (group = wpa_auth->group; group; group = group->next)
		wpa_group_config_group_keys(wpa_auth, group);
}


#ifdef CONFIG_FILS

struct wpa_auth_fils_iter_data {
	struct wpa_authenticator *auth;
	const u8 *cache_id;
	struct rsn_pmksa_cache_entry *pmksa;
	const u8 *spa;
	const u8 *pmkid;
};


static int wpa_auth_fils_iter(struct wpa_authenticator *a, void *ctx)
{
	struct wpa_auth_fils_iter_data *data = ctx;

	if (a == data->auth || !a->conf.fils_cache_id_set ||
	    os_memcmp(a->conf.fils_cache_id, data->cache_id,
		      FILS_CACHE_ID_LEN) != 0)
		return 0;
	data->pmksa = pmksa_cache_auth_get(a->pmksa, data->spa, data->pmkid);
	return data->pmksa != NULL;
}


struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_get_fils_cache_id(struct wpa_authenticator *wpa_auth,
				 const u8 *sta_addr, const u8 *pmkid)
{
	struct wpa_auth_fils_iter_data idata;

	if (!wpa_auth->conf.fils_cache_id_set)
		return NULL;
	idata.auth = wpa_auth;
	idata.cache_id = wpa_auth->conf.fils_cache_id;
	idata.pmksa = NULL;
	idata.spa = sta_addr;
	idata.pmkid = pmkid;
	wpa_auth_for_each_auth(wpa_auth, wpa_auth_fils_iter, &idata);
	return idata.pmksa;
}


#ifdef CONFIG_IEEE80211R_AP
int wpa_auth_write_fte(struct wpa_authenticator *wpa_auth, int use_sha384,
		       u8 *buf, size_t len)
{
	struct wpa_auth_config *conf = &wpa_auth->conf;

	return wpa_write_ftie(conf, use_sha384, conf->r0_key_holder,
			      conf->r0_key_holder_len,
			      NULL, NULL, buf, len, NULL, 0);
}
#endif /* CONFIG_IEEE80211R_AP */


void wpa_auth_get_fils_aead_params(struct wpa_state_machine *sm,
				   u8 *fils_anonce, u8 *fils_snonce,
				   u8 *fils_kek, size_t *fils_kek_len)
{
	os_memcpy(fils_anonce, sm->ANonce, WPA_NONCE_LEN);
	os_memcpy(fils_snonce, sm->SNonce, WPA_NONCE_LEN);
	os_memcpy(fils_kek, sm->PTK.kek, WPA_KEK_MAX_LEN);
	*fils_kek_len = sm->PTK.kek_len;
}

#endif /* CONFIG_FILS */


#ifdef CONFIG_TESTING_OPTIONS

int wpa_auth_resend_m1(struct wpa_state_machine *sm, int change_anonce,
		       void (*cb)(void *ctx1, void *ctx2),
		       void *ctx1, void *ctx2)
{
	const u8 *anonce = sm->ANonce;
	u8 anonce_buf[WPA_NONCE_LEN];

	if (change_anonce) {
		if (random_get_bytes(anonce_buf, WPA_NONCE_LEN))
			return -1;
		anonce = anonce_buf;
	}

	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 1/4 msg of 4-Way Handshake (TESTING)");
	wpa_send_eapol(sm->wpa_auth, sm,
		       WPA_KEY_INFO_ACK | WPA_KEY_INFO_KEY_TYPE, NULL,
		       anonce, NULL, 0, 0, 0);
	return 0;
}


int wpa_auth_resend_m3(struct wpa_state_machine *sm,
		       void (*cb)(void *ctx1, void *ctx2),
		       void *ctx1, void *ctx2)
{
	u8 rsc[WPA_KEY_RSC_LEN], *_rsc, *gtk, *kde, *pos;
#ifdef CONFIG_IEEE80211W
	u8 *opos;
#endif /* CONFIG_IEEE80211W */
	size_t gtk_len, kde_len;
	struct wpa_group *gsm = sm->group;
	u8 *wpa_ie;
	int wpa_ie_len, secure, keyidx, encr = 0;

	/* Send EAPOL(1, 1, 1, Pair, P, RSC, ANonce, MIC(PTK), RSNIE, [MDIE],
	   GTK[GN], IGTK, [FTIE], [TIE * 2])
	 */

	/* Use 0 RSC */
	os_memset(rsc, 0, WPA_KEY_RSC_LEN);
	/* If FT is used, wpa_auth->wpa_ie includes both RSNIE and MDIE */
	wpa_ie = sm->wpa_auth->wpa_ie;
	wpa_ie_len = sm->wpa_auth->wpa_ie_len;
	if (sm->wpa == WPA_VERSION_WPA &&
	    (sm->wpa_auth->conf.wpa & WPA_PROTO_RSN) &&
	    wpa_ie_len > wpa_ie[1] + 2 && wpa_ie[0] == WLAN_EID_RSN) {
		/* WPA-only STA, remove RSN IE and possible MDIE */
		wpa_ie = wpa_ie + wpa_ie[1] + 2;
		if (wpa_ie[0] == WLAN_EID_MOBILITY_DOMAIN)
			wpa_ie = wpa_ie + wpa_ie[1] + 2;
		wpa_ie_len = wpa_ie[1] + 2;
	}
	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 3/4 msg of 4-Way Handshake (TESTING)");
	if (sm->wpa == WPA_VERSION_WPA2) {
		/* WPA2 send GTK in the 4-way handshake */
		secure = 1;
		gtk = gsm->GTK[gsm->GN - 1];
		gtk_len = gsm->GTK_len;
		keyidx = gsm->GN;
		_rsc = rsc;
		encr = 1;
	} else {
		/* WPA does not include GTK in msg 3/4 */
		secure = 0;
		gtk = NULL;
		gtk_len = 0;
		keyidx = 0;
		_rsc = NULL;
		if (sm->rx_eapol_key_secure) {
			/*
			 * It looks like Windows 7 supplicant tries to use
			 * Secure bit in msg 2/4 after having reported Michael
			 * MIC failure and it then rejects the 4-way handshake
			 * if msg 3/4 does not set Secure bit. Work around this
			 * by setting the Secure bit here even in the case of
			 * WPA if the supplicant used it first.
			 */
			wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
					"STA used Secure bit in WPA msg 2/4 - "
					"set Secure for 3/4 as workaround");
			secure = 1;
		}
	}

	kde_len = wpa_ie_len + ieee80211w_kde_len(sm);
	if (gtk)
		kde_len += 2 + RSN_SELECTOR_LEN + 2 + gtk_len;
#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		kde_len += 2 + PMKID_LEN; /* PMKR1Name into RSN IE */
		kde_len += 300; /* FTIE + 2 * TIE */
	}
#endif /* CONFIG_IEEE80211R_AP */
	kde = os_malloc(kde_len);
	if (kde == NULL)
		return -1;

	pos = kde;
	os_memcpy(pos, wpa_ie, wpa_ie_len);
	pos += wpa_ie_len;
#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		int res;
		size_t elen;

		elen = pos - kde;
		res = wpa_insert_pmkid(kde, &elen, sm->pmk_r1_name);
		if (res < 0) {
			wpa_printf(MSG_ERROR, "FT: Failed to insert "
				   "PMKR1Name into RSN IE in EAPOL-Key data");
			os_free(kde);
			return -1;
		}
		pos -= wpa_ie_len;
		pos += elen;
	}
#endif /* CONFIG_IEEE80211R_AP */
	if (gtk) {
		u8 hdr[2];
		hdr[0] = keyidx & 0x03;
		hdr[1] = 0;
		pos = wpa_add_kde(pos, RSN_KEY_DATA_GROUPKEY, hdr, 2,
				  gtk, gtk_len);
	}
#ifdef CONFIG_IEEE80211W
	opos = pos;
	pos = ieee80211w_kde_add(sm, pos);
	if (pos - opos >= 2 + RSN_SELECTOR_LEN + WPA_IGTK_KDE_PREFIX_LEN) {
		/* skip KDE header and keyid */
		opos += 2 + RSN_SELECTOR_LEN + 2;
		os_memset(opos, 0, 6); /* clear PN */
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_IEEE80211R_AP
	if (wpa_key_mgmt_ft(sm->wpa_key_mgmt)) {
		int res;
		struct wpa_auth_config *conf;

		conf = &sm->wpa_auth->conf;
		if (sm->assoc_resp_ftie &&
		    kde + kde_len - pos >= 2 + sm->assoc_resp_ftie[1]) {
			os_memcpy(pos, sm->assoc_resp_ftie,
				  2 + sm->assoc_resp_ftie[1]);
			res = 2 + sm->assoc_resp_ftie[1];
		} else {
			int use_sha384 = wpa_key_mgmt_sha384(sm->wpa_key_mgmt);

			res = wpa_write_ftie(conf, use_sha384,
					     conf->r0_key_holder,
					     conf->r0_key_holder_len,
					     NULL, NULL, pos,
					     kde + kde_len - pos,
					     NULL, 0);
		}
		if (res < 0) {
			wpa_printf(MSG_ERROR, "FT: Failed to insert FTIE "
				   "into EAPOL-Key Key Data");
			os_free(kde);
			return -1;
		}
		pos += res;

		/* TIE[ReassociationDeadline] (TU) */
		*pos++ = WLAN_EID_TIMEOUT_INTERVAL;
		*pos++ = 5;
		*pos++ = WLAN_TIMEOUT_REASSOC_DEADLINE;
		WPA_PUT_LE32(pos, conf->reassociation_deadline);
		pos += 4;

		/* TIE[KeyLifetime] (seconds) */
		*pos++ = WLAN_EID_TIMEOUT_INTERVAL;
		*pos++ = 5;
		*pos++ = WLAN_TIMEOUT_KEY_LIFETIME;
		WPA_PUT_LE32(pos, conf->r0_key_lifetime);
		pos += 4;
	}
#endif /* CONFIG_IEEE80211R_AP */

	wpa_send_eapol(sm->wpa_auth, sm,
		       (secure ? WPA_KEY_INFO_SECURE : 0) |
		       (wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len) ?
			WPA_KEY_INFO_MIC : 0) |
		       WPA_KEY_INFO_ACK | WPA_KEY_INFO_INSTALL |
		       WPA_KEY_INFO_KEY_TYPE,
		       _rsc, sm->ANonce, kde, pos - kde, keyidx, encr);
	os_free(kde);
	return 0;
}


int wpa_auth_resend_group_m1(struct wpa_state_machine *sm,
			     void (*cb)(void *ctx1, void *ctx2),
			     void *ctx1, void *ctx2)
{
	u8 rsc[WPA_KEY_RSC_LEN];
	struct wpa_group *gsm = sm->group;
	const u8 *kde;
	u8 *kde_buf = NULL, *pos, hdr[2];
#ifdef CONFIG_IEEE80211W
	u8 *opos;
#endif /* CONFIG_IEEE80211W */
	size_t kde_len;
	u8 *gtk;

	/* Send EAPOL(1, 1, 1, !Pair, G, RSC, GNonce, MIC(PTK), GTK[GN]) */
	os_memset(rsc, 0, WPA_KEY_RSC_LEN);
	/* Use 0 RSC */
	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"sending 1/2 msg of Group Key Handshake (TESTING)");

	gtk = gsm->GTK[gsm->GN - 1];
	if (sm->wpa == WPA_VERSION_WPA2) {
		kde_len = 2 + RSN_SELECTOR_LEN + 2 + gsm->GTK_len +
			ieee80211w_kde_len(sm);
		kde_buf = os_malloc(kde_len);
		if (kde_buf == NULL)
			return -1;

		kde = pos = kde_buf;
		hdr[0] = gsm->GN & 0x03;
		hdr[1] = 0;
		pos = wpa_add_kde(pos, RSN_KEY_DATA_GROUPKEY, hdr, 2,
				  gtk, gsm->GTK_len);
#ifdef CONFIG_IEEE80211W
		opos = pos;
		pos = ieee80211w_kde_add(sm, pos);
		if (pos - opos >=
		    2 + RSN_SELECTOR_LEN + WPA_IGTK_KDE_PREFIX_LEN) {
			/* skip KDE header and keyid */
			opos += 2 + RSN_SELECTOR_LEN + 2;
			os_memset(opos, 0, 6); /* clear PN */
		}
#endif /* CONFIG_IEEE80211W */
		kde_len = pos - kde;
	} else {
		kde = gtk;
		kde_len = gsm->GTK_len;
	}

	sm->eapol_status_cb = cb;
	sm->eapol_status_cb_ctx1 = ctx1;
	sm->eapol_status_cb_ctx2 = ctx2;

	wpa_send_eapol(sm->wpa_auth, sm,
		       WPA_KEY_INFO_SECURE |
		       (wpa_mic_len(sm->wpa_key_mgmt, sm->pmk_len) ?
			WPA_KEY_INFO_MIC : 0) |
		       WPA_KEY_INFO_ACK |
		       (!sm->Pair ? WPA_KEY_INFO_INSTALL : 0),
		       rsc, NULL, kde, kde_len, gsm->GN, 1);

	os_free(kde_buf);
	return 0;
}


int wpa_auth_rekey_gtk(struct wpa_authenticator *wpa_auth)
{
	if (!wpa_auth)
		return -1;
	eloop_cancel_timeout(wpa_rekey_gtk, wpa_auth, NULL);
	return eloop_register_timeout(0, 0, wpa_rekey_gtk, wpa_auth, NULL);
}

#endif /* CONFIG_TESTING_OPTIONS */
