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

void mod_hdcp_dump_binary_message(uint8_t *msg, uint32_t msg_size,
		uint8_t *buf, uint32_t buf_size)
{
	const uint8_t bytes_per_line = 16,
			byte_size = 3,
			newline_size = 1,
			terminator_size = 1;
	uint32_t line_count = msg_size / bytes_per_line,
			trailing_bytes = msg_size % bytes_per_line;
	uint32_t target_size = (byte_size * bytes_per_line + newline_size) * line_count +
			byte_size * trailing_bytes + newline_size + terminator_size;
	uint32_t buf_pos = 0;
	uint32_t i = 0;

	if (buf_size >= target_size) {
		for (i = 0; i < msg_size; i++) {
			if (i % bytes_per_line == 0)
				buf[buf_pos++] = '\n';
			sprintf(&buf[buf_pos], "%02X ", msg[i]);
			buf_pos += byte_size;
		}
		buf[buf_pos++] = '\0';
	}
}

void mod_hdcp_log_ddc_trace(struct mod_hdcp *hdcp)
{
	if (is_hdcp1(hdcp)) {
		HDCP_DDC_READ_TRACE(hdcp, "BKSV", hdcp->auth.msg.hdcp1.bksv,
				sizeof(hdcp->auth.msg.hdcp1.bksv));
		HDCP_DDC_READ_TRACE(hdcp, "BCAPS", &hdcp->auth.msg.hdcp1.bcaps,
				sizeof(hdcp->auth.msg.hdcp1.bcaps));
		HDCP_DDC_READ_TRACE(hdcp, "BSTATUS",
				(uint8_t *)&hdcp->auth.msg.hdcp1.bstatus,
				sizeof(hdcp->auth.msg.hdcp1.bstatus));
		HDCP_DDC_WRITE_TRACE(hdcp, "AN", hdcp->auth.msg.hdcp1.an,
				sizeof(hdcp->auth.msg.hdcp1.an));
		HDCP_DDC_WRITE_TRACE(hdcp, "AKSV", hdcp->auth.msg.hdcp1.aksv,
				sizeof(hdcp->auth.msg.hdcp1.aksv));
		HDCP_DDC_WRITE_TRACE(hdcp, "AINFO", &hdcp->auth.msg.hdcp1.ainfo,
				sizeof(hdcp->auth.msg.hdcp1.ainfo));
		HDCP_DDC_READ_TRACE(hdcp, "RI' / R0'",
				(uint8_t *)&hdcp->auth.msg.hdcp1.r0p,
				sizeof(hdcp->auth.msg.hdcp1.r0p));
		HDCP_DDC_READ_TRACE(hdcp, "BINFO",
				(uint8_t *)&hdcp->auth.msg.hdcp1.binfo_dp,
				sizeof(hdcp->auth.msg.hdcp1.binfo_dp));
		HDCP_DDC_READ_TRACE(hdcp, "KSVLIST", hdcp->auth.msg.hdcp1.ksvlist,
				hdcp->auth.msg.hdcp1.ksvlist_size);
		HDCP_DDC_READ_TRACE(hdcp, "V'", hdcp->auth.msg.hdcp1.vp,
				sizeof(hdcp->auth.msg.hdcp1.vp));
	} else if (is_hdcp2(hdcp)) {
		HDCP_DDC_READ_TRACE(hdcp, "HDCP2Version",
				&hdcp->auth.msg.hdcp2.hdcp2version_hdmi,
				sizeof(hdcp->auth.msg.hdcp2.hdcp2version_hdmi));
		HDCP_DDC_READ_TRACE(hdcp, "Rx Caps", hdcp->auth.msg.hdcp2.rxcaps_dp,
				sizeof(hdcp->auth.msg.hdcp2.rxcaps_dp));
		HDCP_DDC_WRITE_TRACE(hdcp, "AKE Init", hdcp->auth.msg.hdcp2.ake_init,
				sizeof(hdcp->auth.msg.hdcp2.ake_init));
		HDCP_DDC_READ_TRACE(hdcp, "AKE Cert", hdcp->auth.msg.hdcp2.ake_cert,
				sizeof(hdcp->auth.msg.hdcp2.ake_cert));
		HDCP_DDC_WRITE_TRACE(hdcp, "Stored KM",
				hdcp->auth.msg.hdcp2.ake_stored_km,
				sizeof(hdcp->auth.msg.hdcp2.ake_stored_km));
		HDCP_DDC_WRITE_TRACE(hdcp, "No Stored KM",
				hdcp->auth.msg.hdcp2.ake_no_stored_km,
				sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km));
		HDCP_DDC_READ_TRACE(hdcp, "H'", hdcp->auth.msg.hdcp2.ake_h_prime,
				sizeof(hdcp->auth.msg.hdcp2.ake_h_prime));
		HDCP_DDC_READ_TRACE(hdcp, "Pairing Info",
				hdcp->auth.msg.hdcp2.ake_pairing_info,
				sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info));
		HDCP_DDC_WRITE_TRACE(hdcp, "LC Init", hdcp->auth.msg.hdcp2.lc_init,
				sizeof(hdcp->auth.msg.hdcp2.lc_init));
		HDCP_DDC_READ_TRACE(hdcp, "L'", hdcp->auth.msg.hdcp2.lc_l_prime,
				sizeof(hdcp->auth.msg.hdcp2.lc_l_prime));
		HDCP_DDC_WRITE_TRACE(hdcp, "Exchange KS", hdcp->auth.msg.hdcp2.ske_eks,
				sizeof(hdcp->auth.msg.hdcp2.ske_eks));
		HDCP_DDC_READ_TRACE(hdcp, "Rx Status",
				(uint8_t *)&hdcp->auth.msg.hdcp2.rxstatus,
				sizeof(hdcp->auth.msg.hdcp2.rxstatus));
		HDCP_DDC_READ_TRACE(hdcp, "Rx Id List",
				hdcp->auth.msg.hdcp2.rx_id_list,
				hdcp->auth.msg.hdcp2.rx_id_list_size);
		HDCP_DDC_WRITE_TRACE(hdcp, "Rx Id List Ack",
				hdcp->auth.msg.hdcp2.repeater_auth_ack,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_ack));
		HDCP_DDC_WRITE_TRACE(hdcp, "Content Stream Management",
				hdcp->auth.msg.hdcp2.repeater_auth_stream_manage,
				hdcp->auth.msg.hdcp2.stream_manage_size);
		HDCP_DDC_READ_TRACE(hdcp, "Stream Ready",
				hdcp->auth.msg.hdcp2.repeater_auth_stream_ready,
				sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready));
		HDCP_DDC_WRITE_TRACE(hdcp, "Content Stream Type",
				hdcp->auth.msg.hdcp2.content_stream_type_dp,
				sizeof(hdcp->auth.msg.hdcp2.content_stream_type_dp));
	}
}

char *mod_hdcp_status_to_str(int32_t status)
{
	switch (status) {
	case MOD_HDCP_STATUS_SUCCESS:
		return "MOD_HDCP_STATUS_SUCCESS";
	case MOD_HDCP_STATUS_FAILURE:
		return "MOD_HDCP_STATUS_FAILURE";
	case MOD_HDCP_STATUS_RESET_NEEDED:
		return "MOD_HDCP_STATUS_RESET_NEEDED";
	case MOD_HDCP_STATUS_DISPLAY_OUT_OF_BOUND:
		return "MOD_HDCP_STATUS_DISPLAY_OUT_OF_BOUND";
	case MOD_HDCP_STATUS_DISPLAY_NOT_FOUND:
		return "MOD_HDCP_STATUS_DISPLAY_NOT_FOUND";
	case MOD_HDCP_STATUS_INVALID_STATE:
		return "MOD_HDCP_STATUS_INVALID_STATE";
	case MOD_HDCP_STATUS_NOT_IMPLEMENTED:
		return "MOD_HDCP_STATUS_NOT_IMPLEMENTED";
	case MOD_HDCP_STATUS_INTERNAL_POLICY_FAILURE:
		return "MOD_HDCP_STATUS_INTERNAL_POLICY_FAILURE";
	case MOD_HDCP_STATUS_UPDATE_TOPOLOGY_FAILURE:
		return "MOD_HDCP_STATUS_UPDATE_TOPOLOGY_FAILURE";
	case MOD_HDCP_STATUS_CREATE_PSP_SERVICE_FAILURE:
		return "MOD_HDCP_STATUS_CREATE_PSP_SERVICE_FAILURE";
	case MOD_HDCP_STATUS_DESTROY_PSP_SERVICE_FAILURE:
		return "MOD_HDCP_STATUS_DESTROY_PSP_SERVICE_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_CREATE_SESSION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_CREATE_SESSION_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_DESTROY_SESSION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_DESTROY_SESSION_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_VALIDATE_ENCRYPTION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_VALIDATE_ENCRYPTION_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_NOT_HDCP_REPEATER:
		return "MOD_HDCP_STATUS_HDCP1_NOT_HDCP_REPEATER";
	case MOD_HDCP_STATUS_HDCP1_NOT_CAPABLE:
		return "MOD_HDCP_STATUS_HDCP1_NOT_CAPABLE";
	case MOD_HDCP_STATUS_HDCP1_R0_PRIME_PENDING:
		return "MOD_HDCP_STATUS_HDCP1_R0_PRIME_PENDING";
	case MOD_HDCP_STATUS_HDCP1_VALIDATE_RX_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_VALIDATE_RX_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_BKSV_REVOKED:
		return "MOD_HDCP_STATUS_HDCP1_BKSV_REVOKED";
	case MOD_HDCP_STATUS_HDCP1_KSV_LIST_NOT_READY:
		return "MOD_HDCP_STATUS_HDCP1_KSV_LIST_NOT_READY";
	case MOD_HDCP_STATUS_HDCP1_VALIDATE_KSV_LIST_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_VALIDATE_KSV_LIST_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_KSV_LIST_REVOKED:
		return "MOD_HDCP_STATUS_HDCP1_KSV_LIST_REVOKED";
	case MOD_HDCP_STATUS_HDCP1_ENABLE_ENCRYPTION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_ENABLE_ENCRYPTION_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_ENABLE_STREAM_ENCRYPTION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_ENABLE_STREAM_ENCRYPTION_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_MAX_CASCADE_EXCEEDED_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_MAX_CASCADE_EXCEEDED_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_MAX_DEVS_EXCEEDED_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_MAX_DEVS_EXCEEDED_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_DEVICE_COUNT_MISMATCH_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_DEVICE_COUNT_MISMATCH_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_LINK_INTEGRITY_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_LINK_INTEGRITY_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_REAUTH_REQUEST_ISSUED:
		return "MOD_HDCP_STATUS_HDCP1_REAUTH_REQUEST_ISSUED";
	case MOD_HDCP_STATUS_HDCP1_LINK_MAINTENANCE_FAILURE:
		return "MOD_HDCP_STATUS_HDCP1_LINK_MAINTENANCE_FAILURE";
	case MOD_HDCP_STATUS_HDCP1_INVALID_BKSV:
		return "MOD_HDCP_STATUS_HDCP1_INVALID_BKSV";
	case MOD_HDCP_STATUS_DDC_FAILURE:
		return "MOD_HDCP_STATUS_DDC_FAILURE";
	case MOD_HDCP_STATUS_INVALID_OPERATION:
		return "MOD_HDCP_STATUS_INVALID_OPERATION";
	case MOD_HDCP_STATUS_HDCP2_NOT_CAPABLE:
		return "MOD_HDCP_STATUS_HDCP2_NOT_CAPABLE";
	case MOD_HDCP_STATUS_HDCP2_CREATE_SESSION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_CREATE_SESSION_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_DESTROY_SESSION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_DESTROY_SESSION_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_PREP_AKE_INIT_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_PREP_AKE_INIT_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_AKE_CERT_PENDING:
		return "MOD_HDCP_STATUS_HDCP2_AKE_CERT_PENDING";
	case MOD_HDCP_STATUS_HDCP2_H_PRIME_PENDING:
		return "MOD_HDCP_STATUS_HDCP2_H_PRIME_PENDING";
	case MOD_HDCP_STATUS_HDCP2_PAIRING_INFO_PENDING:
		return "MOD_HDCP_STATUS_HDCP2_PAIRING_INFO_PENDING";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_AKE_CERT_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_AKE_CERT_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_AKE_CERT_REVOKED:
		return "MOD_HDCP_STATUS_HDCP2_AKE_CERT_REVOKED";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_H_PRIME_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_H_PRIME_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_PAIRING_INFO_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_PAIRING_INFO_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_PREP_LC_INIT_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_PREP_LC_INIT_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_L_PRIME_PENDING:
		return "MOD_HDCP_STATUS_HDCP2_L_PRIME_PENDING";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_L_PRIME_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_L_PRIME_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_PREP_EKS_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_PREP_EKS_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_ENABLE_ENCRYPTION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_ENABLE_ENCRYPTION_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_RX_ID_LIST_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_RX_ID_LIST_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_REVOKED:
		return "MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_REVOKED";
	case MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_NOT_READY:
		return "MOD_HDCP_STATUS_HDCP2_RX_ID_LIST_NOT_READY";
	case MOD_HDCP_STATUS_HDCP2_ENABLE_STREAM_ENCRYPTION_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_ENABLE_STREAM_ENCRYPTION_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_STREAM_READY_PENDING:
		return "MOD_HDCP_STATUS_HDCP2_STREAM_READY_PENDING";
	case MOD_HDCP_STATUS_HDCP2_VALIDATE_STREAM_READY_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_VALIDATE_STREAM_READY_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_PREPARE_STREAM_MANAGEMENT_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_PREPARE_STREAM_MANAGEMENT_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_REAUTH_REQUEST:
		return "MOD_HDCP_STATUS_HDCP2_REAUTH_REQUEST";
	case MOD_HDCP_STATUS_HDCP2_REAUTH_LINK_INTEGRITY_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_REAUTH_LINK_INTEGRITY_FAILURE";
	case MOD_HDCP_STATUS_HDCP2_DEVICE_COUNT_MISMATCH_FAILURE:
		return "MOD_HDCP_STATUS_HDCP2_DEVICE_COUNT_MISMATCH_FAILURE";
	case MOD_HDCP_STATUS_UNSUPPORTED_PSP_VER_FAILURE:
		return "MOD_HDCP_STATUS_UNSUPPORTED_PSP_VER_FAILURE";
	default:
		return "MOD_HDCP_STATUS_UNKNOWN";
	}
}

char *mod_hdcp_state_id_to_str(int32_t id)
{
	switch (id) {
	case HDCP_UNINITIALIZED:
		return "HDCP_UNINITIALIZED";
	case HDCP_INITIALIZED:
		return "HDCP_INITIALIZED";
	case HDCP_CP_NOT_DESIRED:
		return "HDCP_CP_NOT_DESIRED";
	case H1_A0_WAIT_FOR_ACTIVE_RX:
		return "H1_A0_WAIT_FOR_ACTIVE_RX";
	case H1_A1_EXCHANGE_KSVS:
		return "H1_A1_EXCHANGE_KSVS";
	case H1_A2_COMPUTATIONS_A3_VALIDATE_RX_A6_TEST_FOR_REPEATER:
		return "H1_A2_COMPUTATIONS_A3_VALIDATE_RX_A6_TEST_FOR_REPEATER";
	case H1_A45_AUTHENTICATED:
		return "H1_A45_AUTHENTICATED";
	case H1_A8_WAIT_FOR_READY:
		return "H1_A8_WAIT_FOR_READY";
	case H1_A9_READ_KSV_LIST:
		return "H1_A9_READ_KSV_LIST";
	case D1_A0_DETERMINE_RX_HDCP_CAPABLE:
		return "D1_A0_DETERMINE_RX_HDCP_CAPABLE";
	case D1_A1_EXCHANGE_KSVS:
		return "D1_A1_EXCHANGE_KSVS";
	case D1_A23_WAIT_FOR_R0_PRIME:
		return "D1_A23_WAIT_FOR_R0_PRIME";
	case D1_A2_COMPUTATIONS_A3_VALIDATE_RX_A5_TEST_FOR_REPEATER:
		return "D1_A2_COMPUTATIONS_A3_VALIDATE_RX_A5_TEST_FOR_REPEATER";
	case D1_A4_AUTHENTICATED:
		return "D1_A4_AUTHENTICATED";
	case D1_A6_WAIT_FOR_READY:
		return "D1_A6_WAIT_FOR_READY";
	case D1_A7_READ_KSV_LIST:
		return "D1_A7_READ_KSV_LIST";
	case H2_A0_KNOWN_HDCP2_CAPABLE_RX:
		return "H2_A0_KNOWN_HDCP2_CAPABLE_RX";
	case H2_A1_SEND_AKE_INIT:
		return "H2_A1_SEND_AKE_INIT";
	case H2_A1_VALIDATE_AKE_CERT:
		return "H2_A1_VALIDATE_AKE_CERT";
	case H2_A1_SEND_NO_STORED_KM:
		return "H2_A1_SEND_NO_STORED_KM";
	case H2_A1_READ_H_PRIME:
		return "H2_A1_READ_H_PRIME";
	case H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		return "H2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME";
	case H2_A1_SEND_STORED_KM:
		return "H2_A1_SEND_STORED_KM";
	case H2_A1_VALIDATE_H_PRIME:
		return "H2_A1_VALIDATE_H_PRIME";
	case H2_A2_LOCALITY_CHECK:
		return "H2_A2_LOCALITY_CHECK";
	case H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		return "H2_A3_EXCHANGE_KS_AND_TEST_FOR_REPEATER";
	case H2_ENABLE_ENCRYPTION:
		return "H2_ENABLE_ENCRYPTION";
	case H2_A5_AUTHENTICATED:
		return "H2_A5_AUTHENTICATED";
	case H2_A6_WAIT_FOR_RX_ID_LIST:
		return "H2_A6_WAIT_FOR_RX_ID_LIST";
	case H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		return "H2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK";
	case H2_A9_SEND_STREAM_MANAGEMENT:
		return "H2_A9_SEND_STREAM_MANAGEMENT";
	case H2_A9_VALIDATE_STREAM_READY:
		return "H2_A9_VALIDATE_STREAM_READY";
	case D2_A0_DETERMINE_RX_HDCP_CAPABLE:
		return "D2_A0_DETERMINE_RX_HDCP_CAPABLE";
	case D2_A1_SEND_AKE_INIT:
		return "D2_A1_SEND_AKE_INIT";
	case D2_A1_VALIDATE_AKE_CERT:
		return "D2_A1_VALIDATE_AKE_CERT";
	case D2_A1_SEND_NO_STORED_KM:
		return "D2_A1_SEND_NO_STORED_KM";
	case D2_A1_READ_H_PRIME:
		return "D2_A1_READ_H_PRIME";
	case D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME:
		return "D2_A1_READ_PAIRING_INFO_AND_VALIDATE_H_PRIME";
	case D2_A1_SEND_STORED_KM:
		return "D2_A1_SEND_STORED_KM";
	case D2_A1_VALIDATE_H_PRIME:
		return "D2_A1_VALIDATE_H_PRIME";
	case D2_A2_LOCALITY_CHECK:
		return "D2_A2_LOCALITY_CHECK";
	case D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER:
		return "D2_A34_EXCHANGE_KS_AND_TEST_FOR_REPEATER";
	case D2_SEND_CONTENT_STREAM_TYPE:
		return "D2_SEND_CONTENT_STREAM_TYPE";
	case D2_ENABLE_ENCRYPTION:
		return "D2_ENABLE_ENCRYPTION";
	case D2_A5_AUTHENTICATED:
		return "D2_A5_AUTHENTICATED";
	case D2_A6_WAIT_FOR_RX_ID_LIST:
		return "D2_A6_WAIT_FOR_RX_ID_LIST";
	case D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK:
		return "D2_A78_VERIFY_RX_ID_LIST_AND_SEND_ACK";
	case D2_A9_SEND_STREAM_MANAGEMENT:
		return "D2_A9_SEND_STREAM_MANAGEMENT";
	case D2_A9_VALIDATE_STREAM_READY:
		return "D2_A9_VALIDATE_STREAM_READY";
	default:
		return "UNKNOWN_STATE_ID";
	}
}

