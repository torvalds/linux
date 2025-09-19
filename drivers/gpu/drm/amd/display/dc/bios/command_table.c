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
#include "amdgpu.h"
#include "atom.h"

#include "include/bios_parser_interface.h"

#include "command_table.h"
#include "command_table_helper.h"
#include "bios_parser_helper.h"
#include "bios_parser_types_internal.h"

#define EXEC_BIOS_CMD_TABLE(command, params)\
	(amdgpu_atom_execute_table(((struct amdgpu_device *)bp->base.ctx->driver_context)->mode_info.atom_context, \
		GetIndexIntoMasterTable(COMMAND, command), \
		(uint32_t *)&params, sizeof(params)) == 0)

#define BIOS_CMD_TABLE_REVISION(command, frev, crev)\
	amdgpu_atom_parse_cmd_header(((struct amdgpu_device *)bp->base.ctx->driver_context)->mode_info.atom_context, \
		GetIndexIntoMasterTable(COMMAND, command), &frev, &crev)

#define BIOS_CMD_TABLE_PARA_REVISION(command)\
	bios_cmd_table_para_revision(bp->base.ctx->driver_context, \
		GetIndexIntoMasterTable(COMMAND, command))

static void init_dig_encoder_control(struct bios_parser *bp);
static void init_transmitter_control(struct bios_parser *bp);
static void init_set_pixel_clock(struct bios_parser *bp);
static void init_enable_spread_spectrum_on_ppll(struct bios_parser *bp);
static void init_adjust_display_pll(struct bios_parser *bp);
static void init_dac_encoder_control(struct bios_parser *bp);
static void init_dac_output_control(struct bios_parser *bp);
static void init_set_crtc_timing(struct bios_parser *bp);
static void init_enable_crtc(struct bios_parser *bp);
static void init_enable_crtc_mem_req(struct bios_parser *bp);
static void init_external_encoder_control(struct bios_parser *bp);
static void init_enable_disp_power_gating(struct bios_parser *bp);
static void init_program_clock(struct bios_parser *bp);
static void init_set_dce_clock(struct bios_parser *bp);

void dal_bios_parser_init_cmd_tbl(struct bios_parser *bp)
{
	init_dig_encoder_control(bp);
	init_transmitter_control(bp);
	init_set_pixel_clock(bp);
	init_enable_spread_spectrum_on_ppll(bp);
	init_adjust_display_pll(bp);
	init_dac_encoder_control(bp);
	init_dac_output_control(bp);
	init_set_crtc_timing(bp);
	init_enable_crtc(bp);
	init_enable_crtc_mem_req(bp);
	init_program_clock(bp);
	init_external_encoder_control(bp);
	init_enable_disp_power_gating(bp);
	init_set_dce_clock(bp);
}

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

/*******************************************************************************
 ********************************************************************************
 **
 **                  D I G E N C O D E R C O N T R O L
 **
 ********************************************************************************
 *******************************************************************************/
static enum bp_result encoder_control_digx_v3(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static enum bp_result encoder_control_digx_v4(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static enum bp_result encoder_control_digx_v5(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static void init_encoder_control_dig_v1(struct bios_parser *bp);

static void init_dig_encoder_control(struct bios_parser *bp)
{
	uint32_t version =
		BIOS_CMD_TABLE_PARA_REVISION(DIGxEncoderControl);

	switch (version) {
	case 2:
		bp->cmd_tbl.dig_encoder_control = encoder_control_digx_v3;
		break;
	case 4:
		bp->cmd_tbl.dig_encoder_control = encoder_control_digx_v4;
		break;

	case 5:
		bp->cmd_tbl.dig_encoder_control = encoder_control_digx_v5;
		break;

	default:
		init_encoder_control_dig_v1(bp);
		break;
	}
}

static enum bp_result encoder_control_dig_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);
static enum bp_result encoder_control_dig1_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);
static enum bp_result encoder_control_dig2_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl);

static void init_encoder_control_dig_v1(struct bios_parser *bp)
{
	struct cmd_tbl *cmd_tbl = &bp->cmd_tbl;

	if (1 == BIOS_CMD_TABLE_PARA_REVISION(DIG1EncoderControl))
		cmd_tbl->encoder_control_dig1 = encoder_control_dig1_v1;
	else
		cmd_tbl->encoder_control_dig1 = NULL;

	if (1 == BIOS_CMD_TABLE_PARA_REVISION(DIG2EncoderControl))
		cmd_tbl->encoder_control_dig2 = encoder_control_dig2_v1;
	else
		cmd_tbl->encoder_control_dig2 = NULL;

	cmd_tbl->dig_encoder_control = encoder_control_dig_v1;
}

static enum bp_result encoder_control_dig_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	struct cmd_tbl *cmd_tbl = &bp->cmd_tbl;

	if (cntl != NULL)
		switch (cntl->engine_id) {
		case ENGINE_ID_DIGA:
			if (cmd_tbl->encoder_control_dig1 != NULL)
				result =
					cmd_tbl->encoder_control_dig1(bp, cntl);
			break;
		case ENGINE_ID_DIGB:
			if (cmd_tbl->encoder_control_dig2 != NULL)
				result =
					cmd_tbl->encoder_control_dig2(bp, cntl);
			break;

		default:
			break;
		}

	return result;
}

static enum bp_result encoder_control_dig1_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_ENCODER_CONTROL_PARAMETERS_V2 params = {0};

	bp->cmd_helper->assign_control_parameter(bp->cmd_helper, cntl, &params);

	if (EXEC_BIOS_CMD_TABLE(DIG1EncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result encoder_control_dig2_v1(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_ENCODER_CONTROL_PARAMETERS_V2 params = {0};

	bp->cmd_helper->assign_control_parameter(bp->cmd_helper, cntl, &params);

	if (EXEC_BIOS_CMD_TABLE(DIG2EncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result encoder_control_digx_v3(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_ENCODER_CONTROL_PARAMETERS_V3 params = {0};

	if (LANE_COUNT_FOUR < cntl->lanes_number)
		params.acConfig.ucDPLinkRate = 1; /* dual link 2.7GHz */
	else
		params.acConfig.ucDPLinkRate = 0; /* single link 1.62GHz */

	params.acConfig.ucDigSel = (uint8_t)(cntl->engine_id);

	/* We need to convert from KHz units into 10KHz units */
	params.ucAction = bp->cmd_helper->encoder_action_to_atom(cntl->action);
	params.usPixelClock = cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
	params.ucEncoderMode =
			(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
					cntl->signal,
					cntl->enable_dp_audio);
	params.ucLaneNum = (uint8_t)(cntl->lanes_number);

	switch (cntl->color_depth) {
	case COLOR_DEPTH_888:
		params.ucBitPerColor = PANEL_8BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_101010:
		params.ucBitPerColor = PANEL_10BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_121212:
		params.ucBitPerColor = PANEL_12BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_161616:
		params.ucBitPerColor = PANEL_16BIT_PER_COLOR;
		break;
	default:
		break;
	}

	if (EXEC_BIOS_CMD_TABLE(DIGxEncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result encoder_control_digx_v4(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_ENCODER_CONTROL_PARAMETERS_V4 params = {0};

	if (LANE_COUNT_FOUR < cntl->lanes_number)
		params.acConfig.ucDPLinkRate = 1; /* dual link 2.7GHz */
	else
		params.acConfig.ucDPLinkRate = 0; /* single link 1.62GHz */

	params.acConfig.ucDigSel = (uint8_t)(cntl->engine_id);

	/* We need to convert from KHz units into 10KHz units */
	params.ucAction = bp->cmd_helper->encoder_action_to_atom(cntl->action);
	params.usPixelClock = cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
	params.ucEncoderMode =
			(uint8_t)(bp->cmd_helper->encoder_mode_bp_to_atom(
					cntl->signal,
					cntl->enable_dp_audio));
	params.ucLaneNum = (uint8_t)(cntl->lanes_number);

	switch (cntl->color_depth) {
	case COLOR_DEPTH_888:
		params.ucBitPerColor = PANEL_8BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_101010:
		params.ucBitPerColor = PANEL_10BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_121212:
		params.ucBitPerColor = PANEL_12BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_161616:
		params.ucBitPerColor = PANEL_16BIT_PER_COLOR;
		break;
	default:
		break;
	}

	if (EXEC_BIOS_CMD_TABLE(DIGxEncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result encoder_control_digx_v5(
	struct bios_parser *bp,
	struct bp_encoder_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ENCODER_STREAM_SETUP_PARAMETERS_V5 params = {0};

	params.ucDigId = (uint8_t)(cntl->engine_id);
	params.ucAction = bp->cmd_helper->encoder_action_to_atom(cntl->action);

	params.ulPixelClock = cntl->pixel_clock / 10;
	params.ucDigMode =
			(uint8_t)(bp->cmd_helper->encoder_mode_bp_to_atom(
					cntl->signal,
					cntl->enable_dp_audio));
	params.ucLaneNum = (uint8_t)(cntl->lanes_number);

	switch (cntl->color_depth) {
	case COLOR_DEPTH_888:
		params.ucBitPerColor = PANEL_8BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_101010:
		params.ucBitPerColor = PANEL_10BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_121212:
		params.ucBitPerColor = PANEL_12BIT_PER_COLOR;
		break;
	case COLOR_DEPTH_161616:
		params.ucBitPerColor = PANEL_16BIT_PER_COLOR;
		break;
	default:
		break;
	}

	if (cntl->signal == SIGNAL_TYPE_HDMI_TYPE_A)
		switch (cntl->color_depth) {
		case COLOR_DEPTH_101010:
			params.ulPixelClock =
				(params.ulPixelClock * 30) / 24;
			break;
		case COLOR_DEPTH_121212:
			params.ulPixelClock =
				(params.ulPixelClock * 36) / 24;
			break;
		case COLOR_DEPTH_161616:
			params.ulPixelClock =
				(params.ulPixelClock * 48) / 24;
			break;
		default:
			break;
		}

	if (EXEC_BIOS_CMD_TABLE(DIGxEncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  TRANSMITTER CONTROL
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result transmitter_control_v2(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);
static enum bp_result transmitter_control_v3(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);
static enum bp_result transmitter_control_v4(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);
static enum bp_result transmitter_control_v1_5(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);
static enum bp_result transmitter_control_v1_6(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl);

static void init_transmitter_control(struct bios_parser *bp)
{
	uint8_t frev;
	uint8_t crev = 0;

	if (BIOS_CMD_TABLE_REVISION(UNIPHYTransmitterControl,
			frev, crev) == false)
		BREAK_TO_DEBUGGER();
	switch (crev) {
	case 2:
		bp->cmd_tbl.transmitter_control = transmitter_control_v2;
		break;
	case 3:
		bp->cmd_tbl.transmitter_control = transmitter_control_v3;
		break;
	case 4:
		bp->cmd_tbl.transmitter_control = transmitter_control_v4;
		break;
	case 5:
		bp->cmd_tbl.transmitter_control = transmitter_control_v1_5;
		break;
	case 6:
		bp->cmd_tbl.transmitter_control = transmitter_control_v1_6;
		break;
	default:
		dm_output_to_console("Don't have transmitter_control for v%d\n", crev);
		bp->cmd_tbl.transmitter_control = NULL;
		break;
	}
}

static enum bp_result transmitter_control_v2(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 params;
	enum connector_id connector_id =
		dal_graphics_object_id_get_connector_id(cntl->connector_obj_id);

	memset(&params, 0, sizeof(params));

	switch (cntl->transmitter) {
	case TRANSMITTER_UNIPHY_A:
	case TRANSMITTER_UNIPHY_B:
	case TRANSMITTER_UNIPHY_C:
	case TRANSMITTER_UNIPHY_D:
	case TRANSMITTER_UNIPHY_E:
	case TRANSMITTER_UNIPHY_F:
	case TRANSMITTER_TRAVIS_LCD:
		break;
	default:
		return BP_RESULT_BADINPUT;
	}

	switch (cntl->action) {
	case TRANSMITTER_CONTROL_INIT:
		if ((CONNECTOR_ID_DUAL_LINK_DVII == connector_id) ||
				(CONNECTOR_ID_DUAL_LINK_DVID == connector_id))
			/* on INIT this bit should be set according to the
			 * physical connector
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

		/* connector object id */
		params.usInitInfo =
				cpu_to_le16((uint8_t)cntl->connector_obj_id.id);
		break;
	case TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS:
		/* voltage swing and pre-emphsis */
		params.asMode.ucLaneSel = (uint8_t)cntl->lane_select;
		params.asMode.ucLaneSet = (uint8_t)cntl->lane_settings;
		break;
	default:
		/* if dual-link */
		if (LANE_COUNT_FOUR < cntl->lanes_number) {
			/* on ENABLE/DISABLE this bit should be set according to
			 * actual timing (number of lanes)
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

			/* link rate, half for dual link
			 * We need to convert from KHz units into 20KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 20));
		} else
			/* link rate, half for dual link
			 * We need to convert from KHz units into 10KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
		break;
	}

	/* 00 - coherent mode
	 * 01 - incoherent mode
	 */

	params.acConfig.fCoherentMode = cntl->coherent;

	if ((TRANSMITTER_UNIPHY_B == cntl->transmitter)
			|| (TRANSMITTER_UNIPHY_D == cntl->transmitter)
			|| (TRANSMITTER_UNIPHY_F == cntl->transmitter))
		/* Bit2: Transmitter Link selection
		 * =0 when bit0=0, single link A/C/E, when bit0=1,
		 * master link A/C/E
		 * =1 when bit0=0, single link B/D/F, when bit0=1,
		 * master link B/D/F
		 */
		params.acConfig.ucLinkSel = 1;

	if (ENGINE_ID_DIGB == cntl->engine_id)
		/* Bit3: Transmitter data source selection
		 * =0 DIGA is data source.
		 * =1 DIGB is data source.
		 * This bit is only useful when ucAction= ATOM_ENABLE
		 */
		params.acConfig.ucEncoderSel = 1;

	if (CONNECTOR_ID_DISPLAY_PORT == connector_id ||
	    CONNECTOR_ID_USBC == connector_id)
		/* Bit4: DP connector flag
		 * =0 connector is none-DP connector
		 * =1 connector is DP connector
		 */
		params.acConfig.fDPConnector = 1;

	/* Bit[7:6]: Transmitter selection
	 * =0 UNIPHY_ENCODER: UNIPHYA/B
	 * =1 UNIPHY1_ENCODER: UNIPHYC/D
	 * =2 UNIPHY2_ENCODER: UNIPHYE/F
	 * =3 reserved
	 */
	params.acConfig.ucTransmitterSel =
			(uint8_t)bp->cmd_helper->transmitter_bp_to_atom(
					cntl->transmitter);

	params.ucAction = (uint8_t)cntl->action;

	if (EXEC_BIOS_CMD_TABLE(UNIPHYTransmitterControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result transmitter_control_v3(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V3 params;
	uint32_t pll_id;
	enum connector_id conn_id =
			dal_graphics_object_id_get_connector_id(cntl->connector_obj_id);
	const struct command_table_helper *cmd = bp->cmd_helper;
	bool dual_link_conn = (CONNECTOR_ID_DUAL_LINK_DVII == conn_id)
					|| (CONNECTOR_ID_DUAL_LINK_DVID == conn_id);

	memset(&params, 0, sizeof(params));

	switch (cntl->transmitter) {
	case TRANSMITTER_UNIPHY_A:
	case TRANSMITTER_UNIPHY_B:
	case TRANSMITTER_UNIPHY_C:
	case TRANSMITTER_UNIPHY_D:
	case TRANSMITTER_UNIPHY_E:
	case TRANSMITTER_UNIPHY_F:
	case TRANSMITTER_TRAVIS_LCD:
		break;
	default:
		return BP_RESULT_BADINPUT;
	}

	if (!cmd->clock_source_id_to_atom(cntl->pll_id, &pll_id))
		return BP_RESULT_BADINPUT;

	/* fill information based on the action */
	switch (cntl->action) {
	case TRANSMITTER_CONTROL_INIT:
		if (dual_link_conn) {
			/* on INIT this bit should be set according to the
			 * phisycal connector
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;
		}

		/* connector object id */
		params.usInitInfo =
				cpu_to_le16((uint8_t)(cntl->connector_obj_id.id));
		break;
	case TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS:
		/* votage swing and pre-emphsis */
		params.asMode.ucLaneSel = (uint8_t)cntl->lane_select;
		params.asMode.ucLaneSet = (uint8_t)cntl->lane_settings;
		break;
	default:
		if (dual_link_conn && cntl->multi_path)
			/* on ENABLE/DISABLE this bit should be set according to
			 * actual timing (number of lanes)
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

		/* if dual-link */
		if (LANE_COUNT_FOUR < cntl->lanes_number) {
			/* on ENABLE/DISABLE this bit should be set according to
			 * actual timing (number of lanes)
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

			/* link rate, half for dual link
			 * We need to convert from KHz units into 20KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 20));
		} else {
			/* link rate, half for dual link
			 * We need to convert from KHz units into 10KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
		}
		break;
	}

	/* 00 - coherent mode
	 * 01 - incoherent mode
	 */

	params.acConfig.fCoherentMode = cntl->coherent;

	if ((TRANSMITTER_UNIPHY_B == cntl->transmitter)
		|| (TRANSMITTER_UNIPHY_D == cntl->transmitter)
		|| (TRANSMITTER_UNIPHY_F == cntl->transmitter))
		/* Bit2: Transmitter Link selection
		 * =0 when bit0=0, single link A/C/E, when bit0=1,
		 * master link A/C/E
		 * =1 when bit0=0, single link B/D/F, when bit0=1,
		 * master link B/D/F
		 */
		params.acConfig.ucLinkSel = 1;

	if (ENGINE_ID_DIGB == cntl->engine_id)
		/* Bit3: Transmitter data source selection
		 * =0 DIGA is data source.
		 * =1 DIGB is data source.
		 * This bit is only useful when ucAction= ATOM_ENABLE
		 */
		params.acConfig.ucEncoderSel = 1;

	/* Bit[7:6]: Transmitter selection
	 * =0 UNIPHY_ENCODER: UNIPHYA/B
	 * =1 UNIPHY1_ENCODER: UNIPHYC/D
	 * =2 UNIPHY2_ENCODER: UNIPHYE/F
	 * =3 reserved
	 */
	params.acConfig.ucTransmitterSel =
			(uint8_t)cmd->transmitter_bp_to_atom(cntl->transmitter);

	params.ucLaneNum = (uint8_t)cntl->lanes_number;

	params.acConfig.ucRefClkSource = (uint8_t)pll_id;

	params.ucAction = (uint8_t)cntl->action;

	if (EXEC_BIOS_CMD_TABLE(UNIPHYTransmitterControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result transmitter_control_v4(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V4 params;
	uint32_t ref_clk_src_id;
	enum connector_id conn_id =
			dal_graphics_object_id_get_connector_id(cntl->connector_obj_id);
	const struct command_table_helper *cmd = bp->cmd_helper;

	memset(&params, 0, sizeof(params));

	switch (cntl->transmitter) {
	case TRANSMITTER_UNIPHY_A:
	case TRANSMITTER_UNIPHY_B:
	case TRANSMITTER_UNIPHY_C:
	case TRANSMITTER_UNIPHY_D:
	case TRANSMITTER_UNIPHY_E:
	case TRANSMITTER_UNIPHY_F:
	case TRANSMITTER_TRAVIS_LCD:
		break;
	default:
		return BP_RESULT_BADINPUT;
	}

	if (!cmd->clock_source_id_to_ref_clk_src(cntl->pll_id, &ref_clk_src_id))
		return BP_RESULT_BADINPUT;

	switch (cntl->action) {
	case TRANSMITTER_CONTROL_INIT:
	{
		if ((CONNECTOR_ID_DUAL_LINK_DVII == conn_id) ||
				(CONNECTOR_ID_DUAL_LINK_DVID == conn_id))
			/* on INIT this bit should be set according to the
			 * phisycal connector
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

		/* connector object id */
		params.usInitInfo =
				cpu_to_le16((uint8_t)(cntl->connector_obj_id.id));
	}
	break;
	case TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS:
		/* votage swing and pre-emphsis */
		params.asMode.ucLaneSel = (uint8_t)(cntl->lane_select);
		params.asMode.ucLaneSet = (uint8_t)(cntl->lane_settings);
		break;
	default:
		if ((CONNECTOR_ID_DUAL_LINK_DVII == conn_id) ||
				(CONNECTOR_ID_DUAL_LINK_DVID == conn_id))
			/* on ENABLE/DISABLE this bit should be set according to
			 * actual timing (number of lanes)
			 * Bit0: dual link connector flag
			 * =0 connector is single link connector
			 * =1 connector is dual link connector
			 */
			params.acConfig.fDualLinkConnector = 1;

		/* if dual-link */
		if (LANE_COUNT_FOUR < cntl->lanes_number)
			/* link rate, half for dual link
			 * We need to convert from KHz units into 20KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 20));
		else {
			/* link rate, half for dual link
			 * We need to convert from KHz units into 10KHz units
			 */
			params.usPixelClock =
					cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
		}
		break;
	}

	/* 00 - coherent mode
	 * 01 - incoherent mode
	 */

	params.acConfig.fCoherentMode = cntl->coherent;

	if ((TRANSMITTER_UNIPHY_B == cntl->transmitter)
		|| (TRANSMITTER_UNIPHY_D == cntl->transmitter)
		|| (TRANSMITTER_UNIPHY_F == cntl->transmitter))
		/* Bit2: Transmitter Link selection
		 * =0 when bit0=0, single link A/C/E, when bit0=1,
		 * master link A/C/E
		 * =1 when bit0=0, single link B/D/F, when bit0=1,
		 * master link B/D/F
		 */
		params.acConfig.ucLinkSel = 1;

	if (ENGINE_ID_DIGB == cntl->engine_id)
		/* Bit3: Transmitter data source selection
		 * =0 DIGA is data source.
		 * =1 DIGB is data source.
		 * This bit is only useful when ucAction= ATOM_ENABLE
		 */
		params.acConfig.ucEncoderSel = 1;

	/* Bit[7:6]: Transmitter selection
	 * =0 UNIPHY_ENCODER: UNIPHYA/B
	 * =1 UNIPHY1_ENCODER: UNIPHYC/D
	 * =2 UNIPHY2_ENCODER: UNIPHYE/F
	 * =3 reserved
	 */
	params.acConfig.ucTransmitterSel =
		(uint8_t)(cmd->transmitter_bp_to_atom(cntl->transmitter));
	params.ucLaneNum = (uint8_t)(cntl->lanes_number);
	params.acConfig.ucRefClkSource = (uint8_t)(ref_clk_src_id);
	params.ucAction = (uint8_t)(cntl->action);

	if (EXEC_BIOS_CMD_TABLE(UNIPHYTransmitterControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result transmitter_control_v1_5(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	const struct command_table_helper *cmd = bp->cmd_helper;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_5 params;

	memset(&params, 0, sizeof(params));
	params.ucPhyId = cmd->phy_id_to_atom(cntl->transmitter);
	params.ucAction = (uint8_t)cntl->action;
	params.ucLaneNum = (uint8_t)cntl->lanes_number;
	params.ucConnObjId = (uint8_t)cntl->connector_obj_id.id;

	params.ucDigMode =
		cmd->signal_type_to_atom_dig_mode(cntl->signal);
	params.asConfig.ucPhyClkSrcId =
		cmd->clock_source_id_to_atom_phy_clk_src_id(cntl->pll_id);
	/* 00 - coherent mode */
	params.asConfig.ucCoherentMode = cntl->coherent;
	params.asConfig.ucHPDSel =
		cmd->hpd_sel_to_atom(cntl->hpd_sel);
	params.ucDigEncoderSel =
		cmd->dig_encoder_sel_to_atom(cntl->engine_id);
	params.ucDPLaneSet = (uint8_t) cntl->lane_settings;
	params.usSymClock = cpu_to_le16((uint16_t) (cntl->pixel_clock / 10));
	/*
	 * In SI/TN case, caller have to set usPixelClock as following:
	 * DP mode: usPixelClock = DP_LINK_CLOCK/10
	 * (DP_LINK_CLOCK = 1.62GHz, 2.7GHz, 5.4GHz)
	 * DVI single link mode: usPixelClock = pixel clock
	 * DVI dual link mode: usPixelClock = pixel clock
	 * HDMI mode: usPixelClock = pixel clock * deep_color_ratio
	 * (=1: 8bpp, =1.25: 10bpp, =1.5:12bpp, =2: 16bpp)
	 * LVDS mode: usPixelClock = pixel clock
	 */
	if  (cntl->signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		switch (cntl->color_depth) {
		case COLOR_DEPTH_101010:
			params.usSymClock =
				cpu_to_le16((le16_to_cpu(params.usSymClock) * 30) / 24);
			break;
		case COLOR_DEPTH_121212:
			params.usSymClock =
				cpu_to_le16((le16_to_cpu(params.usSymClock) * 36) / 24);
			break;
		case COLOR_DEPTH_161616:
			params.usSymClock =
				cpu_to_le16((le16_to_cpu(params.usSymClock) * 48) / 24);
			break;
		default:
			break;
		}
	}

	if (EXEC_BIOS_CMD_TABLE(UNIPHYTransmitterControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result transmitter_control_v1_6(
	struct bios_parser *bp,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result = BP_RESULT_FAILURE;
	const struct command_table_helper *cmd = bp->cmd_helper;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V1_6 params;

	memset(&params, 0, sizeof(params));
	params.ucPhyId = cmd->phy_id_to_atom(cntl->transmitter);
	params.ucAction = (uint8_t)cntl->action;

	if (cntl->action == TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS)
		params.ucDPLaneSet = (uint8_t)cntl->lane_settings;
	else
		params.ucDigMode = cmd->signal_type_to_atom_dig_mode(cntl->signal);

	params.ucLaneNum = (uint8_t)cntl->lanes_number;
	params.ucHPDSel = cmd->hpd_sel_to_atom(cntl->hpd_sel);
	params.ucDigEncoderSel = cmd->dig_encoder_sel_to_atom(cntl->engine_id);
	params.ucConnObjId = (uint8_t)cntl->connector_obj_id.id;
	params.ulSymClock = cntl->pixel_clock/10;

	/*
	 * In SI/TN case, caller have to set usPixelClock as following:
	 * DP mode: usPixelClock = DP_LINK_CLOCK/10
	 * (DP_LINK_CLOCK = 1.62GHz, 2.7GHz, 5.4GHz)
	 * DVI single link mode: usPixelClock = pixel clock
	 * DVI dual link mode: usPixelClock = pixel clock
	 * HDMI mode: usPixelClock = pixel clock * deep_color_ratio
	 * (=1: 8bpp, =1.25: 10bpp, =1.5:12bpp, =2: 16bpp)
	 * LVDS mode: usPixelClock = pixel clock
	 */
	switch (cntl->signal) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		switch (cntl->color_depth) {
		case COLOR_DEPTH_101010:
			params.ulSymClock =
				cpu_to_le16((le16_to_cpu(params.ulSymClock) * 30) / 24);
			break;
		case COLOR_DEPTH_121212:
			params.ulSymClock =
				cpu_to_le16((le16_to_cpu(params.ulSymClock) * 36) / 24);
			break;
		case COLOR_DEPTH_161616:
			params.ulSymClock =
				cpu_to_le16((le16_to_cpu(params.ulSymClock) * 48) / 24);
			break;
		default:
			break;
		}
		break;
		default:
			break;
	}

	if (EXEC_BIOS_CMD_TABLE(UNIPHYTransmitterControl, params))
		result = BP_RESULT_OK;
	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  SET PIXEL CLOCK
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result set_pixel_clock_v3(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
static enum bp_result set_pixel_clock_v5(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
static enum bp_result set_pixel_clock_v6(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
static enum bp_result set_pixel_clock_v7(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);

static void init_set_pixel_clock(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(SetPixelClock)) {
	case 3:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_v3;
		break;
	case 5:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_v5;
		break;
	case 6:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_v6;
		break;
	case 7:
		bp->cmd_tbl.set_pixel_clock = set_pixel_clock_v7;
		break;
	default:
		dm_output_to_console("Don't have set_pixel_clock for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(SetPixelClock));
		bp->cmd_tbl.set_pixel_clock = NULL;
		break;
	}
}

static enum bp_result set_pixel_clock_v3(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	PIXEL_CLOCK_PARAMETERS_V3 *params;
	SET_PIXEL_CLOCK_PS_ALLOCATION allocation;

	memset(&allocation, 0, sizeof(allocation));

	if (CLOCK_SOURCE_ID_PLL1 == bp_params->pll_id)
		allocation.sPCLKInput.ucPpll = ATOM_PPLL1;
	else if (CLOCK_SOURCE_ID_PLL2 == bp_params->pll_id)
		allocation.sPCLKInput.ucPpll = ATOM_PPLL2;
	else
		return BP_RESULT_BADINPUT;

	allocation.sPCLKInput.usRefDiv =
			cpu_to_le16((uint16_t)bp_params->reference_divider);
	allocation.sPCLKInput.usFbDiv =
			cpu_to_le16((uint16_t)bp_params->feedback_divider);
	allocation.sPCLKInput.ucFracFbDiv =
			(uint8_t)(bp_params->fractional_feedback_divider / 100000);
	allocation.sPCLKInput.ucPostDiv =
			(uint8_t)bp_params->pixel_clock_post_divider;

	/* We need to convert from 100Hz units into 10KHz units */
	allocation.sPCLKInput.usPixelClock =
			cpu_to_le16((uint16_t)(bp_params->target_pixel_clock_100hz / 100));

	params = (PIXEL_CLOCK_PARAMETERS_V3 *)&allocation.sPCLKInput;
	params->ucTransmitterId =
			bp->cmd_helper->encoder_id_to_atom(
					dal_graphics_object_id_get_encoder_id(
							bp_params->encoder_object_id));
	params->ucEncoderMode =
			(uint8_t)(bp->cmd_helper->encoder_mode_bp_to_atom(
					bp_params->signal_type, false));

	if (bp_params->flags.FORCE_PROGRAMMING_OF_PLL)
		params->ucMiscInfo |= PIXEL_CLOCK_MISC_FORCE_PROG_PPLL;

	if (bp_params->flags.USE_E_CLOCK_AS_SOURCE_FOR_D_CLOCK)
		params->ucMiscInfo |= PIXEL_CLOCK_MISC_USE_ENGINE_FOR_DISPCLK;

	if (CONTROLLER_ID_D1 != bp_params->controller_id)
		params->ucMiscInfo |= PIXEL_CLOCK_MISC_CRTC_SEL_CRTC2;

	if (EXEC_BIOS_CMD_TABLE(SetPixelClock, allocation))
		result = BP_RESULT_OK;

	return result;
}

#ifndef SET_PIXEL_CLOCK_PS_ALLOCATION_V5
/* video bios did not define this: */
typedef struct _SET_PIXEL_CLOCK_PS_ALLOCATION_V5 {
	PIXEL_CLOCK_PARAMETERS_V5 sPCLKInput;
	/* Caller doesn't need to init this portion */
	ENABLE_SPREAD_SPECTRUM_ON_PPLL sReserved;
} SET_PIXEL_CLOCK_PS_ALLOCATION_V5;
#endif

#ifndef SET_PIXEL_CLOCK_PS_ALLOCATION_V6
/* video bios did not define this: */
typedef struct _SET_PIXEL_CLOCK_PS_ALLOCATION_V6 {
	PIXEL_CLOCK_PARAMETERS_V6 sPCLKInput;
	/* Caller doesn't need to init this portion */
	ENABLE_SPREAD_SPECTRUM_ON_PPLL sReserved;
} SET_PIXEL_CLOCK_PS_ALLOCATION_V6;
#endif

static enum bp_result set_pixel_clock_v5(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	SET_PIXEL_CLOCK_PS_ALLOCATION_V5 clk;
	uint8_t controller_id;
	uint32_t pll_id;

	memset(&clk, 0, sizeof(clk));

	if (bp->cmd_helper->clock_source_id_to_atom(bp_params->pll_id, &pll_id)
			&& bp->cmd_helper->controller_id_to_atom(
					bp_params->controller_id, &controller_id)) {
		clk.sPCLKInput.ucCRTC = controller_id;
		clk.sPCLKInput.ucPpll = (uint8_t)pll_id;
		clk.sPCLKInput.ucRefDiv =
				(uint8_t)(bp_params->reference_divider);
		clk.sPCLKInput.usFbDiv =
				cpu_to_le16((uint16_t)(bp_params->feedback_divider));
		clk.sPCLKInput.ulFbDivDecFrac =
				cpu_to_le32(bp_params->fractional_feedback_divider);
		clk.sPCLKInput.ucPostDiv =
				(uint8_t)(bp_params->pixel_clock_post_divider);
		clk.sPCLKInput.ucTransmitterID =
				bp->cmd_helper->encoder_id_to_atom(
						dal_graphics_object_id_get_encoder_id(
								bp_params->encoder_object_id));
		clk.sPCLKInput.ucEncoderMode =
				(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
						bp_params->signal_type, false);

		/* We need to convert from 100Hz units into 10KHz units */
		clk.sPCLKInput.usPixelClock =
				cpu_to_le16((uint16_t)(bp_params->target_pixel_clock_100hz / 100));

		if (bp_params->flags.FORCE_PROGRAMMING_OF_PLL)
			clk.sPCLKInput.ucMiscInfo |=
					PIXEL_CLOCK_MISC_FORCE_PROG_PPLL;

		if (bp_params->flags.SET_EXTERNAL_REF_DIV_SRC)
			clk.sPCLKInput.ucMiscInfo |=
					PIXEL_CLOCK_MISC_REF_DIV_SRC;

		/* clkV5.ucMiscInfo bit[3:2]= HDMI panel bit depth: =0: 24bpp
		 * =1:30bpp, =2:32bpp
		 * driver choose program it itself, i.e. here we program it
		 * to 888 by default.
		 */
		if (bp_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A)
			switch (bp_params->color_depth) {
			case TRANSMITTER_COLOR_DEPTH_30:
				/* yes this is correct, the atom define is wrong */
				clk.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_32BPP;
				break;
			case TRANSMITTER_COLOR_DEPTH_36:
				/* yes this is correct, the atom define is wrong */
				clk.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V5_MISC_HDMI_30BPP;
				break;
			default:
				break;
			}

		if (EXEC_BIOS_CMD_TABLE(SetPixelClock, clk))
			result = BP_RESULT_OK;
	}

	return result;
}

static enum bp_result set_pixel_clock_v6(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	SET_PIXEL_CLOCK_PS_ALLOCATION_V6 clk;
	uint8_t controller_id;
	uint32_t pll_id;

	memset(&clk, 0, sizeof(clk));

	if (bp->cmd_helper->clock_source_id_to_atom(bp_params->pll_id, &pll_id)
			&& bp->cmd_helper->controller_id_to_atom(
					bp_params->controller_id, &controller_id)) {
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
		clk.sPCLKInput.ulCrtcPclkFreq.ucCRTC = controller_id;
		clk.sPCLKInput.ucPpll = (uint8_t) pll_id;
		clk.sPCLKInput.ucRefDiv =
				(uint8_t) bp_params->reference_divider;
		clk.sPCLKInput.usFbDiv =
				cpu_to_le16((uint16_t) bp_params->feedback_divider);
		clk.sPCLKInput.ulFbDivDecFrac =
				cpu_to_le32(bp_params->fractional_feedback_divider);
		clk.sPCLKInput.ucPostDiv =
				(uint8_t) bp_params->pixel_clock_post_divider;
		clk.sPCLKInput.ucTransmitterID =
				bp->cmd_helper->encoder_id_to_atom(
						dal_graphics_object_id_get_encoder_id(
								bp_params->encoder_object_id));
		clk.sPCLKInput.ucEncoderMode =
				(uint8_t) bp->cmd_helper->encoder_mode_bp_to_atom(
						bp_params->signal_type, false);

		/* We need to convert from 100 Hz units into 10KHz units */
		clk.sPCLKInput.ulCrtcPclkFreq.ulPixelClock =
				cpu_to_le32(bp_params->target_pixel_clock_100hz / 100);

		if (bp_params->flags.FORCE_PROGRAMMING_OF_PLL) {
			clk.sPCLKInput.ucMiscInfo |=
					PIXEL_CLOCK_V6_MISC_FORCE_PROG_PPLL;
		}

		if (bp_params->flags.SET_EXTERNAL_REF_DIV_SRC) {
			clk.sPCLKInput.ucMiscInfo |=
					PIXEL_CLOCK_V6_MISC_REF_DIV_SRC;
		}

		/* clkV6.ucMiscInfo bit[3:2]= HDMI panel bit depth: =0:
		 * 24bpp =1:30bpp, =2:32bpp
		 * driver choose program it itself, i.e. here we pass required
		 * target rate that includes deep color.
		 */
		if (bp_params->signal_type == SIGNAL_TYPE_HDMI_TYPE_A)
			switch (bp_params->color_depth) {
			case TRANSMITTER_COLOR_DEPTH_30:
				clk.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_30BPP_V6;
				break;
			case TRANSMITTER_COLOR_DEPTH_36:
				clk.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_36BPP_V6;
				break;
			case TRANSMITTER_COLOR_DEPTH_48:
				clk.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_HDMI_48BPP;
				break;
			default:
				break;
			}

		if (EXEC_BIOS_CMD_TABLE(SetPixelClock, clk))
			result = BP_RESULT_OK;
	}

	return result;
}

static enum bp_result set_pixel_clock_v7(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	PIXEL_CLOCK_PARAMETERS_V7 clk;
	uint8_t controller_id;
	uint32_t pll_id;

	memset(&clk, 0, sizeof(clk));

	if (bp->cmd_helper->clock_source_id_to_atom(bp_params->pll_id, &pll_id)
			&& bp->cmd_helper->controller_id_to_atom(bp_params->controller_id, &controller_id)) {
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
		clk.ucCRTC = controller_id;
		clk.ucPpll = (uint8_t) pll_id;
		clk.ucTransmitterID = bp->cmd_helper->encoder_id_to_atom(dal_graphics_object_id_get_encoder_id(bp_params->encoder_object_id));
		clk.ucEncoderMode = (uint8_t) bp->cmd_helper->encoder_mode_bp_to_atom(bp_params->signal_type, false);

		clk.ulPixelClock = cpu_to_le32(bp_params->target_pixel_clock_100hz);

		clk.ucDeepColorRatio = (uint8_t) bp->cmd_helper->transmitter_color_depth_to_atom(bp_params->color_depth);

		if (bp_params->flags.FORCE_PROGRAMMING_OF_PLL)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_FORCE_PROG_PPLL;

		if (bp_params->flags.SET_EXTERNAL_REF_DIV_SRC)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_REF_DIV_SRC;

		if (bp_params->flags.PROGRAM_PHY_PLL_ONLY)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_PROG_PHYPLL;

		if (bp_params->flags.SUPPORT_YUV_420)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_YUV420_MODE;

		if (bp_params->flags.SET_XTALIN_REF_SRC)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_XTALIN;

		if (bp_params->flags.SET_GENLOCK_REF_DIV_SRC)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_REF_DIV_SRC_GENLK;

		if (bp_params->signal_type == SIGNAL_TYPE_DVI_DUAL_LINK)
			clk.ucMiscInfo |= PIXEL_CLOCK_V7_MISC_DVI_DUALLINK_EN;

		if (EXEC_BIOS_CMD_TABLE(SetPixelClock, clk))
			result = BP_RESULT_OK;
	}
	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  ENABLE PIXEL CLOCK SS
 **
 ********************************************************************************
 *******************************************************************************/
static enum bp_result enable_spread_spectrum_on_ppll_v1(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable);
static enum bp_result enable_spread_spectrum_on_ppll_v2(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable);
static enum bp_result enable_spread_spectrum_on_ppll_v3(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable);

static void init_enable_spread_spectrum_on_ppll(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(EnableSpreadSpectrumOnPPLL)) {
	case 1:
		bp->cmd_tbl.enable_spread_spectrum_on_ppll =
				enable_spread_spectrum_on_ppll_v1;
		break;
	case 2:
		bp->cmd_tbl.enable_spread_spectrum_on_ppll =
				enable_spread_spectrum_on_ppll_v2;
		break;
	case 3:
		bp->cmd_tbl.enable_spread_spectrum_on_ppll =
				enable_spread_spectrum_on_ppll_v3;
		break;
	default:
		dm_output_to_console("Don't have enable_spread_spectrum_on_ppll for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(EnableSpreadSpectrumOnPPLL));
		bp->cmd_tbl.enable_spread_spectrum_on_ppll = NULL;
		break;
	}
}

static enum bp_result enable_spread_spectrum_on_ppll_v1(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL params;

	memset(&params, 0, sizeof(params));

	if ((enable == true) && (bp_params->percentage > 0))
		params.ucEnable = ATOM_ENABLE;
	else
		params.ucEnable = ATOM_DISABLE;

	params.usSpreadSpectrumPercentage =
			cpu_to_le16((uint16_t)bp_params->percentage);
	params.ucSpreadSpectrumStep =
			(uint8_t)bp_params->ver1.step;
	params.ucSpreadSpectrumDelay =
			(uint8_t)bp_params->ver1.delay;
	/* convert back to unit of 10KHz */
	params.ucSpreadSpectrumRange =
			(uint8_t)(bp_params->ver1.range / 10000);

	if (bp_params->flags.EXTERNAL_SS)
		params.ucSpreadSpectrumType |= ATOM_EXTERNAL_SS_MASK;

	if (bp_params->flags.CENTER_SPREAD)
		params.ucSpreadSpectrumType |= ATOM_SS_CENTRE_SPREAD_MODE;

	if (bp_params->pll_id == CLOCK_SOURCE_ID_PLL1)
		params.ucPpll = ATOM_PPLL1;
	else if (bp_params->pll_id == CLOCK_SOURCE_ID_PLL2)
		params.ucPpll = ATOM_PPLL2;
	else
		BREAK_TO_DEBUGGER(); /* Unexpected PLL value!! */

	if (EXEC_BIOS_CMD_TABLE(EnableSpreadSpectrumOnPPLL, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result enable_spread_spectrum_on_ppll_v2(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V2 params;

	memset(&params, 0, sizeof(params));

	if (bp_params->pll_id == CLOCK_SOURCE_ID_PLL1)
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V2_P1PLL;
	else if (bp_params->pll_id == CLOCK_SOURCE_ID_PLL2)
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V2_P2PLL;
	else
		BREAK_TO_DEBUGGER(); /* Unexpected PLL value!! */

	if ((enable == true) && (bp_params->percentage > 0)) {
		params.ucEnable = ATOM_ENABLE;

		params.usSpreadSpectrumPercentage =
				cpu_to_le16((uint16_t)(bp_params->percentage));
		params.usSpreadSpectrumStep =
				cpu_to_le16((uint16_t)(bp_params->ds.ds_frac_size));

		if (bp_params->flags.EXTERNAL_SS)
			params.ucSpreadSpectrumType |=
					ATOM_PPLL_SS_TYPE_V2_EXT_SPREAD;

		if (bp_params->flags.CENTER_SPREAD)
			params.ucSpreadSpectrumType |=
					ATOM_PPLL_SS_TYPE_V2_CENTRE_SPREAD;

		/* Both amounts need to be left shifted first before bit
		 * comparison. Otherwise, the result will always be zero here
		 */
		params.usSpreadSpectrumAmount = cpu_to_le16((uint16_t)(
				((bp_params->ds.feedback_amount <<
						ATOM_PPLL_SS_AMOUNT_V2_FBDIV_SHIFT) &
						ATOM_PPLL_SS_AMOUNT_V2_FBDIV_MASK) |
						((bp_params->ds.nfrac_amount <<
								ATOM_PPLL_SS_AMOUNT_V2_NFRAC_SHIFT) &
								ATOM_PPLL_SS_AMOUNT_V2_NFRAC_MASK)));
	} else
		params.ucEnable = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(EnableSpreadSpectrumOnPPLL, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result enable_spread_spectrum_on_ppll_v3(
	struct bios_parser *bp,
	struct bp_spread_spectrum_parameters *bp_params,
	bool enable)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ENABLE_SPREAD_SPECTRUM_ON_PPLL_V3 params;

	memset(&params, 0, sizeof(params));

	switch (bp_params->pll_id) {
	case CLOCK_SOURCE_ID_PLL0:
		/* ATOM_PPLL_SS_TYPE_V3_P0PLL; this is pixel clock only,
		 * not for SI display clock.
		 */
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V3_DCPLL;
		break;
	case CLOCK_SOURCE_ID_PLL1:
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V3_P1PLL;
		break;

	case CLOCK_SOURCE_ID_PLL2:
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V3_P2PLL;
		break;

	case CLOCK_SOURCE_ID_DCPLL:
		params.ucSpreadSpectrumType = ATOM_PPLL_SS_TYPE_V3_DCPLL;
		break;

	default:
		BREAK_TO_DEBUGGER();
		/* Unexpected PLL value!! */
		return result;
	}

	if (enable == true) {
		params.ucEnable = ATOM_ENABLE;

		params.usSpreadSpectrumAmountFrac =
				cpu_to_le16((uint16_t)(bp_params->ds_frac_amount));
		params.usSpreadSpectrumStep =
				cpu_to_le16((uint16_t)(bp_params->ds.ds_frac_size));

		if (bp_params->flags.EXTERNAL_SS)
			params.ucSpreadSpectrumType |=
					ATOM_PPLL_SS_TYPE_V3_EXT_SPREAD;
		if (bp_params->flags.CENTER_SPREAD)
			params.ucSpreadSpectrumType |=
					ATOM_PPLL_SS_TYPE_V3_CENTRE_SPREAD;

		/* Both amounts need to be left shifted first before bit
		 * comparison. Otherwise, the result will always be zero here
		 */
		params.usSpreadSpectrumAmount = cpu_to_le16((uint16_t)(
				((bp_params->ds.feedback_amount <<
						ATOM_PPLL_SS_AMOUNT_V3_FBDIV_SHIFT) &
						ATOM_PPLL_SS_AMOUNT_V3_FBDIV_MASK) |
						((bp_params->ds.nfrac_amount <<
								ATOM_PPLL_SS_AMOUNT_V3_NFRAC_SHIFT) &
								ATOM_PPLL_SS_AMOUNT_V3_NFRAC_MASK)));
	} else
		params.ucEnable = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(EnableSpreadSpectrumOnPPLL, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  ADJUST DISPLAY PLL
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result adjust_display_pll_v2(
	struct bios_parser *bp,
	struct bp_adjust_pixel_clock_parameters *bp_params);
static enum bp_result adjust_display_pll_v3(
	struct bios_parser *bp,
	struct bp_adjust_pixel_clock_parameters *bp_params);

static void init_adjust_display_pll(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(AdjustDisplayPll)) {
	case 2:
		bp->cmd_tbl.adjust_display_pll = adjust_display_pll_v2;
		break;
	case 3:
		bp->cmd_tbl.adjust_display_pll = adjust_display_pll_v3;
		break;
	default:
		dm_output_to_console("Don't have adjust_display_pll for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(AdjustDisplayPll));
		bp->cmd_tbl.adjust_display_pll = NULL;
		break;
	}
}

static enum bp_result adjust_display_pll_v2(
	struct bios_parser *bp,
	struct bp_adjust_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ADJUST_DISPLAY_PLL_PS_ALLOCATION params = { 0 };

	/* We need to convert from KHz units into 10KHz units and then convert
	 * output pixel clock back 10KHz-->KHz */
	uint32_t pixel_clock_10KHz_in = bp_params->pixel_clock / 10;

	params.usPixelClock = cpu_to_le16((uint16_t)(pixel_clock_10KHz_in));
	params.ucTransmitterID =
			bp->cmd_helper->encoder_id_to_atom(
					dal_graphics_object_id_get_encoder_id(
							bp_params->encoder_object_id));
	params.ucEncodeMode =
			(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
					bp_params->signal_type, false);

	if (EXEC_BIOS_CMD_TABLE(AdjustDisplayPll, params)) {
		/* Convert output pixel clock back 10KHz-->KHz: multiply
		 * original pixel clock in KHz by ratio
		 * [output pxlClk/input pxlClk] */
		uint64_t pixel_clk_10_khz_out =
				(uint64_t)le16_to_cpu(params.usPixelClock);
		uint64_t pixel_clk = (uint64_t)bp_params->pixel_clock;

		if (pixel_clock_10KHz_in != 0) {
			bp_params->adjusted_pixel_clock =
					div_u64(pixel_clk * pixel_clk_10_khz_out,
							pixel_clock_10KHz_in);
		} else {
			bp_params->adjusted_pixel_clock = 0;
			BREAK_TO_DEBUGGER();
		}

		result = BP_RESULT_OK;
	}

	return result;
}

static enum bp_result adjust_display_pll_v3(
	struct bios_parser *bp,
	struct bp_adjust_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	ADJUST_DISPLAY_PLL_PS_ALLOCATION_V3 params;
	uint32_t pixel_clk_10_kHz_in = bp_params->pixel_clock / 10;

	memset(&params, 0, sizeof(params));

	/* We need to convert from KHz units into 10KHz units and then convert
	 * output pixel clock back 10KHz-->KHz */
	params.sInput.usPixelClock = cpu_to_le16((uint16_t)pixel_clk_10_kHz_in);
	params.sInput.ucTransmitterID =
			bp->cmd_helper->encoder_id_to_atom(
					dal_graphics_object_id_get_encoder_id(
							bp_params->encoder_object_id));
	params.sInput.ucEncodeMode =
			(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
					bp_params->signal_type, false);

	if (bp_params->ss_enable == true)
		params.sInput.ucDispPllConfig |= DISPPLL_CONFIG_SS_ENABLE;

	if (bp_params->signal_type == SIGNAL_TYPE_DVI_DUAL_LINK)
		params.sInput.ucDispPllConfig |= DISPPLL_CONFIG_DUAL_LINK;

	if (EXEC_BIOS_CMD_TABLE(AdjustDisplayPll, params)) {
		/* Convert output pixel clock back 10KHz-->KHz: multiply
		 * original pixel clock in KHz by ratio
		 * [output pxlClk/input pxlClk] */
		uint64_t pixel_clk_10_khz_out =
				(uint64_t)le32_to_cpu(params.sOutput.ulDispPllFreq);
		uint64_t pixel_clk = (uint64_t)bp_params->pixel_clock;

		if (pixel_clk_10_kHz_in != 0) {
			bp_params->adjusted_pixel_clock =
					div_u64(pixel_clk * pixel_clk_10_khz_out,
							pixel_clk_10_kHz_in);
		} else {
			bp_params->adjusted_pixel_clock = 0;
			BREAK_TO_DEBUGGER();
		}

		bp_params->reference_divider = params.sOutput.ucRefDiv;
		bp_params->pixel_clock_post_divider = params.sOutput.ucPostDiv;

		result = BP_RESULT_OK;
	}

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  DAC ENCODER CONTROL
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result dac1_encoder_control_v1(
	struct bios_parser *bp,
	bool enable,
	uint32_t pixel_clock,
	uint8_t dac_standard);
static enum bp_result dac2_encoder_control_v1(
	struct bios_parser *bp,
	bool enable,
	uint32_t pixel_clock,
	uint8_t dac_standard);

static void init_dac_encoder_control(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(DAC1EncoderControl)) {
	case 1:
		bp->cmd_tbl.dac1_encoder_control = dac1_encoder_control_v1;
		break;
	default:
		bp->cmd_tbl.dac1_encoder_control = NULL;
		break;
	}
	switch (BIOS_CMD_TABLE_PARA_REVISION(DAC2EncoderControl)) {
	case 1:
		bp->cmd_tbl.dac2_encoder_control = dac2_encoder_control_v1;
		break;
	default:
		bp->cmd_tbl.dac2_encoder_control = NULL;
		break;
	}
}

static void dac_encoder_control_prepare_params(
	DAC_ENCODER_CONTROL_PS_ALLOCATION *params,
	bool enable,
	uint32_t pixel_clock,
	uint8_t dac_standard)
{
	params->ucDacStandard = dac_standard;
	if (enable)
		params->ucAction = ATOM_ENABLE;
	else
		params->ucAction = ATOM_DISABLE;

	/* We need to convert from KHz units into 10KHz units
	 * it looks as if the TvControl do not care about pixel clock
	 */
	params->usPixelClock = cpu_to_le16((uint16_t)(pixel_clock / 10));
}

static enum bp_result dac1_encoder_control_v1(
	struct bios_parser *bp,
	bool enable,
	uint32_t pixel_clock,
	uint8_t dac_standard)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DAC_ENCODER_CONTROL_PS_ALLOCATION params;

	dac_encoder_control_prepare_params(
		&params,
		enable,
		pixel_clock,
		dac_standard);

	if (EXEC_BIOS_CMD_TABLE(DAC1EncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result dac2_encoder_control_v1(
	struct bios_parser *bp,
	bool enable,
	uint32_t pixel_clock,
	uint8_t dac_standard)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DAC_ENCODER_CONTROL_PS_ALLOCATION params;

	dac_encoder_control_prepare_params(
		&params,
		enable,
		pixel_clock,
		dac_standard);

	if (EXEC_BIOS_CMD_TABLE(DAC2EncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  DAC OUTPUT CONTROL
 **
 ********************************************************************************
 *******************************************************************************/
static enum bp_result dac1_output_control_v1(
	struct bios_parser *bp,
	bool enable);
static enum bp_result dac2_output_control_v1(
	struct bios_parser *bp,
	bool enable);

static void init_dac_output_control(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(DAC1OutputControl)) {
	case 1:
		bp->cmd_tbl.dac1_output_control = dac1_output_control_v1;
		break;
	default:
		bp->cmd_tbl.dac1_output_control = NULL;
		break;
	}
	switch (BIOS_CMD_TABLE_PARA_REVISION(DAC2OutputControl)) {
	case 1:
		bp->cmd_tbl.dac2_output_control = dac2_output_control_v1;
		break;
	default:
		bp->cmd_tbl.dac2_output_control = NULL;
		break;
	}
}

static enum bp_result dac1_output_control_v1(
	struct bios_parser *bp, bool enable)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION params;

	if (enable)
		params.ucAction = ATOM_ENABLE;
	else
		params.ucAction = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(DAC1OutputControl, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result dac2_output_control_v1(
	struct bios_parser *bp, bool enable)
{
	enum bp_result result = BP_RESULT_FAILURE;
	DISPLAY_DEVICE_OUTPUT_CONTROL_PS_ALLOCATION params;

	if (enable)
		params.ucAction = ATOM_ENABLE;
	else
		params.ucAction = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(DAC2OutputControl, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  SET CRTC TIMING
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result set_crtc_using_dtd_timing_v3(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params);
static enum bp_result set_crtc_timing_v1(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params);

static void init_set_crtc_timing(struct bios_parser *bp)
{
	uint32_t dtd_version =
			BIOS_CMD_TABLE_PARA_REVISION(SetCRTC_UsingDTDTiming);
	if (dtd_version > 2)
		switch (dtd_version) {
		case 3:
			bp->cmd_tbl.set_crtc_timing =
					set_crtc_using_dtd_timing_v3;
			break;
		default:
			dm_output_to_console("Don't have set_crtc_timing for dtd v%d\n",
				 dtd_version);
			bp->cmd_tbl.set_crtc_timing = NULL;
			break;
		}
	else
		switch (BIOS_CMD_TABLE_PARA_REVISION(SetCRTC_Timing)) {
		case 1:
			bp->cmd_tbl.set_crtc_timing = set_crtc_timing_v1;
			break;
		default:
			dm_output_to_console("Don't have set_crtc_timing for v%d\n",
				 BIOS_CMD_TABLE_PARA_REVISION(SetCRTC_Timing));
			bp->cmd_tbl.set_crtc_timing = NULL;
			break;
		}
}

static enum bp_result set_crtc_timing_v1(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	SET_CRTC_TIMING_PARAMETERS_PS_ALLOCATION params = {0};
	uint8_t atom_controller_id;

	if (bp->cmd_helper->controller_id_to_atom(
			bp_params->controller_id, &atom_controller_id))
		params.ucCRTC = atom_controller_id;

	params.usH_Total = cpu_to_le16((uint16_t)(bp_params->h_total));
	params.usH_Disp = cpu_to_le16((uint16_t)(bp_params->h_addressable));
	params.usH_SyncStart = cpu_to_le16((uint16_t)(bp_params->h_sync_start));
	params.usH_SyncWidth = cpu_to_le16((uint16_t)(bp_params->h_sync_width));
	params.usV_Total = cpu_to_le16((uint16_t)(bp_params->v_total));
	params.usV_Disp = cpu_to_le16((uint16_t)(bp_params->v_addressable));
	params.usV_SyncStart =
			cpu_to_le16((uint16_t)(bp_params->v_sync_start));
	params.usV_SyncWidth =
			cpu_to_le16((uint16_t)(bp_params->v_sync_width));

	/* VBIOS does not expect any value except zero into this call, for
	 * underscan use another entry ProgramOverscan call but when mode
	 * 1776x1000 with the overscan 72x44 .e.i. 1920x1080 @30 DAL2 is ok,
	 * but when same ,but 60 Hz there is corruption
	 * DAL1 does not allow the mode 1776x1000@60
	 */
	params.ucOverscanRight = (uint8_t)bp_params->h_overscan_right;
	params.ucOverscanLeft = (uint8_t)bp_params->h_overscan_left;
	params.ucOverscanBottom = (uint8_t)bp_params->v_overscan_bottom;
	params.ucOverscanTop = (uint8_t)bp_params->v_overscan_top;

	if (0 == bp_params->flags.HSYNC_POSITIVE_POLARITY)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_HSYNC_POLARITY);

	if (0 == bp_params->flags.VSYNC_POSITIVE_POLARITY)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_VSYNC_POLARITY);

	if (bp_params->flags.INTERLACE) {
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_INTERLACE);

		/* original DAL code has this condition to apply tis for
		 * non-TV/CV only due to complex MV testing for possible
		 * impact
		 * if (pACParameters->signal != SignalType_YPbPr &&
		 *  pACParameters->signal != SignalType_Composite &&
		 *  pACParameters->signal != SignalType_SVideo)
		 */
		/* HW will deduct 0.5 line from 2nd feild.
		 * i.e. for 1080i, it is 2 lines for 1st field, 2.5
		 * lines for the 2nd feild. we need input as 5 instead
		 * of 4, but it is 4 either from Edid data
		 * (spec CEA 861) or CEA timing table.
		 */
		params.usV_SyncStart =
				cpu_to_le16((uint16_t)(bp_params->v_sync_start + 1));
	}

	if (bp_params->flags.HORZ_COUNT_BY_TWO)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_DOUBLE_CLOCK_MODE);

	if (EXEC_BIOS_CMD_TABLE(SetCRTC_Timing, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result set_crtc_using_dtd_timing_v3(
	struct bios_parser *bp,
	struct bp_hw_crtc_timing_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;
	SET_CRTC_USING_DTD_TIMING_PARAMETERS params = {0};
	uint8_t atom_controller_id;

	if (bp->cmd_helper->controller_id_to_atom(
			bp_params->controller_id, &atom_controller_id))
		params.ucCRTC = atom_controller_id;

	/* bios usH_Size wants h addressable size */
	params.usH_Size = cpu_to_le16((uint16_t)bp_params->h_addressable);
	/* bios usH_Blanking_Time wants borders included in blanking */
	params.usH_Blanking_Time =
			cpu_to_le16((uint16_t)(bp_params->h_total - bp_params->h_addressable));
	/* bios usV_Size wants v addressable size */
	params.usV_Size = cpu_to_le16((uint16_t)bp_params->v_addressable);
	/* bios usV_Blanking_Time wants borders included in blanking */
	params.usV_Blanking_Time =
			cpu_to_le16((uint16_t)(bp_params->v_total - bp_params->v_addressable));
	/* bios usHSyncOffset is the offset from the end of h addressable,
	 * our horizontalSyncStart is the offset from the beginning
	 * of h addressable */
	params.usH_SyncOffset =
			cpu_to_le16((uint16_t)(bp_params->h_sync_start - bp_params->h_addressable));
	params.usH_SyncWidth = cpu_to_le16((uint16_t)bp_params->h_sync_width);
	/* bios usHSyncOffset is the offset from the end of v addressable,
	 * our verticalSyncStart is the offset from the beginning of
	 * v addressable */
	params.usV_SyncOffset =
			cpu_to_le16((uint16_t)(bp_params->v_sync_start - bp_params->v_addressable));
	params.usV_SyncWidth = cpu_to_le16((uint16_t)bp_params->v_sync_width);

	/* we assume that overscan from original timing does not get bigger
	 * than 255
	 * we will program all the borders in the Set CRTC Overscan call below
	 */

	if (0 == bp_params->flags.HSYNC_POSITIVE_POLARITY)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_HSYNC_POLARITY);

	if (0 == bp_params->flags.VSYNC_POSITIVE_POLARITY)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_VSYNC_POLARITY);

	if (bp_params->flags.INTERLACE)	{
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_INTERLACE);

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
			le16_add_cpu(&params.usV_SyncOffset, 1);
		}
	}

	if (bp_params->flags.HORZ_COUNT_BY_TWO)
		params.susModeMiscInfo.usAccess =
				cpu_to_le16(le16_to_cpu(params.susModeMiscInfo.usAccess) | ATOM_DOUBLE_CLOCK_MODE);

	if (EXEC_BIOS_CMD_TABLE(SetCRTC_UsingDTDTiming, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  ENABLE CRTC
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result enable_crtc_v1(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable);

static void init_enable_crtc(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(EnableCRTC)) {
	case 1:
		bp->cmd_tbl.enable_crtc = enable_crtc_v1;
		break;
	default:
		dm_output_to_console("Don't have enable_crtc for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(EnableCRTC));
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
	ENABLE_CRTC_PARAMETERS params = {0};
	uint8_t id;

	if (bp->cmd_helper->controller_id_to_atom(controller_id, &id))
		params.ucCRTC = id;
	else
		return BP_RESULT_BADINPUT;

	if (enable)
		params.ucEnable = ATOM_ENABLE;
	else
		params.ucEnable = ATOM_DISABLE;

	if (EXEC_BIOS_CMD_TABLE(EnableCRTC, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  ENABLE CRTC MEM REQ
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result enable_crtc_mem_req_v1(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable);

static void init_enable_crtc_mem_req(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(EnableCRTCMemReq)) {
	case 1:
		bp->cmd_tbl.enable_crtc_mem_req = enable_crtc_mem_req_v1;
		break;
	default:
		bp->cmd_tbl.enable_crtc_mem_req = NULL;
		break;
	}
}

static enum bp_result enable_crtc_mem_req_v1(
	struct bios_parser *bp,
	enum controller_id controller_id,
	bool enable)
{
	bool result = BP_RESULT_BADINPUT;
	ENABLE_CRTC_PARAMETERS params = {0};
	uint8_t id;

	if (bp->cmd_helper->controller_id_to_atom(controller_id, &id)) {
		params.ucCRTC = id;

		if (enable)
			params.ucEnable = ATOM_ENABLE;
		else
			params.ucEnable = ATOM_DISABLE;

		if (EXEC_BIOS_CMD_TABLE(EnableCRTCMemReq, params))
			result = BP_RESULT_OK;
		else
			result = BP_RESULT_FAILURE;
	}

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  DISPLAY PLL
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result program_clock_v5(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);
static enum bp_result program_clock_v6(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params);

static void init_program_clock(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(SetPixelClock)) {
	case 5:
		bp->cmd_tbl.program_clock = program_clock_v5;
		break;
	case 6:
		bp->cmd_tbl.program_clock = program_clock_v6;
		break;
	default:
		dm_output_to_console("Don't have program_clock for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(SetPixelClock));
		bp->cmd_tbl.program_clock = NULL;
		break;
	}
}

static enum bp_result program_clock_v5(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;

	SET_PIXEL_CLOCK_PS_ALLOCATION_V5 params;
	uint32_t atom_pll_id;

	memset(&params, 0, sizeof(params));
	if (!bp->cmd_helper->clock_source_id_to_atom(
			bp_params->pll_id, &atom_pll_id)) {
		BREAK_TO_DEBUGGER(); /* Invalid Input!! */
		return BP_RESULT_BADINPUT;
	}

	/* We need to convert from KHz units into 10KHz units */
	params.sPCLKInput.ucPpll = (uint8_t) atom_pll_id;
	params.sPCLKInput.usPixelClock =
			cpu_to_le16((uint16_t) (bp_params->target_pixel_clock_100hz / 100));
	params.sPCLKInput.ucCRTC = (uint8_t) ATOM_CRTC_INVALID;

	if (bp_params->flags.SET_EXTERNAL_REF_DIV_SRC)
		params.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_MISC_REF_DIV_SRC;

	if (EXEC_BIOS_CMD_TABLE(SetPixelClock, params))
		result = BP_RESULT_OK;

	return result;
}

static enum bp_result program_clock_v6(
	struct bios_parser *bp,
	struct bp_pixel_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;

	SET_PIXEL_CLOCK_PS_ALLOCATION_V6 params;
	uint32_t atom_pll_id;

	memset(&params, 0, sizeof(params));

	if (!bp->cmd_helper->clock_source_id_to_atom(
			bp_params->pll_id, &atom_pll_id)) {
		BREAK_TO_DEBUGGER(); /*Invalid Input!!*/
		return BP_RESULT_BADINPUT;
	}

	/* We need to convert from KHz units into 10KHz units */
	params.sPCLKInput.ucPpll = (uint8_t)atom_pll_id;
	params.sPCLKInput.ulDispEngClkFreq =
			cpu_to_le32(bp_params->target_pixel_clock_100hz / 100);

	if (bp_params->flags.SET_EXTERNAL_REF_DIV_SRC)
		params.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_MISC_REF_DIV_SRC;

	if (bp_params->flags.SET_DISPCLK_DFS_BYPASS)
		params.sPCLKInput.ucMiscInfo |= PIXEL_CLOCK_V6_MISC_DPREFCLK_BYPASS;

	if (EXEC_BIOS_CMD_TABLE(SetPixelClock, params)) {
		/* True display clock is returned by VBIOS if DFS bypass
		 * is enabled. */
		bp_params->dfs_bypass_display_clock =
				(uint32_t)(le32_to_cpu(params.sPCLKInput.ulDispEngClkFreq) * 10);
		result = BP_RESULT_OK;
	}

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  EXTERNAL ENCODER CONTROL
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result external_encoder_control_v3(
	struct bios_parser *bp,
	struct bp_external_encoder_control *cntl);

static void init_external_encoder_control(
	struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(ExternalEncoderControl)) {
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
	enum bp_result result = BP_RESULT_FAILURE;

	/* we need use _PS_Alloc struct */
	EXTERNAL_ENCODER_CONTROL_PS_ALLOCATION_V3 params;
	EXTERNAL_ENCODER_CONTROL_PARAMETERS_V3 *cntl_params;
	struct graphics_object_id encoder;
	bool is_input_signal_dp = false;

	memset(&params, 0, sizeof(params));

	cntl_params = &params.sExtEncoder;

	encoder = cntl->encoder_id;

	/* check if encoder supports external encoder control table */
	switch (dal_graphics_object_id_get_encoder_id(encoder)) {
	case ENCODER_ID_EXTERNAL_NUTMEG:
	case ENCODER_ID_EXTERNAL_TRAVIS:
		is_input_signal_dp = true;
		break;

	default:
		BREAK_TO_DEBUGGER();
		return BP_RESULT_BADINPUT;
	}

	/* Fill information based on the action
	 *
	 * Bit[6:4]: indicate external encoder, applied to all functions.
	 * =0: external encoder1, mapped to external encoder enum id1
	 * =1: external encoder2, mapped to external encoder enum id2
	 *
	 * enum ObjectEnumId
	 * {
	 *  EnumId_Unknown = 0,
	 *  EnumId_1,
	 *  EnumId_2,
	 * };
	 */
	cntl_params->ucConfig = (uint8_t)((encoder.enum_id - 1) << 4);

	switch (cntl->action) {
	case EXTERNAL_ENCODER_CONTROL_INIT:
		/* output display connector type. Only valid in encoder
		 * initialization */
		cntl_params->usConnectorId =
				cpu_to_le16((uint16_t)cntl->connector_obj_id.id);
		break;
	case EXTERNAL_ENCODER_CONTROL_SETUP:
		/* EXTERNAL_ENCODER_CONTROL_PARAMETERS_V3 pixel clock unit in
		 * 10KHz
		 * output display device pixel clock frequency in unit of 10KHz.
		 * Only valid in setup and enableoutput
		 */
		cntl_params->usPixelClock =
				cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
		/* Indicate display output signal type drive by external
		 * encoder, only valid in setup and enableoutput */
		cntl_params->ucEncoderMode =
				(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
						cntl->signal, false);

		if (is_input_signal_dp) {
			/* Bit[0]: indicate link rate, =1: 2.7Ghz, =0: 1.62Ghz,
			 * only valid in encoder setup with DP mode. */
			if (LINK_RATE_HIGH == cntl->link_rate)
				cntl_params->ucConfig |= 1;
			/* output color depth Indicate encoder data bpc format
			 * in DP mode, only valid in encoder setup in DP mode.
			 */
			cntl_params->ucBitPerColor =
					(uint8_t)(cntl->color_depth);
		}
		/* Indicate how many lanes used by external encoder, only valid
		 * in encoder setup and enableoutput. */
		cntl_params->ucLaneNum = (uint8_t)(cntl->lanes_number);
		break;
	case EXTERNAL_ENCODER_CONTROL_ENABLE:
		cntl_params->usPixelClock =
				cpu_to_le16((uint16_t)(cntl->pixel_clock / 10));
		cntl_params->ucEncoderMode =
				(uint8_t)bp->cmd_helper->encoder_mode_bp_to_atom(
						cntl->signal, false);
		cntl_params->ucLaneNum = (uint8_t)cntl->lanes_number;
		break;
	default:
		break;
	}

	cntl_params->ucAction = (uint8_t)cntl->action;

	if (EXEC_BIOS_CMD_TABLE(ExternalEncoderControl, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  ENABLE DISPLAY POWER GATING
 **
 ********************************************************************************
 *******************************************************************************/

static enum bp_result enable_disp_power_gating_v2_1(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action);

static void init_enable_disp_power_gating(
	struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(EnableDispPowerGating)) {
	case 1:
		bp->cmd_tbl.enable_disp_power_gating =
				enable_disp_power_gating_v2_1;
		break;
	default:
		dm_output_to_console("Don't enable_disp_power_gating enable_crtc for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(EnableDispPowerGating));
		bp->cmd_tbl.enable_disp_power_gating = NULL;
		break;
	}
}

static enum bp_result enable_disp_power_gating_v2_1(
	struct bios_parser *bp,
	enum controller_id crtc_id,
	enum bp_pipe_control_action action)
{
	enum bp_result result = BP_RESULT_FAILURE;

	ENABLE_DISP_POWER_GATING_PS_ALLOCATION params = {0};
	uint8_t atom_crtc_id;

	if (bp->cmd_helper->controller_id_to_atom(crtc_id, &atom_crtc_id))
		params.ucDispPipeId = atom_crtc_id;
	else
		return BP_RESULT_BADINPUT;

	params.ucEnable =
			bp->cmd_helper->disp_power_gating_action_to_atom(action);

	if (EXEC_BIOS_CMD_TABLE(EnableDispPowerGating, params))
		result = BP_RESULT_OK;

	return result;
}

/*******************************************************************************
 ********************************************************************************
 **
 **                  SET DCE CLOCK
 **
 ********************************************************************************
 *******************************************************************************/
static enum bp_result set_dce_clock_v2_1(
	struct bios_parser *bp,
	struct bp_set_dce_clock_parameters *bp_params);

static void init_set_dce_clock(struct bios_parser *bp)
{
	switch (BIOS_CMD_TABLE_PARA_REVISION(SetDCEClock)) {
	case 1:
		bp->cmd_tbl.set_dce_clock = set_dce_clock_v2_1;
		break;
	default:
		dm_output_to_console("Don't have set_dce_clock for v%d\n",
			 BIOS_CMD_TABLE_PARA_REVISION(SetDCEClock));
		bp->cmd_tbl.set_dce_clock = NULL;
		break;
	}
}

static enum bp_result set_dce_clock_v2_1(
	struct bios_parser *bp,
	struct bp_set_dce_clock_parameters *bp_params)
{
	enum bp_result result = BP_RESULT_FAILURE;

	SET_DCE_CLOCK_PS_ALLOCATION_V2_1 params;
	uint32_t atom_pll_id;
	uint32_t atom_clock_type;
	const struct command_table_helper *cmd = bp->cmd_helper;

	memset(&params, 0, sizeof(params));

	if (!cmd->clock_source_id_to_atom(bp_params->pll_id, &atom_pll_id) ||
			!cmd->dc_clock_type_to_atom(bp_params->clock_type, &atom_clock_type))
		return BP_RESULT_BADINPUT;

	params.asParam.ucDCEClkSrc  = atom_pll_id;
	params.asParam.ucDCEClkType = atom_clock_type;

	if (bp_params->clock_type == DCECLOCK_TYPE_DPREFCLK) {
		if (bp_params->flags.USE_GENLOCK_AS_SOURCE_FOR_DPREFCLK)
			params.asParam.ucDCEClkFlag |= DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENLK;

		if (bp_params->flags.USE_PCIE_AS_SOURCE_FOR_DPREFCLK)
			params.asParam.ucDCEClkFlag |= DCE_CLOCK_FLAG_PLL_REFCLK_SRC_PCIE;

		if (bp_params->flags.USE_XTALIN_AS_SOURCE_FOR_DPREFCLK)
			params.asParam.ucDCEClkFlag |= DCE_CLOCK_FLAG_PLL_REFCLK_SRC_XTALIN;

		if (bp_params->flags.USE_GENERICA_AS_SOURCE_FOR_DPREFCLK)
			params.asParam.ucDCEClkFlag |= DCE_CLOCK_FLAG_PLL_REFCLK_SRC_GENERICA;
	}
	else
		/* only program clock frequency if display clock is used; VBIOS will program DPREFCLK */
		/* We need to convert from KHz units into 10KHz units */
		params.asParam.ulDCEClkFreq = cpu_to_le32(bp_params->target_clock_frequency / 10);

	if (EXEC_BIOS_CMD_TABLE(SetDCEClock, params)) {
		/* Convert from 10KHz units back to KHz */
		bp_params->target_clock_frequency = le32_to_cpu(params.asParam.ulDCEClkFreq) * 10;
		result = BP_RESULT_OK;
	}

	return result;
}
