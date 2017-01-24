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

#include "reg_helper.h"

#include "core_types.h"
#include "link_encoder.h"
#include "dce_link_encoder.h"
#include "stream_encoder.h"
#include "i2caux_interface.h"
#include "dc_bios_types.h"

#include "gpio_service_interface.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
#include "dce/dce_11_0_enum.h"

#ifndef HPD0_DC_HPD_CONTROL__DC_HPD_EN_MASK
#define HPD0_DC_HPD_CONTROL__DC_HPD_EN_MASK  0x10000000L
#endif

#ifndef HPD0_DC_HPD_CONTROL__DC_HPD_EN__SHIFT
#define HPD0_DC_HPD_CONTROL__DC_HPD_EN__SHIFT  0x1c
#endif

#define CTX \
	enc110->base.ctx

#define REG(reg)\
	(enc110->link_regs->reg)

#define AUX_REG(reg)\
	(enc110->aux_regs->reg)

#define HPD_REG(reg)\
	(enc110->hpd_regs->reg)

#define DEFAULT_AUX_MAX_DATA_SIZE 16
#define AUX_MAX_DEFER_WRITE_RETRY 20
/*
 * @brief
 * Trigger Source Select
 * ASIC-dependent, actual values for register programming
 */
#define DCE110_DIG_FE_SOURCE_SELECT_INVALID 0x0
#define DCE110_DIG_FE_SOURCE_SELECT_DIGA 0x1
#define DCE110_DIG_FE_SOURCE_SELECT_DIGB 0x2
#define DCE110_DIG_FE_SOURCE_SELECT_DIGC 0x4
#define DCE110_DIG_FE_SOURCE_SELECT_DIGD 0x08
#define DCE110_DIG_FE_SOURCE_SELECT_DIGE 0x10
#define DCE110_DIG_FE_SOURCE_SELECT_DIGF 0x20

/* all values are in milliseconds */
/* For eDP, after power-up/power/down,
 * 300/500 msec max. delay from LCDVCC to black video generation */
#define PANEL_POWER_UP_TIMEOUT 300
#define PANEL_POWER_DOWN_TIMEOUT 500
#define HPD_CHECK_INTERVAL 10

/* Minimum pixel clock, in KHz. For TMDS signal is 25.00 MHz */
#define TMDS_MIN_PIXEL_CLOCK 25000
/* Maximum pixel clock, in KHz. For TMDS signal is 165.00 MHz */
#define TMDS_MAX_PIXEL_CLOCK 165000
/* For current ASICs pixel clock - 600MHz */
#define MAX_ENCODER_CLOCK 600000

/* PSR related commands */
#define PSR_ENABLE 0x20
#define PSR_EXIT 0x21
#define PSR_SET 0x23

/*TODO: Used for psr wakeup for set backlight level*/
static unsigned int psr_crtc_offset;

enum {
	DP_MST_UPDATE_MAX_RETRY = 50
};

#define DIG_REG(reg)\
	(reg + enc110->offsets.dig)

#define DP_REG(reg)\
	(reg + enc110->offsets.dp)

static const struct link_encoder_funcs dce110_lnk_enc_funcs = {
	.validate_output_with_stream =
		dce110_link_encoder_validate_output_with_stream,
	.hw_init = dce110_link_encoder_hw_init,
	.setup = dce110_link_encoder_setup,
	.enable_tmds_output = dce110_link_encoder_enable_tmds_output,
	.enable_dp_output = dce110_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dce110_link_encoder_enable_dp_mst_output,
	.disable_output = dce110_link_encoder_disable_output,
	.dp_set_lane_settings = dce110_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dce110_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dce110_link_encoder_update_mst_stream_allocation_table,
	.set_dmcu_psr_enable = dce110_link_encoder_set_dmcu_psr_enable,
	.setup_dmcu_psr = dce110_link_encoder_setup_dmcu_psr,
	.backlight_control = dce110_link_encoder_edp_backlight_control,
	.power_control = dce110_link_encoder_edp_power_control,
	.connect_dig_be_to_fe = dce110_link_encoder_connect_dig_be_to_fe,
	.enable_hpd = dce110_link_encoder_enable_hpd,
	.disable_hpd = dce110_link_encoder_disable_hpd,
	.destroy = dce110_link_encoder_destroy
};


static enum bp_result link_transmitter_control(
	struct dce110_link_encoder *enc110,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;
	struct dc_bios *bp = enc110->base.ctx->dc_bios;

	result = bp->funcs->transmitter_control(bp, cntl);

	return result;
}

static void enable_phy_bypass_mode(
	struct dce110_link_encoder *enc110,
	bool enable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_UPDATE(DP_DPHY_CNTL, DPHY_BYPASS, enable);

}

static void disable_prbs_symbols(
	struct dce110_link_encoder *enc110,
	bool disable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_UPDATE_4(DP_DPHY_CNTL,
			DPHY_ATEST_SEL_LANE0, disable,
			DPHY_ATEST_SEL_LANE1, disable,
			DPHY_ATEST_SEL_LANE2, disable,
			DPHY_ATEST_SEL_LANE3, disable);
}

static void disable_prbs_mode(
	struct dce110_link_encoder *enc110)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_UPDATE(DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN, 0);
}

static void program_pattern_symbols(
	struct dce110_link_encoder *enc110,
	uint16_t pattern_symbols[8])
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_SET_3(DP_DPHY_SYM0, 0,
			DPHY_SYM1, pattern_symbols[0],
			DPHY_SYM2, pattern_symbols[1],
			DPHY_SYM3, pattern_symbols[2]);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_SET_3(DP_DPHY_SYM1, 0,
			DPHY_SYM4, pattern_symbols[3],
			DPHY_SYM5, pattern_symbols[4],
			DPHY_SYM6, pattern_symbols[5]);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_SET_2(DP_DPHY_SYM2, 0,
			DPHY_SYM7, pattern_symbols[6],
			DPHY_SYM8, pattern_symbols[7]);
}

static void set_dp_phy_pattern_d102(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc110, false);

	/* For 10-bit PRBS or debug symbols
	 * please use the following sequence: */

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(enc110, true);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);

	/* Program debug symbols to be output */
	{
		uint16_t pattern_symbols[8] = {
			0x2AA, 0x2AA, 0x2AA, 0x2AA,
			0x2AA, 0x2AA, 0x2AA, 0x2AA
		};

		program_pattern_symbols(enc110, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_link_training_complete(
	struct dce110_link_encoder *enc110,
	bool complete)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset */

	REG_UPDATE(DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, complete);

}

void dce110_link_encoder_set_dp_phy_pattern_training_pattern(
	struct link_encoder *enc,
	uint32_t index)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	/* Write Training Pattern */

	REG_WRITE(DP_DPHY_TRAINING_PATTERN_SEL, index);

	/* Set HW Register Training Complete to false */

	set_link_training_complete(enc110, false);

	/* Disable PHY Bypass mode to output Training Pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);
}

static void set_dp_phy_pattern_symbol_error(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	uint32_t value = 0x0;

	enable_phy_bypass_mode(enc110, false);

	/* program correct panel mode*/
	{
		ASSERT(REG(DP_DPHY_INTERNAL_CTRL));
		/*DCE 120 does not have this reg*/

		REG_WRITE(DP_DPHY_INTERNAL_CTRL, value);
	}

	/* A PRBS23 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(enc110, false);

	/* For PRBS23 Set bit DPHY_PRBS_SEL=1 and Set bit DPHY_PRBS_EN=1 */
	{
		REG_UPDATE_2(DP_DPHY_PRBS_CNTL,
					DPHY_PRBS_SEL, 1,
					DPHY_PRBS_EN, 1);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_prbs7(
	struct dce110_link_encoder *enc110)
{
	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);

	/* A PRBS7 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */

	disable_prbs_symbols(enc110, false);

	/* For PRBS7 Set bit DPHY_PRBS_SEL=0 and Set bit DPHY_PRBS_EN=1 */
	{
		REG_UPDATE_2(DP_DPHY_PRBS_CNTL,
					DPHY_PRBS_SEL, 0,
					DPHY_PRBS_EN, 1);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_80bit_custom(
	struct dce110_link_encoder *enc110,
	const uint8_t *pattern)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc110, false);

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(enc110, true);

	/* Enable PHY bypass mode to enable the test pattern */
	/* TODO is it really needed ? */

	enable_phy_bypass_mode(enc110, true);

	/* Program 80 bit custom pattern */
	{
		uint16_t pattern_symbols[8];

		pattern_symbols[0] =
			((pattern[1] & 0x03) << 8) | pattern[0];
		pattern_symbols[1] =
			((pattern[2] & 0x0f) << 6) | ((pattern[1] >> 2) & 0x3f);
		pattern_symbols[2] =
			((pattern[3] & 0x3f) << 4) | ((pattern[2] >> 4) & 0x0f);
		pattern_symbols[3] =
			(pattern[4] << 2) | ((pattern[3] >> 6) & 0x03);
		pattern_symbols[4] =
			((pattern[6] & 0x03) << 8) | pattern[5];
		pattern_symbols[5] =
			((pattern[7] & 0x0f) << 6) | ((pattern[6] >> 2) & 0x3f);
		pattern_symbols[6] =
			((pattern[8] & 0x3f) << 4) | ((pattern[7] >> 4) & 0x0f);
		pattern_symbols[7] =
			(pattern[9] << 2) | ((pattern[8] >> 6) & 0x03);

		program_pattern_symbols(enc110, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc110, true);
}

static void set_dp_phy_pattern_hbr2_compliance(
	struct dce110_link_encoder *enc110)
{

	/* previously there is a register DP_HBR2_EYE_PATTERN
	 * that is enabled to get the pattern.
	 * But it does not work with the latest spec change,
	 * so we are programming the following registers manually.
	 *
	 * The following settings have been confirmed
	 * by Nick Chorney and Sandra Liu */

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Setup DIG encoder in DP SST mode */

	enc110->base.funcs->setup(&enc110->base, SIGNAL_TYPE_DISPLAY_PORT);

	/* program correct panel mode*/
	{
		ASSERT(REG(DP_DPHY_INTERNAL_CTRL));

		REG_WRITE(DP_DPHY_INTERNAL_CTRL, 0x0);
	}

	/* no vbid after BS (SR)
	 * DP_LINK_FRAMING_CNTL changed history Sandra Liu
	 * 11000260 / 11000104 / 110000FC */

	/* TODO DP_LINK_FRAMING_CNTL should always use hardware default value
	 * output  except output hbr2_compliance pattern for physical PHY
	 * measurement. This is not normal usage case. SW should reset this
	 * register to hardware default value after end use of HBR2 eye
	 */
	BREAK_TO_DEBUGGER();
	/* TODO: do we still need this, find out at compliance test
	addr = mmDP_LINK_FRAMING_CNTL + fe_addr_offset;

	value = dal_read_reg(ctx, addr);

	set_reg_field_value(value, 0xFC,
			DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE);
	set_reg_field_value(value, 1,
			DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE);

	dal_write_reg(ctx, addr, value);
	 */
	/* swap every BS with SR */

	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, 0);

	/*TODO add support for this test pattern
	 * support_dp_hbr2_eye_pattern
	 */

	/* set link training complete */
	set_link_training_complete(enc110, true);
	/* do not enable video stream */

	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, 0);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);
}

static void set_dp_phy_pattern_passthrough_mode(
	struct dce110_link_encoder *enc110,
	enum dp_panel_mode panel_mode)
{
	uint32_t value;

	/* program correct panel mode */
	{
		ASSERT(REG(DP_DPHY_INTERNAL_CTRL));
		value = REG_READ(DP_DPHY_INTERNAL_CTRL);

		switch (panel_mode) {
		case DP_PANEL_MODE_EDP:
			value = 0x1;
		break;
		case DP_PANEL_MODE_SPECIAL:
			value = 0x11;
		break;
		default:
			value = 0x0;
			break;
		}

		REG_WRITE(DP_DPHY_INTERNAL_CTRL, value);
	}

	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, 0x1FF);

	/* set link training complete */

	set_link_training_complete(enc110, true);

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc110, false);

	/* Disable PRBS mode,
	 * make sure DPHY_PRBS_CNTL.DPHY_PRBS_EN=0 */

	disable_prbs_mode(enc110);
}

/* return value is bit-vector */
static uint8_t get_frontend_source(
	enum engine_id engine)
{
	switch (engine) {
	case ENGINE_ID_DIGA:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGA;
	case ENGINE_ID_DIGB:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGB;
	case ENGINE_ID_DIGC:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGC;
	case ENGINE_ID_DIGD:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGD;
	case ENGINE_ID_DIGE:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGE;
	case ENGINE_ID_DIGF:
		return DCE110_DIG_FE_SOURCE_SELECT_DIGF;
	default:
		ASSERT_CRITICAL(false);
		return DCE110_DIG_FE_SOURCE_SELECT_INVALID;
	}
}

static void configure_encoder(
	struct dce110_link_encoder *enc110,
	const struct dc_link_settings *link_settings)
{
	/* set number of lanes */

	REG_SET(DP_CONFIG, 0,
			DP_UDI_LANES, link_settings->lane_count - LANE_COUNT_ONE);

	/* setup scrambler */
	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_ADVANCE, 1);
}

static bool is_panel_powered_on(struct dce110_link_encoder *enc110)
{
	bool ret;
	uint32_t value;

	REG_GET(LVTMA_PWRSEQ_STATE, LVTMA_PWRSEQ_TARGET_STATE_R, &value);
	ret = value;

	return ret == 1;
}


/* TODO duplicate of dc_link.c version */
static struct gpio *get_hpd_gpio(const struct link_encoder *enc)
{
	enum bp_result bp_result;
	struct dc_bios *dcb = enc->ctx->dc_bios;
	struct graphics_object_hpd_info hpd_info;
	struct gpio_pin_info pin_info;

	if (dcb->funcs->get_hpd_info(dcb, enc->connector, &hpd_info) != BP_RESULT_OK)
		return NULL;

	bp_result = dcb->funcs->get_gpio_pin_info(dcb,
		hpd_info.hpd_int_gpio_uid, &pin_info);

	if (bp_result != BP_RESULT_OK) {
		ASSERT(bp_result == BP_RESULT_NORECORD);
		return NULL;
	}

	return dal_gpio_service_create_irq(
		enc->ctx->gpio_service,
		pin_info.offset,
		pin_info.mask);
}

/*
 * @brief
 * eDP only.
 */
static void link_encoder_edp_wait_for_hpd_ready(
	struct dce110_link_encoder *enc110,
	bool power_up)
{
	struct dc_context *ctx = enc110->base.ctx;
	struct graphics_object_id connector = enc110->base.connector;
	struct gpio *hpd;
	bool edp_hpd_high = false;
	uint32_t time_elapsed = 0;
	uint32_t timeout = power_up ?
		PANEL_POWER_UP_TIMEOUT : PANEL_POWER_DOWN_TIMEOUT;

	if (dal_graphics_object_id_get_connector_id(connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!power_up)
		/* from KV, we will not HPD low after turning off VCC -
		 * instead, we will check the SW timer in power_up(). */
		return;

	/* when we power on/off the eDP panel,
	 * we need to wait until SENSE bit is high/low */

	/* obtain HPD */
	/* TODO what to do with this? */
	hpd = get_hpd_gpio(&enc110->base);

	if (!hpd) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dal_gpio_open(hpd, GPIO_MODE_INTERRUPT);

	/* wait until timeout or panel detected */

	do {
		uint32_t detected = 0;

		dal_gpio_get_value(hpd, &detected);

		if (!(detected ^ power_up)) {
			edp_hpd_high = true;
			break;
		}

		msleep(HPD_CHECK_INTERVAL);

		time_elapsed += HPD_CHECK_INTERVAL;
	} while (time_elapsed < timeout);

	dal_gpio_close(hpd);

	dal_gpio_destroy_irq(&hpd);

	if (false == edp_hpd_high) {
		dm_logger_write(ctx->logger, LOG_ERROR,
				"%s: wait timed out!\n", __func__);
	}
}

/*
 * @brief
 * eDP only. Control the power of the eDP panel.
 */
void dce110_link_encoder_edp_power_control(
	struct link_encoder *enc,
	bool power_up)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result bp_result;

	if (dal_graphics_object_id_get_connector_id(enc110->base.connector) !=
		CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if ((power_up && !is_panel_powered_on(enc110)) ||
		(!power_up && is_panel_powered_on(enc110))) {

		/* Send VBIOS command to prompt eDP panel power */

		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));

		cntl.action = power_up ?
			TRANSMITTER_CONTROL_POWER_ON :
			TRANSMITTER_CONTROL_POWER_OFF;
		cntl.transmitter = enc110->base.transmitter;
		cntl.connector_obj_id = enc110->base.connector;
		cntl.coherent = false;
		cntl.lanes_number = LANE_COUNT_FOUR;
		cntl.hpd_sel = enc110->base.hpd_source;

		bp_result = link_transmitter_control(enc110, &cntl);

		if (BP_RESULT_OK != bp_result) {

			dm_logger_write(ctx->logger, LOG_ERROR,
					"%s: Panel Power bp_result: %d\n",
					__func__, bp_result);
		}
	} else {
		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: Skipping Panel Power action: %s\n",
				__func__, (power_up ? "On":"Off"));
	}

	link_encoder_edp_wait_for_hpd_ready(enc110, true);
}

static void aux_initialize(
	struct dce110_link_encoder *enc110)
{
	struct dc_context *ctx = enc110->base.ctx;
	enum hpd_source_id hpd_source = enc110->base.hpd_source;
	uint32_t addr = AUX_REG(AUX_CONTROL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, hpd_source, AUX_CONTROL, AUX_HPD_SEL);
	set_reg_field_value(value, 0, AUX_CONTROL, AUX_LS_READ_EN);
	dm_write_reg(ctx, addr, value);

	addr = AUX_REG(AUX_DPHY_RX_CONTROL0);
	value = dm_read_reg(ctx, addr);

	/* 1/4 window (the maximum allowed) */
	set_reg_field_value(value, 1,
			AUX_DPHY_RX_CONTROL0, AUX_RX_RECEIVE_WINDOW);
	dm_write_reg(ctx, addr, value);

}

/*todo: cloned in stream enc, fix*/
static bool is_panel_backlight_on(struct dce110_link_encoder *enc110)
{
	uint32_t value;

	REG_GET(LVTMA_PWRSEQ_CNTL, LVTMA_BLON, &value);

	return value;
}

/*todo: cloned in stream enc, fix*/
/*
 * @brief
 * eDP only. Control the backlight of the eDP panel
 */
void dce110_link_encoder_edp_backlight_control(
	struct link_encoder *enc,
	bool enable)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };

	if (dal_graphics_object_id_get_connector_id(enc110->base.connector)
		!= CONNECTOR_ID_EDP) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enable && is_panel_backlight_on(enc110)) {
		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: panel already powered up. Do nothing.\n",
				__func__);
		return;
	}

	if (!enable && !is_panel_powered_on(enc110)) {
		dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
				"%s: panel already powered down. Do nothing.\n",
				__func__);
		return;
	}

	/* Send VBIOS command to control eDP panel backlight */

	dm_logger_write(ctx->logger, LOG_HW_RESUME_S3,
			"%s: backlight action: %s\n",
			__func__, (enable ? "On":"Off"));

	cntl.action = enable ?
		TRANSMITTER_CONTROL_BACKLIGHT_ON :
		TRANSMITTER_CONTROL_BACKLIGHT_OFF;
	/*cntl.engine_id = ctx->engine;*/
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	/*todo: unhardcode*/
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.hpd_sel = enc110->base.hpd_source;

	/* For eDP, the following delays might need to be considered
	 * after link training completed:
	 * idle period - min. accounts for required BS-Idle pattern,
	 * max. allows for source frame synchronization);
	 * 50 msec max. delay from valid video data from source
	 * to video on dislpay or backlight enable.
	 *
	 * Disable the delay for now.
	 * Enable it in the future if necessary.
	 */
	/* dc_service_sleep_in_milliseconds(50); */
	link_transmitter_control(enc110, &cntl);
}

static bool is_dig_enabled(const struct dce110_link_encoder *enc110)
{
	uint32_t value;

	REG_GET(DIG_BE_EN_CNTL, DIG_ENABLE, &value);
	return value;
}

static void link_encoder_disable(struct dce110_link_encoder *enc110)
{
	/* reset training pattern */
	REG_SET(DP_DPHY_TRAINING_PATTERN_SEL, 0,
			DPHY_TRAINING_PATTERN_SEL, 0);

	/* reset training complete */
	REG_UPDATE(DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, 0);

	/* reset panel mode */
	ASSERT(REG(DP_DPHY_INTERNAL_CTRL));
	REG_WRITE(DP_DPHY_INTERNAL_CTRL, 0);
}

static void hpd_initialize(
	struct dce110_link_encoder *enc110)
{
	/* Associate HPD with DIG_BE */
	enum hpd_source_id hpd_source = enc110->base.hpd_source;

	REG_UPDATE(DIG_BE_CNTL, DIG_HPD_SELECT, hpd_source);
}

bool dce110_link_encoder_validate_dvi_output(
	const struct dce110_link_encoder *enc110,
	enum signal_type connector_signal,
	enum signal_type signal,
	const struct dc_crtc_timing *crtc_timing)
{
	uint32_t max_pixel_clock = TMDS_MAX_PIXEL_CLOCK;

	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		max_pixel_clock *= 2;

	/* This handles the case of HDMI downgrade to DVI we don't want to
	 * we don't want to cap the pixel clock if the DDI is not DVI.
	 */
	if (connector_signal != SIGNAL_TYPE_DVI_DUAL_LINK &&
			connector_signal != SIGNAL_TYPE_DVI_SINGLE_LINK)
		max_pixel_clock = enc110->base.features.max_hdmi_pixel_clock;

	/* DVI only support RGB pixel encoding */
	if (crtc_timing->pixel_encoding != PIXEL_ENCODING_RGB)
		return false;

	if (crtc_timing->pix_clk_khz < TMDS_MIN_PIXEL_CLOCK)
		return false;

	if (crtc_timing->pix_clk_khz > max_pixel_clock)
		return false;

	/* DVI supports 6/8bpp single-link and 10/16bpp dual-link */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
	case COLOR_DEPTH_888:
	break;
	case COLOR_DEPTH_101010:
	case COLOR_DEPTH_161616:
		if (signal != SIGNAL_TYPE_DVI_DUAL_LINK)
			return false;
	break;
	default:
		return false;
	}

	return true;
}

static bool dce110_link_encoder_validate_hdmi_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing,
	int adjusted_pix_clk_khz)
{
	enum dc_color_depth max_deep_color =
			enc110->base.features.max_hdmi_deep_color;

	if (max_deep_color < crtc_timing->display_color_depth)
		return false;

	if (adjusted_pix_clk_khz < TMDS_MIN_PIXEL_CLOCK)
		return false;

	if ((adjusted_pix_clk_khz == 0) ||
		(adjusted_pix_clk_khz > enc110->base.features.max_hdmi_pixel_clock))
		return false;

	/* DCE11 HW does not support 420 */
	if (!enc110->base.features.ycbcr420_supported &&
			crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		return false;

	return true;
}

bool dce110_link_encoder_validate_dp_output(
	const struct dce110_link_encoder *enc110,
	const struct dc_crtc_timing *crtc_timing)
{
	/* default RGB only */
	if (crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB)
		return true;

	if (enc110->base.features.flags.bits.IS_YCBCR_CAPABLE)
		return true;

	/* for DCE 8.x or later DP Y-only feature,
	 * we need ASIC cap + FeatureSupportDPYonly, not support 666 */
	if (crtc_timing->flags.Y_ONLY &&
		enc110->base.features.flags.bits.IS_YCBCR_CAPABLE &&
		crtc_timing->display_color_depth != COLOR_DEPTH_666)
		return true;

	return false;
}

bool dce110_link_encoder_construct(
	struct dce110_link_encoder *enc110,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dce110_link_enc_registers *link_regs,
	const struct dce110_link_enc_aux_registers *aux_regs,
	const struct dce110_link_enc_hpd_registers *hpd_regs)
{
	struct bp_encoder_cap_info bp_cap_info = {0};
	const struct dc_vbios_funcs *bp_funcs = init_data->ctx->dc_bios->funcs;

	enc110->base.funcs = &dce110_lnk_enc_funcs;
	enc110->base.ctx = init_data->ctx;
	enc110->base.id = init_data->encoder;

	enc110->base.hpd_source = init_data->hpd_source;
	enc110->base.connector = init_data->connector;

	enc110->base.preferred_engine = ENGINE_ID_UNKNOWN;

	enc110->base.features = *enc_features;

	enc110->base.transmitter = init_data->transmitter;

	/* set the flag to indicate whether driver poll the I2C data pin
	 * while doing the DP sink detect
	 */

/*	if (dal_adapter_service_is_feature_supported(as,
		FEATURE_DP_SINK_DETECT_POLL_DATA_PIN))
		enc110->base.features.flags.bits.
			DP_SINK_DETECT_POLL_DATA_PIN = true;*/

	enc110->base.output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	/* For DCE 8.0 and 8.1, by design, UNIPHY is hardwired to DIG_BE.
	 * SW always assign DIG_FE 1:1 mapped to DIG_FE for non-MST UNIPHY.
	 * SW assign DIG_FE to non-MST UNIPHY first and MST last. So prefer
	 * DIG is per UNIPHY and used by SST DP, eDP, HDMI, DVI and LVDS.
	 * Prefer DIG assignment is decided by board design.
	 * For DCE 8.0, there are only max 6 UNIPHYs, we assume board design
	 * and VBIOS will filter out 7 UNIPHY for DCE 8.0.
	 * By this, adding DIGG should not hurt DCE 8.0.
	 * This will let DCE 8.1 share DCE 8.0 as much as possible
	 */

	enc110->link_regs = link_regs;
	enc110->aux_regs = aux_regs;
	enc110->hpd_regs = hpd_regs;

	switch (enc110->base.transmitter) {
	case TRANSMITTER_UNIPHY_A:
		enc110->base.preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		enc110->base.preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		enc110->base.preferred_engine = ENGINE_ID_DIGC;
	break;
	case TRANSMITTER_UNIPHY_D:
		enc110->base.preferred_engine = ENGINE_ID_DIGD;
	break;
	case TRANSMITTER_UNIPHY_E:
		enc110->base.preferred_engine = ENGINE_ID_DIGE;
	break;
	case TRANSMITTER_UNIPHY_F:
		enc110->base.preferred_engine = ENGINE_ID_DIGF;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc110->base.preferred_engine = ENGINE_ID_UNKNOWN;
	}

	dm_logger_write(init_data->ctx->logger, LOG_I2C_AUX,
			"Using channel: %s [%d]\n",
			DECODE_CHANNEL_ID(init_data->channel),
			init_data->channel);

	/* Override features with DCE-specific values */
	if (BP_RESULT_OK == bp_funcs->get_encoder_cap_info(
			enc110->base.ctx->dc_bios, enc110->base.id,
			&bp_cap_info)) {
		enc110->base.features.flags.bits.IS_HBR2_CAPABLE =
				bp_cap_info.DP_HBR2_CAP;
		enc110->base.features.flags.bits.IS_HBR3_CAPABLE =
				bp_cap_info.DP_HBR3_EN;
	}

	return true;
}

bool dce110_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	struct pipe_ctx *pipe_ctx)
{
	struct core_stream *stream = pipe_ctx->stream;
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	bool is_valid;

	switch (pipe_ctx->stream->signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		is_valid = dce110_link_encoder_validate_dvi_output(
			enc110,
			stream->sink->link->public.connector_signal,
			pipe_ctx->stream->signal,
			&stream->public.timing);
	break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		is_valid = dce110_link_encoder_validate_hdmi_output(
				enc110,
				&stream->public.timing,
				stream->phy_pix_clk);
	break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
	case SIGNAL_TYPE_EDP:
		is_valid = dce110_link_encoder_validate_dp_output(
			enc110, &stream->public.timing);
	break;
	case SIGNAL_TYPE_VIRTUAL:
		is_valid = true;
		break;
	default:
		is_valid = false;
	break;
	}

	return is_valid;
}

void dce110_link_encoder_hw_init(
	struct link_encoder *enc)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	cntl.action = TRANSMITTER_CONTROL_INIT;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.coherent = false;
	cntl.hpd_sel = enc110->base.hpd_source;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dm_logger_write(ctx->logger, LOG_ERROR,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enc110->base.connector.id == CONNECTOR_ID_LVDS) {
		cntl.action = TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS;

		result = link_transmitter_control(enc110, &cntl);

		ASSERT(result == BP_RESULT_OK);

	} else if (enc110->base.connector.id == CONNECTOR_ID_EDP) {
		enc->funcs->power_control(&enc110->base, true);
	}
	aux_initialize(enc110);

	/* reinitialize HPD.
	 * hpd_initialize() will pass DIG_FE id to HW context.
	 * All other routine within HW context will use fe_engine_offset
	 * as DIG_FE id even caller pass DIG_FE id.
	 * So this routine must be called first. */
	hpd_initialize(enc110);
}

void dce110_link_encoder_destroy(struct link_encoder **enc)
{
	dm_free(TO_DCE110_LINK_ENC(*enc));
	*enc = NULL;
}

void dce110_link_encoder_setup(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);

	switch (signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		/* DP SST */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 0);
		break;
	case SIGNAL_TYPE_LVDS:
		/* LVDS */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 1);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		/* TMDS-DVI */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 2);
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* TMDS-HDMI */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 3);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* DP MST */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 5);
		break;
	default:
		ASSERT_CRITICAL(false);
		/* invalid mode ! */
		break;
	}

}

/* TODO: still need depth or just pass in adjusted pixel clock? */
void dce110_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	bool hdmi,
	bool dual_link,
	uint32_t pixel_clock)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = enc->preferred_engine;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	if (hdmi) {
		cntl.signal = SIGNAL_TYPE_HDMI_TYPE_A;
		cntl.lanes_number = 4;
	} else if (dual_link) {
		cntl.signal = SIGNAL_TYPE_DVI_DUAL_LINK;
		cntl.lanes_number = 8;
	} else {
		cntl.signal = SIGNAL_TYPE_DVI_SINGLE_LINK;
		cntl.lanes_number = 4;
	}
	cntl.hpd_sel = enc110->base.hpd_source;

	cntl.pixel_clock = pixel_clock;
	cntl.color_depth = color_depth;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dm_logger_write(ctx->logger, LOG_ERROR,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

/* enables DP PHY output */
void dce110_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	configure_encoder(enc110, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = enc->preferred_engine;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dm_logger_write(ctx->logger, LOG_ERROR,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

/* enables DP PHY output in MST mode */
void dce110_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	configure_encoder(enc110, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc110->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dm_logger_write(ctx->logger, LOG_ERROR,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}
/*
 * @brief
 * Disable transmitter and its encoder
 */
void dce110_link_encoder_disable_output(
	struct link_encoder *enc,
	enum signal_type signal)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	if (!is_dig_enabled(enc110)) {
		/* OF_SKIP_POWER_DOWN_INACTIVE_ENCODER */
		return;
	}
	/* Power-down RX and disable GPU PHY should be paired.
	 * Disabling PHY without powering down RX may cause
	 * symbol lock loss, on which we will get DP Sink interrupt. */

	/* There is a case for the DP active dongles
	 * where we want to disable the PHY but keep RX powered,
	 * for those we need to ignore DP Sink interrupt
	 * by checking lane count that has been set
	 * on the last do_enable_output(). */

	/* disable transmitter */
	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.transmitter = enc110->base.transmitter;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.signal = signal;
	cntl.connector_obj_id = enc110->base.connector;

	result = link_transmitter_control(enc110, &cntl);

	if (result != BP_RESULT_OK) {
		dm_logger_write(ctx->logger, LOG_ERROR,
			"%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	/* disable encoder */
	if (dc_is_dp_signal(signal))
		link_encoder_disable(enc110);

	if (enc110->base.connector.id == CONNECTOR_ID_EDP) {
		/* power down eDP panel */
		/* TODO: Power control cause regression, we should implement
		 * it properly, for now just comment it.
		 *
		 * link_encoder_edp_wait_for_hpd_ready(
			link_enc,
			link_enc->connector,
			false);

		 * link_encoder_edp_power_control(
				link_enc, false); */
	}
}

void dce110_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct link_training_settings *link_settings)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	union dpcd_training_lane_set training_lane_set = { { 0 } };
	int32_t lane = 0;
	struct bp_transmitter_control cntl = { 0 };

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return;
	}

	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.transmitter = enc110->base.transmitter;
	cntl.connector_obj_id = enc110->base.connector;
	cntl.lanes_number = link_settings->link_settings.lane_count;
	cntl.hpd_sel = enc110->base.hpd_source;
	cntl.pixel_clock = link_settings->link_settings.link_rate *
						LINK_RATE_REF_FREQ_IN_KHZ;

	for (lane = 0; lane < link_settings->link_settings.lane_count; lane++) {
		/* translate lane settings */

		training_lane_set.bits.VOLTAGE_SWING_SET =
			link_settings->lane_settings[lane].VOLTAGE_SWING;
		training_lane_set.bits.PRE_EMPHASIS_SET =
			link_settings->lane_settings[lane].PRE_EMPHASIS;

		/* post cursor 2 setting only applies to HBR2 link rate */
		if (link_settings->link_settings.link_rate == LINK_RATE_HIGH2) {
			/* this is passed to VBIOS
			 * to program post cursor 2 level */

			training_lane_set.bits.POST_CURSOR2_SET =
				link_settings->lane_settings[lane].POST_CURSOR2;
		}

		cntl.lane_select = lane;
		cntl.lane_settings = training_lane_set.raw;

		/* call VBIOS table to set voltage swing and pre-emphasis */
		link_transmitter_control(enc110, &cntl);
	}
}

/* set DP PHY test and training patterns */
void dce110_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);

	switch (param->dp_phy_pattern) {
	case DP_TEST_PATTERN_TRAINING_PATTERN1:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 0);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN2:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 1);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN3:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 2);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN4:
		dce110_link_encoder_set_dp_phy_pattern_training_pattern(enc, 3);
		break;
	case DP_TEST_PATTERN_D102:
		set_dp_phy_pattern_d102(enc110);
		break;
	case DP_TEST_PATTERN_SYMBOL_ERROR:
		set_dp_phy_pattern_symbol_error(enc110);
		break;
	case DP_TEST_PATTERN_PRBS7:
		set_dp_phy_pattern_prbs7(enc110);
		break;
	case DP_TEST_PATTERN_80BIT_CUSTOM:
		set_dp_phy_pattern_80bit_custom(
			enc110, param->custom_pattern);
		break;
	case DP_TEST_PATTERN_HBR2_COMPLIANCE_EYE:
		set_dp_phy_pattern_hbr2_compliance(enc110);
		break;
	case DP_TEST_PATTERN_VIDEO_MODE: {
		set_dp_phy_pattern_passthrough_mode(
			enc110, param->dp_panel_mode);
		break;
	}

	default:
		/* invalid phy pattern */
		ASSERT_CRITICAL(false);
		break;
	}
}

static void fill_stream_allocation_row_info(
	const struct link_mst_stream_allocation *stream_allocation,
	uint32_t *src,
	uint32_t *slots)
{
	const struct stream_encoder *stream_enc = stream_allocation->stream_enc;

	if (stream_enc) {
		*src = stream_enc->id;
		*slots = stream_allocation->slot_count;
	} else {
		*src = 0;
		*slots = 0;
	}
}

/* programs DP MST VC payload allocation */
void dce110_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	uint32_t value0 = 0;
	uint32_t value1 = 0;
	uint32_t value2 = 0;
	uint32_t slots = 0;
	uint32_t src = 0;
	uint32_t retries = 0;

	/* For CZ, there are only 3 pipes. So Virtual channel is up 3.*/

	/* --- Set MSE Stream Attribute -
	 * Setup VC Payload Table on Tx Side,
	 * Issue allocation change trigger
	 * to commit payload on both tx and rx side */

	/* we should clean-up table each time */

	if (table->stream_count >= 1) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[0],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT0,
			DP_MSE_SAT_SRC0, src,
			DP_MSE_SAT_SLOT_COUNT0, slots);

	if (table->stream_count >= 2) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[1],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT0,
			DP_MSE_SAT_SRC1, src,
			DP_MSE_SAT_SLOT_COUNT1, slots);

	if (table->stream_count >= 3) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[2],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT1,
			DP_MSE_SAT_SRC2, src,
			DP_MSE_SAT_SLOT_COUNT2, slots);

	if (table->stream_count >= 4) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[3],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT1,
			DP_MSE_SAT_SRC3, src,
			DP_MSE_SAT_SLOT_COUNT3, slots);

	/* --- wait for transaction finish */

	/* send allocation change trigger (ACT) ?
	 * this step first sends the ACT,
	 * then double buffers the SAT into the hardware
	 * making the new allocation active on the DP MST mode link */


	/* DP_MSE_SAT_UPDATE:
	 * 0 - No Action
	 * 1 - Update SAT with trigger
	 * 2 - Update SAT without trigger */

	REG_UPDATE(DP_MSE_SAT_UPDATE,
			DP_MSE_SAT_UPDATE, 1);

	/* wait for update to complete
	 * (i.e. DP_MSE_SAT_UPDATE field is reset to 0)
	 * then wait for the transmission
	 * of at least 16 MTP headers on immediate local link.
	 * i.e. DP_MSE_16_MTP_KEEPOUT field (read only) is reset to 0
	 * a value of 1 indicates that DP MST mode
	 * is in the 16 MTP keepout region after a VC has been added.
	 * MST stream bandwidth (VC rate) can be configured
	 * after this bit is cleared */

	do {
		udelay(10);

		value0 = REG_READ(DP_MSE_SAT_UPDATE);

		REG_GET(DP_MSE_SAT_UPDATE,
				DP_MSE_SAT_UPDATE, &value1);

		REG_GET(DP_MSE_SAT_UPDATE,
				DP_MSE_16_MTP_KEEPOUT, &value2);

		/* bit field DP_MSE_SAT_UPDATE is set to 1 already */
		if (!value1 && !value2)
			break;
		++retries;
	} while (retries < DP_MST_UPDATE_MAX_RETRY);
}

static void get_dmcu_psr_state(struct link_encoder *enc, uint32_t *psr_state)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;

	uint32_t count = 0;
	uint32_t psrStateOffset = 0xf0;
	uint32_t value;

	/* Enable write access to IRAM */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 1);

	do {
		dm_delay_in_microseconds(ctx, 2);
		REG_GET(DCI_MEM_PWR_STATUS, DMCU_IRAM_MEM_PWR_STATE, &value);
	} while (value != 0 && count++ < 10);

	/* Write address to IRAM_RD_ADDR in DMCU_IRAM_RD_CTRL */
	REG_WRITE(DMCU_IRAM_RD_CTRL, psrStateOffset);

	/* Read data from IRAM_RD_DATA in DMCU_IRAM_RD_DATA*/
	*psr_state = REG_READ(DMCU_IRAM_RD_DATA);

	/* Disable write access to IRAM after finished using IRAM
	 * in order to allow dynamic sleep state
	 */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 0);
}

void dce110_link_encoder_set_dmcu_psr_enable(struct link_encoder *enc,
								bool enable)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	unsigned int regValue;

	unsigned int retryCount;
	uint32_t psr_state = 0;

	/* waitDMCUReadyForCmd */
	do {
		dm_delay_in_microseconds(ctx, dmcu_wait_reg_ready_interval);
		regValue = REG_READ(MASTER_COMM_CNTL_REG);
		dmcu_max_retry_on_wait_reg_ready--;
	} while
	/* expected value is 0, loop while not 0*/
	((MASTER_COMM_CNTL_REG__MASTER_COMM_INTERRUPT_MASK & regValue) &&
		dmcu_max_retry_on_wait_reg_ready > 0);

	/* setDMCUParam_Cmd */
	if (enable)
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_ENABLE);
	else
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_EXIT);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	for (retryCount = 0; retryCount <= 100; retryCount++) {
		get_dmcu_psr_state(enc, &psr_state);
		if (enable) {
			if (psr_state != 0)
				break;
		} else {
			if (psr_state == 0)
				break;
		}
		dm_delay_in_microseconds(ctx, 10);
	}
}

void dce110_link_encoder_setup_dmcu_psr(struct link_encoder *enc,
			struct psr_dmcu_context *psr_context)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;
	unsigned int regValue;

	union dce110_dmcu_psr_config_data_reg1 masterCmdData1;
	union dce110_dmcu_psr_config_data_reg2 masterCmdData2;
	union dce110_dmcu_psr_config_data_reg3 masterCmdData3;

	if (psr_context->psrExitLinkTrainingRequired)
		REG_UPDATE(DP_DPHY_FAST_TRAINING, DPHY_RX_FAST_TRAINING_CAPABLE, 1);
	else {
		REG_UPDATE(DP_DPHY_FAST_TRAINING, DPHY_RX_FAST_TRAINING_CAPABLE, 0);
		/*In DCE 11, we are able to pre-program a Force SR register
		 * to be able to trigger SR symbol after 5 idle patterns
		 * transmitted. Upon PSR Exit, DMCU can trigger
		 * DPHY_LOAD_BS_COUNT_START = 1. Upon writing 1 to
		 * DPHY_LOAD_BS_COUNT_START and the internal counter
		 * reaches DPHY_LOAD_BS_COUNT, the next BS symbol will be
		 * replaced by SR symbol once.
		 */

		REG_UPDATE(DP_DPHY_BS_SR_SWAP_CNTL, DPHY_LOAD_BS_COUNT, 0x5);
	}

	/* Enable static screen interrupts for PSR supported display */
	/* Disable the interrupt coming from other displays. */
	REG_UPDATE_4(DMCU_INTERRUPT_TO_UC_EN_MASK,
			STATIC_SCREEN1_INT_TO_UC_EN, 0,
			STATIC_SCREEN2_INT_TO_UC_EN, 0,
			STATIC_SCREEN3_INT_TO_UC_EN, 0,
			STATIC_SCREEN4_INT_TO_UC_EN, 0);

	switch (psr_context->controllerId) {
	/* Driver uses case 1 for unconfigured */
	case 1:
		psr_crtc_offset = mmCRTC0_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;

		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	case 2:
		psr_crtc_offset = mmCRTC1_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;

		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN2_INT_TO_UC_EN, 1);
		break;
	case 3:
		psr_crtc_offset = mmCRTC2_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;

		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN3_INT_TO_UC_EN, 1);
		break;
	case 4:
		psr_crtc_offset = mmCRTC3_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;

		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN4_INT_TO_UC_EN, 1);
		break;
	case 5:
		psr_crtc_offset = mmCRTC4_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;
		/* CZ/NL only has 4 CRTC!!
		 * really valid.
		 * There is no interrupt enable mask for these instances.
		 */
		break;
	case 6:
		psr_crtc_offset = mmCRTC5_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;
		/* CZ/NL only has 4 CRTC!!
		 * These are here because they are defined in HW regspec,
		 * but not really valid. There is no interrupt enable mask
		 * for these instances.
		 */
		break;
	default:
		psr_crtc_offset = mmCRTC0_CRTC_STATIC_SCREEN_CONTROL -
				mmCRTC0_CRTC_STATIC_SCREEN_CONTROL;

		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	}

	REG_UPDATE_2(DP_SEC_CNTL1,
		DP_SEC_GSP0_LINE_NUM, psr_context->sdpTransmitLineNumDeadline,
		DP_SEC_GSP0_PRIORITY, 1);

	if (psr_context->psr_level.bits.SKIP_SMU_NOTIFICATION) {
		REG_UPDATE(SMU_INTERRUPT_CONTROL, DC_SMU_INT_ENABLE, 1);
	}

	/* waitDMCUReadyForCmd */
	do {
		dm_delay_in_microseconds(ctx, dmcu_wait_reg_ready_interval);
		regValue = REG_READ(MASTER_COMM_CNTL_REG);
		dmcu_max_retry_on_wait_reg_ready--;
	} while
	/* expected value is 0, loop while not 0*/
	((MASTER_COMM_CNTL_REG__MASTER_COMM_INTERRUPT_MASK & regValue) &&
		dmcu_max_retry_on_wait_reg_ready > 0);

	/* setDMCUParam_PSRHostConfigData */
	masterCmdData1.u32All = 0;
	masterCmdData1.bits.timehyst_frames = psr_context->timehyst_frames;
	masterCmdData1.bits.hyst_lines = psr_context->hyst_lines;
	masterCmdData1.bits.rfb_update_auto_en =
			psr_context->rfb_update_auto_en;
	masterCmdData1.bits.dp_port_num = psr_context->transmitterId;
	masterCmdData1.bits.dcp_sel = psr_context->controllerId;
	masterCmdData1.bits.phy_type  = psr_context->phyType;
	masterCmdData1.bits.frame_cap_ind =
			psr_context->psrFrameCaptureIndicationReq;
	masterCmdData1.bits.aux_chan = psr_context->channel;
	masterCmdData1.bits.aux_repeat = psr_context->aux_repeats;
	dm_write_reg(ctx, REG(MASTER_COMM_DATA_REG1),
					masterCmdData1.u32All);

	masterCmdData2.u32All = 0;
	masterCmdData2.bits.dig_fe = psr_context->engineId;
	masterCmdData2.bits.dig_be = psr_context->transmitterId;
	masterCmdData2.bits.skip_wait_for_pll_lock =
			psr_context->skipPsrWaitForPllLock;
	masterCmdData2.bits.frame_delay = psr_context->frame_delay;
	masterCmdData2.bits.smu_phy_id = psr_context->smuPhyId;
	masterCmdData2.bits.num_of_controllers =
			psr_context->numberOfControllers;
	dm_write_reg(ctx, REG(MASTER_COMM_DATA_REG2),
			masterCmdData2.u32All);

	masterCmdData3.u32All = 0;
	masterCmdData3.bits.psr_level = psr_context->psr_level.u32all;
	dm_write_reg(ctx, REG(MASTER_COMM_DATA_REG3),
			masterCmdData3.u32All);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, PSR_SET);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

void dce110_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	uint32_t field;

	if (engine != ENGINE_ID_UNKNOWN) {

		REG_GET(DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, &field);

		if (connect)
			field |= get_frontend_source(engine);
		else
			field &= ~get_frontend_source(engine);

		REG_UPDATE(DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, field);
	}
}

void dce110_link_encoder_enable_hpd(struct link_encoder *enc)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = HPD_REG(DC_HPD_CONTROL);
	uint32_t hpd_enable = 0;
	uint32_t value = dm_read_reg(ctx, addr);

	get_reg_field_value(hpd_enable, DC_HPD_CONTROL, DC_HPD_EN);

	if (hpd_enable == 0)
		set_reg_field_value(value, 1, DC_HPD_CONTROL, DC_HPD_EN);
}

void dce110_link_encoder_disable_hpd(struct link_encoder *enc)
{
	struct dce110_link_encoder *enc110 = TO_DCE110_LINK_ENC(enc);
	struct dc_context *ctx = enc110->base.ctx;
	uint32_t addr = HPD_REG(DC_HPD_CONTROL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(value, 0, DC_HPD_CONTROL, DC_HPD_EN);
}
