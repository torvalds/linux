#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080event_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080event_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2006-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION (0x20800301) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_EVENT_INTERFACE_ID << 8) | NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS_MESSAGE_ID" */

typedef struct NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS {
    NvU32  event;
    NvU32  action;
    NvBool bNotifyState;
    NvU32  info32;
    NvU16  info16;
} NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS;

#define NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_REPEAT  (0x00000002)

#endif
