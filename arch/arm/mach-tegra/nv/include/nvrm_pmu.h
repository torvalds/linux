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

#ifndef INCLUDED_nvrm_pmu_H
#define INCLUDED_nvrm_pmu_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"

/**
 * @defgroup nvrm_pmu 
 *   
 * This is the power management unit (PMU) API for Rm, which
 * handles the abstraction of external power management devices.
 * For NVIDIA&reg; Driver Development Kit (DDK) clients, PMU is a 
 * set of voltages used to provide power to the SoC or to monitor low battery 
 * conditions. The API allows DDK clients to determine whether the
 * particular voltage is supported by the ODM platform, retrieve the
 * capabilities of PMU, and get/set voltage levels at runtime. 
 *
 * All voltage rails are referenced using ODM-assigned unsigned integers. ODMs
 * may select any convention for assigning these values; however, the values
 * accepted as input parameters by the PMU ODM adaptation interface must
 * match the values stored in the address field of \c NvRmIoModule_Vdd buses
 * defined in the Peripheral Discovery ODM adaptation.
 * 
 *
 * @ingroup nvrm_pmu
 * @{
 */

/**
 * Combines information for the particular PMU Vdd rail.
 */

typedef struct NvRmPmuVddRailCapabilitiesRec
{

    /// Specifies ODM protection attribute; if \c NV_TRUE PMU hardware
    ///  or ODM Kit would protect this voltage from being changed by NvDdk client.
        NvBool RmProtected;

    /// Specifies the minimum voltage level in mV.
        NvU32 MinMilliVolts;

    /// Specifies the step voltage level in mV.
        NvU32 StepMilliVolts;

    /// Specifies the maximum voltage level in mV.
        NvU32 MaxMilliVolts;

    /// Specifies the request voltage level in mV.
        NvU32 requestMilliVolts;
} NvRmPmuVddRailCapabilities;

/// Special level to indicate voltage plane is disabled.
#define ODM_VOLTAGE_OFF (0UL)

/**
 * Gets capabilities for the specified PMU voltage.
 *
 * @param vddId The ODM-defined PMU rail ID.
 * @param pCapabilities A pointer to the targeted
 *  capabilities returned by the ODM.
 */

 void NvRmPmuGetCapabilities( 
    NvRmDeviceHandle hDevice,
    NvU32 vddId,
    NvRmPmuVddRailCapabilities * pCapabilities );

/**
 * Gets current voltage level for the specified PMU voltage.
 *
 * @param hDevice The Rm device handle.
 * @param vddId The ODM-defined PMU rail ID.
 * @param pMilliVolts A pointer to the voltage level returned
 *  by the ODM.
 */

 void NvRmPmuGetVoltage( 
    NvRmDeviceHandle hDevice,
    NvU32 vddId,
    NvU32 * pMilliVolts );

/**
 * Sets new voltage level for the specified PMU voltage.
 *
 * @param hDevice The Rm device handle.
 * @param vddId The ODM-defined PMU rail ID.
 * @param MilliVolts The new voltage level to be set in millivolts (mV).
 *  Set to \c ODM_VOLTAGE_OFF to turn off the target voltage.
 * @param pSettleMicroSeconds A pointer to the settling time in microseconds (uS),
 *  which is the time for supply voltage to settle after this function 
 *  returns; this may or may not include PMU control interface transaction time, 
 *  depending on the ODM implementation. If null this parameter is ignored. 
 */

 void NvRmPmuSetVoltage( 
    NvRmDeviceHandle hDevice,
    NvU32 vddId,
    NvU32 MilliVolts,
    NvU32 * pSettleMicroSeconds );

/**
 * Configures SoC power rail controls for the upcoming PMU voltage transition.
 *
 * @note Should be called just before PMU rail On/Off, or Off/On transition.
 *  Should not be called if rail voltage level is changing within On range.
 * 
 * @param hDevice The Rm device handle.
 * @param vddId The ODM-defined PMU rail ID.
 * @param Enable Set NV_TRUE if target voltage is about to be turned On, or
 *  NV_FALSE if target voltage is about to be turned Off.
 */

 void NvRmPmuSetSocRailPowerState( 
    NvRmDeviceHandle hDevice,
    NvU32 vddId,
    NvBool Enable );

/**
 * Defines Charging path.
 */

typedef enum
{

    /// Specifies external wall plug charger.
        NvRmPmuChargingPath_MainPlug,

    /// Specifies external USB bus charger.
        NvRmPmuChargingPath_UsbBus,
    NvRmPmuChargingPath_Num,
    NvRmPmuChargingPath_Force32 = 0x7FFFFFFF
} NvRmPmuChargingPath;

/// Special level to indicate dumb charger current limit.
#define NVODM_DUMB_CHARGER_LIMIT (0xFFFFFFFFUL)

/**
 * Defines AC status.
 */

typedef enum
{

    /// Specifies AC is offline.
        NvRmPmuAcLine_Offline,

    /// Specifies AC is online.
        NvRmPmuAcLine_Online,

    /// Specifies backup power.
        NvRmPmuAcLine_BackupPower,
    NvRmPmuAcLineStatus_Num,
    NvRmPmuAcLineStatus_Force32 = 0x7FFFFFFF
} NvRmPmuAcLineStatus;

/** @name Battery Status Defines */
/*@{*/

#define NVODM_BATTERY_STATUS_HIGH               0x01
#define NVODM_BATTERY_STATUS_LOW                0x02
#define NVODM_BATTERY_STATUS_CRITICAL           0x04
#define NVODM_BATTERY_STATUS_CHARGING           0x08
#define NVODM_BATTERY_STATUS_NO_BATTERY         0x80
#define NVODM_BATTERY_STATUS_UNKNOWN            0xFF

/*@}*/
/** @name Battery Data Defines */
/*@{*/
#define NVODM_BATTERY_DATA_UNKNOWN              0x7FFFFFFF

/*@}*/

/**
 * Defines battery instances.
 */

typedef enum
{

    /// Specifies main battery.
        NvRmPmuBatteryInst_Main,
    NvRmPmuBatteryInst_Backup,
    NvRmPmuBatteryInstance_Num,
    NvRmPmuBatteryInstance_Force32 = 0x7FFFFFFF
} NvRmPmuBatteryInstance;

/**
 * Defines battery data.
 */

typedef struct NvRmPmuBatteryDataRec
{

    /// Specifies battery life percent.
        NvU32 batteryLifePercent;

    /// Specifies battery life time.
        NvU32 batteryLifeTime;

    /// Specifies voltage.
        NvU32 batteryVoltage;

    /// Specifies battery current.
        NvS32 batteryCurrent;

    /// Specifies battery average current.
        NvS32 batteryAverageCurrent;

    /// Specifies battery interval.
        NvU32 batteryAverageInterval;

    /// Specifies the mAH consumed.
        NvU32 batteryMahConsumed;

    /// Specifies battery temperature.
        NvU32 batteryTemperature;
} NvRmPmuBatteryData;

/**
 * Defines battery chemistry.
 */

typedef enum
{

    /// Specifies an alkaline battery.
        NvRmPmuBatteryChemistry_Alkaline,

    /// Specifies a nickel-cadmium (NiCd) battery.
        NvRmPmuBatteryChemistry_NICD,

    /// Specifies a nickel-metal hydride (NiMH) battery.
        NvRmPmuBatteryChemistry_NIMH,

    /// Specifies a lithium-ion (Li-ion) battery.
        NvRmPmuBatteryChemistry_LION,

    /// Specifies a lithium-ion polymer (Li-poly) battery.
        NvRmPmuBatteryChemistry_LIPOLY,

    /// Specifies a zinc-air battery.
        NvRmPmuBatteryChemistry_XINCAIR,
    NvRmPmuBatteryChemistry_Num,
    NvRmPmuBatteryChemistry_Force32 = 0x7FFFFFFF
} NvRmPmuBatteryChemistry;

/** 
* Sets the charging current limit. 
* 
* @param hRmDevice The Rm device handle.
* @param ChargingPath The charging path. 
* @param ChargingCurrentLimitMa The charging current limit in mA. 
* @param ChargerType Type of the charger detected 
*        @see NvOdmUsbChargerType
*/

 void NvRmPmuSetChargingCurrentLimit( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuChargingPath ChargingPath,
    NvU32 ChargingCurrentLimitMa,
    NvU32 ChargerType );

/**
 * Gets the AC line status.
 *
 * @param hDevice The Rm device handle.
 * @param pStatus A pointer to the AC line
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */

 NvBool NvRmPmuGetAcLineStatus( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuAcLineStatus * pStatus );

/**
 * Gets the battery status.
 *
 * @param hDevice The Rm device handle.
 * @param batteryInst The battery type.
 * @param pStatus A pointer to the battery
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */

 NvBool NvRmPmuGetBatteryStatus( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuBatteryInstance batteryInst,
    NvU8 * pStatus );

/**
 * Gets the battery data.
 *
 * @param hDevice The Rm device handle.
 * @param batteryInst The battery type.
 * @param pData A pointer to the battery
 *  data returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */

 NvBool NvRmPmuGetBatteryData( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuBatteryInstance batteryInst,
    NvRmPmuBatteryData * pData );

/**
 * Gets the battery full life time.
 *
 * @param hDevice The Rm device handle.
 * @param batteryInst The battery type.
 * @param pLifeTime A pointer to the battery
 *  full life time returned by the ODM.
 * 
 */

 void NvRmPmuGetBatteryFullLifeTime( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuBatteryInstance batteryInst,
    NvU32 * pLifeTime );

/**
 * Gets the battery chemistry.
 *
 * @param hDevice The Rm device handle.
 * @param batteryInst The battery type.
 * @param pChemistry A pointer to the battery
 *  chemistry returned by the ODM.
 * 
 */

 void NvRmPmuGetBatteryChemistry( 
    NvRmDeviceHandle hRmDevice,
    NvRmPmuBatteryInstance batteryInst,
    NvRmPmuBatteryChemistry * pChemistry );

/**
 * Reads current RTC count in seconds.
 *
 * @param hRmDevice The Rm device handle.
 * @param Count A pointer to the RTC count returned by this function.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise. 
 */

 NvBool NvRmPmuReadRtc( 
    NvRmDeviceHandle hRmDevice,
    NvU32 * pCount );

/**
 * Updates current RTC seconds count.
 *
 * @param hRmDevice The Rm device handle.
 * @param Count Seconds count to update the RTC counter.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */

 NvBool NvRmPmuWriteRtc( 
    NvRmDeviceHandle hRmDevice,
    NvU32 Count );

/**
 * Verifies whether the RTC is initialized.
 *
 * @param hRmDevice The Rm device handle.
 * 
 * @return NV_TRUE if initialized, or NV_FALSE otherwise.
 */

 NvBool NvRmPmuIsRtcInitialized( 
    NvRmDeviceHandle hRmDevice );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
