/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - error values
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#ifndef ERROR_CODE_H_INCLUDED
#define ERROR_CODE_H_INCLUDED

/*
 * WAVE5
 */

/************************************************************************/
/* WAVE5 COMMON SYSTEM ERROR (FAIL_REASON)                              */
/************************************************************************/
#define WAVE5_SYSERR_QUEUEING_FAIL                                     0x00000001
#define WAVE5_SYSERR_ACCESS_VIOLATION_HW                               0x00000040
#define WAVE5_SYSERR_BUS_ERROR                                         0x00000200
#define WAVE5_SYSERR_DOUBLE_FAULT                                      0x00000400
#define WAVE5_SYSERR_RESULT_NOT_READY                                  0x00000800
#define WAVE5_SYSERR_VPU_STILL_RUNNING                                 0x00001000
#define WAVE5_SYSERR_UNKNOWN_CMD                                       0x00002000
#define WAVE5_SYSERR_UNKNOWN_CODEC_STD                                 0x00004000
#define WAVE5_SYSERR_UNKNOWN_QUERY_OPTION                              0x00008000
#define WAVE5_SYSERR_VLC_BUF_FULL                                      0x00010000
#define WAVE5_SYSERR_WATCHDOG_TIMEOUT                                  0x00020000
#define WAVE5_SYSERR_VCPU_TIMEOUT                                      0x00080000
#define WAVE5_SYSERR_TEMP_SEC_BUF_OVERFLOW                             0x00200000
#define WAVE5_SYSERR_NEED_MORE_TASK_BUF                                0x00400000
#define WAVE5_SYSERR_PRESCAN_ERR                                       0x00800000
#define WAVE5_SYSERR_ENC_GBIN_OVERCONSUME                              0x01000000
#define WAVE5_SYSERR_ENC_MAX_ZERO_DETECT                               0x02000000
#define WAVE5_SYSERR_ENC_LVL_FIRST_ERROR                               0x04000000
#define WAVE5_SYSERR_ENC_EG_RANGE_OVER                                 0x08000000
#define WAVE5_SYSERR_ENC_IRB_FRAME_DROP                                0x10000000
#define WAVE5_SYSERR_INPLACE_V                                         0x20000000
#define WAVE5_SYSERR_FATAL_VPU_HANGUP                                  0xf0000000

/************************************************************************/
/* WAVE5 COMMAND QUEUE ERROR (FAIL_REASON)                              */
/************************************************************************/
#define WAVE5_CMDQ_ERR_NOT_QUEABLE_CMD                                 0x00000001
#define WAVE5_CMDQ_ERR_SKIP_MODE_ENABLE                                0x00000002
#define WAVE5_CMDQ_ERR_INST_FLUSHING                                   0x00000003
#define WAVE5_CMDQ_ERR_INST_INACTIVE                                   0x00000004
#define WAVE5_CMDQ_ERR_QUEUE_FAIL                                      0x00000005
#define WAVE5_CMDQ_ERR_CMD_BUF_FULL                                    0x00000006

/************************************************************************/
/* WAVE5 ERROR ON DECODER (ERR_INFO)                                    */
/************************************************************************/
// HEVC
#define HEVC_SPSERR_SEQ_PARAMETER_SET_ID                               0x00001000
#define HEVC_SPSERR_CHROMA_FORMAT_IDC                                  0x00001001
#define HEVC_SPSERR_PIC_WIDTH_IN_LUMA_SAMPLES                          0x00001002
#define HEVC_SPSERR_PIC_HEIGHT_IN_LUMA_SAMPLES                         0x00001003
#define HEVC_SPSERR_CONF_WIN_LEFT_OFFSET                               0x00001004
#define HEVC_SPSERR_CONF_WIN_RIGHT_OFFSET                              0x00001005
#define HEVC_SPSERR_CONF_WIN_TOP_OFFSET                                0x00001006
#define HEVC_SPSERR_CONF_WIN_BOTTOM_OFFSET                             0x00001007
#define HEVC_SPSERR_BIT_DEPTH_LUMA_MINUS8                              0x00001008
#define HEVC_SPSERR_BIT_DEPTH_CHROMA_MINUS8                            0x00001009
#define HEVC_SPSERR_LOG2_MAX_PIC_ORDER_CNT_LSB_MINUS4                  0x0000100A
#define HEVC_SPSERR_SPS_MAX_DEC_PIC_BUFFERING                          0x0000100B
#define HEVC_SPSERR_SPS_MAX_NUM_REORDER_PICS                           0x0000100C
#define HEVC_SPSERR_SPS_MAX_LATENCY_INCREASE                           0x0000100D
#define HEVC_SPSERR_LOG2_MIN_LUMA_CODING_BLOCK_SIZE_MINUS3             0x0000100E
#define HEVC_SPSERR_LOG2_DIFF_MAX_MIN_LUMA_CODING_BLOCK_SIZE           0x0000100F
#define HEVC_SPSERR_LOG2_MIN_TRANSFORM_BLOCK_SIZE_MINUS2               0x00001010
#define HEVC_SPSERR_LOG2_DIFF_MAX_MIN_TRANSFORM_BLOCK_SIZE             0x00001011
#define HEVC_SPSERR_MAX_TRANSFORM_HIERARCHY_DEPTH_INTER                0x00001012
#define HEVC_SPSERR_MAX_TRANSFORM_HIERARCHY_DEPTH_INTRA                0x00001013
#define HEVC_SPSERR_SCALING_LIST                                       0x00001014
#define HEVC_SPSERR_LOG2_DIFF_MIN_PCM_LUMA_CODING_BLOCK_SIZE_MINUS3    0x00001015
#define HEVC_SPSERR_LOG2_DIFF_MAX_MIN_PCM_LUMA_CODING_BLOCK_SIZE       0x00001016
#define HEVC_SPSERR_NUM_SHORT_TERM_REF_PIC_SETS                        0x00001017
#define HEVC_SPSERR_NUM_LONG_TERM_REF_PICS_SPS                         0x00001018
#define HEVC_SPSERR_GBU_PARSING_ERROR                                  0x00001019
#define HEVC_SPSERR_EXTENSION_FLAG                                     0x0000101A
#define HEVC_SPSERR_VUI_ERROR                                          0x0000101B
#define HEVC_SPSERR_ACTIVATE_SPS                                       0x0000101C
#define HEVC_SPSERR_PROFILE_SPACE                                      0x0000101D
#define HEVC_PPSERR_PPS_PIC_PARAMETER_SET_ID                           0x00002000
#define HEVC_PPSERR_PPS_SEQ_PARAMETER_SET_ID                           0x00002001
#define HEVC_PPSERR_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1               0x00002002
#define HEVC_PPSERR_NUM_REF_IDX_L1_DEFAULT_ACTIVE_MINUS1               0x00002003
#define HEVC_PPSERR_INIT_QP_MINUS26                                    0x00002004
#define HEVC_PPSERR_DIFF_CU_QP_DELTA_DEPTH                             0x00002005
#define HEVC_PPSERR_PPS_CB_QP_OFFSET                                   0x00002006
#define HEVC_PPSERR_PPS_CR_QP_OFFSET                                   0x00002007
#define HEVC_PPSERR_NUM_TILE_COLUMNS_MINUS1                            0x00002008
#define HEVC_PPSERR_NUM_TILE_ROWS_MINUS1                               0x00002009
#define HEVC_PPSERR_COLUMN_WIDTH_MINUS1                                0x0000200A
#define HEVC_PPSERR_ROW_HEIGHT_MINUS1                                  0x0000200B
#define HEVC_PPSERR_PPS_BETA_OFFSET_DIV2                               0x0000200C
#define HEVC_PPSERR_PPS_TC_OFFSET_DIV2                                 0x0000200D
#define HEVC_PPSERR_SCALING_LIST                                       0x0000200E
#define HEVC_PPSERR_LOG2_PARALLEL_MERGE_LEVEL_MINUS2                   0x0000200F
#define HEVC_PPSERR_NUM_TILE_COLUMNS_RANGE_OUT                         0x00002010
#define HEVC_PPSERR_NUM_TILE_ROWS_RANGE_OUT                            0x00002011
#define HEVC_PPSERR_MORE_RBSP_DATA_ERROR                               0x00002012
#define HEVC_PPSERR_PPS_PIC_PARAMETER_SET_ID_RANGE_OUT                 0x00002013
#define HEVC_PPSERR_PPS_SEQ_PARAMETER_SET_ID_RANGE_OUT                 0x00002014
#define HEVC_PPSERR_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1_RANGE_OUT     0x00002015
#define HEVC_PPSERR_NUM_REF_IDX_L1_DEFAULT_ACTIVE_MINUS1_RANGE_OUT     0x00002016
#define HEVC_PPSERR_PPS_CB_QP_OFFSET_RANGE_OUT                         0x00002017
#define HEVC_PPSERR_PPS_CR_QP_OFFSET_RANGE_OUT                         0x00002018
#define HEVC_PPSERR_COLUMN_WIDTH_MINUS1_RANGE_OUT                      0x00002019
#define HEVC_PPSERR_ROW_HEIGHT_MINUS1_RANGE_OUT                        0x00002020
#define HEVC_PPSERR_PPS_BETA_OFFSET_DIV2_RANGE_OUT                     0x00002021
#define HEVC_PPSERR_PPS_TC_OFFSET_DIV2_RANGE_OUT                       0x00002022
#define HEVC_SHERR_SLICE_PIC_PARAMETER_SET_ID                          0x00003000
#define HEVC_SHERR_ACTIVATE_PPS                                        0x00003001
#define HEVC_SHERR_ACTIVATE_SPS                                        0x00003002
#define HEVC_SHERR_SLICE_TYPE                                          0x00003003
#define HEVC_SHERR_FIRST_SLICE_IS_DEPENDENT_SLICE                      0x00003004
#define HEVC_SHERR_SHORT_TERM_REF_PIC_SET_SPS_FLAG                     0x00003005
#define HEVC_SHERR_SHORT_TERM_REF_PIC_SET                              0x00003006
#define HEVC_SHERR_SHORT_TERM_REF_PIC_SET_IDX                          0x00003007
#define HEVC_SHERR_NUM_LONG_TERM_SPS                                   0x00003008
#define HEVC_SHERR_NUM_LONG_TERM_PICS                                  0x00003009
#define HEVC_SHERR_LT_IDX_SPS_IS_OUT_OF_RANGE                          0x0000300A
#define HEVC_SHERR_DELTA_POC_MSB_CYCLE_LT                              0x0000300B
#define HEVC_SHERR_NUM_REF_IDX_L0_ACTIVE_MINUS1                        0x0000300C
#define HEVC_SHERR_NUM_REF_IDX_L1_ACTIVE_MINUS1                        0x0000300D
#define HEVC_SHERR_COLLOCATED_REF_IDX                                  0x0000300E
#define HEVC_SHERR_PRED_WEIGHT_TABLE                                   0x0000300F
#define HEVC_SHERR_FIVE_MINUS_MAX_NUM_MERGE_CAND                       0x00003010
#define HEVC_SHERR_SLICE_QP_DELTA                                      0x00003011
#define HEVC_SHERR_SLICE_QP_DELTA_IS_OUT_OF_RANGE                      0x00003012
#define HEVC_SHERR_SLICE_CB_QP_OFFSET                                  0x00003013
#define HEVC_SHERR_SLICE_CR_QP_OFFSET                                  0x00003014
#define HEVC_SHERR_SLICE_BETA_OFFSET_DIV2                              0x00003015
#define HEVC_SHERR_SLICE_TC_OFFSET_DIV2                                0x00003016
#define HEVC_SHERR_NUM_ENTRY_POINT_OFFSETS                             0x00003017
#define HEVC_SHERR_OFFSET_LEN_MINUS1                                   0x00003018
#define HEVC_SHERR_SLICE_SEGMENT_HEADER_EXTENSION_LENGTH               0x00003019
#define HEVC_SHERR_WRONG_POC_IN_STILL_PICTURE_PROFILE                  0x0000301A
#define HEVC_SHERR_SLICE_TYPE_ERROR_IN_STILL_PICTURE_PROFILE           0x0000301B
#define HEVC_SHERR_PPS_ID_NOT_EQUAL_PREV_VALUE                         0x0000301C
#define HEVC_SPECERR_OVER_PICTURE_WIDTH_SIZE                           0x00004000
#define HEVC_SPECERR_OVER_PICTURE_HEIGHT_SIZE                          0x00004001
#define HEVC_SPECERR_OVER_CHROMA_FORMAT                                0x00004002
#define HEVC_SPECERR_OVER_BIT_DEPTH                                    0x00004003
#define HEVC_SPECERR_OVER_BUFFER_OVER_FLOW                             0x00004004
#define HEVC_SPECERR_OVER_WRONG_BUFFER_ACCESS                          0x00004005
#define HEVC_ETCERR_INIT_SEQ_SPS_NOT_FOUND                             0x00005000
#define HEVC_ETCERR_DEC_PIC_VCL_NOT_FOUND                              0x00005001
#define HEVC_ETCERR_NO_VALID_SLICE_IN_AU                               0x00005002
#define HEVC_ETCERR_INPLACE_V                                          0x0000500F

// AVC
#define AVC_SPSERR_SEQ_PARAMETER_SET_ID                                0x00001000
#define AVC_SPSERR_CHROMA_FORMAT_IDC                                   0x00001001
#define AVC_SPSERR_PIC_WIDTH_IN_LUMA_SAMPLES                           0x00001002
#define AVC_SPSERR_PIC_HEIGHT_IN_LUMA_SAMPLES                          0x00001003
#define AVC_SPSERR_CONF_WIN_LEFT_OFFSET                                0x00001004
#define AVC_SPSERR_CONF_WIN_RIGHT_OFFSET                               0x00001005
#define AVC_SPSERR_CONF_WIN_TOP_OFFSET                                 0x00001006
#define AVC_SPSERR_CONF_WIN_BOTTOM_OFFSET                              0x00001007
#define AVC_SPSERR_BIT_DEPTH_LUMA_MINUS8                               0x00001008
#define AVC_SPSERR_BIT_DEPTH_CHROMA_MINUS8                             0x00001009
#define AVC_SPSERR_SPS_MAX_DEC_PIC_BUFFERING                           0x0000100B
#define AVC_SPSERR_SPS_MAX_NUM_REORDER_PICS                            0x0000100C
#define AVC_SPSERR_SCALING_LIST                                        0x00001014
#define AVC_SPSERR_GBU_PARSING_ERROR                                   0x00001019
#define AVC_SPSERR_VUI_ERROR                                           0x0000101B
#define AVC_SPSERR_ACTIVATE_SPS                                        0x0000101C
#define AVC_PPSERR_PPS_PIC_PARAMETER_SET_ID                            0x00002000
#define AVC_PPSERR_PPS_SEQ_PARAMETER_SET_ID                            0x00002001
#define AVC_PPSERR_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1                0x00002002
#define AVC_PPSERR_NUM_REF_IDX_L1_DEFAULT_ACTIVE_MINUS1                0x00002003
#define AVC_PPSERR_INIT_QP_MINUS26                                     0x00002004
#define AVC_PPSERR_PPS_CB_QP_OFFSET                                    0x00002006
#define AVC_PPSERR_PPS_CR_QP_OFFSET                                    0x00002007
#define AVC_PPSERR_SCALING_LIST                                        0x0000200E
#define AVC_PPSERR_MORE_RBSP_DATA_ERROR                                0x00002012
#define AVC_PPSERR_PPS_PIC_PARAMETER_SET_ID_RANGE_OUT                  0x00002013
#define AVC_PPSERR_PPS_SEQ_PARAMETER_SET_ID_RANGE_OUT                  0x00002014
#define AVC_PPSERR_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1_RANGE_OUT      0x00002015
#define AVC_PPSERR_NUM_REF_IDX_L1_DEFAULT_ACTIVE_MINUS1_RANGE_OUT      0x00002016
#define AVC_PPSERR_PPS_CB_QP_OFFSET_RANGE_OUT                          0x00002017
#define AVC_PPSERR_PPS_CR_QP_OFFSET_RANGE_OUT                          0x00002018
#define AVC_SHERR_SLICE_PIC_PARAMETER_SET_ID                           0x00003000
#define AVC_SHERR_ACTIVATE_PPS                                         0x00003001
#define AVC_SHERR_ACTIVATE_SPS                                         0x00003002
#define AVC_SHERR_SLICE_TYPE                                           0x00003003
#define AVC_SHERR_FIRST_MB_IN_SLICE                                    0x00003004
#define AVC_SHERR_RPLM                                                 0x00003006
#define AVC_SHERR_LT_IDX_SPS_IS_OUT_OF_RANGE                           0x0000300A
#define AVC_SHERR_NUM_REF_IDX_L0_ACTIVE_MINUS1                         0x0000300C
#define AVC_SHERR_NUM_REF_IDX_L1_ACTIVE_MINUS1                         0x0000300D
#define AVC_SHERR_PRED_WEIGHT_TABLE                                    0x0000300F
#define AVC_SHERR_SLICE_QP_DELTA                                       0x00003011
#define AVC_SHERR_SLICE_BETA_OFFSET_DIV2                               0x00003015
#define AVC_SHERR_SLICE_TC_OFFSET_DIV2                                 0x00003016
#define AVC_SHERR_DISABLE_DEBLOCK_FILTER_IDC                           0x00003017
#define AVC_SPECERR_OVER_PICTURE_WIDTH_SIZE                            0x00004000
#define AVC_SPECERR_OVER_PICTURE_HEIGHT_SIZE                           0x00004001
#define AVC_SPECERR_OVER_CHROMA_FORMAT                                 0x00004002
#define AVC_SPECERR_OVER_BIT_DEPTH                                     0x00004003
#define AVC_SPECERR_OVER_BUFFER_OVER_FLOW                              0x00004004
#define AVC_SPECERR_OVER_WRONG_BUFFER_ACCESS                           0x00004005
#define AVC_ETCERR_INIT_SEQ_SPS_NOT_FOUND                              0x00005000
#define AVC_ETCERR_DEC_PIC_VCL_NOT_FOUND                               0x00005001
#define AVC_ETCERR_NO_VALID_SLICE_IN_AU                                0x00005002
#define AVC_ETCERR_ASO                                                 0x00005004
#define AVC_ETCERR_FMO                                                 0x00005005
#define AVC_ETCERR_INPLACE_V                                           0x0000500F

/************************************************************************/
/* WAVE5 WARNING ON DECODER (WARN_INFO)                                 */
/************************************************************************/
// HEVC
#define HEVC_SPSWARN_MAX_SUB_LAYERS_MINUS1                             0x00000001
#define HEVC_SPSWARN_GENERAL_RESERVED_ZERO_44BITS                      0x00000002
#define HEVC_SPSWARN_RESERVED_ZERO_2BITS                               0x00000004
#define HEVC_SPSWARN_SUB_LAYER_RESERVED_ZERO_44BITS                    0x00000008
#define HEVC_SPSWARN_GENERAL_LEVEL_IDC                                 0x00000010
#define HEVC_SPSWARN_SPS_MAX_DEC_PIC_BUFFERING_VALUE_OVER              0x00000020
#define HEVC_SPSWARN_RBSP_TRAILING_BITS                                0x00000040
#define HEVC_SPSWARN_ST_RPS_UE_ERROR                                   0x00000080
#define HEVC_SPSWARN_EXTENSION_FLAG                                    0x01000000
#define HEVC_SPSWARN_REPLACED_WITH_PREV_SPS                            0x02000000
#define HEVC_PPSWARN_RBSP_TRAILING_BITS                                0x00000100
#define HEVC_PPSWARN_REPLACED_WITH_PREV_PPS                            0x00000200
#define HEVC_SHWARN_FIRST_SLICE_SEGMENT_IN_PIC_FLAG                    0x00001000
#define HEVC_SHWARN_NO_OUTPUT_OF_PRIOR_PICS_FLAG                       0x00002000
#define HEVC_SHWARN_PIC_OUTPUT_FLAG                                    0x00004000
#define HEVC_SHWARN_DUPLICATED_SLICE_SEGMENT                           0x00008000
#define HEVC_ETCWARN_INIT_SEQ_VCL_NOT_FOUND                            0x00010000
#define HEVC_ETCWARN_MISSING_REFERENCE_PICTURE                         0x00020000
#define HEVC_ETCWARN_WRONG_TEMPORAL_ID                                 0x00040000
#define HEVC_ETCWARN_ERROR_PICTURE_IS_REFERENCED                       0x00080000
#define HEVC_SPECWARN_OVER_PROFILE                                     0x00100000
#define HEVC_SPECWARN_OVER_LEVEL                                       0x00200000
#define HEVC_PRESWARN_PARSING_ERR                                      0x04000000
#define HEVC_PRESWARN_MVD_OUT_OF_RANGE                                 0x08000000
#define HEVC_PRESWARN_CU_QP_DELTA_VAL_OUT_OF_RANGE                     0x09000000
#define HEVC_PRESWARN_COEFF_LEVEL_REMAINING_OUT_OF_RANGE               0x0A000000
#define HEVC_PRESWARN_PCM_ERR                                          0x0B000000
#define HEVC_PRESWARN_OVERCONSUME                                      0x0C000000
#define HEVC_PRESWARN_END_OF_SUBSET_ONE_BIT_ERR                        0x10000000
#define HEVC_PRESWARN_END_OF_SLICE_SEGMENT_FLAG                        0x20000000

// AVC
#define AVC_SPSWARN_RESERVED_ZERO_2BITS                                0x00000004
#define AVC_SPSWARN_GENERAL_LEVEL_IDC                                  0x00000010
#define AVC_SPSWARN_RBSP_TRAILING_BITS                                 0x00000040
#define AVC_PPSWARN_RBSP_TRAILING_BITS                                 0x00000100
#define AVC_SHWARN_NO_OUTPUT_OF_PRIOR_PICS_FLAG                        0x00002000
#define AVC_ETCWARN_INIT_SEQ_VCL_NOT_FOUND                             0x00010000
#define AVC_ETCWARN_MISSING_REFERENCE_PICTURE                          0x00020000
#define AVC_ETCWARN_ERROR_PICTURE_IS_REFERENCED                        0x00080000
#define AVC_SPECWARN_OVER_PROFILE                                      0x00100000
#define AVC_SPECWARN_OVER_LEVEL                                        0x00200000
#define AVC_PRESWARN_MVD_RANGE_OUT                                     0x00400000
#define AVC_PRESWARN_MB_QPD_RANGE_OUT                                  0x00500000
#define AVC_PRESWARN_COEFF_RANGE_OUT                                   0x00600000
#define AVC_PRESWARN_MV_RANGE_OUT                                      0x00700000
#define AVC_PRESWARN_MB_SKIP_RUN_RANGE_OUT                             0x00800000
#define AVC_PRESWARN_MB_TYPE_RANGE_OUT                                 0x00900000
#define AVC_PRESWARN_SUB_MB_TYPE_RANGE_OUT                             0x00A00000
#define AVC_PRESWARN_CBP_RANGE_OUT                                     0x00B00000
#define AVC_PRESWARN_INTRA_CHROMA_PRED_MODE_RANGE_OUT                  0x00C00000
#define AVC_PRESWARN_REF_IDX_RANGE_OUT                                 0x00D00000
#define AVC_PRESWARN_COEFF_TOKEN_RANGE_OUT                             0x00E00000
#define AVC_PRESWARN_TOTAL_ZERO_RANGE_OUT                              0x00F00000
#define AVC_PRESWARN_RUN_BEFORE_RANGE_OUT                              0x01000000
#define AVC_PRESWARN_OVERCONSUME                                       0x01100000
#define AVC_PRESWARN_MISSING_SLICE                                     0x01200000

/************************************************************************/
/* WAVE5 ERROR ON ENCODER (ERR_INFO)                                    */
/************************************************************************/

/************************************************************************/
/* WAVE5 WARNING ON ENCODER (WARN_INFO)                                 */
/************************************************************************/
#define WAVE5_ETCWARN_FORCED_SPLIT_BY_CU8X8                            0x000000001

/************************************************************************/
/* WAVE5 debug info (PRI_REASON)                                        */
/************************************************************************/
#define WAVE5_DEC_VCORE_VCE_HANGUP                                     0x0001
#define WAVE5_DEC_VCORE_UNDETECTED_SYNTAX_ERR                          0x0002
#define WAVE5_DEC_VCORE_MIB_BUSY                                       0x0003
#define WAVE5_DEC_VCORE_VLC_BUSY                                       0x0004

#endif /* ERROR_CODE_H_INCLUDED */
