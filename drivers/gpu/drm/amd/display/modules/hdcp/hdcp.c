/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "hdcp.h"

static void push_error_status(struct mod_hdcp *hdcp,
		enum mod_hdcp_status status)
{
	struct mod_hdcp_trace *trace = &hdcp->connection.trace;

	if (trace->error_count < MAX_NUM_OF_ERROR_TRACE) {
		trace->errors[trace->error_count].status = status;
		trace->errors[trace->error_count].state_id = hdcp->state.id;
		trace->error_count++;
		HDCP_ERROR_TRACE(hdcp, status);
	}

	if (is_hdcp1(hdcp)) {
		hdcp->connection.hdcp1_retry_count++;
	} else if (is_hdcp2(hdcp)) {
		hdcp->connection.hdcp2_retry_count++;
	}
}

static uint8_t is_cp_desired_hdcp1(struct mod_hdcp *hdcp)
{
	int i, is_auth_needed = 0;

	/* if all displays on the link don't need authentication,
	 * hdcp is not desired
	 */
	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (hdcp->displays[i].state != MOD_HDCP_DISPLAY_INACTIVE &&
				!hdcp->displays[i].adjust.disable) {
			is_auth_needed = 1;
			break;
		}
	}

	return (hdcp->connection.hdcp1_retry_count < MAX_NUM_OF_ATTEMPTS) &&
			is_auth_needed &&
			!hdcp->connection.link.adjust.hdcp1.disable &&
			!hdcp->connection.is_hdcp1_revoked;
}

static uint8_t is_cp_desired_hdcp2(struct mod_hdcp *hdcp)
{
	int i, is_auth_needed = 0;

	/* if all displays on the link don't need authentication,
	 * hdcp is not desired
	 */
	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (hdcp->displays[i].state != MOD_HDCP_DISPLAY_INACTIVE &&
				!hdcp->displays[i].adjust.disable) {
			is_auth_needed = 1;
			break;
		}
	}

	return (hdcp->connection.hdcp2_retry_count < MAX_NUM_OF_ATTEMPTS) &&
			is_auth_needed &&
			!hdcp->connection.link.adjust.hdcp2.disable &&
			!hdcp->connection.is_hdcp2_revoked;
}

static enum mod_hdcp_status execution(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		union mod_hdcp_transition_input *input)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (is_in_initialized_state(hdcp)) {
		if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
			event_ctx->unexpected_event = 1;
			goto out;
		}
		/* initialize transition input */
		memset(input, 0, sizeof(union mod_hdcp_transition_input));
	} else if (is_in_cp_not_desired_state(hdcp)) {
		if (event_ctx->event != MOD_HDCP_EVENT_CALLBACK) {
			event_ctx->unexpected_event = 1;
			goto out;
		}
	} else if (is_in_hdcp1_states(hdcp)) {
		status = mod_hdcp_hdcp1_execution(hdcp, event_ctx, &input->hdcp1);
	} else if (is_in_hdcp1_dp_states(hdcp)) {
		status = mod_hdcp_hdcp1_dp_execution(hdcp,
				event_ctx, &input->hdcp1);
	} else if (is_in_hdcp2_states(hdcp)) {
		status = mod_hdcp_hdcp2_execution(hdcp, event_ctx, &input->hdcp2);
	} else if (is_in_hdcp2_dp_states(hdcp)) {
		status = mod_hdcp_hdcp2_dp_execution(hdcp,
				event_ctx, &input->hdcp2);
	} else {
		event_ctx->unexpected_event = 1;
		goto out;
	}
out:
	return status;
}

static enum mod_hdcp_status transition(struct mod_hdcp *hdcp,
		struct mod_hdcp_event_context *event_ctx,
		union mod_hdcp_transition_input *input,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (event_ctx->unexpected_event)
		goto out;

	if (is_in_initialized_state(hdcp)) {
		if (is_dp_hdcp(hdcp))
			if (is_cp_desired_hdcp2(hdcp)) {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, D2_A0_DETERMINE_RX_HDCP_CAPABLE);
			} else if (is_cp_desired_hdcp1(hdcp)) {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, D1_A0_DETERMINE_RX_HDCP_CAPABLE);
			} else {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, HDCP_CP_NOT_DESIRED);
			}
		else if (is_hdmi_dvi_sl_hdcp(hdcp))
			if (is_cp_desired_hdcp2(hdcp)) {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, H2_A0_KNOWN_HDCP2_CAPABLE_RX);
			} else if (is_cp_desired_hdcp1(hdcp)) {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, H1_A0_WAIT_FOR_ACTIVE_RX);
			} else {
				callback_in_ms(0, output);
				set_state_id(hdcp, output, HDCP_CP_NOT_DESIRED);
			}
		else {
			callback_in_ms(0, output);
			set_state_id(hdcp, output, HDCP_CP_NOT_DESIRED);
		}
	} else if (is_in_cp_not_desired_state(hdcp)) {
		increment_stay_counter(hdcp);
	} else if (is_in_hdcp1_states(hdcp)) {
		status = mod_hdcp_hdcp1_transition(hdcp,
				event_ctx, &input->hdcp1, output);
	} else if (is_in_hdcp1_dp_states(hdcp)) {
		status = mod_hdcp_hdcp1_dp_transition(hdcp,
				event_ctx, &input->hdcp1, output);
	} else if (is_in_hdcp2_states(hdcp)) {
		status = mod_hdcp_hdcp2_transition(hdcp,
				event_ctx, &input->hdcp2, output);
	} else if (is_in_hdcp2_dp_states(hdcp)) {
		status = mod_hdcp_hdcp2_dp_transition(hdcp,
				event_ctx, &input->hdcp2, output);
	} else {
		status = MOD_HDCP_STATUS_INVALID_STATE;
	}
out:
	return status;
}

static enum mod_hdcp_status reset_authentication(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	if (is_hdcp1(hdcp)) {
		if (hdcp->auth.trans_input.hdcp1.create_session != UNKNOWN) {
			/* TODO - update psp to unify create session failure
			 * recovery between hdcp1 and 2.
			 */
			mod_hdcp_hdcp1_destroy_session(hdcp);

		}

		HDCP_TOP_RESET_AUTH_TRACE(hdcp);
		memset(&hdcp->auth, 0, sizeof(struct mod_hdcp_authentication));
		memset(&hdcp->state, 0, sizeof(struct mod_hdcp_state));
		set_state_id(hdcp, output, HDCP_INITIALIZED);
	} else if (is_hdcp2(hdcp)) {
		if (hdcp->auth.trans_input.hdcp2.create_session == PASS) {
			status = mod_hdcp_hdcp2_destroy_session(hdcp);
			if (status != MOD_HDCP_STATUS_SUCCESS) {
				output->callback_needed = 0;
				output->watchdog_timer_needed = 0;
				goto out;
			}
		}

		HDCP_TOP_RESET_AUTH_TRACE(hdcp);
		memset(&hdcp->auth, 0, sizeof(struct mod_hdcp_authentication));
		memset(&hdcp->state, 0, sizeof(struct mod_hdcp_state));
		set_state_id(hdcp, output, HDCP_INITIALIZED);
	} else if (is_in_cp_not_desired_state(hdcp)) {
		HDCP_TOP_RESET_AUTH_TRACE(hdcp);
		memset(&hdcp->auth, 0, sizeof(struct mod_hdcp_authentication));
		memset(&hdcp->state, 0, sizeof(struct mod_hdcp_state));
		set_state_id(hdcp, output, HDCP_INITIALIZED);
	}

out:
	/* stop callback and watchdog requests from previous authentication*/
	output->watchdog_timer_stop = 1;
	output->callback_stop = 1;
	return status;
}

static enum mod_hdcp_status reset_connection(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	memset(output, 0, sizeof(struct mod_hdcp_output));

	status = reset_authentication(hdcp, output);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	if (current_state(hdcp) != HDCP_UNINITIALIZED) {
		HDCP_TOP_RESET_CONN_TRACE(hdcp);
		set_state_id(hdcp, output, HDCP_UNINITIALIZED);
	}
	memset(&hdcp->connection, 0, sizeof(hdcp->connection));
out:
	return status;
}

/*
 * Implementation of functions in mod_hdcp.h
 */
size_t mod_hdcp_get_memory_size(void)
{
	return sizeof(struct mod_hdcp);
}

enum mod_hdcp_status mod_hdcp_setup(struct mod_hdcp *hdcp,
		struct mod_hdcp_config *config)
{
	struct mod_hdcp_output output;
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	memset(hdcp, 0, sizeof(struct mod_hdcp));
	memset(&output, 0, sizeof(output));
	hdcp->config = *config;
	HDCP_TOP_INTERFACE_TRACE(hdcp);
	status = reset_connection(hdcp, &output);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		push_error_status(hdcp, status);
	return status;
}

enum mod_hdcp_status mod_hdcp_teardown(struct mod_hdcp *hdcp)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_output output;

	HDCP_TOP_INTERFACE_TRACE(hdcp);
	memset(&output, 0,  sizeof(output));
	status = reset_connection(hdcp, &output);
	if (status == MOD_HDCP_STATUS_SUCCESS)
		memset(hdcp, 0, sizeof(struct mod_hdcp));
	else
		push_error_status(hdcp, status);
	return status;
}

enum mod_hdcp_status mod_hdcp_add_display(struct mod_hdcp *hdcp,
		struct mod_hdcp_link *link, struct mod_hdcp_display *display,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_display *display_container = NULL;

	HDCP_TOP_INTERFACE_TRACE_WITH_INDEX(hdcp, display->index);
	memset(output, 0, sizeof(struct mod_hdcp_output));

	/* skip inactive display */
	if (display->state != MOD_HDCP_DISPLAY_ACTIVE) {
		status = MOD_HDCP_STATUS_SUCCESS;
		goto out;
	}

	/* check existing display container */
	if (get_active_display_at_index(hdcp, display->index)) {
		status = MOD_HDCP_STATUS_SUCCESS;
		goto out;
	}

	/* find an empty display container */
	display_container = get_empty_display_container(hdcp);
	if (!display_container) {
		status = MOD_HDCP_STATUS_DISPLAY_OUT_OF_BOUND;
		goto out;
	}

	/* reset existing authentication status */
	status = reset_authentication(hdcp, output);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	/* reset retry counters */
	reset_retry_counts(hdcp);

	/* reset error trace */
	memset(&hdcp->connection.trace, 0, sizeof(hdcp->connection.trace));

	/* add display to connection */
	hdcp->connection.link = *link;
	*display_container = *display;
	status = mod_hdcp_add_display_to_topology(hdcp, display_container);

	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	/* request authentication */
	if (current_state(hdcp) != HDCP_INITIALIZED)
		set_state_id(hdcp, output, HDCP_INITIALIZED);
	callback_in_ms(hdcp->connection.link.adjust.auth_delay * 1000, output);
out:
	if (status != MOD_HDCP_STATUS_SUCCESS)
		push_error_status(hdcp, status);

	return status;
}

enum mod_hdcp_status mod_hdcp_remove_display(struct mod_hdcp *hdcp,
		uint8_t index, struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_display *display = NULL;

	HDCP_TOP_INTERFACE_TRACE_WITH_INDEX(hdcp, index);
	memset(output, 0, sizeof(struct mod_hdcp_output));

	/* find display in connection */
	display = get_active_display_at_index(hdcp, index);
	if (!display) {
		status = MOD_HDCP_STATUS_SUCCESS;
		goto out;
	}

	/* stop current authentication */
	status = reset_authentication(hdcp, output);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;

	/* clear retry counters */
	reset_retry_counts(hdcp);

	/* reset error trace */
	memset(&hdcp->connection.trace, 0, sizeof(hdcp->connection.trace));

	/* remove display */
	status = mod_hdcp_remove_display_from_topology(hdcp, index);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		goto out;
	memset(display, 0, sizeof(struct mod_hdcp_display));

	/* request authentication when connection is not reset */
	if (current_state(hdcp) != HDCP_UNINITIALIZED)
		callback_in_ms(hdcp->connection.link.adjust.auth_delay * 1000,
				output);
out:
	if (status != MOD_HDCP_STATUS_SUCCESS)
		push_error_status(hdcp, status);
	return status;
}

enum mod_hdcp_status mod_hdcp_query_display(struct mod_hdcp *hdcp,
		uint8_t index, struct mod_hdcp_display_query *query)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;
	struct mod_hdcp_display *display = NULL;

	/* find display in connection */
	display = get_active_display_at_index(hdcp, index);
	if (!display) {
		status = MOD_HDCP_STATUS_DISPLAY_NOT_FOUND;
		goto out;
	}

	/* populate query */
	query->link = &hdcp->connection.link;
	query->display = display;
	query->trace = &hdcp->connection.trace;
	query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	if (is_display_encryption_enabled(display)) {
		if (is_hdcp1(hdcp)) {
			query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP1_ON;
		} else if (is_hdcp2(hdcp)) {
			if (query->link->adjust.hdcp2.force_type == MOD_HDCP_FORCE_TYPE_0)
				query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON;
			else if (query->link->adjust.hdcp2.force_type == MOD_HDCP_FORCE_TYPE_1)
				query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;
			else
				query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_ON;
		}
	} else {
		query->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;
	}

out:
	return status;
}

enum mod_hdcp_status mod_hdcp_reset_connection(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output)
{
	enum mod_hdcp_status status = MOD_HDCP_STATUS_SUCCESS;

	HDCP_TOP_INTERFACE_TRACE(hdcp);
	status = reset_connection(hdcp, output);
	if (status != MOD_HDCP_STATUS_SUCCESS)
		push_error_status(hdcp, status);

	return status;
}

enum mod_hdcp_status mod_hdcp_process_event(struct mod_hdcp *hdcp,
		enum mod_hdcp_event event, struct mod_hdcp_output *output)
{
	enum mod_hdcp_status exec_status, trans_status, reset_status, status;
	struct mod_hdcp_event_context event_ctx;

	HDCP_EVENT_TRACE(hdcp, event);
	memset(output, 0, sizeof(struct mod_hdcp_output));
	memset(&event_ctx, 0, sizeof(struct mod_hdcp_event_context));
	event_ctx.event = event;

	/* execute and transition */
	exec_status = execution(hdcp, &event_ctx, &hdcp->auth.trans_input);
	trans_status = transition(
			hdcp, &event_ctx, &hdcp->auth.trans_input, output);
	if (trans_status == MOD_HDCP_STATUS_SUCCESS) {
		status = MOD_HDCP_STATUS_SUCCESS;
	} else if (exec_status == MOD_HDCP_STATUS_SUCCESS) {
		status = MOD_HDCP_STATUS_INTERNAL_POLICY_FAILURE;
		push_error_status(hdcp, status);
	} else {
		status = exec_status;
		push_error_status(hdcp, status);
	}

	/* reset authentication if needed */
	if (trans_status == MOD_HDCP_STATUS_RESET_NEEDED) {
		HDCP_FULL_DDC_TRACE(hdcp);
		reset_status = reset_authentication(hdcp, output);
		if (reset_status != MOD_HDCP_STATUS_SUCCESS)
			push_error_status(hdcp, reset_status);
	}

	/* Clear CP_IRQ status if needed */
	if (event_ctx.event == MOD_HDCP_EVENT_CPIRQ) {
		status = mod_hdcp_clear_cp_irq_status(hdcp);
		if (status != MOD_HDCP_STATUS_SUCCESS)
			push_error_status(hdcp, status);
	}

	return status;
}

enum mod_hdcp_operation_mode mod_hdcp_signal_type_to_operation_mode(
		enum signal_type signal)
{
	enum mod_hdcp_operation_mode mode = MOD_HDCP_MODE_OFF;

	switch (signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_HDMI_TYPE_A:
		mode = MOD_HDCP_MODE_DEFAULT;
		break;
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		mode = MOD_HDCP_MODE_DP;
		break;
	default:
		break;
	}

	return mode;
}
