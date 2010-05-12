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
#include "nvrm_i2c.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmI2cTransaction_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmI2cHandle hI2c;
    NvU32 I2cPinMap;
    NvU32 WaitTimeoutInMilliSeconds;
    NvU32 ClockSpeedKHz;
    NvU8  * Data;
    NvU32 DataLen;
    NvRmI2cTransactionInfo  * Transaction;
    NvU32 NumOfTransactions;
} NV_ALIGN(4) NvRmI2cTransaction_in;

typedef struct NvRmI2cTransaction_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmI2cTransaction_inout;

typedef struct NvRmI2cTransaction_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmI2cTransaction_out;

typedef struct NvRmI2cTransaction_params_t
{
    NvRmI2cTransaction_in in;
    NvRmI2cTransaction_inout inout;
    NvRmI2cTransaction_out out;
} NvRmI2cTransaction_params;

typedef struct NvRmI2cClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmI2cHandle hI2c;
} NV_ALIGN(4) NvRmI2cClose_in;

typedef struct NvRmI2cClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmI2cClose_inout;

typedef struct NvRmI2cClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmI2cClose_out;

typedef struct NvRmI2cClose_params_t
{
    NvRmI2cClose_in in;
    NvRmI2cClose_inout inout;
    NvRmI2cClose_out out;
} NvRmI2cClose_params;

typedef struct NvRmI2cOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 IoModule;
    NvU32 instance;
} NV_ALIGN(4) NvRmI2cOpen_in;

typedef struct NvRmI2cOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmI2cOpen_inout;

typedef struct NvRmI2cOpen_out_t
{
    NvError ret_;
    NvRmI2cHandle phI2c;
} NV_ALIGN(4) NvRmI2cOpen_out;

typedef struct NvRmI2cOpen_params_t
{
    NvRmI2cOpen_in in;
    NvRmI2cOpen_inout inout;
    NvRmI2cOpen_out out;
} NvRmI2cOpen_params;

static NvError NvRmI2cTransaction_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmI2cTransaction_in *p_in;
    NvRmI2cTransaction_out *p_out;
    NvU8  *Data = NULL;
    NvRmI2cTransactionInfo *Transaction = NULL;

    p_in = (NvRmI2cTransaction_in *)InBuffer;
    p_out = (NvRmI2cTransaction_out *)((NvU8 *)OutBuffer + OFFSET(NvRmI2cTransaction_params, out) - OFFSET(NvRmI2cTransaction_params, inout));

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
        Transaction = (NvRmI2cTransactionInfo  *)NvOsAlloc( p_in->NumOfTransactions * sizeof( NvRmI2cTransactionInfo  ) );
        if( !Transaction )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->Transaction )
        {
            err_ = NvOsCopyIn( Transaction, p_in->Transaction, p_in->NumOfTransactions * sizeof( NvRmI2cTransactionInfo  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmI2cTransaction( p_in->hI2c, p_in->I2cPinMap, p_in->WaitTimeoutInMilliSeconds, p_in->ClockSpeedKHz, Data, p_in->DataLen, Transaction, p_in->NumOfTransactions );

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

static NvError NvRmI2cClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmI2cClose_in *p_in;

    p_in = (NvRmI2cClose_in *)InBuffer;


    NvRmI2cClose( p_in->hI2c );

    return err_;
}

static NvError NvRmI2cOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmI2cOpen_in *p_in;
    NvRmI2cOpen_out *p_out;

    p_in = (NvRmI2cOpen_in *)InBuffer;
    p_out = (NvRmI2cOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmI2cOpen_params, out) - OFFSET(NvRmI2cOpen_params, inout));


    p_out->ret_ = NvRmI2cOpen( p_in->hDevice, p_in->IoModule, p_in->instance, &p_out->phI2c );

    return err_;
}

NvError nvrm_i2c_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_i2c_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 2:
        err_ = NvRmI2cTransaction_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmI2cClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmI2cOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
