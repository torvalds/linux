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
 *         Touch Pad Sensor Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for touch pad sensor devices.
 *
 */

#ifndef INCLUDED_NVODM_MOUSE_H
#define INCLUDED_NVODM_MOUSE_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"
#include "nverror.h"

/**
 * @defgroup nvodm_mouse Mouse Adaptation Interface
 *
 * This is the mouse ODM adaptation interface.
 *
 * @ingroup nvodm_adaptation
 * @{
 */


/**
 * Defines an opaque handle that exists for each mouse device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmMouseDeviceRec *NvOdmMouseDeviceHandle;


/**
 * Gets a handle to the mouse in the system.
 *
 * @param hDevice A pointer to the handle of the mouse.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmMouseDeviceOpen( NvOdmMouseDeviceHandle *hDevice );

/**
 * Hooks up the interrupt handle to the GPIO interrupt and enables the interrupt.
 *
 * @param hDevice The handle to the mouse.
 * @param hInterruptSemaphore A handle to hook up the interrupt.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmMouseEnableInterrupt(NvOdmMouseDeviceHandle hDevice, NvOdmOsSemaphoreHandle hInterruptSemaphore);

/**
 * Un-registers the interrupt.
 *
 * @param hDevice The handle to the mouse.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmMouseDisableInterrupt(NvOdmMouseDeviceHandle hDevice);

/**
 * Returns the event response information.
 *
 * @param hDevice A handle to the mouse.
 * @param NumPayLoad A pointer to the number of bytes in the returned payload.
 * @param PayLoadBuf A pointer to the returned payload buffer.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmMouseGetEventInfo(NvOdmMouseDeviceHandle hDevice, NvU32 *NumPayLoad, NvU8 *PayLoadBuf);

/**
 *  Releases the mouse handle.
 *
 * @param hDevice The mouse handle to be released. If
 *     NULL, this API has no effect.
 */
void NvOdmMouseDeviceClose(NvOdmMouseDeviceHandle hDevice);

/**
 *  Sends the commands to HW in a form of request packet.
 *
 * @param hDevice A handle to the mouse.
 * @param cmd The command to send.
 * @param ExpectedResponseSize The size expected in the response.
 * @param NumPayLoad A pointer to the number of bytes in the payload.
 * @param PayLoadBuf A pointer to the payload buffer.
 */
NvBool
NvOdmMouseSendRequest(
    NvOdmMouseDeviceHandle hDevice, 
    NvU32 cmd,
    NvU32 ExpectedResponseSize,
    NvU32 *NumPayLoad,
    NvU8 *PayLoadBuf);

/**
 * Enables the EC to stream data from the mouse.
 *
 * @param hDevice A handle to the mouse.
 * @param NumBytesPerSample Number of payload bytes to be sent in each event
 *        packet.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmMouseStartStreaming(
    NvOdmMouseDeviceHandle hDevice,
    NvU32 NumBytesPerSample);

/**
 *  Suspends power for mouse.
 *
 * @param hDevice A handle to the mouse.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmMousePowerSuspend(NvOdmMouseDeviceHandle hDevice);

/**
 *  Resumes power for mouse.
 *
 * @param hDevice A handle to the mouse.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
*/
NvBool NvOdmMousePowerResume(NvOdmMouseDeviceHandle hDevice);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_MOUSE_H
