/*
 * arch/arm/mach-tegra/include/nvrm_moduleloader.h
 *
 *
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef INCLUDED_nvrm_moduleloader_H
#define INCLUDED_nvrm_moduleloader_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"

#include "nvcommon.h"
#include "nvos.h"

/**
 * NvRmLibraryHandle is an opaque handle to the Module Loader interface
 *
 * @ingroup nvrm_moduleloader
 */

typedef struct NvRmLibraryRec *NvRmLibraryHandle;

/**
 * @brief Defines the pin state
 */

typedef enum
{
    NvRmModuleLoaderReason_Attach = 0,
    NvRmModuleLoaderReason_Detach,
    NvRmModuleLoaderReason_AttachGreedy,
    NvRmModuleLoaderReason_Num,
    NvRmModuleLoaderReason_Force32 = 0x7FFFFFFF
} NvRmModuleLoaderReason;

/**
 * Loads the segments of requested library name.
 * This method will parse the ELF dynamic library, relocate the address,
 * resolve the symbols and load the segments accordingly.
 * A successful load should return a valid handle.
 *
 * If some of the parameters passed are not valid assert
 * encountered in debug mode.
 *
 * @ingroup nvrm_moduleloader
 *
 * @param hDevice The handle to the RM device
 * @param pLibName The library to be loaded.
 * @param pArgs The arguments to be passed.
 * @param sizeOfArgs The size of arguments passed.
 * @param hLibHandle The handle to the loaded library
 *
 * @retval NvSuccess Load library operation completed successfully
 * @retval NvError_FileReadFailed Indicates that the fileoffset read failed
 * @retval NvError_LibraryNotFound Indicates the given library could not be found
 * @retval NvError_InsufficientMemory Indicates memory allocation failed
 * @retval NvError_InvalidElfFormat Indicates the ELF file is not valid
 */

 NvError NvRmLoadLibrary(
    NvRmDeviceHandle hDevice,
    const char * pLibName,
    void* pArgs,
    NvU32 sizeOfArgs,
    NvRmLibraryHandle * hLibHandle );

/**
 * Loads the segments of requested library name.This method will parse the ELF dynamic
 * library, relocate the address, resolve the symbols and load the segments depending
 * on the conservative or greedy approach. In both the approaches the the IRAM_MAND
 * sections are loaded in IRAM and DRAM_MAND sections are loaded in DRAM. In conservative
 * approach  the IRAM_PREF sections are always loaded in SDRAM. In greedy approach
 * the IRAM_PREF sections are first laoded in IRAM. If IRAM allocation fails for an IRAM_PREF
 * section, it would fallback to DRAM. A successful load should return a valid handle.
 *
 * IRAM_MAND_ADDR = 0x40000000
 * DRAM_MAND_ADDR = 0x10000000
 * Then
 *    If (vaddr < DRAM_MAND_ADDR)
 *       IRAM_PREF Section
 *   Else (vaddr >= IRAM_MAND_ADDR)
 *       IRAM_MAND Section
 *   Else
 *       DRAM_MAND Section
 *
 * If some of the parameters passed are not valid assert
 * encountered in debug mode.
 *
 * @ingroup nvrm_moduleloader
 *
 * @param hDevice The handle to the RM device
 * @param pLibName The library to be loaded.
 * @param pArgs The arguments to be passed.
 * @param sizeOfArgs The size of arguments passed.
 * @param IsApproachGreedy The approach used to load the segments.
 * @param hLibHandle The handle to the loaded library
 *
 * @retval NvSuccess Load library operation completed successfully
 * @retval NvError_FileReadFailed Indicates that the fileoffset read failed
 * @retval NvError_LibraryNotFound Indicates the given library could not be found
 * @retval NvError_InsufficientMemory Indicates memory allocation failed
 * @retval NvError_InvalidElfFormat Indicates the ELF file is not valid
 */

 NvError NvRmLoadLibraryEx(
    NvRmDeviceHandle hDevice,
    const char * pLibName,
    void* pArgs,
    NvU32 sizeOfArgs,
    NvBool IsApproachGreedy,
    NvRmLibraryHandle * hLibHandle );

/**
 * Get symbol address for a given symbol name and handle.
 *
 * Client will request for symbol address for a export function by
 * sending down the symbol name and handle to the loaded library.
 *
 * Assert encountered if some of the parameters passed are not valid
 *
 * NOTE: This function is currently only used to obtain the entry
 * point address (ie, the address of "main"). It should be noted
 * that the entry point must ALWAYS be in THUMB mode! Using ARM
 * mode will cause the module to crash.
 *
 * @ingroup nvrm_moduleloader
 *
 * @param hLibHandle Library handle which is returned by NvRmLoadLibrary().
 * @param pSymbolName pointer to a symbol name to be looked up
 * @param pSymAddress pointer to a symbol address
 *
 * @retval NvSuccess Symbol address is obtained successfully.
 * @retval NvError_SymbolNotFound Indicates the symbol requested is not found
 */

 NvError NvRmGetProcAddress(
    NvRmLibraryHandle hLibHandle,
    const char * pSymbolName,
    void* * pSymAddress );

/**
 * Free the losded memory of the corresponding library handle.
 *
 * This API will use the handle to get the base loaded address and free the memory
 *
 * @param hLibHandle The handle which is returned by NvRmLoadLibrary().
 *
 * @retval NvSuccess Successfuly unloaded the library memory.
 */

 NvError NvRmFreeLibrary(
    NvRmLibraryHandle hLibHandle );

#if defined(__cplusplus)
}
#endif

#endif
