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

#ifndef INCLUDED_nvrm_keylist_H
#define INCLUDED_nvrm_keylist_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

/** @file
 * @brief <b>NVIDIA Driver Development Kit:
 *     Resource Manager Key-List APIs</b>
 *
 * @b Description: This API, defines a simple means to set/get the state 
 * of ODM-Defined Keys.
 */

#include "nvos.h"
#include "nvodm_keylist_reserved.h"

/**
 * Searches the List of Keys present and returns
 * the Value of the appropriate Key.
 * 
 * @param hRm Handle to the RM Device.
 * @param KeyID ID of the key whose value is required.
 * 
 * @retval returns the value of the corresponding key. If the Key is not 
 * present in the list, it returns 0.
 */



 NvU32 NvRmGetKeyValue( 
    NvRmDeviceHandle hRm,
    NvU32 KeyID );

/**
 * Searches the List of Keys Present and sets the value of the Key to the value 
 * given. If the Key is not present, it adds the key to the list and sets the 
 * value.
 * 
 * @param hRM Handle to the RM Device.
 * @param KeyID ID of the key whose value is to be set.
 * @param Value Value to be set for the corresponding key.
 * 
 * @retval NvSuccess Value has been successfully set.
 * @retval NvError_InsufficientMemory Operation has failed while adding the 
 * key to the existing list.
 */

 NvError NvRmSetKeyValuePair( 
    NvRmDeviceHandle hRm,
    NvU32 KeyID,
    NvU32 Value );

#if defined(__cplusplus)
}
#endif

#endif
