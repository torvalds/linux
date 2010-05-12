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
 *         SDIO Adaptation Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for SDIO devices.
 * 
 */

#ifndef INCLUDED_NVODM_SDIO_H
#define INCLUDED_NVODM_SDIO_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"
#include "nverror.h"

/**
 * @defgroup nvodm_sdio SDIO Adaptation Interface
 *
 * This is the SDIO ODM adaptation interface.
 *
 * @ingroup nvodm_adaptation
 * @{
 */
 

/**
 * Defines an opaque handle that exists for each SDIO device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmSdioRec *NvOdmSdioHandle;

/**
 * Gets a handle to the SDIO device.
 *
 * @return A handle to the SDIO device.
 */
NvOdmSdioHandle NvOdmSdioOpen(NvU32 Instance);

/**
 * Closes the SDIO handle. 
 *
 * @param hOdmSdio The SDIO handle to be closed.
 */
void NvOdmSdioClose(NvOdmSdioHandle hOdmSdio);

/**
 * Suspends the SDIO device.
 * @param hOdmSdio The handle to SDIO device.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmSdioSuspend(NvOdmSdioHandle hOdmSdio);

/**
 * Resumes the SDIO device from suspend mode.
 * @param hOdmSdio The handle to SDIO device.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmSdioResume(NvOdmSdioHandle hOdmSdio);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_Sdio_H
