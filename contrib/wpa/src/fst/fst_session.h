/*
 * FST module - FST Session related definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FST_SESSION_H
#define FST_SESSION_H

#define FST_DEFAULT_SESSION_TIMEOUT_TU 255 /* u8 */

struct fst_iface;
struct fst_group;
struct fst_session;
enum fst_session_state;

int  fst_session_global_init(void);
void fst_session_global_deinit(void);
void fst_session_global_on_iface_detached(struct fst_iface *iface);
struct fst_session *
fst_session_global_get_first_by_group(struct fst_group *g);

struct fst_session * fst_session_create(struct fst_group *g);
void fst_session_set_iface(struct fst_session *s, struct fst_iface *iface,
			   Boolean is_old);
void fst_session_set_llt(struct fst_session *s, u32 llt);
void fst_session_set_peer_addr(struct fst_session *s, const u8 *addr,
			       Boolean is_old);
int fst_session_initiate_setup(struct fst_session *s);
int fst_session_respond(struct fst_session *s, u8 status_code);
int fst_session_initiate_switch(struct fst_session *s);
void fst_session_handle_action(struct fst_session *s, struct fst_iface *iface,
			       const struct ieee80211_mgmt *mgmt,
			       size_t frame_len);
int fst_session_tear_down_setup(struct fst_session *s);
void fst_session_reset(struct fst_session *s);
void fst_session_delete(struct fst_session *s);

struct fst_group * fst_session_get_group(struct fst_session *s);
struct fst_iface * fst_session_get_iface(struct fst_session *s, Boolean is_old);
const u8 * fst_session_get_peer_addr(struct fst_session *s, Boolean is_old);
u32 fst_session_get_id(struct fst_session *s);
u32 fst_session_get_llt(struct fst_session *s);
enum fst_session_state fst_session_get_state(struct fst_session *s);

struct fst_session *fst_session_get_by_id(u32 id);

typedef void (*fst_session_enum_clb)(struct fst_group *g, struct fst_session *s,
				     void *ctx);

void fst_session_enum(struct fst_group *g, fst_session_enum_clb clb, void *ctx);

void fst_session_on_action_rx(struct fst_iface *iface,
			      const struct ieee80211_mgmt *mgmt, size_t len);


int fst_session_set_str_ifname(struct fst_session *s, const char *ifname,
			       Boolean is_old);
int fst_session_set_str_peer_addr(struct fst_session *s, const char *mac,
				  Boolean is_old);
int fst_session_set_str_llt(struct fst_session *s, const char *llt_str);

#ifdef CONFIG_FST_TEST

#define FST_FSTS_ID_NOT_FOUND ((u32) -1)

int fst_test_req_send_fst_request(const char *params);
int fst_test_req_send_fst_response(const char *params);
int fst_test_req_send_ack_request(const char *params);
int fst_test_req_send_ack_response(const char *params);
int fst_test_req_send_tear_down(const char *params);
u32 fst_test_req_get_fsts_id(const char *params);
int fst_test_req_get_local_mbies(const char *request, char *buf,
				 size_t buflen);

#endif /* CONFIG_FST_TEST */

#endif /* FST_SESSION_H */
