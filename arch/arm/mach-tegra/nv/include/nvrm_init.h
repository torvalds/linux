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

#ifndef INCLUDED_nvrm_init_H
#define INCLUDED_nvrm_init_H


#if defined(__cplusplus)
extern "C"
{
#endif


#include "nvcommon.h"
#include "nverror.h"

/**
 * NvRmDeviceHandle is an opaque handle to an RM device.
 */

typedef struct NvRmDeviceRec *NvRmDeviceHandle;

/**
 * A physical address type sized such that it matches the addressing support of
 * the hardware modules RM typically interfaces with.  May be smaller than an
 * NvOsPhysAddr.
 *
 * XXX We should probably get rid of this and just use NvU32.  It's rather
 * difficult to explain what exactly NvRmPhysAddr is.  Also, what if some units
 * are upgraded to do 64-bit addressing and others remain 32?  Would we really
 * want to increase NvRmPhysAddr to NvU64 across the board?
 *
 * Another option would be to put the following types in nvcommon.h:
 *   typedef NvU32 NvPhysAddr32;
 *   typedef NvU64 NvPhysAddr64;
 * Using these types would then be purely a form of documentation and nothing
 * else.
 *
 * This header file is a somewhat odd place to put this type.  Putting it in
 * memmgr would be even worse, though, because then a lot of header files would
 * all suddenly need to #include nvrm_memmgr.h just to get the NvRmPhysAddr
 * type.  (They already all include this header anyway.)
 */

typedef NvU32 NvRmPhysAddr;

/**
 * Opens the Resource Manager for a given device.
 *
 * Can be called multiple times for a given device.  Subsequent
 * calls will not necessarily return the same handle.  Each call to
 * NvRmOpen() must be paired with a corresponding call to NvRmClose().
 *
 * Assert encountered in debug mode if DeviceId value is invalid.
 *
 * This call is not intended to perform any significant hardware
 * initialization of the device; rather its primary purpose is to
 * initialize RM's internal data structures that are involved in
 * managing the device.
 * 
 * @param pHandle the RM handle is stored here.
 * @param DeviceId implementation-dependent value specifying the device
 *     to be opened.  Currently must be set to zero.
 *
 * @retval NvSuccess Indicates that RM was successfully opened.
 * @retval NvError_InsufficientMemory Indicates that RM was unable to allocate
 *     memory for its internal data structures.
 */

 NvError NvRmOpen( 
    NvRmDeviceHandle * pHandle,
    NvU32 DeviceId );

/**
 * Called by the platform/OS code to initialize the Rm. Usage and
 * implementation of this API is platform specific.
 *  
 * This APIs should not be called by the normal clients of the Rm.
 *    
 * This APIs is guaranteed to succeed on the supported platforms.
 *
 * @param pHandle the RM handle is stored here.
 */

 void NvRmInit( 
    NvRmDeviceHandle * pHandle );

/**
 * Temporary version of NvRmOpen lacking the DeviceId parameter
 */

 NvError NvRmOpenNew( 
    NvRmDeviceHandle * pHandle );

/**
 * Closes the Resource Manager for a given device.
 *
 * Each call to NvRmOpen() must be paired with a corresponding call 
 * to NvRmClose().
 *
 * @param hDevice The RM handle.  If hDevice is NULL, this API has no effect.
 */

 void NvRmClose( 
    NvRmDeviceHandle hDevice );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
