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

#define NV_IDL_IS_DISPATCH

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvreftrack.h"
#include "nvidlcmd.h"
#include "nvrm_dma.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmDmaIsDmaTransferCompletes_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDmaHandle hDma;
    NvBool IsFirstHalfBuffer;
} NV_ALIGN(4) NvRmDmaIsDmaTransferCompletes_in;

typedef struct NvRmDmaIsDmaTransferCompletes_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaIsDmaTransferCompletes_inout;

typedef struct NvRmDmaIsDmaTransferCompletes_out_t
{
    NvBool ret_;
} NV_ALIGN(4) NvRmDmaIsDmaTransferCompletes_out;

typedef struct NvRmDmaIsDmaTransferCompletes_params_t
{
    NvRmDmaIsDmaTransferCompletes_in in;
    NvRmDmaIsDmaTransferCompletes_inout inout;
    NvRmDmaIsDmaTransferCompletes_out out;
} NvRmDmaIsDmaTransferCompletes_params;

typedef struct NvRmDmaGetTransferredCount_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDmaHandle hDma;
    NvBool IsTransferStop;
} NV_ALIGN(4) NvRmDmaGetTransferredCount_in;

typedef struct NvRmDmaGetTransferredCount_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaGetTransferredCount_inout;

typedef struct NvRmDmaGetTransferredCount_out_t
{
    NvError ret_;
    NvU32 pTransferCount;
} NV_ALIGN(4) NvRmDmaGetTransferredCount_out;

typedef struct NvRmDmaGetTransferredCount_params_t
{
    NvRmDmaGetTransferredCount_in in;
    NvRmDmaGetTransferredCount_inout inout;
    NvRmDmaGetTransferredCount_out out;
} NvRmDmaGetTransferredCount_params;

typedef struct NvRmDmaAbort_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDmaHandle hDma;
} NV_ALIGN(4) NvRmDmaAbort_in;

typedef struct NvRmDmaAbort_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaAbort_inout;

typedef struct NvRmDmaAbort_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaAbort_out;

typedef struct NvRmDmaAbort_params_t
{
    NvRmDmaAbort_in in;
    NvRmDmaAbort_inout inout;
    NvRmDmaAbort_out out;
} NvRmDmaAbort_params;

typedef struct NvRmDmaStartDmaTransfer_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDmaHandle hDma;
    NvRmDmaClientBuffer pClientBuffer;
    NvRmDmaDirection DmaDirection;
    NvU32 WaitTimeoutInMilliSecond;
    NvOsSemaphoreHandle AsynchSemaphoreId;
} NV_ALIGN(4) NvRmDmaStartDmaTransfer_in;

typedef struct NvRmDmaStartDmaTransfer_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaStartDmaTransfer_inout;

typedef struct NvRmDmaStartDmaTransfer_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDmaStartDmaTransfer_out;

typedef struct NvRmDmaStartDmaTransfer_params_t
{
    NvRmDmaStartDmaTransfer_in in;
    NvRmDmaStartDmaTransfer_inout inout;
    NvRmDmaStartDmaTransfer_out out;
} NvRmDmaStartDmaTransfer_params;

typedef struct NvRmDmaFree_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDmaHandle hDma;
} NV_ALIGN(4) NvRmDmaFree_in;

typedef struct NvRmDmaFree_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaFree_inout;

typedef struct NvRmDmaFree_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaFree_out;

typedef struct NvRmDmaFree_params_t
{
    NvRmDmaFree_in in;
    NvRmDmaFree_inout inout;
    NvRmDmaFree_out out;
} NvRmDmaFree_params;

typedef struct NvRmDmaAllocate_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvBool Enable32bitSwap;
    NvRmDmaPriority Priority;
    NvRmDmaModuleID DmaRequestorModuleId;
    NvU32 DmaRequestorInstanceId;
} NV_ALIGN(4) NvRmDmaAllocate_in;

typedef struct NvRmDmaAllocate_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaAllocate_inout;

typedef struct NvRmDmaAllocate_out_t
{
    NvError ret_;
    NvRmDmaHandle phDma;
} NV_ALIGN(4) NvRmDmaAllocate_out;

typedef struct NvRmDmaAllocate_params_t
{
    NvRmDmaAllocate_in in;
    NvRmDmaAllocate_inout inout;
    NvRmDmaAllocate_out out;
} NvRmDmaAllocate_params;

typedef struct NvRmDmaGetCapabilities_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmDmaCapabilities pRmDmaCaps;
} NV_ALIGN(4) NvRmDmaGetCapabilities_in;

typedef struct NvRmDmaGetCapabilities_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDmaGetCapabilities_inout;

typedef struct NvRmDmaGetCapabilities_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDmaGetCapabilities_out;

typedef struct NvRmDmaGetCapabilities_params_t
{
    NvRmDmaGetCapabilities_in in;
    NvRmDmaGetCapabilities_inout inout;
    NvRmDmaGetCapabilities_out out;
} NvRmDmaGetCapabilities_params;

static NvError NvRmDmaIsDmaTransferCompletes_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaIsDmaTransferCompletes_in *p_in;
    NvRmDmaIsDmaTransferCompletes_out *p_out;

    p_in = (NvRmDmaIsDmaTransferCompletes_in *)InBuffer;
    p_out = (NvRmDmaIsDmaTransferCompletes_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDmaIsDmaTransferCompletes_params, out) - OFFSET(NvRmDmaIsDmaTransferCompletes_params, inout));


    p_out->ret_ = NvRmDmaIsDmaTransferCompletes( p_in->hDma, p_in->IsFirstHalfBuffer );

    return err_;
}

static NvError NvRmDmaGetTransferredCount_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaGetTransferredCount_in *p_in;
    NvRmDmaGetTransferredCount_out *p_out;

    p_in = (NvRmDmaGetTransferredCount_in *)InBuffer;
    p_out = (NvRmDmaGetTransferredCount_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDmaGetTransferredCount_params, out) - OFFSET(NvRmDmaGetTransferredCount_params, inout));


    p_out->ret_ = NvRmDmaGetTransferredCount( p_in->hDma, &p_out->pTransferCount, p_in->IsTransferStop );

    return err_;
}

static NvError NvRmDmaAbort_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaAbort_in *p_in;

    p_in = (NvRmDmaAbort_in *)InBuffer;


    NvRmDmaAbort( p_in->hDma );

    return err_;
}

static NvError NvRmDmaStartDmaTransfer_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaStartDmaTransfer_in *p_in;
    NvRmDmaStartDmaTransfer_out *p_out;
    NvOsSemaphoreHandle AsynchSemaphoreId = NULL;

    p_in = (NvRmDmaStartDmaTransfer_in *)InBuffer;
    p_out = (NvRmDmaStartDmaTransfer_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDmaStartDmaTransfer_params, out) - OFFSET(NvRmDmaStartDmaTransfer_params, inout));

    if( p_in->AsynchSemaphoreId )
    {
        err_ = NvOsSemaphoreUnmarshal( p_in->AsynchSemaphoreId, &AsynchSemaphoreId );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDmaStartDmaTransfer( p_in->hDma, &p_in->pClientBuffer, p_in->DmaDirection, p_in->WaitTimeoutInMilliSecond, AsynchSemaphoreId );

clean:
    NvOsSemaphoreDestroy( AsynchSemaphoreId );
    return err_;
}

static NvError NvRmDmaFree_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaFree_in *p_in;

    p_in = (NvRmDmaFree_in *)InBuffer;


    NvRmDmaFree( p_in->hDma );

    return err_;
}

static NvError NvRmDmaAllocate_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaAllocate_in *p_in;
    NvRmDmaAllocate_out *p_out;

    p_in = (NvRmDmaAllocate_in *)InBuffer;
    p_out = (NvRmDmaAllocate_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDmaAllocate_params, out) - OFFSET(NvRmDmaAllocate_params, inout));


    p_out->ret_ = NvRmDmaAllocate( p_in->hRmDevice, &p_out->phDma, p_in->Enable32bitSwap, p_in->Priority, p_in->DmaRequestorModuleId, p_in->DmaRequestorInstanceId );

    return err_;
}

static NvError NvRmDmaGetCapabilities_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDmaGetCapabilities_in *p_in;
    NvRmDmaGetCapabilities_out *p_out;

    p_in = (NvRmDmaGetCapabilities_in *)InBuffer;
    p_out = (NvRmDmaGetCapabilities_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDmaGetCapabilities_params, out) - OFFSET(NvRmDmaGetCapabilities_params, inout));


    p_out->ret_ = NvRmDmaGetCapabilities( p_in->hDevice, &p_in->pRmDmaCaps );

    return err_;
}

NvError nvrm_dma_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_dma_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 6:
        err_ = NvRmDmaIsDmaTransferCompletes_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmDmaGetTransferredCount_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmDmaAbort_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmDmaStartDmaTransfer_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmDmaFree_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmDmaAllocate_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmDmaGetCapabilities_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
