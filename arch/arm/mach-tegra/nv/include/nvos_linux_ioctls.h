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

#include <linux/ioctl.h>
#include "nvos.h"
#include "nvcommon.h"

#ifndef NVOS_LINUX_IOCTLS_H
#define NVOS_LINUX_IOCTLS_H

typedef struct
{
    NvU32 IoctlCode;
    NvU32 InBufferSize;
    NvU32 InOutBufferSize;
    NvU32 OutBufferSize;
    void *pBuffer;
} NV_ALIGN(4) NvOsIoctlParams;

typedef struct
{
    NvOsSemaphoreHandle sem;
    NvU32 value;
    NvError error;
} NV_ALIGN(4) NvOsSemaphoreIoctlParams;

typedef struct
{
    NvOsSemaphoreHandle hOrig;
    NvOsSemaphoreHandle hNew;
    NvError             Error;
} NV_ALIGN(4) NvOsSemaphoreUnmarshalParams;

typedef struct
{
    NvOsSemaphoreHandle hOrig;
    NvOsSemaphoreHandle hNew;
    NvError             Error;
} NV_ALIGN(4) NvOsSemaphoreCloneParams;

typedef struct
{
    NvU32 nIrqs;
    const NvU32 *Irqs;
    NvOsSemaphoreHandle *SemaphoreList;
    NvError errCode;
    NvUPtr  kernelHandle;
} NV_ALIGN(4) NvOsInterruptRegisterParams;

typedef struct
{
    NvUPtr  handle;
    NvU32   arg;
    NvError errCode;
} NV_ALIGN(4) NvOsInterruptOpParams;

typedef struct
{
    NvUPtr handle;
    NvU32 mask;
} NV_ALIGN(4) NvOsInterruptMaskParams;

typedef struct
{
    NvU32 size;
    char *text;
} NV_ALIGN(4) NvOsDebugStringParams;

typedef struct
{
    NvOsPhysAddr base;
    NvU32 size;
} NV_ALIGN(4) NvOsMemRangeParams;

#define NV_IOCTL_SEMAPHORE_CREATE   _IOWR('N', 0x20, NvOsSemaphoreIoctlParams)
#define NV_IOCTL_SEMAPHORE_DESTROY  _IOW('N', 0x21, NvOsSemaphoreHandle)
#define NV_IOCTL_SEMAPHORE_CLONE \
    _IOWR('N', 0x22, NvOsSemaphoreCloneParams)
#define NV_IOCTL_SEMAPHORE_UNMARSHAL \
    _IOWR('N', 0x23, NvOsSemaphoreUnmarshalParams)
#define NV_IOCTL_SEMAPHORE_SIGNAL   _IOW('N', 0x24, NvOsSemaphoreHandle)
#define NV_IOCTL_SEMAPHORE_WAIT     _IOW('N', 0x25, NvOsSemaphoreHandle)
#define NV_IOCTL_SEMAPHORE_WAIT_TIMEOUT \
    _IOW('N', 0x26, NvOsSemaphoreIoctlParams)
#define NV_IOCTL_INTERRUPT_REGISTER \
    _IOWR('N', 0x27, NvOsInterruptRegisterParams)
#define NV_IOCTL_INTERRUPT_UNREGISTER   _IOWR('N', 0x28, NvOsInterruptOpParams)
#define NV_IOCTL_INTERRUPT_ENABLE       _IOWR('N', 0x29, NvOsInterruptOpParams)
#define NV_IOCTL_INTERRUPT_DONE         _IOWR('N', 0x2A, NvOsInterruptOpParams)
#define NV_IOCTL_INTERRUPT_MASK     _IOWR('N', 0x2B, NvOsInterruptOpParams)
#define NV_IOCTL_GLOBAL_LOCK        _IO('N', 0x2C)
#define NV_IOCTL_GLOBAL_UNLOCK      _IO('N', 0x2D)
#define NV_IOCTL_DEBUG_STRING       _IOW('N', 0x2E, NvOsDebugStringParams)
#define NV_IOCTL_MEMORY_RANGE       _IOW('N', 0x2F, NvOsMemRangeParams)

#endif
