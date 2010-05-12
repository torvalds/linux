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
#include "nvrm_spi.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmSpiSetSignalMode_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmSpiHandle hRmSpi;
    NvU32 ChipSelectId;
    NvU32 SpiSignalMode;
} NV_ALIGN(4) NvRmSpiSetSignalMode_in;

typedef struct NvRmSpiSetSignalMode_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiSetSignalMode_inout;

typedef struct NvRmSpiSetSignalMode_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiSetSignalMode_out;

typedef struct NvRmSpiSetSignalMode_params_t
{
    NvRmSpiSetSignalMode_in in;
    NvRmSpiSetSignalMode_inout inout;
    NvRmSpiSetSignalMode_out out;
} NvRmSpiSetSignalMode_params;

typedef struct NvRmSpiGetTransactionData_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmSpiHandle hRmSpi;
    NvU8  * pReadBuffer;
    NvU32 BytesRequested;
    NvU32 WaitTimeout;
} NV_ALIGN(4) NvRmSpiGetTransactionData_in;

typedef struct NvRmSpiGetTransactionData_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiGetTransactionData_inout;

typedef struct NvRmSpiGetTransactionData_out_t
{
    NvError ret_;
    NvU32 pBytesTransfererd;
} NV_ALIGN(4) NvRmSpiGetTransactionData_out;

typedef struct NvRmSpiGetTransactionData_params_t
{
    NvRmSpiGetTransactionData_in in;
    NvRmSpiGetTransactionData_inout inout;
    NvRmSpiGetTransactionData_out out;
} NvRmSpiGetTransactionData_params;

typedef struct NvRmSpiStartTransaction_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmSpiHandle hRmSpi;
    NvU32 ChipSelectId;
    NvU32 ClockSpeedInKHz;
    NvBool IsReadTransfer;
    NvU8  * pWriteBuffer;
    NvU32 BytesRequested;
    NvU32 PacketSizeInBits;
} NV_ALIGN(4) NvRmSpiStartTransaction_in;

typedef struct NvRmSpiStartTransaction_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiStartTransaction_inout;

typedef struct NvRmSpiStartTransaction_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmSpiStartTransaction_out;

typedef struct NvRmSpiStartTransaction_params_t
{
    NvRmSpiStartTransaction_in in;
    NvRmSpiStartTransaction_inout inout;
    NvRmSpiStartTransaction_out out;
} NvRmSpiStartTransaction_params;

typedef struct NvRmSpiTransaction_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmSpiHandle hRmSpi;
    NvU32 SpiPinMap;
    NvU32 ChipSelectId;
    NvU32 ClockSpeedInKHz;
    NvU8  * pReadBuffer;
    NvU8  * pWriteBuffer;
    NvU32 BytesRequested;
    NvU32 PacketSizeInBits;
} NV_ALIGN(4) NvRmSpiTransaction_in;

typedef struct NvRmSpiTransaction_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiTransaction_inout;

typedef struct NvRmSpiTransaction_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiTransaction_out;

typedef struct NvRmSpiTransaction_params_t
{
    NvRmSpiTransaction_in in;
    NvRmSpiTransaction_inout inout;
    NvRmSpiTransaction_out out;
} NvRmSpiTransaction_params;

typedef struct NvRmSpiClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmSpiHandle hRmSpi;
} NV_ALIGN(4) NvRmSpiClose_in;

typedef struct NvRmSpiClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiClose_inout;

typedef struct NvRmSpiClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiClose_out;

typedef struct NvRmSpiClose_params_t
{
    NvRmSpiClose_in in;
    NvRmSpiClose_inout inout;
    NvRmSpiClose_out out;
} NvRmSpiClose_params;

typedef struct NvRmSpiOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvU32 IoModule;
    NvU32 InstanceId;
    NvBool IsMasterMode;
} NV_ALIGN(4) NvRmSpiOpen_in;

typedef struct NvRmSpiOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSpiOpen_inout;

typedef struct NvRmSpiOpen_out_t
{
    NvError ret_;
    NvRmSpiHandle phRmSpi;
} NV_ALIGN(4) NvRmSpiOpen_out;

typedef struct NvRmSpiOpen_params_t
{
    NvRmSpiOpen_in in;
    NvRmSpiOpen_inout inout;
    NvRmSpiOpen_out out;
} NvRmSpiOpen_params;

static NvError NvRmSpiSetSignalMode_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiSetSignalMode_in *p_in;

    p_in = (NvRmSpiSetSignalMode_in *)InBuffer;


    NvRmSpiSetSignalMode( p_in->hRmSpi, p_in->ChipSelectId, p_in->SpiSignalMode );

    return err_;
}

static NvError NvRmSpiGetTransactionData_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiGetTransactionData_in *p_in;
    NvRmSpiGetTransactionData_out *p_out;
    NvU8  *pReadBuffer = NULL;

    p_in = (NvRmSpiGetTransactionData_in *)InBuffer;
    p_out = (NvRmSpiGetTransactionData_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSpiGetTransactionData_params, out) - OFFSET(NvRmSpiGetTransactionData_params, inout));

    if( p_in->BytesRequested && p_in->pReadBuffer )
    {
        pReadBuffer = (NvU8  *)NvOsAlloc( p_in->BytesRequested * sizeof( NvU8  ) );
        if( !pReadBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmSpiGetTransactionData( p_in->hRmSpi, pReadBuffer, p_in->BytesRequested, &p_out->pBytesTransfererd, p_in->WaitTimeout );

    if(p_in->pReadBuffer && pReadBuffer)
    {
        err_ = NvOsCopyOut( p_in->pReadBuffer, pReadBuffer, p_in->BytesRequested * sizeof( NvU8  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pReadBuffer );
    return err_;
}

static NvError NvRmSpiStartTransaction_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiStartTransaction_in *p_in;
    NvRmSpiStartTransaction_out *p_out;
    NvU8  *pWriteBuffer = NULL;

    p_in = (NvRmSpiStartTransaction_in *)InBuffer;
    p_out = (NvRmSpiStartTransaction_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSpiStartTransaction_params, out) - OFFSET(NvRmSpiStartTransaction_params, inout));

    if( p_in->BytesRequested && p_in->pWriteBuffer )
    {
        pWriteBuffer = (NvU8  *)NvOsAlloc( p_in->BytesRequested * sizeof( NvU8  ) );
        if( !pWriteBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pWriteBuffer )
        {
            err_ = NvOsCopyIn( pWriteBuffer, p_in->pWriteBuffer, p_in->BytesRequested * sizeof( NvU8  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmSpiStartTransaction( p_in->hRmSpi, p_in->ChipSelectId, p_in->ClockSpeedInKHz, p_in->IsReadTransfer, pWriteBuffer, p_in->BytesRequested, p_in->PacketSizeInBits );

clean:
    NvOsFree( pWriteBuffer );
    return err_;
}

static NvError NvRmSpiTransaction_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiTransaction_in *p_in;
    NvU8  *pReadBuffer = NULL;
    NvU8  *pWriteBuffer = NULL;

    p_in = (NvRmSpiTransaction_in *)InBuffer;

    if( p_in->BytesRequested && p_in->pReadBuffer )
    {
        pReadBuffer = (NvU8  *)NvOsAlloc( p_in->BytesRequested * sizeof( NvU8  ) );
        if( !pReadBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }
    if( p_in->BytesRequested && p_in->pWriteBuffer )
    {
        pWriteBuffer = (NvU8  *)NvOsAlloc( p_in->BytesRequested * sizeof( NvU8  ) );
        if( !pWriteBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pWriteBuffer )
        {
            err_ = NvOsCopyIn( pWriteBuffer, p_in->pWriteBuffer, p_in->BytesRequested * sizeof( NvU8  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRmSpiTransaction( p_in->hRmSpi, p_in->SpiPinMap, p_in->ChipSelectId, p_in->ClockSpeedInKHz, pReadBuffer, pWriteBuffer, p_in->BytesRequested, p_in->PacketSizeInBits );

    if(p_in->pReadBuffer && pReadBuffer)
    {
        err_ = NvOsCopyOut( p_in->pReadBuffer, pReadBuffer, p_in->BytesRequested * sizeof( NvU8  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pReadBuffer );
    NvOsFree( pWriteBuffer );
    return err_;
}

static NvError NvRmSpiClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiClose_in *p_in;

    p_in = (NvRmSpiClose_in *)InBuffer;


    NvRmSpiClose( p_in->hRmSpi );

    return err_;
}

static NvError NvRmSpiOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSpiOpen_in *p_in;
    NvRmSpiOpen_out *p_out;

    p_in = (NvRmSpiOpen_in *)InBuffer;
    p_out = (NvRmSpiOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSpiOpen_params, out) - OFFSET(NvRmSpiOpen_params, inout));


    p_out->ret_ = NvRmSpiOpen( p_in->hRmDevice, p_in->IoModule, p_in->InstanceId, p_in->IsMasterMode, &p_out->phRmSpi );

    return err_;
}

NvError nvrm_spi_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_spi_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 5:
        err_ = NvRmSpiSetSignalMode_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmSpiGetTransactionData_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmSpiStartTransaction_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmSpiTransaction_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmSpiClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmSpiOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
