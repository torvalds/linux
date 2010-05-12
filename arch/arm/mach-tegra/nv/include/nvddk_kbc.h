/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
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
 * <b>NVIDIA Driver Development Kit:
 *           Key Board Controller (KBC) Interface</b>
 *
 * @b Description: Declares interface for the KBC DDK module.
 *
 */

#ifndef INCLUDED_NVDDK_KBC_H
#define INCLUDED_NVDDK_KBC_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"
#include "nvcommon.h"
#include "nvos.h"

/**
 * @defgroup nvddk_kbc Keyboard Controller Interface
 * 
 * This is the interface to a hardware keyboard controller. 
 * This keeps track of the keys that are pressed. Only one
 * client is allowed at a time.
 * 
 * @ingroup nvddk_modules
 * @{
 */

/** 
 * An opaque context to the NvDdkKbcRec interface.
 */
typedef struct NvDdkKbcRec *NvDdkKbcHandle;

typedef enum
{

    /// Indicates the key press event.
    NvDdkKbcKeyEvent_KeyPress = 1,

    /// Indicates the key release event.
    NvDdkKbcKeyEvent_KeyRelease,

    /// Indicates key event none.
    NvDdkKbcKeyEvent_None,
    NvDdkKbcKeyEvent_Num,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvDdkKbcKeyEvent_Force32 = 0x7FFFFFFF
} NvDdkKbcKeyEvent;

/**
 * Initializes the keyboard controller.
 * It allocates resources such as memory, mutexes, and sets up the 
 * KBC handle.
 *
 * @param hDevice Handle to the Rm device that is required by NvDDK
 * to acquire the resources from RM.
 * @param phKbc A pointer to the KBC handle where the
 *       allocated handle is stored. The memory for the handle is
 *       allocated inside this API.
 *
 * @retval NvSuccess Open is successful.
 */
 NvError NvDdkKbcOpen( 
    NvRmDeviceHandle hDevice,
    NvDdkKbcHandle * phKbc );

/**
 * Releases the KBC handle and releases any resources that 
 *   are acquired during the NvDdkKbcOpen() call.
 * 
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 */

 void NvDdkKbcClose( 
    NvDdkKbcHandle hKbc );

/**
 * Enables the keyboard controller. This must be called once to 
 *      receive the key events.
 *
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 * @param SemaphoreId Semaphore to be signaled on any key event.
 *
 * @retval NvSuccess KBC is enabled successfully.
 */
 NvError NvDdkKbcStart( 
    NvDdkKbcHandle hKbc,
    NvOsSemaphoreHandle SemaphoreId );

/**
 * Disables the keyboard controller. This must be called to 
 * stop receiving the key events.
 *
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 *
 * @retval NvSuccess KBC is disabled successfully.
 */
 NvError NvDdkKbcStop( 
    NvDdkKbcHandle hKbc );

/**
 * Sets the repeat time period at which rows must be scanned for key
 * events.
 *  
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 * @param RepeatTimeMs Repeat time period in milliseconds.
 */
 void NvDdkKbcSetRepeatTime( 
    NvDdkKbcHandle hKbc,
    NvU32 RepeatTimeMs );

/**
 * Gets the key events. After calling this function, the caller must sleep 
 * for the amount of time returned by this function before calling it again.
 * If the return value is 0, then the client must wait on sema before
 * calling this function again.
 *
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 * @param pKeyCount The returned key events count.
 * @param pKeyCodes The returned key codes.
 * @param pKeyEvents The returned key events( press/release).
 *
 * @return Wait time in milliseconds.
 */
 NvU32 NvDdkKbcGetKeyEvents( 
    NvDdkKbcHandle hKbc,
    NvU32 * pKeyCount,
    NvU32 * pKeyCodes,
    NvDdkKbcKeyEvent * pKeyEvents );

/**
 * Part of static power management, the client must call this API to put
 * the KBC controller into suspend state. This API is a mechanism for the
 * client to augment OS power management policy. The h/w context of the KBC
 * controller is saved, clock is disabled, and power is also disabled
 * to the controller.
 *
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 * @retval NvSuccess If successful, or the appropriate error code.
 */ 
 NvError NvDdkKbcSuspend( 
    NvDdkKbcHandle hKbc );

/**
 * Part of static power management, the client must call this API to
 * wake up the KBC controller from a suspended state. This API is
 * a mechanism for the client to augment OS power management policy.
 * The h/w context of the KBC controller is restored, clock is enabled,
 * and power is also enabled to the controller.
 *
 * @param hKbc A KBC handle that is allocated by NvDdkKbcOpen().
 * @retval NvSuccess If successful, or the appropriate error code.
 */ 
 NvError NvDdkKbcResume( 
    NvDdkKbcHandle hKbc );

#if defined(__cplusplus)
}
#endif

/** @} */
#endif
