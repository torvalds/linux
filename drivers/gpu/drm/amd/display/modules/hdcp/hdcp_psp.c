/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#define MAX_NUM_DISPLAYS 24


#include "hdcp.h"

#include "amdgpu.h"
#include "hdcp_psp.h"

static void hdcp2_message_init(struct mod_hdcp *hdcp,
			       struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *in)
{
	in->session_handle = hdcp->auth.id;
	in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__NULL_MESSAGE;
	in->prepare.msg2_id = TA_HDCP_HDCP2_MSG_ID__NULL_MESSAGE;
	in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__NULL_MESSAGE;
	in->process.msg1_desc.msg_size = 0;
	in->process.msg2_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__NULL_MESSAGE;
	in->process.msg2_desc.msg_size = 0;
	in->process.msg3_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__NULL_MESSAGE;
	in->process.msg3_desc.msg_size = 0;
}
enum mod_hdcp_status mod_hdcp_remove_display_topology(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_dtm_shared_memory *dtm_cmd;
	struct mod_hdcp_display *display = NULL;
	uint8_t i;

	dtm_cmd = (struct ta_dtm_shared_memory *)psp->dtm_context.dtm_shared_buf;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (is_display_added(&(hdcp->connection.displays[i]))) {

			memset(dtm_cmd, 0, sizeof(struct ta_dtm_shared_memory));

			display = &hdcp->connection.displays[i];

			dtm_cmd->cmd_id = TA_DTM_COMMAND__TOPOLOGY_UPDATE_V2;
			dtm_cmd->dtm_in_message.topology_update_v2.display_handle = display->index;
			dtm_cmd->dtm_in_message.topology_update_v2.is_active = 0;
			dtm_cmd->dtm_status = TA_DTM_STATUS__GENERIC_FAILURE;

			psp_dtm_invoke(psp, dtm_cmd->cmd_id);

			if (dtm_cmd->dtm_status != TA_DTM_STATUS__SUCCESS)
				return MOD_HDCP_STATUS_UPDATE_TOPOLOGY_FAILURE;

			display->state = MOD_HDCP_DISPLAY_ACTIVE;
			HDCP_TOP_REMOVE_DISPLAY_TRACE(hdcp, display->index);
		}
	}

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_add_display_topology(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_dtm_shared_memory *dtm_cmd;
	struct mod_hdcp_display *display = NULL;
	struct mod_hdcp_link *link = &hdcp->connection.link;
	uint8_t i;

	if (!psp->dtm_context.dtm_initialized) {
		DRM_ERROR("Failed to add display topology, DTM TA is not initialized.");
		return MOD_HDCP_STATUS_FAILURE;
	}

	dtm_cmd = (struct ta_dtm_shared_memory *)psp->dtm_context.dtm_shared_buf;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (hdcp->connection.displays[i].state == MOD_HDCP_DISPLAY_ACTIVE) {
			display = &hdcp->connection.displays[i];

			memset(dtm_cmd, 0, sizeof(struct ta_dtm_shared_memory));

			dtm_cmd->cmd_id = TA_DTM_COMMAND__TOPOLOGY_UPDATE_V2;
			dtm_cmd->dtm_in_message.topology_update_v2.display_handle = display->index;
			dtm_cmd->dtm_in_message.topology_update_v2.is_active = 1;
			dtm_cmd->dtm_in_message.topology_update_v2.controller = display->controller;
			dtm_cmd->dtm_in_message.topology_update_v2.ddc_line = link->ddc_line;
			dtm_cmd->dtm_in_message.topology_update_v2.dig_be = link->dig_be;
			dtm_cmd->dtm_in_message.topology_update_v2.dig_fe = display->dig_fe;
			dtm_cmd->dtm_in_message.topology_update_v2.dp_mst_vcid = display->vc_id;
			dtm_cmd->dtm_in_message.topology_update_v2.max_hdcp_supported_version =
				TA_DTM_HDCP_VERSION_MAX_SUPPORTED__2_2;
			dtm_cmd->dtm_status = TA_DTM_STATUS__GENERIC_FAILURE;

			psp_dtm_invoke(psp, dtm_cmd->cmd_id);

			if (dtm_cmd->dtm_status != TA_DTM_STATUS__SUCCESS)
				return MOD_HDCP_STATUS_UPDATE_TOPOLOGY_FAILURE;

			display->state = MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED;
			HDCP_TOP_ADD_DISPLAY_TRACE(hdcp, display->index);
		}
	}

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_create_session(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct mod_hdcp_display *display = get_first_added_display(hdcp);
	struct ta_hdcp_shared_memory *hdcp_cmd;

	if (!psp->hdcp_context.hdcp_initialized) {
		DRM_ERROR("Failed to create hdcp session. HDCP TA is not initialized.");
		return MOD_HDCP_STATUS_FAILURE;
	}

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_create_session.display_handle = display->index;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_CREATE_SESSION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	hdcp->auth.id = hdcp_cmd->out_msg.hdcp1_create_session.session_handle;

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_CREATE_SESSION_FAILURE;

	hdcp->auth.msg.hdcp1.ainfo = hdcp_cmd->out_msg.hdcp1_create_session.ainfo_primary;
	memcpy(hdcp->auth.msg.hdcp1.aksv, hdcp_cmd->out_msg.hdcp1_create_session.aksv_primary,
		sizeof(hdcp->auth.msg.hdcp1.aksv));
	memcpy(hdcp->auth.msg.hdcp1.an, hdcp_cmd->out_msg.hdcp1_create_session.an_primary,
		sizeof(hdcp->auth.msg.hdcp1.an));

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_destroy_session(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_destroy_session.session_handle = hdcp->auth.id;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_DESTROY_SESSION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_DESTROY_SESSION_FAILURE;

	HDCP_TOP_HDCP1_DESTROY_SESSION_TRACE(hdcp);

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_validate_rx(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_first_part_authentication.session_handle = hdcp->auth.id;

	memcpy(hdcp_cmd->in_msg.hdcp1_first_part_authentication.bksv_primary, hdcp->auth.msg.hdcp1.bksv,
		TA_HDCP__HDCP1_KSV_SIZE);

	hdcp_cmd->in_msg.hdcp1_first_part_authentication.r0_prime_primary = hdcp->auth.msg.hdcp1.r0p;
	hdcp_cmd->in_msg.hdcp1_first_part_authentication.bcaps = hdcp->auth.msg.hdcp1.bcaps;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_FIRST_PART_AUTHENTICATION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_VALIDATE_RX_FAILURE;

	if (hdcp_cmd->out_msg.hdcp1_first_part_authentication.authentication_status ==
	    TA_HDCP_AUTHENTICATION_STATUS__HDCP1_FIRST_PART_COMPLETE) {
		/* needs second part of authentication */
		hdcp->connection.is_repeater = 1;
	} else if (hdcp_cmd->out_msg.hdcp1_first_part_authentication.authentication_status ==
		   TA_HDCP_AUTHENTICATION_STATUS__HDCP1_AUTHENTICATED) {
		hdcp->connection.is_repeater = 0;
	} else
		return MOD_HDCP_STATUS_HDCP1_VALIDATE_RX_FAILURE;


	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_enable_encryption(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct mod_hdcp_display *display = get_first_added_display(hdcp);

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_enable_encryption.session_handle = hdcp->auth.id;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_ENABLE_ENCRYPTION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_ENABLE_ENCRYPTION;

	if (!is_dp_mst_hdcp(hdcp)) {
		display->state = MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
		HDCP_HDCP1_ENABLED_TRACE(hdcp, display->index);
	}
	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_validate_ksvlist_vp(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_second_part_authentication.session_handle = hdcp->auth.id;

	hdcp_cmd->in_msg.hdcp1_second_part_authentication.ksv_list_size = hdcp->auth.msg.hdcp1.ksvlist_size;
	memcpy(hdcp_cmd->in_msg.hdcp1_second_part_authentication.ksv_list, hdcp->auth.msg.hdcp1.ksvlist,
	       hdcp->auth.msg.hdcp1.ksvlist_size);

	memcpy(hdcp_cmd->in_msg.hdcp1_second_part_authentication.v_prime, hdcp->auth.msg.hdcp1.vp,
	       sizeof(hdcp->auth.msg.hdcp1.vp));

	hdcp_cmd->in_msg.hdcp1_second_part_authentication.bstatus_binfo =
		is_dp_hdcp(hdcp) ? hdcp->auth.msg.hdcp1.binfo_dp : hdcp->auth.msg.hdcp1.bstatus;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_SECOND_PART_AUTHENTICATION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_VALIDATE_KSV_LIST_FAILURE;

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_enable_dp_stream_encryption(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	int i = 0;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {

		if (hdcp->connection.displays[i].state != MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED ||
		    hdcp->connection.displays[i].adjust.disable)
			continue;

		memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

		hdcp_cmd->in_msg.hdcp1_enable_dp_stream_encryption.session_handle = hdcp->auth.id;
		hdcp_cmd->in_msg.hdcp1_enable_dp_stream_encryption.display_handle = hdcp->connection.displays[i].index;
		hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_ENABLE_DP_STREAM_ENCRYPTION;

		psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

		if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
			return MOD_HDCP_STATUS_HDCP1_ENABLE_STREAM_ENCRYPTION_FAILURE;

		hdcp->connection.displays[i].state = MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
		HDCP_HDCP1_ENABLED_TRACE(hdcp, hdcp->connection.displays[i].index);
	}

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp1_link_maintenance(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;

	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp1_get_encryption_status.session_handle = hdcp->auth.id;

	hdcp_cmd->out_msg.hdcp1_get_encryption_status.protection_level = 0;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP1_GET_ENCRYPTION_STATUS;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_LINK_MAINTENANCE_FAILURE;

	return (hdcp_cmd->out_msg.hdcp1_get_encryption_status.protection_level == 1)
		       ? MOD_HDCP_STATUS_SUCCESS
		       : MOD_HDCP_STATUS_HDCP1_LINK_MAINTENANCE_FAILURE;
}

enum mod_hdcp_status mod_hdcp_hdcp1_get_link_encryption_status(struct mod_hdcp *hdcp,
							       enum mod_hdcp_encryption_status *encryption_status)
{
	*encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	if (mod_hdcp_hdcp1_link_maintenance(hdcp) != MOD_HDCP_STATUS_SUCCESS)
		return MOD_HDCP_STATUS_FAILURE;

	*encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP1_ON;

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_create_session(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct mod_hdcp_display *display = get_first_added_display(hdcp);

	if (!psp->hdcp_context.hdcp_initialized) {
		DRM_ERROR("Failed to create hdcp session, HDCP TA is not initialized");
		return MOD_HDCP_STATUS_FAILURE;
	}

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	if (!display)
		return MOD_HDCP_STATUS_DISPLAY_NOT_FOUND;

	hdcp_cmd->in_msg.hdcp2_create_session_v2.display_handle = display->index;

	if (hdcp->connection.link.adjust.hdcp2.force_type == MOD_HDCP_FORCE_TYPE_0)
		hdcp_cmd->in_msg.hdcp2_create_session_v2.negotiate_content_type =
			TA_HDCP2_CONTENT_TYPE_NEGOTIATION_TYPE__FORCE_TYPE0;
	else if (hdcp->connection.link.adjust.hdcp2.force_type == MOD_HDCP_FORCE_TYPE_1)
		hdcp_cmd->in_msg.hdcp2_create_session_v2.negotiate_content_type =
			TA_HDCP2_CONTENT_TYPE_NEGOTIATION_TYPE__FORCE_TYPE1;
	else if (hdcp->connection.link.adjust.hdcp2.force_type == MOD_HDCP_FORCE_TYPE_MAX)
		hdcp_cmd->in_msg.hdcp2_create_session_v2.negotiate_content_type =
			TA_HDCP2_CONTENT_TYPE_NEGOTIATION_TYPE__MAX_SUPPORTED;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_CREATE_SESSION_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_CREATE_SESSION_FAILURE;

	hdcp->auth.id = hdcp_cmd->out_msg.hdcp2_create_session_v2.session_handle;

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_destroy_session(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp2_destroy_session.session_handle = hdcp->auth.id;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_DESTROY_SESSION;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_DESTROY_SESSION_FAILURE;

	HDCP_TOP_HDCP2_DESTROY_SESSION_TRACE(hdcp);

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_prepare_ake_init(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;
	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__AKE_INIT;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_PREP_AKE_INIT_FAILURE;

	memcpy(&hdcp->auth.msg.hdcp2.ake_init[0], &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.ake_init));

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_validate_ake_cert(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__AKE_SEND_CERT;
	msg_in->process.msg1_desc.msg_size = TA_HDCP_HDCP2_MSG_ID_MAX_SIZE__AKE_SEND_CERT;

	memcpy(&msg_in->process.receiver_message[0], hdcp->auth.msg.hdcp2.ake_cert,
	       sizeof(hdcp->auth.msg.hdcp2.ake_cert));

	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__AKE_NO_STORED_KM;
	msg_in->prepare.msg2_id = TA_HDCP_HDCP2_MSG_ID__AKE_STORED_KM;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_AKE_CERT_FAILURE;

	memcpy(hdcp->auth.msg.hdcp2.ake_no_stored_km, &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km));

	memcpy(hdcp->auth.msg.hdcp2.ake_stored_km,
	       &msg_out->prepare.transmitter_message[sizeof(hdcp->auth.msg.hdcp2.ake_no_stored_km)],
	       sizeof(hdcp->auth.msg.hdcp2.ake_stored_km));

	if (msg_out->process.msg1_status == TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS) {
		hdcp->connection.is_km_stored = msg_out->process.is_km_stored ? 1 : 0;
		hdcp->connection.is_repeater = msg_out->process.is_repeater ? 1 : 0;
		return MOD_HDCP_STATUS_SUCCESS;
	}

	return MOD_HDCP_STATUS_FAILURE;
}

enum mod_hdcp_status mod_hdcp_hdcp2_validate_h_prime(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__AKE_SEND_H_PRIME;
	msg_in->process.msg1_desc.msg_size = TA_HDCP_HDCP2_MSG_ID_MAX_SIZE__AKE_SEND_H_PRIME;

	memcpy(&msg_in->process.receiver_message[0], hdcp->auth.msg.hdcp2.ake_h_prime,
	       sizeof(hdcp->auth.msg.hdcp2.ake_h_prime));

	if (!hdcp->connection.is_km_stored) {
		msg_in->process.msg2_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__AKE_SEND_PAIRING_INFO;
		msg_in->process.msg2_desc.msg_size = TA_HDCP_HDCP2_MSG_ID_MAX_SIZE__AKE_SEND_PAIRING_INFO;
		memcpy(&msg_in->process.receiver_message[sizeof(hdcp->auth.msg.hdcp2.ake_h_prime)],
		       hdcp->auth.msg.hdcp2.ake_pairing_info, sizeof(hdcp->auth.msg.hdcp2.ake_pairing_info));
	}

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_H_PRIME_FAILURE;

	if (msg_out->process.msg1_status != TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_H_PRIME_FAILURE;
	else if (!hdcp->connection.is_km_stored &&
		 msg_out->process.msg2_status != TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_PAIRING_INFO_FAILURE;


	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_prepare_lc_init(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__LC_INIT;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_PREP_LC_INIT_FAILURE;

	memcpy(hdcp->auth.msg.hdcp2.lc_init, &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.lc_init));

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_validate_l_prime(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__LC_SEND_L_PRIME;
	msg_in->process.msg1_desc.msg_size = TA_HDCP_HDCP2_MSG_ID_MAX_SIZE__LC_SEND_L_PRIME;

	memcpy(&msg_in->process.receiver_message[0], hdcp->auth.msg.hdcp2.lc_l_prime,
	       sizeof(hdcp->auth.msg.hdcp2.lc_l_prime));

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_L_PRIME_FAILURE;

	if (msg_out->process.msg1_status != TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_L_PRIME_FAILURE;

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_prepare_eks(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__SKE_SEND_EKS;

	if (is_dp_hdcp(hdcp))
		msg_in->prepare.msg2_id = TA_HDCP_HDCP2_MSG_ID__SIGNAL_CONTENT_STREAM_TYPE_DP;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;
	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_PREP_EKS_FAILURE;

	memcpy(hdcp->auth.msg.hdcp2.ske_eks, &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.ske_eks));
	msg_out->prepare.msg1_desc.msg_size = sizeof(hdcp->auth.msg.hdcp2.ske_eks);

	if (is_dp_hdcp(hdcp)) {
		memcpy(hdcp->auth.msg.hdcp2.content_stream_type_dp,
		       &msg_out->prepare.transmitter_message[sizeof(hdcp->auth.msg.hdcp2.ske_eks)],
		       sizeof(hdcp->auth.msg.hdcp2.content_stream_type_dp));
	}

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_enable_encryption(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct mod_hdcp_display *display = get_first_added_display(hdcp);

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	if (!display)
		return MOD_HDCP_STATUS_DISPLAY_NOT_FOUND;

	hdcp_cmd->in_msg.hdcp1_enable_encryption.session_handle = hdcp->auth.id;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_SET_ENCRYPTION;
	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_ENABLE_ENCRYPTION_FAILURE;

	if (!is_dp_mst_hdcp(hdcp)) {
		display->state = MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
		HDCP_HDCP2_ENABLED_TRACE(hdcp, display->index);
	}

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_validate_rx_id_list(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__REPEATERAUTH_SEND_RECEIVERID_LIST;
	msg_in->process.msg1_desc.msg_size = sizeof(hdcp->auth.msg.hdcp2.rx_id_list);
	memcpy(&msg_in->process.receiver_message[0], hdcp->auth.msg.hdcp2.rx_id_list,
	       sizeof(hdcp->auth.msg.hdcp2.rx_id_list));

	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__REPEATERAUTH_SEND_ACK;

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_VALIDATE_RX_ID_LIST_FAILURE;

	memcpy(hdcp->auth.msg.hdcp2.repeater_auth_ack, &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.repeater_auth_ack));

	if (msg_out->process.msg1_status == TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS) {
		hdcp->connection.is_km_stored = msg_out->process.is_km_stored ? 1 : 0;
		hdcp->connection.is_repeater = msg_out->process.is_repeater ? 1 : 0;
		return MOD_HDCP_STATUS_SUCCESS;
	}


	return MOD_HDCP_STATUS_HDCP2_VALIDATE_RX_ID_LIST_FAILURE;
}

enum mod_hdcp_status mod_hdcp_hdcp2_enable_dp_stream_encryption(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	uint8_t i;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);


	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (hdcp->connection.displays[i].state != MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED ||
		    hdcp->connection.displays[i].adjust.disable)
			continue;
		hdcp_cmd->in_msg.hdcp2_enable_dp_stream_encryption.display_handle = hdcp->connection.displays[i].index;
		hdcp_cmd->in_msg.hdcp2_enable_dp_stream_encryption.session_handle = hdcp->auth.id;

		hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_ENABLE_DP_STREAM_ENCRYPTION;
		psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

		if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
			break;

		hdcp->connection.displays[i].state = MOD_HDCP_DISPLAY_ENCRYPTION_ENABLED;
		HDCP_HDCP2_ENABLED_TRACE(hdcp, hdcp->connection.displays[i].index);
	}

	return (hdcp_cmd->hdcp_status == TA_HDCP_STATUS__SUCCESS) ? MOD_HDCP_STATUS_SUCCESS
								  : MOD_HDCP_STATUS_HDCP2_ENABLE_STREAM_ENCRYPTION;
}

enum mod_hdcp_status mod_hdcp_hdcp2_prepare_stream_management(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->prepare.msg1_id = TA_HDCP_HDCP2_MSG_ID__REPEATERAUTH_STREAM_MANAGE;


	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;
	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP2_PREPARE_STREAM_MANAGEMENT_FAILURE;

	hdcp->auth.msg.hdcp2.stream_manage_size = msg_out->prepare.msg1_desc.msg_size;

	memcpy(hdcp->auth.msg.hdcp2.repeater_auth_stream_manage, &msg_out->prepare.transmitter_message[0],
	       sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_manage));

	return MOD_HDCP_STATUS_SUCCESS;
}

enum mod_hdcp_status mod_hdcp_hdcp2_validate_stream_ready(struct mod_hdcp *hdcp)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_input_v2 *msg_in;
	struct ta_hdcp_cmd_hdcp2_process_prepare_authentication_message_output_v2 *msg_out;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;
	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	msg_in = &hdcp_cmd->in_msg.hdcp2_prepare_process_authentication_message_v2;
	msg_out = &hdcp_cmd->out_msg.hdcp2_prepare_process_authentication_message_v2;

	hdcp2_message_init(hdcp, msg_in);

	msg_in->process.msg1_desc.msg_id = TA_HDCP_HDCP2_MSG_ID__REPEATERAUTH_STREAM_READY;

	msg_in->process.msg1_desc.msg_size = sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready);

	memcpy(&msg_in->process.receiver_message[0], hdcp->auth.msg.hdcp2.repeater_auth_stream_ready,
	       sizeof(hdcp->auth.msg.hdcp2.repeater_auth_stream_ready));

	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_PREPARE_PROCESS_AUTHENTICATION_MSG_V2;
	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	return (hdcp_cmd->hdcp_status == TA_HDCP_STATUS__SUCCESS) &&
			       (msg_out->process.msg1_status == TA_HDCP2_MSG_AUTHENTICATION_STATUS__SUCCESS)
		       ? MOD_HDCP_STATUS_SUCCESS
		       : MOD_HDCP_STATUS_HDCP2_VALIDATE_STREAM_READY_FAILURE;
}

enum mod_hdcp_status mod_hdcp_hdcp2_get_link_encryption_status(struct mod_hdcp *hdcp,
							       enum mod_hdcp_encryption_status *encryption_status)
{
	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_hdcp_shared_memory *hdcp_cmd;

	hdcp_cmd = (struct ta_hdcp_shared_memory *)psp->hdcp_context.hdcp_shared_buf;

	memset(hdcp_cmd, 0, sizeof(struct ta_hdcp_shared_memory));

	hdcp_cmd->in_msg.hdcp2_get_encryption_status.session_handle = hdcp->auth.id;
	hdcp_cmd->out_msg.hdcp2_get_encryption_status.protection_level = 0;
	hdcp_cmd->cmd_id = TA_HDCP_COMMAND__HDCP2_GET_ENCRYPTION_STATUS;
	*encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	psp_hdcp_invoke(psp, hdcp_cmd->cmd_id);

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_FAILURE;

	if (hdcp_cmd->out_msg.hdcp2_get_encryption_status.protection_level == 1) {
		if (hdcp_cmd->out_msg.hdcp2_get_encryption_status.hdcp2_type == TA_HDCP2_CONTENT_TYPE__TYPE1)
			*encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON;
		else
			*encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON;
	}

	return MOD_HDCP_STATUS_SUCCESS;
}
