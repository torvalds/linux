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
 *         USB ULPI Interface</b>
 *
 * @b Description: Defines the ODM interface for USB ULPI device.
 */

#ifndef INCLUDED_NVODM_USBULPI_H
#define INCLUDED_NVODM_USBULPI_H


/**
 * @defgroup nvodm_usbulpi USB ULPI Adaptation Interface
 *
 * This is the USB ULPI ODM adaptation interface, which
 * handles the abstraction of opening and closing of the USB ULPI device.
 * For NVIDIA Driver Development Kit (NvDDK) clients, USB ULPI device
 * means a USB controller connected to a ULPI interface that has an
 * external phy. This API allows NvDDK clients to open the USB ULPI device by
 * setting the ODM specific clocks to ULPI controller or external phy, so that USB ULPI
 * device can be used.
 *
 * @ingroup nvodm_adaptation
 * @{
 */
#if defined(_cplusplus)
extern "C"
{
#endif

#include "nvcommon.h"


/**
 * Defines the USB ULPI context.
 */
typedef struct NvOdmUsbUlpiRec * NvOdmUsbUlpiHandle;

/**
 * Opens the USB ULPI device by setting the ODM-specific clocks 
 * and/or settings related to USB ULPI controller and external phy.
 * @param Instance The ULPI instance number.
 * @return A USB ULPI device handle on success, or NULL on failure.
*/
NvOdmUsbUlpiHandle  NvOdmUsbUlpiOpen(NvU32 Instance);

/** 
 * Closes the USB ULPI device handle by clearing 
 * the related ODM-specific clocks and settings.
 * @param hUsbUlpi A handle to USB ULPI device.
*/
void NvOdmUsbUlpiClose(NvOdmUsbUlpiHandle hUsbUlpi);


#if defined(_cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_USBULPI_H

