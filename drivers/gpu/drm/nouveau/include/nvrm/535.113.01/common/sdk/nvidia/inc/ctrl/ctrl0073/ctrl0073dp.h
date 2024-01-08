#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073dp_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073dp_h__
#include <nvrm/535.113.01/common/sdk/nvidia/inc/ctrl/ctrl0073/ctrl0073common.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2005-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define NV0073_CTRL_CMD_DP_AUXCH_CTRL      (0x731341U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_AUXCH_CTRL_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_DP_AUXCH_MAX_DATA_SIZE 16U

typedef struct NV0073_CTRL_DP_AUXCH_CTRL_PARAMS {
    NvU32  subDeviceInstance;
    NvU32  displayId;
    NvBool bAddrOnly;
    NvU32  cmd;
    NvU32  addr;
    NvU8   data[NV0073_CTRL_DP_AUXCH_MAX_DATA_SIZE];
    NvU32  size;
    NvU32  replyType;
    NvU32  retryTimeMs;
} NV0073_CTRL_DP_AUXCH_CTRL_PARAMS;

#define NV0073_CTRL_DP_AUXCH_CMD_TYPE                          3:3
#define NV0073_CTRL_DP_AUXCH_CMD_TYPE_I2C               (0x00000000U)
#define NV0073_CTRL_DP_AUXCH_CMD_TYPE_AUX               (0x00000001U)
#define NV0073_CTRL_DP_AUXCH_CMD_I2C_MOT                       2:2
#define NV0073_CTRL_DP_AUXCH_CMD_I2C_MOT_FALSE          (0x00000000U)
#define NV0073_CTRL_DP_AUXCH_CMD_I2C_MOT_TRUE           (0x00000001U)
#define NV0073_CTRL_DP_AUXCH_CMD_REQ_TYPE                      1:0
#define NV0073_CTRL_DP_AUXCH_CMD_REQ_TYPE_WRITE         (0x00000000U)
#define NV0073_CTRL_DP_AUXCH_CMD_REQ_TYPE_READ          (0x00000001U)
#define NV0073_CTRL_DP_AUXCH_CMD_REQ_TYPE_WRITE_STATUS  (0x00000002U)

#define NV0073_CTRL_CMD_DP_CTRL                     (0x731343U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_CTRL_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_DP_CTRL_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 cmd;
    NvU32 data;
    NvU32 err;
    NvU32 retryTimeMs;
    NvU32 eightLaneDpcdBaseAddr;
} NV0073_CTRL_DP_CTRL_PARAMS;

#define NV0073_CTRL_DP_CMD_SET_LANE_COUNT                           0:0
#define NV0073_CTRL_DP_CMD_SET_LANE_COUNT_FALSE                         (0x00000000U)
#define NV0073_CTRL_DP_CMD_SET_LANE_COUNT_TRUE                          (0x00000001U)
#define NV0073_CTRL_DP_CMD_SET_LINK_BW                              1:1
#define NV0073_CTRL_DP_CMD_SET_LINK_BW_FALSE                            (0x00000000U)
#define NV0073_CTRL_DP_CMD_SET_LINK_BW_TRUE                             (0x00000001U)
#define NV0073_CTRL_DP_CMD_DISABLE_DOWNSPREAD                       2:2
#define NV0073_CTRL_DP_CMD_DISABLE_DOWNSPREAD_FALSE                     (0x00000000U)
#define NV0073_CTRL_DP_CMD_DISABLE_DOWNSPREAD_TRUE                      (0x00000001U)
#define NV0073_CTRL_DP_CMD_UNUSED                                   3:3
#define NV0073_CTRL_DP_CMD_SET_FORMAT_MODE                          4:4
#define NV0073_CTRL_DP_CMD_SET_FORMAT_MODE_SINGLE_STREAM                (0x00000000U)
#define NV0073_CTRL_DP_CMD_SET_FORMAT_MODE_MULTI_STREAM                 (0x00000001U)
#define NV0073_CTRL_DP_CMD_FAST_LINK_TRAINING                       5:5
#define NV0073_CTRL_DP_CMD_FAST_LINK_TRAINING_NO                        (0x00000000U)
#define NV0073_CTRL_DP_CMD_FAST_LINK_TRAINING_YES                       (0x00000001U)
#define NV0073_CTRL_DP_CMD_NO_LINK_TRAINING                         6:6
#define NV0073_CTRL_DP_CMD_NO_LINK_TRAINING_NO                          (0x00000000U)
#define NV0073_CTRL_DP_CMD_NO_LINK_TRAINING_YES                         (0x00000001U)
#define NV0073_CTRL_DP_CMD_SET_ENHANCED_FRAMING                     7:7
#define NV0073_CTRL_DP_CMD_SET_ENHANCED_FRAMING_FALSE                   (0x00000000U)
#define NV0073_CTRL_DP_CMD_SET_ENHANCED_FRAMING_TRUE                    (0x00000001U)
#define NV0073_CTRL_DP_CMD_USE_DOWNSPREAD_SETTING                   8:8
#define NV0073_CTRL_DP_CMD_USE_DOWNSPREAD_SETTING_DEFAULT               (0x00000000U)
#define NV0073_CTRL_DP_CMD_USE_DOWNSPREAD_SETTING_FORCE                 (0x00000001U)
#define NV0073_CTRL_DP_CMD_SKIP_HW_PROGRAMMING                      9:9
#define NV0073_CTRL_DP_CMD_SKIP_HW_PROGRAMMING_NO                       (0x00000000U)
#define NV0073_CTRL_DP_CMD_SKIP_HW_PROGRAMMING_YES                      (0x00000001U)
#define NV0073_CTRL_DP_CMD_POST_LT_ADJ_REQ_GRANTED                10:10
#define NV0073_CTRL_DP_CMD_POST_LT_ADJ_REQ_GRANTED_NO                   (0x00000000U)
#define NV0073_CTRL_DP_CMD_POST_LT_ADJ_REQ_GRANTED_YES                  (0x00000001U)
#define NV0073_CTRL_DP_CMD_FAKE_LINK_TRAINING                     12:11
#define NV0073_CTRL_DP_CMD_FAKE_LINK_TRAINING_NO                        (0x00000000U)
#define NV0073_CTRL_DP_CMD_FAKE_LINK_TRAINING_DONOT_TOGGLE_TRANSMISSION (0x00000001U)
#define NV0073_CTRL_DP_CMD_FAKE_LINK_TRAINING_TOGGLE_TRANSMISSION_ON    (0x00000002U)
#define NV0073_CTRL_DP_CMD_TRAIN_PHY_REPEATER                     13:13
#define NV0073_CTRL_DP_CMD_TRAIN_PHY_REPEATER_NO                        (0x00000000U)
#define NV0073_CTRL_DP_CMD_TRAIN_PHY_REPEATER_YES                       (0x00000001U)
#define NV0073_CTRL_DP_CMD_FALLBACK_CONFIG                        14:14
#define NV0073_CTRL_DP_CMD_FALLBACK_CONFIG_FALSE                        (0x00000000U)
#define NV0073_CTRL_DP_CMD_FALLBACK_CONFIG_TRUE                         (0x00000001U)
#define NV0073_CTRL_DP_CMD_ENABLE_FEC                             15:15
#define NV0073_CTRL_DP_CMD_ENABLE_FEC_FALSE                             (0x00000000U)
#define NV0073_CTRL_DP_CMD_ENABLE_FEC_TRUE                              (0x00000001U)

#define NV0073_CTRL_DP_CMD_BANDWIDTH_TEST                         29:29
#define NV0073_CTRL_DP_CMD_BANDWIDTH_TEST_NO                            (0x00000000U)
#define NV0073_CTRL_DP_CMD_BANDWIDTH_TEST_YES                           (0x00000001U)
#define NV0073_CTRL_DP_CMD_LINK_CONFIG_CHECK_DISABLE              30:30
#define NV0073_CTRL_DP_CMD_LINK_CONFIG_CHECK_DISABLE_FALSE              (0x00000000U)
#define NV0073_CTRL_DP_CMD_LINK_CONFIG_CHECK_DISABLE_TRUE               (0x00000001U)
#define NV0073_CTRL_DP_CMD_DISABLE_LINK_CONFIG                    31:31
#define NV0073_CTRL_DP_CMD_DISABLE_LINK_CONFIG_FALSE                    (0x00000000U)
#define NV0073_CTRL_DP_CMD_DISABLE_LINK_CONFIG_TRUE                     (0x00000001U)

#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT                          4:0
#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT_0                            (0x00000000U)
#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT_1                            (0x00000001U)
#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT_2                            (0x00000002U)
#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT_4                            (0x00000004U)
#define NV0073_CTRL_DP_DATA_SET_LANE_COUNT_8                            (0x00000008U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW                            15:8
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_1_62GBPS                        (0x00000006U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_2_16GBPS                        (0x00000008U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_2_43GBPS                        (0x00000009U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_2_70GBPS                        (0x0000000AU)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_3_24GBPS                        (0x0000000CU)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_4_32GBPS                        (0x00000010U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_5_40GBPS                        (0x00000014U)
#define NV0073_CTRL_DP_DATA_SET_LINK_BW_8_10GBPS                        (0x0000001EU)
#define NV0073_CTRL_DP_DATA_SET_ENHANCED_FRAMING                  18:18
#define NV0073_CTRL_DP_DATA_SET_ENHANCED_FRAMING_NO                     (0x00000000U)
#define NV0073_CTRL_DP_DATA_SET_ENHANCED_FRAMING_YES                    (0x00000001U)
#define NV0073_CTRL_DP_DATA_TARGET                                22:19
#define NV0073_CTRL_DP_DATA_TARGET_SINK                                 (0x00000000U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_0                       (0x00000001U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_1                       (0x00000002U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_2                       (0x00000003U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_3                       (0x00000004U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_4                       (0x00000005U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_5                       (0x00000006U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_6                       (0x00000007U)
#define NV0073_CTRL_DP_DATA_TARGET_PHY_REPEATER_7                       (0x00000008U)

#define NV0073_CTRL_MAX_LANES                                           8U

typedef struct NV0073_CTRL_DP_LANE_DATA_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 numLanes;
    NvU32 data[NV0073_CTRL_MAX_LANES];
} NV0073_CTRL_DP_LANE_DATA_PARAMS;

#define NV0073_CTRL_DP_LANE_DATA_PREEMPHASIS                   1:0
#define NV0073_CTRL_DP_LANE_DATA_PREEMPHASIS_NONE    (0x00000000U)
#define NV0073_CTRL_DP_LANE_DATA_PREEMPHASIS_LEVEL1  (0x00000001U)
#define NV0073_CTRL_DP_LANE_DATA_PREEMPHASIS_LEVEL2  (0x00000002U)
#define NV0073_CTRL_DP_LANE_DATA_PREEMPHASIS_LEVEL3  (0x00000003U)
#define NV0073_CTRL_DP_LANE_DATA_DRIVECURRENT                  3:2
#define NV0073_CTRL_DP_LANE_DATA_DRIVECURRENT_LEVEL0 (0x00000000U)
#define NV0073_CTRL_DP_LANE_DATA_DRIVECURRENT_LEVEL1 (0x00000001U)
#define NV0073_CTRL_DP_LANE_DATA_DRIVECURRENT_LEVEL2 (0x00000002U)
#define NV0073_CTRL_DP_LANE_DATA_DRIVECURRENT_LEVEL3 (0x00000003U)

#define NV0073_CTRL_CMD_DP_SET_LANE_DATA (0x731346U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_SET_LANE_DATA_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_CMD_DP_SET_AUDIO_MUTESTREAM      (0x731359U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 mute;
} NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS;

#define NV0073_CTRL_CMD_DP_TOPOLOGY_ALLOCATE_DISPLAYID  (0x73135bU) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_TOPOLOGY_ALLOCATE_DISPLAYID_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_CMD_DP_TOPOLOGY_ALLOCATE_DISPLAYID_PARAMS {
    NvU32  subDeviceInstance;
    NvU32  displayId;
    NvU32  preferredDisplayId;

    NvBool force;
    NvBool useBFM;

    NvU32  displayIdAssigned;
    NvU32  allDisplayMask;
} NV0073_CTRL_CMD_DP_TOPOLOGY_ALLOCATE_DISPLAYID_PARAMS;

#define NV0073_CTRL_CMD_DP_TOPOLOGY_FREE_DISPLAYID (0x73135cU) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_TOPOLOGY_FREE_DISPLAYID_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_CMD_DP_TOPOLOGY_FREE_DISPLAYID_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
} NV0073_CTRL_CMD_DP_TOPOLOGY_FREE_DISPLAYID_PARAMS;

#define NV0073_CTRL_CMD_DP_CONFIG_STREAM                   (0x731362U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_CONFIG_STREAM_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_CMD_DP_CONFIG_STREAM_PARAMS {
    NvU32  subDeviceInstance;
    NvU32  head;
    NvU32  sorIndex;
    NvU32  dpLink;

    NvBool bEnableOverride;
    NvBool bMST;
    NvU32  singleHeadMultistreamMode;
    NvU32  hBlankSym;
    NvU32  vBlankSym;
    NvU32  colorFormat;
    NvBool bEnableTwoHeadOneOr;

    struct {
        NvU32  slotStart;
        NvU32  slotEnd;
        NvU32  PBN;
        NvU32  Timeslice;
        NvBool sendACT;          // deprecated -Use NV0073_CTRL_CMD_DP_SEND_ACT
        NvU32  singleHeadMSTPipeline;
        NvBool bEnableAudioOverRightPanel;
    } MST;

    struct {
        NvBool bEnhancedFraming;
        NvU32  tuSize;
        NvU32  waterMark;
        NvU32  actualPclkHz;     // deprecated  -Use MvidWarParams
        NvU32  linkClkFreqHz;    // deprecated  -Use MvidWarParams
        NvBool bEnableAudioOverRightPanel;
        struct {
            NvU32  activeCnt;
            NvU32  activeFrac;
            NvU32  activePolarity;
            NvBool mvidWarEnabled;
            struct {
                NvU32 actualPclkHz;
                NvU32 linkClkFreqHz;
            } MvidWarParams;
        } Legacy;
    } SST;
} NV0073_CTRL_CMD_DP_CONFIG_STREAM_PARAMS;

#define NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT                    (0x731365U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT_PARAMS {
    NvU32 subDeviceInstance;
} NV0073_CTRL_CMD_DP_SET_MANUAL_DISPLAYPORT_PARAMS;

#define NV0073_CTRL_CMD_DP_GET_CAPS   (0x731369U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS_MESSAGE_ID (0x69U)

typedef struct NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS {
    NvU32                          subDeviceInstance;
    NvU32                          sorIndex;
    NvU32                          maxLinkRate;
    NvU32                          dpVersionsSupported;
    NvU32                          UHBRSupported;
    NvBool                         bIsMultistreamSupported;
    NvBool                         bIsSCEnabled;
    NvBool                         bHasIncreasedWatermarkLimits;
    NvBool                         bIsPC2Disabled;
    NvBool                         isSingleHeadMSTSupported;
    NvBool                         bFECSupported;
    NvBool                         bIsTrainPhyRepeater;
    NvBool                         bOverrideLinkBw;
    NV0073_CTRL_CMD_DSC_CAP_PARAMS DSC;
} NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS;

#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2                0:0
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2_NO              (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2_YES             (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4                1:1
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4_NO              (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4_YES             (0x00000001U)

#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE                           2:0
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_NONE                          (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_1_62                          (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_2_70                          (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_5_40                          (0x00000003U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_8_10                          (0x00000004U)

#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_RGB                (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_444        (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_NATIVE_422 (0x00000004U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_NATIVE_420 (0x00000008U)

#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_16           (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_8            (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_4            (0x00000003U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_2            (0x00000004U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1              (0x00000005U)

#define NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES (0x731377U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES        8U

typedef struct NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS {
    // In
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU16 linkRateTbl[NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES];

    // Out
    NvU8  linkBwTbl[NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES];
    NvU8  linkBwCount;
} NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS;

#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE                                   3:0
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_MONITOR_ENABLE_BEGIN     (0x00000000U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_MONITOR_ENABLE_CHALLENGE (0x00000001U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_MONITOR_ENABLE_CHECK     (0x00000002U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_DRIVER_ENABLE_BEGIN      (0x00000003U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_DRIVER_ENABLE_CHALLENGE  (0x00000004U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_DRIVER_ENABLE_CHECK      (0x00000005U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_RESET_MONITOR            (0x00000006U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_INIT_PUBLIC_INFO         (0x00000007U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_GET_PUBLIC_INFO          (0x00000008U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_CMD_STAGE_STATUS_CHECK             (0x00000009U)

#define NV0073_CTRL_DP_CMD_ENABLE_VRR_STATUS_OK                          (0x00000000U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_STATUS_PENDING                     (0x80000001U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_STATUS_READ_ERROR                  (0x80000002U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_STATUS_WRITE_ERROR                 (0x80000003U)
#define NV0073_CTRL_DP_CMD_ENABLE_VRR_STATUS_DEVICE_ERROR                (0x80000004U)

#endif
