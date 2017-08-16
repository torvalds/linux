/*
 * Copyright © 2006-2007 Intel Corporation
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

/*
 * SDVO command definitions and structures.
 */

#define SDVO_OUTPUT_FIRST   (0)
#define SDVO_OUTPUT_TMDS0   (1 << 0)
#define SDVO_OUTPUT_RGB0    (1 << 1)
#define SDVO_OUTPUT_CVBS0   (1 << 2)
#define SDVO_OUTPUT_SVID0   (1 << 3)
#define SDVO_OUTPUT_YPRPB0  (1 << 4)
#define SDVO_OUTPUT_SCART0  (1 << 5)
#define SDVO_OUTPUT_LVDS0   (1 << 6)
#define SDVO_OUTPUT_TMDS1   (1 << 8)
#define SDVO_OUTPUT_RGB1    (1 << 9)
#define SDVO_OUTPUT_CVBS1   (1 << 10)
#define SDVO_OUTPUT_SVID1   (1 << 11)
#define SDVO_OUTPUT_YPRPB1  (1 << 12)
#define SDVO_OUTPUT_SCART1  (1 << 13)
#define SDVO_OUTPUT_LVDS1   (1 << 14)
#define SDVO_OUTPUT_LAST    (14)

struct intel_sdvo_caps {
	u8 vendor_id;
	u8 device_id;
	u8 device_rev_id;
	u8 sdvo_version_major;
	u8 sdvo_version_minor;
	unsigned int sdvo_inputs_mask:2;
	unsigned int smooth_scaling:1;
	unsigned int sharp_scaling:1;
	unsigned int up_scaling:1;
	unsigned int down_scaling:1;
	unsigned int stall_support:1;
	unsigned int pad:1;
	u16 output_flags;
} __packed;

/* Note: SDVO detailed timing flags match EDID misc flags. */
#define DTD_FLAG_HSYNC_POSITIVE (1 << 1)
#define DTD_FLAG_VSYNC_POSITIVE (1 << 2)
#define DTD_FLAG_INTERLACE	(1 << 7)

/* This matches the EDID DTD structure, more or less */
struct intel_sdvo_dtd {
	struct {
		u16 clock;	/* pixel clock, in 10kHz units */
		u8 h_active;	/* lower 8 bits (pixels) */
		u8 h_blank;	/* lower 8 bits (pixels) */
		u8 h_high;	/* upper 4 bits each h_active, h_blank */
		u8 v_active;	/* lower 8 bits (lines) */
		u8 v_blank;	/* lower 8 bits (lines) */
		u8 v_high;	/* upper 4 bits each v_active, v_blank */
	} part1;

	struct {
		u8 h_sync_off;	/* lower 8 bits, from hblank start */
		u8 h_sync_width;	/* lower 8 bits (pixels) */
		/* lower 4 bits each vsync offset, vsync width */
		u8 v_sync_off_width;
		/*
		* 2 high bits of hsync offset, 2 high bits of hsync width,
		* bits 4-5 of vsync offset, and 2 high bits of vsync width.
		*/
		u8 sync_off_width_high;
		u8 dtd_flags;
		u8 sdvo_flags;
		/* bits 6-7 of vsync offset at bits 6-7 */
		u8 v_sync_off_high;
		u8 reserved;
	} part2;
} __packed;

struct intel_sdvo_pixel_clock_range {
	u16 min;	/* pixel clock, in 10kHz units */
	u16 max;	/* pixel clock, in 10kHz units */
} __packed;

struct intel_sdvo_preferred_input_timing_args {
	u16 clock;
	u16 width;
	u16 height;
	u8	interlace:1;
	u8	scaled:1;
	u8	pad:6;
} __packed;

/* I2C registers for SDVO */
#define SDVO_I2C_ARG_0				0x07
#define SDVO_I2C_ARG_1				0x06
#define SDVO_I2C_ARG_2				0x05
#define SDVO_I2C_ARG_3				0x04
#define SDVO_I2C_ARG_4				0x03
#define SDVO_I2C_ARG_5				0x02
#define SDVO_I2C_ARG_6				0x01
#define SDVO_I2C_ARG_7				0x00
#define SDVO_I2C_OPCODE				0x08
#define SDVO_I2C_CMD_STATUS			0x09
#define SDVO_I2C_RETURN_0			0x0a
#define SDVO_I2C_RETURN_1			0x0b
#define SDVO_I2C_RETURN_2			0x0c
#define SDVO_I2C_RETURN_3			0x0d
#define SDVO_I2C_RETURN_4			0x0e
#define SDVO_I2C_RETURN_5			0x0f
#define SDVO_I2C_RETURN_6			0x10
#define SDVO_I2C_RETURN_7			0x11
#define SDVO_I2C_VENDOR_BEGIN			0x20

/* Status results */
#define SDVO_CMD_STATUS_POWER_ON		0x0
#define SDVO_CMD_STATUS_SUCCESS			0x1
#define SDVO_CMD_STATUS_NOTSUPP			0x2
#define SDVO_CMD_STATUS_INVALID_ARG		0x3
#define SDVO_CMD_STATUS_PENDING			0x4
#define SDVO_CMD_STATUS_TARGET_NOT_SPECIFIED	0x5
#define SDVO_CMD_STATUS_SCALING_NOT_SUPP	0x6

/* SDVO commands, argument/result registers */

#define SDVO_CMD_RESET					0x01

/* Returns a struct intel_sdvo_caps */
#define SDVO_CMD_GET_DEVICE_CAPS			0x02

#define SDVO_CMD_GET_FIRMWARE_REV			0x86
# define SDVO_DEVICE_FIRMWARE_MINOR			SDVO_I2C_RETURN_0
# define SDVO_DEVICE_FIRMWARE_MAJOR			SDVO_I2C_RETURN_1
# define SDVO_DEVICE_FIRMWARE_PATCH			SDVO_I2C_RETURN_2

/*
 * Reports which inputs are trained (managed to sync).
 *
 * Devices must have trained within 2 vsyncs of a mode change.
 */
#define SDVO_CMD_GET_TRAINED_INPUTS			0x03
struct intel_sdvo_get_trained_inputs_response {
	unsigned int input0_trained:1;
	unsigned int input1_trained:1;
	unsigned int pad:6;
} __packed;

/* Returns a struct intel_sdvo_output_flags of active outputs. */
#define SDVO_CMD_GET_ACTIVE_OUTPUTS			0x04

/*
 * Sets the current set of active outputs.
 *
 * Takes a struct intel_sdvo_output_flags.  Must be preceded by a SET_IN_OUT_MAP
 * on multi-output devices.
 */
#define SDVO_CMD_SET_ACTIVE_OUTPUTS			0x05

/*
 * Returns the current mapping of SDVO inputs to outputs on the device.
 *
 * Returns two struct intel_sdvo_output_flags structures.
 */
#define SDVO_CMD_GET_IN_OUT_MAP				0x06
struct intel_sdvo_in_out_map {
	u16 in0, in1;
};

/*
 * Sets the current mapping of SDVO inputs to outputs on the device.
 *
 * Takes two struct i380_sdvo_output_flags structures.
 */
#define SDVO_CMD_SET_IN_OUT_MAP				0x07

/*
 * Returns a struct intel_sdvo_output_flags of attached displays.
 */
#define SDVO_CMD_GET_ATTACHED_DISPLAYS			0x0b

/*
 * Returns a struct intel_sdvo_ouptut_flags of displays supporting hot plugging.
 */
#define SDVO_CMD_GET_HOT_PLUG_SUPPORT			0x0c

/*
 * Takes a struct intel_sdvo_output_flags.
 */
#define SDVO_CMD_SET_ACTIVE_HOT_PLUG			0x0d

/*
 * Returns a struct intel_sdvo_output_flags of displays with hot plug
 * interrupts enabled.
 */
#define SDVO_CMD_GET_ACTIVE_HOT_PLUG			0x0e

#define SDVO_CMD_GET_INTERRUPT_EVENT_SOURCE		0x0f
struct intel_sdvo_get_interrupt_event_source_response {
	u16 interrupt_status;
	unsigned int ambient_light_interrupt:1;
	unsigned int hdmi_audio_encrypt_change:1;
	unsigned int pad:6;
} __packed;

/*
 * Selects which input is affected by future input commands.
 *
 * Commands affected include SET_INPUT_TIMINGS_PART[12],
 * GET_INPUT_TIMINGS_PART[12], GET_PREFERRED_INPUT_TIMINGS_PART[12],
 * GET_INPUT_PIXEL_CLOCK_RANGE, and CREATE_PREFERRED_INPUT_TIMINGS.
 */
#define SDVO_CMD_SET_TARGET_INPUT			0x10
struct intel_sdvo_set_target_input_args {
	unsigned int target_1:1;
	unsigned int pad:7;
} __packed;

/*
 * Takes a struct intel_sdvo_output_flags of which outputs are targeted by
 * future output commands.
 *
 * Affected commands inclue SET_OUTPUT_TIMINGS_PART[12],
 * GET_OUTPUT_TIMINGS_PART[12], and GET_OUTPUT_PIXEL_CLOCK_RANGE.
 */
#define SDVO_CMD_SET_TARGET_OUTPUT			0x11

#define SDVO_CMD_GET_INPUT_TIMINGS_PART1		0x12
#define SDVO_CMD_GET_INPUT_TIMINGS_PART2		0x13
#define SDVO_CMD_SET_INPUT_TIMINGS_PART1		0x14
#define SDVO_CMD_SET_INPUT_TIMINGS_PART2		0x15
#define SDVO_CMD_SET_OUTPUT_TIMINGS_PART1		0x16
#define SDVO_CMD_SET_OUTPUT_TIMINGS_PART2		0x17
#define SDVO_CMD_GET_OUTPUT_TIMINGS_PART1		0x18
#define SDVO_CMD_GET_OUTPUT_TIMINGS_PART2		0x19
/* Part 1 */
# define SDVO_DTD_CLOCK_LOW				SDVO_I2C_ARG_0
# define SDVO_DTD_CLOCK_HIGH				SDVO_I2C_ARG_1
# define SDVO_DTD_H_ACTIVE				SDVO_I2C_ARG_2
# define SDVO_DTD_H_BLANK				SDVO_I2C_ARG_3
# define SDVO_DTD_H_HIGH				SDVO_I2C_ARG_4
# define SDVO_DTD_V_ACTIVE				SDVO_I2C_ARG_5
# define SDVO_DTD_V_BLANK				SDVO_I2C_ARG_6
# define SDVO_DTD_V_HIGH				SDVO_I2C_ARG_7
/* Part 2 */
# define SDVO_DTD_HSYNC_OFF				SDVO_I2C_ARG_0
# define SDVO_DTD_HSYNC_WIDTH				SDVO_I2C_ARG_1
# define SDVO_DTD_VSYNC_OFF_WIDTH			SDVO_I2C_ARG_2
# define SDVO_DTD_SYNC_OFF_WIDTH_HIGH			SDVO_I2C_ARG_3
# define SDVO_DTD_DTD_FLAGS				SDVO_I2C_ARG_4
# define SDVO_DTD_DTD_FLAG_INTERLACED				(1 << 7)
# define SDVO_DTD_DTD_FLAG_STEREO_MASK				(3 << 5)
# define SDVO_DTD_DTD_FLAG_INPUT_MASK				(3 << 3)
# define SDVO_DTD_DTD_FLAG_SYNC_MASK				(3 << 1)
# define SDVO_DTD_SDVO_FLAS				SDVO_I2C_ARG_5
# define SDVO_DTD_SDVO_FLAG_STALL				(1 << 7)
# define SDVO_DTD_SDVO_FLAG_CENTERED				(0 << 6)
# define SDVO_DTD_SDVO_FLAG_UPPER_LEFT				(1 << 6)
# define SDVO_DTD_SDVO_FLAG_SCALING_MASK			(3 << 4)
# define SDVO_DTD_SDVO_FLAG_SCALING_NONE			(0 << 4)
# define SDVO_DTD_SDVO_FLAG_SCALING_SHARP			(1 << 4)
# define SDVO_DTD_SDVO_FLAG_SCALING_SMOOTH			(2 << 4)
# define SDVO_DTD_VSYNC_OFF_HIGH			SDVO_I2C_ARG_6

/*
 * Generates a DTD based on the given width, height, and flags.
 *
 * This will be supported by any device supporting scaling or interlaced
 * modes.
 */
#define SDVO_CMD_CREATE_PREFERRED_INPUT_TIMING		0x1a
# define SDVO_PREFERRED_INPUT_TIMING_CLOCK_LOW		SDVO_I2C_ARG_0
# define SDVO_PREFERRED_INPUT_TIMING_CLOCK_HIGH		SDVO_I2C_ARG_1
# define SDVO_PREFERRED_INPUT_TIMING_WIDTH_LOW		SDVO_I2C_ARG_2
# define SDVO_PREFERRED_INPUT_TIMING_WIDTH_HIGH		SDVO_I2C_ARG_3
# define SDVO_PREFERRED_INPUT_TIMING_HEIGHT_LOW		SDVO_I2C_ARG_4
# define SDVO_PREFERRED_INPUT_TIMING_HEIGHT_HIGH	SDVO_I2C_ARG_5
# define SDVO_PREFERRED_INPUT_TIMING_FLAGS		SDVO_I2C_ARG_6
# define SDVO_PREFERRED_INPUT_TIMING_FLAGS_INTERLACED		(1 << 0)
# define SDVO_PREFERRED_INPUT_TIMING_FLAGS_SCALED		(1 << 1)

#define SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART1	0x1b
#define SDVO_CMD_GET_PREFERRED_INPUT_TIMING_PART2	0x1c

/* Returns a struct intel_sdvo_pixel_clock_range */
#define SDVO_CMD_GET_INPUT_PIXEL_CLOCK_RANGE		0x1d
/* Returns a struct intel_sdvo_pixel_clock_range */
#define SDVO_CMD_GET_OUTPUT_PIXEL_CLOCK_RANGE		0x1e

/* Returns a byte bitfield containing SDVO_CLOCK_RATE_MULT_* flags */
#define SDVO_CMD_GET_SUPPORTED_CLOCK_RATE_MULTS		0x1f

/* Returns a byte containing a SDVO_CLOCK_RATE_MULT_* flag */
#define SDVO_CMD_GET_CLOCK_RATE_MULT			0x20
/* Takes a byte containing a SDVO_CLOCK_RATE_MULT_* flag */
#define SDVO_CMD_SET_CLOCK_RATE_MULT			0x21
# define SDVO_CLOCK_RATE_MULT_1X				(1 << 0)
# define SDVO_CLOCK_RATE_MULT_2X				(1 << 1)
# define SDVO_CLOCK_RATE_MULT_4X				(1 << 3)

#define SDVO_CMD_GET_SUPPORTED_TV_FORMATS		0x27
/* 6 bytes of bit flags for TV formats shared by all TV format functions */
struct intel_sdvo_tv_format {
	unsigned int ntsc_m:1;
	unsigned int ntsc_j:1;
	unsigned int ntsc_443:1;
	unsigned int pal_b:1;
	unsigned int pal_d:1;
	unsigned int pal_g:1;
	unsigned int pal_h:1;
	unsigned int pal_i:1;

	unsigned int pal_m:1;
	unsigned int pal_n:1;
	unsigned int pal_nc:1;
	unsigned int pal_60:1;
	unsigned int secam_b:1;
	unsigned int secam_d:1;
	unsigned int secam_g:1;
	unsigned int secam_k:1;

	unsigned int secam_k1:1;
	unsigned int secam_l:1;
	unsigned int secam_60:1;
	unsigned int hdtv_std_smpte_240m_1080i_59:1;
	unsigned int hdtv_std_smpte_240m_1080i_60:1;
	unsigned int hdtv_std_smpte_260m_1080i_59:1;
	unsigned int hdtv_std_smpte_260m_1080i_60:1;
	unsigned int hdtv_std_smpte_274m_1080i_50:1;

	unsigned int hdtv_std_smpte_274m_1080i_59:1;
	unsigned int hdtv_std_smpte_274m_1080i_60:1;
	unsigned int hdtv_std_smpte_274m_1080p_23:1;
	unsigned int hdtv_std_smpte_274m_1080p_24:1;
	unsigned int hdtv_std_smpte_274m_1080p_25:1;
	unsigned int hdtv_std_smpte_274m_1080p_29:1;
	unsigned int hdtv_std_smpte_274m_1080p_30:1;
	unsigned int hdtv_std_smpte_274m_1080p_50:1;

	unsigned int hdtv_std_smpte_274m_1080p_59:1;
	unsigned int hdtv_std_smpte_274m_1080p_60:1;
	unsigned int hdtv_std_smpte_295m_1080i_50:1;
	unsigned int hdtv_std_smpte_295m_1080p_50:1;
	unsigned int hdtv_std_smpte_296m_720p_59:1;
	unsigned int hdtv_std_smpte_296m_720p_60:1;
	unsigned int hdtv_std_smpte_296m_720p_50:1;
	unsigned int hdtv_std_smpte_293m_480p_59:1;

	unsigned int hdtv_std_smpte_170m_480i_59:1;
	unsigned int hdtv_std_iturbt601_576i_50:1;
	unsigned int hdtv_std_iturbt601_576p_50:1;
	unsigned int hdtv_std_eia_7702a_480i_60:1;
	unsigned int hdtv_std_eia_7702a_480p_60:1;
	unsigned int pad:3;
} __packed;

#define SDVO_CMD_GET_TV_FORMAT				0x28

#define SDVO_CMD_SET_TV_FORMAT				0x29

/* Returns the resolutiosn that can be used with the given TV format */
#define SDVO_CMD_GET_SDTV_RESOLUTION_SUPPORT		0x83
struct intel_sdvo_sdtv_resolution_request {
	unsigned int ntsc_m:1;
	unsigned int ntsc_j:1;
	unsigned int ntsc_443:1;
	unsigned int pal_b:1;
	unsigned int pal_d:1;
	unsigned int pal_g:1;
	unsigned int pal_h:1;
	unsigned int pal_i:1;

	unsigned int pal_m:1;
	unsigned int pal_n:1;
	unsigned int pal_nc:1;
	unsigned int pal_60:1;
	unsigned int secam_b:1;
	unsigned int secam_d:1;
	unsigned int secam_g:1;
	unsigned int secam_k:1;

	unsigned int secam_k1:1;
	unsigned int secam_l:1;
	unsigned int secam_60:1;
	unsigned int pad:5;
} __packed;

struct intel_sdvo_sdtv_resolution_reply {
	unsigned int res_320x200:1;
	unsigned int res_320x240:1;
	unsigned int res_400x300:1;
	unsigned int res_640x350:1;
	unsigned int res_640x400:1;
	unsigned int res_640x480:1;
	unsigned int res_704x480:1;
	unsigned int res_704x576:1;

	unsigned int res_720x350:1;
	unsigned int res_720x400:1;
	unsigned int res_720x480:1;
	unsigned int res_720x540:1;
	unsigned int res_720x576:1;
	unsigned int res_768x576:1;
	unsigned int res_800x600:1;
	unsigned int res_832x624:1;

	unsigned int res_920x766:1;
	unsigned int res_1024x768:1;
	unsigned int res_1280x1024:1;
	unsigned int pad:5;
} __packed;

/* Get supported resolution with squire pixel aspect ratio that can be
   scaled for the requested HDTV format */
#define SDVO_CMD_GET_SCALED_HDTV_RESOLUTION_SUPPORT		0x85

struct intel_sdvo_hdtv_resolution_request {
	unsigned int hdtv_std_smpte_240m_1080i_59:1;
	unsigned int hdtv_std_smpte_240m_1080i_60:1;
	unsigned int hdtv_std_smpte_260m_1080i_59:1;
	unsigned int hdtv_std_smpte_260m_1080i_60:1;
	unsigned int hdtv_std_smpte_274m_1080i_50:1;
	unsigned int hdtv_std_smpte_274m_1080i_59:1;
	unsigned int hdtv_std_smpte_274m_1080i_60:1;
	unsigned int hdtv_std_smpte_274m_1080p_23:1;

	unsigned int hdtv_std_smpte_274m_1080p_24:1;
	unsigned int hdtv_std_smpte_274m_1080p_25:1;
	unsigned int hdtv_std_smpte_274m_1080p_29:1;
	unsigned int hdtv_std_smpte_274m_1080p_30:1;
	unsigned int hdtv_std_smpte_274m_1080p_50:1;
	unsigned int hdtv_std_smpte_274m_1080p_59:1;
	unsigned int hdtv_std_smpte_274m_1080p_60:1;
	unsigned int hdtv_std_smpte_295m_1080i_50:1;

	unsigned int hdtv_std_smpte_295m_1080p_50:1;
	unsigned int hdtv_std_smpte_296m_720p_59:1;
	unsigned int hdtv_std_smpte_296m_720p_60:1;
	unsigned int hdtv_std_smpte_296m_720p_50:1;
	unsigned int hdtv_std_smpte_293m_480p_59:1;
	unsigned int hdtv_std_smpte_170m_480i_59:1;
	unsigned int hdtv_std_iturbt601_576i_50:1;
	unsigned int hdtv_std_iturbt601_576p_50:1;

	unsigned int hdtv_std_eia_7702a_480i_60:1;
	unsigned int hdtv_std_eia_7702a_480p_60:1;
	unsigned int pad:6;
} __packed;

struct intel_sdvo_hdtv_resolution_reply {
	unsigned int res_640x480:1;
	unsigned int res_800x600:1;
	unsigned int res_1024x768:1;
	unsigned int res_1280x960:1;
	unsigned int res_1400x1050:1;
	unsigned int res_1600x1200:1;
	unsigned int res_1920x1440:1;
	unsigned int res_2048x1536:1;

	unsigned int res_2560x1920:1;
	unsigned int res_3200x2400:1;
	unsigned int res_3840x2880:1;
	unsigned int pad1:5;

	unsigned int res_848x480:1;
	unsigned int res_1064x600:1;
	unsigned int res_1280x720:1;
	unsigned int res_1360x768:1;
	unsigned int res_1704x960:1;
	unsigned int res_1864x1050:1;
	unsigned int res_1920x1080:1;
	unsigned int res_2128x1200:1;

	unsigned int res_2560x1400:1;
	unsigned int res_2728x1536:1;
	unsigned int res_3408x1920:1;
	unsigned int res_4264x2400:1;
	unsigned int res_5120x2880:1;
	unsigned int pad2:3;

	unsigned int res_768x480:1;
	unsigned int res_960x600:1;
	unsigned int res_1152x720:1;
	unsigned int res_1124x768:1;
	unsigned int res_1536x960:1;
	unsigned int res_1680x1050:1;
	unsigned int res_1728x1080:1;
	unsigned int res_1920x1200:1;

	unsigned int res_2304x1440:1;
	unsigned int res_2456x1536:1;
	unsigned int res_3072x1920:1;
	unsigned int res_3840x2400:1;
	unsigned int res_4608x2880:1;
	unsigned int pad3:3;

	unsigned int res_1280x1024:1;
	unsigned int pad4:7;

	unsigned int res_1280x768:1;
	unsigned int pad5:7;
} __packed;

/* Get supported power state returns info for encoder and monitor, rely on
   last SetTargetInput and SetTargetOutput calls */
#define SDVO_CMD_GET_SUPPORTED_POWER_STATES		0x2a
/* Get power state returns info for encoder and monitor, rely on last
   SetTargetInput and SetTargetOutput calls */
#define SDVO_CMD_GET_POWER_STATE			0x2b
#define SDVO_CMD_GET_ENCODER_POWER_STATE		0x2b
#define SDVO_CMD_SET_ENCODER_POWER_STATE		0x2c
# define SDVO_ENCODER_STATE_ON					(1 << 0)
# define SDVO_ENCODER_STATE_STANDBY				(1 << 1)
# define SDVO_ENCODER_STATE_SUSPEND				(1 << 2)
# define SDVO_ENCODER_STATE_OFF					(1 << 3)
# define SDVO_MONITOR_STATE_ON					(1 << 4)
# define SDVO_MONITOR_STATE_STANDBY				(1 << 5)
# define SDVO_MONITOR_STATE_SUSPEND				(1 << 6)
# define SDVO_MONITOR_STATE_OFF					(1 << 7)

#define SDVO_CMD_GET_MAX_PANEL_POWER_SEQUENCING		0x2d
#define SDVO_CMD_GET_PANEL_POWER_SEQUENCING		0x2e
#define SDVO_CMD_SET_PANEL_POWER_SEQUENCING		0x2f
/*
 * The panel power sequencing parameters are in units of milliseconds.
 * The high fields are bits 8:9 of the 10-bit values.
 */
struct sdvo_panel_power_sequencing {
	u8 t0;
	u8 t1;
	u8 t2;
	u8 t3;
	u8 t4;

	unsigned int t0_high:2;
	unsigned int t1_high:2;
	unsigned int t2_high:2;
	unsigned int t3_high:2;

	unsigned int t4_high:2;
	unsigned int pad:6;
} __packed;

#define SDVO_CMD_GET_MAX_BACKLIGHT_LEVEL		0x30
struct sdvo_max_backlight_reply {
	u8 max_value;
	u8 default_value;
} __packed;

#define SDVO_CMD_GET_BACKLIGHT_LEVEL			0x31
#define SDVO_CMD_SET_BACKLIGHT_LEVEL			0x32

#define SDVO_CMD_GET_AMBIENT_LIGHT			0x33
struct sdvo_get_ambient_light_reply {
	u16 trip_low;
	u16 trip_high;
	u16 value;
} __packed;
#define SDVO_CMD_SET_AMBIENT_LIGHT			0x34
struct sdvo_set_ambient_light_reply {
	u16 trip_low;
	u16 trip_high;
	unsigned int enable:1;
	unsigned int pad:7;
} __packed;

/* Set display power state */
#define SDVO_CMD_SET_DISPLAY_POWER_STATE		0x7d
# define SDVO_DISPLAY_STATE_ON				(1 << 0)
# define SDVO_DISPLAY_STATE_STANDBY			(1 << 1)
# define SDVO_DISPLAY_STATE_SUSPEND			(1 << 2)
# define SDVO_DISPLAY_STATE_OFF				(1 << 3)

#define SDVO_CMD_GET_SUPPORTED_ENHANCEMENTS		0x84
struct intel_sdvo_enhancements_reply {
	unsigned int flicker_filter:1;
	unsigned int flicker_filter_adaptive:1;
	unsigned int flicker_filter_2d:1;
	unsigned int saturation:1;
	unsigned int hue:1;
	unsigned int brightness:1;
	unsigned int contrast:1;
	unsigned int overscan_h:1;

	unsigned int overscan_v:1;
	unsigned int hpos:1;
	unsigned int vpos:1;
	unsigned int sharpness:1;
	unsigned int dot_crawl:1;
	unsigned int dither:1;
	unsigned int tv_chroma_filter:1;
	unsigned int tv_luma_filter:1;
} __packed;

/* Picture enhancement limits below are dependent on the current TV format,
 * and thus need to be queried and set after it.
 */
#define SDVO_CMD_GET_MAX_FLICKER_FILTER			0x4d
#define SDVO_CMD_GET_MAX_FLICKER_FILTER_ADAPTIVE	0x7b
#define SDVO_CMD_GET_MAX_FLICKER_FILTER_2D		0x52
#define SDVO_CMD_GET_MAX_SATURATION			0x55
#define SDVO_CMD_GET_MAX_HUE				0x58
#define SDVO_CMD_GET_MAX_BRIGHTNESS			0x5b
#define SDVO_CMD_GET_MAX_CONTRAST			0x5e
#define SDVO_CMD_GET_MAX_OVERSCAN_H			0x61
#define SDVO_CMD_GET_MAX_OVERSCAN_V			0x64
#define SDVO_CMD_GET_MAX_HPOS				0x67
#define SDVO_CMD_GET_MAX_VPOS				0x6a
#define SDVO_CMD_GET_MAX_SHARPNESS			0x6d
#define SDVO_CMD_GET_MAX_TV_CHROMA_FILTER		0x74
#define SDVO_CMD_GET_MAX_TV_LUMA_FILTER			0x77
struct intel_sdvo_enhancement_limits_reply {
	u16 max_value;
	u16 default_value;
} __packed;

#define SDVO_CMD_GET_LVDS_PANEL_INFORMATION		0x7f
#define SDVO_CMD_SET_LVDS_PANEL_INFORMATION		0x80
# define SDVO_LVDS_COLOR_DEPTH_18			(0 << 0)
# define SDVO_LVDS_COLOR_DEPTH_24			(1 << 0)
# define SDVO_LVDS_CONNECTOR_SPWG			(0 << 2)
# define SDVO_LVDS_CONNECTOR_OPENLDI			(1 << 2)
# define SDVO_LVDS_SINGLE_CHANNEL			(0 << 4)
# define SDVO_LVDS_DUAL_CHANNEL				(1 << 4)

#define SDVO_CMD_GET_FLICKER_FILTER			0x4e
#define SDVO_CMD_SET_FLICKER_FILTER			0x4f
#define SDVO_CMD_GET_FLICKER_FILTER_ADAPTIVE		0x50
#define SDVO_CMD_SET_FLICKER_FILTER_ADAPTIVE		0x51
#define SDVO_CMD_GET_FLICKER_FILTER_2D			0x53
#define SDVO_CMD_SET_FLICKER_FILTER_2D			0x54
#define SDVO_CMD_GET_SATURATION				0x56
#define SDVO_CMD_SET_SATURATION				0x57
#define SDVO_CMD_GET_HUE				0x59
#define SDVO_CMD_SET_HUE				0x5a
#define SDVO_CMD_GET_BRIGHTNESS				0x5c
#define SDVO_CMD_SET_BRIGHTNESS				0x5d
#define SDVO_CMD_GET_CONTRAST				0x5f
#define SDVO_CMD_SET_CONTRAST				0x60
#define SDVO_CMD_GET_OVERSCAN_H				0x62
#define SDVO_CMD_SET_OVERSCAN_H				0x63
#define SDVO_CMD_GET_OVERSCAN_V				0x65
#define SDVO_CMD_SET_OVERSCAN_V				0x66
#define SDVO_CMD_GET_HPOS				0x68
#define SDVO_CMD_SET_HPOS				0x69
#define SDVO_CMD_GET_VPOS				0x6b
#define SDVO_CMD_SET_VPOS				0x6c
#define SDVO_CMD_GET_SHARPNESS				0x6e
#define SDVO_CMD_SET_SHARPNESS				0x6f
#define SDVO_CMD_GET_TV_CHROMA_FILTER			0x75
#define SDVO_CMD_SET_TV_CHROMA_FILTER			0x76
#define SDVO_CMD_GET_TV_LUMA_FILTER			0x78
#define SDVO_CMD_SET_TV_LUMA_FILTER			0x79
struct intel_sdvo_enhancements_arg {
	u16 value;
} __packed;

#define SDVO_CMD_GET_DOT_CRAWL				0x70
#define SDVO_CMD_SET_DOT_CRAWL				0x71
# define SDVO_DOT_CRAWL_ON					(1 << 0)
# define SDVO_DOT_CRAWL_DEFAULT_ON				(1 << 1)

#define SDVO_CMD_GET_DITHER				0x72
#define SDVO_CMD_SET_DITHER				0x73
# define SDVO_DITHER_ON						(1 << 0)
# define SDVO_DITHER_DEFAULT_ON					(1 << 1)

#define SDVO_CMD_SET_CONTROL_BUS_SWITCH			0x7a
# define SDVO_CONTROL_BUS_PROM				(1 << 0)
# define SDVO_CONTROL_BUS_DDC1				(1 << 1)
# define SDVO_CONTROL_BUS_DDC2				(1 << 2)
# define SDVO_CONTROL_BUS_DDC3				(1 << 3)

/* HDMI op codes */
#define SDVO_CMD_GET_SUPP_ENCODE	0x9d
#define SDVO_CMD_GET_ENCODE		0x9e
#define SDVO_CMD_SET_ENCODE		0x9f
  #define SDVO_ENCODE_DVI	0x0
  #define SDVO_ENCODE_HDMI	0x1
#define SDVO_CMD_SET_PIXEL_REPLI	0x8b
#define SDVO_CMD_GET_PIXEL_REPLI	0x8c
#define SDVO_CMD_GET_COLORIMETRY_CAP	0x8d
#define SDVO_CMD_SET_COLORIMETRY	0x8e
  #define SDVO_COLORIMETRY_RGB256   0x0
  #define SDVO_COLORIMETRY_RGB220   0x1
  #define SDVO_COLORIMETRY_YCrCb422 0x3
  #define SDVO_COLORIMETRY_YCrCb444 0x4
#define SDVO_CMD_GET_COLORIMETRY	0x8f
#define SDVO_CMD_GET_AUDIO_ENCRYPT_PREFER 0x90
#define SDVO_CMD_SET_AUDIO_STAT		0x91
#define SDVO_CMD_GET_AUDIO_STAT		0x92
#define SDVO_CMD_SET_HBUF_INDEX		0x93
  #define SDVO_HBUF_INDEX_ELD		0
  #define SDVO_HBUF_INDEX_AVI_IF	1
#define SDVO_CMD_GET_HBUF_INDEX		0x94
#define SDVO_CMD_GET_HBUF_INFO		0x95
#define SDVO_CMD_SET_HBUF_AV_SPLIT	0x96
#define SDVO_CMD_GET_HBUF_AV_SPLIT	0x97
#define SDVO_CMD_SET_HBUF_DATA		0x98
#define SDVO_CMD_GET_HBUF_DATA		0x99
#define SDVO_CMD_SET_HBUF_TXRATE	0x9a
#define SDVO_CMD_GET_HBUF_TXRATE	0x9b
  #define SDVO_HBUF_TX_DISABLED	(0 << 6)
  #define SDVO_HBUF_TX_ONCE	(2 << 6)
  #define SDVO_HBUF_TX_VSYNC	(3 << 6)
#define SDVO_CMD_GET_AUDIO_TX_INFO	0x9c
#define SDVO_NEED_TO_STALL  (1 << 7)

struct intel_sdvo_encode {
	u8 dvi_rev;
	u8 hdmi_rev;
} __packed;
