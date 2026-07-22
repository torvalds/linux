// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef _DCN60_RESOURCE_H_
#define _DCN60_RESOURCE_H_

#include "core_types.h"
#include "dcn42/dcn42_mpc.h"

#define TO_DCN60_RES_POOL(pool)\
	container_of(pool, struct dcn60_resource_pool, base)

struct dcn60_resource_pool {
	struct resource_pool base;
};

struct resource_pool *dcn60_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc);

bool dcn60_program_mcache_pipe_config(struct dc_state *context,
	const struct dc_mcache_params *mcache_params);

bool dcn50_program_mcache_pipe_config(struct dc_state *context,
	const struct dc_mcache_params *mcache_params);

/* HWSS */

#define HWSEQ_DCN60_REG_LIST() \
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL), \
	SR(DIO_MEM_PWR_CTRL), \
	SR(ODM_MEM_PWR_CTRL3), \
	SR(MMHUBBUB_MEM_PWR_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL2), \
	SR(DCFCLK_CNTL),\
	SR(DC_MEM_GLOBAL_PWR_REQ_CNTL), \
	SRII(PIXEL_RATE_CNTL, OTG, 0), \
	SRII(PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PIXEL_RATE_CNTL, OTG, 3),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 0),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 3),\
	SR(MICROSECOND_TIME_BASE_DIV), \
	SR(MILLISECOND_TIME_BASE_DIV), \
	SR(DISPCLK_FREQ_CHANGE_CNTL), \
	SR(RBBMIF_TIMEOUT_DIS), \
	SR(RBBMIF_TIMEOUT_DIS_2), \
	SR(DCHUBBUB_CRC_CTRL), \
	SR(DPP_TOP0_DPP_CRC_CTRL), \
	SR(MPC_CRC_CTRL), \
	SR(AZALIA_AUDIO_DTO), \
	SR(HPO_TOP_HW_CONTROL),\
	SR(AZALIA_CONTROLLER_CLOCK_GATING), \
	SR(HDCP_INTERRUPT_DEST)

/* HUBBUB */
#define HUBBUB_REG_LIST_DCN60_RI(id)                                         \
	SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A),                               \
	SR(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B),                               \
	SR(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL),                                  \
	SR(DCHUBBUB_ARB_DRAM_STATE_CNTL),                                        \
	SR(DCHUBBUB_ARB_SAT_LEVEL),                                              \
	SR(DCHUBBUB_ARB_DF_REQ_OUTSTAND),                                        \
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL),                                          \
	SR(DCHUBBUB_TEST_DEBUG_INDEX),                                           \
	SR(DCHUBBUB_TEST_DEBUG_DATA),                                            \
	SR(DCHUBBUB_SOFT_RESET),                                                 \
	SR(DCHUBBUB_CRC_CTRL),                                                   \
	SR(DCN_VM_FB_LOCATION_BASE),                                             \
	SR(DCN_VM_FB_LOCATION_TOP),                                              \
	SR(DCN_VM_FB_OFFSET),                                                    \
	SR(DCN_VM_AGP_BOT),                                                      \
	SR(DCN_VM_AGP_TOP),                                                      \
	SR(DCN_VM_AGP_BASE),                                                     \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A),                              \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B),                              \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_A),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_A),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK1_B),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK1_B),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_A),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_A),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK2_B),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK2_B),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_A),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_A),                             \
	SR(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK3_B),                            \
	SR(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK3_B),                             \
	SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_A),                                      \
	SR(DCHUBBUB_ARB_FRAC_URG_BW_NOM_B),                                      \
	SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_A),                                     \
	SR(DCHUBBUB_ARB_FRAC_URG_BW_FLIP_B),                                     \
	SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_A),                            \
	SR(DCHUBBUB_ARB_REFCYC_PER_TRIP_TO_MEMORY_B),                            \
	SR(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_A),                                 \
	SR(DCHUBBUB_ARB_REFCYC_PER_META_TRIP_B),                                 \
	SR(DCHUBBUB_DET0_CTRL),                                                  \
	SR(DCHUBBUB_DET1_CTRL),                                                  \
	SR(DCHUBBUB_DET2_CTRL),                                                  \
	SR(DCHUBBUB_DET3_CTRL),                                                  \
	SR(DCHUBBUB_COMPBUF_CTRL),                                               \
	SR(COMPBUF_RESERVED_SPACE),                                              \
	SR(DCHUBBUB_DEBUG_CTRL_0),                                               \
	SR(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_A),                            \
	SR(DCHUBBUB_ARB_BUFFER_FULLNESS_WATERMARK_B),                            \
	SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_A),                         \
	SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK_B),                         \
	SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_A),                        \
	SR(DCHUBBUB_ARB_UCLK_PSTATE_CHANGE_WATERMARK1_B),                        \
	SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_A),                         \
	SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK_B),                         \
	SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_A),                        \
	SR(DCHUBBUB_ARB_FCLK_PSTATE_CHANGE_WATERMARK1_B),                        \
	SR(DCN_VM_FAULT_ADDR_MSB), SR(DCN_VM_FAULT_ADDR_LSB),                    \
	SR(DCN_VM_FAULT_CNTL),                                                   \
	SR(DCN_VM_FAULT_STATUS),                                                 \
	SR(SDPIF_REQUEST_RATE_LIMIT),                                            \
	SR(DCHUBBUB_CLOCK_CNTL),                                                 \
	SR(DCHUBBUB_SDPIF_CFG0),                                                 \
	SR(DCHUBBUB_SDPIF_CFG1),                                                 \
	SR(DCHUBBUB_MEM_PWR_MODE_CTRL),                                          \
	SR(DCHUBBUB_TIMEOUT_DETECTION_CTRL1),                                    \
	SR(DCHUBBUB_TIMEOUT_DETECTION_CTRL2),                                    \
	SR(DCHUBBUB_CTRL_STATUS),                                                \
	SR(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL),                               \
	SR(DCHUBBUB_PERFORMANCE_MEASUREMENT_CNTL2),                              \
	SR(DC_PERFMON5_PERFCOUNTER_CNTL),                                        \
	SR(DC_PERFMON5_PERFCOUNTER_CNTL2),                                       \
	SR(DC_PERFMON5_PERFCOUNTER_STATE),                                       \
	SR(DC_PERFMON5_PERFMON_CNTL),                                            \
	SR(DC_PERFMON5_PERFMON_CNTL2),                                           \
	SR(DC_PERFMON5_PERFMON_CVALUE_INT_MISC),                                 \
	SR(DC_PERFMON5_PERFMON_CVALUE_LOW),                                      \
	SR(DC_PERFMON5_PERFMON_HI),                                              \
	SR(DC_PERFMON5_PERFMON_LOW),                                             \
	SR(FMON_CTRL),                                                           \
	SR(DCHUBBUB_ARB_QOS_FORCE)

/* HUBP */
#define HUBP_REG_LIST_DCN60_RI(id)                                               \
	SRI_ARR(NOM_PARAMETERS_0, HUBPREQ, id),                                  \
	SRI_ARR(NOM_PARAMETERS_1, HUBPREQ, id),                                  \
	SRI_ARR(NOM_PARAMETERS_2, HUBPREQ, id),                                  \
	SRI_ARR(NOM_PARAMETERS_3, HUBPREQ, id),                                  \
	SRI_ARR(DCN_VM_MX_L1_TLB_CNTL, HUBPREQ, id),                             \
	SRI_ARR(DCHUBP_CNTL, HUBP, id),                                          \
	SRI_ARR(HUBPREQ_DEBUG_DB, HUBP, id),                                     \
	SRI_ARR(HUBPREQ_DEBUG, HUBP, id),                                        \
	SRI_ARR(DCSURF_ADDR_CONFIG, HUBP, id),                                   \
	SRI_ARR(DCSURF_LEGACY_ADDR_CONFIG, HUBP, id),                            \
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
	SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),             \
	SRI_ARR(DCSURF_PRIMARY_SURFACE_ADDRESS_C, HUBPREQ, id),                  \
	SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH_C, HUBPREQ, id),           \
	SRI_ARR(DCSURF_SECONDARY_SURFACE_ADDRESS_C, HUBPREQ, id),                \
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
	SRI_ARR(DCN_GLOBAL_TTU_CNTL, HUBPREQ, id),                               \
	SRI_ARR(DCN_SURF0_TTU_CNTL0, HUBPREQ, id),                               \
	SRI_ARR(DCN_SURF0_TTU_CNTL1, HUBPREQ, id),                               \
	SRI_ARR(DCN_SURF1_TTU_CNTL0, HUBPREQ, id),                               \
	SRI_ARR(DCN_SURF1_TTU_CNTL1, HUBPREQ, id),                               \
	SRI_ARR(DCN_CUR0_TTU_CNTL0, HUBPREQ, id),                                \
	SRI_ARR(DCN_CUR0_TTU_CNTL1, HUBPREQ, id),                                \
	SRI_ARR(HUBP_CLK_CNTL, HUBP, id),                                        \
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
	SRI_ARR(VMID_SETTINGS_0, HUBPREQ, id),                                   \
	SRI_ARR(FLIP_PARAMETERS_3, HUBPREQ, id),                                 \
	SRI_ARR(FLIP_PARAMETERS_4, HUBPREQ, id),                                 \
	SRI_ARR(FLIP_PARAMETERS_5, HUBPREQ, id),                                 \
	SRI_ARR(FLIP_PARAMETERS_6, HUBPREQ, id),                                 \
	SRI_ARR(VBLANK_PARAMETERS_5, HUBPREQ, id),                               \
	SRI_ARR(VBLANK_PARAMETERS_6, HUBPREQ, id),                               \
	SRI_ARR(DCN_DMDATA_VM_CNTL, HUBPREQ, id),                                \
	SRI_ARR(DCHUBP_VMPG_CONFIG, HUBP, id),                                   \
	SRI_ARR(UCLK_PSTATE_FORCE, HUBPREQ, id),                                 \
	HUBP_3DLUT_FL_REG_LIST_DCN401(id),                                       \
	SRI_ARR(DCSURF_VIEWPORT_MCACHE_SPLIT_COORDINATE, HUBP, id),              \
	SRI_ARR(DCHUBP_MCACHEID_CONFIG, HUBP, id),								 \
	SRI_ARR(HUBPRET_READ_LINE_VALUE, HUBPRET, id),                           \
	SRI_ARR(DST_Y_DELTA_DRQ_LIMIT, HUBPREQ, id),                             \
	SRI_ARR(DST_Y_ALT_CH_DRQ_LIMIT, HUBPREQ, id)

/* DPP */
#define DPP_REG_LIST_DCN60_RI(id)                                            \
	SRI_ARR(CM_DEALPHA, CM, id),                                             \
	SRI_ARR(CM_MEM_PWR_STATUS, CM, id),                                      \
	SRI_ARR(CM_BIAS_CR_R, CM, id),                                           \
	SRI_ARR(CM_BIAS_Y_G_CB_B, CM, id),                                       \
	SRI_ARR(CM_GAMCOR_CONTROL, CM, id),                                      \
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
	SRI_ARR(CM_HIST_CNTL, CM, id),                                           \
	SRI_ARR(CM_HIST_LOCK, CM, id),                                           \
	SRI_ARR(CM_HIST_INDEX, CM, id),                                          \
	SRI_ARR(CM_HIST_DATA, CM, id),                                           \
	SRI_ARR(CM_HIST_STATUS, CM, id),                                         \
	SRI_ARR(CM_HIST_SCALE_SRC1, CM, id),                                     \
	SRI_ARR(CM_HIST_COEFA_SRC2, CM, id),                                     \
	SRI_ARR(CM_HIST_COEFB_SRC2, CM, id),                                     \
	SRI_ARR(CM_HIST_COEFC_SRC2, CM, id),                                     \
	SRI_ARR(CM_HIST_SCALE_SRC3, CM, id),                                     \
	SRI_ARR(CM_HIST_BIAS_SRC1, CM, id),                                      \
	SRI_ARR(CM_HIST_BIAS_SRC2, CM, id),                                      \
	SRI_ARR(CM_HIST_BIAS_SRC3, CM, id),                                      \
	SRI_ARR(DSCL_EXT_OVERSCAN_LEFT_RIGHT, DSCL, id),                         \
	SRI_ARR(DSCL_EXT_OVERSCAN_TOP_BOTTOM, DSCL, id),                         \
	SRI_ARR(OTG_H_BLANK, DSCL, id),                                          \
	SRI_ARR(OTG_V_BLANK, DSCL, id),                                          \
	SRI_ARR(SCL_MODE, DSCL, id),                                             \
	SRI_ARR(LB_DATA_FORMAT, DSCL, id),                                       \
	SRI_ARR(LB_MEMORY_CTRL, DSCL, id),                                       \
	SRI_ARR(DSCL_AUTOCAL, DSCL, id),                                         \
	SRI_ARR(SCL_TAP_CONTROL, DSCL, id),                                      \
	SRI_ARR(SCL_COEF_RAM_TAP_SELECT, DSCL, id),                              \
	SRI_ARR(SCL_COEF_RAM_TAP_DATA, DSCL, id),                                \
	SRI_ARR(DSCL_2TAP_CONTROL, DSCL, id),                                    \
	SRI_ARR(MPC_SIZE, DSCL, id),                                             \
	SRI_ARR(SCL_BLACK_COLOR, DSCL, id),                                      \
	SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO, DSCL, id),                          \
	SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO, DSCL, id),                          \
	SRI_ARR(SCL_HORZ_FILTER_SCALE_RATIO_C, DSCL, id),                        \
	SRI_ARR(SCL_VERT_FILTER_SCALE_RATIO_C, DSCL, id),                        \
	SRI_ARR(SCL_HORZ_FILTER_INIT, DSCL, id),                                 \
	SRI_ARR(SCL_HORZ_FILTER_INIT_C, DSCL, id),                               \
	SRI_ARR(SCL_VERT_FILTER_INIT, DSCL, id),                                 \
	SRI_ARR(SCL_VERT_FILTER_INIT_C, DSCL, id),                               \
	SRI_ARR(RECOUT_START, DSCL, id),                                         \
	SRI_ARR(RECOUT_SIZE, DSCL, id),                                          \
	SRI_ARR(PRE_DEALPHA, CNVC_CFG, id),                                      \
	SRI_ARR(PRE_REALPHA, CNVC_CFG, id),                                      \
	SRI_ARR(PRE_GAM, CNVC_CFG, id),                                          \
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
	SRI_ARR(CM_MEM_PWR_CTRL, CM, id),                                        \
	SRI_ARR(CM_CONTROL, CM, id),                                             \
	SRI_ARR(CM_TEST_DEBUG_INDEX, CM, id),                                    \
	SRI_ARR(CM_TEST_DEBUG_DATA, CM, id),                                     \
	SRI_ARR(FORMAT_CONTROL, CNVC_CFG, id),                                   \
	SRI_ARR(CNVC_SURFACE_PIXEL_FORMAT, CNVC_CFG, id),                        \
	SRI_ARR(CURSOR0_CONTROL, CM_CUR, id),                                    \
	SRI_ARR(CURSOR0_COLOR0, CM_CUR, id),                                     \
	SRI_ARR(CURSOR0_COLOR1, CM_CUR, id),                                     \
	SRI_ARR(CURSOR0_FP_SCALE_BIAS_G_Y, CM_CUR, id),                          \
	SRI_ARR(CURSOR0_FP_SCALE_BIAS_RB_CRCB, CM_CUR, id),                      \
	SRI_ARR(CUR0_MATRIX_MODE, CM_CUR, id),                                   \
	SRI_ARR(CUR0_MATRIX_C11_C12_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C13_C14_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C21_C22_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C23_C24_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C31_C32_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C33_C34_A, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C11_C12_B, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C13_C14_B, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C21_C22_B, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C23_C24_B, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C31_C32_B, CM_CUR, id),                              \
	SRI_ARR(CUR0_MATRIX_C33_C34_B, CM_CUR, id),                              \
	SRI_ARR(DPP_CONTROL, DPP_TOP, id),                                       \
	SRI_ARR(CM_HDR_MULT_COEF, CM, id),                                       \
	SRI_ARR(CURSOR_CONTROL, CURSOR0_, id),                                   \
	SRI_ARR(ALPHA_2BIT_LUT01, CNVC_CFG, id),                                 \
	SRI_ARR(ALPHA_2BIT_LUT23, CNVC_CFG, id),                                 \
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
	SRI_ARR(OBUF_MEM_PWR_CTRL, DSCL, id),                                    \
	SRI_ARR(DSCL_MEM_PWR_STATUS, DSCL, id),                                  \
	SRI_ARR(DSCL_MEM_PWR_CTRL, DSCL, id),                                    \
	SRI_ARR(DSCL_CONTROL, DSCL, id),                                         \
	SRI_ARR(DSCL_SC_MODE, DSCL, id),                                         \
	SRI_ARR(DSCL_EASF_H_MODE, DSCL, id),                                     \
	SRI_ARR(DSCL_EASF_H_BF_CNTL, DSCL, id),                                  \
	SRI_ARR(DSCL_EASF_H_RINGEST_EVENTAP_REDUCE, DSCL, id),                   \
	SRI_ARR(DSCL_EASF_H_RINGEST_EVENTAP_GAIN, DSCL, id),                     \
	SRI_ARR(DSCL_EASF_H_BF_FINAL_MAX_MIN, DSCL, id),                         \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG0, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG1, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG2, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG3, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG4, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG5, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG6, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF1_PWL_SEG7, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG0, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG1, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG2, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG3, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG4, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_H_BF3_PWL_SEG5, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_MODE, DSCL, id),                                     \
	SRI_ARR(DSCL_EASF_V_BF_CNTL, DSCL, id),                                  \
	SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL1, DSCL, id),                       \
	SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL2, DSCL, id),                       \
	SRI_ARR(DSCL_EASF_V_RINGEST_3TAP_CNTL3, DSCL, id),                       \
	SRI_ARR(DSCL_EASF_V_RINGEST_EVENTAP_REDUCE, DSCL, id),                   \
	SRI_ARR(DSCL_EASF_V_RINGEST_EVENTAP_GAIN, DSCL, id),                     \
	SRI_ARR(DSCL_EASF_V_BF_FINAL_MAX_MIN, DSCL, id),                         \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG0, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG1, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG2, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG3, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG4, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG5, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG6, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF1_PWL_SEG7, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG0, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG1, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG2, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG3, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG4, DSCL, id),                             \
	SRI_ARR(DSCL_EASF_V_BF3_PWL_SEG5, DSCL, id),                             \
	SRI_ARR(DSCL_SC_MATRIX_C0C1, DSCL, id),                                  \
	SRI_ARR(DSCL_SC_MATRIX_C2C3, DSCL, id),                                  \
	SRI_ARR(ISHARP_MODE, DSCL, id),                                          \
	SRI_ARR(ISHARP_NOISEDET_THRESHOLD, DSCL, id),                            \
	SRI_ARR(ISHARP_NOISE_GAIN_PWL, DSCL, id),                                \
	SRI_ARR(ISHARP_LBA_PWL_SEG0, DSCL, id),                                  \
	SRI_ARR(ISHARP_LBA_PWL_SEG1, DSCL, id),                                  \
	SRI_ARR(ISHARP_LBA_PWL_SEG2, DSCL, id),                                  \
	SRI_ARR(ISHARP_LBA_PWL_SEG3, DSCL, id),                                  \
	SRI_ARR(ISHARP_LBA_PWL_SEG4, DSCL, id),                                  \
	SRI_ARR(ISHARP_LBA_PWL_SEG5, DSCL, id),                                  \
	SRI_ARR(ISHARP_DELTA_CTRL, DSCL, id),                                    \
	SRI_ARR(ISHARP_DELTA_DATA, DSCL, id),                                    \
	SRI_ARR(ISHARP_DELTA_INDEX, DSCL, id),                                   \
	SRI_ARR(ISHARP_DELTA_LUT_MEM_PWR_CTRL, DSCL, id),                        \
	SRI_ARR(ISHARP_NLDELTA_SOFT_CLIP, DSCL, id)

/* Stream encoder */
#define SE_REG_LIST_DCN60_RI(id)                                               \
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
	SRI_ARR(DP_SEC_TIMESTAMP, DP, id),                                       \
	SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                           \
	SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                          \
	SRI_ARR(DP_SEC_FRAMING4, DP, id), SRI_ARR(DP_GSP11_CNTL, DP, id),        \
	SRI_ARR(DME_CONTROL, DME, id),                                           \
	SRI_ARR(DP_SEC_METADATA_TRANSMISSION, DP, id),                           \
	SRI_ARR(HDMI_METADATA_PACKET_CONTROL, DIG, id),                          \
	SRI_ARR(DIG_FE_CNTL, DIG, id),                                           \
	SRI_ARR(DIG_FE_EN_CNTL, DIG, id),                                        \
	SRI_ARR(DIG_FE_CLK_CNTL, DIG, id),                                       \
	SRI_ARR(DIG_CLOCK_PATTERN, DIG, id),                                     \
	SRI_ARR(DIG_FIFO_CTRL0, DIG, id),                                        \
	SRI_ARR(STREAM_MAPPER_CONTROL, DIG, id),                                 \
	SRI_ARR(DIG_FE_AUDIO_CNTL, DIG, id)

/* DCCG */
#define DCCG_REG_LIST_DCN60_RI()                                                 \
	SR(DPPCLK_DTO_CTRL), DCCG_SRII(DTO_PARAM, DPPCLK, 0),                    \
	DCCG_SRII(DTO_PARAM, DPPCLK, 1), DCCG_SRII(DTO_PARAM, DPPCLK, 2),        \
	DCCG_SRII(DTO_PARAM, DPPCLK, 3), DCCG_SRII(CLOCK_CNTL, HDMICHARCLK, 0),  \
	SR(PHYASYMCLK_CLOCK_CNTL), SR(PHYBSYMCLK_CLOCK_CNTL),                    \
	SR(PHYCSYMCLK_CLOCK_CNTL), SR(PHYDSYMCLK_CLOCK_CNTL),                    \
	SR(DPSTREAMCLK_CNTL), SR(HDMISTREAMCLK_CNTL),                            \
	SR(SYMCLK32_SE_CNTL), SR(SYMCLK32_LE_CNTL),                              \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 0), DCCG_SRII(PIXEL_RATE_CNTL, OTG, 1),  \
	DCCG_SRII(PIXEL_RATE_CNTL, OTG, 2), DCCG_SRII(PIXEL_RATE_CNTL, OTG, 3),  \
	SR(OTG_PIXEL_RATE_DIV), SR(DTBCLK_P_CNTL),                               \
	SR(DCCG_AUDIO_DTO_SOURCE),                                               \
	SR(DPPCLK_CTRL),                                                         \
	DCCG_SRII(MODULO, DP_DTO, 0), DCCG_SRII(MODULO, DP_DTO, 1),              \
	DCCG_SRII(MODULO, DP_DTO, 2), DCCG_SRII(MODULO, DP_DTO, 3),              \
	DCCG_SRII(PHASE, DP_DTO, 0), DCCG_SRII(PHASE, DP_DTO, 1),                \
	DCCG_SRII(PHASE, DP_DTO, 2), DCCG_SRII(PHASE, DP_DTO, 3),                \
	SR(OTG_ADD_DROP_PIXEL_CNTL),                                             \
	SR(DSCCLK0_DTO_PARAM),                                                   \
	SR(DSCCLK1_DTO_PARAM),                                                   \
	SR(DSCCLK2_DTO_PARAM),                                                   \
	SR(DSCCLK3_DTO_PARAM),                                                   \
	SR(DSCCLK_DTO_CTRL),                                                     \
	SR(DCCG_GATE_DISABLE_CNTL),                                              \
	SR(DCCG_GATE_DISABLE_CNTL2),                                             \
	SR(DCCG_GATE_DISABLE_CNTL3),                                             \
	SR(DCCG_GATE_DISABLE_CNTL4),                                             \
	SR(DCCG_GATE_DISABLE_CNTL5),                                             \
	SR(DCCG_GATE_DISABLE_CNTL6),                                             \
	SR(SYMCLKA_CLOCK_ENABLE),                                                \
	SR(SYMCLKB_CLOCK_ENABLE),                                                \
	SR(SYMCLKC_CLOCK_ENABLE),                                                \
	SR(SYMCLKD_CLOCK_ENABLE),                                                \
	SR(DSCCLK_SRC_SEL)

/* HPO FRL stream encoder */
#define DCN60_HPO_FRL_STREAM_ENC_REG_LIST_RI(id)                                 \
	DCN3_0_HPO_FRL_STREAM_ENC_REG_LIST_RI(id),                               \
	SR_ARR(HDMI_STREAM_ENC_AUDIO_CONTROL, id)

#define DCN60_AUX_REG_LIST_RI(id, ddc_id)                                    \
	DCN2_AUX_REG_LIST_RI(id),                                                \
	SRI_ARR_DDC(DC_GPIO_DDC, MASK, ddc_id)

/* HPD */
#define HPD_REG_LIST_DCN60_RI(id)                                            \
	HPD_REG_LIST_RI(id),                                                     \
	SR_ARR(HPD_CTRL, id)

/* DSC */
#define DSC_REG_LIST_DCN60_RI(id)                                          \
	SRI_ARR(DSC_TOP_CONTROL, DSC_TOP, id),                                   \
	SRI_ARR(DSC_DEBUG_CONTROL, DSC_TOP, id),                                 \
	SRI_ARR(DSCC_CONFIG0, DSCC, id), SRI_ARR(DSCC_CONFIG1, DSCC, id),        \
	SRI_ARR(DSCC_STATUS, DSCC, id),                                          \
	SRI_ARR(DSCC_INTERRUPT_CONTROL0, DSCC, id),                              \
	SRI_ARR(DSCC_INTERRUPT_CONTROL1, DSCC, id),                              \
	SRI_ARR(DSCC_INTERRUPT_STATUS0, DSCC, id),                               \
	SRI_ARR(DSCC_INTERRUPT_STATUS1, DSCC, id),                               \
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
	SRI_ARR(DSCC_MEM_POWER_CONTROL0, DSCC, id),                              \
	SRI_ARR(DSCC_MEM_POWER_CONTROL1, DSCC, id),                              \
	SRI_ARR(DSCC_TEST_DEBUG_BUS_ROTATE, DSCC, id),                           \
	SRI_ARR(DSCRM_DSC_FORWARD_CONFIG, DSCRM, id)

/* OPTC */
#define OPTC_COMMON_REG_LIST_DCN60_RI(inst)                                      \
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
	SRI_ARR(OTG_PIPE_UPDATE_STATUS, OTG, inst),\
	SRI_ARR(INTERRUPT_DEST, OTG, inst)

#endif /* _DCN60_RESOURCE_H_ */
