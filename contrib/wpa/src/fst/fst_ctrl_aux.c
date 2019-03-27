/*
 * FST module implementation
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/defs.h"
#include "fst_ctrl_defs.h"
#include "fst_ctrl_aux.h"


static const char *session_event_names[] = {
	[EVENT_FST_ESTABLISHED] = FST_PVAL_EVT_TYPE_ESTABLISHED,
	[EVENT_FST_SETUP] = FST_PVAL_EVT_TYPE_SETUP,
	[EVENT_FST_SESSION_STATE_CHANGED] = FST_PVAL_EVT_TYPE_SESSION_STATE,
};

static const char *reason_names[] = {
	[REASON_TEARDOWN] = FST_CS_PVAL_REASON_TEARDOWN,
	[REASON_SETUP] = FST_CS_PVAL_REASON_SETUP,
	[REASON_SWITCH] = FST_CS_PVAL_REASON_SWITCH,
	[REASON_STT] = FST_CS_PVAL_REASON_STT,
	[REASON_REJECT] = FST_CS_PVAL_REASON_REJECT,
	[REASON_ERROR_PARAMS] = FST_CS_PVAL_REASON_ERROR_PARAMS,
	[REASON_RESET] = FST_CS_PVAL_REASON_RESET,
	[REASON_DETACH_IFACE] = FST_CS_PVAL_REASON_DETACH_IFACE,
};

static const char *session_state_names[] = {
	[FST_SESSION_STATE_INITIAL] = FST_CS_PVAL_STATE_INITIAL,
	[FST_SESSION_STATE_SETUP_COMPLETION] =
	FST_CS_PVAL_STATE_SETUP_COMPLETION,
	[FST_SESSION_STATE_TRANSITION_DONE] = FST_CS_PVAL_STATE_TRANSITION_DONE,
	[FST_SESSION_STATE_TRANSITION_CONFIRMED] =
	FST_CS_PVAL_STATE_TRANSITION_CONFIRMED,
};


/* helpers */
const char * fst_get_str_name(unsigned index, const char *names[],
			      size_t names_size)
{
	if (index >= names_size || !names[index])
		return FST_NAME_UNKNOWN;
	return names[index];
}


const char * fst_session_event_type_name(enum fst_event_type event)
{
	return fst_get_str_name(event, session_event_names,
				ARRAY_SIZE(session_event_names));
}


const char * fst_reason_name(enum fst_reason reason)
{
	return fst_get_str_name(reason, reason_names, ARRAY_SIZE(reason_names));
}


const char * fst_session_state_name(enum fst_session_state state)
{
	return fst_get_str_name(state, session_state_names,
				ARRAY_SIZE(session_state_names));
}
