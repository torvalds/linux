/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_avp_swi_registry.h
 *
 *
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef INCLUDED_NVRM_AVP_SWI_REGISTRY_H
#define INCLUDED_NVRM_AVP_SWI_REGISTRY_H

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_power.h"

#if defined(__cplusplus)
extern "C"
{
#endif

enum {MAX_CLIENTS = 5};
enum {MAX_SWI_PER_CLIENT = 32};
enum {CLIENT_SWI_NUM_START = 0xD0};

typedef NvError (*AvpClientSwiHandler) (int *pRegs);

typedef enum
{
    AvpSwiClientType_None = 0,
    AvpSwiClientType_NvMM,
    AvpSwiClientType_Force32 = 0x7fffffff
} AvpSwiClientType;

typedef enum {
    AvpSwiClientSwiNum_NvMM = 0xD0,
    AvpSwiClientSwiNum_Force32 = 0x7fffffff
} AvpSwiClientSwiNum;

typedef struct AvpSwiClientRec
{
    AvpClientSwiHandler ClientSwiHandler[MAX_SWI_PER_CLIENT];
    AvpSwiClientType ClientId;
    AvpSwiClientSwiNum SwiNum;
} AvpSwiClient;

typedef struct AvpSwiClientRegistryRec
{
    AvpSwiClient SwiClient[MAX_CLIENTS];
    NvU32 RefCount;
    NvOsMutexHandle Mutex;
}AvpSwiClientRegistry;

NvError
NvRmAvpClientSwiHandlerRegister(
    AvpSwiClientType ClientId,
    AvpClientSwiHandler pClinetSwiFunc,
    NvU32 *pClientIndex);

NvError
NvRmAvpClientSwiHandlerUnRegister(
    AvpSwiClientType ClientId,
    NvU32 ClientIndex);

NvError
NvRmAvpHandleClientSwi(
    NvU32 SwiNum,
    NvU32 ClientIndex,
    int *pRegs);

typedef struct{
    NvRmDfsClockId clockId;
    NvU32 clientId;
    NvU32 boostDurationMS;
    NvRmFreqKHz boostKHz;
}NvRm_PowerBusyHint;

/** NvRmRegisterLibraryCall - Register a library call with the AVP RM
 *
 *  @param id The user id to associate with the function
 *  @param pEntry The function to be registered
 *  @param pOwnerKey A special unique key to use when unregistering.
 *
 *  @returns InsufficientMemory or AlreadyAllocated on failure.
 */
NvError
NvRmRegisterLibraryCall(NvU32 Id, void *pEntry, NvU32 *pOwnerKey);

/** NvRmUnregisterLibraryCall - Unregister a library call
 *
 *  @param id The user id associated with the function
 *  @param pOwnerKey A special unique key. Used to ensure that the correct owner
 *  unregisters a function.
 *
 */
void
NvRmUnregisterLibraryCall(NvU32 Id, NvU32 OwnerKey);

/** NvRmGetLibraryCall - Obtain a registered function from the AVP RM
 *
 *  @param id The user id associated with the desired library function
 *
 *  @returns NULL on failure. The function pointer on success
 */
void *NvRmGetLibraryCall(NvU32 Id);

/** NvRmRemoteDebugPrintf - Routes client prints to the CPU.
 *
 *  NOTE: This does *not* route kernel prints. ONLY AVP client prints will
 *  be routed.
 *  @param string The debug string to print to console
 *
 */
void *NvRmRemoteDebugPrintf(const char *string);

/** NvOsAVPThreadCreate - Creates threads on the AVP with an optional stackPtr argument.
 *
 *  AVP clients can use this function to allocate thread stacks wherever they desire (like in
 *  IRAM, for instance). It is the clients responsibility to allocate this pointer and free it.
 *  NOTE: The client must free this pointer only after the thread has been joined.
 *
 *  @param function The thread entry point
 *  @param args The thread args
 *  @param thread The result thread id structure (out param)
 *  @param stackPtr The optional pointer to a user allocated stack (Can be NULL)
 *  @param stackSize The size of the associated stackPtr.
 *
 */
NvError NvOsAVPThreadCreate(NvOsThreadFunction function,
                            void *args,
                            NvOsThreadHandle *thread,
                            void *stackPtr,
                            NvU32 stackSize);

/** NvOsAVPSetIdle - This function is used to force the AVP kernel to save its state
 *
 *  When the PMC_SCRATCH22 register has a non-zero value, the AVP has finished saving all its state.
 *  @param iramSourceAddress The address at which the IRAM aperture begins
 *  @param iramBufferAddress The address of the buffer into which the AVP will save all IRAM state.
 *  @param iramSize The size of the iram aperture.
 *
 */
void NvOsAVPSetIdle(NvU32 iramSourceAddress,
                    NvU32 iramBufferAddress,
                    NvU32 iramSize);

/** NvRmPowerBusyMultiHint - Provide hints to multiple modules. Saves on messaging overhead.
 *
 *  @param multiHint The array of hints
 *  @param numHints The number of hints
 */
void NvRmPowerBusyMultiHint(NvRm_PowerBusyHint *multiHint, NvU32 numHints);

#if defined(__cplusplus)
}
#endif

#endif
