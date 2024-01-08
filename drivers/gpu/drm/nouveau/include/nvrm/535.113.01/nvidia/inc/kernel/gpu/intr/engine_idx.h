#ifndef __src_nvidia_inc_kernel_gpu_intr_engine_idx_h__
#define __src_nvidia_inc_kernel_gpu_intr_engine_idx_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define MC_ENGINE_IDX_DISP                          2

#define MC_ENGINE_IDX_CE0                           15

#define MC_ENGINE_IDX_CE9                           24

#define MC_ENGINE_IDX_MSENC                         38

#define MC_ENGINE_IDX_MSENC2                        40

#define MC_ENGINE_IDX_GSP                           49
#define MC_ENGINE_IDX_NVJPG                         50
#define MC_ENGINE_IDX_NVJPEG                        MC_ENGINE_IDX_NVJPG
#define MC_ENGINE_IDX_NVJPEG0                       MC_ENGINE_IDX_NVJPEG

#define MC_ENGINE_IDX_NVJPEG7                       57

#define MC_ENGINE_IDX_BSP                           64
#define MC_ENGINE_IDX_NVDEC                         MC_ENGINE_IDX_BSP
#define MC_ENGINE_IDX_NVDEC0                        MC_ENGINE_IDX_NVDEC

#define MC_ENGINE_IDX_NVDEC7                        71

#define MC_ENGINE_IDX_OFA0                          80

#define MC_ENGINE_IDX_GR                            82
#define MC_ENGINE_IDX_GR0                           MC_ENGINE_IDX_GR

#endif
