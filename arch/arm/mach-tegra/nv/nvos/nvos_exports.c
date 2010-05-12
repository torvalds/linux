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

#include "nvos.h"
#include "nvutil.h"
#include "nvassert.h"
#if NVOS_IS_LINUX_KERNEL
#include <linux/module.h>
EXPORT_SYMBOL(NvOsBreakPoint);
EXPORT_SYMBOL(NvOsFprintf);
EXPORT_SYMBOL(NvOsSnprintf);
EXPORT_SYMBOL(NvOsVfprintf);
EXPORT_SYMBOL(NvOsVsnprintf);
EXPORT_SYMBOL(NvOsDebugPrintf);
EXPORT_SYMBOL(NvOsDebugVprintf);
EXPORT_SYMBOL(NvOsDebugNprintf);
EXPORT_SYMBOL(NvOsStrncpy);
EXPORT_SYMBOL(NvOsStrlen);
EXPORT_SYMBOL(NvOsStrcmp);
EXPORT_SYMBOL(NvOsStrncmp);
EXPORT_SYMBOL(NvOsStrGetSystemCodePage);
EXPORT_SYMBOL(NvOsMemcpy);
EXPORT_SYMBOL(NvOsMemcmp);
EXPORT_SYMBOL(NvOsMemset);
EXPORT_SYMBOL(NvOsMemmove);
EXPORT_SYMBOL(NvOsCopyIn);
EXPORT_SYMBOL(NvOsCopyOut);
EXPORT_SYMBOL(NvOsFopen);
EXPORT_SYMBOL(NvOsFclose);
EXPORT_SYMBOL(NvOsFwrite);
EXPORT_SYMBOL(NvOsFread);
EXPORT_SYMBOL(NvOsFreadTimeout);
EXPORT_SYMBOL(NvOsFgetc);
EXPORT_SYMBOL(NvOsFseek);
EXPORT_SYMBOL(NvOsFtell);
EXPORT_SYMBOL(NvOsStat);
EXPORT_SYMBOL(NvOsFstat);
EXPORT_SYMBOL(NvOsFflush);
EXPORT_SYMBOL(NvOsFsync);
EXPORT_SYMBOL(NvOsIoctl);
EXPORT_SYMBOL(NvOsOpendir);
EXPORT_SYMBOL(NvOsReaddir);
EXPORT_SYMBOL(NvOsClosedir);
EXPORT_SYMBOL(NvOsSetFileHooks);
EXPORT_SYMBOL(NvOsGetConfigU32);
EXPORT_SYMBOL(NvOsGetConfigString);
EXPORT_SYMBOL(NvOsAlloc);
EXPORT_SYMBOL(NvOsRealloc);
EXPORT_SYMBOL(NvOsFree);
#if NV_DEBUG
EXPORT_SYMBOL(NvOsAllocLeak);
EXPORT_SYMBOL(NvOsReallocLeak);
EXPORT_SYMBOL(NvOsFreeLeak);
#endif
EXPORT_SYMBOL(NvOsExecAlloc);
EXPORT_SYMBOL(NvOsSharedMemAlloc);
EXPORT_SYMBOL(NvOsSharedMemMap);
EXPORT_SYMBOL(NvOsSharedMemUnmap);
EXPORT_SYMBOL(NvOsSharedMemFree);
EXPORT_SYMBOL(NvOsPhysicalMemMap);
EXPORT_SYMBOL(NvOsPhysicalMemMapIntoCaller);
EXPORT_SYMBOL(NvOsPhysicalMemUnmap);
EXPORT_SYMBOL(NvOsPageAlloc);
EXPORT_SYMBOL(NvOsPageFree);
EXPORT_SYMBOL(NvOsPageLock);
EXPORT_SYMBOL(NvOsPageMap);
EXPORT_SYMBOL(NvOsPageMapIntoPtr);
EXPORT_SYMBOL(NvOsPageUnmap);
EXPORT_SYMBOL(NvOsPageAddress);
EXPORT_SYMBOL(NvOsLibraryLoad);
EXPORT_SYMBOL(NvOsLibraryGetSymbol);
EXPORT_SYMBOL(NvOsLibraryUnload);
EXPORT_SYMBOL(NvOsSleepMS);
EXPORT_SYMBOL(NvOsWaitUS);
EXPORT_SYMBOL(NvOsMutexCreate);
EXPORT_SYMBOL(NvOsTraceLogPrintf);
EXPORT_SYMBOL(NvOsTraceLogStart);
EXPORT_SYMBOL(NvOsTraceLogEnd);
EXPORT_SYMBOL(NvOsMutexLock);
EXPORT_SYMBOL(NvOsMutexUnlock);
EXPORT_SYMBOL(NvOsMutexDestroy);
EXPORT_SYMBOL(NvOsIntrMutexCreate);
EXPORT_SYMBOL(NvOsIntrMutexLock);
EXPORT_SYMBOL(NvOsIntrMutexUnlock);
EXPORT_SYMBOL(NvOsIntrMutexDestroy);
EXPORT_SYMBOL(NvOsSpinMutexCreate);
EXPORT_SYMBOL(NvOsSpinMutexLock);
EXPORT_SYMBOL(NvOsSpinMutexUnlock);
EXPORT_SYMBOL(NvOsSpinMutexDestroy);
EXPORT_SYMBOL(NvOsSemaphoreCreate);
EXPORT_SYMBOL(NvOsSemaphoreClone);
EXPORT_SYMBOL(NvOsSemaphoreUnmarshal);
EXPORT_SYMBOL(NvOsSemaphoreWait);
EXPORT_SYMBOL(NvOsSemaphoreWaitTimeout);
EXPORT_SYMBOL(NvOsSemaphoreSignal);
EXPORT_SYMBOL(NvOsSemaphoreDestroy);
EXPORT_SYMBOL(NvOsThreadCreate);
EXPORT_SYMBOL(NvOsInterruptPriorityThreadCreate);
EXPORT_SYMBOL(NvOsThreadSetLowPriority);
EXPORT_SYMBOL(NvOsThreadJoin);
EXPORT_SYMBOL(NvOsThreadYield);
EXPORT_SYMBOL(NvOsGetTimeMS);
EXPORT_SYMBOL(NvOsGetTimeUS);
EXPORT_SYMBOL(NvOsInstrCacheInvalidate);
EXPORT_SYMBOL(NvOsInstrCacheInvalidateRange);
EXPORT_SYMBOL(NvOsFlushWriteCombineBuffer);
EXPORT_SYMBOL(NvOsInterruptRegister);
EXPORT_SYMBOL(NvOsInterruptUnregister);
EXPORT_SYMBOL(NvOsInterruptEnable);
EXPORT_SYMBOL(NvOsInterruptDone);
EXPORT_SYMBOL(NvOsInterruptMask);
EXPORT_SYMBOL(NvOsProfileApertureSizes);
EXPORT_SYMBOL(NvOsProfileStart);
EXPORT_SYMBOL(NvOsProfileStop);
EXPORT_SYMBOL(NvOsProfileWrite);
EXPORT_SYMBOL(NvOsBootArgSet);
EXPORT_SYMBOL(NvOsBootArgGet);
EXPORT_SYMBOL(NvOsGetOsInformation);
EXPORT_SYMBOL(NvOsThreadMode);
EXPORT_SYMBOL(NvOsAtomicCompareExchange32);
EXPORT_SYMBOL(NvOsAtomicExchange32);
EXPORT_SYMBOL(NvOsAtomicExchangeAdd32);
#if (NVOS_TRACE || NV_DEBUG)
EXPORT_SYMBOL(NvOsSetResourceAllocFileLine);
#endif
EXPORT_SYMBOL(NvOsTlsAlloc);
EXPORT_SYMBOL(NvOsTlsFree);
EXPORT_SYMBOL(NvOsTlsGet);
EXPORT_SYMBOL(NvOsTlsSet);
EXPORT_SYMBOL(NvULowestBitSet);
EXPORT_SYMBOL(NvOsGetProcessInfo);
#endif /* NVOS_IS_LINUX_KERNEL */
