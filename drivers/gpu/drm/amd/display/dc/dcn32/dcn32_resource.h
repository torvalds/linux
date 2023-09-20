/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef _DCN32_RESOURCE_H_
#define _DCN32_RESOURCE_H_

#include "core_types.h"

#define DCN3_2_DEFAULT_DET_SIZE 256
#define DCN3_2_MAX_DET_SIZE 1152
#define DCN3_2_MIN_DET_SIZE 128
#define DCN3_2_MIN_COMPBUF_SIZE_KB 128
#define DCN3_2_DET_SEG_SIZE 64
#define DCN3_2_MALL_MBLK_SIZE_BYTES 65536 // 64 * 1024
#define DCN3_2_MBLK_WIDTH 128
#define DCN3_2_MBLK_HEIGHT_4BPE 128
#define DCN3_2_MBLK_HEIGHT_8BPE 64
#define DCN3_2_DCFCLK_DS_INIT_KHZ 10000 // Choose 10Mhz for init DCFCLK DS freq
#define SUBVP_HIGH_REFRESH_LIST_LEN 3
#define DCN3_2_MAX_SUBVP_PIXEL_RATE_MHZ 1800
#define DCN3_2_VMIN_DISPCLK_HZ 717000000

#define TO_DCN32_RES_POOL(pool)\
	container_of(pool, struct dcn32_resource_pool, base)

extern struct _vcs_dpi_ip_params_st dcn3_2_ip;
extern struct _vcs_dpi_soc_bounding_box_st dcn3_2_soc;

struct subvp_high_refresh_list {
	int min_refresh;
	int max_refresh;
	struct resolution {
		int width;
		int height;
	} res[SUBVP_HIGH_REFRESH_LIST_LEN];
};

struct dcn32_resource_pool {
	struct resource_pool base;
};

struct resource_pool *dcn32_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

struct panel_cntl *dcn32_panel_cntl_create(
		const struct panel_cntl_init_data *init_data);

bool dcn32_acquire_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		int mpcc_id,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

bool dcn32_release_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper);

bool dcn32_remove_phantom_pipes(struct dc *dc,
		struct dc_state *context, bool fast_update);

void dcn32_retain_phantom_pipes(struct dc *dc,
		struct dc_state *context);

void dcn32_add_phantom_pipes(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		unsigned int pipe_cnt,
		unsigned int index);

bool dcn32_validate_bandwidth(struct dc *dc,
		struct dc_state *context,
		bool fast_validate);

int dcn32_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate);

void dcn32_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel);

uint32_t dcn32_helper_mall_bytes_to_ways(
		struct dc *dc,
		uint32_t total_size_in_mall_bytes);

uint32_t dcn32_helper_calculate_mall_bytes_for_cursor(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool ignore_cursor_buf);

uint32_t dcn32_helper_calculate_num_ways_for_subvp(
		struct dc *dc,
		struct dc_state *context);

void dcn32_merge_pipes_for_subvp(struct dc *dc,
		struct dc_state *context);

bool dcn32_all_pipes_have_stream_and_plane(struct dc *dc,
		struct dc_state *context);

bool dcn32_subvp_in_use(struct dc *dc,
		struct dc_state *context);

bool dcn32_mpo_in_use(struct dc_state *context);

bool dcn32_any_surfaces_rotated(struct dc *dc, struct dc_state *context);
bool dcn32_is_center_timing(struct pipe_ctx *pipe);
bool dcn32_is_psr_capable(struct pipe_ctx *pipe);

struct pipe_ctx *dcn32_acquire_idle_pipe_for_head_pipe_in_layer(
		struct dc_state *state,
		const struct resource_pool *pool,
		struct dc_stream_state *stream,
		struct pipe_ctx *head_pipe);

void dcn32_determine_det_override(struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes);

void dcn32_set_det_allocations(struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes);

void dcn32_save_mall_state(struct dc *dc,
		struct dc_state *context,
		struct mall_temp_config *temp_config);

void dcn32_restore_mall_state(struct dc *dc,
		struct dc_state *context,
		struct mall_temp_config *temp_config);

struct dc_stream_state *dcn32_can_support_mclk_switch_using_fw_based_vblank_stretch(struct dc *dc, const struct dc_state *context);

bool dcn32_allow_subvp_with_active_margin(struct pipe_ctx *pipe);

bool dcn32_allow_subvp_high_refresh_rate(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe);

unsigned int dcn32_calc_num_avail_chans_for_mall(struct dc *dc, int num_chans);

double dcn32_determine_max_vratio_prefetch(struct dc *dc, struct dc_state *context);

bool dcn32_check_native_scaling_for_res(struct pipe_ctx *pipe, unsigned int width, unsigned int height);

bool dcn32_subvp_drr_admissable(struct dc *dc, struct dc_state *context);

bool dcn32_subvp_vblank_admissable(struct dc *dc, struct dc_state *context, int vlevel);

/* definitions for run time init of reg offsets */

/* CLK SRC */
#define CS_COMMON_REG_LIST_DCN3_0_RI(index, pllid)                             \
  (                                                                            \
  SRI_ARR_ALPHABET(PIXCLK_RESYNC_CNTL, PHYPLL, index, pllid),                  \
      SRII_ARR_2(PHASE, DP_DTO, 0, index),                                     \
      SRII_ARR_2(PHASE, DP_DTO, 1, index),                                     \
      SRII_ARR_2(PHASE, DP_DTO, 2, index),                                     \
      SRII_ARR_2(PHASE, DP_DTO, 3, index),                                     \
      SRII_ARR_2(MODULO, DP_DTO, 0, index),                                    \
      SRII_ARR_2(MODULO, DP_DTO, 1, index),                                    \
      SRII_ARR_2(MODULO, DP_DTO, 2, index),                                    \
      SRII_ARR_2(MODULO, DP_DTO, 3, index),                                    \
      SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 0, index),                              \
      SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 1, index),                              \
      SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 2, index),                              \
      SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 3, index)                               \
  )

/* ABM */
#define ABM_DCN32_REG_LIST_RI(id)                                              \
  ( \
  SRI_ARR(DC_ABM1_HG_SAMPLE_RATE, ABM, id),                                    \
      SRI_ARR(DC_ABM1_LS_SAMPLE_RATE, ABM, id),                                \
      SRI_ARR(BL1_PWM_BL_UPDATE_SAMPLE_RATE, ABM, id),                         \
      SRI_ARR(DC_ABM1_HG_MISC_CTRL, ABM, id),                                  \
      SRI_ARR(DC_ABM1_IPCSC_COEFF_SEL, ABM, id),                               \
      SRI_ARR(BL1_PWM_CURRENT_ABM_LEVEL, ABM, id),                             \
      SRI_ARR(BL1_PWM_TARGET_ABM_LEVEL, ABM, id),                              \
      SRI_ARR(BL1_PWM_USER_LEVEL, ABM, id),                                    \
      SRI_ARR(DC_ABM1_LS_MIN_MAX_PIXEL_VALUE_THRES, ABM, id),                  \
      SRI_ARR(DC_ABM1_HGLS_REG_READ_PROGRESS, ABM, id),                        \
      SRI_ARR(DC_ABM1_ACE_OFFSET_SLOPE_0, ABM, id),                            \
      SRI_ARR(DC_ABM1_ACE_THRES_12, ABM, id), NBIO_SR_ARR(BIOS_SCRATCH_2, id)  \
  )

/* Audio */
#define AUD_COMMON_REG_LIST_RI(id)                                             \
  ( \
  SRI_ARR(AZALIA_F0_CODEC_ENDPOINT_INDEX, AZF0ENDPOINT, id),                   \
      SRI_ARR(AZALIA_F0_CODEC_ENDPOINT_DATA, AZF0ENDPOINT, id),                \
      SR_ARR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_STREAM_FORMATS, id),           \
      SR_ARR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES, id),     \
      SR_ARR(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, id),             \
      SR_ARR(DCCG_AUDIO_DTO_SOURCE, id), SR_ARR(DCCG_AUDIO_DTO0_MODULE, id),   \
      SR_ARR(DCCG_AUDIO_DTO0_PHASE, id), SR_ARR(DCCG_AUDIO_DTO1_MODULE, id),   \
      SR_ARR(DCCG_AUDIO_DTO1_PHASE, id)                                        \
  )

/* VPG */

#define VPG_DCN3_REG_LIST_RI(id)                                               \
  ( \
  SRI_ARR(VPG_GENERIC_STATUS, VPG, id),                                        \
      SRI_ARR(VPG_GENERIC_PACKET_ACCESS_CTRL, VPG, id),                        \
      SRI_ARR(VPG_GENERIC_PACKET_DATA, VPG, id),                               \
      SRI_ARR(VPG_GSP_FRAME_UPDATE_CTRL, VPG, id),                             \
      SRI_ARR(VPG_GSP_IMMEDIATE_UPDATE_CTRL, VPG, id)                          \
  )

/* AFMT */
#define AFMT_DCN3_REG_LIST_RI(id)                                              \
  ( \
  SRI_ARR(AFMT_INFOFRAME_CONTROL0, AFMT, id),                                  \
      SRI_ARR(AFMT_VBI_PACKET_CONTROL, AFMT, id),                              \
      SRI_ARR(AFMT_AUDIO_PACKET_CONTROL, AFMT, id),                            \
      SRI_ARR(AFMT_AUDIO_PACKET_CONTROL2, AFMT, id),                           \
      SRI_ARR(AFMT_AUDIO_SRC_CONTROL, AFMT, id),                               \
      SRI_ARR(AFMT_60958_0, AFMT, id), SRI_ARR(AFMT_60958_1, AFMT, id),        \
      SRI_ARR(AFMT_60958_2, AFMT, id), SRI_ARR(AFMT_MEM_PWR, AFMT, id)         \
  )

/* APG */
#define APG_DCN31_REG_LIST_RI(id)                                              \
  (\
  SRI_ARR(APG_CONTROL, APG, id), SRI_ARR(APG_CONTROL2, APG, id),               \
      SRI_ARR(APG_MEM_PWR, APG, id), SRI_ARR(APG_DBG_GEN_CONTROL, APG, id)     \
  )

/* Stream encoder */
#define SE_DCN32_REG_LIST_RI(id)                                               \
  ( \
  SRI_ARR(AFMT_CNTL, DIG, id), SRI_ARR(DIG_FE_CNTL, DIG, id),                  \
      SRI_ARR(HDMI_CONTROL, DIG, id), SRI_ARR(HDMI_DB_CONTROL, DIG, id),       \
      SRI_ARR(HDMI_GC, DIG, id),                                               \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL0, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL1, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL2, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL3, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL4, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL5, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL6, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL7, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL8, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL9, DIG, id),                          \
      SRI_ARR(HDMI_GENERIC_PACKET_CONTROL10, DIG, id),                         \
      SRI_ARR(HDMI_INFOFRAME_CONTROL0, DIG, id),                               \
      SRI_ARR(HDMI_INFOFRAME_CONTROL1, DIG, id),                               \
      SRI_ARR(HDMI_VBI_PACKET_CONTROL, DIG, id),                               \
      SRI_ARR(HDMI_AUDIO_PACKET_CONTROL, DIG, id),                             \
      SRI_ARR(HDMI_ACR_PACKET_CONTROL, DIG, id),                               \
      SRI_ARR(HDMI_ACR_32_0, DIG, id), SRI_ARR(HDMI_ACR_32_1, DIG, id),        \
      SRI_ARR(HDMI_ACR_44_0, DIG, id), SRI_ARR(HDMI_ACR_44_1, DIG, id),        \
      SRI_ARR(HDMI_ACR_48_0, DIG, id), SRI_ARR(HDMI_ACR_48_1, DIG, id),        \
      SRI_ARR(DP_DB_CNTL, DP, id), SRI_ARR(DP_MSA_MISC, DP, id),               \
      SRI_ARR(DP_MSA_VBID_MISC, DP, id), SRI_ARR(DP_MSA_COLORIMETRY, DP, id),  \
      SRI_ARR(DP_MSA_TIMING_PARAM1, DP, id),                                   \
      SRI_ARR(DP_MSA_TIMING_PARAM2, DP, id),                                   \
      SRI_ARR(DP_MSA_TIMING_PARAM3, DP, id),                                   \
      SRI_ARR(DP_MSA_TIMING_PARAM4, DP, id),                                   \
      SRI_ARR(DP_MSE_RATE_CNTL, DP, id), SRI_ARR(DP_MSE_RATE_UPDATE, DP, id),  \
      SRI_ARR(DP_PIXEL_FORMAT, DP, id), SRI_ARR(DP_SEC_CNTL, DP, id),          \
      SRI_ARR(DP_SEC_CNTL1, DP, id), SRI_ARR(DP_SEC_CNTL2, DP, id),            \
      SRI_ARR(DP_SEC_CNTL5, DP, id), SRI_ARR(DP_SEC_CNTL6, DP, id),            \
      SRI_ARR(DP_STEER_FIFO, DP, id), SRI_ARR(DP_VID_M, DP, id),               \
      SRI_ARR(DP_VID_N, DP, id), SRI_ARR(DP_VID_STREAM_CNTL, DP, id),          \
      SRI_ARR(DP_VID_TIMING, DP, id), SRI_ARR(DP_SEC_AUD_N, DP, id),           \
      SRI_ARR(DP_SEC_TIMESTAMP, DP, id), SRI_ARR(DP_DSC_CNTL, DP, id),         \
      SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                           \
      SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                          \
      SRI_ARR(DP_SEC_FRAMING4, DP, id), SRI_ARR(DP_GSP11_CNTL, DP, id),        \
      SRI_ARR(DME_CONTROL, DME, id),                                           \
      SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                           \
      SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                          \
      SRI_ARR(DIG_FE_CNTL, DIG, id), SRI_ARR(DIG_CLOCK_PATTERN, DIG, id),      \
      SRI_ARR(DIG_FIFO_CTRL0, DIG, id)                                         \
  )

/* Aux regs */

#define AUX_REG_LIST_RI(id)                                                    \
  ( \
  SRI_ARR(AUX_CONTROL, DP_AUX, id), SRI_ARR(AUX_DPHY_RX_CONTROL0, DP_AUX, id), \
      SRI_ARR(AUX_DPHY_RX_CONTROL1, DP_AUX, id)                                \
  )

#define DCN2_AUX_REG_LIST_RI(id)                                               \
  ( \
  AUX_REG_LIST_RI(id), SRI_ARR(AUX_DPHY_TX_CONTROL, DP_AUX, id)                \
  )

/* HDP */
#define HPD_REG_LIST_RI(id) SRI_ARR(DC_HPD_CONTROL, HPD, id)

/* Link encoder */
#define LE_DCN3_REG_LIST_RI(id)                                                \
  ( \
  SRI_ARR(DIG_BE_CNTL, DIG, id), SRI_ARR(DIG_BE_EN_CNTL, DIG, id),             \
      SRI_ARR(TMDS_CTL_BITS, DIG, id),                                         \
      SRI_ARR(TMDS_DCBALANCER_CONTROL, DIG, id), SRI_ARR(DP_CONFIG, DP, id),   \
      SRI_ARR(DP_DPHY_CNTL, DP, id), SRI_ARR(DP_DPHY_PRBS_CNTL, DP, id),       \
      SRI_ARR(DP_DPHY_SCRAM_CNTL, DP, id), SRI_ARR(DP_DPHY_SYM0, DP, id),      \
      SRI_ARR(DP_DPHY_SYM1, DP, id), SRI_ARR(DP_DPHY_SYM2, DP, id),            \
      SRI_ARR(DP_DPHY_TRAINING_PATTERN_SEL, DP, id),                           \
      SRI_ARR(DP_LINK_CNTL, DP, id), SRI_ARR(DP_LINK_FRAMING_CNTL, DP, id),    \
      SRI_ARR(DP_MSE_SAT0, DP, id), SRI_ARR(DP_MSE_SAT1, DP, id),              \
      SRI_ARR(DP_MSE_SAT2, DP, id), SRI_ARR(DP_MSE_SAT_UPDATE, DP, id),        \
      SRI_ARR(DP_SEC_CNTL, DP, id), SRI_ARR(DP_VID_STREAM_CNTL, DP, id),       \
      SRI_ARR(DP_DPHY_FAST_TRAINING, DP, id), SRI_ARR(DP_SEC_CNTL1, DP, id),   \
      SRI_ARR(DP_DPHY_BS_SR_SWAP_CNTL, DP, id),                                \
      SRI_ARR(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id)                            \
  )

#define LE_DCN31_REG_LIST_RI(id)                                               \
  ( \
  LE_DCN3_REG_LIST_RI(id), SRI_ARR(DP_DPHY_INTERNAL_CTRL, DP, id),             \
      SR_ARR(DIO_LINKA_CNTL, id), SR_ARR(DIO_LINKB_CNTL, id),                  \
      SR_ARR(DIO_LINKC_CNTL, id), SR_ARR(DIO_LINKD_CNTL, id),                  \
      SR_ARR(DIO_LINKE_CNTL, id), SR_ARR(DIO_LINKF_CNTL, id)                   \
  )

#define UNIPHY_DCN2_REG_LIST_RI(id, phyid)                                     \
  ( \
  SRI_ARR_ALPHABET(CLOCK_ENABLE, SYMCLK, id, phyid),                           \
      SRI_ARR_ALPHABET(CHANNEL_XBAR_CNTL, UNIPHY, id, phyid)                   \
  )

/* HPO DP stream encoder */
#define DCN3_1_HPO_DP_STREAM_ENC_REG_LIST_RI(id)                               \
  ( \
  SR_ARR(DP_STREAM_MAPPER_CONTROL0, id),                                       \
      SR_ARR(DP_STREAM_MAPPER_CONTROL1, id),                                   \
      SR_ARR(DP_STREAM_MAPPER_CONTROL2, id),                                   \
      SR_ARR(DP_STREAM_MAPPER_CONTROL3, id),                                   \
      SRI_ARR(DP_STREAM_ENC_CLOCK_CONTROL, DP_STREAM_ENC, id),                 \
      SRI_ARR(DP_STREAM_ENC_INPUT_MUX_CONTROL, DP_STREAM_ENC, id),             \
      SRI_ARR(DP_STREAM_ENC_AUDIO_CONTROL, DP_STREAM_ENC, id),                 \
      SRI_ARR(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, DP_STREAM_ENC, id), \
      SRI_ARR(DP_SYM32_ENC_CONTROL, DP_SYM32_ENC, id),                         \
      SRI_ARR(DP_SYM32_ENC_VID_PIXEL_FORMAT, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_VID_PIXEL_FORMAT_DOUBLE_BUFFER_CONTROL, DP_SYM32_ENC, id), \
      SRI_ARR(DP_SYM32_ENC_VID_MSA0, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA1, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA2, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA3, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA4, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA5, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA6, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA7, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA8, DP_SYM32_ENC, id),                        \
      SRI_ARR(DP_SYM32_ENC_VID_MSA_CONTROL, DP_SYM32_ENC, id),                 \
      SRI_ARR(DP_SYM32_ENC_VID_MSA_DOUBLE_BUFFER_CONTROL, DP_SYM32_ENC, id),   \
      SRI_ARR(DP_SYM32_ENC_VID_FIFO_CONTROL, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_VID_STREAM_CONTROL, DP_SYM32_ENC, id),              \
      SRI_ARR(DP_SYM32_ENC_VID_VBID_CONTROL, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_SDP_CONTROL, DP_SYM32_ENC, id),                     \
      SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL0, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL2, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL3, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL5, DP_SYM32_ENC, id),                \
      SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL11, DP_SYM32_ENC, id),               \
      SRI_ARR(DP_SYM32_ENC_SDP_METADATA_PACKET_CONTROL, DP_SYM32_ENC, id),     \
      SRI_ARR(DP_SYM32_ENC_SDP_AUDIO_CONTROL0, DP_SYM32_ENC, id),              \
      SRI_ARR(DP_SYM32_ENC_VID_CRC_CONTROL, DP_SYM32_ENC, id),                 \
      SRI_ARR(DP_SYM32_ENC_HBLANK_CONTROL, DP_SYM32_ENC, id)                   \
  )

/* HPO DP link encoder regs */
#define DCN3_1_HPO_DP_LINK_ENC_REG_LIST_RI(id)                                 \
  ( \
  SRI_ARR(DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC, id),                         \
      SRI_ARR(DP_DPHY_SYM32_CONTROL, DP_DPHY_SYM32, id),                       \
      SRI_ARR(DP_DPHY_SYM32_STATUS, DP_DPHY_SYM32, id),                        \
      SRI_ARR(DP_DPHY_SYM32_TP_CONFIG, DP_DPHY_SYM32, id),                     \
      SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED0, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED1, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED2, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED3, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_TP_SQ_PULSE, DP_DPHY_SYM32, id),                   \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM0, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM1, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM2, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM3, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM4, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM5, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM6, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM7, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM8, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM9, DP_DPHY_SYM32, id),                    \
      SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM10, DP_DPHY_SYM32, id),                   \
      SRI_ARR(DP_DPHY_SYM32_SAT_VC0, DP_DPHY_SYM32, id),                       \
      SRI_ARR(DP_DPHY_SYM32_SAT_VC1, DP_DPHY_SYM32, id),                       \
      SRI_ARR(DP_DPHY_SYM32_SAT_VC2, DP_DPHY_SYM32, id),                       \
      SRI_ARR(DP_DPHY_SYM32_SAT_VC3, DP_DPHY_SYM32, id),                       \
      SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL0, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL1, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL2, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL3, DP_DPHY_SYM32, id),                 \
      SRI_ARR(DP_DPHY_SYM32_SAT_UPDATE, DP_DPHY_SYM32, id)                     \
  )

/* DPP */
#define DPP_REG_LIST_DCN30_COMMON_RI(id)                                       \
  ( \
  SRI_ARR(CM_DEALPHA, CM, id), SRI_ARR(CM_MEM_PWR_STATUS, CM, id),             \
      SRI_ARR(CM_BIAS_CR_R, CM, id), SRI_ARR(CM_BIAS_Y_G_CB_B, CM, id),        \
      SRI_ARR(PRE_DEGAM, CNVC_CFG, id), SRI_ARR(CM_GAMCOR_CONTROL, CM, id),    \
      SRI_ARR(CM_GAMCOR_LUT_CONTROL, CM, id),                                  \
      SRI_ARR(CM_GAMCOR_LUT_INDEX, CM, id),                                    \
      SRI_ARR(CM_GAMCOR_LUT_INDEX, CM, id),                                    \
      SRI_ARR(CM_GAMCOR_LUT_DATA, CM, id),                                     \
      SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_B, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_G, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_R, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_B, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_G, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_R, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_B, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_B, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_G, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_G, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_R, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_R, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMB_REGION_0_1, CM, id),                              \
      SRI_ARR(CM_GAMCOR_RAMB_REGION_32_33, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMB_OFFSET_B, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMB_OFFSET_G, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMB_OFFSET_R, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_B, CM, id),                       \
      SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_G, CM, id),                       \
      SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_R, CM, id),                       \
      SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_B, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_G, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_R, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_B, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_G, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_R, CM, id),                      \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_B, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_B, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_G, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_G, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_R, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_R, CM, id),                             \
      SRI_ARR(CM_GAMCOR_RAMA_REGION_0_1, CM, id),                              \
      SRI_ARR(CM_GAMCOR_RAMA_REGION_32_33, CM, id),                            \
      SRI_ARR(CM_GAMCOR_RAMA_OFFSET_B, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMA_OFFSET_G, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMA_OFFSET_R, CM, id),                                \
      SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_B, CM, id),                       \
      SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_G, CM, id),                       \
      SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_R, CM, id),                       \
      SRI_ARR(CM_GAMUT_REMAP_CONTROL, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C11_C12, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C13_C14, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C21_C22, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C23_C24, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C31_C32, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_C33_C34, CM, id),                                 \
      SRI_ARR(CM_GAMUT_REMAP_B_C11_C12, CM, id),                               \
      SRI_ARR(CM_GAMUT_REMAP_B_C13_C14, CM, id),                               \
      SRI_ARR(CM_GAMUT_REMAP_B_C21_C22, CM, id),                               \
      SRI_ARR(CM_GAMUT_REMAP_B_C23_C24, CM, id),                               \
      SRI_ARR(CM_GAMUT_REMAP_B_C31_C32, CM, id),                               \
      SRI_ARR(CM_GAMUT_REMAP_B_C33_C34, CM, id),                               \
      SRI_ARR(DSCL_EXT_OVERSCAN_LEFT_RIGHT, DSCL, id),                         \
      SRI_ARR(DSCL_EXT_OVERSCAN_TOP_BOTTOM, DSCL, id),                         \
      SRI_ARR(OTG_H_BLANK, DSCL, id), SRI_ARR(OTG_V_BLANK, DSCL, id),          \
      SRI_ARR(SCL_MODE, DSCL, id), SRI_ARR(LB_DATA_FORMAT, DSCL, id),          \
      SRI_ARR(LB_MEMORY_CTRL, DSCL, id), SRI_ARR(DSCL_AUTOCAL, DSCL, id),      \
      SRI_ARR(DSCL_CONTROL, DSCL, id),                                         \
      SRI_ARR(SCL_TAP_CONTROL, DSCL, id),                                      \
      SRI_ARR(SCL_COEF_RAM_TAP_SELECT, DSCL, id),                              \
      SRI_ARR(SCL_COEF_RAM_TAP_DATA, DSCL, id),                                \
      SRI_ARR(DSCL_2TAP_CONTROL, DSCL, id), SRI_ARR(MPC_SIZE, DSCL, id),       \
      SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO, DSCL, id),                          \
      SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO, DSCL, id),                          \
      SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO_C, DSCL, id),                        \
      SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO_C, DSCL, id),                        \
      SRI_ARR(SCL_HORZ_FILTER_INIT, DSCL, id),                                 \
      SRI_ARR(SCL_HORZ_FILTER_INIT_C, DSCL, id),                               \
      SRI_ARR(SCL_VERT_FILTER_INIT, DSCL, id),                                 \
      SRI_ARR(SCL_VERT_FILTER_INIT_C, DSCL, id),                               \
      SRI_ARR(RECOUT_START, DSCL, id), SRI_ARR(RECOUT_SIZE, DSCL, id),         \
      SRI_ARR(PRE_DEALPHA, CNVC_CFG, id), SRI_ARR(PRE_REALPHA, CNVC_CFG, id),  \
      SRI_ARR(PRE_CSC_MODE, CNVC_CFG, id),                                     \
      SRI_ARR(PRE_CSC_C11_C12, CNVC_CFG, id),                                  \
      SRI_ARR(PRE_CSC_C33_C34, CNVC_CFG, id),                                  \
      SRI_ARR(PRE_CSC_B_C11_C12, CNVC_CFG, id),                                \
      SRI_ARR(PRE_CSC_B_C33_C34, CNVC_CFG, id),                                \
      SRI_ARR(CM_POST_CSC_CONTROL, CM, id),                                    \
      SRI_ARR(CM_POST_CSC_C11_C12, CM, id),                                    \
      SRI_ARR(CM_POST_CSC_C33_C34, CM, id),                                    \
      SRI_ARR(CM_POST_CSC_B_C11_C12, CM, id),                                  \
      SRI_ARR(CM_POST_CSC_B_C33_C34, CM, id),                                  \
      SRI_ARR(CM_MEM_PWR_CTRL, CM, id), SRI_ARR(CM_CONTROL, CM, id),           \
      SRI_ARR(FORMAT_CONTROL, CNVC_CFG, id),                                   \
      SRI_ARR(CNVC_SURFACE_PIXEL_FORMAT, CNVC_CFG, id),                        \
      SRI_ARR(CURSOR0_CONTROL, CNVC_CUR, id),                                  \
      SRI_ARR(CURSOR0_COLOR0, CNVC_CUR, id),                                   \
      SRI_ARR(CURSOR0_COLOR1, CNVC_CUR, id),                                   \
      SRI_ARR(CURSOR0_FP_SCALE_BIAS, CNVC_CUR, id),                            \
      SRI_ARR(DPP_CONTROL, DPP_TOP, id), SRI_ARR(CM_HDR_MULT_COEF, CM, id),    \
      SRI_ARR(CURSOR_CONTROL, CURSOR0_, id),                                   \
      SRI_ARR(ALPHA_2BIT_LUT, CNVC_CFG, id),                                   \
      SRI_ARR(FCNV_FP_BIAS_R, CNVC_CFG, id),                                   \
      SRI_ARR(FCNV_FP_BIAS_G, CNVC_CFG, id),                                   \
      SRI_ARR(FCNV_FP_BIAS_B, CNVC_CFG, id),                                   \
      SRI_ARR(FCNV_FP_SCALE_R, CNVC_CFG, id),                                  \
      SRI_ARR(FCNV_FP_SCALE_G, CNVC_CFG, id),                                  \
      SRI_ARR(FCNV_FP_SCALE_B, CNVC_CFG, id),                                  \
      SRI_ARR(COLOR_KEYER_CONTROL, CNVC_CFG, id),                              \
      SRI_ARR(COLOR_KEYER_ALPHA, CNVC_CFG, id),                                \
      SRI_ARR(COLOR_KEYER_RED, CNVC_CFG, id),                                  \
      SRI_ARR(COLOR_KEYER_GREEN, CNVC_CFG, id),                                \
      SRI_ARR(COLOR_KEYER_BLUE, CNVC_CFG, id),                                 \
      SRI_ARR(CURSOR_CONTROL, CURSOR0_, id),                                   \
      SRI_ARR(OBUF_MEM_PWR_CTRL, DSCL, id),                                    \
      SRI_ARR(DSCL_MEM_PWR_STATUS, DSCL, id),                                  \
      SRI_ARR(DSCL_MEM_PWR_CTRL, DSCL, id)                                     \
  )

/* OPP */
#define OPP_REG_LIST_DCN_RI(id)                                                \
  ( \
  SRI_ARR(FMT_BIT_DEPTH_CONTROL, FMT, id), SRI_ARR(FMT_CONTROL, FMT, id),      \
      SRI_ARR(FMT_DITHER_RAND_R_SEED, FMT, id),                                \
      SRI_ARR(FMT_DITHER_RAND_G_SEED, FMT, id),                                \
      SRI_ARR(FMT_DITHER_RAND_B_SEED, FMT, id),                                \
      SRI_ARR(FMT_CLAMP_CNTL, FMT, id),                                        \
      SRI_ARR(FMT_DYNAMIC_EXP_CNTL, FMT, id),                                  \
      SRI_ARR(FMT_MAP420_MEMORY_CONTROL, FMT, id),                             \
      SRI_ARR(OPPBUF_CONTROL, OPPBUF, id),                                     \
      SRI_ARR(OPPBUF_3D_PARAMETERS_0, OPPBUF, id),                             \
      SRI_ARR(OPPBUF_3D_PARAMETERS_1, OPPBUF, id),                             \
      SRI_ARR(OPP_PIPE_CONTROL, OPP_PIPE, id)                                  \
  )

#define OPP_REG_LIST_DCN10_RI(id) OPP_REG_LIST_DCN_RI(id)

#define OPP_DPG_REG_LIST_RI(id)                                                \
  ( \
  SRI_ARR(DPG_CONTROL, DPG, id), SRI_ARR(DPG_DIMENSIONS, DPG, id),             \
      SRI_ARR(DPG_OFFSET_SEGMENT, DPG, id), SRI_ARR(DPG_COLOUR_B_CB, DPG, id), \
      SRI_ARR(DPG_COLOUR_G_Y, DPG, id), SRI_ARR(DPG_COLOUR_R_CR, DPG, id),     \
      SRI_ARR(DPG_RAMP_CONTROL, DPG, id), SRI_ARR(DPG_STATUS, DPG, id)         \
  )

#define OPP_REG_LIST_DCN30_RI(id)                                              \
  ( \
  OPP_REG_LIST_DCN10_RI(id), OPP_DPG_REG_LIST_RI(id),                          \
      SRI_ARR(FMT_422_CONTROL, FMT, id)                                        \
  )

/* Aux engine regs */
#define AUX_COMMON_REG_LIST0_RI(id)                                            \
  ( \
  SRI_ARR(AUX_CONTROL, DP_AUX, id), SRI_ARR(AUX_ARB_CONTROL, DP_AUX, id),      \
      SRI_ARR(AUX_SW_DATA, DP_AUX, id), SRI_ARR(AUX_SW_CONTROL, DP_AUX, id),   \
      SRI_ARR(AUX_INTERRUPT_CONTROL, DP_AUX, id),                              \
      SRI_ARR(AUX_DPHY_RX_CONTROL1, DP_AUX, id),                               \
      SRI_ARR(AUX_SW_STATUS, DP_AUX, id)                                       \
  )

/* DWBC */
#define DWBC_COMMON_REG_LIST_DCN30_RI(id)                                      \
  ( \
  SR_ARR(DWB_ENABLE_CLK_CTRL, id), SR_ARR(DWB_MEM_PWR_CTRL, id),               \
      SR_ARR(FC_MODE_CTRL, id), SR_ARR(FC_FLOW_CTRL, id),                      \
      SR_ARR(FC_WINDOW_START, id), SR_ARR(FC_WINDOW_SIZE, id),                 \
      SR_ARR(FC_SOURCE_SIZE, id), SR_ARR(DWB_UPDATE_CTRL, id),                 \
      SR_ARR(DWB_CRC_CTRL, id), SR_ARR(DWB_CRC_MASK_R_G, id),                  \
      SR_ARR(DWB_CRC_MASK_B_A, id), SR_ARR(DWB_CRC_VAL_R_G, id),               \
      SR_ARR(DWB_CRC_VAL_B_A, id), SR_ARR(DWB_OUT_CTRL, id),                   \
      SR_ARR(DWB_MMHUBBUB_BACKPRESSURE_CNT_EN, id),                            \
      SR_ARR(DWB_MMHUBBUB_BACKPRESSURE_CNT, id),                               \
      SR_ARR(DWB_HOST_READ_CONTROL, id), SR_ARR(DWB_SOFT_RESET, id),           \
      SR_ARR(DWB_HDR_MULT_COEF, id), SR_ARR(DWB_GAMUT_REMAP_MODE, id),         \
      SR_ARR(DWB_GAMUT_REMAP_COEF_FORMAT, id),                                 \
      SR_ARR(DWB_GAMUT_REMAPA_C11_C12, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPA_C13_C14, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPA_C21_C22, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPA_C23_C24, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPA_C31_C32, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPA_C33_C34, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C11_C12, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C13_C14, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C21_C22, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C23_C24, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C31_C32, id),                                    \
      SR_ARR(DWB_GAMUT_REMAPB_C33_C34, id), SR_ARR(DWB_OGAM_CONTROL, id),      \
      SR_ARR(DWB_OGAM_LUT_INDEX, id), SR_ARR(DWB_OGAM_LUT_DATA, id),           \
      SR_ARR(DWB_OGAM_LUT_CONTROL, id),                                        \
      SR_ARR(DWB_OGAM_RAMA_START_CNTL_B, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_START_CNTL_G, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_START_CNTL_R, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_START_BASE_CNTL_B, id),                             \
      SR_ARR(DWB_OGAM_RAMA_START_SLOPE_CNTL_B, id),                            \
      SR_ARR(DWB_OGAM_RAMA_START_BASE_CNTL_G, id),                             \
      SR_ARR(DWB_OGAM_RAMA_START_SLOPE_CNTL_G, id),                            \
      SR_ARR(DWB_OGAM_RAMA_START_BASE_CNTL_R, id),                             \
      SR_ARR(DWB_OGAM_RAMA_START_SLOPE_CNTL_R, id),                            \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL1_B, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL2_B, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL1_G, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL2_G, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL1_R, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_END_CNTL2_R, id),                                   \
      SR_ARR(DWB_OGAM_RAMA_OFFSET_B, id), SR_ARR(DWB_OGAM_RAMA_OFFSET_G, id),  \
      SR_ARR(DWB_OGAM_RAMA_OFFSET_R, id),                                      \
      SR_ARR(DWB_OGAM_RAMA_REGION_0_1, id),                                    \
      SR_ARR(DWB_OGAM_RAMA_REGION_2_3, id),                                    \
      SR_ARR(DWB_OGAM_RAMA_REGION_4_5, id),                                    \
      SR_ARR(DWB_OGAM_RAMA_REGION_6_7, id),                                    \
      SR_ARR(DWB_OGAM_RAMA_REGION_8_9, id),                                    \
      SR_ARR(DWB_OGAM_RAMA_REGION_10_11, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_12_13, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_14_15, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_16_17, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_18_19, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_20_21, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_22_23, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_24_25, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_26_27, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_28_29, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_30_31, id),                                  \
      SR_ARR(DWB_OGAM_RAMA_REGION_32_33, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_START_CNTL_B, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_START_CNTL_G, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_START_CNTL_R, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_START_BASE_CNTL_B, id),                             \
      SR_ARR(DWB_OGAM_RAMB_START_SLOPE_CNTL_B, id),                            \
      SR_ARR(DWB_OGAM_RAMB_START_BASE_CNTL_G, id),                             \
      SR_ARR(DWB_OGAM_RAMB_START_SLOPE_CNTL_G, id),                            \
      SR_ARR(DWB_OGAM_RAMB_START_BASE_CNTL_R, id),                             \
      SR_ARR(DWB_OGAM_RAMB_START_SLOPE_CNTL_R, id),                            \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL1_B, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL2_B, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL1_G, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL2_G, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL1_R, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_END_CNTL2_R, id),                                   \
      SR_ARR(DWB_OGAM_RAMB_OFFSET_B, id), SR_ARR(DWB_OGAM_RAMB_OFFSET_G, id),  \
      SR_ARR(DWB_OGAM_RAMB_OFFSET_R, id),                                      \
      SR_ARR(DWB_OGAM_RAMB_REGION_0_1, id),                                    \
      SR_ARR(DWB_OGAM_RAMB_REGION_2_3, id),                                    \
      SR_ARR(DWB_OGAM_RAMB_REGION_4_5, id),                                    \
      SR_ARR(DWB_OGAM_RAMB_REGION_6_7, id),                                    \
      SR_ARR(DWB_OGAM_RAMB_REGION_8_9, id),                                    \
      SR_ARR(DWB_OGAM_RAMB_REGION_10_11, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_12_13, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_14_15, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_16_17, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_18_19, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_20_21, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_22_23, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_24_25, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_26_27, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_28_29, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_30_31, id),                                  \
      SR_ARR(DWB_OGAM_RAMB_REGION_32_33, id)                                   \
  )

/* MCIF */

#define MCIF_WB_COMMON_REG_LIST_DCN32_RI(inst)                                 \
  ( \
  SRI2_ARR(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUFMGR_STATUS, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUF_PITCH, MCIF_WB, inst),                              \
      SRI2_ARR(MCIF_WB_BUF_1_STATUS, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_1_STATUS2, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUF_2_STATUS, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_2_STATUS2, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUF_3_STATUS, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_3_STATUS2, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUF_4_STATUS, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_4_STATUS2, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_ARBITRATION_CONTROL, MCIF_WB, inst),                    \
      SRI2_ARR(MCIF_WB_SCLK_CHANGE, MCIF_WB, inst),                            \
      SRI2_ARR(MCIF_WB_TEST_DEBUG_INDEX, MCIF_WB, inst),                       \
      SRI2_ARR(MCIF_WB_TEST_DEBUG_DATA, MCIF_WB, inst),                        \
      SRI2_ARR(MCIF_WB_BUF_1_ADDR_Y, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_1_ADDR_C, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_2_ADDR_Y, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_2_ADDR_C, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_3_ADDR_Y, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_3_ADDR_C, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_4_ADDR_Y, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUF_4_ADDR_C, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB, inst),                     \
      SRI2_ARR(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK, MMHUBBUB, inst),           \
      SRI2_ARR(MCIF_WB_NB_PSTATE_CONTROL, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_WATERMARK, MMHUBBUB, inst),                             \
      SRI2_ARR(MCIF_WB_CLOCK_GATER_CONTROL, MCIF_WB, inst),                    \
      SRI2_ARR(MCIF_WB_SELF_REFRESH_CONTROL, MCIF_WB, inst),                   \
      SRI2_ARR(MULTI_LEVEL_QOS_CTRL, MCIF_WB, inst),                           \
      SRI2_ARR(MCIF_WB_SECURITY_LEVEL, MCIF_WB, inst),                         \
      SRI2_ARR(MCIF_WB_BUF_LUMA_SIZE, MCIF_WB, inst),                          \
      SRI2_ARR(MCIF_WB_BUF_CHROMA_SIZE, MCIF_WB, inst),                        \
      SRI2_ARR(MCIF_WB_BUF_1_ADDR_Y_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_1_ADDR_C_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_2_ADDR_Y_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_2_ADDR_C_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_3_ADDR_Y_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_3_ADDR_C_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_4_ADDR_Y_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_4_ADDR_C_HIGH, MCIF_WB, inst),                      \
      SRI2_ARR(MCIF_WB_BUF_1_RESOLUTION, MCIF_WB, inst),                       \
      SRI2_ARR(MCIF_WB_BUF_2_RESOLUTION, MCIF_WB, inst),                       \
      SRI2_ARR(MCIF_WB_BUF_3_RESOLUTION, MCIF_WB, inst),                       \
      SRI2_ARR(MCIF_WB_BUF_4_RESOLUTION, MCIF_WB, inst),                       \
      SRI2_ARR(MMHUBBUB_MEM_PWR_CNTL, MMHUBBUB, inst),                         \
      SRI2_ARR(MMHUBBUB_WARMUP_ADDR_REGION, MMHUBBUB, inst),                   \
      SRI2_ARR(MMHUBBUB_WARMUP_BASE_ADDR_HIGH, MMHUBBUB, inst),                \
      SRI2_ARR(MMHUBBUB_WARMUP_BASE_ADDR_LOW, MMHUBBUB, inst),                 \
      SRI2_ARR(MMHUBBUB_WARMUP_CONTROL_STATUS, MMHUBBUB, inst)                 \
  )

/* DSC */

#define DSC_REG_LIST_DCN20_RI(id)                                              \
  ( \
  SRI_ARR(DSC_TOP_CONTROL, DSC_TOP, id),                                       \
      SRI_ARR(DSC_DEBUG_CONTROL, DSC_TOP, id),                                 \
      SRI_ARR(DSCC_CONFIG0, DSCC, id), SRI_ARR(DSCC_CONFIG1, DSCC, id),        \
      SRI_ARR(DSCC_STATUS, DSCC, id),                                          \
      SRI_ARR(DSCC_INTERRUPT_CONTROL_STATUS, DSCC, id),                        \
      SRI_ARR(DSCC_PPS_CONFIG0, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG1, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG2, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG3, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG4, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG5, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG6, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG7, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG8, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG9, DSCC, id),                                     \
      SRI_ARR(DSCC_PPS_CONFIG10, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG11, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG12, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG13, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG14, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG15, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG16, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG17, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG18, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG19, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG20, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG21, DSCC, id),                                    \
      SRI_ARR(DSCC_PPS_CONFIG22, DSCC, id),                                    \
      SRI_ARR(DSCC_MEM_POWER_CONTROL, DSCC, id),                               \
      SRI_ARR(DSCC_R_Y_SQUARED_ERROR_LOWER, DSCC, id),                         \
      SRI_ARR(DSCC_R_Y_SQUARED_ERROR_UPPER, DSCC, id),                         \
      SRI_ARR(DSCC_G_CB_SQUARED_ERROR_LOWER, DSCC, id),                        \
      SRI_ARR(DSCC_G_CB_SQUARED_ERROR_UPPER, DSCC, id),                        \
      SRI_ARR(DSCC_B_CR_SQUARED_ERROR_LOWER, DSCC, id),                        \
      SRI_ARR(DSCC_B_CR_SQUARED_ERROR_UPPER, DSCC, id),                        \
      SRI_ARR(DSCC_MAX_ABS_ERROR0, DSCC, id),                                  \
      SRI_ARR(DSCC_MAX_ABS_ERROR1, DSCC, id),                                  \
      SRI_ARR(DSCC_RATE_BUFFER0_MAX_FULLNESS_LEVEL, DSCC, id),                 \
      SRI_ARR(DSCC_RATE_BUFFER1_MAX_FULLNESS_LEVEL, DSCC, id),                 \
      SRI_ARR(DSCC_RATE_BUFFER2_MAX_FULLNESS_LEVEL, DSCC, id),                 \
      SRI_ARR(DSCC_RATE_BUFFER3_MAX_FULLNESS_LEVEL, DSCC, id),                 \
      SRI_ARR(DSCC_RATE_CONTROL_BUFFER0_MAX_FULLNESS_LEVEL, DSCC, id),         \
      SRI_ARR(DSCC_RATE_CONTROL_BUFFER1_MAX_FULLNESS_LEVEL, DSCC, id),         \
      SRI_ARR(DSCC_RATE_CONTROL_BUFFER2_MAX_FULLNESS_LEVEL, DSCC, id),         \
      SRI_ARR(DSCC_RATE_CONTROL_BUFFER3_MAX_FULLNESS_LEVEL, DSCC, id),         \
      SRI_ARR(DSCCIF_CONFIG0, DSCCIF, id),                                     \
      SRI_ARR(DSCCIF_CONFIG1, DSCCIF, id),                                     \
      SRI_ARR(DSCRM_DSC_FORWARD_CONFIG, DSCRM, id)                             \
  )

/* MPC */

#define MPC_DWB_MUX_REG_LIST_DCN3_0_RI(inst)                                   \
  SRII_DWB(DWB_MUX, MUX, MPC_DWB, inst)

#define MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0_RI(inst)                            \
  ( \
  SRII(MUX, MPC_OUT, inst), VUPDATE_SRII(CUR, VUPDATE_LOCK_SET, inst)          \
  )

#define MPC_OUT_MUX_REG_LIST_DCN3_0_RI(inst)                                   \
  ( \
  MPC_OUT_MUX_COMMON_REG_LIST_DCN1_0_RI(inst), SRII(CSC_MODE, MPC_OUT, inst),  \
      SRII(CSC_C11_C12_A, MPC_OUT, inst), SRII(CSC_C33_C34_A, MPC_OUT, inst),  \
      SRII(CSC_C11_C12_B, MPC_OUT, inst), SRII(CSC_C33_C34_B, MPC_OUT, inst),  \
      SRII(DENORM_CONTROL, MPC_OUT, inst),                                     \
      SRII(DENORM_CLAMP_G_Y, MPC_OUT, inst),                                   \
      SRII(DENORM_CLAMP_B_CB, MPC_OUT, inst), SR(MPC_OUT_CSC_COEF_FORMAT)      \
  )

#define MPC_COMMON_REG_LIST_DCN1_0_RI(inst)                                    \
  ( \
  SRII(MPCC_TOP_SEL, MPCC, inst), SRII(MPCC_BOT_SEL, MPCC, inst),              \
      SRII(MPCC_CONTROL, MPCC, inst), SRII(MPCC_STATUS, MPCC, inst),           \
      SRII(MPCC_OPP_ID, MPCC, inst), SRII(MPCC_BG_G_Y, MPCC, inst),            \
      SRII(MPCC_BG_R_CR, MPCC, inst), SRII(MPCC_BG_B_CB, MPCC, inst),          \
      SRII(MPCC_SM_CONTROL, MPCC, inst),                                       \
      SRII(MPCC_UPDATE_LOCK_SEL, MPCC, inst)                                   \
  )

#define MPC_REG_LIST_DCN3_0_RI(inst)                                           \
  ( \
  MPC_COMMON_REG_LIST_DCN1_0_RI(inst), SRII(MPCC_TOP_GAIN, MPCC, inst),        \
      SRII(MPCC_BOT_GAIN_INSIDE, MPCC, inst),                                  \
      SRII(MPCC_BOT_GAIN_OUTSIDE, MPCC, inst),                                 \
      SRII(MPCC_MEM_PWR_CTRL, MPCC, inst),                                     \
      SRII(MPCC_OGAM_LUT_INDEX, MPCC_OGAM, inst),                              \
      SRII(MPCC_OGAM_LUT_DATA, MPCC_OGAM, inst),                               \
      SRII(MPCC_GAMUT_REMAP_COEF_FORMAT, MPCC_OGAM, inst),                     \
      SRII(MPCC_GAMUT_REMAP_MODE, MPCC_OGAM, inst),                            \
      SRII(MPC_GAMUT_REMAP_C11_C12_A, MPCC_OGAM, inst),                        \
      SRII(MPC_GAMUT_REMAP_C33_C34_A, MPCC_OGAM, inst),                        \
      SRII(MPC_GAMUT_REMAP_C11_C12_B, MPCC_OGAM, inst),                        \
      SRII(MPC_GAMUT_REMAP_C33_C34_B, MPCC_OGAM, inst),                        \
      SRII(MPCC_OGAM_RAMA_START_CNTL_B, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMA_START_CNTL_G, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMA_START_CNTL_R, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMA_START_SLOPE_CNTL_B, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMA_START_SLOPE_CNTL_G, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMA_START_SLOPE_CNTL_R, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMA_END_CNTL1_B, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_END_CNTL2_B, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_END_CNTL1_G, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_END_CNTL2_G, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_END_CNTL1_R, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_END_CNTL2_R, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMA_REGION_0_1, MPCC_OGAM, inst),                        \
      SRII(MPCC_OGAM_RAMA_REGION_32_33, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMA_OFFSET_B, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMA_OFFSET_G, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMA_OFFSET_R, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMA_START_BASE_CNTL_B, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_RAMA_START_BASE_CNTL_G, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_RAMA_START_BASE_CNTL_R, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_RAMB_START_CNTL_B, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMB_START_CNTL_G, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMB_START_CNTL_R, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMB_START_SLOPE_CNTL_B, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMB_START_SLOPE_CNTL_G, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMB_START_SLOPE_CNTL_R, MPCC_OGAM, inst),                \
      SRII(MPCC_OGAM_RAMB_END_CNTL1_B, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_END_CNTL2_B, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_END_CNTL1_G, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_END_CNTL2_G, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_END_CNTL1_R, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_END_CNTL2_R, MPCC_OGAM, inst),                       \
      SRII(MPCC_OGAM_RAMB_REGION_0_1, MPCC_OGAM, inst),                        \
      SRII(MPCC_OGAM_RAMB_REGION_32_33, MPCC_OGAM, inst),                      \
      SRII(MPCC_OGAM_RAMB_OFFSET_B, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMB_OFFSET_G, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMB_OFFSET_R, MPCC_OGAM, inst),                          \
      SRII(MPCC_OGAM_RAMB_START_BASE_CNTL_B, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_RAMB_START_BASE_CNTL_G, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_RAMB_START_BASE_CNTL_R, MPCC_OGAM, inst),                 \
      SRII(MPCC_OGAM_CONTROL, MPCC_OGAM, inst),                                \
      SRII(MPCC_OGAM_LUT_CONTROL, MPCC_OGAM, inst)                             \
  )

#define MPC_REG_LIST_DCN3_2_RI(inst) \
	MPC_REG_LIST_DCN3_0_RI(inst),\
	SRII(MPCC_MOVABLE_CM_LOCATION_CONTROL, MPCC, inst),\
	SRII(MPCC_MCM_SHAPER_CONTROL, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_OFFSET_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_OFFSET_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_OFFSET_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_SCALE_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_SCALE_G_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_LUT_INDEX, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_LUT_DATA, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_LUT_WRITE_EN_MASK, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_START_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_START_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_START_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_END_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_END_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_END_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_0_1, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_2_3, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_4_5, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_6_7, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_8_9, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_10_11, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_12_13, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_14_15, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_16_17, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_18_19, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_20_21, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_22_23, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_24_25, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_26_27, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_28_29, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_30_31, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMA_REGION_32_33, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_START_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_START_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_START_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_END_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_END_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_END_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_0_1, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_2_3, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_4_5, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_6_7, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_8_9, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_10_11, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_12_13, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_14_15, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_16_17, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_18_19, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_20_21, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_22_23, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_24_25, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_26_27, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_28_29, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_30_31, MPCC_MCM, inst),\
	SRII(MPCC_MCM_SHAPER_RAMB_REGION_32_33, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_MODE, MPCC_MCM, inst), /*TODO: may need to add other 3DLUT regs*/\
	SRII(MPCC_MCM_3DLUT_INDEX, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_DATA, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_DATA_30BIT, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_READ_WRITE_CONTROL, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_OUT_NORM_FACTOR, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_OUT_OFFSET_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_OUT_OFFSET_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_3DLUT_OUT_OFFSET_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_CONTROL, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_LUT_INDEX, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_LUT_DATA, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_LUT_CONTROL, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL1_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL2_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL1_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL2_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL1_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_END_CNTL2_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_OFFSET_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_OFFSET_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_OFFSET_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_0_1, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_2_3, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_4_5, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_6_7, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_8_9, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_10_11, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_12_13, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_14_15, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_16_17, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_18_19, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_20_21, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_22_23, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_24_25, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_26_27, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_28_29, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_30_31, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMA_REGION_32_33, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_SLOPE_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_BASE_CNTL_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_BASE_CNTL_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_START_BASE_CNTL_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL1_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL2_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL1_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL2_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL1_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_END_CNTL2_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_OFFSET_B, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_OFFSET_G, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_OFFSET_R, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_0_1, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_2_3, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_4_5, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_6_7, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_8_9, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_10_11, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_12_13, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_14_15, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_16_17, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_18_19, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_20_21, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_22_23, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_24_25, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_26_27, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_28_29, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_30_31, MPCC_MCM, inst),\
	SRII(MPCC_MCM_1DLUT_RAMB_REGION_32_33, MPCC_MCM, inst),\
	SRII(MPCC_MCM_MEM_PWR_CTRL, MPCC_MCM, inst)

/* OPTC */

#define OPTC_COMMON_REG_LIST_DCN3_2_RI(inst)                                   \
  ( \
  SRI_ARR(OTG_VSTARTUP_PARAM, OTG, inst),                                      \
      SRI_ARR(OTG_VUPDATE_PARAM, OTG, inst),                                   \
      SRI_ARR(OTG_VREADY_PARAM, OTG, inst),                                    \
      SRI_ARR(OTG_MASTER_UPDATE_LOCK, OTG, inst),                              \
      SRI_ARR(OTG_GLOBAL_CONTROL0, OTG, inst),                                 \
      SRI_ARR(OTG_GLOBAL_CONTROL1, OTG, inst),                                 \
      SRI_ARR(OTG_GLOBAL_CONTROL2, OTG, inst),                                 \
      SRI_ARR(OTG_GLOBAL_CONTROL4, OTG, inst),                                 \
      SRI_ARR(OTG_DOUBLE_BUFFER_CONTROL, OTG, inst),                           \
      SRI_ARR(OTG_H_TOTAL, OTG, inst),                                         \
      SRI_ARR(OTG_H_BLANK_START_END, OTG, inst),                               \
      SRI_ARR(OTG_H_SYNC_A, OTG, inst), SRI_ARR(OTG_H_SYNC_A_CNTL, OTG, inst), \
      SRI_ARR(OTG_H_TIMING_CNTL, OTG, inst), SRI_ARR(OTG_V_TOTAL, OTG, inst),  \
      SRI_ARR(OTG_V_BLANK_START_END, OTG, inst),                               \
      SRI_ARR(OTG_V_SYNC_A, OTG, inst), SRI_ARR(OTG_V_SYNC_A_CNTL, OTG, inst), \
      SRI_ARR(OTG_CONTROL, OTG, inst), SRI_ARR(OTG_STEREO_CONTROL, OTG, inst), \
      SRI_ARR(OTG_3D_STRUCTURE_CONTROL, OTG, inst),                            \
      SRI_ARR(OTG_STEREO_STATUS, OTG, inst),                                   \
      SRI_ARR(OTG_V_TOTAL_MAX, OTG, inst),                                     \
      SRI_ARR(OTG_V_TOTAL_MIN, OTG, inst),                                     \
      SRI_ARR(OTG_V_TOTAL_CONTROL, OTG, inst),                                 \
      SRI_ARR(OTG_TRIGA_CNTL, OTG, inst),                                      \
      SRI_ARR(OTG_FORCE_COUNT_NOW_CNTL, OTG, inst),                            \
      SRI_ARR(OTG_STATIC_SCREEN_CONTROL, OTG, inst),                           \
      SRI_ARR(OTG_STATUS_FRAME_COUNT, OTG, inst),                              \
      SRI_ARR(OTG_STATUS, OTG, inst), SRI_ARR(OTG_STATUS_POSITION, OTG, inst), \
      SRI_ARR(OTG_NOM_VERT_POSITION, OTG, inst),                               \
      SRI_ARR(OTG_M_CONST_DTO0, OTG, inst),                                    \
      SRI_ARR(OTG_M_CONST_DTO1, OTG, inst),                                    \
      SRI_ARR(OTG_CLOCK_CONTROL, OTG, inst),                                   \
      SRI_ARR(OTG_VERTICAL_INTERRUPT0_CONTROL, OTG, inst),                     \
      SRI_ARR(OTG_VERTICAL_INTERRUPT0_POSITION, OTG, inst),                    \
      SRI_ARR(OTG_VERTICAL_INTERRUPT1_CONTROL, OTG, inst),                     \
      SRI_ARR(OTG_VERTICAL_INTERRUPT1_POSITION, OTG, inst),                    \
      SRI_ARR(OTG_VERTICAL_INTERRUPT2_CONTROL, OTG, inst),                     \
      SRI_ARR(OTG_VERTICAL_INTERRUPT2_POSITION, OTG, inst),                    \
      SRI_ARR(OPTC_INPUT_CLOCK_CONTROL, ODM, inst),                            \
      SRI_ARR(OPTC_DATA_SOURCE_SELECT, ODM, inst),                             \
      SRI_ARR(OPTC_INPUT_GLOBAL_CONTROL, ODM, inst),                           \
      SRI_ARR(CONTROL, VTG, inst), SRI_ARR(OTG_VERT_SYNC_CONTROL, OTG, inst),  \
      SRI_ARR(OTG_GSL_CONTROL, OTG, inst), SRI_ARR(OTG_CRC_CNTL, OTG, inst),   \
      SRI_ARR(OTG_CRC0_DATA_RG, OTG, inst),                                    \
      SRI_ARR(OTG_CRC0_DATA_B, OTG, inst),                                     \
      SRI_ARR(OTG_CRC0_WINDOWA_X_CONTROL, OTG, inst),                          \
      SRI_ARR(OTG_CRC0_WINDOWA_Y_CONTROL, OTG, inst),                          \
      SRI_ARR(OTG_CRC0_WINDOWB_X_CONTROL, OTG, inst),                          \
      SRI_ARR(OTG_CRC0_WINDOWB_Y_CONTROL, OTG, inst),                          \
      SR_ARR(GSL_SOURCE_SELECT, inst),                                         \
      SRI_ARR(OTG_TRIGA_MANUAL_TRIG, OTG, inst),                               \
      SRI_ARR(OTG_GLOBAL_CONTROL1, OTG, inst),                                 \
      SRI_ARR(OTG_GLOBAL_CONTROL2, OTG, inst),                                 \
      SRI_ARR(OTG_GSL_WINDOW_X, OTG, inst),                                    \
      SRI_ARR(OTG_GSL_WINDOW_Y, OTG, inst),                                    \
      SRI_ARR(OTG_VUPDATE_KEEPOUT, OTG, inst),                                 \
      SRI_ARR(OTG_DSC_START_POSITION, OTG, inst),                              \
      SRI_ARR(OTG_DRR_TRIGGER_WINDOW, OTG, inst),                              \
      SRI_ARR(OTG_DRR_V_TOTAL_CHANGE, OTG, inst),                              \
      SRI_ARR(OPTC_DATA_FORMAT_CONTROL, ODM, inst),                            \
      SRI_ARR(OPTC_BYTES_PER_PIXEL, ODM, inst),                                \
      SRI_ARR(OPTC_WIDTH_CONTROL, ODM, inst),                                  \
      SRI_ARR(OPTC_MEMORY_CONFIG, ODM, inst),                                  \
      SRI_ARR(OTG_DRR_CONTROL, OTG, inst)                                      \
  )

/* HUBP */

#define HUBP_REG_LIST_DCN_VM_RI(id)                                            \
  ( \
  SRI_ARR(NOM_PARAMETERS_0, HUBPREQ, id),                                      \
      SRI_ARR(NOM_PARAMETERS_1, HUBPREQ, id),                                  \
      SRI_ARR(NOM_PARAMETERS_2, HUBPREQ, id),                                  \
      SRI_ARR(NOM_PARAMETERS_3, HUBPREQ, id),                                  \
      SRI_ARR(DCN_VM_MX_L1_TLB_CNTL, HUBPREQ, id)                              \
  )

#define HUBP_REG_LIST_DCN_RI(id)                                               \
  ( \
  SRI_ARR(DCHUBP_CNTL, HUBP, id), SRI_ARR(HUBPREQ_DEBUG_DB, HUBP, id),         \
      SRI_ARR(HUBPREQ_DEBUG, HUBP, id), SRI_ARR(DCSURF_ADDR_CONFIG, HUBP, id), \
      SRI_ARR(DCSURF_TILING_CONFIG, HUBP, id),                                 \
      SRI_ARR(DCSURF_SURFACE_PITCH, HUBPREQ, id),                              \
      SRI_ARR(DCSURF_SURFACE_PITCH_C, HUBPREQ, id),                            \
      SRI_ARR(DCSURF_SURFACE_CONFIG, HUBP, id),                                \
      SRI_ARR(DCSURF_FLIP_CONTROL, HUBPREQ, id),                               \
      SRI_ARR(DCSURF_PRI_VIEWPORT_DIMENSION, HUBP, id),                        \
      SRI_ARR(DCSURF_PRI_VIEWPORT_START, HUBP, id),                            \
      SRI_ARR(DCSURF_SEC_VIEWPORT_DIMENSION, HUBP, id),                        \
      SRI_ARR(DCSURF_SEC_VIEWPORT_START, HUBP, id),                            \
      SRI_ARR(DCSURF_PRI_VIEWPORT_DIMENSION_C, HUBP, id),                      \
      SRI_ARR(DCSURF_PRI_VIEWPORT_START_C, HUBP, id),                          \
      SRI_ARR(DCSURF_SEC_VIEWPORT_DIMENSION_C, HUBP, id),                      \
      SRI_ARR(DCSURF_SEC_VIEWPORT_START_C, HUBP, id),                          \
      SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, HUBPREQ, id),               \
      SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS, HUBPREQ, id),                    \
      SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, HUBPREQ, id),             \
      SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS, HUBPREQ, id),                  \
      SRI_ARR(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, HUBPREQ, id),          \
      SRI_ARR(DCSURF_PRIMARY_META_SURFACE_ADDRESS, HUBPREQ, id),               \
      SRI_ARR(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH, HUBPREQ, id),        \
      SRI_ARR(DCSURF_SECONDARY_META_SURFACE_ADDRESS, HUBPREQ, id),             \
      SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),             \
      SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS_C, HUBPREQ, id),                  \
      SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),           \
      SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS_C, HUBPREQ, id),                \
      SRI_ARR(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),        \
      SRI_ARR(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, HUBPREQ, id),             \
      SRI_ARR(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),      \
      SRI_ARR(DCSURF_SECONDARY_META_SURFACE_ADDRESS_C, HUBPREQ, id),           \
      SRI_ARR(DCSURF_SURFACE_INUSE, HUBPREQ, id),                              \
      SRI_ARR(DCSURF_SURFACE_INUSE_HIGH, HUBPREQ, id),                         \
      SRI_ARR(DCSURF_SURFACE_INUSE_C, HUBPREQ, id),                            \
      SRI_ARR(DCSURF_SURFACE_INUSE_HIGH_C, HUBPREQ, id),                       \
      SRI_ARR(DCSURF_SURFACE_EARLIEST_INUSE, HUBPREQ, id),                     \
      SRI_ARR(DCSURF_SURFACE_EARLIEST_INUSE_HIGH, HUBPREQ, id),                \
      SRI_ARR(DCSURF_SURFACE_EARLIEST_INUSE_C, HUBPREQ, id),                   \
      SRI_ARR(DCSURF_SURFACE_EARLIEST_INUSE_HIGH_C, HUBPREQ, id),              \
      SRI_ARR(DCSURF_SURFACE_CONTROL, HUBPREQ, id),                            \
      SRI_ARR(DCSURF_SURFACE_FLIP_INTERRUPT, HUBPREQ, id),                     \
      SRI_ARR(HUBPRET_CONTROL, HUBPRET, id),                                   \
      SRI_ARR(HUBPRET_READ_LINE_STATUS, HUBPRET, id),                          \
      SRI_ARR(DCN_EXPANSION_MODE, HUBPREQ, id),                                \
      SRI_ARR(DCHUBP_REQ_SIZE_CONFIG, HUBP, id),                               \
      SRI_ARR(DCHUBP_REQ_SIZE_CONFIG_C, HUBP, id),                             \
      SRI_ARR(BLANK_OFFSET_0, HUBPREQ, id),                                    \
      SRI_ARR(BLANK_OFFSET_1, HUBPREQ, id),                                    \
      SRI_ARR(DST_DIMENSIONS, HUBPREQ, id),                                    \
      SRI_ARR(DST_AFTER_SCALER, HUBPREQ, id),                                  \
      SRI_ARR(VBLANK_PARAMETERS_0, HUBPREQ, id),                               \
      SRI_ARR(REF_FREQ_TO_PIX_FREQ, HUBPREQ, id),                              \
      SRI_ARR(VBLANK_PARAMETERS_1, HUBPREQ, id),                               \
      SRI_ARR(VBLANK_PARAMETERS_3, HUBPREQ, id),                               \
      SRI_ARR(NOM_PARAMETERS_4, HUBPREQ, id),                                  \
      SRI_ARR(NOM_PARAMETERS_5, HUBPREQ, id),                                  \
      SRI_ARR(PER_LINE_DELIVERY_PRE, HUBPREQ, id),                             \
      SRI_ARR(PER_LINE_DELIVERY, HUBPREQ, id),                                 \
      SRI_ARR(VBLANK_PARAMETERS_2, HUBPREQ, id),                               \
      SRI_ARR(VBLANK_PARAMETERS_4, HUBPREQ, id),                               \
      SRI_ARR(NOM_PARAMETERS_6, HUBPREQ, id),                                  \
      SRI_ARR(NOM_PARAMETERS_7, HUBPREQ, id),                                  \
      SRI_ARR(DCN_TTU_QOS_WM, HUBPREQ, id),                                    \
      SRI_ARR(DCN_GLOBAL_TTU_CNTL, HUBPREQ, id),                               \
      SRI_ARR(DCN_SURF0_TTU_CNTL0, HUBPREQ, id),                               \
      SRI_ARR(DCN_SURF0_TTU_CNTL1, HUBPREQ, id),                               \
      SRI_ARR(DCN_SURF1_TTU_CNTL0, HUBPREQ, id),                               \
      SRI_ARR(DCN_SURF1_TTU_CNTL1, HUBPREQ, id),                               \
      SRI_ARR(DCN_CUR0_TTU_CNTL0, HUBPREQ, id),                                \
      SRI_ARR(DCN_CUR0_TTU_CNTL1, HUBPREQ, id),                                \
      SRI_ARR(HUBP_CLK_CNTL, HUBP, id)                                         \
  )

#define HUBP_REG_LIST_DCN2_COMMON_RI(id)                                       \
  ( \
  HUBP_REG_LIST_DCN_RI(id), HUBP_REG_LIST_DCN_VM_RI(id),                       \
      SRI_ARR(PREFETCH_SETTINGS, HUBPREQ, id),                                 \
      SRI_ARR(PREFETCH_SETTINGS_C, HUBPREQ, id),                               \
      SRI_ARR(DCN_VM_SYSTEM_APERTURE_LOW_ADDR, HUBPREQ, id),                   \
      SRI_ARR(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR, HUBPREQ, id),                  \
      SRI_ARR(CURSOR_SETTINGS, HUBPREQ, id),                                   \
      SRI_ARR(CURSOR_SURFACE_ADDRESS_HIGH, CURSOR0_, id),                      \
      SRI_ARR(CURSOR_SURFACE_ADDRESS, CURSOR0_, id),                           \
      SRI_ARR(CURSOR_SIZE, CURSOR0_, id),                                      \
      SRI_ARR(CURSOR_CONTROL, CURSOR0_, id),                                   \
      SRI_ARR(CURSOR_POSITION, CURSOR0_, id),                                  \
      SRI_ARR(CURSOR_HOT_SPOT, CURSOR0_, id),                                  \
      SRI_ARR(CURSOR_DST_OFFSET, CURSOR0_, id),                                \
      SRI_ARR(DMDATA_ADDRESS_HIGH, CURSOR0_, id),                              \
      SRI_ARR(DMDATA_ADDRESS_LOW, CURSOR0_, id),                               \
      SRI_ARR(DMDATA_CNTL, CURSOR0_, id),                                      \
      SRI_ARR(DMDATA_SW_CNTL, CURSOR0_, id),                                   \
      SRI_ARR(DMDATA_QOS_CNTL, CURSOR0_, id),                                  \
      SRI_ARR(DMDATA_SW_DATA, CURSOR0_, id),                                   \
      SRI_ARR(DMDATA_STATUS, CURSOR0_, id),                                    \
      SRI_ARR(FLIP_PARAMETERS_0, HUBPREQ, id),                                 \
      SRI_ARR(FLIP_PARAMETERS_1, HUBPREQ, id),                                 \
      SRI_ARR(FLIP_PARAMETERS_2, HUBPREQ, id),                                 \
      SRI_ARR(DCN_CUR1_TTU_CNTL0, HUBPREQ, id),                                \
      SRI_ARR(DCN_CUR1_TTU_CNTL1, HUBPREQ, id),                                \
      SRI_ARR(DCSURF_FLIP_CONTROL2, HUBPREQ, id),                              \
      SRI_ARR(VMID_SETTINGS_0, HUBPREQ, id)                                    \
  )

#define HUBP_REG_LIST_DCN21_RI(id)                                             \
  ( \
  HUBP_REG_LIST_DCN2_COMMON_RI(id), SRI_ARR(FLIP_PARAMETERS_3, HUBPREQ, id),   \
      SRI_ARR(FLIP_PARAMETERS_4, HUBPREQ, id),                                 \
      SRI_ARR(FLIP_PARAMETERS_5, HUBPREQ, id),                                 \
      SRI_ARR(FLIP_PARAMETERS_6, HUBPREQ, id),                                 \
      SRI_ARR(VBLANK_PARAMETERS_5, HUBPREQ, id),                               \
      SRI_ARR(VBLANK_PARAMETERS_6, HUBPREQ, id)                                \
  )

#define HUBP_REG_LIST_DCN30_RI(id)                                             \
  ( \
  HUBP_REG_LIST_DCN21_RI(id), SRI_ARR(DCN_DMDATA_VM_CNTL, HUBPREQ, id)         \
  )

#define HUBP_REG_LIST_DCN32_RI(id)                                             \
  ( \
  HUBP_REG_LIST_DCN30_RI(id), SRI_ARR(DCHUBP_MALL_CONFIG, HUBP, id),           \
      SRI_ARR(DCHUBP_VMPG_CONFIG, HUBP, id),                                   \
      SRI_ARR(UCLK_PSTATE_FORCE, HUBPREQ, id)                                  \
  )

/* HUBBUB */

#define HUBBUB_REG_LIST_DCN32_RI(id)                                           \
  ( \
  SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A),                                   \
      SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B),                               \
      SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C),                               \
      SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D),                               \
      SR(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL),                                  \
      SR(DCHUBBUB_ARB_DRAM_STATE_CNTL), SR(DCHUBBUB_ARB_SAT_LEVEL),            \
      SR(DCHUBBUB_ARB_DF_REQ_OUTSTAND), SR(DCHUBBUB_GLOBAL_TIMER_CNTL),        \
      SR(DCHUBBUB_SOFT_RESET), SR(DCHUBBUB_CRC_CTRL),                          \
      SR(DCN_VM_FB_LOCATION_BASE), SR(DCN_VM_FB_LOCATION_TOP),                 \
      SR(DCN_VM_FB_OFFSET), SR(DCN_VM_AGP_BOT), SR(DCN_VM_AGP_TOP),            \
      SR(DCN_VM_AGP_BASE), HUBBUB_SR_WATERMARK_REG_LIST(),                     \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A), SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B),  \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_C), SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_D),  \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A),                                     \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B),                                     \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_C),                                     \
      SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_D),                                     \
      SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A),                            \
      SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B),                            \
      SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_C),                            \
      SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_D), SR(DCHUBBUB_DET0_CTRL),    \
      SR(DCHUBBUB_DET1_CTRL), SR(DCHUBBUB_DET2_CTRL), SR(DCHUBBUB_DET3_CTRL),  \
      SR(DCHUBBUB_COMPBUF_CTRL), SR(COMPBUF_RESERVED_SPACE),                   \
      SR(DCHUBBUB_DEBUG_CTRL_0),                                               \
      SR(DCHUBBUB_ARB_USR_RETRAINING_CNTL),                                    \
      SR(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_A),                             \
      SR(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_B),                             \
      SR(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_C),                             \
      SR(DCHUBBUB_ARB_USR_RETRAINING_WATERMARK_D),                             \
      SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A),                         \
      SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B),                         \
      SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_C),                         \
      SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_D),                         \
      SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A),                         \
      SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B),                         \
      SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_C),                         \
      SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_D),                         \
      SR(DCN_VM_FAULT_ADDR_MSB), SR(DCN_VM_FAULT_ADDR_LSB),                    \
      SR(DCN_VM_FAULT_CNTL), SR(DCN_VM_FAULT_STATUS),                          \
      SR(SDPIF_REQUEST_RATE_LIMIT)                                             \
  )

/* DCCG */

#define DCCG_REG_LIST_DCN32_RI()                                               \
  ( \
  SR(DPPCLK_DTO_CTRL), DCCG_SRII(DTO_PARAM, DPPCLK, 0),                        \
      DCCG_SRII(DTO_PARAM, DPPCLK, 1), DCCG_SRII(DTO_PARAM, DPPCLK, 2),        \
      DCCG_SRII(DTO_PARAM, DPPCLK, 3), DCCG_SRII(CLOCK_CNTL, HDMICHARCLK, 0),  \
      SR(PHYASYMCLK_CLOCK_CNTL), SR(PHYBSYMCLK_CLOCK_CNTL),                    \
      SR(PHYCSYMCLK_CLOCK_CNTL), SR(PHYDSYMCLK_CLOCK_CNTL),                    \
      SR(PHYESYMCLK_CLOCK_CNTL), SR(DPSTREAMCLK_CNTL), SR(HDMISTREAMCLK_CNTL), \
      SR(SYMCLK32_SE_CNTL), SR(SYMCLK32_LE_CNTL),                              \
      DCCG_SRII(PIXEL_RATE_CNTL, OTG, 0), DCCG_SRII(PIXEL_RATE_CNTL, OTG, 1),  \
      DCCG_SRII(PIXEL_RATE_CNTL, OTG, 2), DCCG_SRII(PIXEL_RATE_CNTL, OTG, 3),  \
      DCCG_SRII(MODULO, DTBCLK_DTO, 0), DCCG_SRII(MODULO, DTBCLK_DTO, 1),      \
      DCCG_SRII(MODULO, DTBCLK_DTO, 2), DCCG_SRII(MODULO, DTBCLK_DTO, 3),      \
      DCCG_SRII(PHASE, DTBCLK_DTO, 0), DCCG_SRII(PHASE, DTBCLK_DTO, 1),        \
      DCCG_SRII(PHASE, DTBCLK_DTO, 2), DCCG_SRII(PHASE, DTBCLK_DTO, 3),        \
      SR(DCCG_AUDIO_DTBCLK_DTO_MODULO), SR(DCCG_AUDIO_DTBCLK_DTO_PHASE),       \
      SR(OTG_PIXEL_RATE_DIV), SR(DTBCLK_P_CNTL),                               \
      SR(DCCG_AUDIO_DTO_SOURCE), SR(DENTIST_DISPCLK_CNTL)                      \
  )

/* VMID */
#define DCN20_VMID_REG_LIST_RI(id)                                             \
  ( \
  SRI_ARR(CNTL, DCN_VM_CONTEXT, id),                                           \
      SRI_ARR(PAGE_TABLE_BASE_ADDR_HI32, DCN_VM_CONTEXT, id),                  \
      SRI_ARR(PAGE_TABLE_BASE_ADDR_LO32, DCN_VM_CONTEXT, id),                  \
      SRI_ARR(PAGE_TABLE_START_ADDR_HI32, DCN_VM_CONTEXT, id),                 \
      SRI_ARR(PAGE_TABLE_START_ADDR_LO32, DCN_VM_CONTEXT, id),                 \
      SRI_ARR(PAGE_TABLE_END_ADDR_HI32, DCN_VM_CONTEXT, id),                   \
      SRI_ARR(PAGE_TABLE_END_ADDR_LO32, DCN_VM_CONTEXT, id)                    \
  )

/* I2C HW */

#define I2C_HW_ENGINE_COMMON_REG_LIST_RI(id)                                   \
  ( \
      SRI_ARR_I2C(SETUP, DC_I2C_DDC, id), SRI_ARR_I2C(SPEED, DC_I2C_DDC, id),  \
      SRI_ARR_I2C(HW_STATUS, DC_I2C_DDC, id),                                  \
      SR_ARR_I2C(DC_I2C_ARBITRATION, id),                                      \
      SR_ARR_I2C(DC_I2C_CONTROL, id), SR_ARR_I2C(DC_I2C_SW_STATUS, id),        \
      SR_ARR_I2C(DC_I2C_TRANSACTION0, id), SR_ARR_I2C(DC_I2C_TRANSACTION1, id),\
      SR_ARR_I2C(DC_I2C_TRANSACTION2, id), SR_ARR_I2C(DC_I2C_TRANSACTION3, id),\
      SR_ARR_I2C(DC_I2C_DATA, id), SR_ARR_I2C(MICROSECOND_TIME_BASE_DIV, id)          \
  )

#define I2C_HW_ENGINE_COMMON_REG_LIST_DCN30_RI(id)                             \
  ( \
      I2C_HW_ENGINE_COMMON_REG_LIST_RI(id), SR_ARR_I2C(DIO_MEM_PWR_CTRL, id),  \
      SR_ARR_I2C(DIO_MEM_PWR_STATUS, id)                                           \
  )

#endif /* _DCN32_RESOURCE_H_ */
