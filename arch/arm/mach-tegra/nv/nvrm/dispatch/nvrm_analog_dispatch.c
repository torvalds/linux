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
#include "nvrm_analog.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmUsbDetectChargerState_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 wait;
} NV_ALIGN(4) NvRmUsbDetectChargerState_in;

typedef struct NvRmUsbDetectChargerState_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmUsbDetectChargerState_inout;

typedef struct NvRmUsbDetectChargerState_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmUsbDetectChargerState_out;

typedef struct NvRmUsbDetectChargerState_params_t
{
    NvRmUsbDetectChargerState_in in;
    NvRmUsbDetectChargerState_inout inout;
    NvRmUsbDetectChargerState_out out;
} NvRmUsbDetectChargerState_params;

typedef struct NvRmUsbIsConnected_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmUsbIsConnected_in;

typedef struct NvRmUsbIsConnected_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmUsbIsConnected_inout;

typedef struct NvRmUsbIsConnected_out_t
{
    NvBool ret_;
} NV_ALIGN(4) NvRmUsbIsConnected_out;

typedef struct NvRmUsbIsConnected_params_t
{
    NvRmUsbIsConnected_in in;
    NvRmUsbIsConnected_inout inout;
    NvRmUsbIsConnected_out out;
} NvRmUsbIsConnected_params;

typedef struct NvRmAnalogGetTvDacConfiguration_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmAnalogTvDacType Type;
} NV_ALIGN(4) NvRmAnalogGetTvDacConfiguration_in;

typedef struct NvRmAnalogGetTvDacConfiguration_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmAnalogGetTvDacConfiguration_inout;

typedef struct NvRmAnalogGetTvDacConfiguration_out_t
{
    NvU8 ret_;
} NV_ALIGN(4) NvRmAnalogGetTvDacConfiguration_out;

typedef struct NvRmAnalogGetTvDacConfiguration_params_t
{
    NvRmAnalogGetTvDacConfiguration_in in;
    NvRmAnalogGetTvDacConfiguration_inout inout;
    NvRmAnalogGetTvDacConfiguration_out out;
} NvRmAnalogGetTvDacConfiguration_params;

typedef struct NvRmAnalogInterfaceControl_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmAnalogInterface Interface;
    NvBool Enable;
    void* Config;
    NvU32 ConfigLength;
} NV_ALIGN(4) NvRmAnalogInterfaceControl_in;

typedef struct NvRmAnalogInterfaceControl_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmAnalogInterfaceControl_inout;

typedef struct NvRmAnalogInterfaceControl_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmAnalogInterfaceControl_out;

typedef struct NvRmAnalogInterfaceControl_params_t
{
    NvRmAnalogInterfaceControl_in in;
    NvRmAnalogInterfaceControl_inout inout;
    NvRmAnalogInterfaceControl_out out;
} NvRmAnalogInterfaceControl_params;

static NvError NvRmUsbDetectChargerState_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmUsbDetectChargerState_in *p_in;
    NvRmUsbDetectChargerState_out *p_out;

    p_in = (NvRmUsbDetectChargerState_in *)InBuffer;
    p_out = (NvRmUsbDetectChargerState_out *)((NvU8 *)OutBuffer + OFFSET(NvRmUsbDetectChargerState_params, out) - OFFSET(NvRmUsbDetectChargerState_params, inout));


    p_out->ret_ = NvRmUsbDetectChargerState( p_in->hDevice, p_in->wait );

    return err_;
}

static NvError NvRmUsbIsConnected_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmUsbIsConnected_in *p_in;
    NvRmUsbIsConnected_out *p_out;

    p_in = (NvRmUsbIsConnected_in *)InBuffer;
    p_out = (NvRmUsbIsConnected_out *)((NvU8 *)OutBuffer + OFFSET(NvRmUsbIsConnected_params, out) - OFFSET(NvRmUsbIsConnected_params, inout));


    p_out->ret_ = NvRmUsbIsConnected( p_in->hDevice );

    return err_;
}

static NvError NvRmAnalogGetTvDacConfiguration_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmAnalogGetTvDacConfiguration_in *p_in;
    NvRmAnalogGetTvDacConfiguration_out *p_out;

    p_in = (NvRmAnalogGetTvDacConfiguration_in *)InBuffer;
    p_out = (NvRmAnalogGetTvDacConfiguration_out *)((NvU8 *)OutBuffer + OFFSET(NvRmAnalogGetTvDacConfiguration_params, out) - OFFSET(NvRmAnalogGetTvDacConfiguration_params, inout));


    p_out->ret_ = NvRmAnalogGetTvDacConfiguration( p_in->hDevice, p_in->Type );

    return err_;
}

static NvError NvRmAnalogInterfaceControl_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmAnalogInterfaceControl_in *p_in;
    NvRmAnalogInterfaceControl_out *p_out;
    void*  Config = NULL;

    p_in = (NvRmAnalogInterfaceControl_in *)InBuffer;
    p_out = (NvRmAnalogInterfaceControl_out *)((NvU8 *)OutBuffer + OFFSET(NvRmAnalogInterfaceControl_params, out) - OFFSET(NvRmAnalogInterfaceControl_params, inout));

    if( p_in->ConfigLength && p_in->Config )
    {
        Config = (void*  )NvOsAlloc( p_in->ConfigLength );
        if( !Config )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->Config )
        {
            err_ = NvOsCopyIn( Config, p_in->Config, p_in->ConfigLength );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmAnalogInterfaceControl( p_in->hDevice, p_in->Interface, p_in->Enable, Config, p_in->ConfigLength );

    if(p_in->Config && Config)
    {
        err_ = NvOsCopyOut( p_in->Config, Config, p_in->ConfigLength );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( Config );
    return err_;
}

NvError nvrm_analog_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_analog_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 3:
        err_ = NvRmUsbDetectChargerState_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmUsbIsConnected_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmAnalogGetTvDacConfiguration_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmAnalogInterfaceControl_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
