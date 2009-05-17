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
#include <linux/delay.h>
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_sdvo_regs.h"

#undef SDVO_DEBUG

struct intel_sdvo_priv {
	struct intel_i2c_chan *i2c_bus;
	int slaveaddr;

	/* Register for the SDVO device: SDVOB or SDVOC */
	int output_device;

	/* Active outputs controlled by this SDVO output */
	uint16_t controlled_output;

	/*
	 * Capabilities of the SDVO device returned by
	 * i830_sdvo_get_capabilities()
	 */
	struct intel_sdvo_caps caps;

	/* Pixel clock limitations reported by the SDVO device, in kHz */
	int pixel_clock_min, pixel_clock_max;

	/**
	 * This is set if we're going to treat the device as TV-out.
	 *
	 * While we have these nice friendly flags for output types that ought
	 * to decide this for us, the S-Video output on our HDMI+S-Video card
	 * shows up as RGB1 (VGA).
	 */
	bool is_tv;

	/**
	 * This is set if we treat the device as HDMI, instead of DVI.
	 */
	bool is_hdmi;

	/**
	 * Returned SDTV resolutions allowed for the current format, if the
	 * device reported it.
	 */
	struct intel_sdvo_sdtv_resolution_reply sdtv_resolutions;

	/**
	 * Current selected TV format.
	 *
	 * This is stored in the same structure that's passed to the device, for
	 * convenience.
	 */
	struct intel_sdvo_tv_format tv_format;

	/*
	 * supported encoding mode, used to determine whether HDMI is
	 * supported
	 */
	struct intel_sdvo_encode encode;

	/* DDC bus used by this SDVO output */
	uint8_t ddc_bus;

	int save_sdvo_mult;
	u16 save_active_outputs;
	struct intel_sdvo_dtd save_input_dtd_1, save_input_dtd_2;
	struct intel_sdvo_dtd save_output_dtd[16];
	u32 save_SDVOX;
};

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
static void intel_sdvo_write_sdvox(struct intel_output *intel_output, u32 val)
{
	struct drm_device *dev = intel_output->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_sdvo_priv   *sdvo_priv = intel_output->dev_priv;
	u32 bval = val, cval = val;
	int i;

	if (sdvo_priv->output_device == SDVOB) {
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

static bool intel_sdvo_read_byte(struct intel_output *intel_output, u8 addr,
				 u8 *ch)
{
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	u8 out_buf[2];
	u8 buf[2];
	int ret;

	struct i2c_msg msgs[] = {
		{
			.addr = sdvo_priv->i2c_bus->slave_addr,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = sdvo_priv->i2c_bus->slave_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = 0;

	if ((ret = i2c_transfer(&sdvo_priv->i2c_bus->adapter, msgs, 2)) == 2)
	{
		*ch = buf[0];
		return true;
	}

	DRM_DEBUG("i2c transfer returned %d\n", ret);
	return false;
}

static bool intel_sdvo_write_byte(struct intel_output *intel_output, int addr,
				  u8 ch)
{
	u8 out_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = intel_output->i2c_bus->slave_addr,
			.flags = 0,
			.len = 2,
			.buf = out_buf,
		}
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(&intel_output->i2c_bus->adapter, msgs, 1) == 1)
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

#define SDVO_NAME(dev_priv) ((dev_priv)->output_device == SDVOB ? "SDVOB" : "SDVOC")
#define SDVO_PRIV(output)   ((struct intel_sdvo_priv *) (output)->dev_priv)

#ifdef SDVO_DEBUG
static void intel_sdvo_debug_write(struct intel_output *intel_output, u8 cmd,
				   void *args, int args_len)
{
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	int i;

	printk(KERN_DEBUG "%s: W: %02X ", SDVO_NAME(sdvo_priv), cmd);
	for (i = 0; i < args_len; i++)
		printk(KERN_DEBUG "%02X ", ((u8 *)args)[i]);
	for (; i < 8; i++)
		printk(KERN_DEBUG "   ");
	for (i = 0; i < sizeof(sdvo_cmd_names) / sizeof(sdvo_cmd_names[0]); i++) {
		if (cmd == sdvo_cmd_names[i].cmd) {
			printk(KERN_DEBUG "(%s)", sdvo_cmd_names[i].name);
			break;
		}
	}
	if (i == sizeof(sdvo_cmd_names)/ sizeof(sdvo_cmd_names[0]))
		printk(KERN_DEBUG "(%02X)", cmd);
	printk(KERN_DEBUG "\n");
}
#else
#define intel_sdvo_debug_write(o, c, a, l)
#endif

static void intel_sdvo_write_cmd(struct intel_output *intel_output, u8 cmd,
				 void *args, int args_len)
{
	int i;

	intel_sdvo_debug_write(intel_output, cmd, args, args_len);

	for (i = 0; i < args_len; i++) {
		intel_sdvo_write_byte(intel_output, SDVO_I2C_ARG_0 - i,
				      ((u8*)args)[i]);
	}

	intel_sdvo_write_byte(intel_output, SDVO_I2C_OPCODE, cmd);
}

#ifdef SDVO_DEBUG
static const char *cmd_status_names[] = {
	"Power on",
	"Success",
	"Not supported",
	"Invalid arg",
	"Pending",
	"Target not specified",
	"Scaling not supported"
};

static void intel_sdvo_debug_response(struct intel_output *intel_output,
				      void *response, int response_len,
				      u8 status)
{
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	int i;

	printk(KERN_DEBUG "%s: R: ", SDVO_NAME(sdvo_priv));
	for (i = 0; i < response_len; i++)
		printk(KERN_DEBUG "%02X ", ((u8 *)response)[i]);
	for (; i < 8; i++)
		printk(KERN_DEBUG "   ");
	if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
		printk(KERN_DEBUG "(%s)", cmd_status_names[status]);
	else
		printk(KERN_DEBUG "(??? %d)", status);
	printk(KERN_DEBUG "\n");
}
#else
#define intel_sdvo_debug_response(o, r, l, s)
#endif

static u8 intel_sdvo_read_response(struct intel_output *intel_output,
				   void *response, int response_len)
{
	int i;
	u8 status;
	u8 retry = 50;

	while (retry--) {
		/* Read the command response */
		for (i = 0; i < response_len; i++) {
			intel_sdvo_read_byte(intel_output,
					     SDVO_I2C_RETURN_0 + i,
					     &((u8 *)response)[i]);
		}

		/* read the return status */
		intel_sdvo_read_byte(intel_output, SDVO_I2C_CMD_STATUS,
				     &status);

		intel_sdvo_debug_response(intel_output, response, response_len,
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
 * Don't check status code from this as it switches the bus back to the
 * SDVO chips which defeats the purpose of doing a bus switch in the first
 * place.
 */
static void intel_sdvo_set_control_bus_switch(struct intel_output *intel_output,
					      u8 target)
{
	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_CONTROL_BUS_SWITCH, &target, 1);
}

static bool intel_sdvo_set_target_input(struct intel_output *intel_output, bool target_0, bool target_1)
{
	struct intel_sdvo_set_target_input_args targets = {0};
	u8 status;

	if (target_0 && target_1)
		return SDVO_CMD_STATUS_NOTSUPP;

	if (target_1)
		targets.target_1 = 1;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_TARGET_INPUT, &targets,
			     sizeof(targets));

	status = intel_sdvo_read_response(intel_output, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static bool intel_sdvo_get_trained_inputs(struct intel_output *intel_output, bool *input_1, bool *input_2)
{
	struct intel_sdvo_get_trained_inputs_response response;
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_TRAINED_INPUTS, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &response, sizeof(response));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	*input_1 = response.input0_trained;
	*input_2 = response.input1_trained;
	return true;
}

static bool intel_sdvo_get_active_outputs(struct intel_output *intel_output,
					  u16 *outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_ACTIVE_OUTPUTS, NULL, 0);
	status = intel_sdvo_read_response(intel_output, outputs, sizeof(*outputs));

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_active_outputs(struct intel_output *intel_output,
					  u16 outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_ACTIVE_OUTPUTS, &outputs,
			     sizeof(outputs));
	status = intel_sdvo_read_response(intel_output, NULL, 0);
	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_encoder_power_state(struct intel_output *intel_output,
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

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_ENCODER_POWER_STATE, &state,
			     sizeof(state));
	status = intel_sdvo_read_response(intel_output, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_get_input_pixel_clock_range(struct intel_output *intel_output,
						   int *clock_min,
						   int *clock_max)
{
	struct intel_sdvo_pixel_clock_range clocks;
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE,
			     NULL, 0);

	status = intel_sdvo_read_response(intel_output, &clocks, sizeof(clocks));

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	/* Convert the values from units of 10 kHz to kHz. */
	*clock_min = clocks.min * 10;
	*clock_max = clocks.max * 10;

	return true;
}

static bool intel_sdvo_set_target_output(struct intel_output *intel_output,
					 u16 outputs)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_TARGET_OUTPUT, &outputs,
			     sizeof(outputs));

	status = intel_sdvo_read_response(intel_output, NULL, 0);
	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_get_timing(struct intel_output *intel_output, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, cmd, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &dtd->part1,
					  sizeof(dtd->part1));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(intel_output, cmd + 1, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &dtd->part2,
					  sizeof(dtd->part2));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_get_input_timing(struct intel_output *intel_output,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_timing(intel_output,
				     SDVO_CMD_GET_INPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_get_output_timing(struct intel_output *intel_output,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_get_timing(intel_output,
				     SDVO_CMD_GET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_set_timing(struct intel_output *intel_output, u8 cmd,
				  struct intel_sdvo_dtd *dtd)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, cmd, &dtd->part1, sizeof(dtd->part1));
	status = intel_sdvo_read_response(intel_output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(intel_output, cmd + 1, &dtd->part2, sizeof(dtd->part2));
	status = intel_sdvo_read_response(intel_output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_set_input_timing(struct intel_output *intel_output,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_output,
				     SDVO_CMD_SET_INPUT_TIMINGS_PART1, dtd);
}

static bool intel_sdvo_set_output_timing(struct intel_output *intel_output,
					 struct intel_sdvo_dtd *dtd)
{
	return intel_sdvo_set_timing(intel_output,
				     SDVO_CMD_SET_OUTPUT_TIMINGS_PART1, dtd);
}

static bool
intel_sdvo_create_preferred_input_timing(struct intel_output *output,
					 uint16_t clock,
					 uint16_t width,
					 uint16_t height)
{
	struct intel_sdvo_preferred_input_timing_args args;
	uint8_t status;

	memset(&args, 0, sizeof(args));
	args.clock = clock;
	args.width = width;
	args.height = height;
	args.interlace = 0;
	args.scaled = 0;
	intel_sdvo_write_cmd(output, SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING,
			     &args, sizeof(args));
	status = intel_sdvo_read_response(output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool intel_sdvo_get_preferred_input_timing(struct intel_output *output,
						  struct intel_sdvo_dtd *dtd)
{
	bool status;

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1,
			     NULL, 0);

	status = intel_sdvo_read_response(output, &dtd->part1,
					  sizeof(dtd->part1));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2,
			     NULL, 0);

	status = intel_sdvo_read_response(output, &dtd->part2,
					  sizeof(dtd->part2));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return false;
}

static int intel_sdvo_get_clock_rate_mult(struct intel_output *intel_output)
{
	u8 response, status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_CLOCK_RATE_MULT, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &response, 1);

	if (status != SDVO_CMD_STATUS_SUCCESS) {
		DRM_DEBUG("Couldn't get SDVO clock rate multiplier\n");
		return SDVO_CLOCK_RATE_MULT_1X;
	} else {
		DRM_DEBUG("Current clock rate multiplier: %d\n", response);
	}

	return response;
}

static bool intel_sdvo_set_clock_rate_mult(struct intel_output *intel_output, u8 val)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_CLOCK_RATE_MULT, &val, 1);
	status = intel_sdvo_read_response(intel_output, NULL, 0);
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

static bool intel_sdvo_get_supp_encode(struct intel_output *output,
				       struct intel_sdvo_encode *encode)
{
	uint8_t status;

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_SUPP_ENCODE, NULL, 0);
	status = intel_sdvo_read_response(output, encode, sizeof(*encode));
	if (status != SDVO_CMD_STATUS_SUCCESS) { /* non-support means DVI */
		memset(encode, 0, sizeof(*encode));
		return false;
	}

	return true;
}

static bool intel_sdvo_set_encode(struct intel_output *output, uint8_t mode)
{
	uint8_t status;

	intel_sdvo_write_cmd(output, SDVO_CMD_SET_ENCODE, &mode, 1);
	status = intel_sdvo_read_response(output, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

static bool intel_sdvo_set_colorimetry(struct intel_output *output,
				       uint8_t mode)
{
	uint8_t status;

	intel_sdvo_write_cmd(output, SDVO_CMD_SET_COLORIMETRY, &mode, 1);
	status = intel_sdvo_read_response(output, NULL, 0);

	return (status == SDVO_CMD_STATUS_SUCCESS);
}

#if 0
static void intel_sdvo_dump_hdmi_buf(struct intel_output *output)
{
	int i, j;
	uint8_t set_buf_index[2];
	uint8_t av_split;
	uint8_t buf_size;
	uint8_t buf[48];
	uint8_t *pos;

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_AV_SPLIT, NULL, 0);
	intel_sdvo_read_response(output, &av_split, 1);

	for (i = 0; i <= av_split; i++) {
		set_buf_index[0] = i; set_buf_index[1] = 0;
		intel_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX,
				     set_buf_index, 2);
		intel_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_INFO, NULL, 0);
		intel_sdvo_read_response(output, &buf_size, 1);

		pos = buf;
		for (j = 0; j <= buf_size; j += 8) {
			intel_sdvo_write_cmd(output, SDVO_CMD_GET_HBUF_DATA,
					     NULL, 0);
			intel_sdvo_read_response(output, pos, 8);
			pos += 8;
		}
	}
}
#endif

static void intel_sdvo_set_hdmi_buf(struct intel_output *output, int index,
				uint8_t *data, int8_t size, uint8_t tx_rate)
{
    uint8_t set_buf_index[2];

    set_buf_index[0] = index;
    set_buf_index[1] = 0;

    intel_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_INDEX, set_buf_index, 2);

    for (; size > 0; size -= 8) {
	intel_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_DATA, data, 8);
	data += 8;
    }

    intel_sdvo_write_cmd(output, SDVO_CMD_SET_HBUF_TXRATE, &tx_rate, 1);
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

static void intel_sdvo_set_avi_infoframe(struct intel_output *output,
					 struct drm_display_mode * mode)
{
	struct dip_infoframe avi_if = {
		.type = DIP_TYPE_AVI,
		.version = DIP_VERSION_AVI,
		.len = DIP_LEN_AVI,
	};

	avi_if.checksum = intel_sdvo_calc_hbuf_csum((uint8_t *)&avi_if,
						    4 + avi_if.len);
	intel_sdvo_set_hdmi_buf(output, 1, (uint8_t *)&avi_if, 4 + avi_if.len,
				SDVO_HBUF_TX_VSYNC);
}

static void intel_sdvo_set_tv_format(struct intel_output *output)
{
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;
	struct intel_sdvo_tv_format *format, unset;
	u8 status;

	format = &sdvo_priv->tv_format;
	memset(&unset, 0, sizeof(unset));
	if (memcmp(format, &unset, sizeof(*format))) {
		DRM_DEBUG("%s: Choosing default TV format of NTSC-M\n",
				SDVO_NAME(sdvo_priv));
		format->ntsc_m = 1;
		intel_sdvo_write_cmd(output, SDVO_CMD_SET_TV_FORMAT, format,
				sizeof(*format));
		status = intel_sdvo_read_response(output, NULL, 0);
		if (status != SDVO_CMD_STATUS_SUCCESS)
			DRM_DEBUG("%s: Failed to set TV format\n",
					SDVO_NAME(sdvo_priv));
	}
}

static bool intel_sdvo_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct intel_output *output = enc_to_intel_output(encoder);
	struct intel_sdvo_priv *dev_priv = output->dev_priv;

	if (!dev_priv->is_tv) {
		/* Make the CRTC code factor in the SDVO pixel multiplier.  The
		 * SDVO device will be told of the multiplier during mode_set.
		 */
		adjusted_mode->clock *= intel_sdvo_get_pixel_multiplier(mode);
	} else {
		struct intel_sdvo_dtd output_dtd;
		bool success;

		/* We need to construct preferred input timings based on our
		 * output timings.  To do that, we have to set the output
		 * timings, even though this isn't really the right place in
		 * the sequence to do it. Oh well.
		 */


		/* Set output timings */
		intel_sdvo_get_dtd_from_mode(&output_dtd, mode);
		intel_sdvo_set_target_output(output,
					     dev_priv->controlled_output);
		intel_sdvo_set_output_timing(output, &output_dtd);

		/* Set the input timing to the screen. Assume always input 0. */
		intel_sdvo_set_target_input(output, true, false);


		success = intel_sdvo_create_preferred_input_timing(output,
								   mode->clock / 10,
								   mode->hdisplay,
								   mode->vdisplay);
		if (success) {
			struct intel_sdvo_dtd input_dtd;

			intel_sdvo_get_preferred_input_timing(output,
							     &input_dtd);
			intel_sdvo_get_mode_from_dtd(adjusted_mode, &input_dtd);

			drm_mode_set_crtcinfo(adjusted_mode, 0);

			mode->clock = adjusted_mode->clock;

			adjusted_mode->clock *=
				intel_sdvo_get_pixel_multiplier(mode);
		} else {
			return false;
		}
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
	struct intel_output *output = enc_to_intel_output(encoder);
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;
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

	intel_sdvo_write_cmd(output, SDVO_CMD_SET_IN_OUT_MAP,
			     &in_out, sizeof(in_out));
	status = intel_sdvo_read_response(output, NULL, 0);

	if (sdvo_priv->is_hdmi) {
		intel_sdvo_set_avi_infoframe(output, mode);
		sdvox |= SDVO_AUDIO_ENABLE;
	}

	/* We have tried to get input timing in mode_fixup, and filled into
	   adjusted_mode */
	if (sdvo_priv->is_tv)
		intel_sdvo_get_dtd_from_mode(&input_dtd, adjusted_mode);
	else
		intel_sdvo_get_dtd_from_mode(&input_dtd, mode);

	/* If it's a TV, we already set the output timing in mode_fixup.
	 * Otherwise, the output timing is equal to the input timing.
	 */
	if (!sdvo_priv->is_tv) {
		/* Set the output timing to the screen */
		intel_sdvo_set_target_output(output,
					     sdvo_priv->controlled_output);
		intel_sdvo_set_output_timing(output, &input_dtd);
	}

	/* Set the input timing to the screen. Assume always input 0. */
	intel_sdvo_set_target_input(output, true, false);

	if (sdvo_priv->is_tv)
		intel_sdvo_set_tv_format(output);

	/* We would like to use intel_sdvo_create_preferred_input_timing() to
	 * provide the device with a timing it can support, if it supports that
	 * feature.  However, presumably we would need to adjust the CRTC to
	 * output the preferred timing, and we don't support that currently.
	 */
#if 0
	success = intel_sdvo_create_preferred_input_timing(output, clock,
							   width, height);
	if (success) {
		struct intel_sdvo_dtd *input_dtd;

		intel_sdvo_get_preferred_input_timing(output, &input_dtd);
		intel_sdvo_set_input_timing(output, &input_dtd);
	}
#else
	intel_sdvo_set_input_timing(output, &input_dtd);
#endif

	switch (intel_sdvo_get_pixel_multiplier(mode)) {
	case 1:
		intel_sdvo_set_clock_rate_mult(output,
					       SDVO_CLOCK_RATE_MULT_1X);
		break;
	case 2:
		intel_sdvo_set_clock_rate_mult(output,
					       SDVO_CLOCK_RATE_MULT_2X);
		break;
	case 4:
		intel_sdvo_set_clock_rate_mult(output,
					       SDVO_CLOCK_RATE_MULT_4X);
		break;
	}

	/* Set the SDVO control regs. */
	if (IS_I965G(dev)) {
		sdvox |= SDVO_BORDER_ENABLE |
			SDVO_VSYNC_ACTIVE_HIGH |
			SDVO_HSYNC_ACTIVE_HIGH;
	} else {
		sdvox |= I915_READ(sdvo_priv->output_device);
		switch (sdvo_priv->output_device) {
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

	intel_sdvo_write_sdvox(output, sdvox);
}

static void intel_sdvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_output *intel_output = enc_to_intel_output(encoder);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	u32 temp;

	if (mode != DRM_MODE_DPMS_ON) {
		intel_sdvo_set_active_outputs(intel_output, 0);
		if (0)
			intel_sdvo_set_encoder_power_state(intel_output, mode);

		if (mode == DRM_MODE_DPMS_OFF) {
			temp = I915_READ(sdvo_priv->output_device);
			if ((temp & SDVO_ENABLE) != 0) {
				intel_sdvo_write_sdvox(intel_output, temp & ~SDVO_ENABLE);
			}
		}
	} else {
		bool input1, input2;
		int i;
		u8 status;

		temp = I915_READ(sdvo_priv->output_device);
		if ((temp & SDVO_ENABLE) == 0)
			intel_sdvo_write_sdvox(intel_output, temp | SDVO_ENABLE);
		for (i = 0; i < 2; i++)
		  intel_wait_for_vblank(dev);

		status = intel_sdvo_get_trained_inputs(intel_output, &input1,
						       &input2);


		/* Warn if the device reported failure to sync.
		 * A lot of SDVO devices fail to notify of sync, but it's
		 * a given it the status is a success, we succeeded.
		 */
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1) {
			DRM_DEBUG("First %s output reported failure to sync\n",
				   SDVO_NAME(sdvo_priv));
		}

		if (0)
			intel_sdvo_set_encoder_power_state(intel_output, mode);
		intel_sdvo_set_active_outputs(intel_output, sdvo_priv->controlled_output);
	}
	return;
}

static void intel_sdvo_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	int o;

	sdvo_priv->save_sdvo_mult = intel_sdvo_get_clock_rate_mult(intel_output);
	intel_sdvo_get_active_outputs(intel_output, &sdvo_priv->save_active_outputs);

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		intel_sdvo_set_target_input(intel_output, true, false);
		intel_sdvo_get_input_timing(intel_output,
					    &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		intel_sdvo_set_target_input(intel_output, false, true);
		intel_sdvo_get_input_timing(intel_output,
					    &sdvo_priv->save_input_dtd_2);
	}

	for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++)
	{
	        u16  this_output = (1 << o);
		if (sdvo_priv->caps.output_flags & this_output)
		{
			intel_sdvo_set_target_output(intel_output, this_output);
			intel_sdvo_get_output_timing(intel_output,
						     &sdvo_priv->save_output_dtd[o]);
		}
	}
	if (sdvo_priv->is_tv) {
		/* XXX: Save TV format/enhancements. */
	}

	sdvo_priv->save_SDVOX = I915_READ(sdvo_priv->output_device);
}

static void intel_sdvo_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	int o;
	int i;
	bool input1, input2;
	u8 status;

	intel_sdvo_set_active_outputs(intel_output, 0);

	for (o = SDVO_OUTPUT_FIRST; o <= SDVO_OUTPUT_LAST; o++)
	{
		u16  this_output = (1 << o);
		if (sdvo_priv->caps.output_flags & this_output) {
			intel_sdvo_set_target_output(intel_output, this_output);
			intel_sdvo_set_output_timing(intel_output, &sdvo_priv->save_output_dtd[o]);
		}
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		intel_sdvo_set_target_input(intel_output, true, false);
		intel_sdvo_set_input_timing(intel_output, &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		intel_sdvo_set_target_input(intel_output, false, true);
		intel_sdvo_set_input_timing(intel_output, &sdvo_priv->save_input_dtd_2);
	}

	intel_sdvo_set_clock_rate_mult(intel_output, sdvo_priv->save_sdvo_mult);

	if (sdvo_priv->is_tv) {
		/* XXX: Restore TV format/enhancements. */
	}

	intel_sdvo_write_sdvox(intel_output, sdvo_priv->save_SDVOX);

	if (sdvo_priv->save_SDVOX & SDVO_ENABLE)
	{
		for (i = 0; i < 2; i++)
			intel_wait_for_vblank(dev);
		status = intel_sdvo_get_trained_inputs(intel_output, &input1, &input2);
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1)
			DRM_DEBUG("First %s output reported failure to sync\n",
				   SDVO_NAME(sdvo_priv));
	}

	intel_sdvo_set_active_outputs(intel_output, sdvo_priv->save_active_outputs);
}

static int intel_sdvo_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (sdvo_priv->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (sdvo_priv->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool intel_sdvo_get_capabilities(struct intel_output *intel_output, struct intel_sdvo_caps *caps)
{
	u8 status;

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_DEVICE_CAPS, NULL, 0);
	status = intel_sdvo_read_response(intel_output, caps, sizeof(*caps));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

struct drm_connector* intel_sdvo_find(struct drm_device *dev, int sdvoB)
{
	struct drm_connector *connector = NULL;
	struct intel_output *iout = NULL;
	struct intel_sdvo_priv *sdvo;

	/* find the sdvo connector */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		iout = to_intel_output(connector);

		if (iout->type != INTEL_OUTPUT_SDVO)
			continue;

		sdvo = iout->dev_priv;

		if (sdvo->output_device == SDVOB && sdvoB)
			return connector;

		if (sdvo->output_device == SDVOC && !sdvoB)
			return connector;

	}

	return NULL;
}

int intel_sdvo_supports_hotplug(struct drm_connector *connector)
{
	u8 response[2];
	u8 status;
	struct intel_output *intel_output;
	DRM_DEBUG("\n");

	if (!connector)
		return 0;

	intel_output = to_intel_output(connector);

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_HOT_PLUG_SUPPORT, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &response, 2);

	if (response[0] !=0)
		return 1;

	return 0;
}

void intel_sdvo_set_hotplug(struct drm_connector *connector, int on)
{
	u8 response[2];
	u8 status;
	struct intel_output *intel_output = to_intel_output(connector);

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);
	intel_sdvo_read_response(intel_output, &response, 2);

	if (on) {
		intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_HOT_PLUG_SUPPORT, NULL, 0);
		status = intel_sdvo_read_response(intel_output, &response, 2);

		intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_ACTIVE_HOT_PLUG, &response, 2);
	} else {
		response[0] = 0;
		response[1] = 0;
		intel_sdvo_write_cmd(intel_output, SDVO_CMD_SET_ACTIVE_HOT_PLUG, &response, 2);
	}

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_ACTIVE_HOT_PLUG, NULL, 0);
	intel_sdvo_read_response(intel_output, &response, 2);
}

static void
intel_sdvo_hdmi_sink_detect(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;
	struct edid *edid = NULL;

	intel_sdvo_set_control_bus_switch(intel_output, sdvo_priv->ddc_bus);
	edid = drm_get_edid(&intel_output->base,
			    &intel_output->ddc_bus->adapter);
	if (edid != NULL) {
		sdvo_priv->is_hdmi = drm_detect_hdmi_monitor(edid);
		kfree(edid);
		intel_output->base.display_info.raw_edid = NULL;
	}
}

static enum drm_connector_status intel_sdvo_detect(struct drm_connector *connector)
{
	u8 response[2];
	u8 status;
	struct intel_output *intel_output = to_intel_output(connector);

	intel_sdvo_write_cmd(intel_output, SDVO_CMD_GET_ATTACHED_DISPLAYS, NULL, 0);
	status = intel_sdvo_read_response(intel_output, &response, 2);

	DRM_DEBUG("SDVO response %d %d\n", response[0], response[1]);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return connector_status_unknown;

	if ((response[0] != 0) || (response[1] != 0)) {
		intel_sdvo_hdmi_sink_detect(connector);
		return connector_status_connected;
	} else
		return connector_status_disconnected;
}

static void intel_sdvo_get_ddc_modes(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = intel_output->dev_priv;

	/* set the bus switch and get the modes */
	intel_sdvo_set_control_bus_switch(intel_output, sdvo_priv->ddc_bus);
	intel_ddc_get_modes(intel_output);

#if 0
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	/* Mac mini hack.  On this device, I get DDC through the analog, which
	 * load-detects as disconnected.  I fail to DDC through the SDVO DDC,
	 * but it does load-detect as connected.  So, just steal the DDC bits
	 * from analog when we fail at finding it the right way.
	 */
	crt = xf86_config->output[0];
	intel_output = crt->driver_private;
	if (intel_output->type == I830_OUTPUT_ANALOG &&
	    crt->funcs->detect(crt) == XF86OutputStatusDisconnected) {
		I830I2CInit(pScrn, &intel_output->pDDCBus, GPIOA, "CRTDDC_A");
		edid_mon = xf86OutputGetEDID(crt, intel_output->pDDCBus);
		xf86DestroyI2CBusRec(intel_output->pDDCBus, true, true);
	}
	if (edid_mon) {
		xf86OutputSetEDID(output, edid_mon);
		modes = xf86OutputGetEDIDModes(output);
	}
#endif
}

/**
 * This function checks the current TV format, and chooses a default if
 * it hasn't been set.
 */
static void
intel_sdvo_check_tv_format(struct intel_output *output)
{
	struct intel_sdvo_priv *dev_priv = output->dev_priv;
	struct intel_sdvo_tv_format format;
	uint8_t status;

	intel_sdvo_write_cmd(output, SDVO_CMD_GET_TV_FORMAT, NULL, 0);
	status = intel_sdvo_read_response(output, &format, sizeof(format));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return;

	memcpy(&dev_priv->tv_format, &format, sizeof(format));
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
	struct intel_output *output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;
	struct intel_sdvo_sdtv_resolution_request tv_res;
	uint32_t reply = 0;
	uint8_t status;
	int i = 0;

	intel_sdvo_check_tv_format(output);

	/* Read the list of supported input resolutions for the selected TV
	 * format.
	 */
	memset(&tv_res, 0, sizeof(tv_res));
	memcpy(&tv_res, &sdvo_priv->tv_format, sizeof(tv_res));
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

static int intel_sdvo_get_modes(struct drm_connector *connector)
{
	struct intel_output *output = to_intel_output(connector);
	struct intel_sdvo_priv *sdvo_priv = output->dev_priv;

	if (sdvo_priv->is_tv)
		intel_sdvo_get_tv_modes(connector);
	else
		intel_sdvo_get_ddc_modes(connector);

	if (list_empty(&connector->probed_modes))
		return 0;
	return 1;
}

static void intel_sdvo_destroy(struct drm_connector *connector)
{
	struct intel_output *intel_output = to_intel_output(connector);

	if (intel_output->i2c_bus)
		intel_i2c_destroy(intel_output->i2c_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(intel_output);
}

static const struct drm_encoder_helper_funcs intel_sdvo_helper_funcs = {
	.dpms = intel_sdvo_dpms,
	.mode_fixup = intel_sdvo_mode_fixup,
	.prepare = intel_encoder_prepare,
	.mode_set = intel_sdvo_mode_set,
	.commit = intel_encoder_commit,
};

static const struct drm_connector_funcs intel_sdvo_connector_funcs = {
	.save = intel_sdvo_save,
	.restore = intel_sdvo_restore,
	.detect = intel_sdvo_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
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
intel_sdvo_get_digital_encoding_mode(struct intel_output *output)
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

bool intel_sdvo_init(struct drm_device *dev, int output_device)
{
	struct drm_connector *connector;
	struct intel_output *intel_output;
	struct intel_sdvo_priv *sdvo_priv;
	struct intel_i2c_chan *i2cbus = NULL;
	int connector_type;
	u8 ch[0x40];
	int i;
	int encoder_type, output_id;

	intel_output = kcalloc(sizeof(struct intel_output)+sizeof(struct intel_sdvo_priv), 1, GFP_KERNEL);
	if (!intel_output) {
		return false;
	}

	connector = &intel_output->base;

	drm_connector_init(dev, connector, &intel_sdvo_connector_funcs,
			   DRM_MODE_CONNECTOR_Unknown);
	drm_connector_helper_add(connector, &intel_sdvo_connector_helper_funcs);
	sdvo_priv = (struct intel_sdvo_priv *)(intel_output + 1);
	intel_output->type = INTEL_OUTPUT_SDVO;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	/* setup the DDC bus. */
	if (output_device == SDVOB)
		i2cbus = intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOB");
	else
		i2cbus = intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOC");

	if (!i2cbus)
		goto err_connector;

	sdvo_priv->i2c_bus = i2cbus;

	if (output_device == SDVOB) {
		output_id = 1;
		sdvo_priv->i2c_bus->slave_addr = 0x38;
	} else {
		output_id = 2;
		sdvo_priv->i2c_bus->slave_addr = 0x39;
	}

	sdvo_priv->output_device = output_device;
	intel_output->i2c_bus = i2cbus;
	intel_output->dev_priv = sdvo_priv;


	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		if (!intel_sdvo_read_byte(intel_output, i, &ch[i])) {
			DRM_DEBUG("No SDVO device found on SDVO%c\n",
				  output_device == SDVOB ? 'B' : 'C');
			goto err_i2c;
		}
	}

	intel_sdvo_get_capabilities(intel_output, &sdvo_priv->caps);

	if (sdvo_priv->caps.output_flags &
	    (SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1)) {
		if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_TMDS0)
			sdvo_priv->controlled_output = SDVO_OUTPUT_TMDS0;
		else
			sdvo_priv->controlled_output = SDVO_OUTPUT_TMDS1;

		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_TMDS;
		connector_type = DRM_MODE_CONNECTOR_DVID;

		if (intel_sdvo_get_supp_encode(intel_output,
					       &sdvo_priv->encode) &&
		    intel_sdvo_get_digital_encoding_mode(intel_output) &&
		    sdvo_priv->is_hdmi) {
			/* enable hdmi encoding mode if supported */
			intel_sdvo_set_encode(intel_output, SDVO_ENCODE_HDMI);
			intel_sdvo_set_colorimetry(intel_output,
						   SDVO_COLORIMETRY_RGB256);
			connector_type = DRM_MODE_CONNECTOR_HDMIA;
		}
	}
	else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_SVID0)
	{
		sdvo_priv->controlled_output = SDVO_OUTPUT_SVID0;
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_TVDAC;
		connector_type = DRM_MODE_CONNECTOR_SVIDEO;
		sdvo_priv->is_tv = true;
		intel_output->needs_tv_clock = true;
	}
	else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_RGB0)
	{
		sdvo_priv->controlled_output = SDVO_OUTPUT_RGB0;
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_DAC;
		connector_type = DRM_MODE_CONNECTOR_VGA;
	}
	else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_RGB1)
	{
		sdvo_priv->controlled_output = SDVO_OUTPUT_RGB1;
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_DAC;
		connector_type = DRM_MODE_CONNECTOR_VGA;
	}
	else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_LVDS0)
	{
		sdvo_priv->controlled_output = SDVO_OUTPUT_LVDS0;
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_LVDS;
		connector_type = DRM_MODE_CONNECTOR_LVDS;
	}
	else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_LVDS1)
	{
		sdvo_priv->controlled_output = SDVO_OUTPUT_LVDS1;
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_LVDS;
		connector_type = DRM_MODE_CONNECTOR_LVDS;
	}
	else
	{
		unsigned char bytes[2];

		sdvo_priv->controlled_output = 0;
		memcpy (bytes, &sdvo_priv->caps.output_flags, 2);
		DRM_DEBUG("%s: Unknown SDVO output type (0x%02x%02x)\n",
			  SDVO_NAME(sdvo_priv),
			  bytes[0], bytes[1]);
		encoder_type = DRM_MODE_ENCODER_NONE;
		connector_type = DRM_MODE_CONNECTOR_Unknown;
		goto err_i2c;
	}

	drm_encoder_init(dev, &intel_output->enc, &intel_sdvo_enc_funcs, encoder_type);
	drm_encoder_helper_add(&intel_output->enc, &intel_sdvo_helper_funcs);
	connector->connector_type = connector_type;

	drm_mode_connector_attach_encoder(&intel_output->base, &intel_output->enc);
	drm_sysfs_connector_add(connector);

	intel_sdvo_select_ddc_bus(sdvo_priv);

	/* Set the input timing to the screen. Assume always input 0. */
	intel_sdvo_set_target_input(intel_output, true, false);

	intel_sdvo_get_input_pixel_clock_range(intel_output,
					       &sdvo_priv->pixel_clock_min,
					       &sdvo_priv->pixel_clock_max);


	DRM_DEBUG("%s device VID/DID: %02X:%02X.%02X, "
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

	intel_output->ddc_bus = i2cbus;

	return true;

err_i2c:
	intel_i2c_destroy(intel_output->i2c_bus);
err_connector:
	drm_connector_cleanup(connector);
	kfree(intel_output);

	return false;
}
