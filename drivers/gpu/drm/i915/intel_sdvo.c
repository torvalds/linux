/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2007 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "intel_drv.h"
#include "drm_edid.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_sdvo_regs.h"
#include <linux/dmi.h>

static char *tv_format_names[] = {
	"NTSC_M"   , "NTSC_J"  , "NTSC_443",
	"PAL_B"    , "PAL_D"   , "PAL_G"   ,
	"PAL_H"    , "PAL_I"   , "PAL_M"   ,
	"PAL_N"    , "PAL_NC"  , "PAL_60"  ,
	"SECAM_B"  , "SECAM_D" , "SECAM_G" ,
	"SECAM_K"  , "SECAM_K1", "SECAM_L" ,
	"SECAM_60"
};

#define TV_FORMAT_NUM  (sizeof(tv_format_names) / sizeof(*tv_format_names))

struct intel_sdvo_priv {
	u8 slave_addr;

	/* Register for the SDVO device: SDVOB or SDVOC */
	int sdvo_reg;

	/* Active outputs controlled by this SDVO output */
	uint16_t controlled_output;

	/*
	 * Capabilities of the SDVO device returned by
	 * i830_sdvo_get_capabilities()
	 */
	struct intel_sdvo_caps caps;

	/* Pixel clock limitations reported by the SDVO device, in kHz */
	int pixel_clock_min, pixel_clock_max;

	/*
	* For multiple function SDVO device,
	* this is for current attached outputs.
	*/
	uint16_t attached_output;

	/**
	 * This is set if we're going to treat the device as TV-out.
	 *
	 * While we have these nice friendly flags for output types that ought
	 * to decide this for us, the S-Video output on our HDMI+S-Video card
	 * shows up as RGB1 (VGA).
	 */
	bool is_tv;

	/* This is for current tv format name */
	char *tv_format_name;

	/* This contains all current supported TV format */
	char *tv_format_supported[TV_FORMAT_NUM];
	int   format_supported_num;
	struct drm_property *tv_format_property;
	struct drm_property *tv_format_name_property[TV_FORMAT_NUM];

	/**
	 * This is set if we treat the device as HDMI, instead of DVI.
	 */
	bool is_hdmi;

	/**
	 * This is set if we detect output of sdvo device as LVDS.
	 */
	bool is_lvds;

	/**
	 * This is sdvo flags for input timing.
	 */
	uint8_t sdvo_flags;

	/**
	 * This is sdvo fixed pannel mode pointer
	 */
	struct drm_display_mode *sdvo_lvds_fixed_mode;

	/**
	 * Returned SDTV resolutions allowed for the current format, if the
	 * device reported it.
	 */
	struct intel_sdvo_sdtv_resolution_reply sdtv_resolutions;

	/*
	 * supported encoding mode, used to determine whether HDMI is
	 * supported
	 */
	struct intel_sdvo_encode encode;

	/* DDC bus used by this SDVO encoder */
	uint8_t ddc_bus;

	/* Mac mini hack -- use the same DDC as the analog connector */
	struct i2c_adapter *analog_ddc_bus;

	int save_sdvo_mult;
	u16 save_active_outputs;
	struct intel_sdvo_dtd save_input_dtd_1, save_input_dtd_2;
	struct intel_sdvo_dtd save_output_dtd[16];
	u32 save_SDVOX;
	/* add the property for the SDVO-TV */
	struct drm_property *left_property;
	struct drm_property *right_property;
	struct drm_property *top_property;
	struct drm_property *bottom_property;
	struct drm_property *hpos_property;
	struct drm_property *vpos_property;

	/* add the property for the SDVO-TV/LVDS */
	struct drm_property *brightness_property;
	struct drm_property *contrast_property;
	struct drm_property *saturation_property;
	struct drm_property *hue_property;

	/* Add variable to record current setting for the above property */
	u32	left_margin, right_margin, top_margin, bottom_margin;
	/* this is to get the range of margin.*/
	u32	max_hscan,  max_vscan;
	u32	max_hpos, cur_hpos;
	u32	max_vpos, cur_vpos;
	u32	cur_brightness, max_brightness;
	u32	cur_contrast,	max_contrast;
	u32	cur_saturation, max_saturation;
	u32	cur_hue,	max_hue;
};

static bool
intel_sdvo_output_setup(struct intel_encoder *intel_encoder, uint16_t flags);

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
static void intel_sdvo_write_sdvox(struct intel_encoder *intel_encoder, u32 val)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_sdvo_priv   *sdvo_priv = intel_encoder->dev_priv;
	u32 bval = val, cval = val;
	int i;

	if (sdvo_priv->sdvo_reg == SDVOB) {
		cval = I915_READ(SDVOC);
	} else {
		bval = I915_READ(SDVOB);
	}
	/*
	 * Write the registers twice for luck. Sometimes,
	 * writing them only once doesn't appear to 'stick'.
	 * The BIOS does this too. Yay, magic
	 */
	for (i = 0; i < 2; i++)
	{
		I915_WRITE(SDVOB, bval);
		I915_READ(SDVOB);
		I915_WRITE(SDVOC, cval);
		I915_READ(SDVOC);
	}
}

static bool intel_sdvo_read_byte(struct intel_encoder *intel_encoder, u8 addr,
				 u8 *ch)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	u8 out_buf[2];
	u8 buf[2];
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if ((ret = i2c_transfer(intel_encoder->i2c_bus, msgs, 2)) == 2)
	{
		*ch = buf[0];
		return true;
	}

	DRM_DEBUG_KMS("i2c transfer returned %d\n", ret);
	return false;
}

static bool intel_sdvo_write_byte(struct intel_encoder *intel_encoder, int addr,
				  u8 ch)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	u8 out_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = 0,
			.len = 2,
			.buf = out_buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(intel_encoder->i2c_bus, msgs, 1) == 1)
	{
		return true;
	}
	return false;
}

#define SDVO_CMD_NAME_ENTRY(cmd) {cmd, #cmd}
/** Mapping of command numbers to names, for debug output */
static const struct _sdvo_cmd_name {
	u8 cmd;
	char *name;
} sdvo_cmd_names[] = {
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_RESET),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_DEVICE_CAPS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_FIRMWARE_REV),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TRAINED_INPUTS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ACTIVE_OUTPUTS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ACTIVE_OUTPUTS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_IN_OUT_MAP),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_IN_OUT_MAP),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ATTACHED_DISPLAYS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HOT_PLUG_SUPPORT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ACTIVE_HOT_PLUG),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ACTIVE_HOT_PLUG),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INTERRUPT_EVENT_SOURCE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TARGET_INPUT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TARGET_OUTPUT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_TIMINGS_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_TIMINGS_PART2),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART2),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_INPUT_TIMINGS_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OUTPUT_TIMINGS_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OUTPUT_TIMINGS_PART2),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_TIMINGS_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_TIMINGS_PART2),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_CLOCK_RATE_MULTS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_CLOCK_RATE_MULT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CLOCK_RATE_MULT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_TV_FORMATS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TV_FORMAT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_FORMAT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_POWER_STATES),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_POWER_STATE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ENCODER_POWER_STATE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_DISPLAY_POWER_STATE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CONTROL_BUS_SWITCH),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SDTV_RESOLUTION_SUPPORT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SCALED_HDTV_RESOLUTION_SUPPORT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS),
    /* Add the op code for SDVO enhancements */
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_POSITION_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_POSITION_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_POSITION_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_POSITION_V),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_POSITION_V),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_POSITION_V),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_SATURATION),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SATURATION),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_SATURATION),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_HUE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HUE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HUE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_CONTRAST),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_CONTRAST),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CONTRAST),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_BRIGHTNESS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_BRIGHTNESS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_BRIGHTNESS),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_OVERSCAN_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OVERSCAN_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OVERSCAN_H),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_OVERSCAN_V),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_OVERSCAN_V),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_OVERSCAN_V),
    /* HDMI op code */
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPP_ENCODE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_ENCODE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_ENCODE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_PIXEL_REPLI),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_PIXEL_REPLI),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_COLORIMETRY_CAP),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_COLORIMETRY),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_COLORIMETRY),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_AUDIO_ENCRYPT_PREFER),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_AUDIO_STAT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_AUDIO_STAT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_INDEX),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_INDEX),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_INFO),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_AV_SPLIT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_AV_SPLIT),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_TXRATE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_TXRATE),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HBUF_DATA),
    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HBUF_DATA),
};

#define SDVO_NAME(dev_priv) ((dev_priv)->sdvo_reg == SDVOB ? "SDVOB" : "SDVOC")
#define SDVO_PRIV(encoder)   ((struct intel_sdvo_priv *) (encoder)->dev_priv)

static void intel_sdvo_debug_write(struct intel_encoder *intel_encoder, u8 cmd,
				   void *args, int args_len)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int i;

	DRM_DEBUG_KMS("%s: W: %02X ",
				SDVO_NAME(sdvo_priv), cmd);
	for (i = 0; i < args_len; i++)
		DRM_LOG_KMS("%02X ", ((u8 *)args)[i]);
	for (; i < 8; i++)
		DRM_LOG_KMS("   ");
	for (i = 0; i < sizeof(sdvo_cmd_names) / sizeof(sdvo_cmd_names[0]); i++) {
		if (cmd == sdvo_cmd_names[i].cmd) {
			DRM_LOG_KMS("(%s)", sdvo_cmd_names[i].name);
			break;
		}
	}
	if (i == sizeof(sdvo_cmd_names)/ sizeof(sdvo_cmd_names[0]))
		DRM_LOG_KMS("(%02X)", cmd);
	DRM_LOG_KMS("\n");
}

static void intel_sdvo_write_cmd(struct intel_encoder *intel_encoder, u8 cmd,
				 void *args, int args_len)
{
	int i;

	intel_sdvo_debug_write(intel_encoder, cmd, args, args_len);

	for (i = 0; i < args_len; i++) {
		intel_sdvo_write_byte(intel_encoder, SDVO_I2C_ARG_0 - i,
				      ((u8*)args)[i]);
	}

	intel_sdvo_write_byte(intel_encoder, SDVO_I2C_OPCODE, cmd);
}

static const char *cmd_status_names[] = {
	"Power on",
	"Success",
	"Not supported",
	"Invalid arg",
	"Pending",
	"Target not specified",
	"Scaling not supported"
};

static void intel_sdvo_debug_response(struct intel_encoder *intel_encoder,
				      void *response, int response_len,
				      u8 status)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int i;

	DRM_DEBUG_KMS("%s: R: ", SDVO_NAME(sdvo_priv));
	for (i = 0; i < response_len; i++)
		DRM_LOG_KMS("%02X ", ((u8 *)response)[i]);
	for (; i < 8; i++)
		DRM_LOG_KMS("   ");
	if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
		DRM_LOG_KMS("(%s)", cmd_status_names[status]);
	else
		DRM_LOG_KMS("(??? %d)", status);
	DRM_LOG_KMS("\n");
}

static u8 intel_sdvo_read_response(struct intel_encoder *intel_encoder,
				   void *response, int response_len)
{
	int i;
	u8 status;
	u8 retry = 50;

	while (retry--) {
		/* Read the command response */
		for (i = 0; i < response_len; i++) {
			intel_sdvo_read_byte(intel_encoder,
					     SDVO_I2C_RETURN_0 + i,
					     &((u8 *)response)[i]);
		}

		/* read the return status */
		intel_sdvo_read_byte(intel_encoder, SDVO_I2C_CMD_STATUS,
				     &status);

		intel_sdvo_debug_response(intel_encoder, response, response_len,
					  status);
		if (status != SDVO_CMD_STATUS_PENDING)
			return status;

		mdelay(50);
	}

	return status;
}

static int intel_sdvo_get_pixel_multiplier(struct drm_display_mode *mode)
{
	if (mode->clock >= 100000)
		return 1;
	else if (mode->clock >= 50000)
		return 2;
	else
		return 4;
}

/**
 * Try to read the response after issuie the DDC switch command. But it
 * is noted that we must do the action of reading response and issuing DDC
 * switch command in one I2C transaction. Otherwise when we try to start
 * another I2C transaction after issuing the DDC bus switch, it will be
 * switched to the internal SDVO register.
 */
static void intel_sdvo_set_control_bus_switch(struct intel_encoder *intel_encoder,
					      u8 target)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	u8 out_buf[2], cmd_buf[2], ret_value[2], ret;
	struct i2c_msg msgs[] = {
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = 0,
			.len = 2,
			.buf = out_buf,
		},
		/* the following two are to read the response */
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = 0,
			.len = 1,
			.buf = cmd_buf,
		},
		{
			.addr = sdvo_priv->slave_addr >> 1,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = ret_value,
		},
	};

	intel_sdvo_debug_write(intel_encoder, SDVO_CMD_SET_CONTROL_BUS_SWITCH,
					&target, 1);
	/* write the DDC switch command argument */
	intel_sdvo_write_byte(intel_encoder, SDVO_I2C_ARG_0, target);

	out_buf[0] = SDVO_I2C_OPCODE;
	out_buf[1] = SDVO_CMD_SET_CONTROL_BUS_SWITCH;
	cmd_buf[0] = SDVO_I2C_CMD_STATUS;
	cmd_buf[1] = 0;
	ret_value[0] = 0;
	ret_value[1] = 0;

	ret = i2c_transfer(intel_encoder->i2c_bus, msgs, 3);
	if (ret != 3) {
		/* failure in I2C transfer */
		DRM_DEBUG_KMS("I2c transfer returned %d\n", ret);
		return;
	}
	if (ret_value[0] != SDVO_CMD_STATUS_SUCCESS) {
		DRM_DEBUG_KMS("DDC switch command returns response %d\n",
					ret_value[0]);
		return;
	}
	return;
}

static bool intel_sdvo_set_target_input(struct intel_encoder *intel_encoder, bool target_0, bool target_1)
{
	struct intel_sdvo_set_target_input_args targets = {0};
	u8 status;

	if (target_0 && target_1)
		return SDVO_CMD_STATUS_NOTSUPP;

	if (target_1)
		targets.target_1 = 1;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_TARGET_INPUT, &targets,
			     sizeof(targets));

	status = intel_sdvo_read_response(intel_encoder, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static bool intel_sdvo_get_trained_inputs(struct intel_encoder *intel_encoder, bool *input_1, bool *input_2)
{
	struct intel_sdvo_get_trained_inputs_response response;
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_TRAINED_INPUTS, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &response, sizeof(response));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	*input_1 = response.input0_trained;
	*input_2 = response.input1_trained;
	return true;
}

static bool intel_sdvo_get_active_outputs(struct intel_encoder *intel_encoder,
					  u16 *outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_ACTIVE_OUTPUTS, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, outputs, sizeof(*outputs));

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_active_outputs(struct intel_encoder *intel_encoder,
					  u16 outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_ACTIVE_OUTPUTS, &outputs,
			     sizeof(outputs));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_encoder_power_state(struct intel_encoder *intel_encoder,
					       int mode)
{
	u8 status, state = SDVO_ENCODER_STATE_ON;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		state = SDVO_ENCODER_STATE_ON;
		break;
	case DRM_MODE_DPMS_STANDBY:
		state = SDVO_ENCODER_STATE_STANDBY;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		state = SDVO_ENCODER_STATE_SUSPEND;
		break;
	case DRM_MODE_DPMS_OFF:
		state = SDVO_ENCODER_STATE_OFF;
		break;
	}

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_ENCODER_POWER_STATE, &state,
			     sizeof(state));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_get_input_pixel_clock_range(struct intel_encoder *intel_encoder,
						   int *clock_min,
						   int *clock_max)
{
	struct intel_sdvo_pixel_clock_range clocks;
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE,
			     NULL, 0);

	status = intel_sdvo_read_response(intel_encoder, &clocks, sizeof(clocks));

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	/* Convert the values from units of 10 kHz to kHz. */
	*clock_min = clocks.min * 10;
	*clock_max = clocks.max * 10;

	return true;
}

static bool intel_sdvo_set_target_output(struct intel_encoder *intel_encoder,
					 u16 outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_TARGET_OUTPUT, &outputs,
			     sizeof(outputs));

	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_get_timing(struct intel_encoder *intel_encoder, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, cmd, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &dtd->part1,
					  sizeof(dtd->part1));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(intel_encoder, cmd + 1, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &dtd->part2,
					  sizeof(dtd->part2));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_get_input_timing(struct intel_encoder *intel_encoder,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_timing(intel_encoder,
				     SDVO_CMD_GET_INPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_get_output_timing(struct intel_encoder *intel_encoder,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_timing(intel_encoder,
				     SDVO_CMD_GET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_set_timing(struct intel_encoder *intel_encoder, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, cmd, &dtd->part1, sizeof(dtd->part1));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(intel_encoder, cmd + 1, &dtd->part2, sizeof(dtd->part2));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_set_input_timing(struct intel_encoder *intel_encoder,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_encoder,
				     SDVO_CMD_SET_INPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_set_output_timing(struct intel_encoder *intel_encoder,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_encoder,
				     SDVO_CMD_SET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool
intel_sdvo_create_preferred_input_timing(struct intel_encoder *intel_encoder,
					 uint16_t clock,
					 uint16_t width,
					 uint16_t height)
{
	struct intel_sdvo_preferred_input_timing_args args;
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	uint8_t status;

	memset(&args, 0, sizeof(args));
	args.clock = clock;
	args.width = width;
	args.height = height;
	args.interlace = 0;

	if (sdvo_priv->is_lvds &&
	   (sdvo_priv->sdvo_lvds_fixed_mode->hdisplay != width ||
	    sdvo_priv->sdvo_lvds_fixed_mode->vdisplay != height))
		args.scaled = 1;

	intel_sdvo_write_cmd(intel_encoder,
			     SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING,
			     &args, sizeof(args));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_get_preferred_input_timing(struct intel_encoder *intel_encoder,
						  struct intel_sdvo_dtd *dtd)
{
	bool status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
			     NULL, 0);

	status = intel_sdvo_read_response(intel_encoder, &dtd->part1,
					  sizeof(dtd->part1));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2,
			     NULL, 0);

	status = intel_sdvo_read_response(intel_encoder, &dtd->part2,
					  sizeof(dtd->part2));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return false;
}

static int intel_sdvo_get_clock_rate_mult(struct intel_encoder *intel_encoder)
{
	u8 response, status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_CLOCK_RATE_MULT, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &response, 1);

	if (status != SDVO_CMD_STATUS_SUCCESS) {
		DRM_DEBUG_KMS("Couldn't get SDVO clock rate multiplier\n");
		return SDVO_CLOCK_RATE_MULT_1X;
	} else {
		DRM_DEBUG_KMS("Current clock rate multiplier: %d\n", response);
	}

	return response;
}

static bool intel_sdvo_set_clock_rate_mult(struct intel_encoder *intel_encoder, u8 val)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_CLOCK_RATE_MULT, &val, 1);
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static void intel_sdvo_get_dtd_from_mode(struct intel_sdvo_dtd *dtd,
					 struct drm_display_mode *mode)
{
	uint16_t width, height;
	uint16_t h_blank_len, h_sync_len, v_blank_len, v_sync_len;
	uint16_t h_sync_offset, v_sync_offset;

	width = mode->crtc_hdisplay;
	height = mode->crtc_vdisplay;

	/* do some mode translations */
	h_blank_len = mode->crtc_hblank_end - mode->crtc_hblank_start;
	h_sync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;

	v_blank_len = mode->crtc_vblank_end - mode->crtc_vblank_start;
	v_sync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;

	h_sync_offset = mode->crtc_hsync_start - mode->crtc_hblank_start;
	v_sync_offset = mode->crtc_vsync_start - mode->crtc_vblank_start;

	dtd->part1.clock = mode->clock / 10;
	dtd->part1.h_active = width & 0xff;
	dtd->part1.h_blank = h_blank_len & 0xff;
	dtd->part1.h_high = (((width >> 8) & 0xf) << 4) |
		((h_blank_len >> 8) & 0xf);
	dtd->part1.v_active = height & 0xff;
	dtd->part1.v_blank = v_blank_len & 0xff;
	dtd->part1.v_high = (((height >> 8) & 0xf) << 4) |
		((v_blank_len >> 8) & 0xf);

	dtd->part2.h_sync_off = h_sync_offset & 0xff;
	dtd->part2.h_sync_width = h_sync_len & 0xff;
	dtd->part2.v_sync_off_width = (v_sync_offset & 0xf) << 4 |
		(v_sync_len & 0xf);
	dtd->part2.sync_off_width_high = ((h_sync_offset & 0x300) >> 2) |
		((h_sync_len & 0x300) >> 4) | ((v_sync_offset & 0x30) >> 2) |
		((v_sync_len & 0x30) >> 4);

	dtd->part2.dtd_flags = 0x18;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		dtd->part2.dtd_flags |= 0x2;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		dtd->part2.dtd_flags |= 0x4;

	dtd->part2.sdvo_flags = 0;
	dtd->part2.v_sync_off_high = v_sync_offset & 0xc0;
	dtd->part2.reserved = 0;
}

static void intel_sdvo_get_mode_from_dtd(struct drm_display_mode * mode,
					 struct intel_sdvo_dtd *dtd)
{
	mode->hdisplay = dtd->part1.h_active;
	mode->hdisplay += ((dtd->part1.h_high >> 4) & 0x0f) << 8;
	mode->hsync_start = mode->hdisplay + dtd->part2.h_sync_off;
	mode->hsync_start += (dtd->part2.sync_off_width_high & 0xc0) << 2;
	mode->hsync_end = mode->hsync_start + dtd->part2.h_sync_width;
	mode->hsync_end += (dtd->part2.sync_off_width_high & 0x30) << 4;
	mode->htotal = mode->hdisplay + dtd->part1.h_blank;
	mode->htotal += (dtd->part1.h_high & 0xf) << 8;

	mode->vdisplay = dtd->part1.v_active;
	mode->vdisplay += ((dtd->part1.v_high >> 4) & 0x0f) << 8;
	mode->vsync_start = mode->vdisplay;
	mode->vsync_start += (dtd->part2.v_sync_off_width >> 4) & 0xf;
	mode->vsync_start += (dtd->part2.sync_off_width_high & 0x0c) << 2;
	mode->vsync_start += dtd->part2.v_sync_off_high & 0xc0;
	mode->vsync_end = mode->vsync_start +
		(dtd->part2.v_sync_off_width & 0xf);
	mode->vsync_end += (dtd->part2.sync_off_width_high & 0x3) << 4;
	mode->vtotal = mode->vdisplay + dtd->part1.v_blank;
	mode->vtotal += (dtd->part1.v_high & 0xf) << 8;

	mode->clock = dtd->part1.clock * 10;

	mode->flags &= ~(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
	if (dtd->part2.dtd_flags & 0x2)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	if (dtd->part2.dtd_flags & 0x4)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
}

static bool intel_sdvo_get_supp_encode(struct intel_encoder *intel_encoder,
				       struct intel_sdvo_encode *encode)
{
	uint8_t status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_SUPP_ENCODE, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, encode, sizeof(*encode));
	if (status != SDVO_CMD_STATUS_SUCCESS) { /* non-support means DVI */
		memset(encode, 0, sizeof(*encode));
		return false;
	}

	return true;
}

static bool intel_sdvo_set_encode(struct intel_encoder *intel_encoder,
				  uint8_t mode)
{
	uint8_t status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_ENCODE, &mode, 1);
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_colorimetry(struct intel_encoder *intel_encoder,
				       uint8_t mode)
{
	uint8_t status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_COLORIMETRY, &mode, 1);
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

#if 0
static void intel_sdvo_dump_hdmi_buf(struct intel_encoder *intel_encoder)
{
	int i, j;
	uint8_t set_buf_index[2];
	uint8_t av_split;
	uint8_t buf_size;
	uint8_t buf[48];
	uint8_t *pos;

	intel_sdvo_write_cmd(encoder, SDVO_CMD_GET_HBUF_AV_SPLIT, NULL, 0);
	intel_sdvo_read_response(encoder, &av_split, 1);

	for (i = 0; i <= av_split; i++) {
		set_buf_index[0] = i; set_buf_index[1] = 0;
		intel_sdvo_write_cmd(encoder, SDVO_CMD_SET_HBUF_INDEX,
				     set_buf_index, 2);
		intel_sdvo_write_cmd(encoder, SDVO_CMD_GET_HBUF_INFO, NULL, 0);
		intel_sdvo_read_response(encoder, &buf_size, 1);

		pos = buf;
		for (j = 0; j <= buf_size; j += 8) {
			intel_sdvo_write_cmd(encoder, SDVO_CMD_GET_HBUF_DATA,
					     NULL, 0);
			intel_sdvo_read_response(encoder, pos, 8);
			pos += 8;
		}
	}
}
#endif

static void intel_sdvo_set_hdmi_buf(struct intel_encoder *intel_encoder,
				    int index,
				    uint8_t *data, int8_t size, uint8_t tx_rate)
{
    uint8_t set_buf_index[2];

    set_buf_index[0] = index;
    set_buf_index[1] = 0;

    intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_HBUF_INDEX,
			 set_buf_index, 2);

    for (; size > 0; size -= 8) {
	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_HBUF_DATA, data, 8);
	data += 8;
    }

    intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_HBUF_TXRATE, &tx_rate, 1);
}

static uint8_t intel_sdvo_calc_hbuf_csum(uint8_t *data, uint8_t size)
{
	uint8_t csum = 0;
	int i;

	for (i = 0; i < size; i++)
		csum += data[i];

	return 0x100 - csum;
}

#define DIP_TYPE_AVI	0x82
#define DIP_VERSION_AVI	0x2
#define DIP_LEN_AVI	13

struct dip_infoframe {
	uint8_t type;
	uint8_t version;
	uint8_t len;
	uint8_t checksum;
	union {
		struct {
			/* Packet Byte #1 */
			uint8_t S:2;
			uint8_t B:2;
			uint8_t A:1;
			uint8_t Y:2;
			uint8_t rsvd1:1;
			/* Packet Byte #2 */
			uint8_t R:4;
			uint8_t M:2;
			uint8_t C:2;
			/* Packet Byte #3 */
			uint8_t SC:2;
			uint8_t Q:2;
			uint8_t EC:3;
			uint8_t ITC:1;
			/* Packet Byte #4 */
			uint8_t VIC:7;
			uint8_t rsvd2:1;
			/* Packet Byte #5 */
			uint8_t PR:4;
			uint8_t rsvd3:4;
			/* Packet Byte #6~13 */
			uint16_t top_bar_end;
			uint16_t bottom_bar_start;
			uint16_t left_bar_end;
			uint16_t right_bar_start;
		} avi;
		struct {
			/* Packet Byte #1 */
			uint8_t channel_count:3;
			uint8_t rsvd1:1;
			uint8_t coding_type:4;
			/* Packet Byte #2 */
			uint8_t sample_size:2; /* SS0, SS1 */
			uint8_t sample_frequency:3;
			uint8_t rsvd2:3;
			/* Packet Byte #3 */
			uint8_t coding_type_private:5;
			uint8_t rsvd3:3;
			/* Packet Byte #4 */
			uint8_t channel_allocation;
			/* Packet Byte #5 */
			uint8_t rsvd4:3;
			uint8_t level_shift:4;
			uint8_t downmix_inhibit:1;
		} audio;
		uint8_t payload[28];
	} __attribute__ ((packed)) u;
} __attribute__((packed));

static void intel_sdvo_set_avi_infoframe(struct intel_encoder *intel_encoder,
					 struct drm_display_mode * mode)
{
	struct dip_infoframe avi_if = {
		.type = DIP_TYPE_AVI,
		.version = DIP_VERSION_AVI,
		.len = DIP_LEN_AVI,
	};

	avi_if.checksum = intel_sdvo_calc_hbuf_csum((uint8_t *)&avi_if,
						    4 + avi_if.len);
	intel_sdvo_set_hdmi_buf(intel_encoder, 1, (uint8_t *)&avi_if,
				4 + avi_if.len,
				SDVO_HBUF_TX_VSYNC);
}

static void intel_sdvo_set_tv_format(struct intel_encoder *intel_encoder)
{

	struct intel_sdvo_tv_format format;
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	uint32_t format_map, i;
	uint8_t status;

	for (i = 0; i < TV_FORMAT_NUM; i++)
		if (tv_format_names[i] == sdvo_priv->tv_format_name)
			break;

	format_map = 1 << i;
	memset(&format, 0, sizeof(format));
	memcpy(&format, &format_map, sizeof(format_map) > sizeof(format) ?
			sizeof(format) : sizeof(format_map));

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_TV_FORMAT, &format_map,
			     sizeof(format));

	status = intel_sdvo_read_response(intel_encoder, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		DRM_DEBUG_KMS("%s: Failed to set TV format\n",
			  SDVO_NAME(sdvo_priv));
}

static bool intel_sdvo_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_sdvo_priv *dev_priv = intel_encoder->dev_priv;

	if (dev_priv->is_tv) {
		struct intel_sdvo_dtd output_dtd;
		bool success;

		/* We need to construct preferred input timings based on our
		 * output timings.  To do that, we have to set the output
		 * timings, even though this isn't really the right place in
		 * the sequence to do it. Oh well.
		 */


		/* Set output timings */
		intel_sdvo_get_dtd_from_mode(&output_dtd, mode);
		intel_sdvo_set_target_output(intel_encoder,
					     dev_priv->controlled_output);
		intel_sdvo_set_output_timing(intel_encoder, &output_dtd);

		/* Set the input timing to the screen. Assume always input 0. */
		intel_sdvo_set_target_input(intel_encoder, true, false);


		success = intel_sdvo_create_preferred_input_timing(intel_encoder,
								   mode->clock / 10,
								   mode->hdisplay,
								   mode->vdisplay);
		if (success) {
			struct intel_sdvo_dtd input_dtd;

			intel_sdvo_get_preferred_input_timing(intel_encoder,
							     &input_dtd);
			intel_sdvo_get_mode_from_dtd(adjusted_mode, &input_dtd);
			dev_priv->sdvo_flags = input_dtd.part2.sdvo_flags;

			drm_mode_set_crtcinfo(adjusted_mode, 0);

			mode->clock = adjusted_mode->clock;

			adjusted_mode->clock *=
				intel_sdvo_get_pixel_multiplier(mode);
		} else {
			return false;
		}
	} else if (dev_priv->is_lvds) {
		struct intel_sdvo_dtd output_dtd;
		bool success;

		drm_mode_set_crtcinfo(dev_priv->sdvo_lvds_fixed_mode, 0);
		/* Set output timings */
		intel_sdvo_get_dtd_from_mode(&output_dtd,
				dev_priv->sdvo_lvds_fixed_mode);

		intel_sdvo_set_target_output(intel_encoder,
					     dev_priv->controlled_output);
		intel_sdvo_set_output_timing(intel_encoder, &output_dtd);

		/* Set the input timing to the screen. Assume always input 0. */
		intel_sdvo_set_target_input(intel_encoder, true, false);


		success = intel_sdvo_create_preferred_input_timing(
				intel_encoder,
				mode->clock / 10,
				mode->hdisplay,
				mode->vdisplay);

		if (success) {
			struct intel_sdvo_dtd input_dtd;

			intel_sdvo_get_preferred_input_timing(intel_encoder,
							     &input_dtd);
			intel_sdvo_get_mode_from_dtd(adjusted_mode, &input_dtd);
			dev_priv->sdvo_flags = input_dtd.part2.sdvo_flags;

			drm_mode_set_crtcinfo(adjusted_mode, 0);

			mode->clock = adjusted_mode->clock;

			adjusted_mode->clock *=
				intel_sdvo_get_pixel_multiplier(mode);
		} else {
			return false;
		}

	} else {
		/* Make the CRTC code factor in the SDVO pixel multiplier.  The
		 * SDVO device will be told of the multiplier during mode_set.
		 */
		adjusted_mode->clock *= intel_sdvo_get_pixel_multiplier(mode);
	}
	return true;
}

static void intel_sdvo_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc = encoder->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	u32 sdvox = 0;
	int sdvo_pixel_multiply;
	struct intel_sdvo_in_out_map in_out;
	struct intel_sdvo_dtd input_dtd;
	u8 status;

	if (!mode)
		return;

	/* First, set the input mapping for the first input to our controlled
	 * output. This is only correct if we're a single-input device, in
	 * which case the first input is the output from the appropriate SDVO
	 * channel on the motherboard.  In a two-input device, the first input
	 * will be SDVOB and the second SDVOC.
	 */
	in_out.in0 = sdvo_priv->controlled_output;
	in_out.in1 = 0;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_IN_OUT_MAP,
			     &in_out, sizeof(in_out));
	status = intel_sdvo_read_response(intel_encoder, NULL, 0);

	if (sdvo_priv->is_hdmi) {
		intel_sdvo_set_avi_infoframe(intel_encoder, mode);
		sdvox |= SDVO_AUDIO_ENABLE;
	}

	/* We have tried to get input timing in mode_fixup, and filled into
	   adjusted_mode */
	if (sdvo_priv->is_tv || sdvo_priv->is_lvds) {
		intel_sdvo_get_dtd_from_mode(&input_dtd, adjusted_mode);
		input_dtd.part2.sdvo_flags = sdvo_priv->sdvo_flags;
	} else
		intel_sdvo_get_dtd_from_mode(&input_dtd, mode);

	/* If it's a TV, we already set the output timing in mode_fixup.
	 * Otherwise, the output timing is equal to the input timing.
	 */
	if (!sdvo_priv->is_tv && !sdvo_priv->is_lvds) {
		/* Set the output timing to the screen */
		intel_sdvo_set_target_output(intel_encoder,
					     sdvo_priv->controlled_output);
		intel_sdvo_set_output_timing(intel_encoder, &input_dtd);
	}

	/* Set the input timing to the screen. Assume always input 0. */
	intel_sdvo_set_target_input(intel_encoder, true, false);

	if (sdvo_priv->is_tv)
		intel_sdvo_set_tv_format(intel_encoder);

	/* We would like to use intel_sdvo_create_preferred_input_timing() to
	 * provide the device with a timing it can support, if it supports that
	 * feature.  However, presumably we would need to adjust the CRTC to
	 * output the preferred timing, and we don't support that currently.
	 */
#if 0
	success = intel_sdvo_create_preferred_input_timing(encoder, clock,
							   width, height);
	if (success) {
		struct intel_sdvo_dtd *input_dtd;

		intel_sdvo_get_preferred_input_timing(encoder, &input_dtd);
		intel_sdvo_set_input_timing(encoder, &input_dtd);
	}
#else
	intel_sdvo_set_input_timing(intel_encoder, &input_dtd);
#endif

	switch (intel_sdvo_get_pixel_multiplier(mode)) {
	case 1:
		intel_sdvo_set_clock_rate_mult(intel_encoder,
					       SDVO_CLOCK_RATE_MULT_1X);
		break;
	case 2:
		intel_sdvo_set_clock_rate_mult(intel_encoder,
					       SDVO_CLOCK_RATE_MULT_2X);
		break;
	case 4:
		intel_sdvo_set_clock_rate_mult(intel_encoder,
					       SDVO_CLOCK_RATE_MULT_4X);
		break;
	}

	/* Set the SDVO control regs. */
	if (IS_I965G(dev)) {
		sdvox |= SDVO_BORDER_ENABLE |
			SDVO_VSYNC_ACTIVE_HIGH |
			SDVO_HSYNC_ACTIVE_HIGH;
	} else {
		sdvox |= I915_READ(sdvo_priv->sdvo_reg);
		switch (sdvo_priv->sdvo_reg) {
		case SDVOB:
			sdvox &= SDVOB_PRESERVE_MASK;
			break;
		case SDVOC:
			sdvox &= SDVOC_PRESERVE_MASK;
			break;
		}
		sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;
	}
	if (intel_crtc->pipe == 1)
		sdvox |= SDVO_PIPE_B_SELECT;

	sdvo_pixel_multiply = intel_sdvo_get_pixel_multiplier(mode);
	if (IS_I965G(dev)) {
		/* done in crtc_mode_set as the dpll_md reg must be written early */
	} else if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev)) {
		/* done in crtc_mode_set as it lives inside the dpll register */
	} else {
		sdvox |= (sdvo_pixel_multiply - 1) << SDVO_PORT_MULTIPLY_SHIFT;
	}

	if (sdvo_priv->sdvo_flags & SDVO_NEED_TO_STALL)
		sdvox |= SDVO_STALL_SELECT;
	intel_sdvo_write_sdvox(intel_encoder, sdvox);
}

static void intel_sdvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	u32 temp;

	if (mode != DRM_MODE_DPMS_ON) {
		intel_sdvo_set_active_outputs(intel_encoder, 0);
		if (0)
			intel_sdvo_set_encoder_power_state(intel_encoder, mode);

		if (mode == DRM_MODE_DPMS_OFF) {
			temp = I915_READ(sdvo_priv->sdvo_reg);
			if ((temp & SDVO_ENABLE) != 0) {
				intel_sdvo_write_sdvox(intel_encoder, temp & ~SDVO_ENABLE);
			}
		}
	} else {
		bool input1, input2;
		int i;
		u8 status;

		temp = I915_READ(sdvo_priv->sdvo_reg);
		if ((temp & SDVO_ENABLE) == 0)
			intel_sdvo_write_sdvox(intel_encoder, temp | SDVO_ENABLE);
		for (i = 0; i < 2; i++)
		  intel_wait_for_vblank(dev);

		status = intel_sdvo_get_trained_inputs(intel_encoder, &input1,
						       &input2);


		/* Warn if the device reported failure to sync.
		 * A lot of SDVO devices fail to notify of sync, but it's
		 * a given it the status is a success, we succeeded.
		 */
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1) {
			DRM_DEBUG_KMS("First %s output reported failure to "
					"sync\n", SDVO_NAME(sdvo_priv));
		}

		if (0)
			intel_sdvo_set_encoder_power_state(intel_encoder, mode);
		intel_sdvo_set_active_outputs(intel_encoder, sdvo_priv->controlled_output);
	}
	return;
}

static void intel_sdvo_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int o;

	sdvo_priv->save_sdvo_mult = intel_sdvo_get_clock_rate_mult(intel_encoder);
	intel_sdvo_get_active_outputs(intel_encoder, &sdvo_priv->save_active_outputs);

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		intel_sdvo_set_target_input(intel_encoder, true, false);
		intel_sdvo_get_input_timing(intel_encoder,
					    &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		intel_sdvo_set_target_input(intel_encoder, false, true);
		intel_sdvo_get_input_timing(intel_encoder,
					    &sdvo_priv->save_input_dtd_2);
	}

	for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++)
	{
	        u16  this_output = (1 << o);
		if (sdvo_priv->caps.output_flags & this_output)
		{
			intel_sdvo_set_target_output(intel_encoder, this_output);
			intel_sdvo_get_output_timing(intel_encoder,
						     &sdvo_priv->save_output_dtd[o]);
		}
	}
	if (sdvo_priv->is_tv) {
		/* XXX: Save TV format/enhancements. */
	}

	sdvo_priv->save_SDVOX = I915_READ(sdvo_priv->sdvo_reg);
}

static void intel_sdvo_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int o;
	int i;
	bool input1, input2;
	u8 status;

	intel_sdvo_set_active_outputs(intel_encoder, 0);

	for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++)
	{
		u16  this_output = (1 << o);
		if (sdvo_priv->caps.output_flags & this_output) {
			intel_sdvo_set_target_output(intel_encoder, this_output);
			intel_sdvo_set_output_timing(intel_encoder, &sdvo_priv->save_output_dtd[o]);
		}
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		intel_sdvo_set_target_input(intel_encoder, true, false);
		intel_sdvo_set_input_timing(intel_encoder, &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		intel_sdvo_set_target_input(intel_encoder, false, true);
		intel_sdvo_set_input_timing(intel_encoder, &sdvo_priv->save_input_dtd_2);
	}

	intel_sdvo_set_clock_rate_mult(intel_encoder, sdvo_priv->save_sdvo_mult);

	if (sdvo_priv->is_tv) {
		/* XXX: Restore TV format/enhancements. */
	}

	intel_sdvo_write_sdvox(intel_encoder, sdvo_priv->save_SDVOX);

	if (sdvo_priv->save_SDVOX & SDVO_ENABLE)
	{
		for (i = 0; i < 2; i++)
			intel_wait_for_vblank(dev);
		status = intel_sdvo_get_trained_inputs(intel_encoder, &input1, &input2);
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1)
			DRM_DEBUG_KMS("First %s output reported failure to "
					"sync\n", SDVO_NAME(sdvo_priv));
	}

	intel_sdvo_set_active_outputs(intel_encoder, sdvo_priv->save_active_outputs);
}

static int intel_sdvo_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (sdvo_priv->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (sdvo_priv->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	if (sdvo_priv->is_lvds == true) {
		if (sdvo_priv->sdvo_lvds_fixed_mode == NULL)
			return MODE_PANEL;

		if (mode->hdisplay > sdvo_priv->sdvo_lvds_fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > sdvo_priv->sdvo_lvds_fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	return MODE_OK;
}

static bool intel_sdvo_get_capabilities(struct intel_encoder *intel_encoder, struct intel_sdvo_caps *caps)
{
	u8 status;

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_DEVICE_CAPS, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, caps, sizeof(*caps));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

struct drm_connector* intel_sdvo_find(struct drm_device *dev, int sdvoB)
{
	struct drm_connector *connector = NULL;
	struct intel_encoder *iout = NULL;
	struct intel_sdvo_priv *sdvo;

	/* find the sdvo connector */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		iout = to_intel_encoder(connector);

		if (iout->type != INTEL_OUTPUT_SDVO)
			continue;

		sdvo = iout->dev_priv;

		if (sdvo->sdvo_reg == SDVOB && sdvoB)
			return connector;

		if (sdvo->sdvo_reg == SDVOC && !sdvoB)
			return connector;

	}

	return NULL;
}

int intel_sdvo_supports_hotplug(struct drm_connector *connector)
{
	u8 response[2];
	u8 status;
	struct intel_encoder *intel_encoder;
	DRM_DEBUG_KMS("\n");

	if (!connector)
		return 0;

	intel_encoder = to_intel_encoder(connector);

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_HOT_PLUG_SUPPORT, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &response, 2);

	if (response[0] !=0)
		return 1;

	return 0;
}

void intel_sdvo_set_hotplug(struct drm_connector *connector, int on)
{
	u8 response[2];
	u8 status;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);
	intel_sdvo_read_response(intel_encoder, &response, 2);

	if (on) {
		intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_HOT_PLUG_SUPPORT, NULL, 0);
		status = intel_sdvo_read_response(intel_encoder, &response, 2);

		intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_ACTIVE_HOT_PLUG, &response, 2);
	} else {
		response[0] = 0;
		response[1] = 0;
		intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_SET_ACTIVE_HOT_PLUG, &response, 2);
	}

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);
	intel_sdvo_read_response(intel_encoder, &response, 2);
}

static bool
intel_sdvo_multifunc_encoder(struct intel_encoder *intel_encoder)
{
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int caps = 0;

	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1))
		caps++;
	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1))
		caps++;
	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_SVID1))
		caps++;
	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_CVBS0 | SDVO_OUTPUT_CVBS1))
		caps++;
	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_YPRPB0 | SDVO_OUTPUT_YPRPB1))
		caps++;

	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_SCART0 | SDVO_OUTPUT_SCART1))
		caps++;

	if (sdvo_priv->caps.output_flags &
		(SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1))
		caps++;

	return (caps > 1);
}

static struct drm_connector *
intel_find_analog_connector(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		intel_encoder = to_intel_encoder(connector);
		if (intel_encoder->type == INTEL_OUTPUT_ANALOG)
			return connector;
	}
	return NULL;
}

static int
intel_analog_is_connected(struct drm_device *dev)
{
	struct drm_connector *analog_connector;
	analog_connector = intel_find_analog_connector(dev);

	if (!analog_connector)
		return false;

	if (analog_connector->funcs->detect(analog_connector) ==
			connector_status_disconnected)
		return false;

	return true;
}

enum drm_connector_status
intel_sdvo_hdmi_sink_detect(struct drm_connector *connector, u16 response)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	enum drm_connector_status status = connector_status_connected;
	struct edid *edid = NULL;

	edid = drm_get_edid(&intel_encoder->base,
			    intel_encoder->ddc_bus);

	/* This is only applied to SDVO cards with multiple outputs */
	if (edid == NULL && intel_sdvo_multifunc_encoder(intel_encoder)) {
		uint8_t saved_ddc, temp_ddc;
		saved_ddc = sdvo_priv->ddc_bus;
		temp_ddc = sdvo_priv->ddc_bus >> 1;
		/*
		 * Don't use the 1 as the argument of DDC bus switch to get
		 * the EDID. It is used for SDVO SPD ROM.
		 */
		while(temp_ddc > 1) {
			sdvo_priv->ddc_bus = temp_ddc;
			edid = drm_get_edid(&intel_encoder->base,
				intel_encoder->ddc_bus);
			if (edid) {
				/*
				 * When we can get the EDID, maybe it is the
				 * correct DDC bus. Update it.
				 */
				sdvo_priv->ddc_bus = temp_ddc;
				break;
			}
			temp_ddc >>= 1;
		}
		if (edid == NULL)
			sdvo_priv->ddc_bus = saved_ddc;
	}
	/* when there is no edid and no monitor is connected with VGA
	 * port, try to use the CRT ddc to read the EDID for DVI-connector
	 */
	if (edid == NULL &&
	    sdvo_priv->analog_ddc_bus &&
	    !intel_analog_is_connected(intel_encoder->base.dev))
		edid = drm_get_edid(&intel_encoder->base,
				    sdvo_priv->analog_ddc_bus);
	if (edid != NULL) {
		/* Don't report the output as connected if it's a DVI-I
		 * connector with a non-digital EDID coming out.
		 */
		if (response & (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1)) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL)
				sdvo_priv->is_hdmi =
					drm_detect_hdmi_monitor(edid);
			else
				status = connector_status_disconnected;
		}

		kfree(edid);
		intel_encoder->base.display_info.raw_edid = NULL;

	} else if (response & (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1))
		status = connector_status_disconnected;

	return status;
}

static enum drm_connector_status intel_sdvo_detect(struct drm_connector *connector)
{
	uint16_t response;
	u8 status;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;

	intel_sdvo_write_cmd(intel_encoder,
			     SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0);
	if (sdvo_priv->is_tv) {
		/* add 30ms delay when the output type is SDVO-TV */
		mdelay(30);
	}
	status = intel_sdvo_read_response(intel_encoder, &response, 2);

	DRM_DEBUG_KMS("SDVO response %d %d\n", response & 0xff, response >> 8);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return connector_status_unknown;

	if (response == 0)
		return connector_status_disconnected;

	if (intel_sdvo_multifunc_encoder(intel_encoder) &&
		sdvo_priv->attached_output != response) {
		if (sdvo_priv->controlled_output != response &&
			intel_sdvo_output_setup(intel_encoder, response) != true)
			return connector_status_unknown;
		sdvo_priv->attached_output = response;
	}
	return intel_sdvo_hdmi_sink_detect(connector, response);
}

static void intel_sdvo_get_ddc_modes(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	int num_modes;

	/* set the bus switch and get the modes */
	num_modes = intel_ddc_get_modes(intel_encoder);

	/*
	 * Mac mini hack.  On this device, the DVI-I connector shares one DDC
	 * link between analog and digital outputs. So, if the regular SDVO
	 * DDC fails, check to see if the analog output is disconnected, in
	 * which case we'll look there for the digital DDC data.
	 */
	if (num_modes == 0 &&
	    sdvo_priv->analog_ddc_bus &&
	    !intel_analog_is_connected(intel_encoder->base.dev)) {
		struct i2c_adapter *digital_ddc_bus;

		/* Switch to the analog ddc bus and try that
		 */
		digital_ddc_bus = intel_encoder->ddc_bus;
		intel_encoder->ddc_bus = sdvo_priv->analog_ddc_bus;

		(void) intel_ddc_get_modes(intel_encoder);

		intel_encoder->ddc_bus = digital_ddc_bus;
	}
}

/*
 * Set of SDVO TV modes.
 * Note!  This is in reply order (see loop in get_tv_modes).
 * XXX: all 60Hz refresh?
 */
struct drm_display_mode sdvo_tv_modes[] = {
	{ DRM_MODE("320x200", DRM_MODE_TYPE_DRIVER, 5815, 320, 321, 384,
		   416, 0, 200, 201, 232, 233, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("320x240", DRM_MODE_TYPE_DRIVER, 6814, 320, 321, 384,
		   416, 0, 240, 241, 272, 273, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("400x300", DRM_MODE_TYPE_DRIVER, 9910, 400, 401, 464,
		   496, 0, 300, 301, 332, 333, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("640x350", DRM_MODE_TYPE_DRIVER, 16913, 640, 641, 704,
		   736, 0, 350, 351, 382, 383, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("640x400", DRM_MODE_TYPE_DRIVER, 19121, 640, 641, 704,
		   736, 0, 400, 401, 432, 433, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 22654, 640, 641, 704,
		   736, 0, 480, 481, 512, 513, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("704x480", DRM_MODE_TYPE_DRIVER, 24624, 704, 705, 768,
		   800, 0, 480, 481, 512, 513, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("704x576", DRM_MODE_TYPE_DRIVER, 29232, 704, 705, 768,
		   800, 0, 576, 577, 608, 609, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("720x350", DRM_MODE_TYPE_DRIVER, 18751, 720, 721, 784,
		   816, 0, 350, 351, 382, 383, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("720x400", DRM_MODE_TYPE_DRIVER, 21199, 720, 721, 784,
		   816, 0, 400, 401, 432, 433, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 25116, 720, 721, 784,
		   816, 0, 480, 481, 512, 513, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("720x540", DRM_MODE_TYPE_DRIVER, 28054, 720, 721, 784,
		   816, 0, 540, 541, 572, 573, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 29816, 720, 721, 784,
		   816, 0, 576, 577, 608, 609, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("768x576", DRM_MODE_TYPE_DRIVER, 31570, 768, 769, 832,
		   864, 0, 576, 577, 608, 609, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("800x600", DRM_MODE_TYPE_DRIVER, 34030, 800, 801, 864,
		   896, 0, 600, 601, 632, 633, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("832x624", DRM_MODE_TYPE_DRIVER, 36581, 832, 833, 896,
		   928, 0, 624, 625, 656, 657, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("920x766", DRM_MODE_TYPE_DRIVER, 48707, 920, 921, 984,
		   1016, 0, 766, 767, 798, 799, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 53827, 1024, 1025, 1088,
		   1120, 0, 768, 769, 800, 801, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
	{ DRM_MODE("1280x1024", DRM_MODE_TYPE_DRIVER, 87265, 1280, 1281, 1344,
		   1376, 0, 1024, 1025, 1056, 1057, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
};

static void intel_sdvo_get_tv_modes(struct drm_connector *connector)
{
	struct intel_encoder *output = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;
	struct intel_sdvo_sdtv_resolution_request tv_res;
	uint32_t reply = 0, format_map = 0;
	int i;
	uint8_t status;


	/* Read the list of supported input resolutions for the selected TV
	 * format.
	 */
	for (i = 0; i < TV_FORMAT_NUM; i++)
		if (tv_format_names[i] ==  sdvo_priv->tv_format_name)
			break;

	format_map = (1 << i);
	memcpy(&tv_res, &format_map,
	       sizeof(struct intel_sdvo_sdtv_resolution_request) >
	       sizeof(format_map) ? sizeof(format_map) :
	       sizeof(struct intel_sdvo_sdtv_resolution_request));

	intel_sdvo_set_target_output(output, sdvo_priv->controlled_output);

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_SDTV_RESOLUTION_SUPPORT,
			     &tv_res, sizeof(tv_res));
	status = intel_sdvo_read_response(output, &reply, 3);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return;

	for (i = 0; i < ARRAY_SIZE(sdvo_tv_modes); i++)
		if (reply & (1 << i)) {
			struct drm_display_mode *nmode;
			nmode = drm_mode_duplicate(connector->dev,
					&sdvo_tv_modes[i]);
			if (nmode)
				drm_mode_probed_add(connector, nmode);
		}

}

static void intel_sdvo_get_lvds_modes(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	struct drm_display_mode *newmode;

	/*
	 * Attempt to get the mode list from DDC.
	 * Assume that the preferred modes are
	 * arranged in priority order.
	 */
	intel_ddc_get_modes(intel_encoder);
	if (list_empty(&connector->probed_modes) == false)
		goto end;

	/* Fetch modes from VBT */
	if (dev_priv->sdvo_lvds_vbt_mode != NULL) {
		newmode = drm_mode_duplicate(connector->dev,
					     dev_priv->sdvo_lvds_vbt_mode);
		if (newmode != NULL) {
			/* Guarantee the mode is preferred */
			newmode->type = (DRM_MODE_TYPE_PREFERRED |
					 DRM_MODE_TYPE_DRIVER);
			drm_mode_probed_add(connector, newmode);
		}
	}

end:
	list_for_each_entry(newmode, &connector->probed_modes, head) {
		if (newmode->type & DRM_MODE_TYPE_PREFERRED) {
			sdvo_priv->sdvo_lvds_fixed_mode =
				drm_mode_duplicate(connector->dev, newmode);
			break;
		}
	}

}

static int intel_sdvo_get_modes(struct drm_connector *connector)
{
	struct intel_encoder *output = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;

	if (sdvo_priv->is_tv)
		intel_sdvo_get_tv_modes(connector);
	else if (sdvo_priv->is_lvds == true)
		intel_sdvo_get_lvds_modes(connector);
	else
		intel_sdvo_get_ddc_modes(connector);

	if (list_empty(&connector->probed_modes))
		return 0;
	return 1;
}

static
void intel_sdvo_destroy_enhance_property(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	struct drm_device *dev = connector->dev;

	if (sdvo_priv->is_tv) {
		if (sdvo_priv->left_property)
			drm_property_destroy(dev, sdvo_priv->left_property);
		if (sdvo_priv->right_property)
			drm_property_destroy(dev, sdvo_priv->right_property);
		if (sdvo_priv->top_property)
			drm_property_destroy(dev, sdvo_priv->top_property);
		if (sdvo_priv->bottom_property)
			drm_property_destroy(dev, sdvo_priv->bottom_property);
		if (sdvo_priv->hpos_property)
			drm_property_destroy(dev, sdvo_priv->hpos_property);
		if (sdvo_priv->vpos_property)
			drm_property_destroy(dev, sdvo_priv->vpos_property);
	}
	if (sdvo_priv->is_tv) {
		if (sdvo_priv->saturation_property)
			drm_property_destroy(dev,
					sdvo_priv->saturation_property);
		if (sdvo_priv->contrast_property)
			drm_property_destroy(dev,
					sdvo_priv->contrast_property);
		if (sdvo_priv->hue_property)
			drm_property_destroy(dev, sdvo_priv->hue_property);
	}
	if (sdvo_priv->is_tv || sdvo_priv->is_lvds) {
		if (sdvo_priv->brightness_property)
			drm_property_destroy(dev,
					sdvo_priv->brightness_property);
	}
	return;
}

static void intel_sdvo_destroy(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;

	if (intel_encoder->i2c_bus)
		intel_i2c_destroy(intel_encoder->i2c_bus);
	if (intel_encoder->ddc_bus)
		intel_i2c_destroy(intel_encoder->ddc_bus);
	if (sdvo_priv->analog_ddc_bus)
		intel_i2c_destroy(sdvo_priv->analog_ddc_bus);

	if (sdvo_priv->sdvo_lvds_fixed_mode != NULL)
		drm_mode_destroy(connector->dev,
				 sdvo_priv->sdvo_lvds_fixed_mode);

	if (sdvo_priv->tv_format_property)
		drm_property_destroy(connector->dev,
				     sdvo_priv->tv_format_property);

	if (sdvo_priv->is_tv || sdvo_priv->is_lvds)
		intel_sdvo_destroy_enhance_property(connector);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);

	kfree(intel_encoder);
}

static int
intel_sdvo_set_property(struct drm_connector *connector,
			struct drm_property *property,
			uint64_t val)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	struct drm_encoder *encoder = &intel_encoder->enc;
	struct drm_crtc *crtc = encoder->crtc;
	int ret = 0;
	bool changed = false;
	uint8_t cmd, status;
	uint16_t temp_value;

	ret = drm_connector_property_set_value(connector, property, val);
	if (ret < 0)
		goto out;

	if (property == sdvo_priv->tv_format_property) {
		if (val >= TV_FORMAT_NUM) {
			ret = -EINVAL;
			goto out;
		}
		if (sdvo_priv->tv_format_name ==
		    sdvo_priv->tv_format_supported[val])
			goto out;

		sdvo_priv->tv_format_name = sdvo_priv->tv_format_supported[val];
		changed = true;
	}

	if (sdvo_priv->is_tv || sdvo_priv->is_lvds) {
		cmd = 0;
		temp_value = val;
		if (sdvo_priv->left_property == property) {
			drm_connector_property_set_value(connector,
				sdvo_priv->right_property, val);
			if (sdvo_priv->left_margin == temp_value)
				goto out;

			sdvo_priv->left_margin = temp_value;
			sdvo_priv->right_margin = temp_value;
			temp_value = sdvo_priv->max_hscan -
					sdvo_priv->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
		} else if (sdvo_priv->right_property == property) {
			drm_connector_property_set_value(connector,
				sdvo_priv->left_property, val);
			if (sdvo_priv->right_margin == temp_value)
				goto out;

			sdvo_priv->left_margin = temp_value;
			sdvo_priv->right_margin = temp_value;
			temp_value = sdvo_priv->max_hscan -
				sdvo_priv->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
		} else if (sdvo_priv->top_property == property) {
			drm_connector_property_set_value(connector,
				sdvo_priv->bottom_property, val);
			if (sdvo_priv->top_margin == temp_value)
				goto out;

			sdvo_priv->top_margin = temp_value;
			sdvo_priv->bottom_margin = temp_value;
			temp_value = sdvo_priv->max_vscan -
					sdvo_priv->top_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_V;
		} else if (sdvo_priv->bottom_property == property) {
			drm_connector_property_set_value(connector,
				sdvo_priv->top_property, val);
			if (sdvo_priv->bottom_margin == temp_value)
				goto out;
			sdvo_priv->top_margin = temp_value;
			sdvo_priv->bottom_margin = temp_value;
			temp_value = sdvo_priv->max_vscan -
					sdvo_priv->top_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_V;
		} else if (sdvo_priv->hpos_property == property) {
			if (sdvo_priv->cur_hpos == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_POSITION_H;
			sdvo_priv->cur_hpos = temp_value;
		} else if (sdvo_priv->vpos_property == property) {
			if (sdvo_priv->cur_vpos == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_POSITION_V;
			sdvo_priv->cur_vpos = temp_value;
		} else if (sdvo_priv->saturation_property == property) {
			if (sdvo_priv->cur_saturation == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_SATURATION;
			sdvo_priv->cur_saturation = temp_value;
		} else if (sdvo_priv->contrast_property == property) {
			if (sdvo_priv->cur_contrast == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_CONTRAST;
			sdvo_priv->cur_contrast = temp_value;
		} else if (sdvo_priv->hue_property == property) {
			if (sdvo_priv->cur_hue == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_HUE;
			sdvo_priv->cur_hue = temp_value;
		} else if (sdvo_priv->brightness_property == property) {
			if (sdvo_priv->cur_brightness == temp_value)
				goto out;

			cmd = SDVO_CMD_SET_BRIGHTNESS;
			sdvo_priv->cur_brightness = temp_value;
		}
		if (cmd) {
			intel_sdvo_write_cmd(intel_encoder, cmd, &temp_value, 2);
			status = intel_sdvo_read_response(intel_encoder,
								NULL, 0);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO command \n");
				return -EINVAL;
			}
			changed = true;
		}
	}
	if (changed && crtc)
		drm_crtc_helper_set_mode(crtc, &crtc->mode, crtc->x,
				crtc->y, crtc->fb);
out:
	return ret;
}

static const struct drm_encoder_helper_funcs intel_sdvo_helper_funcs = {
	.dpms = intel_sdvo_dpms,
	.mode_fixup = intel_sdvo_mode_fixup,
	.prepare = intel_encoder_prepare,
	.mode_set = intel_sdvo_mode_set,
	.commit = intel_encoder_commit,
};

static const struct drm_connector_funcs intel_sdvo_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = intel_sdvo_save,
	.restore = intel_sdvo_restore,
	.detect = intel_sdvo_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_sdvo_set_property,
	.destroy = intel_sdvo_destroy,
};

static const struct drm_connector_helper_funcs intel_sdvo_connector_helper_funcs = {
	.get_modes = intel_sdvo_get_modes,
	.mode_valid = intel_sdvo_mode_valid,
	.best_encoder = intel_best_encoder,
};

static void intel_sdvo_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_sdvo_enc_funcs = {
	.destroy = intel_sdvo_enc_destroy,
};


/**
 * Choose the appropriate DDC bus for control bus switch command for this
 * SDVO output based on the controlled output.
 *
 * DDC bus number assignment is in a priority order of RGB outputs, then TMDS
 * outputs, then LVDS outputs.
 */
static void
intel_sdvo_select_ddc_bus(struct intel_sdvo_priv *dev_priv)
{
	uint16_t mask = 0;
	unsigned int num_bits;

	/* Make a mask of outputs less than or equal to our own priority in the
	 * list.
	 */
	switch (dev_priv->controlled_output) {
	case SDVO_OUTPUT_LVDS1:
		mask |= SDVO_OUTPUT_LVDS1;
	case SDVO_OUTPUT_LVDS0:
		mask |= SDVO_OUTPUT_LVDS0;
	case SDVO_OUTPUT_TMDS1:
		mask |= SDVO_OUTPUT_TMDS1;
	case SDVO_OUTPUT_TMDS0:
		mask |= SDVO_OUTPUT_TMDS0;
	case SDVO_OUTPUT_RGB1:
		mask |= SDVO_OUTPUT_RGB1;
	case SDVO_OUTPUT_RGB0:
		mask |= SDVO_OUTPUT_RGB0;
		break;
	}

	/* Count bits to find what number we are in the priority list. */
	mask &= dev_priv->caps.output_flags;
	num_bits = hweight16(mask);
	if (num_bits > 3) {
		/* if more than 3 outputs, default to DDC bus 3 for now */
		num_bits = 3;
	}

	/* Corresponds to SDVO_CONTROL_BUS_DDCx */
	dev_priv->ddc_bus = 1 << num_bits;
}

static bool
intel_sdvo_get_digital_encoding_mode(struct intel_encoder *output)
{
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;
	uint8_t status;

	intel_sdvo_set_target_output(output, sdvo_priv->controlled_output);

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_ENCODE, NULL, 0);
	status = intel_sdvo_read_response(output, &sdvo_priv->is_hdmi, 1);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;
	return true;
}

static struct intel_encoder *
intel_sdvo_chan_to_intel_encoder(struct intel_i2c_chan *chan)
{
	struct drm_device *dev = chan->drm_dev;
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder = NULL;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head) {
		if (to_intel_encoder(connector)->ddc_bus == &chan->adapter) {
			intel_encoder = to_intel_encoder(connector);
			break;
		}
	}
	return intel_encoder;
}

static int intel_sdvo_master_xfer(struct i2c_adapter *i2c_adap,
				  struct i2c_msg msgs[], int num)
{
	struct intel_encoder *intel_encoder;
	struct intel_sdvo_priv *sdvo_priv;
	struct i2c_algo_bit_data *algo_data;
	const struct i2c_algorithm *algo;

	algo_data = (struct i2c_algo_bit_data *)i2c_adap->algo_data;
	intel_encoder =
		intel_sdvo_chan_to_intel_encoder(
				(struct intel_i2c_chan *)(algo_data->data));
	if (intel_encoder == NULL)
		return -EINVAL;

	sdvo_priv = intel_encoder->dev_priv;
	algo = intel_encoder->i2c_bus->algo;

	intel_sdvo_set_control_bus_switch(intel_encoder, sdvo_priv->ddc_bus);
	return algo->master_xfer(i2c_adap, msgs, num);
}

static struct i2c_algorithm intel_sdvo_i2c_bit_algo = {
	.master_xfer	= intel_sdvo_master_xfer,
};

static u8
intel_sdvo_get_slave_addr(struct drm_device *dev, int sdvo_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct sdvo_device_mapping *my_mapping, *other_mapping;

	if (sdvo_reg == SDVOB) {
		my_mapping = &dev_priv->sdvo_mappings[0];
		other_mapping = &dev_priv->sdvo_mappings[1];
	} else {
		my_mapping = &dev_priv->sdvo_mappings[1];
		other_mapping = &dev_priv->sdvo_mappings[0];
	}

	/* If the BIOS described our SDVO device, take advantage of it. */
	if (my_mapping->slave_addr)
		return my_mapping->slave_addr;

	/* If the BIOS only described a different SDVO device, use the
	 * address that it isn't using.
	 */
	if (other_mapping->slave_addr) {
		if (other_mapping->slave_addr == 0x70)
			return 0x72;
		else
			return 0x70;
	}

	/* No SDVO device info is found for another DVO port,
	 * so use mapping assumption we had before BIOS parsing.
	 */
	if (sdvo_reg == SDVOB)
		return 0x70;
	else
		return 0x72;
}

static int intel_sdvo_bad_tv_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_KMS("Ignoring bad SDVO TV connector for %s\n", id->ident);
	return 1;
}

static struct dmi_system_id intel_sdvo_bad_tv[] = {
	{
		.callback = intel_sdvo_bad_tv_callback,
		.ident = "IntelG45/ICH10R/DME1737",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM CORPORATION"),
			DMI_MATCH(DMI_PRODUCT_NAME, "4800784"),
		},
	},

	{ }	/* terminating entry */
};

static bool
intel_sdvo_output_setup(struct intel_encoder *intel_encoder, uint16_t flags)
{
	struct drm_connector *connector = &intel_encoder->base;
	struct drm_encoder *encoder = &intel_encoder->enc;
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	bool ret = true, registered = false;

	sdvo_priv->is_tv = false;
	intel_encoder->needs_tv_clock = false;
	sdvo_priv->is_lvds = false;

	if (device_is_registered(&connector->kdev)) {
		drm_sysfs_connector_remove(connector);
		registered = true;
	}

	if (flags &
	    (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1)) {
		if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_TMDS0)
			sdvo_priv->controlled_output = SDVO_OUTPUT_TMDS0;
		else
			sdvo_priv->controlled_output = SDVO_OUTPUT_TMDS1;

		encoder->encoder_type = DRM_MODE_ENCODER_TMDS;
		connector->connector_type = DRM_MODE_CONNECTOR_DVID;

		if (intel_sdvo_get_supp_encode(intel_encoder,
					       &sdvo_priv->encode) &&
		    intel_sdvo_get_digital_encoding_mode(intel_encoder) &&
		    sdvo_priv->is_hdmi) {
			/* enable hdmi encoding mode if supported */
			intel_sdvo_set_encode(intel_encoder, SDVO_ENCODE_HDMI);
			intel_sdvo_set_colorimetry(intel_encoder,
						   SDVO_COLORIMETRY_RGB256);
			connector->connector_type = DRM_MODE_CONNECTOR_HDMIA;
			intel_encoder->clone_mask =
					(1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
					(1 << INTEL_ANALOG_CLONE_BIT);
		}
	} else if ((flags & SDVO_OUTPUT_SVID0) &&
		   !dmi_check_system(intel_sdvo_bad_tv)) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_SVID0;
		encoder->encoder_type = DRM_MODE_ENCODER_TVDAC;
		connector->connector_type = DRM_MODE_CONNECTOR_SVIDEO;
		sdvo_priv->is_tv = true;
		intel_encoder->needs_tv_clock = true;
		intel_encoder->clone_mask = 1 << INTEL_SDVO_TV_CLONE_BIT;
	} else if (flags & SDVO_OUTPUT_RGB0) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_RGB0;
		encoder->encoder_type = DRM_MODE_ENCODER_DAC;
		connector->connector_type = DRM_MODE_CONNECTOR_VGA;
		intel_encoder->clone_mask = (1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
					(1 << INTEL_ANALOG_CLONE_BIT);
	} else if (flags & SDVO_OUTPUT_RGB1) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_RGB1;
		encoder->encoder_type = DRM_MODE_ENCODER_DAC;
		connector->connector_type = DRM_MODE_CONNECTOR_VGA;
		intel_encoder->clone_mask = (1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
					(1 << INTEL_ANALOG_CLONE_BIT);
	} else if (flags & SDVO_OUTPUT_CVBS0) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_CVBS0;
		encoder->encoder_type = DRM_MODE_ENCODER_TVDAC;
		connector->connector_type = DRM_MODE_CONNECTOR_SVIDEO;
		sdvo_priv->is_tv = true;
		intel_encoder->needs_tv_clock = true;
		intel_encoder->clone_mask = 1 << INTEL_SDVO_TV_CLONE_BIT;
	} else if (flags & SDVO_OUTPUT_LVDS0) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_LVDS0;
		encoder->encoder_type = DRM_MODE_ENCODER_LVDS;
		connector->connector_type = DRM_MODE_CONNECTOR_LVDS;
		sdvo_priv->is_lvds = true;
		intel_encoder->clone_mask = (1 << INTEL_ANALOG_CLONE_BIT) |
					(1 << INTEL_SDVO_LVDS_CLONE_BIT);
	} else if (flags & SDVO_OUTPUT_LVDS1) {

		sdvo_priv->controlled_output = SDVO_OUTPUT_LVDS1;
		encoder->encoder_type = DRM_MODE_ENCODER_LVDS;
		connector->connector_type = DRM_MODE_CONNECTOR_LVDS;
		sdvo_priv->is_lvds = true;
		intel_encoder->clone_mask = (1 << INTEL_ANALOG_CLONE_BIT) |
					(1 << INTEL_SDVO_LVDS_CLONE_BIT);
	} else {

		unsigned char bytes[2];

		sdvo_priv->controlled_output = 0;
		memcpy(bytes, &sdvo_priv->caps.output_flags, 2);
		DRM_DEBUG_KMS("%s: Unknown SDVO output type (0x%02x%02x)\n",
			      SDVO_NAME(sdvo_priv),
			      bytes[0], bytes[1]);
		ret = false;
	}
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1);

	if (ret && registered)
		ret = drm_sysfs_connector_add(connector) == 0 ? true : false;


	return ret;

}

static void intel_sdvo_tv_create_property(struct drm_connector *connector)
{
      struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	struct intel_sdvo_tv_format format;
	uint32_t format_map, i;
	uint8_t status;

	intel_sdvo_set_target_output(intel_encoder,
				     sdvo_priv->controlled_output);

	intel_sdvo_write_cmd(intel_encoder,
			     SDVO_CMD_GET_SUPPORTED_TV_FORMATS, NULL, 0);
	status = intel_sdvo_read_response(intel_encoder,
					  &format, sizeof(format));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return;

	memcpy(&format_map, &format, sizeof(format) > sizeof(format_map) ?
	       sizeof(format_map) : sizeof(format));

	if (format_map == 0)
		return;

	sdvo_priv->format_supported_num = 0;
	for (i = 0 ; i < TV_FORMAT_NUM; i++)
		if (format_map & (1 << i)) {
			sdvo_priv->tv_format_supported
			[sdvo_priv->format_supported_num++] =
			tv_format_names[i];
		}


	sdvo_priv->tv_format_property =
			drm_property_create(
				connector->dev, DRM_MODE_PROP_ENUM,
				"mode", sdvo_priv->format_supported_num);

	for (i = 0; i < sdvo_priv->format_supported_num; i++)
		drm_property_add_enum(
				sdvo_priv->tv_format_property, i,
				i, sdvo_priv->tv_format_supported[i]);

	sdvo_priv->tv_format_name = sdvo_priv->tv_format_supported[0];
	drm_connector_attach_property(
			connector, sdvo_priv->tv_format_property, 0);

}

static void intel_sdvo_create_enhance_property(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_encoder->dev_priv;
	struct intel_sdvo_enhancements_reply sdvo_data;
	struct drm_device *dev = connector->dev;
	uint8_t status;
	uint16_t response, data_value[2];

	intel_sdvo_write_cmd(intel_encoder, SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS,
						NULL, 0);
	status = intel_sdvo_read_response(intel_encoder, &sdvo_data,
					sizeof(sdvo_data));
	if (status != SDVO_CMD_STATUS_SUCCESS) {
		DRM_DEBUG_KMS(" incorrect response is returned\n");
		return;
	}
	response = *((uint16_t *)&sdvo_data);
	if (!response) {
		DRM_DEBUG_KMS("No enhancement is supported\n");
		return;
	}
	if (sdvo_priv->is_tv) {
		/* when horizontal overscan is supported, Add the left/right
		 * property
		 */
		if (sdvo_data.overscan_h) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_OVERSCAN_H, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO max "
						"h_overscan\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_OVERSCAN_H, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO h_overscan\n");
				return;
			}
			sdvo_priv->max_hscan = data_value[0];
			sdvo_priv->left_margin = data_value[0] - response;
			sdvo_priv->right_margin = sdvo_priv->left_margin;
			sdvo_priv->left_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"left_margin", 2);
			sdvo_priv->left_property->values[0] = 0;
			sdvo_priv->left_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->left_property,
						sdvo_priv->left_margin);
			sdvo_priv->right_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"right_margin", 2);
			sdvo_priv->right_property->values[0] = 0;
			sdvo_priv->right_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->right_property,
						sdvo_priv->right_margin);
			DRM_DEBUG_KMS("h_overscan: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
		if (sdvo_data.overscan_v) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_OVERSCAN_V, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO max "
						"v_overscan\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_OVERSCAN_V, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO v_overscan\n");
				return;
			}
			sdvo_priv->max_vscan = data_value[0];
			sdvo_priv->top_margin = data_value[0] - response;
			sdvo_priv->bottom_margin = sdvo_priv->top_margin;
			sdvo_priv->top_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"top_margin", 2);
			sdvo_priv->top_property->values[0] = 0;
			sdvo_priv->top_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->top_property,
						sdvo_priv->top_margin);
			sdvo_priv->bottom_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"bottom_margin", 2);
			sdvo_priv->bottom_property->values[0] = 0;
			sdvo_priv->bottom_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->bottom_property,
						sdvo_priv->bottom_margin);
			DRM_DEBUG_KMS("v_overscan: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
		if (sdvo_data.position_h) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_POSITION_H, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max h_pos\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_POSITION_H, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get h_postion\n");
				return;
			}
			sdvo_priv->max_hpos = data_value[0];
			sdvo_priv->cur_hpos = response;
			sdvo_priv->hpos_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"hpos", 2);
			sdvo_priv->hpos_property->values[0] = 0;
			sdvo_priv->hpos_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->hpos_property,
						sdvo_priv->cur_hpos);
			DRM_DEBUG_KMS("h_position: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
		if (sdvo_data.position_v) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_POSITION_V, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max v_pos\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_POSITION_V, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get v_postion\n");
				return;
			}
			sdvo_priv->max_vpos = data_value[0];
			sdvo_priv->cur_vpos = response;
			sdvo_priv->vpos_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"vpos", 2);
			sdvo_priv->vpos_property->values[0] = 0;
			sdvo_priv->vpos_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->vpos_property,
						sdvo_priv->cur_vpos);
			DRM_DEBUG_KMS("v_position: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
	}
	if (sdvo_priv->is_tv) {
		if (sdvo_data.saturation) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_SATURATION, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max sat\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_SATURATION, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get sat\n");
				return;
			}
			sdvo_priv->max_saturation = data_value[0];
			sdvo_priv->cur_saturation = response;
			sdvo_priv->saturation_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"saturation", 2);
			sdvo_priv->saturation_property->values[0] = 0;
			sdvo_priv->saturation_property->values[1] =
							data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->saturation_property,
						sdvo_priv->cur_saturation);
			DRM_DEBUG_KMS("saturation: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
		if (sdvo_data.contrast) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_CONTRAST, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max contrast\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_CONTRAST, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get contrast\n");
				return;
			}
			sdvo_priv->max_contrast = data_value[0];
			sdvo_priv->cur_contrast = response;
			sdvo_priv->contrast_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"contrast", 2);
			sdvo_priv->contrast_property->values[0] = 0;
			sdvo_priv->contrast_property->values[1] = data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->contrast_property,
						sdvo_priv->cur_contrast);
			DRM_DEBUG_KMS("contrast: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
		if (sdvo_data.hue) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_HUE, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max hue\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_HUE, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get hue\n");
				return;
			}
			sdvo_priv->max_hue = data_value[0];
			sdvo_priv->cur_hue = response;
			sdvo_priv->hue_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"hue", 2);
			sdvo_priv->hue_property->values[0] = 0;
			sdvo_priv->hue_property->values[1] =
							data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->hue_property,
						sdvo_priv->cur_hue);
			DRM_DEBUG_KMS("hue: max %d, default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
	}
	if (sdvo_priv->is_tv || sdvo_priv->is_lvds) {
		if (sdvo_data.brightness) {
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_MAX_BRIGHTNESS, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&data_value, 4);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO Max bright\n");
				return;
			}
			intel_sdvo_write_cmd(intel_encoder,
				SDVO_CMD_GET_BRIGHTNESS, NULL, 0);
			status = intel_sdvo_read_response(intel_encoder,
				&response, 2);
			if (status != SDVO_CMD_STATUS_SUCCESS) {
				DRM_DEBUG_KMS("Incorrect SDVO get brigh\n");
				return;
			}
			sdvo_priv->max_brightness = data_value[0];
			sdvo_priv->cur_brightness = response;
			sdvo_priv->brightness_property =
				drm_property_create(dev, DRM_MODE_PROP_RANGE,
						"brightness", 2);
			sdvo_priv->brightness_property->values[0] = 0;
			sdvo_priv->brightness_property->values[1] =
							data_value[0];
			drm_connector_attach_property(connector,
						sdvo_priv->brightness_property,
						sdvo_priv->cur_brightness);
			DRM_DEBUG_KMS("brightness: max %d, "
					"default %d, current %d\n",
					data_value[0], data_value[1], response);
		}
	}
	return;
}

bool intel_sdvo_init(struct drm_device *dev, int sdvo_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder;
	struct intel_sdvo_priv *sdvo_priv;

	u8 ch[0x40];
	int i;

	intel_encoder = kcalloc(sizeof(struct intel_encoder)+sizeof(struct intel_sdvo_priv), 1, GFP_KERNEL);
	if (!intel_encoder) {
		return false;
	}

	sdvo_priv = (struct intel_sdvo_priv *)(intel_encoder + 1);
	sdvo_priv->sdvo_reg = sdvo_reg;

	intel_encoder->dev_priv = sdvo_priv;
	intel_encoder->type = INTEL_OUTPUT_SDVO;

	/* setup the DDC bus. */
	if (sdvo_reg == SDVOB)
		intel_encoder->i2c_bus = intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOB");
	else
		intel_encoder->i2c_bus = intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOC");

	if (!intel_encoder->i2c_bus)
		goto err_inteloutput;

	sdvo_priv->slave_addr = intel_sdvo_get_slave_addr(dev, sdvo_reg);

	/* Save the bit-banging i2c functionality for use by the DDC wrapper */
	intel_sdvo_i2c_bit_algo.functionality = intel_encoder->i2c_bus->algo->functionality;

	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		if (!intel_sdvo_read_byte(intel_encoder, i, &ch[i])) {
			DRM_DEBUG_KMS("No SDVO device found on SDVO%c\n",
					sdvo_reg == SDVOB ? 'B' : 'C');
			goto err_i2c;
		}
	}

	/* setup the DDC bus. */
	if (sdvo_reg == SDVOB) {
		intel_encoder->ddc_bus = intel_i2c_create(dev, GPIOE, "SDVOB DDC BUS");
		sdvo_priv->analog_ddc_bus = intel_i2c_create(dev, GPIOA,
						"SDVOB/VGA DDC BUS");
		dev_priv->hotplug_supported_mask |= SDVOB_HOTPLUG_INT_STATUS;
	} else {
		intel_encoder->ddc_bus = intel_i2c_create(dev, GPIOE, "SDVOC DDC BUS");
		sdvo_priv->analog_ddc_bus = intel_i2c_create(dev, GPIOA,
						"SDVOC/VGA DDC BUS");
		dev_priv->hotplug_supported_mask |= SDVOC_HOTPLUG_INT_STATUS;
	}

	if (intel_encoder->ddc_bus == NULL)
		goto err_i2c;

	/* Wrap with our custom algo which switches to DDC mode */
	intel_encoder->ddc_bus->algo = &intel_sdvo_i2c_bit_algo;

	/* In default case sdvo lvds is false */
	intel_sdvo_get_capabilities(intel_encoder, &sdvo_priv->caps);

	if (intel_sdvo_output_setup(intel_encoder,
				    sdvo_priv->caps.output_flags) != true) {
		DRM_DEBUG_KMS("SDVO output failed to setup on SDVO%c\n",
			  sdvo_reg == SDVOB ? 'B' : 'C');
		goto err_i2c;
	}


	connector = &intel_encoder->base;
	drm_connector_init(dev, connector, &intel_sdvo_connector_funcs,
			   connector->connector_type);

	drm_connector_helper_add(connector, &intel_sdvo_connector_helper_funcs);
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;

	drm_encoder_init(dev, &intel_encoder->enc,
			&intel_sdvo_enc_funcs, intel_encoder->enc.encoder_type);

	drm_encoder_helper_add(&intel_encoder->enc, &intel_sdvo_helper_funcs);

	drm_mode_connector_attach_encoder(&intel_encoder->base, &intel_encoder->enc);
	if (sdvo_priv->is_tv)
		intel_sdvo_tv_create_property(connector);

	if (sdvo_priv->is_tv || sdvo_priv->is_lvds)
		intel_sdvo_create_enhance_property(connector);

	drm_sysfs_connector_add(connector);

	intel_sdvo_select_ddc_bus(sdvo_priv);

	/* Set the input timing to the screen. Assume always input 0. */
	intel_sdvo_set_target_input(intel_encoder, true, false);

	intel_sdvo_get_input_pixel_clock_range(intel_encoder,
					       &sdvo_priv->pixel_clock_min,
					       &sdvo_priv->pixel_clock_max);


	DRM_DEBUG_KMS("%s device VID/DID: %02X:%02X.%02X, "
			"clock range %dMHz - %dMHz, "
			"input 1: %c, input 2: %c, "
			"output 1: %c, output 2: %c\n",
			SDVO_NAME(sdvo_priv),
			sdvo_priv->caps.vendor_id, sdvo_priv->caps.device_id,
			sdvo_priv->caps.device_rev_id,
			sdvo_priv->pixel_clock_min / 1000,
			sdvo_priv->pixel_clock_max / 1000,
			(sdvo_priv->caps.sdvo_inputs_mask & 0x1) ? 'Y' : 'N',
			(sdvo_priv->caps.sdvo_inputs_mask & 0x2) ? 'Y' : 'N',
			/* check currently supported outputs */
			sdvo_priv->caps.output_flags &
			(SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_RGB0) ? 'Y' : 'N',
			sdvo_priv->caps.output_flags &
			(SDVO_OUTPUT_TMDS1 | SDVO_OUTPUT_RGB1) ? 'Y' : 'N');

	return true;

err_i2c:
	if (sdvo_priv->analog_ddc_bus != NULL)
		intel_i2c_destroy(sdvo_priv->analog_ddc_bus);
	if (intel_encoder->ddc_bus != NULL)
		intel_i2c_destroy(intel_encoder->ddc_bus);
	if (intel_encoder->i2c_bus != NULL)
		intel_i2c_destroy(intel_encoder->i2c_bus);
err_inteloutput:
	kfree(intel_encoder);

	return false;
}
