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
#include <linux/export.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_sdvo_regs.h"

#define SDVO_TMDS_MASK (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1)
#define SDVO_RGB_MASK  (SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1)
#define SDVO_LVDS_MASK (SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1)
#define SDVO_TV_MASK   (SDVO_OUTPUT_CVBS0 | SDVO_OUTPUT_SVID0)

#define SDVO_OUTPUT_MASK (SDVO_TMDS_MASK | SDVO_RGB_MASK | SDVO_LVDS_MASK |\
			SDVO_TV_MASK)

#define IS_TV(c)	(c->output_flag & SDVO_TV_MASK)
#define IS_TMDS(c)	(c->output_flag & SDVO_TMDS_MASK)
#define IS_LVDS(c)	(c->output_flag & SDVO_LVDS_MASK)
#define IS_TV_OR_LVDS(c) (c->output_flag & (SDVO_TV_MASK | SDVO_LVDS_MASK))
#define IS_DIGITAL(c) (c->output_flag & (SDVO_TMDS_MASK | SDVO_LVDS_MASK))


static const char *tv_format_names[] = {
	"NTSC_M"   , "NTSC_J"  , "NTSC_443",
	"PAL_B"    , "PAL_D"   , "PAL_G"   ,
	"PAL_H"    , "PAL_I"   , "PAL_M"   ,
	"PAL_N"    , "PAL_NC"  , "PAL_60"  ,
	"SECAM_B"  , "SECAM_D" , "SECAM_G" ,
	"SECAM_K"  , "SECAM_K1", "SECAM_L" ,
	"SECAM_60"
};

#define TV_FORMAT_NUM  (sizeof(tv_format_names) / sizeof(*tv_format_names))

struct intel_sdvo {
	struct intel_encoder base;

	struct i2c_adapter *i2c;
	u8 slave_addr;

	struct i2c_adapter ddc;

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

	/*
	 * Hotplug activation bits for this device
	 */
	uint8_t hotplug_active[2];

	/**
	 * This is used to select the color range of RBG outputs in HDMI mode.
	 * It is only valid when using TMDS encoding and 8 bit per color mode.
	 */
	uint32_t color_range;

	/**
	 * This is set if we're going to treat the device as TV-out.
	 *
	 * While we have these nice friendly flags for output types that ought
	 * to decide this for us, the S-Video output on our HDMI+S-Video card
	 * shows up as RGB1 (VGA).
	 */
	bool is_tv;

	/* This is for current tv format name */
	int tv_format_index;

	/**
	 * This is set if we treat the device as HDMI, instead of DVI.
	 */
	bool is_hdmi;
	bool has_hdmi_monitor;
	bool has_hdmi_audio;

	/**
	 * This is set if we detect output of sdvo device as LVDS and
	 * have a valid fixed mode to use with the panel.
	 */
	bool is_lvds;

	/**
	 * This is sdvo fixed pannel mode pointer
	 */
	struct drm_display_mode *sdvo_lvds_fixed_mode;

	/* DDC bus used by this SDVO encoder */
	uint8_t ddc_bus;

	/* Input timings for adjusted_mode */
	struct intel_sdvo_dtd input_dtd;
};

struct intel_sdvo_connector {
	struct intel_connector base;

	/* Mark the type of connector */
	uint16_t output_flag;

	enum hdmi_force_audio force_audio;

	/* This contains all current supported TV format */
	u8 tv_format_supported[TV_FORMAT_NUM];
	int   format_supported_num;
	struct drm_property *tv_format;

	/* add the property for the SDVO-TV */
	struct drm_property *left;
	struct drm_property *right;
	struct drm_property *top;
	struct drm_property *bottom;
	struct drm_property *hpos;
	struct drm_property *vpos;
	struct drm_property *contrast;
	struct drm_property *saturation;
	struct drm_property *hue;
	struct drm_property *sharpness;
	struct drm_property *flicker_filter;
	struct drm_property *flicker_filter_adaptive;
	struct drm_property *flicker_filter_2d;
	struct drm_property *tv_chroma_filter;
	struct drm_property *tv_luma_filter;
	struct drm_property *dot_crawl;

	/* add the property for the SDVO-TV/LVDS */
	struct drm_property *brightness;

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
	u32	cur_sharpness,	max_sharpness;
	u32	cur_flicker_filter,		max_flicker_filter;
	u32	cur_flicker_filter_adaptive,	max_flicker_filter_adaptive;
	u32	cur_flicker_filter_2d,		max_flicker_filter_2d;
	u32	cur_tv_chroma_filter,	max_tv_chroma_filter;
	u32	cur_tv_luma_filter,	max_tv_luma_filter;
	u32	cur_dot_crawl,	max_dot_crawl;
};

static struct intel_sdvo *to_intel_sdvo(struct drm_encoder *encoder)
{
	return container_of(encoder, struct intel_sdvo, base.base);
}

static struct intel_sdvo *intel_attached_sdvo(struct drm_connector *connector)
{
	return container_of(intel_attached_encoder(connector),
			    struct intel_sdvo, base);
}

static struct intel_sdvo_connector *to_intel_sdvo_connector(struct drm_connector *connector)
{
	return container_of(to_intel_connector(connector), struct intel_sdvo_connector, base);
}

static bool
intel_sdvo_output_setup(struct intel_sdvo *intel_sdvo, uint16_t flags);
static bool
intel_sdvo_tv_create_property(struct intel_sdvo *intel_sdvo,
			      struct intel_sdvo_connector *intel_sdvo_connector,
			      int type);
static bool
intel_sdvo_create_enhance_property(struct intel_sdvo *intel_sdvo,
				   struct intel_sdvo_connector *intel_sdvo_connector);

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
static void intel_sdvo_write_sdvox(struct intel_sdvo *intel_sdvo, u32 val)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 bval = val, cval = val;
	int i;

	if (intel_sdvo->sdvo_reg == PCH_SDVOB) {
		I915_WRITE(intel_sdvo->sdvo_reg, val);
		I915_READ(intel_sdvo->sdvo_reg);
		return;
	}

	if (intel_sdvo->sdvo_reg == SDVOB) {
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

static bool intel_sdvo_read_byte(struct intel_sdvo *intel_sdvo, u8 addr, u8 *ch)
{
	struct i2c_msg msgs[] = {
		{
			.addr = intel_sdvo->slave_addr,
			.flags = 0,
			.len = 1,
			.buf = &addr,
		},
		{
			.addr = intel_sdvo->slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = ch,
		}
	};
	int ret;

	if ((ret = i2c_transfer(intel_sdvo->i2c, msgs, 2)) == 2)
		return true;

	DRM_DEBUG_KMS("i2c transfer returned %d\n", ret);
	return false;
}

#define SDVO_CMD_NAME_ENTRY(cmd) {cmd, #cmd}
/** Mapping of command numbers to names, for debug output */
static const struct _sdvo_cmd_name {
	u8 cmd;
	const char *name;
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
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_HPOS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_HPOS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_HPOS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_VPOS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_VPOS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_VPOS),
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
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_FLICKER_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_FLICKER_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_FLICKER_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_FLICKER_FILTER_ADAPTIVE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_FLICKER_FILTER_ADAPTIVE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_FLICKER_FILTER_ADAPTIVE),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_FLICKER_FILTER_2D),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_FLICKER_FILTER_2D),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_FLICKER_FILTER_2D),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_SHARPNESS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SHARPNESS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_SHARPNESS),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_DOT_CRAWL),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_DOT_CRAWL),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_TV_CHROMA_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TV_CHROMA_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_CHROMA_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_MAX_TV_LUMA_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TV_LUMA_FILTER),
	SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_LUMA_FILTER),

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

#define IS_SDVOB(reg)	(reg == SDVOB || reg == PCH_SDVOB)
#define SDVO_NAME(svdo) (IS_SDVOB((svdo)->sdvo_reg) ? "SDVOB" : "SDVOC")

static void intel_sdvo_debug_write(struct intel_sdvo *intel_sdvo, u8 cmd,
				   const void *args, int args_len)
{
	int i;

	DRM_DEBUG_KMS("%s: W: %02X ",
				SDVO_NAME(intel_sdvo), cmd);
	for (i = 0; i < args_len; i++)
		DRM_LOG_KMS("%02X ", ((u8 *)args)[i]);
	for (; i < 8; i++)
		DRM_LOG_KMS("   ");
	for (i = 0; i < ARRAY_SIZE(sdvo_cmd_names); i++) {
		if (cmd == sdvo_cmd_names[i].cmd) {
			DRM_LOG_KMS("(%s)", sdvo_cmd_names[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(sdvo_cmd_names))
		DRM_LOG_KMS("(%02X)", cmd);
	DRM_LOG_KMS("\n");
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

static bool intel_sdvo_write_cmd(struct intel_sdvo *intel_sdvo, u8 cmd,
				 const void *args, int args_len)
{
	u8 buf[args_len*2 + 2], status;
	struct i2c_msg msgs[args_len + 3];
	int i, ret;

	intel_sdvo_debug_write(intel_sdvo, cmd, args, args_len);

	for (i = 0; i < args_len; i++) {
		msgs[i].addr = intel_sdvo->slave_addr;
		msgs[i].flags = 0;
		msgs[i].len = 2;
		msgs[i].buf = buf + 2 *i;
		buf[2*i + 0] = SDVO_I2C_ARG_0 - i;
		buf[2*i + 1] = ((u8*)args)[i];
	}
	msgs[i].addr = intel_sdvo->slave_addr;
	msgs[i].flags = 0;
	msgs[i].len = 2;
	msgs[i].buf = buf + 2*i;
	buf[2*i + 0] = SDVO_I2C_OPCODE;
	buf[2*i + 1] = cmd;

	/* the following two are to read the response */
	status = SDVO_I2C_CMD_STATUS;
	msgs[i+1].addr = intel_sdvo->slave_addr;
	msgs[i+1].flags = 0;
	msgs[i+1].len = 1;
	msgs[i+1].buf = &status;

	msgs[i+2].addr = intel_sdvo->slave_addr;
	msgs[i+2].flags = I2C_M_RD;
	msgs[i+2].len = 1;
	msgs[i+2].buf = &status;

	ret = i2c_transfer(intel_sdvo->i2c, msgs, i+3);
	if (ret < 0) {
		DRM_DEBUG_KMS("I2c transfer returned %d\n", ret);
		return false;
	}
	if (ret != i+3) {
		/* failure in I2C transfer */
		DRM_DEBUG_KMS("I2c transfer returned %d/%d\n", ret, i+3);
		return false;
	}

	return true;
}

static bool intel_sdvo_read_response(struct intel_sdvo *intel_sdvo,
				     void *response, int response_len)
{
	u8 retry = 5;
	u8 status;
	int i;

	DRM_DEBUG_KMS("%s: R: ", SDVO_NAME(intel_sdvo));

	/*
	 * The documentation states that all commands will be
	 * processed within 15µs, and that we need only poll
	 * the status byte a maximum of 3 times in order for the
	 * command to be complete.
	 *
	 * Check 5 times in case the hardware failed to read the docs.
	 */
	if (!intel_sdvo_read_byte(intel_sdvo,
				  SDVO_I2C_CMD_STATUS,
				  &status))
		goto log_fail;

	while (status == SDVO_CMD_STATUS_PENDING && retry--) {
		udelay(15);
		if (!intel_sdvo_read_byte(intel_sdvo,
					  SDVO_I2C_CMD_STATUS,
					  &status))
			goto log_fail;
	}

	if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
		DRM_LOG_KMS("(%s)", cmd_status_names[status]);
	else
		DRM_LOG_KMS("(??? %d)", status);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		goto log_fail;

	/* Read the command response */
	for (i = 0; i < response_len; i++) {
		if (!intel_sdvo_read_byte(intel_sdvo,
					  SDVO_I2C_RETURN_0 + i,
					  &((u8 *)response)[i]))
			goto log_fail;
		DRM_LOG_KMS(" %02X", ((u8 *)response)[i]);
	}
	DRM_LOG_KMS("\n");
	return true;

log_fail:
	DRM_LOG_KMS("... failed\n");
	return false;
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

static bool intel_sdvo_set_control_bus_switch(struct intel_sdvo *intel_sdvo,
					      u8 ddc_bus)
{
	/* This must be the immediately preceding write before the i2c xfer */
	return intel_sdvo_write_cmd(intel_sdvo,
				    SDVO_CMD_SET_CONTROL_BUS_SWITCH,
				    &ddc_bus, 1);
}

static bool intel_sdvo_set_value(struct intel_sdvo *intel_sdvo, u8 cmd, const void *data, int len)
{
	if (!intel_sdvo_write_cmd(intel_sdvo, cmd, data, len))
		return false;

	return intel_sdvo_read_response(intel_sdvo, NULL, 0);
}

static bool
intel_sdvo_get_value(struct intel_sdvo *intel_sdvo, u8 cmd, void *value, int len)
{
	if (!intel_sdvo_write_cmd(intel_sdvo, cmd, NULL, 0))
		return false;

	return intel_sdvo_read_response(intel_sdvo, value, len);
}

static bool intel_sdvo_set_target_input(struct intel_sdvo *intel_sdvo)
{
	struct intel_sdvo_set_target_input_args targets = {0};
	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_TARGET_INPUT,
				    &targets, sizeof(targets));
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static bool intel_sdvo_get_trained_inputs(struct intel_sdvo *intel_sdvo, bool *input_1, bool *input_2)
{
	struct intel_sdvo_get_trained_inputs_response response;

	BUILD_BUG_ON(sizeof(response) != 1);
	if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_TRAINED_INPUTS,
				  &response, sizeof(response)))
		return false;

	*input_1 = response.input0_trained;
	*input_2 = response.input1_trained;
	return true;
}

static bool intel_sdvo_set_active_outputs(struct intel_sdvo *intel_sdvo,
					  u16 outputs)
{
	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_ACTIVE_OUTPUTS,
				    &outputs, sizeof(outputs));
}

static bool intel_sdvo_set_encoder_power_state(struct intel_sdvo *intel_sdvo,
					       int mode)
{
	u8 state = SDVO_ENCODER_STATE_ON;

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

	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_ENCODER_POWER_STATE, &state, sizeof(state));
}

static bool intel_sdvo_get_input_pixel_clock_range(struct intel_sdvo *intel_sdvo,
						   int *clock_min,
						   int *clock_max)
{
	struct intel_sdvo_pixel_clock_range clocks;

	BUILD_BUG_ON(sizeof(clocks) != 4);
	if (!intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE,
				  &clocks, sizeof(clocks)))
		return false;

	/* Convert the values from units of 10 kHz to kHz. */
	*clock_min = clocks.min * 10;
	*clock_max = clocks.max * 10;
	return true;
}

static bool intel_sdvo_set_target_output(struct intel_sdvo *intel_sdvo,
					 u16 outputs)
{
	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_TARGET_OUTPUT,
				    &outputs, sizeof(outputs));
}

static bool intel_sdvo_set_timing(struct intel_sdvo *intel_sdvo, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_value(intel_sdvo, cmd, &dtd->part1, sizeof(dtd->part1)) &&
		intel_sdvo_set_value(intel_sdvo, cmd + 1, &dtd->part2, sizeof(dtd->part2));
}

static bool intel_sdvo_set_input_timing(struct intel_sdvo *intel_sdvo,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_sdvo,
				     SDVO_CMD_SET_INPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_set_output_timing(struct intel_sdvo *intel_sdvo,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_sdvo,
				     SDVO_CMD_SET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool
intel_sdvo_create_preferred_input_timing(struct intel_sdvo *intel_sdvo,
					 uint16_t clock,
					 uint16_t width,
					 uint16_t height)
{
	struct intel_sdvo_preferred_input_timing_args args;

	memset(&args, 0, sizeof(args));
	args.clock = clock;
	args.width = width;
	args.height = height;
	args.interlace = 0;

	if (intel_sdvo->is_lvds &&
	   (intel_sdvo->sdvo_lvds_fixed_mode->hdisplay != width ||
	    intel_sdvo->sdvo_lvds_fixed_mode->vdisplay != height))
		args.scaled = 1;

	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING,
				    &args, sizeof(args));
}

static bool intel_sdvo_get_preferred_input_timing(struct intel_sdvo *intel_sdvo,
						  struct intel_sdvo_dtd *dtd)
{
	BUILD_BUG_ON(sizeof(dtd->part1) != 8);
	BUILD_BUG_ON(sizeof(dtd->part2) != 8);
	return intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
				    &dtd->part1, sizeof(dtd->part1)) &&
		intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2,
				     &dtd->part2, sizeof(dtd->part2));
}

static bool intel_sdvo_set_clock_rate_mult(struct intel_sdvo *intel_sdvo, u8 val)
{
	return intel_sdvo_set_value(intel_sdvo, SDVO_CMD_SET_CLOCK_RATE_MULT, &val, 1);
}

static void intel_sdvo_get_dtd_from_mode(struct intel_sdvo_dtd *dtd,
					 const struct drm_display_mode *mode)
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
					 const struct intel_sdvo_dtd *dtd)
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

static bool intel_sdvo_check_supp_encode(struct intel_sdvo *intel_sdvo)
{
	struct intel_sdvo_encode encode;

	BUILD_BUG_ON(sizeof(encode) != 2);
	return intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_SUPP_ENCODE,
				  &encode, sizeof(encode));
}

static bool intel_sdvo_set_encode(struct intel_sdvo *intel_sdvo,
				  uint8_t mode)
{
	return intel_sdvo_set_value(intel_sdvo, SDVO_CMD_SET_ENCODE, &mode, 1);
}

static bool intel_sdvo_set_colorimetry(struct intel_sdvo *intel_sdvo,
				       uint8_t mode)
{
	return intel_sdvo_set_value(intel_sdvo, SDVO_CMD_SET_COLORIMETRY, &mode, 1);
}

#if 0
static void intel_sdvo_dump_hdmi_buf(struct intel_sdvo *intel_sdvo)
{
	int i, j;
	uint8_t set_buf_index[2];
	uint8_t av_split;
	uint8_t buf_size;
	uint8_t buf[48];
	uint8_t *pos;

	intel_sdvo_get_value(encoder, SDVO_CMD_GET_HBUF_AV_SPLIT, &av_split, 1);

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

static bool intel_sdvo_set_avi_infoframe(struct intel_sdvo *intel_sdvo)
{
	struct dip_infoframe avi_if = {
		.type = DIP_TYPE_AVI,
		.ver = DIP_VERSION_AVI,
		.len = DIP_LEN_AVI,
	};
	uint8_t tx_rate = SDVO_HBUF_TX_VSYNC;
	uint8_t set_buf_index[2] = { 1, 0 };
	uint64_t *data = (uint64_t *)&avi_if;
	unsigned i;

	intel_dip_infoframe_csum(&avi_if);

	if (!intel_sdvo_set_value(intel_sdvo,
				  SDVO_CMD_SET_HBUF_INDEX,
				  set_buf_index, 2))
		return false;

	for (i = 0; i < sizeof(avi_if); i += 8) {
		if (!intel_sdvo_set_value(intel_sdvo,
					  SDVO_CMD_SET_HBUF_DATA,
					  data, 8))
			return false;
		data++;
	}

	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_HBUF_TXRATE,
				    &tx_rate, 1);
}

static bool intel_sdvo_set_tv_format(struct intel_sdvo *intel_sdvo)
{
	struct intel_sdvo_tv_format format;
	uint32_t format_map;

	format_map = 1 << intel_sdvo->tv_format_index;
	memset(&format, 0, sizeof(format));
	memcpy(&format, &format_map, min(sizeof(format), sizeof(format_map)));

	BUILD_BUG_ON(sizeof(format) != 6);
	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_TV_FORMAT,
				    &format, sizeof(format));
}

static bool
intel_sdvo_set_output_timings_from_mode(struct intel_sdvo *intel_sdvo,
					struct drm_display_mode *mode)
{
	struct intel_sdvo_dtd output_dtd;

	if (!intel_sdvo_set_target_output(intel_sdvo,
					  intel_sdvo->attached_output))
		return false;

	intel_sdvo_get_dtd_from_mode(&output_dtd, mode);
	if (!intel_sdvo_set_output_timing(intel_sdvo, &output_dtd))
		return false;

	return true;
}

static bool
intel_sdvo_set_input_timings_for_mode(struct intel_sdvo *intel_sdvo,
					struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	/* Reset the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		return false;

	if (!intel_sdvo_create_preferred_input_timing(intel_sdvo,
						      mode->clock / 10,
						      mode->hdisplay,
						      mode->vdisplay))
		return false;

	if (!intel_sdvo_get_preferred_input_timing(intel_sdvo,
						   &intel_sdvo->input_dtd))
		return false;

	intel_sdvo_get_mode_from_dtd(adjusted_mode, &intel_sdvo->input_dtd);

	return true;
}

static bool intel_sdvo_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct intel_sdvo *intel_sdvo = to_intel_sdvo(encoder);
	int multiplier;

	/* We need to construct preferred input timings based on our
	 * output timings.  To do that, we have to set the output
	 * timings, even though this isn't really the right place in
	 * the sequence to do it. Oh well.
	 */
	if (intel_sdvo->is_tv) {
		if (!intel_sdvo_set_output_timings_from_mode(intel_sdvo, mode))
			return false;

		(void) intel_sdvo_set_input_timings_for_mode(intel_sdvo,
							     mode,
							     adjusted_mode);
	} else if (intel_sdvo->is_lvds) {
		if (!intel_sdvo_set_output_timings_from_mode(intel_sdvo,
							     intel_sdvo->sdvo_lvds_fixed_mode))
			return false;

		(void) intel_sdvo_set_input_timings_for_mode(intel_sdvo,
							     mode,
							     adjusted_mode);
	}

	/* Make the CRTC code factor in the SDVO pixel multiplier.  The
	 * SDVO device will factor out the multiplier during mode_set.
	 */
	multiplier = intel_sdvo_get_pixel_multiplier(adjusted_mode);
	intel_mode_set_pixel_multiplier(adjusted_mode, multiplier);

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
	struct intel_sdvo *intel_sdvo = to_intel_sdvo(encoder);
	u32 sdvox;
	struct intel_sdvo_in_out_map in_out;
	struct intel_sdvo_dtd input_dtd;
	int pixel_multiplier = intel_mode_get_pixel_multiplier(adjusted_mode);
	int rate;

	if (!mode)
		return;

	/* First, set the input mapping for the first input to our controlled
	 * output. This is only correct if we're a single-input device, in
	 * which case the first input is the output from the appropriate SDVO
	 * channel on the motherboard.  In a two-input device, the first input
	 * will be SDVOB and the second SDVOC.
	 */
	in_out.in0 = intel_sdvo->attached_output;
	in_out.in1 = 0;

	intel_sdvo_set_value(intel_sdvo,
			     SDVO_CMD_SET_IN_OUT_MAP,
			     &in_out, sizeof(in_out));

	/* Set the output timings to the screen */
	if (!intel_sdvo_set_target_output(intel_sdvo,
					  intel_sdvo->attached_output))
		return;

	/* We have tried to get input timing in mode_fixup, and filled into
	 * adjusted_mode.
	 */
	if (intel_sdvo->is_tv || intel_sdvo->is_lvds) {
		input_dtd = intel_sdvo->input_dtd;
	} else {
		/* Set the output timing to the screen */
		if (!intel_sdvo_set_target_output(intel_sdvo,
						  intel_sdvo->attached_output))
			return;

		intel_sdvo_get_dtd_from_mode(&input_dtd, adjusted_mode);
		(void) intel_sdvo_set_output_timing(intel_sdvo, &input_dtd);
	}

	/* Set the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		return;

	if (intel_sdvo->has_hdmi_monitor) {
		intel_sdvo_set_encode(intel_sdvo, SDVO_ENCODE_HDMI);
		intel_sdvo_set_colorimetry(intel_sdvo,
					   SDVO_COLORIMETRY_RGB256);
		intel_sdvo_set_avi_infoframe(intel_sdvo);
	} else
		intel_sdvo_set_encode(intel_sdvo, SDVO_ENCODE_DVI);

	if (intel_sdvo->is_tv &&
	    !intel_sdvo_set_tv_format(intel_sdvo))
		return;

	(void) intel_sdvo_set_input_timing(intel_sdvo, &input_dtd);

	switch (pixel_multiplier) {
	default:
	case 1: rate = SDVO_CLOCK_RATE_MULT_1X; break;
	case 2: rate = SDVO_CLOCK_RATE_MULT_2X; break;
	case 4: rate = SDVO_CLOCK_RATE_MULT_4X; break;
	}
	if (!intel_sdvo_set_clock_rate_mult(intel_sdvo, rate))
		return;

	/* Set the SDVO control regs. */
	if (INTEL_INFO(dev)->gen >= 4) {
		/* The real mode polarity is set by the SDVO commands, using
		 * struct intel_sdvo_dtd. */
		sdvox = SDVO_VSYNC_ACTIVE_HIGH | SDVO_HSYNC_ACTIVE_HIGH;
		if (intel_sdvo->is_hdmi)
			sdvox |= intel_sdvo->color_range;
		if (INTEL_INFO(dev)->gen < 5)
			sdvox |= SDVO_BORDER_ENABLE;
	} else {
		sdvox = I915_READ(intel_sdvo->sdvo_reg);
		switch (intel_sdvo->sdvo_reg) {
		case SDVOB:
			sdvox &= SDVOB_PRESERVE_MASK;
			break;
		case SDVOC:
			sdvox &= SDVOC_PRESERVE_MASK;
			break;
		}
		sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;
	}

	if (INTEL_PCH_TYPE(dev) >= PCH_CPT)
		sdvox |= TRANSCODER_CPT(intel_crtc->pipe);
	else
		sdvox |= TRANSCODER(intel_crtc->pipe);

	if (intel_sdvo->has_hdmi_audio)
		sdvox |= SDVO_AUDIO_ENABLE;

	if (INTEL_INFO(dev)->gen >= 4) {
		/* done in crtc_mode_set as the dpll_md reg must be written early */
	} else if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev)) {
		/* done in crtc_mode_set as it lives inside the dpll register */
	} else {
		sdvox |= (pixel_multiplier - 1) << SDVO_PORT_MULTIPLY_SHIFT;
	}

	if (input_dtd.part2.sdvo_flags & SDVO_NEED_TO_STALL &&
	    INTEL_INFO(dev)->gen < 5)
		sdvox |= SDVO_STALL_SELECT;
	intel_sdvo_write_sdvox(intel_sdvo, sdvox);
}

static void intel_sdvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_sdvo *intel_sdvo = to_intel_sdvo(encoder);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	u32 temp;

	if (mode != DRM_MODE_DPMS_ON) {
		intel_sdvo_set_active_outputs(intel_sdvo, 0);
		if (0)
			intel_sdvo_set_encoder_power_state(intel_sdvo, mode);

		if (mode == DRM_MODE_DPMS_OFF) {
			temp = I915_READ(intel_sdvo->sdvo_reg);
			if ((temp & SDVO_ENABLE) != 0) {
				intel_sdvo_write_sdvox(intel_sdvo, temp & ~SDVO_ENABLE);
			}
		}
	} else {
		bool input1, input2;
		int i;
		u8 status;

		temp = I915_READ(intel_sdvo->sdvo_reg);
		if ((temp & SDVO_ENABLE) == 0)
			intel_sdvo_write_sdvox(intel_sdvo, temp | SDVO_ENABLE);
		for (i = 0; i < 2; i++)
			intel_wait_for_vblank(dev, intel_crtc->pipe);

		status = intel_sdvo_get_trained_inputs(intel_sdvo, &input1, &input2);
		/* Warn if the device reported failure to sync.
		 * A lot of SDVO devices fail to notify of sync, but it's
		 * a given it the status is a success, we succeeded.
		 */
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1) {
			DRM_DEBUG_KMS("First %s output reported failure to "
					"sync\n", SDVO_NAME(intel_sdvo));
		}

		if (0)
			intel_sdvo_set_encoder_power_state(intel_sdvo, mode);
		intel_sdvo_set_active_outputs(intel_sdvo, intel_sdvo->attached_output);
	}
	return;
}

static int intel_sdvo_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (intel_sdvo->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (intel_sdvo->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	if (intel_sdvo->is_lvds) {
		if (mode->hdisplay > intel_sdvo->sdvo_lvds_fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > intel_sdvo->sdvo_lvds_fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	return MODE_OK;
}

static bool intel_sdvo_get_capabilities(struct intel_sdvo *intel_sdvo, struct intel_sdvo_caps *caps)
{
	BUILD_BUG_ON(sizeof(*caps) != 8);
	if (!intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_DEVICE_CAPS,
				  caps, sizeof(*caps)))
		return false;

	DRM_DEBUG_KMS("SDVO capabilities:\n"
		      "  vendor_id: %d\n"
		      "  device_id: %d\n"
		      "  device_rev_id: %d\n"
		      "  sdvo_version_major: %d\n"
		      "  sdvo_version_minor: %d\n"
		      "  sdvo_inputs_mask: %d\n"
		      "  smooth_scaling: %d\n"
		      "  sharp_scaling: %d\n"
		      "  up_scaling: %d\n"
		      "  down_scaling: %d\n"
		      "  stall_support: %d\n"
		      "  output_flags: %d\n",
		      caps->vendor_id,
		      caps->device_id,
		      caps->device_rev_id,
		      caps->sdvo_version_major,
		      caps->sdvo_version_minor,
		      caps->sdvo_inputs_mask,
		      caps->smooth_scaling,
		      caps->sharp_scaling,
		      caps->up_scaling,
		      caps->down_scaling,
		      caps->stall_support,
		      caps->output_flags);

	return true;
}

static int intel_sdvo_supports_hotplug(struct intel_sdvo *intel_sdvo)
{
	u8 response[2];

	return intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_HOT_PLUG_SUPPORT,
				    &response, 2) && response[0];
}

static void intel_sdvo_enable_hotplug(struct intel_encoder *encoder)
{
	struct intel_sdvo *intel_sdvo = to_intel_sdvo(&encoder->base);

	intel_sdvo_write_cmd(intel_sdvo, SDVO_CMD_SET_ACTIVE_HOT_PLUG, &intel_sdvo->hotplug_active, 2);
}

static bool
intel_sdvo_multifunc_encoder(struct intel_sdvo *intel_sdvo)
{
	/* Is there more than one type of output? */
	return hweight16(intel_sdvo->caps.output_flags) > 1;
}

static struct edid *
intel_sdvo_get_edid(struct drm_connector *connector)
{
	struct intel_sdvo *sdvo = intel_attached_sdvo(connector);
	return drm_get_edid(connector, &sdvo->ddc);
}

/* Mac mini hack -- use the same DDC as the analog connector */
static struct edid *
intel_sdvo_get_analog_edid(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;

	return drm_get_edid(connector,
			    &dev_priv->gmbus[dev_priv->crt_ddc_pin].adapter);
}

enum drm_connector_status
intel_sdvo_tmds_sink_detect(struct drm_connector *connector)
{
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	enum drm_connector_status status;
	struct edid *edid;

	edid = intel_sdvo_get_edid(connector);

	if (edid == NULL && intel_sdvo_multifunc_encoder(intel_sdvo)) {
		u8 ddc, saved_ddc = intel_sdvo->ddc_bus;

		/*
		 * Don't use the 1 as the argument of DDC bus switch to get
		 * the EDID. It is used for SDVO SPD ROM.
		 */
		for (ddc = intel_sdvo->ddc_bus >> 1; ddc > 1; ddc >>= 1) {
			intel_sdvo->ddc_bus = ddc;
			edid = intel_sdvo_get_edid(connector);
			if (edid)
				break;
		}
		/*
		 * If we found the EDID on the other bus,
		 * assume that is the correct DDC bus.
		 */
		if (edid == NULL)
			intel_sdvo->ddc_bus = saved_ddc;
	}

	/*
	 * When there is no edid and no monitor is connected with VGA
	 * port, try to use the CRT ddc to read the EDID for DVI-connector.
	 */
	if (edid == NULL)
		edid = intel_sdvo_get_analog_edid(connector);

	status = connector_status_unknown;
	if (edid != NULL) {
		/* DDC bus is shared, match EDID to connector type */
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			status = connector_status_connected;
			if (intel_sdvo->is_hdmi) {
				intel_sdvo->has_hdmi_monitor = drm_detect_hdmi_monitor(edid);
				intel_sdvo->has_hdmi_audio = drm_detect_monitor_audio(edid);
			}
		} else
			status = connector_status_disconnected;
		connector->display_info.raw_edid = NULL;
		kfree(edid);
	}

	if (status == connector_status_connected) {
		struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
		if (intel_sdvo_connector->force_audio != HDMI_AUDIO_AUTO)
			intel_sdvo->has_hdmi_audio = (intel_sdvo_connector->force_audio == HDMI_AUDIO_ON);
	}

	return status;
}

static bool
intel_sdvo_connector_matches_edid(struct intel_sdvo_connector *sdvo,
				  struct edid *edid)
{
	bool monitor_is_digital = !!(edid->input & DRM_EDID_INPUT_DIGITAL);
	bool connector_is_digital = !!IS_DIGITAL(sdvo);

	DRM_DEBUG_KMS("connector_is_digital? %d, monitor_is_digital? %d\n",
		      connector_is_digital, monitor_is_digital);
	return connector_is_digital == monitor_is_digital;
}

static enum drm_connector_status
intel_sdvo_detect(struct drm_connector *connector, bool force)
{
	uint16_t response;
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
	enum drm_connector_status ret;

	if (!intel_sdvo_write_cmd(intel_sdvo,
				  SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0))
		return connector_status_unknown;

	/* add 30ms delay when the output type might be TV */
	if (intel_sdvo->caps.output_flags &
	    (SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_CVBS0))
		mdelay(30);

	if (!intel_sdvo_read_response(intel_sdvo, &response, 2))
		return connector_status_unknown;

	DRM_DEBUG_KMS("SDVO response %d %d [%x]\n",
		      response & 0xff, response >> 8,
		      intel_sdvo_connector->output_flag);

	if (response == 0)
		return connector_status_disconnected;

	intel_sdvo->attached_output = response;

	intel_sdvo->has_hdmi_monitor = false;
	intel_sdvo->has_hdmi_audio = false;

	if ((intel_sdvo_connector->output_flag & response) == 0)
		ret = connector_status_disconnected;
	else if (IS_TMDS(intel_sdvo_connector))
		ret = intel_sdvo_tmds_sink_detect(connector);
	else {
		struct edid *edid;

		/* if we have an edid check it matches the connection */
		edid = intel_sdvo_get_edid(connector);
		if (edid == NULL)
			edid = intel_sdvo_get_analog_edid(connector);
		if (edid != NULL) {
			if (intel_sdvo_connector_matches_edid(intel_sdvo_connector,
							      edid))
				ret = connector_status_connected;
			else
				ret = connector_status_disconnected;

			connector->display_info.raw_edid = NULL;
			kfree(edid);
		} else
			ret = connector_status_connected;
	}

	/* May update encoder flag for like clock for SDVO TV, etc.*/
	if (ret == connector_status_connected) {
		intel_sdvo->is_tv = false;
		intel_sdvo->is_lvds = false;
		intel_sdvo->base.needs_tv_clock = false;

		if (response & SDVO_TV_MASK) {
			intel_sdvo->is_tv = true;
			intel_sdvo->base.needs_tv_clock = true;
		}
		if (response & SDVO_LVDS_MASK)
			intel_sdvo->is_lvds = intel_sdvo->sdvo_lvds_fixed_mode != NULL;
	}

	return ret;
}

static void intel_sdvo_get_ddc_modes(struct drm_connector *connector)
{
	struct edid *edid;

	/* set the bus switch and get the modes */
	edid = intel_sdvo_get_edid(connector);

	/*
	 * Mac mini hack.  On this device, the DVI-I connector shares one DDC
	 * link between analog and digital outputs. So, if the regular SDVO
	 * DDC fails, check to see if the analog output is disconnected, in
	 * which case we'll look there for the digital DDC data.
	 */
	if (edid == NULL)
		edid = intel_sdvo_get_analog_edid(connector);

	if (edid != NULL) {
		if (intel_sdvo_connector_matches_edid(to_intel_sdvo_connector(connector),
						      edid)) {
			drm_mode_connector_update_edid_property(connector, edid);
			drm_add_edid_modes(connector, edid);
		}

		connector->display_info.raw_edid = NULL;
		kfree(edid);
	}
}

/*
 * Set of SDVO TV modes.
 * Note!  This is in reply order (see loop in get_tv_modes).
 * XXX: all 60Hz refresh?
 */
static const struct drm_display_mode sdvo_tv_modes[] = {
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
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	struct intel_sdvo_sdtv_resolution_request tv_res;
	uint32_t reply = 0, format_map = 0;
	int i;

	/* Read the list of supported input resolutions for the selected TV
	 * format.
	 */
	format_map = 1 << intel_sdvo->tv_format_index;
	memcpy(&tv_res, &format_map,
	       min(sizeof(format_map), sizeof(struct intel_sdvo_sdtv_resolution_request)));

	if (!intel_sdvo_set_target_output(intel_sdvo, intel_sdvo->attached_output))
		return;

	BUILD_BUG_ON(sizeof(tv_res) != 3);
	if (!intel_sdvo_write_cmd(intel_sdvo,
				  SDVO_CMD_GET_SDTV_RESOLUTION_SUPPORT,
				  &tv_res, sizeof(tv_res)))
		return;
	if (!intel_sdvo_read_response(intel_sdvo, &reply, 3))
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
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct drm_display_mode *newmode;

	/*
	 * Attempt to get the mode list from DDC.
	 * Assume that the preferred modes are
	 * arranged in priority order.
	 */
	intel_ddc_get_modes(connector, intel_sdvo->i2c);
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
			intel_sdvo->sdvo_lvds_fixed_mode =
				drm_mode_duplicate(connector->dev, newmode);

			drm_mode_set_crtcinfo(intel_sdvo->sdvo_lvds_fixed_mode,
					      0);

			intel_sdvo->is_lvds = true;
			break;
		}
	}

}

static int intel_sdvo_get_modes(struct drm_connector *connector)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);

	if (IS_TV(intel_sdvo_connector))
		intel_sdvo_get_tv_modes(connector);
	else if (IS_LVDS(intel_sdvo_connector))
		intel_sdvo_get_lvds_modes(connector);
	else
		intel_sdvo_get_ddc_modes(connector);

	return !list_empty(&connector->probed_modes);
}

static void
intel_sdvo_destroy_enhance_property(struct drm_connector *connector)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
	struct drm_device *dev = connector->dev;

	if (intel_sdvo_connector->left)
		drm_property_destroy(dev, intel_sdvo_connector->left);
	if (intel_sdvo_connector->right)
		drm_property_destroy(dev, intel_sdvo_connector->right);
	if (intel_sdvo_connector->top)
		drm_property_destroy(dev, intel_sdvo_connector->top);
	if (intel_sdvo_connector->bottom)
		drm_property_destroy(dev, intel_sdvo_connector->bottom);
	if (intel_sdvo_connector->hpos)
		drm_property_destroy(dev, intel_sdvo_connector->hpos);
	if (intel_sdvo_connector->vpos)
		drm_property_destroy(dev, intel_sdvo_connector->vpos);
	if (intel_sdvo_connector->saturation)
		drm_property_destroy(dev, intel_sdvo_connector->saturation);
	if (intel_sdvo_connector->contrast)
		drm_property_destroy(dev, intel_sdvo_connector->contrast);
	if (intel_sdvo_connector->hue)
		drm_property_destroy(dev, intel_sdvo_connector->hue);
	if (intel_sdvo_connector->sharpness)
		drm_property_destroy(dev, intel_sdvo_connector->sharpness);
	if (intel_sdvo_connector->flicker_filter)
		drm_property_destroy(dev, intel_sdvo_connector->flicker_filter);
	if (intel_sdvo_connector->flicker_filter_2d)
		drm_property_destroy(dev, intel_sdvo_connector->flicker_filter_2d);
	if (intel_sdvo_connector->flicker_filter_adaptive)
		drm_property_destroy(dev, intel_sdvo_connector->flicker_filter_adaptive);
	if (intel_sdvo_connector->tv_luma_filter)
		drm_property_destroy(dev, intel_sdvo_connector->tv_luma_filter);
	if (intel_sdvo_connector->tv_chroma_filter)
		drm_property_destroy(dev, intel_sdvo_connector->tv_chroma_filter);
	if (intel_sdvo_connector->dot_crawl)
		drm_property_destroy(dev, intel_sdvo_connector->dot_crawl);
	if (intel_sdvo_connector->brightness)
		drm_property_destroy(dev, intel_sdvo_connector->brightness);
}

static void intel_sdvo_destroy(struct drm_connector *connector)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);

	if (intel_sdvo_connector->tv_format)
		drm_property_destroy(connector->dev,
				     intel_sdvo_connector->tv_format);

	intel_sdvo_destroy_enhance_property(connector);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static bool intel_sdvo_detect_hdmi_audio(struct drm_connector *connector)
{
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	struct edid *edid;
	bool has_audio = false;

	if (!intel_sdvo->is_hdmi)
		return false;

	edid = intel_sdvo_get_edid(connector);
	if (edid != NULL && edid->input & DRM_EDID_INPUT_DIGITAL)
		has_audio = drm_detect_monitor_audio(edid);

	return has_audio;
}

static int
intel_sdvo_set_property(struct drm_connector *connector,
			struct drm_property *property,
			uint64_t val)
{
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	uint16_t temp_value;
	uint8_t cmd;
	int ret;

	ret = drm_connector_property_set_value(connector, property, val);
	if (ret)
		return ret;

	if (property == dev_priv->force_audio_property) {
		int i = val;
		bool has_audio;

		if (i == intel_sdvo_connector->force_audio)
			return 0;

		intel_sdvo_connector->force_audio = i;

		if (i == HDMI_AUDIO_AUTO)
			has_audio = intel_sdvo_detect_hdmi_audio(connector);
		else
			has_audio = (i == HDMI_AUDIO_ON);

		if (has_audio == intel_sdvo->has_hdmi_audio)
			return 0;

		intel_sdvo->has_hdmi_audio = has_audio;
		goto done;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		if (val == !!intel_sdvo->color_range)
			return 0;

		intel_sdvo->color_range = val ? SDVO_COLOR_RANGE_16_235 : 0;
		goto done;
	}

#define CHECK_PROPERTY(name, NAME) \
	if (intel_sdvo_connector->name == property) { \
		if (intel_sdvo_connector->cur_##name == temp_value) return 0; \
		if (intel_sdvo_connector->max_##name < temp_value) return -EINVAL; \
		cmd = SDVO_CMD_SET_##NAME; \
		intel_sdvo_connector->cur_##name = temp_value; \
		goto set_value; \
	}

	if (property == intel_sdvo_connector->tv_format) {
		if (val >= TV_FORMAT_NUM)
			return -EINVAL;

		if (intel_sdvo->tv_format_index ==
		    intel_sdvo_connector->tv_format_supported[val])
			return 0;

		intel_sdvo->tv_format_index = intel_sdvo_connector->tv_format_supported[val];
		goto done;
	} else if (IS_TV_OR_LVDS(intel_sdvo_connector)) {
		temp_value = val;
		if (intel_sdvo_connector->left == property) {
			drm_connector_property_set_value(connector,
							 intel_sdvo_connector->right, val);
			if (intel_sdvo_connector->left_margin == temp_value)
				return 0;

			intel_sdvo_connector->left_margin = temp_value;
			intel_sdvo_connector->right_margin = temp_value;
			temp_value = intel_sdvo_connector->max_hscan -
				intel_sdvo_connector->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
			goto set_value;
		} else if (intel_sdvo_connector->right == property) {
			drm_connector_property_set_value(connector,
							 intel_sdvo_connector->left, val);
			if (intel_sdvo_connector->right_margin == temp_value)
				return 0;

			intel_sdvo_connector->left_margin = temp_value;
			intel_sdvo_connector->right_margin = temp_value;
			temp_value = intel_sdvo_connector->max_hscan -
				intel_sdvo_connector->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
			goto set_value;
		} else if (intel_sdvo_connector->top == property) {
			drm_connector_property_set_value(connector,
							 intel_sdvo_connector->bottom, val);
			if (intel_sdvo_connector->top_margin == temp_value)
				return 0;

			intel_sdvo_connector->top_margin = temp_value;
			intel_sdvo_connector->bottom_margin = temp_value;
			temp_value = intel_sdvo_connector->max_vscan -
				intel_sdvo_connector->top_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_V;
			goto set_value;
		} else if (intel_sdvo_connector->bottom == property) {
			drm_connector_property_set_value(connector,
							 intel_sdvo_connector->top, val);
			if (intel_sdvo_connector->bottom_margin == temp_value)
				return 0;

			intel_sdvo_connector->top_margin = temp_value;
			intel_sdvo_connector->bottom_margin = temp_value;
			temp_value = intel_sdvo_connector->max_vscan -
				intel_sdvo_connector->top_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_V;
			goto set_value;
		}
		CHECK_PROPERTY(hpos, HPOS)
		CHECK_PROPERTY(vpos, VPOS)
		CHECK_PROPERTY(saturation, SATURATION)
		CHECK_PROPERTY(contrast, CONTRAST)
		CHECK_PROPERTY(hue, HUE)
		CHECK_PROPERTY(brightness, BRIGHTNESS)
		CHECK_PROPERTY(sharpness, SHARPNESS)
		CHECK_PROPERTY(flicker_filter, FLICKER_FILTER)
		CHECK_PROPERTY(flicker_filter_2d, FLICKER_FILTER_2D)
		CHECK_PROPERTY(flicker_filter_adaptive, FLICKER_FILTER_ADAPTIVE)
		CHECK_PROPERTY(tv_chroma_filter, TV_CHROMA_FILTER)
		CHECK_PROPERTY(tv_luma_filter, TV_LUMA_FILTER)
		CHECK_PROPERTY(dot_crawl, DOT_CRAWL)
	}

	return -EINVAL; /* unknown property */

set_value:
	if (!intel_sdvo_set_value(intel_sdvo, cmd, &temp_value, 2))
		return -EIO;


done:
	if (intel_sdvo->base.base.crtc) {
		struct drm_crtc *crtc = intel_sdvo->base.base.crtc;
		drm_crtc_helper_set_mode(crtc, &crtc->mode, crtc->x,
					 crtc->y, crtc->fb);
	}

	return 0;
#undef CHECK_PROPERTY
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
	struct intel_sdvo *intel_sdvo = to_intel_sdvo(encoder);

	if (intel_sdvo->sdvo_lvds_fixed_mode != NULL)
		drm_mode_destroy(encoder->dev,
				 intel_sdvo->sdvo_lvds_fixed_mode);

	i2c_del_adapter(&intel_sdvo->ddc);
	intel_encoder_destroy(encoder);
}

static const struct drm_encoder_funcs intel_sdvo_enc_funcs = {
	.destroy = intel_sdvo_enc_destroy,
};

static void
intel_sdvo_guess_ddc_bus(struct intel_sdvo *sdvo)
{
	uint16_t mask = 0;
	unsigned int num_bits;

	/* Make a mask of outputs less than or equal to our own priority in the
	 * list.
	 */
	switch (sdvo->controlled_output) {
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
	mask &= sdvo->caps.output_flags;
	num_bits = hweight16(mask);
	/* If more than 3 outputs, default to DDC bus 3 for now. */
	if (num_bits > 3)
		num_bits = 3;

	/* Corresponds to SDVO_CONTROL_BUS_DDCx */
	sdvo->ddc_bus = 1 << num_bits;
}

/**
 * Choose the appropriate DDC bus for control bus switch command for this
 * SDVO output based on the controlled output.
 *
 * DDC bus number assignment is in a priority order of RGB outputs, then TMDS
 * outputs, then LVDS outputs.
 */
static void
intel_sdvo_select_ddc_bus(struct drm_i915_private *dev_priv,
			  struct intel_sdvo *sdvo, u32 reg)
{
	struct sdvo_device_mapping *mapping;

	if (IS_SDVOB(reg))
		mapping = &(dev_priv->sdvo_mappings[0]);
	else
		mapping = &(dev_priv->sdvo_mappings[1]);

	if (mapping->initialized)
		sdvo->ddc_bus = 1 << ((mapping->ddc_pin & 0xf0) >> 4);
	else
		intel_sdvo_guess_ddc_bus(sdvo);
}

static void
intel_sdvo_select_i2c_bus(struct drm_i915_private *dev_priv,
			  struct intel_sdvo *sdvo, u32 reg)
{
	struct sdvo_device_mapping *mapping;
	u8 pin;

	if (IS_SDVOB(reg))
		mapping = &dev_priv->sdvo_mappings[0];
	else
		mapping = &dev_priv->sdvo_mappings[1];

	pin = GMBUS_PORT_DPB;
	if (mapping->initialized)
		pin = mapping->i2c_pin;

	if (pin < GMBUS_NUM_PORTS) {
		sdvo->i2c = &dev_priv->gmbus[pin].adapter;
		intel_gmbus_set_speed(sdvo->i2c, GMBUS_RATE_1MHZ);
		intel_gmbus_force_bit(sdvo->i2c, true);
	} else {
		sdvo->i2c = &dev_priv->gmbus[GMBUS_PORT_DPB].adapter;
	}
}

static bool
intel_sdvo_is_hdmi_connector(struct intel_sdvo *intel_sdvo, int device)
{
	return intel_sdvo_check_supp_encode(intel_sdvo);
}

static u8
intel_sdvo_get_slave_addr(struct drm_device *dev, int sdvo_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct sdvo_device_mapping *my_mapping, *other_mapping;

	if (IS_SDVOB(sdvo_reg)) {
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
	if (IS_SDVOB(sdvo_reg))
		return 0x70;
	else
		return 0x72;
}

static void
intel_sdvo_connector_init(struct intel_sdvo_connector *connector,
			  struct intel_sdvo *encoder)
{
	drm_connector_init(encoder->base.base.dev,
			   &connector->base.base,
			   &intel_sdvo_connector_funcs,
			   connector->base.base.connector_type);

	drm_connector_helper_add(&connector->base.base,
				 &intel_sdvo_connector_helper_funcs);

	connector->base.base.interlace_allowed = 1;
	connector->base.base.doublescan_allowed = 0;
	connector->base.base.display_info.subpixel_order = SubPixelHorizontalRGB;

	intel_connector_attach_encoder(&connector->base, &encoder->base);
	drm_sysfs_connector_add(&connector->base.base);
}

static void
intel_sdvo_add_hdmi_properties(struct intel_sdvo_connector *connector)
{
	struct drm_device *dev = connector->base.base.dev;

	intel_attach_force_audio_property(&connector->base.base);
	if (INTEL_INFO(dev)->gen >= 4 && IS_MOBILE(dev))
		intel_attach_broadcast_rgb_property(&connector->base.base);
}

static bool
intel_sdvo_dvi_init(struct intel_sdvo *intel_sdvo, int device)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	intel_sdvo_connector = kzalloc(sizeof(struct intel_sdvo_connector), GFP_KERNEL);
	if (!intel_sdvo_connector)
		return false;

	if (device == 0) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_TMDS0;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_TMDS0;
	} else if (device == 1) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_TMDS1;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_TMDS1;
	}

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	if (intel_sdvo_supports_hotplug(intel_sdvo) & (1 << device)) {
		connector->polled = DRM_CONNECTOR_POLL_HPD;
		intel_sdvo->hotplug_active[0] |= 1 << device;
		/* Some SDVO devices have one-shot hotplug interrupts.
		 * Ensure that they get re-enabled when an interrupt happens.
		 */
		intel_encoder->hot_plug = intel_sdvo_enable_hotplug;
		intel_sdvo_enable_hotplug(intel_encoder);
	}
	else
		connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	encoder->encoder_type = DRM_MODE_ENCODER_TMDS;
	connector->connector_type = DRM_MODE_CONNECTOR_DVID;

	if (intel_sdvo_is_hdmi_connector(intel_sdvo, device)) {
		connector->connector_type = DRM_MODE_CONNECTOR_HDMIA;
		intel_sdvo->is_hdmi = true;
	}
	intel_sdvo->base.clone_mask = ((1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
				       (1 << INTEL_ANALOG_CLONE_BIT));

	intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo);
	if (intel_sdvo->is_hdmi)
		intel_sdvo_add_hdmi_properties(intel_sdvo_connector);

	return true;
}

static bool
intel_sdvo_tv_init(struct intel_sdvo *intel_sdvo, int type)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	intel_sdvo_connector = kzalloc(sizeof(struct intel_sdvo_connector), GFP_KERNEL);
	if (!intel_sdvo_connector)
		return false;

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	encoder->encoder_type = DRM_MODE_ENCODER_TVDAC;
	connector->connector_type = DRM_MODE_CONNECTOR_SVIDEO;

	intel_sdvo->controlled_output |= type;
	intel_sdvo_connector->output_flag = type;

	intel_sdvo->is_tv = true;
	intel_sdvo->base.needs_tv_clock = true;
	intel_sdvo->base.clone_mask = 1 << INTEL_SDVO_TV_CLONE_BIT;

	intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo);

	if (!intel_sdvo_tv_create_property(intel_sdvo, intel_sdvo_connector, type))
		goto err;

	if (!intel_sdvo_create_enhance_property(intel_sdvo, intel_sdvo_connector))
		goto err;

	return true;

err:
	intel_sdvo_destroy(connector);
	return false;
}

static bool
intel_sdvo_analog_init(struct intel_sdvo *intel_sdvo, int device)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	intel_sdvo_connector = kzalloc(sizeof(struct intel_sdvo_connector), GFP_KERNEL);
	if (!intel_sdvo_connector)
		return false;

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	encoder->encoder_type = DRM_MODE_ENCODER_DAC;
	connector->connector_type = DRM_MODE_CONNECTOR_VGA;

	if (device == 0) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB0;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB0;
	} else if (device == 1) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB1;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB1;
	}

	intel_sdvo->base.clone_mask = ((1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
				       (1 << INTEL_ANALOG_CLONE_BIT));

	intel_sdvo_connector_init(intel_sdvo_connector,
				  intel_sdvo);
	return true;
}

static bool
intel_sdvo_lvds_init(struct intel_sdvo *intel_sdvo, int device)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	intel_sdvo_connector = kzalloc(sizeof(struct intel_sdvo_connector), GFP_KERNEL);
	if (!intel_sdvo_connector)
		return false;

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	encoder->encoder_type = DRM_MODE_ENCODER_LVDS;
	connector->connector_type = DRM_MODE_CONNECTOR_LVDS;

	if (device == 0) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_LVDS0;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_LVDS0;
	} else if (device == 1) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_LVDS1;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_LVDS1;
	}

	intel_sdvo->base.clone_mask = ((1 << INTEL_ANALOG_CLONE_BIT) |
				       (1 << INTEL_SDVO_LVDS_CLONE_BIT));

	intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo);
	if (!intel_sdvo_create_enhance_property(intel_sdvo, intel_sdvo_connector))
		goto err;

	return true;

err:
	intel_sdvo_destroy(connector);
	return false;
}

static bool
intel_sdvo_output_setup(struct intel_sdvo *intel_sdvo, uint16_t flags)
{
	intel_sdvo->is_tv = false;
	intel_sdvo->base.needs_tv_clock = false;
	intel_sdvo->is_lvds = false;

	/* SDVO requires XXX1 function may not exist unless it has XXX0 function.*/

	if (flags & SDVO_OUTPUT_TMDS0)
		if (!intel_sdvo_dvi_init(intel_sdvo, 0))
			return false;

	if ((flags & SDVO_TMDS_MASK) == SDVO_TMDS_MASK)
		if (!intel_sdvo_dvi_init(intel_sdvo, 1))
			return false;

	/* TV has no XXX1 function block */
	if (flags & SDVO_OUTPUT_SVID0)
		if (!intel_sdvo_tv_init(intel_sdvo, SDVO_OUTPUT_SVID0))
			return false;

	if (flags & SDVO_OUTPUT_CVBS0)
		if (!intel_sdvo_tv_init(intel_sdvo, SDVO_OUTPUT_CVBS0))
			return false;

	if (flags & SDVO_OUTPUT_RGB0)
		if (!intel_sdvo_analog_init(intel_sdvo, 0))
			return false;

	if ((flags & SDVO_RGB_MASK) == SDVO_RGB_MASK)
		if (!intel_sdvo_analog_init(intel_sdvo, 1))
			return false;

	if (flags & SDVO_OUTPUT_LVDS0)
		if (!intel_sdvo_lvds_init(intel_sdvo, 0))
			return false;

	if ((flags & SDVO_LVDS_MASK) == SDVO_LVDS_MASK)
		if (!intel_sdvo_lvds_init(intel_sdvo, 1))
			return false;

	if ((flags & SDVO_OUTPUT_MASK) == 0) {
		unsigned char bytes[2];

		intel_sdvo->controlled_output = 0;
		memcpy(bytes, &intel_sdvo->caps.output_flags, 2);
		DRM_DEBUG_KMS("%s: Unknown SDVO output type (0x%02x%02x)\n",
			      SDVO_NAME(intel_sdvo),
			      bytes[0], bytes[1]);
		return false;
	}
	intel_sdvo->base.crtc_mask = (1 << 0) | (1 << 1) | (1 << 2);

	return true;
}

static bool intel_sdvo_tv_create_property(struct intel_sdvo *intel_sdvo,
					  struct intel_sdvo_connector *intel_sdvo_connector,
					  int type)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct intel_sdvo_tv_format format;
	uint32_t format_map, i;

	if (!intel_sdvo_set_target_output(intel_sdvo, type))
		return false;

	BUILD_BUG_ON(sizeof(format) != 6);
	if (!intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_SUPPORTED_TV_FORMATS,
				  &format, sizeof(format)))
		return false;

	memcpy(&format_map, &format, min(sizeof(format_map), sizeof(format)));

	if (format_map == 0)
		return false;

	intel_sdvo_connector->format_supported_num = 0;
	for (i = 0 ; i < TV_FORMAT_NUM; i++)
		if (format_map & (1 << i))
			intel_sdvo_connector->tv_format_supported[intel_sdvo_connector->format_supported_num++] = i;


	intel_sdvo_connector->tv_format =
			drm_property_create(dev, DRM_MODE_PROP_ENUM,
					    "mode", intel_sdvo_connector->format_supported_num);
	if (!intel_sdvo_connector->tv_format)
		return false;

	for (i = 0; i < intel_sdvo_connector->format_supported_num; i++)
		drm_property_add_enum(
				intel_sdvo_connector->tv_format, i,
				i, tv_format_names[intel_sdvo_connector->tv_format_supported[i]]);

	intel_sdvo->tv_format_index = intel_sdvo_connector->tv_format_supported[0];
	drm_connector_attach_property(&intel_sdvo_connector->base.base,
				      intel_sdvo_connector->tv_format, 0);
	return true;

}

#define ENHANCEMENT(name, NAME) do { \
	if (enhancements.name) { \
		if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_MAX_##NAME, &data_value, 4) || \
		    !intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_##NAME, &response, 2)) \
			return false; \
		intel_sdvo_connector->max_##name = data_value[0]; \
		intel_sdvo_connector->cur_##name = response; \
		intel_sdvo_connector->name = \
			drm_property_create_range(dev, 0, #name, 0, data_value[0]); \
		if (!intel_sdvo_connector->name) return false; \
		drm_connector_attach_property(connector, \
					      intel_sdvo_connector->name, \
					      intel_sdvo_connector->cur_##name); \
		DRM_DEBUG_KMS(#name ": max %d, default %d, current %d\n", \
			      data_value[0], data_value[1], response); \
	} \
} while (0)

static bool
intel_sdvo_create_enhance_property_tv(struct intel_sdvo *intel_sdvo,
				      struct intel_sdvo_connector *intel_sdvo_connector,
				      struct intel_sdvo_enhancements_reply enhancements)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct drm_connector *connector = &intel_sdvo_connector->base.base;
	uint16_t response, data_value[2];

	/* when horizontal overscan is supported, Add the left/right  property */
	if (enhancements.overscan_h) {
		if (!intel_sdvo_get_value(intel_sdvo,
					  SDVO_CMD_GET_MAX_OVERSCAN_H,
					  &data_value, 4))
			return false;

		if (!intel_sdvo_get_value(intel_sdvo,
					  SDVO_CMD_GET_OVERSCAN_H,
					  &response, 2))
			return false;

		intel_sdvo_connector->max_hscan = data_value[0];
		intel_sdvo_connector->left_margin = data_value[0] - response;
		intel_sdvo_connector->right_margin = intel_sdvo_connector->left_margin;
		intel_sdvo_connector->left =
			drm_property_create_range(dev, 0, "left_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->left)
			return false;

		drm_connector_attach_property(connector,
					      intel_sdvo_connector->left,
					      intel_sdvo_connector->left_margin);

		intel_sdvo_connector->right =
			drm_property_create_range(dev, 0, "right_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->right)
			return false;

		drm_connector_attach_property(connector,
					      intel_sdvo_connector->right,
					      intel_sdvo_connector->right_margin);
		DRM_DEBUG_KMS("h_overscan: max %d, "
			      "default %d, current %d\n",
			      data_value[0], data_value[1], response);
	}

	if (enhancements.overscan_v) {
		if (!intel_sdvo_get_value(intel_sdvo,
					  SDVO_CMD_GET_MAX_OVERSCAN_V,
					  &data_value, 4))
			return false;

		if (!intel_sdvo_get_value(intel_sdvo,
					  SDVO_CMD_GET_OVERSCAN_V,
					  &response, 2))
			return false;

		intel_sdvo_connector->max_vscan = data_value[0];
		intel_sdvo_connector->top_margin = data_value[0] - response;
		intel_sdvo_connector->bottom_margin = intel_sdvo_connector->top_margin;
		intel_sdvo_connector->top =
			drm_property_create_range(dev, 0,
					    "top_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->top)
			return false;

		drm_connector_attach_property(connector,
					      intel_sdvo_connector->top,
					      intel_sdvo_connector->top_margin);

		intel_sdvo_connector->bottom =
			drm_property_create_range(dev, 0,
					    "bottom_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->bottom)
			return false;

		drm_connector_attach_property(connector,
					      intel_sdvo_connector->bottom,
					      intel_sdvo_connector->bottom_margin);
		DRM_DEBUG_KMS("v_overscan: max %d, "
			      "default %d, current %d\n",
			      data_value[0], data_value[1], response);
	}

	ENHANCEMENT(hpos, HPOS);
	ENHANCEMENT(vpos, VPOS);
	ENHANCEMENT(saturation, SATURATION);
	ENHANCEMENT(contrast, CONTRAST);
	ENHANCEMENT(hue, HUE);
	ENHANCEMENT(sharpness, SHARPNESS);
	ENHANCEMENT(brightness, BRIGHTNESS);
	ENHANCEMENT(flicker_filter, FLICKER_FILTER);
	ENHANCEMENT(flicker_filter_adaptive, FLICKER_FILTER_ADAPTIVE);
	ENHANCEMENT(flicker_filter_2d, FLICKER_FILTER_2D);
	ENHANCEMENT(tv_chroma_filter, TV_CHROMA_FILTER);
	ENHANCEMENT(tv_luma_filter, TV_LUMA_FILTER);

	if (enhancements.dot_crawl) {
		if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_DOT_CRAWL, &response, 2))
			return false;

		intel_sdvo_connector->max_dot_crawl = 1;
		intel_sdvo_connector->cur_dot_crawl = response & 0x1;
		intel_sdvo_connector->dot_crawl =
			drm_property_create_range(dev, 0, "dot_crawl", 0, 1);
		if (!intel_sdvo_connector->dot_crawl)
			return false;

		drm_connector_attach_property(connector,
					      intel_sdvo_connector->dot_crawl,
					      intel_sdvo_connector->cur_dot_crawl);
		DRM_DEBUG_KMS("dot crawl: current %d\n", response);
	}

	return true;
}

static bool
intel_sdvo_create_enhance_property_lvds(struct intel_sdvo *intel_sdvo,
					struct intel_sdvo_connector *intel_sdvo_connector,
					struct intel_sdvo_enhancements_reply enhancements)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct drm_connector *connector = &intel_sdvo_connector->base.base;
	uint16_t response, data_value[2];

	ENHANCEMENT(brightness, BRIGHTNESS);

	return true;
}
#undef ENHANCEMENT

static bool intel_sdvo_create_enhance_property(struct intel_sdvo *intel_sdvo,
					       struct intel_sdvo_connector *intel_sdvo_connector)
{
	union {
		struct intel_sdvo_enhancements_reply reply;
		uint16_t response;
	} enhancements;

	BUILD_BUG_ON(sizeof(enhancements) != 2);

	enhancements.response = 0;
	intel_sdvo_get_value(intel_sdvo,
			     SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS,
			     &enhancements, sizeof(enhancements));
	if (enhancements.response == 0) {
		DRM_DEBUG_KMS("No enhancement is supported\n");
		return true;
	}

	if (IS_TV(intel_sdvo_connector))
		return intel_sdvo_create_enhance_property_tv(intel_sdvo, intel_sdvo_connector, enhancements.reply);
	else if (IS_LVDS(intel_sdvo_connector))
		return intel_sdvo_create_enhance_property_lvds(intel_sdvo, intel_sdvo_connector, enhancements.reply);
	else
		return true;
}

static int intel_sdvo_ddc_proxy_xfer(struct i2c_adapter *adapter,
				     struct i2c_msg *msgs,
				     int num)
{
	struct intel_sdvo *sdvo = adapter->algo_data;

	if (!intel_sdvo_set_control_bus_switch(sdvo, sdvo->ddc_bus))
		return -EIO;

	return sdvo->i2c->algo->master_xfer(sdvo->i2c, msgs, num);
}

static u32 intel_sdvo_ddc_proxy_func(struct i2c_adapter *adapter)
{
	struct intel_sdvo *sdvo = adapter->algo_data;
	return sdvo->i2c->algo->functionality(sdvo->i2c);
}

static const struct i2c_algorithm intel_sdvo_ddc_proxy = {
	.master_xfer	= intel_sdvo_ddc_proxy_xfer,
	.functionality	= intel_sdvo_ddc_proxy_func
};

static bool
intel_sdvo_init_ddc_proxy(struct intel_sdvo *sdvo,
			  struct drm_device *dev)
{
	sdvo->ddc.owner = THIS_MODULE;
	sdvo->ddc.class = I2C_CLASS_DDC;
	snprintf(sdvo->ddc.name, I2C_NAME_SIZE, "SDVO DDC proxy");
	sdvo->ddc.dev.parent = &dev->pdev->dev;
	sdvo->ddc.algo_data = sdvo;
	sdvo->ddc.algo = &intel_sdvo_ddc_proxy;

	return i2c_add_adapter(&sdvo->ddc) == 0;
}

bool intel_sdvo_init(struct drm_device *dev, int sdvo_reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder;
	struct intel_sdvo *intel_sdvo;
	int i;

	intel_sdvo = kzalloc(sizeof(struct intel_sdvo), GFP_KERNEL);
	if (!intel_sdvo)
		return false;

	intel_sdvo->sdvo_reg = sdvo_reg;
	intel_sdvo->slave_addr = intel_sdvo_get_slave_addr(dev, sdvo_reg) >> 1;
	intel_sdvo_select_i2c_bus(dev_priv, intel_sdvo, sdvo_reg);
	if (!intel_sdvo_init_ddc_proxy(intel_sdvo, dev)) {
		kfree(intel_sdvo);
		return false;
	}

	/* encoder type will be decided later */
	intel_encoder = &intel_sdvo->base;
	intel_encoder->type = INTEL_OUTPUT_SDVO;
	drm_encoder_init(dev, &intel_encoder->base, &intel_sdvo_enc_funcs, 0);

	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		u8 byte;

		if (!intel_sdvo_read_byte(intel_sdvo, i, &byte)) {
			DRM_DEBUG_KMS("No SDVO device found on SDVO%c\n",
				      IS_SDVOB(sdvo_reg) ? 'B' : 'C');
			goto err;
		}
	}

	if (IS_SDVOB(sdvo_reg))
		dev_priv->hotplug_supported_mask |= SDVOB_HOTPLUG_INT_STATUS;
	else
		dev_priv->hotplug_supported_mask |= SDVOC_HOTPLUG_INT_STATUS;

	drm_encoder_helper_add(&intel_encoder->base, &intel_sdvo_helper_funcs);

	/* In default case sdvo lvds is false */
	if (!intel_sdvo_get_capabilities(intel_sdvo, &intel_sdvo->caps))
		goto err;

	/* Set up hotplug command - note paranoia about contents of reply.
	 * We assume that the hardware is in a sane state, and only touch
	 * the bits we think we understand.
	 */
	intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_ACTIVE_HOT_PLUG,
			     &intel_sdvo->hotplug_active, 2);
	intel_sdvo->hotplug_active[0] &= ~0x3;

	if (intel_sdvo_output_setup(intel_sdvo,
				    intel_sdvo->caps.output_flags) != true) {
		DRM_DEBUG_KMS("SDVO output failed to setup on SDVO%c\n",
			      IS_SDVOB(sdvo_reg) ? 'B' : 'C');
		goto err;
	}

	intel_sdvo_select_ddc_bus(dev_priv, intel_sdvo, sdvo_reg);

	/* Set the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		goto err;

	if (!intel_sdvo_get_input_pixel_clock_range(intel_sdvo,
						    &intel_sdvo->pixel_clock_min,
						    &intel_sdvo->pixel_clock_max))
		goto err;

	DRM_DEBUG_KMS("%s device VID/DID: %02X:%02X.%02X, "
			"clock range %dMHz - %dMHz, "
			"input 1: %c, input 2: %c, "
			"output 1: %c, output 2: %c\n",
			SDVO_NAME(intel_sdvo),
			intel_sdvo->caps.vendor_id, intel_sdvo->caps.device_id,
			intel_sdvo->caps.device_rev_id,
			intel_sdvo->pixel_clock_min / 1000,
			intel_sdvo->pixel_clock_max / 1000,
			(intel_sdvo->caps.sdvo_inputs_mask & 0x1) ? 'Y' : 'N',
			(intel_sdvo->caps.sdvo_inputs_mask & 0x2) ? 'Y' : 'N',
			/* check currently supported outputs */
			intel_sdvo->caps.output_flags &
			(SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_RGB0) ? 'Y' : 'N',
			intel_sdvo->caps.output_flags &
			(SDVO_OUTPUT_TMDS1 | SDVO_OUTPUT_RGB1) ? 'Y' : 'N');
	return true;

err:
	drm_encoder_cleanup(&intel_encoder->base);
	i2c_del_adapter(&intel_sdvo->ddc);
	kfree(intel_sdvo);

	return false;
}
