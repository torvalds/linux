/*
 * Copyright (c) 2008-2009 NVIDIA Corporation.
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

#ifndef INCLUDED_NVBOOTARGS_H
#define INCLUDED_NVBOOTARGS_H

/**
 * This defines the basic bootarg structure and keys for use with
 * NvOsBootArgGet and NvOsBootArgSet.
 */

#include "nvcommon.h"

/** 
 * The maximum number of memory handles that may be preserved across the
 * bootloader-to-OS transition.  @see NvRmBootArg_PreservedMemHandle.
 */
#define NV_BOOTARGS_MAX_PRESERVED_MEMHANDLES 3

#if defined(__cplusplus)
extern "C"
{
#endif

/* accessor for various boot arg classes, see NvOsBootArg* */
typedef enum
{
    NvBootArgKey_Rm = 0x1,
    NvBootArgKey_Display,
    NvBootArgKey_Framebuffer,
    NvBootArgKey_ChipShmoo,
    NvBootArgKey_ChipShmooPhys,
    NvBootArgKey_Carveout,
    NvBootArgKey_WarmBoot,
    NvBootArgKey_PreservedMemHandle_0 = 0x10000,
    NvBootArgKey_PreservedMemHandle_Num = (NvBootArgKey_PreservedMemHandle_0 +
                                         NV_BOOTARGS_MAX_PRESERVED_MEMHANDLES),
    NvBootArgKey_Force32 = 0x7FFFFFFF,
} NvBootArgKey;

/**
 * Resource Manager boot args.
 *
 * Nothing here yet.
 */
typedef struct NvBootArgsRmRec
{
    NvU32 reserved;
} NvBootArgsRm;

/**
 * Carveout boot args, which define the physical memory location of the GPU
 * carved-out memory region(s).
 */
typedef struct NvBootArgsCarveoutRec
{
    NvUPtr base;
    NvU32 size;
} NvBootArgsCarveout;

/**
 * Warmbootloader boot args. This structure only contains
 * a mem handle key to preserve the warm bootloader
 * across the bootloader->os transition
 */
typedef struct NvBootArgsWarmbootRec
{
    NvU32 MemHandleKey;
} NvBootArgsWarmboot;

/**
 * PreservedMemHandle boot args, indexed by PreservedMemHandle_0 + n.
 * All values n from 0 to the first value which does not return NvSuccess will
 * be quered at RM initialization in the OS environment.  If present, a new
 * memory handle for the physical region specified will be created.
 * This allows physical memory allocations (e.g., for framebuffers) to persist
 * between the bootloader and operating system.  Only carveout and IRAM
 * allocations may be preserved with this interface.
 */
typedef struct NvBootArgsPreservedMemHandleRec
{
    NvUPtr  Address;
    NvU32   Size;
} NvBootArgsPreservedMemHandle;


/**
 * Display boot args, indexed by NvBootArgKey_Display.
 *
 * The bootloader may have a splash screen. This will flag which controller
 * and device was used for the splash screen so the device will not be
 * reinitialized (which causes visual artifacts).
 */
typedef struct NvBootArgsDisplayRec
{
    /* which controller is initialized */
    NvU32 Controller;

    /* index into the ODM device list of the boot display device */
    NvU32 DisplayDeviceIndex;

    /* set to NV_TRUE if the display has been initialized */
    NvBool bEnabled;
} NvBootArgsDisplay;

/**
 * Framebuffer boot args, indexed by NvBootArgKey_Framebuffer
 *
 * A framebuffer may be shared between the bootloader and the
 * operating system display driver.  When this key is present,
 * a preserved memory handle for the framebuffer must also
 * be present, to ensure that no display corruption occurs
 * during the transition.
 */
typedef struct NvBootArgsFramebufferRec
{
    /*  The key used for accessing the preserved memory handle */
    NvU32 MemHandleKey;
    /*  Total memory size of the framebuffer */
    NvU32 Size;
    /*  Color format of the framebuffer, cast to a U32  */
    NvU32 ColorFormat;
    /*  Width of the framebuffer, in pixels  */
    NvU16 Width;
    /*  Height of each surface in the framebuffer, in pixels  */
    NvU16 Height;
    /*  Pitch of a framebuffer scanline, in bytes  */
    NvU16 Pitch;
    /*  Surface layout of the framebuffer, cast to a U8 */
    NvU8  SurfaceLayout;
    /*  Number of contiguous surfaces of the same height in the
     *  framebuffer, if multi-buffering.  Each surface is
     *  assumed to begin at Pitch * Height bytes from the
     *  previous surface.  */
    NvU8  NumSurfaces;
} NvBootArgsFramebuffer;

/**
 * Chip chatcterization shmoo data indexed by NvBootArgKey_ChipShmoo
 */
typedef struct NvBootArgsChipShmooRec
{
    // The key used for accessing the preserved memory handle of packed
    // charcterization tables 
    NvU32 MemHandleKey;

    // Offset and size of each unit in the packed buffer
    NvU32 CoreShmooVoltagesListOffset;
    NvU32 CoreShmooVoltagesListSize;

    NvU32 CoreScaledLimitsListOffset;
    NvU32 CoreScaledLimitsListSize;

    NvU32 OscDoublerListOffset;
    NvU32 OscDoublerListSize;

    NvU32 SKUedLimitsOffset;
    NvU32 SKUedLimitsSize;

    NvU32 CpuShmooVoltagesListOffset;
    NvU32 CpuShmooVoltagesListSize;

    NvU32 CpuScaledLimitsOffset;
    NvU32 CpuScaledLimitsSize;

    // Misc charcterization settings
    NvU16 CoreCorner;
    NvU16 CpuCorner;
    NvU32 Dqsib;
    NvU32 SvopLowVoltage;
    NvU32 SvopLowSetting;
    NvU32 SvopHighSetting;
} NvBootArgsChipShmoo;

/**
 * Chip chatcterization shmoo data indexed by NvBootArgKey_ChipShmooPhys
 */
typedef struct NvBootArgsChipShmooPhysRec
{
    NvU32 PhysShmooPtr;
    NvU32 Size;
} NvBootArgsChipShmooPhys;

#define NVBOOTARG_NUM_PRESERVED_HANDLES (NvBootArgKey_PreservedMemHandle_Num - \
                                         NvBootArgKey_PreservedMemHandle_0)

/**
 * OS-agnostic bootarg structure.
 */
typedef struct NvBootArgsRec
{
    NvBootArgsRm RmArgs;
    NvBootArgsDisplay DisplayArgs;
    NvBootArgsFramebuffer FramebufferArgs;
    NvBootArgsChipShmoo ChipShmooArgs;
    NvBootArgsChipShmooPhys ChipShmooPhysArgs;
    NvBootArgsWarmboot WarmbootArgs;
    NvBootArgsPreservedMemHandle MemHandleArgs[NVBOOTARG_NUM_PRESERVED_HANDLES];
} NvBootArgs;

#if defined(__cplusplus)
}
#endif

#endif // INCLUDED_NVBOOTARGS_H
