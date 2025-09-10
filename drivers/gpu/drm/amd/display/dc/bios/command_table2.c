/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "ObjectID.h"

#include "atomfirmware.h"
#include "atom.h"
#include "include/bios_parser_interface.h"

#include "command_table2.h"
#include "command_table_helper2.h"
#include "bios_parser_helper.h"
#include "bios_parser_types_internal2.h"
#include "amdgpu.h"

#include "dc_dmub_srv.h"
#include "dc.h"

#define DC_LOGGER \
	bp->base.ctx->logger

#define GET_INDEX_INTO_MASTER_TABLE(MasterOrData, FieldName)\
	(offsetof(struct atom_master_list_of_##MasterOrData##_functions_v2_1, FieldName) / sizeof(uint16_t))

#define EXEC_BIOS_CMD_TABLE(fname, params)\
	(amdgpu_atom_execute_table(((struct amdgpu_device *)bp->base.ctx->driver_context)->mode_info.atom_context, \
		GET_INDEX_INTO_MASTER_TABLE(command, fname), \
		(uint32_t *)&params, sizeof(params)) == 0)

#define BIOS_CMD_TABLE_REVISION(fname, frev, crev)\
	amdgpu_atom_parse_cmd_header(((struct amdgpu_device *)bp->base.ctx->driver_context)->mode_info.atom_context, \
		GET_INDEX_INTO_MASTER_TABLE(command, fname), &frev, &crev)

#define BIOS_CMD_TABLE_PARA_REVISION(fname)\
	bios_cmd_table_para_revision(bp->base.ctx->driver_context, \
			GET_INDEX_INTO_MASTER_TABLE(command, fname))



static uint32_t bios_cmd_table_para_revision(void *dev,
					     uint32_t index)
{
	struct amdgpu_device *adev = dev;
	uint8_t frev, crev;

	if (amdgpu_atom_parse_cmd_header(adev->mode_info.atom_context,
					index,
					&frev, &crev))
		return crev;
	else
		return 0;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  D I G E N C O D E R C O N T R O L
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result encoder_control_digx_v1_5(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static enum bp_result encoder_control_fallback(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static void init_dig_encoder_control(struct bios_parser *bp)
{
	uint32_t version =
		BIOS_CMD_TABLE_PARA_REVISION(digxencodercontrol);

	switch (version) {
	case 5:
		bp->cmd_tbl.dig_encoder_control = encoder_control_digx_v1_5;
		break;
	default:
		bp->cmd_tbl.dig_encoder_control = encoder_control_fallback;
		break;
	}
}

static void encoder_control_dmcub(
		struct dc_dmub_srv *dmcub,
		struct dig_encoder_stream_setup_parameters_v1_5 *dig)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.digx_encoder_control.header.type = DMUB_CMD__VBIOS;
	cmd.digx_encoder_control.header.sub_type =
		DMUB_CMD__VBIOS_DIGX_ENCODER_CONTROL;
	cmd.digx_encoder_control.header.payload_bytes =
		sizeof(cmd.digx_encoder_control) -
		sizeof(cmd.digx_encoder_control.header);
	cmd.digx_encoder_control.encoder_control.dig.stream_param = *dig;

	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static enum bp_result encoder_control_digx_v1_5(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	struct dig_encoder_stream_setup_parameters_v1_5 params = {0};

	params.digid = (uint8_t)(cntl->engine_id);
	params.action = bp->cmd_helper->encoder_action_to_atom(cntl->action);

	params.pclk_10khz = cntl->pixel_clock / 10;
	params.digmode =
			(uint8_t)(bp->cmd_helper->encoder_mode_bp_to_atom(
					cntl->signal,
					cntl->enable_dp_audio));
	params.lanenum = (uint8_t)(cntl->lanes_number);

	switch (cntl->color_depth) {
	case COLOR_DEPTH_888:
		params.bitpercolor = PANEL_8BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_101010:
		params.bitpercolor = PANEL_10BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_121212:
		params.bitpercolor = PANEL_12BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_161616:
		params.bitpercolor = PANEL_16BIT_PER_COLOR;
		break;
	default:
		break;
	}

	if (cntl->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		switch (cntl->color_depth) {
		case COLOR_DEPTH_101010:
			params.pclk_10khz =
				(params.pclk_10khz * 30) / 24;
			break;
		case COLOR_DEPTH_121212:
			params.pclk_10khz =
				(params.pclk_10khz * 36) / 24;
			break;
		case COLOR_DEPTH_161616:
			params.pclk_10khz =
				(params.pclk_10khz * 48) / 24;
			break;
		default:
			break;
		}

	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		encoder_control_dmcub(bp->base.ctx->dmub_srv, &params);
		return BP_RESULT_OK;
	}

	if (EXEC_BIOS_CMD_TABLE(digxencodercontrol, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result encoder_control_fallback(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		return encoder_control_digx_v1_5(bp, cntl);
	}

	return BP_RESULT_FAILURE;
}

/*****************************************************************************
 ******************************************************************************
 **
 **                  TRANSMITTER CONTROL
 **
 ******************************************************************************
 *****************************************************************************/


static enum bp_result transmitter_control_v1_6(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);

static enum bp_result transmitter_control_v1_7(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);

static enum bp_result transmitter_control_fallback(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);

static void init_transmitter_control(struct bios_parser *bp)
{
	uint8_t frev;
	uint8_t crev = 0;

	if (!BIOS_CMD_TABLE_REVISION(dig1transmittercontrol, frev, crev) && (bp->base.ctx->dc->ctx->dce_version <= DCN_VERSION_2_0))
		BREAK_TO_DEBUGGER();

	switch (crev) {
	case 6:
		bp->cmd_tbl.transmitter_control = transmitter_control_v1_6;
		break;
	case 7:
		bp->cmd_tbl.transmitter_control = transmitter_control_v1_7;
		break;
	default:
		bp->cmd_tbl.transmitter_control = transmitter_control_fallback;
		break;
	}
}

static void transmitter_control_dmcub(
		struct dc_dmub_srv *dmcub,
		struct dig_transmitter_control_parameters_v1_6 *dig)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.dig1_transmitter_control.header.type = DMUB_CMD__VBIOS;
	cmd.dig1_transmitter_control.header.sub_type =
		DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL;
	cmd.dig1_transmitter_control.header.payload_bytes =
		sizeof(cmd.dig1_transmitter_control) -
		sizeof(cmd.dig1_transmitter_control.header);
	cmd.dig1_transmitter_control.transmitter_control.dig = *dig;

	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static enum bp_result transmitter_control_v1_6(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	const struct command_table_helper *cmd = bp->cmd_helper;
	struct dig_transmitter_control_ps_allocation_v1_6 ps = { { 0 } };

	ps.param.phyid = cmd->phy_id_to_atom(cntl->transmitter);
	ps.param.action = (uint8_t)cntl->action;

	if (cntl->action == TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS)
		ps.param.mode_laneset.dplaneset = (uint8_t)cntl->lane_settings;
	else
		ps.param.mode_laneset.digmode =
				cmd->signal_type_to_atom_dig_mode(cntl->signal);

	ps.param.lanenum = (uint8_t)cntl->lanes_number;
	ps.param.hpdsel = cmd->hpd_sel_to_atom(cntl->hpd_sel);
	ps.param.digfe_sel = cmd->dig_encoder_sel_to_atom(cntl->engine_id);
	ps.param.connobj_id = (uint8_t)cntl->connector_obj_id.id;
	ps.param.symclk_10khz = cntl->pixel_clock/10;


	if (cntl->action == TRANSMITTER_CONTROL_ENABLE ||
		cntl->action == TRANSMITTER_CONTROL_ACTIAVATE ||
		cntl->action == TRANSMITTER_CONTROL_DEACTIVATE) {
		DC_LOG_BIOS("%s:ps.param.symclk_10khz = %d\n",\
		__func__, ps.param.symclk_10khz);
	}

	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		transmitter_control_dmcub(bp->base.ctx->dmub_srv, &ps.param);
		return BP_RESULT_OK;
	}

/*color_depth not used any more, driver has deep color factor in the Phyclk*/
	if (EXEC_BIOS_CMD_TABLE(dig1transmittercontrol, ps))
		result = BP_RESULT_OK;
	return result;
}

static void transmitter_control_dmcub_v1_7(
		struct dc_dmub_srv *dmcub,
		struct dmub_dig_transmitter_control_data_v1_7 *dig)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.dig1_transmitter_control.header.type = DMUB_CMD__VBIOS;
	cmd.dig1_transmitter_control.header.sub_type =
		DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL;
	cmd.dig1_transmitter_control.header.payload_bytes =
		sizeof(cmd.dig1_transmitter_control) -
		sizeof(cmd.dig1_transmitter_control.header);
	cmd.dig1_transmitter_control.transmitter_control.dig_v1_7 = *dig;

	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static struct dc_link *get_link_by_phy_id(struct dc *p_dc, uint32_t phy_id)
{
	struct dc_link *link = NULL;

	// Get Transition Bitmask from dc_link structure associated with PHY
	for (uint8_t link_id = 0; link_id < MAX_LINKS; link_id++) {
		if (phy_id == p_dc->links[link_id]->link_enc->transmitter) {
			link = p_dc->links[link_id];
			break;
		}
	}

	return link;
}

static enum bp_result transmitter_control_v1_7(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	const struct command_table_helper *cmd = bp->cmd_helper;
	struct dmub_dig_transmitter_control_data_v1_7 dig_v1_7 = {0};

	uint8_t hpo_instance = (uint8_t)cntl->hpo_engine_id - ENGINE_ID_HPO_0;

	if (dc_is_dp_signal(cntl->signal))
		hpo_instance = (uint8_t)cntl->hpo_engine_id - ENGINE_ID_HPO_DP_0;

	dig_v1_7.phyid = cmd->phy_id_to_atom(cntl->transmitter);
	dig_v1_7.action = (uint8_t)cntl->action;

	if (cntl->action == TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS)
		dig_v1_7.mode_laneset.dplaneset = (uint8_t)cntl->lane_settings;
	else
		dig_v1_7.mode_laneset.digmode =
				cmd->signal_type_to_atom_dig_mode(cntl->signal);

	dig_v1_7.lanenum = (uint8_t)cntl->lanes_number;
	dig_v1_7.hpdsel = cmd->hpd_sel_to_atom(cntl->hpd_sel);
	dig_v1_7.digfe_sel = cmd->dig_encoder_sel_to_atom(cntl->engine_id);
	dig_v1_7.connobj_id = (uint8_t)cntl->connector_obj_id.id;
	dig_v1_7.HPO_instance = hpo_instance;
	dig_v1_7.symclk_units.symclk_10khz = cntl->pixel_clock/10;

	if (cntl->action == TRANSMITTER_CONTROL_ENABLE ||
		cntl->action == TRANSMITTER_CONTROL_ACTIAVATE ||
		cntl->action == TRANSMITTER_CONTROL_DEACTIVATE) {
			DC_LOG_BIOS("%s:dig_v1_7.symclk_units.symclk_10khz = %d\n",
			__func__, dig_v1_7.symclk_units.symclk_10khz);
	}

	if (bp->base.ctx->dc->ctx->dmub_srv &&
		bp->base.ctx->dc->debug.dmub_command_table) {
		struct dm_process_phy_transition_init_params process_phy_transition_init_params = {0};
		struct dc_link *link = get_link_by_phy_id(bp->base.ctx->dc, dig_v1_7.phyid);
		bool is_phy_transition_interlock_allowed = false;
		uint8_t action = dig_v1_7.action;

		if (link) {
			if (link->phy_transition_bitmask &&
				(action == TRANSMITTER_CONTROL_ENABLE || action == TRANSMITTER_CONTROL_DISABLE)) {
				is_phy_transition_interlock_allowed = true;

				// Prepare input parameters for processing ACPI retimers
				process_phy_transition_init_params.action                   = action;
				process_phy_transition_init_params.display_port_lanes_count = cntl->lanes_number;
				process_phy_transition_init_params.phy_id                   = dig_v1_7.phyid;
				process_phy_transition_init_params.signal                   = cntl->signal;
				process_phy_transition_init_params.sym_clock_10khz          = dig_v1_7.symclk_units.symclk_10khz;
				process_phy_transition_init_params.display_port_link_rate   = link->cur_link_settings.link_rate;
				process_phy_transition_init_params.transition_bitmask       = link->phy_transition_bitmask;
			}
			dig_v1_7.skip_phy_ssc_reduction = link->wa_flags.skip_phy_ssc_reduction;
		}

		// Handle PRE_OFF_TO_ON: Process ACPI PHY Transition Interlock
		if (is_phy_transition_interlock_allowed && action == TRANSMITTER_CONTROL_ENABLE)
			dm_acpi_process_phy_transition_interlock(bp->base.ctx, process_phy_transition_init_params);

		transmitter_control_dmcub_v1_7(bp->base.ctx->dmub_srv, &dig_v1_7);

		// Handle POST_ON_TO_OFF: Process ACPI PHY Transition Interlock
		if (is_phy_transition_interlock_allowed && action == TRANSMITTER_CONTROL_DISABLE)
			dm_acpi_process_phy_transition_interlock(bp->base.ctx, process_phy_transition_init_params);

		return BP_RESULT_OK;
	}

/*color_depth not used any more, driver has deep color factor in the Phyclk*/
	if (EXEC_BIOS_CMD_TABLE(dig1transmittercontrol, dig_v1_7))
		result = BP_RESULT_OK;
	return result;
}

static enum bp_result transmitter_control_fallback(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		return transmitter_control_v1_7(bp, cntl);
	}

	return BP_RESULT_FAILURE;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  SET PIXEL CLOCK
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result set_pixel_clock_v7(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);

static enum bp_result set_pixel_clock_fallback(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);

static void init_set_pixel_clock(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(setpixelclock)) {
	case 7:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_v7;
		break;
	default:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_fallback;
		break;
	}
}

static void set_pixel_clock_dmcub(
		struct dc_dmub_srv *dmcub,
		struct set_pixel_clock_parameter_v1_7 *clk)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.set_pixel_clock.header.type = DMUB_CMD__VBIOS;
	cmd.set_pixel_clock.header.sub_type = DMUB_CMD__VBIOS_SET_PIXEL_CLOCK;
	cmd.set_pixel_clock.header.payload_bytes =
		sizeof(cmd.set_pixel_clock) -
		sizeof(cmd.set_pixel_clock.header);
	cmd.set_pixel_clock.pixel_clock.clk = *clk;

	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static enum bp_result set_pixel_clock_v7(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	struct set_pixel_clock_parameter_v1_7 clk;
	uint8_t controller_id;
	uint32_t pll_id;

	memset(&clk, 0, sizeof(clk));

	if (bp->cmd_helper->clock_source_id_to_atom(bp_params->pll_id, &pll_id)
			&& bp->cmd_helper->controller_id_to_atom(bp_params->
					controller_id, &controller_id)) {
		/* Note: VBIOS still wants to use ucCRTC name which is now
		 * 1 byte in ULONG
		 *typedef struct _CRTC_PIXEL_CLOCK_FREQ
		 *{
		 * target the pixel clock to drive the CRTC timing.
		 * ULONG ulPixelClock:24;
		 * 0 means disable PPLL/DCPLL. Expanded to 24 bits comparing to
		 * previous version.
		 * ATOM_CRTC1~6, indicate the CRTC controller to
		 * ULONG ucCRTC:8;
		 * drive the pixel clock. not used for DCPLL case.
		 *}CRTC_PIXEL_CLOCK_FREQ;
		 *union
		 *{
		 * pixel clock and CRTC id frequency
		 * CRTC_PIXEL_CLOCK_FREQ ulCrtcPclkFreq;
		 * ULONG ulDispEngClkFreq; dispclk frequency
		 *};
		 */
		clk.crtc_id = controller_id;
		clk.pll_id = (uint8_t) pll_id;
		clk.encoderobjid =
			bp->cmd_helper->encoder_id_to_atom(
				dal_graphics_object_id_get_encoder_id(
					bp_params->encoder_object_id));

		clk.encoder_mode = (uint8_t) bp->
			cmd_helper->encoder_mode_bp_to_atom(
				bp_params->signal_type, false);

		clk.pixclk_100hz = cpu_to_le32(bp_params->target_pixel_clock_100hz);

		clk.deep_color_ratio =
			(uint8_t) bp->cmd_helper->
				transmitter_color_depth_to_atom(
					bp_params->color_depth);

		DC_LOG_BIOS("%s:program display clock = %d, tg = %d, pll = %d, "\
				"colorDepth = %d\n", __func__,
				bp_params->target_pixel_clock_100hz, (int)controller_id,
				pll_id, bp_params->color_depth);

		if (bp_params->flags.FORCE_PROGRAMMING_OF_PLL)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_FORCE_PROG_PPLL;

		if (bp_params->flags.PROGRAM_PHY_PLL_ONLY)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_PROG_PHYPLL;

		if (bp_params->flags.SUPPORT_YUV_420)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_YUV420_MODE;

		if (bp_params->flags.SET_XTALIN_REF_SRC)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_XTALIN;

		if (bp_params->flags.SET_GENLOCK_REF_DIV_SRC)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_GENLK;

		if (bp_params->signal_type == SIGNAL_TYPE_DVI_DUAL_LINK)
			clk.miscinfo |= PIXEL_CLOCK_V7_MISC_DVI_DUALLINK_EN;

		if (bp->base.ctx->dc->ctx->dmub_srv &&
		    bp->base.ctx->dc->debug.dmub_command_table) {
			set_pixel_clock_dmcub(bp->base.ctx->dmub_srv, &clk);
			return BP_RESULT_OK;
		}

		if (EXEC_BIOS_CMD_TABLE(setpixelclock, clk))
			result = BP_RESULT_OK;
	}
	return result;
}

static enum bp_result set_pixel_clock_fallback(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		return set_pixel_clock_v7(bp, bp_params);
	}

	return BP_RESULT_FAILURE;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  SET CRTC TIMING
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result set_crtc_using_dtd_timing_v3(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params);

static void init_set_crtc_timing(struct bios_parser *bp)
{
	uint32_t dtd_version =
			BIOS_CMD_TABLE_PARA_REVISION(setcrtc_usingdtdtiming);

	switch (dtd_version) {
	case 3:
		bp->cmd_tbl.set_crtc_timing =
			set_crtc_using_dtd_timing_v3;
		break;
	default:
		bp->cmd_tbl.set_crtc_timing = NULL;
		break;
	}
}

static enum bp_result set_crtc_using_dtd_timing_v3(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	struct set_crtc_using_dtd_timing_parameters params = {0};
	uint8_t atom_controller_id;

	if (bp->cmd_helper->controller_id_to_atom(
			bp_params->controller_id, &atom_controller_id))
		params.crtc_id = atom_controller_id;

	/* bios usH_Size wants h addressable size */
	params.h_size = cpu_to_le16((uint16_t)bp_params->h_addressable);
	/* bios usH_Blanking_Time wants borders included in blanking */
	params.h_blanking_time =
			cpu_to_le16((uint16_t)(bp_params->h_total -
					bp_params->h_addressable));
	/* bios usV_Size wants v addressable size */
	params.v_size = cpu_to_le16((uint16_t)bp_params->v_addressable);
	/* bios usV_Blanking_Time wants borders included in blanking */
	params.v_blanking_time =
			cpu_to_le16((uint16_t)(bp_params->v_total -
					bp_params->v_addressable));
	/* bios usHSyncOffset is the offset from the end of h addressable,
	 * our horizontalSyncStart is the offset from the beginning
	 * of h addressable
	 */
	params.h_syncoffset =
			cpu_to_le16((uint16_t)(bp_params->h_sync_start -
					bp_params->h_addressable));
	params.h_syncwidth = cpu_to_le16((uint16_t)bp_params->h_sync_width);
	/* bios usHSyncOffset is the offset from the end of v addressable,
	 * our verticalSyncStart is the offset from the beginning of
	 * v addressable
	 */
	params.v_syncoffset =
			cpu_to_le16((uint16_t)(bp_params->v_sync_start -
					bp_params->v_addressable));
	params.v_syncwidth = cpu_to_le16((uint16_t)bp_params->v_sync_width);

	/* we assume that overscan from original timing does not get bigger
	 * than 255
	 * we will program all the borders in the Set CRTC Overscan call below
	 */

	if (bp_params->flags.HSYNC_POSITIVE_POLARITY == 0)
		params.modemiscinfo =
				cpu_to_le16(le16_to_cpu(params.modemiscinfo) |
						ATOM_HSYNC_POLARITY);

	if (bp_params->flags.VSYNC_POSITIVE_POLARITY == 0)
		params.modemiscinfo =
				cpu_to_le16(le16_to_cpu(params.modemiscinfo) |
						ATOM_VSYNC_POLARITY);

	if (bp_params->flags.INTERLACE)	{
		params.modemiscinfo =
				cpu_to_le16(le16_to_cpu(params.modemiscinfo) |
						ATOM_INTERLACE);

		/* original DAL code has this condition to apply this
		 * for non-TV/CV only
		 * due to complex MV testing for possible impact
		 * if ( pACParameters->signal != SignalType_YPbPr &&
		 *  pACParameters->signal != SignalType_Composite &&
		 *  pACParameters->signal != SignalType_SVideo)
		 */
		{
			/* HW will deduct 0.5 line from 2nd feild.
			 * i.e. for 1080i, it is 2 lines for 1st field,
			 * 2.5 lines for the 2nd feild. we need input as 5
			 * instead of 4.
			 * but it is 4 either from Edid data (spec CEA 861)
			 * or CEA timing table.
			 */
			le16_add_cpu(&params.v_syncoffset, 1);
		}
	}

	if (bp_params->flags.HORZ_COUNT_BY_TWO)
		params.modemiscinfo =
			cpu_to_le16(le16_to_cpu(params.modemiscinfo) |
					0x100); /* ATOM_DOUBLE_CLOCK_MODE */

	if (EXEC_BIOS_CMD_TABLE(setcrtc_usingdtdtiming, params))
		result = BP_RESULT_OK;

	return result;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  ENABLE CRTC
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result enable_crtc_v1(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable);

static void init_enable_crtc(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(enablecrtc)) {
	case 1:
		bp->cmd_tbl.enable_crtc = enable_crtc_v1;
		break;
	default:
		bp->cmd_tbl.enable_crtc = NULL;
		break;
	}
}

static enum bp_result enable_crtc_v1(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable)
{
	bool result = BP_RESULT_FAILURE;
	struct enable_crtc_parameters params = {0};
	uint8_t id;

	if (bp->cmd_helper->controller_id_to_atom(controller_id, &id))
		params.crtc_id = id;
	else
		return BP_RESULT_BADINPUT;

	if (enable)
		params.enable = ATOM_ENABLE;
	else
		params.enable = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(enablecrtc, params))
		result = BP_RESULT_OK;

	return result;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  DISPLAY PLL
 **
 ******************************************************************************
 *****************************************************************************/



/******************************************************************************
 ******************************************************************************
 **
 **                  EXTERNAL ENCODER CONTROL
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result external_encoder_control_v3(
	struct bios_parser *bp,
	struct bp_external_encoder_control *cntl);

static void init_external_encoder_control(
	struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(externalencodercontrol)) {
	case 3:
		bp->cmd_tbl.external_encoder_control =
				external_encoder_control_v3;
		break;
	default:
		bp->cmd_tbl.external_encoder_control = NULL;
		break;
	}
}

static enum bp_result external_encoder_control_v3(
	struct bios_parser *bp,
	struct bp_external_encoder_control *cntl)
{
	/* TODO */
	return BP_RESULT_OK;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  ENABLE DISPLAY POWER GATING
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result enable_disp_power_gating_v2_1(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action);

static enum bp_result enable_disp_power_gating_fallback(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action);

static void init_enable_disp_power_gating(
	struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(enabledisppowergating)) {
	case 1:
		bp->cmd_tbl.enable_disp_power_gating =
				enable_disp_power_gating_v2_1;
		break;
	default:
		dm_output_to_console("Don't enable_disp_power_gating enable_crtc for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(enabledisppowergating));
		bp->cmd_tbl.enable_disp_power_gating = enable_disp_power_gating_fallback;
		break;
	}
}

static void enable_disp_power_gating_dmcub(
	struct dc_dmub_srv *dmcub,
	struct enable_disp_power_gating_parameters_v2_1 *pwr)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.enable_disp_power_gating.header.type = DMUB_CMD__VBIOS;
	cmd.enable_disp_power_gating.header.sub_type =
		DMUB_CMD__VBIOS_ENABLE_DISP_POWER_GATING;
	cmd.enable_disp_power_gating.header.payload_bytes =
		sizeof(cmd.enable_disp_power_gating) -
		sizeof(cmd.enable_disp_power_gating.header);
	cmd.enable_disp_power_gating.power_gating.pwr = *pwr;

	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static enum bp_result enable_disp_power_gating_v2_1(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action)
{
	enum bp_result result = BP_RESULT_FAILURE;


	struct enable_disp_power_gating_ps_allocation ps = { { 0 } };
	uint8_t atom_crtc_id;

	if (bp->cmd_helper->controller_id_to_atom(crtc_id, &atom_crtc_id))
		ps.param.disp_pipe_id = atom_crtc_id;
	else
		return BP_RESULT_BADINPUT;

	ps.param.enable =
		bp->cmd_helper->disp_power_gating_action_to_atom(action);

	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		enable_disp_power_gating_dmcub(bp->base.ctx->dmub_srv,
					       &ps.param);
		return BP_RESULT_OK;
	}

	if (EXEC_BIOS_CMD_TABLE(enabledisppowergating, ps.param))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result enable_disp_power_gating_fallback(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action)
{
	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		return enable_disp_power_gating_v2_1(bp, crtc_id, action);
	}

	return BP_RESULT_FAILURE;
}

/******************************************************************************
*******************************************************************************
 **
 **                  SET DCE CLOCK
 **
*******************************************************************************
*******************************************************************************/

static enum bp_result set_dce_clock_v2_1(
	struct bios_parser *bp,
	struct bp_set_dce_clock_parameters *bp_params);

static void init_set_dce_clock(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(setdceclock)) {
	case 1:
		bp->cmd_tbl.set_dce_clock = set_dce_clock_v2_1;
		break;
	default:
		bp->cmd_tbl.set_dce_clock = NULL;
		break;
	}
}

static enum bp_result set_dce_clock_v2_1(
	struct bios_parser *bp,
	struct bp_set_dce_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;

	struct set_dce_clock_ps_allocation_v2_1 params;
	uint32_t atom_pll_id;
	uint32_t atom_clock_type;
	const struct command_table_helper *cmd = bp->cmd_helper;

	memset(&params, 0, sizeof(params));

	if (!cmd->clock_source_id_to_atom(bp_params->pll_id, &atom_pll_id) ||
			!cmd->dc_clock_type_to_atom(bp_params->clock_type,
					&atom_clock_type))
		return BP_RESULT_BADINPUT;

	params.param.dceclksrc  = atom_pll_id;
	params.param.dceclktype = atom_clock_type;

	if (bp_params->clock_type == DCECLOCK_TYPE_DPREFCLK) {
		if (bp_params->flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK)
			params.param.dceclkflag |=
					DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENLK;

		if (bp_params->flags.USE_PCIE_AS_SOURCE_FOR_DPREFCLK)
			params.param.dceclkflag |=
					DCE_CLOCK_FLAG_PLL_REFCLK_SRC_PCIE;

		if (bp_params->flags.USE_XTALIN_AS_SOURCE_FOR_DPREFCLK)
			params.param.dceclkflag |=
					DCE_CLOCK_FLAG_PLL_REFCLK_SRC_XTALIN;

		if (bp_params->flags.USE_GENERICA_AS_SOURCE_FOR_DPREFCLK)
			params.param.dceclkflag |=
					DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENERICA;
	} else
		/* only program clock frequency if display clock is used;
		 * VBIOS will program DPREFCLK
		 * We need to convert from KHz units into 10KHz units
		 */
		params.param.dceclk_10khz = cpu_to_le32(
				bp_params->target_clock_frequency / 10);
	DC_LOG_BIOS("%s:target_clock_frequency = %d"\
			"clock_type = %d \n", __func__,\
			bp_params->target_clock_frequency,\
			bp_params->clock_type);

	if (EXEC_BIOS_CMD_TABLE(setdceclock, params)) {
		/* Convert from 10KHz units back to KHz */
		bp_params->target_clock_frequency = le32_to_cpu(
				params.param.dceclk_10khz) * 10;
		result = BP_RESULT_OK;
	}

	return result;
}


/******************************************************************************
 ******************************************************************************
 **
 **                  GET SMU CLOCK INFO
 **
 ******************************************************************************
 *****************************************************************************/

static unsigned int get_smu_clock_info_v3_1(struct bios_parser *bp, uint8_t id);

static void init_get_smu_clock_info(struct bios_parser *bp)
{
	/* TODO add switch for table vrsion */
	bp->cmd_tbl.get_smu_clock_info = get_smu_clock_info_v3_1;

}

static unsigned int get_smu_clock_info_v3_1(struct bios_parser *bp, uint8_t id)
{
	struct atom_get_smu_clock_info_parameters_v3_1 smu_input = {0};
	struct atom_get_smu_clock_info_output_parameters_v3_1 smu_output;

	smu_input.command = GET_SMU_CLOCK_INFO_V3_1_GET_PLLVCO_FREQ;
	smu_input.syspll_id = id;

	/* Get Specific Clock */
	if (EXEC_BIOS_CMD_TABLE(getsmuclockinfo, smu_input)) {
		memmove(&smu_output, &smu_input, sizeof(
			struct atom_get_smu_clock_info_parameters_v3_1));
		return smu_output.atom_smu_outputclkfreq.syspllvcofreq_10khz;
	}

	return 0;
}

/******************************************************************************
 ******************************************************************************
 **
 **                  LVTMA CONTROL
 **
 ******************************************************************************
 *****************************************************************************/

static enum bp_result enable_lvtma_control(
	struct bios_parser *bp,
	uint8_t uc_pwr_on,
	uint8_t pwrseq_instance,
	uint8_t bypass_panel_control_wait);

static void init_enable_lvtma_control(struct bios_parser *bp)
{
	/* TODO add switch for table vrsion */
	bp->cmd_tbl.enable_lvtma_control = enable_lvtma_control;

}

static void enable_lvtma_control_dmcub(
	struct dc_dmub_srv *dmcub,
	uint8_t uc_pwr_on,
	uint8_t pwrseq_instance,
	uint8_t bypass_panel_control_wait)
{

	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.lvtma_control.header.type = DMUB_CMD__VBIOS;
	cmd.lvtma_control.header.sub_type =
			DMUB_CMD__VBIOS_LVTMA_CONTROL;
	cmd.lvtma_control.data.uc_pwr_action =
			uc_pwr_on;
	cmd.lvtma_control.data.pwrseq_inst =
			pwrseq_instance;
	cmd.lvtma_control.data.bypass_panel_control_wait =
			bypass_panel_control_wait;
	dc_wake_and_execute_dmub_cmd(dmcub->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

static enum bp_result enable_lvtma_control(
	struct bios_parser *bp,
	uint8_t uc_pwr_on,
	uint8_t pwrseq_instance,
	uint8_t bypass_panel_control_wait)
{
	enum bp_result result = BP_RESULT_FAILURE;

	if (bp->base.ctx->dc->ctx->dmub_srv &&
	    bp->base.ctx->dc->debug.dmub_command_table) {
		enable_lvtma_control_dmcub(bp->base.ctx->dmub_srv,
				uc_pwr_on,
				pwrseq_instance,
				bypass_panel_control_wait);
		return BP_RESULT_OK;
	}
	return result;
}

void dal_firmware_parser_init_cmd_tbl(struct bios_parser *bp)
{
	init_dig_encoder_control(bp);
	init_transmitter_control(bp);
	init_set_pixel_clock(bp);

	init_set_crtc_timing(bp);

	init_enable_crtc(bp);

	init_external_encoder_control(bp);
	init_enable_disp_power_gating(bp);
	init_set_dce_clock(bp);
	init_get_smu_clock_info(bp);

	init_enable_lvtma_control(bp);
}

