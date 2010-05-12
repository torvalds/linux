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

#ifndef INCLUDED_nvrm_avp_shrd_interrupt_H
#define INCLUDED_nvrm_avp_shrd_interrupt_H

#include "nvos.h"
#include "nvrm_init.h"
#include "nvrm_module.h"
#include "nvrm_interrupt.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/* Max number of clients with shared interrupt handler */
enum {MAX_SHRDINT_CLIENTS = 32};

/* Now AP15 support only VDE interrupts 6 */
enum {MAX_SHRDINT_INTERRUPTS = 6};
    /* VDE Sync Token Interrupt */
enum {AP15_SYNC_TOKEN_INTERRUPT_INDEX = 0};
    /* VDE BSE-V Interrupt */
enum {AP15_BSE_V_INTERRUPT_INDEX = 1};
    /* VDE BSE-A Interrupt */
enum {AP15_BSE_A_INTERRUPT_INDEX = 2};
    /* VDE SXE Interrupt */
enum {AP15_SXE_INTERRUPT_INDEX = 3};
    /* VDE UCQ Error Interrupt */
enum {AP15_UCQ_INTERRUPT_INDEX = 4};
    /* VDE Interrupt */
enum {AP15_VDE_INTERRUPT_INDEX = 5};

/* Now AP20 support only VDE interrupts 5 */
enum {AP20_MAX_SHRDINT_INTERRUPTS = 5};
    /* VDE Sync Token Interrupt */
enum {AP20_SYNC_TOKEN_INTERRUPT_INDEX = 0};
    /* VDE BSE-V Interrupt */
enum {AP20_BSE_V_INTERRUPT_INDEX = 1};
    /* VDE SXE Interrupt */
enum {AP20_SXE_INTERRUPT_INDEX = 2};
    /* VDE UCQ Error Interrupt */
enum {AP20_UCQ_INTERRUPT_INDEX = 3};
    /* VDE Interrupt */
enum {AP20_VDE_INTERRUPT_INDEX = 4};

enum
{
    NvRmArbSema_Vde = 0,
    NvRmArbSema_Bsea,
    //This should be last
    NvRmArbSema_Num,
};

/* Shared interrupt private init , init done during RM init on AVP */
NvError NvRmAvpShrdInterruptPrvInit(NvRmDeviceHandle hRmDevice);

/* Shared interrupt private de-init , de-init done during RM close on AVP */
void NvRmAvpShrdInterruptPrvDeinit(NvRmDeviceHandle hRmDevice);

/* Get logical interrupt for a module*/
NvU32 NvRmAvpShrdInterruptGetIrqForLogicalInterrupt(NvRmDeviceHandle hRmDevice,
                                                    NvRmModuleID ModuleID,
                                                    NvU32 Index);
/* Register for shared interrpt */
NvError NvRmAvpShrdInterruptRegister(NvRmDeviceHandle hRmDevice,
                                     NvU32 IrqListSize,
                                     const NvU32 *pIrqList,
                                     const NvOsInterruptHandler *pIrqHandlerList,
                                     void *pContext,
                                     NvOsInterruptHandle *handle,
                                     NvU32 *ClientIndex);
/* Un-register a shared interrpt */
void NvRmAvpShrdInterruptUnregister(NvRmDeviceHandle hRmDevice,
                                    NvOsInterruptHandle handle,
                                    NvU32 ClientIndex);
/* Get exclisive access to a hardware(VDE) block */
NvError NvRmAvpShrdInterruptAquireHwBlock(NvRmModuleID ModuleID, NvU32 ClientId);

/* Release exclisive access to a hardware(VDE) block */
NvError NvRmAvpShrdInterruptReleaseHwBlock(NvRmModuleID ModuleID, NvU32 ClientId);

#if defined(__cplusplus)
}
#endif

#endif //INCLUDED_nvrm_avp_shrd_interrupt_H
