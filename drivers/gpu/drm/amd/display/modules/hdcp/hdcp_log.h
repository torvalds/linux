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

#ifndef MOD_HDCP_LOG_H_
#define MOD_HDCP_LOG_H_

#ifdef CONFIG_DRM_AMD_DC_HDCP
#define HDCP_LOG_ERR(hdcp, ...) DRM_WARN(__VA_ARGS__)
#define HDCP_LOG_VER(hdcp, ...) DRM_DEBUG_KMS(__VA_ARGS__)
#define HDCP_LOG_FSM(hdcp, ...) DRM_DEBUG_KMS(__VA_ARGS__)
#define HDCP_LOG_TOP(hdcp, ...) pr_debug("[HDCP_TOP]:"__VA_ARGS__)
#define HDCP_LOG_DDC(hdcp, ...) pr_debug("[HDCP_DDC]:"__VA_ARGS__)
#endif

/* default logs */
#define HDCP_ERROR_TRACE(hdcp, status) \
		HDCP_LOG_ERR(hdcp, \
			"[Link %d] WARNING %s IN STATE %s", \
			hdcp->config.index, \
			mod_hdcp_status_to_str(status), \
			mod_hdcp_state_id_to_str(hdcp->state.id))
#define HDCP_HDCP1_ENABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 1.4 enabled on display %d", \
			hdcp->config.index, displayIndex)
#define HDCP_HDCP2_ENABLED_TRACE(hdcp, displayIndex) \
		HDCP_LOG_VER(hdcp, \
			"[Link %d] HDCP 2.2 enabled on display %d", \
			hdcp->config.index, displayIndex)
/* state machine logs */
#define HDCP_REMOVE_DISPLAY_TRACE(hdcp, displayIndex) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d] HDCP_REMOVE_DISPLAY index %d", \
			hdcp->config.index, displayIndex)
#define HDCP_INPUT_PASS_TRACE(hdcp, str) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d]\tPASS %s", \
			hdcp->config.index, str)
#define HDCP_INPUT_FAIL_TRACE(hdcp, str) \
		HDCP_LOG_FSM(hdcp, \
			"[Link %d]\tFAIL %s", \
			hdcp->config.index, str)
#define HDCP_NEXT_STATE_TRACE(hdcp, id, output) do { \
		if (output->watchdog_timer_needed) \
			HDCP_LOG_FSM(hdcp, \
				"[Link %d] > %s with %d ms watchdog", \
				hdcp->config.index, \
				mod_hdcp_state_id_to_str(id), output->watchdog_timer_delay); \
		else \
			HDCP_LOG_FSM(hdcp, \
				"[Link %d] > %s", hdcp->config.index, \
				mod_hdcp_state_id_to_str(id)); \
} while (0)
#define HDCP_TIMEOUT_TRACE(hdcp) \
		HDCP_LOG_FSM(hdcp, "[Link %d] --> TIMEOUT", hdcp->config.index)
#define HDCP_CPIRQ_TRACE(hdcp) \
		HDCP_LOG_FSM(hdcp, "[Link %d] --> CPIRQ", hdcp->config.index)
#define HDCP_EVENT_TRACE(hdcp, event) \
		if (event == MOD_HDCP_EVENT_WATCHDOG_TIMEOUT) \
			HDCP_TIMEOUT_TRACE(hdcp); \
		else if (event == MOD_HDCP_EVENT_CPIRQ) \
			HDCP_CPIRQ_TRACE(hdcp)
/* TODO: find some way to tell if logging is off to save time */
#define HDCP_DDC_READ_TRACE(hdcp, msg_name, msg, msg_size) do { \
		mod_hdcp_dump_binary_message(msg, msg_size, hdcp->buf, \
				sizeof(hdcp->buf)); \
		HDCP_LOG_DDC(hdcp, "[Link %d] Read %s%s", hdcp->config.index, \
				msg_name, hdcp->buf); \
} while (0)
#define HDCP_DDC_WRITE_TRACE(hdcp, msg_name, msg, msg_size) do { \
		mod_hdcp_dump_binary_message(msg, msg_size, hdcp->buf, \
				sizeof(hdcp->buf)); \
		HDCP_LOG_DDC(hdcp, "[Link %d] Write %s%s", \
				hdcp->config.index, msg_name,\
				hdcp->buf); \
} while (0)
#define HDCP_FULL_DDC_TRACE(hdcp) do { \
	if (is_hdcp1(hdcp)) { \
		HDCP_DDC_READ_TRACE(hdcp, "BKSV", hdcp->auth.msg.hdcp1.bksv, \
				sizeof(hdcp->auth.msg.hdcp1.bksv)); \
		HDCP_DDC_READ_TRACE(hdcp, "BCAPS", &hdcp->auth.msg.hdcp1.bcaps, \
				sizeof(hdcp->auth.msg.hdcp1.bcaps)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "AN", hdcp->auth.msg.hdcp1.an, \
				sizeof(hdcp->auth.msg.hdcp1.an)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "AKSV", hdcp->auth.msg.hdcp1.aksv, \
				sizeof(hdcp->auth.msg.hdcp1.aksv)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "AINFO", &hdcp->auth.msg.hdcp1.ainfo, \
				sizeof(hdcp->auth.msg.hdcp1.ainfo)); \
		HDCP_DDC_READ_TRACE(hdcp, "RI' / R0'", \
				(uint8_t *)&hdcp->auth.msg.hdcp1.r0p, \
				sizeof(hdcp->auth.msg.hdcp1.r0p)); \
		HDCP_DDC_READ_TRACE(hdcp, "BINFO", \
				(uint8_t *)&hdcp->auth.msg.hdcp1.binfo_dp, \
				sizeof(hdcp->auth.msg.hdcp1.binfo_dp)); \
		HDCP_DDC_READ_TRACE(hdcp, "KSVLIST", hdcp->auth.msg.hdcp1.ksvlist, \
				hdcp->auth.msg.hdcp1.ksvlist_size); \
		HDCP_DDC_READ_TRACE(hdcp, "V'", hdcp->auth.msg.hdcp1.vp, \
				sizeof(hdcp->auth.msg.hdcp1.vp)); \
	} else { \
		HDCP_DDC_READ_TRACE(hdcp, "HDCP2Version", \
				&hdcp->auth.msg.hdcp2.hdcp2version_hdmi, \
				sizeof(hdcp->auth.msg.hdcp2.hdcp2version_hdmi)); \
		HDCP_DDC_READ_TRACE(hdcp, "Rx Caps", hdcp->auth.msg.hdcp2.rxcaps_dp, \
				sizeof(hdcp->auth.msg.hdcp2.rxcaps_dp)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "AKE Init", hdcp->auth.msg.hdcp2.ake_init, \
				sizeof(hdcp->auth.msg.hdcp2.ake_init)); \
		HDCP_DDC_READ_TRACE(hdcp, "AKE Cert", hdcp->auth.msg.hdcp2.ake_cert, \
				sizeof(hdcp->auth.msg.hdcp2.ake_cert)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "Stored KM", \
				hdcp->auth.msg.hdcp2.ake_stored_km, \
				sizeof(hdcp->auth.msg.hdcp2.ake_stored_km)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "No Stored KM", \
				hdcp->auth.msg.hdcp2.ake_no_stored_km, \
				sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km)); \
		HDCP_DDC_READ_TRACE(hdcp, "H'", hdcp->auth.msg.hdcp2.ake_h_prime, \
				sizeof(hdcp->auth.msg.hdcp2.ake_h_prime)); \
		HDCP_DDC_READ_TRACE(hdcp, "Pairing Info", \
				hdcp->auth.msg.hdcp2.ake_pairing_info, \
				sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "LC Init", hdcp->auth.msg.hdcp2.lc_init, \
				sizeof(hdcp->auth.msg.hdcp2.lc_init)); \
		HDCP_DDC_READ_TRACE(hdcp, "L'", hdcp->auth.msg.hdcp2.lc_l_prime, \
				sizeof(hdcp->auth.msg.hdcp2.lc_l_prime)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "Exchange KS", hdcp->auth.msg.hdcp2.ske_eks, \
				sizeof(hdcp->auth.msg.hdcp2.ske_eks)); \
		HDCP_DDC_READ_TRACE(hdcp, "Rx Status", \
				(uint8_t *)&hdcp->auth.msg.hdcp2.rxstatus, \
				sizeof(hdcp->auth.msg.hdcp2.rxstatus)); \
		HDCP_DDC_READ_TRACE(hdcp, "Rx Id List", \
				hdcp->auth.msg.hdcp2.rx_id_list, \
				hdcp->auth.msg.hdcp2.rx_id_list_size); \
		HDCP_DDC_WRITE_TRACE(hdcp, "Rx Id List Ack", \
				hdcp->auth.msg.hdcp2.repeater_auth_ack, \
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_ack)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "Content Stream Management", \
				hdcp->auth.msg.hdcp2.repeater_auth_stream_manage, \
				hdcp->auth.msg.hdcp2.stream_manage_size); \
		HDCP_DDC_READ_TRACE(hdcp, "Stream Ready", \
				hdcp->auth.msg.hdcp2.repeater_auth_stream_ready, \
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready)); \
		HDCP_DDC_WRITE_TRACE(hdcp, "Content Stream Type", \
				hdcp->auth.msg.hdcp2.content_stream_type_dp, \
				sizeof(hdcp->auth.msg.hdcp2.content_stream_type_dp)); \
	} \
} while (0)
#define HDCP_TOP_ADD_DISPLAY_TRACE(hdcp, i) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tadd display %d", \
				hdcp->config.index, i)
#define HDCP_TOP_REMOVE_DISPLAY_TRACE(hdcp, i) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tremove display %d", \
				hdcp->config.index, i)
#define HDCP_TOP_HDCP1_DESTROY_SESSION_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tdestroy hdcp1 session", \
				hdcp->config.index)
#define HDCP_TOP_HDCP2_DESTROY_SESSION_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\tdestroy hdcp2 session", \
				hdcp->config.index)
#define HDCP_TOP_RESET_AUTH_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\treset authentication", hdcp->config.index)
#define HDCP_TOP_RESET_CONN_TRACE(hdcp) \
		HDCP_LOG_TOP(hdcp, "[Link %d]\treset connection", hdcp->config.index)
#define HDCP_TOP_INTERFACE_TRACE(hdcp) do { \
		HDCP_LOG_TOP(hdcp, "\n"); \
		HDCP_LOG_TOP(hdcp, "[Link %d] %s", hdcp->config.index, __func__); \
} while (0)
#define HDCP_TOP_INTERFACE_TRACE_WITH_INDEX(hdcp, i) do { \
		HDCP_LOG_TOP(hdcp, "\n"); \
		HDCP_LOG_TOP(hdcp, "[Link %d] %s display %d", hdcp->config.index, __func__, i); \
} while (0)

#endif // MOD_HDCP_LOG_H_
