#ifndef __src_nvidia_generated_g_kernel_fifo_nvoc_h__
#define __src_nvidia_generated_g_kernel_fifo_nvoc_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
    /* *************************************************************************
     * Bug 3820969
     * THINK BEFORE CHANGING ENUM ORDER HERE.
     * VGPU-guest uses this same ordering. Because this enum is not versioned,
     * changing the order here WILL BREAK old-guest-on-newer-host compatibility.
     * ************************************************************************/

    // *ENG_XYZ, e.g.: ENG_GR, ENG_CE etc.,
    ENGINE_INFO_TYPE_ENG_DESC = 0,

    // HW engine ID
    ENGINE_INFO_TYPE_FIFO_TAG,

    // RM_ENGINE_TYPE_*
    ENGINE_INFO_TYPE_RM_ENGINE_TYPE,

    //
    // runlist id (meaning varies by GPU)
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST,

    // NV_PFIFO_INTR_MMU_FAULT_ENG_ID_*
    ENGINE_INFO_TYPE_MMU_FAULT_ID,

    // ROBUST_CHANNEL_*
    ENGINE_INFO_TYPE_RC_MASK,

    // Reset Bit Position. On Ampere, only valid if not _INVALID
    ENGINE_INFO_TYPE_RESET,

    // Interrupt Bit Position
    ENGINE_INFO_TYPE_INTR,

    // log2(MC_ENGINE_*)
    ENGINE_INFO_TYPE_MC,

    // The DEV_TYPE_ENUM for this engine
    ENGINE_INFO_TYPE_DEV_TYPE_ENUM,

    // The particular instance of this engine type
    ENGINE_INFO_TYPE_INSTANCE_ID,

    //
    // The base address for this engine's NV_RUNLIST. Valid only on Ampere+
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST_PRI_BASE,

    //
    // If this entry is a host-driven engine.
    // Update _isEngineInfoTypeValidForOnlyHostDriven when adding any new entry.
    //
    ENGINE_INFO_TYPE_IS_HOST_DRIVEN_ENGINE,

    //
    // The index into the per-engine NV_RUNLIST registers. Valid only on Ampere+
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST_ENGINE_ID,

    //
    // The base address for this engine's NV_CHRAM registers. Valid only on
    // Ampere+
    //
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_CHRAM_PRI_BASE,

    // This entry added to copy data at RMCTRL_EXPORT() call for Kernel RM
    ENGINE_INFO_TYPE_KERNEL_RM_MAX,
    // Used for iterating the engine info table by the index passed.
    ENGINE_INFO_TYPE_INVALID = ENGINE_INFO_TYPE_KERNEL_RM_MAX,

    // Size of FIFO_ENGINE_LIST.engineData
    ENGINE_INFO_TYPE_ENGINE_DATA_ARRAY_SIZE = ENGINE_INFO_TYPE_INVALID,

    // Input-only parameter for kfifoEngineInfoXlate.
    ENGINE_INFO_TYPE_PBDMA_ID

    /* *************************************************************************
     * Bug 3820969
     * THINK BEFORE CHANGING ENUM ORDER HERE.
     * VGPU-guest uses this same ordering. Because this enum is not versioned,
     * changing the order here WILL BREAK old-guest-on-newer-host compatibility.
     * ************************************************************************/
} ENGINE_INFO_TYPE;

#endif
