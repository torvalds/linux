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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_sdvo_regs.h"

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


static const char *tv_format_names[] = {
	"NTSC_M"   , "NTSC_J"  , "NTSC_443",
	"PAL_B"    , "PAL_D"   , "PAL_G"   ,
	"PAL_H"    , "PAL_I"   , "PAL_M"   ,
	"PAL_N"    , "PAL_NC"  , "PAL_60"  ,
	"SECAM_B"  , "SECAM_D" , "SECAM_G" ,
	"SECAM_K"  , "SECAM_K1", "SECAM_L" ,
	"SECAM_60"
};

struct psb_intel_sdvo {
	struct gma_encoder base;

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
	struct psb_intel_sdvo_caps caps;

	/* Pixel clock limitations reported by the SDVO device, in kHz */
	int pixel_clock_min, pixel_clock_max;

	/*
	* For multiple function SDVO device,
	* this is for current attached outputs.
	*/
	uint16_t attached_output;

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
	struct psb_intel_sdvo_dtd input_dtd;

	/* Saved SDVO output states */
	uint32_t saveSDVO; /* Can be SDVOB or SDVOC depending on sdvo_reg */
};

struct psb_intel_sdvo_connector {
	struct gma_connector base;

	/* Mark the type of connector */
	uint16_t output_flag;

	int force_audio;

	/* This contains all current supported TV format */
	u8 tv_format_supported[ARRAY_SIZE(tv_format_names)];
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

static struct psb_intel_sdvo *to_psb_intel_sdvo(struct drm_encoder *encoder)
{
	return container_of(encoder, struct psb_intel_sdvo, base.base);
}

static struct psb_intel_sdvo *intel_attached_sdvo(struct drm_connector *connector)
{
	return container_of(gma_attached_encoder(connector),
			    struct psb_intel_sdvo, base);
}

static struct psb_intel_sdvo_connector *to_psb_intel_sdvo_connector(struct drm_connector *connector)
{
	return container_of(to_gma_connector(connector), struct psb_intel_sdvo_connector, base);
}

static bool
psb_intel_sdvo_output_setup(struct psb_intel_sdvo *psb_intel_sdvo, uint16_t flags);
static bool
psb_intel_sdvo_tv_create_property(struct psb_intel_sdvo *psb_intel_sdvo,
			      struct psb_intel_sdvo_connector *psb_intel_sdvo_connector,
			      int type);
static bool
psb_intel_sdvo_create_enhance_property(struct psb_intel_sdvo *psb_intel_sdvo,
				   struct psb_intel_sdvo_connector *psb_intel_sdvo_connector);

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
static void psb_intel_sdvo_write_sdvox(struct psb_intel_sdvo *psb_intel_sdvo, u32 val)
{
	struct drm_device *dev = psb_intel_sdvo->base.base.dev;
	u32 bval = val, cval = val;
	int i, j;
	int need_aux = IS_MRST(dev) ? 1 : 0;

	for (j = 0; j <= need_aux; j++) {
		if (psb_intel_sdvo->sdvo_reg == SDVOB)
			cval = REG_READ_WITH_AUX(SDVOC, j);
		else
			bval = REG_READ_WITH_AUX(SDVOB, j);

		/*
		* Write the registers twice for luck. Sometimes,
		* writing them only once doesn't appear to 'stick'.
		* The BIOS does this too. Yay, magic
		*/
		for (i = 0; i < 2; i++) {
			REG_WRITE_WITH_AUX(SDVOB, bval, j);
			REG_READ_WITH_AUX(SDVOB, j);
			REG_WRITE_WITH_AUX(SDVOC, cval, j);
			REG_READ_WITH_AUX(SDVOC, j);
		}
	}
}

static bool psb_intel_sdvo_read_byte(struct psb_intel_sdvo *psb_intel_sdvo, u8 addr, u8 *ch)
{
	struct i2c_msg msgs[] = {
		{
			.addr = psb_intel_sdvo->slave_addr,
			.flags = 0,
			.len = 1,
			.buf = &addr,
		},
		{
			.addr = psb_intel_sdvo->slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = ch,
		}
	};
	int ret;

	if ((ret = i2c_transfer(psb_intel_sdvo->i2c, msgs, 2)) == 2)
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

#define IS_SDVOB(reg)	(reg == SDVOB)
#define SDVO_NAME(svdo) (IS_SDVOB((svdo)->sdvo_reg) ? "SDVOB" : "SDVOC")

static void psb_intel_sdvo_debug_write(struct psb_intel_sdvo *psb_intel_sdvo, u8 cmd,
				   const void *args, int args_len)
{
	int i;

	DRM_DEBUG_KMS("%s: W: %02X ",
				SDVO_NAME(psb_intel_sdvo), cmd);
	for (i = 0; i < args_len; i++)
		DRM_DEBUG_KMS("%02X ", ((u8 *)args)[i]);
	for (; i < 8; i++)
		DRM_DEBUG_KMS("   ");
	for (i = 0; i < ARRAY_SIZE(sdvo_cmd_names); i++) {
		if (cmd == sdvo_cmd_names[i].cmd) {
			DRM_DEBUG_KMS("(%s)", sdvo_cmd_names[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(sdvo_cmd_names))
		DRM_DEBUG_KMS("(%02X)", cmd);
	DRM_DEBUG_KMS("\n");
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

#define MAX_ARG_LEN 32

static bool psb_intel_sdvo_write_cmd(struct psb_intel_sdvo *psb_intel_sdvo, u8 cmd,
				 const void *args, int args_len)
{
	u8 buf[MAX_ARG_LEN*2 + 2], status;
	struct i2c_msg msgs[MAX_ARG_LEN + 3];
	int i, ret;

	if (args_len > MAX_ARG_LEN) {
		DRM_ERROR("Need to increase arg length\n");
		return false;
	}

	psb_intel_sdvo_debug_write(psb_intel_sdvo, cmd, args, args_len);

	for (i = 0; i < args_len; i++) {
		msgs[i].addr = psb_intel_sdvo->slave_addr;
		msgs[i].flags = 0;
		msgs[i].len = 2;
		msgs[i].buf = buf + 2 *i;
		buf[2*i + 0] = SDVO_I2C_ARG_0 - i;
		buf[2*i + 1] = ((u8*)args)[i];
	}
	msgs[i].addr = psb_intel_sdvo->slave_addr;
	msgs[i].flags = 0;
	msgs[i].len = 2;
	msgs[i].buf = buf + 2*i;
	buf[2*i + 0] = SDVO_I2C_OPCODE;
	buf[2*i + 1] = cmd;

	/* the following two are to read the response */
	status = SDVO_I2C_CMD_STATUS;
	msgs[i+1].addr = psb_intel_sdvo->slave_addr;
	msgs[i+1].flags = 0;
	msgs[i+1].len = 1;
	msgs[i+1].buf = &status;

	msgs[i+2].addr = psb_intel_sdvo->slave_addr;
	msgs[i+2].flags = I2C_M_RD;
	msgs[i+2].len = 1;
	msgs[i+2].buf = &status;

	ret = i2c_transfer(psb_intel_sdvo->i2c, msgs, i+3);
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

static bool psb_intel_sdvo_read_response(struct psb_intel_sdvo *psb_intel_sdvo,
				     void *response, int response_len)
{
	u8 retry = 5;
	u8 status;
	int i;

	DRM_DEBUG_KMS("%s: R: ", SDVO_NAME(psb_intel_sdvo));

	/*
	 * The documentation states that all commands will be
	 * processed within 15µs, and that we need only poll
	 * the status byte a maximum of 3 times in order for the
	 * command to be complete.
	 *
	 * Check 5 times in case the hardware failed to read the docs.
	 */
	if (!psb_intel_sdvo_read_byte(psb_intel_sdvo,
				  SDVO_I2C_CMD_STATUS,
				  &status))
		goto log_fail;

	while ((status == SDVO_CMD_STATUS_PENDING ||
		status == SDVO_CMD_STATUS_TARGET_NOT_SPECIFIED) && retry--) {
		udelay(15);
		if (!psb_intel_sdvo_read_byte(psb_intel_sdvo,
					  SDVO_I2C_CMD_STATUS,
					  &status))
			goto log_fail;
	}

	if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
		DRM_DEBUG_KMS("(%s)", cmd_status_names[status]);
	else
		DRM_DEBUG_KMS("(??? %d)", status);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		goto log_fail;

	/* Read the command response */
	for (i = 0; i < response_len; i++) {
		if (!psb_intel_sdvo_read_byte(psb_intel_sdvo,
					  SDVO_I2C_RETURN_0 + i,
					  &((u8 *)response)[i]))
			goto log_fail;
		DRM_DEBUG_KMS(" %02X", ((u8 *)response)[i]);
	}
	DRM_DEBUG_KMS("\n");
	return true;

log_fail:
	DRM_DEBUG_KMS("... failed\n");
	return false;
}

static int psb_intel_sdvo_get_pixel_multiplier(struct drm_display_mode *mode)
{
	if (mode->clock >= 100000)
		return 1;
	else if (mode->clock >= 50000)
		return 2;
	else
		return 4;
}

static bool psb_intel_sdvo_set_control_bus_switch(struct psb_intel_sdvo *psb_intel_sdvo,
					      u8 ddc_bus)
{
	/* This must be the immediately preceding write before the i2c xfer */
	return psb_intel_sdvo_write_cmd(psb_intel_sdvo,
				    SDVO_CMD_SET_CONTROL_BUS_SWITCH,
				    &ddc_bus, 1);
}

static bool psb_intel_sdvo_set_value(struct psb_intel_sdvo *psb_intel_sdvo, u8 cmd, const void *data, int len)
{
	if (!psb_intel_sdvo_write_cmd(psb_intel_sdvo, cmd, data, len))
		return false;

	return psb_intel_sdvo_read_response(psb_intel_sdvo, NULL, 0);
}

static bool
psb_intel_sdvo_get_value(struct psb_intel_sdvo *psb_intel_sdvo, u8 cmd, void *value, int len)
{
	if (!psb_intel_sdvo_write_cmd(psb_intel_sdvo, cmd, NULL, 0))
		return false;

	return psb_intel_sdvo_read_response(psb_intel_sdvo, value, len);
}

static bool psb_intel_sdvo_set_target_input(struct psb_intel_sdvo *psb_intel_sdvo)
{
	struct psb_intel_sdvo_set_target_input_args targets = {0};
	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_SET_TARGET_INPUT,
				    &targets, sizeof(targets));
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static bool psb_intel_sdvo_get_trained_inputs(struct psb_intel_sdvo *psb_intel_sdvo, bool *input_1, bool *input_2)
{
	struct psb_intel_sdvo_get_trained_inputs_response response;

	BUILD_BUG_ON(sizeof(response) != 1);
	if (!psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_TRAINED_INPUTS,
				  &response, sizeof(response)))
		return false;

	*input_1 = response.input0_trained;
	*input_2 = response.input1_trained;
	return true;
}

static bool psb_intel_sdvo_set_active_outputs(struct psb_intel_sdvo *psb_intel_sdvo,
					  u16 outputs)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_SET_ACTIVE_OUTPUTS,
				    &outputs, sizeof(outputs));
}

static bool psb_intel_sdvo_set_encoder_power_state(struct psb_intel_sdvo *psb_intel_sdvo,
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

	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_SET_ENCODER_POWER_STATE, &state, sizeof(state));
}

static bool psb_intel_sdvo_get_input_pixel_clock_range(struct psb_intel_sdvo *psb_intel_sdvo,
						   int *clock_min,
						   int *clock_max)
{
	struct psb_intel_sdvo_pixel_clock_range clocks;

	BUILD_BUG_ON(sizeof(clocks) != 4);
	if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
				  SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE,
				  &clocks, sizeof(clocks)))
		return false;

	/* Convert the values from units of 10 kHz to kHz. */
	*clock_min = clocks.min * 10;
	*clock_max = clocks.max * 10;
	return true;
}

static bool psb_intel_sdvo_set_target_output(struct psb_intel_sdvo *psb_intel_sdvo,
					 u16 outputs)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_SET_TARGET_OUTPUT,
				    &outputs, sizeof(outputs));
}

static bool psb_intel_sdvo_set_timing(struct psb_intel_sdvo *psb_intel_sdvo, u8 cmd,
				  struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo, cmd, &dtd->part1, sizeof(dtd->part1)) &&
		psb_intel_sdvo_set_value(psb_intel_sdvo, cmd + 1, &dtd->part2, sizeof(dtd->part2));
}

static bool psb_intel_sdvo_set_input_timing(struct psb_intel_sdvo *psb_intel_sdvo,
					 struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_set_timing(psb_intel_sdvo,
				     SDVO_CMD_SET_INPUT_TIMINGS_PART1, dtd);
}

static bool psb_intel_sdvo_set_output_timing(struct psb_intel_sdvo *psb_intel_sdvo,
					 struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_set_timing(psb_intel_sdvo,
				     SDVO_CMD_SET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool
psb_intel_sdvo_create_preferred_input_timing(struct psb_intel_sdvo *psb_intel_sdvo,
					 uint16_t clock,
					 uint16_t width,
					 uint16_t height)
{
	struct psb_intel_sdvo_preferred_input_timing_args args;

	memset(&args, 0, sizeof(args));
	args.clock = clock;
	args.width = width;
	args.height = height;
	args.interlace = 0;

	if (psb_intel_sdvo->is_lvds &&
	   (psb_intel_sdvo->sdvo_lvds_fixed_mode->hdisplay != width ||
	    psb_intel_sdvo->sdvo_lvds_fixed_mode->vdisplay != height))
		args.scaled = 1;

	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING,
				    &args, sizeof(args));
}

static bool psb_intel_sdvo_get_preferred_input_timing(struct psb_intel_sdvo *psb_intel_sdvo,
						  struct psb_intel_sdvo_dtd *dtd)
{
	BUILD_BUG_ON(sizeof(dtd->part1) != 8);
	BUILD_BUG_ON(sizeof(dtd->part2) != 8);
	return psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
				    &dtd->part1, sizeof(dtd->part1)) &&
		psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2,
				     &dtd->part2, sizeof(dtd->part2));
}

static bool psb_intel_sdvo_set_clock_rate_mult(struct psb_intel_sdvo *psb_intel_sdvo, u8 val)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo, SDVO_CMD_SET_CLOCK_RATE_MULT, &val, 1);
}

static void psb_intel_sdvo_get_dtd_from_mode(struct psb_intel_sdvo_dtd *dtd,
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

static void psb_intel_sdvo_get_mode_from_dtd(struct drm_display_mode * mode,
					 const struct psb_intel_sdvo_dtd *dtd)
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

static bool psb_intel_sdvo_check_supp_encode(struct psb_intel_sdvo *psb_intel_sdvo)
{
	struct psb_intel_sdvo_encode encode;

	BUILD_BUG_ON(sizeof(encode) != 2);
	return psb_intel_sdvo_get_value(psb_intel_sdvo,
				  SDVO_CMD_GET_SUPP_ENCODE,
				  &encode, sizeof(encode));
}

static bool psb_intel_sdvo_set_encode(struct psb_intel_sdvo *psb_intel_sdvo,
				  uint8_t mode)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo, SDVO_CMD_SET_ENCODE, &mode, 1);
}

static bool psb_intel_sdvo_set_colorimetry(struct psb_intel_sdvo *psb_intel_sdvo,
				       uint8_t mode)
{
	return psb_intel_sdvo_set_value(psb_intel_sdvo, SDVO_CMD_SET_COLORIMETRY, &mode, 1);
}

#if 0
static void psb_intel_sdvo_dump_hdmi_buf(struct psb_intel_sdvo *psb_intel_sdvo)
{
	int i, j;
	uint8_t set_buf_index[2];
	uint8_t av_split;
	uint8_t buf_size;
	uint8_t buf[48];
	uint8_t *pos;

	psb_intel_sdvo_get_value(encoder, SDVO_CMD_GET_HBUF_AV_SPLIT, &av_split, 1);

	for (i = 0; i <= av_split; i++) {
		set_buf_index[0] = i; set_buf_index[1] = 0;
		psb_intel_sdvo_write_cmd(encoder, SDVO_CMD_SET_HBUF_INDEX,
				     set_buf_index, 2);
		psb_intel_sdvo_write_cmd(encoder, SDVO_CMD_GET_HBUF_INFO, NULL, 0);
		psb_intel_sdvo_read_response(encoder, &buf_size, 1);

		pos = buf;
		for (j = 0; j <= buf_size; j += 8) {
			psb_intel_sdvo_write_cmd(encoder, SDVO_CMD_GET_HBUF_DATA,
					     NULL, 0);
			psb_intel_sdvo_read_response(encoder, pos, 8);
			pos += 8;
		}
	}
}
#endif

static bool psb_intel_sdvo_set_avi_infoframe(struct psb_intel_sdvo *psb_intel_sdvo)
{
	DRM_INFO("HDMI is not supported yet");

	return false;
}

static bool psb_intel_sdvo_set_tv_format(struct psb_intel_sdvo *psb_intel_sdvo)
{
	struct psb_intel_sdvo_tv_format format;
	uint32_t format_map;

	format_map = 1 << psb_intel_sdvo->tv_format_index;
	memset(&format, 0, sizeof(format));
	memcpy(&format, &format_map, min(sizeof(format), sizeof(format_map)));

	BUILD_BUG_ON(sizeof(format) != 6);
	return psb_intel_sdvo_set_value(psb_intel_sdvo,
				    SDVO_CMD_SET_TV_FORMAT,
				    &format, sizeof(format));
}

static bool
psb_intel_sdvo_set_output_timings_from_mode(struct psb_intel_sdvo *psb_intel_sdvo,
					const struct drm_display_mode *mode)
{
	struct psb_intel_sdvo_dtd output_dtd;

	if (!psb_intel_sdvo_set_target_output(psb_intel_sdvo,
					  psb_intel_sdvo->attached_output))
		return false;

	psb_intel_sdvo_get_dtd_from_mode(&output_dtd, mode);
	if (!psb_intel_sdvo_set_output_timing(psb_intel_sdvo, &output_dtd))
		return false;

	return true;
}

static bool
psb_intel_sdvo_set_input_timings_for_mode(struct psb_intel_sdvo *psb_intel_sdvo,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	/* Reset the input timing to the screen. Assume always input 0. */
	if (!psb_intel_sdvo_set_target_input(psb_intel_sdvo))
		return false;

	if (!psb_intel_sdvo_create_preferred_input_timing(psb_intel_sdvo,
						      mode->clock / 10,
						      mode->hdisplay,
						      mode->vdisplay))
		return false;

	if (!psb_intel_sdvo_get_preferred_input_timing(psb_intel_sdvo,
						   &psb_intel_sdvo->input_dtd))
		return false;

	psb_intel_sdvo_get_mode_from_dtd(adjusted_mode, &psb_intel_sdvo->input_dtd);

	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

static bool psb_intel_sdvo_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct psb_intel_sdvo *psb_intel_sdvo = to_psb_intel_sdvo(encoder);
	int multiplier;

	/* We need to construct preferred input timings based on our
	 * output timings.  To do that, we have to set the output
	 * timings, even though this isn't really the right place in
	 * the sequence to do it. Oh well.
	 */
	if (psb_intel_sdvo->is_tv) {
		if (!psb_intel_sdvo_set_output_timings_from_mode(psb_intel_sdvo, mode))
			return false;

		(void) psb_intel_sdvo_set_input_timings_for_mode(psb_intel_sdvo,
							     mode,
							     adjusted_mode);
	} else if (psb_intel_sdvo->is_lvds) {
		if (!psb_intel_sdvo_set_output_timings_from_mode(psb_intel_sdvo,
							     psb_intel_sdvo->sdvo_lvds_fixed_mode))
			return false;

		(void) psb_intel_sdvo_set_input_timings_for_mode(psb_intel_sdvo,
							     mode,
							     adjusted_mode);
	}

	/* Make the CRTC code factor in the SDVO pixel multiplier.  The
	 * SDVO device will factor out the multiplier during mode_set.
	 */
	multiplier = psb_intel_sdvo_get_pixel_multiplier(adjusted_mode);
	psb_intel_mode_set_pixel_multiplier(adjusted_mode, multiplier);

	return true;
}

static void psb_intel_sdvo_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	struct psb_intel_sdvo *psb_intel_sdvo = to_psb_intel_sdvo(encoder);
	u32 sdvox;
	struct psb_intel_sdvo_in_out_map in_out;
	struct psb_intel_sdvo_dtd input_dtd;
	int pixel_multiplier = psb_intel_mode_get_pixel_multiplier(adjusted_mode);
	int rate;
	int need_aux = IS_MRST(dev) ? 1 : 0;

	if (!mode)
		return;

	/* First, set the input mapping for the first input to our controlled
	 * output. This is only correct if we're a single-input device, in
	 * which case the first input is the output from the appropriate SDVO
	 * channel on the motherboard.  In a two-input device, the first input
	 * will be SDVOB and the second SDVOC.
	 */
	in_out.in0 = psb_intel_sdvo->attached_output;
	in_out.in1 = 0;

	psb_intel_sdvo_set_value(psb_intel_sdvo,
			     SDVO_CMD_SET_IN_OUT_MAP,
			     &in_out, sizeof(in_out));

	/* Set the output timings to the screen */
	if (!psb_intel_sdvo_set_target_output(psb_intel_sdvo,
					  psb_intel_sdvo->attached_output))
		return;

	/* We have tried to get input timing in mode_fixup, and filled into
	 * adjusted_mode.
	 */
	if (psb_intel_sdvo->is_tv || psb_intel_sdvo->is_lvds) {
		input_dtd = psb_intel_sdvo->input_dtd;
	} else {
		/* Set the output timing to the screen */
		if (!psb_intel_sdvo_set_target_output(psb_intel_sdvo,
						  psb_intel_sdvo->attached_output))
			return;

		psb_intel_sdvo_get_dtd_from_mode(&input_dtd, adjusted_mode);
		(void) psb_intel_sdvo_set_output_timing(psb_intel_sdvo, &input_dtd);
	}

	/* Set the input timing to the screen. Assume always input 0. */
	if (!psb_intel_sdvo_set_target_input(psb_intel_sdvo))
		return;

	if (psb_intel_sdvo->has_hdmi_monitor) {
		psb_intel_sdvo_set_encode(psb_intel_sdvo, SDVO_ENCODE_HDMI);
		psb_intel_sdvo_set_colorimetry(psb_intel_sdvo,
					   SDVO_COLORIMETRY_RGB256);
		psb_intel_sdvo_set_avi_infoframe(psb_intel_sdvo);
	} else
		psb_intel_sdvo_set_encode(psb_intel_sdvo, SDVO_ENCODE_DVI);

	if (psb_intel_sdvo->is_tv &&
	    !psb_intel_sdvo_set_tv_format(psb_intel_sdvo))
		return;

	(void) psb_intel_sdvo_set_input_timing(psb_intel_sdvo, &input_dtd);

	switch (pixel_multiplier) {
	default:
	case 1: rate = SDVO_CLOCK_RATE_MULT_1X; break;
	case 2: rate = SDVO_CLOCK_RATE_MULT_2X; break;
	case 4: rate = SDVO_CLOCK_RATE_MULT_4X; break;
	}
	if (!psb_intel_sdvo_set_clock_rate_mult(psb_intel_sdvo, rate))
		return;

	/* Set the SDVO control regs. */
	if (need_aux)
		sdvox = REG_READ_AUX(psb_intel_sdvo->sdvo_reg);
	else
		sdvox = REG_READ(psb_intel_sdvo->sdvo_reg);

	switch (psb_intel_sdvo->sdvo_reg) {
	case SDVOB:
		sdvox &= SDVOB_PRESERVE_MASK;
		break;
	case SDVOC:
		sdvox &= SDVOC_PRESERVE_MASK;
		break;
	}
	sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;

	if (gma_crtc->pipe == 1)
		sdvox |= SDVO_PIPE_B_SELECT;
	if (psb_intel_sdvo->has_hdmi_audio)
		sdvox |= SDVO_AUDIO_ENABLE;

	/* FIXME: Check if this is needed for PSB
	sdvox |= (pixel_multiplier - 1) << SDVO_PORT_MULTIPLY_SHIFT;
	*/

	if (input_dtd.part2.sdvo_flags & SDVO_NEED_TO_STALL)
		sdvox |= SDVO_STALL_SELECT;
	psb_intel_sdvo_write_sdvox(psb_intel_sdvo, sdvox);
}

static void psb_intel_sdvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct psb_intel_sdvo *psb_intel_sdvo = to_psb_intel_sdvo(encoder);
	u32 temp;
	int i;
	int need_aux = IS_MRST(dev) ? 1 : 0;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		DRM_DEBUG("DPMS_ON");
		break;
	case DRM_MODE_DPMS_OFF:
		DRM_DEBUG("DPMS_OFF");
		break;
	default:
		DRM_DEBUG("DPMS: %d", mode);
	}

	if (mode != DRM_MODE_DPMS_ON) {
		psb_intel_sdvo_set_active_outputs(psb_intel_sdvo, 0);
		if (0)
			psb_intel_sdvo_set_encoder_power_state(psb_intel_sdvo, mode);

		if (mode == DRM_MODE_DPMS_OFF) {
			if (need_aux)
				temp = REG_READ_AUX(psb_intel_sdvo->sdvo_reg);
			else
				temp = REG_READ(psb_intel_sdvo->sdvo_reg);

			if ((temp & SDVO_ENABLE) != 0) {
				psb_intel_sdvo_write_sdvox(psb_intel_sdvo, temp & ~SDVO_ENABLE);
			}
		}
	} else {
		bool input1, input2;
		u8 status;

		if (need_aux)
			temp = REG_READ_AUX(psb_intel_sdvo->sdvo_reg);
		else
			temp = REG_READ(psb_intel_sdvo->sdvo_reg);

		if ((temp & SDVO_ENABLE) == 0)
			psb_intel_sdvo_write_sdvox(psb_intel_sdvo, temp | SDVO_ENABLE);

		for (i = 0; i < 2; i++)
			gma_wait_for_vblank(dev);

		status = psb_intel_sdvo_get_trained_inputs(psb_intel_sdvo, &input1, &input2);
		/* Warn if the device reported failure to sync.
		 * A lot of SDVO devices fail to notify of sync, but it's
		 * a given it the status is a success, we succeeded.
		 */
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1) {
			DRM_DEBUG_KMS("First %s output reported failure to "
					"sync\n", SDVO_NAME(psb_intel_sdvo));
		}

		if (0)
			psb_intel_sdvo_set_encoder_power_state(psb_intel_sdvo, mode);
		psb_intel_sdvo_set_active_outputs(psb_intel_sdvo, psb_intel_sdvo->attached_output);
	}
	return;
}

static enum drm_mode_status psb_intel_sdvo_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (psb_intel_sdvo->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (psb_intel_sdvo->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	if (psb_intel_sdvo->is_lvds) {
		if (mode->hdisplay > psb_intel_sdvo->sdvo_lvds_fixed_mode->hdisplay)
			return MODE_PANEL;

		if (mode->vdisplay > psb_intel_sdvo->sdvo_lvds_fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	return MODE_OK;
}

static bool psb_intel_sdvo_get_capabilities(struct psb_intel_sdvo *psb_intel_sdvo, struct psb_intel_sdvo_caps *caps)
{
	BUILD_BUG_ON(sizeof(*caps) != 8);
	if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
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

static bool
psb_intel_sdvo_multifunc_encoder(struct psb_intel_sdvo *psb_intel_sdvo)
{
	/* Is there more than one type of output? */
	int caps = psb_intel_sdvo->caps.output_flags & 0xf;
	return caps & -caps;
}

static struct edid *
psb_intel_sdvo_get_edid(struct drm_connector *connector)
{
	struct psb_intel_sdvo *sdvo = intel_attached_sdvo(connector);
	return drm_get_edid(connector, &sdvo->ddc);
}

/* Mac mini hack -- use the same DDC as the analog connector */
static struct edid *
psb_intel_sdvo_get_analog_edid(struct drm_connector *connector)
{
	struct drm_psb_private *dev_priv = connector->dev->dev_private;

	return drm_get_edid(connector,
			    &dev_priv->gmbus[dev_priv->crt_ddc_pin].adapter);
}

static enum drm_connector_status
psb_intel_sdvo_hdmi_sink_detect(struct drm_connector *connector)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	enum drm_connector_status status;
	struct edid *edid;

	edid = psb_intel_sdvo_get_edid(connector);

	if (edid == NULL && psb_intel_sdvo_multifunc_encoder(psb_intel_sdvo)) {
		u8 ddc, saved_ddc = psb_intel_sdvo->ddc_bus;

		/*
		 * Don't use the 1 as the argument of DDC bus switch to get
		 * the EDID. It is used for SDVO SPD ROM.
		 */
		for (ddc = psb_intel_sdvo->ddc_bus >> 1; ddc > 1; ddc >>= 1) {
			psb_intel_sdvo->ddc_bus = ddc;
			edid = psb_intel_sdvo_get_edid(connector);
			if (edid)
				break;
		}
		/*
		 * If we found the EDID on the other bus,
		 * assume that is the correct DDC bus.
		 */
		if (edid == NULL)
			psb_intel_sdvo->ddc_bus = saved_ddc;
	}

	/*
	 * When there is no edid and no monitor is connected with VGA
	 * port, try to use the CRT ddc to read the EDID for DVI-connector.
	 */
	if (edid == NULL)
		edid = psb_intel_sdvo_get_analog_edid(connector);

	status = connector_status_unknown;
	if (edid != NULL) {
		/* DDC bus is shared, match EDID to connector type */
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			status = connector_status_connected;
			if (psb_intel_sdvo->is_hdmi) {
				psb_intel_sdvo->has_hdmi_monitor = drm_detect_hdmi_monitor(edid);
				psb_intel_sdvo->has_hdmi_audio = drm_detect_monitor_audio(edid);
			}
		} else
			status = connector_status_disconnected;
		kfree(edid);
	}

	if (status == connector_status_connected) {
		struct psb_intel_sdvo_connector *psb_intel_sdvo_connector = to_psb_intel_sdvo_connector(connector);
		if (psb_intel_sdvo_connector->force_audio)
			psb_intel_sdvo->has_hdmi_audio = psb_intel_sdvo_connector->force_audio > 0;
	}

	return status;
}

static enum drm_connector_status
psb_intel_sdvo_detect(struct drm_connector *connector, bool force)
{
	uint16_t response;
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector = to_psb_intel_sdvo_connector(connector);
	enum drm_connector_status ret;

	if (!psb_intel_sdvo_write_cmd(psb_intel_sdvo,
				  SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0))
		return connector_status_unknown;

	/* add 30ms delay when the output type might be TV */
	if (psb_intel_sdvo->caps.output_flags &
	    (SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_CVBS0))
		mdelay(30);

	if (!psb_intel_sdvo_read_response(psb_intel_sdvo, &response, 2))
		return connector_status_unknown;

	DRM_DEBUG_KMS("SDVO response %d %d [%x]\n",
		      response & 0xff, response >> 8,
		      psb_intel_sdvo_connector->output_flag);

	if (response == 0)
		return connector_status_disconnected;

	psb_intel_sdvo->attached_output = response;

	psb_intel_sdvo->has_hdmi_monitor = false;
	psb_intel_sdvo->has_hdmi_audio = false;

	if ((psb_intel_sdvo_connector->output_flag & response) == 0)
		ret = connector_status_disconnected;
	else if (IS_TMDS(psb_intel_sdvo_connector))
		ret = psb_intel_sdvo_hdmi_sink_detect(connector);
	else {
		struct edid *edid;

		/* if we have an edid check it matches the connection */
		edid = psb_intel_sdvo_get_edid(connector);
		if (edid == NULL)
			edid = psb_intel_sdvo_get_analog_edid(connector);
		if (edid != NULL) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL)
				ret = connector_status_disconnected;
			else
				ret = connector_status_connected;
			kfree(edid);
		} else
			ret = connector_status_connected;
	}

	/* May update encoder flag for like clock for SDVO TV, etc.*/
	if (ret == connector_status_connected) {
		psb_intel_sdvo->is_tv = false;
		psb_intel_sdvo->is_lvds = false;
		psb_intel_sdvo->base.needs_tv_clock = false;

		if (response & SDVO_TV_MASK) {
			psb_intel_sdvo->is_tv = true;
			psb_intel_sdvo->base.needs_tv_clock = true;
		}
		if (response & SDVO_LVDS_MASK)
			psb_intel_sdvo->is_lvds = psb_intel_sdvo->sdvo_lvds_fixed_mode != NULL;
	}

	return ret;
}

static void psb_intel_sdvo_get_ddc_modes(struct drm_connector *connector)
{
	struct edid *edid;

	/* set the bus switch and get the modes */
	edid = psb_intel_sdvo_get_edid(connector);

	/*
	 * Mac mini hack.  On this device, the DVI-I connector shares one DDC
	 * link between analog and digital outputs. So, if the regular SDVO
	 * DDC fails, check to see if the analog output is disconnected, in
	 * which case we'll look there for the digital DDC data.
	 */
	if (edid == NULL)
		edid = psb_intel_sdvo_get_analog_edid(connector);

	if (edid != NULL) {
		struct psb_intel_sdvo_connector *psb_intel_sdvo_connector = to_psb_intel_sdvo_connector(connector);
		bool monitor_is_digital = !!(edid->input & DRM_EDID_INPUT_DIGITAL);
		bool connector_is_digital = !!IS_TMDS(psb_intel_sdvo_connector);

		if (connector_is_digital == monitor_is_digital) {
			drm_connector_update_edid_property(connector, edid);
			drm_add_edid_modes(connector, edid);
		}

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

static void psb_intel_sdvo_get_tv_modes(struct drm_connector *connector)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	struct psb_intel_sdvo_sdtv_resolution_request tv_res;
	uint32_t reply = 0, format_map = 0;
	int i;

	/* Read the list of supported input resolutions for the selected TV
	 * format.
	 */
	format_map = 1 << psb_intel_sdvo->tv_format_index;
	memcpy(&tv_res, &format_map,
	       min(sizeof(format_map), sizeof(struct psb_intel_sdvo_sdtv_resolution_request)));

	if (!psb_intel_sdvo_set_target_output(psb_intel_sdvo, psb_intel_sdvo->attached_output))
		return;

	BUILD_BUG_ON(sizeof(tv_res) != 3);
	if (!psb_intel_sdvo_write_cmd(psb_intel_sdvo,
				  SDVO_CMD_GET_SDTV_RESOLUTION_SUPPORT,
				  &tv_res, sizeof(tv_res)))
		return;
	if (!psb_intel_sdvo_read_response(psb_intel_sdvo, &reply, 3))
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

static void psb_intel_sdvo_get_lvds_modes(struct drm_connector *connector)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	struct drm_psb_private *dev_priv = connector->dev->dev_private;
	struct drm_display_mode *newmode;

	/*
	 * Attempt to get the mode list from DDC.
	 * Assume that the preferred modes are
	 * arranged in priority order.
	 */
	psb_intel_ddc_get_modes(connector, psb_intel_sdvo->i2c);
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
			psb_intel_sdvo->sdvo_lvds_fixed_mode =
				drm_mode_duplicate(connector->dev, newmode);

			drm_mode_set_crtcinfo(psb_intel_sdvo->sdvo_lvds_fixed_mode,
					      0);

			psb_intel_sdvo->is_lvds = true;
			break;
		}
	}

}

static int psb_intel_sdvo_get_modes(struct drm_connector *connector)
{
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector = to_psb_intel_sdvo_connector(connector);

	if (IS_TV(psb_intel_sdvo_connector))
		psb_intel_sdvo_get_tv_modes(connector);
	else if (IS_LVDS(psb_intel_sdvo_connector))
		psb_intel_sdvo_get_lvds_modes(connector);
	else
		psb_intel_sdvo_get_ddc_modes(connector);

	return !list_empty(&connector->probed_modes);
}

static void psb_intel_sdvo_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static bool psb_intel_sdvo_detect_hdmi_audio(struct drm_connector *connector)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	struct edid *edid;
	bool has_audio = false;

	if (!psb_intel_sdvo->is_hdmi)
		return false;

	edid = psb_intel_sdvo_get_edid(connector);
	if (edid != NULL && edid->input & DRM_EDID_INPUT_DIGITAL)
		has_audio = drm_detect_monitor_audio(edid);

	return has_audio;
}

static int
psb_intel_sdvo_set_property(struct drm_connector *connector,
			struct drm_property *property,
			uint64_t val)
{
	struct psb_intel_sdvo *psb_intel_sdvo = intel_attached_sdvo(connector);
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector = to_psb_intel_sdvo_connector(connector);
	struct drm_psb_private *dev_priv = connector->dev->dev_private;
	uint16_t temp_value;
	uint8_t cmd;
	int ret;

	ret = drm_object_property_set_value(&connector->base, property, val);
	if (ret)
		return ret;

	if (property == dev_priv->force_audio_property) {
		int i = val;
		bool has_audio;

		if (i == psb_intel_sdvo_connector->force_audio)
			return 0;

		psb_intel_sdvo_connector->force_audio = i;

		if (i == 0)
			has_audio = psb_intel_sdvo_detect_hdmi_audio(connector);
		else
			has_audio = i > 0;

		if (has_audio == psb_intel_sdvo->has_hdmi_audio)
			return 0;

		psb_intel_sdvo->has_hdmi_audio = has_audio;
		goto done;
	}

	if (property == dev_priv->broadcast_rgb_property) {
		if (val == !!psb_intel_sdvo->color_range)
			return 0;

		psb_intel_sdvo->color_range = val ? SDVO_COLOR_RANGE_16_235 : 0;
		goto done;
	}

#define CHECK_PROPERTY(name, NAME) \
	if (psb_intel_sdvo_connector->name == property) { \
		if (psb_intel_sdvo_connector->cur_##name == temp_value) return 0; \
		if (psb_intel_sdvo_connector->max_##name < temp_value) return -EINVAL; \
		cmd = SDVO_CMD_SET_##NAME; \
		psb_intel_sdvo_connector->cur_##name = temp_value; \
		goto set_value; \
	}

	if (property == psb_intel_sdvo_connector->tv_format) {
		if (val >= ARRAY_SIZE(tv_format_names))
			return -EINVAL;

		if (psb_intel_sdvo->tv_format_index ==
		    psb_intel_sdvo_connector->tv_format_supported[val])
			return 0;

		psb_intel_sdvo->tv_format_index = psb_intel_sdvo_connector->tv_format_supported[val];
		goto done;
	} else if (IS_TV_OR_LVDS(psb_intel_sdvo_connector)) {
		temp_value = val;
		if (psb_intel_sdvo_connector->left == property) {
			drm_object_property_set_value(&connector->base,
							 psb_intel_sdvo_connector->right, val);
			if (psb_intel_sdvo_connector->left_margin == temp_value)
				return 0;

			psb_intel_sdvo_connector->left_margin = temp_value;
			psb_intel_sdvo_connector->right_margin = temp_value;
			temp_value = psb_intel_sdvo_connector->max_hscan -
				psb_intel_sdvo_connector->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
			goto set_value;
		} else if (psb_intel_sdvo_connector->right == property) {
			drm_object_property_set_value(&connector->base,
							 psb_intel_sdvo_connector->left, val);
			if (psb_intel_sdvo_connector->right_margin == temp_value)
				return 0;

			psb_intel_sdvo_connector->left_margin = temp_value;
			psb_intel_sdvo_connector->right_margin = temp_value;
			temp_value = psb_intel_sdvo_connector->max_hscan -
				psb_intel_sdvo_connector->left_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_H;
			goto set_value;
		} else if (psb_intel_sdvo_connector->top == property) {
			drm_object_property_set_value(&connector->base,
							 psb_intel_sdvo_connector->bottom, val);
			if (psb_intel_sdvo_connector->top_margin == temp_value)
				return 0;

			psb_intel_sdvo_connector->top_margin = temp_value;
			psb_intel_sdvo_connector->bottom_margin = temp_value;
			temp_value = psb_intel_sdvo_connector->max_vscan -
				psb_intel_sdvo_connector->top_margin;
			cmd = SDVO_CMD_SET_OVERSCAN_V;
			goto set_value;
		} else if (psb_intel_sdvo_connector->bottom == property) {
			drm_object_property_set_value(&connector->base,
							 psb_intel_sdvo_connector->top, val);
			if (psb_intel_sdvo_connector->bottom_margin == temp_value)
				return 0;

			psb_intel_sdvo_connector->top_margin = temp_value;
			psb_intel_sdvo_connector->bottom_margin = temp_value;
			temp_value = psb_intel_sdvo_connector->max_vscan -
				psb_intel_sdvo_connector->top_margin;
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
	if (!psb_intel_sdvo_set_value(psb_intel_sdvo, cmd, &temp_value, 2))
		return -EIO;


done:
	if (psb_intel_sdvo->base.base.crtc) {
		struct drm_crtc *crtc = psb_intel_sdvo->base.base.crtc;
		drm_crtc_helper_set_mode(crtc, &crtc->mode, crtc->x,
					 crtc->y, crtc->primary->fb);
	}

	return 0;
#undef CHECK_PROPERTY
}

static void psb_intel_sdvo_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
	struct psb_intel_sdvo *sdvo = to_psb_intel_sdvo(&gma_encoder->base);

	sdvo->saveSDVO = REG_READ(sdvo->sdvo_reg);
}

static void psb_intel_sdvo_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_encoder *encoder = &gma_attached_encoder(connector)->base;
	struct psb_intel_sdvo *sdvo = to_psb_intel_sdvo(encoder);
	struct drm_crtc *crtc = encoder->crtc;

	REG_WRITE(sdvo->sdvo_reg, sdvo->saveSDVO);

	/* Force a full mode set on the crtc. We're supposed to have the
	   mode_config lock already. */
	if (connector->status == connector_status_connected)
		drm_crtc_helper_set_mode(crtc, &crtc->mode, crtc->x, crtc->y,
					 NULL);
}

static const struct drm_encoder_helper_funcs psb_intel_sdvo_helper_funcs = {
	.dpms = psb_intel_sdvo_dpms,
	.mode_fixup = psb_intel_sdvo_mode_fixup,
	.prepare = gma_encoder_prepare,
	.mode_set = psb_intel_sdvo_mode_set,
	.commit = gma_encoder_commit,
};

static const struct drm_connector_funcs psb_intel_sdvo_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = psb_intel_sdvo_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = psb_intel_sdvo_set_property,
	.destroy = psb_intel_sdvo_destroy,
};

static const struct drm_connector_helper_funcs psb_intel_sdvo_connector_helper_funcs = {
	.get_modes = psb_intel_sdvo_get_modes,
	.mode_valid = psb_intel_sdvo_mode_valid,
	.best_encoder = gma_best_encoder,
};

static void psb_intel_sdvo_enc_destroy(struct drm_encoder *encoder)
{
	struct psb_intel_sdvo *psb_intel_sdvo = to_psb_intel_sdvo(encoder);

	if (psb_intel_sdvo->sdvo_lvds_fixed_mode != NULL)
		drm_mode_destroy(encoder->dev,
				 psb_intel_sdvo->sdvo_lvds_fixed_mode);

	i2c_del_adapter(&psb_intel_sdvo->ddc);
	gma_encoder_destroy(encoder);
}

static const struct drm_encoder_funcs psb_intel_sdvo_enc_funcs = {
	.destroy = psb_intel_sdvo_enc_destroy,
};

static void
psb_intel_sdvo_guess_ddc_bus(struct psb_intel_sdvo *sdvo)
{
	/* FIXME: At the moment, ddc_bus = 2 is the only thing that works.
	 * We need to figure out if this is true for all available poulsbo
	 * hardware, or if we need to fiddle with the guessing code above.
	 * The problem might go away if we can parse sdvo mappings from bios */
	sdvo->ddc_bus = 2;

#if 0
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
#endif
}

/**
 * Choose the appropriate DDC bus for control bus switch command for this
 * SDVO output based on the controlled output.
 *
 * DDC bus number assignment is in a priority order of RGB outputs, then TMDS
 * outputs, then LVDS outputs.
 */
static void
psb_intel_sdvo_select_ddc_bus(struct drm_psb_private *dev_priv,
			  struct psb_intel_sdvo *sdvo, u32 reg)
{
	struct sdvo_device_mapping *mapping;

	if (IS_SDVOB(reg))
		mapping = &(dev_priv->sdvo_mappings[0]);
	else
		mapping = &(dev_priv->sdvo_mappings[1]);

	if (mapping->initialized)
		sdvo->ddc_bus = 1 << ((mapping->ddc_pin & 0xf0) >> 4);
	else
		psb_intel_sdvo_guess_ddc_bus(sdvo);
}

static void
psb_intel_sdvo_select_i2c_bus(struct drm_psb_private *dev_priv,
			  struct psb_intel_sdvo *sdvo, u32 reg)
{
	struct sdvo_device_mapping *mapping;
	u8 pin, speed;

	if (IS_SDVOB(reg))
		mapping = &dev_priv->sdvo_mappings[0];
	else
		mapping = &dev_priv->sdvo_mappings[1];

	pin = GMBUS_PORT_DPB;
	speed = GMBUS_RATE_1MHZ >> 8;
	if (mapping->initialized) {
		pin = mapping->i2c_pin;
		speed = mapping->i2c_speed;
	}

	if (pin < GMBUS_NUM_PORTS) {
		sdvo->i2c = &dev_priv->gmbus[pin].adapter;
		gma_intel_gmbus_set_speed(sdvo->i2c, speed);
		gma_intel_gmbus_force_bit(sdvo->i2c, true);
	} else
		sdvo->i2c = &dev_priv->gmbus[GMBUS_PORT_DPB].adapter;
}

static bool
psb_intel_sdvo_is_hdmi_connector(struct psb_intel_sdvo *psb_intel_sdvo, int device)
{
	return psb_intel_sdvo_check_supp_encode(psb_intel_sdvo);
}

static u8
psb_intel_sdvo_get_slave_addr(struct drm_device *dev, int sdvo_reg)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
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
psb_intel_sdvo_connector_init(struct psb_intel_sdvo_connector *connector,
			  struct psb_intel_sdvo *encoder)
{
	drm_connector_init(encoder->base.base.dev,
			   &connector->base.base,
			   &psb_intel_sdvo_connector_funcs,
			   connector->base.base.connector_type);

	drm_connector_helper_add(&connector->base.base,
				 &psb_intel_sdvo_connector_helper_funcs);

	connector->base.base.interlace_allowed = 0;
	connector->base.base.doublescan_allowed = 0;
	connector->base.base.display_info.subpixel_order = SubPixelHorizontalRGB;

	connector->base.save = psb_intel_sdvo_save;
	connector->base.restore = psb_intel_sdvo_restore;

	gma_connector_attach_encoder(&connector->base, &encoder->base);
	drm_connector_register(&connector->base.base);
}

static void
psb_intel_sdvo_add_hdmi_properties(struct psb_intel_sdvo_connector *connector)
{
	/* FIXME: We don't support HDMI at the moment
	struct drm_device *dev = connector->base.base.dev;

	intel_attach_force_audio_property(&connector->base.base);
	intel_attach_broadcast_rgb_property(&connector->base.base);
	*/
}

static bool
psb_intel_sdvo_dvi_init(struct psb_intel_sdvo *psb_intel_sdvo, int device)
{
	struct drm_encoder *encoder = &psb_intel_sdvo->base.base;
	struct drm_connector *connector;
	struct gma_connector *intel_connector;
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector;

	psb_intel_sdvo_connector = kzalloc(sizeof(struct psb_intel_sdvo_connector), GFP_KERNEL);
	if (!psb_intel_sdvo_connector)
		return false;

	if (device == 0) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_TMDS0;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_TMDS0;
	} else if (device == 1) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_TMDS1;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_TMDS1;
	}

	intel_connector = &psb_intel_sdvo_connector->base;
	connector = &intel_connector->base;
	// connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	encoder->encoder_type = DRM_MODE_ENCODER_TMDS;
	connector->connector_type = DRM_MODE_CONNECTOR_DVID;

	if (psb_intel_sdvo_is_hdmi_connector(psb_intel_sdvo, device)) {
		connector->connector_type = DRM_MODE_CONNECTOR_HDMIA;
		psb_intel_sdvo->is_hdmi = true;
	}
	psb_intel_sdvo->base.clone_mask = ((1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
				       (1 << INTEL_ANALOG_CLONE_BIT));

	psb_intel_sdvo_connector_init(psb_intel_sdvo_connector, psb_intel_sdvo);
	if (psb_intel_sdvo->is_hdmi)
		psb_intel_sdvo_add_hdmi_properties(psb_intel_sdvo_connector);

	return true;
}

static bool
psb_intel_sdvo_tv_init(struct psb_intel_sdvo *psb_intel_sdvo, int type)
{
	struct drm_encoder *encoder = &psb_intel_sdvo->base.base;
	struct drm_connector *connector;
	struct gma_connector *intel_connector;
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector;

	psb_intel_sdvo_connector = kzalloc(sizeof(struct psb_intel_sdvo_connector), GFP_KERNEL);
	if (!psb_intel_sdvo_connector)
		return false;

	intel_connector = &psb_intel_sdvo_connector->base;
	connector = &intel_connector->base;
	encoder->encoder_type = DRM_MODE_ENCODER_TVDAC;
	connector->connector_type = DRM_MODE_CONNECTOR_SVIDEO;

	psb_intel_sdvo->controlled_output |= type;
	psb_intel_sdvo_connector->output_flag = type;

	psb_intel_sdvo->is_tv = true;
	psb_intel_sdvo->base.needs_tv_clock = true;
	psb_intel_sdvo->base.clone_mask = 1 << INTEL_SDVO_TV_CLONE_BIT;

	psb_intel_sdvo_connector_init(psb_intel_sdvo_connector, psb_intel_sdvo);

	if (!psb_intel_sdvo_tv_create_property(psb_intel_sdvo, psb_intel_sdvo_connector, type))
		goto err;

	if (!psb_intel_sdvo_create_enhance_property(psb_intel_sdvo, psb_intel_sdvo_connector))
		goto err;

	return true;

err:
	psb_intel_sdvo_destroy(connector);
	return false;
}

static bool
psb_intel_sdvo_analog_init(struct psb_intel_sdvo *psb_intel_sdvo, int device)
{
	struct drm_encoder *encoder = &psb_intel_sdvo->base.base;
	struct drm_connector *connector;
	struct gma_connector *intel_connector;
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector;

	psb_intel_sdvo_connector = kzalloc(sizeof(struct psb_intel_sdvo_connector), GFP_KERNEL);
	if (!psb_intel_sdvo_connector)
		return false;

	intel_connector = &psb_intel_sdvo_connector->base;
	connector = &intel_connector->base;
	connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	encoder->encoder_type = DRM_MODE_ENCODER_DAC;
	connector->connector_type = DRM_MODE_CONNECTOR_VGA;

	if (device == 0) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB0;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB0;
	} else if (device == 1) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB1;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB1;
	}

	psb_intel_sdvo->base.clone_mask = ((1 << INTEL_SDVO_NON_TV_CLONE_BIT) |
				       (1 << INTEL_ANALOG_CLONE_BIT));

	psb_intel_sdvo_connector_init(psb_intel_sdvo_connector,
				  psb_intel_sdvo);
	return true;
}

static bool
psb_intel_sdvo_lvds_init(struct psb_intel_sdvo *psb_intel_sdvo, int device)
{
	struct drm_encoder *encoder = &psb_intel_sdvo->base.base;
	struct drm_connector *connector;
	struct gma_connector *intel_connector;
	struct psb_intel_sdvo_connector *psb_intel_sdvo_connector;

	psb_intel_sdvo_connector = kzalloc(sizeof(struct psb_intel_sdvo_connector), GFP_KERNEL);
	if (!psb_intel_sdvo_connector)
		return false;

	intel_connector = &psb_intel_sdvo_connector->base;
	connector = &intel_connector->base;
	encoder->encoder_type = DRM_MODE_ENCODER_LVDS;
	connector->connector_type = DRM_MODE_CONNECTOR_LVDS;

	if (device == 0) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_LVDS0;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_LVDS0;
	} else if (device == 1) {
		psb_intel_sdvo->controlled_output |= SDVO_OUTPUT_LVDS1;
		psb_intel_sdvo_connector->output_flag = SDVO_OUTPUT_LVDS1;
	}

	psb_intel_sdvo->base.clone_mask = ((1 << INTEL_ANALOG_CLONE_BIT) |
				       (1 << INTEL_SDVO_LVDS_CLONE_BIT));

	psb_intel_sdvo_connector_init(psb_intel_sdvo_connector, psb_intel_sdvo);
	if (!psb_intel_sdvo_create_enhance_property(psb_intel_sdvo, psb_intel_sdvo_connector))
		goto err;

	return true;

err:
	psb_intel_sdvo_destroy(connector);
	return false;
}

static bool
psb_intel_sdvo_output_setup(struct psb_intel_sdvo *psb_intel_sdvo, uint16_t flags)
{
	psb_intel_sdvo->is_tv = false;
	psb_intel_sdvo->base.needs_tv_clock = false;
	psb_intel_sdvo->is_lvds = false;

	/* SDVO requires XXX1 function may not exist unless it has XXX0 function.*/

	if (flags & SDVO_OUTPUT_TMDS0)
		if (!psb_intel_sdvo_dvi_init(psb_intel_sdvo, 0))
			return false;

	if ((flags & SDVO_TMDS_MASK) == SDVO_TMDS_MASK)
		if (!psb_intel_sdvo_dvi_init(psb_intel_sdvo, 1))
			return false;

	/* TV has no XXX1 function block */
	if (flags & SDVO_OUTPUT_SVID0)
		if (!psb_intel_sdvo_tv_init(psb_intel_sdvo, SDVO_OUTPUT_SVID0))
			return false;

	if (flags & SDVO_OUTPUT_CVBS0)
		if (!psb_intel_sdvo_tv_init(psb_intel_sdvo, SDVO_OUTPUT_CVBS0))
			return false;

	if (flags & SDVO_OUTPUT_RGB0)
		if (!psb_intel_sdvo_analog_init(psb_intel_sdvo, 0))
			return false;

	if ((flags & SDVO_RGB_MASK) == SDVO_RGB_MASK)
		if (!psb_intel_sdvo_analog_init(psb_intel_sdvo, 1))
			return false;

	if (flags & SDVO_OUTPUT_LVDS0)
		if (!psb_intel_sdvo_lvds_init(psb_intel_sdvo, 0))
			return false;

	if ((flags & SDVO_LVDS_MASK) == SDVO_LVDS_MASK)
		if (!psb_intel_sdvo_lvds_init(psb_intel_sdvo, 1))
			return false;

	if ((flags & SDVO_OUTPUT_MASK) == 0) {
		unsigned char bytes[2];

		psb_intel_sdvo->controlled_output = 0;
		memcpy(bytes, &psb_intel_sdvo->caps.output_flags, 2);
		DRM_DEBUG_KMS("%s: Unknown SDVO output type (0x%02x%02x)\n",
			      SDVO_NAME(psb_intel_sdvo),
			      bytes[0], bytes[1]);
		return false;
	}
	psb_intel_sdvo->base.crtc_mask = (1 << 0) | (1 << 1);

	return true;
}

static bool psb_intel_sdvo_tv_create_property(struct psb_intel_sdvo *psb_intel_sdvo,
					  struct psb_intel_sdvo_connector *psb_intel_sdvo_connector,
					  int type)
{
	struct drm_device *dev = psb_intel_sdvo->base.base.dev;
	struct psb_intel_sdvo_tv_format format;
	uint32_t format_map, i;

	if (!psb_intel_sdvo_set_target_output(psb_intel_sdvo, type))
		return false;

	BUILD_BUG_ON(sizeof(format) != 6);
	if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
				  SDVO_CMD_GET_SUPPORTED_TV_FORMATS,
				  &format, sizeof(format)))
		return false;

	memcpy(&format_map, &format, min(sizeof(format_map), sizeof(format)));

	if (format_map == 0)
		return false;

	psb_intel_sdvo_connector->format_supported_num = 0;
	for (i = 0 ; i < ARRAY_SIZE(tv_format_names); i++)
		if (format_map & (1 << i))
			psb_intel_sdvo_connector->tv_format_supported[psb_intel_sdvo_connector->format_supported_num++] = i;


	psb_intel_sdvo_connector->tv_format =
			drm_property_create(dev, DRM_MODE_PROP_ENUM,
					    "mode", psb_intel_sdvo_connector->format_supported_num);
	if (!psb_intel_sdvo_connector->tv_format)
		return false;

	for (i = 0; i < psb_intel_sdvo_connector->format_supported_num; i++)
		drm_property_add_enum(
				psb_intel_sdvo_connector->tv_format,
				i, tv_format_names[psb_intel_sdvo_connector->tv_format_supported[i]]);

	psb_intel_sdvo->tv_format_index = psb_intel_sdvo_connector->tv_format_supported[0];
	drm_object_attach_property(&psb_intel_sdvo_connector->base.base.base,
				      psb_intel_sdvo_connector->tv_format, 0);
	return true;

}

#define ENHANCEMENT(name, NAME) do { \
	if (enhancements.name) { \
		if (!psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_MAX_##NAME, &data_value, 4) || \
		    !psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_##NAME, &response, 2)) \
			return false; \
		psb_intel_sdvo_connector->max_##name = data_value[0]; \
		psb_intel_sdvo_connector->cur_##name = response; \
		psb_intel_sdvo_connector->name = \
			drm_property_create_range(dev, 0, #name, 0, data_value[0]); \
		if (!psb_intel_sdvo_connector->name) return false; \
		drm_object_attach_property(&connector->base, \
					      psb_intel_sdvo_connector->name, \
					      psb_intel_sdvo_connector->cur_##name); \
		DRM_DEBUG_KMS(#name ": max %d, default %d, current %d\n", \
			      data_value[0], data_value[1], response); \
	} \
} while(0)

static bool
psb_intel_sdvo_create_enhance_property_tv(struct psb_intel_sdvo *psb_intel_sdvo,
				      struct psb_intel_sdvo_connector *psb_intel_sdvo_connector,
				      struct psb_intel_sdvo_enhancements_reply enhancements)
{
	struct drm_device *dev = psb_intel_sdvo->base.base.dev;
	struct drm_connector *connector = &psb_intel_sdvo_connector->base.base;
	uint16_t response, data_value[2];

	/* when horizontal overscan is supported, Add the left/right  property */
	if (enhancements.overscan_h) {
		if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
					  SDVO_CMD_GET_MAX_OVERSCAN_H,
					  &data_value, 4))
			return false;

		if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
					  SDVO_CMD_GET_OVERSCAN_H,
					  &response, 2))
			return false;

		psb_intel_sdvo_connector->max_hscan = data_value[0];
		psb_intel_sdvo_connector->left_margin = data_value[0] - response;
		psb_intel_sdvo_connector->right_margin = psb_intel_sdvo_connector->left_margin;
		psb_intel_sdvo_connector->left =
			drm_property_create_range(dev, 0, "left_margin", 0, data_value[0]);
		if (!psb_intel_sdvo_connector->left)
			return false;

		drm_object_attach_property(&connector->base,
					      psb_intel_sdvo_connector->left,
					      psb_intel_sdvo_connector->left_margin);

		psb_intel_sdvo_connector->right =
			drm_property_create_range(dev, 0, "right_margin", 0, data_value[0]);
		if (!psb_intel_sdvo_connector->right)
			return false;

		drm_object_attach_property(&connector->base,
					      psb_intel_sdvo_connector->right,
					      psb_intel_sdvo_connector->right_margin);
		DRM_DEBUG_KMS("h_overscan: max %d, "
			      "default %d, current %d\n",
			      data_value[0], data_value[1], response);
	}

	if (enhancements.overscan_v) {
		if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
					  SDVO_CMD_GET_MAX_OVERSCAN_V,
					  &data_value, 4))
			return false;

		if (!psb_intel_sdvo_get_value(psb_intel_sdvo,
					  SDVO_CMD_GET_OVERSCAN_V,
					  &response, 2))
			return false;

		psb_intel_sdvo_connector->max_vscan = data_value[0];
		psb_intel_sdvo_connector->top_margin = data_value[0] - response;
		psb_intel_sdvo_connector->bottom_margin = psb_intel_sdvo_connector->top_margin;
		psb_intel_sdvo_connector->top =
			drm_property_create_range(dev, 0, "top_margin", 0, data_value[0]);
		if (!psb_intel_sdvo_connector->top)
			return false;

		drm_object_attach_property(&connector->base,
					      psb_intel_sdvo_connector->top,
					      psb_intel_sdvo_connector->top_margin);

		psb_intel_sdvo_connector->bottom =
			drm_property_create_range(dev, 0, "bottom_margin", 0, data_value[0]);
		if (!psb_intel_sdvo_connector->bottom)
			return false;

		drm_object_attach_property(&connector->base,
					      psb_intel_sdvo_connector->bottom,
					      psb_intel_sdvo_connector->bottom_margin);
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
		if (!psb_intel_sdvo_get_value(psb_intel_sdvo, SDVO_CMD_GET_DOT_CRAWL, &response, 2))
			return false;

		psb_intel_sdvo_connector->max_dot_crawl = 1;
		psb_intel_sdvo_connector->cur_dot_crawl = response & 0x1;
		psb_intel_sdvo_connector->dot_crawl =
			drm_property_create_range(dev, 0, "dot_crawl", 0, 1);
		if (!psb_intel_sdvo_connector->dot_crawl)
			return false;

		drm_object_attach_property(&connector->base,
					      psb_intel_sdvo_connector->dot_crawl,
					      psb_intel_sdvo_connector->cur_dot_crawl);
		DRM_DEBUG_KMS("dot crawl: current %d\n", response);
	}

	return true;
}

static bool
psb_intel_sdvo_create_enhance_property_lvds(struct psb_intel_sdvo *psb_intel_sdvo,
					struct psb_intel_sdvo_connector *psb_intel_sdvo_connector,
					struct psb_intel_sdvo_enhancements_reply enhancements)
{
	struct drm_device *dev = psb_intel_sdvo->base.base.dev;
	struct drm_connector *connector = &psb_intel_sdvo_connector->base.base;
	uint16_t response, data_value[2];

	ENHANCEMENT(brightness, BRIGHTNESS);

	return true;
}
#undef ENHANCEMENT

static bool psb_intel_sdvo_create_enhance_property(struct psb_intel_sdvo *psb_intel_sdvo,
					       struct psb_intel_sdvo_connector *psb_intel_sdvo_connector)
{
	union {
		struct psb_intel_sdvo_enhancements_reply reply;
		uint16_t response;
	} enhancements;

	BUILD_BUG_ON(sizeof(enhancements) != 2);

	enhancements.response = 0;
	psb_intel_sdvo_get_value(psb_intel_sdvo,
			     SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS,
			     &enhancements, sizeof(enhancements));
	if (enhancements.response == 0) {
		DRM_DEBUG_KMS("No enhancement is supported\n");
		return true;
	}

	if (IS_TV(psb_intel_sdvo_connector))
		return psb_intel_sdvo_create_enhance_property_tv(psb_intel_sdvo, psb_intel_sdvo_connector, enhancements.reply);
	else if(IS_LVDS(psb_intel_sdvo_connector))
		return psb_intel_sdvo_create_enhance_property_lvds(psb_intel_sdvo, psb_intel_sdvo_connector, enhancements.reply);
	else
		return true;
}

static int psb_intel_sdvo_ddc_proxy_xfer(struct i2c_adapter *adapter,
				     struct i2c_msg *msgs,
				     int num)
{
	struct psb_intel_sdvo *sdvo = adapter->algo_data;

	if (!psb_intel_sdvo_set_control_bus_switch(sdvo, sdvo->ddc_bus))
		return -EIO;

	return sdvo->i2c->algo->master_xfer(sdvo->i2c, msgs, num);
}

static u32 psb_intel_sdvo_ddc_proxy_func(struct i2c_adapter *adapter)
{
	struct psb_intel_sdvo *sdvo = adapter->algo_data;
	return sdvo->i2c->algo->functionality(sdvo->i2c);
}

static const struct i2c_algorithm psb_intel_sdvo_ddc_proxy = {
	.master_xfer	= psb_intel_sdvo_ddc_proxy_xfer,
	.functionality	= psb_intel_sdvo_ddc_proxy_func
};

static bool
psb_intel_sdvo_init_ddc_proxy(struct psb_intel_sdvo *sdvo,
			  struct drm_device *dev)
{
	sdvo->ddc.owner = THIS_MODULE;
	sdvo->ddc.class = I2C_CLASS_DDC;
	snprintf(sdvo->ddc.name, I2C_NAME_SIZE, "SDVO DDC proxy");
	sdvo->ddc.dev.parent = &dev->pdev->dev;
	sdvo->ddc.algo_data = sdvo;
	sdvo->ddc.algo = &psb_intel_sdvo_ddc_proxy;

	return i2c_add_adapter(&sdvo->ddc) == 0;
}

bool psb_intel_sdvo_init(struct drm_device *dev, int sdvo_reg)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_encoder *gma_encoder;
	struct psb_intel_sdvo *psb_intel_sdvo;
	int i;

	psb_intel_sdvo = kzalloc(sizeof(struct psb_intel_sdvo), GFP_KERNEL);
	if (!psb_intel_sdvo)
		return false;

	psb_intel_sdvo->sdvo_reg = sdvo_reg;
	psb_intel_sdvo->slave_addr = psb_intel_sdvo_get_slave_addr(dev, sdvo_reg) >> 1;
	psb_intel_sdvo_select_i2c_bus(dev_priv, psb_intel_sdvo, sdvo_reg);
	if (!psb_intel_sdvo_init_ddc_proxy(psb_intel_sdvo, dev)) {
		kfree(psb_intel_sdvo);
		return false;
	}

	/* encoder type will be decided later */
	gma_encoder = &psb_intel_sdvo->base;
	gma_encoder->type = INTEL_OUTPUT_SDVO;
	drm_encoder_init(dev, &gma_encoder->base, &psb_intel_sdvo_enc_funcs,
			 0, NULL);

	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		u8 byte;

		if (!psb_intel_sdvo_read_byte(psb_intel_sdvo, i, &byte)) {
			DRM_DEBUG_KMS("No SDVO device found on SDVO%c\n",
				      IS_SDVOB(sdvo_reg) ? 'B' : 'C');
			goto err;
		}
	}

	if (IS_SDVOB(sdvo_reg))
		dev_priv->hotplug_supported_mask |= SDVOB_HOTPLUG_INT_STATUS;
	else
		dev_priv->hotplug_supported_mask |= SDVOC_HOTPLUG_INT_STATUS;

	drm_encoder_helper_add(&gma_encoder->base, &psb_intel_sdvo_helper_funcs);

	/* In default case sdvo lvds is false */
	if (!psb_intel_sdvo_get_capabilities(psb_intel_sdvo, &psb_intel_sdvo->caps))
		goto err;

	if (psb_intel_sdvo_output_setup(psb_intel_sdvo,
				    psb_intel_sdvo->caps.output_flags) != true) {
		DRM_DEBUG_KMS("SDVO output failed to setup on SDVO%c\n",
			      IS_SDVOB(sdvo_reg) ? 'B' : 'C');
		goto err;
	}

	psb_intel_sdvo_select_ddc_bus(dev_priv, psb_intel_sdvo, sdvo_reg);

	/* Set the input timing to the screen. Assume always input 0. */
	if (!psb_intel_sdvo_set_target_input(psb_intel_sdvo))
		goto err;

	if (!psb_intel_sdvo_get_input_pixel_clock_range(psb_intel_sdvo,
						    &psb_intel_sdvo->pixel_clock_min,
						    &psb_intel_sdvo->pixel_clock_max))
		goto err;

	DRM_DEBUG_KMS("%s device VID/DID: %02X:%02X.%02X, "
			"clock range %dMHz - %dMHz, "
			"input 1: %c, input 2: %c, "
			"output 1: %c, output 2: %c\n",
			SDVO_NAME(psb_intel_sdvo),
			psb_intel_sdvo->caps.vendor_id, psb_intel_sdvo->caps.device_id,
			psb_intel_sdvo->caps.device_rev_id,
			psb_intel_sdvo->pixel_clock_min / 1000,
			psb_intel_sdvo->pixel_clock_max / 1000,
			(psb_intel_sdvo->caps.sdvo_inputs_mask & 0x1) ? 'Y' : 'N',
			(psb_intel_sdvo->caps.sdvo_inputs_mask & 0x2) ? 'Y' : 'N',
			/* check currently supported outputs */
			psb_intel_sdvo->caps.output_flags &
			(SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_RGB0) ? 'Y' : 'N',
			psb_intel_sdvo->caps.output_flags &
			(SDVO_OUTPUT_TMDS1 | SDVO_OUTPUT_RGB1) ? 'Y' : 'N');
	return true;

err:
	drm_encoder_cleanup(&gma_encoder->base);
	i2c_del_adapter(&psb_intel_sdvo->ddc);
	kfree(psb_intel_sdvo);

	return false;
}
