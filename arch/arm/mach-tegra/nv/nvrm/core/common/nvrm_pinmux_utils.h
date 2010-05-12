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

#ifndef NVRM_PINMUX_UTILS_H
#define NVRM_PINMUX_UTILS_H

/*
 * nvrm_pinmux_utils.h defines the pinmux macros to implement for the resource
 * manager.
 */

#include "nvcommon.h"
#include "nvrm_pinmux.h"
#include "nvrm_drf.h"
#include "nvassert.h"
#include "nvrm_hwintf.h"
#include "nvodm_modules.h"

// This is to disable trisate refcounting.
#define SKIP_TRISTATE_REFCNT 0

/*  The pin mux code supports run-time trace debugging of all updates to the
 *  pin mux & tristate registers by embedding strings (cast to NvU32s) into the
 *  control tables.
 */
#define NVRM_PINMUX_DEBUG_FLAG 0
#define NVRM_PINMUX_SET_OPCODE_SIZE_RANGE 3:1


#if NVRM_PINMUX_DEBUG_FLAG
NV_CT_ASSERT(sizeof(NvU32)==sizeof(const char*));
#endif

//  The extra strings bloat the size of Set/Unset opcodes
#define NVRM_PINMUX_SET_OPCODE_SIZE ((NVRM_PINMUX_DEBUG_FLAG)?NVRM_PINMUX_SET_OPCODE_SIZE_RANGE)

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

typedef enum {
    PinMuxConfig_OpcodeExtend = 0,
    PinMuxConfig_Set = 1,
    PinMuxConfig_Unset = 2,
    PinMuxConfig_BranchLink = 3,
} PinMuxConfigStates;

typedef enum {
    PinMuxOpcode_ConfigEnd = 0,
    PinMuxOpcode_ModuleDone = 1,
    PinMuxOpcode_SubroutinesDone = 2,
} PinMuxConfigExtendOpcodes;

//  for extended opcodes, this field is set with the extended opcode
#define MUX_ENTRY_0_OPCODE_EXTENSION_RANGE 3:2
//  The state for this entry
#define MUX_ENTRY_0_STATE_RANGE 1:0

#define MAX_NESTING_DEPTH 4

/*  This macro is used for opcode entries in the tables */
#define PIN_MUX_OPCODE(_OP_) \
    (NV_DRF_NUM(MUX,ENTRY,STATE,PinMuxConfig_OpcodeExtend) | \
     NV_DRF_NUM(MUX,ENTRY,OPCODE_EXTENSION,(_OP_)))

/*  This is a dummy entry in the array which indicates that all setting/unsetting for
 *  a configuration is complete. */
#define CONFIGEND() PIN_MUX_OPCODE(PinMuxOpcode_ConfigEnd)

/*  This is a dummy entry in the array which indicates that the last configuration
 *  for the module instance has been passed. */
#define MODULEDONE()  PIN_MUX_OPCODE(PinMuxOpcode_ModuleDone)

/*  This is a dummy entry in the array which indicates that all "extra" configurations
 *  used by sub-routines have been passed. */
#define SUBROUTINESDONE() PIN_MUX_OPCODE(PinMuxOpcode_SubroutinesDone)

/*  This macro is used to insert a branch-and-link from one configuration to another */
#define BRANCH(_ADDR_) \
     (NV_DRF_NUM(MUX,ENTRY,STATE,PinMuxConfig_BranchLink) | \
      NV_DRF_NUM(MUX,ENTRY,BRANCH_ADDRESS,(_ADDR_)))

/**  RmInitPinMux will program the pin mux settings for all IO controllers to
  *  the ODM-selected value (or a safe reset value, if no value is defined in
  *  the ODM query.
  *  It will also read the current value of the tristate registers, to
  *  initialize the reference count
  *
  * @param hDevice The RM instance
  * @param First Indicates whether to perform just safe-reset and DVC
  *     initialization, for early boot, or full initialization
  */
void NvRmInitPinMux(
    NvRmDeviceHandle hDevice,
    NvBool First);

/**  RmPinMuxConfigSelect sets a specific module to a specific configuration.  It is used
  *  for multiplexed controllers, and should only be called by modules which support
  *  multiplexing.  Note that this interface uses the IoModule enumerant, not the RmModule.
  *
  *@param hDevice The RM instance
  *@param IoModule The module to set
  *@param Instance The instance number of the Module
  *@param Configuaration The module's configuration to set
  */

void NvRmPinMuxConfigSelect(
    NvRmDeviceHandle hDevice,
    NvOdmIoModule IoModule,
    NvU32 Instance,
    NvU32 Configuration);

/** RmPinMuxConfigSetTristate will either enable or disable the tristate for a specific
  * IO module configuration.  It is used for multiplexed controllers, and should only be
  * called by modules which support multiplexing.   Note that this interface uses the
  * IoModule enumerant, not the RmModule.
  *
  *@param hDevice The RM instance
  *@param RMModule The module to set
  *@param Instance  The instance number of the module.
  *@param Configuaration The module's configuration to set
  *@param EnableTristate NV_TRUE will tristate the specified pins, NV_FALSE will un-tristate
  */

void NvRmPinMuxConfigSetTristate(
    NvRmDeviceHandle hDevice,
    NvOdmIoModule IoModule,
    NvU32 Instance,
    NvU32 Configuration,
    NvBool EnableTristate);

/** NvRmSetGpioTristate will either enable or disable the tristate for GPIO ports.
  * RM client gpio should only call NvRmSetGpioTristate,
  * which will program the tristate correctly based pins of the particular port.
  *
  *@param hDevice The RM instance
  *@param Port The GPIO port to set
  *@param Pin The Pinnumber  of the port to set.
  *@param EnableTristate NV_TRUE will tristate the specified pins, NV_FALSE will un-tristate
  */
void NvRmSetGpioTristate(
    NvRmDeviceHandle hDevice,
    NvU32 Port,
    NvU32 Pin,
    NvBool EnableTristate);

/** NvRmPrivRmModuleToOdmModule will perform the mapping of RM modules to
 *  ODM modules and instances, using the chip-specific mapping wherever
 *  necessary */
NvU32 NvRmPrivRmModuleToOdmModule(
    NvU32 ChipId,
    NvU32 RmModule,
    NvOdmIoModule *pOdmModules,
    NvU32 *pOdmInstances);


//  Forward declarations for all chip-specific helper functions
NvError NvRmPrivAp15GetModuleInterfaceCaps(
    NvOdmIoModule Module,
    NvU32 Instance,
    NvU32 Config,
    void* pCaps);

NvError NvRmPrivAp16GetModuleInterfaceCaps(
    NvOdmIoModule Module,
    NvU32 Instance,
    NvU32 Config,
    void* pCaps);

NvError NvRmPrivAp20GetModuleInterfaceCaps(
    NvOdmIoModule Module,
    NvU32 Instance,
    NvU32 Config,
    void* pCaps);

const NvU32*** NvRmAp15GetPinMuxConfigs(NvRmDeviceHandle hDevice);

const NvU32*** NvRmAp16GetPinMuxConfigs(NvRmDeviceHandle hDevice);

const NvU32*** NvRmAp20GetPinMuxConfigs(NvRmDeviceHandle hDevice);

NvBool NvRmAp15GetPinGroupForGpio(
    NvRmDeviceHandle hDevice,
    NvU32 Port,
    NvU32 Pin,
    NvU32 *pMapping);

NvBool NvRmAp20GetPinGroupForGpio(
    NvRmDeviceHandle hDevice,
    NvU32 Port,
    NvU32 Pin,
    NvU32* pMapping);

void NvRmPrivAp15EnableExternalClockSource(
   NvRmDeviceHandle hDevice,
   const NvU32* pModuleProgram,
   NvU32 Config,
   NvBool EnableClock);

void NvRmPrivAp20EnableExternalClockSource(
    NvRmDeviceHandle hDevice,
    const NvU32* pModuleProgram,
    NvU32 Config,
    NvBool EnableClock);

NvU32 NvRmPrivAp15GetExternalClockSourceFreq(
    NvRmDeviceHandle hDevice,
    const NvU32* pModuleProgram,
    NvU32 Config);

NvU32 NvRmPrivAp20GetExternalClockSourceFreq(
    NvRmDeviceHandle hDevice,
    const NvU32* pModuleProgram,
    NvU32 Config);

NvBool NvRmPrivAp15RmModuleToOdmModule(
    NvRmModuleID ModuleID,
    NvOdmIoModule* pOdmModules,
    NvU32* pOdmInstances,
    NvU32 *pCnt);

NvBool NvRmPrivAp16RmModuleToOdmModule(
    NvRmModuleID ModuleID,
    NvOdmIoModule* pOdmModules,
    NvU32* pOdmInstances,
    NvU32 *pCnt);

NvBool NvRmPrivAp20RmModuleToOdmModule(
    NvRmModuleID ModuldID,
    NvOdmIoModule* pOdmModules,
    NvU32* pOdmInstances,
    NvU32 *pCnt);

/**
 * Chip-specific functions to get SoC strap value for the given strap group.
 *
 * @param hDevice The RM instance
 * @param StrapGroup Strap group to be read.
 * @pStrapValue A pointer to the returned strap group value.
 *
 * @retval NvSuccess if strap value is read successfully
 * @retval NvError_NotSupported if the specified strap group does not
 *   exist on the current SoC.
 */
NvError
NvRmAp15GetStraps(
    NvRmDeviceHandle hDevice,
    NvRmStrapGroup StrapGroup,
    NvU32* pStrapValue);

NvError
NvRmAp20GetStraps(
    NvRmDeviceHandle hDevice,
    NvRmStrapGroup StrapGroup,
    NvU32* pStrapValue);

void NvRmPrivAp15SetPadTristates(
    NvRmDeviceHandle hDevice,
    const NvU32* Module,
    NvU32 Config,
    NvBool EnableTristate);

void NvRmPrivAp15SetPinMuxCtl(
    NvRmDeviceHandle hDevice,
    const NvU32* Module,
    NvU32 Config);

void NvRmPrivAp15InitTrisateRefCount(NvRmDeviceHandle hDevice);

const NvU32*
NvRmPrivAp15FindConfigStart(
    const NvU32* Instance,
    NvU32 Config,
    NvU32 EndMarker);

void
NvRmPrivAp15SetGpioTristate(
    NvRmDeviceHandle hDevice,
    NvU32 Port,
    NvU32 Pin,
    NvBool EnableTristate);

void NvRmAp15SetDefaultTristate (NvRmDeviceHandle hDevice);

void NvRmAp20SetDefaultTristate (NvRmDeviceHandle hDevice);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // NVRM_PINMUX_UTILS_H


