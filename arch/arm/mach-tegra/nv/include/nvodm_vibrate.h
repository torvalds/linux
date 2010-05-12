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
 *         Vibrate Interface</b>
 *
 * @b Description: Defines the ODM interface for vibrate devices.
 * 
 */

#ifndef INCLUDED_NVODM_VIBRATE_H
#define INCLUDED_NVODM_VIBRATE_H

#include "nvcommon.h"

/**
 * @defgroup nvodm_vibrate Vibrate Adaption Interface
 *
 * This is the Vibrate ODM adaptation interface.
 *
 * @ingroup nvodm_adaptation
 * @{
 */

/**
 *  @brief Opaque handle to the vibrate device.
 */
typedef struct NvOdmVibDeviceRec *NvOdmVibDeviceHandle;

/**
 * @brief Defines attributes that can be set/queried by clients.
 */
typedef enum
{
    NvOdmVibCaps_Invalid = 0x0,
    /** Specifies the maximum supported frequency. */
    NvOdmVibCaps_MaxFreq,
    /** Specifies the minimum supported frequency. */
    NvOdmVibCaps_MinFreq,
    /** Specifies the maximum supported duty cycle. */
    NvOdmVibCaps_MaxDutyCycle,
    NvOdmVibCaps_Num,
    NvOdmVibCaps_Force32 = 0x7FFFFFFF,
} NvOdmVibCaps;

#if defined(_cplusplus)
extern "C"
{
#endif

/**
 *  @brief Allocates a handle to the device. Configures the PWM
 *   control to the vibro motor with default values. To change
 *   the duty cycle and frequency, use NvOdmVibSetFrequency() and
 *   NvOdmVibSetDutyCycle() APIs.
 *  @param hOdmVibrate  [IN] A pointer to the opaque handle to the device.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibOpen(NvOdmVibDeviceHandle *hOdmVibrate);

/**
 *  @brief Closes the ODM device and destroys all allocated resources.
 *  @param hOdmVibrate  [IN] The opaque handle to the device.
 */
void
NvOdmVibClose(NvOdmVibDeviceHandle hOdmVibrate);

/**
 *  @brief Gets capabilities of the vibrate device.
 *  @param hDevice	    [IN] The opaque handle to the device.
 *  @param RequestedCaps  [IN] Specifies the capability to get.
 *  @param pCapsValue    [OUT] A pointer to the returned value.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibGetCaps(
    NvOdmVibDeviceHandle hDevice,
    NvOdmVibCaps RequestedCaps,
    NvU32 *pCapsValue);

/**
 *  @brief Sets the frequency to the vibro motor.
 *    A frequency less than zero will be set to zero 
 *    and a frequency value beyond the maximum supported value
 *    will be set to the maximum supported value.
 *  @param hDevice	  [IN] The opaque handle to the device.
 *  @param Freq         [IN] The frequency to set in Hz.
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibSetFrequency(NvOdmVibDeviceHandle hDevice, NvS32 Freq);

/**
 *  @brief Sets the dutycycle of the PWM driving the vibro motor.
 *    A duty cycle less than zero will be set to zero 
 *    and value beyond the maximum supported value
 *    will be set to the maximum supported value.
 *  @param hDevice  [IN] The opaque handle to the device.
 *  @param DCycle       [IN] The duty cycle value to set in percentage (0%-100%).
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibSetDutyCycle(NvOdmVibDeviceHandle hDevice, NvS32 DCycle);

/**
 *  @brief Starts the vibro with the frequency and duty cycle set using the
 *    set API.
 *  @param hDevice  [IN] The opaque handle to the device.
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibStart(NvOdmVibDeviceHandle hDevice);

/**
 *  @brief Stops the vibro motor.
 *  @param hDevice  [IN] The opaque handle to the device.
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmVibStop(NvOdmVibDeviceHandle hDevice);

#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_VIBRATE_H
