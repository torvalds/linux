/*
 * FST module - FST related definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE_80211_FST_DEFS_H
#define IEEE_80211_FST_DEFS_H

/* IEEE Std 802.11ad */

#define MB_STA_CHANNEL_ALL 0

enum session_type {
	SESSION_TYPE_BSS = 0, /*  Infrastructure BSS */
	SESSION_TYPE_IBSS = 1,
	SESSION_TYPE_DLS = 2,
	SESSION_TYPE_TDLS = 3,
	SESSION_TYPE_PBSS = 4
};

#define SESSION_CONTROL(session_type, switch_intent) \
	(((u8) ((session_type) & 0x7)) | ((switch_intent) ? 0x10 : 0x00))

#define GET_SESSION_CONTROL_TYPE(session_control) \
	((u8) ((session_control) & 0x7))

#define GET_SESSION_CONTROL_SWITCH_INTENT(session_control) \
	(((session_control) & 0x10) >> 4)

/* 8.4.2.147  Session Transition element */
struct session_transition_ie {
	u8 element_id;
	u8 length;
	le32 fsts_id;
	u8 session_control;
	u8 new_band_id;
	u8 new_band_setup;
	u8 new_band_op;
	u8 old_band_id;
	u8 old_band_setup;
	u8 old_band_op;
} STRUCT_PACKED;

struct fst_setup_req {
	u8 action;
	u8 dialog_token;
	le32 llt;
	struct session_transition_ie stie;
	/* Multi-band (optional) */
	/* Wakeup Schedule (optional) */
	/* Awake Window (optional) */
	/* Switching Stream (optional) */
} STRUCT_PACKED;

struct fst_setup_res {
	u8 action;
	u8 dialog_token;
	u8 status_code;
	struct session_transition_ie stie;
	/* Multi-band (optional) */
	/* Wakeup Schedule (optional) */
	/* Awake Window (optional) */
	/* Switching Stream (optional) */
	/* Timeout Interval (optional) */
} STRUCT_PACKED;

struct fst_ack_req {
	u8 action;
	u8 dialog_token;
	le32 fsts_id;
} STRUCT_PACKED;

struct fst_ack_res {
	u8 action;
	u8 dialog_token;
	le32 fsts_id;
} STRUCT_PACKED;

struct fst_tear_down {
	u8 action;
	le32 fsts_id;
} STRUCT_PACKED;

#endif /* IEEE_80211_FST_DEFS_H */
