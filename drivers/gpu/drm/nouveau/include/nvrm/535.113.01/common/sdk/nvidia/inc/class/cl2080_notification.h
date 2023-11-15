#ifndef __src_common_sdk_nvidia_inc_class_cl2080_notification_h__
#define __src_common_sdk_nvidia_inc_class_cl2080_notification_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define NV2080_NOTIFIERS_HOTPLUG                                   (1)

#define NV2080_NOTIFIERS_DP_IRQ                                    (7)

#define NV2080_ENGINE_TYPE_GRAPHICS                   (0x00000001)
#define NV2080_ENGINE_TYPE_GR0                        NV2080_ENGINE_TYPE_GRAPHICS

#define NV2080_ENGINE_TYPE_COPY0                      (0x00000009)

#define NV2080_ENGINE_TYPE_BSP                        (0x00000013)
#define NV2080_ENGINE_TYPE_NVDEC0                     NV2080_ENGINE_TYPE_BSP

#define NV2080_ENGINE_TYPE_MSENC                      (0x0000001b)
#define NV2080_ENGINE_TYPE_NVENC0                      NV2080_ENGINE_TYPE_MSENC  /* Mutually exclusive alias */

#define NV2080_ENGINE_TYPE_SW                         (0x00000022)

#define NV2080_ENGINE_TYPE_SEC2                       (0x00000026)

#define NV2080_ENGINE_TYPE_NVJPG                      (0x0000002b)
#define NV2080_ENGINE_TYPE_NVJPEG0                     NV2080_ENGINE_TYPE_NVJPG

#define NV2080_ENGINE_TYPE_OFA                        (0x00000033)

typedef struct {
    NvU32 plugDisplayMask;
    NvU32 unplugDisplayMask;
} Nv2080HotplugNotification;

typedef struct Nv2080DpIrqNotificationRec {
    NvU32 displayId;
} Nv2080DpIrqNotification;

#endif
