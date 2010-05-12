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
#include "nvrm_pcie.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmUnmapPciMemory_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmPhysAddr mem;
    NvU32 size;
} NV_ALIGN(4) NvRmUnmapPciMemory_in;

typedef struct NvRmUnmapPciMemory_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmUnmapPciMemory_inout;

typedef struct NvRmUnmapPciMemory_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmUnmapPciMemory_out;

typedef struct NvRmUnmapPciMemory_params_t
{
    NvRmUnmapPciMemory_in in;
    NvRmUnmapPciMemory_inout inout;
    NvRmUnmapPciMemory_out out;
} NvRmUnmapPciMemory_params;

typedef struct NvRmMapPciMemory_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmPciPhysAddr mem;
    NvU32 size;
} NV_ALIGN(4) NvRmMapPciMemory_in;

typedef struct NvRmMapPciMemory_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmMapPciMemory_inout;

typedef struct NvRmMapPciMemory_out_t
{
    NvRmPhysAddr ret_;
} NV_ALIGN(4) NvRmMapPciMemory_out;

typedef struct NvRmMapPciMemory_params_t
{
    NvRmMapPciMemory_in in;
    NvRmMapPciMemory_inout inout;
    NvRmMapPciMemory_out out;
} NvRmMapPciMemory_params;

typedef struct NvRmRegisterPcieLegacyHandler_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvU32 function_device_bus;
    NvOsSemaphoreHandle sem;
    NvBool InterruptEnable;
} NV_ALIGN(4) NvRmRegisterPcieLegacyHandler_in;

typedef struct NvRmRegisterPcieLegacyHandler_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmRegisterPcieLegacyHandler_inout;

typedef struct NvRmRegisterPcieLegacyHandler_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmRegisterPcieLegacyHandler_out;

typedef struct NvRmRegisterPcieLegacyHandler_params_t
{
    NvRmRegisterPcieLegacyHandler_in in;
    NvRmRegisterPcieLegacyHandler_inout inout;
    NvRmRegisterPcieLegacyHandler_out out;
} NvRmRegisterPcieLegacyHandler_params;

typedef struct NvRmRegisterPcieMSIHandler_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvU32 function_device_bus;
    NvU32 index;
    NvOsSemaphoreHandle sem;
    NvBool InterruptEnable;
} NV_ALIGN(4) NvRmRegisterPcieMSIHandler_in;

typedef struct NvRmRegisterPcieMSIHandler_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmRegisterPcieMSIHandler_inout;

typedef struct NvRmRegisterPcieMSIHandler_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmRegisterPcieMSIHandler_out;

typedef struct NvRmRegisterPcieMSIHandler_params_t
{
    NvRmRegisterPcieMSIHandler_in in;
    NvRmRegisterPcieMSIHandler_inout inout;
    NvRmRegisterPcieMSIHandler_out out;
} NvRmRegisterPcieMSIHandler_params;

typedef struct NvRmReadWriteConfigSpace_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvU32 bus_number;
    NvRmPcieAccessType type;
    NvU32 offset;
    NvU8  * Data;
    NvU32 DataLen;
} NV_ALIGN(4) NvRmReadWriteConfigSpace_in;

typedef struct NvRmReadWriteConfigSpace_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmReadWriteConfigSpace_inout;

typedef struct NvRmReadWriteConfigSpace_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmReadWriteConfigSpace_out;

typedef struct NvRmReadWriteConfigSpace_params_t
{
    NvRmReadWriteConfigSpace_in in;
    NvRmReadWriteConfigSpace_inout inout;
    NvRmReadWriteConfigSpace_out out;
} NvRmReadWriteConfigSpace_params;

static NvError NvRmUnmapPciMemory_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmUnmapPciMemory_in *p_in;

    p_in = (NvRmUnmapPciMemory_in *)InBuffer;


    NvRmUnmapPciMemory( p_in->hDeviceHandle, p_in->mem, p_in->size );

    return err_;
}

static NvError NvRmMapPciMemory_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmMapPciMemory_in *p_in;
    NvRmMapPciMemory_out *p_out;

    p_in = (NvRmMapPciMemory_in *)InBuffer;
    p_out = (NvRmMapPciMemory_out *)((NvU8 *)OutBuffer + OFFSET(NvRmMapPciMemory_params, out) - OFFSET(NvRmMapPciMemory_params, inout));


    p_out->ret_ = NvRmMapPciMemory( p_in->hDeviceHandle, p_in->mem, p_in->size );

    return err_;
}

static NvError NvRmRegisterPcieLegacyHandler_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmRegisterPcieLegacyHandler_in *p_in;
    NvRmRegisterPcieLegacyHandler_out *p_out;
    NvOsSemaphoreHandle sem = NULL;

    p_in = (NvRmRegisterPcieLegacyHandler_in *)InBuffer;
    p_out = (NvRmRegisterPcieLegacyHandler_out *)((NvU8 *)OutBuffer + OFFSET(NvRmRegisterPcieLegacyHandler_params, out) - OFFSET(NvRmRegisterPcieLegacyHandler_params, inout));

    if( p_in->sem )
    {
        err_ = NvOsSemaphoreUnmarshal( p_in->sem, &sem );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }

    p_out->ret_ = NvRmRegisterPcieLegacyHandler( p_in->hDeviceHandle, p_in->function_device_bus, sem, p_in->InterruptEnable );

clean:
    NvOsSemaphoreDestroy( sem );
    return err_;
}

static NvError NvRmRegisterPcieMSIHandler_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmRegisterPcieMSIHandler_in *p_in;
    NvRmRegisterPcieMSIHandler_out *p_out;
    NvOsSemaphoreHandle sem = NULL;

    p_in = (NvRmRegisterPcieMSIHandler_in *)InBuffer;
    p_out = (NvRmRegisterPcieMSIHandler_out *)((NvU8 *)OutBuffer + OFFSET(NvRmRegisterPcieMSIHandler_params, out) - OFFSET(NvRmRegisterPcieMSIHandler_params, inout));

    if( p_in->sem )
    {
        err_ = NvOsSemaphoreUnmarshal( p_in->sem, &sem );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }

    p_out->ret_ = NvRmRegisterPcieMSIHandler( p_in->hDeviceHandle, p_in->function_device_bus, p_in->index, sem, p_in->InterruptEnable );

clean:
    NvOsSemaphoreDestroy( sem );
    return err_;
}

static NvError NvRmReadWriteConfigSpace_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmReadWriteConfigSpace_in *p_in;
    NvRmReadWriteConfigSpace_out *p_out;
    NvU8  *Data = NULL;

    p_in = (NvRmReadWriteConfigSpace_in *)InBuffer;
    p_out = (NvRmReadWriteConfigSpace_out *)((NvU8 *)OutBuffer + OFFSET(NvRmReadWriteConfigSpace_params, out) - OFFSET(NvRmReadWriteConfigSpace_params, inout));

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

    p_out->ret_ = NvRmReadWriteConfigSpace( p_in->hDeviceHandle, p_in->bus_number, p_in->type, p_in->offset, Data, p_in->DataLen );

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
    return err_;
}

NvError nvrm_pcie_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pcie_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 4:
        err_ = NvRmUnmapPciMemory_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmMapPciMemory_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmRegisterPcieLegacyHandler_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmRegisterPcieMSIHandler_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmReadWriteConfigSpace_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
