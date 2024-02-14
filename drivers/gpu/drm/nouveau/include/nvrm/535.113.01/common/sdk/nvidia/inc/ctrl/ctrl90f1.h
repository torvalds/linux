#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl90f1_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl90f1_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define GMMU_FMT_MAX_LEVELS  6U

#define NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES (0x90f10106U) /* finn: Evaluated from "(FINN_FERMI_VASPACE_A_VASPACE_INTERFACE_ID << 8) | NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS_MESSAGE_ID" */

typedef struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS {
    /*!
     * [in] GPU sub-device handle - this API only supports unicast.
     *      Pass 0 to use subDeviceId instead.
     */
    NvHandle hSubDevice;

    /*!
     * [in] GPU sub-device ID. Ignored if hSubDevice is non-zero.
     */
    NvU32    subDeviceId;

    /*!
     * [in] Page size (VA coverage) of the level to reserve.
     *      This need not be a leaf (page table) page size - it can be
     *      the coverage of an arbitrary level (including root page directory).
     */
    NV_DECLARE_ALIGNED(NvU64 pageSize, 8);

    /*!
     * [in] First GPU virtual address of the range to reserve.
     *      This must be aligned to pageSize.
     */
    NV_DECLARE_ALIGNED(NvU64 virtAddrLo, 8);

    /*!
     * [in] Last GPU virtual address of the range to reserve.
     *      This (+1) must be aligned to pageSize.
     */
    NV_DECLARE_ALIGNED(NvU64 virtAddrHi, 8);

    /*! 
     * [in] Number of PDE levels to copy.
     */
    NvU32    numLevelsToCopy;

   /*!
     * [in] Per-level information.
     */
    struct {
        /*!
         * Physical address of this page level instance.
         */
        NV_DECLARE_ALIGNED(NvU64 physAddress, 8);

        /*!
         * Size in bytes allocated for this level instance.
         */
        NV_DECLARE_ALIGNED(NvU64 size, 8);

        /*!
         * Aperture in which this page level instance resides.
         */
        NvU32 aperture;

        /*!
         * Page shift corresponding to the level
         */
        NvU8  pageShift;
    } levels[GMMU_FMT_MAX_LEVELS];
} NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS;

#endif
