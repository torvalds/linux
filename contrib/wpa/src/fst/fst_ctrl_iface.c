/*
 * FST module - Control Interface implementation
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "list.h"
#include "fst/fst.h"
#include "fst/fst_internal.h"
#include "fst_ctrl_defs.h"
#include "fst_ctrl_iface.h"


static struct fst_group * get_fst_group_by_id(const char *id)
{
	struct fst_group *g;

	foreach_fst_group(g) {
		const char *group_id = fst_group_get_id(g);

		if (os_strncmp(group_id, id, os_strlen(group_id)) == 0)
			return g;
	}

	return NULL;
}


/* notifications */
static Boolean format_session_state_extra(const union fst_event_extra *extra,
					  char *buffer, size_t size)
{
	int len;
	char reject_str[32] = FST_CTRL_PVAL_NONE;
	const char *initiator = FST_CTRL_PVAL_NONE;
	const struct fst_event_extra_session_state *ss;

	ss = &extra->session_state;
	if (ss->new_state != FST_SESSION_STATE_INITIAL)
		return TRUE;

	switch (ss->extra.to_initial.reason) {
	case REASON_REJECT:
		if (ss->extra.to_initial.reject_code != WLAN_STATUS_SUCCESS)
			os_snprintf(reject_str, sizeof(reject_str), "%u",
				    ss->extra.to_initial.reject_code);
		/* fall through */
	case REASON_TEARDOWN:
	case REASON_SWITCH:
		switch (ss->extra.to_initial.initiator) {
		case FST_INITIATOR_LOCAL:
			initiator = FST_CS_PVAL_INITIATOR_LOCAL;
			break;
		case FST_INITIATOR_REMOTE:
			initiator = FST_CS_PVAL_INITIATOR_REMOTE;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	len = os_snprintf(buffer, size,
			  FST_CES_PNAME_REASON "=%s "
			  FST_CES_PNAME_REJECT_CODE "=%s "
			  FST_CES_PNAME_INITIATOR "=%s",
			  fst_reason_name(ss->extra.to_initial.reason),
			  reject_str, initiator);

	return !os_snprintf_error(size, len);
}


static void fst_ctrl_iface_notify(struct fst_iface *f, u32 session_id,
				  enum fst_event_type event_type,
				  const union fst_event_extra *extra)
{
	struct fst_group *g;
	char extra_str[128] = "";
	const struct fst_event_extra_session_state *ss;
	const struct fst_event_extra_iface_state *is;
	const struct fst_event_extra_peer_state *ps;

	/*
	 * FST can use any of interface objects as it only sends messages
	 * on global Control Interface, so we just pick the 1st one.
	 */

	if (!f) {
		foreach_fst_group(g) {
			f = fst_group_first_iface(g);
			if (f)
				break;
		}
		if (!f)
			return;
	}

	WPA_ASSERT(f->iface_obj.ctx);

	switch (event_type) {
	case EVENT_FST_IFACE_STATE_CHANGED:
		if (!extra)
			return;
		is = &extra->iface_state;
		wpa_msg_global_only(f->iface_obj.ctx, MSG_INFO,
				    FST_CTRL_EVENT_IFACE " %s "
				    FST_CEI_PNAME_IFNAME "=%s "
				    FST_CEI_PNAME_GROUP "=%s",
				    is->attached ? FST_CEI_PNAME_ATTACHED :
				    FST_CEI_PNAME_DETACHED,
				    is->ifname, is->group_id);
		break;
	case EVENT_PEER_STATE_CHANGED:
		if (!extra)
			return;
		ps = &extra->peer_state;
		wpa_msg_global_only(fst_iface_get_wpa_obj_ctx(f), MSG_INFO,
				    FST_CTRL_EVENT_PEER " %s "
				    FST_CEP_PNAME_IFNAME "=%s "
				    FST_CEP_PNAME_ADDR "=" MACSTR,
				    ps->connected ? FST_CEP_PNAME_CONNECTED :
				    FST_CEP_PNAME_DISCONNECTED,
				    ps->ifname, MAC2STR(ps->addr));
		break;
	case EVENT_FST_SESSION_STATE_CHANGED:
		if (!extra)
			return;
		if (!format_session_state_extra(extra, extra_str,
						sizeof(extra_str))) {
			fst_printf(MSG_ERROR,
				   "CTRL: Cannot format STATE_CHANGE extra");
			extra_str[0] = 0;
		}
		ss = &extra->session_state;
		wpa_msg_global_only(fst_iface_get_wpa_obj_ctx(f), MSG_INFO,
				    FST_CTRL_EVENT_SESSION " "
				    FST_CES_PNAME_SESSION_ID "=%u "
				    FST_CES_PNAME_EVT_TYPE "=%s "
				    FST_CES_PNAME_OLD_STATE "=%s "
				    FST_CES_PNAME_NEW_STATE "=%s %s",
				    session_id,
				    fst_session_event_type_name(event_type),
				    fst_session_state_name(ss->old_state),
				    fst_session_state_name(ss->new_state),
				    extra_str);
		break;
	case EVENT_FST_ESTABLISHED:
	case EVENT_FST_SETUP:
		wpa_msg_global_only(fst_iface_get_wpa_obj_ctx(f), MSG_INFO,
				    FST_CTRL_EVENT_SESSION " "
				    FST_CES_PNAME_SESSION_ID "=%u "
				    FST_CES_PNAME_EVT_TYPE "=%s",
				    session_id,
				    fst_session_event_type_name(event_type));
		break;
	}
}


/* command processors */

/* fst session_get */
static int session_get(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	struct fst_iface *new_iface, *old_iface;
	const u8 *old_peer_addr, *new_peer_addr;
	u32 id;

	id = strtoul(session_id, NULL, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	old_peer_addr = fst_session_get_peer_addr(s, TRUE);
	new_peer_addr = fst_session_get_peer_addr(s, FALSE);
	new_iface = fst_session_get_iface(s, FALSE);
	old_iface = fst_session_get_iface(s, TRUE);

	return os_snprintf(buf, buflen,
			   FST_CSG_PNAME_OLD_PEER_ADDR "=" MACSTR "\n"
			   FST_CSG_PNAME_NEW_PEER_ADDR "=" MACSTR "\n"
			   FST_CSG_PNAME_NEW_IFNAME "=%s\n"
			   FST_CSG_PNAME_OLD_IFNAME "=%s\n"
			   FST_CSG_PNAME_LLT "=%u\n"
			   FST_CSG_PNAME_STATE "=%s\n",
			   MAC2STR(old_peer_addr),
			   MAC2STR(new_peer_addr),
			   new_iface ? fst_iface_get_name(new_iface) :
			   FST_CTRL_PVAL_NONE,
			   old_iface ? fst_iface_get_name(old_iface) :
			   FST_CTRL_PVAL_NONE,
			   fst_session_get_llt(s),
			   fst_session_state_name(fst_session_get_state(s)));
}


/* fst session_set */
static int session_set(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	char *p, *q;
	u32 id;
	int ret;

	id = strtoul(session_id, &p, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (*p != ' ' || !(q = os_strchr(p + 1, '=')))
		return os_snprintf(buf, buflen, "FAIL\n");
	p++;

	if (os_strncasecmp(p, FST_CSS_PNAME_OLD_IFNAME, q - p) == 0) {
		ret = fst_session_set_str_ifname(s, q + 1, TRUE);
	} else if (os_strncasecmp(p, FST_CSS_PNAME_NEW_IFNAME, q - p) == 0) {
		ret = fst_session_set_str_ifname(s, q + 1, FALSE);
	} else if (os_strncasecmp(p, FST_CSS_PNAME_OLD_PEER_ADDR, q - p) == 0) {
		ret = fst_session_set_str_peer_addr(s, q + 1, TRUE);
	} else if (os_strncasecmp(p, FST_CSS_PNAME_NEW_PEER_ADDR, q - p) == 0) {
		ret = fst_session_set_str_peer_addr(s, q + 1, FALSE);
	} else if (os_strncasecmp(p, FST_CSS_PNAME_LLT, q - p) == 0) {
		ret = fst_session_set_str_llt(s, q + 1);
	} else {
		fst_printf(MSG_ERROR, "CTRL: Unknown parameter: %s", p);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "%s\n", ret ? "FAIL" : "OK");
}


/* fst session_add/remove */
static int session_add(const char *group_id, char *buf, size_t buflen)
{
	struct fst_group *g;
	struct fst_session *s;

	g = get_fst_group_by_id(group_id);
	if (!g) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find group '%s'",
			   group_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	s = fst_session_create(g);
	if (!s) {
		fst_printf(MSG_ERROR,
			   "CTRL: Cannot create session for group '%s'",
			   group_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "%u\n", fst_session_get_id(s));
}


static int session_remove(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	struct fst_group *g;
	u32 id;

	id = strtoul(session_id, NULL, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	g = fst_session_get_group(s);
	fst_session_reset(s);
	fst_session_delete(s);
	fst_group_delete_if_empty(g);

	return os_snprintf(buf, buflen, "OK\n");
}


/* fst session_initiate */
static int session_initiate(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	u32 id;

	id = strtoul(session_id, NULL, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (fst_session_initiate_setup(s)) {
		fst_printf(MSG_WARNING, "CTRL: Cannot initiate session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "OK\n");
}


/* fst session_respond */
static int session_respond(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	char *p;
	u32 id;
	u8 status_code;

	id = strtoul(session_id, &p, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (*p != ' ')
		return os_snprintf(buf, buflen, "FAIL\n");
	p++;

	if (!os_strcasecmp(p, FST_CS_PVAL_RESPONSE_ACCEPT)) {
		status_code = WLAN_STATUS_SUCCESS;
	} else if (!os_strcasecmp(p, FST_CS_PVAL_RESPONSE_REJECT)) {
		status_code = WLAN_STATUS_PENDING_ADMITTING_FST_SESSION;
	} else {
		fst_printf(MSG_WARNING,
			   "CTRL: session %u: unknown response status: %s",
			   id, p);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (fst_session_respond(s, status_code)) {
		fst_printf(MSG_WARNING, "CTRL: Cannot respond to session %u",
			   id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	fst_printf(MSG_INFO, "CTRL: session %u responded", id);

	return os_snprintf(buf, buflen, "OK\n");
}


/* fst session_transfer */
static int session_transfer(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	u32 id;

	id = strtoul(session_id, NULL, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (fst_session_initiate_switch(s)) {
		fst_printf(MSG_WARNING,
			   "CTRL: Cannot initiate ST for session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "OK\n");
}


/* fst session_teardown */
static int session_teardown(const char *session_id, char *buf, size_t buflen)
{
	struct fst_session *s;
	u32 id;

	id = strtoul(session_id, NULL, 0);

	s = fst_session_get_by_id(id);
	if (!s) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find session %u", id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	if (fst_session_tear_down_setup(s)) {
		fst_printf(MSG_WARNING, "CTRL: Cannot tear down session %u",
			   id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "OK\n");
}


#ifdef CONFIG_FST_TEST
/* fst test_request */
static int test_request(const char *request, char *buf, size_t buflen)
{
	const char *p = request;
	int ret;

	if (!os_strncasecmp(p, FST_CTR_SEND_SETUP_REQUEST,
			    os_strlen(FST_CTR_SEND_SETUP_REQUEST))) {
		ret = fst_test_req_send_fst_request(
			p + os_strlen(FST_CTR_SEND_SETUP_REQUEST));
	} else if (!os_strncasecmp(p, FST_CTR_SEND_SETUP_RESPONSE,
				   os_strlen(FST_CTR_SEND_SETUP_RESPONSE))) {
		ret = fst_test_req_send_fst_response(
			p + os_strlen(FST_CTR_SEND_SETUP_RESPONSE));
	} else if (!os_strncasecmp(p, FST_CTR_SEND_ACK_REQUEST,
				   os_strlen(FST_CTR_SEND_ACK_REQUEST))) {
		ret = fst_test_req_send_ack_request(
			p + os_strlen(FST_CTR_SEND_ACK_REQUEST));
	} else if (!os_strncasecmp(p, FST_CTR_SEND_ACK_RESPONSE,
				   os_strlen(FST_CTR_SEND_ACK_RESPONSE))) {
		ret = fst_test_req_send_ack_response(
			p + os_strlen(FST_CTR_SEND_ACK_RESPONSE));
	} else if (!os_strncasecmp(p, FST_CTR_SEND_TEAR_DOWN,
				   os_strlen(FST_CTR_SEND_TEAR_DOWN))) {
		ret = fst_test_req_send_tear_down(
			p + os_strlen(FST_CTR_SEND_TEAR_DOWN));
	} else if (!os_strncasecmp(p, FST_CTR_GET_FSTS_ID,
				   os_strlen(FST_CTR_GET_FSTS_ID))) {
		u32 fsts_id = fst_test_req_get_fsts_id(
			p + os_strlen(FST_CTR_GET_FSTS_ID));
		if (fsts_id != FST_FSTS_ID_NOT_FOUND)
			return os_snprintf(buf, buflen, "%u\n", fsts_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	} else if (!os_strncasecmp(p, FST_CTR_GET_LOCAL_MBIES,
				   os_strlen(FST_CTR_GET_LOCAL_MBIES))) {
		return fst_test_req_get_local_mbies(
			p + os_strlen(FST_CTR_GET_LOCAL_MBIES), buf, buflen);
	} else if (!os_strncasecmp(p, FST_CTR_IS_SUPPORTED,
				   os_strlen(FST_CTR_IS_SUPPORTED))) {
		ret = 0;
	} else {
		fst_printf(MSG_ERROR, "CTRL: Unknown parameter: %s", p);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	return os_snprintf(buf, buflen, "%s\n", ret ? "FAIL" : "OK");
}
#endif /* CONFIG_FST_TEST */


/* fst list_sessions */
struct list_sessions_cb_ctx {
	char *buf;
	size_t buflen;
	size_t reply_len;
};


static void list_session_enum_cb(struct fst_group *g, struct fst_session *s,
				 void *ctx)
{
	struct list_sessions_cb_ctx *c = ctx;
	int ret;

	ret = os_snprintf(c->buf, c->buflen, " %u", fst_session_get_id(s));

	c->buf += ret;
	c->buflen -= ret;
	c->reply_len += ret;
}


static int list_sessions(const char *group_id, char *buf, size_t buflen)
{
	struct list_sessions_cb_ctx ctx;
	struct fst_group *g;

	g = get_fst_group_by_id(group_id);
	if (!g) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find group '%s'",
			   group_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	ctx.buf = buf;
	ctx.buflen = buflen;
	ctx.reply_len = 0;

	fst_session_enum(g, list_session_enum_cb, &ctx);

	ctx.reply_len += os_snprintf(buf + ctx.reply_len, ctx.buflen, "\n");

	return ctx.reply_len;
}


/* fst iface_peers */
static int iface_peers(const char *group_id, char *buf, size_t buflen)
{
	const char *ifname;
	struct fst_group *g;
	struct fst_iface *f;
	struct fst_get_peer_ctx *ctx;
	const u8 *addr;
	unsigned found = 0;
	int ret = 0;

	g = get_fst_group_by_id(group_id);
	if (!g) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find group '%s'",
			   group_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	ifname = os_strchr(group_id, ' ');
	if (!ifname)
		return os_snprintf(buf, buflen, "FAIL\n");
	ifname++;

	foreach_fst_group_iface(g, f) {
		const char *in = fst_iface_get_name(f);

		if (os_strncmp(ifname, in, os_strlen(in)) == 0) {
			found = 1;
			break;
		}
	}

	if (!found)
		return os_snprintf(buf, buflen, "FAIL\n");

	addr = fst_iface_get_peer_first(f, &ctx, FALSE);
	for (; addr != NULL; addr = fst_iface_get_peer_next(f, &ctx, FALSE)) {
		int res;

		res = os_snprintf(buf + ret, buflen - ret, MACSTR "\n",
				  MAC2STR(addr));
		if (os_snprintf_error(buflen - ret, res))
			break;
		ret += res;
	}

	return ret;
}


static int get_peer_mbies(const char *params, char *buf, size_t buflen)
{
	char *endp;
	char ifname[FST_MAX_INTERFACE_SIZE];
	u8 peer_addr[ETH_ALEN];
	struct fst_group *g;
	struct fst_iface *iface = NULL;
	const struct wpabuf *mbies;

	if (fst_read_next_text_param(params, ifname, sizeof(ifname), &endp) ||
	    !*ifname)
		goto problem;

	while (isspace(*endp))
		endp++;
	if (fst_read_peer_addr(endp, peer_addr))
		goto problem;

	foreach_fst_group(g) {
		iface = fst_group_get_iface_by_name(g, ifname);
		if (iface)
			break;
	}
	if (!iface)
		goto problem;

	mbies = fst_iface_get_peer_mb_ie(iface, peer_addr);
	if (!mbies)
		goto problem;

	return wpa_snprintf_hex(buf, buflen, wpabuf_head(mbies),
				wpabuf_len(mbies));

problem:
	return os_snprintf(buf, buflen, "FAIL\n");
}


/* fst list_ifaces */
static int list_ifaces(const char *group_id, char *buf, size_t buflen)
{
	struct fst_group *g;
	struct fst_iface *f;
	int ret = 0;

	g = get_fst_group_by_id(group_id);
	if (!g) {
		fst_printf(MSG_WARNING, "CTRL: Cannot find group '%s'",
			   group_id);
		return os_snprintf(buf, buflen, "FAIL\n");
	}

	foreach_fst_group_iface(g, f) {
		int res;
		const u8 *iface_addr = fst_iface_get_addr(f);

		res = os_snprintf(buf + ret, buflen - ret,
				  "%s|" MACSTR "|%u|%u\n",
				  fst_iface_get_name(f),
				  MAC2STR(iface_addr),
				  fst_iface_get_priority(f),
				  fst_iface_get_llt(f));
		if (os_snprintf_error(buflen - ret, res))
			break;
		ret += res;
	}

	return ret;
}


/* fst list_groups */
static int list_groups(const char *cmd, char *buf, size_t buflen)
{
	struct fst_group *g;
	int ret = 0;

	foreach_fst_group(g) {
		int res;

		res = os_snprintf(buf + ret, buflen - ret, "%s\n",
				  fst_group_get_id(g));
		if (os_snprintf_error(buflen - ret, res))
			break;
		ret += res;
	}

	return ret;
}


static const char * band_freq(enum mb_band_id band)
{
	static const char *band_names[] = {
		[MB_BAND_ID_WIFI_2_4GHZ] = "2.4GHZ",
		[MB_BAND_ID_WIFI_5GHZ] = "5GHZ",
		[MB_BAND_ID_WIFI_60GHZ] = "60GHZ",
	};

	return fst_get_str_name(band, band_names, ARRAY_SIZE(band_names));
}


static int print_band(unsigned num, struct fst_iface *iface, const u8 *addr,
		      char *buf, size_t buflen)
{
	const struct wpabuf *wpabuf;
	enum hostapd_hw_mode hw_mode;
	u8 channel;
	int ret = 0;

	fst_iface_get_channel_info(iface, &hw_mode, &channel);

	ret += os_snprintf(buf + ret, buflen - ret, "band%u_frequency=%s\n",
			   num, band_freq(fst_hw_mode_to_band(hw_mode)));
	ret += os_snprintf(buf + ret, buflen - ret, "band%u_iface=%s\n",
			   num, fst_iface_get_name(iface));
	wpabuf = fst_iface_get_peer_mb_ie(iface, addr);
	if (wpabuf) {
		ret += os_snprintf(buf + ret, buflen - ret, "band%u_mb_ies=",
				   num);
		ret += wpa_snprintf_hex(buf + ret, buflen - ret,
					wpabuf_head(wpabuf),
					wpabuf_len(wpabuf));
		ret += os_snprintf(buf + ret, buflen - ret, "\n");
	}
	ret += os_snprintf(buf + ret, buflen - ret, "band%u_fst_group_id=%s\n",
			   num, fst_iface_get_group_id(iface));
	ret += os_snprintf(buf + ret, buflen - ret, "band%u_fst_priority=%u\n",
			   num, fst_iface_get_priority(iface));
	ret += os_snprintf(buf + ret, buflen - ret, "band%u_fst_llt=%u\n",
			   num, fst_iface_get_llt(iface));

	return ret;
}


static void fst_ctrl_iface_on_iface_state_changed(struct fst_iface *i,
						  Boolean attached)
{
	union fst_event_extra extra;

	os_memset(&extra, 0, sizeof(extra));
	extra.iface_state.attached = attached;
	os_strlcpy(extra.iface_state.ifname, fst_iface_get_name(i),
		   sizeof(extra.iface_state.ifname));
	os_strlcpy(extra.iface_state.group_id, fst_iface_get_group_id(i),
		   sizeof(extra.iface_state.group_id));

	fst_ctrl_iface_notify(i, FST_INVALID_SESSION_ID,
			      EVENT_FST_IFACE_STATE_CHANGED, &extra);
}


static int fst_ctrl_iface_on_iface_added(struct fst_iface *i)
{
	fst_ctrl_iface_on_iface_state_changed(i, TRUE);
	return 0;
}


static void fst_ctrl_iface_on_iface_removed(struct fst_iface *i)
{
	fst_ctrl_iface_on_iface_state_changed(i, FALSE);
}


static void fst_ctrl_iface_on_event(enum fst_event_type event_type,
				    struct fst_iface *i, struct fst_session *s,
				    const union fst_event_extra *extra)
{
	u32 session_id = s ? fst_session_get_id(s) : FST_INVALID_SESSION_ID;

	fst_ctrl_iface_notify(i, session_id, event_type, extra);
}


static const struct fst_ctrl ctrl_cli = {
	.on_iface_added = fst_ctrl_iface_on_iface_added,
	.on_iface_removed =  fst_ctrl_iface_on_iface_removed,
	.on_event = fst_ctrl_iface_on_event,
};

const struct fst_ctrl *fst_ctrl_cli = &ctrl_cli;


int fst_ctrl_iface_mb_info(const u8 *addr, char *buf, size_t buflen)
{
	struct fst_group *g;
	struct fst_iface *f;
	unsigned num = 0;
	int ret = 0;

	foreach_fst_group(g) {
		foreach_fst_group_iface(g, f) {
			if (fst_iface_is_connected(f, addr, TRUE)) {
				ret += print_band(num++, f, addr,
						  buf + ret, buflen - ret);
			}
		}
	}

	return ret;
}


/* fst ctrl processor */
int fst_ctrl_iface_receive(const char *cmd, char *reply, size_t reply_size)
{
	static const struct fst_command {
		const char *name;
		unsigned has_param;
		int (*process)(const char *group_id, char *buf, size_t buflen);
	} commands[] = {
		{ FST_CMD_LIST_GROUPS, 0, list_groups},
		{ FST_CMD_LIST_IFACES, 1, list_ifaces},
		{ FST_CMD_IFACE_PEERS, 1, iface_peers},
		{ FST_CMD_GET_PEER_MBIES, 1, get_peer_mbies},
		{ FST_CMD_LIST_SESSIONS, 1, list_sessions},
		{ FST_CMD_SESSION_ADD, 1, session_add},
		{ FST_CMD_SESSION_REMOVE, 1, session_remove},
		{ FST_CMD_SESSION_GET, 1, session_get},
		{ FST_CMD_SESSION_SET, 1, session_set},
		{ FST_CMD_SESSION_INITIATE, 1, session_initiate},
		{ FST_CMD_SESSION_RESPOND, 1, session_respond},
		{ FST_CMD_SESSION_TRANSFER, 1, session_transfer},
		{ FST_CMD_SESSION_TEARDOWN, 1, session_teardown},
#ifdef CONFIG_FST_TEST
		{ FST_CMD_TEST_REQUEST, 1, test_request },
#endif /* CONFIG_FST_TEST */
		{ NULL, 0, NULL }
	};
	const struct fst_command *c;
	const char *p;
	const char *temp;
	Boolean non_spaces_found;

	for (c = commands; c->name; c++) {
		if (os_strncasecmp(cmd, c->name, os_strlen(c->name)) != 0)
			continue;
		p = cmd + os_strlen(c->name);
		if (c->has_param) {
			if (!isspace(p[0]))
				return os_snprintf(reply, reply_size, "FAIL\n");
			p++;
			temp = p;
			non_spaces_found = FALSE;
			while (*temp) {
				if (!isspace(*temp)) {
					non_spaces_found = TRUE;
					break;
				}
				temp++;
			}
			if (!non_spaces_found)
				return os_snprintf(reply, reply_size, "FAIL\n");
		}
		return c->process(p, reply, reply_size);
	}

	return os_snprintf(reply, reply_size, "UNKNOWN FST COMMAND\n");
}


int fst_read_next_int_param(const char *params, Boolean *valid, char **endp)
{
	int ret = -1;
	const char *curp;

	*valid = FALSE;
	*endp = (char *) params;
	curp = params;
	if (*curp) {
		ret = (int) strtol(curp, endp, 0);
		if (!**endp || isspace(**endp))
			*valid = TRUE;
	}

	return ret;
}


int fst_read_next_text_param(const char *params, char *buf, size_t buflen,
			     char **endp)
{
	size_t max_chars_to_copy;
	char *cur_dest;

	*endp = (char *) params;
	while (isspace(**endp))
		(*endp)++;
	if (!**endp || buflen <= 1)
		return -EINVAL;

	max_chars_to_copy = buflen - 1;
	/* We need 1 byte for the terminating zero */
	cur_dest = buf;
	while (**endp && !isspace(**endp) && max_chars_to_copy > 0) {
		*cur_dest = **endp;
		(*endp)++;
		cur_dest++;
		max_chars_to_copy--;
	}
	*cur_dest = 0;

	return 0;
}


int fst_read_peer_addr(const char *mac, u8 *peer_addr)
{
	if (hwaddr_aton(mac, peer_addr)) {
		fst_printf(MSG_WARNING, "Bad peer_mac %s: invalid addr string",
			   mac);
		return -1;
	}

	if (is_zero_ether_addr(peer_addr) ||
	    is_multicast_ether_addr(peer_addr)) {
		fst_printf(MSG_WARNING, "Bad peer_mac %s: not a unicast addr",
			   mac);
		return -1;
	}

	return 0;
}


int fst_parse_attach_command(const char *cmd, char *ifname, size_t ifname_size,
			     struct fst_iface_cfg *cfg)
{
	char *pos;
	char *endp;
	Boolean is_valid;
	int val;

	if (fst_read_next_text_param(cmd, ifname, ifname_size, &endp) ||
	    fst_read_next_text_param(endp, cfg->group_id, sizeof(cfg->group_id),
				     &endp))
		return -EINVAL;

	cfg->llt = FST_DEFAULT_LLT_CFG_VALUE;
	cfg->priority = 0;
	pos = os_strstr(endp, FST_ATTACH_CMD_PNAME_LLT);
	if (pos) {
		pos += os_strlen(FST_ATTACH_CMD_PNAME_LLT);
		if (*pos == '=') {
			val = fst_read_next_int_param(pos + 1, &is_valid,
						      &endp);
			if (is_valid)
				cfg->llt = val;
		}
	}
	pos = os_strstr(endp, FST_ATTACH_CMD_PNAME_PRIORITY);
	if (pos) {
		pos += os_strlen(FST_ATTACH_CMD_PNAME_PRIORITY);
		if (*pos == '=') {
			val = fst_read_next_int_param(pos + 1, &is_valid,
						      &endp);
			if (is_valid)
				cfg->priority = (u8) val;
		}
	}

	return 0;
}


int fst_parse_detach_command(const char *cmd, char *ifname, size_t ifname_size)
{
	char *endp;

	return fst_read_next_text_param(cmd, ifname, ifname_size, &endp);
}


int fst_iface_detach(const char *ifname)
{
	struct fst_group *g;

	foreach_fst_group(g) {
		struct fst_iface *f;

		f = fst_group_get_iface_by_name(g, ifname);
		if (f) {
			fst_detach(f);
			return 0;
		}
	}

	return -EINVAL;
}
