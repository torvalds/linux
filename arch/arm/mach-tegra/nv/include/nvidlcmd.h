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

#ifndef INCLUDED_NVIDLCMD_H
#define INCLUDED_NVIDLCMD_H

#include "nvos.h"
#include "nvreftrack.h"

// name of the master FIFO socket on systems which use the master FIFO
#define NVRM_DAEMON_SOCKNAME "/dev/nvrm_daemon"

#define NV_DAEMON_MODULES(F) \
    F(NvRmGraphics) \
    F(NvMM) \
    F(NvDDKAudio) \
    F(NvDispMgr)

#define NV_KERNEL_MODULES(F) \
    F(NvRm) \
    F(NvECPackage) \
    F(NvStorManager) \
    F(NvVib)

// These codes are sent to the daemon to initiate commands from each module.
typedef enum NvRmDaemonCodeEnum
{
    NvRmDaemonCode_FifoCreate   =   0x281e0001,
    NvRmDaemonCode_FifoDelete,

#define F(X) NvRmDaemonCode_##X,
    NV_DAEMON_MODULES(F)
#undef F
    NvRmDaemonCode_Garbage      =   0xdeadbeef,

    NvRmDaemonCode_Force32      =   0x7FFFFFFF
} NvRmDaemonCode;

/* Defines a pair of objects for transferring data to and from the daemon.
 * FifoIn is used to read data from the daemon; FifoOut is used to write data
 * to the daemon
 */
typedef struct NvIdlFifoPairRec
{
    void *FifoIn;
    void *FifoOut;
} NvIdlFifoPair;


/* These functions are called by the IDL-generated code:
 *
 * *_NvIdlGetIoctlCode() - get code to use to identify module
 * *_NvIdlGetFifos()     - get a fifo pair for communication
 * *_NvIdlReleaseFifos() - get a fifo pair for communication
 * *_NvIdlGetIoctlFile() - get the file to use for ioctl
 */

#define NV_IDL_DECLS_STUB(pfx) \
        NvU32          pfx##_NvIdlGetIoctlCode(void); \
        NvOsFileHandle pfx##_NvIdlGetIoctlFile(void);
    
#define NV_IDL_DECLS_DISPATCH_KERNEL(pfx) \
        NvError pfx##_Dispatch( \
                    void *InBuffer, \
                    NvU32 InSize, \
                    void *OutBuffer, \
                    NvU32 OutSize, \
                    NvDispatchCtx* Ctx);

#if NVOS_IS_LINUX
#define NV_IDL_DECLS_DISPATCH_DAEMON(pfx) \
        NvError pfx##_Dispatch( \
            void* hFifoIn, \
            void* hFifoOut, \
            NvDispatchCtx* Ctx);
#else
#define NV_IDL_DECLS_DISPATCH_DAEMON(p) \
        NV_IDL_DECLS_DISPATCH_KERNEL(p)
#endif

#define F(X) NV_IDL_DECLS_STUB(X)
NV_DAEMON_MODULES(F)
NV_KERNEL_MODULES(F)
#undef F

#define F(X) NV_IDL_DECLS_DISPATCH_DAEMON(X)
NV_DAEMON_MODULES(F)
#undef F

#define F(X) NV_IDL_DECLS_DISPATCH_KERNEL(X)
NV_KERNEL_MODULES(F)
#undef F

/* utility functions called by stubs & dispatchers for transferring data
 * over FIFO objects. semantics are identical to NvOsFread / NvOsFwrite */
NvError NvIdlHelperFifoRead(void *fifo, void *ptr, size_t len, size_t *read);
NvError NvIdlHelperFifoWrite(void *fifo, const void *ptr, size_t len);

/* utility functions called by stub helpers to allocate and free FIFOs */
NvError NvIdlHelperGetFifoPair(NvIdlFifoPair **pFifo);
void    NvIdlHelperReleaseFifoPair(NvIdlFifoPair *pFifo);

#endif
