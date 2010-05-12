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
#include "nvrm_gpio.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmGpioGetIrqs_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmGpioPinHandle  * pin;
    NvU32  * Irq;
    NvU32 pinCount;
} NV_ALIGN(4) NvRmGpioGetIrqs_in;

typedef struct NvRmGpioGetIrqs_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioGetIrqs_inout;

typedef struct NvRmGpioGetIrqs_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmGpioGetIrqs_out;

typedef struct NvRmGpioGetIrqs_params_t
{
    NvRmGpioGetIrqs_in in;
    NvRmGpioGetIrqs_inout inout;
    NvRmGpioGetIrqs_out out;
} NvRmGpioGetIrqs_params;

typedef struct NvRmGpioConfigPins_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
    NvRmGpioPinHandle  * pin;
    NvU32 pinCount;
    NvRmGpioPinMode Mode;
} NV_ALIGN(4) NvRmGpioConfigPins_in;

typedef struct NvRmGpioConfigPins_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioConfigPins_inout;

typedef struct NvRmGpioConfigPins_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmGpioConfigPins_out;

typedef struct NvRmGpioConfigPins_params_t
{
    NvRmGpioConfigPins_in in;
    NvRmGpioConfigPins_inout inout;
    NvRmGpioConfigPins_out out;
} NvRmGpioConfigPins_params;

typedef struct NvRmGpioReadPins_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
    NvRmGpioPinHandle  * pin;
    NvRmGpioPinState  * pPinState;
    NvU32 pinCount;
} NV_ALIGN(4) NvRmGpioReadPins_in;

typedef struct NvRmGpioReadPins_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioReadPins_inout;

typedef struct NvRmGpioReadPins_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioReadPins_out;

typedef struct NvRmGpioReadPins_params_t
{
    NvRmGpioReadPins_in in;
    NvRmGpioReadPins_inout inout;
    NvRmGpioReadPins_out out;
} NvRmGpioReadPins_params;

typedef struct NvRmGpioWritePins_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
    NvRmGpioPinHandle  * pin;
    NvRmGpioPinState  * pinState;
    NvU32 pinCount;
} NV_ALIGN(4) NvRmGpioWritePins_in;

typedef struct NvRmGpioWritePins_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioWritePins_inout;

typedef struct NvRmGpioWritePins_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioWritePins_out;

typedef struct NvRmGpioWritePins_params_t
{
    NvRmGpioWritePins_in in;
    NvRmGpioWritePins_inout inout;
    NvRmGpioWritePins_out out;
} NvRmGpioWritePins_params;

typedef struct NvRmGpioReleasePinHandles_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
    NvRmGpioPinHandle  * hPin;
    NvU32 pinCount;
} NV_ALIGN(4) NvRmGpioReleasePinHandles_in;

typedef struct NvRmGpioReleasePinHandles_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioReleasePinHandles_inout;

typedef struct NvRmGpioReleasePinHandles_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioReleasePinHandles_out;

typedef struct NvRmGpioReleasePinHandles_params_t
{
    NvRmGpioReleasePinHandles_in in;
    NvRmGpioReleasePinHandles_inout inout;
    NvRmGpioReleasePinHandles_out out;
} NvRmGpioReleasePinHandles_params;

typedef struct NvRmGpioAcquirePinHandle_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
    NvU32 port;
    NvU32 pin;
} NV_ALIGN(4) NvRmGpioAcquirePinHandle_in;

typedef struct NvRmGpioAcquirePinHandle_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioAcquirePinHandle_inout;

typedef struct NvRmGpioAcquirePinHandle_out_t
{
    NvError ret_;
    NvRmGpioPinHandle phPin;
} NV_ALIGN(4) NvRmGpioAcquirePinHandle_out;

typedef struct NvRmGpioAcquirePinHandle_params_t
{
    NvRmGpioAcquirePinHandle_in in;
    NvRmGpioAcquirePinHandle_inout inout;
    NvRmGpioAcquirePinHandle_out out;
} NvRmGpioAcquirePinHandle_params;

typedef struct NvRmGpioClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmGpioHandle hGpio;
} NV_ALIGN(4) NvRmGpioClose_in;

typedef struct NvRmGpioClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioClose_inout;

typedef struct NvRmGpioClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioClose_out;

typedef struct NvRmGpioClose_params_t
{
    NvRmGpioClose_in in;
    NvRmGpioClose_inout inout;
    NvRmGpioClose_out out;
} NvRmGpioClose_params;

typedef struct NvRmGpioOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmGpioOpen_in;

typedef struct NvRmGpioOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGpioOpen_inout;

typedef struct NvRmGpioOpen_out_t
{
    NvError ret_;
    NvRmGpioHandle phGpio;
} NV_ALIGN(4) NvRmGpioOpen_out;

typedef struct NvRmGpioOpen_params_t
{
    NvRmGpioOpen_in in;
    NvRmGpioOpen_inout inout;
    NvRmGpioOpen_out out;
} NvRmGpioOpen_params;

static NvError NvRmGpioGetIrqs_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioGetIrqs_in *p_in;
    NvRmGpioGetIrqs_out *p_out;
    NvRmGpioPinHandle *pin = NULL;
    NvU32  *Irq = NULL;

    p_in = (NvRmGpioGetIrqs_in *)InBuffer;
    p_out = (NvRmGpioGetIrqs_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGpioGetIrqs_params, out) - OFFSET(NvRmGpioGetIrqs_params, inout));

    if( p_in->pinCount && p_in->pin )
    {
        pin = (NvRmGpioPinHandle  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
        if( !pin )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pin )
        {
            err_ = NvOsCopyIn( pin, p_in->pin, p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->pinCount && p_in->Irq )
    {
        Irq = (NvU32  *)NvOsAlloc( p_in->pinCount * sizeof( NvU32  ) );
        if( !Irq )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmGpioGetIrqs( p_in->hRmDevice, pin, Irq, p_in->pinCount );

    if(p_in->Irq && Irq)
    {
        err_ = NvOsCopyOut( p_in->Irq, Irq, p_in->pinCount * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pin );
    NvOsFree( Irq );
    return err_;
}

static NvError NvRmGpioConfigPins_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioConfigPins_in *p_in;
    NvRmGpioConfigPins_out *p_out;
    NvRmGpioPinHandle *pin = NULL;

    p_in = (NvRmGpioConfigPins_in *)InBuffer;
    p_out = (NvRmGpioConfigPins_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGpioConfigPins_params, out) - OFFSET(NvRmGpioConfigPins_params, inout));

    if( p_in->pinCount && p_in->pin )
    {
        pin = (NvRmGpioPinHandle  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
        if( !pin )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pin )
        {
            err_ = NvOsCopyIn( pin, p_in->pin, p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmGpioConfigPins( p_in->hGpio, pin, p_in->pinCount, p_in->Mode );

clean:
    NvOsFree( pin );
    return err_;
}

static NvError NvRmGpioReadPins_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioReadPins_in *p_in;
    NvRmGpioPinHandle *pin = NULL;
    NvRmGpioPinState *pPinState = NULL;

    p_in = (NvRmGpioReadPins_in *)InBuffer;

    if( p_in->pinCount && p_in->pin )
    {
        pin = (NvRmGpioPinHandle  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
        if( !pin )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pin )
        {
            err_ = NvOsCopyIn( pin, p_in->pin, p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->pinCount && p_in->pPinState )
    {
        pPinState = (NvRmGpioPinState  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinState  ) );
        if( !pPinState )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    NvRmGpioReadPins( p_in->hGpio, pin, pPinState, p_in->pinCount );

    if(p_in->pPinState && pPinState)
    {
        err_ = NvOsCopyOut( p_in->pPinState, pPinState, p_in->pinCount * sizeof( NvRmGpioPinState  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pin );
    NvOsFree( pPinState );
    return err_;
}

static NvError NvRmGpioWritePins_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioWritePins_in *p_in;
    NvRmGpioPinHandle *pin = NULL;
    NvRmGpioPinState *pinState = NULL;

    p_in = (NvRmGpioWritePins_in *)InBuffer;

    if( p_in->pinCount && p_in->pin )
    {
        pin = (NvRmGpioPinHandle  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
        if( !pin )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pin )
        {
            err_ = NvOsCopyIn( pin, p_in->pin, p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->pinCount && p_in->pinState )
    {
        pinState = (NvRmGpioPinState  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinState  ) );
        if( !pinState )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pinState )
        {
            err_ = NvOsCopyIn( pinState, p_in->pinState, p_in->pinCount * sizeof( NvRmGpioPinState  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRmGpioWritePins( p_in->hGpio, pin, pinState, p_in->pinCount );

clean:
    NvOsFree( pin );
    NvOsFree( pinState );
    return err_;
}

static NvError NvRmGpioReleasePinHandles_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioReleasePinHandles_in *p_in;
    NvRmGpioPinHandle *hPin = NULL;

    p_in = (NvRmGpioReleasePinHandles_in *)InBuffer;

    if( p_in->pinCount && p_in->hPin )
    {
        hPin = (NvRmGpioPinHandle  *)NvOsAlloc( p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
        if( !hPin )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->hPin )
        {
            err_ = NvOsCopyIn( hPin, p_in->hPin, p_in->pinCount * sizeof( NvRmGpioPinHandle  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRmGpioReleasePinHandles( p_in->hGpio, hPin, p_in->pinCount );

clean:
    NvOsFree( hPin );
    return err_;
}

static NvError NvRmGpioAcquirePinHandle_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioAcquirePinHandle_in *p_in;
    NvRmGpioAcquirePinHandle_out *p_out;

    p_in = (NvRmGpioAcquirePinHandle_in *)InBuffer;
    p_out = (NvRmGpioAcquirePinHandle_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGpioAcquirePinHandle_params, out) - OFFSET(NvRmGpioAcquirePinHandle_params, inout));


    p_out->ret_ = NvRmGpioAcquirePinHandle( p_in->hGpio, p_in->port, p_in->pin, &p_out->phPin );

    return err_;
}

static NvError NvRmGpioClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioClose_in *p_in;

    p_in = (NvRmGpioClose_in *)InBuffer;


    NvRmGpioClose( p_in->hGpio );

    return err_;
}

static NvError NvRmGpioOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGpioOpen_in *p_in;
    NvRmGpioOpen_out *p_out;

    p_in = (NvRmGpioOpen_in *)InBuffer;
    p_out = (NvRmGpioOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGpioOpen_params, out) - OFFSET(NvRmGpioOpen_params, inout));


    p_out->ret_ = NvRmGpioOpen( p_in->hRmDevice, &p_out->phGpio );

    return err_;
}

NvError nvrm_gpio_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_gpio_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 7:
        err_ = NvRmGpioGetIrqs_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmGpioConfigPins_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmGpioReadPins_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmGpioWritePins_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmGpioReleasePinHandles_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmGpioAcquirePinHandle_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmGpioClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmGpioOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
