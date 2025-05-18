/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_DISP_H__
#define __NVRM_DISP_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

#define NV2080_CTRL_CMD_INTERNAL_DISPLAY_GET_STATIC_INFO (0x20800a01) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS {
    NvU32  feHwSysCap;
    NvU32  windowPresentMask;
    NvBool bFbRemapperEnabled;
    NvU32  numHeads;
    NvU32  i2cPort;
    NvU32  internalDispActiveMask;
    NvU32  embeddedDisplayPortMask;
    NvBool bExternalMuxSupported;
    NvBool bInternalMuxSupported;
    NvU32  numDispChannels;
} NV2080_CTRL_INTERNAL_DISPLAY_GET_STATIC_INFO_PARAMS;

#define NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED (0x730107U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayMask;
    NvU32 displayMaskDDC;
} NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS;

#define NV0073_CTRL_MAX_CONNECTORS                  4U

#define NV0073_CTRL_CMD_SPECIFIC_GET_CONNECTOR_DATA (0x730250U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SPECIFIC_INTERFACE_ID << 8) | NV0073_CTRL_SPECIFIC_GET_CONNECTOR_DATA_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_SPECIFIC_GET_CONNECTOR_DATA_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 flags;
    NvU32 DDCPartners;
    NvU32 count;
    struct {
        NvU32 index;
        NvU32 type;
        NvU32 location;
    } data[NV0073_CTRL_MAX_CONNECTORS];
    NvU32 platform;
} NV0073_CTRL_SPECIFIC_GET_CONNECTOR_DATA_PARAMS;

#define NV0073_CTRL_CMD_DP_GET_CAPS   (0x731369U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_CMD_DSC_CAP_PARAMS {
    NvBool bDscSupported;
    NvU32  encoderColorFormatMask;
    NvU32  lineBufferSizeKB;
    NvU32  rateBufferSizeKB;
    NvU32  bitsPerPixelPrecision;
    NvU32  maxNumHztSlices;
    NvU32  lineBufferBitDepth;
} NV0073_CTRL_CMD_DSC_CAP_PARAMS;

typedef struct NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS {
    NvU32                          subDeviceInstance;
    NvU32                          sorIndex;
    NvU32                          maxLinkRate;
    NvU32                          dpVersionsSupported;
    NvU32                          UHBRSupportedByGpu;
    NvU32                          minPClkForCompressed;
    NvBool                         bIsMultistreamSupported;
    NvBool                         bIsSCEnabled;
    NvBool                         bHasIncreasedWatermarkLimits;
    NvBool                         bIsPC2Disabled;
    NvBool                         isSingleHeadMSTSupported;
    NvBool                         bFECSupported;
    NvBool                         bIsTrainPhyRepeater;
    NvBool                         bOverrideLinkBw;
    NvBool                         bUseRgFlushSequence;
    NvBool                         bSupportDPDownSpread;
    NV0073_CTRL_CMD_DSC_CAP_PARAMS DSC;
} NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS;

#define NV0073_CTRL_CMD_DP_GET_CAPS   (0x731369U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS_MESSAGE_ID" */
#define NV0073_CTRL_CMD_DP_GET_CAPS_PARAMS_MESSAGE_ID (0x69U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2                0:0
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2_NO              (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_2_YES             (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4                1:1
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4_NO              (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP1_4_YES             (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP2_0                2:2
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP2_0_NO              (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DP_VERSIONS_SUPPORTED_DP2_0_YES             (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE                           2:0
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_NONE                          (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_1_62                          (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_2_70                          (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_5_40                          (0x00000003U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_MAX_LINK_RATE_8_10                          (0x00000004U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR10_0                     0:0
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR10_0_NO                  (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR10_0_YES                 (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR13_5                     1:1
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR13_5_NO                  (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR13_5_YES                 (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR20_0                     2:2
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR20_0_NO                  (0x00000000U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_UHBR_SUPPORTED_UHBR20_0_YES                 (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_RGB                (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_444        (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_NATIVE_422 (0x00000004U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_ENCODER_COLOR_FORMAT_Y_CB_CR_NATIVE_420 (0x00000008U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_16           (0x00000001U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_8            (0x00000002U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_4            (0x00000003U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1_2            (0x00000004U)
#define NV0073_CTRL_CMD_DP_GET_CAPS_DSC_BITS_PER_PIXEL_PRECISION_1              (0x00000005U)

#define NV0073_CTRL_CMD_SYSTEM_GET_CONNECT_STATE (0x730108U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 flags;
    NvU32 displayMask;
    NvU32 retryTimeMs;
} NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS;

#define NV0073_CTRL_CMD_DFP_GET_INFO (0x731140U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DFP_INTERFACE_ID << 8) | NV0073_CTRL_DFP_GET_INFO_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_DFP_GET_INFO_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 flags;
    NvU32 UHBRSupportedByDfp;
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
#define NV0073_CTRL_DFP_FLAGS_TYPE_C_TO_DP_CONNECTOR                     13:13
#define NV0073_CTRL_DFP_FLAGS_TYPE_C_TO_DP_CONNECTOR_FALSE      (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS_TYPE_C_TO_DP_CONNECTOR_TRUE       (0x00000001U)
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
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_10_0GBPS                  0:0
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_10_0GBPS_FALSE (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_10_0GBPS_TRUE  (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_13_5GBPS                  1:1
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_13_5GBPS_FALSE (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_13_5GBPS_TRUE  (0x00000001U)
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_20_0GBPS                  2:2
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_20_0GBPS_FALSE (0x00000000U)
#define NV0073_CTRL_DFP_FLAGS2_DP_UHBR_SUPPORTED_20_0GBPS_TRUE  (0x00000001U)

#define NV0073_CTRL_CMD_SYSTEM_GET_ACTIVE                (0x73010cU) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 head;
    NvU32 flags;
    NvU32 displayId;
} NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS;

#define NV0073_CTRL_CMD_SPECIFIC_SET_BACKLIGHT_BRIGHTNESS (0x730292U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SPECIFIC_INTERFACE_ID << 8) | NV0073_CTRL_SPECIFIC_SET_BACKLIGHT_BRIGHTNESS_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_CMD_SPECIFIC_GET_BACKLIGHT_BRIGHTNESS (0x730291U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SPECIFIC_INTERFACE_ID << 8) | NV0073_CTRL_SPECIFIC_GET_BACKLIGHT_BRIGHTNESS_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_PARAMS {
    NvU32  subDeviceInstance;
    NvU32  displayId;
    NvU32  brightness;
    NvBool bUncalibrated;
    NvU8   brightnessType;
} NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_PARAMS;

#define NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES (0x731377U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS_MESSAGE_ID" */

#define NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES        8U

typedef struct NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS {
    // In
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU16 linkRateTbl[NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES];

    // Out
    NvU16 linkBwTbl[NV0073_CTRL_DP_MAX_INDEXED_LINK_RATES];
    NvU8  linkBwCount;
} NV0073_CTRL_CMD_DP_CONFIG_INDEXED_LINK_RATES_PARAMS;

#define NV0073_CTRL_CMD_DP_CTRL                         (0x731343U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_CTRL_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_DP_CTRL_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 cmd;
    NvU32 data;
    NvU32 err;
    NvU32 retryTimeMs;
    NvU32 eightLaneDpcdBaseAddr;
} NV0073_CTRL_DP_CTRL_PARAMS;

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
        NvBool bEnableAudioOverRightPanel;
    } SST;
} NV0073_CTRL_CMD_DP_CONFIG_STREAM_PARAMS;

#define NV0073_CTRL_CMD_DP_SET_AUDIO_MUTESTREAM             (0x731359U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_DP_INTERFACE_ID << 8) | NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS_MESSAGE_ID" */
typedef struct NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayId;
    NvU32 mute;
} NV0073_CTRL_DP_SET_AUDIO_MUTESTREAM_PARAMS;

#define NV2080_CTRL_CMD_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER (0x20800a58) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS {
    NvU32  addressSpace;
    NV_DECLARE_ALIGNED(NvU64 physicalAddr, 8);
    NV_DECLARE_ALIGNED(NvU64 limit, 8);
    NvU32  cacheSnoop;
    NvU32  hclass;
    NvU32  channelInstance;
    NvBool valid;
    NvU32  pbTargetAperture;
    NvU32  channelPBSize;
    NvU32  subDeviceId;
} NV2080_CTRL_INTERNAL_DISPLAY_CHANNEL_PUSHBUFFER_PARAMS;

#define ADDR_SYSMEM                    (1)

#define ADDR_FBMEM      2         // Frame buffer memory space

typedef enum
{
    PB_SIZE_4KB = 0,
    PB_SIZE_8KB,
    PB_SIZE_16KB,
    PB_SIZE_32KB,
    PB_SIZE_64KB
} ChannelPBSize;

typedef struct
{
    NvV32         channelInstance;            // One of the n channel instances of a given channel type.
                                              // Note that core channel has only one instance
                                              // while all others have two (one per head).
    NvHandle      hObjectBuffer;              // ctx dma handle for DMA push buffer
    NvHandle      hObjectNotify;              // ctx dma handle for an area (of type NvNotification defined in sdk/nvidia/inc/nvtypes.h) where RM can write errors/notifications
    NvU32         offset;                     // Initial offset for put/get, usually zero.
    NvP64         pControl NV_ALIGN_BYTES(8); // pControl gives virt addr of UDISP GET/PUT regs

    NvU32         flags;
    ChannelPBSize channelPBSize;              // Size of Push Buffer requested by client (allowed values in enum)
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB                1:1
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB_YES            0x00000000
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB_NO             0x00000001

    NvU32    subDeviceId;                // One-hot encoded subDeviceId (i.e. SDM) that will be used to address the channel in the pushbuffer stream (via SSDM method)
} NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS;

#define NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_TYPE_PERCENT100        1
#define NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_TYPE_PERCENT1000       2
#define NV0073_CTRL_SPECIFIC_BACKLIGHT_BRIGHTNESS_TYPE_NITS              3

typedef enum
{
    IOVA,
    PHYS_NVM,
    PHYS_PCI,
    PHYS_PCI_COHERENT
} PBTARGETAPERTURE;
#endif
