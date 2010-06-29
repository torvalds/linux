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
 *           Cross Processor Communication driver </b>
 *
 * @b Description: Implements the cross processor communication Hw Access APIs
 * 
 */
 
#include "nvcommon.h"
#include "nvassert.h"
#include "nvrm_drf.h"
#include "nvrm_hardware_access.h"
#include "ap15rm_xpc_hw_private.h"
#include "ap15/arres_sema.h"

enum {MESSAGE_BOX_MESSAGE_LENGTH_BITS = 28};
#define RESSEMA_REG_READ32(pResSemaHwRegVirtBaseAdd, reg) \
    NV_READ32((pResSemaHwRegVirtBaseAdd) + (RES_SEMA_##reg##_0)/4)

#define RESSEMA_REG_WRITE32(pResSemaHwRegVirtBaseAdd, reg, val) \
   do { \
         NV_WRITE32(((pResSemaHwRegVirtBaseAdd) + ((RES_SEMA_##reg##_0)/4)), (val)); \
   } while(0)

void NvRmPrivXpcHwResetOutbox(CpuAvpHwMailBoxReg *pHwMailBoxReg)
{
    NvU32 OutboxMessage;
    NvU32 OutboxVal;

    OutboxMessage = 0;

    // Write Outbox in the message box
    // Enable the Valid tag
    // Enable interrupt
#if NV_IS_AVP
    OutboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_INBOX);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_STAT, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_DATA, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_CMD, 0, OutboxVal);
    OutboxVal |= OutboxMessage;
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IE_IBE, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, TAG, 0, OutboxVal);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_INBOX, OutboxVal);
#else        
    OutboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_OUTBOX);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_CMD, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_STAT, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_DATA, 0, OutboxVal);
    OutboxVal |= OutboxMessage;
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, IE_OBE, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, TAG, 0, OutboxVal);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_OUTBOX, OutboxVal);
#endif        
}


/**
 * Send message to the target.
 */
void
NvRmPrivXpcHwSendMessageToTarget(
    CpuAvpHwMailBoxReg *pHwMailBoxReg,
    NvRmPhysAddr MessageAddress)
{
    NvU32 OutboxMessage;
    NvU32 OutboxVal = 0;

    OutboxMessage = ((NvU32)(MessageAddress)) >> (32 - MESSAGE_BOX_MESSAGE_LENGTH_BITS);
    
    // Write Outbox in the message box
    // Enable the Valid tag
    // Enable interrupt
#if NV_IS_AVP
    // !!! not sure why this would need to be read/modify/write
//    OutboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_INBOX);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_STAT, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_DATA, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_INBOX, IN_BOX_CMD, 0, OutboxVal);
    OutboxVal |= OutboxMessage;
    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_INBOX, IE_IBF, FULL, OutboxVal);
//    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_INBOX, IE_IBE, EMPTY, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_INBOX, TAG, VALID, OutboxVal);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_INBOX, OutboxVal);
#else        
    // !!! not sure why this would need to be read/modify/write
//    OutboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_OUTBOX);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_CMD, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_STAT, 0, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_NUM(RES_SEMA, SHRD_OUTBOX, OUT_BOX_DATA, 0, OutboxVal);
    OutboxVal |= OutboxMessage;
    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_OUTBOX, IE_OBF, FULL, OutboxVal);
//    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_OUTBOX, IE_OBE, EMPTY, OutboxVal);
    OutboxVal = NV_FLD_SET_DRF_DEF(RES_SEMA, SHRD_OUTBOX, TAG, VALID, OutboxVal);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_OUTBOX, OutboxVal);
#endif        
}



/**
 * Receive message from the target.
 */
void
NvRmPrivXpcHwReceiveMessageFromTarget(
    CpuAvpHwMailBoxReg *pHwMailBoxReg,
    NvRmPhysAddr *pMessageAddress)
{
    NvU32 InboxMessage = 0;
    NvU32 InboxVal;

    // Read the inbox. Lower 28 bit contains the message.
#if NV_IS_AVP        
    InboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_OUTBOX);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_OUTBOX, 0);
#else        
    InboxVal = RESSEMA_REG_READ32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr,SHRD_INBOX);
    RESSEMA_REG_WRITE32(pHwMailBoxReg->pHwMailBoxRegBaseVirtAddr, SHRD_INBOX, 0);
#endif        
    if (InboxVal & NV_DRF_DEF(RES_SEMA, SHRD_INBOX, TAG, VALID))
    {
         pHwMailBoxReg->MailBoxData = InboxVal;
    }

    InboxVal = (pHwMailBoxReg->MailBoxData) & (0xFFFFFFFFUL >> (32 - MESSAGE_BOX_MESSAGE_LENGTH_BITS));
    InboxMessage = (InboxVal << (32 - MESSAGE_BOX_MESSAGE_LENGTH_BITS));

    *pMessageAddress = InboxMessage;
}




