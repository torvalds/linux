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
 *         UART Adaptation Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for UART devices.
 * 
 */

#ifndef INCLUDED_NVODM_UART_H
#define INCLUDED_NVODM_UART_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"
#include "nverror.h"

/**
 * @defgroup nvodm_uart UART Adaptation Interface
 * 
 * This is the UART ODM adaptation interface. 
 * 
 * @ingroup nvodm_adaptation
 * @{
 */

/**
 * Defines an opaque handle that exists for each UART device in the
 * system, each of which is defined by the customer implementation.
 */
typedef struct NvOdmUartRec *NvOdmUartHandle;

/**
 * Gets a handle to the UART device.
 *
 * @param Instance         [IN] UART instance number
 *
 * @return A handle to the UART device.
 */
NvOdmUartHandle NvOdmUartOpen(NvU32 Instance);

/**
 * Closes the UART handle. 
 *
 * @param hOdmUart The UART handle to be closed.
 */
void NvOdmUartClose(NvOdmUartHandle hOdmUart);

/**
 * Call this API whenever the UART device goes into suspend mode.
 *
 * @param hOdmUart The UART handle.
  */
NvBool NvOdmUartSuspend(NvOdmUartHandle hOdmUart);

/**
 * Call this API whenever the UART device resumes from the suspend mode.
 *
 * @param hOdmUart The UART handle.
  */
NvBool NvOdmUartResume(NvOdmUartHandle hOdmUart);


#if defined(__cplusplus)
}
#endif

/** @} */

#endif // INCLUDED_NVODM_uart_H
