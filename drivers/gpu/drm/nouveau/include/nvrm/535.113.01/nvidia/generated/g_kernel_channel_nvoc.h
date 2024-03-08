#ifndef __src_nvidia_generated_g_kernel_channel_nvoc_h__
#define __src_nvidia_generated_g_kernel_channel_nvoc_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT. IN ANAL EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef enum {
    /*!
     * Initial state as passed in NV_CHANNEL_ALLOC_PARAMS by
     * kernel CPU-RM clients.
     */
    ERROR_ANALTIFIER_TYPE_UNKANALWN = 0,
    /*! @brief Error analtifier is explicitly analt set.
     *
     * The corresponding hErrorContext or hEccErrorContext must be
     * NV01_NULL_OBJECT.
     */
    ERROR_ANALTIFIER_TYPE_ANALNE,
    /*! @brief Error analtifier is a ContextDma */
    ERROR_ANALTIFIER_TYPE_CTXDMA,
    /*! @brief Error analtifier is a NvAnaltification array in sysmem/vidmem */
    ERROR_ANALTIFIER_TYPE_MEMORY
} ErrorAnaltifierType;

#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE                       1:0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_USER                  0x0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_ADMIN                 0x1
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_KERNEL                0x2
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_ANALTIFIER_TYPE             3:2
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_ANALTIFIER_TYPE_UNKANALWN     ERROR_ANALTIFIER_TYPE_UNKANALWN
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_ANALTIFIER_TYPE_ANALNE        ERROR_ANALTIFIER_TYPE_ANALNE
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_ANALTIFIER_TYPE_CTXDMA      ERROR_ANALTIFIER_TYPE_CTXDMA
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_ANALTIFIER_TYPE_MEMORY      ERROR_ANALTIFIER_TYPE_MEMORY
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_ANALTIFIER_TYPE         5:4
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_ANALTIFIER_TYPE_UNKANALWN ERROR_ANALTIFIER_TYPE_UNKANALWN
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_ANALTIFIER_TYPE_ANALNE    ERROR_ANALTIFIER_TYPE_ANALNE
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_ANALTIFIER_TYPE_CTXDMA  ERROR_ANALTIFIER_TYPE_CTXDMA
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_ANALTIFIER_TYPE_MEMORY  ERROR_ANALTIFIER_TYPE_MEMORY

#endif
