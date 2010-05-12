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

#ifndef INCLUDED_nvrm_pinmux_H
#define INCLUDED_nvrm_pinmux_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvodm_modules.h"

/**
 * For each module that has pins (an I/O module), there may be several muxing
 * configurations. This allows a driver to select or query a particular
 * configuration per I/O module.  I/O modules may be instantiated on the
 * chip multiple times.
 *
 * Certain combinations of modules configurations may not be physically
 * possible; say that a hypothetical SPI controller configuration 3 uses pins
 * that are shared by a hypothectial UART configuration 2.  Presently, these
 * conflicting configurations are managed via an external tool provided by
 * SysEng, which identifies the configurations for the ODM pin-mux tables
 * depending upon choices made by the ODM.
 */

/**
 * Sets the module to tristate configuration. 
 * Use enable to release the pinmux.  The pins will be
 * tri-stated when not in use to save power.
 *
 * @param hDevice The RM instance
 * @param RmModule The module to set
 * @param EnableTristate NV_TRUE will tristate the specified pins, NV_FALSE will un-tristate
 */

 NvError NvRmSetModuleTristate( 
    NvRmDeviceHandle hDevice,
    NvRmModuleID RmModule,
    NvBool EnableTristate );

/**
 * Sets an ODM module ID to tristate configuration.  Analagous to @see NvRmSetModuleTristate,
 * but indexed based on the ODM module ID, rather than the controller ID.
 *
 * @param hDevice The RM instance
 * @param OdmModule The module to set (should be of type NvOdmIoModule)
 * @param OdmInstance The instance of the module to set
 * @param EnableTristate NV_TRUE will tristate the specified pins, NV_FALSE will un-tristate
 */

 NvError NvRmSetOdmModuleTristate( 
    NvRmDeviceHandle hDevice,
    NvU32 OdmModule,
    NvU32 OdmInstance,
    NvBool EnableTristate );

/**
 * Configures modules which can provide clock sources to peripherals.
 * If a Tegra application processor is expected to provide a clock source
 * to an external peripheral, this API should be called to configure the
 * clock source and to ensure that its pins are driven prior to attempting
 * to program the peripheral through a command interface (e.g., SPI).
 *
 * @param hDevice The RM instance
 * @param IoModule The module to set, must be NvOdmIoModule_ExternalClock
 * @param Instance The instance of the I/O module to be set.
 * @param Config The pin map configuration for the I/O module.
 * @param EnableTristate NV_TRUE will tristate the specified clock source,
 *                       NV_FALSE will drive it.
 * 
 * @retval Returns the clock frequency, in KHz, that is output on the
 *  designated pin (or '0' if no clock frequency is specified or found).
 */

 NvU32 NvRmExternalClockConfig( 
    NvRmDeviceHandle hDevice,
    NvU32 IoModule,
    NvU32 Instance,
    NvU32 Config,
    NvBool EnableTristate );

typedef struct NvRmModuleSdmmcInterfaceCapsRec
{

   ///  Maximum bus width supported by the physical interface
   ///  Will be 2, 4 or 8 depending on the selected pin mux
       NvU32 MmcInterfaceWidth;
} NvRmModuleSdmmcInterfaceCaps;

typedef struct NvRmModulePcieInterfaceCapsRec
{

   ///  Maximum bus type supported by the physical interface
   ///  Will be 4X1 or 2X2 depending on the selected pin mux
       NvU32 PcieNumEndPoints;
    NvU32 PcieLanesPerEp;
} NvRmModulePcieInterfaceCaps;

typedef struct NvRmModulePwmInterfaceCapsRec
{

   ///  The OR bits value of PWM Output IDs supported by the 
   ///  physical interface depending on the selected pin mux.
   ///  Hence, PwmOutputId_PWM0 = bit 0, PwmOutputId_PWM1 = bit 1,
   ///  PwmOutputId_PWM2 = bit 2, PwmOutputId_PWM3 = bit 3
       NvU32 PwmOutputIdSupported;
} NvRmModulePwmInterfaceCaps;

typedef struct NvRmModuleNandInterfaceCapsRec
{

   ///  Maximum bus width supported by the physical interface
   ///  Will be 8 or 16 depending on the selected pin mux
       NvU8 NandInterfaceWidth;
    NvBool IsCombRbsyMode;
} NvRmModuleNandInterfaceCaps;

typedef struct NvRmModuleUartInterfaceCapsRec
{

   ///  Maximum number of the interface lines supported by the physical interface.
   ///  Will be 0, 2, 4 or 8 depending on the selected pin mux.
   ///  0 means there is no physical interface for the uart.
   ///  2 means only rx/tx lines are supported.
   ///  4 means only rx/tx/rtx/cts lines are supported.
   ///  8 means full modem lines are supported.
       NvU32 NumberOfInterfaceLines;
} NvRmModuleUartInterfaceCaps;

/**
 * @brief Query the board-defined capabilities of an I/O controller
 *
 * This API will return capabilities for controller modules based on
 * interface properties defined by ODM query interfaces, such as the
 * pin mux query.
 *
 * pCap should be a pointer to the matching NvRmxxxInterfaceCaps structure
 * (defined above) for the ModuleId, and CapStructSize should be
 * the sizeof(structure type). and also should be word aligned.
 * 
 * @retval NvError_NotSupported if the specified ModuleID does not
 *     exist on the current platform.
 */

 NvError NvRmGetModuleInterfaceCapabilities( 
    NvRmDeviceHandle hRm,
    NvRmModuleID ModuleId,
    NvU32 CapStructSize,
    void* pCaps );

/**
 * Defines SoC strap groups.
 */

typedef enum
{

    /// ram_code strap group  
        NvRmStrapGroup_RamCode = 1,
    NvRmStrapGroup_Num,
    NvRmStrapGroup_Force32 = 0x7FFFFFFF
} NvRmStrapGroup;

/**
 * Gets SoC strap value for the given strap group.
 * 
 * @param hDevice The RM instance
 * @param StrapGroup Strap group to be read.
 * @pStrapValue A pointer to the returned strap group value.
 * 
 * @retval NvSuccess if strap value is read successfully
 * @retval NvError_NotSupported if the specified strap group does not
 *   exist on the current SoC.
 */

 NvError NvRmGetStraps( 
    NvRmDeviceHandle hDevice,
    NvRmStrapGroup StrapGroup,
    NvU32 * pStrapValue );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
