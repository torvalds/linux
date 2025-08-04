/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_EVENT_H__
#define __NVRM_EVENT_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV01_EVENT_KERNEL_CALLBACK_EX            (0x0000007e)

typedef struct NV0005_ALLOC_PARAMETERS {
    NvHandle hParentClient;
    NvHandle hSrcResource;

    NvV32    hClass;
    NvV32    notifyIndex;
    NV_DECLARE_ALIGNED(NvP64 data, 8);
} NV0005_ALLOC_PARAMETERS;

#define NV01_EVENT_CLIENT_RM                                       (0x04000000)

#define NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION (0x20800301) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_EVENT_INTERFACE_ID << 8) | NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS {
    NvU32  event;
    NvU32  action;
    NvBool bNotifyState;
    NvU32  info32;
    NvU16  info16;
} NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS;

#define NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_REPEAT  (0x00000002)

typedef struct rpc_post_event_v17_00
{
    NvHandle   hClient;
    NvHandle   hEvent;
    NvU32      notifyIndex;
    NvU32      data;
    NvU16      info16;
    NvU32      status;
    NvU32      eventDataSize;
    NvBool     bNotifyList;
    NvU8       eventData[];
} rpc_post_event_v17_00;
#endif
