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
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_sdvo_regs.h"

#define SDVO_TMDS_MASK (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1)
#define SDVO_RGB_MASK  (SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1)
#define SDVO_LVDS_MASK (SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1)
#define SDVO_TV_MASK   (SDVO_OUTPUT_CVBS0 | SDVO_OUTPUT_SVID0 | SDVO_OUTPUT_YPRPB0)

#define SDVO_OUTPUT_MASK (SDVO_TMDS_MASK | SDVO_RGB_MASK | SDVO_LVDS_MASK |\
			SDVO_TV_MASK)

#define IS_TV(c)	(c->output_flag & SDVO_TV_MASK)
#define IS_TMDS(c)	(c->output_flag & SDVO_TMDS_MASK)
#define IS_LVDS(c)	(c->output_flag & SDVO_LVDS_MASK)
#define IS_TV_OR_LVDS(c) (c->output_flag & (SDVO_TV_MASK | SDVO_LVDS_MASK))
#define IS_DIGITAL(c) (c->output_flag & (SDVO_TMDS_MASK | SDVO_LVDS_MASK))


static const char * const tv_format_names[] = {
	"NTSC_M"   , "NTSC_J"  , "NTSC_443",
	"PAL_B"    , "PAL_D"   , "PAL_G"   ,
	"PAL_H"    , "PAL_I"   , "PAL_M"   ,
	"PAL_N"    , "PAL_NC"  , "PAL_60"  ,
	"SECAM_B"  , "SECAM_D" , "SECAM_G" ,
	"SECAM_K"  , "SECAM_K1", "SECAM_L" ,
	"SECAM_60"
};

#define TV_FORMAT_NUM  ARRAY_SIZE(tv_format_names)

struct intel_sdvo {
	struct intel_encoder base;

	struct i2c_adapter *i2c;
	u8 slave_addr;

	struct i2c_adapter ddc;

	/* Register for the SDVO device: SDVOB or SDVOC */
	i915_reg_t sdvo_reg;

	/* Active outputs controlled by this SDVO output */
	uint16_t controlled_output;

	/*
	 * Capabilities of the SDVO device returned by
	 * intel_sdvo_get_capabilities()
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
	uint16_t hotplug_active;

	/**
	 * This is set if we're going to treat the device as TV-out.
	 *
	 * While we have these nice friendly flags for output types that ought
	 * to decide this for us, the S-Video output on our HDMI+S-Video card
	 * shows up as RGB1 (VGA).
	 */
	bool is_tv;

	enum port port;

	/**
	 * This is set if we treat the device as HDMI, instead of DVI.
	 */
	bool is_hdmi;
	bool has_hdmi_monitor;
	bool has_hdmi_audio;
	bool rgb_quant_range_selectable;

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

	/*
	 * the sdvo flag gets lost in round trip: dtd->adjusted_mode->dtd
	 */
	uint8_t dtd_sdvo_flags;
};

struct intel_sdvo_connector {
	struct intel_connector base;

	/* Mark the type of connector */
	uint16_t output_flag;

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

	/* this is to get the range of margin.*/
	u32 max_hscan, max_vscan;
};

struct intel_sdvo_connector_state {
	/* base.base: tv.saturation/contrast/hue/brightness */
	struct intel_digital_connector_state base;

	struct {
		unsigned overscan_h, overscan_v, hpos, vpos, sharpness;
		unsigned flicker_filter, flicker_filter_2d, flicker_filter_adaptive;
		unsigned chroma_filter, luma_filter, dot_crawl;
	} tv;
};

static struct intel_sdvo *to_sdvo(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_sdvo, base);
}

static struct intel_sdvo *intel_attached_sdvo(struct drm_connector *connector)
{
	return to_sdvo(intel_attached_encoder(connector));
}

static struct intel_sdvo_connector *
to_intel_sdvo_connector(struct drm_connector *connector)
{
	return container_of(connector, struct intel_sdvo_connector, base.base);
}

#define to_intel_sdvo_connector_state(conn_state) \
	container_of((conn_state), struct intel_sdvo_connector_state, base.base)

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
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 bval = val, cval = val;
	int i;

	if (HAS_PCH_SPLIT(dev_priv)) {
		I915_WRITE(intel_sdvo->sdvo_reg, val);
		POSTING_READ(intel_sdvo->sdvo_reg);
		/*
		 * HW workaround, need to write this twice for issue
		 * that may result in first write getting masked.
		 */
		if (HAS_PCH_IBX(dev_priv)) {
			I915_WRITE(intel_sdvo->sdvo_reg, val);
			POSTING_READ(intel_sdvo->sdvo_reg);
		}
		return;
	}

	if (intel_sdvo->port == PORT_B)
		cval = I915_READ(GEN3_SDVOC);
	else
		bval = I915_READ(GEN3_SDVOB);

	/*
	 * Write the registers twice for luck. Sometimes,
	 * writing them only once doesn't appear to 'stick'.
	 * The BIOS does this too. Yay, magic
	 */
	for (i = 0; i < 2; i++)
	{
		I915_WRITE(GEN3_SDVOB, bval);
		POSTING_READ(GEN3_SDVOB);
		I915_WRITE(GEN3_SDVOC, cval);
		POSTING_READ(GEN3_SDVOC);
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
} __attribute__ ((packed)) sdvo_cmd_names[] = {
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

#define SDVO_NAME(svdo) ((svdo)->port == PORT_B ? "SDVOB" : "SDVOC")

static void intel_sdvo_debug_write(struct intel_sdvo *intel_sdvo, u8 cmd,
				   const void *args, int args_len)
{
	int i, pos = 0;
#define BUF_LEN 256
	char buffer[BUF_LEN];

#define BUF_PRINT(args...) \
	pos += snprintf(buffer + pos, max_t(int, BUF_LEN - pos, 0), args)


	for (i = 0; i < args_len; i++) {
		BUF_PRINT("%02X ", ((u8 *)args)[i]);
	}
	for (; i < 8; i++) {
		BUF_PRINT("   ");
	}
	for (i = 0; i < ARRAY_SIZE(sdvo_cmd_names); i++) {
		if (cmd == sdvo_cmd_names[i].cmd) {
			BUF_PRINT("(%s)", sdvo_cmd_names[i].name);
			break;
		}
	}
	if (i == ARRAY_SIZE(sdvo_cmd_names)) {
		BUF_PRINT("(%02X)", cmd);
	}
	BUG_ON(pos >= BUF_LEN - 1);
#undef BUF_PRINT
#undef BUF_LEN

	DRM_DEBUG_KMS("%s: W: %02X %s\n", SDVO_NAME(intel_sdvo), cmd, buffer);
}

static const char * const cmd_status_names[] = {
	"Power on",
	"Success",
	"Not supported",
	"Invalid arg",
	"Pending",
	"Target not specified",
	"Scaling not supported"
};

static bool __intel_sdvo_write_cmd(struct intel_sdvo *intel_sdvo, u8 cmd,
				   const void *args, int args_len,
				   bool unlocked)
{
	u8 *buf, status;
	struct i2c_msg *msgs;
	int i, ret = true;

	/* Would be simpler to allocate both in one go ? */
	buf = kzalloc(args_len * 2 + 2, GFP_KERNEL);
	if (!buf)
		return false;

	msgs = kcalloc(args_len + 3, sizeof(*msgs), GFP_KERNEL);
	if (!msgs) {
		kfree(buf);
		return false;
	}

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

	if (unlocked)
		ret = i2c_transfer(intel_sdvo->i2c, msgs, i+3);
	else
		ret = __i2c_transfer(intel_sdvo->i2c, msgs, i+3);
	if (ret < 0) {
		DRM_DEBUG_KMS("I2c transfer returned %d\n", ret);
		ret = false;
		goto out;
	}
	if (ret != i+3) {
		/* failure in I2C transfer */
		DRM_DEBUG_KMS("I2c transfer returned %d/%d\n", ret, i+3);
		ret = false;
	}

out:
	kfree(msgs);
	kfree(buf);
	return ret;
}

static bool intel_sdvo_write_cmd(struct intel_sdvo *intel_sdvo, u8 cmd,
				 const void *args, int args_len)
{
	return __intel_sdvo_write_cmd(intel_sdvo, cmd, args, args_len, true);
}

static bool intel_sdvo_read_response(struct intel_sdvo *intel_sdvo,
				     void *response, int response_len)
{
	u8 retry = 15; /* 5 quick checks, followed by 10 long checks */
	u8 status;
	int i, pos = 0;
#define BUF_LEN 256
	char buffer[BUF_LEN];


	/*
	 * The documentation states that all commands will be
	 * processed within 15µs, and that we need only poll
	 * the status byte a maximum of 3 times in order for the
	 * command to be complete.
	 *
	 * Check 5 times in case the hardware failed to read the docs.
	 *
	 * Also beware that the first response by many devices is to
	 * reply PENDING and stall for time. TVs are notorious for
	 * requiring longer than specified to complete their replies.
	 * Originally (in the DDX long ago), the delay was only ever 15ms
	 * with an additional delay of 30ms applied for TVs added later after
	 * many experiments. To accommodate both sets of delays, we do a
	 * sequence of slow checks if the device is falling behind and fails
	 * to reply within 5*15µs.
	 */
	if (!intel_sdvo_read_byte(intel_sdvo,
				  SDVO_I2C_CMD_STATUS,
				  &status))
		goto log_fail;

	while ((status == SDVO_CMD_STATUS_PENDING ||
		status == SDVO_CMD_STATUS_TARGET_NOT_SPECIFIED) && --retry) {
		if (retry < 10)
			msleep(15);
		else
			udelay(15);

		if (!intel_sdvo_read_byte(intel_sdvo,
					  SDVO_I2C_CMD_STATUS,
					  &status))
			goto log_fail;
	}

#define BUF_PRINT(args...) \
	pos += snprintf(buffer + pos, max_t(int, BUF_LEN - pos, 0), args)

	if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
		BUF_PRINT("(%s)", cmd_status_names[status]);
	else
		BUF_PRINT("(??? %d)", status);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		goto log_fail;

	/* Read the command response */
	for (i = 0; i < response_len; i++) {
		if (!intel_sdvo_read_byte(intel_sdvo,
					  SDVO_I2C_RETURN_0 + i,
					  &((u8 *)response)[i]))
			goto log_fail;
		BUF_PRINT(" %02X", ((u8 *)response)[i]);
	}
	BUG_ON(pos >= BUF_LEN - 1);
#undef BUF_PRINT
#undef BUF_LEN

	DRM_DEBUG_KMS("%s: R: %s\n", SDVO_NAME(intel_sdvo), buffer);
	return true;

log_fail:
	DRM_DEBUG_KMS("%s: R: ... failed\n", SDVO_NAME(intel_sdvo));
	return false;
}

static int intel_sdvo_get_pixel_multiplier(const struct drm_display_mode *adjusted_mode)
{
	if (adjusted_mode->crtc_clock >= 100000)
		return 1;
	else if (adjusted_mode->crtc_clock >= 50000)
		return 2;
	else
		return 4;
}

static bool __intel_sdvo_set_control_bus_switch(struct intel_sdvo *intel_sdvo,
						u8 ddc_bus)
{
	/* This must be the immediately preceding write before the i2c xfer */
	return __intel_sdvo_write_cmd(intel_sdvo,
				      SDVO_CMD_SET_CONTROL_BUS_SWITCH,
				      &ddc_bus, 1, false);
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

static bool intel_sdvo_get_active_outputs(struct intel_sdvo *intel_sdvo,
					  u16 *outputs)
{
	return intel_sdvo_get_value(intel_sdvo,
				    SDVO_CMD_GET_ACTIVE_OUTPUTS,
				    outputs, sizeof(*outputs));
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

static bool intel_sdvo_get_timing(struct intel_sdvo *intel_sdvo, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_value(intel_sdvo, cmd, &dtd->part1, sizeof(dtd->part1)) &&
		intel_sdvo_get_value(intel_sdvo, cmd + 1, &dtd->part2, sizeof(dtd->part2));
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

static bool intel_sdvo_get_input_timing(struct intel_sdvo *intel_sdvo,
					struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_timing(intel_sdvo,
				     SDVO_CMD_GET_INPUT_TIMINGS_PART1, dtd);
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
	int mode_clock;

	memset(dtd, 0, sizeof(*dtd));

	width = mode->hdisplay;
	height = mode->vdisplay;

	/* do some mode translations */
	h_blank_len = mode->htotal - mode->hdisplay;
	h_sync_len = mode->hsync_end - mode->hsync_start;

	v_blank_len = mode->vtotal - mode->vdisplay;
	v_sync_len = mode->vsync_end - mode->vsync_start;

	h_sync_offset = mode->hsync_start - mode->hdisplay;
	v_sync_offset = mode->vsync_start - mode->vdisplay;

	mode_clock = mode->clock;
	mode_clock /= 10;
	dtd->part1.clock = mode_clock;

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
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		dtd->part2.dtd_flags |= DTD_FLAG_INTERLACE;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		dtd->part2.dtd_flags |= DTD_FLAG_HSYNC_POSITIVE;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		dtd->part2.dtd_flags |= DTD_FLAG_VSYNC_POSITIVE;

	dtd->part2.v_sync_off_high = v_sync_offset & 0xc0;
}

static void intel_sdvo_get_mode_from_dtd(struct drm_display_mode *pmode,
					 const struct intel_sdvo_dtd *dtd)
{
	struct drm_display_mode mode = {};

	mode.hdisplay = dtd->part1.h_active;
	mode.hdisplay += ((dtd->part1.h_high >> 4) & 0x0f) << 8;
	mode.hsync_start = mode.hdisplay + dtd->part2.h_sync_off;
	mode.hsync_start += (dtd->part2.sync_off_width_high & 0xc0) << 2;
	mode.hsync_end = mode.hsync_start + dtd->part2.h_sync_width;
	mode.hsync_end += (dtd->part2.sync_off_width_high & 0x30) << 4;
	mode.htotal = mode.hdisplay + dtd->part1.h_blank;
	mode.htotal += (dtd->part1.h_high & 0xf) << 8;

	mode.vdisplay = dtd->part1.v_active;
	mode.vdisplay += ((dtd->part1.v_high >> 4) & 0x0f) << 8;
	mode.vsync_start = mode.vdisplay;
	mode.vsync_start += (dtd->part2.v_sync_off_width >> 4) & 0xf;
	mode.vsync_start += (dtd->part2.sync_off_width_high & 0x0c) << 2;
	mode.vsync_start += dtd->part2.v_sync_off_high & 0xc0;
	mode.vsync_end = mode.vsync_start +
		(dtd->part2.v_sync_off_width & 0xf);
	mode.vsync_end += (dtd->part2.sync_off_width_high & 0x3) << 4;
	mode.vtotal = mode.vdisplay + dtd->part1.v_blank;
	mode.vtotal += (dtd->part1.v_high & 0xf) << 8;

	mode.clock = dtd->part1.clock * 10;

	if (dtd->part2.dtd_flags & DTD_FLAG_INTERLACE)
		mode.flags |= DRM_MODE_FLAG_INTERLACE;
	if (dtd->part2.dtd_flags & DTD_FLAG_HSYNC_POSITIVE)
		mode.flags |= DRM_MODE_FLAG_PHSYNC;
	else
		mode.flags |= DRM_MODE_FLAG_NHSYNC;
	if (dtd->part2.dtd_flags & DTD_FLAG_VSYNC_POSITIVE)
		mode.flags |= DRM_MODE_FLAG_PVSYNC;
	else
		mode.flags |= DRM_MODE_FLAG_NVSYNC;

	drm_mode_set_crtcinfo(&mode, 0);

	drm_mode_copy(pmode, &mode);
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

static bool intel_sdvo_write_infoframe(struct intel_sdvo *intel_sdvo,
				       unsigned if_index, uint8_t tx_rate,
				       const uint8_t *data, unsigned length)
{
	uint8_t set_buf_index[2] = { if_index, 0 };
	uint8_t hbuf_size, tmp[8];
	int i;

	if (!intel_sdvo_set_value(intel_sdvo,
				  SDVO_CMD_SET_HBUF_INDEX,
				  set_buf_index, 2))
		return false;

	if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_HBUF_INFO,
				  &hbuf_size, 1))
		return false;

	/* Buffer size is 0 based, hooray! */
	hbuf_size++;

	DRM_DEBUG_KMS("writing sdvo hbuf: %i, hbuf_size %i, hbuf_size: %i\n",
		      if_index, length, hbuf_size);

	for (i = 0; i < hbuf_size; i += 8) {
		memset(tmp, 0, 8);
		if (i < length)
			memcpy(tmp, data + i, min_t(unsigned, 8, length - i));

		if (!intel_sdvo_set_value(intel_sdvo,
					  SDVO_CMD_SET_HBUF_DATA,
					  tmp, 8))
			return false;
	}

	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_HBUF_TXRATE,
				    &tx_rate, 1);
}

static bool intel_sdvo_set_avi_infoframe(struct intel_sdvo *intel_sdvo,
					 const struct intel_crtc_state *pipe_config)
{
	uint8_t sdvo_data[HDMI_INFOFRAME_SIZE(AVI)];
	union hdmi_infoframe frame;
	int ret;
	ssize_t len;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						       &pipe_config->base.adjusted_mode,
						       false);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return false;
	}

	if (intel_sdvo->rgb_quant_range_selectable) {
		if (pipe_config->limited_color_range)
			frame.avi.quantization_range =
				HDMI_QUANTIZATION_RANGE_LIMITED;
		else
			frame.avi.quantization_range =
				HDMI_QUANTIZATION_RANGE_FULL;
	}

	len = hdmi_infoframe_pack(&frame, sdvo_data, sizeof(sdvo_data));
	if (len < 0)
		return false;

	return intel_sdvo_write_infoframe(intel_sdvo, SDVO_HBUF_INDEX_AVI_IF,
					  SDVO_HBUF_TX_VSYNC,
					  sdvo_data, sizeof(sdvo_data));
}

static bool intel_sdvo_set_tv_format(struct intel_sdvo *intel_sdvo,
				     const struct drm_connector_state *conn_state)
{
	struct intel_sdvo_tv_format format;
	uint32_t format_map;

	format_map = 1 << conn_state->tv.mode;
	memset(&format, 0, sizeof(format));
	memcpy(&format, &format_map, min(sizeof(format), sizeof(format_map)));

	BUILD_BUG_ON(sizeof(format) != 6);
	return intel_sdvo_set_value(intel_sdvo,
				    SDVO_CMD_SET_TV_FORMAT,
				    &format, sizeof(format));
}

static bool
intel_sdvo_set_output_timings_from_mode(struct intel_sdvo *intel_sdvo,
					const struct drm_display_mode *mode)
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

/* Asks the sdvo controller for the preferred input mode given the output mode.
 * Unfortunately we have to set up the full output mode to do that. */
static bool
intel_sdvo_get_preferred_input_mode(struct intel_sdvo *intel_sdvo,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	struct intel_sdvo_dtd input_dtd;

	/* Reset the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		return false;

	if (!intel_sdvo_create_preferred_input_timing(intel_sdvo,
						      mode->clock / 10,
						      mode->hdisplay,
						      mode->vdisplay))
		return false;

	if (!intel_sdvo_get_preferred_input_timing(intel_sdvo,
						   &input_dtd))
		return false;

	intel_sdvo_get_mode_from_dtd(adjusted_mode, &input_dtd);
	intel_sdvo->dtd_sdvo_flags = input_dtd.part2.sdvo_flags;

	return true;
}

static void i9xx_adjust_sdvo_tv_clock(struct intel_crtc_state *pipe_config)
{
	unsigned dotclock = pipe_config->port_clock;
	struct dpll *clock = &pipe_config->dpll;

	/* SDVO TV has fixed PLL values depend on its clock range,
	   this mirrors vbios setting. */
	if (dotclock >= 100000 && dotclock < 140500) {
		clock->p1 = 2;
		clock->p2 = 10;
		clock->n = 3;
		clock->m1 = 16;
		clock->m2 = 8;
	} else if (dotclock >= 140500 && dotclock <= 200000) {
		clock->p1 = 1;
		clock->p2 = 10;
		clock->n = 6;
		clock->m1 = 12;
		clock->m2 = 8;
	} else {
		WARN(1, "SDVO TV clock out of range: %i\n", dotclock);
	}

	pipe_config->clock_set = true;
}

static bool intel_sdvo_compute_config(struct intel_encoder *encoder,
				      struct intel_crtc_state *pipe_config,
				      struct drm_connector_state *conn_state)
{
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);
	struct intel_sdvo_connector_state *intel_sdvo_state =
		to_intel_sdvo_connector_state(conn_state);
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct drm_display_mode *mode = &pipe_config->base.mode;

	DRM_DEBUG_KMS("forcing bpc to 8 for SDVO\n");
	pipe_config->pipe_bpp = 8*3;

	if (HAS_PCH_SPLIT(to_i915(encoder->base.dev)))
		pipe_config->has_pch_encoder = true;

	/* We need to construct preferred input timings based on our
	 * output timings.  To do that, we have to set the output
	 * timings, even though this isn't really the right place in
	 * the sequence to do it. Oh well.
	 */
	if (intel_sdvo->is_tv) {
		if (!intel_sdvo_set_output_timings_from_mode(intel_sdvo, mode))
			return false;

		(void) intel_sdvo_get_preferred_input_mode(intel_sdvo,
							   mode,
							   adjusted_mode);
		pipe_config->sdvo_tv_clock = true;
	} else if (intel_sdvo->is_lvds) {
		if (!intel_sdvo_set_output_timings_from_mode(intel_sdvo,
							     intel_sdvo->sdvo_lvds_fixed_mode))
			return false;

		(void) intel_sdvo_get_preferred_input_mode(intel_sdvo,
							   mode,
							   adjusted_mode);
	}

	/* Make the CRTC code factor in the SDVO pixel multiplier.  The
	 * SDVO device will factor out the multiplier during mode_set.
	 */
	pipe_config->pixel_multiplier =
		intel_sdvo_get_pixel_multiplier(adjusted_mode);

	if (intel_sdvo_state->base.force_audio != HDMI_AUDIO_OFF_DVI)
		pipe_config->has_hdmi_sink = intel_sdvo->has_hdmi_monitor;

	if (intel_sdvo_state->base.force_audio == HDMI_AUDIO_ON ||
	    (intel_sdvo_state->base.force_audio == HDMI_AUDIO_AUTO && intel_sdvo->has_hdmi_audio))
		pipe_config->has_audio = true;

	if (intel_sdvo_state->base.broadcast_rgb == INTEL_BROADCAST_RGB_AUTO) {
		/* See CEA-861-E - 5.1 Default Encoding Parameters */
		/* FIXME: This bit is only valid when using TMDS encoding and 8
		 * bit per color mode. */
		if (pipe_config->has_hdmi_sink &&
		    drm_match_cea_mode(adjusted_mode) > 1)
			pipe_config->limited_color_range = true;
	} else {
		if (pipe_config->has_hdmi_sink &&
		    intel_sdvo_state->base.broadcast_rgb == INTEL_BROADCAST_RGB_LIMITED)
			pipe_config->limited_color_range = true;
	}

	/* Clock computation needs to happen after pixel multiplier. */
	if (intel_sdvo->is_tv)
		i9xx_adjust_sdvo_tv_clock(pipe_config);

	/* Set user selected PAR to incoming mode's member */
	if (intel_sdvo->is_hdmi)
		adjusted_mode->picture_aspect_ratio = conn_state->picture_aspect_ratio;

	return true;
}

#define UPDATE_PROPERTY(input, NAME) \
	do { \
		val = input; \
		intel_sdvo_set_value(intel_sdvo, SDVO_CMD_SET_##NAME, &val, sizeof(val)); \
	} while (0)

static void intel_sdvo_update_props(struct intel_sdvo *intel_sdvo,
				    const struct intel_sdvo_connector_state *sdvo_state)
{
	const struct drm_connector_state *conn_state = &sdvo_state->base.base;
	struct intel_sdvo_connector *intel_sdvo_conn =
		to_intel_sdvo_connector(conn_state->connector);
	uint16_t val;

	if (intel_sdvo_conn->left)
		UPDATE_PROPERTY(sdvo_state->tv.overscan_h, OVERSCAN_H);

	if (intel_sdvo_conn->top)
		UPDATE_PROPERTY(sdvo_state->tv.overscan_v, OVERSCAN_V);

	if (intel_sdvo_conn->hpos)
		UPDATE_PROPERTY(sdvo_state->tv.hpos, HPOS);

	if (intel_sdvo_conn->vpos)
		UPDATE_PROPERTY(sdvo_state->tv.vpos, VPOS);

	if (intel_sdvo_conn->saturation)
		UPDATE_PROPERTY(conn_state->tv.saturation, SATURATION);

	if (intel_sdvo_conn->contrast)
		UPDATE_PROPERTY(conn_state->tv.contrast, CONTRAST);

	if (intel_sdvo_conn->hue)
		UPDATE_PROPERTY(conn_state->tv.hue, HUE);

	if (intel_sdvo_conn->brightness)
		UPDATE_PROPERTY(conn_state->tv.brightness, BRIGHTNESS);

	if (intel_sdvo_conn->sharpness)
		UPDATE_PROPERTY(sdvo_state->tv.sharpness, SHARPNESS);

	if (intel_sdvo_conn->flicker_filter)
		UPDATE_PROPERTY(sdvo_state->tv.flicker_filter, FLICKER_FILTER);

	if (intel_sdvo_conn->flicker_filter_2d)
		UPDATE_PROPERTY(sdvo_state->tv.flicker_filter_2d, FLICKER_FILTER_2D);

	if (intel_sdvo_conn->flicker_filter_adaptive)
		UPDATE_PROPERTY(sdvo_state->tv.flicker_filter_adaptive, FLICKER_FILTER_ADAPTIVE);

	if (intel_sdvo_conn->tv_chroma_filter)
		UPDATE_PROPERTY(sdvo_state->tv.chroma_filter, TV_CHROMA_FILTER);

	if (intel_sdvo_conn->tv_luma_filter)
		UPDATE_PROPERTY(sdvo_state->tv.luma_filter, TV_LUMA_FILTER);

	if (intel_sdvo_conn->dot_crawl)
		UPDATE_PROPERTY(sdvo_state->tv.dot_crawl, DOT_CRAWL);

#undef UPDATE_PROPERTY
}

static void intel_sdvo_pre_enable(struct intel_encoder *intel_encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	const struct drm_display_mode *adjusted_mode = &crtc_state->base.adjusted_mode;
	const struct intel_sdvo_connector_state *sdvo_state =
		to_intel_sdvo_connector_state(conn_state);
	const struct drm_display_mode *mode = &crtc_state->base.mode;
	struct intel_sdvo *intel_sdvo = to_sdvo(intel_encoder);
	u32 sdvox;
	struct intel_sdvo_in_out_map in_out;
	struct intel_sdvo_dtd input_dtd, output_dtd;
	int rate;

	intel_sdvo_update_props(intel_sdvo, sdvo_state);

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

	/* lvds has a special fixed output timing. */
	if (intel_sdvo->is_lvds)
		intel_sdvo_get_dtd_from_mode(&output_dtd,
					     intel_sdvo->sdvo_lvds_fixed_mode);
	else
		intel_sdvo_get_dtd_from_mode(&output_dtd, mode);
	if (!intel_sdvo_set_output_timing(intel_sdvo, &output_dtd))
		DRM_INFO("Setting output timings on %s failed\n",
			 SDVO_NAME(intel_sdvo));

	/* Set the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		return;

	if (crtc_state->has_hdmi_sink) {
		intel_sdvo_set_encode(intel_sdvo, SDVO_ENCODE_HDMI);
		intel_sdvo_set_colorimetry(intel_sdvo,
					   SDVO_COLORIMETRY_RGB256);
		intel_sdvo_set_avi_infoframe(intel_sdvo, crtc_state);
	} else
		intel_sdvo_set_encode(intel_sdvo, SDVO_ENCODE_DVI);

	if (intel_sdvo->is_tv &&
	    !intel_sdvo_set_tv_format(intel_sdvo, conn_state))
		return;

	intel_sdvo_get_dtd_from_mode(&input_dtd, adjusted_mode);

	if (intel_sdvo->is_tv || intel_sdvo->is_lvds)
		input_dtd.part2.sdvo_flags = intel_sdvo->dtd_sdvo_flags;
	if (!intel_sdvo_set_input_timing(intel_sdvo, &input_dtd))
		DRM_INFO("Setting input timings on %s failed\n",
			 SDVO_NAME(intel_sdvo));

	switch (crtc_state->pixel_multiplier) {
	default:
		WARN(1, "unknown pixel multiplier specified\n");
	case 1: rate = SDVO_CLOCK_RATE_MULT_1X; break;
	case 2: rate = SDVO_CLOCK_RATE_MULT_2X; break;
	case 4: rate = SDVO_CLOCK_RATE_MULT_4X; break;
	}
	if (!intel_sdvo_set_clock_rate_mult(intel_sdvo, rate))
		return;

	/* Set the SDVO control regs. */
	if (INTEL_GEN(dev_priv) >= 4) {
		/* The real mode polarity is set by the SDVO commands, using
		 * struct intel_sdvo_dtd. */
		sdvox = SDVO_VSYNC_ACTIVE_HIGH | SDVO_HSYNC_ACTIVE_HIGH;
		if (!HAS_PCH_SPLIT(dev_priv) && crtc_state->limited_color_range)
			sdvox |= HDMI_COLOR_RANGE_16_235;
		if (INTEL_GEN(dev_priv) < 5)
			sdvox |= SDVO_BORDER_ENABLE;
	} else {
		sdvox = I915_READ(intel_sdvo->sdvo_reg);
		if (intel_sdvo->port == PORT_B)
			sdvox &= SDVOB_PRESERVE_MASK;
		else
			sdvox &= SDVOC_PRESERVE_MASK;
		sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;
	}

	if (HAS_PCH_CPT(dev_priv))
		sdvox |= SDVO_PIPE_SEL_CPT(crtc->pipe);
	else
		sdvox |= SDVO_PIPE_SEL(crtc->pipe);

	if (crtc_state->has_audio) {
		WARN_ON_ONCE(INTEL_GEN(dev_priv) < 4);
		sdvox |= SDVO_AUDIO_ENABLE;
	}

	if (INTEL_GEN(dev_priv) >= 4) {
		/* done in crtc_mode_set as the dpll_md reg must be written early */
	} else if (IS_I945G(dev_priv) || IS_I945GM(dev_priv) ||
		   IS_G33(dev_priv) || IS_PINEVIEW(dev_priv)) {
		/* done in crtc_mode_set as it lives inside the dpll register */
	} else {
		sdvox |= (crtc_state->pixel_multiplier - 1)
			<< SDVO_PORT_MULTIPLY_SHIFT;
	}

	if (input_dtd.part2.sdvo_flags & SDVO_NEED_TO_STALL &&
	    INTEL_GEN(dev_priv) < 5)
		sdvox |= SDVO_STALL_SELECT;
	intel_sdvo_write_sdvox(intel_sdvo, sdvox);
}

static bool intel_sdvo_connector_get_hw_state(struct intel_connector *connector)
{
	struct intel_sdvo_connector *intel_sdvo_connector =
		to_intel_sdvo_connector(&connector->base);
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(&connector->base);
	u16 active_outputs = 0;

	intel_sdvo_get_active_outputs(intel_sdvo, &active_outputs);

	if (active_outputs & intel_sdvo_connector->output_flag)
		return true;
	else
		return false;
}

static bool intel_sdvo_get_hw_state(struct intel_encoder *encoder,
				    enum pipe *pipe)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);
	u16 active_outputs = 0;
	u32 tmp;

	tmp = I915_READ(intel_sdvo->sdvo_reg);
	intel_sdvo_get_active_outputs(intel_sdvo, &active_outputs);

	if (!(tmp & SDVO_ENABLE) && (active_outputs == 0))
		return false;

	if (HAS_PCH_CPT(dev_priv))
		*pipe = PORT_TO_PIPE_CPT(tmp);
	else
		*pipe = PORT_TO_PIPE(tmp);

	return true;
}

static void intel_sdvo_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);
	struct intel_sdvo_dtd dtd;
	int encoder_pixel_multiplier = 0;
	int dotclock;
	u32 flags = 0, sdvox;
	u8 val;
	bool ret;

	sdvox = I915_READ(intel_sdvo->sdvo_reg);

	ret = intel_sdvo_get_input_timing(intel_sdvo, &dtd);
	if (!ret) {
		/* Some sdvo encoders are not spec compliant and don't
		 * implement the mandatory get_timings function. */
		DRM_DEBUG_DRIVER("failed to retrieve SDVO DTD\n");
		pipe_config->quirks |= PIPE_CONFIG_QUIRK_MODE_SYNC_FLAGS;
	} else {
		if (dtd.part2.dtd_flags & DTD_FLAG_HSYNC_POSITIVE)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (dtd.part2.dtd_flags & DTD_FLAG_VSYNC_POSITIVE)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	}

	pipe_config->base.adjusted_mode.flags |= flags;

	/*
	 * pixel multiplier readout is tricky: Only on i915g/gm it is stored in
	 * the sdvo port register, on all other platforms it is part of the dpll
	 * state. Since the general pipe state readout happens before the
	 * encoder->get_config we so already have a valid pixel multplier on all
	 * other platfroms.
	 */
	if (IS_I915G(dev_priv) || IS_I915GM(dev_priv)) {
		pipe_config->pixel_multiplier =
			((sdvox & SDVO_PORT_MULTIPLY_MASK)
			 >> SDVO_PORT_MULTIPLY_SHIFT) + 1;
	}

	dotclock = pipe_config->port_clock;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->base.adjusted_mode.crtc_clock = dotclock;

	/* Cross check the port pixel multiplier with the sdvo encoder state. */
	if (intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_CLOCK_RATE_MULT,
				 &val, 1)) {
		switch (val) {
		case SDVO_CLOCK_RATE_MULT_1X:
			encoder_pixel_multiplier = 1;
			break;
		case SDVO_CLOCK_RATE_MULT_2X:
			encoder_pixel_multiplier = 2;
			break;
		case SDVO_CLOCK_RATE_MULT_4X:
			encoder_pixel_multiplier = 4;
			break;
		}
	}

	if (sdvox & HDMI_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	if (sdvox & SDVO_AUDIO_ENABLE)
		pipe_config->has_audio = true;

	if (intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_ENCODE,
				 &val, 1)) {
		if (val == SDVO_ENCODE_HDMI)
			pipe_config->has_hdmi_sink = true;
	}

	WARN(encoder_pixel_multiplier != pipe_config->pixel_multiplier,
	     "SDVO pixel multiplier mismatch, port: %i, encoder: %i\n",
	     pipe_config->pixel_multiplier, encoder_pixel_multiplier);
}

static void intel_disable_sdvo(struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	u32 temp;

	intel_sdvo_set_active_outputs(intel_sdvo, 0);
	if (0)
		intel_sdvo_set_encoder_power_state(intel_sdvo,
						   DRM_MODE_DPMS_OFF);

	temp = I915_READ(intel_sdvo->sdvo_reg);

	temp &= ~SDVO_ENABLE;
	intel_sdvo_write_sdvox(intel_sdvo, temp);

	/*
	 * HW workaround for IBX, we need to move the port
	 * to transcoder A after disabling it to allow the
	 * matching DP port to be enabled on transcoder A.
	 */
	if (HAS_PCH_IBX(dev_priv) && crtc->pipe == PIPE_B) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		temp &= ~SDVO_PIPE_B_SELECT;
		temp |= SDVO_ENABLE;
		intel_sdvo_write_sdvox(intel_sdvo, temp);

		temp &= ~SDVO_ENABLE;
		intel_sdvo_write_sdvox(intel_sdvo, temp);

		intel_wait_for_vblank_if_active(dev_priv, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}
}

static void pch_disable_sdvo(struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
}

static void pch_post_disable_sdvo(struct intel_encoder *encoder,
				  const struct intel_crtc_state *old_crtc_state,
				  const struct drm_connector_state *old_conn_state)
{
	intel_disable_sdvo(encoder, old_crtc_state, old_conn_state);
}

static void intel_enable_sdvo(struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	u32 temp;
	bool input1, input2;
	int i;
	bool success;

	temp = I915_READ(intel_sdvo->sdvo_reg);
	temp |= SDVO_ENABLE;
	intel_sdvo_write_sdvox(intel_sdvo, temp);

	for (i = 0; i < 2; i++)
		intel_wait_for_vblank(dev_priv, intel_crtc->pipe);

	success = intel_sdvo_get_trained_inputs(intel_sdvo, &input1, &input2);
	/* Warn if the device reported failure to sync.
	 * A lot of SDVO devices fail to notify of sync, but it's
	 * a given it the status is a success, we succeeded.
	 */
	if (success && !input1) {
		DRM_DEBUG_KMS("First %s output reported failure to "
				"sync\n", SDVO_NAME(intel_sdvo));
	}

	if (0)
		intel_sdvo_set_encoder_power_state(intel_sdvo,
						   DRM_MODE_DPMS_ON);
	intel_sdvo_set_active_outputs(intel_sdvo, intel_sdvo->attached_output);
}

static enum drm_mode_status
intel_sdvo_mode_valid(struct drm_connector *connector,
		      struct drm_display_mode *mode)
{
	struct intel_sdvo *intel_sdvo = intel_attached_sdvo(connector);
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (intel_sdvo->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (intel_sdvo->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	if (mode->clock > max_dotclk)
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

static uint16_t intel_sdvo_get_hotplug_support(struct intel_sdvo *intel_sdvo)
{
	struct drm_i915_private *dev_priv = to_i915(intel_sdvo->base.base.dev);
	uint16_t hotplug;

	if (!I915_HAS_HOTPLUG(dev_priv))
		return 0;

	/* HW Erratum: SDVO Hotplug is broken on all i945G chips, there's noise
	 * on the line. */
	if (IS_I945G(dev_priv) || IS_I945GM(dev_priv))
		return 0;

	if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_HOT_PLUG_SUPPORT,
					&hotplug, sizeof(hotplug)))
		return 0;

	return hotplug;
}

static void intel_sdvo_enable_hotplug(struct intel_encoder *encoder)
{
	struct intel_sdvo *intel_sdvo = to_sdvo(encoder);

	intel_sdvo_write_cmd(intel_sdvo, SDVO_CMD_SET_ACTIVE_HOT_PLUG,
			&intel_sdvo->hotplug_active, 2);
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
	struct drm_i915_private *dev_priv = to_i915(connector->dev);

	return drm_get_edid(connector,
			    intel_gmbus_get_adapter(dev_priv,
						    dev_priv->vbt.crt_ddc_pin));
}

static enum drm_connector_status
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
				intel_sdvo->rgb_quant_range_selectable =
					drm_rgb_quant_range_selectable(edid);
			}
		} else
			status = connector_status_disconnected;
		kfree(edid);
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

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	if (!intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_ATTACHED_DISPLAYS,
				  &response, 2))
		return connector_status_unknown;

	DRM_DEBUG_KMS("SDVO response %d %d [%x]\n",
		      response & 0xff, response >> 8,
		      intel_sdvo_connector->output_flag);

	if (response == 0)
		return connector_status_disconnected;

	intel_sdvo->attached_output = response;

	intel_sdvo->has_hdmi_monitor = false;
	intel_sdvo->has_hdmi_audio = false;
	intel_sdvo->rgb_quant_range_selectable = false;

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

			kfree(edid);
		} else
			ret = connector_status_connected;
	}

	/* May update encoder flag for like clock for SDVO TV, etc.*/
	if (ret == connector_status_connected) {
		intel_sdvo->is_tv = false;
		intel_sdvo->is_lvds = false;

		if (response & SDVO_TV_MASK)
			intel_sdvo->is_tv = true;
		if (response & SDVO_LVDS_MASK)
			intel_sdvo->is_lvds = intel_sdvo->sdvo_lvds_fixed_mode != NULL;
	}

	return ret;
}

static void intel_sdvo_get_ddc_modes(struct drm_connector *connector)
{
	struct edid *edid;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

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
	const struct drm_connector_state *conn_state = connector->state;
	struct intel_sdvo_sdtv_resolution_request tv_res;
	uint32_t reply = 0, format_map = 0;
	int i;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	/* Read the list of supported input resolutions for the selected TV
	 * format.
	 */
	format_map = 1 << conn_state->tv.mode;
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
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct drm_display_mode *newmode;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
		      connector->base.id, connector->name);

	/*
	 * Fetch modes from VBT. For SDVO prefer the VBT mode since some
	 * SDVO->LVDS transcoders can't cope with the EDID mode.
	 */
	if (dev_priv->vbt.sdvo_lvds_vbt_mode != NULL) {
		newmode = drm_mode_duplicate(connector->dev,
					     dev_priv->vbt.sdvo_lvds_vbt_mode);
		if (newmode != NULL) {
			/* Guarantee the mode is preferred */
			newmode->type = (DRM_MODE_TYPE_PREFERRED |
					 DRM_MODE_TYPE_DRIVER);
			drm_mode_probed_add(connector, newmode);
		}
	}

	/*
	 * Attempt to get the mode list from DDC.
	 * Assume that the preferred modes are
	 * arranged in priority order.
	 */
	intel_ddc_get_modes(connector, &intel_sdvo->ddc);

	list_for_each_entry(newmode, &connector->probed_modes, head) {
		if (newmode->type & DRM_MODE_TYPE_PREFERRED) {
			intel_sdvo->sdvo_lvds_fixed_mode =
				drm_mode_duplicate(connector->dev, newmode);

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

static void intel_sdvo_destroy(struct drm_connector *connector)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);

	drm_connector_cleanup(connector);
	kfree(intel_sdvo_connector);
}

static int
intel_sdvo_connector_atomic_get_property(struct drm_connector *connector,
					 const struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
	const struct intel_sdvo_connector_state *sdvo_state = to_intel_sdvo_connector_state((void *)state);

	if (property == intel_sdvo_connector->tv_format) {
		int i;

		for (i = 0; i < intel_sdvo_connector->format_supported_num; i++)
			if (state->tv.mode == intel_sdvo_connector->tv_format_supported[i]) {
				*val = i;

				return 0;
			}

		WARN_ON(1);
		*val = 0;
	} else if (property == intel_sdvo_connector->top ||
		   property == intel_sdvo_connector->bottom)
		*val = intel_sdvo_connector->max_vscan - sdvo_state->tv.overscan_v;
	else if (property == intel_sdvo_connector->left ||
		 property == intel_sdvo_connector->right)
		*val = intel_sdvo_connector->max_hscan - sdvo_state->tv.overscan_h;
	else if (property == intel_sdvo_connector->hpos)
		*val = sdvo_state->tv.hpos;
	else if (property == intel_sdvo_connector->vpos)
		*val = sdvo_state->tv.vpos;
	else if (property == intel_sdvo_connector->saturation)
		*val = state->tv.saturation;
	else if (property == intel_sdvo_connector->contrast)
		*val = state->tv.contrast;
	else if (property == intel_sdvo_connector->hue)
		*val = state->tv.hue;
	else if (property == intel_sdvo_connector->brightness)
		*val = state->tv.brightness;
	else if (property == intel_sdvo_connector->sharpness)
		*val = sdvo_state->tv.sharpness;
	else if (property == intel_sdvo_connector->flicker_filter)
		*val = sdvo_state->tv.flicker_filter;
	else if (property == intel_sdvo_connector->flicker_filter_2d)
		*val = sdvo_state->tv.flicker_filter_2d;
	else if (property == intel_sdvo_connector->flicker_filter_adaptive)
		*val = sdvo_state->tv.flicker_filter_adaptive;
	else if (property == intel_sdvo_connector->tv_chroma_filter)
		*val = sdvo_state->tv.chroma_filter;
	else if (property == intel_sdvo_connector->tv_luma_filter)
		*val = sdvo_state->tv.luma_filter;
	else if (property == intel_sdvo_connector->dot_crawl)
		*val = sdvo_state->tv.dot_crawl;
	else
		return intel_digital_connector_atomic_get_property(connector, state, property, val);

	return 0;
}

static int
intel_sdvo_connector_atomic_set_property(struct drm_connector *connector,
					 struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct intel_sdvo_connector *intel_sdvo_connector = to_intel_sdvo_connector(connector);
	struct intel_sdvo_connector_state *sdvo_state = to_intel_sdvo_connector_state(state);

	if (property == intel_sdvo_connector->tv_format) {
		state->tv.mode = intel_sdvo_connector->tv_format_supported[val];

		if (state->crtc) {
			struct drm_crtc_state *crtc_state =
				drm_atomic_get_new_crtc_state(state->state, state->crtc);

			crtc_state->connectors_changed = true;
		}
	} else if (property == intel_sdvo_connector->top ||
		   property == intel_sdvo_connector->bottom)
		/* Cannot set these independent from each other */
		sdvo_state->tv.overscan_v = intel_sdvo_connector->max_vscan - val;
	else if (property == intel_sdvo_connector->left ||
		 property == intel_sdvo_connector->right)
		/* Cannot set these independent from each other */
		sdvo_state->tv.overscan_h = intel_sdvo_connector->max_hscan - val;
	else if (property == intel_sdvo_connector->hpos)
		sdvo_state->tv.hpos = val;
	else if (property == intel_sdvo_connector->vpos)
		sdvo_state->tv.vpos = val;
	else if (property == intel_sdvo_connector->saturation)
		state->tv.saturation = val;
	else if (property == intel_sdvo_connector->contrast)
		state->tv.contrast = val;
	else if (property == intel_sdvo_connector->hue)
		state->tv.hue = val;
	else if (property == intel_sdvo_connector->brightness)
		state->tv.brightness = val;
	else if (property == intel_sdvo_connector->sharpness)
		sdvo_state->tv.sharpness = val;
	else if (property == intel_sdvo_connector->flicker_filter)
		sdvo_state->tv.flicker_filter = val;
	else if (property == intel_sdvo_connector->flicker_filter_2d)
		sdvo_state->tv.flicker_filter_2d = val;
	else if (property == intel_sdvo_connector->flicker_filter_adaptive)
		sdvo_state->tv.flicker_filter_adaptive = val;
	else if (property == intel_sdvo_connector->tv_chroma_filter)
		sdvo_state->tv.chroma_filter = val;
	else if (property == intel_sdvo_connector->tv_luma_filter)
		sdvo_state->tv.luma_filter = val;
	else if (property == intel_sdvo_connector->dot_crawl)
		sdvo_state->tv.dot_crawl = val;
	else
		return intel_digital_connector_atomic_set_property(connector, state, property, val);

	return 0;
}

static int
intel_sdvo_connector_register(struct drm_connector *connector)
{
	struct intel_sdvo *sdvo = intel_attached_sdvo(connector);
	int ret;

	ret = intel_connector_register(connector);
	if (ret)
		return ret;

	return sysfs_create_link(&connector->kdev->kobj,
				 &sdvo->ddc.dev.kobj,
				 sdvo->ddc.dev.kobj.name);
}

static void
intel_sdvo_connector_unregister(struct drm_connector *connector)
{
	struct intel_sdvo *sdvo = intel_attached_sdvo(connector);

	sysfs_remove_link(&connector->kdev->kobj,
			  sdvo->ddc.dev.kobj.name);
	intel_connector_unregister(connector);
}

static struct drm_connector_state *
intel_sdvo_connector_duplicate_state(struct drm_connector *connector)
{
	struct intel_sdvo_connector_state *state;

	state = kmemdup(connector->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &state->base.base);
	return &state->base.base;
}

static const struct drm_connector_funcs intel_sdvo_connector_funcs = {
	.detect = intel_sdvo_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_sdvo_connector_atomic_get_property,
	.atomic_set_property = intel_sdvo_connector_atomic_set_property,
	.late_register = intel_sdvo_connector_register,
	.early_unregister = intel_sdvo_connector_unregister,
	.destroy = intel_sdvo_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_sdvo_connector_duplicate_state,
};

static int intel_sdvo_atomic_check(struct drm_connector *conn,
				   struct drm_connector_state *new_conn_state)
{
	struct drm_atomic_state *state = new_conn_state->state;
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(state, conn);
	struct intel_sdvo_connector_state *old_state =
		to_intel_sdvo_connector_state(old_conn_state);
	struct intel_sdvo_connector_state *new_state =
		to_intel_sdvo_connector_state(new_conn_state);

	if (new_conn_state->crtc &&
	    (memcmp(&old_state->tv, &new_state->tv, sizeof(old_state->tv)) ||
	     memcmp(&old_conn_state->tv, &new_conn_state->tv, sizeof(old_conn_state->tv)))) {
		struct drm_crtc_state *crtc_state =
			drm_atomic_get_new_crtc_state(new_conn_state->state,
						      new_conn_state->crtc);

		crtc_state->connectors_changed = true;
	}

	return intel_digital_connector_atomic_check(conn, new_conn_state);
}

static const struct drm_connector_helper_funcs intel_sdvo_connector_helper_funcs = {
	.get_modes = intel_sdvo_get_modes,
	.mode_valid = intel_sdvo_mode_valid,
	.atomic_check = intel_sdvo_atomic_check,
};

static void intel_sdvo_enc_destroy(struct drm_encoder *encoder)
{
	struct intel_sdvo *intel_sdvo = to_sdvo(to_intel_encoder(encoder));

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
			  struct intel_sdvo *sdvo)
{
	struct sdvo_device_mapping *mapping;

	if (sdvo->port == PORT_B)
		mapping = &dev_priv->vbt.sdvo_mappings[0];
	else
		mapping = &dev_priv->vbt.sdvo_mappings[1];

	if (mapping->initialized)
		sdvo->ddc_bus = 1 << ((mapping->ddc_pin & 0xf0) >> 4);
	else
		intel_sdvo_guess_ddc_bus(sdvo);
}

static void
intel_sdvo_select_i2c_bus(struct drm_i915_private *dev_priv,
			  struct intel_sdvo *sdvo)
{
	struct sdvo_device_mapping *mapping;
	u8 pin;

	if (sdvo->port == PORT_B)
		mapping = &dev_priv->vbt.sdvo_mappings[0];
	else
		mapping = &dev_priv->vbt.sdvo_mappings[1];

	if (mapping->initialized &&
	    intel_gmbus_is_valid_pin(dev_priv, mapping->i2c_pin))
		pin = mapping->i2c_pin;
	else
		pin = GMBUS_PIN_DPB;

	sdvo->i2c = intel_gmbus_get_adapter(dev_priv, pin);

	/* With gmbus we should be able to drive sdvo i2c at 2MHz, but somehow
	 * our code totally fails once we start using gmbus. Hence fall back to
	 * bit banging for now. */
	intel_gmbus_force_bit(sdvo->i2c, true);
}

/* undo any changes intel_sdvo_select_i2c_bus() did to sdvo->i2c */
static void
intel_sdvo_unselect_i2c_bus(struct intel_sdvo *sdvo)
{
	intel_gmbus_force_bit(sdvo->i2c, false);
}

static bool
intel_sdvo_is_hdmi_connector(struct intel_sdvo *intel_sdvo, int device)
{
	return intel_sdvo_check_supp_encode(intel_sdvo);
}

static u8
intel_sdvo_get_slave_addr(struct drm_i915_private *dev_priv,
			  struct intel_sdvo *sdvo)
{
	struct sdvo_device_mapping *my_mapping, *other_mapping;

	if (sdvo->port == PORT_B) {
		my_mapping = &dev_priv->vbt.sdvo_mappings[0];
		other_mapping = &dev_priv->vbt.sdvo_mappings[1];
	} else {
		my_mapping = &dev_priv->vbt.sdvo_mappings[1];
		other_mapping = &dev_priv->vbt.sdvo_mappings[0];
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
	if (sdvo->port == PORT_B)
		return 0x70;
	else
		return 0x72;
}

static int
intel_sdvo_connector_init(struct intel_sdvo_connector *connector,
			  struct intel_sdvo *encoder)
{
	struct drm_connector *drm_connector;
	int ret;

	drm_connector = &connector->base.base;
	ret = drm_connector_init(encoder->base.base.dev,
			   drm_connector,
			   &intel_sdvo_connector_funcs,
			   connector->base.base.connector_type);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(drm_connector,
				 &intel_sdvo_connector_helper_funcs);

	connector->base.base.interlace_allowed = 1;
	connector->base.base.doublescan_allowed = 0;
	connector->base.base.display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->base.get_hw_state = intel_sdvo_connector_get_hw_state;

	intel_connector_attach_encoder(&connector->base, &encoder->base);

	return 0;
}

static void
intel_sdvo_add_hdmi_properties(struct intel_sdvo *intel_sdvo,
			       struct intel_sdvo_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.base.dev);

	intel_attach_force_audio_property(&connector->base.base);
	if (INTEL_GEN(dev_priv) >= 4 && IS_MOBILE(dev_priv)) {
		intel_attach_broadcast_rgb_property(&connector->base.base);
	}
	intel_attach_aspect_ratio_property(&connector->base.base);
	connector->base.base.state->picture_aspect_ratio = HDMI_PICTURE_ASPECT_NONE;
}

static struct intel_sdvo_connector *intel_sdvo_connector_alloc(void)
{
	struct intel_sdvo_connector *sdvo_connector;
	struct intel_sdvo_connector_state *conn_state;

	sdvo_connector = kzalloc(sizeof(*sdvo_connector), GFP_KERNEL);
	if (!sdvo_connector)
		return NULL;

	conn_state = kzalloc(sizeof(*conn_state), GFP_KERNEL);
	if (!conn_state) {
		kfree(sdvo_connector);
		return NULL;
	}

	__drm_atomic_helper_connector_reset(&sdvo_connector->base.base,
					    &conn_state->base.base);

	return sdvo_connector;
}

static bool
intel_sdvo_dvi_init(struct intel_sdvo *intel_sdvo, int device)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_i915_private *dev_priv = to_i915(encoder->dev);
	struct drm_connector *connector;
	struct intel_encoder *intel_encoder = to_intel_encoder(encoder);
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	DRM_DEBUG_KMS("initialising DVI device %d\n", device);

	intel_sdvo_connector = intel_sdvo_connector_alloc();
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
	if (intel_sdvo_get_hotplug_support(intel_sdvo) &
		intel_sdvo_connector->output_flag) {
		intel_sdvo->hotplug_active |= intel_sdvo_connector->output_flag;
		/* Some SDVO devices have one-shot hotplug interrupts.
		 * Ensure that they get re-enabled when an interrupt happens.
		 */
		intel_encoder->hot_plug = intel_sdvo_enable_hotplug;
		intel_sdvo_enable_hotplug(intel_encoder);
	} else {
		intel_connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
	}
	encoder->encoder_type = DRM_MODE_ENCODER_TMDS;
	connector->connector_type = DRM_MODE_CONNECTOR_DVID;

	/* gen3 doesn't do the hdmi bits in the SDVO register */
	if (INTEL_GEN(dev_priv) >= 4 &&
	    intel_sdvo_is_hdmi_connector(intel_sdvo, device)) {
		connector->connector_type = DRM_MODE_CONNECTOR_HDMIA;
		intel_sdvo->is_hdmi = true;
	}

	if (intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo) < 0) {
		kfree(intel_sdvo_connector);
		return false;
	}

	if (intel_sdvo->is_hdmi)
		intel_sdvo_add_hdmi_properties(intel_sdvo, intel_sdvo_connector);

	return true;
}

static bool
intel_sdvo_tv_init(struct intel_sdvo *intel_sdvo, int type)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	DRM_DEBUG_KMS("initialising TV type %d\n", type);

	intel_sdvo_connector = intel_sdvo_connector_alloc();
	if (!intel_sdvo_connector)
		return false;

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	encoder->encoder_type = DRM_MODE_ENCODER_TVDAC;
	connector->connector_type = DRM_MODE_CONNECTOR_SVIDEO;

	intel_sdvo->controlled_output |= type;
	intel_sdvo_connector->output_flag = type;

	intel_sdvo->is_tv = true;

	if (intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo) < 0) {
		kfree(intel_sdvo_connector);
		return false;
	}

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

	DRM_DEBUG_KMS("initialising analog device %d\n", device);

	intel_sdvo_connector = intel_sdvo_connector_alloc();
	if (!intel_sdvo_connector)
		return false;

	intel_connector = &intel_sdvo_connector->base;
	connector = &intel_connector->base;
	intel_connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	encoder->encoder_type = DRM_MODE_ENCODER_DAC;
	connector->connector_type = DRM_MODE_CONNECTOR_VGA;

	if (device == 0) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB0;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB0;
	} else if (device == 1) {
		intel_sdvo->controlled_output |= SDVO_OUTPUT_RGB1;
		intel_sdvo_connector->output_flag = SDVO_OUTPUT_RGB1;
	}

	if (intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo) < 0) {
		kfree(intel_sdvo_connector);
		return false;
	}

	return true;
}

static bool
intel_sdvo_lvds_init(struct intel_sdvo *intel_sdvo, int device)
{
	struct drm_encoder *encoder = &intel_sdvo->base.base;
	struct drm_connector *connector;
	struct intel_connector *intel_connector;
	struct intel_sdvo_connector *intel_sdvo_connector;

	DRM_DEBUG_KMS("initialising LVDS device %d\n", device);

	intel_sdvo_connector = intel_sdvo_connector_alloc();
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

	if (intel_sdvo_connector_init(intel_sdvo_connector, intel_sdvo) < 0) {
		kfree(intel_sdvo_connector);
		return false;
	}

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

	if (flags & SDVO_OUTPUT_YPRPB0)
		if (!intel_sdvo_tv_init(intel_sdvo, SDVO_OUTPUT_YPRPB0))
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

static void intel_sdvo_output_cleanup(struct intel_sdvo *intel_sdvo)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct drm_connector *connector, *tmp;

	list_for_each_entry_safe(connector, tmp,
				 &dev->mode_config.connector_list, head) {
		if (intel_attached_encoder(connector) == &intel_sdvo->base) {
			drm_connector_unregister(connector);
			intel_sdvo_destroy(connector);
		}
	}
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

	intel_sdvo_connector->base.base.state->tv.mode = intel_sdvo_connector->tv_format_supported[0];
	drm_object_attach_property(&intel_sdvo_connector->base.base.base,
				   intel_sdvo_connector->tv_format, 0);
	return true;

}

#define _ENHANCEMENT(state_assignment, name, NAME) do { \
	if (enhancements.name) { \
		if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_MAX_##NAME, &data_value, 4) || \
		    !intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_##NAME, &response, 2)) \
			return false; \
		intel_sdvo_connector->name = \
			drm_property_create_range(dev, 0, #name, 0, data_value[0]); \
		if (!intel_sdvo_connector->name) return false; \
		state_assignment = response; \
		drm_object_attach_property(&connector->base, \
					   intel_sdvo_connector->name, 0); \
		DRM_DEBUG_KMS(#name ": max %d, default %d, current %d\n", \
			      data_value[0], data_value[1], response); \
	} \
} while (0)

#define ENHANCEMENT(state, name, NAME) _ENHANCEMENT((state)->name, name, NAME)

static bool
intel_sdvo_create_enhance_property_tv(struct intel_sdvo *intel_sdvo,
				      struct intel_sdvo_connector *intel_sdvo_connector,
				      struct intel_sdvo_enhancements_reply enhancements)
{
	struct drm_device *dev = intel_sdvo->base.base.dev;
	struct drm_connector *connector = &intel_sdvo_connector->base.base;
	struct drm_connector_state *conn_state = connector->state;
	struct intel_sdvo_connector_state *sdvo_state =
		to_intel_sdvo_connector_state(conn_state);
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

		sdvo_state->tv.overscan_h = response;

		intel_sdvo_connector->max_hscan = data_value[0];
		intel_sdvo_connector->left =
			drm_property_create_range(dev, 0, "left_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->left)
			return false;

		drm_object_attach_property(&connector->base,
					   intel_sdvo_connector->left, 0);

		intel_sdvo_connector->right =
			drm_property_create_range(dev, 0, "right_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->right)
			return false;

		drm_object_attach_property(&connector->base,
					      intel_sdvo_connector->right, 0);
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

		sdvo_state->tv.overscan_v = response;

		intel_sdvo_connector->max_vscan = data_value[0];
		intel_sdvo_connector->top =
			drm_property_create_range(dev, 0,
					    "top_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->top)
			return false;

		drm_object_attach_property(&connector->base,
					   intel_sdvo_connector->top, 0);

		intel_sdvo_connector->bottom =
			drm_property_create_range(dev, 0,
					    "bottom_margin", 0, data_value[0]);
		if (!intel_sdvo_connector->bottom)
			return false;

		drm_object_attach_property(&connector->base,
					      intel_sdvo_connector->bottom, 0);
		DRM_DEBUG_KMS("v_overscan: max %d, "
			      "default %d, current %d\n",
			      data_value[0], data_value[1], response);
	}

	ENHANCEMENT(&sdvo_state->tv, hpos, HPOS);
	ENHANCEMENT(&sdvo_state->tv, vpos, VPOS);
	ENHANCEMENT(&conn_state->tv, saturation, SATURATION);
	ENHANCEMENT(&conn_state->tv, contrast, CONTRAST);
	ENHANCEMENT(&conn_state->tv, hue, HUE);
	ENHANCEMENT(&conn_state->tv, brightness, BRIGHTNESS);
	ENHANCEMENT(&sdvo_state->tv, sharpness, SHARPNESS);
	ENHANCEMENT(&sdvo_state->tv, flicker_filter, FLICKER_FILTER);
	ENHANCEMENT(&sdvo_state->tv, flicker_filter_adaptive, FLICKER_FILTER_ADAPTIVE);
	ENHANCEMENT(&sdvo_state->tv, flicker_filter_2d, FLICKER_FILTER_2D);
	_ENHANCEMENT(sdvo_state->tv.chroma_filter, tv_chroma_filter, TV_CHROMA_FILTER);
	_ENHANCEMENT(sdvo_state->tv.luma_filter, tv_luma_filter, TV_LUMA_FILTER);

	if (enhancements.dot_crawl) {
		if (!intel_sdvo_get_value(intel_sdvo, SDVO_CMD_GET_DOT_CRAWL, &response, 2))
			return false;

		sdvo_state->tv.dot_crawl = response & 0x1;
		intel_sdvo_connector->dot_crawl =
			drm_property_create_range(dev, 0, "dot_crawl", 0, 1);
		if (!intel_sdvo_connector->dot_crawl)
			return false;

		drm_object_attach_property(&connector->base,
					   intel_sdvo_connector->dot_crawl, 0);
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

	ENHANCEMENT(&connector->state->tv, brightness, BRIGHTNESS);

	return true;
}
#undef ENHANCEMENT
#undef _ENHANCEMENT

static bool intel_sdvo_create_enhance_property(struct intel_sdvo *intel_sdvo,
					       struct intel_sdvo_connector *intel_sdvo_connector)
{
	union {
		struct intel_sdvo_enhancements_reply reply;
		uint16_t response;
	} enhancements;

	BUILD_BUG_ON(sizeof(enhancements) != 2);

	if (!intel_sdvo_get_value(intel_sdvo,
				  SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS,
				  &enhancements, sizeof(enhancements)) ||
	    enhancements.response == 0) {
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

	if (!__intel_sdvo_set_control_bus_switch(sdvo, sdvo->ddc_bus))
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

static void proxy_lock_bus(struct i2c_adapter *adapter,
			   unsigned int flags)
{
	struct intel_sdvo *sdvo = adapter->algo_data;
	sdvo->i2c->lock_ops->lock_bus(sdvo->i2c, flags);
}

static int proxy_trylock_bus(struct i2c_adapter *adapter,
			     unsigned int flags)
{
	struct intel_sdvo *sdvo = adapter->algo_data;
	return sdvo->i2c->lock_ops->trylock_bus(sdvo->i2c, flags);
}

static void proxy_unlock_bus(struct i2c_adapter *adapter,
			     unsigned int flags)
{
	struct intel_sdvo *sdvo = adapter->algo_data;
	sdvo->i2c->lock_ops->unlock_bus(sdvo->i2c, flags);
}

static const struct i2c_lock_operations proxy_lock_ops = {
	.lock_bus =    proxy_lock_bus,
	.trylock_bus = proxy_trylock_bus,
	.unlock_bus =  proxy_unlock_bus,
};

static bool
intel_sdvo_init_ddc_proxy(struct intel_sdvo *sdvo,
			  struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	sdvo->ddc.owner = THIS_MODULE;
	sdvo->ddc.class = I2C_CLASS_DDC;
	snprintf(sdvo->ddc.name, I2C_NAME_SIZE, "SDVO DDC proxy");
	sdvo->ddc.dev.parent = &pdev->dev;
	sdvo->ddc.algo_data = sdvo;
	sdvo->ddc.algo = &intel_sdvo_ddc_proxy;
	sdvo->ddc.lock_ops = &proxy_lock_ops;

	return i2c_add_adapter(&sdvo->ddc) == 0;
}

static void assert_sdvo_port_valid(const struct drm_i915_private *dev_priv,
				   enum port port)
{
	if (HAS_PCH_SPLIT(dev_priv))
		WARN_ON(port != PORT_B);
	else
		WARN_ON(port != PORT_B && port != PORT_C);
}

bool intel_sdvo_init(struct drm_i915_private *dev_priv,
		     i915_reg_t sdvo_reg, enum port port)
{
	struct intel_encoder *intel_encoder;
	struct intel_sdvo *intel_sdvo;
	int i;

	assert_sdvo_port_valid(dev_priv, port);

	intel_sdvo = kzalloc(sizeof(*intel_sdvo), GFP_KERNEL);
	if (!intel_sdvo)
		return false;

	intel_sdvo->sdvo_reg = sdvo_reg;
	intel_sdvo->port = port;
	intel_sdvo->slave_addr =
		intel_sdvo_get_slave_addr(dev_priv, intel_sdvo) >> 1;
	intel_sdvo_select_i2c_bus(dev_priv, intel_sdvo);
	if (!intel_sdvo_init_ddc_proxy(intel_sdvo, dev_priv))
		goto err_i2c_bus;

	/* encoder type will be decided later */
	intel_encoder = &intel_sdvo->base;
	intel_encoder->type = INTEL_OUTPUT_SDVO;
	intel_encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
	intel_encoder->port = port;
	drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
			 &intel_sdvo_enc_funcs, 0,
			 "SDVO %c", port_name(port));

	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		u8 byte;

		if (!intel_sdvo_read_byte(intel_sdvo, i, &byte)) {
			DRM_DEBUG_KMS("No SDVO device found on %s\n",
				      SDVO_NAME(intel_sdvo));
			goto err;
		}
	}

	intel_encoder->compute_config = intel_sdvo_compute_config;
	if (HAS_PCH_SPLIT(dev_priv)) {
		intel_encoder->disable = pch_disable_sdvo;
		intel_encoder->post_disable = pch_post_disable_sdvo;
	} else {
		intel_encoder->disable = intel_disable_sdvo;
	}
	intel_encoder->pre_enable = intel_sdvo_pre_enable;
	intel_encoder->enable = intel_enable_sdvo;
	intel_encoder->get_hw_state = intel_sdvo_get_hw_state;
	intel_encoder->get_config = intel_sdvo_get_config;

	/* In default case sdvo lvds is false */
	if (!intel_sdvo_get_capabilities(intel_sdvo, &intel_sdvo->caps))
		goto err;

	if (intel_sdvo_output_setup(intel_sdvo,
				    intel_sdvo->caps.output_flags) != true) {
		DRM_DEBUG_KMS("SDVO output failed to setup on %s\n",
			      SDVO_NAME(intel_sdvo));
		/* Output_setup can leave behind connectors! */
		goto err_output;
	}

	/* Only enable the hotplug irq if we need it, to work around noisy
	 * hotplug lines.
	 */
	if (intel_sdvo->hotplug_active) {
		if (intel_sdvo->port == PORT_B)
			intel_encoder->hpd_pin = HPD_SDVO_B;
		else
			intel_encoder->hpd_pin = HPD_SDVO_C;
	}

	/*
	 * Cloning SDVO with anything is often impossible, since the SDVO
	 * encoder can request a special input timing mode. And even if that's
	 * not the case we have evidence that cloning a plain unscaled mode with
	 * VGA doesn't really work. Furthermore the cloning flags are way too
	 * simplistic anyway to express such constraints, so just give up on
	 * cloning for SDVO encoders.
	 */
	intel_sdvo->base.cloneable = 0;

	intel_sdvo_select_ddc_bus(dev_priv, intel_sdvo);

	/* Set the input timing to the screen. Assume always input 0. */
	if (!intel_sdvo_set_target_input(intel_sdvo))
		goto err_output;

	if (!intel_sdvo_get_input_pixel_clock_range(intel_sdvo,
						    &intel_sdvo->pixel_clock_min,
						    &intel_sdvo->pixel_clock_max))
		goto err_output;

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

err_output:
	intel_sdvo_output_cleanup(intel_sdvo);

err:
	drm_encoder_cleanup(&intel_encoder->base);
	i2c_del_adapter(&intel_sdvo->ddc);
err_i2c_bus:
	intel_sdvo_unselect_i2c_bus(intel_sdvo);
	kfree(intel_sdvo);

	return false;
}
