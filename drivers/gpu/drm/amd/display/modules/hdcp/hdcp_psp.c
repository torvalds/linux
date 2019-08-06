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

enum mod_hdcp_status mod_hdcp_remove_display_topology(struct mod_hdcp *hdcp)
{

	struct psp_context *psp = hdcp->config.psp.handle;
	struct ta_dtm_shared_memory *dtm_cmd;
	struct mod_hdcp_display *display = NULL;
	uint8_t i;

	dtm_cmd = (struct ta_dtm_shared_memory *)psp->dtm_context.dtm_shared_buf;

	for (i = 0; i < MAX_NUM_OF_DISPLAYS; i++) {
		if (hdcp->connection.displays[i].state == MOD_HDCP_DISPLAY_ACTIVE_AND_ADDED) {

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
				TA_DTM_HDCP_VERSION_MAX_SUPPORTED__1_x;
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

	if (hdcp_cmd->hdcp_status != TA_HDCP_STATUS__SUCCESS)
		return MOD_HDCP_STATUS_HDCP1_CREATE_SESSION_FAILURE;

	hdcp->auth.id = hdcp_cmd->out_msg.hdcp1_create_session.session_handle;
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

