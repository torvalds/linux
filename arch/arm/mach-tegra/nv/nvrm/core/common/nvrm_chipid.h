/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_NVRM_CHIPID_H
#define INCLUDED_NVRM_CHIPID_H

#include "nvcommon.h"
#include "nvrm_init.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/* Chip Id */
typedef enum
{
    NvRmChipFamily_Gpu = 0,
    NvRmChipFamily_Handheld = 1,
    NvRmChipFamily_BrChips = 2,
    NvRmChipFamily_Crush = 3,
    NvRmChipFamily_Mcp = 4,
    NvRmChipFamily_Ck = 5,
    NvRmChipFamily_Vaio = 6,
    NvRmChipFamily_HandheldSoc = 7,

    NvRmChipFamily_Force32 = 0x7FFFFFFF,
} NvRmChipFamily;

typedef enum
{
    NvRmCaps_HasFalconInterruptController = 0,
    NvRmCaps_Has128bitInterruptSerializer,
    NvRmCaps_Num,
    NvRmCaps_Force32 = 0x7FFFFFFF,
}  NvRmCaps;

typedef struct NvRmChipIdRec
{
    NvU16 Id;
    NvRmChipFamily Family;
    NvU8 Major;
    NvU8 Minor;
    NvU16 SKU;

    /* the following only apply for emulation -- Major will be 0 and
     * Minor is either 0 for quickturn or 1 for fpga
     */
    NvU16 Netlist;
    NvU16 Patch;

    /* List of features and bug WARs */
    NvU32 Flags[(NvRmCaps_Num+31)/32];
} NvRmChipId;

#define NVRM_IS_CAP_SET(h, bit) (((h)->ChipId.Flags)[(bit) >> 5] & (1 << ((bit) & 31)))
#define NVRM_CAP_SET(h, bit) (((h)->ChipId.Flags)[(bit) >> 5] |= (1U << ((bit) & 31U)))
#define NVRM_CAP_CLEAR(h, bit) (((h)->ChipId.Flags)[(bit) >> 5] &= ~(1U << ((bit) & 31U)))

/**
 * Gets the chip id.
 *
 * @param hDevice The RM instance
 */
NvRmChipId *
NvRmPrivGetChipId( NvRmDeviceHandle hDevice );

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // INCLUDED_NVRM_CHIPID_H
