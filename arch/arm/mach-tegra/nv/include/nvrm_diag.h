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

#ifndef INCLUDED_nvrm_diag_H
#define INCLUDED_nvrm_diag_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"

#include "nvcommon.h"

/**
 * All of the hardware modules.  Multiple instances are handled by the
 * NVRM_DIAG_MODULE macro.
 */

typedef enum
{
    NvRmDiagModuleID_Cache = 1,
    NvRmDiagModuleID_Vcp,
    NvRmDiagModuleID_Host1x,
    NvRmDiagModuleID_Display,
    NvRmDiagModuleID_Ide,
    NvRmDiagModuleID_3d,
    NvRmDiagModuleID_Isp,
    NvRmDiagModuleID_Usb,
    NvRmDiagModuleID_2d,
    NvRmDiagModuleID_Vi,
    NvRmDiagModuleID_Epp,
    NvRmDiagModuleID_I2s,
    NvRmDiagModuleID_Pwm,
    NvRmDiagModuleID_Twc,
    NvRmDiagModuleID_Hsmmc,
    NvRmDiagModuleID_Sdio,
    NvRmDiagModuleID_NandFlash,
    NvRmDiagModuleID_I2c,
    NvRmDiagModuleID_Spdif,
    NvRmDiagModuleID_Gpio,
    NvRmDiagModuleID_Uart,
    NvRmDiagModuleID_Timer,
    NvRmDiagModuleID_Rtc,
    NvRmDiagModuleID_Ac97,
    NvRmDiagModuleID_Coprocessor,
    NvRmDiagModuleID_Cpu,
    NvRmDiagModuleID_Bsev,
    NvRmDiagModuleID_Bsea,
    NvRmDiagModuleID_Vde,
    NvRmDiagModuleID_Mpe,
    NvRmDiagModuleID_Emc,
    NvRmDiagModuleID_Sprom,
    NvRmDiagModuleID_Tvdac,
    NvRmDiagModuleID_Csi,
    NvRmDiagModuleID_Hdmi,
    NvRmDiagModuleID_MipiBaseband,
    NvRmDiagModuleID_Tvo,
    NvRmDiagModuleID_Dsi,
    NvRmDiagModuleID_Dvc,
    NvRmDiagModuleID_Sbc,
    NvRmDiagModuleID_Xio,
    NvRmDiagModuleID_Spi,
    NvRmDiagModuleID_NorFlash,
    NvRmDiagModuleID_Slc,
    NvRmDiagModuleID_Fuse,
    NvRmDiagModuleID_Pmc,
    NvRmDiagModuleID_StatMon,
    NvRmDiagModuleID_Kbc,
    NvRmDiagModuleID_Vg,
    NvRmDiagModuleID_ApbDma,
    NvRmDiagModuleID_Mc,
    NvRmDiagModuleID_SpdifIn,
    NvRmDiagModuleID_Vfir,
    NvRmDiagModuleID_Cve,
    NvRmDiagModuleID_ViSensor,
    NvRmDiagModuleID_SystemReset,
    NvRmDiagModuleID_AvpUcq,
    NvRmDiagModuleID_KFuse,
    NvRmDiagModuleID_OneWire,
    NvRmDiagModuleID_SyncNor,
    NvRmDiagModuleID_Pcie,
    NvRmDiagModuleID_Num,
    NvRmDiagModuleID_Force32 = 0x7FFFFFFF
} NvRmDiagModuleID;

/**
 * Create a diag module id with multiple instances.
 */
#define NVRM_DIAG_MODULE( id, instance ) \
    ((NvRmDiagModuleID)( (instance) << 16 | id ))

/**
 * Get the module id.
 */
#define NVRM_DIAG_MODULE_ID( id ) ((id) & 0xFFFF)

/**
 * Get the module instance.
 */
#define NVRM_DIAG_MODULE_INSTANCE( id ) (((id) >> 16) & 0xFFFF)

/**
 * Enable/disable support for individual clock diagnostic lock
 */
#define NVRM_DIAG_LOCK_SUPPORTED (0)

/**
 * Append clock configuration flags with diagnostic lock flag
 */
#define NvRmClockConfig_DiagLock ((NvRmClockConfigFlags_Num & (~0x01)) << 1)

/**
 * Defines clock source types
 */

typedef enum
{

    /// Clock source with fixed frequency
        NvRmDiagClockSourceType_Oscillator = 1,

    /// PLL clock source
        NvRmDiagClockSourceType_Pll,

    /// Clock scaler derives its clock from oscillators, PLLs or other scalers
        NvRmDiagClockSourceType_Scaler,
    NvRmDiagClockSourceType_Num,
    NvRmDiagClockSourceType_Force32 = 0x7FFFFFFF
} NvRmDiagClockSourceType;

/**
 * Defines types of clock scalers. Scale coefficient for all clock scalers
 * is specified as (m, n) pair of 32-bit values. The interpretation of the
 * m, n values for each type is clarified below.
 */

typedef enum
{

    /// No clock scaler: m = n = 1 always
        NvRmDiagClockScalerType_NoScaler = 1,

    /// Clock divider with m = 1 always, and n = 31.1 format
    /// with half-step lowest bit
        NvRmDiagClockScalerType_Divider_1_N,

    /// Clock divider with rational (m+1)/(n+1) coefficient; m and n are
    /// integeres, scale 1:1 is applied if m >= n
        NvRmDiagClockScalerType_Divider_M_N,

    /// Clock divider with rational (m+1)/16 coefficient, i.e., n = 16 always;
    /// m is integer, scale 1:1 is applied if m >= 15 ("keeps" m + 1 clocks
    /// out of every 16)
        NvRmDiagClockScalerType_Divider_M_16,

    /// Clock doubler: scale 2:1 if m != 0, scale 1:1 if m = 0,
    /// n = 1 always 
        NvRmDiagClockScalerType_Doubler,
    NvRmDiagClockScalerType_Num,
    NvRmDiagClockScalerType_Force32 = 0x7FFFFFFF
} NvRmDiagClockScalerType;

/**
 * Defines RM thermal monitoring zones.
 */

typedef enum
{

    /// Specifies ambient temperature zone.
        NvRmTmonZoneId_Ambient = 1,

    /// Specifies SoC core temperature zone.
        NvRmTmonZoneId_Core,
    NvRmTmonZoneId_Num,
    NvRmTmonZoneId_Force32 = 0x7FFFFFFF
} NvRmTmonZoneId;

/// Clock source opaque handle (TODO: replace forward idl declaration
/// of <enum> with forward declaration of <handle>, when it is supported 
typedef struct NvRmClockSourceInfoRec* NvRmDiagClockSourceHandle;

/// Power rail opaque handle

typedef struct NvRmDiagPowerRailRec *NvRmDiagPowerRailHandle;

/**
 * Enables diagnostic mode (disable is not allowed).  Clock, voltage, etc.,
 * will no longer be controlled by the Resource Manager. The NvRmDiag
 * interfaces should be used instead.
 * 
 * @param hDevice The RM device handle.
 * 
 * @retval NvSuccess if diagnostic mode is successfully enabled.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  diagnostic mode.
 */

 NvError NvRmDiagEnable( 
    NvRmDeviceHandle hDevice );

/**
 * Lists modules present in the chip and available for diagnostic.
 * 
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of Ids returned. If
 *  entry size is 0, maximum list size is returned. 
 * @param pIdList Pointer to the list of combined module Id/Instance values
 *  to be filled in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the module list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */                               

 NvError NvRmDiagListModules( 
    NvU32 * pListSize,
    NvRmDiagModuleID * pIdList );

/**
 * Lists available SoC clock sources.
 * 
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of source handles
 *  returned. If entry size is 0, maximum list size is returned.
 * @param phSourceList Pointer to the list of source handles to be filled
 *  in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the source list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */                               

 NvError NvRmDiagListClockSources( 
    NvU32 * pListSize,
    NvRmDiagClockSourceHandle * phSourceList );

/**
 * Lists clock sources for the specified module.
 * 
 * @param id Combined Id and instance for the target module.
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of source handles
 *  returned. If entry size is 0, maximum list size is returned.
 * @param phSourceList Pointer to the list of source handles to be filled
 *  in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the source list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagModuleListClockSources( 
    NvRmDiagModuleID id,
    NvU32 * pListSize,
    NvRmDiagClockSourceHandle * phSourceList );

/**
 * Enables/Disables specified module clock.
 * 
 * @param id Combined Id and instance for the target module.
 * @param enable Requested clock state - enabled if true, disabled if false
 * 
 * @retval NvSuccess if clock state changed successfully.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagModuleClockEnable( 
    NvRmDiagModuleID id,
    NvBool enable );

/**
 * Configures the clock for the specified module.
 *
 * @param id Combined Id and instance for the target module.
 * @param hSource The handle of the clock source to drive the given module.
 * @param divider 31.1 format: lowest bit is half-step. No range checking.
 *  Half-step bit is ignored if module divider is not fractional. High 
 *  bits are silently truncated if the value is out of h/w field range.
 * @param Source1st If true, clock source is updated 1st, and the divider
 *  is modified after the chip specific delay. If false, the order of update
 *  is the reversed.
 * 
 * @retval NvSuccess if clock state changed successfully.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagModuleClockConfigure( 
    NvRmDiagModuleID id,
    NvRmDiagClockSourceHandle hSource,
    NvU32 divider,
    NvBool Source1st );

/**
 * Gets the name of the given clock source..
 *
 * @param hSource The target clock source handle.
 * 
 * @return The 64-bit packed 8-character name of the given clock source. Zero
 *  will be returned if diagnostic mode is not enabled or the source is invalid.
 */

 NvU64 NvRmDiagClockSourceGetName( 
    NvRmDiagClockSourceHandle hSource );

/**
 * Gets the type of the given clock source.
 *
 * @param hSource The target clock source handle.
 * 
 * @return The type of the given clock source. Zero will be returned if
 *  diagnostic mode is not enabled or the source is invalid.
 */

 NvRmDiagClockSourceType NvRmDiagClockSourceGetType( 
    NvRmDiagClockSourceHandle hSource );

/**
 * Gets the type of the scaler for the given clock source.
 *
 * @param hSource The target clock source handle.
 * 
 * @return The type of the scaler for the given clock source. Zero will be
 *  be returned if diagnostic mode is not enabled or the source is invalid.
 */

 NvRmDiagClockScalerType NvRmDiagClockSourceGetScaler( 
    NvRmDiagClockSourceHandle hSource );

/**
 * Lists input clock sources for the specified clock source.
 * Primary oscillators have no input sources, and always return 0 as
 * list size. Other sources (secondary sources with fixed frequency,
 * PLLs and scalers) have 1 + input sources.
 * 
 * @param hSource The target clock source handle.
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of source handles
 *  returned. If entry size is 0, maximum list size is returned.
 * @param phSourceList Pointer to the list of source handles to be filled
 *  in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the source list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagClockSourceListSources( 
    NvRmDiagClockSourceHandle hSource,
    NvU32 * pListSize,
    NvRmDiagClockSourceHandle * phSourceList );

/**
 * Gets the given oscillator frequency in kHz.
 *
 * @param hOscillator The targeted oscillator/fixed frequency source handle.
 * 
 * @return The oscillator frequency in kHz. Zero will be returned if
 *  diagnostic mode is not enabled or the target source is invalid.
 */

 NvU32 NvRmDiagOscillatorGetFreq( 
    NvRmDiagClockSourceHandle hOscillator );

/**
 * Configures given PLL. Switches PLL in bypass mode, changes PLL settings,
 * waits for PLL stabilization, and switches back to PLL output.
 * 
 * @param hPll The targeted PLL  handle.
 * @param M Input divider settings (32-bit integer value)
 * @param N Feedback divider settings (32-bit integer value)
 * @param P Post divider settings (32-bit integer value)
 *  If either M or N is zero PLL is left disabled and bypassed. Bsides that,
 *  no other M, N, P parameters validation. High bits are silently truncated
 *  if value is out of h/w field range.
 * 
 * @retval NvSuccess if clock state changed successfully.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagPllConfigure( 
    NvRmDiagClockSourceHandle hPll,
    NvU32 M,
    NvU32 N,
    NvU32 P );

/**
 * Configures specified clock scaler.
 *
 * @param hScaler The targeted Clock Scaler handle.
 * @param hInput The handle of the input clock source to drive the
 *  targeted scaler.
 * @param M The dividend in the scaler coefficient (M/N) - 31.1 format:
 *  lowest bit is half-step.
 * @param N The divisor in the scaler coefficient (M/N) - 31.1 format:
 *  lowest bit is half-step.
 *  No range checking for M, N parameters. Half-step bit is ignored if
 *  the scaler is not fractional. High bits are silently truncated if
 *  the value is out of h/w field range.
 *
 * @retval NvSuccess if clock state changed successfully.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagClockScalerConfigure( 
    NvRmDiagClockSourceHandle hScaler,
    NvRmDiagClockSourceHandle hInput,
    NvU32 M,
    NvU32 N );

/**
 * Resets module.
 * 
 * @param id Combined Id and instance for the target module.
 * @param KeepAsserted If true, reset will be kept asserted on exit.
 *  If false, reset is kept asserted for chip specific delay, and
 *  de-asserted on exit.
 * 
 * @retval NvSuccess if module reset completed successfully.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagModuleReset( 
    NvRmDiagModuleID id,
    NvBool KeepAsserted );

/**
 * Lists power rails.
 * 
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of rail handles
 *  returned. If entry size is 0, maximum list size is returned.
 * @param phSourceList Pointer to the list of power rail handles to be filled
 *  in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the source list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */                               

 NvError NvRmDiagListPowerRails( 
    NvU32 * pListSize,
    NvRmDiagPowerRailHandle * phRailList );

/**
 * Gets the name of the given power rail.
 *
 * @param hRail The target power rail handle.
 * 
 * @return The 64-bit packed 8-character name of the given rail. Zero will be
 *  returned if diagnostic mode is not enabled or the rail is invalid.
 */

 NvU64 NvRmDiagPowerRailGetName( 
    NvRmDiagPowerRailHandle hRail );

/**
 * Lists power rails for the specified module.
 * 
 * @param id Combined Id and instance for the target module.
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the client, on exit - actual number of power rail handles
 *  returned. If entry size is 0, maximum list size is returned.
 * @param phRailList Pointer to the list of source handles to be filled
 *  in by this function. Ignored if input list size is 0.
 * 
 * @retval NvSuccess if the power rail list is successfully returned.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagModuleListPowerRails( 
    NvRmDiagModuleID id,
    NvU32 * pListSize,
    NvRmDiagPowerRailHandle * phRailList );

/**
 * Configures power rail voltage.
 * 
 * @param hRail The target power rail handle.
 * @param VoltageMV The requested voltage level in millivolts.
 * 
 * @retval NvSuccess if the power rail is successfully configured.
 * @retval NvError_NotInitialized if diagnostic mode is not enabled.
 */

 NvError NvRmDiagConfigurePowerRail( 
    NvRmDiagPowerRailHandle hRail,
    NvU32 VoltageMV );

/**
 * Verifies support for individual clock diagnostic lock (if supported
 * clock frequency can be locked when diagnostic mode is disabled).
 * 
 * @retval NV_TRUE if individual clock diagnostic lock is supported.
 * @retval NV_FALSE if individual clock diagnostic lock is not supported.
 */

 NvBool NvRmDiagIsLockSupported( 
    void  );

/**
 * Gets temperature in the specified thermal zone (used for
 * thermal profiling, does not require diagnostic mode to be enabled)
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param ZoneId The targeted thermal zone ID.
 * @param pTemperatureC Output storage pointer for zone temperature
 *  (in degrees C).
 * 
 * @retval NvSuccess if temperature is returned successfully.
 * @retval NvError_Busy if attempt to access temperature monitoring
 *  device failed.
 * @retval NvError_NotSupported if the specified zone is not monitored.
 */

 NvError NvRmDiagGetTemperature( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmTmonZoneId ZoneId,
    NvS32 * pTemperatureC );

#if defined(__cplusplus)
}
#endif

#endif
