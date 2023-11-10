#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073system_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl0073_ctrl0073system_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2005-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define NV0073_CTRL_CMD_SYSTEM_GET_NUM_HEADS (0x730102U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_NUM_HEADS_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_SYSTEM_GET_NUM_HEADS_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 flags;
    NvU32 numHeads;
} NV0073_CTRL_SYSTEM_GET_NUM_HEADS_PARAMS;

#define NV0073_CTRL_CMD_SYSTEM_GET_SUPPORTED (0x730120U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 displayMask;
    NvU32 displayMaskDDC;
} NV0073_CTRL_SYSTEM_GET_SUPPORTED_PARAMS;

#define NV0073_CTRL_CMD_SYSTEM_GET_CONNECT_STATE (0x730122U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 flags;
    NvU32 displayMask;
    NvU32 retryTimeMs;
} NV0073_CTRL_SYSTEM_GET_CONNECT_STATE_PARAMS;

#define NV0073_CTRL_CMD_SYSTEM_GET_ACTIVE                (0x730126U) /* finn: Evaluated from "(FINN_NV04_DISPLAY_COMMON_SYSTEM_INTERFACE_ID << 8) | NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS_MESSAGE_ID" */

typedef struct NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS {
    NvU32 subDeviceInstance;
    NvU32 head;
    NvU32 flags;
    NvU32 displayId;
} NV0073_CTRL_SYSTEM_GET_ACTIVE_PARAMS;

#define NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS             (16U)

#endif
