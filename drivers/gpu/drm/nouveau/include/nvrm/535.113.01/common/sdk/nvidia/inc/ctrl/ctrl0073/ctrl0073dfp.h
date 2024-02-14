#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073dfp_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073dfp_h__

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

#define NV0073_CTRL_CMD_DFP_GET_INFO (0x731140U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DFP_INTERFACE_ID << 8) | NV0073_CTRL_DFP_GET_INFO_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_DFP_GET_INFO_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 flags;
    NvU32 flags2;
} NV0073_CTRL_DFP_GET_INFO_PARAMS;

#define NV0073_CTRL_DFP_FLAGS_SIGNAL                                       2:0
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_TMDS                       (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_LVDS                       (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_SDI                        (0x00000002U)
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_DISPLAYPORT                (0x00000003U)
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_DSI                        (0x00000004U)
#define NV0073_CTRL_DFP_FLAGS_SIGNAL_WRBK                       (0x00000005U)
#define NV0073_CTRL_DFP_FLAGS_LANE                                         5:3
#define NV0073_CTRL_DFP_FLAGS_LANE_NONE                         (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_LANE_SINGLE                       (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_LANE_DUAL                         (0x00000002U)
#define NV0073_CTRL_DFP_FLAGS_LANE_QUAD                         (0x00000003U)
#define NV0073_CTRL_DFP_FLAGS_LANE_OCT                          (0x00000004U)
#define NV0073_CTRL_DFP_FLAGS_LIMIT                                        6:6
#define NV0073_CTRL_DFP_FLAGS_LIMIT_DISABLE                     (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_LIMIT_60HZ_RR                     (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_SLI_SCALER                                   7:7
#define NV0073_CTRL_DFP_FLAGS_SLI_SCALER_NORMAL                 (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_SLI_SCALER_DISABLE                (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_HDMI_CAPABLE                                 8:8
#define NV0073_CTRL_DFP_FLAGS_HDMI_CAPABLE_FALSE                (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_HDMI_CAPABLE_TRUE                 (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_RANGE_LIMITED_CAPABLE                        9:9
#define NV0073_CTRL_DFP_FLAGS_RANGE_LIMITED_CAPABLE_FALSE       (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_RANGE_LIMITED_CAPABLE_TRUE        (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_RANGE_AUTO_CAPABLE                         10:10
#define NV0073_CTRL_DFP_FLAGS_RANGE_AUTO_CAPABLE_FALSE          (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_RANGE_AUTO_CAPABLE_TRUE           (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR422_CAPABLE                    11:11
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR422_CAPABLE_FALSE     (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR422_CAPABLE_TRUE      (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR444_CAPABLE                    12:12
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR444_CAPABLE_FALSE     (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_FORMAT_YCBCR444_CAPABLE_TRUE      (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_HDMI_ALLOWED                               14:14
#define NV0073_CTRL_DFP_FLAGS_HDMI_ALLOWED_FALSE                (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_HDMI_ALLOWED_TRUE                 (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_EMBEDDED_DISPLAYPORT                       15:15
#define NV0073_CTRL_DFP_FLAGS_EMBEDDED_DISPLAYPORT_FALSE        (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_EMBEDDED_DISPLAYPORT_TRUE         (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_CONSTRAINT                         16:16
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_CONSTRAINT_NONE           (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_CONSTRAINT_PREFER_RBR     (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_BW                                 19:17
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_BW_1_62GBPS               (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_BW_2_70GBPS               (0x00000002U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_BW_5_40GBPS               (0x00000003U)
#define NV0073_CTRL_DFP_FLAGS_DP_LINK_BW_8_10GBPS               (0x00000004U)
#define NV0073_CTRL_DFP_FLAGS_LINK                                       21:20
#define NV0073_CTRL_DFP_FLAGS_LINK_NONE                         (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_LINK_SINGLE                       (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_LINK_DUAL                         (0x00000002U)
#define NV0073_CTRL_DFP_FLAGS_DP_FORCE_RM_EDID                           22:22
#define NV0073_CTRL_DFP_FLAGS_DP_FORCE_RM_EDID_FALSE            (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_DP_FORCE_RM_EDID_TRUE             (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DSI_DEVICE_ID                              24:23
#define NV0073_CTRL_DFP_FLAGS_DSI_DEVICE_ID_DSI_NONE            (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_DSI_DEVICE_ID_DSI_A               (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DSI_DEVICE_ID_DSI_B               (0x00000002U)
#define NV0073_CTRL_DFP_FLAGS_DSI_DEVICE_ID_DSI_GANGED          (0x00000003U)
#define NV0073_CTRL_DFP_FLAGS_DP_POST_CURSOR2_DISABLED                   25:25
#define NV0073_CTRL_DFP_FLAGS_DP_POST_CURSOR2_DISABLED_FALSE    (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_DP_POST_CURSOR2_DISABLED_TRUE     (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS_DP_PHY_REPEATER_COUNT                      29:26
#define NV0073_CTRL_DFP_FLAGS_DYNAMIC_MUX_CAPABLE                        30:30
#define NV0073_CTRL_DFP_FLAGS_DYNAMIC_MUX_CAPABLE_FALSE         (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_DYNAMIC_MUX_CAPABLE_TRUE          (0x00000001U)

#define NV0073_CTRL_CMD_DFP_SET_ELD_AUDIO_CAPS                         (0x731144U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DFP_INTERFACE_ID << 8) | NV0073_CTRL_DFP_SET_ELD_AUDIO_CAP_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_ELD_BUFFER                      96U

typedef struct NV0073_CTRL_DFP_SET_ELD_AUDIO_CAP_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 numELDSize;
    NvU8  bufferELD[NV0073_CTRL_DFP_ELD_AUDIO_CAPS_ELD_BUFFER];
    NvU32 maxFreqSupported;
    NvU32 ctrl;
    NvU32 deviceEntry;
} NV0073_CTRL_DFP_SET_ELD_AUDIO_CAP_PARAMS;

#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_PD                                     0:0
#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_PD_FALSE              (0x00000000U)
#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_PD_TRUE               (0x00000001U)
#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_ELDV                                   1:1
#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_ELDV_FALSE            (0x00000000U)
#define NV0073_CTRL_DFP_ELD_AUDIO_CAPS_CTRL_ELDV_TRUE             (0x00000001U)

#define NV0073_CTRL_CMD_DFP_SET_AUDIO_ENABLE                (0x731150U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DFP_INTERFACE_ID << 8) | NV0073_CTRL_DFP_SET_AUDIO_ENABLE_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_DFP_SET_AUDIO_ENABLE_PARAMS {
    NvU32  subDeviceInstance;
    NvU32  displayId;
    NvBool enable;
} NV0073_CTRL_DFP_SET_AUDIO_ENABLE_PARAMS;

typedef NvU32 NV0073_CTRL_DFP_ASSIGN_SOR_LINKCONFIG;

typedef struct NV0073_CTRL_DFP_ASSIGN_SOR_INFO {
    NvU32 displayMask;
    NvU32 sorType;
} NV0073_CTRL_DFP_ASSIGN_SOR_INFO;

#define NV0073_CTRL_CMD_DFP_ASSIGN_SOR           (0x731152U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DFP_INTERFACE_ID << 8) | NV0073_CTRL_DFP_ASSIGN_SOR_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_CMD_DFP_ASSIGN_SOR_MAX_SORS  4U

typedef struct NV0073_CTRL_DFP_ASSIGN_SOR_PARAMS {
    NvU32                                 subDeviceInstance;
    NvU32                                 displayId;
    NvU8                                  sorExcludeMask;
    NvU32                                 slaveDisplayId;
    NV0073_CTRL_DFP_ASSIGN_SOR_LINKCONFIG forceSublinkConfig;
    NvBool                                bIs2Head1Or;
    NvU32                                 sorAssignList[NV0073_CTRL_CMD_DFP_ASSIGN_SOR_MAX_SORS];
    NV0073_CTRL_DFP_ASSIGN_SOR_INFO       sorAssignListWithTag[NV0073_CTRL_CMD_DFP_ASSIGN_SOR_MAX_SORS];
    NvU8                                  reservedSorMask;
    NvU32                                 flags;
} NV0073_CTRL_DFP_ASSIGN_SOR_PARAMS;

#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_AUDIO                                      0:0
#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_AUDIO_OPTIMAL                    (0x00000001U)
#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_AUDIO_DEFAULT                    (0x00000000U)
#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_ACTIVE_SOR_NOT_AUDIO_CAPABLE               1:1
#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_ACTIVE_SOR_NOT_AUDIO_CAPABLE_NO  (0x00000000U)
#define NV0073_CTRL_DFP_ASSIGN_SOR_FLAGS_ACTIVE_SOR_NOT_AUDIO_CAPABLE_YES (0x00000001U)

#endif
