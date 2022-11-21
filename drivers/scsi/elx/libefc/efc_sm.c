// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * Generic state machine framework.
 */
#include "efc.h"
#include "efc_sm.h"

/**
 * efc_sm_post_event() - Post an event to a context.
 *
 * @ctx: State machine context
 * @evt: Event to post
 * @data: Event-specific data (if any)
 */
int
efc_sm_post_event(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *data)
{
	if (!ctx->current_state)
		return -EIO;

	ctx->current_state(ctx, evt, data);
	return 0;
}

void
efc_sm_transition(struct efc_sm_ctx *ctx,
		  void (*state)(struct efc_sm_ctx *,
				enum efc_sm_event, void *), void *data)

{
	if (ctx->current_state == state) {
		efc_sm_post_event(ctx, EFC_EVT_REENTER, data);
	} else {
		efc_sm_post_event(ctx, EFC_EVT_EXIT, data);
		ctx->current_state = state;
		efc_sm_post_event(ctx, EFC_EVT_ENTER, data);
	}
}

static char *event_name[] = EFC_SM_EVENT_NAME;

const char *efc_sm_event_name(enum efc_sm_event evt)
{
	if (evt > EFC_EVT_LAST)
		return "unknown";

	return event_name[evt];
}
