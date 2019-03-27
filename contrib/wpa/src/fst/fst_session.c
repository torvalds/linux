/*
 * FST module - FST Session implementation
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/defs.h"
#include "fst/fst_internal.h"
#include "fst/fst_defs.h"
#include "fst/fst_ctrl_iface.h"
#ifdef CONFIG_FST_TEST
#include "fst/fst_ctrl_defs.h"
#endif /* CONFIG_FST_TEST */

#define US_80211_TU 1024

#define US_TO_TU(m) ((m) * / US_80211_TU)
#define TU_TO_US(m) ((m) * US_80211_TU)

#define FST_LLT_SWITCH_IMMEDIATELY 0

#define fst_printf_session(s, level, format, ...) \
	fst_printf((level), "%u (0x%08x): [" MACSTR "," MACSTR "] :" format, \
		   (s)->id, (s)->data.fsts_id, \
		   MAC2STR((s)->data.old_peer_addr), \
		   MAC2STR((s)->data.new_peer_addr), \
		   ##__VA_ARGS__)

#define fst_printf_siface(s, iface, level, format, ...) \
	fst_printf_session((s), (level), "%s: " format, \
			   fst_iface_get_name(iface), ##__VA_ARGS__)

#define fst_printf_sframe(s, is_old, level, format, ...) \
	fst_printf_siface((s), \
		(is_old) ? (s)->data.old_iface : (s)->data.new_iface, \
		(level), format, ##__VA_ARGS__)

#define FST_LLT_MS_DEFAULT 50
#define FST_ACTION_MAX_SUPPORTED   FST_ACTION_ON_CHANNEL_TUNNEL

static const char * const fst_action_names[] = {
	[FST_ACTION_SETUP_REQUEST]     = "Setup Request",
	[FST_ACTION_SETUP_RESPONSE]    = "Setup Response",
	[FST_ACTION_TEAR_DOWN]         = "Tear Down",
	[FST_ACTION_ACK_REQUEST]       = "Ack Request",
	[FST_ACTION_ACK_RESPONSE]      = "Ack Response",
	[FST_ACTION_ON_CHANNEL_TUNNEL] = "On Channel Tunnel",
};

struct fst_session {
	struct {
		/* Session configuration that can be zeroed on reset */
		u8 old_peer_addr[ETH_ALEN];
		u8 new_peer_addr[ETH_ALEN];
		struct fst_iface *new_iface;
		struct fst_iface *old_iface;
		u32 llt_ms;
		u8 pending_setup_req_dlgt;
		u32 fsts_id; /* FSTS ID, see spec, 8.4.2.147
			      * Session Transition element */
	} data;
	/* Session object internal fields which won't be zeroed on reset */
	struct dl_list global_sessions_lentry;
	u32 id; /* Session object ID used to identify
		 * specific session object */
	struct fst_group *group;
	enum fst_session_state state;
	Boolean stt_armed;
};

static struct dl_list global_sessions_list;
static u32 global_session_id = 0;

#define foreach_fst_session(s) \
	dl_list_for_each((s), &global_sessions_list, \
			 struct fst_session, global_sessions_lentry)

#define foreach_fst_session_safe(s, temp) \
	dl_list_for_each_safe((s), (temp), &global_sessions_list, \
			      struct fst_session, global_sessions_lentry)


static void fst_session_global_inc_id(void)
{
	global_session_id++;
	if (global_session_id == FST_INVALID_SESSION_ID)
		global_session_id++;
}


int fst_session_global_init(void)
{
	dl_list_init(&global_sessions_list);
	return 0;
}


void fst_session_global_deinit(void)
{
	WPA_ASSERT(dl_list_empty(&global_sessions_list));
}


static inline void fst_session_notify_ctrl(struct fst_session *s,
					   enum fst_event_type event_type,
					   union fst_event_extra *extra)
{
	foreach_fst_ctrl_call(on_event, event_type, NULL, s, extra);
}


static void fst_session_set_state(struct fst_session *s,
				  enum fst_session_state state,
				  union fst_session_state_switch_extra *extra)
{
	if (s->state != state) {
		union fst_event_extra evext = {
			.session_state = {
				.old_state = s->state,
				.new_state = state,
			},
		};

		if (extra)
			evext.session_state.extra = *extra;
		fst_session_notify_ctrl(s, EVENT_FST_SESSION_STATE_CHANGED,
					&evext);
		fst_printf_session(s, MSG_INFO, "State: %s => %s",
				   fst_session_state_name(s->state),
				   fst_session_state_name(state));
		s->state = state;
	}
}


static u32 fst_find_free_session_id(void)
{
	u32 i, id = FST_INVALID_SESSION_ID;
	struct fst_session *s;

	for (i = 0; i < (u32) -1; i++) {
		Boolean in_use = FALSE;

		foreach_fst_session(s) {
			if (s->id == global_session_id) {
				fst_session_global_inc_id();
				in_use = TRUE;
				break;
			}
		}
		if (!in_use) {
			id = global_session_id;
			fst_session_global_inc_id();
			break;
		}
	}

	return id;
}


static void fst_session_timeout_handler(void *eloop_data, void *user_ctx)
{
	struct fst_session *s = user_ctx;
	union fst_session_state_switch_extra extra = {
		.to_initial = {
			.reason = REASON_STT,
		},
	};

	fst_printf_session(s, MSG_WARNING, "Session State Timeout");
	fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &extra);
}


static void fst_session_stt_arm(struct fst_session *s)
{
	/* Action frames sometimes get delayed. Use relaxed timeout (2*) */
	eloop_register_timeout(0, 2 * TU_TO_US(FST_DEFAULT_SESSION_TIMEOUT_TU),
			       fst_session_timeout_handler, NULL, s);
	s->stt_armed = TRUE;
}


static void fst_session_stt_disarm(struct fst_session *s)
{
	if (s->stt_armed) {
		eloop_cancel_timeout(fst_session_timeout_handler, NULL, s);
		s->stt_armed = FALSE;
	}
}


static Boolean fst_session_is_in_transition(struct fst_session *s)
{
	/* See spec, 10.32.2.2  Transitioning between states */
	return s->stt_armed;
}


static int fst_session_is_in_progress(struct fst_session *s)
{
	return s->state != FST_SESSION_STATE_INITIAL;
}


static int fst_session_is_ready_pending(struct fst_session *s)
{
	return s->state == FST_SESSION_STATE_SETUP_COMPLETION &&
		fst_session_is_in_transition(s);
}


static int fst_session_is_ready(struct fst_session *s)
{
	return s->state == FST_SESSION_STATE_SETUP_COMPLETION &&
		!fst_session_is_in_transition(s);
}


static int fst_session_is_switch_requested(struct fst_session *s)
{
	return s->state == FST_SESSION_STATE_TRANSITION_DONE &&
		fst_session_is_in_transition(s);
}


static struct fst_session *
fst_find_session_in_progress(const u8 *peer_addr, struct fst_group *g)
{
	struct fst_session *s;

	foreach_fst_session(s) {
		if (s->group == g &&
		    (os_memcmp(s->data.old_peer_addr, peer_addr,
			       ETH_ALEN) == 0 ||
		     os_memcmp(s->data.new_peer_addr, peer_addr,
			       ETH_ALEN) == 0) &&
		    fst_session_is_in_progress(s))
			return s;
	}

	return NULL;
}


static void fst_session_reset_ex(struct fst_session *s, enum fst_reason reason)
{
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reason = reason,
		},
	};

	if (s->state == FST_SESSION_STATE_SETUP_COMPLETION ||
	    s->state == FST_SESSION_STATE_TRANSITION_DONE)
		fst_session_tear_down_setup(s);
	fst_session_stt_disarm(s);
	os_memset(&s->data, 0, sizeof(s->data));
	fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
}


static int fst_session_send_action(struct fst_session *s, Boolean old_iface,
				   const void *payload, size_t size,
				   const struct wpabuf *extra_buf)
{
	size_t len;
	int res;
	struct wpabuf *buf;
	u8 action;
	struct fst_iface *iface =
		old_iface ? s->data.old_iface : s->data.new_iface;

	WPA_ASSERT(payload != NULL);
	WPA_ASSERT(size != 0);

	action = *(const u8 *) payload;

	WPA_ASSERT(action <= FST_ACTION_MAX_SUPPORTED);

	if (!iface) {
		fst_printf_session(s, MSG_ERROR,
				   "no %s interface for FST Action '%s' sending",
				   old_iface ? "old" : "new",
				   fst_action_names[action]);
		return -1;
	}

	len = sizeof(u8) /* category */ + size;
	if (extra_buf)
		len += wpabuf_size(extra_buf);

	buf = wpabuf_alloc(len);
	if (!buf) {
		fst_printf_session(s, MSG_ERROR,
				   "cannot allocate buffer of %zu bytes for FST Action '%s' sending",
				   len, fst_action_names[action]);
		return -1;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_FST);
	wpabuf_put_data(buf, payload, size);
	if (extra_buf)
		wpabuf_put_buf(buf, extra_buf);

	res = fst_iface_send_action(iface,
				    old_iface ? s->data.old_peer_addr :
				    s->data.new_peer_addr, buf);
	if (res < 0)
		fst_printf_siface(s, iface, MSG_ERROR,
				  "failed to send FST Action '%s'",
				  fst_action_names[action]);
	else
		fst_printf_siface(s, iface, MSG_DEBUG, "FST Action '%s' sent",
				  fst_action_names[action]);
	wpabuf_free(buf);

	return res;
}


static int fst_session_send_tear_down(struct fst_session *s)
{
	struct fst_tear_down td;
	int res;

	if (!fst_session_is_in_progress(s)) {
		fst_printf_session(s, MSG_ERROR, "No FST setup to tear down");
		return -1;
	}

	WPA_ASSERT(s->data.old_iface != NULL);
	WPA_ASSERT(s->data.new_iface != NULL);

	os_memset(&td, 0, sizeof(td));

	td.action = FST_ACTION_TEAR_DOWN;
	td.fsts_id = host_to_le32(s->data.fsts_id);

	res = fst_session_send_action(s, TRUE, &td, sizeof(td), NULL);
	if (!res)
		fst_printf_sframe(s, TRUE, MSG_INFO, "FST TearDown sent");
	else
		fst_printf_sframe(s, TRUE, MSG_ERROR,
				  "failed to send FST TearDown");

	return res;
}


static void fst_session_handle_setup_request(struct fst_iface *iface,
					     const struct ieee80211_mgmt *mgmt,
					     size_t frame_len)
{
	struct fst_session *s;
	const struct fst_setup_req *req;
	struct fst_iface *new_iface = NULL;
	struct fst_group *g;
	u8 new_iface_peer_addr[ETH_ALEN];
	size_t plen;

	if (frame_len < IEEE80211_HDRLEN + 1 + sizeof(*req))  {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Request dropped: too short (%zu < %zu)",
				 frame_len,
				 IEEE80211_HDRLEN + 1 + sizeof(*req));
		return;
	}
	plen = frame_len - IEEE80211_HDRLEN - 1;
	req = (const struct fst_setup_req *)
		(((const u8 *) mgmt) + IEEE80211_HDRLEN + 1);
	if (req->stie.element_id != WLAN_EID_SESSION_TRANSITION ||
	    req->stie.length < 11) {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Request dropped: invalid STIE");
		return;
	}

	if (req->stie.new_band_id == req->stie.old_band_id) {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Request dropped: new and old band IDs are the same");
		return;
	}

	g = fst_iface_get_group(iface);

	if (plen > sizeof(*req)) {
		fst_iface_update_mb_ie(iface, mgmt->sa, (const u8 *) (req + 1),
				       plen - sizeof(*req));
		fst_printf_iface(iface, MSG_INFO,
				 "FST Request: MB IEs updated for " MACSTR,
				 MAC2STR(mgmt->sa));
	}

	new_iface = fst_group_get_peer_other_connection(iface, mgmt->sa,
							req->stie.new_band_id,
							new_iface_peer_addr);
	if (!new_iface) {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Request dropped: new iface not found");
		return;
	}
	fst_printf_iface(iface, MSG_INFO,
			 "FST Request: new iface (%s:" MACSTR ") found",
			 fst_iface_get_name(new_iface),
			 MAC2STR(new_iface_peer_addr));

	s = fst_find_session_in_progress(mgmt->sa, g);
	if (s) {
		union fst_session_state_switch_extra evext = {
			.to_initial = {
				.reason = REASON_SETUP,
			},
		};

		/*
		 * 10.32.2.2  Transitioning between states:
		 * Upon receipt of an FST Setup Request frame, the responder
		 * shall respond with an FST Setup Response frame unless it has
		 * a pending FST Setup Request frame addressed to the initiator
		 * and the responder has a numerically larger MAC address than
		 * the initiator’s MAC address, in which case, the responder
		 * shall delete the received FST Setup Request.
		 */
		if (fst_session_is_ready_pending(s) &&
		    /* waiting for Setup Response */
		    os_memcmp(mgmt->da, mgmt->sa, ETH_ALEN) > 0) {
			fst_printf_session(s, MSG_WARNING,
					   "FST Request dropped due to MAC comparison (our MAC is "
					   MACSTR ")",
					   MAC2STR(mgmt->da));
			return;
		}

		/*
		 * State is SETUP_COMPLETION (either in transition or not) or
		 * TRANSITION_DONE (in transition).
		 * Setup Request arriving in this state could mean:
		 * 1. peer sent it before receiving our Setup Request (race
		 *    condition)
		 * 2. peer didn't receive our Setup Response. Peer is retrying
		 *    after STT timeout
		 * 3. peer's FST state machines are out of sync due to some
		 *    other reason
		 *
		 * We will reset our session and create a new one instead.
		 */

		fst_printf_session(s, MSG_WARNING,
			"resetting due to FST request");

		/*
		 * If FST Setup Request arrived with the same FSTS ID as one we
		 * initialized before, there's no need to tear down the session.
		 * Moreover, as FSTS ID is the same, the other side will
		 * associate this tear down with the session it initiated that
		 * will break the sync.
		 */
		if (le_to_host32(req->stie.fsts_id) != s->data.fsts_id)
			fst_session_send_tear_down(s);
		else
			fst_printf_session(s, MSG_WARNING,
					   "Skipping TearDown as the FST request has the same FSTS ID as initiated");
		fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
		fst_session_stt_disarm(s);
	}

	s = fst_session_create(g);
	if (!s) {
		fst_printf(MSG_WARNING,
			   "FST Request dropped: cannot create session for %s and %s",
			   fst_iface_get_name(iface),
			   fst_iface_get_name(new_iface));
		return;
	}

	fst_session_set_iface(s, iface, TRUE);
	fst_session_set_peer_addr(s, mgmt->sa, TRUE);
	fst_session_set_iface(s, new_iface, FALSE);
	fst_session_set_peer_addr(s, new_iface_peer_addr, FALSE);
	fst_session_set_llt(s, FST_LLT_VAL_TO_MS(le_to_host32(req->llt)));
	s->data.pending_setup_req_dlgt = req->dialog_token;
	s->data.fsts_id = le_to_host32(req->stie.fsts_id);

	fst_session_stt_arm(s);

	fst_session_notify_ctrl(s, EVENT_FST_SETUP, NULL);

	fst_session_set_state(s, FST_SESSION_STATE_SETUP_COMPLETION, NULL);
}


static void fst_session_handle_setup_response(struct fst_session *s,
					      struct fst_iface *iface,
					      const struct ieee80211_mgmt *mgmt,
					      size_t frame_len)
{
	const struct fst_setup_res *res;
	size_t plen = frame_len - IEEE80211_HDRLEN - 1;
	enum hostapd_hw_mode hw_mode;
	u8 channel;
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reject_code = 0,
		},
	};

	if (iface != s->data.old_iface) {
		fst_printf_session(s, MSG_WARNING,
				   "FST Response dropped: %s is not the old iface",
				   fst_iface_get_name(iface));
		return;
	}

	if (!fst_session_is_ready_pending(s)) {
		fst_printf_session(s, MSG_WARNING,
				   "FST Response dropped due to wrong state: %s",
				   fst_session_state_name(s->state));
		return;
	}

	if (plen < sizeof(*res)) {
		fst_printf_session(s, MSG_WARNING,
				   "Too short FST Response dropped");
		return;
	}
	res = (const struct fst_setup_res *)
		(((const u8 *) mgmt) + IEEE80211_HDRLEN + 1);
	if (res->stie.element_id != WLAN_EID_SESSION_TRANSITION ||
	    res->stie.length < 11) {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Response dropped: invalid STIE");
		return;
	}

	if (res->dialog_token != s->data.pending_setup_req_dlgt)  {
		fst_printf_session(s, MSG_WARNING,
				   "FST Response dropped due to wrong dialog token (%u != %u)",
				   s->data.pending_setup_req_dlgt,
				   res->dialog_token);
		return;
	}

	if (res->status_code == WLAN_STATUS_SUCCESS &&
	    le_to_host32(res->stie.fsts_id) != s->data.fsts_id) {
		fst_printf_session(s, MSG_WARNING,
				   "FST Response dropped due to wrong FST Session ID (%u)",
				   le_to_host32(res->stie.fsts_id));
		return;
	}

	fst_session_stt_disarm(s);

	if (res->status_code != WLAN_STATUS_SUCCESS) {
		/*
		 * 10.32.2.2  Transitioning between states
		 * The initiator shall set the STT to the value of the
		 * FSTSessionTimeOut field at ... and at each ACK frame sent in
		 * response to a received FST Setup Response with the Status
		 * Code field equal to PENDING_ADMITTING_FST_SESSION or
		 * PENDING_GAP_IN_BA_WINDOW.
		 */
		evext.to_initial.reason = REASON_REJECT;
		evext.to_initial.reject_code = res->status_code;
		evext.to_initial.initiator = FST_INITIATOR_REMOTE;
		fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
		fst_printf_session(s, MSG_WARNING,
				   "FST Setup rejected by remote side with status %u",
				   res->status_code);
		return;
	}

	fst_iface_get_channel_info(s->data.new_iface, &hw_mode, &channel);

	if (fst_hw_mode_to_band(hw_mode) != res->stie.new_band_id) {
		evext.to_initial.reason = REASON_ERROR_PARAMS;
		fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
		fst_printf_session(s, MSG_WARNING,
				   "invalid FST Setup parameters");
		fst_session_tear_down_setup(s);
		return;
	}

	fst_printf_session(s, MSG_INFO,
			   "%s: FST Setup established for %s (llt=%u)",
			   fst_iface_get_name(s->data.old_iface),
			   fst_iface_get_name(s->data.new_iface),
			   s->data.llt_ms);

	fst_session_notify_ctrl(s, EVENT_FST_ESTABLISHED, NULL);

	if (s->data.llt_ms == FST_LLT_SWITCH_IMMEDIATELY)
		fst_session_initiate_switch(s);
}


static void fst_session_handle_tear_down(struct fst_session *s,
					 struct fst_iface *iface,
					 const struct ieee80211_mgmt *mgmt,
					 size_t frame_len)
{
	const struct fst_tear_down *td;
	size_t plen = frame_len - IEEE80211_HDRLEN - 1;
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reason = REASON_TEARDOWN,
			.initiator = FST_INITIATOR_REMOTE,
		},
	};

	if (plen < sizeof(*td)) {
		fst_printf_session(s, MSG_WARNING,
				   "Too short FST Tear Down dropped");
		return;
	}
	td = (const struct fst_tear_down *)
		(((const u8 *) mgmt) + IEEE80211_HDRLEN + 1);

	if (le_to_host32(td->fsts_id) != s->data.fsts_id) {
		fst_printf_siface(s, iface, MSG_WARNING,
				  "tear down for wrong FST Setup ID (%u)",
				  le_to_host32(td->fsts_id));
		return;
	}

	fst_session_stt_disarm(s);

	fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
}


static void fst_session_handle_ack_request(struct fst_session *s,
					   struct fst_iface *iface,
					   const struct ieee80211_mgmt *mgmt,
					   size_t frame_len)
{
	const struct fst_ack_req *req;
	size_t plen = frame_len - IEEE80211_HDRLEN - 1;
	struct fst_ack_res res;
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reason = REASON_SWITCH,
			.initiator = FST_INITIATOR_REMOTE,
		},
	};

	if (!fst_session_is_ready(s) && !fst_session_is_switch_requested(s)) {
		fst_printf_siface(s, iface, MSG_ERROR,
				  "cannot initiate switch due to wrong session state (%s)",
				  fst_session_state_name(s->state));
		return;
	}

	WPA_ASSERT(s->data.new_iface != NULL);

	if (iface != s->data.new_iface) {
		fst_printf_siface(s, iface, MSG_ERROR,
				  "Ack received on wrong interface");
		return;
	}

	if (plen < sizeof(*req)) {
		fst_printf_session(s, MSG_WARNING,
				   "Too short FST Ack Request dropped");
		return;
	}
	req = (const struct fst_ack_req *)
		(((const u8 *) mgmt) + IEEE80211_HDRLEN + 1);

	if (le_to_host32(req->fsts_id) != s->data.fsts_id) {
		fst_printf_siface(s, iface, MSG_WARNING,
				  "Ack for wrong FST Setup ID (%u)",
				  le_to_host32(req->fsts_id));
		return;
	}

	os_memset(&res, 0, sizeof(res));

	res.action = FST_ACTION_ACK_RESPONSE;
	res.dialog_token = req->dialog_token;
	res.fsts_id = req->fsts_id;

	if (!fst_session_send_action(s, FALSE, &res, sizeof(res), NULL)) {
		fst_printf_sframe(s, FALSE, MSG_INFO, "FST Ack Response sent");
		fst_session_stt_disarm(s);
		fst_session_set_state(s, FST_SESSION_STATE_TRANSITION_DONE,
				      NULL);
		fst_session_set_state(s, FST_SESSION_STATE_TRANSITION_CONFIRMED,
				      NULL);
		fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
	}
}


static void
fst_session_handle_ack_response(struct fst_session *s,
				struct fst_iface *iface,
				const struct ieee80211_mgmt *mgmt,
				size_t frame_len)
{
	const struct fst_ack_res *res;
	size_t plen = frame_len - IEEE80211_HDRLEN - 1;
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reason = REASON_SWITCH,
			.initiator = FST_INITIATOR_LOCAL,
		},
	};

	if (!fst_session_is_switch_requested(s)) {
		fst_printf_siface(s, iface, MSG_ERROR,
				  "Ack Response in inappropriate session state (%s)",
				  fst_session_state_name(s->state));
		return;
	}

	WPA_ASSERT(s->data.new_iface != NULL);

	if (iface != s->data.new_iface) {
		fst_printf_siface(s, iface, MSG_ERROR,
				  "Ack response received on wrong interface");
		return;
	}

	if (plen < sizeof(*res)) {
		fst_printf_session(s, MSG_WARNING,
				   "Too short FST Ack Response dropped");
		return;
	}
	res = (const struct fst_ack_res *)
		(((const u8 *) mgmt) + IEEE80211_HDRLEN + 1);

	if (le_to_host32(res->fsts_id) != s->data.fsts_id) {
		fst_printf_siface(s, iface, MSG_ERROR,
				  "Ack response for wrong FST Setup ID (%u)",
				  le_to_host32(res->fsts_id));
		return;
	}

	fst_session_set_state(s, FST_SESSION_STATE_TRANSITION_CONFIRMED, NULL);
	fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);

	fst_session_stt_disarm(s);
}


struct fst_session * fst_session_create(struct fst_group *g)
{
	struct fst_session *s;
	u32 id;

	id = fst_find_free_session_id();
	if (id == FST_INVALID_SESSION_ID) {
		fst_printf(MSG_ERROR, "Cannot assign new session ID");
		return NULL;
	}

	s = os_zalloc(sizeof(*s));
	if (!s) {
		fst_printf(MSG_ERROR, "Cannot allocate new session object");
		return NULL;
	}

	s->id = id;
	s->group = g;
	s->state = FST_SESSION_STATE_INITIAL;

	s->data.llt_ms = FST_LLT_MS_DEFAULT;

	fst_printf(MSG_INFO, "Session %u created", s->id);

	dl_list_add_tail(&global_sessions_list, &s->global_sessions_lentry);

	foreach_fst_ctrl_call(on_session_added, s);

	return s;
}


void fst_session_set_iface(struct fst_session *s, struct fst_iface *iface,
			   Boolean is_old)
{
	if (is_old)
		s->data.old_iface = iface;
	else
		s->data.new_iface = iface;

}


void fst_session_set_llt(struct fst_session *s, u32 llt)
{
	s->data.llt_ms = llt;
}


void fst_session_set_peer_addr(struct fst_session *s, const u8 *addr,
			       Boolean is_old)
{
	u8 *a = is_old ? s->data.old_peer_addr : s->data.new_peer_addr;

	os_memcpy(a, addr, ETH_ALEN);
}


int fst_session_initiate_setup(struct fst_session *s)
{
	struct fst_setup_req req;
	int res;
	u32 fsts_id;
	u8 dialog_token;
	struct fst_session *_s;

	if (fst_session_is_in_progress(s)) {
		fst_printf_session(s, MSG_ERROR, "Session in progress");
		return -EINVAL;
	}

	if (is_zero_ether_addr(s->data.old_peer_addr)) {
		fst_printf_session(s, MSG_ERROR, "No old peer MAC address");
		return -EINVAL;
	}

	if (is_zero_ether_addr(s->data.new_peer_addr)) {
		fst_printf_session(s, MSG_ERROR, "No new peer MAC address");
		return -EINVAL;
	}

	if (!s->data.old_iface) {
		fst_printf_session(s, MSG_ERROR, "No old interface defined");
		return -EINVAL;
	}

	if (!s->data.new_iface) {
		fst_printf_session(s, MSG_ERROR, "No new interface defined");
		return -EINVAL;
	}

	if (s->data.new_iface == s->data.old_iface) {
		fst_printf_session(s, MSG_ERROR,
				   "Same interface set as old and new");
		return -EINVAL;
	}

	if (!fst_iface_is_connected(s->data.old_iface, s->data.old_peer_addr,
				    FALSE)) {
		fst_printf_session(s, MSG_ERROR,
				   "The preset old peer address is not connected");
		return -EINVAL;
	}

	if (!fst_iface_is_connected(s->data.new_iface, s->data.new_peer_addr,
				    FALSE)) {
		fst_printf_session(s, MSG_ERROR,
				   "The preset new peer address is not connected");
		return -EINVAL;
	}

	_s = fst_find_session_in_progress(s->data.old_peer_addr, s->group);
	if (_s) {
		fst_printf_session(s, MSG_ERROR,
				   "There is another session in progress (old): %u",
				   _s->id);
		return -EINVAL;
	}

	_s = fst_find_session_in_progress(s->data.new_peer_addr, s->group);
	if (_s) {
		fst_printf_session(s, MSG_ERROR,
				   "There is another session in progress (new): %u",
				   _s->id);
		return -EINVAL;
	}

	dialog_token = fst_group_assign_dialog_token(s->group);
	fsts_id = fst_group_assign_fsts_id(s->group);

	os_memset(&req, 0, sizeof(req));

	fst_printf_siface(s, s->data.old_iface, MSG_INFO,
		"initiating FST setup for %s (llt=%u ms)",
		fst_iface_get_name(s->data.new_iface), s->data.llt_ms);

	req.action = FST_ACTION_SETUP_REQUEST;
	req.dialog_token = dialog_token;
	req.llt = host_to_le32(FST_LLT_MS_TO_VAL(s->data.llt_ms));
	/* 8.4.2.147 Session Transition element */
	req.stie.element_id = WLAN_EID_SESSION_TRANSITION;
	req.stie.length = sizeof(req.stie) - 2;
	req.stie.fsts_id = host_to_le32(fsts_id);
	req.stie.session_control = SESSION_CONTROL(SESSION_TYPE_BSS, 0);

	req.stie.new_band_id = fst_iface_get_band_id(s->data.new_iface);
	req.stie.new_band_op = 1;
	req.stie.new_band_setup = 0;

	req.stie.old_band_id = fst_iface_get_band_id(s->data.old_iface);
	req.stie.old_band_op = 1;
	req.stie.old_band_setup = 0;

	res = fst_session_send_action(s, TRUE, &req, sizeof(req),
				      fst_iface_get_mbie(s->data.old_iface));
	if (!res) {
		s->data.fsts_id = fsts_id;
		s->data.pending_setup_req_dlgt = dialog_token;
		fst_printf_sframe(s, TRUE, MSG_INFO, "FST Setup Request sent");
		fst_session_set_state(s, FST_SESSION_STATE_SETUP_COMPLETION,
				      NULL);

		fst_session_stt_arm(s);
	}

	return res;
}


int fst_session_respond(struct fst_session *s, u8 status_code)
{
	struct fst_setup_res res;
	enum hostapd_hw_mode hw_mode;
	u8 channel;

	if (!fst_session_is_ready_pending(s)) {
		fst_printf_session(s, MSG_ERROR, "incorrect state: %s",
				   fst_session_state_name(s->state));
		return -EINVAL;
	}

	if (is_zero_ether_addr(s->data.old_peer_addr)) {
		fst_printf_session(s, MSG_ERROR, "No peer MAC address");
		return -EINVAL;
	}

	if (!s->data.old_iface) {
		fst_printf_session(s, MSG_ERROR, "No old interface defined");
		return -EINVAL;
	}

	if (!s->data.new_iface) {
		fst_printf_session(s, MSG_ERROR, "No new interface defined");
		return -EINVAL;
	}

	if (s->data.new_iface == s->data.old_iface) {
		fst_printf_session(s, MSG_ERROR,
				   "Same interface set as old and new");
		return -EINVAL;
	}

	if (!fst_iface_is_connected(s->data.old_iface,
				    s->data.old_peer_addr, FALSE)) {
		fst_printf_session(s, MSG_ERROR,
				   "The preset peer address is not in the peer list");
		return -EINVAL;
	}

	fst_session_stt_disarm(s);

	os_memset(&res, 0, sizeof(res));

	res.action = FST_ACTION_SETUP_RESPONSE;
	res.dialog_token = s->data.pending_setup_req_dlgt;
	res.status_code = status_code;

	res.stie.element_id = WLAN_EID_SESSION_TRANSITION;
	res.stie.length = sizeof(res.stie) - 2;

	if (status_code == WLAN_STATUS_SUCCESS) {
		res.stie.fsts_id = host_to_le32(s->data.fsts_id);
		res.stie.session_control = SESSION_CONTROL(SESSION_TYPE_BSS, 0);

		fst_iface_get_channel_info(s->data.new_iface, &hw_mode,
					   &channel);
		res.stie.new_band_id = fst_hw_mode_to_band(hw_mode);
		res.stie.new_band_op = 1;
		res.stie.new_band_setup = 0;

		fst_iface_get_channel_info(s->data.old_iface, &hw_mode,
					   &channel);
		res.stie.old_band_id = fst_hw_mode_to_band(hw_mode);
		res.stie.old_band_op = 1;
		res.stie.old_band_setup = 0;

		fst_printf_session(s, MSG_INFO,
				   "%s: FST Setup Request accepted for %s (llt=%u)",
				   fst_iface_get_name(s->data.old_iface),
				   fst_iface_get_name(s->data.new_iface),
				   s->data.llt_ms);
	} else {
		fst_printf_session(s, MSG_WARNING,
				   "%s: FST Setup Request rejected with code %d",
				   fst_iface_get_name(s->data.old_iface),
				   status_code);
	}

	if (fst_session_send_action(s, TRUE, &res, sizeof(res),
				    fst_iface_get_mbie(s->data.old_iface))) {
		fst_printf_sframe(s, TRUE, MSG_ERROR,
				  "cannot send FST Setup Response with code %d",
				  status_code);
		return -EINVAL;
	}

	fst_printf_sframe(s, TRUE, MSG_INFO, "FST Setup Response sent");

	if (status_code != WLAN_STATUS_SUCCESS) {
		union fst_session_state_switch_extra evext = {
			.to_initial = {
				.reason = REASON_REJECT,
				.reject_code = status_code,
				.initiator = FST_INITIATOR_LOCAL,
			},
		};
		fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);
	}

	return 0;
}


int fst_session_initiate_switch(struct fst_session *s)
{
	struct fst_ack_req req;
	int res;
	u8 dialog_token;

	if (!fst_session_is_ready(s)) {
		fst_printf_session(s, MSG_ERROR,
				   "cannot initiate switch due to wrong setup state (%d)",
				   s->state);
		return -1;
	}

	dialog_token = fst_group_assign_dialog_token(s->group);

	WPA_ASSERT(s->data.new_iface != NULL);
	WPA_ASSERT(s->data.old_iface != NULL);

	fst_printf_session(s, MSG_INFO, "initiating FST switch: %s => %s",
			   fst_iface_get_name(s->data.old_iface),
			   fst_iface_get_name(s->data.new_iface));

	os_memset(&req, 0, sizeof(req));

	req.action = FST_ACTION_ACK_REQUEST;
	req.dialog_token = dialog_token;
	req.fsts_id = host_to_le32(s->data.fsts_id);

	res = fst_session_send_action(s, FALSE, &req, sizeof(req), NULL);
	if (!res) {
		fst_printf_sframe(s, FALSE, MSG_INFO, "FST Ack Request sent");
		fst_session_set_state(s, FST_SESSION_STATE_TRANSITION_DONE,
				      NULL);
		fst_session_stt_arm(s);
	} else {
		fst_printf_sframe(s, FALSE, MSG_ERROR,
				  "Cannot send FST Ack Request");
	}

	return res;
}


void fst_session_handle_action(struct fst_session *s,
			       struct fst_iface *iface,
			       const struct ieee80211_mgmt *mgmt,
			       size_t frame_len)
{
	switch (mgmt->u.action.u.fst_action.action) {
	case FST_ACTION_SETUP_REQUEST:
		WPA_ASSERT(0);
		break;
	case FST_ACTION_SETUP_RESPONSE:
		fst_session_handle_setup_response(s, iface, mgmt, frame_len);
		break;
	case FST_ACTION_TEAR_DOWN:
		fst_session_handle_tear_down(s, iface, mgmt, frame_len);
		break;
	case FST_ACTION_ACK_REQUEST:
		fst_session_handle_ack_request(s, iface, mgmt, frame_len);
		break;
	case FST_ACTION_ACK_RESPONSE:
		fst_session_handle_ack_response(s, iface, mgmt, frame_len);
		break;
	case FST_ACTION_ON_CHANNEL_TUNNEL:
	default:
		fst_printf_sframe(s, FALSE, MSG_ERROR,
				  "Unsupported FST Action frame");
		break;
	}
}


int fst_session_tear_down_setup(struct fst_session *s)
{
	int res;
	union fst_session_state_switch_extra evext = {
		.to_initial = {
			.reason = REASON_TEARDOWN,
			.initiator = FST_INITIATOR_LOCAL,
		},
	};

	res = fst_session_send_tear_down(s);

	fst_session_set_state(s, FST_SESSION_STATE_INITIAL, &evext);

	return res;
}


void fst_session_reset(struct fst_session *s)
{
	fst_session_reset_ex(s, REASON_RESET);
}


void fst_session_delete(struct fst_session *s)
{
	fst_printf(MSG_INFO, "Session %u deleted", s->id);
	dl_list_del(&s->global_sessions_lentry);
	foreach_fst_ctrl_call(on_session_removed, s);
	os_free(s);
}


struct fst_group * fst_session_get_group(struct fst_session *s)
{
	return s->group;
}


struct fst_iface * fst_session_get_iface(struct fst_session *s, Boolean is_old)
{
	return is_old ? s->data.old_iface : s->data.new_iface;
}


u32 fst_session_get_id(struct fst_session *s)
{
	return s->id;
}


const u8 * fst_session_get_peer_addr(struct fst_session *s, Boolean is_old)
{
	return is_old ? s->data.old_peer_addr : s->data.new_peer_addr;
}


u32 fst_session_get_llt(struct fst_session *s)
{
	return s->data.llt_ms;
}


enum fst_session_state fst_session_get_state(struct fst_session *s)
{
	return s->state;
}


struct fst_session * fst_session_get_by_id(u32 id)
{
	struct fst_session *s;

	foreach_fst_session(s) {
		if (id == s->id)
			return s;
	}

	return NULL;
}


void fst_session_enum(struct fst_group *g, fst_session_enum_clb clb, void *ctx)
{
	struct fst_session *s;

	foreach_fst_session(s) {
		if (!g || s->group == g)
			clb(s->group, s, ctx);
	}
}


void fst_session_on_action_rx(struct fst_iface *iface,
			      const struct ieee80211_mgmt *mgmt,
			      size_t len)
{
	struct fst_session *s;

	if (len < IEEE80211_HDRLEN + 2 ||
	    mgmt->u.action.category != WLAN_ACTION_FST) {
		fst_printf_iface(iface, MSG_ERROR,
				 "invalid Action frame received");
		return;
	}

	if (mgmt->u.action.u.fst_action.action <= FST_ACTION_MAX_SUPPORTED) {
		fst_printf_iface(iface, MSG_DEBUG,
				 "FST Action '%s' received!",
				 fst_action_names[mgmt->u.action.u.fst_action.action]);
	} else {
		fst_printf_iface(iface, MSG_WARNING,
				 "unknown FST Action (%u) received!",
				 mgmt->u.action.u.fst_action.action);
		return;
	}

	if (mgmt->u.action.u.fst_action.action == FST_ACTION_SETUP_REQUEST) {
		fst_session_handle_setup_request(iface, mgmt, len);
		return;
	}

	s = fst_find_session_in_progress(mgmt->sa, fst_iface_get_group(iface));
	if (s) {
		fst_session_handle_action(s, iface, mgmt, len);
	} else {
		fst_printf_iface(iface, MSG_WARNING,
				 "FST Action '%s' dropped: no session in progress found",
				 fst_action_names[mgmt->u.action.u.fst_action.action]);
	}
}


int fst_session_set_str_ifname(struct fst_session *s, const char *ifname,
			       Boolean is_old)
{
	struct fst_group *g = fst_session_get_group(s);
	struct fst_iface *i;

	i = fst_group_get_iface_by_name(g, ifname);
	if (!i) {
		fst_printf_session(s, MSG_WARNING,
				   "Cannot set iface %s: no such iface within group '%s'",
				   ifname, fst_group_get_id(g));
		return -1;
	}

	fst_session_set_iface(s, i, is_old);

	return 0;
}


int fst_session_set_str_peer_addr(struct fst_session *s, const char *mac,
				  Boolean is_old)
{
	u8 peer_addr[ETH_ALEN];
	int res = fst_read_peer_addr(mac, peer_addr);

	if (res)
		return res;

	fst_session_set_peer_addr(s, peer_addr, is_old);

	return 0;
}


int fst_session_set_str_llt(struct fst_session *s, const char *llt_str)
{
	char *endp;
	long int llt = strtol(llt_str, &endp, 0);

	if (*endp || llt < 0 || (unsigned long int) llt > FST_MAX_LLT_MS) {
		fst_printf_session(s, MSG_WARNING,
				   "Cannot set llt %s: Invalid llt value (1..%u expected)",
				   llt_str, FST_MAX_LLT_MS);
		return -1;
	}
	fst_session_set_llt(s, (u32) llt);

	return 0;
}


void fst_session_global_on_iface_detached(struct fst_iface *iface)
{
	struct fst_session *s;

	foreach_fst_session(s) {
		if (fst_session_is_in_progress(s) &&
		    (s->data.new_iface == iface ||
		     s->data.old_iface == iface))
			fst_session_reset_ex(s, REASON_DETACH_IFACE);
	}
}


struct fst_session * fst_session_global_get_first_by_group(struct fst_group *g)
{
	struct fst_session *s;

	foreach_fst_session(s) {
		if (s->group == g)
			return s;
	}

	return NULL;
}


#ifdef CONFIG_FST_TEST

static int get_group_fill_session(struct fst_group **g, struct fst_session *s)
{
	const u8 *old_addr, *new_addr;
	struct fst_get_peer_ctx *ctx;

	os_memset(s, 0, sizeof(*s));
	foreach_fst_group(*g) {
		s->data.new_iface = fst_group_first_iface(*g);
		if (s->data.new_iface)
			break;
	}
	if (!s->data.new_iface)
		return -EINVAL;

	s->data.old_iface = dl_list_entry(s->data.new_iface->group_lentry.next,
					  struct fst_iface, group_lentry);
	if (!s->data.old_iface)
		return -EINVAL;

	old_addr = fst_iface_get_peer_first(s->data.old_iface, &ctx, TRUE);
	if (!old_addr)
		return -EINVAL;

	new_addr = fst_iface_get_peer_first(s->data.new_iface, &ctx, TRUE);
	if (!new_addr)
		return -EINVAL;

	os_memcpy(s->data.old_peer_addr, old_addr, ETH_ALEN);
	os_memcpy(s->data.new_peer_addr, new_addr, ETH_ALEN);

	return 0;
}


#define FST_MAX_COMMAND_WORD_NAME_LENGTH 16

int fst_test_req_send_fst_request(const char *params)
{
	int fsts_id;
	Boolean is_valid;
	char *endp;
	struct fst_setup_req req;
	struct fst_session s;
	struct fst_group *g;
	enum hostapd_hw_mode hw_mode;
	u8 channel;
	char additional_param[FST_MAX_COMMAND_WORD_NAME_LENGTH];

	if (params[0] != ' ')
		return -EINVAL;
	params++;
	fsts_id = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return -EINVAL;

	if (get_group_fill_session(&g, &s))
		return -EINVAL;

	req.action = FST_ACTION_SETUP_REQUEST;
	req.dialog_token = g->dialog_token;
	req.llt = host_to_le32(FST_LLT_MS_DEFAULT);
	/* 8.4.2.147 Session Transition element */
	req.stie.element_id = WLAN_EID_SESSION_TRANSITION;
	req.stie.length = sizeof(req.stie) - 2;
	req.stie.fsts_id = host_to_le32(fsts_id);
	req.stie.session_control = SESSION_CONTROL(SESSION_TYPE_BSS, 0);

	fst_iface_get_channel_info(s.data.new_iface, &hw_mode, &channel);
	req.stie.new_band_id = fst_hw_mode_to_band(hw_mode);
	req.stie.new_band_op = 1;
	req.stie.new_band_setup = 0;

	fst_iface_get_channel_info(s.data.old_iface, &hw_mode, &channel);
	req.stie.old_band_id = fst_hw_mode_to_band(hw_mode);
	req.stie.old_band_op = 1;
	req.stie.old_band_setup = 0;

	if (!fst_read_next_text_param(endp, additional_param,
				       sizeof(additional_param), &endp)) {
		if (!os_strcasecmp(additional_param, FST_CTR_PVAL_BAD_NEW_BAND))
			req.stie.new_band_id = req.stie.old_band_id;
	}

	return fst_session_send_action(&s, TRUE, &req, sizeof(req),
				       s.data.old_iface->mb_ie);
}


int fst_test_req_send_fst_response(const char *params)
{
	int fsts_id;
	Boolean is_valid;
	char *endp;
	struct fst_setup_res res;
	struct fst_session s;
	struct fst_group *g;
	enum hostapd_hw_mode hw_mode;
	u8 status_code;
	u8 channel;
	char response[FST_MAX_COMMAND_WORD_NAME_LENGTH];
	struct fst_session *_s;

	if (params[0] != ' ')
		return -EINVAL;
	params++;
	fsts_id = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return -EINVAL;

	if (get_group_fill_session(&g, &s))
		return -EINVAL;

	status_code = WLAN_STATUS_SUCCESS;
	if (!fst_read_next_text_param(endp, response, sizeof(response),
				      &endp)) {
		if (!os_strcasecmp(response, FST_CS_PVAL_RESPONSE_REJECT))
			status_code = WLAN_STATUS_PENDING_ADMITTING_FST_SESSION;
	}

	os_memset(&res, 0, sizeof(res));

	res.action = FST_ACTION_SETUP_RESPONSE;
	/*
	 * If some session has just received an FST Setup Request, then
	 * use the correct dialog token copied from this request.
	 */
	_s = fst_find_session_in_progress(fst_session_get_peer_addr(&s, TRUE),
					  g);
	res.dialog_token = (_s && fst_session_is_ready_pending(_s)) ?
		_s->data.pending_setup_req_dlgt : g->dialog_token;
	res.status_code  = status_code;

	res.stie.element_id = WLAN_EID_SESSION_TRANSITION;
	res.stie.length = sizeof(res.stie) - 2;

	if (res.status_code == WLAN_STATUS_SUCCESS) {
		res.stie.fsts_id = host_to_le32(fsts_id);
		res.stie.session_control = SESSION_CONTROL(SESSION_TYPE_BSS, 0);

		fst_iface_get_channel_info(s.data.new_iface, &hw_mode,
					    &channel);
		res.stie.new_band_id = fst_hw_mode_to_band(hw_mode);
		res.stie.new_band_op = 1;
		res.stie.new_band_setup = 0;

		fst_iface_get_channel_info(s.data.old_iface, &hw_mode,
					   &channel);
		res.stie.old_band_id = fst_hw_mode_to_band(hw_mode);
		res.stie.old_band_op = 1;
		res.stie.old_band_setup = 0;
	}

	if (!fst_read_next_text_param(endp, response, sizeof(response),
				      &endp)) {
		if (!os_strcasecmp(response, FST_CTR_PVAL_BAD_NEW_BAND))
			res.stie.new_band_id = res.stie.old_band_id;
	}

	return fst_session_send_action(&s, TRUE, &res, sizeof(res),
				       s.data.old_iface->mb_ie);
}


int fst_test_req_send_ack_request(const char *params)
{
	int fsts_id;
	Boolean is_valid;
	char *endp;
	struct fst_ack_req req;
	struct fst_session s;
	struct fst_group *g;

	if (params[0] != ' ')
		return -EINVAL;
	params++;
	fsts_id = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return -EINVAL;

	if (get_group_fill_session(&g, &s))
		return -EINVAL;

	os_memset(&req, 0, sizeof(req));
	req.action = FST_ACTION_ACK_REQUEST;
	req.dialog_token = g->dialog_token;
	req.fsts_id = host_to_le32(fsts_id);

	return fst_session_send_action(&s, FALSE, &req, sizeof(req), NULL);
}


int fst_test_req_send_ack_response(const char *params)
{
	int fsts_id;
	Boolean is_valid;
	char *endp;
	struct fst_ack_res res;
	struct fst_session s;
	struct fst_group *g;

	if (params[0] != ' ')
		return -EINVAL;
	params++;
	fsts_id = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return -EINVAL;

	if (get_group_fill_session(&g, &s))
		return -EINVAL;

	os_memset(&res, 0, sizeof(res));
	res.action = FST_ACTION_ACK_RESPONSE;
	res.dialog_token = g->dialog_token;
	res.fsts_id = host_to_le32(fsts_id);

	return fst_session_send_action(&s, FALSE, &res, sizeof(res), NULL);
}


int fst_test_req_send_tear_down(const char *params)
{
	int fsts_id;
	Boolean is_valid;
	char *endp;
	struct fst_tear_down td;
	struct fst_session s;
	struct fst_group *g;

	if (params[0] != ' ')
		return -EINVAL;
	params++;
	fsts_id = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return -EINVAL;

	if (get_group_fill_session(&g, &s))
		return -EINVAL;

	os_memset(&td, 0, sizeof(td));
	td.action = FST_ACTION_TEAR_DOWN;
	td.fsts_id = host_to_le32(fsts_id);

	return fst_session_send_action(&s, TRUE, &td, sizeof(td), NULL);
}


u32 fst_test_req_get_fsts_id(const char *params)
{
	int sid;
	Boolean is_valid;
	char *endp;
	struct fst_session *s;

	if (params[0] != ' ')
		return FST_FSTS_ID_NOT_FOUND;
	params++;
	sid = fst_read_next_int_param(params, &is_valid, &endp);
	if (!is_valid)
		return FST_FSTS_ID_NOT_FOUND;

	s = fst_session_get_by_id(sid);
	if (!s)
		return FST_FSTS_ID_NOT_FOUND;

	return s->data.fsts_id;
}


int fst_test_req_get_local_mbies(const char *request, char *buf, size_t buflen)
{
	char *endp;
	char ifname[FST_MAX_COMMAND_WORD_NAME_LENGTH];
	struct fst_group *g;
	struct fst_iface *iface;

	if (request[0] != ' ')
		return -EINVAL;
	request++;
	if (fst_read_next_text_param(request, ifname, sizeof(ifname), &endp) ||
	    !*ifname)
		goto problem;
	g = dl_list_first(&fst_global_groups_list, struct fst_group,
			  global_groups_lentry);
	if (!g)
		goto problem;
	iface = fst_group_get_iface_by_name(g, ifname);
	if (!iface || !iface->mb_ie)
		goto problem;
	return wpa_snprintf_hex(buf, buflen, wpabuf_head(iface->mb_ie),
				wpabuf_len(iface->mb_ie));

problem:
	return os_snprintf(buf, buflen, "FAIL\n");
}

#endif /* CONFIG_FST_TEST */
