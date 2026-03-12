/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef _DCN42_RESOURCE_H_
#define _DCN42_RESOURCE_H_

#include "core_types.h"

#define TO_DCN42_RES_POOL(pool) \
	container_of(pool, struct dcn42_resource_pool, base)

/* DPP */
#define DPP_REG_LIST_DCN42_COMMON_RI(id)                                        \
	SRI_ARR(CM_DEALPHA, CM, id), SRI_ARR(CM_MEM_PWR_STATUS, CM, id),            \
		SRI_ARR(CM_BIAS_CR_R, CM, id), SRI_ARR(CM_BIAS_Y_G_CB_B, CM, id),       \
		SRI_ARR(PRE_DEGAM, CNVC_CFG, id), SRI_ARR(CM_GAMCOR_CONTROL, CM, id),   \
		SRI_ARR(CM_GAMCOR_LUT_CONTROL, CM, id),                                 \
		SRI_ARR(CM_GAMCOR_LUT_INDEX, CM, id),                                   \
		SRI_ARR(CM_GAMCOR_LUT_INDEX, CM, id),                                   \
		SRI_ARR(CM_GAMCOR_LUT_DATA, CM, id),                                    \
		SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_B, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_G, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMB_START_CNTL_R, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_B, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_G, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMB_START_SLOPE_CNTL_R, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_B, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_B, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_G, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_G, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL1_R, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_END_CNTL2_R, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMB_REGION_0_1, CM, id),                             \
		SRI_ARR(CM_GAMCOR_RAMB_REGION_32_33, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMB_OFFSET_B, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMB_OFFSET_G, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMB_OFFSET_R, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_B, CM, id),                      \
		SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_G, CM, id),                      \
		SRI_ARR(CM_GAMCOR_RAMB_START_BASE_CNTL_R, CM, id),                      \
		SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_B, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_G, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMA_START_CNTL_R, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_B, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_G, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMA_START_SLOPE_CNTL_R, CM, id),                     \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_B, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_B, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_G, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_G, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL1_R, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_END_CNTL2_R, CM, id),                            \
		SRI_ARR(CM_GAMCOR_RAMA_REGION_0_1, CM, id),                             \
		SRI_ARR(CM_GAMCOR_RAMA_REGION_32_33, CM, id),                           \
		SRI_ARR(CM_GAMCOR_RAMA_OFFSET_B, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMA_OFFSET_G, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMA_OFFSET_R, CM, id),                               \
		SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_B, CM, id),                      \
		SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_G, CM, id),                      \
		SRI_ARR(CM_GAMCOR_RAMA_START_BASE_CNTL_R, CM, id),                      \
		SRI_ARR(CM_HIST_CNTL, CM, id),                                          \
		SRI_ARR(CM_HIST_LOCK, CM, id),                                          \
		SRI_ARR(CM_HIST_INDEX, CM, id),                                         \
		SRI_ARR(CM_HIST_DATA, CM, id),                                          \
		SRI_ARR(CM_HIST_STATUS, CM, id),                                        \
		SRI_ARR(CM_HIST_SCALE_SRC1, CM, id),                                    \
		SRI_ARR(CM_HIST_COEFA_SRC2, CM, id),                                    \
		SRI_ARR(CM_HIST_COEFB_SRC2, CM, id),                                    \
		SRI_ARR(CM_HIST_COEFC_SRC2, CM, id),                                    \
		SRI_ARR(CM_HIST_SCALE_SRC3, CM, id),                                    \
		SRI_ARR(CM_HIST_BIAS_SRC1, CM, id),                                     \
		SRI_ARR(CM_HIST_BIAS_SRC2, CM, id),                                     \
		SRI_ARR(CM_HIST_BIAS_SRC3, CM, id),                                     \
		SRI_ARR(DSCL_EXT_OVERSCAN_LEFT_RIGHT, DSCL, id),                        \
		SRI_ARR(DSCL_EXT_OVERSCAN_TOP_BOTTOM, DSCL, id),                        \
		SRI_ARR(OTG_H_BLANK, DSCL, id), SRI_ARR(OTG_V_BLANK, DSCL, id),         \
		SRI_ARR(SCL_MODE, DSCL, id), SRI_ARR(LB_DATA_FORMAT, DSCL, id),         \
		SRI_ARR(LB_MEMORY_CTRL, DSCL, id), SRI_ARR(DSCL_AUTOCAL, DSCL, id),     \
		SRI_ARR(SCL_TAP_CONTROL, DSCL, id),                                     \
		SRI_ARR(SCL_COEF_RAM_TAP_SELECT, DSCL, id),                             \
		SRI_ARR(SCL_COEF_RAM_TAP_DATA, DSCL, id),                               \
		SRI_ARR(DSCL_2TAP_CONTROL, DSCL, id), SRI_ARR(MPC_SIZE, DSCL, id),      \
		SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO, DSCL, id),                         \
		SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO, DSCL, id),                         \
		SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO_C, DSCL, id),                       \
		SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO_C, DSCL, id),                       \
		SRI_ARR(SCL_HORZ_FILTER_INIT, DSCL, id),                                \
		SRI_ARR(SCL_HORZ_FILTER_INIT_C, DSCL, id),                              \
		SRI_ARR(SCL_VERT_FILTER_INIT, DSCL, id),                                \
		SRI_ARR(SCL_VERT_FILTER_INIT_C, DSCL, id),                              \
		SRI_ARR(RECOUT_START, DSCL, id), SRI_ARR(RECOUT_SIZE, DSCL, id),        \
		SRI_ARR(PRE_DEALPHA, CNVC_CFG, id), SRI_ARR(PRE_REALPHA, CNVC_CFG, id), \
		SRI_ARR(PRE_CSC_MODE, CNVC_CFG, id),                                    \
		SRI_ARR(PRE_CSC_C11_C12, CNVC_CFG, id),                                 \
		SRI_ARR(PRE_CSC_C33_C34, CNVC_CFG, id),                                 \
		SRI_ARR(PRE_CSC_B_C11_C12, CNVC_CFG, id),                               \
		SRI_ARR(PRE_CSC_B_C33_C34, CNVC_CFG, id),                               \
		SRI_ARR(CM_POST_CSC_CONTROL, CM, id),                                   \
		SRI_ARR(CM_POST_CSC_C11_C12, CM, id),                                   \
		SRI_ARR(CM_POST_CSC_C33_C34, CM, id),                                   \
		SRI_ARR(CM_POST_CSC_B_C11_C12, CM, id),                                 \
		SRI_ARR(CM_POST_CSC_B_C33_C34, CM, id),                                 \
		SRI_ARR(CM_MEM_PWR_CTRL, CM, id), SRI_ARR(CM_CONTROL, CM, id),          \
		SRI_ARR(CM_TEST_DEBUG_INDEX, CM, id),                                   \
		SRI_ARR(CM_TEST_DEBUG_DATA, CM, id),                                    \
		SRI_ARR(FORMAT_CONTROL, CNVC_CFG, id),                                  \
		SRI_ARR(CNVC_SURFACE_PIXEL_FORMAT, CNVC_CFG, id),                       \
		SRI_ARR(CURSOR0_CONTROL, CM_CUR, id),                                   \
		SRI_ARR(CURSOR0_COLOR0, CM_CUR, id),                                    \
		SRI_ARR(CURSOR0_COLOR1, CM_CUR, id),                                    \
		SRI_ARR(CURSOR0_FP_SCALE_BIAS_G_Y, CM_CUR, id),                         \
		SRI_ARR(CURSOR0_FP_SCALE_BIAS_RB_CRCB, CM_CUR, id),                     \
		SRI_ARR(CUR0_MATRIX_MODE, CM_CUR, id),                                  \
		SRI_ARR(CUR0_MATRIX_C11_C12_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C13_C14_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C21_C22_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C23_C24_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C31_C32_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C33_C34_A, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C11_C12_B, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C13_C14_B, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C21_C22_B, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C23_C24_B, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C31_C32_B, CM_CUR, id),                             \
		SRI_ARR(CUR0_MATRIX_C33_C34_B, CM_CUR, id),                             \
		SRI_ARR(DPP_CONTROL, DPP_TOP, id), SRI_ARR(CM_HDR_MULT_COEF, CM, id),   \
		SRI_ARR(CURSOR_CONTROL, CURSOR0_, id),                                  \
		SRI_ARR(ALPHA_2BIT_LUT01, CNVC_CFG, id),                                \
		SRI_ARR(ALPHA_2BIT_LUT23, CNVC_CFG, id),                                \
		SRI_ARR(FCNV_FP_BIAS_R, CNVC_CFG, id),                                  \
		SRI_ARR(FCNV_FP_BIAS_G, CNVC_CFG, id),                                  \
		SRI_ARR(FCNV_FP_BIAS_B, CNVC_CFG, id),                                  \
		SRI_ARR(FCNV_FP_SCALE_R, CNVC_CFG, id),                                 \
		SRI_ARR(FCNV_FP_SCALE_G, CNVC_CFG, id),                                 \
		SRI_ARR(FCNV_FP_SCALE_B, CNVC_CFG, id),                                 \
		SRI_ARR(COLOR_KEYER_CONTROL, CNVC_CFG, id),                             \
		SRI_ARR(COLOR_KEYER_ALPHA, CNVC_CFG, id),                               \
		SRI_ARR(COLOR_KEYER_RED, CNVC_CFG, id),                                 \
		SRI_ARR(COLOR_KEYER_GREEN, CNVC_CFG, id),                               \
		SRI_ARR(COLOR_KEYER_BLUE, CNVC_CFG, id),                                \
		SRI_ARR(OBUF_MEM_PWR_CTRL, DSCL, id),                                   \
		SRI_ARR(DSCL_MEM_PWR_STATUS, DSCL, id),                                 \
		SRI_ARR(DSCL_MEM_PWR_CTRL, DSCL, id),                                   \
		SRI_ARR(DSCL_CONTROL, DSCL, id),                                        \
		SRI_ARR(DSCL_SC_MODE, DSCL, id),                                        \
		SRI_ARR(DSCL_EASF_H_MODE, DSCL, id),                                    \
		SRI_ARR(DSCL_EASF_H_BF_CNTL, DSCL, id),                                 \
		SRI_ARR(DSCL_EASF_H_RINGEST_EVENTAP_REDUCE, DSCL, id),                  \
		SRI_ARR(DSCL_EASF_H_RINGEST_EVENTAP_GAIN, DSCL, id),                    \
		SRI_ARR(DSCL_EASF_H_BF_FINAL_MAX_MIN, DSCL, id),                        \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG0, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG1, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG2, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG3, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG4, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG5, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG6, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG7, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG0, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG1, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG2, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG3, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG4, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG5, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_MODE, DSCL, id),                                    \
		SRI_ARR(DSCL_EASF_V_BF_CNTL, DSCL, id),                                 \
		SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL1, DSCL, id),                      \
		SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL2, DSCL, id),                      \
		SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL3, DSCL, id),                      \
		SRI_ARR(DSCL_EASF_V_RINGEST_EVENTAP_REDUCE, DSCL, id),                  \
		SRI_ARR(DSCL_EASF_V_RINGEST_EVENTAP_GAIN, DSCL, id),                    \
		SRI_ARR(DSCL_EASF_V_BF_FINAL_MAX_MIN, DSCL, id),                        \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG0, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG1, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG2, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG3, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG4, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG5, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG6, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG7, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG0, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG1, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG2, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG3, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG4, DSCL, id),                            \
		SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG5, DSCL, id),                            \
		SRI_ARR(DSCL_SC_MATRIX_C0C1, DSCL, id),                                 \
		SRI_ARR(DSCL_SC_MATRIX_C2C3, DSCL, id),                                 \
		SRI_ARR(ISHARP_MODE, DSCL, id),                                         \
		SRI_ARR(ISHARP_DELTA_LUT_MEM_PWR_CTRL, DSCL, id),                       \
		SRI_ARR(ISHARP_NOISEDET_THRESHOLD, DSCL, id),                           \
		SRI_ARR(ISHARP_NOISE_GAIN_PWL, DSCL, id),                               \
		SRI_ARR(ISHARP_LBA_PWL_SEG0, DSCL, id),                                 \
		SRI_ARR(ISHARP_LBA_PWL_SEG1, DSCL, id),                                 \
		SRI_ARR(ISHARP_LBA_PWL_SEG2, DSCL, id),                                 \
		SRI_ARR(ISHARP_LBA_PWL_SEG3, DSCL, id),                                 \
		SRI_ARR(ISHARP_LBA_PWL_SEG4, DSCL, id),                                 \
		SRI_ARR(ISHARP_LBA_PWL_SEG5, DSCL, id),                                 \
		SRI_ARR(ISHARP_DELTA_CTRL, DSCL, id),                                   \
		SRI_ARR(ISHARP_DELTA_DATA, DSCL, id),                                   \
		SRI_ARR(ISHARP_DELTA_INDEX, DSCL, id),                                  \
		SRI_ARR(ISHARP_NLDELTA_SOFT_CLIP, DSCL, id),                            \
		SRI_ARR(SCL_VERT_FILTER_INIT_BOT, DSCL, id),                            \
		SRI_ARR(SCL_VERT_FILTER_INIT_BOT_C, DSCL, id)

/* Stream encoder */
#define SE_DCN42_REG_LIST_RI(id)                                                \
	SRI_ARR(HDMI_CONTROL, DIG, id), SRI_ARR(HDMI_DB_CONTROL, DIG, id),          \
		SRI_ARR(HDMI_GC, DIG, id),                                              \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL0, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL1, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL2, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL3, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL4, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL5, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL6, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL7, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL8, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL9, DIG, id),                         \
		SRI_ARR(HDMI_GENERIC_PACKET_CONTROL10, DIG, id),                        \
		SRI_ARR(HDMI_INFOFRAME_CONTROL0, DIG, id),                              \
		SRI_ARR(HDMI_VBI_PACKET_CONTROL, DIG, id),                              \
		SRI_ARR(HDMI_AUDIO_PACKET_CONTROL, DIG, id),                            \
		SRI_ARR(HDMI_ACR_PACKET_CONTROL, DIG, id),                              \
		SRI_ARR(HDMI_ACR_32_0, DIG, id), SRI_ARR(HDMI_ACR_32_1, DIG, id),       \
		SRI_ARR(HDMI_ACR_44_0, DIG, id), SRI_ARR(HDMI_ACR_44_1, DIG, id),       \
		SRI_ARR(HDMI_ACR_48_0, DIG, id), SRI_ARR(HDMI_ACR_48_1, DIG, id),       \
		SRI_ARR(DP_DB_CNTL, DP, id), SRI_ARR(DP_MSA_MISC, DP, id),              \
		SRI_ARR(DP_MSA_VBID_MISC, DP, id), SRI_ARR(DP_MSA_COLORIMETRY, DP, id), \
		SRI_ARR(DP_MSA_TIMING_PARAM1, DP, id),                                  \
		SRI_ARR(DP_MSA_TIMING_PARAM2, DP, id),                                  \
		SRI_ARR(DP_MSA_TIMING_PARAM3, DP, id),                                  \
		SRI_ARR(DP_MSA_TIMING_PARAM4, DP, id),                                  \
		SRI_ARR(DP_MSE_RATE_CNTL, DP, id), SRI_ARR(DP_MSE_RATE_UPDATE, DP, id), \
		SRI_ARR(DP_PIXEL_FORMAT, DP, id), SRI_ARR(DP_SEC_CNTL, DP, id),         \
		SRI_ARR(DP_SEC_CNTL1, DP, id), SRI_ARR(DP_SEC_CNTL2, DP, id),           \
		SRI_ARR(DP_SEC_CNTL5, DP, id), SRI_ARR(DP_SEC_CNTL6, DP, id),           \
		SRI_ARR(DP_STEER_FIFO, DP, id), SRI_ARR(DP_VID_M, DP, id),              \
		SRI_ARR(DP_VID_N, DP, id), SRI_ARR(DP_VID_STREAM_CNTL, DP, id),         \
		SRI_ARR(DP_VID_TIMING, DP, id), SRI_ARR(DP_SEC_AUD_N, DP, id),          \
		SRI_ARR(DP_SEC_TIMESTAMP, DP, id),                                      \
		SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                          \
		SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                         \
		SRI_ARR(DP_SEC_FRAMING4, DP, id), SRI_ARR(DP_GSP11_CNTL, DP, id),       \
		SRI_ARR(DME_CONTROL, DME, id),                                          \
		SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                          \
		SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                         \
		SRI_ARR(DIG_FE_CNTL, DIG, id),                                          \
		SRI_ARR(DIG_FE_EN_CNTL, DIG, id),                                       \
		SRI_ARR(DIG_FE_CLK_CNTL, DIG, id),                                      \
		SRI_ARR(DIG_CLOCK_PATTERN, DIG, id),                                    \
		SRI_ARR(DIG_FIFO_CTRL0, DIG, id),                                       \
		SRI_ARR(STREAM_MAPPER_CONTROL, DIG, id),\
		SRI_ARR(DIG_FE_AUDIO_CNTL, DIG, id)

/* HPO DP stream encoder */
#define DCN42_HPO_DP_STREAM_ENC_REG_LIST_RI(id)                                             \
	SR_ARR(DP_STREAM_MAPPER_CONTROL0, id),                                                  \
		SR_ARR(DP_STREAM_MAPPER_CONTROL1, id),                                              \
		SR_ARR(DP_STREAM_MAPPER_CONTROL2, id),                                              \
		SR_ARR(DP_STREAM_MAPPER_CONTROL3, id),                                              \
		SRI_ARR(DP_STREAM_ENC_CLOCK_CONTROL, DP_STREAM_ENC, id),                            \
		SRI_ARR(DP_STREAM_ENC_INPUT_MUX_CONTROL, DP_STREAM_ENC, id),                        \
		SRI_ARR(DP_STREAM_ENC_AUDIO_CONTROL, DP_STREAM_ENC, id),                            \
		SRI_ARR(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, DP_STREAM_ENC, id), \
		SRI_ARR(DP_SYM32_ENC_CONTROL, DP_SYM32_ENC, id),                                    \
		SRI_ARR(DP_SYM32_ENC_VID_PIXEL_FORMAT, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_VID_PIXEL_FORMAT_DOUBLE_BUFFER_CONTROL, DP_SYM32_ENC, id),     \
		SRI_ARR(DP_SYM32_ENC_VID_MSA0, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA1, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA2, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA3, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA4, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA5, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA6, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA7, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA8, DP_SYM32_ENC, id),                                   \
		SRI_ARR(DP_SYM32_ENC_VID_MSA_CONTROL, DP_SYM32_ENC, id),                            \
		SRI_ARR(DP_SYM32_ENC_VID_MSA_DOUBLE_BUFFER_CONTROL, DP_SYM32_ENC, id),              \
		SRI_ARR(DP_SYM32_ENC_VID_FIFO_CONTROL, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_VID_STREAM_CONTROL, DP_SYM32_ENC, id),                         \
		SRI_ARR(DP_SYM32_ENC_VID_VBID_CONTROL, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_SDP_CONTROL, DP_SYM32_ENC, id),                                \
		SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL0, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL2, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL3, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL5, DP_SYM32_ENC, id),                           \
		SRI_ARR(DP_SYM32_ENC_SDP_GSP_CONTROL11, DP_SYM32_ENC, id),                          \
		SRI_ARR(DP_SYM32_ENC_SDP_METADATA_PACKET_CONTROL, DP_SYM32_ENC, id),                \
		SRI_ARR(DP_SYM32_ENC_SDP_AUDIO_CONTROL0, DP_SYM32_ENC, id),                         \
		SRI_ARR(DP_SYM32_ENC_VID_CRC_CONTROL, DP_SYM32_ENC, id),                            \
		SRI_ARR(DP_SYM32_ENC_HBLANK_CONTROL, DP_SYM32_ENC, id)

/*HPO DP link encoder regs */
#define DCN42_HPO_DP_LINK_ENC_REG_LIST_RI(id)                    \
	SRI_ARR(DP_LINK_ENC_CLOCK_CONTROL, DP_LINK_ENC, id),         \
		SRI_ARR(DP_DPHY_SYM32_CONTROL, DP_DPHY_SYM32, id),       \
		SRI_ARR(DP_DPHY_SYM32_STATUS, DP_DPHY_SYM32, id),        \
		SRI_ARR(DP_DPHY_SYM32_TP_CONFIG, DP_DPHY_SYM32, id),     \
		SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED0, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED1, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED2, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_TP_PRBS_SEED3, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_TP_SQ_PULSE, DP_DPHY_SYM32, id),   \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM0, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM1, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM2, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM3, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM4, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM5, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM6, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM7, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM8, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM9, DP_DPHY_SYM32, id),    \
		SRI_ARR(DP_DPHY_SYM32_TP_CUSTOM10, DP_DPHY_SYM32, id),   \
		SRI_ARR(DP_DPHY_SYM32_SAT_VC0, DP_DPHY_SYM32, id),       \
		SRI_ARR(DP_DPHY_SYM32_SAT_VC1, DP_DPHY_SYM32, id),       \
		SRI_ARR(DP_DPHY_SYM32_SAT_VC2, DP_DPHY_SYM32, id),       \
		SRI_ARR(DP_DPHY_SYM32_SAT_VC3, DP_DPHY_SYM32, id),       \
		SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL0, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL1, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL2, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_VC_RATE_CNTL3, DP_DPHY_SYM32, id), \
		SRI_ARR(DP_DPHY_SYM32_SAT_UPDATE, DP_DPHY_SYM32, id)

#define VPG_DCN42_REG_LIST_RI(id) \
	SRI(VPG_GENERIC_STATUS, VPG, id), \
	SRI(VPG_GENERIC_PACKET_ACCESS_CTRL, VPG, id), \
	SRI(VPG_GENERIC_PACKET_DATA, VPG, id), \
	SRI(VPG_GSP_FRAME_UPDATE_CTRL, VPG, id), \
	SRI(VPG_GSP_IMMEDIATE_UPDATE_CTRL, VPG, id), \
	SRI(VPG_MEM_PWR, VPG, id)

/* DCCG */
#define DCCG_REG_LIST_DCN42_RI()               \
	SR(DPPCLK_DTO_CTRL),                       \
	DCCG_SRII(DTO_PARAM, DPPCLK, 0),       \
	DCCG_SRII(DTO_PARAM, DPPCLK, 1),       \
	DCCG_SRII(DTO_PARAM, DPPCLK, 2),       \
	DCCG_SRII(DTO_PARAM, DPPCLK, 3),       \
	DCCG_SRII(CLOCK_CNTL, HDMICHARCLK, 0), \
	SR(HDMISTREAMCLK0_DTO_PARAM),\
	SR(DCCG_GLOBAL_FGCG_REP_CNTL),\
	SR(DISPCLK_FREQ_CHANGE_CNTL), \
	SR(PHYASYMCLK_CLOCK_CNTL),  \
	SR(PHYBSYMCLK_CLOCK_CNTL), \
	SR(PHYCSYMCLK_CLOCK_CNTL), \
	SR(PHYDSYMCLK_CLOCK_CNTL), \
	SR(PHYESYMCLK_CLOCK_CNTL), \
	SR(DPSTREAMCLK_CNTL), \
	SR(HDMISTREAMCLK_CNTL), \
	SR(SYMCLK32_SE_CNTL), \
	SR(SYMCLK32_LE_CNTL), \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 0), \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 1), \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 2), \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 3), \
	SR(OTG_PIXEL_RATE_DIV), \
	SR(DTBCLK_P_CNTL), \
	SR(DCCG_AUDIO_DTO_SOURCE), \
	SR(DENTIST_DISPCLK_CNTL), \
	SR(DPPCLK_CTRL), \
	DCCG_SRII(MODULO, DP_DTO, 0), \
	DCCG_SRII(MODULO, DP_DTO, 1), \
	DCCG_SRII(MODULO, DP_DTO, 2), \
	DCCG_SRII(MODULO, DP_DTO, 3), \
	DCCG_SRII(PHASE, DP_DTO, 0), \
	DCCG_SRII(PHASE, DP_DTO, 1), \
	DCCG_SRII(PHASE, DP_DTO, 2), \
	DCCG_SRII(PHASE, DP_DTO, 3), \
	SR(OTG_ADD_DROP_PIXEL_CNTL), \
	SR(DSCCLK0_DTO_PARAM), \
	SR(DSCCLK1_DTO_PARAM), \
	SR(DSCCLK2_DTO_PARAM), \
	SR(DSCCLK3_DTO_PARAM), \
	SR(DSCCLK_DTO_CTRL), \
	SR(DCCG_GATE_DISABLE_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL2), \
	SR(DCCG_GATE_DISABLE_CNTL3), \
	SR(DCCG_GATE_DISABLE_CNTL4), \
	SR(DCCG_GATE_DISABLE_CNTL5), \
	SR(DCCG_GATE_DISABLE_CNTL6), \
	SR(SYMCLKA_CLOCK_ENABLE), \
	SR(SYMCLKB_CLOCK_ENABLE), \
	SR(SYMCLKC_CLOCK_ENABLE), \
	SR(SYMCLKD_CLOCK_ENABLE), \
	SR(SYMCLKE_CLOCK_ENABLE)

#define DCN42_AUD_COMMON_MASK_SH_LIST(mask_sh)                                             \
	SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),   \
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_SOURCE_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_USE_512FBR_DTO, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO1_USE_512FBR_DTO, mask_sh),\
		SF(DCCG_AUDIO_DTO0_MODULE, DCCG_AUDIO_DTO0_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO0_PHASE, DCCG_AUDIO_DTO0_PHASE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_MODULE, DCCG_AUDIO_DTO1_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_PHASE, DCCG_AUDIO_DTO1_PHASE, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES, AUDIO_RATE_CAPABILITIES, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, CLKSTOP, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, EPSS, mask_sh)

	/* HPO FRL stream encoder */
#define DCN42_HPO_FRL_STREAM_ENC_REG_LIST_RI(id)                                 \
	DCN3_0_HPO_FRL_STREAM_ENC_REG_LIST_RI(id),                               \
	SR_ARR(HDMI_STREAM_ENC_AUDIO_CONTROL, id),\
	SR_ARR(HDMI_TB_ENC_MEM_CTRL, id),\
	SR_ARR(HDMI_FRL_ENC_MEM_CTRL, id)
/* OPTC */
#define OPTC_COMMON_REG_LIST_DCN42_RI(inst)                                      \
		SRI_ARR(OTG_VSTARTUP_PARAM, OTG, inst),                                      \
		SRI_ARR(OTG_VUPDATE_PARAM, OTG, inst),                                   \
		SRI_ARR(OTG_VREADY_PARAM, OTG, inst),                                    \
		SRI_ARR(OTG_MASTER_UPDATE_LOCK, OTG, inst),                              \
		SRI_ARR(OTG_MASTER_UPDATE_MODE, OTG, inst),                              \
		SRI_ARR(OTG_V_COUNT_STOP_CONTROL, OTG, inst),                            \
		SRI_ARR(OTG_V_COUNT_STOP_CONTROL2, OTG, inst),                           \
		SRI_ARR(OTG_GSL_CONTROL, OTG, inst),					 \
		SRI2_ARR(OPTC_CLOCK_CONTROL, OPTC, inst),\
		SRI_ARR(OTG_GLOBAL_CONTROL0, OTG, inst),                                 \
		SRI_ARR(OTG_GLOBAL_CONTROL1, OTG, inst),                                 \
		SRI_ARR(OTG_GLOBAL_CONTROL2, OTG, inst),                                 \
		SRI_ARR(OTG_GLOBAL_CONTROL4, OTG, inst),                                 \
		SRI_ARR(OTG_DOUBLE_BUFFER_CONTROL, OTG, inst),                           \
		SRI_ARR(OTG_H_TOTAL, OTG, inst),                                         \
		SRI_ARR(OTG_H_BLANK_START_END, OTG, inst),                               \
		SRI_ARR(OTG_H_SYNC_A, OTG, inst),\
		SRI_ARR(OTG_H_SYNC_A_CNTL, OTG, inst), \
		SRI_ARR(OTG_H_TIMING_CNTL, OTG, inst), \
		SRI_ARR(OTG_V_TOTAL, OTG, inst),  \
		SRI_ARR(OTG_V_BLANK_START_END, OTG, inst),                               \
		SRI_ARR(OTG_V_SYNC_A, OTG, inst), \
		SRI_ARR(OTG_V_SYNC_A_CNTL, OTG, inst), \
		SRI_ARR(OTG_CONTROL, OTG, inst), \
		SRI_ARR(OTG_STEREO_CONTROL, OTG, inst), \
		SRI_ARR(OTG_3D_STRUCTURE_CONTROL, OTG, inst),                            \
		SRI_ARR(OTG_STEREO_STATUS, OTG, inst),                                   \
		SRI_ARR(OTG_V_TOTAL_MAX, OTG, inst),                                     \
		SRI_ARR(OTG_V_TOTAL_MIN, OTG, inst),                                     \
		SRI_ARR(OTG_V_TOTAL_CONTROL, OTG, inst),                                 \
		SRI_ARR(OTG_TRIGA_CNTL, OTG, inst),                                      \
		SRI_ARR(OTG_FORCE_COUNT_NOW_CNTL, OTG, inst),                            \
		SRI_ARR(OTG_STATIC_SCREEN_CONTROL, OTG, inst),                           \
		SRI_ARR(OTG_STATUS_FRAME_COUNT, OTG, inst),                              \
		SRI_ARR(OTG_STATUS, OTG, inst), 										\
		SRI_ARR(OTG_STATUS_POSITION, OTG, inst), \
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
		SRI_ARR(CONTROL, VTG, inst), \
		SRI_ARR(OTG_VERT_SYNC_CONTROL, OTG, inst),  \
		SRI_ARR(OTG_GSL_CONTROL, OTG, inst), \
		SRI_ARR(OTG_CRC_CNTL, OTG, inst),   \
		SRI_ARR(OTG_CRC0_DATA_R, OTG, inst),                                     \
		SRI_ARR(OTG_CRC0_DATA_G, OTG, inst),                                     \
		SRI_ARR(OTG_CRC0_DATA_B, OTG, inst),                                     \
		SRI_ARR(OTG_CRC1_DATA_R, OTG, inst),                                     \
		SRI_ARR(OTG_CRC1_DATA_G, OTG, inst),                                     \
		SRI_ARR(OTG_CRC1_DATA_B, OTG, inst),                                     \
		SRI_ARR(OTG_CRC0_WINDOWA_X_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC0_WINDOWA_Y_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC0_WINDOWB_X_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC0_WINDOWB_Y_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC1_WINDOWA_X_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC1_WINDOWA_Y_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC1_WINDOWB_X_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC1_WINDOWB_Y_CONTROL, OTG, inst),                          \
		SRI_ARR(OTG_CRC0_WINDOWA_X_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC0_WINDOWA_Y_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC0_WINDOWB_X_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC0_WINDOWB_Y_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC1_WINDOWA_X_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC1_WINDOWA_Y_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC1_WINDOWB_X_CONTROL_READBACK, OTG, inst),\
		SRI_ARR(OTG_CRC1_WINDOWB_Y_CONTROL_READBACK, OTG, inst),\
		SR_ARR(GSL_SOURCE_SELECT, inst),                                         \
		SRI_ARR(OTG_TRIGA_MANUAL_TRIG, OTG, inst),                               \
		SRI_ARR(OTG_GLOBAL_CONTROL1, OTG, inst),                                 \
		SRI_ARR(OTG_GLOBAL_CONTROL2, OTG, inst),                                 \
		SRI_ARR(OTG_GSL_WINDOW_X, OTG, inst),                                    \
		SRI_ARR(OTG_GSL_WINDOW_Y, OTG, inst),                                    \
		SRI_ARR(OTG_VUPDATE_KEEPOUT, OTG, inst),                                 \
		SRI_ARR(OTG_DRR_TRIGGER_WINDOW, OTG, inst),                              \
		SRI_ARR(OTG_DRR_V_TOTAL_CHANGE, OTG, inst),                              \
		SRI_ARR(OPTC_DATA_FORMAT_CONTROL, ODM, inst),                            \
		SRI_ARR(OPTC_BYTES_PER_PIXEL, ODM, inst),                                \
		SRI_ARR(OPTC_WIDTH_CONTROL, ODM, inst),                                  \
		SRI_ARR(OPTC_WIDTH_CONTROL2, ODM, inst),                                 \
		SRI_ARR(OPTC_MEMORY_CONFIG, ODM, inst),                                  \
		SRI_ARR(OTG_DRR_CONTROL, OTG, inst),                                     \
		SRI_ARR(OTG_PSTATE_REGISTER, OTG, inst),                                 \
		SRI_ARR(OTG_PWA_FRAME_SYNC_CONTROL, OTG, inst),\
		SRI_ARR(OTG_PIPE_UPDATE_STATUS, OTG, inst),\
		SRI_ARR(INTERRUPT_DEST, OTG, inst)

/* CLK SRC */
#define CS_COMMON_REG_LIST_DCN42_RI(index, pllid)               \
	SRI_ARR_ALPHABET(PIXCLK_RESYNC_CNTL, PHYPLL, index, pllid), \
		SRII_ARR_2(PHASE, DP_DTO, 0, index),                    \
		SRII_ARR_2(PHASE, DP_DTO, 1, index),                    \
		SRII_ARR_2(PHASE, DP_DTO, 2, index),                    \
		SRII_ARR_2(PHASE, DP_DTO, 3, index),                    \
		SRII_ARR_2(MODULO, DP_DTO, 0, index),                   \
		SRII_ARR_2(MODULO, DP_DTO, 1, index),                   \
		SRII_ARR_2(MODULO, DP_DTO, 2, index),                   \
		SRII_ARR_2(MODULO, DP_DTO, 3, index),                   \
		SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 0, index),             \
		SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 1, index),             \
		SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 2, index),             \
		SRII_ARR_2(PIXEL_RATE_CNTL, OTG, 3, index)

/* ABM */
#define ABM_DCN42_REG_LIST_RI(id)                               \
	SRI_ARR(DC_ABM1_HG_SAMPLE_RATE, ABM, id),                   \
		SRI_ARR(DC_ABM1_LS_SAMPLE_RATE, ABM, id),               \
		SRI_ARR(DC_ABM1_HG_MISC_CTRL, ABM, id),                 \
		SRI_ARR(DC_ABM1_IPCSC_COEFF_SEL, ABM, id),              \
		SRI_ARR(BL1_PWM_BL_UPDATE_SAMPLE_RATE, ABM, id),        \
		SRI_ARR(BL1_PWM_CURRENT_ABM_LEVEL, ABM, id),            \
		SRI_ARR(BL1_PWM_TARGET_ABM_LEVEL, ABM, id),             \
		SRI_ARR(BL1_PWM_USER_LEVEL, ABM, id),                   \
		SRI_ARR(DC_ABM1_LS_MIN_MAX_PIXEL_VALUE_THRES, ABM, id), \
		SRI_ARR(DC_ABM1_HGLS_REG_READ_PROGRESS, ABM, id),       \
		SRI_ARR(DC_ABM1_HG_BIN_33_40_SHIFT_INDEX, ABM, id),     \
		SRI_ARR(DC_ABM1_HG_BIN_33_64_SHIFT_FLAG, ABM, id),      \
		SRI_ARR(DC_ABM1_HG_BIN_41_48_SHIFT_INDEX, ABM, id),     \
		SRI_ARR(DC_ABM1_HG_BIN_49_56_SHIFT_INDEX, ABM, id),     \
		SRI_ARR(DC_ABM1_HG_BIN_57_64_SHIFT_INDEX, ABM, id),     \
		SRI_ARR(DC_ABM1_HG_RESULT_DATA, ABM, id),               \
		SRI_ARR(DC_ABM1_HG_RESULT_INDEX, ABM, id),              \
		SRI_ARR(DC_ABM1_ACE_OFFSET_SLOPE_DATA, ABM, id),        \
		SRI_ARR(DC_ABM1_ACE_PWL_CNTL, ABM, id)

/* HUBP */
#define HUBP_REG_LIST_DCN42_RI(id)                                     \
	HUBP_REG_LIST_DCN30_RI(id), SRI_ARR(DCHUBP_MALL_CONFIG, HUBP, id), \
		SRI_ARR(DCHUBP_VMPG_CONFIG, HUBP, id),                         \
		SRI_ARR(UCLK_PSTATE_FORCE, HUBPREQ, id),                       \
		SRI_ARR(HUBP_3DLUT_DLG_PARAM, CURSOR0_, id),                   \
		HUBP_3DLUT_FL_REG_LIST_DCN401(id)
struct dcn42_resource_pool {
	struct resource_pool base;
};
struct resource_pool *dcn42_create_resource_pool(
	const struct dc_init_data *init_data,
	struct dc *dc);

enum dc_status dcn42_validate_bandwidth(struct dc *dc,
							  struct dc_state *context,
							  enum dc_validate_mode validate_mode);

void dcn42_prepare_mcache_programming(struct dc *dc, struct dc_state *context);

#endif /* _DCN42_RESOURCE_H_ */
