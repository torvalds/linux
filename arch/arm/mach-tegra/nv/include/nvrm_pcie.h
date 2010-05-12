/*
 * Copyright (c) 2009 NVIDIA Corporation.
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

#ifndef INCLUDED_nvrm_pcie_H
#define INCLUDED_nvrm_pcie_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

typedef enum
{

    // NvRm PCIE access type read
        NvRmPcieAccessType_Read,

    // NvRm PCIE access type write
        NvRmPcieAccessType_Write,
    NvRmPcieAccessType_Num,
    NvRmPcieAccessType_Force32 = 0x7FFFFFFF
} NvRmPcieAccessType;


/** Reads or writes the config space of the PCI device. 
 * 
 * @param hRmDeviceHandle The Rm device handle
 * @param bus_number Bus number on on which the device is present.
 * @param type   Specifies the access type
 * @param offset Start offset to read the configuration data
 * @param Data   Data in bytes used to read/write from/to device config space,
 * depending on the access type.
 * @param DataLen Sepcifies the length of Data Array.
 *
 *  Returns NvSuccess or the appropriate error code.
 */

 NvError NvRmReadWriteConfigSpace( 
    NvRmDeviceHandle hDeviceHandle,
    NvU32 bus_number,
    NvRmPcieAccessType type,
    NvU32 offset,
    NvU8 * Data,
    NvU32 DataLen );


/** Registers a MSI handler for the device at an index.
 *
 * @param hRmDeviceHandle The Rm device handle
 * @param function_device_bus function/device/bus tuple.
 * @param index Msi index. Some devices support more than 1 MSI. For those
 * devices, index value is from (0 to max-1)
 * @param sem Semaphore which will be signalled when the MSI interrupt is
 * triggered.
 * @param InterruptEnable To enable or disable interrupt.
 *
 *  Returns NvSuccess or the appropriate error code.
 */


 NvError NvRmRegisterPcieMSIHandler( 
    NvRmDeviceHandle hDeviceHandle,
    NvU32 function_device_bus,
    NvU32 index,
    NvOsSemaphoreHandle sem,
    NvBool InterruptEnable );

 NvError NvRmRegisterPcieLegacyHandler( 
    NvRmDeviceHandle hDeviceHandle,
    NvU32 function_device_bus,
    NvOsSemaphoreHandle sem,
    NvBool InterruptEnable );

//  PCIE address map supports 64-bit addressing. But, RM driver only supports
//  32-addressing. In the future, if the device supports 64-bit addressing, one
//  can change this typedef.

typedef NvU32 NvRmPciPhysAddr;

/**
 * Attemtps to map the Pcie memory to the 32-bit AXI address region.
 * Ap20 reserves only 1GB PCIe aperture. Out of that 1GB, some region is reserved for
 * the register/config/msi access. Only 768MB is left out for the PCIe memory aperture. 
 * 
 * @param hRmDeviceHandle   Rm device handle
 * @param mem               "Base address registers" of a PCI device.
 * 
 * Returns the mapped AXI address. If the mapping fails, it returns 0.
 */

 NvRmPhysAddr NvRmMapPciMemory( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmPciPhysAddr mem,
    NvU32 size );

/** Unmaps the PCI to AXI address mapping
 *  
 * @param hRmDeviceHandle Rm device handle
 * @param mem               AXI addresses mapped by calling NvRmMapPcieMemory
 * API.
 */

 void NvRmUnmapPciMemory( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmPhysAddr mem,
    NvU32 size );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
