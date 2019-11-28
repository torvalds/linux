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

#ifndef HDCP_H_
#define HDCP_H_

#include "mod_hdcp.h"
#include "hdcp_log.h"

#define BCAPS_READY_MASK				0x20
#define BCAPS_REPEATER_MASK				0x40
#define BSTATUS_DEVICE_COUNT_MASK			0X007F
#define BSTATUS_MAX_DEVS_EXCEEDED_MASK			0x0080
#define BSTATUS_MAX_CASCADE_EXCEEDED_MASK		0x0800
#define BCAPS_HDCP_CAPABLE_MASK_DP			0x01
#define BCAPS_REPEATER_MASK_DP				0x02
#define BSTATUS_READY_MASK_DP				0x01
#define BSTATUS_R0_P_AVAILABLE_MASK_DP			0x02
#define BSTATUS_LINK_INTEGRITY_FAILURE_MASK_DP		0x04
#define BSTATUS_REAUTH_REQUEST_MASK_DP			0x08
#define BINFO_DEVICE_COUNT_MASK_DP			0X007F
#define BINFO_MAX_DEVS_EXCEEDED_MASK_DP			0x0080
#define BINFO_MAX_CASCADE_EXCEEDED_MASK_DP		0x0800

#define RXSTATUS_MSG_SIZE_MASK				0x03FF
#define RXSTATUS_READY_MASK				0x0400
#define RXSTATUS_REAUTH_REQUEST_MASK			0x0800
#define RXIDLIST_DEVICE_COUNT_LOWER_MASK		0xf0
#define RXIDLIST_DEVICE_COUNT_UPPER_MASK		0x01
#define RXCAPS_BYTE0_HDCP_CAPABLE_MASK_DP		0x02
#define RXSTATUS_READY_MASK_DP				0x0001
#define RXSTATUS_H_P_AVAILABLE_MASK_DP			0x0002
#define RXSTATUS_PAIRING_AVAILABLE_MASK_DP		0x0004
#define RXSTATUS_REAUTH_REQUEST_MASK_DP			0x0008
#define RXSTATUS_LINK_INTEGRITY_FAILURE_MASK_DP		0x0010

enum mod_hdcp_trans_input_result {
	UNKNOWN = 0,
	PASS,
	FAIL
};

struct mod_hdcp_transition_input_hdcp1 {
	uint8_t bksv_read;
	uint8_t bksv_validation;
	uint8_t add_topology;
	uint8_t create_session;
	uint8_t an_write;
	uint8_t aksv_write;
	uint8_t ainfo_write;
	uint8_t bcaps_read;
	uint8_t r0p_read;
	uint8_t rx_validation;
	uint8_t encryption;
	uint8_t link_maintenance;
	uint8_t ready_check;
	uint8_t bstatus_read;
	uint8_t max_cascade_check;
	uint8_t max_devs_check;
	uint8_t device_count_check;
	uint8_t ksvlist_read;
	uint8_t vp_read;
	uint8_t ksvlist_vp_validation;

	uint8_t hdcp_capable_dp;
	uint8_t binfo_read_dp;
	uint8_t r0p_available_dp;
	uint8_t link_integiry_check;
	uint8_t reauth_request_check;
	uint8_t stream_encryption_dp;
};

union mod_hdcp_transition_input {
	struct mod_hdcp_transition_input_hdcp1 hdcp1;
};

struct mod_hdcp_message_hdcp1 {
	uint8_t		an[8];
	uint8_t		aksv[5];
	uint8_t		ainfo;
	uint8_t		bksv[5];
	uint16_t	r0p;
	uint8_t		bcaps;
	uint16_t	bstatus;
	uint8_t		ksvlist[635];
	uint16_t	ksvlist_size;
	uint8_t		vp[20];

	uint16_t	binfo_dp;
};

union mod_hdcp_message {
	struct mod_hdcp_message_hdcp1 hdcp1;
};

struct mod_hdcp_auth_counters {
	uint8_t stream_management_retry_count;
};

/* contains values per connection */
struct mod_hdcp_connection {
	struct mod_hdcp_link link;
	struct mod_hdcp_display displays[MAX_NUM_OF_DISPLAYS];
	uint8_t is_repeater;
	uint8_t is_km_stored;
	struct mod_hdcp_trace trace;
	uint8_t hdcp1_retry_count;
};

/* contains values per authentication cycle */
struct mod_hdcp_authentication {
	uint32_t id;
	union mod_hdcp_message msg;
	union mod_hdcp_transition_input trans_input;
	struct mod_hdcp_auth_counters count;
};

/* contains values per state change */
struct mod_hdcp_state {
	uint8_t id;
	uint32_t stay_count;
};

/* per event in a state */
struct mod_hdcp_event_context {
	enum mod_hdcp_event event;
	uint8_t rx_id_list_ready;
	uint8_t unexpected_event;
};

struct mod_hdcp {
	/* per link */
	struct mod_hdcp_config config;
	/* per connection */
	struct mod_hdcp_connection connection;
	/* per authentication attempt */
	struct mod_hdcp_authentication auth;
	/* per state in an authentication */
	struct mod_hdcp_state state;
	/* reserved memory buffer */
	uint8_t buf[2025];
};

enum mod_hdcp_initial_state_id {
	HDCP_UNINITIALIZED = 0x0,
	HDCP_INITIAL_STATE_START = HDCP_UNINITIALIZED,
	HDCP_INITIALIZED,
	HDCP_CP_NOT_DESIRED,
	HDCP_INITIAL_STATE_END = HDCP_CP_NOT_DESIRED
};

enum mod_hdcp_hdcp1_state_id {
	HDCP1_STATE_START = HDCP_INITIAL_STATE_END,
	H1_A0_WAIT_FOR_ACTIVE_RX,
	H1_A1_EXCHANGE_KSVS,
	H1_A2_COMPUTATIONS_A3_VALIDATE_RX_A6_TEST_FOR_REPEATER,
	H1_A45_AUTHENTICATED,
	H1_A8_WAIT_FOR_READY,
	H1_A9_READ_KSV_LIST,
	HDCP1_STATE_END = H1_A9_READ_KSV_LIST
};

enum mod_hdcp_hdcp1_dp_state_id {
	HDCP1_DP_STATE_START = HDCP1_STATE_END,
	D1_A0_DETERMINE_RX_HDCP_CAPABLE,
	D1_A1_EXCHANGE_KSVS,
	D1_A23_WAIT_FOR_R0_PRIME,
	D1_A2_COMPUTATIONS_A3_VALIDATE_RX_A5_TEST_FOR_REPEATER,
	D1_A4_AUTHENTICATED,
	D1_A6_WAIT_FOR_READY,
	D1_A7_READ_KSV_LIST,
	HDCP1_DP_STATE_END = D1_A7_READ_KSV_LIST,
};

/* hdcp1 executions and transitions */
typedef enum mod_hdcp_status (*mod_hdcp_action)(struct mod_hdcp *hdcp);
uint8_t mod_hdcp_execute_and_set(
		mod_hdcp_action func, uint8_t *flag,
		enum mod_hdcp_status *status, struct mod_hdcp *hdcp, char *str);
enum mod_hdcp_status mod_hdcp_hdcp1_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input);
enum mod_hdcp_status mod_hdcp_hdcp1_dp_execution(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input);
enum mod_hdcp_status mod_hdcp_hdcp1_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input,
	struct mod_hdcp_output *output);
enum mod_hdcp_status mod_hdcp_hdcp1_dp_transition(struct mod_hdcp *hdcp,
	struct mod_hdcp_event_context *event_ctx,
	struct mod_hdcp_transition_input_hdcp1 *input,
	struct mod_hdcp_output *output);

/* log functions */
void mod_hdcp_dump_binary_message(uint8_t *msg, uint32_t msg_size,
		uint8_t *buf, uint32_t buf_size);
/* TODO: add adjustment log */

/* psp functions */
enum mod_hdcp_status mod_hdcp_add_display_topology(
		struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_remove_display_topology(
		struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_create_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_destroy_session(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_validate_rx(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_enable_encryption(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_validate_ksvlist_vp(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_enable_dp_stream_encryption(
	struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_link_maintenance(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_hdcp1_get_link_encryption_status(struct mod_hdcp *hdcp,
							       enum mod_hdcp_encryption_status *encryption_status);
/* ddc functions */
enum mod_hdcp_status mod_hdcp_read_bksv(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_bcaps(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_bstatus(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_r0p(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_ksvlist(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_vp(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_binfo(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_aksv(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_ainfo(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_an(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rxcaps(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rxstatus(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_ake_cert(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_h_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_pairing_info(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_l_prime(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_rx_id_list(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_read_stream_ready(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_ake_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_no_stored_km(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_stored_km(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_lc_init(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_eks(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_repeater_auth_ack(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_stream_manage(struct mod_hdcp *hdcp);
enum mod_hdcp_status mod_hdcp_write_content_type(struct mod_hdcp *hdcp);

/* hdcp version helpers */
static inline uint8_t is_dp_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DP ||
			hdcp->connection.link.mode == MOD_HDCP_MODE_DP_MST);
}

static inline uint8_t is_dp_mst_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DP_MST);
}

static inline uint8_t is_hdmi_dvi_sl_hdcp(struct mod_hdcp *hdcp)
{
	return (hdcp->connection.link.mode == MOD_HDCP_MODE_DEFAULT);
}

/* hdcp state helpers */
static inline uint8_t current_state(struct mod_hdcp *hdcp)
{
	return hdcp->state.id;
}

static inline void set_state_id(struct mod_hdcp *hdcp,
		struct mod_hdcp_output *output, uint8_t id)
{
	memset(&hdcp->state, 0, sizeof(hdcp->state));
	hdcp->state.id = id;
	/* callback timer should be reset per state */
	output->callback_stop = 1;
	output->watchdog_timer_stop = 1;
	HDCP_NEXT_STATE_TRACE(hdcp, id, output);
}

static inline uint8_t is_in_hdcp1_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP1_STATE_START &&
			current_state(hdcp) <= HDCP1_STATE_END);
}

static inline uint8_t is_in_hdcp1_dp_states(struct mod_hdcp *hdcp)
{
	return (current_state(hdcp) > HDCP1_DP_STATE_START &&
			current_state(hdcp) <= HDCP1_DP_STATE_END);
}

static inline uint8_t is_hdcp1(struct mod_hdcp *hdcp)
{
	return (is_in_hdcp1_states(hdcp) || is_in_hdcp1_dp_states(hdcp));
}

static inline uint8_t is_in_cp_not_desired_state(struct mod_hdcp *hdcp)
{
	return current_state(hdcp) == HDCP_CP_NOT_DESIRED;
}

static inline uint8_t is_in_initialized_state(struct mod_hdcp *hdcp)
{
	return current_state(hdcp) == HDCP_INITIALIZED;
}

/* transition operation helpers */
static inline void increment_stay_counter(struct mod_hdcp *hdcp)
{
	hdcp->state.stay_count++;
}

static inline void fail_and_restart_in_ms(uint16_t time,
		enum mod_hdcp_status *status,
		struct mod_hdcp_output *output)
{
	output->callback_needed = 1;
	output->callback_delay = time;
	output->watchdog_timer_needed = 0;
	output->watchdog_timer_delay = 0;
	*status = MOD_HDCP_STATUS_RESET_NEEDED;
}

static inline void callback_in_ms(uint16_t time, struct mod_hdcp_output *output)
{
	output->callback_needed = 1;
	output->callback_delay = time;
}

static inline void set_watchdog_in_ms(struct mod_hdcp *hdcp, uint16_t time,
		struct mod_hdcp_output *output)
{
	output->watchdog_timer_needed = 1;
	output->watchdog_timer_delay = time;
}

/* connection topology helpers */
static inline uint8_t is_display_active(struct mod_hdcp_display *display)
{
	return display->state >= MOD_HDCP_DISPLAY_ACTIVE;
}

static inline uint8_t is_display_added(struct mod_hdcp_display *display)
{
	return display->state >= MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED;
}

static inline uint8_t is_display_encryption_enabled(struct mod_hdcp_display *display)
{
	return display->state >= MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
}

static inline uint8_t get_active_display_count(struct mod_hdcp *hdcp)
{
	uint8_t added_count = 0;
	uint8_t i;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (is_display_active(&hdcp->connection.displays[i]))
			added_count++;
	return added_count;
}

static inline uint8_t get_added_display_count(struct mod_hdcp *hdcp)
{
	uint8_t added_count = 0;
	uint8_t i;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (is_display_added(&hdcp->connection.displays[i]))
			added_count++;
	return added_count;
}

static inline struct mod_hdcp_display *get_first_added_display(
		struct mod_hdcp *hdcp)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (is_display_added(&hdcp->connection.displays[i])) {
			display = &hdcp->connection.displays[i];
			break;
		}
	return display;
}

static inline struct mod_hdcp_display *get_active_display_at_index(
		struct mod_hdcp *hdcp, uint8_t index)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (hdcp->connection.displays[i].index == index &&
				is_display_active(&hdcp->connection.displays[i])) {
			display = &hdcp->connection.displays[i];
			break;
		}
	return display;
}

static inline struct mod_hdcp_display *get_empty_display_container(
		struct mod_hdcp *hdcp)
{
	uint8_t i;
	struct mod_hdcp_display *display = NULL;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++)
		if (!is_display_active(&hdcp->connection.displays[i])) {
			display = &hdcp->connection.displays[i];
			break;
		}
	return display;
}

static inline void reset_retry_counts(struct mod_hdcp *hdcp)
{
	hdcp->connection.hdcp1_retry_count = 0;
}

#endif /* HDCP_H_ */
