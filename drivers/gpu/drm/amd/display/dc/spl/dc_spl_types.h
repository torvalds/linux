// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_TYPES_H__
#define __DC_SPL_TYPES_H__

#include "spl_debug.h"
#include "spl_os_types.h"   // swap
#include "spl_fixpt31_32.h"	// fixed31_32 and related functions
#include "spl_custom_float.h" // custom float and related functions

struct spl_size {
	uint32_t width;
	uint32_t height;
};
struct spl_rect	{
	int x;
	int y;
	int width;
	int height;
};

struct spl_ratios {
	struct spl_fixed31_32 horz;
	struct spl_fixed31_32 vert;
	struct spl_fixed31_32 horz_c;
	struct spl_fixed31_32 vert_c;
};
struct spl_inits {
	struct spl_fixed31_32 h;
	struct spl_fixed31_32 h_c;
	struct spl_fixed31_32 v;
	struct spl_fixed31_32 v_c;
};

struct spl_taps	{
	uint32_t v_taps;
	uint32_t h_taps;
	uint32_t v_taps_c;
	uint32_t h_taps_c;
	bool integer_scaling;
};
enum spl_view_3d {
	SPL_VIEW_3D_NONE = 0,
	SPL_VIEW_3D_FRAME_SEQUENTIAL,
	SPL_VIEW_3D_SIDE_BY_SIDE,
	SPL_VIEW_3D_TOP_AND_BOTTOM,
	SPL_VIEW_3D_COUNT,
	SPL_VIEW_3D_FIRST = SPL_VIEW_3D_FRAME_SEQUENTIAL
};
/* Pixel format */
enum spl_pixel_format {
	/*graph*/
	SPL_PIXEL_FORMAT_UNINITIALIZED,
	SPL_PIXEL_FORMAT_INDEX8,
	SPL_PIXEL_FORMAT_RGB565,
	SPL_PIXEL_FORMAT_ARGB8888,
	SPL_PIXEL_FORMAT_ARGB2101010,
	SPL_PIXEL_FORMAT_ARGB2101010_XRBIAS,
	SPL_PIXEL_FORMAT_FP16,
	/*video*/
	SPL_PIXEL_FORMAT_420BPP8,
	SPL_PIXEL_FORMAT_420BPP10,
	/*end of pixel format definition*/
	SPL_PIXEL_FORMAT_INVALID,
	SPL_PIXEL_FORMAT_422BPP8,
	SPL_PIXEL_FORMAT_422BPP10,
	SPL_PIXEL_FORMAT_GRPH_BEGIN = SPL_PIXEL_FORMAT_INDEX8,
	SPL_PIXEL_FORMAT_GRPH_END = SPL_PIXEL_FORMAT_FP16,
	SPL_PIXEL_FORMAT_VIDEO_BEGIN = SPL_PIXEL_FORMAT_420BPP8,
	SPL_PIXEL_FORMAT_VIDEO_END = SPL_PIXEL_FORMAT_420BPP10,
	SPL_PIXEL_FORMAT_UNKNOWN
};

enum lb_memory_config {
	/* Enable all 3 pieces of memory */
	LB_MEMORY_CONFIG_0 = 0,

	/* Enable only the first piece of memory */
	LB_MEMORY_CONFIG_1 = 1,

	/* Enable only the second piece of memory */
	LB_MEMORY_CONFIG_2 = 2,

	/* Only applicable in 4:2:0 mode, enable all 3 pieces of memory and the
	 * last piece of chroma memory used for the luma storage
	 */
	LB_MEMORY_CONFIG_3 = 3
};

/* Rotation angle */
enum spl_rotation_angle {
	SPL_ROTATION_ANGLE_0 = 0,
	SPL_ROTATION_ANGLE_90,
	SPL_ROTATION_ANGLE_180,
	SPL_ROTATION_ANGLE_270,
	SPL_ROTATION_ANGLE_COUNT
};
enum spl_color_space {
	SPL_COLOR_SPACE_UNKNOWN,
	SPL_COLOR_SPACE_SRGB,
	SPL_COLOR_SPACE_XR_RGB,
	SPL_COLOR_SPACE_SRGB_LIMITED,
	SPL_COLOR_SPACE_MSREF_SCRGB,
	SPL_COLOR_SPACE_YCBCR601,
	SPL_COLOR_SPACE_YCBCR709,
	SPL_COLOR_SPACE_XV_YCC_709,
	SPL_COLOR_SPACE_XV_YCC_601,
	SPL_COLOR_SPACE_YCBCR601_LIMITED,
	SPL_COLOR_SPACE_YCBCR709_LIMITED,
	SPL_COLOR_SPACE_2020_RGB_FULLRANGE,
	SPL_COLOR_SPACE_2020_RGB_LIMITEDRANGE,
	SPL_COLOR_SPACE_2020_YCBCR,
	SPL_COLOR_SPACE_ADOBERGB,
	SPL_COLOR_SPACE_DCIP3,
	SPL_COLOR_SPACE_DISPLAYNATIVE,
	SPL_COLOR_SPACE_DOLBYVISION,
	SPL_COLOR_SPACE_APPCTRL,
	SPL_COLOR_SPACE_CUSTOMPOINTS,
	SPL_COLOR_SPACE_YCBCR709_BLACK,
};

enum chroma_cositing {
	CHROMA_COSITING_NONE,
	CHROMA_COSITING_LEFT,
	CHROMA_COSITING_TOPLEFT,
	CHROMA_COSITING_COUNT
};

// Scratch space for calculating scaler params
struct spl_scaler_data {
	int h_active;
	int v_active;
	struct spl_taps taps;
	struct spl_rect viewport;
	struct spl_rect viewport_c;
	struct spl_rect recout;
	struct spl_ratios ratios;
	struct spl_ratios recip_ratios;
	struct spl_inits inits;
};

enum spl_transfer_func_type {
	SPL_TF_TYPE_PREDEFINED,
	SPL_TF_TYPE_DISTRIBUTED_POINTS,
	SPL_TF_TYPE_BYPASS,
	SPL_TF_TYPE_HWPWL
};

enum spl_transfer_func_predefined {
	SPL_TRANSFER_FUNCTION_SRGB,
	SPL_TRANSFER_FUNCTION_BT709,
	SPL_TRANSFER_FUNCTION_PQ,
	SPL_TRANSFER_FUNCTION_LINEAR,
	SPL_TRANSFER_FUNCTION_UNITY,
	SPL_TRANSFER_FUNCTION_HLG,
	SPL_TRANSFER_FUNCTION_HLG12,
	SPL_TRANSFER_FUNCTION_GAMMA22,
	SPL_TRANSFER_FUNCTION_GAMMA24,
	SPL_TRANSFER_FUNCTION_GAMMA26
};

/*==============================================================*/
/* Below structs are defined to hold hw register data */

// SPL output is used to set below registers

// MPC_SIZE - set based on scl_data h_active and v_active
struct mpc_size	{
	uint32_t width;
	uint32_t height;
};
// SCL_MODE - set based on scl_data.ratios and always_scale
enum scl_mode {
	SCL_MODE_SCALING_444_BYPASS = 0,
	SCL_MODE_SCALING_444_RGB_ENABLE = 1,
	SCL_MODE_SCALING_444_YCBCR_ENABLE = 2,
	SCL_MODE_SCALING_420_YCBCR_ENABLE = 3,
	SCL_MODE_SCALING_420_LUMA_BYPASS = 4,
	SCL_MODE_SCALING_420_CHROMA_BYPASS = 5,
	SCL_MODE_DSCL_BYPASS = 6
};
// SCL_BLACK_COLOR - set based on scl_data.format
struct scl_black_color	{
	uint32_t offset_rgb_y;
	uint32_t offset_rgb_cbcr;
};
// RATIO - set based on scl_data.ratios
struct ratio	{
	uint32_t h_scale_ratio;
	uint32_t v_scale_ratio;
	uint32_t h_scale_ratio_c;
	uint32_t v_scale_ratio_c;
};

// INIT - set based on scl_data.init
struct init	{
	// SCL_HORZ_FILTER_INIT
	uint32_t h_filter_init_frac;	//	SCL_H_INIT_FRAC
	uint32_t h_filter_init_int;	//	SCL_H_INIT_INT
	// SCL_HORZ_FILTER_INIT_C
	uint32_t h_filter_init_frac_c;	//	SCL_H_INIT_FRAC_C
	uint32_t h_filter_init_int_c;	//	SCL_H_INIT_INT_C
	// SCL_VERT_FILTER_INIT
	uint32_t v_filter_init_frac;	//	SCL_V_INIT_FRAC
	uint32_t v_filter_init_int;	//	SCL_V_INIT_INT
	//	SCL_VERT_FILTER_INIT_C
	uint32_t v_filter_init_frac_c;	//	SCL_V_INIT_FRAC_C
	uint32_t v_filter_init_int_c;	//	SCL_V_INIT_INT_C
	//	SCL_VERT_FILTER_INIT_BOT
	uint32_t v_filter_init_bot_frac;	//	SCL_V_INIT_FRAC_BOT
	uint32_t v_filter_init_bot_int;	//	SCL_V_INIT_INT_BOT
	//	SCL_VERT_FILTER_INIT_BOT_C
	uint32_t v_filter_init_bot_frac_c;	//	SCL_V_INIT_FRAC_BOT_C
	uint32_t v_filter_init_bot_int_c;	//	SCL_V_INIT_INT_BOT_C
};

// FILTER - calculated based on scl_data ratios and taps

// iSHARP
struct isharp_noise_det {
	uint32_t enable;	// ISHARP_NOISEDET_EN
	uint32_t mode;		// ISHARP_NOISEDET_MODE
	uint32_t uthreshold;	// ISHARP_NOISEDET_UTHRE
	uint32_t dthreshold;	// ISHARP_NOISEDET_DTHRE
	uint32_t pwl_start_in;	// ISHARP_NOISEDET_PWL_START_IN
	uint32_t pwl_end_in;	// ISHARP_NOISEDET_PWL_END_IN
	uint32_t pwl_slope;	// ISHARP_NOISEDET_PWL_SLOPE
};
struct isharp_lba	{
	uint32_t mode;	// ISHARP_LBA_MODE
	uint32_t in_seg[6];
	uint32_t base_seg[6];
	uint32_t slope_seg[6];
};
struct isharp_fmt	{
	uint32_t mode;	// ISHARP_FMT_MODE
	uint32_t norm;	// ISHARP_FMT_NORM
};
struct isharp_nldelta_sclip	{
	uint32_t enable_p;	// ISHARP_NLDELTA_SCLIP_EN_P
	uint32_t pivot_p;	// ISHARP_NLDELTA_SCLIP_PIVOT_P
	uint32_t slope_p;	// ISHARP_NLDELTA_SCLIP_SLOPE_P
	uint32_t enable_n;	// ISHARP_NLDELTA_SCLIP_EN_N
	uint32_t pivot_n;	// ISHARP_NLDELTA_SCLIP_PIVOT_N
	uint32_t slope_n;	// ISHARP_NLDELTA_SCLIP_SLOPE_N
};
enum isharp_en	{
	ISHARP_DISABLE,
	ISHARP_ENABLE
};
#define ISHARP_LUT_TABLE_SIZE 32
// Below struct holds values that can be directly used to program
// hardware registers. No conversion/clamping is required
struct dscl_prog_data {
	struct spl_rect recout; // RECOUT - set based on scl_data.recout
	struct mpc_size mpc_size;
	uint32_t dscl_mode;
	struct scl_black_color scl_black_color;
	struct ratio ratios;
	struct init init;
	struct spl_taps taps;	// TAPS - set based on scl_data.taps
	struct spl_rect viewport;
	struct spl_rect viewport_c;
	// raw filter
	const uint16_t *filter_h;
	const uint16_t *filter_v;
	const uint16_t *filter_h_c;
	const uint16_t *filter_v_c;
	// EASF registers
	uint32_t easf_matrix_mode;
	uint32_t easf_ltonl_en;
	uint32_t easf_v_en;
	uint32_t easf_v_sharp_factor;
	uint32_t easf_v_ring;
	uint32_t easf_v_bf1_en;
	uint32_t easf_v_bf2_mode;
	uint32_t easf_v_bf3_mode;
	uint32_t easf_v_bf2_flat1_gain;
	uint32_t easf_v_bf2_flat2_gain;
	uint32_t easf_v_bf2_roc_gain;
	uint32_t easf_v_ringest_3tap_dntilt_uptilt;
	uint32_t easf_v_ringest_3tap_uptilt_max;
	uint32_t easf_v_ringest_3tap_dntilt_slope;
	uint32_t easf_v_ringest_3tap_uptilt1_slope;
	uint32_t easf_v_ringest_3tap_uptilt2_slope;
	uint32_t easf_v_ringest_3tap_uptilt2_offset;
	uint32_t easf_v_ringest_eventap_reduceg1;
	uint32_t easf_v_ringest_eventap_reduceg2;
	uint32_t easf_v_ringest_eventap_gain1;
	uint32_t easf_v_ringest_eventap_gain2;
	uint32_t easf_v_bf_maxa;
	uint32_t easf_v_bf_maxb;
	uint32_t easf_v_bf_mina;
	uint32_t easf_v_bf_minb;
	uint32_t easf_v_bf1_pwl_in_seg0;
	uint32_t easf_v_bf1_pwl_base_seg0;
	uint32_t easf_v_bf1_pwl_slope_seg0;
	uint32_t easf_v_bf1_pwl_in_seg1;
	uint32_t easf_v_bf1_pwl_base_seg1;
	uint32_t easf_v_bf1_pwl_slope_seg1;
	uint32_t easf_v_bf1_pwl_in_seg2;
	uint32_t easf_v_bf1_pwl_base_seg2;
	uint32_t easf_v_bf1_pwl_slope_seg2;
	uint32_t easf_v_bf1_pwl_in_seg3;
	uint32_t easf_v_bf1_pwl_base_seg3;
	uint32_t easf_v_bf1_pwl_slope_seg3;
	uint32_t easf_v_bf1_pwl_in_seg4;
	uint32_t easf_v_bf1_pwl_base_seg4;
	uint32_t easf_v_bf1_pwl_slope_seg4;
	uint32_t easf_v_bf1_pwl_in_seg5;
	uint32_t easf_v_bf1_pwl_base_seg5;
	uint32_t easf_v_bf1_pwl_slope_seg5;
	uint32_t easf_v_bf1_pwl_in_seg6;
	uint32_t easf_v_bf1_pwl_base_seg6;
	uint32_t easf_v_bf1_pwl_slope_seg6;
	uint32_t easf_v_bf1_pwl_in_seg7;
	uint32_t easf_v_bf1_pwl_base_seg7;
	uint32_t easf_v_bf3_pwl_in_set0;
	uint32_t easf_v_bf3_pwl_base_set0;
	uint32_t easf_v_bf3_pwl_slope_set0;
	uint32_t easf_v_bf3_pwl_in_set1;
	uint32_t easf_v_bf3_pwl_base_set1;
	uint32_t easf_v_bf3_pwl_slope_set1;
	uint32_t easf_v_bf3_pwl_in_set2;
	uint32_t easf_v_bf3_pwl_base_set2;
	uint32_t easf_v_bf3_pwl_slope_set2;
	uint32_t easf_v_bf3_pwl_in_set3;
	uint32_t easf_v_bf3_pwl_base_set3;
	uint32_t easf_v_bf3_pwl_slope_set3;
	uint32_t easf_v_bf3_pwl_in_set4;
	uint32_t easf_v_bf3_pwl_base_set4;
	uint32_t easf_v_bf3_pwl_slope_set4;
	uint32_t easf_v_bf3_pwl_in_set5;
	uint32_t easf_v_bf3_pwl_base_set5;
	uint32_t easf_h_en;
	uint32_t easf_h_sharp_factor;
	uint32_t easf_h_ring;
	uint32_t easf_h_bf1_en;
	uint32_t easf_h_bf2_mode;
	uint32_t easf_h_bf3_mode;
	uint32_t easf_h_bf2_flat1_gain;
	uint32_t easf_h_bf2_flat2_gain;
	uint32_t easf_h_bf2_roc_gain;
	uint32_t easf_h_ringest_eventap_reduceg1;
	uint32_t easf_h_ringest_eventap_reduceg2;
	uint32_t easf_h_ringest_eventap_gain1;
	uint32_t easf_h_ringest_eventap_gain2;
	uint32_t easf_h_bf_maxa;
	uint32_t easf_h_bf_maxb;
	uint32_t easf_h_bf_mina;
	uint32_t easf_h_bf_minb;
	uint32_t easf_h_bf1_pwl_in_seg0;
	uint32_t easf_h_bf1_pwl_base_seg0;
	uint32_t easf_h_bf1_pwl_slope_seg0;
	uint32_t easf_h_bf1_pwl_in_seg1;
	uint32_t easf_h_bf1_pwl_base_seg1;
	uint32_t easf_h_bf1_pwl_slope_seg1;
	uint32_t easf_h_bf1_pwl_in_seg2;
	uint32_t easf_h_bf1_pwl_base_seg2;
	uint32_t easf_h_bf1_pwl_slope_seg2;
	uint32_t easf_h_bf1_pwl_in_seg3;
	uint32_t easf_h_bf1_pwl_base_seg3;
	uint32_t easf_h_bf1_pwl_slope_seg3;
	uint32_t easf_h_bf1_pwl_in_seg4;
	uint32_t easf_h_bf1_pwl_base_seg4;
	uint32_t easf_h_bf1_pwl_slope_seg4;
	uint32_t easf_h_bf1_pwl_in_seg5;
	uint32_t easf_h_bf1_pwl_base_seg5;
	uint32_t easf_h_bf1_pwl_slope_seg5;
	uint32_t easf_h_bf1_pwl_in_seg6;
	uint32_t easf_h_bf1_pwl_base_seg6;
	uint32_t easf_h_bf1_pwl_slope_seg6;
	uint32_t easf_h_bf1_pwl_in_seg7;
	uint32_t easf_h_bf1_pwl_base_seg7;
	uint32_t easf_h_bf3_pwl_in_set0;
	uint32_t easf_h_bf3_pwl_base_set0;
	uint32_t easf_h_bf3_pwl_slope_set0;
	uint32_t easf_h_bf3_pwl_in_set1;
	uint32_t easf_h_bf3_pwl_base_set1;
	uint32_t easf_h_bf3_pwl_slope_set1;
	uint32_t easf_h_bf3_pwl_in_set2;
	uint32_t easf_h_bf3_pwl_base_set2;
	uint32_t easf_h_bf3_pwl_slope_set2;
	uint32_t easf_h_bf3_pwl_in_set3;
	uint32_t easf_h_bf3_pwl_base_set3;
	uint32_t easf_h_bf3_pwl_slope_set3;
	uint32_t easf_h_bf3_pwl_in_set4;
	uint32_t easf_h_bf3_pwl_base_set4;
	uint32_t easf_h_bf3_pwl_slope_set4;
	uint32_t easf_h_bf3_pwl_in_set5;
	uint32_t easf_h_bf3_pwl_base_set5;
	uint32_t easf_matrix_c0;
	uint32_t easf_matrix_c1;
	uint32_t easf_matrix_c2;
	uint32_t easf_matrix_c3;
	// iSharp
	uint32_t isharp_en;     //      ISHARP_EN
	struct isharp_noise_det isharp_noise_det;       //      ISHARP_NOISEDET
	uint32_t isharp_nl_en;  //      ISHARP_NL_EN ? TODO:check this
	struct isharp_lba isharp_lba;   //      ISHARP_LBA
	struct isharp_fmt isharp_fmt;   //      ISHARP_FMT
	uint32_t isharp_delta[ISHARP_LUT_TABLE_SIZE];
	struct isharp_nldelta_sclip isharp_nldelta_sclip;       //      ISHARP_NLDELTA_SCLIP
	/* blur and scale filter */
	const uint16_t *filter_blur_scale_v;
	const uint16_t *filter_blur_scale_h;
	int sharpness_level; /* Track sharpness level */
};

/* SPL input and output definitions */
// SPL scratch struct
struct spl_scratch {
	// Pack all SPL outputs in scl_data
	struct spl_scaler_data scl_data;
};

/* SPL input and output definitions */
// SPL outputs struct
struct spl_out	{
	// Pack all output need to program hw registers
	struct dscl_prog_data *dscl_prog_data;
};

// end of SPL outputs

// SPL inputs

// Basic input information
struct basic_in	{
	enum spl_pixel_format format; // Pixel Format
	enum chroma_cositing cositing; /* Chroma Subsampling Offset */
	struct spl_rect src_rect; // Source rect
	struct spl_rect dst_rect; // Destination Rect
	struct spl_rect clip_rect; // Clip rect
	enum spl_rotation_angle rotation;  // Rotation
	bool horizontal_mirror;  // Horizontal mirror
	int mpc_combine_h; // MPC Horizontal Combine Factor (split_count)
	int mpc_combine_v; // MPC Vertical Combine Factor (split_idx)
	// Inputs for adaptive scaler - TODO
	enum spl_transfer_func_type tf_type; /* Transfer function type */
	enum spl_transfer_func_predefined tf_predefined_type; /* Transfer function predefined type */
	// enum dc_transfer_func_predefined tf;
	enum spl_color_space color_space;	//	Color Space
	unsigned int max_luminance;	//	Max Luminance TODO: Is determined in dc_hw_sequencer.c is_sdr
	bool film_grain_applied;	//	Film Grain Applied // TODO: To check from where to get this?
};

// Basic output information
struct basic_out {
	struct spl_size output_size; // Output Size
	struct spl_rect dst_rect;	// Destination Rect
	struct spl_rect src_rect;	// Source rect
	int odm_combine_factor;	// deprecated
	struct spl_rect odm_slice_rect; // OPP input rect in timing active
	enum spl_view_3d view_format;	// TODO: View format Check if it is chroma subsampling
	bool always_scale;	// Is always scale enabled? Required for getting SCL_MODE
	int max_downscale_src_width; // Required to get optimal no of taps
	bool alpha_en;
	bool use_two_pixels_per_container;
};
enum sharpness_setting	{
	SHARPNESS_HW_OFF = 0,
	SHARPNESS_ZERO,
	SHARPNESS_CUSTOM
};
struct spl_sharpness_range {
	int sdr_rgb_min;
	int sdr_rgb_max;
	int sdr_rgb_mid;
	int sdr_yuv_min;
	int sdr_yuv_max;
	int sdr_yuv_mid;
	int hdr_rgb_min;
	int hdr_rgb_max;
	int hdr_rgb_mid;
};
struct adaptive_sharpness {
	bool enable;
	int sharpness_level;
	struct spl_sharpness_range sharpness_range;
};
enum linear_light_scaling	{	// convert it in translation logic
	LLS_PREF_DONT_CARE = 0,
	LLS_PREF_YES,
	LLS_PREF_NO
};
enum sharpen_policy {
	SHARPEN_ALWAYS = 0,
	SHARPEN_YUV = 1,
	SHARPEN_RGB_FULLSCREEN_YUV = 2,
	SHARPEN_FULLSCREEN_ALL = 3
};
enum scale_to_sharpness_policy {
	NO_SCALE_TO_SHARPNESS_ADJ = 0,
	SCALE_TO_SHARPNESS_ADJ_YUV = 1,
	SCALE_TO_SHARPNESS_ADJ_ALL = 2
};
struct spl_callbacks {
	void (*spl_calc_lb_num_partitions)
		(bool alpha_en,
		const struct spl_scaler_data *scl_data,
		enum lb_memory_config lb_config,
		int *num_part_y,
		int *num_part_c);
};

struct spl_debug {
	int visual_confirm_base_offset;
	int visual_confirm_dpp_offset;
	enum scale_to_sharpness_policy scale_to_sharpness_policy;
};

struct spl_in	{
	struct basic_out basic_out;
	struct basic_in basic_in;
	// Basic slice information
	int odm_slice_index;	// ODM Slice Index using get_odm_split_index
	struct spl_taps scaling_quality; // Explicit Scaling Quality
	struct spl_callbacks callbacks;
	// Inputs for isharp and EASF
	struct adaptive_sharpness adaptive_sharpness;	//	Adaptive Sharpness
	enum linear_light_scaling lls_pref;	//	Linear Light Scaling
	bool prefer_easf;
	bool disable_easf;
	struct spl_debug debug;
	bool is_fullscreen;
	bool is_hdr_on;
	int h_active;
	int v_active;
	int sdr_white_level_nits;
	enum sharpen_policy sharpen_policy;
};
// end of SPL inputs

#endif /* __DC_SPL_TYPES_H__ */
