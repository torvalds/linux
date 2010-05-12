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
#include "nvrm_memmgr.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmMemGetStat_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemStat Stat;
} NV_ALIGN(4) NvRmMemGetStat_in;

typedef struct NvRmMemGetStat_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetStat_inout;

typedef struct NvRmMemGetStat_out_t
{
    NvError ret_;
    NvS32 Result;
} NV_ALIGN(4) NvRmMemGetStat_out;

typedef struct NvRmMemGetStat_params_t
{
    NvRmMemGetStat_in in;
    NvRmMemGetStat_inout inout;
    NvRmMemGetStat_out out;
} NvRmMemGetStat_params;

typedef struct NvRmMemHandleFromId_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvU32 id;
} NV_ALIGN(4) NvRmMemHandleFromId_in;

typedef struct NvRmMemHandleFromId_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandleFromId_inout;

typedef struct NvRmMemHandleFromId_out_t
{
    NvError ret_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemHandleFromId_out;

typedef struct NvRmMemHandleFromId_params_t
{
    NvRmMemHandleFromId_in in;
    NvRmMemHandleFromId_inout inout;
    NvRmMemHandleFromId_out out;
} NvRmMemHandleFromId_params;

typedef struct NvRmMemGetId_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemGetId_in;

typedef struct NvRmMemGetId_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetId_inout;

typedef struct NvRmMemGetId_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemGetId_out;

typedef struct NvRmMemGetId_params_t
{
    NvRmMemGetId_in in;
    NvRmMemGetId_inout inout;
    NvRmMemGetId_out out;
} NvRmMemGetId_params;

typedef struct NvRmMemGetHeapType_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemGetHeapType_in;

typedef struct NvRmMemGetHeapType_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetHeapType_inout;

typedef struct NvRmMemGetHeapType_out_t
{
    NvRmHeap ret_;
    NvU32 BasePhysAddr;
} NV_ALIGN(4) NvRmMemGetHeapType_out;

typedef struct NvRmMemGetHeapType_params_t
{
    NvRmMemGetHeapType_in in;
    NvRmMemGetHeapType_inout inout;
    NvRmMemGetHeapType_out out;
} NvRmMemGetHeapType_params;

typedef struct NvRmMemGetCacheLineSize_in_t
{
    NvU32 package_;
    NvU32 function_;
} NV_ALIGN(4) NvRmMemGetCacheLineSize_in;

typedef struct NvRmMemGetCacheLineSize_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetCacheLineSize_inout;

typedef struct NvRmMemGetCacheLineSize_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemGetCacheLineSize_out;

typedef struct NvRmMemGetCacheLineSize_params_t
{
    NvRmMemGetCacheLineSize_in in;
    NvRmMemGetCacheLineSize_inout inout;
    NvRmMemGetCacheLineSize_out out;
} NvRmMemGetCacheLineSize_params;

typedef struct NvRmMemGetAlignment_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemGetAlignment_in;

typedef struct NvRmMemGetAlignment_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetAlignment_inout;

typedef struct NvRmMemGetAlignment_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemGetAlignment_out;

typedef struct NvRmMemGetAlignment_params_t
{
    NvRmMemGetAlignment_in in;
    NvRmMemGetAlignment_inout inout;
    NvRmMemGetAlignment_out out;
} NvRmMemGetAlignment_params;

typedef struct NvRmMemGetSize_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemGetSize_in;

typedef struct NvRmMemGetSize_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetSize_inout;

typedef struct NvRmMemGetSize_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemGetSize_out;

typedef struct NvRmMemGetSize_params_t
{
    NvRmMemGetSize_in in;
    NvRmMemGetSize_inout inout;
    NvRmMemGetSize_out out;
} NvRmMemGetSize_params;

typedef struct NvRmMemMove_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hDstMem;
    NvU32 DstOffset;
    NvRmMemHandle hSrcMem;
    NvU32 SrcOffset;
    NvU32 Size;
} NV_ALIGN(4) NvRmMemMove_in;

typedef struct NvRmMemMove_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemMove_inout;

typedef struct NvRmMemMove_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemMove_out;

typedef struct NvRmMemMove_params_t
{
    NvRmMemMove_in in;
    NvRmMemMove_inout inout;
    NvRmMemMove_out out;
} NvRmMemMove_params;

typedef struct NvRmMemUnpinMult_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle  * hMems;
    NvU32 Count;
} NV_ALIGN(4) NvRmMemUnpinMult_in;

typedef struct NvRmMemUnpinMult_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemUnpinMult_inout;

typedef struct NvRmMemUnpinMult_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemUnpinMult_out;

typedef struct NvRmMemUnpinMult_params_t
{
    NvRmMemUnpinMult_in in;
    NvRmMemUnpinMult_inout inout;
    NvRmMemUnpinMult_out out;
} NvRmMemUnpinMult_params;

typedef struct NvRmMemUnpin_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemUnpin_in;

typedef struct NvRmMemUnpin_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemUnpin_inout;

typedef struct NvRmMemUnpin_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemUnpin_out;

typedef struct NvRmMemUnpin_params_t
{
    NvRmMemUnpin_in in;
    NvRmMemUnpin_inout inout;
    NvRmMemUnpin_out out;
} NvRmMemUnpin_params;

typedef struct NvRmMemGetAddress_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
    NvU32 Offset;
} NV_ALIGN(4) NvRmMemGetAddress_in;

typedef struct NvRmMemGetAddress_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemGetAddress_inout;

typedef struct NvRmMemGetAddress_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemGetAddress_out;

typedef struct NvRmMemGetAddress_params_t
{
    NvRmMemGetAddress_in in;
    NvRmMemGetAddress_inout inout;
    NvRmMemGetAddress_out out;
} NvRmMemGetAddress_params;

typedef struct NvRmMemPinMult_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle  * hMems;
    NvU32  * Addrs;
    NvU32 Count;
} NV_ALIGN(4) NvRmMemPinMult_in;

typedef struct NvRmMemPinMult_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemPinMult_inout;

typedef struct NvRmMemPinMult_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemPinMult_out;

typedef struct NvRmMemPinMult_params_t
{
    NvRmMemPinMult_in in;
    NvRmMemPinMult_inout inout;
    NvRmMemPinMult_out out;
} NvRmMemPinMult_params;

typedef struct NvRmMemPin_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemPin_in;

typedef struct NvRmMemPin_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemPin_inout;

typedef struct NvRmMemPin_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmMemPin_out;

typedef struct NvRmMemPin_params_t
{
    NvRmMemPin_in in;
    NvRmMemPin_inout inout;
    NvRmMemPin_out out;
} NvRmMemPin_params;

typedef struct NvRmMemAlloc_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
    NvRmHeap  * Heaps;
    NvU32 NumHeaps;
    NvU32 Alignment;
    NvOsMemAttribute Coherency;
} NV_ALIGN(4) NvRmMemAlloc_in;

typedef struct NvRmMemAlloc_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemAlloc_inout;

typedef struct NvRmMemAlloc_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmMemAlloc_out;

typedef struct NvRmMemAlloc_params_t
{
    NvRmMemAlloc_in in;
    NvRmMemAlloc_inout inout;
    NvRmMemAlloc_out out;
} NvRmMemAlloc_params;

typedef struct NvRmMemHandleFree_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemHandleFree_in;

typedef struct NvRmMemHandleFree_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandleFree_inout;

typedef struct NvRmMemHandleFree_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandleFree_out;

typedef struct NvRmMemHandleFree_params_t
{
    NvRmMemHandleFree_in in;
    NvRmMemHandleFree_inout inout;
    NvRmMemHandleFree_out out;
} NvRmMemHandleFree_params;

typedef struct NvRmMemHandlePreserveHandle_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmMemHandle hMem;
} NV_ALIGN(4) NvRmMemHandlePreserveHandle_in;

typedef struct NvRmMemHandlePreserveHandle_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandlePreserveHandle_inout;

typedef struct NvRmMemHandlePreserveHandle_out_t
{
    NvError ret_;
    NvU32 Key;
} NV_ALIGN(4) NvRmMemHandlePreserveHandle_out;

typedef struct NvRmMemHandlePreserveHandle_params_t
{
    NvRmMemHandlePreserveHandle_in in;
    NvRmMemHandlePreserveHandle_inout inout;
    NvRmMemHandlePreserveHandle_out out;
} NvRmMemHandlePreserveHandle_params;

typedef struct NvRmMemHandleClaimPreservedHandle_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 Key;
} NV_ALIGN(4) NvRmMemHandleClaimPreservedHandle_in;

typedef struct NvRmMemHandleClaimPreservedHandle_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandleClaimPreservedHandle_inout;

typedef struct NvRmMemHandleClaimPreservedHandle_out_t
{
    NvError ret_;
    NvRmMemHandle phMem;
} NV_ALIGN(4) NvRmMemHandleClaimPreservedHandle_out;

typedef struct NvRmMemHandleClaimPreservedHandle_params_t
{
    NvRmMemHandleClaimPreservedHandle_in in;
    NvRmMemHandleClaimPreservedHandle_inout inout;
    NvRmMemHandleClaimPreservedHandle_out out;
} NvRmMemHandleClaimPreservedHandle_params;

typedef struct NvRmMemHandleCreate_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 Size;
} NV_ALIGN(4) NvRmMemHandleCreate_in;

typedef struct NvRmMemHandleCreate_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMemHandleCreate_inout;

typedef struct NvRmMemHandleCreate_out_t
{
    NvError ret_;
    NvRmMemHandle phMem;
} NV_ALIGN(4) NvRmMemHandleCreate_out;

typedef struct NvRmMemHandleCreate_params_t
{
    NvRmMemHandleCreate_in in;
    NvRmMemHandleCreate_inout inout;
    NvRmMemHandleCreate_out out;
} NvRmMemHandleCreate_params;

static NvError NvRmMemGetStat_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetStat_in *p_in;
    NvRmMemGetStat_out *p_out;

    p_in = (NvRmMemGetStat_in *)InBuffer;
    p_out = (NvRmMemGetStat_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetStat_params, out) - OFFSET(NvRmMemGetStat_params, inout));


    p_out->ret_ = NvRmMemGetStat( p_in->Stat, &p_out->Result );

    return err_;
}

static NvError NvRmMemHandleFromId_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemHandleFromId_in *p_in;
    NvRmMemHandleFromId_out *p_out;
    NvRtObjRefHandle ref_hMem = 0;

    p_in = (NvRmMemHandleFromId_in *)InBuffer;
    p_out = (NvRmMemHandleFromId_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemHandleFromId_params, out) - OFFSET(NvRmMemHandleFromId_params, inout));

    err_ = NvRtAllocObjRef(Ctx, &ref_hMem);
    if (err_ != NvSuccess)
    {
        goto clean;
    }

    p_out->ret_ = NvRmMemHandleFromId( p_in->id, &p_out->hMem );

    if ( p_out->ret_ == NvSuccess )
    {
        NvRtStoreObjRef(Ctx, ref_hMem, NvRtObjType_NvRm_NvRmMemHandle, p_out->hMem);
        ref_hMem = 0;
    }
clean:
    if (ref_hMem) NvRtDiscardObjRef(Ctx, ref_hMem);
    return err_;
}

static NvError NvRmMemGetId_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetId_in *p_in;
    NvRmMemGetId_out *p_out;

    p_in = (NvRmMemGetId_in *)InBuffer;
    p_out = (NvRmMemGetId_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetId_params, out) - OFFSET(NvRmMemGetId_params, inout));


    p_out->ret_ = NvRmMemGetId( p_in->hMem );

    return err_;
}

static NvError NvRmMemGetHeapType_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetHeapType_in *p_in;
    NvRmMemGetHeapType_out *p_out;

    p_in = (NvRmMemGetHeapType_in *)InBuffer;
    p_out = (NvRmMemGetHeapType_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetHeapType_params, out) - OFFSET(NvRmMemGetHeapType_params, inout));


    p_out->ret_ = NvRmMemGetHeapType( p_in->hMem, &p_out->BasePhysAddr );

    return err_;
}

static NvError NvRmMemGetCacheLineSize_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetCacheLineSize_out *p_out;
    p_out = (NvRmMemGetCacheLineSize_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetCacheLineSize_params, out) - OFFSET(NvRmMemGetCacheLineSize_params, inout));


    p_out->ret_ = NvRmMemGetCacheLineSize(  );

    return err_;
}

static NvError NvRmMemGetAlignment_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetAlignment_in *p_in;
    NvRmMemGetAlignment_out *p_out;

    p_in = (NvRmMemGetAlignment_in *)InBuffer;
    p_out = (NvRmMemGetAlignment_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetAlignment_params, out) - OFFSET(NvRmMemGetAlignment_params, inout));


    p_out->ret_ = NvRmMemGetAlignment( p_in->hMem );

    return err_;
}

static NvError NvRmMemGetSize_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetSize_in *p_in;
    NvRmMemGetSize_out *p_out;

    p_in = (NvRmMemGetSize_in *)InBuffer;
    p_out = (NvRmMemGetSize_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetSize_params, out) - OFFSET(NvRmMemGetSize_params, inout));


    p_out->ret_ = NvRmMemGetSize( p_in->hMem );

    return err_;
}

static NvError NvRmMemMove_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemMove_in *p_in;

    p_in = (NvRmMemMove_in *)InBuffer;


    NvRmMemMove( p_in->hDstMem, p_in->DstOffset, p_in->hSrcMem, p_in->SrcOffset, p_in->Size );

    return err_;
}

static NvError NvRmMemUnpinMult_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemUnpinMult_in *p_in;
    NvRmMemHandle *hMems = NULL;

    p_in = (NvRmMemUnpinMult_in *)InBuffer;

    if( p_in->Count && p_in->hMems )
    {
        hMems = (NvRmMemHandle  *)NvOsAlloc( p_in->Count * sizeof( NvRmMemHandle  ) );
        if( !hMems )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->hMems )
        {
            err_ = NvOsCopyIn( hMems, p_in->hMems, p_in->Count * sizeof( NvRmMemHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRmMemUnpinMult( hMems, p_in->Count );

clean:
    NvOsFree( hMems );
    return err_;
}

static NvError NvRmMemUnpin_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemUnpin_in *p_in;

    p_in = (NvRmMemUnpin_in *)InBuffer;


    NvRmMemUnpin( p_in->hMem );

    return err_;
}

static NvError NvRmMemGetAddress_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemGetAddress_in *p_in;
    NvRmMemGetAddress_out *p_out;

    p_in = (NvRmMemGetAddress_in *)InBuffer;
    p_out = (NvRmMemGetAddress_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemGetAddress_params, out) - OFFSET(NvRmMemGetAddress_params, inout));


    p_out->ret_ = NvRmMemGetAddress( p_in->hMem, p_in->Offset );

    return err_;
}

static NvError NvRmMemPinMult_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemPinMult_in *p_in;
    NvRmMemHandle *hMems = NULL;
    NvU32  *Addrs = NULL;

    p_in = (NvRmMemPinMult_in *)InBuffer;

    if( p_in->Count && p_in->hMems )
    {
        hMems = (NvRmMemHandle  *)NvOsAlloc( p_in->Count * sizeof( NvRmMemHandle  ) );
        if( !hMems )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->hMems )
        {
            err_ = NvOsCopyIn( hMems, p_in->hMems, p_in->Count * sizeof( NvRmMemHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->Count && p_in->Addrs )
    {
        Addrs = (NvU32  *)NvOsAlloc( p_in->Count * sizeof( NvU32  ) );
        if( !Addrs )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    NvRmMemPinMult( hMems, Addrs, p_in->Count );

    if(p_in->Addrs && Addrs)
    {
        err_ = NvOsCopyOut( p_in->Addrs, Addrs, p_in->Count * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( hMems );
    NvOsFree( Addrs );
    return err_;
}

static NvError NvRmMemPin_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemPin_in *p_in;
    NvRmMemPin_out *p_out;

    p_in = (NvRmMemPin_in *)InBuffer;
    p_out = (NvRmMemPin_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemPin_params, out) - OFFSET(NvRmMemPin_params, inout));


    p_out->ret_ = NvRmMemPin( p_in->hMem );

    return err_;
}

static NvError NvRmMemAlloc_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemAlloc_in *p_in;
    NvRmMemAlloc_out *p_out;
    NvRmHeap *Heaps = NULL;

    p_in = (NvRmMemAlloc_in *)InBuffer;
    p_out = (NvRmMemAlloc_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemAlloc_params, out) - OFFSET(NvRmMemAlloc_params, inout));

    if( p_in->NumHeaps && p_in->Heaps )
    {
        Heaps = (NvRmHeap  *)NvOsAlloc( p_in->NumHeaps * sizeof( NvRmHeap  ) );
        if( !Heaps )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->Heaps )
        {
            err_ = NvOsCopyIn( Heaps, p_in->Heaps, p_in->NumHeaps * sizeof( NvRmHeap  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmMemAlloc( p_in->hMem, Heaps, p_in->NumHeaps, p_in->Alignment, p_in->Coherency );

clean:
    NvOsFree( Heaps );
    return err_;
}

static NvError NvRmMemHandleFree_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemHandleFree_in *p_in;

    p_in = (NvRmMemHandleFree_in *)InBuffer;

    if (p_in->hMem != NULL) NvRtFreeObjRef(Ctx, NvRtObjType_NvRm_NvRmMemHandle, p_in->hMem);

    NvRmMemHandleFree( p_in->hMem );

    return err_;
}

static NvError NvRmMemHandlePreserveHandle_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemHandlePreserveHandle_in *p_in;
    NvRmMemHandlePreserveHandle_out *p_out;

    p_in = (NvRmMemHandlePreserveHandle_in *)InBuffer;
    p_out = (NvRmMemHandlePreserveHandle_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemHandlePreserveHandle_params, out) - OFFSET(NvRmMemHandlePreserveHandle_params, inout));


    p_out->ret_ = NvRmMemHandlePreserveHandle( p_in->hMem, &p_out->Key );

    return err_;
}

static NvError NvRmMemHandleClaimPreservedHandle_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemHandleClaimPreservedHandle_in *p_in;
    NvRmMemHandleClaimPreservedHandle_out *p_out;
    NvRtObjRefHandle ref_phMem = 0;

    p_in = (NvRmMemHandleClaimPreservedHandle_in *)InBuffer;
    p_out = (NvRmMemHandleClaimPreservedHandle_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemHandleClaimPreservedHandle_params, out) - OFFSET(NvRmMemHandleClaimPreservedHandle_params, inout));

    err_ = NvRtAllocObjRef(Ctx, &ref_phMem);
    if (err_ != NvSuccess)
    {
        goto clean;
    }

    p_out->ret_ = NvRmMemHandleClaimPreservedHandle( p_in->hDevice, p_in->Key, &p_out->phMem );

    if ( p_out->ret_ == NvSuccess )
    {
        NvRtStoreObjRef(Ctx, ref_phMem, NvRtObjType_NvRm_NvRmMemHandle, p_out->phMem);
        ref_phMem = 0;
    }
clean:
    if (ref_phMem) NvRtDiscardObjRef(Ctx, ref_phMem);
    return err_;
}

static NvError NvRmMemHandleCreate_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMemHandleCreate_in *p_in;
    NvRmMemHandleCreate_out *p_out;
    NvRtObjRefHandle ref_phMem = 0;

    p_in = (NvRmMemHandleCreate_in *)InBuffer;
    p_out = (NvRmMemHandleCreate_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMemHandleCreate_params, out) - OFFSET(NvRmMemHandleCreate_params, inout));

    err_ = NvRtAllocObjRef(Ctx, &ref_phMem);
    if (err_ != NvSuccess)
    {
        goto clean;
    }

    p_out->ret_ = NvRmMemHandleCreate( p_in->hDevice, &p_out->phMem, p_in->Size );

    if ( p_out->ret_ == NvSuccess )
    {
        NvRtStoreObjRef(Ctx, ref_phMem, NvRtObjType_NvRm_NvRmMemHandle, p_out->phMem);
        ref_phMem = 0;
    }
clean:
    if (ref_phMem) NvRtDiscardObjRef(Ctx, ref_phMem);
    return err_;
}

NvError nvrm_memmgr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_memmgr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 17:
        err_ = NvRmMemGetStat_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 16:
        err_ = NvRmMemHandleFromId_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 15:
        err_ = NvRmMemGetId_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 14:
        err_ = NvRmMemGetHeapType_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 13:
        err_ = NvRmMemGetCacheLineSize_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 12:
        err_ = NvRmMemGetAlignment_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 11:
        err_ = NvRmMemGetSize_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 10:
        err_ = NvRmMemMove_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRmMemUnpinMult_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmMemUnpin_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmMemGetAddress_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmMemPinMult_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmMemPin_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmMemAlloc_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmMemHandleFree_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmMemHandlePreserveHandle_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmMemHandleClaimPreservedHandle_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmMemHandleCreate_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
