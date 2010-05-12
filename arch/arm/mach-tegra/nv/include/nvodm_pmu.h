/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
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
 *         Power Management Unit Interface</b>
 *
 * @b Description: Defines the ODM interface for NVIDIA PMU devices.
 * 
 */

#ifndef INCLUDED_NVODM_PMU_H
#define INCLUDED_NVODM_PMU_H

#include "nvcommon.h"
#include "nvodm_query.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @defgroup nvodm_pmu Power Management Unit Adaptation Interface 
 *   
 * This is the power management unit (PMU) ODM adaptation interface, which
 * handles the abstraction of external power management devices.
 * For NVIDIA&reg; Driver Development Kit (DDK) clients, PMU is a 
 * set of voltages used to provide power to the SoC or to monitor low battery 
 * conditions. The API allows DDK clients to determine whether the
 * particular voltage is supported by the ODM platform, retrieve the
 * capabilities of PMU, and get/set voltage levels at runtime. 
 * On systems without power a management device, APIs should be dummy implemented.
 *
 * All voltage rails are referenced using ODM-assigned unsigned integers. ODMs
 * may select any convention for assigning these values; however, the values
 * accepted as input parameters by the PMU ODM adaptation interface must
 * match the values stored in the address field of ::NvOdmIoModule_Vdd buses
 * defined in the Peripheral Discovery ODM adaptation.
 * 
 * @ingroup nvodm_adaptation
 * @{
 */


/**
 * Defines an opaque handle that exists for each PMU device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmPmuDeviceRec *NvOdmPmuDeviceHandle;

/**
 * Combines information for the particular PMU Vdd rail.
 */
typedef struct NvOdmPmuVddRailCapabilitiesRec
{
    /// Specifies ODM protection attribute; if \c NV_TRUE PMU hardware
    ///  or ODM Kit would protect this voltage from being changed by NvDdk client.
    NvBool OdmProtected;

    /// Specifies the minimum voltage level in mV.
    NvU32 MinMilliVolts;

    /// Specifies the step voltage level in mV.
    NvU32 StepMilliVolts;

    /// Specifies the maximum voltage level in mV.
    NvU32 MaxMilliVolts;

    /// Specifies the request voltage level in mV.
    NvU32 requestMilliVolts;
} NvOdmPmuVddRailCapabilities;

/// Special level to indicate voltage plane is turned off.
#define ODM_VOLTAGE_OFF (0UL)

/// Special level to enable voltage plane on/off control
///  by the external signal (e.g., low power request from SoC).
#define ODM_VOLTAGE_ENABLE_EXT_ONOFF (0xFFFFFFFFUL)

/// Special level to disable voltage plane on/off control
///  by the external signal (e.g., low power request from SoC).
#define ODM_VOLTAGE_DISABLE_EXT_ONOFF (0xFFFFFFFEUL)

/**
 * Gets capabilities for the specified PMU voltage.
 *
 * @param vddId The ODM-defined PMU rail ID.
 * @param pCapabilities A pointer to the targeted
 *  capabilities returned by the ODM.
 * 
 */
void
NvOdmPmuGetCapabilities(
    NvU32 vddId,
    NvOdmPmuVddRailCapabilities* pCapabilities);


/**
 * Gets current voltage level for the specified PMU voltage.
 *
 * @param hDevice A handle to the PMU.
 * @param vddId The ODM-defined PMU rail ID.
 * @param pMilliVolts A pointer to the voltage level returned
 *  by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuGetVoltage(
    NvOdmPmuDeviceHandle hDevice,
    NvU32 vddId,
    NvU32* pMilliVolts);


/**
 * Sets new voltage level for the specified PMU voltage.
 *
 * @param hDevice A handle to the PMU.
 * @param vddId The ODM-defined PMU rail ID.
 * @param MilliVolts The new voltage level to be set in millivolts (mV).
 * - Set to ::ODM_VOLTAGE_OFF to turn off the target voltage.
 * - Set to ::ODM_VOLTAGE_ENABLE_EXT_ONOFF to enable external control of
 *   target voltage.
 * - Set to ::ODM_VOLTAGE_DISABLE_EXT_ONOFF to disable external control of
 *   target voltage.
 * @param pSettleMicroSeconds A pointer to the settling time in microseconds (uS),
 *  which is the time for supply voltage to settle after this function 
 *  returns; this may or may not include PMU control interface transaction time, 
 *  depending on the ODM implementation. If NULL this parameter is ignored, and the
 *  function must return only after the supply voltage has settled.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuSetVoltage(
    NvOdmPmuDeviceHandle hDevice,
    NvU32 vddId,
    NvU32 MilliVolts,
    NvU32* pSettleMicroSeconds);

/**
 * Gets a handle to the PMU in the system.
 *
 * @param hDevice A pointer to the handle of the PMU.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuDeviceOpen( NvOdmPmuDeviceHandle *hDevice );


/**
 *  Releases the PMU handle. 
 *
 * @param hDevice The PMU handle to be released. If 
 *     NULL, this API has no effect.
 */
void NvOdmPmuDeviceClose(NvOdmPmuDeviceHandle hDevice);


/**
 * Defines AC status.
 */
typedef enum
{
    /// Specifies AC is offline.
    NvOdmPmuAcLine_Offline,

    /// Specifies AC is online.
    NvOdmPmuAcLine_Online,

    /// Specifies backup power.
    NvOdmPmuAcLine_BackupPower,

    NvOdmPmuAcLine_Num,
    NvOdmPmuAcLine_Force32 = 0x7FFFFFFF
}NvOdmPmuAcLineStatus;

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
    NvOdmPmuBatteryInst_Main,

    /// Specifies backup battery.
    NvOdmPmuBatteryInst_Backup,

    NvOdmPmuBatteryInst_Num,
    NvOdmPmuBatteryInst_Force32 = 0x7FFFFFFF
    
}NvOdmPmuBatteryInstance;

/**
 * Defines battery data.
 */
typedef struct NvOdmPmuBatteryDataRec
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
    
}NvOdmPmuBatteryData;

/**
 * Defines battery chemistry.
 */
typedef enum
{
    /// Specifies an alkaline battery.
    NvOdmPmuBatteryChemistry_Alkaline,

    /// Specifies a nickel-cadmium (NiCd) battery.
    NvOdmPmuBatteryChemistry_NICD,

    /// Specifies a nickel-metal hydride (NiMH) battery.
    NvOdmPmuBatteryChemistry_NIMH,

    /// Specifies a lithium-ion (Li-ion) battery.
    NvOdmPmuBatteryChemistry_LION,

    /// Specifies a lithium-ion polymer (Li-poly) battery.
    NvOdmPmuBatteryChemistry_LIPOLY,

    /// Specifies a zinc-air battery.
    NvOdmPmuBatteryChemistry_XINCAIR,

    NvOdmPmuBatteryChemistry_Num,
    NvOdmPmuBatteryChemistry_Force32 = 0x7FFFFFFF
}NvOdmPmuBatteryChemistry;

/**
 * Gets the AC line status.
 *
 * @param hDevice A handle to the PMU.
 * @param pStatus A pointer to the AC line
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool 
NvOdmPmuGetAcLineStatus(
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuAcLineStatus *pStatus);


/**
 * Gets the battery status.
 *
 * @param hDevice A handle to the PMU.
 * @param batteryInst The battery type.
 * @param pStatus A pointer to the battery
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool 
NvOdmPmuGetBatteryStatus(
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuBatteryInstance batteryInst,
    NvU8 *pStatus);

/**
 * Gets the battery data.
 *
 * @param hDevice A handle to the PMU.
 * @param batteryInst The battery type.
 * @param pData A pointer to the battery
 *  data returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuGetBatteryData(
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuBatteryInstance batteryInst,
    NvOdmPmuBatteryData *pData);


/**
 * Gets the battery full life time.
 *
 * @param hDevice A handle to the PMU.
 * @param batteryInst The battery type.
 * @param pLifeTime A pointer to the battery
 *  full life time returned by the ODM.
 * 
 */
void
NvOdmPmuGetBatteryFullLifeTime(
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuBatteryInstance batteryInst,
    NvU32 *pLifeTime);


/**
 * Gets the battery chemistry.
 *
 * @param hDevice A handle to the PMU.
 * @param batteryInst The battery type.
 * @param pChemistry A pointer to the battery
 *  chemistry returned by the ODM.
 * 
 */
void
NvOdmPmuGetBatteryChemistry(
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuBatteryInstance batteryInst,
    NvOdmPmuBatteryChemistry *pChemistry);


/** 
* Defines the charging path. 
*/ 
typedef enum 
{ 
    /// Specifies external wall plug charger.
    NvOdmPmuChargingPath_MainPlug, 
 
    /// Specifies external USB bus charger.
    NvOdmPmuChargingPath_UsbBus, 
 
    NvOdmPmuChargingPath_Num, 
    /// Ignore. Forces compilers to make 32-bit enums.
    NvOdmPmuChargingPath_Force32 = 0x7FFFFFFF 
 
}NvOdmPmuChargingPath; 

/// Special level to indicate dumb charger current limit.
#define NVODM_DUMB_CHARGER_LIMIT (0xFFFFFFFFUL)

/// Special level to indicate USB Host mode current limit.
#define NVODM_USB_HOST_MODE_LIMIT (0x80000000UL)

/** 
* Sets the charging current limit. 
* 
* @param hDevice A handle to the PMU. 
* @param chargingPath The charging path. 
* @param chargingCurrentLimitMa The charging current limit in mA. 
* @param ChargerType The charger type.
* @return NV_TRUE if successful, or NV_FALSE otherwise. 
*/ 
NvBool 
NvOdmPmuSetChargingCurrent( 
    NvOdmPmuDeviceHandle hDevice, 
    NvOdmPmuChargingPath chargingPath, 
    NvU32 chargingCurrentLimitMa,
    NvOdmUsbChargerType ChargerType); 


/**
 * Handles the PMU interrupt.
 *
 * @param hDevice A handle to the PMU.
 */
void NvOdmPmuInterruptHandler( NvOdmPmuDeviceHandle  hDevice);

/**
 * Gets the count in seconds of the current external RTC (in PMU).
 *
 * @param hDevice A handle to the PMU.
 * @param Count A pointer to where to return the current counter in sec.
 * @return NV_TRUE if successful, or NV_FALSE otherwise. 
 */
NvBool
NvOdmPmuReadRtc(
    NvOdmPmuDeviceHandle hDevice,
    NvU32* Count);

/**
 * Updates current RTC value.
 *
 * @param hDevice A handle to the PMU.
 * @param Count data with which to update the current counter in sec.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuWriteRtc(
    NvOdmPmuDeviceHandle hDevice,
    NvU32 Count);

/**
 * Returns whether or not the RTC is initialized.
 *
 * @param hDevice A handle to the PMU.
  * @return NV_TRUE if initialized, or NV_FALSE otherwise.
 */
NvBool
NvOdmPmuIsRtcInitialized(
    NvOdmPmuDeviceHandle hDevice);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_PMU_H
