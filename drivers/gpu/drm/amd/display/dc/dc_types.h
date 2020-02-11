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
#ifndef DC_TYPES_H_
#define DC_TYPES_H_

/* AND EdidUtility only needs a portion
 * of this file, including the rest only
 * causes additional issues.
 */
#include "os_types.h"
#include "fixed31_32.h"
#include "irq_types.h"
#include "dc_dp_types.h"
#include "dc_hw_types.h"
#include "dal_types.h"
#include "grph_object_defs.h"

#ifdef CONFIG_DRM_AMD_DC_HDCP
#include "dm_cp_psp.h"
#endif

/* forward declarations */
struct dc_plane_state;
struct dc_stream_state;
struct dc_link;
struct dc_sink;
struct dal;
struct dc_dmub_srv;

/********************************
 * Environment definitions
 ********************************/
enum dce_environment {
	DCE_ENV_PRODUCTION_DRV = 0,
	/* Emulation on FPGA, in "Maximus" System.
	 * This environment enforces that *only* DC registers accessed.
	 * (access to non-DC registers will hang FPGA) */
	DCE_ENV_FPGA_MAXIMUS,
	/* Emulation on real HW or on FPGA. Used by Diagnostics, enforces
	 * requirements of Diagnostics team. */
	DCE_ENV_DIAG,
	/*
	 * Guest VM system, DC HW may exist but is not virtualized and
	 * should not be used.  SW support for VDI only.
	 */
	DCE_ENV_VIRTUAL_HW
};

/* Note: use these macro definitions instead of direct comparison! */
#define IS_FPGA_MAXIMUS_DC(dce_environment) \
	(dce_environment == DCE_ENV_FPGA_MAXIMUS)

#define IS_DIAG_DC(dce_environment) \
	(IS_FPGA_MAXIMUS_DC(dce_environment) || (dce_environment == DCE_ENV_DIAG))

struct hw_asic_id {
	uint32_t chip_id;
	uint32_t chip_family;
	uint32_t pci_revision_id;
	uint32_t hw_internal_rev;
	uint32_t vram_type;
	uint32_t vram_width;
	uint32_t feature_flags;
	uint32_t fake_paths_num;
	void *atombios_base_address;
};

struct dc_perf_trace {
	unsigned long read_count;
	unsigned long write_count;
	unsigned long last_entry_read;
	unsigned long last_entry_write;
};

struct dc_context {
	struct dc *dc;

	void *driver_context; /* e.g. amdgpu_device */
	struct dc_perf_trace *perf_trace;
	void *cgs_device;

	enum dce_environment dce_environment;
	struct hw_asic_id asic_id;

	/* todo: below should probably move to dc.  to facilitate removal
	 * of AS we will store these here
	 */
	enum dce_version dce_version;
	struct dc_bios *dc_bios;
	bool created_bios;
	struct gpio_service *gpio_service;
	uint32_t dc_sink_id_count;
	uint32_t dc_stream_id_count;
	uint64_t fbc_gpu_addr;
	struct dc_dmub_srv *dmub_srv;

#ifdef CONFIG_DRM_AMD_DC_HDCP
	struct cp_psp cp_psp;
#endif
};


#define DC_MAX_EDID_BUFFER_SIZE 1024
#define DC_EDID_BLOCK_SIZE 128
#define MAX_SURFACE_NUM 4
#define NUM_PIXEL_FORMATS 10
#define MAX_REPEATER_CNT 8

#include "dc_ddc_types.h"

enum tiling_mode {
	TILING_MODE_INVALID,
	TILING_MODE_LINEAR,
	TILING_MODE_TILED,
	TILING_MODE_COUNT
};

enum view_3d_format {
	VIEW_3D_FORMAT_NONE = 0,
	VIEW_3D_FORMAT_FRAME_SEQUENTIAL,
	VIEW_3D_FORMAT_SIDE_BY_SIDE,
	VIEW_3D_FORMAT_TOP_AND_BOTTOM,
	VIEW_3D_FORMAT_COUNT,
	VIEW_3D_FORMAT_FIRST = VIEW_3D_FORMAT_FRAME_SEQUENTIAL
};

enum plane_stereo_format {
	PLANE_STEREO_FORMAT_NONE = 0,
	PLANE_STEREO_FORMAT_SIDE_BY_SIDE = 1,
	PLANE_STEREO_FORMAT_TOP_AND_BOTTOM = 2,
	PLANE_STEREO_FORMAT_FRAME_ALTERNATE = 3,
	PLANE_STEREO_FORMAT_ROW_INTERLEAVED = 5,
	PLANE_STEREO_FORMAT_COLUMN_INTERLEAVED = 6,
	PLANE_STEREO_FORMAT_CHECKER_BOARD = 7
};

/* TODO: Find way to calculate number of bits
 *  Please increase if pixel_format enum increases
 * num  from  PIXEL_FORMAT_INDEX8 to PIXEL_FORMAT_444BPP32
 */

enum dc_edid_connector_type {
	DC_EDID_CONNECTOR_UNKNOWN = 0,
	DC_EDID_CONNECTOR_ANALOG = 1,
	DC_EDID_CONNECTOR_DIGITAL = 10,
	DC_EDID_CONNECTOR_DVI = 11,
	DC_EDID_CONNECTOR_HDMIA = 12,
	DC_EDID_CONNECTOR_MDDI = 14,
	DC_EDID_CONNECTOR_DISPLAYPORT = 15
};

enum dc_edid_status {
	EDID_OK,
	EDID_BAD_INPUT,
	EDID_NO_RESPONSE,
	EDID_BAD_CHECKSUM,
	EDID_THE_SAME,
};

enum act_return_status {
	ACT_SUCCESS,
	ACT_LINK_LOST,
	ACT_FAILED
};

/* audio capability from EDID*/
struct dc_cea_audio_mode {
	uint8_t format_code; /* ucData[0] [6:3]*/
	uint8_t channel_count; /* ucData[0] [2:0]*/
	uint8_t sample_rate; /* ucData[1]*/
	union {
		uint8_t sample_size; /* for LPCM*/
		/*  for Audio Formats 2-8 (Max bit rate divided by 8 kHz)*/
		uint8_t max_bit_rate;
		uint8_t audio_codec_vendor_specific; /* for Audio Formats 9-15*/
	};
};

struct dc_edid {
	uint32_t length;
	uint8_t raw_edid[DC_MAX_EDID_BUFFER_SIZE];
};

/* When speaker location data block is not available, DEFAULT_SPEAKER_LOCATION
 * is used. In this case we assume speaker location are: front left, front
 * right and front center. */
#define DEFAULT_SPEAKER_LOCATION 5

#define DC_MAX_AUDIO_DESC_COUNT 16

#define AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS 20

union display_content_support {
	unsigned int raw;
	struct {
		unsigned int valid_content_type :1;
		unsigned int game_content :1;
		unsigned int cinema_content :1;
		unsigned int photo_content :1;
		unsigned int graphics_content :1;
		unsigned int reserved :27;
	} bits;
};

struct dc_panel_patch {
	unsigned int dppowerup_delay;
	unsigned int extra_t12_ms;
	unsigned int extra_delay_backlight_off;
	unsigned int extra_t7_ms;
	unsigned int skip_scdc_overwrite;
	unsigned int delay_ignore_msa;
};

struct dc_edid_caps {
	/* sink identification */
	uint16_t manufacturer_id;
	uint16_t product_id;
	uint32_t serial_number;
	uint8_t manufacture_week;
	uint8_t manufacture_year;
	uint8_t display_name[AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS];

	/* audio caps */
	uint8_t speaker_flags;
	uint32_t audio_mode_count;
	struct dc_cea_audio_mode audio_modes[DC_MAX_AUDIO_DESC_COUNT];
	uint32_t audio_latency;
	uint32_t video_latency;

	union display_content_support content_support;

	uint8_t qs_bit;
	uint8_t qy_bit;

	/*HDMI 2.0 caps*/
	bool lte_340mcsc_scramble;

	bool edid_hdmi;
	bool hdr_supported;

	struct dc_panel_patch panel_patch;
};

struct view {
	uint32_t width;
	uint32_t height;
};

struct dc_mode_flags {
	/* note: part of refresh rate flag*/
	uint32_t INTERLACE :1;
	/* native display timing*/
	uint32_t NATIVE :1;
	/* preferred is the recommended mode, one per display */
	uint32_t PREFERRED :1;
	/* true if this mode should use reduced blanking timings
	 *_not_ related to the Reduced Blanking adjustment*/
	uint32_t REDUCED_BLANKING :1;
	/* note: part of refreshrate flag*/
	uint32_t VIDEO_OPTIMIZED_RATE :1;
	/* should be reported to upper layers as mode_flags*/
	uint32_t PACKED_PIXEL_FORMAT :1;
	/*< preferred view*/
	uint32_t PREFERRED_VIEW :1;
	/* this timing should be used only in tiled mode*/
	uint32_t TILED_MODE :1;
	uint32_t DSE_MODE :1;
	/* Refresh rate divider when Miracast sink is using a
	 different rate than the output display device
	 Must be zero for wired displays and non-zero for
	 Miracast displays*/
	uint32_t MIRACAST_REFRESH_DIVIDER;
};


enum dc_timing_source {
	TIMING_SOURCE_UNDEFINED,

	/* explicitly specifed by user, most important*/
	TIMING_SOURCE_USER_FORCED,
	TIMING_SOURCE_USER_OVERRIDE,
	TIMING_SOURCE_CUSTOM,
	TIMING_SOURCE_EXPLICIT,

	/* explicitly specified by the display device, more important*/
	TIMING_SOURCE_EDID_CEA_SVD_3D,
	TIMING_SOURCE_EDID_CEA_SVD_PREFERRED,
	TIMING_SOURCE_EDID_CEA_SVD_420,
	TIMING_SOURCE_EDID_DETAILED,
	TIMING_SOURCE_EDID_ESTABLISHED,
	TIMING_SOURCE_EDID_STANDARD,
	TIMING_SOURCE_EDID_CEA_SVD,
	TIMING_SOURCE_EDID_CVT_3BYTE,
	TIMING_SOURCE_EDID_4BYTE,
	TIMING_SOURCE_VBIOS,
	TIMING_SOURCE_CV,
	TIMING_SOURCE_TV,
	TIMING_SOURCE_HDMI_VIC,

	/* implicitly specified by display device, still safe but less important*/
	TIMING_SOURCE_DEFAULT,

	/* only used for custom base modes */
	TIMING_SOURCE_CUSTOM_BASE,

	/* these timing might not work, least important*/
	TIMING_SOURCE_RANGELIMIT,
	TIMING_SOURCE_OS_FORCED,
	TIMING_SOURCE_IMPLICIT,

	/* only used by default mode list*/
	TIMING_SOURCE_BASICMODE,

	TIMING_SOURCE_COUNT
};


struct stereo_3d_features {
	bool supported			;
	bool allTimings			;
	bool cloneMode			;
	bool scaling			;
	bool singleFrameSWPacked;
};

enum dc_timing_support_method {
	TIMING_SUPPORT_METHOD_UNDEFINED,
	TIMING_SUPPORT_METHOD_EXPLICIT,
	TIMING_SUPPORT_METHOD_IMPLICIT,
	TIMING_SUPPORT_METHOD_NATIVE
};

struct dc_mode_info {
	uint32_t pixel_width;
	uint32_t pixel_height;
	uint32_t field_rate;
	/* Vertical refresh rate for progressive modes.
	* Field rate for interlaced modes.*/

	enum dc_timing_standard timing_standard;
	enum dc_timing_source timing_source;
	struct dc_mode_flags flags;
};

enum dc_power_state {
	DC_POWER_STATE_ON = 1,
	DC_POWER_STATE_STANDBY,
	DC_POWER_STATE_SUSPEND,
	DC_POWER_STATE_OFF
};

/* DC PowerStates */
enum dc_video_power_state {
	DC_VIDEO_POWER_UNSPECIFIED = 0,
	DC_VIDEO_POWER_ON = 1,
	DC_VIDEO_POWER_STANDBY,
	DC_VIDEO_POWER_SUSPEND,
	DC_VIDEO_POWER_OFF,
	DC_VIDEO_POWER_HIBERNATE,
	DC_VIDEO_POWER_SHUTDOWN,
	DC_VIDEO_POWER_ULPS,	/* BACO or Ultra-Light-Power-State */
	DC_VIDEO_POWER_AFTER_RESET,
	DC_VIDEO_POWER_MAXIMUM
};

enum dc_acpi_cm_power_state {
	DC_ACPI_CM_POWER_STATE_D0 = 1,
	DC_ACPI_CM_POWER_STATE_D1 = 2,
	DC_ACPI_CM_POWER_STATE_D2 = 4,
	DC_ACPI_CM_POWER_STATE_D3 = 8
};

enum dc_connection_type {
	dc_connection_none,
	dc_connection_single,
	dc_connection_mst_branch,
	dc_connection_active_dongle
};

struct dc_csc_adjustments {
	struct fixed31_32 contrast;
	struct fixed31_32 saturation;
	struct fixed31_32 brightness;
	struct fixed31_32 hue;
};

enum dpcd_downstream_port_max_bpc {
	DOWN_STREAM_MAX_8BPC = 0,
	DOWN_STREAM_MAX_10BPC,
	DOWN_STREAM_MAX_12BPC,
	DOWN_STREAM_MAX_16BPC
};


enum link_training_offset {
	DPRX                = 0,
	LTTPR_PHY_REPEATER1 = 1,
	LTTPR_PHY_REPEATER2 = 2,
	LTTPR_PHY_REPEATER3 = 3,
	LTTPR_PHY_REPEATER4 = 4,
	LTTPR_PHY_REPEATER5 = 5,
	LTTPR_PHY_REPEATER6 = 6,
	LTTPR_PHY_REPEATER7 = 7,
	LTTPR_PHY_REPEATER8 = 8
};

struct dc_lttpr_caps {
	union dpcd_rev revision;
	uint8_t mode;
	uint8_t max_lane_count;
	uint8_t max_link_rate;
	uint8_t phy_repeater_cnt;
	uint8_t max_ext_timeout;
	uint8_t aux_rd_interval[MAX_REPEATER_CNT - 1];
};

struct dc_dongle_caps {
	/* dongle type (DP converter, CV smart dongle) */
	enum display_dongle_type dongle_type;
	bool extendedCapValid;
	/* If dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER,
	indicates 'Frame Sequential-to-lllFrame Pack' conversion capability.*/
	bool is_dp_hdmi_s3d_converter;
	bool is_dp_hdmi_ycbcr422_pass_through;
	bool is_dp_hdmi_ycbcr420_pass_through;
	bool is_dp_hdmi_ycbcr422_converter;
	bool is_dp_hdmi_ycbcr420_converter;
	uint32_t dp_hdmi_max_bpc;
	uint32_t dp_hdmi_max_pixel_clk_in_khz;
};
/* Scaling format */
enum scaling_transformation {
	SCALING_TRANSFORMATION_UNINITIALIZED,
	SCALING_TRANSFORMATION_IDENTITY = 0x0001,
	SCALING_TRANSFORMATION_CENTER_TIMING = 0x0002,
	SCALING_TRANSFORMATION_FULL_SCREEN_SCALE = 0x0004,
	SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE = 0x0008,
	SCALING_TRANSFORMATION_DAL_DECIDE = 0x0010,
	SCALING_TRANSFORMATION_INVALID = 0x80000000,

	/* Flag the first and last */
	SCALING_TRANSFORMATION_BEGING = SCALING_TRANSFORMATION_IDENTITY,
	SCALING_TRANSFORMATION_END =
		SCALING_TRANSFORMATION_PRESERVE_ASPECT_RATIO_SCALE
};

enum display_content_type {
	DISPLAY_CONTENT_TYPE_NO_DATA = 0,
	DISPLAY_CONTENT_TYPE_GRAPHICS = 1,
	DISPLAY_CONTENT_TYPE_PHOTO = 2,
	DISPLAY_CONTENT_TYPE_CINEMA = 4,
	DISPLAY_CONTENT_TYPE_GAME = 8
};

/* writeback */
struct dwb_stereo_params {
	bool				stereo_enabled;		/* false: normal mode, true: 3D stereo */
	enum dwb_stereo_type		stereo_type;		/* indicates stereo format */
	bool				stereo_polarity;	/* indicates left eye or right eye comes first in stereo mode */
	enum dwb_stereo_eye_select	stereo_eye_select;	/* indicate which eye should be captured */
};

struct dc_dwb_cnv_params {
	unsigned int		src_width;	/* input active width */
	unsigned int		src_height;	/* input active height (half-active height in interlaced mode) */
	unsigned int		crop_width;	/* cropped window width at cnv output */
	bool			crop_en;	/* window cropping enable in cnv */
	unsigned int		crop_height;	/* cropped window height at cnv output */
	unsigned int		crop_x;		/* cropped window start x value at cnv output */
	unsigned int		crop_y;		/* cropped window start y value at cnv output */
	enum dwb_cnv_out_bpc cnv_out_bpc;	/* cnv output pixel depth - 8bpc or 10bpc */
};

struct dc_dwb_params {
	struct dc_dwb_cnv_params	cnv_params;	/* CNV source size and cropping window parameters */
	unsigned int			dest_width;	/* Destination width */
	unsigned int			dest_height;	/* Destination height */
	enum dwb_scaler_mode		out_format;	/* default = YUV420 - TODO: limit this to 0 and 1 on dcn3 */
	enum dwb_output_depth		output_depth;	/* output pixel depth - 8bpc or 10bpc */
	enum dwb_capture_rate		capture_rate;	/* controls the frame capture rate */
	struct scaling_taps 		scaler_taps;	/* Scaling taps */
	enum dwb_subsample_position	subsample_position;
	struct dc_transfer_func *out_transfer_func;
};

/* audio*/

union audio_sample_rates {
	struct sample_rates {
		uint8_t RATE_32:1;
		uint8_t RATE_44_1:1;
		uint8_t RATE_48:1;
		uint8_t RATE_88_2:1;
		uint8_t RATE_96:1;
		uint8_t RATE_176_4:1;
		uint8_t RATE_192:1;
	} rate;

	uint8_t all;
};

struct audio_speaker_flags {
	uint32_t FL_FR:1;
	uint32_t LFE:1;
	uint32_t FC:1;
	uint32_t RL_RR:1;
	uint32_t RC:1;
	uint32_t FLC_FRC:1;
	uint32_t RLC_RRC:1;
	uint32_t SUPPORT_AI:1;
};

struct audio_speaker_info {
	uint32_t ALLSPEAKERS:7;
	uint32_t SUPPORT_AI:1;
};


struct audio_info_flags {

	union {

		struct audio_speaker_flags speaker_flags;
		struct audio_speaker_info   info;

		uint8_t all;
	};
};

enum audio_format_code {
	AUDIO_FORMAT_CODE_FIRST = 1,
	AUDIO_FORMAT_CODE_LINEARPCM = AUDIO_FORMAT_CODE_FIRST,

	AUDIO_FORMAT_CODE_AC3,
	/*Layers 1 & 2 */
	AUDIO_FORMAT_CODE_MPEG1,
	/*MPEG1 Layer 3 */
	AUDIO_FORMAT_CODE_MP3,
	/*multichannel */
	AUDIO_FORMAT_CODE_MPEG2,
	AUDIO_FORMAT_CODE_AAC,
	AUDIO_FORMAT_CODE_DTS,
	AUDIO_FORMAT_CODE_ATRAC,
	AUDIO_FORMAT_CODE_1BITAUDIO,
	AUDIO_FORMAT_CODE_DOLBYDIGITALPLUS,
	AUDIO_FORMAT_CODE_DTS_HD,
	AUDIO_FORMAT_CODE_MAT_MLP,
	AUDIO_FORMAT_CODE_DST,
	AUDIO_FORMAT_CODE_WMAPRO,
	AUDIO_FORMAT_CODE_LAST,
	AUDIO_FORMAT_CODE_COUNT =
		AUDIO_FORMAT_CODE_LAST - AUDIO_FORMAT_CODE_FIRST
};

struct audio_mode {
	 /* ucData[0] [6:3] */
	enum audio_format_code format_code;
	/* ucData[0] [2:0] */
	uint8_t channel_count;
	/* ucData[1] */
	union audio_sample_rates sample_rates;
	union {
		/* for LPCM */
		uint8_t sample_size;
		/* for Audio Formats 2-8 (Max bit rate divided by 8 kHz) */
		uint8_t max_bit_rate;
		/* for Audio Formats 9-15 */
		uint8_t vendor_specific;
	};
};

struct audio_info {
	struct audio_info_flags flags;
	uint32_t video_latency;
	uint32_t audio_latency;
	uint32_t display_index;
	uint8_t display_name[AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS];
	uint32_t manufacture_id;
	uint32_t product_id;
	/* PortID used for ContainerID when defined */
	uint32_t port_id[2];
	uint32_t mode_count;
	/* this field must be last in this struct */
	struct audio_mode modes[DC_MAX_AUDIO_DESC_COUNT];
};
struct audio_check {
	unsigned int audio_packet_type;
	unsigned int max_audiosample_rate;
	unsigned int acat;
};
enum dc_infoframe_type {
	DC_HDMI_INFOFRAME_TYPE_VENDOR = 0x81,
	DC_HDMI_INFOFRAME_TYPE_AVI = 0x82,
	DC_HDMI_INFOFRAME_TYPE_SPD = 0x83,
	DC_HDMI_INFOFRAME_TYPE_AUDIO = 0x84,
	DC_DP_INFOFRAME_TYPE_PPS = 0x10,
};

struct dc_info_packet {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[32];
};

struct dc_info_packet_128 {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[128];
};

#define DC_PLANE_UPDATE_TIMES_MAX 10

struct dc_plane_flip_time {
	unsigned int time_elapsed_in_us[DC_PLANE_UPDATE_TIMES_MAX];
	unsigned int index;
	unsigned int prev_update_time_in_us;
};

struct psr_config {
	unsigned char psr_version;
	unsigned int psr_rfb_setup_time;
	bool psr_exit_link_training_required;
	bool psr_frame_capture_indication_req;
	unsigned int psr_sdp_transmit_line_num_deadline;
	bool allow_smu_optimizations;
};

union dmcu_psr_level {
	struct {
		unsigned int SKIP_CRC:1;
		unsigned int SKIP_DP_VID_STREAM_DISABLE:1;
		unsigned int SKIP_PHY_POWER_DOWN:1;
		unsigned int SKIP_AUX_ACK_CHECK:1;
		unsigned int SKIP_CRTC_DISABLE:1;
		unsigned int SKIP_AUX_RFB_CAPTURE_CHECK:1;
		unsigned int SKIP_SMU_NOTIFICATION:1;
		unsigned int SKIP_AUTO_STATE_ADVANCE:1;
		unsigned int DISABLE_PSR_ENTRY_ABORT:1;
		unsigned int SKIP_SINGLE_OTG_DISABLE:1;
		unsigned int RESERVED:22;
	} bits;
	unsigned int u32all;
};

enum physical_phy_id {
	PHYLD_0,
	PHYLD_1,
	PHYLD_2,
	PHYLD_3,
	PHYLD_4,
	PHYLD_5,
	PHYLD_6,
	PHYLD_7,
	PHYLD_8,
	PHYLD_9,
	PHYLD_COUNT,
	PHYLD_UNKNOWN = (-1L)
};

enum phy_type {
	PHY_TYPE_UNKNOWN  = 1,
	PHY_TYPE_PCIE_PHY = 2,
	PHY_TYPE_UNIPHY = 3,
};

struct psr_context {
	/* ddc line */
	enum channel_id channel;
	/* Transmitter id */
	enum transmitter transmitterId;
	/* Engine Id is used for Dig Be source select */
	enum engine_id engineId;
	/* Controller Id used for Dig Fe source select */
	enum controller_id controllerId;
	/* Pcie or Uniphy */
	enum phy_type phyType;
	/* Physical PHY Id used by SMU interpretation */
	enum physical_phy_id smuPhyId;
	/* Vertical total pixels from crtc timing.
	 * This is used for static screen detection.
	 * ie. If we want to detect half a frame,
	 * we use this to determine the hyst lines.
	 */
	unsigned int crtcTimingVerticalTotal;
	/* PSR supported from panel capabilities and
	 * current display configuration
	 */
	bool psrSupportedDisplayConfig;
	/* Whether fast link training is supported by the panel */
	bool psrExitLinkTrainingRequired;
	/* If RFB setup time is greater than the total VBLANK time,
	 * it is not possible for the sink to capture the video frame
	 * in the same frame the SDP is sent. In this case,
	 * the frame capture indication bit should be set and an extra
	 * static frame should be transmitted to the sink.
	 */
	bool psrFrameCaptureIndicationReq;
	/* Set the last possible line SDP may be transmitted without violating
	 * the RFB setup time or entering the active video frame.
	 */
	unsigned int sdpTransmitLineNumDeadline;
	/* The VSync rate in Hz used to calculate the
	 * step size for smooth brightness feature
	 */
	unsigned int vsync_rate_hz;
	unsigned int skipPsrWaitForPllLock;
	unsigned int numberOfControllers;
	/* Unused, for future use. To indicate that first changed frame from
	 * state3 shouldn't result in psr_inactive, but rather to perform
	 * an automatic single frame rfb_update.
	 */
	bool rfb_update_auto_en;
	/* Number of frame before entering static screen */
	unsigned int timehyst_frames;
	/* Partial frames before entering static screen */
	unsigned int hyst_lines;
	/* # of repeated AUX transaction attempts to make before
	 * indicating failure to the driver
	 */
	unsigned int aux_repeats;
	/* Controls hw blocks to power down during PSR active state */
	union dmcu_psr_level psr_level;
	/* Controls additional delay after remote frame capture before
	 * continuing powerd own
	 */
	unsigned int frame_delay;
	bool allow_smu_optimizations;
};

struct colorspace_transform {
	struct fixed31_32 matrix[12];
	bool enable_remap;
};

enum i2c_mot_mode {
	I2C_MOT_UNDEF,
	I2C_MOT_TRUE,
	I2C_MOT_FALSE
};

struct AsicStateEx {
	unsigned int memoryClock;
	unsigned int displayClock;
	unsigned int engineClock;
	unsigned int maxSupportedDppClock;
	unsigned int dppClock;
	unsigned int socClock;
	unsigned int dcfClockDeepSleep;
	unsigned int fClock;
	unsigned int phyClock;
};


enum dc_clock_type {
	DC_CLOCK_TYPE_DISPCLK = 0,
	DC_CLOCK_TYPE_DPPCLK        = 1,
};

struct dc_clock_config {
	uint32_t max_clock_khz;
	uint32_t min_clock_khz;
	uint32_t bw_requirequired_clock_khz;
	uint32_t current_clock_khz;/*current clock in use*/
};

/* DSC DPCD capabilities */
union dsc_slice_caps1 {
	struct {
		uint8_t NUM_SLICES_1 : 1;
		uint8_t NUM_SLICES_2 : 1;
		uint8_t RESERVED : 1;
		uint8_t NUM_SLICES_4 : 1;
		uint8_t NUM_SLICES_6 : 1;
		uint8_t NUM_SLICES_8 : 1;
		uint8_t NUM_SLICES_10 : 1;
		uint8_t NUM_SLICES_12 : 1;
	} bits;
	uint8_t raw;
};

union dsc_slice_caps2 {
	struct {
		uint8_t NUM_SLICES_16 : 1;
		uint8_t NUM_SLICES_20 : 1;
		uint8_t NUM_SLICES_24 : 1;
		uint8_t RESERVED : 5;
	} bits;
	uint8_t raw;
};

union dsc_color_formats {
	struct {
		uint8_t RGB : 1;
		uint8_t YCBCR_444 : 1;
		uint8_t YCBCR_SIMPLE_422 : 1;
		uint8_t YCBCR_NATIVE_422 : 1;
		uint8_t YCBCR_NATIVE_420 : 1;
		uint8_t RESERVED : 3;
	} bits;
	uint8_t raw;
};

union dsc_color_depth {
	struct {
		uint8_t RESERVED1 : 1;
		uint8_t COLOR_DEPTH_8_BPC : 1;
		uint8_t COLOR_DEPTH_10_BPC : 1;
		uint8_t COLOR_DEPTH_12_BPC : 1;
		uint8_t RESERVED2 : 3;
	} bits;
	uint8_t raw;
};

struct dsc_dec_dpcd_caps {
	bool is_dsc_supported;
	uint8_t dsc_version;
	int32_t rc_buffer_size; /* DSC RC buffer block size in bytes */
	union dsc_slice_caps1 slice_caps1;
	union dsc_slice_caps2 slice_caps2;
	int32_t lb_bit_depth;
	bool is_block_pred_supported;
	int32_t edp_max_bits_per_pixel; /* Valid only in eDP */
	union dsc_color_formats color_formats;
	union dsc_color_depth color_depth;
	int32_t throughput_mode_0_mps; /* In MPs */
	int32_t throughput_mode_1_mps; /* In MPs */
	int32_t max_slice_width;
	uint32_t bpp_increment_div; /* bpp increment divisor, e.g. if 16, it's 1/16th of a bit */

	/* Extended DSC caps */
	uint32_t branch_overall_throughput_0_mps; /* In MPs */
	uint32_t branch_overall_throughput_1_mps; /* In MPs */
	uint32_t branch_max_line_width;
};

#endif /* DC_TYPES_H_ */
