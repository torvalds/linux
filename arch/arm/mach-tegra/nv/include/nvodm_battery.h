/*
 * Copyright (c) 2009-2010 NVIDIA Corporation.
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
 *         Battery Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for
 *    Embedded Controller (EC) based battery interface.
 *    Note that this doesn't use PMU interface.
 *    EC Interface is used to get battery and power supply
 *    information and configure for events.
 *    Battery charging is taken care by EC firmware itself.
 */

#ifndef INCLUDED_NVODM_BATTERY_H
#define INCLUDED_NVODM_BATTERY_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"

/**
 * @defgroup nvodm_battery_group Battery Adaptation Interface 
 *
 * @ingroup nvodm_adaptation
 * @{
 */

/**
 * Defines an opaque handle that exists for each battery device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmBatteryDeviceRec *NvOdmBatteryDeviceHandle;

/**
 * Defines the AC status.
 */
typedef enum
{
    /// Specifies AC is offline.
    NvOdmBatteryAcLine_Offline,

    /// Specifies AC is online.
    NvOdmBatteryAcLine_Online,

    /// Specifies backup power.
    NvOdmBatteryAcLine_BackupPower,

    NvOdmBatteryAcLine_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmBatteryAcLine_Force32 = 0x7FFFFFFF
}NvOdmBatteryAcLineStatus;

/**
 * Defines the battery events.
 */
typedef enum
{
    /// Indicates battery present state.
    NvOdmBatteryEventType_Present = 0x01,

    /// Indicates idle state.
    NvOdmBatteryEventType_Idle = 0x02,

    /// Indicates charging state.
    NvOdmBatteryEventType_Charging = 0x04,

    /// Indicates disharging state.
    NvOdmBatteryEventType_Disharging = 0x08,

    /// Indicates remaining capacity alarm set.
    NvOdmBatteryEventType_RemainingCapacityAlarm = 0x10,

    NvOdmBatteryEventType_Num = 0x20,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmBatteryEventType_Force32 = 0x7FFFFFFF
}NvOdmBatteryEventType;

/** @name Battery Status Defines */
/*@{*/

#define NVODM_BATTERY_STATUS_HIGH               0x01
#define NVODM_BATTERY_STATUS_LOW                0x02
#define NVODM_BATTERY_STATUS_CRITICAL           0x04
#define NVODM_BATTERY_STATUS_CHARGING           0x08
#define NVODM_BATTERY_STATUS_DISCHARGING        0x10
#define NVODM_BATTERY_STATUS_IDLE               0x20
#define NVODM_BATTERY_STATUS_VERY_CRITICAL      0x40
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
    NvOdmBatteryInst_Main,

    /// Specifies backup battery.
    NvOdmBatteryInst_Backup,

    NvOdmBatteryInst_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmBatteryInst_Force32 = 0x7FFFFFFF
    
}NvOdmBatteryInstance;

/**
 * Defines battery data.
 */
typedef struct NvOdmBatteryDataRec
{
    /// Specifies battery life percent.
    NvU32 BatteryLifePercent;

    /// Specifies battery lifetime.
    NvU32 BatteryLifeTime;

    /// Specifies voltage.
    NvU32 BatteryVoltage;

    /// Specifies battery current.
    NvS32 BatteryCurrent;

    /// Specifies battery average current.
    NvS32 BatteryAverageCurrent;

    /// Specifies battery interval.
    NvU32 BatteryAverageInterval;

    /// Specifies the mAH consumed.
    NvU32 BatteryMahConsumed;

    /// Specifies battery temperature.
    NvU32 BatteryTemperature;

    /// Specifies battery remaining capacity.
    NvU32 BatteryRemainingCapacity;

    /// Specifies battery last charge full capacity.
    NvU32 BatteryLastChargeFullCapacity;

    /// Specifies battery critical capacity.
    NvU32 BatteryCriticalCapacity;

}NvOdmBatteryData;

/**
 * Defines battery chemistry.
 */
typedef enum
{
    /// Specifies an alkaline battery.
    NvOdmBatteryChemistry_Alkaline,

    /// Specifies a nickel-cadmium (NiCd) battery.
    NvOdmBatteryChemistry_NICD,

    /// Specifies a nickel-metal hydride (NiMH) battery.
    NvOdmBatteryChemistry_NIMH,

    /// Specifies a lithium-ion (Li-ion) battery.
    NvOdmBatteryChemistry_LION,

    /// Specifies a lithium-ion polymer (Li-poly) battery.
    NvOdmBatteryChemistry_LIPOLY,

    /// Specifies a zinc-air battery.
    NvOdmBatteryChemistry_XINCAIR,

    NvOdmBatteryChemistry_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmBatteryChemistry_Force32 = 0x7FFFFFFF
}NvOdmBatteryChemistry;

/**
 * Opens the handle for battery ODM.
 *
 * @param hDevice A pointer to the handle to the battery ODM.
 * @param hOdmSemaphore Battery events signal this registered semaphore.
 *                      Can Pass NULL if events are not needed by client.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmBatteryDeviceOpen(NvOdmBatteryDeviceHandle *hDevice,
                              NvOdmOsSemaphoreHandle *hOdmSemaphore);

/**
 * Closes the handle for battery ODM.
 *
 * @param hDevice A handle to the battery ODM.
 */
void NvOdmBatteryDeviceClose(NvOdmBatteryDeviceHandle hDevice);

/**
 * Gets the AC line status.
 *
 * @param hDevice A handle to the EC.
 * @param pStatus A pointer to the AC line
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmBatteryGetAcLineStatus(
       NvOdmBatteryDeviceHandle hDevice,
       NvOdmBatteryAcLineStatus *pStatus);


/**
 * Gets the battery status.
 *
 * @param hDevice A handle to the EC.
 * @param batteryInst The battery type.
 * @param pStatus A pointer to the battery
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmBatteryGetBatteryStatus(
       NvOdmBatteryDeviceHandle hDevice,
       NvOdmBatteryInstance batteryInst,
       NvU8 *pStatus);

/**
 * Gets the battery data.
 *
 * @param hDevice A handle to the EC.
 * @param batteryInst The battery type.
 * @param pData A pointer to the battery
 *  data returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmBatteryGetBatteryData(
       NvOdmBatteryDeviceHandle hDevice,
       NvOdmBatteryInstance batteryInst,
       NvOdmBatteryData *pData);


/**
 * Gets the battery full lifetime.
 *
 * @param hDevice A handle to the EC.
 * @param batteryInst The battery type.
 * @param pLifeTime A pointer to the battery
 *  full lifetime returned by the ODM.
 * 
 */
void NvOdmBatteryGetBatteryFullLifeTime(
     NvOdmBatteryDeviceHandle hDevice,
     NvOdmBatteryInstance batteryInst,
     NvU32 *pLifeTime);

/**
 * Gets the battery chemistry.
 *
 * @param hDevice A handle to the EC.
 * @param batteryInst The battery type.
 * @param pChemistry A pointer to the battery
 *  chemistry returned by the ODM.
 * 
 */
void NvOdmBatteryGetBatteryChemistry(
     NvOdmBatteryDeviceHandle hDevice,
     NvOdmBatteryInstance batteryInst,
     NvOdmBatteryChemistry *pChemistry);

/**
 * Gets the battery event.
 *
 * @param hDevice A handle to the EC.
 * @param pBatteryEvent A pointer to the battery events.
 * 
 */
void NvOdmBatteryGetEvent(
     NvOdmBatteryDeviceHandle hDevice,
     NvU8   *pBatteryEvent);


/**
 * Gets the battery manufacturer.
 *
 * @param hDevice [IN] A handle to the EC.
 * @param BatteryInst [IN] The battery type.
 * @param pBatteryManufacturer [OUT] A pointer to the battery manufacturer.
 */
NvBool NvOdmBatteryGetManufacturer(
       NvOdmBatteryDeviceHandle hDevice,
       NvOdmBatteryInstance BatteryInst,
       NvU8 *pBatteryManufacturer);

/**
 * Gets the battery model.
 *
 * @param hDevice [IN] A handle to the EC.
 * @param BatteryInst [IN] The battery type.
 * @param pBatteryModel [OUT] A pointer to the battery model.
 */
NvBool NvOdmBatteryGetModel(
       NvOdmBatteryDeviceHandle hDevice,
       NvOdmBatteryInstance BatteryInst,
       NvU8 *pBatteryModel);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_BATTERY_H
