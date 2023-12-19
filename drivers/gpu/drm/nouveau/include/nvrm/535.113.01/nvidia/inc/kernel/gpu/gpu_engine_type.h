#ifndef __src_nvidia_inc_kernel_gpu_gpu_engine_type_h__
#define __src_nvidia_inc_kernel_gpu_gpu_engine_type_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef enum
{
    RM_ENGINE_TYPE_NULL                 =       (0x00000000),
    RM_ENGINE_TYPE_GR0                  =       (0x00000001),
    RM_ENGINE_TYPE_GR1                  =       (0x00000002),
    RM_ENGINE_TYPE_GR2                  =       (0x00000003),
    RM_ENGINE_TYPE_GR3                  =       (0x00000004),
    RM_ENGINE_TYPE_GR4                  =       (0x00000005),
    RM_ENGINE_TYPE_GR5                  =       (0x00000006),
    RM_ENGINE_TYPE_GR6                  =       (0x00000007),
    RM_ENGINE_TYPE_GR7                  =       (0x00000008),
    RM_ENGINE_TYPE_COPY0                =       (0x00000009),
    RM_ENGINE_TYPE_COPY1                =       (0x0000000a),
    RM_ENGINE_TYPE_COPY2                =       (0x0000000b),
    RM_ENGINE_TYPE_COPY3                =       (0x0000000c),
    RM_ENGINE_TYPE_COPY4                =       (0x0000000d),
    RM_ENGINE_TYPE_COPY5                =       (0x0000000e),
    RM_ENGINE_TYPE_COPY6                =       (0x0000000f),
    RM_ENGINE_TYPE_COPY7                =       (0x00000010),
    RM_ENGINE_TYPE_COPY8                =       (0x00000011),
    RM_ENGINE_TYPE_COPY9                =       (0x00000012),
    RM_ENGINE_TYPE_NVDEC0               =       (0x0000001d),
    RM_ENGINE_TYPE_NVDEC1               =       (0x0000001e),
    RM_ENGINE_TYPE_NVDEC2               =       (0x0000001f),
    RM_ENGINE_TYPE_NVDEC3               =       (0x00000020),
    RM_ENGINE_TYPE_NVDEC4               =       (0x00000021),
    RM_ENGINE_TYPE_NVDEC5               =       (0x00000022),
    RM_ENGINE_TYPE_NVDEC6               =       (0x00000023),
    RM_ENGINE_TYPE_NVDEC7               =       (0x00000024),
    RM_ENGINE_TYPE_NVENC0               =       (0x00000025),
    RM_ENGINE_TYPE_NVENC1               =       (0x00000026),
    RM_ENGINE_TYPE_NVENC2               =       (0x00000027),
    RM_ENGINE_TYPE_VP                   =       (0x00000028),
    RM_ENGINE_TYPE_ME                   =       (0x00000029),
    RM_ENGINE_TYPE_PPP                  =       (0x0000002a),
    RM_ENGINE_TYPE_MPEG                 =       (0x0000002b),
    RM_ENGINE_TYPE_SW                   =       (0x0000002c),
    RM_ENGINE_TYPE_TSEC                 =       (0x0000002d),
    RM_ENGINE_TYPE_VIC                  =       (0x0000002e),
    RM_ENGINE_TYPE_MP                   =       (0x0000002f),
    RM_ENGINE_TYPE_SEC2                 =       (0x00000030),
    RM_ENGINE_TYPE_HOST                 =       (0x00000031),
    RM_ENGINE_TYPE_DPU                  =       (0x00000032),
    RM_ENGINE_TYPE_PMU                  =       (0x00000033),
    RM_ENGINE_TYPE_FBFLCN               =       (0x00000034),
    RM_ENGINE_TYPE_NVJPEG0              =       (0x00000035),
    RM_ENGINE_TYPE_NVJPEG1              =       (0x00000036),
    RM_ENGINE_TYPE_NVJPEG2              =       (0x00000037),
    RM_ENGINE_TYPE_NVJPEG3              =       (0x00000038),
    RM_ENGINE_TYPE_NVJPEG4              =       (0x00000039),
    RM_ENGINE_TYPE_NVJPEG5              =       (0x0000003a),
    RM_ENGINE_TYPE_NVJPEG6              =       (0x0000003b),
    RM_ENGINE_TYPE_NVJPEG7              =       (0x0000003c),
    RM_ENGINE_TYPE_OFA                  =       (0x0000003d),
    RM_ENGINE_TYPE_LAST                 =       (0x0000003e),
} RM_ENGINE_TYPE;

#endif
