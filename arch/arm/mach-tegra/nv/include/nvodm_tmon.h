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

/** 
 * @file
 * <b>NVIDIA Tegra ODM Kit:
 *         Temperature Monitor Interface</b>
 *
 * @b Description: Defines the ODM interface for Temperature Monitor (TMON).
 * 
 */

#ifndef INCLUDED_NVODM_TMON_H
#define INCLUDED_NVODM_TMON_H

#include "nvcommon.h"
#include "nvodm_services.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @defgroup nvodm_tmon Temperature Monitor Adaptation Interface 
 *   
 * This is the temperature monitor (TMON) ODM adaptation interface, which
 * handles the abstraction of external devices monitoring temperature zones
 * on NVIDIA SoC based platforms. For the clients of this API, each zone has
 * its own monitoring device. Dependencies introduced by multi-channel devices
 * capable of monitoring several zones are resolved inside the implementation
 * layer.
 * 
 * @ingroup nvodm_adaptation
 * @{
 */

/**
 * Defines an opaque handle for TMON device.
 */
typedef struct NvOdmTmonDeviceRec *NvOdmTmonDeviceHandle;

/**
 * Defines an opaque handle to the TMON interrupt interface.
 */
typedef struct NvOdmTmonIntrRec *NvOdmTmonIntrHandle;

/**
 * Defines temperature zones.
 */
typedef enum
{
    /// Specifies ambient temperature zone.
    NvOdmTmonZoneID_Ambient = 1,

    /// Specifies SoC core temperature zone.
    NvOdmTmonZoneID_Core,

    NvOdmTmonZoneID_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmTmonZoneID_Force32 = 0x7FFFFFFFUL
} NvOdmTmonZoneID;

/**
 * Defines temperature monitoring configuration parameters.
 */
typedef enum
{
    /// Identifies temperature sampling interval in ms.
    NvOdmTmonConfigParam_SampleMs = 1,

    /// Identifies High temperature boundary for TMON out of limit
    ///  interrupt (in degrees C).
    NvOdmTmonConfigParam_IntrLimitHigh,

    /// Identifies Low temperature boundary for TMON out of limit
    ///  interrupt (in degrees C).
    NvOdmTmonConfigParam_IntrLimitLow,

    /// Identifies temperature threshold for TMON comparator that
    ///  controls h/w critical shutdown mechanism (in degrees C).
    NvOdmTmonConfigParam_HwLimitCrit,

    NvOdmTmonConfigParam_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmTmonConfigParam_Force32 = 0x7FFFFFFFUL
} NvOdmTmonConfigParam;

/// Special value for configuration parameters.
#define ODM_TMON_PARAMETER_UNSPECIFIED (0x7FFFFFFF)

/**
 * Holds configuration parameter capabilities.
 */
typedef struct NvOdmTmonParameterCapsRec
{ 
    /// Specifies maximum parameter value (units depend on the parameter).
    NvS32 MaxValue;

    /// Specifies minimum parameter value (units depend on the parameter).
    NvS32 MinValue;

    /// Specifies ODM protection attribute; if \c NV_TRUE TMON ODM Kit would
    ///  not allow to change the parameter.
    NvBool OdmProtected;
} NvOdmTmonParameterCaps;

/**
 * Holds temperature monitoring device capabilities.
 */
typedef struct NvOdmTmonCapabilitiesRec
{ 
    /// Specifies maximum temperature limit for TMON operations (in degrees C).
    NvS32 Tmax;

    /// Specifies minimum temperature limit for TMON operations (in degrees C).
    NvS32 Tmin;

    /// Specifies support for TMON out of limit interrupt.
    NvBool IntrSupported;

    /// Specifies support for TMON hardware critical shutdown mechanism.
    NvBool HwCriticalSupported;

    /// Specifies support for TMON hardware auto-cooling mechanism (e.g., fan).
    NvBool HwCoolingSupported;
} NvOdmTmonCapabilities;


/**
 * Gets a handle to the TMON in the specified zone.
 *
 * @param ZoneId The targeted temperature zone.
 * 
 * @return TMON handle, NULL if zone is not monitored.
 */
NvOdmTmonDeviceHandle
NvOdmTmonDeviceOpen(NvOdmTmonZoneID ZoneId);

/**
 * Closes the TMON handle. 
 *
 * @param hTmon The TMON handle to be closed.
 *  If NULL, this API has no effect.
 */
void NvOdmTmonDeviceClose(NvOdmTmonDeviceHandle hTmon);

/**
 * Gets TMON device capabilities.
 *
 * @param hTmon A handle to the TMON device.
 * @param pCaps A pointer to the TMON device capabilities returned by the ODM.
 */
void
NvOdmTmonCapabilitiesGet(
    NvOdmTmonDeviceHandle hTmon,
    NvOdmTmonCapabilities* pCaps);

/**
 * Gets TMON configuration parameter capabilities.
 *
 * @param hTmon A handle to the TMON device.
 * @param ParamId The targeted parameter. 
 * @param pCaps A pointer to the targeted parameter capabilities
 *  returned by the ODM.
 * 
 *  Special value ::ODM_TMON_PARAMETER_UNSPECIFIED is returned as maximum and
 *  minimum value in capabilities structure if the targeted parameter is not
 *  supported.
 */
void
NvOdmTmonParameterCapsGet(
    NvOdmTmonDeviceHandle hTmon,
    NvOdmTmonConfigParam ParamId,
    NvOdmTmonParameterCaps* pCaps);

/**
 * Gets current zone temperature.
 *
 * @param hTmon A handle to the TMON device.
 * @param pDegreesC A pointer to the zone temperature (in degrees C)
 *  returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmTmonTemperatureGet(
    NvOdmTmonDeviceHandle hTmon,
    NvS32* pDegreesC);

/**
 * Configures specified TMON parameter for the temperature zone.
 *
 * @param hTmon A handle to the TMON device.
 * @param ParamId The targeted parameter to be updated. 
 * @param pSetting A pointer to a variable with parameter settings.
 *  On entry, specifies new requested settings, on exit, actually configured
 *  settings as the best approximation of the request.
 * 
 *  The requested setting is clipped to the maximum/minimum values for the
 *  respective parameter. If special value ::ODM_TMON_PARAMETER_UNSPECIFIED is
 *  specified on entry, current parameter value is preserved and retrieved on
 *  exit. If special value \c ODM_TMON_PARAMETER_UNSPECIFIED is returned on exit,
 *  the targeted parameter is not supported for the given zone.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise
 */
NvBool
NvOdmTmonParameterConfig(
    NvOdmTmonDeviceHandle hTmon,
    NvOdmTmonConfigParam ParamId,
    NvS32* pSetting);

/**
 * Suspends temperature zone monitoring.
 * 
 * @param hTmon A handle to the TMON device.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmTmonSuspend(NvOdmTmonDeviceHandle hTmon);

/**
 * Resumes temperature zone monitoring.
 * 
 * @param hTmon A handle to the TMON device.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmTmonResume(NvOdmTmonDeviceHandle hTmon);

/**
 * Registers for TMON out of limit interrupt.
 * 
 * @param hTmon A handle to the TMON device.
 * @param Callback The callback function that is called when TMON
 *  interrupt triggers.
 * @param arg The argument passed to the callback when it is
 *  invoked by TMON IST.
 * 
 * @return TMON interrupt handle, NULL if failed to register.
 */
NvOdmTmonIntrHandle
NvOdmTmonIntrRegister(
    NvOdmTmonDeviceHandle hTmon,
    NvOdmInterruptHandler Callback,
    void* arg);

/**
 * Unregisters TMON interrupt.
 * 
 * @param hTmon A handle to the TMON device.
 * @param hIntr A TMON interrupt handle.
 *  If NULL, this API has no effect.
 */
void
NvOdmTmonIntrUnregister(
    NvOdmTmonDeviceHandle hTmon,
    NvOdmTmonIntrHandle hIntr);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_TMON_H
