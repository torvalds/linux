/*
 * Copyright (c) 2006-2007 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

#include <linux/i2c.h>
#include <linux/delay.h>
/* #include <drm/drm_crtc.h> */
#include <drm/drmP.h>
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_sdvo_regs.h"

struct psb_intel_sdvo_priv {
	struct psb_intel_i2c_chan *i2c_bus;
	int slaveaddr;
	int output_device;

	u16 active_outputs;

	struct psb_intel_sdvo_caps caps;
	int pixel_clock_min, pixel_clock_max;

	int save_sdvo_mult;
	u16 save_active_outputs;
	struct psb_intel_sdvo_dtd save_input_dtd_1, save_input_dtd_2;
	struct psb_intel_sdvo_dtd save_output_dtd[16];
	u32 save_SDVOX;
	u8 in_out_map[4];

	u8 by_input_wiring;
	u32 active_device;
};

/**
 * Writes the SDVOB or SDVOC with the given value, but always writes both
 * SDVOB and SDVOC to work around apparent hardware issues (according to
 * comments in the BIOS).
 */
void psb_intel_sdvo_write_sdvox(struct psb_intel_output *psb_intel_output,
				u32 val)
{
	struct drm_device *dev = psb_intel_output->base.dev;
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	u32 bval = val, cval = val;
	int i;

	if (sdvo_priv->output_device == SDVOB)
		cval = REG_READ(SDVOC);
	else
		bval = REG_READ(SDVOB);
	/*
	 * Write the registers twice for luck. Sometimes,
	 * writing them only once doesn't appear to 'stick'.
	 * The BIOS does this too. Yay, magic
	 */
	for (i = 0; i < 2; i++) {
		REG_WRITE(SDVOB, bval);
		REG_READ(SDVOB);
		REG_WRITE(SDVOC, cval);
		REG_READ(SDVOC);
	}
}

static bool psb_intel_sdvo_read_byte(
				struct psb_intel_output *psb_intel_output,
				u8 addr, u8 *ch)
{
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
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

	ret = i2c_transfer(&sdvo_priv->i2c_bus->adapter, msgs, 2);
	if (ret == 2) {
		/* DRM_DEBUG("got back from addr %02X = %02x\n",
		 * out_buf[0], buf[0]);
		 */
		*ch = buf[0];
		return true;
	}

	DRM_DEBUG("i2c transfer returned %d\n", ret);
	return false;
}

static bool psb_intel_sdvo_write_byte(
			struct psb_intel_output *psb_intel_output,
			int addr, u8 ch)
{
	u8 out_buf[2];
	struct i2c_msg msgs[] = {
		{
		 .addr = psb_intel_output->i2c_bus->slave_addr,
		 .flags = 0,
		 .len = 2,
		 .buf = out_buf,
		 }
	};

	out_buf[0] = addr;
	out_buf[1] = ch;

	if (i2c_transfer(&psb_intel_output->i2c_bus->adapter, msgs, 1) == 1)
		return true;
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
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_INTERRUPT_EVENT_SOURCE),
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
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_GET_SUPPORTED_CLOCK_RATE_MULTS),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_CLOCK_RATE_MULT),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CLOCK_RATE_MULT),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_SUPPORTED_TV_FORMATS),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_GET_TV_FORMAT),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_TV_FORMAT),
	    SDVO_CMD_NAME_ENTRY
	    (SDVO_CMD_SET_TV_RESOLUTION_SUPPORT),
	    SDVO_CMD_NAME_ENTRY(SDVO_CMD_SET_CONTROL_BUS_SWITCH),};

#define SDVO_NAME(dev_priv) \
		 ((dev_priv)->output_device == SDVOB ? "SDVOB" : "SDVOC")
#define SDVO_PRIV(output)   ((struct psb_intel_sdvo_priv *) (output)->dev_priv)

static void psb_intel_sdvo_write_cmd(struct psb_intel_output *psb_intel_output,
				     u8 cmd,
				     void *args,
				     int args_len)
{
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	int i;

	if (1) {
		DRM_DEBUG("%s: W: %02X ", SDVO_NAME(sdvo_priv), cmd);
		for (i = 0; i < args_len; i++)
			printk(KERN_INFO"%02X ", ((u8 *) args)[i]);
		for (; i < 8; i++)
			printk("   ");
		for (i = 0;
		     i <
		     sizeof(sdvo_cmd_names) / sizeof(sdvo_cmd_names[0]);
		     i++) {
			if (cmd == sdvo_cmd_names[i].cmd) {
				printk("(%s)", sdvo_cmd_names[i].name);
				break;
			}
		}
		if (i ==
		    sizeof(sdvo_cmd_names) / sizeof(sdvo_cmd_names[0]))
			printk("(%02X)", cmd);
		printk("\n");
	}

	for (i = 0; i < args_len; i++) {
		psb_intel_sdvo_write_byte(psb_intel_output,
					SDVO_I2C_ARG_0 - i,
					((u8 *) args)[i]);
	}

	psb_intel_sdvo_write_byte(psb_intel_output, SDVO_I2C_OPCODE, cmd);
}

static const char *const cmd_status_names[] = {
	"Power on",
	"Success",
	"Not supported",
	"Invalid arg",
	"Pending",
	"Target not specified",
	"Scaling not supported"
};

static u8 psb_intel_sdvo_read_response(
				struct psb_intel_output *psb_intel_output,
				void *response, int response_len)
{
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	int i;
	u8 status;
	u8 retry = 50;

	while (retry--) {
		/* Read the command response */
		for (i = 0; i < response_len; i++) {
			psb_intel_sdvo_read_byte(psb_intel_output,
					     SDVO_I2C_RETURN_0 + i,
					     &((u8 *) response)[i]);
		}

		/* read the return status */
		psb_intel_sdvo_read_byte(psb_intel_output,
					 SDVO_I2C_CMD_STATUS,
					 &status);

		if (1) {
			DRM_DEBUG("%s: R: ", SDVO_NAME(sdvo_priv));
			for (i = 0; i < response_len; i++)
				printk(KERN_INFO"%02X ", ((u8 *) response)[i]);
			for (; i < 8; i++)
				printk("   ");
			if (status <= SDVO_CMD_STATUS_SCALING_NOT_SUPP)
				printk(KERN_INFO"(%s)",
					 cmd_status_names[status]);
			else
				printk(KERN_INFO"(??? %d)", status);
			printk("\n");
		}

		if (status != SDVO_CMD_STATUS_PENDING)
			return status;

		mdelay(50);
	}

	return status;
}

int psb_intel_sdvo_get_pixel_multiplier(struct drm_display_mode *mode)
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
void psb_intel_sdvo_set_control_bus_switch(
				struct psb_intel_output *psb_intel_output,
				u8 target)
{
	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_SET_CONTROL_BUS_SWITCH,
				 &target,
				 1);
}

static bool psb_intel_sdvo_set_target_input(
				struct psb_intel_output *psb_intel_output,
				bool target_0, bool target_1)
{
	struct psb_intel_sdvo_set_target_input_args targets = { 0 };
	u8 status;

	if (target_0 && target_1)
		return SDVO_CMD_STATUS_NOTSUPP;

	if (target_1)
		targets.target_1 = 1;

	psb_intel_sdvo_write_cmd(psb_intel_output, SDVO_CMD_SET_TARGET_INPUT,
			     &targets, sizeof(targets));

	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);

	return status == SDVO_CMD_STATUS_SUCCESS;
}

/**
 * Return whether each input is trained.
 *
 * This function is making an assumption about the layout of the response,
 * which should be checked against the docs.
 */
static bool psb_intel_sdvo_get_trained_inputs(struct psb_intel_output
					  *psb_intel_output, bool *input_1,
					  bool *input_2)
{
	struct psb_intel_sdvo_get_trained_inputs_response response;
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, SDVO_CMD_GET_TRAINED_INPUTS,
			     NULL, 0);
	status =
	    psb_intel_sdvo_read_response(psb_intel_output, &response,
				     sizeof(response));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	*input_1 = response.input0_trained;
	*input_2 = response.input1_trained;
	return true;
}

static bool psb_intel_sdvo_get_active_outputs(struct psb_intel_output
					  *psb_intel_output, u16 *outputs)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, SDVO_CMD_GET_ACTIVE_OUTPUTS,
			     NULL, 0);
	status =
	    psb_intel_sdvo_read_response(psb_intel_output, outputs,
				     sizeof(*outputs));

	return status == SDVO_CMD_STATUS_SUCCESS;
}

static bool psb_intel_sdvo_set_active_outputs(struct psb_intel_output
					  *psb_intel_output, u16 outputs)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, SDVO_CMD_SET_ACTIVE_OUTPUTS,
			     &outputs, sizeof(outputs));
	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
	return status == SDVO_CMD_STATUS_SUCCESS;
}

static bool psb_intel_sdvo_set_encoder_power_state(struct psb_intel_output
					       *psb_intel_output, int mode)
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

	psb_intel_sdvo_write_cmd(psb_intel_output,
			     SDVO_CMD_SET_ENCODER_POWER_STATE, &state,
			     sizeof(state));
	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);

	return status == SDVO_CMD_STATUS_SUCCESS;
}

static bool psb_intel_sdvo_get_input_pixel_clock_range(struct psb_intel_output
						   *psb_intel_output,
						   int *clock_min,
						   int *clock_max)
{
	struct psb_intel_sdvo_pixel_clock_range clocks;
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output,
			     SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE, NULL,
			     0);

	status =
	    psb_intel_sdvo_read_response(psb_intel_output, &clocks,
				     sizeof(clocks));

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	/* Convert the values from units of 10 kHz to kHz. */
	*clock_min = clocks.min * 10;
	*clock_max = clocks.max * 10;

	return true;
}

static bool psb_intel_sdvo_set_target_output(
				struct psb_intel_output *psb_intel_output,
				u16 outputs)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, SDVO_CMD_SET_TARGET_OUTPUT,
			     &outputs, sizeof(outputs));

	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
	return status == SDVO_CMD_STATUS_SUCCESS;
}

static bool psb_intel_sdvo_get_timing(struct psb_intel_output *psb_intel_output,
				  u8 cmd, struct psb_intel_sdvo_dtd *dtd)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, cmd, NULL, 0);
	status = psb_intel_sdvo_read_response(psb_intel_output, &dtd->part1,
					  sizeof(dtd->part1));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	psb_intel_sdvo_write_cmd(psb_intel_output, cmd + 1, NULL, 0);
	status = psb_intel_sdvo_read_response(psb_intel_output, &dtd->part2,
					  sizeof(dtd->part2));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool psb_intel_sdvo_get_input_timing(
				struct psb_intel_output *psb_intel_output,
				struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_get_timing(psb_intel_output,
				     SDVO_CMD_GET_INPUT_TIMINGS_PART1,
				     dtd);
}

static bool psb_intel_sdvo_set_timing(
				struct psb_intel_output *psb_intel_output,
				u8 cmd,
				struct psb_intel_sdvo_dtd *dtd)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output, cmd, &dtd->part1,
			     sizeof(dtd->part1));
	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	psb_intel_sdvo_write_cmd(psb_intel_output, cmd + 1, &dtd->part2,
			     sizeof(dtd->part2));
	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool psb_intel_sdvo_set_input_timing(
				struct psb_intel_output *psb_intel_output,
				struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_set_timing(psb_intel_output,
				     SDVO_CMD_SET_INPUT_TIMINGS_PART1,
				     dtd);
}

static bool psb_intel_sdvo_set_output_timing(
				struct psb_intel_output *psb_intel_output,
				struct psb_intel_sdvo_dtd *dtd)
{
	return psb_intel_sdvo_set_timing(psb_intel_output,
				     SDVO_CMD_SET_OUTPUT_TIMINGS_PART1,
				     dtd);
}

static int psb_intel_sdvo_get_clock_rate_mult(struct psb_intel_output
						*psb_intel_output)
{
	u8 response, status;

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_CLOCK_RATE_MULT,
				 NULL,
				 0);

	status = psb_intel_sdvo_read_response(psb_intel_output, &response, 1);

	if (status != SDVO_CMD_STATUS_SUCCESS) {
		DRM_DEBUG("Couldn't get SDVO clock rate multiplier\n");
		return SDVO_CLOCK_RATE_MULT_1X;
	} else {
		DRM_DEBUG("Current clock rate multiplier: %d\n", response);
	}

	return response;
}

static bool psb_intel_sdvo_set_clock_rate_mult(struct psb_intel_output
						*psb_intel_output, u8 val)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output,
				SDVO_CMD_SET_CLOCK_RATE_MULT,
				&val,
				1);

	status = psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

static bool psb_sdvo_set_current_inoutmap(struct psb_intel_output *output,
					  u32 in0outputmask,
					  u32 in1outputmask)
{
	u8 byArgs[4];
	u8 status;
	int i;
	struct psb_intel_sdvo_priv *sdvo_priv = output->dev_priv;

	/* Make all fields of the  args/ret to zero */
	memset(byArgs, 0, sizeof(byArgs));

	/* Fill up the argument values; */
	byArgs[0] = (u8) (in0outputmask & 0xFF);
	byArgs[1] = (u8) ((in0outputmask >> 8) & 0xFF);
	byArgs[2] = (u8) (in1outputmask & 0xFF);
	byArgs[3] = (u8) ((in1outputmask >> 8) & 0xFF);


	/*save inoutmap arg here*/
	for (i = 0; i < 4; i++)
		sdvo_priv->in_out_map[i] = byArgs[0];

	psb_intel_sdvo_write_cmd(output, SDVO_CMD_SET_IN_OUT_MAP, byArgs, 4);
	status = psb_intel_sdvo_read_response(output, NULL, 0);

	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;
	return true;
}


static void psb_intel_sdvo_set_iomap(struct psb_intel_output *output)
{
	u32 dwCurrentSDVOIn0 = 0;
	u32 dwCurrentSDVOIn1 = 0;
	u32 dwDevMask = 0;


	struct psb_intel_sdvo_priv *sdvo_priv = output->dev_priv;

	/* Please DO NOT change the following code. */
	/* SDVOB_IN0 or SDVOB_IN1 ==> sdvo_in0 */
	/* SDVOC_IN0 or SDVOC_IN1 ==> sdvo_in1 */
	if (sdvo_priv->by_input_wiring & (SDVOB_IN0 | SDVOC_IN0)) {
		switch (sdvo_priv->active_device) {
		case SDVO_DEVICE_LVDS:
			dwDevMask = SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1;
			break;
		case SDVO_DEVICE_TMDS:
			dwDevMask = SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1;
			break;
		case SDVO_DEVICE_TV:
			dwDevMask =
			SDVO_OUTPUT_YPRPB0 | SDVO_OUTPUT_SVID0 |
			SDVO_OUTPUT_CVBS0 | SDVO_OUTPUT_YPRPB1 |
			SDVO_OUTPUT_SVID1 | SDVO_OUTPUT_CVBS1 |
			SDVO_OUTPUT_SCART0 | SDVO_OUTPUT_SCART1;
			break;
		case SDVO_DEVICE_CRT:
			dwDevMask = SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1;
			break;
		}
		dwCurrentSDVOIn0 = (sdvo_priv->active_outputs & dwDevMask);
	} else if (sdvo_priv->by_input_wiring & (SDVOB_IN1 | SDVOC_IN1)) {
		switch (sdvo_priv->active_device) {
		case SDVO_DEVICE_LVDS:
			dwDevMask = SDVO_OUTPUT_LVDS0 | SDVO_OUTPUT_LVDS1;
			break;
		case SDVO_DEVICE_TMDS:
			dwDevMask = SDVO_OUTPUT_TMDS0 | SDVO_OUTPUT_TMDS1;
			break;
		case SDVO_DEVICE_TV:
			dwDevMask =
			SDVO_OUTPUT_YPRPB0 | SDVO_OUTPUT_SVID0 |
			SDVO_OUTPUT_CVBS0 | SDVO_OUTPUT_YPRPB1 |
			SDVO_OUTPUT_SVID1 | SDVO_OUTPUT_CVBS1 |
			SDVO_OUTPUT_SCART0 | SDVO_OUTPUT_SCART1;
			break;
		case SDVO_DEVICE_CRT:
			dwDevMask = SDVO_OUTPUT_RGB0 | SDVO_OUTPUT_RGB1;
			break;
		}
		dwCurrentSDVOIn1 = (sdvo_priv->active_outputs & dwDevMask);
	}

	psb_sdvo_set_current_inoutmap(output, dwCurrentSDVOIn0,
					  dwCurrentSDVOIn1);
}


static bool psb_intel_sdvo_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	/* Make the CRTC code factor in the SDVO pixel multiplier.  The SDVO
	 * device will be told of the multiplier during mode_set.
	 */
	adjusted_mode->clock *= psb_intel_sdvo_get_pixel_multiplier(mode);
	return true;
}

static void psb_intel_sdvo_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_intel_output *psb_intel_output =
					enc_to_psb_intel_output(encoder);
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	u16 width, height;
	u16 h_blank_len, h_sync_len, v_blank_len, v_sync_len;
	u16 h_sync_offset, v_sync_offset;
	u32 sdvox;
	struct psb_intel_sdvo_dtd output_dtd;
	int sdvo_pixel_multiply;

	if (!mode)
		return;

	psb_intel_sdvo_set_target_output(psb_intel_output, 0);

	width = mode->crtc_hdisplay;
	height = mode->crtc_vdisplay;

	/* do some mode translations */
	h_blank_len = mode->crtc_hblank_end - mode->crtc_hblank_start;
	h_sync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;

	v_blank_len = mode->crtc_vblank_end - mode->crtc_vblank_start;
	v_sync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;

	h_sync_offset = mode->crtc_hsync_start - mode->crtc_hblank_start;
	v_sync_offset = mode->crtc_vsync_start - mode->crtc_vblank_start;

	output_dtd.part1.clock = mode->clock / 10;
	output_dtd.part1.h_active = width & 0xff;
	output_dtd.part1.h_blank = h_blank_len & 0xff;
	output_dtd.part1.h_high = (((width >> 8) & 0xf) << 4) |
	    ((h_blank_len >> 8) & 0xf);
	output_dtd.part1.v_active = height & 0xff;
	output_dtd.part1.v_blank = v_blank_len & 0xff;
	output_dtd.part1.v_high = (((height >> 8) & 0xf) << 4) |
	    ((v_blank_len >> 8) & 0xf);

	output_dtd.part2.h_sync_off = h_sync_offset;
	output_dtd.part2.h_sync_width = h_sync_len & 0xff;
	output_dtd.part2.v_sync_off_width = (v_sync_offset & 0xf) << 4 |
	    (v_sync_len & 0xf);
	output_dtd.part2.sync_off_width_high =
	    ((h_sync_offset & 0x300) >> 2) | ((h_sync_len & 0x300) >> 4) |
	    ((v_sync_offset & 0x30) >> 2) | ((v_sync_len & 0x30) >> 4);

	output_dtd.part2.dtd_flags = 0x18;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		output_dtd.part2.dtd_flags |= 0x2;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		output_dtd.part2.dtd_flags |= 0x4;

	output_dtd.part2.sdvo_flags = 0;
	output_dtd.part2.v_sync_off_high = v_sync_offset & 0xc0;
	output_dtd.part2.reserved = 0;

	/* Set the output timing to the screen */
	psb_intel_sdvo_set_target_output(psb_intel_output,
				     sdvo_priv->active_outputs);

	/* Set the input timing to the screen. Assume always input 0. */
	psb_intel_sdvo_set_target_input(psb_intel_output, true, false);

	psb_intel_sdvo_set_output_timing(psb_intel_output, &output_dtd);

	/* We would like to use i830_sdvo_create_preferred_input_timing() to
	 * provide the device with a timing it can support, if it supports that
	 * feature.  However, presumably we would need to adjust the CRTC to
	 * output the preferred timing, and we don't support that currently.
	 */
	psb_intel_sdvo_set_input_timing(psb_intel_output, &output_dtd);

	switch (psb_intel_sdvo_get_pixel_multiplier(mode)) {
	case 1:
		psb_intel_sdvo_set_clock_rate_mult(psb_intel_output,
					       SDVO_CLOCK_RATE_MULT_1X);
		break;
	case 2:
		psb_intel_sdvo_set_clock_rate_mult(psb_intel_output,
					       SDVO_CLOCK_RATE_MULT_2X);
		break;
	case 4:
		psb_intel_sdvo_set_clock_rate_mult(psb_intel_output,
					       SDVO_CLOCK_RATE_MULT_4X);
		break;
	}

	/* Set the SDVO control regs. */
	sdvox = REG_READ(sdvo_priv->output_device);
	switch (sdvo_priv->output_device) {
	case SDVOB:
		sdvox &= SDVOB_PRESERVE_MASK;
		break;
	case SDVOC:
		sdvox &= SDVOC_PRESERVE_MASK;
		break;
	}
	sdvox |= (9 << 19) | SDVO_BORDER_ENABLE;
	if (psb_intel_crtc->pipe == 1)
		sdvox |= SDVO_PIPE_B_SELECT;

	sdvo_pixel_multiply = psb_intel_sdvo_get_pixel_multiplier(mode);

	psb_intel_sdvo_write_sdvox(psb_intel_output, sdvox);

	 psb_intel_sdvo_set_iomap(psb_intel_output);
}

static void psb_intel_sdvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct psb_intel_output *psb_intel_output =
					enc_to_psb_intel_output(encoder);
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	u32 temp;

	if (mode != DRM_MODE_DPMS_ON) {
		psb_intel_sdvo_set_active_outputs(psb_intel_output, 0);
		if (0)
			psb_intel_sdvo_set_encoder_power_state(
							psb_intel_output,
							mode);

		if (mode == DRM_MODE_DPMS_OFF) {
			temp = REG_READ(sdvo_priv->output_device);
			if ((temp & SDVO_ENABLE) != 0) {
				psb_intel_sdvo_write_sdvox(psb_intel_output,
						       temp &
						       ~SDVO_ENABLE);
			}
		}
	} else {
		bool input1, input2;
		int i;
		u8 status;

		temp = REG_READ(sdvo_priv->output_device);
		if ((temp & SDVO_ENABLE) == 0)
			psb_intel_sdvo_write_sdvox(psb_intel_output,
					       temp | SDVO_ENABLE);
		for (i = 0; i < 2; i++)
			psb_intel_wait_for_vblank(dev);

		status =
		    psb_intel_sdvo_get_trained_inputs(psb_intel_output,
							&input1,
							&input2);


		/* Warn if the device reported failure to sync.
		 * A lot of SDVO devices fail to notify of sync, but it's
		 * a given it the status is a success, we succeeded.
		 */
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1) {
			DRM_DEBUG
			    ("First %s output reported failure to sync\n",
			     SDVO_NAME(sdvo_priv));
		}

		if (0)
			psb_intel_sdvo_set_encoder_power_state(
							psb_intel_output,
							mode);
		psb_intel_sdvo_set_active_outputs(psb_intel_output,
					      sdvo_priv->active_outputs);
	}
	return;
}

static void psb_intel_sdvo_save(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct psb_intel_output *psb_intel_output =
					to_psb_intel_output(connector);
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	/*int o;*/

	sdvo_priv->save_sdvo_mult =
	    psb_intel_sdvo_get_clock_rate_mult(psb_intel_output);
	psb_intel_sdvo_get_active_outputs(psb_intel_output,
				      &sdvo_priv->save_active_outputs);

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		psb_intel_sdvo_set_target_input(psb_intel_output,
						true,
						false);
		psb_intel_sdvo_get_input_timing(psb_intel_output,
					    &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		psb_intel_sdvo_set_target_input(psb_intel_output,
						false,
						true);
		psb_intel_sdvo_get_input_timing(psb_intel_output,
					    &sdvo_priv->save_input_dtd_2);
	}
	sdvo_priv->save_SDVOX = REG_READ(sdvo_priv->output_device);

	/*TODO: save the in_out_map state*/
}

static void psb_intel_sdvo_restore(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct psb_intel_output *psb_intel_output =
					to_psb_intel_output(connector);
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;
	/*int o;*/
	int i;
	bool input1, input2;
	u8 status;

	psb_intel_sdvo_set_active_outputs(psb_intel_output, 0);

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x1) {
		psb_intel_sdvo_set_target_input(psb_intel_output, true, false);
		psb_intel_sdvo_set_input_timing(psb_intel_output,
					    &sdvo_priv->save_input_dtd_1);
	}

	if (sdvo_priv->caps.sdvo_inputs_mask & 0x2) {
		psb_intel_sdvo_set_target_input(psb_intel_output, false, true);
		psb_intel_sdvo_set_input_timing(psb_intel_output,
					    &sdvo_priv->save_input_dtd_2);
	}

	psb_intel_sdvo_set_clock_rate_mult(psb_intel_output,
				       sdvo_priv->save_sdvo_mult);

	REG_WRITE(sdvo_priv->output_device, sdvo_priv->save_SDVOX);

	if (sdvo_priv->save_SDVOX & SDVO_ENABLE) {
		for (i = 0; i < 2; i++)
			psb_intel_wait_for_vblank(dev);
		status =
		    psb_intel_sdvo_get_trained_inputs(psb_intel_output,
							&input1,
							&input2);
		if (status == SDVO_CMD_STATUS_SUCCESS && !input1)
			DRM_DEBUG
			    ("First %s output reported failure to sync\n",
			     SDVO_NAME(sdvo_priv));
	}

	psb_intel_sdvo_set_active_outputs(psb_intel_output,
				      sdvo_priv->save_active_outputs);

	/*TODO: restore in_out_map*/
	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_SET_IN_OUT_MAP,
				 sdvo_priv->in_out_map,
				 4);

	psb_intel_sdvo_read_response(psb_intel_output, NULL, 0);
}

static int psb_intel_sdvo_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct psb_intel_output *psb_intel_output =
				to_psb_intel_output(connector);
	struct psb_intel_sdvo_priv *sdvo_priv = psb_intel_output->dev_priv;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	if (sdvo_priv->pixel_clock_min > mode->clock)
		return MODE_CLOCK_LOW;

	if (sdvo_priv->pixel_clock_max < mode->clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool psb_intel_sdvo_get_capabilities(
				struct psb_intel_output *psb_intel_output,
				struct psb_intel_sdvo_caps *caps)
{
	u8 status;

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_DEVICE_CAPS,
				 NULL,
				 0);
	status = psb_intel_sdvo_read_response(psb_intel_output,
						caps,
						sizeof(*caps));
	if (status != SDVO_CMD_STATUS_SUCCESS)
		return false;

	return true;
}

struct drm_connector *psb_intel_sdvo_find(struct drm_device *dev, int sdvoB)
{
	struct drm_connector *connector = NULL;
	struct psb_intel_output *iout = NULL;
	struct psb_intel_sdvo_priv *sdvo;

	/* find the sdvo connector */
	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		iout = to_psb_intel_output(connector);

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

int psb_intel_sdvo_supports_hotplug(struct drm_connector *connector)
{
	u8 response[2];
	u8 status;
	struct psb_intel_output *psb_intel_output;
	DRM_DEBUG("\n");

	if (!connector)
		return 0;

	psb_intel_output = to_psb_intel_output(connector);

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_HOT_PLUG_SUPPORT,
				 NULL,
				 0);
	status = psb_intel_sdvo_read_response(psb_intel_output,
						&response,
						2);

	if (response[0] != 0)
		return 1;

	return 0;
}

void psb_intel_sdvo_set_hotplug(struct drm_connector *connector, int on)
{
	u8 response[2];
	u8 status;
	struct psb_intel_output *psb_intel_output =
					to_psb_intel_output(connector);

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_ACTIVE_HOT_PLUG,
				 NULL,
				 0);
	psb_intel_sdvo_read_response(psb_intel_output, &response, 2);

	if (on) {
		psb_intel_sdvo_write_cmd(psb_intel_output,
				     SDVO_CMD_GET_HOT_PLUG_SUPPORT, NULL,
				     0);
		status = psb_intel_sdvo_read_response(psb_intel_output,
						      &response,
						      2);

		psb_intel_sdvo_write_cmd(psb_intel_output,
				     SDVO_CMD_SET_ACTIVE_HOT_PLUG,
				     &response, 2);
	} else {
		response[0] = 0;
		response[1] = 0;
		psb_intel_sdvo_write_cmd(psb_intel_output,
				     SDVO_CMD_SET_ACTIVE_HOT_PLUG,
				     &response, 2);
	}

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_ACTIVE_HOT_PLUG,
				 NULL,
				 0);
	psb_intel_sdvo_read_response(psb_intel_output, &response, 2);
}

static enum drm_connector_status psb_intel_sdvo_detect(struct drm_connector
						   *connector, bool force)
{
	u8 response[2];
	u8 status;
	struct psb_intel_output *psb_intel_output =
					to_psb_intel_output(connector);

	psb_intel_sdvo_write_cmd(psb_intel_output,
				 SDVO_CMD_GET_ATTACHED_DISPLAYS,
				 NULL,
				 0);
	status = psb_intel_sdvo_read_response(psb_intel_output, &response, 2);

	DRM_DEBUG("SDVO response %d %d\n", response[0], response[1]);
	if ((response[0] != 0) || (response[1] != 0))
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static int psb_intel_sdvo_get_modes(struct drm_connector *connector)
{
	struct psb_intel_output *psb_intel_output =
					to_psb_intel_output(connector);

	/* set the bus switch and get the modes */
	psb_intel_sdvo_set_control_bus_switch(psb_intel_output,
					  SDVO_CONTROL_BUS_DDC2);
	psb_intel_ddc_get_modes(psb_intel_output);

	if (list_empty(&connector->probed_modes))
		return 0;
	return 1;
}

static void psb_intel_sdvo_destroy(struct drm_connector *connector)
{
	struct psb_intel_output *psb_intel_output =
				to_psb_intel_output(connector);

	if (psb_intel_output->i2c_bus)
		psb_intel_i2c_destroy(psb_intel_output->i2c_bus);
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(psb_intel_output);
}

static const struct drm_encoder_helper_funcs psb_intel_sdvo_helper_funcs = {
	.dpms = psb_intel_sdvo_dpms,
	.mode_fixup = psb_intel_sdvo_mode_fixup,
	.prepare = psb_intel_encoder_prepare,
	.mode_set = psb_intel_sdvo_mode_set,
	.commit = psb_intel_encoder_commit,
};

static const struct drm_connector_funcs psb_intel_sdvo_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = psb_intel_sdvo_save,
	.restore = psb_intel_sdvo_restore,
	.detect = psb_intel_sdvo_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = psb_intel_sdvo_destroy,
};

static const struct drm_connector_helper_funcs
				psb_intel_sdvo_connector_helper_funcs = {
	.get_modes = psb_intel_sdvo_get_modes,
	.mode_valid = psb_intel_sdvo_mode_valid,
	.best_encoder = psb_intel_best_encoder,
};

void psb_intel_sdvo_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs psb_intel_sdvo_enc_funcs = {
	.destroy = psb_intel_sdvo_enc_destroy,
};


void psb_intel_sdvo_init(struct drm_device *dev, int output_device)
{
	struct drm_connector *connector;
	struct psb_intel_output *psb_intel_output;
	struct psb_intel_sdvo_priv *sdvo_priv;
	struct psb_intel_i2c_chan *i2cbus = NULL;
	int connector_type;
	u8 ch[0x40];
	int i;
	int encoder_type, output_id;

	psb_intel_output =
	    kcalloc(sizeof(struct psb_intel_output) +
		    sizeof(struct psb_intel_sdvo_priv), 1, GFP_KERNEL);
	if (!psb_intel_output)
		return;

	connector = &psb_intel_output->base;

	drm_connector_init(dev, connector, &psb_intel_sdvo_connector_funcs,
			   DRM_MODE_CONNECTOR_Unknown);
	drm_connector_helper_add(connector,
				 &psb_intel_sdvo_connector_helper_funcs);
	sdvo_priv = (struct psb_intel_sdvo_priv *) (psb_intel_output + 1);
	psb_intel_output->type = INTEL_OUTPUT_SDVO;

	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	/* setup the DDC bus. */
	if (output_device == SDVOB)
		i2cbus =
		    psb_intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOB");
	else
		i2cbus =
		    psb_intel_i2c_create(dev, GPIOE, "SDVOCTRL_E for SDVOC");

	if (!i2cbus)
		goto err_connector;

	sdvo_priv->i2c_bus = i2cbus;

	if (output_device == SDVOB) {
		output_id = 1;
		sdvo_priv->by_input_wiring = SDVOB_IN0;
		sdvo_priv->i2c_bus->slave_addr = 0x38;
	} else {
		output_id = 2;
		sdvo_priv->i2c_bus->slave_addr = 0x39;
	}

	sdvo_priv->output_device = output_device;
	psb_intel_output->i2c_bus = i2cbus;
	psb_intel_output->dev_priv = sdvo_priv;


	/* Read the regs to test if we can talk to the device */
	for (i = 0; i < 0x40; i++) {
		if (!psb_intel_sdvo_read_byte(psb_intel_output, i, &ch[i])) {
			DRM_DEBUG("No SDVO device found on SDVO%c\n",
				  output_device == SDVOB ? 'B' : 'C');
			goto err_i2c;
		}
	}

	psb_intel_sdvo_get_capabilities(psb_intel_output, &sdvo_priv->caps);

	memset(&sdvo_priv->active_outputs, 0,
	       sizeof(sdvo_priv->active_outputs));

	/* TODO, CVBS, SVID, YPRPB & SCART outputs. */
	if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_RGB0) {
		sdvo_priv->active_outputs = SDVO_OUTPUT_RGB0;
		sdvo_priv->active_device = SDVO_DEVICE_CRT;
		connector->display_info.subpixel_order =
		    SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_DAC;
		connector_type = DRM_MODE_CONNECTOR_VGA;
	} else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_RGB1) {
		sdvo_priv->active_outputs = SDVO_OUTPUT_RGB1;
		sdvo_priv->active_outputs = SDVO_DEVICE_CRT;
		connector->display_info.subpixel_order =
		    SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_DAC;
		connector_type = DRM_MODE_CONNECTOR_VGA;
	} else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_TMDS0) {
		sdvo_priv->active_outputs = SDVO_OUTPUT_TMDS0;
		sdvo_priv->active_device = SDVO_DEVICE_TMDS;
		connector->display_info.subpixel_order =
		    SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_TMDS;
		connector_type = DRM_MODE_CONNECTOR_DVID;
	} else if (sdvo_priv->caps.output_flags & SDVO_OUTPUT_TMDS1) {
		sdvo_priv->active_outputs = SDVO_OUTPUT_TMDS1;
		sdvo_priv->active_device = SDVO_DEVICE_TMDS;
		connector->display_info.subpixel_order =
		    SubPixelHorizontalRGB;
		encoder_type = DRM_MODE_ENCODER_TMDS;
		connector_type = DRM_MODE_CONNECTOR_DVID;
	} else {
		unsigned char bytes[2];

		memcpy(bytes, &sdvo_priv->caps.output_flags, 2);
		DRM_DEBUG
		    ("%s: No active RGB or TMDS outputs (0x%02x%02x)\n",
		     SDVO_NAME(sdvo_priv), bytes[0], bytes[1]);
		goto err_i2c;
	}

	drm_encoder_init(dev, &psb_intel_output->enc, &psb_intel_sdvo_enc_funcs,
			 encoder_type);
	drm_encoder_helper_add(&psb_intel_output->enc,
			       &psb_intel_sdvo_helper_funcs);
	connector->connector_type = connector_type;

	drm_mode_connector_attach_encoder(&psb_intel_output->base,
					  &psb_intel_output->enc);
	drm_sysfs_connector_add(connector);

	/* Set the input timing to the screen. Assume always input 0. */
	psb_intel_sdvo_set_target_input(psb_intel_output, true, false);

	psb_intel_sdvo_get_input_pixel_clock_range(psb_intel_output,
					       &sdvo_priv->pixel_clock_min,
					       &sdvo_priv->
					       pixel_clock_max);


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

	psb_intel_output->ddc_bus = i2cbus;

	return;

err_i2c:
	psb_intel_i2c_destroy(psb_intel_output->i2c_bus);
err_connector:
	drm_connector_cleanup(connector);
	kfree(psb_intel_output);

	return;
}
