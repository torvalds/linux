/*
 * FST module - miscellaneous definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FST_CTRL_AUX_H
#define FST_CTRL_AUX_H

#include "common/defs.h"

/* FST module control interface API */
#define FST_INVALID_SESSION_ID ((u32) -1)
#define FST_MAX_GROUP_ID_SIZE   32
#define FST_MAX_INTERFACE_SIZE  32

enum fst_session_state {
	FST_SESSION_STATE_INITIAL,
	FST_SESSION_STATE_SETUP_COMPLETION,
	FST_SESSION_STATE_TRANSITION_DONE,
	FST_SESSION_STATE_TRANSITION_CONFIRMED,
	FST_SESSION_STATE_LAST
};

enum fst_event_type {
	EVENT_FST_IFACE_STATE_CHANGED,  /* An interface has been either attached
					 * to or detached from an FST group */
	EVENT_FST_ESTABLISHED,          /* FST Session has been established */
	EVENT_FST_SETUP,                /* FST Session request received */
	EVENT_FST_SESSION_STATE_CHANGED,/* FST Session state has been changed */
	EVENT_PEER_STATE_CHANGED        /* FST related generic event occurred,
					 * see struct fst_hostap_event_data for
					 *  more info */
};

enum fst_reason {
	REASON_TEARDOWN,
	REASON_SETUP,
	REASON_SWITCH,
	REASON_STT,
	REASON_REJECT,
	REASON_ERROR_PARAMS,
	REASON_RESET,
	REASON_DETACH_IFACE,
};

enum fst_initiator {
	FST_INITIATOR_UNDEFINED,
	FST_INITIATOR_LOCAL,
	FST_INITIATOR_REMOTE,
};

union fst_event_extra {
	struct fst_event_extra_iface_state {
		Boolean attached;
		char ifname[FST_MAX_INTERFACE_SIZE];
		char group_id[FST_MAX_GROUP_ID_SIZE];
	} iface_state; /* for EVENT_FST_IFACE_STATE_CHANGED */
	struct fst_event_extra_peer_state {
		Boolean connected;
		char ifname[FST_MAX_INTERFACE_SIZE];
		u8 addr[ETH_ALEN];
	} peer_state; /* for EVENT_PEER_STATE_CHANGED */
	struct fst_event_extra_session_state {
		enum fst_session_state old_state;
		enum fst_session_state new_state;
		union fst_session_state_switch_extra {
			struct {
				enum fst_reason reason;
				u8 reject_code; /* REASON_REJECT */
				/* REASON_SWITCH,
				 * REASON_TEARDOWN,
				 * REASON_REJECT
				 */
				enum fst_initiator initiator;
			} to_initial;
		} extra;
	} session_state; /* for EVENT_FST_SESSION_STATE_CHANGED */
};

/* helpers - prints enum in string form */
#define FST_NAME_UNKNOWN "UNKNOWN"

const char * fst_get_str_name(unsigned index, const char *names[],
			      size_t names_size);

const char * fst_session_event_type_name(enum fst_event_type);
const char * fst_reason_name(enum fst_reason reason);
const char * fst_session_state_name(enum fst_session_state state);

#endif /* FST_CTRL_AUX_H */
