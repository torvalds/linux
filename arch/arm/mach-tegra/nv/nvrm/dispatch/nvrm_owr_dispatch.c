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
#include "nvrm_owr.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmOwrTransaction_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmOwrHandle hOwr;
    NvU32 OwrPinMap;
    NvU8  * Data;
    NvU32 DataLen;
    NvRmOwrTransactionInfo  * Transaction;
    NvU32 NumOfTransactions;
} NV_ALIGN(4) NvRmOwrTransaction_in;

typedef struct NvRmOwrTransaction_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOwrTransaction_inout;

typedef struct NvRmOwrTransaction_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmOwrTransaction_out;

typedef struct NvRmOwrTransaction_params_t
{
    NvRmOwrTransaction_in in;
    NvRmOwrTransaction_inout inout;
    NvRmOwrTransaction_out out;
} NvRmOwrTransaction_params;

typedef struct NvRmOwrClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmOwrHandle hOwr;
} NV_ALIGN(4) NvRmOwrClose_in;

typedef struct NvRmOwrClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOwrClose_inout;

typedef struct NvRmOwrClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOwrClose_out;

typedef struct NvRmOwrClose_params_t
{
    NvRmOwrClose_in in;
    NvRmOwrClose_inout inout;
    NvRmOwrClose_out out;
} NvRmOwrClose_params;

typedef struct NvRmOwrOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 instance;
} NV_ALIGN(4) NvRmOwrOpen_in;

typedef struct NvRmOwrOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOwrOpen_inout;

typedef struct NvRmOwrOpen_out_t
{
    NvError ret_;
    NvRmOwrHandle hOwr;
} NV_ALIGN(4) NvRmOwrOpen_out;

typedef struct NvRmOwrOpen_params_t
{
    NvRmOwrOpen_in in;
    NvRmOwrOpen_inout inout;
    NvRmOwrOpen_out out;
} NvRmOwrOpen_params;

static NvError NvRmOwrTransaction_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmOwrTransaction_in *p_in;
    NvRmOwrTransaction_out *p_out;
    NvU8  *Data = NULL;
    NvRmOwrTransactionInfo *Transaction = NULL;

    p_in = (NvRmOwrTransaction_in *)InBuffer;
    p_out = (NvRmOwrTransaction_out *)((NvU8 *)OutBuffer + OFFSET(NvRmOwrTransaction_params, out) - OFFSET(NvRmOwrTransaction_params, inout));

    if( p_in->DataLen && p_in->Data )
    {
        Data = (NvU8  *)NvOsAlloc( p_in->DataLen * sizeof( NvU8  ) );
        if( !Data )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->Data )
        {
            err_ = NvOsCopyIn( Data, p_in->Data, p_in->DataLen * sizeof( NvU8  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->NumOfTransactions && p_in->Transaction )
    {
        Transaction = (NvRmOwrTransactionInfo  *)NvOsAlloc( p_in->NumOfTransactions * sizeof( NvRmOwrTransactionInfo  ) );
        if( !Transaction )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->Transaction )
        {
            err_ = NvOsCopyIn( Transaction, p_in->Transaction, p_in->NumOfTransactions * sizeof( NvRmOwrTransactionInfo  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmOwrTransaction( p_in->hOwr, p_in->OwrPinMap, Data, p_in->DataLen, Transaction, p_in->NumOfTransactions );

    if(p_in->Data && Data)
    {
        err_ = NvOsCopyOut( p_in->Data, Data, p_in->DataLen * sizeof( NvU8  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( Data );
    NvOsFree( Transaction );
    return err_;
}

static NvError NvRmOwrClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmOwrClose_in *p_in;

    p_in = (NvRmOwrClose_in *)InBuffer;


    NvRmOwrClose( p_in->hOwr );

    return err_;
}

static NvError NvRmOwrOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmOwrOpen_in *p_in;
    NvRmOwrOpen_out *p_out;

    p_in = (NvRmOwrOpen_in *)InBuffer;
    p_out = (NvRmOwrOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmOwrOpen_params, out) - OFFSET(NvRmOwrOpen_params, inout));


    p_out->ret_ = NvRmOwrOpen( p_in->hDevice, p_in->instance, &p_out->hOwr );

    return err_;
}

NvError nvrm_owr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_owr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 2:
        err_ = NvRmOwrTransaction_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmOwrClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmOwrOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
