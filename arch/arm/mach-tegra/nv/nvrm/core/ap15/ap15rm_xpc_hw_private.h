/*
 * Copyright (c) 2010 NVIDIA Corporation.
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
 * @brief <b>nVIDIA Driver Development Kit: 
 *           Priate Hw access function for XPC driver </b>
 *
 * @b Description: Defines the private interface functions for the xpc 
 * 
 */

#ifndef INCLUDED_RM_XPC_HW_PRIVATE_H
#define INCLUDED_RM_XPC_HW_PRIVATE_H


#include "nvcommon.h"
#include "nvrm_init.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Combines the cpu avp hw mail baox system information.
typedef struct CpuAvpHwMailBoxRegRec
{
    // Hw mail box register virtual base address.
    NvU32 *pHwMailBoxRegBaseVirtAddr;
    
    // Bank size of the hw regsiter.
    NvU32 BankSize;

    // Tells whether this is on cpu or on Avp
    NvBool IsCpu;

    // Mail box data which was read last time.
    NvU32 MailBoxData;
} CpuAvpHwMailBoxReg;

void NvRmPrivXpcHwResetOutbox(CpuAvpHwMailBoxReg *pHwMailBoxReg);

/**
 * Send message to the target.
 */
void
NvRmPrivXpcHwSendMessageToTarget(
    CpuAvpHwMailBoxReg *pHwMailBoxReg,
    NvRmPhysAddr MessageAddress);

/**
 * Receive message from the target.
 */
void
NvRmPrivXpcHwReceiveMessageFromTarget(
    CpuAvpHwMailBoxReg *pHwMailBoxReg,
    NvRmPhysAddr *pMessageAddress);


#if defined(__cplusplus)
 }
#endif

#endif  // INCLUDED_RM_XPC_HW_PRIVATE_H
